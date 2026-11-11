/*
 * actors8.c - fire monsters, note monitors, keys, locks, key doors, spike
 * rows, Dr. Proton and his video screen   (seg 0097: AF06..C23E)
 */
#include "dn1.h"

void show_level_note(void);   /* 0097:B118 */
void hint_notes(void);        /* 108E:0F2A */
void hint_lock(void);         /* 108E:0E98 */
void hint_wrong_key(void);    /* 108E:1B19 */

/* 0097:AF06  fire monster (1x2) */
static void draw_firemonster(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x140 + 0x9600);
    if (!offscreen(x, y - 0x80)) draw_sprite(anim_gfx + g, SCREEN_X(x), SCREEN_Y(y) - 16);
    if (!offscreen(x, y))        draw_sprite(anim_gfx + (uint16_t)(g + 0xA0), SCREEN_X(x), SCREEN_Y(y));
}

/* 0097:AF94  fire monsters (303Ch): invulnerable, patrol ledges */
void act_firemonsters(void)
{
    int i;
    if (anim_tick == 0 || anim_tick == 0x40) firemonster_anim++;
    if (firemonster_anim == 3) firemonster_anim = 0;
    for (i = 0; i < n_firemonsters; i++) {
        Obj6 *m = &firemonsters[i];
        if (m->s == 0) continue;
        if (!solid_at(m->x, m->y + 0x80)) m->y += 0x40;
        if (shot_wall(m->x - 1, m->y) || !solid_at(m->x - 2, m->y + 0x80)) m->s = 2;
        if (shot_wall(m->x + 2, m->y) || !solid_at(m->x + 2, m->y + 0x80)) m->s = 1;
        if (m->s == 1) {
            if (anim_tick == 0 || anim_tick == 0x40) m->x--;
            draw_firemonster(firemonster_anim, m->x, m->y);
        }
        if (m->s == 2) {
            if (anim_tick == 0 || anim_tick == 0x40) m->x++;
            draw_firemonster(firemonster_anim + 3, m->x, m->y);
        }
        shot_hits(m->x, m->y);                      /* absorbs shots */
        if (player_touches(m->x, m->y)) player_hurt = 1;
    }
}

/* 0097:B1FD  note monitor (3040h): UP shows this level's message */
void update_notes(void)
{
    if (!monitor_present || offscreen(monitor_x, monitor_y)) return;
    if (hint_5c6b == 1 && player_touches(monitor_x, monitor_y)) { hint_5c6b = 0; hint_notes(); }
    if (player_touches(monitor_x, monitor_y) && in_up) {
        play_sound(0x1C);
        show_level_note();
    }
    draw_obj(0x4CE0, monitor_x, monitor_y);
}

/* 0097:BCCB  coloured keys (3044h..3047h): s = A0h red, 140h green,
 * 1E0h blue, 280h pink */
void act_keys(void)
{
    int i;
    for (i = 0; i < n_keys; i++) {
        Obj6 *k = &keys[i];
        if (k->s == 0 || offscreen(k->x, k->y)) continue;
        if (player_touches(k->x, k->y)) {
            switch (k->s) {
            case 0x0A0: have_key_r = 1; break;
            case 0x140: have_key_g = 1; break;
            case 0x1E0: have_key_b = 1; break;
            case 0x280: have_key_p = 1; break;
            }
            play_sound(9);
            add_score(1000);
            spawn_score(0x3C0, k->x, k->y);
            draw_inventory();
            k->s = 0;
            continue;
        }
        draw_obj(k->s + 0x4CE0, k->x, k->y);
    }
}

/* ------------------------------------------------------------------------
 * 0097:BDD9  locks (3048h..304Bh): Obj8 s = lock type (140h..320h, 0 once
 * opened), t = original type (for the "used" graphic).  The right key opens
 * every key door of the matching colour.
 * ---------------------------------------------------------------------- */
