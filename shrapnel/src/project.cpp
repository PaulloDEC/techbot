/*
 * project.cpp - what Shrapnel edits: a project of one level or an episode,
 * for one of two targets.
 *
 *   Techbot Compatible: saved as a .LEV (one level) or an .EPI (an episode),
 *     both zip files holding PACK.INI and the maps in the game's own format.
 *     An episode has any number of levels, each hallway its own map.
 *   DOS Compatible: an episode saved as the standard map files of one stock
 *     episode (ten levels and the one hallway they share), under the names
 *     that episode's EXE loads, to be copied over a DOS install.
 *
 * Stock levels and episodes open as templates: the first save asks where,
 * and nothing is ever written into the game folder Shrapnel reads.
 */
#include "shrapnel.h"
#include "miniz.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <string.h>

namespace fs = std::filesystem;

static fs::path upath(const std::string &s) { return fs::u8path(s); }

/* ---------------------------------------------------------------- entries */
int Project::count(bool hallways) const
{
    int n = 0;
    for (const ProjEntry &e : entries) n += e.hallway == hallways;
    return n;
}

int Project::number(int i) const                /* 1-based among its kind */
{
    int n = 0;
    for (int k = 0; k <= i && k < (int)entries.size(); k++) n += entries[k].hallway == entries[i].hallway;
    return n;
}

int Project::slot_of(int i) const
{
    const ProjEntry &e = entries[i];
    if (e.hallway) return std::min(number(i) - 1, 9) * 2 + 1;
    return (std::max(1, std::min(10, e.backdrop)) - 1) * 2;
}

std::string Project::label(int i) const
{
    if (!entries[i].hallway) return "Level " + std::to_string(number(i));
    if (dos) return "Hallway (between all levels)";
    return "Hallway " + std::to_string(number(i));
}

CheckContext Project::check_context(int i) const
{
    CheckContext cx;
    cx.dos = dos;
    cx.episode = episode;
    cx.hallway = entries[i].hallway;
    int last = -1;
    for (int k = 0; k < (int)entries.size(); k++) if (!entries[k].hallway) last = k;
    cx.last = !cx.hallway && i == last;
    for (int k = 0; k < i; k++)                 /* boots, claws and the robot hand stay with Duke */
        for (uint16_t v : entries[k].map) if (item_base(v) == 0x3020) { cx.robohand_before = true; break; }
    return cx;
}

std::string Project::kind_name() const
{
    if (dos) return "DOS episode";
    return episode ? "Techbot episode (.EPI)" : "Techbot level (.LEV)";
}

/* ---------------------------------------------------------------- building */
static bool stock_map(const Episode &ep, int slot, std::vector<uint16_t> &m)
{
    m.assign(ED_MAP_CELLS, 0);
    return read_map_file(game_path(ep.slot_file(slot).c_str()), m.data());
}

/* the wall the episode uses most, for a new level's room */
static uint16_t common_wall(const Episode &ep)
{
    std::map<uint16_t, int> n;
    std::vector<uint16_t> m;
    for (int s = 0; s <= 18; s += 2)
        if (stock_map(ep, s, m))
            for (uint16_t v : m) if (cell_kind(v) == CK_SOLID) n[v]++;
    uint16_t best = 0x1800;
    int most = 0;
    for (auto &kv : n) if (kv.second > most) { most = kv.second; best = kv.first; }
    return best;
}

/* An empty level: a small room in the middle of the map, as big inside as
 * the stock hallway's (16 x 5 cells), the player start at its left end and
 * the exit door at its right.  (The middle also keeps the game's opening
 * camera, 7 cells left of and 3 rows above the start, on the map.) */
