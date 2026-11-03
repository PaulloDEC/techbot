/*
 * actors7.c - reactor, floor spikes, flag/radio crates, teleporters, bombs,
 * card & hand readers, the robohand walkway, D-U-K-E letters, rabbits
 * (seg 0097: 8EC1..AF01)
 */
#include "dn1.h"

void hint_teleporter(void);  /* 108E:18B8 */
void hint_cardreader(void);  /* 108E:1821 */
void hint_need_card(void);   /* 108E:1A82 */
void hint_handreader(void);  /* 108E:178A */
void card_used(void);        /* 108E:3B79 */

static void ffields_h_off(void)
{
    int i;
    for (i = 0; i < n_ffield_h; i++) ffields_h[i].on = 0;
}

/* ------------------------------------------------------------------------
 * 0097:8EC1  the reactor (302Bh): 10 hits destroy it (20000 pts) and switch
 * off every horizontal force field; touching it is instant death.  While
 * blowing up, a scripted sequence of sparks/debris plays (reactor_present
 * counts 2..1Fh as the script clock).
 * ---------------------------------------------------------------------- */
void act_reactor(void)
{
    int hit = 0, k;
    reactor_anim += 0xA0;
    if (reactor_anim == 0x320) reactor_anim = 0;

    if (offscreen(reactor_x, reactor_y)) {
        if (reactor_present >= 2) { reactor_present = 0; ffields_h_off(); }
        return;
    }
    if (reactor_alive != 0 && reactor_present == 0) return;

    for (k = 0; k < 3; k++) {
        int y = reactor_y - k * 0x80;
        if (shot_hits(reactor_x, y)) {
            reactor_hp--;
            spawn_debris(reactor_x, y);
            if (reactor_hp == 0) reactor_alive = 1;
            hit = 1;
            play_sound(0x0A);
        }
    }
    if (reactor_alive == 0) {
        const uint8_t *g = hit ? anim_gfx + 0x9F60 : object_gfx + (uint16_t)(reactor_anim + 0x3840);
        play_sound(0x24);
        draw_sprite(g, SCREEN_X(reactor_x), SCREEN_Y(reactor_y));
        if (!offscreen(reactor_x, reactor_y - 0x80))  draw_sprite(g, SCREEN_X(reactor_x), SCREEN_Y(reactor_y) - 16);
        if (!offscreen(reactor_x, reactor_y - 0x100)) draw_sprite(g, SCREEN_X(reactor_x), SCREEN_Y(reactor_y) - 32);
        if (player_touches(reactor_x, reactor_y - 0x40) && health > 0) { health = 0; draw_health(); }
        return;
    }

    /* destroyed: burning husk + explosion script */
    draw_sprite(object_gfx + 0x3840, SCREEN_X(reactor_x), SCREEN_Y(reactor_y));
    if (!offscreen(reactor_x, reactor_y - 0x80))  draw_sprite(object_gfx + 0x3840, SCREEN_X(reactor_x), SCREEN_Y(reactor_y) - 16);
    if (!offscreen(reactor_x, reactor_y - 0x100)) draw_sprite(object_gfx + 0x3840, SCREEN_X(reactor_x), SCREEN_Y(reactor_y) - 32);
    reactor_present++;
    if (anim_tick == 0)    spawn_explosion(reactor_x - 1, reactor_y);
    if (anim_tick == 0x40) spawn_explosion(reactor_x + 1, reactor_y);
    if (reactor_present == 0x20) {
        reactor_present = 0;
        ffields_h_off();
        add_score(20000);
        play_sound(0x11);
        return;
    }
    if (anim_tick == 0) return;
    {
        int x = reactor_x, y = reactor_y;
        switch (reactor_present) {
        case 2:  spawn_spark(1, x - 1, y); break;
        case 8:  spawn_spark(1, x - 2, y - 0x80); break;
        case 13:
            spawn_spark(1, x, y + 0x80);  spawn_spark(1, x - 1, y + 0x180); spawn_spark(1, x, y - 0x180);
            spawn_spark(3, x + 2, y);     spawn_spark(3, x, y + 0x80);
            break;
        case 15: spawn_spark(1, x + 2, y - 0x80); break;
        case 17:
            spawn_spark(1, x + 1, y + 0x100); spawn_spark(1, x + 2, y); spawn_spark(1, x - 3, y - 0x100);
            spawn_debris(x - 1, y);
            break;
        case 18:
            spawn_debris(x + 3, y - 0x100);
            spawn_spark(1, x + 1, y + 0x100);
            break;
        case 23:
            spawn_faller(x, y + 0x40); spawn_faller(x - 1, y - 0x40);
            spawn_spark(3, x, y + 0x80);
            spawn_debris(x + 2, y); spawn_debris(x - 1, y + 0x180);
            spawn_spark(1, x - 1, y + 0x180); spawn_spark(3, x, y - 0x180);
            spawn_spark(3, x + 2, y - 0x80); spawn_spark(1, x + 1, y + 0x100);
            break;
        case 24:
            spawn_debris(x + 1, y + 0x80); spawn_debris(x - 2, y - 0x100);
            spawn_spark(1, x + 2, y); spawn_spark(3, x - 3, y - 0x100);
            spawn_debris(x - 1, y); spawn_debris(x + 3, y - 0x100);
            spawn_spark(1, x - 1, y); spawn_spark(3, x - 2, y - 0x80);
            spawn_spark(1, x + 1, y + 0x100);
            break;
        case 29:
            spawn_faller(x + 3, y - 0x80); spawn_faller(x + 2, y + 0x100);
            spawn_spark(1, x, y + 0x80);
            spawn_debris(x + 2, y); spawn_debris(x - 1, y + 0x180);
            spawn_spark(3, x - 1, y + 0x180); spawn_spark(1, x, y - 0x180);
            spawn_spark(3, x + 2, y - 0x80); spawn_spark(1, x + 1, y + 0x100);
            break;
        case 10: spawn_spark(1, x + 1, y + 0x100); break;
        case 31:
            spawn_faller(x - 2, y - 0x180);
            spawn_debris(x + 1, y + 0x80); spawn_debris(x, y);
            spawn_spark(3, x + 2, y); spawn_spark(3, x - 3, y - 0x100);
            spawn_debris(x - 1, y); spawn_debris(x + 3, y - 0x100);
            spawn_spark(1, x - 1, y); spawn_spark(1, x - 2, y - 0x80);
            spawn_spark(3, x + 1, y + 0x100);
            spawn_score(0x780, x - 1, y); spawn_score(0x780, x + 1, y);
            break;
        default: break;
        }
    }
}

