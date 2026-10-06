/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Renders of the firmware UI (firmware/src/ui*.c on tests/ui_test.c's stubs), the layout lint, the MONO check
 * and the host draw cost.
 *   ui_render OUTDIR [SLOTDIR]   (run by tests/run_tests.sh; tests/ui_render.py makes the PNGs;
 *                                 SLOTDIR: filmstrips of the rolling digits, MONO and GREEN)
 * Layout lint, over every screen below in every palette, every page of every engine and every value of every
 * column: the ink box of every text and icon drawn (gfx.c GFX_HOOK_TEXT) as it lands on the screen. A finding:
 * a box off the screen, cut by its canvas, partly covered by a later strip or fill, overlapping another box, or
 * ellipsised when it is not free text (labels and values must fit; names and messages may be ellipsised), or
 * spilling out of its cell: touching a rounded rectangle of its canvas (gfx.c cv_rrect: a cell, card, row, button,
 * GFX_HOOK_CELL) without lying inside it, though still on the screen (the FX map's REVERSE).
 * MONO: every pixel of every screen is RGB565 gray (R = B, G = 2 R).
 * Rolling digits: every frame of a header BPM roll and a card roll, up and down, in every palette (lint, MONO).
 * FM6 charts (ui_graph.c graph_fm6, its parts through FM6_CHART_HOOK), every algorithm and every chart drawn: no two
 * operator boxes overlapping or touching, no route, loop, bus or label inside a box or touching one it does not
 * connect, no two nets (one gap's routes that share an operator, the output, the loop, the label) sharing or
 * touching a pixel, everything inside the panel.
 * Output: OUTDIR/ppm/<PALETTE>_<screen>.ppm for MONO GREEN PAPER, OUTDIR/report.txt (findings, ellipsised free
 * text, the draw cost), OUTDIR/text_audit.tsv (MONO, per screen: texts, icons and keycaps with their ink-box px, the
 * ellipsised ones, the texts closer than 2 px to their cell's edge, the words).
 * Exit 1 on a finding or a MONO pixel off gray. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[28]; } rbox_t;
static rbox_t pend[256], scr[1200];
static int16_t cells[64][4];                    /* GFX_HOOK_CELL: the cells and cards of the canvas being drawn */
static uint32_t ncells, nspill;
static FILE *aud;                               /* OUTDIR/text_audit.tsv: MONO, the text of each screen */
static char tight[400];                         /* .. the texts of this screen closer than 2 px to their cell's edge */
static uint32_t ntight;
static uint32_t npend, nscr, nfind, nfree;
static uint64_t px_visited, n_text, blit_px;
static FILE *rep;
static const char *cur_name = "";
static char free_seen[64][40];
static uint32_t nfree_seen;
static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    n_text++;
    if (npend >= 256u) return;
    pend[npend] = (rbox_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)flags, {0}};
    strncpy(pend[npend].s, s, 27);
    npend++;
}
static void hk_cell(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (ncells < 64u) {
        cells[ncells][0] = (int16_t)x0; cells[ncells][1] = (int16_t)y0;
        cells[ncells][2] = (int16_t)x1; cells[ncells][3] = (int16_t)y1;
        ncells++;
    }
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h);
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) hk_blit(x, y, r0, cv_w, cv_h)
#define GFX_HOOK_BEGIN() (npend = ncells = 0)
#define GFX_HOOK_CELL(x0, y0, x1, y1) hk_cell(x0, y0, x1, y1)
#define GFX_HOOK_PIXELS(n) (px_visited += (n))
#define UI_FILL_HOOK(x, y, w, h) hk_fill(x, y, w, h)
static void fm6_hook(uint32_t kind, uint32_t a, uint32_t b);
#define FM6_CHART_HOOK(kind, a, b) fm6_hook(kind, a, b)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* the FM6 chart lint: each part of a chart (FMH_*) drawn alone onto a canvas of SENT, its pixels kept in fmp_mask
 * (bit: the part), then the canvas as drawn restored with the part on it; at FMH_END the parts are checked */
#define FMP_MAX 48u
#define FMP_SENT 0x0861u
static uint64_t fmp_mask[CV_MAX];
static uint16_t fmp_save[CV_MAX];
static uint8_t fmp_kind[FMP_MAX], fmp_a[FMP_MAX], fmp_b[FMP_MAX];
static uint32_t fmp_n, fmp_charts;
static void finding(const char *what, const rbox_t *b, const rbox_t *o);
static void fmp_find(uint32_t alg, const char *what, uint32_t i, int32_t x, int32_t y, uint32_t j)
{
    static const char *const KIND[] = {"box", "route", "carrier", "bus", "loop", "label"};
    rbox_t b = {(int16_t)x, (int16_t)y, (int16_t)x, (int16_t)y, 0, {0}}, o = b;
    snprintf(b.s, sizeof b.s, "ALG %u %s %u>%u", alg + 1u, KIND[fmp_kind[i]], fmp_a[i] + 1u, fmp_b[i] + 1u);
    snprintf(o.s, sizeof o.s, "%s %u>%u", j < FMP_MAX ? KIND[fmp_kind[j]] : "panel", j < FMP_MAX ? fmp_a[j] + 1u : 0u,
             j < FMP_MAX ? fmp_b[j] + 1u : 0u);
    finding(what, &b, &o);
}
static void fmp_check(uint32_t alg)
{
    int32_t bx0[FMP_MAX], by0[FMP_MAX], bx1[FMP_MAX], by1[FMP_MAX], x, y, dx, dy;
    uint32_t net[FMP_MAX], par[12], i, j, w = cv_w, h = cv_h;
    for (i = 0; i < 12u; i++) par[i] = i;               /* the routes' nets: an operator as source (0..5), as destination (6..11) */
    #define FMP_ROOT(v) ({ uint32_t r_ = (v); while (par[r_] != r_) r_ = par[r_]; r_; })
    for (i = 0; i < fmp_n; i++)
        if (fmp_kind[i] == FMH_ROUTE) par[FMP_ROOT(fmp_a[i])] = FMP_ROOT(6u + fmp_b[i]);
    for (i = 0; i < fmp_n; i++) {
        net[i] = fmp_kind[i] == FMH_ROUTE ? FMP_ROOT(6u + fmp_b[i]) : fmp_kind[i] == FMH_CAR || fmp_kind[i] == FMH_BUS ? 20u
                 : 21u + fmp_kind[i];
        bx0[i] = by0[i] = 9999; bx1[i] = by1[i] = -1;
    }
    #undef FMP_ROOT
    for (y = 0; y < (int32_t)h; y++)                     /* each part's extent */
        for (x = 0; x < (int32_t)w; x++)
            for (i = 0; i < fmp_n; i++)
                if (fmp_mask[y * w + x] >> i & 1u) {
                    if (x < bx0[i]) bx0[i] = x;
                    if (y < by0[i]) by0[i] = y;
                    if (x > bx1[i]) bx1[i] = x;
                    if (y > by1[i]) by1[i] = y;
                }
    for (i = 0; i < fmp_n; i++)                          /* boxes: apart, by a pixel at least */
        for (j = 0; j < i; j++)
            if (fmp_kind[i] == FMH_BOX && fmp_kind[j] == FMH_BOX && bx0[i] <= bx1[j] + 1 && bx0[j] <= bx1[i] + 1 &&
                by0[i] <= by1[j] + 1 && by0[j] <= by1[i] + 1)
                fmp_find(alg, "FM6 boxes overlap", i, bx0[i], by0[i], j);
    for (y = 0; y < (int32_t)h; y++)
        for (x = 0; x < (int32_t)w; x++) {
            uint64_t mk = fmp_mask[y * w + x];
            if (!mk) continue;
            for (i = 0; i < fmp_n; i++) {
                if (!(mk >> i & 1u) || fmp_kind[i] == FMH_BOX) continue;
                if (x < 6 || x > 233 || y < 2 || y > 119)
                    fmp_find(alg, "FM6 part off the panel", i, x, y, FMP_MAX);
                for (j = 0; j < fmp_n; j++) {
                    if (fmp_kind[j] != FMH_BOX) continue;
                    if (x >= bx0[j] && x <= bx1[j] && y >= by0[j] && y <= by1[j])
                        fmp_find(alg, "FM6 part through a box", i, x, y, j);
                    else if (x >= bx0[j] - 1 && x <= bx1[j] + 1 && y >= by0[j] - 1 && y <= by1[j] + 1 &&
                             !((fmp_kind[i] == FMH_ROUTE && (fmp_a[i] == fmp_a[j] || fmp_b[i] == fmp_a[j])) ||
                               ((fmp_kind[i] == FMH_CAR || fmp_kind[i] == FMH_FB) && fmp_a[i] == fmp_a[j])))
                        fmp_find(alg, "FM6 part touches a box", i, x, y, j);
                }
                for (dy = -1; dy <= 1; dy++)              /* another net on this pixel or next to it */
                    for (dx = -1; dx <= 1; dx++) {
                        int32_t nx = x + dx, ny = y + dy;
                        uint64_t mn;
                        if (nx < 0 || ny < 0 || nx >= (int32_t)w || ny >= (int32_t)h) continue;
                        mn = fmp_mask[ny * w + nx];
                        for (j = 0; j < fmp_n; j++)
                            if (mn >> j & 1u && fmp_kind[j] != FMH_BOX && net[j] != net[i])
                                fmp_find(alg, "FM6 nets cross or touch", i, x, y, j);
                    }
            }
        }
    fmp_charts++;
}
static void fm6_hook(uint32_t kind, uint32_t a, uint32_t b)
{
    uint32_t i, n = cv_w * cv_h;
    if (fmp_n) {                                         /* the part just drawn: its pixels */
        for (i = 0; i < n; i++)
            if (cv_px[i] != FMP_SENT) fmp_mask[i] |= (uint64_t)1u << (fmp_n - 1u);
            else cv_px[i] = fmp_save[i];
    } else {
        memset(fmp_mask, 0, n * sizeof fmp_mask[0]);
    }
    if (kind == FMH_END) {
        fmp_check(a);
        fmp_n = 0;
        return;
    }
    if (fmp_n >= FMP_MAX) { fprintf(stderr, "ui_render: an FM6 chart of more than %u parts\n", FMP_MAX); exit(1); }
    memcpy(fmp_save, cv_px, n * sizeof cv_px[0]);
    for (i = 0; i < n; i++) cv_px[i] = FMP_SENT;
    fmp_kind[fmp_n] = (uint8_t)kind; fmp_a[fmp_n] = (uint8_t)a; fmp_b[fmp_n] = (uint8_t)b;
    fmp_n++;
}

