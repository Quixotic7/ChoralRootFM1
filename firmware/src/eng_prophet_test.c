/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* P5 (engine ENGI_PROPHET, FELUCCA_PROPHET; docs/PROPHET.md; the name a player sees is "P5", the owner's of 2026-10-13:
 * the internal names keep PROPHET and p5_): Melodee's Prophet-5 engine (Kerem Kilic's fork of
 * Felucca, <https://github.com/keremimo/melodee>, GPL-3.0-only; its code name p5_, its file name kept so that a later
 * fix there can be diffed in). Native Sequential programs (prophet_patch.c: the 133 raw bytes of a program dump), two
 * band-limited oscillators with hard sync and audio-rate Poly-Mod into a four-pole ladder with a tanh knee in two
 * characters (SSI: Rev 1/2, Curtis: Rev 3), two ADSRs, the wheel's shared modulation source. Four voices a CORE here
 * (Melodee: five a part): with two cores up to eight on the chord part, four of them rendered by each core (3y46,
 * "THE VOICES" below).
 *
 * WHICH VERSION. Melodee's last INTEGER version: commit 8a3074b ("Lo Freq B seven octaves, free-running phase,
 * smoother saturation"), on b351a14 ("calibrate to Rev-4 demo and add 200 factory presets") and 1fbeccd. Melodee
 * 1.2.1's engine is float on the hardware FPU (fa64ef0); ChoralRoot builds without FPU flags, so nothing of it is
 * here but two things that are integer and stand alone: the part's DC blocker (7cd66fb, p5_post) and the factory
 * bank's packed form (1bc4555, p5_factory). The sample path below (p5_samples and what it inlines, p5_mod_samples,
 * p5_inc, p5_env_tick, p5_note_on, p5_render's set-up) is 8a3074b's, statement for statement, but for:
 *
 *   - three optimisations of Rosen's remix of ChoralRoot 0.14-dev ("Choralroot-remix-step20-timing", its stage 36:
 *     docs/STAGE36-PROPHET-OPTIMIZATION.md there), each bit-identical (P5_STAGE36's bits switch them; tests/
 *     cr_prophet_test.c renders every factory program with and without and compares every sample):
 *       1  an oscillator with no waveform switched on computes nothing (p5_wave returns before its saw);
 *       2  the envelope's terms of the cutoff, the Poly-Mod frequency and the Poly-Mod width once a sample, not once
 *          a pass of the oversampling loop (one pass at P5_OVERSAMPLE 1: the same sums in another order);
 *       4  hard sync's phase products as two 32-bit halves instead of a 64-bit multiply (p5_phase_fraction);
 *   - no .dsp_text / .dsp_tables sections: ChoralRoot's map has none. P5_RAM 1 puts the per-sample functions
 *     (p5_samples with p5_inc inlined, p5_mod_samples) into .ram_text as CR_FX_RAM does the effects' loops; 0 runs
 *     them from flash as every other engine here. A DEVICE BUILD HAS 1 since 3y41 (tools/build.py passes it: on the
 *     unit the chord part went 2084 -> 1533 us with the same instructions, docs/FX.md "Code in RAM"); this file's
 *     default stays 0, the host's and the emulator's. The tables are const (in flash), or RAM with
 *     CR_RAM_TABLES_P5 (build/gen/felucca_tables.h P5_TABLE);
 *   - the pitch bend: Melodee's p5_update_bend (voice.c, the program's own range byte P5_BEND) is not here; the part
 *     bends as ChoralRoot's other engines do, through vmod_t (voice.c: the channel's range, RPN 0, +-2 semitones by
 *     default). The program's P5_BEND byte is kept and not used;
 *   - the modulation matrix's ENV source: Melodee's voice.c asks p5_env_source for an ownenv engine; ChoralRoot's
 *     gives 0 (as FM6's and CZ-1's), so the function is not here;
 *   - a load takes the program's UNISON switch, as Melodee's does (the owner, 2026-10-13: "loading a preset should
 *     load the Unison"; cd43720 did not): p5_track_loaded, p5_voice_mode, p5_leave;
 *   - p5_fast_osc_pulse and p5_filter are kept though the engine calls neither (the tests call p5_filter).
 *
 * ChoralRoot's additions (as eng_cz.c's): the state in the parts' pooled engine state (engines.c eng_state.p5), the
 * PRESETS list (INIT; with FELUCCA_P5_FACTORY the programs of Sequential's 200 that the shipped list names:
 * assets/prophet5-factory/shipped.txt -> build/gen/melodee_prophet_factory.h, tools/gen_prophet_factory.py; a program's
 * identity is its Sequential number 1..200, p5_patch_id, whatever its place in the list), the load hook (p5_track_loaded, on every load path through eng_fm6.c
 * fm6_track_loaded), the deep pages for the sound editor (Melodee's P5_PANEL descriptors, prophet_edit.h there; the
 * page plan started from the remix's P5_DEEP) and the patch blob (the 138-byte p5_patch_t: the session keeps a part's
 * edited program; a user slot does not yet: cr_ui.c cu_save_open refuses, docs/PROPHET.md "Saving"). */
#ifndef P5_OVERSAMPLE
#define P5_OVERSAMPLE 1                 /* Melodee's default; its 2x did not pass its budget (tools/gen_tables.py has the
                                         * 1x coefficient table only) */
#endif
#if P5_OVERSAMPLE != 1
#error "eng_prophet_test.c: tools/gen_tables.py makes P5_TPT_G for P5_OVERSAMPLE 1 only"
#endif
#ifndef P5_STAGE36
#define P5_STAGE36 7                    /* the remix's three optimisations (above): bits 1, 2, 4; 0: 8a3074b's code */
#endif
#ifndef P5_RAM
#define P5_RAM 0                        /* 1 (device builds: tools/build.py's default): the per-sample functions
                                         * in .ram_text; 0 here: the host and the emulator have no such section */
#endif
#ifndef P5_DC_BLOCK
#define P5_DC_BLOCK 1                   /* the part's DC blocker (p5_post); 0: 8a3074b's output, for a measurement */
#endif
#ifndef FELUCCA_P5_FACTORY
#define FELUCCA_P5_FACTORY 1            /* Sequential's factory programs (the shipped list's) in the PRESETS list; 0: INIT alone */
#endif
#ifndef P5_TAP                          /* (tools/p5_probe.c: a stage's value, for the headroom table; nothing here) */
#define P5_TAP(k, x) ((void)0)
#endif
#ifndef P5_IDLE_SKIP
#define P5_IDLE_SKIP 1                  /* a part with no voice does not compute the wheel's block (p5_block); 0: it
                                         * does, as to a773d30 (the same samples either way: tests/cr_prophet_test.c) */
#endif
/* A STOLEN VOICE (3y45, docs/PROPHET.md "A stolen voice"). Four voices, so a fifth note takes one, at every change
 * of a full chord. To a773d30 that voice ended as every voice given up does here: p5_render's ramp of its LAST
 * SAMPLE to 0 over the block (Melodee's, made for a victim whose replacement sounds in the same block: no fifth
 * voice's cost), 0.7 ms of a straight line in place of the wave: a tick (tools/p5_probe.c steal: on 8 % of the
 * steals over all 200 programs more than 6 dB above the largest high-frequency step of the held chord itself; the
 * VA's one-block fade: 3 %). ChoralRoot's allocator makes the note WAIT for the voice it takes (voice.c vsq), so the
 * voice may go on sounding meanwhile and fade as a voice does: P5_STEAL_BLOCKS blocks of the whole voice under a
 * straight ramp of the VCA, the note after it. 2: two blocks (1.45 ms: 1 % of the steals above 6 dB; the note
 * a block later than before, never a fifth voice); 4 / 8: 2.9 / 5.8 ms, the note that much later; 1: one block; 0:
 * a773d30's ramp. Only for the part's own voice taken for a waiting note (engine_t.steal): a voice the budget takes for
 * ANOTHER part's note, or the overload shed's, keeps the ramp, where a whole voice more is what the block cannot pay */
#ifndef P5_STEAL_BLOCKS
#define P5_STEAL_BLOCKS 2
#endif
/* A UNISON STACK'S LEVEL: A PROPOSAL, OFF (3y45, docs/PROPHET.md "Headroom"). The platform's UNISON plays its extra
 * voices at a lower velocity (voice.c mono_play), which P5 does not hear (its VCA follows the program's own envelope,
 * the velocity only with the program's VEL switch), and its voices start in phase: four voices on one key are one voice
 * four times, +12 dB (measured: a single key on a UNISON program peaks 11 to 12 dB over the same program's one voice,
 * as high as a four-note chord). Nothing clips or wraps there (the part's bus has 17 dB left), so nothing is trimmed.
 * 1: a stack's voices at half level (the VCA's output, after the filter: its saturation is the single voice's), the
 * owner's to decide by ear (tools/p5_probe.sh levels DIR writes both) */
#ifndef P5_UNISON_TRIM
#define P5_UNISON_TRIM 0
#endif
/* THE LEVEL (3y46, the owner, 2026-10-13: "Since Unison on is going to bring us 5 dB over the limiter, I think we
 * should bring engine down, since the limiter is going to cause chopping possibly"): a voice's output is HALF of what
 * it was to 3y45, exactly: Melodee's "x 2" after the VCA is gone (P5_OUT_SHIFT 0; 1: 3y45's level, for the proof:
 * tests/run_prophet_test.sh renders every program both ways with the capacitor off and wants every sample of the one
 * to be twice the other's). After the filter and the VCA: the ladder's drive and its saturation are what they were.
 * As CZ-1's "* 2" in 0.19 (docs/AUDIO-QUALITY.md). No stored sound holds a level of the engine's: nothing to migrate */
#ifndef P5_OUT_SHIFT
#define P5_OUT_SHIFT 0
#endif
/* A UNISON STACK (3y46, "A UNISON STACK'S VOICES AND ITS LEVEL" below): P5_STACK_PHASE 1: its voices start together
 * (0: at the phases voice.c left in the slots, as to 3y45); P5_STACK_LAW 2: the level law by the program (0: always
 * the root, 1: always the count; tests/cr_prophet_test.c --stack measures them) */
