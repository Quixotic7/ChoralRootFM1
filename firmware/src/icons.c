/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Icons: 4-bit alpha cells of the Fukiai icon font (MIT), 12 px (parameters, lists) and 16 px
 * (header, dialogs, menu): web/fukiai.ttf -> build/gen/ui_icons.h
 * (tools/gen_aa_icons.py; names from assets/icons.json). Which icon a parameter gets is decided
 * here, by its label. FELUCCA_ICONS=0 turns the parameter icons off (labels get their full width back). */
#include "ui_icons.h"
#ifndef FELUCCA_ICONS
#define FELUCCA_ICONS 1
#endif
#define ICON_CELL 12

/* the circled numeral of track k (filled: the selected one), 12 or 16 px: tracks have no colours */
static uint32_t trk_icon(uint32_t k, int filled) { return (filled ? ICON_X_TRK1 : ICON_X_TRK1_O) + k % NTRK; }

/* icon id at size 12, 16 or 24 px (24: the header battery and USB, the FX map; one missing: its 12 px cell),
 * fg on bg; returns its width */
static int32_t cv_icon_on(int32_t x, int32_t y, uint32_t size, uint32_t id, uint16_t fg, uint16_t bg)
{
    const uint8_t *idx = size == 24u ? AI24_IDX : size == 16u ? AI16_IDX : AI12_IDX;
    const uint8_t *dat = size == 24u ? AI24_DATA : size == 16u ? AI16_DATA : AI12_DATA;
    if (id >= ICON_COUNT)
        return 0;
    if (idx[id] == 0xFFu) {
        if (size == 12u || AI12_IDX[id] == 0xFFu)
            return 0;
        size = 12u;
        idx = AI12_IDX;
        dat = AI12_DATA;
    }
    cv_alpha(x, y, size, size, dat + (uint32_t)idx[id] * (size * size / 2u), ramp(fg, bg));
    GFX_HOOK_TEXT(x, y + cv_oy, x + (int32_t)size, y + cv_oy + (int32_t)size, "icon", 4u);
    return (int32_t)size;
}
/* the same, its ink centred on row cy (header icons: each glyph sits differently in its cell) */
static int32_t cv_icon_mid(int32_t x, int32_t cy, uint32_t size, uint32_t id, uint16_t fg, uint16_t bg)
{
    const uint8_t *idx = size == 24u ? AI24_IDX : size == 16u ? AI16_IDX : AI12_IDX;
    const uint8_t *dat = size == 24u ? AI24_DATA : size == 16u ? AI16_DATA : AI12_DATA;
    int32_t top = -1, bot = 0, r, k;
    if (id < ICON_COUNT && idx[id] != 0xFFu) {
        const uint8_t *c = dat + (uint32_t)idx[id] * (size * size / 2u);
        for (r = 0; r < (int32_t)size; r++)
            for (k = 0; k < (int32_t)size / 2; k++)
                if (c[r * (int32_t)size / 2 + k]) {
                    if (top < 0) top = r;
                    bot = r;
                    break;
                }
    }
    if (top < 0) { top = 0; bot = (int32_t)size - 1; }
    return cv_icon_on(x, cy - (top + bot + 1) / 2, size, id, fg, bg);
}
#define ICON_NONE 0xFFu                   /* no icon (empty column) */
#define ICON_AUTO 0xFEu                   /* draw_column: look the label up */
#define ICON_GAP 2                        /* px between the icon and the label */

#if FELUCCA_ICONS
typedef struct {
    const char *label;
    uint8_t icon;
} icon_map_t;

/* label -> icon. Labels shared by several parameters (RATE, WAVE, SET) are
 * told apart by descriptor in param_icon(). Unknown labels get ICON_GENERIC. */
