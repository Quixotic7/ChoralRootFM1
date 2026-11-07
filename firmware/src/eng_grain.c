/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); the grain player after Felucca's GRAIN,
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GRAIN: a granular engine over material it renders itself (docs/GRAIN.md; ChoralRoot, FELUCCA_GRAIN; engine 8).
 *
 * The material: two buffers of GS_N samples at GS_RATE (grain_src.c: 743 ms at 22.05 kHz, 32 KB each, POOL), the
 * sound's sources A and B, each rendered from four knobs (TYPE TONE MOTION SEED) by the generator, deterministic.
 * The sound stores the knobs, never audio. The buffers are one pair for the unit: the first part on GRAIN (the chord
 * part when it is) owns them, its source knobs render them; a second part on GRAIN plays the same two with its own
 * grain knobs and MORPH.
 *
 * The player (Felucca's eng_grain.c over RAM: no ADPCM decoder, no seek index): a pool of GP_NG grains a part, shared
 * by its voices (a voice's share GP_NG / sounding voices, at least 2), at most GP_POLY voices. A grain: a position (it
 * reads A and B there, mixed by MORPH as it fetches: the voice's MORPH, ramped a fetch from block to block; 3y167), a
 * length (SIZE, 10 .. 393 ms), a rate (the note against A's root, PITCH, a random detune), a direction, a Hann window
 * (from the sine table), a level 1 / sqrt(overlap). A voice starts its grains DENS times a second (1 .. 98 /s; SPRD
 * jitters the interval); POS (+ SCAN: a ramp over the buffer, forward or in reverse, a round in 110 s .. 0.5 s) is
 * the centre, SPRD the random spread round it. RAND: the random detune (quadratic, up to +-1 octave) and the share of
 * reversed grains (up to 50 %). The VA's filter a voice (eng_va.c: the SVF with FTYPE's LP BP HP NOTCH cycle, CUT RES
 * DRIVE KTRK), the platform's ADSR the amplitude (ENV 1), ENV 2 a modulation envelope a voice, two LFOs a part (the
 * VA's: RATE WAVE DEPTH FADE, SYNC to the tempo on the VA's list), and an 8-slot matrix (SRC: ENV1 ENV2 LFO1 LFO2 VEL
 * KEY RAND MODW; DST: MORPH POS SIZE DENS SPRD RAND PITCH SCAN CUT RES FTYPE DRIVE RATE1 RATE2 DEP1 DEP2 AMP; AMT
 * whole or in tenths, as the VA's) evaluated once a block a voice: the grain knobs it moves are read at each grain's
 * start, the filter's at the block. The platform's SHP moves MORPH, its FLT CUT.
 *
 * The render (docs/GRAIN.md section 1): grain_tick, once a block on core 0 (fx.c mix_block, before the parts render),
 * compares the owner's source knobs with what the buffers hold and posts a request; the main loop's grain_poll (main.c's
 * idle loop, the emulator's idle) renders in place, a slice a call (GRAIN_SLICE_N by TYPE: under 0.8 ms on the unit),
 * in two passes (measuring the finish's gain, then writing with it), over the old material while the grains play on;
 * the head's crossfade and a grain's ramped step soften what a grain hears as the material changes under it; latest
 * wins at a render's end (3y168 .. 3y169: docs/GRAIN.md "The third phase"). GRAIN_ASYNC 0 (hostsim, regress,
 * the tests: no main loop): the tick renders synchronously, whole.
 *
 * The editor: a screen plan for OSC (SRC 1 / SRC 2 and GRAIN / GRAIN+ under the wave band: the buffers' envelopes, POS
 * and SPRD, the grains moving over them: cr_draw.c CR_W_WAVE through grain_wave), the title rules for the rest.
 *
 * The SVF's type weights (va_fweights, va_svf), the LFO sync list (N_VA_SYNC, VA_SYNC_Q8), the fine amounts' knob
 * rule (va_amt_q, VA_AMX) and the synced RATE's descriptor (VA_D_SRATE) are the VA's, in va_common.c (every build).
 *
 * State: the patch (grain_patch, GQ_NP values a part), the macros as last seen, the pool and the LFOs (gp_part_t,
 * eng_state's union), the scheduler (grs); per voice s[0] the countdown to the next grain, s[1] MORPH as last played
 * (Q15: the next block ramps from it), s[2..3] the filter, s[4..5] ENV 2 (Q24, its stage), s[6] the control ticks
 * since the note (the LFOs' fade), s[7] the amplitude gain as last applied (the matrix's AMP). */
#include "grain_src.c"
#include "va_common.c"          /* the VA's filter weights, sync list and amount rule (guarded: engines.c has it too) */
/* (eng_fm6.c, before this file, calls grain_track_loaded through its forward declaration) */
#ifndef ENGI_GRAIN
#define ENGI_GRAIN 8u
#endif
#ifndef GRAIN_ASYNC
#define GRAIN_ASYNC 0            /* 1: the renders in slices from the main loop (grain_poll: the unit, the emulator);
                                  * 0: a render completes inside the block that asks for it (a host without a main loop) */
#endif
#define GRAIN_NPART 2u           /* parts with a GRAIN patch (ChoralRoot: chord, bass) */
#define GP_NG 16                 /* grains a part */
#define GP_POLY 8                /* engine_t.poly */
#define GP_STEP_MAX (2u << 16)   /* the rate's cap: two source samples an output sample */
#define GP_RATE_Q16 ((GS_RATE << 16) / 44100u)
#ifndef GRAIN_DECLICK
#define GRAIN_DECLICK 1          /* the render in place's two softeners (0: off, tools/grain_listen.c's comparison) */
#endif
#define GRAIN_RAMP 64u           /* the write head's crossfade: the slice's last samples fade from the new material into the
                                  * old (a grain reading across the head), rewritten whole by the next slice */
#define GP_DECLICK 4u            /* blocks a grain's step (its two held samples rewritten under it) ramps out over */
#define GRAIN_BLOB 64u           /* 'G', version, GQ_NP values, zeros */
/* GRAIN_BLOB_V1 20 (core.h): version 1 (3y136's first build): the first 18 values */
#define GRAIN_MAGIC 0x47u
#define GRAIN_VER 2u             /* 2: the filter's FTYPE DRIVE KTRK, ENV 2, two LFOs with SYNC, the matrix and its fine
                                  * flags, SCAN bipolar (version 1's 0 .. 127 forward triangle: halved, forward) */

/* --------------------------------------------------------- the patch --- */
enum { GL_RATE, GL_WAVE, GL_DEPTH, GL_FADE, GL_SYNC, GL_N };                                /* per LFO */
enum { GM_SRC, GM_DST, GM_AMT, GM_N };                                                      /* per matrix slot */
enum { GQ_TYPE1, GQ_TONE1, GQ_MOTION1, GQ_SEED1, GQ_TYPE2, GQ_TONE2, GQ_MOTION2, GQ_SEED2,   /* 0..7 SRC 1, SRC 2 */
       GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD,                                                    /* 8..11 GRAIN */
       GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN,                                                 /* 12..15 GRAIN+ */
       GQ_CUT, GQ_RES,                                                                       /* 16, 17 FILTER (version 1 ends) */
       GQ_FTYPE, GQ_DRIVE, GQ_KTRK,                                                          /* 18..20 FILTER, FILTER+ */
       GQ_E2ATK, GQ_E2DEC, GQ_E2SUS, GQ_E2REL,                                               /* 21..24 ENV 2 */
       GQ_LFO0,                                                                              /* 25..34 LFO 1, LFO 2 */
       GQ_MOD0 = GQ_LFO0 + 2 * GL_N,                                                         /* 35..58 MOD 1..8 */
       GQ_MFINE0 = GQ_MOD0 + 8 * GM_N, GQ_MFINE1,                                            /* 59, 60: the fine flags */
       GQ_NP };
#define GQ_NP_V1 18u
#define GQ_LFO(l, f) (GQ_LFO0 + (l) * GL_N + (f))
#define GQ_MOD(k, f) (GQ_MOD0 + (k) * GM_N + (f))
_Static_assert(GQ_NP == 61 && GQ_NP + 2u <= GRAIN_BLOB && GRAIN_BLOB <= ENG_BLOB_MAX, "GRAIN patch layout");
enum { GSR_OFF, GSR_ENV1, GSR_ENV2, GSR_LFO1, GSR_LFO2, GSR_VEL, GSR_KEY, GSR_RAND, GSR_MODW, GSR_N };   /* the matrix's sources */
enum { GD_OFF, GD_MORPH, GD_POS, GD_SIZE, GD_DENS, GD_SPRD, GD_RAND, GD_PITCH, GD_SCAN, GD_CUT, GD_RES, GD_FTYPE,
       GD_DRIVE, GD_RATE1, GD_RATE2, GD_DEP1, GD_DEP2, GD_AMP, GD_N };                        /* .. destinations */
static const char *const N_GRAIN_SRC[GSR_N] = {"OFF", "ENV1", "ENV2", "LFO1", "LFO2", "VEL", "KEY", "RAND", "MODW"};
static const char *const N_GRAIN_DST[GD_N] = {"OFF", "MORPH", "POS", "SIZE", "DENS", "SPRD", "RAND", "PITCH", "SCAN",
                                             "CUT", "RES", "FTYPE", "DRIVE", "RATE1", "RATE2", "DEP1", "DEP2", "AMP"};
/* the macros P_E0..P_E7: source A's four, then MORPH SIZE DENS POS */
static const uint8_t GRAIN_MAC[8] = {GQ_TYPE1, GQ_TONE1, GQ_MOTION1, GQ_SEED1, GQ_MORPH, GQ_SIZE, GQ_DENS, GQ_POS};
static int8_t grain_patch[GRAIN_NPART][GQ_NP];
static int16_t grain_mlast[GRAIN_NPART][8];      /* P_E0..P_E7 as last put in the patch */
static uint8_t grain_user_pending;               /* upool.c up_loading: a GRAIN user slot + 1 is being loaded */
static int (*grain_store_read)(uint32_t k, uint8_t *b);   /* upool.c: slot k's GRAIN patch, 0 = there is one */

/* a value's range and default */
typedef struct { int16_t min, max, def; } grain_rng_t;
static grain_rng_t grain_range(uint32_t i)
{
    grain_rng_t r = {0, 127, 0};
    if (i >= GQ_MOD0 && i < GQ_MFINE0) {
        uint32_t f = (i - GQ_MOD0) % GM_N;
        if (f == GM_SRC) r.max = GSR_N - 1;
        else if (f == GM_DST) r.max = GD_N - 1;
        else { r.min = -64; r.max = 63; }
        return r;
    }
    if (i >= GQ_LFO0 && i < GQ_MOD0) {
        uint32_t f = (i - GQ_LFO0) % GL_N;
        if (f == GL_RATE) r.def = 70;
        else if (f == GL_WAVE) r.max = 4;
        else if (f == GL_DEPTH) r.def = 127;
        else if (f == GL_SYNC) r.max = 1;
        return r;
    }
    switch (i) {
    case GQ_TYPE1: r.max = GS_NTYPE - 1; r.def = GT_DRONE; break;
    case GQ_TYPE2: r.max = GS_NTYPE - 1; r.def = GT_BELL; break;
    case GQ_TONE1: case GQ_TONE2: case GQ_MOTION1: case GQ_MOTION2: r.def = 64; break;
    case GQ_SIZE: r.def = 92; break;
    case GQ_DENS: r.def = 88; break;
    case GQ_POS: r.def = 50; break;
    case GQ_SPRD: r.def = 40; break;
    case GQ_RAND: r.def = 14; break;
    case GQ_PITCH: r.min = -24; r.max = 24; break;
    case GQ_SCAN: r.min = -64; r.max = 63; r.def = 12; break;
    case GQ_CUT: r.def = 100; break;
    case GQ_RES: r.def = 20; break;
    case GQ_KTRK: r.def = 64; break;
    case GQ_E2DEC: r.def = 64; break;
    case GQ_E2SUS: r.def = 127; break;
    case GQ_E2REL: r.def = 40; break;
    default: break;                              /* SEED, MORPH, FTYPE (LP), DRIVE, E2ATK, the fine flags: 0 */
    }
    return r;
}
static inline int8_t grain_clampv(uint32_t i, int32_t v)
{
    grain_rng_t r = grain_range(i);
    return (int8_t)clamp(v, r.min, r.max);
}
static uint32_t grain_tr(const track_t *t) { return (uint32_t)(t - trk); }
static void grain_init_patch(int8_t *p)
{
    uint32_t i;
    for (i = 0; i < GQ_NP; i++)
        p[i] = (int8_t)grain_range(i).def;
}
/* the matrix's fine amounts, as the VA's (eng_va.c va_mfine): slot k's AMT is whole (-64 .. 63) or in tenths */
static uint32_t grain_mfine(const int8_t *p, uint32_t k)
{
    return k < 7u ? ((uint32_t)p[GQ_MFINE0] >> k) & 1u : (uint32_t)p[GQ_MFINE1] & 1u;
}
static void grain_mfine_put(int8_t *p, uint32_t k, uint32_t on)
{
    int8_t *f = &p[k < 7u ? GQ_MFINE0 : GQ_MFINE1];
    uint32_t b = k < 7u ? 1u << k : 1u;
    *f = (int8_t)(on ? (uint32_t)*f | b : (uint32_t)*f & ~b);
}
#define GRAIN_AMX(x, mf, k) VA_AMX(x, mf, k)

/* the deep pages' columns (the UI contract, docs/GRAIN.md section 3) */
#define GC_TYPE(d) {"TYPE", F_ENUM, 0, GS_NTYPE - 1, d, GS_TYPE_NAMES, 0}
#define GC_TONE {"TONE", F_PCT, 0, 127, 64, 0, 0}
#define GC_MOTION {"MOTION", F_PCT, 0, 127, 64, 0, 0}
#define GC_SEED {"SEED", F_INT, 0, 127, 0, 0, 0}
#define GC_SIZE {"SIZE", F_PCT, 0, 127, 92, 0, 0}
#define GC_DENS {"DENS", F_PCT, 0, 127, 88, 0, 0}
#define GC_POS {"POS", F_PCT, 0, 127, 50, 0, 0}
#define GC_SPRD {"SPRD", F_PCT, 0, 127, 40, 0, 0}
#define GC_MORPH {"MORPH", F_PCT, 0, 127, 0, 0, 0}
#define GC_RAND {"RAND", F_PCT, 0, 127, 14, 0, 0}
#define GC_PITCH {"PITCH", F_SEMI, -24, 24, 0, 0, 0}
#define GC_SCAN {"SCAN", F_BIPCT, -64, 63, 12, 0, 0}
#define GC_CUT {"CUT", F_CUTOFF, 0, 127, 100, 0, 0}
#define GC_RES {"RES", F_PCT, 0, 127, 20, 0, 0}
#define GC_FTYPE {"FTYPE", F_INT, 0, 127, 0, N_VA_FTYPE128, 0}
#define GC_DRIVE {"DRIVE", F_PCT, 0, 127, 0, 0, 0}
#define GC_KTRK {"KTRK", F_PCT, 0, 127, 64, 0, 0}
#define GC_E2ATK {"ATK", F_TIME, 0, 127, 0, 0, 0}
#define GC_E2DEC {"DEC", F_TIME, 0, 127, 64, 0, 0}
#define GC_E2SUS {"SUS", F_PCT, 0, 127, 127, 0, 0}
#define GC_E2REL {"REL", F_TIME, 0, 127, 40, 0, 0}
#define GC_LRATE {"RATE", F_LFOHZ, 0, 127, 70, 0, 0}
#define GC_LWAVE {"WAVE", F_ENUM, 0, 4, 0, N_VA_LWAVE, 0}
#define GC_LDEPTH {"DEPTH", F_PCT, 0, 127, 127, 0, 0}
#define GC_LFADE {"FADE", F_TIME, 0, 127, 0, 0, 0}
#define GC_LSYNC(l) {l, F_ONOFF, 0, 1, 0, 0, 0}
#define GC_SRC {"SRC", F_ENUM, 0, GSR_N - 1, 0, N_GRAIN_SRC, 0}
#define GC_DST {"DST", F_ENUM, 0, GD_N - 1, 0, N_GRAIN_DST, 0}
#define GC_AMT {"AMT", F_FIX2, -6400, 6300, 0, 0, 0}   /* hundredths: tenths below 6, whole from 6 (the VA's rule) */
#define GC_NONE {0, 0, 0, 0, 0, 0, 0}
/* the platform's envelope as a page of the engine (ENV 1: the track's P_ATK .. P_REL, as params.c TP[] describes
 * them) */
#define GC_ATK {"ATK", F_TIME, 0, 127, 10, 0, 0}
#define GC_DEC {"DEC", F_TIME, 0, 127, 70, 0, 0}
#define GC_SUS {"SUS", F_PCT, 0, 127, 90, 0, 0}
#define GC_REL {"REL", F_TIME, 0, 127, 60, 0, 0}
enum { GPG_SRC1, GPG_SRC2, GPG_GRAIN, GPG_GRAINP, GPG_FILTER, GPG_FILTERP, GPG_ENV1, GPG_ENV2, GPG_LFO1, GPG_LFO2,
       GPG_LFOSYN, GPG_MOD0, GPG_N = GPG_MOD0 + 8 };
static const eng_page_t GRAIN_PAGES[GPG_N] = {
    {"SRC 1", {GC_TYPE(GT_DRONE), GC_TONE, GC_MOTION, GC_SEED}},   /* OSC (the plan: two screens under the wave band) */
    {"SRC 2", {GC_TYPE(GT_BELL), GC_TONE, GC_MOTION, GC_SEED}},
    {"GRAIN", {GC_SIZE, GC_DENS, GC_POS, GC_SPRD}},
    {"GRAIN+", {GC_MORPH, GC_RAND, GC_PITCH, GC_SCAN}},
    {"FILTER", {GC_CUT, GC_RES, GC_FTYPE, GC_DRIVE}},              /* FILTER: the curve (the title rules) */
    {"FILTER+", {GC_KTRK, GC_NONE, GC_NONE, GC_NONE}},
    {"ENV 1", {GC_ATK, GC_DEC, GC_SUS, GC_REL}},                   /* ENV: the platform's (the amplitude), ENV 2 (mod) */
    {"ENV 2", {GC_E2ATK, GC_E2DEC, GC_E2SUS, GC_E2REL}},
    {"LFO 1", {GC_LRATE, GC_LWAVE, GC_LDEPTH, GC_LFADE}},          /* LFO: a stack of two, the SYNC stack */
    {"LFO 2", {GC_LRATE, GC_LWAVE, GC_LDEPTH, GC_LFADE}},
    {"LFO SYN", {GC_LSYNC("SYNC1"), GC_LSYNC("SYNC2"), GC_NONE, GC_NONE}},
    {"MOD 1", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},                  /* MOD: the slots */
    {"MOD 2", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
    {"MOD 3", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
    {"MOD 4", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
    {"MOD 5", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
    {"MOD 6", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
    {"MOD 7", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
    {"MOD 8", {GC_SRC, GC_DST, GC_AMT, GC_NONE}},
};
#define GRAIN_NPAGES ((uint32_t)NELEM(GRAIN_PAGES))
#define GQ_X 0xFFu               /* an empty column */
#define GQ_T(id) (0x80u | (id))  /* a track parameter */
#define GRAIN_MPG(k) {GQ_MOD(k, GM_SRC), GQ_MOD(k, GM_DST), GQ_MOD(k, GM_AMT), GQ_X}
static const uint8_t GRAIN_MAP[GPG_N][4] = {
    {GQ_TYPE1, GQ_TONE1, GQ_MOTION1, GQ_SEED1},
    {GQ_TYPE2, GQ_TONE2, GQ_MOTION2, GQ_SEED2},
    {GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD},
    {GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN},
    {GQ_CUT, GQ_RES, GQ_FTYPE, GQ_DRIVE},
    {GQ_KTRK, GQ_X, GQ_X, GQ_X},
    {GQ_T(P_ATK), GQ_T(P_DEC), GQ_T(P_SUS), GQ_T(P_REL)},
    {GQ_E2ATK, GQ_E2DEC, GQ_E2SUS, GQ_E2REL},
    {GQ_LFO(0, GL_RATE), GQ_LFO(0, GL_WAVE), GQ_LFO(0, GL_DEPTH), GQ_LFO(0, GL_FADE)},
    {GQ_LFO(1, GL_RATE), GQ_LFO(1, GL_WAVE), GQ_LFO(1, GL_DEPTH), GQ_LFO(1, GL_FADE)},
    {GQ_LFO(0, GL_SYNC), GQ_LFO(1, GL_SYNC), GQ_X, GQ_X},
    GRAIN_MPG(0), GRAIN_MPG(1), GRAIN_MPG(2), GRAIN_MPG(3), GRAIN_MPG(4), GRAIN_MPG(5), GRAIN_MPG(6), GRAIN_MPG(7),
};
_Static_assert(P_REL < 0x7F, "GRAIN_MAP's track ids");
/* the editor's screen plan (eng_screen_t): OSC alone, two screens under the wave band (ENG_B_WAVE: the buffers'
 * envelopes, POS, SPRD and the grains; cr_draw.c CR_W_WAVE through grain_wave); FILT, ENV, LFO and MOD by the title
 * rules (cr_edit.c: the filter's curve, the AHDSR a page, the LFO stacks, the matrix's stack) */
#define GR_ROW {ENG_C_RULE, ENG_C_RULE, ENG_C_RULE, ENG_C_RULE}
static const eng_screen_t GRAIN_SCREENS[] = {
    {0, {GPG_SRC1, GPG_SRC2}, ENG_B_WAVE, 1, "SRC", {GR_ROW, GR_ROW}, {{0}, {0}}},
    {0, {GPG_GRAIN, GPG_GRAINP}, ENG_B_WAVE, 1, "GRAIN", {GR_ROW, GR_ROW}, {{0}, {0}}},
    {2, {GPG_ENV1, 0xFF}, ENG_B_ENV, 0, "ENV 1 \267 amp", {GR_ROW, GR_ROW}, {{0}, {0}}},   /* (the title rules would
                                                                                       * name ENV 2 the VA's "filter") */
    {2, {GPG_ENV2, 0xFF}, ENG_B_ENV, 0, "ENV 2 \267 mod", {GR_ROW, GR_ROW}, {{0}, {0}}},
};

static void grain_macros_out(track_t *t)
{
    uint32_t tr = grain_tr(t), k;
    if (tr >= GRAIN_NPART)
        return;
    for (k = 0; k < 8u; k++) {
        int16_t v = grain_patch[tr][GRAIN_MAC[k]];
        t->p[P_E0 + k] = v;
        grain_mlast[tr][k] = v;
    }
}
static int32_t grain_get(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = grain_tr(t), i;
    int32_t v;
    if (page >= GRAIN_NPAGES || col > 3u || (i = GRAIN_MAP[page][col]) == GQ_X)
        return 0;
    if (i & 0x80u)
        return t->p[i & 0x7Fu];
    if (tr >= GRAIN_NPART)
        return grain_range(i).def;
    v = grain_patch[tr][i];
    if (i >= GQ_MOD0 && i < GQ_MFINE0 && (i - GQ_MOD0) % GM_N == GM_AMT)   /* hundredths: tenths or whole */
        return v * (grain_mfine(grain_patch[tr], (i - GQ_MOD0) / GM_N) ? 10 : 100);
    if (i >= GQ_LFO0 && i < GQ_MOD0 && (i - GQ_LFO0) % GL_N == GL_RATE && grain_patch[tr][i + GL_SYNC])
        return v < (int32_t)VA_NSYNC ? v : (int32_t)VA_NSYNC - 1;   /* (synced: an index into N_VA_SYNC) */
    return v;
}
static void grain_set(track_t *t, uint32_t page, uint32_t col, int32_t v)
{
    uint32_t tr = grain_tr(t), i, k;
    int32_t old;
    if (page >= GRAIN_NPAGES || col > 3u || (i = GRAIN_MAP[page][col]) == GQ_X)
        return;
    if (i & 0x80u) {                             /* a track parameter: the column's range */
        const param_desc_t *d = &GRAIN_PAGES[page].col[col];
        t->p[i & 0x7Fu] = (int16_t)clamp(v, d->min, d->max);
        RING_PUBLISH();
        return;
    }
    if (tr >= GRAIN_NPART)
        return;
    if (i >= GQ_MOD0 && i < GQ_MFINE0 && (i - GQ_MOD0) % GM_N == GM_AMT) {   /* the VA's rule: tenths below 6 */
        k = (i - GQ_MOD0) / GM_N;
        v = va_amt_q(v, grain_patch[tr][i] * (grain_mfine(grain_patch[tr], k) ? 10 : 100));
        grain_mfine_put(grain_patch[tr], k, v % 100 != 0);
        v = v % 100 ? v / 10 : v / 100;
    }
    if (i >= GQ_LFO0 && i < GQ_MOD0 && (i - GQ_LFO0) % GL_N == GL_RATE && grain_patch[tr][i + GL_SYNC])
        v = clamp(v, 0, (int32_t)VA_NSYNC - 1);
    old = grain_patch[tr][i];
    grain_patch[tr][i] = grain_clampv(i, v);
    if (i >= GQ_LFO0 && i < GQ_MOD0 && (i - GQ_LFO0) % GL_N == GL_SYNC && grain_patch[tr][i] != old) {
        int8_t *r = &grain_patch[tr][i - GL_SYNC + GL_RATE];   /* SYNC switched: RATE to the same place in the other scale */
        *r = (int8_t)(old ? clamp(*r, 0, (int32_t)VA_NSYNC - 1) * 128 / (int32_t)VA_NSYNC
                          : (int32_t)*r * (int32_t)VA_NSYNC >> 7);
    }
    for (k = 0; k < 8u; k++)                     /* a macro's value: the track's P_E too (grain_block sees no change) */
        if (GRAIN_MAC[k] == i) {
            t->p[P_E0 + k] = grain_patch[tr][i];
            RING_PUBLISH();
            grain_mlast[tr][k] = grain_patch[tr][i];
        }
}
/* LFO n's RATE with its SYNC on: the VA's list (eng_deep_t.desc) */
static const param_desc_t *grain_desc(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = grain_tr(t);
    if ((page == GPG_LFO1 || page == GPG_LFO2) && !col)
        return tr < GRAIN_NPART && grain_patch[tr][GQ_LFO(page - GPG_LFO1, GL_SYNC)] ? &VA_D_SRATE : 0;
    return 0;
}
/* the matrix's destination of a column (the quick mapping), -1 none */
static int32_t grain_mod_dst(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t i;
    (void)t;
    if (page == ENG_MOD_TRK || page >= GRAIN_NPAGES || col > 3u || (i = GRAIN_MAP[page][col]) == GQ_X || (i & 0x80u))
        return -1;
    switch (i) {
    case GQ_MORPH: return GD_MORPH;
    case GQ_POS: return GD_POS;
    case GQ_SIZE: return GD_SIZE;
    case GQ_DENS: return GD_DENS;
    case GQ_SPRD: return GD_SPRD;
    case GQ_RAND: return GD_RAND;
    case GQ_PITCH: return GD_PITCH;
    case GQ_SCAN: return GD_SCAN;
    case GQ_CUT: return GD_CUT;
    case GQ_RES: return GD_RES;
    case GQ_FTYPE: return GD_FTYPE;
    case GQ_DRIVE: return GD_DRIVE;
    case GQ_LFO(0, GL_RATE): return GD_RATE1;
    case GQ_LFO(1, GL_RATE): return GD_RATE2;
    case GQ_LFO(0, GL_DEPTH): return GD_DEP1;
    case GQ_LFO(1, GL_DEPTH): return GD_DEP2;
    default: return -1;
    }
}
/* the patch <-> the blob: 'G', version, the values as bytes (the signed ones as they are), zeros */
static void grain_pack(const int8_t *p, uint8_t *b)
{
    uint32_t i;
    memset(b, 0, GRAIN_BLOB);
    b[0] = GRAIN_MAGIC;
    b[1] = GRAIN_VER;
    for (i = 0; i < GQ_NP; i++)
        b[2 + i] = (uint8_t)p[i];
}
static int grain_unpack(const uint8_t *b, int8_t *p)      /* 1 = a valid blob (version 1: the rest the defaults) */
{
    uint32_t i, n;
    if (!b || b[0] != GRAIN_MAGIC || b[1] < 1u || b[1] > GRAIN_VER)
        return 0;
    n = b[1] == 1u ? GQ_NP_V1 : GQ_NP;
    grain_init_patch(p);
    for (i = 0; i < n; i++)
        p[i] = grain_clampv(i, (int8_t)b[2 + i]);
    if (b[1] == 1u)                              /* version 1's SCAN 0 .. 127 (a forward triangle): forward, halved */
        p[GQ_SCAN] = grain_clampv(GQ_SCAN, (int32_t)(uint8_t)b[2 + GQ_SCAN] / 2);
    return 1;
}
static void grain_blob_get(const track_t *t, uint8_t *out)
{
    uint32_t tr = grain_tr(t);
    int8_t p[GQ_NP];
    if (tr < GRAIN_NPART)
        grain_pack(grain_patch[tr], out);
    else {
        grain_init_patch(p);
        grain_pack(p, out);
    }
}
static void grain_blob_set(track_t *t, const uint8_t *in)
{
    uint32_t tr = grain_tr(t);
    int8_t p[GQ_NP];
    if (tr >= GRAIN_NPART)
        return;
    if (!grain_unpack(in, p))
        grain_init_patch(p);
    memcpy(grain_patch[tr], p, GQ_NP);
    grain_macros_out(t);
}

/* ------------------------------------------------------------ presets --- */
/* the pool after INIT (docs/PRESETS.md): each preset's first 18 values once, as a list (the sources' TYPE TONE MOTION
 * SEED twice, SIZE DENS POS SPRD, MORPH RAND PITCH SCAN, CUT RES); the rest from GRAIN_PX (index, value pairs, 0xFF
 * ends: a slot of the matrix, ENV 2, an LFO); preset_t takes its macros from the same list (GM) */
#define GP_CLOUD_PAD GT_DRONE, 64, 64, 0, GT_BELL, 64, 64, 0, 92, 88, 50, 40, 40, 14, 0, 12, 100, 20
#define GP_SHIMMER GT_BELL, 64, 64, 0, GT_BELL, 104, 104, 11, 70, 100, 30, 30, 64, 24, 12, 8, 110, 10
#define GP_BREATH_PAD GT_BREATH, 64, 64, 0, GT_VOWEL, 64, 20, 3, 92, 88, 50, 40, 64, 10, 0, 12, 96, 30
#define GP_FROZEN GT_VOWEL, 28, 20, 3, GT_BREATH, 28, 20, 3, 108, 72, 40, 0, 0, 10, 0, 5, 92, 10
#define GP_STRUM GT_PLUCK, 64, 100, 0, GT_METAL, 64, 64, 0, 60, 80, 20, 30, 0, 10, 0, 0, 110, 0
#define GP_SWARM GT_CHIRP, 64, 64, 0, GT_SWEEP, 64, 64, 0, 48, 110, 50, 60, 64, 60, 0, 20, 120, 20
#define GP_ACID_DUST GT_SWEEP, 104, 104, 11, GT_PLUCK, 104, 104, 11, 30, 118, 50, 80, 40, 40, 0, -24, 127, 40
#define GP_GLITCH GT_METAL, 64, 64, 0, GT_DRONE, 64, 64, 0, 24, 112, 64, 100, 64, 90, 0, 0, 127, 0
static const int8_t GRAIN_PP[][GQ_NP_V1] = {
    {GP_CLOUD_PAD}, {GP_SHIMMER}, {GP_BREATH_PAD}, {GP_FROZEN}, {GP_STRUM}, {GP_SWARM}, {GP_ACID_DUST}, {GP_GLITCH},
};
#define PM_(k, s, d, a) GQ_MOD(k, GM_SRC), (s), GQ_MOD(k, GM_DST), (d), GQ_MOD(k, GM_AMT), (uint8_t)(a)
static const uint8_t GRAIN_PX[][16] = {
    {GQ_LFO(0, GL_RATE), 40, PM_(0, GSR_LFO1, GD_POS, 20), 0xFF},                      /* CLOUD PAD: LFO 1 wanders POS */
    {GQ_LFO(0, GL_RATE), 50, PM_(0, GSR_LFO1, GD_MORPH, 30), 0xFF},                    /* SHIMMER: LFO 1 breathes MORPH */
    {GQ_LFO(0, GL_RATE), 36, PM_(0, GSR_LFO1, GD_CUT, 10), 0xFF},                      /* BREATH PAD: LFO 1 on CUT */
    {0xFF},                                                                           /* FROZEN */
    {GQ_E2ATK, 0, GQ_E2DEC, 50, GQ_E2SUS, 0, GQ_E2REL, 60, PM_(0, GSR_ENV2, GD_MORPH, 63), 0xFF},   /* STRUM: METAL's strike
                                                                                      * into PLUCK's body (ENV 2 -> MORPH) */
    {PM_(0, GSR_VEL, GD_DENS, 30), 0xFF},                                              /* SWARM: velocity thickens */
    {GQ_FTYPE, 0, GQ_DRIVE, 12, PM_(0, GSR_ENV1, GD_CUT, 24), 0xFF},                   /* ACID DUST: ENV 1 opens the filter */
    {GQ_FTYPE, 64, GQ_CUT, 36, PM_(0, GSR_RAND, GD_PITCH, 12), 0xFF},                  /* GLITCH: a high-pass at ~300 Hz, a
                                                                                      * random pitch */
};
/* the macros of a patch list: source A's four, MORPH SIZE DENS POS (GRAIN_MAC's order) */
#define GM_(t1, n1, m1, s1, t2, n2, m2, s2, sz, dn, ps, sp, mo, ra, pi, sc, cu, re) {t1, n1, m1, s1, mo, sz, dn, ps}
#define GM(...) GM_(__VA_ARGS__)
static const preset_t GRAIN_PRESETS[] = {
    /* name, the macros, {A D S R}, fenv, mono */
    {"CLOUD PAD", GM(GP_CLOUD_PAD), {70, 90, 120, 90}, 0, 0, FX(0, 40, 25, 85), PAT(5)},
    {"SHIMMER", GM(GP_SHIMMER), {30, 90, 110, 90}, 0, 0, FX(0, 50, 40, 90), PAT(7)},
    {"BREATH PAD", GM(GP_BREATH_PAD), {60, 90, 120, 100}, 0, 0, FX(0, 30, 30, 90), PAT(5)},
    {"FROZEN", GM(GP_FROZEN), {50, 100, 127, 100}, 0, 0, FX(0, 30, 20, 95), PAT(5)},
    {"STRUM", GM(GP_STRUM), {0, 70, 100, 60}, 0, 0, FX(0, 20, 30, 60), PAT(3)},
    {"SWARM", GM(GP_SWARM), {10, 80, 110, 70}, 0, 0, FX(0, 30, 40, 70), PAT(4)},
    {"ACID DUST", GM(GP_ACID_DUST), {0, 60, 90, 40}, 0, 0, FX(20, 0, 40, 30), PAT(1)},
    {"GLITCH", GM(GP_GLITCH), {0, 70, 100, 30}, 0, 0, FX(10, 0, 50, 25), PAT(4)},
};
#define GRAIN_NPRESETS ((uint32_t)NELEM(GRAIN_PRESETS))
_Static_assert(NELEM(GRAIN_PP) == NELEM(GRAIN_PRESETS) && NELEM(GRAIN_PX) == NELEM(GRAIN_PRESETS), "GRAIN presets");
static void grain_preset_patch(uint32_t k, int8_t *p)
{
    const uint8_t *x;
    uint32_t i;
    k = k < GRAIN_NPRESETS ? k : 0u;
    grain_init_patch(p);
    memcpy(p, GRAIN_PP[k], GQ_NP_V1);
    for (x = GRAIN_PX[k], i = 0; x[i] != 0xFFu && i + 1u < 16u; i += 2u)
        p[x[i]] = grain_clampv(x[i], (int8_t)x[i + 1u]);
}
static void grain_blob_preset(track_t *t, uint32_t k)
{
    uint32_t tr = grain_tr(t);
    if (tr >= GRAIN_NPART)
        return;
    grain_preset_patch(k, grain_patch[tr]);
    grain_macros_out(t);
}

/* a sound load put GRAIN into track t (eng_fm6.c fm6_track_loaded calls it on every load path: a factory preset, a
 * user slot, INIT, an engine switch): its patch. A user slot (grain_user_pending, upool.c up_loading): its stored
 * patch; a slot without one is a Felucca GRAIN record (SRC POS SIZE DENS PTCH SPRD RAND TONE as its P_E): INIT's
 * sources with its SIZE DENS POS SPRD PITCH RAND and TONE as CUT; else a preset whose macros the track holds: its
 * patch; else the init patch with the track's macros on it (INIT) */
static void grain_track_loaded(const track_t *ct)
{
    track_t *t = (track_t *)ct;
    uint32_t tr = grain_tr(t), pend = grain_user_pending, k;
    uint8_t b[GRAIN_BLOB];
    int8_t p[GQ_NP];
    grain_user_pending = 0;
    if (tr >= GRAIN_NPART || t->eng_req != ENGI_GRAIN)
        return;
    if (pend && grain_store_read && !grain_store_read(pend - 1u, b) && grain_unpack(b, p)) {
        memcpy(grain_patch[tr], p, GQ_NP);
        grain_macros_out(t);
        return;
    }
    grain_init_patch(p);
    if (pend) {                                  /* an old record: Felucca's eight in their old places */
        p[GQ_SIZE] = grain_clampv(GQ_SIZE, t->p[P_E2]);
        p[GQ_DENS] = grain_clampv(GQ_DENS, t->p[P_E3]);
        p[GQ_POS] = grain_clampv(GQ_POS, t->p[P_E1]);
        p[GQ_SPRD] = grain_clampv(GQ_SPRD, t->p[P_E5]);
        p[GQ_PITCH] = grain_clampv(GQ_PITCH, t->p[P_E4]);
        p[GQ_RAND] = grain_clampv(GQ_RAND, t->p[P_E6]);
        p[GQ_CUT] = grain_clampv(GQ_CUT, t->p[P_E7]);
        memcpy(grain_patch[tr], p, GQ_NP);
        grain_macros_out(t);
        return;
    }
    if (t->preset < GRAIN_NPRESETS) {
        for (k = 0; k < 8u && t->p[P_E0 + k] == GRAIN_PRESETS[t->preset].e[k]; k++)
            ;
        if (k == 8u) {
            grain_blob_preset(t, t->preset);
            return;
        }
    }
    for (k = 0; k < 8u; k++)
        p[GRAIN_MAC[k]] = grain_clampv(GRAIN_MAC[k], t->p[P_E0 + k]);
    memcpy(grain_patch[tr], p, GQ_NP);
    grain_macros_out(t);
}

/* -------------------------------------------------------------- the player --- */
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (eng_sample.c's, when SAMPLE is not built) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
/* grain level by overlap (grains sounding at once on average): 1 / sqrt(n), Q15 */
static const int16_t GP_NORM[GP_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585, 10922,
                                           10362, 9880, 9459, 9088, 8757, 8460, 8192};
typedef struct {
    uint32_t pos;                /* forward: the next source sample to fetch; reverse: the index of a (A's and B's) */
    int32_t frac;                /* Q16 between a and b */
    uint32_t step;               /* Q16 source samples an output sample */
    uint32_t wph, winc, left;    /* window phase (one turn = the grain), output samples to go */
    int32_t a, b, gain;          /* a, b: the two samples it interpolates, A and B mixed by MORPH as fetched */
    int32_t dc;                  /* the step a render made under it, ramped out over dcn more blocks */
    uint8_t owner;               /* voice index + 1, 0 = free */
    uint8_t rev, dcn;
} gp_grain_t;
typedef struct {
    gp_grain_t g[GP_NG];
    uint32_t scan;               /* SCAN's phase: a round of the buffer */
    int32_t rng;
    uint32_t lph[2], lrnd[2];    /* the LFOs: phases, S&H */
    int16_t lout[2], lraw[2];    /* .. this block's value x DEPTH, Q15; before DEPTH (the matrix's DEPn) */
    int8_t scan_off, rate_off[2];   /* the matrix's SCAN, RATE1, RATE2 (per part: the latest note's voice) */
    uint8_t depmod;              /* a matrix slot goes to DEP1 / DEP2 */
    uint8_t morph;               /* MORPH as the latest note's voice plays it, 0 .. 127 (the wave band's blend) */
} gp_part_t;
static gp_part_t gp_parts[GRAIN_NPART] __attribute__((section(".pool")));   /* (its own, as the VA's voices: eng_state's
                                  * union is defined before this file, which follows eng_va.c) */
static gp_part_t *grain_part(uint32_t part) { return part < GRAIN_NPART ? &gp_parts[part] : 0; }
static int16_t grain_buf[2][GS_N] __attribute__((section(".pool")));   /* the sources A and B */
#if GRAIN_ASYNC
#define GRAIN_SLICE_MAX 1024u
static int16_t grain_sc[GS_HIST + GRAIN_SLICE_MAX] __attribute__((section(".pool")));   /* a slice, raw, after the
                                  * last one's tail (grain_poll) */
#endif

/* the scheduler: the audio side (grain_tick, core 0) posts; the main loop (grain_poll) renders, in place: the grains
 * play on while the slices write over the old material */
static struct {
    volatile uint8_t req;        /* bit k: source k is to be rendered */
    volatile int32_t root16[2];  /* each buffer's root pitch (PLUCK's is off C4; the player reads at A's) */
    uint8_t cur[2][4];           /* the knobs each buffer holds (or is being rendered toward); 0xFF: nothing yet */
    uint8_t want[2][4];          /* what the owner's patch asks */
    uint8_t stage, which;        /* the poll's: 0 idle, 1 measuring `which`, 2 writing it */
    uint8_t last;                /* the source rendered last (both asked: the other goes first) */
    uint8_t owner;               /* the part whose sources render + 1, 0 none */
    volatile uint32_t wpos;      /* the write head in `which` (stage 2): samples below it are the new material's */
    volatile uint32_t mlo, mhi;  /* the samples rewritten since the tick took them (lo >= hi: none) */
    uint32_t tlo, thi;           /* .. as the tick took them: this block's (gp_run's declick) */
    int16_t stash[GRAIN_RAMP];   /* the last slice's ramp, the new material (the next slice writes it) */
    uint32_t renders;            /* renders completed (the tests, the wave band's redraw) */
    int32_t gain;                /* the measuring pass's: the writing pass's gain, Q12 */
    gs_hp_t hp;
    gs_t g;
} grs = {.root16 = {GS_ROOT16, GS_ROOT16}, .cur = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}}};

static inline uint32_t gp_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
/* the grain window, Hann: sin^2(pi x) = (1 - cos(2 pi x)) / 2, Q15, from the sine table (Felucca's 257-point table
 * of the same, 514 B, is not needed) */
static inline int32_t gp_win(uint32_t wph) { return (32767 - sine_i(wph + 0x40000000u)) >> 1; }

/* the grain knobs as a voice reads them at a grain's start: the patch plus the matrix (grain_render); morph Q15,
 * 32768 = B alone */
typedef struct { int32_t size, dens, sprd, rnd, pitch16, morph, pos_q8; } gp_eff_t;
/* A and B at source sample i, mixed by MORPH mq (Q15, 0 .. 32768: (B - A) x 32768 fits in int32) */
static inline int32_t gp_mix(uint32_t i, int32_t mq)
{
    int32_t x = grain_buf[0][i];
    return x + (((grain_buf[1][i] - x) * mq) >> 15);
}

/* start a grain of voice v (vi) at its interval iv (for the overlap's level) with the effective knobs e */
static void gp_spawn(gp_part_t *P, voice_t *v, uint32_t vi, uint32_t iv, const gp_eff_t *e)
{
    gp_grain_t *g = 0;
    uint32_t i, n = GS_N, len, step, span, c, sp, r1, r2, ov, rev;
    int32_t d16, amt, pos;
    (void)v;
    for (i = 0; i < GP_NG && !g; i++)
        if (!P->g[i].owner)
            g = &P->g[i];
    if (!g)
        return;
    r1 = noise32(&P->rng);
    r2 = noise32(&P->rng);
    len = (441u * pow2_q16(e->size * 8)) >> 16;             /* 10 .. 393 ms */
    amt = e->rnd * e->rnd * 192 / (127 * 127);
    d16 = e->pitch16 - grs.root16[0] + (((int32_t)(r1 & 0xFFFFu) - 32768) * amt >> 15);   /* (one read head: at A's
                                                  * root; a PLUCK B plays 6 cents sharp of it, a PLUCK A the rest flat) */
    step = (pow2_q16(clamp(d16, -1536, 576)) >> 8) * (GP_RATE_Q16 >> 8);
    step = step > GP_STEP_MAX ? GP_STEP_MAX : step < 256u ? 256u : step;
    rev = ((r1 >> 16) & 255u) < (uint32_t)e->rnd;           /* RAND 127: half of them */
    span = (len * (step >> 4)) >> 12;
    if (span + 3u > n) {
        span = n - 3u;
        len = (span << 8) / (step >> 8);
    }
    if (len < 16u)
        return;
    c = gp_scale(n - 1u, (uint32_t)clamp(e->pos_q8, 0, 127 << 8) * 65535u / (127u << 8));
    sp = gp_scale(n, (uint32_t)e->sprd * 516u);
    pos = (int32_t)c + (int32_t)gp_scale(sp, r2 & 0xFFFFu) - (int32_t)(sp >> 1);
    pos = rev ? clamp(pos, (int32_t)span, (int32_t)n - 2) : clamp(pos, 0, (int32_t)(n - 3u - span));
    g->step = step;
    g->frac = 0;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->rev = (uint8_t)rev;
    g->dcn = 0;
    ov = len / (iv ? iv : 1u);
    g->gain = GP_NORM[ov < 1u ? 1u : ov > GP_NG ? GP_NG : ov];
    g->owner = (uint8_t)(vi + 1u);
    g->a = gp_mix((uint32_t)pos, e->morph);
    g->b = gp_mix((uint32_t)pos + 1u, e->morph);
    g->pos = rev ? (uint32_t)pos : (uint32_t)pos + 2u;
}

/* 32768 / n rounded down, n = 0 .. 2 CTL + 1 (the fetches a grain makes in a block at most: the rate's cap, 2):
 * MORPH's step a fetch (down: the steps never carry it past m1, so (B - A) x MORPH stays in int32) */
static const uint16_t GP_RCP[2 * CTL + 2] = {
    0, 32768, 16384, 10922, 8192, 6553, 5461, 4681, 4096, 3640, 3276, 2978, 2730, 2520, 2340, 2184, 2048, 1927, 1820,
    1724, 1638, 1560, 1489, 1424, 1365, 1310, 1260, 1213, 1170, 1129, 1092, 1057, 1024, 992, 963, 936, 910, 885, 862,
    840, 819, 799, 780, 762, 744, 728, 712, 697, 682, 668, 655, 642, 630, 618, 606, 595, 585, 574, 564, 555, 546, 537,
    528, 520, 512, 504
};
_Static_assert(GP_STEP_MAX == 2u << 16 && CTL == 32, "GP_RCP's length");
/* one grain into acc over n samples, MORPH going from m0 to m1 over them (Q15); returns 0 when it ended. The window
 * (times the grain's level) ramps linearly over the block between its exact values at the block's ends; a fetch
 * reads A and B at the same place and mixes them by MORPH, which steps a fetch from m0 to m1 (one read more, a
 * multiply-add and an add a fetch) */
static int gp_run(gp_grain_t *g, int32_t *acc, uint32_t n, int32_t m0, int32_t m1)
{
    uint32_t m = n < g->left ? n : g->left, step = g->step, pos = g->pos, nf;
    int32_t a = g->a, b = g->b, frac = g->frac, w0, w1, wq, dq, mq = m0, dmq, *o = acc, *oe;
    const int16_t *A = grain_buf[0], *B = grain_buf[1];
#if GRAIN_DECLICK
    if (grs.thi > grs.tlo) {                     /* a render rewrote samples: the two it holds among them? */
        uint32_t h = g->rev ? pos : pos - 2u;
        if (h + 1u >= grs.tlo && h < grs.thi) {  /* .. its next samples are the new material's: take them now, and
                                                  * the step between the old and the new ramps out over GP_DECLICK blocks */
            int32_t s0 = a + (((b - a) * (frac >> 1)) >> 15), s1;
            a = gp_mix(h, m0);
            b = gp_mix(h + 1u, m0);
            s1 = a + (((b - a) * (frac >> 1)) >> 15);
            if (s0 != s1) {
                g->dc = clamp(s0 - s1, -65535, 65535);
                g->dcn = GP_DECLICK;
            }
        }
    }
#endif
    w0 = mulq15(gp_win(g->wph), g->gain);
    g->wph += g->winc * m;
    g->left -= m;
    w1 = g->left ? mulq15(gp_win(g->wph), g->gain) : 0;
    wq = w0 * 1024;
    dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
    nf = g->rev ? (m * step + 65535u - (uint32_t)frac) >> 16 : ((uint32_t)frac + m * step) >> 16;   /* the fetches */
    dmq = ((m1 - m0) * GP_RCP[nf < NELEM(GP_RCP) ? nf : NELEM(GP_RCP) - 1u]) >> 15;
    oe = acc + m;
    if (!g->rev) {
        const int16_t *pa = A + pos, *pb = B + pos, *pe = A + GS_N;
        for (; o < oe; o++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            *o += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac += (int32_t)step;
            while (frac >= 65536) {
                frac -= 65536;
                a = b;
                if (pa < pe) {
                    int32_t x = *pa++;
                    mq += dmq;
                    b = x + (((*pb++ - x) * mq) >> 15);
                }
            }
        }
        pos = (uint32_t)(pa - A);
    } else {
        const int16_t *pa = A + pos, *pb = B + pos;
        for (; o < oe; o++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            *o += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac -= (int32_t)step;
            while (frac < 0) {
                frac += 65536;
                b = a;
                if (pa > A) {
                    int32_t x = *--pa;
                    mq += dmq;
                    a = x + (((*--pb - x) * mq) >> 15);
                }
            }
        }
        pos = (uint32_t)(pa - A);
    }
#if GRAIN_DECLICK
    if (g->dcn) {                                /* the step ramping out: from dc x dcn / GP_DECLICK to one less */
        int32_t e = (g->dc * g->dcn) >> 2, e1 = (g->dc * (g->dcn - 1)) >> 2, de;
        _Static_assert(GP_DECLICK == 4u, "the step's ramp is in quarters");
        de = (e1 - e) >> CTL_LOG2;               /* (a shorter block: the grain ends there, its window closed) */
        wq = w0 * 1024;
        for (o = acc; o < oe; o++) {
            *o += (e * (wq >> 10)) >> 15;
            e += de;
            wq += dq;
        }
        g->dcn--;
    }
#endif
    g->pos = pos;
    g->a = a;
    g->b = b;
    g->frac = frac;
    if (!g->left)
        g->owner = 0;
    return g->left != 0;
}

static void grain_note_on(track_t *t, voice_t *v)
{
    uint32_t tr = grain_tr(t), vi = (uint32_t)(v - t->v) % NVOICE, i;
    gp_part_t *P = tr < GRAIN_NPART ? grain_part(tr) : 0;
    v->s[0] = 0;                                 /* the first grain at once (s[1], MORPH as last played, goes on) */
    v->s[4] = 0;                                 /* ENV 2 from 0, its attack */
    v->s[5] = 1;
    v->s[6] = 0;                                 /* the LFOs' fade from the note */
    if (!v->env) {                               /* a fresh voice: no grains left from before, the filter clear */
        if (P)
            for (i = 0; i < GP_NG; i++)
                if (P->g[i].owner == vi + 1u)
                    P->g[i].owner = 0;
        v->s[2] = v->s[3] = 0;
        v->s[7] = 32767;
    }
}

/* SCAN: a round of the buffer in 110 s (|SCAN| 1) .. 0.47 s (64), halving every 8; the phase a block, signed by the
 * direction; 0 off */
static int32_t grain_scan_inc(int32_t scan)
{
    int32_t s = scan < 0 ? -scan : scan, inc;
    if (!s)
        return 0;
    inc = (int32_t)(((25972u >> 4) * (pow2_q16(s * 24) >> 4)) >> 8);   /* 2^32 x 32 / (44100 x 120 s) x 2^(s / 8) */
    return scan < 0 ? -inc : inc;
}
/* LFO l's phase increment a block from its RATE (free: Felucca's table; synced: the VA's list) and the matrix's RATEn */
static uint32_t grain_lfo_inc(const int8_t *p, uint32_t l, int32_t off)
{
    int32_t rate = p[GQ_LFO(l, GL_RATE)];
    if (p[GQ_LFO(l, GL_SYNC)]) {
        uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 20, 300), i;
        i = (uint32_t)clamp(rate + ((off * (int32_t)VA_NSYNC) >> 7), 0, (int32_t)VA_NSYNC - 1);
        return i >= VA_SYNC_MS0 ? VA_SYNC_Q8[i] : (uint32_t)(((uint64_t)VA_SYNC_Q8[i] * bpm) >> 8);
    }
    return LFO_INC[clamp(rate + off, 0, 127)];
}

/* per part and block: the macros into the patch, grains of ended voices go, SCAN moves, the LFOs tick */
static void grain_block(track_t *t)
{
    uint32_t tr = grain_tr(t), k;
    gp_part_t *P;
    int8_t *p;
    if (tr >= GRAIN_NPART || !(P = grain_part(tr)))
        return;
    p = grain_patch[tr];
    for (k = 0; k < 8u; k++)                     /* a knob, the editor, the matrix, motion moved a macro */
        if (t->p[P_E0 + k] != grain_mlast[tr][k]) {
            grain_mlast[tr][k] = t->p[P_E0 + k];
            p[GRAIN_MAC[k]] = grain_clampv(GRAIN_MAC[k], t->p[P_E0 + k]);
        }
    if (!P->rng)
        P->rng = 0x2545F491;
    for (k = 0; k < GP_NG; k++)
        if (P->g[k].owner && !t->v[(P->g[k].owner - 1u) % NVOICE].active)
            P->g[k].owner = 0;
    P->scan += (uint32_t)grain_scan_inc(clamp(p[GQ_SCAN] + P->scan_off, -64, 63));
    for (k = 0; k < 2u; k++) {
        uint32_t old = P->lph[k], ph;
        int32_t x;
        ph = P->lph[k] = old + grain_lfo_inc(p, k, P->rate_off[k]);
        if (ph < old)
            P->lrnd[k] = va_xs(P->lrnd[k] + k);
        switch (p[GQ_LFO(k, GL_WAVE)]) {
        case 1: x = osc_tri(ph); break;
        case 2: x = (int32_t)(ph >> 16) - 32768; break;
        case 3: x = ph < 0x80000000u ? 32767 : -32767; break;
        case 4: x = (int32_t)(P->lrnd[k] >> 16) - 32768; break;
        default: x = osc_sine(ph); break;
        }
        P->lraw[k] = (int16_t)x;
        P->lout[k] = (int16_t)((x * p[GQ_LFO(k, GL_DEPTH)] * 258) >> 15);
    }
    P->depmod = 0;
    for (k = 0; k < 8u; k++) {
        int32_t d = p[GQ_MOD(k, GM_DST)];
        if ((d == GD_DEP1 || d == GD_DEP2) && p[GQ_MOD(k, GM_SRC)] && p[GQ_MOD(k, GM_AMT)])
            P->depmod = 1;
    }
}

/* ENV 2, one control tick of voice v: Q15 (s[4] Q24, s[5] the stage: 1 attack, 3 decay / sustain, 4 release) */
static int32_t grain_env2_tick(const int8_t *p, voice_t *v)
{
    int32_t x = v->s[4];
    if (!v->gate && v->s[5] && v->s[5] < 4)
        v->s[5] = 4;
    switch (v->s[5]) {
    case 1:
        x += (int32_t)ENV_LIN[p[GQ_E2ATK] & 127];
        if (x >= (1 << 24)) {
            x = 1 << 24;
            v->s[5] = 3;
        }
        break;
    case 3:
        x += mulq16(((int32_t)p[GQ_E2SUS] << 17) - x, ENV_EXP[p[GQ_E2DEC] & 127]);
        break;
    case 4:
        x -= mulq16(x, ENV_EXP[p[GQ_E2REL] & 127]);
        if (x < (1 << 12)) {
            x = 0;
            v->s[5] = 0;
        }
        break;
    default:
        x = 0;
        break;
    }
    v->s[4] = x;
    return x >> 9;
}

static void grain_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t tr = grain_tr(t), vi = (uint32_t)(v - t->v) % NVOICE, i, k, mine = 0, nact = 0, iv, share, ty;
    gp_part_t *P = tr < GRAIN_NPART ? grain_part(tr) : 0;
    const int8_t *p;
    int32_t acc[CTL], src[GSR_N], ic1 = v->s[2], ic2 = v->s[3], mf, gain = 32767;
    int32_t dmorph = 0, dpos = 0, dsize = 0, ddens = 0, dsprd = 0, drnd = 0, dpit = 0, dscan = 0, dcut = 0, dres = 0;
    int32_t dfty = 0, ddrv = 0, drate[2] = {0, 0}, ddep[2] = {0, 0}, cut, kd, drv, g, A0, A1, dA, a, m0;
    uint32_t fpos;
    tsvf_t c;
    va_fw_t fw;
    gp_eff_t e;
    if (!P)
        return;
    p = grain_patch[tr];
    if (n > CTL)
        n = CTL;
    if (v->s[6] < 0xFFFF)
        v->s[6]++;
    /* the sources, Q15 */
    src[GSR_OFF] = 0;
    src[GSR_ENV1] = m->envq15;
    src[GSR_ENV2] = grain_env2_tick(p, v);
    for (k = 0; k < 2u; k++) {
        int32_t f = p[GQ_LFO(k, GL_FADE)] ? v->s[6] * (int32_t)(ENV_LIN[p[GQ_LFO(k, GL_FADE)] & 127] >> 9) : 32767;
        src[GSR_LFO1 + k] = f >= 32767 ? P->lout[k] : (P->lout[k] * f) >> 15;
    }
    src[GSR_VEL] = (v->mvel ? v->mvel : v->vel) * 258;
    src[GSR_KEY] = clamp(((int32_t)v->note - 60) * 512, -32767, 32767);
    src[GSR_RAND] = v->mrnd;
    src[GSR_MODW] = t->mw * 258;
    mf = (int32_t)((uint32_t)p[GQ_MFINE0] | (uint32_t)p[GQ_MFINE1] << 7);
    if (P->depmod) {                             /* DEP1 / DEP2 first: the LFOs' depths, then the LFOs as sources */
        for (k = 0; k < 8u; k++) {
            uint32_t sr = (uint32_t)p[GQ_MOD(k, GM_SRC)], d = (uint32_t)p[GQ_MOD(k, GM_DST)];
            if ((d == GD_DEP1 || d == GD_DEP2) && sr && sr < GSR_N)
                ddep[d - GD_DEP1] += GRAIN_AMX(src[sr] * p[GQ_MOD(k, GM_AMT)], (uint32_t)mf, k);
        }
        for (k = 0; k < 2u; k++)
            if (ddep[k]) {
                int32_t dep = clamp(p[GQ_LFO(k, GL_DEPTH)] + (ddep[k] >> 14), 0, 127), f, x;
                f = p[GQ_LFO(k, GL_FADE)] ? v->s[6] * (int32_t)(ENV_LIN[p[GQ_LFO(k, GL_FADE)] & 127] >> 9) : 32767;
                x = (P->lraw[k] * dep * 258) >> 15;
                src[GSR_LFO1 + k] = f >= 32767 ? x : (x * f) >> 15;
            }
    }
    /* the matrix: Q15 x amount, +-2^21 at a full source and AMT 63 */
    for (k = 0; k < 8u; k++) {
        uint32_t sr = (uint32_t)p[GQ_MOD(k, GM_SRC)], d = (uint32_t)p[GQ_MOD(k, GM_DST)];
        int32_t am = p[GQ_MOD(k, GM_AMT)], x;
        if (!sr || !d || !am || sr >= GSR_N || d >= GD_N)
            continue;
        x = GRAIN_AMX(src[sr] * am, (uint32_t)mf, k);
        switch (d) {
        case GD_MORPH: dmorph += x; break;
        case GD_POS: dpos += x; break;
        case GD_SIZE: dsize += x; break;
        case GD_DENS: ddens += x; break;
        case GD_SPRD: dsprd += x; break;
        case GD_RAND: drnd += x; break;
        case GD_PITCH: dpit += x; break;
        case GD_SCAN: dscan += x; break;
        case GD_CUT: dcut += x; break;
        case GD_RES: dres += x; break;
        case GD_FTYPE: dfty += x; break;
        case GD_DRIVE: ddrv += x; break;
        case GD_RATE1: drate[0] += x; break;
        case GD_RATE2: drate[1] += x; break;
        case GD_AMP: {                           /* as the VA's: a positive amount closes with a low source */
            int32_t u = sr >= GSR_LFO1 && sr <= GSR_LFO2 ? (src[sr] + 32768) >> 1 : sr >= GSR_KEY ? (src[sr] + 32768) >> 1 : src[sr];
            u = clamp(u, 0, 32767);
            gain = mulq15(gain, clamp(32767 - (GRAIN_AMX(am > 0 ? (32767 - u) * am : u * -am, (uint32_t)mf, k) >> 6), 0, 32767));
            break;
        }
        default: break;                          /* (DEP1 / DEP2: above) */
        }
    }
    if (v - t->v == t->m_vi) {                   /* the per-part destinations: from the latest note's voice */
        P->scan_off = (int8_t)clamp(dscan >> 15, -64, 63);
        for (k = 0; k < 2u; k++)
            P->rate_off[k] = (int8_t)clamp(drate[k] >> 14, -127, 127);
    }
    /* the grain knobs as this voice's next grain reads them */
    e.size = clamp(p[GQ_SIZE] + (dsize >> 14), 0, 127);
    e.dens = clamp(p[GQ_DENS] + (ddens >> 14), 0, 127);
    e.sprd = clamp(p[GQ_SPRD] + (dsprd >> 14), 0, 127);
    e.rnd = clamp(p[GQ_RAND] + (drnd >> 14), 0, 127);
    e.pitch16 = m->pitch16 + p[GQ_PITCH] * 16 + ((dpit * 3) >> 15);   /* +-12 st at a full source and AMT 63 */
    /* MORPH (+ the track's SHP, the matrix) as Q15, 32768 = B alone; ramped a fetch from the last block's (s[1]) */
    e.morph = (clamp((p[GQ_MORPH] << 8) + m->shape - (64 << 8) + (dmorph >> 6), 0, 127 << 8) * 16513 + 8192) >> 14;
    m0 = clamp(v->s[1], 0, 32768);
    v->s[1] = e.morph;
    if (v - t->v == t->m_vi)
        P->morph = (uint8_t)((e.morph * 127 + 16384) >> 15);
    e.pos_q8 = (int32_t)(((uint32_t)clamp((p[GQ_POS] << 8) + (dpos >> 6), 0, 127 << 8) + (P->scan >> 17)) % (127u * 256u + 1u));
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (i = 0; i < NVOICE; i++)
        nact += t->v[i].active;
    for (i = 0; i < GP_NG; i++)
        if (P->g[i].owner == vi + 1u)
            mine += (uint32_t)gp_run(&P->g[i], acc, n, m0, e.morph);
    iv = (44100u * pow2_q16(-e.dens * 10)) >> 16;           /* the next grain: DENS a second, 1 .. 98 /s */
    v->s[0] -= (int32_t)n;
    if (v->s[0] <= 0) {
        uint32_t r = noise32(&P->rng);
        int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * e.sprd) / 254;   /* +-SPRD / 2, Q15 */
        v->s[0] += (int32_t)iv + mulq15((int32_t)iv, j);
        if (v->s[0] < 0)
            v->s[0] = 0;
        share = nact > 1u ? (GP_NG / nact > 2u ? GP_NG / nact : 2u) : GP_NG;
        if (mine < share)
            gp_spawn(P, v, vi, iv, &e);
    }
    /* the VA's filter: CUT (+ KTRK round C4, the track's FLT, the matrix), RES, FTYPE round the cycle, DRIVE */
    cut = (p[GQ_CUT] << 8) + (((m->pitch16 - 60 * 16) * p[GQ_KTRK] * 150) >> 10) + m->cutoff + (dcut >> 7);
    kd = 8192 - clamp(p[GQ_RES] + (dres >> 14), 0, 127) * 60;
    tsvf_coef_k(&c, cut, kd);
    fpos = (((uint32_t)p[GQ_FTYPE] << 8) + (uint32_t)clamp(dfty >> 6, -32767, 32767)) & 32767u;
    ty = fpos >> 13;
    fw = va_fweights(ty, (int32_t)(fpos & 8191u) >> 3);
    drv = ddrv ? clamp(p[GQ_DRIVE] + (ddrv >> 14), 0, 127) : p[GQ_DRIVE];
    g = 4096 + drv * 97;                         /* DRIVE: 1x .. 4x */
    A0 = v->s[7];                                /* the matrix's AMP, ramped over the block */
    A1 = gain;
    dA = (A1 - A0) >> CTL_LOG2;
    a = A0;
    for (i = 0; i < n; i++) {
        int32_t x = clamp(acc[i], -65535, 65535) >> 1, y;
        if (drv)
            x = softclip((x * (g >> 4)) >> 8);
        y = va_svf(&c, x, &ic1, &ic2, kd, fw);
        a += dA;
        out[i] += voice_amp(mulq15(clamp(y * 2, -32767, 32767), a), m, i);
    }
    v->s[2] = ic1;
    v->s[3] = ic2;
    v->s[7] = A1;
}

/* ------------------------------------------------------------ the screen --- */
/* what the wave band draws (cr_screen.h cr_wave_t; cr_edit.c sets cr_wave_src to this): the two buffers' envelopes
 * over GRAIN_WAVE_W columns (a min / max a column, int8 of 127), kept as the slices write them (the columns a write
 * touched, retaken when the band asks); the grains of the part (position, level), POS as the part's voices read it
 * (SCAN included), MORPH as the latest note plays it (the band draws the two envelopes weighted by it) */
#define GRAIN_WAVE_W 224u
#define GRAIN_WAVE_COL (GS_N / GRAIN_WAVE_W)
static struct {
    uint32_t lo[2], hi[2];       /* the samples written since the envelopes were taken (lo >= hi: none) */
    int8_t env[2][GRAIN_WAVE_W][2];
} grain_wv = {{0, 0}, {GS_N, GS_N}, {{{0}}}};
static void grain_wv_dirty(uint32_t k, uint32_t lo, uint32_t hi)
{
    if (grain_wv.lo[k] >= grain_wv.hi[k]) {
        grain_wv.lo[k] = lo;
        grain_wv.hi[k] = hi;
    } else {
        grain_wv.lo[k] = lo < grain_wv.lo[k] ? lo : grain_wv.lo[k];
        grain_wv.hi[k] = hi > grain_wv.hi[k] ? hi : grain_wv.hi[k];
    }
}
static const cr_wave_t *grain_wave(uint32_t part)
{
    static cr_wave_t w;
    const gp_part_t *P = part < GRAIN_NPART ? grain_part(part) : 0;
    const int8_t *p = part < GRAIN_NPART ? grain_patch[part] : 0;
    uint32_t k, i, j, on = 0;
    for (k = 0; k < 2u; k++) {                   /* the columns written since: GRAIN_WAVE_COL samples a column */
        uint32_t c0 = grain_wv.lo[k] / GRAIN_WAVE_COL, c1 = (grain_wv.hi[k] + GRAIN_WAVE_COL - 1u) / GRAIN_WAVE_COL;
        if (grain_wv.lo[k] >= grain_wv.hi[k])
            continue;
        for (i = c0; i < c1 && i < GRAIN_WAVE_W; i++) {
            const int16_t *s = grain_buf[k] + GRAIN_WAVE_COL * i;
            int32_t lo = 0, hi = 0;
            for (j = 0; j < GRAIN_WAVE_COL; j++) {
                lo = s[j] < lo ? s[j] : lo;
                hi = s[j] > hi ? s[j] : hi;
            }
            grain_wv.env[k][i][0] = (int8_t)(lo >> 8);
            grain_wv.env[k][i][1] = (int8_t)(hi >> 8);
        }
        grain_wv.lo[k] = grain_wv.hi[k] = 0;
    }
    w.cols = GRAIN_WAVE_W;
    w.env[0] = &grain_wv.env[0][0][0];
    w.env[1] = &grain_wv.env[1][0][0];
    w.n = 0;
    for (i = 0; part < GRAIN_NPART && i < NVOICE; i++)
        on |= trk[part].v[i].active;
    w.morph = p ? (uint8_t)clamp(P && on ? P->morph : p[GQ_MORPH], 0, 127) : 0u;
    w.sprd = p ? (uint8_t)clamp(p[GQ_SPRD], 0, 127) : 0u;
    w.pos = p && P ? (uint8_t)((((uint32_t)clamp(p[GQ_POS] << 8, 0, 127 << 8) + (P->scan >> 17)) % (127u * 256u + 1u)) >> 8) : 64u;
    w.busy = grs.stage != 0u;
    w.gen = (uint8_t)grs.renders;
    if (P)
        for (k = 0; k < GP_NG && w.n < CR_WAVE_GRAINS; k++) {
            const gp_grain_t *g = &P->g[k];
            uint32_t pos;
            if (!g->owner)
                continue;
            pos = g->pos < GS_N ? g->pos : GS_N - 1u;
            w.gpos[w.n] = (uint8_t)(pos * (GRAIN_WAVE_W - 1u) / (GS_N - 1u) * 255u / (GRAIN_WAVE_W - 1u));
            w.glev[w.n] = (uint8_t)(mulq15(gp_win(g->wph), g->gain) >> 7);
            w.n++;
        }
    return &w;
}

/* ---------------------------------------------------------- the render --- */
/* in place, over frames (docs/GRAIN.md "The render in place"): the grains never stop; the main loop's poll renders a
 * slice a call into a scratch (grain_sc, after the last slice's tail: PLUCK's string reads its own last 85), twice
 * over: a measuring pass (the finish's high-passed peak, nothing written), then the writing pass from the start with
 * the gain applied as it goes, over the old material; the finished buffer is byte for byte a whole render's (gs_finish).
 * Latest wins at the end of a render, never inside one: a knob turned during a render lets it finish (the sound
 * moves at every render, however fast the knob goes), and the next starts on the knobs as they are then (the turns
 * between are skipped); both sources asked: the one not rendered last first */
static void grain_render_whole(uint32_t k);
#if GRAIN_ASYNC
static void fm1_irq_off(void);                   /* (hal/fm1_irq.h's, the emulator's: defined after this file) */
static void fm1_irq_on(void);
/* samples a slice by TYPE: 0.3 .. 0.5 ms on the unit at the listing's 205 instructions a us (x 1.5, the unit's
 * factor so far: 0.45 .. 0.75 ms) with the writing pass's high-pass and gain (grain_src.c gs_apply) */
static const uint16_t GRAIN_SLICE_N[GS_NTYPE] = {1024, 256, 512, 512, 512, 256, 256, 512};   /* PLUCK DRONE BELL BREATH
                                                                               * CHIRP SWEEP VOWEL METAL */
_Static_assert(GS_HIST * 2u <= 256u && GS_N % 1024u == 0 && GRAIN_SLICE_MAX == 1024u, "the slices and the history");
/* the writing pass's slice x (the new material of [p0, p1)) into buffer k, quickly (copies: the audio side may come
 * in between, rarely): the last slice's ramp whole, the slice, its last GRAIN_RAMP samples fading into the old
 * material ahead (none at the end: the buffer is the whole render's); the samples rewritten for the tick */
static void grain_write(uint32_t k, uint32_t p0, uint32_t p1, const int16_t *x)
{
    int16_t *d = grain_buf[k];
    uint32_t n = p1 - p0, r = GRAIN_DECLICK && p1 < GS_N ? GRAIN_RAMP : 0u, i, lo = GRAIN_DECLICK && p0 ? p0 - GRAIN_RAMP : p0;
    _Static_assert(GRAIN_RAMP <= 256u, "a slice is longer than the ramp");
    if (lo < p0)
        memcpy(d + lo, grs.stash, sizeof grs.stash);
    memcpy(d + p0, x, (n - r) * sizeof *x);
    for (i = n - r; i < n; i++) {
        int32_t o = d[p0 + i], w = (int32_t)(i - (n - r) + 1u) * (32768 / (int32_t)(GRAIN_RAMP + 1u));
        d[p0 + i] = (int16_t)(x[i] + (((o - x[i]) * w) >> 15));
    }
    if (r)
        memcpy(grs.stash, x + n - GRAIN_RAMP, sizeof grs.stash);
    fm1_irq_off();
    if (grs.mlo >= grs.mhi) {
        grs.mlo = lo;
        grs.mhi = p1;
    } else {
        grs.mlo = lo < grs.mlo ? lo : grs.mlo;
        grs.mhi = p1 > grs.mhi ? p1 : grs.mhi;
    }
    fm1_irq_on();
    grs.wpos = p1;
    grain_wv_dirty(k, lo, p1);
}
static void grain_poll(void)
{
    uint32_t k = grs.which, p0, p1, n;
    int16_t *out = grain_sc + GS_HIST;
    if (!grs.stage) {
        if (!grs.req)
            return;
        fm1_irq_off();
        k = grs.req == 3u ? 1u - grs.last : (grs.req & 1u) ? 0u : 1u;
        memcpy(grs.cur[k], grs.want[k], 4);
        grs.req &= (uint8_t)~(1u << k);
        fm1_irq_on();
        grs.which = (uint8_t)k;
        gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
        gs_hp_begin(&grs.hp);
        grs.stage = 1;
    }
    p0 = grs.g.pos;
    p1 = gs_step_to(&grs.g, out, GRAIN_SLICE_N[grs.g.type]);
    n = p1 - p0;
    if (grs.stage == 1u)
        gs_measure(&grs.hp, out, n);
    memcpy(grain_sc, grain_sc + n, GS_HIST * sizeof *grain_sc);    /* the raw tail: the next slice's history (no
                                                  * overlap: a slice is at least 2 x GS_HIST, and GS_N a whole number of them) */
    if (grs.stage == 2u) {
        gs_apply(&grs.hp, out, out, n, grs.gain);
        grain_write(k, p0, p1, out);
    }
    if (p1 < GS_N)
        return;
    if (grs.stage == 1u) {                       /* measured: the gain; the writing pass from the start */
        grs.gain = gs_gain(&grs.hp);
        gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
        gs_hp_begin(&grs.hp);
        grs.root16[k] = grs.g.root16;
        grs.wpos = 0;
        grs.stage = 2;
        return;
    }
    grs.renders++;
    grs.last = (uint8_t)k;
    grs.stage = 0;
}
#endif
/* a whole render here and now (GRAIN_ASYNC 0: the audio side itself) */
static void grain_render_whole(uint32_t k)
{
    memcpy(grs.cur[k], grs.want[k], 4);
    grs.req &= (uint8_t)~(1u << k);
    gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
    while (gs_step(&grs.g, grain_buf[k], GS_N) < GS_N)
        ;
    gs_finish(grain_buf[k]);
    grs.root16[k] = grs.g.root16;
    grs.renders++;
    grain_wv_dirty(k, 0, GS_N);
}

/* once an audio block, on core 0, before the parts render (fx.c mix_block): the owner's source knobs against the
 * buffers. Out of line: its own line in tests/target_budget.txt, not the ISR's */
static __attribute__((noinline)) void grain_tick(void)
{
    uint32_t k, part, own = 0;
    grs.tlo = grs.mlo;                           /* the samples the render rewrote since the last block */
    grs.thi = grs.mhi;
    grs.mlo = grs.mhi = 0;
    for (part = 0; part < GRAIN_NPART && !own; part++)
        if (trk[part].eng_req == ENGI_GRAIN || trk[part].engine == ENGI_GRAIN)
            own = part + 1u;
    grs.owner = (uint8_t)own;
    if (own) {
        const int8_t *p = grain_patch[own - 1u];
        for (k = 0; k < 2; k++) {
            uint8_t w[4] = {(uint8_t)p[GQ_TYPE1 + 4 * k], (uint8_t)p[GQ_TONE1 + 4 * k], (uint8_t)p[GQ_MOTION1 + 4 * k],
                            (uint8_t)p[GQ_SEED1 + 4 * k]};
            memcpy(grs.want[k], w, 4);
            if (memcmp(w, grs.cur[k], 4))          /* (not what the buffer holds, nor what renders toward) */
                grs.req |= (uint8_t)(1u << k);
        }
    }
#if !GRAIN_ASYNC
    while (grs.req)                              /* no main loop: render now, whole */
        grain_render_whole((grs.req & 1u) ? 0u : 1u);
#endif
}

static const eng_deep_t GRAIN_DEEP = {
    .npages = NELEM(GRAIN_PAGES),
    .pages = GRAIN_PAGES,
    .section = {GPG_SRC1, GPG_FILTER, GPG_ENV1, GPG_LFO1, GPG_MOD0, 0xFF, 0xFF, 0xFF},
    .get = grain_get,
    .set = grain_set,
    .blob_size = GRAIN_BLOB,
    .blob_get = grain_blob_get,
    .blob_set = grain_blob_set,
    .blob_preset = grain_blob_preset,
    .desc = grain_desc,
    .mod_dst = grain_mod_dst,
    .screens = GRAIN_SCREENS,
    .nscreens = NELEM(GRAIN_SCREENS),
};

static const engine_t ENG_GRAIN = {
    .name = "GRAIN",
    .page_title = {"SRC", "GRAIN"},
    .edit = {
        GC_TYPE(GT_DRONE),
        GC_TONE,
        GC_MOTION,
        GC_SEED,
        GC_MORPH,
        GC_SIZE,
        GC_DENS,
        GC_POS,
    },
    .presets = GRAIN_PRESETS,
    .npresets = NELEM(GRAIN_PRESETS),
    .knob = {P_E4, P_E5, P_E6, P_E7},
    .poly = GP_POLY,
    .sampled = 1,
    .note_on = grain_note_on,
    .render = grain_render,
    .block = grain_block,
    .deep = &GRAIN_DEEP,
};
