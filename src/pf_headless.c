/*
 * pf_headless.c - deterministic, display-less platform layer used for
 * automated testing (state dumps, screenshots).  Every busy-wait step
 * advances the emulated 145 Hz timer by one tick.
 */
#include "dn1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <strings.h>

int pf_headless = 1;
static const char *export_dir;
static int wide_w;                 /* --wide */
int pf_joy_x, pf_joy_y;
static const char *gamedir = ".";
static FILE *dump;
static long frame_no, max_frames = -1;
static const char *shot_prefix;
static int shot_every;
extern int forced_menu_choice;

const char *game_path(const char *name)
{
    static char buf[1024];
    DIR *d;
    struct dirent *e;
    snprintf(buf, sizeof buf, "%s/%s", gamedir, name);
    d = opendir(gamedir);
    if (!d) return buf;
    while ((e = readdir(d)) != NULL)
        if (strcasecmp(e->d_name, name) == 0) { snprintf(buf, sizeof buf, "%s/%s", gamedir, e->d_name); break; }
    closedir(d);
    return buf;
}

/* --script "tick:sc,tick:sc,..." injects scancodes (hex) at timer ticks and
 * --snap "tick,tick,..." saves screenshots there: lets tests drive menus */
static long tick_no;
static struct { long t; int sc; } script[256];
static int n_script, script_pos;
static long snaps[64];
static int n_snaps, snap_pos;
static void save_shot(long n);
/* --present-sim HZ: imitate pf_sdl's smooth presentation on a display of HZ
 * refreshes per second, in emulated time, and log what each refresh shows */
static double psim_hz, psim_t, psim_next, psim_shown;
static unsigned psim_serial;
static void present_sim(void)
{
    extern int dl_dbg_cx, dl_dbg_cy, dl_dbg_obj;
    static uint8_t img[320 * 200];
    psim_t += 1.0;
    while (psim_t >= psim_next) {
        psim_next += 145.0 / psim_hz;
        if (dl_waiting && dl_active()) {
            double alpha;
            if (dl_serial() != psim_serial) { psim_serial = dl_serial(); psim_shown = psim_t; }
            alpha = (psim_t - psim_shown) / ((double)(game_speed + 9) - 145.0 / psim_hz);   /* as pf_sdl */
            ega_present_indexed(img, ega_display_page);
            if (dl_render(alpha, img)) printf("P %u %.3f cam %d %d duke %d\n", psim_serial, alpha, dl_dbg_cx, dl_dbg_cy, dl_dbg_obj);
            else printf("P %u %.3f exact\n", psim_serial, alpha);
        }
    }
}
static int dlg_auto;            /* DLG_AT: press Enter now and then, saving the screen first */
static void save_shot(long n);
static void tick(void)
{
    static long k;
    timer_isr();
    if (dlg_auto && ++k % 60 == 0) {
        if ((k / 60) & 1) { save_shot(-(k / 120 + 1)); keyboard_isr(0x1C); } else keyboard_isr(0x9C);
    }
    tick_no++;
    if (psim_hz > 0) present_sim();
    while (script_pos < n_script && script[script_pos].t <= tick_no) keyboard_isr((uint8_t)script[script_pos++].sc);
    while (snap_pos < n_snaps && snaps[snap_pos] <= tick_no) save_shot(-snaps[snap_pos++]);
}
void pf_service(void) { tick(); }
void pf_present(void) { }
void pf_delay_ms(unsigned ms) { unsigned n = ms * 145 / 1000 + 1; while (n--) tick(); }
int  pf_joystick_buttons(void) { return 0xFF; }
/* HINT_AT=frame: show a hint popup inside that game frame (like the game does) */
void hint_soda(void);
/* DLG_AT=frame: draw the episode-specific dialogs one after another, each
 * saved as dlg_<n>.ppm (test aid for the DN2/DN3 text routines) */
void ui_show_intro_a(void), ui_show_intro_b(void), dlg_end_1(void), dlg_end_2(void), dlg_end_3(void),
     dlg_end_4(void), dlg_end_5(void), dlg_episode_text(void), dlg_to_be_continued(int), ui_clear_page0(void);