static const icon_map_t ICON_MAP[] = {
    /* track: ENV, LFO, destinations */
    {"LVL", ICON_LEVEL}, {"ATK", ICON_ATTACK}, {"DEC", ICON_DECAY}, {"SUS", ICON_SUSTAIN},
    {"REL", ICON_RELEASE}, {"FLT", ICON_CUTOFF}, {"PIT", ICON_PITCH}, {"SHP", ICON_SHAPE},
    {"FX", ICON_MOD}, {"RATE", ICON_RATE}, {"WAVE", ICON_WAVE}, {"PHS", ICON_PHASE},
    {"FADE", ICON_FADE}, {"AMP", ICON_LEVEL},
    /* arp, scale, pattern */
    {"MODE", ICON_ARP}, {"OCT", ICON_OCTAVE}, {"GATE", ICON_GATE}, {"SWG", ICON_SWING},
    {"PROB", ICON_PROB}, {"HOLD", ICON_HOLD}, {"ORD", ICON_ORDER}, {"ROOT", ICON_PITCH},
    {"SCL", ICON_SCALE}, {"QNT", ICON_QUANTIZE}, {"TRN", ICON_TRANSPOSE}, {"LEN", ICON_LENGTH},
    {"DIV", ICON_DIVISION}, {"VOIC", ICON_OCTAVE},   /* (CHRD: below, PHYS's) */
    /* fx sends, voice */
    {"DST", ICON_DIST}, {"CHO", ICON_CHORUS}, {"DLY", ICON_DELAY}, {"REV", ICON_REVERB},
    {"VCE", ICON_VOICE}, {"GLD", ICON_GLIDE}, {"GLMOD", ICON_GLIDE}, {"PRIO", ICON_ORDER},
    {"ALLOC", ICON_VOICE}, {"DTUNE", ICON_DETUNE}, {"PAN", ICON_PAN}, {"MUTE", ICON_MUTE},
    /* global */
    {"BPM", ICON_TEMPO}, {"CLK", ICON_TEMPO}, {"TUNE", ICON_TUNE}, {"TIME", ICON_TIME},
    {"FDBK", ICON_FEEDBACK}, {"COLR", ICON_TONE}, {"MIX", ICON_MIX}, {"SIZE", ICON_SIZE},
    {"DAMP", ICON_DAMP}, {"TYPE", ICON_REVERB}, {"CRT", ICON_RATE}, {"CDP", ICON_MOD}, {"MIDI", ICON_MIDI},
    {"SYNC", ICON_TEMPO}, {"ROUT", ICON_MIX}, {"CPU", ICON_CHIP}, {"SLOT", ICON_SAVE},
    {"LOAD", ICON_LOAD}, {"SAVE", ICON_SAVE}, {"ENG", ICON_WAVE}, {"CLRSQ", ICON_CLEAR},
    {"INIT", ICON_CLEAR}, {"ERASE", ICON_CLEAR}, {"CH", ICON_MIDI}, {"LEVEL", ICON_LEVEL},
    /* engines (eng_*.c edit[] labels) */
    {"DTN", ICON_DETUNE}, {"NOIS", ICON_NOISE}, {"CUT", ICON_CUTOFF}, {"RES", ICON_RESO},
    {"DRV", ICON_DRIVE}, {"KTR", ICON_KEYTRACK}, {"ALG", ICON_ALGORITHM}, {"R2", ICON_RATIO},
    {"R3", ICON_RATIO}, {"R4", ICON_RATIO}, {"IDX", ICON_MOD}, {"MDEC", ICON_DECAY},
    {"FB", ICON_FEEDBACK}, {"CHIP", ICON_CHIP}, {"DUTY", ICON_PULSE}, {"CRSH", ICON_BITS},
    {"SWP", ICON_SWEEP}, {"VIB", ICON_VIBRATO}, {"ARP", ICON_ARP}, {"TONE", ICON_TONE},
    {"SET", ICON_SAMPLE}, {"BITS", ICON_BITS}, {"LOOP", ICON_LOOP}, {"WAVE2", ICON_WAVE},
    {"DCW", ICON_SHAPE}, {"ENV", ICON_ENV}, {"LINE", ICON_MIX}, {"SUB", ICON_SUB},
    {"FOLD", ICON_FOLD}, {"WAV#", ICON_WAVE}, {"DCY", ICON_DECAY}, {"INT2", ICON_TRANSPOSE},
    {"INT3", ICON_TRANSPOSE}, {"PW", ICON_PULSE},
    {"REG", ICON_DRAWBAR}, {"BODY", ICON_LEVEL}, {"TOP", ICON_TONE}, {"PERC", ICON_DECAY},
    {"CLICK", ICON_ATTACK}, {"ROTR", ICON_VIBRATO},
    {"SRC", ICON_SAMPLE}, {"START", ICON_STEPS}, {"PTCH", ICON_PITCH}, {"DCAY", ICON_DECAY},
    {"POS", ICON_PHASE}, {"DENS", ICON_GRAIN}, {"SPRD", ICON_NOISE},
    {"MODEL", ICON_PHYS}, {"STRC", ICON_SHAPE}, {"BRIT", ICON_TONE}, {"BOW", ICON_SUSTAIN}, {"EXC", ICON_MIX},   /* PHYS */
    {"HARM", ICON_RATIO}, {"BEND", ICON_SWEEP}, {"CHRD", ICON_SCALE}, {"SYMP", ICON_MIX}, {"DECY", ICON_DECAY},
    {"SNAP", ICON_NOISE}, {"KIT", ICON_SAMPLE}, {"KICK", ICON_ATTACK},       /* PHYS: MEMB, SYMP; DRUM */
    {"VOWL", ICON_VOICE}, {"VOWL2", ICON_VOICE}, {"TALK", ICON_SWEEP}, {"SHIFT", ICON_TRANSPOSE},
    {"BUZZ", ICON_PULSE}, {"BRTH", ICON_NOISE}, {"Q", ICON_RESO}, {"RAND", ICON_PROB},
    {"FREQ", ICON_CUTOFF}, {"TRK", ICON_KEYTRACK}, {"DRFT", ICON_SWEEP},   /* NOISE (COLR, DENS, CRSH: above) */
    {"MLVL", ICON_MOD}, {"MRAT", ICON_RATIO}, {"MEG", ICON_DECAY}, {"VMOD", ICON_ACCENT}, {"DTUN", ICON_DETUNE},
    {"PTCH", ICON_LOAD},                                                    /* FM6 (ALG, FB: above) */
    /* fixed columns drawn by ui_draw.c (STEP page, preset browser, SYSTEM) */
    {"NOTE", ICON_PITCH}, {"STEP", ICON_STEPS}, {"FLAG", ICON_ACCENT}, {"ACC", ICON_ACCENT}, {"LANE", ICON_DRUM}, {"HIT", ICON_GATE}, {"SLD", ICON_SLIDE}, {"USB", ICON_MIDI},
    {"TRACK", ICON_MIX},                  /* TRACKS page (LEVEL, LEN, PAN: above) */
    {"SLCR", ICON_SLICE}, {"PAT", ICON_STEPS}, {"DEPTH", ICON_MIX},   /* SLICER page (RATE: param_icon) */
};

