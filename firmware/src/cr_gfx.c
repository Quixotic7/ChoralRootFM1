/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: what gfx.c's canvas does not have, drawn on its canvas (cv_px, cv_w, cv_h, cv_oy). Included
 * after gfx.c (one compilation unit). Integer only, no allocation.
 *   - anti-aliased shapes blended into what the canvas holds: fractional rectangles, discs, arcs (round caps,
 *     dashes: the ring's dotted circle), polylines with round joins (dashes: the arp's hop), quadratic curves;
 *   - scalable text: a face's glyphs resampled to any size and squeezed horizontally (area / box filter of the
 *     4-bit alpha, separable, in 1/256 px), so two big faces (build/gen/cr_fonts.h: CRX 104 px, the chord
 *     charset; CRB 40 px, ASCII; Inter Tight 700, Huffman-coded) give every size of the mock-ups, and the chord
 *     name's squeeze is the same resampling with a horizontal factor.
 * Coordinates are the screen's (cr_draw sets cv_oy = -the strip's top row); positions in 1/16 px (Q4) or 1/256 px
 * (Q8) where they say so; scales in Q12 (4096 = 1:1); angles in 1/65536 turn, 0 at 3 o'clock, clockwise (as the
 * canvas). cr_clip limits every cr_* draw (not gfx.c's own) to a rectangle of the screen. */
#include "cr_fonts.h"                     /* AF_CRX 104 px, AF_CRB 40 px (tools/gen_aa_font.py --preset choralroot) */

#define CR_GLYPH_MAX 8192u                /* the largest glyph, bw x bh (CRX: 'W' 104 x 76; tests/cr_draw_test.c) */
#define CR_KMAX 16u                       /* source pixels under one destination pixel, per axis (scale >= 1/14) */

static struct { int16_t x0, y0, x1, y1; } cr_clip = {0, 0, 240, 240};
static uint8_t cr_gbuf[CR_GLYPH_MAX] __attribute__((section(".pool")));      /* a decoded glyph, a pixel per byte */
static const aag_t *cr_gbuf_g;                                                 /* .. which */
static struct { uint16_t c0, n, w[CR_KMAX]; } cr_cw[256] __attribute__((section(".pool")));

static void cr_clip_set(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    cr_clip.x0 = (int16_t)(x0 < 0 ? 0 : x0);
    cr_clip.y0 = (int16_t)(y0 < 0 ? 0 : y0);
    cr_clip.x1 = (int16_t)(x1 > 240 ? 240 : x1);
    cr_clip.y1 = (int16_t)(y1 > 240 ? 240 : y1);
}
static void cr_clip_all(void) { cr_clip_set(0, 0, 240, 240); }

/* the screen rows the canvas holds, within the clip */
static int32_t cr_row0(void) { int32_t r = -cv_oy; return r > cr_clip.y0 ? r : cr_clip.y0; }
static int32_t cr_row1(void) { int32_t r = (int32_t)cv_h - cv_oy; return r < cr_clip.y1 ? r : cr_clip.y1; }

/* blend colour c over the canvas pixel *p by a (1..256) */
static inline void cr_mix(uint16_t *p, uint16_t c, uint32_t a)
{
    uint32_t d, r, g, b, na;
    if (a >= 256u) {
        *p = swap16(c);
        return;
    }
    d = swap16(*p);
    na = 256u - a;
    r = ((d >> 11) * na + (uint32_t)(c >> 11) * a + 128u) >> 8;
    g = (((d >> 5) & 63u) * na + ((uint32_t)(c >> 5) & 63u) * a + 128u) >> 8;
    b = ((d & 31u) * na + ((uint32_t)c & 31u) * a + 128u) >> 8;
    *p = swap16((uint16_t)(r << 11 | g << 5 | b));
}

/* blend colour c over the canvas pixel at screen (x, y) by a (0..256) */
static void cr_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    if (!a || x < cr_clip.x0 || x >= cr_clip.x1 || y < cr_clip.y0 || y >= cr_clip.y1)
        return;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h)
        return;
    cr_mix(&cv_px[(uint32_t)y * cv_w + (uint32_t)x], c, a);
}

