/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the sound editor (design/choralroot-fm1-sound-editor-mockups.json, the normative spec; docs/EDITOR.md;
 * drawn as cr_screen.h CR_K_EDIT8 / CR_K_STACK by cr_draw.c). Included by cr_ui.c (after its sounds, its engine
 * switch and cu_edited): cr_ui.c's gesture engine calls ce_button / ce_owns / ce_leds (CR_EDIT_HOOKS), its knob
 * dispatch ce_knob / ce_select, its screen builder ce_screen.
 *
 * EDIT tap opens it on the chord sound (BASS held + EDIT: the bass sound), EDIT tap or HOME leaves; SHIFT (OPT) +
 * EDIT switches chord <-> bass inside. Navigation: GROUPS -> SCREENS -> LANES. The function buttons are the groups by
 * their PRINTED names: FX = OSC, SEL = FILT, ENV = ENV, LFO = LFO, SEQ = MOD, PLAY = FX (the sends), REC = MIX. A
 * group's button opens it (at its remembered screen and lane), tapped again it cycles the group's screens; held it
 * does nothing (reserved). SELECT moves the LANE (the four parameters on KNOB 1..4: a row, an oscillator, a slot)
 * and runs on across the screens (past the last lane: the next screen's first, wrapping), so SELECT alone reaches
 * everything. GLO = SHIFT: a tap latches it (fine steps, its LED lit, "fine" in the title line), held it is
 * momentary. KNOB 1..4 edit the active lane's cells (a detent 5 % of the range, enums one by one; SHIFT: one step).
 * The editor remembers its group per part, every group its screen and lane (RAM).
 *
 * The screens are built from the engine's deep pages (core.h eng_deep_t: its sections OSC FILTER ENV LFO MOD, pages
 * of four titled "OSC 1", "OSC 1+", "LFO SYN" ..), never from an engine's column names, so pages and columns an
 * engine adds show up by themselves:
 *   OSC, LFO: one STACK screen per kind of page (the pages "OSC n" are one, "OSC n+" another; a page with no number,
 *     "LFO SYN", is a screen whose lane i shows its column i); OSC then has the MIXER: one lane, the LEVEL column of
 *     each oscillator as four tall bars;
 *   FILT, ENV: edit8 screens of the pages that share an instance number ("FILTER", "FILTER+"; "ENV 2", "ENV 2+"),
 *     two lanes a screen, under the wide filter curve / the envelope of that ENV;
 *   MOD: a stack of the matrix's slots (eight lanes a screen);
 *   an engine without deep pages: OSC = EDIT 1, FILT = EDIT 2 (one lane each), ENV = the platform ENV under the
 *   envelope (an engine with its own envelopes, FM6: a message, the group stays), LFO = the platform LFO, MOD = the
 *   platform MOD page as a stack of 4 slots (source and destination fixed, the amount on KNOB 3);
 *   every engine: FX = the sends (one lane), MIX = level pan voice glide | transpose detune priority glide mode.
 * An engine with a screen plan (eng_deep_t.screens, core.h eng_screen_t: QUAD's) has its groups built from the plan
 * instead: each screen one or two pages under a band (ENG_B_*), its title, each cell's label and style (ENG_C_*: big
 * numbers, the ratio fraction, harm / detune glyphs, a wave + phase span cell); an engine without a MOD section (QUAD)
 * has the platform's MOD routes. VA, FM6 and CZ-1 have no plan: their screens are as before.
 *
 * No motion (the user's feedback, 2026-10-06: responsiveness first; Options > Motion does not apply here): a screen,
 * a lane, a value and the wide band are drawn as they are, in the frame after the detent; only the hot cell (the one
 * just turned) lasts CE_HOT_MS. cr_draw composes only the strips of what changed (the band, a row).
 *
 * Quick modulation mapping (an engine whose eng_deep_t has mod_dst, the VA): ENV or LFO held + a knob turned = the
 * last ENV n / LFO n shown (ENV 2 / LFO 1 before) modulates the parameter under that knob: its matrix slot (that
 * source and destination, else the first free one; none: "matrix full") gets the turn on its AMT, the hot cell
 * showing "LFO1 +24" in the source's colour; OCT- held + a knob = every slot to that parameter cleared. A cell whose
 * parameter the matrix modulates carries a mark in the source's colour (white: several). */

enum { CE_OSC, CE_FILT, CE_ENV, CE_LFO, CE_MOD, CE_FX, CE_MIX, CE_NSEC };   /* (OSC..MOD: eng_deep_t.section 0..4) */
static const char *const CE_SEC_NAME[CE_NSEC] = {"OSC", "FILT", "ENV", "LFO", "MOD", "FX", "MIX"};
static const uint8_t CE_SEC_BTN[CE_NSEC] = {B_FX, B_SCL, B_ENV, B_LFO, B_SEQ, B_PLAY, B_REC};   /* printed ids */
#define CE_HOT_MS 800u                    /* the cell just turned stays hot */

/* a cell's parameter: a deep page's column, a track parameter, a fixed text, a dash (an empty column) */
enum { CE_R_NONE, CE_R_DEEP, CE_R_TRK, CE_R_TXT, CE_R_DASH };
typedef struct { uint8_t k, a, b; } ce_ref_t;             /* DEEP: page a, column b; TRK: id a; TXT: CE_TXT[a] */
static const char *const CE_TXT[] = {"ENV", "LFO", "FILT", "PITCH", "SHAPE", "AMP"};
static const uint8_t CE_PMOD[4][2] = {{0, 2}, {0, 3}, {0, 4}, {1, 5}};   /* the platform MOD's sources, dests */

/* a screen of a group: what its lanes are */
enum { CE_S_PLAT,                         /* the platform's pages (an engine without deep pages; FX, MIX) */
       CE_S_STACK,                        /* a stack: lane i = deep page pg[i] */
       CE_S_TRANS,                        /* a stack: lane i = column i of the one page pg[0] ("LFO SYN") */
       CE_S_MIXER,                        /* one lane: the LEVEL column of the pages pg[0..n) */
       CE_S_EDIT,                         /* edit8: lane i = deep page pg[i] (n <= 2) */
       CE_S_PAGE };                       /* edit8: one lane, the page pg[0] (a page of no instance: "VOICE") */
typedef struct { uint8_t type, n, pg[CR_ED_ROWS], hs; } ce_scr_t;   /* hs: the plan's screen + 1 (eng_deep_t.screens) */

typedef struct {
    uint8_t kind, n, active, wide, tall;  /* CR_K_EDIT8 / STACK, rows, the row on the knobs, CR_W_*, the mixer */
    ce_ref_t ref[CR_ED_ROWS][4];
    char head[4][10];                     /* stack: the column headings */
    char right[16];                       /* the title line's right text */
    uint8_t dp0, np, row0;                /* the band's pages (env: dp0 .. + np; filter: dp0); the first row's number */
    uint8_t hs;                           /* a screen of the engine's plan: its index + 1 (0: the title rules) */
} ce_view_t;

static struct {
    uint8_t grp[2], scr[2][CE_NSEC], lane[2][CE_NSEC];   /* per part: the group; per group its screen and lane */
    uint8_t shift;                        /* SHIFT latched (fine steps) */
    uint8_t hot_r, hot_c;                 /* the cell last turned (row + 1, 0 none), its column */
    uint8_t hot_slot, hot_src;            /* .. by a quick mapping: its matrix page + 1 (0: an edit), the source */
    uint32_t hot_t0;
    uint8_t msrc[2][2];                   /* per part: the ENV n / LFO n last shown (0: none yet, ENV 2 / LFO 1) */
} cx __attribute__((section(".pool")));   /* (zero-initialised: OSC, screen 1, lane 1 for both parts) */

static track_t *ce_trk(void) { return &trk[ce.part ? CR_PART_BASS : CR_PART_CHORD]; }

/* the engine's deep pages when it has the sections OSC FILTER ENV LFO and MOD, or the first four (no MOD: QUAD's;
 * the MOD group is then the platform's routes, as for an engine without deep pages), ascending (eng_deep_t.section) */
static const eng_deep_t *ce_deep(const track_t *t)
{
    const eng_deep_t *d = cp_deep(t);
    uint32_t i;
    if (!d)
        return 0;
    for (i = 0; i < 5u; i++) {
        if (i == 4u && d->section[4] == 0xFFu)
            break;
        if (d->section[i] >= d->npages || (i && d->section[i] <= d->section[i - 1u]))
            return 0;
    }
    return d;
}
/* the engine's MOD group is its own pages (else the platform's routes) */
static int ce_has_mod(const eng_deep_t *d) { return d && d->section[4] != 0xFFu; }
/* the plan's screen of a view / a scan, 0 none */
static const eng_screen_t *ce_plan(const eng_deep_t *d, uint32_t hs)
{
    return d && d->screens && hs && hs <= d->nscreens ? &d->screens[hs - 1u] : 0;
}
/* the pages of section g (CE_OSC..CE_MOD): a .. b - 1 */
static void ce_range(const eng_deep_t *d, uint32_t g, uint32_t *a, uint32_t *b)
{
    *a = d->section[g];
    *b = g + 1u < 8u && d->section[g + 1u] < d->npages && d->section[g + 1u] > *a ? d->section[g + 1u] : d->npages;
}
/* a page title's instance number ("OSC 2+" -> 2, "LFO SYN" / "FILTER" -> 0) and what follows it ("+") */
static uint32_t ce_inst(const char *t)
{
    while (*t && *t != ' ')
        t++;
    return t[0] == ' ' && t[1] >= '1' && t[1] <= '9' ? (uint32_t)(t[1] - '0') : 0u;
}
static const char *ce_suf(const char *t)
{
    while (*t && *t != ' ')
        t++;
    if (t[0] == ' ' && t[1] >= '0' && t[1] <= '9')
        for (t++; *t >= '0' && *t <= '9'; t++)
            ;
    return t;
}
/* pages p and q are of one kind: both numbered, with the same name and suffix ("OSC 1" / "OSC 3"; "OSC 1+" /
 * "OSC 4+"; FM6's "OP 2" and "SCALE 2" are two kinds) */
static int ce_kind_eq(const eng_deep_t *d, uint32_t p, uint32_t q)
{
    const char *a = d->pages[p].title, *b = d->pages[q].title;
    if (!ce_inst(a) || !ce_inst(b))
        return p == q;
    while (*a && *a != ' ' && cp_up(*a) == cp_up(*b))
        a++, b++;
    if (*a != ' ' || *b != ' ')
        return 0;
    return cp_eq(ce_suf(a), ce_suf(b));
}
/* page q (no instance) is page p's "+" page ("LFO" / "LFO+", FM6's "FUNC" / "FUNC+"): one screen of two lanes */
static int ce_plus_of(const eng_deep_t *d, uint32_t p, uint32_t q)
{
    const char *a = d->pages[p].title, *b = d->pages[q].title;
    if (ce_inst(a) || ce_inst(b) || q != p + 1u)
        return 0;
    while (*a && cp_up(*a) == cp_up(*b))
        a++, b++;
    return !*a && b[0] == '+' && !b[1];
}
/* the section's pages are matrix slots (SRC DST AMT: the VA's MOD 1..8), else plain pages (FM6's FUNC, CTRL) */
static int ce_is_matrix(const eng_deep_t *d, uint32_t pg)
{
    return cp_dcol(&d->pages[pg], "SRC") >= 0 && cp_dcol(&d->pages[pg], "DST") >= 0 &&
           cp_dcol(&d->pages[pg], "AMT") >= 0;
}
/* the screen's pages are a DX envelope: rates R1..R4 (FM6's ENV n, PITCH EG; the "dx" wide band) */
static int ce_is_dx(const eng_deep_t *d, uint32_t pg)
{
    return cp_dcol(&d->pages[pg], "R1") == 0 && cp_dcol(&d->pages[pg], "R4") == 3;
}

/* pages p and q have one name (the title up to its space or "+": "ENV 2+" / "ENV 2" "ENV"; CZ-1's "DCW 1 B" "DCW") */
static int ce_name_eq(const eng_deep_t *d, uint32_t p, uint32_t q)
{
    const char *a = d->pages[p].title, *b = d->pages[q].title;
    while (*a && *a != ' ' && *a != '+' && cp_up(*a) == cp_up(*b))
        a++, b++;
    return (!*a || *a == ' ' || *a == '+') && (!*b || *b == ' ' || *b == '+');
}
/* the run of pages of one name and instance that page pg is in: its first page, *n its length */
static uint32_t ce_run(const eng_deep_t *d, uint32_t pg, uint32_t *n)
{
    uint32_t a = pg, e = pg;
    while (a && ce_inst(d->pages[a - 1u].title) == ce_inst(d->pages[pg].title) && ce_name_eq(d, a - 1u, pg))
        a--;
    while (e < d->npages && ce_inst(d->pages[e].title) == ce_inst(d->pages[pg].title) && ce_name_eq(d, e, pg))
        e++;
    *n = e - a;
    return a;
}
/* the screen's pages are a CZ-1 envelope: its run (rates and levels R1..R8 / L1..L8 on four pages, SUS and END on a
 * fifth: "DCW 1", "DCW 1+", "DCW 1 B", "DCW 1 B+", "DCW 1 S"; the "cz" wide band): 1, *r0 / *rn the run */
static int ce_is_cz(const eng_deep_t *d, uint32_t pg, uint32_t *r0, uint32_t *rn)
{
    uint32_t q;
    *r0 = ce_run(d, pg, rn);
    for (q = *r0; q < *r0 + *rn; q++)
        if (cp_dcol(&d->pages[q], "SUS") >= 0 && cp_dcol(&d->pages[q], "END") >= 0 && cp_dcol(&d->pages[*r0], "R1") >= 0)
            return 1;
    return 0;
}

/* a page of no instance whose columns are the instances' ("SYNC1" .. "SYNC4": a label ending in a digit) */
static int ce_percol(const eng_page_t *pg)
{
    const char *l = pg->col[0].label;
    uint32_t n = l ? str_len(l) : 0u;
    return n > 1u && l[n - 1u] >= '1' && l[n - 1u] <= '9';
}

/* the group can be shown for this engine (FM6's ENV: its own envelopes, in the patch) */
static int ce_has(const track_t *t, uint32_t g)
{
    return g != CE_ENV || ce_deep(t) || !ENGINES[eng_idx(t->eng_req)]->ownenv;
}

/* screen k of group g: *o; returns the group's number of screens */
static uint32_t ce_scr(const track_t *t, uint32_t g, uint32_t k, ce_scr_t *o)
{
    const eng_deep_t *d = ce_deep(t);
    uint32_t a, b, p, q, e, n = 0;
    o->type = CE_S_PLAT;
    o->n = 0;
    o->hs = 0;
    if (!d || g > CE_MOD || (g == CE_MOD && !ce_has_mod(d)))
        return 1;
    if (d->screens) {                             /* the engine's screen plan for this group (QUAD's) */
        for (p = 0; p < d->nscreens; p++)
            if (d->screens[p].sec == g) {
                const eng_screen_t *h = &d->screens[p];
                if (n == k && h->pg[0] < d->npages) {
                    o->type = CE_S_EDIT;
                    o->hs = (uint8_t)(p + 1u);
                    o->pg[0] = h->pg[0];
                    o->pg[1] = h->pg[1];
                    o->n = (uint8_t)(h->pg[1] < d->npages ? 2u : 1u);
                }
                n++;
            }
        if (n)
            return n;
    }
    ce_range(d, g, &a, &b);
    if (g == CE_OSC || g == CE_LFO) {             /* stacks: a screen per kind of page */
        for (p = a; p < b; p++) {
            for (q = a; q < p && !ce_kind_eq(d, p, q) && !ce_plus_of(d, q, p); q++)
                ;
            if (q < p)                            /* (a kind seen already; a "+" page: its page's screen) */
                continue;
            if (n == k) {
                if (ce_inst(d->pages[p].title)) {    /* up to eight instances (FM6: its six operators) */
                    o->type = CE_S_STACK;
                    for (q = p; q < b && o->n < CR_ED_ROWS; q++)
                        if (ce_kind_eq(d, p, q))
                            o->pg[o->n++] = (uint8_t)q;
                } else if (p + 1u < b && ce_plus_of(d, p, p + 1u)) {   /* "LFO" and "LFO+": edit8, two lanes */
                    o->type = CE_S_EDIT;
                    o->pg[0] = (uint8_t)p;
                    o->pg[1] = (uint8_t)(p + 1u);
                    o->n = 2;
                } else if (ce_percol(&d->pages[p])) {   /* one page across the instances: lane i = column i */
                    o->type = CE_S_TRANS;
                    o->pg[0] = (uint8_t)p;
                    for (q = 0; q < 4u; q++)
                        if (d->pages[p].col[q].label)
                            o->n = (uint8_t)(q + 1u);
                } else {                          /* a page of its own: one lane */
                    o->type = CE_S_PAGE;
                    o->pg[0] = (uint8_t)p;
                    o->n = 1;
                }
            }
            n++;
        }
        for (q = a, e = 0; q < b; q++)
            e += (uint32_t)ce_kind_eq(d, a, q);
        if (g == CE_OSC && ce_inst(d->pages[a].title) && cp_dcol(&d->pages[a], "LEVEL") >= 0 && e <= 4u) {
            /* the mixer (four tall bars: the oscillators' LEVEL; more than four (FM6's six): the stack has them) */
            if (n == k) {
                o->type = CE_S_MIXER;
                for (q = a; q < b && o->n < 4u; q++)
                    if (ce_kind_eq(d, a, q))
                        o->pg[o->n++] = (uint8_t)q;
            }
            n++;
        }
        return n ? n : 1u;
    }
    if (g == CE_MOD && ce_is_matrix(d, a)) {      /* the matrix: eight slots a screen */
        for (p = a; p < b; p += CR_ED_ROWS, n++)
            if (n == k) {
                o->type = CE_S_STACK;
                for (q = p; q < b && q < p + CR_ED_ROWS; q++)
                    o->pg[o->n++] = (uint8_t)q;
            }
        return n ? n : 1u;
    }
    for (p = a; p < b; p = e) {                   /* FILT, ENV (and MOD's plain pages): the pages of one instance
                                                   * and name, two lanes a screen (CZ-1: "PITCH 1" and "DCW 1" apart) */
        for (e = p; e < b && ce_inst(d->pages[e].title) == ce_inst(d->pages[p].title) && ce_name_eq(d, e, p); e++)
            ;
        for (q = p; q < e; q += 2u, n++)
            if (n == k) {
                o->type = CE_S_EDIT;
                o->pg[0] = (uint8_t)q;
                o->pg[1] = (uint8_t)(q + 1u);
                o->n = (uint8_t)(e - q >= 2u ? 2u : 1u);
            }
    }
    return n ? n : 1u;
}
/* the lanes of a screen */
static uint32_t ce_lanes(const track_t *t, uint32_t g, const ce_scr_t *s)
{
    if (s->type == CE_S_MIXER)
        return 1u;
    if (s->type != CE_S_PLAT)
        return s->n ? s->n : 1u;
    return g == CE_MOD && !ce_has_mod(ce_deep(t)) ? 4u : g == CE_MIX ? 2u : 1u;
}
/* part p's group, screen and lane brought into range (an engine change, a shorter group): *s its screen */
static uint32_t ce_fix(const track_t *t, uint32_t p, ce_scr_t *s)
{
    uint32_t g = cx.grp[p], ns, nl;
    if (g >= CE_NSEC || !ce_has(t, g))
        g = cx.grp[p] = CE_OSC;
    ns = ce_scr(t, g, cx.scr[p][g], s);
    if (cx.scr[p][g] >= ns) {
        cx.scr[p][g] = 0;
        ce_scr(t, g, 0, s);
    }
    nl = ce_lanes(t, g, s);
    if (cx.lane[p][g] >= nl)
        cx.lane[p][g] = (uint8_t)(nl - 1u);
    return g;
}

static ce_ref_t ce_r(uint32_t k, uint32_t a, uint32_t b)
{
    ce_ref_t r;
    r.k = (uint8_t)k;
    r.a = (uint8_t)a;
    r.b = (uint8_t)b;
    return r;
}
static void ce_row_trk(ce_view_t *v, uint32_t row, uint32_t cp)
{
    uint32_t c;
    for (c = 0; c < 4u; c++)
        v->ref[row][c] = ce_r(CE_R_TRK, CP_PAGES[cp].id[c], 0);
}
static void ce_cat_num(char *d, uint32_t n, uint32_t sz)
{
    char b[4];
    uint32_t i = 0;
    if (n >= 10u)
        b[i++] = (char)('0' + n / 10u % 10u);
    b[i++] = (char)('0' + n % 10u);
    b[i] = 0;
    cu_cat(d, b, sz);
}
static void ce_label(const track_t *t, ce_ref_t r, const param_desc_t *d, char *out, uint32_t n);
static const param_desc_t *ce_param(const track_t *t, ce_ref_t r, int32_t *v);
static uint32_t ce_cstyle(const track_t *t, const ce_view_t *vw, uint32_t r, uint32_t c);
/* a stack's column heading: the long label of what the lanes have in column c ("Wave"; two kinds: "Sync/Ring"), its
 * trailing digits cut ("SYNC1" -> "Sync") */
static void ce_head(const track_t *t, const eng_deep_t *d, const ce_view_t *v, uint32_t c, char *out, uint32_t n)
{
    char a[12], b[12];
    uint32_t i, l;
    out[0] = a[0] = b[0] = 0;
    for (i = 0; i < v->n; i++) {
        ce_ref_t r = v->ref[i][c];
        char x[12];
        const param_desc_t *pd;
        int32_t val;
        if (r.k != CE_R_DEEP || !(pd = ce_param(t, r, &val)))
            continue;
        if (cp_eq(d->pages[r.a].col[r.b & 3u].label, "WAVE"))
            pd = &d->pages[r.a].col[r.b & 3u];   /* (the oscillators' modes: "Wave" over WAVE / MORPH / NOISE) */
        ce_label(t, r, pd, x, sizeof x);
        for (l = str_len(x); l > 1u && x[l - 1u] >= '0' && x[l - 1u] <= '9'; l--)
            x[l - 1u] = 0;
        if (!a[0])
            cu_cpy(a, x, sizeof a);
        else if (!cu_eq(a, x) && !b[0])
            cu_cpy(b, x, sizeof b);
    }
    cu_cpy(out, a, n);
    if (b[0]) {
        cu_cat(out, "/", n);
        cu_cat(out, b, n);
    }
}

/* the view of part p's current screen */
static void ce_view(const track_t *t, uint32_t p, ce_view_t *v)
{
    static const char *const ROLE[4] = {"amp", "filter", "free", "free"};
    static const char *const H_MOD[4] = {"Source", "Dest", "Amount", ""};
    const eng_deep_t *d = ce_deep(t);
    const engine_t *en = ENGINES[eng_idx(t->eng_req)];
    ce_scr_t sc;
    uint32_t g, k, ln, i, c;
    g = ce_fix(t, p, &sc);
    k = cx.scr[p][g];
    ln = cx.lane[p][g];
    for (i = 0; i < sizeof *v; i++)
        ((uint8_t *)v)[i] = 0;
    v->kind = CR_K_EDIT8;
    v->n = 1;
    v->active = (uint8_t)ln;
    switch (sc.type) {
    case CE_S_STACK:                              /* OSC / LFO / MOD: a lane per page; a column some lanes lack: "-" */
        v->kind = CR_K_STACK;
        v->n = sc.n;
        for (c = 0; c < 4u; c++) {
            uint32_t any = 0;
            for (i = 0; i < sc.n; i++)
                any |= d->pages[sc.pg[i]].col[c].label != 0;
            for (i = 0; i < sc.n; i++)
                v->ref[i][c] = d->pages[sc.pg[i]].col[c].label ? ce_r(CE_R_DEEP, sc.pg[i], c)
                             : ce_r(any ? CE_R_DASH : CE_R_NONE, 0, 0);
        }
        if (g == CE_MOD) {
            v->row0 = (uint8_t)(k * CR_ED_ROWS);
            for (i = 0; i < sc.n; i++)
                if (!d->get(t, sc.pg[i], 0)) {                /* an unused slot: the source only, a dash */
                    v->ref[i][0] = i == ln ? ce_r(CE_R_DEEP, sc.pg[i], 0) : ce_r(CE_R_DASH, 0, 0);
                    v->ref[i][1] = v->ref[i][2] = v->ref[i][3] = ce_r(CE_R_NONE, 0, 0);
                }
            for (c = 0; c < 4u; c++)
                cu_cpy(v->head[c], H_MOD[c], sizeof v->head[c]);
            cu_cpy(v->right, "MOD ", sizeof v->right);
            ce_cat_num(v->right, v->row0 + ln + 1u, sizeof v->right);
            break;
        }
        for (c = 0; c < 4u; c++)
            ce_head(t, d, v, c, v->head[c], sizeof v->head[c]);
        cu_cpy(v->right, d->pages[sc.pg[0]].title, sizeof v->right);   /* the pages' name: "OSC", FM6's "OP", "SCALE" */
        for (c = 0; v->right[c] && v->right[c] != ' '; c++)
            ;
        v->right[c] = 0;
        cu_cat(v->right, " ", sizeof v->right);
        ce_cat_num(v->right, ln + 1u, sizeof v->right);
        cu_cat(v->right, k ? " \267 B" : " \267 A", sizeof v->right);
        if (k > 1u)
            v->right[str_len(v->right) - 1u] = (char)('A' + k);
        break;
    case CE_S_TRANS:                              /* "LFO SYN": lane i = its column i */
        v->kind = CR_K_STACK;
        v->n = sc.n;
        for (i = 0; i < sc.n; i++)
            v->ref[i][0] = d->pages[sc.pg[0]].col[i].label ? ce_r(CE_R_DEEP, sc.pg[0], i) : ce_r(CE_R_DASH, 0, 0);
        ce_head(t, d, v, 0, v->head[0], sizeof v->head[0]);
        cu_cpy(v->right, CE_SEC_NAME[g], sizeof v->right);
        cu_cat(v->right, " ", sizeof v->right);
        ce_cat_num(v->right, ln + 1u, sizeof v->right);
        cu_cat(v->right, " \267 B", sizeof v->right);
        v->right[str_len(v->right) - 1u] = (char)('A' + (k > 25u ? 25u : k));
        break;
    case CE_S_MIXER:                              /* the oscillators' levels, four tall bars */
        v->tall = 1;
        v->active = 0;
        for (i = 0; i < sc.n && i < 4u; i++) {
            int32_t col = cp_dcol(&d->pages[sc.pg[i]], "LEVEL");
            v->ref[0][i] = col >= 0 ? ce_r(CE_R_DEEP, sc.pg[i], (uint32_t)col) : ce_r(CE_R_DASH, 0, 0);
        }
        cu_cpy(v->right, "OSC \267 MIX", sizeof v->right);
        break;
    case CE_S_PAGE:                               /* a page of its own ("VOICE"): one lane, its title on the right */
        for (c = 0; c < 4u; c++)
            v->ref[0][c] = d->pages[sc.pg[0]].col[c].label ? ce_r(CE_R_DEEP, sc.pg[0], c) : ce_r(CE_R_NONE, 0, 0);
        cu_cpy(v->right, d->pages[sc.pg[0]].title, sizeof v->right);
        break;
    case CE_S_EDIT:                               /* FILT / ENV n: the instance's pages under the wide band */
        v->n = sc.n;
        for (i = 0; i < sc.n; i++)
            for (c = 0; c < 4u; c++)
                v->ref[i][c] = d->pages[sc.pg[i]].col[c].label ? ce_r(CE_R_DEEP, sc.pg[i], c) : ce_r(CE_R_NONE, 0, 0);
        if (ce_plan(d, sc.hs)) {                  /* the engine's plan: its band and title ("SYN 1 \267 A") */
            static const uint8_t W[6] = {CR_W_NONE, CR_W_ENV, CR_W_FILTER, CR_W_FILTER, CR_W_ALGO, CR_W_ADE2};
            const eng_screen_t *h = ce_plan(d, sc.hs);
            v->hs = sc.hs;
            v->wide = h->band < 6u ? W[h->band] : (uint8_t)CR_W_NONE;
            v->dp0 = sc.pg[0];
            v->np = sc.n;
            cu_cpy(v->right, h->title ? h->title : d->pages[sc.pg[0]].title, sizeof v->right);
            if (h->letter && sc.n > 1u)
                cu_cat(v->right, ln ? " \267 B" : " \267 A", sizeof v->right);
        } else if (g == CE_ENV && ce_is_cz(d, sc.pg[0], &c, &i)) {   /* CZ-1: a step envelope under the "cz" band */
            static const char *const PART[3] = {" \267 1-4", " \267 5-8", " \267 END"};
            v->wide = CR_W_CZ;
            v->dp0 = (uint8_t)c;
            v->np = (uint8_t)i;
            cu_cpy(v->right, d->pages[c].title, sizeof v->right);   /* "DCW 1" */
            cu_cat(v->right, PART[(sc.pg[0] - c) / 2u > 2u ? 2u : (sc.pg[0] - c) / 2u], sizeof v->right);
        } else if (g == CE_ENV && ce_is_dx(d, sc.pg[0])) {   /* FM6: R1..R4 / L1..L4 under the "dx" band */
            uint32_t n = ce_inst(d->pages[sc.pg[0]].title);
            v->wide = CR_W_DX;
            v->dp0 = sc.pg[0];
            v->np = sc.n;
            if (n) {
                cu_cpy(v->right, "ENV ", sizeof v->right);
                ce_cat_num(v->right, n, sizeof v->right);
            } else {
                cu_cpy(v->right, d->pages[sc.pg[0]].title, sizeof v->right);   /* "PITCH EG" */
            }
        } else if (g == CE_ENV) {
            uint32_t n = ce_inst(d->pages[sc.pg[0]].title);
            v->wide = CR_W_ENV;
            v->dp0 = sc.pg[0];
            v->np = sc.n;
            cu_cpy(v->right, "ENV ", sizeof v->right);
            ce_cat_num(v->right, n ? n : k + 1u, sizeof v->right);
            if (n >= 1u && n <= 4u) {
                cu_cat(v->right, " \267 ", sizeof v->right);
                cu_cat(v->right, ROLE[n - 1u], sizeof v->right);
            }
        } else if (g != CE_FILT || cp_dcol(&d->pages[sc.pg[0]], "CUT") < 0) {   /* no filter (FM6's ALGO, LFO,
                                                                              * FUNC): no band, the page's title */
            cu_cpy(v->right, d->pages[sc.pg[0]].title, sizeof v->right);
        } else {
            uint32_t a, b;
            ce_range(d, CE_FILT, &a, &b);
            v->wide = CR_W_FILTER;
            v->dp0 = (uint8_t)a;
            cu_cpy(v->right, "FILTER", sizeof v->right);
            (void)b;
        }
        break;
    default:                                      /* the platform's pages */
        switch (g) {
        case CE_OSC:
            ce_row_trk(v, 0, CP_EDIT1);
            cu_cpy(v->right, en->page_title[0] ? en->page_title[0] : "EDIT 1", sizeof v->right);
            break;
        case CE_FILT:
            ce_row_trk(v, 0, CP_EDIT2);
            cu_cpy(v->right, en->page_title[1] ? en->page_title[1] : "EDIT 2", sizeof v->right);
            break;
        case CE_ENV:
            v->wide = CR_W_ENV;
            ce_row_trk(v, 0, CP_ENV);
            cu_cpy(v->right, "ENV", sizeof v->right);
            break;
        case CE_LFO:
            ce_row_trk(v, 0, CP_LFO);
            cu_cpy(v->right, "LFO", sizeof v->right);
            break;
        case CE_MOD:
            v->kind = CR_K_STACK;
            v->n = 4;
            for (c = 0; c < 4u; c++)
                cu_cpy(v->head[c], H_MOD[c], sizeof v->head[c]);
            for (i = 0; i < 4u; i++) {
                v->ref[i][0] = ce_r(CE_R_TXT, CE_PMOD[i][0], 0);
                v->ref[i][1] = ce_r(CE_R_TXT, CE_PMOD[i][1], 0);
                v->ref[i][2] = ce_r(CE_R_TRK, CP_PAGES[CP_MOD].id[i], 0);
            }
            cu_cpy(v->right, "MOD ", sizeof v->right);
            ce_cat_num(v->right, ln + 1u, sizeof v->right);
            break;
        case CE_FX:
            ce_row_trk(v, 0, CP_FX);
            cu_cpy(v->right, "FX", sizeof v->right);
            break;
        default:
            v->n = 2;
            ce_row_trk(v, 0, CP_MIX);
            ce_row_trk(v, 1, CP_MIX2);
            cu_cpy(v->right, "MIX", sizeof v->right);
            break;
        }
        break;
    }
    if (v->active >= v->n)
        v->active = 0;
}

/* the parameter of a ref: its descriptor and value (0: none) */
static const param_desc_t *ce_param(const track_t *t, ce_ref_t r, int32_t *v)
{
    const eng_deep_t *d = cp_deep(t);
    if (r.k == CE_R_DEEP && d && r.a < d->npages && d->pages[r.a].col[r.b & 3u].label) {
        *v = d->get(t, r.a, r.b & 3u);
        if (d->desc) {                                          /* a mode-dependent label / names (the VA's MORPH) */
            const param_desc_t *m = d->desc(t, r.a, r.b & 3u);
            if (m)
                return m;
        }
        return &d->pages[r.a].col[r.b & 3u];
    }
    if (r.k == CE_R_TRK && r.a < P_COUNT) {
        *v = t->p[r.a];
        return cp_desc(t, r.a);
    }
    return 0;
}

#ifdef ENGI_VA
/* an LFO page's RATE reads as a division: its LFO's SYNC on (a "SYNCn" column in the LFO section, or a "SYNC" column
 * on a page of the same LFO) */
static int ce_synced(const track_t *t, uint32_t pg)
{
    const eng_deep_t *d = ce_deep(t);
    uint32_t a, b, p, n;
    int32_t k;
    char l[6] = "SYNC0";
    if (!d || pg >= d->npages)
        return 0;
    ce_range(d, CE_LFO, &a, &b);
    if (pg < a || pg >= b || !(n = ce_inst(d->pages[pg].title)))
        return 0;
    l[4] = (char)('0' + n);
    for (p = a; p < b; p++) {
        if ((k = cp_dcol(&d->pages[p], l)) >= 0)
            return d->get(t, p, (uint32_t)k) != 0;
        if (ce_inst(d->pages[p].title) == n && (k = cp_dcol(&d->pages[p], "SYNC")) >= 0)
            return d->get(t, p, (uint32_t)k) != 0;
    }
    return 0;
}
#endif

/* the value text as the mock-ups write it: "683 ms", "643 Hz", "+7", "-12", "on" (Felucca's numbers and names) */
static void ce_text(const track_t *t, ce_ref_t r, const param_desc_t *d, int32_t v, char *out, uint32_t n)
{
    char val[12];
    const char *unit;
    uint32_t i, l;
    param_format(d, v, val, &unit);
#ifdef ENGI_VA
    if (r.k == CE_R_DEEP && t->eng_req == ENGI_VA && cp_eq(d->label, "RATE") && ce_synced(t, r.a)) {
        cu_cpy(out, N_VA_DIV[(uint32_t)clamp(v, 0, 127) * VA_NDIV / 128u], n);   /* a synced LFO: a division */
        return;
    }
#else
    (void)t;
    (void)r;
#endif
    if (d->fmt == F_SEMI || d->fmt == F_DB || (d->fmt == F_INT && d->unit && cu_eq(d->unit, "ct")))
        unit = "";                                              /* (coarse / fine / transpose / level: the number) */
    if (val[0] == '.') {                                        /* ".69" -> "0.69" */
        for (l = str_len(val) + 1u; l > 0u && l < sizeof val; l--)
            val[l] = val[l - 1u];
        val[0] = '0';
    }
    if (d->fmt == F_INT && d->min < 0 && v > 0) {
        cu_cpy(out, "+", n);
        cu_cat(out, val, n);
    } else {
        cu_cpy(out, val, n);
    }
    if (d->fmt == F_ONOFF)
        for (i = 0; out[i]; i++)
            out[i] = (char)(out[i] >= 'A' && out[i] <= 'Z' ? out[i] + 32 : out[i]);
    l = str_len(out);
    if (unit[0] && l + 1u + str_len(unit) + 1u <= n && (d->fmt == F_TIME || d->fmt == F_LFOHZ || d->fmt == F_CUTOFF)) {
        cu_cat(out, " ", n);
        cu_cat(out, unit, n);
    } else if (unit[0] && l + str_len(unit) + 1u <= n) {
        cu_cat(out, unit, n);
    }
}

/* a parameter's long label: cr_pages.c's tables, else its short one capitalised ("SYNC1" -> "Sync1") */
static void ce_label(const track_t *t, ce_ref_t r, const param_desc_t *d, char *out, uint32_t n)
{
    uint32_t i;
    if (r.k == CE_R_TRK)
        for (i = 0; i < NELEM(CP_LABEL); i++)
            if (CP_LABEL[i].id == r.a) {
                cu_cpy(out, CP_LABEL[i].label, n);
                return;
            }
    for (i = 0; i < NELEM(CP_DLABEL); i++)
        if (cp_eq(d->label, CP_DLABEL[i].s)) {
            cu_cpy(out, CP_DLABEL[i].l, n);
            return;
        }
    cu_cpy(out, d->label, n);
    for (i = 1; out[i]; i++)
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = (char)(out[i] + 32);
    (void)t;
}

static uint8_t ce_pct(const param_desc_t *d, int32_t v)
{
    uint32_t q = cp_q8(d, v);
    return (uint8_t)(q > 255u ? 255u : q);
}

/* one cell: label (edit8), value, its glyph and fill (the mock-ups' choice by the kind of parameter) */
static void ce_cell(const track_t *t, const ce_view_t *vw, ce_ref_t r, cr_cell_t *c)
{
    const param_desc_t *d;
    int32_t v = 0;
    int stack = vw->kind == CR_K_STACK;
    if (r.k == CE_R_NONE)
        return;
    c->flags = CR_CF_ON;
    if (r.k == CE_R_DASH) {
        cu_cpy(c->value, "-", sizeof c->value);
        return;
    }
    if (r.k == CE_R_TXT) {
        cu_cpy(c->value, CE_TXT[r.a % NELEM(CE_TXT)], sizeof c->value);
        return;
    }
    if (!(d = ce_param(t, r, &v))) {
        c->flags = 0;
        return;
    }
    if (!stack)
        ce_label(t, r, d, c->label, sizeof c->label);
    ce_text(t, r, d, v, c->value, sizeof c->value);
    c->pct = ce_pct(d, v);
    if (r.k == CE_R_DEEP && cp_eq(cp_deep(t)->pages[r.a].col[r.b & 3u].label, "WAVE") && cp_eq(d->label, "MORPH")) {
        const eng_deep_t *dd = cp_deep(t);                       /* the VA's MORPH: the morphed wave at its position */
        int32_t m = d->max > d->min ? (v - d->min) * 127 / (d->max - d->min) : 0, k;
        if (d->fmt == F_ENUM && r.a + 1u < dd->npages && (k = cp_dcol(&dd->pages[r.a + 1u], "SHAPE")) >= 0)
            m = dd->get(t, r.a + 1u, (uint32_t)k);              /* (an older table: the position is OSC n+'s SHAPE) */
        c->glyph = CR_G_MORPH;
        c->pct = (uint8_t)(clamp(m, 0, 127) * 255 / 127);
        if (d->fmt == F_INT && d->names && d->max > d->min) {   /* the position's name, whole ("SAW>RMP"; params.c's
                                                                 * F_INT list: n names over the range) */
            uint32_t n = 0;
            while (d->names[n])
                n++;
            if (n)
                cu_cpy(c->value, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * n / (uint32_t)(d->max - d->min + 1)],
                       sizeof c->value);
        }
    } else if (r.k == CE_R_DEEP && cp_eq(cp_deep(t)->pages[r.a].col[r.b & 3u].label, "WAVE")
               && (cp_eq(d->label, "NOISE") || cp_eq(d->label, "NTYPE"))) {
        c->glyph = CR_G_NOISE;                                    /* NOISE: WHITE BROWN VINYL */
        c->pct = (uint8_t)(cp_has(c->value, "BRO") ? 128u : cp_has(c->value, "VIN") ? 255u : 0u);
    } else if (d->fmt == F_ENUM && cp_has(d->label, "WAVE")) {   /* the selected waveform */
        const char *w = c->value;
        c->pct = 128;
        c->glyph = cp_has(w, "SAW") || cp_has(w, "RMP") ? CR_G_SAW
                 : cp_has(w, "SQ") || cp_has(w, "PUL") || cp_has(w, "PW") ? CR_G_SQUARE
                 : cp_has(w, "NOI") || cp_has(w, "NZ") ? CR_G_DOTS
                 : cp_has(w, "S&H") || cp_has(w, "RND") || cp_has(w, "STEP") ? CR_G_STEPS
                 : cp_has(w, "TRI") ? CR_G_MORPH          /* (the triangle: the morph glyph at 24) */
                 : CR_G_WAVE;
        if (c->glyph == CR_G_MORPH)
            c->pct = 24 * 255 / 127;
        if (cp_has(w, "PW") && r.k == CE_R_DEEP) {               /* PWM: the width is the oscillator's SHAPE */
            const eng_deep_t *dd = cp_deep(t);
            int32_t k = r.a + 1u < dd->npages ? cp_dcol(&dd->pages[r.a + 1u], "SHAPE") : -1;
            if (k >= 0)
                c->pct = ce_pct(&dd->pages[r.a + 1u].col[k], dd->get(t, r.a + 1u, (uint32_t)k));
        }
    } else if (cp_eq(d->label, "FTYPE")) {                      /* the filter's position: its name, a cycle of dots */
        c->glyph = CR_G_DOTS;
        c->flags |= CR_CF_PCT;
        cu_cpy(c->value, cr_ftype_name((uint32_t)v), sizeof c->value);
    } else if (d->fmt == F_ENUM && cp_eq(d->label, "TYPE")) {
        c->glyph = CR_G_DOTS;
        c->flags |= CR_CF_PCT;
    } else if (d->fmt == F_ENUM || d->fmt == F_ONOFF) {
        ;                                                         /* text */
    } else if (d->fmt == F_LFOHZ) {
        c->glyph = CR_G_KNOB;
        c->flags |= CR_CF_PCT;
    } else if (stack && (d->fmt == F_SEMI || d->fmt == F_INT)) {
        ;                                                         /* coarse / fine: the number */
    } else {
        c->glyph = CR_G_BAR;
        c->flags |= CR_CF_PCT | (d->min < 0 ? CR_CF_BIP : 0u);
    }
}

