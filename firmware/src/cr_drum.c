/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 CoralFlower contributors (FicusFicusov, <https://github.com/FicusFicusov/CoralFlower>)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The drums' voices (the DRUMS plan, Phase 3; HISTORY 3y294, 3y295): CoralFlower's morph drum synth (its drum_synth.c,
 * 0.6, 7c44dc1: "the second drum engine", GPL-3.0-only, CoralFlower being a fork of ChoralRoot) with the helpers it
 * took from Felucca's drum_voice.c (dv_exp2, dv_k, dv_kb, dv_slow, the metal ratios; removed here in e8f4b360).
 * CoralFlower's notes, kept:
 *
 * Written from general techniques, not from another firmware's code: the analog drum machines' circuits (a sine with a
 * falling pitch for the kick and the toms, oscillators and filtered noise for the snare, a noise burst struck several
 * times for the clap, six square waves for the hats and the cowbell), Gordon Reid's "Synth Secrets" (Sound On Sound,
 * 1999-2004) and the "oscillator + noise per drum, two settings and a morph" layout plug-ins made common.
 *
 * A drum sound (ds_snd_t) is:
 *   an oscillator: SINE or ADD (a second sine added), its pitch falling from BEND above to its own over BTAU, its
 *     level decaying over ODEC;
 *   noise: white, METAL (the six squares of the classic analog hats), RING (inharmonic squares times white noise:
 *     softer, a noisier metal) or FM, the cymbals' metal: three sines, each bent by a sine of its own (FM: frequency
 *     modulation, its depth FM), so their partials crowd into a metallic cluster that thins out as the hit dies;
 *     each with white noise over it as GAP sets (the sizzle);
 *     struck once or as a clap's TEETH (GAP apart, then a tail), decaying over NDEC;
 *   one state-variable filter (LP, BP or HP, CUT, RES) on the noise, on the oscillator too with ROUTE;
 *   a CLICK (a 0.6 ms burst of noise at the hit), a soft-clip DRIVE (the level kept), a LEVEL (0.5 dB a step, 96 as
 *   is: A to B moves it in dB).
 * A kit can add GRIT (fewer samples and bits, as an old sampler: lo-fi) and a ROOM (a second, quieter stage under
 * every decay, as a room's tail after the drum's own) to all its sounds.
 * Every sound of a kit has two settings, A (a soft hit) and B (a hard one). VARY (a kit's) moves each hit: its morph,
 * its tune by a few cents and its decays by a few percent, and its noise runs on instead of starting the same each
 * time. A voice that has rung out (-66 dB) stops computing.
 *
 * ChoralRoot's changes: five sounds (kick, snare, clap, closed and open hat; CoralFlower's tom, rim, bell and cymbal and
 * the oscillator's TRI, FM and SQ2 waves, which only those played, left out). The morph is each drum's own Character
 * (CoralFlower: one MORPH a kit; the kit's is the Character at rest): KICK's for the kick, HATS' for both hats, SNARE's
 * for the snare up to 64 (x2), past it the snare fading out as the CLAP fades in, the clap's morph going 0..127 with it.
 * Tune moves the pitches, the metal and the filter +-12 semitones, Decay the oscillator's and the noise's times +-2
 * octaves, Level the drum's level; 64 is the kit as voiced. The kits (cr_drum_kits.h) live in the resource region on
 * the FM-1: crd_kit_load copies one into RAM in the main loop (the ISR never reads the region), or says it is not
 * there (cr_res.h CR_RES_GONE): then the beat is the metronome's click.
 *
 * Use: crd_kit_load (the main loop), crd_hit at a hit (the ISR's clock; the coefficients are set there, once),
 * cr_drum_mix per block (the ISR): every voice ringing, summed under a soft knee into the master's mix before the
 * limiter (fx.c mix_block). All out of line: the audio ISR only calls them. CR_DRUM_TEST (choralroot.c, 0 by
 * default): the drums in the master and a fixed busy pattern on the tempo's 16ths, to measure the CPU on a unit. */

#ifndef CF_DRUM_VARY
#define CF_DRUM_VARY 1
#endif

enum { DSS_KICK, DSS_SNARE, DSS_CLAP, DSS_HATC, DSS_HATO, DSS_N };   /* the sounds (CoralFlower's first five) */
enum { DSW_SINE, DSW_TRI, DSW_FM, DSW_ADD, DSW_SQ2 };           /* (CoralFlower's numbers; SINE and ADD played here) */
enum { DSN_NONE, DSN_WHITE, DSN_METAL, DSN_RING, DSN_FM };
enum { DSF_LP, DSF_BP, DSF_HP };
enum { CRD_KICK, CRD_SNARE, CRD_HAT, CRD_N };                    /* the drums, each with its knobs */

typedef struct {
    uint8_t wave, src, flt;                      /* (A's, not morphed) */
    uint8_t teeth;                               /* a clap's hands (0: the noise struck once) */
    uint8_t route;                               /* 1: the oscillator through the filter too */
    int16_t pitch;                               /* the oscillator, p16 (16 x MIDI note) */
    int16_t mpitch;                              /* METAL / RING / FM: the lowest square, p16 */
    uint16_t ratio;                              /* the second oscillator over the first, Q12 */
    uint8_t bend, btau, odec, o2, olev;          /* 0..127: BEND p16 / 4 (up to 32 semitones), the times DS_TAU */
    uint8_t cut, res, ndec, nlev;                /* CUT: the SVF's index (30 Hz .. 16 kHz, 0.86 semitone a step) */
    uint8_t fm;                                  /* FM noise: its depth (0..127: up to 4 pi rad); RING: the bare
                                                  * squares' share (127: as loud as the ring) */
    uint8_t gap;                                 /* a clap: its teeth apart; METAL, RING, FM: their sizzle, white noise
                                                  * over the metal (127: as loud as it) */
    uint8_t click, drive, level;
} ds_snd_t;

typedef struct {
    ds_snd_t a[DSS_N], b[DSS_N];
    uint8_t morph;                               /* the hits' place between A (0) and B (127): the Characters at rest */
    uint8_t vary;                                /* each hit's spread (0: none) */
    uint8_t rate, bits;                          /* the kit's grit (lo-fi): every RATE-th sample held (1: none), at BITS
                                                  * bits (0: as is) */
    uint8_t room, rtau;                          /* its room: a second, quieter stage of every sound's decay (ROOM its
                                                  * level, 0: none; RTAU its time, DS_TAU) */
} crd_kit_t;

typedef struct {
    uint32_t inc, span, kp, r2;                  /* the oscillator (Q32 cycles), its bend above inc, the bend's decay
                                                  * a sample (Q15), the second one's ratio (Q12) */
    uint32_t kbo, kbn, kch;                      /* decays a block (Q16): the oscillator, the noise, a choke */
    uint32_t kc, kt;                             /* decays a sample (Q15): the click, a clap's tooth */
    int32_t gf, gz;                              /* FM noise: its depth (Q15 of 4 pi rad), its sizzle (Q15) */
    int32_t go2, go, gn, gc, gd, gm, out, fk;    /* gains (Q15; DRIVE, its make-up, the output Q12); the SVF's damping
                                                  * (Q12) */
    tsvf_t f;
    uint32_t minc[6], mph[6];                    /* the six squares; FM: the three carriers, then their modulators */
    uint32_t ph, ph2, pe, ce, te;                /* phases; the bend's, the click's, the tooth's envelopes (Q16) */
    int32_t qo, qn;                              /* the oscillator's and the noise's envelopes (Q30) */
    int32_t s0, s1, rng;
    uint16_t gap, cnt;                           /* a clap: samples between its teeth, to the next one */
    uint8_t wave, src, flt, route, teeth;
    uint8_t tail;                                /* 1: struck once, 2: a clap's tail runs, 0: its teeth still come */
    uint8_t live, choke;
    uint8_t hn, hc;                              /* the grit: hold every hn-th sample, the count to the next */
    int32_t hv, hq;                              /* .. the sample held, the bits' mask */
    int32_t gr, qr;                              /* the room: its level (Q15), its envelope (Q30) */
    uint32_t kbr;                                /* .. its decay a block (Q16) */
} ds_voice_t;

typedef struct { uint8_t tune, decay, chr, level; } crd_knob_t;  /* 0..127 (tune, decay 64: as voiced) */

#include "cr_drum_kits.h"                        /* CRD_KITS: X Club, X Deep (CoralFlower's) */

static crd_kit_t crd_kit;                        /* the kit playing: a RAM copy (crd_kit_load) */
static uint8_t crd_kit_ok;                       /* 1: crd_kit holds a kit; 0: none (the region refused: the click) */
static crd_knob_t crd_knob[CRD_N] = {{64, 64, 32, 100}, {64, 64, 32, 100}, {64, 64, 32, 100}};
static ds_voice_t crd_v[DSS_N];
static int32_t ds_rng = 0x2F6B1D3;               /* VARY's dice (one for every sound: the hits' order picks) */

#define CRD_P16(p16) pitch_inc((uint32_t)clamp((p16), 0, 2047))
#define CRD_NOISE(v) ((int32_t)noise32(&(v)->rng) >> 16)  /* white, Q15 */

/* ------------------------------------------------------- setup helpers (Felucca's drum_voice.c) --- */
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
/* a slow envelope's block: the ramp start (Q20) and step per sample */
static inline void crd_slow(int32_t *q, uint32_t kb, int32_t *a, int32_t *d)
{
    *a = *q >> 10;
    *q = mulq16(*q, kb);
    *d = ((*q >> 10) - *a) >> CTL_LOG2;
}
/* 0..127 -> a time (us): 0.3 ms x 2^(v / 10), 0.3 ms .. 2 s */
static uint32_t ds_tau(uint32_t v)
{
    return ((crd_exp2(v * 4096u / 10u) >> 8) * 300u) >> 8;
}

/* A and B at m (0..127), value by value; the kinds are A's */
static void ds_morph(ds_snd_t *s, const ds_snd_t *a, const ds_snd_t *b, int32_t m)
{
    const uint8_t *pa = &a->bend, *pb = &b->bend;
    uint8_t *ps = &s->bend;
    uint32_t i;
    *s = *a;
    s->pitch = (int16_t)(a->pitch + (b->pitch - a->pitch) * m / 127);
    s->mpitch = (int16_t)(a->mpitch + (b->mpitch - a->mpitch) * m / 127);
    s->ratio = (uint16_t)(a->ratio + ((int32_t)b->ratio - a->ratio) * m / 127);
    for (i = 0; i <= __builtin_offsetof(ds_snd_t, level) - __builtin_offsetof(ds_snd_t, bend); i++)
        ps[i] = (uint8_t)(pa[i] + (pb[i] - pa[i]) * m / 127);
}

/* a hit of sound sn of the kit at morph m (0..127), Tune and Decay (0..127, 64 as voiced), level lvl (0..100 %) */
static __attribute__((noinline)) void ds_strike(ds_voice_t *v, const crd_kit_t *K, uint32_t sn, int32_t m, uint32_t tune,
                                                uint32_t decay, uint32_t lvl)
{
    const ds_snd_t *A = &K->a[sn % DSS_N], *B = &K->b[sn % DSS_N];
    uint32_t vary = K->vary;
    static const uint16_t R_HAT[6] = {4096, 6073, 7374, 10428, 10774, 15961};   /* (drum_voice.c's: the analog hats') */
    static const uint16_t R_CYM[6] = {4096, 5800, 6560, 7530, 8810, 10650};
    static const uint16_t R_FM[6] = {4096, 6502, 9889, 5492, 6627, 9159};   /* FM: the carriers 1, 1.587 (2^(2/3)),
                                                  * 2.414 (1 + sqrt 2) and their modulators 1.341, 1.618 (the golden
                                                  * ratio), 2.236 (sqrt 5) of MPITCH: no two in a whole ratio */
    ds_snd_t s;
    int32_t r[4] = {0, 0, 0, 0}, p16, g, dt = ((int32_t)tune - 64) * 3, dd = ((int32_t)decay - 64) * 20 / 64;
    uint32_t i, ft, tv, rng = (uint32_t)v->rng;
    const uint16_t *mr;
#if !CF_DRUM_VARY
    vary = 0;
#endif
    if (vary)
        for (i = 0; i < 4u; i++)
            r[i] = (int32_t)noise32(&ds_rng) >> 16;     /* -32768 .. 32767 */
    ds_morph(&s, A, B, clamp(m + ((r[0] * (int32_t)vary) >> 16), 0, 127));
    /* (ChoralRoot: Tune +-12 semitones on the pitches and the filter, Decay +-2 octaves on the two times) */
    s.cut = (uint8_t)clamp(s.cut + dt * 14 / 48, 0, 127);
    s.odec = (uint8_t)clamp(s.odec + dd, 0, 127);
    s.ndec = (uint8_t)clamp(s.ndec + dd, 0, 127);
    s.mpitch = (int16_t)(s.mpitch + dt);
    for (i = 0; i < sizeof *v / 4u; i++)
        ((uint32_t *)v)[i] = 0;
    v->wave = s.wave, v->src = s.src, v->flt = s.flt, v->route = s.route;
    v->teeth = (uint8_t)(s.teeth ? s.teeth - 1u : 0u);       /* (the teeth after the first) */
    ft = 65536u + (uint32_t)((r[1] * (int32_t)vary) >> 12);   /* tune: +-0.5 % at VARY 40 (Q16) */
    tv = 65536u + (uint32_t)((r[2] * (int32_t)vary) >> 11);   /* the decays: +-1 % */
    p16 = s.pitch + dt;
    v->inc = CRD_P16(p16);
    v->span = CRD_P16(p16 + s.bend * 4) - v->inc;
    v->inc = (uint32_t)(((uint64_t)v->inc * ft) >> 16);
    v->span = (uint32_t)(((uint64_t)v->span * ft) >> 16);
    v->kp = crd_k(ds_tau(s.btau));
    v->r2 = s.ratio;
    v->kbo = crd_kb((uint32_t)(((uint64_t)ds_tau(s.odec) * tv) >> 16));
    v->kbn = crd_kb((uint32_t)(((uint64_t)ds_tau(s.ndec) * tv) >> 16));
    v->kch = crd_kb(1500);                                    /* a choke: 1.5 ms */
    v->kc = crd_k(600);
    v->go2 = s.o2 * 258;
    v->go = s.olev * 258;
    v->gn = s.nlev * 258;
    v->gc = s.click * 258;
    g = 4096 + s.drive * 3 * 4096 / 127;                      /* DRIVE: x1 .. x4 into the soft clip, Q12 */
    v->gd = s.drive ? g : 0;
    v->gm = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));   /* (the level kept) */
    v->out = (int32_t)((crd_exp2(s.level * 4096u / 12u) >> 12) * (lvl < 100u ? lvl : 100u) / 100u);   /* LEVEL: 0.5
                                                  * dB a step, 96 is x1 (Q12), 127 +15.5 dB */
    v->out += (v->out * ((r[3] * (int32_t)vary) >> 8)) >> 15;  /* the level: +-8 % at VARY 40 */
    v->fk = 8192 - s.res * 6144 / 127;                         /* damping 2 .. 0.5 (Q up to 2) */
    tsvf_coef_k(&v->f, s.cut << 8, v->fk);
    mr = s.src == DSN_RING ? R_CYM : s.src == DSN_FM ? R_FM : R_HAT;
    for (i = 0; i < 6u; i++)
        v->minc[i] = (CRD_P16(s.mpitch) >> 12) * mr[i];
    v->gf = s.fm * 258;
    v->gz = s.src >= DSN_METAL ? s.gap * 258 : 0;
    if (vary) {                                               /* (the noise and the squares run on) */
        v->rng = (int32_t)(rng ? rng : 0x5EED1234u);
        for (i = 0; i < 6u; i++)
            v->mph[i] = noise32(&ds_rng);
    } else {
        v->rng = (int32_t)(0x5EED1234u + s.cut * 977u + (uint32_t)s.pitch);
    }
    v->pe = v->te = 65536u;
    v->ce = v->gc ? 65536u : 0u;                              /* (no click: none to run) */
    v->qo = v->go ? 1 << 30 : 0;
    v->gap = (uint16_t)(176u + s.gap * 3u);                   /* 4 .. 12.6 ms */
    v->cnt = v->gap;
    v->kt = crd_k(v->gap * 10u);                              /* a tooth: tau 0.45 x the gap */
    if (!s.teeth) {                                           /* (struck once: no tail to start) */
        v->qn = v->gn ? 1 << 30 : 0;
        v->te = 0;
        v->tail = 1;
    }
    v->hn = (uint8_t)(K->rate > 1u ? K->rate : 0u);         /* the grit */
    v->hc = 1;
    v->hq = K->bits ? -(int32_t)(65536u >> K->bits) : -1;     /* (the bits' mask: the samples' lowest bits off) */
    v->gr = K->room * 258 >> (sn == DSS_HATC ? 2 : 0);        /* the room (a closed hat's a quarter: it is short, its
                                                               * tail held it open) */
    v->qr = v->gr ? 1 << 30 : 0;
    v->kbr = crd_kb((uint32_t)(((uint64_t)ds_tau(K->rtau) * tv) >> 16));
    v->live = 1;
}

/* ---------------------------------------------------------------- a block, in passes --- */
/* Each pass keeps its kind's choice out of the sample loop: the oscillator's wave, the noise's source, the filter's
 * output, the drive. */

/* the oscillator into o, at its envelope (ao, dao: crd_slow's ramp) and level */
#define DS_OSC(EXPR)                                                                                                   \
    for (i = 0; i < n; i++) {                                                                                          \
        ao += dao;                                                                                                     \
        pe = (pe * kp) >> 15;                                                                                          \
        inc = inc0 + s1 * pe;                                                                                          \
        ph += inc;                                                                                                     \
        EXPR;                                                                                                          \
        o[i] = (((x * (ao >> 5)) >> 15) * go) >> 15;                                                                   \
    }
static __attribute__((noinline)) void ds_osc(ds_voice_t *v, int32_t *o, uint32_t n, int32_t ao, int32_t dao)
{
    uint32_t i, inc, ph = v->ph, ph2 = v->ph2, pe = v->pe, s1 = v->span >> 16, inc0 = v->inc, kp = v->kp, r2 = v->r2;
    int32_t x, go = v->go, go2 = v->go2;
    if (v->wave == DSW_SINE)
        DS_OSC(x = sine_i(ph))
    else                                                          /* ADD (the five sounds of every kit: SINE or ADD) */
        DS_OSC(ph2 += (inc >> 12) * r2; x = (sine_i(ph) + ((sine_i(ph2) * go2) >> 15)) >> 1)
    v->ph = ph, v->ph2 = ph2, v->pe = pe;
}

/* the six squares (drum_voice.c's dv_metal_run), Q15 */
static __attribute__((noinline)) void ds_metal(ds_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, p0 = v->mph[0], p1 = v->mph[1], p2 = v->mph[2], p3 = v->mph[3], p4 = v->mph[4], p5 = v->mph[5];
    for (i = 0; i < n; i++) {
        p0 += v->minc[0], p1 += v->minc[1], p2 += v->minc[2], p3 += v->minc[3], p4 += v->minc[4], p5 += v->minc[5];
        y[i] = (int32_t)((p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31)) * 10922 - 32766;
    }
    v->mph[0] = p0, v->mph[1] = p1, v->mph[2] = p2, v->mph[3] = p3, v->mph[4] = p4, v->mph[5] = p5;
}

#define DS_SIN(ph) ((int32_t)SINE[(ph) >> 22])          /* (the table's point, not between two: a cluster of partials
                                                          * hides the -55 dB it costs, at half the work) */
