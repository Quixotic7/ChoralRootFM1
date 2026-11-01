/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of VBUDGET units of sounding voices (voice.c; the voice model of Melodee,
 * Kerem Kilic's: an FM6 voice is one unit, any other engine's two, so the others keep their shared eight). Drums are the DRUM
 * engine or the SAMPLE engine's PERC set (General MIDI map) on any part.
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>
#include "cr_ram.h"              /* the CR_RAM_* build switches (code in RAM: docs/FX.md) */

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 16                /* voice slots per part (FM6 plays Dexed's 16; Melodee's voice model) */
#define NPOLY 8                  /* the voices of an engine without its own cap (its per-voice state: engines.c eng_state) */
#define VBUDGET 16               /* the sounding voices of all parts, in units: a voice of an engine capped above NPOLY
                                  * (FM6) takes one, any other two, so the others keep their shared eight. With the
                                  * second core alive (CR_CPU1, cr_cpu1.h) parts 0 and 1 each have their own VBUDGET */
#ifndef CR_CPU1
#define CR_CPU1 0                /* ChoralRoot's bass part on the second core (cr_cpu1.h, docs/DUALCORE.md): choralroot.c
                                  * and the emulator set 1; felucca.c, tests/hostsim.c and tests/regress.c build without */
#endif
#define NPART 4                  /* synth parts: tracks 1..4 */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
#define NSTEP 64
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 1          /* the SLICE engine (eng_slice.c), engine 13; FELUCCA_SLICE=0 builds without it */
#endif
#ifndef FELUCCA_FM4
#define FELUCCA_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#ifndef FELUCCA_VA
#define FELUCCA_VA 0             /* ChoralRoot's VA engine (eng_va.c, docs/VA.md): engine 13 + FELUCCA_SLICE; Felucca builds
                                  * without it (choralroot.c, the emulator and tests/regress.c set 1) */
#endif
#ifndef FELUCCA_SEQ
#define FELUCCA_SEQ 1            /* Felucca's keyboard, arpeggiator, sequencer and song chain (seq.c and its satellites);
                                  * ChoralRoot's units set 0: cr_out.c provides what the kept files call of them */
#endif
#ifndef FELUCCA_SAMPLE
#define FELUCCA_SAMPLE 1         /* the SAMPLE engine (eng_sample.c) and its ADPCM sets (build/gen/felucca_samples.h), the
                                  * user sample slots; ChoralRoot sets 0: engine 4 a retired slot (engines.c ENG_GONE) */
#endif
#ifndef FELUCCA_GRAIN
#define FELUCCA_GRAIN 1          /* the GRAIN engine (eng_grain.c; ChoralRoot 0.22: grains over material it renders itself,
                                  * engine 8; its filter, LFO sync and amount rule are the VA's, va_common.c, in every build) */
#endif
#ifndef FELUCCA_DRUM
#define FELUCCA_DRUM 1           /* the DRUM engine (eng_drum.c, drum_voice.c); ChoralRoot: 0, engine 10 retired */
#endif
#ifndef FELUCCA_ANALOG
#define FELUCCA_ANALOG 1         /* the ANALOG engine (eng_analog.c), engine 0; ChoralRoot: 0, engine 0 retired (the VA
                                  * replaces it: its records load on the VA, cr_ui.c cu_load_user) */
#endif
#ifndef FELUCCA_PHASE
#define FELUCCA_PHASE 1          /* the PHASE engine (eng_phase.c), engine 2; ChoralRoot: 0 since 0.18, engine 2 retired (its
                                  * records load as the VA's init sound, cr_ui.c cu_load_user; so do the next four's) */
