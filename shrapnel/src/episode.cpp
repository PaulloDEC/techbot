/*
 * episode.cpp - loading an episode and its levels through the game's code,
 * and drawing a whole level the way the game draws it.
 */
#include "shrapnel.h"
#include <stdio.h>
#include <string.h>

/* ---- what a cell holds (see QUIRKS/assessment: map cells are tile-cache offsets) */
CellKind cell_kind(uint16_t v)
{
    if (v == 0) return CK_BACKDROP_A;
    if (v == 0x20) return CK_BACKDROP_B;
    if (v >= 0x3000 && v < 0x3000 + N_ACTORS) return CK_ACTOR;
    if (v & 0x1F) return CK_INVALID;
    if (v < 0x5E0) return CK_ANIMATED;
    if (v < 0x1800) return CK_BACKGROUND;
    if (v < 0x3000) return CK_SOLID;
    return CK_INVALID;
}

const char *cell_kind_name(CellKind k)
{
    switch (k) {
    case CK_BACKDROP_A: return "Backdrop A";
    case CK_BACKDROP_B: return "Backdrop B";
    case CK_ANIMATED:   return "Animated background";
    case CK_BACKGROUND: return "Background";
    case CK_SOLID:      return "Solid";
    case CK_ACTOR:      return "Actor";
    default:            return "Not a valid cell";
    }
}

/* names as players know them; "(crate)" = starts inside a crate to shoot open */
static const char *const names[N_ACTORS] = {
    "Grey crate (empty)",            /* 3000 */
    "Elevator",                      /* 3001 */
    "Conveyor, moving left: start",  /* 3002 */
    "Conveyor, moving left: end",    /* 3003 */
    "Conveyor, moving right: start", /* 3004 */
    "Conveyor, moving right: end",   /* 3005 */
    "Boots (crate)",                 /* 3006 */
    "Rocket",                        /* 3007 */
    "Grappling claws (crate)",       /* 3008 */
    "Flamethrower, firing right",    /* 3009 */
    "Flamethrower, firing left",     /* 300A */
    "Floating robot",                /* 300B */
    "Hopping robot",                 /* 300C */
    "Wheeled robot",                 /* 300D */
    "Fire wheel",                    /* 300E */
    "Firepower (crate)",             /* 300F */
    "Mini robot",                    /* 3010 */
    "Exit door",                     /* 3011 */
    "Dynamite (crate)",              /* 3012 */
    "Snake robot",                   /* 3013 */
    "Mirror floor",                  /* 3014 */
    "Soda (crate)",                  /* 3015 */
    "Biter, wall on its left",       /* 3016 */
    "Biter, wall on its right",      /* 3017 */
    "Turkey leg (crate)",            /* 3018 */
    "Bridge",                        /* 3019 */
    "Force field, horizontal",       /* 301A */
    "Fan, facing left",              /* 301B */
    "Fan, facing right",             /* 301C */
    "Football (crate)",              /* 301D */
    "Joystick (crate)",              /* 301E */
    "Floppy disk (crate)",           /* 301F */
    "Robot hand (crate)",            /* 3020 */
    "Force field, vertical",         /* 3021 */
    "Helicopter",                    /* 3022 */
    "Balloon (crate)",               /* 3023 */
    "Security camera",               /* 3024 */
    "Dark Vertical Steel Girder, Damaged Top",              /* 3025 */
    "Damaged Wall Right",              /* 3026 */
    "Damaged Wall Left",              /* 3027 */
    "Window with Bars",              /* 3028 */
    "Atomic health (crate)",         /* 3029 */
    "ACME Sign",                          /* 302A */
    "Reactor",                       /* 302B */
    "Floor spikes",                  /* 302C */
    "Flag (crate)",                  /* 302D */
    "Radio (crate)",                 /* 302E */
    "Teleporter A",                  /* 302F */
    "Teleporter B",                  /* 3030 */
    "Bouncing bomb",                 /* 3031 */
    "Player start",                  /* 3032 */
    "Access card (crate)",           /* 3033 */
    "Card reader",                   /* 3034 */
    "Hand reader",                   /* 3035 */
    "Walkway",                       /* 3036 */
    "Letter D (crate)",              /* 3037 */
    "Letter U (crate)",              /* 3038 */
    "Letter K (crate)",              /* 3039 */
    "Letter E (crate)",              /* 303A */
    "Rabbit",                        /* 303B */
    "Fire monster",                  /* 303C */
    "Chain-Link Fence",              /* 303D */
    "Three-Panel Window, Left",              /* 303E */
    "Three-Panel Window, Right",              /* 303F */
    "Note monitor",                  /* 3040 */
    "Dr. Proton screen",             /* 3041 */
    "Dr. Proton (episode 3 boss)",   /* 3042 */
    "Dr. Proton (episodes 1-2 boss)",/* 3043 */
    "Red key",                       /* 3044 */
    "Green key",                     /* 3045 */
    "Blue key",                      /* 3046 */
    "Pink key",                      /* 3047 */
    "Red lock",                      /* 3048 */
    "Green lock",                    /* 3049 */
    "Blue lock",                     /* 304A */
    "Pink lock",                     /* 304B */
    "Red key door",                  /* 304C */
    "Green key door",                /* 304D */
    "Blue key door",                 /* 304E */
    "Pink key door",                 /* 304F */
    "Football",                      /* 3050 */
    "Turkey leg",                    /* 3051 */
    "Soda",                          /* 3052 */
    "Floppy disk",                   /* 3053 */
    "Joystick",                      /* 3054 */
    "Flag",                          /* 3055 */
    "Radio",                         /* 3056 */
    "Resting bomb",                  /* 3057 */
    "Spikes, pointing up",           /* 3058 */
    "Spikes, pointing down",         /* 3059 */
    "Boots (loose)",                 /* 305A: Techbot only */
    "Grappling claws (loose)",       /* 305B */
    "Robot hand (loose)",            /* 305C */
    "Access card (loose)",           /* 305D */
};