/* the cymbals' metal (FM): three sine carriers, each bent by its own sine (gi: the depth now, Q15 of 4 pi rad), a
 * third of each, Q15. No white noise in it: the partials are many, but each one is a tone */
static __attribute__((noinline)) void ds_fmetal(ds_voice_t *v, int32_t *y, uint32_t n, int32_t gi)
{
    uint32_t i, c0 = v->mph[0], c1 = v->mph[1], c2 = v->mph[2], m0 = v->mph[3], m1 = v->mph[4], m2 = v->mph[5];
    for (i = 0; i < n; i++) {
        int32_t x;
        c0 += v->minc[0], c1 += v->minc[1], c2 += v->minc[2], m0 += v->minc[3], m1 += v->minc[4], m2 += v->minc[5];
        x = DS_SIN(c0 + ((uint32_t)((DS_SIN(m0) * gi) >> 15) << 18));
        x += DS_SIN(c1 + ((uint32_t)((DS_SIN(m1) * gi) >> 15) << 18));
        x += DS_SIN(c2 + ((uint32_t)((DS_SIN(m2) * gi) >> 15) << 18));
        y[i] = (x * 10922) >> 15;
    }
    v->mph[0] = c0, v->mph[1] = c1, v->mph[2] = c2, v->mph[3] = m0, v->mph[4] = m1, v->mph[5] = m2;
}

