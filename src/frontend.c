/*
 * frontend.c - title screens, main menu, in-game help menu, high scores,
 * level notes, the ending, and other UI routines that are not plain
 * straight-line dialogs (those are generated into ui_dialogs.c).
 *
 * Text is always taken from the user's DN1.EXE by data-segment offset.
 */
#include "dn1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ui.c / ui_dialogs.c */
void ui_box(int x, int row, int h);
void ui_box_px(int x, int y, int h);
void ui_text(const char *s, int x, int row);
void ui_wait_cursor(int x, int row);
void ui_clear_page0(void);
void ui_fade_out(int d);
void ui_fade_in(int d);
void ui_flip(void);
void dlg_main_menu_text(int x);       /* 108E:38BE */
void dlg_help_menu_text(int x);       /* 108E:1A0D */
void dlg_save_prompt(int x);          /* 108E:1241 */
void dlg_restore_prompt(int x);       /* 108E:12AD */
void dlg_saved(void);                 /* 108E:0D39 */
void dlg_not_saved(void);             /* 108E:0D97 */
void dlg_not_available(void);         /* 108E:0E19 */
void dlg_save_hallway_only(int x);    /* 108E:1BB0 */
void dlg_hiscore_banner(void);        /* 108E:23F7 */
void dlg_press_key(void);             /* 108E:2473 */
void dlg_end_1(void); void dlg_end_2(void); void dlg_end_3(void);
void dlg_end_4(void); void dlg_end_5(void);
void dlg_to_be_continued(int x);      /* 108E:452B */
void dlg_story(void);                 /* 108E:062C */
void dlg_order_1(void);               /* 108E:02F4 */
void dlg_order_2(void);               /* 108E:0491 */
void dlg_title_text(void);            /* 108E:3FE1 */
void dlg_instructions(int x);         /* 108E:1EE1 */
void dlg_game_setup(int x);           /* 0097:008A */
void ui_patches(int x);               /* port_options.c */
void ui_toggle_sound(int x);
void ui_toggle_hint(int x);
int  dlg_confirm_quit(int x);         /* 108E:3D1E */
void dlg_instructions_title(void);   /* 108E:3FED */
void dlg_keys_help(int x);           /* 108E:1BFB */
void save_game(uint8_t slot);
int  restore_game(int slot);

/* ------------------------------------------------------------------
 * full-screen pictures (108E:315D credits, 32B6 title, 340F bad guy,
 * 3568 Duke, 36C1 ending): copy the 4 planes into page 0
 * ---------------------------------------------------------------- */
static void show_picture(int idx)
{
    int p, i;                            /* (ui_clear_page0 below also resets smooth motion) */
    const uint8_t *img = screen_img[idx];
    ui_clear_page0();
    WRITE_MODE_NORMAL();
    set_draw_page(0);
    set_display_page(1);
    for (p = 0; p < 4; p++)
        for (i = 0; i < 0x1F40; i++) ega_planes[p][i] = img ? img[p * 0x1F40 + i] : 0;
    port_picture = idx;                  /* (port) replacement art can stand in for it */
    port_text_tile = -1;
    memset(ega_written, 0, sizeof ega_written);   /* nothing drawn over it yet */
    set_display_page(0);
    pf_present();
}
void ui_show_title(void)   { show_picture(0); }
void ui_show_end(void)     { show_picture(1); }
void ui_show_badguy(void)  { show_picture(2); }
void ui_show_duke(void)    { show_picture(3); }
void ui_show_credits(void) { show_picture(4); }

/* 108E:3B79  card slot emptied after use */
void card_used(void)
{
    dl_hud++;                            /* (port) status panel: never clipped */
    set_draw_page(draw_page);
    draw_sprite(object_gfx + 0x4C40, 0x24, inv_y + 0xA0);
    set_draw_page(!draw_page);
    draw_sprite(object_gfx + 0x4C40, 0x24, inv_y + 0xA0);
    dl_hud--;
}

