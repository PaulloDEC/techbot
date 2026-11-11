/*
 * loop.c - the main game loop (0097:ECF4) and the level-end bonus tally.
 *
 * One iteration = one game frame.  The original paces frames by busy-waiting
 * until timer_ticks (145 Hz) reaches game_speed + 9, i.e. ~16 frames/s at the
 * default speed 0.  Everything below that wait is fixed-step simulation and
 * drawing into the back page, followed by a page flip.
 *
 * PORTING NOTE: this is where a modern port decouples simulation from
 * rendering - run this logic at the original 145/9 Hz tick and render
 * interpolated frames at the display rate (see PORTING.md).
 */
#include "dn1.h"

void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);
void ui_wait_cursor(int x, int row);
void ui_clear_page0(void);
void ui_frame(void);
void ui_get_ready(void);
void ui_fade_in(int d);
void ui_marquee(void);
int  ui_confirm_restart(void);
int  ui_confirm_quit(int x);
void ui_you_died(void);
void ui_cheat_msg(void);
void ui_toggle_sound(int x);
void ui_toggle_hint(int x);
void ui_pause(int x);
void ui_high_scores(int x);
void inventory_add(const uint8_t *gfx);
void inventory_draw_card(void);
void help_menu(int x);
void enter_high_score(void);
void ending_sequence(void);
void joystick_read(int calib);
void joystick_buttons(void);
void save_game(uint8_t slot);
void update_shots(void);
void draw_mirrors(void);
void ui_status_panel(void);

/* 6 input bytes per frame in a demo: up, down, left, right, fire, jump */
static void demo_get(const uint8_t *b)
{
    in_up = b[0]; in_down = b[1]; in_left = b[2]; in_right = b[3]; in_fire = b[4]; in_jump = b[5];
}
static void demo_put(uint8_t *b)
{
    b[0] = in_up; b[1] = in_down; b[2] = in_left; b[3] = in_right; b[4] = in_fire; b[5] = in_jump;
}

/* resynchronise after a dialog: the shown page becomes the drawn page */
static void resync_pages(void)
{
    set_draw_page(!draw_page);
    set_display_page(draw_page);
}

/* bonus tally when entering a hallway: two score sprites + 10000 each */
static void bonus_pair(uint16_t a, uint16_t b, int si)
{
    int y = (((player_y - 29) >> 4) << 7) + si + scroll_y + 0x300;
    spawn_score(a, player_x + scroll_x + 4, y);
    spawn_score(b, player_x + scroll_x + 6, y);
}

void (*port_frame_input)(void);         /* (port) see port.h */

