/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICES (EDIT family, a SLICE track only; after EDIT 2): the slices of the selected part's sample, set by hand.
 * Based on hugelton/Felucca#27 by andreahaku (SLICE EDIT), on the current UI: a page of the EDIT family with the
 * action-page OCT+ / OCT- of SAVE > TOOLS.
 * The markers: the slice starts the track plays (its DIV: 4..32 equal, AUTO, MAN) and the END of the last slice.
 *   KNOB 1  SLICE  the marker (1..n, then END); a key pressed picks the slice it plays (its keys are lit)
 *   KNOB 2  POS    moves it: one column of the view per detent; the view spans its neighbours, so a marker between
 *                  close ones moves in fine steps. Slice 1's start trims the head, END the tail
 *   KNOB 3  SPLIT  picked (right): OCT+ splits the slice at its middle (up to 32 slices)
 *   KNOB 4  JOIN   picked (right): OCT+ joins the slice to the one before (its start goes)
 *   OCT-    drops the pick, or (none) goes HOME
 * Edits are for a user slot (USR1..3: SRC); BREAK and an empty slot show their slices only. The first edit takes
 * the slices shown into the slot's MAN table (eng_slice.c) and sets DIV to MAN; later edits change that table (the
 * audio side switches tables between notes). Leaving the page saves them with the sample (slice_store.c, once
 * stopped). Included by ui.c (before the action pages); the cards: ui_draw.c, the waveform: ui_graph.c. */
#if FELUCCA_SLICE
#define SP_COLS 216u                                 /* the waveform's columns (the panel's x 12..227) */
static struct {
    uint8_t sel;                                     /* the marker: slice 0..n - 1, n = END */
    uint8_t env_src;                                 /* what lo / hi were decoded for */
    uint32_t env_a, env_b, env_len, env_gen;         /* (env_gen: smp_user_gen, a re-upload of the same length) */
    int8_t lo[SP_COLS], hi[SP_COLS];                 /* per column: smallest / largest sample >> 9 */
} sp;

static int slice_page_ok(void) { return ENGINES[TSEL->eng_req % NENGINES] == &ENG_SLICE; }
static int slice_page_on(void) { return !ui.home && cur_page()->graph == GR_SLICES && slice_page_ok(); }

/* the source the selected part plays (an empty slot: BREAK, as slice_note_on), its DIV; 0 = no material */
static const slc_src_t *slice_src(uint32_t *src, uint32_t *div)
{
    const int16_t *p = TSEL->p;
    uint32_t k = (uint32_t)p[P_E0] & 3u;
    const slc_src_t *s = slc_get(k);
    if (!s)
        s = slc_get(k = 0);
    *src = k;
    *div = (uint32_t)clamp(p[P_E1], 0, SLC_DIV_MAN);
    return s;
}

/* the markers shown: n slices, marker i's position (i = n: the end) */
static uint32_t slice_count(void)
{
    uint32_t src, div;
    const slc_src_t *s = slice_src(&src, &div);
    return s ? slc_count(s, div) : 0u;
}
static uint32_t slice_mark(uint32_t i)
{
    uint32_t src, div, n, a, b, st;
    const slc_src_t *s = slice_src(&src, &div);
    if (!s || !(n = slc_count(s, div)))
        return 0;
    slc_bounds(s, div, i < n ? i : n - 1u, &a, &b, &st);
    return i < n ? a : b;
}
static uint32_t slice_sel(void)
{
    uint32_t n = slice_count();
    return sp.sel < n ? sp.sel : n;
}

/* the view: from the marker before the selected one to the marker after it (the head / the material's end), so the
 * marker's whole range of motion is on the screen; *len the material's samples */
static void slice_view(uint32_t *a, uint32_t *b, uint32_t *len)
{
    uint32_t src, div, n = slice_count(), j = slice_sel();
    const slc_src_t *s = slice_src(&src, &div);
    *len = s ? s->len : 0u;
    *a = j ? slice_mark(j - 1u) : 0u;
    *b = j < n ? slice_mark(j + 1u) : *len;
    if (j + 1u == n && *b < *len)                    /* the last slice: up to the material's end (its END moves) */
        *b = *len;
    if (*b <= *a)
        *b = *a + 1u;
}

