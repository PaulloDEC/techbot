/*
 * ega.c - exact emulation of the EGA hardware behaviour DN1 relies on, plus
 * C versions of the hand-written graphics primitives from the original's
 * startup segment (seg 0000: 02AD, 0315, 0424, 04C5, 053C, 05EB, 05F7).
 *
 * Original memory map (mode 0Dh, 320x200x16, planar, 40 bytes per line):
 *   A000:0000  display page 0   (8000 bytes per plane)
 *   A000:2000  display page 1
 *   A000:4000  tile cache       (map cell values are byte offsets into it;
 *                                each 16x16 tile = 32 bytes per plane, so
 *                                tile index = cell / 32)
 * Sprites, fonts and status-bar graphics live in system RAM in the
 * 5-planes-per-8-pixels format (mask,B,G,R,I) and are blitted with the
 * bit-mask register acting as transparency.
 *
 * Keeping this layer exact means the decompiled game logic renders pixel-
 * identically to the original. A port can later replace these functions
 * with a true-colour / widescreen renderer (see PORTING.md).
 */
#include "dn1.h"
#include <string.h>

uint8_t  ega_planes[4][0x10000];
static uint8_t ega_latch[4];
static uint8_t seq_index, map_mask = 0x0F;
static uint8_t gc_index, gc[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0xFF};
/* mode 0Dh is a 200-line mode: the attribute controller output is read as
 * CGA-style RGBI (bit 4 = intensity) and colour 6 is shown as brown */
