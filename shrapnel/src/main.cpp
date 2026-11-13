/*
 * Shrapnel - a level editor for Duke Nukem (1991), built on the Techbot core.
 *
 * Levels are drawn by the game's own code (episode.cpp); the editing
 * operations live in tools.cpp, the undo history in doc.cpp, projects and
 * their files in project.cpp.  This file is the interface.
 *
 *   shrapnel [--dir <game folder>]                 the start screen
 *            [--open <.lev, .epi, DOS folder or map file>]
 *            [--new level|episode|dos [--levels N]] [--episode N]
 *            [--level N | --slot I] [--dos] [--at C,R] [--zoom Z]
 *   shrapnel --dir <game folder> --selftest
 *   shrapnel --dir <game folder> --catalog <file.json>   (every tile and actor, for tools/dn1_catalog.py)
 *
 * Test aids: --shot FILE saves the window at frame --shot-at N (default 3)
 * and quits; SHRAPNEL_EVENTS="frame action args; ..." feeds input (see
 * scripted_input below); SHRAPNEL_PLAY_FRAMES ends a play-test after N
 * frames; SHRAPNEL_AUTOSAVE_SECS shortens the autosave interval.
 */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <functional>
#include <vector>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "shrapnel.h"
#include "theme.h"
#include "keys.h"
#include "prefabs.h"
#include <set>
#include <filesystem>
#include "icon_data.h"

extern "C" {                                /* Techbot's own platform layer, built in (pf_sdl_embed.c) */
void pf_sdl_play_begin(SDL_Window *w, SDL_Renderer *r);
void pf_sdl_play_end(void);
}

/* ================================================================ state */
static Episode ep;
static Level *lv = new Level;
static History hist;
static Selection sel;
static Clip clip;
static SDL_Renderer *ren;
static SDL_Texture *tex[4];                 /* the level at each animation phase */
static SDL_Texture *atlas;                  /* every tile, 16 per row */
static SDL_Texture *bd_tex[2];              /* backdrops A and B */
static SDL_Texture *clip_tex;               /* the clipboard, drawn */
static SDL_Texture *actor_tex[N_ACTORS];    /* each actor's look, trimmed to its pieces */
static int actor_w[N_ACTORS], actor_h[N_ACTORS], actor_ox[N_ACTORS], actor_oy[N_ACTORS];   /* size, offset from the cell */
static SDL_Texture *contents_tex[N_ACTORS];  /* a crate's contents, 16 x 16 */
static std::vector<uint32_t> pixels(LEVEL_PX_W * LEVEL_PX_H);
static const int ATLAS_TILES = 0x3000 / 0x20;

enum Tool { T_SELECT, T_MOVE, T_MAGIC, T_PENCIL, T_ERASER, T_LINE, T_RECT, T_FILL, T_PICK, N_TOOLS };
static const struct { const char *name, *key, *help; } tool_info[N_TOOLS] = {
    { "Select",  "M", "Drag to select cells. Shift adds, Alt removes." },
    { "Move",    "V", "Drag the selection somewhere else (or, outside it, the actor or cell under the mouse). Shift: a copy. Arrows nudge it." },
    { "Magic",   "W", "Click to select the connected area of the same tile. Shift adds, Alt removes." },
    { "Pencil",  "B", "Paint the brush. Shift-click: a line from the last cell. Alt-click picks up a tile. X: the brush before." },
    { "Eraser",  "E", "Paint the backdrop. Shift-click: a line from the last cell." },
    { "Line",    "L", "Drag a straight line of the brush. Shift: across, down or diagonal." },
    { "Rect",    "R", "Drag a rectangle of the brush (outlined or filled). Shift: a square." },
    { "Fill",    "G", "Fill the connected area of the same tile. Shift fills every matching tile." },
    { "Pick",    "I", "Click to pick up a tile or actor as the brush." },
};

static struct {
    double x = 0, y = 0;                    /* world pixel at the canvas's top-left */
    double zoom = 2.0;                      /* screen points per game pixel */
    bool actors = true, grid = false, animate = true, labels = true, trail = true;
    int phase = 0;
    double phase_time = 0;
} view;

static struct {
    Tool tool = T_PENCIL;
    uint16_t brush = 0x1800;
    uint16_t erase = 0;                     /* backdrop A (0) or B (20h) */
    bool rect_filled = false;
    int locks = 0;
    bool floating = false;                  /* a paste following the cursor */
    /* a drag in progress */
    bool dragging = false;
    int drag_c = 0, drag_r = 0, last_c = 0, last_r = 0;
    int stroke_cells = 0, stroke_walled = 0;
    int sc0, sr0, sc1, sr1;                 /* stroke's changed area */
    bool told_mirror = false;
    uint16_t prev_brush = 0x0600;           /* the brush before this one (X swaps them) */
    int paint_c = -1, paint_r = -1;         /* the last cell the Pencil or Eraser painted (Shift+click: a line from it) */

    std::vector<int32_t> under = std::vector<int32_t>(ED_MAP_CELLS, -1);   /* what a cell held before an actor was put on it (this session) */
    bool shifting = false;                  /* the Move tool dragging cells (lifted, below) */
    bool shift_copy = false;                /* ... leaving the originals where they were */
    int counts[ED_N_TABLES] = { 0 };        /* actor tables filled, as of the last change */
} ed;

static const double ZOOMS[] = { 0.25, 0.5, 1, 2, 3, 4, 6, 8 };
static const int N_ZOOMS = sizeof ZOOMS / sizeof ZOOMS[0];
static char folder_buf[512];
static std::string message;                 /* shown in the status bar for a while */
static double message_until;
static double now_s;
static Uint64 clock_start;
static double clock_s() { return (double)(SDL_GetPerformanceCounter() - clock_start) / SDL_GetPerformanceFrequency(); }
static ImVec2 last_canvas(1000, 700);
static int pending_at_c = -1, pending_at_r = -1;
static float ui_scale = 1;
static float ui_auto = 1, ui_user = 1;         /* ui_scale = the display's, times the size chosen in View */
static bool ui_rescale;                         /* (the size changed: new fonts before the next frame) */
static double last_autosave;
static int edits;                           /* changes since the last autosave */
static bool want_quit, quit_now;
static SDL_Window *main_win;

/* play-testing */
static EdLoadout loadout = { 0, 0, 0, 0, 0, 1, 9, 0 };
static struct { bool pending = false; int c = -1, r = -1; bool from_start = false, watch = false; } play_req;
static std::vector<ImVec2> trail;           /* Duke's path in the last play-test (level pixels) */
static std::string last_play;
static int hover_c = -1, hover_r = -1;      /* the cell under the mouse on the map, or -1 */

static void hint(const char *t)                 /* quiet, wrapped words */
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", t);
    ImGui::PopStyleColor();
}

static void say(const std::string &m) { message = m; message_until = clock_s() + 5; }

/* ================================================================ drawing the level */
static void make_texture(SDL_Texture **t, int w, int h)
{
    if (*t) return;
    *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_SetTextureScaleMode(*t, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(*t, SDL_BLENDMODE_BLEND);
}

/* redraw cells c0..c1-1, r0..r1-1 at every phase */
static void redraw_area(int c0, int r0, int c1, int r1)
{
    c0 = std::max(c0, 0); r0 = std::max(r0, 0); c1 = std::min(c1, MAP_W); r1 = std::min(r1, MAP_H);
    if (c0 >= c1 || r0 >= r1) return;
    SDL_Rect rc = { c0 * 16, r0 * 16, (c1 - c0) * 16, (r1 - r0) * 16 };
    for (int a = 0; a < 4; a++) {
        make_texture(&tex[a], LEVEL_PX_W, LEVEL_PX_H);
        compose_area(ep, *lv, a * 0x20, view.actors, pixels.data(), c0, r0, c1, r1);
        SDL_UpdateTexture(tex[a], &rc, pixels.data() + rc.y * LEVEL_PX_W + rc.x, LEVEL_PX_W * 4);
    }
}

static void redraw_all(void) { redraw_area(0, 0, MAP_W, MAP_H); }

static void build_episode_textures(void)
{
    std::vector<uint32_t> img(256 * (ATLAS_TILES / 16) * 16);
    uint8_t t[256];
    make_texture(&atlas, 256, ATLAS_TILES / 16 * 16);
    for (int i = 0; i < ATLAS_TILES; i++) {
        ed_tile((uint16_t)(i * 0x20), t);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                img[((i / 16) * 16 + y) * 256 + (i % 16) * 16 + x] = 0xFF000000u | ep.pal[t[y * 16 + x] & 15];
    }
    SDL_UpdateTexture(atlas, NULL, img.data(), 256 * 4);
    for (int i = 0; i < N_ACTORS; i++) {        /* actor looks, for the brush and (later) the palette */
        const ActorLook &l = ep.looks[i];
        if (actor_tex[i]) { SDL_DestroyTexture(actor_tex[i]); actor_tex[i] = NULL; }
        if (l.pieces.empty()) continue;
        int x0 = 0, y0 = 0, x1 = 16, y1 = 16;
        for (const Piece &p : l.pieces) { x0 = std::min(x0, p.dx); y0 = std::min(y0, p.dy); x1 = std::max(x1, p.dx + 16); y1 = std::max(y1, p.dy + 16); }
        int w = x1 - x0, h = y1 - y0;
        std::vector<uint32_t> a(w * h, 0);
        for (const Piece &p : l.pieces)
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    if (p.px[y * 16 + x] != 255) a[(p.dy - y0 + y) * w + p.dx - x0 + x] = 0xFF000000u | ep.pal[p.px[y * 16 + x] & 15];
        make_texture(&actor_tex[i], w, h);
        SDL_UpdateTexture(actor_tex[i], NULL, a.data(), w * 4);
        actor_w[i] = w; actor_h[i] = h; actor_ox[i] = x0; actor_oy[i] = y0;
        if (contents_tex[i]) { SDL_DestroyTexture(contents_tex[i]); contents_tex[i] = NULL; }
        if (!l.contents.empty()) {               /* the first piece of what it holds */
            std::vector<uint32_t> c(256, 0);
            for (int k = 0; k < 256; k++) if (l.contents[0].px[k] != 255) c[k] = 0xFF000000u | ep.pal[l.contents[0].px[k] & 15];
            make_texture(&contents_tex[i], 16, 16);
            SDL_UpdateTexture(contents_tex[i], NULL, c.data(), 16 * 4);
        }
    }
}

static void build_level_textures(void)
{
    std::vector<uint32_t> img(ED_BD_W * ED_BD_H);
    for (int b = 0; b < 2; b++) {
        make_texture(&bd_tex[b], ED_BD_W, ED_BD_H);
        for (int i = 0; i < ED_BD_W * ED_BD_H; i++) img[i] = 0xFF000000u | ep.pal[lv->backdrop[b][i] & 15];
        SDL_UpdateTexture(bd_tex[b], NULL, img.data(), ED_BD_W * 4);
    }
    redraw_all();
}

/* a map value drawn into an RGBA image at (ox, oy) (clip preview) */
static void draw_value(std::vector<uint32_t> &img, int iw, int ih, int ox, int oy, uint16_t v)
{
    uint8_t t[256];
    CellKind k = cell_kind(v);
    if (k == CK_BACKDROP_A || k == CK_BACKDROP_B) {   /* the backdrop shows through wherever it lands: just a tint */
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) img[(oy + y) * iw + ox + x] = k == CK_BACKDROP_A ? 0x50000010u : 0x50402060u;
        return;
    }
    if (k == CK_ACTOR) {
        const ActorLook &l = ep.looks[v - 0x3000];
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) img[(oy + y) * iw + ox + x] = 0xFF303040;
        for (const Piece &p : l.pieces)
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    int X = ox + p.dx + x, Y = oy + p.dy + y;
                    if (p.px[y * 16 + x] != 255 && X >= 0 && Y >= 0 && X < iw && Y < ih)
                        img[Y * iw + X] = 0xFF000000u | ep.pal[p.px[y * 16 + x] & 15];
                }
        return;
    }
    ed_tile(v, t);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) img[(oy + y) * iw + ox + x] = 0xFF000000u | ep.pal[t[y * 16 + x] & 15];
}

static void build_clip_texture(void)
{
    if (clip_tex) { SDL_DestroyTexture(clip_tex); clip_tex = NULL; }
    if (clip.empty()) return;
    int iw = clip.w * 16, ih = clip.h * 16;
    std::vector<uint32_t> img(iw * ih, 0);
    for (int r = 0; r < clip.h; r++)
        for (int c = 0; c < clip.w; c++)
            if (clip.on[r * clip.w + c]) draw_value(img, iw, ih, c * 16, r * 16, clip.v[r * clip.w + c]);
    make_texture(&clip_tex, iw, ih);
    SDL_UpdateTexture(clip_tex, NULL, img.data(), iw * 4);
}

/* after the map changed somewhere in c0..c1, r0..r1 (inclusive): what the
 * game makes of it, and the picture */
static void map_changed(int c0, int r0, int c1, int r1)
{
    if (level_changed(ep, *lv, c0, r0, c1, r1)) redraw_area(c0, r0, c1, r1);
}

static void recount(bool warn);
static bool checks_dirty;                   /* the level checks need running again */
static bool checks_ever;                    /* they have run at all on this project */
/* Checks run as you edit (the rules after each change, the reachability
 * search and the recorded clear once the map settles), or only when asked:
 * Check (F7) runs all three, and Shrapnel reminds you now and then */
static bool checks_manual;                  /* (a setting: settings.txt) */
static bool check_req;                      /* Check pressed: run everything once */
static int  unchecked;                      /* changes since the rules last ran */
static double stale_since = -1;             /* (manual) when the results went out of date */
static bool stale_told;
static const char *crate_label(uint16_t v);

/* the map replaced as a whole (undo, redo, history) */
static void set_map(const uint16_t *m)
{
    int c0 = MAP_W, r0 = MAP_H, c1 = -1, r1 = -1;
    for (int i = 0; i < ED_MAP_CELLS; i++)
        if (lv->map[i] != m[i]) {
            int c = i % MAP_W, r = i / MAP_W;
            c0 = std::min(c0, c); r0 = std::min(r0, r); c1 = std::max(c1, c); r1 = std::max(r1, r);
        }
    memcpy(lv->map, m, sizeof lv->map);
    map_changed(c0, r0, c1, r1);
    recount(false);
}

/* Fill-ups of the game's actor tables: say so the moment a change takes one
 * past what the original DOS game holds. */
static int table_cap(int t)                     /* how many entries table t has */
{
    for (int id = 0x3000; id < 0x3000 + N_ACTORS; id++) {
        int cap;
        if (ed_actor_limit((uint16_t)id, &cap) == t) return cap;
    }
    return 0;
}

static void recount(bool warn)
{
    int now[ED_N_TABLES];
    count_actors(lv->map, now);
    for (int t = 1; t < ED_N_TABLES && warn; t++) {
        int cap = table_cap(t);
        if (now[t] > cap && ed.counts[t] <= cap)
            say(std::string(table_name(t)) + ": " + std::to_string(now[t]) + " of " + std::to_string(cap) +
                " - more than the game can hold (see Checks).");
    }
    memcpy(ed.counts, now, sizeof now);
    checks_dirty = true;
    unchecked++;
}

static void commit(const std::string &what)
{
    hist.commit(lv->map, what);
    edits++;
    trail.clear();                          /* the last play-test's path no longer fits the level */
    recount(true);
}

/* ================================================================ projects and files */
static Project proj;                        /* what is being edited */
static int cur = -1;                        /* its entry in the editor (lv, hist), -1 = none: the start screen */
static std::string last_dir;                /* where the file dialogs start */
static std::vector<std::string> recent;     /* most recent first */
static bool prompt_autosave, prompt_remove, prompt_pick;
static Project picked;                      /* an .EPI, to take one level from */
static bool bad_folder;                     /* no DN1/2/3.EXE in the game folder */
static std::vector<std::vector<Check>> checks;   /* per project entry */
static bool show_notes, prompt_dos_save, dos_save_forced;
static bool prompt_settings;                /* File > Project settings */
static bool panels_hidden;
static bool keys_open;                      /* Edit > Keyboard shortcuts */
static bool prompt_prefab;                  /* Edit > Save selection as prefab */
/* ---- the first-run tour: the parts of the window it points at, as drawn this frame */
enum TourSpot { TS_MAP, TS_PALETTE, TS_TOOLS, TS_PROTECT, TS_LEVELS, TS_PLAY, TS_CHECK, TS_REACH, TS_FILE, N_SPOTS };
static ImVec2 spot0[N_SPOTS], spot1[N_SPOTS];   /* (min, max; empty = not on screen) */
static void spot(TourSpot k, ImVec2 a, ImVec2 b) { spot0[k] = a; spot1[k] = b; }
static void spot_join(TourSpot k, ImVec2 a, ImVec2 b)
{
    if (spot1[k].x <= spot0[k].x) { spot(k, a, b); return; }
    spot0[k] = ImVec2(std::min(spot0[k].x, a.x), std::min(spot0[k].y, a.y));
    spot1[k] = ImVec2(std::max(spot1[k].x, b.x), std::max(spot1[k].y, b.y));
}
static int tour_step = -1;                  /* -1: no tour */
static bool tour_seen;                      /* (settings.txt: it starts by itself only once) */
static bool prompt_about;                   /* Help > About */
enum { BG_BUILD, BG_RAIN, BG_OFF };
static int start_bg = BG_BUILD;             /* View > Start screen background (settings.txt: start_bg) */
static bool app_focused = true;
static void sbg_reset(void);
#define SHRAPNEL_VERSION "0.5.0"
static void tour_start(void);
static std::string guide_path(void);
static bool prefab_take[4] = { true, true, true, true };   /* what a prefab takes: walls, background, backdrop cells, actors */                  /* Tab: the side panels hidden, the map wider */
static bool note_win;                       /* the level's message, in its own window */
static inline bool shows_message(uint16_t v) { return v == 0x3040 || v == 0x3041; }   /* note monitor, Dr. Proton screen */
static bool drawer_open;                    /* the Checks / History drawer under the map */
static int drawer_tab;                      /* 0 checks, 1 history */
static std::string status_info;             /* what the mouse is over, for the status bar */
static std::string dos_save_to;
static int check_sel = -1;                  /* the check opened in the panel (its why) */
static int focus_c = -1, focus_r = -1;      /* the cell a clicked check points at */
static double focus_until;

/* ================================================================ reachability
 * The search (port_reach.c) runs Duke's own movement code from the start over
 * every state he can be in.  It runs a slice a frame, so editing carries on,
 * and starts again a moment after the map stops changing.  What it finds
 * joins the open level's checks. */
static bool editing(void);
static void play_in_techbot(void);
static EdReach reach;                       /* the last finished search */
static uint64_t reach_key;                  /* what it was for (0: none yet) */
static bool reach_running;
static uint64_t run_key;
static int run_states;
static double reach_wait_until;             /* after an edit: let the map settle first */
static std::vector<Check> reach_checks;     /* the open level's, from reach */
static bool show_reach = true;

static EdReachOpts reach_opts(int i)        /* what Duke brings from the levels before (he keeps them) */
{
    EdReachOpts o = {};
    for (int k = 0; k < i && k < (int)proj.entries.size(); k++)
        for (uint16_t v : proj.entries[k].map) {
            if (item_base(v) == 0x3006) o.boots = 1;
            if (item_base(v) == 0x3008) o.claws = 1;
            if (item_base(v) == 0x3020) o.robohand = 1;
        }
    return o;
}
static uint64_t reach_key_now(void)
{
    if (!editing()) return 0;
    EdReachOpts o = reach_opts(cur);
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (int i = 0; i < ED_MAP_CELLS; i++) mix(lv->map[i]);
    mix((uint64_t)lv->slot); mix((uint64_t)(o.boots | o.claws << 1 | o.robohand << 2));
    return h | 1;
}
static bool reach_current(void) { return reach_key && reach_key == reach_key_now(); }

static void make_reach_checks(void)
{
    reach_checks.clear();
    const EdReach &r = reach;
    if (r.status == ED_REACH_NO_START) return;              /* (the checks say so already) */
    if (r.status == ED_REACH_TOO_BIG) {
        reach_checks.push_back({ CS_NOTE, -1, -1, "Reachability: too big to search to the end",
            "The search ran out of room (" + std::to_string(r.nodes) + " states) before trying everything, so what it shows "
            "is only part of what Duke can reach, and it reports nothing as unreachable." });
        return;
    }
    auto at = [&](uint16_t id, int &c, int &rr) {
        for (int i = 0; i < ED_MAP_CELLS; i++) if (lv->map[i] == id) { c = i % MAP_W; rr = i / MAP_W; return true; }
        return false;
    };
    const char *how = "Shrapnel tried every way Duke can move from the player start, running the game's own movement "
                      "code frame by frame: walking, jumping, falling, hanging and pulling up, with the boots, claws, keys, "
                      "card, robot hand, teleporters, elevators and conveyors he can get on the way (and breakable blocks shot away). "
                      "Enemies and damage are left out.";
    if (r.goal && !r.finish_ok) {
        int c = -1, rr = -1;
        if (!at(0x3011, c, rr) && !at(0x3043, c, rr)) at(0x3042, c, rr);
        reach_checks.push_back({ CS_BROKEN, c, rr, r.goal == 2 ? "Duke can't reach Dr. Proton from the start" : "Duke can't reach the exit from the start",
            std::string(how) + " None of it gets him there. The map shows where he can get to (green) with Reach on (Ctrl+R)." });
    }
    if (r.finish_ok && r.trap_cells) {
        int c = -1, rr = -1;
        for (int i = 0; i < ED_MAP_CELLS; i++) if (r.cell[i] & ED_REACH_TRAPPED) { c = i % MAP_W; rr = i / MAP_W; break; }
        reach_checks.push_back({ CS_WARNING, c, rr, "Places Duke can get stuck (red on the map)",
            "Duke can get to the cells shown red, but from there there is no way on to the end of the level: a drop he "
            "can't climb back out of, say. " + std::to_string(r.trap_cells) + " cells in all." });
    }
    if (!r.finish_ok && !r.goal) return;
    /* what he can't get to */
    static const char *const colour[4] = { "red", "green", "blue", "pink" };
    for (int i = 0; i < ED_MAP_CELLS; i++) {
        uint16_t v = lv->map[i];
        if (v < 0x3000 || v >= ED_ACTOR_END) continue;
        int c = i % MAP_W, rr = i / MAP_W;
        bool touch = (r.cell[i] & ED_REACH_TOUCH) != 0;
        std::string n = actor_name(v) ? actor_name(v) : "actor";
        bool needed = v != item_base(v) || v == 0x3006 || v == 0x3008 || v == 0x3020 || v == 0x3033 || (v >= 0x3044 && v <= 0x304B) || v == 0x3034 || v == 0x3035;
        bool bonus = v == 0x3015 || v == 0x3052 || v == 0x3018 || v == 0x3051 || v == 0x301D || v == 0x3050 || v == 0x301E ||
                     v == 0x3054 || v == 0x301F || v == 0x3053 || v == 0x3029 || v == 0x302D || v == 0x3055 || v == 0x302E ||
                     v == 0x3056 || (v >= 0x3037 && v <= 0x303A);
        if (!touch && needed)
            reach_checks.push_back({ CS_WARNING, c, rr, "Duke can't reach this " + n, std::string(how) + " None of it gets him to it." });
        else if (!touch && bonus)
            reach_checks.push_back({ CS_NOTE, c, rr, "Duke can't reach this " + n, std::string(how) + " None of it gets him to it." });
        else if (touch && v >= 0x3048 && v <= 0x304B && !(r.gates & ED_GATE_LOCK(v - 0x3048)))
            reach_checks.push_back({ CS_WARNING, c, rr, std::string("The ") + colour[v - 0x3048] + " lock can't be opened",
                std::string("Duke can get to the lock, but not with the ") + colour[v - 0x3048] + " key: he can't reach the key first. " + how });
    }
}

/* ================================================================ the recorded clear
 * A play from the player start that reaches the end (no helper keys) is
 * kept with the level: its controls, frame by frame.  After a change, the
 * game plays them again, unseen, on the level as it now is; if Duke no
 * longer gets to the end, the Checks say where he ended up. */
static struct { uint64_t key = 0; int end = 0, x = 0, y = 0, frames = 0; bool ok = false; } clear_check;
static uint64_t clear_seen_key;
static double clear_wait_until;
static uint64_t clear_key_now(void)
{
    if (!editing() || proj.entries[cur].clear.inputs.empty()) return 0;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (int i = 0; i < ED_MAP_CELLS; i++) mix(lv->map[i]);
    for (uint8_t b : proj.entries[cur].clear.inputs) mix(b);
    mix((uint64_t)lv->slot);
    return h | 1;
}
static const char *play_end_words(int end)
{
    return end == ED_END_EXIT ? "reached the exit" : end == ED_END_DIED ? "died" : end == ED_END_BOSS ? "beat Dr. Proton" :
           end == ED_END_FEED ? "ran out of moves" : end == ED_END_QUIT ? "quit" : "stopped";
}
static void add_clear_checks(std::vector<Check> &v)
{
    if (!clear_check.key || clear_check.key != clear_key_now() || clear_check.ok) return;
    char w[400];
    snprintf(w, sizeof w, "The play-through recorded on this level was played again on the level as it is now, with the same "
             "controls frame by frame. This time Duke %s after %.1f seconds, at column %d, row %d. Something changed "
             "is in his way, or he takes a hit he didn't before. Play the level from the start (Shift+F4) to the end "
             "to record a new clear.", play_end_words(clear_check.end), clear_check.frames / GAME_FPS, clear_check.x / 16, clear_check.y / 16);
    v.push_back({ CS_WARNING, clear_check.x / 16, std::max(0, clear_check.y / 16 - 1), "The recorded clear no longer reaches the end", w });
}

static void recheck_all(void)               /* every level: the context of each depends on the others */
{
    checks.assign(proj.entries.size(), std::vector<Check>());
    for (int i = 0; i < (int)proj.entries.size(); i++)
        run_checks(ep, i == cur ? lv->map : proj.entries[i].map.data(), proj.check_context(i), checks[i]);
    if (cur >= 0 && cur < (int)checks.size()) {
        if (reach_current())                    /* the search's findings, while they fit the map */
            checks[cur].insert(checks[cur].end(), reach_checks.begin(), reach_checks.end());
        add_clear_checks(checks[cur]);
        std::stable_sort(checks[cur].begin(), checks[cur].end(), [](const Check &a, const Check &b) { return a.sev > b.sev; });
    }
    checks_dirty = false;
    checks_ever = true;
    unchecked = 0;
}
static bool checks_stale(void) { return checks_manual && checks_dirty; }

/* a slice of the search each frame (about 12 ms of it) */
static void reach_tick(void)
{
    static int budget = 4000;
    uint64_t k = reach_key_now();
    if (reach_running && run_key != k) { ed_reach_end(); reach_running = false; reach_wait_until = now_s + 0.6; }
    if (!editing()) return;
    if (!reach_running) {
        if (k == reach_key) return;
        if (checks_manual ? !check_req : now_s < reach_wait_until) return;
        EdReachOpts o = reach_opts(cur);
        ed_reach_begin(lv->slot, lv->map, &o);
        reach_running = true;
        run_key = k;
        run_states = 0;
    }
    Uint64 t0 = SDL_GetPerformanceCounter();
    bool done = ed_reach_run(budget, &run_states) != 0;
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    if (ms > 0.5) budget = std::max(500, std::min(400000, (int)(budget * 12.0 / ms)));
    if (getenv("SHRAPNEL_REACH_LOG")) { static int n; if (n++ % 50 == 0) fprintf(stderr, "slice %.1f ms budget %d states %d used %d\n", ms, budget, run_states, ed_core_used); }
    if (done) {
        ed_reach_result(&reach);
        ed_reach_end();
        reach_running = false;
        reach_key = run_key;
        make_reach_checks();
        checks_dirty = true;
        if (getenv("SHRAPNEL_REACH_LOG")) fprintf(stderr, "reach: %d states, end %d, frame budget %d\n", reach.nodes, reach.finish_ok, budget);
    }
}

static int project_broken(void)
{
    int n = 0;
    for (auto &v : checks) n += check_count(v, CS_BROKEN);
    return n;
}

static std::string pref_dir(void)
{
    static std::string d;
    if (d.empty()) {
        char *p = SDL_GetPrefPath("Gnusto", "Shrapnel");
        d = p ? p : "./";
        SDL_free(p);
    }
    return d;
}

static void recent_save(void)
{
    FILE *f = fopen(path_join(pref_dir(), "recent.txt").c_str(), "w");
    if (!f) return;
    for (const std::string &r : recent) fprintf(f, "%s\n", r.c_str());
    fclose(f);
}

static void recent_load(void)
{
    recent.clear();
    FILE *f = fopen(path_join(pref_dir(), "recent.txt").c_str(), "r");
    if (!f) return;
    char b[4096];
    while (fgets(b, sizeof b, f)) {
        size_t n = strlen(b);
        while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0;
        if (n && recent.size() < 12) recent.push_back(b);
    }
    fclose(f);
}

/* Shrapnel's own settings (settings.txt beside recent.txt): key = value */
#define SIDE_W_DEFAULT 268.0f                 /* the side panels' widths, at 100% */
#define RIGHT_W_DEFAULT 312.0f
static float side_w = SIDE_W_DEFAULT, right_w = RIGHT_W_DEFAULT;    /* (dragged wider or narrower) */
#define DRAWER_H_DEFAULT 240.0f
static float drawer_hu = DRAWER_H_DEFAULT;  /* the drawer's height, at 100% (dragged taller or shorter) */

static void settings_save(void)
{
    FILE *f = fopen(path_join(pref_dir(), "settings.txt").c_str(), "w");
    if (!f) return;
    fprintf(f, "checks = %s\n", checks_manual ? "asked" : "live");
    fprintf(f, "theme = %s\n", theme_dark ? "dark" : "light");
    fprintf(f, "accent = %06X\n", theme_accent & 0xFFFFFF);
    fprintf(f, "sidebar = %d\npalette = %d\ndrawer = %d\n", (int)side_w, (int)right_w, (int)drawer_hu);
    fprintf(f, "ui_size = %d\n", (int)(ui_user * 100 + 0.5f));
    fprintf(f, "high_contrast = %d\ncolour_safe = %d\nanimate = %d\n", theme_hc, theme_cb, view.animate);
    fprintf(f, "tour_seen = %d\n", tour_seen);
    fprintf(f, "start_bg = %s\n", start_bg == BG_RAIN ? "rain" : start_bg == BG_OFF ? "off" : "build");
    keys_save(f);
    fclose(f);
}
static void settings_load(void)
{
    FILE *f = fopen(path_join(pref_dir(), "settings.txt").c_str(), "r");
    char b[256];
    if (!f) return;
    while (fgets(b, sizeof b, f))
        if (!strncmp(b, "checks", 6)) checks_manual = strstr(b, "asked") != NULL;
        else if (!strncmp(b, "theme", 5)) theme_dark = strstr(b, "light") == NULL;
        else if (!strncmp(b, "accent", 6) && strchr(b, '=')) theme_accent = (uint32_t)strtoul(strchr(b, '=') + 1, NULL, 16) & 0xFFFFFF;
        else if (!strncmp(b, "sidebar", 7) && strchr(b, '=')) side_w = (float)atof(strchr(b, '=') + 1);
        else if (!strncmp(b, "palette", 7) && strchr(b, '=')) right_w = (float)atof(strchr(b, '=') + 1);
        else if (!strncmp(b, "drawer", 6) && strchr(b, '=')) drawer_hu = (float)atof(strchr(b, '=') + 1);
        else if (!strncmp(b, "ui_size", 7) && strchr(b, '=')) ui_user = std::max(0.5f, std::min(3.0f, (float)atof(strchr(b, '=') + 1) / 100));
        else if (!strncmp(b, "high_contrast", 13) && strchr(b, '=')) theme_hc = atoi(strchr(b, '=') + 1) != 0;
        else if (!strncmp(b, "colour_safe", 11) && strchr(b, '=')) theme_cb = atoi(strchr(b, '=') + 1) != 0;
        else if (!strncmp(b, "animate", 7) && strchr(b, '=')) view.animate = atoi(strchr(b, '=') + 1) != 0;
        else if (!strncmp(b, "start_bg", 8)) start_bg = strstr(b, "rain") ? BG_RAIN : strstr(b, "off") ? BG_OFF : BG_BUILD;
        else if (!strncmp(b, "tour_seen", 9) && strchr(b, '=')) tour_seen = atoi(strchr(b, '=') + 1) != 0;
        else keys_load(b);                          /* key.<action> = ... */
    fclose(f);
}
static void run_check(void)                 /* Check (F7) */
{
    check_req = true;
    recheck_all();
    drawer_open = true;                         /* show what they find */
    drawer_tab = 0;
    say(checks_manual ? "Checking: the rules now, then whether the level can be finished." : "Checking.");
}
static void set_checks_manual(bool on)
{
    checks_manual = on;
    settings_save();
    if (!on) say("Checks run as you edit again.");
    else say("Checks now run when you press Check (" + act_keys(A_CHECK) + "). Shrapnel will remind you now and then.");
}

static void recent_add(const std::string &p)
{
    recent.erase(std::remove(recent.begin(), recent.end(), p), recent.end());
    recent.insert(recent.begin(), p);
    if (recent.size() > 12) recent.resize(12);
    recent_save();
    last_dir = path_dir(p);
}

static void sync_current(void)              /* the editor's map back into the project */
{
    if (cur >= 0) proj.entries[cur].map.assign(lv->map, lv->map + ED_MAP_CELLS);
}

static bool proj_modified(void)
{
    if (cur < 0) return false;
    if (proj.meta_dirty || hist.modified()) return true;
    for (int i = 0; i < (int)proj.entries.size(); i++)
        if (i != cur && proj.entries[i].hist.modified()) return true;
    return false;
}

static void mark_saved(void)
{
    hist.saved = hist.cur;
    for (ProjEntry &e : proj.entries) e.hist.saved = e.hist.cur;
    proj.meta_dirty = false;
}

static std::string untitled_autosave(void) { return path_join(pref_dir(), "untitled.autosave"); }
static std::string autosave_path(void)
{
    if (proj.untitled()) return untitled_autosave();
    return proj.dos ? path_join(proj.path, "SHRAPNEL.AUTOSAVE") : proj.path + ".autosave";
}

static std::string file_safe(const std::string &n)
{
    std::string o;
    for (char c : n) o += strchr("\\/:*?\"<>|", c) ? '_' : c;
    return o.empty() ? std::string("Untitled") : o;
}

static bool save_project_to(const std::string &where)
{
    if (checks_stale()) recheck_all();          /* (on demand: the rules, at least, before it goes) */
    if (proj.dos && !dos_save_forced) {         /* the files are what the original game will load */
        recheck_all();
        if (project_broken()) { dos_save_to = where; prompt_dos_save = true; return false; }
    }
    dos_save_forced = false;
    sync_current();
    std::string err, old_as = autosave_path();
    bool ok = proj.dos ? project_save_dos(proj, ep, where, err) : project_save_zip(proj, where, err);
    if (!ok) { say(err); return false; }
    path_remove(old_as);
    proj.path = where;
    proj.from_stock = proj.fresh = false;
    path_remove(autosave_path());
    mark_saved();
    recent_add(where);
    say(proj.dos ? "Saved the episode's 11 map files in " + where : "Saved " + path_name(where) +
                   (path_exists(where + ".bak") ? " (the previous version is " + path_name(where) + ".bak)" : ""));
    return true;
}

static bool save_project_as(void)
{
    std::string start = proj.path.empty() ? last_dir : path_dir(proj.path);
    std::string p = proj.dos ? dialog_folder(start, "Choose a folder for the DOS episode's map files")
                             : dialog_save_project(start, file_safe(proj.name) + (proj.episode ? ".epi" : ".lev"), proj.episode);
    if (p.empty()) { say("Save as: nothing chosen (on Linux this needs zenity)"); return false; }
    return save_project_to(p);
}

static bool save_project(void) { return proj.untitled() ? save_project_as() : save_project_to(proj.path); }

static void autosave_tick(void)
{
    if (cur < 0 || !proj_modified()) { edits = 0; return; }
    static double every = getenv("SHRAPNEL_AUTOSAVE_SECS") ? atof(getenv("SHRAPNEL_AUTOSAVE_SECS")) : 120;   /* (test aid) */
    if (now_s - last_autosave < every || !edits) return;
    last_autosave = now_s;
    sync_current();
    std::string err;
    if (project_save_zip(proj, autosave_path(), err, "saved_from = " + proj.path + "\n", false)) edits = 0;
}

/* ================================================================ prompts */
static std::function<void()> after_prompt;      /* what to do once changes are dealt with */
static bool prompt_unsaved;
static bool prompt_replace;

