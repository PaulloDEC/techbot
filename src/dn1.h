/*
 * dn1.h - Duke Nukem (1991) episode 1, v1.0 - C reconstruction.
 *
 * Decompiled from the original Turbo C 2.0 large-model executable.  Function
 * comments give the original SEG:OFF (relative to the load module, as in
 * build/dn1.asm) so every routine can be cross-checked against the listing.
 *
 *   seg 0000  startup + hand-written EGA primitives   -> ega.c
 *   seg 0097  game module (DUKE.C ?)                  -> game.c, player.c,
 *                                                        actors*.c, render.c
 *   seg 108E  UI / dialogs / file / sound module      -> ui.c, sound.c, files.c
 *   seg 14F6+ Turbo C runtime library                 -> libc
 *
 * No data from the original is compiled in: graphics, levels, sounds, text
 * and constant tables are read from the user's own game files at runtime
 * (see exe.c).
 */
#ifndef DN1_H
#define DN1_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ records */
#pragma pack(push, 1)
typedef struct { int16_t x, y; } Obj4;                 /* position only           */
typedef struct { int16_t s, x, y; } Obj6;              /* state/type, position    */
typedef struct { int16_t x, y, on; } FField;           /* force field             */
typedef struct { int16_t s, x, y, t; } Obj8;           /* + timer/aux             */
typedef struct { int16_t s, x, y, t, u, v; } Obj12;
typedef struct { int16_t len_unused, x, y, len, dir; } Conveyor; /* s unused(0) */
typedef struct { int16_t dir, x, y, active; } EShot;   /* enemy projectile        */
typedef struct {                                        /* 4-piece debris burst    */
    int16_t t;                     /* step 0,2,4..0x16 (index into delta tables) */
    int16_t x0, y0, x1, y1, x2, y2, x3, y3;
    int16_t tile0, tile1, tile2, tile3;                /* sprite offsets (object) */
    int16_t pad[2];
} Debris;
#pragma pack(pop)

/* --------------------------------------------------------------- globals */
#define G(type, name, off)      extern type name;
#define GA(type, name, off, n)  extern type name[n];
#include "globals.def"
#undef G
#undef GA

/* far buffers allocated at startup (pointers lived at the given DS offsets) */
extern uint8_t  *mapbuf;          /* 7729: 128x90 cells (+slack), 64 KiB, offset-wrapped */
extern uint8_t  *font_gfx;        /* 6B54: font1+font2                                   */
extern uint8_t  *object_gfx;      /* A0FB: object0..2 sprites                            */
extern uint8_t  *numbers_gfx;     /* A0EF: numbers.dn1                                   */
extern uint8_t  *man_gfx;         /* 5EC4: man0..3 (player)                              */
extern uint8_t  *anim_gfx;        /* 637A: anim0..5                                      */
extern uint8_t  *demo_user;       /* 9684: USERDEMO buffer                               */
extern uint8_t  *demo_my;         /* 5A0D: MY_DEMO buffer                                */
extern uint8_t  *sound_bank;      /* D714                                                */
extern uint8_t  *screen_img[6];   /* full-screen pictures (dn, end, badguy, duke, credits)*/

/* map cell access, exactly as the original's 16-bit pointer arithmetic */
/* The map pointer is seg:0008 (after the heap header) and offsets wrap at
 * 64 KiB within that segment, i.e. relative to the start of heap_arena. */
#define MAP16(byteoff)   (*(uint16_t *)(heap_arena + (uint16_t)((byteoff) + 8)))
extern uint8_t  *heap_arena;      /* the original's far-heap blocks, map first */
#define HEAP_MAP_SEG     heap_map_seg   /* segment of the map block under the reference emulator (3661h in DN1) */
#define MAP_AT(x, y)     MAP16(((y) << 1) + (((x) >> 1) << 1) - 2)
#define SOLID_TILE       0x17FF     /* cells > this are solid */

/* actor update routine registry (514B / 554D) */
typedef void (*ActorFn)(void);
extern ActorFn actor_fns[100];

/* ------------------------------------------------------------- exe.c */
int         exe_load(const char *path);   /* DN1.EXE, DN2.EXE or DN3.EXE (sets episode) */
extern int  episode;                     /* 1..3 */
extern uint16_t heap_map_seg;
/* where data named by its DN1.EXE offset lives in DN2/DN3.EXE (episode_map.c) */
typedef struct {
    const uint16_t (*refs)[2]; int nrefs;   /* exact: DN1 offset -> episode offset  */
    const uint16_t (*runs)[3]; int nruns;   /* unchanged runs: dn1 start, ep start, length */
    int bss_shift;                          /* uninitialised data (>= 4ECAh)        */
} EpisodeMap;
extern const EpisodeMap episode_map_2, episode_map_3;
uint16_t    ds_episode_off(uint16_t dn1_off);
const char *ds_ep_str(uint16_t off);       /* by offset in the loaded episode's data */
const char *ds_str(uint16_t off);
uint8_t     ds_u8(uint16_t off);
uint16_t    ds_u16(uint16_t off);
const char *ds_farstr(uint16_t off);
extern uint8_t *exe_ds;

