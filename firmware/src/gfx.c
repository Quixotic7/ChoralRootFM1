/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian).
 * Text and icons are 4-bit alpha bitmaps made at build time (tools/gen_aa_font.py: Inter Tight;
 * tools/gen_aa_icons.py: Fukiai). A glyph is trimmed to its ink box, advances are in 1/16 px, and
 * each glyph is rasterised at 1 << psh horizontal phases (S M 4, L 2: drawn at a pen of +0, +1/4 .. px);
 * cv_text takes the phase that puts it within a phase step of its true place. Blending uses a 16-entry
 * ramp per (ink, background) pair, cached, so a pixel costs a nibble read and a store. Colours come from the theme tokens. */
typedef struct { uint16_t off, adv; int8_t bx; uint8_t by, bw, bh; } aag_t;
typedef struct {
    uint8_t h, asc, first, last, nex;   /* line height, baseline; ASCII range; extras */
    uint16_t nk;                        /* kerning pairs */
    const aag_t *g;
    const uint8_t *data;
    const uint16_t *kkey;               /* a << 8 | b, sorted */
    const int8_t *kd;                   /* 1/16 px */
    const uint8_t *ex;                  /* codes of the extra glyphs */
    uint8_t psh;                        /* log2 of the phases per glyph: g[i << psh] .. g[(i << psh) + phases - 1] */
    const uint8_t *hc;                  /* data's Huffman code (cv_alpha_hc), 0 = 2 px per byte (cv_alpha) */
} aafont_t;
typedef struct { const char *name; uint16_t bg, surf, text, theme, accent; } ui_pal_t;
typedef struct { uint16_t off; uint8_t w; const char *label; } kc_t;
#include "ui_fonts.h"                   /* AF_S 12 px / 400, AF_M 15 px / 500, AF_L 28 px / 600 */
#include "ui_palettes.h"
#ifndef FELUCCA_KEYCAPS
#define FELUCCA_KEYCAPS 1               /* Felucca's keycaps (ui*.c's key hints); ChoralRoot's units set 0: none drawn */
#endif
#if FELUCCA_KEYCAPS
#include "ui_keycaps.h"                 /* KC_*: the keycaps / badges (tools/gen_aa_keycaps.py), KNOB_* arcs */
#endif
#define ELLIPSIS '\x85'                 /* the ellipsis glyph of AF_S and AF_M */

/* host tests hook in here (layout lint, draw cost); nothing in the firmware */
#ifndef GFX_HOOK_TEXT
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) ((void)0)   /* an ink box; flags 1 ellipsised, 2 cut, 4 icon, 8 free text, 32 in a scrolled view */
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
#endif
#ifndef GFX_HOOK_CELL
#define GFX_HOOK_CELL(x0, y0, x1, y1) ((void)0)    /* a cell, card, row or button (cv_rrect): a box touching it lies inside it */
#endif

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static uint16_t cv_bg;           /* what cv_begin cleared the canvas to */
static uint8_t cv_scroll;        /* 1: drawing a scrolled view (its text may run past the canvas) */
static int32_t cv_oy;            /* y offset for graph drawing */
static int16_t cv_cy0, cv_cy1;   /* text clip: canvas rows cv_cy0 .. cv_cy1 - 1 (cv_begin: the whole canvas) */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* ------------------------------------------------------------- theme --- */
/* Five stored colours per palette, the rest derived with fixed blends (tools/gen_ui_palettes.py) */
static struct {
    uint16_t bg, surf, text, theme, accent;
    uint16_t mid, dim, line, sel, tint, ink, rec, raise, key, lane, grid;
    uint8_t light, mono;
    uint32_t gen;                /* bumped by palette_set (the text ramps follow) */
} ux;
#define T_BG ux.bg               /* background */
#define T_SURF ux.surf           /* dialogs, menu rows, mixer strips */
#define T_TEXT ux.text           /* primary text */
#define T_THEME ux.theme         /* values, curves, the gauge's end line */
#define T_ACCENT ux.accent       /* the one active thing: hot knob, cursor, playhead */
#define T_MID ux.mid             /* labels, units, secondary text */
#define T_DIM ux.dim             /* inactive, empty */
#define T_LINE ux.line           /* 1 px dividers, gauge track */
#define T_SEL ux.sel             /* selection and gauge fill */
#define T_TINT ux.tint           /* a faint theme area */
#define T_INK ux.ink             /* text on T_SEL / T_THEME */
#define T_REC ux.rec             /* REC: fixed red (MONO: the accent) */
#define T_RAISE ux.raise         /* a raised area on a surface: stubs, guides, slots, chips, button wells */
#define T_KEY ux.key             /* a keycap's fill (its label: T_INK; unavailable: a T_DIM fill) */
#define T_LANE ux.lane           /* the piano roll: an in-scale row (SURF -> THEME 10 %) */
#define T_GRID ux.grid           /* the piano roll: a step line (SURF -> TEXT 6 %; beats and C rows: RAISE) */
#define NPALETTES UI_NPALETTES

