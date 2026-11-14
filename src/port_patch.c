/*
 * port_patch.c - level patches made in Shrapnel (port addition).
 *
 * A patch (.PAT) is a zip holding PATCH.INI: for each stock map file it
 * changes, that file's checksum and the changed cells, nothing else of the
 * game (see BUILDING.md, "Patches"):
 *
 *     [WORLDAL3.DN1]
 *     crc32 = 1A2B3C4D        CRC-32 of the file's map (its first 23040 bytes)
 *     cell  = 12 40 1800      column, row, the new value (hex)
 *     backdrop_a = DROP5.DN2  a level's backdrop A (B: backdrop_b): a game file of
 *                             any episode, or a picture of its own kept in the zip
 *     note  = One|Two|Three|  a level's own words (note monitor / Dr. Proton screen)
 *     note_after_3 = ...      in the hallway map's section: the words of the hallway
 *                             after level 3 (every hallway shares the map, not the words)
 *
 * As a stock map loads (read_map, game.c), the patches in the game folder's
 * PATCHES folder are applied to it in memory, in file-name order, then the
 * one given with --patch (Shrapnel's Play in Techbot).  The files on disk are
 * never changed.  A section whose checksum doesn't match the map as read (a
 * different release of the episode) is skipped, and the log says so.
 *
 * Never in Original mode (it stays bit-identical) unless --patch asked for a
 * test, never for the game's own demo (MY_DEMO: it would drift), and never
 * over a pack's own maps or the editor's.
 */
#include "dn1.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PATCH_FOLDER "PATCHES"

const char *port_patch_extra;           /* --patch: one more, applied last (in Original mode too) */
int port_patched;                       /* the map just read had changes applied: Techbot's rules for it */
uint32_t port_patch_sig;                /* ... and what it came to (CRC-32 of the patched map; 0: unpatched) */

/* the level just read: its backdrops and words from a patch, if any */
static char p_drop[2][64];
static uint8_t *p_drop_data[2];
static long p_drop_len[2];
static char p_note[4][PACK_NOTE_W + 1];
static int p_has_note, p_level, p_hall;   /* (p_hall: the hallway after level p_hall, or 0) */

/* a saved game being restored: what the level was like when it was saved
 * (save_game / restore_game, files.c), checked once the level is read */
static int sig_pending;
static uint32_t sig_saved;
static int sig_differs;

int port_list_files(const char *dir, char (*names)[128], int max);   /* port_dir.c */