/* a solid rectangle, whole pixels, inside the clip */
static void cr_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h, r0 = cr_row0(), r1 = cr_row1(), i, j;
    uint16_t sc = swap16(c);
    if (x < cr_clip.x0) x = cr_clip.x0;
    if (x1 > cr_clip.x1) x1 = cr_clip.x1;
    if (y < r0) y = r0;
    if (y1 > r1) y1 = r1;
    for (j = y; j < y1; j++) {
        uint16_t *row = cv_px + (uint32_t)(j + cv_oy) * cv_w;
        for (i = x; i < x1; i++)
            row[i] = sc;
    }
}

/* a rectangle with fractional edges (Q4), the edge pixels blended by their coverage */
static void cr_frect(int32_t x16, int32_t y16, int32_t w16, int32_t h16, uint16_t c)
{
    int32_t xe = x16 + w16, ye = y16 + h16, px0 = x16 >> 4, py0 = y16 >> 4, px1 = (xe + 15) >> 4, py1 = (ye + 15) >> 4;
    int32_t i, j;
    if (py0 < cr_row0()) py0 = cr_row0();
    if (py1 > cr_row1()) py1 = cr_row1();
    for (j = py0; j < py1; j++) {
        int32_t oy = (ye < (j + 1) * 16 ? ye : (j + 1) * 16) - (y16 > j * 16 ? y16 : j * 16);
        if (oy <= 0) continue;
        for (i = px0; i < px1; i++) {
            int32_t ox = (xe < (i + 1) * 16 ? xe : (i + 1) * 16) - (x16 > i * 16 ? x16 : i * 16);
            if (ox > 0) cr_blend(i, j, c, (uint32_t)(ox * oy));
        }
    }
}

/* ------------------------------------------------------------ maths --- */
static const int16_t CR_SIN[65] = {     /* sin, a quarter turn in 64 steps, Q14 */
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005,
    7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
static const uint16_t CR_ATAN[65] = {   /* atan(k / 64) in 1/65536 turn */
    0, 163, 326, 489, 651, 813, 975, 1136, 1297, 1457, 1617, 1775, 1933, 2090, 2246, 2401, 2555, 2708, 2860, 3010,
    3159, 3307, 3453, 3599, 3742, 3884, 4025, 4164, 4302, 4438, 4572, 4705, 4836, 4966, 5094, 5220, 5344, 5467,
    5589, 5708, 5826, 5943, 6058, 6171, 6282, 6392, 6500, 6607, 6712, 6815, 6917, 7018, 7117, 7214, 7310, 7405,
    7498, 7589, 7679, 7768, 7856, 7942, 8026, 8110, 8192};

/* sin of a (1/65536 turn), Q14, interpolated */
static int32_t cr_sin(uint32_t a)
{
    uint32_t q = (a >> 14) & 3u, k = a & 16383u, i, f;
    int32_t v;
    if (q & 1u) k = 16384u - k;
    i = k >> 8;
    f = k & 255u;
    v = i >= 64u ? CR_SIN[64] : CR_SIN[i] + (((CR_SIN[i + 1u] - CR_SIN[i]) * (int32_t)f) >> 8);
    return q & 2u ? -v : v;
}
static int32_t cr_cos(uint32_t a) { return cr_sin(a + 16384u); }

/* the angle of (x, y), 1/65536 turn, 0 along +x, clockwise with y down */
static uint32_t cr_atan2(int32_t y, int32_t x)
{
    uint32_t ax = (uint32_t)(x < 0 ? -x : x), ay = (uint32_t)(y < 0 ? -y : y), r, i, f, a;
    if (!ax && !ay) return 0;
    if (ay <= ax) {
        r = (ay << 12) / ax;              /* 0..4096 */
        i = r >> 6; f = r & 63u;
        a = CR_ATAN[i] + (i < 64u ? ((CR_ATAN[i + 1u] - CR_ATAN[i]) * f) >> 6 : 0u);
    } else {
        r = (ax << 12) / ay;
        i = r >> 6; f = r & 63u;
        a = 16384u - (CR_ATAN[i] + (i < 64u ? ((CR_ATAN[i + 1u] - CR_ATAN[i]) * f) >> 6 : 0u));
    }
    if (x < 0) a = 32768u - a;
    if (y < 0) a = 65536u - a;
    return a & 65535u;
}

static uint32_t cr_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* ---------------------------------------------------------- shapes --- */
/* the 4 x 4 sample offsets of a pixel, Q4 from its top-left corner */
static const uint8_t CR_SS[4] = {2, 6, 10, 14};

/* a filled disc, centre and radius Q4 */
static void cr_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x0 = (cx - r) >> 4, x1 = (cx + r + 15) >> 4, y0 = (cy - r) >> 4, y1 = (cy + r + 15) >> 4, i, j;
    int32_t rr = r * r;
    if (y0 < cr_row0()) y0 = cr_row0();
    if (y1 > cr_row1()) y1 = cr_row1();
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) {
            uint32_t n = 0, a, b;
            for (b = 0; b < 4u; b++)
                for (a = 0; a < 4u; a++) {
                    int32_t dx = i * 16 + CR_SS[a] - cx, dy = j * 16 + CR_SS[b] - cy;
                    n += dx * dx + dy * dy <= rr;
                }
            cr_blend(i, j, c, n * 16u);
        }
}

