/*
 * tools.cpp - the editing operations: painting, lines, rectangles, fills,
 * selections and the clipboard.  They work on a bare map, so the self-test
 * can drive them exactly as the interface does.
 */
#include "shrapnel.h"
#include <stdlib.h>

/* ---- locks: a locked kind of cell is never overwritten */
bool cell_locked(uint16_t v, int locks)
{
    switch (cell_kind(v)) {
    case CK_SOLID:                      return (locks & LOCK_SOLID) != 0;
    case CK_BACKGROUND: case CK_ANIMATED: return (locks & LOCK_BACKGROUND) != 0;
    case CK_BACKDROP_A: case CK_BACKDROP_B: return (locks & LOCK_BACKDROP) != 0;
    case CK_ACTOR:                      return (locks & LOCK_ACTORS) != 0;
    default:                            return false;
    }
}

static bool behind_tile(uint16_t v)             /* a tile that can be what an actor stands in front of */
{
    CellKind k = cell_kind(v);
    return k == CK_BACKGROUND || k == CK_ANIMATED || k == CK_BACKDROP_A || k == CK_BACKDROP_B;
}

/* The loader fills an actor's cell from one neighbour (actor_fill); through a row of
 * actors filled to the left or from above, the first cell that is no actor.  Actors
 * to the right or below are not filled yet when the loader reads them, and a few
 * actors get a fixed tile: none then. */
int backing_source(const uint16_t *map, int i)
{
    for (int hops = 0; hops < 64; hops++) {
        uint16_t v = map[i];
        if (cell_kind(v) != CK_ACTOR) return i;
        int c = i % MAP_W, r = i / MAP_W;
        Fill f = actor_fill(v);
        int nc = c + (f == F_LEFT ? -1 : f == F_RIGHT ? 1 : 0), nr = r + (f == F_ABOVE ? -1 : f == F_BELOW ? 1 : 0);
        if (f > F_BELOW || nc < 0 || nc >= MAP_W || nr < 0 || nr >= MAP_H) return -1;
        int j = nr * MAP_W + nc;
        if (hops > 0 && (f == F_RIGHT || f == F_BELOW)) return -1;
        if ((f == F_RIGHT || f == F_BELOW) && cell_kind(map[j]) == CK_ACTOR) return -1;
        i = j;
    }
    return -1;
}

void Edit::finish_behind()
{
    bool keep = behind;
    behind = false;
    for (auto &b : backs) {
        if (cell_kind(map[b.first]) != CK_ACTOR) continue;
        int s = backing_source(map, b.first);
        if (s < 0 || map[s] == b.second) continue;
        if (cell_kind(map[s]) == CK_SOLID) { walled++; continue; }   /* a wall is never painted away for it */
        if (set(s % MAP_W, s / MAP_W, b.second)) spread++;
    }
    backs.clear();
    behind = keep;
}

bool edit_see_variants;

bool Edit::set(int c, int r, uint16_t v)
{
    if (c < 0 || r < 0 || c >= MAP_W || r >= MAP_H) return false;
    uint16_t &cell = map[r * MAP_W + c];
    /* (Techbot) a backdrop painted on a see-through actor - one the brush
     * goes behind, or one protected from being painted over - becomes the
     * backdrop it shows through: the actor's stated variant (game.c) */
    if (edit_see_variants && (v == 0 || v == 0x20) && ed_see_index(cell) >= 0 && (behind || cell_locked(cell, locks)) &&
        !(locks & LOCK_BACKDROP)) {
        uint16_t w = see_with(cell, v);
        if (w == cell) return false;
        cell = w;
        changed++;
        c0 = std::min(c0, c); r0 = std::min(r0, r);
        c1 = std::max(c1, c); r1 = std::max(r1, r);
        return true;
    }
    if (behind && cell_kind(cell) == CK_ACTOR && behind_tile(v)) {   /* behind the actor, not over it */
        if (!cell_locked(cell, locks)) backs.push_back({ r * MAP_W + c, v });
        return false;
    }
    if (cell == v || cell_locked(cell, locks)) return false;
    cell = v;
    changed++;
    c0 = std::min(c0, c); r0 = std::min(r0, r);
    c1 = std::max(c1, c); r1 = std::max(r1, r);
    return true;
}

