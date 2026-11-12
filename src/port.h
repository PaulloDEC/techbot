/*
 * port.h - enhancements that live *around* the reconstructed game.
 *
 * Nothing here changes game rules: settings, the cursor-driven menu layer and
 * the input context the platform layer uses to translate controller buttons.
 * Original-mode gameplay stays bit-identical to DN1.EXE (see VERIFICATION.md).
 */
#ifndef DN1_PORT_H
#define DN1_PORT_H
#include <stdint.h>

/* ---------------------------------------------------------------- settings
 * Stored in TECHBOT.CFG in the game folder (plain "key = value" lines), so
 * the original's own .DN1 files are never touched by port options. */
enum { MODE_ORIGINAL = 0, MODE_ENHANCED = 1 };
enum { SCALE_SHARP = 0, SCALE_NEAREST = 1 };
enum { SND_ORIGINAL = 0, SND_OVERLAP = 1, SND_HIFI = 2 };   /* shown: Classic, Improved, Wacky */
enum { PAD_NONE = -1 };
/* Aspect - Classic: the original 4:3 picture; Wide: the picture follows the
 * window's shape.  HUD - Classic: the original border and status panel
 * (widened in Wide); Immersive: the play area is the whole picture. */
/* shown: Classic, Wide (Fill), Wide (16:9) - the menu steps Classic, 16:9, Fill */
enum { ASPECT_CLASSIC = 0, ASPECT_WIDE = 1, ASPECT_16_9 = 2 };
/* shown: Classic, Slim Overlay, Slim Fixed, Off */
enum { HUD_CLASSIC = 0, HUD_IMMERSIVE = 1, HUD_IMMERSIVE_ALT = 2, HUD_OFF = 3 };
/* (Immersive Alt: the same bar on a black band, the play area ending just
 * above it instead of running on behind it.  Off: Immersive with no bar.) */
enum { PADACT_UP, PADACT_DOWN, PADACT_LEFT, PADACT_RIGHT, PADACT_FIRE, PADACT_JUMP,
       PADACT_MENU, PADACT_BACK, PADACT_QSAVE, PADACT_QLOAD, PADACT_COUNT };

typedef struct {
    int mode;              /* MODE_ORIGINAL / MODE_ENHANCED (gameplay changes, later)  */
    int cursor_menus;      /* 1: arrow-and-confirm menus; 0: original key-letter menus */
    int scale;             /* initial window scale                                     */
    int scaling;           /* SCALE_SHARP (even pixel sizes) / SCALE_NEAREST           */
    int show_fps;          /* frame rate in the window title (testing aid)             */
    int fullscreen;
    int deadzone;          /* analogue stick deadzone, 0..32767                        */
    int pad[PADACT_COUNT]; /* SDL_GameControllerButton per action (PAD_NONE = unbound)  */
    int pad_alt[PADACT_COUNT]; /* optional second button per action                    */
    uint8_t key_qsave, key_qload; /* keyboard scancodes for quicksave / quickload        */
    uint8_t key_hud;       /* in play: HUD Off <-> Immersive (or Immersive Alt)           */
    int smooth;            /* Enhanced mode in-between frames: 0 off, 30/60/120 fps, -1 = display rate */
    int widescreen;        /* Enhanced mode aspect: ASPECT_CLASSIC / ASPECT_WIDE (fill) / ASPECT_16_9 */
    int hud;               /* Enhanced mode HUD: HUD_CLASSIC / HUD_IMMERSIVE / HUD_IMMERSIVE_ALT */
    int sound;             /* Enhanced mode: SND_ORIGINAL / SND_OVERLAP / SND_HIFI         */
    int episode;           /* 1..3: which DNx.EXE to start (the folder may hold all three) */
    int volume;            /* 0 = off .. 5 = full (Game setup > Sound volume)            */
} PortConfig;

