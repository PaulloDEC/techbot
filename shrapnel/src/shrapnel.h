/*
 * shrapnel.h - Shrapnel's shared declarations.
 *
 * The editor's view of a level is built from the game's own data, decoded by
 * the game's own code (src/port_editor.c): tiles from the episode's tile
 * cache, backdrops, and each actor's look as the game draws it when its level
 * starts.
 */
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>

extern "C" {
#include "dn1.h"
#include "port.h"
extern const char *shrapnel_game_dir;
}

const int MAP_W = 128, MAP_H = 90;              /* cells */
const int LEVEL_PX_W = MAP_W * 16, LEVEL_PX_H = MAP_H * 16;
const int N_ACTORS = ED_ACTOR_END - 0x3000;     /* 3000h..3065h (305Ah.. Techbot's own) */
/* a Techbot variant as the actor it is a variant of - a loose item as the
 * boxed one, a see-through actor with its backdrop stated as the plain one;
 * anything else as it is */
inline uint16_t item_base(uint16_t v)
{
    static const uint16_t b[4] = { 0x3006, 0x3008, 0x3020, 0x3033 };
    if (v >= ED_SEE_A && v < ED_SEE_A + 8) return (uint16_t)ed_see_base(v);
    return v >= ED_LOOSE_FIRST && v < ED_SEE_A ? b[v - ED_LOOSE_FIRST] : v;
}
/* a see-through actor (plain or stated) with backdrop `bd` (0 or 20h) stated behind it */
inline uint16_t see_with(uint16_t v, uint16_t bd) { return (uint16_t)((bd ? ED_SEE_B : ED_SEE_A) + ed_see_index(v)); }
const double GAME_FPS = 1193182.0 / 8273.0 / 9.0;   /* one frame = 9 PIT ticks at 144 Hz */

/* ---- cells */
enum CellKind { CK_BACKDROP_A, CK_BACKDROP_B, CK_ANIMATED, CK_BACKGROUND, CK_SOLID, CK_ACTOR, CK_INVALID };
CellKind    cell_kind(uint16_t v);
const char *cell_kind_name(CellKind k);
const char *actor_name(uint16_t id);            /* 3000h..3059h, else NULL */
struct TileName { uint16_t v; const char *name, *set; };
extern const TileName tile_names[];             /* (tile_names.cpp, made from the objects sheet) */
extern const int n_tile_names;
const char *tile_name(uint16_t v);              /* a tile's name, else NULL */
const char *tile_set(uint16_t v);               /* the set it belongs to (tiles of a kind), else NULL */
bool        actor_crate(uint16_t id);           /* a crate with something inside */

/* ---- an episode's graphics and actor looks */
struct Piece {
    int dx, dy;                                 /* pixels from the actor's cell */
    uint16_t tile = 0;                          /* drawn from this map tile (0: from a sprite) */
    uint8_t px[256];                            /* palette index, 255 = see-through */
};
struct ActorLook {
    std::vector<Piece> pieces;                  /* empty: shown by its tile and a label */
    uint16_t after = 0;                         /* what the loader leaves in its cell */
    std::vector<Piece> contents;                /* a crate: what it holds, as the game draws it once shot */
};

struct Episode {
    int number = 0;                             /* 1..3, 0 = none loaded */
    uint32_t pal[16];                           /* 0xRRGGBB */
    ActorLook looks[N_ACTORS];
    bool available[4] = {};                     /* which DNn.EXE are in the folder */
    bool load(int ep);
    std::string slot_file(int slot) const;      /* map file of level table slot 0..20 */
};

/* ---- a level */
struct Level {
    int slot = -1;                              /* level table slot: decides the backdrops */
    std::string file;                           /* file name shown (the slot's, or an opened file's) */
    std::string path;                           /* where it is saved */
    uint16_t map[ED_MAP_CELLS];                 /* as in the file */
    uint16_t after[ED_MAP_CELLS];               /* as the game holds it once loaded */
    uint8_t backdrop[2][ED_BD_W * ED_BD_H];
    bool load(const Episode &ep, int slot);                       /* the slot's own map file */
    bool load_file(const Episode &ep, int slot, const std::string &path);
    void load_map(const Episode &ep, int slot, const uint16_t *m, const std::string &name);   /* a map in memory */
    void refresh();                             /* after an edit: recompute 'after' */
    uint16_t at(int c, int r) const { return map[r * MAP_W + c]; }
};