/* the noise into y, at its envelope (an, dan) and level, a clap's teeth over it */
static __attribute__((noinline)) void ds_noise(ds_voice_t *v, int32_t *y, uint32_t n, int32_t an, int32_t dan)
{
    uint32_t i, te = v->te;
    int32_t gn = v->gn;
    if (v->src == DSN_FM)                                         /* (the depth falls with the hit, to 0.4 of it: the
                                                                   * cluster thins, a ring stays) */
        ds_fmetal(v, y, n, (v->gf * (13107 + (((an >> 5) * 19661) >> 15))) >> 15);
    else if (v->src != DSN_WHITE)
        ds_metal(v, y, n);
    if (v->src == DSN_WHITE)
        for (i = 0; i < n; i++)
            y[i] = CRD_NOISE(v);
    else if (v->src == DSN_RING)                                  /* (the squares times white noise, the squares
                                                                   * themselves as FM sets, white over it) */
        for (i = 0; i < n; i++) {
            int32_t w = CRD_NOISE(v), m = y[i];
            y[i] = ((m * w) >> 15) + ((m * v->gf) >> 15) + ((w * v->gz) >> 15);
        }
    else if (v->gz)                                               /* (METAL, FM: a sizzle of white over the metal) */
        for (i = 0; i < n; i++)
            y[i] += (CRD_NOISE(v) * v->gz) >> 15;
    if (!v->teeth && v->tail != 2 && te <= 4u) {                  /* struck once */
        for (i = 0; i < n; i++) {
            an += dan;
            y[i] = (((y[i] * (an >> 5)) >> 15) * gn) >> 15;
        }
        return;
    }
    for (i = 0; i < n; i++) {                                     /* a clap */
        int32_t e;
        an += dan;
        if (v->teeth && !--v->cnt) {                              /* its next tooth */
            te = 65536u;
            v->cnt = v->gap;
            v->teeth--;
        }
        e = (v->tail == 2 ? ((an >> 5) * 13107) >> 15 : an >> 5) + (int32_t)(te >> 1);   /* (its tail: 0.4) */
        te = (te * v->kt) >> 15;
        y[i] = (((y[i] * (e > 32767 ? 32767 : e)) >> 15) * gn) >> 15;
    }
    v->te = te;
}

