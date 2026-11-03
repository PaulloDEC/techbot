/*
 * port_options.c - the Options menu (Game Setup > Options) and its
 * controller / keyboard remapping screens.
 *
 * Built only from the game's own dialog box, font and cursor, laid out like
 * the original menus: label at the left, current value at the right.
 * Left/right (or confirm) changes a value; Esc / B goes back.
 */
#include "dn1.h"
#include "port.h"
#include <stdio.h>
#include <string.h>

void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);
void save_keys(const char *name);

#define KEY_CANCEL 0x01
enum { K0 = 0xE0 };          /* item keys: not real scancodes, only returned on confirm */

/* ---------------------------------------------------------------- names */
static const char *key_name(uint8_t sc)
{
    static char buf[8];
    static const char *const row1 = "1234567890-=";
    static const char *const row2 = "QWERTYUIOP[]";
    static const char *const row3 = "ASDFGHJKL;'`";
    static const char *const row4 = "\\ZXCVBNM,./";
    if (sc >= 0x02 && sc <= 0x0D) { buf[0] = row1[sc - 0x02]; buf[1] = 0; return buf; }
    if (sc >= 0x10 && sc <= 0x1B) { buf[0] = row2[sc - 0x10]; buf[1] = 0; return buf; }
    if (sc >= 0x1E && sc <= 0x29) { buf[0] = row3[sc - 0x1E]; buf[1] = 0; return buf; }
    if (sc >= 0x2B && sc <= 0x35) { buf[0] = row4[sc - 0x2B]; buf[1] = 0; return buf; }
    if (sc >= 0x3B && sc <= 0x44) { snprintf(buf, sizeof buf, "F%d", sc - 0x3A); return buf; }
    switch (sc) {
    case 0x01: return "Esc";    case 0x0E: return "Bksp";   case 0x0F: return "Tab";
    case 0x1C: return "Enter";  case 0x1D: return "Ctrl";   case 0x2A: return "LShift";
    case 0x36: return "RShift"; case 0x38: return "Alt";    case 0x39: return "Space";
    case 0x3A: return "Caps";   case 0x47: return "Home";   case 0x48: return "Up";
    case 0x49: return "PgUp";   case 0x4B: return "Left";   case 0x4D: return "Right";
    case 0x4F: return "End";    case 0x50: return "Down";   case 0x51: return "PgDn";
    case 0x52: return "Ins";    case 0x53: return "Del";
    case 0x37: return "Pad *";  case 0x4A: return "Pad -";  case 0x4C: return "Pad 5";
    case 0x4E: return "Pad +";  case 0x45: return "NumLk";  case 0x46: return "ScrLk";
    }
    snprintf(buf, sizeof buf, "#%02X", sc);
    return buf;
}

static const char *btn_label(int b)
{
    static const char *const n[] = { "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3",
                                     "LB", "RB", "D-Up", "D-Down", "D-Left", "D-Right" };
    return (b >= 0 && b < (int)(sizeof n / sizeof n[0])) ? n[b] : "-";
}

/* ---------------------------------------------------------------- layout:
 * the dialog box is 13 tiles wide; its inside spans byte columns x+2..x+23 */
static void frame(int x, const char *title)
{
    WRITE_MODE_LATCH();
    ui_box(x, 2, 8);
    WRITE_MODE_NORMAL();
    ui_text(title, x + 2 + (22 - (int)strlen(title)) / 2, 7);
}
static void row(int x, int r, const char *label, const char *value)
{
    ui_text(label, x + 4, r);
    if (value) ui_text(value, x + 24 - (int)strlen(value), r);
}
static void centre(int x, int r, const char *s) { ui_text(s, x + 2 + (22 - (int)strlen(s)) / 2, r); }

static void items(MenuItem *it, const int *rows, int n, int x)
{
    int i;
    for (i = 0; i < n; i++) { it[i].col = (uint8_t)(x + 2); it[i].row = (uint8_t)rows[i]; it[i].key = (uint8_t)(K0 + i); }
}
/* run the cursor; returns -1 back, otherwise the row index with *dir set to
 * -1 / +1 for left / right and 0 for confirm */
static int choose(const MenuItem *it, int n, int *sel, int *dir)
{
    for (;;) {
        uint8_t sc;
        ui_select(it, n, sel);
        sc = last_scancode;
        if (sc == KEY_CANCEL) return -1;
        if (sc == 0x4B) { *dir = -1; return *sel; }
        if (sc == 0x4D) { *dir = +1; return *sel; }
        if (sc >= K0 && sc < K0 + n) { *dir = 0; return sc - K0; }
    }
}

static void save_cfg(void) { cfg_save(game_path(PORT_CFG_FILE)); }

