/*
 * pf_sdl.c - SDL2 platform layer: window, real-time 145 Hz timer, keyboard,
 * game controller and PC-speaker emulation.  This is the "faithful" build:
 * it shows the original 320x200 EGA output (scaled, 4:3 aspect).
 *
* Usage:  techbot [asp [g]] [--dir <game folder>] [--episode N] [--scale N] [--fullscreen]
 * The folder must contain the original files of at least one episode
 * (DN1.EXE + *.DN1, DN2.EXE + *.DN2, DN3.EXE + *.DN3).
 */
#include "dn1.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "icon_data.h"
#ifdef _WIN32
#include <process.h>                     /* _execv (episode select restarts) */
#include <windows.h>                     /* FreeConsole, crash handler, focus */
#include <SDL_syswm.h>
#else
#include <signal.h>                      /* crash report */
#include <unistd.h>                      /* execv */
#endif

int pf_headless = 0;
static void update_view(void);
int pf_joy_x, pf_joy_y;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pad;
static SDL_AudioDeviceID audio;
static uint64_t perf_freq, next_tick_at;
static double tick_len;             /* performance-counter units per PIT tick */
static uint64_t last_present;
static const char *gamedir = ".";
static volatile uint16_t spk_div;   /* 0 = silent */
static int audio_rate = 44100;
static int pad_buttons = 0xFF;

/* ------------------------------------------------------------ files */
const char *game_path(const char *name)
{
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s/%s", gamedir, name);
#ifndef _WIN32
    {   /* case-insensitive lookup on case-sensitive file systems */
        FILE *f = fopen(buf, "rb");
        if (f) { fclose(f); return buf; }
        {
            char low[256]; size_t i;
            for (i = 0; name[i] && i < sizeof low - 1; i++) low[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
            low[i] = 0;
            snprintf(buf, sizeof buf, "%s/%s", gamedir, low);
        }
    }
#endif
    return buf;
}

/* ------------------------------------------------------------ audio:
 * the PIT channel 2 square wave that drives the PC speaker */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    static double phase;
    int16_t *s = (int16_t *)stream;
    int n = len / 2, i;
    double freq = spk_div ? 1193182.0 / spk_div : 0.0;
    (void)ud;
    if (port_sound_mixing()) {
        port_sound_render(s, n, audio_rate);
        if (cfg.volume < 5) for (i = 0; i < n; i++) s[i] = (int16_t)(s[i] * cfg.volume / 5);
        return;
    }
    for (i = 0; i < n; i++) {
        if (freq <= 0.0) { s[i] = 0; continue; }
        phase += freq / audio_rate;
        if (phase >= 1.0) phase -= (double)(int)phase;
        s[i] = (int16_t)((phase < 0.5 ? 3000 : -3000) * cfg.volume / 5);
    }
}
void pf_speaker(uint16_t div) { spk_div = div; }
void pf_audio_lock(void)   { if (audio) SDL_LockAudioDevice(audio); }
void pf_audio_unlock(void) { if (audio) SDL_UnlockAudioDevice(audio); }

/* ------------------------------------------------------------ keyboard:
 * SDL scancodes -> PC/XT set-1 make codes, as seen by the INT 9 handler */
static uint8_t xt_code(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (uint8_t)(0x02 + (sc - SDL_SCANCODE_1));
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: return 0x01; case SDL_SCANCODE_0: return 0x0B;
    case SDL_SCANCODE_MINUS: return 0x0C; case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_BACKSPACE: return 0x0E; case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_E: return 0x12;
    case SDL_SCANCODE_R: return 0x13; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17; case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19; case SDL_SCANCODE_LEFTBRACKET: return 0x1A; case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1C;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
    case SDL_SCANCODE_A: return 0x1E; case SDL_SCANCODE_S: return 0x1F; case SDL_SCANCODE_D: return 0x20;
    case SDL_SCANCODE_F: return 0x21; case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23;
    case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25; case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_SEMICOLON: return 0x27; case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2A; case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_Z: return 0x2C; case SDL_SCANCODE_X: return 0x2D; case SDL_SCANCODE_C: return 0x2E;
    case SDL_SCANCODE_V: return 0x2F; case SDL_SCANCODE_B: return 0x30; case SDL_SCANCODE_N: return 0x31;
    case SDL_SCANCODE_M: return 0x32; case SDL_SCANCODE_COMMA: return 0x33; case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35; case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38; case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_F1: return 0x3B; case SDL_SCANCODE_F2: return 0x3C; case SDL_SCANCODE_F3: return 0x3D;
    case SDL_SCANCODE_F4: return 0x3E; case SDL_SCANCODE_F5: return 0x3F; case SDL_SCANCODE_F6: return 0x40;
    case SDL_SCANCODE_F7: return 0x41; case SDL_SCANCODE_F8: return 0x42; case SDL_SCANCODE_F9: return 0x43;
    case SDL_SCANCODE_F10: return 0x44;
    case SDL_SCANCODE_HOME: return 0x47; case SDL_SCANCODE_UP: return 0x48; case SDL_SCANCODE_PAGEUP: return 0x49;
    case SDL_SCANCODE_LEFT: return 0x4B; case SDL_SCANCODE_RIGHT: return 0x4D; case SDL_SCANCODE_END: return 0x4F;
    case SDL_SCANCODE_DOWN: return 0x50; case SDL_SCANCODE_PAGEDOWN: return 0x51;
    case SDL_SCANCODE_INSERT: return 0x52; case SDL_SCANCODE_DELETE: return 0x53;
    /* numeric keypad: on an XT keyboard these share codes with the cursor keys */
    case SDL_SCANCODE_KP_7: return 0x47; case SDL_SCANCODE_KP_8: return 0x48; case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_MINUS: return 0x4A; case SDL_SCANCODE_KP_4: return 0x4B; case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_KP_6: return 0x4D; case SDL_SCANCODE_KP_PLUS: return 0x4E; case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_KP_2: return 0x50; case SDL_SCANCODE_KP_3: return 0x51; case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53; case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45; case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    default: return 0;
    }
}

/* ------------------------------------------------------------ controller:
 * buttons become the keys a keyboard player would press, chosen by the
 * current ui_context (port.h).  Each source remembers the scancode it sent
 * on press, so its release always lets go of the same key even if a menu
 * opened or closed in between. */
enum { SRC_STICK = SDL_CONTROLLER_BUTTON_MAX, SRC_COUNT = SRC_STICK + 4 };
static uint8_t src_sent[SRC_COUNT];
#define SWALLOWED 0xFF                  /* pressed during remapping: send nothing until released */
static uint8_t btn_prev[SDL_CONTROLLER_BUTTON_MAX];
static int captured = -1;
int pf_pad_connected(void) { return pad != NULL; }
int pf_pad_capture(void) { int b = captured; captured = -1; return b; }

