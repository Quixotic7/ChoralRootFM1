/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the screen renderer. cr_draw(s, anim_ms) draws a cr_screen_t (cr_screen.h) on the 240 x 240 LCD
 * through gfx.c's canvas, in six strips of 240 x 40 (gfx.c CV_MAX holds 124 rows), each drawn with cv_oy = -its
 * top row so the layout is written in screen coordinates and clipped by the canvas.
 * The layout is the designer's (../ChoralRootFM1Designer/index.html renderScreen, cards and knob cards off): the top
 * line rows 0..27, the panel from row 28 to 198 (a footer line at 226) or to 240, the ring round the edge, the
 * message box over the panel. Type: the bold sizes (600..800 in the mock-ups) are Inter Tight 700 resampled from
 * the CRX (104 px, the chord charset) and CRB (40 px) faces (cr_gfx.c); the regular ones (500) are Felucca's own
 * AF_M (15 px) and AF_S (12 px), drawn natively at 12..15 px and resampled below 12. See CR_SCREENS.md.
 * Cache: a signature of the struct, the animation's frame and the palette: unchanged, nothing is drawn; else every
 * strip is drawn and only the strips whose pixels changed are blitted (cr_draw_invalidate: all of them, once).
 * Animations are pure functions of (s, anim_ms): anim_ms is the time since the change that started the ones in
 * s->anim (the firmware's tween clock); cr_anim_busy says whether more frames are still to come.
 * Included after gfx.c and cr_gfx.c (felucca.c's single compilation unit). */
#include "cr_screen.h"

/* ChoralRoot's own colours (PLAN.md section 5), not per palette */
#define CR_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static const uint16_t CR_NAMED[9] = {
    0, CR_RGB(246, 243, 234), CR_RGB(224, 58, 47), CR_RGB(43, 80, 200), CR_RGB(242, 183, 5),
    CR_RGB(240, 122, 26), CR_RGB(47, 179, 122), CR_RGB(110, 110, 118), CR_RGB(10, 10, 12)};
#define CR_RED CR_NAMED[CR_COL_RED]
#define CR_WHITE CR_NAMED[CR_COL_WHITE]
#define CR_YELLOW CR_NAMED[CR_COL_YELLOW]

/* a colour of the struct; NONE: def */
static uint16_t cr_rgb(uint32_t c, uint16_t def)
{
    if (c > CR_COL_NONE && c <= CR_COL_BLACK) return CR_NAMED[c];
    switch (c) {
    case CR_COL_BG: return T_BG;
    case CR_COL_SURF: return T_SURF;
    case CR_COL_TEXT: return T_TEXT;
    case CR_COL_THEME: return T_THEME;
    case CR_COL_ACCENT: return T_ACCENT;
    case CR_COL_MID: return T_MID;
    case CR_COL_DIM: return T_DIM;
    case CR_COL_LINE: return T_LINE;
    case CR_COL_REC: return T_REC;
    default: return def;
    }
}

/* ------------------------------------------------------------ type --- */
/* a size and weight of the mock-ups -> a face and its scale (Q12); native: drawn by gfx.c cv_text */
typedef struct { const aafont_t *f; int32_t sc; int native; } cr_face_t;
static cr_face_t cr_face(const char *s, uint32_t px, int bold)
{
    cr_face_t r;
    if (bold) {
        r.f = px > 40u && cr_covers(&AF_CRX, s) ? &AF_CRX : &AF_CRB;
        r.native = 0;
    } else if (px >= 13u) {
        r.f = &AF_M;                     /* 13..15 px / 500: Felucca's M (15 px / 500) as it is */
        r.native = 1;
        px = 15u;
    } else {
        r.f = &AF_S;                     /* 12 px: S (12 px / 400); below: S resampled */
        r.native = px == 12u;
    }
    r.sc = (int32_t)((px << 12) / cr_face_px(r.f));
    return r;
}
/* the advance of s (Q8 px) */
static int32_t cr_tw(const char *s, uint32_t px, int bold)
{
    cr_face_t f = cr_face(s, px, bold);
    return f.native ? text_w(f.f, s) << 8 : cr_adv8(f.f, s, f.sc);
}

enum { CR_L, CR_C, CR_R };
/* s on baseline y (Q8) at x (Q8; align: left, centre or right of it), squeezed sq (Q12, 4096 none), colour fg; under:
 * what lies behind it (the native faces blend against it); returns the left x (Q8) */
static int32_t cr_text(int32_t x, int32_t y, const char *s, uint32_t px, int bold, int align, int32_t sq, uint16_t fg,
                       uint16_t under, uint32_t flags)
{
    cr_face_t f = cr_face(s, px, bold);
    int32_t w;
    if (!s[0]) return x;
    if (f.native && sq == 4096) {
        w = text_w(f.f, s) << 8;
        x -= align == CR_C ? w / 2 : align == CR_R ? w : 0;
        cv_text_flags((x + 128) >> 8, ((y + 128) >> 8) - f.f->asc, f.f, s, fg, under, flags);
        return x;
    }
    w = (cr_adv8(f.f, s, f.sc) * sq) >> 12;
    x -= align == CR_C ? w / 2 : align == CR_R ? w : 0;
    cr_text_sc(x, y, f.f, s, (f.sc * sq) >> 12, f.sc, fg, flags);
    return x;
}
/* s into d, at most maxw (Q8) wide at px: the end ellipsised. 1 = cut */
static int cr_fit(char *d, uint32_t n, const char *s, uint32_t px, int bold, int32_t maxw)
{
    cr_face_t f = cr_face(s, px, bold);
    uint32_t k = 0;
    if (f.native) return text_fit(d, n, s, f.f, maxw >> 8);
    while (s[k] && k + 1u < n) { d[k] = s[k]; k++; }
    d[k] = 0;
    if (!s[k] && cr_adv8(f.f, d, f.sc) <= maxw) return 0;
    if (k + 2u > n) k = n - 2u;
    for (;;) {
        while (k && d[k - 1u] == ' ') k--;
        d[k] = ELLIPSIS;
        d[k + 1u] = 0;
        if (!k || cr_adv8(f.f, d, f.sc) <= maxw) return 1;
        k--;
    }
}
/* cr_text of s cut to maxw (Q8) */
static void cr_text_fit(int32_t x, int32_t y, const char *s, uint32_t px, int bold, int align, uint16_t fg,
                        uint16_t under, int32_t maxw)
{
    char b[48];
    int cut = cr_fit(b, sizeof b, s, px, bold, maxw);
    cr_text(x, y, b, px, bold, align, 4096, fg, under, cut ? 9u : 0u);
}

#define P8(v) ((int32_t)(v) * 256)       /* whole px -> Q8 */

/* ------------------------------------------------------- animation --- */
#define CR_SQUEEZE_MS 120u               /* the old name squeezes to a column (half), the new one stretches out */
#define CR_THIN 330                      /* .. the column: 8 % (Q12; cr_gfx.c resamples down to 1/16) */
#define CR_SLIDE_MS 160u                 /* a picker's slide */
#define CR_FILL_MS 30u                   /* a meter: a stripe every 30 ms */
#define CR_SWEEP_MS 240u                 /* the idle stripes swept off, each band 40 ms after the one above */
typedef struct {
    int32_t sq;                          /* chord: the squeeze of the name drawn (Q12) */
    int32_t slide;                       /* picker: how far the selection still has to slide (Q12 of its pitch) */
    int32_t shift;                       /* stripes: Q8 px */
    int32_t sweep[4];                    /* stripes: Q8 px each band is swept right */
    uint8_t from;                        /* chord: 1 = `from` is drawn (the first half of the squeeze) */
    uint8_t filled;                      /* meter: stripes filled */
    uint8_t busy;                        /* more frames to come */
} cr_frame_t;

/* 1 - (1 - x)^2 over x = t / d, Q12 */
static int32_t cr_ease_out(uint32_t t, uint32_t d)
{
    int32_t u;
    if (t >= d) return 4096;
    u = 4096 - (int32_t)((t << 12) / d);
    return 4096 - ((u * u) >> 12);
}
static uint32_t cr_segs(const cr_screen_t *s) { return s->segments ? s->segments : 12u; }
static uint32_t cr_filled(const cr_screen_t *s, uint32_t pct)
{
    return (pct * cr_segs(s) + 128u) >> 8;
}

static void cr_frame(const cr_screen_t *s, uint32_t t, cr_frame_t *fr)
{
    uint32_t i, k;
    for (k = 0; k < sizeof *fr; k++)
        ((uint8_t *)fr)[k] = 0;
    fr->sq = 4096;
    fr->filled = (uint8_t)cr_filled(s, s->pct);
    fr->shift = (int32_t)(s->phase & 255u) * 40;
    if (s->kind == CR_K_CHORD && (s->anim & CR_A_SQUEEZE) && t < CR_SQUEEZE_MS) {
        fr->busy = 1;
        if (s->from.root[0] && t < CR_SQUEEZE_MS / 2u) {          /* the old name thins (ease-in) */
            int32_t e = (int32_t)((t << 12) / (CR_SQUEEZE_MS / 2u));
            fr->from = 1;
            fr->sq = 4096 - (((4096 - CR_THIN) * ((e * e) >> 12)) >> 12);
        } else if (s->from.root[0]) {                              /* the new one stretches out (ease-out) */
            fr->sq = CR_THIN + (((4096 - CR_THIN) * cr_ease_out(t - CR_SQUEEZE_MS / 2u, CR_SQUEEZE_MS / 2u)) >> 12);
        } else {
            fr->sq = CR_THIN + (((4096 - CR_THIN) * cr_ease_out(t, CR_SQUEEZE_MS)) >> 12);
        }
    }
    if (s->kind == CR_K_PICKER && (s->anim & CR_A_SLIDE) && s->slide && t < CR_SLIDE_MS) {
        fr->busy = 1;
        fr->slide = 4096 - cr_ease_out(t, CR_SLIDE_MS);
    }
    if (s->kind == CR_K_METER && (s->anim & CR_A_FILL)) {
        uint32_t a = cr_filled(s, s->pct_from), b = fr->filled, n = a < b ? b - a : a - b, done = t / CR_FILL_MS;
        if (done < n) {
            fr->busy = 1;
            fr->filled = (uint8_t)(a < b ? a + done : a - done);
        }
    }
    if (s->kind == CR_K_STRIPES) {
        if ((s->anim & CR_A_STRIPES) && s->period_ms) {
            fr->busy = 1;                                          /* (runs until the screen changes) */
            fr->shift = (int32_t)(((s->phase & 255u) + (t % s->period_ms) * 256u / s->period_ms) & 255u) * 40;
        }
        if (s->anim & CR_A_SWEEP)
            for (i = 0; i < 4u; i++) {
                uint32_t d = i * 40u;
                fr->sweep[i] = t <= d ? 0 : (cr_ease_out(t - d, CR_SWEEP_MS) * 300) >> 4;
                if (t < d + CR_SWEEP_MS) fr->busy = 1;
            }
    }
}
/* 1 while an animation of s still moves at anim_ms (the firmware keeps calling cr_draw with a running clock) */
static int cr_anim_busy(const cr_screen_t *s, uint32_t anim_ms)
{
    cr_frame_t fr;
    cr_frame(s, anim_ms, &fr);
    return fr.busy;
}

/* ----------------------------------------------------------- layout --- */
#define CR_PY0 28                        /* the panel's top row (no knob cards) */
static int32_t cr_ph(const cr_screen_t *s) { return (s->footer[0] ? 198 : 240) - CR_PY0; }
static int cr_key_black(uint32_t k) { return (0x54A >> ((k + 5u) % 12u)) & 1; }

static void cr_header(const cr_screen_t *s)
{
    int bare = s->icon == CR_ICON_NONE, px = bare ? 15 : 12;
    int32_t x = 0, rx = 236, rightw = 0, base = bare ? P8(18) : P8(16) + 128;
    if (!s->header) return;
    if (s->icon == CR_ICON_PLAY) {       /* a triangle (8, 6) (19, 12) (8, 18), a row at a time */
        int32_t j;
        for (j = 6; j < 18; j++) {
            int32_t d = j * 16 + 8 - 12 * 16;                          /* the row's centre from the tip's row, Q4 */
            cr_frect(8 * 16, j * 16, 11 * (6 * 16 - (d < 0 ? -d : d)) / 6, 16, T_THEME);
        }
    } else if (s->icon == CR_ICON_REC) {
        cr_disc(14 * 16, 12 * 16, 88, T_REC);
    } else if (s->icon == CR_ICON_LOOP) {                                /* 0.3 rad .. 1.75 pi, 2 px */
        cr_arc(14 * 16, 12 * 16, 5 * 16, 32, 3129u, 57344u - 3129u, 0, 0, 0, T_THEME);
    }
    if (!bare) x = 26;
    if (s->batt != 255u) rightw += 26;
    if (s->right[0]) rightw += (cr_tw(s->right, 12, 1) >> 8) + 8;
    if (s->mid[0])
        cr_text_fit(P8(x + 8), base, s->mid, (uint32_t)px, 1, CR_L, cr_rgb(s->mid_col, CR_WHITE), T_BG,
                    P8(236 - rightw - x - 10));
    if (s->batt != 255u) {
        uint32_t lvl = s->batt > 4u ? 4u : s->batt;
        rx -= 20;
        cr_fill(rx + 1, 7, 14, 1, T_MID);                            /* the case: 16 x 10, 1 px, round corners */
        cr_fill(rx + 1, 16, 14, 1, T_MID);
        cr_fill(rx, 8, 1, 8, T_MID);
        cr_fill(rx + 15, 8, 1, 8, T_MID);
        cr_frect((rx + 16) * 16 + 8, 9 * 16 + 8, 32, 80, T_MID);      /* the nub */
        if (lvl) cr_frect((rx + 2) * 16, 9 * 16, (int32_t)(12u * 16u * lvl / 4u), 6 * 16, lvl <= 1u ? CR_YELLOW : CR_RED);
        rx -= 6;
    }
    if (s->right[0])
        cr_text(P8(rx), base, s->right, (uint32_t)px, 1, CR_R, 4096, cr_rgb(s->right_col, bare ? CR_WHITE : T_MID),
                T_BG, 0);
}

/* the chord name (Orchid Standard Framework): the root, the quality on its baseline at 0.58, the extensions as a
 * superscript at 0.38; centred at cx on baseline cy (Q8), squeezed to maxw (px) and by sq (Q12) about cx */
static void cr_name(const cr_name_t *n, int32_t cx, int32_t cy, uint32_t size, int32_t maxw, int32_t sq)
{
    uint32_t qs = (size * 58u + 50u) / 100u, ss = (size * 38u + 50u) / 100u;
    int32_t wr = cr_tw(n->root, size, 1), wq = n->quality[0] ? cr_tw(n->quality, qs, 1) : 0, sx, x;
    int32_t w = wr + (n->quality[0] ? wq + P8(2) : 0) + (n->sup[0] ? cr_tw(n->sup, ss, 1) + P8(3) : 0);
    uint16_t cr = cr_rgb(n->col_root, CR_WHITE);
    if (w <= 0) return;
    sx = P8(maxw) < w ? ((P8(maxw) << 12) / w) : 4096;
    sx = (sx * sq) >> 12;
    if (sx < 300) sx = 300;                                       /* (cr_gfx.c CR_KMAX) */
    x = cx - w / 2;
#define CR_SQX(v) (cx + (((v) - cx) * sx >> 12))
    cr_text(CR_SQX(x), cy, n->root, size, 1, CR_L, sx, cr, T_BG, 0);
    x += wr + P8(2);
    if (n->quality[0]) {
        cr_text(CR_SQX(x), cy, n->quality, qs, 1, CR_L, sx, cr_rgb(n->col_quality, cr), T_BG, 0);
        x += wq + P8(2);
    }
    if (n->sup[0])
        cr_text(CR_SQX(x + P8(1)), cy - (int32_t)(size * 133u), n->sup, ss, 1, CR_L, sx, cr_rgb(n->col_sup, CR_RED), T_BG, 0);
#undef CR_SQX
}

/* the 27-key strip: 16 white keys across 224 px from x 8, black keys 8 px over the gaps; y, h px */
static void cr_keyboard(const cr_screen_t *s, int32_t y, int32_t h)
{
    uint32_t k, place = 0;
    int32_t bh16 = h * 16 * 6 / 10;
    for (k = 0; k < CR_KEYS; k++) {
        int lit = (s->lit >> k) & 1u;
        int32_t x;
        if (cr_key_black(k)) continue;
        x = 8 + (int32_t)place * 14;
        cr_frect(x * 16 + 8, y * 16, 13 * 16, h * 16, lit ? cr_rgb(s->lit_col[k], CR_RED) : T_KEY);
        if (s->key_label[k][0])
            cr_text(P8(x) + 7 * 256, P8(y + h - 3), s->key_label[k], 7, 1, CR_C, 4096, T_BG, T_KEY, 0);
        place++;
    }
    for (k = 0, place = 0; k < CR_KEYS; k++) {
        int lit = (s->lit >> k) & 1u;
        int32_t x;
        if (!cr_key_black(k)) { place++; continue; }
        x = 8 + (int32_t)place * 14 - 4;
        cr_frect(x * 16 - 8, y * 16 - 8, 9 * 16, bh16 + 16, T_LINE);
        cr_frect(x * 16 + 8, y * 16 + 8, 7 * 16, bh16 - 16, lit ? cr_rgb(s->lit_col[k], CR_YELLOW) : T_BG);
        if (s->key_label[k][0])
            cr_text(P8(x) + 4 * 256, (y * 16 + bh16) * 16 - P8(2), s->key_label[k], 6, 1, CR_C, 4096, lit ? T_BG : T_MID,
                    T_BG, 0);
    }
}

static void cr_p_chord(const cr_screen_t *s, const cr_frame_t *fr, int32_t ph)
{
    int ringed = s->ring_on;
    uint32_t size = s->size ? s->size : 104u;
    int32_t extra = (s->line[0] ? 256 : 0) + (s->n_notes ? 410 : 0) + (ringed && s->n_notes ? 512 : 0);  /* Q8 */
    int32_t cy = P8(CR_PY0) + P8(ph) / 2 + (int32_t)(size * 87u) - extra * 9, ly = P8(CR_PY0 + ph - 10 - (ringed ? 22 : 0));
    int32_t sq = (s->squeeze ? (int32_t)s->squeeze << 4 : 4096) * fr->sq >> 12;
    uint16_t under = s->block ? cr_rgb(s->block, CR_RED) : T_BG;
    if (s->block) cr_fill(0, CR_PY0, 240, ph, under);
    if (!s->name.root[0] && !(fr->from && s->from.root[0])) return;
    cr_name(fr->from ? &s->from : &s->name, P8(120), cy, size, 224, sq);
    if (s->line[0]) {
        cr_text(P8(120), ly, s->line, 14, 0, CR_C, 4096, cr_rgb(s->line_col, T_MID), under, 0);
        ly -= P8(18);
    }
    if (s->n_notes) {                    /* the notes line: plain bold text, extensions marked by a block */
        uint32_t i, px = s->n_notes > 5u ? 13u : 16u;
        int32_t w[CR_NOTES_MAX], total = 0, x, y = ly - P8(12);
        for (i = 0; i < s->n_notes; i++) {
            w[i] = cr_tw(s->note[i].t, px, 1);
            total += w[i] + (i ? P8(14) : 0);
        }
        x = P8(120) - total / 2;
        for (i = 0; i < s->n_notes; i++) {
            uint16_t c = cr_rgb(s->note[i].col, CR_WHITE);
            cr_text(x, y + P8(5), s->note[i].t, px, 1, CR_L, 4096, c, under, 0);
            if (s->note[i].mark) cr_frect(x >> 4, (y >> 4) + 160, w[i] >> 4, 48, c);
            x += w[i] + P8(14);
        }
    }
}

static const char *cr_item(const cr_screen_t *s, int32_t i)
{
    int32_t k = i - (int32_t)s->item0;
    return i >= 0 && i < (int32_t)s->n_items && k >= 0 && k < (int32_t)CR_PICK_MAX ? s->item[k] : "";
}

static void cr_p_picker(const cr_screen_t *s, const cr_frame_t *fr, int32_t ph)
{
    int ringed = s->ring_on, horiz = s->orient == 1;
    int32_t n = s->n_items, sel = s->sel < n ? s->sel : n - 1, size = s->size ? s->size : horiz ? 36 : 40;
    int32_t maxw = horiz ? (ringed ? 120 : 150) : (ringed ? 170 : 224);
    int32_t bigh = size * 282, valh = s->value[0] ? P8(26) : 0, nbh = horiz ? 0 : P8(22);   /* Q8 */
    int32_t labelh = s->label[0] ? P8(18) : 0, marksh = ringed ? 0 : P8(10);
    int32_t stack = nbh + bigh + valh + nbh, top = P8(CR_PY0) + (P8(ph) - labelh - marksh - stack) / 2 - P8(2);
    int32_t base = top + nbh + bigh * 4 / 5;
    uint16_t col = cr_rgb(s->col, CR_WHITE);
    const char *big = cr_item(s, sel);
    if (s->title[0]) cr_text(P8(10), P8(CR_PY0 + 16), s->title, 14, 1, CR_L, 4096, cr_rgb(s->title_col, T_MID), T_BG, 0);
    if (horiz) {
        int32_t lx = ringed ? 36 : 6, rx = ringed ? 204 : 234, bw = cr_tw(big, (uint32_t)size, 1), room;
        if (bw > P8(maxw)) bw = P8(maxw);
        room = (P8(rx - lx) - bw) / 2 - P8(10);
        if (sel > 0 && room > P8(24))
            cr_text_fit(P8(lx), base - P8(4), cr_item(s, sel - 1), 13, 0, CR_L, T_DIM, T_BG, room);
        if (sel < n - 1 && room > P8(24)) {
            char b[48];
            int cut = cr_fit(b, sizeof b, cr_item(s, sel + 1), 13, 0, room);
            cr_text(P8(rx), base - P8(4), b, 13, 0, CR_R, 4096, T_DIM, T_BG, cut ? 9u : 0u);
        }
    } else {
        if (sel > 0) cr_text_fit(P8(120), top + P8(14), cr_item(s, sel - 1), 15, 0, CR_C, T_DIM, T_BG, P8(maxw));
        if (sel < n - 1)
            cr_text_fit(P8(120), top + nbh + bigh + valh + P8(16), cr_item(s, sel + 1), 15, 0, CR_C, T_DIM, T_BG, P8(maxw));
    }
    {   /* the item, huge, squeezed to fit; sliding: the old one out, the new one in, inside the item's box */
        int32_t w = cr_tw(big, (uint32_t)size, 1), sx = w > P8(maxw) ? ((P8(maxw) << 12) / w) : 4096;
        if (fr->slide && s->slide) {
            const char *old = cr_item(s, sel - s->slide);
            int32_t wo = cr_tw(old, (uint32_t)size, 1), so = wo > P8(maxw) ? ((P8(maxw) << 12) / wo) : 4096;
            int32_t pitch = horiz ? P8(maxw / 2 + 60) : bigh, d = (pitch * fr->slide) >> 12;
            int32_t dir = s->slide > 0 ? 1 : -1;
            if (horiz) {
                cr_clip_set(120 - maxw / 2 - 4, 0, 120 + maxw / 2 + 4, 240);
                cr_text(P8(120) + dir * d, base, big, (uint32_t)size, 1, CR_C, sx, col, T_BG, 32u);
                cr_text(P8(120) + dir * (d - pitch), base, old, (uint32_t)size, 1, CR_C, so, col, T_BG, 32u);
            } else {
                cr_clip_set(0, (top + nbh) >> 8, 240, ((top + nbh + bigh + 255) >> 8) + 4);   /* (+4: descenders) */
                cr_text(P8(120), base + dir * d, big, (uint32_t)size, 1, CR_C, sx, col, T_BG, 32u);
                cr_text(P8(120), base + dir * (d - pitch), old, (uint32_t)size, 1, CR_C, so, col, T_BG, 32u);
            }
            cr_clip_all();
        } else {
            cr_text(P8(120), base, big, (uint32_t)size, 1, CR_C, sx, col, T_BG, 0);
        }
    }
    if (s->value[0]) cr_text(P8(120), top + nbh + bigh + P8(20), s->value, 20, 1, CR_C, 4096, col, T_BG, 0);
    if (s->label[0])
        cr_text(P8(120), P8(CR_PY0 + ph) - marksh - P8(ringed ? 34 : 8), s->label, 13, 0, CR_C, 4096, T_MID, T_BG, 0);
    if (n > 1 && !ringed) {              /* square position marks */
        int32_t mw = 200 / n - 3, gap = 4, x0, i;
        if (mw > 8) mw = 8;
        if (mw < 1) mw = 1;
        x0 = 120 - (n * mw + (n - 1) * gap) / 2;
        for (i = 0; i < n; i++)
            cr_fill(x0 + i * (mw + gap), CR_PY0 + ph - 8, mw, 4, i == sel ? col : T_LINE);
    }
}

static void cr_p_meter(const cr_screen_t *s, const cr_frame_t *fr, int32_t ph)
{
    uint16_t col = cr_rgb(s->col, CR_RED);
    uint32_t size = s->size ? s->size : 104u, n = cr_segs(s), i;
    int32_t vy = P8(CR_PY0) + P8(ph) / 2 + (int32_t)(size * 51u), w = cr_tw(s->value, size, 1);
    int32_t sx = w > P8(232) ? ((P8(232) << 12) / w) : 4096;
    int32_t gap = 4 * 16, sw = (224 * 16 - gap * (int32_t)(n - 1u)) / (int32_t)n, my = CR_PY0 + ph - 46;
    int32_t mh = s->thick ? s->thick : 14;
    cr_text(P8(120), vy, s->value, size, 1, CR_C, sx, col, T_BG, 0);
    if (s->sub[0]) cr_text(P8(120), vy + P8(22), s->sub, 15, 1, CR_C, 4096, col, T_BG, 0);
    for (i = 0; i < n; i++)
        cr_frect(8 * 16 + (int32_t)i * (sw + gap), my * 16, sw, mh * 16, i < fr->filled ? col : T_LINE);
    if (s->label[0]) cr_text(P8(120), P8(CR_PY0 + ph - 10), s->label, 14, 0, CR_C, 4096, T_MID, T_BG, 0);
    if (s->title[0]) cr_text(P8(10), P8(CR_PY0 + 16), s->title, 14, 1, CR_L, 4096, T_MID, T_BG, 0);
}

static void cr_p_stripes(const cr_screen_t *s, const cr_frame_t *fr, int32_t ph)
{
    int32_t bh = s->band ? s->band : 16, gap = s->gap ? s->gap : 8, nb = s->n_bands ? s->n_bands : 3, i, j;
    int32_t total = nb * bh + (nb - 1) * gap, y0 = CR_PY0 + ph - total - 24;
    for (i = 0; i < nb && i < 4; i++) {
        int32_t yb = y0 + i * (bh + gap);
        uint16_t c = cr_rgb(s->bands[i], CR_RED);
        for (j = 0; j < bh; j++) {       /* a row at a time: the skew shifts each row */
            int32_t x16 = ((-60 * 256 + fr->shift + fr->sweep[i] + s->skew * j) >> 4);
            cr_frect(x16, (yb + j) * 16, 360 * 16, 16, c);
        }
    }
    if (s->title[0]) {
        uint32_t px = s->title_px ? s->title_px : 34u;
        int32_t w = cr_tw(s->title, px, 1), sx = w > P8(224) ? ((P8(224) << 12) / w) : 4096;
        cr_text(P8(120), P8(s->title_y ? CR_PY0 + s->title_y : y0 - 22), s->title, px, 1, CR_C, sx,
                cr_rgb(s->title_col, CR_WHITE), T_BG, 0);
    }
    if (s->foot[0]) cr_text(P8(120), P8(CR_PY0 + ph - 8), s->foot, 12, 0, CR_C, 4096, T_MID, T_BG, 0);
}

static void cr_p_keyboard(const cr_screen_t *s, int32_t ph)
{
    int32_t kh = ph - 64 < 52 ? ph - 64 : 52;
    if (s->name.root[0]) cr_name(&s->name, P8(120), P8(CR_PY0 + 40), 40, 224, 4096);
    else if (s->title[0])
        cr_text(P8(120), P8(CR_PY0 + 36), s->title, s->title_px ? s->title_px : 15u, 1, CR_C, 4096,
                cr_rgb(s->col, CR_RED), T_BG, 0);
    cr_keyboard(s, CR_PY0 + ph - kh - 8, kh);
}

static void cr_p_arp(const cr_screen_t *s, int32_t ph)
{
    int32_t n = s->n_notes, by = CR_PY0 + ph - 46, gap16 = n > 1 ? 200 * 16 / (n - 1) : 0, x0, i, pos = s->pos;
    if (gap16 > 52 * 16) gap16 = 52 * 16;
    if (s->name.root[0]) cr_name(&s->name, P8(120), P8(CR_PY0 + 50), s->size ? s->size : 44u, 224, 4096);
    cr_fill(14, by + 17, 212, 2, T_LINE);
    x0 = 120 * 16 - (n - 1) * gap16 / 2;                            /* Q4 */
    if (pos >= 0 && pos < n && n > 1) {                            /* the dotted hop to the next note */
        int32_t xa = x0 + pos * gap16, xb = x0 + ((pos + 1) % n) * gap16;
        cr_quad(xa, (by - 22) * 16, (xa + xb) / 2, (by - 70) * 16, xb, (by - 2) * 16, 32, 48, 112,
                cr_rgb(s->hop_col, T_MID));
    }
    for (i = 0; i < n; i++) {
        uint16_t c = cr_rgb(s->note[i].col, CR_WHITE);
        int32_t x = x0 + i * gap16;
        if (i == pos) {                  /* the sounding note on its block */
            int32_t w = (cr_tw(s->note[i].t, 15, 1) >> 8) + 14;
            cv_rrect((x >> 4) - w / 2, by - 22 - 12, w, 24, 3, c, T_BG);
            cr_text(x << 4, P8(by - 22 + 5), s->note[i].t, 15, 1, CR_C, 4096, T_BG, c, 0);
        } else {
            cr_text(x << 4, P8(by + 5), s->note[i].t, 13, 1, CR_C, 4096, c, T_BG, 0);
        }
    }
    if (s->line[0]) cr_text(P8(120), P8(CR_PY0 + ph - 8), s->line, 13, 0, CR_C, 4096, cr_rgb(s->line_col, T_MID), T_BG, 0);
}

/* a params column's glyph in the box x .. x + 56, gy .. gy + 64 */
static void cr_glyph_param(const cr_param_t *c, int32_t x, int32_t gy, uint16_t col)
{
    const int32_t cw = 56, gh = 64, cx = x + cw / 2, w = 40;      /* line width 2.5 px (Q4) */
    int32_t pct = c->pct > 256u ? 256 : (int32_t)c->pct, k;
    int16_t p[CR_POLY_MAX * 2];
    uint32_t np = 0, cyc = c->cycles ? c->cycles : 2u;
#define CR_PT(px16, py16) (p[2 * np] = (int16_t)(px16), p[2 * np + 1] = (int16_t)(py16), np++)
    switch (c->glyph) {
    case CR_G_ENV: {
        int32_t ww = (cw - 10) * 16, bx = (x + 5) * 16, by = (gy + gh - 6) * 16;
        int32_t ax = bx + ww * 3 * c->env[0] / 2560, dx = ax + ww * 3 * c->env[1] / 2560, sx = bx + ww * 72 / 100;
        int32_t rx = sx + ww * 28 * c->env[3] / 25600, sy = by - (gh - 14) * 16 * c->env[2] / 256;
        CR_PT(bx, by); CR_PT(ax, (gy + 8) * 16); CR_PT(dx, sy); CR_PT(sx, sy); CR_PT(rx, by);
        cr_poly(p, np, w, 0, 0, col);
        break;
    }
    case CR_G_WAVE: {
        int32_t amp = (gh / 2 - 10) * 16 * (77 + 179 * pct / 256) / 256;
        for (k = 0; k <= 44; k++)
            CR_PT((x + 6) * 16 + k * (cw - 12) * 16 / 44,
                  (gy + gh / 2) * 16 - ((cr_sin((uint32_t)(k * 65536 / 44) * cyc) * amp) >> 14));
        cr_poly(p, np, w, 0, 0, col);
        break;
    }
    case CR_G_SAW: {
        int32_t sw = (cw - 12) * 16 / (int32_t)cyc;
        CR_PT((x + 6) * 16, (gy + gh - 10) * 16);
        for (k = 0; k < (int32_t)cyc; k++) {
            CR_PT((x + 6) * 16 + (k + 1) * sw, (gy + 10) * 16);
            CR_PT((x + 6) * 16 + (k + 1) * sw, (gy + gh - 10) * 16);
        }
        cr_poly(p, np, w, 0, 0, col);
        break;
    }
    case CR_G_SQUARE: {
        int32_t sw = (cw - 12) * 16 / (int32_t)cyc;
        for (k = 0; k < (int32_t)cyc; k++) {
            int32_t xx = (x + 6) * 16 + k * sw;
            CR_PT(xx, (gy + gh - 10) * 16); CR_PT(xx, (gy + 10) * 16); CR_PT(xx + sw * pct / 256, (gy + 10) * 16);
            CR_PT(xx + sw * pct / 256, (gy + gh - 10) * 16); CR_PT(xx + sw, (gy + gh - 10) * 16);
        }
        cr_poly(p, np, w, 0, 0, col);
        break;
    }
    case CR_G_FILTER: {
        int32_t fx = (x + 6) * 16 + (cw - 12) * 16 * pct / 256, y18 = (gy + 18) * 16;
        CR_PT((x + 6) * 16, y18); CR_PT(fx - 8 * 16, y18);
        cr_poly(p, np, w, 0, 0, col);
        cr_quad(fx - 8 * 16, y18, fx, y18, fx + 32, (gy + 10) * 16, w, 0, 0, col);
        cr_quad(fx + 32, (gy + 10) * 16, fx + 96, (gy + gh - 8) * 16, fx + 256, (gy + gh - 8) * 16, w, 0, 0, col);
        np = 0;
        CR_PT((x + 6) * 16, y18); CR_PT((x + cw - 6) * 16, y18);
        cr_poly(p, np, 16, 32, 80, T_MID);
        break;
    }
    case CR_G_BAR: {
        int32_t bh = (gh - 8) * pct / 256;
        for (k = 0; k < 7; k++)
            cr_fill(cx - 10, gy + 4 + k * (gh - 8) / 6, 20, 1, T_LINE);
        if (bh > 0) cv_rrect(cx - 9, gy + gh - 4 - bh, 18, bh, 3, col, T_BG);
        break;
    }
    case CR_G_STEPS: {
        int32_t n = c->n ? c->n : 8, sw = (cw - 10) * 16 / n;
        for (k = 0; k < n; k++)
            cr_frect((x + 5) * 16 + k * sw + 16, (gy + gh - 6) * 16 - (gh - 14) * 16 * ((k * 7) % 11) / 11, sw - 32, 48,
                     k * 256 <= pct * n ? col : T_LINE);
        break;
    }
    case CR_G_DOTS: {
        int32_t n = 1 + (pct * 11 + 128) / 256;
        for (k = 0; k < 12; k++) {
            uint32_t a = 49152u + (uint32_t)k * 65536u / 12u;
            cr_disc(cx * 16 + ((20 * 16 * cr_cos(a)) >> 14), (gy + gh / 2) * 16 + ((20 * 16 * cr_sin(a)) >> 14),
                    k < n ? 56 : 32, k < n ? col : T_LINE);
        }
        break;
    }
    default: {                           /* knob: a 270-degree arc with its pointer */
        uint32_t a0 = 24576u, sweep = 49152u, a = a0 + sweep * (uint32_t)pct / 256u;
        int32_t kcy = (gy + gh / 2) * 16;
        cr_arc(cx * 16, kcy, 20 * 16, 64, a0, sweep, 1, 0, 0, T_LINE);
        if (pct) cr_arc(cx * 16, kcy, 20 * 16, 64, a0, sweep * (uint32_t)pct / 256u, 1, 0, 0, col);
        CR_PT(cx * 16 + ((6 * 16 * cr_cos(a)) >> 14), kcy + ((6 * 16 * cr_sin(a)) >> 14));
        CR_PT(cx * 16 + ((14 * 16 * cr_cos(a)) >> 14), kcy + ((14 * 16 * cr_sin(a)) >> 14));
        cr_poly(p, np, 48, 0, 0, col);
        break;
    }
    }
#undef CR_PT
}

static void cr_p_params(const cr_screen_t *s, int32_t ph)
{
    static const uint8_t KC[4] = {CR_COL_BLUE, CR_COL_ORANGE, CR_COL_WHITE, CR_COL_RED};
    int32_t y = CR_PY0 + 4, gy, i;
    if (s->title[0]) {
        cr_text(P8(10), P8(y + 14), s->title, 15, 1, CR_L, 4096, cr_rgb(s->col, CR_WHITE), T_BG, 0);
        if (s->page[0]) cr_text(P8(230), P8(y + 14), s->page, 12, 0, CR_R, 4096, T_MID, T_BG, 0);
        y += 22;
    }
    gy = y + 10;
    for (i = 0; i < 4; i++) {
        const cr_param_t *c = &s->par[i];
        uint16_t col = cr_rgb(c->col ? c->col : KC[i], CR_WHITE);
        int32_t x = 8 + i * 58, cx = x + 28;
        if (!c->label[0] && !c->value[0]) continue;
        cr_glyph_param(c, x, gy, col);
        cr_text_fit(P8(cx), P8(gy + 64 + 14), c->label, 11, 1, CR_C, T_MID, T_BG, P8(54));
        cr_text_fit(P8(cx), P8(gy + 64 + 33), c->value, 15, 1, CR_C, col, T_BG, P8(54));
    }
    if (s->foot[0]) cr_text(P8(120), P8(CR_PY0 + ph - 8), s->foot, 11, 0, CR_C, 4096, T_MID, T_BG, 0);
}

static void cr_p_geek(const cr_screen_t *s, int32_t ph)
{
    uint32_t i;
    cr_name(&s->name, P8(70), P8(CR_PY0 + 56), 40, 224, 4096);
    for (i = 0; i < s->n_notes; i++)
        cr_text(P8(230), P8(CR_PY0 + 30 + 14 * (int32_t)i), s->note[i].t, 12, 1, CR_R, 4096,
                cr_rgb(s->note[i].col, CR_WHITE), T_BG, 0);
    for (i = 0; i < s->n_lines && i < 2u; i++)
        cr_text_fit(P8(10), P8(CR_PY0 + 72 + 11 * (int32_t)i), s->lines[i].t, 10, 0, CR_L, T_MID, T_BG, P8(150));
    cr_keyboard(s, CR_PY0 + ph - 30, 26);
}

static void cr_p_text(const cr_screen_t *s)
{
    int32_t y = CR_PY0 + 16, yy;
    uint32_t i;
    if (s->title[0]) {
        cr_text(P8(10), P8(y), s->title, 15, 1, CR_L, 4096, cr_rgb(s->title_col, CR_RED), T_BG, 0);
        y += 8;
    }
    yy = y + 14;
    for (i = 0; i < s->n_lines; i++) {
        const cr_line_t *l = &s->lines[i];
        uint32_t px = l->px ? l->px : 12u;
        yy += (int32_t)px - 12;
        if (l->t[0])
            cr_text_fit(P8(l->center ? 120 : 10), P8(yy), l->t, px, l->bold, l->center ? CR_C : CR_L,
                        cr_rgb(l->col, CR_WHITE), T_BG, P8(220));
        yy += 16;
    }
}

static void cr_p_big(const cr_screen_t *s, int32_t ph)
{
    uint32_t size = s->size ? s->size : 118u;
    int32_t vy = P8(CR_PY0) + P8(ph) / 2 + (int32_t)(size * 87u) - P8(s->sub[0] ? 12 : 4), w = cr_tw(s->value, size, 1);
    int32_t sx = w > P8(228) ? ((P8(228) << 12) / w) : 4096;
    uint16_t under = s->block ? cr_rgb(s->block, CR_RED) : T_BG;
    if (s->block) cr_fill(0, CR_PY0, 240, ph, under);
    cr_text(P8(120), vy, s->value, size, 1, CR_C, sx, s->block ? T_BG : cr_rgb(s->col, CR_WHITE), under, 0);
    if (s->sub[0]) cr_text(P8(120), P8(CR_PY0 + ph - 26), s->sub, 15, 1, CR_C, 4096, s->block ? T_BG : T_TEXT, under, 0);
    if (s->label[0]) cr_text(P8(120), P8(CR_PY0 + ph - 8), s->label, 14, 0, CR_C, 4096, s->block ? T_BG : T_MID, under, 0);
    if (s->title[0]) cr_text(P8(10), P8(CR_PY0 + 14), s->title, 12, 1, CR_L, 4096, T_MID, under, 0);
}

/* the whole screen into the canvas (its rows only) */
static void cr_compose(const cr_screen_t *s, const cr_frame_t *fr)
{
    int32_t ph = cr_ph(s);
    cr_header(s);
    switch (s->kind) {
    case CR_K_STRIPES: cr_p_stripes(s, fr, ph); break;
    case CR_K_CHORD: cr_p_chord(s, fr, ph); break;
    case CR_K_PICKER: cr_p_picker(s, fr, ph); break;
    case CR_K_METER: cr_p_meter(s, fr, ph); break;
    case CR_K_KEYBOARD: cr_p_keyboard(s, ph); break;
    case CR_K_ARP: cr_p_arp(s, ph); break;
    case CR_K_PARAMS: cr_p_params(s, ph); break;
    case CR_K_GEEK: cr_p_geek(s, ph); break;
    case CR_K_TEXT: cr_p_text(s); break;
    case CR_K_BIG: cr_p_big(s, ph); break;
    default: break;
    }
    if (s->footer[0]) {
        char b[48];
        int cut = cr_fit(b, sizeof b, s->footer, 12, 0, P8(224));
        cr_text(P8(120), P8(226), b, 12, 0, CR_C, 4096, T_MID, T_BG, cut ? 9u : 0u);
    }
    if (s->ring_on) {                    /* Orchid's ring: a dotted circle round the edge, the progress solid */
        cr_arc(120 * 16, 120 * 16, 113 * 16, 5 * 16, 0, 65536u, 0, 2 * 16, 6 * 16, T_LINE);
        if (s->ring)
            cr_arc(120 * 16, 120 * 16, 113 * 16, 5 * 16, 49152u, s->ring >= 256u ? 65536u : (uint32_t)s->ring << 8, 0, 0,
                   0, s->ring_rec ? T_REC : cr_rgb(s->ring_col, CR_RED));
    }
    if (s->message[0]) {                 /* a transient message box over the panel */
        int32_t w = (cr_tw(s->message, 16, 1) >> 8) + 28;
        uint16_t c = cr_rgb(s->message_col, CR_WHITE);
        if (w > 228) w = 228;
        cv_rrect(120 - w / 2, 100, w, 40, 8, c, T_BG);
        cr_text(P8(120), P8(125), s->message, 16, 1, CR_C, 4096, T_BG, c, 0);
    }
}

/* ------------------------------------------------------------ cache --- */
#define CR_STRIP_H 40u
#define CR_NSTRIP (240u / CR_STRIP_H)
static struct {
    uint32_t sig, pix[CR_NSTRIP];
    uint8_t valid, force;
    uint8_t blits;                       /* strips blitted by the last cr_draw (the host test reads it) */
} cr_dc;
static uint32_t cr_strip_y;              /* the strip being drawn: its top row */

static uint32_t cr_hash(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t cr_hash_px(void)
{
    uint32_t h = 2166136261u, i, n = cv_w * cv_h;
    for (i = 0; i < n; i++) h = (h ^ cv_px[i]) * 16777619u;
    return h;
}

/* the next cr_draw draws and blits the whole screen (after something else drew on it, a palette change) */
static void cr_draw_invalidate(void) { cr_dc.force = 1; }

static void cr_draw(const cr_screen_t *s, uint32_t anim_ms)
{
    cr_frame_t fr;
    uint32_t sig, k;
    cr_frame(s, anim_ms, &fr);
    sig = cr_hash(cr_hash(2166136261u ^ ux.gen, s, sizeof *s), &fr, sizeof fr);
    cr_dc.blits = 0;
    if (cr_dc.valid && !cr_dc.force && sig == cr_dc.sig)
        return;                          /* nothing changed: nothing drawn */
    for (k = 0; k < CR_NSTRIP; k++) {
        uint32_t h;
        cr_strip_y = k * CR_STRIP_H;
        cv_begin(240u, CR_STRIP_H, T_BG);
        cv_oy = -(int32_t)cr_strip_y;
        cr_clip_all();
        cr_compose(s, &fr);
        cv_oy = 0;
        h = cr_hash_px();
        if (cr_dc.force || !cr_dc.valid || h != cr_dc.pix[k]) {
            cv_blit(0, cr_strip_y);
            cr_dc.pix[k] = h;
            cr_dc.blits++;
        }
    }
    cr_dc.sig = sig;
    cr_dc.valid = 1;
    cr_dc.force = 0;
}
