/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* QUAD (FM TONE on the device): a Digitone-style four-operator FM voice (ChoralRoot, FELUCCA_QUAD; docs/QUAD.md).
 * Our own code: no Elektron code; the routings, the B LEV law, the HARM series, the ranges are from the Digitone
 * manual (OS 1.44, §11.3-11.12, §9.5, Appendix A, C), the curves where it gives none (DTUN, FDBK, key scaling, the
 * HARM partials) are ours, documented in docs/QUAD.md.
 *
 * Per voice: four operators C, A, B1, B2 in eight fixed routings (QUAD_ALGO below, the manual's Appendix A.3
 * diagram: the modulator -> target pairs, the feedback operator, the X and Y outputs, each output carrier direct = at
 * full level, or enveloped = at its operator envelope x level). A modulator's output into its target = its wave x
 * its operator envelope x LEV (+ velocity, key scaling): the modulation index; an operator that is both a modulator
 * and a direct carrier (B1 in algorithm 1) has its modulation path scaled, its audio path not. The feedback operator
 * modulates itself by the average of its last two outputs (the DX7 family's; FDBK 0..120, saw-like at 35). HARM
 * -26..+26: the 26-wave additive series (quad_tables.h), interpolated between neighbours; - shapes C, + shapes A and
 * B1 (B2 never). DTUN detunes A up and B2 down. The outputs mixed X .. Y (MIX -64..63), a DC blocker (QUAD_DC_K), the
 * base-width filter (a one-pole high-pass at BASE, a one-pole low-pass at BASE + WIDTH), the multimode filter (OFF,
 * LP12, HP12, LP24: dsp.c's trapezoidal SVF, LP24 two in series) with its own ADSR envelope (depth, delay, key track),
 * then QUAD's own amp envelope (engine_t.ownenv / done, as FM6 and the VA) and LEVEL.
 * Operator envelopes A (operator A's level) and B (B1's and B2's, through the B LEV law): ATK to 1, DEC to END, held
 * (attack-decay-end), each with a DELAY, a TRIG mode, a RESET; LEV A, LEV B (the law), velocity, key scaling (A, B1,
 * B2) scale them. Three LFOs per voice (FREE / HOLD run on the part's phase; TRIG / ONE / HALF on the voice's) at
 * control rate (every CTL = 32 samples), tempo-synced (MULT's BPM set) or at 120 BPM (its fixed set), summed into
 * their destinations (QD_*). Operator levels and the amplitude move per sample as linear ramps over the tick; the
 * phase increments, the filter's coefficients, HARM, FDBK and MIX are per tick.
 *
 * The patch is the sound: QP_NP signed bytes per part (quad_patch), the deep pages edit it (eng_deep_t get / set from
 * the main loop), a factory preset (blob_preset), a user slot (the blob) or the init patch load it. The eight P_E0..P_E7
 * are macros into the patch (the SYN 1 page: ALGO RATIO C RATIO A RATIO B | HARM DTUNE FDBK MIX, QUAD_MAC: RATIO B's
 * macro is B2's step, the fast hand; the deep column is the pair B1 x QUAD_NRCB + B2, 0..360); quad_block picks a macro
 * change up, a deep set writes the macro back, as the VA. AMP's PAN and DRIVE are the part's P_PAN and P_DIST (not in
 * the patch: the deep page reads and writes the track parameters).
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
#define QUAD_VER 3u              /* 3: the Digitone review (2026-10-08): B2 KEY, PHRT an enum, the new ranges; 2 and 1
                                  * are read (quad_unpack converts) */
#define QUAD_NP_V1 71u           /* version 1's values (no QP_RB2) */
#define QUAD_NP_V2 72u           /* version 2's values (no QP_B2KEY) */

/* --------------------------------------------------------- the patch --- */
enum { QL_SPEED, QL_MULT, QL_FADE, QL_DEST, QL_WAVE, QL_PHASE, QL_TRIG, QL_DEPTH, QL_N };   /* per LFO */
enum {
    QP_ALGO, QP_RC, QP_RA, QP_RB1, QP_HARM, QP_DTUN, QP_FDBK, QP_MIX,      /* 0..7: SYN 1 (QUAD_MAC: the macros) */
    QP_OFSC, QP_OFSA, QP_OFSB1, QP_OFSB2,                                  /* SYN 2 */
    QP_AATK, QP_ADEC, QP_AEND, QP_ALEV,                                    /* ENV A */
    QP_BATK, QP_BDEC, QP_BEND, QP_BLEV,                                    /* ENV B */
    QP_ADLY, QP_ATRIG, QP_ARST, QP_PHRT,                                   /* ENV 2 */
    QP_BDLY, QP_BTRIG, QP_BRST, QP_VEL,                                    /* ENV 2+ */
    QP_AKEY, QP_B1KEY,                                                     /* ENV 3 (+ QP_B2KEY) */
    QP_FATK, QP_FDEC, QP_FSUS, QP_FREL,                                    /* FILTER */
    QP_FREQ, QP_RESO, QP_FTYPE, QP_FDEPTH,                                 /* FILTER+ */
    QP_FDLY, QP_FKTRK, QP_BASE, QP_WIDTH,                                  /* FILT 2, FILT 2+ */
    QP_EATK, QP_EDEC, QP_ESUS, QP_EREL, QP_LEVEL,                          /* AMP, AMP+ */
    QP_LFO0,
    QP_RB2 = QP_LFO0 + 3 * QL_N,                                           /* B2's step (version 2) */
    QP_B2KEY,                                                              /* B2's key scaling (version 3) */
    QP_NP
};
#define QP_LFO(k, f) (QP_LFO0 + (k) * QL_N + (f))
_Static_assert(QP_NP == 73 && QP_RB2 == QUAD_NP_V1 && QP_B2KEY == QUAD_NP_V2 && QP_NP + 2u + 2u <= QUAD_BLOB &&
               QUAD_BLOB <= ENG_BLOB_MAX, "QUAD patch layout");
/* the macros P_E0..P_E7: these patch values (RATIO B's is B2's step, the fast hand) */
static const uint8_t QUAD_MAC[8] = {QP_ALGO, QP_RC, QP_RA, QP_RB2, QP_HARM, QP_DTUN, QP_FDBK, QP_MIX};

enum { QF_OFF, QF_LP12, QF_HP12, QF_LP24, QF_N };                          /* FTYPE */
enum { QW_TRI, QW_SINE, QW_SQR, QW_SAW, QW_RAMP, QW_EXP, QW_RAND, QW_N };   /* LFO WAVE */
enum { QT_FREE, QT_TRIG, QT_HOLD, QT_ONE, QT_HALF, QT_N };                 /* LFO TRIG */
enum { QR_OFF, QR_ALL, QR_C, QR_AB, QR_AB2, QR_N };                        /* PHRT: the operators reset at a note */
static const uint8_t QUAD_PHRT_MASK[QR_N] = {0, 0xF, 0x1, 0xE, 0xA};       /* (bit 0 C, 1 A, 2 B1, 3 B2) */
enum {                                                                     /* LFO DEST (the Digitone's App. C) */
    QD_NONE, QD_PITCH, QD_PAB2, QD_ALGO, QD_RC, QD_RA, QD_RB, QD_OFSC, QD_OFSA, QD_OFSB1, QD_OFSB2, QD_HARM, QD_DTUN,
    QD_FDBK, QD_MIX, QD_ALEV, QD_BLEV, QD_AATK, QD_ADEC, QD_AEND, QD_BATK, QD_BDEC, QD_BEND, QD_ADLY, QD_BDLY,
    QD_FREQ, QD_RESO, QD_FENV, QD_BASE, QD_WIDTH, QD_FATK, QD_FDEC, QD_FSUS, QD_FREL, QD_EATK, QD_EDEC, QD_ESUS,
    QD_EREL, QD_LEVEL, QD_PAN, QD_N
};
#define QUAD_NMULT 24u           /* 0..11 x1 .. x2k at the part's BPM, 12..23 the same at 120 BPM */
static const char *const N_QUAD_FTYPE[QF_N] = {"OFF", "LP12", "HP12", "LP24"};
static const char *const N_QUAD_WAVE[QW_N] = {"TRI", "SINE", "SQR", "SAW", "RAMP", "EXP", "RAND"};
static const char *const N_QUAD_TRIG[QT_N] = {"FREE", "TRIG", "HOLD", "ONE", "HALF"};
static const char *const N_QUAD_PHRT[QR_N] = {"OFF", "ALL", "C", "A+B", "A+B2"};
static const char *const N_QUAD_DEST[QD_N] = {   /* <= 5 characters (params.c prints 5) */
    "NONE", "PITCH", "P AB2", "ALGO", "RAT C", "RAT A", "RAT B", "OFS C", "OFS A", "OFSB1", "OFSB2", "HARM", "DTUN",
    "FDBK", "MIX", "A LEV", "B LEV", "A ATK", "A DEC", "A END", "B ATK", "B DEC", "B END", "A DLY", "B DLY", "FREQ",
    "RESO", "FENV", "BASE", "WIDTH", "F ATK", "F DEC", "F SUS", "F REL", "AMP A", "AMP D", "AMP S", "AMP R", "LEVEL",
    "PAN"};
static const char *const N_QUAD_MULT[QUAD_NMULT] = {"1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1k",
    "2k", "F1", "F2", "F4", "F8", "F16", "F32", "F64", "F128", "F256", "F512", "F1k", "F2k"};

/* the routings (docs/QUAD.md, the manual's Appendix A.3): per algorithm the modulators of C, A, B1, B2 (bit 0 C, 1 A,
 * 2 B1, 3 B2), the feedback operator, the X and Y outputs (bit sets) and which of those carriers are enveloped (at
 * their envelope x level; the others direct). The renders are written out per algorithm (quad_render's switch); this
 * table is what the test checks them against and what the editor may draw from */
enum { QO_C, QO_A, QO_B1, QO_B2 };
typedef struct { uint8_t mod[4], fb, x, y, env; } quad_algo_t;
static const quad_algo_t QUAD_ALGO[8] = {
    {{0x6, 0, 0x8, 0}, QO_A, 0x1, 0x4, 0},          /* 1: A(fb)>C, B2>B1, B1>C; X C, Y B1 */
    {{0x2, 0, 0x8, 0}, QO_B2, 0x1, 0x4, 0},         /* 2: A>C, B2(fb)>B1; X C, Y B1 */
    {{0x2, 0, 0x2, 0x2}, QO_A, 0x9, 0x4, 0},        /* 3: A(fb)>C, A>B2, A>B1; X C + B2, Y B1 */
    {{0x2, 0x4, 0x8, 0}, QO_B2, 0x1, 0x4, 0},       /* 4: B2(fb)>B1>A>C; X C, Y B1 */
    {{0x2, 0xC, 0, 0}, QO_B1, 0x1, 0x2, 0},         /* 5: B1(fb)>A, B2>A, A>C; X C, Y A */
    {{0xA, 0, 0xA, 0}, QO_A, 0x1, 0x4, 0},          /* 6: A(fb)>C, A>B1, B2>C, B2>B1; X C, Y B1 */
    {{0x2, 0, 0x8, 0}, QO_A, 0x3, 0xC, 0xE},        /* 7: A(fb)>C, B2>B1; X C + A (env), Y B1 + B2 (env) */
    {{0x2, 0, 0, 0}, QO_B1, 0x9, 0x4, 0xC},         /* 8: A>C, B1(fb); X C + B2 (env), Y B1 (env) */
};

/* the deep pages' columns (the UI contract, docs/QUAD.md "Implementation") */
#define QC_ALGO {"ALGO", F_INT, 1, 8, 1, 0, 0}
#define QC_RC {"RATIO C", F_INT, 0, QUAD_NRCB - 1, 3, N_QUAD_RCB, 0}
#define QC_RA {"RATIO A", F_INT, 0, QUAD_NRA - 1, 3, N_QUAD_RA, 0}
/* the pair (deep: one per detent): QUAD_NRCB names over QUAD_NRCB^2 values = a ratio pair to the editor (cr_edit.c
 * ce_pair: names[v % 19] = B2 (the fast hand, drawn first / on top), names[v / 19] = B1) */
#define QC_RB {"RATIO B", F_INT, 0, QUAD_NRB - 1, QUAD_RB_DEF, N_QUAD_RCB, 0}
#define QC_RB2 {"RATIO B", F_INT, 0, QUAD_NRCB - 1, 3, N_QUAD_RCB, 0}             /* the macro P_E3: B2's step */
#define QC_HARM {"HARM", F_OFS, -26, 26, 0, 0, 0}
#define QC_DTUN {"DTUNE", F_INT, 0, 127, 0, 0, 0}
#define QC_FDBK {"FDBK", F_INT, 0, 120, 0, 0, 0}
#define QC_MIX {"MIX", F_OFS, -64, 63, 0, 0, 0}
#define QC_OFS(l) {l, F_INT, -100, 100, 0, N_QUAD_OFS, 0}
#define QC_T(l, d) {l, F_TIME, 0, 127, d, 0, 0}
#define QC_P(l, d) {l, F_INT, 0, 127, d, 0, 0}
#define QC_PCT(l, d) {l, F_PCT, 0, 127, d, 0, 0}
#define QC_ON(l, d) {l, F_ONOFF, 0, 1, d, 0, 0}
#define QC_PHRT {"PHRT", F_ENUM, 0, QR_N - 1, QR_ALL, N_QUAD_PHRT, 0}
#define QC_FREQ {"FREQ", F_CUTOFF, 0, 127, 127, 0, 0}
#define QC_FTYPE {"TYPE", F_ENUM, 0, QF_N - 1, QF_LP12, N_QUAD_FTYPE, 0}
#define QC_DEPTH(l) {l, F_OFS, -64, 63, 0, 0, 0}
#define QC_PAN {"PAN", F_OFS, -64, 63, 0, 0, 0}
#define QC_SPEED {"SPEED", F_OFS, -64, 63, 16, 0, 0}
#define QC_MULT {"MULT", F_ENUM, 0, QUAD_NMULT - 1, 3, N_QUAD_MULT, 0}
#define QC_FADE {"FADE", F_OFS, -64, 63, 0, 0, 0}
#define QC_DEST {"DEST", F_ENUM, 0, QD_N - 1, QD_NONE, N_QUAD_DEST, 0}
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
    {"ENV 2", {QC_T("A DLY", 0), QC_ON("A TRIG", 1), QC_ON("A RESET", 1), QC_PHRT}},
    {"ENV 2+", {QC_T("B DLY", 0), QC_ON("B TRIG", 1), QC_ON("B RESET", 1), QC_PCT("VEL", 64)}},
    {"ENV 3", {QC_PCT("A KEY", 0), QC_PCT("B1 KEY", 0), QC_PCT("B2 KEY", 0), QC_NONE}},
    {"AMP", {QC_T("ATK", 0), QC_T("DEC", 64), QC_PCT("SUS", 127), QC_T("REL", 40)}},
    {"AMP+", {QC_P("LEVEL", 100), QC_PAN, QC_PCT("DRIVE", 0), QC_NONE}},
    {"LFO 1", {QC_SPEED, QC_MULT, QC_FADE, QC_DEST}},                       /* 14 LFO */
    {"LFO 1+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    {"LFO 2", {QC_SPEED, QC_MULT, QC_FADE, QC_DEST}},
    {"LFO 2+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    {"LFO 3", {QC_SPEED, QC_MULT, QC_FADE, QC_DEST}},
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
    {QP_ALGO, QP_RC, QP_RA, QP_RB1},             /* (RATIO B: the pair, QP_RB1 and QP_RB2: quad_get / quad_set) */
    {QP_HARM, QP_DTUN, QP_FDBK, QP_MIX},
    {QP_OFSC, QP_OFSA, QP_OFSB1, QP_OFSB2},
    {QP_FATK, QP_FDEC, QP_FSUS, QP_FREL},
    {QP_FREQ, QP_RESO, QP_FTYPE, QP_FDEPTH},
    {QP_FDLY, QP_FKTRK, QUAD_X, QUAD_X},
    {QP_BASE, QP_WIDTH, QUAD_X, QUAD_X},
    {QP_AATK, QP_ADEC, QP_AEND, QP_ALEV},
    {QP_BATK, QP_BDEC, QP_BEND, QP_BLEV},
    {QP_ADLY, QP_ATRIG, QP_ARST, QP_PHRT},
    {QP_BDLY, QP_BTRIG, QP_BRST, QP_VEL},
    {QP_AKEY, QP_B1KEY, QP_B2KEY, QUAD_X},
    {QP_EATK, QP_EDEC, QP_ESUS, QP_EREL},
    {QP_LEVEL, QUAD_XPAN, QUAD_XDIST, QUAD_X},
    QUAD_LPG(0), QUAD_LPG(1), QUAD_LPG(2),
};

/* the range and the init value of patch value i: the page column that shows it (every value is on one page; B1 and
 * B2 share RATIO B's: a C/B step each) */
typedef struct { int8_t min, max, def; } quad_rng_t;
static quad_rng_t quad_range(uint32_t i)
{
    quad_rng_t r = {0, 0, 0};
    uint32_t pg, c;
    if (i == QP_RB1 || i == QP_RB2) {
        r.max = (int8_t)(QUAD_NRCB - 1u);
        r.def = 3;                               /* 1.00 */
        return r;
    }
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

/* version 1 / 2's ranges where they differ from version 3's (their blobs store value - min) */
static quad_rng_t quad_range_v2(uint32_t i)
{
    quad_rng_t r = quad_range(i);
    if (i == QP_FDBK)
        r.max = 127;
    else if (i == QP_MIX)
        r.min = -63;
    else if (i == QP_FTYPE)
        r.max = 2;                               /* LP HP BP */
    else if (i == QP_PHRT)
        r.max = 1;                               /* on / off */
    else if (i >= QP_LFO0 && i < QP_RB2) {
        uint32_t f = (i - QP_LFO0) % QL_N;
        if (f == QL_SPEED)
            r.max = 64;
        else if (f == QL_MULT)
            r.max = 11;
        else if (f == QL_FADE)
            r.min = 0, r.max = 127;              /* a fade-in time (F_TIME) */
        else if (f == QL_DEST)
            r.max = 12;
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
    int16_t lv[3];                               /* the ramps' ends: operator A's, B1's, B2's level, Q15 */
    int32_t amp;                                 /* .. the amplitude, Q15 */
    int32_t f1, f2, f3, f4;                      /* the SVF (LP24: f3 f4 its first stage) */
    int32_t bh, bl;                              /* the base-width one-poles, Q8 */
    int32_t dc;                                  /* the DC blocker's one-pole (the operators' mean), Q12 */
    uint32_t lph[3], lrnd[3], ltr[3];            /* the LFOs' own phases (TRIG ONE HALF HOLD), random, travel */
    int32_t lsl[3];                              /* the LFOs' RAND, slewed (SPH on RAND), Q15 */
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

/* the macro slot of patch value i, or -1 */
static int32_t quad_mac_of(uint32_t i)
{
    int32_t k;
    for (k = 0; k < 8; k++)
        if (QUAD_MAC[k] == i)
            return k;
    return -1;
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

/* a valid blob: version 3, 2 (QUAD_NP_V2 values) or 1 (QUAD_NP_V1 values, RATIO B 0..QUAD_NRB_V1 - 1) */
static int quad_blob_ok(const uint8_t *b)
{
    uint32_t i, np, ver;
    if (!b || b[0] != QUAD_MAGIC || b[1] < 1u || b[1] > QUAD_VER)
        return 0;
    ver = b[1];
    np = ver == 1u ? QUAD_NP_V1 : ver == 2u ? QUAD_NP_V2 : QP_NP;
    for (i = 0; i < np; i++) {
        quad_rng_t r = ver == QUAD_VER ? quad_range(i) : quad_range_v2(i);
        if (b[2 + i] > (ver == 1u && i == QP_RB1 ? QUAD_NRB_V1 - 1u : (uint32_t)(r.max - r.min)))
            return 0;
    }
    for (i = 2 + np; i < QUAD_BLOB; i++)
        if (b[i])
            return 0;
    return 1;
}

/* version 1's RATIO B (BR index x 19 + B1 index, B2 = B1 x BR) as B1, B2 and OFS B2: B2 the nearest step to B1 x BR
 * (the lower one when halfway), the rest in OFS B2 (1/100, clamped at +-1.00: exact for the halves, 1.5 = 1 + 0.50) */
static void quad_rb_v1(int32_t v, int8_t *p)
{
    int32_t b1 = v % (int32_t)QUAD_NRCB, br = v / (int32_t)QUAD_NRCB, k, b2 = 0, d, o;
    int32_t want = (int32_t)(((int64_t)QUAD_RCB_Q16[b1] * QUAD_BR_Q16[br]) >> 16);
    for (k = 1; k < (int32_t)QUAD_NRCB; k++) {
        int32_t e = QUAD_RCB_Q16[k] - want, e0 = QUAD_RCB_Q16[b2] - want;
        if ((e < 0 ? -e : e) < (e0 < 0 ? -e0 : e0))
            b2 = k;
    }
    d = want - QUAD_RCB_Q16[b2];                 /* Q16, the rounding below: to 1/100 */
    o = p[QP_OFSB2] + (d >= 0 ? (d * 100 + 32768) >> 16 : -((-d * 100 + 32768) >> 16));
    p[QP_RB1] = (int8_t)b1;
    p[QP_RB2] = (int8_t)b2;
    p[QP_OFSB2] = (int8_t)clamp(o, -100, 100);
}

/* FDBK (0..120) -> the feedback multiplier: the self-modulation is (y[n-1] + y[n-2]) x fbq << 2 (2^32 = a cycle), i.e.
 * beta = 2 pi fbq / 16384 rad on the average of the last two outputs: 1.9 rad at 35 (a saw: h2 / h1 0.47, h3 / h1 0.29),
 * 7.5 rad at 120 (noise) */
static int32_t quad_fbq(int32_t f) { return (f * 4237 + ((f * f * 537) >> 6)) >> 5; }

/* version 2's HARM (+-26 = +-7 table positions: + the odd series, - all harmonics, on the carriers) as version 3's
 * (the 26-wave series; - shapes C): - h -> the saw build-up (steps 1..7), + h -> the square build-up (15..19), both
 * on C (v2's + side shaped the carriers too, v3's + shapes A and B1) */
static int32_t quad_harm_v2(int32_t h)
{
    int32_t a = h < 0 ? -h : h, q = (a * 7 + 13) / 26;   /* the old table position, rounded: 0..7 */
    if (!q)
        return 0;
    return h < 0 ? -q : -(14 + clamp((q * 5 + 3) / 7, 1, 5));
}

/* a version 1 / 2 patch (their meanings, in place) -> version 3: FDBK (the same beta), FTYPE (BP -> LP12), HARM, PHRT
 * (on -> ALL), B2 KEY = B KEY, the LFOs (SPEED <= 63, FADE: a fade-in of the same time, DEST by name) */
static void quad_v2_to_v3(int8_t *p)
{
    static const uint8_t DEST2[13] = {QD_NONE, QD_HARM, QD_DTUN, QD_FDBK, QD_MIX, QD_RA, QD_RB, QD_FREQ, QD_RESO,
                                      QD_LEVEL, QD_PAN, QD_ALEV, QD_BLEV};
    static const uint8_t FT2[3] = {QF_LP12, QF_HP12, QF_LP12};
    int32_t k, f, best = 0, want = p[QP_FDBK] * p[QP_FDBK];   /* v2: (fb1 + fb2) x 2 FDBK^2 << 1 */
    for (f = 0; f <= 120; f++) {
        int32_t d = quad_fbq(f) - want, e = quad_fbq(best) - want;
        if ((d < 0 ? -d : d) < (e < 0 ? -e : e))
            best = f;
    }
    p[QP_FDBK] = (int8_t)best;
    p[QP_FTYPE] = (int8_t)FT2[clamp(p[QP_FTYPE], 0, 2)];
    p[QP_HARM] = (int8_t)quad_harm_v2(p[QP_HARM]);
    p[QP_PHRT] = (int8_t)(p[QP_PHRT] ? QR_ALL : QR_OFF);
    p[QP_B2KEY] = p[QP_B1KEY];
    for (k = 0; k < 3; k++) {
        p[QP_LFO(k, QL_SPEED)] = (int8_t)clamp(p[QP_LFO(k, QL_SPEED)], -64, 63);
        p[QP_LFO(k, QL_FADE)] = (int8_t)-(((uint8_t)p[QP_LFO(k, QL_FADE)] + 1) >> 1);
        p[QP_LFO(k, QL_DEST)] = (int8_t)DEST2[clamp(p[QP_LFO(k, QL_DEST)], 0, 12)];
    }
}

/* blob -> patch; 0 or a bad blob: the init patch. 1 = the blob was taken */
static int quad_unpack(const uint8_t *b, int8_t *p)
{
    uint32_t i, np;
    quad_init_patch(p);
    if (!quad_blob_ok(b))
        return 0;
    if (b[1] == QUAD_VER) {
        for (i = 0; i < QP_NP; i++)
            p[i] = (int8_t)(b[2 + i] + quad_range(i).min);
        return 1;
    }
    np = b[1] == 1u ? QUAD_NP_V1 : QUAD_NP_V2;   /* version 1 / 2: their values, then converted */
    for (i = 0; i < np; i++)
        if (b[1] != 1u || i != QP_RB1)
            p[i] = (int8_t)(b[2 + i] + quad_range_v2(i).min);
    if (b[1] == 1u)
        quad_rb_v1(b[2 + QP_RB1], p);
    quad_v2_to_v3(p);
    return 1;
}

/* the macros (P_E0..P_E7 = QUAD_MAC's values) from the patch: into the track, remembered (main loop) */
static void quad_macros_out(track_t *t)
{
    uint32_t tr = quad_tr(t), k;
    if (tr >= QUAD_NPART)
        return;
    for (k = 0; k < 8u; k++) {
        int16_t v = quad_patch[tr][QUAD_MAC[k]];
        t->p[P_E0 + k] = v;
        quad_mlast[tr][k] = v;
    }
}

/* ------------------------------------------------------------ presets --- */
/* a preset: the init patch with these (index, value) pairs, 0xFF ends. The ratio indices: RC / RA / RB1 / RB2
 * (quad_tables.h; QRC(r): C/B's step of ratio r (x4: 1 = 0.25 .. 4 = 1.00, then 8 = 2.00 ..), QRA(r): A's (x4),
 * QRB(b1, b2): RATIO B's pair index (B1 and B2 x4: QRB(4, 8) = 1.00 / 2.00 = B1 1.00, B2 2.00).
 * Retuned 2026-10-08 for the Digitone routings (docs/QUAD.md "Presets"): MIX -64 = X alone, 63 = Y alone */
#define S8(v) (uint8_t)(int8_t)(v)
#define QRC(q) ((q) <= 4 ? (q) - 1 : (q) / 4 + 2)
#define QRA(q) ((q) - 1)
#define QRB(b1, b2) (QRC(b1) * QUAD_NRCB + QRC(b2))
#define SYN(al, rc, ra, b1, b2, h, dt, fb, mx) QP_ALGO, al, QP_RC, rc, QP_RA, ra, QP_RB1, QRC(b1), QP_RB2, QRC(b2), \
                                               QP_HARM, S8(h), QP_DTUN, dt, QP_FDBK, fb, QP_MIX, S8(mx)
#define ENVA(a, d, e, l) QP_AATK, a, QP_ADEC, d, QP_AEND, e, QP_ALEV, l
#define ENVB(a, d, e, l) QP_BATK, a, QP_BDEC, d, QP_BEND, e, QP_BLEV, l
#define KEY(a, b1, b2) QP_AKEY, a, QP_B1KEY, b1, QP_B2KEY, b2
#define AMP(a, d, s, r, l) QP_EATK, a, QP_EDEC, d, QP_ESUS, s, QP_EREL, r, QP_LEVEL, l
#define FLT(ty, f, r, dp) QP_FTYPE, QF_##ty, QP_FREQ, f, QP_RESO, r, QP_FDEPTH, S8(dp)
#define FENV(a, d, s, r) QP_FATK, a, QP_FDEC, d, QP_FSUS, s, QP_FREL, r
#define LFO(k, sp, mu, de, w, dp) QP_LFO(k, QL_SPEED), S8(sp), QP_LFO(k, QL_MULT), mu, QP_LFO(k, QL_DEST), QD_##de, \
                                  QP_LFO(k, QL_WAVE), QW_##w, QP_LFO(k, QL_DEPTH), S8(dp)
/* EP: X = C 1:1 under A (the body), Y = B1 direct 1.00 under B2 14.00 (the tine, short) */
static const uint8_t QUADP_EP[] = {SYN(2, QRC(4), QRA(4), 4, 56, 0, 12, 0, -30), ENVA(0, 78, 20, 66),
    ENVB(0, 52, 0, 62), QP_VEL, 100, KEY(40, 30, 30), AMP(0, 96, 40, 62, 72), 0xFF};
/* BELL: X = C under A 3.50, Y = B1 2.00 under B2 6.00 (two inharmonic pairs) */
static const uint8_t QUADP_BELL[] = {SYN(2, QRC(4), QRA(14), 8, 24, 0, 20, 0, -10), ENVA(0, 92, 30, 80),
    ENVB(0, 96, 20, 70), QP_VEL, 90, KEY(30, 0, 30), AMP(0, 106, 0, 100, 80), 0xFF};
/* BASS: C under a 1:1 A with feedback (a saw-ish modulator), X only */
static const uint8_t QUADP_BASS[] = {SYN(1, QRC(4), QRA(4), 4, 4, 0, 0, 24, -64), ENVA(0, 62, 34, 88),
    ENVB(0, 50, 0, 0), FLT(LP12, 92, 10, 20), FENV(0, 60, 0, 40), AMP(0, 80, 100, 30, 110), 0xFF};
/* PLUCK: B1 and B2 into A 2.00 into C, X only, everything decaying */
static const uint8_t QUADP_PLUCK[] = {SYN(5, QRC(4), QRA(8), 12, 12, 0, 6, 0, -64), ENVA(0, 56, 0, 84),
    ENVB(0, 42, 0, 64), KEY(40, 40, 40), AMP(0, 76, 0, 60, 72), 0xFF};
/* BRASS: C under a slow-attack 1:1 A with feedback, a little B1 */
static const uint8_t QUADP_BRASS[] = {SYN(1, QRC(4), QRA(4), 4, 4, 0, 0, 28, -64), ENVA(50, 80, 60, 74),
    ENVB(40, 80, 50, 10), FLT(LP12, 80, 12, 30), FENV(50, 86, 60, 60), AMP(40, 80, 110, 60, 76), 0xFF};
/* GLASS PAD: X = C under A 2.00 + A, Y = B1 under B2 + B2 (algorithm 7's enveloped carriers), A / B1 a little saw */
static const uint8_t QUADP_GLASS[] = {SYN(7, QRC(4), QRA(8), 16, 16, 8, 40, 0, 0), ENVA(70, 90, 100, 36),
    ENVB(80, 96, 60, 50), QP_PHRT, QR_OFF, AMP(70, 90, 120, 100, 88), LFO(0, 8, 3, HARM, SINE, 30), 0xFF};
/* HOLLOW: C a soft square (HARM -16) under A 2.00, Y = B1 under a little B2 */
static const uint8_t QUADP_HOLLOW[] = {SYN(2, QRC(4), QRA(8), 4, 4, -16, 0, 0, -20), ENVA(20, 90, 80, 40),
    ENVB(0, 80, 40, 50), AMP(30, 80, 110, 70, 76), 0xFF};
/* SQUARE LEAD: C a square (HARM -19) under a little A 2.00, X only */
static const uint8_t QUADP_SQLEAD[] = {SYN(1, QRC(4), QRA(8), 4, 4, -19, 0, 10, -64), ENVA(0, 80, 60, 36),
    ENVB(0, 70, 20, 0), AMP(5, 70, 110, 50, 110), 0xFF};
/* METAL: B1 7.00 (feedback) and B2 10.50 into A 1.50 into C, A detuned against B2; mostly X */
static const uint8_t QUADP_METAL[] = {SYN(5, QRC(4), QRA(6), 28, 40, 0, 90, 20, -40), QP_OFSB2, 50,
    ENVA(0, 96, 40, 90), ENVB(0, 90, 30, 70), AMP(0, 100, 30, 90, 70), 0xFF};
/* WOBBLE: BASS's voice through LP24, an LFO on A LEV and one on FREQ */
static const uint8_t QUADP_WOBBLE[] = {SYN(1, QRC(4), QRA(4), 4, 4, 0, 0, 10, -64), ENVA(0, 64, 127, 60),
    ENVB(0, 60, 40, 0), FLT(LP24, 70, 60, 0), AMP(0, 80, 110, 40, 115), LFO(0, 32, 4, ALEV, TRI, 50),
    LFO(1, 32, 4, FREQ, TRI, 40), QP_LFO(0, QL_TRIG), QT_TRIG, QP_LFO(1, QL_TRIG), QT_TRIG, 0xFF};
/* CLAV: X = C (HARM -15: 1 + 3) under A 3.00, Y = B1 1.00 under B2 4.00, through HP12 */
static const uint8_t QUADP_CLAV[] = {SYN(2, QRC(4), QRA(12), 4, 16, -15, 0, 0, 0), ENVA(0, 50, 10, 90),
    ENVB(0, 46, 0, 80), FLT(HP12, 30, 20, 0), AMP(0, 70, 0, 45, 90), 0xFF};
/* STRINGS: X = C a saw (HARM -7) + B2 detuned, Y = B1 with feedback 35 (a second saw), both enveloped at B LEV 127;
 * DTUN by an LFO */
static const uint8_t QUADP_STRINGS[] = {SYN(8, QRC(4), QRA(4), 4, 4, -7, 100, 35, 0), ENVA(0, 60, 0, 0),
    ENVB(0, 60, 127, 127), QP_PHRT, QR_OFF, FLT(LP12, 96, 8, 0), AMP(70, 90, 115, 96, 80),
    LFO(0, 6, 3, DTUN, SINE, 20), 0xFF};
/* MARIMBA: C under A 4.00 and B1 10.00 (short), X only */
static const uint8_t QUADP_MARIMBA[] = {SYN(1, QRC(4), QRA(16), 40, 40, 0, 0, 0, -64), ENVA(0, 50, 0, 64),
    ENVB(0, 30, 0, 20), KEY(50, 50, 0), AMP(0, 80, 0, 70, 78), 0xFF};
/* DRONE: C 0.50 and B1 under A (feedback) and B2 1.50, three slow LFOs (HARM, MIX, FDBK) */
static const uint8_t QUADP_DRONE[] = {SYN(6, QRC(2), QRA(4), 4, 4, 0, 50, 15, 0), QP_OFSB2, 50, ENVA(100, 100, 90, 44),
    ENVB(110, 100, 80, 52), QP_PHRT, QR_OFF, AMP(64, 100, 127, 110, 74), LFO(0, 4, 3, HARM, SINE, 40),
    LFO(1, 5, 3, MIX, TRI, 50), LFO(2, 3, 3, FDBK, SINE, 30), 0xFF};
/* FEEDBACK: Y = B1 with heavy feedback (enveloped: B LEV 43 = B1 full, B2 off), X = C under A */
static const uint8_t QUADP_FEEDBACK[] = {SYN(8, QRC(4), QRA(4), 4, 4, 0, 0, 42, 30), ENVA(0, 80, 40, 60),
    ENVB(0, 80, 100, 43), AMP(0, 90, 100, 70, 72), 0xFF};
/* NOISE-ISH: algorithm 7's A at full feedback, X = C under it + itself, through a resonant HP12 */
static const uint8_t QUADP_NOISE[] = {SYN(7, QRC(4), QRA(4), 4, 4, 0, 0, 120, -64), ENVA(0, 60, 127, 100),
    ENVB(0, 60, 0, 0), FLT(HP12, 70, 40, 0), AMP(0, 90, 60, 80, 62), 0xFF};

/* {ALGO, RATIO C, RATIO A, RATIO B (B2's step), HARM, DTUNE, FDBK, MIX}: the macros as the patch has them
 * (cr_quad_test checks); env: the amp envelope (QUAD's own: the platform ADSR is not used) */
static const preset_t QUAD_PRESETS[] = {
    {"EP", {2, QRC(4), QRA(4), QRC(56), 0, 12, 0, -30}, {0, 96, 40, 62}, 0, 0, FX(0, 30, 20, 40), PAT(6)},
    {"BELL", {2, QRC(4), QRA(14), QRC(24), 0, 20, 0, -10}, {0, 106, 0, 100}, 0, 0, FX(0, 20, 35, 60), PAT(7)},
    {"BASS", {1, QRC(4), QRA(4), QRC(4), 0, 0, 24, -64}, {0, 80, 100, 30}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"PLUCK", {5, QRC(4), QRA(8), QRC(12), 0, 6, 0, -64}, {0, 76, 0, 60}, 0, 0, FX(0, 20, 30, 40), PAT(6)},
    {"BRASS", {1, QRC(4), QRA(4), QRC(4), 0, 0, 28, -64}, {40, 80, 110, 60}, 0, 0, FX(0, 25, 20, 40), PAT(6)},
    {"GLASS PAD", {7, QRC(4), QRA(8), QRC(16), 8, 40, 0, 0}, {70, 90, 120, 100}, 0, 0, FX(0, 50, 25, 70), PAT(5)},
    {"HOLLOW", {2, QRC(4), QRA(8), QRC(4), -16, 0, 0, -20}, {30, 80, 110, 70}, 0, 0, FX(0, 35, 25, 50), PAT(6)},
    {"SQUARE LEAD", {1, QRC(4), QRA(8), QRC(4), -19, 0, 10, -64}, {5, 70, 110, 50}, 0, 1, FX(0, 25, 40, 40), PAT(4)},
    {"METAL", {5, QRC(4), QRA(6), QRC(40), 0, 90, 20, -40}, {0, 100, 30, 90}, 0, 0, FX(0, 20, 30, 50), PAT(7)},
    {"WOBBLE", {1, QRC(4), QRA(4), QRC(4), 0, 0, 10, -64}, {0, 80, 110, 40}, 0, 1, FX(10, 0, 15, 10), PAT(2)},
    {"CLAV", {2, QRC(4), QRA(12), QRC(16), -15, 0, 0, 0}, {0, 70, 0, 45}, 0, 0, FX(0, 15, 20, 25), PAT(6)},
    {"STRINGS", {8, QRC(4), QRA(4), QRC(4), -7, 100, 35, 0}, {70, 90, 115, 96}, 0, 0, FX(0, 55, 15, 65), PAT(5)},
    {"MARIMBA", {1, QRC(4), QRA(16), QRC(40), 0, 0, 0, -64}, {0, 80, 0, 70}, 0, 0, FX(0, 15, 25, 40), PAT(7)},
    {"DRONE", {6, QRC(2), QRA(4), QRC(4), 0, 50, 15, 0}, {64, 100, 127, 110}, 0, 0, FX(0, 50, 30, 80), PAT(5)},
    {"FEEDBACK", {8, QRC(4), QRA(4), QRC(4), 0, 0, 42, 30}, {0, 90, 100, 70}, 0, 0, FX(0, 25, 30, 40), PAT(4)},
    {"NOISE-ISH", {7, QRC(4), QRA(4), QRC(4), 0, 0, 120, -64}, {0, 90, 60, 80}, 0, 0, FX(0, 20, 30, 50), PAT(7)},
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
#undef KEY
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
    if (i == QP_RB1)                             /* RATIO B: the pair, B1 x QUAD_NRCB + B2 */
        return tr >= QUAD_NPART ? QUAD_RB_DEF : quad_patch[tr][QP_RB1] * (int32_t)QUAD_NRCB + quad_patch[tr][QP_RB2];
    if (tr >= QUAD_NPART)
        return quad_range(i).def;
    return quad_patch[tr][i];
}

/* patch value i of part tr was set: a macro's value goes to the track's P_E too (quad_block sees no change) */
static void quad_mac_put(track_t *t, uint32_t tr, uint32_t i)
{
    int32_t k = quad_mac_of(i);
    if (k < 0)
        return;
    t->p[P_E0 + k] = quad_patch[tr][i];
    RING_PUBLISH();
    quad_mlast[tr][k] = quad_patch[tr][i];
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
    if (i == QP_RB1) {                           /* RATIO B: the pair -> B1 and B2 (the macro: B2) */
        v = clamp(v, 0, QUAD_NRB - 1);
        quad_patch[tr][QP_RB1] = (int8_t)(v / (int32_t)QUAD_NRCB);
        quad_patch[tr][QP_RB2] = (int8_t)(v % (int32_t)QUAD_NRCB);
        quad_mac_put(t, tr, QP_RB2);
        return;
    }
    quad_patch[tr][i] = quad_clampv(i, v);
    quad_mac_put(t, tr, i);
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
    int32_t rb = -1;
    quad_user_pending = 0;
    if (tr >= QUAD_NPART || t->eng_req != ENGI_QUAD)
        return;
    if (t->p[P_E3] >= (int32_t)QUAD_NRCB && t->p[P_E3] < (int32_t)QUAD_NRB_V1)   /* a version-1 RATIO B macro (a
                                                  * project or a record from before: BR x 19 + B1, BR > 0.5) */
        rb = t->p[P_E3];
    if (pend && quad_store_read && !quad_store_read(pend - 1u, b) && quad_unpack(b, p)) {
        memcpy(quad_patch[tr], p, QP_NP);
        quad_macros_out(t);
        return;
    }
    if (!pend && rb < 0 && t->preset < QUAD_NPRESETS) {
        for (k = 0; k < 8u && t->p[P_E0 + k] == QUAD_PRESETS[t->preset].e[k]; k++)
            ;
        if (k == 8u) {
            quad_blob_preset(t, t->preset);
            return;
        }
    }
    quad_init_patch(p);
    for (k = 0; k < 8u; k++)
        if (k != 3u || rb < 0)
            p[QUAD_MAC[k]] = quad_clampv(QUAD_MAC[k], t->p[P_E0 + k]);
    if (rb >= 0)                                 /* (B1, B2 and OFS B2 as a version-1 blob's) */
        quad_rb_v1(rb, p);
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

/* 2^(x / 4096) in Q15 (x: Q12 octaves), the fraction by a quadratic (within 0.3 %); 1 << 30 at 15 octaves up */
static int32_t quad_exp2(int32_t x)
{
    int32_t ip = x >> 12, f = x & 4095, m = (4096 + ((f * (2689 + ((f * 1407) >> 12))) >> 12)) << 3;
    if (ip >= 0)
        return ip < 15 ? m << ip : 1 << 30;
    return -ip < 31 ? m >> -ip : 0;
}

/* LFO k's phase increment per control tick: f = SPEED x MULT / 128 x BPM / 240 Hz (SPEED x MULT = 128: a bar; MULT's
 * fixed set at 120 BPM); negative runs backwards, 0 stops */
static int32_t quad_lfo_inc(const int8_t *p, uint32_t k)
{
    uint32_t mu = (uint32_t)p[QP_LFO(k, QL_MULT)] % QUAD_NMULT;
    int32_t bpm = mu < 12u ? clamp(song.g[G_BPM], 20, 300) : 120;
    int64_t v = ((int64_t)p[QP_LFO(k, QL_SPEED)] * (int64_t)(1u << (mu % 12u)) * bpm * QUAD_LFO_K) >> 16;
    return v > 0x7FFFFFFF ? 0x7FFFFFFF : v < -0x7FFFFFFF ? -0x7FFFFFFF : (int32_t)v;
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
            p[QUAD_MAC[k]] = quad_clampv(QUAD_MAC[k], t->p[P_E0 + k]);
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

/* the LFOs at a note-on: TRIG / ONE / HALF start at PHASE (RAND: at 0, PHASE is its slew), HOLD takes the part's phase
 * (and keeps it) */
static void quad_lfo_on(track_t *t, quad_voice_t *s, const int8_t *p)
{
    uint32_t tr = quad_tr(t), k;
    for (k = 0; k < 3u; k++) {
        uint32_t tm = (uint32_t)p[QP_LFO(k, QL_TRIG)];
        uint32_t sp = p[QP_LFO(k, QL_WAVE)] == QW_RAND ? 0u : (uint32_t)p[QP_LFO(k, QL_PHASE)] << 25;
        s->ltr[k] = 0;
        s->lstop &= (uint8_t)~(1u << k);
        s->lrnd[k] = quad_xs(s->lrnd[k] + 0x9E3779B9u * (k + 1u) + (uint32_t)(s - quad_vs[0]));
        if (tm == QT_HOLD) {
            s->lph[k] = quad_lfo[tr].ph[k] + sp;
            s->lrnd[k] = quad_lfo[tr].rnd[k];
        } else {
            s->lph[k] = sp;
        }
    }
    s->ticks = 0;
}

static void quad_note_on(track_t *t, voice_t *v)
{
    quad_voice_t *s = quad_voice(t, v);
    const int8_t *p;
    uint32_t k, fresh, mask;
    if (!s)
        return;
    p = quad_patch[quad_tr(t)];
    fresh = (!v->env && !v->env_out) || !s->live;
    mask = QUAD_PHRT_MASK[(uint32_t)p[QP_PHRT] % QR_N];
    for (k = 0; k < 4u; k++)                     /* PHRT: these operators restart at 0 (OFF: all run on, even on a
                                                  * fresh voice) */
        if ((mask >> k) & 1u)
            s->ph[k] = 0;
    if (fresh) {                                 /* from silence: filters, envelopes, ramps from 0 */
        for (k = 0; k < 4u; k++)
            s->env[k] = 0;
        s->fb1 = s->fb2 = 0;
        s->lv[0] = s->lv[1] = s->lv[2] = 0;
        s->amp = 0;
        s->f1 = s->f2 = s->f3 = s->f4 = 0;
        s->bh = s->bl = 0;
        s->dc = 0;
        quad_env_start(s, 0, p[QP_ADLY], 1);
        quad_env_start(s, 1, p[QP_BDLY], 1);
    } else {                                     /* a retrigger: TRIG restarts the operator envelopes (RESET: from
                                                  * 0, else from where they are) */
        if (mask)
            s->fb1 = s->fb2 = 0;
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

/* the B LEV law (the manual §11.5.8 graph): v 0..127 -> B1's and B2's share, Q12: 0..43 B1 0 -> 1 (B2 0), 43..85 B1
 * 1 -> 0.1 while B2 0 -> 1, 85..127 B1 0.1 -> 1 (B2 1) */
static void quad_blev(int32_t v, int32_t *u1, int32_t *u2)
{
    if (v <= 43)
        *u1 = v * 4096 / 43, *u2 = 0;
    else if (v < 85)
        *u1 = 4096 - (v - 43) * 3686 / 42, *u2 = (v - 43) * 4096 / 42;
    else
        *u1 = 410 + (v - 85) * 3686 / 42, *u2 = 4096;
}
static int32_t quad_lev(int32_t u) { return u >= 4096 ? 32767 : (u * u) >> 9; }   /* a share (Q12) squared, Q15 */

/* the HARM wave: tables a and b (HARM's neighbours, one index), b's share f (Q15) */
static inline int32_t quad_hw(const int16_t *a, const int16_t *b, int32_t f, uint32_t ph)
{
    uint32_t i = ph >> 23;
    int32_t fr = (int32_t)((ph >> 8) & 0x7FFFu), x0 = a[i], x = x0 + (((a[i + 1u] - x0) * fr) >> 15), y0, y;
    if (!f)
        return x;
    y0 = b[i];
    y = y0 + (((b[i + 1u] - y0) * fr) >> 15);
    return x + (((y - x) * f) >> 15);
}

/* the DC blocker after the operators: a one-pole high-pass at ~8 Hz (Q16: 65536 (1 - exp(-2 pi 8 / FS))). The
 * operators are not DC-free: the DX7-style feedback (its average lags 1.5 samples) skews the feedback operator's saw,
 * and at near-unison ratios (1:1) a modulator's phase offset (that mean, or DTUNE's slow drift) puts J1(I) sin(offset)
 * into the carrier at 0 Hz. A DX7 / Digitone AC-couples its output; here every voice does, before the filters */
#define QUAD_DC_K 75

/* the operator outputs: WS the sine (B2 always), WC / WA / WB1 C's, A's, B1's wave (HARM: defined per loop), QM a
 * modulator's output as a phase offset (Q15 -> 2 cycles at full level), QFB the feedback operator's self-modulation
 * (its wave's last two values fb1, fb2: their sum x fbq << 2, 2^32 = a cycle), QLV an output at a level */
#define WS(ph) sine_i(ph)
#define QM(o) ((uint32_t)(o) << 18)
#define QFB() ((uint32_t)((fb1 + fb2) * fbq) << 2)
#define QLV(y, l) (((y) * (l)) >> 15)
/* the operator loop of one algorithm: the level ramps, BODY (sets X, Y and yf, the feedback operator's wave), the
 * feedback memory, the phases, MIX (X .. Y, gx + gy = 1) at half scale into acc */
#define QUAD_LOOP(BODY)                                                    \
    for (i = 0; i < n; i++) {                                              \
        int32_t LA, LB1, LB2, X, Y, yf, y1, ya, o;                         \
        la += dla;                                                         \
        lb1 += dlb1;                                                       \
        lb2 += dlb2;                                                       \
        LA = la >> 16;                                                     \
        LB1 = lb1 >> 16;                                                   \
        LB2 = lb2 >> 16;                                                   \
        BODY                                                               \
        (void)LA, (void)LB1, (void)LB2;                                    \
        fb2 = fb1;                                                         \
        fb1 = yf;                                                          \
        p0 += i0;                                                          \
        p1 += i1;                                                          \
        p2 += i2;                                                          \
        p3 += i3;                                                          \
        acc[i] = ((X * gx) + (Y * gy)) >> 16;                              \
    }
/* the eight routings (QUAD_ALGO; "direct" carriers at full level, "env" at their envelope x level) */
#define QUAD_ALGOS                                                                                          \
    switch (alg) {                                                                                          \
    case 0: /* A (fb) > C, B2 > B1 > C; X C, Y B1 (direct) */                                               \
        QUAD_LOOP(yf = WA(p1 + QFB()); y1 = WB1(p2 + QM(QLV(WS(p3), LB2)));                                 \
                  X = WC(p0 + QM(QLV(yf, LA) + QLV(y1, LB1))); Y = y1; (void)ya; (void)o;)                  \
        break;                                                                                              \
    case 1: /* A > C (X), B2 (fb) > B1 (Y, direct) */                                                       \
        QUAD_LOOP(X = WC(p0 + QM(QLV(WA(p1), LA))); yf = WS(p3 + QFB()); Y = WB1(p2 + QM(QLV(yf, LB2)));    \
                  (void)y1; (void)ya; (void)o;)                                                             \
        break;                                                                                              \
    case 2: /* A (fb) > C, B2, B1; X C + B2 (direct), Y B1 (direct) */                                      \
        QUAD_LOOP(yf = WA(p1 + QFB()); o = QLV(yf, LA); X = WC(p0 + QM(o)) + WS(p3 + QM(o));               \
                  Y = WB1(p2 + QM(o)); (void)y1; (void)ya;)                                                 \
        break;                                                                                              \
    case 3: /* B2 (fb) > B1 > A > C; X C, Y B1 (direct) */                                                  \
        QUAD_LOOP(yf = WS(p3 + QFB()); y1 = WB1(p2 + QM(QLV(yf, LB2))); ya = WA(p1 + QM(QLV(y1, LB1)));     \
                  X = WC(p0 + QM(QLV(ya, LA))); Y = y1; (void)o;)                                           \
        break;                                                                                              \
    case 4: /* B1 (fb), B2 > A > C; X C, Y A (direct) */                                                    \
        QUAD_LOOP(yf = WB1(p2 + QFB()); ya = WA(p1 + QM(QLV(yf, LB1) + QLV(WS(p3), LB2)));                  \
                  X = WC(p0 + QM(QLV(ya, LA))); Y = ya; (void)y1; (void)o;)                                 \
        break;                                                                                              \
    case 5: /* A (fb), B2 > C and B1; X C, Y B1 (direct) */                                                 \
        QUAD_LOOP(yf = WA(p1 + QFB()); o = QLV(yf, LA) + QLV(WS(p3), LB2); X = WC(p0 + QM(o));              \
                  Y = WB1(p2 + QM(o)); (void)y1; (void)ya;)                                                 \
        break;                                                                                              \
    case 6: /* A (fb) > C, B2 > B1; X C + A (env), Y B1 (env) + B2 (env) */                                 \
        QUAD_LOOP(yf = WA(p1 + QFB()); o = QLV(yf, LA); X = WC(p0 + QM(o)) + o; ya = QLV(WS(p3), LB2);      \
                  Y = QLV(WB1(p2 + QM(ya)), LB1) + ya; (void)y1;)                                           \
        break;                                                                                              \
    default: /* A > C, B1 (fb); X C + B2 (env), Y B1 (env) */                                               \
        QUAD_LOOP(X = WC(p0 + QM(QLV(WA(p1), LA))) + QLV(WS(p3), LB2); yf = WB1(p2 + QFB()); Y = QLV(yf, LB1); \
                  (void)y1; (void)ya; (void)o;)                                                             \
        break;                                                                                              \
    }

/* the voice's block: out gets it */
static void quad_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    quad_voice_t *s = quad_voice(t, v);
    uint32_t tr = quad_tr(t), k, i, alg, inc[4], bAll, bAB2;
    const int8_t *p;
    int32_t eA, eB, eF, eE, dst[QD_N], A0, A1, dA, a, x, y;
    int32_t hq, dt, mix, ra, rb, cut, kd, reso, gx, gy, fbq, hf, fdep, base, wid;
    int32_t la, lb1, lb2, dla, dlb1, dlb2, tl[3], hpk, lpk, svf, fty, fb1, fb2, bh, bl;
    const int16_t *ha_t = QUAD_HARM[0], *hb_t = QUAD_HARM[0];
    uint32_t p0, p1, p2, p3, i0, i1, i2, i3;
    int32_t acc[CTL];
    tsvf_t c, c2;
    if (!s || !s->live || n > CTL)
        return;
    p = quad_patch[tr];
    /* the LFOs: read, then advanced; summed into their destinations (Q15 each) */
    for (k = 0; k < QD_N; k++)
        dst[k] = 0;
    for (k = 0; k < 3u; k++) {
        uint32_t d = (uint32_t)p[QP_LFO(k, QL_DEST)], tm = (uint32_t)p[QP_LFO(k, QL_TRIG)], ph, rv;
        uint32_t wv = (uint32_t)p[QP_LFO(k, QL_WAVE)];
        int32_t inc0 = quad_lfo_inc(p, k), w, fd = p[QP_LFO(k, QL_FADE)];
        if (tm == QT_FREE) {
            ph = quad_lfo[tr].ph[k] + (wv == QW_RAND ? 0u : (uint32_t)p[QP_LFO(k, QL_PHASE)] << 25);
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
        if (wv == QW_RAND) {                     /* RAND: SPH is the slew, a one-pole on the steps (127: about a
                                                  * period) */
            int32_t r = (int32_t)(rv >> 16) - 32768, sl = p[QP_LFO(k, QL_PHASE)];
            if (sl) {
                int32_t kk = clamp((int32_t)(((uint32_t)(inc0 < 0 ? -inc0 : inc0) >> 17) * 254u / (uint32_t)sl), 1,
                                   32767);
                s->lsl[k] += ((r - s->lsl[k]) * kk) >> 15;
            } else {
                s->lsl[k] = r;
            }
        }
        if (!d || d >= QD_N || !p[QP_LFO(k, QL_DEPTH)])
            continue;
        w = quad_lfo_wave(wv, ph, s->lsl[k]);
        w = (w * p[QP_LFO(k, QL_DEPTH)]) >> 6;
        if (fd) {                                /* FADE: - in, + out, over the time of 2 |FADE| - 1 */
            int32_t f = (int32_t)s->ticks * (int32_t)(ENV_LIN[2 * (fd < 0 ? -fd : fd) - 1] >> 9);
            f = f > 32767 ? 32767 : f;
            w = (w * (fd < 0 ? f : 32767 - f)) >> 15;
        }
        dst[d] += w;
    }
    if (s->ticks < 0xFFFFu)
        s->ticks++;
#define QMV(i, d, sc, lo, hi) clamp(p[i] + ((dst[d] * (sc)) >> 15), lo, hi)
    /* the envelopes (their times, END, SUS modulated) */
    eA = quad_env_tick(s, 0, QMV(QP_AATK, QD_AATK, 127, 0, 127), QMV(QP_ADEC, QD_ADEC, 127, 0, 127),
                       QMV(QP_AEND, QD_AEND, 127, 0, 127) << 17, 0, QMV(QP_ADLY, QD_ADLY, 127, 0, 127), 1, 0);
    eB = quad_env_tick(s, 1, QMV(QP_BATK, QD_BATK, 127, 0, 127), QMV(QP_BDEC, QD_BDEC, 127, 0, 127),
                       QMV(QP_BEND, QD_BEND, 127, 0, 127) << 17, 0, QMV(QP_BDLY, QD_BDLY, 127, 0, 127), 1, 0);
    eF = quad_env_tick(s, 2, QMV(QP_FATK, QD_FATK, 127, 0, 127), QMV(QP_FDEC, QD_FDEC, 127, 0, 127),
                       QMV(QP_FSUS, QD_FSUS, 127, 0, 127) << 17, QMV(QP_FREL, QD_FREL, 127, 0, 127), p[QP_FDLY],
                       v->gate, 1);
    eE = quad_env_tick(s, 3, QMV(QP_EATK, QD_EATK, 127, 0, 127), QMV(QP_EDEC, QD_EDEC, 127, 0, 127),
                       QMV(QP_ESUS, QD_ESUS, 127, 0, 127) << 17, QMV(QP_EREL, QD_EREL, 127, 0, 127), 0, v->gate, 1);
    if (v - t->v == t->m_vi)                     /* the part's PAN: from the latest note's voice */
        quad_pan_off[tr] = (int8_t)clamp((dst[QD_PAN] * 64) >> 15, -64, 63);
    /* the operators: ratios, detune, increments (PITCH: all, P AB2: A and B2, +-1 octave) */
    alg = (uint32_t)QMV(QP_ALGO, QD_ALGO, 7, 1, 8) - 1u;
    dt = QMV(QP_DTUN, QD_DTUN, 127, 0, 127);
    ra = QMV(QP_RA, QD_RA, 63, 0, QUAD_NRA - 1);
    rb = clamp(p[QP_RB1] * (int32_t)QUAD_NRCB + p[QP_RB2] + ((dst[QD_RB] * 180) >> 15), 0, QUAD_NRB - 1);
    bAll = m->inc;
    if (dst[QD_PITCH])
        bAll = (uint32_t)(((uint64_t)bAll * (uint32_t)quad_exp2((dst[QD_PITCH] * 4096) >> 15)) >> 15);
    bAB2 = bAll;
    if (dst[QD_PAB2])
        bAB2 = (uint32_t)(((uint64_t)bAll * (uint32_t)quad_exp2((dst[QD_PAB2] * 4096) >> 15)) >> 15);
    {
        int32_t rc = quad_ratio(QUAD_RCB_Q16[QMV(QP_RC, QD_RC, 18, 0, QUAD_NRCB - 1)], QMV(QP_OFSC, QD_OFSC, 100, -100, 100));
        int32_t rA = quad_ratio(QUAD_RA_Q16[ra], QMV(QP_OFSA, QD_OFSA, 100, -100, 100));
        int32_t rB1 = quad_ratio(QUAD_RCB_Q16[rb / (int32_t)QUAD_NRCB], QMV(QP_OFSB1, QD_OFSB1, 100, -100, 100));
        int32_t rB2 = quad_ratio(QUAD_RCB_Q16[rb % (int32_t)QUAD_NRCB], QMV(QP_OFSB2, QD_OFSB2, 100, -100, 100));
        inc[0] = quad_inc(bAll, rc, 0);
        inc[1] = quad_inc(bAB2, rA, QUAD_DT_UP[dt]);            /* DTUN: A up, B2 down (B1, C untouched) */
        inc[2] = quad_inc(bAll, rB1, 0);
        inc[3] = quad_inc(bAB2, rB2, QUAD_DT_DN[dt]);
    }
    /* HARM (Q8: the LFO's in between the steps): the two tables and the share of the second, Q15 */
    hq = clamp((p[QP_HARM] << 8) + ((dst[QD_HARM] * 26 * 256) >> 15), -26 * 256, 26 * 256);
    hf = 0;
    if (hq) {
        int32_t h = hq < 0 ? -hq : hq, t0 = h >> 8, t1 = t0 < 26 ? t0 + 1 : 26;
        hf = t1 > t0 ? (h & 255) << 7 : 0;
        ha_t = QUAD_HARM[t0];
        hb_t = QUAD_HARM[t1];
    }
    fbq = quad_fbq(QMV(QP_FDBK, QD_FDBK, 120, 0, 120));
    mix = QMV(QP_MIX, QD_MIX, 127, -64, 63);
    gy = (64 + mix) * 258;                       /* -64: X alone, 63: Y alone (gx + gy = 32766) */
    gx = 32766 - gy;
    /* the operator levels: envelope x LEV (A: squared; B: the B LEV law, squared) x velocity x key scaling, Q15 */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, velf = 32767 - ((p[QP_VEL] * (127 - vel) * 2080) >> 10), j, u1, u2;
        int32_t lv[3];
        quad_blev(QMV(QP_BLEV, QD_BLEV, 127, 0, 127), &u1, &u2);
        lv[0] = quad_lev(QMV(QP_ALEV, QD_ALEV, 127, 0, 127) * 4096 / 127);
        lv[1] = quad_lev(u1);
        lv[2] = quad_lev(u2);
        for (j = 0; j < 3; j++) {
            int32_t l = mulq15(lv[j], velf), kt = p[j == 0 ? QP_AKEY : j == 1 ? QP_B1KEY : QP_B2KEY];
            if (kt) {                            /* KEY: the modulation x 2^(-octaves above C3 x KEY / 127) */
                int32_t g = quad_exp2(-(((m->pitch16 - 60 * 16) * kt * 172) >> 10));
                g = g > 65535 ? 65535 : g;
                l = (l * g) >> 15;
                l = l > 32767 ? 32767 : l;
            }
            tl[j] = mulq15(j ? eB : eA, l);
        }
    }
    la = (int32_t)s->lv[0] << 16;
    lb1 = (int32_t)s->lv[1] << 16;
    lb2 = (int32_t)s->lv[2] << 16;
    dla = ((tl[0] << 16) - la) >> CTL_LOG2;
    dlb1 = ((tl[1] << 16) - lb1) >> CTL_LOG2;
    dlb2 = ((tl[2] << 16) - lb2) >> CTL_LOG2;
    /* the multimode filter: FREQ, its envelope by DEPTH, key track, the LFOs */
    fdep = QMV(QP_FDEPTH, QD_FENV, 127, -64, 63);
    cut = (QMV(QP_FREQ, QD_FREQ, 127, 0, 127) << 8) + ((eF * fdep) >> 6) +
          (((m->pitch16 - 60 * 16) * p[QP_FKTRK] * 150) >> 10);
    reso = QMV(QP_RESO, QD_RESO, 127, 0, 127);
    fty = p[QP_FTYPE];
    svf = fty != QF_OFF && !(fty != QF_HP12 && cut >= (127 << 8) && !reso);   /* LP wide open, no resonance: none */
    kd = 8192 - reso * 60;
    if (svf) {
        tsvf_coef_k(&c, cut, kd);
        if (fty == QF_LP24)
            tsvf_coef_k(&c2, cut, 8192);
    }
    base = QMV(QP_BASE, QD_BASE, 127, 0, 127);
    wid = QMV(QP_WIDTH, QD_WIDTH, 127, 0, 127);
    hpk = base ? (int32_t)QUAD_BW_K[base] : 0;
    lpk = base + wid < 127 ? (int32_t)QUAD_BW_K[base + wid] : 0;
    /* the amplitude: the amp envelope x velocity (half) x LEVEL (squared), the voice's (fades, LFO -> AMP) */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, lv = QMV(QP_LEVEL, QD_LEVEL, 127, 0, 127);
        A1 = mulq15(mulq15(eE, 32767 - (127 - vel) * 129), lv * lv * 2);
        if (t->p[P_VOICE] == V_UNISON)
            A1 = (A1 * 13107) >> 15;             /* 2 / 5 */
        A1 = mulq15(A1, m->amp1);
    }
#undef QMV
    A0 = s->amp;
    dA = (A1 - A0) >> CTL_LOG2;
    a = A0;
    /* the operators and MIX into acc: a loop per algorithm and HARM (none, - on C, + on A and B1) */
    p0 = s->ph[0], p1 = s->ph[1], p2 = s->ph[2], p3 = s->ph[3];
    i0 = inc[0], i1 = inc[1], i2 = inc[2], i3 = inc[3];
    fb1 = s->fb1, fb2 = s->fb2;
    if (hq < 0) {
#define WC(ph) quad_hw(ha_t, hb_t, hf, ph)
#define WA(ph) sine_i(ph)
#define WB1(ph) sine_i(ph)
        QUAD_ALGOS
#undef WC
#undef WA
#undef WB1
    } else if (hq > 0) {
#define WC(ph) sine_i(ph)
#define WA(ph) quad_hw(ha_t, hb_t, hf, ph)
#define WB1(ph) quad_hw(ha_t, hb_t, hf, ph)
        QUAD_ALGOS
#undef WC
#undef WA
#undef WB1
    } else {
#define WC(ph) sine_i(ph)
#define WA(ph) sine_i(ph)
#define WB1(ph) sine_i(ph)
        QUAD_ALGOS
#undef WC
#undef WA
#undef WB1
    }
    /* the DC blocker (QUAD_DC_K), then the base-width filter (BASE: a one-pole high-pass, BASE + WIDTH: a one-pole
     * low-pass, Q8 states): the manual's order, base-width before the multimode */
    {
        int32_t d = s->dc;
        bh = s->bh, bl = s->bl;
        for (i = 0; i < n; i++) {
            x = acc[i];
            d += (int32_t)(((((int64_t)x << 12) - d) * QUAD_DC_K) >> 16);
            y = x - ((d + 2048) >> 12);
            if (hpk) {
                bh += (int32_t)(((int64_t)((y << 8) - bh) * hpk) >> 16);
                y -= bh >> 8;
            }
            if (lpk) {
                bl += (int32_t)(((int64_t)((y << 8) - bl) * lpk) >> 16);
                y = bl >> 8;
            }
            acc[i] = y;
        }
        s->dc = d;
        s->bh = bh, s->bl = bl;
    }
    /* the multimode SVF, a loop per type (LP12 v2, HP12 x - kd v1 - v2, LP24 a plain LP12 then the resonant one; the
     * products rounded: truncation's bias, integrated, was DC), the soft knee after it */
    if (svf) {
        int32_t ic1 = s->f1, ic2 = s->f2, ic3 = s->f3, ic4 = s->f4;
#define QUAD_SVF(PRE, Y)                                                   \
    for (i = 0; i < n; i++) {                                              \
        int32_t v1, v2, v3;                                                \
        x = acc[i];                                                        \
        PRE                                                                \
        v3 = x - ic2;                                                      \
        v1 = (c.a1 * ic1 + c.a2 * v3 + 4096) >> 13;                        \
        v2 = ic2 + ((c.a2 * ic1 + c.a3 * v3 + 4096) >> 13);                \
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);                        \
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);                        \
        acc[i] = soft_knee(clamp((Y), -200000, 200000), 16000);            \
    }
        if (fty == QF_HP12)
            QUAD_SVF(, x - ((kd * v1) >> 12) - v2)
        else if (fty == QF_LP24)
            QUAD_SVF(v3 = x - ic4; v1 = (c2.a1 * ic3 + c2.a2 * v3 + 4096) >> 13;
                     v2 = ic4 + ((c2.a2 * ic3 + c2.a3 * v3 + 4096) >> 13);
                     ic3 = clamp(2 * v1 - ic3, -150000, 150000); ic4 = clamp(2 * v2 - ic4, -150000, 150000); x = v2;,
                     v2)
        else
            QUAD_SVF(, v2)
#undef QUAD_SVF
        s->f1 = ic1, s->f2 = ic2, s->f3 = ic3, s->f4 = ic4;
    }
    /* the amplitude */
    for (i = 0; i < n; i++) {
        y = clamp(acc[i], -65535, 65535);
        a += dA;
        out[i] += (mulq15(y, a) * (VOICE_FS / 4)) >> 11;
    }
    s->ph[0] = p0, s->ph[1] = p1, s->ph[2] = p2, s->ph[3] = p3;
    s->fb1 = fb1, s->fb2 = fb2;
    s->lv[0] = (int16_t)tl[0];
    s->lv[1] = (int16_t)tl[1];
    s->lv[2] = (int16_t)tl[2];
    s->amp = A1;
}
#undef WS
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
 * envelope, the base-width window; ENV the operator envelopes A / B, their delays and modes, the key scaling, the amp
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
    {2, {11, 0xFF}, ENG_B_NONE, 0, "ENV 3", {{QS_N, QS_N, QS_N}}, {{"A Key", "B1 Key", "B2 Key"}}},
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
    .name = "FM TONE",
    .page_title = {"SYN 1", "SYN 1+"},
    .edit = {QC_ALGO, QC_RC, QC_RA, QC_RB2, QC_HARM, QC_DTUN, QC_FDBK, QC_MIX},
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
