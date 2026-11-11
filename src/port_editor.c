/*
 * port_editor.c - what the Shrapnel level editor needs from the game.
 *
 * Everything here works on the game's own data, loaded by the game's own
 * code, so the editor shows levels exactly as the game would:
 *
 *   ed_boot()        load the episode's graphics (after exe_load), as the
 *                    game does at start-up, and keep that state;
 *   ed_load()        load a level slot's backdrops (and spawn its actors, as
 *                    the game does, which the editor then ignores);
 *   ed_tile()        a 16x16 tile from the tile cache, as palette indices;
 *   ed_backdrop()    backdrop A or B, 13 x 10 tiles;
 *   ed_sprite()      a 16x16 masked sprite;
 *   ed_actor_look()  how an actor looks when its level starts: the game is
 *                    run for a frame on a small test map holding just that
 *                    actor, and everything it draws is recorded (through the
 *                    smooth-motion recorder's spy, see dl_record).
 *
 * The game can busy-wait (SPIN_WHILE) for things an editor never supplies,
 * such as a key press after a hint.  While an actor is being looked at, the
 * platform layer counts those waits (ed_spin) and, past a limit, abandons the
 * look instead of hanging.
 */
#include "dn1.h"
#include "port.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_globals_from_exe(void);
void load_graphics(void);
void player_update(void);
void draw_mirrors(void);
void act_conveyors(void);
void inventory_add(const uint8_t *gfx);
void inventory_draw_card(void);

static PortState *boot_state;
static jmp_buf look_abort;
static int looking;
static long spins;
int ed_core_used;                        /* bumped whenever the game's state is reloaded or played */
static int play_on, play_slot;           /* a play-test (ed_play) */
static long play_spins;

int ed_boot(void)
{
    extern uint8_t *heap_arena;
    ed_core_used++;
    free(heap_arena);                    /* a second episode: start over */
    heap_arena = NULL;
    port_state_free(boot_state);
    boot_state = NULL;
    init_globals_from_exe();
    port_init_game();                    /* as the game starts: keys, graphics, sounds, screens */
    boot_state = port_state_new();
    if (!boot_state) return 0;
    port_state_save(boot_state);
    return 1;
}

/* called by the editor's pf_service: a busy-wait is going on */
void ed_spin(void)
{
    if (looking && ++spins > 20000) longjmp(look_abort, 1);
    if (play_on && ed_play_unseen && ++play_spins > 400000) ed_play_stop(ED_END_STUCK);   /* waiting for a key */
}

int ed_read_map(const char *name, uint16_t map[ED_MAP_CELLS])
{
    uint8_t b[ED_MAP_CELLS * 2];
    FILE *f = fopen(game_path(name), "rb");
    size_t n;
    int i;
    if (!f) return 0;
    n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != sizeof b) return 0;
    for (i = 0; i < ED_MAP_CELLS; i++) map[i] = (uint16_t)(b[i * 2] | b[i * 2 + 1] << 8);
    return 1;
}

static void map_to_override(const uint16_t *map, uint8_t *b)
{
    int i;
    for (i = 0; i < ED_MAP_CELLS; i++) { b[i * 2] = (uint8_t)map[i]; b[i * 2 + 1] = (uint8_t)(map[i] >> 8); }
}

/* Which of the game's actor tables an actor ID is kept in (0: none, a single
 * one), and how many entries that table has.  IDs that share a table share
 * its limit (e.g. the four key colours). */
