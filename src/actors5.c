/*
 * actors5.c - turkey, collapsing bridge, force fields, fans, bonus crates
 * (seg 0097: 7163..7DFA)
 */
#include "dn1.h"

/* ------------------------------------------------------------------------
 * 0097:7163  turkey (3018h; 3051h = already unboxed).
 * s: 1 boxed, 2 drumstick (+1 health, 100), 3 roast (shoot the drumstick:
 * +2 health, 200).
 * ---------------------------------------------------------------------- */
void hint_turkey(void);   /* 108E:15C9 */
void act_turkeys(void)
{
    int i;
    for (i = 0; i < n_turkey; i++) {
        Obj6 *t = &turkeys[i];
        if (t->s == 0 || offscreen(t->x, t->y)) continue;
        if (!solid_at(t->x, t->y + 0x80)) t->y += 0x80;
        if (player_touches(t->x, t->y)) {
            if (t->s > 1 && hint_637e) { hint_637e = 0; hint_turkey(); }
            if (t->s == 2) {
                t->s = 0; add_score(100); spawn_score(0, t->x, t->y); play_sound(1);
                add_health();
                continue;
            }
            if (t->s == 3) {
                t->s = 0; add_score(200); spawn_score(0x140, t->x, t->y); play_sound(1);
                add_health(); add_health();
                continue;
            }
        }
        if (t->s != 3 && shot_hits(t->x, t->y)) {
            if (t->s == 1) {
                spawn_debris(t->x, t->y);
                if (!solid_at(t->x, t->y + 0x80)) { add_score(500); spawn_score(0x280, t->x, t->y); }
                t->s = 2;
            } else if (t->s == 2) {
                t->s = 3;
            }
        }
        if (t->s == 1) draw_obj(0x3F20, t->x, t->y);
        else {
            if (t->s == 2) draw_obj(0x1B80, t->x, t->y);
            if (t->s == 3) draw_obj(0x1C20, t->x, t->y);
        }
        if (t->s > 1 && hint_637e && player_touches(t->x, t->y)) { hint_637e = 0; hint_turkey(); }
    }
}

/* ------------------------------------------------------------------------
 * 0097:73D3  the collapsing bridge (3019h): it holds the first time Duke
 * walks over it and crumbles when he steps on it again.
 * bridge_state: 0 untouched, 1 being walked on, 2 walked off once.
 * ---------------------------------------------------------------------- */
void act_bridge(void)
{
    int i;
    if (!bridge_present) return;
    if (player_in_span(bridge_x - 2, bridge_x + bridge_len, bridge_y - 0x20)) {
        if (bridge_state == 2) {
            for (i = 0; (bridge_len >> 1) > i; i++) {
                uint16_t o = (uint16_t)((bridge_col << 1) + (i << 1) + ((bridge_row << 7) << 1) - 4);
                MAP16(o) = MAP16(o - 0x100);
                if (i % 2) spawn_debris((bridge_x << 1) + (i << 1), bridge_y);
                else       spawn_spark(1, (bridge_x << 1) + (i << 1), bridge_y);
            }
            bridge_present = 0;
            bridge_state = 0;
        }
        bridge_state = 1;
    } else if (bridge_state == 1) {
        bridge_state = 2;
    }
}

/* ------------------------------------------------------------------------
 * 0097:74C2  horizontal force fields (301Ah): always hurt on contact.
 * ---------------------------------------------------------------------- */
void act_ffields_h(void)
{
    int i;
    ffh_anim += 0xA0;
    if (ffh_anim == 0x280) ffh_anim = 0;
    for (i = 0; i < n_ffield_h; i++) {
        FField *f = &ffields_h[i];
        if (f->on == 0 || offscreen(f->x, f->y)) continue;
        if (player_touches(f->x, f->y)) {
            draw_obj(ffh_anim + 0x21C0, f->x, f->y);
            player_hurt = 1;
        } else {
            draw_obj(ffh_anim + 0x1F40, f->x, f->y);
        }
        if (anim_tick == 0) play_sound(0x15);
    }
}

/* ------------------------------------------------------------------------
 * 0097:7CFA  vertical force fields (3021h): block Duke and absorb shots.
 * Records are 4 bytes {x,y}; the "active" word the code tests lies at +4,
 * i.e. in the *next* record (the loader writes 1 there) - reproduced as is.
 * ---------------------------------------------------------------------- */