static void dlg_shots(void)
{
    static void (*const f[])(void) = { ui_show_intro_a, ui_show_intro_b, dlg_end_1, dlg_end_2, dlg_end_3, dlg_end_4, dlg_end_5 };
    unsigned i;
    shot_prefix = "dlg"; dlg_auto = 1;            /* every wait is captured, then answered */
    set_draw_page(0); set_display_page(0);        /* as in the menus */
    for (i = 0; i < sizeof f / sizeof f[0]; i++) { ui_clear_page0(); f[i](); }
    ui_clear_page0(); dlg_episode_text(); pf_delay_ms(1000);
    dlg_to_be_continued(7);
    pf_exit(0);
}
void pf_test_midframe(void)
{
    static long done = -1;
    if (getenv("HINT_AT") && atol(getenv("HINT_AT")) == frame_no && done != frame_no) { done = frame_no; hint_soda(); }
    if (getenv("DLG_AT") && atol(getenv("DLG_AT")) == frame_no && done != frame_no) { done = frame_no; dlg_shots(); }
}
void pf_apply_settings(void) { }
int  pf_pad_connected(void) { return 0; }
int  pf_pad_capture(void) { return -1; }
void pf_speaker(uint16_t div) { (void)div; }
void pf_audio_lock(void) {}
void pf_audio_unlock(void) {}
void pf_restart(void) { pf_exit(0); }
void pf_exit(int code) { if (dump) fclose(dump); exit(code); }

/* PPM screenshot of a page */
static void save_shot(long n)
{
    static uint8_t idx[320 * 200];
    char name[512];
    FILE *f;
    int i;
    if (n < 0 && wide_w) {                  /* widescreen: the composed picture, as pf_present shows it */
        static uint8_t wout[640 * 200];
        snprintf(name, sizeof name, "%s_t%06ld.ppm", shot_prefix ? shot_prefix : "snap", -n);
        if (dl_compose_wide(1.0, 1, wout, wide_w) && (f = fopen(name, "wb"))) {
            fprintf(f, "P6\n%d 200\n255\n", wide_w);
            for (i = 0; i < wide_w * 200; i++) {
                uint32_t c = wout[i] == 16 ? 0xFF00FF : ega_rgb(ega_palette[wout[i] & 15]);
                fputc((c >> 16) & 0xFF, f); fputc((c >> 8) & 0xFF, f); fputc(c & 0xFF, f);
            }
            fclose(f);
            return;
        }
    }
    if (n < 0) snprintf(name, sizeof name, "%s_t%06ld.ppm", shot_prefix ? shot_prefix : "snap", -n);
    else       snprintf(name, sizeof name, "%s_%05ld.ppm", shot_prefix, n);
    ega_present_indexed(idx, ega_display_page);
    f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 200\n255\n");
    for (i = 0; i < 320 * 200; i++) {
        uint32_t c = ega_rgb(ega_palette[idx[i]]);
        fputc((c >> 16) & 0xFF, f); fputc((c >> 8) & 0xFF, f); fputc(c & 0xFF, f);
    }
    fclose(f);
}

/* called once per game frame, right after the frame-pacing wait */
typedef struct { const char *name; void *addr; unsigned ds_off; unsigned size; } StateVar;
extern const StateVar state_map[];
/* --dl-test: check the smooth-motion renderer against the exact frames:
 * drawn at alpha 0 it must reproduce the previous frame's play window and
 * at alpha ~1 the current one.  Prints mismatching pixels per frame. */