void empty_level(const Episode &ep, std::vector<uint16_t> &m)
{
    uint16_t w = common_wall(ep);
    m.assign(ED_MAP_CELLS, 0);
    int c0 = (MAP_W - ROOM_W) / 2, r0 = (MAP_H - ROOM_H) / 2;           /* the inside's top-left */
    int c1 = c0 + ROOM_W - 1, r1 = r0 + ROOM_H - 1;
    for (int c = c0 - 1; c <= c1 + 1; c++) { m[(r0 - 1) * MAP_W + c] = w; m[(r1 + 1) * MAP_W + c] = w; }
    for (int r = r0; r <= r1; r++) { m[r * MAP_W + c0 - 1] = w; m[r * MAP_W + c1 + 1] = w; }
    m[r1 * MAP_W + c0 + 1] = 0x3032;               /* player start, standing on the floor */
    m[r1 * MAP_W + c1 - 2] = 0x3011;               /* exit door (two cells wide) */
}

static ProjEntry make_entry(bool hallway, int backdrop, const std::vector<uint16_t> &m)
{
    ProjEntry e;
    e.hallway = hallway;
    e.backdrop = backdrop;
    e.map = m;
    return e;
}

void project_new(Project &p, const Episode &ep, bool dos, bool episode, int levels)
{
    p = Project();
    p.dos = dos;
    p.episode = episode || dos;
    p.ep = ep.number;
    p.ending = ep.number;
    p.name = dos ? "New DOS episode" : p.episode ? "New episode" : "New level";
    if (dos) levels = 10;
    if (!p.episode) levels = 1;
    levels = std::max(1, std::min(PROJ_MAX_LEVELS, levels));
    std::vector<uint16_t> room, hall;
    empty_level(ep, room);
    if (!stock_map(ep, 1, hall)) hall.assign(ED_MAP_CELLS, 0);
    for (int i = 0; i < levels; i++) {
        p.entries.push_back(make_entry(false, i % 10 + 1, room));
        if (p.episode && !dos && i + 1 < levels) p.entries.push_back(make_entry(true, 1, hall));
    }
    if (dos) p.entries.push_back(make_entry(true, 1, hall));
    p.fresh = true;
}

bool project_from_stock(Project &p, const Episode &ep, bool dos, int slot, std::string &err)
{
    p = Project();
    p.dos = dos;
    p.ep = ep.number;
    p.ending = ep.number;
    p.from_stock = true;
    std::vector<uint16_t> m;
    if (slot >= 0) {                                    /* one level */
        if (slot & 1 || slot > 18) { err = "Only the ten levels can be opened on their own."; return false; }
        if (!stock_map(ep, slot, m)) { err = "Could not read " + ep.slot_file(slot) + " from the game folder."; return false; }
        p.entries.push_back(make_entry(false, slot / 2 + 1, m));
        p.name = "Episode " + std::to_string(ep.number) + ", level " + std::to_string(slot / 2 + 1);
        return true;
    }
    p.episode = true;
    p.name = "Episode " + std::to_string(ep.number);
    for (int k = 0; k < 10; k++) {
        if (!stock_map(ep, k * 2, m)) { err = "Could not read " + ep.slot_file(k * 2) + " from the game folder."; return false; }
        p.entries.push_back(make_entry(false, k + 1, m));
        if (!dos && k < 9) {
            if (!stock_map(ep, k * 2 + 1, m)) { err = "Could not read " + ep.slot_file(k * 2 + 1) + "."; return false; }
            p.entries.push_back(make_entry(true, 1, m));
        }
    }
    if (dos) {
        if (!stock_map(ep, 1, m)) { err = "Could not read " + ep.slot_file(1) + "."; return false; }
        p.entries.push_back(make_entry(true, 1, m));
    }
    return true;
}

/* a level added after entry i (Techbot episodes): a hallway, then the level */
int project_add_level(Project &p, const Episode &ep, int i)
{
    if (p.dos || !p.episode || p.count(false) >= PROJ_MAX_LEVELS) return -1;
    std::vector<uint16_t> room, hall;
    empty_level(ep, room);
    if (!stock_map(ep, 1, hall)) hall.assign(ED_MAP_CELLS, 0);
    if (i < 0 || i >= (int)p.entries.size()) i = (int)p.entries.size() - 1;
    if (p.entries[i].hallway) i--;                      /* after the level before the hallway */
    int nb = p.count(false) % 10 + 1;
    p.entries.insert(p.entries.begin() + i + 1, make_entry(false, nb, room));
    p.entries.insert(p.entries.begin() + i + 1, make_entry(true, 1, hall));
    p.meta_dirty = true;
    return i + 2;                                       /* the new level */
}

