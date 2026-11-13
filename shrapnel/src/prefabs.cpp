/*
 * prefabs.cpp - reusable pieces of level: the user's (files) and the ones
 * that come with Shrapnel (prefab_builtin.cpp).  See prefabs.h.
 */
#include "prefabs.h"
#include "shrapnel.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::vector<Prefab> prefabs;

std::string prefab_text(const Prefab &p)
{
    std::string s = "; Shrapnel prefab - see BUILDING.md\n";
    s += "name = " + p.name + "\n";
    s += "size = " + std::to_string(p.w) + " x " + std::to_string(p.h) + "\n";
    if (!p.from.empty()) s += "from = " + p.from + "\n";
    for (int r = 0; r < p.h; r++) {
        s += "row  =";
        for (int c = 0; c < p.w; c++) {
            char b[8];
            int i = r * p.w + c;
            if (p.on[i]) snprintf(b, sizeof b, " %04X", p.v[i]);
            else snprintf(b, sizeof b, " ....");
            s += b;
        }
        s += "\n";
    }
    return s;
}

static std::string trim(std::string t)
{
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r' || t.back() == '\n')) t.pop_back();
    size_t i = 0;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) i++;
    return t.substr(i);
}

bool prefab_parse(const std::string &text, Prefab &p)
{
    std::istringstream in(text);
    std::string line;
    std::vector<std::vector<std::string>> rows;
    p = Prefab();
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "name") p.name = v;
        else if (k == "from") p.from = v;
        else if (k == "row") {
            std::istringstream ws(v);
            std::vector<std::string> cells;
            std::string w;
            while (ws >> w) cells.push_back(w);
            rows.push_back(cells);
        }
    }
    if (rows.empty() || rows[0].empty()) return false;
    p.h = (int)rows.size();
    p.w = (int)rows[0].size();
    if (p.w > MAP_W || p.h > MAP_H) return false;
    p.v.assign(p.w * p.h, 0);
    p.on.assign(p.w * p.h, 0);
    for (int r = 0; r < p.h; r++) {
        if ((int)rows[r].size() != p.w) return false;
        for (int c = 0; c < p.w; c++) {
            const std::string &t = rows[r][c];
            if (t[0] == '.') continue;
            char *end;
            unsigned long x = strtoul(t.c_str(), &end, 16);
            if (*end || x > 0xFFFF || (x >= 0x3000 && x >= ED_ACTOR_END) || (x < 0x3000 && (x & 0x1F))) return false;
            p.v[r * p.w + c] = (uint16_t)x;
            p.on[r * p.w + c] = 1;
        }
    }
    if (p.name.empty()) p.name = "Prefab";
    return std::find(p.on.begin(), p.on.end(), 1) != p.on.end();
}

static bool cut_builtin(const PrefabBuiltin &b, std::string (*game_file)(const std::string &), Prefab &p)
{
    p = Prefab();
    p.name = b.name;
    p.builtin = true;
    p.w = b.w; p.h = b.h;
    p.v.assign(b.w * b.h, 0);
    p.on.assign(b.w * b.h, 0);
    for (int i = 0; i < b.w * b.h; i++) p.on[i] = b.mask[i] == '#';
    if (b.cells) { for (int i = 0; i < b.w * b.h; i++) p.v[i] = b.cells[i]; return true; }
    std::string path = game_file(b.from_file);   /* cut from the player's own game level */
    if (path.empty()) return false;
    uint16_t m[ED_MAP_CELLS];
    if (!read_map_file(path, m)) return false;
    for (int r = 0; r < b.h; r++)
        for (int c = 0; c < b.w; c++) p.v[r * b.w + c] = m[(b.r + r) * MAP_W + b.c + c];
    p.from = std::string(b.from_file) + " " + std::to_string(b.c) + " " + std::to_string(b.r);
    return true;
}

void prefabs_load(const std::string &dir, std::string (*game_file)(const std::string &))
{
    prefabs.clear();
    for (int i = 0; i < n_prefab_builtin; i++) {
        Prefab p;
        if (cut_builtin(prefab_builtin[i], game_file, p)) prefabs.push_back(p);
    }
    std::vector<Prefab> mine;
    std::error_code ec;
    for (auto &d : fs::directory_iterator(fs::u8path(dir), ec)) {
        if (d.path().extension() != ".prefab") continue;
        std::ifstream f(d.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        Prefab p;
        if (!prefab_parse(ss.str(), p)) continue;
        p.file = d.path().u8string();
        mine.push_back(p);
    }
    std::sort(mine.begin(), mine.end(), [](const Prefab &a, const Prefab &b) { return a.name < b.name; });
    prefabs.insert(prefabs.end(), mine.begin(), mine.end());
}

static std::string file_name_for(const std::string &dir, const std::string &name, const std::string &keep)
{
    std::string base;
    for (char ch : name) base += (isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
    base = trim(base);
    if (base.empty()) base = "Prefab";
    std::string path = (fs::u8path(dir) / fs::u8path(base + ".prefab")).u8string();
    for (int n = 2; fs::exists(fs::u8path(path)) && path != keep; n++)
        path = (fs::u8path(dir) / fs::u8path(base + " " + std::to_string(n) + ".prefab")).u8string();
    return path;
}

bool prefab_save(const std::string &dir, Prefab &p, std::string &err)
{
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    if (p.file.empty()) p.file = file_name_for(dir, p.name, "");
    std::ofstream f(fs::u8path(p.file), std::ios::binary);
    if (!f) { err = "Could not write " + p.file; return false; }
    f << prefab_text(p);
    return (bool)f;
}

bool prefab_remove(const Prefab &p)
{
    std::error_code ec;
    return !p.file.empty() && fs::remove(fs::u8path(p.file), ec);
}

bool prefab_rename(const std::string &dir, Prefab &p, const std::string &name, std::string &err)
{
    std::string old = p.file;
    p.name = name;
    p.file = file_name_for(dir, name, old);
    if (!prefab_save(dir, p, err)) { p.file = old; return false; }
    if (p.file != old) { std::error_code ec; fs::remove(fs::u8path(old), ec); }
    return true;
}
