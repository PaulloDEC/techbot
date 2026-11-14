/*
 * port_art.c - replacement art from the "widescreen" subfolder.
 *
 * In Enhanced widescreen, a PNG in <game folder>/widescreen/ named after an
 * original picture or backdrop file plus ".png" (DN.DN1.png, DROP3.DN2.png -
 * the names tools/extract_images.py writes) is shown in its place:
 *   - full-screen pictures (title, ending, Dr. Proton, Duke, credits) fill
 *     the wide screen, with anything the game draws over them (menus,
 *     dialogs) laid on top;
 *   - level backdrops fill the wide play area.
 * Any size and any colours: art 200, 400 or 600 lines tall (backdrops also
 * 160, 320 or 480) is taken to be in the game's own pixels, shown 1.2x as
 * tall as wide like the originals; anything else is taken to have square
 * pixels (e.g. 1920x1080).  Art is scaled to fill the area and cropped
 * evenly at the sides (or top and bottom) if its shape differs.
 *
 * Difference art: a PNG with a "techbot-base" text chunk (written by
 * tools/art_diff.py: "DROP9.DN1.png 1 52 0" - an original's file, an
 * enlargement and a position) holds only what the wide art adds to that
 * original; its see-through pixels there are the original's.  The original
 * is decoded from the player's own game file and laid under it here, in
 * memory, so nothing of the game ships with the art and nothing is written.
 * If that game file isn't there (an episode the player doesn't have), the
 * art isn't used.
 *
 * Fades: the game fades by setting its 16 palette slots to black (or grey)
 * one at a time.  Each art pixel is given the nearest of the 16 EGA colours;
 * while that slot holds its normal colour the pixel shows its own colour,
 * otherwise the slot's current colour - so EGA-palette art fades exactly as
 * the original, and other art dissolves in step with it.
 */
#include "dn1.h"
#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_UNUSED
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static const uint8_t default_pal[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };

#define MAX_ART 24
typedef struct {
    char name[24];               /* original file name, upper case ("DROP3.DN2") */
    int missing;                 /* looked for, not there                         */
    Art art;
    uint8_t *slot;               /* nearest EGA slot per pixel                    */
    uint8_t pal[16];             /* palette the current pixels were made for      */
    int made;
} Entry;
static Entry cache[MAX_ART];
static int ncache;

