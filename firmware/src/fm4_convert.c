/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL (engine 1, four-operator FM): retired, and its sounds converted to FM6.
 *
 * "The DIGITAL engine has been replaced by FM6 (Dexed-based)" (1.0). Its DSP stays in the tree (eng_digital.c)
 * and builds with FELUCCA_FM4=1 (core.h; off by default, as FELUCCA_SLICE), so it can come back. Engine index 1
 * stays reserved: the stores name engines by index and the table is append-only. Without FELUCCA_FM4 engine 1 is
 * ENG_FM4_GONE below: no presets, never offered (PRESETS, the EDIT layer, the editor's lists skip it: engines.c
 * eng_ok), and a DIGITAL sound that arrives anyway -- a project (FUN1..FUN8), a user preset, the editor's PRESET /
 * SET G_ENGSEL / UP_PUT, a full backup, a stored DIGITAL preset number (TRK_DEF of old projects, favourites) --
 * becomes an FM6 sound with a patch of its own, made by fm4_convert from its DIGITAL values:
 *
 *   DIGITAL                        FM6 patch (155-byte voice; operator numbers 1..6 as the six-operator charts)
 *   ALG 1  4>3>2>1                 ALG 1   6>5>4>3        op 4 -> 6, 3 -> 5, 2 -> 4, 1 -> 3   (2>1 silent)
 *   ALG 2  (3+4)>2>1               ALG 14  (6+5)>4>3      op 4 -> 6, 3 -> 5, 2 -> 4, 1 -> 3   (2>1 silent)
 *   ALG 3  3>2, (2+4)>1            ALG 8   6>5, (5+4)>3   op 4 -> 4 (FB), 3 -> 6, 2 -> 5, 1 -> 3
 *   ALG 4  4>3, (2+3)>1            ALG 7   6>5, (5+4)>3   op 4 -> 6, 3 -> 5, 2 -> 4, 1 -> 3
 *   ALG 5  2>1, 4>3, (1+3)         ALG 5   2>1, 6>5 (4>3) op 4 -> 6, 3 -> 5, 2 -> 2, 1 -> 1   (4>3 silent)
 *   ALG 6  4>(1, 2, 3), (1+2+3)    ALG 22  6>(3, 4, 5)    op 4 -> 6, 3 -> 5, 2 -> 4, 1 -> 3   (2>1 silent)
 *   ALG 7  4>3, (1+2+3)            ALG 29  1, 2, 6>5      op 4 -> 6, 3 -> 5, 2 -> 2, 1 -> 1   (4>3 silent)
 *   ALG 8  (1+2+3+4)               ALG 32  1, 2, 3, 6     op 4 -> 6, 3 -> 3, 2 -> 2, 1 -> 1   (4, 5 silent)
 *   (DIGITAL's op 4 always lands on the operator that has the feedback; the two left over: level 0, rates 99)
 *   R2 R3 R4 (.5 1 2 .. 12 14 16)  coarse of the ops of 2, 3, 4 (0 = .5, n = n), op 1 coarse 1; fine 0, detune 0
 *   INDEX x the op's LEVEL         a modulator's output level: DIGITAL's deviation is 2 cycles x INDEX / 127 (/ 2
 *                                  where two are summed into one input), FM6's 2 cycles at level 99, -0.75 dB a step
 *   MODDEC                         a modulator's EG: L1 99, R2 so that it falls to INDEX / 4 (-12 dB, where
 *                                  MODDEC's exponential lands) over the MODDEC time, L3 = L2; L4 = L3 with
 *                                  R4 = R2 (the index keeps falling after the key, as DIGITAL's does)
 *   ENV -> FLT (P_ED_FLT)          added to the index at the envelope's peak and x SUS at the sustain (DIGITAL
 *                                  adds the master envelope x FLT to the index), the attack from ATK
 *   FDBK                           FB 0..7 of the operator op 4 became: 8 + log2 of DIGITAL's feedback deviation
 *                                  (FDBK x 0.0158 cycles of op 4's sine, whatever its level) over that operator's
 *                                  output at the sustain, rounded. From FDBK 20 (FM4_FB_NOISE: DIGITAL's op 4 turns
 *                                  to noise) the operator is held up by at most 18 dB so that FM6's gets there too
 *   op ATK DEC SUS REL LEVEL       (P_FM1_ATK .. P_FM4_LEVEL, the OP ENV pages) the op's EG on top of the above:
 *                                  attack the longer one, decay towards the product of the sustains over the
 *                                  longer decay, release in parallel (1 / (1 / a + 1 / b)), REL 0 a cut; LEVEL
 *                                  the output level. The flat default (0 0 127 0) changes nothing (as DIGITAL)
 *   master ENV (ATK DEC SUS REL)   kept as the track's ENV (FM6's own envelopes are the voice's amplitude: the
 *                                  track ADSR does nothing there), and folded into the carriers' EGs: R1 from ATK,
 *                                  R2 / L2 from DEC / SUS, R4 from REL, L4 0
 *   times                          DIGITAL's 1 ms .. 10 s (TIME_MS_X10): attack linear, decay and release -40 dB at
 *                                  the time; the FM6 rate the nearest in samples per 6 dB (msfa's step per block,
 *                                  rate scaling 0; its attack: 1.46 x that)
 *   carriers' level                DIGITAL's output / 1 .. / 4 (summed carriers), LEVEL; FM6 renders one carrier
 *                                  at level 99 at half DIGITAL's full scale (eng_fm6.c), so a lone carrier stays
 *                                  6 dB lower (level 99 is the top); 3 steps lower for the velocity below
 *   velocity                       carriers' sensitivity 3 (DIGITAL's output is linear in velocity: -2.4 / -6 / -12
 *                                  dB at 96 / 64 / 32; sensitivity 3: -2.6 / -6.8 / -12.4 from 127), modulators' 1
 *                                  (DIGITAL adds (velocity - 96) / 4 to the index)
 *   the rest                       pitch EG flat (50), LFO off (depths 0), transpose 0 (24), osc key sync, name
 *                                  the DIGITAL preset's when the values are one, else "4OP ALG n"
 *   EDIT values after              FM6's macros neutral (ALG PAT, FB MLVL MRAT MEG VMOD DTUN 0), PTCH the factory
 *                                  patch of the nearest DIGITAL preset (E.PIANO -> TINE EP, BELL, BASS -> FM BASS,
 *                                  BRASS, ORGAN, PAD, MARIMBA, FUNK KEY -> PLUCK): what reloads when the sound is
 *                                  saved as a user preset again (a user preset keeps PTCH, not the patch); the
 *                                  OP ENV values their defaults (inert since; ids 61..80 stay, no id moves)
 * Not carried: the accent's index boost (velocity > 110), LFO / matrix amounts keep their values (FM6 reads FLT
 * as MLVL, SHP as the feedback). fm4_test.c renders DIGITAL (FELUCCA_FM4=1) against the conversion. */

