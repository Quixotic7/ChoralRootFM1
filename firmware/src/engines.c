/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SEQ > PATTERNS) and the parts' sounds at power-on. */
#include "dsp.c"
#include "va_common.c"          /* the VA's and GRAIN's shared filter, LFO sync list and amount rule (every build) */
/* what the kept files call of PHASE: cz_native.c's carrier (eng_phase.c pd_cos: -cos, Q15, from the 16-bit PD phase) */
static inline int32_t pd_cos(uint32_t ph16) { return -sine_i(((ph16 & 0xFFFFu) << 16) + 0x40000000u); }
#include "eng_wheel.c"
/* (GRAIN, engine 8, is included after eng_va.c below: it uses the VA's filter and LFO sync) */
#include "eng_fm6.c"            /* FM6: 6-operator FM rendered as Dexed renders it (fm6_core.c; Melodee's, Kerem Kilic) */
#if FELUCCA_CZ
#if !FELUCCA_VA
#error "FELUCCA_CZ: its tone store sits beside the VA's and FM6's (upreset.c): FELUCCA_VA 1"
#endif
#include "eng_cz.c"             /* CZ-1: native Casio CZ-1 tones (cz_native.c; Melodee's, Kerem Kilic) */
#endif
#if FELUCCA_QUAD
#if !FELUCCA_CZ
#error "FELUCCA_QUAD: engine 15 and its patch store's flash pair follow the CZ-1's (core.h ENGI_QUAD, storage.c): FELUCCA_CZ 1"
#endif
#include "eng_quad.c"           /* QUAD: a Digitone-style four-operator FM voice (docs/QUAD.md; its state is its own pool) */
#endif
#if FELUCCA_PROPHET
#if !FELUCCA_QUAD
#error "FELUCCA_PROPHET: engine 16 follows FM TONE's 15 (core.h ENGI_PROPHET): FELUCCA_QUAD 1"
#endif
#include "eng_prophet_test.c"   /* PROPHET: Melodee's Prophet-5 engine (docs/PROPHET.md; Kerem Kilic) */
#endif

/* the engines' runtime state of a part (Melodee's voice model). A part renders one engine at a time (an engine switch
 * fades the old one out first, voice.c engine_block), so their states share one block per part, cleared at every
 * switch: an engine finds its state as at power-on (the pool section is zeroed at boot). WHEEL's part and voices,
 * FM6's 16 voices and controllers, CZ-1's and P5's voices. The patch of a part (FM6's, the VA's) is not in here; nor is
 * GRAIN's state (its own). ENG_STATE_NPART: the parts that have one. ChoralRoot plays the chord part and the bass part only (cr_out.c: midi_event takes parts 0 / 1, cr_route.part is CR_PART_CHORD,
 * CR_PART_BASS or none), as eng_va.c's VA_NPART and eng_quad.c's QUAD_NPART. A part above them has no state (0, never
 * another part's): its engine takes no note and does no per-block work, so it is silent; it never gets a note there */
#ifndef ENG_STATE_NPART
#define ENG_STATE_NPART 2u       /* (ChoralRoot: chord, bass; 6.9 KB of pool a part: FM6's, the largest since PHYS's
                                  * three slots (12.9 KB) went with PHYS) */
#endif
static union {
    drw_part_t wheel;
    fm6_part_t fm6;
#if FELUCCA_CZ
    cz_part_t cz;                /* CZ-1's voices: six envelopes each, vibrato, noise */
#endif
#if FELUCCA_PROPHET
    p5_part_t p5;                /* P5's four voices, the wheel's block of samples, the DC blocker (672 B: the union
                                  * stays FM6's size) */
#endif
} eng_state[ENG_STATE_NPART] __attribute__((section(".pool")));
static drw_part_t *drw_of(const track_t *t)
{
    uint32_t part = (uint32_t)(t - trk);
    return part < ENG_STATE_NPART ? &eng_state[part].wheel : 0;
}
static fm6_part_t *fm6_part(uint32_t part) { return part < ENG_STATE_NPART ? &eng_state[part].fm6 : 0; }
#if FELUCCA_CZ
static cz_part_t *cz_part(uint32_t part) { return part < ENG_STATE_NPART ? &eng_state[part].cz : 0; }
/* CZ-1's envelope definitions of a part's tone (eng_cz.c cz_defs_t, 412 B a part): not in eng_state, where another
 * engine's bytes could pass for the tone's generation when a part comes to CZ-1 without a clear (power-on's loads) */