/* ------------------------------------------------------------- the state --- */
/* the ENV n / LFO n the current group shows (its lane's instance), remembered for the quick mapping */
static void ce_msrc_note(void)
{
    track_t *t = ce_trk();
    const eng_deep_t *d = ce_deep(t);
    uint32_t p = ce.part & 1u, g = cx.grp[p] % CE_NSEC, n = 0, ln = cx.lane[p][g];
    ce_scr_t sc;
    if (!d || (g != CE_ENV && g != CE_LFO))
        return;
    ce_scr(t, g, cx.scr[p][g], &sc);
    if (sc.type == CE_S_EDIT || sc.type == CE_S_PAGE)
        n = ce_inst(d->pages[sc.pg[0]].title);
    else if (sc.type == CE_S_STACK)
        n = ce_inst(d->pages[sc.pg[ln < sc.n ? ln : 0u]].title);
    else if (sc.type == CE_S_TRANS)
        n = ln + 1u;
    if (n >= 1u && n <= 9u)
        cx.msrc[p][g == CE_LFO] = (uint8_t)n;
}
static void ce_trace_nav(void)
{
    ce_msrc_note();
    uint32_t p = ce.part & 1u, g = cx.grp[p] % CE_NSEC;
    cu_trace("edit: group %s screen %u lane %u part %u\n", CE_SEC_NAME[g], (unsigned)cx.scr[p][g] + 1u,
             (unsigned)cx.lane[p][g] + 1u, (unsigned)p);
    (void)p;
    (void)g;
}