extern PortConfig cfg;
/* Techbot - An Enhanced PC Port of Duke Nukem */
#define PORT_NAME      "Techbot"
#define PORT_FULL_NAME "Techbot - An Enhanced PC Port of Duke Nukem"
#define PORT_CFG_FILE  "TECHBOT.CFG"   /* (older builds: DN1PORT.CFG, read if this is missing) */
void cfg_defaults(void);                 /* the port's defaults (cursor menus on)    */
void cfg_default_pad(void);              /* controller bindings + deadzone only      */
void cfg_load(const char *path);         /* missing file: keep defaults, write one   */
void cfg_save(const char *path);
/* platform: save settings and start over with cfg.episode (Options > Episode) */
void pf_restart(void);
int  episode_available(int ep);          /* DNx.EXE present in the game folder */
extern const char *const padact_name[PADACT_COUNT];

/* ---------------------------------------------------------------- input context
 * Tells the platform layer what a controller button should mean right now:
 *   UI_CTX_GAME  - press the game's bound keys (KEYS.DN1), as a keyboard would
 *   UI_CTX_MENU  - up/down arrows, Enter = confirm, Esc = back
 *   UI_CTX_YESNO - confirm = Y, back = N (the original's yes/no prompts)
 * Keyboard input is never translated: the keys always do what they did. */
enum { UI_CTX_GAME = 0, UI_CTX_MENU = 1, UI_CTX_YESNO = 2,
       UI_CTX_CAPTURE = 3 /* remapping: controller buttons send nothing */ };
extern volatile int ui_context;

/* ---------------------------------------------------------------- input device
 * Which device was used last (set by the platform layer) and the controller's
 * button-label family, so prompts can name the right button. */
enum { DEV_KEYBOARD = 0, DEV_PAD = 1 };
enum { PAD_STYLE_XBOX = 0, PAD_STYLE_PLAYSTATION = 1, PAD_STYLE_NINTENDO = 2 };
extern volatile int input_device;
extern int pad_style;
/* game text about to be drawn: with a controller in use, "PRESS ENTER" and
 * the like name the controller's buttons instead (see port_labels.c) */
const char *port_relabel(const char *s);

/* ---------------------------------------------------------------- cursor menus
 * Every original menu draws its text, waits with ui_wait_cursor() and then
 * dispatches on last_scancode.  ui_menu_wait() is a drop-in for that wait:
 * with cursor menus on it draws the game's own spinning cursor beside the
 * selected option, moves it with up/down and, on confirm, leaves that
 * option's original hotkey in last_scancode - so the menu logic is unchanged
 * and the letter keys keep working. */
typedef struct { uint8_t col, row, key; } MenuItem;   /* byte column, text row, XT make code */

void ui_menu_wait(int wait_x, int wait_row, const MenuItem *items, int n, int *sel);
/* the same, with the main menu's idle timeout: returns 0 on timeout */
int  ui_menu_wait_idle(const MenuItem *items, int n, int *sel, long *idle, long limit);
/* the raw selection loop: returns with the chosen hotkey, or any other key
 * (e.g. left/right arrows), in last_scancode; *sel holds the cursor row */
void ui_select(const MenuItem *items, int n, int *sel);
/* Game setup > Sound volume (OFF, 1-4, MAX), replacing the sound toggle */
void ui_sound_volume(int x);
extern int port_last_volume;             /* last non-zero volume (in-game S toggle) */
extern int cfg_loaded;                   /* a settings file was read (not the test build) */
/* Options menu (Game Setup > Options) */
void ui_options(int x);
/* main menu > E)pisode select: returns 1..3, or 0 to keep the current one */
int  ui_episode_select(int x);
void port_switch_episode(int e);         /* saves, then restarts in episode e */
extern int port_start_at_menu;           /* set after a switch: skip the title wait */
extern int port_start_level;             /* --level: level table index to start at, or -1 */
int port_parse_level(const char *a, int raw);   /* --level N / --level-index I, -1 if bad */