#ifndef P5_STACK_PHASE
#define P5_STACK_PHASE 1
#endif
#ifndef P5_STACK_LAW
#define P5_STACK_LAW 2
#endif
#ifdef P5_HOT                           /* (tools/p5_cost.py: its own, to build the sample loop for one program) */
#elif P5_RAM
#define P5_HOT static __attribute__((section(".ram_text"), noinline, used))
#define P5_HOT_IN static __attribute__((section(".ram_text")))   /* called from the hot functions only */
#else
#define P5_HOT static __attribute__((noinline))
#define P5_HOT_IN static
#endif
#include "prophet_patch.c"
#if FELUCCA_P5_FACTORY
#include "melodee_prophet_factory.h"   /* Sequential's v1.03 programs, the shipped ones (tools/gen_prophet_factory.py) */
#endif
/* THE VOICES (3y46; the owner, 2026-10-13: "lets split main P5 across both cores so we can get up to 8 voices of P5,
 * 4 per core. If the bass is using P5, then it can use up to 4, but if main chord needs 6 voices, and bass has 4, then
 * priority goes to the main chord engine. For Single Core, limit P5 bass to using max of 1 voice"). docs/PROPHET.md
 * "Voices" has the table; the rules are voice.c's (p5_pool_*), the split fx.c's (p5x_run):
 *   P5_CORE   4  the P5 voices one core renders in a block (the unit's readings: four are 1533 .. 1760 us of the
 *                half's 2902; Melodee's five were 2033 from flash and late);
 *   P5_POLY   8  a part's voice STATES: the chord part's voices with two cores (four on each);
 *   P5_STACK  4  a UNISON key's voices at most, on either part, with one core or two (the owner asked for eight
 *                voices of chords, not for a stack of eight);
 *   the pool     TWO CORES: the chord part's and the bass part's P5 voices together are at most 2 x P5_CORE; the
 *                chord part may have all eight, the bass part four, and the chord comes first (a bass stack thins to
 *                one voice, then to none, before a chord note is refused or a chord voice taken).
 *                ONE CORE (Options > Dual Core Off, or core 1 not running): the chord part four, the bass part ONE,
 *                and P5_ONE_POOL (P5_CORE: 4) of both together: with a P5 bass sounding the chord part has three (five P5
 *                voices on one core were the readings that ran late: 2814 us from flash; about 3050 with three
 *                effects by the second reading). -DP5_ONE_POOL=5: four and one, the owner's words alone */
#ifndef P5_CORE
#define P5_CORE 4u                      /* -DP5_CORE=3: three a core, six with two cores: what the unit's readings say
                                         * fits with three effects up (docs/PROPHET.md "Will it fit?"); not the default */
#endif
#define P5_VOICE_US 440u                /* a voice at the peak (the unit: four are 1533 .. 1760): fx.c p5x_assign */
#define P5_POLY (2u * P5_CORE)
#define P5_STACK (P5_CORE < 4u ? P5_CORE : 4u)
#ifndef P5_ONE_POOL
#define P5_ONE_POOL P5_CORE
#endif
_Static_assert(P5_CORE >= 2u && P5_CORE <= 4u && P5_POLY <= NVOICE / 2u, "P5_CORE: 2 .. 4 (P5_STACK_G, the voice slots)");
typedef struct { int32_t value; uint8_t stage; } p5_env_t;
typedef struct {
    uint32_t phase[2];
    int32_t z[4], triangle, noise, sync_tail;
    int32_t drift, filter_drift, amp_drift, env_drift;
    int32_t filter_prev, amp_prev, glide_pitch, last_pcm;
    int32_t coeff_cut, coeff_k, coeff_g[4], coeff_inv;
    uint32_t coeff_valid;
    p5_env_t filter, amp;
    uint32_t steal;                     /* the blocks left of a stolen voice's fade (P5_STEAL_BLOCKS), 0: none */
} p5_voice_t;
/* THE PART'S COEFFICIENTS (3y50, after Melodee 1.4's p5_cache_update, commit 6da744c; written anew): what p5_render
 * worked out for every voice of every block from the part's values (value[]), the macros P_E0..7 and the pressure
 * alone. p5_block works them out once a block for the part, and only when one of those moved (p5_coef_update): the
 * same integers, by the same expressions, regrouped only where a sum or a product of integers is the same whatever
 * the order (docs/PROPHET.md "The part's coefficients") */
typedef struct {
    int32_t coarse_a, coarse_b, pb_free, fine_b, glide_step, cut, kt, k, filter_gain, env_amount;
    int32_t pa_env, pa_b, pf_env, pf_b, pw_env, pw_b, pwm_a, pwm_b, la, lb, ln, e2, e3;
    int16_t e[8];                       /* the key: the macros and the pressure they were worked out for */
    uint8_t at, ok;                     /* ok 0: never worked out (the engine's state is cleared when a part comes to P5) */
} p5_coef_t;
typedef struct { p5_voice_t voice[P5_POLY]; uint32_t lfo; int32_t lfo_value, last_pitch, mod_noise, dc_x1, dc_y1; int16_t wheel[CTL * P5_OVERSAMPLE]; uint16_t wheel_pitch[CTL * P5_OVERSAMPLE]; uint8_t value[55];
    uint16_t sg0, sg1;                  /* a UNISON stack's gain over this block (Q12; 0: no stack), p5_block */
#ifndef P5_COEF_CACHE
#define P5_COEF_CACHE 1                 /* 0: p5_render works its coefficients out per voice, as to 3y48 (a measurement) */
#endif
#if P5_COEF_CACHE
    p5_coef_t c;
#endif
} p5_part_t;
static p5_part_t *p5_part(uint32_t part);         /* engines.c: shared runtime pool (0: a part without one) */
static p5_patch_t p5_patch[NPART] __attribute__((section(".pool")));
static uint8_t p5_ready[NPART];
static p5_patch_t *p5_patch_of(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    if (!p5_ready[k]) { p5_patch_init(&p5_patch[k]); p5_ready[k] = 1; }
    return &p5_patch[k];
}
static inline __attribute__((always_inline)) p5_voice_t *p5_voice(track_t *t, voice_t *v)
{
    p5_part_t *part = p5_part((uint32_t)(t - trk));
    return part ? &part->voice[(uint32_t)(v - t->v) % P5_POLY] : 0;
}
/* Forced-inline local helpers keep RAM code from branching back to XIP.
 * Arithmetic matches dsp.c; the common renderer remains unchanged. */
static inline __attribute__((always_inline)) int32_t p5_fast_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline __attribute__((always_inline)) int32_t p5_fast_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* Normalize before division, retaining at least 16 significant bits. Unlike
 * dividing the denominator by 32768 first, this stays bounded at low rates. */
static inline __attribute__((always_inline)) uint32_t p5_fraction_q15(uint32_t distance,uint32_t inc)
{
    uint32_t shift=(uint32_t)__builtin_clz(inc);if(shift>15u)shift=15u;
    uint32_t value=(distance<<shift)/(inc>>(15u-shift));
    return value>32768u?32768u:value;
}
/* (3y50, the edges' study: tools/p5_edges.c counts the divides through P5_EDGE and builds the alternatives through
 * P5_BLEP_FRACTION_ALT / P5_BLEP_ALT; docs/PROPHET.md "The oscillators' edges". Nothing here in any other build) */
#ifndef P5_EDGE
#define P5_EDGE(k, distance, inc) ((void)0)
#endif
static inline __attribute__((always_inline)) int32_t p5_blep_fraction(uint32_t distance,uint32_t inc)
{
    P5_EDGE(0, distance, inc);
#ifdef P5_BLEP_FRACTION_ALT
    return P5_BLEP_FRACTION_ALT(distance, inc);
#endif
    /* At high rates the short division loses at most six Q15 correction
     * units. Low rates require normalization to avoid the earlier overflow. */
    if(inc>=(1u<<27)){
        uint32_t x=distance/(inc>>15);
        return (int32_t)(x>32768u?32768u:x);
    }
    return (int32_t)p5_fraction_q15(distance,inc);
}
static inline __attribute__((always_inline)) int32_t p5_fast_blep(uint32_t ph, uint32_t inc)
{
    int32_t x;
    P5_EDGE(2, ph, inc);
#ifdef P5_BLEP_ALT
    return P5_BLEP_ALT(ph, inc);
#endif
    if (!inc)
        return 0;
    if (ph < inc) {
        x = p5_blep_fraction(ph,inc);                /* 0..32768 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -p5_blep_fraction(0xFFFFFFFFu-ph,inc);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline __attribute__((always_inline)) int32_t p5_fast_osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - p5_fast_blep(ph, inc);
}
static inline __attribute__((always_inline)) int32_t p5_fast_osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (p5_fast_osc_saw(ph, inc) - p5_fast_osc_saw(ph + pw, inc)) >> 1;
}
static inline __attribute__((always_inline)) int32_t p5_fast_softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = (a + 16) >> 5;
    int32_t y = P5_TANH_Q15[i > 2048 ? 2048 : i];
    return x < 0 ? -y : y;
}
/* The same knee, interpolated. The nearest lookup quantizes to 32-unit steps;
 * where its output reaches the VCA unfiltered, those steps are an audible
 * grain once the filter has closed (Internalized's sustain, -50 dB). */
static inline __attribute__((always_inline)) int32_t p5_smooth_softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 5;
    int32_t y = i >= 2048 ? P5_TANH_Q15[2048] : P5_TANH_Q15[i] + (((P5_TANH_Q15[i + 1] - P5_TANH_Q15[i]) * (a & 31)) >> 5);
    return x < 0 ? -y : y;
}
static inline __attribute__((always_inline)) uint32_t p5_fast_noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
static inline __attribute__((always_inline)) int32_t p5_fast_amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}
static inline __attribute__((always_inline)) int32_t p5_fast_voice_amp(int32_t s, const vmod_t *m, uint32_t i)
{
    return p5_fast_mulq15(p5_fast_mulq15(s, p5_fast_amp_at(m, i)), VOICE_FS);
}

/* Stored knobs are 0..127 (Sequential's factory data uses the whole range,
 * e.g. levels and sustains of 127), not the 0..120 of the MIDI guide. */
static int32_t p5_value(const uint8_t *p,uint32_t k)
{
    return p[k]>127u?127:p[k];
}
static inline __attribute__((always_inline)) int32_t p5_m12(int32_t a, int32_t b) { return (a * b) >> 12; }

#define P5_ENV_END (1 << 14)   /* -60 dB: a release has ended */
/* Attack is a linear ramp; decay and release are exponential, except the
 * Rev 1/2 filter envelope (SSM), which Sequential describes as almost linear. */
static int32_t p5_env_tick(p5_env_t *e, int gate, const uint8_t *p,
                          uint32_t a, uint32_t d, uint32_t s, uint32_t r, int32_t variation, const uint8_t *value, int ssm)
{
    if (!gate && e->stage != 0u) e->stage = 3;
    int32_t target = value[s] * (1 << 24) / 127;
    uint32_t rel = p[P5_RELEASE_ON] ? (uint32_t)value[r] : 16u;     /* release off: fast (3 ms) */
    if (e->stage == 1u) {
        int32_t step = (int32_t)P5_ENV_ATK[value[a]];
        step += (step >> 8) * variation / 128;
        e->value += step;
        if (e->value >= (1 << 24)) { e->value = 1 << 24; e->stage = 2; }
    } else if (ssm && e->stage >= 2u) {
        int32_t step = (int32_t)P5_ENV_SSM[e->stage == 2u ? value[d] : rel];
        step = clamp(step + (step >> 8) * variation / 128, 1, 1 << 24);
        if (e->stage == 3u) target = 0;
        e->value = e->value > target ? (e->value - step > target ? e->value - step : target)
                                     : (e->value + step < target ? e->value + step : target);
        if (e->stage == 3u && e->value < P5_ENV_END) { e->value = 0; e->stage = 0; }
    } else if (e->stage == 2u) {
        uint32_t coeff=P5_ENV_EXP[value[d]];
        coeff=(uint32_t)clamp((int32_t)coeff+((int32_t)coeff>>8)*variation/128,1,65535);
        e->value += mulq16(target - e->value, coeff);
    } else if (e->stage == 3u) {
        uint32_t coeff=P5_ENV_EXP[rel];
        coeff=(uint32_t)clamp((int32_t)coeff+((int32_t)coeff>>8)*variation/128,1,65535);
        e->value -= mulq16(e->value, coeff);
        if (e->value < P5_ENV_END) { e->value = 0; e->stage = 0; }
    }
    return e->value >> 9;
}