static int action_of(int b)
{
    int a;
    for (a = 0; a < PADACT_COUNT; a++) if (cfg.pad[a] == b || cfg.pad_alt[a] == b) return a;
    return -1;
}

static uint8_t code_for(int src)
{
    int act;
    if (ui_context == UI_CTX_CAPTURE) return 0;     /* remapping screen: buttons are read raw */
    act = src >= SRC_STICK ? PADACT_UP + (src - SRC_STICK) : action_of(src);
    if (ui_context == UI_CTX_GAME) {
        if (act >= PADACT_UP && act <= PADACT_JUMP) return key_bind[act];
        if (act == PADACT_MENU) return 0x3B;               /* F1: the in-game menu */
        if (act == PADACT_BACK) return 0x01;               /* Esc                  */
        if (act == PADACT_QSAVE) return cfg.key_qsave;
        if (act == PADACT_QLOAD) return cfg.key_qload;
        return 0;
    }
    /* menus: fixed south = confirm, east = back, as on every modern console */
    if (src == SDL_CONTROLLER_BUTTON_A) return ui_context == UI_CTX_YESNO ? 0x15 : 0x1C;  /* Y / Enter */
    if (src == SDL_CONTROLLER_BUTTON_B || act == PADACT_MENU || act == PADACT_BACK)
        return ui_context == UI_CTX_YESNO ? 0x31 : 0x01;                                  /* N / Esc   */
    if (src == SDL_CONTROLLER_BUTTON_DPAD_UP    || act == PADACT_UP)    return 0x48;
    if (src == SDL_CONTROLLER_BUTTON_DPAD_DOWN  || act == PADACT_DOWN)  return 0x50;
    if (src == SDL_CONTROLLER_BUTTON_DPAD_LEFT  || act == PADACT_LEFT)  return 0x4B;
    if (src == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || act == PADACT_RIGHT) return 0x4D;
    return 0;
}

static void pad_update(void)
{
    int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    int dz = cfg.deadzone, s;
    int stick[4];
    stick[0] = ay < -dz; stick[1] = ay > dz; stick[2] = ax < -dz; stick[3] = ax > dz;
    for (s = 0; s < SRC_COUNT; s++) {
        int down = s >= SRC_STICK ? stick[s - SRC_STICK]
                                  : SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)s);
        if (s < SRC_STICK) {                        /* newest press, for the remapping screen */
            if (down && !btn_prev[s]) captured = s;
            btn_prev[s] = (uint8_t)down;
        }
        if (down && !src_sent[s]) {
            uint8_t c = code_for(s);
            input_device = DEV_PAD;
            if (ui_context == UI_CTX_CAPTURE) src_sent[s] = SWALLOWED;   /* taken by the remap screen */
            else if (c) { src_sent[s] = c; keyboard_isr(c); }
        } else if (!down && src_sent[s]) {
            /* a press swallowed by the remap screen must not reach the menu
             * it returns to, even while the button is still held */
            if (src_sent[s] != SWALLOWED) keyboard_isr((uint8_t)(src_sent[s] | 0x80));
            src_sent[s] = 0;
        }
    }
    /* the original's analogue-joystick mode (Game setup > Joystick) */
    pf_joy_x = stick[2] || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? -1 :
               stick[3] || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 1 : 0;
    pf_joy_y = stick[0] || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) ? -1 :
               stick[1] || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 1 : 0;
    pad_buttons = 0xFF;
    if (joystick_mode) {        /* only then do the game's joystick paths read buttons */
        if (cfg.pad[PADACT_FIRE] >= 0 && SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)cfg.pad[PADACT_FIRE])) pad_buttons &= ~0x10;
        if (cfg.pad[PADACT_JUMP] >= 0 && SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)cfg.pad[PADACT_JUMP])) pad_buttons &= ~0x20;
    }
}

/* button-label family for on-screen prompts (port_labels.c) */
static void pad_detect_style(void)
{
    pad_style = PAD_STYLE_XBOX;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    if (pad) {
        switch (SDL_GameControllerGetType(pad)) {
        case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
        case SDL_CONTROLLER_TYPE_PS5:
#endif
            pad_style = PAD_STYLE_PLAYSTATION; break;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
            pad_style = PAD_STYLE_NINTENDO; break;
        default: break;
        }
    }
#endif
}

static void poll_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: pf_exit(0); break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            uint8_t c;
            if (e.key.repeat) break;
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                Uint32 fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                cfg.fullscreen = !fs;
                break;
            }
            c = xt_code(e.key.keysym.scancode);
            if (c && e.type == SDL_KEYDOWN) input_device = DEV_KEYBOARD;
            if (c) keyboard_isr(e.type == SDL_KEYDOWN ? c : (uint8_t)(c | 0x80));
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) update_view();
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!pad) { pad = SDL_GameControllerOpen(e.cdevice.which); pad_detect_style(); }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                int k;
                for (k = 0; k < SRC_COUNT; k++)      /* let go of anything it was holding */
                    if (src_sent[k]) { if (src_sent[k] != SWALLOWED) keyboard_isr((uint8_t)(src_sent[k] | 0x80)); src_sent[k] = 0; }
                SDL_GameControllerClose(pad); pad = NULL; input_device = DEV_KEYBOARD;
            }
            break;
        default: break;
        }
    }
    if (pad) pad_update();
}
int pf_joystick_buttons(void) { return pad_buttons; }

/* ------------------------------------------------------------ video
 * The picture is W x 200 indexed pixels (W = 320, or wider in Enhanced
 * widescreen), shown with the original's tall pixels (200 lines -> 240 units)
 * and fitted into the window with black bars where the shapes differ.  In
 * widescreen the level backdrop is a second texture stretched by the GPU
 * under the play area; play-area pixels showing backdrop are transparent. */
#define MAX_W 640
static int display_hz = 60;         /* the display's refresh rate            */
static uint64_t present_started;    /* when the last present began (frame cap) */
static int present_pending;         /* a present was held back by the cap     */
static SDL_Texture *bdtex;           /* the backdrop, 208 x 160               */
static SDL_Texture *sharp_tex;       /* sharp scaling: whole-number enlargement */
static int sharp_w, sharp_h, sharp_ok = 1;
static int out_w = 320;              /* width of the picture being shown       */

/* Layout: how a picture W x 200 maps onto the window - as large as fits,
 * with the original's tall pixels (200 lines -> 240 units).  px/py are the
 * size of one game pixel in screen pixels (usually not whole numbers). */
typedef struct { int w; double px, py; SDL_Rect dst; } Layout;

