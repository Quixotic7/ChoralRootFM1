/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SEQ > PATTERNS) and the parts' sounds at power-on. */
#if (FELUCCA_GRAIN || FELUCCA_SLICE) && !FELUCCA_SAMPLE
#error "GRAIN and SLICE play the SAMPLE engine's sets: FELUCCA_GRAIN / FELUCCA_SLICE need FELUCCA_SAMPLE"
#endif
#include "dsp.c"
#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#if FELUCCA_SAMPLE
#include "eng_sample.c"         /* SAMPLE: the ADPCM sets (build/gen/felucca_samples.h), the user sample slots */
#endif
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#if FELUCCA_GRAIN
#include "eng_grain.c"
#endif
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#if FELUCCA_DRUM
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#endif
#include "eng_noise.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM rendered as Dexed renders it (fm6_core.c; Melodee's, Kerem Kilic) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
#if FELUCCA_CZ
#if !FELUCCA_VA
#error "FELUCCA_CZ: its tone store sits beside the VA's and FM6's (upreset.c): FELUCCA_VA 1"
#endif
#include "eng_cz.c"             /* CZ-1: native Casio CZ-1 tones (cz_native.c; Melodee's, Kerem Kilic) */
#endif

/* the engines' runtime state of a part (Melodee's voice model). A part renders one engine at a time (an engine switch
 * fades the old one out first, voice.c engine_block), so their states share one block per part, cleared at every
 * switch: an engine finds its state as at power-on (the pool section is zeroed at boot). PHYS's voice slots, WHEEL's
 * part and voices, FM6's 16 voices and controllers. The patch of a part (FM6's, the VA's) is not in here; nor are the
 * states of the engines ChoralRoot does not build (DRUM, GRAIN, SLICE keep their own: Felucca's unit) */
static union {
    phys_slot_t phys[PHYS_POLY];
    drw_part_t wheel;
    fm6_part_t fm6;
#if FELUCCA_CZ
    cz_part_t cz;                /* CZ-1's voices: six envelopes each, vibrato, noise */
#endif
} eng_state[NPART] __attribute__((section(".pool")));
static phys_slot_t *phys_slots(uint32_t part) { return eng_state[part % NPART].phys; }
static drw_part_t *drw_of(const track_t *t) { return &eng_state[(uint32_t)(t - trk) % NPART].wheel; }
static fm6_part_t *fm6_part(uint32_t part) { return &eng_state[part % NPART].fm6; }
#if FELUCCA_CZ
static cz_part_t *cz_part(uint32_t part) { return &eng_state[part % NPART].cz; }
#endif
static void eng_state_clear(uint32_t part)
{
    if (part < NPART)
        memset(&eng_state[part], 0, sizeof eng_state[part]);
}

/* a retired engine's slot (FELUCCA_SAMPLE / GRAIN / DRUM 0, ChoralRoot): no DSP, no presets, silent, never offered
 * (eng_ok: PRESETS, the EDIT layer, the pickers skip it); its number stays reserved (the stores hold numbers) */
#if !FELUCCA_FM4
#define ENG_GONE ENG_FM4_GONE    /* (fm4_convert.c: DIGITAL's empty slot is the same thing) */
#elif !FELUCCA_SAMPLE || !FELUCCA_GRAIN || !FELUCCA_DRUM
static void eng_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void eng_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = eng_gone_note_on,
    .render = eng_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#endif
#if !FELUCCA_SAMPLE
/* what the kept files read of SAMPLE: params.c enum_orig's alias table (no set: never an alias) */
#define SMP_NSETS 0
static const char *const SMP_ALL_NAMES[1] = {"-"};
static const uint8_t SMP_SET_ORIG[1] = {0};
#endif
#if !FELUCCA_DRUM
/* what the kept files call of DRUM: voice.c (a part on engine 10 shares a lane's voice; never one here), upreset.c
 * (a DRUM part's grid into a user slot; never one here) */
#define ENG_DRUM ENG_GONE
static voice_t *drum_reuse(struct track *t, uint32_t note) { (void)t; (void)note; return 0; }
static uint32_t step_lanes(const step_t *s) { (void)s; return 0; }
static uint32_t step_accents(const step_t *s) { (void)s; return 0; }
#endif
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
#if FELUCCA_SAMPLE
    &ENG_SAMPLE,                 /* 4 */
#else
    &ENG_GONE,                   /* 4: reserved (SAMPLE, retired: FELUCCA_SAMPLE 0) */
#endif
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_TRIO,                   /* 6 */
    &ENG_WHEEL,                  /* 7 */
#if FELUCCA_GRAIN
    &ENG_GRAIN,                  /* 8 */
#else
    &ENG_GONE,                   /* 8: reserved (GRAIN, retired: FELUCCA_GRAIN 0) */
#endif
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
#if FELUCCA_DRUM
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
#else
    &ENG_GONE,                   /* 10: reserved (DRUM, retired: FELUCCA_DRUM 0) */
#endif
    &ENG_NOISE,                  /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#if FELUCCA_SLICE
    &ENG_SLICE,                  /* 13 (FELUCCA_SLICE=0 builds without it) */
#endif
#if FELUCCA_VA
    &ENG_VA,                     /* 13 + FELUCCA_SLICE (ENGI_VA; ChoralRoot: 13) */
#endif
#if FELUCCA_CZ
    &ENG_CZ,                     /* 13 + FELUCCA_SLICE + FELUCCA_VA (ENGI_CZ; ChoralRoot: 14) */
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
    2,                           /* PHASE */
#if FELUCCA_CZ
    ENGI_CZ,                     /* CZ-1 (after PHASE, as Melodee shows it) */
#endif
    3,                           /* LOFI */
#if FELUCCA_SAMPLE
    4,                           /* SAMPLE */
#endif
    5, 6, 7,                     /* VOICE TRIO WHEEL */
#if FELUCCA_GRAIN
    8,                           /* GRAIN */
#endif
    9,                           /* PHYS */
    11,                          /* NOISE */
#if FELUCCA_SLICE
    13,                          /* SLICE */
#endif
#if FELUCCA_DRUM
    10,                          /* DRUM */
#endif
};

/* the engines one can pick (engine 1 only with FELUCCA_FM4; 4, 8, 10 only with FELUCCA_SAMPLE, GRAIN, DRUM), in
 * ENGINE_ORDER: eng_ok(e), the n-th of them eng_vis(n), e's place among them eng_rank(e), the next / previous one
 * eng_step(e, dir) (wraps) */
#define ENG_RETIRED ((FELUCCA_FM4 ? 0u : 1u << ENGI_DIGITAL) | (FELUCCA_SAMPLE ? 0u : 1u << 4) | \
                     (FELUCCA_GRAIN ? 0u : 1u << 8) | (FELUCCA_DRUM ? 0u : 1u << ENGI_DRUM))
static int eng_ok(uint32_t e) { return e < NENGINES && !((ENG_RETIRED >> e) & 1u); }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (DIGITAL without FELUCCA_FM4: its sounds play as FM6; a retired
                                                  * engine's place: FM6's) */
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
static const uint8_t TRK_DEF[NPART][3] = {{0, 4, 0}, {ENGI_FM6, 4, 0}, {3, 0, 0},  /* ANALOG ACID, FM6 PAD (was DIGITAL
                                                                                  * PAD), LOFI PULSE LD, DRUM KIT */
#if FELUCCA_DRUM
                                          {ENGI_DRUM, 0, 0}};
#else
                                          {ENGI_FM6, 0, 0}};   /* (no DRUM: FM6's first sound) */
#endif
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