/* ------------------------------------------------------------- ega.c */
extern uint8_t ega_planes[4][0x10000];
extern uint8_t ega_palette[16];
extern int     ega_draw_page, ega_display_page;
uint8_t  ega_read(uint16_t off);
void     ega_write(uint16_t off, uint8_t val);
void     ega_outb(uint16_t port, uint8_t v);
void     ega_outw(uint16_t port, uint16_t v);
void     set_draw_page(int p);
void     set_display_page(int p);
void     set_palette_reg(int reg, int val);
void     blit_tile(uint16_t src, uint16_t dst);
void     blit_column_up(int x, int y, int back);
void     draw_sprite(const uint8_t *src, int x, int y);
void     draw_glyph_masked(const uint8_t *src, int x, int y);
void     draw_glyph_opaque(const uint8_t *src, int x, int y);
uint32_t ega_rgb(uint8_t v);
void     ega_present(uint32_t *out);
void     ega_present_indexed(uint8_t *out, int page);

#define WRITE_MODE_LATCH()  ega_outw(0x3CE, 0x105)
#define WRITE_MODE_NORMAL() ega_outw(0x3CE, 0x005)

/* ------------------------------------------------------------ platform */
void pf_service(void);              /* pump timer ISR + keyboard ISR (busy waits) */
void pf_present(void);              /* show ega_display_page                       */
void pf_delay_ms(unsigned ms);      /* Turbo C delay()                             */
int  pf_joystick_buttons(void);     /* port 201h bits 4/5 (1 = not pressed)        */
void pf_speaker(uint16_t divisor);  /* 0 = off                                     */
void pf_exit(int code);
void pf_frame_hook(void);
/* (port) used by the Options menu */
void pf_apply_settings(void);       /* window scale / fullscreen from cfg          */
int  pf_pad_connected(void);
int  pf_pad_capture(void);
void pf_test_midframe(void);         /* test hook inside a game frame             */          /* next newly pressed button (SDL index), -1 none */
const char *game_path(const char *name);
extern int pf_headless;

/* busy-wait helper: the original spins on ISR-updated globals */
#define SPIN_WHILE(cond) do { while (cond) pf_service(); } while (0)

/* ------------------------------------------------------------ game code */
void timer_isr(void);               /* 108E:25FC */
extern uint8_t joy_fire_swap;
void keyboard_isr(uint8_t scancode);/* 0097:15F4 */

/* sound.c */
void play_sound(int n);             /* 0097:0042 */
void sound_load_bank(const char *a, const char *b);

/* helpers (game.c) */
int  offscreen(int x, int y);                    /* 3266E */
int  unheard(int x, int y);                      /* (port) offscreen() for sounds */
void shots_spent_clear(void);                    /* (port) see update_shots */
int  solid_at(int x, int y);                     /* 3714  */
int  player_touches(int x, int y);               /* 3513  */
int  player_in_span(int x0, int x1, int y);      /* 35EE  */
int  player_on(int x, int y);                    /* 363B  */
int  shot_hits(int x, int y);                    /* 3558  */
void spawn_debris(int x, int y);                 /* 314C  */
void spawn_score(int tile, int x, int y);        /* 2A40  */
void spawn_spark(int type, int x, int y);        /* 2B86  */
void spawn_explosion(int x, int y);              /* 2E64  */
void spawn_faller(int x, int y);                 /* 2FE7  */
void spawn_dust(int x, int y);                   /* 29D0  */
void spawn_eshot(int dir, int x, int y);         /* 1B8A  */
void spawn_shot(int x, int y, int dir);          /* 18D4  */
int  shot_wall(int x, int y);                    /* 367E  */
void add_score(int32_t pts);                     /* 0B85  */
void register_actor(ActorFn fn);                 /* C243  */
void spawn_level_actors(int rows);               /* C287  */
void draw_obj(int gfx_off, int x, int y);        /* object sprite at world pos  */
void draw_anim(int gfx_off, int x, int y);       /* anim   sprite at world pos  */
#define SCREEN_X(x)  ((x) - scroll_x)
#define SCREEN_Y(y)  ((((y) - scroll_y) >> 3) + 16)

/* player.c */
void player_update(void);                        /* 1E77 */
int  can_walk_left(void);                        /* 1247 */
int  can_walk_right(void);                       /* 12CE */
int  head_clear(void);                           /* 1352 */
int  feet_clear(void);                           /* 143E */
void draw_player(int frame, int x, int y);       /* 17F5 */
void update_shots(void);                         /* 1933 */
void update_eshots(void);                        /* 1BF6 */
void clear_eshots(void);                         /* 1B51 */

/* render.c */
void draw_world(void);                           /* 14AF */
void draw_health(void);                          /* 0A0E */
void add_health(void);                           /* 0ACC */
void draw_firepower(void);                       /* 0ADC */
void recenter_view(void);                        /* 290F */
void update_scores(void);                        /* 2A93 */
void update_debris(void);                        /* 32A5 */
void update_dust(void);                          /* 2F4C */
void update_explosions(void);                    /* 2EB1 */
void update_fallers(void);                       /* 30C9 */
void update_sparks(void);                        /* 2BD9 */
void clear_scores(void);                         /* 2A1D */
void draw_inventory(void);                       /* BB9F */

/* game.c */
void game_main(int argc, char **argv);           /* FF16 */
void play_level_loop(void);                      /* ECF4 */
void new_game(void);                             /* E655 */
void load_level(const char *name);               /* E3E0 */

#include "port.h"                                /* port enhancements (settings, menus) */

#endif