void op_line(Edit &e, int c0, int r0, int c1, int r1, uint16_t v)
{
    int dc = abs(c1 - c0), dr = -abs(r1 - r0), sc = c0 < c1 ? 1 : -1, sr = r0 < r1 ? 1 : -1, err = dc + dr;
    for (;;) {                                  /* Bresenham: every cell along the way, no gaps */
        e.set(c0, r0, v);
        if (c0 == c1 && r0 == r1) break;
        int e2 = 2 * err;
        if (e2 >= dr) { err += dr; c0 += sc; }
        if (e2 <= dc) { err += dc; r0 += sr; }
    }
    e.finish_behind();
}

void op_rect(Edit &e, int c0, int r0, int c1, int r1, uint16_t v, bool filled)
{
    if (c0 > c1) std::swap(c0, c1);
    if (r0 > r1) std::swap(r0, r1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            if (filled || r == r0 || r == r1 || c == c0 || c == c1) e.set(c, r, v);
    e.finish_behind();
}

/* what a cell counts as for filling: an actor, when painting behind, counts as its background */
static uint16_t fill_value(const Edit &e, int i)
{
    if (!e.behind || cell_kind(e.map[i]) != CK_ACTOR) return e.map[i];
    int s = backing_source(e.map, i);
    return s < 0 ? e.map[i] : e.map[s];
}

void op_fill(Edit &e, int c, int r, uint16_t v, bool everywhere)
{
    if (c < 0 || r < 0 || c >= MAP_W || r >= MAP_H) return;
    uint16_t from = fill_value(e, r * MAP_W + c);
    if (from == v || cell_locked(from, e.locks)) return;
    if (everywhere) { op_replace(e, from, v); return; }
    std::vector<int> todo(1, r * MAP_W + c);
    std::vector<uint8_t> seen(ED_MAP_CELLS, 0);
    seen[r * MAP_W + c] = 1;
    std::vector<int> area;
    while (!todo.empty()) {                     /* the connected area (4 neighbours), found before any change */
        int i = todo.back();
        todo.pop_back();
        int x = i % MAP_W, y = i / MAP_W;
        area.push_back(i);
        const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= MAP_W || ny[k] >= MAP_H) continue;
            int j = ny[k] * MAP_W + nx[k];
            if (!seen[j] && fill_value(e, j) == from) { seen[j] = 1; todo.push_back(j); }
        }
    }
    for (int i : area) e.set(i % MAP_W, i / MAP_W, v);
    e.finish_behind();
}

void op_replace(Edit &e, uint16_t from, uint16_t to)
{
    if (from == to) return;
    std::vector<int> hit;                       /* decided before any change: actors read their sources */
    for (int i = 0; i < ED_MAP_CELLS; i++)
        if (e.map[i] == from || fill_value(e, i) == from) hit.push_back(i);
    for (int i : hit) e.set(i % MAP_W, i / MAP_W, to);
    e.finish_behind();
}

/* ---- selections */
void Selection::update()
{
    any = std::find(on.begin(), on.end(), 1) != on.end();
}

void Selection::rect(int c0, int r0, int c1, int r1, int mode)
{
    if (mode == 0) std::fill(on.begin(), on.end(), 0);
    if (c0 > c1) std::swap(c0, c1);
    if (r0 > r1) std::swap(r0, r1);
    c0 = std::max(c0, 0); r0 = std::max(r0, 0); c1 = std::min(c1, MAP_W - 1); r1 = std::min(r1, MAP_H - 1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) on[r * MAP_W + c] = mode == 2 ? 0 : 1;
    update();
}