static cz_defs_t cz_defs_[ENG_STATE_NPART] __attribute__((section(".pool")));
static cz_defs_t *cz_defs(uint32_t part) { return part < ENG_STATE_NPART ? &cz_defs_[part] : 0; }
#endif
#if FELUCCA_PROPHET
static p5_part_t *p5_part(uint32_t part) { return part < ENG_STATE_NPART ? &eng_state[part].p5 : 0; }
_Static_assert(sizeof(p5_part_t) <= sizeof(fm6_part_t), "PROPHET's part state does not grow the parts' engine state");
#endif
static void eng_state_clear(uint32_t part)
{
    if (part < ENG_STATE_NPART)
        memset(&eng_state[part], 0, sizeof eng_state[part]);
}

/* a retired engine's slot (ANALOG, DIGITAL, SAMPLE, DRUM, and since 0.18 PHASE, LOFI, VOICE, TRIO, NOISE, PHYS: their
 * sources left the tree in 3y232, DIGITAL's converter to FM6 with them; GRAIN's with FELUCCA_GRAIN 0): no DSP, no
 * presets, silent, never offered
 * (eng_ok: PRESETS, the EDIT layer, the pickers skip it); its number stays reserved (the stores hold numbers) */
static void eng_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void eng_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
/* .edit: the ranges a retired engine's record is clamped to before cr_ui.c cu_load_user reads it (up_values: ANALOG's
 * CUT / RES / DRV .. carried to the VA by name need their 0..127): DIGITAL's empty slot's (fm4_convert.c ENG_FM4_GONE,
 * the retired slots' until 3y232), kept as they were */
