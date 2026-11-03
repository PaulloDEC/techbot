/*
 * actors2.c - robots: floating "techbots", hopping robots, wheeled robots
 * (seg 0097: 4874..5738)
 */
#include "dn1.h"

/* ------------------------------------------------------------------------
 * 0097:4874  floating robots (300Bh).  Obj8: s = facing frame 1..6, or
 * FEh/FFh while being blown apart along one of two trajectories (tables at
 * DS:0546 / DS:0576); t = trajectory step.
 * ---------------------------------------------------------------------- */
void act_floatbots(void)
{
    int i;
    if (++floatbot_fire == 0x0A) floatbot_fire = 0;
    floatbot_anim += 0xA0;
    if (floatbot_anim == 0x280) floatbot_anim = 0;

    for (i = 0; i < n_floatbots; i++) {
        Obj8 *b = &floatbots[i];
        if (b->s == 0xFE || b->s == 0xFF) {
            int fe = (b->s == 0xFE);
            if (solid_at(b->x, b->y + 0x80)) {
                spawn_debris(b->x, b->y);
                spawn_spark(1, b->x, b->y);
                b->s = 0;
                continue;
            }
            if (fe ? (b->t > 0x14) : (b->t >= 0x14)) {
                b->y += 0x80;
            } else {
                uint16_t tb = fe ? 0x546 : 0x576;
                b->x = (int16_t)(b->x + (int16_t)ds_u16((uint16_t)(tb + b->t * 2)));
                b->y = (int16_t)(b->y + (int16_t)ds_u16((uint16_t)(tb + 2 + b->t * 2)));
                b->t += 2;
            }
            if (offscreen(b->x, b->y)) { b->s = 0; continue; }
            draw_anim(0x3C0, b->x, b->y);
            draw_sprite(anim_gfx + (fe ? 0 : 0x320), SCREEN_X(b->x), SCREEN_Y(b->y) - 16);
            spawn_explosion(b->x, b->y - 0x80);
            continue;
        }
        if (b->s == 0) continue;
        if (offscreen(b->x, b->y) || offscreen(b->x, b->y - 0x80)) continue;

        if (shot_hits(b->x, b->y - 0x80) || player_touches(b->x, b->y - 0x80)) {
            if (player_touches(b->x, b->y)) player_hurt = 1;
            b->s = (anim_tick == 0 || anim_tick == 0x20) ? 0xFE : 0xFF;
            spawn_spark(1, b->x, b->y);
            b->t = 0;
            add_score(200);
            play_sound(0x12);
            continue;
        }
        /* hover: sink 1 px when unsupported, bounce back up off the floor */
        if (!solid_at(b->x, b->y + 0x80)) b->y += 8;
        if (solid_at(b->x, b->y + 0x80)) {
            b->y -= 8;
            if (!solid_at(b->x, b->y + 0x80) && floatbot_anim == 0) spawn_dust(b->x, b->y);
        }
        if (floatbot_anim == 0) {
            if (b->x > player_x + scroll_x) {                 /* turn / move left */
                if (b->s > 1) b->s--;
                else if (!shot_wall(b->x - 1, b->y - 5)) {
                    b->x--;
                    if (floatbot_fire == 0) spawn_eshot(1, b->x - 1, b->y - 0xA0);
                }
            }
            if (b->x < player_x + scroll_x) {                 /* turn / move right */
                if (b->s < 6) b->s++;
                else if (!shot_wall(b->x + 2, b->y - 5)) {
                    b->x++;
                    if (floatbot_fire == 0) spawn_eshot(2, b->x + 1, b->y - 0xA0);
                }
            }
        }
        draw_anim(floatbot_anim + 0x3C0, b->x, b->y);
        draw_sprite(anim_gfx + (uint16_t)((b->s - 1) * 0xA0), SCREEN_X(b->x), SCREEN_Y(b->y) - 16);
    }
}