/* 108E:1058 / 10F0 / 11F2  in-game toggles */
void ui_toggle_sound(int x)
{
    ui_box(x, 3, 2);
    WRITE_MODE_NORMAL();
    SPIN_WHILE(last_scancode == 0x1F);
    ui_text(ds_str(0x2F32), x, 7);
    sound_enabled = !sound_enabled;
    /* (port) the in-game S key with the volume set to OFF: back to the last
     * audible level (full if there was none) */
    if (sound_enabled && cfg.volume == 0) cfg.volume = port_last_volume ? port_last_volume : 5;
    if (sound_enabled) ui_text(ds_str(0x2F46), x, 9);
    else { ui_text(ds_str(0x2F5B), x, 9); pf_speaker(0); }
    pf_delay_ms(300);
    ui_wait_cursor(x + 0x18, 0x0A);
    SPIN_WHILE(last_scancode == 0x1F);
}
void ui_toggle_hint(int x)
{
    uint8_t v;
    ui_box(x, 3, 2);
    WRITE_MODE_NORMAL();
    SPIN_WHILE(last_scancode == 0x23);
    ui_text(ds_str(0x2F71), x, 7);
    hints_on = !hints_on;
    v = hints_on ? 1 : 0;
    ui_text(ds_str(hints_on ? 0x2F85 : 0x2F9A), x, 9);
    hint_5a11 = hint_5c6b = hint_5554 = hint_637e = hint_5ec8 = hint_555a = v;
    hint_555c = hint_5553 = hint_555b = hint_68f7 = hint_5555 = hint_5c6a = v;
    ui_wait_cursor(x + 0x18, 0x0A);
    SPIN_WHILE(last_scancode == 0x23);
}
void ui_pause(int x)
{
    ui_box(x, 3, 1);
    WRITE_MODE_NORMAL();
    ui_text(ds_str(0x2FB0), x, 7);
    SPIN_WHILE(last_scancode == 0x19);
    ui_wait_cursor(x + 0x18, 8);
}

/* 0097:B118  show this level's 4-line message (table of far pointers at
 * DS:0BA1, four per level) and wait for ENTER */
void show_level_note(void)
{
    int si = level_index * 4, k;
    const char (*own)[PACK_NOTE_W + 1] = pack_note(level_index);   /* (port) a pack's own text */
    if (!own) own = patch_note(level_index);   /* (port) or a patch's */
    ui_box(2, 1, 3);
    for (k = 0; k < 4; k++) {
        /* (a pack's entry without text of its own: the stock one, while there is one) */
        const char *s = own ? own[k] : pack.on && level_index >= 20 ? NULL : ds_farstr((uint16_t)(0xBA1 + (si + k) * 4));
        if (s) ui_text(s, 3, 3 + k);
    }
    ui_text(ds_str(0x08AE), 3, 8);
    do { ui_wait_cursor(0x1A, 8); } while (last_scancode != 0x1C);
    in_right = 0; in_down = 0;
    set_draw_page(!draw_page);
    set_display_page(draw_page);
    timer_ticks = 0;
}

/* ------------------------------------------------------------------
 * high scores: HIGHS.DN1 holds 10 "%ld\n%s\n" pairs (108E:24B1 load,
 * 1E1B save, 1D40 show, 0097:0928 enter)
 * ---------------------------------------------------------------- */
int32_t hiscore[10];          /* 4FA2 */
char    hiname[10][11];       /* 5008 */