static void finding(const char *what, const rbox_t *b, const rbox_t *o)
{
    static char seen[600][64];
    static uint32_t nseen;
    char key[64];
    uint32_t i;
    nfind++;
    snprintf(key, sizeof key, "%.20s|%.14s|%.24s", cur_name, what, b->s);
    for (i = 0; i < nseen && strcmp(seen[i], key); i++) ;
    if (i < nseen) return;                       /* each finding once per screen */
    if (nseen < 600u) strcpy(seen[nseen++], key);
    else return;
    fprintf(rep, "LINT %-22s %-32s '%s' (%d,%d)-(%d,%d)", cur_name, what, b->s, b->x0, b->y0, b->x1, b->y1);
    if (o) fprintf(rep, " / '%s' (%d,%d)-(%d,%d)", o->s, o->x0, o->y0, o->x1, o->y1);
    fputc('\n', rep);
}
/* a rectangle of the screen is drawn again: boxes inside it are gone, boxes it covers in part are hidden */
static void cover(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t i, k = 0;
    for (i = 0; i < nscr; i++) {
        rbox_t *b = &scr[i];
        int in = b->x0 >= x0 && b->x1 <= x1 && b->y0 >= y0 && b->y1 <= y1;
        int touch = b->x0 < x1 && x0 < b->x1 && b->y0 < y1 && y0 < b->y1;
        if (in) continue;
        if (touch && !(b->flags & 48u)) finding("hidden in part by a later draw", b, 0);
        if (touch && (b->flags & 48u)) scr[k++] = *b;   /* split across bands / scrolled: drawn on purpose */
        else scr[k++] = *b;
    }
    nscr = k;
}
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { cover((int32_t)x, (int32_t)y, (int32_t)(x + w), (int32_t)(y + h)); }
/* a text across two bands (the menu and the document draw 124 + 85 rows): each band draws its part, the same
 * screen box twice, cut at the band edge; together they are whole (flag 16) */
static int split_pair(rbox_t *b)
{
    uint32_t j;
    if (!(b->flags & 2u)) return 0;
    for (j = 0; j < nscr; j++) {
        rbox_t *o = &scr[j];
        if ((o->flags & 2u) && o->x0 == b->x0 && o->x1 == b->x1 && o->y0 == b->y0 && o->y1 == b->y1 && !strcmp(o->s, b->s)) {
            o->flags = (uint8_t)((o->flags & ~2u) | 16u);
            return 1;
        }
    }
    return 0;
}
/* a text or icon box that touches a cell or card of its canvas (GFX_HOOK_CELL) but spills past its edge; for the
 * audit, a text inside its smallest cell closer than 2 px to an edge */
static void contain(int32_t x, int32_t y)
{
    uint32_t i, c;
    for (i = 0; i < npend; i++) {
        const rbox_t *b = &pend[i];
        int32_t best = 1 << 30, gap = 99;
        for (c = 0; c < ncells && !(b->flags & 4u); c++) {   /* (texts only: an icon's box is its whole cell) */
            const int16_t *r = cells[c];
            int32_t a = (r[2] - r[0]) * (r[3] - r[1]), g;
            if (b->x0 < r[0] || b->x1 > r[2] || b->y0 < r[1] || b->y1 > r[3] || a >= best) continue;
            best = a;
            g = b->x0 - r[0];
            if (r[2] - b->x1 < g) g = r[2] - b->x1;
            if (b->y0 - r[1] < g) g = b->y0 - r[1];
            if (r[3] - b->y1 < g) g = r[3] - b->y1;
            gap = g;
        }
        if (gap < 2 && aud) {
            ntight++;
            if (strlen(tight) + strlen(b->s) + 8u < sizeof tight)
                snprintf(tight + strlen(tight), sizeof tight - strlen(tight), "%s'%s' %dpx", tight[0] ? ", " : "", b->s, (int)gap);
        }
        for (c = 0; c < ncells; c++) {
            const int16_t *r = cells[c];
            int touch = b->x0 < r[2] && r[0] < b->x1 && b->y0 < r[3] && r[1] < b->y1;
            int in = b->x0 >= r[0] && b->x1 <= r[2] && b->y0 >= r[1] && b->y1 <= r[3];
            int around = r[0] >= b->x0 && r[2] <= b->x1 && r[1] >= b->y0 && r[3] <= b->y1;   /* (a mark inside the box) */
            if (touch && !in && !around) {
                rbox_t t = *b, o = {(int16_t)(r[0] + x), (int16_t)(r[1] + y), (int16_t)(r[2] + x), (int16_t)(r[3] + y), 0, "cell"};
                t.x0 += (int16_t)x; t.x1 += (int16_t)x; t.y0 += (int16_t)y; t.y1 += (int16_t)y;
                nspill++;
                finding("spills out of its cell", &t, &o);
                break;
            }
        }
    }
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h)
{
    uint32_t i, k = 0;
    contain((int32_t)x, (int32_t)y);
    ncells = 0;
    blit_px += (uint64_t)w * (h > r0 ? h - r0 : 0u);
    for (i = 0; i < npend; i++) {
        rbox_t b = pend[i];
        if (b.y1 <= (int32_t)r0) continue;           /* rows not blitted */
        if (b.y0 < (int32_t)r0) b.flags |= 2u;
        b.x0 += (int16_t)x; b.x1 += (int16_t)x; b.y0 += (int16_t)y; b.y1 += (int16_t)y;
        if (!split_pair(&b)) pend[k++] = b;
    }
    cover((int32_t)x, (int32_t)(y + r0), (int32_t)(x + w), (int32_t)(y + h));
    for (i = 0; i < k && nscr < 1200u; i++)
        scr[nscr++] = pend[i];
    npend = 0;
}

static void lint(void)
{
    uint32_t i, j;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        if (b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) finding("off the screen", b, 0);
        if ((b->flags & 2u) && !(b->flags & 32u)) finding("cut by its canvas", b, 0);
        if ((b->flags & 1u) && !(b->flags & 8u)) finding("ellipsised (not free text)", b, 0);
        if ((b->flags & 9u) == 9u) {
            for (j = 0; j < nfree_seen && strcmp(free_seen[j], b->s); j++) ;
            if (j == nfree_seen && nfree_seen < 64u) {
                snprintf(free_seen[nfree_seen++], 40, "%s", b->s);
                fprintf(rep, "free   %-22s ellipsised '%s'\n", cur_name, b->s);
            }
            nfree++;
        }
        for (j = i + 1u; j < nscr; j++) {
            const rbox_t *o = &scr[j];
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) finding("overlaps", b, o);
        }
    }
}

/* the audit of one screen (MONO): its texts (count, ink-box px), icons, keycaps, the ellipsised and tight texts,
 * and the words themselves */
