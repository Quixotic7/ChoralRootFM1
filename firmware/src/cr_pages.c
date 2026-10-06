/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the sound editor's data (cr_edit.c builds the editor from these). The platform pages of four
 * parameters, KNOB 1..4:
 *
 *   EDIT 1, EDIT 2  the engine's own parameters (engines.c edit[0..3], edit[4..7], titled by its page_title)
 *   ENV             ATK DEC SUS REL
 *   LFO             RATE WAVE, its depth to the pitch and the filter
 *   MOD             the envelope to the filter / pitch / shape (Felucca's ENV DEST: P_ED_*), the LFO to the amplitude
 *   FX              the sends DIST CHO DLY REV
 *   MIX, MIX 2      LEVEL PAN VOICE GLIDE | TRANS DETUNE PRIO GLMODE
 *
 * and an engine's deep pages (core.h eng_deep_t: the VA's 31). Ranges, defaults and the value text are Felucca's
 * (params.c TP / engines.c edit[] through track_desc, param_format); this file chooses the pages, the long labels
 * and the steps. Pure functions of a track: cr_edit.c owns the state. */

enum { CP_EDIT1, CP_EDIT2, CP_ENV, CP_LFO, CP_MOD, CP_FX, CP_MIX, CP_MIX2, CP_N };
static const struct { const char *title; uint8_t id[4]; } CP_PAGES[CP_N] = {
    {0, {P_E0, P_E1, P_E2, P_E3}},
    {0, {P_E4, P_E5, P_E6, P_E7}},
    {"ENV", {P_ATK, P_DEC, P_SUS, P_REL}},
    {"LFO", {P_LRATE, P_LWAVE, P_LD_PIT, P_LD_FLT}},
    {"MOD", {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_LD_AMP}},
    {"FX", {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"MIX", {P_LEVEL, P_PAN, P_VOICE, P_GLIDE}},
    {"MIX", {P_TRANS, P_DETUNE, P_PRIO, P_GLMODE}},
};
static const struct { uint8_t id; const char *label; } CP_LABEL[] = {
    {P_ATK, "Attack"}, {P_DEC, "Decay"}, {P_SUS, "Sustain"}, {P_REL, "Release"},
    {P_LRATE, "Rate"}, {P_LWAVE, "Wave"}, {P_LD_PIT, "Vibrato"}, {P_LD_FLT, "Wah"},
    {P_ED_FLT, "Env>Filter"}, {P_ED_PIT, "Env>Pitch"}, {P_ED_SHP, "Env>Shape"}, {P_LD_AMP, "Tremolo"},
    {P_DIST, "Drive"}, {P_CHOR, "Chorus"}, {P_DLY, "Delay"}, {P_REV, "Reverb"},
    {P_LEVEL, "Level"}, {P_PAN, "Pan"}, {P_VOICE, "Voice"}, {P_GLIDE, "Glide"},
    {P_TRANS, "Trans"}, {P_DETUNE, "Detune"}, {P_PRIO, "Priority"}, {P_GLMODE, "Gl. mode"},
};

/* the engine's deep pages, 0: none (the eight P_E only) */
static const eng_deep_t *cp_deep(const track_t *t)
{
    const eng_deep_t *d = ENGINES[eng_idx(t->eng_req)]->deep;
    return d && d->pages && d->npages && d->get && d->set ? d : 0;
}

static const param_desc_t *cp_desc(const track_t *t, uint32_t id) { return track_desc(t, id); }

/* KNOB detents -> the new value (Felucca's ranges; an F_ENUM skips its aliases: params.c enum_step).
 * A detent: about 5% of the range (max(1, round(range / 20))), enums one by one; fine (OPT held): one step.
 * Clamped, so both ends are reached exactly */
static int32_t cp_dstep(const param_desc_t *d, int32_t v0, int32_t s, uint32_t fine)
{
    int32_t r = d->max - d->min, step = fine || d->fmt == F_ENUM ? 1 : (r + 10) / 20, v;
    if (step < 1)
        step = 1;
    v = v0 + s * step;
    v = clamp(v, d->min, d->max);
    if (d->fmt == F_ENUM && v != v0)
        v = enum_step(d, v0, v);
    return v;
}
static int32_t cp_step(const track_t *t, uint32_t id, int32_t s, uint32_t fine)
{
    return cp_dstep(cp_desc(t, id), t->p[id], s, fine);
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

/* ------------------------------------------------------ deep pages --- */
/* the long labels of the deep pages' short ones (else the short one, capitalised: "SYNC/RING" -> "Sync/ring") */
static const struct { const char *s, *l; } CP_DLABEL[] = {
    {"ATK", "Attack"}, {"DEC", "Decay"}, {"SUS", "Sustain"}, {"REL", "Release"}, {"CUT", "Cutoff"},
    {"RES", "Reso"}, {"KTRK", "Key trk"}, {"FENV", "Env amt"}, {"VEL", "Velocity"}, {"SRC", "Source"},
    {"DST", "Dest"}, {"AMT", "Amount"},
};
static int cp_up(int ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }
static int cp_eq(const char *a, const char *b)
{
    while (*a && cp_up(*a) == cp_up(*b))
        a++, b++;
    return !*a && !*b;
}
static int cp_has(const char *s, const char *sub)        /* sub in s, case-blind */
{
    uint32_t i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; sub[j] && s[i + j] && cp_up(s[i + j]) == cp_up(sub[j]); j++)
            ;
        if (!sub[j])
            return 1;
    }
    return 0;
}
/* the column of deep page dp labelled l, -1 none */
static int32_t cp_dcol(const eng_page_t *pg, const char *l)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (pg->col[k].label && cp_eq(pg->col[k].label, l))
            return (int32_t)k;
    return -1;
}
/* v of d as Q8 of its range */
static uint32_t cp_q8(const param_desc_t *d, int32_t v)
{
    return d->max > d->min ? (uint32_t)(clamp(v, d->min, d->max) - d->min) * 256u / (uint32_t)(d->max - d->min) : 0u;
}
