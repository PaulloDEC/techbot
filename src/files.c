/*
 * files.c - loading the original data files, demos, settings, saves.
 *
 * Every file name comes from the original executable's data segment (read
 * at runtime), so this module contains no game data itself.
 */
#include "dn1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *mapbuf, *font_gfx, *object_gfx, *numbers_gfx, *man_gfx, *anim_gfx;
uint8_t *heap_arena;
size_t   heap_arena_size;
uint8_t *demo_user, *demo_my, *sound_bank;
uint8_t *screen_img[6];

static FILE *gopen(const char *name, const char *mode) { return fopen(game_path(name), mode); }

/* 0097:10FB  tile file (5 planes per 8 px, 3-byte header) -> VRAM tile cache.
 * 'bytes' is the per-call budget; one iteration writes one byte to each of
 * the 4 planes (the mask byte is skipped).  Reading past EOF yields FFh,
 * exactly like Turbo C's fgetc() returning EOF. */
int load_tiles(const char *name, uint16_t vram, int bytes)   /* (port) 0 = no such file */
{
    FILE *f;
    int i, n = bytes / 4, p;
    if (!name || !*name) return 0;
    f = gopen(name, "rb");
    if (!f) return 0;
    fgetc(f); fgetc(f); fgetc(f);
    for (i = 0; i < n; i++) {
        fgetc(f);
        for (p = 0; p < 4; p++) {
            int c = fgetc(f);
            ega_planes[p][(uint16_t)(0x4000 + vram + i)] = (uint8_t)c;
        }
    }
    fclose(f);
    return 1;
}

/* (port) the same from a file in memory (a pack's backdrop) */
void load_tiles_mem(const uint8_t *data, long len, uint16_t vram, int bytes)
{
    int i, n = bytes / 4, p;
    long at = 4;                         /* the header, then each group's mask byte skipped */
    for (i = 0; i < n; i++, at++)        /* past the end: 0FFh, as fgetc's EOF is in load_tiles */
        for (p = 0; p < 4; p++, at++) ega_planes[p][(uint16_t)(0x4000 + vram + i)] = at < len ? data[at] : 0xFF;
}

PortDrop port_drop[2];                   /* (port) see port.h */

/* 0097:295E  sprite file -> RAM (skip 3-byte header) */
static void load_raw(const char *name, uint8_t *dst, int bytes)
{
    FILE *f = gopen(name, "rb");
    if (!f) return;
    fgetc(f); fgetc(f); fgetc(f);
    { size_t n = fread(dst, (size_t)bytes, 1, f); (void)n; }
    fclose(f);
}

/* 108E:30B0 etc.  32000-byte planar full-screen pictures */
static uint8_t *load_screen(const char *name)
{
    FILE *f = gopen(name, "rb");
    uint8_t *b = (uint8_t *)calloc(1, 0x7D01);
    if (f) { size_t n = fread(b, 0x7D00, 1, f); (void)n; fclose(f); }
    return b;
}

/* demo files: 16-bit length, then 6 bytes per frame (0097:DE71 / DEE9) */
static void load_demo(const char *name, uint8_t *buf, uint16_t *len, int user)
{
    FILE *f = gopen(pack_file_name(name), "rb");
    if (!f) { demo_available = 0; if (user) demo_missing = 1; return; }
    if (user) demo_missing = 0;
    *len = (uint16_t)(fgetc(f) | (fgetc(f) << 8));
    { size_t n = fread(buf, 0x4268, 1, f); (void)n; }
    demo_available = 1;
    fclose(f);
}
static void save_demo(const char *name, const uint8_t *buf, uint16_t len)
{
    FILE *f = gopen(pack_file_name(name), "wb");
    if (!f) return;
    fputc(len & 0xFF, f); fputc(len >> 8, f);
    fwrite(buf, 0x4268, 1, f);
    fclose(f);
}

/* 0097:E00D  allocate buffers and load all graphics */
void ui_fade_out(int d);
void ui_fade_in(int d);
void ui_loading(void);
void ui_clear_page0(void);
void ui_apogee(void);
/* (port) the two cards shown while the game loads (LOADING and the Apogee
 * one) were on screen for as long as the disk took; on a modern machine they
 * flash past.  Techbot holds each at least this many timer ticks (145 Hz),
 * or until a key; 0 (the editor, the test build) = as the original. */