static void p5_note_on(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    const uint8_t *p = p5_patch_of(t)->raw;
    if (!s) return;                                   /* (no engine state: no voice, engines.c ENG_STATE_NPART) */
    s->steal = 0;                                     /* (a fading voice struck again in place: no fade left) */
    if (!voice_was) {
        memset(s, 0, sizeof *s);
        s->phase[0]=v->ph[0];s->phase[1]=v->ph[1];
#if P5_STACK_PHASE
        if(t->p[P_VOICE]==V_UNISON)s->phase[0]=s->phase[1]=0;   /* (a stack's voices start together: "A UNISON STACK'S VOICES") */
#endif
        s->triangle = 32767;
        /* Thermal excitation lets resonance start with the oscillator mixer
         * shut, as native self-oscillating factory programs require. */
        if(p[P5_RESONANCE]>=110u && !p[P5_LEVEL_A] && !p[P5_LEVEL_B] && !p[P5_NOISE]){
            /* The physical filter is already oscillating behind the closed
             * VCA. Seed that state when waking an otherwise silent voice. */
            s->z[0]=16384;s->z[2]=-16384;
        }
        s->noise = (int32_t)(0x5A1793u + v->age * 17u + (uint32_t)(v-t->v) * 101u);
    }
    if (!voice_was) {
        p5_part_t *part=p5_part((uint32_t)(t-trk));
        s->glide_pitch=part->last_pitch ? part->last_pitch : v->pitch16;
    }
    p5_part((uint32_t)(t-trk))->last_pitch=v->pitch16;
    s->filter.stage = s->amp.stage = 1;
    /* 0 is the stable Rev 4 position, 127 the loose Rev 1: most factory
     * programs use 0, many 42 (Rev 3), few 127. */
    int32_t vintage = p5_value(p, P5_VINTAGE);
    /* Fixed mismatch for the life of the voice, deterministic per note/slot.
     * This is an initial engineering range, awaiting reference calibration. */
    s->drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 1024;
    s->filter_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 32;
    s->amp_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 16;
    s->env_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 128;
    /* The oscillators free-run, so a key finds a Lo Freq B anywhere in its
     * cycle. Starting each new voice at the bottom of the ramp instead held
     * Pickle Pincher's filter shut (Poly-Mod B 127) for every fresh note. */
    if (!voice_was && p[P5_LOW_B]) s->phase[1] = p5_fast_noise32(&s->noise);
}
static int p5_done(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    return !s || (!v->gate && s->amp.stage == 0u);
}
/* Triangle is bipolar; saw and square are positive with the same
 * peak-to-peak swing (Q14: triangle +-16384, saw/square 0..32767).
 * Preserve the level of a single selected waveform when combining sources. */
static inline __attribute__((always_inline)) int32_t p5_lfo_wave(uint32_t phase,const uint8_t *p)
{
    int32_t y=0;uint32_t n=0;
    if(p[P5_LFO_SAW]){y+=(int32_t)(phase>>17);n++;}
    if(p[P5_LFO_TRI]){int32_t v=(int32_t)(phase>>17);y+=phase<0x80000000u?v*2-16384:49151-v*2;n++;}
    if(p[P5_LFO_PULSE]){y+=phase<0x80000000u?32767:0;n++;}
    return n==3u ? y/3 : n==2u ? y>>1 : y;
}
/* 2^(x/65536) in Q15 for |x| < 65536 (Q16 octaves): the wheel's pitch ratio. */
static inline __attribute__((always_inline)) uint16_t p5_ratio_q15(int32_t x)
{
    x=p5_fast_clamp(x,-65535,65535);
    uint32_t f=(uint32_t)x&65535u,i=f>>8;
    uint32_t r=P5_EXP2_Q14[i]+(((P5_EXP2_Q14[i+1u]-P5_EXP2_Q14[i])*(f&255u)+128u)>>8);
    return (uint16_t)(x<0?r:r*2u);
}
/* Rev-4 vibrato: a full-amount triangle swings +-235 cents (measured at
 * initial amounts 18..55 against a factory demo, see gen_tables.py). */
#define P5_WHEEL_PITCH_Q14 12834    /* 235/1200 octave per 16384, in Q16 octaves */
/* The Prophet's wheel source is shared across all voices in a part. Calculate
 * each audio-rate sample once, including combined waveforms and noise. */
P5_HOT void p5_mod_samples(p5_part_t *s,const uint8_t *p,uint32_t inc,int32_t amount,int32_t mix)
{
    uint32_t phase=s->lfo;if(!s->mod_noise)s->mod_noise=0x517953;
    for(uint32_t i=0;i<CTL*P5_OVERSAMPLE;i++){
        int32_t noise=(int32_t)(p5_fast_noise32(&s->mod_noise)>>17)-16384;
        int32_t w=p5_fast_mulq15(p5_fast_mulq15(p5_lfo_wave(phase,p),32767-mix)+p5_fast_mulq15(noise,mix),amount);
        s->wheel[i]=(int16_t)w;
        /* Exponential, so a triangle goes equally sharp and flat in cents.
         * Calculate once per part, rather than for each voice. */
        s->wheel_pitch[i]=p5_ratio_q15((w*P5_WHEEL_PITCH_Q14+8192)>>14);
        phase+=inc;
    }
    s->lfo=phase;s->lfo_value=p5_lfo_wave(phase,p);
}
/* A UNISON STACK'S VOICES AND ITS LEVEL (3y46). The stack is the program's VOICES count, P5_STACK at most. How many
 * of them SOUND is the budget's to say: all with two cores and room in the pool, ONE on the bass part with one core,
 * fewer while the chord part needs the pool's voices. So that a key is as loud whatever it was given (the owner,
 * 2026-10-13: "the engine also sounds quite different on single core"), the voices that sound are raised to the
 * level of the whole stack (P5_STACK_G, Q12), after the filter and the VCA (p5_samples), on a straight line over a
 * block when a voice of the stack is given up or comes back. Two things make that a level one can name:
 *   THE VOICES START TOGETHER (p5_note_on: both oscillators at phase 0). To 3y45 a stack's first key after power-on
 *     did (every slot's phase 0: the +12 dB of docs/PROPHET.md "Headroom") and every later one had oscillator A at
 *     the phases voice.c mono_play had left in the slots and oscillator B together: a level between a voice's
 *     x 2 and x 4 that changed from key to key and that one voice cannot stand in for. Now a stack with DETUNE 0
 *     and Vintage 0 is one voice N times, exactly, and with them it is spread as the program spreads it;
 *   THE LAW: n voices of a stack of N play at N / n each (one voice of four: x 4, the stack's own sum while its
 *     voices are together), and at sqrt(N / n) where the program takes its voices apart at once: DETUNE above 0,
 *     or no oscillator in the mixer (noise, the filter's own tone: each voice's is its own).
 * Measured over the 38 stacks of Sequential's 200 (cr_prophet_test --stack; docs/PROPHET.md "Level"). P5_UNISON_TRIM
 * 1 halves it all. p5_block counts, on the core that renders the part */
static const uint16_t P5_STACK_G[2][4][4]={   /* [law][N - 1][n - 1]: 4096 x sqrt(N / n), 4096 x N / n */
    {{4096,4096,4096,4096},{5793,4096,4096,4096},{7094,5017,4096,4096},{8192,5793,4730,4096}},
    {{4096,4096,4096,4096},{8192,4096,4096,4096},{12288,6144,4096,4096},{16384,8192,5461,4096}}};
static uint32_t p5_stack(const track_t *t)        /* a UNISON key's voices: the program's count, P5_STACK at most */
{
    const uint8_t *p=p5_patch_of(t)->raw;
    return p[P5_UNISON] ? (uint32_t)clamp(p[P5_UNISON_COUNT]?p[P5_UNISON_COUNT]:(int32_t)P5_STACK,1,(int32_t)P5_STACK) : P5_STACK;
}
static void p5_stack_gain(track_t *t,p5_part_t *s)
{
    uint32_t i,n=0,N;
    if(t->p[P_VOICE]!=V_UNISON){s->sg0=s->sg1=0;return;}
    N=p5_stack(t);
    for(i=0;i<NVOICE;i++)n+=t->v[i].active && t->v[i].stage!=4u;
    if(!n){s->sg0=s->sg1=0;return;}
    s->sg0=s->sg1;
    {
        const uint8_t *p=p5_patch_of(t)->raw;
        uint32_t law=P5_STACK_LAW<2 ? P5_STACK_LAW : !(p[P5_UNISON_DETUNE] || !(p[P5_LEVEL_A] || p[P5_LEVEL_B]));
        s->sg1=(uint16_t)(P5_STACK_G[law][N-1u][(n>N?N:n)-1u]>>P5_UNISON_TRIM);
    }
    if(!s->sg0)s->sg0=s->sg1;
}
#if P5_COEF_CACHE
/* the part's coefficients (p5_coef_t): each p5_render's expression for it, statement for statement, with the voice's
 * own terms (m, the drifts, the glide, the velocity, the width's modulation) left to the voice */