static int ends_with(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a > b && strcasecmp_port(s + a - b, ext) == 0;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

/* one patch file over the map `name`: the cells it changes (and, for a
   level, its backdrops and words) */
static int apply_file(const char *path, const char *name, uint8_t *map, uint32_t crc)
{
    int extras = 0;
    mz_zip_archive z;
    size_t len = 0;
    char *ini, *line, *next;
    int in = 0, ok = 0, cells = 0, seen = 0;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, path, 0)) { port_log("patch %s: not a zip file", path); return 0; }
    ini = (char *)mz_zip_reader_extract_file_to_heap(&z, "PATCH.INI", &len, 0);
    if (!ini) { mz_zip_reader_end(&z); port_log("patch %s: no PATCH.INI inside", path); return 0; }
    {   /* (NUL-terminated) */
        char *t = (char *)realloc(ini, len + 1);
        if (!t) { mz_free(ini); return 0; }
        ini = t;
        ini[len] = 0;
    }
    for (line = ini; line && *line; line = next) {
        char *t, *eq;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        t = trim(line);
        if (!*t || *t == ';' || *t == '#') continue;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) *e = 0;
            in = strcasecmp_port(trim(t + 1), name) == 0;
            if (in) seen = 1;
            ok = 0;
            continue;
        }
        if (!in || !(eq = strchr(t, '='))) continue;
        *eq = 0;
        if (!strcasecmp_port(trim(t), "crc32")) {
            ok = (uint32_t)strtoul(trim(eq + 1), NULL, 16) == crc;
            if (!ok) port_log("patch %s: %s is a different release (checksum %08X, the patch wants %s): skipped",
                              path, name, (unsigned)crc, trim(eq + 1));
        } else if (ok && p_level && (!strcasecmp_port(trim(t), "backdrop_a") || !strcasecmp_port(trim(t), "backdrop_b"))) {
            int k = tolower((unsigned char)trim(t)[9]) == 'b';
            char *v = trim(eq + 1);
            size_t n = 0;
            void *d = mz_zip_reader_extract_file_to_heap(&z, v, &n, 0);   /* a picture of its own, or a game file */
            free(p_drop_data[k]);
            p_drop_data[k] = NULL;
            p_drop_len[k] = 0;
            snprintf(p_drop[k], sizeof p_drop[k], "%s", v);
            if (d) {
                p_drop_data[k] = (uint8_t *)malloc(n);
                if (p_drop_data[k]) { memcpy(p_drop_data[k], d, n); p_drop_len[k] = (long)n; }
                mz_free(d);
            }
            extras++;
        } else if (ok && ((p_level && !strcasecmp_port(trim(t), "note")) ||
                          (p_hall && !strncasecmp_port(trim(t), "note_after_", 11) && atoi(trim(t) + 11) == p_hall))) {
            char *v = eq + 1, *bar;
            int k;
            if (*v == ' ') v++;           /* (one space after the =; a line's own spaces kept) */
            memset(p_note, 0, sizeof p_note);
            for (k = 0; k < 4 && v; k++) {
                bar = strchr(v, '|');
                if (bar) *bar = 0;
                snprintf(p_note[k], sizeof p_note[k], "%s", v);
                {   /* (no line end) */
                    size_t L = strlen(p_note[k]);
                    while (L && (p_note[k][L - 1] == '\r' || p_note[k][L - 1] == '\n')) p_note[k][--L] = 0;
                }
                v = bar ? bar + 1 : NULL;
            }
            p_has_note = 1;
            extras++;
        } else if (ok && !strcasecmp_port(trim(t), "cell")) {
            int c, r;
            unsigned v;
            if (sscanf(eq + 1, "%d %d %x", &c, &r, &v) == 3 && c >= 0 && c < 128 && r >= 0 && r < 90 && v <= 0xFFFF) {
                map[(r * 128 + c) * 2] = (uint8_t)(v & 0xFF);
                map[(r * 128 + c) * 2 + 1] = (uint8_t)(v >> 8);
                cells++;
            }
        }
    }
    free(ini);
    mz_zip_reader_end(&z);
    if (cells || extras) port_log("patch %s: %d cells of %s%s", path, cells, name, extras ? ", and its backdrops or words" : "");
    (void)seen;
    return cells + extras;
}