int port_card_hold;
static uint16_t hold_from;
static void hold_card(uint16_t from)
{
    if (!port_card_hold) return;
    last_scancode = 0;
    SPIN_WHILE((uint16_t)(timer_ticks - from) < port_card_hold && (last_scancode == 0 || (last_scancode & 0x80)));
}

void load_graphics(void)
{
    load_tiles(ds_str(0x08D6), 0x3000, 0x1E00);                 /* border  */
    {
        /* The eight farmalloc() blocks are laid out exactly like Turbo C's
         * far heap: an 8-byte header (size|1, 0, previous block's segment)
         * then the data, each block rounded up to a paragraph.  This matters:
         * map reads with out-of-range coordinates wrap inside the map's 64 KiB
         * segment and land in the blocks that follow it (see QUIRKS.md), and
         * sprite reads past a buffer's end hit the next block. */
        static const uint32_t sizes[8] = { 0x5DC0, 0x1194, 0x5DCA, 0x1F41,
                                           0x84D1, 0xBB81, 0x4650, 0x4650 };
        uint8_t **dst[8] = { &mapbuf, &font_gfx, &object_gfx, &numbers_gfx,
                             &man_gfx, &anim_gfx, &demo_user, &demo_my };
        uint32_t total = 0, pos = 0; int i;
        uint16_t seg = HEAP_MAP_SEG, prev = HEAP_MAP_SEG - 2;
        for (i = 0; i < 8; i++) total += (sizes[i] + 8 + 15) & ~15u;
        heap_arena_size = total + 0x10000;
        heap_arena = (uint8_t *)calloc(1, heap_arena_size);   /* + slack past the last block */
        for (i = 0; i < 8; i++) {
            uint32_t blk = (sizes[i] + 8 + 15) & ~15u;
            uint8_t *h = heap_arena + pos;
            h[0] = (uint8_t)(blk | 1); h[1] = (uint8_t)(blk >> 8);
            h[6] = (uint8_t)prev;      h[7] = (uint8_t)(prev >> 8);
            *dst[i] = h + 8;
            prev = seg; seg = (uint16_t)(seg + blk / 16); pos += blk;
        }
    }
    load_raw(ds_str(0x08E1), font_gfx, 0x7D0);
    load_raw(ds_str(0x08EB), font_gfx + 0x7D0, 0x7D0);
    ui_fade_out(5);
    ui_loading();
    ui_fade_in(5);
    hold_from = timer_ticks;
    load_raw(ds_str(0x08F5), numbers_gfx, 0x1F40);
    load_raw(ds_str(0x0901), object_gfx, 0x1F40);
    load_raw(ds_str(0x090D), object_gfx + 0x1F40, 0x1F40);
    load_raw(ds_str(0x0919), object_gfx + 0x3E80, 0x1F40);
    load_demo(ds_str(0x0925), demo_user, &demo_len_user, 1);
    load_demo(ds_str(0x0932), demo_my, &demo_len_my, 0);
    load_raw(ds_str(0x093E), man_gfx, 0x1E00);
    load_raw(ds_str(0x0947), man_gfx + 0x1E00, 0x1E00);
    load_raw(ds_str(0x0950), man_gfx + 0x3C00, 0x1E00);
    load_raw(ds_str(0x0959), man_gfx + 0x5A00, 0x1E00);
    load_tiles(ds_str(0x0962), 0x0000, 0x1E00);
    load_tiles(ds_str(0x096C), 0x0600, 0x1E00);
    load_tiles(ds_str(0x0976), 0x0C00, 0x1E00);
    load_tiles(ds_str(0x0980), 0x1200, 0x1E00);
    hold_card(hold_from);
    ui_fade_out(5);
    ui_clear_page0();
    load_tiles(ds_str(0x098A), 0x1800, 0x1E00);
    load_tiles(ds_str(0x0995), 0x1E00, 0x1E00);
    ui_apogee();
    ui_fade_in(5);
    hold_from = timer_ticks;
    load_tiles(ds_str(0x09A0), 0x2400, 0x1E00);
    load_tiles(ds_str(0x09AB), 0x2A00, 0x1E00);
    load_tiles(ds_str(0x09B6), 0x3000, 0x1E00);
    load_raw(ds_str(0x09C1), anim_gfx, 0x1E00);
    load_raw(ds_str(0x09CB), anim_gfx + 0x1E00, 0x1E00);
    load_raw(ds_str(0x09D5), anim_gfx + 0x3C00, 0x1E00);
    load_raw(ds_str(0x09DF), anim_gfx + 0x5A00, 0x1E00);
    load_raw(ds_str(0x09E9), anim_gfx + 0x7800, 0x1E00);
    load_raw(ds_str(0x09F3), anim_gfx + 0x9600, 0x1E00);
    hold_card(hold_from);
    ui_fade_out(5);
}