static __attribute__((noinline)) void p5_coef_update(const track_t *t,p5_part_t *s,const uint8_t *p)
{
    const uint8_t *q=s->value;
    p5_coef_t *c=&s->c;
    int curtis=!!p[P5_FILTER_REV];
    int32_t c0=q[P5_CUTOFF],resonance;
    for(uint32_t k=0;k<8u;k++)c->e[k]=t->p[P_E0+k];
    c->at=t->at;
    c->coarse_a=(clamp(q[P5_FREQ_A],0,96)/2-12)*16;
    c->coarse_b=(clamp(q[P5_FREQ_B],0,96)/2-12)*16;
    c->pb_free=12*16+q[P5_FREQ_B]*108*16/127;
    c->fine_b=q[P5_FINE_B]*100/128;
    c->e2=t->p[P_E2]*16;
    c->e3=t->p[P_E3]*16;
    c->glide_step=(int32_t)(ENV_LIN[q[P5_GLIDE]]>>14);if(c->glide_step<1)c->glide_step=1;
    /* cut: the sum's part-wide terms (+ m->cutoff + the drift + the key tracking, per voice: a sum of integers) */
    c->cut=1618+c0*236+(curtis?0:(127-c0>66?66:127-c0)*38) + t->p[P_E0]*256 + (p[P5_PRESS_FILTER]?t->at*96:0);
    c->kt=p5_fast_clamp(p[P5_KEY_FILTER],0,2)*P5_KEYTRACK_HALF_Q7;   /* (d * kf * HALF: one product, either order) */
    resonance=p5_fast_clamp(q[P5_RESONANCE]+t->p[P_E1],0,127);
    c->k=resonance*(curtis?19200:18800)/127;
    c->filter_gain=32767-resonance*(curtis?100:45);
    c->env_amount=q[P5_ENV_FILTER]*165;
    c->pa_env=q[P5_POLY_ENV]*85; c->pa_b=q[P5_POLY_B]*35;
    c->pf_env=q[P5_POLY_ENV]*226; c->pf_b=q[P5_POLY_B]*226;
    c->pw_env=q[P5_POLY_ENV]*64; c->pw_b=q[P5_POLY_B]*258;
    c->pwm_a=q[P5_PW_A]*258+t->p[P_E4]*256; c->pwm_b=q[P5_PW_B]*258+t->p[P_E5]*256;
    c->la=clamp(q[P5_LEVEL_A]+t->p[P_E6],0,127)*258; c->lb=clamp(q[P5_LEVEL_B]+t->p[P_E7],0,127)*258; c->ln=q[P5_NOISE]*258;
    c->ok=1;
}
#endif
static void p5_block(track_t *t)
{
    p5_part_t *s=p5_part((uint32_t)(t-trk));const uint8_t *p=p5_patch_of(t)->raw;
    if(!s)return;
#if P5_IDLE_SKIP
    /* ChoralRoot, 3y45: a part with no voice (an unplayed bass part on P5, every block of it; the chord part between
     * phrases) needs none of the wheel's 32 samples, only where they leave the part's state: the LFO's phase (it runs
     * free: 32 increments, mod 2^32) and the wheel's noise generator (32 steps). value[] is written again before the
     * first voice renders (this function runs before the voices, voice.c track_render_x). The same samples as the
     * whole block, bit for bit (docs/PROPHET.md "An unplayed part") */
    {
        uint32_t i,any=0;
        for(i=0;i<NVOICE;i++)any|=t->v[i].active;
        if(!any){
            s->sg0=s->sg1=0;
            if(!s->mod_noise)s->mod_noise=0x517953;
            for(i=0;i<CTL*P5_OVERSAMPLE;i++)(void)p5_fast_noise32(&s->mod_noise);
            s->lfo+=(P5_LFO_INC[p5_value(p,P5_LFO_RATE)]/P5_OVERSAMPLE)*(CTL*P5_OVERSAMPLE);
            s->lfo_value=p5_lfo_wave(s->lfo,p);
            return;
        }
    }
#endif
    p5_stack_gain(t,s);
    /* Raw-to-DSP clamping is part-wide; envelopes and all the voices share it. */
#if P5_COEF_CACHE
    {
        /* (the bytes are read every block, as before: any writer, the editor's, a load's, MIDI's or a test's own, is
         * seen in the next block. A version counter would skip this loop, but it would need every writer to count;
         * the compare is the loop's own cost, docs/PROPHET.md) */
        uint32_t d=0;
        for(uint32_t k=0;k<55u;k++){uint8_t x=(uint8_t)p5_value(p,k);d|=(uint32_t)(x^s->value[k]);s->value[k]=x;}
        for(uint32_t k=0;k<8u;k++)d|=(uint32_t)(t->p[P_E0+k]^s->c.e[k]);
        d|=(uint32_t)(s->c.at^t->at)|(uint32_t)(s->c.ok^1u);   /* (one test: no cold path laid behind the function) */
        if(d)p5_coef_update(t,s,p);
    }
#else
    for(uint32_t k=0;k<55u;k++)s->value[k]=(uint8_t)p5_value(p,k);
#endif
    uint32_t inc=P5_LFO_INC[p5_value(p,P5_LFO_RATE)]/P5_OVERSAMPLE;
    int32_t amount=P5_AMOUNT_Q15[clamp(p5_value(p,P5_LFO_INITIAL)+t->mw+(p[P5_PRESS_LFO]?t->at:0),0,127)];
    void (*volatile run)(p5_part_t *,const uint8_t *,uint32_t,int32_t,int32_t)=p5_mod_samples;
    run(s,p,inc,amount,p5_value(p,P5_WHEEL_MIX)*258);
}
#define P5_PART_BASS 1u                           /* cr_out.c CR_PART_BASS (asserted there) */
/* the part's voices at most ("THE VOICES" above): a UNISON key its stack; the chord part 8 with two cores and 4 with
 * one; the bass part 4 with two cores and ONE with one, whatever its mode */
static uint32_t p5_cap(const track_t *t)
{
    uint32_t bass=(uint32_t)(t-trk)==P5_PART_BASS,two=(uint32_t)cr_dualcore_active();
    uint32_t n=t->p[P_VOICE]==V_UNISON ? p5_stack(t) : two && !bass ? P5_POLY : P5_CORE;
    return bass && !two ? 1u : n;
}
static void p5_legato(track_t *t,voice_t *v)
{
    if(p5_patch_of(t)->raw[P5_RETRIGGER]&1u){p5_voice_t *s=p5_voice(t,v);if(s)s->filter.stage=s->amp.stage=1;}
}
/* Native performance controls become the track's allocator settings at load
 * or native edit. Afterwards the MIX group's VOICE / PRIORITY / DETUNE can override.
 * ChoralRoot, UNISON (docs/PROPHET.md "UNISON"): a LOAD takes the program's UNISON switch, on the chord part and on
 * the bass part (the owner, 2026-10-13; Melodee's does the same). p5_own is the part's voice mode WITHOUT the
 * program's switch: what the load's caller gave it (cr_ui.c cu_load: the chord part POLY, the bass part MONO; a
 * restored session or the picker's snapshot: the mode saved, and when that is UNISON the part's default). A program
 * without the switch leaves the part's mode as the caller set it, so the next load of a poly program is POLY again;
 * the editor's UNISON cell switches between UNISON and p5_own; an engine change that keeps the part's mix
 * (cr_ui.c cu_engine_switch) takes a program's UNISON off again (p5_leave) */
static uint8_t p5_own[NPART];                     /* the part's voice mode without the program's UNISON (V_POLY, V_MONO ..) */
static void p5_track_accept(track_t *t)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    t->p[P_PRIO]=p[P5_RETRIGGER]<2u?1:0;
    t->p[P_DETUNE]=(int16_t)(clamp(p[P5_UNISON_DETUNE],0,7)*127/7);
    t->p[P_ALLOC]=1; /* repeated keys reuse a voice, as the original does */
}
static void p5_voice_mode(track_t *t)             /* the editor turned UNISON */
{
    uint32_t k=(uint32_t)(t-trk)%NPART;
    t->p[P_VOICE]=(int16_t)(p5_patch_of(t)->raw[P5_UNISON]?V_UNISON:p5_own[k]);
}
/* the part leaves the engine with its mix kept (cr_ui.c cu_engine_switch, before it writes eng_req): a UNISON that
 * the program gave is not the next engine's (the VA's UNISON is eight voices); one the player set on the MIX page
 * of a program without the switch stays, as between any two engines */
static void p5_leave(track_t *t)
{
    uint32_t k=(uint32_t)(t-trk)%NPART;
    if(t->p[P_VOICE]==V_UNISON && p5_patch_of(t)->raw[P5_UNISON])t->p[P_VOICE]=p5_own[k];
}

/* Four trapezoidal one-poles with an algebraic feedback solution. Curtis:
 * concentrated input saturation with resonance gain loss. SSI: distributed
 * stage saturation and different resonance compensation. These are separate
 * provisional character models, NOT calibrated chip emulations. Q12 products
 * stay within int32 bounds; state clamps are protection, not the normal knee. */
static inline __attribute__((always_inline)) int32_t p5_filter_core(p5_voice_t *s, int32_t input, int32_t cutoff, int32_t k, int32_t gain, int curtis)
{
    cutoff = p5_fast_clamp(cutoff, 0, 127 << 8);
    int32_t g,g2,g3,g4,reciprocal;
    if(s->coeff_valid && cutoff==s->coeff_cut && k==s->coeff_k){
        g=s->coeff_g[0];g2=s->coeff_g[1];g3=s->coeff_g[2];g4=s->coeff_g[3];reciprocal=s->coeff_inv;
    }else{
        uint32_t i=(uint32_t)cutoff>>8;g=P5_TPT_G[i];
        if(i<127u)g+=(P5_TPT_G[i+1u]-g)*(cutoff&255)>>8;
        g2=p5_m12(g,g);g3=p5_m12(g2,g);g4=p5_m12(g3,g);
        uint32_t d=(uint32_t)p5_m12(k,g4),index=d>>5;reciprocal=P5_INV_DEN[index];
        reciprocal+=((P5_INV_DEN[index+1u]-reciprocal)*(int32_t)(d&31u))>>5;
        s->coeff_g[0]=g;s->coeff_g[1]=g2;s->coeff_g[2]=g3;s->coeff_g[3]=g4;
        s->coeff_cut=cutoff;s->coeff_k=k;s->coeff_inv=reciprocal;s->coeff_valid=1;
    }
    int32_t sigma=p5_m12(4096-g,p5_m12(g3,s->z[0])+p5_m12(g2,s->z[1])+p5_m12(g,s->z[2])+s->z[3]);
    input=p5_fast_mulq15(input,gain);
    P5_TAP(1,input-p5_m12(k,sigma));
    int32_t drive=p5_fast_clamp(input-p5_m12(k,sigma),-262143,262143);
    int32_t u=(drive*reciprocal)>>13;
    P5_TAP(2,u);
    u = p5_fast_softclip(u);
    /* Fixed four-pole stages: no indexed loop or per-stage mode branch.
     * The following poles filter an SSI stage's lookup steps, except the last. */
#define P5_STAGE(j, clip) do { \
    int32_t delta=p5_m12(g,u-s->z[j]); \
    int32_t y=s->z[j]+delta; \
    P5_TAP(3,y+delta); \
    s->z[j]=p5_fast_clamp(y+delta,-60000,60000); \
    u=clip(y); \
} while(0)
#define P5_LINEAR(y) (y)
    if(curtis){P5_STAGE(0,P5_LINEAR);P5_STAGE(1,P5_LINEAR);P5_STAGE(2,P5_LINEAR);P5_STAGE(3,P5_LINEAR);}
    else {P5_STAGE(0,p5_fast_softclip);P5_STAGE(1,p5_fast_softclip);P5_STAGE(2,p5_fast_softclip);P5_STAGE(3,p5_smooth_softclip);}
#undef P5_LINEAR
#undef P5_STAGE
    return p5_smooth_softclip(u);
}