#endif
#ifndef FELUCCA_LOFI
#define FELUCCA_LOFI 1           /* the LOFI engine (eng_lofi.c), engine 3; ChoralRoot: 0, engine 3 retired */
#endif
#ifndef FELUCCA_VOICE
#define FELUCCA_VOICE 1          /* the VOICE engine (eng_formant.c), engine 5; ChoralRoot: 0, engine 5 retired */
#endif
#ifndef FELUCCA_TRIO
#define FELUCCA_TRIO 1           /* the TRIO engine (eng_trio.c), engine 6; ChoralRoot: 0, engine 6 retired */
#endif
#ifndef FELUCCA_NOISE
#define FELUCCA_NOISE 1          /* the NOISE engine (eng_noise.c), engine 11; ChoralRoot: 0, engine 11 retired */
#endif
#ifndef FELUCCA_PHYS
#define FELUCCA_PHYS 1           /* the PHYS engine (eng_phys.c, phys_dsp.c, phys_symp.c), engine 9; ChoralRoot: 0 since 0.18,
                                  * engine 9 retired (its records load as the VA's init sound, as the five's above) */
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 0             /* Melodee's CZ-1 engine (eng_cz.c, docs/CZ1.md): engine 13 + FELUCCA_SLICE + FELUCCA_VA (14 on
                                  * ChoralRoot); Felucca builds without it (choralroot.c, the emulator and tests/regress.c set 1) */
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 0           /* ChoralRoot's QUAD engine (eng_quad.c, docs/QUAD.md): engine 13 + FELUCCA_SLICE + FELUCCA_VA +
                                  * FELUCCA_CZ (15 on ChoralRoot); Felucca builds without it (choralroot.c, the emulator and
                                  * tests/regress.c set 1) */
#endif
#ifndef FELUCCA_PROPHET
#define FELUCCA_PROPHET 0        /* Melodee's Prophet-5 engine (eng_prophet_test.c, docs/PROPHET.md): engine 13 + FELUCCA_SLICE +
                                  * FELUCCA_VA + FELUCCA_CZ + FELUCCA_QUAD (16 on ChoralRoot); Felucca builds without it
                                  * (choralroot.c, the emulator and tests/regress.c set 1). FELUCCA_P5_FACTORY (its file):
                                  * Sequential's 200 factory programs with it */
#endif
#define NENGINES (13 + FELUCCA_SLICE + FELUCCA_VA + FELUCCA_CZ + FELUCCA_QUAD + FELUCCA_PROPHET)   /* SLICE (13), VA, CZ-1,
                                                                                  * QUAD and PROPHET come last: the other
                                                                                  * engines keep their numbers */
#define ENGI_CZ (13u + FELUCCA_SLICE + FELUCCA_VA)   /* engines.c ENGINES[] (append-only): 14 on ChoralRoot (Melodee: 15) */
#if FELUCCA_QUAD
#define ENGI_QUAD (13u + FELUCCA_SLICE + FELUCCA_VA + FELUCCA_CZ)   /* engines.c ENGINES[]: 15 on ChoralRoot */
#endif
#if FELUCCA_PROPHET
#define ENGI_PROPHET (13u + FELUCCA_SLICE + FELUCCA_VA + FELUCCA_CZ + FELUCCA_QUAD)   /* engines.c ENGINES[]: 16 on ChoralRoot
                                                                                       * (Melodee: 19) */
#endif
#define ENGI_DIGITAL 1u          /* reserved without FELUCCA_FM4: never selectable (eng_ok), its sounds load as FM6 */
#define NENG_SHOWN (NENGINES - !FELUCCA_FM4 - !FELUCCA_SAMPLE - !FELUCCA_GRAIN - !FELUCCA_DRUM - !FELUCCA_ANALOG - \
                    !FELUCCA_PHASE - !FELUCCA_LOFI - !FELUCCA_VOICE - !FELUCCA_TRIO - !FELUCCA_NOISE - !FELUCCA_PHYS)
                                 /* the engines one can pick: PRESETS, the EDIT layer, the editor, in the display order
                                  * of engines.c ENGINE_ORDER (the retired slots are not among them) */
#if FELUCCA_VA && FELUCCA_CZ && FELUCCA_QUAD
#define UP_POOL 1                /* ChoralRoot 0.22: the sound pool (upool.c, storage.c sp_*): 128 slots shared by every
                                  * engine, a slot the record and its engine's patch */