/* the filter (A. Simper's SVF, dsp.c's): the noise in y, the oscillator o in it too (r: -1) or after it (r: 0) */
#define DS_SVF(OUT)                                                                                                    \
    for (i = 0; i < n; i++) {                                                                                          \
        int32_t in = y[i] + (o[i] & r), v3 = in - z1, v1, v2;                                                          \
        v1 = (v->f.a1 * z0 + v->f.a2 * v3) >> 13;                                                                      \
        v2 = z1 + ((v->f.a2 * z0 + v->f.a3 * v3) >> 13);                                                               \
        z0 = clamp(2 * v1 - z0, -150000, 150000);                                                                      \
        z1 = clamp(2 * v2 - z1, -150000, 150000);                                                                      \
        y[i] = (OUT) + (o[i] & ~r);                                                                                    \
    }
static __attribute__((noinline)) void ds_filter(ds_voice_t *v, int32_t *y, const int32_t *o, uint32_t n)
{
    uint32_t i;
    int32_t z0 = v->s0, z1 = v->s1, r = v->route ? -1 : 0, fk = v->fk;
    switch (v->flt) {
    case DSF_LP: DS_SVF(v2); break;
    case DSF_BP: DS_SVF(v1); break;
    default: DS_SVF(in - ((fk * v1) >> 12) - v2); break;
    }
    v->s0 = z0, v->s1 = z1;
}

