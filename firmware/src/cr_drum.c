/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The drums' voices (the DRUMS plan, Phase 3; HISTORY 3y294): three drums, KICK, SNARE (crossfading to a CLAP past
 * the middle of its Character) and HATS (closed and open, the open one choked by the closed), each with four knobs:
 * Tune, Decay, Character, Level (0..127; Tune and Decay 64: as designed). From Felucca's drum_voice.c (removed in
 * e8f4b360; its run functions dv_kick_run, dv_snare_run, dv_clap_run, dv_hat_run, dv_metal_run and their helpers, the
 * sample loops as they were): the tom, rim, bell and cymbal and the 12-type dv_setup left behind.
 *
 * Every tuned part is a damped sine (the SINE table); noise is xorshift through dsp.c's trapezoidal SVF or a one-pole.
 * Envelopes: fast (Q16 per sample, e = (e * k) >> 15, k Q15), slow (Q30 per CTL block times a Q16 factor, ramped
 * inside the block).
 *   KICK   a sine swept in two stages, phase-locked 2nd and 3rd harmonics, a DC-free click, a soft-clip drive.
 *          Character 0: Felucca's ROUND (no attack stage, no hold, strong harmonics, a soft click, a longer body);
 *          127: PUNCH (+2 octaves of attack, a 12 ms hold, a bright click, more drive)
 *   SNARE  two sines (f, 1.6 f) under a shell envelope, band-passed noise under a spike and a body. Character 0..64:
 *          dark and loose .. bright and snappy; 64..127: crossfades to the CLAP (noise through a band-pass struck
 *          three times, then a tail)
 *   HATS   the metal source (six squares at the classic analog hats' ratios) through a band-pass and a high-pass per
 *          hat, plus the sizzle. Character: the high-passes' TONE and the sizzle, dull .. bright. The open hat rings
 *          longer; a closed hat chokes it (a 1.5 ms release)
 * Character interpolates each drum's parameters between a soft set and a hard set (crd_lerp); Tune moves the
 * designed pitch +-12 semitones, Decay the main time over the designed span. A voice stops computing once it has rung
 * out (-66 dB: CRD_QUIET, CRD_EQUIET). The voices sum into one bus at their levels, a soft knee above -6 dBFS
 * (crd_knee), into the master's mix before the limiter (fx.c mix_block).
 *
 * Use: crd_setup(k) after drum k's knobs changed (the main loop; not yet double-buffered: the test sets them once),
 * crd_hit(v) for a hit (the ISR's clock), cr_drum_mix per block (the ISR). CR_DRUM_TEST (choralroot.c, 0 by
 * default): the voices in the master and a fixed busy pattern on the tempo's 16ths, to measure the CPU on a unit. */

enum { CRD_KICK, CRD_SNARE, CRD_HAT, CRD_N };                    /* the drums */
enum { CRV_KICK, CRV_SNARE, CRV_CLAP, CRV_HATC, CRV_HATO, CRV_N };   /* their voices */

typedef struct { uint8_t tune, decay, chr, level; } crd_knob_t;  /* 0..127 */

typedef struct {                                         /* a voice's coefficients (crd_setup; meaning per voice) */
    uint32_t inc[3];                                     /* phase increments, cycles Q32 */
    uint32_t span[2];                                    /* pitch sweeps (increment above inc[0]) */
    uint32_t k[3];                                       /* fast decays per sample, Q15 */
    uint32_t kb[2];                                      /* slow decays per block, Q16 */
    int32_t g[6];                                        /* gains, Q15 unless noted */
    tsvf_t f;                                            /* the band-pass */
    int32_t hp;                                          /* one-pole coefficient, Q15 */
    int32_t out;                                         /* output gain: the voice's, Level (Q12); 0: not played */
    uint16_t hold, click;                                /* kick: hold (blocks), click length (samples); clap: tooth
                                                          * spacing (samples), teeth */
} crd_coef_t;

typedef struct {
    uint8_t trig, choke, live, pad;
    uint16_t n, cnt;                                     /* counters: samples, teeth / hold blocks */
    uint32_t ph[3];
    uint32_t e[3];                                       /* fast envelopes, Q16 */
    int32_t q[2];                                        /* slow envelopes, Q30 */
    int32_t s[5];                                        /* filter states */
    int32_t rng;
} crd_voice_t;

static crd_knob_t crd_knob[CRD_N] = {{64, 64, 64, 100}, {64, 64, 32, 100}, {64, 64, 64, 90}};
static crd_coef_t crd_c[CRV_N];
static crd_voice_t crd_v[CRV_N];
static uint32_t crd_metal_ph[6], crd_metal_inc[6];       /* the hats' six squares */

#define CRD_SEED 0x5EED1234
#define CRD_P16(p16) pitch_inc((uint32_t)clamp((p16), 0, 2047))
#define CRD_QUIET (1 << 19)                              /* a slow envelope's -66 dB (Q30) */
#define CRD_EQUIET 33u                                   /* a fast one's (Q16) */
/* the hats' cluster: 205.3, 304.4, 369.6, 522.7, 540, 800 Hz over 205.3 (Q12) */
static const uint16_t CRD_METAL_R[6] = {4096, 6073, 7374, 10428, 10774, 15961};

/* ------------------------------------------------------- setup helpers (not in the sample loops) --- */
/* 2^(x / 4096), 0 <= x < 16 * 4096, Q16 (a cubic for the fraction: within 0.01 %) */
static uint32_t crd_exp2(uint32_t x)
{
    uint32_t f = x & 4095u, m;
    m = 65536u + ((f * (45594u + ((f * (14851u + ((f * 5092u) >> 12))) >> 12))) >> 12);
    return m << (x >> 12);
}
/* tau (us) -> per-sample decay, Q15: e^(-1 / (tau fs)), to the second order */
static uint32_t crd_k(uint32_t us)
{
    uint32_t x = 743039u / (us | 1u);
    return x >= 32768u ? 0u : 32768u - x + ((x * x) >> 16);
}
/* tau (us) -> per-block decay, Q16: e^(-CTL / (tau fs)), to the fourth order */
static uint32_t crd_kb(uint32_t us)
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
/* the main tau (us) at Decay d: tau0 (0.1 ms) at 0, oct16 octaves x 16 more at 127 */
static uint32_t crd_tau(uint32_t tau0, uint32_t oct16, uint32_t d)
{
    uint32_t m = crd_exp2(d * oct16 * 4096u / (127u * 16u));   /* Q16, below 2^21 */
    return (tau0 * ((m * 100u) >> 12)) >> 4;
}
/* p16 -> the SVF's cutoff index (0..127 << 8: 30 Hz .. 16 kHz) */
static int32_t crd_cut(int32_t p16) { return clamp(((p16 - 360) * 4785) >> 8, 0, 127 << 8); }
/* p16 -> one-pole coefficient, Q15: w / (1 + w), w = 2 pi f / fs */
static int32_t crd_pole(int32_t p16)
{
    int32_t w = (int32_t)(((CRD_P16(p16) >> 17) * 205887u) >> 15);
    return (w << 15) / (32768 + w);
}
/* p16 -> a bilinear one-pole high-pass (s / (s + w), not prewarped): y = g (x - x1) + p y1, g and p in Q14 */
static void crd_hp1(int32_t *g, int32_t *p, int32_t p16)
{
    uint32_t inc = p16 > 1900 ? CRD_P16(clamp(p16, 0, 2239) - 192) << 1 : CRD_P16(p16);   /* (above the table: an
                                                          * octave down, doubled) */
    int32_t u = (int32_t)(((inc >> 17) * 205887u) >> 15);   /* w / 2fs = pi f / fs, Q16 */
    *g = (65536 << 14) / (65536 + u);
    *p = (65536 - u) * 16384 / (65536 + u);
}
/* Character h (0..127) between a soft value and a hard one */
static int32_t crd_lerp(int32_t soft, int32_t hard, uint32_t h) { return soft + (hard - soft) * (int32_t)h / 127; }
/* Tune: +-12 semitones (p16: 16 a semitone) around the designed pitch */
static int32_t crd_tune(uint32_t t) { return ((int32_t)t - 64) * 3; }

static void crd_zero(void *p, uint32_t n) { for (uint32_t i = 0; i < n / 4u; i++) ((uint32_t *)p)[i] = 0; }

static void crd_kick_setup(crd_coef_t *c, const crd_knob_t *kn)
{
    uint32_t h = kn->chr;
    int32_t p16 = clamp(crd_lerp(533, 502, h) + crd_tune(kn->tune), 403, 664);   /* 56 .. 50 Hz */
    int32_t a = crd_lerp(0, 384, h), b = crd_lerp(96, 290, h), g;   /* the attack stage, the second (p16) */
    crd_zero(c, sizeof *c);
    c->inc[0] = CRD_P16(p16);
    c->span[1] = CRD_P16(p16 + b) - c->inc[0];
    c->span[0] = CRD_P16(p16 + a + b) - CRD_P16(p16 + b);
    c->k[0] = crd_k(1500);                               /* attack sweep tau 1.5 ms */
    c->k[1] = crd_k((uint32_t)crd_lerp(45118, 24126, h));
    c->kb[0] = crd_kb(crd_tau((uint32_t)crd_lerp(200, 100, h), (uint32_t)crd_lerp(69, 75, h), kn->decay));
    c->hold = (uint16_t)crd_lerp(0, 17, h);              /* blocks: 0 .. 12 ms */
    c->g[0] = crd_lerp(6500, 2300, h);                   /* 2nd harmonic -14 .. -23 dB */
    c->g[1] = crd_lerp(9800, 1300, h);                   /* 3rd -10 .. -28 dB */
    c->inc[1] = CRD_P16(crd_lerp(1523, 1636, h));        /* click: 2 .. 3 kHz, two cycles */
    c->inc[2] = c->inc[1] >> 1;
    c->click = (uint16_t)(0xFFFFFFFFu / c->inc[2]);
    c->g[2] = crd_lerp(5000, 9000, h);                   /* click level */
    g = 4096 + crd_lerp(40, 80, h) * 3 * 4096 / 127;     /* drive into the soft clip, Q12 */
    c->g[3] = g;
    c->g[4] = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));   /* level kept */
    c->out = 3224 * (int32_t)kn->level / 127;
}

