/*
 * actors1.c - pick-ups that sit in grey boxes, elevators, conveyor belts,
 * rockets and flamethrowers  (seg 0097: 3714..486F)
 *
 * Common pattern of a "boxed item": the item falls until it rests on solid
 * ground; shooting the grey box (object sprite 0) reveals it (debris burst,
 * 500 points, "500" score sprite), touching the revealed item collects it.
 */
#include "dn1.h"

void inventory_add(const uint8_t *gfx);   /* 108E:1FFA */
void hint_elevator(void);                 /* 108E:0FC1 */
void inventory_draw_card(void);           /* 108E:1F9C */

/* fall one tile per frame while nothing solid is underneath */
#define SETTLE(x, y) do { if (!solid_at((x), (y) + 0x80)) (y) += 0x80; } while (0)

/* shared logic for the single-instance boxed items (boots, claws, hand, card) */
static int boxed_item(int16_t *x, int16_t *y, int16_t *opened, uint16_t item_gfx)
{
    if (offscreen(*x, *y)) return 0;
    SETTLE(*x, *y);
    if (player_touches(*x, *y) && *opened) return 1;      /* collected */
    if (!*opened && shot_hits(*x, *y)) {
        spawn_debris(*x, *y);
        *opened = 1;
        if (!solid_at(*x, *y + 0x80)) {                   /* (still in mid-air) */
            add_score(500);
            spawn_score(0x280, *x, *y);
        }
    }
    draw_obj(*opened ? item_gfx : 0, *x, *y);
    return 0;
}

/* 0097:376F  empty grey boxes (3000h) */
void act_greyboxes(void)
{
    int i;
    for (i = 0; i < n_greybox; i++) {
        Obj6 *b = &greyboxes[i];
        if (b->s == 0 || offscreen(b->x, b->y)) continue;
        SETTLE(b->x, b->y);
        if (shot_hits(b->x, b->y)) {
            b->s = 0;
            spawn_debris(b->x, b->y);
            if (!solid_at(b->x, b->y + 0x80)) {
                add_score(500);
                spawn_score(0x280, b->x, b->y);
            }
            continue;
        }
        if (!offscreen(b->x, b->y)) draw_obj(0, b->x, b->y);
    }
}

/* 0097:3CBC  boots (3006h): higher jumps + somersault */
void act_boots(void)
{
    if (have_boots) return;
    if (boxed_item(&boots_x, &boots_y, &boots_state, 0x640)) {
        have_boots = 1;
        spawn_score(0x3C0, boots_x, boots_y);
        add_score(1000);
        inventory_add(object_gfx + 0x640);
        play_sound(0x0C);
    }
}

/* 0097:42B8  grappling hooks / claws (3008h) */
void act_claws(void)
{
    if (have_claws) return;
    if (boxed_item(&claws_x, &claws_y, &claws_state, 0xB40)) {
        have_claws = 1;
        inventory_add(object_gfx + 0xB40);
        spawn_score(0x3C0, claws_x, claws_y);
        add_score(1000);
    }
}

/* 0097:573D  robohand (3020h) */
void act_robohand(void)
{
    if (have_robohand) return;
    if (boxed_item(&robohand_x, &robohand_y, &robohand_state, 0x2760)) {
        have_robohand = 1;
        spawn_score(0x3C0, robohand_x, robohand_y);
        add_score(1000);
        play_sound(0x0C);
        inventory_add(object_gfx + 0x2760);
    }
}

/* 0097:5887  access card (3033h) */
void act_card(void)
{
    if (have_card || !card_present) return;
    if (boxed_item(&card_x, &card_y, &card_state, 0x2800)) {
        have_card = 1;
        spawn_score(0x3C0, card_x, card_y);
        add_score(1000);
        inventory_draw_card();
        play_sound(0x0C);
    }
}

/* 0097:3882  elevators (3001h).  s = height in tiles (1 = retracted).
 * Standing on the platform and pressing UP extends it one tile per frame;
 * stepping off retracts it, restoring the cells it covered. */
