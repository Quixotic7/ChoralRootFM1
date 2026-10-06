/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* PERFORM: the effects of the FX hold layer, on the master. A key pressed while FX is held belongs to the
 * layer (seq.c keyboard_block): it plays no note, sends no MIDI, records nothing, and holds its effect until
 * it is let go (ui_input.c opens the layer and shows the map). The 10 white keys from the left (F3 .. A4):
 *   REPEAT 1/8 1/16 1/32, REVERSE, LPF | HPF, TAPE STOP, FREEZE, OCT UP, OCT DN;  the other 6 white keys do
 *   nothing; black keys 1..4 (F#3 G#3 A#3 C#4): tracks 1..4 muted while held (not P_MUTE, never saved).
 * REPEAT and REVERSE start on the next 1/16 (at once while stopped); the others at once; all end when let go,
 * with a 2.9 ms ramp. Effects of different kinds stack; of the buffer ones (REPEAT, REVERSE, TAPE STOP,
 * FREEZE, OCT UP, OCT DN) the last pressed plays, and letting it go returns to the one held before.
 * OCT UP / OCT DN: a harmonizer, the mix an octave up / down beside itself (after the author's earlier
 * fxHarmonizer, the classic rotating-delay shifter): two read taps of a delay sweep 128 .. 2176 samples, half a sweep apart, at the
 * speed that makes the pitch 2x / 0.5x; a raised-cosine cross-fade (the sine table) hides each tap's jump
 * back; reads between samples, linear. Left and right apart, at 44.1 kHz in the loop's frames (4096 of
 * them). Out: 0.56 the live mix + 0.7 the shifted one (its 0.55, 0.7). While it plays, DEPTH (KNOB 4) is its
 * SHIMMER instead of the level: the shifted sound fed back into its own delay (0 .. 0.82, through the soft
 * clip): with OCT UP each pass climbs another octave.
 * The buffer: the SLICER's recordings (sl_buf, 32 KB) borrowed as one stereo loop of 8192 frames at 22.05 kHz
 * (371 ms). While borrowed, STUT tracks play live; their recordings are dropped afterwards. A REPEAT 1/8 or
 * REVERSE longer than the loop at the tempo (below 81 BPM) does nothing (the map shows it dimmed).
 * KNOB 1..4 with FX: the macros FILTER (the LPF / HPF), CRUSH, THROW (the dry mix into the delay and reverb
 * sends), DEPTH (the buffer effects' level; OCT UP / DN: the shimmer).
 * Chain: [REPEAT / REVERSE / TAPE / FREEZE] -> LPF -> HPF -> CRUSH, after the master level and before
 * master_out (the limiter); THROW and the mutes act before the buses (fx.c mix_block / mix_part).
 * Idle (no key, no knob, no ramp left) every stage is skipped: the output is bit-identical. */
enum { PF_R8, PF_R16, PF_R32, PF_REV, PF_LPF, PF_HPF, PF_TAPE, PF_FRZ, PF_OUP, PF_ODN, PF_M1, PF_N = PF_M1 + NTRK };
#define PF_BIT(e) (1u << (e))
#define PF_REPEAT 0x7u                                         /* the three REPEAT rates */
#define PF_Q (PF_REPEAT | PF_BIT(PF_REV))                      /* start on the 1/16 */
#define PF_HARM (PF_BIT(PF_OUP) | PF_BIT(PF_ODN))
#define PF_BUF (PF_REPEAT | PF_BIT(PF_REV) | PF_BIT(PF_TAPE) | PF_BIT(PF_FRZ) | PF_HARM)
#define PF_MUTE (((1u << NTRK) - 1u) << PF_M1)
#define PB_FRAMES (NTRK * SL_LEN / 2u)        /* stereo frames in sl_buf: 8192, 371 ms at 22.05 kHz */
#define PB_MAX (2u * PB_FRAMES)               /* the longest loop, 44.1 kHz samples */
#define PB_TAPE 32000u                        /* TAPE STOP takes 1 beat, at most this (it lags a quarter of it) */
#define PB_FREC 4096u                         /* FREEZE: frames it records (186 ms) */
#define PB_GRAIN 2048u                        /* .. its two overlapping grains, samples (46 ms) */
#define HB_MASK 4095u                         /* OCT UP / DN: the delay's frames (of the loop's 8192) */
#define HB_BASE 128u                          /* .. the taps' shortest delay, samples; the sweep: 2048 */
#define HB_FB 269                             /* .. KNOB 4 (0 .. 100) -> the shimmer, Q15: 0 .. 0.82 */
#define PF_TOP (63 << 8)                      /* filter cutoff index, Q8 (PF_SVF): the LPF's open end */
#define PF_LOW (14 << 8)                      /* .. the LPF sweep's end, the HPF sweep's (and K1's) top */
enum { BM_NONE, BM_LOOP, BM_TAPE, BM_FRZ, BM_HARM };

static volatile uint32_t perf_mask;   /* main: the FX button's bit while its layer may own keys, 0 = none */
static volatile uint32_t kb_mask;     /* main: the button bits whose hold makes keys a layer's (ui_layer.c), 0 = none */
static volatile uint8_t perf_solo;    /* main: tracks soloed in the GLO layer (the others muted as by a black key) */
static volatile uint8_t perf_kill;    /* main: every effect off (the menu, a dialog, UBOOT) */
static volatile int8_t perf_k[4];     /* main: the knob macros, 0 = untouched: FILTER -100..100 (- LPF, + HPF),
                                       * CRUSH 0..100, THROW 0..100, DEPTH cut 0..100 (the buffer effects' level) */
static volatile uint32_t kb_layer;    /* ISR: the keys held that are the layer's, a bit per key */
static volatile uint32_t perf_held;   /* ISR: the effects held (PF_*) */
static volatile uint32_t perf_act;    /* ISR: .. running (a 1/16 one from its start on) */
static uint32_t perf_ord[PF_N], perf_seq;   /* press order: the last pressed buffer effect plays */

/* the ZDF state-variable filter at cutoff index i (30 Hz * 2^(i / 7), 0..63: up to 15.4 kHz), k = 1:
 * a1 a2 a3 in Q14 (tools: g = tan(pi fc / fs), a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2) */
static const int16_t PF_SVF[64][3] = {
    {16349, 35, 0}, {16345, 39, 0}, {16341, 43, 0}, {16337, 47, 0}, {16332, 52, 0}, {16327, 57, 0}, {16321, 63, 0},
    {16314, 70, 0}, {16307, 77, 0}, {16299, 85, 0}, {16290, 94, 1}, {16280, 103, 1}, {16269, 114, 1},
    {16257, 126, 1}, {16244, 139, 1}, {16229, 153, 1}, {16213, 169, 2}, {16196, 186, 2}, {16176, 205, 3},
    {16154, 227, 3}, {16130, 250, 4}, {16104, 275, 5}, {16075, 303, 6}, {16043, 334, 7}, {16007, 368, 8},
    {15968, 406, 10}, {15925, 447, 13}, {15877, 492, 15}, {15824, 541, 19}, {15766, 596, 22}, {15702, 655, 27},
    {15631, 720, 33}, {15553, 791, 40}, {15467, 869, 49}, {15372, 953, 59}, {15267, 1046, 72}, {15151, 1146, 87},
    {15024, 1255, 105}, {14883, 1374, 127}, {14729, 1502, 153}, {14559, 1640, 185}, {14372, 1790, 223},
    {14166, 1950, 268}, {13940, 2121, 323}, {13692, 2304, 388}, {13420, 2499, 465}, {13122, 2704, 557},
    {12797, 2921, 667}, {12441, 3147, 796}, {12053, 3382, 949}, {11631, 3624, 1129}, {11173, 3870, 1341},
    {10677, 4119, 1589}, {10140, 4365, 1879}, {9562, 4605, 2218}, {8940, 4832, 2612}, {8274, 5040, 3070},
    {7563, 5219, 3602}, {6807, 5359, 4219}, {6006, 5444, 4934}, {5163, 5456, 5766}, {4282, 5369, 6733},
    {3372, 5149, 7863}, {2449, 4745, 9190},
};

static struct {
    uint32_t ph, P, split;             /* samples since play; the 1/16; this block: where a 1/16 starts */
    uint32_t act, act0;                /* running after / before split */
    uint8_t busy, sync, mute;          /* sync: a 1/16 starts with the next block; mute: tracks to ramp */
    /* the buffer */
    uint8_t mode, src, next, rev;      /* BM_*, the effect it plays (PF_N none), one waiting for the fade, REVERSE */
    uint8_t state, odd, xrev;          /* LOOP: 0 recording, 1 playing; the sample of a pair; the old loop's */
    uint32_t len, wr, rp;              /* loop length (samples), frames written, read (samples into the loop) */
    uint32_t xrp, xlen, xn;            /* the loop it switched from, faded over xn samples */
    uint32_t tv, dv, tpos;             /* TAPE: speed Q16, its step, read position Q16 frames */
    uint32_t gp[2], gs[2];             /* FREEZE: grain positions (samples), starts (frames) */
    uint32_t hph, hinc;                /* OCT UP / DN: tap A's place in the sweep (Q32), its step a sample */
    int32_t hfb;                       /* .. the shimmer now, Q15 (glides to KNOB 4's a block at a time) */
    int32_t w, tw, hl, hr;             /* the buffer's share and its target, Q15; the first of a pair */
    /* the filters, CRUSH, THROW, mutes */
    int32_t lc, hc, la, ha;            /* cutoffs (Q8 index) and shares (Q15) of LPF and HPF */
    int32_t lz[4], hz[4];              /* their states: L ic1 ic2, R ic1 ic2 */
    int16_t lk[3], hk[3];              /* their coefficients this block */
    uint32_t lt, ht;                   /* samples the LPF / HPF key has been held */
    int32_t cw, cc, chl, chr;          /* CRUSH share, amount, held sample */
    uint32_t cn;
    int32_t td;                        /* THROW: the share of the dry mix sent, Q15 */
    int32_t mg[NTRK];                  /* mute gains, Q15 (32768 = open) */
} pf = {.src = PF_N, .next = PF_N, .lc = PF_TOP, .mg = {32768, 32768, 32768, 32768}};

/* the loop length of a REPEAT / REVERSE (1/8) at the tempo, 44.1 kHz samples; 0 = not one */
static const uint8_t PF_DEN[PF_REV + 1] = {2, 4, 8, 2};
static uint32_t perf_len(uint32_t e) { return e <= PF_REV ? beat_samples() / PF_DEN[e] : 0u; }
/* the effects that can run at the tempo: a REPEAT / REVERSE longer than the loop cannot */
static uint32_t perf_avail(void)
{
    uint32_t e, m = ~0u, b = beat_samples();
    for (e = PF_R8; e <= PF_REV; e++)
        if (b > PB_MAX * PF_DEN[e])
            m &= ~PF_BIT(e);
    return m;
}

/* seq.c keyboard_block: a layer key down / up (e: PF_*, PF_N = no effect) */
static void perf_press(uint32_t e, int down)
{
    if (e >= PF_N)
        return;
    if (down) {
        perf_held |= PF_BIT(e);
        perf_ord[e] = ++perf_seq;
    } else {
        perf_held &= ~PF_BIT(e);
    }
}

static void perf_start(void) { pf.ph = 0; pf.sync = 1; }      /* seq_start: a 1/16 starts with the transport */

/* the SLICER's recordings dropped (when the buffer is taken and given back) */
static void perf_drop_slicer(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        sl[k].rec = sl[k].loop = sl[k].rec_on = 0;
}

/* the buffer effect to play: the last pressed of those running */
static uint32_t perf_pick(uint32_t a)
{
    uint32_t e, best = PF_N;
    a &= PF_BUF;
    for (e = 0; e < PF_M1; e++)
        if (((a >> e) & 1u) && (best == PF_N || perf_ord[e] - perf_ord[best] < 0x80000000u))
            best = e;
    return best;
}

static void perf_buf_start(uint32_t e)
{
    if (!sl_lent) {
        sl_lent = 1;
        perf_drop_slicer();
    }
    pf.src = (uint8_t)e;
    pf.next = PF_N;
    pf.wr = pf.rp = 0;
    pf.odd = 0;
    pf.xn = 0;
    if (e == PF_TAPE) {
        uint32_t t = beat_samples();
        pf.mode = BM_TAPE;
        if (t > PB_TAPE)
            t = PB_TAPE;
        pf.tv = 65536;
        pf.dv = 65536u / t + 1u;
        pf.tpos = 0;
    } else if (e == PF_OUP || e == PF_ODN) {    /* (wr: the frame written next, len: frames written) */
        pf.mode = BM_HARM;
        pf.len = 0;
        pf.hph = 0;
        pf.hinc = e == PF_OUP ? 0u - (1u << 21) : 1u << 20;   /* the delay: -1 / +0.5 sample a sample */
        pf.hfb = 0;
    } else if (e == PF_FRZ) {
        pf.mode = BM_FRZ;
        pf.gp[0] = 0;
        pf.gp[1] = PB_GRAIN / 2u;
        pf.gs[0] = pf.gs[1] = 0;
    } else {
        pf.mode = BM_LOOP;
        pf.state = 0;
        pf.len = perf_len(e);
        pf.rev = e == PF_REV;
    }
}

/* the buffer effect to play becomes e (at a 1/16 for REPEAT / REVERSE): a shorter REPEAT of what is
 * recorded switches with a cross-fade, anything else fades the old one out first */
static void perf_buf_select(uint32_t e)
{
    if (e == pf.src && pf.next == PF_N)
        return;
    if (e != PF_N && pf.mode == BM_LOOP && pf.state && e <= PF_REV && pf.src != PF_N &&
        perf_len(e) <= 2u * pf.wr) {
        pf.xrp = pf.rp;
        pf.xlen = pf.len;
        pf.xrev = pf.rev;
        pf.xn = SL_RAMP;
        pf.src = (uint8_t)e;
        pf.next = PF_N;
        pf.len = perf_len(e);
        pf.rev = e == PF_REV;
        pf.rp = 0;
        return;
    }
    if (pf.mode == BM_NONE || !pf.w) {
        if (e == PF_N) {
            pf.src = PF_N;
            pf.next = PF_N;
        } else {
            perf_buf_start(e);
        }
        return;
    }
    pf.src = PF_N;                                  /* fade out, then the next one (perf_seg) */
    pf.next = (uint8_t)e;
}

/* each block, after the keys (seq.c) and before the parts: what runs, the 1/16, the filters, the mutes.
 * Returns 1 when a stage has something to do */
static __attribute__((noinline)) int perf_begin(uint32_t n)
{
    uint32_t held = perf_kill ? 0u : (perf_held | (perf_solo ? (~(uint32_t)perf_solo & 15u) << PF_M1 : 0u)) & perf_avail();
    uint32_t q, k, ph0, bnd;
    int32_t m;
    if (!held && !pf.busy && !(perf_k[0] | perf_k[1] | perf_k[2])) {   /* idle: the clock only */
        pf.ph += n;
        pf.sync = 0;
        perf_act = 0;
        return 0;
    }
    pf.P = beat_samples() >> 2;
    if (pf.sync) {
        pf.ph = 0;
        pf.sync = 0;
    }
    ph0 = pf.ph % pf.P;                             /* the 1/16 in this block (n: none) */
    bnd = ph0 ? (pf.P - ph0 < n ? pf.P - ph0 : n) : 0u;
    pf.ph += n;
    pf.act &= held;                                 /* let go: at once */
    pf.act |= held & ~PF_Q;
    pf.act0 = pf.act;
    q = held & PF_Q & ~pf.act;
    pf.split = n;
    if (q && (!song.playing || bnd < n)) {
        pf.split = song.playing ? bnd : 0u;
        pf.act |= q;
    }
    perf_act = pf.act;
    /* the filters: LPF from open down over a bar while its key is held, KNOB 1 left; HPF up, KNOB 1 right */
    m = perf_k[0];
    {
        uint32_t bar = 16u * pf.P, t;
        int32_t lt = PF_TOP, ht = 0;
        pf.lt = (pf.act & PF_BIT(PF_LPF)) ? pf.lt + n : 0u;
        pf.ht = (pf.act & PF_BIT(PF_HPF)) ? pf.ht + n : 0u;
        if (pf.lt) {
            t = pf.lt < bar ? pf.lt : bar;
            lt = PF_TOP - (int32_t)((uint32_t)(PF_TOP - PF_LOW) / 64u * (t * 64u / bar));
        }
        if (m < 0 && PF_TOP + m * (PF_TOP - PF_LOW) / 100 < lt)
            lt = PF_TOP + m * (PF_TOP - PF_LOW) / 100;
        if (pf.ht) {
            t = pf.ht < bar ? pf.ht : bar;
            ht = (int32_t)((uint32_t)(PF_TOP - PF_LOW) / 64u * (t * 64u / bar));
        }
        if (m > 0 && m * (PF_TOP - PF_LOW) / 100 > ht)
            ht = m * (PF_TOP - PF_LOW) / 100;
        pf.lc += clamp(lt - pf.lc, -192, 192);      /* ~45 ms over the range */
        pf.hc += clamp(ht - pf.hc, -192, 192);
        for (k = 0; k < 3u; k++) {                  /* the coefficients, between two table rows */
            int32_t i = pf.lc >> 8, f = pf.lc & 255, j = pf.hc >> 8, g = pf.hc & 255;
            pf.lk[k] = (int16_t)(PF_SVF[i][k] + (((PF_SVF[i < 63 ? i + 1 : 63][k] - PF_SVF[i][k]) * f) >> 8));
            pf.hk[k] = (int16_t)(PF_SVF[j][k] + (((PF_SVF[j < 63 ? j + 1 : 63][k] - PF_SVF[j][k]) * g) >> 8));
        }
    }
    pf.mute = 0;
    for (k = 0; k < NTRK; k++)
        if (((held >> (PF_M1 + k)) & 1u) || pf.mg[k] != 32768)
            pf.mute |= (uint8_t)(1u << k);
    pf.busy = 1;
    return 1;
}

/* a track muted by its black key: its signal ramps to 0 and back (fx.c mix_part, after the SLICER) */
static __attribute__((noinline)) void perf_mute(uint32_t k, int32_t *b, uint32_t n)
{
    uint32_t i;
    int32_t g = pf.mg[k], t = (pf.act >> (PF_M1 + k)) & 1u ? 0 : 32768;
    for (i = 0; i < n; i++) {
        g += clamp(t - g, -SL_SLOPE, SL_SLOPE);
        b[i] = mulq16(b[i], (uint32_t)g << 1);
    }
    pf.mg[k] = g;
}

/* before the buses: THROW (KNOB 3) adds the dry mix to the delay and reverb sends */
static __attribute__((noinline)) void perf_pre(const int32_t *ml, const int32_t *mr, int32_t *sd, int32_t *sr,
                                               uint32_t n)
{
    uint32_t i;
    int32_t m = perf_k[2] * 327;
    for (i = 0; i < n; i++) {
        pf.td += clamp(m - pf.td, -SL_SLOPE, SL_SLOPE);
        if (pf.td) {
            int32_t x = mulq16((ml[i] + mr[i]) >> 1, (uint32_t)pf.td << 1);
            sd[i] += x;
            sr[i] += x;
        }
    }
}

/* x towards y by w (Q15, 32768 = y) */
static inline int32_t pf_mix(int32_t x, int32_t y, int32_t w)
{
    return w == 32768 ? y : x + mulq16(y - x, (uint32_t)w << 1);
}

/* the loop: frame f of it, both channels (Q15) */
static inline void pb_put(uint32_t f, int32_t l, int32_t r)
{
    int16_t *p = &sl_buf[0][0] + 2u * (f & (PB_FRAMES - 1u));
    p[0] = (int16_t)clamp(l, -32768, 32767);
    p[1] = (int16_t)clamp(r, -32768, 32767);
}
/* sample s of the frames recorded from 0 (n of them): between two frames on an odd one */
static inline void pb_at(uint32_t s, uint32_t n, int32_t *l, int32_t *r)
{
    uint32_t f = s >> 1 < n ? s >> 1 : n - 1u;    /* (an odd loop's last sample: the frame before) */
    const int16_t *p = &sl_buf[0][0] + 2u * (f & (PB_FRAMES - 1u));
    const int16_t *q;                               /* a frame is the mean of samples 2f, 2f + 1: sample s */
    if (s & 1u)                                     /* lies a quarter frame from it, towards f + 1 or f - 1 */
        q = f + 1u < n ? p + 2 : p;
    else
        q = f ? p - 2 : p;
    *l = (3 * p[0] + q[0]) >> 1;
    *r = (3 * p[1] + q[1]) >> 1;
}
/* REPEAT / REVERSE at sample rp of a loop of len, windowed at both ends */
static inline void pb_loop(uint32_t rp, uint32_t len, int rev, uint32_t n, int32_t *l, int32_t *r)
{
    uint32_t e = len - 1u - rp;
    pb_at(rev ? e : rp, n, l, r);
    if (rp < e)
        e = rp;
    if (e < (uint32_t)SL_RAMP) {
        *l = (*l * (int32_t)e) >> SL_RAMP_LOG2;
        *r = (*r * (int32_t)e) >> SL_RAMP_LOG2;
    }
}

/* record the pair into frame f (the mean, >> 1: headroom) */
static inline uint32_t pb_rec(uint32_t f, int32_t l, int32_t r)
{
    if (!pf.odd) {
        pf.hl = l;
        pf.hr = r;
        pf.odd = 1;
        return f;
    }
    pf.odd = 0;
    pb_put(f, (pf.hl + l) >> 2, (pf.hr + r) >> 2);
    return f + 1u;
}

/* FREEZE: grain g's sample, by its triangle window, added to yl / yr; a new start at each pass */
static inline void perf_grain(uint32_t g, int32_t *yl, int32_t *yr)
{
    uint32_t p = pf.gp[g], tri = p < PB_GRAIN / 2u ? p : PB_GRAIN - p;
    int32_t a, b;
    if (!p)
        pf.gs[g] = ((rng() >> 16) * (pf.wr - PB_GRAIN / 2u + 1u)) >> 16;
    pb_at(2u * pf.gs[g] + p, pf.wr, &a, &b);
    *yl += a * (int32_t)tri;
    *yr += b * (int32_t)tri;
    pf.gp[g] = p + 1u < PB_GRAIN ? p + 1u : 0u;
}

/* OCT UP / DN: the tap at sweep place ph, both channels (as stored: half the level); silence before the
 * delay has that much in it */
static inline void hb_tap(uint32_t ph, int32_t *l, int32_t *r)
{
    uint32_t d = (HB_BASE << 16) + (ph >> 5), di = d >> 16;   /* the delay, Q16: 128 .. 2176 samples */
    int32_t f = (int32_t)((d >> 1) & 0x7FFFu);
    const int16_t *p, *q;
    if (di + 1u > pf.len) {
        *l = *r = 0;
        return;
    }
    p = &sl_buf[0][0] + 2u * ((pf.wr - di) & HB_MASK);
    q = &sl_buf[0][0] + 2u * ((pf.wr - di - 1u) & HB_MASK);
    *l = p[0] + (((q[0] - p[0]) * f) >> 15);
    *r = p[1] + (((q[1] - p[1]) * f) >> 15);
}
/* OCT UP / DN on one sample: yl / yr the harmonized mix (see the top). noinline: perf_block's loop keeps its
 * code layout (target_budget.py: a block placed out of line looks like a loop round the per-sample code) */
static __attribute__((noinline)) void perf_harm(int32_t l, int32_t r, int32_t *yl, int32_t *yr)
{
    int32_t wa = (32768 - osc_sine(pf.hph + 0x40000000u)) >> 1, al, ar, bl, br, sl_, sr_;   /* A's share, Q15 */
    hb_tap(pf.hph, &al, &ar);
    hb_tap(pf.hph + 0x80000000u, &bl, &br);
    sl_ = (al * wa + bl * (32768 - wa)) >> 15;
    sr_ = (ar * wa + br * (32768 - wa)) >> 15;
    l >>= 1;                                        /* (stored at half: headroom, as the loop's frames) */
    r >>= 1;
    {
        int32_t xl = l, xr = r;
        if (pf.hfb) {                               /* the shimmer: back into the delay, soft-clipped */
            xl += softclip(mulq15(sl_, pf.hfb));
            xr += softclip(mulq15(sr_, pf.hfb));
        }
        if (pf.len < (uint32_t)SL_RAMP) {           /* the recording fades in (2.9 ms): no step where it */
            xl = (xl * (int32_t)pf.len) >> SL_RAMP_LOG2;   /* begins, for a tap to read across */
            xr = (xr * (int32_t)pf.len) >> SL_RAMP_LOG2;
        }
        pb_put(pf.wr & HB_MASK, xl, xr);
    }
    pf.wr++;
    if (pf.len <= HB_MASK)
        pf.len++;
    pf.hph += pf.hinc;
    *yl = l + (l >> 3) + ((sl_ * 45875) >> 15);     /* 0.56 live + 0.7 shifted (x2: stored at half) */
    *yr = r + (r >> 3) + ((sr_ * 45875) >> 15);
}

/* the buffer effect faded out: the next one, or the buffer back. Once per effect, out of the per-sample loop
 * (noinline: its calls stay out of perf_block's loop code) */
static __attribute__((noinline)) void perf_buf_done(void)
{
    if (pf.next != PF_N) {
        perf_buf_start(pf.next);
    } else {
        pf.mode = BM_NONE;
        if (sl_lent) {
            sl_lent = 0;
            perf_drop_slicer();
        }
    }
}

/* the buffer effect on one sample: the share pf.w of what it plays, the rest live */
static inline void perf_buf(int32_t *pl, int32_t *pr)
{
    int32_t l = *pl, r = *pr, yl = 0, yr = 0, on = 0;
    if (pf.mode == BM_LOOP) {
        if (!pf.state) {
            pf.wr = pb_rec(pf.wr, l, r);
            if (++pf.rp >= pf.len) {                /* (rp: samples recorded) */
                pf.state = 1;
                pf.rp = 0;
            }
        } else {
            pb_loop(pf.rp, pf.len, pf.rev, pf.wr, &yl, &yr);
            if (pf.xn) {                            /* the loop it switched from, fading out */
                int32_t ol, orr, g = (int32_t)pf.xn;
                pb_loop(pf.xrp, pf.xlen, pf.xrev, pf.wr, &ol, &orr);
                yl += (ol * g) >> SL_RAMP_LOG2;
                yr += (orr * g) >> SL_RAMP_LOG2;
                pf.xn--;
                if (++pf.xrp >= pf.xlen)
                    pf.xrp = 0;
            }
            if (++pf.rp >= pf.len)
                pf.rp = 0;
            on = 1;
        }
    } else if (pf.mode == BM_TAPE) {
        pf.wr = pb_rec(pf.wr, l, r);
        if (pf.wr >= 2u) {                          /* read behind the writing, slower and slower */
            uint32_t f = pf.tpos >> 16;
            int32_t fr = (int32_t)((pf.tpos >> 1) & 0x7FFFu), a, b, env;
            const int16_t *p = &sl_buf[0][0] + 2u * (f & (PB_FRAMES - 1u));
            const int16_t *s = &sl_buf[0][0] + 2u * ((f + 1u) & (PB_FRAMES - 1u));
            a = p[0];
            b = s[0];
            yl = (a + (((b - a) * fr) >> 15)) << 1;
            a = p[1];
            b = s[1];
            yr = (a + (((b - a) * fr) >> 15)) << 1;
            env = pf.tv >= 8192u ? 32768 : (int32_t)(pf.tv << 2);   /* the last eighth fades to silence */
            yl = mulq16(yl, (uint32_t)env << 1);
            yr = mulq16(yr, (uint32_t)env << 1);
            pf.tpos += pf.tv >> 1;
            pf.tv = pf.tv > pf.dv ? pf.tv - pf.dv : 0u;
            on = 1;
        }
    } else if (pf.mode == BM_FRZ) {
        if (pf.wr < PB_FREC)
            pf.wr = pb_rec(pf.wr, l, r);
        if (pf.wr >= PB_GRAIN / 2u) {               /* two grains of what it caught, triangle windows */
            perf_grain(0, &yl, &yr);
            perf_grain(1, &yl, &yr);
            yl >>= 10;                              /* (the windows sum to PB_GRAIN / 2) */
            yr >>= 10;
            on = 1;
        }
    } else if (pf.mode == BM_HARM) {
        perf_harm(l, r, &yl, &yr);
        on = 2;                                     /* (KNOB 4 is the shimmer: the level stays full) */
    }
    pf.tw = on && pf.src != PF_N ? on == 2 ? 32768 : 32768 - perf_k[3] * 327 : 0;
    pf.w += clamp(pf.tw - pf.w, -SL_SLOPE, SL_SLOPE);
    if (pf.w) {
        *pl = pf_mix(l, yl, pf.w);
        *pr = pf_mix(r, yr, pf.w);
    } else if (pf.src == PF_N) {                    /* faded out: the next one, or the buffer back */
        perf_buf_done();
    }
}

/* one channel of a state-variable filter (k = 1): x in Q15 >> 2 */
static inline int32_t pf_svf(int32_t x, int32_t *z, const int16_t *c, int hp)
{
    int32_t v3 = x - z[1], v1 = (c[0] * z[0] + c[1] * v3) >> 14, v2 = z[1] + ((c[1] * z[0] + c[2] * v3) >> 14);
    z[0] = 2 * v1 - z[0];
    z[1] = 2 * v2 - z[1];
    return hp ? x - v1 - v2 : v2;
}

static __attribute__((noinline)) void perf_switch(uint32_t a) { perf_buf_select(perf_pick(a)); }

/* the UI: OCT UP / DN is the buffer effect playing (KNOB 4 is its shimmer, the card says so) */
static int perf_harm_on(void) { return (PF_HARM >> perf_pick(perf_act)) & 1u; }

/* after the master level: the buffer effect, LPF, HPF, CRUSH on l / r (stereo, Q15) */
static __attribute__((noinline)) void perf_block(int32_t *bl, int32_t *br, uint32_t n)
{
    uint32_t i, a = pf.act0, sh, hold;
    int32_t c = perf_k[1], lt, ht, ct;
    if (c)
        pf.cc = c;
    ct = c ? 32768 : 0;
    sh = 4u + (uint32_t)pf.cc * 7u / 100u;
    hold = sh - 3u;
    if (pf.split)
        perf_switch(a);
    pf.hfb += clamp(perf_k[3] * HB_FB - pf.hfb, -512, 512);   /* OCT UP / DN's shimmer glides (~40 ms) */
    for (i = 0; i < n; i++) {
        int32_t l = bl[i], r = br[i];
        if (i == pf.split) {
            a = pf.act;
            perf_switch(a);
        }
        if (pf.mode != BM_NONE)
            perf_buf(&l, &r);
        lt = (a & PF_BIT(PF_LPF)) || perf_k[0] < 0 || pf.lc < PF_TOP ? 32768 : 0;
        pf.la += clamp(lt - pf.la, -SL_SLOPE, SL_SLOPE);
        if (pf.la) {
            int32_t fl = pf_svf(clamp(l >> 2, -32767, 32767), &pf.lz[0], pf.lk, 0) << 2;
            int32_t fr = pf_svf(clamp(r >> 2, -32767, 32767), &pf.lz[2], pf.lk, 0) << 2;
            l = pf_mix(l, fl, pf.la);
            r = pf_mix(r, fr, pf.la);
        } else {
            pf.lz[0] = pf.lz[1] = pf.lz[2] = pf.lz[3] = 0;
        }
        ht = (a & PF_BIT(PF_HPF)) || perf_k[0] > 0 || pf.hc > 0 ? 32768 : 0;
        pf.ha += clamp(ht - pf.ha, -SL_SLOPE, SL_SLOPE);
        if (pf.ha) {
            int32_t fl = pf_svf(clamp(l >> 2, -32767, 32767), &pf.hz[0], pf.hk, 1) << 2;
            int32_t fr = pf_svf(clamp(r >> 2, -32767, 32767), &pf.hz[2], pf.hk, 1) << 2;
            l = pf_mix(l, fl, pf.ha);
            r = pf_mix(r, fr, pf.ha);
        } else {
            pf.hz[0] = pf.hz[1] = pf.hz[2] = pf.hz[3] = 0;
        }
        {   /* CRUSH: fewer bits, a held sample (KNOB 2) */
            pf.cw += clamp(ct - pf.cw, -SL_SLOPE, SL_SLOPE);
            if (pf.cw) {
                if (++pf.cn >= hold) {
                    pf.cn = 0;
                    pf.chl = ((l >> sh) << sh) + (1 << (sh - 1u));
                    pf.chr = ((r >> sh) << sh) + (1 << (sh - 1u));
                }
                l = pf_mix(l, pf.chl, pf.cw);
                r = pf_mix(r, pf.chr, pf.cw);
            }
        }
        bl[i] = l;
        br[i] = r;
    }
    pf.busy = pf.act || pf.mode != BM_NONE || pf.w || pf.la || pf.ha || pf.cw || pf.td || pf.mute || perf_k[0] ||
              perf_k[1] || perf_k[2];
}