uint8_t  ega_palette[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
uint8_t  ega_border;
int      ega_draw_page;            /* CS:0247 in the original                 */
int      ega_display_page;

/* (port) quicksave: the adapter's internal registers, beyond VRAM itself */
size_t ega_regs_size(void) { return 4 + 1 + 1 + 1 + 9 + 16 + 1 + 2; }
void ega_regs_get(uint8_t *b)
{
    memcpy(b, ega_latch, 4); b[4] = seq_index; b[5] = map_mask; b[6] = gc_index;
    memcpy(b + 7, gc, 9); memcpy(b + 16, ega_palette, 16); b[32] = ega_border;
    b[33] = (uint8_t)ega_draw_page; b[34] = (uint8_t)ega_display_page;
}
void ega_regs_set(const uint8_t *b)
{
    memcpy(ega_latch, b, 4); seq_index = b[4]; map_mask = b[5]; gc_index = b[6];
    memcpy(gc, b + 7, 9); memcpy(ega_palette, b + 16, 16); ega_border = b[32];
    ega_draw_page = b[33]; ega_display_page = b[34];
}

uint8_t ega_read(uint16_t off)
{
    int p;
    for (p = 0; p < 4; p++) ega_latch[p] = ega_planes[p][off];
    return ega_planes[gc[4] & 3][off];
}

/* (port) which bytes of page 0 the game has drawn since a full-screen picture
 * was put there: replacement art shows wherever it has not (port_art.c) */
uint8_t ega_written[8000];

void ega_write(uint16_t off, uint8_t val)
{
    int p, mode = gc[5] & 3, rot = gc[3] & 7, func = (gc[3] >> 3) & 3;
    uint8_t mask = gc[8];
    if (off < 8000) ega_written[off] = 1;
    if (rot) val = (uint8_t)((val >> rot) | (val << (8 - rot)));
    for (p = 0; p < 4; p++) {
        uint8_t d, l;
        if (!((map_mask >> p) & 1)) continue;
        if (mode == 1) { ega_planes[p][off] = ega_latch[p]; continue; }
        if (mode == 0) d = ((gc[1] >> p) & 1) ? (((gc[0] >> p) & 1) ? 0xFF : 0) : val;
        else if (mode == 2) d = ((val >> p) & 1) ? 0xFF : 0;
        else {
            uint8_t m2 = mask & val;
            d = ((gc[0] >> p) & 1) ? 0xFF : 0;
            ega_planes[p][off] = (uint8_t)((d & m2) | (ega_latch[p] & ~m2));
            continue;
        }
        l = ega_latch[p];
        if (func == 1) d &= l; else if (func == 2) d |= l; else if (func == 3) d ^= l;
        ega_planes[p][off] = (uint8_t)((d & mask) | (l & ~mask));
    }
}

/* Turbo C outportb()/outport() to the EGA register ports */
void ega_outb(uint16_t port, uint8_t v)
{
    switch (port) {
    case 0x3C4: seq_index = v; break;
    case 0x3C5: if (seq_index == 2) map_mask = v & 0x0F; break;
    case 0x3CE: gc_index = v & 0x0F; break;
    case 0x3CF: if (gc_index < 9) gc[gc_index] = v; break;
    default: break;
    }
}
void ega_outw(uint16_t port, uint16_t v) { ega_outb(port, (uint8_t)v); ega_outb((uint16_t)(port + 1), (uint8_t)(v >> 8)); }

static uint16_t page_base(void) { return ega_draw_page ? 0x2000 : 0x0000; }

/* 0000:05EB */ void set_draw_page(int p)    { ega_draw_page = p; }
/* 0000:05F7 */ void set_display_page(int p) { ega_display_page = p; }
/* 0000:029D  int 10h AX=1000h */
void set_palette_reg(int reg, int val) { if (reg < 16) ega_palette[reg] = (uint8_t)val; else if (reg == 16) ega_border = (uint8_t)val; }

/* 0000:02AD - copy a 16x16 tile from the VRAM tile cache (A400:src) to the
 * draw page at byte offset dst using the latches (caller selects write
 * mode 1 beforehand). */
void blit_tile(uint16_t src, uint16_t dst)
{
    int r;
    uint16_t s = (uint16_t)(0x4000 + src), d = (uint16_t)(page_base() + dst);
    if (dl_capturing) dl_record(DL_TILE, NULL, src, dst % 40, dst / 40, 0);   /* (port) */
    else dl_note_direct((dst % 40) * 8, dst / 40, 16, 16);
    for (r = 0; r < 16; r++) {
        ega_read(s++); ega_write(d++, 0);
        ega_read(s++); ega_write(d++, 0);
        d += 0x26;
    }
}

/* 0000:0315 - latched copy of a 2-byte-wide, 30-line column within the
 * draw page: line y+2+k receives line y-1-2k (minus 'back' bytes), i.e. a
 * vertically squashed mirror image of the scene above (mirror floors, 6AFD).
 * x: byte column, y: line, back: shimmer offset (0 or 28h). */
void blit_column_up(int x, int y, int back)
{
    int r;
    uint32_t base = page_base();     /* the routine uses segment A000h or A200h */
    if (dl_capturing) dl_record(DL_MIRROR, NULL, 0, x, y, back);              /* (port) */
    else dl_note_direct(x * 8, y, 16, 32);
    if (dl_clip(DL_MIRROR, x, y, 2)) return;
    uint16_t di = (uint16_t)(y * 40 + x + 0x50);
    uint16_t si = (uint16_t)(di - 0x78 - back);
    int k;
    for (r = 0; r < 30; r++) {
        for (k = 0; k < 2; k++) {
            /* Offsets wrap inside the segment.  Near the top of page 1 the
             * source climbs past line 0 and wraps to A200:FFxx = B1Fxxh,
             * outside the EGA's 64 KiB window: that read does not load the
             * latches, so the write stores the previous (stale) latches. */
            if (base + si < 0x10000) ega_read((uint16_t)(base + si));
            if (base + di < 0x10000) ega_write((uint16_t)(base + di), 0);
            si++; di++;
        }
        di += 0x26; si -= 0x52;
    }
}

/* 0000:0424 - draw a masked 16x16 sprite (160 bytes: per 8 pixels mask,B,G,R,I)
 * at byte column x, pixel line y of the draw page. */
void draw_sprite(const uint8_t *src, int x, int y)
{
    if (dl_capturing) dl_record(DL_SPRITE, src, 0, x, y, 0);                  /* (port) */
    else dl_note_direct(x * 8, y, 16, 16);
    if (dl_clip(DL_SPRITE, x, y, 2)) return;
    int r, b, p;
    uint16_t o = (uint16_t)(y * 40 + x + page_base());
    ega_outb(0x3C4, 2);
    for (r = 0; r < 16; r++) {
        for (b = 0; b < 2; b++) {
            ega_outb(0x3CE, 8); ega_outb(0x3CF, *src++);
            for (p = 0; p < 4; p++) {
                ega_outb(0x3C5, (uint8_t)(1 << p));
                ega_read(o); ega_write(o, *src++);
            }
            o++;
        }
        o += 0x26;
    }
    ega_outb(0x3C5, 0x0F);
    ega_outb(0x3CE, 8); ega_outb(0x3CF, 0xFF);
}

/* 0000:04C5 - masked 8x8 glyph (40 bytes) */
void draw_glyph_masked(const uint8_t *src, int x, int y)
{
    if (dl_capturing) dl_record(DL_GLYPH, src, 0, x, y, 0);                   /* (port) */
    else dl_note_direct(x * 8, y, 8, 8);
    int r, p;
    uint16_t o = (uint16_t)(y * 40 + x + page_base());
    ega_outb(0x3C4, 2);
    for (r = 0; r < 8; r++) {
        ega_outb(0x3CE, 8); ega_outb(0x3CF, *src++);
        for (p = 0; p < 4; p++) {
            ega_outb(0x3C5, (uint8_t)(1 << p));
            ega_read(o); ega_write(o, *src++);
        }
        o += 0x28;
    }
    ega_outb(0x3C5, 0x0F);
    ega_outb(0x3CE, 8); ega_outb(0x3CF, 0xFF);
}

/* 0000:053C - opaque 8x8 glyph (mask byte skipped, bit mask left as is) */
void draw_glyph_opaque(const uint8_t *src, int x, int y)
{
    if (dl_capturing) dl_record(DL_GLYPH_OPAQUE, src, 0, x, y, 0);            /* (port) */
    int r, p;
    uint16_t o = (uint16_t)(y * 40 + x + page_base());
    ega_outb(0x3C4, 2);
    for (r = 0; r < 8; r++) {
        src++;
        for (p = 0; p < 4; p++) {
            ega_outb(0x3C5, (uint8_t)(1 << p));
            ega_read(o); ega_write(o, *src++);
        }
        o += 0x28;
    }
    ega_outb(0x3C5, 0x0F);
    ega_outb(0x3CE, 8); ega_outb(0x3CF, 0xFF);
}

/* palette register value (200-line mode: RGBI, bit 4 = intensity) -> 0xRRGGBB */
uint32_t ega_rgb(uint8_t v)
{
    static const uint32_t cga[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };
    return cga[(v & 7) | ((v & 0x10) ? 8 : 0)];
}

/* Convert the displayed page to 320x200 32-bit pixels. */
void ega_present(uint32_t *out)
{
    int y, xb, bit;
    uint16_t base = ega_display_page ? 0x2000 : 0;
    uint32_t pal[16];
    for (y = 0; y < 16; y++) pal[y] = ega_rgb(ega_palette[y]);
    for (y = 0; y < 200; y++)
        for (xb = 0; xb < 40; xb++) {
            uint16_t o = (uint16_t)(base + y * 40 + xb);
            uint8_t b0 = ega_planes[0][o], b1 = ega_planes[1][o], b2 = ega_planes[2][o], b3 = ega_planes[3][o];
            for (bit = 0; bit < 8; bit++) {
                int s = 7 - bit;
                int c = ((b0 >> s) & 1) | (((b1 >> s) & 1) << 1) | (((b2 >> s) & 1) << 2) | (((b3 >> s) & 1) << 3);
                out[y * 320 + xb * 8 + bit] = pal[c];
            }
        }
}

/* 4-bit index version (used by the test harness to compare frames) */
void ega_present_indexed(uint8_t *out, int page)
{
    int y, xb, bit;
    uint16_t base = page ? 0x2000 : 0;
    for (y = 0; y < 200; y++)
        for (xb = 0; xb < 40; xb++) {
            uint16_t o = (uint16_t)(base + y * 40 + xb);
            uint8_t b0 = ega_planes[0][o], b1 = ega_planes[1][o], b2 = ega_planes[2][o], b3 = ega_planes[3][o];
            for (bit = 0; bit < 8; bit++) {
                int s = 7 - bit;
                out[y * 320 + xb * 8 + bit] = (uint8_t)(((b0 >> s) & 1) | (((b1 >> s) & 1) << 1) | (((b2 >> s) & 1) << 2) | (((b3 >> s) & 1) << 3));
            }
        }
}
