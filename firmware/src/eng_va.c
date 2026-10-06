/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* VA: a four-oscillator virtual analog with deep editing (ChoralRoot, FELUCCA_VA; docs/VA.md).
 *
 * Per voice: four oscillators (MODE BASIC: SAW SQR TRI SIN PWM NOIS; MORPH: one continuous wave sine -> triangle
 * -> saw -> ramp -> square -> narrow pulse by SHAPE; NOISE: WHITE BROWN VINYL; LEVEL, COARSE, FINE, SHAPE, KEY
 * TRACK; OSC 2 can hard-sync to OSC 1, OSC 4 ring-modulate with OSC 3), a mixer into the trapezoidal SVF of dsp.c
 * (LP BP HP NOTCH and a continuous MORPH between them, CUT RES DRIVE KTRK, FENV from ENV 2; SPREAD: a second SVF
 * per voice, the cutoffs apart for the left and the right, rendered as mid + side: engine_t.render2), four AHDSR envelopes at control rate on Felucca's time tables (ENV 1 is the
 * amplitude: engine_t.ownenv / done, as FM6), four LFOs per part at control rate (free or synced to the tempo) and
 * an 8-slot modulation matrix evaluated once per control tick; pitch, level and shape move per sample as linear
 * ramps over the tick.
 *
 * The patch is the sound: VA_NP signed bytes per part (va_patch), edited through the deep pages (eng_deep_t:
 * get / set from the main loop), loaded from a factory preset (blob_preset), a user slot (va_store.c: the blob) or
 * the init patch. The eight P_E0..P_E7 are macros that write into the patch: CUT RES FENV DRIVE | MIX DTN ATK REL
 * (MIX crossfades the pairs 1+2 / 3+4, DTN spreads the four fine tunings; both are patch fields too). The audio ISR
 * picks a macro change up in va_block (va_mlast); the deep set writes the macro back, so both always agree.
 *
 * State: the patch and the LFOs per part (VA_NPART: the two ChoralRoot parts; a VA sound on a part above them is
 * silent), per voice voice_t (ph[0..2] OSC 1..3, s[0..1] the filter, s[2] the noise, s[3..6] the oscillators' last
 * phase increments, s[7] the last amplitude) plus a va_voice_t (OSC 4's phase, the envelopes, the ramps' ends). */
#ifndef ENGI_VA
#define ENGI_VA (13u + FELUCCA_SLICE)   /* engines.c ENGINES[] (append-only): 13 on ChoralRoot */
#endif
#define VA_NPART 2u              /* parts with a VA patch (ChoralRoot: chord, bass) */
#define VA_POLY 8                /* engine_t.poly (docs/VA.md: CPU) */
#define VA_BLOB 110u             /* packed patch: 2 bytes magic / version, VA_NP values, zero padding */
#define VA_MAGIC 0x56u           /* 'V' */
#define VA_VER 2u
#define VA_BLOB1 104u            /* version 1 (before the oscillator modes, FILTER MORPH / SPREAD, USPREAD): 99 values */
#define VA_NP1 99u

/* --------------------------------------------------------- the patch --- */
enum { VO_WAVE, VO_LEVEL, VO_COARSE, VO_FINE, VO_SHAPE, VO_KTRK, VO_N };           /* per oscillator */
enum { VE_ATK, VE_HOLD, VE_DEC, VE_SUS, VE_REL, VE_N };                            /* per envelope */
enum { VL_RATE, VL_WAVE, VL_DEPTH, VL_FADE, VL_SYNC, VL_N };                       /* per LFO */
enum { VM_SRC, VM_DST, VM_AMT, VM_N };                                             /* per matrix slot */
#define VA_OSC(k, f) ((k) * VO_N + (f))
enum {
    VA_SYNC2 = 4 * VO_N, VA_RING4,
    VA_FTYPE, VA_CUT, VA_RES, VA_DRIVE, VA_FKTRK, VA_FENV,
    VA_ENV0, VA_VEL = VA_ENV0 + 4 * VE_N,
    VA_LFO0, VA_MOD0 = VA_LFO0 + 4 * VL_N,
    VA_OMIX = VA_MOD0 + 8 * VM_N, VA_DETUNE,
    VA_OMODE0,                                   /* version 2 (appended: version 1's values keep their places): */
    VA_FMORPH = VA_OMODE0 + 4, VA_FSPREAD, VA_USPREAD,   /* MODE of OSC 1..4, FILTER MORPH, SPREAD, USPREAD */
    VA_NP
};
#define VA_ENV(k, f) (VA_ENV0 + (k) * VE_N + (f))
#define VA_LFO(k, f) (VA_LFO0 + (k) * VL_N + (f))
#define VA_MOD(k, f) (VA_MOD0 + (k) * VM_N + (f))
_Static_assert(VA_NP == 106 && VA_OMODE0 == VA_NP1 && VA_NP + 2u <= VA_BLOB && VA_BLOB <= 128u &&
               VA_NP1 + 2u <= VA_BLOB1, "VA patch layout");

enum { VW_SAW, VW_SQR, VW_TRI, VW_SIN, VW_PWM, VW_NOIS, VW_N };
enum { VF_LP, VF_BP, VF_HP, VF_NOTCH };
enum { VOM_BASIC, VOM_MORPH, VOM_NOISE, VOM_N };                                  /* oscillator MODE */
enum { VN_WHITE, VN_BROWN, VN_VINYL, VN_N };                                      /* NOISE: WAVE is the type */
enum { VS_OFF, VS_ENV1, VS_LFO1 = VS_ENV1 + 4, VS_VEL = VS_LFO1 + 4, VS_KEY, VS_RAND, VS_MODW, VS_N };
enum { VD_OFF, VD_PITCH, VD_PIT1, VD_LVL1 = VD_PIT1 + 4, VD_SHP1 = VD_LVL1 + 4, VD_CUT = VD_SHP1 + 4, VD_RES, VD_AMP,
       VD_PAN, VD_RATE1, VD_FMORPH = VD_RATE1 + 4, VD_N };   /* (append only: the blobs store them) */
#define VA_NDIV 14u              /* LFO SYNC: the divisions RATE picks (va_div) */

static const char *const N_VA_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "NOIS"};
static const char *const N_VA_FTYPE[] = {"LP", "BP", "HP", "NOTCH"};
static const char *const N_VA_OMODE[] = {"BASIC", "MORPH", "NOISE"};
static const char *const N_VA_NTYPE[VW_N] = {"WHITE", "BROWN", "VINYL", "WHITE", "WHITE", "WHITE"};   /* NOISE's WAVE
                                                                         * (a stored value above VINYL plays WHITE) */
static const char *const N_VA_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_VA_SRC[] = {"OFF", "ENV1", "ENV2", "ENV3", "ENV4", "LFO1", "LFO2", "LFO3", "LFO4",
                                       "VEL", "KEY", "RAND", "MODW"};
static const char *const N_VA_DST[] = {"OFF", "PITCH", "PIT1", "PIT2", "PIT3", "PIT4", "LVL1", "LVL2", "LVL3",
                                       "LVL4", "SHP1", "SHP2", "SHP3", "SHP4", "CUT", "RES", "AMP", "PAN",
                                       "RATE1", "RATE2", "RATE3", "RATE4", "FMORPH"};
static const char *const N_VA_DIV[VA_NDIV] = {"8BAR", "4BAR", "2BAR", "1BAR", "1/2", "1/4.", "1/4", "1/4T",
                                              "1/8.", "1/8", "1/8T", "1/16", "16T", "1/32"};
_Static_assert(NELEM(N_VA_SRC) == VS_N && NELEM(N_VA_DST) == VD_N && NELEM(N_VA_WAVE) == VW_N &&
               NELEM(N_VA_OMODE) == VOM_N && VN_N <= VW_N, "VA names");

/* the deep pages' columns (the UI contract, docs/VA.md) */
#define VC_WAVE {"WAVE", F_ENUM, 0, VW_N - 1, VW_SAW, N_VA_WAVE, 0}
#define VC_LEVEL {"LEVEL", F_PCT, 0, 127, 0, 0, 0}
#define VC_COARSE {"COARSE", F_SEMI, -24, 24, 0, 0, 0}
#define VC_FINE {"FINE", F_INT, -64, 63, 0, 0, "ct"}
#define VC_SHAPE {"SHAPE", F_PCT, 0, 127, 0, 0, 0}
#define VC_KTRK {"KTRK", F_ONOFF, 0, 1, 1, 0, 0}
#define VC_SYNC {"SYNC", F_ONOFF, 0, 1, 0, 0, 0}
#define VC_RING {"RING", F_ONOFF, 0, 1, 0, 0, 0}
#define VC_MODE {"MODE", F_ENUM, 0, VOM_N - 1, VOM_BASIC, N_VA_OMODE, 0}
#define VC_FMORPH {"MORPH", F_PCT, 0, 127, 0, 0, 0}
#define VC_FSPREAD {"SPREAD", F_PCT, 0, 127, 0, 0, 0}
#define VC_USPREAD {"USPREAD", F_PCT, 0, 127, 0, 0, 0}
#define VC_FTYPE {"TYPE", F_ENUM, 0, 3, VF_LP, N_VA_FTYPE, 0}
#define VC_CUT {"CUT", F_CUTOFF, 0, 127, 100, 0, 0}
#define VC_RES {"RES", F_PCT, 0, 127, 0, 0, 0}
#define VC_DRIVE {"DRIVE", F_PCT, 0, 127, 0, 0, 0}
#define VC_FKTRK {"KTRK", F_PCT, 0, 127, 64, 0, 0}
#define VC_FENV {"FENV", F_BIPCT, -64, 63, 0, 0, 0}
#define VC_ATK {"ATK", F_TIME, 0, 127, 0, 0, 0}
#define VC_HOLD {"HOLD", F_TIME, 0, 127, 0, 0, 0}
#define VC_DEC {"DEC", F_TIME, 0, 127, 64, 0, 0}
#define VC_SUS {"SUS", F_PCT, 0, 127, 127, 0, 0}
#define VC_REL {"REL", F_TIME, 0, 127, 40, 0, 0}
#define VC_VEL {"VEL", F_PCT, 0, 127, 64, 0, 0}
#define VC_LRATE {"RATE", F_LFOHZ, 0, 127, 70, 0, 0}
#define VC_LWAVE {"WAVE", F_ENUM, 0, 4, 0, N_VA_LWAVE, 0}
#define VC_DEPTH {"DEPTH", F_PCT, 0, 127, 127, 0, 0}
#define VC_FADE {"FADE", F_TIME, 0, 127, 0, 0, 0}
#define VC_LSYNC(l) {l, F_ONOFF, 0, 1, 0, 0, 0}
#define VC_SRC {"SRC", F_ENUM, 0, VS_N - 1, 0, N_VA_SRC, 0}
#define VC_DST {"DST", F_ENUM, 0, VD_N - 1, 0, N_VA_DST, 0}
#define VC_AMT {"AMT", F_BIPCT, -64, 63, 0, 0, 0}
#define VC_NONE {0, 0, 0, 0, 0, 0, 0}
#define VA_X 0xFFu               /* an empty column */