/* 0097:9698  floor spikes (302Ch): Obj4 {x,y} */
void act_floorspikes(void)
{
    int i;
    for (i = 0; i < n_spikes; i++) {
        Obj4 *s = &floorspikes[i];
        if (offscreen(s->x, s->y)) continue;
        if (player_touches(s->x, s->y)) { player_hurt = 1; draw_obj(0x3C00, s->x, s->y); }
        else draw_obj(0x3B60, s->x, s->y);
    }
}

/* ------------------------------------------------------------------------
 * 0097:9757 / 9986  flag and radio crates (302Dh/3055h, 302Eh/3056h).
 * The points depend on the animation phase when collected: 100/500/2000/5000.
 * ---------------------------------------------------------------------- */
static void phase_crates(Obj6 *arr, int n, int16_t *anim, uint16_t gfx)
{
    int i;
    *anim += 0xA0;
    if (*anim == 0x1E0) *anim = 0;
    for (i = 0; i < n; i++) {
        Obj6 *c = &arr[i];
        if (c->s == 0 || offscreen(c->x, c->y)) continue;
        if (!solid_at(c->x, c->y + 0x80)) c->y += 0x80;
        if (c->s == 1 && shot_hits(c->x, c->y)) {
            spawn_debris(c->x, c->y);
            if (!solid_at(c->x, c->y + 0x80)) { add_score(500); spawn_score(0x280, c->x, c->y); }
            c->s = 2;
        }
        if (c->s == 2 && player_touches(c->x, c->y)) {
            c->s = 0;
            play_sound(0x10);
            switch (anim_tick) {
            case 0x00: add_score(100);  spawn_score(0x000, c->x, c->y); break;
            case 0x20: add_score(500);  spawn_score(0x280, c->x, c->y); break;
            case 0x40: add_score(2000); spawn_score(0x500, c->x, c->y); break;
            case 0x60: add_score(5000); spawn_score(0x640, c->x, c->y); break;
            }
            continue;
        }
        if (c->s == 1) draw_obj(0x3E80, c->x, c->y);
        else draw_obj(*anim + gfx, c->x, c->y);
    }
}
void act_flags(void)  { phase_crates(flags,  n_flags,  &flag_anim,  0x3CA0); }
void act_radios(void) { phase_crates(radios, n_radios, &radio_anim, 0x3FC0); }