/* port_state.c: in-memory snapshots of the whole game state */
typedef struct PortState PortState;
PortState *port_state_new(void);
void port_state_free(PortState *s);
void port_state_save(PortState *s);
void port_state_load(const PortState *s);
int  port_state_compare(const PortState *a, const PortState *b);   /* 0 = same */
uint64_t port_state_hash(const PortState *s);                      /* for tests  */
/* main.c: called once after start-up (graphics loaded, before the title);
 * port_restart() unwinds to the top of the title/play loop - restore a
 * snapshot first.  Play then starts afresh (at port_start_level, if set). */
extern void (*port_boot_hook)(void);
void port_restart(void);

/* port_editor.c: what the Shrapnel level editor needs from the game */
#define ED_MAP_CELLS 0x2D00              /* 128 x 90 */
#define ED_BD_W 208                      /* backdrop: 13 x 10 tiles */
#define ED_BD_H 160
#define ED_ACTOR_END 0x305E              /* actors: 3000h..305Dh (305Ah.. Techbot's loose items) */
#define ED_LOOSE_FIRST 0x305A            /* loose boots, claws, robot hand, access card */
#define ED_LOOK_DUKE 0x10000             /* ed_actor_look: keep Duke's own pieces too */
typedef struct {
    const uint8_t *src;                  /* a 16x16 masked sprite, or NULL: a tile */
    uint16_t tile;                       /* tile cache offset, when src is NULL */
    int dx, dy;                          /* pixels from the actor's cell */
} EdPiece;
extern const uint8_t *port_map_override;  /* load_level reads the map from here when set */
extern int port_god;                     /* editor play-test: no damage */
extern int port_techbot_rules;           /* the level loading is a Techbot project's (Shrapnel; packs need no flag) */
extern void (*dl_spy)(int type, const uint8_t *src, uint16_t tile, int col, int line);
int  ed_boot(void);
void ed_spin(void);
int  ed_read_map(const char *name, uint16_t map[ED_MAP_CELLS]);
#define ED_N_TABLES 42
int  ed_actor_limit(uint16_t id, int *cap);   /* table (1..41, 0 none) and its size */
void ed_load(int slot, const uint16_t *map);
extern int ed_core_used;                 /* bumped whenever the editor's calls reload or play the game */
void ed_map_after(uint16_t out[ED_MAP_CELLS]);
void ed_game_view(int sx, int sy, int anim, uint8_t out[320 * 200]);
void ed_tile(uint16_t t, uint8_t out[256]);
void ed_backdrop(int which, uint8_t *out);
const char *ed_stock_drop(int slot, int which);   /* its backdrop files' names */
const char *ed_stock_note(int slot, int k);   /* a slot's stock note, line k */
#define ED_NOTE_CHARS 23                 /* a note line, inside the game's box */
#define ED_NOTE_W 208                    /* ed_note_view: the box, in pixels */
#define ED_NOTE_H 64
void ed_note_view(const char *const lines[4], uint8_t *out);
void ed_sprite(const uint8_t *src, uint8_t out[256]);
void ed_palette(uint32_t rgb[16]);
int  ed_actor_look(uint16_t id, EdPiece *out, int max, uint16_t *cell_after);
int  ed_actor_contents(uint16_t id, EdPiece *out, int max);   /* what a crate holds */
void port_init_game(void);               /* main.c: the game's start-up (files, graphics, sound) */

/* play-testing in the editor (port_editor.c) */
typedef struct {
    int boots, claws, robohand, card;    /* 0/1 */
    int keys;                            /* bits: 1 red, 2 green, 4 blue, 8 pink */
    int firepower;                       /* 1..4 */
    int health;                          /* 1..9 */
    int god;                             /* no damage */
} EdLoadout;
enum { ED_END_NONE, ED_STOP_USER, ED_STOP_CLOSE, ED_STOP_RESTART, ED_STOP_EXIT,
       ED_END_EXIT, ED_END_DIED, ED_END_BOSS, ED_END_QUIT,
       ED_END_FEED,                      /* a replay's controls ran out */
       ED_END_STUCK };                   /* a replay waited for a key that will not come */
