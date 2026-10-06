/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Drum voices: lean fixed-point drums for a kit of 8 lanes. Felucca's own design, voiced by ear.
 *
 * Every tuned part is a damped sine (a phase accumulator, the SINE table, a decaying envelope): exact
 * pitch, always stable, no high-Q resonator. Noise is xorshift through dsp.c's trapezoidal SVF or a
 * one-pole. Only 32-bit products in the sample loops. Envelopes:
 *   fast (tau under ~40 ms)  Q16 per sample, e = (e * k) >> 15 with k Q15 (the floor form: always reaches 0)
 *   slow (tau above)         Q30 per CTL block times a Q16 factor, ramped linearly inside the block
 *
 * Lanes and their variants (dv_type: what a lane plays):
 *   KICK    PUNCH: a sine swept in two stages (an attack of +2 octaves, then TONE's 0..+3 octaves after a
 *           hold), held flat for 12 ms, phase-locked 2nd and 3rd harmonics, a DC-free click (two cycles of
 *           a high sine under a raised-cosine window) and a soft-clip DRIVE; ROUND: no attack stage and
 *           no hold, a shallow slow sweep (up to +1 octave), strong 2nd and 3rd harmonics, more drive, a
 *           softer, lower click, a longer body.
 *           The sweep through 100..400 Hz, the harmonics and the drive keep the kick audible on a small
 *           speaker (energy above 150 Hz >= -12 dB at the defaults) without touching the fundamental
 *   SNARE   two sines (f, 1.6 f) with a small downward glide under a short shell envelope, plus noise
 *           through a band-pass (TONE: its centre) under a spike and a body envelope and a little
 *           high-passed noise on the spike; soft-clipped. Extra: SNAPPY (the noise level)
 *   CLAP    noise through a band-pass (TUNE: its centre), re-struck 3 times (4 when SPREAD is high) at
 *           SPREAD's interval, then a long low tail; TONE adds high-passed noise. Extra: SPREAD
 *   HAT CL, HAT OP   classic analog hats: the metal source (six square waves at 205, 304, 370, 523, 540 and
 *           800 Hz: a dense, clangy cluster) through a band-pass at 7.1 kHz (Q 0.8), then a bilinear one-pole
 *           high-pass per hat (CL 12 kHz, OP 8.8 kHz), plus a sizzle: the raw cluster high-passed at 13 kHz
 *           (strong on the closed hat, a trace on the open one). TONE moves both high-passes +-12 semitones,
 *           TUNE the cluster and every filter. Envelope: a 0.3 ms rise, then CL tau 6..42 ms (16 at the design),
 *           OP 25..380 ms (98). Accent up to +6 dB. Extra: SIZZLE (0: none, 64: as designed, 127: double).
 *           A closed hat chokes the open one (dv_choke: a 1.5 ms release)
 *   TOM     a sine whose pitch drops as it rings down (it follows the square of the amplitude), a little
 *           low-passed noise for the stick (TONE). Extra: BEND. CONGA: higher, shorter, a slap of high noise
 *           instead of the stick (TONE), a little soft clip
 *   RIM     two short sines (f, 3.25 f) into a soft clip (extra: DRIVE, the saturation; TONE: the crack's
 *           share). CLAVE: one sine, higher (TONE: a little of the upper sine)
 *   BELL    two square waves (f, 1.48 f) through a band-pass under a short strike and a tail envelope
 *           (extra: STRIKE; TONE: the squares themselves mixed in). CYM: the metal source through two
 *           band-passes (TONE: high / low balance; TUNE moves both) under a strike (STRIKE) and a ring
 * Parameters per lane (dv_param_t): TUNE (1/16 semitone from the designed pitch, held in the voice's
 * range), DECAY, TONE, extra, LEVEL, accent, 0..127 each; the designed sound is at DV_DEF's values.
 * Accent adds up to 4 dB, a little more on the transient (click, spike, the claps). At LEVEL 100 the
 * voices peak near -7 dBFS (the snare's, the clap's and the closed hat's spikes a little higher);
 * louder settings go into the output's soft knee.
 *
 * Use: dv_setup when a parameter changes (dv_param_t compared as two words), dv_trigger on a hit,
 * dv_run per block (n <= CTL) into y: the voice at its level, Q15, linear up to -6 dBFS and never at full
 * scale (a soft knee above).
 * Voices whose type uses the metal source take its block (dv_metal_run) in metal. A voice that has rung out
 * (-84 dB) stops computing (live 0) and writes zeros. */

enum { DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_RIM, DV_BELL, DV_NLANE };
enum {
    DVT_PUNCH, DVT_ROUND, DVT_SNARE, DVT_CLAP, DVT_HATC, DVT_HATO, DVT_TOM, DVT_CONGA, DVT_RIM, DVT_CLAVE,
    DVT_BELL, DVT_CYM, DVT_COUNT
};
static const uint8_t DV_LANE_TYPE[DV_NLANE][2] = {   /* lane, variant -> type */
    {DVT_PUNCH, DVT_ROUND}, {DVT_SNARE, DVT_SNARE}, {DVT_CLAP, DVT_CLAP}, {DVT_HATC, DVT_HATC},
    {DVT_HATO, DVT_HATO}, {DVT_TOM, DVT_CONGA}, {DVT_RIM, DVT_CLAVE}, {DVT_BELL, DVT_CYM},
};

typedef struct {
    uint8_t type;                                        /* DVT_* */
    uint8_t decay, tone, extra, level, accent;           /* 0..127 */
    int16_t tune;                                        /* 1/16 semitone from the designed pitch */
} dv_param_t;

typedef struct {                                         /* coefficients (dv_setup); meaning per type, see the run */
    uint32_t inc[3];                                     /* phase increments, cycles Q32 */
    uint32_t span[2];                                    /* pitch sweeps (increment above inc[0]) */
    uint32_t k[3];                                       /* fast decays per sample, Q15 */
    uint32_t kb[2];                                      /* slow decays per block, Q16 */
    int32_t g[6];                                        /* gains, Q15 unless noted */
    tsvf_t f[2];                                         /* band-passes */
    int32_t hp;                                          /* one-pole coefficient, Q15 */
    int32_t out;                                         /* output gain: type, LEVEL, accent (Q12) */
    uint16_t hold, click;                                /* kick: hold (blocks), click length (samples); clap:
                                                          * tooth spacing (samples), teeth */
    uint8_t type, pad;
    int16_t metal;                                       /* the metal source's pitch (p16) for this lane */
} dv_coef_t;

typedef struct {
    uint8_t type, trig, choke, live;
    uint16_t n, cnt;                                     /* counters: samples, teeth / hold blocks */
    uint32_t ph[3];
    uint32_t e[3];                                       /* fast envelopes, Q16 */
    int32_t q[2];                                        /* slow envelopes, Q30 */
    int32_t s[5];                                        /* filter states */
    int32_t rng;
} dv_voice_t;

typedef struct {                                         /* the metal source: 6 square waves */
    uint32_t ph[6], inc[6];
} dv_metal_t;

/* designed sound per type: pitch (p16 = 16 x MIDI note) and its range, the main decay's tau at DECAY 0
 * (0.1 ms) and its span (octaves x 16 over DECAY 0..127), the designed DECAY TONE extra, the output gain
 * (Q12, evens the voices out by RMS) */
static const struct {
    int16_t p16, lo, hi;
    uint16_t tau0;
    uint8_t oct16, def[3];
    uint16_t gain;
} DV_DEF[DVT_COUNT] = {
    {502, 403, 664, 100, 75, {64, 64, 32}, 3225},        /* PUNCH  50 Hz (35..90), tau 10..250 ms */
    {533, 403, 664, 200, 69, {64, 64, 56}, 3222},        /* ROUND  56 Hz, tau 20..400 ms */
    {856, 744, 958, 300, 37, {64, 64, 80}, 4080},        /* SNARE  180 Hz (120..260), noise tau 30..150 ms */
    {1358, 1233, 1523, 200, 53, {64, 64, 64}, 8000},     /* CLAP   band 1.1 kHz (0.7..2), tail tau 20..200 ms */
    {893, 701, 1085, 60, 45, {64, 64, 64}, 9015},       /* HAT CL metal 205 Hz (103..410), tau 6..42 ms */
    {893, 701, 1085, 250, 63, {64, 64, 64}, 14040},      /* HAT OP tau 25..380 ms */
    {720, 502, 1190, 600, 53, {64, 64, 40}, 2384},       /* TOM    110 Hz (50..400 with the variants' range),
                                                          * tau 60..600 ms */
    {998, 806, 1190, 300, 53, {64, 64, 40}, 2638},       /* CONGA  300 Hz (150..600), tau 30..300 ms */
    {1139, 947, 1331, 20, 32, {64, 64, 64}, 5215},       /* RIM    500 Hz (250..1000), tau 2..8 ms */
    {1585, 1382, 1715, 30, 32, {64, 64, 64}, 5608},      /* CLAVE  2.5 kHz (1.2..4), tau 3..12 ms */
    {1161, 969, 1353, 300, 59, {64, 64, 64}, 2785},      /* BELL   540 Hz (270..1080), tail tau 30..400 ms */
    {1078, 886, 1270, 3000, 53, {64, 64, 64}, 7583},     /* CYM    metal 400 Hz, ring tau 0.3..3 s */
};
#define DV_SEED 0x5EED1234
#define DV_P16(hz_p16) pitch_inc(clamp((hz_p16), 0, 2047))
/* the metal source's ratios (Q12). Hats: the six-square cluster of the classic analog hats (205.3, 304.4,
 * 369.6, 522.7, 540, 800 Hz over 205.3); the cymbal: inharmonic, picked by ear */
static const uint16_t DV_METAL_R[2][6] = {
    {4096, 6073, 7374, 10428, 10774, 15961},
    {4096, 5800, 6560, 7530, 8810, 10650},
};

/* ------------------------------------------------------- setup helpers (not in the sample loops) --- */
/* 2^(x / 4096), 0 <= x < 16 * 4096, Q16 (a cubic for the fraction: within 0.01 %) */
static uint32_t dv_exp2(uint32_t x)
{
    uint32_t f = x & 4095u, m;
    m = 65536u + ((f * (45594u + ((f * (14851u + ((f * 5092u) >> 12))) >> 12))) >> 12);
    return m << (x >> 12);
}
/* tau (us) -> per-sample decay, Q15: e^(-1 / (tau fs)), to the second order */
static uint32_t dv_k(uint32_t us)
{
    uint32_t x = 743039u / (us | 1u);
    return x >= 32768u ? 0u : 32768u - x + ((x * x) >> 16);
}
/* tau (us) -> per-block decay, Q16: e^(-CTL / (tau fs)), to the fourth order */
static uint32_t dv_kb(uint32_t us)
{
    uint32_t x = 47554467u / (us | 1u), x2, x3, x4, k;
    if (x > 46000u)
        x = 46000u;
    x2 = (x * x) >> 16;
    x3 = (x2 * x) >> 16;
    x4 = (x3 * x) >> 16;
    k = 65536u - x + (x2 >> 1) - x3 / 6u + x4 / 24u;
    return k > 65535u ? 65535u : k;
}
/* the main tau of a type at DECAY d (us) */
static uint32_t dv_tau(uint32_t type, uint32_t d)
{
    uint32_t m = dv_exp2(d * DV_DEF[type].oct16 * 4096u / (127u * 16u));   /* Q16, below 2^21 */
    return (DV_DEF[type].tau0 * ((m * 100u) >> 12)) >> 4;
}
/* p16 -> the SVF's cutoff index (0..127 << 8: 30 Hz .. 16 kHz) */
static int32_t dv_cut(int32_t p16) { return clamp(((p16 - 360) * 4785) >> 8, 0, 127 << 8); }
/* p16 -> one-pole coefficient, Q15: w / (1 + w), w = 2 pi f / fs */
static int32_t dv_pole(int32_t p16)
{
    int32_t w = (int32_t)(((DV_P16(p16) >> 17) * 205887u) >> 15);
    return (w << 15) / (32768 + w);
}
/* p16 -> a bilinear one-pole high-pass (s / (s + w), not prewarped): y = g (x - x1) + p y1, g and p in Q14 */
static void dv_hp1(int32_t *g, int32_t *p, int32_t p16)
{
    uint32_t inc = p16 > 1900 ? DV_P16(clamp(p16, 0, 2239) - 192) << 1 : DV_P16(p16);   /* (above the table: an octave
                                                          * down, doubled) */
    int32_t u = (int32_t)(((inc >> 17) * 205887u) >> 15);   /* w / 2fs = pi f / fs, Q16 */
    *g = (65536 << 14) / (65536 + u);
    *p = (65536 - u) * 16384 / (65536 + u);
}
/* 0..127 -> a / b .. 1: a gain in Q15 from v (lin) */
static int32_t dv_lin(uint32_t v, int32_t lo, int32_t hi) { return lo + (int32_t)(((hi - lo) * (int32_t)v) / 127); }

static void dv_setup(dv_coef_t *c, const dv_param_t *p)
{
    uint32_t t = p->type < DVT_COUNT ? p->type : 0u, d = p->decay, tn = p->tone, x = p->extra;
    int32_t p16 = clamp(DV_DEF[t].p16 + p->tune, DV_DEF[t].lo, DV_DEF[t].hi), acc = p->accent;
    uint32_t tau = dv_tau(t, d), i;
    for (i = 0; i < sizeof *c / 4u; i++)
        ((uint32_t *)c)[i] = 0;
    c->type = (uint8_t)t;
    c->inc[0] = DV_P16(p16);
    c->metal = (int16_t)p16;
    /* output: the type's gain x LEVEL (127 = 1) x accent (+0..4 dB) */
    c->out = (int32_t)(DV_DEF[t].gain * (uint32_t)p->level / 127u);
    c->out += (c->out * acc * 75) >> 14;                 /* 1 + 0.585 a */
    switch (t) {
    case DVT_PUNCH:
    case DVT_ROUND: {
        int32_t punch = t == DVT_PUNCH, b = punch ? (int32_t)tn * 576 / 127 : (int32_t)tn * 192 / 127;   /* stage 2:
                                                          * +3 / +1 octave at TONE 127 */
        int32_t a = punch ? 384 : 0, g;                  /* stage 1: +2 octaves (p16) */
        c->span[1] = DV_P16(p16 + b) - c->inc[0];
        c->span[0] = DV_P16(p16 + a + b) - DV_P16(p16 + b);
        c->k[0] = dv_k(1500);                            /* attack sweep tau 1.5 ms */
        c->k[1] = dv_k(punch ? 8000u + tn * 32000u / 127u : 30000u + tn * 30000u / 127u);
        c->kb[0] = dv_kb(tau);
        c->hold = punch ? 17 : 0;                        /* blocks: 12 ms */
        c->g[0] = punch ? 2300 : 6500;                   /* 2nd harmonic -23 / -14 dB */
        c->g[1] = punch ? 1300 : 9800;                   /* 3rd -28 / -10 dB (ROUND: above 150 Hz) */
        c->inc[1] = DV_P16(punch ? 1636 : 1523);         /* click: 3 / 2 kHz, two cycles */
        c->inc[2] = c->inc[1] >> 1;
        c->click = (uint16_t)(0xFFFFFFFFu / c->inc[2]);
        c->g[2] = (punch ? 9000 : 5000) + acc * 24;      /* click level */
        g = 4096 + (int32_t)x * 3 * 4096 / 127;          /* DRIVE: x1 .. x4 into the soft clip, Q12 */
        c->g[3] = g;
        c->g[4] = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));   /* level kept */
        break;
    }
    case DVT_SNARE:
        c->span[0] = DV_P16(p16 + 11) - c->inc[0];       /* glide: +4 % */
        c->k[0] = dv_k(20000);
        c->k[1] = dv_k(tau * 2u / 5u);                   /* shell: 0.4 x the noise body */
        c->k[2] = dv_k(3000);                            /* noise spike */
        c->kb[0] = dv_kb(tau);
        tsvf_coef_k(&c->f[0], dv_cut(1715 + (int32_t)tn * 160 / 127 - 80), 5120);   /* 2.5 .. 7 kHz, Q 0.8 */
        c->hp = dv_pole(1777);                           /* bright noise above 5 kHz */
        c->g[0] = 19000;                                 /* 2nd shell sine */
        c->g[1] = dv_lin(tn, 8192, 16384) * 3 / 2;       /* shell level */
        c->g[2] = (int32_t)x * 34000 / 127;              /* SNAPPY: noise */
        c->g[3] = 26000 + acc * 50;                      /* spike */
        c->g[4] = 8000;                                  /* bright noise */
        break;
    case DVT_CLAP:
        tsvf_coef_k(&c->f[0], dv_cut(p16), 4096);        /* Q 1 */
        c->hp = dv_pole(1680 + p16 - DV_DEF[t].p16);     /* brightness: noise above 3.5 kHz (with TUNE) */
        c->k[0] = dv_k(5000);                            /* tooth tau */
        c->kb[0] = dv_kb(tau);
        c->hold = (uint16_t)(265u + x * 353u / 127u);    /* SPREAD: 6 .. 14 ms */
        c->click = x > 89u ? 4 : 3;
        c->g[0] = dv_lin(tn, 0, 9000);                   /* TONE: the bright copy */
        c->g[1] = 11000;                                 /* tail */
        c->g[2] = 30000 + acc * 20;                      /* teeth */
        break;
    case DVT_HATC:
    case DVT_HATO: {
        int32_t dp = p16 - DV_DEF[t].p16, cl = t == DVT_HATC, tp;
        tsvf_coef_k(&c->f[0], dv_cut(1874 + dp), 5120);  /* 7.1 kHz with TUNE, Q 0.8 */
        tp = (int32_t)tn * 384 / 127 - 192 + dp;          /* TONE: +-12 semitones on both high-passes */
        dv_hp1(&c->g[2], &c->g[3], (cl ? 2020 : 1934) + tp);   /* 12 / 8.8 kHz */
        dv_hp1(&c->g[4], &c->g[5], 2042 + tp);           /* the sizzle: 13 kHz */
        c->g[1] = (cl ? 7209 : 1147) * (int32_t)x / 64;   /* SIZZLE: the raw cluster, 0 .. 2 x the design (CL 1.76,
                                                          * OP 0.28 x the band, Q12) */
        c->kb[0] = dv_kb(tau);
        c->k[1] = dv_k(300);                             /* the rise: 0.3 ms */
        c->kb[1] = dv_kb(1500);                          /* the choke */
        c->out += (c->out * acc * 34) >> 14;             /* accent: +6 dB in all */
        break;
    }
    case DVT_TOM:
    case DVT_CONGA:
        c->kb[0] = dv_kb(tau);
        c->span[0] = DV_P16(p16 + (int32_t)x * 96 / 127) - c->inc[0];   /* BEND: up to +6 semitones */
        c->k[0] = dv_k(10000);                           /* stick */
        if (t == DVT_TOM) {                              /* the stick: low-passed noise, 300 Hz .. 2.7 kHz */
            c->hp = dv_pole(1302 + (int32_t)tn * 384 / 127 - 192);
            c->g[0] = dv_lin(tn, 1600, 6000);
        } else {                                         /* CONGA: a slap, noise above 2 kHz */
            c->hp = dv_pole(1523);
            c->g[2] = dv_lin(tn, 0, 6000);
        }
        c->g[1] = t == DVT_CONGA ? 6144 : 0;             /* soft clip x1.5 (Q12) */
        break;
    case DVT_RIM:
    case DVT_CLAVE:
        c->k[0] = dv_k(tau);
        c->inc[1] = DV_P16(p16 + 326);                   /* x3.25 */
        c->g[0] = t == DVT_RIM ? dv_lin(tn, 24000, 10000) : dv_lin(tn, 30000, 24000);   /* body */
        c->g[1] = t == DVT_RIM ? dv_lin(tn, 8000, 24000) : dv_lin(tn, 0, 9000);         /* crack */
        c->g[2] = 8192 + (int32_t)x * (t == DVT_RIM ? 24576 : 32768) / 127;   /* DRIVE x2..8 / x2..10, Q12 */
        c->g[2] += (c->g[2] * acc) >> 9;
        c->g[3] = 32767 * 4096 / c->g[2] + 9000;         /* (louder as it clips: a little) */
        break;
    case DVT_BELL:
        c->inc[1] = DV_P16(p16 + 109);                   /* x1.48 */
        tsvf_coef_k(&c->f[0], dv_cut(p16 + 130 + (int32_t)tn * 192 / 127 - 96), 3413);   /* 1.6 f, Q 1.2 */
        c->k[0] = dv_k(4000);
        c->kb[0] = dv_kb(tau);
        c->g[0] = dv_lin(x, 8000, 40000) + acc * 40;     /* STRIKE */
        c->g[1] = dv_lin(tn, 0, 5000);                   /* TONE: the squares themselves (brighter) */
        break;
    case DVT_CYM:
        tsvf_coef_k(&c->f[0], dv_cut(1870 + p16 - DV_DEF[t].p16), 5120);   /* 7 kHz with TUNE, Q 0.8 */
        tsvf_coef_k(&c->f[1], dv_cut(1678 + p16 - DV_DEF[t].p16), 5851);   /* 3.5 kHz, Q 0.7 */
        c->kb[0] = dv_kb(tau);                           /* ring */
        c->kb[1] = dv_kb(150000);                        /* strike / body */
        c->g[0] = dv_lin(tn, 6000, 26000);               /* high band */
        c->g[1] = 32767 - c->g[0];                       /* low band */
        c->g[2] = dv_lin(x, 8000, 32000) + acc * 60;     /* STRIKE: the strike over the ring */
        break;
    }
}

