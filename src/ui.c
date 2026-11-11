/*
 * ui.c - text, dialog boxes, palette fades, status panel (seg 108E core).
 *
 * All strings are read from the original executable at runtime: dialog
 * routines refer to them by their offset in the data segment.
 *
 * Font: font1.dn1 + font2.dn1 in RAM, 40 bytes per 8x8 glyph.
 *   ' '..'`' at font+190h, 'a'.. at font+AC8h, digits (opaque) at font+410h.
 * Dialog boxes are built from border.dn1 tiles in the VRAM tile cache
 * (3220h fill, 3240h..32A0h corners, 32C0h/32E0h sides, 3300h/3320h edges).
 */
#include "dn1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const uint8_t *glyph(uint8_t c)
{
    if (c >= 0x61) return font_gfx + (c - 0x61) * 0x28 + 0xAC8;
    return font_gfx + (c - 0x20) * 0x28 + 0x190;
}

/* 108E:37B6  string at byte column x, text row (8 px) */
void ui_text(const char *s, int x, int row)
{
    int i;
    s = port_relabel(s);                /* (port) controller button names */
    dl_mark_dirty(x * 8, row * 8, (int)strlen(s) * 8, 8);   /* (port) a dialog: show this frame exactly */
    WRITE_MODE_NORMAL();
    for (i = 0; s[i]; i++) draw_glyph_masked(glyph((uint8_t)s[i]), x + i, row * 8);
}

/* 108E:385B  single character at byte column x, pixel line y */
void ui_char(int c, int x, int y)
{
    draw_glyph_masked(glyph((uint8_t)c), x, y);
}

/* 108E:2789  right-aligned decimal number ending at column x, on both pages */
void draw_number(int32_t n, int x, int row)
{
    char buf[16];
    int len, si;
    dl_hud++;                            /* (port) status panel: never clipped */
    snprintf(buf, sizeof buf, "%ld", (long)n);
    len = (int)strlen(buf);
    for (si = len - 1; si >= 0; si--) {
        const uint8_t *g = font_gfx + (buf[len - si - 1] - '0') * 0x28 + 0x410;
        set_draw_page(1); draw_glyph_opaque(g, x - si, row * 8);
        set_draw_page(0); draw_glyph_opaque(g, x - si, row * 8);
    }
    set_draw_page(!draw_page);
    dl_hud--;
}

/* 108E:3D8C  dialog box at byte column x (x==2: on the current back page,
 * otherwise page 0), text row 'row', 'h' tile rows tall, 13 tiles wide */
/* (port) the same box at any pixel height: y = its top, h = from its top
 * edge to its bottom edge's tile (ui_box: row * 16 and h * 16) */
void ui_box_px(int x, int y, int h)
{
    int si, di;
    int top = y * 0x28, hh = h * 0x28;
    ega_outw(0x3C4, 0x0F02);
    WRITE_MODE_LATCH();
    set_draw_page(x == 2 ? draw_page : 0);
    /* (port) the box's area - once the page it goes on is chosen, so it counts
     * as on screen exactly when it is */
    dl_mark_dirty(x * 8, y, 26 * 8, h + 16);
    for (si = 0; hh > si; si += 0x280) {
        blit_tile(0x32C0, (uint16_t)(top + si + x));
        blit_tile(0x32E0, (uint16_t)(top + si + x + 0x18));
    }
    for (di = 2; di < 0x17; di += 2)
        for (si = 0; hh > si; si += 0x280)
            blit_tile(0x3220, (uint16_t)(top + x + di + si));
    blit_tile(0x3240, (uint16_t)(top + x));
    blit_tile(0x3260, (uint16_t)(top + x + 0x18));
    blit_tile(0x3280, (uint16_t)(top + x + hh));
    blit_tile(0x32A0, (uint16_t)(top + x + hh + 0x18));
    for (di = 0; di < 0x16; di += 2) {
        blit_tile(0x3300, (uint16_t)(top + di + x + 2));
        blit_tile(0x3320, (uint16_t)(hh + di + x + top + 2));
    }
}

void ui_box(int x, int row, int h) { ui_box_px(x, row << 4, h << 4); }

/* (port) one step of the spinning cursor lasts at least 3 timer ticks (about
 * 21 ms, as with a 60 Hz display's wait): the original's 5 ms alone made it
 * spin too fast to see wherever presenting doesn't wait for the display */
void ui_cursor_hold(uint16_t from)
{
    SPIN_WHILE((uint16_t)(timer_ticks - from) < 3 && last_scancode == 0);
}

/* 108E:2870  animated cursor at (x, row) until a key / joystick button */
void ui_wait_cursor(int x, int row)
{
    int b1 = 1, b2 = 1;
    int prev_ctx = ui_context;          /* (port) controller buttons act as menu keys here */
    if (prev_ctx != UI_CTX_YESNO) ui_context = UI_CTX_MENU;
    last_scancode = 0;
    if (joy_present) { int b = pf_joystick_buttons(); b2 = b & 0x10; b1 = b & 0x20; }
    while (last_scancode == 0 && b2 && b1) {
        static const uint16_t frames[4] = {0x3520, 0x35C0, 0x3660, 0x3700};
        int k;
        if (joy_present) { int b = pf_joystick_buttons(); b2 = b & 0x10; b1 = b & 0x20; }
        for (k = 0; k < 4; k++) {
            uint16_t t = timer_ticks;
            draw_sprite(object_gfx + frames[k], x, row * 8);
            pf_present();
            pf_delay_ms(5);
            ui_cursor_hold(t);
        }
    }
    draw_sprite(object_gfx + 0x37A0, x, row * 8);
    ui_context = prev_ctx;
}