/* level i removed, with the hallway before it (or after, for the first) */
bool project_remove_level(Project &p, int i)
{
    if (p.dos || !p.episode || i < 0 || i >= (int)p.entries.size() || p.entries[i].hallway || p.count(false) < 2) return false;
    int h = i > 0 ? i - 1 : i + 1;
    int a = std::min(i, h), b = std::max(i, h);
    p.entries.erase(p.entries.begin() + a, p.entries.begin() + b + 1);
    p.meta_dirty = true;
    return true;
}

/* ---------------------------------------------------------------- PACK.INI */
static std::string map_name(const Project &p, int i)
{
    char b[32];
    snprintf(b, sizeof b, "%s%02d.DN%d", p.entries[i].hallway ? "HALL" : "LEVEL", p.number(i), p.ep);
    return b;
}

/* ---------------------------------------------------------------- clears
 * "SHRAPNEL CLEAR 1", a line of what Duke starts with, then the controls,
 * a byte a frame. */
std::string clear_bytes(const Clear &c)
{
    char head[320];
    snprintf(head, sizeof head, "SHRAPNEL CLEAR 2\nboots %d claws %d hand %d card %d keys %d firepower %d health %d god %d\n"
             "view mode %d smooth %d widescreen %d hud %d wide %d w %d h %d\n",
             c.lo.boots, c.lo.claws, c.lo.robohand, c.lo.card, c.lo.keys, c.lo.firepower, c.lo.health, c.lo.god,
             c.mode, c.smooth, c.widescreen, c.hud, c.wide, c.view_w, c.view_h);
    return std::string(head) + std::string(c.inputs.begin(), c.inputs.end());
}
bool bytes_clear(const std::string &b, Clear &c)
{
    size_t a = b.find('\n'), e = a == std::string::npos ? a : b.find('\n', a + 1);
    if (e == std::string::npos) return false;
    int version = b.compare(0, a, "SHRAPNEL CLEAR 1") == 0 ? 1 : b.compare(0, a, "SHRAPNEL CLEAR 2") == 0 ? 2 : 0;
    if (!version) return false;
    Clear n;
    if (sscanf(b.substr(a + 1, e - a - 1).c_str(), "boots %d claws %d hand %d card %d keys %d firepower %d health %d god %d",
               &n.lo.boots, &n.lo.claws, &n.lo.robohand, &n.lo.card, &n.lo.keys, &n.lo.firepower, &n.lo.health, &n.lo.god) != 8)
        return false;
    if (version == 2) {
        size_t v = b.find('\n', e + 1);
        if (v == std::string::npos || sscanf(b.substr(e + 1, v - e - 1).c_str(), "view mode %d smooth %d widescreen %d hud %d wide %d w %d h %d",
                                             &n.mode, &n.smooth, &n.widescreen, &n.hud, &n.wide, &n.view_w, &n.view_h) != 7)
            return false;
        e = v;
    }
    n.inputs.assign(b.begin() + e + 1, b.end());
    if (n.inputs.size() > ED_INPUTS) return false;
    c = std::move(n);
    return true;
}
static std::string clear_name(const Project &p, int i)
{
    std::string m = map_name(p, i);
    size_t dot = m.rfind('.');
    return (dot == std::string::npos ? m : m.substr(0, dot)) + ".CLR";
}

/* a level's own backdrop picture, inside the zip */
static std::string drop_name(const Project &p, int i, int k)
{
    std::string m = map_name(p, i);
    return m.substr(0, m.rfind('.')) + (k ? "B" : "A") + ".DRP";
}
static bool has_drop(const Project &p, int i, int k)
{
    return !p.dos && !p.entries[i].hallway && (!p.entries[i].drop[k].empty() || !p.entries[i].drop_data[k].empty());
}

