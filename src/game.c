/*
 * game.c - level setup, actor spawning from map markers, and the main
 * per-frame game loop.   (seg 0097: C243..C287, DE26, E3E0, E655, ECF4)
 */
#include "dn1.h"
#include <stdio.h>
#include <string.h>

/* actor routines (actors*.c) */
void act_greyboxes(void); void act_conveyors(void); void act_boots(void); void act_rockets(void);
void act_claws(void); void act_flamers_r(void); void act_flamers_l(void); void act_floatbots(void);
void act_hopbots(void); void act_wheelbots(void); void act_firewheels(void); void act_guns(void);
void act_minibots(void); void act_dynamite(void); void act_snake(void); void act_soda(void);
void act_biters(void); void act_turkeys(void); void act_bridge(void); void act_ffields_h(void);
void act_fans(void); void act_footballs(void); void act_joysticks(void); void act_floppies(void);
void act_robohand(void); void act_ffields_v(void); void act_helis(void); void act_balloons(void);
void act_molecules(void); void act_signs(void); void act_reactor(void); void act_floorspikes(void);
void act_flags(void); void act_radios(void); void act_tele_a(void); void act_tele_b(void);
void act_bombs(void); void act_card(void); void act_walkway(void); void act_letters(void);
void act_rabbits(void); void act_firemonsters(void); void act_boss(void); void act_keys(void);
void act_keydoors(void); void act_static_bombs(void); void act_spikes(void);
void draw_mirrors(void);

/* ui.c */
void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);
void ui_wait_cursor(int x, int row);
void ui_clear_page0(void);
void ui_frame(void);
void ui_status_panel(void);
void ui_get_ready(void);
void ui_marquee(void);
void ui_flip(void);
void ui_fade_in(int d);
void ui_fade_grey(int d);
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
int  restore_game(int slot);              /* 108E:219D */
void help_menu(int x);                    /* 0097:037E */
void enter_high_score(void);              /* 0097:0928 */
void ending_sequence(void);               /* 0097:E947 */
void joystick_read(int calib);            /* 0097:0C52 */
void joystick_buttons(void);              /* 0097:10A5 */
void save_game(uint8_t slot);             /* 0097:0151 */