static const eng_page_t VA_PAGES[] = {
    {"OSC 1", {VC_WAVE, VC_LEVEL, VC_COARSE, VC_FINE}},       /* 0  OSC */
    {"OSC 1+", {VC_MODE, VC_SHAPE, VC_KTRK, VC_NONE}},
    {"OSC 2", {VC_WAVE, VC_LEVEL, VC_COARSE, VC_FINE}},
    {"OSC 2+", {VC_MODE, VC_SHAPE, VC_KTRK, VC_SYNC}},
    {"OSC 3", {VC_WAVE, VC_LEVEL, VC_COARSE, VC_FINE}},
    {"OSC 3+", {VC_MODE, VC_SHAPE, VC_KTRK, VC_NONE}},
    {"OSC 4", {VC_WAVE, VC_LEVEL, VC_COARSE, VC_FINE}},
    {"OSC 4+", {VC_MODE, VC_SHAPE, VC_KTRK, VC_RING}},
    {"FILTER", {VC_FTYPE, VC_CUT, VC_RES, VC_DRIVE}},         /* 8  FILTER */
    {"FILTER+", {VC_FKTRK, VC_FENV, VC_FMORPH, VC_FSPREAD}},
    {"ENV 1", {VC_ATK, VC_DEC, VC_SUS, VC_REL}},              /* 10 ENV */
    {"ENV 1+", {VC_HOLD, VC_VEL, VC_NONE, VC_NONE}},
    {"ENV 2", {VC_ATK, VC_DEC, VC_SUS, VC_REL}},
    {"ENV 2+", {VC_HOLD, VC_NONE, VC_NONE, VC_NONE}},
    {"ENV 3", {VC_ATK, VC_DEC, VC_SUS, VC_REL}},
    {"ENV 3+", {VC_HOLD, VC_NONE, VC_NONE, VC_NONE}},
    {"ENV 4", {VC_ATK, VC_DEC, VC_SUS, VC_REL}},
    {"ENV 4+", {VC_HOLD, VC_NONE, VC_NONE, VC_NONE}},
    {"LFO 1", {VC_LRATE, VC_LWAVE, VC_DEPTH, VC_FADE}},       /* 18 LFO */
    {"LFO 2", {VC_LRATE, VC_LWAVE, VC_DEPTH, VC_FADE}},
    {"LFO 3", {VC_LRATE, VC_LWAVE, VC_DEPTH, VC_FADE}},
    {"LFO 4", {VC_LRATE, VC_LWAVE, VC_DEPTH, VC_FADE}},
    {"LFO SYN", {VC_LSYNC("SYNC1"), VC_LSYNC("SYNC2"), VC_LSYNC("SYNC3"), VC_LSYNC("SYNC4")}},
    {"VOICE", {VC_USPREAD, VC_NONE, VC_NONE, VC_NONE}},      /* 23 (the LFO section's last) */
    {"MOD 1", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},             /* 24 MOD */
    {"MOD 2", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
    {"MOD 3", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
    {"MOD 4", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
    {"MOD 5", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
    {"MOD 6", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
    {"MOD 7", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
    {"MOD 8", {VC_SRC, VC_DST, VC_AMT, VC_NONE}},
};
#define VA_NPAGES ((uint32_t)NELEM(VA_PAGES))
_Static_assert(NELEM(VA_PAGES) == 32, "VA pages");
/* the patch value of each page column, VA_X = none */
#define VA_OPG(k) {VA_OSC(k, VO_WAVE), VA_OSC(k, VO_LEVEL), VA_OSC(k, VO_COARSE), VA_OSC(k, VO_FINE)}
#define VA_EPG(k) {VA_ENV(k, VE_ATK), VA_ENV(k, VE_DEC), VA_ENV(k, VE_SUS), VA_ENV(k, VE_REL)}
#define VA_LPG(k) {VA_LFO(k, VL_RATE), VA_LFO(k, VL_WAVE), VA_LFO(k, VL_DEPTH), VA_LFO(k, VL_FADE)}
#define VA_MPG(k) {VA_MOD(k, VM_SRC), VA_MOD(k, VM_DST), VA_MOD(k, VM_AMT), VA_X}
static const uint8_t VA_MAP[NELEM(VA_PAGES)][4] = {
    VA_OPG(0), {VA_OMODE0, VA_OSC(0, VO_SHAPE), VA_OSC(0, VO_KTRK), VA_X},
    VA_OPG(1), {VA_OMODE0 + 1, VA_OSC(1, VO_SHAPE), VA_OSC(1, VO_KTRK), VA_SYNC2},
    VA_OPG(2), {VA_OMODE0 + 2, VA_OSC(2, VO_SHAPE), VA_OSC(2, VO_KTRK), VA_X},
    VA_OPG(3), {VA_OMODE0 + 3, VA_OSC(3, VO_SHAPE), VA_OSC(3, VO_KTRK), VA_RING4},
    {VA_FTYPE, VA_CUT, VA_RES, VA_DRIVE}, {VA_FKTRK, VA_FENV, VA_FMORPH, VA_FSPREAD},
    VA_EPG(0), {VA_ENV(0, VE_HOLD), VA_VEL, VA_X, VA_X},
    VA_EPG(1), {VA_ENV(1, VE_HOLD), VA_X, VA_X, VA_X},
    VA_EPG(2), {VA_ENV(2, VE_HOLD), VA_X, VA_X, VA_X},
    VA_EPG(3), {VA_ENV(3, VE_HOLD), VA_X, VA_X, VA_X},
    VA_LPG(0), VA_LPG(1), VA_LPG(2), VA_LPG(3),
    {VA_LFO(0, VL_SYNC), VA_LFO(1, VL_SYNC), VA_LFO(2, VL_SYNC), VA_LFO(3, VL_SYNC)},
    {VA_USPREAD, VA_X, VA_X, VA_X},
    VA_MPG(0), VA_MPG(1), VA_MPG(2), VA_MPG(3), VA_MPG(4), VA_MPG(5), VA_MPG(6), VA_MPG(7),
};

/* the range and the init value of patch value i */
typedef struct { int8_t min, max, def; } va_rng_t;
static va_rng_t va_range(uint32_t i)
{
    va_rng_t r = {0, 127, 0};
    if (i < VA_SYNC2) {
        uint32_t k = i / VO_N;
        switch (i % VO_N) {
        case VO_WAVE: r.max = VW_N - 1; break;
        case VO_LEVEL: r.def = k ? 0 : 100; break;
        case VO_COARSE: r.min = -24; r.max = 24; break;
        case VO_FINE: r.min = -64; r.max = 63; break;
        case VO_KTRK: r.max = 1; r.def = 1; break;
        default: break;
        }
    } else if (i == VA_SYNC2 || i == VA_RING4) {
        r.max = 1;
    } else if (i < VA_ENV0) {
        static const va_rng_t F[6] = {{0, 3, 0}, {0, 127, 100}, {0, 127, 0}, {0, 127, 0}, {0, 127, 64}, {-64, 63, 0}};
        r = F[i - VA_FTYPE];
    } else if (i < VA_VEL) {
        static const int8_t D[VE_N] = {0, 0, 64, 127, 40};
        r.def = D[(i - VA_ENV0) % VE_N];
        if ((i - VA_ENV0) % VE_N == VE_SUS && i >= VA_ENV(1, 0))
            r.def = 0;                                  /* ENV 2..4: a decay to 0 */
    } else if (i == VA_VEL) {
        r.def = 64;
    } else if (i < VA_MOD0) {
        static const va_rng_t L[VL_N] = {{0, 127, 70}, {0, 4, 0}, {0, 127, 127}, {0, 127, 0}, {0, 1, 0}};
        r = L[(i - VA_LFO0) % VL_N];
    } else if (i < VA_OMIX) {
        static const va_rng_t M[VM_N] = {{0, VS_N - 1, 0}, {0, VD_N - 1, 0}, {-64, 63, 0}};
        r = M[(i - VA_MOD0) % VM_N];
    } else if (i == VA_OMIX) {
        r.def = 64;
    } else if (i >= VA_OMODE0 && i < VA_FMORPH) {
        r.max = VOM_N - 1;
    }
    return r;
}

/* the state lives in the pool (zero-initialised; per-voice side state as PHYS's phys_slot): RAM is the scarce one */
static int8_t va_patch[VA_NPART][VA_NP] __attribute__((section(".pool")));   /* the parts' patches (main loop and
                                                                               * va_block write, the ISR reads) */
static int16_t va_mlast[VA_NPART][8] __attribute__((section(".pool")));      /* P_E0..P_E7 as last put in the patch */
static uint8_t va_user_pending;                  /* upreset.c up_values: a VA user slot + 1 is being loaded */
static int (*va_store_read)(uint32_t k, uint8_t *blob);   /* va_store.c: slot k's blob, 0 = there is one */
static int8_t va_pan_off[VA_NPART];              /* the matrix's PAN (fx.c mix_part adds it) */
static int8_t va_rate_off[VA_NPART][4] __attribute__((section(".pool")));   /* the matrix's RATE1..4 (the latest note) */
static struct {                                  /* per part: the LFOs */
    uint32_t ph[4], rnd[4];
    int16_t out[4];                              /* this block's value x DEPTH, Q15 bipolar */
    uint32_t pwm;                                /* PWM's own slow sweep */
    uint32_t wrnd;                               /* VINYL's wow: a slow random walk (va_block) */
    int32_t wow, wowt;                           /* .. its value and its target, Q15 */
    uint16_t wown;                               /* .. blocks to the next target */
} va_lfo[VA_NPART] __attribute__((section(".pool")));
typedef struct {
    uint32_t ph4;                                /* OSC 4 (OSC 1..3: voice_t.ph) */
    int32_t env[4];                              /* Q24 */
    uint32_t hold[4];                            /* HOLD progress, Q24 */
    int16_t lvl[4], shp[4];                      /* the ramps' ends: levels and shapes, Q15 */
    int32_t dc;                                  /* SYNC's DC blocker (a hard-synced wave has a mean), Q8 */
    int32_t nz[4][2];                            /* NOISE per oscillator: BROWN's integrator; VINYL's crackle, hiss */
    int32_t fr[2];                               /* SPREAD: the right channel's SVF (the left one: voice_t.s[0..1]) */
    uint8_t spr;                                 /* .. it runs (primed from the left one when SPREAD comes on) */
    uint16_t ticks;                              /* control ticks since the note-on (LFO FADE) */
    uint8_t stage[4];                            /* 0 off, 1 attack, 2 hold, 3 decay / sustain, 4 release */
    uint8_t live;
} va_voice_t;
static va_voice_t va_vs[VA_NPART][NVOICE] __attribute__((section(".pool")));

static const uint8_t VA_MACRO[8] = {VA_CUT, VA_RES, VA_FENV, VA_DRIVE, VA_OMIX, VA_DETUNE, VA_ENV(0, VE_ATK),
                                    VA_ENV(0, VE_REL)};

static uint32_t va_tr(const track_t *t) { return (uint32_t)(t - trk); }

static void va_init_patch(int8_t *p)
{
    uint32_t i;
    for (i = 0; i < VA_NP; i++)
        p[i] = va_range(i).def;
}

static int8_t va_clampv(uint32_t i, int32_t v)
{
    va_rng_t r = va_range(i);
    return (int8_t)(v < r.min ? r.min : v > r.max ? r.max : v);
}

/* the patch -> blob (7-bit bytes: value - min) */
static void va_pack(const int8_t *p, uint8_t *b)
{
    uint32_t i;
    b[0] = VA_MAGIC;
    b[1] = VA_VER;
    for (i = 0; i < VA_NP; i++)
        b[2 + i] = (uint8_t)(va_clampv(i, p[i]) - va_range(i).min);
    for (i = 2 + VA_NP; i < VA_BLOB; i++)
        b[i] = 0;
}

/* a valid blob: version 2 (VA_BLOB bytes, VA_NP values) or version 1 (its first VA_BLOB1 bytes, VA_NP1 values; the
 * ranges of version 1's values only grew: SRC / DST gained names at the end) */
static int va_blob_ok(const uint8_t *b)
{
    uint32_t i, np, len;
    if (!b || b[0] != VA_MAGIC || (b[1] != VA_VER && b[1] != 1u))
        return 0;
    np = b[1] == 1u ? VA_NP1 : VA_NP;
    len = b[1] == 1u ? VA_BLOB1 : VA_BLOB;
    for (i = 0; i < np; i++) {
        va_rng_t r = va_range(i);
        if (b[2 + i] > (uint32_t)(r.max - r.min))
            return 0;
    }
    for (i = 2 + np; i < len; i++)
        if (b[i])
            return 0;
    return 1;
}

/* blob -> patch; 0 or a bad blob: the init patch. 1 = the blob was taken (version 1: the values version 2 added
 * take their init values: MODE BASIC, MORPH / SPREAD / USPREAD 0, the sound as it was) */
static int va_unpack(const uint8_t *b, int8_t *p)
{
    uint32_t i, np;
    va_init_patch(p);
    if (!va_blob_ok(b))
        return 0;
    np = b[1] == 1u ? VA_NP1 : VA_NP;
    for (i = 0; i < np; i++)
        p[i] = (int8_t)(b[2 + i] + va_range(i).min);
    return 1;
}

/* the macros (P_E0..P_E7) from the patch: written into the track and remembered (main loop) */
static void va_macros_out(track_t *t)
{
    uint32_t tr = va_tr(t), k;
    if (tr >= VA_NPART)
        return;
    for (k = 0; k < 8u; k++) {
        int16_t v = va_patch[tr][VA_MACRO[k]];
        t->p[P_E0 + k] = v;
        va_mlast[tr][k] = v;
    }
}

/* ------------------------------------------------------------ presets --- */
/* a preset: the init patch with these (index, value) pairs, 0xFF ends */
#define O_(k, f) VA_OSC(k, VO_##f)
#define E_(k, f) VA_ENV(k, VE_##f)
#define L_(k, f) VA_LFO(k, VL_##f)
#define M_(k, s, d, a) VA_MOD(k, VM_SRC), (s), VA_MOD(k, VM_DST), (d), VA_MOD(k, VM_AMT), (uint8_t)(a)
#define S8(v) (uint8_t)(int8_t)(v)
#define ENV1(a, d, s, r) E_(0, ATK), a, E_(0, DEC), d, E_(0, SUS), s, E_(0, REL), r
#define ENV2(a, d, s, r) E_(1, ATK), a, E_(1, DEC), d, E_(1, SUS), s, E_(1, REL), r
#define ENV3(a, d, s, r) E_(2, ATK), a, E_(2, DEC), d, E_(2, SUS), s, E_(2, REL), r
#define OSC(k, w, l, c, f) O_(k, WAVE), VW_##w, O_(k, LEVEL), l, O_(k, COARSE), S8(c), O_(k, FINE), S8(f)
#define FLT(ty, c, r, fe) VA_FTYPE, VF_##ty, VA_CUT, c, VA_RES, r, VA_FENV, S8(fe)
#define LFO(k, r, w, d, f) L_(k, RATE), r, L_(k, WAVE), w, L_(k, DEPTH), d, L_(k, FADE), f
#define MODE(k, m) VA_OMODE0 + (k), VOM_##m
static const uint8_t VAP_LUSH[] = {OSC(0, SAW, 62, 0, -7), OSC(1, SAW, 62, 0, 7), OSC(2, SAW, 40, 12, 3),
    FLT(LP, 70, 14, 10), VA_FKTRK, 64, ENV1(86, 90, 120, 98), ENV2(88, 100, 60, 95), VA_VEL, 50,
    LFO(0, 50, 0, 40, 0), LFO(1, 84, 0, 20, 92), M_(0, VS_LFO1, VD_CUT, 8), M_(1, VS_LFO1 + 1, VD_PITCH, 3), 0xFF};
static const uint8_t VAP_WARM[] = {OSC(0, PWM, 80, 0, 0), O_(0, SHAPE), 60, OSC(1, TRI, 56, 0, 5),
    FLT(LP, 62, 10, 6), ENV1(80, 90, 115, 92), ENV2(80, 100, 50, 92), VA_VEL, 60,
    M_(0, VS_VEL, VD_CUT, 18), 0xFF};
static const uint8_t VAP_GLASS[] = {OSC(0, SIN, 70, 0, 0), O_(0, SHAPE), 30, OSC(1, TRI, 48, 12, 0),
    OSC(2, SIN, 26, 19, 4), FLT(LP, 104, 22, 0), ENV1(70, 100, 100, 100), ENV3(90, 105, 40, 100),
    LFO(0, 40, 1, 60, 0), M_(0, VS_ENV1 + 2, VD_SHP1, 30), M_(1, VS_LFO1, VD_SHP1, 10), 0xFF};
static const uint8_t VAP_SLOWSTR[] = {OSC(0, SAW, 68, 0, -5), OSC(1, SAW, 68, 0, 5), OSC(2, PWM, 38, 0, 0),
    O_(2, SHAPE), 70, FLT(LP, 76, 8, 12), VA_FKTRK, 80, ENV1(92, 90, 118, 96), ENV2(95, 100, 80, 96),
    LFO(0, 84, 0, 25, 95), M_(0, VS_LFO1, VD_PITCH, 2), 0xFF};
static const uint8_t VAP_ENSEMBLE[] = {OSC(0, SAW, 56, 0, -10), OSC(1, SAW, 56, 0, 10), OSC(2, SAW, 50, 0, -3),
    OSC(3, SAW, 50, 0, 4), FLT(LP, 82, 5, 0), ENV1(80, 90, 120, 92), VA_DETUNE, 30,
    LFO(0, 60, 0, 60, 0), LFO(1, 66, 1, 60, 0), M_(0, VS_LFO1, VD_PIT1, 2), M_(1, VS_LFO1 + 1, VD_PIT1 + 1, -2), 0xFF};
static const uint8_t VAP_BRASS[] = {OSC(0, SAW, 72, 0, -4), OSC(1, SAW, 72, 0, 4), FLT(LP, 52, 20, 40),
    ENV1(50, 80, 110, 70), ENV2(62, 85, 60, 80), VA_VEL, 80, M_(0, VS_VEL, VD_CUT, 15), 0xFF};
static const uint8_t VAP_SOFTBRASS[] = {OSC(0, SAW, 78, 0, 0), OSC(1, SQR, 46, 0, 6), O_(1, SHAPE), 20,
    FLT(LP, 50, 10, 28), ENV1(66, 85, 112, 74), ENV2(74, 90, 70, 80), 0xFF};
static const uint8_t VAP_POLYKEYS[] = {OSC(0, SAW, 86, 0, 0), OSC(1, SQR, 52, 12, 0), O_(1, SHAPE), 30,
    FLT(LP, 58, 18, 38), ENV1(0, 92, 70, 72), ENV2(0, 82, 30, 70), VA_VEL, 90, M_(0, VS_VEL, VD_CUT, 20), 0xFF};
static const uint8_t VAP_PWMKEYS[] = {OSC(0, PWM, 90, 0, 0), O_(0, SHAPE), 80, FLT(LP, 72, 12, 22),
    ENV1(12, 90, 90, 70), ENV2(0, 80, 40, 70), 0xFF};
static const uint8_t VAP_CLAV[] = {OSC(0, SQR, 84, 0, 0), O_(0, SHAPE), 70, OSC(1, SAW, 36, 12, 0),
    FLT(LP, 60, 55, 45), ENV1(0, 80, 0, 50), ENV2(0, 64, 0, 50), VA_VEL, 100, M_(0, VS_VEL, VD_CUT, 25), 0xFF};
static const uint8_t VAP_SOFTLEAD[] = {OSC(0, SQR, 80, 0, 0), O_(0, SHAPE), 10, OSC(1, SAW, 46, 0, 8),
    FLT(LP, 70, 25, 20), ENV1(30, 80, 115, 70), ENV2(20, 90, 60, 70), LFO(0, 86, 0, 40, 92),
    M_(0, VS_LFO1, VD_PITCH, 4), 0xFF};
static const uint8_t VAP_HOLLOW[] = {OSC(0, SQR, 0, 0, 0), OSC(1, SAW, 82, 7, 0), VA_SYNC2, 1, OSC(2, TRI, 46, 0, 0),
    FLT(LP, 90, 10, 0), ENV1(10, 85, 100, 80), ENV3(0, 90, 30, 80), M_(0, VS_ENV1 + 2, VD_PIT1 + 1, 20), 0xFF};
static const uint8_t VAP_BELLS[] = {OSC(0, SIN, 56, 0, 0), OSC(2, SIN, 0, 16, 12), OSC(3, SIN, 80, 0, 0),
    VA_RING4, 1, FLT(LP, 112, 0, 0), ENV1(0, 105, 0, 100), VA_VEL, 90, 0xFF};
static const uint8_t VAP_SWEEP[] = {OSC(0, SAW, 67, 0, -6), OSC(1, SAW, 67, 0, 6), OSC(2, SQR, 38, 12, 0),
    FLT(LP, 40, 50, 30), ENV1(84, 90, 120, 100), ENV2(105, 110, 50, 100), LFO(0, 34, 1, 80, 0),
    M_(0, VS_LFO1, VD_CUT, 20), 0xFF};
static const uint8_t VAP_AAH[] = {OSC(0, SAW, 60, 0, 0), OSC(1, SAW, 60, 0, 9), OSC(2, TRI, 52, 12, 0),
    FLT(BP, 76, 40, 0), ENV1(84, 90, 118, 92), LFO(0, 58, 0, 60, 0), LFO(1, 84, 0, 30, 95),
    M_(0, VS_LFO1, VD_CUT, 6), M_(1, VS_LFO1 + 1, VD_PITCH, 2), 0xFF};
static const uint8_t VAP_ORGAN[] = {OSC(0, SIN, 59, 0, 0), OSC(1, SIN, 45, 12, 0), OSC(2, SIN, 30, 19, 0),
    OSC(3, SIN, 22, 24, 0), FLT(LP, 127, 0, 0), ENV1(4, 60, 127, 30), VA_VEL, 0, 0xFF};
static const uint8_t VAP_SUB[] = {OSC(0, SIN, 104, 0, 0), OSC(1, TRI, 28, 0, 0), FLT(LP, 60, 0, 0),
    ENV1(2, 80, 120, 40), VA_VEL, 40, 0xFF};
static const uint8_t VAP_PUNCH[] = {OSC(0, SAW, 88, 0, 0), OSC(1, SQR, 64, -12, 0), FLT(LP, 45, 25, 45),
    VA_DRIVE, 30, ENV1(0, 85, 90, 45), ENV2(0, 70, 10, 50), 0xFF};
static const uint8_t VAP_RUBBER[] = {OSC(0, SQR, 110, 0, 0), O_(0, SHAPE), 40, FLT(LP, 36, 70, 35),
    ENV1(0, 90, 80, 40), ENV2(0, 76, 0, 50), VA_VEL, 70, 0xFF};
static const uint8_t VAP_SYNCBASS[] = {OSC(0, SAW, 64, 0, 0), OSC(1, SAW, 100, 12, 0), VA_SYNC2, 1,
    FLT(LP, 70, 20, 25), ENV1(0, 85, 100, 40), ENV3(0, 80, 0, 50), M_(0, VS_ENV1 + 2, VD_PIT1 + 1, 30), 0xFF};
/* version 2: the oscillator modes, FILTER MORPH / SPREAD */
static const uint8_t VAP_MORPHPAD[] = {OSC(0, SAW, 66, 0, -6), MODE(0, MORPH), O_(0, SHAPE), 44,
    OSC(1, SAW, 60, 0, 6), MODE(1, MORPH), O_(1, SHAPE), 56, OSC(2, SIN, 30, -12, 0),
    FLT(LP, 78, 12, 8), VA_FSPREAD, 40, ENV1(84, 90, 118, 96), ENV2(90, 100, 60, 96), VA_VEL, 50,
    LFO(0, 30, 1, 127, 0), LFO(1, 38, 0, 127, 0), M_(0, VS_LFO1, VD_SHP1, 22), M_(1, VS_LFO1 + 1, VD_SHP1 + 1, -20),
    0xFF};
static const uint8_t VAP_VINYLKEYS[] = {OSC(0, TRI, 84, 0, 0), OSC(1, SIN, 40, 12, 3),
    O_(3, WAVE), VN_VINYL, MODE(3, NOISE), O_(3, LEVEL), 50, O_(3, SHAPE), 40, O_(3, KTRK), 0,
    FLT(LP, 72, 10, 18), ENV1(6, 92, 84, 76), ENV2(0, 86, 40, 76), VA_VEL, 70, M_(0, VS_VEL, VD_CUT, 14), 0xFF};
static const uint8_t VAP_WIDESTR[] = {OSC(0, SAW, 54, 0, -10), OSC(1, SAW, 54, 0, 10), OSC(2, SAW, 48, 0, -3),
    OSC(3, SAW, 48, 0, 4), FLT(LP, 80, 8, 6), VA_FSPREAD, 110, ENV1(86, 90, 120, 94), ENV2(90, 100, 70, 94),
    VA_DETUNE, 30, LFO(0, 60, 0, 60, 0), LFO(1, 66, 1, 60, 0), M_(0, VS_LFO1, VD_PIT1, 2),
    M_(1, VS_LFO1 + 1, VD_PIT1 + 1, -2), 0xFF};

/* {CUT, RES, FENV, DRIVE, MIX, DTN, ATK, REL}: the macros as the patch has them (cr_va_test checks) */
static const preset_t VA_PRESETS[] = {
    {"LUSH PAD", {70, 14, 10, 0, 64, 0, 86, 98}, {86, 90, 120, 98}, 0, 0, FX(0, 60, 20, 70), PAT(5)},
    {"WARM PAD", {62, 10, 6, 0, 64, 0, 80, 92}, {80, 90, 115, 92}, 0, 0, FX(0, 50, 20, 65), PAT(5)},
    {"GLASS PAD", {104, 22, 0, 0, 64, 0, 70, 100}, {70, 100, 100, 100}, 0, 0, FX(0, 40, 30, 75), PAT(5)},
    {"SLOW STRINGS", {76, 8, 12, 0, 64, 0, 92, 96}, {92, 90, 118, 96}, 0, 0, FX(0, 55, 15, 70), PAT(5)},
    {"ENSEMBLE STR", {82, 5, 0, 0, 64, 30, 80, 92}, {80, 90, 120, 92}, 0, 0, FX(0, 70, 15, 65), PAT(5)},
    {"SYNTH BRASS", {52, 20, 40, 0, 64, 0, 50, 70}, {50, 80, 110, 70}, 0, 0, FX(0, 25, 20, 40), PAT(6)},
    {"SOFT BRASS", {50, 10, 28, 0, 64, 0, 66, 74}, {66, 85, 112, 74}, 0, 0, FX(0, 30, 20, 50), PAT(6)},
    {"POLY KEYS", {58, 18, 38, 0, 64, 0, 0, 72}, {0, 92, 70, 72}, 0, 0, FX(0, 35, 25, 40), PAT(6)},
    {"PWM KEYS", {72, 12, 22, 0, 64, 0, 12, 70}, {12, 90, 90, 70}, 0, 0, FX(0, 40, 25, 40), PAT(6)},
    {"CLAV", {60, 55, 45, 0, 64, 0, 0, 50}, {0, 80, 0, 50}, 0, 0, FX(0, 15, 20, 25), PAT(6)},
    {"SOFT LEAD", {70, 25, 20, 0, 64, 0, 30, 70}, {30, 80, 115, 70}, 0, 0, FX(0, 25, 40, 40), PAT(4)},
    {"HOLLOW", {90, 10, 0, 0, 64, 0, 10, 80}, {10, 85, 100, 80}, 0, 0, FX(0, 35, 25, 50), PAT(6)},
    {"BELLS", {112, 0, 0, 0, 64, 0, 0, 100}, {0, 105, 0, 100}, 0, 0, FX(0, 20, 35, 60), PAT(7)},
    {"SWEEP PAD", {40, 50, 30, 0, 64, 0, 84, 100}, {84, 90, 120, 100}, 0, 0, FX(0, 55, 25, 70), PAT(5)},
    {"SOFT AAH", {76, 40, 0, 0, 64, 0, 84, 92}, {84, 90, 118, 92}, 0, 0, FX(0, 50, 15, 70), PAT(5)},
    {"ORGANISH", {127, 0, 0, 0, 64, 0, 4, 30}, {4, 60, 127, 30}, 0, 0, FX(0, 45, 0, 35), PAT(6)},
    {"DEEP SUB", {60, 0, 0, 0, 64, 0, 2, 40}, {2, 80, 120, 40}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PUNCH BASS", {45, 25, 45, 30, 64, 0, 0, 45}, {0, 85, 90, 45}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"RUBBER BASS", {36, 70, 35, 0, 64, 0, 0, 40}, {0, 90, 80, 40}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"SYNC BASS", {70, 20, 25, 0, 64, 0, 0, 40}, {0, 85, 100, 40}, 0, 1, FX(0, 0, 15, 10), PAT(1)},
    {"MORPH PAD", {78, 12, 8, 0, 64, 0, 84, 96}, {84, 90, 118, 96}, 0, 0, FX(0, 50, 25, 70), PAT(5)},
    {"VINYL KEYS", {72, 10, 18, 0, 64, 0, 6, 76}, {6, 92, 84, 76}, 0, 0, FX(0, 30, 20, 45), PAT(6)},
    {"WIDE STRINGS", {80, 8, 6, 0, 64, 30, 86, 94}, {86, 90, 120, 94}, 0, 0, FX(0, 55, 15, 65), PAT(5)},
};
static const uint8_t *const VA_PRESET_EDITS[] = {VAP_LUSH, VAP_WARM, VAP_GLASS, VAP_SLOWSTR, VAP_ENSEMBLE, VAP_BRASS,
    VAP_SOFTBRASS, VAP_POLYKEYS, VAP_PWMKEYS, VAP_CLAV, VAP_SOFTLEAD, VAP_HOLLOW, VAP_BELLS, VAP_SWEEP, VAP_AAH,
    VAP_ORGAN, VAP_SUB, VAP_PUNCH, VAP_RUBBER, VAP_SYNCBASS, VAP_MORPHPAD, VAP_VINYLKEYS, VAP_WIDESTR};
_Static_assert(NELEM(VA_PRESETS) == NELEM(VA_PRESET_EDITS) && NELEM(VA_PRESETS) == 23, "a patch per VA preset");
#define VA_NPRESETS ((uint32_t)NELEM(VA_PRESETS))
#undef O_
#undef E_
#undef L_
#undef M_
#undef S8
#undef ENV1
#undef ENV2
#undef ENV3
#undef OSC
#undef FLT
#undef LFO
#undef MODE

static void va_preset_patch(uint32_t k, int8_t *p)
{
    const uint8_t *e;
    va_init_patch(p);
    if (k >= VA_NPRESETS)
        return;
    for (e = VA_PRESET_EDITS[k]; *e != 0xFFu; e += 2)
        p[e[0]] = va_clampv(e[0], (int8_t)e[1]);
}

/* ------------------------------------------------- deep pages, the blob --- */
static int32_t va_get(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = va_tr(t), i;
    if (page >= VA_NPAGES || col >= 4u || (i = VA_MAP[page][col]) == VA_X)
        return 0;
    return tr < VA_NPART ? va_patch[tr][i] : va_range(i).def;
}

/* the deep pages' mode-dependent columns (eng_deep_t.desc): on OSC n, WAVE names the morph position (MORPH: WAVE
 * itself does nothing) or the noise type (NOISE); on OSC n+, SHAPE is MORPH / COLOR (BROWN) / DENS (VINYL) */
static const char *const N_VA_MPV[11][VW_N] = {   /* (each a full WAVE range of one name) */
    {"SIN", "SIN", "SIN", "SIN", "SIN", "SIN"},
    {"SIN>TRI", "SIN>TRI", "SIN>TRI", "SIN>TRI", "SIN>TRI", "SIN>TRI"},
    {"TRI", "TRI", "TRI", "TRI", "TRI", "TRI"},
    {"TRI>SAW", "TRI>SAW", "TRI>SAW", "TRI>SAW", "TRI>SAW", "TRI>SAW"},
    {"SAW", "SAW", "SAW", "SAW", "SAW", "SAW"},
    {"SAW>RMP", "SAW>RMP", "SAW>RMP", "SAW>RMP", "SAW>RMP", "SAW>RMP"},
    {"RAMP", "RAMP", "RAMP", "RAMP", "RAMP", "RAMP"},
    {"RMP>SQR", "RMP>SQR", "RMP>SQR", "RMP>SQR", "RMP>SQR", "RMP>SQR"},
    {"SQR", "SQR", "SQR", "SQR", "SQR", "SQR"},
    {"SQR>PLS", "SQR>PLS", "SQR>PLS", "SQR>PLS", "SQR>PLS", "SQR>PLS"},
    {"PULSE", "PULSE", "PULSE", "PULSE", "PULSE", "PULSE"},
};
#define VA_MPV(k) {"MORPH", F_ENUM, 0, VW_N - 1, VW_SAW, N_VA_MPV[k], 0}
static const param_desc_t VA_D_MORPHPV[11] = {VA_MPV(0), VA_MPV(1), VA_MPV(2), VA_MPV(3), VA_MPV(4), VA_MPV(5),
                                              VA_MPV(6), VA_MPV(7), VA_MPV(8), VA_MPV(9), VA_MPV(10)};
#undef VA_MPV
static const param_desc_t VA_D_NTYPE = {"NTYPE", F_ENUM, 0, VN_N - 1, VN_WHITE, N_VA_NTYPE, 0};
static const param_desc_t VA_D_SHAPE[4] = {{"MORPH", F_PCT, 0, 127, 0, 0, 0}, {"COLOR", F_PCT, 0, 127, 0, 0, 0},
                                           {"DENS", F_PCT, 0, 127, 0, 0, 0}, {"SHAPE", F_PCT, 0, 127, 0, 0, 0}};
/* the morph position's name: the shape at 0 24 48 72 96 127, "A>B" between them */
static uint32_t va_morph_name(int32_t sh)
{
    int32_t seg = sh / 24, f = sh % 24;
    if (sh >= 96)
        return sh < 100 ? 8u : sh < 124 ? 9u : 10u;
    return (uint32_t)(f < 4 ? 2 * seg : f > 20 ? 2 * seg + 2 : 2 * seg + 1);
}
static const param_desc_t *va_desc(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = va_tr(t), k = page >> 1, mode;
    if (page >= 8u || tr >= VA_NPART || col != (page & 1u))   /* OSC n: WAVE (col 0); OSC n+: SHAPE (col 1) */
        return 0;
    mode = (uint32_t)va_patch[tr][VA_OMODE0 + k];
    if (!(page & 1u)) {
        if (mode == VOM_MORPH)
            return &VA_D_MORPHPV[va_morph_name(va_patch[tr][VA_OSC(k, VO_SHAPE)])];
        return mode == VOM_NOISE ? &VA_D_NTYPE : 0;
    }
    if (mode == VOM_MORPH)
        return &VA_D_SHAPE[0];
    if (mode == VOM_NOISE)
        return va_patch[tr][VA_OSC(k, VO_WAVE)] == VN_BROWN ? &VA_D_SHAPE[1]
             : va_patch[tr][VA_OSC(k, VO_WAVE)] == VN_VINYL ? &VA_D_SHAPE[2] : &VA_D_SHAPE[3];
    return 0;
}

static void va_set(track_t *t, uint32_t page, uint32_t col, int32_t v)
{
    uint32_t tr = va_tr(t), i, k;
    int32_t old;
    if (tr >= VA_NPART || page >= VA_NPAGES || col >= 4u || (i = VA_MAP[page][col]) == VA_X)
        return;
    if (i < VA_SYNC2 && i % VO_N == VO_WAVE && va_patch[tr][VA_OMODE0 + i / VO_N] == VOM_NOISE)
        v = clamp(v, 0, VN_N - 1);               /* NOISE: WAVE is the noise type */
    old = va_patch[tr][i];
    va_patch[tr][i] = va_clampv(i, v);
    if (i >= VA_OMODE0 && i < VA_FMORPH && va_patch[tr][i] == VOM_NOISE && old != VOM_NOISE) {   /* -> NOISE: a type,
                                                                                                * no key tracking */
        k = i - VA_OMODE0;
        if (va_patch[tr][VA_OSC(k, VO_WAVE)] >= VN_N)
            va_patch[tr][VA_OSC(k, VO_WAVE)] = VN_WHITE;
        va_patch[tr][VA_OSC(k, VO_KTRK)] = 0;
    }
    for (k = 0; k < 8u; k++)                     /* a macro's value: the track's P_E too (va_block sees no change) */
        if (VA_MACRO[k] == i) {
            t->p[P_E0 + k] = va_patch[tr][i];
            RING_PUBLISH();
            va_mlast[tr][k] = va_patch[tr][i];
        }
}

static void va_blob_get(const track_t *t, uint8_t *out)
{
    uint32_t tr = va_tr(t);
    int8_t p[VA_NP];
    if (tr < VA_NPART)
        va_pack(va_patch[tr], out);
    else {
        va_init_patch(p);
        va_pack(p, out);
    }
}

static void va_blob_set(track_t *t, const uint8_t *in)
{
    uint32_t tr = va_tr(t);
    int8_t p[VA_NP];
    if (tr >= VA_NPART)
        return;
    va_unpack(in, p);
    memcpy(va_patch[tr], p, VA_NP);
    va_macros_out(t);
}

static void va_blob_preset(track_t *t, uint32_t k)
{
    uint32_t tr = va_tr(t);
    int8_t p[VA_NP];
    if (tr >= VA_NPART)
        return;
    va_preset_patch(k, p);
    memcpy(va_patch[tr], p, VA_NP);
    va_macros_out(t);
}

/* a sound load put VA into track t (eng_fm6.c fm6_track_loaded calls it on every load path: a factory preset, a
 * user slot, INIT, an engine switch): its patch. A user slot (va_user_pending, set by upreset.c up_values): its
 * stored patch (va_store.c), else a preset whose macros the track holds: its patch, else the init patch with the
 * track's macros on it (INIT; a user slot with no stored patch keeps its macros) */
static void va_track_loaded(const track_t *ct)
{
    track_t *t = (track_t *)ct;
    uint32_t tr = va_tr(t), pend = va_user_pending, k;
    uint8_t b[VA_BLOB];
    int8_t p[VA_NP];
    va_user_pending = 0;
    if (tr >= VA_NPART || t->eng_req != ENGI_VA)
        return;
    if (pend && va_store_read && !va_store_read(pend - 1u, b) && va_unpack(b, p)) {
        memcpy(va_patch[tr], p, VA_NP);
        va_macros_out(t);
        return;
    }
    if (!pend && t->preset < VA_NPRESETS) {
        for (k = 0; k < 8u && t->p[P_E0 + k] == VA_PRESETS[t->preset].e[k]; k++)
            ;
        if (k == 8u) {
            va_blob_preset(t, t->preset);
            return;
        }
    }
    va_init_patch(p);
    for (k = 0; k < 8u; k++)
        p[VA_MACRO[k]] = va_clampv(VA_MACRO[k], t->p[P_E0 + k]);
    memcpy(va_patch[tr], p, VA_NP);
    va_macros_out(t);
}

/* ------------------------------------------------------------ the ISR --- */
/* LFO SYNC: a division's phase increment per control tick and BPM, Q4 (2^32 * CTL / (FS * 60 * beats) * 16; at
 * 300 BPM the largest is 2.0e9) */
static const uint32_t VA_DIV_Q4[VA_NDIV] = {25971, 51942, 103884, 207769, 415537, 554050, 831075, 1246612, 1108099,
                                            1662149, 2493224, 3324298, 4986447, 6648596};

static uint32_t va_lfo_inc(const int8_t *p, uint32_t l, int32_t rate)
{
    rate = clamp(rate, 0, 127);
    if (p[VA_LFO(l, VL_SYNC)]) {
        int32_t bpm = clamp(song.g[G_BPM], 20, 300);
        return (VA_DIV_Q4[(uint32_t)rate * VA_NDIV >> 7] * (uint32_t)bpm) >> 4;
    }
    return LFO_INC[rate];
}

static uint32_t va_xs(uint32_t s)                /* xorshift32 (S&H; its own: the shared rng is not touched) */
{
    s = s ? s : 0x6C8E9CF5u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* once a block and part: the macros into the patch, the LFOs */
static void va_block(track_t *t)
{
    uint32_t tr = va_tr(t), k;
    int8_t *p;
    if (tr >= VA_NPART)
        return;
    p = va_patch[tr];
    for (k = 0; k < 8u; k++)                     /* a knob, the editor, the matrix, motion moved a macro */
        if (t->p[P_E0 + k] != va_mlast[tr][k]) {
            va_mlast[tr][k] = t->p[P_E0 + k];
            p[VA_MACRO[k]] = va_clampv(VA_MACRO[k], t->p[P_E0 + k]);
        }
    for (k = 0; k < 4u; k++) {
        uint32_t old = va_lfo[tr].ph[k], ph;
        int32_t x;
        ph = va_lfo[tr].ph[k] = old + va_lfo_inc(p, k, p[VA_LFO(k, VL_RATE)] + va_rate_off[tr][k]);
        if (ph < old)
            va_lfo[tr].rnd[k] = va_xs(va_lfo[tr].rnd[k] + k);
        switch (p[VA_LFO(k, VL_WAVE)]) {
        case 1: x = osc_tri(ph); break;
        case 2: x = (int32_t)(ph >> 16) - 32768; break;
        case 3: x = ph < 0x80000000u ? 32767 : -32767; break;
        case 4: x = (int32_t)(va_lfo[tr].rnd[k] >> 16) - 32768; break;
        default: x = osc_sine(ph); break;
        }
        va_lfo[tr].out[k] = (int16_t)((x * p[VA_LFO(k, VL_DEPTH)] * 258) >> 15);
    }
    va_lfo[tr].pwm += LFO_INC[60];               /* PWM's own sweep: ~0.8 Hz */
    if (!va_lfo[tr].wown--) {                    /* VINYL's wow: a new target every 0.3 .. 0.75 s, eased into */
        va_lfo[tr].wrnd = va_xs(va_lfo[tr].wrnd + 7u);
        va_lfo[tr].wowt = (int32_t)(va_lfo[tr].wrnd >> 16) - 32768;
        va_lfo[tr].wown = (uint16_t)(400u + (va_lfo[tr].wrnd & 511u));
    }
    va_lfo[tr].wow += (va_lfo[tr].wowt - va_lfo[tr].wow) >> 8;
}

static va_voice_t *va_voice(track_t *t, voice_t *v)
{
    uint32_t tr = va_tr(t), i = (uint32_t)(v - t->v);
    return tr < VA_NPART && i < NVOICE ? &va_vs[tr][i] : 0;
}

static void va_note_on(track_t *t, voice_t *v)
{
    va_voice_t *s = va_voice(t, v);
    uint32_t k, i, fresh, any = 0;
    if (!s)
        return;
    fresh = (!v->env && !v->env_out) || !s->live;
    for (i = 0; i < NVOICE; i++)                 /* a fresh phrase: the LFOs restart (as Felucca's) */
        any |= &t->v[i] != v && t->v[i].gate;
    if (!any)
        for (k = 0; k < 4u; k++)
            va_lfo[va_tr(t)].ph[k] = 0;
    if (fresh) {                                 /* from silence: phases, filter, envelopes, ramps from 0 */
        uint32_t sp = v->age * 0x9E3779B9u;
        v->ph[0] = 0;
        v->ph[1] = sp;
        v->ph[2] = sp * 3u;
        s->ph4 = sp * 5u;
        v->s[0] = v->s[1] = 0;
        if (!v->s[2])
            v->s[2] = 0x2545F491 + (int32_t)v->age;
        for (k = 0; k < 4u; k++) {
            s->env[k] = 0;
            s->lvl[k] = s->shp[k] = 0;
            v->s[3 + k] = 0;                     /* (no inc yet: the first tick starts at its target) */
        }
        v->s[7] = 0;
        s->dc = 0;
        memset(s->nz, 0, sizeof s->nz);
        s->fr[0] = s->fr[1] = 0;
        s->spr = 0;
    }
    for (k = 0; k < 4u; k++) {                   /* a retrigger: the attack from the current level */
        s->stage[k] = 1;
        s->hold[k] = 0;
    }
    s->ticks = 0;
    s->live = 1;
}

/* one control tick of envelope k: Q15 */
static int32_t va_env_tick(const int8_t *p, va_voice_t *s, uint32_t k, uint32_t gate)
{
    const int8_t *e = p + VA_ENV(k, 0);
    int32_t x = s->env[k];
    if (!gate && s->stage[k] && s->stage[k] < 4u)
        s->stage[k] = 4;
    switch (s->stage[k]) {
    case 1:
        x += (int32_t)ENV_LIN[e[VE_ATK] & 127];
        if (x >= (1 << 24)) {
            x = 1 << 24;
            s->stage[k] = e[VE_HOLD] ? 2 : 3;
        }
        break;
    case 2:
        s->hold[k] += ENV_LIN[e[VE_HOLD] & 127];
        if (s->hold[k] >= (1u << 24))
            s->stage[k] = 3;
        break;
    case 3:
        x += mulq16(((int32_t)e[VE_SUS] << 17) - x, ENV_EXP[e[VE_DEC] & 127]);
        break;
    case 4:
        x -= mulq16(x, ENV_EXP[e[VE_REL] & 127]);
        if (x < (1 << 12)) {
            x = 0;
            s->stage[k] = 0;
        }
        break;
    default:
        x = 0;
        break;
    }
    s->env[k] = x;
    return x >> 9;
}

static int va_done(track_t *t, voice_t *v)      /* ENV 1 has ended (voice.c, engine_t.done) */
{
    va_voice_t *s = va_voice(t, v);
    if (!s || !s->live || !s->stage[0]) {
        if (s)
            s->live = 0;
        return 1;
    }
    return 0;
}

/* one sample of wave w at phase ph (increment inc, shape sh Q15, pw the pulse width), +-32767 */
#define VA_PULSE_PW(sh) (0x80000000u - (uint32_t)(sh) * 0xE666u)   /* SHAPE: 50 % .. ~5 % */

/* MORPH: sine (SHAPE 0) -> triangle (24) -> saw (48) -> ramp (72) -> square (96) -> a narrowing pulse (127: BASIC
 * SQR's width at SHAPE 127); between two neighbours a linear crossfade of the band-limited waves (polyBLEP saw, ramp
 * and square). The waves are aligned on the saw's fundamental (SAW and SQR are BASIC's own; the sine is shifted by
 * half a cycle, the triangle by three quarters, the ramp is the saw reversed), so no crossfade cancels the
 * fundamental. sh: Q15 (SHAPE x 258) */
#define VA_MP(k) ((k) * 24 * 258)               /* the shapes' positions, Q15 */
static inline int32_t va_morph(uint32_t ph, uint32_t inc, int32_t sh)
{
    int32_t a, b, f;
    if (sh >= VA_MP(2)) {
        if (sh >= VA_MP(4))                      /* square -> narrow pulse */
            return osc_pulse(ph, inc, VA_PULSE_PW(clamp(((sh - VA_MP(4)) * 16785) >> 12, 0, 32766)));
        b = -osc_saw(ph + 0x80000000u, inc);     /* the ramp */
        if (sh >= VA_MP(3)) {                    /* ramp -> square (BASIC SQR: (saw - saw half a cycle on) / 2) */
            a = b;
            b = (osc_saw(ph, inc) + b) >> 1;
            f = sh - VA_MP(3);
        } else {                                 /* saw -> ramp */
            a = osc_saw(ph, inc);
            f = sh - VA_MP(2);
        }
    } else if (sh >= VA_MP(1)) {                 /* triangle -> saw */
        a = osc_tri(ph + 0xC0000000u);
        b = osc_saw(ph, inc);
        f = sh - VA_MP(1);
    } else {                                     /* sine -> triangle */
        a = sine_i(ph + 0x80000000u);
        b = osc_tri(ph + 0xC0000000u);
        f = sh;
    }
    return a + (((b - a) * ((f * 2710) >> 12)) >> 12);   /* (Q12 within the segment) */
}

/* NOISE BROWN: white noise through a leaky integrator (a one-pole low-pass: SHAPE (COLOR) sets its corner, 30 Hz ..
 * 3 kHz, KTRK moves it with the key), made up to about half the white noise's RMS. The corner's coefficient (Q15)
 * and the make-up gain (Q8) at SHAPE 0, 8, .. 127 */
static const uint16_t VA_BROWN_K[17] = {140, 187, 249, 333, 444, 592, 789, 1050, 1396, 1852, 2451, 3234, 4249, 5552,
                                        7200, 9248, 11397};
static const uint16_t VA_BROWN_G[17] = {2769, 2395, 2072, 1792, 1550, 1341, 1160, 1003, 868, 751, 649, 562, 486, 421,
                                        364, 316, 279};
static inline int32_t va_brown(int32_t *nst, int32_t *y, int32_t k, int32_t g)
{
    int32_t w = (int32_t)(noise32(nst) >> 16) - 32768;
    *y += ((w - *y) * k) >> 15;
    return clamp((*y * g) >> 8, -32767, 32767);
}
/* NOISE VINYL: sparse crackle (clicks of random size and sign, each gone in ~0.2 ms; SHAPE (DENS) sets how many:
 * thr / 65536 a sample, ~1 .. 200 a second) over a low hiss (white noise low-passed at ~2.5 kHz, about -30 dB) */
static inline int32_t va_vinyl(int32_t *nst, int32_t *c, int32_t *h, uint32_t thr)
{
    uint32_t r = noise32(nst);
    *h += ((((int32_t)(r >> 16) - 32768) - *h) * 10000) >> 15;
    *c -= (*c >> 3) + (*c > 0);                  /* (to 0 from either side) */
    if ((r & 0xFFFFu) < thr)
        *c = (int32_t)(noise32(nst) >> 16) - 32768;
    return clamp(*c + (*h >> 3), -32767, 32767);
}

/* the filter's output as weights (Q10) of the input, the band-pass (kd v1) and the low-pass (v2): LP (0, 0, 1),
 * BP (0, 1, 0), HP (1, -1, -1), NOTCH (1, -1, 0) (exactly va_render's switch at 1024); MORPH crossfades the weights
 * towards the next type of the cycle LP -> BP -> HP -> NOTCH -> LP (ff, Q10) */
typedef struct { int32_t x, b, l; } va_fw_t;
static const int8_t VA_FW[4][3] = {{0, 0, 1}, {0, 1, 0}, {1, -1, -1}, {1, -1, 0}};
static va_fw_t va_fweights(uint32_t ty, int32_t ff)
{
    const int8_t *a = VA_FW[ty & 3u], *b = VA_FW[(ty + 1u) & 3u];
    va_fw_t w;
    w.x = (a[0] << 10) + (b[0] - a[0]) * ff;
    w.b = (a[1] << 10) + (b[1] - a[1]) * ff;
    w.l = (a[2] << 10) + (b[2] - a[2]) * ff;
    return w;
}
/* one sample of the SVF c (state ic1, ic2) with the output weights w; the soft knee after it */
static inline int32_t va_svf(const tsvf_t *c, int32_t x, int32_t *ic1, int32_t *ic2, int32_t kd, va_fw_t w)
{
    int32_t v3 = x - *ic2, v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13, v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13), y;
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    y = (w.x * x + w.b * ((kd * v1) >> 12) + w.l * v2) >> 10;
    return soft_knee(clamp(y, -200000, 200000), 16000);
}

/* the oscillator loop for one wave: x(ph, inc, sh) the sample; adds level-ramped into acc */
#define VA_OSC_LOOP_FULL(X)                                                \
    for (i = 0; i < n; i++) {                                              \
        int32_t x_ = (X);                                                  \
        uint32_t o_ = ph;                                                  \
        if (keep)                                                          \
            r3[i] = x_;                                                    \
        if (ring)                                                          \
            x_ = (x_ * r3[i]) >> 15;                                       \
        acc[i] += (x_ * (l >> 16)) >> 15;                                  \
        l += dl;                                                           \
        sh += dsh;                                                         \
        ph += inc;                                                         \
        inc += dinc;                                                       \
        if (rec && ph < o_) {                  /* OSC 1 wrapped: OSC 2 restarts there */ \
            wrap |= 1u << i;                                               \
            sph[i] = ph;                                                   \
        }                                                                  \
        if (sync && ((wrap >> i) & 1u))                                    \
            ph = (uint32_t)(((uint64_t)sph[i] * ratio) >> 16);             \
    }
#define VA_OSC_LOOP(X)                                                     \
    if (keep | ring | rec | sync) {                                        \
        VA_OSC_LOOP_FULL(X)                                                \
    } else {                                   /* (no sync, no ring: the plain loop) */ \
        for (i = 0; i < n; i++) {                                          \
            int32_t x_ = (X);                                              \
            acc[i] += (x_ * (l >> 16)) >> 15;                              \
            l += dl;                                                       \
            sh += dsh;                                                     \
            ph += inc;                                                     \
            inc += dinc;                                                   \
        }                                                                  \
    }

/* a voice's block: out gets the mono (the mid), side (0: none) the stereo side of SPREAD / USPREAD; 1 = side got
 * something */
static int va_render(track_t *t, voice_t *v, int32_t *out, int32_t *side, uint32_t n, const vmod_t *m)
{
    va_voice_t *s = va_voice(t, v);
    uint32_t tr = va_tr(t), k, i, wrap = 0, osc_on = 0, vinyl = 0;
    const int8_t *p;
    int32_t src[VS_N], dpit = 0, dosc[3][4] = {{0}}, dcut = 0, dres = 0, dpan = 0, drate[4] = {0}, dfm = 0, gain = 32767;
    int32_t acc[CTL], r3[CTL];
    uint32_t sph[CTL], tinc[4];
    int32_t tlvl[4], tshp[4], tsh[4], env[4], A0, A1, dA, a, cut, kd, mixg[2], dwow = 0, spr, fm8, up = 0;
    tsvf_t c, cr;
    if (!s || !s->live || n > CTL)
        return 0;
    p = va_patch[tr];
    for (k = 0; k < 4u; k++)
        env[k] = va_env_tick(p, s, k, v->gate);
    if (s->ticks < 0xFFFFu)
        s->ticks++;
    /* the sources, Q15 */
    src[VS_OFF] = 0;
    for (k = 0; k < 4u; k++) {
        int32_t f = p[VA_LFO(k, VL_FADE)] ? (int32_t)s->ticks * (int32_t)(ENV_LIN[p[VA_LFO(k, VL_FADE)] & 127] >> 9) : 32767;
        src[VS_ENV1 + k] = env[k];
        src[VS_LFO1 + k] = f >= 32767 ? va_lfo[tr].out[k] : (va_lfo[tr].out[k] * f) >> 15;
    }
    src[VS_VEL] = (v->mvel ? v->mvel : v->vel) * 258;
    src[VS_KEY] = clamp(((int32_t)v->note - 60) * 512, -32767, 32767);
    src[VS_RAND] = v->mrnd;
    src[VS_MODW] = t->mw * 258;
    /* the matrix */
    for (k = 0; k < 8u; k++) {
        uint32_t sr = (uint32_t)p[VA_MOD(k, VM_SRC)], d = (uint32_t)p[VA_MOD(k, VM_DST)];
        int32_t am = p[VA_MOD(k, VM_AMT)], x;
        if (!sr || !d || !am || sr >= VS_N || d >= VD_N)
            continue;
        x = src[sr] * am;                        /* Q15 x amount: +-2^21 */
        if (d == VD_PITCH)
            dpit += x;
        else if (d < VD_LVL1)
            dosc[0][d - VD_PIT1] += x;
        else if (d < VD_SHP1)
            dosc[1][d - VD_LVL1] += x;
        else if (d < VD_CUT)
            dosc[2][d - VD_SHP1] += x;
        else if (d == VD_CUT)
            dcut += x;
        else if (d == VD_RES)
            dres += x;
        else if (d == VD_AMP) {
            int32_t u = sr >= VS_LFO1 && sr < VS_VEL ? (src[sr] + 32768) >> 1 : sr >= VS_KEY ? (src[sr] + 32768) >> 1 : src[sr];
            u = clamp(u, 0, 32767);
            gain = mulq15(gain, clamp(32767 - (am > 0 ? ((32767 - u) * am) >> 6 : (u * -am) >> 6), 0, 32767));
        } else if (d == VD_PAN)
            dpan += x;
        else if (d == VD_FMORPH)
            dfm += x;
        else
            drate[d - VD_RATE1] += x;
    }
    if (v - t->v == t->m_vi) {                   /* the per-part destinations: from the latest note's voice */
        va_pan_off[tr] = (int8_t)clamp(dpan >> 15, -64, 63);
        for (k = 0; k < 4u; k++)
            va_rate_off[tr][k] = (int8_t)clamp(drate[k] >> 14, -127, 127);
    }
    /* the oscillators' targets: increments, levels, shapes */
    {
        int32_t om = p[VA_OMIX], d3 = (p[VA_DETUNE] * 68) >> 10;   /* DTN: a third of up to 25 cents (no divide) */
        static const int8_t SPREAD[4] = {-3, 3, -1, 1};
        mixg[0] = om <= 64 ? 32767 : (127 - om) * 520;           /* MIX: 64 both pairs, 0 OSC 1+2 only, 127 3+4 */
        mixg[1] = om >= 64 ? 32767 : om * 512;
        for (k = 0; k < 4u; k++)                 /* a VINYL oscillator sounding: the voice's pitch wows (+-6 ct) */
            if (p[VA_OMODE0 + k] == VOM_NOISE && p[VA_OSC(k, VO_WAVE)] == VN_VINYL && p[VA_OSC(k, VO_LEVEL)])
                vinyl = 1;
        if (vinyl)
            dwow = (va_lfo[tr].wow * 15) >> 15;
        for (k = 0; k < 4u; k++) {
            int32_t base = p[VA_OSC(k, VO_KTRK)] ? m->pitch16 : 60 * 16;
            int32_t ct = p[VA_OSC(k, VO_FINE)] + SPREAD[k] * d3;
            int32_t q = (base << 4) + p[VA_OSC(k, VO_COARSE)] * 256 + ((ct * 2621) >> 10) + (((dpit + dosc[0][k]) * 3) >> 11) +
                        dwow;
            int32_t lv, sh, fu;
            uint32_t inc;
            q = clamp(q, 0, 2047 << 4);
            inc = pitch_inc((uint32_t)q >> 4);
            fu = (p[VA_OSC(k, VO_KTRK)] ? m->fine : 0) + (((q & 15) * 237) >> 8);
            if (fu)
                inc += (uint32_t)((int32_t)(inc >> 12) * fu);
            tinc[k] = inc;
            lv = clamp(p[VA_OSC(k, VO_LEVEL)] + (dosc[1][k] >> 14), 0, 127);
            tlvl[k] = mulq15(lv * lv * 2, mixg[k >> 1]);
            sh = clamp(p[VA_OSC(k, VO_SHAPE)] + (dosc[2][k] >> 14) + ((m->shape - (64 << 8)) >> 8), 0, 127);
            tshp[k] = sh * 258;
            tsh[k] = sh;
            if (tlvl[k] || s->lvl[k])
                osc_on |= 1u << k;
        }
        if ((osc_on & 2u) && p[VA_SYNC2])
            osc_on |= 1u;                        /* OSC 1 drives OSC 2's sync, OSC 3 OSC 4's ring */
        if ((osc_on & 8u) && p[VA_RING4])
            osc_on |= 4u;
    }
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (k = 0; k < 4u; k++) {
        uint32_t ph = k < 3u ? v->ph[k] : s->ph4, inc0 = (uint32_t)v->s[3 + k], inc = inc0 ? inc0 : tinc[k];
        int32_t dinc = ((int32_t)(tinc[k] - inc) + ((int32_t)(tinc[k] - inc) >> 31 & (CTL - 1))) >> CTL_LOG2;
        int32_t l = (int32_t)s->lvl[k] << 16, dl = (((int32_t)tlvl[k] << 16) - l) >> CTL_LOG2;
        int32_t sh = p[VA_OMODE0 + k] != VOM_BASIC && !inc0 ? tshp[k] : (int32_t)s->shp[k];   /* (MORPH / NOISE: */
        int32_t dsh = (tshp[k] - sh) >> CTL_LOG2;                    /* a fresh voice starts at its shape) */
        uint32_t sync = k == 1u && p[VA_SYNC2], rec = k == 0u && p[VA_SYNC2];
        uint32_t keep = k == 2u && p[VA_RING4], ring = k == 3u && p[VA_RING4], ratio = 0, w = (uint32_t)p[VA_OSC(k, VO_WAVE)];
        uint32_t mode = (uint32_t)p[VA_OMODE0 + k];
        if (!((osc_on >> k) & 1u)) {             /* silent: the phase runs on (no step when it comes back) */
            ph += ((inc >> 1) + (tinc[k] >> 1)) * n;
            if (k < 3u)
                v->ph[k] = ph;
            else
                s->ph4 = ph;
            v->s[3 + k] = (int32_t)tinc[k];
            s->lvl[k] = (int16_t)tlvl[k];
            s->shp[k] = (int16_t)tshp[k];
            continue;
        }
        if (sync)
            ratio = tinc[1] / ((tinc[0] >> 16) | 1u);   /* OSC 2 / OSC 1, Q16 */
        if (sync && ratio > (64u << 16))
            ratio = 64u << 16;
        if (mode == VOM_MORPH)
            w = VW_N;                            /* (the switch's MORPH) */
        else if (mode == VOM_NOISE)
            w = w == VN_BROWN ? VW_N + 1u : w == VN_VINYL ? VW_N + 2u : VW_NOIS;
        switch (w) {
        case VW_SQR:
            VA_OSC_LOOP(osc_pulse(ph, inc, VA_PULSE_PW(sh)))
            break;
        case VW_TRI:
            VA_OSC_LOOP(osc_tri(ph))
            break;
        case VW_SIN:
            if (!sh && !dsh) {                   /* no fold: the plain sine */
                VA_OSC_LOOP(sine_i(ph))
                break;
            }
            VA_OSC_LOOP(osc_tri((uint32_t)(((sine_i(ph) * ((32768 + 3 * sh) >> 2)) >> 13) + 32768) << 15))   /* SHAPE folds */
            break;
        case VW_PWM: {
            uint32_t pw = 0x80000000u + (uint32_t)((osc_tri(va_lfo[tr].pwm + (uint32_t)(v - t->v) * 0x20000000u) *
                                                    (sh >> 7)) * 0x58);
            VA_OSC_LOOP(osc_pulse(ph, inc, pw))
            break;
        }
        case VW_NOIS: {
            int32_t nst = v->s[2];
            VA_OSC_LOOP(((int32_t)(noise32(&nst) >> 16) - 32768))
            v->s[2] = nst;
            break;
        }
        case VW_N:                               /* MORPH */
            VA_OSC_LOOP(va_morph(ph, inc, sh))
            break;
        case VW_N + 1u: {                        /* NOISE BROWN: SHAPE the corner, KTRK moves it with the key */
            int32_t nst = v->s[2], *y = &s->nz[k][0], e, j, fq, kq, gq;
            e = clamp(tsh[k] + (p[VA_OSC(k, VO_KTRK)] ? ((m->pitch16 - 60 * 16) * 13) >> 7 : 0), 0, 127);
            j = e >> 3;
            fq = e & 7;
            kq = VA_BROWN_K[j] + (((VA_BROWN_K[j + 1] - VA_BROWN_K[j]) * fq) >> 3);
            gq = VA_BROWN_G[j] + (((VA_BROWN_G[j + 1] - VA_BROWN_G[j]) * fq) >> 3);
            VA_OSC_LOOP(va_brown(&nst, y, kq, gq))
            v->s[2] = nst;
            break;
        }
        case VW_N + 2u: {                        /* NOISE VINYL: SHAPE the crackle's density */
            int32_t nst = v->s[2], *cz = &s->nz[k][0], *hz = &s->nz[k][1];
            uint32_t thr = 2u + (uint32_t)(tsh[k] * tsh[k] * tsh[k]) / 7000u;
            VA_OSC_LOOP(va_vinyl(&nst, cz, hz, thr))
            v->s[2] = nst;
            break;
        }
        default:
            VA_OSC_LOOP(osc_saw(ph, inc) + ((sh ? (osc_tri(ph) - osc_saw(ph, inc)) * (sh >> 3) : 0) >> 12))
            break;
        }
        if (k < 3u)
            v->ph[k] = ph;
        else
            s->ph4 = ph;
        v->s[3 + k] = (int32_t)tinc[k];
        s->lvl[k] = (int16_t)tlvl[k];
        s->shp[k] = (int16_t)tshp[k];
    }
    /* the filter: cutoff from CUT, ENV 2 by FENV, KTRK, the track's (ENV / LFO -> FLT), the matrix */
    cut = (p[VA_CUT] << 8) + ((env[1] * p[VA_FENV]) >> 6) + (((m->pitch16 - 60 * 16) * p[VA_FKTRK] * 150) >> 10) +
          m->cutoff + (dcut >> 7);
    kd = 8192 - clamp(p[VA_RES] + (dres >> 14), 0, 127) * 60;
    spr = p[VA_FSPREAD];                         /* SPREAD: the left SVF's cutoff down, the right's up, +-1 octave */
    if (spr) {
        int32_t off = (spr * 7235) >> 8;         /* (14.02 cutoff steps an octave, << 8) */
        tsvf_coef_k(&c, cut - off, kd);
        tsvf_coef_k(&cr, cut + off, kd);
    } else {
        tsvf_coef_k(&c, cut, kd);
    }
    fm8 = clamp((p[VA_FMORPH] << 8) + (dfm >> 6), 0, 127 << 8);   /* MORPH, 1/256 */
    if (side && t->p[P_VOICE] == V_UNISON && p[VA_USPREAD]) {     /* USPREAD: voice i of 8 at (2i - 7) / 7 */
        int32_t kk = 2 * (int32_t)(v - t->v) - (NVOICE - 1);
        up = clamp(kk * p[VA_USPREAD] * 37, -32767, 32767) >> 4;   /* Q11 */
    }
    /* the amplitude: ENV 1 by velocity, the matrix's AMP, the voice's (fades, LFO -> AMP); UNISON as FM6 */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, va = p[VA_VEL];
        int32_t velf = 32767 - ((va * (127 - vel) * 2080) >> 10);
        A1 = mulq15(mulq15(env[0], velf), gain);
        if (t->p[P_VOICE] == V_UNISON)
            A1 = (A1 * 13107) >> 15;   /* 2 / 5 */
        A1 = mulq15(A1, m->amp1);
    }
    A0 = v->s[7];
    dA = (A1 - A0) >> CTL_LOG2;
    a = A0;
    {
        int32_t ic1 = v->s[0], ic2 = v->s[1], drv = p[VA_DRIVE], g = 4096 + drv * 97;   /* DRIVE: 1x .. 4x */
        uint32_t ty = (uint32_t)p[VA_FTYPE];
        if (p[VA_SYNC2]) {                       /* ~27 Hz high-pass */
            int32_t dc = s->dc;
            for (i = 0; i < n; i++) {
                acc[i] -= dc >> 8;
                dc += acc[i];
            }
            s->dc = dc;
        }
        if (!fm8 && !spr && !up) {               /* one SVF, a discrete type, mono: the plain loop */
            s->spr = 0;
            for (i = 0; i < n; i++) {
                int32_t x = acc[i] >> 1, v1, v2, v3, y;
                if (drv)
                    x = softclip((x * (g >> 4)) >> 8);
                v3 = x - ic2;
                v1 = (c.a1 * ic1 + c.a2 * v3) >> 13;
                v2 = ic2 + ((c.a2 * ic1 + c.a3 * v3) >> 13);
                ic1 = clamp(2 * v1 - ic1, -150000, 150000);
                ic2 = clamp(2 * v2 - ic2, -150000, 150000);
                switch (ty) {
                case VF_BP: y = (kd * v1) >> 12; break;
                case VF_HP: y = x - ((kd * v1) >> 12) - v2; break;
                case VF_NOTCH: y = x - ((kd * v1) >> 12); break;
                default: y = v2; break;
                }
                y = soft_knee(clamp(y, -200000, 200000), 16000);
                a += dA;
                out[i] += (mulq15(y, a) * (VOICE_FS / 4)) >> 11;
            }
        } else {                                 /* MORPH, SPREAD (two SVFs: mid + side), USPREAD (the voice's pan) */
            uint32_t pos = (ty << 13) + (uint32_t)fm8;
            int32_t jc1 = 0, jc2 = 0;
            va_fw_t fw = va_fweights(fm8 ? (pos >> 13) & 3u : ty, fm8 ? (int32_t)(pos & 8191u) >> 3 : 0);
            if (spr) {
                if (!s->spr) {                   /* SPREAD came on: the right SVF starts from the left one's state */
                    s->fr[0] = ic1;
                    s->fr[1] = ic2;
                    s->spr = 1;
                }
                jc1 = s->fr[0];
                jc2 = s->fr[1];
            } else {
                s->spr = 0;
            }
            for (i = 0; i < n; i++) {
                int32_t x = acc[i] >> 1, y, o;
                if (drv)
                    x = softclip((x * (g >> 4)) >> 8);
                y = va_svf(&c, x, &ic1, &ic2, kd, fw);
                a += dA;
                if (spr) {
                    int32_t yr = va_svf(&cr, x, &jc1, &jc2, kd, fw);
                    o = (mulq15((y + yr) >> 1, a) * (VOICE_FS / 4)) >> 11;
                    if (side)
                        side[i] += ((mulq15((yr - y) >> 1, a) * (VOICE_FS / 4)) >> 11) + ((o * up) >> 11);
                } else {
                    o = (mulq15(y, a) * (VOICE_FS / 4)) >> 11;
                    if (up)
                        side[i] += (o * up) >> 11;
                }
                out[i] += o;
            }
            if (spr) {
                s->fr[0] = jc1;
                s->fr[1] = jc2;
            }
        }
        v->s[0] = ic1;
        v->s[1] = ic2;
    }
    v->s[7] = A1;
    return side && (spr || up);
}

static void va_render_mono(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)   /* engine_t.render */
{
    va_render(t, v, out, 0, n, m);
}

/* fx.c mix_part: the part's pan with the matrix's PAN */
static int32_t va_pan(const track_t *t, int32_t pan)
{
    uint32_t tr = va_tr(t);
    if (t->engine != ENGI_VA || tr >= VA_NPART || !va_pan_off[tr])
        return pan;
    return clamp(pan + va_pan_off[tr], -64, 63);
}

/* --------------------------------------------------------- the engine --- */
static const eng_deep_t VA_DEEP = {
    .npages = NELEM(VA_PAGES),
    .pages = VA_PAGES,
    .section = {0, 8, 10, 18, 24, 0xFF, 0xFF, 0xFF},
    .get = va_get,
    .set = va_set,
    .blob_size = VA_BLOB,
    .blob_get = va_blob_get,
    .blob_set = va_blob_set,
    .blob_preset = va_blob_preset,
    .desc = va_desc,
};

static const engine_t ENG_VA = {
    .name = "VA",
    .page_title = {"FILTER", "MACRO"},
    .edit = {
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"FENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
        {"MIX", F_INT, 0, 127, 64, 0, 0},
        {"DTN", F_PCT, 0, 127, 0, 0, 0},
        {"ATK", F_TIME, 0, 127, 0, 0, 0},
        {"REL", F_TIME, 0, 127, 40, 0, 0},
    },
    .presets = VA_PRESETS,
    .npresets = NELEM(VA_PRESETS),
    .knob = {P_E0, P_E1, P_E6, P_E7},
    .poly = VA_POLY,
    .keep = 0x03,                /* the filter */
    .note_on = va_note_on,
    .render = va_render_mono,
    .render2 = va_render,
    .block = va_block,
    .ownenv = 1,
    .done = va_done,
    .deep = &VA_DEEP,
};
