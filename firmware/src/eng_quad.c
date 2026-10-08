/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* QUAD: a Digitone-style four-operator FM voice (ChoralRoot, FELUCCA_QUAD; docs/QUAD.md). Our own code: no Elektron
 * code, the ratio steps and the HARM curve are from public descriptions and by ear.
 *
 * Per voice: four sine operators C, A, B1, B2 in eight fixed routings (QUAD_ALGO below, the designer's ALGOS table:
 * the modulator -> target pairs, the feedback operator, the X and Y outputs), the feedback operator modulating itself
 * by the average of its last two outputs (the DX7 family's), the carriers' wave a morph of 15 harmonic tables (HARM:
 * sine, 7 odd-series steps on the + side, 7 all-series steps on the - side, two neighbours crossfaded), the outputs
 * mixed X .. Y (MIX), a DC blocker (QUAD_DC_K), then a multimode SVF (LP HP BP: dsp.c's trapezoidal one, as the
 * VA's) with its own ADSR envelope (depth, delay, key track), then the base-width filter (a one-pole high-pass at
 * BASE, a one-pole low-pass at BASE + WIDTH), then QUAD's own amp envelope (engine_t.ownenv / done, as FM6 and the
 * VA) and LEVEL.
 * Operator envelopes A (operator A's level) and B (B1's and B2's): ATK to LEV, DEC to END, held (attack-decay-end:
 * the gate does not release them), each with a DELAY, a TRIG mode, a RESET, a key track; VEL scales LEV. Three LFOs
 * per voice (FREE / HOLD run on the part's phase; TRIG / ONE / HALF on the voice's), at control rate (every CTL = 32
 * samples), summed into their destinations. Operator levels and the amplitude move per sample as linear ramps over
 * the tick; the operators' phase increments, the filter's coefficients, HARM, FDBK and MIX are per tick.
 *
 * The patch is the sound: QP_NP signed bytes per part (quad_patch), the deep pages edit it (eng_deep_t get / set from
 * the main loop), a factory preset (blob_preset), a user slot (the blob) or the init patch load it. The eight P_E0..P_E7
 * are macros into the patch (the SYN 1 page: ALGO RATIO C RATIO A RATIO B | HARM DTUNE FDBK MIX, patch values 0..7);
 * quad_block picks a macro change up, a deep set writes the macro back, as the VA. AMP's PAN and DRIVE are the part's
 * P_PAN and P_DIST (not in the patch: the deep page reads and writes the track parameters).
 *
 * State: the patch, the macros as last seen, the part's LFO phases (the pool, as the VA's); per voice a quad_voice_t
 * (the pool: QUAD_NPART x QUAD_POLY). voice_t.s / .ph are not used (engine_t.keep 0): voice.c's retrigger restore of
 * ph[0..2] does not touch QUAD's own phases. */
#include "quad_tables.h"
#ifndef ENGI_QUAD
#define ENGI_QUAD 15u            /* engines.c ENGINES[] (append-only): 15 on ChoralRoot (after CZ-1, 14) */
#endif
#define QUAD_NPART 2u            /* parts with a QUAD patch (ChoralRoot: chord, bass) */
#define QUAD_POLY 8              /* engine_t.poly (docs/QUAD.md: CPU) */
#define QUAD_BLOB 80u            /* packed patch: 2 bytes magic / version, QP_NP values, zero padding */
#define QUAD_MAGIC 0x51u         /* 'Q' */
#define QUAD_VER 1u

/* --------------------------------------------------------- the patch --- */
enum { QL_SPEED, QL_MULT, QL_FADE, QL_DEST, QL_WAVE, QL_PHASE, QL_TRIG, QL_DEPTH, QL_N };   /* per LFO */
enum {
    QP_ALGO, QP_RC, QP_RA, QP_RB, QP_HARM, QP_DTUN, QP_FDBK, QP_MIX,       /* 0..7: the macros (SYN 1) */
    QP_OFSC, QP_OFSA, QP_OFSB1, QP_OFSB2,                                  /* SYN 2 */
    QP_AATK, QP_ADEC, QP_AEND, QP_ALEV,                                    /* ENV A */
    QP_BATK, QP_BDEC, QP_BEND, QP_BLEV,                                    /* ENV B */
    QP_ADLY, QP_ATRIG, QP_ARST, QP_PHRST,                                  /* ENV 2 */
    QP_BDLY, QP_BTRIG, QP_BRST, QP_VEL,                                    /* ENV 2+ */
    QP_AKTRK, QP_BKTRK,                                                    /* ENV 3 */
    QP_FATK, QP_FDEC, QP_FSUS, QP_FREL,                                    /* FILTER */
    QP_FREQ, QP_RESO, QP_FTYPE, QP_FDEPTH,                                 /* FILTER+ */
    QP_FDLY, QP_FKTRK, QP_BASE, QP_WIDTH,                                  /* FILT 2, FILT 2+ */
    QP_EATK, QP_EDEC, QP_ESUS, QP_EREL, QP_LEVEL,                          /* AMP, AMP+ */
    QP_LFO0,
    QP_NP = QP_LFO0 + 3 * QL_N
};
#define QP_LFO(k, f) (QP_LFO0 + (k) * QL_N + (f))
_Static_assert(QP_NP == 71 && QP_NP + 2u + 2u <= QUAD_BLOB && QUAD_BLOB <= ENG_BLOB_MAX, "QUAD patch layout");

enum { QF_LP, QF_HP, QF_BP, QF_N };                                        /* FTYPE */
enum { QW_TRI, QW_SINE, QW_SQR, QW_SAW, QW_RAMP, QW_EXP, QW_RAND, QW_N };   /* LFO WAVE */
enum { QT_FREE, QT_TRIG, QT_HOLD, QT_ONE, QT_HALF, QT_N };                 /* LFO TRIG */
enum { QD_OFF, QD_HARM, QD_DTUN, QD_FDBK, QD_MIX, QD_RA, QD_RB, QD_FREQ, QD_RESO, QD_LEVEL, QD_PAN, QD_ALEV, QD_BLEV,
       QD_N };                                                             /* LFO DEST (append only: blobs store it) */
#define QUAD_NMULT 12u
static const char *const N_QUAD_FTYPE[QF_N] = {"LP", "HP", "BP"};
static const char *const N_QUAD_WAVE[QW_N] = {"TRI", "SINE", "SQR", "SAW", "RAMP", "EXP", "RAND"};
static const char *const N_QUAD_TRIG[QT_N] = {"FREE", "TRIG", "HOLD", "ONE", "HALF"};
static const char *const N_QUAD_DEST[QD_N] = {"OFF", "HARM", "DTUNE", "FDBK", "MIX", "RAT A", "RAT B", "FREQ", "RESO",
                                              "LEVEL", "PAN", "A LEV", "B LEV"};
static const char *const N_QUAD_MULT[QUAD_NMULT] = {"x1", "x2", "x4", "x8", "x16", "x32", "x64", "x128", "x256", "x512",
                                                    "x1k", "x2k"};

/* the routings (docs/QUAD.md, the designer's ALGOS): per algorithm the modulators of C, A, B1, B2 (bit 0 C, 1 A, 2 B1,
 * 3 B2), the feedback operator, the X and Y outputs (bit sets). The renders are written out per algorithm
 * (quad_render's switch); this table is what the test checks them against and what the editor may draw from */
enum { QO_C, QO_A, QO_B1, QO_B2 };
typedef struct { uint8_t mod[4], fb, x, y; } quad_algo_t;
static const quad_algo_t QUAD_ALGO[8] = {
    {{0x2, 0x4, 0x8, 0}, QO_B2, 0x1, 0x1},         /* 1: A>C, B1>A, B2>B1; X C, Y C */
    {{0x6, 0, 0x8, 0}, QO_B2, 0x1, 0x1},           /* 2: A>C, B1>C, B2>B1 */
    {{0x2, 0xC, 0, 0}, QO_B2, 0x1, 0x1},           /* 3: A>C, B1>A, B2>A */
    {{0x2, 0, 0x8, 0}, QO_B2, 0x1, 0x4},           /* 4: A>C, B2>B1; X C, Y B1 */
    {{0x2, 0x4, 0, 0}, QO_B2, 0x1, 0x8},           /* 5: A>C, B1>A; X C, Y B2 */
    {{0x2, 0, 0xA, 0}, QO_B2, 0x1, 0x4},           /* 6: A>C, A>B1, B2>B1; X C, Y B1 */
    {{0, 0, 0x8, 0}, QO_A, 0x3, 0x4},              /* 7: B2>B1; feedback A; X C+A, Y B1 */
    {{0, 0, 0, 0}, QO_B2, 0x3, 0xC},               /* 8: none; X C+A, Y B1+B2 */
};

