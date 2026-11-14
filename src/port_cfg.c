/*
 * port_cfg.c - TECHBOT.CFG: the port's own settings.
 *
 * Format: one "key = value" per line, '#' starts a comment.  Unknown keys are
 * ignored, so older/newer versions can share a file.  Controller buttons use
 * SDL2's names (a, b, x, y, back, start, leftshoulder, dpup, ...).
 */
#include "dn1.h"          /* game_path */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

PortConfig cfg;
int cfg_loaded;
volatile int ui_context = UI_CTX_GAME;

const char *const padact_name[PADACT_COUNT] = {
    "up", "down", "left", "right", "fire", "jump", "menu", "back", "quicksave", "quickload"
};

/* SDL_GameControllerButton values (stable in SDL2), kept here so the core
 * does not depend on SDL headers */
static const char *const btn_name[] = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
    "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright"
};
#define N_BTN ((int)(sizeof btn_name / sizeof btn_name[0]))

static int btn_from(const char *s)
{
    int i;
    if (!strcmp(s, "none")) return PAD_NONE;
    for (i = 0; i < N_BTN; i++) if (!strcmp(s, btn_name[i])) return i;
    return -2;   /* unknown: keep current */
}
static const char *btn_str(int b) { return (b >= 0 && b < N_BTN) ? btn_name[b] : "none"; }

void cfg_default_pad(void)
{
    /* d-pad, X fire (B too), A jump, Start menu, Back, LB quicksave, RB quickload */
    static const int def[PADACT_COUNT]     = { 11, 12, 13, 14, 2, 0, 6, 4, 9, 10 };
    static const int def_alt[PADACT_COUNT] = { -1, -1, -1, -1, 1, -1, -1, -1, -1, -1 };
    int i;
    for (i = 0; i < PADACT_COUNT; i++) { cfg.pad[i] = def[i]; cfg.pad_alt[i] = def_alt[i]; }
    cfg.deadzone = 12000;
}

void cfg_defaults(void)
{
    cfg.mode = MODE_ORIGINAL;
    cfg.cursor_menus = 1;
    cfg.scale = 4;
    cfg.scaling = SCALE_SHARP;
    cfg.fullscreen = 0;
    cfg.smooth = -1;                     /* match the display */
    cfg.widescreen = ASPECT_16_9;
    cfg.hud = HUD_CLASSIC;
    cfg.sound = SND_OVERLAP;
    cfg.episode = 1;
    cfg.volume = 5;
    cfg_default_pad();
    cfg.key_qsave = 0x3F;   /* F5 */
    cfg.key_qload = 0x43;   /* F9 */
    cfg.key_hud = 0x0F;     /* Tab (H is the game's own hint toggle) */
}