/* 0097:4D42  draw a 2x3-tile hopping robot pose (tile table at DS:0650) */
static void draw_hopbot(int pose, int x, int y)
{
    uint16_t t = (uint16_t)(pose * 0x0C + 0x650);
    int sx = SCREEN_X(x), sy = SCREEN_Y(y);
    if ((int16_t)ds_u16(t) > 0 && !offscreen(x, y - 0x100))
        draw_sprite(anim_gfx + (uint16_t)(ds_u16(t) + 0x5A0), sx, sy - 0x20);
    if ((int16_t)ds_u16(t) > 0 && !offscreen(x + 2, y - 0x100))
        draw_sprite(anim_gfx + (uint16_t)(ds_u16(t + 2) + 0x5A0), sx + 2, sy - 0x20);
    if (!offscreen(x, y - 0x80))     draw_sprite(anim_gfx + (uint16_t)(ds_u16(t + 4) + 0x5A0), sx, sy - 0x10);
    if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + (uint16_t)(ds_u16(t + 6) + 0x5A0), sx + 2, sy - 0x10);
    if (!offscreen(x, y))            draw_sprite(anim_gfx + (uint16_t)(ds_u16(t + 8) + 0x5A0), sx, sy);
    if (!offscreen(x + 2, y))        draw_sprite(anim_gfx + (uint16_t)(ds_u16(t + 10) + 0x5A0), sx + 2, sy);
    if (player_touches(x + 1, y) || player_touches(x + 1, y - 0x80)) player_hurt = 1;
}

/* ------------------------------------------------------------------------
 * 0097:4F91  hopping robots (300Ch).  Obj8: s = phase counter (1..18h crouch
 * and shoot, 19h..31h leap using the arc table at DS:0698), t = 0 asleep
 * until first seen, then 1 = facing left / 2 = facing right.
 * hopbot_aux[i] = hits taken (3 destroys it, 2000 points).
 * ---------------------------------------------------------------------- */
void act_hopbots(void)
{
    int i;
    for (i = 0; i < n_hopbots; i++) {
        Obj8 *b = &hopbots[i];
        if (!offscreen(b->x, b->y) && b->t == 0) b->t = 1;
        if (b->s == 0 || b->t == 0) continue;
    again:
        if (shot_hits(b->x + 1, b->y - 0x80) || shot_hits(b->x + 1, b->y))
            hopbot_aux[i]++;
        if (hopbot_aux[i] == 3) {
            spawn_debris(b->x + 1, b->y - 0x100);
            spawn_debris(b->x, b->y - 0x80);
            spawn_debris(b->x + 2, b->y);
            spawn_spark(1, b->x + 1, b->y - 0x80);
            spawn_spark(1, b->x + 2, b->y - 0x40);
            spawn_spark(1, b->x + 2, b->y - 0x80);
            spawn_spark(1, b->x + 1, b->y);
            if (anim_tick == 0) spawn_faller(b->x, b->y);
            add_score(2000);
            b->s = 0;
            continue;
        }
        if (hopbot_aux[i] == 1 && anim_tick == 0) spawn_explosion(b->x, b->y - 0xC0);
        if (hopbot_aux[i] == 2 && anim_tick == 0) {
            spawn_explosion(b->x + 2, b->y - 0xC0);
            spawn_explosion(b->x, b->y - 0xC0);
        }

        if (b->s < 0x19) {                                   /* on the ground */
            int left = b->x > player_x + scroll_x - 3;
            if (!solid_at(b->x + 1, b->y + 0x80)) {
                draw_hopbot(left ? 2 : 5, b->x, b->y);
                b->y += 0x80;
                continue;
            }
            if (left) { b->t = 1; if (b->s != 0x0C) draw_hopbot(0, b->x, b->y); }
            else      { if (b->s != 0x0C) draw_hopbot(3, b->x, b->y); b->t = 2; }
            b->s++;
            b->y = (int16_t)((b->y >> 7) << 7);
            if (b->s == 0x0D) {
                if (b->t == 1) { spawn_eshot(1, b->x - 1, b->y - 0x50); draw_hopbot(1, b->x, b->y); }
                if (b->t == 2) { spawn_eshot(2, b->x + 3, b->y - 0x50); draw_hopbot(4, b->x, b->y); }
                b->s++;
            }
            continue;
        }
        if (b->s < 0x32 && (b->t == 1 || b->t == 2)) {        /* leaping */
            int dir = (b->t == 1) ? -1 : 1;
            if (solid_at(b->x + 1, b->y - 0x100)) {           /* bumped the ceiling */
                b->y = (int16_t)((b->y >> 7) << 7);
                draw_hopbot(dir < 0 ? 2 : 5, b->x, b->y);
                b->s = 1;
                continue;
            }
            if (!shot_wall(dir < 0 ? b->x - 1 : b->x + 4, b->y))
                b->x = (int16_t)(b->x + dir * (int16_t)ds_u16((uint16_t)((b->s - 0x19) * 2 + 0x698)));
            b->y = (int16_t)(b->y - (int16_t)ds_u16((uint16_t)((b->s - 0x18) * 2 + 0x698)));
            draw_hopbot(dir < 0 ? 2 : 5, b->x, b->y);
            b->s += 2;
            continue;
        }
        b->s = 1;
        goto again;
    }
}