static void crd_snare_setup(crd_coef_t *c, crd_coef_t *cl, const crd_knob_t *kn)
{
    uint32_t h = kn->chr < 64u ? kn->chr * 2u : 127u, tn = (uint32_t)crd_lerp(30, 100, h), x = (uint32_t)crd_lerp(50, 110, h);
    int32_t p16 = clamp(856 + crd_tune(kn->tune), 744, 958), xf;   /* 180 Hz */
    uint32_t tau = crd_tau(300, 37, kn->decay);
    crd_zero(c, sizeof *c);
    c->inc[0] = CRD_P16(p16);
    c->span[0] = CRD_P16(p16 + 11) - c->inc[0];          /* glide: +4 % */
    c->k[0] = crd_k(20000);
    c->k[1] = crd_k(tau * 2u / 5u);                      /* shell: 0.4 x the noise body */
    c->k[2] = crd_k(3000);                               /* noise spike */
    c->kb[0] = crd_kb(tau);
    tsvf_coef_k(&c->f, crd_cut(1715 + (int32_t)tn * 160 / 127 - 80), 5120);   /* 2.5 .. 7 kHz, Q 0.8 */
    c->hp = crd_pole(1777);                              /* bright noise above 5 kHz */
    c->g[0] = 19000;                                     /* 2nd shell sine */
    c->g[1] = crd_lerp(8192, 16384, tn) * 3 / 2;         /* shell level */
    c->g[2] = (int32_t)x * 34000 / 127;                  /* snappy: noise */
    c->g[3] = 26000;                                     /* spike */
    c->g[4] = 8000;                                      /* bright noise */
    /* the clap: band 1.1 kHz with Tune, three teeth 10 ms apart, then the tail */
    p16 = clamp(1358 + crd_tune(kn->tune), 1233, 1523);
    crd_zero(cl, sizeof *cl);
    tsvf_coef_k(&cl->f, crd_cut(p16), 4096);             /* Q 1 */
    cl->hp = crd_pole(1680 + p16 - 1358);                /* brightness: noise above 3.5 kHz */
    cl->k[0] = crd_k(5000);                              /* tooth tau */
    cl->kb[0] = crd_kb(crd_tau(200, 53, kn->decay));
    cl->hold = 441;                                      /* 10 ms */
    cl->click = 3;
    cl->g[0] = 4535;                                     /* the bright copy */
    cl->g[1] = 11000;                                    /* tail */
    cl->g[2] = 30000;                                    /* teeth */
    /* Character past the middle: the snare fades out as the clap fades in (Q15) */
    xf = kn->chr > 64u ? ((int32_t)kn->chr - 64) * 32768 / 63 : 0;
    c->out = (int32_t)(4080 * (int32_t)kn->level / 127 * (32768 - xf)) >> 15;
    cl->out = (int32_t)(8000 * (int32_t)kn->level / 127 * xf) >> 15;
}