#define ED_INPUTS 65536                  /* a play's controls: an hour at 16 frames a second */
typedef struct {
    int end;                             /* how it ended (above) */
    int x, y;                            /* where Duke was, in level pixels (his feet, centre) */
    int frames;                          /* game frames played */
    int trail_n;                         /* his path, one point per frame (up to ED_TRAIL) */
    int16_t trail[2 * 4096];
    int inputs_n;                        /* the controls, a byte a frame (1 left 2 right 4 up */
    uint8_t inputs[ED_INPUTS];           /* 8 down 16 jump 32 fire), as the game read them */
} EdPlay;
#define ED_TRAIL 4096
/* play the map in slot's level-table place, Duke starting at cell (c, r)
 * (or the map's own start when c < 0), until the level is left, Duke dies,
 * or the editor stops it; the platform must be live (pf_sdl_play_begin) */
void ed_play(int slot, const uint16_t *map, int c, int r, const EdLoadout *lo, EdPlay *out);
/* a replay: ed_play takes Duke's controls from here (a byte a frame, as
 * EdPlay.inputs), and ends with ED_END_FEED when they run out.  With
 * ed_play_unseen set it needs no window: it runs as fast as it can, unseen. */
extern const uint8_t *ed_play_feed;
extern int ed_play_feed_n;
extern int ed_play_unseen;
/* loop.c: called just before Duke moves, each frame of play (or NULL) */
extern void (*port_frame_input)(void);
/* Packs (port_pack.c): levels and episodes made in Shrapnel (.LEV / .EPI) */
#define PACK_NOTE_W 40
typedef struct {
    int hallway;
    uint8_t *map;                        /* 5A00h bytes */
    char map_name[32];
    int backdrop;                        /* a level: the pair of stock level 1..10 of the episode */
    char drop[2][40];                    /* backdrop A / B: a game file or one in the pack ("" = from backdrop) */
    uint8_t *drop_data[2];               /* ... the pack's own picture */
    long drop_len[2];
    char note[4][PACK_NOTE_W + 1];       /* the hallway note / Dr. Proton screen */
    int has_note;
} PackEntry;
typedef struct {
    int on;
    char path[1024], stem[64], name[64], author[64];
    int episode;                         /* whose art: 1..3 */
    int classic;                         /* made for the DOS game's rules */
    int ending;                          /* the last level's exit: stock ending 1..3, 0 = ending_text */
    char ending_text[1024];
    int n;                               /* entries: level, hallway, level... */
    PackEntry *e;
} Pack;
extern Pack pack;
int  pack_open(const char *path, char *err, int errlen);
void pack_close(void);
const uint8_t *pack_map(int index);
int  pack_is_last(int index);
const char *pack_backdrop(int index, int which, const uint8_t **data, long *len);
const char (*pack_note(int index))[PACK_NOTE_W + 1];
const char *pack_file_name(const char *stock);   /* saves, scores, demos: kept apart for a pack */
int  strcasecmp_port(const char *a, const char *b);
int  strncasecmp_port(const char *a, const char *b, int n);
#define PACK_FOLDER "LEVELS"              /* in the game folder: the CUSTOM LEVEL list */
typedef struct { char path[1200], name[64], author[64]; int episode, levels; } PackInfo;
int  pack_list(PackInfo *out, int max);
void pf_restart_pack(const char *path);  /* restarts Techbot playing that pack */
void port_switch_pack(const char *path); /* main.c: saves, then that */
int  ui_pick_pack(int x);                /* port_options.c: the CUSTOM LEVEL list; 1 if a pack is open to play */
extern int pack_finished;                /* the pack's last level was left by its exit */
void pack_ending(void);                  /* frontend.c: the ending PACK.INI names */
void load_tiles_mem(const uint8_t *data, long len, uint16_t vram, int bytes);   /* files.c */
/* (Shrapnel) a level's backdrops A and B in place of its slot's: a game file
 * by name, or a backdrop file's bytes (data, len); none set = the slot's */