static void dv_metal_tune(dv_metal_t *b, const dv_coef_t *c)
{
    uint32_t i, inc = DV_P16(c->metal);
    const uint16_t *r = DV_METAL_R[c->type == DVT_CYM];
    for (i = 0; i < 6u; i++)
        b->inc[i] = (inc >> 12) * r[i];
}

static __attribute__((noinline)) void dv_metal_run(dv_metal_t *b, int32_t *y, uint32_t n)
{
    uint32_t i, p0 = b->ph[0], p1 = b->ph[1], p2 = b->ph[2], p3 = b->ph[3], p4 = b->ph[4], p5 = b->ph[5];
    for (i = 0; i < n; i++) {
        uint32_t s;
        p0 += b->inc[0];
        p1 += b->inc[1];
        p2 += b->inc[2];
        p3 += b->inc[3];
        p4 += b->inc[4];
        p5 += b->inc[5];
        s = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
        y[i] = (int32_t)s * 10922 - 32766;               /* six +-1 squares, Q15 */
    }
    b->ph[0] = p0, b->ph[1] = p1, b->ph[2] = p2, b->ph[3] = p3, b->ph[4] = p4, b->ph[5] = p5;
}

/* ----------------------------------------------------------------- the voices --- */
/* SVF band-pass (dsp.c's tsvf, its band output). No clamps: every input here is within +-32768 and Q at most
 * 1.25, so the states stay below about 2 x 2 x 32768 x 1.25 and a1 s below 2^31 */