bool actor_crate(uint16_t id)
{
    switch (id) {
    case 0x300F: case 0x3006: case 0x3008: case 0x3020: case 0x3033: case 0x3029: case 0x3012: case 0x3015:
    case 0x3018: case 0x301D: case 0x301E: case 0x301F: case 0x302D: case 0x302E: case 0x3023:
    case 0x3037: case 0x3038: case 0x3039: case 0x303A:
        return true;
    }
    return false;
}

const char *actor_name(uint16_t id)
{
    return (id >= 0x3000 && id < 0x3000 + N_ACTORS) ? names[id - 0x3000] : NULL;
}

/* ---- tiles' names and sets (tile_names.cpp) */
static const TileName *tile_entry(uint16_t v)
{
    static const TileName *by[0x3000 / 0x20];
    static bool made;
    if (!made) {
        for (int i = 0; i < n_tile_names; i++) if (tile_names[i].v < 0x3000) by[tile_names[i].v / 0x20] = &tile_names[i];
        made = true;
    }
    return v < 0x3000 && !(v & 0x1F) ? by[v / 0x20] : NULL;
}
const char *tile_name(uint16_t v) { const TileName *t = tile_entry(v); return t ? t->name : NULL; }
const char *tile_set(uint16_t v) { const TileName *t = tile_entry(v); return t && t->set[0] ? t->set : NULL; }