/* map files: 23,040 bytes, 16-bit little-endian cells */
bool read_map_file(const std::string &path, uint16_t map[ED_MAP_CELLS]);
bool write_map_file(const std::string &path, const uint16_t map[ED_MAP_CELLS]);

/* the whole level as RGBA (0xAARRGGBB), LEVEL_PX_W x LEVEL_PX_H, at one
 * animation phase (0, 20h, 40h, 60h); backdrops are repeated across the map */
void compose_level(const Episode &ep, const Level &lv, int anim, bool actors, uint32_t *out);
/* the same for the cells c0..c1-1, r0..r1-1 only (the rest of 'out' untouched) */
void compose_area(const Episode &ep, const Level &lv, int anim, bool actors, uint32_t *out,
                  int c0, int r0, int c1, int r1);
extern bool compose_backdrop;                   /* false: backdrop cells as two flat shades (A, B) */
int  look_reach(const Episode &ep);             /* how many cells an actor's look can reach from its cell */
/* After an edit somewhere in cells c0..c1, r0..r1 (inclusive): recompute what
 * the game makes of the map, and turn the area into the cells whose picture
 * may have changed (half-open: c0 <= c < c1).  False if nothing changed. */
bool level_changed(const Episode &ep, Level &lv, int &c0, int &r0, int &c1, int &r1);

/* ---- editing (tools.cpp): operations on a map, independent of the UI */
enum LockBits { LOCK_SOLID = 1, LOCK_BACKGROUND = 2, LOCK_BACKDROP = 4, LOCK_ACTORS = 8 };
bool cell_locked(uint16_t v, int locks);        /* is a cell holding v protected by these locks? */
extern bool edit_see_variants;                 /* Techbot projects: backdrops painted on see-through actors state them */
struct Edit {                                   /* one change in progress */
    uint16_t *map;
    int locks;
    int changed = 0;
    int c0 = MAP_W, r0 = MAP_H, c1 = -1, r1 = -1;   /* changed area */
    /* Painting a non-solid tile over an actor means "this behind it": the actor stays, and
     * the cell the game copies its background from gets the tile (finish_behind). */
    bool behind = false;
    std::vector<std::pair<int, uint16_t>> backs;    /* actor cells painted over, and with what */
    int spread = 0, walled = 0;                 /* source cells painted for them; ones left (a wall) */
    Edit(uint16_t *m, int l) : map(m), locks(l) {}
    bool set(int c, int r, uint16_t v);         /* respects the locks; true if it changed */
    void finish_behind();
};
int  backing_source(const uint16_t *map, int i);    /* the cell an actor's background comes from, -1 = none */
void op_line(Edit &e, int c0, int r0, int c1, int r1, uint16_t v);
void op_rect(Edit &e, int c0, int r0, int c1, int r1, uint16_t v, bool filled);
void op_fill(Edit &e, int c, int r, uint16_t v, bool everywhere);   /* same-value area, or all of that value */
void op_replace(Edit &e, uint16_t from, uint16_t to);

struct Selection {
    std::vector<uint8_t> on = std::vector<uint8_t>(ED_MAP_CELLS, 0);
    bool any = false;
    void clear() { std::fill(on.begin(), on.end(), 0); any = false; }
    void rect(int c0, int r0, int c1, int r1, int mode);            /* mode: 0 replace, 1 add, 2 remove */
    void magic(const uint16_t *map, int c, int r, int mode);        /* the connected same-value area */
    void matching(const uint16_t *map, uint16_t v);                 /* every cell holding v */
    void all();
    void update();
    bool bounds(int &c0, int &r0, int &c1, int &r1) const;          /* inclusive */
};

struct Clip {                                   /* copied cells */
    int w = 0, h = 0;
    std::vector<uint16_t> v;
    std::vector<uint8_t> on;                    /* which cells were selected */
    bool empty() const { return w == 0; }
    void copy(const uint16_t *map, const Selection &s);
    void flip_h();                              /* mirror; directional actors swap facing */
    void flip_v();
    void paste(Edit &e, int c, int r) const;    /* top-left at (c, r) */
};
uint16_t mirror_h(uint16_t v);                  /* the actor's left/right counterpart (or v) */
uint16_t mirror_v(uint16_t v);