#define UP_SLOTS 128u            /* user sounds (upreset.c, upool.c); 7-bit SysEx slot numbers: at most 128 */
#else
#define UP_POOL 0
#define UP_SLOTS 32u             /* user presets (upreset.c) */
#endif
#define UP_LEGACY 32u            /* the slots of the legacy objects (0.21's: OBJ_UPRESET0 / 1 and the engine stores) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_OFS, F_FMNOTE, F_FMFRQ,   /* FM6's pages: 0 at the middle of the range, the DX7 break point, an operator's frequency
                                 * (params.c param_format; F_FMFRQ: Melodee's operator pages, here a plain number) */
    F_TIMEI,                    /* F_TIME with 127 = "INF" (a release that holds, the Digitone's 0..126 + INF; QUAD's
                                 * filter REL until 2026-10-14, now F_INTI: its times are the Digitone's, not
                                 * F_TIME's); the editor protocol's fmt 17 */
    F_FIX2,                     /* a hi-res value in hundredths (FM TONE, the Digitone's two decimals): 4201 = "42.01",
                                 * a range below 0 signed ("+41.34", "-64.00"); its finest step 1, its whole-number step
                                 * 100; names[0] (if any) = the word for the top whole number (AMP REL: "INF" at
                                 * 127.00 and up); the editor protocol's fmt 18 */
    F_INTI                      /* a number with its max = "INF" (FM TONE's AMP DEC and REL, its filter REL: 0..126 +
                                 * INF, the Digitone's); the editor protocol's fmt 19 */
};
#define FMT_HI(f) ((f) == F_FIX2)   /* a hundredths format (cr_pages.c kn_whole: finest step 1, whole numbers 100) */

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: unused, kept for the formats / protocol */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4), 1 SEL (seq.c) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING (fx.c). Was G_DRCH, the GM drum part's MIDI
                                 * channel (inert since 1.0, never read); projects of formats before FUN7 load it
                                 * as ROOM (project.c proj_rtype_room) */
    G_DRLVL, G_DRREV,           /* inert (label "-", on no page): the GM drum part they set is gone; kept
                                 * because the ids and G_COUNT are fixed by the formats and the protocol (only
                                 * the import of an old project reads them: proj_drums_to_part) */
    G_COUNT
};

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_GRAIN 8u            /* GRAIN (eng_grain.c; ChoralRoot 0.22: back in slot 8 over its own material) */
#define GRAIN_BLOB_V1 20u        /* GRAIN's first build's patch record (version 1, 3y136): the pool and a restore take it
                                  * beside the 64-byte version 2 (3y139); eng_grain.c converts it on load */
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK 0..1, DRV 0}. 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = (int16_t)((e[7] < 0 ? 0 : e[7] > 127 ? 127 : e[7]) >> 5);   /* PERC -> KIT: STD HAND CYM H+CYM */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK: PUNCH, ROUND */
    e[7] = 0;
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
    int32_t plog;                /* the voice's own pitch offset without TUNE and bend (glide, LFO and ENV pitch, the
                                  * matrix's, unison detune), Q24 octaves: for engines that figure their pitch (FM6) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t pat;                 /* suggested pattern (PATTERNS[pat - 1], 0 = none): only a hint. A sound load
                                  * never touches the steps; SEQ > PATTERNS loads a pattern (ui.c pat_load). The
                                  * arp is the track's too: a preset does not set it */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define PAT(n) .pat = (n)

struct track;
/* ChoralRoot: deep editing. An engine with more parameters than its eight P_E publishes pages of four
 * (cr_pages.c shows them after ENGINE / EDIT 1 / EDIT 2 and before ENV; SELECT turns, OPT + SELECT jumps to the
 * next section). Its parameters live in a patch the engine owns (RAM per track); the patch travels with a user
 * slot as a blob (cr_bank.c / the VA patch store), factory presets carry one too. get / set run in the main loop;
 * the engine picks changes up in its block(). */
typedef struct {
    const char *title;           /* "OSC 1" (<= 7 chars) */
    param_desc_t col[4];         /* KNOB 1..4; label 0 = an empty column */
} eng_page_t;
/* ChoralRoot's editor (cr_edit.c, docs/EDITOR.md §4): an engine's screen plan (eng_deep_t.screens), in place of the
 * title rules for the groups it has screens for: a screen is one or two pages (rows A, B) under a band, its title and
 * each cell's style */