/* full-screen pictures (108E:30B0, 3252, 33AB, 3504, 365D) */
static const uint16_t screen_name_off[5] = { 0x0AD1, 0x0AD8, 0x0AE0, 0x0AEB, 0x0AF4 };
void load_screens(void)
{
    screen_img[0] = load_screen(ds_str(0x0AD1));   /* title            */
    screen_img[1] = load_screen(ds_str(0x0AD8));   /* end              */
    screen_img[2] = load_screen(ds_str(0x0AE0));   /* bad guy          */
    screen_img[3] = load_screen(ds_str(0x0AEB));   /* duke             */
    screen_img[4] = load_screen(ds_str(0x0AF4));   /* credits          */
}
/* (port) the END picture again: a pack's ending borrowed from another episode
 * shows that episode's (the others are the same in all three) */
void reload_end_screen(void)
{
    uint8_t *b = load_screen(ds_str(0x0AD8));
    free(screen_img[1]);
    screen_img[1] = b;
}
/* (port) for replacement art (port_art.c) */
const char *picture_name(int idx) { return idx >= 0 && idx < 5 ? ds_str(screen_name_off[idx]) : NULL; }
const uint8_t *screen_img_get(int idx) { return idx >= 0 && idx < 5 ? screen_img[idx] : NULL; }

/* 108E:2104 / 205F  KEYS.DN1 */
void load_keys(const char *name)
{
    FILE *f = gopen(name, "rb");
    int i;
    if (!f) return;
    for (i = 0; i < 6; i++) key_bind[i] = (uint8_t)fgetc(f);
    fclose(f);
}
int port_no_disk;                        /* (port) see port.h */
int port_sim;                            /* (port) see port.h */
int port_boxes_open;                     /* (port) see port.h */
void save_keys(const char *name)
{
    if (port_no_disk) return;            /* (port) a play-test in Shrapnel */
    FILE *f = gopen(name, "wb");
    int i;
    if (!f) return;
    for (i = 0; i < 6; i++) fputc(key_bind[i], f);
    fclose(f);
}

/* 0097:E89D / E8ED  SPEED.DN1 (Turbo C getw/putw) */
void load_speed(const char *name)
{
    FILE *f = gopen(name, "rb");
    if (!f) { game_speed = 0; return; }
    game_speed = (int16_t)(fgetc(f) | (fgetc(f) << 8));
    fclose(f);
}
void save_speed(const char *name)
{
    if (port_no_disk) return;            /* (port) a play-test in Shrapnel */
    FILE *f = gopen(name, "wb");
    if (!f) return;
    fputc(game_speed & 0xFF, f); fputc((game_speed >> 8) & 0xFF, f);
    fclose(f);
}

void save_demos(void)
{
    if (port_no_disk) return;            /* (port) a play-test in Shrapnel */
    save_demo(ds_str(0x0B22), demo_user, demo_len_user);
    save_demo(ds_str(0x0B2F), demo_my, demo_len_my);
}

/* ------------------------------------------------------------------
 * 0097:0151 save / 108E:219D restore.  SAVEDx.DN1 = 22 putw() words + the
 * score as a decimal %ld line.  The file name template lives at DS:0722
 * ("saved?.dn1", slot character at index 5).
 * ---------------------------------------------------------------- */