static inline uint16_t ux_gray(uint32_t x5) { return (uint16_t)((x5 << 11) | (x5 << 6) | x5); }
/* a + (b - a) * pct / 100 per channel, rounded; MONO on the red channel, so it stays gray */
static uint16_t ux_mix(uint16_t a, uint16_t b, int32_t pct)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t out = 0, k;
    if (ux.mono) {
        int32_t x = a >> 11, d = ((b >> 11) - x) * pct;
        return ux_gray((uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100));
    }
    for (k = 0; k < 3u; k++) {
        int32_t x = (a >> SH[k]) & MK[k], d = (((b >> SH[k]) & MK[k]) - x) * pct;
        out |= (uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100) << SH[k];
    }
    return (uint16_t)out;
}
static uint32_t ux_luma(uint16_t c)        /* 0..255 */
{
    return ((c >> 11) * 255u / 31u * 54u + ((c >> 5) & 63u) * 255u / 63u * 183u + (c & 31u) * 255u / 31u * 19u) >> 8;
}

static void palette_set(uint32_t i)
{
    const ui_pal_t *p = &UI_PALETTES[i % NPALETTES];
    ux.mono = i % NPALETTES == UI_MONO_INDEX;
    ux.bg = p->bg; ux.surf = p->surf; ux.text = p->text; ux.theme = p->theme; ux.accent = p->accent;
    ux.mid = ux_mix(p->bg, p->text, UI_MID_PCT);
    ux.dim = ux_mix(p->bg, p->text, UI_DIM_PCT);
    ux.line = ux_mix(p->bg, p->text, UI_LINE_PCT);
    ux.sel = ux_mix(p->bg, p->theme, UI_SEL_PCT);
    ux.tint = ux_mix(p->bg, p->theme, UI_TINT_PCT);
    ux.ink = p->bg;
    ux.raise = ux_mix(p->surf, p->text, UI_RAISE_PCT);
    ux.key = ux_mix(p->bg, p->text, UI_KEY_PCT);
    ux.lane = ux_mix(p->surf, p->theme, 10);
    ux.grid = ux_mix(p->surf, p->text, 6);
    ux.light = ux_luma(p->bg) > 128u;
    ux.rec = ux.mono ? p->accent : ux.light ? UI_REC_LIGHT : UI_REC_DARK;
    ux.gen++;                                    /* the text ramps change with the coverage curve */
}

/* settings store a tagged id; ids below 20 are the palettes of earlier firmware */
static uint32_t palette_from_stored(uint32_t v)
{
    if (v >= UI_PAL_TAG && v - UI_PAL_TAG < NPALETTES) return v - UI_PAL_TAG;
    return v < 20u ? UI_PALETTE_MIGRATE[v] : UI_MONO_INDEX;
}
static uint32_t palette_to_stored(uint32_t i) { return UI_PAL_TAG + i % NPALETTES; }
static int palette_stored_ok(uint32_t v) { return v < 20u || (v >= UI_PAL_TAG && v - UI_PAL_TAG < NPALETTES); }

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

/* ------------------------------------------------------------ canvas --- */
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    uint16_t s = swap16(bg);
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    GFX_HOOK_BEGIN();
    cv_w = w;
    cv_h = h;
    cv_cy0 = 0;
    cv_cy1 = (int16_t)h;
    cv_bg = bg;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = s;
}

static void cv_blit(uint32_t x, uint32_t y)
{
    GFX_HOOK_BLIT(x, y, 0u);
    lcd_blit(x, y, cv_w, cv_h, cv_px);
}

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    GFX_HOOK_BLIT(x, y, r0);
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