#define NA(a) ((int)(sizeof (a) / sizeof (a)[0]))
int ed_actor_limit(uint16_t id, int *cap)
{
    int t = 0, n = 0;
    switch (id) {
    case 0x3000: t = 1;  n = NA(greyboxes); break;
    case 0x3001: t = 2;  n = NA(elevators); break;
    case 0x3002: case 0x3004: t = 3; n = NA(conveyors); break;
    case 0x3007: t = 4;  n = NA(rockets); break;
    case 0x3009: t = 5;  n = NA(flamers_r); break;
    case 0x300A: t = 6;  n = NA(flamers_l); break;
    case 0x300B: t = 7;  n = NA(floatbots); break;
    case 0x300C: t = 8;  n = NA(hopbots); break;
    case 0x300D: t = 9;  n = NA(wheelbots); break;
    case 0x300E: t = 10; n = NA(firewheels); break;
    case 0x300F: t = 11; n = NA(guns); break;
    case 0x3010: t = 12; n = NA(minibots); break;
    case 0x3012: t = 13; n = NA(dynamite); break;
    case 0x3014: t = 14; n = NA(mirrors); break;
    case 0x3015: case 0x3052: t = 15; n = NA(soda); break;
    case 0x3016: case 0x3017: t = 16; n = NA(biters); break;
    case 0x3018: case 0x3051: t = 17; n = NA(turkeys); break;
    case 0x301A: t = 18; n = NA(ffields_h); break;
    case 0x301B: case 0x301C: t = 19; n = NA(fans); break;
    case 0x301D: case 0x3050: t = 20; n = NA(footballs); break;
    case 0x301E: case 0x3054: t = 21; n = NA(joysticks); break;
    case 0x301F: case 0x3053: t = 22; n = NA(floppies); break;
    case 0x3021: t = 23; n = NA(ffields_v) / 2; break;          /* {x, y} pairs */
    case 0x3022: t = 24; n = NA(helis); break;
    case 0x3023: t = 25; n = NA(balloons); break;
    case 0x3024: t = 26; n = NA(cameras); break;
    case 0x3025: case 0x3026: case 0x3027: case 0x3028:
    case 0x303D: case 0x303E: case 0x303F: t = 27; n = NA(decor); break;
    case 0x3029: t = 28; n = NA(molecules); break;
    case 0x302A: t = 29; n = NA(signs); break;
    case 0x302C: t = 30; n = NA(floorspikes); break;
    case 0x302D: case 0x3055: t = 31; n = NA(flags); break;
    case 0x302E: case 0x3056: t = 32; n = NA(radios); break;
    case 0x3031: t = 33; n = NA(bombs); break;
    case 0x3037: case 0x3038: case 0x3039: case 0x303A: t = 34; n = NA(letters); break;
    case 0x303B: t = 35; n = NA(rabbits); break;
    case 0x303C: t = 36; n = NA(firemonsters); break;
    case 0x3044: case 0x3045: case 0x3046: case 0x3047: t = 37; n = NA(keys); break;
    case 0x3048: case 0x3049: case 0x304A: case 0x304B: t = 38; n = NA(locks); break;
    case 0x304C: case 0x304D: case 0x304E: case 0x304F: t = 39; n = NA(keydoors); break;
    case 0x3057: t = 40; n = NA(static_bombs); break;
    case 0x3058: case 0x3059: t = 41; n = NA(floorspikes2); break;
    }
    if (cap) *cap = n;
    return t;
}

/* What the editor hands the game's loader.  A conveyor start with no end
 * marker after it makes the loader search forever, and more of a kind than
 * the game's table holds would write past it (the level checks report
 * both): the start is left out, and only the first ones of a kind loaded. */
static void make_safe(uint16_t *safe)
{
    int i, j, count[ED_N_TABLES] = { 0 };
    for (i = 0; i < ED_MAP_CELLS; i++) {
        int cap, t = ed_actor_limit(safe[i], &cap);
        if (safe[i] == 0x3002 || safe[i] == 0x3004) {
            for (j = i + 1; j < ED_MAP_CELLS && safe[j] != safe[i] + 1; j++) ;
            if (j == ED_MAP_CELLS) { safe[i] = 0; continue; }
        }
        if (t && ++count[t] > cap) safe[i] = 0;
    }
}

/* the backdrops of a slot (hallways keep the level before's, as in the game) */
void ed_load(int slot, const uint16_t *map)
{
    static uint8_t b[ED_MAP_CELLS * 2];
    int lv = slot & ~1;
    ed_core_used++;
    port_state_load(boot_state);
    level_index = (int16_t)lv;
    if (map) {
        static uint16_t safe[ED_MAP_CELLS];
        memcpy(safe, map, sizeof safe);
        make_safe(safe);
        map_to_override(safe, b);
        port_map_override = b;
    }
    load_level(ds_farstr((uint16_t)(0x3A4 + lv * 4)));
    port_map_override = NULL;
    level_index = (int16_t)slot;
    /* Conveyor belts are not in the map: every frame of play, the conveyors
     * write their belt tiles from start to end.  Do that once, so the map
     * shows them as play does (at their first frame). */
    conveyor_anim = 3;                   /* stepped first: 0 */
    act_conveyors();
}

/* the map as the game holds it once the level has loaded (after ed_load):
 * actor markers replaced by what the loader puts in their place */