/* 0097:9BB5  teleporter booth (3x3 tiles, animated) */
static void draw_teleporter(int frame, int x, int y)
{
    int sx = SCREEN_X(x), sy = SCREEN_Y(y);
    if (++tele_anim == 3) tele_anim = 0;
    if (!offscreen(x, y - 0x100))     draw_sprite(anim_gfx + 0x8480, sx, sy - 32);
    if (!offscreen(x + 4, y - 0x100)) draw_sprite(anim_gfx + 0x85C0, sx + 4, sy - 32);
    if (!offscreen(x, y - 0x80))      draw_sprite(anim_gfx + 0x8660, sx, sy - 16);
    if (!offscreen(x + 2, y - 0x80))  draw_sprite(anim_gfx + 0x8700, sx + 2, sy - 16);
    if (!offscreen(x + 4, y - 0x80))  draw_sprite(anim_gfx + 0x87A0, sx + 4, sy - 16);
    if (!offscreen(x, y))             draw_sprite(anim_gfx + 0x8840, sx, sy);
    if (!offscreen(x + 2, y))         draw_sprite(anim_gfx + 0x88E0, sx + 2, sy);
    if (!offscreen(x + 4, y))         draw_sprite(anim_gfx + 0x8980, sx + 4, sy);
    if (tele_anim == 0) {
        if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + 0x8700, sx + 2, sy - 16);
        if (!offscreen(x + 4, y - 0x80)) draw_sprite(anim_gfx + 0x87A0, sx + 4, sy - 16);
    } else if (tele_anim == 1) {
        if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + 0x8C00, sx + 2, sy - 16);
        if (!offscreen(x + 4, y - 0x80)) draw_sprite(anim_gfx + 0x8D40, sx + 4, sy - 16);
    } else if (tele_anim == 2) {
        if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + 0x8CA0, sx + 2, sy - 16);
        if (!offscreen(x + 4, y - 0x80)) draw_sprite(anim_gfx + 0x8DE0, sx + 4, sy - 16);
    }
    if ((unsigned)frame <= 3 && !offscreen(x + 2, y - 0x100)) {
        static const uint16_t top[4] = {0x8520, 0x8A20, 0x8520, 0x8AC0};
        draw_sprite(anim_gfx + top[frame], sx + 2, sy - 32);
    }
}

/* ------------------------------------------------------------------------
 * 0097:A032 / A125  teleporters A (302Fh) and B (3030h): UP inside one
 * starts an 8-frame countdown, then Duke appears in the other.
 * ---------------------------------------------------------------------- */