/* run 'then' now, or once the user has saved or discarded the changes */
static void guard_changes(std::function<void()> then)
{
    if (!proj_modified()) { then(); return; }
    after_prompt = then;
    prompt_unsaved = true;
}

static void entry_opened(void)
{
    std::fill(ed.under.begin(), ed.under.end(), -1);
    recount(false);
    sel.clear();
    ed.floating = false;
    trail.clear();
    build_level_textures();
    pending_at_c = -1;
    for (int i = 0; i < ED_MAP_CELLS; i++)      /* start where the player starts */
        if (lv->map[i] == 0x3032) { pending_at_c = i % MAP_W; pending_at_r = i / MAP_W; break; }
    if (pending_at_c < 0) view.x = view.y = 0;
}

/* the backdrops entry i plays with, for the editor's loads and plays: a
 * Techbot project's own choices (a hallway: the level's before it, as
 * Techbot plays it); a DOS project's are its slot's */
static std::string drop_buf[2];
static void apply_drops(int i)
{
    memset(port_drop, 0, sizeof port_drop);
    port_techbot_rules = !proj.dos;             /* (see-through actors over backdrop B: game.c) */
    edit_see_variants = !proj.dos;
    if (proj.dos || i < 0 || i >= (int)proj.entries.size()) return;
    int l = i;
    while (l > 0 && proj.entries[l].hallway) l--;
    const ProjEntry &e = proj.entries[l];
    if (e.hallway) return;
    for (int k = 0; k < 2; k++) {
        drop_buf[k] = e.drop_data[k];
        if (!drop_buf[k].empty()) {
            port_drop[k].data = (const uint8_t *)drop_buf[k].data();
            port_drop[k].len = (long)drop_buf[k].size();
        } else {
            std::string n = e.drop[k].empty() ? stock_drop_name(e.backdrop, k) : e.drop[k];
            snprintf(port_drop[k].name, sizeof port_drop[k].name, "%s", n.c_str());
        }
    }
}

/* show entry i in the editor; the one shown before keeps its map and history */
static void open_entry(int i)
{
    if (i < 0 || i >= (int)proj.entries.size()) return;
    if (cur >= 0 && cur < (int)proj.entries.size()) { sync_current(); std::swap(proj.entries[cur].hist, hist); }
    cur = i;
    ProjEntry &e = proj.entries[i];
    apply_drops(i);
    lv->load_map(ep, proj.slot_of(i), e.map.data(), proj.label(i));
    hist = History();
    std::swap(hist, e.hist);
    if (hist.steps.empty()) hist.start(lv->map, "Opened " + proj.label(i));
    entry_opened();
}

static void close_project(void)
{
    if (cur >= 0 && proj.untitled()) path_remove(untitled_autosave());
    else if (cur >= 0) path_remove(autosave_path());
    proj = Project();
    cur = -1;
    hist = History();
    SDL_SetWindowTitle(main_win, "Shrapnel");
}

static bool load_art(int e)                     /* the episode whose art and files the project uses */
{
    if (e == ep.number) return true;
    if (e < 1 || e > 3 || !ep.available[e]) return false;
    if (!ep.load(e)) return false;
    build_episode_textures();
    return true;
}

static bool use_project(Project &p, int first = 0)
{
    if (!load_art(p.ep)) { say("This project uses episode " + std::to_string(p.ep) + ", whose DN" + std::to_string(p.ep) + ".EXE is not in the game folder."); return false; }
    cur = -1;
    proj = std::move(p);
    hist = History();
    checks.clear();
    checks_ever = false;
    last_play.clear();
    open_entry(std::max(0, std::min(first, (int)proj.entries.size() - 1)));
    edits = 0;
    last_autosave = now_s;
    if (!proj.untitled()) {
        recent_add(proj.path);
        if (path_exists(autosave_path())) prompt_autosave = true;    /* left over from a session that ended unsaved */
    }
    return true;
}

static void new_project(int e, bool dos, bool episode, int levels)
{
    if (!load_art(e)) { say("No DN" + std::to_string(e) + ".EXE in the game folder."); return; }
    Project p;
    project_new(p, ep, dos, episode, levels);
    if (use_project(p)) say(proj.dos ? "A new DOS episode: ten empty levels and the hallway. Save it in its own folder." :
                            proj.episode ? "A new episode: " + std::to_string(proj.count(false)) + " empty levels, a hallway between each." :
                            "A new level: a small room with the player start and the exit door, in the middle of the map.");
}

static void open_stock(int e, bool dos, int slot, int first = 0)
{
    std::string err;
    Project p;
    if (!load_art(e)) { say("No DN" + std::to_string(e) + ".EXE in the game folder."); return; }
    if (!project_from_stock(p, ep, dos, slot, err)) { say(err); return; }
    if (use_project(p, first)) say("A copy of the stock " + std::string(slot < 0 ? "episode" : "level") + ": saving asks where, and the game's own files stay as they are.");
}

static void open_path(const std::string &path)
{
    std::string err;
    Project p;
    if (path_is_dir(path)) {                    /* a DOS episode folder */
        int e = dos_folder_episode(path), found = 0;
        if (!e) { say("No WORLDAL1.DN1, .DN2 or .DN3 in " + path); return; }
        if (!load_art(e)) { say("That folder holds episode " + std::to_string(e) + " maps, but DN" + std::to_string(e) + ".EXE is not in the game folder."); return; }
        if (!project_load_dos(p, ep, path, err, &found)) { say(err); return; }
        if (use_project(p)) say("Opened " + path_name(path) + (found < 11 ? " (" + std::to_string(found) + " of its 11 maps there, the rest from the stock episode)" : ""));
        return;
    }
    std::string x = path_ext(path);
    if (x == ".lev" || x == ".epi" || x == ".zip" || x == ".autosave") {
        if (!project_load_zip(p, path, err)) { say(err); return; }
        if (use_project(p)) say("Opened " + path_name(path));
        return;
    }
    std::vector<uint16_t> m(ED_MAP_CELLS);      /* a loose map file: a level of its own */
    if (!read_map_file(path, m.data())) { say("Not a project, a DOS episode folder or a map file: " + path); return; }
    int e = x.size() == 4 && x[3] >= '1' && x[3] <= '3' ? x[3] - '0' : ep.number;
    if (!load_art(e)) e = ep.number;
    p.ep = e;
    p.ending = e;
    p.from_stock = true;
    p.name = path_name(path);
    ProjEntry en;
    en.map = m;
    p.entries.push_back(en);
    if (use_project(p)) say("Opened the map " + path_name(path) + " as a new level (saved as a .LEV).");
}

/* a DOS episode as a Techbot episode: the ten levels with a hallway of their own between
 * each (copies of the shared one); each level keeps its undo history */
static void convert_to_techbot(void)
{
    if (!proj.dos) return;
    sync_current();
    std::swap(proj.entries[cur].hist, hist);
    Project q;
    q.ep = proj.ep;
    q.episode = true;
    q.name = proj.name;
    q.author = proj.author;
    q.ending = proj.ep;
    q.fresh = true;
    int hall = (int)proj.entries.size() - 1, keep = cur, now = 0;
    for (int k = 0; k < 10; k++) {
        if (keep == k) now = (int)q.entries.size();
        q.entries.push_back(std::move(proj.entries[k]));
        if (k < 9) {
            ProjEntry h;
            h.hallway = true;
            h.map = proj.entries[hall].map;
            if (keep == hall && k == 0) now = (int)q.entries.size();
            q.entries.push_back(std::move(h));
        }
    }
    cur = -1;
    if (use_project(q, now)) {
        proj.meta_dirty = true;
        say("Now a Techbot episode (an .EPI): each hallway is its own map. Saving asks where; the DOS folder stays as it was.");
    }
}

static void open_project_dialog(void)
{
    std::string p = dialog_open_project(last_dir, false);
    if (p.empty()) { say("Open: nothing chosen (on Linux this needs zenity)"); return; }
    open_path(p);
}

static void restore_autosave(void)
{
    Project p;
    std::string err;
    std::map<std::string, std::string> top;
    std::string as = autosave_path();
    if (!project_load_zip(p, as, err, &top)) { say("Could not read the autosave: " + err); return; }
    p.path = proj.untitled() ? top["saved_from"] : proj.path;
    p.from_stock = proj.from_stock;
    p.fresh = proj.fresh || p.path.empty();
    int keep = cur;
    if (use_project(p, keep)) { proj.meta_dirty = true; say("Restored the autosave: save to keep it."); }
}

/* ================================================================ edit commands */
static bool editing(void) { return ep.number && cur >= 0; }

static void do_undo(void) { if (hist.cur > 0) { set_map(hist.go(hist.cur - 1)); say("Undid: " + hist.steps[hist.cur + 1].name); } }
static void do_redo(void) { if (hist.cur + 1 < (int)hist.steps.size()) { set_map(hist.go(hist.cur + 1)); say("Redid: " + hist.steps[hist.cur].name); } }

static void do_copy(void)
{
    if (!sel.any) { say("Nothing selected to copy"); return; }
    clip.copy(lv->map, sel);
    build_clip_texture();
    say("Copied " + std::to_string(clip.w) + " x " + std::to_string(clip.h) + " cells");
}

static void do_delete(const char *what)
{
    if (!sel.any) return;
    Edit e(lv->map, ed.locks);
    for (int i = 0; i < ED_MAP_CELLS; i++) if (sel.on[i]) e.set(i % MAP_W, i / MAP_W, ed.erase);
    if (e.changed) { map_changed(e.c0, e.r0, e.c1, e.r1); commit(std::string(what) + " (" + std::to_string(e.changed) + " cells)"); }
}

static void do_cut(void) { do_copy(); do_delete("Cut"); }

static void do_paste(void)
{
    if (clip.empty()) { say("The clipboard is empty"); return; }
    ed.floating = true;
    say("Click to place the paste. X / Y mirror it, Esc or right-click cancels.");
}

static void do_duplicate(void)
{
    int c0, r0, c1, r1;
    if (!sel.bounds(c0, r0, c1, r1)) { say("Select something to duplicate"); return; }
    clip.copy(lv->map, sel);
    build_clip_texture();
    Edit e(lv->map, ed.locks);
    clip.paste(e, c0 + 1, r0 + 1);
    std::vector<uint8_t> moved(ED_MAP_CELLS, 0);   /* the selection follows the copy */
    for (int i = 0; i < ED_MAP_CELLS; i++)
        if (sel.on[i] && i % MAP_W + 1 < MAP_W && i / MAP_W + 1 < MAP_H) moved[i + MAP_W + 1] = 1;
    sel.on = moved;
    sel.update();
    if (e.changed) { map_changed(e.c0, e.r0, e.c1, e.r1); commit("Duplicate (" + std::to_string(e.changed) + " cells)"); }
}

static void flip_clip(bool h)
{
    if (h) clip.flip_h(); else clip.flip_v();
    build_clip_texture();
    if (!ed.told_mirror) { say("Mirrored: cells move and facing actors turn round, but tiles are not redrawn (DN1 has no mirrored tiles)."); ed.told_mirror = true; }
}

/* Move and Flip: the selected cells taken up and put down again as cl, its
 * top-left at (c, r); the selection follows them.  What a cell is left with
 * is the backdrop, or for an actor what the game shows there.  Locked cells
 * stay where they are and aren't written over. */
static Clip lifted;                         /* the cells the Move tool is dragging */
static SDL_Texture *lifted_tex;
static void build_lifted_texture(void)
{
    if (lifted_tex) { SDL_DestroyTexture(lifted_tex); lifted_tex = NULL; }
    if (lifted.empty()) return;
    int iw = lifted.w * 16, ih = lifted.h * 16;
    std::vector<uint32_t> img(iw * ih, 0);
    for (int r = 0; r < lifted.h; r++)
        for (int c = 0; c < lifted.w; c++)
            if (lifted.on[r * lifted.w + c]) draw_value(img, iw, ih, c * 16, r * 16, lifted.v[r * lifted.w + c]);
    make_texture(&lifted_tex, iw, ih);
    SDL_UpdateTexture(lifted_tex, NULL, img.data(), iw * 4);
}
static void put_down(const Clip &cl, int c, int r, bool keep_originals, const std::string &what)
{
    Edit e(lv->map, ed.locks);
    if (!keep_originals)
        for (int i = 0; i < ED_MAP_CELLS; i++) {
            if (!sel.on[i]) continue;
            uint16_t v = lv->map[i];
            int x = i % MAP_W, y = i / MAP_W;
            e.set(x, y, cell_kind(v) == CK_ACTOR ? uncovered(lv->after, x, y, v, ed.erase) : ed.erase);
        }
    cl.paste(e, c, r);
    std::vector<uint8_t> moved(ED_MAP_CELLS, 0);
    for (int y = 0; y < cl.h; y++)
        for (int x = 0; x < cl.w; x++)
            if (cl.on[y * cl.w + x] && c + x >= 0 && c + x < MAP_W && r + y >= 0 && r + y < MAP_H) moved[(r + y) * MAP_W + c + x] = 1;
    sel.on = moved;
    sel.update();
    if (e.changed) { map_changed(e.c0, e.r0, e.c1, e.r1); commit(what + " (" + std::to_string(e.changed) + " cells)"); say(what + "."); }
}

/* Edit > Flip: the selection mirrored where it is */
static void do_flip(bool h)
{
    int c0, r0, c1, r1;
    if (!sel.bounds(c0, r0, c1, r1)) { say("Select something to flip"); return; }
    Clip cl;
    cl.copy(lv->map, sel);
    if (h) cl.flip_h(); else cl.flip_v();
    put_down(cl, c0, r0, false, h ? "Flip horizontal" : "Flip vertical");
    if (!ed.told_mirror) { say("Flipped: cells move and facing actors turn round, but tiles are not redrawn (DN1 has no mirrored tiles)."); ed.told_mirror = true; }
}

/* ================================================================ prompts drawn */
static char find_buf[8], repl_buf[8];

