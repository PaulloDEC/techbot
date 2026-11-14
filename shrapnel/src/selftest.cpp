/*
 * selftest.cpp - shrapnel --selftest: Shrapnel's drawing of every level
 * against the game's own.
 *
 * For each level table slot, the game draws its play window (draw_world, the
 * routine play uses) with the camera at a grid of positions covering the
 * whole map, at each of the four animation phases.  Every pixel must match
 * Shrapnel's composed level: tiles at their place in the map, backdrop cells
 * against the backdrop as the game shows it (fixed to the screen).  Actor
 * sprites are not part of draw_world; their looks are captured from the game
 * directly (ed_actor_look).
 */
#include "shrapnel.h"
#include "prefabs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <ctype.h>
#include <functional>
#include <chrono>

static int edit_tests(Episode &ep);

int selftest(Episode &ep)
{
    std::vector<uint32_t> img(LEVEL_PX_W * LEVEL_PX_H);
    static uint8_t view[320 * 200];
    uint32_t pal[16];
    long views = 0, bad_px = 0;
    int bad_slots = 0;
    Level *lv = new Level;
    std::vector<int> cols, rows;          /* camera positions: every 13 x 10 block, plus the far edges */
    for (int c = 0; c < MAP_W - 13; c += 13) cols.push_back(c);
    cols.push_back(MAP_W - 13);
    for (int r = 0; r < MAP_H - 10; r += 10) rows.push_back(r);
    rows.push_back(MAP_H - 10);
    for (int i = 0; i < 16; i++) pal[i] = 0xFF000000u | ep.pal[i];
    for (int slot = 0; slot <= 20; slot++) {
        long slot_bad = 0;
        if (!lv->load(ep, slot)) { printf("episode %d slot %2d: cannot load %s\n", ep.number, slot, ep.slot_file(slot).c_str()); bad_slots++; continue; }
        for (int anim = 0; anim < 0x80; anim += 0x20) {
            compose_level(ep, *lv, anim, false, img.data());
            for (int r0 : rows)
                for (int c0 : cols) {
                    ed_game_view(c0 * 2, r0 << 7, anim, view);
                    views++;
                    for (int y = 0; y < 160; y++)
                        for (int x = 0; x < 208; x++) {
                            int c = c0 + x / 16, r = r0 + y / 16;
                            uint16_t v = lv->after[r * MAP_W + c];
                            uint32_t want = v <= 0x20 ? pal[lv->backdrop[v == 0x20][y * ED_BD_W + x]]
                                                      : img[(r0 * 16 + y) * LEVEL_PX_W + c0 * 16 + x];
                            if (pal[view[(16 + y) * 320 + 16 + x] & 15] != want) slot_bad++;
                        }
                }
        }
        printf("episode %d slot %2d %-13s %s\n", ep.number, slot, lv->file.c_str(),
               slot_bad ? "DIFFERS" : "matches");
        if (slot_bad) { bad_slots++; bad_px += slot_bad; }
    }
    delete lv;
    printf("episode %d: %ld game views compared, %d slot(s) differ (%ld pixels)\n", ep.number, views, bad_slots, bad_px);
    return (bad_slots ? 1 : 0) | edit_tests(ep);
}

