/*
 * checks.cpp - the level checks (M6).
 *
 * The original game checks nothing.  Every rule here comes from what its
 * code does with a map (src/game.c spawn_level_actors and the actor
 * routines), and each result says why in those terms.  Three strengths:
 *   Broken  - the level will hang, crash, or cannot be finished;
 *   Warning - the game allows it, but it is almost certainly a mistake;
 *   Note    - worth knowing.
 * Reachability (can the exit be reached, traps) is M7.
 */
#include "shrapnel.h"
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- what the loader does
 * Most actors' cells are filled from a neighbour when the level loads
 * (spawn_level_actors: LEFT / RIGHT / ABOVE / BELOW); a few get a fixed tile. */
Fill actor_fill(uint16_t id, uint16_t *set)
{
    uint16_t dummy;
    if (!set) set = &dummy;
    switch (id) {
    case 0x3009: case 0x3016: case 0x301C: case 0x3027: return F_RIGHT;
    case 0x3007: case 0x301A: case 0x3025: case 0x302C: case 0x3058: return F_ABOVE;
    case 0x3024: case 0x3059: return F_BELOW;
    case 0x3001: *set = 0x1AE0; return F_SET;                       /* elevator */
    case 0x3002: *set = 0x1820; return F_SET;                       /* conveyor (left) start */
    case 0x3019: *set = 0x21A0; return F_SET;                       /* bridge */
    case 0x3028: case 0x303D: case 0x303E: case 0x303F: *set = 0; return F_SET;
    case 0x304C: case 0x304D: case 0x304E: case 0x304F: *set = 0x1820; return F_SET;   /* key doors */
    case 0x3036: *set = 0x2FE0; return F_SET;                       /* walkway */
    case 0x3003: case 0x3004: case 0x3005: return F_KEEP;           /* drawn over by the belt */
    }
    return F_LEFT;
}

static bool is_solid(uint16_t v) { return v >= 0x1800 && v < 0x3000; }
static bool is_actor(uint16_t v) { return cell_kind(v) == CK_ACTOR; }

/* the map with actor cells filled as the loader fills them (scan order) */
static void loaded_map(const uint16_t *m, std::vector<uint16_t> &a)
{
    a.assign(m, m + ED_MAP_CELLS);
    for (int i = 0; i < ED_MAP_CELLS; i++) {
        uint16_t v = a[i], set = 0;
        if (!is_actor(v)) continue;
        int c = i % MAP_W;
        switch (actor_fill(v, &set)) {
        case F_LEFT:  a[i] = c > 0 ? a[i - 1] : 0; break;
        case F_RIGHT: a[i] = c < MAP_W - 1 ? a[i + 1] : 0; break;
        case F_ABOVE: a[i] = i >= MAP_W ? a[i - MAP_W] : 0; break;
        case F_BELOW: a[i] = i + MAP_W < ED_MAP_CELLS ? a[i + MAP_W] : 0; break;
        case F_SET:   a[i] = set; break;
        case F_KEEP:  break;
        }
    }
}

/* actors the game keeps one of: their position is a single variable */
static bool single(uint16_t id)
{
    switch (id) {
    case 0x3006: case 0x3008: case 0x3011: case 0x3013: case 0x3019: case 0x3020: case 0x302B:
    case 0x302F: case 0x3030: case 0x3032: case 0x3033: case 0x3034: case 0x3035: case 0x3036:
    case 0x3040: case 0x3041: case 0x3042: case 0x3043:
        return true;
    }
    return false;
}

/* actors that fall a cell at a time while nothing solid is below them */
static bool falls(uint16_t id)
{
    switch (id) {
    case 0x300C: case 0x300D: case 0x300E: case 0x3010: case 0x303B: case 0x303C:      /* enemies */
    case 0x300F: case 0x3012: case 0x3015: case 0x3052: case 0x3018: case 0x3051: case 0x301D: case 0x3050:
    case 0x301E: case 0x3054: case 0x301F: case 0x3053: case 0x3029: case 0x302D: case 0x3055: case 0x302E:
    case 0x3056: case 0x3037: case 0x3038: case 0x3039: case 0x303A: case 0x3057:      /* items */
        return true;
    }
    return false;
}

static std::string nm(uint16_t id) { const char *n = actor_name(id); return n ? n : "actor"; }
static std::string hexv(uint16_t v) { char b[16]; snprintf(b, sizeof b, "%04Xh", v); return b; }

