/*
 * port_sound.c - Enhanced-mode sound: overlapping effects and "hi-fi"
 * reinterpretations of the PC-speaker effects.
 *
 * The original's single-voice speaker logic (sound.c) keeps running
 * untouched, so game state is identical; in Enhanced mode its output is
 * silenced and every effect the game starts is also played here.  The mixer
 * runs inside the audio callback and steps each voice through its divisor
 * list at exactly 1193182/8273 Hz, counted in samples.
 *
 *   SND_OVERLAP - several voices at once, each a plain square wave like the
 *                 speaker: the original sound, minus the cut-offs.
 *   SND_HIFI    - several voices, each effect resynthesised from what its
 *                 pitch list does (classified once, when the banks load):
 *                   sweep  - mostly small pitch steps: a continuous glide on
 *                            a rounded, warmer waveform;
 *                   tone   - stepped melodies / held tones: band-limited
 *                            pulse with a slightly detuned second oscillator;
 *                   noise  - large random pitch jumps (how the speaker faked
 *                            explosions): band-passed noise following the
 *                            pitch, with some tone mixed in;
 *                 plus attack/release envelopes (no clicks), a gentle
 *                 low-pass and a little room reverb.
 */
#include "dn1.h"
#include "port.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern const uint8_t *sound_ptr[48];
enum { K_SWEEP, K_TONE, K_NOISE };
static uint8_t kind[48];
static uint16_t length[48];

#define NV 6                      /* voices */
typedef struct {
    int on, effect;
    long pos;                     /* step index                                  */
    double f_target, f_cur;       /* Hz (0 = rest)                               */
    double ph, ph2;               /* oscillator phases                           */
    double env;                   /* 0..1                                        */
    int gate, done;
    double bp_lo, bp_band;        /* noise band-pass state                       */
    unsigned age;
} Voice;
static Voice v[NV];
static unsigned age_counter;
static double step_acc;           /* samples until the next 1/145 s step         */
static uint32_t lfsr = 0xACE1u;
/* starts requested by the game thread, picked up by the audio thread */
static volatile int queue[16], q_head, q_tail;

/* reverb: four feedback delay lines (short, roomy) */
#define RV_LEN 4096
static float rv[4][RV_LEN];
static int rv_pos;
static const int rv_tap[4] = { 1557, 1617, 1491, 1422 };
static double lp_state;