static std::string ini_text(const Project &p, const std::string &extra)
{
    std::ostringstream o;
    std::string et = p.ending_text;
    for (char &ch : et) if (ch == '\n') ch = '|';
    o << "; Shrapnel project - see BUILDING.md\n";
    o << "name    = " << p.name << "\n";
    o << "author  = " << p.author << "\n";
    o << "target  = " << (p.dos ? "classic" : "enhanced") << "\n";
    o << "kind    = " << (p.episode ? "episode" : "level") << "\n";
    o << "episode = " << p.ep << "\n";
    if (p.episode && !p.dos) {
        o << "ending  = " << (p.ending >= 1 && p.ending <= 3 ? std::to_string(p.ending) : std::string("custom")) << "\n";
        if (!et.empty()) o << "ending_text = " << et << "\n";
    }
    o << extra;
    for (int i = 0; i < (int)p.entries.size(); i++) {
        const ProjEntry &e = p.entries[i];
        o << "\n[" << (e.hallway ? "hallway " : "level ") << p.number(i) << "]\n";
        o << "map      = " << map_name(p, i) << "\n";
        if (!e.hallway) o << "backdrop = " << e.backdrop << "\n";
        for (int k = 0; k < 2; k++)
            if (has_drop(p, i, k))
                o << (k ? "backdrop_b = " : "backdrop_a = ") << (e.drop_data[k].empty() ? e.drop[k] : drop_name(p, i, k)) << "\n";
        if (e.own_note && !p.dos) {
            o << "note     = ";
            for (int k = 0; k < 4; k++) o << (k ? "|" : "") << e.note[k];
            o << "\n";
        }
        if (!e.clear.inputs.empty()) o << "clear    = " << clear_name(p, i) << "\n";
    }
    return o.str();
}

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::string lower(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }

/* sections in file order: "" for the top, then "level 1", "hallway 1"... */
typedef std::vector<std::pair<std::string, std::map<std::string, std::string>>> Ini;

static Ini ini_parse(const std::string &text)
{
    Ini ini(1);
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[') {
            size_t e = t.find(']');
            ini.push_back({ lower(trim(t.substr(1, e == std::string::npos ? std::string::npos : e - 1))), {} });
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string v = trim(t.substr(eq + 1));
        if (lower(trim(t.substr(0, eq))) != "ending_text" && lower(trim(t.substr(0, eq))) != "name" &&
            lower(trim(t.substr(0, eq))) != "author" && lower(trim(t.substr(0, eq))) != "note") {
            size_t sc = v.find(';');                    /* a comment after the value */
            if (sc != std::string::npos) v = trim(v.substr(0, sc));
        }
        ini.back().second[lower(trim(t.substr(0, eq)))] = v;
    }
    return ini;
}