/* ------------------------------------------------------------------ */
/* 0097:C287  scan the map for actor markers (cells >= 3000h)          */
/* ------------------------------------------------------------------ */
void spawn_level_actors(int rows)
{
    unsigned row, col;
    for (row = 0; row < (unsigned)rows; row++) {
        for (col = 0; col < 0x80; col++) {
            uint16_t o = (uint16_t)((row << 8) + (col << 1));
            uint16_t id = MAP16(o);
            int16_t X = (int16_t)(col * 2 + 2), Y = (int16_t)(row << 7);
            int k;
#define LEFT()   (MAP16(o) = MAP16(o - 2))
#define RIGHT()  (MAP16(o) = MAP16(o + 2))
#define ABOVE()  (MAP16(o) = MAP16(o - 0x100))
#define BELOW()  (MAP16(o) = MAP16(o + 0x100))
#define OBJ6(a, n, S)       do { a[n].s = (S); a[n].x = X; a[n].y = Y; } while (0)
#define OBJ8(a, n, S, T)    do { a[n].s = (S); a[n].x = X; a[n].y = Y; a[n].t = (T); } while (0)
            if (id < 0x3000 || id - 0x3000 > 0x59) continue;
            switch (id - 0x3000) {
            case 0x00: OBJ6(greyboxes, n_greybox, 1); register_actor(act_greyboxes); LEFT(); n_greybox++; break;
            case 0x01:
                elevators[n_elevators].s = 1; elevators[n_elevators].x = X;
                elevator_under[n_elevators] = (int16_t)MAP16(o - 0x100);
                elevators[n_elevators].y = Y;
                MAP16(o) = 0x1AE0;
                n_elevators++;
                break;
            case 0x02: case 0x04: {
                Conveyor *c = &conveyors[n_conveyors];
                uint16_t end = (id == 0x3002) ? 0x3003 : 0x3005;
                c->len_unused = 0; c->x = (int16_t)(col + 2); c->y = Y;
                for (k = 0; MAP16(o + k * 2) != end; k++) ;
                c->len = (int16_t)(k * 2);
                c->dir = (id == 0x3002) ? 0 : 1;
                register_actor(act_conveyors);
                if (id == 0x3002) MAP16(o) = 0x1820;
                n_conveyors++;
                break;
            }
            case 0x06: boots_x = X; boots_y = Y; boots_state = 0; register_actor(act_boots); LEFT(); break;
            case 0x07: OBJ6(rockets, n_rockets, 1); register_actor(act_rockets); rockets_present = 1; ABOVE(); n_rockets++; break;
            case 0x08: claws_x = X; claws_y = Y; claws_state = 0; register_actor(act_claws); LEFT(); break;
            case 0x09: OBJ6(flamers_r, n_flamer_r, 1); register_actor(act_flamers_r); RIGHT(); n_flamer_r++; break;
            case 0x0A: OBJ6(flamers_l, n_flamer_l, 1); register_actor(act_flamers_l); LEFT(); n_flamer_l++; break;
            case 0x0B: OBJ8(floatbots, n_floatbots, 1, 0); register_actor(act_floatbots); LEFT(); n_floatbots++; break;
            case 0x0C: OBJ8(hopbots, n_hopbots, 1, 0); hopbot_aux[n_hopbots] = 0; register_actor(act_hopbots); LEFT(); n_hopbots++; break;
            case 0x0D: OBJ6(wheelbots, n_wheelbots, 1); wheelbot_aux[n_wheelbots] = 0; register_actor(act_wheelbots); LEFT(); n_wheelbots++; break;
            case 0x0E: OBJ6(firewheels, n_firewheels, 1); firewheel_aux[n_firewheels] = 0; register_actor(act_firewheels); LEFT(); n_firewheels++; break;
            case 0x0F: OBJ8(guns, n_guns, 1, 0); register_actor(act_guns); LEFT(); n_guns++; break;
            case 0x10: OBJ8(minibots, n_minibots, 1, 0); register_actor(act_minibots); LEFT(); n_minibots++; break;
            case 0x11: exit_stage = 0; exit_present = 1; exit_x = X; exit_y = (int16_t)(Y + 2); LEFT(); break;
            case 0x12: {
                Obj12 *d = &dynamite[n_dynamite];
                d->s = 1; d->x = X; d->y = Y; d->t = 0; d->u = 1; d->v = 1;
                register_actor(act_dynamite); LEFT(); n_dynamite++;
                break;
            }
            case 0x13:
                bonus_5310 = 0; snake_present = 1; snake_step = 0;
                for (k = 9; k > 0; k--) {
                    snake_x[k] = (int16_t)((col << 1) + (k << 1));
                    snake_y[k] = (int16_t)(Y + 2);
                    snake_alive[k] = 1;
                }
                register_actor(act_snake); LEFT();
                break;
            case 0x14: mirrors[n_mirrors].x = X; mirrors[n_mirrors].y = Y; n_mirrors++; LEFT(); break;
            case 0x15: case 0x52:
                OBJ6(soda, n_soda, (id == 0x3052) ? 2 : 1); n_soda++; LEFT(); register_actor(act_soda); break;
            case 0x16: case 0x17:
                if (id == 0x3016) { biters[n_biters].s = 1; RIGHT(); }
                else              { biters[n_biters].s = 3; LEFT(); }
                biters[n_biters].x = X; biters[n_biters].y = Y; n_biters++;
                register_actor(act_biters);
                break;
            case 0x18: case 0x51:
                OBJ6(turkeys, n_turkey, (id == 0x3051) ? 2 : 1); n_turkey++; LEFT(); register_actor(act_turkeys); break;
            case 0x19:
                bridge_col = (int16_t)(col + 2); bridge_row = (int16_t)row;
                bridge_present = 1; bridge_state = 0;
                bridge_x = (int16_t)(col + 2); bridge_y = Y;
                for (k = 0; MAP16(o + k * 2 + 2) < 0x1800; k += 2) {
                    MAP16(o + k * 2) = 0x21A0;
                    MAP16(o + k * 2 + 2) = 0x21C0;
                }
                bridge_len = (int16_t)(k * 2);
                MAP16(o) = 0x21A0;
                register_actor(act_bridge);
                break;
            case 0x1A:
                ffields_h[n_ffield_h].x = X; ffields_h[n_ffield_h].y = Y; ffields_h[n_ffield_h].on = 1;
                n_ffield_h++; register_actor(act_ffields_h); ABOVE();
                break;
            case 0x1B: case 0x1C:
                if (id == 0x301B) { fans[n_fans].s = 1; LEFT(); } else { fans[n_fans].s = 2; RIGHT(); }
                fans[n_fans].x = X; fans[n_fans].y = Y; fans[n_fans].t = 0; n_fans++;
                register_actor(act_fans);
                break;
            case 0x1D: case 0x50: OBJ8(footballs, n_football, 1, id == 0x3050); n_football++; LEFT(); register_actor(act_footballs); break;
            case 0x1E: case 0x54: OBJ8(joysticks, n_joystick, 1, id == 0x3054); n_joystick++; LEFT(); register_actor(act_joysticks); break;
            case 0x1F: case 0x53: OBJ8(floppies, n_floppy, 1, id == 0x3053); n_floppy++; LEFT(); register_actor(act_floppies); break;
            case 0x20: robohand_x = X; robohand_y = Y; robohand_state = 0; register_actor(act_robohand); LEFT(); break;
            case 0x21:
                ffields_v[n_ffield_v * 2] = X; ffields_v[n_ffield_v * 2 + 1] = Y;
                if (n_ffield_v * 2 + 2 < 200) ffields_v[n_ffield_v * 2 + 2] = 1; else rabbits[0].s = 1;
                n_ffield_v++; register_actor(act_ffields_v); LEFT();
                break;
            case 0x22: {
                Obj12 *h = &helis[n_helis];
                h->s = 1; h->x = X; h->y = Y; h->t = 0; h->u = 4; h->v = 0;
                n_helis++; register_actor(act_helis); LEFT();
                break;
            }
            case 0x23: OBJ6(balloons, n_balloons, 1); n_balloons++; register_actor(act_balloons); LEFT(); break;
            case 0x24: bonus_d57c = 1; OBJ6(cameras, n_cameras, 1); n_cameras++; BELOW(); break;
            case 0x25: OBJ6(decor, n_decor, 0x7EE0); n_decor++; ABOVE(); break;
            case 0x26: case 0x27:
                OBJ6(decor, n_decor, (id - 0x3026) * 0xA0 + 0x7F80); n_decor++;
                if (id == 0x3026) LEFT(); else RIGHT();
                break;
            case 0x28: OBJ6(decor, n_decor, 0x80C0); n_decor++; MAP16(o) = 0; break;
            case 0x29: OBJ6(molecules, n_molecules, 1); n_molecules++; register_actor(act_molecules); LEFT(); break;
            case 0x2A: OBJ6(signs, n_signs, 1); n_signs++; register_actor(act_signs); LEFT(); break;
            case 0x2B:
                reactor_alive = 0; reactor_x = X; reactor_y = Y; reactor_present = 1; reactor_hp = 0x0A;
                register_actor(act_reactor); LEFT();
                break;
            case 0x2C: floorspikes[n_spikes].x = X; floorspikes[n_spikes].y = Y; n_spikes++; register_actor(act_floorspikes); ABOVE(); break;
            case 0x2D: case 0x55: OBJ6(flags, n_flags, (id == 0x3055) ? 2 : 1); n_flags++; register_actor(act_flags); LEFT(); break;
            case 0x2E: case 0x56: OBJ6(radios, n_radios, (id == 0x3056) ? 2 : 1); n_radios++; register_actor(act_radios); LEFT(); break;
            case 0x2F: tele_a_present = 1; tele_a_x = X; tele_a_y = Y; tele_a_busy = 0; tele_a_state = 0; register_actor(act_tele_a); LEFT(); break;
            case 0x30: tele_b_present = 1; tele_b_x = X; tele_b_y = Y; tele_b_busy = 0; tele_b_state = 0; register_actor(act_tele_b); LEFT(); break;
            case 0x31: OBJ6(bombs, n_bombs, -6); register_actor(act_bombs); LEFT(); n_bombs++; break;
            case 0x32:
                scroll_x = (int16_t)((col << 1) - 14);
                scroll_y = (int16_t)(Y - 0x180);
                player_y = 0x40;
                LEFT();
                break;
            case 0x33: have_card = 0; card_present = 1; card_x = X; card_y = Y; card_state = 0; register_actor(act_card); LEFT(); break;
            case 0x34: cardreader_present = 1; cardreader_x = X; cardreader_y = Y; LEFT(); break;
            case 0x35: handreader_present = 1; handreader_alarm = 0; handreader_used = 0; handreader_x = X; handreader_y = Y; LEFT(); break;
            case 0x36: walkway_present = 1; walkway_go = 0; walkway_x = X; walkway_y = Y; MAP16(o) = 0x2FE0; register_actor(act_walkway); break;
            case 0x37: case 0x38: case 0x39: case 0x3A: {
                static const int16_t ch[4] = {'D', 'U', 'K', 'E'};
                bonus_530c = 0;
                OBJ8(letters, n_letters, ch[id - 0x3037], 0);
                LEFT(); register_actor(act_letters); n_letters++;
                break;
            }
            case 0x3B: OBJ8(rabbits, n_rabbits, 1, 0); n_rabbits++; register_actor(act_rabbits); LEFT(); break;
            case 0x3C: OBJ6(firemonsters, n_firemonsters, 1); n_firemonsters++; register_actor(act_firemonsters); LEFT(); break;
            case 0x3D: case 0x3E: case 0x3F:
                OBJ6(decor, n_decor, (id - 0x303D) * 0xA0 + 0x9D80); n_decor++; MAP16(o) = 0; break;
            case 0x40: monitor_present = 1; monitor_x = X; monitor_y = Y; LEFT(); break;
            case 0x41: protonscreen_present = 1; protonscreen_state = 0; protonscreen_x = X; protonscreen_y = Y; LEFT(); break;
            case 0x42: case 0x43:
                boss_present = 1; boss_x = X; boss_y = Y; boss_mode = 1;
                boss_ep1 = (id == 0x3043);
                boss_hp = 0x0A; boss_dead = 0;
                LEFT(); register_actor(act_boss);
                break;
            case 0x44: case 0x45: case 0x46: case 0x47:
                OBJ6(keys, n_keys, (id - 0x3043) * 0xA0); n_keys++; LEFT(); register_actor(act_keys); break;
            case 0x48: case 0x49: case 0x4A: case 0x4B:
                OBJ8(locks, n_locks, (id - 0x3046) * 0xA0, (id - 0x3046) * 0xA0); n_locks++; LEFT(); break;
            case 0x4C: case 0x4D: case 0x4E: case 0x4F:
                OBJ8(keydoors, n_keydoors, (id - 0x304B) * 0xA0, 0); n_keydoors++;
                MAP16(o) = 0x1820; register_actor(act_keydoors);
                break;
            case 0x57: OBJ6(static_bombs, n_static_bombs, 1); n_static_bombs++; LEFT(); register_actor(act_static_bombs); break;
            case 0x58: case 0x59:
                floorspikes2[n_floorspikes].x = X; floorspikes2[n_floorspikes].y = Y;
                register_actor(act_spikes);
                if (id == 0x3058) { floorspikes2[n_floorspikes].s = 0x5C80; ABOVE(); }
                else              { floorspikes2[n_floorspikes].s = 0x5D20; BELOW(); }
                n_floorspikes++;
                break;
            default: break;
            }
        }
    }
#undef LEFT
#undef RIGHT
#undef ABOVE
#undef BELOW
#undef OBJ6
#undef OBJ8
}