static void ce_open(uint32_t part)
{
    ce_scr_t sc;
    ce.part = (uint8_t)(part & 1u);
    cu.opt_open = 0;
    cu.lock = L_NONE;
    cu.page = PG_EDIT;
    cx.hot_r = 0;
    cx.shift = 0;
    ce_fix(ce_trk(), ce.part, &sc);
    cu_trace("edit: open part %u\n", (unsigned)ce.part);
    ce_trace_nav();
}
static void ce_close(void)
{
    if (cu.page == PG_EDIT)
        cu.page = PG_NONE;
    cx.shift = 0;
    cu_trace("edit: close\n");
}

/* a group's button tapped: another group opens (where it was left), the same one cycles its screens */
static void ce_group(uint32_t g)
{
    track_t *t = ce_trk();
    uint32_t p = ce.part & 1u, ns, nl;
    ce_scr_t sc;
    if (!ce_has(t, g)) {
        cu_message("FM6: own envelopes", CR_COL_WHITE);
        return;
    }
    if (cx.grp[p] != g) {
        cx.grp[p] = (uint8_t)g;
        ce_fix(t, p, &sc);
    } else {
        ns = ce_scr(t, g, 0, &sc);
        cx.scr[p][g] = (uint8_t)((cx.scr[p][g] + 1u) % ns);
        ce_scr(t, g, cx.scr[p][g], &sc);
        nl = ce_lanes(t, g, &sc);
        if (cx.lane[p][g] >= nl)                  /* (the lane kept: OSC 3 stays OSC 3 on the next stack) */
            cx.lane[p][g] = (uint8_t)(nl - 1u);
    }
    cx.hot_r = 0;
    ce_trace_nav();
}