/* ---------------------------------------------------------------- files */
static bool read_all(const std::string &path, std::string &out)
{
    std::ifstream f(upath(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

/* written beside the target first, then moved over it: a failed save never
 * leaves half a file; the previous version is kept as <name>.bak */
static bool write_all(const std::string &path, const void *data, size_t n, bool keep_bak, std::string &err)
{
    std::error_code ec;
    fs::path p = upath(path), tmp = upath(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write((const char *)data, (std::streamsize)n) || !(f.flush())) {
            err = "Could not write " + path;
            fs::remove(tmp, ec);
            return false;
        }
    }
    if (keep_bak && fs::exists(p, ec)) fs::copy_file(p, upath(path + ".bak"), fs::copy_options::overwrite_existing, ec);
    fs::rename(tmp, p, ec);
    if (ec) {                                       /* Windows will not rename over a file */
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
        if (ec) { err = "Could not replace " + path; return false; }
    }
    return true;
}

static void map_bytes(const std::vector<uint16_t> &m, std::string &b)
{
    b.resize(ED_MAP_CELLS * 2);
    for (int i = 0; i < ED_MAP_CELLS; i++) { b[i * 2] = (char)(m[i] & 0xFF); b[i * 2 + 1] = (char)(m[i] >> 8); }
}

static bool bytes_map(const void *d, size_t n, std::vector<uint16_t> &m)
{
    if (n != ED_MAP_CELLS * 2) return false;
    const uint8_t *b = (const uint8_t *)d;
    m.resize(ED_MAP_CELLS);
    for (int i = 0; i < ED_MAP_CELLS; i++) m[i] = (uint16_t)(b[i * 2] | b[i * 2 + 1] << 8);
    return true;
}

bool project_save_zip(const Project &p, const std::string &path, std::string &err, const std::string &extra, bool keep_bak)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_heap(&z, 0, 64 * 1024)) { err = "Could not start the zip"; return false; }
    std::string ini = ini_text(p, extra), b;
    bool ok = mz_zip_writer_add_mem(&z, "PACK.INI", ini.data(), ini.size(), MZ_BEST_COMPRESSION) != 0;
    for (int i = 0; i < (int)p.entries.size() && ok; i++) {
        map_bytes(p.entries[i].map, b);
        ok = mz_zip_writer_add_mem(&z, map_name(p, i).c_str(), b.data(), b.size(), MZ_BEST_COMPRESSION) != 0;
        if (ok && !p.entries[i].clear.inputs.empty()) {
            b = clear_bytes(p.entries[i].clear);
            ok = mz_zip_writer_add_mem(&z, clear_name(p, i).c_str(), b.data(), b.size(), MZ_BEST_COMPRESSION) != 0;
        }
        for (int k = 0; k < 2 && ok; k++)
            if (has_drop(p, i, k) && !p.entries[i].drop_data[k].empty())
                ok = mz_zip_writer_add_mem(&z, drop_name(p, i, k).c_str(), p.entries[i].drop_data[k].data(),
                                           p.entries[i].drop_data[k].size(), MZ_BEST_COMPRESSION) != 0;
    }
    void *buf = NULL;
    size_t n = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&z, &buf, &n);
    if (ok) ok = write_all(path, buf, n, keep_bak, err);
    else err = "Could not build the zip";
    mz_zip_writer_end(&z);
    return ok;
}

static bool zip_get(mz_zip_archive &z, const std::string &name, std::string &out)
{
    int i = mz_zip_reader_locate_file(&z, name.c_str(), NULL, 0);    /* case-insensitive */
    if (i < 0) return false;
    size_t n = 0;
    void *d = mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &n, 0);
    if (!d) return false;
    out.assign((const char *)d, n);
    mz_free(d);
    return true;
}