int port_sound_mixing(void) { return cfg.mode == MODE_ENHANCED && cfg.sound != SND_ORIGINAL; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* classify each effect by how its pitch moves */
void port_sound_init(void)
{
    int e;
    for (e = 0; e < 48; e++) {
        const uint8_t *p = sound_ptr[e];
        int n = 0, pairs = 0, small = 0, big = 0;
        double prev = 0;
        kind[e] = K_TONE; length[e] = 0;
        if (!p) continue;
        while (n < 4000 && rd16(p + n * 2) != 0xFFFF) {
            uint16_t d = rd16(p + n * 2);
            if (d) {
                double f = 1193182.0 / d;
                if (prev > 0) {
                    double j = fabs(log(f / prev) / log(2.0));
                    pairs++;
                    if (j < 0.09) small++;
                    if (j > 0.6) big++;
                }
                prev = f;
            }
            n++;
        }
        length[e] = (uint16_t)(n < 600 ? n : 600);    /* 45-47 have no proper end */
        if (pairs >= 4 && big * 4 >= pairs) kind[e] = K_NOISE;
        else if (pairs >= 4 && small * 10 >= pairs * 6) kind[e] = K_SWEEP;
        else kind[e] = K_TONE;
    }
}

int port_sound_kind(int e) { return e >= 0 && e < 48 ? kind[e] : -1; }

void port_sound_play(int n)
{
    int nt;
    if (!port_sound_mixing() || n < 0 || n >= 48 || !sound_ptr[n]) return;
    pf_audio_lock();
    nt = (q_tail + 1) & 15;
    if (nt != q_head) { queue[q_tail] = n; q_tail = nt; }
    pf_audio_unlock();
}

static void start_voice(int e)
{
    int i, pick = -1;
    unsigned oldest = 0xFFFFFFFFu;
    for (i = 0; i < NV; i++) if (v[i].on && v[i].effect == e) pick = i;      /* the same effect restarts */
    if (pick < 0) for (i = 0; i < NV; i++) if (!v[i].on) { pick = i; break; }
    if (pick < 0) for (i = 0; i < NV; i++) if (v[i].age < oldest) { oldest = v[i].age; pick = i; }
    memset(&v[pick], 0, sizeof v[pick]);
    v[pick].on = 1; v[pick].effect = e; v[pick].pos = -1; v[pick].age = ++age_counter;
}

/* advance every voice by one 1/145 s step */
static void step_voices(void)
{
    int i;
    for (i = 0; i < NV; i++) {
        Voice *w = &v[i];
        uint16_t d;
        if (!w->on || w->done) continue;
        w->pos++;
        d = rd16(sound_ptr[w->effect] + w->pos * 2);
        if (d == 0xFFFF || w->pos >= length[w->effect]) { w->done = 1; w->gate = 0; continue; }
        if (d == 0) { w->gate = 0; continue; }
        w->f_target = 1193182.0 / d;
        if (!w->gate && w->env < 0.01) w->f_cur = w->f_target;   /* new note after a rest */
        w->gate = 1;
    }
}

static double noise(void)
{
    lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5;
    return (double)(int32_t)lfsr / 2147483648.0;
}

/* band-limited step correction (polyBLEP) for the pulse oscillators */
static double blep(double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
static double pulse(double ph, double dt)
{
    double s = ph < 0.5 ? 1.0 : -1.0;
    double t2 = ph + 0.5; if (t2 >= 1.0) t2 -= 1.0;
    return s + blep(ph, dt) - blep(t2, dt);
}

static double voice_sample(Voice *w, int rate, int hifi)
{
    double out = 0, f, dt;
    double att = 1.0 - exp(-1.0 / (0.0015 * rate)), rel = 1.0 - exp(-1.0 / (0.012 * rate));
    if (!hifi) {                                         /* the speaker: square, no envelope */
        if (!w->gate) return 0;
        w->ph += w->f_target / rate;
        w->ph -= floor(w->ph);
        return w->ph < 0.5 ? 1.0 : -1.0;
    }
    w->env += ((w->gate ? 1.0 : 0.0) - w->env) * (w->gate ? att : rel);
    if (w->done && w->env < 0.0005) { w->on = 0; return 0; }
    if (w->env < 0.0001) return 0;
    if (kind[w->effect] == K_SWEEP) w->f_cur += (w->f_target - w->f_cur) * (1.0 - exp(-1.0 / (0.003 * rate)));
    else w->f_cur = w->f_target;
    f = w->f_cur;
    if (f > 9000) f = 9000;
    dt = f / rate;
    w->ph += dt; w->ph -= floor(w->ph);
    switch (kind[w->effect]) {
    case K_SWEEP: {                                      /* rounded: sine pushed towards square */
        double s = sin(2 * M_PI * w->ph);
        out = 0.55 * s + 0.45 * tanh(2.5 * s);
        break;
    }
    case K_TONE:                                         /* pulse + a gently detuned twin */
        w->ph2 += dt * 1.004; w->ph2 -= floor(w->ph2);
        out = 0.6 * pulse(w->ph, dt) + 0.4 * pulse(w->ph2, dt * 1.004);
        out *= 0.8;
        break;
    case K_NOISE: {                                      /* band-passed noise at the pitch */
        double fc = f < 120 ? 120 : f, g = 2.0 * sin(M_PI * fc / rate), q = 0.6;
        double x = noise();
        double hp = x - w->bp_lo - q * w->bp_band;
        w->bp_band += g * hp; w->bp_lo += g * w->bp_band;
        out = 1.6 * w->bp_band + 0.25 * sin(2 * M_PI * w->ph);
        break;
    }
    }
    {   /* level each style to roughly the loudness of the speaker's square */
        static const double gain[3] = { 0.68, 0.86, 1.7 };
        return out * w->env * gain[kind[w->effect]];
    }
}

int port_sound_busy(void)
{
    int i;
    if (q_head != q_tail) return 1;
    for (i = 0; i < NV; i++) if (v[i].on) return 1;
    return 0;
}

void port_sound_render(int16_t *buf, int n, int rate)
{
    const double step_len = (double)rate * 8273.0 / 1193182.0;
    int hifi = cfg.sound == SND_HIFI, i, k;
    while (q_head != q_tail) { start_voice(queue[q_head]); q_head = (q_head + 1) & 15; }
    for (i = 0; i < n; i++) {
        double mix = 0, out;
        if (step_acc <= 0) { step_voices(); step_acc += step_len; }
        step_acc -= 1.0;
        if (sound_enabled)
            for (k = 0; k < NV; k++) if (v[k].on) {
                mix += voice_sample(&v[k], rate, hifi);
                if (!hifi && v[k].done) v[k].on = 0;
            }
        if (hifi) {
            double wet = 0;
            int t;
            lp_state += (mix - lp_state) * 0.55;          /* gentle low-pass (~8 kHz at 44.1 kHz) */
            mix = lp_state;
            for (t = 0; t < 4; t++) {
                int r = (rv_pos - rv_tap[t] + RV_LEN) % RV_LEN;
                float d = rv[t][r];
                wet += d;
                rv[t][rv_pos] = (float)(mix * 0.5 + d * 0.62);
            }
            rv_pos = (rv_pos + 1) % RV_LEN;
            out = 0.22 * mix + 0.035 * wet;
        } else out = 0.09 * mix;                          /* each square voice at the speaker's level */
        out = tanh(out * 1.2) / 1.2;                       /* soft limit when many voices pile up */
        buf[i] = (int16_t)(out * 32767.0);
    }
}