/* ---------------------------------------------------------------- keyboard */
static void keyboard_menu(int x)
{
    static const char *const act[8] = { "Up", "Down", "Left", "Right", "Fire", "Jump", "Quicksave", "Quickload" };
    static const int rows[10] = { 9, 10, 11, 12, 13, 14, 15, 16, 18, 19 };
    MenuItem it[10];
    int sel = 0, dir, i, k;
    items(it, rows, 10, x);
    for (;;) {
        frame(x, "Keyboard");
        for (i = 0; i < 6; i++) row(x, rows[i], act[i], key_name(key_bind[i]));
        row(x, rows[6], act[6], key_name(cfg.key_qsave));
        row(x, rows[7], act[7], key_name(cfg.key_qload));
        row(x, 18, "Defaults", NULL);
        row(x, 19, "Done", NULL);
        k = choose(it, 10, &sel, &dir);
        if (k < 0 || k == 9) return;
        if (k == 8) {                                   /* the EXE's own initial bindings */
            for (i = 0; i < 6; i++) key_bind[i] = ds_u8((uint16_t)(0x02F4 + i));
            save_keys(ds_str(0x0AB2));
            cfg.key_qsave = 0x3F; cfg.key_qload = 0x43;
            save_cfg();
            continue;
        }
        if (dir) continue;
        frame(x, "Keyboard");
        centre(x, 10, "Press a key for");
        centre(x, 11, act[k]);
        centre(x, 13, "Esc cancels");
        pf_present();
        {
            int prev = ui_context;
            ui_context = UI_CTX_CAPTURE;
            last_scancode = 0;
            while (last_scancode == 0 || (last_scancode & 0x80)) { if (last_scancode & 0x80) last_scancode = 0; pf_service(); }
            ui_context = prev;
        }
        if (last_scancode != KEY_CANCEL) {
            uint8_t sc = last_scancode, *slot[8], old;
            for (i = 0; i < 6; i++) slot[i] = &key_bind[i];
            slot[6] = &cfg.key_qsave; slot[7] = &cfg.key_qload;
            old = *slot[k];
            for (i = 0; i < 8; i++) if (i != k && *slot[i] == sc) *slot[i] = old;   /* swap */
            *slot[k] = sc;
            save_keys(ds_str(0x0AB2));                  /* KEYS.DN1, as the original's own screen does */
            save_cfg();                                 /* quicksave keys live in TECHBOT.CFG */
        }
        last_scancode = 0;
    }
}