/* a 1 px outline */
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* a line `thick` px tall (the scope's trace: 2) */
static void cv_line_t(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, int32_t thick)
{
    int32_t t;
    for (t = 0; t < thick; t++)
        cv_line(x0, y0 + t, x1, y1 + t, c);
}

/* coverage (0..16) of corner pixel (i, j) of a radius-r corner, (0, 0) the outermost: 4 x 4 samples */
static uint32_t rr_cov(int32_t r, int32_t i, int32_t j)
{
    int32_t sx, sy, n = 0;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int32_t dx = r * 8 - ((i * 4 + sx) * 2 + 1), dy = r * 8 - ((j * 4 + sy) * 2 + 1);   /* 1/8 px */
            n += dx * dx + dy * dy <= r * r * 64;
        }
    return (uint32_t)n;
}
/* a filled rectangle with rounded corners (r <= 8), the corners anti-aliased against `under` (what lies
 * behind it): the flat look's cards, panels, rows, bars and buttons */
static void cv_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    int32_t i, j;
    GFX_HOOK_CELL(x, y + cv_oy, x + w, y + h + cv_oy);   /* (the lint: what is drawn on it stays inside it) */
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, r, h - 2 * r, c);
    cv_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = rr_cov(r, i, j);
            uint16_t px = a >= 16u ? c : a == 0u ? under : ux_mix(under, c, (int32_t)(a * 100u / 16u));
            cv_pset(x + i, y + j, px);
            cv_pset(x + w - 1 - i, y + j, px);
            cv_pset(x + i, y + h - 1 - j, px);
            cv_pset(x + w - 1 - i, y + h - 1 - j, px);
        }
}

/* ------------------------------------------------- 4-bit alpha blit --- */
/* coverage curves: a little heavier for light ink on dark, lighter for dark ink on light */
static const uint8_t CURVE_DARK[16] = {0, 24, 43, 62, 80, 97, 114, 130, 147, 163, 178, 194, 210, 225, 240, 255};
static const uint8_t CURVE_LIGHT[16] = {0, 12, 27, 42, 58, 75, 91, 109, 126, 144, 162, 180, 199, 217, 236, 255};
#define NRAMP 6u
static struct {
    uint16_t fg[NRAMP], bg[NRAMP], v[NRAMP][16];   /* v: byte-swapped panel values */
    uint32_t gen;
    uint8_t next, n;
} rc;

static const uint16_t *ramp(uint16_t fg, uint16_t bg)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    const uint8_t *cv = ux.light ? CURVE_LIGHT : CURVE_DARK;
    uint32_t i, a, k;
    uint16_t *v;
    if (rc.gen != ux.gen) {
        rc.gen = ux.gen;
        rc.n = rc.next = 0;
    }
    for (i = 0; i < rc.n; i++)
        if (rc.fg[i] == fg && rc.bg[i] == bg)
            return rc.v[i];
    i = rc.next;
    rc.next = (uint8_t)((i + 1u) % NRAMP);
    if (rc.n < NRAMP)
        rc.n++;
    rc.fg[i] = fg;
    rc.bg[i] = bg;
    v = rc.v[i];
    for (a = 0; a < 16u; a++) {
        uint32_t out = 0;
        if (ux.mono) {
            int32_t x = bg >> 11;
            out = ux_gray((uint32_t)(x + ((((fg >> 11) - x) * (int32_t)cv[a] + 128) >> 8)));
        } else {
            for (k = 0; k < 3u; k++) {
                int32_t x = (bg >> SH[k]) & MK[k], y = (fg >> SH[k]) & MK[k];
                out |= (uint32_t)(x + (((y - x) * (int32_t)cv[a] + 128) >> 8)) << SH[k];
            }
        }
        v[a] = swap16(out);
    }
    return v;
}