static char *trim(char *s)
{
    char *e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int episode_available(int ep)
{
    char name[16];
    FILE *f;
    snprintf(name, sizeof name, "DN%d.EXE", ep);
    if (!(f = fopen(game_path(name), "rb"))) return 0;
    fclose(f);
    return 1;
}

void cfg_load(const char *path_in)
{
    char keep[1024];                 /* path_in may be game_path()'s shared buffer */
    const char *path = keep;
    char line[256];
    FILE *f;
    snprintf(keep, sizeof keep, "%s", path_in);
    f = fopen(path, "r");
    cfg_loaded = 1;
    if (!f && (f = fopen(game_path("DN1PORT.CFG"), "r")) != NULL) {
        /* settings from before the Techbot name: read them, write TECHBOT.CFG */
        fclose(f);
        cfg_load(game_path("DN1PORT.CFG"));
        cfg_save(keep);
        return;
    }
    if (!f) { cfg_save(path); return; }          /* first run: write the defaults out */
    while (fgets(line, sizeof line, f)) {
        char *k, *v, *eq, *hash = strchr(line, '#');
        int i;
        if (hash) *hash = 0;
        if (!(eq = strchr(line, '='))) continue;
        *eq = 0; k = trim(line); v = trim(eq + 1);
        for (i = 0; k[i]; i++) k[i] = (char)tolower((unsigned char)k[i]);
        for (i = 0; v[i]; i++) v[i] = (char)tolower((unsigned char)v[i]);
        if (!strcmp(k, "mode")) cfg.mode = !strcmp(v, "enhanced") ? MODE_ENHANCED : MODE_ORIGINAL;
        else if (!strcmp(k, "menus")) cfg.cursor_menus = strcmp(v, "classic") != 0;
        else if (!strcmp(k, "scale")) { int s = atoi(v); if (s >= 1 && s <= 16) cfg.scale = s; }
        else if (!strcmp(k, "smooth")) {
            if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "no")) cfg.smooth = 0;
            else if (!strcmp(v, "30") || !strcmp(v, "60") || !strcmp(v, "120")) cfg.smooth = atoi(v);
            else cfg.smooth = -1;                /* display, on, 1, yes */
        }
        else if (!strcmp(k, "show_fps")) cfg.show_fps = atoi(v) != 0 || !strcmp(v, "yes");
        else if (!strcmp(k, "scaling")) cfg.scaling = !strcmp(v, "nearest") ? SCALE_NEAREST : SCALE_SHARP;
        else if (!strcmp(k, "aspect"))        /* ("wide": the older name of wide_fill) */
            cfg.widescreen = !strcmp(v, "classic") ? ASPECT_CLASSIC : !strcmp(v, "wide_16_9") ? ASPECT_16_9 : ASPECT_WIDE;
        else if (!strcmp(k, "hud"))           /* (older names: immersive, immersive_alt) */
            cfg.hud = !strcmp(v, "slim_overlay") || !strcmp(v, "immersive") ? HUD_IMMERSIVE
                    : !strcmp(v, "slim_fixed") || !strcmp(v, "immersive_alt") ? HUD_IMMERSIVE_ALT
                    : !strcmp(v, "off") ? HUD_OFF : HUD_CLASSIC;
        else if (!strcmp(k, "widescreen")) {     /* older files: 0 off, 1 on (2 = wide + immersive, briefly) */
            int n = atoi(v);
            cfg.widescreen = n != 0 || !strcmp(v, "on") || !strcmp(v, "yes") ? ASPECT_WIDE : ASPECT_CLASSIC;
            if (n == 2) cfg.hud = HUD_IMMERSIVE;
        }
        else if (!strcmp(k, "volume")) { int n = atoi(v); if (n >= 0 && n <= 5) cfg.volume = n; }
        else if (!strcmp(k, "patch_off") && *v && cfg.n_patch_off < 32)
            snprintf(cfg.patch_off[cfg.n_patch_off++], sizeof cfg.patch_off[0], "%s", v);
        else if (!strcmp(k, "episode")) { int e = atoi(v); if (e >= 1 && e <= 3) cfg.episode = e; }
        else if (!strcmp(k, "sound"))         /* (older names: original, overlap, hifi) */
            cfg.sound = !strcmp(v, "classic") || !strcmp(v, "original") ? SND_ORIGINAL
                      : !strcmp(v, "wacky") || !strcmp(v, "hifi") ? SND_HIFI : SND_OVERLAP;
        else if (!strcmp(k, "fullscreen")) cfg.fullscreen = atoi(v) != 0 || !strcmp(v, "yes");
        else if (!strcmp(k, "deadzone")) { int d = atoi(v); if (d >= 0 && d < 32767) cfg.deadzone = d; }
        else if (!strcmp(k, "key.quicksave")) { long c = strtol(v, NULL, 0); if (c > 0 && c < 0x80) cfg.key_qsave = (uint8_t)c; }
        else if (!strcmp(k, "key.hud")) { long c = strtol(v, NULL, 0); if (c > 0 && c < 0x80) cfg.key_hud = (uint8_t)c; }
        else if (!strcmp(k, "key.quickload")) { long c = strtol(v, NULL, 0); if (c > 0 && c < 0x80) cfg.key_qload = (uint8_t)c; }
        else if (!strncmp(k, "pad.", 4)) {
            for (i = 0; i < PADACT_COUNT; i++) {
                if (!strcmp(k + 4, padact_name[i])) {
                    char *comma = strchr(v, ',');
                    int b;
                    if (comma) { *comma = 0; b = btn_from(trim(comma + 1)); cfg.pad_alt[i] = b == -2 ? cfg.pad_alt[i] : b; }
                    else cfg.pad_alt[i] = PAD_NONE;
                    b = btn_from(trim(v));
                    if (b != -2) cfg.pad[i] = b;
                }
            }
        }
    }
    fclose(f);
}