/* (Melodee's and this tree's tests call the filter alone; the engine calls p5_filter_core) */
static __attribute__((unused)) int32_t p5_filter(p5_voice_t *s,int32_t input,int32_t cutoff,int32_t resonance,int curtis)
{
    return p5_filter_core(s,input,cutoff,resonance*(curtis?19200:18800)/127,
                          32767-resonance*(curtis?100:45),curtis);
}

static inline __attribute__((always_inline)) int32_t p5_wave(uint32_t phase, uint32_t inc, uint32_t pw, int saw, int pulse, int tri, int32_t *triangle)
{
    /* Simultaneous waveforms share their discontinuity correction. */
#if P5_STAGE36 & 1
    if (!(saw || pulse || tri)) return 0; /* No waveform or triangle state to update (the remix's stage 36). */
#endif
    int32_t y=0, a=p5_fast_osc_saw(phase,inc), square=0;
    if(saw)y+=a>>1;
    if(pulse){square=(a-p5_fast_osc_saw(phase+pw,inc))>>1;y+=square;}
    if(tri){
        if(!pulse || pw!=0x80000000u)square=(a-p5_fast_osc_saw(phase+0x80000000u,inc))>>1;
        *triangle=p5_fast_clamp(p5_fast_mulq15(*triangle,32767)+
            (int32_t)(((int64_t)square*2*(int32_t)(inc>>15))>>15),-40000,40000);
        y+=*triangle>>1;
    }
    return y;
}

/* Normalize the fraction to a 32-bit divide. Both numerator and denominator
 * retain at least 16 significant bits; error is at most one Q15 unit. */
static inline __attribute__((always_inline)) uint32_t p5_sync_fraction(uint32_t distance,uint32_t inc)
{
    P5_EDGE(1, distance, inc);
    return p5_fraction_q15(distance,inc);
}
static inline __attribute__((always_inline)) uint32_t p5_pw(int32_t value)
{
    /* Avoid degenerate pulse widths; signed modulation is applied before p5_fast_clamp. */
    return (uint32_t)p5_fast_clamp(value, 1024, 31743) << 17;
}
/* Poly-Mod to oscillator A frequency is exponential, as on the CEM3340:
 * base * 2^(x/4096), x in Q12 octaves. 32-bit halves only (no 64-bit shifts). */
P5_HOT_IN uint32_t p5_inc(uint32_t base, int32_t x)
{
    if (!x) return base;
    x = p5_fast_clamp(x, -8 * 4096, 6 * 4096);
    int32_t oct = x >> 12;
    uint32_t f = (uint32_t)x & 4095u, i = f >> 4;
    uint32_t r = P5_EXP2_Q14[i] + (((P5_EXP2_Q14[i + 1u] - P5_EXP2_Q14[i]) * (f & 15u)) >> 4);
    uint32_t y = (base >> 14) * r + (((base & 16383u) * r) >> 14);   /* base * 1..2, < 2^32 */
    if (oct < 0) return (y >> -oct) ? y >> -oct : 1u;
    return y > (1932735280u >> oct) ? 1932735280u : y << oct;
}
static inline __attribute__((always_inline)) uint32_t p5_wheel_inc(uint32_t base,uint16_t ratio)
{
    /* Base is capped below 2^31; both partial products fit uint32. */
    uint32_t inc=(base>>15)*ratio+(((base&32767u)*ratio)>>15);
    return inc>1932735280u?1932735280u:(uint32_t)inc;
}
/* inc * fraction >> 15 for hard sync's phase fractions (0..32768), mod 2^32 as the 64-bit product's low word is (the
 * remix's stage 36): inc = hi * 32768 + lo, so the product >> 15 is hi * fraction + (lo * fraction >> 15) exactly;
 * lo * fraction < 2^30, and the sum wraps as the truncated 64-bit result does. */