static inline int32_t dv_bp(const tsvf_t *c, int32_t in, int32_t *s)
{
    int32_t v3 = in - s[1];
    int32_t v1 = (c->a1 * s[0] + c->a2 * v3) >> 13;
    int32_t v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return v1;
}
/* a slow envelope's block: the ramp start (Q20) and step per sample */
static inline void dv_slow(int32_t *q, uint32_t kb, int32_t *a, int32_t *d)
{
    *a = *q >> 10;
    *q = mulq16(*q, kb);
    *d = ((*q >> 10) - *a) >> CTL_LOG2;
}
#define DV_NOISE(v) ((int32_t)noise32(&(v)->rng) >> 16)  /* white, Q15 */
#define DV_QUIET (1 << 16)                               /* Q30: -84 dB */

static __attribute__((noinline)) void dv_kick_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, ph = v->ph[0], cph = v->ph[1], wph = v->ph[2], p1 = v->e[0], p2 = v->e[1], cn = v->n;
    uint32_t k2 = v->cnt ? 32768u : c->k[1], s1 = c->span[0] >> 16, s2 = c->span[1] >> 16;
    int32_t a, d;
    dv_slow(&v->q[0], v->cnt ? 65536u : c->kb[0], &a, &d);   /* (held) */
    if (v->cnt)
        v->cnt--;
    for (i = 0; i < n; i++) {
        int32_t b, x;
        ph += c->inc[0] + s1 * p1 + s2 * p2;
        p1 = (p1 * c->k[0]) >> 15;
        p2 = (p2 * k2) >> 15;
        b = sine_i(ph) + ((sine_i(ph << 1) * c->g[0]) >> 15) + ((sine_i(ph * 3u) * c->g[1]) >> 15);
        a += d;
        x = (b * (a >> 5)) >> 15;
        if (cn) {                                        /* the click: a windowed burst, mean 0 */
            int32_t w = (32768 - sine_i(wph + 0x40000000u)) >> 1;
            x += (((sine_i(cph) * w) >> 15) * c->g[2]) >> 15;
            cph += c->inc[1];
            wph += c->inc[2];
            cn--;
        }
        y[i] = (softclip((x * c->g[3]) >> 12) * c->g[4]) >> 15;
    }
    v->ph[0] = ph, v->ph[1] = cph, v->ph[2] = wph, v->e[0] = p1, v->e[1] = p2, v->n = (uint16_t)cn;
    v->live = v->q[0] > DV_QUIET;
}