/* w x h nibbles, rows back to back, at canvas (x, y) */
static void cv_alpha(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint16_t *rv)
{
    uint32_t k = 0, gx, gy;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row;
        if (py < cv_cy0 || py >= cv_cy1) {
            k += w;
            continue;
        }
        row = cv_px + (uint32_t)py * cv_w;
        for (gx = 0; gx < w; gx++, k++) {
            uint32_t v = (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t px = x + (int32_t)gx;
            if (v && (uint32_t)px < cv_w)
                row[px] = rv[v];
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* the same from a glyph's Huffman-coded bit stream (tools/gen_aa_font.py huff_pack): canonical codes, MSB
 * first; hc = the number of codes of each length 1..15, then the symbols in code order, each a value and
 * its repeat count ((count - 1) << 4 | value: runs of zeros and 15s). Codes of up to 8 bits come from a
 * 256-entry table made once per font in RAM (symbol | length << 8; 0 = a longer code: bit by bit). The
 * stream is read in order, so rows outside the clip are decoded and dropped; a run spans rows */
#define HC_FONTS 2                       /* the coded faces: M and L (gen_aa_font.py HUFF) */
static struct { const uint8_t *hc; uint16_t lut[256]; } hc_tab[HC_FONTS];
static const uint16_t *hc_lut(const uint8_t *hc)
{
    uint32_t k, len, code = 0, sym = 15, i;
    for (k = 0; k < HC_FONTS - 1u && hc_tab[k].hc && hc_tab[k].hc != hc; k++)
        ;
    if (hc_tab[k].hc != hc) {                        /* (more fonts than slots: the last slot is rebuilt) */
        hc_tab[k].hc = hc;
        for (i = 0; i < 256u; i++)
            hc_tab[k].lut[i] = 0;
        for (len = 1; len <= 8u; len++, code <<= 1)
            for (i = 0; i < hc[len - 1u]; i++, code++, sym++) {
                uint32_t a = code << (8u - len), n = 1u << (8u - len);
                while (n--)
                    hc_tab[k].lut[a + n] = (uint16_t)(hc[sym] | len << 8);
            }
    }
    return hc_tab[k].lut;
}

static void cv_alpha_hc(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint8_t *hc,
                        const uint16_t *rv)
{
    const uint16_t *lut = hc_lut(hc);
    uint32_t gy, v = 0, run = 0, acc = 0, nb = 0;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row = py < cv_cy0 || py >= cv_cy1 ? 0 : cv_px + (uint32_t)py * cv_w;
        uint32_t gx = 0;
        while (gx < w) {
            uint32_t n;
            if (!run) {                              /* the next symbol */
                uint32_t e, s;
                while (nb < 16u) {                   /* (the data ends with 2 spare bytes) */
                    acc = acc << 8 | *d++;
                    nb += 8;
                }
                e = lut[(acc >> (nb - 8u)) & 255u];
                if (e) {
                    nb -= e >> 8;
                    s = e & 255u;
                } else {                             /* a code over 8 bits */
                    uint32_t code = 0, first = 0, idx = 0, len = 0;
                    for (;;) {
                        code |= (acc >> --nb) & 1u;
                        if (code - first < hc[len])
                            break;
                        idx += hc[len];
                        first = (first + hc[len]) << 1;
                        code <<= 1;
                        len++;
                    }
                    s = hc[15u + idx + code - first];
                }
                v = s & 15u;
                run = (s >> 4) + 1u;
            }
            n = run < w - gx ? run : w - gx;
            if (v && row) {
                int32_t px = x + (int32_t)gx;
                uint32_t k;
                for (k = 0; k < n; k++, px++)
                    if ((uint32_t)px < cv_w)
                        row[px] = rv[v];
            }
            gx += n;
            run -= n;
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* ------------------------------------------------------------- text --- */
/* the glyph number of ch in f, -1 = not in the face */
static int32_t glyph_at(const aafont_t *f, uint32_t ch)
{
    uint32_t i;
    if (ch >= f->first && ch <= f->last)
        return (int32_t)(ch - f->first);
    for (i = 0; i < f->nex; i++)
        if (f->ex[i] == ch)
            return (int32_t)(f->last - f->first + 1u + i);
    return -1;
}

/* the table entry of ch at phase 0 (its other phases follow it). Not in the face: the ellipsis as '.',
 * else '?', else the face's first glyph (L is sparse: its space) */
static uint32_t glyph(const aafont_t *f, uint32_t ch)
{
    int32_t k;
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;                                   /* L has capitals only */
    if ((k = glyph_at(f, ch)) < 0 && (ch != (uint8_t)ELLIPSIS || (k = glyph_at(f, '.')) < 0) &&
        (k = glyph_at(f, '?')) < 0)
        k = 0;
    return (uint32_t)k << f->psh;
}

static int32_t kern(const aafont_t *f, uint32_t a, uint32_t b)       /* 1/16 px */
{
    uint32_t key = (a << 8) | b, lo = 0, hi = f->nk;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2u;
        if (f->kkey[m] == key)
            return f->kd[m];
        if (f->kkey[m] < key)
            lo = m + 1u;
        else
            hi = m;
    }
    return 0;
}

static uint32_t fold(const aafont_t *f, uint32_t c) { return c >= 'a' && c <= 'z' && f->last < 'a' ? c - 32u : c; }

static int32_t text_w(const aafont_t *f, const char *s)
{
    int32_t pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        if (prev)
            pen += kern(f, prev, c);
        pen += f->g[glyph(f, c)].adv;
        prev = c;
    }
    return (pen + 8) >> 4;
}

/* y = top of the line box (the baseline is y + f->asc); returns the pen x at the end.
 * fg on bg: bg is what lies under the text (the canvas, a card, a selection).
 * Placement: the pen (1/16 px) lies between two phase positions a step (16 >> psh) apart; the glyph takes the
 * one whose offset from the pen is nearer the previous glyph's offset, so the space between neighbours stays
 * truest and no glyph is a whole step from its true place (tools/aa_raster.py, text_spacing_test.py).
 * A glyph with one phase (its phases share one bitmap: figures and .) sits at the rounded pen, as when it
 * is drawn alone at text_w of what precedes it (ui_draw.c roll_text). */
static int32_t cv_text_flags(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                             uint32_t flags)
{
    const uint16_t *rv = ramp(fg, bg);
    const char *s0 = s;
    int32_t pen = 0, err = 0, x0 = 0x7FFF, y0 = 0x7FFF, x1 = -0x7FFF, y1 = -0x7FFF;
    const int32_t step = 16 >> f->psh;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        const aag_t *g;
        int32_t pos, d0, d1;
        if (prev)
            pen += kern(f, prev, c);
        g = &f->g[glyph(f, c)];
        if (f->psh && g[1].off != g->off) {
            pos = pen - (pen & (step - 1));          /* the phase position at or left of the pen */
            d0 = pos - pen - err;                    /* .. and the one right of it: which keeps the gap truer */
            d1 = d0 + step;
            if ((d0 < 0 ? -d0 : d0) > (d1 < 0 ? -d1 : d1))
                pos += step;
            g += ((uint32_t)pos >> (4u - f->psh)) & ((1u << f->psh) - 1u);
        } else {
            pos = (pen + 8) & ~15;                   /* one phase (figures, ., blanks): the rounded pen */
        }
        err = pos - pen;
        if (g->bw) {
            int32_t gx = x + (pos >> 4) + g->bx, gy = y + g->by;
            if (f->hc)
                cv_alpha_hc(gx, gy, g->bw, g->bh, f->data + g->off, f->hc, rv);
            else
                cv_alpha(gx, gy, g->bw, g->bh, f->data + g->off, rv);
            if (gx < x0) x0 = gx;
            if (gy < y0) y0 = gy;
            if (gx + g->bw > x1) x1 = gx + g->bw;
            if (gy + g->bh > y1) y1 = gy + g->bh;
        }
        pen += g->adv;
        prev = c;
    }
    if (x1 > x0) {
        if (x0 < 0 || x1 > (int32_t)cv_w || y0 + cv_oy < cv_cy0 || y1 + cv_oy > cv_cy1)
            flags |= 2u;                             /* cut by the canvas */
        if (cv_scroll) {                             /* a scrolled view: what lands on the screen is its visible part */
            flags |= 32u;
            if (y0 + cv_oy < cv_cy0) y0 = cv_cy0 - cv_oy;
            if (y1 + cv_oy > cv_cy1) y1 = cv_cy1 - cv_oy;
        }
        GFX_HOOK_TEXT(x0, y0 + cv_oy, x1, y1 + cv_oy, s0, flags);
    }
    (void)s0;
    return x + ((pen + 8) >> 4);
}
static int32_t cv_text_on(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    return cv_text_flags(x, y, f, s, fg, bg, 0);
}
/* .. on the canvas background */
static int32_t cv_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg)
{
    return cv_text_flags(x, y, f, s, fg, cv_bg, 0);
}
/* right-aligned: the advance ends at xr */
static int32_t cv_text_r(int32_t xr, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(f, s);
    cv_text_on(xr - w, y, f, s, fg, bg);
    return xr - w;
}
static int32_t cv_text_c(int32_t xc, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(f, s);
    return cv_text_on(xc - w / 2, y, f, s, fg, bg);
}

/* s into d (n bytes), at most maxw px wide: never a smaller font, the end ellipsised. 1 = cut */
static int text_fit(char *d, uint32_t n, const char *s, const aafont_t *f, int32_t maxw)
{
    uint32_t k = 0;
    while (s[k] && k + 1u < n) {
        d[k] = s[k];
        k++;
    }
    d[k] = 0;
    if (!s[k] && text_w(f, d) <= maxw)
        return 0;
    if (k + 2u > n)
        k = n - 2u;
    for (;;) {                                       /* drop characters until "text..." fits */
        while (k && d[k - 1u] == ' ')
            k--;
        d[k] = ELLIPSIS;
        d[k + 1u] = 0;
        if (!k || text_w(f, d) <= maxw)
            return 1;
        k--;
    }
}
/* text in maxw px, ellipsised when it is longer: a label or a value should always fit (the host
 * layout lint reports a cut one), free text (cv_free_text: a preset name, a message) may be cut */
static int32_t cv_text_fit(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                           int32_t maxw)
{
    char b[48];
    int cut = text_fit(b, sizeof b, s, f, maxw);
    return cv_text_flags(x, y, f, b, fg, bg, cut ? 1u : 0u);
}
static int32_t cv_free_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                            int32_t maxw)
{
    char b[48];
    int cut = text_fit(b, sizeof b, s, f, maxw);
    return cv_text_flags(x, y, f, b, fg, bg, cut ? 9u : 8u);
}