/* the map `name` as just read (`len` bytes of it): patched, if patches apply */
void port_patch_map(const char *name, uint8_t *map, size_t len)
{
    static char names[256][128];
    char dir[1100];
    uint32_t crc;
    int n, i, j, cells = 0, k;
    port_patched = 0;
    port_patch_sig = 0;
    for (k = 0; k < 2; k++) { free(p_drop_data[k]); p_drop_data[k] = NULL; p_drop_len[k] = 0; p_drop[k][0] = 0; }
    p_has_note = 0;
    p_level = level_index % 2 == 0 && level_index < 20;   /* (a level's own: not the hallway map all hallways share) */
    p_hall = level_index % 2 == 1 && level_index < 20 ? (level_index + 1) / 2 : 0;   /* (a hallway's words: its own) */
    /* not for the game's own demo (recording or playing MY_DEMO: it would drift).
       A new game always records the first level as USERDEMO, and plays it back
       later in the same folder: patched both times, it keeps step. */
    if (demo_play == 2 || demo_record == 2 || !len) { sig_pending = 0; return; }
    {   /* (the log: once a run, where patches come from, or why not) */
        static int told;
        if (!told) {
            told = 1;
            if (cfg.mode != MODE_ENHANCED) port_log("patches: not applied in Original mode%s", port_patch_extra ? " (but --patch is)" : "");
            else {
                int k2, np = 0;
                snprintf(dir, sizeof dir, "%s", game_path(PATCH_FOLDER));
                n = port_list_files(dir, names, 256);
                int off = 0;
                for (k2 = 0; k2 < n; k2++) if (ends_with(names[k2], ".pat")) { np++; off += !patch_enabled(names[k2]); }
                port_log("patches: %d .pat file%s in %s%s", np, np == 1 ? "" : "s", dir, off ? " (some switched off in Game setup > Patches)" : "");
            }
        }
    }
    if (cfg.mode != MODE_ENHANCED && !port_patch_extra) goto check;
    crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, map, len);
    if (cfg.mode == MODE_ENHANCED) {
        snprintf(dir, sizeof dir, "%s", game_path(PATCH_FOLDER));
        n = port_list_files(dir, names, 256);
        for (i = 1; i < n; i++)              /* in file-name order */
            for (j = i; j > 0 && strcasecmp_port(names[j - 1], names[j]) > 0; j--) {
                char t[128];
                memcpy(t, names[j], 128); memcpy(names[j], names[j - 1], 128); memcpy(names[j - 1], t, 128);
            }
        for (i = 0; i < n; i++) {
            char path[1300];
            if (!ends_with(names[i], ".pat") || !patch_enabled(names[i])) continue;
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            cells += apply_file(path, name, map, crc);
        }
    }
    if (port_patch_extra) cells += apply_file(port_patch_extra, name, map, crc);
    port_patched = cells > 0;
    if (port_patched) port_patch_sig = (uint32_t)mz_crc32(MZ_CRC32_INIT, map, len) ^
                                       (uint32_t)mz_crc32(MZ_CRC32_INIT, (const uint8_t *)p_drop, sizeof p_drop) ^
                                       (p_has_note ? (uint32_t)mz_crc32(MZ_CRC32_INIT, (const uint8_t *)p_note, sizeof p_note) : 0);
check:
    if (sig_pending) {                   /* (a saved game: the level as it was when saved?) */
        sig_pending = 0;
        sig_differs = sig_saved != port_patch_sig;
        if (sig_differs) port_log("saved game: made with %s; the level is %s now",
                                  sig_saved ? "patches" : "no patches", port_patch_sig ? "patched differently" : "not patched");
    }
}