/* the deep pages' columns (the UI contract, docs/QUAD.md "Implementation") */
#define QC_ALGO {"ALGO", F_INT, 1, 8, 1, 0, 0}
#define QC_RC {"RATIO C", F_INT, 0, QUAD_NRCB - 1, 3, N_QUAD_RCB, 0}
#define QC_RA {"RATIO A", F_INT, 0, QUAD_NRA - 1, 3, N_QUAD_RA, 0}
#define QC_RB {"RATIO B", F_INT, 0, QUAD_NRB - 1, QUAD_RB_DEF, N_QUAD_RB, 0}
#define QC_HARM {"HARM", F_OFS, -26, 26, 0, 0, 0}
#define QC_DTUN {"DTUNE", F_INT, 0, 127, 0, 0, 0}
#define QC_FDBK {"FDBK", F_INT, 0, 127, 0, 0, 0}
#define QC_MIX {"MIX", F_OFS, -63, 63, 0, 0, 0}
#define QC_OFS(l) {l, F_INT, -100, 100, 0, N_QUAD_OFS, 0}
#define QC_T(l, d) {l, F_TIME, 0, 127, d, 0, 0}
#define QC_P(l, d) {l, F_INT, 0, 127, d, 0, 0}
#define QC_PCT(l, d) {l, F_PCT, 0, 127, d, 0, 0}
#define QC_ON(l, d) {l, F_ONOFF, 0, 1, d, 0, 0}
#define QC_FREQ {"FREQ", F_CUTOFF, 0, 127, 127, 0, 0}
#define QC_FTYPE {"TYPE", F_ENUM, 0, QF_N - 1, QF_LP, N_QUAD_FTYPE, 0}
#define QC_DEPTH(l) {l, F_OFS, -64, 63, 0, 0, 0}
#define QC_PAN {"PAN", F_OFS, -64, 63, 0, 0, 0}
#define QC_SPEED {"SPEED", F_OFS, -64, 64, 16, 0, 0}
#define QC_MULT {"MULT", F_ENUM, 0, QUAD_NMULT - 1, 3, N_QUAD_MULT, 0}
#define QC_DEST {"DEST", F_ENUM, 0, QD_N - 1, QD_OFF, N_QUAD_DEST, 0}
#define QC_WAVE {"WAVE", F_ENUM, 0, QW_N - 1, QW_TRI, N_QUAD_WAVE, 0}
#define QC_LTRIG {"TRIG", F_ENUM, 0, QT_N - 1, QT_FREE, N_QUAD_TRIG, 0}
#define QC_NONE {0, 0, 0, 0, 0, 0, 0}
#define QUAD_X 0xFFu             /* an empty column */
#define QUAD_XPAN 0xFEu          /* AMP+: the part's P_PAN */
#define QUAD_XDIST 0xFDu         /* AMP+: the part's P_DIST (DRIVE) */