bool project_load_zip(Project &p, const std::string &path, std::string &err, std::map<std::string, std::string> *top)
{
    std::string data, ini, mb;
    if (!read_all(path, data)) { err = "Could not read " + path; return false; }
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_mem(&z, data.data(), data.size(), 0)) { err = "Not a Shrapnel project (not a zip file): " + path; return false; }
    bool ok = zip_get(z, "PACK.INI", ini);
    if (!ok) err = "Not a Shrapnel project (no PACK.INI inside): " + path;
    Project q;
    if (ok) {
        Ini s = ini_parse(ini);
        auto &t = s[0].second;
        q.name = t["name"];
        q.author = t["author"];
        q.dos = lower(t["target"]) == "classic";
        q.episode = q.dos || lower(t["kind"]) == "episode";
        q.ep = atoi(t["episode"].c_str());
        q.ending = lower(t["ending"]) == "custom" ? 0 : t["ending"].empty() ? q.ep : atoi(t["ending"].c_str());
        q.ending_text = t["ending_text"];
        for (char &ch : q.ending_text) if (ch == '|') ch = '\n';
        if (top) *top = t;
        if (q.ep < 1 || q.ep > 3) { err = "PACK.INI names no episode (1, 2 or 3)."; ok = false; }
        for (size_t k = 1; k < s.size() && ok; k++) {
            const std::string &sec = s[k].first;
            bool hall = sec.compare(0, 7, "hallway") == 0;
            if (!hall && sec.compare(0, 5, "level") != 0) continue;
            auto &kv = s[k].second;
            ProjEntry e;
            e.hallway = hall;
            e.backdrop = kv.count("backdrop") ? atoi(kv["backdrop"].c_str()) : 1;
            if (e.backdrop < 1 || e.backdrop > 10) e.backdrop = 1;
            if (!zip_get(z, kv["map"], mb) || !bytes_map(mb.data(), mb.size(), e.map)) {
                err = "The map " + kv["map"] + " for [" + sec + "] is missing or not 23,040 bytes.";
                ok = false;
                break;
            }
            if (kv.count("clear") && zip_get(z, kv["clear"], mb)) bytes_clear(mb, e.clear);   /* (a bad one is dropped) */
            for (int d = 0; d < 2 && !hall; d++) {      /* a picture in the zip, else a game file's name */
                const char *key = d ? "backdrop_b" : "backdrop_a";
                if (!kv.count(key) || kv[key].empty()) continue;
                if (zip_get(z, kv[key], mb)) e.drop_data[d] = mb;
                else e.drop[d] = kv[key];
            }
            if (kv.count("note")) {
                std::string n = kv["note"];
                e.own_note = true;
                for (int l = 0; l < 4; l++) {
                    size_t bar = n.find('|');
                    e.note[l] = n.substr(0, bar);
                    n = bar == std::string::npos ? std::string() : n.substr(bar + 1);
                }
            }
            q.entries.push_back(std::move(e));
        }
        if (ok && q.count(false) == 0) { err = "PACK.INI lists no levels."; ok = false; }
        if (ok && !q.episode && (q.count(false) != 1 || q.count(true))) { err = "A .LEV holds exactly one level."; ok = false; }
        if (ok && q.dos && (q.count(false) != 10 || q.count(true) != 1)) { err = "A DOS episode has ten levels and one hallway."; ok = false; }
    }
    mz_zip_reader_end(&z);
    if (!ok) return false;
    q.path = path;
    p = std::move(q);
    return true;
}

/* ---------------------------------------------------------------- DOS folders */
static std::string find_ci(const std::string &dir, const std::string &name)   /* a file in dir, any case */
{
    std::error_code ec;
    for (auto &d : fs::directory_iterator(upath(dir), ec))
        if (lower(d.path().filename().u8string()) == lower(name)) return d.path().u8string();
    return "";
}

bool same_folder(const std::string &a, const std::string &b)
{
    std::error_code e1, e2;
    fs::path x = fs::weakly_canonical(upath(a), e1), y = fs::weakly_canonical(upath(b), e2);
    if (e1 || e2) return false;
#ifdef _WIN32
    return lower(x.u8string()) == lower(y.u8string());
#else
    return x == y;
#endif
}

int dos_folder_episode(const std::string &dir)
{
    for (int e = 1; e <= 3; e++)
        if (!find_ci(dir, "WORLDAL1.DN" + std::to_string(e)).empty()) return e;
    return 0;
}

static std::string dos_name(const Project &p, const Episode &ep, int i)
{
    return p.entries[i].hallway ? ep.slot_file(1) : ep.slot_file((p.number(i) - 1) * 2);
}