/* 0097:5427  draw a wheeled robot (2 tiles) and hurt Duke on contact */
static void draw_wheelbot(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x140 + 0x1540);
    if (!offscreen(x, y))     draw_sprite(anim_gfx + g, SCREEN_X(x), SCREEN_Y(y));
    if (!offscreen(x + 2, y)) draw_sprite(anim_gfx + g + 0xA0, SCREEN_X(x) + 2, SCREEN_Y(y));
    if (player_touches(x, y) || player_touches(x + 2, y)) player_hurt = 1;
}

/* ------------------------------------------------------------------------
 * 0097:54E1  wheeled robots (300Dh).  s: 1 = rolling left, 2 = right.
 * Reverses (and fires) at walls and ledges; 2 hits, 2500 points.
 * ---------------------------------------------------------------------- */
void act_wheelbots(void)
{
    int i;
    if (++wheel_sub == 2) { wheel_sub = 0; wheel_frame++; }
    if (wheel_frame == 2) wheel_frame = 0;

    for (i = 0; i < n_wheelbots; i++) {
        Obj6 *w = &wheelbots[i];
        if (w->s == 0) continue;
        if (shot_hits(w->x, w->y) || shot_hits(w->x + 2, w->y)) wheelbot_aux[i]++;
        if (wheelbot_aux[i] == 2) {
            spawn_debris(w->x + 2, w->y);
            spawn_debris(w->x, w->y);
            spawn_spark(1, w->x + 1, w->y);
            add_score(2500);
            play_sound(0x12);
            w->s = 0;
            continue;
        }
        if (wheelbot_aux[i] == 1 && anim_tick == 0) spawn_explosion(w->x + 2, w->y - 0x40);
        if (!solid_at(w->x, w->y + 0x80)) {                 /* falling */
            w->y += 0x20;
            draw_wheelbot(0, w->x, w->y);
            continue;
        }
        if (w->s == 1) {
            if (shot_wall(w->x, w->y) || !shot_wall(w->x, w->y + 0x80)) {
                w->s = 2;
                spawn_eshot(2, w->x + 4, w->y - 0x40);
            } else {
                if (wheel_sub == 0) w->x--;
                draw_wheelbot(wheel_frame, w->x, w->y);
            }
        }
        if (w->s == 2) {
            if (shot_wall(w->x + 4, w->y) || !shot_wall(w->x + 4, w->y + 0x80)) {
                w->s = 1;
                spawn_eshot(1, w->x - 2, w->y - 0x40);
                draw_wheelbot(wheel_frame + 2, w->x - 1, w->y);
            } else {
                if (wheel_sub == 0) w->x++;
                draw_wheelbot(wheel_frame + 2, w->x, w->y);
            }
        }
    }
}