static const eng_page_t QUAD_PAGES[] = {
    {"SYN 1", {QC_ALGO, QC_RC, QC_RA, QC_RB}},                              /* 0  OSC */
    {"SYN 1+", {QC_HARM, QC_DTUN, QC_FDBK, QC_MIX}},
    {"SYN 2", {QC_OFS("OFS C"), QC_OFS("OFS A"), QC_OFS("OFS B1"), QC_OFS("OFS B2")}},
    {"FILTER", {QC_T("ATK", 0), QC_T("DEC", 64), QC_PCT("SUS", 0), QC_T("REL", 40)}},   /* 3  FILTER */
    {"FILTER+", {QC_FREQ, QC_PCT("RESO", 0), QC_FTYPE, QC_DEPTH("DEPTH")}},
    {"FILT 2", {QC_T("DELAY", 0), QC_PCT("KTRK", 0), QC_NONE, QC_NONE}},
    {"FILT 2+", {QC_P("BASE", 0), QC_P("WIDTH", 127), QC_NONE, QC_NONE}},
    {"ENV A", {QC_T("A ATK", 0), QC_T("A DEC", 60), QC_P("A END", 64), QC_P("A LEV", 48)}},   /* 7  ENV */
    {"ENV B", {QC_T("B ATK", 0), QC_T("B DEC", 60), QC_P("B END", 0), QC_P("B LEV", 0)}},
    {"ENV 2", {QC_T("A DLY", 0), QC_ON("A TRIG", 1), QC_ON("A RESET", 1), QC_ON("PHASE", 1)}},
    {"ENV 2+", {QC_T("B DLY", 0), QC_ON("B TRIG", 1), QC_ON("B RESET", 1), QC_PCT("VEL", 64)}},
    {"ENV 3", {QC_PCT("A KTRK", 0), QC_PCT("B KTRK", 0), QC_NONE, QC_NONE}},
    {"AMP", {QC_T("ATK", 0), QC_T("DEC", 64), QC_PCT("SUS", 127), QC_T("REL", 40)}},
    {"AMP+", {QC_P("LEVEL", 100), QC_PAN, QC_PCT("DRIVE", 0), QC_NONE}},
    {"LFO 1", {QC_SPEED, QC_MULT, QC_T("FADE", 0), QC_DEST}},               /* 14 LFO */
    {"LFO 1+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    {"LFO 2", {QC_SPEED, QC_MULT, QC_T("FADE", 0), QC_DEST}},
    {"LFO 2+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    {"LFO 3", {QC_SPEED, QC_MULT, QC_T("FADE", 0), QC_DEST}},
    {"LFO 3+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
};
#define QUAD_NPAGES ((uint32_t)NELEM(QUAD_PAGES))
#define QUAD_PG_OSC 0u
#define QUAD_PG_FILTER 3u
#define QUAD_PG_ENV 7u
#define QUAD_PG_LFO 14u
_Static_assert(NELEM(QUAD_PAGES) == 20, "QUAD pages");
#define QUAD_LPG(k) {QP_LFO(k, QL_SPEED), QP_LFO(k, QL_MULT), QP_LFO(k, QL_FADE), QP_LFO(k, QL_DEST)}, \
                    {QP_LFO(k, QL_WAVE), QP_LFO(k, QL_PHASE), QP_LFO(k, QL_TRIG), QP_LFO(k, QL_DEPTH)}
static const uint8_t QUAD_MAP[NELEM(QUAD_PAGES)][4] = {
    {QP_ALGO, QP_RC, QP_RA, QP_RB},
    {QP_HARM, QP_DTUN, QP_FDBK, QP_MIX},
    {QP_OFSC, QP_OFSA, QP_OFSB1, QP_OFSB2},
    {QP_FATK, QP_FDEC, QP_FSUS, QP_FREL},
    {QP_FREQ, QP_RESO, QP_FTYPE, QP_FDEPTH},
    {QP_FDLY, QP_FKTRK, QUAD_X, QUAD_X},
    {QP_BASE, QP_WIDTH, QUAD_X, QUAD_X},
    {QP_AATK, QP_ADEC, QP_AEND, QP_ALEV},
    {QP_BATK, QP_BDEC, QP_BEND, QP_BLEV},
    {QP_ADLY, QP_ATRIG, QP_ARST, QP_PHRST},
    {QP_BDLY, QP_BTRIG, QP_BRST, QP_VEL},
    {QP_AKTRK, QP_BKTRK, QUAD_X, QUAD_X},
    {QP_EATK, QP_EDEC, QP_ESUS, QP_EREL},
    {QP_LEVEL, QUAD_XPAN, QUAD_XDIST, QUAD_X},
    QUAD_LPG(0), QUAD_LPG(1), QUAD_LPG(2),
};

/* the range and the init value of patch value i: the page column that shows it (every value is on one page) */
typedef struct { int8_t min, max, def; } quad_rng_t;
static quad_rng_t quad_range(uint32_t i)
{
    quad_rng_t r = {0, 0, 0};
    uint32_t pg, c;
    for (pg = 0; pg < QUAD_NPAGES; pg++)
        for (c = 0; c < 4u; c++)
            if (QUAD_MAP[pg][c] == i) {
                const param_desc_t *d = &QUAD_PAGES[pg].col[c];
                r.min = (int8_t)d->min;
                r.max = (int8_t)d->max;
                r.def = (int8_t)d->def;
                return r;
            }
    return r;
}

/* the state lives in the pool (zero-initialised), as the VA's */
static int8_t quad_patch[QUAD_NPART][QP_NP] __attribute__((section(".pool")));
static int16_t quad_mlast[QUAD_NPART][8] __attribute__((section(".pool")));   /* P_E0..P_E7 as last put in the patch */
static uint8_t quad_user_pending;                /* upreset.c up_values: a QUAD user slot + 1 is being loaded */
static int (*quad_store_read)(uint32_t k, uint8_t *blob);   /* the patch store: slot k's blob, 0 = there is one */
static int8_t quad_pan_off[QUAD_NPART];          /* the LFOs' PAN (fx.c mix_part adds it: quad_pan) */
static struct {                                  /* per part: the LFOs' FREE phases */
    uint32_t ph[3], rnd[3];
} quad_lfo[QUAD_NPART] __attribute__((section(".pool")));
typedef struct {
    uint32_t ph[4];                              /* C A B1 B2 */
    int32_t fb1, fb2;                            /* the feedback operator's last two outputs (its wave, Q15) */
    int32_t env[4];                              /* Q24: operator A, operator B, filter, amp */
    uint32_t dly[4];                             /* DELAY progress, Q24 */
    uint8_t stage[4];                            /* 0 off, 1 delay, 2 attack, 3 decay (/ sustain), 4 release */
    int16_t lv[2];                               /* the ramps' ends: operator A's, B's level, Q15 */
    int32_t amp;                                 /* .. the amplitude, Q15 */
    int32_t f1, f2;                              /* the SVF */
    int32_t bh, bl;                              /* the base-width one-poles, Q8 */
    int32_t dc;                                  /* the DC blocker's one-pole (the operators' mean), Q12 */
    uint32_t lph[3], lrnd[3], ltr[3];            /* the LFOs' own phases (TRIG ONE HALF HOLD), random, travel */
    uint16_t ticks;                              /* control ticks since the note-on (LFO FADE) */
    uint8_t lstop;                               /* bit k: LFO k (ONE / HALF) has stopped */
    uint8_t live;
} quad_voice_t;
static quad_voice_t quad_vs[QUAD_NPART][QUAD_POLY] __attribute__((section(".pool")));

static uint32_t quad_tr(const track_t *t) { return (uint32_t)(t - trk); }

static void quad_init_patch(int8_t *p)
{
    uint32_t i;
    for (i = 0; i < QP_NP; i++)
        p[i] = quad_range(i).def;
}

static int8_t quad_clampv(uint32_t i, int32_t v)
{
    quad_rng_t r = quad_range(i);
    return (int8_t)(v < r.min ? r.min : v > r.max ? r.max : v);
}

/* the patch -> blob: 'Q', version, a byte a value (value - min: 0..200), zeros */
static void quad_pack(const int8_t *p, uint8_t *b)
{
    uint32_t i;
    b[0] = QUAD_MAGIC;
    b[1] = QUAD_VER;
    for (i = 0; i < QP_NP; i++)
        b[2 + i] = (uint8_t)(quad_clampv(i, p[i]) - quad_range(i).min);
    for (i = 2 + QP_NP; i < QUAD_BLOB; i++)
        b[i] = 0;
}

static int quad_blob_ok(const uint8_t *b)
{
    uint32_t i;
    if (!b || b[0] != QUAD_MAGIC || b[1] != QUAD_VER)
        return 0;
    for (i = 0; i < QP_NP; i++) {
        quad_rng_t r = quad_range(i);
        if (b[2 + i] > (uint32_t)(r.max - r.min))
            return 0;
    }
    for (i = 2 + QP_NP; i < QUAD_BLOB; i++)
        if (b[i])
            return 0;
    return 1;
}

/* blob -> patch; 0 or a bad blob: the init patch. 1 = the blob was taken */
static int quad_unpack(const uint8_t *b, int8_t *p)
{
    uint32_t i;
    quad_init_patch(p);
    if (!quad_blob_ok(b))
        return 0;
    for (i = 0; i < QP_NP; i++)
        p[i] = (int8_t)(b[2 + i] + quad_range(i).min);
    return 1;
}

/* the macros (P_E0..P_E7 = patch values 0..7) from the patch: into the track, remembered (main loop) */
static void quad_macros_out(track_t *t)
{
    uint32_t tr = quad_tr(t), k;
    if (tr >= QUAD_NPART)
        return;
    for (k = 0; k < 8u; k++) {
        int16_t v = quad_patch[tr][k];
        t->p[P_E0 + k] = v;
        quad_mlast[tr][k] = v;
    }
}

/* ------------------------------------------------------------ presets --- */
/* a preset: the init patch with these (index, value) pairs, 0xFF ends. The ratio indices: RC / RA / RB (quad_tables.h;
 * QRC(r): C/B's step of ratio r (x4: 1 = 0.25 .. 4 = 1.00, then 8 = 2.00 ..), QRA(r): A's (x4), QRB(b1, br): the pair
 * B1 step (x4) and BR's index (0.5 1 1.5 2 3 4) */
#define S8(v) (uint8_t)(int8_t)(v)
#define QRC(q) ((q) <= 4 ? (q) - 1 : (q) / 4 + 2)
#define QRA(q) ((q) - 1)
#define QRB(q, br) ((br) * QUAD_NRCB + QRC(q))
#define SYN(al, rc, ra, rb, h, dt, fb, mx) QP_ALGO, al, QP_RC, rc, QP_RA, ra, QP_RB, rb, QP_HARM, S8(h), QP_DTUN, dt, \
                                           QP_FDBK, fb, QP_MIX, S8(mx)
#define ENVA(a, d, e, l) QP_AATK, a, QP_ADEC, d, QP_AEND, e, QP_ALEV, l
#define ENVB(a, d, e, l) QP_BATK, a, QP_BDEC, d, QP_BEND, e, QP_BLEV, l
#define AMP(a, d, s, r, l) QP_EATK, a, QP_EDEC, d, QP_ESUS, s, QP_EREL, r, QP_LEVEL, l
#define FLT(ty, f, r, dp) QP_FTYPE, QF_##ty, QP_FREQ, f, QP_RESO, r, QP_FDEPTH, S8(dp)
#define FENV(a, d, s, r) QP_FATK, a, QP_FDEC, d, QP_FSUS, s, QP_FREL, r
#define LFO(k, sp, mu, de, w, dp) QP_LFO(k, QL_SPEED), S8(sp), QP_LFO(k, QL_MULT), mu, QP_LFO(k, QL_DEST), QD_##de, \
                                  QP_LFO(k, QL_WAVE), QW_##w, QP_LFO(k, QL_DEPTH), S8(dp)
static const uint8_t QUADP_EP[] = {SYN(2, QRC(4), QRA(4), QRB(56, 1), 0, 10, 0, 0), ENVA(0, 80, 24, 72),
    ENVB(0, 46, 0, 44), QP_VEL, 100, QP_BKTRK, 40, AMP(0, 96, 40, 62, 72), 0xFF};
static const uint8_t QUADP_BELL[] = {SYN(4, QRC(4), QRA(14), QRB(8, 4), 0, 20, 0, 0), ENVA(0, 92, 30, 80),
    ENVB(0, 96, 20, 72), QP_VEL, 90, AMP(0, 106, 0, 100, 88), 0xFF};
static const uint8_t QUADP_BASS[] = {SYN(1, QRC(4), QRA(4), QRB(4, 1), 0, 0, 30, 0), ENVA(0, 62, 30, 86),
    ENVB(0, 50, 0, 40), FLT(LP, 92, 10, 20), FENV(0, 60, 0, 40), AMP(0, 80, 100, 30, 110), 0xFF};
static const uint8_t QUADP_PLUCK[] = {SYN(3, QRC(4), QRA(8), QRB(12, 1), 0, 6, 0, 0), ENVA(0, 56, 0, 90),
    ENVB(0, 42, 0, 60), AMP(0, 76, 0, 60, 72), 0xFF};
static const uint8_t QUADP_BRASS[] = {SYN(1, QRC(4), QRA(4), QRB(4, 1), 0, 8, 60, 0), ENVA(50, 80, 60, 76),
    ENVB(40, 80, 50, 50), FLT(LP, 80, 12, 30), FENV(50, 86, 60, 60), AMP(40, 80, 110, 60, 70), 0xFF};
static const uint8_t QUADP_GLASS[] = {SYN(7, QRC(4), QRA(8), QRB(16, 1), 8, 40, 0, 0), ENVA(70, 90, 100, 60),
    ENVB(80, 96, 60, 40), AMP(70, 90, 120, 100, 92), LFO(0, 8, 3, HARM, SINE, 30), 0xFF};
static const uint8_t QUADP_HOLLOW[] = {SYN(4, QRC(4), QRA(8), QRB(4, 1), 12, 0, 0, -20), ENVA(20, 90, 80, 50),
    ENVB(0, 80, 40, 30), AMP(30, 80, 110, 70, 88), 0xFF};
static const uint8_t QUADP_SQLEAD[] = {SYN(1, QRC(4), QRA(8), QRB(4, 1), 26, 0, 20, 0), ENVA(0, 80, 60, 40),
    ENVB(0, 70, 20, 20), AMP(5, 70, 110, 50, 120), 0xFF};
static const uint8_t QUADP_METAL[] = {SYN(3, QRC(4), QRA(6), QRB(28, 2), 0, 60, 50, 0), ENVA(0, 96, 40, 100),
    ENVB(0, 90, 30, 80), AMP(0, 100, 30, 90, 70), 0xFF};
static const uint8_t QUADP_WOBBLE[] = {SYN(1, QRC(4), QRA(4), QRB(4, 1), 0, 0, 20, 0), ENVA(0, 64, 127, 60),
    ENVB(0, 60, 40, 30), FLT(LP, 70, 60, 0), AMP(0, 80, 110, 40, 115), LFO(0, 32, 4, ALEV, TRI, 50),
    LFO(1, 32, 4, FREQ, TRI, 40), QP_LFO(0, QL_TRIG), QT_TRIG, QP_LFO(1, QL_TRIG), QT_TRIG, 0xFF};
static const uint8_t QUADP_CLAV[] = {SYN(4, QRC(4), QRA(12), QRB(4, 5), 4, 0, 10, 0), ENVA(0, 50, 10, 96),
    ENVB(0, 46, 0, 80), FLT(HP, 30, 20, 0), AMP(0, 70, 0, 45, 96), 0xFF};
static const uint8_t QUADP_STRINGS[] = {SYN(8, QRC(4), QRA(4), QRB(4, 1), -20, 70, 0, 0), ENVA(0, 60, 127, 100),
    ENVB(0, 60, 127, 100), FLT(LP, 86, 8, 0), AMP(70, 90, 115, 96, 90), LFO(0, 6, 3, DTUN, SINE, 30), 0xFF};
static const uint8_t QUADP_MARIMBA[] = {SYN(2, QRC(4), QRA(16), QRB(40, 1), 0, 0, 0, 0), ENVA(0, 50, 0, 64),
    ENVB(0, 30, 0, 40), AMP(0, 80, 0, 70, 78), 0xFF};
static const uint8_t QUADP_DRONE[] = {SYN(6, QRC(2), QRA(4), QRB(4, 2), 0, 50, 40, 0), ENVA(100, 100, 90, 70),
    ENVB(110, 100, 80, 60), AMP(64, 100, 127, 110, 110), LFO(0, 4, 3, HARM, SINE, 40), LFO(1, 5, 3, MIX, TRI, 50),
    LFO(2, 3, 3, FDBK, SINE, 30), 0xFF};
static const uint8_t QUADP_FEEDBACK[] = {SYN(5, QRC(4), QRA(4), QRB(4, 1), 0, 0, 90, 40), ENVA(0, 80, 40, 60),
    ENVB(0, 80, 70, 100), AMP(0, 90, 100, 70, 88), 0xFF};
static const uint8_t QUADP_NOISE[] = {SYN(7, QRC(4), QRA(4), QRB(4, 1), 0, 0, 127, -63), ENVA(0, 60, 127, 127),
    ENVB(0, 60, 0, 0), FLT(BP, 90, 40, 0), AMP(0, 90, 60, 80, 84), 0xFF};

/* {ALGO, RATIO C, RATIO A, RATIO B, HARM, DTUNE, FDBK, MIX}: the macros as the patch has them (cr_quad_test checks);
 * env: the amp envelope (QUAD's own: the platform ADSR is not used) */
static const preset_t QUAD_PRESETS[] = {
    {"EP", {2, QRC(4), QRA(4), QRB(56, 1), 0, 10, 0, 0}, {0, 96, 40, 62}, 0, 0, FX(0, 30, 20, 40), PAT(6)},
    {"BELL", {4, QRC(4), QRA(14), QRB(8, 4), 0, 20, 0, 0}, {0, 106, 0, 100}, 0, 0, FX(0, 20, 35, 60), PAT(7)},
    {"BASS", {1, QRC(4), QRA(4), QRB(4, 1), 0, 0, 30, 0}, {0, 80, 100, 30}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"PLUCK", {3, QRC(4), QRA(8), QRB(12, 1), 0, 6, 0, 0}, {0, 76, 0, 60}, 0, 0, FX(0, 20, 30, 40), PAT(6)},
    {"BRASS", {1, QRC(4), QRA(4), QRB(4, 1), 0, 8, 60, 0}, {40, 80, 110, 60}, 0, 0, FX(0, 25, 20, 40), PAT(6)},
    {"GLASS PAD", {7, QRC(4), QRA(8), QRB(16, 1), 8, 40, 0, 0}, {70, 90, 120, 100}, 0, 0, FX(0, 50, 25, 70), PAT(5)},
    {"HOLLOW", {4, QRC(4), QRA(8), QRB(4, 1), 12, 0, 0, -20}, {30, 80, 110, 70}, 0, 0, FX(0, 35, 25, 50), PAT(6)},
    {"SQUARE LEAD", {1, QRC(4), QRA(8), QRB(4, 1), 26, 0, 20, 0}, {5, 70, 110, 50}, 0, 1, FX(0, 25, 40, 40), PAT(4)},
    {"METAL", {3, QRC(4), QRA(6), QRB(28, 2), 0, 60, 50, 0}, {0, 100, 30, 90}, 0, 0, FX(0, 20, 30, 50), PAT(7)},
    {"WOBBLE", {1, QRC(4), QRA(4), QRB(4, 1), 0, 0, 20, 0}, {0, 80, 110, 40}, 0, 1, FX(10, 0, 15, 10), PAT(2)},
    {"CLAV", {4, QRC(4), QRA(12), QRB(4, 5), 4, 0, 10, 0}, {0, 70, 0, 45}, 0, 0, FX(0, 15, 20, 25), PAT(6)},
    {"STRINGS", {8, QRC(4), QRA(4), QRB(4, 1), -20, 70, 0, 0}, {70, 90, 115, 96}, 0, 0, FX(0, 55, 15, 65), PAT(5)},
    {"MARIMBA", {2, QRC(4), QRA(16), QRB(40, 1), 0, 0, 0, 0}, {0, 80, 0, 70}, 0, 0, FX(0, 15, 25, 40), PAT(7)},
    {"DRONE", {6, QRC(2), QRA(4), QRB(4, 2), 0, 50, 40, 0}, {64, 100, 127, 110}, 0, 0, FX(0, 50, 30, 80), PAT(5)},
    {"FEEDBACK", {5, QRC(4), QRA(4), QRB(4, 1), 0, 0, 90, 40}, {0, 90, 100, 70}, 0, 0, FX(0, 25, 30, 40), PAT(4)},
    {"NOISE-ISH", {7, QRC(4), QRA(4), QRB(4, 1), 0, 0, 127, -63}, {0, 90, 60, 80}, 0, 0, FX(0, 20, 30, 50), PAT(7)},
};
static const uint8_t *const QUAD_PRESET_EDITS[] = {QUADP_EP, QUADP_BELL, QUADP_BASS, QUADP_PLUCK, QUADP_BRASS,
    QUADP_GLASS, QUADP_HOLLOW, QUADP_SQLEAD, QUADP_METAL, QUADP_WOBBLE, QUADP_CLAV, QUADP_STRINGS, QUADP_MARIMBA,
    QUADP_DRONE, QUADP_FEEDBACK, QUADP_NOISE};
_Static_assert(NELEM(QUAD_PRESETS) == NELEM(QUAD_PRESET_EDITS) && NELEM(QUAD_PRESETS) == 16, "a patch per QUAD preset");
#define QUAD_NPRESETS ((uint32_t)NELEM(QUAD_PRESETS))
#undef S8
#undef SYN
#undef ENVA
#undef ENVB
#undef AMP
#undef FLT
#undef FENV
#undef LFO

static void quad_preset_patch(uint32_t k, int8_t *p)
{
    const uint8_t *e;
    quad_init_patch(p);
    if (k >= QUAD_NPRESETS)
        return;
    for (e = QUAD_PRESET_EDITS[k]; *e != 0xFFu; e += 2)
        p[e[0]] = quad_clampv(e[0], (int8_t)e[1]);
}

/* ------------------------------------------------- deep pages, the blob --- */
static uint32_t quad_index(uint32_t page, uint32_t col)
{
    return page >= QUAD_NPAGES || col >= 4u ? QUAD_X : QUAD_MAP[page][col];
}

static int32_t quad_get(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = quad_tr(t), i = quad_index(page, col);
    if (i == QUAD_X)
        return 0;
    if (i == QUAD_XPAN)
        return t->p[P_PAN];
    if (i == QUAD_XDIST)
        return t->p[P_DIST];
    if (tr >= QUAD_NPART)
        return quad_range(i).def;
    return quad_patch[tr][i];
}

static void quad_set(track_t *t, uint32_t page, uint32_t col, int32_t v)
{
    uint32_t tr = quad_tr(t), i = quad_index(page, col);
    if (i == QUAD_X)
        return;
    if (i == QUAD_XPAN || i == QUAD_XDIST) {     /* the part's own parameters (any part) */
        const param_desc_t *d = &QUAD_PAGES[page].col[col];
        t->p[i == QUAD_XPAN ? P_PAN : P_DIST] = (int16_t)clamp(v, d->min, d->max);
        return;
    }
    if (tr >= QUAD_NPART)
        return;
    quad_patch[tr][i] = quad_clampv(i, v);
    if (i < 8u) {                                /* a macro's value: the track's P_E too (quad_block sees no change) */
        t->p[P_E0 + i] = quad_patch[tr][i];
        RING_PUBLISH();
        quad_mlast[tr][i] = quad_patch[tr][i];
    }
}

static void quad_blob_get(const track_t *t, uint8_t *out)
{
    uint32_t tr = quad_tr(t);
    int8_t p[QP_NP];
    if (tr < QUAD_NPART)
        quad_pack(quad_patch[tr], out);
    else {
        quad_init_patch(p);
        quad_pack(p, out);
    }
}

static void quad_blob_set(track_t *t, const uint8_t *in)
{
    uint32_t tr = quad_tr(t);
    int8_t p[QP_NP];
    if (tr >= QUAD_NPART)
        return;
    quad_unpack(in, p);
    memcpy(quad_patch[tr], p, QP_NP);
    quad_macros_out(t);
}

static void quad_blob_preset(track_t *t, uint32_t k)
{
    uint32_t tr = quad_tr(t);
    int8_t p[QP_NP];
    if (tr >= QUAD_NPART)
        return;
    quad_preset_patch(k, p);
    memcpy(quad_patch[tr], p, QP_NP);
    quad_macros_out(t);
}

/* a sound load put QUAD into track t (to be called on every load path, as va_track_loaded): a user slot
 * (quad_user_pending): its stored patch, else a preset whose macros the track holds: its patch, else the init patch
 * with the track's macros on it */
static void quad_track_loaded(const track_t *ct)
{
    track_t *t = (track_t *)ct;
    uint32_t tr = quad_tr(t), pend = quad_user_pending, k;
    uint8_t b[QUAD_BLOB];
    int8_t p[QP_NP];
    quad_user_pending = 0;
    if (tr >= QUAD_NPART || t->eng_req != ENGI_QUAD)
        return;
    if (pend && quad_store_read && !quad_store_read(pend - 1u, b) && quad_unpack(b, p)) {
        memcpy(quad_patch[tr], p, QP_NP);
        quad_macros_out(t);
        return;
    }
    if (!pend && t->preset < QUAD_NPRESETS) {
        for (k = 0; k < 8u && t->p[P_E0 + k] == QUAD_PRESETS[t->preset].e[k]; k++)
            ;
        if (k == 8u) {
            quad_blob_preset(t, t->preset);
            return;
        }
    }
    quad_init_patch(p);
    for (k = 0; k < 8u; k++)
        p[k] = quad_clampv(k, t->p[P_E0 + k]);
    memcpy(quad_patch[tr], p, QP_NP);
    quad_macros_out(t);
}

/* ------------------------------------------------------------ the ISR --- */
static uint32_t quad_xs(uint32_t s)              /* xorshift32 (the LFOs' RAND) */
{
    s = s ? s : 0x6C8E9CF5u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* LFO k's phase increment per control tick: SPEED x MULT x QUAD_LFO_K (f = SPEED x MULT / 320 Hz; negative runs
 * backwards, 0 stops) */
static int32_t quad_lfo_inc(const int8_t *p, uint32_t k)
{
    uint32_t mu = (uint32_t)p[QP_LFO(k, QL_MULT)];
    return (int32_t)p[QP_LFO(k, QL_SPEED)] * (int32_t)(1u << (mu < QUAD_NMULT ? mu : 0u)) * QUAD_LFO_K;
}

/* LFO wave w at phase ph (r: the random value, Q15): Q15 */
static int32_t quad_lfo_wave(uint32_t w, uint32_t ph, int32_t r)
{
    int32_t u;
    switch (w) {
    case QW_SINE: return sine_i(ph);
    case QW_SQR: return ph < 0x80000000u ? 32767 : -32767;
    case QW_SAW: return (int32_t)((ph + 0x80000000u) >> 16) - 32768;          /* bipolar, rising through 0 */
    case QW_RAMP: return 32767 - (int32_t)(ph >> 17);                           /* unipolar, falling */
    case QW_EXP:                                                                /* unipolar, a steep fall: (1 - x)^4 */
        u = 32767 - (int32_t)(ph >> 17);
        u = (u * u) >> 15;
        return (u * u) >> 15;
    case QW_RAND: return r;
    default: return osc_tri(ph + 0x40000000u);                                  /* (TRI: 0 at phase 0, rising) */
    }
}

/* once a block and part: the macros into the patch, the FREE LFOs' phases */
static void quad_block(track_t *t)
{
    uint32_t tr = quad_tr(t), k;
    int8_t *p;
    if (tr >= QUAD_NPART)
        return;
    p = quad_patch[tr];
    for (k = 0; k < 8u; k++)                     /* a knob, the editor, the matrix, motion moved a macro */
        if (t->p[P_E0 + k] != quad_mlast[tr][k]) {
            quad_mlast[tr][k] = t->p[P_E0 + k];
            p[k] = quad_clampv(k, t->p[P_E0 + k]);
        }
    for (k = 0; k < 3u; k++) {
        uint32_t old = quad_lfo[tr].ph[k];
        int32_t inc = quad_lfo_inc(p, k);
        quad_lfo[tr].ph[k] = old + (uint32_t)inc;
        if (inc > 0 ? quad_lfo[tr].ph[k] < old : inc < 0 && quad_lfo[tr].ph[k] > old)
            quad_lfo[tr].rnd[k] = quad_xs(quad_lfo[tr].rnd[k] + k);
    }
}

static quad_voice_t *quad_voice(track_t *t, voice_t *v)
{
    uint32_t tr = quad_tr(t), i = (uint32_t)(v - t->v);
    return tr < QUAD_NPART && i < QUAD_POLY ? &quad_vs[tr][i] : 0;
}

/* (re)start envelope k (0 A, 1 B, 2 filter, 3 amp) with its delay d; from 0 when reset */
static void quad_env_start(quad_voice_t *s, uint32_t k, int32_t d, int reset)
{
    if (reset)
        s->env[k] = 0;
    s->dly[k] = 0;
    s->stage[k] = d ? 1 : 2;
}

/* the LFOs at a note-on: TRIG / ONE / HALF start at PHASE, HOLD takes the part's phase (and keeps it) */
static void quad_lfo_on(track_t *t, quad_voice_t *s, const int8_t *p)
{
    uint32_t tr = quad_tr(t), k;
    for (k = 0; k < 3u; k++) {
        uint32_t tm = (uint32_t)p[QP_LFO(k, QL_TRIG)];
        s->ltr[k] = 0;
        s->lstop &= (uint8_t)~(1u << k);
        s->lrnd[k] = quad_xs(s->lrnd[k] + 0x9E3779B9u * (k + 1u) + (uint32_t)(s - quad_vs[0]));
        if (tm == QT_HOLD) {
            s->lph[k] = quad_lfo[tr].ph[k] + ((uint32_t)p[QP_LFO(k, QL_PHASE)] << 25);
            s->lrnd[k] = quad_lfo[tr].rnd[k];
        } else {
            s->lph[k] = (uint32_t)p[QP_LFO(k, QL_PHASE)] << 25;
        }
    }
    s->ticks = 0;
}

static void quad_note_on(track_t *t, voice_t *v)
{
    quad_voice_t *s = quad_voice(t, v);
    const int8_t *p;
    uint32_t k, fresh;
    if (!s)
        return;
    p = quad_patch[quad_tr(t)];
    fresh = (!v->env && !v->env_out) || !s->live;
    if (fresh) {                                 /* from silence: phases, filters, envelopes, ramps from 0 */
        for (k = 0; k < 4u; k++) {
            s->ph[k] = 0;
            s->env[k] = 0;
        }
        s->fb1 = s->fb2 = 0;
        s->lv[0] = s->lv[1] = 0;
        s->amp = 0;
        s->f1 = s->f2 = 0;
        s->bh = s->bl = 0;
        s->dc = 0;
        quad_env_start(s, 0, p[QP_ADLY], 1);
        quad_env_start(s, 1, p[QP_BDLY], 1);
    } else {                                     /* a retrigger: TRIG restarts the operator envelopes (RESET: from
                                                  * 0, else from where they are); PHASE RESET the phases */
        if (p[QP_PHRST]) {
            for (k = 0; k < 4u; k++)
                s->ph[k] = 0;
            s->fb1 = s->fb2 = 0;
        }
        if (p[QP_ATRIG])
            quad_env_start(s, 0, p[QP_ADLY], p[QP_ARST]);
        if (p[QP_BTRIG])
            quad_env_start(s, 1, p[QP_BDLY], p[QP_BRST]);
    }
    quad_env_start(s, 2, p[QP_FDLY], fresh);     /* filter and amp: the attack from the current level */
    quad_env_start(s, 3, 0, fresh);
    quad_lfo_on(t, s, p);
    s->live = 1;
}

/* MONO / LEGATO moved the voice to a new note without a new attack: TRIG on restarts the operator envelopes (the
 * Digitone's "every note"), off holds them (legato) */
static void quad_legato(track_t *t, voice_t *v)
{
    quad_voice_t *s = quad_voice(t, v);
    const int8_t *p;
    if (!s)
        return;
    p = quad_patch[quad_tr(t)];
    if (p[QP_ATRIG])
        quad_env_start(s, 0, p[QP_ADLY], p[QP_ARST]);
    if (p[QP_BTRIG])
        quad_env_start(s, 1, p[QP_BDLY], p[QP_BRST]);
}

/* one control tick of an envelope (Q24 x, stage st, delay progress dp): DELAY, then ATK (linear to 1), then DEC
 * (exponential to sus, Q24), held; adsr: the gate's end releases (REL, exponential to 0, then off). Q15 */
static int32_t quad_env_tick(quad_voice_t *s, uint32_t k, int32_t atk, int32_t dec, int32_t sus, int32_t rel,
                             int32_t dly, int gate, int adsr)
{
    int32_t x = s->env[k];
    if (adsr && !gate && s->stage[k] && s->stage[k] < 4u)
        s->stage[k] = 4;
    switch (s->stage[k]) {
    case 1:
        s->dly[k] += ENV_LIN[dly & 127];
        if (s->dly[k] >= (1u << 24))
            s->stage[k] = 2;
        break;
    case 2:
        x += (int32_t)ENV_LIN[atk & 127];
        if (x >= (1 << 24)) {
            x = 1 << 24;
            s->stage[k] = 3;
        }
        break;
    case 3:
        x += mulq16(sus - x, ENV_EXP[dec & 127]);
        break;
    case 4:
        x -= mulq16(x, ENV_EXP[rel & 127]);
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

static int quad_done(track_t *t, voice_t *v)    /* the amp envelope has ended (voice.c, engine_t.done) */
{
    quad_voice_t *s = quad_voice(t, v);
    if (!s || !s->live || !s->stage[3]) {
        if (s)
            s->live = 0;
        return 1;
    }
    return 0;
}

/* the ratio (Q16) of table value r plus the fine offset o (1/100), at least 0 */
static int32_t quad_ratio(int32_t r, int32_t o)
{
    r += o * 65536 / 100;
    return r < 0 ? 0 : r;
}
/* an operator's phase increment: the voice's x ratio (Q16) x a detune factor (Q16) */
static uint32_t quad_inc(uint32_t inc, int32_t r, int32_t det)
{
    uint64_t q = ((uint64_t)(uint32_t)r * (uint32_t)(65536 + det)) >> 16;
    return (uint32_t)(((uint64_t)inc * q) >> 16);
}

/* the carriers' HARM wave: tables a and b (HARM's neighbours, one index), b's share f (Q15) */
static inline int32_t quad_hw(const int16_t *a, const int16_t *b, int32_t f, uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t fr = (int32_t)((ph >> 7) & 0x7FFFu), x0 = a[i], x = x0 + (((a[i + 1u] - x0) * fr) >> 15), y0, y;
    if (!f)
        return x;
    y0 = b[i];
    y = y0 + (((b[i + 1u] - y0) * fr) >> 15);
    return x + (((y - x) * f) >> 15);
}

/* the DC blocker after the operators: a one-pole high-pass at ~8 Hz (Q16: 65536 (1 - exp(-2 pi 8 / FS))). The
 * operators are not DC-free: the DX7-style feedback (its average lags 1.5 samples) skews the feedback operator's saw,
 * its mean -12 % of its RMS at FDBK 60 (-61 % at 100) on a plain sine, and at near-unison ratios (1:1) a modulator's
 * phase offset (that mean, or DTUNE's slow drift: 0.06..0.5 Hz at DTUNE 8) puts J1(I) sin(offset) into the carrier at
 * 0 Hz (BRASS: +57 % of its RMS). A DX7 / Digitone AC-couples its output; here every voice does, before the SVF and its
 * knee */
#define QUAD_DC_K 75

/* the operator outputs: QW a modulator's sine, QCW a carrier's (HARM: defined per loop), QM a modulator's output as a
 * phase offset (Q15 -> 2 cycles at full level), QFB the feedback operator's self-modulation (its wave's last two
 * values fb1, fb2: their sum x FDBK^2 x 2, 2^32 = a cycle), QLV an output at a level */
#define QW(ph) sine_i(ph)
#define QM(o) ((uint32_t)(o) << 18)
#define QFB() ((uint32_t)((fb1 + fb2) * fbq) << 1)
#define QLV(y, l) (((y) * (l)) >> 15)
/* the operator loop of one algorithm: the level ramps, BODY (sets X, Y and yf, the feedback operator's wave), the
 * feedback memory, the phases, MIX (X .. Y, gx + gy = 1) at half scale into acc */
#define QUAD_LOOP(BODY)                                                    \
    for (i = 0; i < n; i++) {                                              \
        int32_t LA, LB, X, Y, yf, oa, ob1, ob2;                            \
        la += dla;                                                         \
        lb += dlb;                                                         \
        LA = la >> 16;                                                     \
        LB = lb >> 16;                                                     \
        BODY                                                               \
        fb2 = fb1;                                                         \
        fb1 = yf;                                                          \
        p0 += i0;                                                          \
        p1 += i1;                                                          \
        p2 += i2;                                                          \
        p3 += i3;                                                          \
        acc[i] = (int32_t)(((X * gx) + (Y * gy)) >> 15) >> 1;              \
    }
/* the eight routings (QUAD_ALGO) */
#define QUAD_ALGOS                                                                                          \
    switch (alg) {                                                                                          \
    case 0: /* B2 (fb) > B1 > A > C */                                                                      \
        QUAD_LOOP(yf = QW(p3 + QFB()); ob2 = QLV(yf, LB); ob1 = QLV(QW(p2 + QM(ob2)), LB);                  \
                  oa = QLV(QW(p1 + QM(ob1)), LA); X = Y = QCW(p0 + QM(oa));)                                \
        break;                                                                                              \
    case 1: /* B2 (fb) > B1 > C, A > C */                                                                   \
        QUAD_LOOP(yf = QW(p3 + QFB()); ob2 = QLV(yf, LB); ob1 = QLV(QW(p2 + QM(ob2)), LB);                  \
                  oa = QLV(QW(p1), LA); X = Y = QCW(p0 + QM(oa) + QM(ob1));)                                \
        break;                                                                                              \
    case 2: /* B1 > A, B2 (fb) > A, A > C */                                                                \
        QUAD_LOOP(yf = QW(p3 + QFB()); ob2 = QLV(yf, LB); ob1 = QLV(QW(p2), LB);                            \
                  oa = QLV(QW(p1 + QM(ob1) + QM(ob2)), LA); X = Y = QCW(p0 + QM(oa));)                      \
        break;                                                                                              \
    case 3: /* A > C (X), B2 (fb) > B1 (Y) */                                                               \
        QUAD_LOOP(yf = QW(p3 + QFB()); ob2 = QLV(yf, LB); oa = QLV(QW(p1), LA); X = QCW(p0 + QM(oa));       \
                  Y = QLV(QCW(p2 + QM(ob2)), LB); (void)ob1;)                                               \
        break;                                                                                              \
    case 4: /* B1 > A > C (X), B2 (fb) (Y) */                                                               \
        QUAD_LOOP(ob1 = QLV(QW(p2), LB); oa = QLV(QW(p1 + QM(ob1)), LA); X = QCW(p0 + QM(oa));              \
                  yf = QCW(p3 + QFB()); Y = QLV(yf, LB); (void)ob2;)                                        \
        break;                                                                                              \
    case 5: /* A > C (X), A > B1 < B2 (fb) (Y) */                                                           \
        QUAD_LOOP(yf = QW(p3 + QFB()); ob2 = QLV(yf, LB); oa = QLV(QW(p1), LA); X = QCW(p0 + QM(oa));       \
                  Y = QLV(QCW(p2 + QM(oa) + QM(ob2)), LB); (void)ob1;)                                      \
        break;                                                                                              \
    case 6: /* C + A (fb) (X), B2 > B1 (Y) */                                                               \
        QUAD_LOOP(yf = QCW(p1 + QFB()); X = QCW(p0) + QLV(yf, LA); ob2 = QLV(QW(p3), LB);                   \
                  Y = QLV(QCW(p2 + QM(ob2)), LB); (void)oa; (void)ob1;)                                     \
        break;                                                                                              \
    default: /* C + A (X), B1 + B2 (fb) (Y) */                                                              \
        QUAD_LOOP(yf = QCW(p3 + QFB()); X = QCW(p0) + QLV(QCW(p1), LA); Y = QLV(QCW(p2), LB) + QLV(yf, LB); \
                  (void)oa; (void)ob1; (void)ob2;)                                                          \
        break;                                                                                              \
    }

/* the voice's block: out gets it */
static void quad_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    quad_voice_t *s = quad_voice(t, v);
    uint32_t tr = quad_tr(t), k, i, alg, inc[4];
    const int8_t *p;
    int32_t eA, eB, eF, eE, dst[QD_N], A0, A1, dA, a, x, y;
    int32_t harm, dt, fdbk, mix, ra, rb1i, br, cut, kd, reso, gx, gy, fbq, hm, hf;
    int32_t la, lb, dla, dlb, tl[2], hpk, lpk, svf, fty, ic1, ic2, fb1, fb2, bh, bl;
    const int16_t *ha_t = QUAD_HARM[0], *hb_t = QUAD_HARM[0];
    uint32_t p0, p1, p2, p3, i0, i1, i2, i3;
    int32_t acc[CTL];
    tsvf_t c;
    if (!s || !s->live || n > CTL)
        return;
    p = quad_patch[tr];
    /* the envelopes */
    eA = quad_env_tick(s, 0, p[QP_AATK], p[QP_ADEC], p[QP_AEND] << 17, 0, p[QP_ADLY], 1, 0);
    eB = quad_env_tick(s, 1, p[QP_BATK], p[QP_BDEC], p[QP_BEND] << 17, 0, p[QP_BDLY], 1, 0);
    eF = quad_env_tick(s, 2, p[QP_FATK], p[QP_FDEC], p[QP_FSUS] << 17, p[QP_FREL], p[QP_FDLY], v->gate, 1);
    eE = quad_env_tick(s, 3, p[QP_EATK], p[QP_EDEC], p[QP_ESUS] << 17, p[QP_EREL], 0, v->gate, 1);
    /* the LFOs: read, then advance; summed into their destinations (Q15 each) */
    for (k = 0; k < QD_N; k++)
        dst[k] = 0;
    for (k = 0; k < 3u; k++) {
        uint32_t d = (uint32_t)p[QP_LFO(k, QL_DEST)], tm = (uint32_t)p[QP_LFO(k, QL_TRIG)], ph, rv;
        int32_t inc0 = quad_lfo_inc(p, k), w, f;
        if (tm == QT_FREE) {
            ph = quad_lfo[tr].ph[k] + ((uint32_t)p[QP_LFO(k, QL_PHASE)] << 25);
            rv = quad_lfo[tr].rnd[k];
        } else {
            ph = s->lph[k];
            rv = s->lrnd[k];
            if (tm != QT_HOLD && !((s->lstop >> k) & 1u)) {        /* TRIG ONE HALF: the voice's own phase runs */
                uint32_t old = ph, st = (uint32_t)(inc0 < 0 ? -inc0 : inc0) >> 1;
                if (tm != QT_TRIG) {                               /* ONE: a cycle (2^31 halves), HALF: half */
                    uint32_t lim = tm == QT_ONE ? 0x80000000u : 0x40000000u;
                    if (s->ltr[k] + st >= lim) {
                        st = lim - s->ltr[k];
                        s->lstop |= (uint8_t)(1u << k);
                    }
                    s->ltr[k] += st;
                }
                s->lph[k] = old + (inc0 < 0 ? (uint32_t)-(int32_t)(st << 1) : st << 1);
                if (inc0 > 0 ? s->lph[k] < old : inc0 < 0 && s->lph[k] > old)
                    s->lrnd[k] = quad_xs(s->lrnd[k] + k);
            }
        }
        if (!d || d >= QD_N || !p[QP_LFO(k, QL_DEPTH)])
            continue;
        w = quad_lfo_wave((uint32_t)p[QP_LFO(k, QL_WAVE)], ph, (int32_t)(rv >> 16) - 32768);
        w = (w * p[QP_LFO(k, QL_DEPTH)]) >> 6;
        f = p[QP_LFO(k, QL_FADE)] ? (int32_t)s->ticks * (int32_t)(ENV_LIN[p[QP_LFO(k, QL_FADE)] & 127] >> 9) : 32767;
        if (f < 32767)
            w = (w * f) >> 15;
        dst[d] += w;
    }
    if (s->ticks < 0xFFFFu)
        s->ticks++;
    if (v - t->v == t->m_vi)                     /* the part's PAN: from the latest note's voice */
        quad_pan_off[tr] = (int8_t)clamp((dst[QD_PAN] * 64) >> 15, -64, 63);
    /* the operators: ratios, detune, increments */
    alg = (uint32_t)clamp(p[QP_ALGO], 1, 8) - 1u;
    dt = clamp(p[QP_DTUN] + ((dst[QD_DTUN] * 127) >> 15), 0, 127);
    ra = clamp(p[QP_RA] + ((dst[QD_RA] * 32) >> 15), 0, QUAD_NRA - 1);
    rb1i = clamp(p[QP_RB], 0, QUAD_NRB - 1);
    br = rb1i / QUAD_NRCB;
    rb1i = clamp(rb1i % QUAD_NRCB + ((dst[QD_RB] * 18) >> 15), 0, QUAD_NRCB - 1);
    {
        int32_t rc = quad_ratio(QUAD_RCB_Q16[clamp(p[QP_RC], 0, QUAD_NRCB - 1)], p[QP_OFSC]);
        int32_t rA = quad_ratio(QUAD_RA_Q16[ra], p[QP_OFSA]);
        int32_t rB1 = quad_ratio(QUAD_RCB_Q16[rb1i], p[QP_OFSB1]);
        int32_t rB2 = quad_ratio((int32_t)(((int64_t)QUAD_RCB_Q16[rb1i] * QUAD_BR_Q16[br]) >> 16), p[QP_OFSB2]);
        int32_t db = (dt * 15) >> 1, da = (dt * 15) >> 3;   /* B1 / B2 +-25 cents at 127, A +6 cents */
        inc[0] = quad_inc(m->inc, rc, 0);
        inc[1] = quad_inc(m->inc, rA, da);
        inc[2] = quad_inc(m->inc, rB1, -db);
        inc[3] = quad_inc(m->inc, rB2, db);
    }
    /* HARM: the two tables and the share of the second, Q15 */
    harm = clamp(p[QP_HARM] + ((dst[QD_HARM] * 26) >> 15), -26, 26);
    hm = harm != 0;
    hf = 0;
    if (hm) {
        int32_t h = harm < 0 ? -harm : harm, q = h * 7 * 32768 / 26, t0 = q >> 15, t1 = t0 < 7 ? t0 + 1 : 7;
        hf = t1 > t0 ? q & 32767 : 0;
        ha_t = QUAD_HARM[t0 ? (harm < 0 ? 7 + t0 : t0) : 0];
        hb_t = QUAD_HARM[harm < 0 ? 7 + t1 : t1];
    }
    fdbk = clamp(p[QP_FDBK] + ((dst[QD_FDBK] * 127) >> 15), 0, 127);
    fbq = fdbk * fdbk * 2;
    mix = clamp(p[QP_MIX] + ((dst[QD_MIX] * 126) >> 15), -63, 63);
    gx = (63 - mix) * 260;
    gy = (63 + mix) * 260;
    /* the operator levels: envelope x LEV (squared) x velocity x key track, Q15 */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, velf = 32767 - ((p[QP_VEL] * (127 - vel) * 2080) >> 10), j;
        for (j = 0; j < 2; j++) {
            int32_t l = clamp(p[j ? QP_BLEV : QP_ALEV] + (((j ? dst[QD_BLEV] : dst[QD_ALEV]) * 127) >> 15), 0, 127);
            int32_t kt = p[j ? QP_BKTRK : QP_AKTRK], g;
            l = mulq15(l * l * 2, velf);
            if (kt) {
                g = clamp(32768 + (((m->pitch16 - 60 * 16) * kt * 43) >> 7), 0, 65535);
                l = clamp((l * g) >> 15, 0, 32767);
            }
            tl[j] = mulq15(j ? eB : eA, l);
        }
    }
    la = (int32_t)s->lv[0] << 16;
    lb = (int32_t)s->lv[1] << 16;
    dla = ((tl[0] << 16) - la) >> CTL_LOG2;
    dlb = ((tl[1] << 16) - lb) >> CTL_LOG2;
    /* the filter: FREQ, its envelope by DEPTH, key track, the LFOs */
    cut = (p[QP_FREQ] << 8) + ((eF * p[QP_FDEPTH]) >> 6) + (((m->pitch16 - 60 * 16) * p[QP_FKTRK] * 150) >> 10) +
          ((dst[QD_FREQ] * 127) >> 7);
    reso = clamp(p[QP_RESO] + ((dst[QD_RESO] * 127) >> 15), 0, 127);
    fty = p[QP_FTYPE];
    svf = !(fty == QF_LP && cut >= (127 << 8) && !reso);   /* LP wide open, no resonance: no SVF */
    kd = 8192 - reso * 60;
    if (svf)
        tsvf_coef_k(&c, cut, kd);
    hpk = p[QP_BASE] ? (int32_t)QUAD_BW_K[p[QP_BASE] & 127] : 0;
    lpk = p[QP_BASE] + p[QP_WIDTH] < 127 ? (int32_t)QUAD_BW_K[(p[QP_BASE] + p[QP_WIDTH]) & 127] : 0;
    /* the amplitude: the amp envelope x velocity (half) x LEVEL (squared), the voice's (fades, LFO -> AMP) */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, lv = clamp(p[QP_LEVEL] + ((dst[QD_LEVEL] * 127) >> 15), 0, 127);
        A1 = mulq15(mulq15(eE, 32767 - (127 - vel) * 129), lv * lv * 2);
        if (t->p[P_VOICE] == V_UNISON)
            A1 = (A1 * 13107) >> 15;             /* 2 / 5 */
        A1 = mulq15(A1, m->amp1);
    }
    A0 = s->amp;
    dA = (A1 - A0) >> CTL_LOG2;
    a = A0;
    /* the operators and MIX into acc: a loop per algorithm, HARM or sine carriers */
    p0 = s->ph[0], p1 = s->ph[1], p2 = s->ph[2], p3 = s->ph[3];
    i0 = inc[0], i1 = inc[1], i2 = inc[2], i3 = inc[3];
    fb1 = s->fb1, fb2 = s->fb2;
    if (hm) {
#define QCW(ph) quad_hw(ha_t, hb_t, hf, ph)
        QUAD_ALGOS
#undef QCW
    } else {
#define QCW(ph) sine_i(ph)
        QUAD_ALGOS
#undef QCW
    }
    /* the DC blocker (QUAD_DC_K) */
    {
        int32_t d = s->dc;
        for (i = 0; i < n; i++) {
            x = acc[i];
            d += (int32_t)(((((int64_t)x << 12) - d) * QUAD_DC_K) >> 16);
            acc[i] = x - ((d + 2048) >> 12);
        }
        s->dc = d;
    }
    /* the SVF, a loop per type (LP v2, HP x - kd v1 - v2, BP kd v1), the soft knee after it */
    if (svf) {
        ic1 = s->f1, ic2 = s->f2;
#define QUAD_SVF(Y)                                                        \
    for (i = 0; i < n; i++) {                                              \
        int32_t v1, v2, v3;                                                \
        x = acc[i];                                                        \
        v3 = x - ic2;                                                      \
        v1 = (c.a1 * ic1 + c.a2 * v3) >> 13;                               \
        v2 = ic2 + ((c.a2 * ic1 + c.a3 * v3) >> 13);                       \
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);                        \
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);                        \
        acc[i] = soft_knee(clamp((Y), -200000, 200000), 16000);            \
    }
        if (fty == QF_BP)
            QUAD_SVF((kd * v1) >> 12)
        else if (fty == QF_HP)
            QUAD_SVF(x - ((kd * v1) >> 12) - v2)
        else
            QUAD_SVF(v2)
#undef QUAD_SVF
        s->f1 = ic1, s->f2 = ic2;
    }
    /* the base-width filter (BASE: a one-pole high-pass, BASE + WIDTH: a one-pole low-pass, Q8 states), the amplitude */
    bh = s->bh, bl = s->bl;
    for (i = 0; i < n; i++) {
        y = acc[i];
        if (hpk) {
            bh += (int32_t)(((int64_t)((y << 8) - bh) * hpk) >> 16);
            y -= bh >> 8;
        }
        if (lpk) {
            bl += (int32_t)(((int64_t)((y << 8) - bl) * lpk) >> 16);
            y = bl >> 8;
        }
        y = clamp(y, -65535, 65535);
        a += dA;
        out[i] += (mulq15(y, a) * (VOICE_FS / 4)) >> 11;
    }
    s->ph[0] = p0, s->ph[1] = p1, s->ph[2] = p2, s->ph[3] = p3;
    s->fb1 = fb1, s->fb2 = fb2;
    s->bh = bh, s->bl = bl;
    s->lv[0] = (int16_t)tl[0];
    s->lv[1] = (int16_t)tl[1];
    s->amp = A1;
}
#undef QW
#undef QM
#undef QFB
#undef QLV
#undef QUAD_LOOP
#undef QUAD_ALGOS