/* the edit table of the selected part's slot, the first time from the slices shown (DIV becomes MAN); 0 = BREAK
 * or an empty slot: nothing to edit (says so) */
static slc_man_t *slice_edit_begin(uint32_t *k)
{
    uint32_t src, div, n, i, a, b, st;
    const slc_src_t *s = slice_src(&src, &div);
    slc_man_t *m;
    if (!s || !src) {
        static const char *const EMPTY[3] = {"USR1 EMPTY", "USR2 EMPTY", "USR3 EMPTY"};
        uint32_t u = (uint32_t)TSEL->p[P_E0] & 3u;   /* (an empty USR slot plays BREAK: src 0) */
        ui_message(u ? EMPTY[u - 1u] : "SRC USR1-3 TO EDIT");
        return 0;
    }
    *k = src - 1u;
    if (div != SLC_DIV_MAN && slc_man_of(s)) {       /* the slot has slices set by hand: those, not the grid; this */
        TSEL->p[P_E1] = SLC_DIV_MAN;                 /* touch only shows them (its marker was another one) */
        ui_message("DIV MAN");
        return 0;
    }
    m = slc_man_begin(*k);
    if (m && div != SLC_DIV_MAN) {                   /* the slices shown (4..32 equal, AUTO) become the MAN table */
        n = slc_count(s, div);
        m->n = 0;
        m->end = 0;
        for (i = 0; i < n; i++) {
            slc_bounds(s, div, i, &a, &b, &st);
            if (a < s->len && s->len - a >= SLC_MIN && (!m->n || a - m->pos[m->n - 1u] >= SLC_MIN)) {
                m->pos[m->n] = a;
                m->st[m->n++] = st;
            }
        }
    }
    if (!m || !m->n) {
        ui_message("SAMPLE TOO SHORT");
        return 0;
    }
    return m;
}
static void slice_edit_end(uint32_t k)
{
    slc_man_commit(k);
    if (TSEL->p[P_E1] != SLC_DIV_MAN) {
        TSEL->p[P_E1] = SLC_DIV_MAN;                 /* (the editor sees it as any change of the value) */
        ui_message("DIV MAN");
    }
    slc_man_save |= (uint8_t)(1u << k);              /* to flash when the page is left (slice_store.c) */
}

/* KNOB 1: the marker; KNOB 2: move it */
static void slice_knob(uint32_t slot, int32_t steps)
{
    uint32_t n = slice_count(), j = slice_sel(), k, a, b, len;
    slc_man_t *m;
    if (!n)
        return;
    if (slot == 0u) {
        sp.sel = (uint8_t)clamp((int32_t)j + steps, 0, (int32_t)n);
        return;
    }
    slice_view(&a, &b, &len);
    if (!(m = slice_edit_begin(&k)))
        return;
    j = j < n ? (j < m->n ? j : m->n - 1u) : m->n;   /* (a table cut short by SLC_MIN: the nearest) */
    {
        int32_t d = steps * (int32_t)((b - a) / SP_COLS ? (b - a) / SP_COLS : 1u);
        const slc_src_t *s = slc_get(k + 1u);
        if (j < m->n)
            slc_man_move(s, m, j, d);
        else
            slc_man_move_end(s, m, d);
    }
    slice_edit_end(k);
    sp.sel = (uint8_t)j;
}

/* OCT+ would do it: SPLIT (c 2) a slice of a user slot below 32 slices and long enough, JOIN (c 3) a slice but the
 * first; on the END marker neither */