static FILE *audf;
static void audit_scene(const char *name)
{
    uint32_t i, nt = 0, ni = 0, nk = 0, ne = 0, at = 0, ai = 0, ak = 0;
    char words[1200] = "";
    if (!audf) return;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        uint32_t a = (uint32_t)((b->x1 - b->x0) * (b->y1 - b->y0));
        if ((b->flags & 4u) && !strcmp(b->s, "icon")) { ni++; ai += a; continue; }
        if (b->flags & 4u) { nk++; ak += a; continue; }
        nt++; at += a;
        if (b->flags & 1u) ne++;
        if (strlen(words) + strlen(b->s) + 4u < sizeof words)
            snprintf(words + strlen(words), sizeof words - strlen(words), "%s%s", words[0] ? " | " : "", b->s);
    }
    fprintf(audf, "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\n", name, nt, at, ni, ai, nk, ak, ne, ntight, words, tight);
}
static uint32_t mono_bad;
static void mono_check(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u) {
            if (!mono_bad) fprintf(rep, "MONO %s: pixel %u,%u = %04x off gray\n", cur_name, i % 240u, i / 240u, c);
            mono_bad++;
        }
    }
}
static void write_ppm(const char *dir, const char *pal, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/ppm/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ the screens --- */
static void pal(uint32_t p) { settings.palette = p; palette_set(p); }
static void state(void)                          /* a playing song with steps on every track */
{
    uint32_t i;
    ui_power_on();
    song.playing = 1;
    song.g[G_BPM] = 124;
    for (i = 0; i < NTRK; i++) trk[i].seq_idx = 5;
    my_steps(&trk[1]);
    trk[0].step[2].flags |= SF_ACCENT;
    trk[0].step[6].flags |= SF_SLIDE;
#if FELUCCA_SLICE
    if (usr_nz[0]) {                              /* (slices_usr filled USR1: empty again) */
        memset(host_slots, 0, sizeof host_slots);
        smp_user_scan(0);
        slc_man_save = 0;
    }
#endif
    for (i = 0; i < SCOPE_N; i++)                 /* a stand-in signal for the scope */
        scope_buf[i] = (int16_t)((int32_t)((i * 37u) % 128u) * 200 - 12800 + (int32_t)((i % 32u) < 16u ? 3000 : -3000));
    scope_w = 0;
}
static void drum(uint32_t kit)
{
    uint32_t i;
    song.sel = 3;
    trk[3].p[P_E0] = (int16_t)kit;
    for (i = 0; i < 16u; i++) {
        step_t *s = &trk[3].step[i];
        memset(s, 0, sizeof *s);
        s->time = ST_NOTE; s->vel = 100;
        if (i % 4u == 0u) s->hit |= 1u;
        if (i == 4u || i == 12u) s->hit |= 2u, s->acc |= 2u;
        if (i == 10u) s->hit |= 4u;
        if (i % 2u == 0u) s->hit |= 8u;
        if (i == 14u) s->hit |= 16u;
        if (i == 7u || i == 15u) s->hit |= 32u;
        if (i == 3u) s->hit |= 64u;
        if (i == 0u) s->hit |= 128u, s->acc |= 128u;
    }
    trk[3].p[P_SLEN] = 32;
}
static void eng(uint32_t e) { set_engine_of(TSEL, e); }
/* the FM engine of the FM screens: DIGITAL with FELUCCA_FM4, else FM6 (DIGITAL retired: its own screens, the OP ENV /
 * OP LEVEL pages and the algorithm charts, exist only there: fm4_screen) */
#define E_FM (FELUCCA_FM4 ? ENGI_DIGITAL : ENGI_FM6)

enum { S_HOME, S_HOME_IDLE, S_MESSAGE, S_MESSAGE_KEY, S_PRESETS, S_PRESETS_NOFAV, S_USER, S_PHRASES, S_PROJECT, S_TOOLS,
       S_SONG_EMPTY, S_SONG, S_STEP, S_PATTERN, S_CHANCE, S_MOTION, S_DRUM, S_DRUM_HAND, S_DRUM_CYM, S_MIXER, S_MIXER_PAN,
       S_ENV, S_ENVDEST, S_LFO, S_MOD, S_FX, S_SLICER, S_DLY, S_SCL, S_CHORD, S_CHORD_WIDE, S_CHORD_OFF, S_CHORD_KIT, S_ARP, S_VOICE, S_GLOBAL, S_SYSTEM,
       S_EDIT_ANALOG, S_EDIT_DIGITAL, S_OP_ENV, S_EDIT_WHEEL, S_EDIT_SAMPLE, S_EDIT_GRAIN, S_EDIT_PHYS,
       S_ALG1, S_ALG2, S_ALG3, S_ALG4, S_ALG5, S_ALG6, S_ALG7, S_ALG8, S_OP_LEVEL,
       S_FM6_ALG1, S_FM6_ALG5, S_FM6_ALG22, S_FM6_ALG32,
       S_CONFIRM_SEQ, S_CONFIRM_PROJ, S_CONFIRM_USER, S_CONFIRM_PAT, S_CONFIRM_MOTION, S_CONFIRM_ERASE,
       S_MENU, S_MENU_SPEAKER, S_ABOUT, S_ABOUT_REC, S_ABOUT_CREDITS, S_ABOUT_END, S_UBOOT, S_CALIBRATION,
       S_BATT0, S_BATT1, S_BATT2, S_BATT3, S_BATT_USB, S_MOTION_REC, S_MOTION_OFF, S_SONG_HOME,
       S_FX_PEEK, S_FX_HELD, S_FX_WAIT, S_FX_HARM, S_MENU_HOLD, S_MENU_LEDS, S_REVERB,
       S_GLO_PEEK, S_GLO_ACTIVE, S_GLO_EXT, S_SCL_PEEK, S_SCL_ACTIVE, S_EDIT_PEEK, S_EDIT_ACTIVE, S_EDIT_USER, S_LAYER_HINT,
       S_NAME_USER, S_NAME_TYPING, S_NAME_123, S_NAME_EMPTY, S_NAME_FULL, S_NAME_PLAYING, S_PROJECT_NAMED, S_SONG_NAMED,
       S_USER_FOOT, S_SLICES_BREAK, S_SLICES_USR,
       S_ROLL_EMPTY, S_ROLL_ACID, S_ROLL_CHORDS, S_ROLL_TIES, S_ROLL_LEN32, S_ROLL_HIGH, S_ROLL_LOW, S_ROLL_WIDE, S_ROLL_PLAYING,
       S_MOCK_HOME, S_MOCK_PRESETS, S_MOCK_SEQ, S_MOCK_DRUM, S_MOCK_MIXER, S_MOCK_DIALOG, S_MOCK_MENU, S_COUNT };
static const char *const S_NAME[S_COUNT] = {"home", "home_idle", "message", "message_key", "presets", "presets_nofav", "user",
    "phrases", "project", "tools", "song_empty", "song", "step", "pattern", "chance", "motion", "drum",
    "drum_hand", "drum_cym", "mixer", "mixer_pan", "env", "env_dest", "lfo", "mod", "fx", "slicer", "dly", "scl", "chord", "chord_wide", "chord_off", "chord_kit", "arp",
    "voice", "global", "system", "edit_analog", FELUCCA_FM4 ? "edit_digital" : "edit_fm6", "op_env", "edit_wheel", "edit_sample",
    "edit_grain", "edit_phys", "alg_1", "alg_2", "alg_3", "alg_4", "alg_5", "alg_6", "alg_7", "alg_8", "op_level", "fm6_alg_01", "fm6_alg_05", "fm6_alg_22", "fm6_alg_32", "confirm_seq", "confirm_project", "confirm_user", "confirm_pattern",
    "confirm_motion", "confirm_erase", "menu", "menu_speaker", "about", "about_rec", "about_credits", "about_end", "uboot", "calibration",
    "batt_0", "batt_1", "batt_2", "batt_3", "batt_usb", "motion_rec", "motion_off", "song_home",
    "perform_peek", "perform_held", "perform_wait", "perform_harm", "menu_hold", "menu_leds", "reverb_spring",
    "layer_glo_peek", "layer_glo_active", "layer_glo_ext", "layer_scl_peek", "layer_scl_active", "layer_edit_peek",
    "layer_edit_active", "layer_edit_user", "layer_hint",
    "name_user", "name_typing", "name_123", "name_empty", "name_full", "name_playing", "project_named", "song_named",
    "user_foot", "slices_break", "slices_usr",
    "roll_empty", "roll_acid", "roll_chords", "roll_ties", "roll_len32_p2", "roll_high", "roll_low", "roll_wide", "roll_playing",
    "mock_home", "mock_presets", "mock_seq", "mock_drum", "mock_mixer", "mock_dialog", "mock_menu"};

/* the scenes of the UI design screens: the state the UI-redesign
 * prototype drew them from (its setup(): two pattern tracks, the drum pattern on track 4, a synthetic scope),
 * plus the hot knob each mock shows; tests/ui_mockcmp.py compares these renders with the mock PNGs */
static void mock_state(int s)
{
    uint32_t i;
    ui_power_on();
    usb.config = 0;
    song.batt_raw = 600;
    for (i = 0; i < SCOPE_N; i++) {                  /* a saw with a little second harmonic */
        double ph = fmod(i / 64.0, 1.0);
        scope_buf[i] = (int16_t)((ph * 2.0 - 1.0) * 9000.0 + sin(i * 0.4908) * 2500.0);
    }
    scope_w = 0;
    song.playing = 1;
    song.g[G_BPM] = 124;
    for (i = 0; i < NTRK; i++) trk[i].seq_idx = 5;
    load_pat16(&trk[0], PATTERNS[0].note, PATTERNS[0].flags);
    load_pat16(&trk[1], PATTERNS[4].note, PATTERNS[4].flags);
    for (i = 0; i < 16u; i++) {
        step_t *st = &trk[3].step[i];
        memset(st, 0, sizeof *st);
        st->time = ST_NOTE; st->vel = 100;
        if (i % 4u == 0u) st->hit |= 1u;
        if (i == 4u || i == 12u) st->hit |= 2u, st->acc |= 2u;
        if (i == 10u) st->hit |= 4u;
        if (i % 2u == 0u) st->hit |= 8u;
        if (i == 14u) st->hit |= 16u;
        if (i == 7u || i == 15u) st->hit |= 32u;
        if (i == 3u) st->hit |= 64u;
        if (i == 0u) st->hit |= 128u, st->acc |= 128u;
    }
    trk[3].p[P_SLEN] = 32;
    switch (s) {
    case S_MOCK_HOME: ui.home = 1; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_MOCK_PRESETS: go_page(GR_BROWSE); break;
    case S_MOCK_SEQ: song.sel = 0; song.rec = 1; go_page(GR_ROLL); ui.cursor = 6; ui.hot_col = 0; ui.hot_t = 30; break;
    case S_MOCK_DRUM: song.sel = 3; go_page(GR_ROLL); ui.cursor = 4; ui.lane = 1; trk[3].seq_idx = 9; ui.hot_col = 0; ui.hot_t = 30; break;
    case S_MOCK_MIXER:
        go_page(GR_TRK);
        trk[0].p[P_LEVEL] = 100; trk[1].p[P_LEVEL] = 84; trk[2].p[P_LEVEL] = 64; trk[3].p[P_LEVEL] = 110;
        trk[0].p[P_PAN] = -20; trk[1].p[P_PAN] = 24; trk[2].p[P_PAN] = 0; trk[3].p[P_PAN] = -4;
        trk[0].p[P_REV] = 30; trk[1].p[P_REV] = 80; trk[2].p[P_REV] = 10; trk[3].p[P_REV] = 0;
        trk[2].p[P_MUTE] = 1;
        song.rec = 2u;
        trk[0].peak = 11000; trk[1].peak = 3300; trk[3].peak = 20000;     /* (the mock's stand-in meters) */
        ui.hot_col = 1; ui.hot_t = 30;
        break;
    case S_MOCK_DIALOG: ui.confirm = CF_OVR_PROJ; ui.confirm_trk = 0; go_page(GR_PATS); break;
    case S_MOCK_MENU: ui.menu = 1; ui.menu_sel = 0; break;
    default: break;
    }
}

/* the STEP page's piano roll (ui_graph.c graph_roll) on track 1, stopped unless said: an empty pattern; ACID in
 * A minor; POLY chords (Am7 F C G, tied); a line with ties, slides and accents in C major; LEN 32 on its second
 * page, playing; high (A6..) and low (C-1..) notes; a page wider than the view (edge marks); ACID playing with a
 * key held (A3, its row lit) */
static void step_put(step_t *st, uint32_t time, uint32_t flags, uint32_t n, const uint8_t *notes)
{
    uint32_t j;
    memset(st, 0, sizeof *st);
    st->time = (uint8_t)time; st->flags = (uint8_t)flags; st->n = (uint8_t)n; st->vel = 100;
    for (j = 0; j < n; j++) st->note[j] = notes[j];
}
static void roll_scene(int s)
{
    static const uint8_t AM7[] = {57, 60, 64, 67}, FM7[] = {53, 57, 60, 64}, CMA[] = {60, 64, 67}, GMA[] = {55, 59, 62, 67};
    static const uint8_t LINE[16] = {48, 0, 55, 60, 0, 59, 0, 0, 62, 64, 0, 67, 0, 65, 64, 0};
    static const uint8_t LINE_T[16] = {0, 1, 0, 0, 1, 0, 2, 2, 0, 0, 1, 0, 1, 0, 0, 2};   /* 0 note, 1 tie, 2 rest */
    track_t *t;
    uint32_t i;
    song.sel = 0; t = TSEL;
    song.playing = 0;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_page(GR_ROLL);
    ui.cursor = 0;
    switch (s) {
    case S_ROLL_EMPTY: break;
    case S_ROLL_ACID:
    case S_ROLL_PLAYING:
        load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);
        t->p[P_ROOT] = 9; t->p[P_SCALE] = 2; ui.cursor = 6;
        if (s == S_ROLL_PLAYING) {
            song.playing = 1; t->seq_idx = 9;
            kb_trk[3] = 0; kb_chn[3] = 1; kb_chord[3][0] = 57;            /* A3 held */
        }
        break;
    case S_ROLL_CHORDS:
        t->p[P_VOICE] = V_POLY; t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
        for (i = 0; i < 16u; i++) {
            const uint8_t *c = i / 4u == 0u ? AM7 : i / 4u == 1u ? FM7 : i / 4u == 2u ? CMA : GMA;
            uint32_t n = i / 4u == 2u ? 3u : 4u;
            if (i % 4u == 0u) step_put(&t->step[i], ST_NOTE, i == 8u ? SF_ACCENT : 0u, n, c);
            else if (i % 4u == 1u) step_put(&t->step[i], ST_TIE, 0, 0, c);
            else if (i % 4u == 2u) step_put(&t->step[i], ST_NOTE, 0, n, c);
            else step_put(&t->step[i], ST_REST, 0, 0, c);
        }
        ui.cursor = 4;
        break;
    case S_ROLL_TIES:
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;
        for (i = 0; i < 16u; i++) {
            uint8_t n = LINE[i];
            uint32_t f = (i == 3u || i == 9u ? SF_SLIDE : 0u) | (i == 0u || i == 8u || i == 13u ? SF_ACCENT : 0u);
            if (LINE_T[i] == 0u) step_put(&t->step[i], ST_NOTE, f, 1, &n);
            else step_put(&t->step[i], LINE_T[i] == 1u ? ST_TIE : ST_REST, 0, 0, &n);
        }
        t->step[2].flags |= SF_SLIDE;                  /* (slides into a note, from before a tie) */
        ui.cursor = 9;
        break;
    case S_ROLL_LEN32:
        load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);
        t->p[P_SLEN] = 32;
        for (i = 16; i < 32u; i++) {
            uint8_t n = (uint8_t)(60 + (i * 5u) % 12u);
            if (i % 3u != 2u) step_put(&t->step[i], ST_NOTE, i % 8u == 0u ? SF_ACCENT : 0u, 1, &n);
        }
        ui.cursor = 20; song.playing = 1; t->seq_idx = 22;
        break;
    case S_ROLL_HIGH:
    case S_ROLL_LOW:
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n = (uint8_t)(s == S_ROLL_HIGH ? 112 + (i * 7u) % 15u : (i * 7u) % 15u);
            step_put(&t->step[i], ST_NOTE, 0, 1, &n);
        }
        ui.cursor = 2;
        break;
    case S_ROLL_WIDE:
        t->p[P_VOICE] = V_POLY;
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n[2] = {(uint8_t)(36 + i), (uint8_t)(72 + i)};
            step_put(&t->step[i], ST_NOTE, 0, 2, n);
        }
        ui.cursor = 4;
        break;
    default: break;
    }
    ui.bank = (uint8_t)(ui.cursor / 16u);
}