/* 0097:DE26  read the 128x90 cell map */
static void read_map(const char *name)
{
    FILE *f = fopen(game_path(name), "rb");
    if (f) { size_t n = fread(mapbuf, 2, 0x2D00, f); (void)n; fclose(f); }
}

/* 0097:10FB  load a tile file into the VRAM tile cache at A000:4000+vram */
void load_tiles(const char *name, uint16_t vram, int bytes);   /* files.c */

/* ------------------------------------------------------------------ */
/* 0097:E3E0  reset per-level state, load map + backdrop, spawn actors */
/* ------------------------------------------------------------------ */
void load_level(const char *name)
{
    dl_reset();                          /* (port) smooth motion starts afresh */
    clear_eshots();
    shots_spent_clear();                 /* (port) */
    level_done = 0;
    anim_tick = 0;
    shots_blocked = 0;
    player_hanging = 0;
    player_facing = 4;
    player_walkframe = 0;
    player_climb = 0;
    n_greybox = 0;
    max_debris = 0x0C;
    max_eshots = 3;
    max_dust = 7;
    max_explosions = 0x12;
    max_sparks = 8;
    n_elevators = 0; n_conveyors = 0;
    n_rockets = 0; n_flamer_r = 0; n_flamer_l = 0; n_floatbots = 0;
    floatbot_anim = 0; floatbot_fire = 0;
    n_hopbots = 0; n_wheelbots = 0; n_firewheels = 0;
    n_actor_fns = 0;
    n_guns = 0; n_minibots = 0;
    exit_stage = 0; exit_present = 0;
    n_dynamite = 0; snake_present = 0;
    n_mirrors = 0; n_soda = 0; n_biters = 0; n_turkey = 0;
    bridge_present = 0;
    n_ffield_h = 0; n_fans = 0; n_football = 0; n_joystick = 0; n_floppy = 0; n_ffield_v = 0;
    n_helis = 0; n_balloons = 0; n_cameras = 0; n_decor = 0; n_molecules = 0; n_signs = 0;
    reactor_alive = 1; reactor_present = 0;
    n_spikes = 0; n_flags = 0; n_radios = 0;
    tele_a_busy = 0; tele_a_present = 0; tele_b_busy = 0; tele_b_present = 0;
    n_bombs = 0;
    cardreader_present = -1;
    handreader_present = 0; walkway_present = 0; n_letters = 0; letter_count = 0;
    n_rabbits = 0; n_firemonsters = 0;
    monitor_present = 0; protonscreen_present = 0; boss_present = 0;
    player_hurt = 0;
    hurt_timer = 0x15;
    have_card = 0; card_present = 0;
    n_locks = 0; n_keys = 0; n_keydoors = 0; ps_seen = 0;
    reactor_hp = 0x0A;
    n_static_bombs = 0;
    boss_frame = 0; boss_wait = 0; boss_hop = 0; boss_hopping = 0; boss_hop_left = 0;
    boss_delay = 0x0A;
    boss_dead = 0; boss_ep1 = 0; n_floorspikes = 0; boss_end_timer = 0;

    read_map(name);
    WRITE_MODE_NORMAL();
    if (level_index % 2 == 0) {
        load_tiles(ds_farstr((uint16_t)(0xB47 + level_index * 4)), 0x4000, 0x5140);
        load_tiles(ds_farstr((uint16_t)(0xB47 + (level_index + 1) * 4)), 0x8100, 0x5140);
    }
    if (level_index % 2 == 0) {
        bonus_d247 = 1;
        bonus_5312 = 0;
        clear_scores();
    }
    spawn_level_actors(0x5A);
    recenter_y = 1;
    flip_dir = 0;
    flip_timer = 0;
    have_key_r = have_key_g = have_key_b = have_key_p = 0;
    draw_inventory();
}