/* Enhanced mode: the picture follows the window's shape (Aspect = Wide) /
 * the play area is the whole picture (HUD = Immersive) - either or both */
static int want_wide(void) { return cfg.widescreen && cfg.mode == MODE_ENHANCED; }
static int want_imm(void) { return cfg.hud != HUD_CLASSIC && cfg.mode == MODE_ENHANCED; }
/* the play area's height in an Immersive picture: all 200 lines, or (Alt)
 * down to one line above the HUD bar */
static int imm_h(void) { return cfg.hud == HUD_IMMERSIVE_ALT ? hud_bar_top() : 200; }

/* the layout for this window; w = 0 picks the width (widescreen follows the
 * window's shape) */
static Layout layout(int w)
{
    Layout L;
    int dw = 0, dh = 0;
    double s;
    SDL_GetRendererOutputSize(ren, &dw, &dh);
    if (dw <= 0 || dh <= 0) { dw = 320; dh = 240; }
    if (!w) w = want_wide() ? (int)(240.0 * dw / dh) & ~7 : 320;
    if (w < 320) w = 320;
    if (w > MAX_W) w = MAX_W;
    s = (double)dw / w < (double)dh / 240.0 ? (double)dw / w : (double)dh / 240.0;
    L.px = s; L.py = 1.2 * s;
    L.w = w;
    L.dst.w = (int)(w * L.px + 0.5); L.dst.h = (int)(200 * L.py + 0.5);
    L.dst.x = (dw - L.dst.w) / 2;     L.dst.y = (dh - L.dst.h) / 2;
    return L;
}

static int wide_width(void) { return want_wide() ? layout(0).w : 320; }
/* the play area's width in a picture w wide: between the border and the status
 * panel, or (immersive) all of it */
static int play_w(int w) { return want_imm() ? w : w - 112; }
static void update_view(void)
{
    int w = wide_width();
    if (want_imm()) dl_set_view(1, w, imm_h());
    else dl_set_view(w > 320, w - 112, 160);
}
/* the play area in the picture: x, y, width, height */
static void play_rect(int w, int *x, int *y, int *pw, int *ph)
{
    if (want_imm()) { *x = 0; *y = 0; *pw = w; *ph = imm_h(); }
    else { *x = 16; *y = 16; *pw = w - 112; *ph = 160; }
}

/* optional frame-rate readout in the window title (show_fps = 1) */
static void count_frame(void)
{
    static uint64_t t0; static int frames;
    uint64_t now = SDL_GetPerformanceCounter();
    if (!cfg.show_fps) return;
    if (!t0) t0 = now;
    frames++;
    if (now - t0 >= perf_freq) {
        char title[96];
        snprintf(title, sizeof title, PORT_FULL_NAME " - %.1f fps", frames * (double)perf_freq / (double)(now - t0));
        SDL_SetWindowTitle(win, title);
        t0 = now; frames = 0;
    }
}

/* ------------------------------------------------------------ replacement art
 * (port_art.c finds and decodes it; here it becomes textures) */
typedef struct { SDL_Texture *t; const Art *a; unsigned serial; int w, h; } ArtTex;
static ArtTex at_bd[2], at_pic;
static const Art *bd_art[2];        /* this frame's backdrop replacements (or NULL) */
static const Art *pic_art;          /* full-screen picture replacement being shown  */
static SDL_Texture *mask_tex[2], *bd_layer[2];
static int bd_layer_w, bd_layer_h;

static SDL_Texture *art_texture(ArtTex *x, const Art *a)
{
    if (x->t && (x->w != a->w || x->h != a->h)) { SDL_DestroyTexture(x->t); x->t = NULL; }
    if (!x->t) {
        x->t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, a->w, a->h);
        if (!x->t) return NULL;
        x->w = a->w; x->h = a->h; x->a = NULL;
        SDL_SetTextureBlendMode(x->t, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        /* pixel art stays pixel-sharp; high-resolution art is smoothed */
        SDL_SetTextureScaleMode(x->t, a->game_pixels ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#endif
    }
    if (x->a != a || x->serial != a->serial) {
        SDL_UpdateTexture(x->t, NULL, a->pixels, a->w * 4);
        x->a = a; x->serial = a->serial;
    }
    return x->t;
}

/* the part of the art that fills an area of the given shape (displayed
 * width / height): the middle, cropped evenly at the sides or top/bottom */
static SDL_Rect cover_src(const Art *a, double area_aspect)
{
    SDL_Rect r;
    r.x = 0; r.y = 0; r.w = a->w; r.h = a->h;
    if (a->aspect > area_aspect) { r.w = (int)(a->w * area_aspect / a->aspect + 0.5); r.x = (a->w - r.w) / 2; }
    else                         { r.h = (int)(a->h * a->aspect / area_aspect + 0.5); r.y = (a->h - r.h) / 2; }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    return r;
}

/* the play area's backdrop(s) into 'bd' on the current render target.
 * Index 16 in the frame shows the level backdrop, 17 the second one (only
 * used when the second one is replacement art).  Two backdrops: each is drawn
 * into its own layer, multiplied by a mask of where it shows (MOD), and the
 * two layers are added - plain blend modes every renderer supports. */
static void draw_backdrops(const SDL_Rect *bd, int w, const uint8_t *idx)
{
    static uint32_t mpix[2][MAX_W * 200];
    SDL_Texture *t[2], *prev;
    SDL_Rect s[2], full, msrc;
    int k, x, y, x0, y0, pw, ph;
    double area;
    play_rect(w, &x0, &y0, &pw, &ph);
    area = pw / (ph * 1.2);                     /* displayed shape: lines are 1.2 tall */
    t[0] = bd_art[0] ? art_texture(&at_bd[0], bd_art[0]) : NULL;
    if (t[0]) s[0] = cover_src(bd_art[0], area);
    else { t[0] = bdtex; s[0].x = 0; s[0].y = 0; s[0].w = 208; s[0].h = 160; }
    t[1] = dl_ext_bd2 && bd_art[1] ? art_texture(&at_bd[1], bd_art[1]) : NULL;
    if (!t[1]) { SDL_RenderCopy(ren, t[0], &s[0], bd); return; }
    s[1] = cover_src(bd_art[1], area);
    /* masks: white where each backdrop shows, black elsewhere */
    for (k = 0; k < 2; k++) {
        if (!mask_tex[k]) {
            mask_tex[k] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, MAX_W, 200);
            if (!mask_tex[k]) { SDL_RenderCopy(ren, t[0], &s[0], bd); return; }
            SDL_SetTextureBlendMode(mask_tex[k], SDL_BLENDMODE_MOD);
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(mask_tex[k], SDL_ScaleModeNearest);
#endif
        }
    }
    for (y = 0; y < ph; y++)
        for (x = 0; x < pw; x++) {
            uint8_t c = idx[(y0 + y) * w + x0 + x];
            mpix[0][y * pw + x] = c == 16 ? 0xFFFFFFFFu : 0xFF000000u;
            mpix[1][y * pw + x] = c == 17 ? 0xFFFFFFFFu : 0xFF000000u;
        }
    msrc.x = 0; msrc.y = 0; msrc.w = pw; msrc.h = ph;
    for (k = 0; k < 2; k++) SDL_UpdateTexture(mask_tex[k], &msrc, mpix[k], pw * 4);
    if (!bd_layer[0] || bd->w > bd_layer_w || bd->h > bd_layer_h) {
        for (k = 0; k < 2; k++) { if (bd_layer[k]) SDL_DestroyTexture(bd_layer[k]); bd_layer[k] = NULL; }
        bd_layer_w = bd->w > bd_layer_w ? bd->w : bd_layer_w; bd_layer_h = bd->h > bd_layer_h ? bd->h : bd_layer_h;
        for (k = 0; k < 2; k++)
            bd_layer[k] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, bd_layer_w, bd_layer_h);
    }
    if (!bd_layer[0] || !bd_layer[1]) { SDL_RenderCopy(ren, t[0], &s[0], bd); return; }
    prev = SDL_GetRenderTarget(ren);
    full.x = 0; full.y = 0; full.w = bd->w; full.h = bd->h;
    for (k = 0; k < 2; k++) {
        SDL_SetRenderTarget(ren, bd_layer[k]);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, t[k], &s[k], &full);
        SDL_RenderCopy(ren, mask_tex[k], &msrc, &full);
    }
    SDL_SetRenderTarget(ren, prev);
    SDL_SetTextureBlendMode(bd_layer[0], SDL_BLENDMODE_NONE);
    SDL_RenderCopy(ren, bd_layer[0], &full, bd);
    SDL_SetTextureBlendMode(bd_layer[1], SDL_BLENDMODE_ADD);
    SDL_RenderCopy(ren, bd_layer[1], &full, bd);
}