/* ---- episodes */
bool Episode::load(int ep)
{
    static const char *const exe[4] = { "", "DN1.EXE", "DN2.EXE", "DN3.EXE" };
    FILE *f;
    for (int e = 1; e <= 3; e++) {
        available[e] = (f = fopen(game_path(exe[e]), "rb")) != NULL;
        if (f) fclose(f);
    }
    if (ep < 1 || ep > 3 || !available[ep]) return false;
    if (!exe_load(game_path(exe[ep])) || !ed_boot()) { number = 0; return false; }
    number = ep;
    ed_palette(pal);
    static EdPiece raw[64];
    for (int i = 0; i < N_ACTORS; i++) {
        ActorLook &l = looks[i];
        uint16_t after = 0;
        int id = 0x3000 + i;
        int n = ed_actor_look((uint16_t)id, raw, 64 | (id == 0x3032 ? ED_LOOK_DUKE : 0), &after);
        l.pieces.clear();
        l.after = after;
        for (int k = 0; k < n; k++) {
            Piece p;
            p.dx = raw[k].dx;
            p.dy = raw[k].dy;
            if (raw[k].src) ed_sprite(raw[k].src, p.px);
            else ed_tile(raw[k].tile, p.px);
            l.pieces.push_back(p);
        }
        l.contents.clear();                     /* crates: shoot one and see what the game draws */
        if (actor_crate((uint16_t)id)) {
            n = ed_actor_contents((uint16_t)id, raw, 64);
            for (int k = 0; k < n; k++) {
                Piece p;
                p.dx = raw[k].dx;
                p.dy = raw[k].dy;
                ed_sprite(raw[k].src, p.px);
                l.contents.push_back(p);
            }
        }
    }
    return true;
}

std::string Episode::slot_file(int slot) const
{
    if (!number || slot < 0 || slot > 20) return "";
    return ds_farstr((uint16_t)(0x3A4 + slot * 4));
}

/* ---- map files */
bool read_map_file(const std::string &path, uint16_t map[ED_MAP_CELLS])
{
    uint8_t b[ED_MAP_CELLS * 2 + 1];
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != ED_MAP_CELLS * 2) return false;    /* exactly 23,040 bytes */
    for (int i = 0; i < ED_MAP_CELLS; i++) map[i] = (uint16_t)(b[i * 2] | b[i * 2 + 1] << 8);
    return true;
}