enum { ENG_B_NONE, ENG_B_ENV, ENG_B_FILTER, ENG_B_WINDOW, ENG_B_ALGO, ENG_B_ADE2,
       ENG_B_WAVE };             /* .. GRAIN: the buffers' envelopes, POS, SPRD and the grains (cr_wave_t, below) */
                                 /* the band: none, the AHDSR
                                    * (ATK DEC SUS REL), the filter's response (FREQ / CUT RESO / RES TYPE), the same
                                    * with the base-width window (BASE WIDTH), the algorithm (ALGO FDBK MIX), two
                                    * attack-decay-end envelopes (rows A and B: ATK DEC END LEV each) */
enum { ENG_C_RULE,               /* the editor's rule (the kind of the value: a bar, a wave, text ..) */
       ENG_C_BIG,                /* a number only, large (its whole name: "16.00") */
       ENG_C_RATIO,              /* a fraction "0.50/1.00" (its whole name) */
       ENG_C_HARM, ENG_C_DETUNE, /* the harmonics / detune glyph, pct the value */
       ENG_C_NUM,                /* the value as a number (signed when the range is), a small bar (centre-zero) */
       ENG_C_VAL,                /* the value's text without its unit ("2.1k"), a small bar */
       ENG_C_TEXT,               /* the value's text ("Free", "x16", "LP"), no bar */
       ENG_C_KNOB,               /* a knob (bipolar when the range is), the value's text */
       ENG_C_BAR,                /* a bar glyph, the number */
       ENG_C_WAVEPH,             /* this column (an LFO wave, CR_LW_* order) and the next (its start phase, its range a
                                  * cycle) as one double-width cell: the wave drawn from the phase, "Tri \267 90" (degrees) */
       /* the Prophet-5's controls as the cell's glyph (cr_screen.h CR_G_P5*), the value on the cell's row ("64", "On",
        * "Half", "1/2", "Low R"); no engine's plan uses them yet: the layout tool's mock-ups (docs/LAYOUT-TOOL.md) */
       ENG_C_P5KNOB,             /* the ticked dial (bipolar from 12 o'clock when the range is) */
       ENG_C_P5LED,              /* the LED button, no icon */
       ENG_C_P5LED_SAW, ENG_C_P5LED_TRI, ENG_C_P5LED_SQR,   /* .. with its wave's one-cycle icon over the cap */
       ENG_C_P5LED2,             /* the two-LED button (a parameter of two values: the minimum red, the maximum yellow) */
       ENG_C_P5SWITCH };         /* the lever of a three-position enum */
/* the layout tool's preview only (tools/layout/layout_web.c, cr_edit.c under CR_LAYOUT_PREVIEW): a design's sketch glyph
 * ("sketch:<name>", web/fm1sketch.js): the cell drawn with its label, value, bars and hot block and nothing in its
 * glyph box, which the page draws (docs/LAYOUT-TOOL.md "Sketch glyphs"); no build has it */
#define ENG_C_SKETCH (ENG_C_P5SWITCH + 1)
typedef struct {
    uint8_t sec;                 /* the group: eng_deep_t.section's index (0 OSC, 1 FILTER, 2 ENV, 3 LFO, 4 MOD) */
    uint8_t pg[2];               /* row A's page, row B's (0xFF: one row) */
    uint8_t band;                /* ENG_B_* */
    uint8_t letter;              /* 1: the active row's letter after the title ("SYN 1 \267 A") */
    const char *title;           /* the title line's right text, 0 = row A's page title */
    uint8_t cell[2][4];          /* ENG_C_* of each row's columns */
    const char *label[2][4];     /* the cells' labels, 0 = the editor's (cr_pages.c CP_DLABEL) */
} eng_screen_t;
#define ENG_BLOB_MAX 160u       /* the largest blob (CZ-1: 144; the VA's 110 and FM6's 128 keep their store records) */
/* ENG_B_WAVE's data (GRAIN, eng_grain.c grain_wave; drawn by cr_draw.c CR_W_WAVE through cr_screen.h's cr_wave_src): the
 * two buffers' envelopes, cols columns of (min, max) in 1/127 of full scale; the grains sounding (a position 0..255
 * across the buffer, its level 0..255: each reads A and B mixed); POS 0..127 as the voices read it (SCAN included),
 * SPRD 0..127, MORPH 0..127 (B's weight in the mix, as the latest note plays it); busy: a source is being rendered
 * (in place, the grains playing on); gen: the renders completed (low byte) */
