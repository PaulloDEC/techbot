/*
 * port_state.c - in-memory snapshots of the whole game, and going back to one.
 *
 * A snapshot holds what a quicksave holds (see port_qsave.c: every global in
 * globals.def, the actor routine list, the far-heap arena with the live map,
 * EGA memory and registers, the sound position and the shot flag) plus the
 * two things a quicksave leaves out: the spent-shot pool (port) and the
 * EGA "written" map.  Kept in RAM, so taking or restoring one costs a few
 * hundred kilobytes of copying and no file access.
 *
 * Uses:
 *   - Shrapnel's play-here: one snapshot right after start-up (graphics
 *     loaded, nothing played), restored after every play session, so no
 *     state from one session can leak into the next;
 *   - the test harness (--state-test, --reset-test), which proves that going
 *     back to a snapshot reproduces the game exactly, frame for frame.
 *
 * port_restart() unwinds to the top of game_main's loop (main.c); together
 * with port_state_load() of the start-up snapshot that is a clean restart
 * without leaving the process.
 */
#include "dn1.h"
#include "port.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; void *addr; unsigned ds_off; unsigned size; } StateVar;
extern const StateVar state_map[];
extern uint8_t *heap_arena;
extern size_t heap_arena_size;
extern uint8_t player_shot_pass;
extern uint8_t ega_written[8000];
size_t   ega_regs_size(void);
void     ega_regs_get(uint8_t *b);
void     ega_regs_set(const uint8_t *b);
uint16_t sound_get_pos(void);
void     sound_set_pos(uint16_t p);
size_t   shots_spent_size(void);
void     shots_spent_get(void *b);
void     shots_spent_set(const void *b);

struct PortState {
    uint8_t *globals;            /* state_map variables, in order */
    uint8_t *arena;              /* heap_arena_size bytes          */
    uint8_t *planes;             /* 4 x 64 KiB                     */
    uint8_t *spent;
    ActorFn  fns[100];
    uint8_t  regs[64];
    uint8_t  written[8000];
    uint16_t sound_pos;
    uint8_t  shot_pass;
};

static size_t globals_size(void)
{
    const StateVar *v;
    size_t n = 0;
    for (v = state_map; v->name; v++) n += v->size;
    return n;
}

PortState *port_state_new(void)
{
    PortState *s = (PortState *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->globals = (uint8_t *)malloc(globals_size());
    s->arena = (uint8_t *)malloc(heap_arena_size);
    s->planes = (uint8_t *)malloc(4 * 0x10000);
    s->spent = (uint8_t *)malloc(shots_spent_size());
    if (!s->globals || !s->arena || !s->planes || !s->spent) { port_state_free(s); return NULL; }
    return s;
}

void port_state_free(PortState *s)
{
    if (!s) return;
    free(s->globals); free(s->arena); free(s->planes); free(s->spent);
    free(s);
}

void port_state_save(PortState *s)
{
    const StateVar *v;
    size_t off = 0;
    int i;
    for (v = state_map; v->name; v++) { memcpy(s->globals + off, v->addr, v->size); off += v->size; }
    memcpy(s->fns, actor_fns, sizeof s->fns);
    memcpy(s->arena, heap_arena, heap_arena_size);
    for (i = 0; i < 4; i++) memcpy(s->planes + i * 0x10000, ega_planes[i], 0x10000);
    ega_regs_get(s->regs);
    memcpy(s->written, ega_written, sizeof s->written);
    shots_spent_get(s->spent);
    s->sound_pos = sound_get_pos();
    s->shot_pass = player_shot_pass;
}

void port_state_load(const PortState *s)
{
    const StateVar *v;
    size_t off = 0;
    int i;
    for (v = state_map; v->name; v++) { memcpy(v->addr, s->globals + off, v->size); off += v->size; }
    memcpy(actor_fns, s->fns, sizeof s->fns);
    memcpy(heap_arena, s->arena, heap_arena_size);
    for (i = 0; i < 4; i++) memcpy(ega_planes[i], s->planes + i * 0x10000, 0x10000);
    ega_regs_set(s->regs);
    memcpy(ega_written, s->written, sizeof ega_written);
    shots_spent_set(s->spent);
    sound_set_pos(s->sound_pos);
    player_shot_pass = s->shot_pass;
    pf_speaker(0);                       /* nothing left sounding from before */
    dl_reset();                          /* smooth motion starts afresh       */
}

/* a 64-bit FNV-1a fingerprint of a snapshot (for tests) */
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) { h ^= *b++; h *= 0x100000001B3ull; }
    return h;
}
uint64_t port_state_hash(const PortState *s)
{
    uint64_t h = 0xCBF29CE484222325ull;
    h = fnv(h, s->globals, globals_size());
    h = fnv(h, s->fns, sizeof s->fns);
    h = fnv(h, s->arena, heap_arena_size);
    h = fnv(h, s->planes, 4 * 0x10000);
    h = fnv(h, s->regs, ega_regs_size());
    h = fnv(h, s->written, sizeof s->written);
    h = fnv(h, s->spent, shots_spent_size());
    h = fnv(h, &s->sound_pos, sizeof s->sound_pos);
    return fnv(h, &s->shot_pass, 1);
}

/* the same bytes?  (for tests: 0 when equal) */
int port_state_compare(const PortState *a, const PortState *b)
{
    int i;
    if (memcmp(a->globals, b->globals, globals_size())) return 1;
    for (i = 0; i < 100; i++) if (a->fns[i] != b->fns[i]) return 2;
    if (memcmp(a->arena, b->arena, heap_arena_size)) return 3;
    if (memcmp(a->planes, b->planes, 4 * 0x10000)) return 4;
    if (memcmp(a->regs, b->regs, ega_regs_size())) return 5;
    if (memcmp(a->written, b->written, sizeof a->written)) return 6;
    if (memcmp(a->spent, b->spent, shots_spent_size())) return 7;
    if (a->sound_pos != b->sound_pos || a->shot_pass != b->shot_pass) return 8;
    return 0;
}