/* an arc: centre (cx, cy), radius r and width w (Q4), from angle a0 for `sweep` (65536 = the whole circle),
 * round end caps (caps), dashes along it (dash_on of every dash_per, Q4 px of the arc at radius r; 0 = solid) */
typedef struct {
    int32_t cx, cy, r, ro, ri, ro2, ri2, hw2, ex0, ey0, ex1, ey1, circ, dash_on, dash_per;
    uint32_t a0, sweep;
    int caps, full;
} cr_arcg_t;

static void cr_arc_geom(cr_arcg_t *g, int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t a0, uint32_t sweep,
                        int caps, int32_t dash_on, int32_t dash_per)
{
    g->cx = cx; g->cy = cy; g->r = r;
    g->ro = r + w / 2; g->ri = r - w / 2; g->ro2 = g->ro * g->ro; g->ri2 = g->ri * g->ri; g->hw2 = (w / 2) * (w / 2);
    g->ex0 = cx + ((r * cr_cos(a0)) >> 14); g->ey0 = cy + ((r * cr_sin(a0)) >> 14);
    g->ex1 = cx + ((r * cr_cos(a0 + sweep)) >> 14); g->ey1 = cy + ((r * cr_sin(a0 + sweep)) >> 14);
    g->circ = (r * 6434) >> 10;           /* the circumference, Q4 */
    g->a0 = a0; g->sweep = sweep; g->caps = caps; g->full = sweep >= 65536u;
    g->dash_on = dash_on; g->dash_per = dash_per;
}

/* the samples of pixel (i, j) the arc covers (0..16); 0 at once for a pixel well off its band */
static uint32_t cr_arc_px(const cr_arcg_t *g, int32_t i, int32_t j)
{
    int32_t pdx = i * 16 + 8 - g->cx, pdy = j * 16 + 8 - g->cy, pd2 = pdx * pdx + pdy * pdy, pd, ang = -1;
    uint32_t pa, n = 0, a, b;
    if (pd2 > (g->ro + 12) * (g->ro + 12) || (pd2 < (g->ri - 12) * (g->ri - 12) && g->ri > 12)) return 0;
    pa = cr_atan2(pdy, pdx);
    pd = (int32_t)cr_isqrt((uint32_t)pd2);
    if (pd < 1) pd = 1;
    /* a sample's angle is the pixel's moved by at most |offset| (<= 6 sqrt 2 Q4) x 10430 / pd: a pixel whose centre
     * is that far inside the sweep has every sample's angle in it, that far outside none (undashed: no sample's
     * angle needed then) */
    if (!g->full && !g->dash_per) {
        uint32_t rc = (pa - g->a0) & 65535u, dev = (uint32_t)(88508 * (pd + 1) / (pd * pd)) + 1u;
        if (rc >= dev && rc + dev <= g->sweep) ang = 1;
        else if (rc > g->sweep + dev && rc + dev < 65536u) ang = 0;
    }
    for (b = 0; b < 4u; b++)
        for (a = 0; a < 4u; a++) {
            int32_t ox = (int32_t)CR_SS[a] - 8, oy = (int32_t)CR_SS[b] - 8;
            int32_t dx = pdx + ox, dy = pdy + oy, d2 = dx * dx + dy * dy, in = 0;
            if (d2 <= g->ro2 && d2 >= g->ri2) {
                if (ang >= 0) {
                    in = ang;
                } else {
                    /* the sample's angle: the pixel's, moved by the tangential offset / radius */
                    uint32_t sa = (pa + (uint32_t)((((-pdy * ox + pdx * oy) * 10430) / pd) / pd)) & 65535u;
                    uint32_t rel = (sa - g->a0) & 65535u;
                    in = g->full || rel <= g->sweep;
                    if (in && g->dash_per) {
                        int32_t s = (int32_t)((rel * (uint32_t)g->circ) >> 16);
                        in = s % g->dash_per < g->dash_on;
                    }
                }
            }
            if (!in && g->caps) {
                int32_t ax = i * 16 + CR_SS[a] - g->ex0, ay = j * 16 + CR_SS[b] - g->ey0;
                int32_t bx = i * 16 + CR_SS[a] - g->ex1, by = j * 16 + CR_SS[b] - g->ey1;
                in = ax * ax + ay * ay <= g->hw2 || bx * bx + by * by <= g->hw2;
            }
            n += (uint32_t)in;
        }
    return n;
}

