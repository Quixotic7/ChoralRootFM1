/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the sound pages (PLAN.md section 4 "Sound editing", mock-up state 21). Eight pages of four
 * parameters, KNOB 1..4, SELECT turns them:
 *
 *   1 ENGINE   the engine, its factory presets, INIT          (cr_ui.c does these three: CPX_*)
 *   2, 3       the engine's own parameters (engines.c edit[0..3], edit[4..7], titled by its page_title)
 *   4 ENV      ATK DEC SUS REL
 *   5 LFO      RATE WAVE, its depth to the pitch and the filter
 *   6 MOD      the envelope to the filter / pitch / shape (Felucca's ENV DEST: P_ED_*), the LFO to the amplitude
 *   7 FX       the sends DIST CHO DLY REV
 *   8 MIX      LEVEL PAN VOICE GLIDE
 *
 * Ranges, defaults and the value text are Felucca's (params.c TP / engines.c edit[] through track_desc,
 * param_format); this file only chooses the pages, the long labels, the glyph of each column (cr_screen.h
 * CR_G_*) and its fill. Pure functions of a track: cr_ui.c owns the state. */

enum { CP_ENGINE, CP_EDIT1, CP_EDIT2, CP_ENV, CP_LFO, CP_MOD, CP_FX, CP_MIX, CP_N };
#define CPX_ENGINE 0xF1u
#define CPX_PRESET 0xF2u
#define CPX_INIT 0xF3u
#define CPX_NONE 0xFFu
static const struct { const char *title; uint8_t id[4]; } CP_PAGES[CP_N] = {
    {"ENGINE", {CPX_ENGINE, CPX_PRESET, CPX_INIT, CPX_NONE}},
    {0, {P_E0, P_E1, P_E2, P_E3}},
    {0, {P_E4, P_E5, P_E6, P_E7}},
    {"ENV", {P_ATK, P_DEC, P_SUS, P_REL}},
    {"LFO", {P_LRATE, P_LWAVE, P_LD_PIT, P_LD_FLT}},
    {"MOD", {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_LD_AMP}},
    {"FX", {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"MIX", {P_LEVEL, P_PAN, P_VOICE, P_GLIDE}},
};
static const struct { uint8_t id; const char *label; } CP_LABEL[] = {
    {P_ATK, "Attack"}, {P_DEC, "Decay"}, {P_SUS, "Sustain"}, {P_REL, "Release"},
    {P_LRATE, "Rate"}, {P_LWAVE, "Wave"}, {P_LD_PIT, "Vibrato"}, {P_LD_FLT, "Wah"},
    {P_ED_FLT, "Env>Filter"}, {P_ED_PIT, "Env>Pitch"}, {P_ED_SHP, "Env>Shape"}, {P_LD_AMP, "Tremolo"},
    {P_DIST, "Drive"}, {P_CHOR, "Chorus"}, {P_DLY, "Delay"}, {P_REV, "Reverb"},
    {P_LEVEL, "Level"}, {P_PAN, "Pan"}, {P_VOICE, "Voice"}, {P_GLIDE, "Glide"},
};

static const char *cp_title(const track_t *t, uint32_t pg)
{
    if (pg == CP_EDIT1 || pg == CP_EDIT2) {
        const char *s = ENGINES[eng_idx(t->eng_req)]->page_title[pg - CP_EDIT1];
        return s ? s : pg == CP_EDIT1 ? "EDIT 1" : "EDIT 2";
    }
    return CP_PAGES[pg % CP_N].title;
}
static const param_desc_t *cp_desc(const track_t *t, uint32_t id) { return track_desc(t, id); }

/* KNOB detents -> the new value (Felucca's ranges; an F_ENUM skips its aliases: params.c enum_step) */
static int32_t cp_step(const track_t *t, uint32_t id, int32_t s)
{
    const param_desc_t *d = cp_desc(t, id);
    int32_t v0 = t->p[id], step = d->fmt == F_ENUM || d->max - d->min < 64 ? 1 : 2, v = v0 + s * step;
    v = clamp(v, d->min, d->max);
    if (d->fmt == F_ENUM && v != v0)
        v = enum_step(d, v0, v);
    return v;
}

/* Felucca's value text and its unit, joined when they fit ("25.0ms", "+50%", "SAW") */
static void cp_value(const param_desc_t *d, int32_t v, char *out, uint32_t n)
{
    char val[12];
    const char *unit;
    uint32_t l;
    param_format(d, v, val, &unit);
    str_cpy(out, val, n);
    l = str_len(out);
    if (l + str_len(unit) + 1u <= n)
        str_cpy(out + l, unit, n - l);
}

/* column c of page pg for track t: label, value, glyph and its fill. Not the CPX_ ones */
static void cp_column(const track_t *t, uint32_t pg, uint32_t slot, cr_param_t *c)
{
    uint32_t id = CP_PAGES[pg % CP_N].id[slot & 3u], i;
    const param_desc_t *d;
    int32_t v;
    if (id >= P_COUNT)
        return;
    d = cp_desc(t, id);
    v = t->p[id];
    str_cpy(c->label, d->label, sizeof c->label);
    for (i = 0; i < NELEM(CP_LABEL); i++)
        if (CP_LABEL[i].id == id)
            str_cpy(c->label, CP_LABEL[i].label, sizeof c->label);
    cp_value(d, v, c->value, sizeof c->value);
    c->pct = (uint16_t)(d->max > d->min ? (uint32_t)(clamp(v, d->min, d->max) - d->min) * 256u / (uint32_t)(d->max - d->min) : 0u);
    c->env[0] = (uint8_t)(t->p[P_ATK] * 2);
    c->env[1] = (uint8_t)(t->p[P_DEC] * 2);
    c->env[2] = (uint8_t)(t->p[P_SUS] * 2);
    c->env[3] = (uint8_t)(t->p[P_REL] * 2);
    /* the glyph by the kind of parameter (mock-up 21: the envelope, a knob, a bar for SUS, a knob) */
    if (id == P_ATK)
        c->glyph = CR_G_ENV;
    else if (id == P_SUS)
        c->glyph = CR_G_BAR;
    else if (id == P_LWAVE) {
        static const uint8_t W[5] = {CR_G_WAVE, CR_G_WAVE, CR_G_SAW, CR_G_SQUARE, CR_G_STEPS};
        c->glyph = W[clamp(v, 0, 4)];
        c->cycles = 2;
        c->pct = 128;
        c->n = 6;
    } else if (d->fmt == F_CUTOFF || cb_streq(d->label, "CUT"))
        c->glyph = CR_G_FILTER;
    else if (d->fmt == F_ENUM)
        c->glyph = CR_G_DOTS;
    else if (d->fmt == F_PCT || d->fmt == F_DB)
        c->glyph = CR_G_BAR;
    else
        c->glyph = CR_G_KNOB;
}
