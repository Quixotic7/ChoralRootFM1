/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the screen's view-model. The firmware fills one cr_screen_t every frame from the engine and the
 * UI state; firmware/src/cr_draw.c renders it and never reads engine state. It mirrors the "screen" object of the
 * designer's JSON (../ChoralRootFM1Designer/FORMAT.md, design/choralroot-fm1-mockups.json): one panel kind, the
 * top line, the one-line footer, the ring and the message. tests/gen_cr_screens.py converts the 24 mock-up states
 * into a table of these (tests/cr_screens_gen.h). See CR_SCREENS.md.
 *
 * Rules for the producer: start from a zeroed struct every frame (cr_screen_clear: the draw cache hashes the
 * bytes, padding and the tails of the strings included), strings are NUL-terminated and cut to their arrays,
 * fractions are Q8 (CR_ONE = 1.0), colours are cr_col_t (CR_COL_NONE: the field's default). */
#pragma once
#include <stdint.h>

#define CR_ONE 256u                 /* Q8 fractions: pct, ring, squeeze, phase */

/* colours: ChoralRoot's seven (fixed RGB565 in cr_draw.c, not per palette), then the palette's tokens */
enum {
    CR_COL_NONE,                    /* the field's default (as the designer's colOf fallback) */
    CR_COL_WHITE, CR_COL_RED, CR_COL_BLUE, CR_COL_YELLOW, CR_COL_ORANGE, CR_COL_GREEN, CR_COL_GREY, CR_COL_BLACK,
    CR_COL_BG, CR_COL_SURF, CR_COL_TEXT, CR_COL_THEME, CR_COL_ACCENT, CR_COL_MID, CR_COL_DIM, CR_COL_LINE, CR_COL_REC,
    CR_COL_N
};

/* panel kinds (the device's subset of the designer's) */
enum { CR_K_NONE, CR_K_STRIPES, CR_K_CHORD, CR_K_PICKER, CR_K_METER, CR_K_KEYBOARD, CR_K_ARP,
       CR_K_PARAMS,                 /* (retired: the old sound pages; nothing draws it) */
       CR_K_GEEK, CR_K_TEXT, CR_K_BIG, CR_K_SCOPE,
       CR_K_EDIT8,                  /* the sound editor: two rows of four cells, an optional wide band over them */
       CR_K_STACK,                  /* the sound editor: N rows (1..8) of four cells under column headings */
       CR_K_KNOBROW,                /* a layer: a horizontal picker band over one row of four knob cells (cell[0]) */
       CR_K_LOOPMENU,               /* the looper's PLAY menu (docs/LOOPER-MODES.md): the slot strip, the record mode,
                                     * the middle (a big text, the record timeline, the step grid), the knob row */
       CR_K_FREE,                   /* the sound editor: a compiled layout's free screen (cr_layout.h; docs/LAYOUT-TOOL.md
                                     * "Step 4"): its elements drawn in the p5 style from the table, cell[lane][slot] every
                                     * lane's values (pct2: an led's on, a switch's position), `active` the lane on the
                                     * knobs, fl_eng / fl_scr the layout and screen */
       CR_K_ROUTE,                  /* the FX layer's Routing page (GitHub issue 21, second design): the signal flow as
                                     * boxes, Chain (IN, four boxes in a row, OUT) or Parallel (the three buses stacked
                                     * into a sum, the Filter after it); a held block lifted (rt_*) */
       CR_K_N };

/* CR_K_LOOPMENU's middle (lm_mid) */
enum { CR_LM_TEXT, CR_LM_TIMELINE, CR_LM_STEPS };
#define CR_LM_CUR_OFF 0x8000u       /* lm_pos (steps): the cursor's blink, off this frame (the producer's clock) */
#define CR_LM_PLAY(st) ((uint16_t)((((st) & 63u) + 1u) << 6))   /* lm_pos (steps): the playhead on the page's step st */
#define CR_LMC_COND 0x80u           /* lm_cell: the step has a condition */
#define CR_LM_NOW_MAX 16            /* lm_now: 2 + 4 + 11 - 1 characters and the 0 */
#define CR_LM_PLAYED(pos) ((((uint32_t)(pos) >> 6) & 127u))     /* .. that step + 1 (0: no playhead) */

/* the top line's icon: none = the bare Orchid line (mid at the left in 15 px, right at the right) */
enum { CR_ICON_NONE, CR_ICON_PLAY, CR_ICON_REC, CR_ICON_LOOP };
/* the corner dial's looks (dial_mode): the loop playing; REC armed (no loop yet: the track alone; an overdub armed: over
 * the playing arc); the first take recording (the track red); an overdub recording (the playing arc) */
enum { CR_DIAL_PLAY, CR_DIAL_ARMED, CR_DIAL_REC, CR_DIAL_OD };

/* an editor cell's glyph (the designer's params glyphs, drawn small); CR_G_NONE: a text cell */
enum { CR_G_NONE, CR_G_KNOB, CR_G_BAR, CR_G_WAVE, CR_G_SAW, CR_G_SQUARE, CR_G_STEPS, CR_G_DOTS,
       CR_G_MORPH,                  /* the VA's MORPH wave at pct = the position (Q8 of 255 = 0..127: sine 0, triangle 24,
                                     * saw 48, ramp 72, square 96, a pulse narrowing to ~5 % at 127), crossfaded */
       CR_G_NOISE,                  /* a noise: pct 0..84 WHITE (dense jitter), 85..169 BROWN (a wandering line),
                                     * 170..255 VINYL (sparse spikes on a faint line) */
       /* the parameter pictograms (FORMAT.md "cell glyphs"): flat 2 px strokes in the cell colour */
       CR_G_ROOM,                   /* a room in one-point perspective: the box fixed, its far wall 22 % (pct 0) .. 70 % (1) of it */
       CR_G_MOON,                   /* a moon phase: lit 8 % (pct 0, a thin crescent) .. full (1) */
       CR_G_ECHOES,                 /* a bar and its repeats: pct the spacing, pct2 the feedback (each 0.2 + 0.75 pct2 of
                                     * the one before); pct and pct2 both 0: the baseline alone (no grid) */
       CR_G_LFO,                    /* a sine: pct the rate (1 .. 5 cycles), pct2 the depth (nearly flat .. full) */
       CR_G_CLIP,                   /* one sine cycle driven into a clipper (gain 1 .. 10), dashed clip lines */
       CR_G_MIX,                    /* dry / wet: an outlined square behind one filled from the bottom to pct */
       CR_G_GATE,                   /* a pulse on a baseline, 10 .. 100 % of the box wide */
       CR_G_RANGE,                  /* a line with end stops, a thick segment over 10 .. 100 % of it */
       CR_G_ARROW,                  /* pct < 1/4 up, < 1/2 down, < 3/4 up and down, else three dots (random) */
       CR_G_SHIFT,                  /* five staff lines, a square on line round(4 pct) from the bottom */
       /* QUAD's (docs/QUAD.md): */
       CR_G_HARM,                   /* one cycle of the carriers' wave: pct 0.5 a sine, -> 1 the odd series (square-ish),
                                     * -> 0 all harmonics (saw-ish): harmonics 2..7 at 1/k, normalised */
       CR_G_DETUNE,                 /* a flat line breaking up: 16 segments, the joints jittered by pct (a fixed table),
                                     * past 0.3 every third segment gone, past 0.7 every other */
       CR_G_LFOWAVE,                /* one LFO cycle of `wave` (CR_LW_*; two cycles in a CR_CF_SPAN2 cell), started pct of
                                     * a cycle later (the start phase), a tick at its start */
       CR_G_RATIO,                  /* not a picture: B1 / B2 drawn as a fraction (numerator over a divider over the
                                     * denominator, 13 px), pct and pct2 the two ratios in quarters (1..255 = 0.25..63.75,
                                     * each "%u.%02u"); pct2 0: the value is drawn as CR_CF_BIG */
       /* the FX layer's (0.18, docs/FX.md), in cr_picto's manner */
       CR_G_TAIL,                   /* a reverb's decay: a struck edge and its falling curve, short and steep (pct 0) ..
                                     * long (1: nearly level, the "Inf" end) */
       CR_G_PRE,                    /* pre-delay: a struck bar, a gap of pct, then the reverb as a falling wedge */
       CR_G_CUT,                    /* a low cut: the response rising to its corner (pct: from the left edge to 60 % of the
                                     * box), level after it */
       CR_G_WIDTH,                  /* stereo width: two bars moving apart from the centre mark (pct 0: on it) */
       CR_G_SPREAD,                 /* two rows of echoes: the upper one's spacing pct, the lower one's a part of it
                                     * (pct2 0: the same .. 1: half) */
       CR_G_SYNC,                   /* pct < 1/2: a clock face (free: milliseconds); else two notes on a beam (the tempo) */
       CR_G_PING,                   /* between two walls: pct < 1/2 two level paths (stereo), else one bouncing down
                                     * from wall to wall (ping-pong) */
       /* the Prophet-5's controls as an edit8 cell's glyph (docs/EDITOR.md section 9 "A cell"; cr_draw.c cr_p5glyph, on the p5
        * primitives), the value on the cell's row; sized to the cell's glyph box */
       CR_G_P5KNOB,                 /* the ticked dial (11 ticks over 270 degrees, the dark face, its rim), the pointer at
                                     * pct; CR_CF_BIP: tick 5 lit (12 o'clock). No value inside: the cell's row has it */
       CR_G_P5LED,                  /* the LED button: the cap with its black window, the red LED lit (pct >= 128) or a
                                     * dim dot; pct2 the one-cycle icon over it (CR_P5I_*); a 22 px box (under a
                                     * band): the cap only */
       CR_G_P5LED2,                 /* the two-LED button (red: the minimum, left; yellow: the maximum, right), the
                                     * current one lit (pct >= 128: the right); the two value names over them when
                                     * they fit, NUL-separated in cr_cell_t.label after the label's own NUL */
       CR_G_P5SWITCH,               /* the lever in a slot of three positions, the lever at pct (0, 1/2, 1) */
       CR_G_N };
/* CR_G_P5LED's icon (cr_cell_t.pct2): one cycle of the wave as the panel prints it over the button */
enum { CR_P5I_NONE, CR_P5I_SAW, CR_P5I_TRI, CR_P5I_SQUARE, CR_P5I_SINE, CR_P5I_NOISE };

/* CR_G_LFOWAVE's wave (cr_cell_t.wave) */
enum { CR_LW_TRI, CR_LW_SINE, CR_LW_SQUARE, CR_LW_SAW, CR_LW_RAMP, CR_LW_EXP, CR_LW_RANDOM, CR_LW_N };

/* the editor's wide band (CR_K_EDIT8) */
enum { CR_W_NONE, CR_W_ENV, CR_W_FILTER, CR_W_DX, CR_W_CZ,     /* CR_W_DX: a DX7 envelope (FM6), 4 rates / 4 levels;
                                                                * CR_W_CZ: a CZ-1 envelope, 8 steps, SUS, END */
       CR_W_ALGO,                   /* QUAD: the algorithm diagram (operators C A B1 B2, the arrows, feedback, X / Y) */
       CR_W_ADE2,                   /* QUAD: the operator envelopes A and B side by side, each with its level bar */
       CR_W_WAVE };                 /* GRAIN: the source buffers' envelopes with the grains moving over them (wv[0]: 0 A,
                                     * 1 B, 2 both morphed; wv[1] the part; wv[2] a frame count: it redraws every frame);
                                     * the data from cr_wave_src (core.h cr_wave_t), 0: an empty band */
static const cr_wave_t *(*cr_wave_src)(uint32_t part);   /* set by cr_edit.c (eng_grain.c grain_wave) */
#define CR_ED_ROWS 8u               /* a stack's rows at most (the mod matrix) */
#define CR_CF_ON 1u                 /* a cell: shown (an empty cell draws nothing) */
#define CR_CF_PCT 2u                /* .. pct is shown (a text cell: a small bar) */
#define CR_CF_BIP 4u                /* .. pct is centre-zero (128 = 0); a CR_G_KNOB: the arc from 12 o'clock, a centre mark */
#define CR_CF_BIPOLAR CR_CF_BIP
#define CR_CF_DIM 8u                /* .. dim: drawn in the grey (knobrow: a value that cannot change now) */
#define CR_CF_MARK(c) ((uint8_t)((c) << 4))   /* .. a modulation mark, a 4 px square at its top right in colour c
                                     * (a named CR_COL_*, 0 none): the matrix modulates its parameter */
#define CR_CF_MARKCOL(f) (((uint32_t)(f) >> 4) & 15u)
#define CR_CF_BIG 0x100u            /* .. edit8 / stack: a number-only cell: the value 7 px larger where glyph and value
                                     * would be, no glyph, no bar (too wide: 2 px smaller at a time, never ellipsised) */
#define CR_CF_SPAN2 0x200u          /* .. edit8 / stack: two columns wide (from a cell in column 0..2): the cells after it
                                     * in the row shift a column right (cell index != column then; hot_c is the cell's
                                     * index, the colour its first column's) */

/* animations in progress (cr_screen_t.anim); each is a pure function of the fields and cr_draw's anim_ms, the time
 * since the change that started it (cr_draw.c CR_*_MS: the durations) */
#define CR_A_SQUEEZE 1u             /* chord: `from` squeezes to a thin column, `name` stretches out of it */
#define CR_A_SLIDE 2u               /* picker: the selection slides in from sel - slide */
#define CR_A_FILL 4u                /* meter: the stripes fill one by one from pct_from to pct */
#define CR_A_STRIPES 8u             /* stripes: they slide, a cycle every `period_ms` from `phase` */
#define CR_A_SWEEP 16u              /* stripes: they are swept off to the right (the first chord) */
#define CR_A_INTRO 32u              /* stripes: power-on, the bands slide in from the left one after another, the name lands */
#define CR_A_LIFT 64u               /* route: the held block rises (rt_held), or the one let go lands (rt_drop) */

#define CR_PICK_MAX 8u              /* picker items held (a window of the list around the selection) */
#define CR_NOTES_MAX 8u
#define CR_KEYS 27u                 /* key index 0..26 = F3..G5 */
#define CR_LINES_MAX 6u
#define CR_WAVE_N 240u              /* scope: one sample per column */

typedef struct { char root[4], quality[6], sup[12]; uint8_t col_root, col_quality, col_sup; } cr_name_t;   /* (sup: the
                                                             * engine's CR_SUP_MAX, cr_ui.c asserts it: "add4 JAZZ",
                                                             * "add9add4" whole) */
typedef struct { char t[6]; uint8_t col, mark; } cr_note_t;                    /* "C#5", its colour, a block under it */
typedef struct {                                                                /* an editor cell (KNOB 1..4) */
    char label[14], value[12];      /* label: edit8 only (a stack's columns have headings), "Wave \267 Phase" fits;
                                     * value: "16.00" fits (a CR_G_RATIO pair's two parts are pct / pct2), and
                                     * "Split Bass" (the KEY layer's Single; 10 until 0.17) */
    uint16_t flags;                 /* CR_CF_*, the mark's colour in bits 4..7 */
    uint8_t glyph;                  /* CR_G_* */
    uint8_t pct;                    /* Q8 of 255: the glyph's / bar's fill (square: the duty) */
    uint8_t pct2;                   /* Q8 of 255: a pictogram's second value (echoes: feedback, lfo: depth; edit8
                                     * tall 2: the peak tick, 0 none) */
    uint8_t wave;                   /* CR_G_LFOWAVE: CR_LW_* */
} cr_cell_t;                        /* 32 bytes, no padding */
typedef struct { char t[32]; uint8_t px, col, bold, center;
                 uint8_t cur; } cr_line_t;       /* a text line (px 0 = 12). cur: a cursor under character cur - 1 of a
                                                  * left-aligned line, in the
                                                  * title's colour, steady; 0: none, every pixel as before (naming) */

typedef struct {
    uint8_t kind;                   /* CR_K_* */
    uint8_t anim;                   /* CR_A_* in progress */

    /* the top line (header) */
    uint8_t header;                 /* 1 = shown */
    uint8_t icon;                   /* CR_ICON_* */
    uint8_t batt;                   /* 0..4 (4 = charging), 255 = none */
    uint8_t mid_col, right_col;
    char mid[24], right[16];

    /* the one-line footer (empty: none; the panel then runs to the bottom) */
    char footer[64];

    /* the ring round the edge (only where the loop is the whole screen: the count-in, undo; calibration) */
    uint8_t ring_on, ring_rec, ring_col, message_col;
    uint16_t ring;                  /* Q8 progress (0: the dotted track only) */
    /* the corner dial: every loop state but the count-in (no ring), a 16 px ring at the right end of the top line
     * (centre 229, 12, r 8, 3 px: the dotted track, the progress red from 12 o'clock, the REC dot in its middle); the
     * header's right text moves 22 px left */
    uint8_t dial_on, dial_pulse;    /* dial_pulse: the downbeat's frame (~100 ms): the arc drawn 5 px wide */
    uint8_t dial_mode;              /* CR_DIAL_*: CR_DIAL_REC draws the track red */
    uint8_t dial_dot;               /* the REC dot lit this frame (the REC LED's twin: lit armed, blinking capturing) */
    uint16_t dial;                  /* Q8 progress (the loop's fraction or the take's, as ring; 0: no arc) */
    char message[24];

    /* panel: chord, arp, keyboard (with a root), geek */
    cr_name_t name;
    cr_name_t from;                 /* CR_A_SQUEEZE: the name before (empty: stretch out of nothing) */
    uint16_t squeeze;               /* Q8: a forced squeeze factor (0 = none): one animation frame of the designer */
    uint8_t block;                  /* chord / big: the panel filled with this colour (NONE = no block) */
    uint8_t line_col;
    char line[32];                  /* chord: the voicing line; arp: the rate line */
    cr_note_t note[CR_NOTES_MAX];   /* chord: the notes line; arp: the notes on the line; geek: the notes list */
    uint8_t n_notes;
    int8_t pos;                     /* arp: the sounding note (-1 none) */
    uint8_t hop_col;                /* arp: the dotted hop arc */
    uint8_t numeral_on;             /* chord / arp: the layout with a numeral (0.22, the KEY layer's Numerals with Key Mode
                                     * on): the arp's name 6 px lower; 0: every pixel as before */
    char numeral[6];                /* .. the sounding chord's Roman numeral ("V", "#vii\260"), blue, 16 px, centred
                                     * above the name; empty: none (nothing sounding), the layout stays */

    /* panel: picker */
    uint8_t n_items, item0, sel;    /* the list's length, the index of item[0] in it, the selection (an index in it) */
    uint8_t orient;                 /* 0 vertical, 1 horizontal (left / right) */
    int8_t slide;                   /* CR_A_SLIDE: the selection came from sel - slide */
    uint8_t col;                    /* picker / meter / keyboard title / params title / big value colour */
    uint8_t title_col;
    char item[CR_PICK_MAX][24];
    char label[32];                 /* picker / meter / big: the line under it */
    char value[24];                 /* picker: the value under the item; meter / big: the huge value */
    char title[24];                 /* picker / meter: top-left; keyboard / text / params / stripes: the title */
    uint8_t size;                   /* px of the huge type (0: the kind's default) */

    /* panel: meter */
    char sub[32];                   /* under the value, in its colour; loopmenu: the line under the big text ("the first
                                     * chord starts the take"), the elapsed "0:07", the step's chord name "Dm7" */
    uint16_t pct, pct_from;         /* Q8: the stripes filled; CR_A_FILL: from */
    uint8_t segments, thick;        /* stripes (0 = 12), their height (0 = 14) */
    uint8_t sub_mark;               /* a small square after `sub` (an overwritten factory preset, docs/PRESETS.md) */
    uint8_t star;                   /* a favourite's star before the sound's name, the two centred together (0.22,
                                     * docs/PRESETS.md "Favourites"): the meter's `sub`, the picker's `value`. 0: every
                                     * pixel as before */

    uint8_t label_fav;              /* the meter's `label` in the star's colour: a pool's read-out while the favourites
                                     * filter is on (its place among the favourites, or "no favorites: all"). 0: every
                                     * pixel as before */

    /* panel: stripes */
    uint8_t bands[4], n_bands, band, gap;   /* colours (top first), band height, the gap between */
    uint8_t title_px;               /* stripes: the name's px (0 = 34); keyboard: the title's (0 = 15) */
    int16_t skew;                   /* Q8: x shift per row (0: flat) */
    int16_t title_y;                /* stripes: the name's baseline from the panel top (0 = above the bands) */
    uint16_t phase;                 /* Q8: 0..1 slides them 40 px */
    uint16_t period_ms;             /* CR_A_STRIPES: a cycle (one bar at the BPM) */

    /* panel: keyboard, geek */
    uint32_t lit;                   /* bit k: key k lit */
    uint8_t lit_col[CR_KEYS];       /* its colour (NONE: white keys theme, black keys accent) */
    char key_label[CR_KEYS][3];     /* text printed on key k */

    /* panel: edit8, stack (the sound editor, full screen: header 0; its title line is `title` in `title_col`,
     * `page` right-aligned). No motion: a change is drawn at once (docs/EDITOR.md "Responsiveness").
     * knobrow (a layer): the picker's fields (items, sel, col, label, value; the slide) for the band, cell[0][0..3]
     * the knobs' cells (CR_CF_ON off: a dim dash), hot_r 1 / hot_c the cell just turned, hot_col its block;
     * kr_band 1: the band is the keyboard instead of the picker (KEY: lit / lit_col / key_label, `label` top left);
     * kr_band 2: the arp (PERF while notes are stepped: name, note[] / n_notes, pos, hop_col as the arp screen's,
     * fitted to the band; `label` top left) */
    uint8_t kr_band;                /* knobrow: 0 the picker, 1 the keyboard, 2 the arp */
    char page[16];                  /* edit8 / stack: the title line's right text ("OSC 2 \267 A") */
    cr_cell_t cell[CR_ED_ROWS][4];  /* rows of four cells (edit8: 1..2 rows) */
    char head[4][10];               /* stack: the column headings */
    char rlabel[CR_ED_ROWS][3];     /* stack: the row labels */
    uint8_t n_rows, active;         /* rows shown; the row on KNOB 1..4 (knob colours, bars) */
    uint8_t hot_r, hot_c;           /* the cell just turned (a filled block behind its value): row + 1 (0: none), column */
    uint8_t hot_col;                /* .. the block's colour (NONE: the knob's; a modulation source's while mapping) */
    uint8_t wide;                   /* edit8: CR_W_* */
    uint8_t tall;                   /* edit8: one row of tall level bars over the whole panel: 1 the oscillator mixer
                                     * (knob colours by `active`, the knob bars under them); 2 a read-out (Geek Out 2:
                                     * bright, no knob bars; a cell's pct2, when not 0, a peak tick across its well;
                                     * CR_CF_DIM: the cell in the grey) */
    uint8_t fl_eng, fl_scr;         /* free: the compiled layout and its screen (cr_layout.c: index + 1; 0 none) */
    uint8_t fine;                   /* edit8 / stack: SHIFT on (OPT snap): "snap" small in the title line (2: the
                                     * lane has a ratio pair, SHIFT steps its slow hand B1: "snap \267 B1");
                                     * `batt` (0..4, 4 = charging; 255 = none): edit8 / stack draw the header's battery
                                     * at the title line's right end (the MIX screens) */
    uint8_t wv[20];                 /* env: a h d s r (Q8 of 255), the lit segment + 1 (1 A .. 5 R, 0 none), 1 = REL INF (6: the tail flat);
                                     * filter: cut res (Q8 of 255), ftype (0..127: 0 LP, 32 BP, 64 HP, 96 NOTCH,
                                     * crossfaded between, 127 back toward LP), drive;
                                     * dx: R1..R4, L1..L4 (0..99, the DX7's), the lit segment (1..4, 0 none), 1 = a
                                     * pitch EG (levels round 50: a centre line);
                                     * cz: R1..R8 (0..7), L1..L8 (8..15) (0..99, the CZ-1's panel values), SUS (16: the
                                     * step 0..7, 8 none), END (17: 0..7), the lit step (18: 1..8, 0 none);
                                     * filter (QUAD's base-width window, 4..6): shown (4: 1, 0 none), base (5), width
                                     * (6) (Q8 of 255 of the band's width; the window runs base .. base + width);
                                     * QUAD's type (7): 0 the VA's (the position's name), 1 OFF (a flat line),
                                     * 2 12 dB ("LP12" "HP12"), 3 24 dB ("LP24": twice the slope);
                                     * algo: the algorithm (0: 1..8), feedback (1: 0..255, the loop's stroke 1..3 px),
                                     * mix (2: 0..255, 128 = X and Y alike, below X brighter, above Y);
                                     * ade2: A's attack decay end level (0..3), B's (4..7) (0..255), the lit segment
                                     * (8: 1..8 = A's attack decay end level, B's; 0 none) */
    char foot[48];                  /* stripes: the bottom line */

    /* panel: text, geek (lines: geek's status lines are lines[0..1].t) */
    cr_line_t lines[CR_LINES_MAX];
    uint8_t n_lines;

    /* panel: scope (the master output, triggered; -127..127 = the panel's half height; hashed with the rest, so a
     * moving trace redraws and a still one costs nothing) */
    int8_t wave[CR_WAVE_N];

    /* panel: loopmenu (CR_K_LOOPMENU, the PLAY menu, docs/LOOPER-MODES.md): the knobrow's picker band = the record mode
     * (items / sel / col as a knobrow; the slide), cell[0][0..3] = Quantize Count-in Level Keys (as a knobrow's; hot_r /
     * hot_c), the slot strip above the band, the middle under it: a big text (`title` in title_col, NONE red; `sub`
     * under it) or a timeline or the step grid (`sub` the cursor step's chord at the left under it, and after a tab in `sub` the step's voicing, blue: "Dm7\t+3", `value` the right
     * text "step 7/16"), `foot` the bottom line (the knob row then 64 px). The top line is the ordinary header.
     * lm_lane .. lm_erase1 (lm_cell, lm_bar and lm_ed among them) are hashed apart (cr_draw.c): a playhead, a new mark or the cursor's blink redraws only the
     * middle's strips (the cursor: its row's) */
    uint64_t lm_lane[5];            /* timeline: event marks per lane (0 .. 3), 64 columns over the bars shown (64 /
                                     * lm_bars columns a bar); steps: lm_lane[0] = the filled steps of the page, bit s =
                                     * step s (lm_beats a bar), lm_lane[1] = the steps a longer note still sounds in,
                                     * lm_lane[2] = the steps AFTER the cursor's that its note sounds on into (the white
                                     * length bar's further cells), lm_lane[3] / [4] = the low / high bit of a step's
                                     * ratchet (1 .. 3 = x2 .. x4: that many ticks at the cell's top) */
    uint8_t lm_cell[64];            /* steps: a filled step's look: bits 0-3 its velocity as a level 1 .. 15 (12 = the
                                     * default 100: the role colour; lower cooler and darker, higher hotter; 0: no
                                     * velocity given: the role colour), bits 4-6 its chance (0 = 100 %, 1 .. 7 = 90 75
                                     * 60 50 40 25 10 %), CR_LMC_COND: it has a condition. Chance under 100 %: a yellow
                                     * strip down the cell's right side, dimmer with the chance; a condition: the strip
                                     * blue (cr_loop.h crl_edmap_t.cell: the same byte) */
    uint8_t lm_bar;                 /* steps: the white length bar on the cursor's own cell, in quarters of its width:
                                     * 0 none (an empty step), 1 .. 3 a part of a step, 4 the whole cell */
    uint8_t lm_ed;                  /* steps: the loop editor: the cursor's blink is bright / dim, never gone */
    uint16_t lm_pos;                /* timeline: the playhead, Q8 bars from the left edge (0..lm_bars*256); steps: the
                                     * cursor's step index on the page (0..63), | CR_LM_CUR_OFF: the cursor not drawn
                                     * (its blink), | CR_LM_PLAY(st): the step being played (the loop editor) */
    uint16_t lm_erase0, lm_erase1;  /* Replace: the held span, Q8 bars (both 0: none): the marks of the older lanes in it
                                     * are struck (grey, a red x), on a dark band */
    uint16_t lm_used;               /* slots holding a loop, bit k (0..9) */
    uint8_t lm_slot, lm_jump;       /* the selected slot 0..9; Advance's target + 1 (0: none): the hop arrow */
    uint8_t lm_mid;                 /* CR_LM_TEXT, CR_LM_TIMELINE, CR_LM_STEPS */
    uint8_t lm_bars, lm_beats, lm_free;   /* timeline: bars shown 1..8, beats a bar (0: 4), free = the bars appear as they
                                     * pass (a bar 1/4 of the strip, the current one up to the playhead); steps: the
                                     * page's bars 1..4 (a row each) and lm_beats the steps a bar (sixteenths: 16, 12
                                     * in 3/4 and 6/8; 0: 16; lm_bars * steps <= 64) */
    uint8_t lm_nlane, lm_newlane;   /* lanes in use 0..4 (lane 0 the oldest, dimmer); the take's lane + 1 (bright), 0 none */
    uint8_t lm_dirty;               /* the slot + 1 whose loop has changes not saved yet (0: none): a small mark in its
                                     * cell's corner (saved by the global save or by leaving the slot) */
    uint8_t lm_steps;               /* steps: the cells of the page that exist (the loop editor: a loop ends inside the
                                     * page's last bar); 0: all of them */
    char lm_now[CR_LM_NOW_MAX];     /* the loop editor while its loop plays: the chord / note the loop struck last
                                     * ("G#madd4 JAZZ" the longest), blue, at the LEFT of the picker band's row, where a
                                     * left neighbour would stand (not drawn then); the band's item does not move. "":
                                     * none. Hashed apart (cr_draw.c): a new name redraws its strip (rows 40 .. 79) only */

    /* panel: route (CR_K_ROUTE, the FX layer's Routing page; cr_ui.c builds it from cr_fx.c xfp[XP_ORDER] /
     * xfp[XP_RMODE]). A block: 0 Chorus, 1 Delay, 2 Reverb, 3 Filter (cr_fx.c XFE_*, cr_ui.c asserts it). `label` is
     * the line over the picture ("fx \267 on \267 chain"). The slide (CR_A_SLIDE, slide != 0): each block moves from
     * its place in rt_from to its place in rt_order; the lift (CR_A_LIFT): rt_held rises, rt_drop lands */
    uint8_t rt_order[4];            /* the blocks by position, left to right (Parallel: the three effects top down in
                                     * this order, the Filter after the sum wherever it stands) */
    uint8_t rt_from[4];             /* CR_A_SLIDE: the order before the move */
    uint8_t rt_mode;                /* 0 Parallel, 1 Chain (xfp[XP_RMODE]) */
    uint8_t rt_held, rt_drop;       /* the block held (its black key down) + 1, the block let go and landing + 1; 0 none */
    uint8_t rt_fx_on, rt_filter_on; /* FX off: the whole picture grey; the Filter Off: its box hollow */
} cr_screen_t;

/* the filter's FTYPE position's name (eng_va.c's names): "LP" "BP" "HP" "NOTCH" at 0 32 64 96, "LP>BP" .. "NT>LP"
 * between */
static inline const char *cr_ftype_name(uint32_t v)
{
    static const char *const N[8] = {"LP", "LP>BP", "BP", "BP>HP", "HP", "HP>NT", "NOTCH", "NT>LP"};
    v &= 127u;
    return N[(v >> 5) * 2u + ((v & 31u) ? 1u : 0u)];
}

static inline void cr_screen_clear(cr_screen_t *s)
{
    uint8_t *p = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        p[i] = 0;
    s->batt = 255u;
    s->pos = -1;
}
