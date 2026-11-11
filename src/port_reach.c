/*
 * port_reach.c - Shrapnel's reachability search (port addition).
 *
 * Duke's movement is not modelled: the search runs the game's own movement
 * code (player_motion, recenter_view, player_climb_step and the collision
 * tests they use) one frame at a time, from every state Duke can be in, with
 * every combination of the controls.  A state is everything that movement
 * code reads: where he is (and where the camera is, since how fast he falls
 * depends on his height on screen), whether he is standing, jumping, falling,
 * hanging or pulling himself up, which way he faces, and the jump latch.
 *
 * The world can change on the way: boots, claws, the robot hand, the access
 * card and keys can be picked up, locks opened, the card reader and hand
 * reader used and the reactor shot.  Each of those is a bit of the search
 * state too ("what has been done"), so an order that matters - which lock a
 * key is spent on, say - is found as the game would play it.
 *
 * A few of the moving parts Duke rides are mirrored here from their actors,
 * small enough to follow line for line: elevators (update_elevators), conveyor
 * belts (act_conveyors) and teleporters (teleporter).  Enemies, hazards and
 * damage are left out: this answers "can he get there", not "will he survive".
 *
 * The result: which cells Duke can be in, which of those leave no way to the
 * end (traps), which actors he can reach, and one route to the end, frame by
 * frame, with the controls that play it.
 *
 * The search runs in slices (ed_reach_run), so the editor stays responsive;
 * each slice loads the level again, since the editor uses the same game state
 * between slices.
 */
#include "dn1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void ed_start_clamp(void);               /* port_editor.c: as a play-test starts */

/* ------------------------------------------------------------------ state */
/* the controls, as route bits */
#define IN_L 1
#define IN_R 2
#define IN_U 4
#define IN_D 8
#define IN_J 16
#define IN_F 32
#define IN_TELE 64          /* (route) the teleporter's countdown follows: 8 idle frames */

/* what has been done (search bits) */
#define W_BOOTS   0x0001
#define W_CLAWS   0x0002
#define W_HAND    0x0004
#define W_CARD    0x0008    /* carrying the access card */
#define W_CARDUSE 0x0010    /* the card reader used: vertical force fields off */
#define W_REACTOR 0x0020    /* the reactor destroyed: horizontal force fields off */
#define W_WALKWAY 0x0040    /* the hand reader used: the walkway is built */
#define W_KEY(c)  (0x0100 << (c))   /* carrying a key: 0 red 1 green 2 blue 3 pink */
#define W_LOCK(c) (0x1000 << (c))   /* that colour's lock opened (its doors gone) */
#define W_BRIDGE  0x30000   /* the collapsing bridge: 0 untouched, 1 walked on, 2 walked off */
#define W_BRIDGE1 0x10000
#define W_BRIDGE2 0x20000
#define W_GONE    0x30000   /* (both bits) it has collapsed */

typedef struct {
    int16_t ax;             /* player_x + scroll_x */
    int16_t py;             /* player_y (screen) */
    int16_t sy;             /* scroll_y >> 7 */
    uint8_t grounded, falling, hanging, climb, jumpstep, latch, facing, recenter;
    uint8_t ei, eh;         /* the elevator being ridden (0xFF none) and its height */
    uint8_t wk;             /* the walkway: cells built so far while it grows (0: not growing) */
    uint8_t ht, hp;         /* hurt_timer (blinking: no more hits) and player_hurt (a hit to take) */
} St;

/* 64 bits: ax 9, py 9 (+128), sy 7 (+16), flags 12, elevator 7+7, walkway 7,
 * hurt 5+1 (on_map keeps the values in range) */
static uint64_t pack_st(const St *s)
{
    uint64_t k = 0;
    k |= (uint64_t)(s->ax & 0x1FF);
    k |= (uint64_t)((s->py + 128) & 0x1FF) << 9;
    k |= (uint64_t)((s->sy + 16) & 0x7F) << 18;
    k |= (uint64_t)(s->grounded & 1) << 25;
    k |= (uint64_t)(s->falling & 1) << 26;
    k |= (uint64_t)(s->hanging & 1) << 27;
    k |= (uint64_t)(s->climb & 3) << 28;
    k |= (uint64_t)(s->jumpstep & 15) << 30;
    k |= (uint64_t)(s->latch & 1) << 34;
    k |= (uint64_t)(s->facing ? 1 : 0) << 35;
    k |= (uint64_t)(s->recenter & 1) << 36;
    k |= (uint64_t)(s->ei & 0x7F) << 37;
    k |= (uint64_t)(s->eh & 0x7F) << 44;
    k |= (uint64_t)(s->wk & 0x7F) << 51;
    k |= (uint64_t)(s->ht & 0x1F) << 58;
    k |= (uint64_t)(s->hp & 1) << 63;
    return k;
}
static void unpack(uint64_t k, St *s)
{
    s->ax = (int16_t)(k & 0x1FF);
    s->py = (int16_t)(((k >> 9) & 0x1FF) - 128);
    s->sy = (int16_t)(((k >> 18) & 0x7F) - 16);
    s->grounded = (k >> 25) & 1;
    s->falling = (k >> 26) & 1;
    s->hanging = (k >> 27) & 1;
    s->climb = (k >> 28) & 3;
    s->jumpstep = (k >> 30) & 15;
    s->latch = (k >> 34) & 1;
    s->facing = ((k >> 35) & 1) ? 4 : 0;
    s->recenter = (k >> 36) & 1;
    s->ei = (uint8_t)((k >> 37) & 0x7F); if (s->ei == 0x7F) s->ei = 0xFF;
    s->eh = (uint8_t)((k >> 44) & 0x7F);
    s->wk = (uint8_t)((k >> 51) & 0x7F);
    s->ht = (uint8_t)((k >> 58) & 0x1F);
    s->hp = (uint8_t)((k >> 63) & 1);
}

/* ------------------------------------------------------------------ nodes */
#pragma pack(push, 4)
typedef struct {
    uint64_t s;
    uint32_t parent;        /* node it was first reached from (route) */
    uint32_t w;
    uint8_t  in;            /* controls used on that step */
    uint8_t  flags;         /* N_FINISH, N_GOOD */
    uint8_t  nsucc;         /* successors (from succ[succ_at[i]]) */
    uint8_t  world;         /* index in worlds[] */
} Node;
#pragma pack(pop)
#define N_FINISH 1          /* the end is reached from here, in one frame */
#define N_GOOD   2          /* (after) the end can still be reached from here */
#define N_DONE   4          /* expanded (its successors listed) */

static Node *nodes;
static uint32_t n_nodes, cap_nodes, max_nodes;
static uint32_t *table;     /* open addressing: node index + 1, 0 empty */
static uint32_t table_mask;
static uint32_t *succ, n_succ, cap_succ;      /* each node's successors, in node order */
static uint32_t *succ_at;                     /* where node i's list starts (n_nodes + 1) */

