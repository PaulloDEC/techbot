/*
 * player.c - Duke: movement, jumping, ledge hanging (grappling hooks),
 * somersaults (boots), shooting and the player sprite.   (seg 0097)
 *
 * Player coordinates are special: player_x is a screen column (8 px) and the
 * camera scrolls horizontally to keep him between columns 0Bh and 11h;
 * player_y is the screen pixel line of his feet + 16 (the status border).
 * Jumping follows a per-boots-level velocity table stored in the EXE at
 * DS:0613 (12 bytes per entry, indexed by have_boots).
 */
#include "dn1.h"

#define CELL(o) MAP16(o)

/* byte offset of the map cell under the player's column (+dx) at screen line py */
static uint16_t player_cell(int dx, int py)
{
    return (uint16_t)((((player_x + scroll_x + dx) >> 1) << 1) + (scroll_y << 1) + (((py >> 4) << 7) << 1));
}

/* 0097:1247  can Duke step one column left? (3 cells: head, body, feet) */
int can_walk_left(void)
{
    uint16_t o = player_cell(-3, player_y - 16), up = (uint16_t)(o - 0x100), lo = o;
    if ((player_y >> 3) & 1) lo += 0x100;
    return !(CELL(o) > SOLID_TILE || CELL(up) > SOLID_TILE || CELL(lo) > SOLID_TILE);
}

/* 0097:12CE  ... one column right? */
int can_walk_right(void)
{
    uint16_t o = player_cell(0, player_y - 16), up = (uint16_t)(o - 0x100), lo = o;
    if ((player_y >> 3) & 1) lo += 0x100;
    return !(CELL(o) > SOLID_TILE || CELL(up) > SOLID_TILE || CELL(lo) > SOLID_TILE);
}

static int grabbable(uint16_t c) { return c >= 0x2EE0 || (c >= 0x1B80 && c <= 0x1CA0); }

/* 0097:1352  room above Duke's head?  Also latches onto grapple ceilings
 * (tiles >= 2EE0h, and conveyor belts 1B80h..1CA0h) when he owns the hooks. */
int head_clear(void)
{
    uint16_t o = player_cell(-2, player_y - 40), o2 = o;
    if ((player_x + scroll_x) & 1) o2 += 2;
    if (grabbable(CELL(o)) || grabbable(CELL(o2))) {
        if (player_climb || player_falling) return 0;
        if (!player_hanging) player_walkframe = 0;
        if (have_claws) {
            player_hanging = 1;
            recenter_y = 1;
            play_sound(0x1B);
            return 0;
        }
        play_sound(0x26);
        return 0;
    }
    player_hanging = 0;
    if (CELL(o) > SOLID_TILE || CELL(o2) > SOLID_TILE) {
        if (!player_hanging) play_sound(0x26);   /* bumped head */
        return 0;
    }
    return 1;
}

/* 0097:143E  nothing solid under Duke's feet? */
int feet_clear(void)
{
    uint16_t o, o2;
    if (player_climb) return 0;
    o = player_cell(-2, player_y); o2 = o;
    if ((player_x + scroll_x) & 1) o2 += 2;
    return !(CELL(o) > SOLID_TILE || CELL(o2) > SOLID_TILE);
}

/* 0097:17F5  draw Duke (2x2 sprites from man*.dn1); blinks while hurt_timer
 * runs.  Hidden during the exit-door sequence and after the boss died. */
void draw_player(int frame, int x, int y)
{
    uint16_t g = (uint16_t)(frame * 4 * 0xA0);
    if (exit_stage >= 4 || boss_dead) return;
    if (hurt_timer > 0) hurt_timer--;
    if (y >= 0x20 && (hurt_timer % 2) == 0) {
        draw_sprite(man_gfx + g,        x - 1, y - 16);
        draw_sprite(man_gfx + g + 0xA0, x + 1, y - 16);
    } else if ((hurt_timer % 2) == 0 && dl_capturing && dl_active()) {
        /* (port) the original leaves out a half of Duke that would reach the
         * border above the play window (EGA memory there is the border);
         * Enhanced composes the screen itself, so the half goes into the
         * frame only - clipped to the view, or shown above it in Immersive */
        dl_record(DL_SPRITE, man_gfx + g,        0, x - 1, y - 16, 0);
        dl_record(DL_SPRITE, man_gfx + g + 0xA0, 0, x + 1, y - 16, 0);
    }
    if (y < 0x10) {
        recenter_y = 1;
        if ((hurt_timer % 2) == 0 && dl_capturing && dl_active()) {   /* (port) as above */
            dl_record(DL_SPRITE, man_gfx + g + 0x140, 0, x - 1, y, 0);
            dl_record(DL_SPRITE, man_gfx + g + 0x1E0, 0, x + 1, y, 0);
        }
        return;
    }
    if ((hurt_timer % 2) == 0) {
        draw_sprite(man_gfx + g + 0x140, x - 1, y);
        draw_sprite(man_gfx + g + 0x1E0, x + 1, y);
    }
}