static uint32_t icon_for_label(const char *l)
{
    uint32_t i;
    if (!l || !l[0] || l[0] == '-')
        return ICON_NONE;
    for (i = 0; i < sizeof(ICON_MAP) / sizeof(ICON_MAP[0]); i++)
        if (str_eq(l, ICON_MAP[i].label))
            return ICON_MAP[i].icon;
    return ICON_GENERIC;
}

/* WAVE / WAVE2 set to a shape: that shape's icon (value names of the engines and the LFO) */
static const icon_map_t WAVE_ICON[] = {
    {"SIN", ICON_W_SIN}, {"TRI", ICON_W_TRI}, {"SAW", ICON_W_SAW}, {"SQR", ICON_W_SQR},
    {"PLS", ICON_W_PLS}, {"PWM", ICON_W_PWM}, {"S&H", ICON_W_SH}, {"NOIS", ICON_NOISE},
    {"DSIN", ICON_W_DSIN}, {"SPLS", ICON_W_SPLS}, {"RSAW", ICON_W_RSAW}, {"RTRI", ICON_W_RTRI},
    {"RTRP", ICON_W_RTRP},
};

static uint32_t param_icon(const param_desc_t *d, int32_t v)
{
    uint32_t i;
    if (!d)
        return ICON_NONE;
    if (d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max &&
        (str_eq(d->label, "WAVE") || str_eq(d->label, "WAVE2")))
        for (i = 0; i < sizeof(WAVE_ICON) / sizeof(WAVE_ICON[0]); i++)
            if (str_eq(d->names[v], WAVE_ICON[i].label))
                return WAVE_ICON[i].icon;
    if (d == &TP[P_LWAVE])
        return ICON_LFO_WAVE;                 /* "WAVE" is also the oscillator wave */
    if (d == &TP[P_ARATE] || d == &TP[P_SLRATE])
        return ICON_DIVISION;                 /* arp / SLICER RATE is a note division, not Hz */
    if (d->names == N_TRIO_MODE)
        return ICON_CUTOFF;                   /* TRIO's MODE is the filter type, not the arp mode */
    if (d->names == N_NOISE_MODE)
        return ICON_NOISE;                    /* NOISE's MODE is the source; its CLK the register clock */
    if (d == &NOISE_CLK)
        return ICON_RATE;
#if FELUCCA_SLICE
    if (d->names == N_SLC_DIV)
        return ICON_SLICE;                    /* SLICE: DIV is the slicing, MODE the gate, REV the direction */
    if (d->names == N_SLC_MODE)
        return ICON_GATE;
    if (d->names == N_SLC_REV)
        return ICON_ORDER;
#endif
    return icon_for_label(d->label);
}

