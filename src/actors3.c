/*
 * actors3.c - fire wheels, firepower boxes, mini robots, exit door,
 * dynamite boxes   (seg 0097: 59CF..6942)
 */
#include "dn1.h"

/* draw a 2x2-tile anim sprite whose top half sits one row above y */
static void draw_2x2(uint16_t g, int x, int y)
{
    int sx = SCREEN_X(x), sy = SCREEN_Y(y);
    if (!offscreen(x, y - 0x80))     draw_sprite(anim_gfx + g, sx, sy - 16);
    if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + (uint16_t)(g + 0xA0), sx + 2, sy - 16);
    if (!offscreen(x, y))            draw_sprite(anim_gfx + (uint16_t)(g + 0x140), sx, sy);
    if (!offscreen(x + 2, y))        draw_sprite(anim_gfx + (uint16_t)(g + 0x1E0), sx + 2, sy);
}

/* 0097:59CF */
static void draw_firewheel(int frame, int x, int y)
{
    draw_2x2((uint16_t)(frame * 0x280 + 0x1E00), x, y);
    if (player_touches(x + 1, y) || player_touches(x + 1, y - 0x40)) player_hurt = 1;
}

/* ------------------------------------------------------------------------
 * 0097:5B0B  fire wheel robots (300Eh).  A 60-frame cycle: for 25 frames the
 * wheel is vulnerable (frames 0-3), then it bursts into flame (frames 4-7)
 * and ignores hits.  s: 1 rolling left, 2 rolling right.  2 hits, 5000 pts.
 * ---------------------------------------------------------------------- */
void act_firewheels(void)
{
    int i;
    firewheel_frame++;
    if (++firewheel_cycle == 0x3C) firewheel_cycle = 0;
    if (firewheel_cycle < 0x19 && firewheel_frame >= 4) firewheel_frame = 0;
    if (firewheel_cycle >= 0x19 && firewheel_frame >= 8) firewheel_frame = 4;

    for (i = 0; i < n_firewheels; i++) {
        Obj6 *w = &firewheels[i];
        if (w->s == 0) continue;
        if ((shot_hits(w->x, w->y) || shot_hits(w->x + 2, w->y)) && firewheel_cycle < 0x19)
            firewheel_aux[i]++;
        if (firewheel_aux[i] == 2) {
            spawn_debris(w->x + 2, w->y);
            spawn_debris(w->x, w->y);
            spawn_explosion(w->x + 2, w->y - 0x80);
            spawn_spark(1, w->x + 1, w->y);
            spawn_spark(1, w->x + 1, w->y - 0x80);
            spawn_spark(1, w->x, w->y - 0x80);
            add_score(5000);
            w->s = 0;
            continue;
        }
        if (firewheel_aux[i] == 1 && anim_tick == 0) spawn_explosion(w->x + 2, w->y - 0x40);
        if (!solid_at(w->x + 1, w->y + 0x80)) {
            w->y += 0x80;
            draw_firewheel(firewheel_frame, w->x, w->y);
            continue;
        }
        if (w->s == 1) {
            if (!shot_wall(w->x, w->y + 0x80) || shot_wall(w->x, w->y)) {
                w->s = 2;                 /* turn and fall through to the s==2 case */
            } else {
                if (anim_tick == 0x40 || anim_tick == 0) w->x--;
                draw_firewheel(firewheel_frame, w->x, w->y);
                continue;
            }
        }
        if (w->s != 2) continue;
        if (!shot_wall(w->x + 3, w->y + 0x80) || shot_wall(w->x + 3, w->y)) {
            w->s = 1;
        } else if (anim_tick == 0x40 || anim_tick == 0) {
            w->x++;
        }
        draw_firewheel(firewheel_frame, w->x, w->y);
    }
}

/* ------------------------------------------------------------------------
 * 0097:5D6F  firepower boxes (300Fh): each adds a shot slot (max 4).
 * Obj8: s alive, t opened.
 * ---------------------------------------------------------------------- */
void act_guns(void)
{
    int i;
    for (i = 0; i < n_guns; i++) {
        Obj8 *g = &guns[i];
        if (g->s == 0 || offscreen(g->x, g->y)) continue;
        if (!solid_at(g->x, g->y + 0x80)) g->y += 0x80;
        if (player_touches(g->x, g->y) && g->t) {
            g->s = 0;
            if (max_shots < 4) { max_shots++; draw_firepower(); }
            play_sound(0x0C);
            add_score(1000);
            spawn_score(0x3C0, g->x, g->y);
            continue;
        }
        if (!g->t && shot_hits(g->x, g->y)) {
            spawn_debris(g->x, g->y);
            g->t = 1;
            if (!solid_at(g->x, g->y + 0x80)) { add_score(500); spawn_score(0x280, g->x, g->y); }
        }
        draw_obj(g->t ? 0x1AE0 : 0, g->x, g->y);
    }
}

/* ------------------------------------------------------------------------
 * 0097:5F0C  mini robots (3010h).  s: 1 left, 2 right, 3..9 dying.
 * A hit knocks them one column back; 100 points.
 * ---------------------------------------------------------------------- */