static void project_details(void);
static std::string value_text(uint16_t v);
struct Prefab;
static bool selection_prefab(Prefab &p);
static void prefab_picture(const Prefab &p, float w, float most);
static std::string prefab_dir(void);
static void reload_prefabs(void);
static bool level_has(uint16_t id);
static void draw_prompts(void)
{
    if (prompt_unsaved) { ImGui::OpenPopup("Unsaved changes"); prompt_unsaved = false; }
    if (prompt_autosave) { ImGui::OpenPopup("Restore autosave?"); prompt_autosave = false; }
    if (prompt_replace) { ImGui::OpenPopup("Find and replace"); prompt_replace = false; }
    if (prompt_settings) { ImGui::OpenPopup("Project settings"); prompt_settings = false; }
    static char prefab_name[64];
    if (prompt_prefab) {
        ImGui::OpenPopup("Save as a prefab");
        prompt_prefab = false;
        snprintf(prefab_name, sizeof prefab_name, "%s", "");
    }
    if (prompt_about) { ImGui::OpenPopup("About Shrapnel"); prompt_about = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460 * ui_scale, 0));
    if (ImGui::BeginPopupModal("About Shrapnel", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(font_bold);
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextUnformatted("Shrapnel " SHRAPNEL_VERSION);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("The level editor for Techbot, the enhanced PC port of Duke Nukem 1. "
                               "Make levels and episodes for Techbot, or map files for the original DOS game.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
        ImGui::TextUnformatted("Shrapnel contains nothing of the game itself: everything it shows is read from your own game files.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::SeparatorText("Made with");
        ImGui::BulletText("SDL2 - zlib licence");
        ImGui::BulletText("Dear ImGui - MIT licence");
        ImGui::BulletText("miniz - MIT licence");
        ImGui::BulletText("stb_image - public domain (MIT alternative)");
        ImGui::BulletText("Inter, by The Inter Project Authors - SIL Open Font Licence 1.1");
        ImGui::BulletText("Material Symbols, by Google - Apache Licence 2.0");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(110 * ui_scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420 * ui_scale, 0));
    if (ImGui::BeginPopupModal("Save as a prefab", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static Prefab pf;
        static bool any;
        if (ImGui::IsWindowAppearing()) { any = selection_prefab(pf); ImGui::SetKeyboardFocusHere(); }
        ImGui::TextUnformatted("Name it:");
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputTextWithHint("##pfname", "e.g. Exit room, Key door, Spike pit", prefab_name, sizeof prefab_name,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::TextUnformatted("Take from the selection:");
        static const char *const kinds[4] = { "Walls", "Background", "Backdrop cells", "Actors" };
        for (int k = 0; k < 4; k++) {
            if (k) ImGui::SameLine();
            if (ImGui::Checkbox(kinds[k], &prefab_take[k])) any = selection_prefab(pf);
        }
        ImGui::Spacing();
        if (!any) ImGui::TextColored(T.warn, "None of those in the selection.");
        else prefab_picture(pf, ImGui::GetContentRegionAvail().x, 16 * ui_scale);
        if (!pf.from.empty()) ImGui::TextDisabled("Cut from %s", pf.from.substr(0, pf.from.find(' ')).c_str());
        ImGui::Spacing();
        bool ok_name = prefab_name[0] != 0 && any;
        ImGui::BeginDisabled(!ok_name);
        if (ImGui::Button("Save", ImVec2(110 * ui_scale, 0)) || (enter && ok_name)) {
            pf.name = prefab_name;
            pf.file.clear();
            std::string err;
            if (prefab_save(prefab_dir(), pf, err)) { reload_prefabs(); say("Saved the prefab " + pf.name + " (in Prefabs, beside Tiles and Actors)"); }
            else say(err);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110 * ui_scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460 * ui_scale, 0));
    if (ImGui::BeginPopupModal("Project settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        project_details();
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Done", ImVec2(110 * ui_scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s has changes that are not saved.", proj.name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            if (save_project()) { ImGui::CloseCurrentPopup(); if (after_prompt) after_prompt(); after_prompt = nullptr; }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) {
            path_remove(autosave_path());
            mark_saved();                       /* let the action go ahead */
            ImGui::CloseCurrentPopup();
            if (after_prompt) after_prompt();
            after_prompt = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { after_prompt = nullptr; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Restore autosave?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("There is an autosave of %s, from a session that ended without saving.", proj.name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Restore it")) { ImGui::CloseCurrentPopup(); restore_autosave(); }
        ImGui::SameLine();
        if (ImGui::Button("Discard it")) { path_remove(autosave_path()); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Find and replace", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        unsigned f = (unsigned)strtoul(find_buf, NULL, 16), r = (unsigned)strtoul(repl_buf, NULL, 16);
        int n = 0;
        for (int i = 0; i < ED_MAP_CELLS; i++) n += lv->map[i] == f;
        ImGui::SetNextItemWidth(90 * ui_scale);
        ImGui::InputText("Find (hex)", find_buf, sizeof find_buf, ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::TextDisabled("%s, %d cells", cell_kind(f) == CK_ACTOR ? actor_name(f) : cell_kind_name(cell_kind(f)), n);
        ImGui::SetNextItemWidth(90 * ui_scale);
        ImGui::InputText("Replace with (hex)", repl_buf, sizeof repl_buf, ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", cell_kind(r) == CK_ACTOR ? actor_name(r) : cell_kind_name(cell_kind(r)));
        ImGui::Spacing();
        if (ImGui::Button("Select them")) { sel.matching(lv->map, (uint16_t)f); ed.tool = T_SELECT; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Replace all")) {
            Edit e(lv->map, ed.locks);
            op_replace(e, (uint16_t)f, (uint16_t)r);
            if (e.changed) {
                char what[96];
                snprintf(what, sizeof what, "Replaced %04Xh with %04Xh (%d cells)", f, r, e.changed);
                map_changed(e.c0, e.r0, e.c1, e.r1);
                commit(what);
                say(what);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (prompt_dos_save) { ImGui::OpenPopup("Problems for the DOS game"); prompt_dos_save = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Problems for the DOS game", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The episode has %d Broken check result%s: the original game would hang, crash or never finish there.",
                    project_broken(), project_broken() == 1 ? "" : "s");
        ImGui::TextDisabled("The Checks panel lists them. Save anyway to keep your work; the files just aren't ready for DOS yet.");
        ImGui::Spacing();
        if (ImGui::Button("Save anyway")) { ImGui::CloseCurrentPopup(); dos_save_forced = true; save_project_to(dos_save_to); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (prompt_remove) { ImGui::OpenPopup("Remove level"); prompt_remove = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Remove level", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        int i = cur;
        int h = i > 0 ? i - 1 : i + 1;
        ImGui::Text("Remove %s and %s from the episode?", proj.label(i).c_str(), proj.label(h).c_str());
        ImGui::TextDisabled("This cannot be undone (the saved file keeps them until you save).");
        ImGui::Spacing();
        if (ImGui::Button("Remove")) {
            std::string what = proj.label(i);
            hist = History();                   /* its history goes with it */
            int keep = cur;
            cur = -1;
            if (project_remove_level(proj, keep)) {
                open_entry(std::max(0, std::min(keep - 1, (int)proj.entries.size() - 1)));
                edits++;
                say("Removed " + what + ". The levels after it are numbered one lower.");
            } else { cur = keep; open_entry(keep); }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

/* ================================================================ menu */
static void menu_bar(void)
{
    if (!ImGui::BeginMainMenuBar()) return;
    bool file_open = ImGui::BeginMenu("File");
    spot(TS_FILE, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (file_open) {
        if (ImGui::MenuItem("Start screen...", NULL, false, editing())) guard_changes([] { close_project(); });
        if (ImGui::MenuItem("Open project (.LEV, .EPI)...", act_keys(A_OPEN).c_str(), false, ep.number != 0)) guard_changes([] { open_project_dialog(); });
        if (ImGui::MenuItem("Open DOS episode folder...", NULL, false, ep.number != 0))
            guard_changes([] { std::string d = dialog_folder(last_dir, "Open a DOS episode folder"); if (!d.empty()) open_path(d); else say("Open: nothing chosen (on Linux this needs zenity)"); });
        ImGui::Separator();
        if (ImGui::MenuItem("Save", act_keys(A_SAVE).c_str(), false, editing())) save_project();
        if (ImGui::MenuItem(proj.dos ? "Save in another folder..." : "Save as...", act_keys(A_SAVE_AS).c_str(), false, editing())) save_project_as();
        if (ImGui::MenuItem("Convert to Techbot episode", NULL, false, editing() && proj.dos)) convert_to_techbot();
        if (ImGui::MenuItem("Play in Techbot", act_keys(A_PLAY_TECHBOT).c_str(), false, editing())) play_in_techbot();
        if (ImGui::MenuItem("Revert to saved", NULL, false, editing() && !proj.untitled() && proj_modified()))
            guard_changes([] { std::string p = proj.path; open_path(p); });
        ImGui::Separator();
        if (ImGui::MenuItem("Project settings...", NULL, false, editing())) prompt_settings = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) want_quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        bool ok = editing();
        if (ImGui::MenuItem("Undo", act_keys(A_UNDO).c_str(), false, ok && hist.cur > 0)) do_undo();
        if (ImGui::MenuItem("Redo", act_keys(A_REDO).c_str(), false, ok && hist.cur + 1 < (int)hist.steps.size())) do_redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", act_keys(A_CUT).c_str(), false, ok && sel.any)) do_cut();
        if (ImGui::MenuItem("Copy", act_keys(A_COPY).c_str(), false, ok && sel.any)) do_copy();
        if (ImGui::MenuItem("Paste", act_keys(A_PASTE).c_str(), false, ok && !clip.empty())) do_paste();
        if (ImGui::MenuItem("Duplicate", act_keys(A_DUPLICATE).c_str(), false, ok && sel.any)) do_duplicate();
        if (ImGui::MenuItem("Delete", act_keys(A_DELETE).c_str(), false, ok && sel.any)) do_delete("Delete");
        ImGui::Separator();
        if (ImGui::MenuItem("Flip horizontal", act_keys(A_FLIP_H).c_str(), false, ok && sel.any)) do_flip(true);
        if (ImGui::MenuItem("Flip vertical", act_keys(A_FLIP_V).c_str(), false, ok && sel.any)) do_flip(false);
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", act_keys(A_SELECT_ALL).c_str(), false, ok)) sel.all();
        if (ImGui::MenuItem("Select none", act_keys(A_DESELECT).c_str(), false, ok && sel.any)) sel.clear();
        if (ImGui::MenuItem("Find and replace...", act_keys(A_FIND).c_str(), false, ok)) {
            snprintf(find_buf, sizeof find_buf, "%04X", ed.brush);
            snprintf(repl_buf, sizeof repl_buf, "%04X", ed.brush);
            prompt_replace = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save selection as prefab...", NULL, false, ok && sel.any)) prompt_prefab = true;
        if (ImGui::MenuItem("Level message...", NULL, false, ok)) note_win = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Keyboard shortcuts...")) keys_open = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Actors", NULL, &view.actors)) redraw_all();
        if (ImGui::MenuItem("Backdrop", act_keys(A_BACKDROP).c_str(), &compose_backdrop)) redraw_all();
        ImGui::MenuItem("Labels", NULL, &view.labels);
        ImGui::MenuItem("Grid", act_keys(A_GRID).c_str(), &view.grid);
        ImGui::MenuItem("Reach", act_keys(A_REACH).c_str(), &show_reach);
        ImGui::Separator();
        if (ImGui::MenuItem("Check now", act_keys(A_CHECK).c_str(), false, editing())) run_check();
        bool live = !checks_manual;
        if (ImGui::MenuItem("Check as you edit", NULL, &live)) set_checks_manual(!live);
        if (ImGui::MenuItem("Animate", NULL, &view.animate)) settings_save();
        ImGui::MenuItem("Play-test path", NULL, &view.trail, !trail.empty());
        if (ImGui::MenuItem("Side panels", act_keys(A_PANELS).c_str(), !panels_hidden)) panels_hidden = !panels_hidden;
        bool dr = drawer_open && drawer_tab == 0, hi = drawer_open && drawer_tab == 1;
        if (ImGui::MenuItem("Checks panel", NULL, dr, editing())) { drawer_open = !dr; drawer_tab = 0; }
        if (ImGui::MenuItem("History panel", NULL, hi, editing())) { drawer_open = !hi; drawer_tab = 1; }
        ImGui::Separator();
        if (ImGui::BeginMenu("Interface size")) {
            static const int sizes[] = { 60, 70, 80, 90, 100, 110, 125, 150, 175, 200 };
            for (int pc : sizes) {
                char l[32];
                snprintf(l, sizeof l, pc == 100 ? "%d%% (your display's)" : "%d%%", pc);
                if (ImGui::MenuItem(l, NULL, (int)(ui_user * 100 + 0.5f) == pc)) { ui_user = pc / 100.0f; ui_rescale = true; settings_save(); }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Accessibility")) {
            if (ImGui::MenuItem("High contrast", NULL, &theme_hc)) { theme_apply(ui_scale); settings_save(); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Full-strength text and edges, plainer surfaces (dark or light)");
            if (ImGui::MenuItem("Colour-blind safe colours", NULL, &theme_cb)) { theme_apply(ui_scale); settings_save(); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Results and the Reach overlay in blue, orange and vermilion instead of green, amber and red");
            if (ImGui::MenuItem("Animate tiles and actors", NULL, &view.animate)) settings_save();
            if (ImGui::MenuItem("Keyboard shortcuts...")) keys_open = true;
            ImGui::TextDisabled("Interface size and theme: below");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Start screen background")) {
            static const char *const names[] = { "Levels building themselves", "Tile rain", "None" };
            for (int k = 0; k < 3; k++)
                if (ImGui::MenuItem(names[k], NULL, start_bg == k)) { start_bg = k; sbg_reset(); settings_save(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem(ICON_DARK_MODE "  Dark", NULL, theme_dark)) { theme_dark = true; theme_apply(ui_scale); settings_save(); }
            if (ImGui::MenuItem(ICON_LIGHT_MODE "  Light", NULL, !theme_dark)) { theme_dark = false; theme_apply(ui_scale); settings_save(); }
            ImGui::SeparatorText("Accent");
            for (int k = 0; k < n_accent_choices; k++) {
                uint32_t c = accent_choices[k].rgb;
                ImVec2 q = ImGui::GetCursorScreenPos();
                float d = ImGui::GetTextLineHeight();
                char l[48];
                snprintf(l, sizeof l, "       %s", accent_choices[k].name);
                if (ImGui::MenuItem(l, NULL, (theme_accent & 0xFFFFFF) == c)) { theme_accent = c; theme_apply(ui_scale); settings_save(); }
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(q.x + d * 0.6f, q.y + d * 0.55f), d * 0.38f,
                                                            IM_COL32(c >> 16, (c >> 8) & 255, c & 255, 255));
            }
            if (ImGui::BeginMenu("       Your own...")) {
                float rgb[3] = { (theme_accent >> 16 & 255) / 255.0f, (theme_accent >> 8 & 255) / 255.0f, (theme_accent & 255) / 255.0f };
                if (ImGui::ColorPicker3("##accent", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoAlpha |
                                                         ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayHex)) {
                    theme_accent = (uint32_t)(rgb[0] * 255 + 0.5f) << 16 | (uint32_t)(rgb[1] * 255 + 0.5f) << 8 | (uint32_t)(rgb[2] * 255 + 0.5f);
                    theme_apply(ui_scale);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) settings_save();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Take the tour", NULL, false, editing())) tour_start();
        if (!editing() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Open a project first (the example level is a good one)");
        if (ImGui::MenuItem("Keyboard shortcuts...")) keys_open = true;
        std::string g = guide_path();
        if (ImGui::MenuItem("The guide (BUILDING.md)", NULL, false, !g.empty())) {
            std::string u = "file:///" + g;
            for (char &ch : u) if (ch == '\\') ch = '/';
            SDL_OpenURL(u.c_str());
        }
        if (g.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("BUILDING.md isn't beside Shrapnel");
        ImGui::Separator();
        if (ImGui::MenuItem("About Shrapnel...")) prompt_about = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

static void shortcuts(void)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput || keys_capturing() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    if (!editing() || tour_step >= 0) return;
    /* (Edit > Keyboard shortcuts changes these: keys.cpp) */
    if (act_pressed(A_CHECK)) run_check();
    if (act_pressed(A_PLAY_HERE)) { play_req.pending = true; play_req.from_start = false; }
    if (act_pressed(A_PLAY_START)) { play_req.pending = true; play_req.from_start = true; }
    if (act_pressed(A_PLAY_TECHBOT)) play_in_techbot();
    if (act_pressed(A_UNDO)) do_undo();
    if (act_pressed(A_REDO)) do_redo();
    if (act_pressed(A_SAVE)) save_project();
    if (act_pressed(A_SAVE_AS)) save_project_as();
    if (act_pressed(A_OPEN)) guard_changes([] { open_project_dialog(); });
    if (act_pressed(A_COPY)) do_copy();
    if (act_pressed(A_CUT)) do_cut();
    if (act_pressed(A_PASTE)) do_paste();
    if (act_pressed(A_DUPLICATE)) do_duplicate();
    if (act_pressed(A_SELECT_ALL)) sel.all();
    if (act_pressed(A_INVERT) && sel.any) {    /* invert the selection */
        for (auto &b : sel.on) b = !b;
        sel.update();
    }
    if (act_pressed(A_GRID)) view.grid = !view.grid;
    if (act_pressed(A_BACKDROP)) { compose_backdrop = !compose_backdrop; redraw_all(); }
    if (act_pressed(A_REACH)) show_reach = !show_reach;
    if (act_pressed(A_FIND)) {
        snprintf(find_buf, sizeof find_buf, "%04X", ed.brush);
        snprintf(repl_buf, sizeof repl_buf, "%04X", ed.brush);
        prompt_replace = true;
    }
    if (act_pressed(A_DESELECT)) { if (ed.floating) ed.floating = false; else sel.clear(); }
    if (ed.floating) {                          /* a paste following the mouse */
        if (act_pressed(A_PASTE_MIRROR)) flip_clip(true);
        if (act_pressed(A_PASTE_FLIP)) flip_clip(false);
        return;
    }
    if (act_pressed(A_DELETE)) do_delete("Delete");
    if (act_pressed(A_FLIP_H) && sel.any) do_flip(true);
    if (act_pressed(A_FLIP_V) && sel.any) do_flip(false);
    for (int t = 0; t < N_TOOLS; t++) if (act_pressed((Act)(A_TOOL_SELECT + t))) ed.tool = (Tool)t;
    if (act_pressed(A_PREV_BRUSH) && ed.prev_brush != ed.brush) {   /* back to the brush before */
        std::swap(ed.brush, ed.prev_brush);
        say("Brush: " + value_text(ed.brush) + "  (" + act_keys(A_PREV_BRUSH) + ": back to " + value_text(ed.prev_brush) + ")");
    }
    if (act_pressed(A_PANELS)) panels_hidden = !panels_hidden;   /* the map alone */
    if (act_pressed(A_FACE) && cell_kind(ed.brush) == CK_ACTOR) {   /* face the other way */
        uint16_t f = actor_flip(ed.brush);
        if (f != ed.brush) { ed.brush = f; say(std::string("Brush: ") + actor_name(f)); }
        else say(std::string(actor_name(ed.brush)) + " has no other facing");
    }
}

/* ================================================================ panels */
static void center_on(double wx, double wy, ImVec2 canvas)
{
    view.x = wx - canvas.x / 2 / view.zoom;
    view.y = wy - canvas.y / 2 / view.zoom;
}

static void zoom_at(double new_zoom, ImVec2 anchor_rel)
{
    double wx = view.x + anchor_rel.x / view.zoom, wy = view.y + anchor_rel.y / view.zoom;
    view.zoom = new_zoom;
    view.x = wx - anchor_rel.x / view.zoom;
    view.y = wy - anchor_rel.y / view.zoom;
}

static int zoom_index(void)
{
    int best = 0;
    for (int i = 0; i < N_ZOOMS; i++) if (fabs(ZOOMS[i] - view.zoom) < fabs(ZOOMS[best] - view.zoom)) best = i;
    return best;
}

static ImTextureID tid(SDL_Texture *t) { return (ImTextureID)(intptr_t)t; }

/* a tile from the atlas */
static void tile_uv(uint16_t v, ImVec2 &a, ImVec2 &b)
{
    int i = (v / 0x20) % ATLAS_TILES;
    float th = (float)(ATLAS_TILES / 16 * 16);
    a = ImVec2((i % 16) * 16 / 256.0f, (i / 16) * 16 / th);
    b = ImVec2(a.x + 16 / 256.0f, a.y + 16 / th);
}

/* a crate's contents, small, in the bottom-right corner of the square at p */
static bool contents_badge(ImDrawList *dl, ImVec2 p, float sz, uint16_t id)
{
    if (cell_kind(id) != CK_ACTOR || !contents_tex[id - 0x3000]) return false;
    float b = sz * 0.55f, x = p.x + sz - b, y = p.y + sz - b;
    dl->AddRectFilled(ImVec2(x - 1, y - 1), ImVec2(p.x + sz, p.y + sz), IM_COL32(0, 0, 0, 190), 3);
    dl->AddImage(tid(contents_tex[id - 0x3000]), ImVec2(x, y), ImVec2(x + b, y + b));
    return true;
}

/* a small picture of a value (tile, backdrop or actor) */
static void value_image(ImDrawList *dl, ImVec2 p, float sz, uint16_t v, int anim)
{
    CellKind k = cell_kind(v);
    if (k == CK_BACKDROP_A || k == CK_BACKDROP_B) {
        dl->AddImage(tid(bd_tex[k == CK_BACKDROP_B]), p, ImVec2(p.x + sz, p.y + sz), ImVec2(0, 0), ImVec2(16.0f / ED_BD_W, 16.0f / ED_BD_H));
        return;
    }
    if (k == CK_ACTOR) {                        /* its look, fitted in the square */
        int i = v - 0x3000;
        dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(48, 48, 64, 255));
        if (actor_tex[i]) {
            float f = sz / std::max(actor_w[i], actor_h[i]), w = actor_w[i] * f, h = actor_h[i] * f;
            ImVec2 q(p.x + (sz - w) / 2, p.y + (sz - h) / 2);
            dl->AddImage(tid(actor_tex[i]), q, ImVec2(q.x + w, q.y + h));
            contents_badge(dl, p, sz, v);
        } else {
            const char *t = v == 0x3002 || v == 0x3004 ? "C" : v == 0x3003 || v == 0x3005 ? "|" : "?";
            dl->AddText(ImVec2(p.x + 3, p.y + 2), IM_COL32(255, 230, 120, 255), t);
        }
        return;
    }
    ImVec2 a, b;
    tile_uv((uint16_t)(k == CK_ANIMATED ? v + anim : v), a, b);
    dl->AddImage(tid(atlas), p, ImVec2(p.x + sz, p.y + sz), a, b);
}

static std::string value_text(uint16_t v)
{
    char b[96];
    CellKind k = cell_kind(v);
    const char *tn = k == CK_ACTOR ? NULL : tile_name(v);
    if (tn) snprintf(b, sizeof b, "%04Xh  %s (%s)", v, tn, cell_kind_name(k));
    else snprintf(b, sizeof b, "%04Xh  %s", v, k == CK_ACTOR ? actor_name(v) : cell_kind_name(k));
    return b;
}

/* a Techbot episode's levels in another order: level `from` (an entry
 * index) takes level `to`'s place, the ones between shift along; the
 * hallways keep their places between the levels.  Each level keeps its map,
 * history, clear, backdrops and words. */
static void move_level(int from, int to)
{
    if (from == to || proj.dos || !proj.episode) return;
    std::vector<int> levels, halls;
    for (int i = 0; i < (int)proj.entries.size(); i++) (proj.entries[i].hallway ? halls : levels).push_back(i);
    int a = (int)(std::find(levels.begin(), levels.end(), from) - levels.begin());
    int b = (int)(std::find(levels.begin(), levels.end(), to) - levels.begin());
    if (a >= (int)levels.size() || b >= (int)levels.size()) return;
    sync_current();
    std::swap(proj.entries[cur].hist, hist);
    int was = cur;
    cur = -1;                                   /* parked: each entry's history moves with it */
    std::string what = proj.label(from);
    levels.erase(levels.begin() + a);
    levels.insert(levels.begin() + b, from);
    std::vector<ProjEntry> next;
    int nl = 0, nh = 0, now = 0, landed = 0;
    for (int i = 0; i < (int)proj.entries.size(); i++) {
        int src = proj.entries[i].hallway ? halls[nh++] : levels[nl++];
        if (src == was) now = i;
        if (src == from) landed = i;
        next.push_back(std::move(proj.entries[src]));
    }
    proj.entries = std::move(next);
    proj.meta_dirty = true;
    edits++;
    open_entry(now);
    checks_dirty = true;
    std::string where = proj.label(landed);
    say("Moved " + what + ": it is " + where + " now. The hallways stay between the levels.");
}

static void minimap(float mw)                   /* click or drag to move the view */
{
    float mh = mw * MAP_H / MAP_W;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##minimap", ImVec2(mw, mh));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (tex[0]) dl->AddImage(tid(tex[view.phase]), p, ImVec2(p.x + mw, p.y + mh));
    dl->AddRect(p, ImVec2(p.x + mw, p.y + mh), ImGui::GetColorU32(T.border));
    double s = mw / LEVEL_PX_W;
    ImVec2 a((float)(p.x + view.x * s), (float)(p.y + view.y * s));
    ImVec2 b((float)(a.x + last_canvas.x / view.zoom * s), (float)(a.y + last_canvas.y / view.zoom * s));
    dl->PushClipRect(p, ImVec2(p.x + mw, p.y + mh), true);
    dl->AddRect(a, b, ImGui::GetColorU32(T.accent), 2.0f, 0, 1.5f);
    dl->PopClipRect();
    if (ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        center_on((m.x - p.x) / s, (m.y - p.y) / s, last_canvas);
    }
}

static void level_props(void);
/* the left sidebar: the levels, the open level's properties, the overview */
static void sidebar(float y0, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(0, y0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##sidebar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    if (editing()) {
        bool can_add = !proj.dos && proj.episode;
        ImGui::PushFont(font_bold);
        ImGui::TextUnformatted(proj.name.c_str());
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
        ImGui::Text("%s, episode %d", proj.dos ? "DOS episode" : proj.episode ? "Techbot episode" : "Techbot level", proj.ep);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        {   /* LEVELS, with add and remove at the right */
            float y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(y + 7 * ui_scale);
            section_label("LEVELS");
            if (can_add) {
                float bw = ImGui::GetFrameHeight() * 0.95f;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw * 2 - 2 * ui_scale);
                ImGui::SetCursorPosY(y);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * ui_scale, 0));
                if (icon_button(ICON_ADD, "Add a level (with a hallway before it)", false, true)) {
                    sync_current();
                    std::swap(proj.entries[cur].hist, hist);
                    int keep = cur;
                    cur = -1;                       /* parked: each entry's history moves with it */
                    int n = project_add_level(proj, ep, keep);
                    if (n >= 0) { open_entry(n); edits++; say("Added " + proj.label(n) + ", with a hallway before it."); }
                    else { open_entry(keep); say("An episode holds at most " + std::to_string(PROJ_MAX_LEVELS) + " levels."); }
                }
                ImGui::SameLine();
                if (icon_button(ICON_REMOVE, "Remove this level (and its hallway)", false,
                                !proj.entries[cur].hallway && proj.count(false) >= 2)) prompt_remove = true;
                ImGui::PopStyleVar();
            }
        }
        float mini_w = std::min(w - ImGui::GetStyle().WindowPadding.x * 2, h * 0.22f * MAP_W / MAP_H);   /* (not too tall) */
        float mini_h = mini_w * MAP_H / MAP_W;
        float list_h = std::max(ImGui::GetFrameHeight() * 4, std::min((float)proj.entries.size() * ImGui::GetTextLineHeightWithSpacing() + 10 * ui_scale,
                                                                      (h - mini_h) * 0.42f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, T.panel2);
        ImVec2 levels_top = ImGui::GetCursorScreenPos();
        ImGui::BeginChild("##levels", ImVec2(0, list_h), ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        for (int i = 0; i < (int)proj.entries.size(); i++) {
            ImGui::PushID(i);
            bool mod = i == cur ? hist.modified() : proj.entries[i].hist.modified();
            bool hall = proj.entries[i].hallway;
            bool drag = can_add && !hall;
            float x0 = ImGui::GetCursorPosX();
            std::string label = std::string(hall ? "      " : "      ") + proj.label(i) + (mod ? "  \xE2\x80\xA2" : "");
            if (hall) ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
            if (ImGui::Selectable(label.c_str(), i == cur) && i != cur) open_entry(i);
            if (hall) ImGui::PopStyleColor();
            ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (drag && ImGui::IsItemHovered()) {   /* the drag handle */
                ImGui::PushFont(font_body);
                dl->AddText(ImVec2(r0.x, r0.y + 1), ImGui::GetColorU32(T.faint), ICON_DRAG_INDICATOR);
                ImGui::PopFont();
            }
            if (drag) {                         /* drag a level onto another to reorder */
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("SHRAPNEL_LEVEL", &i, sizeof i);
                    ImGui::Text("Move %s", proj.label(i).c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("SHRAPNEL_LEVEL")) {
                        int from = *(const int *)pl->Data, to = i;
                        move_level(from, to);
                        ImGui::EndDragDropTarget();
                        ImGui::PopID();
                        break;                  /* (the list changed under us) */
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    ImGui::SetItemTooltip("Drag onto another level to change the order");
            }
            (void)x0;
            if (i < (int)checks.size()) {           /* a dot for its worst check result, at the right */
                int nb = check_count(checks[i], CS_BROKEN), nw = check_count(checks[i], CS_WARNING);
                if (nb || nw) {                 /* (an icon as well as a colour: broken, or a warning) */
                    const char *ic = nb ? ICON_ERROR_FILLED : ICON_WARNING_FILLED;
                    ImVec2 ts = ImGui::CalcTextSize(ic);
                    dl->AddText(ImVec2(r1.x - ts.x - 2 * ui_scale, (r0.y + r1.y - ts.y) / 2 + 1), ImGui::GetColorU32(nb ? T.bad : T.warn), ic);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%d broken, %d warnings (see Checks)", nb, nw);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        /* the open level: backdrops and its note, as tall as they need (as last
           frame measured them), short of squeezing the overview below a minimum;
           the overview takes what is left */
        static float props_need = 200;
        float label_h = ImGui::GetTextLineHeightWithSpacing() + 14 * ui_scale;
        float rest = ImGui::GetContentRegionAvail().y - label_h;
        float mini_full = ImGui::GetContentRegionAvail().x * MAP_H / MAP_W;            /* (the overview at full width) */
        float mini_min = std::min(mini_full, std::max(90 * ui_scale, rest * 0.3f));   /* (at least a third of the room when both want more) */
        float props_h = std::max(40.0f, std::min(props_need, ImGui::GetContentRegionAvail().y - label_h - mini_min));
        ImGui::BeginChild("##props", ImVec2(0, props_h), 0);
        level_props();
        props_need = ImGui::GetCursorPosY() + 4 * ui_scale;
        ImGui::EndChild();
        spot(TS_LEVELS, levels_top, ImGui::GetItemRectMax());

        ImGui::Spacing();
        section_label("OVERVIEW");
        float room = ImGui::GetContentRegionAvail().y - 4 * ui_scale;
        minimap(std::min(ImGui::GetContentRegionAvail().x, room * MAP_W / MAP_H));
    }
    ImGui::End();
}

/* the right-hand panel: the brush and its tiles, and the history */
/* ================================================================ project details: backdrops, notes */
static SDL_Texture *drop_thumb(const std::string &file)    /* a game backdrop file's picture */
{
    static std::map<std::string, SDL_Texture *> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    SDL_Texture *t = NULL;
    std::vector<uint8_t> px(ED_BD_W * ED_BD_H);
    if (game_drop_pixels(file, px.data())) {
        std::vector<uint32_t> img(px.size());
        for (size_t i = 0; i < px.size(); i++) img[i] = 0xFF000000u | ep.pal[px[i] & 15];
        make_texture(&t, ED_BD_W, ED_BD_H);
        SDL_UpdateTexture(t, NULL, img.data(), ED_BD_W * 4);
    }
    cache[file] = t;
    return t;
}

/* the game folder's backdrops, each picture once: the open episode's file
 * first (it needs no other episode), with the names of its copies */
struct DropChoice { std::string file; std::vector<std::string> same; };
static const std::vector<DropChoice> &drop_choices(void)
{
    static std::vector<DropChoice> list;
    static int for_ep = -1;
    if (for_ep == proj.ep) return list;
    for_ep = proj.ep;
    list.clear();
    std::vector<std::string> files = game_drop_files();
    std::stable_sort(files.begin(), files.end(), [](const std::string &a, const std::string &b) {
        return (a.back() - '0' == proj.ep) > (b.back() - '0' == proj.ep);
    });
    std::vector<std::vector<uint8_t>> seen;
    for (const std::string &f : files) {
        std::vector<uint8_t> px(ED_BD_W * ED_BD_H);
        if (!game_drop_pixels(f, px.data())) continue;
        size_t k = 0;
        while (k < seen.size() && seen[k] != px) k++;
        if (k < seen.size()) { list[k].same.push_back(f); continue; }
        seen.push_back(px);
        list.push_back({ f, {} });
    }
    return list;
}

/* which stock levels of the open episode use a file, as words: "level 2 (A), level 5 (A)" */
static std::string drop_users(const std::string &file)
{
    std::string out;
    for (int l = 1; l <= 10; l++)
        for (int k = 0; k < 2; k++)
            if (stock_drop_name(l, k) == file)
                out += (out.empty() ? "" : ", ") + std::string("level ") + std::to_string(l) + (k ? " (B)" : " (A)");
    return out;
}

static void entry_backdrops_changed(void)
{
    proj.meta_dirty = true;
    edits++;
    apply_drops(cur);
    lv->load_map(ep, proj.slot_of(cur), lv->map, proj.label(cur));   /* same map, other backdrops */
    build_level_textures();
}

/* a picture made into a backdrop: shown first, so dithering can be tried */
static struct {
    bool open = false;
    int which = 0;
    std::string path;
    bool dither = false;
    std::vector<uint8_t> px;
    SDL_Texture *tex = NULL;
    std::string err;
} import_pic;

static void import_convert(void)
{
    import_pic.px.assign(ED_BD_W * ED_BD_H, 0);
    import_pic.err.clear();
    if (!picture_to_drop(import_pic.path, import_pic.dither, ep.pal, import_pic.px.data(), import_pic.err)) { import_pic.px.clear(); return; }
    std::vector<uint32_t> img(import_pic.px.size());
    for (size_t i = 0; i < img.size(); i++) img[i] = 0xFF000000u | ep.pal[import_pic.px[i] & 15];
    make_texture(&import_pic.tex, ED_BD_W, ED_BD_H);
    SDL_UpdateTexture(import_pic.tex, NULL, img.data(), ED_BD_W * 4);
}

static void import_window(void)
{
    if (const char *aid = getenv("SHRAPNEL_IMPORT")) {   /* (test aid: screenshots) */
        static int n = 5;
        if (n && !--n) { import_pic.path = aid; import_pic.which = 1; import_pic.dither = getenv("SHRAPNEL_DITHER") != NULL; import_pic.open = true; import_convert(); }
    }
    if (import_pic.open) { ImGui::OpenPopup("Picture as a backdrop"); import_pic.open = false; }
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    if (!ImGui::BeginPopupModal("Picture as a backdrop", NULL, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%s, as backdrop %c", path_name(import_pic.path).c_str(), import_pic.which ? 'B' : 'A');
    if (!import_pic.err.empty()) ImGui::TextWrapped("%s", import_pic.err.c_str());
    else if (import_pic.tex) {
        ImGui::Image((ImTextureID)(intptr_t)import_pic.tex, ImVec2(ED_BD_W * 2 * ui_scale, ED_BD_H * 2 * ui_scale));
        if (ImGui::Checkbox("Dither (for photos and smooth shading)", &import_pic.dither)) import_convert();
        hint("Made to fill 208 x 160 pixels (the middle kept), in the game's 16 colours. It repeats across the level.");
    }
    bool ok = import_pic.err.empty() && !import_pic.px.empty();
    if (!ok) ImGui::BeginDisabled();
    if (ImGui::Button("Use it", ImVec2(120 * ui_scale, 0))) {
        ProjEntry &e = proj.entries[cur];
        e.drop_data[import_pic.which] = drop_encode(import_pic.px.data());
        e.drop[import_pic.which].clear();
        entry_backdrops_changed();
        ImGui::CloseCurrentPopup();
    }
    if (!ok) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * ui_scale, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

/* backdrops A and B of the open level (Techbot projects) */
static void backdrops_section(void)
{
    ProjEntry &e = proj.entries[cur];
    float tw = std::min((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2 - ImGui::GetStyle().FramePadding.x * 2,
                        ED_BD_W * ui_scale);
    ImVec2 ts(tw, tw * ED_BD_H / ED_BD_W);
    for (int k = 0; k < 2; k++) {
        ImGui::PushID(k);
        if (k) ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::ImageButton("##bd", (ImTextureID)(intptr_t)bd_tex[k], ts, ImVec2(0, 0), ImVec2(1, 1)))
            ImGui::OpenPopup("pick");
        { static int pa = getenv("SHRAPNEL_PICK") ? 5 : 0; if (k == 0 && pa && !--pa) ImGui::OpenPopup("pick"); }   /* (test aid: screenshots) */
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to choose backdrop %c", k ? 'B' : 'A');
        std::string what = !e.drop_data[k].empty() ? "Its own picture" : e.drop[k].empty() ? stock_drop_name(e.backdrop, k) : e.drop[k];
        ImGui::Text("%c: %s", k ? 'B' : 'A', what.c_str());
        if (e.drop_data[k].empty() && !e.drop[k].empty() && !drop_thumb(e.drop[k])) {
            ImGui::TextColored(T.warn, ICON_WARNING " Missing");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("So the level's stock backdrop shows instead.");
        }
        ImGui::EndGroup();
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.85f));
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x - ImGui::GetStyle().ItemSpacing.x, ImGui::GetItemRectMin().y),
                                ImGuiCond_Appearing, ImVec2(1, 0));
        if (ImGui::BeginPopup("pick")) {
            ImGui::Text("Backdrop %c: the game's backdrops, from every episode in the game folder", k ? 'B' : 'A');
            const std::vector<DropChoice> &files = drop_choices();
            std::string now = e.drop_data[k].empty() ? (e.drop[k].empty() ? stock_drop_name(e.backdrop, k) : e.drop[k]) : "";
            ImVec2 ps(ED_BD_W * 0.5f * ui_scale, ED_BD_H * 0.5f * ui_scale);
            int last_ep = 0, col = 0;
            for (const DropChoice &c : files) {
                const std::string &f = c.file;
                SDL_Texture *t = drop_thumb(f);
                if (!t) continue;
                int fe = f.back() - '0';
                if (fe != last_ep) {
                    last_ep = fe;
                    col = 0;
                    ImGui::SeparatorText(fe == proj.ep ? ("Episode " + std::to_string(fe) + " (this one's)").c_str()
                                                       : ("Episode " + std::to_string(fe) + ": not in the ones above").c_str());
                }
                if (col % 5) ImGui::SameLine();
                col++;
                bool on = f == now || std::find(c.same.begin(), c.same.end(), now) != c.same.end();
                ImGui::PushID(f.c_str());
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::ImageButton("##f", (ImTextureID)(intptr_t)t, ps)) {
                    if (!on) {
                        e.drop[k] = f;
                        e.drop_data[k].clear();
                        entry_backdrops_changed();
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    std::string tip = f;
                    for (const std::string &o : c.same) tip += ", " + o;
                    std::string u = fe == proj.ep ? drop_users(f) : "";
                    if (!u.empty()) tip += "\nThis episode's own for " + u;
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::PopID();
            }
            if (files.empty()) ImGui::TextDisabled("No backdrop files (DROPn.DN1...) in the game folder.");
            ImGui::Separator();
            if (ImGui::Button("A picture of your own (PNG)...")) {
                std::string pth = dialog_open_picture(last_dir);
                if (!pth.empty()) {
                    import_pic.path = pth;
                    import_pic.which = k;
                    import_pic.open = true;
                    import_convert();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    /* how the two are used, with how much of this level shows each */
    int na = 0, nb = 0;
    for (int i = 0; i < ED_MAP_CELLS; i++) { if (lv->map[i] == 0) na++; else if (lv->map[i] == 0x20) nb++; }
    hint("A level has two backdrops. Its backdrop cells (the open space behind the walls) show A, "
         "except the ones painted as B, which show B: so one level can have two views, say a skyline "
         "outside and a cave further in.");
    hint("To paint B: choose B in Tiles > Backdrop and use any tool (Pencil, Rectangle, Fill...), or set the "
         "Eraser to B on its brush card. Protect the other layers to paint only the open space. "
         "Click a picture to change it.");
    char cnt[96];
    snprintf(cnt, sizeof cnt, "This level: %d cells show A, %d show B.", na, nb);
    ImGui::TextColored(T.muted, "%s", cnt);
    for (int k = 0; k < 2; k++) {                   /* one from another episode */
        const std::string &f = e.drop[k];
        int fe = f.empty() || !e.drop_data[k].empty() ? 0 : f.back() - '0';
        if (fe > 0 && fe <= 3 && fe != proj.ep)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, T.warn);
            ImGui::TextWrapped(ICON_WARNING " %c is from episode %d: players need that episode's files too, else the stock backdrop shows.",
                               k ? 'B' : 'A', fe);
            ImGui::PopStyleColor();
        }
    }
}

/* the note monitor's / Dr. Proton screen's words for an entry */
static void note_preview(const std::string lines[4])
{
    static SDL_Texture *tex;
    static std::string shown = "\x01";
    std::string key = lines[0] + '\n' + lines[1] + '\n' + lines[2] + '\n' + lines[3];
    if (key != shown) {
        const char *l[4] = { lines[0].c_str(), lines[1].c_str(), lines[2].c_str(), lines[3].c_str() };
        std::vector<uint8_t> px(ED_NOTE_W * ED_NOTE_H);
        ed_note_view(l, px.data());
        std::vector<uint32_t> img(px.size());
        for (size_t i = 0; i < px.size(); i++) img[i] = 0xFF000000u | ep.pal[px[i] & 15];
        make_texture(&tex, ED_NOTE_W, ED_NOTE_H);
        SDL_UpdateTexture(tex, NULL, img.data(), ED_NOTE_W * 4);
        shown = key;
    }
    float w = std::min(ImGui::GetContentRegionAvail().x, ED_NOTE_W * 2 * ui_scale);
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, w * ED_NOTE_H / ED_NOTE_W));
}

static int note_char(ImGuiInputTextCallbackData *d)   /* what the game's font has, and no | (PACK.INI's line break) */
{
    return d->EventChar < 32 || d->EventChar > 126 || d->EventChar == '|';
}

static void note_section(void)
{
    ProjEntry &e = proj.entries[cur];
    std::string stock[4];
    int slot = proj.dos ? proj.slot_of(cur) : cur;      /* (a Techbot episode plays its entries in order) */
    for (int k = 0; k < 4; k++) { const char *t = ed_stock_note(slot, k); stock[k] = t ? t : ""; }
    if (proj.dos) {
        if (e.hallway) { hint("The game's own notes: a different one in each hallway, fixed by the original EXE."); return; }
        note_preview(stock);
        hint("The game's own words here, fixed by the original EXE.");
        return;
    }
    bool own = e.own_note;
    if (ImGui::Checkbox("Customize", &own)) {
        if (own && !e.own_note) for (int k = 0; k < 4; k++) e.note[k] = stock[k];   /* start from what shows now */
        e.own_note = own;
        proj.meta_dirty = true;
        edits++;
    }
    if (e.own_note) {
        for (int k = 0; k < 4; k++) {
            char b[ED_NOTE_CHARS + 1];
            snprintf(b, sizeof b, "%s", e.note[k].c_str());
            ImGui::PushID(k);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##l", b, sizeof b, ImGuiInputTextFlags_CallbackCharFilter, note_char)) {
                e.note[k] = b;
                proj.meta_dirty = true;
                edits++;
            }
            ImGui::PopID();
        }
        note_preview(e.note);
    } else {
        note_preview(stock);
        hint(slot < 20 ? "The episode's own words for this place in it. Tick the box to write your own."
                       : "Past the episode's own: no words. Tick the box to write some.");
    }
    if (e.own_note) hint("Four lines of up to 23 characters.");
}

/* ---- what the panels hold (M10-A): the palette's two halves, the drawer's
 * two tabs, the Play button's options, the project's details, the level's */
static char pal_find[48];                   /* the palette's search */
static bool pal_match(const std::string &s)
{
    if (!pal_find[0]) return true;
    std::string a = s, b = pal_find;
    for (char &c : a) c = (char)tolower((unsigned char)c);
    for (char &c : b) c = (char)tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

/* the brush, large, with what it is (and what F or the eraser would do) */
static void brush_card(void)
{
    ImGuiStyle &st = ImGui::GetStyle();
    float big = 44 * ui_scale, pad = 8 * ui_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::BeginChild("##brush", ImVec2(0, big + 2 * pad), ImGuiChildFlags_Borders);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    value_image(dl, p, big, ed.brush, view.phase * 0x20);
    dl->AddRect(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + big + 1, p.y + big + 1), ImGui::GetColorU32(T.accent), 3, 0, 2 * ui_scale);
    ImGui::Dummy(ImVec2(big, big));
    ImGui::SameLine(0, st.ItemSpacing.x * 1.5f);
    ImGui::BeginGroup();
    CellKind k = cell_kind(ed.brush);
    ImGui::PushFont(font_bold);
    {   /* its name, cut short with an ellipsis where the card is narrow (the tooltip has it whole) */
        const char *nm = k == CK_ACTOR ? actor_name(ed.brush) : tile_name(ed.brush) ? tile_name(ed.brush) : cell_kind_name(k);
        std::string t = nm;
        float room = ImGui::GetContentRegionAvail().x;
        bool cut = false;
        while (t.size() > 1 && ImGui::CalcTextSize((t + (cut ? "\xE2\x80\xA6" : "")).c_str()).x > room) { t.pop_back(); cut = true; }
        if (cut) { while (!t.empty() && (t.back() == ' ' || t.back() == ',')) t.pop_back(); t += "\xE2\x80\xA6"; }
        ImGui::TextUnformatted(t.c_str());
        if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", nm);
    }
    ImGui::PopFont();
    if (k == CK_ACTOR && actor_flip(ed.brush) != ed.brush) ImGui::TextDisabled("%04Xh · F: %s", ed.brush, actor_name(actor_flip(ed.brush)));
    else if (k == CK_ACTOR) ImGui::TextDisabled("%04Xh", ed.brush);
    else {
        if (tile_name(ed.brush)) ImGui::TextDisabled("%04Xh %s · eraser", ed.brush, cell_kind_name(k));
        else ImGui::TextDisabled("%04Xh · eraser paints", ed.brush);
        ImGui::SameLine(0, 4 * ui_scale);
        if (chip("A", ed.erase == 0)) ed.erase = 0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The Eraser paints backdrop A");
        ImGui::SameLine(0, 2 * ui_scale);
        if (chip("B", ed.erase == 0x20)) ed.erase = 0x20;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The Eraser paints backdrop B");
    }
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

/* Where a tile sits in its set, from the words of its name: top row first,
 * left to right ("Top-Left", "Centre", "Bottom-Right", "Cap Left"...). */
static int tile_place(const char *n)
{
    int v = strstr(n, "Top") ? 0 : strstr(n, "Bottom") ? 2 : 1;
    int h = strstr(n, "Left") ? 0 : strstr(n, "Right") ? 2 : 1;
    return v * 3 + h;
}

/* the tiles from `from` to `to`, in their sets: each set's tiles together
 * (in place order), the sets in name order; a tile in no set is its own */
static const std::vector<std::vector<uint16_t>> &tile_groups(int from, int to)
{
    static std::map<int, std::vector<std::vector<uint16_t>>> cache;
    auto it = cache.find(from << 16 | to);
    if (it != cache.end()) return it->second;
    std::map<std::string, std::vector<uint16_t>> by;
    for (int v = from; v <= to; v += 0x20) {
        const char *st = tile_set((uint16_t)v), *nm = tile_name((uint16_t)v);
        char key[160];
        snprintf(key, sizeof key, "%s\x01%04X", st ? st : nm ? nm : "~", st ? 0 : v);   /* (ungrouped: each alone) */
        by[key].push_back((uint16_t)v);
    }
    std::vector<std::vector<uint16_t>> &g = cache[from << 16 | to];
    for (auto &kv : by) {
        std::vector<uint16_t> t = kv.second;
        std::stable_sort(t.begin(), t.end(), [](uint16_t a, uint16_t b) {
            const char *na = tile_name(a), *nb = tile_name(b);
            if (!na || !nb) return a < b;
            int pa = tile_place(na), pb = tile_place(nb);
            return pa != pb ? pa < pb : strcmp(na, nb) < 0;
        });
        g.push_back(t);
    }
    return g;
}

/* the two backdrops, big enough to tell apart */
static void pal_backdrops(void)
{
    float w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    float tw = std::min(w, ED_BD_W * 2 * ui_scale), th = tw * ED_BD_H / ED_BD_W;
    ProjEntry &e = proj.entries[cur];
    for (int k = 0; k < 2; k++) {
        uint16_t v = (uint16_t)(k ? 0x20 : 0);
        ImGui::PushID(k);
        ImVec2 q = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("bd", ImVec2(tw, th))) {
            ed.brush = v;
            if (ed.tool == T_SELECT || ed.tool == T_MOVE || ed.tool == T_MAGIC || ed.tool == T_PICK) ed.tool = T_PENCIL;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%04Xh  Backdrop %c: paints cells that show backdrop %c", v, k ? 'B' : 'A', k ? 'B' : 'A');
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddImage(tid(bd_tex[k]), q, ImVec2(q.x + tw, q.y + th));
        if (ed.brush == v) dl->AddRect(q, ImVec2(q.x + tw, q.y + th), ImGui::GetColorU32(T.accent_text), 3, 0, 2 * ui_scale);
        std::string what = !e.drop_data[k].empty() ? "its own picture" : e.drop[k].empty() ? stock_drop_name(e.backdrop, k) : e.drop[k];
        ImGui::Text("%c: %s", k ? 'B' : 'A', what.c_str());
        ImGui::Spacing();
        ImGui::PopID();
    }
    hint("These are the open level's two backdrops (chosen in the sidebar). Click one, then paint with any tool: "
         "cells painted with A show A; with B, B. To change only the open space, protect the walls, background and actors.");
}

static void pal_tiles(float w)
{
    (void)w;
    static int cat = 3;
    static const char *cats[4] = { "Backdrop", "Animated", "Background", "Solid" };
    {   /* the kinds, as chips: narrower padding when the panel is narrow */
        float tw = 0, gap = 4 * ui_scale;
        for (int k = 0; k < 4; k++) tw += ImGui::CalcTextSize(cats[k]).x;
        float pad = std::max(2.0f, std::min(ImGui::GetStyle().FramePadding.x, (ImGui::GetContentRegionAvail().x - tw - gap * 3) / 8 / 1.2f - 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, ImGui::GetStyle().FramePadding.y));
        for (int k = 0; k < 4; k++) {
            if (k) ImGui::SameLine(0, gap);
            if (chip(cats[k], cat == k && !pal_find[0])) { cat = k; pal_find[0] = 0; }
        }
        ImGui::PopStyleVar();
    }
    int from, to;
    switch (cat) {
    case 0:  from = 0; to = 0x20; break;
    case 1:  from = 0x40; to = 0x5C0; break;     /* (5E0h and up are plain background) */
    case 2:  from = 0x5E0; to = 0x17E0; break;
    default: from = 0x1800; to = 0x2FE0; break;
    }
    if (pal_find[0]) { from = 0; to = 0x2FE0; }  /* searching: every tile whose words match ("1820", "solid") */
    ImGui::BeginChild("##tiles", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (cat == 0 && !pal_find[0]) { pal_backdrops(); ImGui::EndChild(); return; }
    if ((cat >= 2 || pal_find[0])) {
        /* background and solid tiles in their sets: a set starts a new row when
           it would not fit on this one, sits on a shaded band, and the sets on
           a row have a gap between them */
        float avail = ImGui::GetContentRegionAvail().x, gap = 6 * ui_scale;
        float cell = 34 * ui_scale;
        int per_row = std::max(1, (int)(avail / cell));
        cell = floorf(avail / per_row);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        float x = 0, y = 0;
        dl->ChannelsSplit(2);
        {
            int lo = pal_find[0] ? 0x40 : from, hi = pal_find[0] ? 0x2FE0 : to;   /* (searching: all but the backdrops) */
            for (const std::vector<uint16_t> &grp : tile_groups(lo, hi)) {
                std::vector<uint16_t> t;
                for (uint16_t v : grp) {
                    if (v < 0x5E0 && (v & 0x7F)) continue;          /* (an animation: its first frame stands for it) */
                    if (!pal_find[0] || pal_match(value_text(v)) || (tile_set(v) && pal_match(tile_set(v)))) t.push_back(v);
                }
                if (t.empty()) continue;
                int k = (int)t.size();
                float need = k * cell;
                if (x > 0 && x + need > avail + 0.5f && k <= per_row) { x = 0; y += cell + gap; }   /* (a new row for it) */
                else if (x > 0) x += gap;
                if (x > 0 && x + cell > avail + 0.5f) { x = 0; y += cell + gap; }
                float bx = x, by = y;
                for (int i = 0; i < k; i++) {
                    if (x + cell > avail + 0.5f) {      /* a set longer than a row wraps */
                        dl->ChannelsSetCurrent(0);
                        if (k > 1) dl->AddRectFilled(ImVec2(o.x + bx - 2, o.y + by - 2), ImVec2(o.x + x + 2, o.y + by + cell + 2),
                                                     ImGui::GetColorU32(ImVec4(T.border.x, T.border.y, T.border.z, 0.9f)), 4 * ui_scale);
                        x = 0; y += cell + gap; bx = 0; by = y;
                    }
                    uint16_t v = t[i];
                    ImVec2 q(o.x + x, o.y + y);
                    ImGui::SetCursorScreenPos(q);
                    ImGui::PushID(v);
                    if (ImGui::InvisibleButton("t", ImVec2(cell, cell))) {
                        ed.brush = v;
                        if (ed.tool == T_SELECT || ed.tool == T_MOVE || ed.tool == T_MAGIC || ed.tool == T_ERASER || ed.tool == T_PICK) ed.tool = T_PENCIL;
                    }
                    if (ImGui::IsItemHovered()) {
                        const char *st = tile_set(v);
                        if (st) ImGui::SetTooltip("%s\nSet: %s", value_text(v).c_str(), st);
                        else ImGui::SetTooltip("%s", value_text(v).c_str());
                    }
                    ImGui::PopID();
                    dl->ChannelsSetCurrent(1);
                    float pad = 1 * ui_scale;
                    value_image(dl, ImVec2(q.x + pad, q.y + pad), cell - 2 * pad, v, view.phase * 0x20);
                    if (ed.brush == v) dl->AddRect(q, ImVec2(q.x + cell, q.y + cell), ImGui::GetColorU32(T.accent_text), 3, 0, 2 * ui_scale);
                    x += cell;
                }
                dl->ChannelsSetCurrent(0);
                if (k > 1) dl->AddRectFilled(ImVec2(o.x + bx - 2, o.y + by - 2), ImVec2(o.x + x + 2, o.y + by + cell + 2),
                                             ImGui::GetColorU32(ImVec4(T.border.x, T.border.y, T.border.z, 0.9f)), 4 * ui_scale);
            }
        }
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + y + cell + gap));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        return;
    }
    /* Animated tiles: a cell shows its tile and the next three in
     * turn, and the animations come in groups of four at multiples
     * of 80h.  So each row is one group: the animation starting at
     * its first, second, third or fourth frame.  They keep still
     * (all moving at once is unreadable) unless pointed at. */
    static bool partway = false;
    if (cat == 1 && !pal_find[0]) {
        ImGui::Checkbox("Show part-way starts", &partway);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A cell can start its animation at frame 2, 3 or 4. The game then shows that\n"
                              "tile and the next three - so its last frames come from the next row.\n"
                              "The game's own levels do this in the blue-light rows (400h / 480h),\n"
                              "which are drawn alike so the lights chase along a strip.");
    }
    bool anim_cat = cat == 1 && partway && !pal_find[0];
    bool anim_group = cat == 1 && !pal_find[0];
    float avail = ImGui::GetContentRegionAvail().x;
    float cell = anim_cat ? std::min(56 * ui_scale, avail / 4) : anim_group ? 44 * ui_scale : 34 * ui_scale;
    int per_row = anim_cat ? 4 : std::max(1, (int)(avail / cell));
    if (!anim_cat) cell = floorf(avail / per_row);     /* (fill the row: the panel can be any width) */
    ImDrawList *dl = ImGui::GetWindowDrawList();
    int n = anim_cat ? (from / 0x20) % 4 : 0;          /* 40h sits in the third column */
    if (n) { ImGui::Dummy(ImVec2(cell * n, cell)); }
    for (int v = from; v <= to; v += 0x20) {
        if (anim_group && !anim_cat && (v & 0x7F)) continue;   /* one entry per animation */
        if (pal_find[0] && !pal_match(value_text((uint16_t)v))) continue;
        if (n++ % per_row) ImGui::SameLine(0, 0);
        ImVec2 q = ImGui::GetCursorScreenPos();
        ImGui::PushID(v);
        if (ImGui::InvisibleButton("t", ImVec2(cell, cell))) {
            ed.brush = (uint16_t)v;
            if (ed.tool == T_SELECT || ed.tool == T_MOVE || ed.tool == T_MAGIC || ed.tool == T_ERASER || ed.tool == T_PICK) ed.tool = T_PENCIL;
        }
        bool hov = ImGui::IsItemHovered();
        if (hov && anim_cat)
            ImGui::SetTooltip("%s\nShows tiles %04Xh to %04Xh in turn (starting at frame %d of its group)",
                              value_text((uint16_t)v).c_str(), v, v + 0x60, (v / 0x20) % 4 + 1);
        else if (hov) ImGui::SetTooltip("%s", value_text((uint16_t)v).c_str());
        ImGui::PopID();
        float pad = 1 * ui_scale;
        value_image(dl, ImVec2(q.x + pad, q.y + pad), cell - 2 * pad, (uint16_t)v, !anim_cat || hov ? view.phase * 0x20 : 0);
        if (anim_cat && (v & 0x7F)) dl->AddRectFilled(q, ImVec2(q.x + cell, q.y + cell), IM_COL32(0, 0, 0, hov ? 0 : 90));
        if (ed.brush == v) dl->AddRect(q, ImVec2(q.x + cell, q.y + cell), ImGui::GetColorU32(T.accent_text), 3, 0, 2 * ui_scale);
    }
    ImGui::EndChild();
}

static void pal_actors(void)
{
    /* the palette: actors as the game draws them; one entry per actor,
     * facing variants together (F turns the brush round) */
    static const struct { const char *name; std::vector<uint16_t> ids; } groups[] = {
        { "Enemies", { 0x3010, 0x300C, 0x300D, 0x300B, 0x300E, 0x3013, 0x3016, 0x303B, 0x303C, 0x3022, 0x3024, 0x3043, 0x3042 } },
        { "Hazards", { 0x3009, 0x302C, 0x3058, 0x3031, 0x3057, 0x301A, 0x3021, 0x301B, 0x3007 } },
        { "Crates", { 0x3000, 0x300F, 0x3006, 0x3008, 0x3020, 0x3033, 0x3029, 0x3012, 0x3015, 0x3018, 0x301D, 0x301E,
                      0x301F, 0x302D, 0x302E, 0x3023, 0x3037, 0x3038, 0x3039, 0x303A } },
        { "Loose items (Techbot)", { 0x305A, 0x305B, 0x305C, 0x305D } },
        { "Bonuses", { 0x3050, 0x3051, 0x3052, 0x3053, 0x3054, 0x3055, 0x3056 } },
        { "Keys and doors", { 0x3044, 0x3045, 0x3046, 0x3047, 0x3048, 0x3049, 0x304A, 0x304B, 0x304C, 0x304D, 0x304E, 0x304F,
                              0x3034, 0x3035 } },
        { "Machinery", { 0x3001, 0x3002, 0x3036, 0x3019, 0x302F, 0x3030, 0x302B, 0x3014 } },
        { "Start, exit, displays", { 0x3032, 0x3011, 0x3040, 0x3041, 0x302A, 0x3025, 0x3026, 0x3027, 0x3028,
                                     0x303D, 0x303E, 0x303F } },
    };
    ImGui::BeginChild("##actors", ImVec2(0, 0), ImGuiChildFlags_Borders);
    float cell = 44 * ui_scale, avail = ImGui::GetContentRegionAvail().x;
    int per_row = std::max(1, (int)(avail / cell));
    cell = floorf(avail / per_row);                     /* (fill the row) */
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (const auto &g : groups) {
        if (proj.dos && g.ids[0] >= ED_LOOSE_FIRST) continue;   /* (the DOS game can't have them) */
        int shown = 0;
        for (uint16_t id : g.ids) shown += pal_match(actor_name(id)) || pal_match(value_text(id));
        if (!shown) continue;
        ImGui::SeparatorText(g.name);
        int n = 0;
        for (uint16_t id : g.ids) {
            if (!pal_match(actor_name(id)) && !pal_match(value_text(id))) continue;
            if (n++ % per_row) ImGui::SameLine(0, 0);
            ImVec2 q = ImGui::GetCursorScreenPos();
            ImGui::PushID(id);
            if (ImGui::InvisibleButton("a", ImVec2(cell, cell))) { ed.brush = id; ed.tool = T_PENCIL; }
            if (ImGui::IsItemHovered()) {
                std::string tip = actor_name(id);
                if (actor_flip(id) != id) tip += std::string("\nF: ") + actor_name(actor_flip(id));
                if (span_actor(id)) tip += "\nDrag along a row: start where you press, end where you let go.";
                if (actor_single(id)) tip += "\nOne per level: placing it again moves it.";
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::PopID();
            float pad = 2 * ui_scale;
            bool on = ed.brush == id || (cell_kind(ed.brush) == CK_ACTOR && actor_flip(ed.brush) == id && actor_flip(id) == ed.brush &&
                                         std::find(g.ids.begin(), g.ids.end(), ed.brush) == g.ids.end());
            value_image(dl, ImVec2(q.x + pad, q.y + pad), cell - 2 * pad, id, 0);
            /* (value_image shows a crate's contents; a label only where there is no sprite) */
            if (const char *cl = contents_tex[id - 0x3000] ? NULL : crate_label(id)) {
                ImVec2 ts = ImGui::CalcTextSize(cl);
                float sc = std::min(1.0f, (cell - 4 * pad) / ts.x) * 0.8f, fs = ImGui::GetFontSize() * sc;
                ImVec2 bp(q.x + (cell - ts.x * sc) / 2, q.y + cell - fs - pad * 2);
                dl->AddRectFilled(ImVec2(bp.x - 2, bp.y), ImVec2(bp.x + ts.x * sc + 2, bp.y + fs), IM_COL32(0, 0, 0, 200), 3);
                dl->AddText(NULL, fs, bp, IM_COL32(255, 230, 120, 255), cl);
            }
            if (on) dl->AddRect(q, ImVec2(q.x + cell, q.y + cell), ImGui::GetColorU32(T.accent_text), 3, 0, 2 * ui_scale);
        }
    }
    ImGui::EndChild();
}

static void checks_body(void)
{
    int nb_here = cur < (int)checks.size() ? check_count(checks[cur], CS_BROKEN) : 0;
    int nw_here = cur < (int)checks.size() ? check_count(checks[cur], CS_WARNING) : 0;
    const std::vector<Check> &v = cur < (int)checks.size() ? checks[cur] : std::vector<Check>();
    int nn = check_count(v, CS_NOTE);
    if (checks_stale())
        ImGui::TextColored(T.warn, ICON_WARNING "  Out of date: the level changed since these were found. Check (%s) runs them again.", act_keys(A_CHECK).c_str());
    if (!nb_here && !nw_here) ImGui::TextColored(T.good, "%s: nothing broken, no warnings.", proj.label(cur).c_str());
    else ImGui::Text("%s: %d broken, %d warning%s.", proj.label(cur).c_str(), nb_here, nw_here, nw_here == 1 ? "" : "s");
    char nl[48];
    snprintf(nl, sizeof nl, "Show notes (%d)", nn);
    ImGui::Checkbox(nl, &show_notes);
    /* the reachability search */
    if (reach_running) ImGui::TextDisabled("Reach: searching... (%.1f million states)", run_states / 1e6);
    else if (!reach_current()) ImGui::TextDisabled("Reach: waiting for the map to settle");
    else if (reach.status == ED_REACH_OK && reach.finish_ok)
        ImGui::TextColored(T.good, "Reach: the %s can be reached (one way takes %.1f s).",
                           reach.goal == 2 ? "end" : "exit", reach.route_n / GAME_FPS);
    else if (reach.status == ED_REACH_OK && reach.goal) ImGui::TextColored(T.bad, "Reach: no way to the end.");
    else if (reach.status == ED_REACH_TOO_BIG) ImGui::TextDisabled("Reach: too big to search to the end.");
    /* the other levels with problems */
    bool others = false;
    for (int i = 0; i < (int)checks.size(); i++) {
        int b = check_count(checks[i], CS_BROKEN), wn = check_count(checks[i], CS_WARNING);
        if (i == cur || (!b && !wn)) continue;
        if (!others) { ImGui::SeparatorText("Other levels"); others = true; }
        char l[96];
        snprintf(l, sizeof l, "%s: %d broken, %d warnings##o%d", proj.label(i).c_str(), b, wn, i);
        ImGui::PushStyleColor(ImGuiCol_Text, b ? T.bad : T.warn);
        if (ImGui::Selectable(l)) { open_entry(i); check_sel = -1; }
        ImGui::PopStyleColor();
    }
    if (ImGui::CollapsingHeader("Actor counts")) {
        ImGui::TextWrapped("Actors against what the game can hold (the original DOS game's limits).");
    ImGui::BeginChild("##counts", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    int shown = 0;
    for (int pass = 0; pass < 2; pass++)          /* over the limit first */
    for (int t = 1; t < ED_N_TABLES; t++) {
        int n = ed.counts[t], cap = table_cap(t);
        if (!n || (pass == 0) != (n > cap)) continue;
        shown++;
        float f = cap ? (float)n / cap : 0;
        ImVec4 col = n > cap ? T.bad : f >= 0.8f ? T.warn : T.info;
        ImGui::TextUnformatted(table_name(t));
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70 * ui_scale);
        ImGui::TextColored(n > cap ? col : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%d / %d", n, cap);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(std::min(f, 1.0f), ImVec2(-1, 4 * ui_scale), "");
        ImGui::PopStyleColor();
    }
    if (!shown) ImGui::TextDisabled("No actors with a table limit.");
    ImGui::EndChild();
    }
    ImGui::SeparatorText("This level");
    ImGui::BeginChild("##checklist", ImVec2(0, 0), ImGuiChildFlags_Borders);
    int shown = 0;
    for (int i = 0; i < (int)v.size(); i++) {
        const Check &k = v[i];
        if (k.sev == CS_NOTE && !show_notes) continue;
        shown++;
        ImGui::PushID(i);
        ImVec4 col = k.sev == CS_BROKEN ? T.bad : k.sev == CS_WARNING ? T.warn : T.info;
        ImGui::TextColored(col, "%s", k.sev == CS_BROKEN ? "BROKEN" : k.sev == CS_WARNING ? "WARNING" : "NOTE");
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        bool sel = check_sel == i;
        if (ImGui::Selectable(k.what.c_str(), sel, ImGuiSelectableFlags_None)) {
            check_sel = sel ? -1 : i;
            if (k.c >= 0) { pending_at_c = k.c; pending_at_r = k.r; focus_c = k.c; focus_r = k.r; focus_until = now_s + 2.5; }
        }
        ImGui::PopTextWrapPos();
        if (sel) {
            ImGui::Indent(12 * ui_scale);
            if (k.c >= 0) ImGui::TextDisabled("Column %d, row %d", k.c, k.r);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(k.why.c_str());
            ImGui::PopTextWrapPos();
            if (k.fix && k.c >= 0 && ImGui::Button("Fix: turn it round")) {
                Edit e(lv->map, 0);
                int fc = k.c, fr = k.r;
                e.set(fc, fr, k.fix);
                map_changed(e.c0, e.r0, e.c1, e.r1);
                commit("Turned the biter round");
                check_sel = -1;
            }
            ImGui::Unindent(12 * ui_scale);
        }
        ImGui::PopID();
    }
    if (!shown) ImGui::TextDisabled(nn ? "Only notes (tick Show notes to see them)." : "Nothing to report.");
    ImGui::EndChild();
}

static void history_body(void)
{
    ImGui::BeginChild("##hist", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)hist.steps.size(); i++) {
        ImGui::PushID(i);
        std::string t = hist.steps[i].name + (i == hist.saved ? "   (saved)" : "");
        if (i > hist.cur) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(t.c_str(), i == hist.cur) && i != hist.cur) set_map(hist.go(i));
        if (i > hist.cur) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

static void play_body(void)
{
    ImGui::TextWrapped("%s plays the level from the cell under the mouse (Duke drops to the floor below it); "
                       "%s from the player start. F4, dying or reaching the exit comes back here.",
                       act_keys(A_PLAY_HERE).c_str(), act_keys(A_PLAY_START).c_str());
    if (ImGui::Button("Play in Techbot (Ctrl+F5)")) play_in_techbot();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The whole %s in Techbot itself, from the start: its levels in order,\n"
                          "its notes, backdrops and ending, with Techbot's own settings.", proj.kind_name().c_str());
    ImGui::SeparatorText("Duke starts with");
    bool b;
    b = loadout.boots;    if (ImGui::Checkbox("Boots", &b)) loadout.boots = b;
    ImGui::SameLine(150 * ui_scale);
    b = loadout.claws;    if (ImGui::Checkbox("Grappling claws", &b)) loadout.claws = b;
    b = loadout.robohand; if (ImGui::Checkbox("Robot hand", &b)) loadout.robohand = b;
    ImGui::SameLine(150 * ui_scale);
    b = loadout.card;     if (ImGui::Checkbox("Access card", &b)) loadout.card = b;
    static const char *const kn[4] = { "Red key", "Green key", "Blue key", "Pink key" };
    for (int k = 0; k < 4; k++) {
        if (k & 1) ImGui::SameLine(150 * ui_scale);
        b = (loadout.keys >> k) & 1;
        if (ImGui::Checkbox(kn[k], &b)) loadout.keys = b ? loadout.keys | 1 << k : loadout.keys & ~(1 << k);
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##fp", &loadout.firepower, 1, 4, "Firepower %d");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##hp", &loadout.health, 1, 9, "Health %d");
    b = loadout.god; if (ImGui::Checkbox("Invulnerable", &b)) loadout.god = b;
    if (ImGui::Button("Nothing (as the game starts)")) loadout = EdLoadout{ 0, 0, 0, 0, 0, 1, 9, 0 };
    ImGui::SameLine();
    if (ImGui::Button("Everything")) loadout = EdLoadout{ 1, 1, 1, 1, 15, 4, 9, loadout.god };
    ImGui::TextDisabled("(Play from start always starts with nothing.)");
    ImGui::SeparatorText("While playing");
    ImGui::TextDisabled("F4  back to the editor\nF2  invulnerable on / off\nF3  refill health\nF10 start again\nEsc the game's quit question");
    if (!last_play.empty()) { ImGui::SeparatorText("Last play-test"); ImGui::TextWrapped("%s", last_play.c_str()); }
    ImGui::SeparatorText("Recorded clear");
    {
        Clear &cl = proj.entries[cur].clear;
        if (cl.inputs.empty()) hint("None yet. Play from the start (Shift+F4) to the end without F2 or F3, and Shrapnel keeps "
                                    "that play-through: after each change it plays it again, unseen, to see it still works.");
        else {
            ImGui::Text("%.1f seconds from start to end.", cl.inputs.size() / GAME_FPS);
            if (clear_check.key && clear_check.key == clear_key_now())
                ImGui::TextColored(clear_check.ok ? T.good : T.warn,
                                   clear_check.ok ? "It still reaches the end." : "It no longer reaches the end (see Checks).");
            else ImGui::TextDisabled("Checking it against the level as it is...");
            if (ImGui::Button("Watch it")) { play_req.pending = true; play_req.watch = true; }
            ImGui::SameLine();
            if (ImGui::Button("Forget it")) { cl.inputs.clear(); proj.meta_dirty = true; clear_check.key = 0; checks_dirty = true; }
        }
    }
}

static void project_details(void)
{
    static char name_buf[128], author_buf[128], ending_buf[1024];
    static const Project *shown;
    static std::string shown_name;
    if (shown != &proj || shown_name != proj.name) {        /* another project, or renamed elsewhere */
        snprintf(name_buf, sizeof name_buf, "%s", proj.name.c_str());
        snprintf(author_buf, sizeof author_buf, "%s", proj.author.c_str());
        snprintf(ending_buf, sizeof ending_buf, "%s", proj.ending_text.c_str());
        shown = &proj;
        shown_name = proj.name;
    }
    ImGui::TextDisabled("%s, episode %d", proj.kind_name().c_str(), proj.ep);
    if (proj.untitled()) ImGui::TextWrapped("Not saved yet: saving asks where.");
    else ImGui::TextWrapped("Saved as %s", proj.path.c_str());
    ImGui::SeparatorText("Details");
    ImGui::SetNextItemWidth(-70 * ui_scale);
    if (ImGui::InputText("Name", name_buf, sizeof name_buf)) { proj.name = shown_name = name_buf; proj.meta_dirty = true; edits++; }
    ImGui::SetNextItemWidth(-70 * ui_scale);
    if (ImGui::InputText("Author", author_buf, sizeof author_buf)) { proj.author = author_buf; proj.meta_dirty = true; edits++; }
    if (proj.dos) {
        ImGui::TextWrapped("A DOS episode is saved as episode %d's own map files: ten levels and the one hallway they share. "
                           "The original EXE fixes each level's backdrop, the hallway notes and the ending.", proj.ep);
    }
    if (proj.episode && !proj.dos) {
        ImGui::SeparatorText("Ending");
        ImGui::TextWrapped("Beating Dr. Proton ends the episode, in whichever level he is; with no Proton, so does the last level's exit.");
        static const char *endings[4] = { "Your own text", "Episode 1's ending", "Episode 2's ending", "Episode 3's ending" };
        int en = std::max(0, std::min(3, proj.ending));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##ending", &en, endings, 4)) { proj.ending = en; proj.meta_dirty = true; edits++; }
        if (proj.ending == 0) {
            if (ImGui::InputTextMultiline("##endtext", ending_buf, sizeof ending_buf, ImVec2(-1, 110 * ui_scale))) {
                proj.ending_text = ending_buf; proj.meta_dirty = true; edits++;
            }
            hint("Shown over Duke's picture, five lines of up to 23 characters a box; longer lines break at a space, "
                 "and an empty line starts a new box only where five lines are full. With no words, the last "
                 "level's exit just ends the game (beating Dr. Proton still shows the episode's own ending).");
        }
    }
}

static void level_props(void)
{
    ImGui::SeparatorText(proj.label(cur).c_str());
    if (!proj.dos && !proj.entries[cur].hallway) backdrops_section();
    else if (!proj.dos) {
        int l = cur;
        while (l > 0 && proj.entries[l].hallway) l--;
        hint(("Shows the backdrops of " + proj.label(l) + ", as the game keeps a level's for the hallway after it.").c_str());
    }
    /* the message: only where something in the level shows it (its own window) */
    if (level_has(0x3040) || level_has(0x3041)) {
        ImGui::Spacing();
        ImGui::TextUnformatted(level_has(0x3041) ? "Dr. Proton screen" : "Note monitor");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_EDIT " Message...")) note_win = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("What it says (or double-click it on the map)");
    }
}

/* the level's message: what its note monitor or Dr. Proton screen shows */
static void note_window(void)
{
    if (!note_win || !editing()) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ED_NOTE_W * 2 * ui_scale + 40 * ui_scale, 0), ImGuiCond_Always);
    std::string title = proj.label(cur) + ": message###levelmsg";
    if (ImGui::Begin(title.c_str(), &note_win, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        bool here = level_has(0x3040) || level_has(0x3041);
        hint(here ? "Shown when Duke reads the level's note monitor or meets its Dr. Proton screen. A level has one message, "
                    "whichever of them shows it."
                  : "Nothing in this level shows it yet: place a Note monitor or a Dr. Proton screen (Actors).");
        note_section();
        ImGui::Spacing();
        if (ImGui::Button("Done", ImVec2(110 * ui_scale, 0)) ||
            (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()))
            note_win = false;
    }
    ImGui::End();
}

/* ---- prefabs (prefabs.cpp) */
static std::string prefab_dir(void) { return pref_dir() + "prefabs"; }
static std::string prefab_game_file(const std::string &name)
{
    std::string p = game_path(name.c_str());
    return path_exists(p) ? p : std::string();
}
static void reload_prefabs(void) { prefabs_load(prefab_dir(), prefab_game_file); }

/* the selection as a prefab, and whether it is still exactly a piece of one
 * of the game's levels at the same place (then the copy that comes with
 * Shrapnel can be cut from the player's own files instead of shipped) */
static bool taken(uint16_t v)
{
    switch (cell_kind(v)) {
    case CK_SOLID: return prefab_take[0];
    case CK_BACKGROUND: case CK_ANIMATED: return prefab_take[1];
    case CK_BACKDROP_A: case CK_BACKDROP_B: return prefab_take[2];
    case CK_ACTOR: return prefab_take[3];
    default: return false;
    }
}

static bool selection_prefab(Prefab &p)
{
    int c0, r0, c1, r1;
    if (!sel.bounds(c0, r0, c1, r1)) return false;
    Clip k;
    k.copy(lv->map, sel);
    p = Prefab();
    /* only the kinds taken, and trimmed to them */
    int a0 = k.w, b0 = k.h, a1 = -1, b1 = -1;
    for (int r = 0; r < k.h; r++)
        for (int c = 0; c < k.w; c++) {
            int i = r * k.w + c;
            if (k.on[i] && !taken(k.v[i])) k.on[i] = 0;
            if (k.on[i]) { a0 = std::min(a0, c); b0 = std::min(b0, r); a1 = std::max(a1, c); b1 = std::max(b1, r); }
        }
    if (a1 < 0) return false;                   /* (nothing of those kinds) */
    p.w = a1 - a0 + 1; p.h = b1 - b0 + 1;
    for (int r = b0; r <= b1; r++)
        for (int c = a0; c <= a1; c++) { p.v.push_back(k.on[r * k.w + c] ? k.v[r * k.w + c] : 0); p.on.push_back(k.on[r * k.w + c]); }
    c0 += a0; r0 += b0;
    std::set<std::string> seen;
    for (int slot = 0; slot <= 20 && p.from.empty(); slot++) {
        std::string f = ep.slot_file(slot);
        if (f.empty() || !seen.insert(f).second) continue;
        uint16_t m[ED_MAP_CELLS];
        if (!read_map_file(game_path(f.c_str()), m)) continue;
        bool same = true;
        for (int r = 0; r < p.h && same; r++)
            for (int c = 0; c < p.w && same; c++)
                if (p.on[r * p.w + c] && m[(r0 + r) * MAP_W + c0 + c] != p.v[r * p.w + c]) same = false;
        if (same) p.from = f + " " + std::to_string(c0) + " " + std::to_string(r0);
    }
    return true;
}

static void use_prefab(const Prefab &p)
{
    clip.w = p.w; clip.h = p.h; clip.v = p.v; clip.on = p.on;
    build_clip_texture();
    ed.floating = true;
    say("Click to place " + p.name + ". " + act_keys(A_PASTE_MIRROR) + " / " + act_keys(A_PASTE_FLIP) + " mirror it, Esc cancels.");
}

/* a prefab's picture: drawn by the game's own code (actors in their cells,
 * backdrop cells a flat shade), at its own size, then shrunk smoothly
 * (each picture pixel the average of the ones it covers) to fit */
static SDL_Texture *prefab_thumb(const Prefab &p, int tw, int th)
{
    static std::map<std::string, SDL_Texture *> cache;
    std::string key = std::to_string(p.w) + "x" + std::to_string(p.h) + "@" + std::to_string(tw) + "," + std::to_string(th) + ":" +
                      std::to_string(ep.number) + ":";
    for (int i = 0; i < p.w * p.h; i++) { char b[8]; snprintf(b, sizeof b, "%04X%c", p.v[i], p.on[i] ? '+' : '-'); key += b; }
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (cache.size() > 200) { for (auto &kv : cache) SDL_DestroyTexture(kv.second); cache.clear(); }
    /* the prefab in an empty map (at 4, 4: room for actors drawn beyond their cell) */
    static Level scratch;
    static std::vector<uint32_t> px, bare;
    px.assign((size_t)LEVEL_PX_W * LEVEL_PX_H, 0);
    bare.assign((size_t)LEVEL_PX_W * LEVEL_PX_H, 0);
    std::vector<uint16_t> m(ED_MAP_CELLS, 0);
    const int X = 4, Y = 4;
    for (int r = 0; r < p.h; r++)
        for (int c = 0; c < p.w; c++)
            if (p.on[r * p.w + c]) m[(Y + r) * MAP_W + X + c] = p.v[r * p.w + c];
    scratch.load_map(ep, lv ? lv->slot : 0, m.data(), "prefab");
    bool keep = compose_backdrop;
    compose_backdrop = false;                   /* (backdrop cells: a flat shade, not the picture) */
    compose_area(ep, scratch, 0, true, px.data(), X, Y, X + p.w, Y + p.h);
    compose_area(ep, scratch, 0, false, bare.data(), X, Y, X + p.w, Y + p.h);   /* (without actors: to tell their pixels) */
    compose_backdrop = keep;
    if (lv) lv->refresh();                      /* (the game holds the open level again) */
    int sw = p.w * 16, sh = p.h * 16;
    std::vector<uint32_t> src((size_t)sw * sh);
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            size_t at = (size_t)(Y * 16 + y) * LEVEL_PX_W + X * 16 + x;
            uint32_t v = px[at];
            if (!p.on[(y / 16) * p.w + x / 16] && v == bare[at]) v = 0;   /* not part of it: clear (but an actor drawn over it stays) */
            src[(size_t)y * sw + x] = v;
        }
    std::vector<uint32_t> out((size_t)tw * th);
    for (int y = 0; y < th; y++)                /* area average (with alpha), for any scale down */
        for (int x = 0; x < tw; x++) {
            double x0 = (double)x * sw / tw, x1 = (double)(x + 1) * sw / tw, y0 = (double)y * sh / th, y1 = (double)(y + 1) * sh / th;
            double a = 0, rr = 0, gg = 0, bb = 0, n = 0;
            for (int sy = (int)y0; sy < (int)ceil(y1) && sy < sh; sy++)
                for (int sx = (int)x0; sx < (int)ceil(x1) && sx < sw; sx++) {
                    double wgt = (std::min(x1, sx + 1.0) - std::max(x0, (double)sx)) * (std::min(y1, sy + 1.0) - std::max(y0, (double)sy));
                    uint32_t v = src[(size_t)sy * sw + sx];
                    double al = (v >> 24) / 255.0;
                    a += al * wgt; rr += ((v >> 16) & 255) * al * wgt; gg += ((v >> 8) & 255) * al * wgt; bb += (v & 255) * al * wgt; n += wgt;
                }
            uint32_t o = 0;
            if (a > 0) o = (uint32_t)(a / n * 255 + 0.5) << 24 | (uint32_t)(rr / a + 0.5) << 16 | (uint32_t)(gg / a + 0.5) << 8 | (uint32_t)(bb / a + 0.5);
            out[(size_t)y * tw + x] = o;
        }
    SDL_Texture *t = NULL;
    make_texture(&t, tw, th);
    SDL_UpdateTexture(t, NULL, out.data(), tw * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    cache[key] = t;
    return t;
}

/* a prefab's picture, as wide as `w` at most, cells at most `most` across */
static void prefab_picture(const Prefab &p, float w, float most)
{
    float cell = std::min(most, w / p.w);
    int tw = std::max(1, (int)(cell * p.w)), th = std::max(1, (int)(cell * p.h));
    if (cell >= 16) { tw = p.w * 16; th = p.h * 16; }   /* (bigger than its own size: drawn up, crisp) */
    SDL_Texture *t = prefab_thumb(p, tw, th);
    ImGui::Image(tid(t), ImVec2(cell * p.w, cell * p.h));
}

static void pal_prefabs(void)
{
    static bool loaded;
    if (!loaded) { reload_prefabs(); loaded = true; }
    ImGui::BeginDisabled(!sel.any);
    if (ImGui::Button(ICON_ADD " Save the selection as a prefab", ImVec2(-1, 0))) prompt_prefab = true;
    ImGui::EndDisabled();
    if (!sel.any && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Select part of the level first (Select or Magic select)");
    ImGui::BeginChild("##prefabs", ImVec2(0, 0), ImGuiChildFlags_Borders);
    float w = ImGui::GetContentRegionAvail().x;
    static int renaming = -1;
    static char rename_buf[64];
    bool any_mine = false, any_game = false;
    for (int pass = 0; pass < 2; pass++) {      /* the user's first, then the ones that come with Shrapnel */
        bool header = false;
        for (int i = 0; i < (int)prefabs.size(); i++) {
            Prefab &p = prefabs[i];
            if (p.builtin != (pass == 1)) continue;
            if (pass == 0) any_mine = true; else any_game = true;
            if (pal_find[0] && !pal_match(p.name)) continue;
            if (!header) { ImGui::Spacing(); section_label(pass ? "COME WITH SHRAPNEL" : "YOURS"); header = true; }
            ImGui::PushID(i);
            ImVec2 top = ImGui::GetCursorScreenPos();
            prefab_picture(p, w - 8 * ui_scale, 20 * ui_scale);
            if (renaming == i) {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
                bool done = ImGui::InputText("##rn", rename_buf, sizeof rename_buf, ImGuiInputTextFlags_EnterReturnsTrue);
                if (done || ImGui::IsItemDeactivated()) {
                    std::string err, nm = rename_buf;
                    if (!nm.empty() && nm != p.name && !prefab_rename(prefab_dir(), p, nm, err)) say(err);
                    renaming = -1;
                    reload_prefabs();
                    ImGui::PopID();
                    break;
                }
            } else ImGui::TextUnformatted(p.name.c_str());
            ImVec2 bot = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(top);
            if (ImGui::InvisibleButton("##use", ImVec2(w, std::max(1.0f, bot.y - top.y - (renaming == i ? ImGui::GetFrameHeightWithSpacing() : 0)))))
                use_prefab(p);
            if (ImGui::IsItemHovered()) {
                ImGui::GetWindowDrawList()->AddRect(ImVec2(top.x - 2, top.y - 2), ImVec2(top.x + w, bot.y), ImGui::GetColorU32(T.accent), 4 * ui_scale);
                std::string cut = p.from.empty() ? "" : "\nCut from " + p.from.substr(0, p.from.find(' '));
                ImGui::SetTooltip("%s: %d x %d%s\nClick, then click on the map to place it.%s", p.name.c_str(), p.w, p.h,
                                  cut.c_str(), p.builtin ? "" : " Right-click: rename or delete.");
            }
            if (!p.builtin && ImGui::BeginPopupContextItem("##ctx")) {
                if (ImGui::MenuItem("Rename")) { renaming = i; snprintf(rename_buf, sizeof rename_buf, "%s", p.name.c_str()); }
                if (ImGui::MenuItem("Delete")) {
                    std::string nm = p.name;
                    prefab_remove(p);
                    reload_prefabs();
                    say("Deleted the prefab " + nm);
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                ImGui::EndPopup();
            }
            ImGui::SetCursorScreenPos(bot);
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    if (!any_mine) {
        ImGui::Spacing();
        hint("Select part of a level (a room, a door with its key, a trap), then save it as a prefab to place it "
             "again anywhere, in any project.");
    }
    if (!any_game && n_prefab_builtin > 0)
        hint("The prefabs that come with Shrapnel are cut from the game's own levels, which aren't in this game folder.");
    ImGui::Spacing();
    if (ImGui::SmallButton(ICON_FOLDER_OPEN " Open the prefabs folder")) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(prefab_dir()), ec);
        std::string u = "file:///" + prefab_dir();
        for (char &ch : u) if (ch == '\\') ch = '/';
        SDL_OpenURL(u.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Your prefabs are files there (%s): copy them to share them.", prefab_dir().c_str());
    ImGui::EndChild();
}

static int pal_side;                        /* the palette: 0 tiles, 1 actors, 2 prefabs */
static void right_panel(float x0, float y0, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x0, y0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##right", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    spot(TS_PALETTE, ImVec2(x0, y0), ImVec2(x0 + w, y0 + h));
    if (!editing()) { ImGui::End(); return; }
    static int side_aid = getenv("SHRAPNEL_TAB") && !strcmp(getenv("SHRAPNEL_TAB"), "Actors") ? 1 : 0;   /* (test aid: screenshots) */
    if (side_aid) { pal_side = 1; side_aid = 0; }
    {   /* Tiles | Actors | Prefabs */
        static const char *const sides[3] = { "Tiles", "Actors", "Prefabs" };
        segmented("##side", sides, 3, &pal_side);
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##find", pal_side == 2 ? ICON_SEARCH "  Find a prefab" : ICON_SEARCH "  Find a tile or actor",
                             pal_find, sizeof pal_find);
    if (pal_side == 2) { pal_prefabs(); ImGui::End(); return; }
    brush_card();
    if (pal_side == 0) pal_tiles(w);
    else pal_actors();
    ImGui::End();
}

/* the read-out for the cell under the cursor */
static std::string describe(int c, int r)
{
    char b[256];
    uint16_t v = lv->at(c, r), a = lv->after[r * MAP_W + c];
    if (cell_kind(v) == CK_ACTOR) {
        /* the game fills an actor's own cell at load time: which neighbour it copies */
        static const char *const from[4] = { "the cell to its left", "the cell to its right", "the cell above", "the cell below" };
        uint16_t la = ep.looks[v - 0x3000].after;
        std::string how = la >= 0x2000 && la <= 0x2060 && !(la & 0x1F) ? std::string(", copied from ") + from[(la - 0x2000) / 0x20] : "";
        snprintf(b, sizeof b, "Column %d, row %d    %s    (in play its cell shows %04Xh%s)", c, r, value_text(v).c_str(), a, how.c_str());
    }
    else
        snprintf(b, sizeof b, "Column %d, row %d    %s", c, r, value_text(v).c_str());
    return b;
}

/* what's in a crate (the game draws them all alike) */
static const char *crate_label(uint16_t v)
{
    switch (v) {
    case 0x300F: return "GUN";   case 0x3006: return "BOOTS"; case 0x3008: return "CLAW";  case 0x3020: return "HAND";
    case 0x3033: return "CARD";  case 0x3029: return "ATOM";  case 0x3012: return "DYN";   case 0x3015: return "SODA";
    case 0x3018: return "FOOD";  case 0x301D: return "BALL";  case 0x301E: return "JOY";   case 0x301F: return "DISK";
    case 0x302D: return "FLAG";  case 0x302E: return "RADIO"; case 0x3023: return "BLN";
    case 0x3037: return "D";     case 0x3038: return "U";     case 0x3039: return "K";     case 0x303A: return "E";
    }
    return NULL;
}

static const char *marker_label(uint16_t v)
{
    const char *c = crate_label(v);
    if (c) return c;
    switch (v) {
    case 0x3002: return "<<";   case 0x3003: return "<|";
    case 0x3004: return ">>";   case 0x3005: return "|>";
    case 0x3014: return "MIR";  case 0x3019: return "BRG";
    case 0x3036: return "WALK";
    }
    return NULL;
}

/* keep the level in view: no further than a cell past its edges; a smaller one stays centred */
static void clamp_view(ImVec2 size)
{
    double vw = size.x / view.zoom, vh = size.y / view.zoom;
    if (vw >= LEVEL_PX_W) view.x = -(vw - LEVEL_PX_W) / 2;
    else view.x = fmax(-16.0, fmin(view.x, LEVEL_PX_W - vw + 16));
    if (vh >= LEVEL_PX_H) view.y = -(vh - LEVEL_PX_H) / 2;
    else view.y = fmax(-16.0, fmin(view.y, LEVEL_PX_H - vh + 16));
}

/* the toolbar's next button: on this row if it fits, else on the next */

static const char *const tool_icon[N_TOOLS] = { ICON_SELECT, ICON_OPEN_WITH, ICON_MAGIC_BUTTON, ICON_EDIT, ICON_INK_ERASER,
                                                ICON_DIAGONAL_LINE, ICON_RECTANGLE, ICON_FORMAT_COLOR_FILL, ICON_COLORIZE };

/* The toolbar is one row: every item is as tall as an icon button, or centred
   on that height (chips, the PROTECT label), so the row lines up whatever the
   scale.  Text buttons get the padding that makes them that tall. */
static float tb_row_y, tb_h;                    /* (the row's top, in window coordinates, and its height) */

static void tb_place(float item_h, float gap)   /* the next item: beside the last, centred on the row */
{
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(tb_row_y + floorf((tb_h - item_h) / 2));
}

static void toolbar_sep(void)
{
    tb_place(tb_h, 10 * ui_scale);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + tb_h * 0.2f), ImVec2(p.x, p.y + tb_h * 0.8f), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(1, tb_h));
}

static bool tb_large;                           /* the toolbar at its larger size (it fits the window) */

static bool tb_icon(const char *icon, const char *tip, bool on, bool enabled, float gap)
{
    tb_place(tb_h, gap);
    return icon_button(icon, tip, on, enabled, tb_large);
}

static float chip_h(void) { return ImGui::GetFontSize() + 1.2f * ImGui::GetStyle().FramePadding.y; }
static float chip_w(const char *label) { return ImGui::CalcTextSize(label, NULL, true).x + ImGui::GetStyle().FramePadding.x * 2; }

static bool tb_chip(const char *label, bool on, float gap)
{
    tb_place(chip_h(), gap);
    return chip(label, on);
}

/* The toolbar sits over the columns below it: playing over the sidebar, the
 * editing tools over the map (close to where they are used), what is
 * protected over the palette.  When the window is too narrow for that at
 * the larger size, it falls back to one compact row. */
static void toolbar_window(float x0, float y0, float w, float h, float side, float right)
{
    ImGui::SetNextWindowPos(ImVec2(x0, y0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * ui_scale, 0));
    ImGui::Begin("##toolbar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    const float pad = 12 * ui_scale, near = 4 * ui_scale, apart = 10 * ui_scale, sep_w = 2 * apart + 1;
    const char *from_label = ICON_SKIP_PREVIOUS " From start";

    /* how wide each group is at the larger size, to choose the size */
    float B = TB_BUTTON * ui_scale;
    ImGui::PushFont(font_tb_bold);
    float play_w = ImGui::CalcTextSize(ICON_PLAY_ARROW_FILLED " Play here").x + ImGui::GetFontSize() * 2.4f;
    ImGui::PopFont();
    ImGui::PushFont(font_tb);
    float tpx = ImGui::GetStyle().FramePadding.x * 1.2f;
    float play_btn_w = play_w + 1 + ImGui::CalcTextSize(ICON_EXPAND_MORE).x + ImGui::GetStyle().FramePadding.x * 0.8f;
    float from_w = ImGui::CalcTextSize(from_label).x + tpx * 2, from_icon_w = B;
    ImGui::PopFont();
    float chips = 0;
    if (ed.tool == T_RECT) chips += apart + chip_w("Filled");
    if (ed.floating) chips += apart + chip_w("Mirror (X)") + near + chip_w("Flip (Y)");
    float canvas_w = 16 * B + 13 * near + 2 * sep_w + chips;
    float prot_w = ImGui::CalcTextSize("PROTECT").x * 0.8f + 8 * ui_scale + 4 * B + 3 * near;
    bool from_icon = play_btn_w + apart + from_w > side - pad + 60 * ui_scale;   /* (a narrow sidebar: From start as an icon) */
    float play_need = play_btn_w + apart + (from_icon ? from_icon_w : from_w);
    tb_large = pad + play_need + apart + canvas_w + apart + prot_w + pad <= w;

    if (tb_large) { tb_h = B; ImGui::PushFont(font_tb); }
    else tb_h = ImGui::GetFrameHeight() + 4 * ImGui::GetStyle().FramePadding.y / 5;
    tb_row_y = floorf((h - tb_h) / 2);
    ImVec2 text_pad(ImGui::GetStyle().FramePadding.x * 1.2f, (tb_h - ImGui::GetFontSize()) / 2);   /* text buttons as tall as the icons */

    ImGui::SetCursorPos(ImVec2(pad, tb_row_y));
    {   /* Play here, with its menu; From start */
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, text_pad);
        std::string ph = "Play from the middle of the view (" + act_keys(A_PLAY_HERE) + ": from the cell under the mouse). F4 comes back.";
        std::string ps = "Play from the player start (" + act_keys(A_PLAY_START) + "); reaching the end records a clear.";
        ImVec2 play0 = ImGui::GetCursorScreenPos();
        if (split_button("Play here", ICON_PLAY_ARROW_FILLED, "##playmenu", ph.c_str(),
                         tb_large ? font_tb_bold : NULL))
            { play_req.pending = true; play_req.from_start = false; }
        bool go;
        if (tb_large && from_icon) go = tb_icon(ICON_SKIP_PREVIOUS, ("From start: " + ps).c_str(), false, true, apart);
        else {
            tb_place(tb_h, apart);
            go = ImGui::Button(from_label);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", ps.c_str());
        }
        if (go) { play_req.pending = true; play_req.from_start = true; }
        spot(TS_PLAY, play0, ImGui::GetItemRectMax());
        ImGui::PopStyleVar();
        ImGui::SetNextWindowSize(ImVec2(330 * ui_scale, 0));
        if (ImGui::BeginPopup("##playmenu")) {
            if (tb_large) ImGui::PushFont(font_body);
            play_body();
            if (tb_large) ImGui::PopFont();
            ImGui::EndPopup();
        }
    }
    /* the editing tools, from the map's left edge */
    float wx = ImGui::GetWindowPos().x, gap0 = apart;
    if (tb_large) {
        float end = ImGui::GetItemRectMax().x - wx;
        gap0 = std::max(apart, side + pad - end);
    } else toolbar_sep();
    for (int t = 0; t < N_TOOLS; t++) {
        char tip[200];
        snprintf(tip, sizeof tip, "%s (%s)\n%s", tool_info[t].name, act_keys((Act)(A_TOOL_SELECT + t)).c_str(), tool_info[t].help);
        if (tb_icon(tool_icon[t], tip, ed.tool == t, true, t ? near : gap0)) { ed.tool = (Tool)t; ed.floating = false; }
        spot_join(TS_TOOLS, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }
    if (tb_large) ImGui::PushFont(font_body);       /* (chips at the usual size) */
    if (ed.tool == T_RECT && tb_chip("Filled", ed.rect_filled, apart)) ed.rect_filled = !ed.rect_filled;
    if (ed.floating) {
        if (tb_chip("Mirror (X)", false, apart)) flip_clip(true);
        if (tb_chip("Flip (Y)", false, near)) flip_clip(false);
    }
    if (tb_large) ImGui::PopFont();
    toolbar_sep();
    if (tb_icon(ICON_UNDO, "Undo (Ctrl+Z)", false, hist.cur > 0, apart)) do_undo();
    if (tb_icon(ICON_REDO, "Redo (Ctrl+Y)", false, hist.cur + 1 < (int)hist.steps.size(), near)) do_redo();
    spot_join(TS_TOOLS, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    toolbar_sep();
    if (tb_icon(ICON_GRID_4X4, "Grid (Ctrl+G)", view.grid, true, apart)) view.grid = !view.grid;
    if (tb_icon(ICON_PERSON, "Actors", view.actors, true, near)) { view.actors = !view.actors; redraw_all(); }
    if (tb_icon(ICON_LABEL, "Labels: what the game shows only as tiles", view.labels, true, near)) view.labels = !view.labels;
    if (tb_icon(ICON_LANDSCAPE, "Backdrop (Ctrl+B)", compose_backdrop, true, near)) { compose_backdrop = !compose_backdrop; redraw_all(); }
    if (tb_icon(ICON_ROUTE, "Reach (Ctrl+R): where Duke can get to (green), where he gets stuck (red), one way to the end",
                show_reach, true, near))
        show_reach = !show_reach;
    spot(TS_REACH, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    /* what painting, filling, pasting and moving leave alone: over the palette */
    float pgap = apart;
    if (tb_large) {
        float end = ImGui::GetItemRectMax().x - wx;
        pgap = std::max(apart, std::min(w - right + pad, w - pad - prot_w) - end);
        ImGui::PopFont();
    } else toolbar_sep();
    {
        tb_place(ImGui::GetFontSize() * 0.8f + (tb_large ? 22 : 11) * ui_scale, pgap);   /* (capitals sit low in the line: lift them) */
        section_label("PROTECT");
        spot_join(TS_PROTECT, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        static const char *const icons[4] = { ICON_BRICK, ICON_TEXTURE, ICON_LANDSCAPE, ICON_PERSON };
        static const char *const tips[4] = { "Protect the walls (solid tiles)", "Protect the background tiles",
                                             "Protect the backdrop cells", "Protect the actors" };
        for (int k = 0; k < 4; k++) {
            bool on = (ed.locks >> k) & 1;
            ImGui::PushID(k + 100);
            char t[96];
            snprintf(t, sizeof t, "%s%s", tips[k], on ? " (on)" : "");
            if (tb_icon(icons[k], t, on, true, k ? near : 8 * ui_scale)) ed.locks ^= 1 << k;
            spot_join(TS_PROTECT, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::PopID();
            if (on) {                           /* a padlock on its corner */
                ImVec2 r = ImGui::GetItemRectMax();
                ImGui::PushFont(font_body);
                ImGui::SetWindowFontScale(0.75f);
                ImVec2 ts = ImGui::CalcTextSize(ICON_LOCK_FILLED);
                ImGui::GetWindowDrawList()->AddText(ImVec2(r.x - ts.x + 1, r.y - ts.y - 1), ImGui::GetColorU32(T.accent_text), ICON_LOCK_FILLED);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopFont();
            }
        }
    }
    ImGui::End();
}

/* Shift while dragging a line or rectangle: a line snaps to across, down or
 * a diagonal; a rectangle becomes a square (as in most painting programs) */
static void shape_end(int &c, int &r)
{
    if (!ImGui::GetIO().KeyShift || (ed.tool != T_LINE && ed.tool != T_RECT)) return;
    int dc = c - ed.drag_c, dr = r - ed.drag_r, ac = abs(dc), ar = abs(dr);
    if (ed.tool == T_RECT || (ac && ar && ac * 2 > ar && ar * 2 > ac)) {   /* a square, or a diagonal */
        int n = std::max(ac, ar);
        c = ed.drag_c + (dc < 0 ? -n : n);
        r = ed.drag_r + (dr < 0 ? -n : n);
    } else if (ac >= ar) r = ed.drag_r;                /* across */
    else c = ed.drag_c;                                /* down */
    c = std::max(0, std::min(MAP_W - 1, c));
    r = std::max(0, std::min(MAP_H - 1, r));
}

/* cells the line or rectangle being dragged would paint (for the preview) */
static void shape_cells(std::vector<int> &out)
{
    out.clear();
    if (!ed.dragging || (ed.tool != T_LINE && ed.tool != T_RECT)) return;
    static std::vector<uint16_t> scratch(ED_MAP_CELLS);
    std::fill(scratch.begin(), scratch.end(), 0);
    Edit e(scratch.data(), 0);
    int c = ed.last_c, r = ed.last_r;
    shape_end(c, r);
    if (ed.tool == T_LINE) op_line(e, ed.drag_c, ed.drag_r, c, r, 1);
    else op_rect(e, ed.drag_c, ed.drag_r, c, r, 1, ed.rect_filled);
    for (int i = 0; i < ED_MAP_CELLS; i++) if (scratch[i]) out.push_back(i);
}

static void stroke_begin(void)
{
    ed.stroke_cells = 0;
    ed.sc0 = MAP_W; ed.sr0 = MAP_H; ed.sc1 = ed.sr1 = -1;
}

static void stroke_add(const Edit &e)
{
    ed.stroke_walled += e.walled;
    if (!e.changed) return;
    ed.stroke_cells += e.changed;
    ed.sc0 = std::min(ed.sc0, e.c0); ed.sr0 = std::min(ed.sr0, e.r0);
    ed.sc1 = std::max(ed.sc1, e.c1); ed.sr1 = std::max(ed.sr1, e.r1);
    map_changed(e.c0, e.r0, e.c1, e.r1);
}

static void stroke_end(const char *what)
{
    if (ed.stroke_cells) commit(std::string(what) + " (" + std::to_string(ed.stroke_cells) + (ed.stroke_cells == 1 ? " cell)" : " cells)"));
    if (ed.stroke_walled)                       /* an actor whose background is copied from a wall */
        say("The game copies " + std::string(ed.stroke_walled == 1 ? "that actor's" : "those actors'") +
            " background from the wall beside " + (ed.stroke_walled == 1 ? "it" : "them") +
            ", so it stays as that wall. Move the actor away from the wall to give it another.");
    ed.stroke_walled = 0;
}

/* the mouse on the map: what the current tool does */
static bool level_has(uint16_t id)
{
    for (int i = 0; i < ED_MAP_CELLS; i++) if (item_base(lv->map[i]) == id) return true;   /* (boxed or loose alike) */
    return false;
}

/* Is map pixel (x, y) on the picture of a note monitor or Dr. Proton
 * screen, any part of it (they are drawn bigger than their cell)? */
/* The actor whose picture covers map pixel (x, y): actors are kept in one
 * cell but drawn bigger (Duke is two cells tall, the Dr. Proton screen two
 * by two), so a click on any part of the picture means that actor.  The cell
 * itself wins; else the nearest actor whose picture covers the pixel. */
static bool actor_under(double x, double y, int *ac, int *ar)
{
    int c0 = (int)floor(x / 16), r0 = (int)floor(y / 16);
    if (c0 >= 0 && r0 >= 0 && c0 < MAP_W && r0 < MAP_H && cell_kind(lv->at(c0, r0)) == CK_ACTOR) { *ac = c0; *ar = r0; return true; }
    int best = 1 << 30;
    for (int r = r0 - 4; r <= r0 + 4; r++)
        for (int c = c0 - 4; c <= c0 + 4; c++) {
            if (c < 0 || r < 0 || c >= MAP_W || r >= MAP_H || cell_kind(lv->at(c, r)) != CK_ACTOR) continue;
            int id = lv->at(c, r) - 0x3000;
            if (id < 0 || id >= N_ACTORS) continue;
            for (const Piece &pc : ep.looks[id].pieces) {
                double px = c * 16 + pc.dx, py = r * 16 + pc.dy;
                int d = (c - c0) * (c - c0) + (r - r0) * (r - r0);
                if (x >= px && x < px + 16 && y >= py && y < py + 16 && d < best) { best = d; *ac = c; *ar = r; }
            }
        }
    return best < (1 << 30);
}

/* the box around an actor's picture, in map pixels */
static bool actor_box(int c, int r, double *x0, double *y0, double *x1, double *y1)
{
    int id = lv->at(c, r) - 0x3000;
    *x0 = c * 16; *y0 = r * 16; *x1 = *x0 + 16; *y1 = *y0 + 16;
    if (id < 0 || id >= N_ACTORS) return false;
    for (const Piece &pc : ep.looks[id].pieces) {
        *x0 = std::min(*x0, (double)(c * 16 + pc.dx)); *y0 = std::min(*y0, (double)(r * 16 + pc.dy));
        *x1 = std::max(*x1, (double)(c * 16 + pc.dx + 16)); *y1 = std::max(*y1, (double)(r * 16 + pc.dy + 16));
    }
    return true;
}

static bool on_message_actor(double x, double y)
{
    int c, r;
    return actor_under(x, y, &c, &r) && shows_message(lv->at(c, r));
}
static double mouse_fx, mouse_fy;              /* the map pixel under the mouse */

/* After placing part of a set, offer the next part (as Super Mario Maker
 * does): the brush becomes it, and the status bar says so. */
static void suggest_next(uint16_t id)
{
    static const char *const colour[4] = { "red", "green", "blue", "pink" };
    uint16_t next = 0;
    std::string why;
    id = item_base(id);
    if (id >= 0x3044 && id <= 0x3047 && !level_has(id + 4)) { next = id + 4; why = std::string("The ") + colour[id - 0x3044] + " key needs its lock: place it next."; }
    else if (id >= 0x3048 && id <= 0x304B && !level_has(id + 4)) { next = id + 4; why = std::string("Now the ") + colour[id - 0x3048] + " doors the lock opens."; }
    else if (id == 0x3033 && !level_has(0x3034)) { next = 0x3034; why = "The access card needs a card reader: place it next."; }
    else if (id == 0x3034 && !level_has(0x3033)) { next = 0x3033; why = "The card reader needs an access card: place it next."; }
    else if (id == 0x3020 && !level_has(0x3035)) { next = 0x3035; why = "The robot hand needs a hand reader: place it next."; }
    else if (id == 0x3035 && !level_has(0x3036)) { next = 0x3036; why = "The hand reader extends a walkway: drag one out next."; }
    else if (id == 0x302F && !level_has(0x3030)) { next = 0x3030; why = "Now teleporter B, where A leads."; }
    else if (id == 0x3030 && !level_has(0x302F)) { next = 0x302F; why = "Now teleporter A, where B leads."; }
    if (!next) return;
    ed.brush = next;
    say(why + "  (The brush is now: " + actor_name(next) + ".)");
}

static void tool_input(int c, int r, bool inside)
{
    ImGuiIO &io = ImGui::GetIO();
    bool press = ImGui::IsMouseClicked(ImGuiMouseButton_Left), down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool release = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    int mode = io.KeyShift ? 1 : io.KeyAlt ? 2 : 0;
    bool actor_brush = cell_kind(ed.brush) == CK_ACTOR;

    if (ed.floating) {                          /* a paste waiting for its place */
        if (press && inside) {
            Edit e(lv->map, ed.locks);
            clip.paste(e, c, r);
            if (e.changed) { map_changed(e.c0, e.r0, e.c1, e.r1); commit("Paste (" + std::to_string(e.changed) + " cells)"); }
            if (!io.KeyShift) ed.floating = false;      /* Shift: keep pasting */
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ed.floating = false;
        return;
    }
    /* double-click a note monitor or Dr. Proton screen: its message.  The first
       click of the two may have painted over its picture; that is taken back. */
    static int msg_press_cur = -1;
    if (inside && press && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && msg_press_cur >= 0) {
        if (hist.cur > msg_press_cur) {             /* (the first click's stroke) */
            set_map(hist.go(msg_press_cur));
            hist.steps.resize(msg_press_cur + 1);
        }
        msg_press_cur = -1;
        note_win = true;
        return;
    }
    if (press) msg_press_cur = inside && view.actors && on_message_actor(mouse_fx, mouse_fy) ? hist.cur : -1;
    Tool t = ed.tool;
    bool paint = t == T_PENCIL || t == T_ERASER || t == T_LINE || t == T_RECT || t == T_FILL;
    if (paint && io.KeyAlt && press && inside) {
        int pc = c, pr = r;
        if (view.actors) actor_under(mouse_fx, mouse_fy, &pc, &pr);
        ed.brush = lv->at(pc, pr);
        say("Brush: " + value_text(ed.brush));
        return;
    }

    if (press && inside) {
        ed.dragging = true;
        ed.drag_c = ed.last_c = c;
        ed.drag_r = ed.last_r = r;
        stroke_begin();
        switch (t) {
        case T_PENCIL: case T_ERASER: {
            if (t == T_PENCIL && actor_brush) {
                if (span_actor(ed.brush)) break;        /* conveyor / walkway: placed on release */
                if (cell_kind(lv->at(c, r)) != CK_ACTOR) ed.under[r * MAP_W + c] = lv->at(c, r);
                Edit e(lv->map, ed.locks);              /* an actor: one per click */
                bool moved = actor_single(ed.brush) && level_has(ed.brush);
                place_actor(e, lv->after, c, r, ed.brush, ed.erase);
                stroke_add(e);
                uint16_t id = ed.brush;
                if (ed.stroke_cells) {
                    commit((moved ? "Moved " : "Placed ") + std::string(actor_name(id)));
                    suggest_next(id);
                    if (shows_message(id) && !proj.dos) note_win = true;    /* (write what it says) */
                }
                ed.stroke_cells = 0;
                ed.dragging = false;
                break;
            }
            Edit e(lv->map, ed.locks);
            e.behind = t == T_PENCIL;           /* a tile painted over an actor goes behind it */
            if (io.KeyShift && ed.paint_c >= 0)  /* Shift+click: a straight line from the last cell painted */
                op_line(e, ed.paint_c, ed.paint_r, c, r, t == T_ERASER ? ed.erase : ed.brush);
            else {
                e.set(c, r, t == T_ERASER ? ed.erase : ed.brush);
                e.finish_behind();
            }
            stroke_add(e);
            ed.paint_c = c; ed.paint_r = r;
            break;
        }
        case T_FILL: {
            Edit e(lv->map, ed.locks);
            e.behind = true;
            op_fill(e, c, r, ed.brush, io.KeyShift);
            stroke_add(e);
            stroke_end(io.KeyShift ? "Fill everywhere" : "Fill");
            ed.dragging = false;
            break;
        }
        case T_PICK: {
            int pc = c, pr = r;                     /* (on an actor's picture: that actor) */
            if (view.actors) actor_under(mouse_fx, mouse_fy, &pc, &pr);
            ed.brush = lv->at(pc, pr);
            say("Brush: " + value_text(ed.brush));
            ed.dragging = false;
            break;
        }
        case T_MAGIC:
            sel.magic(lv->map, c, r, mode);
            ed.dragging = false;
            break;
        case T_MOVE: {                          /* the selection, or what's under the mouse */
            int mc = c, mr = r;                     /* (on an actor's picture: that actor) */
            if (!sel.on[r * MAP_W + c] && view.actors) actor_under(mouse_fx, mouse_fy, &mc, &mr);
            if (!sel.on[mr * MAP_W + mc]) {
                if (cell_locked(lv->at(mc, mr), ed.locks)) { say("That cell is locked"); ed.dragging = false; break; }
                sel.clear();
                sel.on[mr * MAP_W + mc] = 1;        /* (nothing selected there: just this cell, or the actor) */
                sel.update();
            }
            lifted.copy(lv->map, sel);
            build_lifted_texture();
            ed.shifting = true;
            ed.shift_copy = io.KeyShift;
            break;
        }
        default: break;
        }
        return;
    }
    if (!ed.dragging) return;
    c = std::max(0, std::min(MAP_W - 1, c));
    r = std::max(0, std::min(MAP_H - 1, r));
    if (down && (c != ed.last_c || r != ed.last_r)) {
        if ((t == T_PENCIL && !actor_brush) || t == T_ERASER) {   /* every cell between mouse samples */
            Edit e(lv->map, ed.locks);
            e.behind = t == T_PENCIL;
            op_line(e, ed.last_c, ed.last_r, c, r, t == T_ERASER ? ed.erase : ed.brush);
            stroke_add(e);
            ed.paint_c = c; ed.paint_r = r;
        }
        ed.last_c = c;
        ed.last_r = r;
    }
    if (release) {
        ed.dragging = false;
        if (t == T_PENCIL && actor_brush && span_actor(ed.brush)) {
            Edit e(lv->map, ed.locks);
            place_span(e, ed.drag_c, c, ed.drag_r, ed.brush);
            stroke_add(e);
            if (ed.stroke_cells) commit(ed.brush == 0x3036 ? "Placed a walkway" : ed.brush == 0x3002 ? "Placed a conveyor (moving left)" : "Placed a conveyor (moving right)");
            if (ed.brush == 0x3036) say("Walkway placed, with its stop where you let go. A hand reader extends it.");
        }
        else if (t == T_PENCIL) stroke_end("Pencil");
        else if (t == T_ERASER) stroke_end("Eraser");
        else if (t == T_LINE) {
            shape_end(c, r);
            Edit e(lv->map, ed.locks);
            e.behind = true;
            op_line(e, ed.drag_c, ed.drag_r, c, r, ed.brush);
            stroke_add(e);
            stroke_end("Line");
        } else if (t == T_RECT) {
            shape_end(c, r);
            Edit e(lv->map, ed.locks);
            e.behind = true;
            op_rect(e, ed.drag_c, ed.drag_r, c, r, ed.brush, ed.rect_filled);
            stroke_add(e);
            stroke_end(ed.rect_filled ? "Filled rectangle" : "Rectangle");
        } else if (t == T_MOVE && ed.shifting) {
            ed.shifting = false;
            int dc = c - ed.drag_c, dr = r - ed.drag_r, c0, r0, c1, r1;
            if ((dc || dr) && sel.bounds(c0, r0, c1, r1))
                put_down(lifted, c0 + dc, r0 + dr, ed.shift_copy, ed.shift_copy ? "Moved a copy" : "Moved");
        } else if (t == T_SELECT) {
            sel.rect(ed.drag_c, ed.drag_r, c, r, mode);
        }
    }
}

static void drawer(float x, float y, float w, float h);
static void canvas(float x0, float y0, float w, float h)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(x0, y0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##canvas", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImVec2 p(x0, y0);
    /* the drawer: as tall as it was dragged, leaving the map at least a strip */
    float drawer_h = drawer_open && editing() ? std::max(100 * ui_scale, std::min(drawer_hu * ui_scale, h - 120 * ui_scale)) : 0;
    ImVec2 size(w, h - drawer_h);
    spot(TS_MAP, p, ImVec2(p.x + size.x, p.y + size.y));
    ImGui::SetCursorScreenPos(p);
    last_canvas = size;
    if (pending_at_c >= 0) { center_on(pending_at_c * 16 + 8, pending_at_r * 16 + 8, size); pending_at_c = -1; }
    ImGui::InvisibleButton("##map", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                              ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(theme_dark ? ImVec4(0.06f, 0.07f, 0.08f, 1) : ImVec4(0.86f, 0.87f, 0.89f, 1)));

    std::string info;
    if (editing()) {
        /* pan: middle or right drag, or Space + left drag; zoom: wheel at the cursor */
        bool space = ImGui::IsKeyDown(ImGuiKey_Space);
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2) ||
                       (space && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)))) {
            view.x -= io.MouseDelta.x / view.zoom;
            view.y -= io.MouseDelta.y / view.zoom;
        }
        if (hovered && io.MouseWheel != 0) {
            int zi = zoom_index() + (io.MouseWheel > 0 ? 1 : -1);
            if (zi >= 0 && zi < N_ZOOMS) zoom_at(ZOOMS[zi], ImVec2(io.MousePos.x - p.x, io.MousePos.y - p.y));
        }
        int n0, m0, n1, m1;
        bool keys_ok = !io.WantTextInput && !keys_capturing() && tour_step < 0 &&
                       !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (keys_ok && ed.tool == T_MOVE && !ed.dragging && sel.any && sel.bounds(n0, m0, n1, m1)) {
            /* the Move tool and a selection: the pan keys nudge it a cell (Shift: four) */
            int n = io.KeyShift ? 4 : 1, dc = 0, dr = 0;
            if (act_pressed(A_PAN_LEFT)) dc = -n;
            if (act_pressed(A_PAN_RIGHT)) dc = n;
            if (act_pressed(A_PAN_UP)) dr = -n;
            if (act_pressed(A_PAN_DOWN)) dr = n;
            dc = std::max(-n0, std::min(MAP_W - 1 - n1, dc));
            dr = std::max(-m0, std::min(MAP_H - 1 - m1, dr));
            if (dc || dr) {
                Clip cl;
                cl.copy(lv->map, sel);
                put_down(cl, n0 + dc, m0 + dr, false, "Nudged");
            }
        } else if (keys_ok) {
            double step = 64 / view.zoom * (io.KeyShift ? 4 : 1);
            if (act_pressed(A_PAN_LEFT)) view.x -= step;
            if (act_pressed(A_PAN_RIGHT)) view.x += step;
            if (act_pressed(A_PAN_UP)) view.y -= step;
            if (act_pressed(A_PAN_DOWN)) view.y += step;
        }
        if (keys_ok && !ed.floating) {
            for (int k = 0; k < 4; k++)
                if (act_pressed((Act)(A_ZOOM1 + k))) zoom_at(k + 1, ImVec2(size.x / 2, size.y / 2));
            if (act_pressed(A_ZOOM_ALL)) {               /* the whole level */
                view.zoom = fmin(size.x / LEVEL_PX_W, size.y / LEVEL_PX_H);
                view.x = -(size.x / view.zoom - LEVEL_PX_W) / 2;
                view.y = -(size.y / view.zoom - LEVEL_PX_H) / 2;
            }
        }
        clamp_view(size);

        ImVec2 a((float)(p.x - view.x * view.zoom), (float)(p.y - view.y * view.zoom));
        ImVec2 b((float)(a.x + LEVEL_PX_W * view.zoom), (float)(a.y + LEVEL_PX_H * view.zoom));
        dl->AddImage(tid(tex[view.phase]), a, b);
        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(90, 90, 110, 255));
        float cell = (float)(16 * view.zoom);
        auto cell_pos = [&](int c, int r) { return ImVec2(a.x + c * cell, a.y + r * cell); };

        int c0 = std::max(0, (int)floor(view.x / 16)), r0 = std::max(0, (int)floor(view.y / 16));
        int c1 = std::min(MAP_W, (int)ceil((view.x + size.x / view.zoom) / 16));
        int r1 = std::min(MAP_H, (int)ceil((view.y + size.y / view.zoom) / 16));
        if (view.grid && cell >= 8) {
            for (int c = c0; c <= c1; c++) dl->AddLine(cell_pos(c, r0), cell_pos(c, r1), IM_COL32(255, 255, 255, 40));
            for (int r = r0; r <= r1; r++) dl->AddLine(cell_pos(c0, r), cell_pos(c1, r), IM_COL32(255, 255, 255, 40));
        }
        if (view.actors && view.labels && cell >= 16)            /* the walkway: where it will extend, once a hand reader runs it */
            for (int r = r0; r < r1; r++)
                for (int c = 0; c < c1; c++) {                 /* (from the row's start: its origin may be off to the left) */
                    if (lv->at(c, r) != 0x3036) continue;
                    int e = c + 1;
                    while (e < MAP_W && lv->at(e, r) != 0x2FE0) e++;
                    if (e >= MAP_W || e <= c + 1 || e <= c0) continue;
                    ImVec2 ta, tb;
                    tile_uv(0x2FE0, ta, tb);
                    for (int k = std::max(c + 1, c0); k < std::min(e, c1); k++) {
                        ImVec2 q = cell_pos(k, r);
                        dl->AddImage(tid(atlas), q, ImVec2(q.x + cell, q.y + cell), ta, tb, IM_COL32(255, 255, 255, 70));
                    }
                    dl->AddRect(cell_pos(c + 1, r), cell_pos(e, r + 1), IM_COL32(255, 230, 120, 160), 0, 0, 1.0f);
                }
        if (view.actors && view.labels && cell >= 16)            /* markers the game shows only as tiles */
            for (int r = r0; r < r1; r++)
                for (int c = c0; c < c1; c++) {
                    uint16_t v = lv->at(c, r);
                    if (contents_badge(dl, cell_pos(c, r), cell, v)) continue;
                    const char *t = marker_label(v);
                    if (!t) continue;
                    ImVec2 q = cell_pos(c, r), ts = ImGui::CalcTextSize(t);
                    q.x += 2; q.y += 2;
                    dl->AddRectFilled(ImVec2(q.x - 1, q.y - 1), ImVec2(q.x + ts.x + 3, q.y + ts.y + 1), IM_COL32(0, 0, 0, 170), 3);
                    dl->AddText(ImVec2(q.x + 1, q.y), IM_COL32(255, 230, 120, 255), t);
                }

        /* the selection: tinted, with its outline */
        if (sel.any)
            for (int r = r0; r < r1; r++)
                for (int c = c0; c < c1; c++) {
                    if (!sel.on[r * MAP_W + c]) continue;
                    ImVec2 q = cell_pos(c, r), e(q.x + cell, q.y + cell);
                    dl->AddRectFilled(q, e, IM_COL32(80, 160, 255, 60));
                    ImU32 col = IM_COL32(120, 200, 255, 255);
                    if (c == 0 || !sel.on[r * MAP_W + c - 1]) dl->AddLine(q, ImVec2(q.x, e.y), col, 1.5f);
                    if (c == MAP_W - 1 || !sel.on[r * MAP_W + c + 1]) dl->AddLine(ImVec2(e.x, q.y), e, col, 1.5f);
                    if (r == 0 || !sel.on[(r - 1) * MAP_W + c]) dl->AddLine(q, ImVec2(e.x, q.y), col, 1.5f);
                    if (r == MAP_H - 1 || !sel.on[(r + 1) * MAP_W + c]) dl->AddLine(ImVec2(q.x, e.y), e, col, 1.5f);
                }

        if (show_reach && reach_current()) {    /* where Duke can get to; where he gets stuck; a way to the end */
            for (int r = r0; r < r1; r++)
                for (int c = c0; c < c1; c++) {
                    int k = r * MAP_W + c;
                    uint8_t f = reach.cell[k];
                    ImVec2 q = cell_pos(c, r), e(q.x + cell, q.y + cell);
                    if (f & ED_REACH_TRAPPED) {     /* stuck: the "bad" colour, hatched (not colour alone) */
                        dl->AddRectFilled(q, e, ImGui::GetColorU32(ImVec4(T.bad.x, T.bad.y, T.bad.z, 0.38f)));
                        ImU32 hc = ImGui::GetColorU32(ImVec4(T.bad.x, T.bad.y, T.bad.z, 0.9f));
                        dl->AddLine(ImVec2(q.x, e.y), ImVec2(e.x, q.y), hc, 1.0f);
                        dl->AddLine(ImVec2(q.x, (q.y + e.y) / 2), ImVec2((q.x + e.x) / 2, q.y), hc, 1.0f);
                        dl->AddLine(ImVec2((q.x + e.x) / 2, e.y), ImVec2(e.x, (q.y + e.y) / 2), hc, 1.0f);
                    }
                    else if (f & ED_REACH_BODY) dl->AddRectFilled(q, e, ImGui::GetColorU32(ImVec4(T.good.x, T.good.y, T.good.z, 0.25f)));
                    else if (cell_kind(lv->after[k]) != CK_SOLID) dl->AddRectFilled(q, e, IM_COL32(0, 0, 0, 130));
                    if (f & ED_REACH_SHOT) dl->AddRect(ImVec2(q.x + 1, q.y + 1), ImVec2(e.x - 1, e.y - 1), IM_COL32(255, 200, 60, 200), 0, 0, 1.0f);
                }
            if (reach.route_n > 1) {
                ImDrawListFlags keep = dl->Flags;
                dl->Flags &= ~ImDrawListFlags_AntiAliasedLines;
                ImVec2 prev;
                for (int i = 0; i < reach.route_n; i++) {
                    ImVec2 pt(a.x + reach.route[i * 2] * (float)view.zoom, a.y + (reach.route[i * 2 + 1] - 8) * (float)view.zoom);
                    /* (a teleport is a jump: no line across the map) */
                    if (i && fabsf(pt.x - prev.x) < 48 * view.zoom && fabsf(pt.y - prev.y) < 48 * view.zoom)
                        dl->AddLine(prev, pt, IM_COL32(90, 220, 255, 230), 2.0f);
                    prev = pt;
                }
                dl->Flags = keep;
            }
        }

        if ((ed.tool == T_MOVE || ed.tool == T_PICK) && hovered && view.actors) {   /* the actor a click would take */
            int hc, hr;
            double x0, y0, x1, y1;
            if (actor_under(mouse_fx, mouse_fy, &hc, &hr) && !(ed.tool == T_MOVE && sel.on[hr * MAP_W + hc]) && !ed.dragging &&
                actor_box(hc, hr, &x0, &y0, &x1, &y1)) {
                dl->AddRect(ImVec2(a.x + (float)(x0 * view.zoom) - 1, a.y + (float)(y0 * view.zoom) - 1),
                            ImVec2(a.x + (float)(x1 * view.zoom) + 1, a.y + (float)(y1 * view.zoom) + 1),
                            ImGui::GetColorU32(T.accent), 2 * ui_scale, 0, 1.5f * ui_scale);
            }
        }

        if (trail.size() > 1 && view.trail) {   /* Duke's path in the last play-test */
            std::vector<ImVec2> pts;
            pts.reserve(trail.size());
            for (const ImVec2 &t : trail) pts.push_back(ImVec2(a.x + t.x * (float)view.zoom, a.y + t.y * (float)view.zoom));
            ImDrawListFlags keep = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedLines;   /* crisp, like the pixels under it */
            for (size_t i = 1; i < pts.size(); i++) dl->AddLine(pts[i - 1], pts[i], IM_COL32(255, 120, 40, 255), 2.0f);
            dl->Flags = keep;
            dl->AddCircleFilled(pts.back(), 4, IM_COL32(255, 120, 40, 255));
        }

        /* the cell under the cursor, and the tool */
        double fx = view.x + (io.MousePos.x - p.x) / view.zoom, fy = view.y + (io.MousePos.y - p.y) / view.zoom;
        int mc = (int)floor(fx / 16), mr = (int)floor(fy / 16);
        mouse_fx = fx;
        mouse_fy = fy;
        bool inside = hovered && fx >= 0 && fy >= 0 && mc < MAP_W && mr < MAP_H;
        hover_c = inside ? mc : -1;
        hover_r = inside ? mr : -1;
        if (!space && (hovered || ed.dragging)) tool_input(mc, mr, inside);

        static std::vector<int> shape;
        shape_cells(shape);
        for (int i : shape) {                   /* line / rectangle preview */
            ImVec2 q = cell_pos(i % MAP_W, i / MAP_W);
            ImVec2 ta, tb;
            tile_uv((uint16_t)(cell_kind(ed.brush) == CK_ANIMATED ? ed.brush + view.phase * 0x20 : ed.brush), ta, tb);
            if (cell_kind(ed.brush) <= CK_BACKDROP_B || cell_kind(ed.brush) >= CK_ACTOR)
                dl->AddRectFilled(q, ImVec2(q.x + cell, q.y + cell), IM_COL32(255, 220, 0, 90));
            else dl->AddImage(tid(atlas), q, ImVec2(q.x + cell, q.y + cell), ta, tb, IM_COL32(255, 255, 255, 200));
        }
        if (ed.tool == T_SELECT && ed.dragging) {
            ImVec2 q = cell_pos(std::min(ed.drag_c, ed.last_c), std::min(ed.drag_r, ed.last_r));
            ImVec2 e = cell_pos(std::max(ed.drag_c, ed.last_c) + 1, std::max(ed.drag_r, ed.last_r) + 1);
            dl->AddRectFilled(q, e, IM_COL32(80, 160, 255, 50));
            dl->AddRect(q, e, IM_COL32(120, 200, 255, 255), 0, 0, 1.5f);
        }
        if (ed.shifting && lifted_tex && ed.dragging) {   /* Move: where the cells came from, and where they'd go */
            int c0, r0, c1, r1;
            if (sel.bounds(c0, r0, c1, r1)) {
                int mc2 = std::max(0, std::min(MAP_W - 1, mc)), mr2 = std::max(0, std::min(MAP_H - 1, mr));
                if (!ed.shift_copy)
                    for (int i = 0; i < ED_MAP_CELLS; i++)
                        if (sel.on[i]) { ImVec2 q = cell_pos(i % MAP_W, i / MAP_W); dl->AddRectFilled(q, ImVec2(q.x + cell, q.y + cell), IM_COL32(0, 0, 0, 140)); }
                ImVec2 q = cell_pos(c0 + mc2 - ed.drag_c, r0 + mr2 - ed.drag_r);
                dl->AddImage(tid(lifted_tex), q, ImVec2(q.x + lifted.w * cell, q.y + lifted.h * cell), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 220));
                dl->AddRect(q, ImVec2(q.x + lifted.w * cell, q.y + lifted.h * cell), IM_COL32(255, 220, 0, 255), 0, 0, 1.5f);
            }
        }
        if (inside) {
            if (ed.floating && clip_tex) {      /* the paste, where it would land */
                ImVec2 q = cell_pos(mc, mr);
                dl->AddImage(tid(clip_tex), q, ImVec2(q.x + clip.w * cell, q.y + clip.h * cell), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 190));
                dl->AddRect(q, ImVec2(q.x + clip.w * cell, q.y + clip.h * cell), IM_COL32(255, 220, 0, 255), 0, 0, 1.5f);
            } else {
                ImVec2 q = cell_pos(mc, mr);
                uint16_t ghost = (ed.tool == T_PENCIL && cell_kind(ed.brush) == CK_ACTOR) ? ed.brush : 0;
                if (ghost && span_actor(ghost) && ed.dragging) {   /* a conveyor / walkway being dragged */
                    int a0 = std::min(ed.drag_c, mc), a1 = std::max(ed.drag_c, mc);
                    if (a1 == a0) a1 = a0 + 1;
                    ImVec2 s0 = cell_pos(a0, ed.drag_r), s1 = cell_pos(a1 + 1, ed.drag_r + 1);
                    dl->AddRectFilled(s0, s1, IM_COL32(255, 220, 0, 60));
                    dl->AddRect(s0, s1, IM_COL32(255, 220, 0, 255), 0, 0, 1.5f);
                    const char *l0 = ghost == 0x3036 ? "WALK" : ghost == 0x3002 ? "<<" : ">>";
                    const char *l1 = ghost == 0x3036 ? "STOP" : ghost == 0x3002 ? "<|" : "|>";
                    dl->AddText(ImVec2(s0.x + 3, s0.y + 2), IM_COL32(255, 230, 120, 255), l0);
                    dl->AddText(ImVec2(cell_pos(a1, ed.drag_r).x + 3, s0.y + 2), IM_COL32(255, 230, 120, 255), l1);
                } else if (ghost) {                     /* the actor as it will look there */
                    int i = ghost - 0x3000;
                    if (actor_tex[i]) {
                        ImVec2 g(q.x + actor_ox[i] * (float)view.zoom, q.y + actor_oy[i] * (float)view.zoom);
                        dl->AddImage(tid(actor_tex[i]), g, ImVec2(g.x + actor_w[i] * (float)view.zoom, g.y + actor_h[i] * (float)view.zoom),
                                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 170));
                    } else {
                        const char *l = marker_label(ghost);
                        dl->AddText(ImVec2(q.x + 3, q.y + 2), IM_COL32(255, 230, 120, 255), l ? l : "?");
                    }
                }
                if ((ed.tool == T_PENCIL || ed.tool == T_LINE || ed.tool == T_RECT) && !ed.dragging &&
                    cell_kind(ed.brush) > CK_BACKDROP_B && cell_kind(ed.brush) < CK_ACTOR) {
                    ImVec2 ta, tb;
                    tile_uv((uint16_t)(cell_kind(ed.brush) == CK_ANIMATED ? ed.brush + view.phase * 0x20 : ed.brush), ta, tb);
                    dl->AddImage(tid(atlas), q, ImVec2(q.x + cell, q.y + cell), ta, tb, IM_COL32(255, 255, 255, 170));
                }
                bool locked = cell_locked(lv->at(mc, mr), ed.locks);
                dl->AddRect(q, ImVec2(q.x + cell, q.y + cell), locked ? IM_COL32(255, 80, 60, 230) : IM_COL32(255, 255, 0, 220), 0, 0, 1.5f);
            }
            info = describe(mc, mr);
            if (cell_locked(lv->at(mc, mr), ed.locks)) info += "    (protected)";
            if (view.actors && on_message_actor(fx, fy)) info += "    Double-click: the level's message";
        }
        if (cur >= 0 && cur < (int)checks.size()) {    /* check results: a corner flag on the cell */
            for (const Check &k : checks[cur]) {
                if (k.c < 0 || (k.sev == CS_NOTE && !show_notes)) continue;
                ImVec2 q = cell_pos(k.c, k.r);
                /* the colour says how bad; so does the shape: broken flags two corners, a warning one, a note a dot */
                ImU32 col = ImGui::GetColorU32(k.sev == CS_BROKEN ? T.bad : k.sev == CS_WARNING ? T.warn : T.info);
                float t = std::max(6.0f, cell * 0.45f);
                ImVec2 e(q.x + cell, q.y + cell);
                if (k.sev == CS_NOTE) dl->AddCircleFilled(ImVec2(q.x + t * 0.4f, q.y + t * 0.4f), t * 0.28f, col);
                else dl->AddTriangleFilled(q, ImVec2(q.x + t, q.y), ImVec2(q.x, q.y + t), col);
                if (k.sev == CS_BROKEN) dl->AddTriangleFilled(e, ImVec2(e.x - t, e.y), ImVec2(e.x, e.y - t), col);
                dl->AddRect(q, e, (col & 0x00FFFFFF) | 0x90000000, 0, 0, k.sev == CS_BROKEN ? 2.0f : 1.0f);
            }
            if (focus_c >= 0 && now_s < focus_until) {  /* the one clicked in the panel */
                ImVec2 q = cell_pos(focus_c, focus_r);
                float pulse = 2.0f + 2.0f * (float)fabs(sin(now_s * 6));
                dl->AddRect(ImVec2(q.x - pulse, q.y - pulse), ImVec2(q.x + cell + pulse, q.y + cell + pulse), IM_COL32(255, 255, 255, 240), 0, 0, 2.0f);
            }
        }
        dl->PopClipRect();

        status_info = info;
        if (drawer_h > 0) drawer(p.x, p.y + size.y, size.x, drawer_h);
    } else {
        dl->PopClipRect();
        status_info.clear();
    }
    ImGui::End();
}

/* a panel edge to drag: dir +1 when dragging right widens it (the sidebar),
   -1 for the palette.  Double-click goes back to the usual width. */
static void splitter(const char *id, float x, float y, float h, float *width, int dir, float usual)
{
    float grab = 6 * ui_scale;
    ImGui::SetNextWindowPos(ImVec2(x - grab / 2, y));
    ImGui::SetNextWindowSize(ImVec2(grab, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::Begin(id, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(2);
    ImGui::InvisibleButton("##grip", ImVec2(grab, h));
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y), ImVec2(x, y + h), ImGui::GetColorU32(ImVec4(T.accent.x, T.accent.y, T.accent.z, ImGui::IsItemActive() ? 1.0f : 0.6f)), 2 * ui_scale);
    }
    if (ImGui::IsItemActive()) {                /* the edge follows the mouse, within what the layout allows */
        float W = ImGui::GetIO().DisplaySize.x, m = ImGui::GetIO().MousePos.x;
        float want = (dir > 0 ? m : W - m) / ui_scale;
        *width = std::max(dir > 0 ? 200.0f : 240.0f, std::min(want, W / ui_scale * (dir > 0 ? 0.4f : 0.45f)));
    }
    if (ImGui::IsItemDeactivated()) settings_save();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { *width = usual; settings_save(); }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
        ImGui::SetTooltip("Drag to resize; double-click for the usual width");
    ImGui::End();
}

/* ---- the first-run tour: read and click Next */
struct TourCard { int spot; const char *title; std::string (*text)(void); };
static std::string tk(Act a) { std::string k = act_keys(a); return k.empty() ? "(no key)" : k; }
static const TourCard tour_cards[] = {
    { -1, "Welcome to Shrapnel", [] { return std::string(
        "A quick look around, in ten short steps. Next (or the right arrow) moves on, Back goes back, "
        "Skip (or Esc) ends it. Help > Take the tour shows it again any time."); } },
    { TS_MAP, "The map", [] { return std::string(
        "The level, drawn by the game itself. The wheel zooms (" + tk(A_ZOOM1) + "-" + tk(A_ZOOM4) + " and " + tk(A_ZOOM_ALL) +
        " too); drag with the right or middle button, or hold Space, to move around. The overview at the bottom left "
        "jumps anywhere. Tab hides the side panels for a bigger map."); } },
    { TS_PALETTE, "Tiles, actors and prefabs", [] { return std::string(
        "Everything you can paint with. Click a tile or actor and it becomes your brush; search finds anything by name. "
        "Prefabs are pieces of level saved to place again - select part of a level and save it."); } },
    { TS_TOOLS, "The tools", [] { return std::string(
        "Select, Move, Magic select, Pencil, Eraser, Line, Rectangle, Fill and Pick, then Undo and Redo. "
        "Alt-click picks up whatever is under the mouse as your brush; Shift-click with the Pencil draws a straight line. "
        "Hover a tool for its key."); } },
    { TS_PROTECT, "Protect", [] { return std::string(
        "The padlocks keep walls, background, backdrop or actors safe from painting, filling, pasting and moving - "
        "say, to repaint the open space without touching the walls."); } },
    { TS_LEVELS, "Levels and backdrops", [] { return std::string(
        "The project's levels (drag one to change the order; a mark shows any problems), then the open level's two "
        "backdrops: its open space shows A, except where you paint B. Its message, if it has a Dr. Proton screen, is here too."); } },
    { TS_PLAY, "Play it", [] { return std::string(
        "Play here (" + tk(A_PLAY_HERE) + ") starts Duke at the middle of the view, or at the mouse, so you can test any part "
        "right away; F4 comes back. From start (" + tk(A_PLAY_START) + ") plays the whole level, and reaching the exit "
        "records a clear. The arrow beside Play here sets what Duke starts with."); } },
    { TS_CHECK, "Checks", [] { return std::string(
        "Shrapnel checks what the game needs - a start, an exit, a key for every lock, the actor limits - and shows the "
        "result here. Click it for the list; Check (" + tk(A_CHECK) + ") runs them now."); } },
    { TS_REACH, "Reach", [] { return std::string(
        "Shows where Duke can get to, where he would get stuck (hatched), and one way to the exit."); } },
    { TS_FILE, "Save and share", [] { return std::string(
        "Save (" + tk(A_SAVE) + ") makes a .LEV (a level) or an .EPI (an episode). Put it in Techbot's LEVELS folder and play it "
        "from U)ser levels. A DOS episode saves as map files for the original game. That's the tour - have fun!"); } },
};
static const int n_tour = (int)(sizeof tour_cards / sizeof tour_cards[0]);

static void tour_start(void) { tour_step = 0; panels_hidden = false; }

static void tour(void)
{
    if (tour_step < 0 || !editing()) return;
    if (tour_step >= n_tour) { tour_step = -1; return; }
    const TourCard &cd = tour_cards[tour_step];
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImVec2 V0 = vp->Pos, V1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    bool has = cd.spot >= 0 && spot1[cd.spot].x > spot0[cd.spot].x;
    float pad = 6 * ui_scale;
    ImVec2 a = has ? ImVec2(spot0[cd.spot].x - pad, spot0[cd.spot].y - pad) : ImVec2(0, 0);
    ImVec2 b = has ? ImVec2(spot1[cd.spot].x + pad, spot1[cd.spot].y + pad) : ImVec2(0, 0);

    /* a window over everything (clicks don't reach the editor), dark but for the part explained */
    ImGui::SetNextWindowPos(V0);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##tourshade", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 dim = IM_COL32(0, 0, 0, theme_dark ? 165 : 120);
    if (has) {
        dl->AddRectFilled(V0, ImVec2(V1.x, a.y), dim);
        dl->AddRectFilled(ImVec2(V0.x, b.y), V1, dim);
        dl->AddRectFilled(ImVec2(V0.x, a.y), ImVec2(a.x, b.y), dim);
        dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(V1.x, b.y), dim);
        dl->AddRect(a, b, ImGui::GetColorU32(T.accent), 6 * ui_scale, 0, 2.5f * ui_scale);
    } else dl->AddRectFilled(V0, V1, dim);
    ImGui::End();

    /* the card: beside the part explained, where there's room */
    float cw = 380 * ui_scale, gap = 14 * ui_scale;
    ImVec2 pos, piv(0, 0);
    if (!has) { pos = ImVec2((V0.x + V1.x) / 2, (V0.y + V1.y) / 2); piv = ImVec2(0.5f, 0.5f); }
    else if (V1.x - b.x > cw + gap) pos = ImVec2(b.x + gap, std::max(V0.y + gap, a.y));
    else if (a.x - V0.x > cw + gap) pos = ImVec2(a.x - gap, std::max(V0.y + gap, a.y)), piv = ImVec2(1, 0);
    else if (V1.y - b.y > 200 * ui_scale) pos = ImVec2(std::min(std::max(V0.x + gap, a.x), V1.x - cw - gap), b.y + gap);
    else if (a.y - V0.y > 200 * ui_scale) pos = ImVec2(std::min(std::max(V0.x + gap, a.x), V1.x - cw - gap), a.y - gap), piv = ImVec2(0, 1);
    else pos = ImVec2((a.x + b.x) / 2, b.y - 3 * gap), piv = ImVec2(0.5f, 1);   /* (no room around it - the map: inside, at the bottom) */
    if (has && piv.y == 0 && b.y - a.y > (V1.y - V0.y) * 0.6f) pos.y = (a.y + b.y) / 2 - 90 * ui_scale;   /* (tall, beside it: about the middle) */
    else if (has && piv.y == 0 && a.y > (V0.y + V1.y) / 2) pos.y = std::min(b.y, V1.y - gap), piv.y = 1;   /* (low down: lined up with its bottom) */
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, piv);
    ImGui::SetNextWindowSize(ImVec2(cw, 0));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * ui_scale, 14 * ui_scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::PushStyleColor(ImGuiCol_Border, T.accent);
    ImGui::Begin("##tourcard", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
    ImGui::Text("%d of %d", tour_step + 1, n_tour);
    ImGui::PopStyleColor();
    ImGui::PushFont(font_bold);
    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextUnformatted(cd.title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw - 32 * ui_scale);
    ImGui::TextUnformatted(cd.text().c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Spacing();
    bool last = tour_step == n_tour - 1;
    if (ImGui::Button("Skip", ImVec2(80 * ui_scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) tour_step = -1;
    float bw = 90 * ui_scale;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw * 2 - 6 * ui_scale);
    ImGui::BeginDisabled(tour_step <= 0);
    if (ImGui::Button("Back", ImVec2(bw, 0)) || (tour_step > 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))) tour_step--;
    ImGui::EndDisabled();
    ImGui::SameLine();
    {   /* (like Play here: the accent, white words) */
        ImVec4 f = T.accent, k = theme_dark ? ImVec4(0, 0, 0, 1) : f;
        if (theme_dark) f = ImVec4(f.x * 0.65f + k.x * 0.35f, f.y * 0.65f + k.y * 0.35f, f.z * 0.65f + k.z * 0.35f, 1);
        ImGui::PushStyleColor(ImGuiCol_Button, f);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(f.x * 0.88f + 0.12f, f.y * 0.88f + 0.12f, f.z * 0.88f + 0.12f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    if (tour_step >= 0 && (ImGui::Button(last ? "Done" : "Next", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
                           ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
        tour_step = last ? -1 : tour_step + 1;
    ImGui::PopStyleColor(3);
    ImGui::End();
}

/* the guide (BUILDING.md), if it came with Shrapnel: beside it, or a folder up */
static std::string guide_path(void)
{
    char *b = SDL_GetBasePath();
    std::string base = b ? b : "";
    SDL_free(b);
    for (const char *rel : { "BUILDING.md", "../BUILDING.md", "../../BUILDING.md" })
        if (path_exists(base + rel)) return base + rel;
    return "";
}

/* the drawer under the map: Checks or History */
static void drawer(float x, float y, float w, float h)
{
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * ui_scale, 8 * ui_scale));
    float map_top = ImGui::GetWindowPos().y;          /* (the canvas: the drawer leaves the map a strip) */
    ImGui::BeginChild("##drawer", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    {   /* its top edge: drag to make it taller or shorter, double-click for the usual height */
        ImVec2 keep = ImGui::GetCursorScreenPos();
        float grab = 6 * ui_scale, bottom = y + h;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::InvisibleButton("##drawer_edge", ImVec2(w, grab));
        bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            drawer_hu = std::max(100.0f, (bottom - std::max(ImGui::GetIO().MousePos.y, map_top + 120 * ui_scale)) / ui_scale);
        if (ImGui::IsItemDeactivated()) settings_save();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { drawer_hu = DRAWER_H_DEFAULT; settings_save(); }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
            ImGui::SetTooltip("Drag to resize; double-click for the usual height");
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), hot ? ImGui::GetColorU32(ImVec4(T.accent.x, T.accent.y, T.accent.z, ImGui::IsItemActive() ? 1.0f : 0.6f))
                                                        : ImGui::GetColorU32(T.border), hot ? 2 * ui_scale : 1.0f);
        ImGui::SetCursorScreenPos(keep);
    }
    {   /* tabs, the run mode, close */
        int nb = cur < (int)checks.size() ? check_count(checks[cur], CS_BROKEN) : 0;
        int nw = cur < (int)checks.size() ? check_count(checks[cur], CS_WARNING) : 0;
        char t[48];
        snprintf(t, sizeof t, "Checks  %d##tab", nb + nw);
        float y0 = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y0 + 3 * ui_scale);
        if (chip(t, drawer_tab == 0)) drawer_tab = 0;
        ImGui::SameLine(0, 4 * ui_scale);
        if (chip("History", drawer_tab == 1)) drawer_tab = 1;
        if (drawer_tab == 0) {
            ImGui::SameLine(0, 18 * ui_scale);
            ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
            ImGui::TextUnformatted("Run:");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6 * ui_scale);
            if (chip("As you edit", !checks_manual) && checks_manual) set_checks_manual(false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Check after every change (can slow big levels down)");
            ImGui::SameLine(0, 4 * ui_scale);
            if (chip("When asked", checks_manual) && !checks_manual) set_checks_manual(true);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Check only with Check (%s), before saving and before playing", act_keys(A_CHECK).c_str());
        }
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
        ImGui::SetCursorPosY(y0);
        if (icon_button(ICON_CLOSE, "Close", false)) drawer_open = false;
    }
    ImGui::Spacing();
    ImGui::BeginChild("##drawerbody", ImVec2(0, 0), 0);
    if (drawer_tab == 0) checks_body(); else history_body();
    ImGui::EndChild();
    ImGui::EndChild();
}

/* the status bar: the message or what is under the mouse; zoom; the checks */
static void status_bar(float y0, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(0, y0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, T.bar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * ui_scale, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##status", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(0, y0), ImVec2(w, y0), ImGui::GetColorU32(T.border));
    float ty = (h - ImGui::GetTextLineHeight()) / 2, fy = (h - ImGui::GetFrameHeight()) / 2;
    double now_s = clock_s();
    float right_w = 0;
    if (editing()) {
        /* right: zoom - 100% +, then the checks badge */
        int nb = cur < (int)checks.size() ? check_count(checks[cur], CS_BROKEN) : 0;
        int nw = cur < (int)checks.size() ? check_count(checks[cur], CS_WARNING) : 0;
        char badge[64];
        if (nb) snprintf(badge, sizeof badge, ICON_ERROR_FILLED " %d broken%s", nb, nw ? "" : "");
        else if (nw) snprintf(badge, sizeof badge, ICON_WARNING_FILLED " %d warning%s", nw, nw == 1 ? "" : "s");
        else snprintf(badge, sizeof badge, ICON_TASK_ALT " No problems");
        if (!checks_ever) snprintf(badge, sizeof badge, ICON_TASK_ALT " Not checked yet");
        else if (checks_stale()) strncat(badge, "  (out of date)", sizeof badge - strlen(badge) - 1);
        char zl[16];
        snprintf(zl, sizeof zl, "%d%%", (int)(view.zoom * 100 + 0.5));
        float bw = ImGui::CalcTextSize(badge).x + ImGui::GetStyle().FramePadding.x * 2;
        float zw = ImGui::GetFrameHeight() * 2 + ImGui::CalcTextSize("1000%").x + 8 * ui_scale;
        std::string chk_s = ICON_TASK_ALT " Check" + (act_keys(A_CHECK).empty() ? std::string() : " (" + act_keys(A_CHECK) + ")");
        const char *chk = chk_s.c_str();
        float cw0 = ImGui::CalcTextSize(chk).x + ImGui::GetStyle().FramePadding.x * 2;
        const char *path_lbl = ICON_VISIBILITY " Play-test path";
        float pw = trail.size() > 1 ? ImGui::CalcTextSize(path_lbl).x + ImGui::GetStyle().FramePadding.x * 2 + 14 * ui_scale : 0;
        right_w = bw + zw + cw0 + pw + 32 * ui_scale;
        float x = w - 10 * ui_scale - right_w;
        ImGui::SetCursorPos(ImVec2(x, fy));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * ui_scale, 0));
        if (pw > 0) {                           /* Duke's path from the last play-test: show or hide it */
            ImGui::PushStyleColor(ImGuiCol_Text, view.trail ? IM_COL32(255, 140, 60, 255) : ImGui::GetColorU32(T.muted));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(path_lbl)) view.trail = !view.trail;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(view.trail ? "Hide Duke's path from the last play-test (View > Play-test path)" : "Show Duke's path from the last play-test");
            ImGui::SameLine(0, 14 * ui_scale);
            ImGui::SetCursorPosY(fy);
        }
        int zi = zoom_index();
        if (icon_button(ICON_ZOOM_OUT, "Zoom out (wheel)", false, zi > 0)) zoom_at(ZOOMS[zi - 1], ImVec2(last_canvas.x / 2, last_canvas.y / 2));
        ImGui::SameLine();
        ImGui::SetCursorPosY(ty);
        float tw = ImGui::CalcTextSize(zl).x, cw = ImGui::CalcTextSize("1000%").x + 8 * ui_scale;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - tw) / 2);
        ImGui::TextUnformatted(zl);
        if (ImGui::IsItemClicked()) zoom_at(1.0, ImVec2(last_canvas.x / 2, last_canvas.y / 2));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Click for 100%% (1-4: zoom, 0: the whole level)");
        ImGui::SameLine(0, (cw - tw) / 2 + 2 * ui_scale);
        ImGui::SetCursorPosY(fy);
        if (icon_button(ICON_ZOOM_IN, "Zoom in (wheel)", false, zi + 1 < N_ZOOMS)) zoom_at(ZOOMS[zi + 1], ImVec2(last_canvas.x / 2, last_canvas.y / 2));
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 14 * ui_scale);
        ImGui::SetCursorPosY(fy);
        if (ImGui::Button(chk)) run_check();                /* run the checks, and their result beside it */
        spot(TS_CHECK, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Run the checks now: the rules, whether the level can be finished, and the recorded clear.");
        ImGui::SameLine(0, 4 * ui_scale);
        ImGui::SetCursorPosY(fy);
        ImVec4 col = !checks_ever ? T.muted : nb ? T.bad : nw ? T.warn : T.good;
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::PushStyleColor(ImGuiCol_Button, drawer_open ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered) : ImVec4(0, 0, 0, 0));
        if (ImGui::Button(badge)) { if (drawer_open && drawer_tab == 0) drawer_open = false; else { drawer_open = true; drawer_tab = 0; } }
        spot_join(TS_CHECK, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Show or hide the checks");
    }
    /* left: a message for a while, else what the mouse is over, else how the tool works */
    ImGui::SetCursorPos(ImVec2(10 * ui_scale, ty));
    ImGui::PushClipRect(ImVec2(0, y0), ImVec2(w - right_w - 20 * ui_scale, y0 + h), true);
    if (now_s < message_until) ImGui::TextColored(T.warn, "%s", message.c_str());
    else if (!editing()) ;
    else if (!status_info.empty()) ImGui::TextUnformatted(status_info.c_str());
    else ImGui::TextColored(T.muted, "%s  -  wheel: zoom, right/middle drag or Space: pan, 1-4: zoom, 0: whole level", tool_info[ed.tool].help);
    ImGui::PopClipRect();
    ImGui::End();
}

/* ================================================================ the start screen */
static bool try_folder(const char *dir)        /* a game folder: some DN1/2/3.EXE in it */
{
    static std::string keep;
    keep = dir;
    shrapnel_game_dir = keep.c_str();
    for (int e = 1; e <= 3; e++)
        if (ep.load(e)) { build_episode_textures(); bad_folder = false; if (last_dir.empty()) last_dir = keep; return true; }
    ep.number = 0;
    bad_folder = true;
    return false;
}

static int episode_choice(int *e)               /* radio buttons for the episodes in the folder */
{
    if (!ep.available[*e]) for (int k = 1; k <= 3; k++) if (ep.available[k]) { *e = k; break; }
    for (int k = 1; k <= 3; k++) {
        char n[16];
        snprintf(n, sizeof n, "Episode %d", k);
        ImGui::BeginDisabled(!ep.available[k]);
        ImGui::RadioButton(n, e, k);
        ImGui::EndDisabled();
        if (k < 3) ImGui::SameLine();
    }
    return *e;
}

static bool popup_begin(const char *name)
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    return ImGui::BeginPopupModal(name, NULL, ImGuiWindowFlags_AlwaysAutoResize);
}

static bool popup_cancel(void)
{
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); return true; }
    return false;
}

/* the example level (PaulloDEC's), opened as a new project: saving asks where */
#include "../resources/example_lev.h"
static void open_example(void)
{
    std::string f = pref_dir() + "example.lev", err;
    FILE *o = fopen(f.c_str(), "wb");
    if (!o) { say("Could not write " + f); return; }
    fwrite(example_lev, 1, sizeof example_lev, o);
    fclose(o);
    Project p;
    if (!project_load_zip(p, f, err)) { say(err); return; }
    p.fresh = true;                             /* (a copy of its own: Save asks where) */
    if (use_project(p)) say("The example level: play it (Play here or From start), take it apart, save your own copy.");
}

/* the logo, drawn at a whole number of screen pixels per game pixel */
#include "../resources/logo_png.h"
extern "C" unsigned char *stbi_load_from_memory(const unsigned char *, int, int *, int *, int *, int);
extern "C" void stbi_image_free(void *);
static SDL_Texture *logo_tex;
static int logo_w, logo_h;
/* the logo as wide as `w` (the page's content): drawn from a copy enlarged
   a whole number of times with no smoothing (crisp pixels) to at least that
   width, then shrunk to it smoothly - so any width looks sharp, not just whole
   multiples.  Never smaller than one screen pixel a logo pixel: if `w` is
   narrower than that, it is drawn at that and may overhang. */
static std::vector<uint32_t> logo_px;           /* the logo as it is */
static SDL_Texture *logo_big;                   /* ... enlarged logo_k times */
static int logo_k;
static void draw_logo(float w)
{
    if (!logo_tex && logo_w >= 0) {
        int n;
        unsigned char *px = stbi_load_from_memory(logo_png, (int)sizeof logo_png, &logo_w, &logo_h, &n, 4);
        if (!px) { logo_w = -1; return; }
        logo_px.resize((size_t)logo_w * logo_h);
        for (size_t i = 0; i < logo_px.size(); i++)
            logo_px[i] = (uint32_t)px[i * 4 + 3] << 24 | (uint32_t)px[i * 4] << 16 | (uint32_t)px[i * 4 + 1] << 8 | px[i * 4 + 2];
        stbi_image_free(px);
        make_texture(&logo_tex, logo_w, logo_h);
        SDL_UpdateTexture(logo_tex, NULL, logo_px.data(), logo_w * 4);
    }
    if (!logo_tex) return;
    float fbs = ImGui::GetIO().DisplayFramebufferScale.x;
    float wp = floorf(w * fbs);                 /* (screen pixels) */
    SDL_Texture *t = logo_tex;
    if (wp <= logo_w) wp = (float)logo_w;       /* (1x: as it is) */
    else {
        int k = std::min((int)ceilf(wp / logo_w), 8192 / std::max(logo_w, logo_h));
        if (k != logo_k || !logo_big) {
            if (logo_big) SDL_DestroyTexture(logo_big);
            logo_big = NULL;
            int bw = logo_w * k, bh = logo_h * k;
            std::vector<uint32_t> big((size_t)bw * bh);
            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++) big[(size_t)y * bw + x] = logo_px[(size_t)(y / k) * logo_w + x / k];
            make_texture(&logo_big, bw, bh);
            SDL_SetTextureScaleMode(logo_big, SDL_ScaleModeLinear);
            SDL_UpdateTexture(logo_big, NULL, big.data(), bw * 4);
            logo_k = k;
        }
        t = logo_big;
    }
    float hp = roundf(wp * logo_h / logo_w);
    ImVec2 p = ImGui::GetCursorScreenPos();     /* (on a whole screen pixel) */
    ImGui::SetCursorScreenPos(ImVec2(floorf(p.x * fbs) / fbs, floorf(p.y * fbs) / fbs));
    ImGui::Image(tid(t), ImVec2(wp / fbs, hp / fbs));
}

/* ---- the game's own pixel font (FONT1.DN1 + FONT2.DN1, read from the
   player's game folder - nothing of it is shipped): 100 glyphs of 8x8, each
   row a mask byte and four colour planes.  The white pixels are the letter
   and the black ones (drawn, not see-through) its shadow; they go into two
   textures so each can take its own colour. */
static SDL_Texture *pfont_face, *pfont_shadow;
static std::string pfont_from;                  /* the game folder they were read from ("" none; "-" tried, missing) */
static bool pixel_font(void)
{
    std::string dir = bad_folder ? "" : std::string(shrapnel_game_dir ? shrapnel_game_dir : "");
    if (dir == pfont_from || (pfont_from == "-" + dir)) return pfont_face != NULL;
    if (pfont_face) { SDL_DestroyTexture(pfont_face); SDL_DestroyTexture(pfont_shadow); pfont_face = pfont_shadow = NULL; }
    pfont_from = "-" + dir;
    if (dir.empty()) return false;
    std::vector<uint8_t> f(4000, 0);
    for (int k = 0; k < 2; k++) {
        FILE *fp = fopen(game_path(k ? "FONT2.DN1" : "FONT1.DN1"), "rb");
        if (!fp) return false;
        size_t n = 0;
        if (fseek(fp, 3, SEEK_SET) == 0) n = fread(&f[k * 2000], 1, 2000, fp);    /* (as the game loads them: past a 3-byte header) */
        fclose(fp);
        if (n != 2000) return false;
    }
    std::vector<uint32_t> face(100 * 8 * 8, 0), shadow(100 * 8 * 8, 0);
    for (int g = 0; g < 100; g++)
        for (int r = 0; r < 8; r++) {
            const uint8_t *b = &f[g * 40 + r * 5];
            for (int x = 0; x < 8; x++) {
                int bit = 0x80 >> x;
                if (!(b[0] & bit)) continue;
                int c = 0;
                for (int pl = 0; pl < 4; pl++) if (b[1 + pl] & bit) c |= 1 << pl;
                (c ? face : shadow)[(size_t)r * 800 + g * 8 + x] = 0xFFFFFFFF;
            }
        }
    make_texture(&pfont_face, 800, 8);
    make_texture(&pfont_shadow, 800, 8);
    SDL_UpdateTexture(pfont_face, NULL, face.data(), 800 * 4);
    SDL_UpdateTexture(pfont_shadow, NULL, shadow.data(), 800 * 4);
    pfont_from = dir;
    return true;
}
/* the pixel font's game pixel at 1x: about one interface pixel, as a whole
   number of screen pixels (in the interface's units, which may be fractions of
   a screen pixel on a scaled display) */
static float logo_scale(void)
{
    float fbs = ImGui::GetIO().DisplayFramebufferScale.x;
    return std::max(1.0f, roundf(ui_scale * fbs)) / fbs;
}
static float pixel_width(const char *t, float size) { return 8 * logo_scale() * size * (float)strlen(t); }
/* `t` in the game's font as an item at the cursor, `size` times the logo's
   pixels (false, drawing nothing, if the game folder has no font: the caller
   writes it its own way).  As the game does: a fixed 8-pixel grid, lowercase
   and the punctuation it has.  `face`: the letters' colour (else the game's
   white in the dark theme, the text colour in the light). */
static bool pixel_text(const char *t, float size = 2, const ImVec4 *face = NULL)
{
    if (!pixel_font()) return false;
    float s = logo_scale() * size, g = 8 * s, fbs = ImGui::GetIO().DisplayFramebufferScale.x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    p = ImVec2(floorf(p.x * fbs) / fbs, floorf(p.y * fbs) / fbs);     /* (on a whole screen pixel) */
    int n = (int)strlen(t);
    ImGui::Dummy(ImVec2(g * n, g));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float a = ImGui::GetStyle().Alpha;           /* (dimmed with the rest when disabled) */
    ImVec4 fc = face ? *face : theme_dark ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 sc = theme_dark ? ImVec4(0, 0, 0, a) : ImVec4(fc.x, fc.y, fc.z, 0.28f * a);
    fc.w = a;
    ImU32 fu = ImGui::GetColorU32(fc), su = ImGui::GetColorU32(sc);
    for (int i = 0; i < n; i++) {
        int c = (unsigned char)t[i], k;
        if (c >= 0x61 && c <= 0x7A) k = 69 + (c - 0x61);         /* (ui.c glyph(): 'a'.. at +AC8h, ' '.. at +190h) */
        else if (c >= 0x20 && c < 0x61) k = 10 + (c - 0x20);
        else continue;
        ImVec2 q0(p.x + i * g, p.y), q1(q0.x + g, q0.y + g);
        ImVec2 u0(k / 100.0f, 0), u1((k + 1) / 100.0f, 1);
        dl->AddImage(tid(pfont_shadow), q0, q1, u0, u1, su);
        dl->AddImage(tid(pfont_face), q0, q1, u0, u1, fu);
    }
    return true;
}
/* a start-screen heading: the game's font at 2x, or bold if there's none */
static void start_heading(const char *t)
{
    if (pixel_text(t, 2)) return;
    ImGui::PushFont(font_bold);
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(t);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
}
/* the start screen's buttons: filled with the accent (the main choices), or plain */
static bool start_button(const char *label, ImVec2 size, bool primary)
{
    if (primary) {
        ImVec4 f = theme_dark ? ImVec4(T.accent.x * 0.65f, T.accent.y * 0.65f, T.accent.z * 0.65f, 1) : T.accent;
        ImGui::PushStyleColor(ImGuiCol_Button, f);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(f.x * 0.88f + 0.12f, f.y * 0.88f + 0.12f, f.z * 0.88f + 0.12f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(f.x * 0.88f, f.y * 0.88f, f.z * 0.88f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, theme_dark ? T.accent : f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    bool r = ImGui::Button(label, size);
    if (primary) ImGui::PopStyleColor(5);
    return r;
}
/* a card on the start screen; card_end() evens up the heights of side-by-side ones */
static void card_begin(const char *id, float w, bool accent_top = false, const ImVec4 *edge = NULL)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, edge ? *edge : T.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * ui_scale, 18 * ui_scale));
    ImGui::BeginChild(id, ImVec2(w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Border, T.border);   /* (the edge is the card's; what's in it, as usual) */
    if (accent_top) {
        ImVec2 p = ImGui::GetWindowPos();
        float r = 8 * ui_scale;
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::GetWindowWidth(), p.y + 3 * ui_scale),
                                                  ImGui::GetColorU32(T.accent), r, ImDrawFlags_RoundCornersTop);
    }
}
static void card_end(float *need = NULL, float tallest = 0)
{
    if (need) {                                 /* (what it needs, and room to match the tallest beside it) */
        *need = ImGui::GetCursorPosY();
        if (tallest > *need) ImGui::Dummy(ImVec2(1, tallest - *need - ImGui::GetStyle().ItemSpacing.y));
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
/* a path, cut from the left with "..." to fit `w` */
static std::string fit_path(const std::string &t, float w)
{
    if (ImGui::CalcTextSize(t.c_str()).x <= w) return t;
    for (size_t i = 1; i < t.size(); i++) {
        std::string c = "..." + t.substr(i);
        if (ImGui::CalcTextSize(c.c_str()).x <= w) return c;
    }
    return "...";
}

/* ---- behind the start screen: the game's own tiles, very faint.  Two
   kinds (View > Start screen background): pieces of the stock levels (of
   every episode in the folder) wiping on and off while drifting slowly, or a
   slow rain of tiles piling up.  The tiles are greyed and put over the page's
   colour as an Overlay blend would, at a quarter strength: the page is one flat
   colour, so the result is worked out per pixel beforehand and drawn as it
   is (SDL has no Overlay mode).  Animated tiles stand still. */
struct SbgEpisode {
    int number = 0;
    uint32_t pal[16];
    std::vector<uint8_t> px;                    /* every tile's pixels (colours 0..15), ATLAS_TILES x 256 */
    std::vector<std::vector<uint16_t>> levels;  /* its ten levels, as in their files */
    std::vector<std::vector<uint16_t>> after;   /* the same as the game holds them once loaded (made when first wanted) */
    SDL_Texture *tex = NULL;                    /* the tiles, greyed and overlaid: 16 a row */
    SDL_Texture *actors = NULL;                 /* each actor's picture, the same way (sbg.actor_at) */
};
static struct StartBg {
    std::vector<SbgEpisode> eps;
    std::string dir;                            /* the game folder they were read from */
    std::string key;                            /* the page colour the textures were made for */
    uint8_t black[3] = {};                      /* a black tile, overlaid the same way */
    struct ActorAt { int x = 0, y = 0, w = 0, h = 0, ox = 0, oy = 0; };   /* where an actor's picture is in the actor atlas */
    ActorAt actor_at[N_ACTORS];
    int actors_w = 0, actors_h = 0;
    SDL_Texture *target = NULL;                 /* building: the tiles drawn on a whole-pixel grid, then this moved smoothly */
    int target_w = 0, target_h = 0;
    int cur_ep = -1, cur_level = -1;            /* the level the tiles come from */
    double t = -1;                              /* the clock (still while not animating or not focused) */
    uint32_t seed = 1;
    /* building */
    double cycle0 = 0;
    int oc = 0, orow = 0;                       /* the piece of level */
    float pan_x = 0, pan_y = 0;                 /* a slow drift across the level, screen pixels a second */
    bool fresh = true;
    /* rain: a tile is its episode << 16 | its value */
    struct Drop { int col; float y, v; uint32_t tile; };
    std::vector<Drop> drops;
    std::vector<std::vector<uint32_t>> piles;
    std::vector<float> sink;
    double next_drop = 0, next_level = 0;
} sbg;

static void sbg_reset(void) { sbg.t = -1; sbg.piles.clear(); sbg.drops.clear(); sbg.next_drop = sbg.next_level = 0; }
static uint32_t sbg_rand(void) { sbg.seed = sbg.seed * 1664525u + 1013904223u; return sbg.seed >> 8; }
static float sbg_frand(void) { return (sbg_rand() & 0xFFFF) / 65535.0f; }
static float sbg_hash(int a, int b, int c)      /* a steady 0..1 for a cell */
{
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)c * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}
static bool sbg_content(uint16_t v) { return v > 0x20 && v < 0x3000; }   /* walls and background, not the backdrop */

/* every episode's tiles and levels, read straight from the game files once
   per game folder (a few milliseconds: no episode is loaded into the editor
   for it).  The file names are the loaded episode's own, with the other
   episodes' extensions; the tile files go in as the game loads them (each
   one's last part spills into the next one's place, which that overwrites). */
static std::string sbg_ep_name(std::string name, int n)
{
    if (!name.empty()) name[name.size() - 1] = (char)('0' + n);   /* WORLDAL1.DN1 -> WORLDAL1.DN2 */
    return name;
}
static bool sbg_data(void)
{
    if (bad_folder || !ep.number || !shrapnel_game_dir) return false;
    if (sbg.dir == shrapnel_game_dir) return !sbg.eps.empty();
    sbg.dir = shrapnel_game_dir;
    for (SbgEpisode &e : sbg.eps) { if (e.tex) SDL_DestroyTexture(e.tex); if (e.actors) SDL_DestroyTexture(e.actors); }
    sbg.eps.clear();
    sbg.key.clear();
    sbg.cur_ep = sbg.cur_level = -1;
    static const uint16_t tile_files[8] = { 0x0962, 0x096C, 0x0976, 0x0980, 0x098A, 0x0995, 0x09A0, 0x09AB };
    std::string tnames[8], lnames[10];
    for (int k = 0; k < 8; k++) tnames[k] = ds_str(tile_files[k]);
    for (int k = 0; k < 10; k++) lnames[k] = ep.slot_file(k * 2);
    const int VR = 0x3200;
    std::vector<uint8_t> planes(4 * VR);
    uint16_t m[ED_MAP_CELLS];
    for (int n = 1; n <= 3; n++) {
        if (!ep.available[n]) continue;
        SbgEpisode e;
        e.number = n;
        memcpy(e.pal, ep.pal, sizeof e.pal);    /* (the same EGA colours in every episode) */
        std::fill(planes.begin(), planes.end(), 0);
        bool ok = true;
        for (int k = 0; k < 8 && ok; k++) {
            FILE *f = fopen(game_path(sbg_ep_name(tnames[k], n).c_str()), "rb");
            if (!f) { ok = false; break; }
            fgetc(f); fgetc(f); fgetc(f);
            for (int i = 0; i < 0x780; i++) {
                fgetc(f);
                for (int pl = 0; pl < 4; pl++) {
                    int c = fgetc(f);
                    int at = k * 0x600 + i;
                    if (at < VR) planes[pl * VR + at] = (uint8_t)c;
                }
            }
            fclose(f);
        }
        if (!ok) continue;
        e.px.resize((size_t)ATLAS_TILES * 256);
        for (int t = 0; t < ATLAS_TILES; t++)
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    int o = t * 0x20 + y * 2 + (x >> 3), sh = 7 - (x & 7);
                    uint8_t v = 0;
                    if (o < VR)
                        for (int pl = 0; pl < 4; pl++) v |= (uint8_t)(((planes[pl * VR + o] >> sh) & 1) << pl);
                    e.px[(size_t)t * 256 + y * 16 + x] = v;
                }
        for (int k = 0; k < 10; k++)
            if (!lnames[k].empty() && read_map_file(game_path(sbg_ep_name(lnames[k], n).c_str()), m))
                e.levels.emplace_back(m, m + ED_MAP_CELLS);
        if (!e.levels.empty()) sbg.eps.push_back(std::move(e));
    }
    if (getenv("SHRAPNEL_BG_CHECK"))            /* (tests: the tiles as read here against the editor's own) */
        for (SbgEpisode &e : sbg.eps) {
            Episode *tmp = new Episode();
            tmp->load(e.number);
            int bad = 0, pal_bad = memcmp(tmp->pal, e.pal, sizeof e.pal) != 0;
            delete tmp;
            uint8_t t[256];
            for (int i = 0; i < 0x3000 / 0x20; i++) {
                ed_tile((uint16_t)(i * 0x20), t);
                if (memcmp(t, &e.px[(size_t)i * 256], 256)) bad++;
            }
            fprintf(stderr, "start background: episode %d, %d of %d tiles differ from the editor's%s\n", e.number, bad, 0x3000 / 0x20,
                    pal_bad ? " (and its colours)" : "");
            ep.load(ep.number);
        }
    return !sbg.eps.empty();
}

/* the tiles, greyed and laid over the page colour at a quarter strength */
static bool sbg_tiles(void)
{
    if (!sbg_data()) return false;
    ImVec4 bg = T.bar;
    char key[64];
    snprintf(key, sizeof key, "%.3f %.3f %.3f %d %d", bg.x, bg.y, bg.z, theme_dark, ep.number);
    if (sbg.key == key) return true;
    sbg.key = key;
    const float strength = 0.25f * (theme_dark ? 1.0f : 0.8f);   /* (a quarter: faint) */
    float b[3] = { bg.x, bg.y, bg.z };
    for (int ch = 0; ch < 3; ch++) {            /* black (what shows the backdrop in the game), overlaid */
        float r = b[ch] < 0.5f ? 0 : 1 - 2 * (1 - b[ch]);
        sbg.black[ch] = (uint8_t)std::max(0, std::min(255, (int)lroundf((b[ch] + (r - b[ch]) * strength) * 255)));
    }
    auto overlay = [&](uint32_t c) -> uint32_t {   /* a colour greyed and laid over the page */
        float l = (0.299f * ((c >> 16) & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * (c & 255)) / 255;
        uint32_t o = 0xFF000000u;
        for (int ch = 0; ch < 3; ch++) {
            float r = b[ch] < 0.5f ? 2 * b[ch] * l : 1 - 2 * (1 - b[ch]) * (1 - l);   /* Overlay */
            r = b[ch] + (r - b[ch]) * strength;
            o |= (uint32_t)std::max(0, std::min(255, (int)lroundf(r * 255))) << (16 - ch * 8);
        }
        return o;
    };
    /* where each actor's picture goes in the actor atlas (the same for every
       episode: the actors are the loaded episode's, their sprites are the same
       files in every episode, and pieces made of map tiles take each episode's tiles) */
    {
        int x = 0, y = 0, shelf = 0, aw = 512;
        for (int i = 0; i < N_ACTORS; i++) {
            const ActorLook &l = ep.looks[i];
            StartBg::ActorAt &at = sbg.actor_at[i];
            at = StartBg::ActorAt();
            if (l.pieces.empty()) continue;
            int x0 = 0, y0 = 0, x1 = 16, y1 = 16;
            for (const Piece &pc : l.pieces) { x0 = std::min(x0, pc.dx); y0 = std::min(y0, pc.dy); x1 = std::max(x1, pc.dx + 16); y1 = std::max(y1, pc.dy + 16); }
            at.w = x1 - x0; at.h = y1 - y0; at.ox = x0; at.oy = y0;
            if (at.w > aw) { at.w = 0; continue; }
            if (x + at.w > aw) { x = 0; y += shelf; shelf = 0; }
            at.x = x; at.y = y;
            x += at.w; shelf = std::max(shelf, at.h);
        }
        sbg.actors_w = aw; sbg.actors_h = std::max(16, y + shelf);
    }
    std::vector<uint32_t> img(256 * (ATLAS_TILES / 16) * 16);
    std::vector<uint32_t> act((size_t)sbg.actors_w * sbg.actors_h);
    for (SbgEpisode &e : sbg.eps) {
        for (int i = 0; i < ATLAS_TILES; i++)
            for (int k = 0; k < 256; k++)
                img[((i / 16) * 16 + k / 16) * 256 + (i % 16) * 16 + k % 16] = overlay(e.pal[e.px[(size_t)i * 256 + k] & 15]);
        make_texture(&e.tex, 256, ATLAS_TILES / 16 * 16);
        SDL_UpdateTexture(e.tex, NULL, img.data(), 256 * 4);
        std::fill(act.begin(), act.end(), 0);
        for (int i = 0; i < N_ACTORS; i++) {
            const StartBg::ActorAt &at = sbg.actor_at[i];
            if (!at.w) continue;
            for (const Piece &pc : ep.looks[i].pieces)
                for (int k = 0; k < 256; k++) {
                    int ci = pc.px[k];
                    if (pc.tile && pc.tile < 0x3000 && e.number != ep.number) ci = e.px[(size_t)(pc.tile / 0x20) * 256 + k];
                    if (ci == 255) continue;
                    act[(size_t)(at.y + pc.dy - at.oy + k / 16) * sbg.actors_w + at.x + pc.dx - at.ox + k % 16] = overlay(e.pal[ci & 15]);
                }
        }
        make_texture(&e.actors, sbg.actors_w, sbg.actors_h);
        SDL_UpdateTexture(e.actors, NULL, act.data(), sbg.actors_w * 4);
    }
    return true;
}
/* the current level as in its file (where the actors are) */
static const uint16_t *sbg_raw(void)
{
    if (sbg.cur_ep < 0 || sbg.cur_ep >= (int)sbg.eps.size()) return NULL;
    SbgEpisode &e = sbg.eps[sbg.cur_ep];
    return sbg.cur_level >= 0 && sbg.cur_level < (int)e.levels.size() ? e.levels[sbg.cur_level].data() : NULL;
}
/* ... and as the game holds it once loaded: each actor's cell takes what the
   game's loader puts there (most often a copy of a neighbour), worked out by
   the editor's own loader the first time the level is wanted (a millisecond
   or so; the actors' rules are the same in every episode) */
static const uint16_t *sbg_level(void)
{
    const uint16_t *raw = sbg_raw();
    if (!raw) return NULL;
    SbgEpisode &e = sbg.eps[sbg.cur_ep];
    if (e.after.size() != e.levels.size()) e.after.resize(e.levels.size());
    std::vector<uint16_t> &a = e.after[sbg.cur_level];
    if (a.empty()) {
        static Level *lv = new Level();
        lv->load_map(ep, sbg.cur_level * 2, raw, "");
        a.assign(lv->after, lv->after + ED_MAP_CELLS);
    }
    return a.data();
}

/* another level, from any episode, not the one just shown */
static void sbg_next_level(void)
{
    int total = 0;
    for (SbgEpisode &e : sbg.eps) total += (int)e.levels.size();
    if (!total) return;
    for (int tries = 0; tries < 8; tries++) {
        int k = (int)(sbg_rand() % total), ei = 0;
        while (k >= (int)sbg.eps[ei].levels.size()) k -= (int)sbg.eps[ei++].levels.size();
        if (total > 1 && ei == sbg.cur_ep && k == sbg.cur_level) continue;
        sbg.cur_ep = ei;
        sbg.cur_level = k;
        return;
    }
}

/* building: a piece of a level that is all level - no edge of the map and
   none of the empty space around a level's outside - for the whole of the
   window and the whole of its drift (cols x rows cells on screen, drifting
   for `secs` at ts pixels a cell) */
static void sbg_new_piece(int cols, int rows, float ts, double secs)
{
    float a = sbg_frand() * 6.2831853f, speed = 7 * ui_scale;
    sbg.pan_x = cosf(a) * speed;
    sbg.pan_y = sinf(a) * speed;
    float dx = sbg.pan_x * (float)secs, dy = sbg.pan_y * (float)secs;
    int lo_c = (int)floorf(std::min(0.0f, dx) / ts) - 2, hi_c = cols + (int)ceilf(std::max(0.0f, dx) / ts) + 3;
    int lo_r = (int)floorf(std::min(0.0f, dy) / ts) - 2, hi_r = rows + (int)ceilf(std::max(0.0f, dy) / ts) + 3;
    int need_c = hi_c - lo_c, need_r = hi_r - lo_r;
    int best = -1, best_oc = 0, best_or = 0, best_ep = -1, best_lv = -1;
    for (int tries = 0; tries < 6 && best < 2000000; tries++) {
        sbg_next_level();
        const uint16_t *m = sbg_level();
        if (!m) return;
        /* where the level is: the rows and columns with a fair share of walls in them */
        int rc[MAP_H] = {}, cc[MAP_W] = {};
        for (int r = 0; r < MAP_H; r++)
            for (int c = 0; c < MAP_W; c++) if (sbg_content(m[r * MAP_W + c])) { rc[r]++; cc[c]++; }
        int r0 = 0, r1 = MAP_H - 1, c0 = 0, c1 = MAP_W - 1;
        while (r0 < r1 && rc[r0] < MAP_W / 5) r0++;
        while (r1 > r0 && rc[r1] < MAP_W / 5) r1--;
        while (c0 < c1 && cc[c0] < MAP_H / 5) c0++;
        while (c1 > c0 && cc[c1] < MAP_H / 5) c1--;
        r0++; c0++; r1--; c1--;                 /* (not the outer wall itself either) */
        int span_c = c1 - c0 + 1 - need_c, span_r = r1 - r0 + 1 - need_r;
        for (int k = 0; k < 40; k++) {
            int sc = span_c > 0 ? c0 + (int)(sbg_rand() % (span_c + 1)) : c0 + span_c / 2;
            int sr = span_r > 0 ? r0 + (int)(sbg_rand() % (span_r + 1)) : r0 + span_r / 2;
            /* no stretch of nothing: every ninth of it has walls in it; and
               shapes, not a solid block (count the edges of walls) */
            int blocks_ok = 1, edges = 0;
            for (int by = 0; by < 3; by++)
                for (int bx = 0; bx < 3; bx++) {
                    int n = 0, filled = 0;
                    for (int r = sr + by * need_r / 3; r < sr + (by + 1) * need_r / 3; r++)
                        for (int c = sc + bx * need_c / 3; c < sc + (bx + 1) * need_c / 3; c++) {
                            n++;
                            if (r >= 0 && r < MAP_H && c >= 0 && c < MAP_W && sbg_content(m[r * MAP_W + c])) filled++;
                        }
                    if (n && filled * 100 < n * 15) blocks_ok = 0;
                }
            for (int r = std::max(0, sr); r < std::min(MAP_H - 1, sr + need_r); r++)
                for (int c = std::max(0, sc); c < std::min(MAP_W - 1, sc + need_c); c++) {
                    bool h = sbg_content(m[r * MAP_W + c]);
                    edges += (h != sbg_content(m[r * MAP_W + c + 1])) + (h != sbg_content(m[(r + 1) * MAP_W + c]));
                }
            bool fits = span_c >= 0 && span_r >= 0;      /* (a level too small for the window: the best there is) */
            int score = (fits && blocks_ok ? 2000000 : fits ? 1000000 : 0) + edges;
            if (score > best) { best = score; best_oc = sc - lo_c; best_or = sr - lo_r; best_ep = sbg.cur_ep; best_lv = sbg.cur_level; }
        }
    }
    sbg.cur_ep = best_ep;
    sbg.cur_level = best_lv;
    sbg.oc = best_oc;
    sbg.orow = best_or;
}

static void start_background(ImDrawList *dl, ImVec2 p0, ImVec2 p1)
{
    if (start_bg == BG_OFF || !sbg_tiles()) return;
    /* game pixels as big as the window's height suits (whole screen pixels):
       2x below about 1260 pixels high (a 1080p screen), 3x from there (a
       1440p one, maximised), 4x from about 1680, 5x from 2100 */
    float fbs0 = ImGui::GetIO().DisplayFramebufferScale.x;
    float ts = 16 * std::max(2.0f, floorf((p1.y - p0.y) * fbs0 / 420)) / fbs0;
    int cols = (int)ceilf((p1.x - p0.x) / ts), rows = (int)ceilf((p1.y - p0.y) / ts);
    if (cols <= 0 || rows <= 0) return;
    bool moving = view.animate && app_focused;
    if (sbg.t < 0) {                            /* (first time: a running start) */
        sbg.seed = (uint32_t)SDL_GetTicks() | 1;
        const char *e = getenv("SHRAPNEL_BG_TIME");
        sbg.t = e ? atof(e) : (start_bg == BG_BUILD ? 6.5 : 0);
        sbg.fresh = true;
        if (e) sbg.seed = 12345;
    }
    static bool fixed = getenv("SHRAPNEL_BG_TIME") != NULL;    /* (tests: 60 steps a second, whatever the frame rate) */
    double dt = moving ? (fixed ? 1.0 / 60 : std::min(0.1, (double)ImGui::GetIO().DeltaTime)) : 0;
    sbg.t += dt;
    auto tile = [&](uint32_t et, float x, float y, float a) {   /* (episode << 16 | value) */
        int ei = (int)(et >> 16), i = (int)((et & 0xFFFF) / 0x20);
        if (ei >= (int)sbg.eps.size() || i >= ATLAS_TILES || !sbg.eps[ei].tex) return;
        ImVec2 uv0((i % 16) / 16.0f, (i / 16) * 16.0f / (ATLAS_TILES / 16 * 16));
        ImVec2 uv1(uv0.x + 1 / 16.0f, uv0.y + 16.0f / (ATLAS_TILES / 16 * 16));
        ImU32 col = a >= 1 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, (int)(a * 255));
        dl->AddImage(tid(sbg.eps[ei].tex), ImVec2(floorf(x), floorf(y)), ImVec2(floorf(x) + ts, floorf(y) + ts), uv0, uv1, col);
    };

    if (start_bg == BG_BUILD) {
        /* wipe on (3.5 s), stand (9 s), wipe off (3 s), a breath (1.5 s) */
        const double BUILD = 3.5, STAND = 9, APART = 3, GAP = 1.5, CYCLE = BUILD + STAND + APART + GAP;
        const double EACH = 0.4, SPREAD_IN = BUILD - EACH, SPREAD_OUT = APART - EACH;
        double ct = sbg.t - sbg.cycle0;
        if (sbg.fresh || ct >= CYCLE) {
            if (!sbg.fresh) sbg.cycle0 += CYCLE * floor(ct / CYCLE);
            else sbg.cycle0 = 0;
            sbg.fresh = false;
            sbg_new_piece(cols, rows, ts, CYCLE);
            ct = sbg.t - sbg.cycle0;
        }
        if (!view.animate) ct = BUILD + 1;      /* (no motion: the piece, built) */
        const uint16_t *m = sbg_level();
        if (!m || sbg.cur_ep < 0 || !sbg.eps[sbg.cur_ep].tex) return;
        /* the whole time, a slow drift across the level.  The tiles are drawn
           on a whole-pixel grid into a texture of their own (crisp), and that
           is drawn at the fraction of a pixel left over, smoothed, so the drift
           is smooth rather than a pixel at a time.  It wipes on from the bottom
           left to the top right, and off the same way; a cell that shows the
           backdrop in the game is a black tile. */
        float ox = view.animate ? sbg.pan_x * (float)ct : 0, oy = view.animate ? sbg.pan_y * (float)ct : 0;
        float fbs = ImGui::GetIO().DisplayFramebufferScale.x;
        float oxi = floorf(ox * fbs) / fbs, oyi = floorf(oy * fbs) / fbs;     /* (whole pixels of the screen) */
        float W = p1.x - p0.x, H = p1.y - p0.y;
        int tw = (int)ceilf((W + 2 * ts) * fbs), th = (int)ceilf((H + 2 * ts) * fbs);
        if (!sbg.target || tw != sbg.target_w || th != sbg.target_h) {
            if (sbg.target) SDL_DestroyTexture(sbg.target);
            sbg.target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (!sbg.target) return;
            SDL_SetTextureScaleMode(sbg.target, SDL_ScaleModeLinear);
            SDL_SetTextureBlendMode(sbg.target, SDL_BLENDMODE_NONE);   /* (opaque: its own page colour) */
            sbg.target_w = tw; sbg.target_h = th;
        }
        SDL_Texture *was = SDL_GetRenderTarget(ren);
        float sx, sy;
        SDL_RenderGetScale(ren, &sx, &sy);
        SDL_SetRenderTarget(ren, sbg.target);
        SDL_RenderSetScale(ren, 1, 1);
        ImVec4 bg = T.bar;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, (Uint8)lroundf(bg.x * 255), (Uint8)lroundf(bg.y * 255), (Uint8)lroundf(bg.z * 255), 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_Texture *tex = sbg.eps[sbg.cur_ep].tex;
        int tpx = (int)lroundf(ts * fbs);
        int c0 = (int)floorf(oxi / ts) - 1, r0 = (int)floorf(oyi / ts) - 1;
        for (int r = r0; r <= r0 + rows + 2; r++)
            for (int c = c0; c <= c0 + cols + 2; c++) {
                int mc = ((sbg.oc + c) % MAP_W + MAP_W) % MAP_W, mr = ((sbg.orow + r) % MAP_H + MAP_H) % MAP_H;
                uint16_t v = m[mr * MAP_W + mc];            /* (actors' cells: what the loader left there) */
                float x = c * ts - oxi, y = r * ts - oyi;   /* (from the window's corner) */
                float u = std::max(0.0f, std::min(1.0f, (x + ts / 2) / W));
                float w = std::max(0.0f, std::min(1.0f, 1 - (y + ts / 2) / H));
                float d = 0.92f * (u * W + w * H) / (W + H);                          /* (the wipe's front, at 45 degrees) */
                double tin = (d + 0.08f * sbg_hash(mc, mr, 1)) * SPREAD_IN;
                double tout = BUILD + STAND + (d + 0.08f * sbg_hash(mc, mr, 2)) * SPREAD_OUT;
                float a_in = (float)std::max(0.0, std::min(1.0, (ct - tin) / EACH));
                float a_out = (float)std::max(0.0, std::min(1.0, (ct - tout) / EACH));
                if (a_in <= 0 || a_out >= 1) continue;
                Uint8 a = (Uint8)lroundf(a_in * (1 - a_out) * 255);         /* (a fade in place) */
                SDL_Rect dst = { (int)lroundf((x + ts) * fbs), (int)lroundf((y + ts) * fbs), tpx, tpx };
                if (sbg_content(v)) {
                    int i = v / 0x20;
                    SDL_Rect src = { (i % 16) * 16, (i / 16) * 16, 16, 16 };
                    SDL_SetTextureAlphaMod(tex, a);
                    SDL_RenderCopy(ren, tex, &src, &dst);
                } else {
                    SDL_SetRenderDrawColor(ren, sbg.black[0], sbg.black[1], sbg.black[2], a);
                    SDL_RenderFillRect(ren, &dst);
                }
            }
        /* the actors over the cells, each fading with its own cell (ones just
           off the edge can reach in) */
        SDL_Texture *atex = sbg.eps[sbg.cur_ep].actors;
        const uint16_t *raw = sbg_raw();
        float gp = ts / 16;                     /* (screen pixels a game pixel) */
        for (int r = r0 - 3; r <= r0 + rows + 5 && atex; r++)
            for (int c = c0 - 3; c <= c0 + cols + 5; c++) {
                int mc = ((sbg.oc + c) % MAP_W + MAP_W) % MAP_W, mr = ((sbg.orow + r) % MAP_H + MAP_H) % MAP_H;
                uint16_t v = raw[mr * MAP_W + mc];
                if (v < 0x3000 || v >= 0x3000 + N_ACTORS) continue;
                const StartBg::ActorAt &at = sbg.actor_at[v - 0x3000];
                if (!at.w) continue;
                float x = c * ts - oxi, y = r * ts - oyi;
                float u = std::max(0.0f, std::min(1.0f, (x + ts / 2) / W));
                float w = std::max(0.0f, std::min(1.0f, 1 - (y + ts / 2) / H));
                float d = 0.92f * (u * W + w * H) / (W + H);
                double tin = (d + 0.08f * sbg_hash(mc, mr, 1)) * SPREAD_IN;
                double tout = BUILD + STAND + (d + 0.08f * sbg_hash(mc, mr, 2)) * SPREAD_OUT;
                float a_in = (float)std::max(0.0, std::min(1.0, (ct - tin) / EACH));
                float a_out = (float)std::max(0.0, std::min(1.0, (ct - tout) / EACH));
                if (a_in <= 0 || a_out >= 1) continue;
                SDL_Rect src = { at.x, at.y, at.w, at.h };
                SDL_Rect dst = { (int)lroundf((x + ts + at.ox * gp) * fbs), (int)lroundf((y + ts + at.oy * gp) * fbs),
                                 (int)lroundf(at.w * gp * fbs), (int)lroundf(at.h * gp * fbs) };
                SDL_SetTextureAlphaMod(atex, (Uint8)lroundf(a_in * (1 - a_out) * 255));
                SDL_RenderCopy(ren, atex, &src, &dst);
            }
        if (atex) SDL_SetTextureAlphaMod(atex, 255);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetRenderTarget(ren, was);
        SDL_RenderSetScale(ren, sx, sy);
        float fx = ox - oxi, fy = oy - oyi;     /* (the fraction left over) */
        ImVec2 q0(p0.x - ts - fx, p0.y - ts - fy);
        dl->AddImage(tid(sbg.target), q0, ImVec2(q0.x + tw / fbs, q0.y + th / fbs));
    } else {
        /* rain: tiles fall down the columns and pile up; a pile that gets
           high enough lets its bottom tile go and settles down a place */
        if (!sbg_level() || sbg.t >= sbg.next_level) { sbg_next_level(); sbg.next_level = sbg.t + 40; }
        const uint16_t *m = sbg_level();
        if (!m) return;
        int high = std::max(3, rows / 5);
        auto any_tile = [&]() -> uint32_t {
            for (int k = 0; k < 40; k++) {
                uint16_t v = m[sbg_rand() % ED_MAP_CELLS];
                if (sbg_content(v)) return (uint32_t)sbg.cur_ep << 16 | v;
            }
            return (uint32_t)sbg.cur_ep << 16 | 0x1800;
        };
        if ((int)sbg.piles.size() != cols) {    /* (a start mid-way: an uneven skyline, and some on the way down) */
            sbg.piles.assign(cols, {});
            sbg.sink.assign(cols, 0);
            sbg.drops.clear();
            float h = high * 0.5f;
            for (int c = 0; c < cols; c++) {
                h = std::max(0.0f, std::min((float)high, h + (sbg_frand() - 0.5f) * 2.5f));
                for (int k = 0; k < (int)h; k++) sbg.piles[c].push_back(any_tile());
            }
            for (int k = 0; k < cols * rows / 60; k++) {
                StartBg::Drop d;
                d.col = (int)(sbg_rand() % cols);
                d.y = sbg_frand() * rows * ts * 0.7f;
                d.v = ts * (2.6f + 1.6f * sbg_frand());
                d.tile = any_tile();
                sbg.drops.push_back(d);
            }
        }
        float rate = 0.12f * 90.0f / cols;      /* seconds between drops, for the window's width */
        while (moving && sbg.t >= sbg.next_drop) {
            sbg.next_drop = std::max(sbg.next_drop, sbg.t - 1) + rate * (0.5f + sbg_frand());
            StartBg::Drop d;
            d.col = (int)(sbg_rand() % cols);
            d.y = -ts;
            d.v = ts * (2.6f + 1.6f * sbg_frand());            /* tiles a second */
            d.tile = any_tile();
            sbg.drops.push_back(d);
        }
        float floor_y = p0.y + rows * ts;
        for (int c = 0; c < cols; c++) {        /* the piles */
            std::vector<uint32_t> &pl = sbg.piles[c];
            if ((int)pl.size() > high || sbg.sink[c] > 0) {
                sbg.sink[c] += (float)dt / 1.2f;
                if (sbg.sink[c] >= 1) { pl.erase(pl.begin()); sbg.sink[c] = 0; }
            }
            float off = sbg.sink[c] * ts;
            for (size_t k = 0; k < pl.size(); k++)
                tile(pl[k], p0.x + c * ts, floor_y - (k + 1) * ts + off, k == 0 ? 1 - sbg.sink[c] : 1);
        }
        for (size_t i = 0; i < sbg.drops.size();) {   /* what's falling */
            StartBg::Drop &d = sbg.drops[i];
            std::vector<uint32_t> &pl = sbg.piles[d.col];
            float top = floor_y - (pl.size() + 1) * ts + sbg.sink[d.col] * ts;
            d.y += d.v * (float)dt;
            if (p0.y + d.y >= top) { pl.push_back(d.tile); sbg.drops.erase(sbg.drops.begin() + i); continue; }
            tile(d.tile, p0.x + d.col * ts, p0.y + d.y, std::min(1.0f, (d.y + ts) / (3 * ts)));
            i++;
        }
    }
}

static void start_screen(float y0)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, y0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - y0));
    /* the page's content in the middle of the window, top to bottom, when it
       fits (no scrollbar, no scrolling); from the top, scrolling, when not.
       (By its height last frame.) */
    static float content_h = 0;
    const float u = ui_scale, margin = 20 * u;
    float inner = io.DisplaySize.y - y0 - 2 * ImGui::GetStyle().WindowPadding.y;
    bool fits = content_h > 0 && content_h + 2 * margin <= inner;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, T.bar);    /* (the page a shade back, the cards on it) */
    ImGui::Begin("##start", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 (fits ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0));
    ImGui::PopStyleColor();
    if (fits) ImGui::SetScrollY(0);
    {   /* the tiles behind, fixed to the window (not scrolled) */
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        start_background(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    }
    float W = std::min(io.DisplaySize.x - 64 * u, 960 * u);
    float x0 = floorf((io.DisplaySize.x - W) / 2);
    ImGui::Dummy(ImVec2(1, fits ? floorf((inner - content_h) / 2) : margin));
    ImGui::SetCursorPosX(x0);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(W, 0));
    const ImVec2 sp = ImGui::GetStyle().ItemSpacing;

    /* the logo, and what Shrapnel is - centred */
    if (logo_w > 0) {                           /* (centred, if 1x is wider than the page: overhanging both sides alike) */
        float fbs = ImGui::GetIO().DisplayFramebufferScale.x;
        ImGui::SetCursorPosX(x0 + floorf((W - std::max(W, logo_w / fbs)) / 2));
    }
    draw_logo(W);
    if (logo_w < 0) {                           /* (no logo: the word) */
        ImGui::SetWindowFontScale(1.6f);
        ImGui::Text("Shrapnel");
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::Dummy(ImVec2(1, 6 * u));
    {
        const char *tag = "A level editor for Duke Nukem (1991), for Techbot and for the original DOS game.";
        float tw = pixel_font() ? pixel_width(tag, 1) : ImGui::CalcTextSize(tag).x;
        ImGui::SetCursorPosX(x0 + std::max(0.0f, floorf((W - tw) / 2)));
        if (!pixel_text(tag, 1, &T.muted)) ImGui::TextDisabled("%s", tag);
    }
    ImGui::Dummy(ImVec2(1, 26 * u));

    /* the game folder: when it's fine, a plain statement of it (and a way to
       change it); when it isn't, or while changing it, what to do */
    start_heading("Game folder");
    ImGui::Dummy(ImVec2(1, 4 * u));
    static bool folder_edit;                    /* changing a folder that was fine */
    static std::string folder_err;              /* the last folder tried, if it had no game in it */
    bool asking = bad_folder || folder_edit;
    auto use_folder = [&](const char *dir) {   /* (a folder that was fine stays, if the new one isn't) */
        std::string prev = (!bad_folder && shrapnel_game_dir) ? shrapnel_game_dir : "";
        if (try_folder(dir)) { folder_edit = false; folder_err.clear(); return; }
        folder_err = dir;
        if (!prev.empty()) try_folder(prev.c_str());
    };
    static bool first = true;                   /* (the folder Shrapnel started with: say why it won't do) */
    if (first) { first = false; if (bad_folder && folder_buf[0]) folder_err = folder_buf; }
    ImVec4 warn_edge(T.warn.x, T.warn.y, T.warn.z, 0.7f);
    card_begin("##folder", W, false, bad_folder ? &warn_edge : NULL);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float big = font_icons_lg->FontSize * io.FontGlobalScale, line = ImGui::GetTextLineHeightWithSpacing();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tx = p.x + big + 14 * u;
        const char *icon = asking ? (bad_folder ? ICON_WARNING_FILLED : ICON_FOLDER_OPEN) : ICON_CHECK_CIRCLE;
        ImVec4 ic = asking ? (bad_folder ? T.warn : T.accent_text) : T.good;
        dl->AddText(font_icons_lg, big, ImVec2(p.x, p.y + (2 * line - big) / 2 - 2 * u), ImGui::GetColorU32(ic), icon);
        float bw = ImGui::CalcTextSize("Change folder...").x + 32 * u;
        ImGui::SetCursorScreenPos(ImVec2(tx, p.y));
        ImGui::BeginGroup();
        ImGui::PushFont(font_bold);
        if (!asking) {
            std::string eps;
            int n = 0;
            for (int k = 1; k <= 3; k++) if (ep.available[k]) n++;
            for (int k = 1, i = 0; k <= 3; k++) {
                if (!ep.available[k]) continue;
                eps += (i == 0 ? "" : i == n - 1 ? " and " : ", ") + std::to_string(k);
                i++;
            }
            std::string title = "Ready: Duke Nukem " + std::string(n > 1 ? "episodes " : "episode ") + eps + " found";
            ImGui::TextUnformatted(title.c_str());
        } else ImGui::TextUnformatted(bad_folder ? "Shrapnel needs your Duke Nukem folder" : "Choose another Duke Nukem folder");
        ImGui::PopFont();
        float avail = ImGui::GetContentRegionAvail().x - (asking ? 0 : bw + 16 * u);
        if (!asking) {
            std::error_code ec;
            std::string full = shrapnel_game_dir ? shrapnel_game_dir : "";
            std::filesystem::path fp = std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::u8path(full), ec), ec);
            if (!ec) full = fp.u8string();
            ImGui::TextColored(T.muted, "%s", fit_path(full, avail).c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", full.c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail);
            ImGui::TextUnformatted("The folder of an installed copy of the game, with DN1.EXE, DN2.EXE or DN3.EXE in it "
                                   "(any episodes). Shrapnel only reads from it.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();
        if (!asking) {                          /* (the one thing to do here: change it) */
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw);
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + (2 * line - ImGui::GetFrameHeight()) / 2 - 2 * u));
            if (ImGui::Button("Change folder...", ImVec2(bw, 0))) {
                folder_edit = true;
                folder_err.clear();
                snprintf(folder_buf, sizeof folder_buf, "%s", shrapnel_game_dir ? shrapnel_game_dir : "");
            }
        } else {
            ImGui::Dummy(ImVec2(1, 6 * u));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + big + 14 * u);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * u, 7 * u));
            float bw1 = ImGui::CalcTextSize("Browse...").x + 28 * u, bw2 = ImGui::CalcTextSize("Use this folder").x + 28 * u;
            float bw3 = folder_edit ? ImGui::CalcTextSize("Cancel").x + 28 * u : 0;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw1 - bw2 - bw3 - (folder_edit ? 3 : 2) * sp.x);
            bool enter = ImGui::InputTextWithHint("##folder", "Type or paste the folder, or Browse", folder_buf, sizeof folder_buf,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (start_button("Browse...", ImVec2(bw1, 0), true)) {
                std::string d = dialog_folder(folder_buf, "Choose the game folder (with DN1.EXE)");
                if (!d.empty()) { snprintf(folder_buf, sizeof folder_buf, "%s", d.c_str()); use_folder(folder_buf); }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!folder_buf[0]);
            if (ImGui::Button("Use this folder", ImVec2(bw2, 0)) || (enter && folder_buf[0])) use_folder(folder_buf);
            ImGui::EndDisabled();
            if (folder_edit) {
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(bw3, 0))) { folder_edit = false; folder_err.clear(); }
            }
            ImGui::PopStyleVar();
            if (!folder_err.empty()) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + big + 14 * u);
                ImGui::TextColored(T.warn, "No DN1.EXE, DN2.EXE or DN3.EXE in %s.", fit_path(folder_err, W * 0.6f).c_str());
            }
        }
    }
    card_end();
    ImGui::Dummy(ImVec2(1, 18 * u));

    /* the two kinds of project */
    static int new_ep = 1, new_levels = 10, dos_ep = 1;
    static float need[2];
    const char *open_now = NULL;
    bool ok = !bad_folder;
    float gap = 20 * u, col = floorf((W - gap) / 2), tallest = std::max(need[0], need[1]);
    ImGui::BeginDisabled(!ok);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * u, 8 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * u, 10 * u));
    card_begin("##techbot", col, true);
    {
        float half = floorf((ImGui::GetContentRegionAvail().x - 10 * u) / 2), full = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(1, 2 * u));
        start_heading("Techbot Compatible");
        ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
        ImGui::TextWrapped("Plays in Techbot. An episode can have any number of levels, each hallway its own map. "
                           "Saved as a .LEV (one level) or an .EPI (an episode), ready to share.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(1, 2 * u));
        if (start_button(ICON_NOTE_ADD "  New level", ImVec2(half, 0), true)) open_now = "New level";
        ImGui::SameLine();
        if (start_button(ICON_CREATE_NEW_FOLDER "  New episode", ImVec2(half, 0), true)) open_now = "New episode";
        if (start_button(ICON_FILE_OPEN "  Open a level", ImVec2(half, 0), false)) open_now = "Open a level";
        ImGui::SameLine();
        if (start_button(ICON_FOLDER_OPEN "  Open an episode", ImVec2(half, 0), false)) open_now = "Open an episode";
        if (start_button(ICON_PLAY_ARROW "  Open the example level", ImVec2(full, 0), false)) open_example();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("A small finished level to play and take apart: keys and locks, items, traps, an exit.");
    }
    card_end(&need[0], tallest);
    ImGui::SameLine(0, gap);
    card_begin("##dos", col, true);
    {
        float half = floorf((ImGui::GetContentRegionAvail().x - 10 * u) / 2);
        ImGui::Dummy(ImVec2(1, 2 * u));
        start_heading("DOS Compatible");
        ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
        ImGui::TextWrapped("Plays in the original DOS game too. An episode of ten levels and the one hallway they share, "
                           "saved as that episode's own map files, to copy over a DOS install.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(1, 2 * u));
        if (start_button(ICON_CREATE_NEW_FOLDER "  New DOS episode", ImVec2(half, 0), true)) open_now = "New DOS episode";
        ImGui::SameLine();
        if (start_button(ICON_FOLDER_OPEN "  Open a DOS episode", ImVec2(half, 0), false)) open_now = "Open a DOS episode";
    }
    card_end(&need[1], tallest);
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();
    if (open_now) ImGui::OpenPopup(open_now);

    /* unsaved work */
    if (ok && path_exists(untitled_autosave())) {
        ImGui::Dummy(ImVec2(1, 10 * u));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(T.warn.x, T.warn.y, T.warn.z, theme_dark ? 0.12f : 0.10f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(T.warn.x, T.warn.y, T.warn.z, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * u);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * u, 10 * u));
        ImGui::BeginChild("##unsaved", ImVec2(W, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(T.warn, ICON_WARNING_FILLED);
        ImGui::SameLine();
        ImGui::TextUnformatted("A new project was left unsaved when Shrapnel last closed.");
        float bw = 110 * u;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - bw * 2 - sp.x);
        if (start_button("Recover it", ImVec2(bw, 0), true)) { proj = Project(); restore_autosave(); }
        ImGui::SameLine();
        if (ImGui::Button("Discard it", ImVec2(bw, 0))) path_remove(untitled_autosave());
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    /* recent projects */
    ImGui::Dummy(ImVec2(1, 18 * u));
    start_heading("Recent");
    ImGui::Dummy(ImVec2(1, 4 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * u, 8 * u));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, T.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, T.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * u);
    ImGui::BeginChild("##recent", ImVec2(W, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    if (recent.empty()) {
        ImGui::Dummy(ImVec2(1, 4 * u));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12 * u);
        ImGui::TextColored(T.muted, "Nothing yet. The projects you open will be listed here.");
        ImGui::Dummy(ImVec2(1, 4 * u));
    }
    ImGui::BeginDisabled(!ok);
    float row_h = 32 * u, rw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp.x, 2 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(T.accent.x, T.accent.y, T.accent.z, theme_dark ? 0.16f : 0.10f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(T.accent.x, T.accent.y, T.accent.z, theme_dark ? 0.26f : 0.18f));
    for (size_t i = 0; i < recent.size(); i++) {
        const std::string &r = recent[i];
        bool there = path_exists(r), dir = path_is_dir(r), epi = path_ext(r) == ".epi";
        const char *icon = dir ? ICON_FOLDER : epi ? ICON_STACKS : ICON_DESCRIPTION;
        const char *kind = dir ? "DOS episode" : epi ? "Techbot episode" : "Techbot level";
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!there);
        if (ImGui::Selectable("##r", false, 0, ImVec2(rw, row_h))) open_path(r);
        bool hov = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        if (there && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", r.c_str());
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float a = there ? 1.0f : 0.5f, ty = p.y + (row_h - ImGui::GetTextLineHeight()) / 2;
        ImVec4 ic = there ? T.accent_text : T.muted;
        if (hov) ic = T.accent_text;
        float isz = font_icons->FontSize * io.FontGlobalScale, bsz = font_bold->FontSize * io.FontGlobalScale;   /* (fonts are made at the screen's own pixels) */
        dl->AddText(font_icons, isz, ImVec2(p.x + 12 * u, p.y + (row_h - isz) / 2),
                    ImGui::GetColorU32(ImVec4(ic.x, ic.y, ic.z, a)), icon);
        std::string name = path_name(r);
        float nx = p.x + 44 * u;
        dl->AddText(font_bold, bsz, ImVec2(nx, ty), ImGui::GetColorU32(ImVec4(T.text.x, T.text.y, T.text.z, a)), name.c_str());
        float kx = nx + std::max(ImGui::CalcTextSize(name.c_str()).x + 12 * u, rw * 0.28f);
        dl->AddText(ImVec2(kx, ty), ImGui::GetColorU32(ImVec4(T.muted.x, T.muted.y, T.muted.z, a)), there ? kind : "not found");
        float px = kx + ImGui::CalcTextSize("Techbot episode").x + 24 * u, pr = p.x + rw - 12 * u;
        std::string shown = fit_path(r, pr - px);
        dl->AddText(ImVec2(pr - ImGui::CalcTextSize(shown.c_str()).x, ty), ImGui::GetColorU32(ImVec4(T.faint.x, T.faint.y, T.faint.z, a)), shown.c_str());
        ImGui::PopID();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();
    content_h = ImGui::GetItemRectSize().y;
    if (!fits) ImGui::Dummy(ImVec2(1, margin));

    /* the choices */
    if (popup_begin("New level")) {
        ImGui::Text("An empty level: a small room in the middle of the map, with the player start and the exit door.");
        ImGui::Text("Art from");
        episode_choice(&new_ep);
        ImGui::Spacing();
        if (ImGui::Button("Create")) { ImGui::CloseCurrentPopup(); new_project(new_ep, false, false, 1); }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (popup_begin("New episode")) {
        ImGui::Text("Empty levels with a blue hallway between each. Levels can be added or removed later.");
        ImGui::SetNextItemWidth(120 * ui_scale);
        ImGui::InputInt("levels", &new_levels);
        new_levels = std::max(1, std::min(PROJ_MAX_LEVELS, new_levels));
        ImGui::Text("Art from");
        episode_choice(&new_ep);
        ImGui::Spacing();
        if (ImGui::Button("Create")) { ImGui::CloseCurrentPopup(); new_project(new_ep, false, true, new_levels); }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (popup_begin("Open a level")) {
        ImGui::Text("A stock level opens as a copy: saving asks where, as a .LEV.");
        for (int e = 1; e <= 3; e++) {
            if (!ep.available[e]) continue;
            ImGui::PushID(e);
            ImGui::Text("Episode %d", e);
            for (int k = 0; k < 10; k++) {
                char n[16];
                snprintf(n, sizeof n, "Level %d", k + 1);
                if (k % 5) ImGui::SameLine();
                if (ImGui::Button(n, ImVec2(86 * ui_scale, 0))) { ImGui::CloseCurrentPopup(); open_stock(e, false, k * 2); }
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (ImGui::Button("From a .LEV or .EPI file...")) {
            std::string f = dialog_open_project(last_dir, false);
            std::string err;
            if (f.empty()) say("Open: nothing chosen (on Linux this needs zenity)");
            else if (path_ext(f) == ".lev") { ImGui::CloseCurrentPopup(); open_path(f); }
            else if (!project_load_zip(picked, f, err)) say(err);
            else { ImGui::CloseCurrentPopup(); prompt_pick = true; }
        }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (prompt_pick) { ImGui::OpenPopup("Choose a level"); prompt_pick = false; }
    if (popup_begin("Choose a level")) {
        ImGui::Text("Which level of %s? It opens as a copy, saved as a .LEV.", picked.name.c_str());
        for (int i = 0; i < (int)picked.entries.size(); i++) {
            if (picked.entries[i].hallway) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(picked.label(i).c_str())) {
                Project p;
                p.ep = picked.ep;
                p.ending = picked.ep;
                p.author = picked.author;
                p.name = picked.name + ", " + picked.label(i);
                p.from_stock = true;
                p.entries.push_back(picked.entries[i]);
                ImGui::CloseCurrentPopup();
                use_project(p);
            }
            ImGui::PopID();
        }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (popup_begin("Open an episode")) {
        ImGui::Text("A stock episode opens as a copy, with a hallway of its own between each level. Saving asks where, as an .EPI.");
        for (int e = 1; e <= 3; e++) {
            char n[16];
            snprintf(n, sizeof n, "Episode %d", e);
            ImGui::BeginDisabled(!ep.available[e]);
            if (ImGui::Button(n, ImVec2(110 * ui_scale, 0))) { ImGui::CloseCurrentPopup(); open_stock(e, false, -1); }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("From an .EPI file...")) {
            std::string f = dialog_open_project(last_dir, true);
            if (f.empty()) say("Open: nothing chosen (on Linux this needs zenity)");
            else { ImGui::CloseCurrentPopup(); open_path(f); }
        }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (popup_begin("New DOS episode")) {
        ImGui::Text("Ten empty levels and the stock hallway, saved as that episode's own map files.");
        ImGui::Text("For");
        episode_choice(&dos_ep);
        ImGui::Spacing();
        if (ImGui::Button("Create")) { ImGui::CloseCurrentPopup(); new_project(dos_ep, true, true, 10); }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (popup_begin("Open a DOS episode")) {
        ImGui::Text("A stock episode opens as a copy: saving asks for a folder (never the game folder).");
        for (int e = 1; e <= 3; e++) {
            char n[16];
            snprintf(n, sizeof n, "Episode %d", e);
            ImGui::BeginDisabled(!ep.available[e]);
            if (ImGui::Button(n, ImVec2(110 * ui_scale, 0))) { ImGui::CloseCurrentPopup(); open_stock(e, true, -1); }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("From a folder...")) {
            std::string d = dialog_folder(last_dir, "Open a DOS episode folder");
            if (d.empty()) say("Open: nothing chosen (on Linux this needs zenity)");
            else { ImGui::CloseCurrentPopup(); open_path(d); }
        }
        popup_cancel();
        ImGui::EndPopup();
    }
    if (now_s < message_until) {                /* the status line has nowhere else to go here */
        ImGui::SetCursorPos(ImVec2(16 * ui_scale, ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - 6 * ui_scale));
        ImGui::TextColored(T.warn, "%s", message.c_str());
    }
    ImGui::End();
}

/* ================================================================ play-testing */
static bool solid_cell(int c, int r)
{
    return c < 0 || r < 0 || c >= MAP_W || r >= MAP_H || cell_kind(lv->after[r * MAP_W + c]) == CK_SOLID;
}

/* Run the real game on the level as it stands (Techbot's platform layer,
 * built in, takes over the window), then come back. */
/* Play the project in Techbot itself: written as a pack (a DOS episode as one
 * too, its hallway between each level) to Shrapnel's own folder, then
 * Techbot started on it.  Techbot keeps the pack's saves by its file name. */
static void play_in_techbot(void)
{
    if (!editing()) return;
    if (checks_stale()) {                       /* (on demand: the rules, at least, and a word if they found something) */
        recheck_all();
        if (project_broken()) { say("Note: the checks find something broken in this project (see Checks)."); drawer_open = true; drawer_tab = 0; }
    }
    sync_current();
    Project p = proj;
    if (p.dos) {                                /* ten levels, then the hallway: as the game plays them */
        std::vector<ProjEntry> e;
        const ProjEntry &hall = p.entries.back();
        for (int i = 0; i + 1 < (int)p.entries.size(); i++) {
            if (i) e.push_back(hall);
            e.push_back(p.entries[i]);
        }
        p.entries = e;
        p.dos = false;
        p.episode = true;
    }
    std::string stem = "Untitled";
    if (!proj.untitled()) {
        stem = path_name(proj.path);
        size_t dot = stem.rfind('.');
        if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
    }
    std::string path = path_join(pref_dir(), stem + (p.episode ? ".epi" : ".lev")), err;
    if (!project_save_zip(p, path, err, "", false)) { say("Couldn't write the level for Techbot: " + err); return; }
    char *base = SDL_GetBasePath();
    std::string exe = std::string(base ? base : "") +
#ifdef _WIN32
        "techbot.exe";
#else
        "techbot";
#endif
    SDL_free(base);
    if (!launch_program({ exe, "--dir", shrapnel_game_dir, "--pack", path }, err)) {
        say(err + " (Techbot needs to be in the same folder as Shrapnel)");
        return;
    }
    say("Starting Techbot on " + (proj.name.empty() ? stem : proj.name) + "...");
}

static void replay_clear(const Clear &cl, EdPlay &res);
static void run_play(void)
{
    int c = -1, r = -1;
    const Clear *watch = play_req.watch && !proj.entries[cur].clear.inputs.empty() ? &proj.entries[cur].clear : NULL;
    if (watch) play_req.from_start = true;
    if (!play_req.from_start) {
        c = hover_c; r = hover_r;
        if (c < 0) {                            /* from a button: the middle of the view */
            c = (int)((view.x + last_canvas.x / view.zoom / 2) / 16);
            r = (int)((view.y + last_canvas.y / view.zoom / 2) / 16);
        }
        c = std::max(1, std::min(MAP_W - 2, c));
        r = std::max(1, std::min(MAP_H - 2, r));
        while (r > 1 && solid_cell(c, r)) r--;          /* out of a wall */
        while (r < MAP_H - 2 && !solid_cell(c, r + 1)) r++;   /* down to the floor */
    } else {
        bool has_start = false;
        for (int i = 0; i < ED_MAP_CELLS; i++) if (lv->map[i] == 0x3032) has_start = true;
        if (!has_start) { say("This level has no player start: place one, or use F4 to play from the mouse."); return; }
    }
    EdLoadout lo = watch ? watch->lo : play_req.from_start ? EdLoadout{ 0, 0, 0, 0, 0, 1, 9, 0 } : loadout;
    static EdPlay res;
    PortConfig keep = cfg;                      /* play with the player's Techbot settings */
    FILE *f = fopen(game_path(PORT_CFG_FILE), "rb");
    if (f) { fclose(f); cfg_load(game_path(PORT_CFG_FILE)); }
    SDL_SetWindowTitle(main_win, "Shrapnel - play-testing (F4 to come back)");
    pf_sdl_play_begin(main_win, ren);
    if (watch) { ed_play_feed = watch->inputs.data(); ed_play_feed_n = (int)watch->inputs.size(); }
    for (;;) {
        ed_play(lv->slot, lv->map, c, r, &lo, &res);
        if (res.end != ED_STOP_RESTART) break;          /* F10: once more from the same place */
    }
    ed_play_feed = NULL;
    bool helped = ed_play_message != NULL;      /* F2 / F3 used */
    pf_sdl_play_end();
    PortConfig played = cfg;                    /* (what a clear needs to play the same again) */
    int played_wide = dl_wide, played_w = dl_view_w, played_h = dl_view_h;
    cfg = keep;
    lv->refresh();                              /* the editor's view of the game, back as it was */
    ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse();

    trail.clear();
    view.trail = true;                          /* (a new play-test: its path shows) */
    for (int i = 0; i < res.trail_n; i++) {     /* one point per place: standing still adds nothing */
        ImVec2 t(res.trail[i * 2], res.trail[i * 2 + 1]);
        if (trail.empty() || t.x != trail.back().x || t.y != trail.back().y) trail.push_back(t);
    }
    if (res.frames) pending_at_c = res.x / 16, pending_at_r = res.y / 16;   /* where Duke stopped */
    char t[200];
    const char *how = res.end == ED_END_EXIT ? "reached the exit" : res.end == ED_END_DIED ? "died" :
                      res.end == ED_END_BOSS ? "beat the boss" : res.end == ED_END_QUIT ? "quit the game" : "stopped";
    snprintf(t, sizeof t, "Duke %s after %.1f seconds%s.", how, res.frames / GAME_FPS,
             play_req.from_start && res.end == ED_END_EXIT ? ", starting from the player start" : "");
    last_play = t;
    if (watch) last_play = std::string("The recorded clear: ") + t;
    else if (play_req.from_start && (res.end == ED_END_EXIT || res.end == ED_END_BOSS) && !helped && res.inputs_n < ED_INPUTS) {
        Clear cl;                                       /* a clear: kept with the level */
        cl.inputs.assign(res.inputs, res.inputs + res.inputs_n);
        cl.lo = lo;
        cl.mode = played.mode; cl.smooth = played.smooth; cl.widescreen = played.widescreen; cl.hud = played.hud;
        cl.wide = played_wide; cl.view_w = played_w; cl.view_h = played_h;
        static EdPlay again;                            /* played again unseen at once: the same, every time */
        replay_clear(cl, again);
        if (again.end == res.end) {
            proj.entries[cur].clear = cl;
            proj.meta_dirty = true;
            clear_check.key = clear_key_now();
            clear_check.end = again.end; clear_check.x = again.x; clear_check.y = again.y; clear_check.frames = again.frames;
            clear_check.ok = true;
            checks_dirty = true;
            last_play += " Kept as this level's clear: if a change stops it working, Checks will say.";
        } else last_play += " (Not kept as the level's clear: played again, it came out differently - was the window resized?)";
    } else if (play_req.from_start && (res.end == ED_END_EXIT || res.end == ED_END_BOSS) && helped)
        last_play += " (Not kept as the level's clear: F2 or F3 helped.)";
    play_req.watch = false;
    say(last_play);
    if (res.end == ED_STOP_CLOSE) want_quit = true;     /* the window was closed during play */
}

/* the recorded clear, played again unseen on the level as it is now (after a
 * change, once the map has settled for a second) */
/* play a clear's controls on the level as it is now, unseen, with the
 * settings it was played with */
static void replay_clear(const Clear &cl, EdPlay &res)
{
    EdLoadout lo = cl.lo;
    PortConfig keep = cfg;
    int kw = dl_wide, kvw = dl_view_w, kvh = dl_view_h;
    int ka[4] = { port_act_left, port_act_right, port_act_up, port_act_down };
    FILE *f = fopen(game_path(PORT_CFG_FILE), "rb");
    if (f) { fclose(f); cfg_load(game_path(PORT_CFG_FILE)); }
    cfg.mode = cl.mode; cfg.smooth = cl.smooth; cfg.widescreen = cl.widescreen; cfg.hud = cl.hud;
    dl_set_view(cl.wide, cl.view_w, cl.view_h);
    ed_play_unseen = 1;
    ed_play_feed = cl.inputs.data();
    ed_play_feed_n = (int)cl.inputs.size();
    ed_play(lv->slot, lv->map, -1, -1, &lo, &res);
    ed_play_feed = NULL;
    ed_play_unseen = 0;
    cfg = keep;
    dl_set_view(kw, kvw, kvh);
    port_act_left = ka[0]; port_act_right = ka[1]; port_act_up = ka[2]; port_act_down = ka[3];
    lv->refresh();
}

/* the recorded clear, played again unseen on the level as it is now (after a
 * change, once the map has settled for a second) */
static void clear_tick(void)
{
    uint64_t k = clear_key_now();
    if (k != clear_seen_key) { clear_seen_key = k; clear_wait_until = now_s + 1.0; }
    if (!k || k == clear_check.key) return;
    if (checks_manual ? !check_req : now_s < clear_wait_until) return;
    static EdPlay res;
    replay_clear(proj.entries[cur].clear, res);
    clear_check.key = k;
    clear_check.end = res.end;
    clear_check.x = res.x; clear_check.y = res.y; clear_check.frames = res.frames;
    clear_check.ok = res.end == ED_END_EXIT || res.end == ED_END_BOSS;
    checks_dirty = true;
}

/* ================================================================ scripted input (test aid)
 * SHRAPNEL_EVENTS="F move X Y; F down L|R|M; F up L|R|M; F key NAME; F wheel N; ..."
 * F is the frame number; X, Y are window points; NAME is an SDL key name,
 * optionally prefixed with ctrl+ / shift+ / alt+ (e.g. "ctrl+z"). */
struct Scripted { int frame; std::string act, a, b; };
static std::vector<Scripted> script;
static int mouse_x, mouse_y;

static void parse_script(const char *s)
{
    while (s && *s) {
        const char *e = strchr(s, ';');
        std::string item(s, e ? (size_t)(e - s) : strlen(s));
        char act[32] = "", a[64] = "", b[64] = "";
        int f = 0;
        if (sscanf(item.c_str(), "%d %31s %63s %63s", &f, act, a, b) >= 2) script.push_back({ f, act, a, b });
        s = e ? e + 1 : NULL;
    }
}

static void scripted_input(int frame, SDL_Window *win)
{
    for (const Scripted &s : script) {
        if (s.frame != frame) continue;
        SDL_Event e;
        memset(&e, 0, sizeof e);
        Uint32 wid = SDL_GetWindowID(win);
        if (s.act == "move") {
            mouse_x = atoi(s.a.c_str()); mouse_y = atoi(s.b.c_str());
            e.type = SDL_MOUSEMOTION; e.motion.windowID = wid; e.motion.x = mouse_x; e.motion.y = mouse_y;
        } else if (s.act == "down" || s.act == "up") {
            e.type = s.act == "down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.windowID = wid; e.button.x = mouse_x; e.button.y = mouse_y; e.button.clicks = 1;
            e.button.button = s.a == "R" ? SDL_BUTTON_RIGHT : s.a == "M" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
            e.button.state = s.act == "down" ? SDL_PRESSED : SDL_RELEASED;
        } else if (s.act == "wheel") {
            e.type = SDL_MOUSEWHEEL; e.wheel.windowID = wid; e.wheel.y = atoi(s.a.c_str());
        } else if (s.act == "key") {
            std::string k = s.a;
            Uint16 mod = 0;
            for (;;) {
                if (k.compare(0, 5, "ctrl+") == 0) { mod |= KMOD_LCTRL; k = k.substr(5); }
                else if (k.compare(0, 6, "shift+") == 0) { mod |= KMOD_LSHIFT; k = k.substr(6); }
                else if (k.compare(0, 4, "alt+") == 0) { mod |= KMOD_LALT; k = k.substr(4); }
                else break;
            }
            SDL_Keycode kc = SDL_GetKeyFromName(k.c_str());
            SDL_SetModState((SDL_Keymod)mod);
            e.type = SDL_KEYDOWN; e.key.windowID = wid; e.key.state = SDL_PRESSED;
            e.key.keysym.sym = kc; e.key.keysym.scancode = SDL_GetScancodeFromKey(kc); e.key.keysym.mod = mod;
            SDL_PushEvent(&e);
            e.type = SDL_KEYUP; e.key.state = SDL_RELEASED;
            SDL_PushEvent(&e);
            SDL_SetModState(KMOD_NONE);
            continue;
        } else if (s.act == "close") {      /* the window's close button */
            e.type = SDL_WINDOWEVENT; e.window.windowID = wid; e.window.event = SDL_WINDOWEVENT_CLOSE;
            SDL_PushEvent(&e);
            e.type = SDL_QUIT;
        } else if (s.act == "mods") {       /* hold modifiers: "F mods shift" / "F mods none" */
            SDL_SetModState(s.a == "shift" ? KMOD_LSHIFT : s.a == "alt" ? KMOD_LALT : s.a == "ctrl" ? KMOD_LCTRL : KMOD_NONE);
            continue;
        } else continue;
        SDL_PushEvent(&e);
    }
}

/* ================================================================ main */
int main(int argc, char **argv)
{
    const char *shot = NULL, *dir = ".", *open_arg = NULL, *new_arg = NULL;
    const char *catalog = NULL;
    int want_ep = 1, want_slot = -1, run_selftest = 0, at_c = -1, at_r = -1, shot_at = 3, want_dos = 0, new_levels = 10;
    double want_zoom = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--episode") && i + 1 < argc) want_ep = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) want_slot = (atoi(argv[++i]) - 1) * 2;
        else if (!strcmp(argv[i], "--slot") && i + 1 < argc) want_slot = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--open") && i + 1 < argc) open_arg = argv[++i];
        else if (!strcmp(argv[i], "--new") && i + 1 < argc) new_arg = argv[++i];
        else if (!strcmp(argv[i], "--levels") && i + 1 < argc) new_levels = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dos")) want_dos = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--shot-at") && i + 1 < argc) shot_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest")) run_selftest = 1;
        else if (!strcmp(argv[i], "--catalog") && i + 1 < argc) catalog = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &at_c, &at_r);
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) want_zoom = atof(argv[++i]);
    }
    if (want_slot > 19) want_slot = -1;
    cfg_defaults();
    if (getenv("SHRAPNEL_PLAY_FRAMES")) ed_play_max_frames = atoi(getenv("SHRAPNEL_PLAY_FRAMES"));   /* test aid */
    snprintf(folder_buf, sizeof folder_buf, "%s", dir);
    parse_script(getenv("SHRAPNEL_EVENTS"));

    if (catalog) { shrapnel_game_dir = dir; return write_catalog(catalog); }   /* every tile and actor, no window */
    if (run_selftest) {                 /* every episode in the folder, no window */
        int fails = 0, any = 0;
        shrapnel_game_dir = dir;
        for (int e = 1; e <= 3; e++)
            if (ep.load(e)) {
                any = 1;
                if (getenv("SHRAPNEL_ONLY_REACH")) { fails |= selftest_reach(ep); continue; }
                fails |= getenv("SHRAPNEL_ONLY_CHECKS") ? 0 : selftest(ep) | selftest_projects(ep);
                fails |= selftest_checks(ep);
                if (!getenv("SHRAPNEL_ONLY_CHECKS") && !getenv("SHRAPNEL_SKIP_REACH")) fails |= selftest_reach(ep);
            }
        if (!any) { fprintf(stderr, "no DN1.EXE, DN2.EXE or DN3.EXE in %s\n", dir); return 2; }
        return fails;
    }

#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#ifdef _WIN32
    /* the icon in shrapnel.exe (shrapnel/resources/shrapnel.rc), at every size Windows wants */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    int win_w = 1500, win_h = 900;
    if (getenv("SHRAPNEL_WINDOW")) sscanf(getenv("SHRAPNEL_WINDOW"), "%dx%d", &win_w, &win_h);   /* (tests: another size) */
    SDL_Window *win = SDL_CreateWindow("Shrapnel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    main_win = win;
#ifndef _WIN32
    if (win) {                                  /* window icon: the 16x16 pixel art doubled, pixel for pixel */
        static uint32_t px[32 * 32];
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                const unsigned char *p = shrapnel_icon16 + ((y / 2) * 16 + x / 2) * 4;
                px[y * 32 + x] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
            }
        SDL_Surface *ic = SDL_CreateRGBSurfaceFrom(px, 32, 32, 32, 32 * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); }
    }
#endif
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : NULL;
    if (!ren && win) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
        return 1;
    }

    /* sharp text at any display scale: rasterise the font at the real pixel size */
    float fb_scale = 1;
    {
        int ww, wh, dw, dh;
        float ddpi = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GetRendererOutputSize(ren, &dw, &dh);
        if (ww > 0) fb_scale = (float)dw / ww;
        if (const char *f = getenv("SHRAPNEL_TEST_FBS")) fb_scale = (float)atof(f);   /* (test aid: a scaled display, on one that isn't; see below) */
        /* the system's own scaling, when it doesn't come through as more
           pixels: on Windows, the display's DPI is the scaling setting
           (96 = 100%); on Linux, the desktop's GDK_SCALE / GDK_DPI_SCALE if it
           sets them.  (Not the screen's physical DPI, which Linux reports: a
           1080p laptop screen is ~150 DPI but its desktop is at 100%.) */
#ifdef _WIN32
        if (fb_scale < 1.01f && SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &ddpi, NULL, NULL) == 0 && ddpi > 110)
            ui_scale = floorf(ddpi / 96 * 4 + 0.5f) / 4;      /* no OS scaling: scale the interface ourselves */
#elif !defined(__APPLE__)
        (void)ddpi;
        if (fb_scale < 1.01f) {
            float g = 1;
            if (const char *e = getenv("GDK_SCALE")) g *= std::max(1.0f, (float)atof(e));
            if (const char *e = getenv("GDK_DPI_SCALE")) g *= std::max(0.5f, (float)atof(e));
            ui_scale = floorf(g * 4 + 0.5f) / 4;
        }
#else
        (void)ddpi;
#endif
        if (const char *u = getenv("SHRAPNEL_UI_SCALE")) {     /* (test aid: screenshots at 200%) */
            ui_scale = (float)atof(u);
            SDL_SetWindowSize(win, (int)(1500 * ui_scale), (int)(900 * ui_scale));
        }
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;
    settings_load();                            /* (the theme, before the first frame) */
    ui_auto = ui_scale;
    ui_scale = ui_auto * ui_user;
    theme_fonts(ui_scale, fb_scale);
    theme_apply(ui_scale);
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);

    if (want_zoom > 0) view.zoom = want_zoom;
    clock_start = SDL_GetPerformanceCounter(); Uint64 last = clock_start;
    recent_load();
    if (try_folder(dir)) {
        if (open_arg) open_path(open_arg);
        else if (new_arg) new_project(want_ep, !strcmp(new_arg, "dos"), strcmp(new_arg, "level") != 0, new_levels);
        else if (want_slot >= 0)                /* --level / --slot: that level of a copy of the stock episode */
            open_stock(want_ep, want_dos, -1, want_dos ? (want_slot & 1 ? 10 : want_slot / 2) : want_slot);
        if (at_c >= 0 && at_r >= 0) { pending_at_c = at_c; pending_at_r = at_r; }
    }
    bool running = true;
    int frame = 0;
    while (running) {
        frame++;
        scripted_input(frame, win);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) want_quit = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) app_focused = false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) app_focused = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(win)) want_quit = true;
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        if (dt > 0.25) dt = 0.25;                /* after a play-test or a stall */
        now_s = clock_s();
        if (view.animate) {                     /* animated tiles step once per game frame */
            view.phase_time += dt;
            while (view.phase_time >= 1.0 / GAME_FPS) { view.phase_time -= 1.0 / GAME_FPS; view.phase = (view.phase + 1) & 3; }
        }
        autosave_tick();
        reach_tick();
        clear_tick();
        if (checks_dirty && editing() && (!checks_manual || check_req)) recheck_all();
        if (check_req && !reach_running && reach_current() && (!clear_key_now() || clear_check.key == clear_key_now()))
            check_req = false;                  /* (all three done) */
        if (checks_manual && editing()) {       /* now and then: a reminder */
            if (!checks_stale()) { stale_since = -1; stale_told = false; }
            else if (stale_since < 0) stale_since = now_s;
            else if (!stale_told && (now_s - stale_since > 300 || unchecked >= 40)) {
                say("The checks haven't run since your last changes: press Check (" + act_keys(A_CHECK) + ") to see where the level stands.");
                stale_told = true;
            }
        }

        if (ui_rescale) {                       /* View > Interface size: the fonts again, at the new size */
            ui_rescale = false;
            ui_scale = ui_auto * ui_user;
            ImGui::GetIO().Fonts->Clear();
            theme_fonts(ui_scale, fb_scale);
            ImGui_ImplSDLRenderer2_DestroyFontsTexture();
            theme_apply(ui_scale);
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        if (static const char *tf = getenv("SHRAPNEL_TEST_FBS"); tf) {   /* (the window's pixels as if the OS scaled them) */
            float f = (float)atof(tf);
            int dw, dh;
            SDL_GetRendererOutputSize(ren, &dw, &dh);
            io.DisplaySize = ImVec2(dw / f, dh / f);
            io.DisplayFramebufferScale = ImVec2(f, f);
            io.MousePos = ImVec2(io.MousePos.x / f, io.MousePos.y / f);
        }
        ImGui::NewFrame();
        for (int k = 0; k < N_SPOTS; k++) spot0[k] = spot1[k] = ImVec2(0, 0);   /* (marked again as drawn) */
        menu_bar();
        shortcuts();
        if (want_quit) {                        /* the close button, Alt+F4 or File > Quit */
            want_quit = false;
            if (!ImGui::IsPopupOpen("Unsaved changes")) guard_changes([] { quit_now = true; });
        }
        {
            float W = io.DisplaySize.x, H = io.DisplaySize.y, top = ImGui::GetFrameHeight();
            /* the side panels: as wide as they were dragged, leaving the map room */
            float lo_s = 200 * ui_scale, lo_r = 240 * ui_scale, map_min = 360 * ui_scale;
            float side = std::max(lo_s, std::min(side_w * ui_scale, W * 0.4f));
            float right = std::max(lo_r, std::min(right_w * ui_scale, W * 0.45f));
            if (W - side - right < map_min) {           /* (a narrow window: both give way) */
                float over = map_min - (W - side - right);
                float ts = side - lo_s, tr = right - lo_r;
                if (ts + tr > 0) { side -= over * ts / (ts + tr); right -= over * tr / (ts + tr); }
                side = std::max(side, lo_s); right = std::max(right, lo_r);
            }
            float tb = TB_BUTTON * ui_scale + 16 * ui_scale, status = ImGui::GetFrameHeight() + 6 * ui_scale;
            if (panels_hidden) side = right = 0;       /* (Tab: the map alone) */
            if (editing()) {
                float body = top + tb, body_h = H - body - status;
                {   /* the brush before this one, for X */
                    static uint16_t seen = ed.brush;
                    if (ed.brush != seen) { ed.prev_brush = seen; seen = ed.brush; }
                }
                toolbar_window(0, top, W, tb, side, right);
                if (!panels_hidden) sidebar(body, side, body_h);
                canvas(side, body, W - side - right, body_h);
                if (!panels_hidden) right_panel(W - right, body, right, body_h);
                status_bar(H - status, W, status);
                if (!panels_hidden) {
                    splitter("##split_l", side, body, body_h, &side_w, +1, SIDE_W_DEFAULT);
                    splitter("##split_r", W - right, body, body_h, &right_w, -1, RIGHT_W_DEFAULT);
                }
                note_window();
                import_window();
            } else { start_screen(top); }
            keys_window(&keys_open, ui_scale, settings_save);
        }
        draw_prompts();
        if (editing() && !tour_seen && tour_step < 0 && !keys_open &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            tour_seen = true;                   /* the first project opened: the tour, once */
            settings_save();
            tour_start();
        }
        tour();
        {
            char title[160];
            if (editing())
                snprintf(title, sizeof title, "%sShrapnel - %s - %s", proj_modified() ? "* " : "", proj.name.c_str(), proj.label(cur).c_str());
            else snprintf(title, sizeof title, "Shrapnel");
            SDL_SetWindowTitle(win, title);
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 20, 20, 26, 255);
        SDL_RenderClear(ren);
        /* with Windows display scaling the window is measured in points and
         * the screen in pixels: have SDL scale the drawing to match (the
         * ImGui backend scales only its clip rectangles itself) */
        SDL_RenderSetScale(ren, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        static int shot_every = getenv("SHRAPNEL_SHOT_EVERY") ? atoi(getenv("SHRAPNEL_SHOT_EVERY")) : 0;
        if (shot && shot_every > 0 && frame < shot_at && frame % shot_every == 0) {   /* (tests: a sequence, shot_NNNNN.bmp) */
            int w, h;
            char name[600];
            snprintf(name, sizeof name, "%s_%05d.bmp", shot, frame);
            SDL_GetRendererOutputSize(ren, &w, &h);
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (s && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) SDL_SaveBMP(s, name);
            SDL_FreeSurface(s);
        }
        if (shot && frame == shot_at) {
            int w, h;
            SDL_GetRendererOutputSize(ren, &w, &h);
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (s && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) SDL_SaveBMP(s, shot);
            SDL_FreeSurface(s);
            running = false;
        }
        SDL_RenderPresent(ren);
        if (quit_now) running = false;
        if (play_req.pending && editing() && !quit_now) { play_req.pending = false; run_play(); }
        else play_req.pending = false;
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