static const engine_t ENG_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 7, 0, 0, 0}, {"-", F_INT, 0, 14, 1, 0, 0}, {"-", F_INT, 0, 14, 1, 0, 0},
             {"-", F_INT, 0, 14, 1, 0, 0}, {"-", F_PCT, 0, 127, 60, 0, 0}, {"-", F_INT, 0, 127, 60, 0, 0},
             {"-", F_PCT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = eng_gone_note_on,
    .render = eng_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
/* what the kept files read of SAMPLE: params.c enum_orig's alias table (no set: never an alias) */
#define SMP_NSETS 0
static const char *const SMP_ALL_NAMES[1] = {"-"};
static const uint8_t SMP_SET_ORIG[1] = {0};
/* what the kept files call of DRUM: voice.c (a part on engine 10 shares a lane's voice; never one here), upreset.c
 * (a DRUM part's grid into a user slot; never one here) */
#define ENG_DRUM ENG_GONE
static voice_t *drum_reuse(struct track *t, uint32_t note) { (void)t; (void)note; return 0; }
static uint32_t step_lanes(const step_t *s) { (void)s; return 0; }
static uint32_t step_accents(const step_t *s) { (void)s; return 0; }
/* what the kept files call of VOICE: fx.c c1_split_ok (both parts on VOICE share its breath noise: never one here) */
#define ENG_FORMANT ENG_GONE
#if FELUCCA_VA
#include "eng_va.c"             /* VA: ChoralRoot's four-oscillator virtual analog (deep pages, its own patch) */
#endif
#if FELUCCA_GRAIN
#include "eng_grain.c"          /* GRAIN: grains over material it renders itself (grain_src.c; docs/GRAIN.md); ChoralRoot
                                 * 0.22: engine 8 again, Felucca's player over RAM, no SAMPLE needed; the VA's filter,
                                 * LFO sync list and fine amounts (eng_va.c before it) */
#endif

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_GONE,                   /* 0: reserved (ANALOG, retired: its records load on the VA) */
    &ENG_GONE,                   /* 1: reserved (DIGITAL, retired in Felucca 1.0: its records load as the VA's init
                                  * sound since 3y232, cr_ui.c cu_load_user; FM6 conversions before) */
    &ENG_GONE,                   /* 2: reserved (PHASE, retired in 0.18) */
    &ENG_GONE,                   /* 3: reserved (LOFI, retired in 0.18) */
    &ENG_GONE,                   /* 4: reserved (SAMPLE, retired) */
    &ENG_GONE,                   /* 5: reserved (VOICE, retired in 0.18) */
    &ENG_GONE,                   /* 6: reserved (TRIO, retired in 0.18) */
    &ENG_WHEEL,                  /* 7 */
#if FELUCCA_GRAIN
    &ENG_GRAIN,                  /* 8 */
#else
    &ENG_GONE,                   /* 8: reserved (GRAIN, retired: FELUCCA_GRAIN 0) */
#endif
    &ENG_GONE,                   /* 9: reserved (PHYS, retired in 0.18) */
    &ENG_GONE,                   /* 10: reserved (DRUM, retired) */
    &ENG_GONE,                   /* 11: reserved (NOISE, retired in 0.18) */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#if FELUCCA_VA
    &ENG_VA,                     /* 13 + FELUCCA_SLICE (ENGI_VA; ChoralRoot: 13) */
#endif
#if FELUCCA_CZ
    &ENG_CZ,                     /* 13 + FELUCCA_SLICE + FELUCCA_VA (ENGI_CZ; ChoralRoot: 14) */
#endif
#if FELUCCA_QUAD
    &ENG_QUAD,                   /* 13 + FELUCCA_SLICE + FELUCCA_VA + FELUCCA_CZ (ENGI_QUAD; ChoralRoot: 15) */
#endif
#if FELUCCA_PROPHET
    &ENG_P5_TEST,                /* .. + FELUCCA_QUAD (ENGI_PROPHET; ChoralRoot: 16; Melodee's name for its engine) */
#endif
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1. The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    12,                          /* FM6 */
#if FELUCCA_QUAD
    ENGI_QUAD,                   /* FM TONE (code name QUAD; after FM6: the other FM engine) */
#endif
#if FELUCCA_VA
    ENGI_VA,                     /* VA */
#endif
#if FELUCCA_CZ
    ENGI_CZ,                     /* CZ-1 (after the VA since ChoralRoot 0.14; Melodee shows it after PHASE) */
#endif
#if FELUCCA_PROPHET
    ENGI_PROPHET,                /* PROPHET (after CZ-1, before WHEEL) */
#endif
    7,                           /* WHEEL */
#if FELUCCA_GRAIN
    8,                           /* GRAIN */
#endif
};

/* the engines one can pick (not the retired slots 0..6, 9..11; 8 only with FELUCCA_GRAIN), in ENGINE_ORDER: eng_ok(e), the n-th of them eng_vis(n), e's place among them eng_rank(e), the next / previous one
 * eng_step(e, dir) (wraps) */
/* the six retired in ChoralRoot 0.18 (PHASE, LOFI, VOICE, TRIO, NOISE, then PHYS: their records carry what an init sound
 * can honestly take: cr_ui.c cu_load_user) */
#define ENG_RETIRED_018 (1u << 2 | 1u << 3 | 1u << 5 | 1u << 6 | 1u << 11 | 1u << 9)
#define ENG_RETIRED (1u << ENGI_DIGITAL | 1u << 4 | (FELUCCA_GRAIN ? 0u : 1u << 8) | 1u << ENGI_DRUM | 1u | ENG_RETIRED_018)
static int eng_ok(uint32_t e) { return e < NENGINES && !((ENG_RETIRED >> e) & 1u); }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (a retired engine's place: FM6's) */
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
static const uint8_t TRK_DEF[NPART][3] = {
                                          {ENGI_FM6, 0, 0},           /* (no ANALOG: FM6's first sound) */
                                          {ENGI_FM6, 4, 0},           /* FM6 PAD (was DIGITAL PAD), LOFI PULSE LD,
                                                                       * DRUM KIT */
                                          {ENGI_FM6, 0, 0},           /* (no LOFI: FM6's first sound) */
                                          {ENGI_FM6, 0, 0}};   /* (no DRUM: FM6's first sound) */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