/* ---- editing: saving, undo, and redrawing only what changed */
static bool same_file(const std::string &a, const std::string &b)
{
    FILE *fa = fopen(a.c_str(), "rb"), *fb = fopen(b.c_str(), "rb");
    bool same = fa && fb;
    while (same) {
        int x = fgetc(fa), y = fgetc(fb);
        if (x != y) same = false;
        if (x == EOF || y == EOF) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static int edit_tests(Episode &ep)
{
    int fails = 0;
    Level *lv = new Level;
    std::string tmp = std::string(shrapnel_game_dir) + "/shrapnel_selftest.tmp";

    /* 1. open and save every slot unchanged: byte-identical */
    int same = 0;
    for (int slot = 0; slot <= 20; slot++) {
        if (!lv->load(ep, slot) || !write_map_file(tmp, lv->map)) { fails = 1; continue; }
        if (same_file(tmp, lv->path)) same++;
        else { printf("episode %d slot %d: saved copy differs from %s\n", ep.number, slot, lv->file.c_str()); fails = 1; }
    }
    remove(tmp.c_str());
    printf("episode %d: %d of 21 maps saved back byte-identical\n", ep.number, same);

    /* 2. random edits with every tool: each history step must give back
     *    exactly the map it recorded, through undo, redo and jumps; and the
     *    picture redrawn piecemeal must equal one drawn whole */
    srand(1234 + ep.number);
    for (int slot : { 0, 12 }) {
        lv->load(ep, slot);
        History h;
        h.start(lv->map, "opened");
        std::vector<std::vector<uint16_t>> want(1, std::vector<uint16_t>(lv->map, lv->map + ED_MAP_CELLS));
        std::vector<uint32_t> inc(LEVEL_PX_W * LEVEL_PX_H), full(LEVEL_PX_W * LEVEL_PX_H);
        compose_level(ep, *lv, 0, true, inc.data());
        Selection sel;
        Clip clip;
        int redraw_bad = 0, steps = 300;
        static const uint16_t picks[] = { 0, 0x20, 0x80, 0x420, 0x600, 0x1000, 0x1800, 0x1AE0, 0x2400, 0x2FE0,
                                          0x3000, 0x3010, 0x3016, 0x3019, 0x3024, 0x302F, 0x3036, 0x3044, 0x3058 };
        for (int k = 0; k < steps; k++) {
            Edit e(lv->map, rand() % 4 == 0 ? rand() % 16 : 0);
            uint16_t v = picks[rand() % (sizeof picks / sizeof picks[0])];
            int c = rand() % MAP_W, r = rand() % MAP_H, c2 = rand() % MAP_W, r2 = rand() % MAP_H;
            switch (rand() % 7) {
            case 0: e.set(c, r, v); break;
            case 1: op_line(e, c, r, c2, r2, v); break;
            case 2: op_rect(e, c, r, c + rand() % 12, r + rand() % 8, v, rand() & 1); break;
            case 3: op_fill(e, c, r, v, rand() % 5 == 0); break;
            case 4: sel.magic(lv->map, c, r, 0); clip.copy(lv->map, sel); if (rand() & 1) clip.flip_h(); if (rand() & 1) clip.flip_v(); clip.paste(e, c2, r2); break;
            case 5: sel.rect(c, r, c + rand() % 10, r + rand() % 6, 0); clip.copy(lv->map, sel); clip.paste(e, c2, r2); break;
            default: op_replace(e, lv->at(c, r), v); break;
            }
            if (!e.changed) continue;
            h.commit(lv->map, "step");
            want.push_back(std::vector<uint16_t>(lv->map, lv->map + ED_MAP_CELLS));
            int a0 = e.c0, b0 = e.r0, a1 = e.c1, b1 = e.r1;
            if (level_changed(ep, *lv, a0, b0, a1, b1)) compose_area(ep, *lv, 0, true, inc.data(), a0, b0, a1, b1);
            if (k % 10 == 0) {                  /* (a whole redraw is slow: check every tenth step) */
                compose_level(ep, *lv, 0, true, full.data());
                if (inc != full) { redraw_bad++; inc = full; }
            }
        }
        int hist_bad = 0, n = (int)h.steps.size();
        for (int i = n - 1; i >= 0; i--) if (memcmp(h.go(i), want[i].data(), ED_MAP_CELLS * 2)) hist_bad++;   /* undo all */
        for (int i = 0; i < n; i++) if (memcmp(h.go(i), want[i].data(), ED_MAP_CELLS * 2)) hist_bad++;        /* redo all */
        for (int j = 0; j < 200; j++) { int i = rand() % n; if (memcmp(h.go(i), want[i].data(), ED_MAP_CELLS * 2)) hist_bad++; }
        uint16_t orig[ED_MAP_CELLS];
        read_map_file(lv->path, orig);
        if (memcmp(h.go(0), orig, sizeof orig)) hist_bad++;    /* the first step is the file as opened */
        printf("episode %d slot %2d: %d edits, history %s, piecemeal redraw %s\n", ep.number, slot, n - 1,
               hist_bad ? "WRONG" : "exact", redraw_bad ? "DIFFERS" : "matches");
        if (hist_bad || redraw_bad) fails = 1;
    }
    /* 3. every actor: placed (with its partner for conveyors and the
     *    walkway) beside the player start of level 1, loaded by the game's
     *    loader, then removed again.  With SHRAPNEL_ACTOR_MAPS=<dir> set,
     *    each level with its actor is also written there (one file per ID)
     *    for playing in Techbot. */
    {
        const char *dir = getenv("SHRAPNEL_ACTOR_MAPS");
        int ok = 0;
        lv->load(ep, 0);
        uint16_t orig[ED_MAP_CELLS];
        memcpy(orig, lv->map, sizeof orig);
        int sc = 0, sr = 0;
        for (int i = 0; i < ED_MAP_CELLS; i++) if (orig[i] == 0x3032) { sc = i % MAP_W; sr = i / MAP_W; }
        for (int id = 0x3000; id < 0x3000 + N_ACTORS; id++) {
            memcpy(lv->map, orig, sizeof orig);
            lv->refresh();
            Edit e(lv->map, 0);
            int c = sc + 3, r = sr;
            if (span_actor((uint16_t)id)) place_span(e, c, c + 3, r, (uint16_t)id);
            else place_actor(e, lv->after, c, r, (uint16_t)id, 0);
            bool placed = lv->map[r * MAP_W + c] == id;
            lv->refresh();                      /* the game's loader takes it */
            if (dir) {
                char path[1024];
                snprintf(path, sizeof path, "%s/e%d_%04X.dn%d", dir, ep.number, id, ep.number);
                write_map_file(path, lv->map);
            }
            Edit x(lv->map, 0);                 /* and away again */
            x.set(c, r, uncovered(lv->after, c, r, (uint16_t)id, 0));
            if (span_actor((uint16_t)id)) x.set(c + 3, r, 0);
            bool gone = true;
            for (int i = 0; i < ED_MAP_CELLS; i++) if (lv->map[i] == id && orig[i] != id) gone = false;
            if (id == 0x3032) gone = true;      /* (the start moved; nothing to remove) */
            if (placed && gone) ok++;
            else printf("episode %d: actor %04Xh %s\n", ep.number, id, placed ? "not removed" : "not placed");
        }
        printf("episode %d: %d of %d actors placed, loaded and removed\n", ep.number, ok, N_ACTORS);
        if (ok != N_ACTORS) fails = 1;
    }
    delete lv;
    return fails;
}

/* ---------------------------------------------------------------- projects
 * Every stock episode and level through a .LEV / .EPI and a DOS folder and
 * back must give the very same maps (the DOS files byte for byte the stock
 * ones); new episodes, added and removed levels and the details round-trip;
 * saving a DOS episode into the game folder is refused; and afterwards the
 * game folder holds exactly what it did. */
#include <filesystem>
#include <fstream>
#include <sstream>
#include "miniz.h"

static std::map<std::string, std::string> folder_snapshot(const std::string &dir)
{
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (auto &d : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec)) {
        if (!d.is_regular_file()) continue;
        std::ifstream f(d.path(), std::ios::binary);
        std::ostringstream s;
        s << f.rdbuf();
        m[d.path().filename().u8string()] = s.str();
    }
    return m;
}

static bool strcasecmp_ascii(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static bool same_maps(const ProjEntry &a, const ProjEntry &b)
{
    return a.hallway == b.hallway && a.backdrop == b.backdrop && a.map == b.map && a.clear.inputs == b.clear.inputs &&
           !memcmp(&a.clear.lo, &b.clear.lo, sizeof a.clear.lo) && a.clear.mode == b.clear.mode && a.clear.hud == b.clear.hud &&
           a.clear.widescreen == b.clear.widescreen && a.clear.wide == b.clear.wide && a.clear.view_w == b.clear.view_w && a.clear.view_h == b.clear.view_h &&
           a.drop[0] == b.drop[0] && a.drop[1] == b.drop[1] && a.drop_data[0] == b.drop_data[0] && a.drop_data[1] == b.drop_data[1] &&
           a.own_note == b.own_note && (!a.own_note || (a.note[0] == b.note[0] && a.note[1] == b.note[1] && a.note[2] == b.note[2] && a.note[3] == b.note[3]));
}

static bool same_project(const Project &a, const Project &b, std::string &why)
{
    if (a.dos != b.dos || a.episode != b.episode || a.ep != b.ep) { why = "target, kind or episode"; return false; }
    if (a.name != b.name || a.author != b.author) { why = "name or author"; return false; }
    if (a.episode && !a.dos && (a.ending != b.ending || a.ending_text != b.ending_text)) { why = "ending"; return false; }
    if (a.entries.size() != b.entries.size()) { why = "number of levels and hallways"; return false; }
    for (size_t i = 0; i < a.entries.size(); i++)
        if (!same_maps(a.entries[i], b.entries[i])) { why = a.label((int)i) + " differs"; return false; }
    return true;
}

int selftest_projects(Episode &ep)
{
    namespace fs = std::filesystem;
    int fails = 0, checks = 0;
    std::string err, why, game = shrapnel_game_dir;
    auto before = folder_snapshot(game);
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("shrapnel_selftest_" + std::to_string(ep.number));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    std::string T = tmp.u8string();
    auto fail = [&](const std::string &what) { printf("episode %d projects: %s\n", ep.number, what.c_str()); fails = 1; };
    auto stock = [&](int slot) { std::vector<uint16_t> m(ED_MAP_CELLS); read_map_file(game_path(ep.slot_file(slot).c_str()), m.data()); return m; };

    {   /* prefabs: a piece of the first level with a hole, through its text and through a file */
        checks++;
        std::vector<uint16_t> m = stock(0);
        Prefab a;
        a.name = "Test = piece";
        a.w = 7; a.h = 5;
        for (int r = 0; r < a.h; r++)
            for (int c = 0; c < a.w; c++) { a.v.push_back(m[(40 + r) * MAP_W + 20 + c]); a.on.push_back((r + c) % 3 != 0); }
        for (int i = 0; i < a.w * a.h; i++) if (!a.on[i]) a.v[i] = 0;
        Prefab b;
        if (!prefab_parse(prefab_text(a), b) || b.name != a.name || b.w != a.w || b.h != a.h || b.v != a.v || b.on != a.on)
            fail("prefab text round trip");
        std::string pd = path_join(T, "prefabs");
        if (!prefab_save(pd, a, err)) fail("prefab save: " + err);
        prefabs_load(pd, [](const std::string &) { return std::string(); });
        bool found = false;
        for (const Prefab &c : prefabs) if (!c.builtin && c.name == a.name && c.v == a.v && c.on == a.on) found = true;
        if (!found) fail("prefab file round trip");
        prefabs.clear();
    }

    /* the stock episode as a Techbot episode */
    Project p, q;
    if (!project_from_stock(p, ep, false, -1, err)) fail(err);
    else {
        checks++;
        if (p.count(false) != 10 || p.count(true) != 9) fail("stock episode: not 10 levels and 9 hallways");
        for (int k = 0; k < 10; k++) if (p.entries[k * 2].map != stock(k * 2) || p.entries[k * 2].backdrop != k + 1) fail("stock episode: level " + std::to_string(k + 1));
        p.name = "Round trip; with = signs";
        p.author = "PaulloDEC";
        p.ending = 0;
        p.ending_text = "Line one\nLine two";
        for (int k = 0; k < 4000; k++) p.entries[0].clear.inputs.push_back((uint8_t)(k * 7 % 64));   /* recorded clears */
        p.entries[0].clear.lo = EdLoadout{ 1, 0, 1, 0, 5, 3, 7, 0 };
        p.entries[0].clear.hud = 2; p.entries[0].clear.wide = 1; p.entries[0].clear.view_w = 300; p.entries[0].clear.view_h = 190;
        p.entries[3].clear.inputs.assign(10, 0x0A);
        /* backdrops: another episode's file, a picture of its own; notes of its own */
        p.entries[0].drop[0] = "DROP5.DN2";
        {
            std::vector<uint8_t> px(ED_BD_W * ED_BD_H);
            for (int i = 0; i < (int)px.size(); i++) px[i] = (uint8_t)((i / ED_BD_W / 10 + i % ED_BD_W / 13) & 15);
            p.entries[2].drop_data[1] = drop_encode(px.data());
        }
        p.entries[1].own_note = true;
        p.entries[1].note[0] = "A note; with = signs";
        p.entries[1].note[2] = "  (third line)";
        p.entries[4].own_note = true;                    /* own, and empty */
        std::string f = path_join(T, "ep.epi");
        if (!project_save_zip(p, f, err)) fail(err);
        else if (!project_load_zip(q, f, err)) fail(err);
        else if (!same_project(p, q, why)) fail(".EPI round trip: " + why);
    }
    /* each stock level as a .LEV */
    for (int k = 0; k < 10; k++) {
        checks++;
        std::string f = path_join(T, "l" + std::to_string(k) + ".lev");
        if (!project_from_stock(p, ep, false, k * 2, err)) { fail(err); continue; }
        if (!project_save_zip(p, f, err) || !project_load_zip(q, f, err)) { fail(err); continue; }
        if (!same_project(p, q, why)) fail(".LEV round trip of level " + std::to_string(k + 1) + ": " + why);
        if (q.entries[0].map != stock(k * 2)) fail(".LEV of level " + std::to_string(k + 1) + " differs from the stock map");
    }
    /* backdrop files: decoded and encoded again, byte for byte; a PNG in the
     * game's colours comes out exactly */
    {
        checks++;
        std::vector<std::string> drops = game_drop_files();
        if (drops.empty()) fail("no backdrop files found");
        std::vector<uint8_t> px(ED_BD_W * ED_BD_H);
        for (const std::string &d : drops) {
            std::ifstream in(fs::u8path(game_path(d.c_str())), std::ios::binary);
            std::ostringstream o;
            o << in.rdbuf();
            if (!drop_decode(o.str(), px.data()) || drop_encode(px.data()) != o.str()) fail("backdrop file " + d + " doesn't round-trip");
        }
        if (stock_drop_name(1, 0).empty()) fail("no stock backdrop name for level 1");
        std::vector<uint8_t> rgba(ED_BD_W * ED_BD_H * 4), want(ED_BD_W * ED_BD_H), got(ED_BD_W * ED_BD_H);
        for (int i = 0; i < ED_BD_W * ED_BD_H; i++) {
            want[i] = (uint8_t)((i % ED_BD_W) / 13 ^ (i / ED_BD_W) / 10) & 15;
            uint32_t c = ep.pal[want[i]];
            rgba[i * 4] = (uint8_t)(c >> 16); rgba[i * 4 + 1] = (uint8_t)(c >> 8); rgba[i * 4 + 2] = (uint8_t)c; rgba[i * 4 + 3] = 255;
        }
        size_t n = 0;
        void *png = tdefl_write_image_to_png_file_in_memory(rgba.data(), ED_BD_W, ED_BD_H, 4, &n);
        std::string pf = path_join(T, "pic.png");
        { std::ofstream o(fs::u8path(pf), std::ios::binary); o.write((const char *)png, (std::streamsize)n); }
        mz_free(png);
        if (!picture_to_drop(pf, false, ep.pal, got.data(), err)) fail(err);
        else if (got != want) fail("a PNG in the game's colours didn't come out exactly");
        if (!picture_to_drop(pf, true, ep.pal, got.data(), err) || got != want) fail("the same, dithered, didn't either");
        /* a note box can be drawn */
        const char *l[4] = { "Hello", "", "World", NULL };
        std::vector<uint8_t> nb(ED_NOTE_W * ED_NOTE_H, 99);
        ed_note_view(l, nb.data());
        int lit = 0;
        bool all16 = true;
        for (uint8_t v : nb) { all16 = all16 && v < 16; lit += v != 0; }
        if (!all16 || lit < (int)nb.size() / 2) fail("the note box wasn't drawn");
    }
    /* the stock episode as a DOS folder: the files byte for byte the stock ones */
    {
        checks++;
        std::string d = path_join(T, "dos");
        if (!project_from_stock(p, ep, true, -1, err) || !project_save_dos(p, ep, d, err)) fail(err);
        else {
            auto saved = folder_snapshot(d), orig = folder_snapshot(game);
            if (saved.size() != 11) fail("DOS folder: " + std::to_string(saved.size()) + " files, not 11");
            for (auto &kv : saved) {
                std::string match;
                for (auto &o : orig) if (strcasecmp_ascii(o.first, kv.first)) match = o.second;
                if (match != kv.second) fail("DOS folder: " + kv.first + " differs from the stock file");
            }
            int found = 0;
            if (dos_folder_episode(d) != ep.number) fail("DOS folder: episode not recognised");
            if (!project_load_dos(q, ep, d, err, &found)) fail(err);
            else {
                q.name = p.name;            /* (named after its folder) */
                if (found != 11 || !same_project(p, q, why)) fail("DOS folder round trip: " + why);
            }
        }
        /* with recorded clears: kept beside the maps, which stay as they were */
        checks++;
        std::string dc = path_join(T, "dosclr");
        p.entries[2].clear.inputs.assign(300, 0x12);
        p.entries[10].clear.inputs.assign(20, 0x04);
        p.entries[10].clear.lo.god = 1;
        if (!project_save_dos(p, ep, dc, err)) fail(err);
        else if (folder_snapshot(dc).size() != 12 || !path_exists(path_join(dc, "SHRAPNEL.CLR"))) fail("DOS folder with clears: no SHRAPNEL.CLR");
        else if (!project_load_dos(q, ep, dc, err)) fail(err);
        else { q.name = p.name; if (!same_project(p, q, why)) fail("DOS folder with clears, round trip: " + why); }
        p.entries[2].clear.inputs.clear(); p.entries[10].clear.inputs.clear();
        if (project_save_dos(p, ep, dc, err) && path_exists(path_join(dc, "SHRAPNEL.CLR"))) fail("DOS folder: SHRAPNEL.CLR left after the clears went");
        /* never into the game folder */
        if (project_save_dos(p, ep, game, err)) fail("saving a DOS episode into the game folder was not refused");
    }
    /* new episodes: Techbot of 13 levels, levels added and removed; DOS */
    {
        checks++;
        project_new(p, ep, false, true, 13);
        if (p.count(false) != 13 || p.count(true) != 12) fail("new episode: not 13 levels and 12 hallways");
        int n = project_add_level(p, ep, 0);
        if (n != 2 || p.count(false) != 14 || p.count(true) != 13 || p.entries[1].hallway != true) fail("adding a level");
        p.entries[n].map[50 * MAP_W + 50] = 0x3047;
        if (!project_remove_level(p, n) || p.count(false) != 13 || p.count(true) != 12) fail("removing a level");
        for (const ProjEntry &e : p.entries) if (e.map[50 * MAP_W + 50] == 0x3047) fail("removing a level: the wrong one went");
        std::string f = path_join(T, "new.epi");
        if (!project_save_zip(p, f, err) || !project_load_zip(q, f, err)) fail(err);
        else if (!same_project(p, q, why)) fail("new episode round trip: " + why);
        std::vector<uint16_t> room;
        empty_level(ep, room);
        int starts = 0, exits = 0;
        for (uint16_t v : room) { starts += v == 0x3032; exits += v == 0x3011; }
        if (starts != 1 || exits != 1) fail("an empty level should have one start and one exit");

        project_new(p, ep, true, true, 10);
        std::string d = path_join(T, "dosnew");
        if (!project_save_dos(p, ep, d, err) || !project_load_dos(q, ep, d, err)) fail(err);
        else { q.name = p.name; if (!same_project(p, q, why)) fail("new DOS episode round trip: " + why); }
        /* a folder with only some of the maps: the rest come from the stock episode */
        int removed = 0, found = 0;
        for (auto &d2 : fs::directory_iterator(fs::u8path(d), ec))
            if (removed < 5 && d2.path().filename().u8string().find("1.") == std::string::npos) { fs::remove(d2.path(), ec); removed++; }
        if (!project_load_dos(q, ep, d, err, &found)) fail(err);
        else if (found != 11 - removed) fail("partial DOS folder: " + std::to_string(found) + " maps found");
    }
    {   /* a patch: every episode's stock levels; changes saved as cells only, and made again on loading */
        checks++;
        Project a, b;
        if (!project_new_patch(a, ep, err)) fail("new patch: " + err);
        else {
            int eps = 0;
            for (int e = 1; e <= 3; e++) eps += ep.available[e];
            if ((int)a.entries.size() != eps * 11) fail("new patch: not 11 maps an episode");
            for (const ProjEntry &e : a.entries) if (patch_changes(e)) fail("new patch: changes already");
            ProjEntry &x = a.entries[2], &y = a.entries.back();     /* (episode 1's level 3, the last episode's hallway) */
            x.map[40 * MAP_W + 12] = 0x1800; x.map[41 * MAP_W + 12] = 0x3011; x.map[0] = 0x0020;
            y.map[10 * MAP_W + 100] = 0x305E;
            a.entries[4].drop[0] = "DROP5.DN2";                     /* a level's backdrops and words, no cells */
            a.entries[4].drop_data[1] = std::string("\x01\x02picture\x00bytes", 15);
            a.entries[4].own_note = true;
            a.entries[4].note[0] = "  Indented line";
            a.entries[4].note[1] = "Second";
            a.entries[4].note[3] = "Last";
            y.hnote_own[2] = true;                                  /* a hallway's own words (the one after level 3) */
            y.hnote[2][0] = "Hallway three"; y.hnote[2][2] = " spaced";
            a.name = "Test patch = name";
            a.author = "PaulloDEC";
            std::string f = path_join(T, "test.pat");
            if (!project_save_patch(a, f, err)) fail("patch save: " + err);
            else if (!is_patch_file(f)) fail("patch: not seen as one");
            else if (!project_load_patch(b, ep, f, err)) fail("patch load: " + err);
            else {
                if (b.name != a.name || b.author != a.author) fail("patch: name or author");
                for (size_t i = 0; i < a.entries.size(); i++)
                    if (b.entries[i].map != a.entries[i].map) fail("patch round trip: " + a.label((int)i));
                const ProjEntry &q4 = b.entries[4], &a4 = a.entries[4];
                if (q4.drop[0] != a4.drop[0] || q4.drop_data[1] != a4.drop_data[1] || !q4.own_note)
                    fail("patch round trip: a level's backdrops");
                for (int k = 0; k < 4; k++) if (q4.note[k] != a4.note[k]) fail("patch round trip: note line " + std::to_string(k + 1) + " '" + q4.note[k] + "'");
                if (patch_changes(q4) || !patch_settings(q4)) fail("patch: settings without cells");
                const ProjEntry &qy = b.entries.back(), &ay = a.entries.back();
                for (int h = 0; h < 9; h++) {
                    if (qy.hnote_own[h] != ay.hnote_own[h]) fail("patch round trip: hallway words, which");
                    for (int k = 0; k < 4; k++) if (ay.hnote_own[h] && qy.hnote[h][k] != ay.hnote[h][k]) fail("patch round trip: hallway words '" + qy.hnote[h][k] + "'");
                }
                if (!b.patch_keep.empty()) fail("patch: sections left over");
            }
            /* a map from another release: its section kept as it was, not applied */
            std::string data;
            {
                std::ifstream in(fs::u8path(f), std::ios::binary);
                std::ostringstream ss; ss << in.rdbuf(); data = ss.str();
            }
            Project c;
            c.patch = true;
            ProjEntry &w = a.entries[2];
            w.crc ^= 1;                         /* (as if the player's file were different) */
            std::string g = path_join(T, "other.pat");
            if (!project_save_patch(a, g, err) || !project_load_patch(c, ep, g, err)) fail("patch (another release): " + err);
            else {
                if (patch_changes(c.entries[2])) fail("patch: a section for another release was applied");
                if (c.patch_keep.find(w.pfile) == std::string::npos) fail("patch: a section for another release was lost");
                std::string h = path_join(T, "again.pat");
                Project d2;
                if (!project_save_patch(c, h, err) || !project_load_patch(d2, ep, h, err) || d2.patch_keep != c.patch_keep)
                    fail("patch: a kept section doesn't survive saving");
            }
            w.crc ^= 1;
            /* a stock copy, edited, as a patch */
            Project s1, s2;
            if (!project_from_stock(s1, ep, false, 4, err)) fail(err);
            else {
                s1.entries[0].map[30 * MAP_W + 30] = 0x1800;
                if (!project_to_patch(s1, ep, s2, err)) fail("stock copy to patch: " + err);
                else {
                    int n = 0;
                    for (const ProjEntry &e : s2.entries) n += patch_changes(e);
                    if (n != 1) fail("stock copy to patch: " + std::to_string(n) + " changes, not 1");
                }
            }
        }
    }
    fs::remove_all(tmp, ec);
    if (folder_snapshot(game) != before) fail("the game folder changed");
    printf("episode %d: %d project checks %s; the game folder is unchanged\n", ep.number, checks, fails ? "FAILED" : "passed");
    return fails;
}

/* ---------------------------------------------------------------- checks on the stock levels
 * As a DOS episode: no stock level may show a Broken result; the rest are
 * listed (SHRAPNEL_CHECKS_LIST=1) for review. */
int selftest_checks(Episode &ep)
{
    int fails = 0, n[3] = { 0, 0, 0 };
    bool list = getenv("SHRAPNEL_CHECKS_LIST") != NULL;
    Project p;
    std::string err;
    if (!project_from_stock(p, ep, true, -1, err)) { printf("episode %d checks: %s\n", ep.number, err.c_str()); return 1; }
    for (int i = 0; i < (int)p.entries.size(); i++) {
        std::vector<Check> out;
        run_checks(ep, p.entries[i].map.data(), p.check_context(i), out);
        for (const Check &k : out) {
            n[k.sev]++;
            if (k.sev == CS_BROKEN) fails = 1;
            if (list || k.sev == CS_BROKEN)
                printf("  E%d %-10s %s (%d,%d) %s\n", ep.number, p.label(i).c_str(),
                       k.sev == CS_BROKEN ? "BROKEN " : k.sev == CS_WARNING ? "warning" : "note   ", k.c, k.r, k.what.c_str());
        }
    }
    printf("episode %d: checks on the stock levels: %d broken, %d warnings, %d notes%s\n", ep.number, n[2], n[1], n[0],
           fails ? " - FAILED (no stock level may be broken)" : "");

    /* a broken level for each rule: the empty room, changed so as to break one rule */
    std::vector<uint16_t> room;
    empty_level(ep, room);
    const int C0 = (MAP_W - 16) / 2, R0 = (MAP_H - 5) / 2, FL = R0 + 4;    /* the room's inside; FL = floor row */
    auto W = [&](std::vector<uint16_t> &m, int c, int r, uint16_t v) { m[r * MAP_W + c] = v; };
    uint16_t wall = room[(R0 - 1) * MAP_W + C0];
    struct Case { const char *name; CheckSev sev; const char *what; std::function<void(std::vector<uint16_t> &, CheckContext &)> make; };
    CheckContext mid;  mid.episode = true;  mid.last = false;
    std::vector<Case> cases = {
        { "invalid cell", CS_BROKEN, "Not a tile", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0, 0x1234); } },
        { "no start", CS_BROKEN, "No player start", [&](std::vector<uint16_t> &m, CheckContext &) { for (auto &v : m) if (v == 0x3032) v = 0; } },
        { "start at the edge", CS_WARNING, "too near the map", [&](std::vector<uint16_t> &m, CheckContext &) {
              for (auto &v : m) { if (v == 0x3032) v = 0; } W(m, 2, 10, 0x3032); } },
        { "no exit", CS_BROKEN, "No exit door", [&](std::vector<uint16_t> &m, CheckContext &cx) { cx = mid; for (auto &v : m) if (v == 0x3011) v = 0; } },
        { "Proton early", CS_WARNING, "outside the episode", [&](std::vector<uint16_t> &m, CheckContext &cx) { cx = mid; W(m, C0 + 6, FL, 0x3043); } },
        { "DOS end, no Proton", CS_WARNING, "No Dr. Proton", [&](std::vector<uint16_t> &, CheckContext &cx) { cx.dos = cx.episode = true; } },
        { "DOS end with exit", CS_WARNING, "Exit door in the episode", [&](std::vector<uint16_t> &m, CheckContext &cx) { cx.dos = cx.episode = true; W(m, C0 + 6, FL, 0x3043); } },
        { "Techbot end, nothing", CS_BROKEN, "No way to end", [&](std::vector<uint16_t> &m, CheckContext &cx) { cx.episode = true; for (auto &v : m) if (v == 0x3011) v = 0; } },
        { "two exits", CS_WARNING, "A second Exit door", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 6, FL, 0x3011); } },
        { "both Protons", CS_WARNING, "Both kinds", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3042); W(m, C0 + 8, FL, 0x3043); } },
        { "key, no lock", CS_WARNING, "red key, but no red lock", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3044); } },
        { "lock, no key", CS_WARNING, "green lock, but no green key", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3049); W(m, C0 + 7, FL, 0x304D); } },
        { "door, no lock", CS_WARNING, "blue key door, but no blue lock", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x304E); } },
        { "lock, no doors", CS_WARNING, "pink lock, but no pink key doors", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x304B); W(m, C0 + 6, FL, 0x3047); } },
        { "loose card in DOS", CS_BROKEN, "Techbot only", [&](std::vector<uint16_t> &m, CheckContext &cx) { cx.dos = cx.episode = true; W(m, C0 + 5, FL, 0x305D); W(m, C0 + 7, FL, 0x3034); } },
        { "loose card, no reader", CS_WARNING, "card, but no card reader", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x305D); } },
        { "boxed and loose boots", CS_WARNING, "A second Boots", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x305A); W(m, C0 + 7, FL, 0x3006); } },
        { "card, no reader", CS_WARNING, "card, but no card reader", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3033); } },
        { "reader, no card", CS_WARNING, "reader, but no access card", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3034); } },
        { "fields, no card", CS_WARNING, "never switch off", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3021); } },
        { "fields, no reactor", CS_NOTE, "stay on for good", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x301A); } },
        { "hand reader, no hand", CS_WARNING, "but no robot hand", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3035); } },
        { "hand reader, no walkway", CS_WARNING, "but no walkway", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x3035); } },
        { "walkway, no reader", CS_WARNING, "never extends", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0 + 2, 0x3036); W(m, C0 + 9, R0 + 2, 0x2FE0); } },
        { "one teleporter", CS_WARNING, "without its partner", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL, 0x302F); } },
        { "conveyor, no end", CS_BROKEN, "conveyor start with no end", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL + 1, 0x3004); } },
        { "conveyor end below", CS_WARNING, "end is on a later row", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL + 1, 0x3002); W(m, C0 + 2, FL + 3, 0x3003); } },
        { "conveyor end alone", CS_WARNING, "end with no start", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, FL + 1, 0x3005); } },
        { "walkway, no stop", CS_BROKEN, "walkway with no stop", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0 + 2, 0x3036); W(m, C0 + 3, FL, 0x3035); W(m, C0 + 4, FL, 0x3020); } },
        { "bridge to nowhere", CS_BROKEN, "nothing to reach", [&](std::vector<uint16_t> &m, CheckContext &) {
              for (int c = C0 + 3; c < MAP_W; c++) { W(m, c, R0 + 2, 0); } W(m, C0 + 3, R0 + 2, 0x3019); } },
        { "bridge through a wall", CS_WARNING, "runs through a wall", [&](std::vector<uint16_t> &m, CheckContext &) {
              W(m, C0 + 2, R0 + 2, 0x3019); W(m, C0 + 4, R0 + 2, wall); W(m, C0 + 9, R0 + 2, wall); } },
        { "bridge a cell short", CS_WARNING, "a cell short", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 2, R0 + 2, 0x3019); W(m, C0 + 7, R0 + 2, wall); } },
        { "biter with no wall", CS_WARNING, "biter with no wall", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0 + 2, 0x3016); } },
        { "down spikes, no ceiling", CS_WARNING, "no ceiling above", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0 + 2, 0x3059); } },
        { "floor spikes, no floor", CS_WARNING, "no floor below", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0 + 2, 0x302C); } },
        { "flamethrower into a wall", CS_WARNING, "straight into a wall", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + ROOM_W - 1, R0 + 2, 0x3009); } },
        { "falls off the map", CS_WARNING, "falls off the bottom", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, 5, 5, 0x3010); } },
        { "drops to the floor", CS_NOTE, "drops to row", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0 + 5, R0, 0x3015); } },
        { "copies a wall", CS_NOTE, "copy of the wall", [&](std::vector<uint16_t> &m, CheckContext &) { W(m, C0, FL, 0x3000); } },
        { "big actor in a wall", CS_WARNING, "overlaps", [&](std::vector<uint16_t> &m, CheckContext &) {
              for (auto &v : m) { if (v == 0x3011) v = 0; } W(m, C0 + ROOM_W - 1, FL, 0x3011); } },
        { "too many", CS_BROKEN, "than the game can hold", [&](std::vector<uint16_t> &m, CheckContext &) {
              int cap; ed_actor_limit(0x3010, &cap); for (int k = 0; k <= cap; k++) W(m, k % MAP_W, 1 + k / MAP_W, 0x3010); } },
    };
    std::vector<Check> base;
    CheckContext single;
    run_checks(ep, room.data(), single, base);
    if (check_count(base, CS_BROKEN) || check_count(base, CS_WARNING)) {
        printf("episode %d checks: the empty room is not clean: %s\n", ep.number, base[0].what.c_str());
        fails = 1;
    }
    int caught = 0;
    for (Case &k : cases) {
        std::vector<uint16_t> m = room;
        CheckContext cx;
        k.make(m, cx);
        std::vector<Check> out;
        run_checks(ep, m.data(), cx, out);
        bool hit = false;
        for (const Check &o : out) if (o.sev == k.sev && o.what.find(k.what) != std::string::npos) hit = true;
        if (hit) caught++;
        else {
            printf("episode %d checks: \"%s\" not caught (wanted a %s with \"%s\"; got:", ep.number, k.name,
                   k.sev == CS_BROKEN ? "Broken" : k.sev == CS_WARNING ? "Warning" : "Note", k.what);
            for (const Check &o : out) printf(" [%s]", o.what.c_str());
            printf(")\n");
            fails = 1;
        }
    }
    printf("episode %d: %d of %d deliberately broken levels caught\n", ep.number, caught, (int)cases.size());

    /* painting a tile over an actor puts it behind the actor */
    uint16_t bg = 0;
    for (uint16_t v = 0x40; v < 0x1800 && !bg; v += 0x20) if (cell_kind(v) == CK_BACKGROUND) bg = v;
    int behind_ok = 0, behind_n = 0;
    auto expect = [&](const char *what, bool ok) { behind_n++; if (ok) behind_ok++; else { printf("episode %d painting behind actors: %s\n", ep.number, what); fails = 1; } };
    auto cell = [&](const std::vector<uint16_t> &m, int c, int r) { return m[r * MAP_W + c]; };
    {
        std::vector<uint16_t> m = room;
        W(m, C0 + 5, FL, 0x3000);                           /* a crate (its background comes from the left) */
        Edit e(m.data(), 0); e.behind = true;
        e.set(C0 + 5, FL, bg); e.finish_behind();
        expect("a dab on a crate keeps the crate", cell(m, C0 + 5, FL) == 0x3000);
        expect("a dab on a crate paints the cell it copies", cell(m, C0 + 4, FL) == bg && e.spread == 1);
    }
    {
        std::vector<uint16_t> m = room;
        W(m, C0, FL, 0x3000);                               /* against the left wall */
        Edit e(m.data(), 0); e.behind = true;
        e.set(C0, FL, bg); e.finish_behind();
        expect("a crate against a wall: the wall stays", cell(m, C0 - 1, FL) == wall && cell(m, C0, FL) == 0x3000 && e.walled == 1);
    }
    {
        std::vector<uint16_t> m = room;
        W(m, C0 + 5, FL, 0x3000); W(m, C0 + 6, FL, 0x3000); /* two crates in a row */
        Edit e(m.data(), 0); e.behind = true;
        e.set(C0 + 6, FL, bg); e.finish_behind();
        expect("a row of crates: the first real tile", cell(m, C0 + 4, FL) == bg && cell(m, C0 + 6, FL) == 0x3000);
    }
    {
        std::vector<uint16_t> m = room;
        for (int r = R0; r <= FL; r++) W(m, C0 + 7, r, 0x3000);   /* a column of crates across the room */
        Edit e(m.data(), 0); e.behind = true;
        op_fill(e, C0 + 1, R0, bg, false);
        bool all = true;
        for (int r = R0; r <= FL; r++) for (int c = C0; c < C0 + ROOM_W; c++)
            if (c != C0 + 7 && cell(m, c, r) != bg && cell(m, c, r) != 0x3032 && cell(m, c, r) != 0x3011) all = false;
        expect("fill flows past actors", all && cell(m, C0 + 7, R0) == 0x3000);
    }
    {
        std::vector<uint16_t> m = room;
        W(m, C0 + 5, FL, 0x3000);
        Edit e(m.data(), 0);                                /* the eraser: not behind */
        e.set(C0 + 5, FL, 0); e.finish_behind();
        expect("the eraser still removes an actor", cell(m, C0 + 5, FL) == 0);
        Edit w(m.data(), 0); w.behind = true;
        W(m, C0 + 5, FL, 0x3000);
        w.set(C0 + 5, FL, wall); w.finish_behind();
        expect("a wall tile still replaces an actor", cell(m, C0 + 5, FL) == wall);
    }
    printf("episode %d: painting behind actors: %d of %d\n", ep.number, behind_ok, behind_n);
    return fails;
}