static void teleporter(int16_t *busy, int16_t *state, int16_t x, int16_t y,
                       int16_t ox, int16_t oy, int16_t *other_state)
{
    if (*busy == 1) {
        *busy = 0;
        scroll_x = ox - player_x + 2;
        scroll_y = oy - 0x300;
        recenter_y = 1;
        in_up = 0;
        *other_state = 8;
        *state = 0;
    }
    if (*busy > 1) (*busy)--;
    if (*state > 0) { draw_teleporter(*state % 4, x, y); (*state)--; }
    else draw_teleporter(*busy % 4, x, y);
    if (hint_68f7 && player_touches(x + 2, y)) { hint_68f7 = 0; hint_teleporter(); }
    if (player_touches(x + 2, y) && in_up && *busy == 0 && !in_left && !in_right && !in_jump) {
        *busy = 8;
        play_sound(7);
    }
}
void act_tele_a(void) { if (tele_a_present) teleporter(&tele_a_busy, &tele_a_state, tele_a_x, tele_a_y, tele_b_x, tele_b_y, &tele_b_state); }
void act_tele_b(void) { if (tele_b_present) teleporter(&tele_b_busy, &tele_b_state, tele_b_x, tele_b_y, tele_a_x, tele_a_y, &tele_a_state); }

/* 0097:A218  bouncing bombs (3031h): s = bounce phase -6..7, dy table DS:05E2 */
void act_bombs(void)
{
    int i;
    for (i = 0; i < n_bombs; i++) {
        Obj6 *b = &bombs[i];
        if (offscreen(b->x, b->y)) continue;
        b->y = (int16_t)(b->y - (int16_t)ds_u16((uint16_t)((b->s + 6) * 2 + 0x5E2)));
        if (++b->s == 8) b->s = -6;
        if (b->s == -6) play_sound(0x22);
        if (player_touches(b->x, b->y)) player_hurt = 1;
        shot_hits(b->x, b->y);
        if (!offscreen(b->x, b->y)) draw_anim(0x8B60, b->x, b->y);
    }
}

/* 0097:A30A  resting bombs (3057h): explode on contact */
void act_static_bombs(void)
{
    int i;
    for (i = 0; i < n_static_bombs; i++) {
        Obj6 *b = &static_bombs[i];
        if (b->s == 0 || offscreen(b->x, b->y)) continue;
        if (!solid_at(b->x, b->y + 0x80)) b->y += 0x40;
        if (player_touches(b->x, b->y)) {
            b->s = 0;
            player_hurt = 1;
            spawn_spark(2, b->x, b->y);
            play_sound(0x21);
        }
        draw_anim(0x8B60, b->x, b->y);
    }
}

/* ------------------------------------------------------------------------
 * 0097:A3E7  card reader (3034h): with the access card, UP disables every
 * vertical force field.  cardreader_present: -1 none, 1 armed, 0 used.
 * ---------------------------------------------------------------------- */
void update_cardreader(void)
{
    int i;
    if (cardreader_present == -1 || offscreen(cardreader_x, cardreader_y)) return;
    if (cardreader_present == 0) { draw_obj(0x46A0, cardreader_x, cardreader_y); return; }
    cardreader_anim += 0xA0;
    if (cardreader_anim == 0x500) cardreader_anim = 0;
    draw_obj(cardreader_anim + 0x41A0, cardreader_x, cardreader_y);
    if (player_touches(cardreader_x, cardreader_y) && hint_5553) { hint_5553 = 0; hint_cardreader(); }
    if (player_touches(cardreader_x, cardreader_y) && have_card && in_up) {
        for (i = 0; i < n_ffield_v; i++)
            if (i * 2 + 2 < 200) ffields_v[i * 2 + 2] = 0; else rabbits[0].s = 0;   /* see act_ffields_v */
        cardreader_present = 0;
        card_used();
        play_sound(0x1F);
        card_present = 0;
        have_card = 0;
    }
    if (player_touches(cardreader_x, cardreader_y) && !have_card && in_up && cardreader_present != 0 && hint_5555)
        hint_need_card();
}

/* ------------------------------------------------------------------------
 * 0097:A53A  hand reader (3035h): needs the robohand; without it the two
 * guns beside it open fire.  Using it starts the walkway (act_walkway).
 * ---------------------------------------------------------------------- */