/* ---------------------------------------------------------- keycaps --- */
#if FELUCCA_KEYCAPS
/* a pre-rendered pill (tools/gen_aa_keycaps.py): nibble 0 outside, 1..4 the edge over `under`, 5 the fill,
 * 6..15 the label over the fill; the 16 colours per (under, fill, ink) are cached like the text ramps */
#define NKRAMP 4u
static struct {
    uint16_t key[NKRAMP][3], v[NKRAMP][16];
    uint32_t gen;
    uint8_t next, n;
} kr __attribute__((section(".pool")));      /* (zero-initialised; outside .bss) */
static const uint16_t *kc_ramp(uint16_t under, uint16_t fill, uint16_t ink)
{
    uint32_t i, a;
    uint16_t *v;
    if (kr.gen != ux.gen) {
        kr.gen = ux.gen;
        kr.n = kr.next = 0;
    }
    for (i = 0; i < kr.n; i++)
        if (kr.key[i][0] == under && kr.key[i][1] == fill && kr.key[i][2] == ink)
            return kr.v[i];
    i = kr.next;
    kr.next = (uint8_t)((i + 1u) % NKRAMP);
    if (kr.n < NKRAMP)
        kr.n++;
    kr.key[i][0] = under;
    kr.key[i][1] = fill;
    kr.key[i][2] = ink;
    v = kr.v[i];
    v[0] = swap16(under);
    for (a = 1; a < 5u; a++)
        v[a] = swap16(ux_mix(under, fill, (int32_t)a * 20));
    for (a = 5; a < 16u; a++)
        v[a] = swap16(ux_mix(fill, ink, (int32_t)(a - 5u) * 10));
    return v;
}
static int32_t kc_w(uint32_t id) { return id < KC_COUNT ? KC[id].w : 0; }
/* keycap / badge `id` with its top-left at (x, y), KC_H tall: fill and label tinted with tokens, `under` what lies
 * behind its corners; returns its right edge */