static void crd_hat_setup(crd_coef_t *c, uint32_t open, const crd_knob_t *kn)
{
    uint32_t h = kn->chr;
    int32_t dp = crd_tune(kn->tune), tp = crd_lerp(30, 100, h) * 384 / 127 - 192 + dp;   /* the high-passes +-12 st */
    crd_zero(c, sizeof *c);
    tsvf_coef_k(&c->f, crd_cut(1874 + dp), 5120);        /* 7.1 kHz with Tune, Q 0.8 */
    crd_hp1(&c->g[2], &c->g[3], (open ? 1934 : 2020) + tp);   /* 8.8 / 12 kHz */
    crd_hp1(&c->g[4], &c->g[5], 2042 + tp);              /* the sizzle: 13 kHz */
    c->g[1] = (open ? 1147 : 7209) * crd_lerp(32, 110, h) / 64;   /* the raw cluster (Q12) */
    c->kb[0] = crd_kb(open ? crd_tau(250, 63, kn->decay) : crd_tau(60, 45, kn->decay));
    c->k[1] = crd_k(300);                                /* the rise: 0.3 ms */
    c->kb[1] = crd_kb(1500);                             /* the choke */
    c->out = (open ? 14040 : 9015) * (int32_t)kn->level / 127;
}

/* drum k's knobs into its voices' coefficients */
static void crd_setup(uint32_t k)
{
    const crd_knob_t *kn = &crd_knob[k];
    if (k == CRD_KICK) {
        crd_kick_setup(&crd_c[CRV_KICK], kn);
    } else if (k == CRD_SNARE) {
        crd_snare_setup(&crd_c[CRV_SNARE], &crd_c[CRV_CLAP], kn);
    } else {
        uint32_t inc = CRD_P16(clamp(893 + crd_tune(kn->tune), 701, 1085));   /* 205 Hz */
        crd_hat_setup(&crd_c[CRV_HATC], 0, kn);
        crd_hat_setup(&crd_c[CRV_HATO], 1, kn);
        for (uint32_t i = 0; i < 6u; i++)
            crd_metal_inc[i] = (inc >> 12) * CRD_METAL_R[i];
    }
}