/* the click, the drive, the level and the knee (linear to -6 dBFS) */
static __attribute__((noinline)) void ds_out(ds_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, ce = v->ce;
    int32_t out = v->out;
    for (i = 0; i < n && ce > 4u; i++) {
        y[i] += (((CRD_NOISE(v) * (int32_t)(ce >> 1)) >> 15) * v->gc) >> 15;
        ce = (ce * v->kc) >> 15;
    }
    v->ce = ce;
    if (v->gd)
        for (i = 0; i < n; i++)
            y[i] = (softclip((y[i] * v->gd) >> 12) * v->gm) >> 15;
    if (v->hn || v->hq != -1) {                                   /* the grit: fewer samples, fewer bits */
        int32_t hv = v->hv, hq = v->hq;
        uint32_t hc = v->hc;
        for (i = 0; i < n; i++) {
            if (!--hc) {
                hv = (y[i] + ((~hq + 1) >> 1)) & hq;              /* (rounded to the bits) */
                hc = v->hn ? v->hn : 1u;
            }
            y[i] = hv;
        }
        v->hv = hv, v->hc = (uint8_t)hc;
    }
    for (i = 0; i < n; i++) {
        int32_t x = (y[i] * out) >> 12, a = x < 0 ? -x : x;
        if (a > 16384) {
            a = 16384 + (softclip((a - 16384) * 2) >> 1);
            x = x < 0 ? -a : a;
        }
        y[i] = x;
    }
}