void update_locks(void)
{
    int i, j;
    if (anim_tick == 0) lock_anim++;
    if (lock_anim == 0x140) lock_anim = 0;
    for (i = 0; i < n_locks; i++) {
        Obj8 *l = &locks[i];
        if (offscreen(l->x, l->y)) continue;
        if (hint_5a11 && player_touches(l->x, l->y)) { hint_5a11 = 0; hint_lock(); }
        if (player_touches(l->x, l->y) && in_up && l->s != 0) {
            if ((have_key_r && l->s == 0x140) || (have_key_g && l->s == 0x1E0) ||
                (have_key_b && l->s == 0x280) || (have_key_p && l->s == 0x320)) {
                for (j = 0; j < n_keydoors; j++) {
                    Obj8 *d = &keydoors[j];
                    if (d->s == l->s - 0xA0) {
                        uint16_t o = (uint16_t)((d->y << 1) + ((d->x >> 1) << 1));
                        d->t = 1;
                        MAP16(o - 2) = MAP16(o - 4);
                    }
                }
                switch (l->s - 0xA0) {
                case 0x0A0: have_key_r = 0; break;
                case 0x140: have_key_g = 0; break;
                case 0x1E0: have_key_b = 0; break;
                case 0x280: have_key_p = 0; break;
                }
                draw_inventory();
                l->s = 0;
                play_sound(0x1F);
                add_score(1000);
                continue;
            }
            if (hint_5c6a) hint_wrong_key();
        }
        if (l->s == 0) draw_obj(l->t + 0x5460, l->x, l->y);
        else if (lock_anim % 2) draw_obj(0x5500, l->x, l->y);
        else draw_obj(l->s + 0x5460, l->x, l->y);
    }
}

/* 0097:C0C7  key doors (304Ch..304Fh): dissolve once unlocked */
void act_keydoors(void)
{
    int i;
    for (i = 0; i < n_keydoors; i++) {
        Obj8 *d = &keydoors[i];
        if (d->s == 0 || offscreen(d->x, d->y)) continue;
        if (d->t) {
            if (anim_tick == 0 || anim_tick == 0x40) d->s += 0xA0;
            if (d->s == 0x5A0) { d->s = 0; continue; }
            draw_obj(d->s + 0x4F60, d->x, d->y);
        } else {
            draw_obj(0x5000, d->x, d->y);
        }
    }
}

/* 0097:C1B2  spike rows (3058h up / 3059h down): s = sprite offset */
void act_spikes(void)
{
    int i;
    for (i = 0; i < n_floorspikes; i++) {
        Obj6 *s = &floorspikes2[i];
        if (offscreen(s->x, s->y)) continue;
        if (player_touches(s->x, s->y)) player_hurt = 1;
        draw_obj(s->s, s->x, s->y);
    }
}

/* 0097:B292  Dr. Proton (2x2) */
static void draw_boss(int frame, int x, int y)
{
    static const uint16_t bottom[4] = {0xAA00, 0xAB40, 0xADC0, 0xAF00};
    int sx = SCREEN_X(x), sy = SCREEN_Y(y);
    uint16_t t0 = frame < 2 ? 0xA8C0 : 0xAC80, t1 = frame < 2 ? 0xA960 : 0xAD20;
    if (!offscreen(x, y - 0x80))     draw_sprite(anim_gfx + t0, sx, sy - 16);
    if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + t1, sx + 2, sy - 16);
    if ((unsigned)frame <= 3) {
        if (!offscreen(x, y))     draw_sprite(anim_gfx + bottom[frame], sx, sy);
        if (!offscreen(x + 2, y)) draw_sprite(anim_gfx + (uint16_t)(bottom[frame] + 0xA0), sx + 2, sy);
    }
    if (player_touches(x, y)) player_hurt = 1;
}

/* ------------------------------------------------------------------------
 * 0097:B595  Dr. Proton (3042h ep.3 style / 3043h ep.1).  boss_mode:
 * 1 fight (hop toward Duke, shoot), 0 escape (episode 1: flies off when
 * down to 1 hp - ends the game), 2 shot down (falls, then game over).
 * ---------------------------------------------------------------------- */