#define CR_WAVE_GRAINS 16u
typedef struct {
    uint32_t cols;
    const int8_t *env[2];
    uint8_t n, gpos[CR_WAVE_GRAINS], glev[CR_WAVE_GRAINS];
    uint8_t pos, sprd, morph, busy, gen;
} cr_wave_t;
typedef struct {
    uint32_t npages;
    const eng_page_t *pages;
    uint8_t section[8];          /* first page of each section, in order; 0xFF ends the list */
    int32_t (*get)(const struct track *t, uint32_t page, uint32_t col);
    void (*set)(struct track *t, uint32_t page, uint32_t col, int32_t v);
    uint32_t blob_size;          /* the patch, packed (<= ENG_BLOB_MAX bytes: CZ-1's tone is 144) */
    void (*blob_get)(const struct track *t, uint8_t *out);
    void (*blob_set)(struct track *t, const uint8_t *in);          /* 0 or a bad blob = the init patch */
    void (*blob_preset)(struct track *t, uint32_t k);              /* factory preset k's patch (engine_t.presets[k]) */
    /* optional (0 = none): a mode-dependent descriptor of a column (the label and the value names to show; the
     * stored value and set() are the same), 0 = pages[page].col[col] (VA: WAVE / SHAPE in MORPH and NOISE mode) */
    const param_desc_t *(*desc)(const struct track *t, uint32_t page, uint32_t col);
    /* optional (0 = none): the modulation matrix's destination of a column, as the MOD pages' DST column numbers it
     * (its value), -1 = not a destination; page ENG_MOD_TRK: a track parameter, col = its P_ id (the editor's quick
     * mapping, docs/EDITOR.md) */
    int32_t (*mod_dst)(const struct track *t, uint32_t page, uint32_t col);
    /* optional (0 = none): the editor's screen plan (eng_screen_t above; QUAD's), nscreens of them in group order */
    const eng_screen_t *screens;
    uint32_t nscreens;
} eng_deep_t;
#define ENG_MOD_TRK 0xFFu
/* get / set's col | ENG_COL_RAW: the page's own value, where desc shows the cell as another one by a mode (FM TONE's
 * FILTER+ on a filter machine: the cell MORPH / LPF / Q, the type the SEL hold picker's, eng_quad.c quad_ix). Passed
 * only when desc gives the cell another descriptor (cr_edit.c ce_ftype), so an engine without one never sees it */
#define ENG_COL_RAW 0x80u
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON (up to NVOICE), 0 = NPOLY */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (DRUM): always POLY, no glide; amp ends its voices (eng_drum.c) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    const eng_deep_t *deep;      /* ChoralRoot: deep editing pages and the patch blob, 0 = the eight P_E only */
    /* optional: render with a stereo side (VA's SPREAD / USPREAD): out gets the mid as render() would, side the
     * side, (R - L) / 2, at the same scale; returns 1 if it added to side. voice.c calls it instead of render() and
     * fx.c mix_part plays the part as L = mid - side, R = mid + side (then LEVEL and the pan law as for a mono
     * part). Nothing added to side: the part is mono, bit for bit as render() */
    int (*render2)(struct track *t, voice_t *v, int32_t *out, int32_t *side, uint32_t n, const vmod_t *m);
    /* Melodee's voice model (voice.c), all optional: */
    /* the POLY voice (0..cap-1) for a new note, the engine's own choice (FM6: Dexed's) */
    uint32_t (*alloc)(struct track *t, uint32_t note);
    /* MONO / LEGATO / UNISON moved a sounding voice to a new note without a new attack */
    void (*legato)(struct track *t, voice_t *v);
    /* a key went down in MONO / LEGATO / UNISON, whether or not it takes the voice */
    void (*mono_key)(struct track *t, uint32_t note);
    /* the part's block after its voices (FM6: Dexed's DC filter); nr: voices rendered */
    void (*post)(struct track *t, int32_t *out, uint32_t n, uint32_t nr);
    /* asked after post when none of the part's voices was rendered: 1 = the part still sounds (P5: its output's
     * capacitor discharging, p5_post), the mix takes the block as if a voice had */
    uint32_t (*tail)(struct track *t);
    /* the voice cap of the part now, instead of poly */
    uint32_t (*cap)(const struct track *t);
    /* the voice budget units one of its voices takes now (default: 1 above NPOLY voices, else 2) */
    uint32_t (*units)(const struct track *t);
    /* optional: 1 = the voice would sound on after its release (QUAD's REL INF): trk_all_off (panic, all notes off, a
     * sound load) kills it instead of releasing it */
    int (*endless)(const struct track *t, const voice_t *v);
    /* optional (P5, 3y45; voice.c VOICE_STEAL_FADE): a voice of the part's own taken for a note that waits for it
     * (voice_alloc, the fade-and-defer steal) fades over the engine's own number of blocks, not one. steal: that
     * voice was just given up; fade: asked once a block while it fades (env_tick, stage 4): the level (Q15) this
     * block ends at, 0: this is its last block. An engine without them: one block, as ever */