static __attribute__((noinline)) void dv_snare_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, p0 = v->ph[0], p1 = v->ph[1], eg = v->e[0], es = v->e[1], ek = v->e[2], gs = c->span[0] >> 16;
    int32_t a, d, lp = v->s[2];
    dv_slow(&v->q[0], c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        uint32_t inc = c->inc[0] + gs * eg;
        int32_t sh, nz = DV_NOISE(v), bp, hp, x;
        eg = (eg * c->k[0]) >> 15;
        p0 += inc;
        p1 += inc + (inc >> 8) * 154u;                   /* x1.6 */
        sh = sine_i(p0) + ((sine_i(p1) * c->g[0]) >> 15);
        sh = (sh * (int32_t)(es >> 1)) >> 15;
        es = (es * c->k[1]) >> 15;
        bp = dv_bp(&c->f[0], nz, v->s);
        lp += ((nz - lp) * c->hp) >> 15;
        hp = nz - lp;
        a += d;
        x = (bp * (((((int32_t)(ek >> 1) * c->g[3]) >> 15) + (a >> 5)) >> 1)) >> 14;
        x += (hp * (((int32_t)(ek >> 1) * c->g[4]) >> 15)) >> 15;
        ek = (ek * c->k[2]) >> 15;
        x = ((sh * c->g[1]) >> 15) + ((x * c->g[2]) >> 15);
        y[i] = softclip((x * 5325) >> 12);               /* x1.3 */
    }
    v->ph[0] = p0, v->ph[1] = p1, v->e[0] = eg, v->e[1] = es, v->e[2] = ek, v->s[2] = lp;
    v->live = v->q[0] > DV_QUIET || es > 4u;
}

