/*
 * port_hud.c - the Immersive HUD: a slim bar along the bottom of the screen.
 *
 * The bar's art (resources/hud/hud_bar.png, built in through hud_data.h) is
 * the frame; what goes in it is taken from the original status panel, which
 * the game keeps drawing into EGA memory as always: the health cells, the
 * score digits, the pistol, the keys and the inventory items are copied from
 * where the game drew them, so the bar always shows exactly what the panel
 * would (items in the order they were picked up, the access card in the
 * last slot, empty slots black).  Only the firepower meter differs: one of
 * four pictures (gun_power_1..4.png) for the number of shots, instead of the
 * panel's row of repeated icons.
 *
 * Everything is palette slots, so the bar fades with the game.  In a wide
 * picture the bar keeps its 320-pixel size, centred.
 */
#include "dn1.h"
#include "port.h"
#include <string.h>
#include "hud_data.h"

/* copy a rectangle of the original panel (320-wide indexed page) into the picture */
static void copy_panel(uint8_t *out, int w, const uint8_t *base, int sx, int sy, int cw, int ch, int dx, int dy)
{
    int y;
    for (y = 0; y < ch; y++) {
        if (dy + y < 0 || dy + y >= 200) continue;
        memcpy(out + (dy + y) * w + dx, base + (sy + y) * 320 + sx, (size_t)cw);
    }
}

static void blit(uint8_t *out, int w, const uint8_t *src, int sw, int sh, int dx, int dy)
{
    int x, y;
    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++)
            if (src[y * sw + x] != 255) out[(dy + y) * w + dx + x] = src[y * sw + x];
}

int hud_bar_top(void) { return HUD_BAR_TOP; }

void hud_draw(uint8_t *out, int w, const uint8_t *base)
{
    int bx = (w - 320) / 2, k;
    if (bx < 0) return;
    blit(out + HUD_BAR_TOP * w, w, &hud_bar[0][0], 320, 200 - HUD_BAR_TOP, bx, 0);
    /* (panel positions: the game's own, in draw_health / draw_number /
     * draw_firepower / draw_inventory / inventory_add; bar positions: the
     * HUD art's compartments) */
    copy_panel(out, w, base, 240, 61, 65, 14, bx + 8, 180);         /* health: 8 cells, 8 px apart */
    copy_panel(out, w, base, 240, 24, 64, 8, bx + 83, 184);         /* score: 8 digits             */
    copy_panel(out, w, base, 264, 96, 16, 16, bx + 154, 178);       /* the pistol                  */
    {
        static const uint8_t *const power[4] = { &hud_power1[0][0], &hud_power2[0][0], &hud_power3[0][0], &hud_power4[0][0] };
        int n = max_shots < 1 ? 1 : max_shots > 4 ? 4 : max_shots;
        blit(out, w, power[n - 1], 16, 7, bx + 171, 182);
    }
    for (k = 0; k < 4; k++)                                          /* keys (10 px wide, 11 apart) */
        copy_panel(out, w, base, 240 + 16 * k + 3, 144, 10, 16, bx + 196 + 11 * k, 179);
    for (k = 0; k < 4; k++)                                          /* items, card last            */
        copy_panel(out, w, base, 240 + 16 * k, 160, 16, 16, bx + 248 + 16 * k, 178);
}