static int ce_owns(uint32_t b)
{
    uint32_t s;
    if (cu.page != PG_EDIT)
        return 0;
    if (b == B_GLO)
        return 1;
    for (s = 0; s < CE_NSEC; s++)
        if (CE_SEC_BTN[s] == b)
            return 1;
    return 0;
}

static int ce_button(uint32_t b, uint32_t ev)
{
    uint32_t s;
    if (cu.page != PG_EDIT)
        return 0;
    if (ev == CE_SHIFT) {                         /* SHIFT + EDIT: the other part's sound, where it was left */
        ce.part ^= 1u;
        cx.hot_r = 0;
        cu_trace("edit: part %u\n", (unsigned)ce.part);
        ce_trace_nav();
        return 1;
    }
    if (b == BT_EDIT && ev == CE_TAP) {
        ce_close();
        return 1;
    }
    if (b == B_GLO) {                             /* SHIFT: a tap latches / unlatches it; held: momentary */
        if (ev == CE_TAP) {
            cx.shift ^= 1u;
            cu_trace("edit: shift %s\n", cx.shift ? "latched" : "off");
        }
        return 1;
    }
    for (s = 0; s < CE_NSEC; s++)
        if (CE_SEC_BTN[s] == b) {
            if (ev == CE_TAP)                     /* (held: nothing, reserved) */
                ce_group(s);
            return 1;
        }
    return 0;                                     /* SAVE, PERF, HOME, OCT: as outside */
}