static __attribute__((noinline)) void dv_clap_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, et = v->e[0], cnt = v->n;
    int32_t a, d, lp = v->s[2];
    if (v->cnt == 0 && v->q[1] == 0) {                   /* the last tooth struck: the tail starts */
        v->q[0] = 1 << 30;
        v->q[1] = 1;
    }
    dv_slow(&v->q[0], c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        int32_t nz = DV_NOISE(v), x, env;
        if (v->cnt && !--cnt) {                          /* the next tooth */
            et = 65536u;
            cnt = c->hold;
            v->cnt--;
        }
        x = dv_bp(&c->f[0], nz, v->s);
        lp += ((nz - lp) * c->hp) >> 15;
        x += ((nz - lp) * c->g[0]) >> 15;
        a += d;
        env = (((int32_t)(et >> 1) * c->g[2]) >> 15) + (((a >> 5) * c->g[1]) >> 15);
        et = (et * c->k[0]) >> 15;
        y[i] = (x * (env >> 1)) >> 14;
    }
    v->e[0] = et, v->n = (uint16_t)cnt, v->s[2] = lp;
    v->live = v->cnt || v->q[0] > DV_QUIET || et > 4u;
}

/* the hats: band-pass -> high-pass, + the sizzle (the raw cluster high-passed), x (decay - rise) */
static __attribute__((noinline)) void dv_hat_run(const dv_coef_t *c, dv_voice_t *v, const int32_t *m, int32_t *y,
                                                 uint32_t n)
{
    uint32_t i, r = v->e[1];
    int32_t a, d, y1 = v->s[2], x1 = v->s[3], z1 = v->s[4], m1 = (int32_t)v->ph[0];
    dv_slow(&v->q[0], v->choke ? c->kb[1] : c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        int32_t b = dv_bp(&c->f[0], m[i], v->s), x;
        y1 = (c->g[2] * (b - x1) + c->g[3] * y1) >> 14;
        z1 = (c->g[4] * (m[i] - m1) + c->g[5] * z1) >> 14;
        x1 = b, m1 = m[i];
        x = y1 + ((z1 * c->g[1]) >> 12);
        a += d;
        y[i] = (x * (((a >> 5) - (int32_t)(r >> 1)) >> 2)) >> 13;   /* (x can pass 2^17: squares flipping
                                                          * together through the sizzle) */
        r = (r * c->k[1]) >> 15;
    }
    v->e[1] = r, v->s[2] = y1, v->s[3] = x1, v->s[4] = z1, v->ph[0] = (uint32_t)m1;
    v->live = v->q[0] > DV_QUIET;
}