static void cr_arc(int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t a0, uint32_t sweep, int caps,
                   int32_t dash_on, int32_t dash_per, uint16_t c)
{
    cr_arcg_t g;
    int32_t x0, x1, y0, y1, i, j;
    cr_arc_geom(&g, cx, cy, r, w, a0, sweep, caps, dash_on, dash_per);
    x0 = (cx - g.ro) >> 4; x1 = (cx + g.ro + 15) >> 4; y0 = (cy - g.ro) >> 4; y1 = (cy + g.ro + 15) >> 4;
    if (y0 < cr_row0()) y0 = cr_row0();
    if (y1 > cr_row1()) y1 = cr_row1();
    if (x0 < cr_clip.x0) x0 = cr_clip.x0;
    if (x1 > cr_clip.x1) x1 = cr_clip.x1;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++)
            cr_blend(i, j, c, cr_arc_px(&g, i, j) * 16u);
}

/* a polyline of n points (Q4 pairs), w wide (Q4), round joins and caps; dashes (dash_on of every dash_per Q4 px
 * along it; 0 = solid). A pixel's samples are tested against the segments near it (a sample is in when any segment
 * covers it: joins blend once). Per row, the segments whose box (grown by w / 2) reaches the row's samples; per
 * pixel, those of them reaching its samples' columns: a pixel no segment reaches is skipped at once (the boxes are
 * the per-sample test's own, so the pixels are the same as testing every segment: a curve across the screen, the
 * arp's hop, costs its own pixels, not its box's) */