/* the MOD page: the icon of a source, of a destination (an engine parameter: its own) */
static uint32_t mod_src_icon(int32_t s)
{
    static const uint8_t I[MS_N] = {ICON_MOD, ICON_LFO_WAVE, ICON_ENV, ICON_ACCENT, ICON_KEYTRACK, ICON_PROB,
                                    ICON_MOD, ICON_MIDI, ICON_LEVEL};
    return I[clamp(s, 0, MS_N - 1)];
}
static uint32_t mod_dst_icon(const track_t *t, int32_t d)
{
    static const uint8_t I[MD_E1] = {ICON_MOD, ICON_PITCH, ICON_CUTOFF, ICON_SHAPE, ICON_LEVEL, ICON_PAN, ICON_DIST,
                                     ICON_CHORUS, ICON_DELAY, ICON_REVERB, ICON_RATE, ICON_VIBRATO};
    uint32_t id;
    d = clamp(d, 0, MD_N - 1);
    if (d < MD_E1)
        return I[d];
    id = P_E0 + (uint32_t)(d - MD_E1);
    return param_icon(track_desc(t, id), t->p[id]);
}

/* engine name (ENGINES[]->name) -> icon */
static uint32_t engine_icon(const char *name)
{
    static const icon_map_t M[] = {
        {"ANALOG", ICON_WAVE},
#if FELUCCA_FM4
        {"DIGITAL", ICON_ALGORITHM},
#endif
        {"PHASE", ICON_PHASE}, {"LOFI", ICON_BITS},
        {"SAMPLE", ICON_SAMPLE}, {"VOICE", ICON_MOUTH}, {"TRIO", ICON_TRIO}, {"WHEEL", ICON_DRAWBAR},
        {"SLICE", ICON_SLICE},
        {"GRAIN", ICON_GRAIN},
        {"PHYS", ICON_PHYS},
        {"DRUM", ICON_DRUM},
        {"NOISE", ICON_NOISE},
        {"FM6", ICON_MOD},                    /* (symbol_modular: six operators patched; a glyph of its own wanted) */
    };
    uint32_t i;
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        if (str_eq(name, M[i].label))
            return M[i].icon;
    return ICON_GENERIC;
}

/* MOTION: the knob and its trace; recording into it (REC armed on the selected track, playing): its REC form */
static uint32_t motion_icon(void) { return rec_on(TSEL) ? ICON_X_MOTION_REC : ICON_X_MOTION; }

/* the icon beside a page's title in the footer (the pages that have one of their own; else ICON_NONE) */
static uint32_t page_icon(const page_t *pg)
{
    switch (pg->graph) {
    case GR_TRK: return ICON_X_MIXER;         /* MIXER (GLO): vertical faders */
    case GR_PATS: return ICON_X_PATTERN;      /* SEQ > PHRASES: the pattern loader */
    case GR_SONG: return ICON_X_SONG;         /* SONG: the disc */
    case GR_CHANCE: return ICON_PROB;         /* CHANCE: the die */
    case GR_MOTION: return motion_icon();
    default: return ICON_NONE;
    }
}

#else
static uint32_t icon_for_label(const char *l) { (void)l; return ICON_NONE; }
static uint32_t param_icon(const param_desc_t *d, int32_t v) { (void)d; (void)v; return ICON_NONE; }
static uint32_t engine_icon(const char *name) { (void)name; return ICON_NONE; }
static uint32_t mod_src_icon(int32_t s) { (void)s; return ICON_NONE; }
static uint32_t mod_dst_icon(const track_t *t, int32_t d) { (void)t; (void)d; return ICON_NONE; }
static uint32_t motion_icon(void) { return ICON_NONE; }
static uint32_t page_icon(const page_t *pg) { (void)pg; return ICON_NONE; }
#endif