void draw_handreaders(void)
{
    if (!handreader_present) return;
    if (!offscreen(handreader_x - 2, handreader_y)) draw_sprite(object_gfx + 0x4880, SCREEN_X(handreader_x) - 2, SCREEN_Y(handreader_y));
    if (!offscreen(handreader_x + 2, handreader_y)) draw_sprite(object_gfx + 0x4920, SCREEN_X(handreader_x) + 2, SCREEN_Y(handreader_y));
    if (offscreen(handreader_x, handreader_y)) { handreader_alarm = 0; return; }
    if (player_touches(handreader_x, handreader_y) && hint_555c) { hint_555c = 0; hint_handreader(); }
    if (player_touches(handreader_x, handreader_y) && in_up && !handreader_used) {
        if (have_robohand) { handreader_used = 1; walkway_go = 1; play_sound(5); }
        else handreader_alarm = 1;
    }
    if (!handreader_used && anim_tick == 0) draw_obj(0x4740, handreader_x, handreader_y);
    else draw_obj(0x47E0, handreader_x, handreader_y);
    if (handreader_alarm) {
        if (anim_tick == 0)    spawn_eshot(1, handreader_x + 2, handreader_y - 0x24);
        if (anim_tick == 0x40) spawn_eshot(2, handreader_x - 2, handreader_y - 0x24);
    }
}

/* 0097:A6F4  walkway (3036h) that grows one cell per frame once activated */
void act_walkway(void)
{
    uint16_t o;
    if (!walkway_present || !walkway_go) return;
    spawn_spark(3, walkway_x + 2, walkway_y);
    o = (uint16_t)((walkway_y << 1) + ((walkway_x >> 1) << 1));
    if (MAP16(o) == 0x2FE0) { walkway_present = 0; walkway_go = 0; }
    else MAP16(o) = 0x2FE0;
    walkway_x += 2;
}

/* ------------------------------------------------------------------------
 * 0097:A75C  letter crates D/U/K/E (3037h..303Ah): collecting them in order
 * gives 10000 points.
 * ---------------------------------------------------------------------- */
void act_letters(void)
{
    int i;
    for (i = 0; i < n_letters; i++) {
        Obj8 *l = &letters[i];
        if (l->s == 0 || offscreen(l->x, l->y)) continue;
        if (!solid_at(l->x, l->y + 0x80)) l->y += 0x80;
        if (l->t == 0 && shot_hits(l->x, l->y)) {
            spawn_debris(l->x, l->y);
            if (!solid_at(l->x, l->y + 0x80)) { add_score(500); spawn_score(0x280, l->x, l->y); }
            l->t = 1;
        }
        if (l->t && player_touches(l->x, l->y)) {
            if (l->s != 'E') {
                letter_seq[letter_count] = l->s;
                letter_count++;
                goto plain;
            }
            if (letter_count == 3 && letter_seq[0] == 'D' && letter_seq[1] == 'U' && letter_seq[2] == 'K') {
                add_score(10000);
                spawn_score(0x780, l->x, l->y);
                play_sound(0x25);
                bonus_530c = 1;
            } else {
            plain:
                add_score(500);
                spawn_score(0x280, l->x, l->y);
                play_sound(0x10);
            }
            l->s = 0;
        }
        if (offscreen(l->x, l->y)) continue;
        if (l->t == 0) { draw_obj(0, l->x, l->y); continue; }
        if (l->s == 'D') draw_obj(0x49C0, l->x, l->y);
        if (l->s == 'U') draw_obj(0x4A60, l->x, l->y);
        if (l->s == 'K') draw_obj(0x4B00, l->x, l->y);
        if (l->s == 'E') draw_obj(0x4BA0, l->x, l->y);
    }
}