/* a full-screen picture's replacement, behind the 4:3 frame: as tall as the
 * frame and as wide as its shape makes it (filling the window's last few
 * pixels when it is within 2% of doing so), cropped to the window */
static void draw_picture_art(const Layout *L)
{
    SDL_Texture *t = art_texture(&at_pic, pic_art);
    SDL_Rect src, dst;
    int dw = 0, dh = 0;
    double wd;
    if (!t) return;
    SDL_GetRendererOutputSize(ren, &dw, &dh);
    src.x = 0; src.y = 0; src.w = pic_art->w; src.h = pic_art->h;
    dst.y = L->dst.y; dst.h = L->dst.h;
    wd = pic_art->aspect * dst.h;                            /* (dst is in displayed proportions) */
    if (wd < dw && wd > dw * 0.98) wd = dw;
    if (wd > dw) {                                          /* wider than the window: crop */
        src.w = (int)(pic_art->w * dw / wd + 0.5); src.x = (pic_art->w - src.w) / 2;
        dst.x = 0; dst.w = dw;
    } else { dst.w = (int)(wd + 0.5); dst.x = (dw - dst.w) / 2; }
    SDL_RenderCopy(ren, t, &src, &dst);
}

/* the window around the picture: black, except in Enhanced widescreen, where
 * it takes palette slot 0 - the game's background colour - so palette effects
 * such as the white flash before a level fill the whole screen */
static void clear_outside(void)
{
    uint32_t c = want_wide() || want_imm() ? ega_rgb(ega_palette[0]) : 0;
    SDL_SetRenderDrawColor(ren, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, 255);
    SDL_RenderClear(ren);
}

static void show_indexed(const uint8_t *idx, int w, const uint8_t *backdrop)
{
    static uint32_t pix[MAX_W * 200], bpix[208 * 160];
    uint32_t pal[17];
    SDL_Rect src = { 0, 0, w, 200 };
    Layout L = layout(w);
    int i;
    present_started = SDL_GetPerformanceCounter();
    present_pending = 0;
    for (i = 0; i < 16; i++) pal[i] = 0xFF000000u | ega_rgb(ega_palette[i]);
    pal[16] = 0;                                   /* transparent: backdrop shows */
    for (i = 0; i < w * 200; i++) pix[i] = idx[i] >= 16 ? 0 : pal[idx[i]];   /* 16, 17: backdrops / picture art */
    SDL_UpdateTexture(tex, &src, pix, w * 4);
    if (backdrop) {
        for (i = 0; i < 208 * 160; i++) bpix[i] = pal[backdrop[i] & 15];
        SDL_UpdateTexture(bdtex, NULL, bpix, 208 * 4);
    }
    {
        /* Sharp: enlarge by whole numbers first (nearest neighbour, so every
         * game pixel becomes an identical block), then shrink smoothly to the
         * exact size.  Every pixel ends up the same size; only the boundary
         * between two pixels is softened, by less than one screen pixel.
         * Nearest: straight to the window (sizes vary by a screen pixel). */
        int kx = (int)ceil(L.px - 1e-6), ky = (int)ceil(L.py - 1e-6);
        int sharp = cfg.scaling == SCALE_SHARP && sharp_ok &&
                    (fabs(L.px - kx) > 1e-6 || fabs(L.py - ky) > 1e-6);
        SDL_Rect area, bd;
        if (sharp) {
            int tw = w * kx, th = 200 * ky;
            if (!sharp_tex || tw > sharp_w || th > sharp_h) {
                if (sharp_tex) SDL_DestroyTexture(sharp_tex);
                sharp_w = tw > sharp_w ? tw : sharp_w; sharp_h = th > sharp_h ? th : sharp_h;
                sharp_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, sharp_w, sharp_h);
                if (sharp_tex) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
                    SDL_SetTextureScaleMode(sharp_tex, SDL_ScaleModeLinear);
#endif
                } else sharp_ok = 0;
            }
            if (sharp_tex && SDL_SetRenderTarget(ren, sharp_tex) == 0) {
                area.x = 0; area.y = 0; area.w = tw; area.h = th;
                /* with picture art behind, the frame's unchanged pixels stay transparent */
                SDL_SetRenderDrawColor(ren, 0, 0, 0, pic_art ? 0 : 255);
                SDL_RenderClear(ren);
                if (backdrop) {
                    int x0, y0, pw, ph;
                    play_rect(w, &x0, &y0, &pw, &ph);
                    bd.x = x0 * kx; bd.y = y0 * ky; bd.w = pw * kx; bd.h = ph * ky;
                    draw_backdrops(&bd, w, idx);
                }
                SDL_RenderCopy(ren, tex, &src, &area);
                SDL_SetRenderTarget(ren, NULL);
                clear_outside();
                if (pic_art) draw_picture_art(&L);
                SDL_SetTextureBlendMode(sharp_tex, pic_art ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                SDL_RenderCopy(ren, sharp_tex, &area, &L.dst);
            } else sharp = 0, sharp_ok = 0;
        }
        if (!sharp) {
            clear_outside();
            if (pic_art) draw_picture_art(&L);
            if (backdrop) {
                int x0, y0, pw, ph;
                play_rect(w, &x0, &y0, &pw, &ph);
                bd.x = L.dst.x + (int)(x0 * L.px + 0.5); bd.y = L.dst.y + (int)(y0 * L.py + 0.5);
                bd.w = (int)(pw * L.px + 0.5);           bd.h = (int)(ph * L.py + 0.5);
                draw_backdrops(&bd, w, idx);
            }
            SDL_RenderCopy(ren, tex, &src, &L.dst);
        }
    }
    {   /* test aid: TECHBOT_SHOT=<prefix> TECHBOT_SHOT_EVERY=<n> saves every n-th
         * presented picture as <prefix>_NNNNN.bmp */
        static int checked, every; static const char *pfx; static long n;
        if (!checked) { checked = 1; pfx = getenv("TECHBOT_SHOT"); every = pfx && getenv("TECHBOT_SHOT_EVERY") ? atoi(getenv("TECHBOT_SHOT_EVERY")) : 0; }
        if (every > 0 && n++ % every == 0) {
            int ow = 0, oh = 0; SDL_Surface *s;
            SDL_GetRendererOutputSize(ren, &ow, &oh);
            s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
            if (s && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
                char name[600]; snprintf(name, sizeof name, "%s_%05ld.bmp", pfx, n / every); SDL_SaveBMP(s, name);
            }
            if (s) SDL_FreeSurface(s);
        }
    }
    SDL_RenderPresent(ren);
    last_present = SDL_GetPerformanceCounter();
    count_frame();
}