static int dl_test;
static void dl_check(void)
{
    static uint8_t exact_prev[320 * 200], exact_cur[320 * 200], img[320 * 200];
    static int have_prev;
    long d0 = 0, d1 = 0;
    int x, y, r0, r1;
    ega_present_indexed(exact_cur, ega_display_page);
    memcpy(img, exact_cur, sizeof img);
    r0 = dl_render(0.0, img);
    if (r0 && have_prev)
        for (y = 16; y < 176; y++) for (x = 16; x < 224; x++) d0 += img[y * 320 + x] != exact_prev[y * 320 + x];
    memcpy(img, exact_cur, sizeof img);
    r1 = dl_render(0.9999, img);
    if (r1)
        for (y = 16; y < 176; y++) for (x = 16; x < 224; x++) d1 += img[y * 320 + x] != exact_cur[y * 320 + x];
    { int dl_split_pieces(void), dl_cam_dx(void), dl_cam_dy(void), dl_moving_actors(void); printf("split %d cam %d %d mov %d\n", dl_split_pieces(), dl_cam_dx(), dl_cam_dy(), dl_moving_actors());
      { int dl_torn_blocks(int *), chk, t = dl_torn_blocks(&chk); printf("blocks %d torn %d\n", chk, t); } }
    if (getenv("DL_STRIP") && atol(getenv("DL_STRIP")) == frame_no) {
        /* the play window at five points of the glide, side by side */
        static uint8_t strip[160 * 5 * 208];
        int k, yy, xx; FILE *f;
        for (k = 0; k < 5; k++) {
            memcpy(img, exact_cur, sizeof img);
            if (!dl_render(k * 0.24, img)) memcpy(img, exact_cur, sizeof img);
            for (yy = 0; yy < 160; yy++) for (xx = 0; xx < 208; xx++) strip[yy * 1040 + k * 208 + xx] = img[(yy + 16) * 320 + xx + 16];
        }
        f = fopen("dl_strip.pgm", "wb"); fprintf(f, "P5\n1040 160\n15\n"); fwrite(strip, sizeof strip, 1, f); fclose(f);
    }
    printf("dl frame %ld: rendered %d/%d  alpha0 diff %ld  alpha1 diff %ld\n", frame_no, r0, r1, d0, d1);
    if (getenv("DL_DUMP") && atol(getenv("DL_DUMP")) == frame_no) {
        FILE *f; int i;
        f = fopen("dl_exact.pgm", "wb"); fprintf(f, "P5\n320 200\n15\n"); for (i = 0; i < 64000; i++) fputc(exact_cur[i], f); fclose(f);
        f = fopen("dl_render.pgm", "wb"); fprintf(f, "P5\n320 200\n15\n"); for (i = 0; i < 64000; i++) fputc(img[i], f); fclose(f);
    }
    memcpy(exact_prev, exact_cur, sizeof exact_prev);
    have_prev = 1;
}

/* --wide W: Enhanced widescreen at picture width W; --wideshot F,F,...
 * writes the composed wide picture after those frames (PPM) */
static long wshots[32]; static int n_wshots;
static void wide_shot(void)
{
    static uint8_t out[640 * 200];
    char name[64]; FILE *f; int i, k;
    for (k = 0; k < n_wshots; k++) if (wshots[k] == frame_no) break;
    if (getenv("WIDE_CHECK") && dl_compose_wide(1.0, 0, out, wide_w)) {
        /* centre of the wide picture vs the original window (backdrop pixels skipped) */
        static uint8_t ex[320 * 200];
        long d = 0, n = 0, edge = 0; int x, y, ox = dl_play_x0() + dl_view_offset(), oy = dl_play_y0() + dl_view_offset_y();
        uint8_t bd[208 * 160];
        dl_backdrop(bd);
        ega_present_indexed(ex, ega_display_page);
        for (y = 16; y < 176; y++) for (x = 16; x < 224; x++) {
            uint8_t a = out[(y - 16 + oy) * wide_w + ox + x - 16], b = ex[y * 320 + x];
            if (a == 16) continue;
            n++;
            if (a != b) { d++; if (x < 32 || x >= 208) edge++; }
        }
        printf("widecheck frame %ld offset %d,%d compared %ld differ %ld edge %ld\n", frame_no, ox - dl_play_x0(), oy - dl_play_y0(), n, d, edge);
    }
    if (k == n_wshots || !dl_compose_wide(1.0, 0, out, wide_w)) return;
    snprintf(name, sizeof name, "wide_%05ld.ppm", frame_no);
    if (!(f = fopen(name, "wb"))) return;
    fprintf(f, "P6\n%d 200\n255\n", wide_w);
    for (i = 0; i < wide_w * 200; i++) {
        uint32_t c = out[i] == 16 ? 0xFF00FF : ega_rgb(ega_palette[out[i] & 15]);   /* magenta = backdrop */
        fputc((c >> 16) & 0xFF, f); fputc((c >> 8) & 0xFF, f); fputc(c & 0xFF, f);
    }
    fclose(f);
}