static __attribute__((noinline)) void dv_tom_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, ph = v->ph[0], es = v->e[0], bs = c->span[0] >> 16;
    int32_t a, d, lp = v->s[2];
    dv_slow(&v->q[0], c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        int32_t al, x, nz;
        a += d;
        al = a >> 5;
        ph += c->inc[0] + bs * (uint32_t)((al * al) >> 14);   /* the pitch follows amplitude^2 (Q16) */
        x = (sine_i(ph) * al) >> 15;
        nz = DV_NOISE(v);
        lp += ((nz - lp) * c->hp) >> 15;                 /* the stick (low), the slap (high) */
        x += ((((lp * c->g[0]) >> 15) + (((nz - lp) * c->g[2]) >> 15)) * (int32_t)(es >> 1)) >> 15;
        es = (es * c->k[0]) >> 15;
        if (c->g[1])
            x = softclip((x * c->g[1]) >> 12);
        y[i] = x;
    }
    v->ph[0] = ph, v->e[0] = es, v->s[2] = lp;
    v->live = v->q[0] > DV_QUIET;
}

static __attribute__((noinline)) void dv_rim_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, p0 = v->ph[0], p1 = v->ph[1], e = v->e[0];
    for (i = 0; i < n; i++) {
        int32_t x;
        p0 += c->inc[0];
        p1 += c->inc[1];
        x = ((sine_i(p0) * c->g[0]) >> 15) + ((sine_i(p1) * c->g[1]) >> 15);
        x = (x * (int32_t)(e >> 1)) >> 15;
        e = (e * c->k[0]) >> 15;
        y[i] = (softclip((x * c->g[2]) >> 12) * c->g[3]) >> 15;
    }
    v->ph[0] = p0, v->ph[1] = p1, v->e[0] = e;
    v->live = e > 4u;
}