static void slot_name(char *out, uint16_t tmpl, uint8_t slot)
{
    char b[32];
    strcpy(b, ds_str(tmpl));
    b[5] = (char)slot;
    strcpy(out, pack_file_name(b));      /* (port) a pack's saves are its own */
}
static void putw16(int v, FILE *f) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }
static int  getw16(FILE *f) { int lo = fgetc(f), hi = fgetc(f); return (int16_t)(lo | (hi << 8)); }

/* (port) peek at slot '1'..'9' for the slot picker: 1 if the file exists */
int save_slot_info(int slot, int *lvl_index, long *sc)
{
    char name[160];
    FILE *f;
    int i;
    slot_name(name, 0x0722, (uint8_t)('0' + slot));
    if (!(f = gopen(name, "rb"))) return 0;
    for (i = 0; i < 4; i++) getw16(f);
    *lvl_index = getw16(f);
    for (i = 5; i < 22; i++) getw16(f);
    *sc = 0;
    if (fscanf(f, "%ld", sc) != 1) *sc = 0;
    fclose(f);
    return 1;
}

void save_game(uint8_t slot)
{
    if (port_no_disk) return;            /* (port) a play-test in Shrapnel */
    char name[160];
    FILE *f;
    slot_name(name, 0x0722, slot);
    f = gopen(name, "wb");
    if (!f) return;
    putw16(scroll_x, f); putw16(scroll_y, f); putw16(player_x, f); putw16(player_y, f);
    putw16(level_index, f); putw16(max_shots, f); putw16(have_boots, f); putw16(have_claws, f);
    putw16(health, f); putw16(have_robohand, f);
    putw16(hint_5a11, f); putw16(hint_5c6b, f); putw16(hint_5554, f); putw16(hint_637e, f);
    putw16(hint_5ec8, f); putw16(hint_555a, f); putw16(hint_555b, f); putw16(hint_68f7, f);
    putw16(hint_555c, f); putw16(hint_5553, f); putw16(hint_5555, f); putw16(hint_5c6a, f);
    fprintf(f, ds_str(0x0730), (long)score);
    patch_save_note(f);                  /* (port) a patched level: which patches (nothing if unpatched) */
    fclose(f);
}

void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);
int restore_game(int slot)
{
    char name[160];
    uint8_t first = last_scancode;
    FILE *f;
    long sc = 0;
    slot_name(name, 0x37C9, (uint8_t)slot);
    f = gopen(name, "rb");
    if (!f) {
        ui_box(7, 2, 1);
        ui_text(ds_str(0x37D7), 7, 5);
        SPIN_WHILE(last_scancode == first);
        last_scancode = 0;
        SPIN_WHILE(last_scancode == 0);
        quit_flag = 1;
        return 0;
    }
    quit_flag = 0;
    scroll_x = (int16_t)getw16(f); scroll_y = (int16_t)getw16(f);
    player_x = (int16_t)getw16(f); player_y = (int16_t)getw16(f);
    level_index = (uint8_t)getw16(f);
    max_shots = (int16_t)getw16(f); have_boots = (int16_t)getw16(f); have_claws = (int16_t)getw16(f);
    health = (int16_t)getw16(f); have_robohand = (int16_t)getw16(f);
    hint_5a11 = (uint8_t)getw16(f); hint_5c6b = (uint8_t)getw16(f); hint_5554 = (uint8_t)getw16(f);
    hint_637e = (uint8_t)getw16(f); hint_5ec8 = (uint8_t)getw16(f); hint_555a = (uint8_t)getw16(f);
    hint_555b = (uint8_t)getw16(f); hint_68f7 = (uint8_t)getw16(f); hint_555c = (uint8_t)getw16(f);
    hint_5553 = (uint8_t)getw16(f); hint_5555 = (uint8_t)getw16(f); hint_5c6a = (uint8_t)getw16(f);
    if (!hints_on) {
        hint_5c6a = hint_5555 = hint_5553 = hint_555c = hint_68f7 = hint_555b = 0;
        hint_555a = hint_5ec8 = hint_637e = hint_5554 = hint_5c6b = hint_5a11 = 0;
    }
    if (fscanf(f, ds_str(0x37ED), &sc) == 1) score = (int32_t)sc;
    patch_restore_note(f);               /* (port) the level's patches when it was saved */
    fclose(f);
    return 1;
}
