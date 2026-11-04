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
enum { SND_ORIGINAL = 0, SND_OVERLAP = 1, SND_HIFI = 2 };
enum { PAD_NONE = -1 };
/* Aspect - Classic: the original 4:3 picture; Wide: the picture follows the
 * window's shape.  HUD - Classic: the original border and status panel
 * (widened in Wide); Immersive: the play area is the whole picture. */
enum { ASPECT_CLASSIC = 0, ASPECT_WIDE = 1 };
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
    int widescreen;        /* Enhanced mode aspect: ASPECT_CLASSIC / ASPECT_WIDE             */
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
/* quicksave / quickload (one slot per episode, TECHBOTn.QSV); called from the play loop */
void port_quicksave(void);
void port_quickload(void);
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