void Selection::magic(const uint16_t *map, int c, int r, int mode)
{
    if (c < 0 || r < 0 || c >= MAP_W || r >= MAP_H) return;
    if (mode == 0) std::fill(on.begin(), on.end(), 0);
    uint16_t v = map[r * MAP_W + c];
    std::vector<int> todo(1, r * MAP_W + c);
    std::vector<uint8_t> seen(ED_MAP_CELLS, 0);
    seen[r * MAP_W + c] = 1;
    while (!todo.empty()) {
        int i = todo.back();
        todo.pop_back();
        on[i] = mode == 2 ? 0 : 1;
        int x = i % MAP_W, y = i / MAP_W;
        const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= MAP_W || ny[k] >= MAP_H) continue;
            int j = ny[k] * MAP_W + nx[k];
            if (!seen[j] && map[j] == v) { seen[j] = 1; todo.push_back(j); }
        }
    }
    update();
}

void Selection::matching(const uint16_t *map, uint16_t v)
{
    for (int i = 0; i < ED_MAP_CELLS; i++) on[i] = map[i] == v;
    update();
}

void Selection::all()
{
    std::fill(on.begin(), on.end(), 1);
    any = true;
}

bool Selection::bounds(int &c0, int &r0, int &c1, int &r1) const
{
    c0 = MAP_W; r0 = MAP_H; c1 = -1; r1 = -1;
    for (int i = 0; i < ED_MAP_CELLS; i++)
        if (on[i]) {
            int c = i % MAP_W, r = i / MAP_W;
            c0 = std::min(c0, c); r0 = std::min(r0, r); c1 = std::max(c1, c); r1 = std::max(r1, r);
        }
    return c1 >= 0;
}

/* ---- the clipboard */
void Clip::copy(const uint16_t *map, const Selection &s)
{
    int c0, r0, c1, r1;
    if (!s.bounds(c0, r0, c1, r1)) { w = h = 0; v.clear(); on.clear(); return; }
    w = c1 - c0 + 1;
    h = r1 - r0 + 1;
    v.assign(w * h, 0);
    on.assign(w * h, 0);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            int i = (r0 + r) * MAP_W + c0 + c;
            v[r * w + c] = map[i];
            on[r * w + c] = s.on[i];
        }
}

/* Mirroring moves cells, it cannot redraw tiles (DN1 has no mirrored tiles).
 * Actors that face a way swap for their counterpart.  A conveyor runs from
 * its start marker rightwards to its end marker, so a mirrored left-moving
 * conveyor becomes a right-moving one: start and end swap roles too. */
uint16_t mirror_h(uint16_t v)
{
    switch (v) {
    case 0x3002: return 0x3005;   case 0x3005: return 0x3002;   /* conveyor left start <-> right end */
    case 0x3003: return 0x3004;   case 0x3004: return 0x3003;   /* conveyor left end <-> right start */
    case 0x3009: return 0x300A;   case 0x300A: return 0x3009;   /* flamethrowers */
    case 0x3016: return 0x3017;   case 0x3017: return 0x3016;   /* biters */
    case 0x301B: return 0x301C;   case 0x301C: return 0x301B;   /* fans */
    }
    return v;
}

uint16_t mirror_v(uint16_t v)
{
    switch (v) {
    case 0x3058: return 0x3059;   case 0x3059: return 0x3058;   /* spikes up <-> down */
    }
    return v;
}

void Clip::flip_h()
{
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w / 2; c++) {
            std::swap(v[r * w + c], v[r * w + w - 1 - c]);
            std::swap(on[r * w + c], on[r * w + w - 1 - c]);
        }
    for (uint16_t &x : v) x = mirror_h(x);
}

void Clip::flip_v()
{
    for (int r = 0; r < h / 2; r++)
        for (int c = 0; c < w; c++) {
            std::swap(v[r * w + c], v[(h - 1 - r) * w + c]);
            std::swap(on[r * w + c], on[(h - 1 - r) * w + c]);
        }
    for (uint16_t &x : v) x = mirror_v(x);
}