#define CR_POLY_MAX 48u
static void cr_poly(const int16_t *p, uint32_t n, int32_t w, int32_t dash_on, int32_t dash_per, uint16_t c)
{
    int32_t x0 = 99999, y0 = 99999, x1 = -99999, y1 = -99999, hw = w / 2, hw2 = (w / 2) * (w / 2), i, j;
    int32_t len[CR_POLY_MAX], s0[CR_POLY_MAX];
    int16_t bx0[CR_POLY_MAX], bx1[CR_POLY_MAX], by0[CR_POLY_MAX], by1[CR_POLY_MAX];
    uint8_t row[CR_POLY_MAX], near[CR_POLY_MAX];
    uint32_t k, nrow, nnear, q;
    if (n < 2u) return;
    if (n > CR_POLY_MAX) n = CR_POLY_MAX;
    for (k = 0; k < n; k++) {
        if (p[2 * k] < x0) x0 = p[2 * k];
        if (p[2 * k] > x1) x1 = p[2 * k];
        if (p[2 * k + 1] < y0) y0 = p[2 * k + 1];
        if (p[2 * k + 1] > y1) y1 = p[2 * k + 1];
    }
    for (k = 0; k + 1u < n; k++) {
        int32_t ax = p[2 * k], ay = p[2 * k + 1], bx = p[2 * k + 2], by = p[2 * k + 3], dx = bx - ax, dy = by - ay;
        len[k] = (int32_t)cr_isqrt((uint32_t)(dx * dx + dy * dy));
        s0[k] = k ? s0[k - 1] + len[k - 1] : 0;
        bx0[k] = (int16_t)((ax < bx ? ax : bx) - hw);
        bx1[k] = (int16_t)((ax > bx ? ax : bx) + hw);
        by0[k] = (int16_t)((ay < by ? ay : by) - hw);
        by1[k] = (int16_t)((ay > by ? ay : by) + hw);
    }
    x0 = (x0 - hw) >> 4; y0 = (y0 - hw) >> 4; x1 = (x1 + hw + 15) >> 4; y1 = (y1 + hw + 15) >> 4;
    if (y0 < cr_row0()) y0 = cr_row0();
    if (y1 > cr_row1()) y1 = cr_row1();
    if (x0 < cr_clip.x0) x0 = cr_clip.x0;
    if (x1 > cr_clip.x1) x1 = cr_clip.x1;
    for (j = y0; j < y1; j++) {
        int32_t ry0 = j * 16 + CR_SS[0], ry1 = j * 16 + CR_SS[3];
        int32_t ix0 = 99999, ix1 = -99999;
        for (k = 0, nrow = 0; k + 1u < n; k++)
            if (by1[k] >= ry0 && by0[k] <= ry1) {
                row[nrow++] = (uint8_t)k;
                if (bx0[k] < ix0) ix0 = bx0[k];
                if (bx1[k] > ix1) ix1 = bx1[k];
            }
        if (!nrow) continue;
        ix0 = (ix0 - CR_SS[3]) >> 4;                 /* (the row's boxes: the pixels whose samples they reach) */
        ix1 = ((ix1 - CR_SS[0]) >> 4) + 1;
        if (ix0 < x0) ix0 = x0;
        if (ix1 > x1) ix1 = x1;
        for (i = ix0; i < ix1; i++) {
            int32_t rx0 = i * 16 + CR_SS[0], rx1 = i * 16 + CR_SS[3], cxp = i * 16 + 8, cyp = j * 16 + 8;
            uint32_t cnt = 0, a, b, all = 0;
            for (q = 0, nnear = 0; q < nrow && !all; q++) {
                /* the pixel's centre against the segment, exact for its 16 samples (each within 8.5 Q4 of the
                 * centre): every sample out (far: skip it), or every one in (solid: the pixel is covered) */
                int32_t ax, ay, ux, uy, vx, vy, dot, cr, l1, m;
                k = row[q];
                if (bx1[k] < rx0 || bx0[k] > rx1) continue;
                ax = p[2 * k]; ay = p[2 * k + 1];
                ux = p[2 * k + 2] - ax; uy = p[2 * k + 3] - ay; vx = cxp - ax; vy = cyp - ay;
                dot = vx * ux + vy * uy;
                cr = vx * uy - vy * ux;
                if (cr < 0) cr = -cr;
                l1 = len[k] + 1;
                m = 9 * l1;                                  /* (a sample's reach along / across, x the length) */
                if (cr > (hw + 9) * l1) continue;            /* off the line by more than the width: far */
                if (dot <= 0 && vx * vx + vy * vy > (hw + 9) * (hw + 9)) continue;
                if (dot >= l1 * l1) {
                    int32_t qx = cxp - p[2 * k + 2], qy = cyp - p[2 * k + 3];
                    if (qx * qx + qy * qy > (hw + 9) * (hw + 9)) continue;
                }
                if (!dash_per && len[k] && dot > m && dot < len[k] * len[k] - m && cr <= hw * len[k] - m) {
                    all = 1;                                 /* inside the stroke, away from its ends: solid */
                    break;
                }
                near[nnear++] = (uint8_t)k;
            }
            if (all) {
                cr_blend(i, j, c, 256u);
                continue;
            }
            if (!nnear) continue;
            for (b = 0; b < 4u; b++)
                for (a = 0; a < 4u; a++) {
                    int32_t sx = i * 16 + CR_SS[a], sy = j * 16 + CR_SS[b];
                    int in = 0;
                    for (q = 0; q < nnear && !in; q++) {
                        int32_t ax, ay, ux, uy, vx, vy, dot, cr, along;
                        k = near[q];
                        if (sx < bx0[k] || sx > bx1[k] || sy < by0[k] || sy > by1[k]) continue;
                        ax = p[2 * k]; ay = p[2 * k + 1];
                        ux = p[2 * k + 2] - ax; uy = p[2 * k + 3] - ay; vx = sx - ax; vy = sy - ay;
                        dot = vx * ux + vy * uy;
                        if (len[k] && dot > 0 && dot < len[k] * len[k]) {
                            cr = vx * uy - vy * ux;
                            if (cr < 0) cr = -cr;
                            if (cr > hw * len[k]) continue;
                            along = s0[k] + dot / len[k];
                        } else {
                            int32_t qx = dot <= 0 ? vx : sx - p[2 * k + 2], qy = dot <= 0 ? vy : sy - p[2 * k + 3];
                            if (qx * qx + qy * qy > hw2) continue;
                            along = dot <= 0 ? s0[k] : s0[k] + len[k];
                        }
                        in = !dash_per || along % dash_per < dash_on;
                    }
                    cnt += (uint32_t)in;
                }
            cr_blend(i, j, c, cnt * 16u);
        }
    }
}
/* a quadratic curve from (ax, ay) by (bx, by) to (cx, cy), Q4, as a polyline of 16 segments */
static void cr_quad(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy, int32_t w,
                    int32_t dash_on, int32_t dash_per, uint16_t c)
{
    int16_t p[34];
    int32_t t;
    for (t = 0; t <= 16; t++) {
        int32_t u = 16 - t;               /* (u^2 a + 2 u t b + t^2 c) / 256 */
        p[2 * t] = (int16_t)((u * u * ax + 2 * u * t * bx + t * t * cx + 128) >> 8);
        p[2 * t + 1] = (int16_t)((u * u * ay + 2 * u * t * by + t * t * cy + 128) >> 8);
    }
    cr_poly(p, 17u, w, dash_on, dash_per, c);
}