static int32_t cv_keycap(int32_t x, int32_t y, uint32_t id, uint16_t fill, uint16_t ink, uint16_t under)
{
    const kc_t *k = &KC[id < KC_COUNT ? id : 0u];
    const uint16_t *rv = kc_ramp(under, fill, ink);
    cv_alpha(x, y, k->w, KC_H, KC_DATA + k->off, rv);
    if (x < 0 || x + k->w > (int32_t)cv_w || y + cv_oy < 0 || y + KC_H + cv_oy > (int32_t)cv_h)
        GFX_HOOK_TEXT(x, y + cv_oy, x + k->w, y + KC_H + cv_oy, k->label, 6u);      /* cut by the canvas */
    else
        GFX_HOOK_TEXT(x, y + cv_oy, x + k->w, y + KC_H + cv_oy, k->label, 4u);
    return x + k->w;
}

/* a key hint: the keycap, then what it does (S, TEXT); unavailable: a DIM keycap and a DIM word. The word sits on
 * the keycap's centre line (S caps 9 rows, the keycap's 7). act may be 0 (the keycap alone); returns the end x */
#define KH_GAP 4                                  /* keycap -> its word */
static int32_t kh_w(uint32_t id, const char *act) { return kc_w(id) + (act && act[0] ? KH_GAP + text_w(&AF_S, act) : 0); }
static int32_t cv_key_hint(int32_t x, int32_t y, uint32_t id, const char *act, int on, uint16_t under)
{
    x = cv_keycap(x, y, id, on ? T_KEY : T_DIM, T_INK, under);
    if (act && act[0])
        x = cv_text_on(x + KH_GAP, y - 1, &AF_S, act, on ? T_TEXT : T_DIM, under);
    return x;
}
/* a row of key hints from x0 to x1: the first at x0, the last ending at x1, the space shared between them. When
 * the row is too wide the action words go first, from the last hint back (the keycaps never shrink).
 * on: a bit per hint (1 = available) */