void act_minibots(void)
{
    int i;
    minibot_anim += 0xA0;
    if (minibot_anim == 0x1E0) minibot_anim = 0;
    for (i = 0; i < n_minibots; i++) {
        Obj8 *m = &minibots[i];
        if (m->s == 0 || offscreen(m->x, m->y)) continue;
        if (m->s < 3) {
            int h = shot_hits(m->x, m->y);
            if (h) {
                if (h == 1) m->x--;
                if (h == 2) m->x++;
                play_sound(0x12);
                m->s = 3;
            }
        }
        if (!solid_at(m->x, m->y + 0x80)) m->y += 0x80;
        if (m->s == 1 || m->s == 2) {
            if (m->s == 1) {
                if (shot_wall(m->x - 1, m->y) || !shot_wall(m->x - 1, m->y + 0x80)) m->s = 2;
                else if (minibot_anim == 0) m->x--;
            } else {
                if (shot_wall(m->x + 2, m->y) || !shot_wall(m->x + 2, m->y + 0x80)) m->s = 1;
                else if (minibot_anim == 0) m->x++;
            }
            if (!offscreen(m->x, m->y))
                draw_anim(minibot_anim + 0x3200, m->x, m->y);
        } else if (m->s >= 3) {
            if (!offscreen(m->x, m->y)) draw_anim((m->s - 3) * 0xA0 + 0x33E0, m->x, m->y);
            if (++m->s == 0x0A) {
                m->s = 0;
                spawn_spark(1, m->x, m->y);
                add_score(100);
            }
        }
        if (player_touches(m->x, m->y) && m->s > 0 && m->s < 3) player_hurt = 1;
    }
}

/* ------------------------------------------------------------------------
 * 0097:6292  the exit door (3011h).  UP in front of it starts an 8-step
 * sequence (open, Duke vanishes, close) and then sets level_done.
 * ---------------------------------------------------------------------- */
void hint_door(void);   /* 108E:16F3 */
void update_exit_door(void)
{
    if (++door_sub == 1) door_sub = 0;           /* (always 0: the door advances every frame) */
    if (!exit_present) return;
    if ((player_touches(exit_x + 1, exit_y) || player_touches(exit_x + 1, exit_y - 0x80)) && hint_555a) {
        hint_door();
        hint_555a = 0;
    }
    if (exit_stage == 0 && in_up && !in_left && !in_right && !in_jump &&
        (player_touches(exit_x + 1, exit_y) || player_touches(exit_x + 1, exit_y - 0x80))) {
        exit_stage = 1;
        if (level_index % 2) play_sound(0x27);
        in_up = 0;
    }
    if (exit_stage == 0) { draw_2x2(0x3C00, exit_x, exit_y); return; }
    if (door_sub == 0) exit_stage++;
    if (exit_stage < 5) {
        if (exit_stage == 3 && level_index % 2 == 0) play_sound(0x13);
        draw_2x2((uint16_t)((exit_stage - 1) * 0x280 + 0x3C00), exit_x, exit_y);
        return;
    }
    switch (exit_stage - 4) {
    case 1: draw_2x2(2 * 0x280 + 0x3C00, exit_x, exit_y); break;
    case 2: draw_2x2(1 * 0x280 + 0x3C00, exit_x, exit_y); break;
    case 3: draw_2x2(0x3C00, exit_x, exit_y); break;
    case 4: draw_2x2(0x3C00, exit_x, exit_y); level_done = 1; break;
    default: break;
    }
}

/* ------------------------------------------------------------------------
 * 0097:6445  dynamite boxes (3012h).  Obj12: s fuse counter, t opened,
 * u/v = left/right flame still spreading.  The blast creeps outward along
 * the floor two columns every other frame (sparks of type 2 hurt Duke).
 * ---------------------------------------------------------------------- */
static int floor_ok(int x, int y) { return !shot_wall(x, y) && shot_wall(x, y + 0x80); }

void act_dynamite(void)
{
    int i;
    for (i = 0; i < n_dynamite; i++) {
        Obj12 *d = &dynamite[i];
        if (d->s == 0) continue;
        if (!solid_at(d->x, d->y + 0x80)) d->y += 0x80;
        if (d->t == 0 && shot_hits(d->x, d->y)) {
            spawn_debris(d->x, d->y);
            d->t = 1;
            if (!solid_at(d->x, d->y + 0x80)) { add_score(500); spawn_score(0x280, d->x, d->y); }
        }
        if (d->t == 0 && !offscreen(d->x, d->y) && d->s < 0x0C) { draw_obj(0, d->x, d->y); continue; }
        if (d->t == 0) continue;

        d->s++;
        if (!offscreen(d->x, d->y)) {
            if (d->s < 0x0C) draw_anim(0x4600, d->x, d->y);
            if ((d->s % 2) && d->s < 0x0C) draw_anim(0x46A0, d->x, d->y);
        }
        if (d->s <= 0x0C) continue;

        switch (d->s) {
        case 0x0E:
            spawn_spark(2, d->x, d->y);
            play_sound(0x21);
            break;
        case 0x11:
            if (floor_ok(d->x - 2, d->y)) spawn_spark(2, d->x - 2, d->y); else d->u = 0;
            if (floor_ok(d->x + 2, d->y)) spawn_spark(2, d->x + 2, d->y); else d->v = 0;
            break;
        case 0x13:
        case 0x15: {
            int k = (d->s == 0x13) ? 4 : 6;
            if (d->u) { if (floor_ok(d->x - k, d->y)) spawn_spark(2, d->x - k, d->y); else d->u = 0; }
            if (d->v) { if (floor_ok(d->x + k, d->y)) spawn_spark(2, d->x + k, d->y); else d->v = 0; }
            break;
        }
        case 0x17:
            if (d->u && floor_ok(d->x - 8, d->y)) spawn_spark(2, d->x - 8, d->y);
            if (d->v && floor_ok(d->x + 8, d->y)) spawn_spark(2, d->x + 8, d->y);
            break;
        default: break;
        }
        if (d->s == 0x18) d->s = 0;
    }
}