/* actors (tools.cpp) */
uint16_t actor_flip(uint16_t id);               /* F: face or point the other way */
bool     actor_single(uint16_t id);             /* one per level: placing another moves it */
uint16_t uncovered(const uint16_t *after, int c, int r, uint16_t id, uint16_t erase);   /* a cell once actor id goes */
void     place_actor(Edit &e, const uint16_t *after, int c, int r, uint16_t id, uint16_t erase);
bool     span_actor(uint16_t id);               /* placed by dragging: a start and an end */
void     place_span(Edit &e, int c0, int c1, int r, uint16_t id);
void     count_actors(const uint16_t *map, int counts[ED_N_TABLES]);
const char *table_name(int t);

/* ---- history (doc.cpp) */
struct History {
    struct Step { std::string name; std::vector<uint16_t> map; };
    std::vector<Step> steps;
    int cur = 0, saved = 0;
    void start(const uint16_t *map, const std::string &name);
    void commit(const uint16_t *map, const std::string &name);
    bool modified() const { return cur != saved; }
    const uint16_t *go(int i);                  /* undo / redo / jump: the map to show */
};

/* ---- what the game's loader puts in an actor's own cell (checks.cpp) */
enum Fill { F_LEFT, F_RIGHT, F_ABOVE, F_BELOW, F_SET, F_KEEP };   /* copied from a neighbour, a fixed tile, left as is */
Fill actor_fill(uint16_t id, uint16_t *set = NULL);

/* ---- level checks (checks.cpp) */
enum CheckSev { CS_NOTE, CS_WARNING, CS_BROKEN };
struct Check {
    CheckSev sev;
    int c, r;                                   /* the cell responsible, -1 = the level as a whole */
    std::string what, why;
    uint16_t fix = 0;                           /* a one-click fix: put this in the cell */
};
struct CheckContext {
    bool dos = false;                           /* a DOS episode (the original EXE's rules) */
    bool episode = false;                       /* part of an episode, not a single level */
    bool last = true;                           /* the episode's last level (or a single level) */
    bool hallway = false;
    bool robohand_before = false;               /* an earlier level of the episode has the robot hand (Duke keeps it) */
};
void run_checks(const Episode &ep, const uint16_t *map, const CheckContext &cx, std::vector<Check> &out);
int  check_count(const std::vector<Check> &v, CheckSev s);