/* widescreen picture (composed by port_render.c), shown with the stretched backdrop */
static int compose_wide(double alpha, int overlay)
{
    static uint8_t out[MAX_W * 200], bd[208 * 160];
    int w = wide_width();
    if ((w <= 320 && !want_imm()) || !(dl_frame_ready() || port_level_intro)) return 0;
    if (dl_view_w != play_w(w) || dl_immersive != want_imm() || (want_imm() && dl_view_h != imm_h())) update_view();
    /* replacement backdrops for this level (widescreen/DROPn.DNx.png) */
    bd_art[0] = art_get(art_backdrop_name(0), ART_BACKDROP);
    bd_art[1] = art_get(art_backdrop_name(1), ART_BACKDROP);
    dl_ext_bd2 = bd_art[1] != NULL;
    if (!dl_compose_wide(alpha, overlay, out, w)) return 0;
    out_w = w;
    dl_backdrop(bd);
    show_indexed(out, w, bd);
    return 1;
}

/* the exact frame, as the original shows it (widescreen during play: the
 * same, composed into the wide layout) */
/* the frame-rate cap applies to every present (menus and popups animate
 * their cursor every few milliseconds): too early, and the frame is shown
 * when its time comes instead */
static uint64_t cap_period(void)
{
    int hz;
    if (cfg.mode != MODE_ENHANCED || cfg.smooth <= 0) return 0;     /* no cap: vsync only */
    hz = cfg.smooth < display_hz ? cfg.smooth : display_hz;
    return hz > 0 ? perf_freq / (uint64_t)hz : 0;
}
static int too_early(void)
{
    uint64_t per = cap_period(), now = SDL_GetPerformanceCounter();
    if (per && present_started && now - present_started < per - per / 10) { present_pending = 1; return 1; }
    present_pending = 0;
    return 0;
}

/* a full-screen picture with replacement art: everything the game has not
 * drawn over since the picture went up becomes transparent (index 16), so the
 * art shows there and what the game drew (menus, dialogs) stays whole.  (Not
 * by colour: a menu pixel that happens to match the picture beneath it would
 * otherwise let the art through.)  Tracked per screen byte (8 pixels): the
 * game's boxes and text sit on byte columns. */
static int picture_with_art(uint8_t *idx)
{
    int i;
    pic_art = NULL;
    if (!want_wide() || port_in_play || port_picture < 0 || port_picture > 4) return 0;
    if (ega_display_page != 0) return 0;
    if (!(pic_art = art_get(picture_name(port_picture), ART_PICTURE))) return 0;
    for (i = 0; i < 64000; i++) if (!ega_written[i >> 3]) idx[i] = 16;
    return 1;
}

/* between levels (and after a quickload) the recorded frames are cleared and
 * the next level's first frame is a moment away: keep the last widescreen
 * frame on screen meanwhile rather than showing the 4:3 page for a frame */
static int hold_wide(void)
{
    return (want_wide() || want_imm()) && port_in_play && dl_wide_shown && !port_level_intro &&
           !dl_frame_ready() && (wide_width() > 320 || want_imm());
}

void pf_present(void)
{
    static uint8_t idx[320 * 200];
    if (too_early()) return;
    if (compose_wide(1.0, 1)) return;
    if (hold_wide()) return;
    out_w = 320;
    ega_present_indexed(idx, ega_display_page);
    picture_with_art(idx);
    show_indexed(idx, 320, NULL);
    pic_art = NULL;
}

/* smooth motion (port_render.c): an in-between frame while the game waits
 * for its next update; alpha = share of the expected frame time elapsed */
static uint64_t frame_shown_at;
static unsigned frame_serial_seen, frame_serial_shown = ~0u;
/* refresh period of the smooth path: the chosen step, never faster than the display */
static uint64_t present_period(void)
{
    int hz = cfg.smooth > 0 ? cfg.smooth : display_hz;
    if (hz > display_hz) hz = display_hz;
    if (hz < 1) hz = 60;
    return perf_freq / (uint64_t)hz;
}
static void present_smooth(uint64_t now)
{
    static uint8_t idx[320 * 200];
    /* the glide is timed so that the last refresh before the next update
     * lands on the end position (one refresh period short of the full step);
     * otherwise the last 10-20% of every step would be skipped as a jump */
    double dur = tick_len * (double)(game_speed + 9) - (double)present_period(), alpha;
    if (dl_serial() != frame_serial_seen) { frame_serial_seen = dl_serial(); frame_shown_at = now; }
    if (!cfg.smooth && frame_serial_shown == frame_serial_seen) return;   /* widescreen only: once per frame */
    frame_serial_shown = frame_serial_seen;
    alpha = dur > 0 ? (double)(now - frame_shown_at) / dur : 1.0;
    if (!cfg.smooth) alpha = 1.0;
    if (compose_wide(alpha, 0)) return;
    if (hold_wide()) return;
    ega_present_indexed(idx, ega_display_page);
    if (!dl_render(alpha, idx)) { pf_present(); return; }
    out_w = 320;
    show_indexed(idx, 320, NULL);
}