bool write_map_file(const std::string &path, const uint16_t map[ED_MAP_CELLS])
{
    uint8_t b[ED_MAP_CELLS * 2];
    for (int i = 0; i < ED_MAP_CELLS; i++) { b[i * 2] = (uint8_t)map[i]; b[i * 2 + 1] = (uint8_t)(map[i] >> 8); }
    std::string tmp = path + ".tmp";            /* write it whole, then put it in place */
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(b, 1, sizeof b, f) == sizeof b;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

/* ---- levels */
bool Level::load(const Episode &ep, int s)
{
    if (!ep.number) return false;
    return load_file(ep, s, game_path(ep.slot_file(s).c_str()));
}

bool Level::load_file(const Episode &ep, int s, const std::string &p)
{
    if (!ep.number || !read_map_file(p, map)) return false;
    slot = s;
    path = p;
    size_t cut = p.find_last_of("/\\");
    file = cut == std::string::npos ? p : p.substr(cut + 1);
    ed_load(s, map);
    ed_map_after(after);
    ed_backdrop(0, backdrop[0]);
    ed_backdrop(1, backdrop[1]);
    return true;
}

void Level::load_map(const Episode &ep, int s, const uint16_t *m, const std::string &name)
{
    memcpy(map, m, sizeof map);
    slot = s;
    path.clear();
    file = name;
    ed_load(s, map);
    ed_map_after(after);
    ed_backdrop(0, backdrop[0]);
    ed_backdrop(1, backdrop[1]);
}

/* the game's view of the map once loaded: actors put in their cells, bridges laid... */
void Level::refresh()
{
    ed_load(slot, map);
    ed_map_after(after);
}

/* ---- drawing */
int look_reach(const Episode &ep)
{
    int reach = 1;
    for (const ActorLook &l : ep.looks)
        for (const Piece &p : l.pieces) {
            int m = std::max(std::max(-p.dx, p.dx + 16), std::max(-p.dy, p.dy + 16)) / 16 + 1;
            reach = std::max(reach, m);
        }
    return reach;
}

bool compose_backdrop = true;

void compose_area(const Episode &ep, const Level &lv, int anim, bool actors, uint32_t *out,
                  int c0, int r0, int c1, int r1)
{
    uint8_t t[256];
    uint32_t pal[16];
    c0 = std::max(c0, 0); r0 = std::max(r0, 0); c1 = std::min(c1, MAP_W); r1 = std::min(r1, MAP_H);
    if (c0 >= c1 || r0 >= r1) return;
    for (int i = 0; i < 16; i++) pal[i] = 0xFF000000u | ep.pal[i];
    for (int r = r0; r < r1; r++)
        for (int c = c0; c < c1; c++) {
            uint16_t v = lv.after[r * MAP_W + c];
            uint32_t *o = out + r * 16 * LEVEL_PX_W + c * 16;
            if (v <= 0x20 && !compose_backdrop) {   /* backdrop hidden: a flat shade each for A and B */
                uint32_t k = v == 0x20 ? 0xFF262636u : 0xFF141418u;
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++) o[y * LEVEL_PX_W + x] = k;
                continue;
            }
            if (v <= 0x20) {                 /* backdrop: repeated every 13 x 10 tiles */
                const uint8_t *bd = lv.backdrop[v == 0x20];
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++)
                        o[y * LEVEL_PX_W + x] = pal[bd[((r * 16 + y) % ED_BD_H) * ED_BD_W + (c * 16 + x) % ED_BD_W]];
                continue;
            }
            /* everything else as draw_world blits it: animated cells step through four frames */
            ed_tile((uint16_t)(v < 0x5E0 ? v + anim : v), t);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) o[y * LEVEL_PX_W + x] = pal[t[y * 16 + x]];
        }
    if (!actors) return;
    /* actors near enough to reach into the area, in the game's (scan) order, clipped to it */
    int reach = look_reach(ep);
    int px0 = c0 * 16, py0 = r0 * 16, px1 = c1 * 16, py1 = r1 * 16;
    for (int r = std::max(0, r0 - reach); r < std::min(MAP_H, r1 + reach); r++)
        for (int c = std::max(0, c0 - reach); c < std::min(MAP_W, c1 + reach); c++) {
            uint16_t v = lv.map[r * MAP_W + c];
            if (cell_kind(v) != CK_ACTOR) continue;
            for (const Piece &p : ep.looks[v - 0x3000].pieces)
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++) {
                        int X = c * 16 + p.dx + x, Y = r * 16 + p.dy + y;
                        uint8_t i = p.px[y * 16 + x];
                        if (i != 255 && X >= px0 && X < px1 && Y >= py0 && Y < py1)
                            out[Y * LEVEL_PX_W + X] = pal[i & 15];
                    }
        }
}

bool level_changed(const Episode &ep, Level &lv, int &c0, int &r0, int &c1, int &r1)
{
    static uint16_t before[ED_MAP_CELLS];
    memcpy(before, lv.after, sizeof before);
    lv.refresh();
    for (int i = 0; i < ED_MAP_CELLS; i++)      /* the loader may change cells further away (bridges) */
        if (before[i] != lv.after[i]) {
            int c = i % MAP_W, r = i / MAP_W;
            c0 = std::min(c0, c); r0 = std::min(r0, r); c1 = std::max(c1, c); r1 = std::max(r1, r);
        }
    if (c1 < 0) return false;
    int reach = look_reach(ep);                 /* actors there reach into their neighbours */
    c0 = std::max(0, c0 - reach); r0 = std::max(0, r0 - reach);
    c1 = std::min(MAP_W, c1 + 1 + reach); r1 = std::min(MAP_H, r1 + 1 + reach);
    return true;
}

void compose_level(const Episode &ep, const Level &lv, int anim, bool actors, uint32_t *out)
{
    compose_area(ep, lv, anim, actors, out, 0, 0, MAP_W, MAP_H);
}