/* SELECT: s lanes on (both ways round), across the group's screens (wrapping) */
static void ce_select(int32_t s)
{
    track_t *t = ce_trk();
    uint32_t p = ce.part & 1u, g, ns, k, ln, k0, l0;
    ce_scr_t sc;
    g = ce_fix(t, p, &sc);
    ns = ce_scr(t, g, 0, &sc);
    k = k0 = cx.scr[p][g];
    ln = l0 = cx.lane[p][g];
    ce_scr(t, g, k, &sc);
    for (; s > 0; s--) {
        if (ln + 1u < ce_lanes(t, g, &sc)) {
            ln++;
        } else {
            k = (k + 1u) % ns;
            ln = 0;
            ce_scr(t, g, k, &sc);
        }
    }
    for (; s < 0; s++) {
        if (ln) {
            ln--;
        } else {
            k = (k + ns - 1u) % ns;
            ce_scr(t, g, k, &sc);
            ln = ce_lanes(t, g, &sc) - 1u;
        }
    }
    if (k == k0 && ln == l0)
        return;
    cx.scr[p][g] = (uint8_t)k;
    cx.lane[p][g] = (uint8_t)ln;
    cx.hot_r = 0;
    ce_trace_nav();
}

/* ------------------------------------------------- the quick mapping --- */
/* the matrix: the MOD section's pages with SRC DST AMT columns (eng_deep_t; their values are the engine's numbers) */
typedef struct { uint8_t pg; int8_t cs, cd, ca; } ce_slot_t;
static int ce_slot(const eng_deep_t *d, uint32_t pg, ce_slot_t *o)
{
    o->pg = (uint8_t)pg;
    o->cs = (int8_t)cp_dcol(&d->pages[pg], "SRC");
    o->cd = (int8_t)cp_dcol(&d->pages[pg], "DST");
    o->ca = (int8_t)cp_dcol(&d->pages[pg], "AMT");
    return o->cs >= 0 && o->cd >= 0 && o->ca >= 0;
}
/* the matrix's destination of a cell's parameter, -1 none */
static int32_t ce_dst(const track_t *t, ce_ref_t r)
{
    const eng_deep_t *d = ce_deep(t);
    if (!d || !d->mod_dst)
        return -1;
    if (r.k == CE_R_DEEP)
        return d->mod_dst(t, r.a, r.b & 3u);
    return r.k == CE_R_TRK ? d->mod_dst(t, ENG_MOD_TRK, r.a) : -1;
}
/* a source's name in the SRC column ("ENV2", "LFO1"), its value, -1 none */
static int32_t ce_src_of(const eng_deep_t *d, const ce_slot_t *sl, const char *name)
{
    const param_desc_t *c = &d->pages[sl->pg].col[(uint32_t)sl->cs];
    int32_t v;
    for (v = c->min; c->names && v <= c->max; v++)
        if (c->names[v] && cp_eq(c->names[v], name))
            return v;
    return -1;
}
static const char *ce_name_of(const eng_deep_t *d, uint32_t pg, uint32_t col, int32_t v)
{
    const param_desc_t *c = &d->pages[pg].col[col & 3u];
    return c->names && v >= c->min && v <= c->max && c->names[v] ? c->names[v] : "?";
}
/* a destination's name ("CUT") */
static const char *ce_dst_name(const eng_deep_t *d, int32_t dst)
{
    uint32_t a, b, pg;
    ce_slot_t sl;
    ce_range(d, CE_MOD, &a, &b);
    for (pg = a; pg < b; pg++)
        if (ce_slot(d, pg, &sl))
            return ce_name_of(d, pg, (uint32_t)sl.cd, dst);
    return "?";
}
/* a source's colour (its name: ENV yellow, LFO red, the player's VEL KEY RAND MODW blue) */
static uint32_t ce_src_col(const char *n)
{
    return cp_has(n, "ENV") ? CR_COL_YELLOW : cp_has(n, "LFO") ? CR_COL_RED : CR_COL_BLUE;
}
/* "ENV2 -> CUT +12 slot 1" (the trace, the hot cell's "ENV2 +12") */
static void ce_amt_text(char *o, const char *src, int32_t v, uint32_t n)
{
    char b[8];
    cu_cpy(o, src, n);
    cu_cat(o, " ", n);
    cu_int(b, v, 1, sizeof b);
    cu_cat(o, b, n);
}