static inline __attribute__((always_inline)) uint32_t p5_phase_fraction(uint32_t inc,uint32_t fraction)
{
#if P5_STAGE36 & 4
    return (inc>>15)*fraction+(((inc&32767u)*fraction)>>15);
#else
    return (uint32_t)(((uint64_t)inc*fraction)>>15);
#endif
}
/* Summed native waveforms can exceed Q15 before the mixer saturation. */
static inline __attribute__((always_inline)) int32_t p5_wave_gain(int32_t wave,int32_t gain)
{
    /* gain is 0..32766. Split the signed waveform into whole/fractional Q15
     * parts for an exact product without overflow or a target 64-bit helper. */
    return (wave>>15)*gain+(((wave&32767)*gain)>>15);
}
typedef struct { int32_t f0, a0, f1, a1, ia, ib, cut, k, filter_gain, filter_velocity, amp_velocity, env_amount, pa_env, pa_b, pf_env, pf_b, pw_env, pw_b, pwm_a, pwm_b, la, lb, ln; const int16_t *wheel; const uint16_t *wheel_pitch;
} p5_render_params_t;
P5_HOT void p5_samples(p5_voice_t *restrict s, const uint8_t *restrict p, int32_t *restrict out, uint32_t n, const vmod_t *m, const p5_render_params_t *c)
{
    int32_t f0=c->f0;
    int32_t a0=c->a0;
    int32_t f1=c->f1;
    int32_t a1=c->a1;
    int32_t ia=c->ia;
    int32_t ib=c->ib;
    int32_t cut=c->cut;
    int32_t k=c->k, filter_gain=c->filter_gain;
    int32_t filter_velocity=c->filter_velocity;
    int32_t amp_velocity=c->amp_velocity;
    int32_t env_amount=c->env_amount;
    int32_t pa_env=c->pa_env, pa_b=c->pa_b, pf_env=c->pf_env, pf_b=c->pf_b, pw_env=c->pw_env, pw_b=c->pw_b;
    int32_t pwm_a=c->pwm_a;
    int32_t pwm_b=c->pwm_b;
    int32_t la=c->la;
    int32_t lb=c->lb;
    int32_t ln=c->ln;
    int32_t last=0;
    for (uint32_t i=0; i<n; i++) {
        int32_t fe=f0+(((f1-f0)*(int32_t)i)>>CTL_LOG2);
        int32_t ae=a0+(((a1-a0)*(int32_t)i)>>CTL_LOG2);
#if P5_STAGE36 & 2
        /* Envelope values are constant across this sample's oversampling passes.
         * Keep the original fixed-point rounding/order for identical PCM (the remix's stage 36). */
        int32_t fc_base=cut+p5_fast_mulq15(p5_fast_mulq15(fe,filter_velocity),env_amount);
        int32_t freq_env=p[P5_POLY_FREQ]?p5_fast_mulq15(fe,pa_env):0;
        int32_t width_env=p[P5_POLY_PW]?p5_fast_mulq15(fe,pw_env):0;
        if (p[P5_POLY_FILTER]) fc_base+=p5_fast_mulq15(fe,pf_env);
#endif
        int32_t sum=0;
        for (uint32_t os=0; os<P5_OVERSAMPLE; os++) {
            int32_t noise=(int32_t)(p5_fast_noise32(&s->noise)>>17)-16384;
            int32_t lfo=c->wheel[i*P5_OVERSAMPLE+os];
            uint16_t wheel_pitch=c->wheel_pitch[i*P5_OVERSAMPLE+os];
            uint32_t db=p[P5_WHEEL_FREQ_B]?p5_wheel_inc(ib,wheel_pitch):(uint32_t)ib;
            int32_t b=p5_wave(s->phase[1],db,p5_pw(pwm_b+(p[P5_WHEEL_PW_B]?lfo:0)),p[P5_SAW_B],p[P5_PULSE_B],p[P5_TRI_B],&s->triangle);
            int32_t bn=b*2;   /* one full oscillator B wave: +-32768 */
#if P5_STAGE36 & 2
            uint32_t da=p[P5_POLY_FREQ]?p5_inc(ia,freq_env+p5_wave_gain(bn,pa_b)):(uint32_t)ia;
            if(p[P5_WHEEL_FREQ_A])da=p5_wheel_inc(da,wheel_pitch);
            uint32_t pw=p5_pw(pwm_a+(p[P5_POLY_PW]?width_env+p5_wave_gain(b,pw_b):0)+(p[P5_WHEEL_PW_A]?lfo:0));
#else
            uint32_t da=p[P5_POLY_FREQ]?p5_inc(ia,p5_fast_mulq15(fe,pa_env)+p5_wave_gain(bn,pa_b)):(uint32_t)ia;
            if(p[P5_WHEEL_FREQ_A])da=p5_wheel_inc(da,wheel_pitch);
            uint32_t pw=p5_pw(pwm_a+(p[P5_POLY_PW]?p5_fast_mulq15(fe,pw_env)+p5_wave_gain(b,pw_b):0)+(p[P5_WHEEL_PW_A]?lfo:0));
#endif
            int32_t unused=0;
            int32_t a=p5_wave(s->phase[0],da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused)+s->sync_tail;
            s->sync_tail=0;
            uint32_t nb=s->phase[1]+db, na=s->phase[0]+da;
            if (p[P5_SYNC] && nb<s->phase[1] && db) {
                uint32_t frac=p5_sync_fraction(0u-s->phase[1],db);
                uint32_t at=s->phase[0]+p5_phase_fraction(da,frac);
                int32_t before=p5_wave(at,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                int32_t after=p5_wave(0,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                int32_t jump=after-before, remain=32768-(int32_t)frac;
                /* Fractional discontinuity correction over this/next sub-sample. */
                a += p5_fast_mulq15((jump>>1),p5_fast_mulq15(remain,remain));
                s->sync_tail=-p5_fast_mulq15((jump>>1),p5_fast_mulq15((int32_t)frac,(int32_t)frac));
                na=p5_phase_fraction(da,(uint32_t)remain);
            }
            s->phase[0]=na; s->phase[1]=nb;
            int32_t x=p5_wave_gain(a,la)+p5_wave_gain(b,lb)+p5_fast_mulq15(noise,ln);
            P5_TAP(0,x);
#if P5_STAGE36 & 2
            int32_t fc=fc_base;
            if (p[P5_POLY_FILTER]) fc+=p5_wave_gain(bn,pf_b);
#else
            int32_t fc=cut+p5_fast_mulq15(p5_fast_mulq15(fe,filter_velocity),env_amount);
            if (p[P5_POLY_FILTER]) fc+=p5_fast_mulq15(fe,pf_env)+p5_wave_gain(bn,pf_b);
#endif
            if (p[P5_WHEEL_FILTER]) fc+=(lfo*7)>>3;   /* +-4 octaves at full amount */
            sum+=p5_filter_core(s,p5_fast_softclip(x),fc,k,filter_gain,!!p[P5_FILTER_REV]);
        }
        #if P5_OVERSAMPLE == 2
        sum >>= 1;
        #endif
        int32_t gain=p5_fast_mulq15(p5_fast_mulq15(ae,amp_velocity),32767+s->amp_drift);
        P5_TAP(4,sum);
        last=p5_fast_voice_amp(p5_fast_mulq15(sum,gain),m,i)<<P5_OUT_SHIFT;
        P5_TAP(5,last);
        out[i]+=last;
    }
    s->last_pcm=last;
}
/* the part after its voices (Melodee 7cd66fb, integer as it is there): the output's coupling capacitor, about 5 Hz
 * (y = x - x' + (1 - 2 pi 5 / FS) y'). The oscillators are unipolar, as the CEM3340's, so the mixer's DC rides through
 * the filter and the VCA as it does in the instrument up to its output stage; without the capacitor it reached the
 * sends and the drive. With no voice it starts afresh */
#define P5_DC_K 1072976913                       /* (1 - 2 pi 5 / FS) << 30 */
_Static_assert(FS == 44100, "P5_DC_K: the sample rate");
/* ChoralRoot, 3y44 (docs/PROPHET.md "The click at a release's end"): "afresh" was the block after the part's last
 * voice ended, whatever the capacitor still held: on a program with a short release that is most of the mixer's DC
 * with its sign turned (the capacitor's time is 32 ms), and it went to 0 in one sample: a step up to 8 dB under the
 * chord's peak, 10 to 130 ms after every release (26 676 of 69 049 on DIRTY LEAD; 34 of the 200 programs above
 * 100). Now the capacitor discharges into the silent blocks (p5_tail keeps the part in the mix:
 * voice.c track_render_x) and starts afresh once it is at rest, P5_DC_REST: 16 output units, 70 dB under a chord.
 * Below that at the last voice's end nothing changes, sample for sample. -DP5_DC_REST=0x7FFFFFFF is 74eae04's
 * output (always at rest: the cut), for a measurement and for the test's proof that it fails without this */
#ifndef P5_DC_REST
#define P5_DC_REST (16 << (7 + P5_OUT_SHIFT))     /* (3y46: 8 units of an output at half the level) */
#endif
static void p5_post(track_t *t,int32_t *out,uint32_t n,uint32_t nr)
{
    p5_part_t *P=p5_part((uint32_t)(t-trk)%NPART);
    if(!P)return;
    int32_t x1=P->dc_x1,y=P->dc_y1;
    if(!nr && y>-P5_DC_REST && y<P5_DC_REST){P->dc_x1=P->dc_y1=0;return;}
    for(uint32_t i=0;i<n;i++){
        int32_t x=out[i];
        P5_TAP(6,x);
        y=(int32_t)((int64_t)(x-x1)*256+(((int64_t)y*P5_DC_K)>>30));
        x1=x;
        out[i]=(y+128)>>8;
        P5_TAP(7,out[i]);
    }
    P->dc_x1=x1;P->dc_y1=y;
}
static uint32_t p5_tail(track_t *t)              /* no voice, and the capacitor not at rest yet (p5_post) */
{
    p5_part_t *P=p5_part((uint32_t)(t-trk)%NPART);
    return P && (P->dc_x1 || P->dc_y1);
}
/* XIP stalls dominated Melodee's first hardware run; its hot path ran from RAM. Here: P5_RAM (above), on in a
 * device build. This function, p5_env_tick, p5_block and p5_post (a voice's and the part's block) are in flash. */
static void p5_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    p5_voice_t *s=p5_voice(t,v);
    p5_part_t *part=p5_part((uint32_t)(t-trk)%NPART);
    const uint8_t *p=p5_patch_of(t)->raw;
    if (!s || !part) return;
    /* A budget victim is already inactive when the common renderer delivers
     * its final fade block. Fade its last PCM sample to zero instead of running
     * an extra complete Prophet voice alongside the replacement track. This
     * keeps the first sample continuous and ends exactly at zero. Engine-switch
     * fades and active native releases continue through the full synth. */
    /* (ChoralRoot, 3y45: but the part's own voice stolen for a waiting note fades as a whole voice, s->steal) */
    if(!v->active && !m->amp1 && !s->steal){
        if(n>1u){
            int32_t step=s->last_pcm/(int32_t)(n-1u);
            for(uint32_t i=0;i<n-1u;i++)out[i]+=s->last_pcm-step*(int32_t)i;
        }
        s->last_pcm=0;return;
    }
    const uint8_t *q=part->value;
    int32_t f0=s->filter_prev, a0=s->amp_prev;
    int curtis=!!p[P5_FILTER_REV];
    int32_t f1=p5_env_tick(&s->filter,v->gate,p,P5_ATTACK_FILTER,P5_DECAY_FILTER,P5_SUSTAIN_FILTER,P5_RELEASE_FILTER,s->env_drift,q,!curtis);
    int32_t a1=p5_env_tick(&s->amp,v->gate,p,P5_ATTACK_AMP,P5_DECAY_AMP,P5_SUSTAIN_AMP,P5_RELEASE_AMP,s->env_drift,q,0);
    s->filter_prev=f1; s->amp_prev=a1;
#if P5_COEF_CACHE
    const p5_coef_t *C=&part->c;
    int32_t glide=0;
    if(p[P5_GLIDE]){
        int32_t target=v->pitch16,d=target-s->glide_pitch;
        int32_t step=C->glide_step;
        s->glide_pitch+=clamp(d,-step,step);glide=s->glide_pitch-v->pitch_cur;
    }else s->glide_pitch=v->pitch16;
    int32_t pitch_a = m->pitch16 + glide + C->coarse_a + C->e2;
    int32_t pitch_b = (p[P5_KEY_B] ? m->pitch16+glide+C->coarse_b : C->pb_free) + C->e3;
    uint32_t ia = cents_inc(pitch_a, s->drift, m->fine) / P5_OVERSAMPLE;
    uint32_t ib = cents_inc(pitch_b, C->fine_b-s->drift, m->fine) / P5_OVERSAMPLE;
    if (p[P5_LOW_B]) ib >>= 7;
    int32_t filter_velocity=p[P5_VEL_FILTER] ? v->mvel*258 : 32767;
    int32_t amp_velocity=p[P5_VEL_AMP] ? v->mvel*258 : 32767;
    int32_t cut=C->cut + m->cutoff + s->filter_drift + (m->pitch16-36*16)*C->kt/128;
    int32_t width_mod=m->shape-(64<<8);
    const p5_render_params_t c={f0,a0,f1,a1,ia,ib,cut,C->k,C->filter_gain,filter_velocity,amp_velocity,C->env_amount,C->pa_env,C->pa_b,C->pf_env,C->pf_b,C->pw_env,C->pw_b,C->pwm_a+width_mod,C->pwm_b+width_mod,C->la,C->lb,C->ln,part->wheel,part->wheel_pitch};
#else
    int32_t glide=0;
    if(p[P5_GLIDE]){
        int32_t target=v->pitch16,d=target-s->glide_pitch;
        int32_t step=(int32_t)(ENV_LIN[q[P5_GLIDE]]>>14);if(step<1)step=1;
        s->glide_pitch+=clamp(d,-step,step);glide=s->glide_pitch-v->pitch_cur;
    }else s->glide_pitch=v->pitch16;
    /* Keyed coarse tuning has semitone detents, with adjacent raw values
     * selecting the same detent (24/25 are both the C1 anchor). Its four
     * octaves end at raw 96. B's keyboard-off knob instead spans nine octaves
     * continuously. Only the rendered pitch is quantized; patch bytes stay exact. */
    int32_t coarse_a=(clamp(q[P5_FREQ_A],0,96)/2-12)*16;
    int32_t coarse_b=(clamp(q[P5_FREQ_B],0,96)/2-12)*16;
    int32_t pitch_a = m->pitch16 + glide + coarse_a + t->p[P_E2]*16;
    int32_t pitch_b = (p[P5_KEY_B] ? m->pitch16+glide+coarse_b : 12*16+q[P5_FREQ_B]*108*16/127) + t->p[P_E3]*16;
    uint32_t ia = cents_inc(pitch_a, s->drift, m->fine) / P5_OVERSAMPLE;
    uint32_t ib = cents_inc(pitch_b, q[P5_FINE_B]*100/128-s->drift, m->fine) / P5_OVERSAMPLE;
    /* Lo Freq drops B seven octaves: Pickle Pincher's keyboard-off B (raw 48,
     * 173 Hz) moves its filter at the demo's 1.35 Hz (ten octaves: 0.17 Hz). */
    if (p[P5_LOW_B]) ib >>= 7;
    int32_t filter_velocity=p[P5_VEL_FILTER] ? v->mvel*258 : 32767;
    int32_t amp_velocity=p[P5_VEL_AMP] ? v->mvel*258 : 32767;
    /* Cutoff in Q8 steps of the coefficient table (14.02 per octave, 30 Hz
     * at 0). Fitted to Rev-4 recordings: the Curtis knob puts the lowest C
     * (MIDI 36) near 870 Hz * 2^((raw-67)/15.2); SSI sits ~0.7 octave higher
     * below the middle of the knob, converging at the top. */
    int32_t c0=q[P5_CUTOFF];
    int32_t cut=1618+c0*236+(curtis?0:(127-c0>66?66:127-c0)*38) + m->cutoff + t->p[P_E0]*256 + s->filter_drift;
    /* Keyboard tracking pivots on the lowest C, where the keyboard CV is 0 V:
     * full tracking doubles Hz per keyboard octave; half tracks sqrt(2). */
    cut += (m->pitch16-36*16) * p5_fast_clamp(p[P5_KEY_FILTER],0,2)*P5_KEYTRACK_HALF_Q7/128;
    if(p[P5_PRESS_FILTER])cut+=t->at*96;
    int32_t resonance=p5_fast_clamp(q[P5_RESONANCE]+t->p[P_E1],0,127);
    int32_t k=resonance*(curtis?19200:18800)/127;
    int32_t filter_gain=32767-resonance*(curtis?100:45);
    int32_t env_amount=q[P5_ENV_FILTER]*165;     /* 0.046 octave per step (Rev-4 recordings) */
    /* Poly-Mod. Oscillator A (Q12 octaves): filter envelope 2 octaves at 96,
     * oscillator B +-1.1 octave at 127. Filter (Q8 steps): 8 octaves at 127. */
    int32_t pa_env=q[P5_POLY_ENV]*85, pa_b=q[P5_POLY_B]*35;
    int32_t pf_env=q[P5_POLY_ENV]*226, pf_b=q[P5_POLY_B]*226;
    int32_t pw_env=q[P5_POLY_ENV]*64, pw_b=q[P5_POLY_B]*258;
    int32_t width_mod=m->shape-(64<<8);
    int32_t pwm_a=q[P5_PW_A]*258+t->p[P_E4]*256+width_mod, pwm_b=q[P5_PW_B]*258+t->p[P_E5]*256+width_mod;
    int32_t la=clamp(q[P5_LEVEL_A]+t->p[P_E6],0,127)*258, lb=clamp(q[P5_LEVEL_B]+t->p[P_E7],0,127)*258, ln=q[P5_NOISE]*258;
    const p5_render_params_t c={f0,a0,f1,a1,ia,ib,cut,k,filter_gain,filter_velocity,amp_velocity,env_amount,pa_env,pa_b,pf_env,pf_b,pw_env,pw_b,pwm_a,pwm_b,la,lb,ln,part->wheel,part->wheel_pitch};
#endif
    /* An indirect call crosses the XIP/RAM distance beyond direct branch range. */
    void (*volatile run)(p5_voice_t *,const uint8_t *,int32_t *,uint32_t,const vmod_t *,const p5_render_params_t *)=p5_samples;
    if(part->sg1 && n<=CTL){
        /* (3y46) a voice of a UNISON stack: its samples by themselves, then into the part at the stack's level
         * (p5_stack_gain: a straight line from the last block's gain to this one's). Here and not in the sample
         * loop: that one stays 8a3074b's but for the level, and a POLY voice pays nothing */
        int32_t tmp[CTL],g0=part->sg0,dg=part->sg1-g0;
        for(uint32_t i=0;i<n;i++)tmp[i]=0;
        run(s,p,tmp,n,m,&c);
        for(uint32_t i=0;i<n;i++)out[i]+=(tmp[i]*(g0+((dg*(int32_t)i)>>CTL_LOG2)))>>12;
        s->last_pcm=(s->last_pcm*part->sg1)>>12;
    }else
    run(s,p,out,n,m,&c);
    if(!v->active)s->steal=0;                         /* (the fade's last block) */
}
/* the stolen voice's fade (above; voice.c voice_alloc, env_tick) */
static void p5_steal(track_t *t,voice_t *v)
{
    p5_voice_t *s=p5_voice(t,v);
    if(s)s->steal=P5_STEAL_BLOCKS;
}
static int32_t p5_fade(track_t *t,voice_t *v)       /* the level this block ends at (Q15); 0: its last block */
{
    p5_voice_t *s=p5_voice(t,v);
    if(!s || s->steal<2u)return 0;
    s->steal--;
    return (int32_t)(32767u*s->steal/(uint32_t)(P5_STEAL_BLOCKS?P5_STEAL_BLOCKS:1));
}

#if FELUCCA_P5_FACTORY
/* the shipped factory program k (0..P5_FACTORY_N-1: the pool's order, assets/prophet5-factory/shipped.txt), unpacked
 * (tools/gen_prophet_factory.py pack; Melodee 1bc4555). Its group and program bytes are Sequential's (the program's
 * number P5_FACTORY_ID[k], 1..200: its identity, p5_patch_id), not its place in the list */
static void p5_factory(uint32_t k,p5_patch_t *p)
{
    const uint8_t *b=P5_FPACK+k*P5_FACTORY_BYTES;
    uint32_t pos=0,id=P5_FACTORY_ID[k]-1u;
    memset(p,0,sizeof *p);
    for(uint32_t i=0;i<P5_RAW_MAX;i++){
        uint32_t v=0;
        for(uint32_t j=0;j<P5_FWIDTH[i];j++,pos++)v|=(uint32_t)(b[pos>>3]>>(pos&7u)&1u)<<j;
        p->raw[i]=(uint8_t)(P5_FBASE[i]+v);
    }
    p->size=P5_RAW_MAX;p->model=0x32;p->command=2;p->group=(uint8_t)(id/40u);p->program=(uint8_t)(id%40u);
}
#endif
/* INIT, then the shipped factory programs in the list's order (preset k is program P5_FACTORY_ID[k-1] of the v1.03
 * bank; 111..588 on the Prophet). Dry, as the Prophet: the sends are the part's here (cr_bank.c
 * param_kept), so a program plays with the part's reverb, chorus and delay as every other engine's sound does. */
#define P5_FACTORY_PRESET(n) {n,{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0),PAT(1)},
static const preset_t P5_TEST_PRESETS[] RES2_PRESET = {   /* (FM-1: the second resource region, cr_res.h) */
    {"INIT P5",{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0),PAT(1)},
#if FELUCCA_P5_FACTORY
    P5_FACTORY_PRESETS(P5_FACTORY_PRESET)
#endif
};
#if FELUCCA_P5_FACTORY
_Static_assert(NELEM(P5_TEST_PRESETS)==P5_FACTORY_N+1u, "Prophet factory presets");
#endif
_Static_assert(NELEM(P5_TEST_PRESETS)<=255u, "engine_t.npresets and track_t.preset are bytes");
/* A PROGRAM'S IDENTITY: its Sequential program number 1..200 (a program dump's group and program bytes, which every
 * factory program carries in its own 138 bytes and an edit does not touch); 0: none (INIT, an edit-buffer dump).
 * It does not depend on the shipped list, so a record that holds the program (the session's blob, cr_settings.c)
 * names the same sound in a build with another list. p5_preset_of: the preset (= the pool's position) of identity
 * id in THIS build, 0: not shipped here (or no identity) */
static uint32_t p5_patch_id(const p5_patch_t *p)
{
    return p->command==2u && p->group<5u && p->program<40u ? (uint32_t)p->group*40u+p->program+1u : 0u;
}
static uint32_t p5_preset_of(uint32_t id)
{
#if FELUCCA_P5_FACTORY
    for(uint32_t k=0;id && k<P5_FACTORY_N;k++)if(P5_FACTORY_ID[k]==id)return k+1u;
#else
    (void)id;
#endif
    return 0u;
}
static uint32_t p5_preset_id(uint32_t pi)        /* preset pi's identity (0: INIT) */
{
#if FELUCCA_P5_FACTORY
    if(pi && pi<=P5_FACTORY_N)return P5_FACTORY_ID[pi-1u];
#else
    (void)pi;
#endif
    return 0u;
}
/* A factory preset's native program; preset 0 is INIT. */
static void p5_preset_patch(uint32_t pi,p5_patch_t *p)
{
#if FELUCCA_P5_FACTORY
    if(pi && pi<=P5_FACTORY_N){p5_factory(pi-1u,p);return;}
#else
    (void)pi;
#endif
    p5_patch_init(p);
}
/* a whole program into part t (the main loop; the audio side reads the bytes a block at a time: a note that sounds
 * across the write plays a block of mixed values at worst, and every load path releases the part's notes first) */
static void p5_patch_put(track_t *t,const p5_patch_t *in)
{
    uint32_t k=(uint32_t)(t-trk)%NPART;
    p5_patch[k]=*in;p5_ready[k]=1;
    RING_PUBLISH();
}
/* ----------------------------------------------------- ChoralRoot: the program of a part --- */
/* A user slot's program (0.22, the sound pool: upool.c): p5_user_pending = the slot + 1 being loaded (upreset.c
 * up_values), p5_store_read its 138 bytes (0 = there is one) */
static uint8_t p5_user_pending;
static int (*p5_store_read)(uint32_t k, uint8_t *blob);
/* a sound load put P5 into track t (eng_fm6.c fm6_track_loaded calls it on every load path: a factory preset,
 * a user slot, an engine change, INIT, the picker): a user slot's own program (p5_user_pending: the whole program as
 * it was saved, docs/PROPHET.md "Saving"), else the factory program t->preset names (0 and INIT: INIT; a user slot
 * without a program: INIT). The eight macros are offsets on the program (CUT RES TUNA TUNB PWA PWB MIXA MIXB), 0 in
 * every preset. The program's UNISON switch makes the part UNISON (above); a program without it leaves the mode the
 * caller set */
static void p5_track_loaded(track_t *t)
{
    uint32_t tr=(uint32_t)(t-trk),pend=p5_user_pending;
    p5_patch_t p;
    p5_user_pending=0;
    if(tr>=NTRK || t->eng_req!=ENGI_PROPHET)return;
    if(!pend || !p5_store_read || p5_store_read(pend-1u,(uint8_t *)&p) || !p5_patch_valid(&p))
        p5_preset_patch(pend?0u:t->preset,&p);
    p5_patch_put(t,&p);
    p5_own[tr]=(uint8_t)(t->p[P_VOICE]==V_UNISON?(tr==P5_PART_BASS?V_MONO:V_POLY):t->p[P_VOICE]);
    p5_track_accept(t);
    if(p.raw[P5_UNISON])t->p[P_VOICE]=V_UNISON;
}
/* a record's program came back over a load (cr_settings.c css_part_put, cr_ui.c cu_pick_end: after blob_set): the
 * part's voice mode is the one the record held (the load above took the factory program's switch, and the record
 * may hold another: UNISON turned off in the editor, a mode set on the MIX page) */
static void p5_voice_restore(track_t *t,int32_t mode)
{
    t->p[P_VOICE]=(int16_t)mode;
}
/* the blob: p5_patch_t as it is (138 bytes: the 133 raw bytes, then size, model, command, group, program) */
static void p5_blob_get(const track_t *t,uint8_t *out)
{
    memcpy(out,p5_patch_of(t),sizeof(p5_patch_t));
}
static void p5_blob_set(track_t *t,const uint8_t *in)             /* 0 or a bad blob: INIT */
{
    p5_patch_t p;
    if(in)memcpy(&p,in,sizeof p);
    if(!in || !p5_patch_valid(&p))p5_patch_init(&p);
    p5_patch_put(t,&p);
    p5_track_accept(t);
}
static void p5_blob_preset(track_t *t,uint32_t k)                 /* factory preset k: its program */
{
    p5_patch_t p;
    if(k>=NELEM(P5_TEST_PRESETS))return;
    p5_preset_patch(k,&p);
    p5_patch_put(t,&p);
    p5_track_accept(t);
}

/* ------------------------------------------------- ChoralRoot: deep pages --- */
/* The sound editor's sections (cr_edit.c builds its screens from these titles, docs/EDITOR.md, docs/PROPHET.md); a
 * column is one raw byte of the program (P5_DKEY), named and ranged as Melodee's panel (prophet_edit.h P5_PANEL):
 *   OSC     OSC A, OSC A+        FREQ SAW PULSE WIDTH / SYNC                 (two lanes a screen)
 *           OSC B, OSC B+        FREQ FINE SAW TRI / PULSE WIDTH LOFREQ KEYBD
 *           MIXER                MIX A, MIX B, NOISE
 *   FILTER  FILTER, FILTER+      CUT RES FENV KTRK / REV AFTER               (under the filter's curve; REV 1/2: the
 *                                                                              SSI model, 3: the Curtis)
 *   ENV     ENV 1, ENV 1+        ATK DEC SUS REL / VEL RELSW                 (the amplifier's; RELSW: the RELEASE switch,
 *           ENV 2, ENV 2+        ATK DEC SUS REL / VEL                        both envelopes'; ENV 2: the filter's)
 *   LFO     LFO, LFO+            RATE AMOUNT NOISE AFTER / SAW TRI SQUARE    (the wheel's source: INITIAL amount, the
 *           WHEEL, WHEEL+        FREQ A, FREQ B, PW A, PW B / FILTER          noise mix; its five destinations)
 *   MOD     POLYMOD, POLYMOD+    ENV, OSC B, FREQ A, PW A / FILTER           (the two sources, the three destinations)
 *           VOICE, VOICE+        UNISON VOICES DETUNE GLIDE / PRIO VINTAGE
 * All 56 controls that change the sound; the 57th, the program's BEND range, is not shown (the part bends by the
 * channel's range, above). get: the byte; set: the byte, clamped to the column's range (the other 132 stay).
 * No matrix destination (mod_dst -1): the platform's routes act on the pitch, the cutoff, the widths and the level. */
static const char *const N_P5_REV[]={"1/2","3"};      /* the panel's FILTER REV switch: 1/2 the SSI model, 3 the Curtis */
static const char *const N_P5_TRACK[]={"OFF","HALF","FULL"};
static const char *const N_P5_PRIO[]={"LOW","LOW R","LAST","LAS R"};   /* low / last note, R: it retriggers (Melodee's LOW RET,
                                                                         * LAST RET: a cell shows five characters) */
#define P5D_K(l) {l,F_INT,0,127,0,0,0}                 /* a knob: the stored 0..127 */
#define P5D_S(l) {l,F_ONOFF,0,1,0,0,0}                 /* a switch */
#define P5D_NONE {0,0,0,0,0,0,0}
static const eng_page_t P5_PAGES[] = {
    {"OSC A",{P5D_K("FREQ"),P5D_S("SAW"),P5D_S("PULSE"),P5D_K("WIDTH")}},                              /* 0 OSC */
    {"OSC A+",{P5D_S("SYNC"),P5D_NONE,P5D_NONE,P5D_NONE}},
    {"OSC B",{P5D_K("FREQ"),P5D_K("FINE"),P5D_S("SAW"),P5D_S("TRI")}},
    {"OSC B+",{P5D_S("PULSE"),P5D_K("WIDTH"),P5D_S("LOFREQ"),P5D_S("KEYBD")}},
    {"MIXER",{P5D_K("MIX A"),P5D_K("MIX B"),P5D_K("NOISE"),P5D_NONE}},
    {"FILTER",{P5D_K("CUT"),P5D_K("RES"),P5D_K("FENV"),{"KTRK",F_ENUM,0,2,0,N_P5_TRACK,0}}},           /* 5 FILTER */
    {"FILTER+",{{"REV",F_ENUM,0,1,1,N_P5_REV,0},P5D_S("AFTER"),P5D_NONE,P5D_NONE}},
    {"ENV 1",{P5D_K("ATK"),P5D_K("DEC"),P5D_K("SUS"),P5D_K("REL")}},                                   /* 7 ENV */
    {"ENV 1+",{P5D_S("VEL"),P5D_S("RELSW"),P5D_NONE,P5D_NONE}},
    {"ENV 2",{P5D_K("ATK"),P5D_K("DEC"),P5D_K("SUS"),P5D_K("REL")}},
    {"ENV 2+",{P5D_S("VEL"),P5D_NONE,P5D_NONE,P5D_NONE}},
    {"LFO",{P5D_K("RATE"),P5D_K("AMOUNT"),P5D_K("NOISE"),P5D_S("AFTER")}},                             /* 11 LFO */
    {"LFO+",{P5D_S("SAW"),P5D_S("TRI"),P5D_S("SQUARE"),P5D_NONE}},
    {"WHEEL",{P5D_S("FREQ A"),P5D_S("FREQ B"),P5D_S("PW A"),P5D_S("PW B")}},
    {"WHEEL+",{P5D_S("FILTER"),P5D_NONE,P5D_NONE,P5D_NONE}},
    {"POLYMOD",{P5D_K("ENV"),P5D_K("OSC B"),P5D_S("FREQ A"),P5D_S("PW A")}},                           /* 15 MOD */
    {"POLYMOD+",{P5D_S("FILTER"),P5D_NONE,P5D_NONE,P5D_NONE}},
    {"VOICE",{P5D_S("UNISON"),{"VOICES",F_INT,1,(int16_t)P5_STACK,(int16_t)P5_STACK,0,0},{"DETUNE",F_INT,0,7,0,0,0},P5D_K("GLIDE")}},
    {"VOICE+",{{"PRIO",F_ENUM,0,3,0,N_P5_PRIO,0},P5D_K("VINTAGE"),P5D_NONE,P5D_NONE}},
};
#undef P5D_K
#undef P5D_S
#undef P5D_NONE
#define P5_NK 0xFFu                                    /* no column */
static const uint8_t P5_DKEY[][4] = {
    {P5_FREQ_A,P5_SAW_A,P5_PULSE_A,P5_PW_A},
    {P5_SYNC,P5_NK,P5_NK,P5_NK},
    {P5_FREQ_B,P5_FINE_B,P5_SAW_B,P5_TRI_B},
    {P5_PULSE_B,P5_PW_B,P5_LOW_B,P5_KEY_B},
    {P5_LEVEL_A,P5_LEVEL_B,P5_NOISE,P5_NK},
    {P5_CUTOFF,P5_RESONANCE,P5_ENV_FILTER,P5_KEY_FILTER},
    {P5_FILTER_REV,P5_PRESS_FILTER,P5_NK,P5_NK},
    {P5_ATTACK_AMP,P5_DECAY_AMP,P5_SUSTAIN_AMP,P5_RELEASE_AMP},
    {P5_VEL_AMP,P5_RELEASE_ON,P5_NK,P5_NK},
    {P5_ATTACK_FILTER,P5_DECAY_FILTER,P5_SUSTAIN_FILTER,P5_RELEASE_FILTER},
    {P5_VEL_FILTER,P5_NK,P5_NK,P5_NK},
    {P5_LFO_RATE,P5_LFO_INITIAL,P5_WHEEL_MIX,P5_PRESS_LFO},
    {P5_LFO_SAW,P5_LFO_TRI,P5_LFO_PULSE,P5_NK},
    {P5_WHEEL_FREQ_A,P5_WHEEL_FREQ_B,P5_WHEEL_PW_A,P5_WHEEL_PW_B},
    {P5_WHEEL_FILTER,P5_NK,P5_NK,P5_NK},
    {P5_POLY_ENV,P5_POLY_B,P5_POLY_FREQ,P5_POLY_PW},
    {P5_POLY_FILTER,P5_NK,P5_NK,P5_NK},
    {P5_UNISON,P5_UNISON_COUNT,P5_UNISON_DETUNE,P5_GLIDE},
    {P5_RETRIGGER,P5_VINTAGE,P5_NK,P5_NK},
};
enum { P5_PG_OSC = 0, P5_PG_FILTER = 5, P5_PG_ENV = 7, P5_PG_LFO = 11, P5_PG_MOD = 15 };
_Static_assert(NELEM(P5_PAGES)==19u && NELEM(P5_DKEY)==NELEM(P5_PAGES), "PROPHET pages");
static int32_t p5_dref(uint32_t page,uint32_t col)                /* page column -> its raw byte, -1 none */
{
    if(page>=NELEM(P5_PAGES) || col>=4u || !P5_PAGES[page].col[col].label || P5_DKEY[page][col]==P5_NK)return -1;
    return P5_DKEY[page][col];
}
/* get: the value the engine plays, in the column's range (a switch byte other than 0: on; VOICES 0, as the factory
 * programs have it: all P5_STACK, p5_stack; a stored 5: the four there are); the byte itself stays as it is until the column is turned */
static int32_t p5_dget(const track_t *t,uint32_t page,uint32_t col)
{
    int32_t id=p5_dref(page,col),v;
    const param_desc_t *d;
    if(id<0)return 0;
    d=&P5_PAGES[page].col[col];
    v=p5_patch_of(t)->raw[id];
    if(d->fmt==F_ONOFF)return v!=0;
    if(id==P5_UNISON_COUNT && !v)return (int32_t)P5_STACK;
    return clamp(v,d->min,d->max);
}
static void p5_dset(track_t *t,uint32_t page,uint32_t col,int32_t v)
{
    int32_t id=p5_dref(page,col);
    const param_desc_t *d;
    if(id<0)return;
    d=&P5_PAGES[page].col[col];
    p5_patch_of(t)->raw[id]=(uint8_t)clamp(v,d->min,d->max);
    RING_PUBLISH();
    if(id==P5_UNISON_DETUNE || id==P5_RETRIGGER)p5_track_accept(t);
    if(id==P5_UNISON)p5_voice_mode(t);
}
static int32_t p5_mod_dst(const track_t *t,uint32_t page,uint32_t col)   /* no matrix destination of its own */
{
    (void)t;(void)page;(void)col;
    return -1;
}
static const eng_deep_t P5_DEEP={
    .npages=NELEM(P5_PAGES),.pages=P5_PAGES,
    .section={P5_PG_OSC,P5_PG_FILTER,P5_PG_ENV,P5_PG_LFO,P5_PG_MOD,0xFF,0xFF,0xFF},
    .get=p5_dget,.set=p5_dset,
    .blob_size=sizeof(p5_patch_t),.blob_get=p5_blob_get,.blob_set=p5_blob_set,.blob_preset=p5_blob_preset,
    .mod_dst=p5_mod_dst,
};
_Static_assert(sizeof(p5_patch_t)==138u && sizeof(p5_patch_t)<=ENG_BLOB_MAX,"the Prophet program fits the blob cap");

/* The budget. ONE CORE: THREE of the sixteen units a voice (as Melodee's), so that the chord part KEEPS its four
 * voices when a bass of another engine sounds: 4 x 3 + a VA or FM6 bass's 2 = 14 of 16 (c176dd4's four units left it
 * three). TWO CORES: each part has its own sixteen (voice.c VB_SAME), and the chord part's are eight voices: TWO
 * units. How many P5 voices the two parts may have TOGETHER is not the units' to say but the pool's (voice.c
 * p5_pool_*; "THE VOICES" above) */
static uint32_t p5_units(const track_t *t) { (void)t; return cr_dualcore_active() ? 2u : 3u; }
static const engine_t ENG_P5_TEST = {
    .name="P5", .page_title={"FILTER/TUNE","PW/MIX"},
    .edit={{"CUT",F_INT,-63,63,0,0,0},{"RES",F_INT,-63,63,0,0,0},
           {"TUNA",F_SEMI,-24,24,0,0,0},{"TUNB",F_SEMI,-24,24,0,0,0},
           {"PWA",F_INT,-63,63,0,0,0},{"PWB",F_INT,-63,63,0,0,0},
           {"MIXA",F_INT,-63,63,0,0,0},{"MIXB",F_INT,-63,63,0,0,0}},
    .presets=P5_TEST_PRESETS,.npresets=NELEM(P5_TEST_PRESETS),.poly=P5_CORE,.ownenv=1,.deep=&P5_DEEP,
    .note_on=p5_note_on,.render=p5_render,.done=p5_done,.block=p5_block,.units=p5_units,.cap=p5_cap,.legato=p5_legato,
#if P5_DC_BLOCK
    .post=p5_post,.tail=p5_tail,
#endif
#if P5_STEAL_BLOCKS
    .steal=p5_steal,.fade=p5_fade,
#endif
    .split=P5_VOICE_US/10u,
    .knob={P_E0,P_E1,P_CHOR,P_REV}
};