/* ---- reachability: every stock level's end can be reached */
/* planted problems in the empty room: each must come out as it should */
static int reach_cases(Episode &ep)
{
    static EdReach r;
    int fails = 0, n = 0;
    std::vector<uint16_t> room;
    empty_level(ep, room);
    const int C0 = (MAP_W - ROOM_W) / 2, R0 = (MAP_H - ROOM_H) / 2, R1 = R0 + ROOM_H - 1;
    uint16_t wall = room[(R0 - 1) * MAP_W + C0];
    auto W = [&](std::vector<uint16_t> &m, int c, int rr, uint16_t v) { m[rr * MAP_W + c] = v; };
    struct Case { const char *name; bool finish; bool trap; std::function<void(std::vector<uint16_t> &)> make; };
    auto tall = [&](std::vector<uint16_t> &m) {     /* the room 5 rows higher, a wall across it 4 high */
        const int C1 = C0 + ROOM_W - 1;
        for (int rr = R0 - 6; rr < R0; rr++)
            for (int c = C0 - 1; c <= C1 + 1; c++) W(m, c, rr, c == C0 - 1 || c == C1 + 1 || rr == R0 - 6 ? wall : 0);
        for (int rr = R1 - 3; rr <= R1; rr++) W(m, C0 + 8, rr, wall);
    };
    std::vector<Case> cases = {
        { "the room as it is", true, false, [&](std::vector<uint16_t> &) {} },
        { "a wall to the ceiling", false, false, [&](std::vector<uint16_t> &m) { for (int rr = R0; rr <= R1; rr++) W(m, C0 + 8, rr, wall); } },
        { "a wall he can jump", true, false, [&](std::vector<uint16_t> &m) { W(m, C0 + 8, R1, wall); } },
        { "a pit he can't get out of", true, true, [&](std::vector<uint16_t> &m) {
              W(m, C0 + 6, R1 + 1, 0);
              for (int rr = R1 + 1; rr <= R1 + 6; rr++) { W(m, C0 + 5, rr, wall); W(m, C0 + 7, rr, wall); }
              W(m, C0 + 6, R1 + 6, wall); } },
        { "a blue door, key and lock", true, false, [&](std::vector<uint16_t> &m) {
              for (int rr = R0; rr <= R1; rr++) W(m, C0 + 10, rr, 0x304E);
              W(m, C0 + 4, R1, 0x3046); W(m, C0 + 7, R1, 0x304A); } },
        { "a blue door, no key", false, false, [&](std::vector<uint16_t> &m) {
              for (int rr = R0; rr <= R1; rr++) W(m, C0 + 10, rr, 0x304E);
              W(m, C0 + 7, R1, 0x304A); } },
        { "the key beyond its door", false, false, [&](std::vector<uint16_t> &m) {
              for (int rr = R0; rr <= R1; rr++) W(m, C0 + 10, rr, 0x304E);
              W(m, C0 + 12, R1, 0x3046); W(m, C0 + 7, R1, 0x304A); } },
        { "a wall too high without boots", false, false, [&](std::vector<uint16_t> &m) { tall(m); } },
        { "the same, boots in a crate", true, false, [&](std::vector<uint16_t> &m) { tall(m); W(m, C0 + 4, R1, 0x3006); } },
        { "the same, loose boots", true, false, [&](std::vector<uint16_t> &m) { tall(m); W(m, C0 + 4, R1, 0x305A); } },
    };
    for (auto &k : cases) {
        std::vector<uint16_t> m = room;
        k.make(m);
        EdReachOpts o = {};
        ed_reach(0, m.data(), &o, &r);
        bool ok = r.status == ED_REACH_OK && (r.finish_ok != 0) == k.finish && (r.trap_cells > 0) == k.trap;
        if (ok) n++;
        else { fails = 1; printf("  reach case \"%s\": end %s, %d trapped cells - FAILED\n", k.name, r.finish_ok ? "reachable" : "not reachable", r.trap_cells); }
    }
    printf("episode %d: %d of %d planted reachability cases right\n", ep.number, n, (int)cases.size());
    return fails;
}