/* fx.c mix_part: the part's pan with the LFOs' PAN */
static int32_t quad_pan(const track_t *t, int32_t pan)
{
    uint32_t tr = quad_tr(t);
    if (t->engine != ENGI_QUAD || tr >= QUAD_NPART || !quad_pan_off[tr])
        return pan;
    return clamp(pan + quad_pan_off[tr], -64, 63);
}

/* --------------------------------------------------------- the engine --- */
/* the editor's screens (cr_edit.c's screen plan, docs/EDITOR.md §4; the user-approved mock-ups
 * design/choralroot-fm1-quad-screens.png): OSC SYN 1 (+ SYN 1+) under the algorithm, SYN 2; FILT the filter and its
 * envelope, the base-width window; ENV the operator envelopes A / B, their delays and modes, the key tracks, the amp
 * envelope; LFO one screen an LFO (Wave + Phase one double cell). Labels are the mock-ups' */
#define QS_N ENG_C_NUM
#define QS_T ENG_C_TEXT
#define QUAD_LSCR(k)                                                                                               \
    {3, {QUAD_PG_LFO + 2 * ((k) - 1), QUAD_PG_LFO + 2 * ((k) - 1) + 1}, ENG_B_NONE, 1, "LFO " #k,                              \
     {{ENG_C_KNOB, QS_T, QS_N, QS_T}, {ENG_C_WAVEPH, 0, QS_T, QS_N}},                                              \
     {{"Speed", "Mult", "Fade", "Dest"}, {"Wave \267 Phase", 0, "Trig", "Depth"}}}