/* ENV (env 1) or LFO (env 0) held + KNOB k turned s: the last ENV n / LFO n shown modulates the parameter under k */
static void ce_map(uint32_t knob, int32_t s, uint32_t fine, uint32_t env)
{
    track_t *t = ce_trk();
    const eng_deep_t *d = ce_deep(t);
    uint32_t p = ce.part & 1u, a, b, pg, n;
    int32_t dst, src = -1, amt0, amt;
    ce_view_t vw;
    ce_slot_t sl, use;
    char nm[8], tx[12];
    ce_view(t, p, &vw);
    dst = ce_dst(t, vw.ref[vw.active][knob & 3u]);
    use.pg = 0xFF;
    if (dst <= 0 || !d) {
        cu_message("not modulatable", CR_COL_WHITE);
        cu_trace("mod: not modulatable\n");
        return;
    }
    n = cx.msrc[p][!env] ? cx.msrc[p][!env] : env ? 2u : 1u;
    cu_cpy(nm, env ? "ENV" : "LFO", sizeof nm);
    ce_cat_num(nm, n, sizeof nm);
    ce_range(d, CE_MOD, &a, &b);
    for (pg = a; pg < b; pg++) {                  /* that source to that destination, else the first free slot */
        if (!ce_slot(d, pg, &sl))
            continue;
        if (src < 0)
            src = ce_src_of(d, &sl, nm);
        if (d->get(t, pg, (uint32_t)sl.cd) == dst && d->get(t, pg, (uint32_t)sl.cs) == src) {
            use = sl;
            break;
        }
        if (use.pg == 0xFF && (!d->get(t, pg, (uint32_t)sl.cs) || !d->get(t, pg, (uint32_t)sl.cd)))
            use = sl;
    }
    if (src <= 0) {
        cu_message("not modulatable", CR_COL_WHITE);
        cu_trace("mod: not modulatable\n");
        return;
    }
    if (use.pg == 0xFF) {
        cu_message("matrix full", CR_COL_RED);
        cu_trace("mod: %s -> %s matrix full\n", nm, ce_dst_name(d, dst));
        return;
    }
    if (d->get(t, use.pg, (uint32_t)use.cs) != src || d->get(t, use.pg, (uint32_t)use.cd) != dst) {   /* a free slot */
        d->set(t, use.pg, (uint32_t)use.ca, 0);
        d->set(t, use.pg, (uint32_t)use.cs, src);
        d->set(t, use.pg, (uint32_t)use.cd, dst);
    }
    amt0 = d->get(t, use.pg, (uint32_t)use.ca);
    amt = cp_dstep(&d->pages[use.pg].col[(uint32_t)use.ca], amt0, s, fine || cx.shift);
    if (amt != amt0)
        d->set(t, use.pg, (uint32_t)use.ca, amt);
    amt = d->get(t, use.pg, (uint32_t)use.ca);
    cu_edited(ce.part);
    cx.hot_r = (uint8_t)(vw.active + 1u);
    cx.hot_c = (uint8_t)(knob & 3u);
    cx.hot_t0 = cu_now();
    cx.hot_slot = (uint8_t)(use.pg + 1u);
    cx.hot_src = (uint8_t)src;
    ce_amt_text(tx, nm, amt, sizeof tx);
    cu_trace("mod: %s -> %s %s slot %u\n", nm, ce_name_of(d, use.pg, (uint32_t)use.cd, dst), tx + str_len(nm) + 1u,
             (unsigned)(use.pg - a + 1u));
    (void)tx;
}

/* OCT- held + KNOB k turned: every matrix slot to the parameter under k cleared */
static void ce_unmap(uint32_t knob)
{
    track_t *t = ce_trk();
    const eng_deep_t *d = ce_deep(t);
    uint32_t a, b, pg, n = 0;
    int32_t dst;
    ce_view_t vw;
    ce_slot_t sl;
    ce_view(t, ce.part & 1u, &vw);
    dst = ce_dst(t, vw.ref[vw.active][knob & 3u]);
    if (dst <= 0 || !d) {
        cu_message("not modulatable", CR_COL_WHITE);
        cu_trace("mod: not modulatable\n");
        return;
    }
    ce_range(d, CE_MOD, &a, &b);
    for (pg = a; pg < b; pg++)
        if (ce_slot(d, pg, &sl) && d->get(t, pg, (uint32_t)sl.cd) == dst) {
            d->set(t, pg, (uint32_t)sl.ca, 0);
            d->set(t, pg, (uint32_t)sl.cs, 0);
            d->set(t, pg, (uint32_t)sl.cd, 0);
            n++;
        }
    if (n)
        cu_edited(ce.part);
    cx.hot_r = 0;
    cu_message(n ? "cleared" : "no modulation", CR_COL_WHITE);
    cu_trace("mod: clear %s, %u slots\n", ce_dst_name(d, dst), (unsigned)n);
}

/* the editor's modifiers on the knobs: ENV / LFO held (the quick mapping), OCT- held (clear); 0 none */
static uint32_t ce_kmod(void)
{
    if ((cu.bheld >> B_OCTDN) & 1u)
        return 3u;
    if (cu.armed == B_ENV && ((cu.bheld >> B_ENV) & 1u))
        return 1u;
    if (cu.armed == B_LFO && ((cu.bheld >> B_LFO) & 1u))
        return 2u;
    return 0;
}

/* KNOB 1..4 on the active lane's cell (fine: SHIFT latched or held, one step) */
static void ce_knob(uint32_t knob, int32_t s, uint32_t fine)
{
    track_t *t = ce_trk();
    ce_view_t vw;
    ce_ref_t r;
    const param_desc_t *d;
    int32_t v0 = 0, v;
    uint32_t i, m = ce_kmod();
    char b[12];
    if (m == 3u) {                                 /* OCT- held: clear the parameter's modulation */
        cu.oct_mod = 1;                            /* (its release: no octave step) */
        ce_unmap(knob);
        return;
    }
    if (m) {                                       /* ENV / LFO held: map */
        ce_map(knob, s, fine, m == 1u);
        return;
    }
    ce_view(t, ce.part & 1u, &vw);
    r = vw.ref[vw.active][knob & 3u];
    if (!(d = ce_param(t, r, &v0)))
        return;
    v = cp_dstep(d, v0, s, fine || cx.shift);
    if (r.k == CE_R_DEEP && d->names && (ce_cstyle(t, &vw, vw.active, knob & 3u) == ENG_C_RATIO ||
                                         ce_cstyle(t, &vw, vw.active, knob & 3u) == ENG_C_BIG))
        v = clamp(v0 + s, d->min, d->max);        /* (a ratio: one step a detent, as the Digitone's) */
    cx.hot_slot = 0;
    cx.hot_r = (uint8_t)(vw.active + 1u);
    cx.hot_c = (uint8_t)(knob & 3u);
    cx.hot_t0 = cu_now();
    if (r.k == CE_R_DEEP) {
        const eng_deep_t *dd = cp_deep(t);
        if (v != v0) {
            dd->set(t, r.a, r.b, v);
            v = dd->get(t, r.a, r.b);
            cu_edited(ce.part);
        }
        cp_value(d, v, b, sizeof b);
        cu_trace("deep: part %u page %u %s col %u %s %d -> %d (%s)%s\n", (unsigned)ce.part, (unsigned)r.a,
                 dd->pages[r.a].title, (unsigned)r.b, d->label, (int)v0, (int)v, b, psnd[ce.part].edited ? " edited" : "");
        return;
    }
    fm1_irq_off();
    t->p[r.a] = (int16_t)v;
    fm1_irq_on();
    if (!ce.part)                                  /* part 0's sends are the FX amounts */
        for (i = 0; i < CU_NFX; i++)
            if (CU_FX[i].send == r.a) {
                cs.fx_amt[i] = (uint8_t)v;
                cs.fx_on = 1;
                cu_fx_apply();
            }
    if (v != v0)
        cu_edited(ce.part);
    cp_value(d, v, b, sizeof b);
    cu_trace("param: part %u %s %s %d -> %d (%s)\n", (unsigned)ce.part, CE_SEC_NAME[cx.grp[ce.part & 1u] % CE_NSEC],
             d->label, (int)v0, (int)v, b);
}

/* -------------------------------------------------------------- the LEDs --- */
/* EDIT blinks, the group's button lit, the other group buttons dim (cr_leds: every button's dim is on); SHIFT lit
 * while latched (or held) */
static void ce_leds(uint8_t *nl, uint32_t blink)
{
    uint8_t m[FM1_NCOL];
    uint32_t s, k;
    for (k = 0; k < FM1_NCOL; k++)
        m[k] = 0;
    for (s = 0; s < CE_NSEC; s++)
        cu_led(m, panel.btn[CE_SEC_BTN[s]], 1);
    cu_led(m, panel.btn[BT_EDIT], 1);
    for (k = 0; k < FM1_NCOL; k++)
        nl[k] &= (uint8_t)~m[k];
    cu_led(nl, panel.btn[BT_EDIT], (int)blink);
    cu_led(nl, panel.btn[CE_SEC_BTN[cx.grp[ce.part & 1u] % CE_NSEC]], 1);
    if (cx.shift)
        cu_led(nl, panel.btn[B_GLO], 1);
}