static void setup(int s)
{
    memset(kb_chn, 0, sizeof kb_chn);               /* no key held (roll_playing holds one) */
    if (s >= S_MOCK_HOME) {
        mock_state(s);
        return;
    }
    state();
    switch (s) {
    case S_HOME: song.octave = 2; song.rec = 1; usb.config = 1; break;
    case S_HOME_IDLE: song.playing = 0; song.batt_raw = 570; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_MESSAGE: ui_say("LOADED ", "07 A VERY LONG PATTERN NAME"); break;
    case S_MESSAGE_KEY: ui_message("[SAVE] HOLD TO UNDO"); break;            /* a message with a keycap */
    case S_PRESETS: favorite_set(0, 4, 1); favorite_set(0, 5, 1); go_page(GR_BROWSE); break;
    case S_PRESETS_NOFAV: favorites.filter = 1; go_page(GR_BROWSE); break;
    case S_USER: song.playing = 0; up_store(3, "MY LONG BASS NAME"); up_store(4, "PAD"); ui.uslot = 3; go_page(GR_USER); break;
    case S_PHRASES: go_page(GR_PATS); break;
    case S_PROJECT: song.playing = 0; project_save(1); song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 4; break;
    case S_TOOLS: go_page(GR_TOOLS); ui.act = 1; break;
    case S_SONG_EMPTY: song.playing = 0; go_page(GR_SONG); break;
    case S_SONG:
        song.playing = 0; project_save(0); project_save(1);
        chain_config.count = 3;
        chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4}; chain_config.row[2] = (chain_row_t){2, 1};
        ui.song_row = 1; go_page(GR_SONG); chain_prepare(); events_block(32);
        break;
    case S_STEP: song.rec = 1; go_page(GR_ROLL); ui.cursor = 6; break;
    case S_PATTERN: go_title("PATTERN"); ui.cursor = 3; break;
    case S_CHANCE: go_page(GR_CHANCE); step_set_chance(&TSEL->step[0], 65); break;
    case S_MOTION: go_page(GR_MOTION); break;
    case S_DRUM: drum(0); go_page(GR_ROLL); ui.cursor = 4; ui.lane = 1; trk[3].seq_idx = 9; break;
    case S_DRUM_HAND: drum(1); go_page(GR_ROLL); ui.lane = 5; ui.cursor = 7; break;
    case S_DRUM_CYM: drum(2); go_page(GR_ROLL); ui.lane = 7; break;
    case S_MIXER:
        go_page(GR_TRK);
        trk[0].p[P_LEVEL] = 100; trk[1].p[P_LEVEL] = 84; trk[2].p[P_LEVEL] = 64; trk[3].p[P_LEVEL] = 110;
        trk[0].p[P_PAN] = -20; trk[1].p[P_PAN] = 24; trk[3].p[P_PAN] = -4; trk[1].p[P_REV] = 80;
        trk[2].p[P_MUTE] = 1; trk[3].p[P_MUTE] = 1; song.rec = 2u | 8u; trk[0].peak = 9000;
        break;
    case S_MIXER_PAN:                            /* PAN just turned, the ends of each knob, OFF, armed while stopped */
        go_page(GR_TRK); song.playing = 0; song.sel = 1;
        trk[0].p[P_LEVEL] = 127; trk[1].p[P_LEVEL] = 0; trk[2].p[P_LEVEL] = 1; trk[3].p[P_LEVEL] = 104;
        trk[0].p[P_PAN] = -64; trk[1].p[P_PAN] = 63; trk[2].p[P_PAN] = 0; trk[3].p[P_PAN] = 1;
        trk[0].p[P_REV] = 127; trk[2].p[P_REV] = 1; trk[3].p[P_MUTE] = 1;
        song.rec = 1u | 8u; ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_ENV: go_title("ENV"); ui.hot_col = 2; ui.hot_t = 30; break;
    case S_ENVDEST: go_title("ENV DEST"); break;
    case S_LFO: go_title("LFO"); break;
    case S_MOD: TSEL->p[P_M1SRC] = 1; TSEL->p[P_M1DST] = 2; TSEL->p[P_M1AMT] = 40; TSEL->p[P_M2SRC] = 6;
        TSEL->p[P_M2DST] = 14; TSEL->p[P_M2AMT] = -64; mod_ui_slot = 1; go_title("MOD"); break;
    case S_FX: go_title("FX"); break;
    case S_SLICER: TSEL->p[P_SLCR] = 1; go_title("SLICER"); break;
    case S_DLY: go_title("DLY"); break;
    case S_SCL: TSEL->p[P_SCALE] = 2; go_title("SCL"); break;
    case S_CHORD:                                    /* A minor DIA7, the last chord on B: Bm7b5 */
    case S_CHORD_WIDE: {                             /* A harmonic minor DIA7 +OCT on G#: G#dim7 over three octaves */
        uint8_t out[CHORD_MAX];
        TSEL->p[P_VOICE] = V_POLY; TSEL->p[P_ROOT] = 9; TSEL->p[P_SCALE] = s == S_CHORD ? 2 : 7;
        TSEL->p[P_CHRD] = CH_DIA7; TSEL->p[P_VOIC] = s == S_CHORD ? VC_CLOSE : VC_BASS;
        chord_build(TSEL, s == S_CHORD ? 71u : 68u, out);
        go_title("CHORD"); ui.hot_col = 0; ui.hot_t = 30;
        break;
    }
    case S_CHORD_OFF: go_title("CHORD"); break;
    case S_CHORD_KIT: eng(ENGI_DRUM); TSEL->p[P_CHRD] = CH_DIA3; go_title("CHORD"); break;
    case S_ARP: go_title("ARP"); break;
    case S_VOICE: go_title("VOICE"); break;
    case S_GLOBAL: go_title("GLOBAL"); break;
    case S_SYSTEM: go_title("SYSTEM"); break;
    case S_EDIT_ANALOG: go_title("EDIT 1"); break;
    case S_EDIT_DIGITAL: eng(E_FM); go_title("EDIT 1"); break;
    case S_OP_ENV: eng(1); go_title("OP1 ENV"); break;
    case S_EDIT_WHEEL: eng(7); go_title("EDIT 1"); ui.hot_col = 1; ui.hot_t = 30; break;
    case S_EDIT_SAMPLE: {
        uint32_t i;
        eng(4); go_title("EDIT 1"); last_note = 60;
        for (i = 0; i < 4000u && !sample_wave.ready; i++) sample_wave_tick(TSEL);
        break;
    }
    case S_EDIT_GRAIN: eng(8); go_title("EDIT 2"); break;
    case S_EDIT_PHYS: eng(9); go_title("EDIT 1"); break;
    case S_ALG1: case S_ALG2: case S_ALG3: case S_ALG4: case S_ALG5: case S_ALG6: case S_ALG7: case S_ALG8:
        eng(1); TSEL->p[P_E0] = (int16_t)(s - S_ALG1);      /* the 8 DIGITAL charts; FB on the odd ones, IDX high .. 0 */
        TSEL->p[P_E6] = (s - S_ALG1) & 1 ? 40 : 0; TSEL->p[P_E4] = (int16_t)((S_ALG8 - s) * 18);
        go_title("EDIT 2"); break;
    case S_OP_LEVEL: eng(1); TSEL->p[P_E0] = 1; TSEL->p[P_FM4_LEVEL] = 0; go_title("OP LEVEL");   /* op 4 silent, op 3 hot */
        ui.hot_col = 2; ui.hot_t = 30; break;
    /* FM6's algorithm charts: 1 as it is; 5 with FB +3 just turned; 22 on EDIT 2, DTUN just turned (the carriers);
     * 32 with operator 6 at output level 0, MLVL just turned (no routes: nothing in ACCENT but nothing either) */
    case S_FM6_ALG1: eng(ENGI_FM6); TSEL->p[P_E0] = 1; go_title("EDIT 1"); break;
    case S_FM6_ALG5: eng(ENGI_FM6); TSEL->p[P_E0] = 5; TSEL->p[P_E1] = 3; go_title("EDIT 1"); ui.hot_col = 1; ui.hot_t = 30; break;
    case S_FM6_ALG22: eng(ENGI_FM6); TSEL->p[P_E0] = 22; TSEL->p[P_E6] = 40; go_title("EDIT 2"); ui.hot_col = 2; ui.hot_t = 30; break;
    case S_FM6_ALG32:
        eng(ENGI_FM6); TSEL->p[P_E0] = 32; fm6_patch[song.sel][FP_OL] = 0; fm6_pgen[song.sel]++;   /* (op 6: the patch's first) */
        go_title("EDIT 1"); ui.hot_col = 2; ui.hot_t = 30;
        break;
    case S_CONFIRM_SEQ: ui.confirm = CF_CLEAR_SEQ; ui.confirm_trk = 2; break;
    case S_CONFIRM_PROJ: ui.confirm = CF_OVR_PROJ; ui.confirm_trk = 0; break;
    case S_CONFIRM_USER: song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); ui.confirm = CF_OVR_USER; ui.confirm_trk = 6; break;
    case S_CONFIRM_PAT: ui.confirm = CF_LOAD_PAT; ui.confirm_trk = 0; ui.ppick = 4; break;
    case S_CONFIRM_MOTION: ui.confirm = CF_CLEAR_MOTION; ui.confirm_trk = 3; break;
    case S_CONFIRM_ERASE: song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); ui.confirm = CF_ERASE_USER; ui.confirm_trk = 6; break;
    case S_MENU: ui.menu = 1; ui.menu_sel = 0; song.rec = 1; break;
    case S_MENU_SPEAKER: ui.menu = 1; ui.menu_sel = 1; settings.lowcut = 2; break;
    case S_ABOUT: ui.menu = 2; ui.menu_scroll = 0; break;
    case S_ABOUT_REC: ui.menu = 2; ui.menu_scroll = 0; song.rec = 1; break;          /* the REC mark beside OCT- BACK */
    case S_ABOUT_CREDITS: ui.menu = 2; ui.menu_scroll = 360; break;
    case S_ABOUT_END: ui.menu = 2; ui.menu_scroll = (uint16_t)menu_scroll_max(); break;
    case S_UBOOT: ui.uboot = 3; break;
    /* the header's battery at each stock level (raw ADC: under 531, 531.., 561.., 591..) and on USB power */
    case S_BATT0: usb.config = 0; song.batt_raw = 500; go_home(); break;
    case S_BATT1: usb.config = 0; song.batt_raw = 540; go_home(); break;
    case S_BATT2: usb.config = 0; song.batt_raw = 570; go_home(); break;
    case S_BATT3: usb.config = 0; song.batt_raw = 600; go_home(); break;
    case S_BATT_USB: usb.config = 1; usb.suspended = 0; song.batt_raw = 500; go_home(); break;
    case S_MOTION_REC: song.rec = 1u << song.sel; go_page(GR_MOTION); break;    /* recording into the motion */
    case S_MOTION_OFF: song.playing = 0; go_page(GR_MOTION); ui.act = 4; break;  /* stopped, CLEAR picked */
    case S_SONG_HOME:                            /* the song playing: the disc and its row in the header */
        song.playing = 0; project_save(0); project_save(1);
        chain_config.count = 2;
        chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4};
        go_page(GR_SONG); chain_prepare(); events_block(32); go_home(); ui.msg_t = 0;
        break;
    /* the FX layer's map: held alone; REPEAT 1/16 + LPF + a mute playing with the macros turned;
     * at 72 BPM: a REPEAT 1/16 waiting for its 1/16 (shown THEME) beside TAPE STOP playing, and REPEAT 1/8
     * held, too long at that tempo (it and REVERSE dimmed) */
    case S_FX_PEEK: go_title("ENV"); ui.layer = LAYER_FX; break;
    case S_FX_HELD:
        go_home(); ui.layer = LAYER_FX;
        perf_held = perf_act = PF_BIT(PF_R16) | PF_BIT(PF_LPF) | PF_BIT(PF_M1 + 1);
        perf_k[0] = -40; perf_k[1] = 25; perf_k[3] = 30; ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_FX_WAIT:
        song.playing = 1; song.g[G_BPM] = 72; ui.layer = LAYER_FX;
        perf_held = PF_BIT(PF_R16) | PF_BIT(PF_R8) | PF_BIT(PF_TAPE); perf_act = PF_BIT(PF_TAPE);
        perf_k[2] = 100;
        break;
    case S_FX_HARM:                                 /* OCT UP playing, KNOB 4 its shimmer; OCT DN held under it */
        song.playing = 1; ui.layer = LAYER_FX;
        perf_ord[PF_ODN] = ++perf_seq; perf_ord[PF_OUP] = ++perf_seq;
        perf_held = perf_act = PF_BIT(PF_OUP) | PF_BIT(PF_ODN);
        perf_k[3] = 60; ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_REVERB: go_title("REVERB"); song.g[G_RTYPE] = 1; ui.hot_col = 0; ui.hot_t = 30; break;   /* TYPE: SPRING */
    case S_MENU_HOLD: ui.menu = 1; ui.menu_sel = MI_HOLD; settings_hold = 2; break;
    case S_MENU_LEDS: ui.menu = 1; ui.menu_sel = MI_LEDS; settings_leds = LEDS_INV; break;
    /* the GLO SCL EDIT layers (ui_layer.c): just opened (a peek), and in use: GLO with T2 muted, T3 soloed (its key
     * held) and KNOB 1 turned; with CLK EXT (TAP dimmed); SCL at D# minor, KNOB 2 turned; EDIT on DIGITAL preset 3,
     * a favourite; a user preset; the hint after a tap */
    case S_GLO_PEEK: go_title("ENV"); ui.layer = LAYER_GLO; break;
    case S_GLO_ACTIVE:
        go_home(); ui.layer = LAYER_GLO; trk[1].p[P_MUTE] = 1; perf_solo = 4; trk[0].p[P_LEVEL] = 90;
        ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_GLO_EXT: go_home(); ui.layer = LAYER_GLO; song.g[G_CLOCK] = 1; trk[0].p[P_MUTE] = trk[3].p[P_MUTE] = 1; break;
    case S_SCL_PEEK: go_title("ENV"); ui.layer = LAYER_SCL; break;
    case S_SCL_ACTIVE: go_home(); ui.layer = LAYER_SCL; TSEL->p[P_ROOT] = 3; TSEL->p[P_SCALE] = 2; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_EDIT_PEEK: go_title("EDIT 1"); ui.layer = LAYER_EDIT; break;
    case S_EDIT_ACTIVE: eng(E_FM); apply_preset_to(TSEL, 2); favorite_set(E_FM, 2, 1); go_home(); ui.layer = LAYER_EDIT;
        ui.hot_col = 0; ui.hot_t = 30; break;
    case S_EDIT_USER: song.playing = 0; eng(6); up_store(6, "MY LONG TRIO NAME"); up_load(6); favorite_set(NENGINES, 6, 1);
        go_home(); ui.layer = LAYER_EDIT; break;
    case S_LAYER_HINT: go_page(GR_TRK); ui.msg_t = 0; layer_tap(LAYER_GLO); break;
    /* NAME (ui_name.c): USER SAVE prefilled; a letter cycling (RS: S, R next); 123 on a project; an empty project name
     * (the placeholder); 12 of the widest letters, the cursor past them; playing (OCT+ dim) */
    case S_NAME_USER: song.playing = 0; go_page(GR_USER); ui.uslot = 6; name_open(NK_USER_SAVE, 6); break;
    case S_NAME_TYPING:
        song.playing = 0; go_page(GR_USER); name_open(NK_USER_SAVE, 6);
        str_cpy(nm.s, "SUB BAS", sizeof nm.s); nm.len = 7; nm.cur = 6; nm.key = 9; nm.tap = 1; nm.t = fm1_ms;
        break;
    case S_NAME_123:
        song.playing = 0; go_page(GR_SLOTS); name_open(NK_PROJ_SAVE, 1);
        str_cpy(nm.s, "LIVE 2026", sizeof nm.s); nm.len = 9; nm.cur = 5; nm.num = 1;
        break;
    case S_NAME_EMPTY: song.playing = 0; go_page(GR_SLOTS); proj_name[0] = 0; name_open(NK_PROJ_SAVE, 2); break;
    case S_NAME_FULL:
        song.playing = 0; up_store(4, "PAD"); go_page(GR_USER); name_open(NK_USER_RENAME, 4);
        str_cpy(nm.s, "MWMWMWMWMWMW", sizeof nm.s); nm.len = nm.cur = 12;
        break;
    case S_NAME_PLAYING: go_page(GR_USER); name_open(NK_USER_SAVE, 6); ui_message("STOP TO SAVE"); break;
    case S_PROJECT_NAMED:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save_as(1, "MWMWMWMWMWMW"); project_save_as(2, "");
        song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 4; ui.msg_t = 0;
        break;
    case S_SONG_NAMED:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save_as(1, "MWMWMWMWMWMW");
        chain_config.count = 3;
        chain_config.row[0] = (chain_row_t){0, 2}; chain_config.row[1] = (chain_row_t){1, 4}; chain_config.row[2] = (chain_row_t){2, 1};
        ui.song_row = 0; go_page(GR_SONG); ui.msg_t = 0;
        break;
    case S_ROLL_EMPTY: case S_ROLL_ACID: case S_ROLL_CHORDS: case S_ROLL_TIES: case S_ROLL_LEN32: case S_ROLL_HIGH:
    case S_ROLL_LOW: case S_ROLL_WIDE: case S_ROLL_PLAYING: roll_scene(s); break;
    case S_USER_FOOT: song.playing = 0; up_store(3, "MY BASS"); ui.uslot = 3; go_page(GR_USER); break;   /* EDIT NAME lit */