void ed_map_after(uint16_t out[ED_MAP_CELLS])
{
    int i;
    for (i = 0; i < ED_MAP_CELLS; i++) out[i] = (uint16_t)(mapbuf[i * 2] | mapbuf[i * 2 + 1] << 8);
}

/* the play window as the game draws it (draw_world, after ed_load) at a
 * scroll position and animation phase (0, 20h, 40h, 60h): a 320 x 200 page */
void ed_game_view(int sx, int sy, int anim, uint8_t out[320 * 200])
{
    int16_t keep_x = scroll_x, keep_y = scroll_y;
    scroll_x = (int16_t)sx;
    scroll_y = (int16_t)sy;
    anim_tick = (uint16_t)((anim - 0x20) & 0x7F);   /* draw_world steps it first */
    set_draw_page(1);
    draw_world();
    ega_present_indexed(out, 1);
    set_draw_page(0);
    scroll_x = keep_x;
    scroll_y = keep_y;
}

void ed_tile(uint16_t t, uint8_t out[256])
{
    int x, y;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            uint16_t o = (uint16_t)(0x4000 + t + y * 2 + (x >> 3));
            int s = 7 - (x & 7);
            out[y * 16 + x] = (uint8_t)(((ega_planes[0][o] >> s) & 1) | (((ega_planes[1][o] >> s) & 1) << 1) |
                                        (((ega_planes[2][o] >> s) & 1) << 2) | (((ega_planes[3][o] >> s) & 1) << 3));
        }
}

void ed_backdrop(int which, uint8_t *out)
{
    uint8_t t[256];
    int tx, ty, y;
    for (ty = 0; ty < 10; ty++)
        for (tx = 0; tx < 13; tx++) {
            ed_tile((uint16_t)((which ? 0x8100 : 0x4000) + ((ty * 13 + tx) << 5)), t);
            for (y = 0; y < 16; y++) memcpy(out + (ty * 16 + y) * ED_BD_W + tx * 16, t + y * 16, 16);
        }
}

/* the backdrop files of a level slot (even): which = 0 (A) or 1 (B) */
const char *ed_stock_drop(int slot, int which)
{
    if (slot < 0 || slot >= 20 || which < 0 || which > 1) return NULL;
    return ds_farstr((uint16_t)(0x0B47 + ((slot & ~1) + which) * 4));
}

/* a slot's stock note (the hallway monitor's, or the Dr. Proton screen's):
 * line k (0..3) of the episode's text, or NULL */
const char *ed_stock_note(int slot, int k)
{
    if (slot < 0 || slot >= 20 || k < 0 || k > 3) return NULL;
    return ds_farstr((uint16_t)(0xBA1 + (slot * 4 + k) * 4));
}

void ui_box(int x, int row, int h);       /* ui.c */
void ui_text(const char *s, int x, int row);
/* the note box as the game draws it (show_level_note): ED_NOTE_W x ED_NOTE_H */
void ed_note_view(const char *const lines[4], uint8_t *out)
{
    static uint8_t page[320 * 200];
    int k, y;
    int16_t keep = draw_page;
    draw_page = 1;                       /* (ui_box: x == 2 draws on the game's draw page) */
    set_draw_page(1);
    ui_box(2, 1, 3);
    for (k = 0; k < 4; k++)
        if (lines[k] && lines[k][0]) {
            char b[ED_NOTE_CHARS + 1];
            snprintf(b, sizeof b, "%s", lines[k]);
            ui_text(b, 3, 3 + k);
        }
    ui_text(ds_str(0x08AE), 3, 8);
    ega_present_indexed(page, 1);
    draw_page = keep;
    set_draw_page(0);
    for (y = 0; y < ED_NOTE_H; y++) memcpy(out + y * ED_NOTE_W, page + (16 + y) * 320 + 16, ED_NOTE_W);
}

/* 5 bytes per 8 pixels: mask (1 = drawn), then planes 0..3.  Transparent = 255 */
void ed_sprite(const uint8_t *src, uint8_t out[256])
{
    int r, b, bit;
    for (r = 0; r < 16; r++)
        for (b = 0; b < 2; b++) {
            const uint8_t *g = src + (r * 2 + b) * 5;
            for (bit = 0; bit < 8; bit++) {
                int s = 7 - bit;
                out[r * 16 + b * 8 + bit] = !((g[0] >> s) & 1) ? 255 :
                    (uint8_t)(((g[1] >> s) & 1) | (((g[2] >> s) & 1) << 1) | (((g[3] >> s) & 1) << 2) | (((g[4] >> s) & 1) << 3));
            }
        }
}