typedef struct { char name[40]; const uint8_t *data; long len; } PortDrop;
extern PortDrop port_drop[2];
extern uint8_t port_key_held[128];
extern int port_card_hold;
extern int port_text_tile;
void ui_cursor_hold(uint16_t from);      /* ui.c: the spinning cursor's least step */               /* ui_dialogs.c: the tile a text page (Ordering info...) is filled with, -1 none */              /* files.c: the loading cards' least time, in ticks */       /* input.c: keys down now, by scancode */

/* Reachability (port_reach.c): where Duke can get to in a map, found by
 * running the game's own movement code over every state he can be in */
#define ED_ROUTE 16384
enum { ED_REACH_OK, ED_REACH_NO_START, ED_REACH_TOO_BIG };
#define ED_REACH_BODY    1               /* cell: Duke can be here (and still finish) */
#define ED_REACH_TRAPPED 2               /* cell: Duke can be here only where there is no way on to the end */
#define ED_REACH_TOUCH   4               /* cell (an actor's): Duke can reach it */
#define ED_REACH_SHOT    8               /* cell: a breakable block the search took as shot away */
/* EdReach.gates: what Duke can get done somewhere on the way */
#define ED_GATE_BOOTS    0x0001
#define ED_GATE_CLAWS    0x0002
#define ED_GATE_HAND     0x0004
#define ED_GATE_CARD     0x0008          /* picked up the access card */
#define ED_GATE_CARDUSE  0x0010          /* used it in the card reader */
#define ED_GATE_REACTOR  0x0020          /* shot the reactor */
#define ED_GATE_WALKWAY  0x0040          /* used the hand reader */
#define ED_GATE_KEY(c)   (0x0100 << (c)) /* got the key: 0 red, 1 green, 2 blue, 3 pink */
#define ED_GATE_LOCK(c)  (0x1000 << (c)) /* opened that colour's lock */
#define ED_GATE_BRIDGE   0x30000         /* (the collapsing bridge's state) */
typedef struct {
    int boots, claws, robohand;          /* carried in from earlier levels */
    int max_nodes;                       /* 0: the default limit */
} EdReachOpts;
typedef struct {
    int status;                          /* ED_REACH_* */
    int nodes, edges;                    /* the size of the search */
    int goal;                            /* 1 the exit, 2 Dr. Proton, 0 neither */
    int finish_ok;                       /* the end can be reached */
    unsigned gates;                      /* everything done somewhere (W_* bits, port_reach.c) */
    int body_cells, trap_cells;
    uint8_t cell[ED_MAP_CELLS];          /* ED_REACH_* bits */
    uint16_t shot[ED_MAP_CELLS];         /* what the search took shooting to leave in a cell (0: unchanged) */
    int route_n;                         /* one way to the end: Duke's feet, a point a frame */
    int16_t route[2 * ED_ROUTE];
    int inputs_n;                        /* ... and the controls that play it, a byte a frame */
    uint8_t inputs[ED_ROUTE];            /* (1 left 2 right 4 up 8 down 16 jump 32 fire) */
} EdReach;
int  ed_reach(int slot, const uint16_t *map, const EdReachOpts *opt, EdReach *out);   /* all at once */
/* in slices: begin, run until it returns 1 (budget = states expanded per
 * call), then take the result and end (which frees the search) */
int  ed_reach_begin(int slot, const uint16_t *map, const EdReachOpts *opt);
int  ed_reach_run(int budget, int *states);
void ed_reach_result(EdReach *out);
void ed_reach_end(void);
/* nothing is written to the game folder while set (a play-test) */
extern int port_no_disk;
/* Shrapnel's movement search is running the game's movement code: no sound,
 * no dust, nothing drawn (files.c) */