void load_high_scores(void)
{
    FILE *f = fopen(game_path(pack_file_name(ds_str(0x38D2))), "r");
    int i;
    if (!f) {
        static const int32_t def[5] = {40000, 30000, 20000, 10000, 500};
        static const uint16_t nm[5] = {0x38DE, 0x38E3, 0x38E9, 0x38F0, 0x38F3};
        for (i = 0; i < 5; i++) { hiscore[i] = def[i]; strncpy(hiname[i], ds_str(nm[i]), 10); }
        return;
    }
    for (i = 0; i < 10; i++) {
        long v = 0;
        if (fscanf(f, ds_str(0x38F8), &v) != 1) break;
        hiscore[i] = (int32_t)v;
        if (fscanf(f, ds_str(0x38FC), hiname[i]) != 1) hiname[i][0] = 0;
    }
    fclose(f);
}
void save_high_scores(void)
{
    if (port_no_disk) return;            /* (port) a play-test in Shrapnel */
    FILE *f = fopen(game_path(pack_file_name(ds_str(0x3757))), "w");
    int i;
    if (!f) return;
    for (i = 0; i < 10; i++) {
        fprintf(f, ds_str(0x3764), (long)hiscore[i]);
        fprintf(f, ds_str(0x3768), hiname[i]);
    }
    fclose(f);
}

/* 108E:2D58  framed panel for the high-score table */
void ui_hiscore_frame(int x)
{
    int si, di;
    ega_outw(0x3C4, 0x0F02);
    WRITE_MODE_LATCH();
    set_draw_page(x == 2 ? draw_page : 0);
    for (si = 0; si < 0x0A; si++)
        for (di = x; x + 0x1A > di; di += 2) blit_tile(0x3220, (uint16_t)(si * 0x280 + di + 0x280));
    blit_tile(0x3240, (uint16_t)(x + 0x280));
    blit_tile(0x3260, (uint16_t)(x + 0x298));
    blit_tile(0x3280, (uint16_t)(x + 0x1900));
    blit_tile(0x32A0, (uint16_t)(x + 0x1918));
    for (si = 0x500; si < 0x16A8; si += 0x280) {
        blit_tile(0x32C0, (uint16_t)(si + x));
        blit_tile(0x32E0, (uint16_t)(si + x + 0x18));
    }
    for (si = 0x282; si < 0x298; si += 2) {
        blit_tile(0x3300, (uint16_t)(si + x));
        blit_tile(0x3320, (uint16_t)(si + x + 0x1680));
    }
}
void ui_high_scores(int x)
{
    char buf[16];
    int si;
    ui_hiscore_frame(x);
    ui_text(ds_str(0x373F), x + 2, 3);
    for (si = 0; si < 8; si++) {
        snprintf(buf, sizeof buf, "%d", si + 1);
        ui_text(buf, x + 2, si * 2 + 5);
        ui_text(ds_str(0x3755), x + 3, si * 2 + 5);
        snprintf(buf, sizeof buf, "%ld", (long)hiscore[si]);
        ui_text(buf, x + 6, si * 2 + 5);
        ui_text(hiname[si], x + 0x0E, si * 2 + 5);
    }
}

/* 0097:0874  read a name (up to 10 chars) at (x,row)
 * (port) plus the way a console does it, for a controller (and the arrow
 * keys): up and down step through the letters and digits for in the next place, right keeps
 * it and moves on (with nothing picked: a space), left takes the last one
 * back to change it; Enter / confirm keeps the one picked and finishes.
 * Typing works as in the original. */