static int slice_act_ready(uint32_t c)
{
    uint32_t src, div, n = slice_count(), j = slice_sel();
    const slc_src_t *s = slice_src(&src, &div);
    if (!s || !src || j >= n)
        return 0;
    if (c == 3u)
        return j > 0u;
    return n < SLC_AUTO && slice_mark(j + 1u) - slice_mark(j) >= 2u * SLC_MIN;
}
static void slice_act(uint32_t c)
{
    uint32_t n = slice_count(), j = slice_sel(), k;
    slc_man_t *m;
    if (j >= n) {
        ui_message("PICK A SLICE");
        return;
    }
    if (!(m = slice_edit_begin(&k)))
        return;
    j = j < m->n ? j : m->n - 1u;
    if (c == 3u) {
        if (!j) {
            ui_message("FIRST SLICE");
            return;
        }
        j = slc_man_join(m, j);
    } else {
        uint32_t before = m->n;
        j = slc_man_split(slc_get(k + 1u), m, j);
        if (m->n == before) {
            ui_message(before >= SLC_AUTO ? "32 SLICES AT MOST" : "SLICE TOO SHORT");
            return;
        }
    }
    slice_edit_end(k);
    sp.sel = (uint8_t)j;
}

/* a key pressed on the page: its slice becomes the marker (the key plays it as always) */
static void slice_keys_pick(uint32_t pressed)
{
    uint32_t n = slice_count(), k;
    for (k = 0; n && k < 27u; k++)
        if ((pressed >> k) & 1u) {
            uint32_t note = kb_map(TSEL, k);
            if (note != KB_SILENT)
                sp.sel = (uint8_t)slc_note_slice(TSEL->p, note, n);
        }
}

/* ui_leds: the keys that play the selected slice (bit k = key k), as kb_map and slice_note_on map them */
static uint32_t slice_leds(void)
{
    uint32_t n = slice_count(), j = slice_sel(), k, mask = 0;
    for (k = 0; j < n && k < 27u; k++) {
        uint32_t note = kb_map(TSEL, k);
        if (note != KB_SILENT && slc_note_slice(TSEL->p, note, n) == j)
            mask |= 1u << k;
    }
    return mask;
}

/* the waveform of [a, b): per column the smallest and largest sample (main loop, when the view moves) */
static void slice_env(uint32_t a, uint32_t b)
{
    uint32_t src, div, c, e, len;
    const slc_src_t *s = slice_src(&src, &div);
    slc_dec_t d;
    len = s ? s->len : 0u;
    if (!s || (sp.env_a == a && sp.env_b == b && sp.env_len == len && sp.env_src == src &&
               sp.env_gen == smp_user_gen))
        return;
    slc_dec_at(&d, a, slc_state_at(s, a));
    for (c = 0; c < SP_COLS; c++) {
        int32_t lo = 32767, hi = -32768;
        e = a + (uint32_t)((uint32_t)(c + 1u) * (b - a) / SP_COLS);
        do {                                         /* at least one sample per column */
            int32_t x = d.pos < len ? slc_dec_next(s, &d) : 0;
            lo = x < lo ? x : lo;
            hi = x > hi ? x : hi;
        } while (d.pos < e);
        sp.lo[c] = (int8_t)(lo >> 9);
        sp.hi[c] = (int8_t)(hi >> 9);
    }
    sp.env_a = a;
    sp.env_b = b;
    sp.env_len = len;
    sp.env_gen = smp_user_gen;
    sp.env_src = (uint8_t)src;
}

/* "1.234": samples of s as seconds */
static void slice_time(char *b, uint32_t n)
{
    uint32_t src, div, hz;
    const slc_src_t *s = slice_src(&src, &div);
    hz = s ? (s->rate >> 4) * 44100u >> 12 : 0u;    /* (as slc_scan) */
    fmt_fix(b, (int32_t)((uint32_t)n * 1000u / (hz ? hz : 1u)), 3);
    if (b[0] == '.') {                               /* fmt_fix writes ".255": "0.255" */
        uint32_t i = str_len(b) + 1u;
        for (; i; i--)
            b[i] = b[i - 1u];
        b[0] = '0';
    }
}

/* graph_signature: what the page shows */
static uint32_t slice_sig(void)
{
    uint32_t n = slice_count(), i, h = n * 131u + slice_sel() * 7919u + smp_user_gen * 104729u;
    for (i = 0; i <= n; i++)
        h = (h ^ slice_mark(i)) * 16777619u;
    return h;
}
#else
static int slice_page_ok(void) { return 0; }
static int slice_page_on(void) { return 0; }
#endif
