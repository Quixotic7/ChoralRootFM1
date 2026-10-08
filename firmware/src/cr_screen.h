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
       CR_K_N };

/* the top line's icon: none = the bare Orchid line (mid at the left in 15 px, right at the right) */
enum { CR_ICON_NONE, CR_ICON_PLAY, CR_ICON_REC, CR_ICON_LOOP };

/* an editor cell's glyph (the designer's params glyphs, drawn small); CR_G_NONE: a text cell */
enum { CR_G_NONE, CR_G_KNOB, CR_G_BAR, CR_G_WAVE, CR_G_SAW, CR_G_SQUARE, CR_G_STEPS, CR_G_DOTS,
       CR_G_MORPH,                  /* the VA's MORPH wave at pct = the position (Q8 of 255 = 0..127: sine 0, triangle 24,
                                     * saw 48, ramp 72, square 96, a pulse narrowing to ~5 % at 127), crossfaded */
       CR_G_NOISE,                  /* a noise: pct 0..84 WHITE (dense jitter), 85..169 BROWN (a wandering line),
                                     * 170..255 VINYL (sparse spikes on a faint line) */
       /* the parameter pictograms (FORMAT.md "cell glyphs"): flat 2 px strokes in the cell colour */
       CR_G_ROOM,                   /* a room in one-point perspective: the far wall 70 % (pct 0) .. 22 % (1) of the box */
       CR_G_MOON,                   /* a moon phase: lit 8 % (pct 0, a thin crescent) .. full (1) */
       CR_G_ECHOES,                 /* a bar and its repeats: pct the spacing, pct2 the feedback (each 0.2 + 0.75 pct2 of
                                     * the one before); pct and pct2 both 0: the baseline alone (no grid) */
       CR_G_LFO,                    /* a sine: pct the rate (1 .. 5 cycles), pct2 the depth (nearly flat .. full) */
       CR_G_CLIP,                   /* one sine cycle driven into a clipper (gain 1 .. 10), dashed clip lines */
       CR_G_SPRING,                 /* a coil (6.5 zigzag turns between two short ends); pct ignored */
       CR_G_MIX,                    /* dry / wet: an outlined square behind one filled from the bottom to pct */
       CR_G_GATE,                   /* a pulse on a baseline, 10 .. 100 % of the box wide */
       CR_G_RANGE,                  /* a line with end stops, a thick segment over 10 .. 100 % of it */
       CR_G_ARROW,                  /* pct < 1/4 up, < 1/2 down, < 3/4 up and down, else three dots (random) */
       CR_G_SHIFT,                  /* five staff lines, a square on line round(4 pct) from the bottom */
       CR_G_N };

/* the editor's wide band (CR_K_EDIT8) */
enum { CR_W_NONE, CR_W_ENV, CR_W_FILTER, CR_W_DX, CR_W_CZ };   /* CR_W_DX: a DX7 envelope (FM6), 4 rates / 4 levels;
                                                                * CR_W_CZ: a CZ-1 envelope, 8 steps, SUS, END */
#define CR_ED_ROWS 8u               /* a stack's rows at most (the mod matrix) */
#define CR_CF_ON 1u                 /* a cell: shown (an empty cell draws nothing) */
#define CR_CF_PCT 2u                /* .. pct is shown (a text cell: a small bar) */
#define CR_CF_BIP 4u                /* .. pct is centre-zero (128 = 0) */
#define CR_CF_DIM 8u                /* .. dim: drawn in the grey (knobrow: a value that cannot change now) */
#define CR_CF_MARK(c) ((uint8_t)((c) << 4))   /* .. a modulation mark, a 4 px square at its top right in colour c
                                     * (a named CR_COL_*, 0 none): the matrix modulates its parameter */
#define CR_CF_MARKCOL(f) ((uint32_t)(f) >> 4)

/* animations in progress (cr_screen_t.anim); each is a pure function of the fields and cr_draw's anim_ms, the time
 * since the change that started it (cr_draw.c CR_*_MS: the durations) */
#define CR_A_SQUEEZE 1u             /* chord: `from` squeezes to a thin column, `name` stretches out of it */
#define CR_A_SLIDE 2u               /* picker: the selection slides in from sel - slide */
#define CR_A_FILL 4u                /* meter: the stripes fill one by one from pct_from to pct */
#define CR_A_STRIPES 8u             /* stripes: they slide, a cycle every `period_ms` from `phase` */
#define CR_A_SWEEP 16u              /* stripes: they are swept off to the right (the first chord) */
#define CR_A_INTRO 32u              /* stripes: power-on, the bands slide in from the left one after another, the name lands */