#if FELUCCA_SLICE
    /* EDIT > SLICES: BREAK's 16 slices (slice 6 selected); a user sample's slices set by hand: DIV 8 taken as MAN,
     * slice 3's start moved (KNOB 2 hot), SPLIT picked (OCT+ lit) */
    case S_SLICES_BREAK: eng(13u); go_page(GR_SLICES); sp.sel = 5; break;
    case S_SLICES_USR:
        eng(13u); host_slot_make(0); smp_user_scan(0);
        TSEL->p[P_E0] = 1; TSEL->p[P_E1] = 1;
        go_page(GR_SLICES); sp.sel = 0; slice_knob(0, 2); slice_knob(1, 6); ui.act = 3; ui.msg_t = 0;
        ui.hot_col = 1; ui.hot_t = 30;
        break;
#endif
    default: break;
    }
}
static void draw(int s)
{
    nscr = npend = 0;
    ntight = 0;
    tight[0] = 0;
    memset(host_screen, 0, sizeof host_screen);
    if (s == S_CALIBRATION) {                     /* the blocking setup screen: its two drawing steps */
        setup_title();
        setup_show("TURN RIGHT", "ALGORITHM");
        return;
    }
    ui.force = 1;
    ui_draw();
}

/* every value of every column on every page of every engine: labels and values fit their column */
static uint32_t nsweep;
static void sweep_columns(void)
{
    uint32_t e, i, c;
    char name[64];
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;                                    /* (DIGITAL without FELUCCA_FM4: no track has it) */
        for (i = 0; i < NPAGES; i++) {
            state();
            pal(UI_MONO_INDEX);
            eng(e);
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            snprintf(name, sizeof name, "%s/%s", ENGINES[e]->name, PAGES[i].title);
            cur_name = name;
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                const param_desc_t *d = PAGES[i].scope == SC_GLOBAL || PAGES[i].scope == SC_TRACK || PAGES[i].scope == SC_ENGINE
                                        ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || PAGES[i].graph == GR_MOD) continue;
                v0 = *vp;
                for (v = d->min; v <= d->max; v++) {
                    *vp = (int16_t)v;
                    nscr = npend = 0;
                    ui.force = 1;
                    draw_columns();
                    lint();
                    nsweep++;
                    if (v - d->min > 300) v = d->max - 1;      /* wide ranges: the ends */
                }
                *vp = (int16_t)v0;
            }
            draw(-1);                                    /* the whole page */
            lint();
        }
        state(); pal(UI_MONO_INDEX); eng(e); go_home();   /* HOME's four knobs of this engine */
        snprintf(name, sizeof name, "%s/HOME", ENGINES[e]->name);
        cur_name = name;
        draw(-1);
        lint();
    }
    for (e = 0; e < 4u; e++) {                           /* the MOD page: every source and destination */
        int32_t v;
        state(); pal(UI_MONO_INDEX); go_title("MOD");
        cur_name = "MOD sweep";
        for (v = 0; v < MD_N; v++) {
            TSEL->p[P_M1SRC] = (int16_t)(v % MS_N); TSEL->p[P_M1DST] = (int16_t)v; TSEL->p[P_M1AMT] = (int16_t)(e * 40 - 64);
            mod_ui_slot = (uint8_t)(v & 3u);
            draw(-1);
            lint();
        }
    }
}

