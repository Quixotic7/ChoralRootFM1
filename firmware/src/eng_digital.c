/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL: four-operator FM. Retired (replaced by FM6): built with FELUCCA_FM4=1 only (engines.c); its tables,
 * presets and the conversion of its sounds to FM6 are in fm4_convert.c. */
/* Four sine operators, eight classic 4-operator algorithms (ALG = P_E0, EDIT 1 KNOB 1),
 * op 4 with feedback, one modulation INDEX shaped by a modulator envelope.
 * Phase modulation wraps naturally in the 32-bit phase. */
static const uint16_t RATIO_Q8[15] = {128, 256, 512, 768, 1024, 1280, 1536, 1792, 2048, 2304, 2560, 2816,
                                      3072, 3584, 4096};

static int32_t digital_env[NPART][NVOICE][4];
static uint8_t digital_stage[NPART][NVOICE][4];

static void digital_note_on(track_t *t, voice_t *v)
{
    uint32_t tr = (uint32_t)(t - trk), vi = (uint32_t)(v - t->v), op;
    for (op = 0; op < 4u; op++) {
        digital_env[tr][vi][op] = t->p[P_FM1_ATK + op * 5u] ? 0 : 1 << 24;
        digital_stage[tr][vi][op] = t->p[P_FM1_ATK + op * 5u] ? 1 : 2;
    }
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[7] = 0;                 /* op 4 phase */
    v->s[4] = 1 << 24;           /* modulator envelope, Q24 */
    v->s[5] = v->s[6] = 0;       /* feedback history */
}

/* operator increment = carrier * ratio (Q8) without 32-bit overflow; kept below Nyquist */
static inline uint32_t fm_ratio_inc(uint32_t inc, uint32_t r)
{
    uint32_t lim = 0x73000000u / r;                      /* (inc >> 8) * r must stay < 0x73000000 */
    return (inc >> 8) > lim ? 0x73000000u : (inc >> 8) * r;
}

static inline uint32_t digital_mod(int32_t x, int32_t idx) { return (uint32_t)(x * idx) * 2065u; }

static __attribute__((noinline)) void digital_render_legacy(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t alg = (uint32_t)p[P_E0] & 7u, i;
    uint32_t i1 = m->inc;
    uint32_t i2 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E1] % 15]), i3 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E2] % 15]);
    uint32_t i4 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E3] % 15]);
    int32_t me, idx, fb = p[P_E6], fb1, fb2;                 /* fb1, fb2: op 4 feedback history */
    uint32_t ph0, ph1, ph2, ph4;
    /* modulator envelope: decays (MODDEC) towards 25 %, per block */
    v->s[4] += mulq16((1 << 22) - v->s[4], ENV_EXP[p[P_E5] & 127]);
    me = v->s[4] >> 9;                                         /* Q15 */
    idx = (p[P_E4] * me) >> 15;                                 /* 0..127 */
    idx = clamp(idx + ((m->cutoff + m->shape - (64 << 8)) >> 8) + (v->vel - 96) / 4, 0, 127);
    ph0 = v->ph[0];                                             /* state in locals: out[] may alias v->s[] */
    ph1 = v->ph[1];
    ph2 = v->ph[2];
    ph4 = (uint32_t)v->s[7];
    fb1 = v->s[5];
    fb2 = v->s[6];
    for (i = 0; i < n; i++) {
        int32_t o1, o2, o3, o4, s;
        o4 = sine_i((ph4 + digital_mod((fb1 + fb2) >> 1, fb)));
        fb2 = fb1;
        fb1 = o4;
        switch (alg) {
        case 0:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 1:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod((o3 + o4) >> 1, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 2:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod((o2 + o4) >> 1, idx)));
            break;
        case 3:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            s = sine_i((ph0 + digital_mod((o2 + o3) >> 1, idx)));
            break;
        case 4:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i((ph0 + digital_mod(o2, idx)));
            s = (o1 + o3) >> 1;
            break;
        case 5:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o4, idx)));
            o1 = sine_i((ph0 + digital_mod(o4, idx)));
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        case 6:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        default:
            o3 = sine_i(ph2);
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = (o1 + o2 + o3 + o4) >> 2;
            break;
        }
        ph0 += i1;
        ph1 += i2;
        ph2 += i3;
        ph4 += i4;
        out[i] += voice_amp(s, m, i);   /* full-scale sines: 6 dB below the others */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[7] = (int32_t)ph4;
    v->s[5] = fb1;
    v->s[6] = fb2;
}