#define CR_PICK_MAX 8u              /* picker items held (a window of the list around the selection) */
#define CR_NOTES_MAX 8u
#define CR_KEYS 27u                 /* key index 0..26 = F3..G5 */
#define CR_LINES_MAX 6u
#define CR_WAVE_N 240u              /* scope: one sample per column */

typedef struct { char root[4], quality[6], sup[8]; uint8_t col_root, col_quality, col_sup; } cr_name_t;
typedef struct { char t[6]; uint8_t col, mark; } cr_note_t;                    /* "C#5", its colour, a block under it */
typedef struct {                                                                /* an editor cell (KNOB 1..4) */
    char label[10], value[9];       /* label: edit8 only (a stack's columns have headings) */
    uint8_t flags;                  /* CR_CF_*, the mark's colour in the high nibble */
    uint8_t glyph;                  /* CR_G_* */
    uint8_t pct;                    /* Q8 of 255: the glyph's / bar's fill (square: the duty) */
    uint8_t pct2;                   /* Q8 of 255: a pictogram's second value (echoes: feedback, lfo: depth) */
} cr_cell_t;
typedef struct { char t[32]; uint8_t px, col, bold, center; } cr_line_t;       /* a text line (px 0 = 12) */

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

    /* the ring round the edge and the message box */
    uint8_t ring_on, ring_rec, ring_col, message_col;
    uint16_t ring;                  /* Q8 progress (0: the dotted track only) */
    /* the corner dial: a loop merely playing (no ring), a 16 px ring at the right end of the top line (centre 229, 12,
     * r 8, 3 px: the dotted track, the progress red from 12 o'clock); the header's right text moves 22 px left */
    uint8_t dial_on, dial_pulse;    /* dial_pulse: the downbeat's frame (~100 ms): the arc drawn 5 px wide */
    uint16_t dial;                  /* Q8 progress (the loop's fraction, as ring) */
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
    char sub[12];                   /* under the value, in its colour */
    uint16_t pct, pct_from;         /* Q8: the stripes filled; CR_A_FILL: from */
    uint8_t segments, thick;        /* stripes (0 = 12), their height (0 = 14) */
    uint8_t sub_mark;               /* a small square after `sub` (an overwritten factory preset, docs/PRESETS.md) */

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
     * kr_band 1: the band is the keyboard instead of the picker (KEY: lit / lit_col / key_label, `label` top left) */
    uint8_t kr_band;                /* knobrow: 0 the picker, 1 the keyboard */
    char page[16];                  /* edit8 / stack: the title line's right text ("OSC 2 \267 A") */
    cr_cell_t cell[CR_ED_ROWS][4];  /* rows of four cells (edit8: 1..2 rows) */
    char head[4][10];               /* stack: the column headings */
    char rlabel[CR_ED_ROWS][3];     /* stack: the row labels */
    uint8_t n_rows, active;         /* rows shown; the row on KNOB 1..4 (knob colours, bars) */
    uint8_t hot_r, hot_c;           /* the cell just turned (a filled block behind its value): row + 1 (0: none), column */
    uint8_t hot_col;                /* .. the block's colour (NONE: the knob's; a modulation source's while mapping) */
    uint8_t wide;                   /* edit8: CR_W_* */
    uint8_t tall;                   /* edit8: one row of tall level bars over the whole panel (the oscillator mixer) */
    uint8_t fine;                   /* edit8 / stack: SHIFT on (fine steps): "fine" small in the title line;
                                     * `batt` (0..4, 4 = charging; 255 = none): edit8 / stack draw the header's battery
                                     * at the title line's right end (the MIX screens) */
    uint8_t wv[20];                 /* env: a h d s r (Q8 of 255), the lit segment + 1 (1 A .. 5 R, 0 none);
                                     * filter: cut res (Q8 of 255), ftype (0..127: 0 LP, 32 BP, 64 HP, 96 NOTCH,
                                     * crossfaded between, 127 back toward LP), drive;
                                     * dx: R1..R4, L1..L4 (0..99, the DX7's), the lit segment (1..4, 0 none), 1 = a
                                     * pitch EG (levels round 50: a centre line);
                                     * cz: R1..R8 (0..7), L1..L8 (8..15) (0..99, the CZ-1's panel values), SUS (16: the
                                     * step 0..7, 8 none), END (17: 0..7), the lit step (18: 1..8, 0 none) */
    char foot[48];                  /* stripes: the bottom line */

    /* panel: text, geek (lines: geek's status lines are lines[0..1].t) */
    cr_line_t lines[CR_LINES_MAX];
    uint8_t n_lines;

    /* panel: scope (the master output, triggered; -127..127 = the panel's half height; hashed with the rest, so a
     * moving trace redraws and a still one costs nothing) */
    int8_t wave[CR_WAVE_N];
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
