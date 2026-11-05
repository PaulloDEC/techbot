/*
 * port_menu.c - cursor-driven menus built from the game's own parts.
 *
 * The selection marker is the original "waiting for a key" cursor (four
 * spinning frames in OBJECTx.DN1, visible area 8x8 in the top-left of its
 * 16x16 cell), drawn with the original sprite routine.  Text uses the
 * game's font and dialog boxes, so nothing here looks foreign.
 */
#include "dn1.h"
#include "port.h"
#include <stdio.h>
#include <string.h>

void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);
void ui_wait_cursor(int x, int row);
int  save_slot_info(int slot, int *lvl_index, long *sc);

static const uint16_t cursor_frames[4] = { 0x3520, 0x35C0, 0x3660, 0x3700 };
#define CURSOR_REST 0x37A0      /* the frame ui_wait_cursor leaves behind */

/* ---- the 8x8 cell under the cursor, saved so moving it restores the box */
static uint8_t cell[4][8];
static int cell_col, cell_y;
static void cell_save(int col, int y)
{
    uint16_t base = ega_draw_page ? 0x2000 : 0;
    int p, r;
    cell_col = col; cell_y = y;
    for (p = 0; p < 4; p++)
        for (r = 0; r < 8; r++) cell[p][r] = ega_planes[p][(uint16_t)(base + (y + r) * 40 + col)];
}
static void cell_restore(void)
{
    uint16_t base = ega_draw_page ? 0x2000 : 0;
    int p, r;
    for (p = 0; p < 4; p++)
        for (r = 0; r < 8; r++) ega_planes[p][(uint16_t)(base + (cell_y + r) * 40 + cell_col)] = cell[p][r];
}

/* ---- the selection loop shared by every menu.  Returns 1 with the chosen
 * option's hotkey (or any other key pressed) in last_scancode, 0 on idle
 * timeout. */
static int select_loop(const MenuItem *it, int n, int *sel, long *idle, long limit)
{
    int prev = ui_context, k;
    ui_context = UI_CTX_MENU;
    if (*sel < 0 || *sel >= n) *sel = 0;
    last_scancode = 0;
    for (;;) {
        int col = it[*sel].col, y = it[*sel].row * 8;
        uint8_t sc;
        cell_save(col, y);
        while (last_scancode == 0) {
            for (k = 0; k < 4 && last_scancode == 0; k++) {
                draw_sprite(object_gfx + cursor_frames[k], col, y);
                pf_present();
                pf_delay_ms(5);
            }
            if (idle && (*idle += 20) > limit) { cell_restore(); ui_context = prev; return 0; }
        }
        sc = last_scancode;
        cell_restore();
        if (sc & 0x80) { last_scancode = 0; continue; }           /* key released */
        if (sc == 0x48) { *sel = (*sel + n - 1) % n; last_scancode = 0; continue; }  /* up   */
        if (sc == 0x50) { *sel = (*sel + 1) % n;     last_scancode = 0; continue; }  /* down */
        if (sc == 0x1C || sc == 0x39) sc = it[*sel].key;            /* confirm = its hotkey */
        draw_sprite(object_gfx + CURSOR_REST, col, y);
        pf_present();
        last_scancode = sc;
        ui_context = prev;
        return 1;
    }
}

void ui_select(const MenuItem *items, int n, int *sel) { select_loop(items, n, sel, NULL, 0); }

/* ---- Game setup > Sound volume: OFF 1 2 3 4 MAX on one row (20% steps).
 * Left/right (or up/down) move the cursor and play the jump sound at the new
 * volume; confirm or Esc closes.  Replaces the original's sound on/off toggle. */