/* ------------------------------------------------- DIGITAL's tables --- */
static const char *const N_FMALG[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
static const char *const N_RATIO[] = {".5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "14", "16"};

/* the factory presets (the stores' preset numbers of engine 1 index this table) */
static const preset_t DIGITAL_PRESETS[] = {
    /* ALG R2 R3 R4 INDEX MODDEC FDBK - */
    {"E.PIANO", {4, 1, 1, 14, 70, 55, 0, 0}, {0, 80, 40, 60}, 0, 0, FX(0, 50, 25, 35), PAT(6)},
    {"BELL", {4, 8, 1, 4, 90, 70, 0, 0}, {0, 95, 0, 85}, 0, 0, FX(0, 0, 30, 70), PAT(7)},
    {"BASS", {0, 1, 1, 1, 48, 40, 8, 0}, {0, 60, 70, 25}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"BRASS", {0, 1, 1, 1, 70, 40, 20, 0}, {40, 70, 100, 40}, 20, 0, FX(0, 20, 20, 40), PAT(6)},
    {"ORGAN", {7, 2, 3, 4, 0, 0, 0, 0}, {2, 60, 127, 20}, 0, 0, FX(10, 40, 0, 30), PAT(6)},
    {"PAD", {5, 2, 1, 3, 40, 90, 10, 0}, {80, 90, 110, 95}, 0, 0, FX(0, 60, 30, 70), PAT(5)},
    {"MARIMBA", {4, 4, 1, 1, 60, 30, 0, 0}, {0, 80, 0, 60}, 0, 0, FX(0, 0, 25, 40), PAT(3)},
    {"FUNK KEY", {3, 1, 3, 5, 90, 25, 20, 0}, {0, 45, 30, 30}, 0, 0, FX(0, 20, 30, 20), PAT(6)},
};
#define FM4_NPRESETS 8u
_Static_assert(NELEM(DIGITAL_PRESETS) == FM4_NPRESETS, "DIGITAL presets");
/* DIGITAL preset k -> the FM6 factory preset (= its PTCH) that covers that sound */
static const uint8_t FM4_TO_FM6[FM4_NPRESETS] = {0, 1, 2, 3, 6, 4, 5, 7};

/* EDIT: the ranges are the stores' (a value of engine 1 is clamped to them before it converts) */
#define FM4_EDIT(l0, l1, l2, l3, l4, l5, l6, f0, f1, n0, n1) {                                              \
        {l0, f0, 0, 7, 0, n0, 0}, {l1, f0, 0, 14, 1, n1, 0}, {l2, f0, 0, 14, 1, n1, 0}, {l3, f0, 0, 14, 1, n1, 0}, \
        {l4, F_PCT, 0, 127, 60, 0, 0}, {l5, f1, 0, 127, 60, 0, 0}, {l6, F_PCT, 0, 127, 0, 0, 0},               \
        {"-", F_INT, 0, 0, 0, 0, 0}}

#if !FELUCCA_FM4
/* engine 1 without the DSP: never selectable, no presets, silent (a track never keeps it: fm4_convert) */
static void fm4_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void fm4_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_FM4_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = FM4_EDIT("-", "-", "-", "-", "-", "-", "-", F_INT, F_INT, 0, 0),
    .note_on = fm4_gone_note_on,
    .render = fm4_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#endif

/* ------------------------------------------------------- conversion --- */
#define FM4_MUTE (-100000)       /* the microsteps of silence */
#ifndef FM4_FB_NOISE
#define FM4_FB_NOISE 20          /* FDBK from here: DIGITAL's op 4 turns to noise (its feedback near 0.33 cycles) */
#endif
#ifndef FM4_FB_LIFT
#define FM4_FB_LIFT 768          /* .. FM6's operator held up by at most this (microsteps: 18 dB) to get there */
#endif
/* per DIGITAL algorithm: the FM6 algorithm (0-based), the FM6 operator (1..6) of DIGITAL's op 1..4, and its share:
 * 1 alone, 2..4 summed with others into one input or the output (DIGITAL averages them) */
static const struct { uint8_t alg, op[4], share[4]; } FM4_ALG[8] = {
    {0, {3, 4, 5, 6}, {1, 1, 1, 1}},
    {13, {3, 4, 5, 6}, {1, 1, 2, 2}},
    {7, {3, 5, 6, 4}, {1, 2, 1, 2}},
    {6, {3, 4, 5, 6}, {1, 2, 2, 1}},
    {4, {1, 2, 5, 6}, {2, 1, 2, 1}},
    {21, {3, 4, 5, 6}, {3, 3, 3, 1}},
    {28, {1, 2, 5, 6}, {3, 3, 3, 1}},
    {31, {1, 2, 3, 6}, {4, 4, 4, 4}},
};

static int32_t fm4_abs(int32_t x) { return x < 0 ? -x : x; }

/* 256 x log2(x), x > 0 (the mantissa: f + 0.346 f (1 - f), within 0.01 octave) */
static int32_t fm4_lg(uint32_t x)
{
    int32_t n = 0;
    uint32_t f;
    if (!x)
        return FM4_MUTE;
    while (n < 31 && x >> (n + 1))
        n++;
    f = (n >= 8 ? x >> (n - 8) : x << (8 - n)) & 255u;
    return n * 256 + (int32_t)(f + ((f * (256u - f) * 89u) >> 16));
}
/* microsteps (256 = 6 dB) of an amplitude in Q15 (32768 = 1) */
static int32_t fm4_ms(uint32_t q15) { return q15 ? fm4_lg(q15) - 15 * 256 : FM4_MUTE; }

/* DIGITAL time value v (0..127) in samples */
static int32_t fm4_samples(int32_t v) { return (int32_t)(TIME_MS_X10[v & 127] * 441u / 100u); }

/* the FM6 rate (0..99) of a stage that takes `per` samples per 6 dB (msfa: (4 + q % 4) << (7 + q / 4) a block,
 * q = rate x 41 / 64): the nearest in log */
static uint32_t fm4_rate(int32_t per)
{
    uint32_t r, best = 0;
    int32_t bd = 0x7FFFFFFF, lt = fm4_lg((uint32_t)(per < 1 ? 1 : per));
    for (r = 0; r < 100u; r++) {
        uint32_t q = (r * 41u) >> 6;
        int32_t d = fm4_abs(fm4_lg((1u << (22u - (q >> 2))) / (4u + (q & 3u))) - lt);
        if (d < bd)
            bd = d, best = r;
    }
    return best;
}
/* a decay over t samples that drops `drop` microsteps (FM4_MUTE: to silence, -40 dB at t) */
static uint32_t fm4_fall(int32_t t, int32_t drop)
{
    if (drop <= 0)
        return drop == FM4_MUTE ? fm4_rate(t * 100 / 664) : 99u;
    return fm4_rate(t * 111 / drop);                   /* (exponential: most of the way at t / 2.3) */
}
/* the EG level (0..99) `ms` microsteps below level 99 (msfa: (scaleout(L) >> 1) << 6) */
static uint32_t fm4_level(int32_t ms)
{
    uint32_t l, best = 0;
    int32_t bd = 0x7FFFFFFF;
    if (ms <= FM4_MUTE)
        return 0;
    for (l = 0; l < 100u; l++) {
        int32_t d = fm4_abs(((fm6_scaleout((int32_t)l) >> 1) << 6) - (4032 + ms));
        if (d < bd)
            bd = d, best = l;
    }
    return best;
}
/* the output level (0..99) `ms` microsteps below level 99 (msfa: scaleout(OL) << 5) */
static uint32_t fm4_outlevel(int32_t ms)
{
    uint32_t l, best = 0;
    int32_t bd = 0x7FFFFFFF;
    if (ms <= FM4_MUTE)
        return 0;
    for (l = 0; l < 100u; l++) {
        int32_t d = fm4_abs((fm6_scaleout((int32_t)l) << 5) - (4064 + ms));
        if (d < bd)
            bd = d, best = l;
    }
    return best;
}

/* the DIGITAL factory preset nearest to these EDIT values (ALG first) */
static uint32_t fm4_nearest(const int16_t *e)
{
    uint32_t k, i, best = 0;
    int32_t bd = 0x7FFFFFFF;
    for (k = 0; k < FM4_NPRESETS; k++) {
        int32_t d = ((e[0] & 7) != DIGITAL_PRESETS[k].e[0]) * 1000;
        for (i = 1; i < 7u; i++)
            d += fm4_abs(e[i] - DIGITAL_PRESETS[k].e[i]) * (i < 4u ? 8 : 1);
        if (d < bd)
            bd = d, best = k;
    }
    return best;
}

/* p: a track's values as DIGITAL has them (P_COUNT) -> v: its FM6 patch (FP_SIZE + 1 bytes); p's EDIT values become
 * FM6's (see the top) and the OP ENV values their defaults. -> the FM6 factory preset of PTCH */
static uint32_t fm4_convert(int16_t *p, uint8_t *v)
{
    uint32_t alg = (uint32_t)p[P_E0] & 7u, car, k, near = fm4_nearest(&p[P_E0]);
    int32_t idx = clamp(p[P_E4], 0, 127), fenv = clamp(p[P_ED_FLT], -64, 63), sus = clamp(p[P_SUS], 0, 127);
    int32_t ipk = clamp(idx + fenv, 0, 127), isus = clamp(idx / 4 + fenv * sus / 127, 0, 127), fb = FM4_MUTE;
    int32_t t_att = fm4_samples(p[P_ATK]), t_mod = fm4_samples(p[P_E5]);
    /* the loop's two divides, here, with divisors that are never 0 and no test around them (#61): the JieLi compiler
     * moves a divide the loop does not change ahead of the test that guards it (and folds a guard's `? x : 1` back
     * to x), and a divide by 0 trapped (firmware/hal/fm1_irq.h, up to 1.0.3). INDEX + FLT ENV == 0 (0.9's ORGAN: INDEX 0) then faulted in
     * persist_boot, before the LCD, on every start: UBOOT after three. Same values as the guarded forms
     * (tests/fm4_div0_test.c) */
    uint32_t isus_q = (uint32_t)isus * 32767u / (ipk > 0 ? (uint32_t)ipk : 1u);       /* read only when ipk > 0 */
    int32_t t_env = t_att * (fenv > 0 ? fenv : 0) / (idx + fenv > 0 ? idx + fenv : 1);   /* 0 unless fenv > 0 */
    const char *name = 0;
    memset(v, 0, FP_SIZE + 1u);
    for (k = 0; k < 6u; k++) {                         /* every operator silent, as a start */
        uint8_t *o = v + k * FP_OP;
        o[FP_R1] = o[FP_R1 + 1] = o[FP_R1 + 2] = o[FP_R1 + 3] = 99;
        o[FP_BP] = 39;
        o[FP_FC] = 1;
        o[FP_DET] = 7;
    }
    v[FP_ALG] = FM4_ALG[alg].alg;
    car = fm6_carriers(v[FP_ALG]);
    for (k = 0; k < 4u; k++) {
        const int16_t *e = &p[P_FM1_ATK + k * 5u];
        uint32_t slot = 6u - FM4_ALG[alg].op[k], share = FM4_ALG[alg].share[k], r;
        uint8_t *o = v + slot * FP_OP;
        int flat = !e[0] && !e[1] && e[2] == 127 && !e[3];
        uint32_t lvl = (uint32_t)clamp(e[4], 0, 127) * 258u;             /* Q15 */
        uint32_t osus = flat ? 32767u : (uint32_t)clamp(e[2], 0, 127) * 258u;
        int32_t t_odec = flat || e[2] == 127 ? 0 : e[1] ? fm4_samples(e[1]) : 1;
        int32_t t_orel = flat ? 0x7FFFFFFF : e[3] ? fm4_samples(e[3]) : 0;
        int32_t ta, td, tr, ms_s;
        r = k ? (uint32_t)p[P_E1 + k - 1u] % 15u : 1u;                  /* .5 1 2 .. 12 14 16 */
        o[FP_FC] = (uint8_t)(k ? r == 0u ? 0u : r <= 12u ? r : r == 13u ? 14u : 16u : 1u);
        if ((car >> slot) & 1u) {                      /* a carrier: the master envelope x the op's */
            uint32_t s = (uint32_t)sus * 258u;
            o[FP_OL] = (uint8_t)fm4_outlevel(fm4_ms(lvl / share) + 256 - 96);
            o[FP_KVS] = 3;
            ta = t_att;
            if (!flat && e[0] && fm4_samples(e[0]) > ta)
                ta = fm4_samples(e[0]);
            s = (s * osus) >> 15;
            td = sus < 127 ? fm4_samples(p[P_DEC]) : 0;
            td = td > t_odec ? td : t_odec;
            tr = fm4_samples(p[P_REL]);
            if (!flat)
                tr = t_orel ? ((tr >> 5) * (t_orel >> 5) / ((tr >> 5) + (t_orel >> 5) + 1)) << 5 : 0;
            ms_s = fm4_ms(s);
            o[FP_R1] = (uint8_t)fm4_rate(ta * 100 / 146);
            o[FP_L1] = 99;
            o[FP_L1 + 1] = o[FP_L1 + 2] = (uint8_t)fm4_level(ms_s);
            o[FP_R1 + 1] = (uint8_t)fm4_fall(td, ms_s == FM4_MUTE ? FM4_MUTE : -ms_s);
            o[FP_R1 + 2] = 99;
            o[FP_R1 + 3] = (uint8_t)fm4_rate(tr * 100 / 664);
            o[FP_L1 + 3] = 0;
            if (k == 3u && p[P_E6] && o[FP_OL]) {      /* op 4 a carrier (ALG 8): feedback on its output (sustain) */
                int32_t need = fm4_ms((uint32_t)p[P_E6] * 258u * lvl >> 15) + 256;
                int32_t out = (fm6_scaleout(o[FP_OL]) << 5) - 4064 + (ms_s == FM4_MUTE ? -2048 : ms_s);
                if (p[P_E6] >= FM4_FB_NOISE && ms_s != FM4_MUTE && out < need) {   /* noisy: held up (louder) */
                    int32_t lift = need - out;
                    out += lift > FM4_FB_LIFT ? FM4_FB_LIFT : lift;
                    o[FP_OL] = (uint8_t)fm4_outlevel(out - ms_s);
                    out = (fm6_scaleout(o[FP_OL]) << 5) - 4064 + ms_s;
                }
                fb = need - 256 - out;
            }
        } else {                                       /* a modulator: INDEX through MODDEC x the op's envelope */
            uint32_t a = (uint32_t)ipk * lvl / 127u / share;
            uint32_t m = ipk ? (isus_q * osus) >> 15 : 0u;
            int32_t ms_a = fm4_ms(a);
            ms_s = fm4_ms(m);
            if (k == 3u && p[P_E6] && a) {             /* op 4's feedback (FM6: its deviation is the op's output x
                                                        * 2^(FB - 8); DIGITAL's does not follow the index) */
                int32_t need = fm4_ms((uint32_t)p[P_E6] * 258u * lvl >> 15) + 256;   /* the output FB 7 wants */
                if (p[P_E6] >= FM4_FB_NOISE && ms_s != FM4_MUTE && ms_a + ms_s < need) {
                    int32_t lift = need - (ms_a + ms_s), t;      /* DIGITAL's noisy feedback: FM6 gets there only
                                                                  * with the op held up (more index: the price) */
                    t = ms_a + ms_s + (lift > FM4_FB_LIFT ? FM4_FB_LIFT : lift);
                    if (t <= ms_a) {
                        ms_s = t - ms_a;
                    } else {
                        ms_s = 0;
                        ms_a = t > 0 ? 0 : t;
                    }
                }
                fb = need - 256 - (ms_a + (ms_s == FM4_MUTE ? -2048 : ms_s));        /* at the sustain */
            }
            o[FP_OL] = (uint8_t)fm4_outlevel(ms_a);
            o[FP_KVS] = a ? 1 : 0;
            ta = t_env;
            if (!flat && e[0] && fm4_samples(e[0]) > ta)
                ta = fm4_samples(e[0]);
            td = ms_s < 0 ? (isus < ipk ? t_mod : 0) : 0;
            td = td > t_odec ? td : t_odec;
            o[FP_R1] = (uint8_t)fm4_rate(ta * 100 / 146);
            o[FP_L1] = 99;
            o[FP_L1 + 1] = o[FP_L1 + 2] = (uint8_t)fm4_level(ms_s);
            o[FP_R1 + 1] = (uint8_t)fm4_fall(td, ms_s == FM4_MUTE ? FM4_MUTE : -ms_s);
            o[FP_R1 + 2] = 99;
            if (flat) {                                /* the index keeps falling after the key */
                o[FP_R1 + 3] = o[FP_R1 + 1];
                o[FP_L1 + 3] = o[FP_L1 + 2];
            } else {
                o[FP_R1 + 3] = (uint8_t)(t_orel ? fm4_rate(t_orel * 100 / 664) : 99u);
                o[FP_L1 + 3] = 0;
            }
        }
    }
    fb += 8 * 256 + 128;                               /* FM6 feedback: deviation = output x 2^(FB - 8) */
    if (p[P_E6] && fb > 0)
        v[FP_FB] = (uint8_t)(fb / 256 > 7 ? 7 : fb / 256);
    for (k = 0; k < 4u; k++) {
        v[FP_PR1 + k] = 99;
        v[FP_PL1 + k] = 50;
    }
    v[FP_OKS] = 1;
    v[FP_LFS] = 35;
    v[FP_TRNSP] = 24;
    for (k = 0; k < FM4_NPRESETS && !name; k++) {      /* a DIGITAL preset as it is: its name */
        const preset_t *pr = &DIGITAL_PRESETS[k];
        uint32_t i, same = pr->env[0] == p[P_ATK] && pr->env[1] == p[P_DEC] && pr->env[2] == p[P_SUS] &&
                           pr->env[3] == p[P_REL];
        for (i = 0; i < 7u; i++)
            same &= pr->e[i] == p[P_E0 + i];
        if (same)
            name = pr->name;
    }
    for (k = 0; k < 10u; k++)
        v[FP_NAME + k] = ' ';
    if (name) {
        for (k = 0; k < 10u && name[k]; k++)
            v[FP_NAME + k] = (uint8_t)name[k];
    } else {
        memcpy(v + FP_NAME, "4OP ALG", 7);
        v[FP_NAME + 8] = (uint8_t)('1' + alg);
    }
    fm6_sanitize(v);
    for (k = 0; k < 7u; k++)                           /* FM6's macros: the patch as it is */
        p[P_E0 + k] = 0;
    p[P_E7] = FM4_TO_FM6[near];
    for (k = P_FM1_ATK; k <= P_FM4_LEVEL; k++)
        p[k] = (k - P_FM1_ATK) % 5u == 2u || (k - P_FM1_ATK) % 5u == 4u ? 127 : 0;
    return FM4_TO_FM6[near];
}

/* DIGITAL preset k's values into p as a preset load sets them (ui.c apply_preset_to: the EDIT values, ENV, ENV ->
 * FLT, VOICE, the sends; the OP ENV values their defaults); the track's own values (param_kept) are the caller's */
static void fm4_preset_values(int16_t *p, uint32_t k)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const preset_t *pr = &DIGITAL_PRESETS[k % FM4_NPRESETS];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        p[P_E0 + i] = pr->e[i];
    p[P_ATK] = pr->env[0];
    p[P_DEC] = pr->env[1];
    p[P_SUS] = pr->env[2];
    p[P_REL] = pr->env[3];
    p[P_ED_FLT] = pr->fenv;
    p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
    for (i = P_FM1_ATK; i <= P_FM4_LEVEL; i++)
        p[i] = (i - P_FM1_ATK) % 5u == 2u || (i - P_FM1_ATK) % 5u == 4u ? 127 : 0;
}