void act_ffields_v(void)
{
    int i;
    ffv_anim += 0xA0;
    if (ffv_anim == 0x280) ffv_anim = 0;
    for (i = 0; i < n_ffield_v; i++) {
        int16_t x = ffields_v[i * 2], y = ffields_v[i * 2 + 1];
        int16_t on = (i * 2 + 2 < 200) ? ffields_v[i * 2 + 2] : rabbits[0].s;
        if (on == 0 || offscreen(x, y)) continue;
        shot_hits(x, y);
        if (player_touches(x + 1, y) && in_left && can_walk_right()) player_x++;
        if (player_touches(x - 1, y) && in_right && can_walk_left()) player_x--;
        draw_obj(ffv_anim + 0x28A0, x, y);
        if (anim_tick == 0) play_sound(0x15);
    }
}

/* 0097:75B9  fan sprite (1x2 tiles) */
static void draw_fan(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x140 + 0x6180);
    if (!offscreen(x, y - 0x80)) draw_sprite(anim_gfx + g, SCREEN_X(x), SCREEN_Y(y) - 16);
    if (!offscreen(x, y))        draw_sprite(anim_gfx + (uint16_t)(g + 0xA0), SCREEN_X(x), SCREEN_Y(y));
}

/* ------------------------------------------------------------------------
 * 0097:7647  fans (301Bh blows left, 301Ch blows right).  s 3..25 = shot,
 * spinning down; t = blade frame.
 * ---------------------------------------------------------------------- */
void act_fans(void)
{
    int i;
    for (i = 0; i < n_fans; i++) {
        Obj8 *f = &fans[i];
        if (f->s != 0 && (f->s <= 3 || f->s == 5 || f->s == 9 || f->s == 0x0F || f->s == 0x19)) f->t++;
        if (f->t == 4) f->t = 0;
        if (offscreen(f->x, f->y) && offscreen(f->x, f->y - 0x80)) continue;
        if (shot_hits(f->x, f->y) || shot_hits(f->x, f->y - 0x80)) {
            if (f->s == 1 || f->s == 2) add_score(1000);
            f->s = 3;
            play_sound(0x12);
            spawn_explosion(f->x, f->y - 0xC0);
        }
        if (f->s >= 3) f->s++;
        if (f->s == 0x1A) f->s = 0;
        draw_fan(f->t, f->x, f->y);
        if (f->s == 1 &&
            (player_touches(f->x - 2, f->y - 0x40) || player_touches(f->x - 4, f->y - 0x40) || player_touches(f->x - 6, f->y - 0x40)) &&
            can_walk_left())
            player_x--;
        if (f->s == 2 &&
            (player_touches(f->x + 2, f->y - 0x40) || player_touches(f->x + 4, f->y - 0x40) || player_touches(f->x + 6, f->y - 0x40)) &&
            can_walk_right())
            player_x++;
    }
}

/* ------------------------------------------------------------------------
 * 0097:784B / 79D9 / 7B68  blue bonus crates: football (100), joystick
 * (2000), floppy disk (5000).  Obj8: s alive, t opened (3050h/3054h/3053h
 * spawn pre-opened).
 * ---------------------------------------------------------------------- */
static void bonus_crates(Obj8 *arr, int n, uint16_t item_gfx, int pop, int32_t pts)
{
    int i;
    for (i = 0; i < n; i++) {
        Obj8 *c = &arr[i];
        if (c->s == 0 || offscreen(c->x, c->y)) continue;
        if (!solid_at(c->x, c->y + 0x80)) c->y += 0x80;
        if (player_touches(c->x, c->y) && c->t) {
            c->s = 0;
            spawn_score(pop, c->x, c->y);
            add_score(pts);
            play_sound(0x10);
            continue;
        }
        if (!c->t && shot_hits(c->x, c->y)) {
            spawn_debris(c->x, c->y);
            c->t = 1;
            if (!solid_at(c->x, c->y + 0x80)) { add_score(500); spawn_score(0x280, c->x, c->y); }
        }
        draw_obj(c->t ? item_gfx : 0x3E80, c->x, c->y);
    }
}
void act_footballs(void) { bonus_crates(footballs, n_football, 0x2440, 0x000, 100); }
void act_joysticks(void) { bonus_crates(joysticks, n_joystick, 0x24E0, 0x500, 2000); }
void act_floppies(void)  { bonus_crates(floppies,  n_floppy,   0x2580, 0x640, 5000); }
