/*
 * port_hud.c - the Immersive HUD: a slim bar along the bottom of the screen.
 *
 * Four bars (resources/hud/, built in through hud_data.h): 4:3 (320 wide)
 * and 16:9 (424 wide, with a D-U-K-E box), each for Immersive (laid over the
 * play) and Immersive Alt (on a black band below it).  The 16:9 bar is used
 * whenever the picture is at least 424 wide, the 4:3 one otherwise (Classic
 * aspect, and Wide windows narrower than 16:9).  Bars are centred; with
 * Immersive the play shows either side; with Alt the bar's end columns are
 * repeated out to the edges whenever the picture is wider than the bar.
 *
 * What goes in the bar is taken from the original status panel, which the
 * game keeps drawing into EGA memory as always: health cells, score digits,
 * the pistol, keys and inventory items are copied from where the game drew
 * them, so the bar always shows exactly what the panel would (items in the
 * order they were picked up, the access card in the last slot, empty slots
 * black).  The firepower meter is one of four pictures (gun_power_1..4.png)
 * for the number of shots.  The D-U-K-E letters - not on the original
 * panel - are worked out from the game's own record of them (see below).
 *
 * Everything is palette slots, so the bar fades with the game.
 */
#include "dn1.h"
#include "port.h"
#include <string.h>
#include "hud_data.h"

typedef struct {
    const uint8_t *bar;          /* HUD_BAR_TOP.. rows, w wide, 255 = transparent */
    int w;
    int o;                       /* how far the shared elements sit to the right  */
    int letters;                 /* has the D-U-K-E box                           */
} Layout;

/* copy a rectangle of the original panel (320-wide indexed page) into the picture */
static void copy_panel(uint8_t *out, int w, const uint8_t *base, int sx, int sy, int cw, int ch, int dx, int dy)
{
    int y;
    for (y = 0; y < ch; y++) memcpy(out + (dy + y) * w + dx, base + (sy + y) * 320 + sx, (size_t)cw);
}

static void blit(uint8_t *out, int w, const uint8_t *src, int sw, int sh, int dx, int dy)
{
    int x, y;
    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++)
            if (src[y * sw + x] != 255) out[(dy + y) * w + dx + x] = src[y * sw + x];
}

/* a 16x16 object sprite (object_gfx: per 8 pixels mask, blue, green, red,
 * intensity), drawn with its mask */
static void sprite(uint8_t *out, int w, uint16_t gfx, int dx, int dy)
{
    const uint8_t *g = object_gfx + gfx;
    int r, b, bit, p;
    for (r = 0; r < 16; r++)
        for (b = 0; b < 2; b++) {
            const uint8_t *q = g + (r * 2 + b) * 5;
            for (bit = 0; bit < 8; bit++) {
                int s = 7 - bit, c = 0;
                if (!((q[0] >> s) & 1)) continue;
                for (p = 0; p < 4; p++) c |= ((q[1 + p] >> s) & 1) << p;
                out[(dy + r) * w + dx + b * 8 + bit] = (uint8_t)c;
            }
        }
}

/* Which D-U-K-E letters this level has been collected.  The game records
 * D, U and K in pickup order (letter_seq, letter_count - the bonus needs
 * them in order before E); E itself is not recorded, but every collected
 * letter's object is cleared, so E has been picked up when one more letter
 * object is cleared than letter_count accounts for.  All game state: right
 * after a quickload too. */
static void letters_got(int got[4])
{
    int i, cleared = 0, n = letter_count < 0 ? 0 : letter_count > 3 ? 3 : letter_count;
    memset(got, 0, 4 * sizeof got[0]);
    for (i = 0; i < n; i++) {
        if (letter_seq[i] == 'D') got[0] = 1;
        if (letter_seq[i] == 'U') got[1] = 1;
        if (letter_seq[i] == 'K') got[2] = 1;
    }
    for (i = 0; i < n_letters && i < 6; i++) if (letters[i].s == 0) cleared++;
    if (cleared > letter_count) got[3] = 1;
}

int hud_bar_top(void) { return HUD_BAR_TOP; }

void hud_draw(uint8_t *out, int w, const uint8_t *base)
{
    static const uint16_t letter_gfx[4] = { 0x49C0, 0x4A60, 0x4B00, 0x4BA0 };   /* (act_letters) */
    static const uint8_t *const power[4] = { &hud_power1[0][0], &hud_power2[0][0], &hud_power3[0][0], &hud_power4[0][0] };
    int alt = cfg.hud == HUD_IMMERSIVE_ALT, k, bx, o, y, n;
    Layout L;
    uint8_t *row0 = out + HUD_BAR_TOP * w;
    if (w >= 424) { L.bar = alt ? &hud_wide_alt[0][0] : &hud_wide[0][0]; L.w = 424; L.o = 16; L.letters = 1; }
    else          { L.bar = alt ? &hud_43_alt[0][0] : &hud_43[0][0];     L.w = 320; L.o = 0;  L.letters = 0; }
    if (w < L.w) return;
    bx = (w - L.w) / 2; o = bx + L.o;
    blit(row0, w, L.bar, L.w, 200 - HUD_BAR_TOP, bx, 0);
    /* an Alt bar in a wider picture: its end columns repeated to the edges
     * (the 16:9 bar beyond 16:9, and the 4:3 bar in Wide (Fill) windows too
     * narrow for the 16:9 one) */
    if (alt && w > L.w)
        for (y = 0; y < 200 - HUD_BAR_TOP; y++) {
            uint8_t l = L.bar[y * L.w], r = L.bar[y * L.w + L.w - 1], *d = row0 + y * w;
            if (l != 255) memset(d, l, (size_t)bx);
            if (r != 255) memset(d + bx + L.w, r, (size_t)(w - bx - L.w));
        }
    /* (panel positions: the game's own, in draw_health / draw_number /
     * draw_firepower / draw_inventory / inventory_add; bar positions: the
     * HUD art's compartments, as in guide_*_full.png) */
    copy_panel(out, w, base, 240, 61, 65, 14, o + 8, 180);           /* health: 8 cells, 8 px apart */
    copy_panel(out, w, base, 240, 24, 64, 8, o + 83, 184);           /* score: 8 digits             */
    copy_panel(out, w, base, 264, 96, 16, 16, o + 154, 179);         /* the pistol                  */
    n = max_shots < 1 ? 1 : max_shots > 4 ? 4 : max_shots;
    blit(out, w, power[n - 1], 16, 7, o + 171, 183);                 /* firepower                   */
    for (k = 0; k < 4; k++)                                          /* keys (10 px wide, 11 apart) */
        copy_panel(out, w, base, 240 + 16 * k + 3, 144, 10, 16, o + 196 + 11 * k, 179);
    {
        static const int slot[4] = { 247, 264, 281, 297 };           /* items; the card last        */
        for (k = 0; k < 4; k++) copy_panel(out, w, base, 240 + 16 * k, 160, 16, 16, o + slot[k], 179);
    }
    if (L.letters) {                                                 /* D, U, K, E: each its own slot */
        int got[4];
        letters_got(got);
        for (k = 0; k < 4; k++) if (got[k]) sprite(out, w, letter_gfx[k], bx + 336 + 17 * k, 179);
    }
}
