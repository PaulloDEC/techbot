/*
 * prefabs.h - reusable pieces of level.
 *
 * A prefab is a named block of cells (with the ones not part of it left
 * out).  The user's own are files in Shrapnel's settings folder
 * (prefabs/<name>.prefab, plain text).  The ones that come with Shrapnel
 * (prefab_builtin.cpp, made by tools/prefab_pack.py) are of two kinds: ones
 * drawn from scratch, which carry their cells, and ones cut from the game's
 * own levels, which carry only where to cut them - the cells are read from
 * the player's own game files when Shrapnel starts, so nothing of the game
 * is shipped.
 *
 * File format (one prefab per file):
 *     ; Shrapnel prefab
 *     name = Exit room
 *     size = 6 x 4
 *     from = WORLDAL3.DN1 12 40      (where it was cut, if it matches a game level there; optional)
 *     row  = 1800 1800 1800 1800 1800 1800
 *     row  = 1800 0000 0000 3013 .... 1800      (.... = not part of it)
 *     ...
 */
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct Prefab {
    std::string name;
    std::string file;                   /* the user's: its file; built in: "" */
    bool builtin = false;
    int w = 0, h = 0;
    std::vector<uint16_t> v;            /* w * h cells */
    std::vector<uint8_t> on;            /* which are part of it */
    std::string from;                   /* "WORLDAL3.DN1 12 40", or "" */
};

/* a built-in prefab, as tools/prefab_pack.py writes it */
struct PrefabBuiltin {
    const char *name;
    const char *from_file;              /* cut from this game level (or NULL: cells below) */
    int c, r, w, h;
    const char *mask;                   /* w*h of '#' (part of it) and '.', row by row */
    const uint16_t *cells;              /* drawn from scratch: w*h cells; else NULL */
};
extern const PrefabBuiltin prefab_builtin[];
extern const int n_prefab_builtin;

extern std::vector<Prefab> prefabs;     /* built in (those whose game files are here), then the user's */

/* read them all again: the built-in ones cut from the game files (found
 * through `game_file`, which gives a game file's path or "" if it isn't
 * there), and the user's from `dir` */
void prefabs_load(const std::string &dir, std::string (*game_file)(const std::string &name));
bool prefab_save(const std::string &dir, Prefab &p, std::string &err);     /* sets p.file */
bool prefab_remove(const Prefab &p);
bool prefab_rename(const std::string &dir, Prefab &p, const std::string &name, std::string &err);
std::string prefab_text(const Prefab &p);
bool prefab_parse(const std::string &text, Prefab &p);