/* ----------------------------------------------------------------- the voices --- */
/* SVF band-pass (dsp.c's tsvf, its band output). No clamps: every input here is within +-32768 and Q at most 1.25,
 * so the states stay below about 2 x 2 x 32768 x 1.25 and a1 s below 2^31 */
static inline int32_t crd_bp(const tsvf_t *c, int32_t in, int32_t *s)
{
    int32_t v3 = in - s[1];
    int32_t v1 = (c->a1 * s[0] + c->a2 * v3) >> 13;
    int32_t v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return v1;
}
/* a slow envelope's block: the ramp start (Q20) and step per sample */
static inline void crd_slow(int32_t *q, uint32_t kb, int32_t *a, int32_t *d)
{
    *a = *q >> 10;
    *q = mulq16(*q, kb);
    *d = ((*q >> 10) - *a) >> CTL_LOG2;
}
#define CRD_NOISE(v) ((int32_t)noise32(&(v)->rng) >> 16)  /* white, Q15 */

static __attribute__((noinline)) void cr_drum_metal(int32_t *y, uint32_t n)
{
    uint32_t i, p0 = crd_metal_ph[0], p1 = crd_metal_ph[1], p2 = crd_metal_ph[2], p3 = crd_metal_ph[3];
    uint32_t p4 = crd_metal_ph[4], p5 = crd_metal_ph[5];
    for (i = 0; i < n; i++) {
        uint32_t s;
        p0 += crd_metal_inc[0];
        p1 += crd_metal_inc[1];
        p2 += crd_metal_inc[2];
        p3 += crd_metal_inc[3];
        p4 += crd_metal_inc[4];
        p5 += crd_metal_inc[5];
        s = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
        y[i] = (int32_t)s * 10922 - 32766;               /* six +-1 squares, Q15 */
    }
    crd_metal_ph[0] = p0, crd_metal_ph[1] = p1, crd_metal_ph[2] = p2;
    crd_metal_ph[3] = p3, crd_metal_ph[4] = p4, crd_metal_ph[5] = p5;
}

