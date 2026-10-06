/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware's text (src/gfx.c cv_text) against the reference text path of the UI-redesign prototype that
 * drew the UI design screens (its aa_text / aa_blit4 / ramp_make,
 * restated below): pen in 1/16 px accumulated over the string, pair kerning by binary search, no extra tracking,
 * the 16-entry ramp with the light / dark coverage curve. Glyph placement has since moved on from the prototype's
 * (the glyph at the rounded pen) to horizontal phases (tools/aa_raster.py): of the two phase positions around
 * the pen, the one whose offset is nearer the previous glyph's, plus that phase's ink offset; a glyph with one
 * phase (figures, .) at the rounded pen as before; restated here too.
 * How close that lands to the font's own spacing is tests/text_spacing_test.py's question. Every string below, in S M L, THEME on SURF and TEXT on BG, in every palette: the end pen and every
 * pixel must be identical (MONO expands its gray with G = 2 R, the gray rule; the prototype's G = 2 R + R / 16
 * differed by one green LSB there, and that is the only difference to the mock renders). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"

static const char *const STR[] = {
    "CUTOFF", "RESO", "ATK", "DEC", "LEVEL", "PAN", "REV", "MUTE", "STEP", "LANE", "HIT", "ACC", "No.",
    "124", "1234.5", "-12", "+3", "0.87", "100 %", "/32", "C#4", "ANALOG", "DIGITAL", "OFF", "ON",
    "OVERWRITE PROJECT A?", "SONG PATTERN CHANGES", "OCT-   NO", "OCT+   YES", "PRESETS", "MOVE", "BACK",
    "AV AW LT TA YO To Vo", "KEYS = STEPS", "PAGE 1/2", "HARDWARE CALIBRATION", "Multi-engine synthesizer",
    "H\xFCgelton Instruments", "FELUCCA", "MENU", "HOME", "ENV DEST 2/2", "ANLG", "SUPER SAW", "WIRE",
    "oeo coco eco nono", "minimum hello level", "decay release sound", "Pitch Cutoff", "bdpq doob", "-1 +2.5",
};
#define NSTR (sizeof STR / sizeof STR[0])
#define W 240
#define H 40
static uint16_t ref[W * H];

/* --- the prototype's text path (gfx_aa.h), on a plain frame buffer --- */
static uint16_t rv[16];
static void ref_ramp(uint16_t fg, uint16_t bg)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    const uint8_t *cv = ux.light ? CURVE_LIGHT : CURVE_DARK;
    for (uint32_t a = 0; a < 16u; a++) {
        uint32_t out = 0;
        if (ux.mono) {
            int32_t x = bg >> 11, y = fg >> 11;
            rv[a] = ux_gray((uint32_t)(x + (((y - x) * (int32_t)cv[a] + 128) >> 8)));
            continue;
        }
        for (uint32_t k = 0; k < 3u; k++) {
            int32_t x = (bg >> SH[k]) & MK[k], y = (fg >> SH[k]) & MK[k];
            out |= (uint32_t)(x + (((y - x) * (int32_t)cv[a] + 128) >> 8)) << SH[k];
        }
        rv[a] = (uint16_t)out;
    }
}
static int32_t ref_at(const aafont_t *f, uint32_t ch)       /* the glyph's number in the face, -1 = none */
{
    if (ch >= f->first && ch <= f->last) return (int32_t)(ch - f->first);
    for (uint32_t i = 0; i < f->nex; i++)
        if (f->ex[i] == ch) return (int32_t)(f->last - f->first + 1 + i);
    return -1;
}
static uint32_t ref_gi(const aafont_t *f, uint32_t ch)      /* the glyph's number (its phases: g[n << psh] ..) */
{
    int32_t k;
    if (ch >= 'a' && ch <= 'z' && f->last < 'a') ch -= 32u;
    k = ref_at(f, ch);
    if (k < 0 && ch == 0x85u) k = ref_at(f, '.');          /* the ellipsis */
    if (k < 0) k = ref_at(f, '?');
    return k < 0 ? 0u : (uint32_t)k;                        /* a sparse face (L) without '?': its first glyph */
}
static int32_t ref_kern(const aafont_t *f, uint32_t a, uint32_t b)
{
    uint32_t key = (a << 8) | b, lo = 0, hi = f->nk;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2u;
        if (f->kkey[m] == key) return f->kd[m];
        if (f->kkey[m] < key) lo = m + 1u; else hi = m;
    }
    return 0;
}
/* a glyph's alpha values: 2 px per byte, or (f->hc) the Huffman bit stream of tools/gen_aa_font.py, decoded
 * here by matching each (length, code) against the canonical code table built from hc's length counts */