#if FELUCCA_PROPHET
    void (*steal)(struct track *t, voice_t *v);
    int32_t (*fade)(struct track *t, voice_t *v);
#endif
    /* optional (P5 3y46, the VA 3y177; fx.c p5x_run, docs/DUALCORE.md "What each core runs"): with two cores the
     * chord part's voices are shared out between them, one voice weighing split x 10 us (the unit's worst: P5 44, the
     * VA 28). 0: the part renders whole on core 0. A voice's render must write only its own state and the buffers it
     * is given (each engine's audit: docs/PROPHET.md, docs/VA.md "Two cores") */
    uint8_t split;
} engine_t;
/* voice_start: what the voice it starts did just before (0 free, 1 released, 2 its key down: a steal or a move);
 * engine_t.note_on may read it */
static uint8_t voice_was;

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* bit l: that hit is accented (velocity 127) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent */
} step_t;
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
static uint32_t step_chance(const step_t *s) { return !s->probability ? 100u : s->probability <= 100u ? s->probability : 0u; }
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
#define MOTION_MAX 64u
/* Four tracks x64 steps fit one byte. Values retain their signed parameter range. */
typedef struct { uint8_t place, param; int16_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 260u, "motion disk layout");
#define CHAIN_ROWS 16u
typedef struct { uint8_t slot, repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    int8_t trim;                 /* a gain after P_LEVEL, LEVEL steps (0.5 dB): ChoralRoot's factory sound's own
                                  * (cr_bank.c), set on its load; 0 = none (user sounds, Felucca) */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[NVOICE];  /* keys held, in press order (as many as Dexed keeps voices for) */
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
    uint8_t foot, breath, porta; /* CC4 foot, CC2 breath, CC65 portamento on: FM6's DX7 controllers (mod.c) */
    int16_t bend_raw;            /* the pitch bend as sent (signed 14-bit): FM6 bends by its own range (its patch's) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
#include "cr_cpu1.h"             /* the second core and the ISR's stage profile (cr_dbg) */
#define TSEL (&trk[song.sel])    /* the selected track */

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
#define ST_NOW_MS() fm1_ms                /* (storage.c st_note, 3y48: the last erase's time) */
/* boot-loop guard (main.c, cr_bootguard.h): crash-type resets within 30 s of a boot counted; 2 -> SAFE MODE (no flash
 * object loads), 4 -> UBOOT; a power-on clears it */
#include "cr_bootguard.h"
struct bootguard bootguard __attribute__((section(".noinit")));
uint8_t cr_safe;                  /* this session runs in SAFE MODE (bootguard BOOT_SAFE): stores neither read nor written */
#define ST_BLOCKED() (cr_safe)    /* storage.c st_load / st_save, cr_ui.c's loop slots: off in SAFE MODE */
#define UAC_BLOCKED() (cr_safe)   /* usb.c: no USB audio stream in SAFE MODE */