void play_level_loop(void)
{
    int demo_done = 0;
    int si;

    marquee_delay = 0;
    marquee_pos = 0;
    marquee_phase = 0;
    timer_ticks = 0;
    game_over = 0;
    in_down = 0; in_up = 0; in_right = 0; in_left = 0; in_jump = 0; in_fire = 0;
    if (demo_play) last_scancode = 0;
    port_qs_request = 0;                 /* (port) ignore presses made before play */
    port_hud_request = 0;
    if (port_resume_qs) {                /* (port) chosen from the restore list */
        port_resume_qs = 0;
        if (!port_quickload()) {         /* unusable: carry on with the new game */
            ui_get_ready(); ui_frame(); ui_fade_in(0x14);
        }
    }

resume:
    for (;;) {
        /* ------------------------------------------------ frame pacing */
        dl_waiting = 1;                  /* (port) smooth motion draws during this wait */
        SPIN_WHILE(game_speed + 9 > (int16_t)timer_ticks);
        dl_waiting = 0;
        timer_ticks = 0;
        pf_frame_hook();                 /* test harness / port hook */
        dl_begin_frame();
        recenter_view();
        if (joystick_mode) {
            if (tele_a_busy == 0 && tele_b_busy == 0) joystick_read(1);
            joystick_buttons();
        }

        /* ------------------------------------------------ demo input */
        if (demo_play == 1) {
            demo_get(demo_user + demo_pos);
            demo_pos += 6;
            if (demo_pos >= 0x425A || demo_pos >= demo_len_user) { demo_play = 0; return; }
        }
        if (demo_record == 1 && demo_pos != 0x425A) {
            demo_put(demo_user + demo_pos);
            demo_pos += 6;
            if (demo_pos >= 0x4259) { demo_record = 0; demo_len_user = demo_pos; }
        }
        if (demo_play == 2) {
            demo_get(demo_my + demo_pos);
            demo_pos += 6;
            if (demo_pos >= 0x425A || demo_pos >= demo_len_my) { demo_play = 0; return; }
        }
        if (demo_record == 2 && demo_pos != 0x425A) {
            demo_put(demo_my + demo_pos);
            demo_pos += 6;
            demo_len_my = demo_pos;
            if (demo_pos >= 0x4259) demo_record = 0;
        }

        /* ------------------------------------------------ simulate + draw */
        /* (port) dl_group tags what each routine draws, for smooth motion */
        if (port_frame_input) port_frame_input();   /* (port) Shrapnel: replays, recording */
        dl_group = 0x80; player_update();
        dl_group = 0x81; update_scores();
        dl_group = 0x82; update_debris();
        dl_group = 0x83; update_dust();
        dl_group = 0x84; update_explosions();
        dl_group = 0x85; update_shots();
        dl_group = 0x86; update_eshots();
        dl_group = 0x87; update_fallers();
        for (si = 0; si < n_actor_fns; si++) { dl_group = (uint8_t)si; actor_fns[si](); }
        dl_group = 0x88;
        pf_test_midframe();              /* test harness hook (no-op in the game) */
        if (demo_play == 2) {
            if (anim_tick == 0 || anim_tick == 0x20) ui_text(ds_str(0x0B0A), 8, 0x0A);
            ui_marquee();
        }

        /* G+O+D (needs the "asp" command-line switch): all items */
        if (key_g && key_o && key_d && cmd_asp) {
            have_robohand = have_claws = have_boots = have_card = 1;
            card_present = 0;
            inventory_add(object_gfx + 0x640);
            inventory_add(object_gfx + 0xB40);
            inventory_add(object_gfx + 0x2760);
            inventory_draw_card();
            health = 9;
            draw_health();
            have_key_p = have_key_b = have_key_g = have_key_r = 1;
            draw_inventory();
            max_shots = 4;
            draw_firepower();
            timer_ticks = 0;
            inv_x = 0;
        }
        /* Backspace+PgDn: the same, with a message */
        if (key_bksp == 1 && key_pgdn == 1) {
            play_sound(0x19);
            ui_box(2, 1, 2);
            ui_cheat_msg();
            SPIN_WHILE(key_bksp != 0 || key_pgdn != 0);
            ui_wait_cursor(0x1A, 6);
            have_key_p = have_key_b = have_key_g = have_key_r = 1;
            draw_inventory();
            max_shots = 4;
            draw_firepower();
            timer_ticks = 0;
            in_right = 0; in_down = 0;
        }

        update_sparks();
        draw_mirrors();
        ui_frame();
        if (demo_record) blit_tile(0x3200, 2);              /* "recording" marker */
        if (demo_play && anim_tick == 0) blit_tile(0x3180, 2);

        /* G+O held with asp: wait for release (pause cheat) */
        if (key_bksp == 1 && key_g) {
            SPIN_WHILE(key_bksp == 1 || key_g != 0);
            SPIN_WHILE(key_bksp == 0 && key_g == 0);
        }
        if (last_scancode == 0x11 && key_g && key_o && cmd_asp) level_done = 1;   /* G+O+W: warp */

        /* ------------------------------------------------ function keys */
        if (last_scancode == 0x23) {                         /* H: hint mode toggle */
            SPIN_WHILE(last_scancode == 0x23);
            ui_toggle_hint(2);
            timer_ticks = 0;
            resync_pages();
            in_right = 0; in_down = 0;
        }
        if (last_scancode == 0x19) {                         /* P: pause */
            ui_pause(2);
            SPIN_WHILE(last_scancode == 0x19);
            timer_ticks = 0;
            resync_pages();
            in_right = 0; in_down = 0;
        }
        if (last_scancode == 0x3B && demo_play == 0) {       /* F1: help menu */
            g_0094 = 0;
            help_menu(2);
            if (g_0094) {
                g_0094 = 0;
                ui_confirm_restart();
                if (last_scancode == 0x15) {
                    if (level_index == 0 && demo_record) { demo_record = 0; demo_len_user = demo_pos; }
                    in_right = 0; in_down = 0;
                    timer_ticks = 0;
                    goto restart;
                }
                timer_ticks = 0;
                resync_pages();
            }
            SPIN_WHILE(last_scancode == 1);
            in_right = 0; in_down = 0;
            timer_ticks = 0;
            resync_pages();
        }
        if (last_scancode == 0x1F && demo_play == 0) {       /* S: sound toggle */
            ui_toggle_sound(2);
            SPIN_WHILE(last_scancode == 0x1F);
            timer_ticks = 0;
            resync_pages();
            in_right = 0; in_down = 0;
        }
        /* (port) quicksave / quickload, handled like the original's toggles.
         * Keys are 0 (off) unless the settings file defines them, so the
         * verification build never takes this path. */
        if (port_hud_request) { port_hud_request = 0; port_toggle_hud(); }   /* (port) */
        if (port_qs_request && demo_play == 0) {
            uint8_t k = port_qs_request;
            port_qs_request = 0;
            if (k == cfg.key_qsave) port_quicksave(); else port_quickload();
            timer_ticks = 0;
            resync_pages();
            /* all six, not just two: after a load the held keys of the
             * moment of saving must not stick */
            in_up = 0; in_down = 0; in_left = 0; in_right = 0; in_fire = 0; in_jump = 0;
        }
        if (key_speed_up && game_speed > 0) { game_speed--; blit_tile(0x3500, 4); }
        if (key_speed_down && game_speed <= 8) { game_speed++; blit_tile(0x3520, 4); }

        /* ------------------------------------------------ page flip */
        dl_end_frame();
        set_draw_page(draw_page);
        draw_page = !draw_page;
        set_display_page(draw_page);
        /* (port) with smooth motion the wait below draws this frame (gliding
         * from the previous one); showing it here first would make every
         * moving thing jump ahead and snap back once per update */
        if (!dl_owns_screen()) pf_present();

        /* ------------------------------------------------ level finished */
        if (level_done == 1) {
            if (demo_record == 1) { demo_record = 0; demo_len_user = demo_pos; }
            if (demo_record == 2) { demo_record = 0; demo_len_my = demo_pos; return; }
            if (demo_play) demo_done = 1;
            if (pack_is_last(level_index)) {     /* (port) a pack's last level: the episode is over */
                pack_finished = 1;
                game_over = 1;
                goto frame_end;
            }
            level_index++;
            load_level(ds_farstr((uint16_t)(0x3A4 + level_index * 4)));
            if (level_index % 2 != 0) {
                si = 0;
                if (bonus_d247) {
                    bonus_pair(0xB40, 0xC80, si); si += 0x100; add_score(10000);
                }
                if (bonus_4ed6 && bonus_d57c) {
                    bonus_pair(0x8C0, 0xA00, si); si += 0x100; add_score(10000);
                    bonus_4ed6 = 0; bonus_d57c = 0;
                }
                if (bonus_531c) {
                    bonus_pair(0xDC0, 0xF00, si); add_score(10000);
                    bonus_531c = 0;
                }
                bonus_d247 = 1;
                if (bonus_rockets && rockets_present) {
                    bonus_pair(0x1040, 0x1180, si); si += 0x100; add_score(10000);
                    bonus_rockets = 0;
                }
                if (bonus_530c) { bonus_pair(0x12C0, 0x1400, si); add_score(10000); si += 0x100; bonus_530c = 0; }
                if (bonus_5310) { bonus_pair(0x1540, 0x1680, si); add_score(10000); si += 0x100; bonus_5310 = 0; }
                if (bonus_5312) { bonus_pair(0x17C0, 0x1900, si); add_score(10000); si += 0x100; bonus_5312 = 0; }
                save_game('T');
            }
        }

        /* ------------------------------------------------ low-health blink */
        if (health < 2) {
            if (draw_page == 0) { blit_tile(0x3540, 0x660); blit_tile(0x3560, 0x662); goto after_blink; }
        } else {
            WRITE_MODE_LATCH();
        }
        blit_tile(0x3480, 0x660); blit_tile(0x34A0, 0x662);
    after_blink:

        /* ------------------------------------------------ death sequence */
        if (death_timer) {
            int PX = player_x + scroll_x;
            int R  = (((player_y - 16) >> 4) << 7) + scroll_y;
            int Rm = 0x80 - (((player_y - 16) >> 4) << 7) + scroll_y;   /* sic */
            death_timer++;
            if (death_timer < 0x0F) play_sound(0x0A);
            switch (death_timer - 1) {
            case 0:  spawn_debris(PX, R); /* fall through */
            case 1: case 6: spawn_debris(PX, R + 0x80); break;
            case 2:  spawn_debris(PX - 2, Rm); /* fall through */
            case 3:  spawn_debris(PX + 2, Rm); break;
            case 4:  spawn_debris(PX - 3, R); /* fall through */
            case 5:  spawn_debris(PX + 3, R); break;
            case 7:  spawn_debris(PX, R); break;
            case 9: case 14:  spawn_debris(PX - 2, Rm); /* fall through */
            case 10: case 15: spawn_debris(PX + 3, Rm); break;
            case 11: case 16: spawn_debris(PX - 3, R); break;
            case 12: case 17: spawn_debris(PX + 2, R); /* fall through */
            case 8: case 13: case 18: spawn_debris(PX, Rm); break;
            default: break;
            }
        }
        if (health == 0 && death_timer == 0) death_timer = 1;

        if ((health == 0 && demo_play == 0 && death_timer == 0x1E) ||
            (last_scancode == 0x44 && level_index % 2 == 0)) {
            if (last_scancode == 0x44) {                     /* F10: restart level? */
                ui_confirm_restart();
                if (last_scancode == 0x15) {
                    if (level_index == 0 && demo_record) { demo_record = 0; demo_len_user = demo_pos; }
                    goto restart;
                }
                timer_ticks = 0;
                resync_pages();
                goto frame_end;
            }
            play_sound(0);
            if (level_index != 0) {
                ui_box(2, 1, 2);
                ui_you_died();
                SPIN_WHILE(timer_ticks < 100);
                timer_ticks = 0;
                do { last_scancode = 0; ui_wait_cursor(0x1A, 6); } while (last_scancode != 0x1C);
                in_down = 0; in_right = 0;
            }
        restart:
            last_scancode = 0;
            set_draw_page(0); ui_frame();
            set_draw_page(1); ui_frame();
            draw_page = 0;
            set_draw_page(draw_page);
            set_display_page(draw_page);
            death_timer = 0;
            quit_flag = 0;
            restoring_game = 1;
            save_slot = 'T';
            if (demo_record) demo_len_user = demo_pos;
            demo_record = 0;
            ui_frame();
            new_game();
            inv_x = 0;
            inv_y = 0;
            if (have_boots)    inventory_add(object_gfx + 0x640);
            if (have_claws)    inventory_add(object_gfx + 0xB40);
            if (have_robohand) inventory_add(object_gfx + 0x2760);
            have_card = 0;
        }
    frame_end:

        /* ------------------------------------------------ loop exit tests */
        if (demo_done == 1) break;
        if (demo_play && (last_scancode != 0 || health == 0)) break;
        if (game_over) {
            ending_sequence();
            set_draw_page(draw_page);
            set_display_page(!draw_page);
            goto end_game;
        }
        if (last_scancode == 1 || last_scancode == 0x10) {   /* Esc / Q */
            SPIN_WHILE(last_scancode == 0x10);
            break;
        }
    }

    if (demo_play) { demo_play = 0; return; }
    if (!ui_confirm_quit(2)) {
        timer_ticks = 0;
        set_draw_page(draw_page);
        set_display_page(!draw_page);
        in_right = 0; in_down = 0;
        goto resume;
    }
    play_sound(8);
    if (!demo_done) {
        if (demo_record == 2) demo_len_my = demo_pos;
        if (demo_record == 1) demo_len_user = demo_pos;
    }
end_game:
    draw_page = 0;
    set_draw_page(0);
    ui_clear_page0();
    set_display_page(0);
    enter_high_score();
    SPIN_WHILE(!(last_scancode & 0x80));
    last_scancode = 0;
    ui_high_scores(7);
    pf_delay_ms(300);
    ui_wait_cursor(0x1F, 0x14);
    timer_ticks = 0;
    demo_record = 0;
    demo_play = 0;
}