void pf_frame_hook(void)
{
    if (wide_w) wide_shot();
    if (dl_test) dl_check();
    if (dump) {
        static uint8_t ds[0x10000];
        const StateVar *v;
        memset(ds, 0, sizeof ds);
        for (v = state_map; v->name; v++) memcpy(ds + v->ds_off, v->addr, v->size);
        fwrite("FRM0", 4, 1, dump);
        fwrite(&frame_no, 4, 1, dump);
        fwrite(ds, sizeof ds, 1, dump);
        fwrite(mapbuf, 0x5A00, 1, dump);
        {   int p; uint16_t pg = ega_display_page ? 0x2000 : 0;
            for (p = 0; p < 4; p++) fwrite(&ega_planes[p][pg], 8000, 1, dump); }
        fflush(dump);   /* a run killed while stalled in a dialog keeps every finished frame */
    }
    if (shot_prefix && shot_every && frame_no % shot_every == 0) save_shot(frame_no);
    frame_no++;
    if (max_frames >= 0 && frame_no >= max_frames) pf_exit(0);
}

/* --export-sounds DIR: every effect as WAV, in the original single-voice
 * square wave and in the Enhanced hi-fi style, for auditioning */
static void wav_write(const char *path, const int16_t *s, long n, int rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t u; uint16_t h;
    if (!f) return;
#define W32(x) (u = (uint32_t)(x), fwrite(&u, 4, 1, f))
#define W16(x) (h = (uint16_t)(x), fwrite(&h, 2, 1, f))
    fwrite("RIFF", 1, 4, f); W32(36 + n * 2); fwrite("WAVEfmt ", 1, 8, f);
    W32(16); W16(1); W16(1); W32(rate); W32(rate * 2); W16(2); W16(16);
    fwrite("data", 1, 4, f); W32(n * 2); fwrite(s, 2, (size_t)n, f);
    fclose(f);
}
static void export_sounds(const char *dir)
{
    enum { RATE = 44100, MAXS = RATE * 8 };
    static int16_t buf[MAXS];
    static const char *const style[3] = { "orig", "overlap", "hifi" };
    char path[512];
    int e, st;
    sound_load_bank(ds_str(0x0ABB), ds_str(0x0AC5));
    sound_enabled = 1;
    cfg.mode = MODE_ENHANCED;
    for (st = 1; st < 3; st++) {
        for (e = 0; e < 48; e++) {
            long n = 0;
            cfg.sound = st;
            port_sound_play(e);
            while (n < MAXS - 512 && (port_sound_busy() || n < RATE / 20)) { port_sound_render(buf + n, 512, RATE); n += 512; }
            if (st == 2) { long t = n + RATE * 2 / 5; while (n < t && n < MAXS - 512) { port_sound_render(buf + n, 512, RATE); n += 512; } }
            if (st == 2) printf("%02d %s\n", e, (const char *[]){ "sweep", "tone", "noise" }[port_sound_kind(e)]);
            snprintf(path, sizeof path, "%s/%02d_%s.wav", dir, e, st == 1 ? style[0] : style[2]);
            wav_write(path, buf, n, RATE);
        }
        /* a burst of overlapping effects: shots, a pickup and an explosion */
        {
            static const int seq[][2] = { {0, 0}, {6, 0}, {6, 4}, {6, 8}, {2, 3}, {6, 12}, {17, 14}, {6, 16} };
            long n = 0; unsigned k = 0; int frame = 0;
            cfg.sound = st;
            while (n < RATE * 3) {
                while (k < sizeof seq / sizeof seq[0] && seq[k][1] == frame) port_sound_play(seq[k++][0]);
                port_sound_render(buf + n, RATE / 16, RATE); n += RATE / 16; frame++;
            }
            snprintf(path, sizeof path, "%s/mix_%s.wav", dir, style[st]);
            wav_write(path, buf, n, RATE);
        }
    }
}