void act_boss(void)
{
    int px;
    if (boss_end_timer) {
        if (++boss_end_timer == 0x0A) game_over = 1;
        return;
    }
    boss_frame = !boss_frame;
    if (!boss_present) return;
    px = player_x + scroll_x - 2;

    if (boss_mode == 2) {                                   /* falling */
        boss_y += 0x20;
        if (anim_tick == 0)    spawn_debris(boss_x + 2, boss_y);
        if (anim_tick == 0x20) spawn_spark(1, boss_x + 2, boss_y);
        if (anim_tick == 0x40) spawn_debris(boss_x, boss_y);
        if (anim_tick == 0x60) spawn_spark(3, boss_x + 2, boss_y);
        draw_boss(px < boss_x ? 0 : 2, boss_x, boss_y);
        if (solid_at(boss_x + 1, boss_y)) { play_sound(0x29); boss_end_timer = 1; }
        return;
    }
    if (boss_mode == 0) {                                   /* escaping */
        if (!offscreen(boss_x, boss_y) && boss_dead == 0) {
            boss_dead = 1;
            play_sound(0x28);
            in_down = in_up = in_right = in_left = in_jump = in_fire = 0;
        }
        if (boss_dead > 0x1E) game_over = 1;
        if (boss_dead) boss_dead++;
        if (boss_dead && boss_y > 0x100 && scroll_y > 0x100) {
            boss_y -= 0x80;
            scroll_y -= 0x80;
            shots_blocked = 1;
        }
        draw_boss(px < boss_x ? boss_frame : boss_frame + 2, boss_x, boss_y);
        return;
    }

    /* fighting */
    if (!boss_hopping) {
        boss_wait++;
        if ((boss_delay >> 1) == boss_wait) {
            if (px < boss_x) spawn_eshot(1, boss_x, boss_y - 0x20);
            else             spawn_eshot(2, boss_x + 3, boss_y - 0x20);
        }
        if (boss_wait > boss_delay) {
            boss_wait = 0; boss_hop = 0; boss_hopping = 1;
            boss_hop_left = (px < boss_x) ? 1 : 0;
        }
    } else {
        boss_y = (int16_t)(boss_y + (int16_t)ds_u16((uint16_t)(boss_hop * 2 + 0x702)));
        if (boss_hop_left) { if (!shot_wall(boss_x - 1, boss_y - 0x40)) boss_x--; }
        else               { if (!shot_wall(boss_x + 3, boss_y - 0x40)) boss_x++; }
        if (++boss_hop == 0x0C) {
            boss_hopping = 0;
            boss_delay = (anim_tick >> 1) + 1;
            boss_wait = 0;
        }
    }
    draw_boss(px < boss_x ? boss_frame : boss_frame + 2, boss_x, boss_y);
    if (shot_hits(boss_x + 1, boss_y - 0x40)) {
        spawn_spark(3, boss_x + 2, boss_y - 0x5C);
        boss_hp--;
    }
    if (anim_tick != 0 && boss_hp < 0x0A) {
        spawn_explosion(boss_x + 2, boss_y - 0x20);
        if (boss_hp < 6) spawn_explosion(boss_x + 1, boss_y - 0x40);
        if (boss_hp < 2) spawn_explosion(boss_x + 3, boss_y - 0x40);
    }
    if (boss_hp == 1 && boss_ep1) { boss_mode = 0; return; }
    if (boss_hp == 0) boss_mode = 2;
}

/* 0097:B926  Dr. Proton's video screen (2x2) */
static void draw_protonscreen(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 0x140 + 0xA140);
    if (!offscreen(x, y - 0x80))     draw_sprite(anim_gfx + g, SCREEN_X(x), SCREEN_Y(y) - 16);
    if (!offscreen(x + 2, y - 0x80)) draw_sprite(anim_gfx + (uint16_t)(g + 0xA0), SCREEN_X(x) + 2, SCREEN_Y(y) - 16);
    if (!offscreen(x, y))            draw_sprite(anim_gfx + 0xA000, SCREEN_X(x), SCREEN_Y(y));
    if (!offscreen(x + 2, y))        draw_sprite(anim_gfx + 0xA0A0, SCREEN_X(x) + 2, SCREEN_Y(y));
    else                             protonscreen_state = 0;
}

/* 0097:BA39  the Dr. Proton screen (3041h) in the hallways: taunts once */
void update_monitor(void)
{
    /* (the game never shows it on its first level; a pack's first level may have one) */
    if (!protonscreen_present || (level_index == 0 && !pack.on)) return;
    if (++ps_frame == 3) ps_frame = 0;
    ps_timer++;
    if (ps_show) {
        show_level_note();
        ps_show = 0;
        if (!joystick_mode) SPIN_WHILE(!(last_scancode & 0x80));
    }
    if (ps_timer == 0x7D) ps_timer = 0;
    if (!protonscreen_state && !ps_seen &&
        (player_touches(protonscreen_x - 6, protonscreen_y) || player_touches(protonscreen_x + 8, protonscreen_y))) {
        play_sound(0x1D);
        protonscreen_state = 1; ps_seen = 1; ps_show = 1;
    }
    if (in_up && (player_touches(protonscreen_x, protonscreen_y) || player_touches(protonscreen_x + 2, protonscreen_y))) {
        protonscreen_state = 1;
        play_sound(0x1D);
        ps_show = 1; ps_seen = 1;
    }
    if (!protonscreen_state) draw_protonscreen(ps_frame, protonscreen_x, protonscreen_y);
    else if (player_x + scroll_x < protonscreen_x - 1) draw_protonscreen(3, protonscreen_x, protonscreen_y);
    else if (player_x + scroll_x > protonscreen_x + 3) draw_protonscreen(5, protonscreen_x, protonscreen_y);
    else draw_protonscreen(4, protonscreen_x, protonscreen_y);
}