extern int port_sim;
extern int port_boxes_open;              /* (Shrapnel self-test) boxed items start shot open */
void player_motion(void);                /* player.c: Duke's movement for one frame */
void player_climb_step(void);            /* player.c: one step of a ledge pull-up */
void recenter_view(void);                /* render.c */
int  can_walk_left(void);
int  can_walk_right(void);
int  head_clear(void);
int  feet_clear(void);
void ed_play_stop(int why);              /* platform: end the play-test now */
void ed_play_frame(void);                /* platform: once per game frame */
void ed_play_note(const char *what);     /* platform: a helper key was used */
extern const char *ed_play_message;      /* the last note, for the editor's status bar */
extern int ed_play_max_frames;           /* test aid: end a play-test after this many frames (0: never) */
/* quicksave / quickload (one slot per episode, TECHBOTn.QSV); called from the play loop */
void port_quicksave(void);
int  port_quickload(void);                        /* 1 if loaded */
int  port_qs_info(int *lvl_index, long *score);   /* is there a usable quicksave? */
const char *port_yesno_hint(void);                /* "A = YES  B = NO" with a controller, else NULL */
extern int port_resume_qs;                        /* load it as play starts       */
/* latched quicksave/quickload key press (set by the keyboard handler, taken by
 * the play loop): a press is never lost to a later key event in the same frame */
extern volatile uint8_t port_qs_request;
/* the HUD toggle key was pressed (latched like the quicksave keys) */
extern volatile uint8_t port_hud_request;
void port_toggle_hud(void);
/* save-slot picker replacing "which game (1-9)?": returns slot 1..9, 0 = cancel */
int  ui_pick_slot(int x, int saving);

/* ---------------------------------------------------------------- smooth motion
 * port_render.c: records what each game frame draws in the play window and
 * redraws the window between frames with positions interpolated, so motion
 * runs at the display's refresh rate while the game still updates at its
 * original ~16 Hz.  The original's own frame is shown at the end of each step. */
enum { DL_TILE, DL_SPRITE, DL_GLYPH, DL_GLYPH_OPAQUE, DL_MIRROR };
extern int dl_capturing, dl_in_world, dl_waiting;
extern uint8_t dl_group;
void dl_reset(void);                       /* level start, restore, quickload        */
void dl_begin_frame(void);
void dl_end_frame(void);
/* a dialog (box or text) was drawn during this frame; the rectangle is in
 * screen pixels and is what widescreen lays over its own picture */
void dl_mark_dirty(int x, int y, int w, int h);
/* the game drew in the play window outside a recorded frame (screen pixels) */
void dl_note_direct(int x, int y, int w, int h);
extern int port_in_play;                   /* inside the play loop (main.c)          */
/* the play screen is up (border, status panel, "Get ready") but no level
 * frame has been drawn yet: widescreen composes the wide layout anyway */
extern int port_level_intro;
/* a widescreen frame of the play screen has been shown (reset when it goes) */
extern int dl_wide_shown;
void dl_world(void);                       /* draw_world: camera + map cells         */
void dl_record(int type, const uint8_t *src, uint16_t tile, int col, int line, int back);
unsigned dl_serial(void);                  /* changes at every captured frame        */
/* redraw the play window of a 320x200 indexed image at 'alpha' (0 = previous
 * frame, 1 = current); returns 0 when the exact frame should be shown */
int  dl_render(double alpha, uint8_t *idx);
/* widescreen: render the play area (dl_view_w x 160) into a buffer of 'stride'
 * pixels per row starting at its top-left; backdrop-1 pixels are written as
 * index 16 (the platform layer stretches the backdrop separately).  Returns 0
 * if there is no frame, 1 if drawn, 3 if drawn and the frame had a dialog
 * (the caller then overlays the original view from EGA memory). */
int  dl_render_wide(double alpha, uint8_t *dst, int stride);
/* the level backdrop as 208x160 indices (cache 4000h), for stretching */
void dl_backdrop(uint8_t *out208x160);
int  dl_compose_wide(double alpha, int overlay, uint8_t *out, int w);
int  dl_active(void);
/* smooth motion draws the play screen itself: the just-finished frame (or
 * the one being recorded) is clean and can be shown interpolated */
int  dl_owns_screen(void);

/* ---------------------------------------------------------------- widescreen
 * The game keeps its own camera and 13-tile view.  In Enhanced widescreen the
 * display gets a wider play area (dl_view_w pixels instead of 208) centred on
 * the game's view and stopped at the map edges; the activity window widens
 * to match (port_act_left/right, in 8-px columns, used by offscreen()), and
 * actor sprites outside the original window are recorded for the renderer
 * but not written into EGA memory, which still provides the borders and the
 * status panel. */