typedef struct { uint8_t key; const char *act; } khint_t;
static void cv_key_row(int32_t x0, int32_t x1, int32_t y, const khint_t *h, uint32_t n, uint32_t on, uint16_t under)
{
    uint32_t i, words = n;                         /* hints 0 .. words-1 keep their word */
    int32_t w, gap, x = x0;
    for (;;) {
        w = 0;
        for (i = 0; i < n; i++)
            w += kh_w(h[i].key, i < words ? h[i].act : 0);
        if (w + 6 * (int32_t)(n - 1u) <= x1 - x0 || !words)
            break;
        words--;
    }
    gap = n > 1u ? (x1 - x0 - w) / (int32_t)(n - 1u) : 0;
    for (i = 0; i < n; i++) {
        if (n > 1u && i + 1u == n)
            x = x1 - kh_w(h[i].key, i < words ? h[i].act : 0);   /* the last one flush right */
        x = cv_key_hint(x, y, h[i].key, i < words ? h[i].act : 0, (on >> i) & 1u, under) + gap;
    }
}

/* "[OCT+]..." -> the keycap's id and the length of the bracket, else -1 (a bracketed word that is no keycap is text) */
static int32_t kc_tag(const char *s, uint32_t *len)
{
    uint32_t i, n;
    if (s[0] != '[')
        return -1;
    for (n = 1; s[n] && s[n] != ']'; n++)
        if (n > 8u)
            return -1;
    if (s[n] != ']')
        return -1;
    for (i = 0; i < KC_COUNT; i++) {
        const char *l = KC[i].label;
        uint32_t k = 0;
        while (k + 1u < n && l[k] == s[k + 1u])
            k++;
        if (k + 1u == n && !l[k]) {
            *len = n + 1u;
            return (int32_t)i;
        }
    }
    return -1;
}
/* free text (S) that may start with a keycap, "[SAVE] HOLD TO UNDO", or have one after its first word,
 * "HOLD [GLO] QUICK": the word, the keycap, then the rest, cut to maxw */
static int32_t cv_free_hint(int32_t x, int32_t y, const char *s, uint16_t fg, uint16_t bg, int32_t maxw)
{
    uint32_t n, i;
    int32_t id = kc_tag(s, &n), x0 = x;
    for (i = 0; id < 0 && i < 7u && s[i] && s[i] != ' '; i++)
        ;
    if (id < 0 && i && s[i] == ' ' && (id = kc_tag(s + i + 1u, &n)) >= 0) {
        char w[8] = {0};
        uint32_t j;
        for (j = 0; j < i; j++)
            w[j] = s[j];
        x = cv_text_on(x, y, &AF_S, w, fg, bg) + KH_GAP;
        s += i + 1u;
    }
    if (id >= 0 && kc_w((uint32_t)id) + KH_GAP < maxw - (x - x0)) {
        x = cv_keycap(x, y + 1, (uint32_t)id, T_KEY, T_INK, bg) + KH_GAP;
        s += n;
        while (*s == ' ')
            s++;
        if (!*s)
            return x - KH_GAP;
    }
    return cv_free_text(x, y, &AF_S, s, fg, bg, maxw - (x - x0));
}
#endif /* FELUCCA_KEYCAPS */

/* one-shot: a line of text in a box of colour bg, blitted (align: 0 left, 1 centre, 2 right) */
static void draw_text_line(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s,
                           uint16_t c, uint16_t bg, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, bg);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s,
                          uint16_t c, int align)
{
    draw_text_line(x, y, w, f, s, c, T_BG, align);
}