bool project_save_dos(const Project &p, const Episode &ep, const std::string &dir, std::string &err)
{
    if (!p.dos || ep.number != p.ep) { err = "Not a DOS episode for the loaded episode."; return false; }
    if (same_folder(dir, shrapnel_game_dir)) {
        err = "That is the game folder Shrapnel reads the stock levels from. Choose another folder "
              "(copy the result over a copy of the game).";
        return false;
    }
    std::error_code ec;
    fs::create_directories(upath(dir), ec);
    std::string b;
    for (int i = 0; i < (int)p.entries.size(); i++) {
        std::string have = find_ci(dir, dos_name(p, ep, i));
        std::string path = have.empty() ? (upath(dir) / upath(dos_name(p, ep, i))).u8string() : have;
        map_bytes(p.entries[i].map, b);
        if (!write_all(path, b.data(), b.size(), false, err)) return false;
    }
    /* the recorded clears, beside the maps (the game never looks at it) */
    std::string clr = (upath(dir) / upath("SHRAPNEL.CLR")).u8string();
    bool any = false;
    for (const ProjEntry &e : p.entries) any |= !e.clear.inputs.empty();
    if (!any) { std::error_code e2; fs::remove(upath(clr), e2); return true; }
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_heap(&z, 0, 16 * 1024)) return true;
    bool ok = true;
    for (int i = 0; i < (int)p.entries.size() && ok; i++) {
        if (p.entries[i].clear.inputs.empty()) continue;
        char n[32];
        snprintf(n, sizeof n, "CLEAR%02d.CLR", i);
        b = clear_bytes(p.entries[i].clear);
        ok = mz_zip_writer_add_mem(&z, n, b.data(), b.size(), MZ_BEST_COMPRESSION) != 0;
    }
    void *buf = NULL;
    size_t n = 0;
    if (ok && mz_zip_writer_finalize_heap_archive(&z, &buf, &n)) { std::string e3; write_all(clr, buf, n, false, e3); }
    mz_zip_writer_end(&z);
    return true;
}

/* A DOS episode folder: the maps it has, the game folder's for any it lacks
 * (a folder holding just the changed levels opens too). */
bool project_load_dos(Project &p, const Episode &ep, const std::string &dir, std::string &err, int *from_folder)
{
    Project q;
    q.dos = q.episode = true;
    q.ep = ep.number;
    q.ending = ep.number;
    q.path = dir;
    q.name = upath(dir).filename().u8string();
    if (q.name.empty()) q.name = "DOS episode";
    int found = 0;
    for (int k = 0; k <= 10; k++) {
        int slot = k < 10 ? k * 2 : 1;
        std::string f = find_ci(dir, ep.slot_file(slot)), data;
        ProjEntry e;
        e.hallway = k == 10;
        e.backdrop = k < 10 ? k + 1 : 1;
        if (!f.empty() && read_all(f, data) && bytes_map(data.data(), data.size(), e.map)) found++;
        else if (!stock_map(ep, slot, e.map)) { err = "Could not read " + ep.slot_file(slot) + "."; return false; }
        q.entries.push_back(std::move(e));
    }
    if (!found) { err = "No map files of episode " + std::to_string(ep.number) + " in " + dir; return false; }
    if (from_folder) *from_folder = found;
    std::string clr = find_ci(dir, "SHRAPNEL.CLR"), data;   /* the recorded clears */
    if (!clr.empty() && read_all(clr, data)) {
        mz_zip_archive z;
        memset(&z, 0, sizeof z);
        if (mz_zip_reader_init_mem(&z, data.data(), data.size(), 0)) {
            for (int i = 0; i < (int)q.entries.size(); i++) {
                char n[32];
                std::string b;
                snprintf(n, sizeof n, "CLEAR%02d.CLR", i);
                if (zip_get(z, n, b)) bytes_clear(b, q.entries[i].clear);
            }
            mz_zip_reader_end(&z);
        }
    }
    p = std::move(q);
    return true;
}

bool path_is_dir(const std::string &p) { std::error_code ec; return fs::is_directory(upath(p), ec); }
bool path_exists(const std::string &p) { std::error_code ec; return fs::exists(upath(p), ec); }
long long path_time(const std::string &p)
{
    std::error_code ec;
    auto t = fs::last_write_time(upath(p), ec);
    return ec ? 0 : (long long)t.time_since_epoch().count();
}
void path_remove(const std::string &p) { std::error_code ec; fs::remove(upath(p), ec); }
std::string path_join(const std::string &a, const std::string &b) { return (upath(a) / upath(b)).u8string(); }
std::string path_name(const std::string &p) { return upath(p).filename().u8string(); }
std::string path_dir(const std::string &p)
{
    std::string d = upath(p).parent_path().u8string();
    return d.empty() ? std::string(".") : d;
}
std::string path_ext(const std::string &p) { return lower(upath(p).extension().u8string()); }