#define DS_END (1 << 19)                         /* (Q30: a decay ends at -66 dB, drum_voice.c's at -84: the last 18 dB
                                                  * of a long tail cost as much as a hit, under every sound with it) */
#define DS_ROOM_END (1 << 20)                    /* (Q30: the room's tail ends at -60 dB of the hit: under every other
                                                  * drum, and a hit's voice lives no longer than its sound) */
static __attribute__((noinline)) void ds_run(ds_voice_t *v, int32_t *y, uint32_t n)
{
    static const int32_t Z[CTL];
    int32_t o[CTL], ao, dao, an, dan;
    uint32_t i;
    if (!v->teeth && !v->tail) {                                  /* a clap's last tooth struck: its tail starts */
        v->qn = 1 << 30;
        v->tail = 2;
    }
    crd_slow(&v->qo, v->choke ? v->kch : v->kbo, &ao, &dao);
    crd_slow(&v->qn, v->choke ? v->kch : v->kbn, &an, &dan);
    if (v->gr) {                                                  /* the room: its stage under both decays (Q20) */
        int32_t ar, dar, t, dt;
        crd_slow(&v->qr, v->choke ? v->kch : v->kbr, &ar, &dar);
        t = ((ar >> 5) * v->gr) >> 10;
        dt = (dar * v->gr) >> 15;
        if (v->go)
            ao += t, dao += dt;
        if (v->gn)
            an += t, dan += dt;
    }
    if (v->go)
        ds_osc(v, o, n, ao, dao);
    if (v->src) {
        ds_noise(v, y, n, an, dan);
        ds_filter(v, y, v->go ? o : Z, n);
    } else if (v->route) {                                        /* (the oscillator alone through the filter) */
        for (i = 0; i < n; i++)
            y[i] = 0;
        ds_filter(v, y, o, n);
    } else {
        for (i = 0; i < n; i++)
            y[i] = v->go ? o[i] : 0;
    }
    ds_out(v, y, n);
    v->live = v->qo > DS_END || v->qn > DS_END || (v->qr >> 15) * v->gr > DS_ROOM_END || v->teeth || v->te > 4u ||
              v->ce > 4u;
}