/* ---- Game setup > Patches: which are there, and which are switched off */
int patch_enabled(const char *file)
{
    int i;
    for (i = 0; i < cfg.n_patch_off; i++) if (!strcasecmp_port(cfg.patch_off[i], file)) return 0;
    return 1;
}
void patch_set_enabled(const char *file, int on)
{
    int i;
    for (i = 0; i < cfg.n_patch_off; i++)
        if (!strcasecmp_port(cfg.patch_off[i], file)) {
            if (on) { memmove(cfg.patch_off[i], cfg.patch_off[i + 1], (size_t)(cfg.n_patch_off - i - 1) * sizeof cfg.patch_off[0]); cfg.n_patch_off--; }
            return;
        }
    if (!on && cfg.n_patch_off < 32) snprintf(cfg.patch_off[cfg.n_patch_off++], sizeof cfg.patch_off[0], "%s", file);
}
int patch_list(PatchInfo *out, int max)
{
    static char names[256][128];
    char dir[1100];
    int n, i, j, k = 0;
    snprintf(dir, sizeof dir, "%s", game_path(PATCH_FOLDER));
    n = port_list_files(dir, names, 256);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcasecmp_port(names[j - 1], names[j]) > 0; j--) {
            char t[128];
            memcpy(t, names[j], 128); memcpy(names[j], names[j - 1], 128); memcpy(names[j - 1], t, 128);
        }
    for (i = 0; i < n && k < max; i++) {
        mz_zip_archive z;
        char path[1300], *ini, *line, *next;
        size_t len = 0;
        PatchInfo *p = &out[k];
        if (!ends_with(names[i], ".pat")) continue;
        memset(p, 0, sizeof *p);
        snprintf(p->file, sizeof p->file, "%s", names[i]);
        snprintf(p->name, sizeof p->name, "%s", names[i]);
        {   /* (the name without .pat, until PATCH.INI says better) */
            char *dot = strrchr(p->name, '.');
            if (dot) *dot = 0;
        }
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        memset(&z, 0, sizeof z);
        if (mz_zip_reader_init_file(&z, path, 0)) {
            ini = (char *)mz_zip_reader_extract_file_to_heap(&z, "PATCH.INI", &len, 0);
            mz_zip_reader_end(&z);
            if (ini) {
                char *t2 = (char *)realloc(ini, len + 1);
                if (t2) {
                    ini = t2;
                    ini[len] = 0;
                    for (line = ini; line && *line; line = next) {
                        char *t, *eq;
                        next = strchr(line, '\n');
                        if (next) *next++ = 0;
                        t = trim(line);
                        if (*t == '[') { p->maps++; continue; }
                        if (!(eq = strchr(t, '='))) continue;
                        *eq = 0;
                        if (!p->maps && !strcasecmp_port(trim(t), "name") && *trim(eq + 1)) snprintf(p->name, sizeof p->name, "%s", trim(eq + 1));
                        else if (!p->maps && !strcasecmp_port(trim(t), "author")) snprintf(p->author, sizeof p->author, "%s", trim(eq + 1));
                    }
                }
                free(ini);
            }
        }
        k++;
    }
    return k;
}

/* a level's backdrop from a patch: the file name (also for the replacement-
   art lookup), and its own picture when it has one; NULL: none */
const char *patch_backdrop(int which, const uint8_t **data, long *len)
{
    *data = NULL;
    *len = 0;
    if (!port_patched || !p_drop[which][0]) return NULL;
    *data = p_drop_data[which];
    *len = p_drop_len[which];
    return p_drop[which];
}

const char (*patch_note(int index))[PACK_NOTE_W + 1]
{
    return port_patched && p_has_note && index == level_index ? (const char (*)[PACK_NOTE_W + 1])p_note : NULL;
}

/* saved games: a patched level's signature goes after the original's data
   (which the DOS game ignores); restoring checks it against the level now */
void patch_save_note(void *fp)
{
    FILE *f = (FILE *)fp;
    if (port_patch_sig) fprintf(f, "techbot_patches = %08X\n", (unsigned)port_patch_sig);
}
void patch_restore_note(void *fp)
{
    FILE *f = (FILE *)fp;
    char line[80];
    sig_saved = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned v;
        if (sscanf(line, " techbot_patches = %x", &v) == 1) sig_saved = v;
    }
    sig_pending = 1;
    sig_differs = 0;
}

/* after a restored game's level is read: a word if its patches differ */
void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);
void ui_wait_cursor(int x, int row);
void patch_restore_warning(void)
{
    if (!sig_differs) return;
    sig_differs = 0;
    ui_box(2, 1, 3);
    ui_text(sig_saved ? "This game was saved with" : "This game was saved", 3, 3);
    ui_text(sig_saved ? "other level patches." : "before a level patch.", 3, 4);
    ui_text("Duke may stand somewhere", 3, 5);
    ui_text("the map has changed.", 3, 6);
    ui_text("Press ENTER:", 3, 8);
    do { ui_wait_cursor(0x10, 8); } while (last_scancode != 0x1C);
    in_right = 0; in_down = 0;
    set_draw_page(!draw_page);
    set_display_page(draw_page);
    timer_ticks = 0;
}