/* Independent operator envelopes use a shared 640-byte runtime table, never
 * retained state. Transparent defaults retain the original DSP path exactly. */
static int digital_custom(const track_t *t)
{
    uint32_t op;
    for (op = 0; op < 4u; op++) {
        const int16_t *p = &t->p[P_FM1_ATK + op * 5u];
        if (p[0] || p[1] || p[2] != 127 || p[3] || p[4] != 127) return 1;
    }
    return 0;
}

static void digital_op_tick(track_t *t, voice_t *v, int32_t gain[4])
{
    uint32_t tr = (uint32_t)(t - trk), vi = (uint32_t)(v - t->v), op;
    for (op = 0; op < 4u; op++) {
        const int16_t *p = &t->p[P_FM1_ATK + op * 5u];
        int32_t e = digital_env[tr][vi][op], sus = p[2] == 127 ? 1 << 24 : (int32_t)p[2] * (1 << 17);
        uint8_t st = digital_stage[tr][vi][op];
        if (!p[0] && !p[1] && p[2] == 127 && !p[3]) {
            /* Flat operator envelope: LEVEL edits retain the master release. */
            digital_env[tr][vi][op] = 1 << 24;
            gain[op] = (int32_t)p[4] * 258;                 /* 127 -> 32766: no divide */
            continue;
        }
        if (!v->gate) st = 3;
        if (st == 1) {
            e += (int32_t)ENV_LIN[p[0] & 127];
            if (e >= (1 << 24)) { e = 1 << 24; st = 2; }
        } else if (st == 2) {
            if (!p[1]) e = p[2] == 127 ? 1 << 24 : sus;
            else e += mulq16(sus - e, ENV_EXP[p[1] & 127]);
        } else if (st == 3) {
            if (!p[3]) e = 0;
            else e -= mulq16(e, ENV_EXP[p[3] & 127]);
        }
        e = clamp(e, 0, 1 << 24);
        digital_env[tr][vi][op] = e;
        digital_stage[tr][vi][op] = st;
        gain[op] = ((e >> 9) * ((int32_t)p[4] * 258)) >> 15;
    }
}

