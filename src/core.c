/*
 * core.c - shared helpers of the game module (seg 0097): collision queries,
 * the small effect pools (score pop-ups, debris, dust, explosions, sparks,
 * falling rubble, player/enemy shots) and the actor-routine registry.
 */
#include "dn1.h"
#include <stdlib.h>

/* draw helpers: sprite at a world position (x in columns, y in 1/8 px) */
void draw_obj(int gfx_off, int x, int y)  { draw_sprite(object_gfx + (uint16_t)gfx_off, SCREEN_X(x), SCREEN_Y(y)); }
void draw_anim(int gfx_off, int x, int y) { draw_sprite(anim_gfx + (uint16_t)gfx_off, SCREEN_X(x), SCREEN_Y(y)); }

/* 0097:326E  1 = outside the 13x10-tile play window (plus a half-tile margin) */
int offscreen(int x, int y)
{
    /* (port) port_act_left/right are 0 except in Enhanced widescreen */
    if (x >= scroll_x - port_act_left && scroll_x + 0x1D + port_act_right > x &&
        scroll_y - 0x40 <= y && scroll_y + 0x500 > y)
        return 0;
    return 1;
}

/* 0097:3714  solid map cell under (x,y); an odd x straddles two cells */
int solid_at(int x, int y)
{
    uint16_t o = (uint16_t)((((y >> 7) << 7) << 1) + ((x >> 1) << 1) - 2);
    uint16_t o2 = (x & 1) ? (uint16_t)(o + 2) : o;
    return MAP16(o) > SOLID_TILE || MAP16(o2) > SOLID_TILE;
}

/* 0097:3513  actor at (x,y) overlaps Duke */
int player_touches(int x, int y)
{
    int dy = ((player_y >> 4) << 7) + scroll_y - y + 2;
    if (abs(player_x + scroll_x - x) < 2 && dy > 0x10 && dy < 0x180) return 1;
    return 0;
}

/* 0097:363B  Duke stands exactly on an object (elevator platform) */
int player_on(int x, int y)
{
    int dy = ((player_y >> 4) << 7) + scroll_y - y + 2;
    if (abs(player_x + scroll_x - x) == 0 && dy > 0 && dy < 0x180) return 1;
    return 0;
}

/* 0097:35EE  Duke stands on a conveyor belt.  x0/x1 are in *tile* units
 * (conveyor records store a tile column), hence the  duke-x0 > x0  test,
 * i.e. duke > 2*x0 in 8-pixel columns. */
int player_in_span(int x0, int x1, int y)
{
    int dy = ((player_y >> 4) << 7) + scroll_y - y + 2;
    int rel = player_x + scroll_x - x0;
    if (rel > x0 && rel < x1 && dy > 0 && dy < 0x180) return 1;
    return 0;
}

/* 0097:3558  does one of Duke's shots hit (x,y)?  Consumes the shot.
 * returns 1 for a normal shot (state 1), 2 for state 5, else 0 */
int shot_hits(int x, int y)
{
    int i;
    for (i = 0; i < max_shots; i++) {
        Obj8 *s = &shots[i];
        if (s->s == 0) continue;
        if (abs(s->x - x) >= 2) continue;
        if (s->y - y >= 0x80 || s->y - y < -0x80) continue;
        if (s->s == 1) { s->s = 0; return 1; }
        if (s->s == 5) { s->s = 0; return 2; }
        s->s = 0;
    }
    return 0;
}

/* 0097:367E  shot at (x,y) hits a wall?  Player shots destroy brick 1800h */
extern uint8_t player_shot_pass;   /* 530F */
static int shot_blocked(int x, int y)
{
    uint16_t o = (uint16_t)((((y >> 7) << 7) << 1) + ((x >> 1) << 1) - 2);
    if (player_shot_pass) {
        if (MAP16(o) == 0x1800) {
            MAP16(o) = 0x17E0; spawn_explosion(x, y);
            goto destroyed;
        }
        if (MAP16(o + 0x100) == 0x1800) {
            MAP16(o + 0x100) = 0x17E0; spawn_explosion(x, y + 0x80);
            goto destroyed;
        }
    }
    return (int16_t)MAP16(o) > SOLID_TILE;
destroyed:
    play_sound(0x2A);
    add_score(10);
    return 1;
}
int shot_wall(int x, int y) { return shot_blocked(x, y); }