int main(int argc, char **argv)
{
    const char *exe = "DN1.EXE";
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) gamedir = argv[++i];
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--menu") && i + 1 < argc) forced_menu_choice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "--shots") && i + 2 < argc) { shot_prefix = argv[++i]; shot_every = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--cursor-menus")) { cfg_defaults(); cfg.cursor_menus = 1; }
        else if (!strcmp(argv[i], "--pad-labels") && i + 1 < argc) { input_device = DEV_PAD; pad_style = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--present-sim") && i + 1 < argc) { psim_hz = atof(argv[++i]); cfg.mode = MODE_ENHANCED; cfg.smooth = 1; }
        else if (!strcmp(argv[i], "--wide") && i + 1 < argc) {
            wide_w = atoi(argv[++i]); cfg.mode = MODE_ENHANCED; cfg.widescreen = ASPECT_WIDE; cfg.hud = HUD_CLASSIC; cfg.smooth = 0;
            dl_set_view(1, wide_w - 112, 160);
        }
        else if (!strcmp(argv[i], "--hud-off") && i + 1 < argc) {        /* Immersive without the bar */
            wide_w = atoi(argv[++i]); cfg.mode = MODE_ENHANCED; cfg.widescreen = wide_w > 320; cfg.hud = HUD_OFF; cfg.smooth = 0;
            dl_set_view(1, wide_w, 200);
        }
        else if (!strcmp(argv[i], "--immersive-alt") && i + 1 < argc) {   /* the same, play area above the bar */
            wide_w = atoi(argv[++i]); cfg.mode = MODE_ENHANCED; cfg.widescreen = wide_w > 320; cfg.hud = HUD_IMMERSIVE_ALT; cfg.smooth = 0;
            dl_set_view(1, wide_w, hud_bar_top());
        }
        else if (!strcmp(argv[i], "--immersive") && i + 1 < argc) {   /* picture width W, all play area */
            wide_w = atoi(argv[++i]); cfg.mode = MODE_ENHANCED; cfg.widescreen = wide_w > 320; cfg.hud = HUD_IMMERSIVE; cfg.smooth = 0;
            dl_set_view(1, wide_w, 200);
        }
        else if (!strcmp(argv[i], "--wideshot") && i + 1 < argc) {
            char *p = argv[++i];
            while (*p && n_wshots < 32) { wshots[n_wshots++] = strtol(p, &p, 10); if (*p == ',') p++; }
        }
        else if (!strcmp(argv[i], "--export-sounds") && i + 1 < argc) export_dir = argv[++i];
        else if (!strcmp(argv[i], "--dl-test")) { dl_test = 1; cfg.mode = MODE_ENHANCED; cfg.smooth = 1; }
        else if (!strcmp(argv[i], "--qsave")) { cfg.key_qsave = 0x3F; cfg.key_qload = 0x43; }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *p = argv[++i];
            while (*p && n_script < 256) {
                script[n_script].t = strtol(p, &p, 10);
                if (*p == ':') p++;
                script[n_script++].sc = (int)strtol(p, &p, 16);
                if (*p == ',') p++;
            }
        }
        else if (!strcmp(argv[i], "--snap") && i + 1 < argc) {
            char *p = argv[++i];
            while (*p && n_snaps < 64) { snaps[n_snaps++] = strtol(p, &p, 10); if (*p == ',') p++; }
        }
    }
    if (!exe_load(game_path(exe)) && !exe_load(exe)) {
        fprintf(stderr, "cannot load %s (the original DN1.EXE is required)\n", exe);
        return 1;
    }
    if (export_dir) { export_sounds(export_dir); return 0; }
    game_main(0, NULL);
    return 0;
}