/* ------------------------------------------------------------ the screen --- */
/* the band's values from the view: env a h d s r, filter cut res type drive (Q8 of 255) */
static void ce_band(const track_t *t, const ce_view_t *vw, uint8_t *o)
{
    static const char *const EL[5] = {"ATK", "HOLD", "DEC", "SUS", "REL"};
    const eng_deep_t *d = cp_deep(t);
    uint32_t i;
    int32_t k, pg;
    for (i = 0; i < 20u; i++)
        o[i] = 0;
    o[16] = 8;
    if (vw->wide == CR_W_CZ && d && vw->np) {               /* CZ-1: R1..R8, L1..L8, SUS, END from the run's pages */
        for (pg = vw->dp0; pg < vw->dp0 + vw->np; pg++)
            for (i = 0; i < 4u; i++) {
                const char *l = d->pages[pg].col[i].label;
                int32_t v;
                if (!l)
                    continue;
                v = d->get(t, (uint32_t)pg, i);
                if ((l[0] == 'R' || l[0] == 'L') && l[1] >= '1' && l[1] <= '8' && !l[2])
                    o[(l[0] == 'L' ? 8u : 0u) + (uint32_t)(l[1] - '1')] = (uint8_t)clamp(v, 0, 99);
                else if (cp_eq(l, "SUS"))
                    o[16] = (uint8_t)clamp(v, 0, 8);
                else if (cp_eq(l, "END"))
                    o[17] = (uint8_t)clamp(v, 0, 7);
            }
    } else if (vw->wide == CR_W_DX && d && vw->np) {               /* FM6: R1..R4 on the first page, L1..L4 on the second */
        for (i = 0; i < 4u; i++) {
            o[i] = (uint8_t)clamp(d->get(t, vw->dp0, i), 0, 99);
            o[4u + i] = vw->np > 1u ? (uint8_t)clamp(d->get(t, vw->dp0 + 1u, i), 0, 99) : 99u;
        }
        o[9] = !ce_inst(d->pages[vw->dp0].title);           /* (no instance: the pitch EG) */
    } else if (vw->wide == CR_W_ENV && d && ce_deep(t) && vw->np) {
        for (i = 0; i < 5u; i++)
            for (pg = vw->dp0; pg < vw->dp0 + vw->np; pg++)
                if ((k = cp_dcol(&d->pages[pg], EL[i])) >= 0)
                    o[i] = ce_pct(&d->pages[pg].col[k], d->get(t, (uint32_t)pg, (uint32_t)k));
    } else if (vw->wide == CR_W_ENV) {
        static const uint8_t ID[5] = {P_ATK, 0xFF, P_DEC, P_SUS, P_REL};
        for (i = 0; i < 5u; i++)
            if (ID[i] != 0xFFu)
                o[i] = ce_pct(cp_desc(t, ID[i]), t->p[ID[i]]);
    } else if (vw->wide == CR_W_FILTER && d) {               /* cut res ftype drive, from the FILTER section's pages */
        static const char *const FL[6] = {"CUT", "RES", "FTYPE", "DRIVE", "TYPE", "MORPH"};
        int32_t typ = -1, mor = 0;
        uint32_t a, b;
        ce_range(d, CE_FILT, &a, &b);
        for (pg = (int32_t)a; pg < (int32_t)b; pg++)
            for (i = 0; i < 6u; i++)
                if ((k = cp_dcol(&d->pages[pg], FL[i])) >= 0) {
                    int32_t v = d->get(t, (uint32_t)pg, (uint32_t)k);
                    if (i == 2u)
                        o[2] = (uint8_t)(v & 127);
                    else if (i == 4u)
                        typ = clamp(v, 0, 3);
                    else if (i == 5u)
                        mor = v;
                    else
                        o[i] = ce_pct(&d->pages[pg].col[k], v);
                }
        if (typ >= 0)                                         /* (an older table: TYPE, MORPH on from it) */
            o[2] = (uint8_t)((typ * 32 + mor) & 127);
    }
}
/* the CZ-1 step (1..8; 0 none) of the cell just turned: R k / L k, SUS its step, END its step */
static uint32_t ce_cz_step(const track_t *t, ce_ref_t r)
{
    const param_desc_t *d;
    int32_t v;
    if (r.k != CE_R_DEEP || !(d = ce_param(t, r, &v)))
        return 0;
    if ((d->label[0] == 'R' || d->label[0] == 'L') && d->label[1] >= '1' && d->label[1] <= '8' && !d->label[2])
        return (uint32_t)(d->label[1] - '0');
    if (cp_eq(d->label, "SUS") || cp_eq(d->label, "END"))
        return v >= 0 && v < 8 ? (uint32_t)v + 1u : 0u;
    return 0;
}
/* the envelope segment (1 A, 2 H, 3 D, 4 S, 5 R; 0 none) of the cell just turned */
static uint32_t ce_seg(const track_t *t, ce_ref_t r)
{
    static const char *const EL[5] = {"ATK", "HOLD", "DEC", "SUS", "REL"};
    static const uint8_t ID[5] = {P_ATK, 0xFF, P_DEC, P_SUS, P_REL};
    const param_desc_t *d;
    int32_t v;
    uint32_t i;
    if (!(d = ce_param(t, r, &v)))
        return 0;
    if (r.k == CE_R_DEEP && (d->label[0] == 'R' || d->label[0] == 'L') && d->label[1] >= '1' && d->label[1] <= '4' &&
        !d->label[2])                                          /* FM6's R1..R4 / L1..L4: the segment they end */
        return (uint32_t)(d->label[1] - '0');
    for (i = 0; i < 5u; i++)
        if (r.k == CE_R_TRK ? r.a == ID[i] : cp_eq(d->label, EL[i]))
            return i + 1u;
    return 0;
}

/* the battery as Felucca's header showed it (ui_draw.c batt_shown): 4 on USB power, else 0..3 by the smoothed ADC
 * (thresholds 531 / 561 / 591) */


/* the matrix's marks: per destination the colour of its source (white: several), 0 none */
#define CE_NDST 48u
static void ce_marks(const track_t *t, uint8_t *mc)
{
    const eng_deep_t *d = ce_deep(t);
    uint32_t a, b, pg, i;
    uint8_t seen[CE_NDST];
    ce_slot_t sl;
    for (i = 0; i < CE_NDST; i++)
        mc[i] = seen[i] = 0;
    if (!d || !d->mod_dst)
        return;
    ce_range(d, CE_MOD, &a, &b);
    for (pg = a; pg < b; pg++) {
        int32_t sr, ds;
        if (!ce_slot(d, pg, &sl) || !(sr = d->get(t, pg, (uint32_t)sl.cs)) || (ds = d->get(t, pg, (uint32_t)sl.cd)) <= 0 ||
            ds >= (int32_t)CE_NDST || !d->get(t, pg, (uint32_t)sl.ca))
            continue;
        mc[ds] = (uint8_t)(seen[ds] && seen[ds] != sr ? CR_COL_WHITE : ce_src_col(ce_name_of(d, pg, (uint32_t)sl.cs, sr)));
        seen[ds] = (uint8_t)sr;
    }
}

/* ------------------------------------------------- the engine's screen plan --- */
/* the plan's band values (eng_screen_t.band): algo fdbk mix; A's attack decay end level, B's; the filter cut res
 * ftype drive (+ shown, base, width: the window), from the screen's / the FILTER section's columns by label. The
 * number of wv values, 0: not a plan band (the AHDSR: ce_band) */
static int32_t ce_dget(const track_t *t, const eng_deep_t *d, uint32_t a, uint32_t b, const char *l, int32_t *v)
{
    uint32_t pg;
    int32_t k;
    for (pg = a; pg < b && pg < d->npages; pg++)
        if ((k = cp_dcol(&d->pages[pg], l)) >= 0) {
            *v = d->get(t, pg, (uint32_t)k);
            return (int32_t)ce_pct(&d->pages[pg].col[k], *v);
        }
    *v = 0;
    return -1;
}
static uint32_t ce_band_plan(const track_t *t, const ce_view_t *vw, uint8_t *o)
{
    const eng_deep_t *d = ce_deep(t);
    const eng_screen_t *h = ce_plan(d, vw->hs);
    uint32_t i, a, b;
    int32_t v, q;
    if (!h)
        return 0;
    if (h->band == ENG_B_ALGO) {
        a = vw->dp0;
        b = vw->dp0 + vw->np;
        ce_dget(t, d, a, b, "ALGO", &v);
        o[0] = (uint8_t)clamp(v, 1, 8);
        o[1] = (uint8_t)clamp(ce_dget(t, d, a, b, "FDBK", &v), 0, 255);
        q = ce_dget(t, d, a, b, "MIX", &v);
        o[2] = (uint8_t)(q < 0 ? 128 : clamp(q, 0, 255));
        return 3;
    }
    if (h->band == ENG_B_ADE2) {                  /* rows A and B: their four columns each */
        for (i = 0; i < 8u; i++) {
            uint32_t pg = vw->dp0 + i / 4u;
            const param_desc_t *c = pg < d->npages && i / 4u < vw->np ? &d->pages[pg].col[i & 3u] : 0;
            o[i] = c && c->label ? ce_pct(c, d->get(t, pg, i & 3u)) : 0u;
        }
        return 9;
    }
    if (h->band == ENG_B_FILTER || h->band == ENG_B_WINDOW) {
        ce_range(d, CE_FILT, &a, &b);
        q = ce_dget(t, d, a, b, "FREQ", &v);
        o[0] = (uint8_t)clamp(q >= 0 ? q : ce_dget(t, d, a, b, "CUT", &v), 0, 255);
        q = ce_dget(t, d, a, b, "RESO", &v);
        o[1] = (uint8_t)clamp(q >= 0 ? q : ce_dget(t, d, a, b, "RES", &v), 0, 255);
        o[2] = 0;
        for (i = a; i < b; i++) {                 /* the type by its name: LP 0, BP 32, HP 64 (the band's positions) */
            int32_t k = cp_dcol(&d->pages[i], "TYPE");
            if (k >= 0) {
                const param_desc_t *c = &d->pages[i].col[k];
                v = d->get(t, i, (uint32_t)k);
                if (c->names && v >= c->min && v <= c->max)
                    o[2] = (uint8_t)(cp_eq(c->names[v - c->min], "HP") ? 64 : cp_eq(c->names[v - c->min], "BP") ? 32 : 0);
                break;
            }
        }
        o[3] = (uint8_t)clamp(ce_dget(t, d, a, b, "DRIVE", &v), 0, 255);
        if (h->band != ENG_B_WINDOW)
            return 4;
        o[4] = 1;
        o[5] = (uint8_t)clamp(ce_dget(t, d, a, b, "BASE", &v), 0, 255);
        o[6] = (uint8_t)clamp(ce_dget(t, d, a, b, "WIDTH", &v), 0, 255);
        return 7;
    }
    return 0;
}
/* the plan's style of the cell on row r, column c (ENG_C_*) */
static uint32_t ce_cstyle(const track_t *t, const ce_view_t *vw, uint32_t r, uint32_t c)
{
    const eng_screen_t *h = ce_plan(ce_deep(t), vw->hs);
    return h && r < 2u && c < 4u ? h->cell[r][c] : ENG_C_RULE;
}
/* "TRI" -> "Tri", "RAT A" -> "Rat A" (words of min or more capitals; "LP", "x16" stay at 3) */
static void ce_title_case(char *s, uint32_t min)
{
    uint32_t i = 0, j, n;
    while (s[i]) {
        for (n = 0; s[i + n] && s[i + n] != ' '; n++)
            ;
        for (j = 0; j < n && s[i + j] >= 'A' && s[i + j] <= 'Z'; j++)
            ;
        if (n >= min && j == n)
            for (j = 1; j < n; j++)
                s[i + j] = (char)(s[i + j] + 32);
        i += n;
        while (s[i] == ' ')
            i++;
    }
}
/* a value's text as the plan's styles show it: the number (signed), the text without the unit, the whole name */
static void ce_ptext(const param_desc_t *d, int32_t v, uint32_t style, char *out, uint32_t n)
{
    char val[12];
    const char *unit;
    if ((style == ENG_C_BIG || style == ENG_C_RATIO || style == ENG_C_TEXT) && d->fmt == F_INT && d->names) {
        uint32_t k = 0;                           /* (one name a value: the whole name, params.c cuts it at 5) */
        while (d->names[k])
            k++;
        if (k == (uint32_t)(d->max - d->min + 1)) {
            cu_cpy(out, d->names[clamp(v, d->min, d->max) - d->min], n);
            return;
        }
    }
    if (style == ENG_C_NUM || style == ENG_C_BAR || style == ENG_C_DETUNE ||
        (style == ENG_C_BIG && d->fmt != F_INT && d->fmt != F_OFS)) {
        cu_int(out, v, d->min < 0, n);
        return;
    }
    param_format(d, v, val, &unit);
    cu_cpy(out, val, n);
    if (d->fmt == F_CUTOFF && unit[0] == 'k')
        cu_cat(out, "k", n);                      /* "2.1k" */
    if (style == ENG_C_TEXT)
        ce_title_case(out, d->fmt == F_ONOFF ? 2u : 3u);   /* ("ON" -> "On"; "LP" stays) */
}
/* cell c of row r (of ref rf) restyled by the plan; a label of the plan's */
static void ce_cell_plan(const track_t *t, const ce_view_t *vw, uint32_t r, uint32_t c, cr_cell_t *cl)
{
    const eng_screen_t *h = ce_plan(ce_deep(t), vw->hs);
    uint32_t st = ce_cstyle(t, vw, r, c);
    ce_ref_t rf = vw->ref[r][c];
    const param_desc_t *d;
    int32_t v = 0;
    if (!h || !(cl->flags & CR_CF_ON) || rf.k != CE_R_DEEP || !(d = ce_param(t, rf, &v)))
        return;
    if (r < 2u && h->label[r][c])
        cu_cpy(cl->label, h->label[r][c], sizeof cl->label);
    if (st == ENG_C_RULE)
        return;
    cl->flags = CR_CF_ON;
    cl->glyph = CR_G_NONE;
    cl->pct = ce_pct(d, v);
    ce_ptext(d, v, st, cl->value, sizeof cl->value);
    switch (st) {
    case ENG_C_BIG: cl->flags |= CR_CF_BIG; break;
    case ENG_C_RATIO: cl->glyph = CR_G_RATIO; break;
    case ENG_C_HARM: cl->glyph = CR_G_HARM; cl->flags |= CR_CF_PCT; break;
    case ENG_C_DETUNE: cl->glyph = CR_G_DETUNE; cl->flags |= CR_CF_PCT; break;
    case ENG_C_KNOB: cl->glyph = CR_G_KNOB; cl->flags |= CR_CF_PCT | (d->min < 0 ? CR_CF_BIPOLAR : 0u); break;
    case ENG_C_BAR: cl->glyph = CR_G_BAR; cl->flags |= CR_CF_PCT | (d->min < 0 ? CR_CF_BIP : 0u); break;
    case ENG_C_NUM:
    case ENG_C_VAL: cl->flags |= CR_CF_PCT | (d->min < 0 ? CR_CF_BIP : 0u); break;
    case ENG_C_WAVEPH: {                          /* the wave (CR_LW_* order) and the next column's phase: one cell */
        const param_desc_t *pd;
        int32_t ph = 0;
        char w[8], b[6];
        ce_ref_t r2 = c < 3u ? vw->ref[r][c + 1u] : ce_r(CE_R_NONE, 0, 0);
        pd = r2.k == CE_R_DEEP ? ce_param(t, r2, &ph) : 0;
        cl->glyph = CR_G_LFOWAVE;
        cl->flags |= CR_CF_SPAN2;
        cl->wave = (uint8_t)clamp(v - d->min, 0, CR_LW_N - 1);
        cl->pct = (uint8_t)(pd ? clamp(ph - pd->min, 0, 127) * 2 : 0);   /* (0..127 a cycle: Q8 of it) */
        ce_ptext(d, v, ENG_C_TEXT, w, sizeof w);
        cu_int(b, pd ? (ph - pd->min) * 360 / (pd->max - pd->min + 1) : 0, 0, sizeof b);   /* (degrees, as the mock-up) */
        cu_cpy(cl->value, w, sizeof cl->value);
        cu_cat(cl->value, str_len(w) + str_len(b) + 3u < sizeof cl->value ? " \267 " : "\267", sizeof cl->value);
        cu_cat(cl->value, b, sizeof cl->value);
        break;
    }
    default:
        break;
    }
}
/* a plan row's span cell (ENG_C_WAVEPH at column c) takes two columns: the cells after it move a place left (the
 * cell index is not the column: cr_screen.h CR_CF_SPAN2); the column it spans; 4 none */