/* the rolling digits (ui_draw.c roll_*): GLOBAL, SELECT and KNOB 4 turned together, BPM 129 -> 130 in the header
 * and TUNE 19 -> 20 on a card, then back. Every frame is linted and (MONO) checked for gray; with a directory,
 * filmstrips of both (each frame side by side, x4: the static frame before, then the roll's frames) as
 * DIR/<PALETTE>_bpm_roll.ppm and DIR/<PALETTE>_card_roll.ppm, the up roll above the down roll. */
#define FS_N (1u + ROLL_FRAMES)                   /* frames per filmstrip row */
#define FS_Z 4u                                   /* zoom */
#define FS_GAP 2u                                 /* px between frames, before the zoom */
static void roll_turns(int32_t s)                 /* one UI frame with SELECT and KNOB 4 turned by s */
{
    host_enc[panel.enc[EN_SELECT]] += s * panel.dir[EN_SELECT];
    host_enc[panel.enc[EN_K4]] += s * panel.dir[EN_K4];
    host_ticks += 16000u; fm1_ms += 16u;
    ui_input();
    ui_draw();
    host_ticks += 200000u;
}
static void roll_film_put(uint8_t *img, uint32_t iw, uint32_t row, uint32_t k, int32_t x0, int32_t y0, uint32_t w, uint32_t h)
{
    uint32_t x, y;
    for (y = 0; y < h * FS_Z; y++)
        for (x = 0; x < w * FS_Z; x++) {
            uint16_t c = swap16(host_screen[(uint32_t)(y0 + (int32_t)(y / FS_Z)) * 240u + (uint32_t)x0 + x / FS_Z]);
            uint8_t *p = img + (((row * (h + FS_GAP) + FS_GAP) * FS_Z + y) * iw + (k * (w + FS_GAP) + FS_GAP) * FS_Z + x) * 3u;
            p[0] = (uint8_t)((c >> 11) * 255u / 31u);
            p[1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
            p[2] = (uint8_t)((c & 31u) * 255u / 31u);
        }
}
static void roll_film_save(const char *dir, const char *pal, const char *name, const uint8_t *img, uint32_t iw, uint32_t ih)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", iw, ih);
    fwrite(img, 1, (size_t)iw * ih * 3u, f);
    fclose(f);
}
static void roll_frames(const char *dir)
{
    enum { HX = 56, HW = 48, HH = H_HEAD };       /* the header crop: the tempo icon and the BPM strip */
    static uint8_t bimg[(FS_N * (HW + FS_GAP) + FS_GAP) * FS_Z * (2u * (HH + FS_GAP) + FS_GAP) * FS_Z * 3u];
    static uint8_t cimg[(FS_N * (COL_W + FS_GAP) + FS_GAP) * FS_Z * (2u * (COL_H + FS_GAP) + FS_GAP) * FS_Z * 3u];
    const uint32_t biw = (FS_N * (HW + FS_GAP) + FS_GAP) * FS_Z, bih = (2u * (HH + FS_GAP) + FS_GAP) * FS_Z;
    const uint32_t ciw = (FS_N * (COL_W + FS_GAP) + FS_GAP) * FS_Z, cih = (2u * (COL_H + FS_GAP) + FS_GAP) * FS_Z;
    uint32_t p, row, k;
    for (p = 0; p < NPALETTES; p++) {
        char name[64];
        memset(bimg, 90, sizeof bimg);
        memset(cimg, 90, sizeof cimg);
        state(); song.playing = 0; pal(p);
        song.g[G_BPM] = 129; song.g[G_TUNE] = 19;
        go_title("GLOBAL");
        snprintf(name, sizeof name, "%s/roll", UI_PALETTES[p].name);
        cur_name = name;
        draw(-1);
        for (k = 0; k < 8u; k++) { ui.hot_t = 0; ui.bpm_t = 0; ui_draw(); }   /* settled, nothing hot */
        lint();
        for (row = 0; row < 2u; row++) {
            roll_film_put(bimg, biw, row, 0, HX, 0, HW, HH);
            roll_film_put(cimg, ciw, row, 0, CARD_X(3), Y_LABEL, COL_W, COL_H);
            for (k = 1; k < FS_N; k++) {
                if (k == 1u) roll_turns(row ? -1 : 1);
                else ui_draw();
                lint();
                if (p == UI_MONO_INDEX) mono_check();
                roll_film_put(bimg, biw, row, k, HX, 0, HW, HH);
                roll_film_put(cimg, ciw, row, k, CARD_X(3), Y_LABEL, COL_W, COL_H);
            }
            if (ui.roll[3].from[0] || ui.roll[ROLL_BPM].from[0] || song.g[G_BPM] != (row ? 129 : 130) ||
                song.g[G_TUNE] != (row ? 19 : 20)) {
                fprintf(rep, "ROLL %s: not over after %u frames (BPM %d TUNE %d)\n", name, ROLL_FRAMES, song.g[G_BPM], song.g[G_TUNE]);
                nfind++;
            }
            for (k = 0; k < 8u; k++) ui_draw();
        }
        if (dir && (!strcmp(UI_PALETTES[p].name, "MONO") || !strcmp(UI_PALETTES[p].name, "GREEN"))) {
            roll_film_save(dir, UI_PALETTES[p].name, "bpm_roll", bimg, biw, bih);
            roll_film_save(dir, UI_PALETTES[p].name, "card_roll", cimg, ciw, cih);
        }
    }
}
/* the host cost of a roll frame: the header and the cards (draw_head, draw_columns) while both strips roll,
 * against the same calls on an idle frame (nothing drawn) and a full redraw of the four cards and the header */