static __attribute__((noinline)) void digital_render_custom(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t gains[4];
    digital_op_tick(t, v, gains);
    uint32_t alg = (uint32_t)p[P_E0] & 7u, i;
    uint32_t i1 = m->inc;
    uint32_t i2 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E1] % 15]), i3 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E2] % 15]);
    uint32_t i4 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E3] % 15]);
    int32_t me, idx, fb = p[P_E6], fb1, fb2;                 /* fb1, fb2: op 4 feedback history */
    uint32_t ph0, ph1, ph2, ph4;
    /* modulator envelope: decays (MODDEC) towards 25 %, per block */
    v->s[4] += mulq16((1 << 22) - v->s[4], ENV_EXP[p[P_E5] & 127]);
    me = v->s[4] >> 9;                                         /* Q15 */
    idx = (p[P_E4] * me) >> 15;                                 /* 0..127 */
    idx = clamp(idx + ((m->cutoff + m->shape - (64 << 8)) >> 8) + (v->vel - 96) / 4, 0, 127);
    ph0 = v->ph[0];                                             /* state in locals: out[] may alias v->s[] */
    ph1 = v->ph[1];
    ph2 = v->ph[2];
    ph4 = (uint32_t)v->s[7];
    fb1 = v->s[5];
    fb2 = v->s[6];
    for (i = 0; i < n; i++) {
        int32_t o1, o2, o3, o4, s;
        o4 = mulq15(sine_i((ph4 + digital_mod((fb1 + fb2) >> 1, fb))), gains[3]);
        fb2 = fb1;
        fb1 = o4;
        switch (alg) {
        case 0:
            o3 = mulq15(sine_i((ph2 + digital_mod(o4, idx))), gains[2]);
            o2 = mulq15(sine_i((ph1 + digital_mod(o3, idx))), gains[1]);
            s = mulq15(sine_i((ph0 + digital_mod(o2, idx))), gains[0]);
            break;
        case 1:
            o3 = mulq15(sine_i(ph2), gains[2]);
            o2 = mulq15(sine_i((ph1 + digital_mod((o3 + o4) >> 1, idx))), gains[1]);
            s = mulq15(sine_i((ph0 + digital_mod(o2, idx))), gains[0]);
            break;
        case 2:
            o3 = mulq15(sine_i(ph2), gains[2]);
            o2 = mulq15(sine_i((ph1 + digital_mod(o3, idx))), gains[1]);
            s = mulq15(sine_i((ph0 + digital_mod((o2 + o4) >> 1, idx))), gains[0]);
            break;
        case 3:
            o3 = mulq15(sine_i((ph2 + digital_mod(o4, idx))), gains[2]);
            o2 = mulq15(sine_i(ph1), gains[1]);
            s = mulq15(sine_i((ph0 + digital_mod((o2 + o3) >> 1, idx))), gains[0]);
            break;
        case 4:
            o3 = mulq15(sine_i((ph2 + digital_mod(o4, idx))), gains[2]);
            o2 = mulq15(sine_i(ph1), gains[1]);
            o1 = mulq15(sine_i((ph0 + digital_mod(o2, idx))), gains[0]);
            s = (o1 + o3) >> 1;
            break;
        case 5:
            o3 = mulq15(sine_i((ph2 + digital_mod(o4, idx))), gains[2]);
            o2 = mulq15(sine_i((ph1 + digital_mod(o4, idx))), gains[1]);
            o1 = mulq15(sine_i((ph0 + digital_mod(o4, idx))), gains[0]);
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        case 6:
            o3 = mulq15(sine_i((ph2 + digital_mod(o4, idx))), gains[2]);
            o2 = mulq15(sine_i(ph1), gains[1]);
            o1 = mulq15(sine_i(ph0), gains[0]);
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        default:
            o3 = mulq15(sine_i(ph2), gains[2]);
            o2 = mulq15(sine_i(ph1), gains[1]);
            o1 = mulq15(sine_i(ph0), gains[0]);
            s = (o1 + o2 + o3 + o4) >> 2;
            break;
        }
        ph0 += i1;
        ph1 += i2;
        ph2 += i3;
        ph4 += i4;
        out[i] += voice_amp(s, m, i);   /* full-scale sines: 6 dB below the others */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[7] = (int32_t)ph4;
    v->s[5] = fb1;
    v->s[6] = fb2;
}

static void digital_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    if (digital_custom(t)) digital_render_custom(t, v, out, n, m);
    else digital_render_legacy(t, v, out, n, m);
}

static const engine_t ENG_DIGITAL = {
    .name = "DIGITAL",
    .page_title = {"OPS", "MOD"},
    .edit = FM4_EDIT("ALG", "R2", "R3", "R4", "IDX", "MDEC", "FB", F_ENUM, F_TIME, N_FMALG, N_RATIO),
    .presets = DIGITAL_PRESETS,
    .npresets = NELEM(DIGITAL_PRESETS),
    .note_on = digital_note_on,
    .render = digital_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
    .keep = 0xE0,                /* the feedback history and op 4's phase; the modulator envelope restarts */
};