/* ---------------------------------------------------------------- controller */
static void controller_menu(int x)
{
    static const char *const act[PADACT_COUNT] = { "Up", "Down", "Left", "Right", "Fire", "Jump",
                                                   "Menu", "Back", "Quicksave", "Quickload" };
    static const int rows[13] = { 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
    MenuItem it[13];
    int sel = 0, dir, i, k;
    char v[24];
    items(it, rows, 13, x);
    for (;;) {
        WRITE_MODE_LATCH();
        ui_box(x, 2, 8);
        WRITE_MODE_NORMAL();
        centre(x, 6, pf_pad_connected() ? "Controller" : "Controller (none)");
        for (i = 0; i < PADACT_COUNT; i++) {
            if (cfg.pad_alt[i] >= 0) snprintf(v, sizeof v, "%s / %s", btn_label(cfg.pad[i]), btn_label(cfg.pad_alt[i]));
            else                     snprintf(v, sizeof v, "%s", btn_label(cfg.pad[i]));
            row(x, rows[i], act[i], v);
        }
        snprintf(v, sizeof v, "%d%%", (int)((long)cfg.deadzone * 100 / 32767));
        row(x, 17, "Deadzone", v);
        row(x, 18, "Defaults", NULL);
        row(x, 19, "Done", NULL);
        k = choose(it, 13, &sel, &dir);
        if (k < 0 || k == 12) { save_cfg(); return; }
        if (k == 10) {
            cfg.deadzone += (dir < 0 ? -2000 : 2000);
            if (cfg.deadzone < 2000) cfg.deadzone = 2000;
            if (cfg.deadzone > 30000) cfg.deadzone = 30000;
            continue;
        }
        if (k == 11) { cfg_default_pad(); continue; }
        if (dir) continue;
        frame(x, "Controller");
        centre(x, 10, "Press a button for");
        centre(x, 11, act[k]);
        centre(x, 13, "Esc cancels");
        pf_present();
        {
            int prev = ui_context, b = -1;
            long waited = 0;
            ui_context = UI_CTX_CAPTURE;
            pf_pad_capture();                           /* forget the press that opened this */
            last_scancode = 0;
            while (b < 0 && last_scancode != KEY_CANCEL && waited < 8000) {
                pf_delay_ms(10); waited += 10;
                b = pf_pad_capture();
            }
            ui_context = prev;
            if (b >= 0) {
                for (i = 0; i < PADACT_COUNT; i++) {
                    if (i == k) continue;
                    if (cfg.pad[i] == b) cfg.pad[i] = cfg.pad[k];     /* swap with the old owner */
                    if (cfg.pad_alt[i] == b) cfg.pad_alt[i] = PAD_NONE;
                }
                cfg.pad[k] = b;
                cfg.pad_alt[k] = PAD_NONE;
            }
        }
        last_scancode = 0;
    }
}

/* ---------------------------------------------------------------- episode select
 * Lists the episodes whose EXE is in the folder, by the names the game's own
 * ordering screen gives them ("Episode TWO:    Mission: Moonbase" - every
 * episode's EXE has all three lines). */
static void episode_name(int e, char *out, size_t n)
{
    static const uint16_t line[3] = { 0x1732, 0x175B, 0x1784 };   /* DN1 offsets */
    const char *s = strchr(ds_str(line[e - 1]), ':');
    size_t k;
    if (!s) { snprintf(out, n, "Episode %d", e); return; }
    for (s++; *s == ' '; s++) ;
    snprintf(out, n, "%s", s);
    for (k = strlen(out); k > 0 && (out[k - 1] == ' ' || out[k - 1] == '!'); k--) out[k - 1] = 0;
}

int ui_episode_select(int x)
{
    MenuItem it[3];
    int ep_of[3], n = 0, e, sel = 0, dir, k;
    char name[40];
    /* laid out like the main menu it replaces on screen */
    WRITE_MODE_LATCH();
    ui_box(x, 2, 8);
    WRITE_MODE_NORMAL();
    centre(x, 6, "EPISODE SELECT");
    centre(x, 7, "--------------");
    for (e = 1; e <= 3; e++) {
        if (!episode_available(e)) continue;
        episode_name(e, name, sizeof name);
        name[21] = 0;                                    /* the box holds 21 characters here */
        ui_text(name, x + 3, 9 + n);
        it[n].col = (uint8_t)(x + 2); it[n].row = (uint8_t)(9 + n); it[n].key = (uint8_t)(K0 + n);
        if (e == episode) sel = n;
        ep_of[n++] = e;
    }
    for (;;) {
        k = choose(it, n, &sel, &dir);
        if (k < 0) { last_scancode = 0; return 0; }
        if (dir == 0) { last_scancode = 0; return ep_of[k]; }
    }
}

/* ---------------------------------------------------------------- top level */
void ui_options(int x)
{
    static const int rows[11] = { 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
    MenuItem it[11];
    int sel = 0, dir, k;
    char v[12];
    items(it, rows, 11, x);
    for (;;) {
        frame(x, "Options");
        row(x, 9,  "Game mode", cfg.mode == MODE_ENHANCED ? "Enhanced" : "Original");
        row(x, 10, "Menus", cfg.cursor_menus ? "Cursor" : "Classic");
        row(x, 11, "Fullscreen", cfg.fullscreen ? "On" : "Off");
        snprintf(v, sizeof v, "x%d", cfg.scale);
        row(x, 12, "Window size", v);
        row(x, 13, "Scaling", cfg.scaling == SCALE_NEAREST ? "Nearest" : "Sharp");
        /* smooth motion is an Enhanced-mode feature: Original shows every
         * frame exactly as DN1.EXE drew it */
        {
            static char sm[16];
            if (!cfg.smooth) strcpy(sm, "Off");
            else if (cfg.smooth > 0) snprintf(sm, sizeof sm, "%d fps", cfg.smooth);
            else strcpy(sm, "Display");
            if (cfg.smooth && cfg.mode != MODE_ENHANCED) strcpy(sm, "Enh. only");
            row(x, 14, "Smoothing", sm);
        }
        row(x, 15, "Widescreen", !cfg.widescreen ? "Off" : cfg.mode == MODE_ENHANCED ? "On" : "Enh. only");
        row(x, 16, "Sound", !cfg.sound ? "Original" : cfg.mode != MODE_ENHANCED ? "Enh. only"
                              : cfg.sound == SND_OVERLAP ? "Overlap" : "Hi-fi");
        row(x, 17, "Controller", NULL);
        row(x, 18, "Keyboard", NULL);
        row(x, 19, "Done", NULL);
        k = choose(it, 11, &sel, &dir);
        if (k < 0 || k == 10) break;
        switch (k) {
        case 0:
            cfg.mode = !cfg.mode;
            if (cfg.mode == MODE_ENHANCED) joystick_mode = 0;   /* see dlg_game_setup */
            pf_apply_settings();
            break;
        case 1: cfg.cursor_menus = !cfg.cursor_menus; break;
        case 2: cfg.fullscreen = !cfg.fullscreen; pf_apply_settings(); break;
        case 3:
            cfg.scale += dir < 0 ? -1 : 1;
            if (cfg.scale < 1) cfg.scale = 8;
            if (cfg.scale > 8) cfg.scale = 1;
            pf_apply_settings();
            break;
        case 4: cfg.scaling = !cfg.scaling; break;
        case 5: {   /* Off, 30, 60, 120, Display (left/right steps through them) */
            static const int steps[5] = { 0, 30, 60, 120, -1 };
            int i, cur = 4;
            for (i = 0; i < 5; i++) if (steps[i] == cfg.smooth) cur = i;
            cur = (cur + (dir < 0 ? 4 : 1)) % 5;
            cfg.smooth = steps[cur];
            break;
        }
        case 6: cfg.widescreen = !cfg.widescreen; pf_apply_settings(); break;
        case 7: cfg.sound = (cfg.sound + (dir < 0 ? 2 : 1)) % 3; break;
        case 8: if (!dir) controller_menu(x); break;
        case 9: if (!dir) keyboard_menu(x); break;
        }
        save_cfg();
    }
    save_cfg();
    last_scancode = 0;
}