/* ------------------------------------------------------------------- ChoralRoot's API --- */
/* kit k into RAM (the main loop): 1, or 0 when it is not there (cr_res.h: the resource region refused; nothing read) */
static int crd_kit_load(uint32_t k)
{
    const crd_kit_t *K = &CRD_KITS[k < CRD_NKIT ? k : 0u];
    if (CR_RES_GONE(K)) {
        crd_kit_ok = 0;
        return 0;
    }
    memcpy(&crd_kit, K, sizeof crd_kit);
    crd_kit_ok = 1;
    return 1;
}

/* a hit of sound sn (the ISR, before cr_drum_mix): its drum's knobs; the snare's Character past 64 fades the snare out
 * and the clap in; the closed hat chokes the open one */
static __attribute__((noinline)) void crd_hit(uint32_t sn)
{
    const crd_knob_t *kn;
    uint32_t c, lvl, m, xf;
    if (sn >= DSS_N || !crd_kit_ok)
        return;
    kn = &crd_knob[sn == DSS_KICK ? CRD_KICK : sn <= DSS_CLAP ? CRD_SNARE : CRD_HAT];
    c = kn->chr;
    lvl = kn->level * 100u / 127u;
    m = c;
    if (sn == DSS_SNARE || sn == DSS_CLAP) {
        xf = c > 64u ? (c - 64u) * 127u / 63u : 0u;           /* the clap's share, 0..127 */
        if (sn == DSS_SNARE) {
            m = c < 64u ? c * 2u : 127u;
            lvl = lvl * (127u - xf) / 127u;
        } else {
            m = xf;
            lvl = lvl * xf / 127u;
        }
    }
    if (!lvl)
        return;
    if (sn == DSS_HATC)
        crd_v[DSS_HATO].choke = 1;
    ds_strike(&crd_v[sn], &crd_kit, sn, (int32_t)m, kn->tune, kn->decay, lvl);
}

