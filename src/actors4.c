/*
 * actors4.c - orb snake, soda cans, wall biters   (seg 0097: 6947..715E)
 */
#include "dn1.h"

/* ------------------------------------------------------------------------
 * 0097:6947  the flying orb chain (3013h): nine orbs follow the head, which
 * traces a 39-step loop from the EXE's path tables (DS:02FD dx, DS:034D dy).
 * Index 9 is the head.  1000 points per orb, 10000 bonus when all are gone.
 * ---------------------------------------------------------------------- */
void act_snake(void)
{
    int i, alive_none = 1;
    if (!snake_present) return;
    for (i = 1; i < 10; i++) {
        snake_x[i - 1] = snake_x[i];
        snake_y[i - 1] = snake_y[i];
        if (snake_alive[i]) alive_none = 0;
    }
    if (alive_none) {
        snake_present = 0;
        add_score(10000);
        bonus_5310 = 1;
        return;
    }
    snake_x[9] = (int16_t)(snake_x[9] + (int16_t)ds_u16((uint16_t)(snake_step * 2 + 0x2FD)));
    snake_y[9] = (int16_t)(snake_y[9] + ((int16_t)ds_u16((uint16_t)(snake_step * 2 + 0x34D)) << 6));
    for (i = 1; i < 10; i++) {
        if (!offscreen(snake_x[i], snake_y[i]) && snake_alive[i]) {
            draw_anim(snake_anim + 0x4B00, snake_x[i], snake_y[i]);
            if (player_touches(snake_x[i], snake_y[i])) player_hurt = 1;
        }
        if (anim_tick != 0) snake_anim += 0xA0;           /* (advanced once per orb) */
        if (snake_anim == 0x460) snake_anim = 0;
        if (snake_alive[i] && shot_hits(snake_x[i], snake_y[i] - 0x80)) {
            spawn_spark(3, snake_x[i], snake_y[i]);
            add_score(1000);
            play_sound(0x0A);
            spawn_debris(snake_x[i], snake_y[i] - 0x80);
            snake_alive[i] = 0;
        }
    }
    if (++snake_step == 0x27) snake_step = 0;
}

/* ------------------------------------------------------------------------
 * 0097:6B82  soda cans (3015h; 3052h = already outside its red box).
 * s: 1 boxed, 2 can (+1 health, 200 pts), 3 shot -> rockets upward
 * (catch it: 1000 pts).
 * ---------------------------------------------------------------------- */
void hint_soda(void);   /* 108E:1534 */
void act_soda(void)
{
    int i;
    if (anim_tick == 0) soda_anim += 0xA0;
    if (soda_anim == 0x280) soda_anim = 0;
    for (i = 0; i < n_soda; i++) {
        Obj6 *c = &soda[i];
        if (c->s >= 3 && offscreen(c->x, c->y)) c->s = 0;
        if (c->s == 0 || offscreen(c->x, c->y)) continue;
        if (!solid_at(c->x, c->y + 0x80) && c->s < 3) c->y += 0x80;
        if (c->s >= 3) {
            if (anim_tick != 0) c->y -= 0x40;
            if (solid_at(c->x, c->y - 0x40)) { spawn_spark(1, c->x, c->y); c->s = 0; continue; }
            if (player_touches(c->x, c->y)) {
                c->s = 0;
                add_score(1000);
                spawn_score(0x3C0, c->x, c->y);
                play_sound(0x10);
                continue;
            }
        }
        if (shot_hits(c->x, c->y)) {
            if (c->s == 1) {
                spawn_debris(c->x, c->y);
                c->s = 2;
                if (!solid_at(c->x, c->y + 0x80)) { add_score(500); spawn_score(0x280, c->x, c->y); }
            } else if (c->s == 2) {
                c->s = 3;
                play_sound(2);
            }
        }
        if (player_touches(c->x, c->y) && c->s == 2) {
            c->s = 0;
            add_score(200);
            spawn_score(0x140, c->x, c->y);
            add_health();
            play_sound(1);
            if (hint_5554) { hint_soda(); hint_5554 = 0; }
        }
        if (c->s == 1) draw_obj(0x3F20, c->x, c->y);
        if (c->s == 2) draw_anim(soda_anim + 0x5000, c->x, c->y);
        if (c->s >= 3) draw_anim(soda_anim + 0x5280, c->x, c->y);
    }
}

/* ------------------------------------------------------------------------
 * 0097:6E71  wall-crawling biters (3016h on a wall to their left, 3017h on
 * a wall to their right).  s 1/3 climbing, 2/4 descending; 100 points.
 * ---------------------------------------------------------------------- */
void act_biters(void)
{
    int i;
    biter_anim_up += 0xA0;
    biter_anim_dn -= 0xA0;
    if (biter_anim_dn == -0xA0) biter_anim_dn = 0x1E0;
    if (biter_anim_up == 0x280) biter_anim_up = 0;
    for (i = 0; i < n_biters; i++) {
        Obj6 *b = &biters[i];
        if (b->s == 0 || offscreen(b->x, b->y)) continue;
        switch (b->s) {
        case 1:
            b->y -= 8;
            draw_anim(biter_anim_up + 0x5500, b->x, b->y);
            if (!solid_at(b->x - 2, b->y) || solid_at(b->x, b->y)) b->s = 2;
            break;
        case 2:
            b->y += 8;
            draw_anim(biter_anim_dn + 0x5500, b->x, b->y);
            if (!solid_at(b->x - 2, b->y + 0x80) || solid_at(b->x, b->y + 0x80)) b->s = 1;
            break;
        case 3:
            b->y -= 8;
            draw_anim(biter_anim_up + 0x5780, b->x, b->y);
            if (!solid_at(b->x + 2, b->y) || solid_at(b->x, b->y)) b->s = 4;
            break;
        case 4:
            b->y += 8;
            draw_anim(biter_anim_dn + 0x5780, b->x, b->y);
            if (!solid_at(b->x + 2, b->y + 0x80) || solid_at(b->x, b->y + 0x80)) b->s = 3;
            break;
        }
        if (shot_hits(b->x, b->y)) {
            spawn_spark(1, b->x, b->y);
            spawn_explosion(b->x, b->y - 0x40);
            add_score(100);
            play_sound(0x12);
            b->s = 0;
        }
        if (b->s != 0 && player_touches(b->x, b->y)) player_hurt = 1;
    }
}
