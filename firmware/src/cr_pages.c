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
 * An engine with deep pages (core.h eng_deep_t: the VA) has its own between EDIT 2 and ENV: the sequence is then
 * ENGINE, EDIT 1, EDIT 2, deep 0 .. D - 1, ENV .. MIX (cp_n pages; cp_base: the platform page of one, cp_dp: the
 * deep page). Sections (OPT + SELECT): ENGINE, the engine's own (eng_deep_t.section), ENV, FX, MIX.
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

#ifdef CR_DEEP_STUB
/* a test engine's deep pages for the UI's own tests (built with -DCR_DEEP_STUB: ANALOG gets them): four pages,
 * a 16-byte patch per track that nothing plays */
static const char *const CPS_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PULSE", "NOISE"};
static const char *const CPS_TYPE[] = {"LP", "BP", "HP", "NOTCH"};
static const char *const CPS_SRC[] = {"LFO1", "ENV2", "VEL", "MW"};
static const char *const CPS_DST[] = {"PITCH", "CUT", "PW", "AMP"};
static const eng_page_t CPS_PAGES[] = {
    {"OSC 1", {{"WAVE", F_ENUM, 0, 5, 0, CPS_WAVE, 0}, {"LEVEL", F_PCT, 0, 127, 100, 0, 0},
               {"COARSE", F_INT, -24, 24, 0, 0, 0}, {"FINE", F_INT, -50, 50, 0, 0, 0}}},
    {"FILTER", {{"TYPE", F_ENUM, 0, 3, 0, CPS_TYPE, 0}, {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
                {"RES", F_PCT, 0, 127, 20, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}},
    {"ENV 1", {{"ATK", F_TIME, 0, 127, 10, 0, 0}, {"DEC", F_TIME, 0, 127, 60, 0, 0},
               {"SUS", F_PCT, 0, 127, 80, 0, 0}, {"REL", F_TIME, 0, 127, 50, 0, 0}}},
    {"MOD 1", {{"SRC", F_ENUM, 0, 3, 0, CPS_SRC, 0}, {"DST", F_ENUM, 0, 3, 1, CPS_DST, 0},
               {"AMT", F_BIPCT, -64, 64, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}},
};
static int8_t cps_patch[NTRK][16];
static uint8_t cps_init[NTRK];
static int8_t *cps_at(const track_t *t, uint32_t p, uint32_t c)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, i, j;
    if (!cps_init[k]) {
        cps_init[k] = 1;
        for (i = 0; i < NELEM(CPS_PAGES); i++)
            for (j = 0; j < 4u; j++)
                cps_patch[k][i * 4u + j] = (int8_t)CPS_PAGES[i].col[j].def;
    }
    return &cps_patch[k][(p % NELEM(CPS_PAGES)) * 4u + (c & 3u)];
}
static int32_t cps_get(const track_t *t, uint32_t p, uint32_t c) { return *cps_at(t, p, c); }
static void cps_set(track_t *t, uint32_t p, uint32_t c, int32_t v) { *cps_at(t, p, c) = (int8_t)v; }
static const eng_deep_t CPS_DEEP = {NELEM(CPS_PAGES), CPS_PAGES, {0, 1, 2, 3, 0xFF}, cps_get, cps_set, 16, 0, 0, 0};
#endif

/* the page sequence: deep pages (0 without) between EDIT 2 and ENV */
static const eng_deep_t *cp_deep(const track_t *t)
{
#ifdef CR_DEEP_STUB
    const eng_deep_t *d = eng_idx(t->eng_req) == 0u ? &CPS_DEEP : ENGINES[eng_idx(t->eng_req)]->deep;
#else
    const eng_deep_t *d = ENGINES[eng_idx(t->eng_req)]->deep;
#endif
    return d && d->pages && d->npages && d->get && d->set ? d : 0;
}
static uint32_t cp_nd(const track_t *t)
{
    const eng_deep_t *d = cp_deep(t);
    return d ? d->npages : 0u;
}
static uint32_t cp_n(const track_t *t) { return CP_N + cp_nd(t); }
/* page pg of the sequence -> its deep page, -1: a platform page */
static int32_t cp_dp(const track_t *t, uint32_t pg)
{
    uint32_t nd = cp_nd(t);
    return pg >= CP_ENV && pg < CP_ENV + nd ? (int32_t)(pg - CP_ENV) : -1;
}
/* page pg of the sequence -> its platform page (CP_*), CP_N: a deep page */
static uint32_t cp_base(const track_t *t, uint32_t pg)
{
    uint32_t nd = cp_nd(t);
    pg %= cp_n(t);
    return pg < CP_ENV ? pg : pg < CP_ENV + nd ? (uint32_t)CP_N : pg - nd;
}
/* KNOB slot of page pg: a track parameter, CPX_*, or CPX_NONE (a deep page: cp_dp) */
static uint32_t cp_id(const track_t *t, uint32_t pg, uint32_t slot)
{
    uint32_t b = cp_base(t, pg);
    return b < CP_N ? CP_PAGES[b].id[slot & 3u] : CPX_NONE;
}

static const char *cp_title(const track_t *t, uint32_t pg)
{
    int32_t dp = cp_dp(t, pg);
    if (dp >= 0)
        return cp_deep(t)->pages[dp].title;
    pg = cp_base(t, pg);
    if (pg == CP_EDIT1 || pg == CP_EDIT2) {
        const char *s = ENGINES[eng_idx(t->eng_req)]->page_title[pg - CP_EDIT1];
        return s ? s : pg == CP_EDIT1 ? "EDIT 1" : "EDIT 2";
    }
    return CP_PAGES[pg % CP_N].title;
}

/* the sections (OPT + SELECT jumps to the next one's first page): their first pages in order, how many */
#define CP_SECT_MAX 12u
static uint32_t cp_sections(const track_t *t, uint8_t *out)
{
    const eng_deep_t *d = cp_deep(t);
    uint32_t n = 0, nd = cp_nd(t), i;
    out[n++] = CP_ENGINE;
    for (i = 0; d && i < NELEM(d->section) && d->section[i] != 0xFFu; i++)
        if (d->section[i] < nd && (uint32_t)(CP_ENV + d->section[i]) > out[n - 1u])
            out[n++] = (uint8_t)(CP_ENV + d->section[i]);
    out[n++] = (uint8_t)(CP_ENV + nd);
    out[n++] = (uint8_t)(CP_FX + nd);
    out[n++] = (uint8_t)(CP_MIX + nd);
    return n;
}
/* the section page pg is in */
static uint32_t cp_sect(const track_t *t, uint32_t pg)
{
    uint8_t st[CP_SECT_MAX];
    uint32_t n = cp_sections(t, st), k = 0;
    while (k + 1u < n && st[k + 1u] <= pg)
        k++;
    return k;
}
/* s sections on from page pg (s < 0: back; from inside a section, back is its own first page first): the page */
static uint32_t cp_sect_step(const track_t *t, uint32_t pg, int32_t s)
{
    uint8_t st[CP_SECT_MAX];
    uint32_t n = cp_sections(t, st), k = cp_sect(t, pg);
    for (; s > 0; s--)
        k = (k + 1u) % n;
    for (; s < 0; s++) {
        if (st[k] == pg)
            k = (k + n - 1u) % n;
        pg = st[k];
    }
    return st[k];
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

/* column c of page pg for track t: label, value, glyph and its fill. Not the CPX_ ones */
static void cp_dcolumn(const track_t *t, uint32_t dp, uint32_t slot, cr_param_t *c);
static void cp_column(const track_t *t, uint32_t pg, uint32_t slot, cr_param_t *c)
{
    uint32_t id = cp_id(t, pg, slot), i;
    const param_desc_t *d;
    int32_t v;
    if (cp_dp(t, pg) >= 0) {
        cp_dcolumn(t, (uint32_t)cp_dp(t, pg), slot, c);
        return;
    }
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
/* deep page dp, column slot: label, value, glyph (by the label and the kind: the selected waveform, the filter
 * type's curve, the envelope of the page's ATK DEC SUS REL, a mod slot's SRC -> DST and its amount) */
static void cp_dcolumn(const track_t *t, uint32_t dp, uint32_t slot, cr_param_t *c)
{
    const eng_deep_t *dd = cp_deep(t);
    const eng_page_t *pg = &dd->pages[dp];
    const param_desc_t *d = &pg->col[slot & 3u];
    static const char *const EL[4] = {"ATK", "DEC", "SUS", "REL"};
    uint32_t i;
    int32_t v, k;
    if (!d->label)
        return;                                    /* an empty column: nothing */
    v = dd->get(t, dp, slot & 3u);
    str_cpy(c->label, d->label, sizeof c->label);
    for (i = 1; c->label[i]; i++)
        if (c->label[i] >= 'A' && c->label[i] <= 'Z')
            c->label[i] = (char)(c->label[i] + 32);
    for (i = 0; i < NELEM(CP_DLABEL); i++)
        if (cp_eq(d->label, CP_DLABEL[i].s))
            str_cpy(c->label, CP_DLABEL[i].l, sizeof c->label);
    cp_value(d, v, c->value, sizeof c->value);
    c->pct = (uint16_t)cp_q8(d, v);
    for (i = 0; i < 4u; i++)                       /* the page's envelope, when it has one */
        if ((k = cp_dcol(pg, EL[i])) >= 0) {
            uint32_t e = cp_q8(&pg->col[k], dd->get(t, dp, (uint32_t)k));
            c->env[i] = (uint8_t)(e > 255u ? 255u : e);
        }
    if (cp_eq(d->label, "ATK") && cp_dcol(pg, "DEC") >= 0)
        c->glyph = CR_G_ENV;
    else if (cp_eq(d->label, "SUS"))
        c->glyph = CR_G_BAR;
    else if (d->fmt == F_ENUM && cp_has(d->label, "WAVE")) {    /* the waveform selected */
        const char *w = c->value;
        c->cycles = 2;
        c->pct = 128;
        c->glyph = cp_has(w, "SAW") || cp_has(w, "RMP") ? CR_G_SAW
                 : cp_has(w, "PUL") || cp_has(w, "PW") ? CR_G_SQUARE
                 : cp_has(w, "SQ") ? CR_G_SQUARE
                 : cp_has(w, "TRI") ? CR_G_TRI
                 : cp_has(w, "NOI") || cp_has(w, "NZ") || cp_has(w, "RND") || cp_has(w, "S&H") ? CR_G_NOISE
                 : cp_has(w, "STEP") ? CR_G_STEPS
                 : CR_G_WAVE;
        if (cp_has(w, "PUL") || cp_has(w, "PW"))
            c->pct = 64;                           /* a narrow pulse */
        if (c->glyph == CR_G_STEPS)
            c->n = 6;
    } else if (d->fmt == F_ENUM && cp_eq(d->label, "TYPE")) {   /* the filter's response */
        const char *w = c->value;
        c->glyph = CR_G_FTYPE;
        c->n = (uint8_t)(cp_has(w, "BP") || cp_has(w, "BAND") ? 1 : cp_has(w, "HP") || cp_has(w, "HI") ? 2
                         : cp_has(w, "NO") || cp_has(w, "BR") ? 3 : 0);
    } else if (cp_eq(d->label, "AMT") && cp_dcol(pg, "SRC") >= 0 && cp_dcol(pg, "DST") >= 0) {
        char b[8];
        int32_t ks = cp_dcol(pg, "SRC"), kd = cp_dcol(pg, "DST");
        c->glyph = CR_G_MOD;
        cp_value(&pg->col[ks], dd->get(t, dp, (uint32_t)ks), b, sizeof b);
        str_cpy(c->src, b, sizeof c->src);
        cp_value(&pg->col[kd], dd->get(t, dp, (uint32_t)kd), b, sizeof b);
        str_cpy(c->dst, b, sizeof c->dst);
    } else if (d->fmt == F_CUTOFF || cp_eq(d->label, "CUT"))
        c->glyph = CR_G_FILTER;
    else if (d->fmt == F_ENUM)
        c->glyph = CR_G_DOTS;
    else if (d->fmt == F_PCT || d->fmt == F_DB || d->fmt == F_ONOFF)
        c->glyph = CR_G_BAR;
    else
        c->glyph = CR_G_KNOB;
}