/* 108E:01D5 */
void ui_flip(void) { set_draw_page(!draw_page); set_display_page(draw_page); pf_present(); }

/* palette fades (108E:0009 to grey/white, 0036 restore, 01A9 to black) */
void ui_fade_grey(int d) { int i; for (i = 0; i < 16; i++) { pf_delay_ms(d); set_palette_reg(i, 0x17); pf_present(); } }
void ui_fade_out(int d)  { int i; for (i = 0; i < 16; i++) { pf_delay_ms(d); set_palette_reg(i, 0); pf_present(); } }
void ui_fade_in(int d)
{
    int i;
    for (i = 0; i < 16; i++) {
        set_palette_reg(i, i < 8 ? i : i + 8);
        pf_delay_ms(d);
        pf_present();
    }
}

/* 108E:3114  clear page 0 with tile 0 (needs the latch write mode) */
void ui_clear_page0(void)
{
    int di, si;
    dl_reset();                          /* (port) leaving the play screen */
    port_picture = -1;                   /* (port) no full-screen picture any more */
    port_text_tile = -1;                 /* ... nor a tiled text page */
    port_level_intro = 0;
    dl_wide_shown = 0;
    ega_outw(0x3C4, 0x0F02);
    set_draw_page(0);
    for (di = 0; di < 0x19; di++)
        for (si = 0; si < 0x28; si += 2)
            blit_tile(0, (uint16_t)(di * 0x280 + si));
}

/* 108E:29A2  play-field frame + status panel separators (current page) */
void ui_frame(void)
{
    int si;
    WRITE_MODE_LATCH();
    for (si = 0x280; si < 0x1B80; si += 0x280) {
        blit_tile(0x3000, (uint16_t)si);
        blit_tile(0x3020, (uint16_t)(si + 0x1C));
    }
    for (si = 2; si < 0x1C; si += 2) {
        blit_tile(0x3040, (uint16_t)si);
        blit_tile(0x3060, (uint16_t)(si + 0x1B80));
    }
    blit_tile(0x31C0, 0x141C);
    blit_tile(0x31C0, 0x065C);
    blit_tile(0x31C0, 0x0C9C);
    blit_tile(0x3080, 0x0000);
    blit_tile(0x30A0, 0x001C);
    blit_tile(0x30C0, 0x1B9C);
    blit_tile(0x30E0, 0x1B80);
    blit_tile(0x3340, 0x1B8B);
    blit_tile(0x3360, 0x1B8D);
    blit_tile(0x3380, 0x1B8F);
    blit_tile(0x33A0, 0x1B91);
}

/* 108E:2AB9  status panel (both pages) */
void ui_status_panel(void)
{
    int di = 0, si;
    WRITE_MODE_LATCH();
    ui_frame();
    do {
        di++;
        set_draw_page(di - 1);
        blit_tile(0x3080, 0); blit_tile(0x30A0, 0x1C); blit_tile(0x30C0, 0x1B9C); blit_tile(0x30E0, 0x1B80);
        blit_tile(0x3100, 0x1E); blit_tile(0x3100, 0x20); blit_tile(0x3100, 0x22); blit_tile(0x3100, 0x24);
        blit_tile(0x3120, 0x26); blit_tile(0x3160, 0x1BA6);
        for (si = 0x280; si < 0x1B80; si += 0x280) blit_tile(0x3140, (uint16_t)(si + 0x26));
        blit_tile(0x3100, 0x141E); blit_tile(0x3100, 0x0C9E); blit_tile(0x3100, 0x1420); blit_tile(0x3100, 0x0CA0);
        blit_tile(0x3100, 0x1422); blit_tile(0x3100, 0x0CA2); blit_tile(0x3100, 0x1424); blit_tile(0x3100, 0x0CA4);
        blit_tile(0x31E0, 0x1426); blit_tile(0x31E0, 0x0CA6);
        blit_tile(0x3100, 0x1B9E); blit_tile(0x3100, 0x1BA0); blit_tile(0x3100, 0x1BA2); blit_tile(0x3100, 0x1BA4);
        blit_tile(0x3100, 0x065E); blit_tile(0x3480, 0x0660); blit_tile(0x34A0, 0x0662); blit_tile(0x3100, 0x0664);
        blit_tile(0x34C0, 0x0020); blit_tile(0x34E0, 0x0022);
        blit_tile(0x33C0, 0x141F); blit_tile(0x33E0, 0x1421); blit_tile(0x3400, 0x1423);
        blit_tile(0x3420, 0x0C9F); blit_tile(0x3440, 0x0CA1); blit_tile(0x3460, 0x0CA3);
        blit_tile(0x31E0, 0x0666);
    } while (di == 1);
}

/* 108E:1454  "get ready" box on page 0 */
void ui_get_ready(void)
{
    draw_page = 0;
    ui_box(2, 5, 1);
    ui_text(ds_str(0x308D), 3, 0x0B);
    ui_text(ds_str(0x30A6), 3, 0x0C);
}

/* 108E:3F68  wavy scrolling promo line shown during the attract-mode demo */
void ui_marquee(void)
{
    int si;
    if (marquee_delay < 0x28) marquee_delay++;
    if (marquee_delay == 0x28) {
        marquee_pos += 2;
        if (marquee_pos % 0x1A == 0) marquee_delay = 0;
        if (marquee_pos > 0x1EE) marquee_pos = 0;
    }
    if (++marquee_phase == 4) marquee_phase = 0;
    for (si = 0; si < 0x1A; si++)
        ui_char(ds_u8((uint16_t)(marquee_pos + si + 0xD04)), si + 2,
                ds_u8((uint16_t)(marquee_phase + si + 0xCE1)) + 0xA2);
}