/* ------------------------------------------------------------------ */
/* 0097:E655  start (or restart) a game                                */
/* ------------------------------------------------------------------ */
void new_game(void)
{
    int32_t sc;
    firewheel_frame = 0; firewheel_cycle = 0;
    player_facing = 4;
    player_walkframe = 0;
    player_climb = 0;
    boots_x = 0; boots_y = 0; have_boots = 0; boots_state = 0;
    claws_state = 0; have_claws = 0;
    robohand_x = 0; robohand_y = 0; have_robohand = 0; robohand_state = 0;
    card_x = 0; card_y = 0; have_card = 0; card_state = 0;
    inv_x = 0; inv_y = 0;
    bonus_5310 = 0; bonus_530c = 0;
    max_shots = 1;
    rockets_present = 0;
    player_grounded = 1; player_falling = 0; player_jumpstep = 0;
    player_x = 0x10; player_y = 0x30;
    scroll_x = 0; scroll_y = 0;
    score = 0;
    bonus_d57c = 0; bonus_4ed6 = 0; bonus_531c = 0;
    level_index = 0;
    health = 9;
    if (restoring_game) {
        restore_game(save_slot);
        if (quit_flag) return;
        ui_fade_grey(0x14);
        ui_clear_page0();
        restoring_game = 0;
        if (have_boots)    inventory_add(object_gfx + 0x640);
        if (have_claws)    inventory_add(object_gfx + 0xB40);
        if (have_robohand) inventory_add(object_gfx + 0x2760);
    } else {
        ui_clear_page0();
    }
    port_level_intro = 1;                /* (port) widescreen: lay the play screen out wide already */
    ui_status_panel();
    ui_get_ready();
    ui_frame();
    ui_fade_in(0x14);
    if (demo_record == 2 || demo_play == 2) level_index = 0x14;
    load_level(ds_farstr((uint16_t)(0x3A4 + level_index * 4)));
    sc = score; score = 0;
    add_score(0);
    score = sc;
    add_score(0);
    draw_firepower();
    draw_health();
}