/* ------------------------------------------------------------ timing:
 * run the 1193182/8273 Hz timer interrupt in real time */
void pf_service(void)
{
    uint64_t now = SDL_GetPerformanceCounter();
    int n = 0;
    poll_events();
    {   /* test aid: TECHBOT_KEYS="ms:sc,ms:sc,..." presses scancodes (hex, 80h set
         * = release) at those times after start */
        static int init; static long at[64]; static int sc[64], nk, k; static uint64_t t0;
        if (!init) {
            const char *p = getenv("TECHBOT_KEYS");
            init = 1; t0 = now;
            while (p && *p && nk < 64) { at[nk] = strtol(p, (char **)&p, 10); if (*p == ':') p++; sc[nk++] = (int)strtol(p, (char **)&p, 16); if (*p == ',') p++; }
        }
        while (k < nk && (long)((now - t0) * 1000 / perf_freq) >= at[k]) keyboard_isr((uint8_t)sc[k++]);
    }
    while (now >= next_tick_at && n < 16) {
        timer_isr();
        next_tick_at += (uint64_t)tick_len;
        n++;
    }
    if (now >= next_tick_at) next_tick_at = now;      /* fell far behind: resync */
    if (dl_waiting && dl_active()) {
        /* between game frames: present on a fixed timeline, one frame per
         * period of the chosen rate.  Targets are multiples of the period
         * (not "a period after the last present"), so the average can never
         * exceed the cap; after a stall the timeline restarts instead of
         * bursting to catch up.  With vsync each present then lands on a
         * consistent refresh. */
        static uint64_t next_at;
        uint64_t per = present_period();
        if (next_at == 0 || now + 2 * per < next_at) next_at = now;
        if (now >= next_at) {
            present_smooth(now);
            next_at += per;
            if (now >= next_at + per) next_at = now + per;   /* fell behind: resync */
        } else if (next_at - now > perf_freq / 500) SDL_Delay(1);   /* more than 2 ms to go */
        return;
    }
    /* keep dialogs / fades visible: refresh the window at ~60 Hz while spinning
     * (not during a clean game frame with smooth motion: the screen then shows
     * an in-between image, and the exact page would be a step ahead of it) */
    {
        uint64_t per = cap_period(), idle = perf_freq / 60;
        if (per > idle) idle = per;                       /* the cap applies here too */
        if (present_pending && per && now - present_started >= per) pf_present();
        else if ((now - last_present) > idle && !dl_owns_screen()) pf_present();
    }
    if (n == 0) SDL_Delay(1);
}

void pf_delay_ms(unsigned ms)
{
    uint64_t end = SDL_GetPerformanceCounter() + perf_freq * ms / 1000;
    while (SDL_GetPerformanceCounter() < end) pf_service();
}

void pf_frame_hook(void) { }
void pf_test_midframe(void) { }

/* Options menu: window size / fullscreen take effect immediately */
/* windowed size for the settings: 4:3, or 16:9 with widescreen */
static void window_size(int scale, int *w, int *h)
{
    *h = 240 * scale;
    *w = want_wide() ? (*h * 16 / 9) & ~1 : 320 * scale;
}

void pf_apply_settings(void)
{
    if (!win) return;
    SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!cfg.fullscreen) {
        int w, h;
        window_size(cfg.scale, &w, &h);
        SDL_SetWindowSize(win, w, h);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    update_view();
}

/* a startup error: the Windows build has no console, so also show a message
 * box (SDL can show one before SDL_Init) */
static void fatal_message(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    port_log("ERROR: %s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PORT_NAME, msg, NULL);
}

/* ------------------------------------------------------------ crash report:
 * what failed and where, into TECHBOT.LOG (plus TECHBOT.DMP on Windows, which
 * Visual Studio opens together with techbot.pdb from the build folder) */
#ifdef _WIN32
typedef BOOL (WINAPI *MiniDumpFn)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);
/* dbghelp's symbol lookup, loaded at run time: with techbot.pdb next to
 * techbot.exe, each address in a crash report gets its function and line */
typedef struct { ULONG SizeOfStruct, TypeIndex; ULONG64 Reserved[2]; ULONG Index, Size; ULONG64 ModBase;
                 ULONG Flags; ULONG64 Value, Address; ULONG Register, Scope, Tag, NameLen, MaxNameLen;
                 CHAR Name[1]; } TbSymbolInfo;                     /* = SYMBOL_INFO */