/* ------------------------------------------------------------ text --- */
/* the em of a face in px (aafont_t does not keep it) */
static uint32_t cr_face_px(const aafont_t *f)
{
    return f == &AF_CRX ? 104u : f == &AF_CRB ? 40u : f == &AF_L ? 28u : f == &AF_M ? 15u : 12u;
}
/* the phase-0 glyph of ch (no case folding: CRX is sparse but has lower case); not in the face: the ellipsis as
 * '.', else '?', else the first glyph */
static const aag_t *cr_glyph_of(const aafont_t *f, uint32_t ch)
{
    int32_t k = glyph_at(f, ch);
    if (k < 0 && ch == (uint8_t)ELLIPSIS) k = glyph_at(f, '.');
    if (k < 0) k = glyph_at(f, '?');
    if (k < 0) k = 0;
    return &f->g[(uint32_t)k << f->psh];
}
/* every character of s is in f */
static int cr_covers(const aafont_t *f, const char *s)
{
    for (; *s; s++)
        if (glyph_at(f, (uint8_t)*s) < 0) return 0;
    return 1;
}
/* the advance of s in f drawn at scale sc (Q12), in Q8 px */
static int32_t cr_adv8(const aafont_t *f, const char *s, int32_t sc)
{
    int32_t pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = (uint8_t)*s;
        if (prev) pen += kern(f, prev, c);
        pen += cr_glyph_of(f, c)->adv;
        prev = c;
    }
    return (pen * sc) >> 8;               /* 1/16 px x Q12 -> Q8 */
}

/* Huffman tables of the cr faces (gfx.c's hc_lut keeps two, for M and L) */
static struct { const uint8_t *hc; uint16_t lut[256]; } cr_hc[2] __attribute__((section(".pool")));
static uint8_t cr_hc_next;
static const uint16_t *cr_hc_lut(const uint8_t *hc)
{
    uint32_t k, len, code = 0, sym = 15, i;
    for (k = 0; k < 2u; k++)
        if (cr_hc[k].hc == hc) return cr_hc[k].lut;
    k = cr_hc_next;
    cr_hc_next ^= 1u;
    cr_hc[k].hc = hc;
    for (i = 0; i < 256u; i++)
        cr_hc[k].lut[i] = 0;
    for (len = 1; len <= 8u; len++, code <<= 1)
        for (i = 0; i < hc[len - 1u]; i++, code++, sym++) {
            uint32_t a = code << (8u - len), m = 1u << (8u - len);
            while (m--)
                cr_hc[k].lut[a + m] = (uint16_t)(hc[sym] | len << 8);
        }
    return cr_hc[k].lut;
}