/* the drums' block into the master's mix (l, r: Q15 units, before the limiter); nothing when none rings */
static __attribute__((noinline)) void cr_drum_mix(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t bus[CTL], y[CTL];
    uint32_t i, k, any = 0;
    for (k = 0; k < DSS_N; k++) {
        if (!crd_v[k].live)
            continue;
        ds_run(&crd_v[k], y, n);
        if (!any)
            for (i = 0; i < n; i++) bus[i] = y[i];
        else
            for (i = 0; i < n; i++) bus[i] += y[i];
        any = 1;
    }
    if (!any)
        return;
    for (i = 0; i < n; i++) {                            /* the sum: linear to -6 dBFS, a soft knee above */
        int32_t s = bus[i], a = s < 0 ? -s : s;
        if (a > 16384) {
            a = 16384 + (softclip((a - 16384) * 2) >> 1);
            s = s < 0 ? -a : a;
        }
        l[i] += s;
        r[i] += s;
    }
}

#if CR_DRUM_TEST
/* the CPU test (CR_DRUM_TEST=1 ./build.sh): a fixed busy bar on the tempo's 16ths, whatever the loop does: the kick on
 * the quarters, the snare (at Character 96: the snare and the clap both sound) on 2 and 4, a hat on every 16th, the
 * open one on the 8ths (choked by the closed one a 16th later: both hats ring in every block). Kit X Club, VARY at the
 * kit's. crd_test_boot: the main loop, after the resource region's check (main.c) */
static uint32_t crd_test_step = ~0u;
static uint8_t crd_test_state;                           /* 0 not yet booted, 1 the drums, 2 the region refused */
static void crd_test_boot(void)
{
    uint32_t k;
    for (k = 0; k < CRD_N; k++)
        crd_knob[k].chr = 32;                            /* (X Club's MORPH; not read from the region: it may be gone) */
    crd_knob[CRD_SNARE].chr = 96;
    crd_test_state = crd_kit_load(0) ? 1 : 2;
}
/* rt: the tempo clock's tick, t16 its ticks a 16th (choralroot.c); 0 when the kits are not there (the click instead) */
static __attribute__((noinline)) int cr_drum_test_clock(uint32_t rt, uint32_t t16)
{
    uint32_t step = rt / t16;
    if (crd_test_state != 1u)
        return crd_test_state != 2u;
    if (step == crd_test_step)
        return 1;
    crd_test_step = step;
    step &= 15u;
    if (!(step & 3u))
        crd_hit(DSS_KICK);
    if (step == 4u || step == 12u) {
        crd_hit(DSS_SNARE);
        crd_hit(DSS_CLAP);
    }
    crd_hit(step & 1u ? DSS_HATC : DSS_HATO);
    return 1;
}
#endif