static void roll_cost(void)
{
    enum { N = 400 };
    uint32_t i, k;
    double t_roll = 0, t_full = 0;
    uint64_t px_roll = 0, bl_roll = 0, nroll = 0, px_full = 0, bl_full = 0;
    for (i = 0; i < N; i++) {
        clock_t c0;
        state(); song.playing = 0; pal(1);
        song.g[G_BPM] = 129; song.g[G_TUNE] = 19;
        go_title("GLOBAL");
        draw(-1);
        for (k = 0; k < 8u; k++) { ui.hot_t = 0; ui.bpm_t = 0; ui_draw(); }
        px_visited = 0; blit_px = 0;
        c0 = clock();
        ui.force = 1; ui.frame++; draw_head(); draw_columns(); ui.force = 0;
        t_full += (double)(clock() - c0); px_full += px_visited; bl_full += blit_px;
        nscr = npend = 0;
        roll_turns(i & 1u ? -1 : 1);                /* the change: the card and the header redrawn whole */
        for (k = 2; k < ROLL_FRAMES; k++) {         /* the strip-only frames */
            px_visited = 0; blit_px = 0;
            c0 = clock();
            ui.frame++; draw_head(); draw_columns();
            t_roll += (double)(clock() - c0); px_roll += px_visited; bl_roll += blit_px; nroll++;
            nscr = npend = 0;
        }
    }
    fprintf(rep, "  roll frame (header BPM 129->130 and card TUNE 19->20, strips only): %.2f us, %llu glyph px, %llu px blitted;"
            " header + 4 cards redrawn whole: %.2f us, %llu glyph px, %llu px blitted\n",
            t_roll / CLOCKS_PER_SEC / (double)nroll * 1e6, (unsigned long long)(px_roll / nroll), (unsigned long long)(bl_roll / nroll),
            t_full / CLOCKS_PER_SEC / N * 1e6, (unsigned long long)(px_full / N), (unsigned long long)(bl_full / N));
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/ui_new";
    char path[600];
    uint32_t p, s;
    static const char *const SHOW[] = {"MONO", "GREEN", "PAPER"};
    snprintf(path, sizeof path, "%s/report.txt", out);
    rep = fopen(path, "w");
    if (!rep) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    {
        char ap[600];
        snprintf(ap, sizeof ap, "%s/text_audit.tsv", out);
        audf = fopen(ap, "w");
    }
    if (audf) fprintf(audf, "screen\ttexts\ttext_px\ticons\ticon_px\tkeycaps\tkeycap_px\tellipsised\ttight\twords\ttight_list\n");
    {   /* the lint itself: an overlap, a value too wide for its column, a text past its canvas, one past its cell */
        state(); pal(0); cur_name = "self-test";
        nscr = npend = 0;
        cv_begin(COL_W, COL_H, T_BG);
        cv_text(0, 0, &AF_S, "AAAA", T_TEXT);
        cv_text(10, 4, &AF_S, "BBBB", T_TEXT);
        cv_text_fit(0, 15, &AF_M, "WWWWWWWW", T_THEME, T_BG, COL_W);
        cv_text(40, 30, &AF_M, "CUT", T_THEME);
        cv_rrect(2, 31, 20, 13, 3, T_SURF, T_BG);          /* a cell narrower than its word */
        cv_text(4, 31, &AF_S, "SPILL", T_TEXT);
        cv_blit(4, Y_LABEL);
        lint();
        if (nfind < 4u || !nspill) { fprintf(stderr, "ui_render: the lint missed its self-test (%u)\n", nfind); return 1; }
        fprintf(rep, "(self-test: %u findings above are expected)\n\n", nfind);
        nfind = 0;
    }
    for (p = 0; p < NPALETTES; p++)
        for (s = 0; s < S_COUNT; s++) {
            char name[64];
            uint32_t k;
            if (!FELUCCA_FM4 && (s == S_OP_ENV || (s >= S_ALG1 && s <= S_OP_LEVEL)))
                continue;                           /* (DIGITAL's own screens: FELUCCA_FM4=1 only) */
            snprintf(name, sizeof name, "%s/%s", UI_PALETTES[p].name, S_NAME[s]);
            cur_name = name;
            setup((int)s);
            pal(p);
            if (p == UI_MONO_INDEX) aud = audf;
            draw((int)s);
            lint();
            if (p == UI_MONO_INDEX) { audit_scene(S_NAME[s]); aud = 0; mono_check(); }
            for (k = 0; k < 3u; k++)
                if (!strcmp(UI_PALETTES[p].name, SHOW[k])) write_ppm(out, SHOW[k], S_NAME[s]);
        }
    {   /* every FM6 chart (the lint above, fmp_check, runs on each), in MONO: gray */
        uint32_t a, c0 = fmp_charts;
        for (a = 1; a <= 32u; a++) {
            char name[32];
            snprintf(name, sizeof name, "FM6 ALG %u", a);
            cur_name = name;
            state(); pal(UI_MONO_INDEX); eng(ENGI_FM6); TSEL->p[P_E0] = (int16_t)a; go_title("EDIT 1");
            draw(-1);
            lint();
            mono_check();
        }
        if (fmp_charts - c0 != 32u) { fprintf(stderr, "ui_render: %u FM6 charts drawn of 32\n", fmp_charts - c0); return 1; }
    }
    sweep_columns();
    roll_frames(argc > 2 ? argv[2] : 0);
    /* draw cost: a full redraw of a screen (all strips), and HOME frame by frame (the scope, every other frame) */
    {
        static const int COST[] = {S_HOME, S_PRESETS, S_STEP, S_ROLL_CHORDS, S_DRUM, S_MIXER, S_MENU, S_ABOUT_CREDITS, S_CONFIRM_PROJ};
        enum { N = 200 };
        uint32_t i, k;
        fprintf(rep, "\ndraw cost on the host (cc -O1; only the ratios mean anything on the device):\n");
        fprintf(rep, "  screen            full redraw us   texts+icons   glyph px read\n");
        for (k = 0; k < sizeof COST / sizeof COST[0]; k++) {
            clock_t c0;
            uint64_t px, nt;
            setup(COST[k]); pal(1);
            draw(COST[k]);
            px_visited = n_text = 0;
            c0 = clock();
            for (i = 0; i < N; i++) draw(COST[k]);
            px = px_visited / N; nt = n_text / N;
            fprintf(rep, "  %-16s %15.1f %13llu %15llu\n", S_NAME[COST[k]], (double)(clock() - c0) / CLOCKS_PER_SEC / N * 1e6,
                    (unsigned long long)nt, (unsigned long long)px);
        }
        {
            clock_t c0;
            setup(S_HOME); pal(1); draw(S_HOME);
            px_visited = n_text = 0;
            c0 = clock();
            for (i = 0; i < N * 4; i++) { ui_draw(); nscr = npend = 0; }
            fprintf(rep, "  HOME, a frame without force (lazy strips): %.1f us, %llu texts\n",
                    (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6, (unsigned long long)(n_text / (N * 4)));
        }
        {   /* the STEP page's piano roll: its panel alone redrawn, and a frame that changes nothing (the signature only) */
            static const int G[] = {S_ROLL_CHORDS, S_ROLL_PLAYING, S_DRUM};
            for (k = 0; k < 3u; k++) {
                clock_t c0;
                double tg, ti;
                setup(G[k]); pal(1); draw(G[k]);
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui.force = 1; draw_graph(); ui.force = 0; nscr = npend = 0; }
                tg = (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6;
                song.playing = 0;                        /* (no playhead moving) */
                ui_draw(); ui_draw(); nscr = npend = 0;
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui.frame++; draw_graph(); nscr = npend = 0; }
                ti = (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6;
                fprintf(rep, "  %s: the panel redrawn %.1f us; an unchanged frame (signature only) %.2f us\n", S_NAME[G[k]], tg, ti);
            }
        }
        roll_cost();
    }
    if (audf) fclose(audf);
    fprintf(rep, "\n%u lint findings, %u ellipsised free texts, %u MONO pixels off gray; %u column values swept; "
            "%u FM6 charts linted\n", nfind, nfree, mono_bad, nsweep, fmp_charts);
    fclose(rep);
    printf("ui_render: %u screens x %u palettes + the page/value sweep; %u lint findings, %u ellipsised free texts (%u distinct), "
           "%u MONO pixels off gray; report %s\n", (unsigned)(S_COUNT - (FELUCCA_FM4 ? 0 : 1 + S_OP_LEVEL - S_ALG1 + 1)), (unsigned)NPALETTES, nfind, nfree, nfree_seen, mono_bad, path);
    return nfind || mono_bad ? 1 : 0;
}