/* glyph g of f into cr_gbuf, a value 0..15 per pixel; 0 when it does not fit */
static int cr_decode(const aafont_t *f, const aag_t *g)
{
    uint32_t n = (uint32_t)g->bw * g->bh, k = 0;
    const uint8_t *d = f->data + g->off;
    if (n > CR_GLYPH_MAX) return 0;
    if (cr_gbuf_g == g) return 1;
    if (!f->hc) {
        for (k = 0; k < n; k++)
            cr_gbuf[k] = (uint8_t)((d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u);
    } else {                              /* gfx.c cv_alpha_hc's decoder, into the buffer */
        const uint16_t *lut = cr_hc_lut(f->hc);
        const uint8_t *hc = f->hc;
        uint32_t acc = 0, nb = 0;
        while (k < n) {
            uint32_t e, s, run, v;
            while (nb < 16u) {
                acc = acc << 8 | *d++;
                nb += 8;
            }
            e = lut[(acc >> (nb - 8u)) & 255u];
            if (e) {
                nb -= e >> 8;
                s = e & 255u;
            } else {
                uint32_t code = 0, first = 0, idx = 0, len = 0;
                for (;;) {
                    code |= (acc >> --nb) & 1u;
                    if (code - first < hc[len]) break;
                    idx += hc[len];
                    first = (first + hc[len]) << 1;
                    code <<= 1;
                    len++;
                }
                s = hc[15u + idx + code - first];
            }
            v = s & 15u;
            run = (s >> 4) + 1u;
            while (run-- && k < n)
                cr_gbuf[k++] = (uint8_t)v;
        }
    }
    cr_gbuf_g = g;
    return 1;
}

/* the glyph's top-left at (X, Y) Q8, scaled sx, sy (Q12), colour c blended over the canvas */
static void cr_glyph(const aafont_t *f, const aag_t *g, int32_t X, int32_t Y, int32_t sx, int32_t sy, uint16_t c)
{
    const uint8_t *cv = ux.light ? CURVE_LIGHT : CURVE_DARK;
    int32_t bw = g->bw, bh = g->bh, dx0, dx1, dy0, dy1, i, j, r0 = cr_row0(), r1 = cr_row1();
    if (!bw || !cr_decode(f, g)) return;
    if (sx == 4096 && sy == 4096) {       /* 1:1: whole pixels */
        int32_t xi = (X + 128) >> 8, yi = (Y + 128) >> 8;
        for (j = 0; j < bh; j++) {
            if (yi + j < r0 || yi + j >= r1) continue;
            for (i = 0; i < bw; i++) {
                uint32_t v = cr_gbuf[j * bw + i];
                if (v) cr_blend(xi + i, yi + j, c, cv[v] + (cv[v] >> 7));
            }
        }
        return;
    }
    dx0 = X >> 8;
    dx1 = (X + ((bw * sx) >> 4) + 255) >> 8;
    dy0 = Y >> 8;
    dy1 = (Y + ((bh * sy) >> 4) + 255) >> 8;
    if (dx0 < cr_clip.x0) dx0 = cr_clip.x0;
    if (dx1 > cr_clip.x1) dx1 = cr_clip.x1;
    if (dx1 - dx0 > 256) dx1 = dx0 + 256;
    if (dy0 < r0) dy0 = r0;
    if (dy1 > r1) dy1 = r1;
    if (dx0 >= dx1 || dy0 >= dy1) return;
    for (i = dx0; i < dx1; i++) {         /* the columns: source columns under each and their overlap (Q8) */
        int32_t lo = i * 256, hi = lo + 256, s = (lo - X) * 16 / sx, n = 0;
        if (s < 0) s = 0;
        while (s > 0 && X + ((s * sx) >> 4) > lo) s--;
        cr_cw[i - dx0].c0 = (uint16_t)s;
        for (; s < bw && n < (int32_t)CR_KMAX; s++) {
            int32_t e0 = X + ((s * sx) >> 4), e1 = X + (((s + 1) * sx) >> 4);
            int32_t ov = (e1 < hi ? e1 : hi) - (e0 > lo ? e0 : lo);
            if (e0 >= hi) break;
            if (ov <= 0) { if (!n) cr_cw[i - dx0].c0 = (uint16_t)(s + 1); continue; }
            cr_cw[i - dx0].w[n++] = (uint16_t)(ov > 256 ? 256 : ov);
        }
        cr_cw[i - dx0].n = (uint16_t)n;
    }
    for (j = dy0; j < dy1; j++) {
        int32_t lo = j * 256, hi = lo + 256, s = (lo - Y) * 16 / sy, rs = 0, nr = 0;
        int32_t wy[CR_KMAX];
        if (s < 0) s = 0;
        while (s > 0 && Y + ((s * sy) >> 4) > lo) s--;
        for (; s < bh && nr < (int32_t)CR_KMAX; s++) {
            int32_t e0 = Y + ((s * sy) >> 4), e1 = Y + (((s + 1) * sy) >> 4);
            int32_t ov = (e1 < hi ? e1 : hi) - (e0 > lo ? e0 : lo);
            if (e0 >= hi) break;
            if (ov <= 0) continue;
            if (!nr) rs = s;
            wy[nr++] = ov > 256 ? 256 : ov;
        }
        if (!nr) continue;
        for (i = dx0; i < dx1; i++) {
            uint32_t sum = 0, a4;
            int32_t k, q, n = cr_cw[i - dx0].n, c0 = cr_cw[i - dx0].c0;
            for (q = 0; q < nr; q++) {
                const uint8_t *row = cr_gbuf + (rs + q) * bw + c0;
                uint32_t h = 0;
                for (k = 0; k < n; k++)
                    h += (uint32_t)row[k] * cr_cw[i - dx0].w[k];
                sum += h * (uint32_t)wy[q];
            }
            a4 = (sum + 32768u) >> 16;
            if (a4 > 15u) a4 = 15u;
            if (a4) cr_blend(i, j, c, cv[a4] + (cv[a4] >> 7));
        }
    }
}

/* s in face f, its pen starting at x (Q8) on baseline y (Q8), scaled sx, sy (Q12); the ink box goes to the layout
 * hook (gfx.c GFX_HOOK_TEXT, canvas rows); returns the pen at the end (Q8) */
static int32_t cr_text_sc(int32_t x, int32_t y, const aafont_t *f, const char *s, int32_t sx, int32_t sy, uint16_t c,
                          uint32_t flags)
{
    const char *s0 = s;
    int32_t pen = 0, bx0 = 0x7FFF, by0 = 0x7FFF, bx1 = -0x7FFF, by1 = -0x7FFF;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t ch = (uint8_t)*s;
        const aag_t *g;
        int32_t X, Y;
        if (prev) pen += kern(f, prev, ch);
        g = cr_glyph_of(f, ch);
        X = x + ((pen * sx) >> 8) + ((g->bx * sx) >> 4);
        Y = y + (((g->by - f->asc) * sy) >> 4);
        if (g->bw) {
            int32_t gx0 = X >> 8, gy0 = Y >> 8, gx1 = (X + ((g->bw * sx) >> 4) + 255) >> 8;
            int32_t gy1 = (Y + ((g->bh * sy) >> 4) + 255) >> 8;
            if (gy1 > cr_row0() && gy0 < cr_row1() && gx1 > cr_clip.x0 && gx0 < cr_clip.x1)
                cr_glyph(f, g, X, Y, sx, sy, c);
            gx0 = (X + 128) >> 8;                                       /* the ink box: pixels at least half covered */
            gy0 = (Y + 128) >> 8;
            gx1 = (X + ((g->bw * sx) >> 4) + 128) >> 8;
            gy1 = (Y + ((g->bh * sy) >> 4) + 128) >> 8;
            if (gx0 < bx0) bx0 = gx0;
            if (gy0 < by0) by0 = gy0;
            if (gx1 > bx1) bx1 = gx1;
            if (gy1 > by1) by1 = gy1;
        }
        pen += g->adv;
        prev = ch;
    }
    if (bx1 > bx0) {
        if (bx0 < 0 || bx1 > (int32_t)cv_w || by0 + cv_oy < 0 || by1 + cv_oy > (int32_t)cv_h)
            flags |= 2u;
        GFX_HOOK_TEXT(bx0, by0 + cv_oy, bx1, by1 + cv_oy, s0, flags);
    }
    (void)s0;
    return x + ((pen * sx) >> 8);
}