static uint64_t hkey(uint64_t s, uint32_t w)
{
    uint64_t h = s * 0x9E3779B97F4A7C15ull ^ ((uint64_t)w * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    return h;
}
static int grow_table(void)
{
    uint32_t i, size = table ? (table_mask + 1) * 2 : 1u << 16;
    uint32_t *t = (uint32_t *)calloc(size, sizeof *t);
    if (!t) return 0;
    for (i = 0; i < n_nodes; i++) {
        uint32_t h = (uint32_t)hkey(nodes[i].s, nodes[i].w) & (size - 1);
        while (t[h]) h = (h + 1) & (size - 1);
        t[h] = i + 1;
    }
    free(table);
    table = t;
    table_mask = size - 1;
    return 1;
}
/* the node for (s, w): found, or added (*fresh = 1); UINT32_MAX when full */
static uint32_t node_get(uint64_t s, uint32_t w, int *fresh)
{
    uint32_t h;
    *fresh = 0;
    if ((!table || (n_nodes + 1) * 2 > table_mask + 1) && !grow_table()) return UINT32_MAX;
    h = (uint32_t)hkey(s, w) & table_mask;
    while (table[h]) {
        Node *n = &nodes[table[h] - 1];
        if (n->s == s && n->w == w) return table[h] - 1;
        h = (h + 1) & table_mask;
    }
    if (n_nodes >= max_nodes) return UINT32_MAX;
    if (n_nodes == cap_nodes) {
        uint32_t nc = cap_nodes ? cap_nodes * 2 : 65536;
        Node *nn = (Node *)realloc(nodes, (size_t)nc * sizeof *nn);
        uint32_t *na = (uint32_t *)realloc(succ_at, ((size_t)nc + 1) * sizeof *na);
        if (na) succ_at = na;
        if (!nn || !na) { if (nn) nodes = nn; return UINT32_MAX; }
        nodes = nn; cap_nodes = nc;
    }
    memset(&nodes[n_nodes], 0, sizeof nodes[n_nodes]);
    nodes[n_nodes].s = s;
    nodes[n_nodes].w = w;
    table[h] = n_nodes + 1;
    *fresh = 1;
    return n_nodes++;
}
static int succ_add(uint32_t b)
{
    if (n_succ == cap_succ) {
        uint32_t nc = cap_succ ? cap_succ * 2 : 1u << 20;
        uint32_t *ne = (uint32_t *)realloc(succ, (size_t)nc * sizeof *ne);
        if (!ne) return 0;
        succ = ne; cap_succ = nc;
    }
    succ[n_succ++] = b;
    return 1;
}

/* ------------------------------------------------------------------ the level */
static uint16_t base_map[ED_MAP_CELLS];      /* the map as loaded */
typedef struct { int present; int16_t x, y; } Spot;
static Spot boots_at, claws_at, hand_at, card_at;
static int boss_goal;                        /* the end is Dr. Proton (no exit) */

static void put_cell(int i, uint16_t v) { mapbuf[i * 2] = (uint8_t)v; mapbuf[i * 2 + 1] = (uint8_t)(v >> 8); }
static uint16_t get_cell(int i) { return (uint16_t)(mapbuf[i * 2] | mapbuf[i * 2 + 1] << 8); }

/* The cells that change with the world: each colour's doors (as
 * update_locks opens them) and the walkway (as act_walkway builds it, all at
 * once).  Worked out once from the loaded map; switching worlds then only
 * touches those cells. */
#define MAX_WCELLS 512
typedef struct { uint16_t cell, v; } WCell;
static WCell wcells[6][MAX_WCELLS];          /* 0..3 doors by colour, 4 the walkway, 5 the bridge gone */
static int n_wcells[6];
static uint8_t blasted[ED_MAP_CELLS];        /* cells changed by shooting: 1 a breakable block (1800h) shot away, 2 by a rocket */
static uint16_t blasted_v[ED_MAP_CELLS];     /* ... and what is there since */
static uint8_t rocket_gone[100];             /* rockets taken as shot (launched) */
static int has_blocks;                       /* any left in the map */
static uint16_t pending[ED_MAP_CELLS];       /* shot away since the states near them were last looked at */
static int n_pending;
static uint32_t cur_w;                       /* the world the map now shows */

static void world_cells(void)
{
    int c, j, i;
    for (c = 0; c < 6; c++) n_wcells[c] = 0;
    for (c = 0; c < 4; c++) {               /* in the game's order: a door takes what is left of it by then */
        int16_t lock_s = (int16_t)((c + 2) * 0xA0);
        for (j = 0; j < n_keydoors && j < 200; j++) {
            Obj8 *d = &keydoors[j];
            if (d->s == lock_s - 0xA0 && n_wcells[c] < MAX_WCELLS) {
                uint16_t o = (uint16_t)((d->y << 1) + ((d->x >> 1) << 1));
                int cell = (uint16_t)(o - 2) / 2;
                if (cell >= 1 && cell < ED_MAP_CELLS) {
                    wcells[c][n_wcells[c]].cell = (uint16_t)cell;
                    wcells[c][n_wcells[c]].v = MAP16(o - 4);
                    put_cell(cell, MAP16(o - 4));
                    n_wcells[c]++;
                }
            }
        }
        for (j = 0; j < n_wcells[c]; j++) put_cell(wcells[c][j].cell, base_map[wcells[c][j].cell]);
    }
    if (walkway_present) {
        int16_t x = walkway_x;
        for (i = 0; i < 130 && n_wcells[4] < MAX_WCELLS; i++) {
            uint16_t o = (uint16_t)((walkway_y << 1) + ((x >> 1) << 1));
            int cell = o / 2;
            if (cell >= ED_MAP_CELLS || MAP16(o) == 0x2FE0) break;
            wcells[4][n_wcells[4]].cell = (uint16_t)cell;
            wcells[4][n_wcells[4]].v = 0x2FE0;
            n_wcells[4]++;
            x += 2;
        }
    }
    if (bridge_present)                     /* as act_bridge collapses it */
        for (i = 0; (bridge_len >> 1) > i && n_wcells[5] < MAX_WCELLS; i++) {
            uint16_t o = (uint16_t)((bridge_col << 1) + (i << 1) + ((bridge_row << 7) << 1) - 4);
            if (o / 2 >= ED_MAP_CELLS) break;
            wcells[5][n_wcells[5]].cell = (uint16_t)(o / 2);
            wcells[5][n_wcells[5]].v = MAP16(o - 0x100);
            n_wcells[5]++;
        }
    cur_w = 0;
}
static int world_has(uint32_t w, int g)
{
    if (g < 4) return (w & W_LOCK(g)) != 0;
    if (g == 4) return (w & W_WALKWAY) != 0;
    return (w & W_BRIDGE) == W_GONE;
}
static void world_map(uint32_t w)
{
    int g, k;
    if (w == cur_w) return;
    for (g = 0; g < 6; g++) {
        int on = world_has(w, g);
        if (on == world_has(cur_w, g)) continue;
        for (k = 0; k < n_wcells[g]; k++)
            put_cell(wcells[g][k].cell, on ? wcells[g][k].v : base_map[wcells[g][k].cell]);
    }
    cur_w = w;
}

/* the game's globals from a search state */
static void set_globals(const St *s)
{
    player_x = 0x0E;
    scroll_x = (int16_t)(s->ax - 0x0E);
    player_y = s->py;
    scroll_y = (int16_t)(s->sy << 7);
    player_grounded = s->grounded;
    player_falling = s->falling;
    player_hanging = s->hanging;
    player_climb = s->climb;
    player_jumpstep = s->jumpstep;
    jump_latch = s->latch;
    player_facing = s->facing;
    recenter_y = s->recenter;
    player_hurt = 0; hurt_timer = 0; death_timer = 0;
    flip_dir = 0; flip_timer = 0; player_walkframe = 0;
    anim_tick = 1;          /* (only the walking animation and somersaults read it) */
}
static void get_globals(St *s)
{
    s->ax = (int16_t)(player_x + scroll_x);
    s->py = player_y;
    s->sy = (int16_t)(scroll_y >> 7);
    s->grounded = (uint8_t)(player_grounded != 0);
    s->falling = (uint8_t)(player_falling != 0);
    s->hanging = (uint8_t)(player_hanging != 0);
    s->climb = (uint8_t)player_climb;
    s->jumpstep = (uint8_t)player_jumpstep;
    s->latch = (uint8_t)(jump_latch != 0);
    s->facing = (uint8_t)player_facing;
    s->recenter = (uint8_t)(recenter_y != 0);
}
static void set_inputs(int in)
{
    in_left = (in & IN_L) != 0; in_right = (in & IN_R) != 0;
    in_up = (in & IN_U) != 0;   in_down = (in & IN_D) != 0;
    in_jump = (in & IN_J) != 0; in_fire = (in & IN_F) != 0;
}

/* A ridden elevator's cells are written into the map for a step and taken
 * back after it: the column above each elevator touched is kept first. */
static int16_t elev_y0[100];
static uint16_t col_keep[2][90];
static int col_of[2], n_cols;
static void keep_col(int ei)
{
    int c = (elevators[ei].x >> 1) - 1, r, k;
    for (k = 0; k < n_cols; k++) if (col_of[k] == c) return;
    if (n_cols == 2 || c < 0 || c >= 128) return;
    for (r = 0; r < 90; r++) col_keep[n_cols][r] = get_cell(r * 128 + c);
    col_of[n_cols++] = c;
}
static void restore_cols(void)
{
    int r, k;
    for (k = 0; k < n_cols; k++)
        for (r = 0; r < 90; r++) put_cell(r * 128 + col_of[k], col_keep[k][r]);
    n_cols = 0;
}
static void elevator_cells(const St *s)
{
    int k;
    Obj6 *e;
    if (s->ei == 0xFF) return;
    keep_col(s->ei);
    e = &elevators[s->ei];
    for (k = 1; k < s->eh; k++) {
        int16_t y = (int16_t)(elev_y0[s->ei] - 0x80 * k);
        uint16_t o = (uint16_t)((y << 1) + ((e->x >> 1) << 1) - 2);
        MAP16(o) = 0x1AE0;
    }
}

/* the actors the search mirrors, in the order the game runs them */
void act_conveyors(void); void act_ffields_v(void); void act_ffields_h(void); void act_floorspikes(void);
void act_spikes(void); void act_bridge(void); void act_boots(void); void act_claws(void); void act_robohand(void);
void act_card(void); void act_keys(void);
enum { A_CONV = 1, A_FFV, A_FFH, A_FSPIKES, A_SPIKES, A_BRIDGE, A_BOOTS, A_CLAWS, A_HAND, A_CARD, A_KEYS };
static uint8_t actor_order[100];
static int n_order;
static void actor_order_from_level(void)
{
    static const struct { ActorFn fn; uint8_t a; } known[] = {
        { act_conveyors, A_CONV }, { act_ffields_v, A_FFV }, { act_ffields_h, A_FFH }, { act_floorspikes, A_FSPIKES },
        { act_spikes, A_SPIKES }, { act_bridge, A_BRIDGE }, { act_boots, A_BOOTS }, { act_claws, A_CLAWS },
        { act_robohand, A_HAND }, { act_card, A_CARD }, { act_keys, A_KEYS },
    };
    int i, j;
    n_order = 0;
    for (i = 0; i < n_actor_fns && i < 100; i++)
        for (j = 0; j < (int)(sizeof known / sizeof known[0]); j++)
            if (actor_fns[i] == known[j].fn) actor_order[n_order++] = known[j].a;
}

/* within the map (Duke's body and the cell under his feet) */
static int on_map(const St *s)
{
    int r = s->sy + (s->py >> 4);
    return s->ax >= 2 && s->ax <= 255 && r >= 2 && r < 90 &&
           s->py >= -128 && s->py < 384 && s->sy >= -16 && s->sy < 112 && s->eh < 127 && s->wk < 127;
}

/* ------------------------------------------------------------------ one frame */
/* mirrors update_elevators for the elevator being ridden (or a new one) */
static void elevators_step(St *s, int in)
{
    int i;
    for (i = 0; i < n_elevators && i < 100; i++) {
        Obj6 *e = &elevators[i];
        int h = (s->ei == i) ? s->eh : 1;
        int16_t y = (int16_t)(elev_y0[i] - 0x80 * (h - 1));
        if (player_on(e->x, y - 0x20)) {
            if (!player_hanging && (in & IN_U) && head_clear() && !(in & IN_D) && h < 90) {
                uint16_t o;
                keep_col(i);
                h++;
                y -= 0x80;
                o = (uint16_t)((y << 1) + ((e->x >> 1) << 1) - 2);
                MAP16(o) = 0x1AE0;
                player_y -= 0x10;
                if (player_y <= 0x40) recenter_y = 1;
                player_hanging = 0;
                s->ei = (uint8_t)i; s->eh = (uint8_t)h;
            }
            return;
        }
    }
    if (s->ei != 0xFF) {            /* stepped off: it sinks a cell a frame */
        /* (once Duke is clear of its column, it is taken as down already:
         * where it is while sinking only matters to him next to it, and
         * keeping it would copy every state he can reach meanwhile) */
        if (--s->eh <= 1 || abs(player_x + scroll_x - elevators[s->ei].x) > 3) { s->ei = 0xFF; s->eh = 0; }
    }
}
/* mirrors the part of act_conveyors that carries Duke */
static void conveyors_step(void)
{
    int i;
    for (i = 0; i < n_conveyors && i < 100; i++) {
        Conveyor *c = &conveyors[i];
        int len = c->len;
        if (player_in_span(c->x - 2, c->x + len + 2, c->y - 0x20)) {
            if (c->dir == 0 && can_walk_left()) player_x--;
            if (c->dir == 1 && can_walk_right()) player_x++;
        }
        if (player_hanging && player_in_span(c->x - 2, c->x + len + 2, c->y + 0x60)) {
            if (c->dir == 1 && can_walk_left()) player_x--;
            if (c->dir == 0 && can_walk_right()) player_x++;
        }
    }
}

/* places Duke cannot be in this world: force fields (they push him back or
 * hurt him) and the reactor (instant death) */
static int forbidden(uint32_t w)
{
    return !(w & W_REACTOR) && reactor_present && player_touches(reactor_x, reactor_y);
}

/* while Duke blinks (ht frames to go), can he reach anything that hurts?
 * He moves at most a column and a row a frame. */
static int hazard_near(const St *s, uint32_t w)
{
    int i, ax = s->ax, row = s->sy + (s->py >> 4), d = s->ht + 3;
#define NEAR(x, y) (abs((x) - ax) <= d + 2 && abs(((y) >> 7) - row) <= d + 3)
    if (!(w & W_REACTOR))
        for (i = 0; i < n_ffield_h && i < 100; i++) if (NEAR(ffields_h[i].x, ffields_h[i].y)) return 1;
    for (i = 0; i < n_spikes && i < 100; i++) if (NEAR(floorspikes[i].x, floorspikes[i].y)) return 1;
    for (i = 0; i < n_floorspikes && i < 200; i++) if (NEAR(floorspikes2[i].x, floorspikes2[i].y)) return 1;
#undef NEAR
    return 0;
}


/* can a shot from here hit an object at (x, y)?  (shot_hits takes a shot on
 * its row or the row above; h 2 for that) */
static int can_shoot(int16_t x, int16_t y, int h)
{
    int sx = player_x + scroll_x, srow = ((((player_y - 29) >> 4) << 7) + scroll_y) >> 7;
    int orow = y >> 7, dir, cx;
    if (srow > orow || srow <= orow - h) return 0;
    if (abs(x - sx) > 40) return 0;     /* a shot leaves the screen (13 cells) */
    dir = x > sx ? 1 : -1;
    for (cx = sx; cx != x; cx += dir) {
        uint16_t o = (uint16_t)((srow << 8) + ((cx >> 1) << 1) - 2);
        if (cx > 1 && MAP16(o) > SOLID_TILE) return 0;
    }
    return 1;
}

/* run one frame from s with controls in; the new state, or 0 when it leaves
 * the map or lands somewhere he cannot be */
static int step(const St *s, uint32_t w, int in, St *out, uint32_t *wout, int *finish)
{
    St t = *s;
    int i, k, ok = 1;
    *finish = 0;
    *wout = w;
    n_cols = 0;
    if (s->wk && (w & W_WALKWAY))          /* still growing: the cells it has not reached yet */
        for (i = s->wk; i < n_wcells[4]; i++) put_cell(wcells[4][i].cell, base_map[wcells[4][i].cell]);
    elevator_cells(s);
    set_globals(s);
    have_boots = (w & W_BOOTS) != 0;     /* the jump table and the hooks depend on these */
    have_claws = (w & W_CLAWS) != 0;
    set_inputs(in);
    recenter_view();
    if (t.hp && t.ht) t.hp = 0;          /* player_update: no hit while blinking */
    player_motion();
    /* player_update's background part: the hand reader and the exit door,
     * then elevators, the card reader and locks */
    /* (offscreen: what is outside the game's activity window does nothing -
     * above the view, say, while the camera catches up with a jump) */
    if ((in & IN_U) && handreader_present && !offscreen(handreader_x, handreader_y) && player_touches(handreader_x, handreader_y)
        && (w & W_HAND) && !(w & W_WALKWAY))
        *wout |= W_WALKWAY;
    if (exit_present && (in & IN_U) && !(in & (IN_L | IN_R | IN_J)) &&
        (player_touches(exit_x + 1, exit_y) || player_touches(exit_x + 1, exit_y - 0x80)))
        *finish = 1;
    elevators_step(&t, in);
    if ((in & IN_U) && cardreader_present == 1 && !offscreen(cardreader_x, cardreader_y) &&
        player_touches(cardreader_x, cardreader_y) && (w & W_CARD)) {
        *wout &= ~W_CARD; *wout |= W_CARDUSE;
    }
    if (in & IN_U)
        for (i = 0; i < n_locks && i < 15; i++) {
            Obj8 *l = &locks[i];
            int c = l->s / 0xA0 - 2;
            if (c < 0 || c > 3 || (w & W_LOCK(c)) || offscreen(l->x, l->y)) continue;
            if ((w & W_KEY(c)) && player_touches(l->x, l->y)) { *wout &= ~W_KEY(c); *wout |= W_LOCK(c); }
        }
    player_hurt = t.hp;
    player_climb_step();
    if (player_y < 0x10) recenter_y = 1;    /* draw_player: Duke above the top of the view brings the camera */
    /* player_update's last part: the blinking runs down (draw_player), then a hit knocks him off his feet */
    if (t.ht) t.ht--;
    if (t.hp && !t.ht) {
        t.hp = 0;
        t.ht = 0x15;
        player_jumpstep = 0; player_grounded = 0; player_hanging = 0;
        player_falling = 1;
    }
    /* the actors, in the order the game runs them (the order the level
     * first names each kind): conveyors carry him, force fields push him
     * back, hazards hurt, the bridge counts his steps, he picks things up */
    for (k = 0; k < n_order; k++)
        switch (actor_order[k]) {
        case A_CONV: conveyors_step(); break;
        case A_FFV:
            if (!(w & W_CARDUSE))
                for (i = 0; i < n_ffield_v && i < 100; i++) {
                    int16_t x = ffields_v[i * 2], y = ffields_v[i * 2 + 1];
                    if (offscreen(x, y)) continue;
                    if (player_touches(x + 1, y) && (in & IN_L) && can_walk_right()) player_x++;
                    if (player_touches(x - 1, y) && (in & IN_R) && can_walk_left()) player_x--;
                }
            break;
        case A_FFH:
            if (!(*wout & W_REACTOR))
                for (i = 0; i < n_ffield_h && i < 100; i++)
                    if (!offscreen(ffields_h[i].x, ffields_h[i].y) && player_touches(ffields_h[i].x, ffields_h[i].y)) t.hp = 1;
            break;
        case A_FSPIKES:
            for (i = 0; i < n_spikes && i < 100; i++)
                if (!offscreen(floorspikes[i].x, floorspikes[i].y) && player_touches(floorspikes[i].x, floorspikes[i].y)) t.hp = 1;
            break;
        case A_SPIKES:
            for (i = 0; i < n_floorspikes && i < 200; i++)
                if (!offscreen(floorspikes2[i].x, floorspikes2[i].y) && player_touches(floorspikes2[i].x, floorspikes2[i].y)) t.hp = 1;
            break;
        case A_BRIDGE:
            if (bridge_present && (w & W_BRIDGE) != W_GONE) {
                if (player_in_span(bridge_x - 2, bridge_x + bridge_len, bridge_y - 0x20))
                    *wout = (*wout & ~W_BRIDGE) | (((w & W_BRIDGE) == W_BRIDGE2) ? W_GONE : W_BRIDGE1);
                else if ((w & W_BRIDGE) == W_BRIDGE1)
                    *wout = (*wout & ~W_BRIDGE) | W_BRIDGE2;
            }
            break;
#define GOT(a) ((a).present && !offscreen((a).x, (a).y) && player_touches((a).x, (a).y))
        case A_BOOTS: if (!(w & W_BOOTS) && GOT(boots_at)) *wout |= W_BOOTS; break;
        case A_CLAWS: if (!(w & W_CLAWS) && GOT(claws_at)) *wout |= W_CLAWS; break;
        case A_HAND: if (!(w & W_HAND) && GOT(hand_at)) *wout |= W_HAND; break;
        case A_CARD: if (!(w & (W_CARD | W_CARDUSE)) && GOT(card_at)) *wout |= W_CARD; break;
#undef GOT
        case A_KEYS:
            for (i = 0; i < n_keys && i < 15; i++) {
                int c = keys[i].s / 0xA0 - 1;
                if (c >= 0 && c < 4 && !(w & (W_KEY(c) | W_LOCK(c))) && !offscreen(keys[i].x, keys[i].y) && player_touches(keys[i].x, keys[i].y))
                    *wout |= W_KEY(c);
            }
            break;
        }
    if (reactor_present && !(w & W_REACTOR) && !player_hanging && can_shoot(reactor_x, reactor_y, 2))
        *wout |= W_REACTOR;
    if (boss_present && !player_hanging &&
        (can_shoot((int16_t)(boss_x + 1), boss_y, 2) || player_touches(boss_x + 1, boss_y)))
        *finish = 1;
    if (t.ht) t.hp = 0;                     /* (blinking: next frame drops it unfelt) */
    if (t.ht && !hazard_near(&t, *wout)) t.ht = 0;   /* (blinking only matters where he can be hit again) */
    get_globals(&t);
    /* act_walkway: a cell a frame from the frame the hand reader is used */
    if ((*wout & W_WALKWAY) && !(w & W_WALKWAY)) t.wk = 1;
    else if (t.wk) t.wk++;
    if (t.wk >= n_wcells[4] || t.wk >= 126) t.wk = 0;
    if (!on_map(&t) || forbidden(*wout)) ok = 0;
    restore_cols();
    if (s->wk && (w & W_WALKWAY))
        for (i = s->wk; i < n_wcells[4]; i++) put_cell(wcells[4][i].cell, wcells[4][i].v);
    if (ok) *out = t;
    return ok;
}

/* standing in a teleporter (after a step with UP): 1 A, 2 B, 0 neither */
static int at_teleporter(void)
{
    if (!tele_a_present || !tele_b_present) return 0;
    if (player_touches(tele_a_x + 2, tele_a_y)) return 1;
    if (player_touches(tele_b_x + 2, tele_b_y)) return 2;
    return 0;
}
/* the teleporter's countdown (8 frames, idle), then the jump to the other */
static int teleport(const St *s, uint32_t w, int from_a, St *out)
{
    St t = *s;
    int k, f;
    uint32_t w2;
    for (k = 0; k < 8; k++) if (!step(&t, w, 0, &t, &w2, &f)) return 0;
    set_globals(&t);
    if (from_a) { scroll_x = (int16_t)(tele_b_x - player_x + 2); scroll_y = (int16_t)(tele_b_y - 0x300); }
    else        { scroll_x = (int16_t)(tele_a_x - player_x + 2); scroll_y = (int16_t)(tele_a_y - 0x300); }
    recenter_y = 1;
    get_globals(&t);
    if (!on_map(&t)) return 0;
    *out = t;
    return 1;
}

/* ------------------------------------------------------------------ the search */
static const uint8_t inputs[] = {
    0, IN_L, IN_R, IN_J, IN_L | IN_J, IN_R | IN_J,
    IN_U, IN_U | IN_L, IN_U | IN_R, IN_U | IN_J, IN_U | IN_L | IN_J, IN_U | IN_R | IN_J,
    IN_D, IN_D | IN_L, IN_D | IN_R, IN_D | IN_J, IN_D | IN_L | IN_J, IN_D | IN_R | IN_J,
};

/* items fall until something solid is under them (as SETTLE) */
static void settle(int16_t x, int16_t *y)
{
    int k;
    for (k = 0; k < 90 && *y < 89 * 0x80 && !solid_at(x, *y + 0x80); k++) *y += 0x80;
}
static int has_id(const uint16_t *map, uint16_t id)
{
    int i;
    for (i = 0; i < ED_MAP_CELLS; i++) if (map[i] == id) return 1;
    return 0;
}
/* actors that fall while nothing solid is below them, and the boxed items */
static int falls(uint16_t id)
{
    switch (id) {
    case 0x300C: case 0x300D: case 0x300E: case 0x3010: case 0x303B: case 0x303C:
    case 0x300F: case 0x3012: case 0x3015: case 0x3052: case 0x3018: case 0x3051: case 0x301D: case 0x3050:
    case 0x301E: case 0x3054: case 0x301F: case 0x3053: case 0x3029: case 0x302D: case 0x3055: case 0x302E:
    case 0x3056: case 0x3037: case 0x3038: case 0x3039: case 0x303A: case 0x3057:
    case 0x3006: case 0x3008: case 0x3020: case 0x3033:
    case 0x305A: case 0x305B: case 0x305C: case 0x305D:
        return 1;
    }
    return 0;
}

/* ---- worlds: what has been done, and which world can stand in for which.
 * More done is never worse (a key only opens, boots only jump higher), so a
 * state already reached with more done makes the same state with less done
 * pointless: it is not searched again.  The bridge is the exception (once
 * gone, it is gone), so worlds differing in it never stand in for each other.
 * Worlds with more done are searched first, so they spread over the level
 * before the worlds they make pointless. */
#define MAX_WORLDS 250
static struct World {
    uint32_t w;
    int score;
    uint32_t *queue, qhead, qn, qcap;
    uint8_t better[MAX_WORLDS];              /* worlds that stand in for this one */
    int n_better;
} worlds[MAX_WORLDS];
static int n_worlds;

static int progress(uint32_t w, int part)
{
    switch (part) {
    case 0: return (w & W_BOOTS) != 0;
    case 1: return (w & W_CLAWS) != 0;
    case 2: return (w & W_HAND) != 0;
    case 3: return (w & W_CARDUSE) ? 2 : (w & W_CARD) ? 1 : 0;
    case 4: return (w & W_REACTOR) != 0;
    case 5: return (w & W_WALKWAY) != 0;
    default: { int c = part - 6; return (w & W_LOCK(c)) ? 2 : (w & W_KEY(c)) ? 1 : 0; }
    }
}
static int stands_in(uint32_t a, uint32_t b)         /* a for b */
{
    int p;
    if (a == b || (a & W_BRIDGE) != (b & W_BRIDGE)) return 0;
    for (p = 0; p < 10; p++) if (progress(a, p) < progress(b, p)) return 0;
    return 1;
}
static int world_of(uint32_t w)
{
    int i, p;
    for (i = 0; i < n_worlds; i++) if (worlds[i].w == w) return i;
    if (n_worlds == MAX_WORLDS) return -1;
    memset(&worlds[i], 0, sizeof worlds[i]);
    worlds[i].w = w;
    for (p = 0; p < 10; p++) worlds[i].score += progress(w, p);
    for (p = 0; p < n_worlds; p++) {
        if (stands_in(w, worlds[p].w)) worlds[p].better[worlds[p].n_better++] = (uint8_t)i;
        if (stands_in(worlds[p].w, w)) worlds[i].better[worlds[i].n_better++] = (uint8_t)p;
    }
    n_worlds++;
    return i;
}
static int enqueue(int wi, uint32_t n)
{
    struct World *W = &worlds[wi];
    if (W->qn == W->qcap) {
        uint32_t nc = W->qcap ? W->qcap * 2 : 4096;
        uint32_t *q = (uint32_t *)realloc(W->queue, (size_t)nc * sizeof *q);
        if (!q) return 0;
        W->queue = q; W->qcap = nc;
    }
    W->queue[W->qn++] = n;
    return 1;
}
/* a node already reached for s in a world standing in for world wi */
static uint32_t find_node(uint64_t s, uint32_t w)
{
    uint32_t h = (uint32_t)hkey(s, w) & table_mask;
    while (table[h]) {
        Node *n = &nodes[table[h] - 1];
        if (n->s == s && n->w == w) return table[h] - 1;
        h = (h + 1) & table_mask;
    }
    return UINT32_MAX;
}
/* The jump latch set (jump held last frame) only takes away: the same
 * state without it can do all it can (holding jump changes nothing more
 * once latched), so it stands in for it too. */
#define LATCH_BIT ((uint64_t)1 << 34)
static uint32_t stand_in(uint64_t s, int wi)
{
    int k;
    uint32_t m;
    if ((s & LATCH_BIT) && (m = find_node(s & ~LATCH_BIT, worlds[wi].w)) != UINT32_MAX) return m;
    for (k = 0; k < worlds[wi].n_better; k++) {
        uint32_t w = worlds[worlds[wi].better[k]].w;
        if ((m = find_node(s, w)) != UINT32_MAX) return m;
        if ((s & LATCH_BIT) && (m = find_node(s & ~LATCH_BIT, w)) != UINT32_MAX) return m;
    }
    return UINT32_MAX;
}

/* the search in progress */
static struct {
    int live, done, status, pass;
    int slot;
    uint16_t map[ED_MAP_CELLS];
    uint32_t w0;
    St s0;
    uint32_t expanded, requeued_at;
    int blasts, final;                  /* blocks were shot; the last search (with them gone) */
} R;

/* load the level for a slice (the editor uses the same game state between) */
static int loaded_at = -1;
static void reach_load(void)
{
    int k;
    ed_load(R.slot, R.map);
    ed_start_clamp();
    for (k = 0; k < ED_MAP_CELLS; k++) if (blasted[k]) put_cell(k, blasted_v[k]);
    boss_goal = !exit_present && boss_present;
    boots_at.present = has_id(R.map, 0x3006) || has_id(R.map, 0x305A); boots_at.x = boots_x; boots_at.y = boots_y;
    claws_at.present = has_id(R.map, 0x3008) || has_id(R.map, 0x305B); claws_at.x = claws_x; claws_at.y = claws_y;
    hand_at.present  = has_id(R.map, 0x3020) || has_id(R.map, 0x305C); hand_at.x = robohand_x; hand_at.y = robohand_y;
    card_at.present  = has_id(R.map, 0x3033) || has_id(R.map, 0x305D); card_at.x = card_x; card_at.y = card_y;
    settle(boots_at.x, &boots_at.y); settle(claws_at.x, &claws_at.y);
    settle(hand_at.x, &hand_at.y);   settle(card_at.x, &card_at.y);
    for (k = 0; k < n_elevators && k < 100; k++) elev_y0[k] = elevators[k].y;
    actor_order_from_level();
    has_blocks = 0;
    for (k = 0; k < ED_MAP_CELLS; k++) { base_map[k] = get_cell(k); if (base_map[k] == 0x1800) has_blocks = 1; }
    world_cells();
    loaded_at = ed_core_used;
}
/* ... unless nothing else has used it since */
static void reach_reload(void)
{
    if (loaded_at != ed_core_used) reach_load();
}

static void free_search(void)
{
    int i;
    free(nodes); nodes = NULL; n_nodes = cap_nodes = 0;
    free(table); table = NULL; table_mask = 0;
    free(succ); succ = NULL; n_succ = cap_succ = 0;
    free(succ_at); succ_at = NULL;
    for (i = 0; i < n_worlds; i++) free(worlds[i].queue);
    n_worlds = 0;
}
void ed_reach_end(void)
{
    free_search();
    R.live = 0;
}

/* (re)start the search from the start, with the blocks shot so far */
static int search_start(void)
{
    int fresh, wi;
    uint32_t n;
    free_search();
    reach_load();
    get_globals(&R.s0);
    R.s0.ei = 0xFF; R.s0.eh = 0; R.s0.wk = 0; R.s0.ht = 0; R.s0.hp = 0;
    wi = world_of(R.w0);
    n = node_get(pack_st(&R.s0), R.w0, &fresh);
    if (n == UINT32_MAX || wi < 0) { R.done = 1; R.status = ED_REACH_TOO_BIG; return 0; }
    nodes[n].parent = UINT32_MAX;
    nodes[n].world = (uint8_t)wi;
    enqueue(wi, n);
    R.expanded = 0;
    return 1;
}

int ed_reach_begin(int slot, const uint16_t *map, const EdReachOpts *opt)
{
    int k, start = 0;
    ed_reach_end();
    memset(&R, 0, sizeof R);
    memset(blasted, 0, sizeof blasted);
    memset(rocket_gone, 0, sizeof rocket_gone);
    n_pending = 0;
    R.slot = slot;
    memcpy(R.map, map, sizeof R.map);
    max_nodes = opt && opt->max_nodes ? (uint32_t)opt->max_nodes : 8000000u;
    for (k = 0; k < ED_MAP_CELLS; k++) if (map[k] == 0x3032) start = 1;
    R.live = 1;
    if (!start) { R.done = 1; R.status = ED_REACH_NO_START; return 0; }
    if (opt) {
        if (opt->boots) R.w0 |= W_BOOTS;
        if (opt->claws) R.w0 |= W_CLAWS;
        if (opt->robohand) R.w0 |= W_HAND;
    }
    return search_start();
}

/* a block shot away: in the map at once (as shot_blocked leaves it), and
 * the states next to it looked at again later */
static void change_cell(int cell, uint16_t v, int why)
{
    if (cell < 0 || cell >= ED_MAP_CELLS) return;
    blasted[cell] = (uint8_t)why;
    blasted_v[cell] = v;
    base_map[cell] = v;
    put_cell(cell, v);
    if (n_pending < ED_MAP_CELLS) pending[n_pending++] = (uint16_t)cell;
}
static void blast(int cell) { change_cell(cell, 0x17E0, 1); }   /* as shot_blocked leaves it */

/* a rocket shot (act_rockets): it takes its launch pad with it and knocks
 * a cell out of the ceiling it flies into */
static void launch(int i)
{
    Obj6 *r = &rockets[i];
    int col = (r->x >> 1) - 1, row = r->y >> 7, k;
    int16_t y = r->y;
    rocket_gone[i] = 1;
    change_cell((row + 1) * 128 + col, get_cell((row + 2) * 128 + col), 2);
    for (k = 0; k < 90; k++) {
        y -= 0x80;
        if (y - 0x200 < 0) return;
        if (solid_at(r->x, y - 0x200)) {
            int crow = (y >> 7) - 4;
            change_cell(crow * 128 + col, get_cell((crow + 1) * 128 + col), 2);
            return;
        }
    }
}
/* the states expanded next to blocks shot away since: once more */
static void requeue_near_blasts(void)
{
    static uint8_t near[ED_MAP_CELLS];
    uint32_t i;
    int k, dc, dr;
    memset(near, 0, sizeof near);
    for (k = 0; k < n_pending; k++)
        for (dr = -1; dr <= 1; dr++)
            for (dc = -1; dc <= 1; dc++) {
                int c = pending[k] % 128 + dc, r = pending[k] / 128 + dr;
                if (c >= 0 && c < 128 && r >= 0 && r < 90) near[r * 128 + c] = 1;
            }
    n_pending = 0;
    for (i = 0; i < n_nodes; i++) {
        St s;
        int r, c, c2, rr, hit = 0;
        if (!(nodes[i].flags & N_DONE)) continue;
        unpack(nodes[i].s, &s);
        r = s.sy + (s.py >> 4); c = (s.ax - 2) >> 1; c2 = ((s.ax - 2) & 1) ? c + 1 : c;
        for (rr = r - 2; rr <= r && !hit; rr++) {
            if (rr < 0 || rr >= 90) continue;
            if ((c >= 0 && c < 128 && near[rr * 128 + c]) || (c2 >= 0 && c2 < 128 && near[rr * 128 + c2])) hit = 1;
        }
        if (hit) { nodes[i].flags &= ~N_DONE; enqueue(nodes[i].world, i); }
    }
}

/* the breakable blocks shots from here would break (as shot_blocked: the
 * shot's row and the one below, either way, as far as the screen), shot
 * after shot until a wall that does not break */
static int shoot_blocks(const St *s)
{
    int srow = (((((s->py - 29) >> 4) << 7) + (s->sy << 7)) >> 7), dir, x, got = 0;
    if (srow < 0 || srow >= 89) return 0;
    {   /* rockets: a shot along its row, nothing solid between */
        int i;
        set_globals(s);
        for (i = 0; i < n_rockets && i < 100; i++)
            if (!rocket_gone[i] && rockets[i].s == 1 && can_shoot(rockets[i].x, rockets[i].y, 2)) { launch(i); got = 1; }
    }
    for (dir = -1; dir <= 1; dir += 2)
        for (x = s->ax + dir; x >= 2 && x <= 256 && abs(x - s->ax) <= 40; x += dir) {
            int c = (x >> 1) - 1, a = srow * 128 + c, b = a + 128;
            if (c < 0 || c >= 128) break;
            /* shot after shot from the same place: each breaks the next block */
            if (get_cell(a) == 0x1800) { blast(a); got = 1; }
            if (b < ED_MAP_CELLS && get_cell(b) == 0x1800) { blast(b); got = 1; }
            if (get_cell(a) > SOLID_TILE) break;
        }
    return got;
}

static void add_succ(uint32_t *list, int *nl, uint32_t m)
{
    int j;
    for (j = 0; j < *nl && list[j] != m; j++) ;
    if (j == *nl && *nl < 24) list[(*nl)++] = m;
}

/* the node for a state reached from ni with controls in: an existing one,
 * one standing in for it, or a new one (queued) */
static uint32_t reach_node(const St *t, uint32_t w2, uint32_t ni, int in)
{
    int fresh, wi = world_of(w2);
    uint64_t k = pack_st(t);
    uint32_t m;
    if (wi < 0) return UINT32_MAX;
    if ((!table || (n_nodes + 1) * 2 > table_mask + 1) && !grow_table()) return UINT32_MAX;
    {   /* found as it is? */
        uint32_t h = (uint32_t)hkey(k, w2) & table_mask;
        while (table[h]) {
            Node *n = &nodes[table[h] - 1];
            if (n->s == k && n->w == w2) return table[h] - 1;
            h = (h + 1) & table_mask;
        }
    }
    m = stand_in(k, wi);
    if (m != UINT32_MAX) return m;
    m = node_get(k, w2, &fresh);
    if (m == UINT32_MAX) return m;
    nodes[m].parent = ni; nodes[m].in = (uint8_t)in; nodes[m].world = (uint8_t)wi;
    if (!enqueue(wi, m)) return UINT32_MAX;
    return m;
}

/* The search uses the original game's activity window (offscreen()): Wide
 * and Immersive views only widen it, which only helps Duke. */
static int keep_act[4];
static void original_window(int on)
{
    if (on) {
        keep_act[0] = port_act_left; keep_act[1] = port_act_right; keep_act[2] = port_act_up; keep_act[3] = port_act_down;
        port_act_left = port_act_right = port_act_up = port_act_down = 0;
    } else {
        port_act_left = keep_act[0]; port_act_right = keep_act[1]; port_act_up = keep_act[2]; port_act_down = keep_act[3];
    }
}

/* expand up to budget states; 1 once the search is over */
static int reach_run(int budget, int *states);
int ed_reach_run(int budget, int *states)
{
    int r;
    original_window(1);
    r = reach_run(budget, states);
    original_window(0);
    return r;
}
static int reach_run(int budget, int *states)
{
    int cur = -1;
    if (!R.live) return 1;
    if (R.done) { if (states) *states = (int)n_nodes; return 1; }
    reach_reload();
    port_sim = 1;
    while (budget-- > 0) {
        uint32_t ni, w, list[24];
        int nl = 0, k;
        St s;
        /* the world with the most done that has states waiting */
        if (cur < 0 || worlds[cur].qhead == worlds[cur].qn || (budget & 1023) == 0) {
            int i;
            if (n_pending && (R.expanded - R.requeued_at > 250000)) { R.requeued_at = R.expanded; requeue_near_blasts(); }
            cur = -1;
            for (i = 0; i < n_worlds; i++)
                if (worlds[i].qhead < worlds[i].qn && (cur < 0 || worlds[i].score > worlds[cur].score)) cur = i;
            if (cur < 0 && n_pending) { R.requeued_at = R.expanded; requeue_near_blasts(); budget++; continue; }
            if (cur < 0) break;
        }
        ni = worlds[cur].queue[worlds[cur].qhead++];
        if (nodes[ni].flags & N_DONE) continue;          /* (queued twice) */
        nodes[ni].flags |= N_DONE;
        unpack(nodes[ni].s, &s);
        w = nodes[ni].w;
        succ_at[ni] = n_succ;
        R.expanded++;
        {   /* reached since in a world standing in for this one: nothing new here */
            uint32_t m = stand_in(nodes[ni].s, cur);
            if (m != UINT32_MAX) {
                if (succ_add(m)) nodes[ni].nsucc = 1;
                continue;
            }
        }
        world_map(w);
        if ((has_blocks || n_rockets) && !R.final && shoot_blocks(&s)) R.blasts = 1;   /* (before its own moves: they see the blocks gone) */
        {
            /* every control; most give the same state, so look each up once */
            uint64_t seen_k[sizeof inputs];
            uint32_t seen_w[sizeof inputs];
            int ns = 0, j;
            for (k = 0; k < (int)sizeof inputs; k++) {
                St t; uint32_t w2, m; int fin;
                uint64_t key;
                if (!step(&s, w, inputs[k], &t, &w2, &fin)) continue;
                if (fin) nodes[ni].flags |= N_FINISH;
                key = pack_st(&t);
                for (j = 0; j < ns && (seen_k[j] != key || seen_w[j] != w2); j++) ;
                if (j < ns) continue;
                seen_k[ns] = key; seen_w[ns] = w2; ns++;
                m = reach_node(&t, w2, ni, inputs[k]);
                if (m == UINT32_MAX) { R.status = ED_REACH_TOO_BIG; break; }
                if (m != ni) add_succ(list, &nl, m);
            }
        }
        /* a teleporter: UP while standing in it */
        if (s.grounded && tele_a_present && tele_b_present && R.status != ED_REACH_TOO_BIG) {
            St t, u; uint32_t w2; int fin, which = 0;
            if (step(&s, w, IN_U, &t, &w2, &fin)) { set_globals(&t); which = at_teleporter(); }
            if (which && t.grounded && teleport(&t, w2, which == 1, &u)) {
                uint32_t m = reach_node(&u, w2, ni, IN_U | IN_TELE);
                if (m == UINT32_MAX) R.status = ED_REACH_TOO_BIG;
                else add_succ(list, &nl, m);
            }
        }
        for (k = 0; k < nl; k++) if (!succ_add(list[k])) R.status = ED_REACH_TOO_BIG;
        nodes[ni].nsucc = (uint8_t)nl;
        if (R.status == ED_REACH_TOO_BIG) break;
    }
    if (states) *states = (int)n_nodes;
    if (R.status == ED_REACH_TOO_BIG) { port_sim = 0; world_map(0); R.done = 1; return 1; }
    if (budget >= 0) {                  /* nothing left to search */
        port_sim = 0;
        world_map(0);
        /* Blocks shot away during the search changed the map under states
         * reached before: search once more with them gone from the start,
         * so every step of the result (and the route) holds in one map. */
        if (R.blasts && !R.final) { R.final = 1; search_start(); return 0; }
        R.done = 1;
        return 1;
    }
    port_sim = 0;
    world_map(0);
    return 0;
}

static void mark_body(EdReach *o, const St *s, uint8_t bit)
{
    int r = s->sy + (s->py >> 4);
    int c = (s->ax - 2) >> 1, c2 = ((s->ax - 2) & 1) ? c + 1 : c, k;
    for (k = 1; k <= 2; k++) {
        int rr = r - k;
        if (rr < 0 || rr >= 90) continue;
        if (c >= 0 && c < 128) o->cell[rr * 128 + c] |= bit;
        if (c2 >= 0 && c2 < 128) o->cell[rr * 128 + c2] |= bit;
    }
}
/* the actor cells Duke touches from here (player_touches, inverted:
 * dy in (10h, 180h) is rows r-1 and r-2, x within one column) */
static uint8_t touch_grid[90][260];
static void mark_touch(const St *s)
{
    int r = s->sy + (s->py >> 4), x, y;
    for (y = r - 2; y <= r - 1; y++) {
        if (y < 0 || y >= 90) continue;
        for (x = s->ax - 1; x <= s->ax + 1; x++)
            if (x >= 0 && x < 260) touch_grid[y][x] = 1;
    }
}

/* (debugging) REACH_STATS=1: how many states differ only in one part */
static int cmp64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static void reach_stats(void)
{
    static const struct { const char *name; uint64_t drop; } v[] = {
        { "camera(py,sy,recenter)->absY", 0 }, { "facing", (uint64_t)1 << 35 }, { "latch", (uint64_t)1 << 34 },
        { "elevator", (uint64_t)0x3FFF << 37 }, { "jumpstep", (uint64_t)15 << 30 }, { "world", 0 },
        { "recenter", (uint64_t)1 << 36 }, { "hurt", (uint64_t)0x3F << 58 }, { "walkway", (uint64_t)0x7F << 51 },
    };
    uint32_t i, q;
    uint64_t *ks = (uint64_t *)malloc((size_t)n_nodes * 16);
    if (!ks) return;
    for (q = 0; q < sizeof v / sizeof v[0]; q++) {
        uint64_t uniq = 0;
        for (i = 0; i < n_nodes; i++) {
            uint64_t key = nodes[i].s, w = nodes[i].w;
            if (q == 0) { St t; unpack(key, &t); key &= ~(((uint64_t)0x1FF << 9) | ((uint64_t)0x7F << 18) | ((uint64_t)1 << 36)); key |= (uint64_t)((t.sy * 16 + t.py + 512) & 0xFFFF) << 9; }
            else if (q == 5) w = 0;
            else key &= ~v[q].drop;
            ks[i * 2] = key; ks[i * 2 + 1] = w;
        }
        for (i = 0; i < n_nodes; i++) ks[i] = ks[i * 2] * 0x9E3779B97F4A7C15ull ^ ks[i * 2 + 1] * 0xC2B2AE3D27D4EB4Full ^ (ks[i * 2] >> 31);
        qsort(ks, n_nodes, sizeof *ks, cmp64);
        for (i = 0; i < n_nodes; i++) if (!i || ks[i] != ks[i - 1]) uniq++;
        fprintf(stderr, "    without %-30s %10llu  (x%.2f)\n", v[q].name, (unsigned long long)uniq, (double)n_nodes / (double)uniq);
    }
    free(ks);
}

/* the result of a finished search */
static void reach_result(EdReach *out);
void ed_reach_result(EdReach *out)
{
    original_window(1);
    reach_result(out);
    original_window(0);
}
static void reach_result(EdReach *out)
{
    uint32_t i;
    int k;
    memset(out, 0, sizeof *out);
    if (!R.live) { out->status = ED_REACH_NO_START; return; }
    out->status = R.status;
    out->nodes = (int)n_nodes;
    out->edges = (int)n_succ;
    if (R.status == ED_REACH_NO_START) return;
    reach_load();
    out->goal = boss_goal ? 2 : exit_present ? 1 : 0;
    if (getenv("REACH_STATS")) reach_stats();

    /* which states can still reach the end: backwards from the finishing ones */
    {
        uint32_t *cnt = (uint32_t *)calloc((size_t)n_nodes + 1, sizeof *cnt), *rev, *stack, sp = 0, e;
        int any = 0;
        if (!cnt) return;
        for (i = 0; i < n_nodes; i++)
            for (e = succ_at[i]; e < succ_at[i] + nodes[i].nsucc; e++) cnt[succ[e] + 1]++;
        for (i = 0; i < n_nodes; i++) cnt[i + 1] += cnt[i];
        rev = (uint32_t *)malloc(((size_t)n_succ + 1) * sizeof *rev);
        stack = (uint32_t *)malloc(((size_t)n_nodes + 1) * sizeof *stack);
        if (rev && stack) {
            uint32_t *pos = cnt;          /* filled by bumping each start: shifted back after */
            for (i = 0; i < n_nodes; i++)
                for (e = succ_at[i]; e < succ_at[i] + nodes[i].nsucc; e++) rev[pos[succ[e]]++] = i;
            for (i = n_nodes; i > 0; i--) cnt[i] = cnt[i - 1];
            cnt[0] = 0;
            for (i = 0; i < n_nodes; i++)
                if (nodes[i].flags & N_FINISH) { nodes[i].flags |= N_GOOD; stack[sp++] = i; any = 1; }
            while (sp) {
                uint32_t v = stack[--sp];
                for (e = cnt[v]; e < cnt[v + 1]; e++) {
                    uint32_t u = rev[e];
                    if (!(nodes[u].flags & N_GOOD)) { nodes[u].flags |= N_GOOD; stack[sp++] = u; }
                }
            }
        }
        free(cnt); free(rev); free(stack);
        out->finish_ok = any;
    }

    /* the overlay (a search cut short marks no traps: it does not know) */
    memset(touch_grid, 0, sizeof touch_grid);
    for (i = 0; i < n_nodes; i++) {
        St s;
        unpack(nodes[i].s, &s);
        out->gates |= nodes[i].w;
        if (!out->finish_ok || R.status != ED_REACH_OK || (nodes[i].flags & N_GOOD)) mark_body(out, &s, ED_REACH_BODY);
        else mark_body(out, &s, ED_REACH_TRAPPED);
        mark_touch(&s);
    }
    for (k = 0; k < ED_MAP_CELLS; k++) {
        if ((out->cell[k] & ED_REACH_TRAPPED) && (out->cell[k] & ED_REACH_BODY)) out->cell[k] &= ~ED_REACH_TRAPPED;
        if (out->cell[k] & ED_REACH_TRAPPED) out->trap_cells++;
        if (out->cell[k] & ED_REACH_BODY) out->body_cells++;
    }
    for (k = 0; k < ED_MAP_CELLS; k++) {
        if (blasted[k] == 1) out->cell[k] |= ED_REACH_SHOT;
        if (blasted[k]) out->shot[k] = blasted_v[k] ? blasted_v[k] : 0x0001;   /* (0 would read as unchanged) */
    }
    for (k = 0; k < n_rockets && k < 100; k++)
        if (rocket_gone[k]) {
            int cell = (rockets[k].y >> 7) * 128 + (rockets[k].x >> 1) - 1;
            if (cell >= 0 && cell < ED_MAP_CELLS) out->cell[cell] |= ED_REACH_SHOT;
        }
    /* actors he can get to: where the game holds them (fallers where they land) */
    for (k = 0; k < ED_MAP_CELLS; k++) {
        uint16_t v = R.map[k];
        int16_t x = (int16_t)((k % 128) * 2 + 2), y = (int16_t)((k / 128) << 7);
        if (v < 0x3000 || v >= ED_ACTOR_END) continue;
        if (falls(v)) settle(x, &y);
        if ((y >> 7) < 90 && touch_grid[y >> 7][x]) out->cell[k] |= ED_REACH_TOUCH;
    }

    /* a route to the end: back along the parents, then played forward to
     * fill in the frames (a teleport is 9) and the controls */
    if (out->finish_ok) {
        uint32_t best = UINT32_MAX, n, len = 0, f;
        uint8_t *ins;
        int fire_last = 0, fin_in = -1;
        St s = R.s0;
        uint32_t w = R.w0;
        for (i = 0; i < n_nodes; i++) if (nodes[i].flags & N_FINISH) { best = i; break; }
        for (n = best; n != UINT32_MAX; n = nodes[n].parent) len++;
        ins = (uint8_t *)malloc(len + 1);
        if (!ins) return;
        k = (int)len - 1;
        for (n = best; nodes[n].parent != UINT32_MAX; n = nodes[n].parent) ins[k--] = nodes[n].in;
        port_sim = 1;
        world_map(w);
        for (f = 1; f <= len; f++) {
            int in, use, fin;
            St t; uint32_t w2;
            if (f < len) in = ins[f];
            else {                        /* the finishing frame: the first input that finishes */
                for (k = 0; k < (int)sizeof inputs && fin_in < 0; k++)
                    if (step(&s, w, inputs[k], &t, &w2, &fin) && fin) fin_in = inputs[k];
                if (fin_in < 0) break;
                in = fin_in;
            }
            if (getenv("ED_TRACE") && out->inputs_n >= atoi(getenv("ED_TRACE")) && out->inputs_n < atoi(getenv("ED_TRACE")) + 20)
                fprintf(stderr, "search %4d: ax %d py %d sy %d rc %d g %d f %d h %d j %d c %d ht %d hurt %d\n", out->inputs_n, s.ax, s.py,
                        s.sy, s.recenter, s.grounded, s.falling, s.hanging, s.jumpstep, s.climb, s.ht, s.hp);
            use = in & ~IN_TELE;
            /* fire every other frame when it changes nothing: played back, it
             * opens boxes and clears enemies on the way */
            if (!fire_last) {
                St t2; uint32_t w3; int f2;
                if (step(&s, w, use, &t, &w2, &fin) && step(&s, w, use | IN_F, &t2, &w3, &f2) &&
                    pack_st(&t) == pack_st(&t2) && w2 == w3)
                    use |= IN_F;
            }
            fire_last = (use & IN_F) != 0;
            if (out->inputs_n < ED_ROUTE) out->inputs[out->inputs_n++] = (uint8_t)use;
            if (in & IN_TELE) {
                St u;
                int which = 0;
                if (step(&s, w, use, &t, &w2, &fin)) { set_globals(&t); which = at_teleporter(); }
                if (!which || !teleport(&t, w2, which == 1, &u)) break;
                if (out->route_n < ED_ROUTE) {        /* the frame UP was pressed */
                    out->route[out->route_n * 2] = (int16_t)((t.ax - 2) * 8 + 8);
                    out->route[out->route_n * 2 + 1] = (int16_t)(t.sy * 16 + t.py - 16);
                    out->route_n++;
                }
                for (k = 0; k < 8; k++) {            /* the countdown, standing there */
                    St v; uint32_t w4; int f4;
                    if (out->inputs_n < ED_ROUTE) out->inputs[out->inputs_n++] = 0;
                    if (k < 7 && step(&t, w2, 0, &v, &w4, &f4)) t = v;
                    if (k < 7 && out->route_n < ED_ROUTE) {
                        out->route[out->route_n * 2] = (int16_t)((t.ax - 2) * 8 + 8);
                        out->route[out->route_n * 2 + 1] = (int16_t)(t.sy * 16 + t.py - 16);
                        out->route_n++;
                    }
                }
                s = u; w = w2;
                fire_last = 0;
            } else {
                if (!step(&s, w, use, &t, &w2, &fin)) break;
                s = t; w = w2;
            }
            world_map(w);
            if (out->route_n < ED_ROUTE) {
                out->route[out->route_n * 2] = (int16_t)((s.ax - 2) * 8 + 8);
                out->route[out->route_n * 2 + 1] = (int16_t)(s.sy * 16 + s.py - 16);
                out->route_n++;
            }
        }
        /* the exit door takes a few frames to open and close behind him */
        for (k = 0; k < 40 && out->inputs_n < ED_ROUTE; k++) out->inputs[out->inputs_n++] = 0;
        port_sim = 0;
        world_map(0);
        free(ins);
    }
}

/* all at once (tests) */
int ed_reach(int slot, const uint16_t *map, const EdReachOpts *opt, EdReach *out)
{
    if (ed_reach_begin(slot, map, opt))
        while (!ed_reach_run(1 << 30, NULL)) ;
    ed_reach_result(out);
    ed_reach_end();
    return out->status == ED_REACH_OK;
}
