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
 * modulates itself by the average of its last two outputs (the DX7 family's; FDBK 0..127 on a 0..120 curve,
 * saw-like at 37). HARM -26.00..+26.00: the 26-wave additive series (quad_tables.h), interpolated between neighbours;
 * - shapes C, + shapes A and B1 (B2 never). DTUN detunes A up and B2 down. The outputs mixed X .. Y (MIX -64..63), a
 * DC blocker (QUAD_DC_K), the base-width filter (a one-pole high-pass at BASE, a one-pole low-pass at BASE + WIDTH),
 * the multimode filter (OFF, LP12, HP12, LP24: dsp.c's trapezoidal SVF, LP24 two in series) with its own ADSR envelope
 * (depth, delay, key track),
 * then QUAD's own amp envelope (engine_t.ownenv / done, as FM6 and the VA) and LEVEL.
 * Operator envelopes A (operator A's level) and B (B1's and B2's, through the B LEV law): ATK to 1, DEC to END, held
 * (attack-decay-end), each with a DELAY, a TRIG mode, a RESET; LEV A, LEV B (the law), velocity, key scaling (A, B1,
 * B2) scale them. Three LFOs per voice (FREE / HOLD run on the part's phase; TRIG / ONE / HALF on the voice's) at
 * control rate (every CTL = 32 samples), tempo-synced (MULT's BPM set) or at 120 BPM (its fixed set), summed into
 * their destinations (QD_*). Operator levels and the amplitude move per sample as linear ramps over the tick; the
 * phase increments, the filter's coefficients, HARM, FDBK and MIX are per tick. The times are the Digitone's (measured
 * on the owner's unit, docs/QUAD.md "Times"): a table per envelope type (amp attack, amp decay / release, filter
 * attack, filter decay / release, operator attack, operator decay; quad_times.h), the DELAYs v / 32 beats at the
 * part's tempo, the LFO FADE the measured fade times.
 *
 * The patch is the sound: QP_NP int16 values per part (quad_patch; since 2026-10-14 the Digitone's ranges, the
 * hi-res ones in hundredths: quad_hires, docs/QUAD.md "Ranges"), the deep pages edit it (eng_deep_t get / set from
 * the main loop), a factory preset (blob_preset), a user slot (the blob) or the init patch load it. The eight P_E0..P_E7
 * are macros into the patch (the SYN 1 page: ALGO RATIO C RATIO A RATIO B | HARM DTUNE FDBK MIX, QUAD_MAC: RATIO B's
 * macro is B2's step, the fast hand; the deep column is the pair B1 x QUAD_NRCB + B2, 0..360); quad_block picks a macro
 * change up, a deep set writes the macro back, as the VA. AMP's PAN and DRIVE are the part's P_PAN and P_DIST (not in
 * the patch: the deep page reads and writes the track parameters). The DRIVE is the sound's all the same: a preset's is
 * its preset_t FX() drive, a user sound's its record's P_DIST field, and ChoralRoot loads, saves and never gates it
 * (cr_ui.c "FM TONE'S DRIVE IS ITS PRESET'S").
 *
 * State: the patch, the macros as last seen, the part's LFO phases (the pool, as the VA's); per voice a quad_voice_t
 * (the pool: QUAD_NPART x QUAD_POLY). voice_t.s / .ph are not used (engine_t.keep 0): voice.c's retrigger restore of
 * ph[0..2] does not touch QUAD's own phases. */
#include "quad_tables.h"
#include "quad_times.h"          /* the Digitone's envelope and LFO fade times (tools/digitone/make_quad_times.py) */
#ifndef ENGI_QUAD
#define ENGI_QUAD 15u            /* engines.c ENGINES[] (append-only): 15 on ChoralRoot (after CZ-1, 14) */
#endif
#define QUAD_NPART 2u            /* parts with a QUAD patch (ChoralRoot: chord, bass) */
#define QUAD_POLY 8              /* engine_t.poly (docs/QUAD.md: CPU) */
#define QUAD_VOICE_US 200u       /* a chord voice at the unit's worst (WOBBLE: part0 1509 us for 8, ~190; DRIVE PRE
                                  * ~+20): engine_t.split, fx.c p5x_assign (3y206) */
#define QUAD_BLOB 96u            /* packed patch: 'Q', version, QP_NP values (the hi-res ones 2 bytes), zeros */
#define QUAD_BLOB_V3 80u         /* versions 1..3's blob (the legacy store's records, older pool slots, sessions) */
#define QUAD_MAGIC 0x51u         /* 'Q' */
#define QUAD_VER 6u              /* 6 (2026-10-31, FIZZ's engine): version 5 + the values QP_AMOD .. QP_SENDS in
                                  * bytes 87..91 (quad_pack6), FTYPE 0..7; written only when one of them is set (a
                                  * sound without them stays version 5, as before: older firmware and tools play it);
                                  * 5 (2026-10-14, the owner's corrections): AMP REL, AMP DEC 0..126 + INF, LEVEL
                                  * 0..127 integers (bytes), FREQ DTUN RESO BASE top 127.00; 4: the Digitone's ranges
                                  * (2026-10-14): the hi-res values in hundredths (u16le), FDBK 0..127; 3 (the Digitone review 2026-10-08), 2 and 1 are read (quad_unpack
                                  * converts) */
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
    QP_NP5,                                                                /* version 5's values end here */
    /* version 6 (2026-10-31, FIZZ's engine: "they sound quite a bit better with FIZZ's engine"): FIZZ's E1..E3 values
     * (FMToneSeq quad_voice.h QD_QP_AMOD ..), each one's default today's sound; packed in bytes 87..91 (quad_pack6) */
    QP_AMOD = QP_NP5,                                                      /* the amp envelope: 0 ADSR, 1 AHD */
    QP_HOLD,                                                               /* AHD's HOLD 0..126 (a time), 127 NOTE */
    QP_DRPRE,                                                              /* DRIVE: 0 after the voice, 1 before the filter */
    QP_FSHAPE,                                                             /* the filter machine's own (MULTI, COMB, EQ) */
    QP_FGAIN,                                                              /* EQ's gain -64..63 */
    QP_BWPOST,                                                             /* base-width: 0 before the filter, 1 after */
    QP_BITS,                                                               /* CRUSH: bits 0 OFF, 1..127 */
    QP_SRR,                                                                /* .. sample-rate reduction 0 OFF, 1..127 */
    QP_CRPRE,                                                              /* .. 0 after the filter, 1 before (FIZZ's
                                                                            * CRPOST 1 / 0) */
    QP_SENDS,                                                              /* 1: the sound carries its sends (its record's
                                                                            * P_REV P_DLY P_CHOR; cr_ui.c cu_sends_load) */
    QP_NP
};
#define QP_LFO(k, f) (QP_LFO0 + (k) * QL_N + (f))
/* the hi-res fields (version 5: F_FIX2, hundredths, two bytes in the blob): HARM DTUN FREQ RESO filter DEPTH BASE,
 * each LFO's SPEED and DEPTH */
static int quad_hires(uint32_t i)
{
    if (i >= QP_LFO0 && i < QP_RB2)
        return (i - QP_LFO0) % QL_N == QL_SPEED || (i - QP_LFO0) % QL_N == QL_DEPTH;
    return i == QP_HARM || i == QP_DTUN || i == QP_FREQ || i == QP_RESO || i == QP_FDEPTH || i == QP_BASE;
}
/* version 4's hi-res fields: these and AMP REL, LEVEL */
static int quad_hires_v4(uint32_t i) { return quad_hires(i) || i == QP_EREL || i == QP_LEVEL; }
#define QUAD_NHIRES 12u
#define QUAD_V4_LEN (2u + QP_NP5 + QUAD_NHIRES + 2u)  /* 89: version 4's (the rest of the 96 is zeros) */
#define QUAD_V5_LEN (2u + QP_NP5 + QUAD_NHIRES)       /* 87 */
#define QUAD_V6_LEN (QUAD_V5_LEN + 5u)                /* 92: version 6's (bytes 92..95 zeros) */
_Static_assert(QP_NP5 == 73 && QP_NP == 83 && QP_RB2 == QUAD_NP_V1 && QP_B2KEY == QUAD_NP_V2 && QP_NP5 + 2u + 2u <= QUAD_BLOB_V3 &&
               QUAD_V6_LEN <= QUAD_BLOB &&
               QUAD_V4_LEN <= QUAD_BLOB && QUAD_V5_LEN <= QUAD_BLOB && QUAD_BLOB <= ENG_BLOB_MAX && QUAD_BLOB <= 160u /* CSS_BLOB */ &&
               QUAD_BLOB <= 188u /* upool.c UPP_MAX */, "QUAD patch layout");
/* the macros P_E0..P_E7: these patch values (RATIO B's is B2's step, the fast hand) */
static const uint8_t QUAD_MAC[8] = {QP_ALGO, QP_RC, QP_RA, QP_RB2, QP_HARM, QP_DTUN, QP_FDBK, QP_MIX};

enum { QF_OFF, QF_LP12, QF_HP12, QF_LP24,                                  /* FTYPE */
       QF_MULTI, QF_COMBP, QF_COMBN, QF_EQ, QF_N };                        /* (version 6: FIZZ's machines, E2) */
enum { QW_TRI, QW_SINE, QW_SQR, QW_SAW, QW_RAMP, QW_EXP, QW_RAND, QW_N };   /* LFO WAVE */
enum { QT_FREE, QT_TRIG, QT_HOLD, QT_ONE, QT_HALF, QT_N };                 /* LFO TRIG */
enum { QR_OFF, QR_ALL, QR_C, QR_AB, QR_AB2, QR_N };                        /* PHRT: the operators reset at a note */
static const uint8_t QUAD_PHRT_MASK[QR_N] = {0, 0xF, 0x1, 0xE, 0xA};       /* (bit 0 C, 1 A, 2 B1, 3 B2) */
/* PHRT on a voice that sounds (struck again, a chord's common tone): an operator of the set restarts at 0 only where
 * no one hears it (quad_phrt_mask): as a carrier, the voice under QUAD_PHRT_QUIET (its envelope x level for an enveloped
 * one); as a modulator, its level under QUAD_PHRT_QMOD (Q15) */
#ifndef QUAD_PHRT_QUIET
#define QUAD_PHRT_QUIET 328      /* -40 dB */
#endif
#ifndef QUAD_PHRT_QMOD
#define QUAD_PHRT_QMOD 16        /* -66 dB: a phase step of 0.013 rad in what it modulates */
#endif
enum {                                                                     /* LFO DEST (the Digitone's App. C) */
    QD_NONE, QD_PITCH, QD_PAB2, QD_ALGO, QD_RC, QD_RA, QD_RB, QD_OFSC, QD_OFSA, QD_OFSB1, QD_OFSB2, QD_HARM, QD_DTUN,
    QD_FDBK, QD_MIX, QD_ALEV, QD_BLEV, QD_AATK, QD_ADEC, QD_AEND, QD_BATK, QD_BDEC, QD_BEND, QD_ADLY, QD_BDLY,
    QD_FREQ, QD_RESO, QD_FENV, QD_BASE, QD_WIDTH, QD_FATK, QD_FDEC, QD_FSUS, QD_FREL, QD_EATK, QD_EDEC, QD_ESUS,
    QD_EREL, QD_LEVEL, QD_PAN,
    /* appended 2026-10-14 (the Digitone's full list; the numbers above stay: version-3 blobs keep their meaning):
     * LFO 1's own SPEED MULT FADE WAVE SPH MODE DEPTH (for LFO 2 and 3; LFO 1 on itself: none), RATIO ALL, the AB
     * macros (both operator envelopes), the part's DRIVE and its three sends, the filter envelope's DELAY */
    QD_L1SPD, QD_L1MUL, QD_L1FAD, QD_L1WAV, QD_L1SPH, QD_L1MOD, QD_L1DEP, QD_RALL, QD_ABLEV, QD_ABATK, QD_ABDEC,
    QD_ABEND, QD_ABDLY, QD_DRIVE, QD_SREV, QD_SDLY, QD_SCHO, QD_FDLY, QD_N
};
#define QD_N_V3A 40u             /* the destinations before 2026-10-14 (an older firmware refuses a blob with more) */
#define QUAD_NMULT 24u           /* 0..11 x1 .. x2k at the part's BPM, 12..23 the same at 120 BPM */
static const char *const N_QUAD_FTYPE[QF_N] = {"OFF", "LP12", "HP12", "LP24", "MULTI", "COMB+", "COMB-", "EQ"};
static const char *const N_QUAD_BWRT[2] = {"PRE", "POST"};
static const char *const N_QUAD_AMOD[2] = {"ADSR", "AHD"};    /* the amp envelope's mode (version 6) */    /* base-width: before the filter, after it (BWPOST) */
static const char *const N_QUAD_WAVE[QW_N] = {"TRI", "SINE", "SQR", "SAW", "RAMP", "EXP", "RAND"};
static const char *const N_QUAD_TRIG[QT_N] = {"FREE", "TRIG", "HOLD", "ONE", "HALF"};
static const char *const N_QUAD_PHRT[QR_N] = {"OFF", "ALL", "C", "A+B", "A+B2"};
static const char *const N_QUAD_ROUTE[2] = {"POST", "PRE"};   /* DRIVE ROUTE (version 6): after the voice, before the filter */
static const char *const N_QUAD_DEST[QD_N] = {   /* <= 5 characters (params.c prints 5) */
    "NONE", "PITCH", "P AB2", "ALGO", "RAT C", "RAT A", "RAT B", "OFS C", "OFS A", "OFSB1", "OFSB2", "HARM", "DTUN",
    "FDBK", "MIX", "A LEV", "B LEV", "A ATK", "A DEC", "A END", "B ATK", "B DEC", "B END", "A DLY", "B DLY", "FREQ",
    "RESO", "FENV", "BASE", "WIDTH", "F ATK", "F DEC", "F SUS", "F REL", "AMP A", "AMP D", "AMP S", "AMP R", "LEVEL",
    "PAN", "L1SPD", "L1MUL", "L1FAD", "L1WAV", "L1SPH", "L1MOD", "L1DEP", "R ALL", "ABLEV", "ABATK", "ABDEC",
    "ABEND", "ABDLY", "DRIVE", "REV", "DEL", "CHO", "F DLY"};
/* the editor's order of DEST (cr_pages.c cp_dstep steps through it): the Digitone's (its destination numbers 0, 1 .. 77
 * ascending, docs/QUAD.md "LFO"), so the list reads as the Digitone's; the stored numbers are QD_*'s */
static const uint8_t QUAD_DEST_ORDER[QD_N] = {
    QD_NONE, QD_L1SPD, QD_L1MUL, QD_L1FAD, QD_L1WAV, QD_L1SPH, QD_L1MOD, QD_L1DEP, QD_ALGO, QD_RC, QD_RA, QD_RB,
    QD_HARM, QD_DTUN, QD_FDBK, QD_MIX, QD_AATK, QD_ADEC, QD_AEND, QD_ALEV, QD_BATK, QD_BDEC, QD_BEND, QD_BLEV,
    QD_ADLY, QD_BDLY, QD_PITCH, QD_PAB2, QD_RALL, QD_ABLEV, QD_ABATK, QD_ABDEC, QD_ABEND, QD_ABDLY, QD_FREQ, QD_RESO,
    QD_FENV, QD_FATK, QD_FDEC, QD_FSUS, QD_FREL, QD_BASE, QD_WIDTH, QD_EATK, QD_EDEC, QD_ESUS, QD_EREL, QD_DRIVE,
    QD_PAN, QD_LEVEL, QD_SREV, QD_SDLY, QD_SCHO, QD_OFSC, QD_OFSA, QD_OFSB1, QD_OFSB2, QD_FDLY};
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
#define QC_HARM {"HARM", F_FIX2, -2600, 2600, 0, 0, 0}   /* F_FIX2: hundredths (the Digitone's -26.00..+26.00) */
#define QC_DTUN {"DTUNE", F_FIX2, 0, 12700, 0, 0, 0}
#define QC_HARM_M {"HARM", F_OFS, -26, 26, 0, 0, 0}         /* the macros P_E4 / P_E5: the coarse part (quad_mac_val) */
#define QC_DTUN_M {"DTUNE", F_INT, 0, 127, 0, 0, 0}
#define QC_FDBK {"FDBK", F_INT, 0, 127, 0, 0, 0}
#define QC_MIX {"MIX", F_OFS, -64, 63, -64, 0, 0}
#define QC_OFS(l) {l, F_INT, -100, 100, 0, N_QUAD_OFS, 0}
/* a time: 0..127 as a number, as the Digitone shows it (its times are quad_times.h's, docs/QUAD.md "Times": F_TIME's
 * 1 ms .. 10 s text would be another curve's) */
#define QC_T(l, d) {l, F_INT, 0, 127, d, 0, 0}
#define QC_TI(l, d) {l, F_INTI, 0, 127, d, 0, 0}          /* a release: 0..126, 127 = INF (it holds) */
#define QC_FX(l, mn, mx, d) {l, F_FIX2, mn, mx, d, 0, 0}   /* hi-res: hundredths, "42.01" / "+41.34" */
#define QC_EI(l, d) {l, F_INTI, 0, 127, d, 0, 0}   /* AMP DEC / REL: 0..126, 127 = INF */
#define QC_P(l, d) {l, F_INT, 0, 127, d, 0, 0}
#define QC_PCT(l, d) {l, F_PCT, 0, 127, d, 0, 0}
#define QC_ON(l, d) {l, F_ONOFF, 0, 1, d, 0, 0}
#define QC_PHRT {"PHRT", F_ENUM, 0, QR_N - 1, QR_ALL, N_QUAD_PHRT, 0}
#define QC_ROUTE {"ROUTE", F_ENUM, 0, 1, 0, N_QUAD_ROUTE, 0}
#define QC_BWRT {"BW RT", F_ENUM, 0, 1, 0, N_QUAD_BWRT, 0}
#define QC_FREQ {"FREQ", F_FIX2, 0, 12700, 12700, 0, 0}
#define QC_FTYPE {"TYPE", F_ENUM, 0, QF_N - 1, QF_LP12, N_QUAD_FTYPE, 0}
#define QC_DEPTH(l) {l, F_FIX2, -6400, 6399, 0, 0, 0}
#define QC_PAN {"PAN", F_OFS, -64, 63, 0, 0, 0}
#define QC_SPEED {"SPEED", F_FIX2, -6400, 6399, 3200, 0, 0}
#define QC_MULT {"MULT", F_ENUM, 0, QUAD_NMULT - 1, 5, N_QUAD_MULT, 0}
#define QC_FADE {"FADE", F_OFS, -64, 63, 0, 0, 0}
#define QC_DEST {"DEST", F_ENUM, 0, QD_N - 1, QD_NONE, N_QUAD_DEST, 0}
#define QC_WAVE {"WAVE", F_ENUM, 0, QW_N - 1, QW_TRI, N_QUAD_WAVE, 0}
#define QC_LTRIG {"TRIG", F_ENUM, 0, QT_N - 1, QT_FREE, N_QUAD_TRIG, 0}
#define QC_NONE {0, 0, 0, 0, 0, 0, 0}
#define QUAD_X 0xFFu             /* an empty column */
#define QUAD_XPAN 0xFEu          /* AMP+: the part's P_PAN */
#define QUAD_XDIST 0xFDu         /* AMP+: the part's P_DIST (DRIVE: the sound's while the part is on FM TONE) */

/* the columns' defaults are the init patch: since 2026-10-14 the Digitone's init sound (the owner's export, decoded by
 * web/fm1dnsnd.js: MIX -64, A / B DEC 32 END 127, A LEV 0, the filter REL 64, AMP ATK 8 DEC 32 SUS 96 REL 24, LFO
 * SPEED 32.00 MULT 32; its times are FM TONE's 1:1, docs/QUAD.md "Times"); LFO 3 (the Digitone has two) as 1 and 2 */
static const eng_page_t QUAD_PAGES[] = {
    {"SYN 1", {QC_ALGO, QC_RC, QC_RA, QC_RB}},                              /* 0  OSC */
    {"SYN 1+", {QC_HARM, QC_DTUN, QC_FDBK, QC_MIX}},
    {"SYN 2", {QC_OFS("OFS C"), QC_OFS("OFS A"), QC_OFS("OFS B1"), QC_OFS("OFS B2")}},
    {"FILTER", {QC_T("ATK", 0), QC_T("DEC", 64), QC_P("SUS", 0), QC_TI("REL", 64)}},   /* 3  FILTER */
    {"FILTER+", {QC_FREQ, QC_FX("RESO", 0, 12700, 0), QC_FTYPE, QC_DEPTH("DEPTH")}},
    {"FILT 2", {QC_T("DELAY", 0), QC_P("KTRK", 0), QC_P("SHAPE", 0), {"GAIN", F_OFS, -64, 63, 0, 0, 0}}},   /* (SHAPE, GAIN:
                                                  * the machines', version 6) */
    {"FILT 2+", {QC_FX("BASE", 0, 12700, 0), QC_P("WIDTH", 127), QC_BWRT, QC_NONE}},
    {"ENV A", {QC_T("A ATK", 0), QC_T("A DEC", 32), QC_P("A END", 127), QC_P("A LEV", 0)}},   /* 7  ENV */
    {"ENV B", {QC_T("B ATK", 0), QC_T("B DEC", 32), QC_P("B END", 127), QC_P("B LEV", 0)}},
    {"ENV 2", {QC_T("A DLY", 0), QC_ON("A TRIG", 1), QC_ON("A RESET", 1), QC_PHRT}},
    {"ENV 2+", {QC_T("B DLY", 0), QC_ON("B TRIG", 1), QC_ON("B RESET", 1), QC_P("VEL", 64)}},
    {"ENV 3", {QC_P("A KEY", 0), QC_P("B1 KEY", 0), QC_P("B2 KEY", 0), QC_NONE}},
    {"AMP", {QC_T("ATK", 8), QC_EI("DEC", 32), QC_PCT("SUS", 96), QC_EI("REL", 24)}},
    {"AMP+", {QC_P("LEVEL", 110), QC_PAN, QC_PCT("DRIVE", 0), QC_ROUTE}},   /* (ROUTE: DRIVE's, version 6) */
    {"LFO 1", {QC_SPEED, QC_MULT, QC_FADE, QC_DEST}},                       /* 14 LFO */
    {"LFO 1+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    {"LFO 2", {QC_SPEED, QC_MULT, QC_FADE, QC_DEST}},
    {"LFO 2+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    {"LFO 3", {QC_SPEED, QC_MULT, QC_FADE, QC_DEST}},
    {"LFO 3+", {QC_WAVE, QC_P("PHASE", 0), QC_LTRIG, QC_DEPTH("DEPTH")}},
    /* appended 2026-10-31 (version 6; the pages before keep their numbers: layouts, the web editor, the CC map) */
    {"AMP 2", {{"MODE", F_ENUM, 0, 1, 0, N_QUAD_AMOD, 0}, QC_TI("HOLD", 127), QC_NONE, QC_NONE}},   /* 20: AHD */
    {"CRUSH", {QC_P("BITS", 0), QC_P("SRR", 0), QC_ROUTE, QC_NONE}},    /* 21: CRUSH, its ROUTE (POST after the filter) */
};
#define QUAD_NPAGES ((uint32_t)NELEM(QUAD_PAGES))
#define QUAD_PG_OSC 0u
#define QUAD_PG_FILTER 3u
#define QUAD_PG_FPLUS 4u          /* FILTER+: its RESO and TYPE cells by the type (quad_ix) */
#define QUAD_PG_ENV 7u
#define QUAD_PG_LFO 14u
#define QUAD_PG_AMP2 20u
#define QUAD_PG_CRUSH 21u
_Static_assert(NELEM(QUAD_PAGES) == 22, "QUAD pages");
#define QUAD_LPG(k) {QP_LFO(k, QL_SPEED), QP_LFO(k, QL_MULT), QP_LFO(k, QL_FADE), QP_LFO(k, QL_DEST)}, \
                    {QP_LFO(k, QL_WAVE), QP_LFO(k, QL_PHASE), QP_LFO(k, QL_TRIG), QP_LFO(k, QL_DEPTH)}
static const uint8_t QUAD_MAP[NELEM(QUAD_PAGES)][4] = {
    {QP_ALGO, QP_RC, QP_RA, QP_RB1},             /* (RATIO B: the pair, QP_RB1 and QP_RB2: quad_get / quad_set) */
    {QP_HARM, QP_DTUN, QP_FDBK, QP_MIX},
    {QP_OFSC, QP_OFSA, QP_OFSB1, QP_OFSB2},
    {QP_FATK, QP_FDEC, QP_FSUS, QP_FREL},
    {QP_FREQ, QP_RESO, QP_FTYPE, QP_FDEPTH},
    {QP_FDLY, QP_FKTRK, QP_FSHAPE, QP_FGAIN},
    {QP_BASE, QP_WIDTH, QP_BWPOST, QUAD_X},
    {QP_AATK, QP_ADEC, QP_AEND, QP_ALEV},
    {QP_BATK, QP_BDEC, QP_BEND, QP_BLEV},
    {QP_ADLY, QP_ATRIG, QP_ARST, QP_PHRT},
    {QP_BDLY, QP_BTRIG, QP_BRST, QP_VEL},
    {QP_AKEY, QP_B1KEY, QP_B2KEY, QUAD_X},
    {QP_EATK, QP_EDEC, QP_ESUS, QP_EREL},
    {QP_LEVEL, QUAD_XPAN, QUAD_XDIST, QP_DRPRE},
    QUAD_LPG(0), QUAD_LPG(1), QUAD_LPG(2),
    {QP_AMOD, QP_HOLD, QUAD_X, QUAD_X},
    {QP_BITS, QP_SRR, QP_CRPRE, QUAD_X},
};

/* the range and the init value of patch value i: the page column that shows it (every value is on one page; B1 and
 * B2 share RATIO B's: a C/B step each) */
typedef struct { int16_t min, max, def; } quad_rng_t;
/* version 6's values (QP_AMOD ..): their ranges and defaults (today's sound) */
static const quad_rng_t QUAD_RNG6[QP_NP - QP_NP5] = {
    {0, 1, 0}, {0, 127, 127}, {0, 1, 0}, {0, 127, 0}, {-64, 63, 0}, {0, 1, 0}, {0, 127, 0}, {0, 127, 0}, {0, 1, 0},
    {0, 1, 0}};
#define QUAD_FT6 7               /* version 6's FTYPE top (FIZZ's machines: MULTI COMB+ COMB- EQ, 4..7) */
static quad_rng_t quad_range(uint32_t i)
{
    quad_rng_t r = {0, 0, 0};
    uint32_t pg, c;
    if (i >= QP_NP5)
        return QUAD_RNG6[i < QP_NP ? i - QP_NP5 : 0];
    if (i == QP_RB1 || i == QP_RB2) {
        r.max = (int16_t)(QUAD_NRCB - 1u);
        r.def = 3;                               /* 1.00 */
        return r;
    }
    for (pg = 0; pg < QUAD_NPAGES; pg++)
        for (c = 0; c < 4u; c++)
            if (QUAD_MAP[pg][c] == i) {
                const param_desc_t *d = &QUAD_PAGES[pg].col[c];
                r.min = (int16_t)d->min;
                r.max = (int16_t)d->max;
                r.def = (int16_t)d->def;
                return r;
            }
    return r;
}

/* version 4's ranges: the hi-res tops 127.99, AMP REL 0..127.99 (127.xx INF), LEVEL 0..127.99 */
static quad_rng_t quad_range_v4(uint32_t i)
{
    quad_rng_t r = quad_range(i);
    if (i == QP_DTUN || i == QP_FREQ || i == QP_RESO || i == QP_BASE || i == QP_EREL || i == QP_LEVEL) {
        r.max = 12799;
        r.def = (int16_t)(quad_hires(i) ? r.def : r.def * 100);
    }
    return r;
}

/* a version-4 value of field i -> version 5's: FREQ DTUN RESO BASE clamped to 127.00 (quad_clampv), AMP REL 127.xx
 * = INF (127), below it rounded but at most 126; LEVEL rounded */
static int32_t quad_v5_of_v4(uint32_t i, int32_t v)
{
    if (i == QP_EREL)
        return v >= 12700 ? 127 : v >= 12650 ? 126 : (v + 50) / 100;
    if (i == QP_LEVEL)
        return (v + 50) / 100;
    return v;
}

/* version 3's ranges (its blob stores value - min): the hi-res fields in whole numbers (-64..63, 0..127, -26..26),
 * FDBK 0..120 */
static quad_rng_t quad_range_v3(uint32_t i)
{
    quad_rng_t r = quad_range_v4(i);
    if (quad_hires_v4(i)) {
        r.min = (int16_t)(r.min / 100);
        r.max = (int16_t)(r.max / 100);
        r.def = (int16_t)(r.def / 100);
    } else if (i == QP_FDBK) {
        r.max = 120;
    }
    return r;
}

/* a version-3 value of field i -> version 4's: the hi-res x 100 (AMP REL 127 = INF stays INF), FDBK 0..120 -> 0..127 */
static int32_t quad_v4_of_v3(uint32_t i, int32_t v)
{
    if (quad_hires_v4(i))
        return v * 100;
    if (i == QP_FDBK)
        return (v * 127 + 60) / 120;
    return v;
}

/* version 1 / 2's ranges where they differ from version 3's (their blobs store value - min) */
static quad_rng_t quad_range_v2(uint32_t i)
{
    quad_rng_t r = quad_range_v3(i);
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
static int16_t quad_patch[QUAD_NPART][QP_NP] __attribute__((section(".pool")));
static int16_t quad_mlast[QUAD_NPART][8] __attribute__((section(".pool")));   /* P_E0..P_E7 as last put in the patch */
static uint8_t quad_user_pending;                /* upreset.c up_values: a QUAD user slot + 1 is being loaded */
static int (*quad_store_read)(uint32_t k, uint8_t *blob);   /* the patch store: slot k's blob, 0 = there is one */
static int8_t quad_pan_off[QUAD_NPART];          /* the LFOs' PAN (fx.c mix_part adds it: quad_pan) */
static int8_t quad_fx_off[QUAD_NPART][4];        /* .. DRIVE, the reverb, delay, chorus sends (fx.c: quad_drive,
                                                  * quad_sends; read once a block) */
static int8_t quad_dro[QUAD_NPART];              /* the LFOs' DRIVE as the last block left it, for DRIVE PRE (quad_block:
                                                  * the voices on two cores read it while the latest one writes
                                                  * quad_fx_off, 3y206) */
static struct {                                  /* per part: the LFOs' FREE phases */
    uint32_t ph[3], rnd[3];
} quad_lfo[QUAD_NPART] __attribute__((section(".pool")));
/* DRIVE ROUTE PRE, the part's (quad_block, 3y213): dsp.c's dsp_dist at drive d, laid out for the voices' sample loop.
 * The soft clip and the make-up as one table on the clip's input, signed (u -65536 .. 65536 in steps of 256: the
 * nodes TANH_Q15 has, so the same line between them), less its bias's output and x mk: a load and an interpolation a
 * sample, no sign, no make-up, no flash read; built again only when d changes (257 steps). elo / ehi: the low cut's
 * output clamped so that u stays on the table (past it the clip is flat, as dsp_dist's +-230000 is) */
typedef struct {
    uint8_t on, d;                               /* (on: DRPRE and a drive this block; d: the table's) */
    int32_t g, k, elo, ehi;                      /* dsp_dist's drive (Q13 here: half its g) and tone (Q15) */
    int32_t tab[514];                            /* (514: u = 65536 reads its last node at a fraction 0) */
} quad_drv_t;
static quad_drv_t quad_drv[QUAD_NPART] __attribute__((section(".pool")));
#define QUAD_DRV_BIAS 2400                       /* dsp_dist's bias (even harmonics) */
typedef struct {
    uint32_t ph[4];                              /* C A B1 B2 */
    int32_t fb1, fb2;                            /* the feedback operator's last two outputs (its wave, Q15) */
    int32_t env[4];                              /* Q24: operator A, operator B, filter, amp */
    uint32_t dly[4];                             /* DELAY progress, Q24 */
    uint8_t stage[4];                            /* 0 off, 1 delay, 2 attack, 3 decay (/ sustain), 4 release, 5 the
                                                  * release held (REL INF: 127) */
    int16_t lv[3];                               /* the ramps' ends: operator A's, B1's, B2's level, Q15 */
    int32_t amp;                                 /* .. the amplitude, Q15 */
    int32_t f1, f2, f3, f4;                      /* the SVF (LP24: f3 f4 its first stage) */
    int32_t bh, bl;                              /* the base-width one-poles, Q8 */
    int32_t dc;                                  /* the DC blocker's one-pole (the operators' mean), Q12 */
    uint32_t lph[3], lrnd[3], ltr[3];            /* the LFOs' own phases (TRIG ONE HALF HOLD), random, travel */
    int32_t lsl[3];                              /* the LFOs' RAND, slewed (SPH on RAND), Q15 */
    uint32_t ticks;                              /* control ticks since the note-on (LFO FADE: up to ~200 s; the
                                                  * padding before lof held the two bytes more) */
    uint32_t lof;                                /* LFO 1 FREE: its phase's lead from L1SPD / L1MUL (LFO 2, 3) */
    uint8_t lstop;                               /* bit k: LFO k (ONE / HALF) has stopped */
    uint8_t live;
    uint8_t fon;                                 /* the filters running in the last block: bit 0 the multimode, 1 the
                                                  * base-width high-pass, 2 its low-pass (QUAD_FON_NEW: a fresh voice,
                                                  * none before it); a filter switched in or out crossfades its block */
    int8_t mixp;                                 /* MIX in the last block (QUAD_MIX_NEW: a fresh voice): ramped over
                                                  * the block to this block's */
    int16_t fsm, rsm;                            /* FREQ and RESO as the filter runs them (quad_glide_v, FIZZ's P2.6b) */
    int32_t dh, dl1, dl2;                        /* DRIVE before the filter (QP_DRPRE): the voice's DIST (dsp.c) */
    uint32_t crph;                               /* CRUSH: SRR's phase, the value it holds */
    int32_t crheld;
    uint8_t cline, cfresh;                       /* COMB: the line lent to it + 1 (0 none: MULTI; quad_comb_lend), its
                                                  * line from rest at the next render */
    uint16_t cw;                                 /* .. the line's write place */
    int32_t clp, clen;                           /* .. its damping low-pass, its length as the last block ended (Q8) */
} quad_voice_t;
#define QUAD_FON_NEW 0xFFu
#define QUAD_MIX_NEW (-128)
static quad_voice_t quad_vs[QUAD_NPART][QUAD_POLY] __attribute__((section(".pool")));

static uint32_t quad_tr(const track_t *t) { return (uint32_t)(t - trk); }

static void quad_init_patch(int16_t *p)
{
    uint32_t i;
    for (i = 0; i < QP_NP; i++)
        p[i] = quad_range(i).def;
}

static int16_t quad_clampv(uint32_t i, int32_t v)
{
    quad_rng_t r = quad_range(i);
    return (int16_t)(v < r.min ? r.min : v > r.max ? r.max : v);
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

/* a macro holds a hi-res value's coarse part (HARM, DTUNE: P_E4, P_E5 are 7-bit everywhere: a record, the editor's
 * v14, the matrix, motion, a CC): patch -> macro truncates the hundredths, macro -> patch sets fine 0 */
static int16_t quad_mac_val(uint32_t k, int32_t v) { return (int16_t)(quad_hires(QUAD_MAC[k]) ? v / 100 : v); }
static int16_t quad_mac_patch(uint32_t k, int32_t m)
{
    return quad_clampv(QUAD_MAC[k], quad_hires(QUAD_MAC[k]) ? m * 100 : m);
}

/* version 6's five bytes (87..91, docs/QUAD.md "Version 6"): each a 7-bit value with a flag in bit 7, every default
 * 0: 87 127 - HOLD | AMOD, 88 FSHAPE | BWPOST, 89 FGAIN (7-bit two's complement) | DRPRE, 90 BITS | CRPRE, 91 SRR |
 * SENDS. All five 0 (and FTYPE 0..3): a version-5 sound */
static void quad_pack6(const int16_t *p, uint8_t *t)
{
#define QB(i) (uint32_t)quad_clampv(i, p[i])
    t[0] = (uint8_t)((127u - QB(QP_HOLD)) | QB(QP_AMOD) << 7);
    t[1] = (uint8_t)(QB(QP_FSHAPE) | QB(QP_BWPOST) << 7);
    t[2] = (uint8_t)((QB(QP_FGAIN) & 127u) | QB(QP_DRPRE) << 7);
    t[3] = (uint8_t)(QB(QP_BITS) | QB(QP_CRPRE) << 7);
    t[4] = (uint8_t)(QB(QP_SRR) | QB(QP_SENDS) << 7);
#undef QB
}
static void quad_unpack6(const uint8_t *t, int16_t *p)
{
    p[QP_HOLD] = (int16_t)(127 - (t[0] & 127)), p[QP_AMOD] = (int16_t)(t[0] >> 7);
    p[QP_FSHAPE] = (int16_t)(t[1] & 127), p[QP_BWPOST] = (int16_t)(t[1] >> 7);
    p[QP_FGAIN] = (int16_t)((int32_t)((uint32_t)t[2] << 25) >> 25), p[QP_DRPRE] = (int16_t)(t[2] >> 7);
    p[QP_BITS] = (int16_t)(t[3] & 127), p[QP_CRPRE] = (int16_t)(t[3] >> 7);
    p[QP_SRR] = (int16_t)(t[4] & 127), p[QP_SENDS] = (int16_t)(t[4] >> 7);
}

/* the patch -> blob: 'Q', version, per value (value - min): the hi-res ones u16 little-endian (0..12700), the rest a
 * byte (0..200); version 6's five bytes when one is set or FTYPE is a machine (4..7), else version 5; zeros to
 * QUAD_BLOB */
static void quad_pack(const int16_t *p, uint8_t *b)
{
    uint32_t i, o = 2;
    uint8_t t[5];
    b[0] = QUAD_MAGIC;
    for (i = 0; i < QP_NP5; i++) {
        uint32_t u = (uint32_t)(quad_clampv(i, p[i]) - quad_range(i).min);
        b[o++] = (uint8_t)u;
        if (quad_hires(i))
            b[o++] = (uint8_t)(u >> 8);
    }
    quad_pack6(p, t);
    b[1] = (t[0] | t[1] | t[2] | t[3] | t[4]) || quad_clampv(QP_FTYPE, p[QP_FTYPE]) > QF_LP24 ? 6u : 5u;
    if (b[1] == 6u)
        for (i = 0; i < 5u; i++)
            b[o++] = t[i];
    while (o < QUAD_BLOB)
        b[o++] = 0;
}

/* a valid blob: version 6 (version 5's values, FTYPE 0..7, its five bytes, zeros), version 5 / 4 (QUAD_V5_LEN / QUAD_V4_LEN bytes in QUAD_BLOB), 3, 2 (QUAD_NP_V2 values) or 1 (QUAD_NP_V1 values, RATIO B
 * 0..QUAD_NRB_V1 - 1), versions 1..3 in QUAD_BLOB_V3 bytes (the rest of a QUAD_BLOB buffer zeros). An older firmware
 * (QUAD_VER 3 / 4 / 5) refuses a newer blob here: it loads the init patch for it (a version-6 blob on 0.27 or
 * older: INIT) */
static int quad_blob_ok(const uint8_t *b)
{
    uint32_t i, np, ver, o = 2;
    if (!b || b[0] != QUAD_MAGIC || b[1] < 1u || b[1] > QUAD_VER)
        return 0;
    ver = b[1];
    np = ver == 1u ? QUAD_NP_V1 : ver == 2u ? QUAD_NP_V2 : QP_NP5;
    for (i = 0; i < np; i++) {
        if (ver >= 4u) {
            quad_rng_t r = ver == 4u ? quad_range_v4(i) : quad_range(i);
            uint32_t u = b[o++];
            if (i == QP_FTYPE)                   /* (the machines 4..7: version 6 only) */
                r.max = ver == 6u ? QUAD_FT6 : QF_LP24;
            if (ver == 4u ? quad_hires_v4(i) : quad_hires(i))
                u |= (uint32_t)b[o++] << 8;
            if (u > (uint32_t)(r.max - r.min))
                return 0;
        } else {
            quad_rng_t r = ver == 3u ? quad_range_v3(i) : quad_range_v2(i);
            if (b[o++] > (ver == 1u && i == QP_RB1 ? QUAD_NRB_V1 - 1u : (uint32_t)(r.max - r.min)))
                return 0;
        }
    }
    if (ver == 6u)
        o += 5u;                                 /* (its five bytes: any value is one) */
    for (i = o; i < (ver >= 4u ? QUAD_BLOB : QUAD_BLOB_V3); i++)
        if (b[i])
            return 0;
    return 1;
}

/* version 1's RATIO B (BR index x 19 + B1 index, B2 = B1 x BR) as B1, B2 and OFS B2: B2 the nearest step to B1 x BR
 * (the lower one when halfway), the rest in OFS B2 (1/100, clamped at +-1.00: exact for the halves, 1.5 = 1 + 0.50) */
static void quad_rb_v1(int32_t v, int16_t *p)
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
    p[QP_RB1] = (int16_t)b1;
    p[QP_RB2] = (int16_t)b2;
    p[QP_OFSB2] = (int16_t)clamp(o, -100, 100);
}

/* FDBK (0..120: the stored 0..127 scaled, quad_render) -> the feedback multiplier: the self-modulation is
 * (y[n-1] + y[n-2]) x fbq << 2 (2^32 = a cycle), i.e. beta = 2 pi fbq / 16384 rad on the average of the last two
 * outputs: 1.9 rad at 35 (a saw: h2 / h1 0.47, h3 / h1 0.29), 7.5 rad at 120 (noise) */
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
static void quad_v2_to_v3(int16_t *p)
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
    p[QP_FDBK] = (int16_t)best;
    p[QP_FTYPE] = (int16_t)FT2[clamp(p[QP_FTYPE], 0, 2)];
    p[QP_HARM] = (int16_t)quad_harm_v2(p[QP_HARM]);
    p[QP_PHRT] = (int16_t)(p[QP_PHRT] ? QR_ALL : QR_OFF);
    p[QP_B2KEY] = p[QP_B1KEY];
    for (k = 0; k < 3; k++) {
        p[QP_LFO(k, QL_SPEED)] = (int16_t)clamp(p[QP_LFO(k, QL_SPEED)], -64, 63);
        p[QP_LFO(k, QL_FADE)] = (int16_t)-(((uint8_t)p[QP_LFO(k, QL_FADE)] + 1) >> 1);
        p[QP_LFO(k, QL_DEST)] = (int16_t)DEST2[clamp(p[QP_LFO(k, QL_DEST)], 0, 12)];
    }
}

/* blob -> patch; 0 or a bad blob: the init patch. 1 = the blob was taken */
static int quad_unpack(const uint8_t *b, int16_t *p)
{
    uint32_t i, np;
    quad_init_patch(p);
    if (!quad_blob_ok(b))
        return 0;
    if (b[1] >= 4u) {
        uint32_t o = 2, v4 = b[1] == 4u;
        for (i = 0; i < QP_NP5; i++) {
            int32_t u = b[o++];
            if (v4 ? quad_hires_v4(i) : quad_hires(i))
                u |= (int32_t)b[o++] << 8;
            p[i] = (int16_t)(u + (v4 ? quad_range_v4(i) : quad_range(i)).min);
            if (v4)                              /* version 4 -> 5 */
                p[i] = quad_clampv(i, quad_v5_of_v4(i, p[i]));
        }
        if (b[1] == 6u) {
            quad_unpack6(b + o, p);
            p[QP_FTYPE] = quad_clampv(QP_FTYPE, p[QP_FTYPE]);   /* (a machine this build has not: its nearest) */
        }
        return 1;
    }
    if (b[1] == 3u) {
        for (i = 0; i < QP_NP5; i++)
            p[i] = (int16_t)(b[2 + i] + quad_range_v3(i).min);
    } else {
        np = b[1] == 1u ? QUAD_NP_V1 : QUAD_NP_V2;   /* version 1 / 2: their values, then converted */
        for (i = 0; i < np; i++)
            if (b[1] != 1u || i != QP_RB1)
                p[i] = (int16_t)(b[2 + i] + quad_range_v2(i).min);
        if (b[1] == 1u)
            quad_rb_v1(b[2 + QP_RB1], p);
        quad_v2_to_v3(p);
    }
    for (i = 0; i < QP_NP5; i++)                 /* version 3's values -> 4's (best effort: x 100, FDBK) -> 5's */
        p[i] = quad_clampv(i, quad_v5_of_v4(i, quad_v4_of_v3(i, p[i])));
    return 1;
}

/* the macros (P_E0..P_E7 = QUAD_MAC's values) from the patch: into the track, remembered (main loop) */
static void quad_macros_out(track_t *t)
{
    uint32_t tr = quad_tr(t), k;
    if (tr >= QUAD_NPART)
        return;
    for (k = 0; k < 8u; k++) {
        int16_t v = quad_mac_val(k, quad_patch[tr][QUAD_MAC[k]]);
        t->p[P_E0 + k] = v;
        quad_mlast[tr][k] = v;
    }
}

/* ------------------------------------------------------------ presets --- */
/* a preset: the init patch with these (index, value) pairs, 0xFF ends. The ratio indices: RC / RA / RB1 / RB2
 * (quad_tables.h; QRC(r): C/B's step of ratio r (x4: 1 = 0.25 .. 4 = 1.00, then 8 = 2.00 ..), QRA(r): A's (x4),
 * QRB(b1, b2): RATIO B's pair index (B1 and B2 x4: QRB(4, 8) = 1.00 / 2.00 = B1 1.00, B2 2.00).
 * Retuned 2026-10-08 for the Digitone routings (docs/QUAD.md "Presets"): MIX -64 = X alone, 63 = Y alone. The times
 * (ATK DEC REL of ENVA ENVB AMP FENV, and preset_t.env) re-expressed 2026-10-14 on the Digitone's tables (quad_times.h):
 * each the value whose new time is nearest the time it had (docs/QUAD.md "Times"), so the presets sound as designed */
#define S8(v) (uint8_t)(int16_t)(v)
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
static const uint8_t QUADP_EP[] = {SYN(2, QRC(4), QRA(4), 4, 56, 0, 12, 0, -30), ENVA(0, 29, 20, 66),
    ENVB(0, 12, 0, 62), QP_VEL, 100, KEY(40, 30, 30), AMP(0, 39, 40, 14, 102), 0xFF};
/* BELL: X = C under A 3.50, Y = B1 2.00 under B2 6.00 (two inharmonic pairs) */
static const uint8_t QUADP_BELL[] = {SYN(2, QRC(4), QRA(14), 8, 24, 0, 20, 0, -10), ENVA(0, 43, 30, 80),
    ENVB(0, 48, 20, 70), QP_VEL, 90, KEY(30, 0, 30), AMP(0, 52, 0, 43, 113), 0xFF};
/* BASS: C under a 1:1 A with feedback (a saw-ish modulator), X only */
static const uint8_t QUADP_BASS[] = {SYN(1, QRC(4), QRA(4), 4, 4, 0, 0, 24, -64), ENVA(0, 18, 34, 88),
    ENVB(0, 11, 0, 0), FLT(LP12, 92, 10, 20), FENV(0, 12, 0, 0), AMP(0, 24, 100, 5, 127), 0xFF};
/* PLUCK: B1 and B2 into A 2.00 into C, X only, everything decaying */
static const uint8_t QUADP_PLUCK[] = {SYN(5, QRC(4), QRA(8), 12, 12, 0, 6, 0, -64), ENVA(0, 14, 0, 84),
    ENVB(0, 7, 0, 64), KEY(40, 40, 40), AMP(0, 21, 0, 13, 102), 0xFF};
/* BRASS: C under a slow-attack 1:1 A with feedback, a little B1 */
static const uint8_t QUADP_BRASS[] = {SYN(1, QRC(4), QRA(4), 4, 4, 0, 0, 28, -64), ENVA(11, 31, 60, 74),
    ENVB(9, 31, 50, 10), FLT(LP12, 80, 12, 30), FENV(9, 48, 60, 12), AMP(22, 24, 110, 13, 107), 0xFF};
/* GLASS PAD: X = C under A 2.00 + A, Y = B1 under B2 + B2 (algorithm 7's enveloped carriers), A / B1 a little saw */
static const uint8_t QUADP_GLASS[] = {SYN(7, QRC(4), QRA(8), 16, 16, 8, 40, 0, 0), ENVA(17, 41, 100, 36),
    ENVB(24, 48, 60, 50), QP_PHRT, QR_OFF, AMP(53, 32, 120, 43, 124), LFO(0, 8, 3, HARM, SINE, 30), 0xFF};
/* HOLLOW: C a soft square (HARM -16) under A 2.00, Y = B1 under a little B2 */
static const uint8_t QUADP_HOLLOW[] = {SYN(2, QRC(4), QRA(8), 4, 4, -16, 0, 0, -20), ENVA(5, 41, 80, 40),
    ENVB(0, 31, 40, 50), AMP(16, 24, 110, 18, 107), 0xFF};
/* SQUARE LEAD: C a square (HARM -19) under a little A 2.00, X only */
static const uint8_t QUADP_SQLEAD[] = {SYN(1, QRC(4), QRA(8), 4, 4, -19, 0, 10, -64), ENVA(0, 31, 60, 36),
    ENVB(0, 22, 20, 0), AMP(0, 18, 110, 9, 127), 0xFF};
/* METAL: B1 7.00 (feedback) and B2 10.50 into A 1.50 into C, A detuned against B2; mostly X */
static const uint8_t QUADP_METAL[] = {SYN(5, QRC(4), QRA(6), 28, 40, 0, 90, 20, -40), QP_OFSB2, 50,
    ENVA(0, 48, 40, 90), ENVB(0, 41, 30, 70), AMP(0, 43, 30, 32, 99), 0xFF};
/* WOBBLE: BASS's voice through LP24, an LFO on A LEV and one on FREQ */
static const uint8_t QUADP_WOBBLE[] = {SYN(1, QRC(4), QRA(4), 4, 4, 0, 0, 10, -64), ENVA(0, 19, 127, 60),
    ENVB(0, 16, 40, 0), FLT(LP24, 70, 60, 0), AMP(0, 24, 110, 7, 127), LFO(0, 32, 4, ALEV, TRI, 50),
    LFO(1, 32, 4, FREQ, TRI, 40), QP_LFO(0, QL_TRIG), QT_TRIG, QP_LFO(1, QL_TRIG), QT_TRIG, 0xFF};
/* CLAV: X = C (HARM -15: 1 + 3) under A 3.00, Y = B1 1.00 under B2 4.00, through HP12 */
static const uint8_t QUADP_CLAV[] = {SYN(2, QRC(4), QRA(12), 4, 16, -15, 0, 0, 0), ENVA(0, 11, 10, 90),
    ENVB(0, 10, 0, 80), FLT(HP12, 30, 20, 0), AMP(0, 18, 0, 8, 127), 0xFF};
/* STRINGS: X = C a saw (HARM -7) + B2 detuned, Y = B1 with feedback 35 (a second saw), both enveloped at B LEV 127;
 * DTUN by an LFO */
static const uint8_t QUADP_STRINGS[] = {SYN(8, QRC(4), QRA(4), 4, 4, -7, 100, 35, 0), ENVA(0, 16, 0, 0),
    ENVB(0, 16, 127, 127), QP_PHRT, QR_OFF, FLT(LP12, 96, 8, 0), AMP(53, 32, 115, 39, 113),
    LFO(0, 6, 3, DTUN, SINE, 20), 0xFF};
/* MARIMBA: C under A 4.00 and B1 10.00 (short), X only */
static const uint8_t QUADP_MARIMBA[] = {SYN(1, QRC(4), QRA(16), 40, 40, 0, 0, 0, -64), ENVA(0, 11, 0, 64),
    ENVB(0, 2, 0, 20), KEY(50, 50, 0), AMP(0, 24, 0, 18, 110), 0xFF};
/* DRONE: C 0.50 and B1 under A (feedback) and B2 1.50, three slow LFOs (HARM, MIX, FDBK) */
static const uint8_t QUADP_DRONE[] = {SYN(6, QRC(2), QRA(4), 4, 4, 0, 50, 15, 0), QP_OFSB2, 50, ENVA(43, 55, 90, 44),
    ENVB(59, 55, 80, 52), QP_PHRT, QR_OFF, AMP(44, 43, 127, 58, 105), LFO(0, 4, 3, HARM, SINE, 40),
    LFO(1, 5, 3, MIX, TRI, 50), LFO(2, 3, 3, FDBK, SINE, 30), 0xFF};
/* FEEDBACK: Y = B1 with heavy feedback (enveloped: B LEV 43 = B1 full, B2 off), X = C under A */
static const uint8_t QUADP_FEEDBACK[] = {SYN(8, QRC(4), QRA(4), 4, 4, 0, 0, 42, 30), ENVA(0, 31, 40, 60),
    ENVB(0, 31, 100, 43), AMP(0, 32, 100, 18, 102), 0xFF};
/* NOISE-ISH: algorithm 7's A at full feedback, X = C under it + itself, through a resonant HP12 */
static const uint8_t QUADP_NOISE[] = {SYN(7, QRC(4), QRA(4), 4, 4, 0, 0, 120, -64), ENVA(0, 16, 127, 100),
    ENVB(0, 16, 0, 0), FLT(HP12, 70, 40, 0), AMP(0, 32, 60, 24, 88), 0xFF};

/* {ALGO, RATIO C, RATIO A, RATIO B (B2's step), HARM, DTUNE, FDBK, MIX}: the macros as the patch has them
 * (cr_quad_test checks); env: the amp envelope (QUAD's own: the platform ADSR is not used). FX(drive, chorus, delay,
 * reverb): the DRIVE is loaded with the preset (the AMP page's cell; WOBBLE's 10 is the only one above 0); the three
 * sends are not: they are the part's (cr_bank.c param_kept) */
static const preset_t QUAD_PRESETS[] = {
    {"EP", {2, QRC(4), QRA(4), QRC(56), 0, 12, 0, -30}, {0, 39, 40, 14}, 0, 0, FX(0, 30, 20, 40), PAT(6)},
    {"BELL", {2, QRC(4), QRA(14), QRC(24), 0, 20, 0, -10}, {0, 52, 0, 43}, 0, 0, FX(0, 20, 35, 60), PAT(7)},
    {"BASS", {1, QRC(4), QRA(4), QRC(4), 0, 0, 24, -64}, {0, 24, 100, 5}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"PLUCK", {5, QRC(4), QRA(8), QRC(12), 0, 6, 0, -64}, {0, 21, 0, 13}, 0, 0, FX(0, 20, 30, 40), PAT(6)},
    {"BRASS", {1, QRC(4), QRA(4), QRC(4), 0, 0, 28, -64}, {22, 24, 110, 13}, 0, 0, FX(0, 25, 20, 40), PAT(6)},
    {"GLASS PAD", {7, QRC(4), QRA(8), QRC(16), 8, 40, 0, 0}, {53, 32, 120, 43}, 0, 0, FX(0, 50, 25, 70), PAT(5)},
    {"HOLLOW", {2, QRC(4), QRA(8), QRC(4), -16, 0, 0, -20}, {16, 24, 110, 18}, 0, 0, FX(0, 35, 25, 50), PAT(6)},
    {"SQUARE LEAD", {1, QRC(4), QRA(8), QRC(4), -19, 0, 10, -64}, {0, 18, 110, 9}, 0, 1, FX(0, 25, 40, 40), PAT(4)},
    {"METAL", {5, QRC(4), QRA(6), QRC(40), 0, 90, 20, -40}, {0, 43, 30, 32}, 0, 0, FX(0, 20, 30, 50), PAT(7)},
    {"WOBBLE", {1, QRC(4), QRA(4), QRC(4), 0, 0, 10, -64}, {0, 24, 110, 7}, 0, 1, FX(10, 0, 15, 10), PAT(2)},
    {"CLAV", {2, QRC(4), QRA(12), QRC(16), -15, 0, 0, 0}, {0, 18, 0, 8}, 0, 0, FX(0, 15, 20, 25), PAT(6)},
    {"STRINGS", {8, QRC(4), QRA(4), QRC(4), -7, 100, 35, 0}, {53, 32, 115, 39}, 0, 0, FX(0, 55, 15, 65), PAT(5)},
    {"MARIMBA", {1, QRC(4), QRA(16), QRC(40), 0, 0, 0, -64}, {0, 24, 0, 18}, 0, 0, FX(0, 15, 25, 40), PAT(7)},
    {"DRONE", {6, QRC(2), QRA(4), QRC(4), 0, 50, 15, 0}, {44, 43, 127, 58}, 0, 0, FX(0, 50, 30, 80), PAT(5)},
    {"FEEDBACK", {8, QRC(4), QRA(4), QRC(4), 0, 0, 42, 30}, {0, 32, 100, 18}, 0, 0, FX(0, 25, 30, 40), PAT(4)},
    {"NOISE-ISH", {7, QRC(4), QRA(4), QRC(4), 0, 0, 120, -64}, {0, 32, 60, 24}, 0, 0, FX(0, 20, 30, 50), PAT(7)},
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

static void quad_preset_patch(uint32_t k, int16_t *p)
{
    const uint8_t *e;
    quad_init_patch(p);
    if (k >= QUAD_NPRESETS)
        return;
    for (e = QUAD_PRESET_EDITS[k]; *e != 0xFFu; e += 2)
        p[e[0]] = quad_clampv(e[0], quad_v5_of_v4(e[0], quad_v4_of_v3(e[0], (int8_t)e[1])));   /* (the tables: version 3's values) */
}

/* ------------------------------------------------- deep pages, the blob --- */
static uint32_t quad_index(uint32_t page, uint32_t col)
{
    return page >= QUAD_NPAGES || col >= 4u ? QUAD_X : QUAD_MAP[page][col];
}
/* FILTER+ by the filter's type (3y217, the owner 2026-11-01: "For the new filters you should not be able to use that
 * third param slot to change the filter type"; FIZZ's FILTER page by machine, ctl_snd.c SND_FILTER_IDS): OFF LP12 HP12
 * LP24 FREQ RESO TYPE DEPTH, as before; MULTI FREQ RESO MORPH DEPTH; COMB+ / COMB- FREQ FEEDBACK (RESO) LPF DEPTH; EQ
 * FREQ GAIN Q DEPTH (MORPH, LPF, Q: SHAPE; GAIN: FGAIN). A machine's TYPE is the SEL hold picker's alone (cr_edit.c
 * ce_fpick), which asks for the page's own column (ENG_COL_RAW). FILT 2's SHAPE and GAIN stay where they were */
static uint32_t quad_ix(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = quad_tr(t), i = quad_index(page, col & ~ENG_COL_RAW), ft;
    if (page != QUAD_PG_FPLUS || (col & ENG_COL_RAW) || tr >= QUAD_NPART || (ft = (uint32_t)quad_patch[tr][QP_FTYPE]) < QF_MULTI)
        return i;
    return col == 2u ? QP_FSHAPE : col == 1u && ft == QF_EQ ? QP_FGAIN : i;
}
/* (eng_deep_t.desc: FILTER+'s cells by the type, quad_ix; 0 the page's own). The classic types' TYPE cell steps OFF ..
 * LP24 only, as FIZZ's LEGACY KNOB 3: a knob never lands on a machine (its cell would then be another value, no way
 * back by that knob); the machines are the picker's */
static const param_desc_t QUAD_FMACH[6] = {
    {"TYPE", F_ENUM, 0, QF_LP24, QF_LP12, N_QUAD_FTYPE, 0}, {"MORPH", F_INT, 0, 127, 0, 0, 0},
    QC_FX("FEEDBACK", 0, 12700, 0), {"LPF", F_INT, 0, 127, 0, 0, 0}, {"GAIN", F_OFS, -64, 63, 0, 0, 0},
    {"Q", F_INT, 0, 127, 0, 0, 0}};
static const param_desc_t *quad_desc(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = quad_tr(t), ft;
    if (page != QUAD_PG_FPLUS || (col != 1u && col != 2u) || tr >= QUAD_NPART)
        return 0;
    ft = (uint32_t)quad_patch[tr][QP_FTYPE];
    if (ft < QF_MULTI || (col == 1u && ft == QF_MULTI))
        return col == 2u ? &QUAD_FMACH[0] : 0;
    return &QUAD_FMACH[ft == QF_MULTI ? 1 : ft == QF_EQ ? 3 + col : 1 + col];
}

static int32_t quad_get(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = quad_tr(t), i = quad_ix(t, page, col);
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
    t->p[P_E0 + k] = quad_mac_val((uint32_t)k, quad_patch[tr][i]);
    RING_PUBLISH();
    quad_mlast[tr][k] = t->p[P_E0 + k];
}

static void quad_set(track_t *t, uint32_t page, uint32_t col, int32_t v)
{
    uint32_t tr = quad_tr(t), i = quad_ix(t, page, col);
    col &= 3u;
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
        quad_patch[tr][QP_RB1] = (int16_t)(v / (int32_t)QUAD_NRCB);
        quad_patch[tr][QP_RB2] = (int16_t)(v % (int32_t)QUAD_NRCB);
        quad_mac_put(t, tr, QP_RB2);
        return;
    }
    quad_patch[tr][i] = quad_clampv(i, v);
    quad_mac_put(t, tr, i);
}

static void quad_blob_get(const track_t *t, uint8_t *out)
{
    uint32_t tr = quad_tr(t);
    int16_t p[QP_NP];
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
    int16_t p[QP_NP];
    if (tr >= QUAD_NPART)
        return;
    quad_unpack(in, p);
    memcpy(quad_patch[tr], p, sizeof p);
    quad_macros_out(t);
}

static void quad_blob_preset(track_t *t, uint32_t k)
{
    uint32_t tr = quad_tr(t);
    int16_t p[QP_NP];
    if (tr >= QUAD_NPART)
        return;
    quad_preset_patch(k, p);
    memcpy(quad_patch[tr], p, sizeof p);
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
    int16_t p[QP_NP];
    int32_t rb = -1;
    quad_user_pending = 0;
    if (tr >= QUAD_NPART || t->eng_req != ENGI_QUAD)
        return;
    if (t->p[P_E3] >= (int32_t)QUAD_NRCB && t->p[P_E3] < (int32_t)QUAD_NRB_V1)   /* a version-1 RATIO B macro (a
                                                  * project or a record from before: BR x 19 + B1, BR > 0.5) */
        rb = t->p[P_E3];
    memset(b, 0, sizeof b);                      /* (a version 1..3 blob fills QUAD_BLOB_V3 of it) */
    if (pend && quad_store_read && !quad_store_read(pend - 1u, b) && quad_unpack(b, p)) {
        memcpy(quad_patch[tr], p, sizeof p);
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
            p[QUAD_MAC[k]] = quad_mac_patch(k, t->p[P_E0 + k]);
    if (rb >= 0)                                 /* (B1, B2 and OFS B2 as a version-1 blob's) */
        quad_rb_v1(rb, p);
    memcpy(quad_patch[tr], p, sizeof p);
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
 * fixed set at 120 BPM); negative runs backwards, 0 stops. SPEED in hundredths (-6400..6399), exact in 64 bits */
static int32_t quad_lfo_incv(int32_t sp, uint32_t mu)
{
    int32_t bpm;
    uint64_t v;
    mu %= QUAD_NMULT;
    bpm = mu < 12u ? clamp(song.g[G_BPM], 20, 300) : 120;
    /* the magnitude (a signed clamp of the quotient made the compiler load a constant tools/build.py takes for a ROM
     * address) */
    v = ((uint64_t)(uint32_t)(sp < 0 ? -sp : sp) << (mu % 12u)) * (uint32_t)bpm * QUAD_LFO_K / (100u * 65536u);
    v = v > 0x7FFFFFFFu ? 0x7FFFFFFFu : v;
    return sp < 0 ? -(int32_t)v : (int32_t)v;
}
static int32_t quad_lfo_inc(const int16_t *p, uint32_t k)
{
    return quad_lfo_incv(p[QP_LFO(k, QL_SPEED)], (uint32_t)p[QP_LFO(k, QL_MULT)]);
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

static void quad_comb_lend(const track_t *t, uint32_t tr, const int16_t *p);   /* (the combs' lines, below) */
static uint8_t quad_comb_was[QUAD_NPART];        /* the part's FTYPE was a comb in its last block (quad_block) */
/* the part's DRIVE PRE for this block at drive d (0: off; the LFOs' DRIVE in it: the last block's, quad_dro) */
static void quad_drv_set(quad_drv_t *q, int32_t d)
{
    int32_t i, mk, b0;
    q->on = d != 0;
    if (!d || d == q->d)
        return;
    q->d = (uint8_t)d;
    q->g = 2048 + d * d, q->k = 32000 - d * 95, mk = 30000 - d * 120;   /* (dsp_dist's: g / 2, k, mk) */
    b0 = softclip(QUAD_DRV_BIAS);
    for (i = 0; i <= 256; i++) {
        q->tab[256 + i] = mulq15(TANH_Q15[i] - b0, mk);
        q->tab[256 - i] = mulq15(-TANH_Q15[i] - b0, mk);
    }
    q->tab[513] = q->tab[512];
    /* u = ((e x g) >> 11) + bias in -65536 .. 65536 (e x g: under 2^31 within them) */
    q->ehi = ((65536 - QUAD_DRV_BIAS) * 2048 + 2047) / q->g;
    q->elo = -((65536 + QUAD_DRV_BIAS) * 2048 / q->g);
}
/* once a block and part: the macros into the patch, the FREE LFOs' phases */
static void quad_block(track_t *t)
{
    uint32_t tr = quad_tr(t), k;
    int16_t *p;
    if (tr >= QUAD_NPART)
        return;
    p = quad_patch[tr];
    quad_dro[tr] = quad_fx_off[tr][0];
    quad_drv_set(&quad_drv[tr], p[QP_DRPRE] ? clamp(t->p[P_DIST] + quad_dro[tr], 0, 127) : 0);
    k = p[QP_FTYPE] == QF_COMBP || p[QP_FTYPE] == QF_COMBN;
    if (k || quad_comb_was[tr])                  /* (the lines: while a comb runs, and once after, to give them
                                                  * back; a part on no comb holds none) */
        quad_comb_lend(t, tr, p);
    quad_comb_was[tr] = (uint8_t)k;
    for (k = 0; k < 8u; k++)                     /* a knob, the editor, the matrix, motion moved a macro */
        if (t->p[P_E0 + k] != quad_mlast[tr][k]) {
            quad_mlast[tr][k] = t->p[P_E0 + k];
            p[QUAD_MAC[k]] = quad_mac_patch(k, t->p[P_E0 + k]);
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
static void quad_lfo_on(track_t *t, quad_voice_t *s, const int16_t *p)
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
    s->lof = 0;
    s->ticks = 0;
}

/* PHRT's operators (mask) that may restart at 0 in voice s, which sounds: those no one hears. An operator restarted
 * under the voice's level stepped its wave (the pops of docs/QUAD.md "Pops": a carrier's step, or a modulator's in the
 * carrier's phase); one that is silent restarts as on a fresh note, so its attack is the patch's (a modulator that
 * starts at an arbitrary phase as its level leaps up in a tick bursts) */
static uint32_t quad_phrt_mask(const quad_voice_t *s, const int16_t *p, uint32_t mask)
{
    const quad_algo_t *al = &QUAD_ALGO[(uint32_t)clamp(p[QP_ALGO], 1, 8) - 1u];
    int32_t a = s->amp < 0 ? -s->amp : s->amp;
    uint32_t k, out = al->x | al->y, mods = al->mod[0] | al->mod[1] | al->mod[2] | al->mod[3];
    for (k = 0; k < 4u; k++) {
        int32_t lv = k ? s->lv[k - 1u] : 32767, heard = 0;
        lv = lv < 0 ? -lv : lv;
        if ((out >> k) & 1u)                     /* a carrier: direct (C always), or at its envelope x level */
            heard = ((al->env >> k) & 1u ? (int32_t)(((int64_t)a * lv) >> 15) : a) >= QUAD_PHRT_QUIET;
        if (((mods >> k) & 1u || k == al->fb) && lv >= QUAD_PHRT_QMOD && a >= QUAD_PHRT_QUIET)
            heard = 1;                           /* a modulator (or the feedback operator) of a voice that sounds */
        if (heard)
            mask &= ~(1u << k);
    }
    return mask;
}

CR_HOT_NOTE static void quad_note_on(track_t *t, voice_t *v)
{
    quad_voice_t *s = quad_voice(t, v);
    const int16_t *p;
    uint32_t k, fresh, mask;
    if (!s)
        return;
    p = quad_patch[quad_tr(t)];
    fresh = (!v->env && !v->env_out) || !s->live;
    mask = QUAD_PHRT_MASK[(uint32_t)p[QP_PHRT] % QR_N];
    if (!fresh)
        mask = quad_phrt_mask(s, p, mask);
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
        s->dh = s->dl1 = s->dl2 = 0;
        s->crph = 0, s->crheld = 0;
        s->cfresh = 1;                           /* (a comb from rest) */
        s->fon = QUAD_FON_NEW;
        s->mixp = QUAD_MIX_NEW;
        quad_env_start(s, 0, p[QP_ADLY], 1);
        quad_env_start(s, 1, p[QP_BDLY], 1);
    } else {                                     /* a retrigger: TRIG restarts the operator envelopes (RESET: from
                                                  * 0, else from where they are) */
        if ((mask >> QUAD_ALGO[(uint32_t)clamp(p[QP_ALGO], 1, 8) - 1u].fb) & 1u)
            s->fb1 = s->fb2 = 0;                 /* (the feedback operator restarted: its history with it) */
        if (p[QP_ATRIG])
            quad_env_start(s, 0, p[QP_ADLY], p[QP_ARST]);
        if (p[QP_BTRIG])
            quad_env_start(s, 1, p[QP_BDLY], p[QP_BRST]);
    }
    s->fsm = p[QP_FREQ], s->rsm = p[QP_RESO];    /* (a note at its own FREQ and RESO: no glide into it) */
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
    const int16_t *p;
    if (!s)
        return;
    p = quad_patch[quad_tr(t)];
    if (p[QP_ATRIG])
        quad_env_start(s, 0, p[QP_ADLY], p[QP_ARST]);
    if (p[QP_BTRIG])
        quad_env_start(s, 1, p[QP_BDLY], p[QP_BRST]);
}

/* the time tables of envelope k (0 operator A, 1 operator B, 2 filter, 3 amp; quad_times.h, the Digitone's): the
 * attack's step a tick (the operators': the level's growth a tick), the decay's (and the release's) share of the way
 * a tick, all Q24 */
static const uint32_t *const QUAD_TATK[4] = {QUAD_T_OATK, QUAD_T_OATK, QUAD_T_FATK, QUAD_T_AATK};
static const uint32_t *const QUAD_TDEC[4] = {QUAD_T_ODEC, QUAD_T_ODEC, QUAD_T_FDEC, QUAD_T_ADEC};

/* one control tick of an envelope (Q24 x, stage st, delay progress dp): DELAY (dly / 32 beats at the part's tempo,
 * the Digitone's: dp counts 1/32 beats in Q24), then ATK (the filter's and the amp's linear to 1, the operators'
 * linear in dB from QUAD_OP_FLOOR: the Digitone's centroid tracks; a restart without RESET goes on from the level it
 * is at, at least the floor), then DEC (exponential to sus, Q24; the amp
 * envelope's DEC 127 = INF: the level held where the decay starts, no decay, the Digitone's), held; adsr: the gate's
 * end releases (REL, exponential to 0 on the decay's table, then off; REL 127 = INF: the level held, stage 5, until
 * REL (modulated) leaves 127, a steal or a panic: the Digitone's 0..126 + INF). The step of a release is rounded up,
 * so a long one's (a share < 1 / 4096) reaches the end at 2^12 too. Q15 */
static int32_t quad_env_tick(quad_voice_t *s, uint32_t k, int32_t atk, int32_t dec, int32_t sus, int32_t rel,
                             int32_t dly, int gate, int adsr)
{
    int32_t x = s->env[k];                       /* (rel: 0..126, 127 = INF) */
    if (adsr && !gate && s->stage[k] && s->stage[k] < 4u)
        s->stage[k] = 4;
    switch (s->stage[k]) {
    case 1:
        s->dly[k] += (uint32_t)clamp(song.g[G_BPM], 20, 300) * QUAD_DLY_K;
        if (s->dly[k] >= (uint32_t)(dly & 127) << 24)
            s->stage[k] = 2;
        break;
    case 2:
        if (k < 2u) {                            /* the operators: linear in dB from QUAD_OP_FLOOR (-38 dB) to the
                                                  * top, the growth a tick (the Digitone's late bloom) */
            x = x < QUAD_OP_FLOOR ? QUAD_OP_FLOOR : x;
            x += (int32_t)(((int64_t)x * QUAD_TATK[k][atk & 127]) >> 24);
        } else {
            x += (int32_t)QUAD_TATK[k][atk & 127];
        }
        if (x >= (1 << 24)) {
            x = 1 << 24;
            s->stage[k] = 3;
        }
        break;
    case 3:
        if (k == 3u && dec >= 127)               /* AMP DEC INF: held */
            break;
        x += (int32_t)(((int64_t)(sus - x) * QUAD_TDEC[k][dec & 127]) >> 24);
        break;
    case 4:
    case 5:
        if (rel >= 127) {                        /* INF: held */
            s->stage[k] = 5;
            break;
        }
        s->stage[k] = 4;
        x -= (int32_t)(((int64_t)x * QUAD_TDEC[k][rel] + 0xFFFFFF) >> 24);
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

/* AHD (version 6's AMOD 1, FIZZ's E1 fz_ahd_step): the amp envelope's ATK to 1 (the amp attack's table), a HOLD (a
 * time on the same table: dly[3] its progress, the amp has no DELAY; 127 NOTE: while the key is down), then DEC to 0
 * (the amp decay's table; 127 INF: held) and the voice ends; SUS and REL unread, the key's end unread in a timed HOLD
 * (one-shot). Stages 6 hold, 7 decay; ADSR's met at a switch mid-note (its decay / sustain the hold, its release the
 * decay; back to ADSR: 6 -> 3, 7 -> 4). Q15 */
static int32_t quad_ahd(quad_voice_t *s, int32_t atk, int32_t hold, int32_t dec, int gate)
{
    int32_t x = s->env[3];
    if (s->stage[3] == 3u)
        s->stage[3] = 6, s->dly[3] = 0;
    else if (s->stage[3] == 1u)
        s->stage[3] = 2, s->dly[3] = 0;
    else if (s->stage[3] == 4u || s->stage[3] == 5u)
        s->stage[3] = 7;
    switch (s->stage[3]) {
    case 2:
        x += (int32_t)QUAD_T_AATK[atk & 127];
        if (x >= (1 << 24)) {
            x = 1 << 24;
            s->stage[3] = 6, s->dly[3] = 0;
        }
        break;
    case 6:
        if (hold >= 127) {
            if (!gate)
                s->stage[3] = 7;
        } else {
            s->dly[3] += QUAD_T_AATK[hold & 127];
            if (s->dly[3] >= (1u << 24))
                s->stage[3] = 7;
        }
        break;
    case 7:
        if (dec < 127) {
            x -= (int32_t)(((int64_t)x * QUAD_T_ADEC[dec & 127] + 0xFFFFFF) >> 24);
            if (x < 4096) {
                x = 0;
                s->stage[3] = 0;
            }
        }
        break;
    default:
        x = 0;
        break;
    }
    s->env[3] = x;
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

/* the voice would sound on after its release (engine_t.endless: trk_all_off kills it): AMP REL at INF (the patch's, or
 * the release already held by a modulated 127) */
static int quad_endless(const track_t *t, const voice_t *v)
{
    uint32_t tr = quad_tr(t), i = (uint32_t)(v - t->v);
    const quad_voice_t *s = tr < QUAD_NPART && i < QUAD_POLY ? &quad_vs[tr][i] : 0;
    if (s && quad_patch[tr][QP_AMOD])            /* (AHD: its DEC at INF holds the level for ever) */
        return s->live && s->stage[3] && quad_patch[tr][QP_EDEC] >= 127;
    return s && s->live && s->stage[3] && (quad_patch[tr][QP_EREL] >= 127 || s->stage[3] == 5u);
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

/* QUAD_BW_K at x hundredths (0..12700), interpolated (a whole number: its entry, no read past the table at 127.00:
 * an LFO on BASE takes it there; FIZZ's QUADRILLE (d), the same values) */
static int32_t quad_bwk(int32_t x)
{
    int32_t i = x / 100, k0 = QUAD_BW_K[i];
    if (!(x % 100))
        return k0;
    return k0 + (((int32_t)QUAD_BW_K[i + 1] - k0) * (x % 100)) / 100;
}

/* a value gliding to its target: a quarter of the way a block, the last 7 at once (FIZZ's P2.6b: FREQ and RESO turned
 * on a sounding voice reach the new value over ~7 ms for a detent, not in one 32-sample step: no zipper, no click at
 * high resonance; a note whose FREQ and RESO stay put renders as before, sample for sample) */
static int32_t quad_glide_v(int32_t cur, int32_t tgt)
{
    int32_t d = tgt - cur;
    return d > -8 && d < 8 ? tgt : cur + d / 4;
}

/* the HARM wave: tables a and b (HARM's neighbours, one index), b's share f (Q15). The tables are the stored first
 * halves (quad_tables.h QUAD_HARM: the points 0 .. 256 of 512; every wave is a sum of sines, so f(512 - i) = -f(i)
 * exactly, tests/quad_ref.py checks every point), read from flash with a mirror: in the second half (s = -1) the
 * pair of points around the phase is the pair at u = 511 - i, u + 1 of the first, swapped and negated, so the slope
 * h - l is the same and the start is -h (l - ((l + h) & s), branchless): bit for bit the whole cycle's values
 * (cr_quad_test checks every point). The whole cycles were built into POOL at power-on (3y126, 27.7 KB) until 3y150 */
static inline int32_t quad_hw(const int16_t *a, const int16_t *b, int32_t f, uint32_t ph)
{
    int32_t s = (int32_t)ph >> 31, fr = (int32_t)((ph >> 8) & 0x7FFFu), l, h, x, y;
    uint32_t u = (ph ^ (uint32_t)s) >> 23;
    l = a[u], h = a[u + 1u];
    x = l - ((l + h) & s) + (((h - l) * fr) >> 15);
    if (!f)
        return x;
    l = b[u], h = b[u + 1u];
    y = l - ((l + h) & s) + (((h - l) * fr) >> 15);
    return x + (((y - x) * f) >> 15);
}

/* the DC blocker after the operators: a one-pole high-pass at ~8 Hz (Q16: 65536 (1 - exp(-2 pi 8 / FS))). The
 * operators are not DC-free: the DX7-style feedback (its average lags 1.5 samples) skews the feedback operator's saw,
 * and at near-unison ratios (1:1) a modulator's phase offset (that mean, or DTUNE's slow drift) puts J1(I) sin(offset)
 * into the carrier at 0 Hz. A DX7 / Digitone AC-couples its output; here every voice does, before the filters */
#define QUAD_DC_K 75

/* the multimode filter's coefficients (dsp.c tsvf_coef_k's, cut Q8, damping k Q12) with a2 to 1/32 of its Q13 step
 * (f2: the fraction, 0..31): FREQ in hundredths moves g by 0.05 % a hundredth, below a Q13 step where g is small
 * (FREQ 42: a2 ~ 116), so the fine part would not reach the sound; g is interpolated in Q20. (a3 = a2 g, a3's own
 * fraction: not worth its multiply a sample) */
typedef struct { int32_t a1, a2, a3, f2; } quad_svf_t;
static void quad_svf_coef(quad_svf_t *c, int32_t cut, int32_t k)
{
    int32_t i, g, den;
    int64_t a2, a3;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = (int32_t)SVF_G[i] << 8;                  /* Q20 */
    if (i < 127)
        g += ((int32_t)SVF_G[i + 1] - (int32_t)SVF_G[i]) * (cut & 255);
    den = 4096 + (int32_t)(((int64_t)g * (g + ((int64_t)k << 8))) >> 28);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    a2 = ((int64_t)c->a1 * g) >> 15;             /* Q18 */
    a3 = (a2 * g) >> 20;
    c->a2 = (int32_t)(a2 >> 5), c->f2 = (int32_t)(a2 & 31);
    c->a3 = (int32_t)(a3 >> 5);
}

/* ---------------------------------------- the filter machines (version 6) --- */
/* FIZZ's E2 (FMToneSeq quad_ext.c, the owner's; research/FMTONE-DN2-PLAN.md section 4.3), FTYPE 4..7 in the place of
 * the multimode SVF, the soft knee after it as the SVF's: MULTI (the SVF's LP, BP, HP morphed by SHAPE: 0 LP12, 64
 * BP, 127 HP12, each to the LSB), EQ (a peaking bell from the SVF: FREQ its centre, SHAPE its Q 0.5 .. 10, GAIN -18 ..
 * +17.7 dB), COMB+ / COMB- (3y208). The EQ's damping k = 1 / Q (Q12) by SHAPE, its gain 10^(dB / 20) - 1 (Q12) by
 * GAIN + 64 */
static const uint16_t QE_EQ_K[128] = {
    8192, 8001, 7815, 7632, 7454, 7281, 7111, 6945, 6783, 6625, 6471, 6320,
    6172, 6029, 5888, 5751, 5617, 5486, 5358, 5233, 5111, 4992, 4875, 4762,
    4651, 4542, 4436, 4333, 4232, 4133, 4037, 3943, 3851, 3761, 3674, 3588,
    3504, 3423, 3343, 3265, 3189, 3114, 3042, 2971, 2902, 2834, 2768, 2703,
    2640, 2579, 2519, 2460, 2403, 2347, 2292, 2238, 2186, 2135, 2086, 2037,
    1989, 1943, 1898, 1854, 1810, 1768, 1727, 1687, 1647, 1609, 1571, 1535,
    1499, 1464, 1430, 1397, 1364, 1332, 1301, 1271, 1241, 1212, 1184, 1156,
    1129, 1103, 1077, 1052, 1028, 1004, 980, 958, 935, 913, 892, 871,
    851, 831, 812, 793, 774, 756, 739, 721, 705, 688, 672, 657,
    641, 626, 612, 597, 583, 570, 557, 544, 531, 519, 506, 495,
    483, 472, 461, 450, 440, 429, 419, 410,
};
static const int16_t QE_EQ_G[128] = {
    -3580, -3563, -3546, -3528, -3509, -3490, -3470, -3449, -3428, -3406, -3383, -3360,
    -3335, -3310, -3285, -3258, -3230, -3202, -3172, -3142, -3111, -3078, -3045, -3010,
    -2974, -2937, -2899, -2860, -2819, -2777, -2734, -2689, -2643, -2595, -2545, -2494,
    -2442, -2387, -2331, -2273, -2213, -2151, -2087, -2021, -1953, -1882, -1809, -1734,
    -1656, -1576, -1493, -1407, -1319, -1227, -1133, -1035, -935, -831, -723, -612,
    -498, -379, -257, -131, 0, 135, 274, 418, 566, 720, 878, 1042,
    1211, 1386, 1566, 1753, 1945, 2144, 2349, 2561, 2780, 3007, 3240, 3482,
    3731, 3989, 4255, 4530, 4814, 5107, 5410, 5723, 6046, 6379, 6724, 7080,
    7448, 7828, 8220, 8626, 9044, 9477, 9924, 10385, 10862, 11354, 11862, 12387,
    12930, 13490, 14069, 14667, 15284, 15922, 16581, 17261, 17964, 18690, 19440, 20215,
    21015, 21841, 22695, 23576, 24487, 25428, 26399, 27403,
};

/* the SVF's one sample (QUAD_SVF's arithmetic: the integrators ic1 ic2, the band v1, the low v2) */
static inline void quad_svf_tick(const quad_svf_t *c, int32_t *ic1, int32_t *ic2, int32_t x, int32_t *v1, int32_t *v2)
{
    int32_t a = *ic1, b = *ic2, v3 = x - b;
    *v1 = (c->a1 * a + c->a2 * v3 + ((c->f2 * v3) >> 5) + 4096) >> 13;
    *v2 = b + ((c->a2 * a + c->a3 * v3 + ((c->f2 * a) >> 5) + 4096) >> 13);
    *ic1 = clamp(2 * *v1 - a, -150000, 150000);
    *ic2 = clamp(2 * *v2 - b, -150000, 150000);
}
/* COMB+ / COMB- (3y208, FIZZ's E2): a 1024-sample line a voice, FREQ its pitch (the length QE_COMB_LEN by the cut,
 * Q8 samples: ChoralRoot's cutoff law, 30 Hz .. 16 kHz, at most 1022: 43 Hz the lowest), RESO its feedback (0 .. 0.985),
 * SHAPE the damping of what it feeds back; y = x +- g lp(d[n - L]) into the line, out y (1 - g / 2). The lines are
 * LENT, as FIZZ's device build lends them: QUAD_NCOMB (8, 16 KB of POOL) for the two parts' 16 voices; a voice that
 * starts a comb (or is switched to one) takes a free line, and keeps it while it runs a comb and sounds; none free:
 * that voice runs MULTI (FIZZ's rule). quad_block lends and takes back, on the part's own core before its voices (the
 * two parts' blocks never run at once: part 0's is before the hand-over, part 1's in core 1's job), so the voices of a
 * chord on two cores only use the line they were given */
static const uint32_t QE_COMB_LEN[128] = {
    261632, 261632, 261632, 261632, 261632, 261632, 261632, 261632, 253383, 241160, 229527, 218454,
    207916, 197886, 188340, 179255, 170608, 162378, 154544, 147089, 139994, 133240, 126813, 120696,
    114873, 109332, 104058, 99038, 94260, 89713, 85386, 81267, 77346, 73615, 70064, 66684,
    63467, 60406, 57492, 54718, 52079, 49566, 47175, 44900, 42734, 40672, 38710, 36843,
    35066, 33374, 31764, 30232, 28773, 27385, 26064, 24807, 23610, 22471, 21387, 20356,
    19374, 18439, 17550, 16703, 15897, 15130, 14400, 13706, 13045, 12415, 11816, 11246,
    10704, 10188, 9696, 9228, 8783, 8359, 7956, 7572, 7207, 6859, 6529, 6214,
    5914, 5629, 5357, 5099, 4853, 4619, 4396, 4184, 3982, 3790, 3607, 3433,
    3267, 3110, 2960, 2817, 2681, 2552, 2429, 2312, 2200, 2094, 1993, 1897,
    1805, 1718, 1635, 1556, 1481, 1410, 1342, 1277, 1216, 1157, 1101, 1048,
    997, 949, 903, 860, 818, 779, 741, 706,
};
#define QUAD_NCOMB 8u
#define QUAD_COMB_N 1024u
static int16_t qe_comb[QUAD_NCOMB][QUAD_COMB_N] __attribute__((section(".pool")));
static uint8_t qe_by[QUAD_NCOMB];                /* the voice holding line k: part x QUAD_POLY + voice + 1, 0 free */
static int32_t qe_comb_len(int32_t cut)
{
    int32_t i;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    if (i >= 127)
        return (int32_t)QE_COMB_LEN[127];
    return (int32_t)QE_COMB_LEN[i] + (((int32_t)QE_COMB_LEN[i + 1] - (int32_t)QE_COMB_LEN[i]) * (cut & 255)) / 256;
}
/* quad_block: part tr's voices that run a comb and sound get a line (their own place's first, then any free), the
 * others give theirs back. Called while the part's TYPE is a comb and the block after (3y214: not in every block of
 * every FM TONE part; out of quad_block's code: tests/target_budget.py) */
static __attribute__((noinline)) void quad_comb_lend(const track_t *t, uint32_t tr, const int16_t *p)
{
    int comb = p[QP_FTYPE] == QF_COMBP || p[QP_FTYPE] == QF_COMBN;
    uint32_t i, k;
    for (k = 0; k < QUAD_NCOMB; k++)             /* (lines held by a part no longer on FM TONE: free) */
        if (qe_by[k] && (qe_by[k] - 1u) / QUAD_POLY != tr && trk[(qe_by[k] - 1u) / QUAD_POLY].engine != ENGI_QUAD)
            qe_by[k] = 0;
    for (i = 0; i < QUAD_POLY; i++) {
        quad_voice_t *s = &quad_vs[tr][i];
        uint8_t id = (uint8_t)(tr * QUAD_POLY + i + 1u);
        int want = comb && s->live && t->v[i].active;
        if (s->cline && (!want || qe_by[s->cline - 1u] != id)) {
            if (qe_by[s->cline - 1u] == id)
                qe_by[s->cline - 1u] = 0;
            s->cline = 0;
        }
        if (want && !s->cline)
            for (k = 0; k < QUAD_NCOMB; k++) {
                uint32_t at = (id - 1u + k) % QUAD_NCOMB;
                if (!qe_by[at]) {
                    qe_by[at] = id;
                    s->cline = (uint8_t)(at + 1u);
                    s->cfresh = 1;
                    break;
                }
            }
    }
}
/* the comb on n samples of acc (the voice's line s->cline) */
static void quad_comb(quad_voice_t *s, int neg, int32_t *acc, uint32_t n, int32_t cut, int32_t reso, int32_t shape)
{
    int16_t *line = qe_comb[s->cline - 1u];
    const int32_t g = clamp(reso, 0, 12700) * 32276 / 12700;   /* (0 .. 0.985, Q15) */
    const int32_t ka = 1024 + shape * 31744 / 127, outk = 32768 - g / 2;
    int32_t l1 = qe_comb_len(cut), l0, dl, lp;
    uint32_t i, w;
    if (s->cfresh) {                             /* (from rest: a fresh note, a line just lent) */
        memset(line, 0, sizeof qe_comb[0]);
        s->cw = 0, s->clp = 0, s->clen = l1, s->cfresh = 0;
    }
    l0 = s->clen, dl = (l1 - l0) * 16 / (int32_t)(n ? n : 1u), lp = s->clp, w = s->cw;
    for (i = 0; i < n; i++) {
        const int32_t len = l0 + ((dl * (int32_t)i) >> 4), di = len >> 8, fr = len & 255;
        const int32_t a0 = line[(w - (uint32_t)di) & (QUAD_COMB_N - 1u)];
        const int32_t a1 = line[(w - (uint32_t)di - 1u) & (QUAD_COMB_N - 1u)];
        const int32_t d = a0 + (((a1 - a0) * fr) >> 8);
        int32_t y;
        lp += (int32_t)(((int64_t)(d - lp) * ka) >> 15);
        y = acc[i] + (neg ? -1 : 1) * (int32_t)(((int64_t)g * lp) >> 15);
        line[w & (QUAD_COMB_N - 1u)] = (int16_t)clamp(y, -32768, 32767);
        w++;
        acc[i] = soft_knee(clamp((int32_t)(((int64_t)y * outk) >> 15), -200000, 200000), 16000);
    }
    s->clp = lp, s->clen = l1, s->cw = (uint16_t)(w & (QUAD_COMB_N - 1u));
}
/* MULTI and EQ (the combs without a line: MULTI) on n samples of acc: c the SVF's coefficients at the cut and RESO's
 * damping kd, shape / gain SHAPE and GAIN; the integrators the voice's (a switch among the SVF's types keeps them) */
static void quad_machine(uint32_t fty, int32_t *acc, uint32_t n, const quad_svf_t *c, int32_t kd, int32_t cut,
                         int32_t shape, int32_t gain, int32_t *ic1, int32_t *ic2)
{
    quad_svf_t ce;
    const quad_svf_t *cc = c;
    int32_t k = kd, g = 0, wm = 0, a = *ic1, b = *ic2;
    uint32_t i;
    if (fty == QF_EQ) {                          /* the bell: its own damping (SHAPE: Q), GAIN its height */
        k = QE_EQ_K[shape & 127];
        quad_svf_coef(&ce, cut, k);
        cc = &ce;
        g = QE_EQ_G[clamp(gain + 64, 0, 127)];
    } else {
        wm = shape <= 64 ? shape * 512 : (shape - 64) * 32768 / 63;   /* (Q15: LP -> BP, then BP -> HP) */
    }
    for (i = 0; i < n; i++) {
        int32_t v1, v2, x = acc[i], y;
        quad_svf_tick(cc, &a, &b, x, &v1, &v2);
        if (fty == QF_EQ) {
            y = x + (int32_t)(((int64_t)g * ((k * v1) >> 12)) >> 12);
        } else {
            int32_t bp = (kd * v1) >> 12, hp = x - bp - v2;
            y = shape == 0 ? v2 : shape == 127 ? hp
              : shape <= 64 ? v2 + (int32_t)(((int64_t)(bp - v2) * wm) >> 15)
                            : bp + (int32_t)(((int64_t)(hp - bp) * wm) >> 15);
        }
        acc[i] = soft_knee(clamp(y, -200000, 200000), 16000);
    }
    *ic1 = a, *ic2 = b;
}
/* CRUSH (version 6, FIZZ's E3): SRR a sample and hold (a new value when its phase wraps: 1 / N of the rate, N 1..64 on
 * a log table by SRR 1..127), BITS the sample to b bits of +-32768 (b 16 .. 1 over BITS 1..127, rounded; 0 stays 0);
 * CRPRE 1 before the filter, 0 after it; both 0: none */
static const uint32_t QE_SRR_INC[128] = {
    0u, 4156597496u, 4022685519u, 3893087748u, 3767665193u, 3646283343u, 3528812018u, 3415125236u,
    3305101070u, 3198621523u, 3095572399u, 2995843180u, 2899326911u, 2805920080u, 2715522512u, 2628037257u,
    2543370492u, 2461431413u, 2382132143u, 2305387636u, 2231115586u, 2159236339u, 2089672806u, 2022350382u,
    1957196867u, 1894142385u, 1833119312u, 1774062203u, 1716907721u, 1661594569u, 1608063426u, 1556256882u,
    1506119375u, 1457597134u, 1410638122u, 1365191975u, 1321209954u, 1278644891u, 1237451134u, 1197584506u,
    1159002250u, 1121662988u, 1085526674u, 1050554555u, 1016709123u, 983954080u, 952254298u, 921575779u,
    891885621u, 863151983u, 835344050u, 808431996u, 782386961u, 757181012u, 732787116u, 709179112u,
    686331680u, 664220318u, 642821312u, 622111712u, 602069307u, 582672603u, 563900797u, 545733758u,
    528152001u, 511136671u, 494669519u, 478732885u, 463309677u, 448383354u, 433937909u, 419957849u,
    406428181u, 393334394u, 380662446u, 368398747u, 356530144u, 345043909u, 333927723u, 323169664u,
    312758194u, 302682148u, 292930719u, 283493449u, 274360217u, 265521228u, 256967003u, 248688366u,
    240676441u, 232922633u, 225418627u, 218156376u, 211128091u, 204326235u, 197743512u, 191372863u,
    185207455u, 179240676u, 173466128u, 167877617u, 162469149u, 157234924u, 152169329u, 147266931u,
    142522472u, 137930864u, 133487183u, 129186662u, 125024691u, 120996804u, 117098683u, 113326146u,
    109675148u, 106141774u, 102722233u, 99412859u, 96210101u, 93110527u, 90110810u, 87207734u,
    84398186u, 81679153u, 79047718u, 76501059u, 74036445u, 71651233u, 69342864u, 67108864u,
};
static void quad_crush(quad_voice_t *s, int32_t bits, int32_t srr, int32_t *acc, uint32_t n)
{
    const uint32_t inc = srr ? QE_SRR_INC[srr & 127] : 0u;
    const int32_t sh = bits ? 16 - (16 - (bits - 1) * 15 / 126) : 0;
    uint32_t ph = s->crph, i;
    int32_t held = s->crheld;
    for (i = 0; i < n; i++) {
        int32_t y = acc[i];
        if (inc) {
            const uint32_t np = ph + inc;
            if (np <= ph)
                held = y;
            ph = np;
            y = held;
        }
        if (sh) {
            y = clamp(y, -32768, 32767);
            y = ((y + (1 << (sh - 1))) >> sh) << sh;
        }
        acc[i] = y;
    }
    s->crph = ph, s->crheld = held;
}
/* BWPOST: the base-width filter (BASE: a one-pole high-pass hpk, BASE + WIDTH: a one-pole low-pass lpk, the voice's
 * bh / bl) after the multimode filter (FIZZ's quad_ext_bw_post; a pole switched in or out steps: no crossfade here) */
static void quad_bw_post(quad_voice_t *s, int32_t *acc, uint32_t n, int32_t hpk, int32_t lpk)
{
    int32_t bh = s->bh, bl = s->bl;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = acc[i];
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
    s->bh = bh, s->bl = bl;
}

/* the operator outputs: WS the sine (B2 always), WC / WA / WB1 C's, A's, B1's wave (HARM: defined per loop), QM a
 * modulator's output as a phase offset (Q15 -> 2 cycles at full level), QFB the feedback operator's self-modulation
 * (its wave's last two values fb1, fb2: their sum x fbq << 2, 2^32 = a cycle), QLV an output at a level */
#define WS(ph) sine_i(ph)
#define QM(o) ((uint32_t)(o) << 18)
#define QFB() ((uint32_t)((fb1 + fb2) * fbq) << 2)
#define QLV(y, l) (((y) * (l)) >> 15)
/* the operator loop of one algorithm: the level ramps, BODY (sets X, Y and yf, the feedback operator's wave), the
 * feedback memory, the phases, MIX (X .. Y, gx + gy = 1: Y's gain gq ramped over the block, Q16) at half scale into
 * acc */
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
        gq += dgq;                                                         \
        acc[i] = ((X * (32766 - (gq >> 16))) + (Y * (gq >> 16))) >> 16;   \
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

/* DRIVE ROUTE PRE (version 6's DRPRE, FIZZ's E1; the Digitone's place: "distortion of the signal entering the
 * filter", per voice): the part's DIST (dsp.c dsp_dist) on voice s's signal before its filter and its amp, its state
 * the voice's, the LFOs' DRIVE on it; the part's own after the voices then none (fx.c track_dist: quad_drive_pre).
 * Each note driven alone (no intermodulation of a chord in one clipper), the filter shaping the drive, the grit kept
 * as the amp envelope falls. 3y213, under half dsp_dist's cost: the clip and the make-up one interpolation in the
 * part's table (quad_drv), and the voice's DC blocker and base-width (hpk, lpk: quad_render's) in the same loop, the
 * blocker in 32 bits (its mean before the sample's step, within 0.1 % of quad_render's). hpk -1: the drive alone (a
 * block in which a base-width pole fades: quad_render ran them). A function of its own: in quad_render the loop moved
 * its blocks about (tests/target_budget.py) */
static __attribute__((noinline)) void quad_drv_run(quad_voice_t *s, const quad_drv_t *q, int32_t *acc, uint32_t n,
                                                   int32_t hpk, int32_t lpk)
{
    const int32_t *tb = q->tab;
    const int32_t g = q->g, k = q->k, elo = q->elo, ehi = q->ehi;
    int32_t d = s->dc, bh = s->bh, bl = s->bl, h = s->dh, l1 = s->dl1, l2 = s->dl2, y, j;
    uint32_t i;
#define QUAD_DC(y)                               /* (quad_render's ((x << 12) - d) x K >> 16 in 32 bits) */ \
    y -= (d + 2048) >> 12;                                                  \
    d += (y * QUAD_DC_K) >> 4;
#define QUAD_DRV(y)                                                         \
    h += (y - h + 64) >> 7;                      /* ~55 Hz low cut: keep the bass out of the clipper */ \
    y = clamp(y - h, elo, ehi);                  /* (u on the table; past it the clip is flat) */ \
    y = ((y * g) >> 11) + QUAD_DRV_BIAS + 65536; /* (g: dsp_dist's / 2) */ \
    j = y >> 8;                                                             \
    y = tb[j] + (((tb[j + 1] - tb[j]) * (y & 255)) >> 8);                   \
    l1 += mulq15(y - l1, k);                     /* two poles: tames the fizz */ \
    l2 += mulq15(l1 - l2, k);                                               \
    acc[i] = l2;
    if (hpk < 0) {
        for (i = 0; i < n; i++) {
            y = acc[i];
            QUAD_DRV(y)
        }
    } else if (!(hpk | lpk)) {                   /* (no base-width: a loop with the registers it needs) */
        for (i = 0; i < n; i++) {
            y = acc[i];
            QUAD_DC(y)
            QUAD_DRV(y)
        }
        s->dc = d;
    } else {
        for (i = 0; i < n; i++) {
            y = acc[i];
            QUAD_DC(y)
            if (hpk) {
                bh += (int32_t)(((int64_t)((y << 8) - bh) * hpk) >> 16);
                y -= bh >> 8;
            }
            if (lpk) {
                bl += (int32_t)(((int64_t)((y << 8) - bl) * lpk) >> 16);
                y = bl >> 8;
            }
            QUAD_DRV(y)
        }
        s->dc = d, s->bh = bh, s->bl = bl;
    }
#undef QUAD_DC
#undef QUAD_DRV
    s->dh = h, s->dl1 = l1, s->dl2 = l2;
}

/* the voice's block: out gets it */
static void quad_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    quad_voice_t *s = quad_voice(t, v);
    uint32_t tr = quad_tr(t), k, i, alg, inc[4], bAll, bAB2;
    const int16_t *p;
    int32_t eA, eB, eF, eE, dst[QD_N], A0, A1, dA, a, x, y;
    int32_t hq, dt, mix, ra, rb, cut, kd, reso, gy, gq, dgq, fbq, hf, fdep, base, wid;
    int32_t la, lb1, lb2, dla, dlb1, dlb2, tl[3], hpk, lpk, svf, fty, fb1, fb2, bh, bl, bwh, bwl, drv;
    uint32_t fon, fon0;
    const int16_t *ha_t = QUAD_HARM[0], *hb_t = QUAD_HARM[0];
    uint32_t p0, p1, p2, p3, i0, i1, i2, i3;
    int32_t acc[CTL], dry[CTL];
    quad_svf_t c;
    tsvf_t c2;
    if (!s || !s->live || n > CTL)
        return;
    p = quad_patch[tr];
    /* the LFOs: read, then advanced; summed into their destinations (Q15 each) */
    for (k = 0; k < QD_N; k++)
        dst[k] = 0;
    for (i = 0; i < 3u; i++) {                   /* LFO 2, 3, then 1 (2 and 3 may modulate LFO 1: QD_L1*) */
        uint32_t d, tm, ph, rv, wv;
        int32_t inc0, w, fd, sph, dep;
        k = i < 2u ? i + 1u : 0u;
        d = (uint32_t)p[QP_LFO(k, QL_DEST)], tm = (uint32_t)p[QP_LFO(k, QL_TRIG)], wv = (uint32_t)p[QP_LFO(k, QL_WAVE)];
        fd = p[QP_LFO(k, QL_FADE)], sph = p[QP_LFO(k, QL_PHASE)], dep = p[QP_LFO(k, QL_DEPTH)];
        if (!k && (dst[QD_L1SPD] | dst[QD_L1MUL] | dst[QD_L1FAD] | dst[QD_L1WAV] | dst[QD_L1SPH] | dst[QD_L1MOD] |
                   dst[QD_L1DEP])) {            /* LFO 1's own parameters, moved by LFO 2 / 3 (none: as stored) */
            int32_t sp = clamp(p[QP_LFO(0, QL_SPEED)] + ((dst[QD_L1SPD] * 12700) >> 15), -6400, 6399);
            int32_t mu = p[QP_LFO(0, QL_MULT)], ms = mu < 12 ? 0 : 12, ib = quad_lfo_inc(p, 0);
            mu = ms + clamp(mu - ms + ((dst[QD_L1MUL] * 11) >> 15), 0, 11);   /* (within its set: BPM or fixed) */
            inc0 = quad_lfo_incv(sp, (uint32_t)mu);
            tm = (uint32_t)clamp((int32_t)tm + ((dst[QD_L1MOD] * (QT_N - 1)) >> 15), 0, QT_N - 1);
            if (tm == QT_FREE)                   /* the part's phase runs at the stored rate: the voice's lead */
                s->lof += (uint32_t)inc0 - (uint32_t)ib;
            fd = clamp(fd + ((dst[QD_L1FAD] * 127) >> 15), -64, 63);
            wv = (uint32_t)clamp((int32_t)wv + ((dst[QD_L1WAV] * (QW_N - 1)) >> 15), 0, QW_N - 1);
            sph = clamp(sph + ((dst[QD_L1SPH] * 127) >> 15), 0, 127);
            dep = clamp(dep + ((dst[QD_L1DEP] * 12700) >> 15), -6400, 6399);
        } else {
            inc0 = quad_lfo_inc(p, k);
        }
        if (tm == QT_FREE) {
            ph = quad_lfo[tr].ph[k] + (wv == QW_RAND ? 0u : (uint32_t)sph << 25) + (k ? 0u : s->lof);
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
            if (wv != QW_RAND)                   /* (L1SPH: the start phase moved = the phase read moved) */
                ph += (uint32_t)(sph - p[QP_LFO(k, QL_PHASE)]) << 25;
        }
        if (wv == QW_RAND) {                     /* RAND: SPH is the slew, a one-pole on the steps (127: about a
                                                  * period) */
            int32_t r = (int32_t)(rv >> 16) - 32768, sl = sph;
            if (sl) {
                int32_t kk = clamp((int32_t)(((uint32_t)(inc0 < 0 ? -inc0 : inc0) >> 17) * 254u / (uint32_t)sl), 1,
                                   32767);
                s->lsl[k] += ((r - s->lsl[k]) * kk) >> 15;
            } else {
                s->lsl[k] = r;
            }
        }
        if (!d || d >= QD_N || !dep)
            continue;
        w = quad_lfo_wave(wv, ph, s->lsl[k]);
        w = (w * dep) / 6400;                    /* (DEPTH: hundredths, 64.00 = full) */
        if (fd) {                                /* FADE: - in, + out, a linear ramp over |FADE|'s time (the
                                                  * Digitone's, QUAD_T_FADE: 32767 x 2^16 / the ticks it takes) */
            uint64_t f = ((uint64_t)s->ticks * QUAD_T_FADE[fd < 0 ? -fd : fd]) >> 16;
            int32_t g = f > 32767u ? 32767 : (int32_t)f;
            w = (w * (fd < 0 ? g : 32767 - g)) >> 15;
        }
        dst[d] += w;
    }
    if (s->ticks < 0xFFFFFFFFu)
        s->ticks++;
#define QMV(i, d, sc, lo, hi) clamp(p[i] + ((dst[d] * (sc)) >> 15), lo, hi)
#define QMV2(i, d, d2, sc, lo, hi) clamp(p[i] + (((dst[d] + dst[d2]) * (sc)) >> 15), lo, hi)   /* (+ an AB macro) */
    /* the envelopes (their times, END, SUS modulated; the AB macros move A's and B's together) */
    eA = quad_env_tick(s, 0, QMV2(QP_AATK, QD_AATK, QD_ABATK, 127, 0, 127),
                       QMV2(QP_ADEC, QD_ADEC, QD_ABDEC, 127, 0, 127), QMV2(QP_AEND, QD_AEND, QD_ABEND, 127, 0, 127) << 17,
                       0, QMV2(QP_ADLY, QD_ADLY, QD_ABDLY, 127, 0, 127), 1, 0);
    eB = quad_env_tick(s, 1, QMV2(QP_BATK, QD_BATK, QD_ABATK, 127, 0, 127),
                       QMV2(QP_BDEC, QD_BDEC, QD_ABDEC, 127, 0, 127), QMV2(QP_BEND, QD_BEND, QD_ABEND, 127, 0, 127) << 17,
                       0, QMV2(QP_BDLY, QD_BDLY, QD_ABDLY, 127, 0, 127), 1, 0);
    eF = quad_env_tick(s, 2, QMV(QP_FATK, QD_FATK, 127, 0, 127), QMV(QP_FDEC, QD_FDEC, 127, 0, 127),
                       QMV(QP_FSUS, QD_FSUS, 127, 0, 127) << 17, QMV(QP_FREL, QD_FREL, 127, 0, 127),
                       QMV(QP_FDLY, QD_FDLY, 127, 0, 127), v->gate, 1);
    if (p[QP_AMOD]) {                            /* AHD (version 6) */
        eE = quad_ahd(s, QMV(QP_EATK, QD_EATK, 127, 0, 127), p[QP_HOLD], QMV(QP_EDEC, QD_EDEC, 127, 0, 127), v->gate);
    } else {
        if (s->stage[3] >= 6u)                   /* (back from AHD mid-note) */
            s->stage[3] = s->stage[3] == 6u ? 3u : 4u;
        eE = quad_env_tick(s, 3, QMV(QP_EATK, QD_EATK, 127, 0, 127), QMV(QP_EDEC, QD_EDEC, 127, 0, 127),
                           QMV(QP_ESUS, QD_ESUS, 127, 0, 127) << 17, QMV(QP_EREL, QD_EREL, 127, 0, 127), 0, v->gate, 1);
    }
    if (v - t->v == t->m_vi)                     /* the part's PAN: from the latest note's voice */
    {
        quad_pan_off[tr] = (int8_t)clamp((dst[QD_PAN] * 64) >> 15, -64, 63);
        for (k = 0; k < 4u; k++)                 /* DRIVE, REV, DEL, CHO: the part's, +-127 at full depth */
            quad_fx_off[tr][k] = (int8_t)clamp((dst[QD_DRIVE + k] * 127) >> 15, -127, 127);
    }
    /* the operators: ratios, detune, increments (PITCH: all, P AB2: A and B2, +-1 octave) */
    alg = (uint32_t)QMV(QP_ALGO, QD_ALGO, 7, 1, 8) - 1u;
    dt = QMV(QP_DTUN, QD_DTUN, 12700, 0, 12700);   /* hundredths: between two table steps, interpolated */
    ra = QMV2(QP_RA, QD_RA, QD_RALL, 63, 0, QUAD_NRA - 1);   /* (RATIO ALL: C, A, B1, B2 each over its range) */
    rb = clamp(p[QP_RB1] * (int32_t)QUAD_NRCB + p[QP_RB2] + ((dst[QD_RB] * 180) >> 15), 0, QUAD_NRB - 1);
    if (dst[QD_RALL]) {
        int32_t o = (dst[QD_RALL] * 18) >> 15;
        rb = clamp(rb / (int32_t)QUAD_NRCB + o, 0, QUAD_NRCB - 1) * (int32_t)QUAD_NRCB +
             clamp(rb % (int32_t)QUAD_NRCB + o, 0, QUAD_NRCB - 1);
    }
    bAll = m->inc;
    if (dst[QD_PITCH])
        bAll = (uint32_t)(((uint64_t)bAll * (uint32_t)quad_exp2((dst[QD_PITCH] * 4096) >> 15)) >> 15);
    bAB2 = bAll;
    if (dst[QD_PAB2])
        bAB2 = (uint32_t)(((uint64_t)bAll * (uint32_t)quad_exp2((dst[QD_PAB2] * 4096) >> 15)) >> 15);
    {
        int32_t rc = quad_ratio(QUAD_RCB_Q16[QMV2(QP_RC, QD_RC, QD_RALL, 18, 0, QUAD_NRCB - 1)], QMV(QP_OFSC, QD_OFSC, 100, -100, 100));
        int32_t rA = quad_ratio(QUAD_RA_Q16[ra], QMV(QP_OFSA, QD_OFSA, 100, -100, 100));
        int32_t rB1 = quad_ratio(QUAD_RCB_Q16[rb / (int32_t)QUAD_NRCB], QMV(QP_OFSB1, QD_OFSB1, 100, -100, 100));
        int32_t rB2 = quad_ratio(QUAD_RCB_Q16[rb % (int32_t)QUAD_NRCB], QMV(QP_OFSB2, QD_OFSB2, 100, -100, 100));
        inc[0] = quad_inc(bAll, rc, 0);
        int32_t d0 = dt / 100, d1 = d0 < 127 ? d0 + 1 : 127, df = dt % 100;
        int32_t du = QUAD_DT_UP[d0] + ((QUAD_DT_UP[d1] - QUAD_DT_UP[d0]) * df) / 100;
        int32_t dd = QUAD_DT_DN[d0] + ((QUAD_DT_DN[d1] - QUAD_DT_DN[d0]) * df) / 100;
        inc[1] = quad_inc(bAB2, rA, du);                        /* DTUN: A up, B2 down (B1, C untouched) */
        inc[2] = quad_inc(bAll, rB1, 0);
        inc[3] = quad_inc(bAB2, rB2, dd);
    }
    /* HARM (Q8: the LFO's in between the steps): the two tables and the share of the second, Q15 */
    hq = QMV(QP_HARM, QD_HARM, 2600, -2600, 2600) * 256 / 100;   /* (hundredths -> Q8) */
    hf = 0;
    if (hq) {
        int32_t h = hq < 0 ? -hq : hq, t0 = h >> 8, t1 = t0 < 26 ? t0 + 1 : 26;
        hf = t1 > t0 ? (h & 255) << 7 : 0;
        ha_t = QUAD_HARM[t0];
        hb_t = QUAD_HARM[t1];
    }
    fbq = quad_fbq((QMV(QP_FDBK, QD_FDBK, 127, 0, 127) * 120 + 63) / 127);   /* (0..127: the 0..120 curve) */
    mix = QMV(QP_MIX, QD_MIX, 127, -64, 63);
    gy = (64 + mix) * 258;                       /* -64: X alone, 63: Y alone (gx + gy = 32766) */
    {   /* from the last block's MIX to this one's over the block (a step of MIX, an LFO's RAMP or SQR on it, stepped
         * the output between two samples); the same gain when it stays */
        int32_t g0 = s->mixp == QUAD_MIX_NEW ? gy : (64 + s->mixp) * 258;
        gq = g0 * 65536;
        dgq = ((gy - g0) * 65536) >> CTL_LOG2;
        s->mixp = (int8_t)mix;
    }
    /* the operator levels: envelope x LEV (A: squared; B: the B LEV law, squared) x velocity x key scaling, Q15 */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, velf = 32767 - ((p[QP_VEL] * (127 - vel) * 2080) >> 10), j, u1, u2;
        int32_t lv[3];
        quad_blev(QMV2(QP_BLEV, QD_BLEV, QD_ABLEV, 127, 0, 127), &u1, &u2);
        lv[0] = quad_lev(QMV2(QP_ALEV, QD_ALEV, QD_ABLEV, 127, 0, 127) * 4096 / 127);
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
    fdep = QMV(QP_FDEPTH, QD_FENV, 12700, -6400, 6399);          /* (hundredths: FREQ, RESO, DEPTH, BASE) */
    s->fsm = (int16_t)quad_glide_v(s->fsm, p[QP_FREQ]);
    s->rsm = (int16_t)quad_glide_v(s->rsm, p[QP_RESO]);
    cut = (clamp(s->fsm + ((dst[QD_FREQ] * 12700) >> 15), 0, 12700) * 256 + 50) / 100 + (eF * fdep) / 6400 +
          (((m->pitch16 - 60 * 16) * p[QP_FKTRK] * 150) >> 10);
    reso = clamp(s->rsm + ((dst[QD_RESO] * 12700) >> 15), 0, 12700);
    fty = p[QP_FTYPE];
    svf = fty != QF_OFF && !(fty != QF_HP12 && cut >= (127 << 8) && !reso);   /* LP wide open, no resonance: none */
    if (fty >= QF_MULTI)                         /* (the machines always run) */
        svf = 1;
    kd = 8192 - reso * 3 / 5;                    /* (60 a whole step) */
    base = QMV(QP_BASE, QD_BASE, 12700, 0, 12700);
    wid = QMV(QP_WIDTH, QD_WIDTH, 127, 0, 127);
    hpk = base ? quad_bwk(base) : 0;
    lpk = base + wid * 100 < 12700 ? quad_bwk(base + wid * 100) : 0;
    bwh = hpk, bwl = lpk;
    if (p[QP_BWPOST])                            /* (BWPOST: the base-width after the filter, quad_bw_post) */
        hpk = lpk = 0;
    /* the filters running now (fon) and in the last block (fon0): one switched in or out (an envelope or an LFO taking
     * the multimode past its open top, BASE to 0, BASE + WIDTH to the top) runs this block too and crossfades (below) */
    fon = (svf ? 1u : 0u) | (hpk ? 2u : 0u) | (lpk ? 4u : 0u);
    fon0 = s->fon == QUAD_FON_NEW ? fon : s->fon;
    s->fon = (uint8_t)fon;
    if ((fon | fon0) & 1u) {
        quad_svf_coef(&c, cut, kd);
        if (fty == QF_LP24)
            tsvf_coef_k(&c2, cut, 8192);
    }
    /* the amplitude: the amp envelope x velocity (half) x LEVEL (squared), the voice's (fades, LFO -> AMP) */
    {
        int32_t vel = v->mvel ? v->mvel : v->vel, lv = QMV(QP_LEVEL, QD_LEVEL, 127, 0, 127);
        A1 = mulq15(mulq15(eE, 32767 - (127 - vel) * 129), lv * lv * 2);   /* (2 LEVEL^2) */
        if (t->p[P_VOICE] == V_UNISON)
            A1 = (A1 * 13107) >> 15;             /* 2 / 5 */
        A1 = mulq15(A1, m->amp1);
    }
#undef QMV
#undef QMV2
    A0 = s->amp;
    dA = (A1 - A0) * (1 << (8 - CTL_LOG2));      /* the ramp in Q23, exact over the tick (in Q15, >> 5 floored a slow
                                                  * decay's step to -1 a sample: the tail saw-toothed up to 31 below
                                                  * its level, a decay read ~2 % short) */
    a = A0 * 256;
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
     * low-pass, Q8 states): the manual's order, base-width before the multimode. A pole switched out runs its last
     * block at its end of the range and fades out over it (the high-pass's state then 0); one switched in starts
     * from the state its input implies (the high-pass from 0, the low-pass at the input): no step either way */
    {
        int32_t d = s->dc;
        uint32_t sw = (fon ^ fon0) & 6u;
        bh = s->bh, bl = s->bl;
        drv = p[QP_DRPRE] && quad_drv[tr].on;    /* DRIVE ROUTE PRE (quad_drv_run) */
        if (drv && !sw) {
            /* (the DC blocker and the base-width in the drive's loop, below) */
        } else if (!sw) {
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
        } else {                                 /* a pole switched in or out: this block's fades (0 .. 1) */
            int32_t hk = hpk ? hpk : sw & 2u ? QUAD_BW_K[0] : 0, lk = lpk ? lpk : sw & 4u ? quad_bwk(12699) : 0;
            int32_t nf = n > 1u ? (int32_t)n - 1 : 1;
            if ((sw & 4u) && lpk)
                bl = INT32_MIN;                  /* (in: at its first input) */
            for (i = 0; i < n; i++) {
                x = acc[i];
                d += (int32_t)(((((int64_t)x << 12) - d) * QUAD_DC_K) >> 16);
                y = x - ((d + 2048) >> 12);
                if (hk) {
                    bh += (int32_t)(((int64_t)((y << 8) - bh) * hk) >> 16);
                    y -= hpk ? bh >> 8 : (int32_t)(((int64_t)(bh >> 8) * (nf - (int32_t)i)) / nf);
                }
                if (lk) {
                    if (bl == INT32_MIN)
                        bl = y << 8;
                    bl += (int32_t)(((int64_t)((y << 8) - bl) * lk) >> 16);
                    y = lpk ? bl >> 8 : (bl >> 8) + (int32_t)(((int64_t)(y - (bl >> 8)) * (int32_t)i) / nf);
                }
                acc[i] = y;
            }
            if ((sw & 2u) && !hpk)
                bh = 0;                          /* (out: the high-pass from rest when it comes back) */
        }
        s->dc = d;
        s->bh = bh, s->bl = bl;
    }
    if (drv)                                     /* DRIVE PRE with the DC blocker and the base-width (a block in
                                                  * which a pole fades: after them, the drive alone) */
        quad_drv_run(s, &quad_drv[tr], acc, n, (fon ^ fon0) & 6u ? -1 : hpk, lpk);
    if ((p[QP_BITS] || p[QP_SRR]) && p[QP_CRPRE])   /* CRUSH before the filter */
        quad_crush(s, p[QP_BITS], p[QP_SRR], acc, n);
    /* the multimode SVF, a loop per type (LP12 v2, HP12 x - kd v1 - v2, LP24 a plain LP12 then the resonant one; the
     * products rounded: truncation's bias, integrated, was DC), the soft knee after it. Switched in (its envelope or
     * an LFO bringing a wide-open LP down): its states start at the input (the LP's at the input, the band's at 0)
     * and its output fades in over the block; switched out: its last block fades out to the input */
    if ((fon | fon0) & 1u) {
        int32_t ic1 = s->f1, ic2 = s->f2, ic3 = s->f3, ic4 = s->f4;
        uint32_t sw = (fon ^ fon0) & 1u;
        if (sw) {
            for (i = 0; i < n; i++)
                dry[i] = acc[i];
            if (svf) {
                ic1 = ic3 = 0;
                ic2 = ic4 = acc[0];
            }
        }
#define QUAD_SVF(PRE, Y)                                                   \
    for (i = 0; i < n; i++) {                                              \
        int32_t v1, v2, v3;                                                \
        x = acc[i];                                                        \
        PRE                                                                \
        v3 = x - ic2;                                                      \
        v1 = (c.a1 * ic1 + c.a2 * v3 + ((c.f2 * v3) >> 5) + 4096) >> 13;     \
        v2 = ic2 + ((c.a2 * ic1 + c.a3 * v3 + ((c.f2 * ic1) >> 5) + 4096) >> 13); \
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);                        \
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);                        \
        acc[i] = soft_knee(clamp((Y), -200000, 200000), 16000);            \
    }
        if ((fty == QF_COMBP || fty == QF_COMBN) && s->cline)   /* (a comb with its line) */
            quad_comb(s, fty == QF_COMBN, acc, n, cut, reso, p[QP_FSHAPE]);
        else if (fty >= QF_MULTI)                /* the machines (version 6) */
            quad_machine((uint32_t)fty, acc, n, &c, kd, cut, p[QP_FSHAPE], p[QP_FGAIN], &ic1, &ic2);
        else if (fty == QF_HP12)
            QUAD_SVF(, x - ((kd * v1) >> 12) - v2)
        else if (fty == QF_LP24)
            QUAD_SVF(v3 = x - ic4; v1 = (c2.a1 * ic3 + c2.a2 * v3 + 4096) >> 13;
                     v2 = ic4 + ((c2.a2 * ic3 + c2.a3 * v3 + 4096) >> 13);
                     ic3 = clamp(2 * v1 - ic3, -150000, 150000); ic4 = clamp(2 * v2 - ic4, -150000, 150000); x = v2;,
                     v2)
        else
            QUAD_SVF(, v2)
#undef QUAD_SVF
        if (sw) {                                /* in: dry -> filtered, out: filtered -> dry */
            int32_t nf = n > 1u ? (int32_t)n - 1 : 1;
            for (i = 0; i < n; i++) {
                int32_t g = svf ? (int32_t)i : nf - (int32_t)i;
                acc[i] = dry[i] + (int32_t)(((int64_t)(acc[i] - dry[i]) * g) / nf);
            }
        }
        s->f1 = ic1, s->f2 = ic2, s->f3 = ic3, s->f4 = ic4;
    }
    if (p[QP_BWPOST] && (bwh | bwl))
        quad_bw_post(s, acc, n, bwh, bwl);
    if ((p[QP_BITS] || p[QP_SRR]) && !p[QP_CRPRE])  /* CRUSH after it */
        quad_crush(s, p[QP_BITS], p[QP_SRR], acc, n);
    /* the amplitude, into the part at the engine's output scale: >> 12, 6 dB under the >> 11 of before 2026-10-16. At
     * the Digitone's levels (VOL 100 .. 127: INIT's 110, a .dnsnd import's own) a sound was 6 dB over the other
     * engines' (tests/va_levels.c loud / dnsnd; docs/QUAD.md "Level"); the factory presets' LEVELs went up by the same
     * 6 dB (x 1.41 on the squared law), so they reach the part, its DRIVE among it, as before */
    for (i = 0; i < n; i++) {
        y = clamp(acc[i], -65535, 65535);
        a += dA;
        out[i] += (mulq15(y, a >> 8) * (VOICE_FS / 4)) >> 12;
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

/* the part's sound carries its sends (version 6's SENDS: a Digitone import; cr_ui.c cu_sends_load, cu_store) */
static int quad_sends_own(const track_t *t)
{
    uint32_t tr = quad_tr(t);
    return tr < QUAD_NPART && t->eng_req == ENGI_QUAD && quad_patch[tr][QP_SENDS];
}

/* fx.c mix_part: the part's pan with the LFOs' PAN */
static int32_t quad_pan(const track_t *t, int32_t pan)
{
    uint32_t tr = quad_tr(t);
    if (t->engine != ENGI_QUAD || tr >= QUAD_NPART || !quad_pan_off[tr])
        return pan;
    return clamp(pan + quad_pan_off[tr], -64, 63);
}

/* fx.c track_dist: the part's DRIVE runs in its voices (DRIVE ROUTE PRE), not after them */
static int quad_drive_pre(const track_t *t)
{
    uint32_t tr = quad_tr(t);
    return t->engine == ENGI_QUAD && tr < QUAD_NPART && quad_patch[tr][QP_DRPRE];
}

/* fx.c: the part's DRIVE (track_dist) and its sends (mix_part_out: c d r, x 258) with the LFOs' (QD_DRIVE, QD_SREV ..);
 * read once a block, the sends ramped over it as any send change */
static int32_t quad_drive(const track_t *t, int32_t d)
{
    uint32_t tr = quad_tr(t);
    if (t->engine != ENGI_QUAD || tr >= QUAD_NPART || !quad_fx_off[tr][0])
        return d;
    return clamp(d + quad_fx_off[tr][0], 0, 127);
}
static void quad_sends(const track_t *t, int32_t *c, int32_t *d, int32_t *r)
{
    uint32_t tr = quad_tr(t);
    if (t->engine != ENGI_QUAD || tr >= QUAD_NPART || !(quad_fx_off[tr][1] | quad_fx_off[tr][2] | quad_fx_off[tr][3]))
        return;
    *r = clamp(t->p[P_REV] + quad_fx_off[tr][1], 0, 127) * 258;
    *d = clamp(t->p[P_DLY] + quad_fx_off[tr][2], 0, 127) * 258;
    *c = clamp(t->p[P_CHOR] + quad_fx_off[tr][3], 0, 127) * 258;
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
    {1, {5, 6}, ENG_B_WINDOW, 1, "FILTER 2", {{QS_N, QS_N, QS_N, QS_N}, {QS_N, QS_N, QS_T}},
     {{"Env delay", "Key track", "Shape", "Gain"}, {"Base", "Width", "BW route"}}},
    {2, {7, 8}, ENG_B_ADE2, 0, "ENV A/B", {{QS_N, QS_N, QS_N, QS_N}, {QS_N, QS_N, QS_N, QS_N}},
     {{"A Attack", "A Decay", "A End", "A Level"}, {"B Attack", "B Decay", "B End", "B Level"}}},
    {2, {9, 10}, ENG_B_NONE, 0, "ENV 2", {{QS_N, QS_T, QS_T, QS_T}, {QS_N, QS_T, QS_T, QS_N}},
     {{"A Delay", "A Trig", "A Reset", "Phase"}, {"B Delay", "B Trig", "B Reset", "Velocity"}}},
    {2, {11, 0xFF}, ENG_B_NONE, 0, "ENV 3", {{QS_N, QS_N, QS_N}}, {{"A Key", "B1 Key", "B2 Key"}}},
    {2, {12, 13}, ENG_B_ENV, 1, "AMP", {{QS_N, QS_N, QS_N, QS_N}, {QS_N, QS_N, QS_N, QS_T}},
     {{"Attack", "Decay", "Sustain", "Release"}, {"Level", "Pan", "Drive", "Route"}}},
    QUAD_LSCR(1), QUAD_LSCR(2), QUAD_LSCR(3),
    /* appended (version 6): the group's last screens (ENV's fifth, FILT's third) */
    {2, {QUAD_PG_AMP2, 0xFF}, ENG_B_NONE, 0, "AMP 2", {{QS_T, QS_N}}, {{"Amp mode", "Hold"}}},
    {1, {QUAD_PG_CRUSH, 0xFF}, ENG_B_NONE, 0, "CRUSH", {{QS_N, QS_N, QS_T}}, {{"Bits", "Rate", "Route"}}},
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
    .desc = quad_desc,
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
    .edit = {QC_ALGO, QC_RC, QC_RA, QC_RB2, QC_HARM_M, QC_DTUN_M, QC_FDBK, QC_MIX},   /* (HARM, DTUNE: coarse) */
    .presets = QUAD_PRESETS,
    .npresets = NELEM(QUAD_PRESETS),
    .knob = {P_E2, P_E4, P_E6, P_E7},            /* HOME: RATIO A, HARM, FDBK, MIX */
    .poly = QUAD_POLY,
    .note_on = quad_note_on,
    .render = quad_render,
    .block = quad_block,
    .ownenv = 1,
    .done = quad_done,
    .endless = quad_endless,
    .legato = quad_legato,
    .deep = &QUAD_DEEP,
    .split = QUAD_VOICE_US / 10u,  /* the chord's voices on both cores (3y206, fx.c p5x_run; docs/QUAD.md "Two cores") */
};
