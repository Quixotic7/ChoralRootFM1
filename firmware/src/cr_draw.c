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
 * strip is drawn and only the tiles whose pixels changed are blitted (cr_draw_invalidate: all of them, once); when
 * only the ring's fraction moved, only the strips its tip crossed are drawn (the ring itself: a table, cr_ring_draw);
 * when only the editor's parts changed (a row's cells, the band, the hot cell, the title line), only their strips
 * (a knob row's cells and hot cell the same: its row's strips).
 * A blit goes out by DMA from its own buffer while the next strip is drawn (cr_send).
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
#define CR_INTRO_MS 220u                 /* power-on: each band slides in, 90 ms after the one above */
#define CR_INTRO_GAP 90u
#define CR_INTRO_NAME 300u               /* .. the name lands from 300 ms, over 150 ms */
#define CR_SWEEP_MS 240u                 /* the idle stripes swept off, each band 40 ms after the one above */
typedef struct {
    int32_t sq;                          /* chord: the squeeze of the name drawn (Q12) */
    int32_t slide;                       /* picker: how far the selection still has to slide (Q12 of its pitch) */
    int32_t shift;                       /* stripes: Q8 px */
    int32_t sweep[4];                    /* stripes: Q8 px each band is swept right */
    int32_t name_dy;                     /* stripes (CR_A_INTRO): Q8 px the name is still above its place */
    uint8_t name_off;                    /* stripes (CR_A_INTRO): the name not drawn yet */
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
    if ((s->kind == CR_K_PICKER || s->kind == CR_K_KNOBROW) && (s->anim & CR_A_SLIDE) && s->slide && t < CR_SLIDE_MS) {
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
        if (s->anim & CR_A_INTRO) {                                /* (anim_ms settled: the final picture) */
            for (i = 0; i < 4u; i++) {
                uint32_t d = i * CR_INTRO_GAP;
                fr->sweep[i] -= t <= d ? 300 * 256 : ((4096 - cr_ease_out(t - d, CR_INTRO_MS)) * 300) >> 4;
                if (t < d + CR_INTRO_MS) fr->busy = 1;
            }
            fr->name_off = t < CR_INTRO_NAME;
            if (t < CR_INTRO_NAME + 150u) {
                fr->busy = 1;
                if (t >= CR_INTRO_NAME)
                    fr->name_dy = ((4096 - cr_ease_out(t - CR_INTRO_NAME, 150u)) * 24) >> 4;
            }
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

/* the battery: a 16 x 10 case at (rx, 7) with its nub, lvl 0..4 (4 = charging) quarters filled */
static void cr_battery(int32_t rx, uint32_t lvl)
{
    if (lvl > 4u) lvl = 4u;
    cr_fill(rx + 1, 7, 14, 1, T_MID);                                /* the case: 16 x 10, 1 px, round corners */
    cr_fill(rx + 1, 16, 14, 1, T_MID);
    cr_fill(rx, 8, 1, 8, T_MID);
    cr_fill(rx + 15, 8, 1, 8, T_MID);
    cr_frect((rx + 16) * 16 + 8, 9 * 16 + 8, 32, 80, T_MID);          /* the nub */
    if (lvl) cr_frect((rx + 2) * 16, 9 * 16, (int32_t)(12u * 16u * lvl / 4u), 6 * 16, lvl <= 1u ? CR_YELLOW : CR_RED);
}

/* the corner dial (a loop playing, no ring): Orchid's ring shrunk into the top line's right end, centre (229, 12), r 8,
 * 3 px: the dotted track (10 dots of 2.5 px, about 2 on 3 off, T_LINE: cr_disc, cheaper than a dashed cr_arc's per-sample angles), the
 * progress red clockwise from 12 o'clock, 5 px on the downbeat's frame. Rows 2..22: strip 0 only (cr_draw redraws
 * that strip alone when only its fraction or pulse moved) */
#define CR_DIAL_X (229 * 16)
#define CR_DIAL_Y (12 * 16)
#define CR_DIAL_R (8 * 16)
#define CR_DIAL_SHIFT 22                 /* the header's right text moves this far left while the dial shows */
static void cr_dial(const cr_screen_t *s)
{
    uint32_t sweep = s->dial >= 256u ? 65536u : (uint32_t)s->dial << 8;
    uint32_t k;
    if (cr_row0() > 23) return;          /* (not this strip) */
    for (k = 0; k < 10u; k++) {
        uint32_t a = k * 65536u / 10u;
        cr_disc(CR_DIAL_X + ((CR_DIAL_R * cr_cos(a)) >> 14), CR_DIAL_Y + ((CR_DIAL_R * cr_sin(a)) >> 14), 20, T_LINE);
    }
    if (sweep)
        cr_arc(CR_DIAL_X, CR_DIAL_Y, CR_DIAL_R, s->dial_pulse ? 5 * 16 : 3 * 16, 49152u, sweep, 0, 0, 0, CR_RED);
}

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
    if (s->dial_on) {
        rx -= CR_DIAL_SHIFT;
        rightw += CR_DIAL_SHIFT;
    }
    if (s->batt != 255u) rightw += 26;
    if (s->right[0]) rightw += (cr_tw(s->right, 12, 1) >> 8) + 8;
    if (s->mid[0])
        cr_text_fit(P8(x + 8), base, s->mid, (uint32_t)px, 1, CR_L, cr_rgb(s->mid_col, CR_WHITE), T_BG,
                    P8(236 - rightw - x - 10));
    if (s->batt != 255u) {
        rx -= 20;
        cr_battery(rx, s->batt);
        rx -= 6;
    }
    if (s->right[0])
        cr_text(P8(rx), base, s->right, (uint32_t)px, 1, CR_R, 4096, cr_rgb(s->right_col, bare ? CR_WHITE : T_MID),
                T_BG, 0);
    if (s->dial_on)
        cr_dial(s);
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

/* the picker's square position marks: n of them centred on x 120 at row y, the selected one in col */
static void cr_pick_marks(int32_t n, int32_t sel, int32_t y, uint16_t col)
{
    int32_t mw = 200 / n - 3, gap = 4, x0, i;
    if (mw > 8) mw = 8;
    if (mw < 1) mw = 1;
    x0 = 120 - (n * mw + (n - 1) * gap) / 2;
    for (i = 0; i < n; i++)
        cr_fill(x0 + i * (mw + gap), y, mw, 4, i == sel ? col : T_LINE);
}
/* a horizontal picker's neighbours: 13 px dim at lx (left) and rx (right), on baseline base - 4 (Q8), cut to room */
static void cr_pick_sides(const cr_screen_t *s, int32_t n, int32_t sel, int32_t base, int32_t lx, int32_t rx,
                          int32_t room)
{
    if (sel > 0 && room > P8(24))
        cr_text_fit(P8(lx), base - P8(4), cr_item(s, sel - 1), 13, 0, CR_L, T_DIM, T_BG, room);
    if (sel < n - 1 && room > P8(24)) {
        char b[48];
        int cut = cr_fit(b, sizeof b, cr_item(s, sel + 1), 13, 0, room);
        cr_text(P8(rx), base - P8(4), b, 13, 0, CR_R, 4096, T_DIM, T_BG, cut ? 9u : 0u);
    }
}
/* the picker's item, huge, squeezed to maxw, centred on x 120 on baseline base (Q8); sliding (fr->slide): the old
 * one out and the new one in inside the item's box (horiz: x 120 +- maxw / 2; else the rows y0 .. y1, Q8) */
static void cr_pick_item(const cr_screen_t *s, const cr_frame_t *fr, int32_t sel, int32_t size, int32_t maxw,
                         int horiz, int32_t base, int32_t y0, int32_t y1, uint16_t col)
{
    const char *big = cr_item(s, sel);
    int32_t w = cr_tw(big, (uint32_t)size, 1), sx = w > P8(maxw) ? ((P8(maxw) << 12) / w) : 4096;
    if (fr->slide && s->slide) {
        const char *old = cr_item(s, sel - s->slide);
        int32_t wo = cr_tw(old, (uint32_t)size, 1), so = wo > P8(maxw) ? ((P8(maxw) << 12) / wo) : 4096;
        int32_t pitch = horiz ? P8(maxw / 2 + 60) : y1 - y0, d = (pitch * fr->slide) >> 12;
        int32_t dir = s->slide > 0 ? 1 : -1;
        if (horiz) {
            cr_clip_set(120 - maxw / 2 - 4, 0, 120 + maxw / 2 + 4, 240);
            cr_text(P8(120) + dir * d, base, big, (uint32_t)size, 1, CR_C, sx, col, T_BG, 32u);
            cr_text(P8(120) + dir * (d - pitch), base, old, (uint32_t)size, 1, CR_C, so, col, T_BG, 32u);
        } else {
            cr_clip_set(0, y0 >> 8, 240, ((y1 + 255) >> 8) + 4);   /* (+4: descenders) */
            cr_text(P8(120), base + dir * d, big, (uint32_t)size, 1, CR_C, sx, col, T_BG, 32u);
            cr_text(P8(120), base + dir * (d - pitch), old, (uint32_t)size, 1, CR_C, so, col, T_BG, 32u);
        }
        cr_clip_all();
    } else {
        cr_text(P8(120), base, big, (uint32_t)size, 1, CR_C, sx, col, T_BG, 0);
    }
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
        int32_t lx = ringed ? 36 : 6, rx = ringed ? 204 : 234, bw = cr_tw(big, (uint32_t)size, 1);
        if (bw > P8(maxw)) bw = P8(maxw);
        cr_pick_sides(s, n, sel, base, lx, rx, (P8(rx - lx) - bw) / 2 - P8(10));
    } else {
        if (sel > 0) cr_text_fit(P8(120), top + P8(14), cr_item(s, sel - 1), 15, 0, CR_C, T_DIM, T_BG, P8(maxw));
        if (sel < n - 1)
            cr_text_fit(P8(120), top + nbh + bigh + valh + P8(16), cr_item(s, sel + 1), 15, 0, CR_C, T_DIM, T_BG, P8(maxw));
    }
    cr_pick_item(s, fr, sel, size, maxw, horiz, base, top + nbh, top + nbh + bigh, col);
    if (s->value[0]) cr_text(P8(120), top + nbh + bigh + P8(20), s->value, 20, 1, CR_C, 4096, col, T_BG, 0);
    if (s->label[0])
        cr_text(P8(120), P8(CR_PY0 + ph) - marksh - P8(ringed ? 34 : 8), s->label, 13, 0, CR_C, 4096, T_MID, T_BG, 0);
    if (n > 1 && !ringed)                /* square position marks */
        cr_pick_marks(n, sel, CR_PY0 + ph - 8, col);
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
    if (s->sub[0] && s->sub_mark) {      /* the name and a 5 px square after it, centred together */
        int32_t tw = cr_tw(s->sub, 15, 1), x0 = P8(120) - (tw + P8(9)) / 2;
        cr_text(x0, vy + P8(22), s->sub, 15, 1, CR_L, 4096, col, T_BG, 0);
        cr_frect((x0 + tw + P8(4)) >> 4, ((vy + P8(22)) >> 4) - 6 * 16, 5 * 16, 5 * 16, col);
    } else if (s->sub[0]) {
        cr_text(P8(120), vy + P8(22), s->sub, 15, 1, CR_C, 4096, col, T_BG, 0);
    }
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
    if (s->title[0] && !fr->name_off) {
        uint32_t px = s->title_px ? s->title_px : 34u;
        int32_t w = cr_tw(s->title, px, 1), sx = w > P8(224) ? ((P8(224) << 12) / w) : 4096;
        cr_text(P8(120), P8(s->title_y ? CR_PY0 + s->title_y : y0 - 22) - fr->name_dy, s->title, px, 1, CR_C, sx,
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

/* ------------------------------------------------------ the sound editor --- */
/* CR_K_EDIT8 and CR_K_STACK (../ChoralRootFM1Designer/FORMAT.md "Sound editor panels"): the whole screen, no
 * header. The title line (0..24), then edit8: an optional wide band (24..120: the AHDSR or the filter's response)
 * over two rows of four cells (124..180, 184..240; without the band 30..130, 134..234), or stack: column headings
 * at y 36 over N rows sharing 41..239. The active row is in the knob colours (blue orange white green) with a 2 px
 * bar under each cell, the others grey; the hot cell's value sits on a block of its colour. Cheap by design: text,
 * rectangles, small glyphs, at most two polylines of <= 12 segments in the band. No motion: a change is drawn at
 * once, and cr_draw composes only the strips of the parts that changed (the band, a row; cr_ed_strips). A cell
 * whose parameter the matrix modulates has a 4 px mark at its top right (CR_CF_MARK). */
static const uint8_t CR_KC[4] = {CR_COL_BLUE, CR_COL_ORANGE, CR_COL_WHITE, CR_COL_GREEN};

/* the VA's MORPH wave (eng_va.c va_morph, its alignment) at x (Q12 of a cycle, 0..4096; left: the limit from the
 * left at a jump): shape k (0 sine, 1 triangle, 2 saw, 3 ramp, 4 square, 5 the pulse rising at e), +-4096 (up +).
 * Drawn full height each (the device's square is half the saw's: legibility first) */
static int32_t cr_mshape(uint32_t k, int32_t x, int left, int32_t e)
{
    int32_t p = (x + 3072) & 4095;
    switch (k) {
    case 0: return -(cr_sin((uint32_t)x << 4) >> 2);                  /* the sine half a cycle on */
    case 1: return p < 2048 ? 4 * p - 4096 : 12288 - 4 * p;           /* the triangle three quarters on */
    case 2: return 2 * x - 4096;                                      /* the saw */
    case 3: return x < 2048 || (x == 2048 && left) ? -2 * x : 8192 - 2 * x;   /* the ramp: the saw half on, reversed */
    case 4: e = 2048; /* fall through */
    default: return x < e || (x == e && left) ? -4096 : 4096;
    }
}
/* the morph position m (0..127) at x: the crossfade of the neighbouring shapes (the positions 0 24 48 72 96), past 96
 * the pulse narrowing from 50 % to ~5 % (its edge at e) */
static int32_t cr_morph_y(int32_t m, int32_t x, int left, int32_t e)
{
    int32_t k = m / 24, f = m % 24;
    if (m >= 96) return cr_mshape(5, x, left, e);
    return (cr_mshape((uint32_t)k, x, left, e) * (24 - f) + cr_mshape((uint32_t)k + 1u, x, left, e) * f) / 24;
}

/* a stroked rectangle (Q4): its edges lw wide centred on the outline, as four rectangles that do not overlap */
static void cr_srect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t lw, uint16_t col)
{
    cr_frect(x - lw / 2, y - lw / 2, w + lw, lw, col);
    cr_frect(x - lw / 2, y + h - lw / 2, w + lw, lw, col);
    cr_frect(x - lw / 2, y + lw / 2, lw, h - lw, col);
    cr_frect(x + w - lw / 2, y + lw / 2, lw, h - lw, col);
}

/* the moon's lit part: within the circle (cx, cy, r), the right half less the ellipse (rx, r) for k >= 0 (a crescent),
 * the right half plus it for k < 0 (gibbous); 4 x 4 samples a pixel (Q4), a pixel whose four corner samples agree
 * taken whole (the shapes are smooth at a pixel's scale) */
static int cr_moon_in(int32_t dx, int32_t dy, int64_t r2, int64_t rx2, int64_t e, int gib)
{
    int64_t el;
    if ((int64_t)dx * dx + (int64_t)dy * dy > r2) return 0;
    el = (int64_t)dx * dx * r2 + (int64_t)dy * dy * rx2;   /* <= e: inside the ellipse */
    return gib ? (dx >= 0 || el <= e) : (dx >= 0 && el >= e);
}
static void cr_moon_fill(int32_t cx, int32_t cy, int32_t r, int32_t rx, int gib, uint16_t col)
{
    int32_t x0 = (cx - r) >> 4, x1 = (cx + r + 15) >> 4, y0 = (cy - r) >> 4, y1 = (cy + r + 15) >> 4, i, j;
    int64_t r2 = (int64_t)r * r, rx2 = (int64_t)rx * rx, e = r2 * rx2;
    if (y0 < cr_row0()) y0 = cr_row0();
    if (y1 > cr_row1()) y1 = cr_row1();
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) {
            int32_t ax = i * 16 + CR_SS[0] - cx, bx = i * 16 + CR_SS[3] - cx, ay = j * 16 + CR_SS[0] - cy,
                    by = j * 16 + CR_SS[3] - cy;
            uint32_t n = (uint32_t)(cr_moon_in(ax, ay, r2, rx2, e, gib) + cr_moon_in(bx, ay, r2, rx2, e, gib) +
                                    cr_moon_in(ax, by, r2, rx2, e, gib) + cr_moon_in(bx, by, r2, rx2, e, gib)), a, b;
            if (n == 4u) n = 16u;
            else if (n) {
                for (n = 0, b = 0; b < 4u; b++)
                    for (a = 0; a < 4u; a++)
                        n += (uint32_t)cr_moon_in(i * 16 + CR_SS[a] - cx, j * 16 + CR_SS[b] - cy, r2, rx2, e, gib);
            }
            if (n) cr_blend(i, j, col, n * 16u);
        }
}

/* the parameter pictograms (FORMAT.md "cell glyphs", the designer's drawPicto) in the box x, y, w, h (Q4): flat 2 px
 * strokes in col (cr_poly, round joins; a path that retraces a segment is still one coverage), filled parts in col,
 * 2 px inside the box; pct, pct2 Q8 (256 = 1) */
static void cr_picto(uint32_t g, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t col, int32_t pct, int32_t pct2)
{
    int32_t lw = 32, x0 = x + lw, x1 = x + w - lw, y0 = y + lw, y1 = y + h - lw, W = x1 - x0, H = y1 - y0;
    int32_t cx = x + w / 2, cy = y + h / 2, mn = W < H ? W : H, k;
    int16_t p[48 * 2];
    uint32_t np = 0;
#define CR_PT(px16, py16) (p[2 * np] = (int16_t)(px16), p[2 * np + 1] = (int16_t)(py16), np++)
    switch (g) {
    case CR_G_ROOM: {                               /* one-point perspective: the far wall 70 % .. 22 % of the box */
        int32_t bw = H * 14 / 10 < W ? H * 14 / 10 : W, bx = cx - bw / 2, by = y0, f = 2867 - 1966 * pct / 256;
        int32_t iw = bw * f >> 12, ih = H * f >> 12, ix = cx - iw / 2, iy = cy - ih / 2;
        cr_srect(bx, by, bw, H, lw, col);            /* the box and the far wall: rectangles (cheap), the corner */
        cr_srect(ix, iy, iw, ih, lw, col);           /* lines: four short polylines */
        for (k = 0; k < 4; k++) {
            np = 0;
            CR_PT(k & 1 ? bx + bw : bx, k & 2 ? by + H : by);
            CR_PT(k & 1 ? ix + iw : ix, k & 2 ? iy + ih : iy);
            cr_poly(p, np, lw, 0, 0, col);
        }
        break;
    }
    case CR_G_MOON: {                               /* lit 8 % (a thin crescent) .. 100 % (full); the rim outlined */
        int32_t r = mn / 2, f = 328 + 3768 * pct / 256, kk = 4096 - 2 * f, rx = r * (kk < 0 ? -kk : kk) >> 12;
        cr_moon_fill(cx, cy, r, rx > 0 ? rx : 1, kk < 0, col);
        cr_arc(cx, cy, r, lw, 0, 65536u, 0, 0, 0, col);
        break;
    }
    case CR_G_ECHOES: {                             /* a struck bar and its repeats: spacing pct, feedback pct2 */
        int32_t bw = 48, sp = 80 + ((W - bw) / 2 - 80) * pct / 256, d = 819 + 3072 * pct2 / 256, bh = H, bx;
        if (!pct && !pct2) {                        /* both 0: nothing struck (the loop's Quantize "none"): the
                                                     * baseline alone */
            cr_frect(x0, y1, W, 16, col);
            break;
        }
        if (sp < 64) sp = 64;
        cr_frect(x0, y1 - H, bw, H, col);
        for (bx = x0 + sp; bx + bw <= x1; bx += sp) {
            bh = bh * d >> 12;
            if (bh < 24) break;
            cr_frect(bx, y1 - bh, bw, bh, col);
        }
        cr_frect(x0, y1, W, 16, col);               /* the baseline, 1 px */
        break;
    }
    case CR_G_LFO: {                                /* 1 .. 5 cycles (pct), nearly flat .. full height (pct2) */
        int32_t cyc = 4096 + 16384 * pct / 256, a = (H / 2) * (328 + 3768 * pct2 / 256) >> 12;
        int32_t n = 8 * ((cyc + 4095) >> 12);
        if (n < 16) n = 16;
        if (n > 44) n = 44;
        for (k = 0; k <= n; k++)
            CR_PT(x0 + k * W / n, cy - ((cr_sin((uint32_t)(k * cyc * 16 / n)) * a) >> 14));
        cr_poly(p, np, lw, 0, 0, col);
        break;
    }
    case CR_G_CLIP: {                               /* one sine cycle into a clipper, gain 1 .. 10 */
        int32_t gq = 4096 + 36864 * pct / 256, a = H / 2, v;
        for (k = 0; k <= 32; k++) {
            v = (cr_sin((uint32_t)(k * 65536 / 32)) * gq) >> 12;   /* Q14 */
            v = v > 16384 ? 16384 : v < -16384 ? -16384 : v;
            CR_PT(x0 + k * W / 32, cy - ((v * a) >> 14));
        }
        if (pct > 25)                               /* the clip levels, dashed 1 px (2 on, 2 off) */
            for (k = x0; k < x1; k += 64) {
                int32_t dw = x1 - k < 32 ? x1 - k : 32;
                cr_frect(k, y0 - 8, dw, 16, col);
                cr_frect(k, y1 - 8, dw, 16, col);
            }
        cr_poly(p, np, lw, 0, 0, col);
        break;
    }
    case CR_G_SPRING: {                             /* a coil of 6.5 turns between two short ends */
        int32_t e = W * 12 / 100, zx0 = x0 + e, zw = W - 2 * e, a = H * 36 / 100;
        CR_PT(x0, cy); CR_PT(zx0, cy);
        for (k = 0; k < 13; k++) CR_PT(zx0 + zw * (2 * k + 1) / 26, cy + (k & 1 ? a : -a));
        CR_PT(zx0 + zw, cy); CR_PT(x1, cy);
        cr_poly(p, np, lw, 0, 0, col);
        break;
    }
    case CR_G_MIX: {                                /* dry (back, outlined) behind wet (front, filled to pct) */
        int32_t s = mn * 72 / 100, off = mn - s, bx = cx - (s + off) / 2, by = cy - (s + off) / 2, fx = bx + off,
                fy = by + off, fh = s * pct / 256;
        cr_srect(bx, by, s, s, lw, col);
        cr_frect(fx - lw, fy - lw, s + 2 * lw, s + 2 * lw, T_BG);   /* the front square hides the back one */
        if (fh > 0) cr_frect(fx, fy + s - fh, s, fh, col);
        cr_srect(fx, fy, s, s, lw, col);
        break;
    }
    case CR_G_GATE: {                               /* a pulse 10 .. 100 % of the box wide */
        int32_t pw = W * (26 + 230 * pct / 256) / 256, top = y0 + H * 12 / 100;
        CR_PT(x0, y1); CR_PT(x0, top); CR_PT(x0 + pw, top); CR_PT(x0 + pw, y1); CR_PT(x1, y1);
        cr_poly(p, np, lw, 0, 0, col);
        break;
    }
    case CR_G_RANGE: {                              /* a span with end stops, a thick segment 10 .. 100 % from the left */
        int32_t t = H / 4;
        CR_PT(x0, cy - t); CR_PT(x0, cy + t); CR_PT(x0, cy); CR_PT(x1, cy); CR_PT(x1, cy - t); CR_PT(x1, cy + t);
        cr_poly(p, np, lw, 0, 0, col);
        cr_frect(x0, cy - 40, W * (26 + 230 * pct / 256) / 256, 80, col);
        break;
    }
    case CR_G_ARROW: {                              /* up / down / up and down / random (three dots) */
        int32_t hd = H * 32 / 100 < W * 30 / 100 ? H * 32 / 100 : W * 30 / 100, n = 0, ax[2], up[2];
        if (pct < 64) ax[0] = cx, up[0] = 1, n = 1;
        else if (pct < 128) ax[0] = cx, up[0] = 0, n = 1;
        else if (pct < 192) {
            int32_t dx = W * 22 / 100 < hd * 14 / 10 ? W * 22 / 100 : hd * 14 / 10;
            ax[0] = cx - dx, up[0] = 1, ax[1] = cx + dx, up[1] = 0, n = 2;
        } else {
            int32_t d = mn * 32 / 100;
            cr_disc(cx, cy - d, 40, col);
            cr_disc(cx - d * 95 / 100, cy + d * 6 / 10, 40, col);
            cr_disc(cx + d * 95 / 100, cy + d * 6 / 10, 40, col);
        }
        for (k = 0; k < n; k++) {
            int32_t t = up[k] ? y0 : y1, b = up[k] ? y1 : y0, sg = up[k] ? 1 : -1;
            np = 0;
            CR_PT(ax[k], b); CR_PT(ax[k], t); CR_PT(ax[k] - hd, t + sg * hd); CR_PT(ax[k], t); CR_PT(ax[k] + hd, t + sg * hd);
            cr_poly(p, np, lw, 0, 0, col);
        }
        break;
    }
    case CR_G_SHIFT: {                              /* five staff lines, a square on line round(4 pct) from the bottom */
        int32_t sq = H / 5 > 64 ? H / 5 : 64, ly0 = y0 + sq / 2, ly1 = y1 - sq / 2, gap = (ly1 - ly0) / 4;
        int32_t l2 = W < H * 16 / 10 ? W : H * 16 / 10, yy;
        for (k = 0; k < 5; k++)
            cr_frect(cx - l2 / 2 - lw / 2, ly0 + k * gap - lw / 2, l2 + lw, lw, col);
        yy = ly1 - ((pct * 4 + 128) >> 8) * gap;
        cr_frect(cx - sq / 2, yy - sq / 2, sq, sq, col);
        break;
    }
    case CR_G_HARM: {                               /* one cycle: a sine at 0.5, + the odd series -> 1, + all -> 0 */
        int32_t so = 2 * pct - 256, sa = 256 - 2 * pct, v[41], mx = 1, j;   /* (Q8) */
        if (so < 0) so = 0;
        if (sa < 0) sa = 0;
        for (k = 0; k <= 40; k++) {
            uint32_t ph = (uint32_t)(k * 65536 / 40);
            int32_t a = cr_sin(ph);                                  /* Q14 */
            for (j = 2; j <= 7; j++)
                a += cr_sin(ph * (uint32_t)j) * ((j & 1 ? so : 0) + sa) / (256 * j);
            v[k] = a;
            if ((a < 0 ? -a : a) > mx) mx = a < 0 ? -a : a;
        }
        for (k = 0; k <= 40; k++) CR_PT(x0 + k * W / 40, cy - v[k] * (H / 2) / mx);
        cr_poly(p, np, lw, 0, 0, col);
        break;
    }
    case CR_G_DETUNE: {                             /* a 16-segment line, the joints jittered by pct, segments dropped */
        /* the designer's sin(i 78.233 + 1.7), Q8: a fixed jitter, the same every frame */
        static const int16_t JT[17] = {254, -252, 227, -180, 117, -42, -36, 111, -175, 223, -251, 255, -235, 193, -133,
                                       61, 17};
        int32_t a = H * 115 / 256 * pct / 256;                       /* 0.45 H pct */
        for (k = 0; k < 16; k++) {
            int skip = pct > 179 ? (k & 1) : pct > 76 ? k % 3 == 2 : 0;
            if (skip) {
                if (np >= 2u) cr_poly(p, np, lw, 0, 0, col);
                np = 0;
                continue;
            }
            if (!np) CR_PT(x0 + W * k / 16, cy + JT[k] * a / 256);
            CR_PT(x0 + W * (k + 1) / 16, cy + JT[k + 1] * a / 256);
        }
        if (np >= 2u) cr_poly(p, np, lw, 0, 0, col);
        break;
    }
    default: break;
    }
#undef CR_PT
}

/* the LFO wave k (CR_LW_*) at t (Q12 of a cycle, 0..4096), -16384..16384 (up +): the designer's lfowave */
static int32_t cr_lfo_y(uint32_t k, int32_t t)
{
    static const int16_t EX[17] = {16384, 7530, 1052, -3687, -7154, -9691, -11547, -12905, -13898, -14625, -15157,
                                   -15546, -15830, -16039, -16191, -16302, -16384};   /* 2 (e^-5t - e^-5) / (1 - e^-5) - 1 */
    static const int16_t RN[8] = {59, 78, -149, -155, -76, 73, -201, 254};           /* the random steps, Q8 */
    switch (k) {
    case CR_LW_TRI: return t < 1024 ? 16 * t : t < 3072 ? 32768 - 16 * t : 16 * t - 65536;
    case CR_LW_SQUARE: return t < 2048 ? 16384 : -16384;
    case CR_LW_SAW: return 8 * t - 16384;
    case CR_LW_RAMP: return 16384 - 8 * t;
    case CR_LW_EXP: {
        int32_t i = t >> 8, f = t & 255;
        return i >= 16 ? EX[16] : EX[i] + (EX[i + 1] - EX[i]) * f / 256;
    }
    case CR_LW_RANDOM: return RN[(t >> 9) > 7 ? 7 : t >> 9] * 64;
    default: return cr_sin((uint32_t)t << 4);
    }
}

/* CR_G_LFOWAVE in the box x, y, w, h (Q4): cyc cycles (1, or 2 in a span-2 cell) of wave k from pct (Q8) of a cycle on,
 * 2 px strokes 2 px inside the box; a 1.5 px tick at the left marks the start. The points: the breakpoints (corners,
 * both sides of a jump) and, for the smooth waves, 16 samples a cycle: <= 37 */
static void cr_lfowave(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t col, int32_t pct, uint32_t k, int32_t cyc)
{
    static const int16_t B_TRI[2] = {1024, 3072}, B_SQ[2] = {0, 2048}, B_0[1] = {0},
                         B_RN[8] = {0, 512, 1024, 1536, 2048, 2560, 3072, 3584};
    int32_t lw = 32, x0 = x + lw, x1 = x + w - lw, cy = y + h / 2, H = h - 2 * lw, wx0 = x0 + 64, WW = x1 - wx0;
    int32_t U = cyc * 4096, ph = pct * 16, n = 0, i, j, c;
    int32_t u[48];
    uint8_t jmp[48];
    const int16_t *B = B_0;
    int32_t nb = 1, smooth = 0, jumps = 1;
    int16_t p[48 * 2];
    uint32_t np = 0;
#define CR_PT2(px16, py16) (p[2 * np] = (int16_t)(px16), p[2 * np + 1] = (int16_t)(py16), np++)
    if (k >= CR_LW_N) k = CR_LW_SINE;
    switch (k) {
    case CR_LW_TRI: B = B_TRI; nb = 2; jumps = 0; break;
    case CR_LW_SQUARE: B = B_SQ; nb = 2; break;
    case CR_LW_RANDOM: B = B_RN; nb = 8; break;
    case CR_LW_SINE: nb = 0; smooth = 1; break;
    case CR_LW_EXP: smooth = 1; break;
    default: break;                                  /* saw, ramp: the jump at 0 */
    }
    u[n] = 0; jmp[n++] = 0;
    u[n] = U; jmp[n++] = 0;
    if (smooth)
        for (i = 1; i < cyc * 16; i++) u[n] = i * 256, jmp[n++] = 0;
    for (c = -1; c <= cyc; c++)
        for (j = 0; j < nb; j++) {
            int32_t v = c * 4096 + B[j] - ph;
            if (v > 0 && v < U && n < 46) u[n] = v, jmp[n++] = (uint8_t)jumps;
        }
    for (i = 1; i < n; i++)                           /* sort by position (a jump after a sample at the same place) */
        for (j = i; j && (u[j - 1] > u[j] || (u[j - 1] == u[j] && jmp[j - 1] > jmp[j])); j--) {
            int32_t t = u[j]; uint8_t q = jmp[j];
            u[j] = u[j - 1]; jmp[j] = jmp[j - 1];
            u[j - 1] = t; jmp[j - 1] = q;
        }
    for (i = 0; i < n && np + 2u <= 48u; i++) {
        int32_t t = (u[i] + ph) & 4095, px = wx0 + u[i] * WW / U;
        if (i + 1 < n && u[i + 1] == u[i] && !jmp[i]) continue;   /* (a sample under a jump: the jump draws it) */
        if (jmp[i] || i == n - 1)                   /* the value from the left (at the end: the cycle's last value) */
            CR_PT2(px, cy - cr_lfo_y(k, t ? t - 1 : 4096) * (H / 2) / 16384);
        if (i < n - 1)
            CR_PT2(px, cy - cr_lfo_y(k, t) * (H / 2) / 16384);
    }
    cr_poly(p, np, lw, 0, 0, col);
    if (np) cr_frect(x0 - 12, p[1] - 64, 24, 128, col);   /* the start tick, 1.5 px, 8 px tall */
#undef CR_PT2
}

/* a cell's glyph in the box x .. x + w, y .. y + h (Q4): the designer's drawGlyph scaled by h / 64 */
static void cr_cglyph(const cr_cell_t *c, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t col)
{
    int32_t sc = h * 4, cwg = w * 4096 / sc, k, pct = c->pct, cx = x + w / 2;   /* sc: glyph units -> Q4 (Q12) */
    int16_t p[26 * 2];
    uint32_t np = 0;
#define CR_GU(v) ((int32_t)(v) * sc >> 12)          /* glyph units (Q4) -> px (Q4) */
#define CR_PT(px16, py16) (p[2 * np] = (int16_t)(px16), p[2 * np + 1] = (int16_t)(py16), np++)
    if (pct >= 255) pct = 256;
    switch (c->glyph) {
    case CR_G_BAR: {
        int32_t bh = 56 * 16 * pct / 256;
        if (c->flags & CR_CF_BIP) {                 /* centre-zero: a well with the fill out of its middle */
            int32_t bw = w * 4 / 10 < 14 * 16 ? w * 4 / 10 : 14 * 16, bx = cx - bw / 2, m = y + h / 2;
            int32_t v = (pct - 128) * h / 256;
            cr_frect(bx, y, bw, h, T_LINE);
            if (v > 0) cr_frect(bx, m - v, bw, v, col);
            else if (v < 0) cr_frect(bx, m, bw, -v, col);
            cr_frect(bx - 48, m - 8, bw + 96, 16, col);
            break;
        }
        for (k = 0; k < 7; k++)
            cr_frect(cx - CR_GU(160), y + CR_GU(64 + k * 56 * 16 / 6), CR_GU(320), 13, T_LINE);
        if (bh > 0) cr_frect(cx - CR_GU(144), y + CR_GU(60 * 16 - bh), CR_GU(288), CR_GU(bh), col);
        break;
    }
    case CR_G_DOTS: {
        int32_t n = 1 + (pct * 11 + 128) / 256, cy = y + h / 2;
        for (k = 0; k < 12; k++) {
            uint32_t a = 49152u + (uint32_t)k * 65536u / 12u;
            cr_disc(cx + CR_GU((320 * cr_cos(a)) >> 14), cy + CR_GU((320 * cr_sin(a)) >> 14), CR_GU(k < n ? 56 : 32),
                    k < n ? col : T_LINE);
        }
        break;
    }
    case CR_G_WAVE: {                                /* two cycles of a sine, its amplitude by pct */
        int32_t amp = CR_GU(22 * 16) * (77 + 179 * pct / 256) / 256, x0 = x + CR_GU(96), ww = w - 2 * CR_GU(96);
        for (k = 0; k <= 20; k++)
            CR_PT(x0 + k * ww / 20, y + h / 2 - ((cr_sin((uint32_t)(k * 65536 / 10)) * amp) >> 14));
        cr_poly(p, np, 32, 0, 0, col);
        break;
    }
    case CR_G_SAW: case CR_G_SQUARE: {               /* two cycles */
        int32_t x0 = x + CR_GU(96), sw = (w - 2 * CR_GU(96)) / 2, top = y + CR_GU(160), bot = y + h - CR_GU(160);
        if (c->glyph == CR_G_SAW) {
            CR_PT(x0, bot);
            for (k = 0; k < 2; k++) {
                CR_PT(x0 + (k + 1) * sw, top);
                CR_PT(x0 + (k + 1) * sw, bot);
            }
        } else {
            for (k = 0; k < 2; k++) {
                int32_t xx = x0 + k * sw, d = sw * pct / 256;
                CR_PT(xx, bot); CR_PT(xx, top); CR_PT(xx + d, top); CR_PT(xx + d, bot); CR_PT(xx + sw, bot);
            }
        }
        cr_poly(p, np, 32, 0, 0, col);
        break;
    }
    case CR_G_MORPH: {                               /* two cycles of the morphed wave: <= 23 segments */
        int32_t m = ((int32_t)c->pct * 127 + 127) / 255, x0 = x + CR_GU(96), cw2 = (w - 2 * CR_GU(96)) / 2;
        int32_t top = y + CR_GU(160), bot = y + h - CR_GU(160), mid = (top + bot) / 2, half = (bot - top) / 2;
        int32_t e = m > 96 ? 4096 - (2048 - 1843 * (m - 96) / 31) : 2048, cyc, j, nk, px, py;
        static const int16_t KX[10] = {0, 512, 1024, 1536, 2048, 2048, 2560, 3072, 3584, 4096};
        static const uint8_t KL[10] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        int16_t kx[12];
        uint8_t kl[12];
        for (j = nk = 0; j < 10; j++) {              /* the eighths, the jump at 1/2 and the pulse's edge e, both sides */
            if (m > 96 && KX[j] > e && (!nk || kx[nk - 1] < e)) {
                kx[nk] = (int16_t)e; kl[nk++] = 1;
                kx[nk] = (int16_t)e; kl[nk++] = 0;
            }
            kx[nk] = KX[j]; kl[nk++] = KL[j];
        }
        for (cyc = 0; cyc < 2; cyc++)
            for (j = 0; j < nk; j++) {
                px = x0 + cyc * cw2 + kx[j] * cw2 / 4096;
                py = mid - cr_morph_y(m, kx[j], kl[j], e) * half / 4096;
                if (np && p[2 * np - 2] == (int16_t)px && p[2 * np - 1] == (int16_t)py)
                    continue;                        /* (no jump here: one point) */
                CR_PT(px, py);
            }
        cr_poly(p, np, 32, 0, 0, col);
        break;
    }
    case CR_G_NOISE: {                               /* white: dense jitter; brown: a wandering line; vinyl: spikes */
        static const int8_t WN[21] = {10, -90, 70, -40, 110, -120, 30, 85, -70, 20, -105, 95, -15, 60, -127, 45, -55,
                                      120, -30, 75, 0};
        static const int8_t BN[11] = {0, 40, 70, 50, 10, -30, -45, -20, 25, 60, 35};
        int32_t x0 = x + CR_GU(96), ww = w - 2 * CR_GU(96), top = y + CR_GU(160), bot = y + h - CR_GU(160);
        int32_t mid = (top + bot) / 2, half = (bot - top) / 2, ty = pct * 3 / 256;
        if (ty == 0) {
            for (k = 0; k < 21; k++) CR_PT(x0 + k * ww / 20, mid - WN[k] * half / 127);
            cr_poly(p, np, 24, 0, 0, col);
        } else if (ty == 1) {
            for (k = 0; k < 11; k++) CR_PT(x0 + k * ww / 10, mid - BN[k] * half / 127);
            cr_poly(p, np, 32, 0, 0, col);
        } else {
            static const int16_t SX[4] = {40, 128, 150, 214};             /* (of 256 across) */
            static const int8_t SY[4] = {110, -70, 127, -100};
            cr_frect(x0, mid - 8, ww, 16, T_LINE);
            for (k = 0; k < 4; k++) {
                int32_t sx = x0 + SX[k] * ww / 256, sy = SY[k] * half / 127;
                cr_frect(sx - 12, sy > 0 ? mid - sy : mid, 24, sy > 0 ? sy : -sy, col);
            }
        }
        break;
    }
    case CR_G_STEPS: {                               /* sample and hold: eight short dashes */
        int32_t sw = (w - 2 * CR_GU(80)) / 8;
        for (k = 0; k < 8; k++)
            cr_frect(x + CR_GU(80) + k * sw + CR_GU(16), y + h - CR_GU(96) - CR_GU(50 * 16 * ((k * 7) % 11) / 11),
                     sw - CR_GU(32), CR_GU(48), k * 256 <= pct * 8 ? col : T_LINE);
        break;
    }
    case CR_G_KNOB: {                                /* a 270-degree arc with its pointer */
        uint32_t a0 = 24576u, sweep = 49152u, a = a0 + sweep * (uint32_t)pct / 256u;
        int32_t r = CR_GU(320), cy = y + h / 2;
        cr_arc(cx, cy, r, 51, a0, sweep, 1, 0, 0, T_LINE);
        if (c->flags & CR_CF_BIP) {                  /* centre-zero: from 12 o'clock to the value, a mark over 12 */
            uint32_t top = 49152u;
            if (a > top) cr_arc(cx, cy, r, 51, top, a - top, 1, 0, 0, col);
            else if (a < top) cr_arc(cx, cy, r, 51, a, top - a, 1, 0, 0, col);
            cr_frect(cx - 13, cy - CR_GU(456), 26, CR_GU(64), col);
        } else if (pct) {
            cr_arc(cx, cy, r, 51, a0, sweep * (uint32_t)pct / 256u, 1, 0, 0, col);
        }
        CR_PT(cx + ((CR_GU(96) * cr_cos(a)) >> 14), cy + ((CR_GU(96) * cr_sin(a)) >> 14));
        CR_PT(cx + ((CR_GU(224) * cr_cos(a)) >> 14), cy + ((CR_GU(224) * cr_sin(a)) >> 14));
        cr_poly(p, np, 38, 0, 0, col);
        break;
    }
    case CR_G_LFOWAVE:                               /* the wave and its start phase (two cycles in a span-2 cell) */
        cr_lfowave(x, y, w, h, col, pct, c->wave, c->flags & CR_CF_SPAN2 ? 2 : 1);
        break;
    case CR_G_RATIO: break;                          /* (the cell draws it: a fraction) */
    default:                                         /* the pictograms: drawn at true size (2 px strokes) in the box */
        if (c->glyph >= CR_G_ROOM && c->glyph < CR_G_N)
            cr_picto(c->glyph, x, y, w, h, col, pct, c->pct2 >= 255 ? 256 : c->pct2);
        break;
    }
    (void)cwg;
#undef CR_PT
#undef CR_GU
}

/* a text cell's pct as a small horizontal bar (Q4): from the left, or out of the middle when bipolar */
static void cr_hbar(const cr_cell_t *c, int32_t x, int32_t w, int32_t by, int32_t bh, uint16_t col)
{
    int32_t pct = c->pct >= 255 ? 256 : c->pct;
    if (!(c->flags & CR_CF_PCT)) return;
    cr_frect(x, by, w, bh, T_LINE);
    if (c->flags & CR_CF_BIP) {
        int32_t m = x + w / 2, v = (pct - 128) * w / 256;
        if (v > 0) cr_frect(m, by, v, bh, col);
        else if (v < 0) cr_frect(m + v, by, -v, bh, col);
        cr_frect(m - 8, by - 32, 16, bh + 64, col);
    } else if (pct) {
        cr_frect(x, by, w * pct / 256, bh, col);
    }
}

/* the value centred at cx (Q8) on baseline vy (Q8) in a cell w px (Q8) wide; hot: on a block of its colour */
static void cr_cvalue(const cr_cell_t *c, int32_t cx, int32_t vy, int32_t w, uint32_t vpx, uint16_t col, int hot,
                      uint32_t flags)
{
    char b[16], v[sizeof c->value];
    int cut = cr_fit(b, sizeof b, c->value, vpx, 1, w - P8(6));
    uint32_t i, j, bpx = vpx;
    if (cut) {                                       /* too wide: "790 ms" -> "790ms" first, then 2 px smaller */
        for (i = j = 0; c->value[i]; i++)
            if (c->value[i] != ' ') v[j++] = c->value[i];
        v[j] = 0;
        cut = cr_fit(b, sizeof b, v, vpx, 1, w - P8(6));
        for (i = vpx; cut && i >= 11u; i -= 2u)      /* ("ENV2+12", the quick mapping's hot cell) */
            if (!cr_fit(b, sizeof b, v, i - 2u, 1, w - P8(6)))
                cut = 0, vpx = i - 2u;
        if (cut)
            cut = cr_fit(b, sizeof b, v, vpx, 1, w - P8(6));
    }
    if (hot) {
        int32_t x0 = (cx - w / 2 + P8(3) + 128) >> 8, x1 = (cx + w / 2 - P8(3) + 128) >> 8, y0 = ((vy + 128) >> 8) - (int32_t)bpx - 1;
        cv_rrect(x0, y0, x1 - x0, (int32_t)bpx + 5, 2, col, T_BG);
    }
    cr_text(cx, vy, b, vpx, 1, CR_C, 4096, hot ? T_BG : col, hot ? col : T_BG, (cut ? 9u : 0u) | flags);
}

/* a number centred at cx (Q8) on baseline vy (Q8) in a cell w (Q8) wide at px: too wide, 2 px smaller at a time (to
 * 7 px), never ellipsised; hot: on a block of its colour */
static void cr_cnum(const char *v, int32_t cx, int32_t vy, int32_t w, uint32_t px, uint16_t col, int hot)
{
    uint32_t bpx = px;
    while (px > 7u && cr_tw(v, px, 1) > w - P8(6)) px -= 2u;
    if (hot) {
        int32_t x0 = (cx - w / 2 + P8(3) + 128) >> 8, x1 = (cx + w / 2 - P8(3) + 128) >> 8, y0 = ((vy + 128) >> 8) - (int32_t)bpx - 1;
        cv_rrect(x0, y0, x1 - x0, (int32_t)bpx + 5, 2, col, T_BG);
    }
    cr_text(cx, vy, v, px, 1, CR_C, 4096, hot ? T_BG : col, hot ? col : T_BG, 0);
}

/* a ratio in quarters (1..255) as "%u.%02u": "0.25", "16.00" */
static void cr_quarters(uint32_t q, char *b)
{
    uint32_t w = q / 4u, f = (q % 4u) * 25u, i = 0;
    if (w >= 10u) b[i++] = (char)('0' + w / 10u);
    b[i++] = (char)('0' + w % 10u);
    b[i++] = '.';
    b[i++] = (char)('0' + f / 10u);
    b[i++] = (char)('0' + f % 10u);
    b[i] = 0;
}

/* CR_G_RATIO: top / bottom (pct / pct2, quarters: QUAD's B2 / B1) as a fraction round mid (Q8), both "%u.%02u":
 * the numerator 13 px over a 1.5 px divider (w - 14 wide) over the denominator 13 px; hot: the whole on a block */
static void cr_cratio(const cr_cell_t *c, int32_t cx, int32_t mid, int32_t w, uint16_t col, int hot)
{
    char nu[8], de[8];
    int32_t dw = w - P8(14);
    uint16_t tc = hot ? T_BG : col;
    cr_quarters(c->pct, nu);
    cr_quarters(c->pct2, de);
    if (hot) {
        int32_t x0 = (cx - w / 2 + P8(3) + 128) >> 8, x1 = (cx + w / 2 - P8(3) + 128) >> 8, y0 = ((mid + 128) >> 8) - 18;
        cv_rrect(x0, y0, x1 - x0, 36, 2, col, T_BG);
    }
    cr_cnum(nu, cx, mid - P8(4), dw + P8(6), 13, tc, 0);
    cr_frect((cx - dw / 2) >> 4, (mid >> 4) - 12, dw >> 4, 24, tc);
    cr_cnum(de, cx, mid + P8(15), dw + P8(6), 13, tc, 0);
}

/* a cell's columns: 2 for a CR_CF_SPAN2 cell starting in column 0..2, else 1 */
static int32_t cr_span(const cr_cell_t *c, int32_t col0) { return (c->flags & CR_CF_ON) && (c->flags & CR_CF_SPAN2) && col0 < 3 ? 2 : 1; }

static void cr_ed_title(const cr_screen_t *s)
{
    int32_t rx = 232, rw;
    if (s->batt != 255u) {                           /* the battery at the right end (the MIX screens) */
        cr_battery(rx - 18, s->batt);
        rx -= 24;
    }
    rw = (s->page[0] ? cr_tw(s->page, 11, 1) : 0) + P8(232 - rx);
    if (s->page[0]) cr_text(P8(rx), P8(17), s->page, 11, 1, CR_R, 4096, T_MID, T_BG, 0);
    if (s->fine) {                                   /* SHIFT on: "fine", small, left of the right text (2: a ratio
                                                      * pair's slow hand B1 steps) */
        const char *ft = s->fine == 2u ? "fine \267 B1" : "fine";
        int32_t fx = P8(232) - rw - (rw ? P8(6) : 0);
        cr_text(fx, P8(16), ft, 9, 1, CR_R, 4096, T_TEXT, T_BG, 0);
        rw += cr_tw(ft, 9, 1) + P8(6);
    }
    if (s->title[0])
        cr_text_fit(P8(8), P8(17), s->title, 13, 1, CR_L, cr_rgb(s->title_col, T_TEXT), T_BG, P8(224) - rw - (rw ? P8(8) : 0));
}

/* log2(v), Q12 (the fraction linear: within 0.09) */
static int32_t cr_log2(uint32_t v)
{
    int32_t e = 31;
    if (!v) return 0;
    while (!(v >> e)) e--;
    return e * 4096 + (int32_t)(((v << (31 - e)) >> 19) & 4095u);
}
/* the 2nd-order response at w = 2^(q / 4) (q quarter octaves from the cutoff), Q = 0.6 + 7 res, of the mix
 * lp LP + bp BP + hp HP (weights Q8; NOTCH = LP + HP): |lp - hp w^2 + j bp g w / Q|^2 / |1 - w^2 + j w / Q|^2, the
 * band-pass made up by g^2 = Q / 0.6 (0 dB at its peak, as before). Its level in dB x 16. 32-bit only: the levels as
 * log2 differences */
static int32_t cr_fdb(int32_t lp, int32_t bp, int32_t hp, int32_t q, int32_t res)
{
    int32_t w2, a, qq = 2458 + res * 7 * 4096 / 255, qq2, inv, term, l, re;
    uint32_t den, num;
    if (q > 14) q = 14;
    if (q < -14) q = -14;
    w2 = q >= 0 ? 256 << (q / 2) : 256 >> (-q / 2);                  /* w^2 = 2^(q / 2), Q8 */
    if (q & 1) w2 = q > 0 ? w2 * 362 / 256 : w2 * 181 / 256;
    a = 256 - w2;
    qq2 = (qq >> 6) * (qq >> 6);                                     /* Q^2, Q12 */
    inv = (int32_t)((1u << 24) / (uint32_t)(qq2 ? qq2 : 1));          /* 1 / Q^2, Q12 */
    term = (w2 * inv) >> 4;                                          /* w^2 / Q^2, Q16 */
    den = (uint32_t)(a * a) + (uint32_t)term;
    re = (lp * 256 - hp * w2) >> 8;                                  /* the real part, Q8 */
    num = (uint32_t)(re * re) + (uint32_t)(((bp * bp) >> 8) * ((term >> 8) * (qq >> 4) / (2458 >> 4)));
    l = cr_log2(num ? num : 1u) - cr_log2(den ? den : 1u);
    return l * 3011 / 256000;                                        /* 10 log10 = 3.0103 log2: dB x 16 */
}

/* QUAD's eight algorithms, the Digitone manual's (Appendix A.3, its diagram; the designer's ALGOS,
 * ../ChoralRootFM1Designer/index.html, the same table): operators C A B1 B2 (0..3); mod: up to four [source, target]
 * arrows; fb: the feedback operator; x / y: the carriers summed into each output (bit per operator), xm / ym the one
 * the label sits under; dir: the direct carriers (full level: a dotted line), the others enveloped (a solid line);
 * side: the carriers whose line leaves the box's right side (an operator below it in its column); rows: 3 or 4;
 * pos: [row 0 top, column 0..2] */
typedef struct { uint8_t nmod, mod[4][2], fb, x, y, xm, ym, dir, side, rows, pos[4][2]; } cr_algo_t;
static const cr_algo_t CR_ALGOS[8] = {
    /* 1: A(fb)>C, B2>B1, B1>C; X C, Y B1 (direct) */
    {3, {{1, 0}, {3, 2}, {2, 0}}, 1, 0x1, 0x4, 0, 2, 0x5, 0, 3, {{2, 0}, {1, 0}, {1, 1}, {0, 1}}},
    /* 2: A>C, B2(fb)>B1; X C, Y B1 (direct) */
    {2, {{1, 0}, {3, 2}}, 3, 0x1, 0x4, 0, 2, 0x5, 0, 3, {{2, 0}, {1, 0}, {2, 1}, {1, 1}}},
    /* 3: A(fb)>C, A>B2, A>B1; X C + B2 (direct), Y B1 (direct) */
    {3, {{1, 0}, {1, 3}, {1, 2}}, 1, 0x9, 0x4, 0, 2, 0xD, 0, 3, {{2, 0}, {1, 1}, {2, 2}, {2, 1}}},
    /* 4: B2(fb)>B1, B1>A, A>C; X C, Y B1 (direct, from its side) */
    {3, {{3, 2}, {2, 1}, {1, 0}}, 3, 0x1, 0x4, 0, 2, 0x5, 0x4, 4, {{3, 0}, {2, 0}, {1, 0}, {0, 0}}},
    /* 5: B1(fb)>A, B2>A, A>C; X C, Y A (direct, from its side) */
    {3, {{2, 1}, {3, 1}, {1, 0}}, 2, 0x1, 0x2, 0, 1, 0x3, 0x2, 3, {{2, 0}, {1, 0}, {0, 0}, {0, 1}}},
    /* 6: A(fb)>C, A>B1, B2>C, B2>B1; X C, Y B1 (direct) */
    {4, {{1, 0}, {1, 2}, {3, 0}, {3, 2}}, 1, 0x1, 0x4, 0, 2, 0x5, 0, 3, {{2, 0}, {1, 0}, {2, 1}, {1, 1}}},
    /* 7: A(fb)>C, B2>B1; X C + A (enveloped, from its side), Y B1 + B2 (enveloped, B2 from its side) */
    {2, {{1, 0}, {3, 2}}, 1, 0x3, 0xC, 0, 2, 0x1, 0xA, 3, {{2, 0}, {1, 0}, {2, 1}, {1, 1}}},
    /* 8: A>C, B1(fb); X C + B2 (enveloped), Y B1 (enveloped) */
    {1, {{1, 0}}, 2, 0x9, 0x4, 0, 2, 0x1, 0, 3, {{2, 0}, {1, 0}, {2, 2}, {2, 1}}},
};
static const char *const CR_OPS[4] = {"C", "A", "B1", "B2"};
#define CR_ALGO_HS (7 * 16)              /* a box's half size (14 px) */
#define CR_ALGO_COL (32 * 16)            /* the column pitch */
#define CR_ALGO_SIDE (15 * 16)           /* a side line: this far right of its box's centre */
#define CR_ALGO_LABY (24 + 92)           /* the X / Y labels' baseline (px) */

/* algorithm n (1..8)'s geometry (Q4): the boxes' centres, the side-line columns, the X / Y labels' x; the diagram
 * centred on x 137 (the band's room right of "ALGO" and its number: 60 .. 214) */
static void cr_algo_geom(uint32_t n, int32_t ox[4], int32_t oy[4], int32_t sx[4], int32_t lx[2])
{
    static const int32_t ROW3[3] = {13, 37, 61}, ROW4[4] = {11, 30, 49, 68};
    const cr_algo_t *al = &CR_ALGOS[(n < 1u ? 1u : n > 8u ? 8u : n) - 1u];
    int32_t mn = 99999, mx = -99999, k, off;
    for (k = 0; k < 4; k++) {
        ox[k] = al->pos[k][1] * CR_ALGO_COL;
        oy[k] = (24 + (al->rows == 4u ? ROW4[al->pos[k][0] & 3u] : ROW3[al->pos[k][0] % 3u])) * 16;
        if (ox[k] - CR_ALGO_HS < mn) mn = ox[k] - CR_ALGO_HS;
        if (ox[k] + (al->side >> k & 1u ? CR_ALGO_SIDE + 16 : CR_ALGO_HS) > mx)
            mx = ox[k] + (al->side >> k & 1u ? CR_ALGO_SIDE + 16 : CR_ALGO_HS);
    }
    off = 137 * 16 - (mn + mx) / 2;
    for (k = 0; k < 4; k++) {
        ox[k] += off;
        sx[k] = al->side >> k & 1u ? ox[k] + CR_ALGO_SIDE : ox[k];
    }
    lx[0] = sx[al->xm];
    lx[1] = sx[al->ym];
}

/* a carrier's line to its output (axis-aligned pieces, 1.5 px; dotted when direct): its main carrier straight down
 * (or from its side, then down) to the label's top; another one down (or from its side, then down) to the label's
 * middle, then across to the label's side */
static void cr_algo_out(int32_t ox, int32_t oy, int32_t sx, int32_t lx, int main_, int dotted, uint16_t c)
{
    const int32_t top = (CR_ALGO_LABY - 10) * 16, midy = (CR_ALGO_LABY - 4) * 16;
    int16_t p[4 * 2];
    uint32_t np = 0;
#define CR_AP(px16, py16) (p[2 * np] = (int16_t)(px16), p[2 * np + 1] = (int16_t)(py16), np++)
    if (sx != ox) {                                  /* from the box's right side */
        CR_AP(ox + CR_ALGO_HS + 16, oy);
        CR_AP(sx, oy);
    } else {
        CR_AP(ox, oy + CR_ALGO_HS + 16);
    }
    if (main_) {
        CR_AP(sx, top);
    } else {
        CR_AP(sx, midy);
        CR_AP(sx > lx ? lx + 6 * 16 : lx - 6 * 16, midy);
    }
#undef CR_AP
    cr_poly(p, np, 24, dotted ? 24 : 0, dotted ? 56 : 0, c);
}

/* the algo band (wv: algo 1..8, fdbk, mix): "ALGO" and the number at the left, the diagram as the manual draws it: the
 * modulation arrows (box edge to box edge, a filled head), each carrier's line to X / Y (dotted: direct, solid:
 * enveloped; a second carrier joins at the label's side; the stronger output by mix in TEXT, the other DIM; both when
 * mix is centred), the four boxes with their letters, the feedback loop over its operator's top left (1..3 px with
 * fdbk). Bounded: <= 4 arrows, 4 lines, 4 boxes, one loop */
static void cr_algo_band(const cr_screen_t *s)
{
    const int32_t hs = CR_ALGO_HS;
    int32_t n = s->wv[0] < 1u ? 1 : s->wv[0] > 8u ? 8 : s->wv[0], fb = s->wv[1], mix = s->wv[2];
    int32_t ox[4], oy[4], sx[4], lx[2], k, o;
    const cr_algo_t *al = &CR_ALGOS[n - 1];
    char num[2] = {(char)('0' + n), 0};
    int16_t p[5 * 2];
    cr_algo_geom((uint32_t)n, ox, oy, sx, lx);
    cr_text(P8(8), P8(24 + 12), "ALGO", 10, 1, CR_L, 4096, T_MID, T_BG, 0);
    cr_text(P8(8), P8(24 + 40), num, 24, 1, CR_L, 4096, T_TEXT, T_BG, 0);
    for (k = 0; k < al->nmod; k++) {                 /* the arrows: 1.5 px, MID, a head <= 4.5 x 5 px at the target */
        int32_t ax = ox[al->mod[k][0]], ay = oy[al->mod[k][0]], tx = ox[al->mod[k][1]], ty = oy[al->mod[k][1]];
        int32_t dx = tx - ax, dy = ty - ay, len = (int32_t)cr_isqrt((uint32_t)(dx * dx + dy * dy)), vx, vy, m, e, bx, by;
        int32_t hl, gap;
        if (!len) continue;
        vx = dx * 4096 / len;                        /* (Q12) */
        vy = dy * 4096 / len;
        m = (vx < 0 ? -vx : vx) > (vy < 0 ? -vy : vy) ? (vx < 0 ? -vx : vx) : (vy < 0 ? -vy : vy);
        e = hs * 4096 / (m ? m : 1) + 16;            /* (Q4) the box edge, +1 px */
        gap = len - 2 * e;
        hl = gap < 72 ? (gap > 32 ? gap : 32) : 72; /* (a short gap: a shorter head) */
        bx = tx - vx * e / 4096;
        by = ty - vy * e / 4096;
        if (gap > hl) {
            p[0] = (int16_t)(ax + vx * e / 4096); p[1] = (int16_t)(ay + vy * e / 4096);
            p[2] = (int16_t)(bx - vx * (hl - 24) / 4096); p[3] = (int16_t)(by - vy * (hl - 24) / 4096);
            cr_poly(p, 2, 24, 0, 0, T_MID);
        }
        cr_tri(bx, by, bx - (vx * hl + vy * 40) / 4096, by - (vy * hl - vx * 40) / 4096,
               bx - (vx * hl - vy * 40) / 4096, by - (vy * hl + vx * 40) / 4096, T_MID);
    }
    for (o = 0; o < 2; o++) {                        /* the outputs */
        uint32_t msk = o ? al->y : al->x, mo = o ? al->ym : al->xm;
        uint16_t c = (o ? mix >= 128 : mix <= 128) ? T_TEXT : T_DIM;
        for (k = 0; k < 4; k++)
            if (msk >> k & 1u)
                cr_algo_out(ox[k], oy[k], sx[k], lx[o], (uint32_t)k == mo, al->dir >> k & 1u, c);
        cr_text(lx[o] * 16, P8(CR_ALGO_LABY), o ? "Y" : "X", 11, 1, CR_C, 4096, c, T_BG, 0);
    }
    for (k = 0; k < 4; k++) {                        /* the boxes: 14 px, a 1.5 px outline, the letter 9 px */
        cr_frect(ox[k] - hs, oy[k] - hs, 2 * hs, 2 * hs, T_BG);
        cr_srect(ox[k] - hs, oy[k] - hs, 2 * hs, 2 * hs, 24, T_TEXT);
        cr_text(ox[k] * 16, oy[k] * 16 + 819, CR_OPS[k], 9, 1, CR_C, 4096, T_TEXT, T_BG, 0);
    }
    {                                                /* the feedback loop: up from the box's top, left past its left
                                                      * edge, down, back in (a head at the edge), as the manual's */
        int32_t j = al->fb, w = 16 + 32 * fb / 255, l = ox[j] - hs, t = oy[j] - hs, ty = t - 3 * 16;
        if (ty < 24 * 16 + w / 2) ty = 24 * 16 + w / 2;
        p[0] = (int16_t)(ox[j] - 2 * 16); p[1] = (int16_t)(t - 16);
        p[2] = (int16_t)(ox[j] - 2 * 16); p[3] = (int16_t)ty;
        p[4] = (int16_t)(l - 4 * 16); p[5] = (int16_t)ty;
        p[6] = (int16_t)(l - 4 * 16); p[7] = (int16_t)(oy[j] - 3 * 16);
        p[8] = (int16_t)(l - 2 * 16); p[9] = (int16_t)(oy[j] - 3 * 16);
        cr_poly(p, 5, w, 0, 0, T_TEXT);
        cr_tri(l, oy[j] - 3 * 16, l - 3 * 16, oy[j] - 6 * 16, l - 3 * 16, oy[j], T_TEXT);
    }
}

/* the ade2 band (wv: A's a d end lev, B's, the lit segment 1..8): A's envelope in x 8..112, B's in 128..232, each a
 * rise to its level over 40 % of its room at a = 1, a fall to its end over 40 % at d = 1, then held; a 4 px level bar
 * at its right (the lit colour when a segment of that half is lit, else MID), its letter top left; the lit segment
 * 4 px in segcol with dots at its ends; a 1 px divider between the halves */
static void cr_ade2_band(const cr_screen_t *s, uint16_t segcol)
{
    const int32_t y0 = 24 * 16, yT = y0 + 18 * 16, yB = y0 + 84 * 16;
    int32_t seg = (int32_t)s->wv[8] - 1, hi;
    cr_frect(8 * 16, yB + 16, 224 * 16, 16, T_LINE);
    for (hi = 0; hi < 2; hi++) {
        const uint8_t *e = s->wv + 4 * hi;
        int32_t hx0 = (hi ? 128 : 8) * 16, hx1 = (hi ? 232 : 112) * 16, barX = hx1 - 64, ex1 = barX - 96, EW = ex1 - hx0;
        int32_t sg = seg >= hi * 4 && seg < hi * 4 + 4 ? seg - hi * 4 : -1;
        int32_t yP = yB - (yB - yT) * e[3] / 255, yE = yB - (yB - yT) * e[2] / 255;
        int32_t xA = hx0 + EW * 2 * e[0] / (5 * 255), xD = xA + EW * 2 * e[1] / (5 * 255);
        int32_t xs[4] = {hx0, xA, xD, ex1}, py[4] = {yB, yP, yE, yE}, i;
        int16_t p[16 * 2];
        uint32_t np = 0, vi[4];
        for (i = 0; i < 4; i++) {                    /* steep runs in pieces <= 26 px tall: small boxes for cr_poly */
            if (i) {
                int32_t dy = py[i] - py[i - 1], m = (dy < 0 ? -dy : dy) / (26 * 16) + 1, k2;
                for (k2 = 1; k2 < m; k2++) {
                    p[2 * np] = (int16_t)(xs[i - 1] + (xs[i] - xs[i - 1]) * k2 / m);
                    p[2 * np + 1] = (int16_t)(py[i - 1] + dy * k2 / m);
                    np++;
                }
            }
            vi[i] = np;
            p[2 * np] = (int16_t)xs[i];
            p[2 * np + 1] = (int16_t)py[i];
            np++;
        }
        cr_poly(p, np, 48, 0, 0, T_TEXT);
        if (sg >= 0 && sg <= 2) {                    /* 0 attack, 1 decay, 2 the hold at the end level */
            cr_poly(p + 2 * vi[sg], vi[sg + 1] - vi[sg] + 1u, 64, 0, 0, segcol);
            cr_disc(xs[sg], py[sg], 56, segcol);
            cr_disc(xs[sg + 1], py[sg + 1], 56, segcol);
        }
        cr_frect(barX, yT, 64, yB - yT, T_LINE);
        if (yB > yP) cr_frect(barX, yP, 64, yB - yP, sg >= 0 ? segcol : T_MID);
        cr_text(hx0 * 16, P8(24 + 10), hi ? "B" : "A", 10, 1, CR_L, 4096, sg >= 0 ? segcol : T_MID, T_BG, 0);
    }
    cr_frect(1912, y0 + 4 * 16, 16, yB - y0 - 4 * 16, T_LINE);
}

/* the wide band: the AHDSR with its lit segment, or the filter's response over a dashed 0 dB line */
static void cr_wide(const cr_screen_t *s, uint16_t segcol)
{
    const int32_t L = 8 * 16, R = 232 * 16, W = R - L, y0 = 24 * 16;
    int16_t p[40 * 2];          /* (filter: <= 7 segments + 4 crossings + 1 = 13 points; cz: <= 9 segments of <= 4) */
    uint32_t np = 0, i;
#define CR_PT(px16, py16) (p[2 * np] = (int16_t)(px16), p[2 * np + 1] = (int16_t)(py16), np++)
    if (s->wide == CR_W_ALGO) {
        cr_algo_band(s);
    } else if (s->wide == CR_W_ADE2) {
        cr_ade2_band(s, segcol);
    } else if (s->wide == CR_W_ENV) {
        static const char *const LET[5] = {"A", "H", "D", "S", "R"};
        int32_t u[5], xs[6], sum = 0, yT = y0 + 8 * 16, yB = y0 + 84 * 16, yS = yB - (yB - yT) * s->wv[3] / 255;
        int32_t seg = (int32_t)s->wv[5] - 1;
        int32_t py[6];
        uint32_t vi[6];
        u[0] = 15 + s->wv[0]; u[1] = s->wv[1]; u[2] = 15 + s->wv[2]; u[3] = 77; u[4] = 15 + s->wv[4];
        for (i = 0; i < 5u; i++) sum += u[i];
        xs[0] = L;
        for (i = 0; i < 5u; i++) xs[i + 1] = xs[i] + u[i] * W / sum;
        py[0] = yB; py[1] = yT; py[2] = yT; py[3] = yS; py[4] = yS; py[5] = yB;
        cr_frect(L, yB + 16, W, 16, T_LINE);
        for (i = 0; i < 6u; i++) {               /* steep runs in pieces <= 26 px tall: small boxes for cr_poly */
            if (i) {
                int32_t dy = py[i] - py[i - 1], m = (dy < 0 ? -dy : dy) / (26 * 16) + 1, k2;
                for (k2 = 1; k2 < m; k2++)
                    CR_PT(xs[i - 1] + (xs[i] - xs[i - 1]) * k2 / m, py[i - 1] + dy * k2 / m);
            }
            vi[i] = np;
            CR_PT(xs[i], py[i]);
        }
        cr_poly(p, np, 48, 0, 0, T_TEXT);
        if (seg >= 0 && seg <= 4) {
            cr_poly(p + 2 * vi[seg], vi[seg + 1] - vi[seg] + 1u, 64, 0, 0, segcol);
            cr_disc(xs[seg], py[seg], 56, segcol);
            cr_disc(xs[seg + 1], py[seg + 1], 56, segcol);
        }
        for (i = 0; i < 5u; i++)
            if (xs[i + 1] - xs[i] >= 16 || (int32_t)i == seg)
                cr_text((xs[i] + xs[i + 1]) * 8, P8(24 + 95), LET[i], 9, 1, CR_C, 4096, (int32_t)i == seg ? segcol : T_DIM,
                        T_BG, 0);
    } else if (s->wide == CR_W_CZ) {
        /* CZ-1's eight-step envelope (wv: R1..R8, L1..L8 0..99, SUS 0..7 / 8 none, END 0..7, the lit step 1..8): from 0
         * to L1 at R1, .. L k at R k, the step END to 0 (the CZ ignores END's level); after the SUS step's point a hold
         * (the key down); the steps after END are not drawn. A step's width grows with its distance and the slowness
         * of its rate (a sketch of the times, as the dx band) */
        static const char *const LET[8] = {"1", "2", "3", "4", "5", "6", "7", "8"};
        int32_t yT = y0 + 8 * 16, yB = y0 + 84 * 16, u[9], xs[10], py[10], sum = 0, lv = 0, d;
        int32_t end = s->wv[17] > 7 ? 7 : s->wv[17], sus = s->wv[16] < end ? s->wv[16] : -1, hot = (int32_t)s->wv[18] - 1;
        int32_t ns = 0, sg[8], k;
        uint32_t vi[10];
        py[0] = yB;
        for (k = 0; k <= end; k++) {             /* segment ns: step k; after the SUS step a hold */
            int32_t r = s->wv[k] > 99 ? 99 : s->wv[k], t = k == end ? 0 : (s->wv[8 + k] > 99 ? 99 : s->wv[8 + k]);
            d = t - lv;
            d = d < 0 ? -d : d;
            sg[k] = ns;
            u[ns] = 6 + (99 - r) * (6 + d) / 24;
            py[ns + 1] = yB - (yB - yT) * t / 99;
            ns++;
            if (k == sus) {
                u[ns] = 30;
                py[ns + 1] = py[ns];
                ns++;
            }
            lv = t;
        }
        for (k = 0; k < ns; k++) sum += u[k];
        xs[0] = L;
        for (k = 0; k < ns; k++) xs[k + 1] = xs[k] + u[k] * W / (sum ? sum : 1);
        cr_frect(L, yB + 16, W, 16, T_LINE);
        for (k = 0; k <= ns; k++) {              /* steep runs in pieces <= 26 px tall: small boxes for cr_poly */
            if (k) {
                int32_t dy = py[k] - py[k - 1], m = (dy < 0 ? -dy : dy) / (26 * 16) + 1, k2;
                for (k2 = 1; k2 < m; k2++)
                    CR_PT(xs[k - 1] + (xs[k] - xs[k - 1]) * k2 / m, py[k - 1] + dy * k2 / m);
            }
            vi[k] = np;
            CR_PT(xs[k], py[k]);
        }
        cr_poly(p, np, 48, 0, 0, T_TEXT);
        if (sus >= 0)                            /* the hold: dashed over its run */
            cr_poly(p + 2 * vi[sg[sus] + 1], vi[sg[sus] + 2] - vi[sg[sus] + 1] + 1u, 48, 32, 64, T_DIM);
        if (hot >= 0 && hot <= end) {            /* step k (R k / L k / SUS / END turned): its segment */
            int32_t a0 = sg[hot], a1 = a0 + 1;
            cr_poly(p + 2 * vi[a0], vi[a1] - vi[a0] + 1u, 64, 0, 0, segcol);
            cr_disc(xs[a0], py[a0], 56, segcol);
            cr_disc(xs[a1], py[a1], 56, segcol);
        }
        for (k = 0; k <= end; k++) {
            int32_t j = sg[k];
            if (xs[j + 1] - xs[j] >= 16 * 16 || k == hot)
                cr_text((xs[j] + xs[j + 1]) * 8, P8(24 + 95), LET[k], 9, 1, CR_C, 4096, k == hot ? segcol : T_DIM,
                        T_BG, 0);
        }
        if (sus >= 0 && xs[sg[sus] + 2] - xs[sg[sus] + 1] >= 16 * 16)
            cr_text((xs[sg[sus] + 1] + xs[sg[sus] + 2]) * 8, P8(24 + 95), "S", 9, 1, CR_C, 4096, T_DIM, T_BG, 0);
    } else if (s->wide == CR_W_DX) {
        /* FM6's DX7 envelope (wv: R1..R4, L1..L4 0..99, the lit segment, the pitch EG flag): from L4 to L1 at R1, L2
         * at R2, L3 at R3, held at L3 (the key down), to L4 at R4; a segment's width grows with its distance and the
         * slowness of its rate (a sketch of the times, not to scale); the pitch EG's levels round a centre line (50) */
        static const char *const LET[4] = {"1", "2", "3", "4"};
        int32_t yT = y0 + 8 * 16, yB = y0 + 84 * 16, lv[5], u[5], xs[6], py[6], sum = 0, d;
        int32_t seg = (int32_t)s->wv[8] - 1, a0, a1;
        uint32_t vi[6], sg[4] = {0, 1, 2, 4};
        lv[0] = s->wv[7] > 99 ? 99 : s->wv[7];
        for (i = 0; i < 4u; i++)
            lv[i + 1] = s->wv[4 + i] > 99 ? 99 : s->wv[4 + i];
        for (i = 0; i < 4u; i++) {                /* R1 R2 R3 (segments 0..2), R4 (segment 4); 3 is the hold */
            uint32_t j = sg[i];
            int32_t r = s->wv[i] > 99 ? 99 : s->wv[i];
            d = i < 3u ? lv[i + 1] - lv[i] : lv[0] - lv[3];
            d = d < 0 ? -d : d;
            u[j] = 6 + (99 - r) * (6 + d) / 24;
        }
        u[3] = 36;
        for (i = 0; i < 5u; i++) sum += u[i];
        xs[0] = L;
        for (i = 0; i < 5u; i++) xs[i + 1] = xs[i] + u[i] * W / sum;
        for (i = 0; i < 4u; i++) py[i] = yB - (yB - yT) * lv[i] / 99;
        py[4] = py[3];
        py[5] = py[0];
        cr_frect(L, yB + 16, W, 16, T_LINE);
        if (s->wv[9])                             /* the pitch EG: its centre (no pitch change) */
            cr_frect(L, yB - (yB - yT) * 50 / 99, W, 16, T_LINE);
        for (i = 0; i < 6u; i++) {               /* steep runs in pieces <= 26 px tall: small boxes for cr_poly */
            if (i) {
                int32_t dy = py[i] - py[i - 1], m = (dy < 0 ? -dy : dy) / (26 * 16) + 1, k2;
                for (k2 = 1; k2 < m; k2++)
                    CR_PT(xs[i - 1] + (xs[i] - xs[i - 1]) * k2 / m, py[i - 1] + dy * k2 / m);
            }
            vi[i] = np;
            CR_PT(xs[i], py[i]);
        }
        cr_poly(p, np, 48, 0, 0, T_TEXT);
        if (seg >= 0 && seg <= 3) {               /* segment k (R k / L k turned): the one ending at L k */
            a0 = (int32_t)sg[seg];
            a1 = a0 + 1;
            cr_poly(p + 2 * vi[a0], vi[a1] - vi[a0] + 1u, 64, 0, 0, segcol);
            cr_disc(xs[a0], py[a0], 56, segcol);
            cr_disc(xs[a1], py[a1], 56, segcol);
        }
        for (i = 0; i < 4u; i++) {
            uint32_t j = sg[i];
            if (xs[j + 1] - xs[j] >= 16 || (int32_t)i == seg)
                cr_text((xs[j] + xs[j + 1]) * 8, P8(24 + 95), LET[i], 9, 1, CR_C, 4096, (int32_t)i == seg ? segcol : T_DIM,
                        T_BG, 0);
        }
    } else if (s->wide == CR_W_FILTER) {
        /* FTYPE: the weights of LP BP HP NOTCH (= LP + HP) at its position, crossfaded round the cycle */
        static const uint8_t TW[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}};
        /* the response at quarter-octave offsets from the cutoff, the edges extrapolated; 1.4 px a dB round the
         * 0 dB line, clamped to the band with the crossings: <= 13 points */
        static const int8_t Q[7] = {-12, -4, -1, 0, 1, 4, 12};
        int32_t cut = s->wv[0], res = s->wv[1], ft = s->wv[2] & 127, dr = s->wv[3], y0db = y0 + 38 * 16;
        int32_t sg = ft >> 5, fr = ft & 31, wt[3];
        int32_t ymin = y0 + 4 * 16, ymax = y0 + 90 * 16, cx = L + W * cut / 255, xs[9], ys[9], n = 0, k, j;
        for (k = 0; k < 3; k++)
            wt[k] = (TW[sg][k] * (32 - fr) + TW[(sg + 1) & 3][k] * fr) * 8;   /* (Q8) */
        for (k = 0; k < 7; k++) {
            int32_t db = s->wv[7] == 1u ? 0 : cr_fdb(wt[0], wt[1], wt[2], Q[k], res);   /* (OFF: flat) */
            xs[n + 1] = cx + Q[k] * W / 36;                          /* (9 octaves across W) */
            if (s->wv[7] == 3u) db *= 2;                             /* (24 dB: two 12 dB stages, twice the slope) */
            ys[n + 1] = y0db - (db + dr * 96 / 255) * 14 / 10;
            n++;
        }
        xs[0] = L < xs[1] ? L : xs[1] - 16;                          /* the edges, on the outer lines */
        ys[0] = ys[1] + (ys[2] - ys[1]) * (xs[0] - xs[1]) / (xs[2] - xs[1]);
        xs[8] = R > xs[7] ? R : xs[7] + 16;
        ys[8] = ys[7] + (ys[7] - ys[6]) * (xs[8] - xs[7]) / (xs[7] - xs[6]);
        for (k = 0; k < 8; k++) {
            int32_t x0 = xs[k], x1 = xs[k + 1], ya, yb, xa, xb, t[2], nt = 0, lim[2];
            if (x1 <= L || x0 >= R || x1 <= x0) continue;
            xa = x0 < L ? L : x0;
            xb = x1 > R ? R : x1;
            ya = ys[k] + (ys[k + 1] - ys[k]) * (xa - x0) / (x1 - x0);
            yb = ys[k] + (ys[k + 1] - ys[k]) * (xb - x0) / (x1 - x0);
            if (!np) CR_PT(xa, ya < ymin ? ymin : ya > ymax ? ymax : ya);
            for (j = 0; j < 2; j++) {                                /* the band's limits crossed on the way */
                int32_t y = j ? ymax : ymin;
                if ((ya - y) * (yb - y) < 0) {
                    t[nt] = xa + (xb - xa) * (y - ya) / (yb - ya);
                    lim[nt++] = y;
                }
            }
            if (nt == 2 && t[0] > t[1]) {
                int32_t u = t[0]; t[0] = t[1]; t[1] = u;
                u = lim[0]; lim[0] = lim[1]; lim[1] = u;
            }
            for (j = 0; j < nt; j++) CR_PT(t[j], lim[j]);
            yb = yb < ymin ? ymin : yb > ymax ? ymax : yb;
            if (np >= 2 && p[2 * np - 1] == yb && p[2 * np - 3] == yb)
                p[2 * np - 2] = (int16_t)xb;                         /* (a run along a limit: one segment) */
            else
                CR_PT(xb, yb);
        }
        if (s->wv[4]) {                              /* QUAD's base-width window: a dim region (the orange at 12 % over
                                                      * the background: the designer's alpha, as a solid colour) */
            int32_t b = s->wv[5], e = b + s->wv[6] > 255 ? 255 : b + s->wv[6], bx = L + W * b / 255, ex = L + W * e / 255;
            if (ex > bx) cr_frect(bx, ymin, ex - bx, ymax - ymin, ux_mix(T_BG, CR_NAMED[CR_COL_ORANGE], 12));
        }
        {
            int16_t d[4] = {(int16_t)L, (int16_t)y0db, (int16_t)R, (int16_t)y0db};
            cr_poly(d, 2, 16, 48, 112, T_MID);
        }
        if (s->wv[4]) {                              /* .. its edges dashed 1.5 px (3 on, 3 off), B and W under them */
            int32_t b = s->wv[5], e = b + s->wv[6] > 255 ? 255 : b + s->wv[6], bx = L + W * b / 255, ex = L + W * e / 255;
            int32_t close = ex - bx < 12 * 16, xx, yy, k2;
            uint16_t oc = CR_NAMED[CR_COL_ORANGE];
            for (k2 = 0; k2 < 2; k2++)
                for (xx = k2 ? ex : bx, yy = ymin; yy < ymax; yy += 96)
                    cr_frect(xx - 12, yy, 24, ymax - yy < 48 ? ymax - yy : 48, oc);
            xx = bx - (close ? 80 : 0);
            cr_text((xx > L + 48 ? xx : L + 48) * 16, P8(24 + 96), "B", 9, 1, CR_C, 4096, oc, T_BG, 0);
            xx = ex + (close ? 80 : 0);
            cr_text((xx < R - 64 ? xx : R - 64) * 16, P8(24 + 96), "W", 9, 1, CR_C, 4096, oc, T_BG, 0);
        }
        cr_poly(p, np, 48, 0, 0, CR_NAMED[CR_COL_ORANGE]);
        {                                            /* the type: the VA's position name; QUAD's (wv[7]) OFF, the
                                                      * name and its slope "LP12" "HP12" "LP24" */
            char tn[8];
            const char *nm = s->wv[7] == 1u ? "OFF" : cr_ftype_name((uint32_t)ft);
            uint32_t i2 = 0;
            while (nm[i2] && i2 < 5u) { tn[i2] = nm[i2]; i2++; }
            if (s->wv[7] >= 2u) { tn[i2++] = s->wv[7] == 3u ? '2' : '1'; tn[i2++] = s->wv[7] == 3u ? '4' : '2'; }
            tn[i2] = 0;
            cr_text(P8(8), P8(24 + 12), tn, 10, 1, CR_L, 4096, T_MID, T_BG, 0);
        }
        if (dr) {
            char b[12] = "DRIVE ";
            int32_t v = (dr * 100 + 127) / 255, i2 = 6;
            if (v >= 100) b[i2++] = '1';
            if (v >= 10) b[i2++] = (char)('0' + v / 10 % 10);
            b[i2++] = (char)('0' + v % 10);
            b[i2] = 0;
            cr_text(P8(232), P8(24 + 12), b, 10, 1, CR_R, 4096, T_MID, T_BG, 0);
        }
    }
#undef CR_PT
}

static uint16_t cr_ccol(uint32_t ci, int on) { return on ? CR_NAMED[CR_KC[ci & 3u]] : T_DIM; }
/* rows y0 .. y1 - 1 (px) cross the strip being drawn: else what lies wholly in them is not even measured */
static int cr_in_strip(int32_t y0, int32_t y1) { return y1 > cr_row0() && y0 < cr_row1(); }
/* the hot cell's block: the knob's colour, or the modulation source's while mapping */
static uint16_t cr_hcol(const cr_screen_t *s, uint16_t col) { return s->hot_col ? cr_rgb(s->hot_col, col) : col; }
/* a cell's modulation mark: a 4 px square, its top right corner at (x1, y0) px */
static void cr_mark(const cr_cell_t *c, int32_t x1, int32_t y0)
{
    uint32_t m = CR_CF_MARKCOL(c->flags);
    if (m) cr_fill(x1 - 4, y0, 4, 4, cr_rgb(m, CR_WHITE));
}

/* edit8 `tall`: one row of four level bars over the panel (the oscillator mixer): the label, a tall well filled from
 * the bottom, the value; the knob bars under them */
static void cr_p_tall(const cr_screen_t *s)
{
    int32_t ci;
    for (ci = 0; ci < 4; ci++) {
        const cr_cell_t *c = &s->cell[0][ci];
        int32_t x = ci * 60, cx = P8(x + 30), top = 56, h = 148, f;
        uint16_t col = cr_ccol((uint32_t)ci, s->active == 0u);
        int hot = s->hot_r == 1u && s->hot_c == ci;
        if (!(c->flags & CR_CF_ON)) continue;
        if (c->label[0]) {
            char b[16];
            int cut = cr_fit(b, sizeof b, c->label, 12, 1, P8(56));
            cr_text(cx, P8(46), b, 12, 1, CR_C, 4096, col, T_BG, cut ? 9u : 0u);
        }
        cr_mark(c, x + 56, 32);
        f = h * (c->pct >= 255 ? 256 : c->pct) / 256;
        cr_fill(x + 18, top, 24, h - f, T_LINE);
        if (f > 0) cr_fill(x + 18, top + h - f, 24, f, col);
        cr_cvalue(c, cx, P8(226), P8(60), 17, hot ? cr_hcol(s, col) : col, hot, 0);
    }
    if (s->active == 0u)
        for (ci = 0; ci < 4; ci++)
            if (s->cell[0][ci].flags & CR_CF_ON)
                cr_fill(ci * 60 + 4, 236, 52, 2, cr_ccol((uint32_t)ci, 1));
}

static void cr_p_edit8(const cr_screen_t *s)
{
    int wide = s->wide != CR_W_NONE, big = !wide;
    int32_t rowy[2], rowh = wide ? 56 : 100, ri, ci;
    uint32_t lpx = big ? 12u : 10u, vpx = big ? 17u : 13u;
    int32_t gh = big ? 34 : 22, ly = big ? 16 : 11, gyo = big ? 26 : 15, vyo = big ? 82 : 51;
    rowy[0] = wide ? 124 : 30;
    rowy[1] = wide ? 184 : 134;
    if (cr_in_strip(0, 26)) cr_ed_title(s);
    if (s->tall) {
        cr_p_tall(s);
        return;
    }
    if (wide && cr_in_strip(22, 124)) {
        uint32_t lit = s->wide == CR_W_DX || s->wide == CR_W_ADE2 ? s->wv[8] : s->wide == CR_W_CZ ? s->wv[18] : s->wv[5];
        uint16_t sc = s->hot_r ? cr_ccol(s->hot_c, 1) : CR_NAMED[CR_KC[(lit ? lit - 1u : 0u) % 4u]];
        cr_wide(s, sc);
    }
    for (ri = 0; ri < (int32_t)s->n_rows && ri < 2; ri++) {
        int on = ri == s->active, y = rowy[ri];
        int32_t col0 = 0, mid = y + (gyo + vyo) / 2;                 /* (mid: the glyph and value's space) */
        if (!cr_in_strip(y - 2, y + rowh + 2)) continue;
        for (ci = 0; ci < 4 && col0 < 4; ci++) {
            const cr_cell_t *c = &s->cell[ri][ci];
            int32_t sp = cr_span(c, col0), x = col0 * 60, w = 60 * sp, cx = P8(x) + P8(w) / 2;
            uint16_t col = cr_ccol((uint32_t)col0, on), vcol;
            int hot = s->hot_r == ri + 1 && s->hot_c == ci, ratio, bigv;
            col0 += sp;
            if (!(c->flags & CR_CF_ON)) continue;
            vcol = hot ? cr_hcol(s, col) : col;
            ratio = c->glyph == CR_G_RATIO && c->pct && c->pct2;
            bigv = !ratio && ((c->flags & CR_CF_BIG) || c->glyph == CR_G_RATIO);
            if (c->label[0]) {
                char b[16];
                int cut = cr_fit(b, sizeof b, c->label, lpx, 1, CR_CF_MARKCOL(c->flags) && !big ? P8(w - 14) : P8(w - 4));
                cr_text(cx, P8(y + ly), b, lpx, 1, CR_C, 4096, col, T_BG, cut ? 9u : 0u);
            }
            cr_mark(c, x + w - 2, y + 1);
            if (ratio) {
                cr_cratio(c, cx, P8(mid), P8(w), vcol, hot);
            } else if (bigv) {                                       /* the value 7 px larger, centred in the space */
                cr_cnum(c->value, cx, P8(mid) + (int32_t)(vpx + 7u) * 92, P8(w), vpx + 7u, vcol, hot);
            } else {
                if (c->glyph != CR_G_NONE) cr_cglyph(c, (x + 30 * sp - 25 * sp) * 16, (y + gyo) * 16, 50 * sp * 16, gh * 16, col);
                else cr_hbar(c, (x + 8) * 16, (w - 16) * 16, (y + gyo + gh / 2 - 2) * 16, 64, col);
                cr_cvalue(c, cx, P8(y + vyo), P8(w), vpx, vcol, hot, 0);
            }
        }
    }
    if (s->active < s->n_rows && s->active < 2u) {               /* the active row's bars (a span's: both columns) */
        int32_t col0 = 0;
        for (ci = 0; ci < 4 && col0 < 4; ci++) {
            const cr_cell_t *c = &s->cell[s->active][ci];
            int32_t sp = cr_span(c, col0);
            if (c->flags & CR_CF_ON)
                cr_fill(col0 * 60 + 4, rowy[s->active] + rowh - 2, 60 * sp - 8, 2, cr_ccol((uint32_t)col0, 1));
            col0 += sp;
        }
    }
}

static void cr_p_stack(const cr_screen_t *s)
{
    uint32_t n = s->n_rows ? (s->n_rows > CR_ED_ROWS ? CR_ED_ROWS : s->n_rows) : 1u;
    int32_t labw = 0, lw, cw, r0 = P8(41), rh = P8(198) / (int32_t)n, ri, ci;
    int compact = rh < P8(30);
    for (ri = 0; ri < (int32_t)n; ri++)
        if (s->rlabel[ri][0]) {
            int32_t w = cr_tw(s->rlabel[ri], 11, 1);
            labw = w > labw ? w : labw;
        }
    lw = labw ? ((labw + 255) & ~255) + P8(12) : 0;
    lw = lw > P8(44) ? P8(44) : lw;
    cw = (P8(240) - lw) / 4;
    if (cr_in_strip(0, 26)) cr_ed_title(s);
    for (ci = 0; ci < 4 && cr_in_strip(24, 41); ci++)
        if (s->head[ci][0]) {
            char b[16];
            int cut = cr_fit(b, sizeof b, s->head[ci], 10, 1, cw - P8(4));
            cr_text(lw + ci * cw + cw / 2, P8(36), b, 10, 1, CR_C, 4096, T_DIM, T_BG, cut ? 9u : 0u);
        }
    for (ri = 0; ri < (int32_t)n; ri++) {
        int32_t y = r0 + ri * rh;                                    /* Q8 */
        int on = ri == s->active;
        if (!cr_in_strip((y >> 8) - 2, ((y + rh) >> 8) + 2)) continue;
        if (ri) cr_frect(4 * 16, (y >> 4) - 8, 232 * 16, 16, T_LINE);
        if (s->rlabel[ri][0])
            cr_text_fit(P8(6), y + rh / 2 + P8(4), s->rlabel[ri], 11, 1, CR_L, on ? T_TEXT : T_DIM, T_BG, lw - P8(6));
        int32_t col0 = 0;
        for (ci = 0; ci < 4 && col0 < 4; ci++) {
            const cr_cell_t *c = &s->cell[ri][ci];
            int32_t sp = cr_span(c, col0), x = lw + col0 * cw, cw1 = cw, cx;
            uint16_t col = cr_ccol((uint32_t)col0, on), vcol;
            int hot = s->hot_r == ri + 1 && s->hot_c == ci;
            int32_t cw = cw1 * sp;                                   /* (this cell's width: a span's two columns) */
            col0 += sp;
            cx = x + cw / 2;
            if (!(c->flags & CR_CF_ON)) continue;
            vcol = hot ? cr_hcol(s, col) : col;
            cr_mark(c, (x + cw) >> 8, (y >> 8) + 2);
            if (compact) {                                           /* ~24 px rows (the mod matrix): text, a thin bar */
                cr_cvalue(c, cx, y + P8(14), cw, 12, vcol, hot, 0);
                cr_hbar(c, (x + P8(6)) >> 4, (cw - P8(12)) >> 4, (y + P8(17) + 128) >> 4, 32, col);
            } else if (c->glyph != CR_G_NONE && c->glyph != CR_G_BAR && c->glyph != CR_G_RATIO) {   /* a glyph over its value */
                int showv = rh >= P8(44) && c->value[0];
                int32_t gh = showv ? rh - P8(24) : rh - P8(10);
                if (gh > P8(40)) gh = P8(40);
                cr_cglyph(c, (x + P8(6)) >> 4, (y + P8(4) + (showv ? 0 : (rh - P8(10) - gh) / 2)) >> 4, (cw - P8(12)) >> 4,
                          gh >> 4, col);
                if (showv)                                           /* (a long name, "SAW>RMP": 9 px) */
                    cr_cvalue(c, cx, y + P8(4) + gh + P8(14), cw, cr_tw(c->value, 11, 1) > cw - P8(6) ? 9u : 11u, vcol, hot,
                              0);
            } else {                                                 /* a value with its bar under it */
                uint32_t vpx = rh >= P8(45) ? 15u : 13u;
                int32_t vy = y + rh / 2 + P8(vpx) / 2 - P8(3);
                cr_cvalue(c, cx, vy, cw, vpx, vcol, hot, 0);
                cr_hbar(c, (x + P8(6)) >> 4, (cw - P8(12)) >> 4, (vy + P8(5)) >> 4, 48, col);
            }
        }
    }
    if (s->active < n) {                                             /* the active row's bars */
        int32_t col0 = 0;
        for (ci = 0; ci < 4 && col0 < 4; ci++) {
            const cr_cell_t *c = &s->cell[s->active][ci];
            int32_t sp = cr_span(c, col0), x = lw + col0 * cw, y = r0 + (int32_t)s->active * rh + rh - P8(3);
            if (c->flags & CR_CF_ON) cr_frect((x + P8(4)) >> 4, y >> 4, (cw * sp - P8(8)) >> 4, 32, cr_ccol((uint32_t)col0, 1));
            col0 += sp;
        }
    }
}

/* CR_K_KNOBROW (FORMAT.md "knobrow"): a layer screen. The panel above the lowest 72 px is a horizontal picker band
 * (the item 34 px bold in col squeezed to 170 px, its neighbours 13 px dim at the edges, the square marks under it,
 * `label` 11 px dim top left, `value` 13 px bold under the marks); the 72 px are one row of four knob cells,
 * edit8's look made taller: the label 10 px (+11), a 48 x 36 glyph box (+15), the value 13 px bold (+66), the 2 px
 * knob-colour bar (+70). A cell with pct and no glyph draws the bar glyph; a cell not CR_CF_ON a dim dash, a
 * CR_CF_DIM cell all in the grey. kr_band 1: the band is cr_keyboard's strip (KEY) under `label`. The band
 * slides (CR_A_SLIDE) as a picker's; the row has no motion (the hot cell: the value on a block of its colour). */
#define CR_KR_ROW 72
static int32_t cr_kr_rowy(const cr_screen_t *s) { return CR_PY0 + cr_ph(s) - CR_KR_ROW; }

static void cr_p_knobrow(const cr_screen_t *s, const cr_frame_t *fr, int32_t ph)
{
    int32_t n = s->n_items, sel = s->sel < n ? s->sel : n - 1, size = s->size ? s->size : 34, ry = cr_kr_rowy(s);
    int32_t bandh = P8(ph - CR_KR_ROW), bigh = size * 282, labelh = s->label[0] ? P8(14) : 0;   /* Q8 */
    int32_t stack = bigh + P8(10) + (s->value[0] ? P8(18) : 0), top = P8(CR_PY0) + labelh + (bandh - labelh - stack) / 2;
    int32_t base = top + bigh * 4 / 5, marks = (top + bigh + P8(4) + 128) >> 8, ci;
    uint16_t col = cr_rgb(s->col, T_TEXT);
    if (cr_in_strip(CR_PY0, ry)) {                   /* the band */
        if (s->label[0]) cr_text_fit(P8(8), P8(CR_PY0 + 12), s->label, 11, 0, CR_L, T_DIM, T_BG, P8(224));
        if (s->kr_band == 1u) {                      /* the keyboard (KEY): the 224 px strip, <= 56 px tall, centred */
            int32_t avail = ry - CR_PY0 - 18 - 6, kh = avail < 56 ? avail : 56;
            cr_keyboard(s, CR_PY0 + 18 + (avail - kh) / 2, kh);
        } else if (n > 0) {
            int32_t bw = cr_tw(cr_item(s, sel), (uint32_t)size, 1);
            if (bw > P8(170)) bw = P8(170);
            cr_pick_sides(s, n, sel, base, 6, 234, (P8(228) - bw) / 2 - P8(10));
            cr_pick_item(s, fr, sel, size, 170, 1, base, top, top + bigh, col);
            cr_pick_marks(n, sel, marks, col);
        }
        if (s->value[0] && s->kr_band != 1u) cr_text_fit(P8(120), P8(marks + 19), s->value, 13, 1, CR_C, col, T_BG, P8(224));
    }
    if (!cr_in_strip(ry - 2, ry + CR_KR_ROW + 2)) return;
    for (ci = 0; ci < 4; ci++) {                     /* the knobs' cells */
        cr_cell_t c = s->cell[0][ci];
        int32_t x = ci * 60, cx = P8(x + 30);
        uint16_t kc = (c.flags & CR_CF_DIM) ? cr_rgb(CR_COL_GREY, T_DIM) : cr_ccol((uint32_t)ci, 1);   /* dim: grey */
        int hot = s->hot_r == 1u && s->hot_c == ci;
        if (!(c.flags & CR_CF_ON)) {                 /* no parameter: a dim dash where the value would be */
            cr_frect((x + 26) * 16 + 8, (ry + 61) * 16, 7 * 16, 32, T_DIM);
            continue;
        }
        if (c.label[0]) {
            char b[16];
            int cut = cr_fit(b, sizeof b, c.label, 10, 1, CR_CF_MARKCOL(c.flags) ? P8(46) : P8(56));
            cr_text(cx, P8(ry + 11), b, 10, 1, CR_C, 4096, kc, T_BG, cut ? 9u : 0u);
        }
        cr_mark(&c, x + 58, ry + 1);
        if (c.glyph == CR_G_NONE && (c.flags & CR_CF_PCT)) c.glyph = CR_G_BAR;
        if (c.glyph != CR_G_NONE) cr_cglyph(&c, (x + 6) * 16, (ry + 15) * 16, 48 * 16, 36 * 16, kc);
        cr_cvalue(&c, cx, P8(ry + 66), P8(60), 13, hot ? cr_hcol(s, kc) : kc, hot, 0);
        cr_fill(x + 4, ry + CR_KR_ROW - 2, 52, 2, kc);
    }
}

static void cr_p_geek(const cr_screen_t *s, int32_t ph)
{
    uint32_t i;
    cr_name(&s->name, P8(70), P8(CR_PY0 + 56), 40, 224, 4096);
    for (i = 0; i < s->n_notes; i++)
        cr_text(P8(230), P8(CR_PY0 + 30 + 14 * (int32_t)i), s->note[i].t, 12, 1, CR_R, 4096,
                cr_rgb(s->note[i].col, CR_WHITE), T_BG, 0);
    for (i = 0; i < s->n_lines && i < 3u; i++)    /* (the third: the audio ISR's load, cr_ui.c) */
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
    int32_t lift = s->ring_on ? 22 : 0;  /* the ring's band: the lines under the value stay inside it */
    uint16_t under = s->block ? cr_rgb(s->block, CR_RED) : T_BG;
    if (s->block) cr_fill(0, CR_PY0, 240, ph, under);
    cr_text(P8(120), vy - P8(lift / 2), s->value, size, 1, CR_C, sx, s->block ? T_BG : cr_rgb(s->col, CR_WHITE), under, 0);
    if (s->sub[0]) cr_text(P8(120), P8(CR_PY0 + ph - 26 - lift), s->sub, 15, 1, CR_C, 4096, s->block ? T_BG : T_TEXT, under, 0);
    if (s->label[0]) cr_text(P8(120), P8(CR_PY0 + ph - 8 - lift), s->label, 14, 0, CR_C, 4096, s->block ? T_BG : T_MID, under, 0);
    if (s->title[0]) cr_text(P8(10), P8(CR_PY0 + 14), s->title, 12, 1, CR_L, 4096, T_MID, under, 0);
}

/* the scope: the master output as one bold line (3 px, white) over a thin grey centre line. Cheap: a column at a
 * time, the span the polyline covers within a pixel either side (its neighbours' samples) +- 1.5 px, one
 * fractional rectangle per column, only for the columns crossing the strip's rows */
static void cr_p_scope(const cr_screen_t *s, int32_t ph)
{
    int32_t cy = (CR_PY0 + ph / 2) * 16 + 8, amp = (ph / 2 - 6) * 16, r0 = cr_row0() * 16, r1 = cr_row1() * 16, x;
    int32_t prev, cur, next;
    cr_fill(0, CR_PY0 + ph / 2, 240, 1, CR_NAMED[CR_COL_GREY]);
    cur = cy - s->wave[0] * amp / 127;
    prev = cur;
    for (x = 0; x < (int32_t)CR_WAVE_N; x++) {
        int32_t lo, hi;
        next = x + 1 < (int32_t)CR_WAVE_N ? cy - s->wave[x + 1] * amp / 127 : cur;
        lo = cur < prev ? cur : prev;
        lo = next < lo ? next : lo;
        hi = cur > prev ? cur : prev;
        hi = next > hi ? next : hi;
        lo -= 24;
        hi += 24;
        if (hi > r0 && lo < r1)
            cr_frect(x * 16, lo, 16, hi - lo, cr_rgb(s->col, CR_WHITE));
        prev = cur;
        cur = next;
    }
}

/* ------------------------------------------------------------ ring --- */
/* Orchid's ring: a dotted circle round the edge (cr_arc width 5, dash 2 of 6 at R 113, T_LINE) and the progress
 * over it (the same band solid, clockwise from 12 o'clock). cr_ui.c sets it only for the count-in, the undo screen,
 * the capture states (armed, recording, overdubbing) and calibration; a loop merely playing is the corner dial. Only its colour and fraction change, so the coverage
 * of every pixel of the band is computed once (cr_arc_px, the first time a ring is drawn) into the POOL: per row up
 * to two runs of pixels, a byte a pixel (the solid band's and the dotted circle's sample counts, d <= s, as one
 * index of the 153 pairs). A frame blends the runs of its rows from the table; the progress is decided per run
 * from its end pixels' angles (the angle is monotonic along a row): the runs well inside or outside the sweep take
 * the band's coverage or none, and only the pixels within 64 / 65536 turn of its start or tip (more than a sample's
 * angle can differ from its pixel's) take cr_arc_px's samples. The pixels are cr_arc's (tests/run_cr_draw.sh). */
#define CR_RING_C (120 * 16)
#define CR_RING_R (113 * 16)
#define CR_RING_W (5 * 16)
#define CR_RING_TOP 49152u                /* 12 o'clock */
#define CR_RING_MAX 4352u                 /* the band's pixels (4 232: tests/cr_draw_test.c) */
static struct {
    uint16_t off[241];                    /* row j's pixels: cov[off[j] .. off[j + 1]) */
    uint8_t x0[240][2], n[240][2];        /* its runs */
    uint8_t ps[153], pd[153];             /* a pair's solid and dotted sample counts */
    uint8_t cov[CR_RING_MAX];
    uint8_t state;                        /* 0 not built, 1 built, 2 did not fit (cr_arc then) */
} cr_ring __attribute__((section(".pool")));      /* (zero-initialised) */

static void cr_ring_build(void)
{
    cr_arcg_t gs, gd;
    uint32_t k = 0, q = 0, s, d;
    int32_t i, j;
    cr_arc_geom(&gs, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 0, 0);
    cr_arc_geom(&gd, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 2 * 16, 6 * 16);
    for (s = 0; s <= 16u; s++)
        for (d = 0; d <= s; d++) {
            cr_ring.ps[q] = (uint8_t)s;
            cr_ring.pd[q++] = (uint8_t)d;
        }
    cr_ring.state = 2;
    for (j = 0; j < 240; j++) {
        int runs = 0, open = 0;
        cr_ring.off[j] = (uint16_t)k;
        cr_ring.n[j][0] = cr_ring.n[j][1] = 0;
        for (i = 0; i < 240; i++) {
            s = cr_arc_px(&gs, i, j);
            if (!s) {
                open = 0;
                continue;
            }
            if (!open) {
                if (runs == 2 || k >= CR_RING_MAX) return;
                cr_ring.x0[j][runs++] = (uint8_t)i;
                open = 1;
            }
            if (k >= CR_RING_MAX) return;
            d = cr_arc_px(&gd, i, j);
            cr_ring.cov[k++] = (uint8_t)(s * (s + 1u) / 2u + d);
            cr_ring.n[j][runs - 1]++;
        }
    }
    cr_ring.off[240] = (uint16_t)k;
    cr_ring.state = 1;
}

/* the progress over a run of row j from x0 to x1: 0 none, 1 the band's coverage, 2 pixel by pixel */
static int cr_ring_run(int32_t j, int32_t x0, int32_t x1, uint32_t sweep)
{
    int32_t pdy = j * 16 + 8 - CR_RING_C, rf, e, lo, hi;
    uint32_t af, al;
    if (sweep >= 65536u) return 1;
    af = cr_atan2(pdy, x0 * 16 + 8 - CR_RING_C);
    al = cr_atan2(pdy, x1 * 16 + 8 - CR_RING_C);
    rf = (int32_t)((af - CR_RING_TOP) & 65535u);
    e = rf + (int16_t)(uint16_t)(al - af);  /* the run spans under half a turn */
    if (e < 0 || e > 65535) return 2;     /* across 12 o'clock */
    lo = rf < e ? rf : e;
    hi = rf < e ? e : rf;
    if (lo >= 96 && hi + 96 <= (int32_t)sweep) return 1;
    if (lo > (int32_t)sweep + 96 && hi < 65536 - 96) return 0;
    return 2;
}

static void cr_ring_draw(const cr_screen_t *s)
{
    uint16_t lc = T_LINE, pc = s->ring_rec ? T_REC : cr_rgb(s->ring_col, CR_RED);
    uint32_t sweep = !s->ring ? 0u : s->ring >= 256u ? 65536u : (uint32_t)s->ring << 8;
    int32_t j, r0 = cr_row0(), r1 = cr_row1();
    cr_arcg_t gp;
    if (!cr_ring.state) cr_ring_build();
    if (cr_ring.state != 1) {             /* (not reached: the table fits) */
        cr_arc(CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 2 * 16, 6 * 16, lc);
        if (sweep) cr_arc(CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, CR_RING_TOP, sweep, 0, 0, 0, pc);
        return;
    }
    if (sweep) cr_arc_geom(&gp, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, CR_RING_TOP, sweep, 0, 0, 0);
    for (j = r0; j < r1; j++) {
        uint16_t *row = cv_px + (uint32_t)(j + cv_oy) * cv_w;
        const uint8_t *cv = cr_ring.cov + cr_ring.off[j];
        uint32_t h;
        for (h = 0; h < 2u && cr_ring.n[j][h]; h++) {
            int32_t x0 = cr_ring.x0[j][h], x1 = x0 + cr_ring.n[j][h], i;
            int mode = sweep ? cr_ring_run(j, x0, x1 - 1, sweep) : 0;
            for (i = x0; i < x1; i++) {
                uint32_t q = *cv++, n = 0;
                if (i < cr_clip.x0 || i >= cr_clip.x1) continue;
                if (mode == 1) n = cr_ring.ps[q];
                else if (mode == 2) {
                    uint32_t rel = (cr_atan2(j * 16 + 8 - CR_RING_C, i * 16 + 8 - CR_RING_C) - CR_RING_TOP) & 65535u;
                    if (rel >= 64u && rel + 64u <= sweep) n = cr_ring.ps[q];
                    else if (!(rel > sweep + 64u && rel < 65536u - 64u)) n = cr_arc_px(&gp, i, j);
                }
                if (n >= 16u) {
                    row[i] = swap16(pc);
                    continue;
                }
                if (cr_ring.pd[q]) cr_mix(row + i, lc, cr_ring.pd[q] * 16u);
                if (n) cr_mix(row + i, pc, n * 16u);
            }
        }
    }
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
    case CR_K_EDIT8: cr_p_edit8(s); break;
    case CR_K_STACK: cr_p_stack(s); break;
    case CR_K_KNOBROW: cr_p_knobrow(s, fr, ph); break;
    case CR_K_GEEK: cr_p_geek(s, ph); break;
    case CR_K_TEXT: cr_p_text(s); break;
    case CR_K_BIG: cr_p_big(s, ph); break;
    case CR_K_SCOPE: cr_p_scope(s, ph); break;
    default: break;
    }
    if (s->footer[0]) {
        char b[48];
        int cut = cr_fit(b, sizeof b, s->footer, 12, 0, P8(224));
        cr_text(P8(120), P8(226), b, 12, 0, CR_C, 4096, T_MID, T_BG, cut ? 9u : 0u);
    }
    if (s->ring_on)                      /* Orchid's ring: a dotted circle round the edge, the progress solid */
        cr_ring_draw(s);
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
#define CR_TILE_W 60u                     /* a strip's change tiles: 4 x 5 of 60 x 8 px, hashed; a blit sends the box */
#define CR_TILE_H 8u                      /* .. of the tiles whose pixels changed */
#define CR_NTX (240u / CR_TILE_W)
#define CR_NTY (CR_STRIP_H / CR_TILE_H)
static struct {
    uint32_t sig, base;                  /* base: the signature without the ring's fraction and the editor's parts */
    uint32_t tile[CR_NSTRIP][CR_NTY * CR_NTX];   /* the pixels' hashes, per strip and tile */
    uint32_t row[CR_ED_ROWS], wv, hot, ttl;   /* the editor's parts (cr_ed_strips): each row's cells, the band, the
                                               * hot cell, the title line */
    uint16_t ring;                       /* .. which was drawn */
    uint32_t dl;                         /* the corner dial's fraction and pulse drawn (dial | dial_pulse << 16) */
    uint8_t valid, force;
    uint8_t blits, drawn;                /* strips blitted / composed by the last cr_draw (the host test reads them) */
    uint8_t slot;                        /* the DMA buffer the next blit uses (cr_send) */
    uint32_t bytes;                      /* .. bytes sent to the LCD (the emulator's log: the SPI time) */
} cr_dc __attribute__((section(".pool")));        /* (zero-initialised) */
static uint32_t cr_strip_y;              /* the strip being drawn: its top row */

static uint32_t cr_hash(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}
/* the canvas (one strip) as tiles: the mask of those whose hash changed from t[], t[] updated (all: every tile) */
static uint32_t cr_hash_tiles(uint32_t *t, int all)
{
    uint32_t m = 0, tx, ty, i, j;
    for (ty = 0; ty < CR_NTY; ty++)
        for (tx = 0; tx < CR_NTX; tx++) {
            uint32_t h = 2166136261u, k = ty * CR_NTX + tx;
            for (j = 0; j < CR_TILE_H; j++) {
                const uint16_t *r = cv_px + (ty * CR_TILE_H + j) * 240u + tx * CR_TILE_W;
                for (i = 0; i < CR_TILE_W; i++) h = (h ^ r[i]) * 16777619u;
            }
            if (all || h != t[k]) m |= 1u << k;
            t[k] = h;
        }
    return m;
}
/* the strip's canvas (rows 0 .. CR_STRIP_H - 1 of cv_px), cleared: gfx.c cv_begin without its lcd_sync, since the
 * LCD's DMA reads the blits from cv_px's rows CR_STRIP_H .. 3 CR_STRIP_H - 1 (cr_send), not these */
static void cr_cv_begin(void)
{
    uint32_t i;
    uint16_t c = swap16(T_BG);
    GFX_HOOK_BEGIN();
    cv_w = 240u;
    cv_h = CR_STRIP_H;
    cv_cy0 = 0;
    cv_cy1 = (int16_t)CR_STRIP_H;
    cv_bg = T_BG;
    for (i = 0; i < 240u * CR_STRIP_H; i++) cv_px[i] = c;
}
/* the tiles m of the strip at row y to the LCD: their box copied into one of two DMA buffers (cv_px's rows 40..79,
 * 80..119: CV_MAX holds 124) and sent from there, so the next strip is composed while this one goes out (lcd.c
 * leaves the transfer running; the next blit waits for it, and the buffer it reuses is the one before's) */
static void cr_send(uint32_t m, uint32_t y)
{
    uint32_t x0 = CR_NTX, x1 = 0, y0 = CR_NTY, y1 = 0, k, i, j, w, h;
    uint16_t *d = cv_px + 240u * CR_STRIP_H * (1u + (cr_dc.slot & 1u)), *o = d;
    _Static_assert(240u * CR_STRIP_H * 3u <= CV_MAX, "two DMA buffers after the strip's canvas");
    for (k = 0; k < CR_NTX * CR_NTY; k++)
        if (m >> k & 1u) {
            uint32_t tx = k % CR_NTX, ty = k / CR_NTX;
            if (tx < x0) x0 = tx;
            if (tx + 1u > x1) x1 = tx + 1u;
            if (ty < y0) y0 = ty;
            if (ty + 1u > y1) y1 = ty + 1u;
        }
    if (x1 <= x0) return;
    x0 *= CR_TILE_W; w = x1 * CR_TILE_W - x0;
    y0 *= CR_TILE_H; h = y1 * CR_TILE_H - y0;
    for (j = 0; j < h; j++) {
        const uint16_t *r = cv_px + (y0 + j) * 240u + x0;
        for (i = 0; i < w; i++) *o++ = r[i];
    }
    GFX_HOOK_BLIT(x0, y + y0, 0u);
    lcd_blit(x0, y + y0, w, h, d);
    cr_dc.slot ^= 1u;
    cr_dc.bytes += w * h * 2u;
}

/* the next cr_draw draws and blits the whole screen (after something else drew on it, a palette change) */
static void cr_draw_invalidate(void) { cr_dc.force = 1; }

/* the strips (bit k) holding pixels the ring's progress covers at one fraction and not the other (Q8): the band's
 * rows over the angles between the two tips, widened by the samples' reach (cr_ring_draw) and two pixels */
static uint32_t cr_ring_strips(uint32_t q0, uint32_t q1)
{
    uint32_t w0 = !q0 ? 0u : q0 >= 256u ? 65536u : q0 << 8, w1 = !q1 ? 0u : q1 >= 256u ? 65536u : q1 << 8;
    uint32_t lo = w0 < w1 ? w0 : w1, hi = w0 < w1 ? w1 : w0, a0, len, m = 0, k;
    int32_t s0, s1, smin, smax, rin = CR_RING_R - CR_RING_W / 2 - 12, rout = CR_RING_R + CR_RING_W / 2 + 12;
    int32_t y0, y1;
    if (hi - lo + 256u >= 65536u) return (1u << CR_NSTRIP) - 1u;
    a0 = (CR_RING_TOP + lo - 128u) & 65535u;
    len = hi - lo + 256u;
    s0 = cr_sin(a0);
    s1 = cr_sin(a0 + len);
    smin = s0 < s1 ? s0 : s1;
    smax = s0 < s1 ? s1 : s0;
    if (((16384u - a0) & 65535u) <= len) smax = 16384;       /* the bottom within the angles */
    if (((49152u - a0) & 65535u) <= len) smin = -16384;      /* the top */
    y0 = CR_RING_C + (((smin < 0 ? rout : rin) * smin) >> 14);
    y1 = CR_RING_C + (((smax < 0 ? rin : rout) * smax) >> 14);
    y0 = (y0 >> 4) - 2;
    y1 = (y1 >> 4) + 2;
    for (k = 0; k < CR_NSTRIP; k++)
        if ((int32_t)((k + 1u) * CR_STRIP_H) > y0 && (int32_t)(k * CR_STRIP_H) <= y1) m |= 1u << k;
    return m;
}

/* the strips (bit k) holding rows y0 .. y1 - 1 */
static uint32_t cr_strips_of(int32_t y0, int32_t y1)
{
    uint32_t m = 0, k;
    for (k = 0; k < CR_NSTRIP; k++)
        if ((int32_t)((k + 1u) * CR_STRIP_H) > y0 && (int32_t)(k * CR_STRIP_H) < y1) m |= 1u << k;
    return m;
}
/* the editor's row r (CR_K_EDIT8 / STACK; KNOBROW: row 0, its knob cells): the strips its cells, its hot block, its
 * mark and bars are drawn in */
static uint32_t cr_ed_row_strips(const cr_screen_t *s, uint32_t r)
{
    if (s->kind == CR_K_STACK) {
        uint32_t n = s->n_rows ? (s->n_rows > CR_ED_ROWS ? CR_ED_ROWS : s->n_rows) : 1u;
        int32_t rh = P8(198) / (int32_t)n, y = P8(41) + (int32_t)r * rh;
        return cr_strips_of((y >> 8) - 1, ((y + rh) >> 8) + 1);   /* (its line above it: row - 1) */
    }
    if (s->kind == CR_K_KNOBROW) {                   /* the one row of knob cells (and the strips' edges) */
        int32_t y = cr_kr_rowy(s);
        return r ? 0u : cr_strips_of(y - 2, y + CR_KR_ROW + 2);
    }
    if (s->tall || r > 1u) return (1u << CR_NSTRIP) - 1u;
    if (s->wide) return cr_strips_of((r ? 184 : 124) - 2, (r ? 240 : 180) + 2);
    return cr_strips_of((r ? 134 : 30) - 2, (r ? 234 : 130) + 2);
}
/* the strips of what changed when only the editor's parts did: a row's cells, the band's values, the hot cell (its
 * old and new rows, and the band: its lit segment follows it) */
static uint32_t cr_ed_strips(const cr_screen_t *s, const uint32_t *row, uint32_t wv, uint32_t hot)
{
    uint32_t m = 0, r, band = s->kind == CR_K_EDIT8 && s->wide && !s->tall ? cr_strips_of(20, 122) : (1u << CR_NSTRIP) - 1u;
    if (s->kind != CR_K_EDIT8 && s->kind != CR_K_STACK && s->kind != CR_K_KNOBROW) return (1u << CR_NSTRIP) - 1u;
    for (r = 0; r < CR_ED_ROWS; r++)
        if (row[r] != cr_dc.row[r]) m |= cr_ed_row_strips(s, r);
    if (wv != cr_dc.wv) m |= band;
    if (hot != cr_dc.hot) {
        if (cr_dc.hot & 255u) m |= cr_ed_row_strips(s, (cr_dc.hot & 255u) - 1u);
        if (hot & 255u) m |= cr_ed_row_strips(s, (hot & 255u) - 1u);
        if (s->wide && s->kind == CR_K_EDIT8) m |= band;
    }
    return m;
}

static void cr_draw(const cr_screen_t *s, uint32_t anim_ms)
{
    cr_frame_t fr;
    uint32_t sig, base, k, strips = (1u << CR_NSTRIP) - 1u, row[CR_ED_ROWS], wv, hot;
    uint32_t ttl, ed = s->kind == CR_K_EDIT8 || s->kind == CR_K_STACK, i, j, o;
    /* the struct's bytes but these (offset, size), hashed apart: the ring's fraction, the corner dial's fraction and
     * pulse, the editor's parts (its cells, the hot cell, the band's values, its title line) */
    uint32_t sk[8][2] = {
        {(uint32_t)__builtin_offsetof(cr_screen_t, ring), sizeof s->ring},
        {(uint32_t)__builtin_offsetof(cr_screen_t, dial), sizeof s->dial},
        {(uint32_t)__builtin_offsetof(cr_screen_t, dial_pulse), sizeof s->dial_pulse},
        {(uint32_t)__builtin_offsetof(cr_screen_t, cell), sizeof s->cell},
        {(uint32_t)__builtin_offsetof(cr_screen_t, hot_r), 3u},
        {(uint32_t)__builtin_offsetof(cr_screen_t, wv), sizeof s->wv},
        {(uint32_t)__builtin_offsetof(cr_screen_t, title), sizeof s->title},
        {(uint32_t)__builtin_offsetof(cr_screen_t, page), sizeof s->page}};
    _Static_assert(__builtin_offsetof(cr_screen_t, hot_c) == __builtin_offsetof(cr_screen_t, hot_r) + 1 &&
                   __builtin_offsetof(cr_screen_t, hot_col) == __builtin_offsetof(cr_screen_t, hot_r) + 2, "hot_r c col");
    uint32_t dl = (uint32_t)s->dial | (uint32_t)s->dial_pulse << 16;
    for (i = 1; i < 8u; i++)                                     /* (in the struct's order) */
        for (j = i; j && sk[j - 1][0] > sk[j][0]; j--) {
            uint32_t a = sk[j][0], c = sk[j][1];
            sk[j][0] = sk[j - 1][0]; sk[j][1] = sk[j - 1][1];
            sk[j - 1][0] = a; sk[j - 1][1] = c;
        }
    cr_frame(s, anim_ms, &fr);
    base = 2166136261u ^ ux.gen;
    for (i = 0, o = 0; i < 8u; o = sk[i][0] + sk[i][1], i++)
        base = cr_hash(base, (const uint8_t *)s + o, sk[i][0] - o);
    base = cr_hash(base, (const uint8_t *)s + o, (uint32_t)sizeof *s - o);
    base = cr_hash(base, &fr, sizeof fr);
    ttl = cr_hash(cr_hash(cr_hash(2166136261u, s->title, sizeof s->title), s->page, sizeof s->page), &s->title_col, 1u);
    if (!ed)                                                     /* (the title line: the editor's top strip only) */
        base = cr_hash(base, &ttl, sizeof ttl);
    for (k = 0; k < CR_ED_ROWS; k++)
        row[k] = cr_hash(2166136261u, s->cell[k], sizeof s->cell[k]);
    wv = cr_hash(2166136261u, s->wv, sizeof s->wv);
    hot = (uint32_t)s->hot_r | (uint32_t)s->hot_c << 8 | (uint32_t)s->hot_col << 16;
    sig = cr_hash(cr_hash(cr_hash(cr_hash(cr_hash(cr_hash(base, &s->ring, sizeof s->ring), &dl, sizeof dl), row, sizeof row),
                                  &wv, sizeof wv), &hot, sizeof hot), &ttl, sizeof ttl);
    cr_dc.blits = cr_dc.drawn = 0;
    if (cr_dc.valid && !cr_dc.force && sig == cr_dc.sig)
        return;                          /* nothing changed: nothing drawn */
    if (cr_dc.valid && !cr_dc.force && base == cr_dc.base) {
        strips = 0;
        if (s->ring_on && s->ring != cr_dc.ring)
            strips = cr_ring_strips(cr_dc.ring, s->ring);   /* the ring's fraction moved: the strips its tip crossed */
        if (ttl != cr_dc.ttl)
            strips |= 1u;                                        /* the editor's title line (rows 0..24) */
        if (dl != cr_dc.dl)
            strips |= 1u;                                        /* the corner dial (rows 2..22) */
        if (wv != cr_dc.wv || hot != cr_dc.hot)
            strips |= cr_ed_strips(s, row, wv, hot);
        else
            for (k = 0; k < CR_ED_ROWS; k++)
                if (row[k] != cr_dc.row[k]) {
                    strips |= cr_ed_strips(s, row, wv, hot);    /* the editor: only the parts that changed */
                    break;
                }
    }
    cr_dc.bytes = 0;
    if (cr_dc.force)
        lcd_sync();                      /* (something else drew: its transfer may read cv_px's first rows) */
    for (k = 0; k < CR_NSTRIP; k++) {
        uint32_t m;
        if (!(strips >> k & 1u))
            continue;
        cr_strip_y = k * CR_STRIP_H;
        cr_cv_begin();
        cv_oy = -(int32_t)cr_strip_y;
        cr_clip_all();
        cr_compose(s, &fr);
        cr_dc.drawn++;
        cv_oy = 0;
        if ((m = cr_hash_tiles(cr_dc.tile[k], cr_dc.force || !cr_dc.valid)) != 0) {
            cr_send(m, cr_strip_y);
            cr_dc.blits++;
        }
    }
    cr_dc.sig = sig;
    cr_dc.base = base;
    cr_dc.ring = s->ring;
    cr_dc.dl = dl;
    for (k = 0; k < CR_ED_ROWS; k++)
        cr_dc.row[k] = row[k];
    cr_dc.wv = wv;
    cr_dc.hot = hot;
    cr_dc.ttl = ttl;
    cr_dc.valid = 1;
    cr_dc.force = 0;
}