/* 0097:314C  spawn a four-piece debris burst; the piece graphics rotate */
void spawn_debris(int x, int y)
{
    static const int16_t sets[4][4] = {
        {0x0A0, 0x140, 0x1E0, 0x280}, {0x280, 0x0A0, 0x140, 0x1E0},
        {0x140, 0x280, 0x1E0, 0x0A0}, {0x1E0, 0x280, 0x140, 0x0A0},
    };
    int i;
    if (++debris_phase > 3) debris_phase = 0;
    for (i = 0; i < max_debris; i++) {
        Debris *d = &debris[i];
        if (d->t) continue;
        d->t = 1;
        d->x0 = (int16_t)x;     d->y0 = (int16_t)y;
        d->x1 = (int16_t)(x + 1); d->y1 = (int16_t)y;
        d->x2 = (int16_t)x;     d->y2 = (int16_t)(y + 8);
        d->x3 = (int16_t)(x + 1); d->y3 = (int16_t)(y + 8);
        d->tile0 = sets[debris_phase][0]; d->tile1 = sets[debris_phase][1];
        d->tile2 = sets[debris_phase][2]; d->tile3 = sets[debris_phase][3];
        play_sound(0x0A);
        return;
    }
}

/* 0097:32A5  move + draw debris; deltas come from tables in the EXE */
void update_debris(void)
{
    int i;
    for (i = 0; i < max_debris; i++) {
        Debris *d = &debris[i];
        int16_t *p = &d->x0;
        int k, t;
        if (d->t == 0) continue;
        t = d->t;
        for (k = 0; k < 8; k++)   /* x0,y0 @440/442  x1,y1 @476/478  x2,y2 @4AC/4AE  x3,y3 @4E2/4E4 */
            p[k] = (int16_t)(p[k] + (int16_t)ds_u16((uint16_t)(0x440 + (k >> 1) * 0x36 + (k & 1) * 2 + t * 2)));
        for (k = 0; k < 4; k++) {
            int px = p[k * 2], py = p[k * 2 + 1];
            if (!offscreen(px, py) && !offscreen(px, py + 0x80))
                draw_obj((&d->tile0)[k], px, py);
        }
        d->t += 2;
        if (d->t == 0x17) d->t = 0;
    }
}

/* 0097:2A40  floating score/bonus sprite (numbers.dn1) */
void spawn_score(int tile, int x, int y)
{
    int i;
    for (i = 0; i < max_scores; i++) {
        Obj8 *s = &scores[i];
        if (s->s) continue;
        s->s = 1; s->x = (int16_t)x; s->y = (int16_t)y; s->t = (int16_t)tile;
        return;
    }
}

/* 0097:2A1D */
void clear_scores(void)
{
    int i;
    for (i = 0; i < max_scores; i++) scores[i].s = 0;
}

/* 0097:2A93  scores rise 8 units per frame; big bonus sprites (>=8C0h) linger */
void update_scores(void)
{
    int i;
    score_anim += 0xA0;
    if (score_anim > 0xA0) score_anim = 0;
    for (i = 0; i < max_scores; i++) {
        Obj8 *s = &scores[i];
        if (offscreen(s->x, s->y) && s->t < 0x8C0) { s->s = 0; continue; }
        if (s->s == 0) continue;
        s->y -= 8;
        if (!offscreen(s->x, s->y))
            draw_sprite(numbers_gfx + (uint16_t)(s->t + score_anim), SCREEN_X(s->x), SCREEN_Y(s->y));
        s->s++;
        if (s->t < 0x8C0 && s->s == 0x28) s->s = 0;
        if (s->t >= 0x8C0 && s->s == 0x2BC) s->s = 0;
    }
}

/* 0097:2B86  spark / small blast (type 1: shot impact, 3: bomb burst, 2: hurting flash) */
void spawn_spark(int type, int x, int y)
{
    int i;
    for (i = 0; i < max_sparks; i++) {
        Obj8 *s = &sparks[i];
        if (s->s) continue;
        s->s = 1; s->x = (int16_t)x; s->y = (int16_t)y; s->t = (int16_t)type;
        return;
    }
}

/* 0097:2BD9 */
void update_sparks(void)
{
    int i;
    for (i = 0; i < max_sparks; i++) {
        Obj8 *s = &sparks[i];
        if (s->s == 0) continue;
        if (offscreen(s->x, s->y)) { s->s = 0; continue; }
        if (s->t == 3) {                               /* bomb: 2-frame burst */
            s->s++;
            if (s->s == 2) draw_anim(0x8160, s->x, s->y);
            if (s->s != 4) continue;
            if (!offscreen(s->x - 1, s->y - 0x40))
                draw_sprite(anim_gfx + 0x8200, SCREEN_X(s->x) - 1, SCREEN_Y(s->y) - 8);
            if (!offscreen(s->x + 1, s->y - 0x40))
                draw_sprite(anim_gfx + 0x82A0, SCREEN_X(s->x) + 1, SCREEN_Y(s->y) - 8);
            if (!offscreen(s->x - 1, s->y + 0x40))
                draw_sprite(anim_gfx + 0x8340, SCREEN_X(s->x) - 1, SCREEN_Y(s->y) + 8);
            if (!offscreen(s->x + 1, s->y + 0x40))
                draw_sprite(anim_gfx + 0x83E0, SCREEN_X(s->x) + 1, SCREEN_Y(s->y) + 8);
            s->s = 0;
            continue;
        }
        if (s->t == 1) {
            draw_anim(s->s * 0xA0 + 0x19A0, s->x, s->y);
        } else {
            if (player_touches(s->x, s->y)) player_hurt = 1;
            draw_anim(s->s * 0xA0 + 0x37A0, s->x, s->y);
        }
        s->s++;
        if (s->s == 7) s->s = 0;
    }
}