static __attribute__((noinline)) void cr_drum_kick(const crd_coef_t *c, crd_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, ph = v->ph[0], cph = v->ph[1], wph = v->ph[2], p1 = v->e[0], p2 = v->e[1], cn = v->n;
    uint32_t k2 = v->cnt ? 32768u : c->k[1], s1 = c->span[0] >> 16, s2 = c->span[1] >> 16;
    int32_t a, d;
    crd_slow(&v->q[0], v->cnt ? 65536u : c->kb[0], &a, &d);   /* (held) */
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
    v->live = v->q[0] > CRD_QUIET;
}

static __attribute__((noinline)) void cr_drum_snare(const crd_coef_t *c, crd_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, p0 = v->ph[0], p1 = v->ph[1], eg = v->e[0], es = v->e[1], ek = v->e[2], gs = c->span[0] >> 16;
    int32_t a, d, lp = v->s[2];
    crd_slow(&v->q[0], c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        uint32_t inc = c->inc[0] + gs * eg;
        int32_t sh, nz = CRD_NOISE(v), bp, hp, x;
        eg = (eg * c->k[0]) >> 15;
        p0 += inc;
        p1 += inc + (inc >> 8) * 154u;                   /* x1.6 */
        sh = sine_i(p0) + ((sine_i(p1) * c->g[0]) >> 15);
        sh = (sh * (int32_t)(es >> 1)) >> 15;
        es = (es * c->k[1]) >> 15;
        bp = crd_bp(&c->f, nz, v->s);
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
    v->live = v->q[0] > CRD_QUIET || es > CRD_EQUIET;
}

static __attribute__((noinline)) void cr_drum_clap(const crd_coef_t *c, crd_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, et = v->e[0], cnt = v->n;
    int32_t a, d, lp = v->s[2];
    if (v->cnt == 0 && v->q[1] == 0) {                   /* the last tooth struck: the tail starts */
        v->q[0] = 1 << 30;
        v->q[1] = 1;
    }
    crd_slow(&v->q[0], c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        int32_t nz = CRD_NOISE(v), x, env;
        if (v->cnt && !--cnt) {                          /* the next tooth */
            et = 65536u;
            cnt = c->hold;
            v->cnt--;
        }
        x = crd_bp(&c->f, nz, v->s);
        lp += ((nz - lp) * c->hp) >> 15;
        x += ((nz - lp) * c->g[0]) >> 15;
        a += d;
        env = (((int32_t)(et >> 1) * c->g[2]) >> 15) + (((a >> 5) * c->g[1]) >> 15);
        et = (et * c->k[0]) >> 15;
        y[i] = (x * (env >> 1)) >> 14;
    }
    v->e[0] = et, v->n = (uint16_t)cnt, v->s[2] = lp;
    v->live = v->cnt || v->q[0] > CRD_QUIET || et > CRD_EQUIET;
}

/* a hat: band-pass -> high-pass, + the sizzle (the raw cluster high-passed), x (decay - rise) */
static __attribute__((noinline)) void cr_drum_hat(const crd_coef_t *c, crd_voice_t *v, const int32_t *m, int32_t *y,
                                                  uint32_t n)
{
    uint32_t i, r = v->e[1];
    int32_t a, d, y1 = v->s[2], x1 = v->s[3], z1 = v->s[4], m1 = (int32_t)v->ph[0];
    crd_slow(&v->q[0], v->choke ? c->kb[1] : c->kb[0], &a, &d);
    for (i = 0; i < n; i++) {
        int32_t b = crd_bp(&c->f, m[i], v->s), x;
        y1 = (c->g[2] * (b - x1) + c->g[3] * y1) >> 14;
        z1 = (c->g[4] * (m[i] - m1) + c->g[5] * z1) >> 14;
        x1 = b, m1 = m[i];
        x = y1 + ((z1 * c->g[1]) >> 12);
        a += d;
        y[i] = (x * (((a >> 5) - (int32_t)(r >> 1)) >> 2)) >> 13;   /* (x can pass 2^17: squares flipping together
                                                          * through the sizzle) */
        r = (r * c->k[1]) >> 15;
    }
    v->e[1] = r, v->s[2] = y1, v->s[3] = x1, v->s[4] = z1, v->ph[0] = (uint32_t)m1;
    v->live = v->q[0] > CRD_QUIET;
}

/* ------------------------------------------------------------------- the API --- */
/* a hit on voice k (the ISR, before cr_drum_mix): the closed hat chokes the open one */
static void crd_hit(uint32_t k)
{
    if (k >= CRV_N || !crd_c[k].out)
        return;
    crd_v[k].trig = 1;
    if (k == CRV_HATC)
        crd_v[CRV_HATO].choke = 1;
}

/* a hit: every state from rest (phases, filters, noise seed), so that each hit renders the same */
static void crd_strike(uint32_t k)
{
    crd_voice_t *v = &crd_v[k];
    crd_zero(v, sizeof *v);
    v->live = 1;
    v->rng = CRD_SEED + (int32_t)k;
    v->e[0] = v->e[1] = v->e[2] = 65536u;
    v->q[0] = 1 << 30;
    if (k == CRV_KICK) {
        v->cnt = crd_c[k].hold;
        v->n = crd_c[k].click;
    } else if (k == CRV_CLAP) {
        v->q[0] = 0;
        v->cnt = (uint16_t)(crd_c[k].click - 1u);        /* teeth after the first */
        v->n = crd_c[k].hold;
    }
}

/* the drums' block into the master's mix (l, r: Q15 units, before the limiter); nothing when none rings */
static __attribute__((noinline)) void cr_drum_mix(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t bus[CTL], y[CTL], m[CTL];
    uint32_t i, k, any = 0;
    for (k = 0; k < CRV_N; k++)
        if (crd_v[k].trig)
            crd_strike(k);
    if (crd_v[CRV_HATC].live || crd_v[CRV_HATO].live)
        cr_drum_metal(m, n);
    for (k = 0; k < CRV_N; k++) {
        crd_voice_t *v = &crd_v[k];
        const crd_coef_t *c = &crd_c[k];
        int32_t g = c->out;
        if (!v->live)
            continue;
        switch (k) {
        case CRV_KICK: cr_drum_kick(c, v, y, n); break;
        case CRV_SNARE: cr_drum_snare(c, v, y, n); break;
        case CRV_CLAP: cr_drum_clap(c, v, y, n); break;
        default: cr_drum_hat(c, v, m, y, n); break;
        }
        if (!any)
            for (i = 0; i < n; i++) bus[i] = (y[i] * g) >> 12;
        else
            for (i = 0; i < n; i++) bus[i] += (y[i] * g) >> 12;
        any = 1;
    }
    if (!any)
        return;
    for (i = 0; i < n; i++) {                            /* linear to -6 dBFS, a soft knee above (at most 32179) */
        int32_t s = bus[i], a = s < 0 ? -s : s;
        if (a > 16384) {
            a = 16384 + (softclip((a - 16384) * 2) >> 1);
            s = s < 0 ? -a : a;
        }
        l[i] += s;
        r[i] += s;
    }
}

static __attribute__((noinline)) void crd_init(void)
{
    crd_zero(crd_v, sizeof crd_v);
    crd_zero(crd_metal_ph, sizeof crd_metal_ph);
    for (uint32_t k = 0; k < CRD_N; k++)
        crd_setup(k);
}

#if CR_DRUM_TEST
/* the CPU test (CR_DRUM_TEST=1 ./build.sh): a fixed busy bar on the tempo's 16ths, whatever the loop does: the kick on
 * the quarters, the snare (at Character 96: the snare and the clap both sound) on 2 and 4, a hat on every 16th, the
 * open one on the 8ths (choked by the closed one a 16th later: both hats and the metal ring in every block) */
static uint32_t crd_test_step = ~0u;
/* rt: the tempo clock's tick, t16 its ticks a 16th (choralroot.c); out of line, as crd_init: the ISR only calls them */
static __attribute__((noinline)) void cr_drum_test_clock(uint32_t rt, uint32_t t16)
{
    uint32_t step = rt / t16;
    if (step == crd_test_step)
        return;
    if (crd_test_step == ~0u) {
        crd_knob[CRD_SNARE].chr = 96;
        crd_init();
    }
    crd_test_step = step;
    step &= 15u;
    if (!(step & 3u))
        crd_hit(CRV_KICK);
    if (step == 4u || step == 12u) {
        crd_hit(CRV_SNARE);
        crd_hit(CRV_CLAP);
    }
    crd_hit(step & 1u ? CRV_HATC : CRV_HATO);
}
#endif