extern int dl_wide, dl_view_w, dl_view_h, dl_immersive, dl_hud;
extern int port_act_left, port_act_right;     /* 8-px columns */
extern int port_act_up, port_act_down;        /* map units (1/8 px) */
extern int port_aud_left;                     /* the visible part of port_act_left (sounds) */
/* platform: the play area for this window - wide or immersive, view_w x view_h
 * pixels (Wide: x 160 between the borders; Immersive: x 200, the whole picture) */
void dl_set_view(int wide, int view_w, int view_h);
int  dl_view_offset(void);                /* x of the original 208-px view in the wide area */
int  dl_view_offset_y(void);              /* y of the original 160-line view in the area   */
int  dl_play_x0(void);                    /* where the play area starts in the picture     */
int  dl_play_y0(void);
int  dl_clip(int type, int x, int y, int wbytes);   /* 1: skip the EGA write */
int  dl_frame_ready(void);                /* a recorded frame exists to compose from */

/* ---------------------------------------------------------------- Immersive HUD
 * port_hud.c: the slim bar along the bottom of an Immersive picture, filled
 * from the original status panel in EGA memory ('base': the 320x200 page) */
void hud_draw(uint8_t *out, int w, const uint8_t *base);
int  hud_bar_top(void);                   /* first picture row the bar covers */
void dl_display_flipped(void);            /* ega.c: the page on screen changed */

/* ---------------------------------------------------------------- sound
 * port_sound.c: in Enhanced mode effects overlap (several voices) and can be
 * resynthesised ("hi-fi"); the original single-voice logic keeps running for
 * the game state, its speaker output replaced by the mixer. */
int  port_sound_mixing(void);              /* Enhanced + overlap/hi-fi selected      */
void port_sound_init(void);                /* after the banks load: classify effects  */
void port_sound_play(int n);               /* every effect the game starts            */
void port_sound_render(int16_t *out, int n, int rate);   /* audio thread             */
int  port_sound_kind(int e);               /* 0 sweep, 1 tone, 2 noise (tests)       */
int  port_sound_busy(void);                /* any voice still sounding (tests)       */
void pf_audio_lock(void);
void pf_audio_unlock(void);

/* ---------------------------------------------------------------- log
 * port_log.c: TECHBOT.LOG in the game folder (startup details, errors, crashes) */
void port_log_open(void);
void port_log(const char *fmt, ...);
void port_log_step(const char *what);      /* logs it and remembers it for a crash report */
extern const char *volatile port_step;
const char *port_log_path(void);

/* ---------------------------------------------------------------- replacement art
 * port_art.c: PNGs in <game folder>/widescreen/, named after an original
 * picture or backdrop file + ".png", shown instead in Enhanced widescreen. */
enum { ART_PICTURE = 0, ART_BACKDROP = 1 };
typedef struct {
    int w, h;                    /* art size in pixels                              */
    int game_pixels;             /* height a multiple of 200/160: 1.2x-tall pixels  */
    double aspect;               /* displayed width / height                        */
    uint32_t *orig;              /* ARGB as loaded                                  */
    uint32_t *pixels;            /* ARGB for the current palette (fades)            */
    unsigned serial;             /* changes whenever pixels change                  */
} Art;
const Art *art_get(const char *orig_name, int kind);   /* NULL: no replacement */
const char *art_backdrop_name(int which);              /* 0: level backdrop, 1: second */
extern int port_picture;                               /* picture on screen, or -1 */
const char *picture_name(int idx);                     /* files.c: "dn.dn1" etc.   */
extern const uint8_t *screen_img_get(int idx);         /* the original's 32000 bytes */
extern int dl_ext_bd2;                                 /* second backdrop drawn by the platform */
extern uint8_t ega_written[8000];                      /* page-0 bytes drawn since the picture */

#endif