/* 0097:AA1B  rabbit sprite (1x2 tiles); frames 0-2 walk, 3+ from anim 8E80h */
static void draw_rabbit(int frame, int x, int y)
{
    uint16_t g = frame > 2 ? (uint16_t)((frame - 3) * 0x140 + 0x8E80) : (uint16_t)(frame * 0x140 + 0x4740);
    if (!offscreen(x, y - 0x80)) draw_sprite(anim_gfx + g, SCREEN_X(x), SCREEN_Y(y) - 16);
    if (!offscreen(x, y))        draw_sprite(anim_gfx + (uint16_t)(g + 0xA0), SCREEN_X(x), SCREEN_Y(y));
}

/* ------------------------------------------------------------------------
 * 0097:AAC2  hopping rabbits (303Bh).  s: 1 left, 2 right, >=3 exploding;
 * t: hop phase.  bonus_5312 = all gone.
 * ---------------------------------------------------------------------- */
void act_rabbits(void)
{
    int i;
    bonus_5312 = 1;
    for (i = 0; i < n_rabbits; i++) {
        Obj8 *r = &rabbits[i];
        if (r->s == 0) continue;
        bonus_5312 = 0;
        if (r->s > 2) {
            static const int8_t f[4] = {0, 6, 3, 7};
            if ((unsigned)r->t <= 3) draw_rabbit(f[r->t], r->x, r->y);
            r->s++;
            if (++r->t == 4) r->t = 0;
            if (r->s == 0x14) {
                r->s = 0;
                spawn_explosion(r->x, r->y);
                spawn_explosion(r->x, r->y - 0x80);
                spawn_explosion(r->x - 1, r->y - 0x40);
                spawn_explosion(r->x + 1, r->y - 0x40);
                spawn_explosion(r->x + 1, r->y - 0xC0);
                spawn_explosion(r->x - 1, r->y);
                add_score(5000);
                spawn_score(0x640, r->x, r->y);
            }
            continue;
        }
        if (!solid_at(r->x, r->y + 0x80)) {
            r->y += 0x40;
        } else {
            if (shot_wall(r->x - 1, r->y) && r->s == 1) { r->s = 2; r->t = 0; }
            else if (shot_wall(r->x + 2, r->y) && r->s == 2) { r->s = 1; r->t = 0; }
        }
        if (r->t == 0) {
            if (r->s == 1) draw_rabbit(6, r->x, r->y);
            if (r->s == 2) draw_rabbit(7, r->x, r->y);
            if (anim_tick == 0x40 || anim_tick == 0) r->t = 1;
            continue;                                   /* (no hit test this frame) */
        } else {
            if (r->s == 1) {
                if (anim_tick == 0 || anim_tick == 0x40) r->x--;
                draw_rabbit(r->t < 3 ? r->t : 5 - r->t, r->x, r->y);
                if (anim_tick == 0 || anim_tick == 0x40) {
                    if (solid_at(r->x, r->y + 0x80)) r->t++;
                    if (r->t == 6) r->t = 1;
                }
            }
            if (r->s == 2) {
                if (anim_tick == 0 || anim_tick == 0x40) r->x++;
                draw_rabbit(r->t < 3 ? r->t + 2 : 8 - r->t, r->x, r->y);
                if (anim_tick == 0 || anim_tick == 0x40) {
                    if (solid_at(r->x, r->y + 0x80)) r->t++;
                    if (r->t == 6) r->t = 1;
                }
            }
        }
        if (shot_hits(r->x, r->y)) {
            r->s = 0;
            spawn_explosion(r->x, r->y);
            spawn_explosion(r->x, r->y - 0x80);
            spawn_explosion(r->x - 1, r->y - 0x40);
            spawn_explosion(r->x + 1, r->y - 0x40);
            spawn_explosion(r->x + 1, r->y - 0xC0);
            spawn_explosion(r->x - 1, r->y);
            play_sound(0x12);
            add_score(200);
            continue;
        }
        if (player_touches(r->x, r->y)) {
            player_hurt = 1;
            play_sound(0x23);
            r->s = 3;
            r->t = 0;
        }
    }
}