static void land(void)
{
    player_y = (player_y >> 4) << 4;
    if (!port_sim) spawn_dust(player_x + scroll_x, (((player_y - 16) >> 4) << 7) + scroll_y);
    play_sound(0x0E);
}

/* background-layer routines called from player_update (see actors*.c) */
void draw_handreaders(void);   /* A53A */
void draw_decor(void);         /* 892B */
void draw_cameras(void);       /* 87BF */
void update_exit_door(void);   /* 6292 */
void update_notes(void);       /* B1FD */
void update_monitor(void);     /* BA39 */
void update_elevators(void);   /* 3882 */
void update_cardreader(void);  /* A3E7 */
void update_locks(void);       /* BDD9 */

/* (port) Duke's movement for one frame: the first part of player_update
 * (0097:1E77), split out unchanged so Shrapnel's reachability search can run
 * exactly the game's own movement code (with port_sim set: no sound, no dust). */
void player_motion(void)
{
    /* ------------------------------------------------ vertical motion */
    if (feet_clear() && player_grounded) {          /* walked off a ledge */
        player_grounded = 0;
        player_falling = 1;
    }
    if (!player_grounded && !player_hanging) {
        if (flip_dir) flip_timer++;
        if (flip_timer == 0x0E) { flip_timer = 0; flip_dir = 0; }
        if (!head_clear() && flip_dir) { flip_dir = 0; flip_timer = 0; }

        if (!player_falling && player_jumpstep < 9 && head_clear()) {
            /* rising: per-boots velocity table in the EXE (DS:0613) */
            player_y -= ds_u8((uint16_t)(have_boots * 0x0C + 0x613 + player_jumpstep));
            player_jumpstep++;
            recenter_y = 0;
            if (!feet_clear()) {                    /* hit a platform while rising */
                player_grounded = 1;
                recenter_y = 1;
                land();
                flip_dir = 0; flip_timer = 0;
            }
        } else {
            player_falling = 1;
        }

        if (player_falling) {
            if (!feet_clear()) {
                player_grounded = 1; recenter_y = 1; player_falling = 0;
                flip_dir = 0; flip_timer = 0;
                land();
            } else {
                player_y += 8;
            }
            /* NB: after a landing above this fires a second time
             * (second dust puff + sound); kept as in the original. */
            if (!feet_clear()) {
                land();
                flip_dir = 0; flip_timer = 0;
                recenter_y = 1;
            }
            if (player_y >= 0x84) {                 /* low on screen: fall faster */
                player_y += 8;
                recenter_y = 1;
                if (!feet_clear()) {
                    player_grounded = 1;
                    player_jumpstep = 0;
                    player_falling = 0;
                    land();
                }
            }
        }
    }

    /* ------------------------------------------ pull up while hanging */
    if (in_up && player_hanging && !player_climb) {
        player_y -= 0x10;
        if (head_clear()) {
            player_y -= 0x10;
            if (head_clear()) { player_climb = 3; player_hanging = 0; }
            player_y += 0x10;
        } else {
            player_climb = 0;
            player_hanging = 1;
        }
        player_y += 0x10;
    }
    if (in_down && player_hanging) { player_walkframe = 0; player_hanging = 0; }

    /* ------------------------------------------------ walk left */
    if (!(player_hanging && in_fire) && !in_right && in_left && !player_climb) {
        if (player_facing == 0 && can_walk_left()) player_x--;
        if (player_facing != 0) {
            player_facing = 0; player_walkframe = 0; flip_dir = 0;
        } else if (anim_tick == 0 || anim_tick == 0x40) {
            if (!feet_clear()) play_sound(0x16);
            if (player_walkframe < 3) player_walkframe++; else player_walkframe = 0;
        }
    }
    /* keep Duke at column 0Bh..11h by scrolling */
    if (player_x == 0x0B) {
        if (scroll_x > 0) { scroll_x--; player_x++; }
    } else if (player_x <= 0x0A && scroll_x > 1) {
        scroll_x -= 2; player_x += 2;
        if (scroll_x == 1) { player_x++; scroll_x--; }
    }

    /* ------------------------------------------------ walk right */
    if (!(player_hanging && in_fire) && !in_left && in_right && !player_climb) {
        if (player_facing != 0 && can_walk_right()) player_x++;
        if (player_facing == 0) {
            player_facing = 4; player_walkframe = 0; flip_dir = 0;
        } else if (anim_tick == 0 || anim_tick == 0x40) {
            if (!feet_clear()) play_sound(0x16);
            if (player_walkframe < 3) player_walkframe++; else player_walkframe = 0;
        }
    }
    if (player_x == 0x11) {
        if (scroll_x < 0xE6) { scroll_x++; player_x--; }
    } else if (player_x >= 0x12 && scroll_x < 0xE6) {
        scroll_x += 2; player_x -= 2;
        if (scroll_x == 0xE5) { player_x--; scroll_x++; }
    }

    if (player_hanging) {
        player_y = (player_y >> 4) << 4;
        player_falling = 1;
        player_grounded = 0;
        head_clear();                               /* re-latch / let go */
    }

    /* ------------------------------------------------ jump */
    if (in_jump && player_grounded && !jump_latch && !player_hanging && !player_climb) {
        player_grounded = 0; player_jumpstep = 0; player_falling = 0;
        if (have_boots && anim_tick == 0) {         /* somersault */
            if (in_left && can_walk_left()) flip_dir = 1;
            if (in_right && can_walk_right()) flip_dir = 2;
        }
        play_sound(0x0D);
    }
    jump_latch = (in_jump && !player_hanging) ? 1 : 0;
}

