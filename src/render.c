/*
 * render.c - play-field and status-panel drawing (seg 0097 + 108E bits).
 *
 * The view is 13 x 10 tiles at screen byte column 2, line 16.  Map cells are
 * VRAM offsets into the tile cache; cell 0 shows the level's backdrop
 * (DROPn.DN1, cached at tile-cache offset 4000h) and cell 20h the second
 * backdrop (offset 8100h).  The backdrop does not scroll with the map: it is
 * drawn screen-fixed, shifted by one byte column when scroll_x is odd.
 * Cells 21h..5DFh are 4-frame animated tiles (anim_tick adds 0/20h/40h/60h).
 */
#include "dn1.h"

/* 0097:14AF */
void draw_world(void)
{
    int di = 0;
    int si = 0x282 - (scroll_x & 1);
    uint16_t cell = (uint16_t)(((scroll_x >> 1) << 1) + (scroll_y << 1));
    uint16_t back = 0x4000;

    WRITE_MODE_LATCH();
    anim_tick += 0x20;
    if (anim_tick == 0x80) anim_tick = 0;
    dl_world();                          /* (port) smooth motion: camera + map cells */
    dl_in_world = 1;

    do {
        uint16_t c = MAP16(cell);
        if (scroll_x & 1) {
            if (c == 0 || MAP16(cell + 2) == 0)            blit_tile(back, (uint16_t)(si + 1));
            else if (c == 0x20 || MAP16(cell + 2) == 0x20) blit_tile((uint16_t)(back + 0x4100), (uint16_t)(si + 1));
        } else {
            if (c == 0)          blit_tile(back, (uint16_t)si);
            else if (c == 0x20)  blit_tile((uint16_t)(back + 0x4100), (uint16_t)si);
        }
        if (c > 0x20) {
            if (c < 0x5E0) blit_tile((uint16_t)(c + anim_tick), (uint16_t)si);
            else           blit_tile(c, (uint16_t)si);
        }
        cell += 2;
        si += 2;
        di++;
        if (di == 0x0E) {
            si += 0x264;
            cell += 0xE4;
            di = 0;
        } else {
            back += 0x20;
        }
    } while (si < 0x191C);
    dl_in_world = 0;
    WRITE_MODE_NORMAL();
}

/* 0097:0A0E  health bar: 8 cells, both pages */
void draw_health(void)
{
    dl_hud++;                            /* (port) status panel: never clipped */
    int i, pass;
    for (i = 0; i < 8; i++) {
        for (pass = 0; pass < 2; pass++) {
            set_draw_page(pass == 0 ? draw_page : !draw_page);
            draw_sprite(object_gfx + 0x26C0, i + 0x1E, 0x3C);
            if (health - 1 > i) draw_sprite(object_gfx + 0x2620, i + 0x1E, 0x3C);
        }
        death_timer = 0;
    }
    dl_hud--;
}

/* 0097:0ACC */
void add_health(void)
{
    if (health < 9) health++;
    draw_health();
}

/* 0097:0ADC  firepower meter (one gun icon per shot slot) */
void draw_firepower(void)
{
    dl_hud++;                            /* (port) status panel: never clipped */
    int i;
    for (i = 0; i < max_shots; i++) {
        set_draw_page(draw_page);
        draw_sprite(object_gfx + 0x1AE0, 0x21, 0x60);
        draw_sprite(object_gfx + 0x500, i * 2 + 0x1E, 0x6C);
        set_draw_page(!draw_page);
        draw_sprite(object_gfx + 0x1AE0, 0x21, 0x60);
        draw_sprite(object_gfx + 0x500, i * 2 + 0x1E, 0x6C);
    }
    dl_hud--;
}

/* 0097:BB9F  coloured key slots in the inventory box, both pages */
void draw_inventory(void)
{
    dl_hud++;                            /* (port) status panel: never clipped */
    int page = draw_page, n = 0;
    do {
        n++;
        set_draw_page(page);
        draw_sprite(object_gfx + (have_key_r ? 0x4D80 : 0x4C40), 0x1E, 0x90);
        draw_sprite(object_gfx + (have_key_g ? 0x4E20 : 0x4C40), 0x20, 0x90);
        draw_sprite(object_gfx + (have_key_b ? 0x4EC0 : 0x4C40), 0x22, 0x90);
        draw_sprite(object_gfx + (have_key_p ? 0x4F60 : 0x4C40), 0x24, 0x90);
        page = !page;
    } while (n == 1);
    dl_hud--;
}

/* 108E:1FFA  append an item icon to the inventory box (both pages) */
void inventory_add(const uint8_t *gfx)
{
    dl_hud++;                            /* (port) status panel: never clipped */
    set_draw_page(draw_page);
    draw_sprite(gfx, inv_x + 0x1E, inv_y + 0xA0);
    set_draw_page(!draw_page);
    draw_sprite(gfx, inv_x + 0x1E, inv_y + 0xA0);
    inv_x += 2;
    dl_hud--;
}

/* 108E:1F9C  access card slot */
void inventory_draw_card(void)
{
    dl_hud++;                            /* (port) status panel: never clipped */
    set_draw_page(draw_page);
    draw_sprite(object_gfx + 0x2800, 0x24, inv_y + 0xA0);
    set_draw_page(!draw_page);
    draw_sprite(object_gfx + 0x2800, 0x24, inv_y + 0xA0);
    dl_hud--;
}

/* 0097:290F  smoothly re-centre the view vertically on Duke */
void recenter_view(void)
{
    if (boss_dead || !recenter_y) return;
    if (player_y < 0x64 && scroll_y > 0) {
        scroll_y -= 0x80;
        player_y += 0x10;
        if (player_y < 0x64) return;
    } else {
        if (player_y <= 0x70) return;
        scroll_y += 0x80;
        player_y -= 0x10;
        if (player_y > 0x70) return;
    }
    recenter_y = 0;
}

/* 0097:6AFD  mirror floors: copy the scene above upside-down (latched) */
void draw_mirrors(void)
{
    int i;
    WRITE_MODE_LATCH();
    mirror_anim += 0x28;
    if (mirror_anim > 0x28) mirror_anim = 0;
    for (i = 0; i < n_mirrors; i++) {
        Obj4 *m = &mirrors[i];
        if (!offscreen(m->x, m->y))
            blit_column_up(SCREEN_X(m->x), SCREEN_Y(m->y), mirror_anim);
    }
}