void Clip::paste(Edit &e, int c, int r) const
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (on[y * w + x]) e.set(c + x, r + y, v[y * w + x]);
}

/* ---- actors */

/* F: the same actor facing the other way (or pointing the other way) */
uint16_t actor_flip(uint16_t id)
{
    if (id == 0x3002) return 0x3004;            /* conveyors: change direction */
    if (id == 0x3004) return 0x3002;
    uint16_t m = mirror_h(id);
    return m != id ? m : mirror_v(id);
}

/* actors a level has one of: placing another moves it */
bool actor_single(uint16_t id)
{
    return id == 0x3032 || id == 0x3011 || id == 0x3042 || id == 0x3043 || id == 0x302F || id == 0x3030;
}

/* What is left in a cell when its actor goes: what the game shows there
 * (the loader fills an actor's cell from a neighbour), unless that is one of
 * the tiles the game itself lays for an actor; then the eraser's backdrop. */
uint16_t uncovered(const uint16_t *after, int c, int r, uint16_t id, uint16_t erase)
{
    uint16_t a = after[r * MAP_W + c];
    switch (id) {                               /* their cells hold tiles the game lays for them */
    case 0x3001: case 0x3002: case 0x3003: case 0x3004: case 0x3005: case 0x3019: case 0x3036: return erase;
    }
    switch (a) {
    case 0x1AE0: case 0x1820: case 0x21A0: case 0x21C0: case 0x2FE0: return erase;
    }
    return cell_kind(a) == CK_ACTOR || cell_kind(a) == CK_INVALID ? erase : a;
}

/* place an actor; a single one already in the level moves */
void place_actor(Edit &e, const uint16_t *after, int c, int r, uint16_t id, uint16_t erase)
{
    if (actor_single(id))
        for (int i = 0; i < ED_MAP_CELLS; i++)
            if (e.map[i] == id && i != r * MAP_W + c) e.set(i % MAP_W, i / MAP_W, uncovered(after, i % MAP_W, i / MAP_W, id, erase));
    e.set(c, r, id);
}

/* actors that come as a start and an end on one row, the end to the right:
 * conveyors (start 3002h / 3004h, end one higher) and the walkway (its stop
 * is tile 2FE0h) */
bool span_actor(uint16_t id) { return id == 0x3002 || id == 0x3004 || id == 0x3036; }

void place_span(Edit &e, int c0, int c1, int r, uint16_t id)
{
    if (c0 > c1) std::swap(c0, c1);
    if (c1 == c0) c1 = std::min(c0 + 1, MAP_W - 1);
    e.set(c0, r, id);
    e.set(c1, r, id == 0x3036 ? 0x2FE0 : (uint16_t)(id + 1));
}

/* how many of each of the game's actor tables a map fills */
void count_actors(const uint16_t *map, int counts[ED_N_TABLES])
{
    for (int t = 0; t < ED_N_TABLES; t++) counts[t] = 0;
    for (int i = 0; i < ED_MAP_CELLS; i++) {
        int cap, t = ed_actor_limit(map[i], &cap);
        if (t) counts[t]++;
    }
}

const char *table_name(int t)
{
    static const char *const n[ED_N_TABLES] = { "",
        "Grey crates", "Elevators", "Conveyors", "Rockets", "Flamethrowers (right)", "Flamethrowers (left)",
        "Floating robots", "Hopping robots", "Wheeled robots", "Fire wheels", "Firepower crates", "Mini robots",
        "Dynamite crates", "Mirror floors", "Soda", "Biters", "Turkey legs", "Force fields (horizontal)", "Fans",
        "Footballs", "Joysticks", "Floppy disks", "Force fields (vertical)", "Helicopters", "Balloons", "Cameras",
        "Decorations", "Atomic health", "Signs", "Floor spikes", "Flags", "Radios", "Bouncing bombs", "Letters",
        "Rabbits", "Fire monsters", "Keys", "Locks", "Key doors", "Resting bombs", "Spikes" };
    return t > 0 && t < ED_N_TABLES ? n[t] : "";
}