typedef struct { DWORD SizeOfStruct; PVOID Key; DWORD LineNumber; PCHAR FileName; DWORD64 Address; } TbLine64;   /* = IMAGEHLP_LINE64 */
typedef BOOL (WINAPI *SymInitFn)(HANDLE, PCSTR, BOOL);
typedef DWORD (WINAPI *SymOptFn)(DWORD);
typedef BOOL (WINAPI *SymFromAddrFn)(HANDLE, DWORD64, DWORD64 *, TbSymbolInfo *);
typedef BOOL (WINAPI *SymLineFn)(HANDLE, DWORD64, DWORD *, TbLine64 *);
static SymFromAddrFn sym_from_addr;
static SymLineFn sym_line;
static void symbols_start(void)
{
    HMODULE dbg = LoadLibraryA("dbghelp.dll");
    SymInitFn init; SymOptFn opt;
    if (!dbg) return;
    init = (SymInitFn)(void *)GetProcAddress(dbg, "SymInitialize");
    opt = (SymOptFn)(void *)GetProcAddress(dbg, "SymSetOptions");
    sym_from_addr = (SymFromAddrFn)(void *)GetProcAddress(dbg, "SymFromAddr");
    sym_line = (SymLineFn)(void *)GetProcAddress(dbg, "SymGetLineFromAddr64");
    if (opt) opt(0x00000002 /* UNDNAME */ | 0x00000010 /* LOAD_LINES */ | 0x00000004 /* DEFERRED_LOADS */);
    if (!init || !init(GetCurrentProcess(), NULL, TRUE)) sym_from_addr = NULL, sym_line = NULL;
}
/* " in function (file.c:123)" for an address, or "" */
static const char *symbol_of(void *addr)
{
    static char out[512];
    union { TbSymbolInfo s; char buf[sizeof(TbSymbolInfo) + 256]; } u;
    TbLine64 line; DWORD64 d64 = 0; DWORD d32 = 0;
    out[0] = 0;
    if (!sym_from_addr) return out;
    memset(&u, 0, sizeof u);
    u.s.SizeOfStruct = 88;                       /* sizeof(SYMBOL_INFO) */
    u.s.MaxNameLen = 255;
    if (!sym_from_addr(GetCurrentProcess(), (DWORD64)(uintptr_t)addr, &d64, &u.s)) return out;
    memset(&line, 0, sizeof line); line.SizeOfStruct = sizeof line;
    if (sym_line && sym_line(GetCurrentProcess(), (DWORD64)(uintptr_t)addr, &d32, &line) && line.FileName) {
        const char *f = strrchr(line.FileName, '\\');
        snprintf(out, sizeof out, "  %s (%s:%lu)", u.s.Name, f ? f + 1 : line.FileName, (unsigned long)line.LineNumber);
    } else snprintf(out, sizeof out, "  %s+0x%llX", u.s.Name, (unsigned long long)d64);
    return out;
}
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep)
{
    static volatile LONG once;
    HMODULE base = GetModuleHandleA(NULL);
    void *frames[32];
    USHORT n, i;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;
    symbols_start();
    port_log("CRASH: exception 0x%08lX at techbot.exe+0x%llX (%p), during: %s",
             (unsigned long)ep->ExceptionRecord->ExceptionCode,
             (unsigned long long)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - (uintptr_t)base),
             ep->ExceptionRecord->ExceptionAddress, port_step ? port_step : "?");
    port_log("  at%s", symbol_of(ep->ExceptionRecord->ExceptionAddress));
    if (ep->ExceptionRecord->ExceptionCode == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2)
        port_log("  (%s address %p)", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                 (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    n = CaptureStackBackTrace(0, 32, frames, NULL);
    for (i = 0; i < n; i++) {
        HMODULE m = NULL; char mod[MAX_PATH] = "?";
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)frames[i], &m);
        if (m) { char *s; GetModuleFileNameA(m, mod, sizeof mod); s = strrchr(mod, '\\'); if (s) memmove(mod, s + 1, strlen(s)); }
        port_log("  %2u  %s+0x%llX%s", i, mod, (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)m),
                 m == base ? symbol_of(frames[i]) : "");
    }
    {   /* a minidump, if dbghelp is there (it is on every Windows) */
        HMODULE dbg = LoadLibraryA("dbghelp.dll");
        MiniDumpFn dump = dbg ? (MiniDumpFn)(void *)GetProcAddress(dbg, "MiniDumpWriteDump") : NULL;
        if (dump) {
            char path[1100]; HANDLE f;
            snprintf(path, sizeof path, "%s", game_path("TECHBOT.DMP"));
            f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                struct { DWORD tid; EXCEPTION_POINTERS *ep; BOOL client; } info;
                info.tid = GetCurrentThreadId(); info.ep = ep; info.client = FALSE;
                if (dump(GetCurrentProcess(), GetCurrentProcessId(), f, 0 /* MiniDumpNormal */, &info, NULL, NULL))
                    port_log("crash dump written: TECHBOT.DMP");
                CloseHandle(f);
            }
        }
    }
    MessageBoxA(NULL, "Techbot has crashed. Details were written to TECHBOT.LOG in the game folder.",
                PORT_NAME, MB_OK | MB_ICONERROR | MB_TOPMOST);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void install_crash_handler(void) { SetUnhandledExceptionFilter(crash_handler); }
#else
static void crash_signal(int sig)
{
    port_log("CRASH: signal %d, during: %s", sig, port_step ? port_step : "?");
    signal(sig, SIG_DFL);
    raise(sig);
}
static void install_crash_handler(void)
{
    signal(SIGSEGV, crash_signal); signal(SIGABRT, crash_signal);
    signal(SIGFPE, crash_signal);  signal(SIGILL, crash_signal);
}
#endif

static const char *name_of_exe(void)
{
    static char n[16];
    snprintf(n, sizeof n, "DN%d.EXE", episode);
    return n;
}

static char **saved_argv;
/* main menu > Episode select: start over with cfg.episode (already saved),
 * same arguments minus any --episode, plus --to-menu (skip the title wait) */
void pf_restart(void)
{
    static char *a[24];
    int i, n = 0;
    for (i = 0; saved_argv[i] && n < 21; i++) {
        if (!strcmp(saved_argv[i], "--to-menu")) continue;
        if (!strcmp(saved_argv[i], "--episode")) { if (saved_argv[i + 1]) i++; continue; }
        a[n++] = saved_argv[i];
    }
    a[n++] = "--to-menu";
    a[n] = NULL;
    port_log("restarting for episode %d", cfg.episode);
#ifdef _WIN32
    /* Windows lets only the program in front bring another one forward: while
     * this one still is, allow the new one to (see main, --to-menu) */
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
#ifdef _WIN32
    /* the Windows C runtime joins the arguments unquoted: quote each one */
    for (i = 0; i < n; i++) {
        size_t len = strlen(a[i]) + 3;
        char *q = (char *)malloc(len);
        snprintf(q, len, "\"%s\"", a[i]);
        a[i] = q;
    }
    _execv(saved_argv[0], (const char *const *)a);
#else
    execv(saved_argv[0], a);
#endif
    exit(0);                                    /* exec failed: just quit */
}

void pf_exit(int code)
{
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
    exit(code);
}

int main(int argc, char **argv)
{
    int scale = -1, fullscreen = -1, i, gargc = 1, ep = 0;
    char *gargv[4] = {"dn1", 0, 0, 0};
    SDL_AudioSpec want, have;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) gamedir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--episode") && i + 1 < argc) ep = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--to-menu")) port_start_at_menu = 1;     /* after an episode switch */
        else if (gargc < 3) gargv[gargc++] = argv[i];      /* "asp", "g" switches */
    }
    cfg_defaults();
    port_log_open();
    install_crash_handler();
    {
        SDL_version c, l;
        SDL_VERSION(&c); SDL_GetVersion(&l);
        port_log("SDL %d.%d.%d (built with %d.%d.%d), game folder: %s", l.major, l.minor, l.patch,
                 c.major, c.minor, c.patch, gamedir);
        for (i = 1; i < argc; i++) port_log("argument: %s", argv[i]);
    }
    port_log_step("reading settings");
    cfg_load(game_path(PORT_CFG_FILE));                   /* command line overrides it */
    port_log("settings: mode %s, aspect %s, hud %s, smoothing %d, scaling %s, sound %d, volume %d, fullscreen %d, scale %d, episode %d",
             cfg.mode == MODE_ENHANCED ? "enhanced" : "original",
             cfg.widescreen ? "wide" : "classic", cfg.hud == HUD_IMMERSIVE ? "immersive" : cfg.hud == HUD_OFF ? "off" : cfg.hud ? "immersive_alt" : "classic", cfg.smooth,
             cfg.scaling == SCALE_NEAREST ? "nearest" : "sharp", cfg.sound, cfg.volume, cfg.fullscreen, cfg.scale, cfg.episode);
    if (scale < 1) scale = cfg.scale;
    if (fullscreen < 0) fullscreen = cfg.fullscreen;
    saved_argv = argv;