int selftest_reach(Episode &ep)
{
    int fails = reach_cases(ep);
    Project p;
    std::string err;
    if (!project_from_stock(p, ep, true, -1, err)) { printf("episode %d reach: %s\n", ep.number, err.c_str()); return 1; }
    if (getenv("SHRAPNEL_REACH_CASES")) return fails;
    static EdReach r;
    EdReachOpts o = {};
    const char *only = getenv("SHRAPNEL_REACH_ONLY");
    if (getenv("SHRAPNEL_REACH_MAX")) o.max_nodes = atoi(getenv("SHRAPNEL_REACH_MAX"));
    for (int i = 0; i < (int)p.entries.size(); i++) {
        if (only && atoi(only) != i) {
            for (uint16_t v : p.entries[i].map) { v = item_base(v); if (v == 0x3006) o.boots = 1; if (v == 0x3008) o.claws = 1; if (v == 0x3020) o.robohand = 1; }
            continue;
        }
        auto t0 = std::chrono::steady_clock::now();
        ed_reach(p.slot_of(i), p.entries[i].map.data(), &o, &r);
        long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        bool ok = r.status == ED_REACH_OK && r.finish_ok;
        if (!ok) fails = 1;
        printf("  E%d %-10s %s  %8d states %9d edges  %4d cells  %3d trapped  route %5d frames  gates %04x  %ld ms\n",
               ep.number, p.label(i).c_str(), ok ? "ok    " : r.status == ED_REACH_TOO_BIG ? "TOOBIG" : "NO END",
               r.nodes, r.edges, r.body_cells, r.trap_cells, r.route_n, r.gates, ms);
        if (ok && getenv("SHRAPNEL_REACH_REPLAY")) {
            /* the route, played by the game itself: with the enemies taken out
             * (they knock Duke about), the search's route must reach the exit */
            static EdReach r2;
            static EdPlay play, again;
            std::vector<uint16_t> m = p.entries[i].map;
            uint16_t after[ED_MAP_CELLS];
            ed_load(p.slot_of(i), m.data());
            ed_map_after(after);
            for (int k = 0; k < ED_MAP_CELLS; k++) {
                uint16_t v = m[k];
                bool keep = v == 0x3001 || (v >= 0x3002 && v <= 0x3006) || v == 0x3008 || v == 0x3011 || v == 0x3019 ||
                            v == 0x301A || v == 0x3020 || v == 0x3021 || v == 0x302F || v == 0x3030 ||
                            v == 0x3032 || v == 0x3033 || v == 0x3034 || v == 0x3035 || v == 0x3036 ||
                            v == 0x3042 || v == 0x3043 || (v >= 0x3044 && v <= 0x304F) || v == 0x302C || v == 0x3058 || v == 0x3059 || v == 0x3007;
                if (v >= 0x3000 && v < ED_ACTOR_END && !keep) m[k] = after[k];
            }
            ed_reach(p.slot_of(i), m.data(), &o, &r2);
            /* what the search took as shot (blocks, rockets launched): so in the map played, as the search had it */
            for (int k = 0; k < ED_MAP_CELLS; k++) {
                if (r2.shot[k] && m[k] < 0x3000) m[k] = r2.shot[k] == 1 ? 0 : r2.shot[k];
                if ((r2.cell[k] & ED_REACH_SHOT) && m[k] == 0x3007) m[k] = after[k];
            }
            EdLoadout lo = {};
            lo.boots = o.boots; lo.claws = o.claws; lo.robohand = o.robohand; lo.firepower = 4; lo.health = 9; lo.god = 1;   /* (shots open the boxes on the way) */
            ed_play_unseen = 1;
            port_boxes_open = 1;                /* (the search takes them as shot open: the route doesn't shoot) */
            ed_play_feed = r2.inputs; ed_play_feed_n = r2.inputs_n;
            ed_play(p.slot_of(i), m.data(), -1, -1, &lo, &play);
            ed_play_feed = play.inputs; ed_play_feed_n = play.inputs_n;
            ed_play(p.slot_of(i), m.data(), -1, -1, &lo, &again);
            ed_play_feed = NULL; ed_play_unseen = 0; port_boxes_open = 0;
            bool same = again.end == play.end && again.trail_n == play.trail_n &&
                        !memcmp(again.trail, play.trail, sizeof(int16_t) * 2 * play.trail_n);
            /* where the game and the search first part ways */
            int part = -1;
            /* (the trail is taken as each frame starts: trail[f + 1] is where frame f left him) */
            for (int f = 0; f + 1 < play.trail_n && f < r2.route_n && part < 0; f++)
                if (play.trail[f * 2 + 2] != r2.route[f * 2] || play.trail[f * 2 + 3] != r2.route[f * 2 + 1]) part = f;
            if (const char *wd = getenv("SHRAPNEL_WRITE_CLEARS")) {   /* (test aid) the level with this as its clear, and blocked */
                Project one;
                std::string e2;
                if (project_from_stock(one, ep, false, p.slot_of(i), e2)) {
                    one.entries[0].clear.inputs.assign(play.inputs, play.inputs + play.inputs_n);
                    one.entries[0].clear.lo = lo;
                    one.entries[0].map = m;                    /* (the enemies taken out, as played) */
                    one.name = "clear test";
                    project_save_zip(one, path_join(wd, "clear_" + std::to_string(ep.number) + "_" + std::to_string(i) + ".lev"), e2);
                    int f = play.trail_n / 2, c = (play.trail[f * 2] - 8) / 16, rr = play.trail[f * 2 + 1] / 16 - 1;
                    uint16_t wall = 0;
                    for (uint16_t v : m) if (v >= 0x1800 && v < 0x3000 && v != 0x1800) { wall = v; break; }
                    for (int dr = -3; dr <= 1; dr++) if (rr + dr >= 0 && rr + dr < MAP_H) one.entries[0].map[(rr + dr) * MAP_W + c] = wall;
                    project_save_zip(one, path_join(wd, "blocked_" + std::to_string(ep.number) + "_" + std::to_string(i) + ".lev"), e2);
                }
            }
            /* (Dr. Proton: the route ends where Duke can shoot him; the game must follow it all the way) */
            bool rok = r2.finish_ok && same && (r2.goal == 2 ? part < 0 && play.trail_n > r2.route_n : play.end == ED_END_EXIT);
            if (!rok) fails = 1;
            printf("      replayed: %s  (%d frames, end %d, replay %s%s)\n", rok ? (r2.goal == 2 ? "follows the route to Dr. Proton" : "reaches the exit") : "FAILED", play.frames, play.end,
                   same ? "identical" : "DIFFERS", part >= 0 ? (" , parts at frame " + std::to_string(part)).c_str() : "");
            if (part >= 0 && getenv("SHRAPNEL_REACH_PART"))
                for (int f = part > 3 ? part - 3 : 0; f < part + 4 && f < play.trail_n; f++)
                    printf("        %d: game %d,%d  search %d,%d  in %02x\n", f, play.trail[f * 2 + 2], play.trail[f * 2 + 3],
                           r2.route[f * 2], r2.route[f * 2 + 1], r2.inputs[f]);
        }
        if (getenv("SHRAPNEL_REACH_DUMP")) {     /* (debugging) the map: # wall, o reached, x trapped, letters actors */
            uint16_t after[ED_MAP_CELLS];
            ed_load(p.slot_of(i), p.entries[i].map.data());
            ed_map_after(after);
            for (int rr = 0; rr < MAP_H; rr++) {
                char line[MAP_W + 1];
                for (int cc = 0; cc < MAP_W; cc++) {
                    int k = rr * MAP_W + cc;
                    uint16_t v = p.entries[i].map[k];
                    char ch = after[k] > 0x17FF ? '#' : ' ';
                    if (r.cell[k] & ED_REACH_BODY) ch = 'o';
                    if (r.cell[k] & ED_REACH_TRAPPED) ch = 'x';
                    if (v == 0x3032) ch = 'S'; else if (v == 0x3011) ch = 'E';
                    else if (v >= 0x3000 && v < ED_ACTOR_END) ch = (r.cell[k] & ED_REACH_TOUCH) ? 'A' : 'a';
                    line[cc] = ch;
                }
                line[MAP_W] = 0;
                printf("%2d %s\n", rr, line);
            }
        }
        if (const char *hx = getenv("SHRAPNEL_REACH_HEX")) {   /* (debugging) c0,r0,c1,r1: the cells in hex */
            int c0, r0, c1, r1;
            if (sscanf(hx, "%d,%d,%d,%d", &c0, &r0, &c1, &r1) == 4)
                for (int rr = r0; rr <= r1; rr++) {
                    printf("%2d", rr);
                    for (int cc = c0; cc <= c1; cc++) printf(" %04x", p.entries[i].map[rr * MAP_W + cc]);
                    printf("\n");
                }
        }
        for (uint16_t v : p.entries[i].map) { v = item_base(v); if (v == 0x3006) o.boots = 1; if (v == 0x3008) o.claws = 1; if (v == 0x3020) o.robohand = 1; }
    }
    return fails;
}