/* 0097:2E64 / 2EB1  explosion puff (object gfx 14A0h..) */
void spawn_explosion(int x, int y)
{
    int i;
    for (i = 0; i < max_explosions; i++) {
        Obj6 *e = &explosions[i];
        if (e->s) continue;
        e->s = 1; e->x = (int16_t)x; e->y = (int16_t)y;
        return;
    }
}
void update_explosions(void)
{
    int i;
    for (i = 0; i < max_explosions; i++) {
        Obj6 *e = &explosions[i];
        if (e->s == 0) continue;
        if (offscreen(e->x, e->y)) { e->s = 0; continue; }
        draw_obj(e->s * 0xA0 + 0x14A0, e->x, e->y);
        if (++e->s == 6) e->s = 0;
    }
}

/* 0097:29D0 / 2F4C  landing dust (object gfx B40h..) */
void spawn_dust(int x, int y)
{
    int i;
    for (i = 0; i < max_dust; i++) {
        Obj6 *e = &dust[i];
        if (e->s) continue;
        e->s = 1; e->x = (int16_t)x; e->y = (int16_t)y;
        return;
    }
}
void update_dust(void)
{
    int i;
    for (i = 0; i < max_dust; i++) {
        Obj6 *e = &dust[i];
        if (e->s == 0) continue;
        if (offscreen(e->x, e->y)) { e->s = 0; continue; }
        draw_obj(e->s * 0xA0 + 0xB40, e->x, e->y);
        if (++e->s == 6) e->s = 0;
    }
}

/* 0097:2FE7  falling rubble (two anim tiles wide), gravity table at DS:0412 */
void spawn_faller(int x, int y)
{
    int i;
    for (i = 0; i < max_fallers; i++) {
        Obj6 *e = &fallers[i];
        if (e->s) continue;
        e->s = 1; e->x = (int16_t)x; e->y = (int16_t)y;
        return;
    }
}
/* 0097:3034 */
static void draw_faller(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x140 + 0x5B40);
    if (!offscreen(x, y)) draw_sprite(anim_gfx + g, SCREEN_X(x), SCREEN_Y(y));
    if (!offscreen(x + 2, y)) draw_sprite(anim_gfx + g + 0xA0, SCREEN_X(x) + 2, SCREEN_Y(y));
}
/* 0097:30C9 */
void update_fallers(void)
{
    int i;
    for (i = 0; i < max_fallers; i++) {
        Obj6 *e = &fallers[i];
        if (e->s == 0) continue;
        e->s++;
        e->y = (int16_t)(e->y + (int16_t)ds_u16((uint16_t)(0x412 + (e->s - 2) * 2)));
        if (e->s < 0x10) draw_faller((e->s - 2) / 3, e->x, e->y);
        if (e->s == 0x12) e->s = 0;
    }
}

/* 0097:0B85  add points and redraw the 8-digit score on both pages */
void draw_number(int32_t n, int x, int row);   /* 108E:2789 */
void add_score(int32_t pts)
{
    WRITE_MODE_NORMAL();
    score += pts;
    if (score == 0) {
        set_draw_page(0); draw_number(10000000L, 0x25, 3); draw_number(0, 0x1E, 3);
        set_draw_page(1); draw_number(10000000L, 0x25, 3); draw_number(0, 0x1E, 3);
    }
    set_draw_page(0); draw_number(score, 0x25, 3);
    set_draw_page(1); draw_number(score, 0x25, 3);
}

/* 0097:C243  add an actor routine to the per-frame list (once) */
ActorFn actor_fns[100];
void register_actor(ActorFn fn)
{
    int i;
    for (i = 0; i < n_actor_fns; i++)
        if (actor_fns[i] == fn) return;
    actor_fns[n_actor_fns++] = fn;
}

/* 0097:1B8A  enemy projectile */
void spawn_eshot(int dir, int x, int y)
{
    int i;
    for (i = 0; i < max_eshots; i++) {
        EShot *e = &eshots[i];
        if (e->dir) continue;
        e->dir = (int16_t)dir; e->x = (int16_t)x; e->y = (int16_t)y; e->active = 1;
        if (!offscreen(e->x, e->y)) play_sound(0x0B);
        return;
    }
}