int port_last_volume;
void ui_sound_volume(int x)
{
    static const char *const lbl[6] = { "OFF", "1", "2", "3", "4", "MAX" };
    static const int col[6] = { 2, 7, 10, 13, 16, 19 };     /* cursor columns in the box */
    MenuItem it[6];
    int i, sel = cfg.volume;
    ui_box(x, 3, 2);
    WRITE_MODE_NORMAL();
    SPIN_WHILE(last_scancode == 0x1F);
    ui_text("      Sound volume", x, 7);
    for (i = 0; i < 6; i++) {
        ui_text(lbl[i], x + col[i] + 1, 9);
        it[i].col = (uint8_t)(x + col[i]); it[i].row = 9; it[i].key = 0x1C;
    }
    for (;;) {
        uint8_t sc;
        select_loop(it, 6, &sel, NULL, 0);          /* (up/down move inside, silently) */
        sc = last_scancode;
        if (sc == 0x4B && sel > 0) sel--;
        else if (sc == 0x4D && sel < 5) sel++;
        else if (sc == 0x1C || sc == 0x39 || sc == 0x01 || sc == 0x1F) break;
        if (sel != cfg.volume) {
            if (cfg.volume) port_last_volume = cfg.volume;
            cfg.volume = sel;
            sound_enabled = sel > 0;                /* OFF is the original's "sound off" */
            if (!sound_enabled) pf_speaker(0);
            else play_sound(0x0D);                  /* the jump: hear the new level */
        }
        last_scancode = 0;
    }
    if (sel != cfg.volume) { cfg.volume = sel; sound_enabled = sel > 0; }
    cfg_save(game_path(PORT_CFG_FILE));
    SPIN_WHILE(last_scancode == 0x1C || last_scancode == 0x1F);
    last_scancode = 0;
}

void ui_menu_wait(int wait_x, int wait_row, const MenuItem *items, int n, int *sel)
{
    if (!cfg.cursor_menus || n <= 0) { ui_wait_cursor(wait_x, wait_row); return; }
    select_loop(items, n, sel, NULL, 0);
}

int ui_menu_wait_idle(const MenuItem *items, int n, int *sel, long *idle, long limit)
{
    return select_loop(items, n, sel, idle, limit);
}

/* level number for a save made at level_index lvl (saves are made in the
 * hallway before a level: show that level) */
static int level_no(int lvl) { return lvl % 2 ? (lvl + 1) / 2 + 1 : lvl / 2 + 1; }

/* ---- save-slot picker: a dialog box listing slots 1-9 with the level and
 * score stored in each file.  Digits still pick a slot directly.  Restoring,
 * the quicksave (if there is one) heads the list as "Q": returns 10. */
int ui_pick_slot(int x, int saving)
{
    static int last_sel = 0;
    MenuItem it[10];
    char buf[32];
    int i, lvl, first_used = -1, first_free = -1, sel, qs, top, n;
    long sc;
    qs = !saving && port_qs_info(&lvl, &sc);
    top = qs ? 10 : 9;                     /* first slot's row */
    WRITE_MODE_LATCH();
    ui_box(x, 2, 8);                       /* same size as the main menu box it covers */
    WRITE_MODE_NORMAL();
    ui_text(saving ? "Save your game" : "Restore a game", x + 6, qs ? 6 : 7);
    n = 0;
    if (qs) {
        snprintf(buf, sizeof buf, "Q  Level %-2d %8ld", level_no(lvl), sc);
        ui_text(buf, x + 4, 8);
        it[n].col = (uint8_t)(x + 2); it[n].row = 8; it[n].key = 0x10; n++;   /* Q */
    }
    for (i = 0; i < 9; i++) {
        if (save_slot_info(i + 1, &lvl, &sc)) {
            /* saves are only made in the hallway before a level: show that level */
            snprintf(buf, sizeof buf, "%d  Level %-2d %8ld", i + 1, level_no(lvl), sc);
            if (first_used < 0) first_used = i;
        } else {
            snprintf(buf, sizeof buf, "%d  - empty -", i + 1);
            if (first_free < 0) first_free = i;
        }
        ui_text(buf, x + 4, top + i);
        it[n].col = (uint8_t)(x + 2); it[n].row = (uint8_t)(top + i); it[n].key = (uint8_t)(2 + i); n++;
    }
    /* the cursor starts on the quicksave if there is one, else as before */
    sel = qs ? 0 : last_sel;
    if (!qs && !saving && first_used >= 0 && !save_slot_info(sel + 1, &lvl, &sc)) sel = first_used;
    select_loop(it, n, &sel, NULL, 0);
    if (qs && last_scancode == 0x10) return 10;
    if (last_scancode >= 2 && last_scancode <= 0x0A) {
        last_sel = last_scancode - 2;
        return last_scancode - 1;
    }
    (void)first_free;
    return 0;
}