static __attribute__((noinline)) void dv_bell_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, p0 = v->ph[0], p1 = v->ph[1], e = v->e[0];
    int32_t a, d;
    dv_slow(&v->q[0], c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        int32_t x;
        p0 += c->inc[0];
        p1 += c->inc[1];
        x = (int32_t)((p0 >> 31) + (p1 >> 31)) * 16384 - 16384;   /* two squares, +-16384 each */
        x = dv_bp(&c->f[0], x, v->s) + ((x * c->g[1]) >> 15);
        a += d;
        y[i] = (x * (((a >> 5) + (((int32_t)(e >> 1) * c->g[0]) >> 15)) >> 2)) >> 13;
        e = (e * c->k[0]) >> 15;
    }
    v->ph[0] = p0, v->ph[1] = p1, v->e[0] = e;
    v->live = v->q[0] > DV_QUIET;
}

static __attribute__((noinline)) void dv_cym_run(const dv_coef_t *c, dv_voice_t *v, const int32_t *m, int32_t *y,
                                                 uint32_t n)
{
    uint32_t i;
    int32_t a, d, b, e;
    dv_slow(&v->q[0], c->kb[0], &a, &d);
    dv_slow(&v->q[1], c->kb[1], &b, &e);
    for (i = 0; i < n; i++) {
        int32_t hi = dv_bp(&c->f[0], m[i], v->s), lo = dv_bp(&c->f[1], m[i], v->s + 2), r, s;
        a += d;
        b += e;
        r = a >> 5;
        s = (b >> 5) * c->g[2] >> 15;
        y[i] = ((((hi * ((r + s) >> 1)) >> 15) * c->g[0]) >> 15) + ((((lo * s) >> 15) * c->g[1]) >> 15);
    }
    v->live = v->q[0] > DV_QUIET;
}