void update_elevators(void)
{
    int i;
    for (i = 0; i < n_elevators; i++) {
        Obj6 *e = &elevators[i];
        if (offscreen(e->x, e->y)) {
            if (e->s > 1) {                               /* retract while off-screen */
                e->s--;
                MAP16((e->y << 1) + ((e->x >> 1) << 1) - 2) = (uint16_t)elevator_under[i];
                e->y += 0x80;
            }
            continue;
        }
        if (player_on(e->x, e->y - 0x20)) {
            if (hint_555b) { hint_555b = 0; hint_elevator(); }
            if (!player_hanging && in_up && head_clear() && !in_down) {
                e->s++;
                e->y -= 0x80;
                MAP16((e->y << 1) + ((e->x >> 1) << 1) - 2) = 0x1AE0;
                player_y -= 0x10;
                play_sound(0x14);
                if (player_y <= 0x40) recenter_y = 1;
                player_hanging = 0;
                draw_obj(0x320, e->x, e->y);
            }
            draw_obj(0x320, e->x, e->y);
            continue;
        }
        if (e->s > 1) {
            e->s--;
            MAP16((e->y << 1) + ((e->x >> 1) << 1) - 2) = (uint16_t)elevator_under[i];
            e->y += 0x80;
            draw_sprite(object_gfx + 0x320, SCREEN_X(e->x), ((e->y - scroll_y) >> 3));
            continue;
        }
        if (e->s == 1) draw_obj(0x320, e->x, e->y);
    }
}

/* 0097:3A98  conveyor belts (3002h..3005h): animate the belt cells and carry
 * Duke (standing on it, or hanging underneath with the hooks). */
void act_conveyors(void)
{
    int i, k;
    if (++conveyor_anim == 4) conveyor_anim = 0;
    for (i = 0; i < n_conveyors; i++) {
        Conveyor *c = &conveyors[i];
        int len = c->len;
        uint16_t base = (uint16_t)((c->x << 1) + (c->y << 1) - 4);
        for (k = 0; k < len; k++)
            MAP16(base + ((k >> 1) << 1)) = (uint16_t)(((conveyor_anim & 1) << 5) + 0x1C00);
        if (c->dir == 1) {
            MAP16(base) = (uint16_t)((conveyor_anim << 5) + 0x1B80);
            MAP16(base + ((len >> 1) << 1)) = (uint16_t)((conveyor_anim << 5) + 0x1C40);
        }
        if (c->dir == 0) {
            MAP16(base) = (uint16_t)(0x1BE0 - (conveyor_anim << 5));
            MAP16(base + ((len >> 1) << 1)) = (uint16_t)(0x1CA0 - (conveyor_anim << 5));
        }
        if (player_in_span(c->x - 2, c->x + len + 2, c->y - 0x20)) {
            if (c->dir == 0 && can_walk_left()) player_x--;
            if (c->dir == 1 && can_walk_right()) player_x++;
        }
        if (player_hanging && player_in_span(c->x - 2, c->x + len + 2, c->y + 0x60)) {
            if (c->dir == 1 && can_walk_left()) player_x--;   /* underside runs the other way */
            if (c->dir == 0 && can_walk_right()) player_x++;
        }
    }
}

/* 0097:3E06  rockets (3007h).  s: 1 idle, 2..9 ignition (after being shot),
 * 10 = flying up 1 tile/frame until it hits the ceiling and explodes. */