void cfg_save(const char *path)
{
    if (port_no_disk) return;            /* (port) a play-test in Shrapnel */
    int i;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# " PORT_FULL_NAME " - settings (the original game's own files are not affected)\n");
    fprintf(f, "# mode: original = exactly as DN1.EXE; enhanced = port improvements\n");
    fprintf(f, "mode = %s\n", cfg.mode == MODE_ENHANCED ? "enhanced" : "original");
    fprintf(f, "# episode: 1, 2 or 3 - which DN1/DN2/DN3.EXE in this folder to start\n");
    fprintf(f, "episode = %d\n", cfg.episode);
    fprintf(f, "# volume: 0 (off) .. 5 (full), in 20%% steps\n");
    fprintf(f, "volume = %d\n", cfg.volume);
    fprintf(f, "# menus: cursor = arrow keys / d-pad and confirm; classic = letter keys only\n");
    fprintf(f, "menus = %s\n", cfg.cursor_menus ? "cursor" : "classic");
    fprintf(f, "# smooth: in-between frames in Enhanced mode: off | 30 | 60 | 120 | display\n");
    if (cfg.smooth == 0) fprintf(f, "smooth = off\n");
    else if (cfg.smooth > 0) fprintf(f, "smooth = %d\n", cfg.smooth);
    else fprintf(f, "smooth = display\n");
    fprintf(f, "# scaling: sharp (every pixel the same size) | nearest (plain nearest-neighbour; sizes can vary)\n");
    fprintf(f, "scaling = %s\n", cfg.scaling == SCALE_NEAREST ? "nearest" : "sharp");
    fprintf(f, "# show_fps: frame rate in the window title\nshow_fps = %d\n", cfg.show_fps);
    fprintf(f, "# aspect: in Enhanced mode: classic (4:3) | wide_16_9 (16:9 on any display)\n"
               "# | wide_fill (the picture follows the window's or display's shape, up to about 2.4:1)\n");
    fprintf(f, "aspect = %s\n", cfg.widescreen == ASPECT_16_9 ? "wide_16_9" : cfg.widescreen ? "wide_fill" : "classic");
    fprintf(f, "# hud: in Enhanced mode: classic (border and status panel) | slim_overlay (a slim bar over a\n"
               "# full-screen play area) | slim_fixed (the bar in a fixed panel below the play area)\n"
               "# | off (a full-screen play area, no HUD)\n");
    fprintf(f, "hud = %s\n", cfg.hud == HUD_IMMERSIVE ? "slim_overlay" : cfg.hud == HUD_IMMERSIVE_ALT ? "slim_fixed"
                              : cfg.hud == HUD_OFF ? "off" : "classic");
    fprintf(f, "# sound: in Enhanced mode: classic (one voice, as DN1.EXE) | improved (effects overlap) | wacky (overlap + reworked sounds)\n");
    fprintf(f, "sound = %s\n", cfg.sound == SND_ORIGINAL ? "classic" : cfg.sound == SND_OVERLAP ? "improved" : "wacky");
    fprintf(f, "scale = %d\nfullscreen = %d\ndeadzone = %d\n", cfg.scale, cfg.fullscreen, cfg.deadzone);
    fprintf(f, "# patch_off: a patch in the PATCHES folder switched off (one line each; any other applies in Enhanced mode)\n");
    for (i = 0; i < cfg.n_patch_off; i++) fprintf(f, "patch_off = %s\n", cfg.patch_off[i]);
    fprintf(f, "# quicksave / quickload keys as PC scancodes (0x3f = F5, 0x43 = F9)\n");
    fprintf(f, "key.quicksave = 0x%02x\nkey.quickload = 0x%02x\n", cfg.key_qsave, cfg.key_qload);
    fprintf(f, "# HUD toggle in play (Off <-> Immersive), scancode (0x0f = Tab)\nkey.hud = 0x%02x\n", cfg.key_hud);
    fprintf(f, "# controller: pad.<action> = button[, second button]\n");
    fprintf(f, "# buttons: a b x y back start leftshoulder rightshoulder leftstick rightstick dpup dpdown dpleft dpright none\n");
    for (i = 0; i < PADACT_COUNT; i++) {
        if (cfg.pad_alt[i] >= 0) fprintf(f, "pad.%s = %s, %s\n", padact_name[i], btn_str(cfg.pad[i]), btn_str(cfg.pad_alt[i]));
        else                     fprintf(f, "pad.%s = %s\n", padact_name[i], btn_str(cfg.pad[i]));
    }
    fclose(f);
}