/* ------------------------------------------------------------------- the API --- */
static void dv_init(dv_voice_t *v, uint32_t type)
{
    uint32_t i;
    for (i = 0; i < sizeof *v / 4u; i++)
        ((uint32_t *)v)[i] = 0;
    v->type = (uint8_t)type;
}

static void dv_trigger(dv_voice_t *v) { v->trig = 1; }
static void dv_choke(dv_voice_t *v) { v->choke = 1; }
static uint32_t dv_uses_metal(uint32_t type) { return type == DVT_HATC || type == DVT_HATO || type == DVT_CYM; }

/* a hit: every state from rest (phases, filters, noise seed), so that each hit renders the same */
static void dv_strike(const dv_coef_t *c, dv_voice_t *v)
{
    dv_init(v, c->type);
    v->live = 1;
    v->rng = DV_SEED + c->type;
    v->e[0] = v->e[1] = v->e[2] = 65536u;
    v->q[0] = 1 << 30;
    switch (c->type) {
    case DVT_PUNCH:
    case DVT_ROUND:
        v->cnt = c->hold;
        v->n = c->click;
        break;
    case DVT_CLAP:
        v->q[0] = 0;
        v->cnt = (uint16_t)(c->click - 1u);              /* teeth after the first */
        v->n = c->hold;
        break;
    case DVT_CYM:
        v->q[1] = 1 << 30;
        break;
    }
}

/* the voice at its level (c->out), Q15, under full scale: linear to -6 dBFS, a soft knee above */
static __attribute__((noinline)) void dv_out(const dv_coef_t *c, int32_t *y, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t s = (y[i] * c->out) >> 12, a = s < 0 ? -s : s;
        if (a > 16384) {
            a = 16384 + (softclip((a - 16384) * 2) >> 1);   /* (at most 32179) */
            s = s < 0 ? -a : a;
        }
        y[i] = s;
    }
}

/* one block of the voice into y (Q15 at its level); metal: the metal source's block, or 0 */
static void dv_run(const dv_coef_t *c, dv_voice_t *v, const int32_t *metal, int32_t *y, uint32_t n)
{
    uint32_t i;
    if (v->trig)
        dv_strike(c, v);
    if (!v->live || v->type != c->type || (dv_uses_metal(c->type) && !metal)) {
        for (i = 0; i < n; i++)
            y[i] = 0;
        v->live = 0;
        return;
    }
    switch (c->type) {
    case DVT_PUNCH:
    case DVT_ROUND:
        dv_kick_run(c, v, y, n);
        break;
    case DVT_SNARE:
        dv_snare_run(c, v, y, n);
        break;
    case DVT_CLAP:
        dv_clap_run(c, v, y, n);
        break;
    case DVT_HATC:
    case DVT_HATO:
        dv_hat_run(c, v, metal, y, n);
        break;
    case DVT_TOM:
    case DVT_CONGA:
        dv_tom_run(c, v, y, n);
        break;
    case DVT_RIM:
    case DVT_CLAVE:
        dv_rim_run(c, v, y, n);
        break;
    case DVT_BELL:
        dv_bell_run(c, v, y, n);
        break;
    default:
        dv_cym_run(c, v, metal, y, n);
        break;
    }
    dv_out(c, y, n);
}

/* the designed parameters of a type (LEVEL 100, no accent) */
static void dv_default(dv_param_t *p, uint32_t type)
{
    p->type = (uint8_t)type;
    p->decay = DV_DEF[type].def[0];
    p->tone = DV_DEF[type].def[1];
    p->extra = DV_DEF[type].def[2];
    p->level = 100;
    p->accent = 0;
    p->tune = 0;
}
