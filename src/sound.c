/*
 * sound.c - PC-speaker sound effects and the 145 Hz timer interrupt.
 *
 * Sound banks DUKE1.DN1 / DUKE1-B.DN1 (24 effects each): a 16-byte header,
 * then 16-byte directory entries {u16 offset, u8 priority, ...}; each effect
 * is a list of PIT divisors (one per 1/145 s, 0 = silence) ending in FFFFh.
 * A new effect only interrupts the current one if its priority is >= the
 * playing one's.
 */
#include "dn1.h"
#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t *sound_ptr[48];    /* CBB5: far pointers to each effect   */
uint8_t        sound_prio[48];   /* 4FCE (bank A) / 4FE6 (bank B)       */
static uint8_t bank_a[0x2454 + 2], bank_b[0x2134 + 2];   /* + end marker, see load_file */
static uint16_t snd_pos;         /* 0F2A */
uint16_t sound_get_pos(void) { return snd_pos; }      /* (port) quicksave */
void     sound_set_pos(uint16_t p) { snd_pos = p; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* The bank buffer is followed by an FFFFh end marker that the original never
 * had: effects 45-47 of every episode's DUKE1-B run to the end of the buffer
 * without one, and anything reading them (the Enhanced mixer's scan, or the
 * timer playing one to the end) would otherwise read past the allocation. */
static void load_file(uint8_t *b, const char *name, size_t alloc, size_t want)
{
    FILE *f = fopen(game_path(name), "rb");
    memset(b, 0, alloc);
    b[alloc] = b[alloc + 1] = 0xFF;
    if (f) { size_t n = fread(b, 1, want, f); (void)n; fclose(f); }
}

/* an effect's start; an offset outside the bank points at the end marker */
static const uint8_t *effect_at(const uint8_t *bank, size_t alloc, uint16_t off)
{
    off = (uint16_t)((off >> 1) << 1);
    return off < alloc ? bank + off : bank + alloc;
}

/* 108E:3980 + 108E:3A38 */
void sound_load_bank(const char *a, const char *b)
{
    int i;
    load_file(bank_a, a, 0x2454, 0x2454);
    for (i = 0; i < 24; i++) {
        sound_ptr[i] = effect_at(bank_a, 0x2454, rd16(bank_a + i * 16 + 0x10));
        sound_prio[i] = bank_a[i * 16 + 0x12];
    }
    load_file(bank_b, b, 0x2134, 0x1F40);
    for (i = 0; i < 24; i++) {
        sound_ptr[24 + i] = effect_at(bank_b, 0x2134, rd16(bank_b + i * 16 + 0x10));
        sound_prio[24 + i] = bank_b[i * 16 + 0x10];   /* sic: low byte of the offset */
    }
    port_sound_init();
}

/* 0097:0042 */
void play_sound(int n)
{
    {   /* test aid: SNDLOG=1 prints every sound started */
        static int chk, on; if (!chk) { chk = 1; on = getenv("SNDLOG") != NULL; }
        if (on) printf("snd %d\n", n);
    }
    port_sound_play(n);             /* Enhanced mixer (no effect on the logic below) */
    if (sound_prio[n] >= snd_prio_cur) {
        snd_prio_cur = sound_prio[n];
        snd_start = 1;
        snd_current = (uint8_t)n;
        snd_playing = 0;
    }
}

/* 108E:25FC  timer interrupt (PIT ch.0 reprogrammed to 1193182/8273 Hz) */
void timer_isr(void)
{
    const uint8_t *p;
    if (snd_start) {
        snd_start = 0;
        snd_pos = 0;
        snd_playing = 1;
    }
    p = sound_ptr[snd_current] ? sound_ptr[snd_current] + snd_pos * 2 : NULL;
    if (p && rd16(p) == 0xFFFF) {
        snd_playing = 0;
        snd_prio_cur = 0;
        pf_speaker(0);
    }
    if (snd_playing && p) {
        uint16_t v = rd16(p);
        if (v == 0 && sound_enabled) pf_speaker(0);
        else if (sound_enabled) pf_speaker(v);
        snd_pos++;
    } else {
        pf_speaker(0);
    }
    timer_ticks++;
    if (++snd_isr_div == 8) snd_isr_div = 0;    /* original chains to the BIOS tick here */
}