/* ---------------------------------------------------------------- the checks */
void run_checks(const Episode &ep, const uint16_t *m, const CheckContext &cx, std::vector<Check> &out)
{
    out.clear();
    std::vector<uint16_t> a;
    loaded_map(m, a);
    auto add = [&](CheckSev s, int i, const std::string &what, const std::string &why, uint16_t fix = 0) {
        Check k;
        k.sev = s;
        k.c = i < 0 ? -1 : i % MAP_W;
        k.r = i < 0 ? -1 : i / MAP_W;
        k.what = what;
        k.why = why;
        k.fix = fix;
        out.push_back(k);
    };
    auto solid = [&](int c, int r) -> bool { return c < 0 || c >= MAP_W || r < 0 || r >= MAP_H || is_solid(a[r * MAP_W + c]); };

    std::vector<int> where[N_ACTORS];
    for (int i = 0; i < ED_MAP_CELLS; i++)
        if (is_actor(m[i])) where[item_base(m[i]) - 0x3000].push_back(i);   /* (a loose item counts as its boxed one) */
    auto all = [&](uint16_t id) -> const std::vector<int> & { return where[id - 0x3000]; };
    auto has = [&](uint16_t id) { return !where[id - 0x3000].empty(); };

    /* ---- 1. what every level needs */
    for (int i = 0; i < ED_MAP_CELLS; i++)
        if (cell_kind(m[i]) == CK_INVALID)
            add(CS_BROKEN, i, "Not a tile or an actor (" + hexv(m[i]) + ")",
                "Every cell must hold a tile (a multiple of 20h below 3000h) or one of the actor IDs. "
                "The game would draw whatever memory lies at that offset.");

    if (cx.dos)
        for (int i = 0; i < ED_MAP_CELLS; i++)
            if (m[i] >= ED_LOOSE_FIRST && m[i] < ED_ACTOR_END)
                add(CS_BROKEN, i, nm(m[i]) + ": Techbot only",
                    "The DOS game can only start the boots, claws, robot hand and access card in a crate; it would draw "
                    "this cell as junk. Use the crate (it is shot open to get the item).", item_base(m[i]));

    const std::vector<int> &starts = all(0x3032);
    if (starts.empty())
        add(CS_BROKEN, -1, "No player start",
            "The player start (3032h) sets where Duke begins and where the camera looks. Without one he "
            "begins wherever the previous level's camera was left.");
    else {
        int s = starts.back(), c = s % MAP_W, r = s / MAP_W;
        if (c < 7 || c > MAP_W - 6 || r < 3 || r > MAP_H - 7)
            add(CS_WARNING, s, "Player start too near the map's edge",
                "The game points its opening camera 7 cells left of and 3 rows above the start, and does not keep "
                "it on the map, so the first view shows past the edge. Keep the start in columns 7 to 122 and "
                "rows 3 to 83. (Shrapnel's Play here keeps the camera on the map, so only the game shows this. A few stock "
                "levels start low enough to show a row or two.)");
    }

    bool proton = has(0x3042) || has(0x3043), exits = has(0x3011);
    if (!cx.last) {
        if (!exits && !proton)
            add(CS_BROKEN, -1, "No exit door",
                cx.hallway ? "A hallway is left by its exit door. Without one Duke cannot go on to the next level."
                           : "A level is finished at its exit door (3011h). Without one Duke cannot leave it.");
        if (proton)
            add(CS_WARNING, all(has(0x3043) ? 0x3043 : 0x3042).back(), "Dr. Proton outside the episode's last level",
                "Beating Dr. Proton ends the whole episode, in whichever level he is, so the levels after this one "
                "would never be played.");
    } else if (cx.dos) {
        if (!proton)
            add(CS_WARNING, -1, "No Dr. Proton in the last level",
                "The original game ends an episode only when Dr. Proton is beaten, so without him this "
                "episode has no ending.");
        if (exits)
            add(CS_WARNING, all(0x3011).back(), "Exit door in the episode's last level",
                "The original game has no level after the tenth: its exit leads into the demo map.");
    } else if (!exits && !proton) {
        add(CS_BROKEN, -1, cx.episode ? "No way to end the episode" : "No exit door",
            cx.episode ? "The last level of a Techbot episode ends it when Dr. Proton is beaten or Duke "
                         "reaches its exit door. It has neither."
                       : "A level is finished at its exit door (3011h), or by beating Dr. Proton. It has neither.");
    }

    /* one of each: the game keeps the last */
    for (int k = 0; k < N_ACTORS; k++) {
        uint16_t id = (uint16_t)(0x3000 + k);
        if (!single(id) || where[k].size() < 2) continue;
        for (size_t j = 0; j + 1 < where[k].size(); j++)
            add(CS_WARNING, where[k][j], "A second " + nm(id),
                "The game keeps one " + nm(id) + " per level, in a single variable: the last one in the map "
                "(reading left to right, top to bottom) wins, and this one never appears or works.");
    }
    if (has(0x3042) && has(0x3043))
        add(CS_WARNING, all(0x3042).back() > all(0x3043).back() ? all(0x3043).back() : all(0x3042).back(),
            "Both kinds of Dr. Proton",
            "The two Dr. Protons share one set of variables, so only the last one in the map appears.");

    /* ---- 2. things that come in sets */
    static const char *const colour[4] = { "red", "green", "blue", "pink" };
    for (int k = 0; k < 4; k++) {
        uint16_t key = (uint16_t)(0x3044 + k), lock = (uint16_t)(0x3048 + k), door = (uint16_t)(0x304C + k);
        std::string c = colour[k];
        if (has(key) && !has(lock))
            add(CS_WARNING, all(key)[0], "A " + c + " key, but no " + c + " lock",
                "A key is used at the lock of its colour, which opens the doors of that colour. This key opens nothing.");
        if (has(lock) && !has(key))
            add(CS_WARNING, all(lock)[0], "A " + c + " lock, but no " + c + " key",
                "The lock needs the " + c + " key, and there is none in the level.");
        if (has(door) && !has(lock))
            add(CS_WARNING, all(door)[0], "A " + c + " key door, but no " + c + " lock",
                "Key doors open from the lock of their colour. With no " + c + " lock this door never opens.");
        if (has(lock) && !has(door))
            add(CS_WARNING, all(lock)[0], "A " + c + " lock, but no " + c + " key doors",
                "The lock opens the doors of its colour, and there are none.");
    }
    if (has(0x3033) && !has(0x3034))
        add(CS_WARNING, all(0x3033)[0], "An access card, but no card reader",
            "The card is used at the card reader (3034h), and the level has none.");
    if (has(0x3034) && !has(0x3033))
        add(CS_WARNING, all(0x3034)[0], "A card reader, but no access card",
            "The card reader needs the access card (3033h, in a crate), and the level has none.");
    if (has(0x3021) && !(has(0x3033) && has(0x3034)))
        add(CS_WARNING, all(0x3021)[0], "Vertical force fields that never switch off",
            "Vertical force fields go off when the access card is used at the card reader. This level lacks " +
            std::string(!has(0x3033) && !has(0x3034) ? "both" : !has(0x3033) ? "the card" : "the reader") +
            ", so they stay on for good.");
    if (has(0x301A) && !has(0x302B))
        add(CS_NOTE, all(0x301A)[0], "Horizontal force fields stay on for good",
            "Horizontal force fields go off only when a reactor (302Bh) is destroyed, and the level has none.");
    if (has(0x3035) && !has(0x3020) && !cx.robohand_before)
        add(CS_WARNING, all(0x3035)[0], "A hand reader, but no robot hand",
            std::string("The hand reader needs the robot hand (3020h, in a crate), which Duke keeps from level to level. ") +
            (cx.episode ? "Neither this level nor any before it has one" : "This level has none") +
            ", so using the reader sets off its guns.");
    if (has(0x3035) && !has(0x3036))
        add(CS_WARNING, all(0x3035)[0], "A hand reader, but no walkway",
            "Using the hand reader extends the walkway (3036h), and the level has none.");
    if (has(0x3036) && !has(0x3035))
        add(CS_WARNING, all(0x3036)[0], "A walkway that never extends",
            "The walkway extends when the hand reader (3035h) is used, and the level has none.");
    if (has(0x302F) != has(0x3030))
        add(CS_WARNING, all(has(0x302F) ? 0x302F : 0x3030)[0], "A teleporter without its partner",
            "Teleporter A sends Duke to teleporter B and back. The level has only one of the pair.");

    for (uint16_t s : { (uint16_t)0x3002, (uint16_t)0x3004 })        /* conveyors */
        for (int i : all(s)) {
            int j = i + 1;
            while (j < ED_MAP_CELLS && m[j] != s + 1) j++;
            if (j == ED_MAP_CELLS)
                add(CS_BROKEN, i, "A conveyor start with no end",
                    "The game searches onwards from the start for its end marker (" + hexv((uint16_t)(s + 1)) +
                    "), and never stops: there is none after it.");
            else if (j / MAP_W != i / MAP_W)
                add(CS_WARNING, i, "A conveyor whose end is on a later row",
                    "The belt runs from the start to the first end marker after it, wrapping round the map's rows "
                    "on the way, and is redrawn over everything there on every frame.");
        }
    for (uint16_t e : { (uint16_t)0x3003, (uint16_t)0x3005 })
        for (int i : all(e)) {
            int j = i - 1;
            while (j >= 0 && m[j] != e - 1 && m[j] != e) j--;
            if (j < 0 || m[j] == e)
                add(CS_WARNING, i, "A conveyor end with no start",
                    "An end marker belongs to the conveyor start (" + hexv((uint16_t)(e - 1)) +
                    ") before it. On its own the game leaves the marker in the map, where it draws as junk.");
        }
    for (int i : all(0x3036)) {                                     /* walkway */
        int c = i % MAP_W, r = i / MAP_W, s = -1;
        for (int x = c + 1; x < MAP_W && s < 0; x++) if (m[r * MAP_W + x] == 0x2FE0) s = x;
        if (s < 0)
            add(CS_BROKEN, i, "A walkway with no stop",
                "The walkway extends one cell at a time until it meets a walkway-stop tile (2FE0h). There is none "
                "to its right on this row, so it would run on past the row's end.");
    }
    for (int i : all(0x3019)) {                                     /* bridge */
        /* the loader: for (k = 0; cell(c + k + 1) < 1800h; k += 2) cells c+k, c+k+1 = bridge.
         * Only the odd distances are tested, so the bridge covers c .. end-2, where end is
         * the first solid cell at an odd distance; a wall at an even distance before that is
         * overwritten, and a free cell at end-1 is left as a gap. */
        int c = i % MAP_W, r = i / MAP_W, end = -1;
        auto stop = [&](int x) { return x >= MAP_W || m[r * MAP_W + x] >= 0x1800; };
        for (int k = 1; c + k < MAP_W; k += 2) if (stop(c + k)) { end = c + k; break; }
        if (end < 0) {
            add(CS_BROKEN, i, "A bridge with nothing to reach",
                "The game lays the bridge two cells at a time until the second of a pair is solid. There is no solid "
                "cell an odd number of cells to its right on this row, so it would run on past the row's end.");
            continue;
        }
        for (int x = c + 2; x <= end - 3; x += 2)
            if (stop(x)) {
                add(CS_WARNING, r * MAP_W + x, "A bridge that runs through a wall",
                    "The game lays the bridge two cells at a time and tests only the second cell of each pair, so this "
                    "solid cell (an even number of cells from the bridge) is written over. The bridge stops at column " +
                    std::to_string(end - 1) + ".");
                break;
            }
        if (end - 1 > c && !stop(end - 1))
            add(CS_WARNING, i, "A bridge that stops a cell short of the wall",
                "The game lays the bridge two cells at a time and stops when the second cell of the next pair is "
                "solid. The wall is an odd number of cells away, so one free cell is left between the bridge and it. "
                "Move the bridge or the wall by one.");
    }

    /* ---- 3. where actors go */
    for (int i = 0; i < ED_MAP_CELLS; i++) {
        uint16_t v = m[i];
        if (!is_actor(v)) continue;
        int c = i % MAP_W, r = i / MAP_W;
        switch (v) {
        case 0x3016: case 0x3017: {                                 /* biters */
            bool left = v == 0x3016, want = solid(left ? c - 1 : c + 1, r), other = solid(left ? c + 1 : c - 1, r);
            if (!want)
                add(CS_WARNING, i, std::string("A biter with no wall on its ") + (left ? "left" : "right"),
                    "Biters climb the wall beside them and turn round where it ends. With no wall on that side this "
                    "one turns round every frame and jitters on the spot." +
                    std::string(other ? " The wall is on its other side: the fix turns it round." : ""),
                    other ? (uint16_t)(left ? 0x3017 : 0x3016) : 0);
            break;
        }
        case 0x3059:                                                /* hung from a ceiling */
            if (!solid(c, r - 1))
                add(CS_WARNING, i, nm(v) + " with no ceiling above",
                    "The game fills its cell from the cell below it, as for something hanging from the ceiling, "
                    "but there is nothing solid above it.");
            break;
        case 0x3007: case 0x302C: case 0x3058:                      /* standing on a floor */
            if (!solid(c, r + 1))
                add(CS_WARNING, i, nm(v) + " with no floor below",
                    "The game fills its cell from the cell above it, as for something standing on the floor, "
                    "but there is nothing solid below it.");
            break;
        case 0x3009: case 0x301C:                                   /* facing right */
        case 0x300A: case 0x301B: {                                 /* facing left */
            bool right = v == 0x3009 || v == 0x301C;
            if (solid(right ? c + 1 : c - 1, r))
                add(CS_WARNING, i, nm(v) + " facing straight into a wall",
                    std::string("It works towards its ") + (right ? "right" : "left") + ", and the cell there is solid.");
            break;
        }
        }
        if (falls(v) && !solid(c, r + 1)) {
            int land = r + 1;
            while (land < MAP_H && !solid(c, land)) land++;
            if (land >= MAP_H)
                add(CS_WARNING, i, nm(v) + " falls off the bottom of the map",
                    "It falls while nothing solid is below it, and there is no floor below it at all.");
            else
                add(CS_NOTE, i, nm(v) + " drops to row " + std::to_string(land - 1) + " when the level starts",
                    "It falls a cell at a time while nothing solid is below it.");
        }
        /* the cell the loader copies in, when that is a wall or another actor's tile */
        uint16_t set = 0;
        Fill f = actor_fill(v, &set);
        bool embedded = v == 0x3014 || v == 0x302A || (v >= 0x3025 && v <= 0x3028);   /* made to sit in walls */
        if (f <= F_BELOW && !embedded) {
            int nc = c + (f == F_LEFT ? -1 : f == F_RIGHT ? 1 : 0), nr = r + (f == F_ABOVE ? -1 : f == F_BELOW ? 1 : 0);
            uint16_t got = a[i];
            if (nc >= 0 && nc < MAP_W && nr >= 0 && nr < MAP_H && is_solid(got))
                add(CS_NOTE, i, nm(v) + "'s cell shows a copy of the wall " +
                                (f == F_LEFT ? "to its left" : f == F_RIGHT ? "to its right" : f == F_ABOVE ? "above it" : "below it"),
                    "When the level loads, the game fills an actor's own cell from one neighbour, so the actor is "
                    "drawn over a copy of that wall (and the cell is solid).");
        }
    }

    /* ---- big actors overlapping walls */
    for (uint16_t id : { (uint16_t)0x3011, (uint16_t)0x302F, (uint16_t)0x3030, (uint16_t)0x3022, (uint16_t)0x3042, (uint16_t)0x3043 }) {
        const ActorLook &lk = ep.looks[id - 0x3000];
        for (int i : all(id)) {
            int c = i % MAP_W, r = i / MAP_W, hit = -1;
            for (const Piece &p : lk.pieces) {
                int pc = c + (int)floorf(p.dx / 16.0f), pr = r + (int)floorf(p.dy / 16.0f);
                for (int dy = 0; dy < 2 && hit < 0; dy++)
                    for (int dx = 0; dx < 2 && hit < 0; dx++) {
                        int cc = pc + dx, rr = pr + dy;
                        if ((p.dx % 16 == 0 && dx) || (p.dy % 16 == 0 && dy)) continue;
                        if (cc == c && rr == r) continue;
                        if (cc < 0 || cc >= MAP_W || rr < 0 || rr >= MAP_H) { hit = 0; continue; }
                        if (is_solid(m[rr * MAP_W + cc])) hit = rr * MAP_W + cc;
                    }
            }
            if (hit >= 0)
                add(CS_WARNING, i, nm(id) + " overlaps " + (hit > 0 ? "a wall" : "the map's edge"),
                    "It is drawn larger than its own cell, and part of it lies over solid tiles or off the map.");
        }
    }

    /* ---- 4. the game's actor tables */
    int counts[ED_N_TABLES] = { 0 }, first_over[ED_N_TABLES];
    for (int t = 0; t < ED_N_TABLES; t++) first_over[t] = -1;
    for (int i = 0; i < ED_MAP_CELLS; i++) {
        int cap, t = ed_actor_limit(m[i], &cap);
        if (t && ++counts[t] > cap && first_over[t] < 0) first_over[t] = i;
    }
    for (int t = 1; t < ED_N_TABLES; t++) {
        if (first_over[t] < 0) continue;
        int cap;
        ed_actor_limit(m[first_over[t]], &cap);
        add(CS_BROKEN, first_over[t], std::string("More ") + table_name(t) + " than the game can hold (" +
                                      std::to_string(counts[t]) + " of " + std::to_string(cap) + ")",
            "The game keeps each kind of actor in a table of fixed size and does not check it: the extra ones are "
            "written over whatever follows in memory. This one is the first past the limit.");
    }

    std::stable_sort(out.begin(), out.end(), [](const Check &x, const Check &y) { return x.sev > y.sev; });
}

int check_count(const std::vector<Check> &v, CheckSev s)
{
    int n = 0;
    for (const Check &k : v) n += k.sev == s;
    return n;
}
