/*
 * actors6.c - helicopters, balloons, security cameras, static decorations,
 * atomic health molecules, falling signs   (seg 0097: 7DFF..8EBC)
 */
#include "dn1.h"

/* 0097:7DFF  helicopter body, 4 tiles wide */
static void draw_heli_body(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x280 + 0x6680);
    int k;
    for (k = 0; k < 4; k++)
        if (!offscreen(x + k * 2, y))
            draw_sprite(anim_gfx + (uint16_t)(g + k * 0xA0), SCREEN_X(x) + k * 2, SCREEN_Y(y));
    if (player_touches(x + 2, y - 0x40) || player_touches(x + 4, y - 0x40)) player_hurt = 1;
}

/* 0097:7F4A  helicopter rotor, 4 tiles wide (whirr sound when fully visible) */
static void draw_heli_rotor(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x280 + 0x6E00);
    int k;
    for (k = 0; k < 4; k++)
        if (!offscreen(x + k * 2, y)) {
            draw_sprite(anim_gfx + (uint16_t)(g + k * 0xA0), SCREEN_X(x) + k * 2, SCREEN_Y(y));
            if (k == 3 && !unheard(x + k * 2, y)) play_sound(0x16);     /* (port: unheard) */
        }
}

/* ------------------------------------------------------------------------
 * 0097:8069  helicopters (3022h).  Obj12: s facing (1 front, 2 left, 3 right),
 * t climb counter (18h = rising), u hit points (4), v dive counter.
 * They hover toward Duke, swoop down when above him, and crash when killed.
 * ---------------------------------------------------------------------- */
void act_helis(void)
{
    int i;
    heli_rotor = !heli_rotor;
    for (i = 0; i < n_helis; i++) {
        Obj12 *h = &helis[i];
        if (h->s == 0) continue;
        if (!offscreen(h->x, h->y) && h->t == 0)
            h->t = solid_at(h->x + 3, h->y + 0x80) ? 0x18 : 1;
        if (h->t == 0) continue;
        if (h->t > 2) { h->t--; h->y -= 0x10; }
        if (h->v > 1 && h->t <= 2) {
            h->y += 0x10;
            h->v++;
            if (h->v >= 0x18 || solid_at(h->x + 3, h->y + 0x80)) { h->t = 0x18; h->v = 0; }
        }
        if (h->x + 3 > player_x + scroll_x) {
            h->s = 2;
            if ((anim_tick == 0 || anim_tick == 0x40) && !shot_wall(h->x - 2, h->y)) h->x--;
        } else if (h->x + 3 < player_x + scroll_x) {
            h->s = 3;
            if ((anim_tick == 0 || anim_tick == 0x40) && !shot_wall(h->x + 8, h->y)) h->x++;
        } else {
            if (h->t < 3 && h->v == 0) h->v = 2;
            h->s = 1;
        }
        if ((shot_hits(h->x, h->y) || shot_hits(h->x + 8, h->y) || shot_hits(h->x + 4, h->y - 0x80)) && h->u != 0)
            h->u--;
        if (h->u < 4 && anim_tick == 0)    spawn_explosion(h->x + 3, h->y - 0x60);
        if (h->u < 3 && anim_tick == 0x20) spawn_explosion(h->x, h->y - 0x60);
        if (h->u < 2 && anim_tick == 0x40) spawn_explosion(h->x + 2, h->y - 0x80);
        if (h->u == 0) {
            if (solid_at(h->x, h->y + 0x80) || solid_at(h->x + 8, h->y + 0x80)) {
                spawn_debris(h->x, h->y);
                spawn_debris(h->x + 2, h->y - 0x40);
                spawn_debris(h->x + 4, h->y);
                spawn_debris(h->x + 6, h->y - 0x40);
                spawn_debris(h->x + 8, h->y);
                spawn_faller(h->x + 1, h->y - 0x20);
                spawn_faller(h->x + 5, h->y - 0x80);
                spawn_spark(1, h->x + 1, h->y - 0x20);
                spawn_spark(1, h->x + 7, h->y - 0x40);
                spawn_spark(1, h->x + 1, h->y - 0x20);
                spawn_spark(1, h->x + 2, h->y - 0x80);
                spawn_spark(1, h->x + 3, h->y - 0x80);
                spawn_spark(1, h->x + 5, h->y - 0x20);
                h->s = 0;
                add_score(5000);
                continue;
            }
            h->y += 0x20;                                   /* crashing */
        }
        switch (h->s) {
        case 1: draw_heli_rotor(heli_rotor + 2, h->x, h->y - 0x80); draw_heli_body(1, h->x, h->y); break;
        case 2: draw_heli_rotor(heli_rotor,     h->x, h->y - 0x80); draw_heli_body(0, h->x, h->y); break;
        case 3: draw_heli_rotor(heli_rotor + 4, h->x, h->y - 0x80); draw_heli_body(2, h->x, h->y); break;
        }
    }
}

