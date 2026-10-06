/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SEQ > PATTERNS) and the parts' sounds at power-on. */
#include "dsp.c"
#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_grain.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#include "eng_noise.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
#if FELUCCA_FM4
#include "eng_digital.c"        /* DIGITAL: four-operator FM (retired; FELUCCA_FM4=1 builds it) */
#endif
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif
#if FELUCCA_VA
#include "eng_va.c"             /* VA: ChoralRoot's four-oscillator virtual analog (deep pages, its own patch) */
#endif

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,                 /* 0 */
#if FELUCCA_FM4
    &ENG_DIGITAL,                /* 1 (ENGI_DIGITAL) */
#else
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
#endif
    &ENG_PHASE,                  /* 2 */
    &ENG_LOFI,                   /* 3 */
    &ENG_SAMPLE,                 /* 4 */
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_TRIO,                   /* 6 */
    &ENG_WHEEL,                  /* 7 */
    &ENG_GRAIN,                  /* 8 */
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    &ENG_NOISE,                  /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#if FELUCCA_SLICE
    &ENG_SLICE,                  /* 13 (FELUCCA_SLICE=0 builds without it) */
#endif
#if FELUCCA_VA
    &ENG_VA,                     /* 13 + FELUCCA_SLICE (ENGI_VA; ChoralRoot: 13) */
#endif
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with FELUCCA_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    0,                           /* ANALOG */
    12,                          /* FM6 */
#if FELUCCA_VA
    ENGI_VA,                     /* VA */
#endif
#if FELUCCA_FM4
    1,                           /* DIGITAL */
#endif
    2, 3, 4, 5, 6, 7, 8, 9,      /* PHASE LOFI SAMPLE VOICE TRIO WHEEL GRAIN PHYS */
    11,                          /* NOISE */
#if FELUCCA_SLICE
    13,                          /* SLICE */
#endif
    10,                          /* DRUM */
};

/* the engines one can pick (engine 1 only with FELUCCA_FM4), in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e) { return e < NENGINES && (FELUCCA_FM4 || e != ENGI_DIGITAL); }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (DIGITAL without FELUCCA_FM4: its sounds play as FM6) */
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}

/* factory sequence patterns: SEQ > PATTERNS loads one into the selected track (ui.c pat_load); a preset
 * only suggests one with PAT(n), loading a sound never touches the steps. Absolute notes, loaded as they
 * are (DRUM and SAMPLE PERC play them as GM drums, SLICE as slices; SCL TRANS and OCT transpose): 0 = rest;
 * flags 1 = accent, 2 = slide, 4 = tie (holds the previous note). Names: at most 8 characters */
#define T_ 4
static const struct {
    const char *name;
    uint8_t note[16], flags[16];
} PATTERNS[] = {
    {"ACID", {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},          /* 1 */
     {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}},
    {"OFFBEAT", {0, 36, 0, 36, 0, 36, 0, 48, 0, 36, 0, 36, 0, 39, 0, 43},          /* 2 bass */
     {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}},
    {"MELODY", {60, 0, 67, 0, 72, 67, 0, 64, 62, 0, 69, 0, 74, 69, 0, 67},         /* 3 pluck */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    {"LEAD", {72, 0, 0, 74, 0, 0, 76, 0, 79, 0, 76, 0, 74, 0, 0, 0},               /* 4 */
     {1, T_, 0, 2, T_, 0, 0, 0, 1, 0, 2, 0, 0, T_, T_, 0}},
    {"PAD", {60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},                   /* 5 long notes */
     {0, T_, T_, T_, T_, T_, T_, 0, 0, T_, T_, 0, 0, T_, T_, 0}},
    {"KEYS", {0, 0, 60, 0, 0, 63, 0, 0, 0, 0, 60, 0, 0, 65, 0, 63},                /* 6 offbeat stabs */
     {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}},
    {"BELL", {72, 0, 0, 79, 0, 0, 84, 0, 0, 0, 76, 0, 0, 0, 0, 0},                 /* 7 sparse */
     {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"SUB", {36, 0, 0, 0, 0, 0, 0, 36, 0, 0, 34, 0, 0, 0, 0, 0},                   /* 8 low, held */
     {1, T_, T_, T_, 0, 0, 0, 0, 0, 0, 0, T_, T_, T_, 0, 0}},
    /* SLICE (eng_slice.c): note = C4 + slice */
    {"CHOP", {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64},     /* 9 16 slices re-ordered */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"STUTTER", {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67},    /* 10 8 slices, repeats */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"SLICES", {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75},   /* 11 in order */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* DRUM, SAMPLE PERC (General MIDI: 36 kick, 38 snare, 42 closed / 46 open hi-hat) */
    {"BEAT", {36, 42, 42, 42, 38, 42, 36, 42, 36, 42, 42, 36, 38, 42, 46, 42},     /* 12 */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* 16ths up a C minor arpeggio, twice: the line the ARP presets (RAVE, ARP 8BIT, ARP LEAD) suggest
     * now that the arpeggiator is the track's and a preset no longer switches it on */
    {"ARP", {48, 51, 55, 60, 63, 67, 72, 75, 48, 51, 55, 60, 63, 67, 72, 75},       /* 13 */
     {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
};
#undef T_
#define NPATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

/* the parts at power-on (engine, preset, PATTERNS[n - 1] in the sequencer, 0 = empty: all are): bass, pad, lead, drums */
static const uint8_t TRK_DEF[NPART][3] = {{0, 4, 0}, {ENGI_FM6, 4, 0}, {3, 0, 0}, {ENGI_DRUM, 0, 0}}; /* ANALOG ACID,
                                                                       * FM6 PAD (was DIGITAL PAD), LOFI PULSE LD, DRUM KIT */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