static const eng_screen_t QUAD_SCREENS[] = {
    {0, {0, 1}, ENG_B_ALGO, 1, "SYN 1", {{ENG_C_BIG, ENG_C_BIG, ENG_C_BIG, ENG_C_RATIO},
                                         {ENG_C_HARM, ENG_C_DETUNE, ENG_C_BAR, QS_N}},
     {{"Algo", "Ratio C", "Ratio A", "Ratio B"}, {"Harm", "Dtune", "Feedback", "Mix"}}},
    {0, {2, 0xFF}, ENG_B_NONE, 0, "SYN 2", {{QS_T, QS_T, QS_T, QS_T}},
     {{"Offset C", "Offset A", "Offset B1", "Offset B2"}}},
    {1, {3, 4}, ENG_B_FILTER, 1, "FILTER", {{QS_N, QS_N, QS_N, QS_N}, {ENG_C_VAL, QS_N, QS_T, QS_N}},
     {{"Attack", "Decay", "Sustain", "Release"}, {"Freq", "Reso", "Type", "Env depth"}}},
    {1, {5, 6}, ENG_B_WINDOW, 1, "FILTER 2", {{QS_N, QS_N}, {QS_N, QS_N}},
     {{"Env delay", "Key track"}, {"Base", "Width"}}},
    {2, {7, 8}, ENG_B_ADE2, 0, "ENV A/B", {{QS_N, QS_N, QS_N, QS_N}, {QS_N, QS_N, QS_N, QS_N}},
     {{"A Attack", "A Decay", "A End", "A Level"}, {"B Attack", "B Decay", "B End", "B Level"}}},
    {2, {9, 10}, ENG_B_NONE, 0, "ENV 2", {{QS_N, QS_T, QS_T, QS_T}, {QS_N, QS_T, QS_T, QS_N}},
     {{"A Delay", "A Trig", "A Reset", "Phase"}, {"B Delay", "B Trig", "B Reset", "Velocity"}}},
    {2, {11, 0xFF}, ENG_B_NONE, 0, "ENV 3", {{QS_N, QS_N}}, {{"A Key", "B Key"}}},
    {2, {12, 13}, ENG_B_ENV, 1, "AMP", {{QS_N, QS_N, QS_N, QS_N}, {QS_N, QS_N, QS_N}},
     {{"Attack", "Decay", "Sustain", "Release"}, {"Level", "Pan", "Drive"}}},
    QUAD_LSCR(1), QUAD_LSCR(2), QUAD_LSCR(3),
};
#undef QS_N
#undef QS_T
#undef QUAD_LSCR