static int nearest_slot(uint32_t argb)
{
    int i, best = 0;
    long bd = 0x7FFFFFFF;
    for (i = 0; i < 16; i++) {
        uint32_t c = ega_rgb(default_pal[i]);
        long dr = (long)((argb >> 16) & 255) - (long)((c >> 16) & 255);
        long dg = (long)((argb >> 8) & 255) - (long)((c >> 8) & 255);
        long db = (long)(argb & 255) - (long)(c & 255);
        long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Art in the game's own pixels (shown 1.2x as tall as wide, unsmoothed) is
 * recognised by its height: 1x to 3x the original's - 200 lines for pictures,
 * 160 (the Wide play area) or 200 (the Immersive one) for backdrops.  Taller
 * art - 720, 800, 960, 1080 lines and so on - has square pixels. */
static int game_pixel_height(int h, int kind)
{
    int k;
    for (k = 1; k <= 3; k++)
        if (h == 200 * k || (kind == ART_BACKDROP && h == 160 * k)) return 1;
    return 0;
}

/* a PNG's tEXt chunk `key`, into out (empty if none) */
static void png_text(const uint8_t *d, long n, const char *key, char *out, int outn)
{
    long p = 8;
    size_t kl = strlen(key);
    out[0] = 0;
    while (p + 12 <= n) {
        long len = (long)((uint32_t)d[p] << 24 | (uint32_t)d[p + 1] << 16 | (uint32_t)d[p + 2] << 8 | d[p + 3]);
        const uint8_t *t = d + p + 4, *body = d + p + 8;
        if (len < 0 || p + 12 + len > n) return;
        if (!memcmp(t, "tEXt", 4) && (size_t)len > kl && !memcmp(body, key, kl) && body[kl] == 0) {
            long m = len - (long)kl - 1;
            if (m > outn - 1) m = outn - 1;
            memcpy(out, body + kl + 1, (size_t)m);
            out[m] = 0;
            return;
        }
        if (!memcmp(t, "IEND", 4)) return;
        p += 12 + len;
    }
}

/* an original picture (320x200) or backdrop (208x160) from the game folder,
 * as EGA colour slots 0..15 (tools/extract_images.py decodes them the same way) */
static uint8_t *original_pixels(const char *name, int *w, int *h)
{
    FILE *f = fopen(game_path(name), "rb");
    uint8_t *d, *px;
    long n;
    int x, y, p;
    char up[24];
    for (x = 0; name[x] && x < 23; x++) up[x] = (char)toupper((unsigned char)name[x]);
    up[x] = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f);
    if (!strncmp(up, "DROP", 4)) {               /* tiles: 3-byte header, then per 8 pixels mask + 4 planes; 13 x 10 of 16x16 */
        static uint8_t pl[4][0x5140 / 4];
        int i;
        for (i = 0; i < 0x5140 / 4; i++)
            for (p = 0; p < 4; p++) { long o = 3 + (long)i * 5 + 1 + p; pl[p][i] = o < n ? d[o] : 0xFF; }
        *w = 208; *h = 160;
        px = (uint8_t *)malloc(208 * 160);
        if (px)
            for (y = 0; y < 160; y++)
                for (x = 0; x < 208; x++) {
                    int t = (y >> 4) * 13 + (x >> 4), o = t * 32 + (y & 15) * 2 + ((x & 15) >> 3), s = 7 - (x & 7), v = 0;
                    for (p = 0; p < 4; p++) v |= ((pl[p][o] >> s) & 1) << p;
                    px[y * 208 + x] = (uint8_t)v;
                }
    } else {                                     /* a picture: four 8000-byte planes, 40 bytes a row */
        *w = 320; *h = 200;
        px = (uint8_t *)malloc(320 * 200);
        if (px)
            for (y = 0; y < 200; y++)
                for (x = 0; x < 320; x++) {
                    int o = y * 40 + (x >> 3), s = 7 - (x & 7), v = 0;
                    for (p = 0; p < 4; p++) { long q = (long)p * 8000 + o; v |= (q < n ? (d[q] >> s) & 1 : 0) << p; }
                    px[y * 320 + x] = (uint8_t)v;
                }
    }
    free(d);
    return px;
}

static void load(Entry *e, int kind)
{
    char path[64], base[96];
    FILE *f;
    int w, h, n, i;
    unsigned char *px;
    uint8_t *file;
    long len;
    snprintf(path, sizeof path, "widescreen/%s.png", e->name);
    f = fopen(game_path(path), "rb");
    if (!f) { e->missing = 1; return; }
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    file = (uint8_t *)malloc(len > 0 ? (size_t)len : 1);
    if (!file || fread(file, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(file); e->missing = 1; return; }
    fclose(f);
    png_text(file, len, "techbot-base", base, sizeof base);
    px = stbi_load_from_memory(file, (int)len, &w, &h, &n, 4);
    free(file);
    if (!px || w < 8 || h < 8) { if (px) stbi_image_free(px); e->missing = 1; return; }
    if (base[0]) {                               /* difference art: the original from the game files, under it */
        char oname[64];
        int sc = 1, dx = 0, dy = 0, ow, oh, x, y;
        uint8_t *op;
        if (sscanf(base, "%63s %d %d %d", oname, &sc, &dx, &dy) < 4 || sc < 1 || sc > 16) {
            port_log("art: %s: can't read its techbot-base note (%s)", path, base);
            stbi_image_free(px); e->missing = 1; return;
        }
        {   /* (the note names the original's PNG: DROP9.DN1.png) */
            size_t L = strlen(oname);
            if (L > 4 && !strcasecmp_port(oname + L - 4, ".png")) oname[L - 4] = 0;
        }
        op = original_pixels(oname, &ow, &oh);
        if (!op) {
            port_log("art: %s needs %s from the game folder, which isn't there: not used", path, oname);
            stbi_image_free(px); e->missing = 1; return;
        }
        for (y = 0; y < oh * sc; y++)
            for (x = 0; x < ow * sc; x++) {
                int X = dx + x, Y = dy + y;
                unsigned char *q;
                uint32_t c;
                if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                q = px + ((size_t)Y * w + X) * 4;
                if (q[3]) continue;              /* (the art's own pixel) */
                c = ega_rgb(default_pal[op[(y / sc) * ow + x / sc] & 15]);
                q[0] = (unsigned char)(c >> 16); q[1] = (unsigned char)(c >> 8); q[2] = (unsigned char)c; q[3] = 255;
            }
        free(op);
    }
    e->art.w = w; e->art.h = h;
    e->art.game_pixels = game_pixel_height(h, kind);
    e->art.aspect = e->art.game_pixels ? (double)w / (h * 1.2) : (double)w / h;
    e->art.orig = (uint32_t *)malloc((size_t)w * h * 4);
    e->art.pixels = (uint32_t *)malloc((size_t)w * h * 4);
    e->slot = (uint8_t *)malloc((size_t)w * h);
    if (!e->art.orig || !e->art.pixels || !e->slot) { stbi_image_free(px); e->missing = 1; return; }
    for (i = 0; i < w * h; i++) {
        const unsigned char *p = px + i * 4;
        uint32_t c = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        e->art.orig[i] = c;
        e->slot[i] = (uint8_t)nearest_slot(c);
    }
    stbi_image_free(px);
}

const Art *art_get(const char *orig_name, int kind)
{
    char up[24];
    int i;
    Entry *e;
    if (!orig_name || !*orig_name) return NULL;
    for (i = 0; orig_name[i] && i < 23; i++) up[i] = (char)toupper((unsigned char)orig_name[i]);
    up[i] = 0;
    for (i = 0; i < ncache; i++)
        if (!strcmp(cache[i].name, up)) { e = &cache[i]; goto found; }
    if (ncache == MAX_ART) return NULL;
    e = &cache[ncache++];
    memset(e, 0, sizeof *e);
    strcpy(e->name, up);
    load(e, kind);
found:
    if (e->missing) return NULL;
    /* the pixels for the current palette (see the fades note above) */
    if (!e->made || memcmp(e->pal, ega_palette, 16)) {
        int n = e->art.w * e->art.h, s;
        uint32_t now[16];
        int same[16];
        for (s = 0; s < 16; s++) { same[s] = ega_palette[s] == default_pal[s]; now[s] = ega_rgb(ega_palette[s]); }
        for (i = 0; i < n; i++) {
            s = e->slot[i];
            e->art.pixels[i] = same[s] ? e->art.orig[i] : (e->art.orig[i] & 0xFF000000u) | now[s];
        }
        memcpy(e->pal, ega_palette, 16);
        e->made = 1;
        e->art.serial++;
    }
    return &e->art;
}

/* ---------------------------------------------------------------- what is on screen */
int port_picture = -1;           /* full-screen picture being shown (0..4), -1 none */

/* the original file names of the current level's two backdrops (the game's
 * own table at DS:0B47, indexed by level - the second one is the next entry).
 * level_index counts levels (even) and the hallways after them (odd); the
 * game loads backdrops only when a level starts (load_level), so a hallway
 * shows the pair of the level before it. */
const char *art_backdrop_name(int which)
{
    if (port_drop[which].name[0]) return port_drop[which].name;
    if (pack.on) return pack_backdrop(level_index, which, NULL, NULL);
    return ds_farstr((uint16_t)(0x0B47 + ((level_index & ~1) + which) * 4));
}