#if defined(_WIN32) && !defined(TECHBOT_CONSOLE)
    {   /* should the program still be a console one (see CMakeLists.txt), close a
         * console window that was opened only for it - not one it was started from */
        DWORD pids[2];
        if (GetConsoleWindow() && GetConsoleProcessList(pids, 2) == 1) FreeConsole();
    }
#endif
    /* the episode: --episode, else the setting; if that EXE is missing, the
     * first one present (a folder may hold one, two or all three episodes) */
    if (ep >= 1 && ep <= 3) cfg.episode = ep;
    if (!episode_available(cfg.episode))
        for (i = 1; i <= 3; i++) if (episode_available(i)) { cfg.episode = i; break; }
    {
        char name[16];
        snprintf(name, sizeof name, "DN%d.EXE", cfg.episode);
        port_log_step("loading the game EXE");
        if (!exe_load(game_path(name))) {
            char msg[1200];
            snprintf(msg, sizeof msg, "No DN1.EXE, DN2.EXE or DN3.EXE found in:\n%s\n\n"
                     "Put %s in the folder with the original game files, or start it with "
                     "--dir <game folder>.", gamedir[0] == '.' && !gamedir[1] ? "the current folder" : gamedir,
                     PORT_NAME);
            fatal_message(msg);
            return 1;
        }
    }
    port_log("episode %d (%s)", episode, name_of_exe());
    /* high-DPI displays (e.g. 4K at 250%): draw at the display's full
     * resolution, while window sizes stay in Windows' scaled units */
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    port_log_step("starting SDL");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        char msg[512];
        snprintf(msg, sizeof msg, "Could not start SDL: %s", SDL_GetError());
        fatal_message(msg);
        return 1;
    }
    {
        int ww, wh;
        cfg.scale = scale;
        window_size(scale, &ww, &wh);
        {   /* no bigger than the desktop's usable area (a smaller scale instead) */
            SDL_Rect ub;
            while (scale > 1 && SDL_GetDisplayUsableBounds(0, &ub) == 0 && (ww > ub.w || wh > ub.h - 40)) {
                scale--; window_size(scale, &ww, &wh);
            }
        }
        char title[64];
        snprintf(title, sizeof title, "%s", PORT_FULL_NAME);
#ifdef _WIN32
        /* the icon in techbot.exe (resources/techbot.rc), at every size Windows wants */
        SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
        SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
        port_log_step("creating the window");
        win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!win) {
            char msg[512];
            snprintf(msg, sizeof msg, "Could not create the window: %s", SDL_GetError());
            fatal_message(msg);
            return 1;
        }
#ifndef _WIN32
        {   /* window icon: the 16x16 pixel art doubled, pixel for pixel */
            static uint32_t px[32 * 32];
            int x, y;
            SDL_Surface *ic;
            for (y = 0; y < 32; y++)
                for (x = 0; x < 32; x++) {
                    const unsigned char *p = techbot_icon16 + ((y / 2) * 16 + x / 2) * 4;
                    px[y * 32 + x] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
                }
            ic = SDL_CreateRGBSurfaceFrom(px, 32, 32, 32, 32 * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); }
        }
#endif
    }
    port_log_step("creating the renderer");
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) { port_log("renderer (accelerated, render targets) failed: %s", SDL_GetError());
                ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC); }
    if (!ren) { port_log("renderer (accelerated) failed: %s", SDL_GetError());
                ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); }
    if (!ren) {
        char msg[512];
        snprintf(msg, sizeof msg, "Could not start the renderer: %s", SDL_GetError());
        fatal_message(msg);
        return 1;
    }
    {
        SDL_RendererInfo ri; int ow = 0, oh = 0, pw = 0, ph = 0;
        SDL_GetRendererInfo(ren, &ri); SDL_GetRendererOutputSize(ren, &ow, &oh); SDL_GetWindowSize(win, &pw, &ph);
        port_log("video: driver %s, renderer %s%s%s, window %dx%d, drawing %dx%d", SDL_GetCurrentVideoDriver(), ri.name,
                 (ri.flags & SDL_RENDERER_ACCELERATED) ? " (accelerated)" : " (software)",
                 (ri.flags & SDL_RENDERER_TARGETTEXTURE) ? ", render targets" : "", pw, ph, ow, oh);
    }
    port_log_step("creating textures");
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, MAX_W, 200);
    bdtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 208, 160);
    if (!tex || !bdtex) {
        char msg[512];
        snprintf(msg, sizeof msg, "Could not create textures: %s", SDL_GetError());
        fatal_message(msg);
        return 1;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    update_view();
    SDL_memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = audio_cb;
    port_log_step("opening audio");
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio) { audio_rate = have.freq; SDL_PauseAudioDevice(audio, 0); port_log("audio: %d Hz, %d-sample buffer", have.freq, have.samples); }
    else port_log("audio: none (%s)", SDL_GetError());
    port_log_step("looking for a controller");
    for (i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); pad_detect_style(); break; }
    port_log("controller: %s", pad ? SDL_GameControllerName(pad) : "none");
    if (port_start_at_menu) {
        /* restarted for another episode: come to the front (the old copy
         * allowed it, see pf_restart) */
#ifdef SDL_HINT_FORCE_RAISEWINDOW
        SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
#endif
        SDL_RaiseWindow(win);
#ifdef _WIN32
        {
            SDL_SysWMinfo wm;
            SDL_VERSION(&wm.version);
            if (SDL_GetWindowWMInfo(win, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS) {
                SetForegroundWindow(wm.info.win.window);
                SetFocus(wm.info.win.window);
            }
        }
#endif
    }

    {
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(win), &dm) == 0 && dm.refresh_rate > 0)
            display_hz = dm.refresh_rate;
    }
    perf_freq = SDL_GetPerformanceFrequency();
    tick_len = (double)perf_freq * 8273.0 / 1193182.0;
    next_tick_at = SDL_GetPerformanceCounter();
    port_log("display refresh %d Hz", display_hz);
    port_log_step("running");
    game_main(gargc, gargv);
    pf_exit(0);
    return 0;
}