static const eng_deep_t QUAD_DEEP = {
    .npages = NELEM(QUAD_PAGES),
    .pages = QUAD_PAGES,
    .section = {QUAD_PG_OSC, QUAD_PG_FILTER, QUAD_PG_ENV, QUAD_PG_LFO, 0xFF, 0xFF, 0xFF, 0xFF},   /* MOD: none */
    .get = quad_get,
    .set = quad_set,
    .blob_size = QUAD_BLOB,
    .blob_get = quad_blob_get,
    .blob_set = quad_blob_set,
    .blob_preset = quad_blob_preset,
    .screens = QUAD_SCREENS,
    .nscreens = NELEM(QUAD_SCREENS),
};

static const engine_t ENG_QUAD = {
    .name = "QUAD",
    .page_title = {"SYN 1", "SYN 1+"},
    .edit = {QC_ALGO, QC_RC, QC_RA, QC_RB, QC_HARM, QC_DTUN, QC_FDBK, QC_MIX},
    .presets = QUAD_PRESETS,
    .npresets = NELEM(QUAD_PRESETS),
    .knob = {P_E2, P_E4, P_E6, P_E7},            /* HOME: RATIO A, HARM, FDBK, MIX */
    .poly = QUAD_POLY,
    .note_on = quad_note_on,
    .render = quad_render,
    .block = quad_block,
    .ownenv = 1,
    .done = quad_done,
    .legato = quad_legato,
    .deep = &QUAD_DEEP,
};