/* (port) the ledge pull-up's per-frame step (from player_update's sprite part) */
void player_climb_step(void)
{
    if (player_climb && !player_hurt) {
        player_climb--;
        player_y -= 0x10;
        if (!port_sim) draw_player((player_facing ? 0x2B : 0x28) - player_climb, player_x, player_y);
        if (player_climb == 0) recenter_y = 1;
    }
}

/* 0097:1E77  per-frame player logic + world/background drawing */
void player_update(void)
{
    if (player_hurt && hurt_timer) player_hurt = 0;

    if (death_timer == 0) player_motion();

    /* ------------------------------------------- background layer */
    draw_world();
    draw_handreaders();
    draw_decor();
    draw_cameras();
    update_exit_door();
    update_notes();
    update_monitor();
    update_elevators();
    update_cardreader();
    update_locks();
    if (death_timer) return;

    /* ------------------------------------------- Duke sprite */
    if (player_hanging && !in_fire && !player_climb && !player_hurt) {
        draw_player((player_facing ? 0x20 : 0x1C) + player_walkframe, player_x, player_y);
        if ((in_left != 0) != (in_right != 0))
            if (anim_tick == 0x20 || anim_tick == 0x60) play_sound(0x1B);
    }
    if (player_hanging && in_fire && !player_climb && !player_hurt)
        draw_player(player_facing ? 0x25 : 0x24, player_x, player_y);
    if (in_fire && !in_left && !in_right && !player_hanging && !player_climb && !player_hurt)
        player_walkframe = 3;
    if (!flip_dir && !in_left && !in_right && !in_fire && !player_hanging && !player_climb && !player_hurt)
        player_walkframe = player_facing ? 9 : 0x0C;

    /* standing / walking (also when airborne but already touching ground) */
    if ((!player_falling && player_grounded) || !feet_clear())
        if (!flip_dir && !player_hanging && !player_climb && !player_hurt)
            draw_player(player_facing + player_walkframe, player_x, player_y);

    /* airborne */
    if (!player_grounded && feet_clear() && !flip_dir && !player_hanging && !player_climb && !player_hurt) {
        if (!player_falling) draw_player(player_facing ? player_facing + 5 : player_facing + 8, player_x, player_y);
        else                 draw_player(player_facing ? player_facing + 7 : player_facing + 0x0A, player_x, player_y);
    }

    /* pulling up onto a ledge (3 frames) */
    player_climb_step();

    /* somersault: 7 frames, two ticks each */
    if (flip_dir && !player_hanging && !player_climb && !player_hurt && flip_timer <= 0x0D) {
        if (flip_dir == 1) draw_player(0x0E + flip_timer / 2, player_x, player_y);
        else if (flip_dir == 2) draw_player(0x15 + flip_timer / 2, player_x, player_y);
    }

    /* fire (edge triggered) */
    if (in_fire && !fire_held && !player_hurt) {
        if (shots_blocked == 0)
            spawn_shot(player_x + scroll_x, (((player_y - 29) >> 4) << 7) + scroll_y, player_facing);
        if (player_hanging) player_walkframe = player_facing ? 5 : 8;
    }
    fire_held = in_fire ? 1 : 0;

    /* took a hit this frame */
    if (player_hurt && !hurt_timer) {
        bonus_d247 = 0;
        if (death_timer == 0 && !port_god) { health--; draw_health(); }   /* (port) editor play-test: invulnerable */
        play_sound(0x0F);
        player_hurt = 0;
        hurt_timer = 0x15;
        player_jumpstep = 0; player_grounded = 0; player_hanging = 0;
        player_falling = 1;
        flip_dir = 0; flip_timer = 0;
        draw_player(player_facing ? 0x2D : 0x2C, player_x, player_y);
    }
}
