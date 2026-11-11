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

static void load(Entry *e, int kind)
{
    char path[64];
    FILE *f;
    int w, h, n, i;
    unsigned char *px;
    snprintf(path, sizeof path, "widescreen/%s.png", e->name);
    f = fopen(game_path(path), "rb");
    if (!f) { e->missing = 1; return; }
    px = stbi_load_from_file(f, &w, &h, &n, 4);
    fclose(f);
    if (!px || w < 8 || h < 8) { if (px) stbi_image_free(px); e->missing = 1; return; }
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