static uint8_t alpha[64 * 64];
static void ref_unpack(const aafont_t *f, const aag_t *g)
{
    uint32_t n = (uint32_t)g->bw * g->bh, k = 0, pos = 0;
    const uint8_t *d = f->data + g->off;
    if (!f->hc) {
        for (k = 0; k < n; k++) alpha[k] = (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
        return;
    }
    while (k < n) {
        uint32_t code = 0, len, s = 0, found = 0;
        for (len = 1; len <= 15u && !found; len++) {
            uint32_t c = 0, sym = 15, l;
            code = code << 1 | ((d[pos >> 3] >> (7u - (pos & 7u))) & 1u);
            pos++;
            for (l = 1; l <= 15u && !found; l++, c <<= 1)       /* canonical: codes of length l, in order */
                for (uint32_t i = 0; i < f->hc[l - 1u]; i++, c++, sym++)
                    if (l == len && c == code) { s = f->hc[sym]; found = 1; break; }
        }
        for (uint32_t r = 0; r <= (s >> 4) && k < n; r++) alpha[k++] = (uint8_t)(s & 15u);
    }
}

static int32_t ref_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t pen = 0, err = 0, n = 1 << f->psh, step = 16 / n;
    uint32_t prev = 0;
    ref_ramp(fg, bg);
    for (; *s; s++) {
        uint32_t c = (uint8_t)*s, k = 0;
        const aag_t *g;
        int32_t lo, hi, pos;
        if (c >= 'a' && c <= 'z' && f->last < 'a') c -= 32u;
        if (prev) pen += ref_kern(f, prev, c);
        g = &f->g[ref_gi(f, c) * (uint32_t)n];
        if (n > 1 && g[1].off != g[0].off) {
            lo = step * (int32_t)floor((double)pen / step);       /* the phase positions around the pen */
            hi = lo + step;
            pos = fabs((double)(lo - pen - err)) <= fabs((double)(hi - pen - err)) ? lo : hi;
            g += ((pos / step) % n + n) % n;
        } else {
            pos = 16 * (int32_t)floor((pen + 8) / 16.0);           /* one phase: the rounded pen */
        }
        err = pos - pen;
        ref_unpack(f, g);
        for (uint32_t gy = 0; gy < g->bh; gy++)
            for (uint32_t gx = 0; gx < g->bw; gx++, k++) {
                uint32_t v = alpha[k];
                int32_t px = x + (int32_t)floor(pos / 16.0) + g->bx + (int32_t)gx, py = y + g->by + (int32_t)gy;
                if (v && (uint32_t)px < W && (uint32_t)py < H) ref[py * W + px] = v == 15u ? fg : rv[v];
            }
        pen += g->adv;
        prev = c;
    }
    return x + ((pen + 8) >> 4);
}

int main(void)
{
    const aafont_t *F[3] = {&AF_S, &AF_M, &AF_L};
    uint32_t n = 0, bad_px = 0, bad_pen = 0;
    for (uint32_t p = 0; p < NPALETTES; p++) {
        palette_set(p);
        for (uint32_t k = 0; k < 2u; k++)
            for (uint32_t f = 0; f < 3u; f++)
                for (uint32_t s = 0; s < NSTR; s++) {
                    uint16_t fg = k ? T_TEXT : T_THEME, bg = k ? T_BG : T_SURF;
                    int32_t e0, e1;
                    for (uint32_t i = 0; i < W * H; i++) ref[i] = bg;
                    e0 = ref_text(3, 2, F[f], STR[s], fg, bg);
                    cv_begin(W, H, bg);
                    e1 = cv_text(3, 2, F[f], STR[s], fg);
                    bad_pen += e0 != e1;
                    for (uint32_t i = 0; i < W * H; i++) bad_px += swap16(cv_px[i]) != ref[i];
                    n++;
                }
    }
    printf("text: %u strings x fonts x palettes against the mock renderer: %u end pens and %u pixels differ %s\n",
           n, bad_pen, bad_px, bad_pen || bad_px ? "FAIL" : "ok");
    return bad_pen || bad_px ? 1 : 0;
}