static uint32_t ce_span_col(const track_t *t, const ce_view_t *vw, uint32_t r)
{
    uint32_t c;
    for (c = 0; c < 3u; c++)
        if (ce_cstyle(t, vw, r, c) == ENG_C_WAVEPH)
            return c;
    return 4u;
}

static void ce_screen(cr_screen_t *s, uint32_t now)
{
    track_t *t = ce_trk();
    uint32_t p = ce.part & 1u, r, c, g;
    ce_view_t vw;
    uint8_t band[20], mc[CE_NDST];
    ce_view(t, p, &vw);
    g = cx.grp[p] % CE_NSEC;
    s->kind = vw.kind;
    s->header = 0;
    s->ring_on = 0;                               /* (the editor has the whole screen) */
    cu_cpy(s->title, psnd[p].name, sizeof s->title);
    if (psnd[p].edited)
        cu_cat(s->title, "*", sizeof s->title);
    if (p)
        cu_cat(s->title, " \267 BASS", sizeof s->title);
    s->title_col = p ? CR_COL_ORANGE : CR_COL_NONE;
    cu_cpy(s->page, vw.right, sizeof s->page);
    s->fine = (uint8_t)(cx.shift || cu_shift());
    s->batt = 255;                                /* no battery in the editor (it is on the Options page) */
    s->n_rows = vw.n;
    s->active = vw.active;
    s->wide = vw.wide;
    s->tall = vw.tall;
    for (c = 0; c < 4u; c++)
        cu_cpy(s->head[c], vw.head[c], sizeof s->head[c]);
    ce_marks(t, mc);
    for (r = 0; r < vw.n; r++) {
        if (vw.kind == CR_K_STACK)
            ce_cat_num(s->rlabel[r], vw.row0 + r + 1u, sizeof s->rlabel[r]);
        for (c = 0; c < 4u; c++) {
            int32_t ds;
            ce_cell(t, &vw, vw.ref[r][c], &s->cell[r][c]);
            if (vw.hs)
                ce_cell_plan(t, &vw, r, c, &s->cell[r][c]);
            if ((s->cell[r][c].flags & CR_CF_ON) && (ds = ce_dst(t, vw.ref[r][c])) > 0 && ds < (int32_t)CE_NDST && mc[ds])
                s->cell[r][c].flags |= CR_CF_MARK(mc[ds]);
        }
        if (vw.hs && (c = ce_span_col(t, &vw, r)) < 3u) {   /* a span cell: the next column is in it */
            uint32_t j;
            for (j = c + 1u; j < 3u; j++)
                s->cell[r][j] = s->cell[r][j + 1u];
            for (j = 0; j < sizeof s->cell[r][3]; j++)
                ((uint8_t *)&s->cell[r][3])[j] = 0;
        }
    }
    if (vw.tall)                                  /* the mixer: "OSC n" over each level's bar */
        for (c = 0; c < 4u; c++)
            if (s->cell[0][c].flags & CR_CF_ON) {
                cu_cpy(s->cell[0][c].label, "OSC ", sizeof s->cell[0][c].label);
                ce_cat_num(s->cell[0][c].label, c + 1u, sizeof s->cell[0][c].label);
                s->cell[0][c].glyph = CR_G_BAR;
                s->cell[0][c].flags |= CR_CF_PCT;
            }
    if (cx.hot_r && now - cx.hot_t0 < CE_HOT_MS && cx.hot_r == vw.active + 1u) {
        s->hot_r = cx.hot_r;
        s->hot_c = cx.hot_c;
        if (vw.hs && (c = ce_span_col(t, &vw, (cx.hot_r - 1u) % CR_ED_ROWS)) < 3u && cx.hot_c > c)
            s->hot_c = (uint8_t)(cx.hot_c - 1u);  /* (a knob past a span cell: the cell index, not the column) */
        if (cx.hot_slot) {                        /* a quick mapping: "LFO1 +24" in the source's colour */
            const eng_deep_t *d = ce_deep(t);
            ce_slot_t sl;
            cr_cell_t *hc = &s->cell[(cx.hot_r - 1u) % CR_ED_ROWS][cx.hot_c & 3u];
            if (d && cx.hot_slot - 1u < d->npages && ce_slot(d, cx.hot_slot - 1u, &sl)) {
                const char *nm = ce_name_of(d, sl.pg, (uint32_t)sl.cs, cx.hot_src);
                ce_amt_text(hc->value, nm, d->get(t, sl.pg, (uint32_t)sl.ca), sizeof hc->value);
                s->hot_col = (uint8_t)ce_src_col(nm);
            }
        }
    }
    if (vw.wide && vw.hs && (c = ce_band_plan(t, &vw, band)) > 0) {   /* the plan's algo, ade2, filter (+ window) */
        uint32_t j;
        for (j = 0; j < c; j++)
            s->wv[j] = band[j];
        if (vw.wide == CR_W_ADE2)                 /* the segment the cell turned: A's a d end lev 1..4, B's 5..8 */
            s->wv[8] = (uint8_t)(s->hot_r ? ((s->hot_r - 1u) % 2u) * 4u + (cx.hot_c & 3u) + 1u : 0u);
    } else if (vw.wide) {                         /* the band: its values as they are (no tween: the knob leads) */
        ce_band(t, &vw, band);
        for (c = 0; c < (vw.wide == CR_W_CZ ? 18u : vw.wide == CR_W_DX ? 10u : vw.wide == CR_W_ENV ? 5u : 4u); c++)
            s->wv[c] = band[c];
        if (vw.wide == CR_W_CZ && s->hot_r)       /* the step the cell turned (R k, L k; SUS / END: their step) */
            s->wv[18] = (uint8_t)ce_cz_step(t, vw.ref[(s->hot_r - 1u) % CR_ED_ROWS][s->hot_c & 3u]);
        if (vw.wide == CR_W_ENV && s->hot_r)
            s->wv[5] = (uint8_t)ce_seg(t, vw.ref[(s->hot_r - 1u) % CR_ED_ROWS][s->hot_c & 3u]);
        if (vw.wide == CR_W_DX && s->hot_r)       /* the segment the cell turned ends (R k, L k: k) */
            s->wv[8] = (uint8_t)ce_seg(t, vw.ref[(s->hot_r - 1u) % CR_ED_ROWS][s->hot_c & 3u]);
    }
}
