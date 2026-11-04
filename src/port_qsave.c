/*
 * port_qsave.c - quicksave / quickload (one slot per episode, TECHBOTn.QSV).
 *
 * Both run at the same point of a game frame (the original's in-game hotkey
 * section, next to the sound toggle and pause), so loading puts the game
 * back into exactly the state it was saved in.  A snapshot holds:
 *   - every global in globals.def (the complete game state of DN1.EXE)
 *   - the actor routine list, as indices into a fixed table
 *   - the far-heap arena (live map, plus sprite data and the USERDEMO buffer)
 *   - EGA memory (both pages and the tile cache) and the adapter registers
 *   - the two statics outside globals.def (sound position, shot flag)
 * The status panel, backdrop and tiles therefore come back as they were,
 * even on a different level, without reloading anything.
 */
#include "dn1.h"
#include "port.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { const char *name; void *addr; unsigned ds_off; unsigned size; } StateVar;
extern const StateVar state_map[];
extern uint8_t *heap_arena;
extern size_t heap_arena_size;
extern uint8_t player_shot_pass;
size_t   ega_regs_size(void);
void     ega_regs_get(uint8_t *b);
void     ega_regs_set(const uint8_t *b);
uint16_t sound_get_pos(void);
void     sound_set_pos(uint16_t p);
void ui_box(int x, int row, int h);
void ui_text(const char *s, int x, int row);

/* every routine the level loader can register (game.c), in a fixed order;
 * the file stores positions in this list instead of code addresses */
#define A(f) void f(void);
#define ACTORS(X) X(act_balloons) X(act_biters) X(act_bombs) X(act_boots) X(act_boss) \
    X(act_bridge) X(act_card) X(act_claws) X(act_conveyors) X(act_dynamite) X(act_fans) \
    X(act_ffields_h) X(act_ffields_v) X(act_firemonsters) X(act_firewheels) X(act_flags) \
    X(act_flamers_l) X(act_flamers_r) X(act_floatbots) X(act_floorspikes) X(act_floppies) \
    X(act_footballs) X(act_greyboxes) X(act_guns) X(act_helis) X(act_hopbots) X(act_joysticks) \
    X(act_keydoors) X(act_keys) X(act_letters) X(act_minibots) X(act_molecules) X(act_rabbits) \
    X(act_radios) X(act_reactor) X(act_robohand) X(act_rockets) X(act_signs) X(act_snake) \
    X(act_soda) X(act_spikes) X(act_static_bombs) X(act_tele_a) X(act_tele_b) X(act_turkeys) \
    X(act_walkway) X(act_wheelbots)
ACTORS(A)
#undef A
#define A(f) f,
static ActorFn const actor_table[] = { ACTORS(A) };
#undef A
#define N_ACTORS ((int)(sizeof actor_table / sizeof actor_table[0]))

volatile uint8_t port_qs_request;

static const char magic[8] = { 'D', 'N', '1', 'Q', 'S', 'A', 'V', 'E' };
#define QS_VERSION 1u

/* one quicksave slot per episode: TECHBOT1.QSV, TECHBOT2.QSV, TECHBOT3.QSV */
static const char *qsv_name(void)
{
    static char n[16];
    snprintf(n, sizeof n, "TECHBOT%d.QSV", episode);
    return n;
}
/* quicksaves from before the Techbot name (DNnPORT.QSV) still load */
static FILE *qsv_open_read(void)
{
    char old[16];
    FILE *f = qsv_open_read();
    if (f) return f;
    snprintf(old, sizeof old, "DN%dPORT.QSV", episode);
    return fopen(game_path(old), "rb");
}

static size_t globals_size(void)
{
    const StateVar *v;
    size_t n = 0;
    for (v = state_map; v->name; v++) n += v->size;
    return n;
}

/* ---- message in the same box the original uses for its in-game toggles */
static void message(const char *line)
{
    int x = 2;
    ui_box(x, 3, 2);
    WRITE_MODE_NORMAL();
    ui_text("Quicksave", x + 2 + (22 - 9) / 2, 7);
    ui_text(line, x + 2 + (22 - (int)strlen(line)) / 2, 9);
    pf_present();
    pf_delay_ms(700);
}