/* 0097:1B51 */
void clear_eshots(void)
{
    int i;
    for (i = 0; i < max_eshots; i++) { eshots[i].dir = 0; eshots[i].active = 0; }
}

/* 0097:18D4  Duke's shot: dir = facing (0 left / 4 right) -> state 1 or 5 */
void spawn_shot(int x, int y, int dir)
{
    int i;
    for (i = 0; i < max_shots; i++) {
        Obj8 *s = &shots[i];
        if (s->s) continue;
        play_sound(6);
        s->t = 1;                /* muzzle flash on first frame */
        s->s = (int16_t)(dir + 1);
        s->x = (int16_t)x;
        s->y = (int16_t)y;
        return;
    }
}

/* 0097:1933  move + draw Duke's shots (2 columns per frame) */
uint8_t player_shot_pass;   /* 530F: set while player shots are tested against walls */
void update_shots(void)
{
    int i;
    anim_0646 += 0xA0;
    if (anim_0646 == 0x140) anim_0646 = 0;
    for (i = 0; i < max_shots; i++) {
        Obj8 *s = &shots[i];
        if (s->s == 0) continue;
        if (s->s == 1) {                                   /* flying left */
            s->x -= 2;
            player_shot_pass = 1;
            if (shot_blocked(s->x + 1, s->y)) {
                s->s = 0; spawn_spark(1, s->x + 1, s->y + 0x40); continue;
            }
            if (offscreen(s->x, s->y)) s->s = 0;
            else if (s->t == 0)
                draw_sprite(object_gfx + (uint16_t)(anim_0646 + 0x3C0), SCREEN_X(s->x), SCREEN_Y(s->y) + 3);
            if (s->t) {
                s->t = 0;
                draw_sprite(object_gfx + 0x1CC0, SCREEN_X(s->x), SCREEN_Y(s->y) + 3);
            }
        }
        if (s->s == 5) {                                   /* flying right */
            s->x += 2;
            player_shot_pass = 1;
            if (shot_blocked(s->x, s->y)) {
                s->s = 0; spawn_spark(1, s->x - 1, s->y + 0x40); continue;
            }
            if (offscreen(s->x, s->y)) s->s = 0;
            else if (s->t == 0)
                draw_sprite(object_gfx + (uint16_t)(anim_0646 + 0x500), SCREEN_X(s->x), SCREEN_Y(s->y) + 3);
            if (s->t) {
                s->t = 0;
                draw_sprite(object_gfx + 0x1D60, SCREEN_X(s->x), SCREEN_Y(s->y) + 3);
            }
        }
    }
    player_shot_pass = 0;
}

/* 0097:1BF6  move + draw enemy shots (1 column per frame) */
void update_eshots(void)
{
    int i;
    anim_0648 += 0xA0;
    if (anim_0648 == 0x140) anim_0648 = 0;
    for (i = 0; i < max_eshots; i++) {
        EShot *e = &eshots[i];
        if (e->dir == 0) continue;
        if (e->dir == 1) {                                 /* leftwards */
            if (player_touches(e->x, e->y)) {
                if (!player_hurt && !hurt_timer && can_walk_left()) player_x--;
                goto hit;
            }
            e->x--;
            if (shot_blocked(e->x, e->y) || shot_blocked(e->x, e->y + 0x40)) { e->dir = 0; continue; }
            if (offscreen(e->x, e->y)) { e->dir = 0; }
            else if (e->active) {
                e->active = 0;
                draw_sprite(object_gfx + 0x1E00, SCREEN_X(e->x), SCREEN_Y(e->y) + 3);
            } else
                draw_sprite(object_gfx + (uint16_t)(anim_0648 + 0x1860), SCREEN_X(e->x), SCREEN_Y(e->y) + 3);
        }
        if (e->dir == 2) {                                 /* rightwards */
            if (player_touches(e->x, e->y)) {
                if (!player_hurt && !hurt_timer && can_walk_right()) player_x++;
                goto hit;
            }
            e->x++;
            if (shot_blocked(e->x + 1, e->y) || shot_blocked(e->x + 1, e->y + 0x40) || offscreen(e->x, e->y)) {
                e->dir = 0; continue;
            }
            if (e->active) {
                e->active = 0;
                draw_sprite(object_gfx + 0x1EA0, SCREEN_X(e->x), SCREEN_Y(e->y) + 3);
            } else
                draw_sprite(object_gfx + (uint16_t)(anim_0648 + 0x19A0), SCREEN_X(e->x), SCREEN_Y(e->y) + 3);
        }
        continue;
    hit:
        e->dir = 0;
        player_hurt = 1;
    }
}