static const uint8_t name_keys[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                       0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
static char name_char(int i)            /* A..Z (as typing gives them), then 0..9 */
{
    return i < 26 ? (char)ds_u8((uint16_t)(name_keys[i] * 7 + 0xA1)) : (char)('0' + i - 26);
}
static void name_show(char ch, int col, int row)   /* one place, cleared first */
{
    char s[2];
    draw_sprite(object_gfx + 0x37A0, col, row * 8);   /* (the cursor's resting frame: the box's own background) */
    s[0] = ch; s[1] = 0;
    if (ch) ui_text(s, col, row);
}
static void read_name(char *dst, int x, int row)
{
    int si = 0, pick = -1;               /* (port) pick: the letter being chosen, -1 none */
    last_scancode = 0;
    for (;;) {
        uint8_t c;
        ui_wait_cursor(x + si + 1 + (pick >= 0), row);
        c = last_scancode;
        if (c & 0x80) continue;          /* a key let go */
        if (c == 0x48 || c == 0x50) {    /* (port) up / down: the next or previous letter */
            if (si >= 10) continue;
            if (pick < 0) {
                int i;
                pick = 0;
                for (i = 0; si > 0 && i < 36; i++) if (name_char(i) == dst[si - 1]) pick = i;   /* from the last one */
            } else pick = (pick + (c == 0x48 ? 1 : 35)) % 36;
            name_show(name_char(pick), x + si + 1, row);
            continue;
        }
        if (c == 0x4D) {                 /* (port) right: keep it (or a space) and move on */
            if (si >= 10) continue;
            dst[si++] = pick >= 0 ? name_char(pick) : ' ';
            pick = -1;
            continue;
        }
        if (c == 0x4B) {                 /* (port) left: back to the last one, to change it */
            if (pick >= 0) { name_show(0, x + si + 1, row); pick = -1; continue; }
            if (si == 0) continue;
            si--;
            for (pick = 35; pick > 0 && name_char(pick) != dst[si]; pick--) ;
            if (name_char(pick) != dst[si]) pick = 0;
            name_show(name_char(pick), x + si + 1, row);
            continue;
        }
        if (pick >= 0 && c != 0x1C) { name_show(0, x + si + 1, row); pick = -1; }   /* (port) typing instead */
        if (c == 0x1C) { if (pick >= 0) dst[si++] = name_char(pick); dst[si] = 0; return; }
        if (c == 0x0E) { if (si > 0) si--; continue; }
        if (si >= 10) continue;
        if ((c >= 0x10 && c <= 0x19) || (c >= 0x2C && c <= 0x32) || (c >= 0x1E && c <= 0x26)) {
            char s[2];
            dst[si] = (char)ds_u8((uint16_t)(c * 7 + 0xA1));   /* scancode -> ASCII table */
            s[0] = dst[si]; s[1] = 0;
            si++;
            ui_text(s, x + si, row);
            continue;
        }
        if (c == 0x39) dst[si++] = ' ';
    }
}

/* 0097:0928  insert the current score into the table */
void enter_high_score(void)
{
    int di, si;
    for (di = 0; di < 8; di++) {
        if (hiscore[di] > score) continue;
        for (si = 10; si > di; si--) {
            if (si < 10) { hiscore[si] = hiscore[si - 1]; memcpy(hiname[si], hiname[si - 1], 11); }
        }
        hiname[di][0] = 0;
        hiscore[di] = score;
        ui_box(7, 2, 5);
        play_sound(0x18);
        dlg_hiscore_banner();
        SPIN_WHILE(last_scancode == 0x15);
        read_name(hiname[di], 0x0E, 0x0B);
        return;
    }
}

/* ------------------------------------------------------------------
 * 0097:049F  the main menu.  Returns 1 start, 2 play USERDEMO, 3 restore,
 * 4 record MY_DEMO (asp only), 5 play MY_DEMO (also on timeout), 0 quit.
 * While idle on the title screen it cycles to the credits after a while.
 * ---------------------------------------------------------------- */
int main_menu(int x)
{
    long idle;
    for (;;) {
        ui_clear_page0();
        if (port_start_at_menu) {        /* (port) just switched episode: straight to the menu */
            port_start_at_menu = 0;
            ui_fade_out(0x14);
            ui_show_title();
            goto menu;
        }
    title:
        ui_fade_out(0x14);
        ui_show_title();
        ui_fade_in(0x14);
        idle = 0;
        last_scancode = 0;
        while (last_scancode == 0) {
            pf_service();
            if (timer_ticks % 2 == 0) continue;
            idle++;
            if (idle == 0x16E360L) { last_scancode = 0; return 5; }
            if (idle == 0x0F4240L) { ui_fade_out(0x14); dlg_title_text(); dlg_order_1(); ui_fade_in(0x14); }
            if (idle == 0x07A120L) { ui_fade_out(0x14); ui_show_credits(); ui_fade_in(0x14); }
        }
        if ((last_scancode == 0x10 || last_scancode == 1) && idle < 0x07A120L) {
            SPIN_WHILE(last_scancode == 0x10);
            if (dlg_confirm_quit(7)) return 0;
        }
    menu:
        WRITE_MODE_LATCH();
        ui_box_px(x, 32, 136);           /* (port) one text row (8 pixels) taller: U)ser levels */
        WRITE_MODE_NORMAL();
        dlg_main_menu_text(x);
        ui_fade_in(0x0F);
        idle = 0;
        for (;;) {
            last_scancode = 0;
            if (cfg.cursor_menus) {
                /* (port) cursor menu over the original's eleven options */
                static int sel;
                static const uint8_t keys[12] = { 0x1F, 0x13, 0x12, 0x16, 0x17, 0x18, 0x22,
                                                  0x23, 0x19, 0x2F, 0x2E, 0x10 };
                MenuItem it[12];
                int i;
                for (i = 0; i < 12; i++) { it[i].col = (uint8_t)(x + 2); it[i].row = (uint8_t)(9 + i); it[i].key = keys[i]; }
                if (!ui_menu_wait_idle(it, 12, &sel, &idle, 0xBBAEE0L)) { last_scancode = 0; ui_fade_out(0x14); return 5; }
            } else
            while (last_scancode == 0) {
                pf_service();
                if (++idle > 0xBBAEE0L) { last_scancode = 0; ui_fade_out(0x14); return 5; }
            }
            if (last_scancode == 0x1F || last_scancode == 0x39 || last_scancode == 0x1C) return 1;
            if (last_scancode == 0xC0 && in_fire && cmd_asp) return 4;
            if (last_scancode == 0x2E) {
                SPIN_WHILE(last_scancode == 0x2E);
                ui_fade_out(0x0F); ui_show_credits(); ui_fade_in(0x0F);
                last_scancode = 0; SPIN_WHILE(last_scancode == 0);
            } else if (last_scancode == 0x23) {
                ui_high_scores(x);
                SPIN_WHILE(last_scancode == 0x23);
                ui_wait_cursor(x + 0x18, 0x14);
            } else if (last_scancode == 0x2F) {
                if (!demo_missing) return 2;
                dlg_not_available();
            } else if (last_scancode == 0x13) {
                return 3;
            } else if (last_scancode == 0x19) {
                return 5;
            } else if (last_scancode == 0x17) {
                ui_fade_out(0x0F); dlg_story();
            } else if (last_scancode == 1 || last_scancode == 0x10) {
                SPIN_WHILE(last_scancode == 0x10);
                if (dlg_confirm_quit(7)) return 0;
            } else if (last_scancode == 0x22) {
                dlg_game_setup(x);
            } else if (last_scancode == 0x16) {    /* (port) U: user levels, made in Shrapnel */
                if (ui_pick_pack(x)) return 1;
            } else if (last_scancode == 0x12) {    /* (port) E: episode select */
                int e = ui_episode_select(x);
                if (e && e != episode) port_switch_episode(e);
            } else if (last_scancode == 0x18) {
                ui_fade_out(0x0F); dlg_title_text(); dlg_order_1();
                ui_wait_cursor(0x27, 0x18);
                ui_clear_page0(); dlg_order_2();
                ui_wait_cursor(0x27, 0x18);
            } else {
                continue;
            }
            ui_fade_out(0x0F);
            ui_show_title();
            goto menu;
        }
    }
}

/* 0097:0126  "restore which game?" -> 1 if a slot 1..9 was chosen */
int restore_menu(int x)
{
    int lvl; long sc;
    port_resume_qs = 0;
    if (cfg.cursor_menus) {                 /* (port) slot picker instead of "1-9?" */
        int s = ui_pick_slot(x, 0);
        if (s == 10) { port_resume_qs = 1; return 1; }        /* (port) the quicksave */
        last_scancode = (uint8_t)(s ? s + 1 : 0);
    } else
        dlg_restore_prompt(x);
    /* (port) Q at the "1-9?" prompt: the quicksave too */
    if (last_scancode == 0x10 && port_qs_info(&lvl, &sc)) { port_resume_qs = 1; return 1; }
    if (last_scancode >= 2 && last_scancode <= 0x0A) {
        save_slot = (uint8_t)(last_scancode + 0x2F);
        return 1;
    }
    return 0;
}

/* 0097:0351  save into slot 1..9 */
static int save_menu(int x)
{
    if (cfg.cursor_menus) {                 /* (port) slot picker instead of "1-9?" */
        int s = ui_pick_slot(x, 1);
        last_scancode = (uint8_t)(s ? s + 1 : 0);
    } else
        dlg_save_prompt(x);
    if (last_scancode >= 2 && last_scancode <= 0x0A) {
        save_game((uint8_t)(last_scancode + 0x2F));
        return 1;
    }
    return 0;
}

/* 0097:037E  F1 help menu during play */
void help_menu(int x)
{
    WRITE_MODE_LATCH();
redraw:
    ui_box(x, 1, 5);
    WRITE_MODE_NORMAL();
    dlg_help_menu_text(x);
    pf_delay_ms(100);
    for (;;) {
        /* (port) cursor menu: Save, Instructions, Game setup, High scores, F10 restart */
        static int sel;
        const MenuItem it[5] = { {(uint8_t)(x + 4), 7, 0x1F}, {(uint8_t)(x + 4), 8, 0x17},
                                 {(uint8_t)(x + 4), 9, 0x22}, {(uint8_t)(x + 4), 10, 0x23},
                                 {(uint8_t)(x + 2), 11, 0x44} };
        ui_menu_wait(0x1A, 0x0C, it, 5, &sel);
        switch (last_scancode) {
        case 0x01: case 0x39: case 0x1C:
            goto done;
        case 0x1F:                                      /* S: save (hallways only) */
            if (level_index % 2 == 0) { dlg_save_hallway_only(x); goto done; }
            if (save_menu(x)) { dlg_saved(); goto done; }
            dlg_not_saved();
            goto redraw;
        case 0x23:                                      /* H: high scores */
            ui_high_scores(x);
            SPIN_WHILE(last_scancode == 0x23);
            ui_wait_cursor(x + 0x18, 0x14);
            goto done;
        case 0x17: dlg_instructions(x); goto done;      /* I */
        case 0x10: last_scancode = 0; goto done;        /* Q */
        case 0x22: dlg_game_setup(x); goto done;        /* G */
        case 0x44: g_0094 = 1; goto done;               /* F10: restart */
        default: break;
        }
    }
done:
    timer_ticks = 0;
}

/* ------------------------------------------------------------------
 * 0097:E947  end of the episode (Dr. Proton escapes / is defeated)
 * ---------------------------------------------------------------- */
static void wait_ticks(int n) { timer_ticks = 0; SPIN_WHILE(timer_ticks < (uint16_t)n); }   /* E92E */
static void wait_release(void) { if (!joystick_mode) SPIN_WHILE(!(last_scancode & 0x80)); }
int pack_finished;                       /* (port) see port.h */
static void stock_ending(void);
int  exe_text_borrow(const char *path);   /* exe.c */
void exe_text_return(void);
void reload_end_screen(void);            /* files.c */
const char *game_path(const char *name);

/* (port) a pack's own words for its ending, over Duke's picture, a box at
 * a time like the game's own ending (five lines of at most 23 characters) */
#define END_LINES 5
#define END_W 23
static void custom_ending(void)
{
    const char *t = pack.ending_text;
    add_score(100000L);
    ui_fade_out(0x14);
    ui_clear_page0();
    ui_show_duke();
    ui_fade_in(0x14);
    while (*t) {
        char lines[END_LINES][END_W + 1];
        int n = 0, k;
        memset(lines, 0, sizeof lines);
        while (*t && n < END_LINES) {
            int len = 0, cut = -1, i;
            while (t[len] && t[len] != '\n' && len < END_W) { if (t[len] == ' ') cut = len; len++; }
            if (t[len] && t[len] != '\n' && t[len] != ' ' && cut > 0) len = cut;   /* break at a space */
            for (i = 0; i < len; i++) lines[n][i] = t[i];
            n++;
            t += len;
            while (*t == ' ') t++;
            if (*t == '\n') t++;
        }
        wait_release();
        ui_box(7, 8, 3);
        for (k = 0; k < n; k++) ui_text(lines[k], 8, 17 + k);
        ui_wait_cursor(0x1F, 0x16);
        timer_ticks = 0;
    }
    ui_fade_out(0x14);
}

/* (port) the end of a pack: the ending PACK.INI names - a stock one (the
 * episode's own, or another episode's when its EXE is in the folder) or the
 * pack's own words */
void pack_ending(void)
{
    int which = pack.ending;
    if (which == 0) {                    /* its own words; with none, no words at the exit */
        if (pack.ending_text[0]) custom_ending();
        else if (!pack_finished) stock_ending();   /* (Dr. Proton beaten: the episode's own) */
        return;
    }
    if (which < 1 || which > 3) which = episode;
    if (which != episode) {
        char name[16];
        snprintf(name, sizeof name, "DN%d.EXE", which);
        if (exe_text_borrow(game_path(name))) {
            reload_end_screen();
            stock_ending();
            exe_text_return();
            reload_end_screen();
            return;
        }
    }
    stock_ending();
}

void ending_sequence(void)
{
    /* (port) the ending's pictures are full-screen pictures, not play: so
     * widescreen replacement art (END.DN1.png and the rest) stands in for
     * them as everywhere else, and its sprites aren't clipped as actors */
    int keep = port_in_play;
    port_in_play = 0;
    if (pack.on) pack_ending();
    else stock_ending();
    port_in_play = keep;
}

static void stock_ending(void)
{
    int si, di = 0;
    add_score(100000L);
    if (boss_ep1) {
        ui_fade_out(0x0A);
        ui_show_end();
        ui_fade_in(0x0A);
        for (si = 0; si < 0x34; si++) {                 /* Proton's ship rises */
            di = !di;
            draw_sprite(object_gfx + (uint16_t)(di * 0xA0 + 0x5820), 0x1A, 0x7D - si);
            pf_present(); wait_ticks(0x0A);
        }
        for (si = 0; si < 0x32; si++) {
            di = !di;
            draw_sprite(object_gfx + (uint16_t)(di * 0xA0 + 0x5820), 0x1A, 0x4A);
            pf_present(); wait_ticks(0x0A);
        }
        for (si = 0; si < 5; si++) {
            draw_sprite(object_gfx + (uint16_t)(si * 0xA0 + 0x5960), 0x1A, 0x4B - si);
            pf_present(); wait_ticks(0x0E);
        }
        last_scancode = 0;
        {
            long n = 0;
            while (last_scancode == 0) {
                pf_service();
                n++;
                if (n == 0x16E360L) break;
                if (n == 0x0493E0L) dlg_press_key();
            }
        }
        ui_fade_out(0x0A); ui_show_badguy(); ui_fade_in(0x0A); wait_release();
        dlg_end_1(); timer_ticks = 0;
        ui_fade_out(0x0A); ui_show_duke(); ui_fade_in(0x0A); wait_release();
        dlg_end_2(); timer_ticks = 0;
        ui_fade_out(0x0A); ui_show_badguy(); ui_fade_in(0x0A); wait_release();
        dlg_end_3(); timer_ticks = 0;
        ui_fade_out(0x0A); ui_show_duke(); ui_fade_in(0x0A); wait_release();
        dlg_end_4();
        ui_fade_out(0x0A);
    } else {
        wait_ticks(0x1F4);
        ui_fade_out(0x14);
        ui_clear_page0();
        ui_show_duke();
        ui_fade_in(0x14);
        wait_release();
        dlg_end_5();
        ui_fade_out(0x14);
    }
    timer_ticks = 0;
    dlg_to_be_continued(7);
}

/* 108E:2F04  text printed on exit (B800 text page) */
void ui_exit_text(void)
{
    static const uint16_t lines[] = {
        0x3A89, 0x3AD8, 0x3B29, 0x3B7A, 0x3BBB, 0x3C0C, 0x3C5D, 0x3CAE, 0x3CFF, 0x3D50,
        0x3DA1, 0x3DF2, 0x3E43, 0x3E94, 0x3EE5, 0x3F36, 0x3F87, 0x3FD8, 0x4029, 0x4070,
        0x40C1, 0x4112, 0x4163 };
    size_t i;
    if (pf_headless) return;
    for (i = 0; i < sizeof lines / sizeof lines[0]; i++) printf("%s\n", ds_str(lines[i]));
}

/* ------------------------------------------------------------------
 * 0097:008A  game setup: (J)oystick calibration, (K)eyboard redefinition,
 * (S)ound and (H)int toggles.  Joystick calibration (0097:0E30) and key
 * redefinition (108E:4009) talk directly to the game port / scancodes;
 * the port maps them onto its own controller and key handling, so only
 * the menu structure is reproduced here.
 * ---------------------------------------------------------------- */
void dlg_setup_menu_text(int x);   /* 108E:148F */
/* (port) "not in Enhanced mode", in the style of the setup toggles */
static void dlg_enhanced_only(int x)
{
    uint8_t k = last_scancode;
    ui_box(x, 3, 2);
    WRITE_MODE_NORMAL();
    SPIN_WHILE(last_scancode == k);
    ui_text("     Not available in", x, 7);
    ui_text("      Enhanced mode", x, 9);
    pf_delay_ms(300);
    ui_wait_cursor(x + 0x18, 0x0A);
    SPIN_WHILE(last_scancode == k);
}

void dlg_game_setup(int x)
{
    ui_box(x, 2, 5);                    /* (port) two text rows (one tile row) taller than the original's 4 */
    WRITE_MODE_NORMAL();
    dlg_setup_menu_text(x);
    ui_text("O)ptions", x + 6, 12);      /* (port) shown in classic mode too, so the
                                            cursor menus can always be switched back on */
    ui_text("P)atches", x + 6, 13);      /* (port) level patches from Shrapnel */
    pf_delay_ms(100);
    for (;;) {
        /* (port) cursor menu: Joystick, Keyboard, Sound, Hints, Options, Patches */
        static int sel;
        const MenuItem it[6] = { {(uint8_t)(x + 4), 8, 0x24}, {(uint8_t)(x + 4), 9, 0x25},
                                 {(uint8_t)(x + 4), 10, 0x1F}, {(uint8_t)(x + 4), 11, 0x23},
                                 {(uint8_t)(x + 4), 12, 0x18}, {(uint8_t)(x + 4), 13, 0x19} };
        ui_menu_wait(x + 0x18, 0x0D, it, 6, &sel);
        if (last_scancode == 0x18) { ui_options(x); return; }        /* O (port) */
        if (last_scancode == 0x19) { ui_patches(x); return; }        /* P (port) */
        /* J/K: in Enhanced mode the controller always works alongside the
         * keyboard (with remapping), so the original's joystick mode - which
         * turns keyboard movement off - has nothing to add (port) */
        if ((last_scancode == 0x24 || last_scancode == 0x25) && cfg.mode == MODE_ENHANCED) {
            joystick_mode = 0;
            dlg_enhanced_only(x);
            return;
        }
        if (last_scancode == 0x24) { joystick_mode = 1; return; }     /* J */
        if (last_scancode == 0x25) { joystick_mode = 0; return; }     /* K */
        if (last_scancode == 0x1F) { ui_sound_volume(x); return; }    /* S: volume (port) */
        if (last_scancode == 0x23) { ui_toggle_hint(x); return; }     /* H */
        if (last_scancode == 0x1C || last_scancode == 1 || last_scancode == 0x39) return;
    }
}