/* the palette play uses: the EGA default (the registers themselves may be
 * mid-fade, and are black right after load_graphics) */
void ed_palette(uint32_t rgb[16])
{
    static const uint8_t def[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    int i;
    for (i = 0; i < 16; i++) rgb[i] = ega_rgb(def[i]);
}

/* ---- how an actor looks */

static EdPiece *look_out;
static int look_max, look_n, look_has_duke;

static void spy(int type, const uint8_t *src, uint16_t tile, int col, int line)
{
    EdPiece *p;
    if (type != DL_SPRITE && type != DL_TILE) return;
    if (type == DL_SPRITE && src >= man_gfx && src < man_gfx + 4 * 0x1E00) { look_has_duke = 1; if (!(look_max & ED_LOOK_DUKE)) return; }
    if (look_n >= (look_max & 0xFFFF)) return;
    p = &look_out[look_n++];
    p->src = type == DL_SPRITE ? src : NULL;
    p->tile = tile;
    p->dx = col * 8;
    p->dy = line;
}

/* one game frame, in the play loop's order (loop.c), without input */
static void one_frame(void)
{
    int si;
    player_update();
    for (si = 0; si < n_actor_fns; si++) actor_fns[si]();
    draw_mirrors();
}

/* Run the game on a small test room holding just this actor, recording what
 * it draws.  shoot = 0: the first frame anything is drawn (how it looks).
 * shoot = 1: draw a frame, then put one of Duke's shots on the actor and keep
 * going until something new is drawn: what a crate holds. */
static int look_run(uint16_t id, EdPiece *out, int max, uint16_t *cell_after, int shoot)
{
    static uint16_t m[ED_MAP_CELLS];
    static uint8_t b[ED_MAP_CELLS * 2];
    static PortState *keep;
    static const uint8_t *before[64];
    const int c = 64, r = 45;
    ed_core_used++;
    int i, frame, c0, r0, ok = 1, n_before = 0;

    if (!keep && !(keep = port_state_new())) return -1;
    port_state_save(keep);
    port_state_load(boot_state);

    /* a small room: the actor on a floor, with a different solid tile on each
     * side, so the cell it copies at load time shows which neighbour it was */
    memset(m, 0, sizeof m);
    for (i = c - 8; i <= c + 8; i++) m[(r + 1) * 128 + i] = 0x2060;
    m[r * 128 + c - 1] = 0x2000;
    m[r * 128 + c + 1] = 0x2020;
    m[(r - 1) * 128 + c] = 0x2040;
    if (id == 0x3002 || id == 0x3004) m[r * 128 + c + 3] = (uint16_t)(id + 1);   /* a conveyor's end */
    if (id == 0x3036) m[r * 128 + c + 3] = 0x2FE0;                               /* a walkway's stop */
    m[r * 128 + c] = id;

    map_to_override(m, b);
    port_map_override = b;
    level_index = 2;                     /* not 0: some things (the Dr. Proton screen) never show there */
    load_level("");
    port_map_override = NULL;
    if (cell_after) *cell_after = MAP16(r * 256 + c * 2);
    if (id != 0x3032) {                  /* put the actor at view tile (6,4); the start sets its own view */
        scroll_x = (int16_t)((c - 6) * 2);
        scroll_y = (int16_t)((r - 4) << 7);
    }
    c0 = scroll_x >> 1;
    r0 = scroll_y >> 7;
    hint_5a11 = hint_5c6b = hint_5554 = hint_637e = hint_5ec8 = hint_555a = 0;   /* no hints */
    hint_555c = hint_5553 = hint_555b = hint_68f7 = hint_5555 = hint_5c6a = 0;

    look_out = out; look_max = max; look_n = 0; look_has_duke = 0;
    dl_spy = spy;
    dl_capturing = 1;
    looking = 1; spins = 0;
    if (setjmp(look_abort)) ok = 0;
    else if (!shoot)
        for (frame = 0; frame < 80; frame++) {
            look_n = 0;
            one_frame();
            if (look_n) break;           /* some only appear later (flamethrowers: mid-cycle) */
        }
    else {
        look_n = 0;
        one_frame();                     /* the crate itself */
        for (i = 0; i < look_n && n_before < 64; i++) before[n_before++] = out[i].src;
        if (max_shots < 1) max_shots = 1;
        shots[0].x = (int16_t)(c * 2 + 2);   /* a shot right on it (as the loader places it) */
        shots[0].y = (int16_t)(r << 7);
        shots[0].s = 1;
        for (frame = 0; frame < 40; frame++) {
            int k, n = 0;
            look_n = 0;
            one_frame();
            for (i = 0; i < look_n; i++) {   /* keep only what is new */
                for (k = 0; k < n_before && out[i].src != before[k]; k++) ;
                if (k == n_before && out[i].src) out[n++] = out[i];
            }
            look_n = n;
            if (n) break;
        }
    }
    looking = 0;
    dl_spy = NULL;
    dl_capturing = 0;

    for (i = 0; i < look_n; i++) {       /* screen position -> offset from the actor's cell */
        out[i].dx -= 16 + 16 * (c - c0);
        out[i].dy -= 16 + 16 * (r - r0);
    }
    port_state_load(keep);
    return ok ? look_n : -1;
}

int ed_actor_look(uint16_t id, EdPiece *out, int max, uint16_t *cell_after)
{
    return look_run(id, out, max, cell_after, 0);
}

/* what an actor turns into when shot (a crate: its contents), or 0 pieces */
int ed_actor_contents(uint16_t id, EdPiece *out, int max)
{
    return look_run(id, out, max, NULL, 1);
}

/* ---- play-testing: the real game, on the editor's map */
const char *ed_play_message;
int ed_play_max_frames;
static jmp_buf play_jb;
static EdPlay *play_out;
const uint8_t *ed_play_feed;
int ed_play_feed_n;
int ed_play_unseen;

/* just before Duke moves: a replay's controls, and the controls recorded */
static void play_input(void)
{
    EdPlay *o = play_out;
    int f;
    if (!play_on || !o) return;
    f = o->inputs_n;
    if (getenv("ED_TRACE") && f >= atoi(getenv("ED_TRACE")) && f < atoi(getenv("ED_TRACE")) + 20)   /* (debugging) */
        fprintf(stderr, "game  %4d: ax %d py %d sy %d rc %d g %d f %d h %d j %d c %d ht %d hurt %d card %d/%d\n", f, player_x + scroll_x, player_y,
                scroll_y >> 7, recenter_y, player_grounded, player_falling, player_hanging, player_jumpstep, player_climb, hurt_timer, player_hurt, have_card, cardreader_present);
    if (ed_play_feed) {
        uint8_t b;
        if (f >= ed_play_feed_n) ed_play_stop(ED_END_FEED);
        b = ed_play_feed[f];
        in_left = (b & 1) != 0; in_right = (b & 2) != 0; in_up = (b & 4) != 0;
        in_down = (b & 8) != 0; in_jump = (b & 16) != 0; in_fire = (b & 32) != 0;
    }
    if (o->inputs_n < ED_INPUTS)
        o->inputs[o->inputs_n++] = (uint8_t)((in_left ? 1 : 0) | (in_right ? 2 : 0) | (in_up ? 4 : 0) |
                                             (in_down ? 8 : 0) | (in_jump ? 16 : 0) | (in_fire ? 32 : 0));
    else if (ed_play_feed) ed_play_stop(ED_END_FEED);
    play_spins = 0;
}

void ed_play_note(const char *what) { ed_play_message = what; }
void ed_play_stop(int why) { if (play_on) longjmp(play_jb, why); }

/* where Duke is, in level pixels (actor units: x = column * 2 + 2) */
static void duke_at(int *x, int *y)
{
    *x = (player_x + scroll_x - 2) * 8 + 8;
    *y = (scroll_y >> 7) * 16 + player_y - 16;
}

void ed_play_frame(void)
{
    EdPlay *o = play_out;
    int x, y;
    if (!play_on) return;
    if (level_index != play_slot) ed_play_stop(ED_END_EXIT);   /* the exit: the game moved on */
    if (game_over) ed_play_stop(ED_END_BOSS);
    if (health == 0 && death_timer >= 0x1C) ed_play_stop(ED_END_DIED);   /* before "You died" waits for a key */
    if (ed_play_max_frames && o->frames >= ed_play_max_frames) ed_play_stop(ED_STOP_USER);   /* (test aid) */
    duke_at(&x, &y);
    o->x = x;
    o->y = y;
    o->frames++;
    if (o->trail_n < ED_TRAIL) { o->trail[o->trail_n * 2] = (int16_t)x; o->trail[o->trail_n * 2 + 1] = (int16_t)y; o->trail_n++; }
}

/* a start near the map's edge: keep the camera on the map */
void ed_start_clamp(void)
{
    if (scroll_x < 0) { player_x += scroll_x; scroll_x = 0; }
    if (scroll_x > (128 - 13) * 2) { player_x += scroll_x - (128 - 13) * 2; scroll_x = (128 - 13) * 2; }
    if (scroll_y < 0) { player_y += (scroll_y >> 7) * 16; scroll_y = 0; }
    if (scroll_y > (90 - 10) << 7) { player_y += ((scroll_y - ((90 - 10) << 7)) >> 7) * 16; scroll_y = (90 - 10) << 7; }
}

void ed_play(int slot, const uint16_t *map, int c, int r, const EdLoadout *lo, EdPlay *out)
{
    static uint16_t m[ED_MAP_CELLS];
    static uint8_t b[ED_MAP_CELLS * 2];
    int why, i, here = -1;
    uint16_t here_was = 0;
    uint8_t keep_qs = cfg.key_qsave, keep_ql = cfg.key_qload;

    memcpy(m, map, sizeof m);
    make_safe(m);
    if (c >= 0 && r >= 0 && c < 128 && r < 90) {   /* "play here": the start moves to (c, r) */
        for (i = 0; i < ED_MAP_CELLS; i++)
            if (m[i] == 0x3032) m[i] = (i % 128 && (m[i - 1] < 0x3000 || m[i - 1] >= ED_ACTOR_END)) ? m[i - 1] : 0;
        here = r * 128 + c;
        here_was = m[here] >= 0x3000 && m[here] < ED_ACTOR_END ? 0 : m[here];
        m[here] = 0x3032;
    }
    memset(out, 0, sizeof *out);
    ed_core_used++;
    port_state_load(boot_state);
    ed_play_message = NULL;
    port_god = lo ? lo->god : 0;
    cfg.key_qsave = cfg.key_qload = 0;   /* a quicksave would overwrite the player's own */
    demo_play = 0; demo_record = 0; restoring_game = 0; port_resume_qs = 0; quit_flag = 0;
    port_start_level = slot;
    map_to_override(m, b);
    port_map_override = b;
    play_slot = slot;
    play_out = out;
    play_spins = 0;
    port_frame_input = play_input;
    port_no_disk = 1;                    /* no saved games, scores, settings or demos */
    why = setjmp(play_jb);
    if (!why) {
        play_on = 1;
        new_game();                      /* as the game starts a level: Get ready, the level loaded */
        if (ed_play_feed) {              /* a replay: no hint waits for a key (they change nothing else) */
            hint_5a11 = hint_5c6b = hint_5554 = hint_637e = hint_5ec8 = hint_555a = 0;
            hint_555c = hint_5553 = hint_555b = hint_68f7 = hint_5555 = hint_5c6a = 0;
        }
        port_map_override = NULL;
        if (here >= 0 && here_was) {     /* the loader filled the start's cell from its left-hand
                                            neighbour: give it back what the editor shows there */
            mapbuf[here * 2] = (uint8_t)here_was;
            mapbuf[here * 2 + 1] = (uint8_t)(here_was >> 8);
        }
        if (lo) {                        /* what Duke carries */
            if (lo->boots)    { have_boots = 1;    inventory_add(object_gfx + 0x640); }
            if (lo->claws)    { have_claws = 1;    inventory_add(object_gfx + 0xB40); }
            if (lo->robohand) { have_robohand = 1; inventory_add(object_gfx + 0x2760); }
            if (lo->card)     { have_card = 1;     inventory_draw_card(); }
            have_key_r = (lo->keys & 1) != 0; have_key_g = (lo->keys & 2) != 0;
            have_key_b = (lo->keys & 4) != 0; have_key_p = (lo->keys & 8) != 0;
            draw_inventory();
            max_shots = (int16_t)(lo->firepower < 1 ? 1 : lo->firepower > 4 ? 4 : lo->firepower);
            draw_firepower();
            health = (int16_t)(lo->health < 1 ? 1 : lo->health > 9 ? 9 : lo->health);
            draw_health();
        }
        ed_start_clamp();
        port_in_play = 1;
        play_level_loop();               /* returns if the player quits (Esc) */
        why = ED_END_QUIT;
    }
    play_on = 0;
    port_frame_input = NULL;
    port_no_disk = 0;
    port_in_play = 0;
    port_map_override = NULL;
    port_god = 0;
    cfg.key_qsave = keep_qs;
    cfg.key_qload = keep_ql;
    out->end = why;
}