/* ------------------------------------------------------------------------
 * 0097:8506  balloons (3023h): a blue crate; shoot it and a balloon rises -
 * touch it for 10000 points, shoot it and it pops.
 * ---------------------------------------------------------------------- */
void act_balloons(void)
{
    int i;
    for (i = 0; i < n_balloons; i++) {
        Obj6 *b = &balloons[i];
        if (b->s == 0) continue;
        if (b->s == 1) {
            if (!solid_at(b->x, b->y + 0x80)) b->y += 0x40;
            if (offscreen(b->x, b->y)) continue;
            draw_obj(0x3E80, b->x, b->y);
            if (shot_hits(b->x, b->y)) {
                spawn_debris(b->x, b->y);
                if (!solid_at(b->x, b->y + 0x80)) { add_score(500); spawn_score(0x280, b->x, b->y); }
                b->s = 2;
            }
            continue;
        }
        if (anim_tick == 0) balloon_anim += 0xA0;
        if (balloon_anim == 0x1E0) balloon_anim = 0;
        if (!solid_at(b->x, b->y - 0x80)) b->y -= 8;
        if (shot_hits(b->x, b->y - 0x80) || solid_at(b->x, b->y - 0x80)) {
            if (!offscreen(b->x, b->y - 0x80)) draw_sprite(object_gfx + 0x2DA0, SCREEN_X(b->x), SCREEN_Y(b->y) - 16);
            b->s = 0;
            spawn_explosion(b->x, b->y - 0x80);
            continue;
        }
        if (!offscreen(b->x, b->y - 0x80)) draw_sprite(object_gfx + 0x2B20, SCREEN_X(b->x), SCREEN_Y(b->y) - 16);
        if (!offscreen(b->x, b->y)) draw_obj(balloon_anim + 0x2BC0, b->x, b->y);
        if (player_touches(b->x, b->y)) {
            add_score(10000);
            spawn_score(0x780, b->x, b->y - 0x80);
            play_sound(3);
            b->s = 0;
        }
    }
}

/* ------------------------------------------------------------------------
 * 0097:87BF  security cameras (3024h): look toward Duke; 100 points each,
 * a bonus is awarded at the level end if all were shot (bonus_4ed6).
 * ---------------------------------------------------------------------- */
void draw_cameras(void)
{
    int i;
    bonus_4ed6 = 1;
    for (i = 0; i < n_cameras; i++) {
        Obj6 *c = &cameras[i];
        if (c->s == 0) continue;
        bonus_4ed6 = 0;
        if (offscreen(c->x, c->y)) continue;
        if (c->x - 2 > player_x + scroll_x)      draw_anim(0x7D00, c->x, c->y);
        else if (c->x + 2 < player_x + scroll_x) draw_anim(0x7E40, c->x, c->y);
        else                                     draw_anim(0x7DA0, c->x, c->y);
        if (shot_hits(c->x, c->y)) {
            spawn_score(0, c->x, c->y);
            add_score(100);
            spawn_spark(1, c->x, c->y);
            c->s = 0;
            play_sound(0x12);
        }
    }
}

/* 0097:892B  static decorations (3025h..3028h, 303Dh..303Fh): s = anim gfx offset */
void draw_decor(void)
{
    unsigned i;
    for (i = 0; i < (unsigned)(uint16_t)n_decor; i++) {
        Obj6 *d = &decor[i];
        if (offscreen(d->x, d->y)) continue;
        draw_sprite(anim_gfx + (uint16_t)d->s, SCREEN_X(d->x), (int)((uint16_t)(d->y - scroll_y) >> 3) + 16);
    }
}