void act_rockets(void)
{
    int i;
    bonus_rockets = 1;
    for (i = 0; i < n_rockets; i++) {
        Obj6 *r = &rockets[i];
        if (r->s == 0) continue;
        bonus_rockets = 0;
        if (shot_hits(r->x, r->y) && r->s == 1) {
            r->s = 2;
            play_sound(0x1E);
            add_score(200);
        }
        if (r->s > 1 && r->s < 10) {
            if (r->s == 9) {               /* launch: clear the launch-pad cell */
                MAP16((r->y << 1) + ((r->x >> 1) << 1) + 0xFE) = MAP16((r->y << 1) + ((r->x >> 1) << 1) + 0x1FE);
                spawn_debris(r->x, r->y + 0x80);
            }
            if (r->s == 3) spawn_spark(3, r->x, r->y + 0x80);
            if ((r->s & 1) && !offscreen(r->x, r->y + 0x80))
                draw_sprite(object_gfx + 0xAA0, SCREEN_X(r->x), SCREEN_Y(r->y) + 0x10);
            r->s++;
        }
        if (r->s == 10) {
            r->y -= 0x80;
            if (r->y % 0x100 != 0) {
                if (!offscreen(r->x, r->y + 0x80))
                    draw_sprite(object_gfx + 0xA00, SCREEN_X(r->x), SCREEN_Y(r->y) + 0x10);
                if (!offscreen(r->x, r->y + 0x100))
                    draw_sprite(object_gfx + 0xAA0, SCREEN_X(r->x), SCREEN_Y(r->y) + 0x20);
            }
        }
        /* the rocket body: 3 tiles tall with fins */
        if (!offscreen(r->x, r->y - 0x180)) draw_sprite(object_gfx + 0x6E0, SCREEN_X(r->x), SCREEN_Y(r->y) - 0x30);
        if (!offscreen(r->x, r->y - 0xFE))  draw_sprite(object_gfx + 0x780, SCREEN_X(r->x), SCREEN_Y(r->y) - 0x20);
        if (!offscreen(r->x, r->y - 0x80))  draw_sprite(object_gfx + 0x780, SCREEN_X(r->x), SCREEN_Y(r->y) - 0x10);
        if (!offscreen(r->x, r->y))         draw_sprite(object_gfx + 0x820, SCREEN_X(r->x), SCREEN_Y(r->y));
        if (!offscreen(r->x - 2, r->y))     draw_sprite(object_gfx + 0x8C0, SCREEN_X(r->x) - 2, SCREEN_Y(r->y));
        if (!offscreen(r->x + 2, r->y))     draw_sprite(object_gfx + 0x960, SCREEN_X(r->x) + 2, SCREEN_Y(r->y));
        if (solid_at(r->x, r->y - 0x200) && r->s == 10) {
            spawn_debris(r->x - 3, r->y - 0x200);
            spawn_debris(r->x + 2, r->y - 0x180);
            spawn_debris(r->x + 1, r->y - 0x180);
            spawn_debris(r->x - 1, r->y - 0x100);
            spawn_debris(r->x,     r->y - 0x80);
            r->s = 0;
            MAP16((r->y << 1) + ((r->x >> 1) << 1) - 0x402) = MAP16((r->y << 1) + ((r->x >> 1) << 1) - 0x302);
        }
    }
}

/* 0097:43FA / 4638  flamethrowers (3009h right, 300Ah left).  Cycle of 65
 * frames: 15..24 pilot flame flicker, 25..44 three-tile jet that hurts. */
static void flamers(Obj6 *arr, int n, int dir)
{
    int i;
    for (i = 0; i < n; i++) {
        Obj6 *f = &arr[i];
        if (f->s == 0) continue;
        if (++f->s < 0x0F) continue;
        if (f->s == 0x0F && !unheard(f->x, f->y)) play_sound(0x2B);    /* (port: unheard) */
        if (f->s < 0x19) {
            if ((f->s % 2) && !offscreen(f->x, f->y))
                draw_obj(dir > 0 ? 0xF00 : 0x1220, f->x, f->y);
            continue;
        }
        if (f->s < 0x2D) {
            int fr = (f->s % 2) * 0xA0;
            if (offscreen(f->x, f->y) || offscreen(f->x + 6 * dir, f->y)) continue;
            if (dir > 0) {
                draw_obj(fr + 0xFA0, f->x, f->y);
                draw_obj(fr + 0xFA0, f->x + 2, f->y);
                draw_obj(fr + 0x10E0, f->x + 4, f->y);
                if (player_touches(f->x, f->y) || player_touches(f->x + 2, f->y - 0x20) || player_touches(f->x + 4, f->y))
                    player_hurt = 1;
            } else {
                draw_obj(fr + 0x12C0, f->x, f->y);
                draw_obj(fr + 0x12C0, f->x - 2, f->y);
                draw_obj(fr + 0x1400, f->x - 4, f->y);
                if (player_touches(f->x, f->y) || player_touches(f->x - 2, f->y) || player_touches(f->x - 4, f->y))
                    player_hurt = 1;
            }
            continue;
        }
        if (f->s > 0x41) f->s = 1;
    }
}
void act_flamers_r(void) { flamers(flamers_r, n_flamer_r, +1); }
void act_flamers_l(void) { flamers(flamers_l, n_flamer_l, -1); }