static void put32(FILE *f, uint32_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); fputc((v >> 16) & 0xFF, f); fputc(v >> 24, f); }
static uint32_t get32(FILE *f) { uint32_t v = (uint32_t)fgetc(f); v |= (uint32_t)fgetc(f) << 8; v |= (uint32_t)fgetc(f) << 16; v |= (uint32_t)fgetc(f) << 24; return v; }

void port_quicksave(void)
{
    const StateVar *v;
    uint8_t regs[64], idx[100];
    int i, j;
    uint16_t sp = sound_get_pos();
    FILE *f = fopen(game_path(qsv_name()), "wb");
    if (!f) { message("Could not save!"); return; }
    for (i = 0; i < 100; i++) {
        idx[i] = 0xFF;
        for (j = 0; j < N_ACTORS; j++) if (actor_fns[i] == actor_table[j]) idx[i] = (uint8_t)j;
    }
    fwrite(magic, 8, 1, f);
    put32(f, QS_VERSION);
    put32(f, (uint32_t)globals_size());
    put32(f, (uint32_t)heap_arena_size);
    put32(f, (uint32_t)ega_regs_size());
    for (v = state_map; v->name; v++) fwrite(v->addr, v->size, 1, f);
    fwrite(idx, 100, 1, f);
    fputc(sp & 0xFF, f); fputc(sp >> 8, f); fputc(player_shot_pass, f);
    fwrite(heap_arena, heap_arena_size, 1, f);
    for (i = 0; i < 4; i++) fwrite(ega_planes[i], 0x10000, 1, f);
    ega_regs_get(regs);
    fwrite(regs, ega_regs_size(), 1, f);
    i = ferror(f);
    fclose(f);
    message(i ? "Could not save!" : "Game saved.");
}

void port_quickload(void)
{
    const StateVar *v;
    uint8_t regs[64], idx[100], *globals, *arena, *planes;
    char m[8];
    size_t gsz = globals_size(), off;
    uint16_t sp;
    uint8_t shot;
    int i, ok;
    FILE *f = fopen(game_path(qsv_name()), "rb");
    if (!f) { message("No quicksave yet."); return; }
    ok = fread(m, 8, 1, f) == 1 && !memcmp(m, magic, 8) && get32(f) == QS_VERSION &&
         get32(f) == gsz && get32(f) == heap_arena_size && get32(f) == ega_regs_size();
    globals = (uint8_t *)malloc(gsz);
    arena = (uint8_t *)malloc(heap_arena_size);
    planes = (uint8_t *)malloc(4 * 0x10000);
    /* read everything first: a short or foreign file must not leave the
     * game half-restored */
    ok = ok && globals && arena && planes &&
         fread(globals, gsz, 1, f) == 1 && fread(idx, 100, 1, f) == 1;
    if (ok) { int lo = fgetc(f), hi = fgetc(f), s = fgetc(f); ok = s != EOF; sp = (uint16_t)(lo | (hi << 8)); shot = (uint8_t)s; }
    ok = ok && fread(arena, heap_arena_size, 1, f) == 1 && fread(planes, 4 * 0x10000, 1, f) == 1 &&
         fread(regs, ega_regs_size(), 1, f) == 1;
    for (i = 0; ok && i < 100; i++) if (idx[i] != 0xFF && idx[i] >= N_ACTORS) ok = 0;
    fclose(f);
    if (ok) {
        for (v = state_map, off = 0; v->name; v++) { memcpy(v->addr, globals + off, v->size); off += v->size; }
        for (i = 0; i < 100; i++) actor_fns[i] = idx[i] == 0xFF ? NULL : actor_table[idx[i]];
        sound_set_pos(sp);
        player_shot_pass = shot;
        memcpy(heap_arena, arena, heap_arena_size);
        for (i = 0; i < 4; i++) memcpy(ega_planes[i], planes + i * 0x10000, 0x10000);
        ega_regs_set(regs);
        pf_speaker(0);
        /* the snapshot was taken with the quicksave key down: resume as if it
         * had just been released, so the play loop doesn't save again */
        last_scancode = (uint8_t)(cfg.key_qsave | 0x80);
        dl_reset();
        shots_spent_clear();             /* spent shots are not part of the snapshot */
    }
    free(globals); free(arena); free(planes);
    message(ok ? "Game loaded." : "Quicksave unusable.");
}