/* ---- projects (project.cpp): what is edited and saved */
const int PROJ_MAX_LEVELS = 99;
struct Clear {                                  /* a play from the player start to the end, recorded */
    std::vector<uint8_t> inputs;                /* the controls, a byte a frame (EdPlay.inputs); empty: none */
    EdLoadout lo = { 0, 0, 0, 0, 0, 1, 9, 0 };  /* what Duke started with */
    /* the Techbot settings it was played with that change what the game does:
     * what is active depends on the view (Wide and Immersive widen it) */
    int mode = 1, smooth = 0, widescreen = 0, hud = 0;
    int wide = 0, view_w = 208, view_h = 160;
};
struct ProjEntry {                              /* a level or a hallway */
    bool hallway = false;
    int backdrop = 1;                           /* a level: the backdrops of stock level 1..10 */
    std::vector<uint16_t> map;                  /* as last left (the open one: in the editor) */
    History hist;                               /* its undo history, kept while the project is open */
    Clear clear;                                /* the last clear recorded on it */
    /* a level (Techbot projects): backdrop A / B in place of its stock pair's -
     * "" none, a game file of any episode ("DROP5.DN2"), or with drop_data a
     * picture of its own (a backdrop file's bytes, kept in the project) */
    std::string drop[2], drop_data[2];
    bool own_note = false;                      /* Techbot projects: its own note / Dr. Proton text */
    std::string note[4];
};
/* a clear as a file (inside a project's zip) */
std::string clear_bytes(const Clear &c);
bool bytes_clear(const std::string &b, Clear &c);
struct Project {
    bool dos = false;                           /* DOS Compatible, else Techbot Compatible */
    bool episode = false;                       /* an episode, else a single level */
    int ep = 1;                                 /* whose art (and, for DOS, whose files) */
    std::string name, author;
    std::string path;                           /* .LEV / .EPI file or DOS folder; empty until saved */
    bool from_stock = false;                    /* a stock level or episode: the first save asks where */
    bool fresh = false;                         /* made new: likewise */
    int ending = 1;                             /* Techbot episode: stock ending 1..3, 0 = custom text */
    std::string ending_text;
    bool meta_dirty = false;                    /* levels added or removed, details changed */
    std::vector<ProjEntry> entries;             /* in play order: level, hallway, level... (DOS: ten levels, then the hallway) */
    int count(bool hallways) const;
    int number(int i) const;                    /* 1-based among levels, or among hallways */
    int slot_of(int i) const;                   /* the level table slot it plays as (backdrops) */
    std::string label(int i) const;
    std::string kind_name() const;
    CheckContext check_context(int i) const;
    bool untitled() const { return path.empty() || from_stock || fresh; }
};
const int ROOM_W = 16, ROOM_H = 5;              /* a new level's room, inside */
void empty_level(const Episode &ep, std::vector<uint16_t> &m);
void project_new(Project &p, const Episode &ep, bool dos, bool episode, int levels);
bool project_from_stock(Project &p, const Episode &ep, bool dos, int slot, std::string &err);   /* slot -1: the episode */
int  project_add_level(Project &p, const Episode &ep, int after);   /* the new level's entry, or -1 */
bool project_remove_level(Project &p, int i);
bool project_save_zip(const Project &p, const std::string &path, std::string &err, const std::string &extra = "", bool keep_bak = true);
bool project_load_zip(Project &p, const std::string &path, std::string &err, std::map<std::string, std::string> *top = NULL);
bool project_save_dos(const Project &p, const Episode &ep, const std::string &dir, std::string &err);
bool project_load_dos(Project &p, const Episode &ep, const std::string &dir, std::string &err, int *from_folder = NULL);
int  dos_folder_episode(const std::string &dir);   /* 1..3 by its map files, 0 = none */
bool same_folder(const std::string &a, const std::string &b);
bool path_is_dir(const std::string &p);
bool path_exists(const std::string &p);
long long path_time(const std::string &p);
void path_remove(const std::string &p);
std::string path_join(const std::string &a, const std::string &b);
std::string path_name(const std::string &p);
std::string path_dir(const std::string &p);
std::string path_ext(const std::string &p);   /* lower case, with the dot */

/* ---- native file dialogs (filedlg.cpp); empty string when cancelled */
std::string dialog_open_map(const std::string &dir);
std::string dialog_save_map(const std::string &dir, const std::string &name);
std::string dialog_open_project(const std::string &dir, bool episodes_only);   /* .LEV / .EPI */
std::string dialog_save_project(const std::string &dir, const std::string &name, bool episode);
std::string dialog_folder(const std::string &dir, const char *title);
std::string dialog_open_picture(const std::string &dir);   /* a .PNG */
bool launch_program(const std::vector<std::string> &args, std::string &err);   /* filedlg.cpp: not waited for */

/* ---- backdrops (picture.cpp): ED_BD_W x ED_BD_H pixels of colours 0..15 */
bool drop_decode(const std::string &file, uint8_t *px);           /* a backdrop file (DROPn.DNx) */
std::string drop_encode(const uint8_t *px);                         /* ... and back */
bool picture_to_drop(const std::string &path, bool dither, const uint32_t pal[16], uint8_t *px, std::string &err);
bool game_drop_pixels(const std::string &name, uint8_t *px);         /* one of the game folder's */
std::vector<std::string> game_drop_files();                         /* DROP*.DN1..3 in the game folder */
std::string stock_drop_name(int level, int which);                 /* the open episode's level 1..10: its A / B ("DROP1.DN1") */

/* --selftest: compare with the game's own drawing of every level, and test the editing */
int selftest(Episode &ep);
int selftest_projects(Episode &ep);    /* project files, DOS folders, the game folder untouched */
int selftest_checks(Episode &ep);      /* no stock level shows a Broken result */
int selftest_reach(Episode &ep);       /* every stock level's end can be reached */
int write_catalog(const char *path);   /* catalog.cpp: --catalog */