/* ------------------------------------------------------------------------
 * 0097:899E  atomic health molecule (3029h): full health + 1000.
 * ---------------------------------------------------------------------- */
void hint_molecule(void);   /* 108E:165E */
void act_molecules(void)
{
    int i;
    molecule_anim += 0xA0;
    if (molecule_anim == 0x5A0) molecule_anim = 0;
    for (i = 0; i < n_molecules; i++) {
        Obj6 *m = &molecules[i];
        if (m->s == 0 || offscreen(m->x, m->y)) continue;
        if (!solid_at(m->x, m->y + 0x80)) m->y += 0x80;
        if (m->s == 1 && shot_hits(m->x, m->y)) {
            spawn_debris(m->x, m->y);
            m->s = 2;
            if (!solid_at(m->x, m->y + 0x80)) { add_score(500); spawn_score(0x280, m->x, m->y); }
        }
        if (m->s == 2 && player_touches(m->x, m->y)) {
            m->s = 0;
            health = 9;
            add_score(1000);
            spawn_score(0x3C0, m->x, m->y);
            draw_health();
            play_sound(4);
            if (hint_5ec8) { hint_5ec8 = 0; hint_molecule(); }
            continue;
        }
        if (m->s == 1)
            draw_sprite(object_gfx, SCREEN_X(m->x), (int)((uint16_t)(m->y - scroll_y) >> 3) + 16);
        else
            draw_sprite(object_gfx + (uint16_t)(molecule_anim + 0x2E40), SCREEN_X(m->x), (int)((uint16_t)(m->y - scroll_y) >> 3) + 16);
    }
}

/* ------------------------------------------------------------------------
 * 0097:8B5B  falling signs (302Ah): wobble when Duke walks under them, then
 * drop (s >= 10h) and hurt.  bonus_531c: all signs destroyed.
 * ---------------------------------------------------------------------- */
void act_signs(void)
{
    int i;
    bonus_531c = 1;
    for (i = 0; i < n_signs; i++) {
        Obj6 *s = &signs[i];
        int dy;
        if (s->s == 0) continue;
        bonus_531c = 0;
        if (offscreen(s->x, s->y)) {
            if (s->s > 0x0F) s->s = 0;
            continue;
        }
        if (s->s > 1) {
            if (solid_at(s->x + 1, s->y + 0x80)) {
                spawn_debris(s->x + 1, s->y);
                spawn_explosion(s->x + 1, s->y);
                s->s = 0;
                continue;
            }
            s->s++;
        }
        dy = ((player_y >> 4) << 7) + scroll_y - s->y;
        if (s->s < 2 && s->x - 1 <= player_x + scroll_x && s->x + 3 >= player_x + scroll_x && dy < 0x400 && dy > 0)
            s->s = 2;
        if (s->s == 0x10) play_sound(0x20);
        if (s->s > 0x0F) {
            s->y += 0x60;
            if (player_touches(s->x, s->y) || player_touches(s->x + 2, s->y)) player_hurt = 1;
            if (shot_hits(s->x, s->y - 0x40) || shot_hits(s->x + 2, s->y - 0x40)) {
                spawn_debris(s->x + 1, s->y);
                spawn_explosion(s->x + 1, s->y);
                spawn_spark(1, s->x + 1, s->y);
                add_score(500);
                spawn_score(0x280, s->x + 1, s->y);
                s->s = 0;
                continue;
            }
        }
        if (s->s == 1 || s->s >= 0x0F) {
            if (!offscreen(s->x, s->y))     draw_obj(0x33E0, s->x, s->y);
            if (!offscreen(s->x + 2, s->y)) draw_sprite(object_gfx + 0x3480, SCREEN_X(s->x) + 2, SCREEN_Y(s->y));
        }
        if (s->s < 0x0F) {                                  /* wobble */
            int w = s->s % 2;
            if (!offscreen(s->x, s->y))     draw_sprite(object_gfx + 0x33E0, SCREEN_X(s->x), SCREEN_Y(s->y) + w);
            if (!offscreen(s->x + 2, s->y)) draw_sprite(object_gfx + 0x3480, SCREEN_X(s->x) + 2, SCREEN_Y(s->y) + w);
        }
    }
}
