/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the ChoralRoot screen renderer (firmware/src/cr_draw.c on gfx.c, the LCD a 240 x 240 array as in
 * tests/ui_test.c), run by tests/run_cr_draw.sh:
 *   cr_draw_test OUTDIR
 * Renders every mock-up state of tests/cr_screens_gen.h (tests/gen_cr_screens.py from
 * design/choralroot-fm1-mockups.json) in the MOD palette, settled (anim cleared, anim_ms 0) and mid-animation (the
 * state's anim at CR_TEST_MID ms), to OUTDIR/ppm/<nn>_<slug>.ppm and <nn>_<slug>_mid.ppm (tests/run_cr_draw.sh
 * makes the PNGs and the contact sheet).
 * Layout lint (gfx.c GFX_HOOK_TEXT, as tests/ui_render.c): the ink box of every text as it lands on the screen; a
 * finding: a box off the screen, two texts overlapping, a text on the ring's band. Ellipsised texts are listed.
 * Checks: every glyph of the cr faces fits cr_gfx.c's buffer; the draw cache (unchanged: nothing drawn; one line
 * changed: one strip blitted; cr_draw_invalidate: all six); animations are pure (the same (state, ms) twice gives the
 * same pixels, whatever was drawn between) and settle (past their duration they equal the settled screen).
 * Exit 1 on a finding or a failed check. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define __attribute__(x)

static uint16_t host_screen[240 * 240];
static uint32_t host_blits;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blits++;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)   /* (cr_draw_black: the screen off) */
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}

/* the layout lint's hook: ink boxes in canvas rows; the strip's top row makes them screen rows */
typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[40]; } tbox_t;
static tbox_t boxes[256];
static uint32_t nbox;
static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)

#define FELUCCA_KEYCAPS 0                        /* as choralroot.c: no Felucca keycaps */
#include "../firmware/src/gfx.c"
#include "../firmware/src/cr_gfx.c"
#include "../firmware/src/core.h"        /* (the compiled classic layouts are core.h eng_page_t / eng_screen_t) */
#include "../firmware/src/cr_layout.c"   /* the compiled layouts (build/gen/cr_layouts.h): P5's rows; CR_K_FREE in the
                                          * second build, tests/run_cr_draw.sh: the panel's table */
#include "../firmware/src/cr_draw.c"
#include "cr_screens_gen.h"
#include "../firmware/src/cr_engine.h"   /* cr_chord_name: the names as the engine writes them (run_cr_draw.sh links cr_engine.c) */

#define CR_TEST_MID 80u                   /* ms into the animations: mid-squeeze, mid-slide, 2 stripes filled */

static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    uint32_t i;
    tbox_t b;
    b.x0 = (int16_t)x0; b.x1 = (int16_t)x1;
    b.y0 = (int16_t)(y0 - cv_oy); b.y1 = (int16_t)(y1 - cv_oy);     /* canvas rows -> screen rows */
    b.flags = (uint8_t)(flags & ~2u);                                 /* (cut by a strip: every strip draws its part) */
    snprintf(b.s, sizeof b.s, "%s", s);
    for (i = 0; i < nbox; i++)
        if (!memcmp(&boxes[i], &b, 8) && !strcmp(boxes[i].s, b.s)) return;   /* the same text from another strip */
    if (nbox < 256u) boxes[nbox++] = b;
}

static FILE *rep;
static uint32_t nfind, nell, nfail;
static void finding(const char *scr, const char *what, const tbox_t *a, const tbox_t *b)
{
    nfind++;
    fprintf(rep, "LINT %-34s %-22s '%s' (%d,%d)-(%d,%d)", scr, what, a->s, a->x0, a->y0, a->x1, a->y1);
    if (b) fprintf(rep, " / '%s' (%d,%d)-(%d,%d)", b->s, b->x0, b->y0, b->x1, b->y1);
    fputc('\n', rep);
}
static int ring_hits(const tbox_t *b)
{
    int32_t x, y;
    for (y = b->y0; y < b->y1; y++)
        for (x = b->x0; x < b->x1; x++) {
            int32_t dx = 2 * x + 1 - 240, dy = 2 * y + 1 - 240, d2 = dx * dx + dy * dy;   /* (half px) */
            if (d2 >= 2 * 110 * 2 * 110 && d2 <= 2 * 116 * 2 * 116) return 1;
        }
    return 0;
}
static void lint(const char *scr, const cr_screen_t *s)
{
    uint32_t i, j;
    for (i = 0; i < nbox; i++) {
        const tbox_t *b = &boxes[i];
        if ((b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) && !(b->flags & 32u))   /* (sliding: clipped) */
            finding(scr, "off the screen", b, 0);
        if (b->flags & 1u) {
            nell++;
            fprintf(rep, "note %-34s ellipsised '%s'\n", scr, b->s);
        }
        if (s->ring_on && !(b->flags & 32u) && ring_hits(b)) finding(scr, "on the ring", b, 0);
        if (s->dial_on && s->header && b->x1 > 219 && b->y0 < 23) finding(scr, "on the dial", b, 0);
        if (s->message[0] && b->x0 < 120 + 114 && b->x1 > 120 - 114 && b->y0 < 140 && b->y1 > 100) continue;
        for (j = i + 1u; j < nbox; j++) {
            const tbox_t *o = &boxes[j];
            if ((b->flags | o->flags) & 32u) continue;                /* (a sliding text: clipped on purpose) */
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) finding(scr, "overlaps", b, o);
        }
    }
}

static void write_ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/ppm/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb"))) { fprintf(stderr, "cr_draw_test: cannot write %s\n", path); nfail++; return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t c = swap16(host_screen[i]);
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u),
                          (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void check(const char *what, int ok)
{
    printf("cr_draw: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fprintf(rep, "check %-78s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) nfail++;
}

/* draw s at t from scratch: the lint sees every text */
static void render(const cr_screen_t *s, uint32_t t)
{
    nbox = 0;
    cr_draw_invalidate();
    cr_draw(s, t);
}

/* the meter on screen: its stripes (the runs of ink on the row through their middle) -> *all, the lit ones (in the
 * panel's colour) returned */
static uint32_t meter_stripes(const cr_screen_t *s, uint32_t *all)
{
    uint32_t y = (uint32_t)(CR_PY0 + cr_ph(s) - 46 + 7), x, lit = 0, n = 0, in = 0, hot = 0;
    uint16_t bg = swap16(T_BG), on = swap16(cr_rgb(s->col, CR_RED));
    for (x = 0; x <= 240u; x++) {
        uint16_t p = x < 240u ? host_screen[y * 240u + x] : bg;
        if (p != bg) {
            in = 1;
            hot |= p == on;
        } else if (in) {
            n++;
            lit += hot;
            in = hot = 0;
        }
    }
    *all = n;
    return lit;
}

static uint32_t mod_index(void)
{
    uint32_t i;
    for (i = 0; i < NPALETTES; i++)
        if (!strcmp(UI_PALETTES[i].name, "MOD")) return i;
    return NPALETTES;
}

/* the ring's table (cr_draw.c cr_ring, firmware/src/cr_ring_table.h): built here from cr_arc_px, checked against
 * the header byte for byte; CR_RING_GEN=1 rewrites the header */
static int cr_ring_build(cr_ring_t *r)
{
    cr_arcg_t gs, gd;
    uint32_t k = 0, q = 0, s, d;
    int32_t i, j;
    memset(r, 0, sizeof *r);
    cr_arc_geom(&gs, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 0, 0);
    cr_arc_geom(&gd, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 2 * 16, 6 * 16);
    for (s = 0; s <= 16u; s++)
        for (d = 0; d <= s; d++) {
            r->ps[q] = (uint8_t)s;
            r->pd[q++] = (uint8_t)d;
        }
    for (j = 0; j < 240; j++) {
        int runs = 0, open = 0;
        r->off[j] = (uint16_t)k;
        for (i = 0; i < 240; i++) {
            s = cr_arc_px(&gs, i, j);
            if (!s) {
                open = 0;
                continue;
            }
            if (!open) {
                if (runs == 2 || k >= CR_RING_MAX) return 0;
                r->x0[j][runs++] = (uint8_t)i;
                open = 1;
            }
            if (k >= CR_RING_MAX) return 0;
            d = cr_arc_px(&gd, i, j);
            r->cov[k++] = (uint8_t)(s * (s + 1u) / 2u + d);
            r->n[j][runs - 1]++;
        }
    }
    r->off[240] = (uint16_t)k;
    return 1;
}

static void ring_arr(FILE *f, const char *name, const uint8_t *p, uint32_t n, int u16)
{
    uint32_t i;
    fprintf(f, "    /* %s */ {", name);
    for (i = 0; i < n; i++)
        fprintf(f, "%s%u,", i % 20u ? "" : "\n        ", u16 ? ((const uint16_t *)(const void *)p)[i] : p[i]);
    fprintf(f, "\n    },\n");
}

static int ring_check(void)
{
    static cr_ring_t r;
    const char *gen = getenv("CR_RING_GEN");
    FILE *f;
    if (!cr_ring_build(&r)) { fprintf(stderr, "cr_draw_test: the ring's table does not fit CR_RING_MAX\n"); return 0; }
    if (gen && *gen == '1') {
        if (!(f = fopen("firmware/src/cr_ring_table.h", "w"))) return 0;
        fprintf(f, "/* SPDX-License-Identifier: GPL-3.0-only */\n/* generated by tests/cr_draw_test.c (CR_RING_GEN=1 sh "
                   "tests/run_cr_draw.sh): cr_draw.c's cr_ring, the loop ring's pixels (%u) */\n", r.off[240]);
        ring_arr(f, "off", (const uint8_t *)r.off, 241, 1);
        fprintf(f, "    /* x0 */ {");
        { uint32_t j; for (j = 0; j < 240u; j++) fprintf(f, "%s{%u,%u},", j % 10u ? "" : "\n        ", r.x0[j][0], r.x0[j][1]); }
        fprintf(f, "\n    },\n    /* n */ {");
        { uint32_t j; for (j = 0; j < 240u; j++) fprintf(f, "%s{%u,%u},", j % 10u ? "" : "\n        ", r.n[j][0], r.n[j][1]); }
        fprintf(f, "\n    },\n");
        ring_arr(f, "ps", r.ps, 153, 0);
        ring_arr(f, "pd", r.pd, 153, 0);
        ring_arr(f, "cov", r.cov, CR_RING_MAX, 0);
        fclose(f);
        printf("cr_draw_test: wrote firmware/src/cr_ring_table.h (rebuild to check it)\n");
        return 1;
    }
    return !memcmp(&r, &cr_ring, sizeof r);
}

/* a hash of the picture (the goldens) */
static uint32_t screen_hash(void)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 240u * 240u; i++) h = (h ^ host_screen[i]) * 16777619u;
    return h;
}
/* the goldens of a set of pictures: file's lines "slug hash" (write: rewrite it first) -> how many are unchanged, *seen
 * the file's lines */
static uint32_t goldens(const char *file, const char *what, int write, const char (*slug)[40], const uint32_t *h, uint32_t ng,
                        uint32_t *seen)
{
    FILE *gf;
    char ln[128], sl[64];
    unsigned hv;
    uint32_t i, same = 0;
    *seen = 0;
    if (write && (gf = fopen(file, "w"))) {
        fprintf(gf, "# tests/cr_draw_test.c %s: a hash of each picture (rewritten when its variable is 1)\n", what);
        for (i = 0; i < ng; i++) fprintf(gf, "%s %08x\n", slug[i], h[i]);
        fclose(gf);
    }
    if ((gf = fopen(file, "r"))) {
        while (fgets(ln, sizeof ln, gf))
            if (sscanf(ln, "%63s %x", sl, &hv) == 2 && sl[0] != '#') {
                (*seen)++;
                for (i = 0; i < ng; i++)
                    if (!strcmp(slug[i], sl)) {
                        same += h[i] == hv;
                        if (h[i] != hv) fprintf(rep, "golden %s: %08x, not %08x\n", sl, h[i], hv);
                    }
            }
        fclose(gf);
    }
    return same;
}

#if CR_LAY_FREE
/* ------------------------------------------------------ the free screens (CR_K_FREE) --- */
/* a mock-up state of a compiled layout's screen si (index + 1) on lane `lane`: every bound element's cell filled from
 * seed (a knob's value and fill, an led's on, a switch's position, a named value's name), the sound "ITS PROPHET5" */
static void free_state(cr_screen_t *s, uint32_t li, uint32_t si, uint32_t lane, uint32_t seed)
{
    const cr_lay_screen_t *ls = cr_lay_scr(li, si);
    uint32_t r, c, x;
    cr_screen_clear(s);
    s->kind = CR_K_FREE;
    s->fl_eng = (uint8_t)li;
    s->fl_scr = (uint8_t)si;
    s->n_rows = ls->nlanes;
    s->active = (uint8_t)lane;
    snprintf(s->title, sizeof s->title, "ITS PROPHET5");
    snprintf(s->page, sizeof s->page, "%s", cr_lay_str(ls->title));
    for (r = 0; r < ls->nlanes; r++)
        for (c = 0; c < 4u; c++) {
            const cr_lay_elem_t *e;
            cr_cell_t *cl = &s->cell[r][c];
            uint32_t k = seed + r * 4u + c, n = 0, v;
            const char *nm;
            if ((x = cr_lay_slot(ls, r, c)) == CR_LAY_EMPTY) continue;
            e = cr_lay_el(li, ls, x);
            cl->flags = CR_CF_ON;
            snprintf(cl->label, sizeof cl->label, "%s", cr_lay_str(e->label));
            for (nm = cr_lay_names(e); nm; nm = cr_lay_next(nm)) n++;
            if (e->type == CR_E_LED) {
                cl->pct2 = (uint8_t)(k & 1u);
                cl->pct = cl->pct2 ? 255u : 0u;
                snprintf(cl->value, sizeof cl->value, "%s", cl->pct2 ? "on" : "off");
            } else if (e->type == CR_E_SWITCH) {
                cl->pct2 = (uint8_t)(k % (CR_LF_POS(e->flags) == 3u ? 3u : 2u));
                cl->pct = (uint8_t)(cl->pct2 * 127u);
            } else if (n) {                                   /* a knob of named values: the k-th name */
                for (nm = cr_lay_names(e), v = k % n; v; v--) nm = cr_lay_next(nm);
                snprintf(cl->value, sizeof cl->value, "%s", nm);
                cl->pct = (uint8_t)((k % n) * 255u / (n - 1u));
            } else {
                v = (37u * k + 11u) % 128u;
                snprintf(cl->value, sizeof cl->value, "%u", v);
                cl->pct = (uint8_t)(v * 256u / 127u > 255u ? 255u : v * 256u / 127u);
            }
        }
}
/* every screen x lane of the compiled P5 panel, its hot element, REV at both values: PPMs, the lint, the goldens
 * (tests/cr_free_golden.txt: a hash of each picture; CR_FREE_GOLDEN=1 writes it); a knob turn's partial compose against
 * the whole screen; the compose's cost against edit8's FILTER screen */
static void free_screens(const char *dir)
{
    static cr_screen_t s, t;
    static uint16_t part[240 * 240];
    static struct { char slug[40]; uint32_t h; } gold[64];
    uint32_t li = cr_lay_find(CR_LAYOUTS[0].engine), si, lane, ng = 0, ok, f0 = nfind, i, filt = 0, k;
    const cr_layout_t *L = cr_lay(li);
    char name[200], slug[40];
    FILE *gf;
    int write = getenv("CR_FREE_GOLDEN") && !strcmp(getenv("CR_FREE_GOLDEN"), "1");
    check("the compiled layouts: P5's (8 screens, 73 elements)", L && L->nscreens == 8u && L->nelems == 73u);
    if (!L) return;
    for (si = 1; si <= L->nscreens; si++) {
        const cr_lay_screen_t *ls = cr_lay_scr(li, si);
        if (!strcmp(cr_lay_str(ls->title), "FILTER")) filt = si;
        for (lane = 0; lane < ls->nlanes; lane++) {
            free_state(&s, li, si, lane, si);
            render(&s, 0);
            snprintf(slug, sizeof slug, "p5free_%u_%u", si, lane + 1u);
            lint(slug, &s);
            write_ppm(dir, slug);
            snprintf(gold[ng].slug, sizeof gold[ng].slug, "%s", slug);
            gold[ng++].h = screen_hash();
        }
    }
    free_state(&s, li, filt, 0, 0);                          /* FILTER: CUTOFF hot (lane 1, KNOB 2) */
    s.hot_r = 1;
    s.hot_c = 1;
    render(&s, 0);
    lint("p5free_filter_hot", &s);
    write_ppm(dir, "p5free_filter_hot");
    snprintf(gold[ng].slug, sizeof gold[ng].slug, "p5free_filter_hot");
    gold[ng++].h = screen_hash();
    for (k = 0; k < 2u; k++) {                                /* REV (lane 1, KNOB 1): 1/2 (red) and 3 (yellow) */
        free_state(&s, li, filt, 0, 0);
        s.cell[0][0].pct2 = (uint8_t)k;
        snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "%s", k ? "3" : "1/2");
        render(&s, 0);
        snprintf(slug, sizeof slug, "p5free_rev_%u", k);
        lint(slug, &s);
        write_ppm(dir, slug);
        snprintf(gold[ng].slug, sizeof gold[ng].slug, "%s", slug);
        gold[ng++].h = screen_hash();
    }
    for (i = 0, ok = 1; i < ng; i++)
        for (k = i + 1u; k < ng; k++)
            if (gold[i].h == gold[k].h) ok = 0;
    snprintf(name, sizeof name, "P5's panel: %u pictures (19 screen x lane, the hot element, REV both ways), all different, lint clean", ng);
    check(name, ng == 22u && ok && nfind == f0);
    {   /* the goldens */
        uint32_t same = 0, seen = 0;
        char ln[128], sl[64];
        unsigned hv;
        if (write && (gf = fopen("tests/cr_free_golden.txt", "w"))) {
            fprintf(gf, "# tests/cr_draw_test.c free_screens: a hash of each P5 free-screen picture (CR_FREE_GOLDEN=1 rewrites)\n");
            for (i = 0; i < ng; i++) fprintf(gf, "%s %08x\n", gold[i].slug, gold[i].h);
            fclose(gf);
        }
        if ((gf = fopen("tests/cr_free_golden.txt", "r"))) {
            while (fgets(ln, sizeof ln, gf))
                if (sscanf(ln, "%63s %x", sl, &hv) == 2 && sl[0] != '#') {
                    seen++;
                    for (i = 0; i < ng; i++)
                        if (!strcmp(gold[i].slug, sl)) {
                            same += gold[i].h == hv;
                            if (gold[i].h != hv) fprintf(rep, "golden %s: %08x, not %08x\n", sl, gold[i].h, hv);
                        }
                }
            fclose(gf);
        }
        snprintf(name, sizeof name, "P5's panel: the goldens (tests/cr_free_golden.txt): %u of %u pictures unchanged", same, ng);
        check(name, seen == ng && same == ng);
    }
    {   /* a knob turn: CUTOFF's value and the hot mark; drawn alone (its strips), then the mark gone */
        uint32_t strips, d1, d2;
        free_state(&s, li, filt, 0, 0);
        render(&s, 0);
        t = s;
        snprintf(t.cell[0][1].value, sizeof t.cell[0][1].value, "99");
        t.cell[0][1].pct = 200;
        t.hot_r = 1;
        t.hot_c = 1;
        cr_draw(&t, 0);
        strips = cr_dc.drawn;
        memcpy(part, host_screen, sizeof part);
        render(&t, 0);
        d1 = (uint32_t)memcmp(part, host_screen, sizeof part);
        t.hot_r = 0;
        cr_draw(&t, 0);
        memcpy(part, host_screen, sizeof part);
        render(&t, 0);
        d2 = (uint32_t)memcmp(part, host_screen, sizeof part);
        snprintf(name, sizeof name, "a knob turn on FILTER (CUTOFF): %u strips composed, the screen as a whole redraw's; the mark gone, too", strips);
        check(name, strips >= 1u && strips <= 3u && !d1 && !d2);
    }
    {   /* every bound slot of every screen turned (its value, fill, state and the hot mark), then cooled: each partial
         * compose leaves the screen as a whole redraw does (the element's rows, cr_free_ext, hold all it draws) */
        uint32_t turns = 0, bad = 0, c;
        for (si = 1; si <= L->nscreens; si++) {
            const cr_lay_screen_t *ls = cr_lay_scr(li, si);
            for (lane = 0; lane < ls->nlanes; lane++)
                for (c = 0; c < 4u; c++) {
                    if (cr_lay_slot(ls, lane, c) == CR_LAY_EMPTY) continue;
                    free_state(&s, li, si, lane, 3u);
                    render(&s, 0);
                    t = s;
                    snprintf(t.cell[lane][c].value, sizeof t.cell[lane][c].value, "%s", "127");
                    t.cell[lane][c].pct = (uint8_t)(255u - t.cell[lane][c].pct);
                    t.cell[lane][c].pct2 = (uint8_t)(t.cell[lane][c].pct2 ? 0u : 1u);
                    t.hot_r = (uint8_t)(lane + 1u);
                    t.hot_c = (uint8_t)c;
                    cr_draw(&t, 0);
                    memcpy(part, host_screen, sizeof part);
                    render(&t, 0);
                    bad += memcmp(part, host_screen, sizeof part) != 0;
                    t.hot_r = 0;
                    cr_draw(&t, 0);
                    memcpy(part, host_screen, sizeof part);
                    render(&t, 0);
                    bad += memcmp(part, host_screen, sizeof part) != 0;
                    turns++;
                }
        }
        snprintf(name, sizeof name, "every slot of P5's panel turned and cooled (%u): each partial compose as a whole redraw (%u differ)", turns, bad);
        check(name, turns == 56u && !bad);
    }
    {   /* the cost on the host: a whole compose and a knob turn, FILTER (the densest) against edit8's FILTER screen */
        const cr_screen_t *e8 = 0;
        clock_t c0;
        double full_f, full_e, turn_f, turn_e;
        uint32_t n = 400u, drawn_f = 0, drawn_e = 0;
        for (i = 0; i < CR_NSCREENS && !e8; i++)
            if (CR_SCREENS[i].kind == CR_K_EDIT8 && CR_SCREENS[i].wide == CR_W_FILTER) e8 = &CR_SCREENS[i];
        free_state(&s, li, filt, 0, 0);
        c0 = clock();
        for (i = 0; i < n; i++) { cr_draw_invalidate(); cr_draw(&s, 0); }
        full_f = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / n;
        c0 = clock();
        for (i = 0; i < n; i++) {
            snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "%u", i % 128u);
            s.cell[0][1].pct = (uint8_t)(i % 128u * 2u);
            s.hot_r = 1;
            s.hot_c = 1;
            cr_draw(&s, 0);
            drawn_f += cr_dc.drawn;
        }
        turn_f = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / n;
        if (e8) {
            t = *e8;
            t.anim = 0;
            c0 = clock();
            for (i = 0; i < n; i++) { cr_draw_invalidate(); cr_draw(&t, 0); }
            full_e = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / n;
            c0 = clock();
            for (i = 0; i < n; i++) {
                snprintf(t.cell[0][0].value, sizeof t.cell[0][0].value, "%u", i % 128u);
                t.cell[0][0].pct = (uint8_t)(i % 128u * 2u);
                t.hot_r = 1;
                t.hot_c = 0;
                cr_draw(&t, 0);
                drawn_e += cr_dc.drawn;
            }
            turn_e = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / n;
            snprintf(name, sizeof name, "cost (host): a whole compose FILTER free %.0f us, edit8 %.0f us; a knob turn %.0f us (%.1f strips), edit8 %.0f us (%.1f strips)",
                     full_f, full_e, turn_f, (double)drawn_f / n, turn_e, (double)drawn_e / n);
            fprintf(rep, "%s\n", name);
            printf("cr_draw: %s\n", name);
        }
    }
}
#endif /* CR_LAY_FREE */

/* the P5 cell glyphs (cr_draw.c cr_p5glyph; docs/EDITOR.md section 9): edit8 screens of them, no band (two rows, either
 * active), one lane under a band (the full-size row at 124), two lanes under a band (the 22 px box): PPMs, the lint,
 * the goldens (tests/cr_p5cell_golden.txt; CR_P5CELL_GOLDEN=1 writes it); the glyphs' pixels (a lit LED red, the
 * knob's nearest tick white, a wave white when on, mid on the dim row); the one-lane band row is the no-band row
 * moved down 94 px (label 12, the 34 px box, value 17), the two-lane one the small rows (124..180, 184..240) */
static void p5cell_set(cr_cell_t *c, const char *label, uint32_t glyph, uint32_t pct, uint32_t pct2, const char *value)
{
    memset(c, 0, sizeof *c);
    c->flags = CR_CF_ON;
    snprintf(c->label, sizeof c->label, "%s", label);
    snprintf(c->value, sizeof c->value, "%s", value);
    c->glyph = (uint8_t)glyph;
    c->pct = (uint8_t)pct;
    c->pct2 = (uint8_t)pct2;
    if (glyph == CR_G_P5LED2) {                  /* the two value names after the label's NUL */
        size_t l = strlen(c->label);
        memcpy(c->label + l + 1, "1/2", 4);
        memcpy(c->label + l + 5, "3", 2);
    }
}
static void p5cell_screen(cr_screen_t *s, uint32_t wide, uint32_t rows, uint32_t active)
{
    cr_screen_clear(s);
    s->kind = CR_K_EDIT8;
    s->wide = (uint8_t)wide;
    s->n_rows = (uint8_t)rows;
    s->active = (uint8_t)active;
    s->batt = 255;
    snprintf(s->title, sizeof s->title, "ITS PROPHET5");
    snprintf(s->page, sizeof s->page, "%s", wide ? "FILTER" : "OSC A");
    p5cell_set(&s->cell[0][0], "Freq", CR_G_P5KNOB, 128, 0, "64");
    p5cell_set(&s->cell[0][1], "Saw", CR_G_P5LED, 255, CR_P5I_SAW, "On");
    p5cell_set(&s->cell[0][2], "Tri", CR_G_P5LED, 0, CR_P5I_TRI, "Off");
    p5cell_set(&s->cell[0][3], "Rev", CR_G_P5LED2, 0, 0, "1/2");
    if (rows > 1u) {
        p5cell_set(&s->cell[1][0], "Fine", CR_G_P5KNOB, 40, 0, "-12");
        s->cell[1][0].flags |= CR_CF_BIP;
        p5cell_set(&s->cell[1][1], "Pulse", CR_G_P5LED, 255, CR_P5I_SQUARE, "On");
        p5cell_set(&s->cell[1][2], "Key trk", CR_G_P5SWITCH, 128, 0, "Half");
        p5cell_set(&s->cell[1][3], "Sync", CR_G_P5LED, 0, 0, "Off");
    }
}
static uint16_t p5px(int32_t x, int32_t y) { return swap16(host_screen[y * 240 + x]); }
static void p5cell_screens(const char *dir)
{
    static cr_screen_t s;
    static uint16_t a[240 * 240];
    static const struct { const char *slug; uint32_t wide, rows, active; } V[6] = {
        {"p5cell_full_a", CR_W_NONE, 2, 0}, {"p5cell_full_b", CR_W_NONE, 2, 1}, {"p5cell_band1", CR_W_FILTER, 1, 0},
        {"p5cell_band1_env", CR_W_ENV, 1, 0}, {"p5cell_band2_a", CR_W_ENV, 2, 0}, {"p5cell_band2_b", CR_W_FILTER, 2, 1}};
    static struct { char slug[40]; uint32_t h; } gold[16];
    uint32_t ng = 0, i, k, f0 = nfind, sw[3], same = 0, seen = 0;
    int write = getenv("CR_P5CELL_GOLDEN") && !strcmp(getenv("CR_P5CELL_GOLDEN"), "1"), y, x, okm = 1;
    char name[220], ln[128], sl[64];
    unsigned hv;
    FILE *gf;
    for (i = 0; i < 6u; i++) {
        p5cell_screen(&s, V[i].wide, V[i].rows, V[i].active);
        render(&s, 0);
        lint(V[i].slug, &s);
        write_ppm(dir, V[i].slug);
        snprintf(gold[ng].slug, sizeof gold[ng].slug, "%s", V[i].slug);
        gold[ng++].h = screen_hash();
    }
    for (k = 0; k < 3u; k++) {                   /* the lever: Off, Half, Full (none, the left, both LEDs) */
        p5cell_screen(&s, CR_W_NONE, 2, 1);
        s.cell[1][2].pct = (uint8_t)(k * 255u / 2u);
        render(&s, 0);
        sw[k] = (p5px(143, 175) == CR_RED) + (p5px(157, 175) == CR_RED) * 2u;
        if (k == 1u) continue;                   /* (Half: p5cell_full_b) */
        snprintf(gold[ng].slug, sizeof gold[ng].slug, "p5cell_switch_%u", k);
        write_ppm(dir, gold[ng].slug);
        gold[ng++].h = screen_hash();
    }
    p5cell_screen(&s, CR_W_NONE, 2, 0);           /* the glyphs' pixels: row A active at 30, row B dim at 134 */
    render(&s, 0);
    snprintf(name, sizeof name, "P5 cells: a lit LED red (%04x), the knob's nearest tick white (%04x), the saw white when on (%04x), "
             "the dim row's square mid (%04x), the off LED dark (%04x)", p5px(90, 71), p5px(29, 58), p5px(93, 57), p5px(94, 161),
             p5px(150, 71));
    check(name, p5px(90, 71) == CR_RED && p5px(89, 70) == CR_RED && p5px(29, 58) == CR_WHITE && p5px(93, 57) == CR_WHITE &&
                p5px(94, 161) == T_MID && p5px(150, 71) != CR_RED && p5px(149, 71) != CR_RED);
    snprintf(name, sizeof name, "P5 cells: the lever's LEDs count its position (Off %u, Half %u, Full %u: none, the left, both)", sw[0], sw[1], sw[2]);
    check(name, sw[0] == 0u && sw[1] == 1u && sw[2] == 3u);
    {   /* one lane under a band: the no-band screen's row A moved to 124 (label 12, box 34, value 17, bar) */
        p5cell_screen(&s, CR_W_NONE, 1, 0);
        render(&s, 0);
        memcpy(a, host_screen, sizeof a);
        p5cell_screen(&s, CR_W_FILTER, 1, 0);
        render(&s, 0);
        for (y = 124; y < 228 && okm; y++)
            for (x = 0; x < 240; x++)
                if (host_screen[y * 240 + x] != a[(y - 94) * 240 + x]) { okm = 0; break; }
        {
            int32_t lab = -1, val = -1, lab2 = -1, val2 = -1;
            for (i = 0; i < nbox; i++) {
                if (!strcmp(boxes[i].s, "Freq")) lab = boxes[i].y1 - boxes[i].y0;
                if (!strcmp(boxes[i].s, "64")) val = boxes[i].y1 - boxes[i].y0;
            }
            p5cell_screen(&s, CR_W_FILTER, 2, 0);
            render(&s, 0);
            for (i = 0; i < nbox; i++) {
                if (!strcmp(boxes[i].s, "Freq")) lab2 = boxes[i].y0;
                if (!strcmp(boxes[i].s, "64")) val2 = boxes[i].y1;
            }
            snprintf(name, sizeof name, "one lane under a band: rows 124..224 are the no-band row's (label %d px, value %d px of ink); "
                     "two lanes: the small rows (label at %d, value to %d)", lab, val, lab2, val2);
            check(name, okm && lab >= 9 && val >= 12 && lab2 >= 124 && lab2 < 130 && val2 > 160 && val2 <= 180);
        }
    }
    {   /* the layout tool's sketch cell (core.h ENG_C_SKETCH: glyph CR_G_N): the label and value, nothing in the box */
        uint32_t ink = 0;
        p5cell_screen(&s, CR_W_NONE, 2, 0);
        s.cell[0][0].glyph = CR_G_N;
        render(&s, 0);
        for (y = 30 + 19; y < 30 + 68; y++)
            for (x = 0; x < 60; x++) ink += swap16(host_screen[y * 240 + x]) != T_BG;
        for (i = 0, k = 0; i < nbox; i++) k += !strcmp(boxes[i].s, "Freq") || !strcmp(boxes[i].s, "64");
        snprintf(name, sizeof name, "a sketch cell (glyph CR_G_N): its label and value drawn, its glyph area empty (%u px of ink)", ink);
        check(name, ink == 0u && k == 2u);
    }
    for (i = 0; i < ng; i++)
        for (k = i + 1u; k < ng; k++)
            if (gold[i].h == gold[k].h) okm = 0;
    snprintf(name, sizeof name, "P5 cells: %u pictures (no band, one lane and two under a band, the lever Off and Full), all different, lint clean", ng);
    check(name, ng == 8u && okm && nfind == f0);
    if (write && (gf = fopen("tests/cr_p5cell_golden.txt", "w"))) {
        fprintf(gf, "# tests/cr_draw_test.c p5cell_screens: a hash of each P5 cell-glyph picture (CR_P5CELL_GOLDEN=1 rewrites)\n");
        for (i = 0; i < ng; i++) fprintf(gf, "%s %08x\n", gold[i].slug, gold[i].h);
        fclose(gf);
    }
    if ((gf = fopen("tests/cr_p5cell_golden.txt", "r"))) {
        while (fgets(ln, sizeof ln, gf))
            if (sscanf(ln, "%63s %x", sl, &hv) == 2 && sl[0] != '#') {
                seen++;
                for (i = 0; i < ng; i++)
                    if (!strcmp(gold[i].slug, sl)) {
                        same += gold[i].h == hv;
                        if (gold[i].h != hv) fprintf(rep, "golden %s: %08x, not %08x\n", sl, gold[i].h, hv);
                    }
            }
        fclose(gf);
    }
    snprintf(name, sizeof name, "P5 cells: the goldens (tests/cr_p5cell_golden.txt): %u of %u pictures unchanged", same, ng);
    check(name, seen == ng && same == ng);
}

#if CR_LAYC_N
/* P5's rows (the compiled classic layout, build/gen/cr_layouts.h CR_LAYC: design/layouts/designs/p5-rows.json): every
 * screen x lane as the editor's screen model has it, from mock-up states (cr_draw_test draws, it does not edit): the
 * plan's title, band and rows, each bound cell its design's label (else its copy's), its p5 glyph by the cell style
 * (cr_edit.c ce_cell_p5's), a value and a fill from a seed; the band's values from the seed. PPMs, the lint, the goldens
 * (tests/cr_p5rows_golden.txt; CR_P5ROWS_GOLDEN=1 writes it); FILTER with its KNOB 1 hot; every bound cell turned and
 * cooled: each partial compose leaves the screen as a whole redraw does */
static void rows_state(cr_screen_t *s, uint32_t si, uint32_t lane, uint32_t seed)
{
    static const uint8_t W[6] = {CR_W_NONE, CR_W_ENV, CR_W_FILTER, CR_W_FILTER, CR_W_ALGO, CR_W_ADE2};
    static const uint8_t G[7] = {CR_G_P5KNOB, CR_G_P5LED, CR_G_P5LED, CR_G_P5LED, CR_G_P5LED, CR_G_P5LED2, CR_G_P5SWITCH};
    static const uint8_t I[7] = {0, CR_P5I_NONE, CR_P5I_SAW, CR_P5I_TRI, CR_P5I_SQUARE, 0, 0};
    static const char *const SW[3] = {"Off", "Half", "Full"};
    const cr_layc_t *L = &CR_LAYC[0];
    const eng_screen_t *h = &CR_LAYC_SCREENS[L->screen0 + si];
    uint32_t r, c, rows = h->pg[1] == 255u ? 1u : 2u, i;
    cr_screen_clear(s);
    s->kind = CR_K_EDIT8;
    s->wide = h->band < 6u ? W[h->band] : CR_W_NONE;
    s->n_rows = (uint8_t)rows;
    s->active = (uint8_t)lane;
    s->batt = 255;
    snprintf(s->title, sizeof s->title, "ITS PROPHET5");
    snprintf(s->page, sizeof s->page, "%s", h->title);
    for (i = 0; i < 8u; i++) s->wv[i] = (uint8_t)((37u * (seed + i) + 11u) % 128u * 2u);
    for (r = 0; r < rows; r++)
        for (c = 0; c < 4u; c++) {
            const uint8_t *src = CR_LAYC_SRC[L->page0 + h->pg[r]][c];
            const param_desc_t *d = &CR_LAYC_PAGES[L->page0 + h->pg[r]].col[c];
            cr_cell_t *cl = &s->cell[r][c];
            uint32_t k = seed + r * 4u + c, st = h->cell[r][c] - ENG_C_P5KNOB, v;
            if (src[0] == 255u) continue;
            cl->flags = CR_CF_ON;
            snprintf(cl->label, sizeof cl->label, "%s", h->label[r][c] ? h->label[r][c] : d->label);
            cl->glyph = st < 7u ? G[st] : CR_G_NONE;
            cl->pct2 = st < 7u ? I[st] : 0u;
            if (cl->glyph == CR_G_P5KNOB) {
                v = (37u * k + 11u) % (uint32_t)(d->max - d->min + 1) + (uint32_t)d->min;
                snprintf(cl->value, sizeof cl->value, "%u", v);
                cl->pct = (uint8_t)(d->max > d->min ? (v - (uint32_t)d->min) * 255u / (uint32_t)(d->max - d->min) : 0u);
            } else if (cl->glyph == CR_G_P5SWITCH) {
                cl->pct = (uint8_t)(k % 3u * 255u / 2u);
                snprintf(cl->value, sizeof cl->value, "%s", SW[k % 3u]);
            } else {
                cl->pct = (uint8_t)(k & 1u ? 255u : 0u);
                if (cl->glyph == CR_G_P5LED2 && d->names) {      /* the two value names after the label's NUL */
                    size_t l = strlen(cl->label);
                    snprintf(cl->label + l + 1, sizeof cl->label - l - 1, "%s", d->names[0]);
                    l += strlen(d->names[0]) + 1u;
                    snprintf(cl->label + l + 1, sizeof cl->label - l - 1, "%s", d->names[1]);
                    snprintf(cl->value, sizeof cl->value, "%s", d->names[k & 1u]);
                } else {
                    snprintf(cl->value, sizeof cl->value, "%s", k & 1u ? "On" : "Off");
                }
            }
        }
}
static void rows_screens(const char *dir)
{
    static cr_screen_t s, t;
    static uint16_t part[240 * 240];
    static char slug[48][40];
    static uint32_t hs[48];
    const cr_layc_t *L = &CR_LAYC[0];
    uint32_t si, lane, ng = 0, i, k, ok = 1, f0 = nfind, turns = 0, bad = 0, same, seen, filt = 0, c;
    int write = getenv("CR_P5ROWS_GOLDEN") && !strcmp(getenv("CR_P5ROWS_GOLDEN"), "1");
    char name[220];
    check("the compiled layouts: P5's rows (11 screens over 17 pages; no free layout)", CR_LAYC_N == 1u && L->nscreens == 11u &&
          L->npages == 17u && !CR_LAYOUTS_FREE);
    for (si = 0; si < L->nscreens; si++) {
        const eng_screen_t *h = &CR_LAYC_SCREENS[L->screen0 + si];
        if (!strcmp(h->title, "FILTER")) filt = si;
        for (lane = 0; lane < (h->pg[1] == 255u ? 1u : 2u); lane++) {
            rows_state(&s, si, lane, si + 1u);
            render(&s, 0);
            snprintf(slug[ng], sizeof slug[ng], "p5rows_%u_%u", si + 1u, lane + 1u);
            lint(slug[ng], &s);
            write_ppm(dir, slug[ng]);
            hs[ng++] = screen_hash();
        }
    }
    rows_state(&s, filt, 0, 0);                              /* FILTER: CUTOFF hot (KNOB 1) */
    s.hot_r = 1;
    s.hot_c = 0;
    render(&s, 0);
    snprintf(slug[ng], sizeof slug[ng], "p5rows_filter_hot");
    lint(slug[ng], &s);
    write_ppm(dir, slug[ng]);
    hs[ng++] = screen_hash();
    for (i = 0; i < ng; i++)
        for (k = i + 1u; k < ng; k++)
            if (hs[i] == hs[k]) ok = 0;
    snprintf(name, sizeof name, "P5's rows: %u pictures (17 screen x lane, FILTER's KNOB 1 hot), all different, lint clean", ng);
    check(name, ng == 18u && ok && nfind == f0);
    same = goldens("tests/cr_p5rows_golden.txt", "rows_screens", write, (const char (*)[40])slug, hs, ng, &seen);
    snprintf(name, sizeof name, "P5's rows: the goldens (tests/cr_p5rows_golden.txt): %u of %u pictures unchanged", same, ng);
    check(name, seen == ng && same == ng);
    for (si = 0; si < L->nscreens; si++) {          /* every bound cell turned and cooled: partial compose = a whole redraw */
        const eng_screen_t *h = &CR_LAYC_SCREENS[L->screen0 + si];
        for (lane = 0; lane < (h->pg[1] == 255u ? 1u : 2u); lane++)
            for (c = 0; c < 4u; c++) {
                if (CR_LAYC_SRC[L->page0 + h->pg[lane]][c][0] == 255u) continue;
                rows_state(&s, si, lane, 3u);
                render(&s, 0);
                t = s;
                snprintf(t.cell[lane][c].value, sizeof t.cell[lane][c].value, "%s", "127");
                t.cell[lane][c].pct = (uint8_t)(255u - t.cell[lane][c].pct);
                t.hot_r = (uint8_t)(lane + 1u);
                t.hot_c = (uint8_t)c;
                if (s.wide) t.wv[c] = (uint8_t)(255u - t.wv[c]);
                cr_draw(&t, 0);
                memcpy(part, host_screen, sizeof part);
                render(&t, 0);
                bad += memcmp(part, host_screen, sizeof part) != 0;
                t.hot_r = 0;
                cr_draw(&t, 0);
                memcpy(part, host_screen, sizeof part);
                render(&t, 0);
                bad += memcmp(part, host_screen, sizeof part) != 0;
                turns++;
            }
    }
    snprintf(name, sizeof name, "every cell of P5's rows turned and cooled (%u): each partial compose as a whole redraw (%u differ)", turns, bad);
    check(name, turns == 56u && !bad);
}
#endif

int main(int argc, char **argv)
{
    static uint16_t a[240 * 240], b[240 * 240];
    const char *dir = argc > 1 ? argv[1] : "build/cr_screens";
    char path[512], name[160];
    uint32_t i, k, pal = mod_index(), worst = 0;

    snprintf(path, sizeof path, "%s/report.txt", dir);
    if (!(rep = fopen(path, argc > 2 && !strcmp(argv[2], "free") ? "a" : "w"))) {   /* (the free build: after the main one's) */
        fprintf(stderr, "cr_draw_test: cannot write %s (make %s/ppm first)\n", path, dir);
        return 1;
    }
    check("the MOD palette is one of the UI palettes", pal < NPALETTES);
    check("the ring's table (firmware/src/cr_ring_table.h) is cr_ring_build's (CR_RING_GEN=1 rewrites it)", ring_check());
    palette_set(pal < NPALETTES ? pal : 0u);
#if CR_LAY_FREE
    if (argc > 2 && !strcmp(argv[2], "free")) {   /* (tests/run_cr_draw.sh's second build: the panel's free table) */
        free_screens(dir);
        fclose(rep);
        printf("cr_draw: the free screens (P5's panel, tests/cr_free_golden.txt): %u lint findings\n", nfind);
        return nfind || nfail ? 1 : 0;
    }
#endif

    {   /* every glyph of the big faces fits the decode buffer */
        const aafont_t *F[2] = {&AF_CRX, &AF_CRB};
        uint32_t n[2] = {(uint32_t)(AF_CRX.last - AF_CRX.first + 1u + AF_CRX.nex), (uint32_t)(AF_CRB.last - AF_CRB.first + 1u + AF_CRB.nex)};
        int ok = 1;
        for (k = 0; k < 2u; k++)
            for (i = 0; i < n[k]; i++) {
                uint32_t area = (uint32_t)F[k]->g[i].bw * F[k]->g[i].bh;
                if (area > worst) worst = area;
                if (area > CR_GLYPH_MAX) ok = 0;
            }
        snprintf(name, sizeof name, "every CRX / CRB glyph fits cr_gfx.c's buffer (largest %u of %u px)", worst, CR_GLYPH_MAX);
        check(name, ok);
        check("CRX has the chord charset (roots, # b, m dim sus +, M7 9 JAZZ, figures, PANIC)",
              cr_covers(&AF_CRX, "ABCDEFG#b") && cr_covers(&AF_CRX, "mdimsus+") && cr_covers(&AF_CRX, "0123456789") &&
              cr_covers(&AF_CRX, "MJZWTF?PANIC"));
    }

    for (i = 0; i < CR_NSCREENS; i++) {
        cr_screen_t s = CR_SCREENS[i];
        s.anim = 0;
        render(&s, 0);
        lint(CR_SCREEN_SLUGS[i], &s);
        write_ppm(dir, CR_SCREEN_SLUGS[i]);
        fprintf(rep, "screen %-34s %u texts\n", CR_SCREEN_SLUGS[i], nbox);
        render(&CR_SCREENS[i], CR_TEST_MID);
        snprintf(name, sizeof name, "%s_mid", CR_SCREEN_SLUGS[i]);
        lint(name, &CR_SCREENS[i]);
        write_ppm(dir, name);
    }

    {   /* the cache */
        cr_screen_t s = CR_SCREENS[3];  /* Em in Key Mode */
        s.anim = 0;
        render(&s, 0);
        check("a forced draw blits all six strips", cr_dc.blits == CR_NSTRIP);
        host_blits = 0;
        cr_draw(&s, 0);
        check("the same screen again: nothing drawn, nothing blitted", cr_dc.blits == 0 && host_blits == 0);
        cr_draw(&s, 5000);
        check("a later clock with no animation: nothing drawn", cr_dc.blits == 0);
        snprintf(s.mid, sizeof s.mid, "Key: G");
        cr_draw(&s, 0);
        check("the top line changes: only its strip is blitted", cr_dc.blits == 1);
        s.ring_on = 1;
        cr_draw(&s, 0);
        check("the ring appears: every strip it crosses (all six)", cr_dc.blits == CR_NSTRIP);
        cr_draw_invalidate();
        cr_draw(&s, 0);
        check("cr_draw_invalidate: all six again", cr_dc.blits == CR_NSTRIP);
        {   /* the screen off (cr_ui.c): the whole panel black; the wake's draw, of the very same screen, puts every
             * pixel back (a strip or a tile left to the cache would keep what the panel had). MOD's background is
             * black too, so the host panel is then filled with a word the screen does not hold (counted in it as
             * well): a pixel the draw did not write keeps it */
            static uint16_t before[240 * 240];
            uint32_t i, ink = 0, left = 0;
            memcpy(before, host_screen, sizeof before);
            cr_draw_black();
            for (i = 0; i < 240u * 240u; i++) ink += host_screen[i] != 0;
            check("cr_draw_black: every pixel of the panel black", ink == 0);
            for (i = 0; i < 240u * 240u; i++) left += before[i] == 0xF81Fu, host_screen[i] = 0xF81Fu;
            cr_draw(&s, 0);
            for (i = 0; i < 240u * 240u; i++) left += host_screen[i] == 0xF81Fu;
            check("the draw after it: all six strips whole (115200 bytes), the screen as before, no pixel left unwritten",
                  cr_dc.blits == CR_NSTRIP && cr_dc.drawn == CR_NSTRIP && cr_dc.bytes == 240u * 240u * 2u &&
                  !memcmp(before, host_screen, sizeof before) && left == 0);
        }
    }

    {   /* the meters settled: every fixture's stripes are its `segments`, `pct` of them lit. The sound read-out's
         * (cr_ui.c cu_pool_popup: a stripe per preset; INIT, position 00, is not one): 5 of 25 on FM6's 05 (the preset
         * sheet's 1), none on INIT, every one on the last preset (pool_init, pool_last: device renders, no mock-up),
         * one unlit stripe for a pool of INIT alone (not cr_segs' 12 of `segments` 0) */
        cr_screen_t s;
        uint32_t all, lit, nm = 0, nbad = 0;
        int32_t ip = -1;
        int ok;
        for (k = 0; k < CR_NSCREENS; k++) {
            if (CR_SCREENS[k].kind != CR_K_METER || CR_SCREENS[k].ring_on) continue;
            s = CR_SCREENS[k];
            s.anim = 0;
            render(&s, 0);
            lit = meter_stripes(&s, &all);
            nm++;
            if (all != cr_segs(&s) || lit != cr_filled(&s, s.pct)) {
                nbad++;
                fprintf(rep, "meter %s: %u of %u stripes lit, not %u of %u\n", CR_SCREEN_SLUGS[k], lit, all,
                        cr_filled(&s, s.pct), cr_segs(&s));
            }
            if (ip < 0 && !strcmp(s.label, "Hold OPT to change engine") && !strcmp(s.value, "05")) ip = (int32_t)k;
        }
        snprintf(name, sizeof name, "the %u meters: as many stripes as segments, pct of them lit", nm);
        check(name, nm > 0u && nbad == 0u);
        check("the sound read-out's fixture (FM6, 05, Hold OPT to change engine) is in the table", ip >= 0);
        if (ip >= 0) {
            /* the read-out on the home pages (0.18; cr_ui.c cu_pool_popup): the engine over the number in orange,
             * "Hold OPT to change engine" under the stripes, whole (no ellipsis, inside the screen), nothing on
             * anything else: the chord part's, the bass part's (all orange, its own line, the longest: "FM TONE"
             * with " . solo"), a user's preset with the mark, the widest number, mid-fill. Elsewhere (no `line`): the
             * old label, nothing over the number */
            static const struct { const char *value, *sub, *label, *line; uint8_t col, mark; uint32_t t; const char *slug; } V[] = {
                {"05", "FM PAD", "Hold OPT to change engine", "FM6", CR_COL_WHITE, 0, 0, "pool_home_chord"},
                {"05", "FM PAD", "Hold OPT to change engine", "FM6", CR_COL_WHITE, 0, CR_TEST_MID, "pool_home_fill"},
                {"00", "INIT", "Hold OPT to change engine", "CZ-1", CR_COL_WHITE, 0, 0, "pool_home_init"},
                {"26", "WWWWWWWWWWWW", "Hold OPT to change engine", "FM TONE", CR_COL_WHITE, 1, 0, "pool_home_user"},
                {"88", "ADG", "Hold OPT to change engine", "VA", CR_COL_WHITE, 0, 0, "pool_home_standin"},
                {"17", "DEEP SUB", "BASS + EDIT, hold EDIT: engine", "VA", CR_COL_ORANGE, 0, 0, "pool_home_bass"},
                {"17", "DEEP SUB", "BASS + EDIT, hold EDIT: engine", "FM TONE \267 solo", CR_COL_ORANGE, 0, 0, "pool_home_bass_solo"},
            };
            uint32_t v, j, f0 = nfind, e0 = nell, good = 0;
            uint16_t org = swap16(cr_rgb(CR_COL_ORANGE, 0)), wht = swap16(cr_rgb(CR_COL_WHITE, 0));
            for (v = 0; v < sizeof V / sizeof V[0]; v++) {
                const tbox_t *bl = 0, *bv = 0, *bb = 0;
                uint32_t no = 0, nw = 0, x, y;
                s = CR_SCREENS[ip];
                snprintf(s.value, sizeof s.value, "%s", V[v].value);
                snprintf(s.sub, sizeof s.sub, "%s", V[v].sub);
                snprintf(s.label, sizeof s.label, "%s", V[v].label);
                snprintf(s.line, sizeof s.line, "%s", V[v].line);
                s.line_col = CR_COL_ORANGE;
                s.col = V[v].col;
                s.sub_mark = V[v].mark;
                s.anim = V[v].t ? CR_A_FILL : 0;
                s.pct_from = 0;
                render(&s, V[v].t);
                write_ppm(dir, V[v].slug);
                lint(V[v].slug, &s);
                for (j = 0; j < nbox; j++) {
                    if (!strcmp(boxes[j].s, V[v].line)) bl = &boxes[j];
                    if (!strcmp(boxes[j].s, V[v].value)) bv = &boxes[j];
                    if (!strcmp(boxes[j].s, V[v].label)) bb = &boxes[j];
                }
                if (bl)
                    for (y = (uint32_t)bl->y0; y < (uint32_t)bl->y1; y++)
                        for (x = (uint32_t)bl->x0; x < (uint32_t)bl->x1; x++) {
                            no += host_screen[y * 240u + x] == org;
                            nw += host_screen[y * 240u + x] == wht;
                        }
                ok = bl && bv && bb && bl->y1 <= bv->y0 && bl->y0 >= CR_PY0 && no > 20u && nw == 0u &&
                     !(bl->flags & 1u) && !(bb->flags & 1u) && bb->x0 >= 0 && bb->x1 <= 240 &&
                     bl->x0 + bl->x1 >= 238 && bl->x0 + bl->x1 <= 242;
                if (!ok)
                    fprintf(rep, "pool read-out %s: line %s value %s label %s, %u orange / %u white px in the line\n",
                            V[v].slug, bl ? "drawn" : "MISSING", bv ? "drawn" : "MISSING", bb ? "drawn" : "MISSING", no, nw);
                else if (!v)
                    fprintf(rep, "pool read-out: '%s' (%d,%d)-(%d,%d) over '%s' (%d,%d)-(%d,%d); '%s' (%d,%d)-(%d,%d)\n",
                            bl->s, bl->x0, bl->y0, bl->x1, bl->y1, bv->s, bv->x0, bv->y0, bv->x1, bv->y1, bb->s, bb->x0,
                            bb->y0, bb->x1, bb->y1);
                else if (v == 5u)
                    fprintf(rep, "pool read-out (bass): '%s' (%d,%d)-(%d,%d)\n", bb->s, bb->x0, bb->y0, bb->x1, bb->y1);
                good += (uint32_t)ok;
            }
            snprintf(name, sizeof name, "the home read-out: the engine over the number in orange, the bottom line whole (%u of %u)",
                     good, (unsigned)(sizeof V / sizeof V[0]));
            check(name, good == sizeof V / sizeof V[0] && nfind == f0 && nell == e0);
            s = CR_SCREENS[ip];
            s.anim = 0;
            s.line[0] = 0;
            snprintf(s.label, sizeof s.label, "FM TONE \267 02/16");
            render(&s, 0);
            write_ppm(dir, "pool_picker");
            for (j = 0, ok = 1; j < nbox; j++)
                if (boxes[j].y1 <= CR_PY0 + 40 && boxes[j].y0 >= CR_PY0) ok = 0;
            check("the read-out elsewhere (the picker's preview): nothing over the number", ok);
            /* a favourite's star (0.22, cr_screen_t.star; docs/PRESETS.md "Favourites"): a yellow five-point star
             * before the sound's name, the two centred together, on the pool read-out (the meter's sub; with the
             * overwritten preset's square too), under the save dialog's choice and in the editor's browser (the
             * picker's value), white and orange names. star 0: the frame is the one without the field, every pixel */
            {
                static const struct { uint8_t kind, col, mark; const char *name, *slug; } F[] = {
                    {CR_K_METER, CR_COL_WHITE, 0, "FM PAD", "fav_pool"},
                    {CR_K_METER, CR_COL_WHITE, 1, "WWWWWWWWWWWW", "fav_pool_user"},
                    {CR_K_METER, CR_COL_ORANGE, 0, "DEEP SUB", "fav_pool_bass"},
                    {CR_K_PICKER, CR_COL_WHITE, 0, "TINE EP", "fav_save"},
                    {CR_K_PICKER, CR_COL_ORANGE, 0, "DEEP SUB *", "fav_save_bass"},
                    {CR_K_PICKER, CR_COL_WHITE, 2, "ABCDEFGHIJKL*", "fav_browser"},
                };
                static uint16_t ref[240u * 240u];
                uint32_t v, j, x, y, good = 0, f0 = nfind, e0 = nell;
                for (v = 0; v < sizeof F / sizeof F[0]; v++) {
                    const tbox_t *b = 0;
                    int16_t nx0 = 0, nx1 = 0, ny0 = 0, ny1 = 0;
                    uint32_t ny = 0, out = 0, same = 1, x0 = 240, x1 = 0, y0 = 240, y1 = 0;
                    if (F[v].kind == CR_K_METER) {
                        s = CR_SCREENS[ip];
                        s.anim = 0;
                        snprintf(s.sub, sizeof s.sub, "%s", F[v].name);
                        s.sub_mark = F[v].mark;
                    } else {
                        cr_screen_clear(&s);
                        s.kind = CR_K_PICKER;
                        s.header = 1;
                        if (F[v].mark == 2u) {       /* the editor's browser: the engines, left / right */
                            s.n_items = 6;
                            s.sel = 0;
                            s.orient = 1;
                            snprintf(s.item[0], sizeof s.item[0], "FM6");
                            snprintf(s.item[1], sizeof s.item[1], "FM TONE");
                            snprintf(s.label, sizeof s.label, "engine \267 KNOB 1 its presets");
                            snprintf(s.footer, sizeof s.footer, "SELECT: engine \267 OCT-: cancel \267 OCT+: ok");
                        } else {                     /* the save dialog on its third choice */
                            s.n_items = 3;
                            s.sel = 2;
                            snprintf(s.item[0], sizeof s.item[0], "Overwrite");
                            snprintf(s.item[1], sizeof s.item[1], "Save as new");
                            snprintf(s.item[2], sizeof s.item[2], "Favorite");
                            snprintf(s.label, sizeof s.label, "FM TONE \267 KNOB 4 marks");
                            snprintf(s.footer, sizeof s.footer, "KNOB 4: mark \267 OCT-: close");
                        }
                        s.title_col = CR_COL_MID;
                        snprintf(s.value, sizeof s.value, "%s", F[v].name);
                    }
                    s.col = F[v].col;
                    s.star = 0;
                    render(&s, 0);
                    memcpy(ref, host_screen, sizeof ref);
                    for (j = 0; j < nbox; j++)
                        if (!strcmp(boxes[j].s, F[v].name)) { nx0 = boxes[j].x0; nx1 = boxes[j].x1; }
                    s.star = 1;
                    render(&s, 0);
                    snprintf(name, sizeof name, "%s", F[v].slug);
                    write_ppm(dir, name);
                    lint(name, &s);
                    for (j = 0; j < nbox; j++)
                        if (!strcmp(boxes[j].s, F[v].name)) b = &boxes[j];
                    if (b) { ny0 = b->y0; ny1 = b->y1; }
                    for (y = 0; y < 240u; y++)
                        for (x = 0; x < 240u; x++) {
                            uint32_t c = swap16(host_screen[y * 240u + x]), r = (c >> 11) * 255u / 31u,
                                     g = ((c >> 5) & 63u) * 255u / 63u, bl = (c & 31u) * 255u / 31u;
                            if (r > 200u && g > 140u && g < 215u && bl < 90u) {    /* the role yellow, not the orange */
                                ny++;
                                x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1; y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
                            }
                            /* outside the name's row nothing changes */
                            if (b && ((int32_t)y < ny0 - 4 || (int32_t)y >= ny1 + 4) && host_screen[y * 240u + x] != ref[y * 240u + x])
                                same = 0;
                        }
                    out = !b || ny < 18u || (int32_t)x1 >= b->x0 || (int32_t)y0 < ny0 - 3 || (int32_t)y1 > ny1 + 1 || x1 - x0 > 16u || y1 - y0 > 16u;
                    ok = b && !out && same && b->x0 > nx0 && b->x1 - b->x0 >= nx1 - nx0 - 1 && b->x1 - b->x0 <= nx1 - nx0 + 1 && !(b->flags & 1u) &&
                         (int32_t)x0 + b->x1 + (F[v].mark == 1u ? 9 : 0) >= 236 && (int32_t)x0 + b->x1 + (F[v].mark == 1u ? 9 : 0) <= 244;
                    if (!ok)
                        fprintf(rep, "favourite %s: name %s, %u yellow px in (%u,%u)-(%u,%u), name (%d..%d was %d..%d) rows %d..%d, "
                                "rest %s\n", F[v].slug, b ? "drawn" : "MISSING", ny, x0, y0, x1, y1, b ? b->x0 : 0, b ? b->x1 : 0, nx0,
                                nx1, ny0, ny1, same ? "same" : "CHANGED");
                    else
                        fprintf(rep, "favourite %s: the star %u yellow px in (%u,%u)-(%u,%u), the name at %d..%d (alone: %d..%d)\n",
                                F[v].slug, ny, x0, y0, x1, y1, b->x0, b->x1, nx0, nx1);
                    good += (uint32_t)ok;
                }
                snprintf(name, sizeof name, "a favourite's star: yellow, before the name, the pair centred, nothing else moves (%u of %u)",
                         good, (unsigned)(sizeof F / sizeof F[0]));
                check(name, good == sizeof F / sizeof F[0] && nfind == f0 && nell == e0);
            }
        }
        if (ip >= 0) {
            s = CR_SCREENS[ip];
            s.anim = 0;
            render(&s, 0);
            lit = meter_stripes(&s, &all);
            ok = lit == 5u && all == 25u;
            snprintf(s.value, sizeof s.value, "00");
            snprintf(s.sub, sizeof s.sub, "INIT");
            snprintf(s.label, sizeof s.label, "FM6 \267 00/25");
            s.pct = 0;
            render(&s, 0);
            write_ppm(dir, "pool_init");
            lit = meter_stripes(&s, &all);
            ok = ok && lit == 0u && all == 25u;
            snprintf(s.value, sizeof s.value, "25");
            snprintf(s.sub, sizeof s.sub, "PIANO");
            snprintf(s.label, sizeof s.label, "FM6 \267 25/25");
            s.pct = 256;
            render(&s, 0);
            write_ppm(dir, "pool_last");
            lit = meter_stripes(&s, &all);
            snprintf(name, sizeof name, "a pool's meter: 5 of 25 lit on 05, none on INIT, every one on the last (%u of %u)", lit, all);
            check(name, ok && lit == 25u && all == 25u);
            snprintf(s.value, sizeof s.value, "00");
            snprintf(s.sub, sizeof s.sub, "INIT");
            snprintf(s.label, sizeof s.label, "FM6 \267 00/00");
            s.pct = 0;
            s.segments = 1;
            render(&s, 0);
            lit = meter_stripes(&s, &all);
            check("a pool of INIT alone: one stripe, not lit", lit == 0u && all == 1u);
        }
    }

    {   /* a meter's fill (CR_A_FILL): a stripe every CR_FILL_MS; more than CR_FILL_MAX to light: all of them in the
         * time of CR_FILL_MAX (a pool's meter, at most 32 stripes, is full inside the popup's 900 ms) */
        cr_screen_t s;
        uint32_t all, lit[5];
        cr_screen_clear(&s);
        s.kind = CR_K_METER;
        s.batt = 255;
        s.col = CR_COL_WHITE;
        s.thick = 14;
        s.pos = -1;
        snprintf(s.value, sizeof s.value, "64");
        s.anim = CR_A_FILL;
        s.pct = 256;
        s.segments = 12;                /* a knob's meter: 30 ms a stripe, as ever */
        render(&s, 12u * CR_FILL_MS - 1u);
        lit[0] = meter_stripes(&s, &all);
        render(&s, 12u * CR_FILL_MS);
        lit[1] = meter_stripes(&s, &all);
        snprintf(name, sizeof name, "a meter's fill: 12 stripes, one every %u ms (%u lit at %u ms, %u at %u)", CR_FILL_MS,
                 lit[0], 12u * CR_FILL_MS - 1u, lit[1], 12u * CR_FILL_MS);
        check(name, lit[0] == 11u && lit[1] == 12u && all == 12u);
        s.segments = 32;                /* a long pool's last preset */
        render(&s, CR_FILL_MAX * CR_FILL_MS / 2u);
        lit[0] = meter_stripes(&s, &all);
        render(&s, CR_FILL_MAX * CR_FILL_MS - 1u);
        lit[1] = meter_stripes(&s, &all);
        render(&s, CR_FILL_MAX * CR_FILL_MS);
        lit[2] = meter_stripes(&s, &all);
        s.pct_from = 248;               /* a step from the place before: one stripe, at once */
        render(&s, CR_FILL_MS);
        lit[3] = meter_stripes(&s, &all);
        s.pct_from = 256;               /* back to INIT: emptied in the same time */
        s.pct = 0;
        render(&s, CR_FILL_MAX * CR_FILL_MS);
        lit[4] = meter_stripes(&s, &all);
        snprintf(name, sizeof name, "32 stripes fill in the %u ms of %u (%u lit halfway, %u at %u ms, %u at %u)",
                 CR_FILL_MAX * CR_FILL_MS, CR_FILL_MAX, lit[0], lit[1], CR_FILL_MAX * CR_FILL_MS - 1u, lit[2],
                 CR_FILL_MAX * CR_FILL_MS);
        check(name, lit[0] == 16u && lit[1] == 31u && lit[2] == 32u && lit[3] == 32u && lit[4] == 0u && all == 32u);
        {   /* a step of the sound read-out (cr_ui.c cu_animate: pct_from is the place shown before): only the stripes
             * between the two places move, never fewer than the lower place lit nor more than the higher; down: they
             * go dark one by one; a wrap (25 -> none): every frame between, done in the time of CR_FILL_MAX */
            uint32_t t, lo = 99u, hi = 0u, ok = 1u, f;
            s.segments = 25;
            s.pct_from = 5u * 256u / 25u;   /* 05 -> 06 */
            s.pct = 6u * 256u / 25u;
            for (t = 0; t <= 4u * CR_FILL_MS; t += 5u) {
                render(&s, t);
                f = meter_stripes(&s, &all);
                lo = f < lo ? f : lo;
                hi = f > hi ? f : hi;
                ok &= f == (t < CR_FILL_MS ? 5u : 6u) && cr_anim_busy(&s, t) == (t < CR_FILL_MS);
            }
            snprintf(name, sizeof name, "a step up (05 -> 06 of 25): %u..%u stripes lit, the sixth after %u ms", lo, hi,
                     CR_FILL_MS);
            check(name, ok && lo == 5u && hi == 6u && all == 25u);
            s.pct_from = 6u * 256u / 25u;   /* 06 -> 03: 6, 5, 4, 3 */
            s.pct = 3u * 256u / 25u;
            for (t = 0, ok = 1u; t <= 5u * CR_FILL_MS; t += 5u) {
                render(&s, t);
                f = meter_stripes(&s, &all);
                ok &= f == (t >= 3u * CR_FILL_MS ? 3u : 6u - t / CR_FILL_MS);
            }
            check("a step down (06 -> 03): the stripes go dark one every 30 ms, never more than 6 lit", ok);
            s.pct_from = 256;               /* the last -> INIT */
            s.pct = 0;
            for (t = 0, ok = 1u, hi = 25u; t <= CR_FILL_MAX * CR_FILL_MS; t += 15u) {
                render(&s, t);
                f = meter_stripes(&s, &all);
                ok &= f <= hi;
                hi = f;
            }
            ok &= cr_fill_at(25u, 0u, CR_FILL_MAX * CR_FILL_MS - 1u) == 1u && cr_fill_at(25u, 0u, CR_FILL_MAX * CR_FILL_MS) == 0u &&
                  cr_fill_at(7u, 7u, 0u) == 7u && cr_fill_at(0u, 14u, 14u * CR_FILL_MS - 1u) == 13u;
            snprintf(name, sizeof name, "a wrap (25 -> none): darker frame by frame, none lit at %u ms", CR_FILL_MAX * CR_FILL_MS);
            check(name, ok && hi == 0u);
        }
    }

    {   /* the corner dial (a loop playing): red in its box in the top line, none in the panel's centre; the downbeat's
         * frame (5 px) redder than a plain one; the right text clear of it; its fraction alone: the top strip only */
        cr_screen_t s = CR_SCREENS[3];  /* Em in Key Mode */
        uint32_t box0, box1, mid0, mid1, x, y, rx1 = 0, i;
        uint16_t red = swap16(CR_RED);
#define DIAL_RED(x0, y0, x1, y1, n) do { n = 0; for (y = (y0); y < (y1); y++) for (x = (x0); x < (x1); x++) \
                                                n += host_screen[y * 240u + x] == red; } while (0)
        s.anim = 0;
        snprintf(s.right, sizeof s.right, "Dub 3.2");
        s.dial_on = 1;
        s.dial = 158;                   /* 62 % */
        render(&s, 0);
        write_ppm(dir, "loop_dial");
        DIAL_RED(218, 0, 240, 26, box0);
        DIAL_RED(30, 90, 210, 150, mid0);
        for (i = 0; i < nbox; i++)
            if (!strcmp(boxes[i].s, "Dub 3.2")) rx1 = (uint32_t)boxes[i].x1;
        check("the corner dial: red in its box, none in the panel's centre", box0 > 20u && mid0 == 0u);
        check("the corner dial: the right text moves left of it", rx1 && rx1 <= 219u);
        s.dial_pulse = 1;
        render(&s, 0);
        write_ppm(dir, "loop_dial_pulse");
        DIAL_RED(218, 0, 240, 26, box1);
        DIAL_RED(30, 90, 210, 150, mid1);
        check("the corner dial: the downbeat's frame has more red than a plain one", box1 > box0 && mid1 == 0u);
        s.dial_pulse = 0;
        cr_draw(&s, 0);
        s.dial = 170;
        cr_draw(&s, 0);
        check("the corner dial's fraction moves: only the top strip is blitted", cr_dc.drawn == 1 && cr_dc.blits == 1);
        {   /* the REC looks (docs/LOOPER.md): armed, no loop yet: the grey track and the REC dot, no arc; the first take:
             * the track red too; an overdub: the playing arc and the dot, which blinks (the top strip alone redrawn) */
            uint32_t armed, play, rec, od0, od1, c0, c1;
            s.dial_mode = CR_DIAL_ARMED;
            s.dial = 0;
            s.dial_dot = 1;
            render(&s, 0);
            write_ppm(dir, "loop_dial_armed");
            DIAL_RED(218, 0, 240, 26, armed);
            DIAL_RED(30, 90, 210, 150, mid0);
            c0 = host_screen[12u * 240u + 229u] == red;
            check("the dial armed: the REC dot red in its middle, no arc, the track grey", c0 && armed > 12u && armed < 40u && mid0 == 0u);
            s.dial_mode = CR_DIAL_PLAY;
            s.dial = 100;
            render(&s, 0);
            DIAL_RED(218, 0, 240, 26, play);
            s.dial_mode = CR_DIAL_REC;
            render(&s, 0);
            write_ppm(dir, "loop_dial_rec");
            DIAL_RED(218, 0, 240, 26, rec);
            check("the dial recording the first take: the track red as well (more red than the arc and the dot)", rec > play + 10u);
            s.dial_mode = CR_DIAL_OD;
            s.dial = 158;
            render(&s, 0);
            write_ppm(dir, "loop_dial_od");
            DIAL_RED(218, 0, 240, 26, od1);
            c1 = host_screen[12u * 240u + 229u] == red;
            s.dial_dot = 0;
            cr_draw(&s, 0);
            DIAL_RED(218, 0, 240, 26, od0);
            c0 = host_screen[12u * 240u + 229u] == red;
            check("the dial overdubbing: the dot blinks, only the top strip is drawn", c1 && !c0 && od1 > od0 + 12u &&
                  cr_dc.drawn == 1 && cr_dc.blits == 1);
            s.dial_mode = CR_DIAL_PLAY;
        }
        s.dial_on = 0;
        s.dial = 0;
        render(&s, 0);
        DIAL_RED(218, 0, 240, 26, box1);
        check("no dial: no red in its box", box1 == 0u);
#undef DIAL_RED
    }

    {   /* the ring's table (cr_draw.c cr_ring_draw): pixel for pixel the two cr_arc calls it replaces, over a pattern,
         * at every fraction and both colours */
        static uint16_t ref[240 * 40];
        uint32_t f, k, p, diffs = 0, maxd = 0;
        cr_screen_t s;
        cr_screen_clear(&s);
        s.ring_on = 1;
        for (f = 0; f <= 257u; f++)
            for (k = 0; k < CR_NSTRIP; k++) {
                s.ring = (uint16_t)(f > 256u ? 300u : f);
                s.ring_rec = (uint8_t)(f & 1u);
                s.ring_col = (uint8_t)(f % 3u ? CR_COL_BLUE : CR_COL_NONE);
                cv_begin(240u, CR_STRIP_H, T_BG);
                cv_oy = -(int32_t)(k * CR_STRIP_H);
                cr_clip_all();
                for (p = 0; p < 240u * CR_STRIP_H; p++) cv_px[p] = (uint16_t)(p * 2654435761u >> 16);
                cr_arc(120 * 16, 120 * 16, 113 * 16, 5 * 16, 0, 65536u, 0, 2 * 16, 6 * 16, T_LINE);
                if (s.ring)
                    cr_arc(120 * 16, 120 * 16, 113 * 16, 5 * 16, 49152u, s.ring >= 256u ? 65536u : (uint32_t)s.ring << 8,
                           0, 0, 0, s.ring_rec ? T_REC : cr_rgb(s.ring_col, CR_RED));
                memcpy(ref, cv_px, sizeof ref);
                for (p = 0; p < 240u * CR_STRIP_H; p++) cv_px[p] = (uint16_t)(p * 2654435761u >> 16);
                cr_ring_draw(&s);
                for (p = 0; p < 240u * CR_STRIP_H; p++)
                    if (cv_px[p] != ref[p]) {
                        uint32_t a = swap16(cv_px[p]), b = swap16(ref[p]), d;
                        d = (uint32_t)abs((int)(a >> 11) - (int)(b >> 11));
                        if ((uint32_t)abs((int)((a >> 5) & 63u) - (int)((b >> 5) & 63u)) > d)
                            d = (uint32_t)abs((int)((a >> 5) & 63u) - (int)((b >> 5) & 63u));
                        if ((uint32_t)abs((int)(a & 31u) - (int)(b & 31u)) > d) d = (uint32_t)abs((int)(a & 31u) - (int)(b & 31u));
                        diffs++;
                        if (d > maxd) maxd = d;
                    }
            }
        cv_oy = 0;
        snprintf(name, sizeof name, "the ring's table: %u band pixels (of %u), as cr_arc at 258 fractions (%u px differ, max %u)",
                 cr_ring.off[240], CR_RING_MAX, diffs, maxd);
        check(name, diffs == 0);
    }

    {   /* the ring's fraction alone moves: only the strips its tip crossed are drawn, and the screen is the full draw's */
        uint32_t k, f, same = 1, found = 0, most = 0, sum = 0, steps = 0;
        for (k = 0; k < CR_NSCREENS && !found; k++)
            if (CR_SCREENS[k].ring_on) {
                cr_screen_t s = CR_SCREENS[k];
                found = 1;
                s.anim = 0;
                s.ring = 0;
                render(&s, 0);
                for (f = 3; f <= 270u; f += 3) {
                    s.ring = (uint16_t)(f == 270u ? 0u : f);   /* (and back to 0: the loop starts again) */
                    cr_draw(&s, 0);
                    memcpy(a, host_screen, sizeof a);
                    if (f < 270u) {                            /* (the jump back to 0 clears the whole ring) */
                        if (cr_dc.blits > most) most = cr_dc.blits;
                        sum += cr_dc.blits;
                        steps++;
                    }
                    cr_draw_invalidate();
                    cr_draw(&s, 0);
                    same &= !memcmp(a, host_screen, sizeof a);
                }
            }
        snprintf(name, sizeof name, "the ring's fraction moves: the strips its tip crosses (avg %.1f, max %u), as a full draw%s",
                 (double)sum / (double)(steps ? steps : 1u), most, same ? "" : " (DIFFERS)");
        check(name, found && same && most <= 3u);
    }

    {   /* the scope: its samples are in the signature; one moved sample redraws, the same trace costs nothing */
        uint32_t k, found = 0;
        for (k = 0; k < CR_NSCREENS; k++)
            if (CR_SCREENS[k].kind == CR_K_SCOPE && CR_SCREENS[k].wave[60]) {
                cr_screen_t s = CR_SCREENS[k];
                found = 1;
                render(&s, 0);
                cr_draw(&s, 0);
                check("scope: the same trace again: nothing drawn", cr_dc.blits == 0);
                s.wave[120] = (int8_t)(s.wave[120] > 0 ? -100 : 100);
                cr_draw(&s, 0);
                check("scope: a sample moved: redrawn, only the strips it crosses blitted",
                      cr_dc.blits > 0 && cr_dc.blits < CR_NSTRIP);
                break;
            }
        check("scope: a sounding scope state is in the table", found);
    }

    {   /* the pictograms (FORMAT.md "cell glyphs"): ink inside their box and none outside it, and each changes with its
         * value the way its picture says */
        static const struct { uint8_t g; const char *name; } G[] = {
            {CR_G_ROOM, "room"}, {CR_G_MOON, "moon"}, {CR_G_ECHOES, "echoes"}, {CR_G_LFO, "lfo"}, {CR_G_CLIP, "clip"},
            {CR_G_MIX, "mix"}, {CR_G_GATE, "gate"}, {CR_G_RANGE, "range"},
            {CR_G_ARROW, "arrow"}, {CR_G_SHIFT, "shift"},
            /* the FX layer's (0.18, docs/FX.md) */
            {CR_G_TAIL, "tail"}, {CR_G_PRE, "pre"}, {CR_G_CUT, "cut"}, {CR_G_WIDTH, "width"}, {CR_G_SPREAD, "spread"},
            {CR_G_SYNC, "sync"}, {CR_G_PING, "ping"}};
        enum { BX = 20, BY = 2, BW = 48, BH = 36 };
        uint32_t gi, inside_all = 1;
        uint16_t ink = swap16(CR_NAMED[CR_COL_GREEN]), bg = swap16(T_BG);
#define PIC(gl_, p1, p2) do { cr_cell_t c_; memset(&c_, 0, sizeof c_); c_.flags = CR_CF_ON; c_.glyph = (gl_); \
        c_.pct = (uint8_t)(p1); c_.pct2 = (uint8_t)(p2); cr_cv_begin(); cv_oy = 0; cr_clip_all(); \
        cr_cglyph(&c_, BX * 16, BY * 16, BW * 16, BH * 16, CR_NAMED[CR_COL_GREEN]); } while (0)
#define PX(x, y) cv_px[(y) * 240u + (x)]
        for (gi = 0; gi < sizeof G / sizeof G[0]; gi++) {
            uint32_t in = 0, out = 0, x, y;
            PIC(G[gi].g, 128, 128);
            for (y = 0; y < CR_STRIP_H; y++)
                for (x = 0; x < 240u; x++) {
                    int inb = x >= BX && x < BX + BW && y >= BY && y < BY + BH;
                    if (PX(x, y) != bg) { if (inb) in++; else out++; }
                }
            if (!(in > 20u && out == 0u)) {
                inside_all = 0;
                fprintf(rep, "pictogram %s: %u px in its box, %u outside\n", G[gi].name, in, out);
            }
        }
        check("pictograms: each draws ink inside its 48 x 36 box and none outside it", inside_all);
        {   /* room: the box is fixed and only the far wall grows with pct (the owner 2026-10-11, the second note): on
             * the middle row the box's left edge (the first ink from the glyph box's left) stays, the far wall's half
             * width (from the centre to its left edge) grows, is visible at 0 and never reaches the box (>= 3 px of
             * background between the two strokes at the largest) */
            int32_t w[3], m[3], gap[3], k, x;
            for (k = 0; k < 3; k++) {
                PIC(CR_G_ROOM, k == 2 ? 255 : k * 127, 128);
                for (x = BX + BW / 2; x > BX && PX(x, BY + BH / 2) == bg; x--) {}
                w[k] = BX + BW / 2 - x;
                for (x = BX; x < BX + BW / 2 && PX(x, BY + BH / 2) == bg; x++) {}
                m[k] = BX + BW / 2 - x;
                for (; x < BX + BW / 2 && PX(x, BY + BH / 2) != bg; x++) {}
                for (gap[k] = 0; x < BX + BW / 2 && PX(x, BY + BH / 2) == bg; x++) gap[k]++;
            }
            snprintf(name, sizeof name, "room: only the far wall grows with SIZE (half widths: the far wall %d %d %d px, the box %d %d %d px, between them %d %d %d px)",
                     w[0], w[1], w[2], m[0], m[1], m[2], gap[0], gap[1], gap[2]);
            check(name, w[0] < w[1] && w[1] < w[2] && m[0] == m[1] && m[1] == m[2] && w[0] >= 4 && gap[2] >= 3);
        }
        {   /* moon: the lit (full colour) pixels grow with pct */
            uint32_t n[3], k, x, y;
            for (k = 0; k < 3; k++) {
                PIC(CR_G_MOON, k * 127, 128);
                for (n[k] = 0, y = BY; y < BY + BH; y++) for (x = BX; x < BX + BW; x++) n[k] += PX(x, y) == ink;
            }
            snprintf(name, sizeof name, "moon: the lit part grows with pct (%u %u %u px)", n[0], n[1], n[2]);
            check(name, n[0] < n[1] && n[1] < n[2]);
        }
        {   /* echoes: the bars (ink runs along the row just above the baseline): fewer as the spacing grows */
            uint32_t n[2], k, x, on;
            for (k = 0; k < 2; k++) {
                PIC(CR_G_ECHOES, k * 255, 255);
                for (n[k] = on = 0, x = BX; x < BX + BW; x++) {
                    int i = PX(x, BY + BH - 4) != bg;
                    n[k] += i && !on;
                    on = (uint32_t)i;
                }
            }
            snprintf(name, sizeof name, "echoes: the repeats spread with TIME (%u bars at pct 0, %u at 1)", n[0], n[1]);
            check(name, n[0] > n[1] && n[1] >= 2u);
        }
        {   /* lfo: the zero crossings (ink runs on the centre row) grow with the rate */
            uint32_t n[2], k, x, on;
            for (k = 0; k < 2; k++) {
                PIC(CR_G_LFO, k * 255, 255);
                for (n[k] = on = 0, x = BX; x < BX + BW; x++) {
                    int i = PX(x, BY + BH / 2) != bg;
                    n[k] += i && !on;
                    on = (uint32_t)i;
                }
            }
            snprintf(name, sizeof name, "lfo: the crossings grow with RATE (%u at pct 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 4u);
        }
        {   /* clip: the peak flattens: the top stroke's row (fully inked pixels: not the dashed clip line's half pixels)
             * widens from a point (pct 0: a clean sine) to most of the half cycle (pct 1: nearly square) */
            uint32_t n[2], k, x;
            for (k = 0; k < 2; k++) {
                PIC(CR_G_CLIP, k * 255, 128);
                for (n[k] = 0, x = BX; x < BX + BW / 2; x++) n[k] += PX(x, BY + 2) == ink;
            }
            snprintf(name, sizeof name, "clip: the peak flattens with the drive (%u px wide at pct 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > 2u * n[0] + 8u);
        }
        {   /* mix: the wet square fills; gate / range: wider; shift: the square climbs; arrow: four pictures */
            uint32_t n[2], k, x, y, cyy[2];
            static uint16_t ar[4][240 * 40];
            for (k = 0; k < 2; k++) {
                PIC(CR_G_MIX, k * 255, 128);
                for (n[k] = 0, y = BY; y < BY + BH; y++) for (x = BX; x < BX + BW; x++) n[k] += PX(x, y) == ink;
            }
            snprintf(name, sizeof name, "mix: the wet square fills with the amount (%u px at 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 200u);
            for (k = 0; k < 2; k++) {
                PIC(CR_G_GATE, k * 255, 128);
                for (n[k] = 0, x = BX; x < BX + BW; x++) n[k] += PX(x, BY + 2 + 4) != bg;
            }
            snprintf(name, sizeof name, "gate: the pulse widens with pct (%u px at 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 20u);
            for (k = 0; k < 2; k++) {
                PIC(CR_G_RANGE, k * 255, 128);
                for (n[k] = 0, y = BY; y < BY + BH; y++) for (x = BX; x < BX + BW; x++) n[k] += PX(x, y) == ink;
            }
            snprintf(name, sizeof name, "range: the thick segment grows with pct (%u px at 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 60u);
            for (k = 0; k < 2; k++) {
                uint32_t sy = 0, c = 0;
                PIC(CR_G_SHIFT, k * 255, 128);
                for (y = BY; y < BY + BH; y++) for (x = BX + BW / 2 - 1; x <= BX + BW / 2 + 1; x++) if (PX(x, y) == ink) sy += y, c++;
                cyy[k] = c ? sy / c : 0;
            }
            snprintf(name, sizeof name, "shift: the square climbs the staff with pct (centre row %u at 0, %u at 1)", cyy[0], cyy[1]);
            check(name, cyy[1] + 10u < cyy[0]);
            for (k = 0; k < 4; k++) {
                PIC(CR_G_ARROW, k * 64 + 20, 128);
                memcpy(ar[k], cv_px, sizeof ar[k]);
            }
            check("arrow: up, down, up and down, random: four different pictures",
                  memcmp(ar[0], ar[1], sizeof ar[0]) && memcmp(ar[1], ar[2], sizeof ar[0]) && memcmp(ar[2], ar[3], sizeof ar[0]) &&
                  memcmp(ar[0], ar[2], sizeof ar[0]) && memcmp(ar[0], ar[3], sizeof ar[0]));
        }
#undef PX
#undef PIC
    }

    {   /* the knob row: the hot cell's value on a block of its colour; a cell or the hot cell changing composes only the
         * row's strips */
        uint32_t k, found = 0;
        for (k = 0; k < CR_NSCREENS && !found; k++)
            if (CR_SCREENS[k].kind == CR_K_KNOBROW && !CR_SCREENS[k].hot_r) {
                cr_screen_t s = CR_SCREENS[k];
                uint32_t n0, n1, x, y, ry = (uint32_t)cr_kr_rowy(&s);
                uint16_t orange = swap16(CR_NAMED[CR_COL_ORANGE]);
                found = 1;
                s.anim = 0;
                render(&s, 0);
                for (n0 = 0, y = ry + 50u; y < ry + 70u; y++) for (x = 64; x < 116; x++) n0 += host_screen[y * 240u + x] == orange;
                s.hot_r = 1;
                s.hot_c = 1;
                cr_draw(&s, 0);
                check("knob row: the hot cell composes only the row's strips (2)", cr_dc.drawn == 2u);
                for (n1 = 0, y = ry + 50u; y < ry + 70u; y++) for (x = 64; x < 116; x++) n1 += host_screen[y * 240u + x] == orange;
                write_ppm(dir, "knobrow_hot");
                snprintf(name, sizeof name, "knob row: the hot cell's value on a filled block (%u orange px, %u not hot)", n1, n0);
                check(name, n1 > n0 + 500u);
                memcpy(a, host_screen, sizeof a);
                snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "62");
                s.cell[0][1].pct = 140;
                cr_draw(&s, 0);
                check("knob row: a cell's value changes: only the row's strips composed", cr_dc.drawn == 2u);
                memcpy(a, host_screen, sizeof a);
                cr_draw_invalidate();
                cr_draw(&s, 0);
                check("knob row: the partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
                {   /* PERF / BASS: a text-only cell (Behaviour, Swing) and a null cell (a mode with fewer knobs) */
                    uint16_t bg = host_screen[(ry - 4u) * 240u + 2u];
                    uint32_t ink0 = 0, ink2 = 0;
                    s.hot_r = 0;
                    s.cell[0][0].flags = CR_CF_ON;
                    s.cell[0][0].glyph = CR_G_NONE;
                    snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Behaviour");
                    snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "Chords");
                    memset(&s.cell[0][2], 0, sizeof s.cell[0][2]);
                    cr_draw_invalidate();
                    cr_draw(&s, 0);
                    write_ppm(dir, "knobrow_text_null");
                    for (y = ry; y < ry + 75u && y < 240u; y++)
                        for (x = 0; x < 60u; x++) ink0 += host_screen[y * 240u + x] != bg;
                    for (y = ry; y < ry + 75u && y < 240u; y++)
                        for (x = 124; x < 176u; x++) ink2 += host_screen[y * 240u + x] != bg;
                    snprintf(name, sizeof name, "knob row: a text-only cell (%u ink px) and a null cell (%u: its dash) render",
                             ink0, ink2);
                    check(name, ink0 > 100u && ink2 > 0u && ink2 < ink0);
                }
            }
        check("knob row: a knob row state is in the table", found);
    }

    {   /* the knob row's keyboard band (KEY): the lit tonic yellow in the band, the cells under it; a dim cell grey */
        cr_screen_t s;
        uint32_t x, y, yb = 0, ink = 0, grey = 0, grey0 = 0, ry, k;
        uint16_t yel = swap16(CR_NAMED[CR_COL_YELLOW]), gr = swap16(CR_NAMED[CR_COL_GREY]), bg;
        cr_screen_clear(&s);
        s.kind = CR_K_KNOBROW;
        s.kr_band = 1;
        s.header = 1;
        s.col = CR_COL_YELLOW;
        snprintf(s.label, sizeof s.label, "key");
        snprintf(s.footer, sizeof s.footer, "MIN: minor");
        s.lit = 1u << 7;                              /* C4 */
        s.lit_col[7] = CR_COL_YELLOW;
        for (k = 0; k < 4u; k++) {
            s.cell[0][k].flags = CR_CF_ON;
            snprintf(s.cell[0][k].label, sizeof s.cell[0][k].label, "K%u", k + 1u);
            snprintf(s.cell[0][k].value, sizeof s.cell[0][k].value, "C");
        }
        s.n_rows = 1;
        ry = (uint32_t)cr_kr_rowy(&s);
        render(&s, 0);
        bg = host_screen[(ry - 4u) * 240u + 2u];
        for (y = 30; y < ry; y++) for (x = 0; x < 240u; x++) yb += host_screen[y * 240u + x] == yel;
        for (y = ry; y < 240u && y < ry + 72u; y++) for (x = 0; x < 240u; x++) ink += host_screen[y * 240u + x] != bg;
        write_ppm(dir, "knobrow_keyboard");
        snprintf(name, sizeof name, "knob row, keyboard band: the tonic yellow in the band (%u px), the row under it (%u ink)", yb, ink);
        check(name, yb > 200u && ink > 200u);
        for (y = ry; y < ry + 72u && y < 240u; y++) for (x = 0; x < 60u; x++) grey0 += host_screen[y * 240u + x] == gr;
        s.cell[0][0].flags |= CR_CF_DIM;
        render(&s, 0);
        for (y = ry; y < ry + 72u && y < 240u; y++) for (x = 0; x < 60u; x++) grey += host_screen[y * 240u + x] == gr;
        write_ppm(dir, "knobrow_dim");
        snprintf(name, sizeof name, "knob row: a dim cell is drawn grey (%u grey px, %u not dim)", grey, grey0);
        check(name, grey > grey0 + 30u);
    }

    {   /* the KEY layer's two pages (0.17): page 1 with Single = "Split Bass" (the longest value: inside its cell),
         * page 2: Key plays / Keyboard on KNOB 1 / 2, Snap on KNOB 3 (0.22; grey with Key plays Chords on the Chords
         * keyboard, as here), Numerals on KNOB 4 (0.22); the label carries the page,
         * the footer "SELECT: page" */
        static const char *const LB[4] = {"Tonic", "Scale", "Transpose", "Single"};
        static const char *const VA[4] = {"D", "Minor", "+0", "Full"};
        static uint16_t one[240u * 240u];
        cr_screen_t s;
        uint32_t x, y, ry, k, out = 0, in = 0, c01 = 0, c2 = 0, c3 = 0;
        uint16_t bg;
        cr_screen_clear(&s);
        s.kind = CR_K_KNOBROW;
        s.kr_band = 1;
        s.header = 1;
        s.orient = 1;
        s.col = CR_COL_YELLOW;
        s.title_col = CR_COL_WHITE;
        snprintf(s.label, sizeof s.label, "key \267 keys pick \267 1/2");
        snprintf(s.footer, sizeof s.footer, "SELECT: page \267 MAJ MIN: scale \267 GLO: play");
        s.lit = 1u << 9;
        s.lit_col[9] = CR_COL_YELLOW;
        for (k = 0; k < 4u; k++) {
            s.cell[0][k].flags = CR_CF_ON;
            snprintf(s.cell[0][k].label, sizeof s.cell[0][k].label, "%s", LB[k]);
            snprintf(s.cell[0][k].value, sizeof s.cell[0][k].value, "%s", VA[k]);
        }
        s.n_rows = 1;
        ry = (uint32_t)cr_kr_rowy(&s);
        lint("key_page1", &s);
        render(&s, 0);
        memcpy(one, host_screen, sizeof one);
        snprintf(s.cell[0][3].value, sizeof s.cell[0][3].value, "Split Bass");
        lint("key_page1_splitbass", &s);
        render(&s, 0);
        write_ppm(dir, "key_page1_splitbass");
        for (y = 0; y < 240u; y++)
            for (x = 0; x < 240u; x++) {
                if (host_screen[y * 240u + x] == one[y * 240u + x]) continue;
                if (x >= 180u && y >= ry) in++; else out++;
            }
        snprintf(name, sizeof name, "KEY page 1: \"Split Bass\" changes its own cell only (%u px in it, %u outside)", in, out);
        check(name, in > 40u && out == 0u && sizeof s.cell[0][3].value > strlen("Split Bass"));
        for (k = 0; k < 4u; k++)
            memset(&s.cell[0][k], 0, sizeof s.cell[0][k]);
        s.cell[0][0].flags = s.cell[0][1].flags = s.cell[0][3].flags = CR_CF_ON;
        s.cell[0][2].flags = CR_CF_ON | CR_CF_DIM;
        snprintf(s.cell[0][2].label, sizeof s.cell[0][2].label, "Snap");
        snprintf(s.cell[0][2].value, sizeof s.cell[0][2].value, "Scale");
        snprintf(s.cell[0][3].label, sizeof s.cell[0][3].label, "Numerals");
        snprintf(s.cell[0][3].value, sizeof s.cell[0][3].value, "On");
        snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Key plays");
        snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "Notes");
        snprintf(s.cell[0][1].label, sizeof s.cell[0][1].label, "Keyboard");
        snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "Chords");
        snprintf(s.label, sizeof s.label, "key \267 keys pick \267 2/2");
        lint("key_page2", &s);
        render(&s, 0);
        write_ppm(dir, "key_page2");
        bg = host_screen[(ry - 4u) * 240u + 2u];
        for (y = ry; y < 240u && y < ry + 72u; y++)
            for (x = 0; x < 240u; x++)
                if (host_screen[y * 240u + x] != bg) { if (x < 120u) c01++; else if (x < 180u) c2++; else c3++; }
        snprintf(name, sizeof name, "KEY page 2: Key plays / Keyboard drawn (%u ink px), KNOB 3 Snap, grey (%u), KNOB 4 Numerals (%u)", c01, c2, c3);
        check(name, c01 > 200u && c2 > 80u && c3 > 80u);
        {   /* Numerals' third value (GitHub issue 15): "Solf\350ge" (e grave: CRB's extra) whole in its cell, every glyph
             * drawn (more ink than "On"), the other cells as they were */
            static uint16_t on[240u * 240u];
            uint32_t j, cut = 0, got = 0, c3s = 0, other = 0;
            memcpy(on, host_screen, sizeof on);
            snprintf(s.cell[0][3].value, sizeof s.cell[0][3].value, "Solf\350ge");
            lint("key_page2_solfege", &s);
            render(&s, 0);
            write_ppm(dir, "key_page2_solfege");
            for (j = 0; j < nbox; j++)
                if (!strcmp(boxes[j].s, "Solf\350ge")) { got++; cut |= boxes[j].flags & 1u; got += 10u * (boxes[j].x0 < 180 || boxes[j].x1 > 240); }
            for (y = 0; y < 240u; y++)
                for (x = 0; x < 240u; x++) {
                    if (y >= ry && y < ry + 72u && x >= 180u && host_screen[y * 240u + x] != bg) c3s++;
                    if (x < 180u && host_screen[y * 240u + x] != on[y * 240u + x]) other++;
                }
            snprintf(name, sizeof name, "KEY page 2: Numerals \"Solf\350ge\" whole in KNOB 4's cell (%u ink px against %u for On), the other cells untouched",
                     c3s, c3);
            check(name, got == 1u && !cut && c3s > c3 + 60u && !other && strlen("Solf\350ge") < sizeof s.cell[0][3].value &&
                  glyph_at(&AF_CRB, 0xE8u) >= 0);          /* (e grave is one of CRB's extras: gen_aa_font.py CRB_EXTRAS) */
        }
    }

    {   /* the numeral on the Chord view (0.22, the KEY layer's Numerals; the owner, 2026-10-12: "render in the Blue
         * color we use centered above the chord name, you'd need to shift the chord name down a bit when playing an
         * arp"): blue, bold 16 px, centred on baseline 44. The top line carries no numeral. Checked on the pixels:
         * the big name does not move (numeral_on with no numeral is the picture without it; a numeral adds ink only
         * in rows between the top line and the name); the arp's name moves down 6 px with numeral_on, whether or not
         * a numeral shows, and the rows from the hop's arc down are untouched; the widest numeral ("#vii" + the
         * degree sign) over the tallest names (a sharp root, a superscript, the voicing line under it) keeps 2 px
         * clear of the top line and of the name; the lint finds no overlap */
        static const struct { const char *slug, *mid, *root, *qual, *sup, *num, *line; uint8_t arp, on; } N[] = {
            {"numeral_major", "Key: C", "G", "", "7", "V", "", 0, 1},
            {"numeral_minor", "Key: A minor", "D", "m", "", "iv", "", 0, 1},
            {"numeral_dim", "Key: C", "B", "dim", "", "vii\260", "", 0, 1},
            {"numeral_longest", "Key: G# minor", "G#", "m", "add4", "#vii\260", "voicing +12", 0, 1},
            {"numeral_idle", "Key: C", "G", "", "7", "", "", 0, 1},
            {"numeral_off", "Key: C", "G", "", "7", "", "", 0, 0},
            {"numeral_arp", "Key: C", "G", "", "7", "V", "arp 1/8", 1, 1},
            {"numeral_arp_longest", "Key: G# minor", "G#", "m", "add4", "#vii\260", "arp 1/8", 1, 1},
            {"numeral_arp_idle", "Key: C", "G", "", "7", "", "arp 1/8", 1, 1},
            {"numeral_arp_off", "Key: C", "G", "", "7", "", "arp 1/8", 1, 0},
            /* Numerals = Solfege (GitHub issue 15): the syllable in the numeral's place, the same checks; the widest
             * syllables (a caps SOL with a mark, an accidental with a mark) over the tallest names */
            {"solfege_major", "Key: C", "G", "", "7", "SOL", "", 0, 1},
            {"solfege_minor", "Key: A minor", "D", "m", "", "fa", "", 0, 1},
            {"solfege_dim", "Key: C", "B", "dim", "", "ti\260", "", 0, 1},
            {"solfege_wide", "Key: G# minor", "G#", "m", "add4", "#fa\260", "voicing +12", 0, 1},
            {"solfege_aug", "Key: A minor", "C#", "+", "", "#MI+", "voicing +12", 0, 1},
            {"solfege_sol_aug", "Key: C", "G#", "+", "add4", "SOL+", "voicing +12", 0, 1},
            {"solfege_arp", "Key: C", "G", "", "7", "SOL", "arp 1/8", 1, 1},
            {"solfege_arp_wide", "Key: G# minor", "G#", "m", "add4", "#MI+", "arp 1/8", 1, 1},
        };
        enum { NN = sizeof N / sizeof N[0] };
        static uint16_t pic[NN][240u * 240u], bare[240u * 240u];
        cr_screen_t s;
        uint32_t k, x, y, f0 = nfind, bad = 0, gap_top = 999, gap_name = 999, w_max = 0;
        uint16_t bg = swap16(T_BG), blue = swap16(CR_NAMED[CR_COL_BLUE]);
        for (k = 0; k < NN; k++) {
            uint32_t pass, n0 = 999, n1 = 0, x0 = 999, x1 = 0, top = 999, nblue = 0;
            for (pass = 0; pass < 2u; pass++) {            /* 0: the same screen without the numeral's text */
                cr_screen_clear(&s);
                s.kind = N[k].arp ? CR_K_ARP : CR_K_CHORD;
                s.header = 1;
                s.batt = 255;
                s.icon = CR_ICON_NONE;
                s.mid_col = CR_COL_YELLOW;
                snprintf(s.mid, sizeof s.mid, "%s", N[k].mid);
                snprintf(s.name.root, sizeof s.name.root, "%s", N[k].root);
                snprintf(s.name.quality, sizeof s.name.quality, "%s", N[k].qual);
                snprintf(s.name.sup, sizeof s.name.sup, "%s", N[k].sup);
                s.name.col_root = s.name.col_quality = CR_COL_WHITE;
                s.name.col_sup = CR_COL_ORANGE;
                snprintf(s.note[0].t, sizeof s.note[0].t, "G3");
                snprintf(s.note[1].t, sizeof s.note[1].t, "B3");
                snprintf(s.note[2].t, sizeof s.note[2].t, "D4");
                snprintf(s.note[3].t, sizeof s.note[3].t, "F#4");
                s.note[0].col = s.note[1].col = s.note[2].col = CR_COL_WHITE;
                s.note[3].col = CR_COL_ORANGE;
                s.n_notes = 4;
                s.pos = 1;
                s.hop_col = CR_COL_WHITE;
                snprintf(s.line, sizeof s.line, "%s", N[k].line);
                s.line_col = N[k].arp ? CR_COL_MID : CR_COL_BLUE;
                s.numeral_on = N[k].on;
                if (pass) snprintf(s.numeral, sizeof s.numeral, "%s", N[k].num);
                render(&s, 0);
                if (!pass) memcpy(bare, host_screen, sizeof bare);
            }
            lint(N[k].slug, &s);
            write_ppm(dir, N[k].slug);
            memcpy(pic[k], host_screen, sizeof pic[k]);
            for (y = 0; y < 240u; y++)
                for (x = 0; x < 240u; x++) {
                    if (y >= 28u && top == 999u && bare[y * 240u + x] != bg) top = y;      /* the name's first row */
                    if (host_screen[y * 240u + x] == bare[y * 240u + x]) continue;
                    nblue += host_screen[y * 240u + x] == blue;
                    if (y < n0) n0 = y;
                    if (y > n1) n1 = y;
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                }
            if (!N[k].num[0]) { bad += n0 != 999u; continue; }
            /* (the ink box: under the top line, over the name, centred on x 120 within a pixel or two of bearing) */
            bad += n0 == 999u || n0 < 24u || n1 >= top || x0 < 8u || x1 > 231u || x0 + x1 < 236u || x0 + x1 > 243u || nblue < 12u;
            if (n0 - 23u < gap_top) gap_top = n0 - 23u;    /* (the top line's last row with ink is 22: "g", "y") */
            if (top - n1 - 1u < gap_name) gap_name = top - n1 - 1u;
            if (x1 - x0 + 1u > w_max) w_max = x1 - x0 + 1u;
        }
        snprintf(name, sizeof name, "numeral / solfege: blue, centred, in the gap: %u px under the top line, %u over the name; the widest %u px",
                 gap_top, gap_name, w_max);
        check(name, !bad && gap_top >= 2u && gap_name >= 2u && nfind == f0);
        check("numeral: the big name does not move (numeral_on, nothing sounding = the picture without Numerals)",
              !memcmp(pic[4], pic[5], sizeof pic[4]));
        {   /* the arp: numeral_on moves the name's rows (28 .. 121) CR_NUM_ARP_DY (6) px down and nothing else */
            uint32_t moved = 1, low = 1, clear = 1, dy = CR_NUM_ARP_DY;
            for (y = 28u; y < 240u; y++)
                for (x = 0; x < 240u; x++) {
                    uint16_t a = pic[9][y * 240u + x], b = pic[8][y * 240u + x];
                    if (y >= 122u) low &= a == b;
                    else moved &= b == (y >= 28u + dy ? pic[9][(y - dy) * 240u + x] : bg);
                    if (y >= 122u - dy && y < 122u) clear &= a == bg;   /* (.. and the name ended that far above the arc) */
                }
            check("numeral: the arp's name sits 6 px lower with numeral_on (a numeral or none), the hop and the notes where they were",
                  moved && low && clear && !memcmp(pic[6] + 122u * 240u, pic[9] + 122u * 240u, 118u * 240u * sizeof pic[0][0]));
        }
    }

    {   /* QUAD's primitives (docs/QUAD.md, design/choralroot-fm1-quad-mockups.json): the algo band, the ade2 band, the
         * filter's base-width window, the harm / detune / lfowave glyphs, big / ratio / span-2 cells, the bipolar knob */
        uint16_t bg = swap16(T_BG), orange = swap16(CR_NAMED[CR_COL_ORANGE]), blue = swap16(CR_NAMED[CR_COL_BLUE]);
        uint16_t mid = swap16(T_MID), line = swap16(T_LINE);
        int32_t ia = -1, id = -1, ib = -1, ir = -1;
        uint32_t x, y, k;
#define HS(x_, y_) host_screen[(uint32_t)(y_) * 240u + (uint32_t)(x_)]
#define COUNT(n_, x0_, y0_, x1_, y1_, cond_) do { n_ = 0; for (y = (y0_); y < (uint32_t)(y1_); y++) \
            for (x = (x0_); x < (uint32_t)(x1_); x++) n_ += (cond_); } while (0)
        for (k = 0; k < CR_NSCREENS; k++) {
            if (CR_SCREENS[k].kind != CR_K_EDIT8) continue;
            if (CR_SCREENS[k].wide == CR_W_ALGO && ia < 0) ia = (int32_t)k;
            if (CR_SCREENS[k].wide == CR_W_ADE2 && id < 0) id = (int32_t)k;
            if (CR_SCREENS[k].wide == CR_W_FILTER && CR_SCREENS[k].wv[4] && ib < 0) ib = (int32_t)k;
            if (CR_SCREENS[k].cell[0][3].glyph == CR_G_RATIO && ir < 0) ir = (int32_t)k;
        }
        check("QUAD: the algo, ade2, base-width and ratio states are in the table", ia >= 0 && id >= 0 && ib >= 0 && ir >= 0);
        if (ib >= 0) {   /* FM TONE's filter types on the band (3y217: "COMB and EQ display OFF"): OFF, MULTI, COMB+,
                          * COMB-, EQ (wv[7] 1, 4..7) at a mid cut, RESO, SHAPE and GAIN: each its own picture, linted
                          * (filter_type_<n>.ppm) */
            static uint16_t pic[5][240 * 100];
            static const uint8_t TY[5] = {1, 4, 5, 6, 7};
            cr_screen_t s = CR_SCREENS[ib];
            uint32_t n, j, differ = 1;
            char an[24];
            for (n = 0; n < 5u; n++) {
                s.wv[0] = 120, s.wv[1] = 200, s.wv[2] = 20, s.wv[3] = 0, s.wv[4] = 0;
                s.wv[7] = TY[n], s.wv[8] = 90, s.wv[9] = 110;
                s.anim = 0;
                render(&s, 0);
                snprintf(an, sizeof an, "filter_type_%u", (unsigned)TY[n]);
                lint(an, &s);
                write_ppm(dir, an);
                memcpy(pic[n], host_screen + 22u * 240u, sizeof pic[0]);
                for (j = 0; j < n; j++)
                    differ &= memcmp(pic[j], pic[n], sizeof pic[0]) != 0;
            }
            check("FM TONE's filter band: OFF, MULTI, COMB+, COMB-, EQ five different pictures (each its name and curve)", differ);
        }
        if (ia >= 0) {   /* algo: the manual's eight diagrams (cr_algo_geom): four boxes where the geometry puts them
                          * (outline columns inked, the letter inside), no two boxes, loops, side lines or labels
                          * overlapping, every algorithm rendered (algo_<n>.ppm) and linted, 8 different pictures */
            static uint16_t pic[8][240 * 100];
            cr_screen_t s = CR_SCREENS[ia];
            uint32_t n, boxes4 = 1, differ = 1, apart = 1, j, thin, thick;
            for (n = 1; n <= 8u; n++) {
                uint32_t found = 0;
                int32_t ox[4], oy[4], sx[4], lx[2], bx[6][4], nb = 0, q, r2;
                char an[16];
                const cr_algo_t *al = &CR_ALGOS[n - 1];
                s.wv[0] = (uint8_t)n;
                s.anim = 0;
                render(&s, 0);
                snprintf(an, sizeof an, "algo_%u", n);
                lint(an, &s);
                write_ppm(dir, an);
                memcpy(pic[n - 1], host_screen + 22u * 240u, sizeof pic[0]);
                cr_algo_geom(n, ox, oy, sx, lx);
                for (k = 0; k < 4u; k++) {
                    uint32_t cx = (uint32_t)(ox[k] / 16), cy = (uint32_t)(oy[k] / 16), el, er, in;
                    COUNT(el, cx - 8u, cy - 6u, cx - 6u, cy + 7u, HS(x, y) != bg);
                    COUNT(er, cx + 6u, cy - 6u, cx + 8u, cy + 7u, HS(x, y) != bg);
                    COUNT(in, cx - 4u, cy - 4u, cx + 5u, cy + 5u, HS(x, y) != bg);
                    found += el >= 13u && er >= 13u && in >= 8u;
                    bx[nb][0] = ox[k] / 16 - 8; bx[nb][1] = oy[k] / 16 - 8;   /* (a box and its outline) */
                    bx[nb][2] = ox[k] / 16 + 8; bx[nb][3] = oy[k] / 16 + 8; nb++;
                    if (sx[k] != ox[k] && !(((al->x | al->y) >> k) & 1u)) apart = 0;
                    for (q = 0; q < 4; q++)          /* a side line passes no other box */
                        if (q != (int32_t)k && sx[k] != ox[k] && oy[q] > oy[k] && sx[k] / 16 + 1 >= ox[q] / 16 - 8 &&
                            sx[k] / 16 - 1 <= ox[q] / 16 + 8) {
                            fprintf(rep, "algo %u: %s's side line crosses %s\n", n, CR_OPS[k], CR_OPS[q]);
                            apart = 0;
                        }
                }
                k = al->fb;                          /* the loop's box: up and left of its operator */
                bx[nb][0] = ox[k] / 16 - 13; bx[nb][1] = oy[k] / 16 - 12 < 24 ? 24 : oy[k] / 16 - 12; bx[nb][2] = ox[k] / 16 - 9; bx[nb][3] = oy[k] / 16 - 8; nb++;
                for (j = 0; j < 2u; j++) {           /* the labels X / Y: 8 x 10 px over the baseline */
                    bx[nb][0] = lx[j] / 16 - 4; bx[nb][1] = 24 + 92 - 9; bx[nb][2] = lx[j] / 16 + 4; bx[nb][3] = 24 + 92; nb++;
                }
                for (q = 0; q < nb; q++) {
                    if (bx[q][0] < 52 || bx[q][2] > 236 || bx[q][1] < 24 || bx[q][3] > 24 + 96) {
                        fprintf(rep, "algo %u: item %d off the band\n", n, (int)q);
                        apart = 0;
                    }
                    for (r2 = q + 1; r2 < nb; r2++)
                        if (bx[q][0] < bx[r2][2] && bx[r2][0] < bx[q][2] && bx[q][1] < bx[r2][3] && bx[r2][1] < bx[q][3]) {
                            fprintf(rep, "algo %u: items %d and %d overlap\n", n, (int)q, (int)r2);
                            apart = 0;
                        }
                }
                if (found != 4u) {
                    boxes4 = 0;
                    fprintf(rep, "algo %u: %u of 4 boxes found\n", n, found);
                }
            }
            for (n = 0; n < 8u; n++)
                for (j = n + 1u; j < 8u; j++) differ &= memcmp(pic[n], pic[j], sizeof pic[0]) != 0;
            check("algo band: four operator boxes (outlines and letters) in every algorithm", boxes4);
            check("algo band: the eight algorithms draw eight different diagrams", differ);
            check("algo band: no boxes, loops, side lines or labels overlap; all on the band (8 algorithms)", apart);
            {                                        /* algo 3: X takes C (dotted) and B2 (dotted), Y B1 (dotted);
                                                      * algo 7: X C dotted, A solid: count the ink on the lines */
                int32_t ox[4], oy[4], sx[4], lx[2];
                uint32_t dots, solid;
                s.wv[0] = 7; s.wv[1] = 0;
                render(&s, 0);
                cr_algo_geom(7, ox, oy, sx, lx);
                COUNT(dots, (uint32_t)(ox[0] / 16) - 1u, (uint32_t)(oy[0] / 16) + 9u, (uint32_t)(ox[0] / 16) + 2u, 24u + 82u, HS(x, y) != bg);
                COUNT(solid, (uint32_t)(sx[1] / 16) - 1u, (uint32_t)(oy[0] / 16) + 9u, (uint32_t)(sx[1] / 16) + 2u, 24u + 82u, HS(x, y) != bg);
                snprintf(name, sizeof name, "algo band: a direct carrier's line dotted, an enveloped one solid (%u < %u ink px)", dots, solid);
                check(name, dots + 3u < solid);
            }
            s.wv[0] = 3;                                         /* A's loop over its top left */
            {
                int32_t ox[4], oy[4], sx[4], lx[2];
                uint32_t lx0, ly0;
                cr_algo_geom(3, ox, oy, sx, lx);
                lx0 = (uint32_t)(ox[1] / 16) - 13u; ly0 = (uint32_t)(oy[1] / 16) - 12u;
                s.wv[1] = 0;
                render(&s, 0);
                COUNT(thin, lx0, ly0, lx0 + 12u, ly0 + 9u, HS(x, y) != bg);
                s.wv[1] = 255;
                render(&s, 0);
                COUNT(thick, lx0, ly0, lx0 + 12u, ly0 + 9u, HS(x, y) != bg);
            }
            snprintf(name, sizeof name, "algo band: the feedback loop's stroke grows with fdbk (%u ink px at 0, %u at 255)", thin, thick);
            check(name, thick > thin + 10u);
            s.wv[1] = 77;
            render(&s, 0);
            s.wv[0] = 5;
            cr_draw(&s, 0);
            snprintf(name, sizeof name, "algo band: a new algorithm composes the band's strips only (%u)", cr_dc.drawn);
            check(name, cr_dc.drawn == 4u);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("algo band: the partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
        }
        if (id >= 0) {   /* ade2: a curve in each half, a level bar at each half's right, filled to its level */
            cr_screen_t s = CR_SCREENS[id];
            uint32_t ca, cb, ba, bb, fa, fb2;
            render(&s, 0);
            COUNT(ca, 10u, 42u, 100u, 110u, HS(x, y) != bg);
            COUNT(cb, 130u, 42u, 220u, 110u, HS(x, y) != bg);
            COUNT(ba, 108u, 42u, 109u, 108u, HS(x, y) != bg);
            COUNT(bb, 228u, 42u, 229u, 108u, HS(x, y) != bg);
            COUNT(fa, 109u, 42u, 110u, 108u, HS(x, y) == orange);        /* A: a segment lit (decay): its colour */
            COUNT(fb2, 229u, 42u, 230u, 108u, HS(x, y) == mid);          /* B: MID */
            snprintf(name, sizeof name, "ade2 band: two curves (%u, %u ink px), two bars (%u, %u rows), filled to A %u / B %u rows",
                     ca, cb, ba, bb, fa, fb2);
            check(name, ca > 150u && cb > 150u && ba >= 60u && bb >= 60u && fa > fb2 + 5u && fb2 > 20u);
        }
        if (ib >= 0) {   /* the base-width window: two dashed orange edges and the dim fill between them; none when off */
            cr_screen_t s = CR_SCREENS[ib];
            uint32_t bx = (128u + 3584u * s.wv[5] / 255u) >> 4, e = s.wv[5] + s.wv[6] > 255u ? 255u : s.wv[5] + s.wv[6];
            uint32_t ex = (128u + 3584u * e / 255u) >> 4, eb, ee, eb0, fill = swap16(ux_mix(T_BG, CR_NAMED[CR_COL_ORANGE], 12));
            int fill_on, fill_off;
            render(&s, 0);
            COUNT(eb, bx, 28u, bx + 1u, 114u, HS(x, y) != bg);
            COUNT(ee, ex, 28u, ex + 1u, 114u, HS(x, y) != bg);
            fill_on = HS(bx + 6u, 100u) == fill;
            s.wv[4] = 0;
            render(&s, 0);
            COUNT(eb0, bx, 28u, bx + 1u, 114u, HS(x, y) != bg);
            fill_off = HS(bx + 6u, 100u) == bg;
            snprintf(name, sizeof name, "filter band, base-width window: dashed edges at %u and %u (%u, %u px; %u off), the dim fill",
                     bx, ex, eb, ee, eb0);
            check(name, eb >= 35u && ee >= 35u && eb0 < 12u && fill_on && fill_off);
        }
        if (ir >= 0) {   /* ratio: the numerator over the denominator in its cell */
            cr_screen_t s = CR_SCREENS[ir];
            int32_t yn = -1, yd = -1, xn = 0;
            render(&s, 0);
            for (k = 0; k < nbox; k++) {
                if (!strcmp(boxes[k].s, "0.50")) yn = boxes[k].y1, xn = boxes[k].x0;
                if (!strcmp(boxes[k].s, "1.00") && boxes[k].x0 >= 180) yd = boxes[k].y0;
            }
            check("ratio cell: the numerator above the denominator, in the fourth column", yn > 0 && yd > yn && xn >= 180);
            s.cell[0][3].pct = 64;                               /* 16.00 / 16.00: both whole, two decimals each */
            s.cell[0][3].pct2 = 64;
            render(&s, 0);
            for (yn = 0, k = 0; k < nbox; k++)
                yn += !strcmp(boxes[k].s, "16.00") && boxes[k].x0 >= 180;
            s.cell[0][3].pct = 1;                                /* 0.25 / 1.00 */
            s.cell[0][3].pct2 = 4;
            render(&s, 0);
            for (yd = 0, k = 0; k < nbox; k++)
                yd += (!strcmp(boxes[k].s, "0.25") || !strcmp(boxes[k].s, "1.00")) && boxes[k].x0 >= 180;
            snprintf(name, sizeof name, "ratio cell: 16.00 over 16.00 (%d), 0.25 over 1.00 (%d): the parts uncut", yn, yd);
            check(name, yn == 2 && yd == 2);
        }
        {   /* the new glyphs: ink, and a change with pct (and the wave) */
            static uint16_t g0[240 * 40];
            static const uint8_t NG[3] = {CR_G_HARM, CR_G_DETUNE, CR_G_LFOWAVE};
            int ok = 1;
            uint32_t gi, w, w2, ink0, ink1;
            cr_cell_t c;
#define GL(gl_, p1_, wv_, fl_) do { memset(&c, 0, sizeof c); c.flags = (uint16_t)(CR_CF_ON | (fl_)); c.glyph = (gl_); \
            c.pct = (uint8_t)(p1_); c.wave = (uint8_t)(wv_); cr_cv_begin(); cv_oy = 0; cr_clip_all(); \
            cr_cglyph(&c, 20 * 16, 2 * 16, 48 * 16, 36 * 16, CR_NAMED[CR_COL_GREEN]); } while (0)
            for (gi = 0; gi < 3u; gi++) {
                uint32_t out;
                GL(NG[gi], 128, 0, 0);
                COUNT(ink0, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                COUNT(out, 0u, 0u, 240u, 40u, cv_px[y * 240u + x] != bg && !(x >= 20u && x < 68u && y >= 2u && y < 38u));
                memcpy(g0, cv_px, sizeof g0);
                GL(NG[gi], 250, 0, 0);
                COUNT(ink1, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                if (ink0 < 40u || ink1 < 40u || out || !memcmp(g0, cv_px, sizeof g0)) {
                    ok = 0;
                    fprintf(rep, "glyph %u: ink %u / %u, %u outside, %s with pct\n", NG[gi], ink0, ink1, out,
                            memcmp(g0, cv_px, sizeof g0) ? "changes" : "SAME");
                }
            }
            check("harm, detune, lfowave: ink in their box, none outside, a change with pct", ok);
            {   /* the FX layer's glyphs of 0.18 (docs/FX.md): ink in the box at both ends of the value, none outside,
                 * and the picture changes with it (pct; SPREAD with pct2 too) */
                static const struct { uint8_t g; const char *name; } FG[7] = {
                    {CR_G_TAIL, "tail"}, {CR_G_PRE, "pre"}, {CR_G_CUT, "cut"}, {CR_G_WIDTH, "width"}, {CR_G_SPREAD, "spread"},
                    {CR_G_SYNC, "sync"}, {CR_G_PING, "ping"}};
                int fok = 1;
                for (gi = 0; gi < 7u; gi++) {
                    uint32_t out, out1, same2 = 0;
                    GL(FG[gi].g, 0, 0, 0);
                    COUNT(ink0, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                    COUNT(out, 0u, 0u, 240u, 40u, cv_px[y * 240u + x] != bg && !(x >= 20u && x < 68u && y >= 2u && y < 38u));
                    memcpy(g0, cv_px, sizeof g0);
                    GL(FG[gi].g, 255, 0, 0);
                    COUNT(ink1, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                    COUNT(out1, 0u, 0u, 240u, 40u, cv_px[y * 240u + x] != bg && !(x >= 20u && x < 68u && y >= 2u && y < 38u));
                    if (ink0 < 40u || ink1 < 40u || out || out1 || !memcmp(g0, cv_px, sizeof g0)) {
                        fok = 0;
                        fprintf(rep, "fx glyph %s: ink %u / %u, %u / %u outside, %s with pct\n", FG[gi].name, ink0, ink1, out, out1,
                                memcmp(g0, cv_px, sizeof g0) ? "changes" : "SAME");
                    }
                    if (FG[gi].g == CR_G_SPREAD) {              /* the lower row closes up as the spread grows */
                        GL(CR_G_SPREAD, 128, 0, 0);
                        memcpy(g0, cv_px, sizeof g0);
                        memset(&c, 0, sizeof c);
                        c.flags = CR_CF_ON; c.glyph = CR_G_SPREAD; c.pct = 128; c.pct2 = 255;
                        cr_cv_begin(); cv_oy = 0; cr_clip_all();
                        cr_cglyph(&c, 20 * 16, 2 * 16, 48 * 16, 36 * 16, CR_NAMED[CR_COL_GREEN]);
                        same2 = !memcmp(g0, cv_px, sizeof g0);
                        if (same2) { fok = 0; fprintf(rep, "fx glyph spread: the same at pct2 0 and 1\n"); }
                    }
                }
                check("tail, pre, cut, width, spread, sync, ping: ink in their box at both ends, none outside, a change with the value", fok);
            }
            GL(CR_G_DETUNE, 0, 0, 0);                            /* detune at 0: one flat line (2 px + its edges) */
            for (y = 0, w = 0; y < 40u; y++) {
                uint32_t r = 0;
                for (x = 20; x < 68u; x++) r += cv_px[y * 240u + x] != bg;
                w += r > 0u;
            }
            GL(CR_G_DETUNE, 255, 0, 0);
            for (y = 0, w2 = 0; y < 40u; y++) {
                uint32_t r = 0;
                for (x = 20; x < 68u; x++) r += cv_px[y * 240u + x] != bg;
                w2 += r > 0u;
            }
            snprintf(name, sizeof name, "detune: flat at 0 (%u rows inked), broken up at 1 (%u rows)", w, w2);
            check(name, w <= 4u && w2 >= 12u);
            {   /* lfowave: seven different waves; the phase slides the shape; a span-2 cell draws two cycles */
                static uint16_t wv7[7][240 * 40];
                uint32_t i2, j2, diff = 1;
                for (i2 = 0; i2 < 7u; i2++) {
                    GL(CR_G_LFOWAVE, 0, i2, 0);
                    memcpy(wv7[i2], cv_px, sizeof wv7[0]);
                }
                for (i2 = 0; i2 < 7u; i2++)
                    for (j2 = i2 + 1u; j2 < 7u; j2++) diff &= memcmp(wv7[i2], wv7[j2], sizeof wv7[0]) != 0;
                check("lfowave: tri, sine, square, saw, ramp, exp, random: seven different pictures", diff);
                GL(CR_G_LFOWAVE, 64, CR_LW_SAW, 0);
                check("lfowave: the start phase slides the shape", memcmp(wv7[CR_LW_SAW], cv_px, sizeof wv7[0]) != 0);
                {   /* square, one cycle vs two: the vertical edges (columns inked over most of the height) double */
                    uint32_t e1 = 0, e2 = 0, r, xc;
                    GL(CR_G_LFOWAVE, 0, CR_LW_SQUARE, 0);
                    for (xc = 26; xc < 68u; xc++) { COUNT(r, xc, 6u, xc + 1u, 34u, cv_px[y * 240u + x] != bg); e1 += r >= 24u; }
                    GL(CR_G_LFOWAVE, 0, CR_LW_SQUARE, CR_CF_SPAN2);
                    for (xc = 26; xc < 68u; xc++) { COUNT(r, xc, 6u, xc + 1u, 34u, cv_px[y * 240u + x] != bg); e2 += r >= 24u; }
                    snprintf(name, sizeof name, "lfowave: a span-2 cell draws two cycles (%u edge columns, %u with one)", e2, e1);
                    check(name, e2 > e1);
                }
            }
#undef GL
        }
        {   /* FM TONE's pages in the Digitone's ranges (eng_quad.c QUAD_SCREENS, F_FIX2: two decimals): every page whose
             * cells show a hi-res value, each value at its longest text, the glyphs cr_edit.c gives those cells; 0 lint
             * findings (no overlap, nothing off the screen) */
            static const struct {
                const char *slug, *title, *lab[2][4], *val[2][4];
                uint8_t gl[2][4];                 /* 0 a bar, 1 HARM, 2 DETUNE, 3 the knob, 4 text, 5 none */
            } QP[] = {
                {"quad_syn1_longest", "SYN 1", {{"Algo", "Ratio C", "Ratio A", "Ratio B"}, {"Harm", "Dtune", "Feedback", "Mix"}},
                 {{"8", "16.00", "16.00", "16.00"}, {"-26.00", "127.00", "127", "-64"}}, {{5, 5, 5, 5}, {1, 2, 0, 0}}},
                {"quad_filter_longest", "FILTER", {{"Attack", "Decay", "Sustain", "Release"}, {"Freq", "Reso", "Type", "Env depth"}},
                 {{"127", "127", "127", "INF"}, {"126.85", "127.00", "LP24", "-64.00"}}, {{0, 0, 0, 0}, {0, 0, 4, 0}}},
                {"quad_filter2_longest", "FILTER 2", {{"Env delay", "Key track", 0, 0}, {"Base", "Width", 0, 0}},
                 {{"127", "127", 0, 0}, {"127.00", "127", 0, 0}}, {{0, 0, 5, 5}, {0, 0, 5, 5}}},
                {"quad_amp_longest", "AMP", {{"Attack", "Decay", "Sustain", "Release"}, {"Level", "Pan", "Drive", 0}},
                 {{"127", "INF", "127", "126"}, {"127", "-64", "127", 0}}, {{0, 0, 0, 0}, {0, 0, 0, 5}}},
                {"quad_lfo_longest", "LFO 1", {{"Speed", "Mult", "Fade", "Dest"}, {"Wave", "Phase", "Trig", "Depth"}},
                 {{"-64.00", "F2k", "-64", "ABLEV"}, {"RAMP", "127", "HALF", "-64.00"}}, {{3, 4, 4, 4}, {4, 0, 4, 0}}},
            };
            uint32_t q, r, c, n0 = nfind;
            for (q = 0; q < sizeof QP / sizeof QP[0]; q++) {
                cr_screen_t s;
                cr_screen_clear(&s);
                s.kind = CR_K_EDIT8;
                s.n_rows = 2;
                s.header = 1;
                snprintf(s.title, sizeof s.title, "%s", QP[q].title);
                for (r = 0; r < 2u; r++)
                    for (c = 0; c < 4u; c++) {
                        cr_cell_t *cl = &s.cell[r][c];
                        if (!QP[q].lab[r][c])
                            continue;
                        cl->flags = CR_CF_ON;
                        snprintf(cl->label, sizeof cl->label, "%s", QP[q].lab[r][c]);
                        snprintf(cl->value, sizeof cl->value, "%s", QP[q].val[r][c]);
                        cl->pct = 250;
                        switch (QP[q].gl[r][c]) {
                        case 0: cl->glyph = CR_G_BAR; cl->flags |= CR_CF_PCT | CR_CF_BIP; break;
                        case 1: cl->glyph = CR_G_HARM; cl->flags |= CR_CF_PCT; break;
                        case 2: cl->glyph = CR_G_DETUNE; cl->flags |= CR_CF_PCT; break;
                        case 3: cl->glyph = CR_G_KNOB; cl->flags |= CR_CF_PCT | CR_CF_BIPOLAR; break;
                        default: break;
                        }
                    }
                s.hot_r = 2, s.hot_c = 0;            /* (the hot cell, its block: a hi-res one) */
                render(&s, 0);
                write_ppm(dir, QP[q].slug);
                lint(QP[q].slug, &s);
            }
            snprintf(name, sizeof name, "FM TONE pages at their longest two-decimal values: %u lint findings", nfind - n0);
            check(name, nfind == n0);
        }
        {   /* cells: big (no bar, a larger value), span 2 (two columns, the cells after it shift), the bipolar knob */
            cr_screen_t s;
            int32_t h0 = 0, h1 = 0, xt = -1, xd = -1;
            uint32_t bar0, bar1, gl, gr, run, lb, rb, lb2, rb2;
            cr_screen_clear(&s);
            s.kind = CR_K_EDIT8;
            s.n_rows = 1;
            snprintf(s.title, sizeof s.title, "QUAD TEST");
            s.cell[0][0].flags = CR_CF_ON | CR_CF_PCT;
            s.cell[0][0].pct = 128;
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Algo");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "3");
            render(&s, 0);
            for (k = 0; k < nbox; k++) if (!strcmp(boxes[k].s, "3")) h0 = boxes[k].y1 - boxes[k].y0;
            COUNT(bar0, 8u, 30u + 26u + 17u - 3u, 52u, 30u + 26u + 17u + 3u, HS(x, y) == line || HS(x, y) == blue);
            s.cell[0][0].flags |= CR_CF_BIG;
            render(&s, 0);
            write_ppm(dir, "quad_big_cell");
            for (k = 0; k < nbox; k++) if (!strcmp(boxes[k].s, "3")) h1 = boxes[k].y1 - boxes[k].y0;
            COUNT(bar1, 8u, 30u + 26u + 17u - 3u, 52u, 30u + 26u + 17u + 3u, HS(x, y) == line);
            snprintf(name, sizeof name, "big cell: no bar (%u px, %u without big), a larger value (%d px tall, %d)", bar1, bar0, h1, h0);
            check(name, bar0 > 60u && bar1 == 0u && h1 > h0 + 3);
            memset(s.cell, 0, sizeof s.cell);
            s.cell[0][0].flags = CR_CF_ON | CR_CF_SPAN2 | CR_CF_PCT;
            s.cell[0][0].glyph = CR_G_LFOWAVE;
            s.cell[0][0].wave = CR_LW_TRI;
            s.cell[0][0].pct = 64;
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Wave \267 Phase");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "Tri \267 90");
            s.cell[0][1].flags = CR_CF_ON;
            snprintf(s.cell[0][1].label, sizeof s.cell[0][1].label, "Trig");
            snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "Free");
            s.cell[0][2].flags = CR_CF_ON | CR_CF_PCT | CR_CF_BIP;
            s.cell[0][2].pct = 188;
            snprintf(s.cell[0][2].label, sizeof s.cell[0][2].label, "Depth");
            snprintf(s.cell[0][2].value, sizeof s.cell[0][2].value, "+30");
            render(&s, 0);
            write_ppm(dir, "quad_span_cell");
            for (k = 0; k < nbox; k++) {
                if (!strcmp(boxes[k].s, "Trig")) xt = boxes[k].x0;
                if (!strcmp(boxes[k].s, "Depth")) xd = boxes[k].x0;
            }
            COUNT(gl, 6u, 56u, 58u, 90u, HS(x, y) == blue);
            COUNT(gr, 62u, 56u, 114u, 90u, HS(x, y) == blue);
            COUNT(run, 4u, 128u, 116u, 130u, HS(x, y) == blue);
            snprintf(name, sizeof name, "span cell: the wave over both columns (%u, %u px), its bar across (%u px), Trig at %d, Depth at %d",
                     gl, gr, run, xt, xd);
            check(name, gl > 40u && gr > 40u && run >= 220u && xt >= 120 && xt < 180 && xd >= 180);
            memset(s.cell, 0, sizeof s.cell);
            s.cell[0][0].flags = CR_CF_ON | CR_CF_PCT | CR_CF_BIPOLAR;
            s.cell[0][0].glyph = CR_G_KNOB;
            s.cell[0][0].pct = 179;                              /* +0.2: the arc right of 12 o'clock */
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Speed");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "+24");
            render(&s, 0);
            write_ppm(dir, "quad_bipolar_knob");
            COUNT(lb, 5u, 56u, 30u, 90u, HS(x, y) == blue);
            COUNT(rb, 30u, 56u, 55u, 90u, HS(x, y) == blue);
            s.cell[0][0].pct = 77;                               /* -0.2: left of it */
            render(&s, 0);
            COUNT(lb2, 5u, 56u, 30u, 90u, HS(x, y) == blue);
            COUNT(rb2, 30u, 56u, 55u, 90u, HS(x, y) == blue);
            snprintf(name, sizeof name, "bipolar knob: the arc from 12 o'clock flips across 0.5 (left / right %u / %u, then %u / %u)",
                     lb, rb, lb2, rb2);
            check(name, rb > lb + 10u && lb2 > rb2 + 10u);
        }
#undef COUNT
#undef HS
    }

    {   /* the looper's PLAY menu (CR_K_LOOPMENU, design/choralroot-fm1-looper-mockups.json): the slot strip's selected
         * cell red and taller; the playhead where lm_pos puts it, its move composing only the timeline's strips; a
         * struck mark in Replace's span; the step cursor's blink composing only its row's strip */
        int32_t it = -1, ir = -1, is = -1, ix = -1;
        uint32_t x, y, k, n0, n1, n2;
        uint16_t red = swap16(CR_RED), dim = swap16(T_DIM);
#define HS(x_, y_) host_screen[(uint32_t)(y_) * 240u + (uint32_t)(x_)]
#define COUNT(n_, x0_, y0_, x1_, y1_, cond_) do { n_ = 0; for (y = (y0_); y < (uint32_t)(y1_); y++) \
            for (x = (x0_); x < (uint32_t)(x1_); x++) n_ += (cond_); } while (0)
        for (k = 0; k < CR_NSCREENS; k++) {
            const cr_screen_t *q = &CR_SCREENS[k];
            if (q->kind != CR_K_LOOPMENU) continue;
            if (ix < 0 && q->lm_mid == CR_LM_TEXT && !q->lm_jump && q->lm_slot == 2u) ix = (int32_t)k;
            if (it < 0 && q->lm_mid == CR_LM_TIMELINE && !q->lm_free && !q->lm_erase1) it = (int32_t)k;
            if (ir < 0 && q->lm_mid == CR_LM_TIMELINE && q->lm_erase1) ir = (int32_t)k;
            if (is < 0 && q->lm_mid == CR_LM_STEPS) is = (int32_t)k;
        }
        check("loop menu: the idle, playing, Replace and Step states are in the table", ix >= 0 && it >= 0 && ir >= 0 && is >= 0);
        if (ix >= 0) {   /* slot 3 (x 48..68): red from row 29; slot 2 and 4: none red, nothing above row 34 */
            cr_screen_t s = CR_SCREENS[ix];
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, 49u, 29u, 67u, 50u, HS(x, y) == red);
            COUNT(n1, 49u, 29u, 67u, 33u, HS(x, y) == red);
            COUNT(n2, 6u, 28u, 47u, 56u, HS(x, y) == red);
            snprintf(name, sizeof name, "loop menu: the selected slot red and 5 px taller (%u red px, %u above the others; %u left of it)",
                     n0, n1, n2);
            check(name, n0 > 200u && n1 > 50u && n2 == 0u);
            s.lm_slot = 4;                       /* slot 5, empty: a red outline only */
            render(&s, 0);
            COUNT(n0, 92u, 29u, 110u, 50u, HS(x, y) == red);
            COUNT(n1, 96u, 34u, 106u, 46u, HS(x, y) == red);
            check("loop menu: an empty selected slot is a red outline (no fill)", n0 > 50u && n1 < n0 / 4u);
        }
        if (it >= 0) {   /* the playhead: a red column at x 20 + pos * 50 (4 bars), moving composes the timeline only */
            cr_screen_t s = CR_SCREENS[it];
            int32_t sy = cr_lm_tl_y(&s), px = 20 + (int32_t)s.lm_pos * (200 / (int32_t)s.lm_bars) / 256;
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, (uint32_t)px - 1u, (uint32_t)sy - 2u, (uint32_t)px + 1u, (uint32_t)sy + 42u, HS(x, y) == red);
            s.lm_pos = (uint16_t)(s.lm_pos + 64u);                    /* a quarter bar on */
            cr_draw(&s, 0);
            COUNT(n1, (uint32_t)px - 1u, (uint32_t)sy - 2u, (uint32_t)px + 1u, (uint32_t)sy + 42u, HS(x, y) == red);
            COUNT(n2, (uint32_t)px + 11u, (uint32_t)sy - 2u, (uint32_t)px + 14u, (uint32_t)sy + 42u, HS(x, y) == red);
            snprintf(name, sizeof name, "loop menu: the playhead at x %d (%u red px), a quarter bar on at %d (%u; %u left)",
                     px, n0, px + 12, n2, n1);
            check(name, n0 >= 40u && n2 >= 40u && n1 < 30u);
            snprintf(name, sizeof name, "loop menu: the playhead moves: only the timeline's strips composed (%u)", cr_dc.drawn);
            check(name, cr_dc.drawn >= 1u && cr_dc.drawn <= 2u);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("loop menu: the playhead's partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
        }
        if (ir >= 0) {   /* a mark at 2.25 bars (x 132) inside the span 2.1 .. 2.4: grey, a red x over it */
            cr_screen_t s = CR_SCREENS[ir];
            int32_t sy = cr_lm_tl_y(&s), mx = 20 + 225 * 50 / 100;
            s.anim = 0;
            render(&s, 0);
            write_ppm(dir, "loopmenu_struck");
            COUNT(n0, (uint32_t)mx - 1u, (uint32_t)sy + 5u, (uint32_t)mx + 2u, (uint32_t)sy + 35u, HS(x, y) == dim);
            COUNT(n1, (uint32_t)mx - 5u, (uint32_t)sy + 5u, (uint32_t)mx + 6u, (uint32_t)sy + 35u, HS(x, y) == red);
            s.lm_erase0 = s.lm_erase1 = 0;
            render(&s, 0);
            COUNT(n2, (uint32_t)mx - 1u, (uint32_t)sy + 5u, (uint32_t)mx + 2u, (uint32_t)sy + 35u, HS(x, y) == dim);
            snprintf(name, sizeof name, "loop menu: a struck mark: grey (%u px, %u unstruck) under a red x (%u red px)", n0, n2, n1);
            check(name, n0 >= 6u && n2 == 0u && n1 >= 10u);
        }
        if (is >= 0) {   /* the cursor's outline (red around a grey step); its blink: its row's strip only */
            cr_screen_t s = CR_SCREENS[is];
            int32_t gy = cr_lm_st_y(&s), per = cr_lm_per(&s), c = (int32_t)(s.lm_pos & 63u) % per;
            int32_t cx0 = cr_lm_st_x(per, c) / 16, cy0 = gy + (int32_t)(s.lm_pos & 63u) / per * 9;
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, (uint32_t)cx0 - 3u, (uint32_t)cy0 - 3u, (uint32_t)cx0 + 14u, (uint32_t)cy0 + 10u, HS(x, y) == red);
            s.lm_pos |= CR_LM_CUR_OFF;
            cr_draw(&s, 0);
            COUNT(n1, (uint32_t)cx0 - 3u, (uint32_t)cy0 - 3u, (uint32_t)cx0 + 14u, (uint32_t)cy0 + 10u, HS(x, y) == red);
            snprintf(name, sizeof name, "loop menu: the step cursor blinks (%u red px lit, %u off), only its strip composed (%u)",
                     n0, n1, cr_dc.drawn);
            check(name, n0 >= 30u && n1 + 20u < n0 && cr_dc.drawn == 1u && cr_dc.blits == 1u);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("loop menu: the blink's partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
            s.lm_pos = (uint16_t)((s.lm_pos & 63u) + 16u);            /* the cursor a bar down: both rows' strips */
            cr_draw(&s, 0);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("loop menu: the cursor moving rows: the partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
        }
        /* 0.18. The mark of a loop not saved yet (lm_dirty: the slot + 1): a 3 px square in its cell's top right
         * corner, in the number's colour: the background's on a filled cell, the cell's on an outlined one */
        if (ix >= 0) {
            cr_screen_t s = CR_SCREENS[ix];
            uint16_t bg = swap16(T_BG);
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, 62u, 31u, 72u, 37u, HS(x, y) == bg);          /* slot 3 (selected, filled): its top right */
            memcpy(a, host_screen, sizeof a);
            s.lm_dirty = 3;
            cr_draw(&s, 0);
            COUNT(n1, 62u, 31u, 72u, 37u, HS(x, y) == bg);
            snprintf(name, sizeof name, "loop menu: a loop not saved yet: a mark in its slot's corner (%u -> %u background px there), "
                     "the strip redrawn (%u)", n0, n1, cr_dc.drawn);
            check(name, n1 == n0 + 9u && cr_dc.drawn >= 1u && memcmp(a, host_screen, sizeof a) != 0);
            write_ppm(dir, "loopmenu_dirty");
            s.lm_dirty = 5;                      /* slot 5 (not selected) */
            s.lm_used &= (uint16_t)~(1u << 4);
            render(&s, 0);
            COUNT(n0, 106u, 35u, 120u, 42u, HS(x, y) == dim);
            s.lm_dirty = 0;
            render(&s, 0);
            COUNT(n1, 106u, 35u, 120u, 42u, HS(x, y) == dim);
            check("loop menu: the mark on an empty, unselected slot (a cleared loop not saved): dim, as its number", n0 >= n1 + 6u);
        }
        /* the PLAY menu's two pages and the loop editor (cr_ui.c cu_loop_cells, cu_ledit_cells, cu_loopmenu): the
         * bottom line, the cells; the editor's grid ends where the loop ends (lm_steps) */
        if (ix >= 0 && is >= 0) {
            static const char *const LB1[4] = {"Length", "Rate", "Quantize", "Count-in"};
            static const char *const V1[4] = {"4 bars", "x1.5", "1/16", "On"};
            static const char *const LBE[4] = {"Note len", "Steps", "Chance", "Velocity"};
            static const char *const VE[4] = {"3", "16", "100%", "100"};
            static const char *const LBT[4] = {"Ratchet", "Condition", "Voicing", "Transpose"};
            static const char *const VT[4] = {"x3", "not 1st", "Follow", "C#4"};
            cr_screen_t s = CR_SCREENS[ix];
            uint16_t line = swap16(T_LINE);
            uint32_t c;
            s.anim = 0;
            memset(s.cell, 0, sizeof s.cell);
            for (c = 0; c < 4u; c++) {
                s.cell[0][c].flags = CR_CF_ON;
                snprintf(s.cell[0][c].label, sizeof s.cell[0][c].label, "%s", LB1[c]);
                snprintf(s.cell[0][c].value, sizeof s.cell[0][c].value, "%s", V1[c]);
            }
            s.cell[0][2].glyph = CR_G_ECHOES; s.cell[0][2].pct = 170; s.cell[0][2].pct2 = 255;
            s.cell[0][3].glyph = CR_G_GATE; s.cell[0][3].pct = 255;
            snprintf(s.foot, sizeof s.foot, "1/2 \267 keys: play \267 GLO: loops");
            render(&s, 0);
            COUNT(n0, 20u, 228u, 220u, 240u, HS(x, y) != swap16(T_BG));
            write_ppm(dir, "loopmenu_page1");
            check("loop menu, page 1: Length / Rate / Quantize / Count-in over the bottom line (the page, the keys, GLO)", n0 > 100u);
            memset(s.cell, 0, sizeof s.cell);
            s.cell[0][0].flags = CR_CF_ON;
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Level");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "100");
            s.cell[0][0].glyph = CR_G_BAR; s.cell[0][0].pct = 255;
            s.cell[0][1].flags = CR_CF_ON;
            snprintf(s.cell[0][1].label, sizeof s.cell[0][1].label, "Metronome");
            snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "Off");
            s.cell[0][1].glyph = CR_G_GATE; s.cell[0][1].pct = 26;
            snprintf(s.foot, sizeof s.foot, "2/2 \267 keys: play \267 GLO: loops");
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            COUNT(n1, 122u, 170u, 240u, 222u, HS(x, y) == swap16(T_TEXT));
            COUNT(n0, 62u, 170u, 120u, 222u, HS(x, y) != swap16(T_BG));
            write_ppm(dir, "loopmenu_page2");
            {                                    /* the new label whole, in its cell (KNOB 2's: x 60 .. 120) */
                uint32_t i, whole = 0;
                for (i = 0; i < nbox; i++)
                    if (!strcmp(boxes[i].s, "Metronome") && !(boxes[i].flags & 1u) && boxes[i].x0 >= 60 && boxes[i].x1 <= 120)
                        whole++;
                check("loop menu, page 2: Level and Metronome (the label whole, inside its cell), the other two cells empty",
                      memcmp(a, host_screen, sizeof a) != 0 && n1 < 40u && n0 > 60u && whole == 1u);
            }
            snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "On");
            s.cell[0][1].pct = 255;
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            write_ppm(dir, "loopmenu_page2_metro");
            check("loop menu, page 2: Metronome On: its cell changes", memcmp(a, host_screen, sizeof a) != 0);
            snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "Off");
            s.cell[0][1].pct = 26;
            s = CR_SCREENS[is];                  /* the editor, a step sequencer: a 16-step pattern, sixteenths: chords
                                                  * on steps 1 2 5 9 13, step 2's note 3 steps long (it sounds on
                                                  * into 3 and 4), step 9's 2 (into 10) */
            s.anim = 0;
            s.n_items = 2; s.item0 = 0; s.sel = 0;   /* (the editor's two pages where the record mode is) */
            snprintf(s.item[0], sizeof s.item[0], "Step");
            snprintf(s.item[1], sizeof s.item[1], "Trig");
            s.lm_bars = 1; s.lm_beats = 16; s.lm_lane[0] = 0x1113ull; s.lm_lane[1] = 0x20Cull; s.lm_lane[2] = 0xCull;
            s.lm_lane[3] = 0; s.lm_pos = 1; s.lm_steps = 16; s.lm_dirty = (uint8_t)(s.lm_slot + 1u);
            s.lm_bar = 4; s.lm_ed = 1;           /* (the bar begins on the selected step: its whole cell) */
            snprintf(s.sub, sizeof s.sub, "Am");
            snprintf(s.value, sizeof s.value, "step 2/16");
            snprintf(s.foot, sizeof s.foot, "1/2 \267 PRESETS: step \267 REC: clear \267 SEQ: back");
            s.right[0] = 0;
            memset(s.cell, 0, sizeof s.cell);
            for (c = 0; c < 4u; c++) {
                s.cell[0][c].flags = CR_CF_ON;
                snprintf(s.cell[0][c].label, sizeof s.cell[0][c].label, "%s", LBE[c]);
                snprintf(s.cell[0][c].value, sizeof s.cell[0][c].value, "%s", VE[c]);
            }
            s.cell[0][3].glyph = CR_G_BAR; s.cell[0][3].pct = 200;
            s.cell[0][2].glyph = CR_G_BAR; s.cell[0][2].pct = 255;
            render(&s, 0);
            {
                int32_t gy = cr_lm_st_y(&s), x1 = cr_lm_st_x(16, 1) / 16, x4 = cr_lm_st_x(16, 4) / 16, x8 = cr_lm_st_x(16, 8) / 16;
                uint16_t tail = swap16(ux_mix(CR_RED, T_BG, 62)), white = swap16(T_TEXT), mid = swap16(T_MID);
                uint32_t n3, n4, n5, n6;
                COUNT(n0, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == red);
                COUNT(n1, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == line);
                COUNT(n3, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == tail);
                COUNT(n4, (uint32_t)x1, (uint32_t)gy + 5u, (uint32_t)x4, (uint32_t)gy + 7u, HS(x, y) == white);
                COUNT(n5, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == white);
                write_ppm(dir, "loopmenu_editor");
                snprintf(name, sizeof name, "loop editor: 16 steps: 5 filled (%u red px), 3 a longer note sounds in (%u dark red), 8 "
                         "empty (%u grey); the selected step's note: a white bar along steps 2-4 (%u px, %u in the row)", n0, n3, n1, n4, n5);
                check(name, n0 > 230u && n3 > 100u && n1 > 400u && n4 >= 48u && n5 == n4);
                {   /* the selected step's white bar; the velocity's colour; the chance / condition strip; the
                     * ratchet's ticks; the cursor's dim phase; the step's voicing after its chord; then the editor's
                     * second page (Trig) */
                    cr_screen_t t2 = s, e = s;
                    uint32_t r1, c2, w[6], ok = 1, lu[16], k, q, m1, m2;
                    int32_t x5 = cr_lm_st_x(16, 4) / 16, x2 = cr_lm_st_x(16, 2) / 16, x3 = cr_lm_st_x(16, 3) / 16;
                    uint16_t yel = swap16(CR_YELLOW), blu = swap16(CR_NAMED[CR_COL_BLUE]), bg = swap16(T_BG);
                    static const uint8_t PC[8] = {100, 90, 75, 60, 50, 40, 25, 10}, VL[6] = {20, 50, 80, 100, 115, 127};
                    /* the bar: 1/4, 1/2, 3/4 of the selected cell, the whole cell, two cells; nowhere else */
                    e.lm_lane[2] = 0;
                    for (q = 1; q <= 4u; q++) {
                        e.lm_bar = (uint8_t)q;
                        render(&e, 0);
                        COUNT(w[q], (uint32_t)x1, (uint32_t)gy + 5u, (uint32_t)x2, (uint32_t)gy + 7u, HS(x, y) == white);
                        COUNT(r1, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == white);
                        ok &= r1 == w[q];
                        if (q == 2u) write_ppm(dir, "loopmenu_editor_half");
                    }
                    e.lm_lane[2] = 4ull;
                    render(&e, 0);
                    COUNT(w[5], (uint32_t)x2, (uint32_t)gy + 5u, (uint32_t)x3, (uint32_t)gy + 7u, HS(x, y) == white);
                    COUNT(r1, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == white);
                    e.lm_lane[2] = 0; e.lm_bar = 0;
                    render(&e, 0);
                    COUNT(w[0], 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == white);
                    snprintf(name, sizeof name, "loop editor: the bar on the selected step: 1/4 1/2 3/4 1 of its cell (%u %u %u %u white px), "
                             "2 steps: the next cell too (%u px, %u in the row); an empty step: none (%u)", w[1], w[2], w[3], w[4], w[5], r1, w[0]);
                    check(name, ok && w[1] >= 4u && w[1] < w[2] && w[2] < w[3] && w[3] < w[4] && w[4] >= 18u && w[5] >= 18u &&
                                r1 == w[4] + w[5] && w[0] == 0u);
                    /* the cursor's blink in the editor: bright, then dim: never gone; the partial draw = a full one */
                    e = s;
                    e.lm_pos |= CR_LM_CUR_OFF;
                    render(&s, 0);
                    cr_draw(&e, 0);
                    COUNT(r1, (uint32_t)x1 - 3u, (uint32_t)gy - 3u, (uint32_t)x1 + 14u, (uint32_t)gy + 10u, HS(x, y) == swap16(ux_mix(CR_RED, T_BG, 50)));
                    COUNT(c2, (uint32_t)x1, (uint32_t)gy + 5u, (uint32_t)x2, (uint32_t)gy + 7u, HS(x, y) == white);
                    memcpy(a, host_screen, sizeof a);
                    render(&e, 0);
                    write_ppm(dir, "loopmenu_editor_blink");
                    snprintf(name, sizeof name, "loop editor: the cursor's other blink phase: a dim outline (%u px) and the bar (%u px) stay; "
                             "partial = full", r1, c2);
                    check(name, r1 >= 30u && c2 >= 18u && !memcmp(a, host_screen, sizeof a));
                    /* the velocity's colour (step 5): 100 and "none given" the role red; brighter with the velocity */
                    e = s;
                    for (k = 0; k < 6u; k++) {
                        uint32_t px;
                        e.lm_cell[4] = (uint8_t)((VL[k] + 12u) / 9u);
                        render(&e, 0);
                        px = swap16(HS(x5 + 4, gy + 3));
                        lu[k] = (px >> 11) * 54u * 2u + ((px >> 5) & 63u) * 183u + (px & 31u) * 19u * 2u;
                        if (VL[k] == 100u) ok &= HS(x5 + 4, gy + 3) == red;
                        if (k) ok &= lu[k] > lu[k - 1u];
                    }
                    e.lm_cell[4] = 1;
                    render(&e, 0);
                    ok &= HS(x5 + 4, gy + 3) == swap16(CR_LM_SOFT) && HS(x5 + 4, gy + 3) != tail && HS(x5 + 4, gy + 3) != line;
                    snprintf(name, sizeof name, "loop editor: the velocity colours a step: 20 50 80 100 115 127 brighter each (%u %u %u %u %u %u), "
                             "100 the role red, the softest a plum (not the tail's dark red)", lu[0], lu[1], lu[2], lu[3], lu[4], lu[5]);
                    check(name, ok);
                    /* the strip (step 5): none at 100 %; yellow, dimmer with the chance, the cell's 7 rows; blue with
                     * a condition */
                    ok = 1;
                    for (k = 1; k < 8u; k++) {
                        uint16_t want = swap16(ux_mix(T_BG, CR_YELLOW, PC[k]));
                        uint32_t px;
                        e.lm_cell[4] = (uint8_t)(12u | k << 4);
                        render(&e, 0);
                        COUNT(r1, (uint32_t)x5, (uint32_t)gy, (uint32_t)x5 + 11u, (uint32_t)gy + 7u, HS(x, y) == want);
                        ok &= r1 == 21u;
                        for (q = 0; q < 7u; q++) ok &= HS(x5 + 8, gy + (int32_t)q) == want;   /* (its full height) */
                        px = swap16(want);
                        lu[k] = (px >> 11) * 54u * 2u + ((px >> 5) & 63u) * 183u + (px & 31u) * 19u * 2u;
                        if (k > 1u) ok &= lu[k] < lu[k - 1u];
                        ok &= (px >> 11) > (px & 31u) || k == 7u;                            /* (yellow: no blue in it) */
                        if (k == 4u) write_ppm(dir, "loopmenu_editor_marks");
                    }
                    e.lm_cell[4] = (uint8_t)(12u | CR_LMC_COND);
                    render(&e, 0);
                    COUNT(m1, (uint32_t)x5, (uint32_t)gy, (uint32_t)x5 + 11u, (uint32_t)gy + 7u, HS(x, y) == blu);
                    COUNT(r1, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == yel);
                    e.lm_cell[4] = (uint8_t)(12u | CR_LMC_COND | 4u << 4);
                    render(&e, 0);
                    COUNT(m2, (uint32_t)x5, (uint32_t)gy, (uint32_t)x5 + 11u, (uint32_t)gy + 7u, HS(x, y) == swap16(ux_mix(T_BG, CR_NAMED[CR_COL_BLUE], 50)));
                    e.lm_cell[4] = 12;
                    render(&e, 0);
                    COUNT(c2, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == red);
                    snprintf(name, sizeof name, "loop editor: the chance strip: 3 x 7 px, yellow, dimmer at each of 90 .. 10 %%; a condition: "
                             "blue (%u px; %u at 50 %%, dimmer; %u yellow); 100 %% and Always: none (%u red px, %u before)", m1, m2, r1, c2, n0);
                    check(name, ok && m1 == 21u && m2 == 21u && r1 == 0u && c2 == n0);
                    /* the ratchet (step 5): 2, 3, 4 ticks of 1 x 2 px along the top; with a strip beside them */
                    ok = 1;
                    for (k = 1; k <= 3u; k++) {
                        e.lm_lane[3] = (uint64_t)(k & 1u) << 4; e.lm_lane[4] = (uint64_t)(k >> 1) << 4;
                        e.lm_cell[4] = 12;
                        render(&e, 0);
                        COUNT(r1, (uint32_t)x5, (uint32_t)gy, (uint32_t)x5 + 10u, (uint32_t)gy + 7u, HS(x, y) == bg);
                        ok &= r1 == 2u * (k + 1u);
                        for (q = 0; q <= k; q++) ok &= HS(x5 + 1 + 2 * (int32_t)q, gy) == bg && HS(x5 + 1 + 2 * (int32_t)q, gy + 1) == bg;
                        e.lm_cell[4] = (uint8_t)(12u | 2u << 4);
                        render(&e, 0);
                        COUNT(r1, (uint32_t)x5, (uint32_t)gy, (uint32_t)x5 + 10u, (uint32_t)gy + 7u, HS(x, y) == bg);
                        COUNT(m1, (uint32_t)x5, (uint32_t)gy, (uint32_t)x5 + 11u, (uint32_t)gy + 7u, HS(x, y) == swap16(ux_mix(T_BG, CR_YELLOW, 75)));
                        ok &= r1 == 2u * (k + 1u) && m1 >= 19u;
                        if (k == 2u) write_ppm(dir, "loopmenu_editor_ratchet");
                    }
                    check("loop editor: a ratchet: 2 / 3 / 4 ticks (1 x 2 px) along the cell's top, alone and beside the strip (19 px of it or more)", ok);
                    /* the step's voicing, blue, after its chord; Follow: nothing; the longest name and -12 fit */
                    e = s;
                    render(&e, 0);
                    COUNT(r1, 20u, (uint32_t)gy + 9u, 150u, (uint32_t)gy + 30u, HS(x, y) == blu);
                    snprintf(e.sub, sizeof e.sub, "Am\t+3");
                    render(&e, 0);
                    COUNT(c2, 20u, (uint32_t)gy + 9u, 150u, (uint32_t)gy + 30u, HS(x, y) == blu);
                    COUNT(m1, 20u, (uint32_t)gy + 9u, 44u, (uint32_t)gy + 30u, HS(x, y) == blu);
                    write_ppm(dir, "loopmenu_editor_voicing");
                    m2 = nell;
                    snprintf(e.sub, sizeof e.sub, "G#madd4 JAZZ\t-12");
                    snprintf(e.value, sizeof e.value, "step 64/64");
                    render(&e, 0);
                    lint("loopmenu_editor_voicing", &e);
                    snprintf(name, sizeof name, "loop editor: the step's voicing after its chord, blue (%u px, %u over the name, %u on Follow); "
                             "'G#madd4 JAZZ -12' beside 'step 64/64': not cut (%u)", c2, m1, r1, nell - m2);
                    check(name, c2 >= 12u && m1 == 0u && r1 == 0u && nell == m2);
                    render(&s, 0);
                    t2.sel = 1; t2.lm_lane[3] = 0x100ull; t2.lm_cell[4] = (uint8_t)(12u | CR_LMC_COND | 3u << 4);
                    snprintf(t2.foot, sizeof t2.foot, "2/2 \267 PRESETS: step \267 REC: clear \267 SEQ: back");
                    memset(t2.cell, 0, sizeof t2.cell);
                    for (c2 = 0; c2 < 4u; c2++) {
                        t2.cell[0][c2].flags = CR_CF_ON;
                        snprintf(t2.cell[0][c2].label, sizeof t2.cell[0][c2].label, "%s", LBT[c2]);
                        snprintf(t2.cell[0][c2].value, sizeof t2.cell[0][c2].value, "%s", VT[c2]);
                    }
                    memcpy(a, host_screen, sizeof a);
                    render(&t2, 0);
                    COUNT(r1, 20u, 228u, 220u, 240u, HS(x, y) != swap16(T_BG));
                    write_ppm(dir, "loopmenu_editor_trig");
                    check("loop editor, page 2 (Trig): Ratchet / Condition / Voicing / Transpose over its bottom line",
                          memcmp(a, host_screen, sizeof a) != 0 && r1 > 100u);
                    {   /* Transpose (KNOB 4): the label whole inside its cell (x 180 .. 240), the root note under it; hot:
                         * the turn's sum; an empty step: "-", dim. 0 lint findings */
                        uint32_t i, whole = 0, val = 0, f0 = nfind, e0 = nell, hot, dim;
                        lint("loopmenu_editor_trig", &t2);
                        for (i = 0; i < nbox; i++) {
                            if (!strcmp(boxes[i].s, "Transpose") && !(boxes[i].flags & 1u) && boxes[i].x0 >= 180 && boxes[i].x1 <= 240) whole++;
                            if (!strcmp(boxes[i].s, "C#4") && boxes[i].x0 >= 180 && boxes[i].x1 <= 240) val++;
                        }
                        memcpy(a, host_screen, sizeof a);
                        snprintf(t2.cell[0][3].value, sizeof t2.cell[0][3].value, "+1");
                        t2.hot_r = 1; t2.hot_c = 3;
                        render(&t2, 0);
                        lint("loopmenu_editor_trig_hot", &t2);
                        write_ppm(dir, "loopmenu_editor_trig_hot");
                        hot = memcmp(a, host_screen, sizeof a) != 0;
                        t2.hot_r = t2.hot_c = 0;
                        snprintf(t2.cell[0][3].value, sizeof t2.cell[0][3].value, "-");
                        t2.cell[0][3].flags = CR_CF_ON | CR_CF_DIM;
                        memcpy(a, host_screen, sizeof a);
                        render(&t2, 0);
                        lint("loopmenu_editor_trig_empty", &t2);
                        dim = memcmp(a, host_screen, sizeof a) != 0;
                        snprintf(name, sizeof name, "loop editor, Trig: the Transpose cell: its label whole in its cell (%u), the root note C#4 (%u); hot "
                                 "'+1'; an empty step '-'; %u lint findings, %u cut", whole, val, nfind - f0, nell - e0);
                        check(name, whole == 1u && val == 1u && hot && dim && nfind == f0 && nell == e0);
                        snprintf(t2.cell[0][3].value, sizeof t2.cell[0][3].value, "C#4");
                        t2.cell[0][3].flags = CR_CF_ON;
                    }
                    {   /* the playing name (lm_now): blue, at the left of the page's name on the picker band's row; the
                         * page's name does not move; no voicing; the longest name fits whole; on the Trig page it takes
                         * the left neighbour's place; a new name redraws one strip */
                        static const char *const LONG[4] = {"G#madd4 JAZZ", "G#madd4 WTF", "A#madd4 JAZZ", "D#sus JAZZ"};
                        cr_screen_t p = s, p2;
                        uint32_t i, f0 = nfind, e0 = nell, nb, nz, nout, nx1 = 0, sx0 = 240, okl = 1, in, lblue = 0, lmin = 9999, nostep = 1, hasstep = 0, d1, b1, same;
                        int32_t x, y;
                        render(&p, 0);                               /* stopped: none */
                        memcpy(a, host_screen, sizeof a);
                        COUNT(nz, 0u, 56u, 240u, 92u, HS(x, y) == blu);
                        snprintf(p.lm_now, sizeof p.lm_now, "Am");
                        render(&p, 0);
                        lint("loopmenu_editor_now", &p);
                        write_ppm(dir, "loopmenu_editor_now");
                        COUNT(nb, 6u, 62u, 84u, 80u, HS(x, y) == blu);
                        for (nout = 0, y = 0; y < 240; y++)          /* nothing else changed: only x 6 .. 83, rows 62 .. 79 */
                            for (x = 0; x < 240; x++)
                                if (!(x >= 6 && x < 84 && y >= 62 && y < 80) && a[y * 240 + x] != host_screen[y * 240 + x]) nout++;
                        for (i = 0; i < nbox; i++) {
                            if (!strcmp(boxes[i].s, "Am") && boxes[i].y0 < 92 && boxes[i].y1 > 56) nx1 = (uint32_t)boxes[i].x1;
                            if (!strcmp(boxes[i].s, "Step") && boxes[i].y0 < 92) sx0 = (uint32_t)boxes[i].x0;
                        }
                        snprintf(name, sizeof name, "loop editor, playing: the playing name left of the page's name, blue (%u px; %u when stopped); "
                                 "nothing else moves (%u px differ outside its box); 'Am' ends at x %u, 'Step' begins at %u", nb, nz, nout, nx1, sx0);
                        check(name, nb >= 20u && nz == 0u && nout == 0u && nx1 > 6u && nx1 <= 84u && sx0 >= 90u);
                        for (i = 0; i < 4u; i++) {                   /* the longest names: whole, 6 px or more before "Step" */
                            uint32_t j, x1 = 0;
                            snprintf(p.lm_now, sizeof p.lm_now, "%s", LONG[i]);
                            render(&p, 0);
                            lint("loopmenu_editor_now_long", &p);
                            if (!i) write_ppm(dir, "loopmenu_editor_now_long");
                            for (j = 0; j < nbox; j++)
                                if (!strcmp(boxes[j].s, LONG[i])) x1 = (uint32_t)boxes[j].x1;
                            COUNT(in, 6u, 62u, 84u, 80u, HS(x, y) == blu);
                            okl &= x1 > 40u && x1 <= 84u && in >= 40u;
                            if (!i) lblue = x1;
                            if (in < lmin) lmin = in;
                        }
                        snprintf(name, sizeof name, "loop editor, playing: 'G#madd4 JAZZ' (the longest) and three more: whole, not cut (%u), ending by x 84 "
                                 "(%u): 6 px clear of 'Step'; %u blue px or more; %u lint findings", nell - e0, lblue, lmin, nfind - f0);
                        check(name, okl && nell == e0 && nfind == f0);
                        {   /* the major triad's "M" (2026-10-12): the widest, G#M: the playing name at its biggest face
                             * (as tall as "G#m"), and the name line "G#M -12" beside "step 64/64": whole */
                            cr_screen_t pm = s;
                            uint32_t j, hM = 0, hm = 0, cutM = 0, line = 0;
                            snprintf(pm.lm_now, sizeof pm.lm_now, "G#m");
                            render(&pm, 0);
                            for (j = 0; j < nbox; j++)
                                if (!strcmp(boxes[j].s, "G#m") && boxes[j].x1 <= 84) hm = (uint32_t)(boxes[j].y1 - boxes[j].y0);
                            snprintf(pm.lm_now, sizeof pm.lm_now, "G#M");
                            snprintf(pm.sub, sizeof pm.sub, "G#M\t-12");
                            snprintf(pm.value, sizeof pm.value, "step 64/64");
                            render(&pm, 0);
                            lint("loopmenu_editor_major", &pm);
                            write_ppm(dir, "loopmenu_editor_major");
                            for (j = 0; j < nbox; j++) {
                                if (strncmp(boxes[j].s, "G#M", 3)) continue;
                                cutM |= boxes[j].flags & 1u;
                                if (boxes[j].x1 <= 84 && boxes[j].y1 < 92) hM = (uint32_t)(boxes[j].y1 - boxes[j].y0);
                                else line++;
                            }
                            snprintf(name, sizeof name, "loop editor: a major triad 'G#M': the playing name at the biggest face (%u px tall, "
                                     "'G#m' %u), the name line beside 'step 64/64' (%u): whole", hM, hm, line);
                            check(name, hM > 0u && hM == hm && line >= 1u && !cutM && nell == e0 && nfind == f0);
                        }
                        p2 = t2;                                     /* the Trig page: the name where "Step" (the left neighbour) stood */
                        render(&p2, 0);
                        for (i = 0; i < nbox; i++)
                            if (!strcmp(boxes[i].s, "Step")) hasstep++;
                        snprintf(p2.lm_now, sizeof p2.lm_now, "C#m7 9");
                        render(&p2, 0);
                        lint("loopmenu_editor_now_trig", &p2);
                        write_ppm(dir, "loopmenu_editor_now_trig");
                        for (i = 0; i < nbox; i++)
                            if (!strcmp(boxes[i].s, "Step")) nostep = 0;
                        COUNT(nb, 6u, 62u, 84u, 80u, HS(x, y) == blu);
                        snprintf(name, sizeof name, "loop editor, playing, the Trig page: the name (%u blue px) in the left neighbour's place: 'Step' drawn "
                                 "when stopped (%u), not while a name shows; %u lint findings", nb, hasstep, nfind - f0);
                        check(name, nb >= 40u && hasstep == 1u && nostep && nfind == f0);
                        snprintf(p.lm_now, sizeof p.lm_now, "Am");   /* a new name: one strip composed, = a full draw */
                        render(&p, 0);
                        snprintf(p.lm_now, sizeof p.lm_now, "Dm7");
                        cr_draw(&p, 0);
                        d1 = cr_dc.drawn; b1 = cr_dc.blits;
                        memcpy(a, host_screen, sizeof a);
                        render(&p, 0);
                        same = !memcmp(a, host_screen, sizeof a);
                        cr_draw(&p, 0);
                        p.lm_now[0] = 0;                             /* stopped: it goes, one strip again */
                        cr_draw(&p, 0);
                        snprintf(name, sizeof name, "loop editor, playing: another name redraws one strip (%u composed, %u blitted) and equals a full "
                                 "draw; the name gone (stopped): one strip (%u)", d1, b1, cr_dc.drawn);
                        check(name, d1 == 1u && b1 == 1u && same && cr_dc.drawn == 1u);
                        p = s;                                       /* a page sliding in crosses its place: not drawn meanwhile */
                        snprintf(p.lm_now, sizeof p.lm_now, "Am");
                        p.anim = CR_A_SLIDE; p.slide = 1; p.sel = 1;
                        render(&p, 60);
                        COUNT(nb, 0u, 56u, 240u, 92u, HS(x, y) == blu);
                        render(&p, 400);
                        COUNT(in, 6u, 62u, 84u, 80u, HS(x, y) == blu);
                        snprintf(name, sizeof name, "loop editor, playing: while the page's name slides (160 ms) the playing name waits (%u blue px), "
                                 "then it is back (%u)", nb, in);
                        check(name, nb == 0u && in >= 20u);
                    }
                    render(&s, 0);
                }
                s.lm_steps = 12;                 /* the loop four steps shorter: steps 13 .. 16 are not drawn */
                render(&s, 0);
                COUNT(n2, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == red);
                COUNT(n6, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == line);
                snprintf(name, sizeof name, "loop editor: the grid is the loop's steps: 12 of them: a filled step and 3 empty ones "
                         "fewer (%u red, %u grey)", n2, n6);
                check(name, n2 + 40u < n0 && n6 + 120u < n1);
                s.lm_steps = 16;
                /* the playhead: the step being played white (filled) or light grey (empty); it alone moving is a
                 * partial draw, equal to a full one */
                s.lm_pos = (uint16_t)(1u | CR_LM_PLAY(8));
                render(&s, 0);
                COUNT(n2, (uint32_t)x8, (uint32_t)gy, (uint32_t)x8 + 9u, (uint32_t)gy + 7u, HS(x, y) == white);
                write_ppm(dir, "loopmenu_editor_play");
                s.lm_pos = (uint16_t)(1u | CR_LM_PLAY(10));
                cr_draw(&s, 0);
                COUNT(n3, (uint32_t)x8, (uint32_t)gy, (uint32_t)x8 + 9u, (uint32_t)gy + 7u, HS(x, y) == white);
                COUNT(n4, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 5u, HS(x, y) == mid);
                snprintf(name, sizeof name, "loop editor: the playhead: step 9 (filled) white while it plays (%u px, %u after), "
                         "step 11 (empty) light grey (%u px); %u strip(s) composed", n2, n3, n4, cr_dc.drawn);
                check(name, n2 > 40u && n3 == 0u && n4 > 40u && cr_dc.drawn >= 1u && cr_dc.drawn <= 2u);
                memcpy(a, host_screen, sizeof a);
                render(&s, 0);
                check("loop editor: the playhead's partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
                /* 64 steps: four rows of 16; the playhead from row to row */
                s.lm_bars = 4; s.lm_steps = 64; s.lm_lane[0] = 0x1113111311131113ull; s.lm_lane[1] = 0x020C020C020C020Cull;
                s.lm_lane[2] = 0xFFFFCull;       /* (step 2's note 20 steps long: into the second row) */
                snprintf(s.value, sizeof s.value, "step 2/64");
                s.lm_pos = (uint16_t)(1u | CR_LM_PLAY(15));
                render(&s, 0);
                gy = cr_lm_st_y(&s);
                COUNT(n0, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 34u, HS(x, y) == red);
                COUNT(n5, 20u, (uint32_t)gy + 9u, 220u, (uint32_t)gy + 16u, HS(x, y) == white);
                write_ppm(dir, "loopmenu_editor64");
                s.lm_pos = (uint16_t)(1u | CR_LM_PLAY(16));
                cr_draw(&s, 0);
                memcpy(a, host_screen, sizeof a);
                render(&s, 0);
                snprintf(name, sizeof name, "loop editor: 64 steps: four rows of 16 (%u red px), the long note's line on into row 2 "
                         "(%u px); the playhead from row 1 to row 2: the partial draw equals a full one", n0, n5);
                check(name, n0 > 1000u && n5 >= 60u && !memcmp(a, host_screen, sizeof a));
            }
        }
#undef COUNT
#undef HS
    }

    {   /* the arp screen (the Chord view while an arpeggio plays): the hop alone moves -> only the strips from the
         * arc to the notes' line are composed (the name's are not), and the screen is the full draw's */
        int found = 0;
        for (k = 0; k < CR_NSCREENS && !found; k++)
            if (CR_SCREENS[k].kind == CR_K_ARP) {
                cr_screen_t s = CR_SCREENS[k];
                int same = 1;
                uint32_t most = 0, p;
                found = 1;
                s.anim = 0;
                render(&s, 0);
                for (p = 0; p < 2u * s.n_notes + 1u; p++) {           /* round the notes twice, then none (-1) */
                    s.pos = (int8_t)(p < 2u * s.n_notes ? (int32_t)((s.pos + 1) % (int32_t)s.n_notes) : -1);
                    cr_draw(&s, 0);
                    if (cr_dc.drawn > most) most = cr_dc.drawn;
                    memcpy(a, host_screen, sizeof a);
                    render(&s, 0);
                    same &= !memcmp(a, host_screen, sizeof a);
                    memcpy(b, host_screen, sizeof b);
                }
                snprintf(name, sizeof name, "arp: the hop moves: only its strips composed (%u of %u)", most,
                         (unsigned)CR_NSTRIP);
                check(name, most >= 1u && most <= 3u);
                check("arp: the hop's partial draw equals a full one", same);
                s.pos = 0;
                render(&s, 0);
                check("arp: another note sounding is another screen", memcmp(b, host_screen, sizeof b) != 0);
            }
        check("arp: the screen is in the table (the main sheet's arp state)", found);
    }

    {   /* the knob row's arp band (kr_band 2: the PERF layer while notes are stepped): a knob row of the table with a
         * footer (the fx sheet's), its band the arp of the main sheet's arp state. The hop alone moves: at most the
         * two strips over the row are composed, the row's pixels (and the footer's) never change, the partial draw is
         * the full one's; the band differs from the picker's and has ink between the label and the row */
        int fk = -1, fa = -1;
        for (k = 0; k < CR_NSCREENS; k++) {
            if (fk < 0 && CR_SCREENS[k].kind == CR_K_KNOBROW && !CR_SCREENS[k].kr_band && CR_SCREENS[k].footer[0])
                fk = (int)k;
            if (fa < 0 && CR_SCREENS[k].kind == CR_K_ARP)
                fa = (int)k;
        }
        check("knob row arp band: a knob row with a footer and the arp state are in the table", fk >= 0 && fa >= 0);
        if (fk >= 0 && fa >= 0) {
            cr_screen_t s = CR_SCREENS[fk];
            uint32_t ry, most = 0, p, i, ink = 0;
            int same = 1, row = 1;
            s.anim = 0;
            render(&s, 0);
            memcpy(b, host_screen, sizeof b);                         /* the picker band */
            s.kr_band = 2;
            s.name = CR_SCREENS[fa].name;
            s.n_notes = CR_SCREENS[fa].n_notes;
            memcpy(s.note, CR_SCREENS[fa].note, sizeof s.note);
            s.hop_col = CR_COL_WHITE;
            s.pos = 0;
            ry = (uint32_t)cr_kr_rowy(&s);
            render(&s, 0);
            check("knob row arp band: not the picker's band", memcmp(b, host_screen, ry * 240u * sizeof b[0]) != 0);
            check("knob row arp band: the row and the footer are the picker screen's",
                  !memcmp(b + ry * 240u, host_screen + ry * 240u, (240u - ry) * 240u * sizeof b[0]));
            for (i = (CR_PY0 + 16u) * 240u; i < ry * 240u; i++) ink += host_screen[i] != host_screen[0];
            check("knob row arp band: the name, the notes and the hop are drawn in the band", ink > 400u);
            memcpy(b, host_screen, sizeof b);
            for (p = 0; p < 2u * s.n_notes + 1u; p++) {               /* round the notes twice, then none (-1) */
                s.pos = (int8_t)(p < 2u * s.n_notes ? (int32_t)((s.pos + 1) % (int32_t)s.n_notes) : -1);
                cr_draw(&s, 0);
                if (cr_dc.drawn > most) most = cr_dc.drawn;
                row &= !memcmp(b + ry * 240u, host_screen + ry * 240u, (240u - ry) * 240u * sizeof b[0]);
                memcpy(a, host_screen, sizeof a);
                render(&s, 0);
                same &= !memcmp(a, host_screen, sizeof a);
            }
            snprintf(name, sizeof name, "knob row arp band: a hop composes the band's strips only (%u of %u; the row's two never)",
                     most, (unsigned)CR_NSTRIP);
            check(name, most >= 1u && most <= 2u && cr_strips_of(CR_KA_BY(&s) - 40, CR_KA_BY(&s) + 14) == 6u);
            check("knob row arp band: the knob row's pixels are unchanged across the hops", row);
            check("knob row arp band: the hop's partial draw equals a full one", same);
            s.hot_r = 1;                                              /* a cell turned under the arp: the row's strips */
            s.hot_c = 0;
            cr_draw(&s, 0);
            check("knob row arp band: a hot cell composes the row's strips (2), not the band's", cr_dc.drawn == 2u);
        }
    }

    {   /* animations: pure and settling */
        int pure = 1, settle = 1, moving = 1;
        for (i = 0; i < CR_NSCREENS; i++) {
            const cr_screen_t *s = &CR_SCREENS[i];
            cr_screen_t st = *s;
            if (!s->anim) continue;
            render(s, CR_TEST_MID);
            memcpy(a, host_screen, sizeof a);
            render(&CR_SCREENS[(i + 1u) % CR_NSCREENS], 33u);
            render(s, 17u);
            render(s, CR_TEST_MID);
            pure &= !memcmp(a, host_screen, sizeof a);
            st.anim = 0;
            render(&st, 0);
            memcpy(b, host_screen, sizeof b);
            moving &= memcmp(a, b, sizeof a) != 0 || !(s->anim & (CR_A_SQUEEZE | CR_A_SLIDE | CR_A_FILL));
            if (s->anim & CR_A_STRIPES) continue;                       /* (the stripes run until the screen changes) */
            render(s, 5000u);
            settle &= !memcmp(b, host_screen, sizeof b) && !cr_anim_busy(s, 5000u);
            if (memcmp(b, host_screen, sizeof b)) fprintf(rep, "not settled: %s\n", CR_SCREEN_SLUGS[i]);
        }
        check("an animation frame is a function of (state, anim_ms) only", pure);
        check("mid-animation frames differ from the settled ones (squeeze, slide, fill)", moving);
        check("past their duration the animations equal the settled screen", settle);
    }

    {   /* the major triad's "M" (2026-10-12: "C major would read CM"): the widest of them, G#M, as the big name of every
         * screen in the table that names a chord (the "M" at the quality's size after the root, on its line, inside
         * the panel: narrower than "dim" was), and on step entry's line beside "step 64/64". Nothing cut, no lint
         * finding. (The loop editor's name line and its playing name: with the editor's checks above) */
        uint32_t k, i, f0 = nfind, e0 = nell, views = 0, okv = 1, wM = 0, wdim = 0, steps = 0, oks = 1;
        for (k = 0; k < CR_NSCREENS; k++) {
            cr_screen_t s = CR_SCREENS[k];
            int32_t rx1 = -1, mx0 = -1, mx1 = -1, ry0 = 0, ry1 = 0, my1 = 0, cut = 0, got = 0;
            s.anim = 0;
            if (s.kind == CR_K_LOOPMENU && s.lm_mid == CR_LM_STEPS) {
                snprintf(s.sub, sizeof s.sub, "G#M");
                snprintf(s.value, sizeof s.value, "step 64/64");
                render(&s, 0);
                lint("major_m_step", &s);
                if (!steps) write_ppm(dir, "major_m_step");
                for (i = 0; i < nbox; i++)
                    if (!strcmp(boxes[i].s, "G#M")) { got = 1; cut |= boxes[i].flags & 1; }
                oks &= got && !cut;
                steps++;
                continue;
            }
            if (!s.name.root[0] || s.name.sup[0] || s.kind == CR_K_LOOPMENU) continue;
            snprintf(s.name.root, sizeof s.name.root, "G#");
            snprintf(s.name.quality, sizeof s.name.quality, "M");
            render(&s, 0);
            lint("major_m_name", &s);
            if (!views) write_ppm(dir, "major_m_name");
            for (i = 0; i < nbox; i++) {
                if (!strcmp(boxes[i].s, "G#") && rx1 < 0) { rx1 = boxes[i].x1; ry0 = boxes[i].y0; ry1 = boxes[i].y1; }
                if (!strcmp(boxes[i].s, "M") && mx0 < 0) { mx0 = boxes[i].x0; mx1 = boxes[i].x1; my1 = boxes[i].y1; cut = boxes[i].flags & 1; }
            }
            if (!(rx1 > 0 && mx0 >= rx1 && mx1 <= 232 && my1 > ry0 && my1 <= ry1 + 2 && !cut)) {
                okv = 0;
                fprintf(rep, "major M: %s: root x1 %d rows %d..%d, M x %d..%d y1 %d cut %d\n", CR_SCREEN_SLUGS[k], (int)rx1, (int)ry0,
                        (int)ry1, (int)mx0, (int)mx1, (int)my1, (int)cut);
            }
            if (!views) wM = (uint32_t)(mx1 - mx0);
            snprintf(s.name.quality, sizeof s.name.quality, "dim");
            render(&s, 0);
            if (!views)
                for (i = 0; i < nbox; i++)
                    if (!strcmp(boxes[i].s, "dim")) wdim = (uint32_t)(boxes[i].x1 - boxes[i].x0);
            views++;
        }
        snprintf(name, sizeof name, "major 'M': the big name G#M on %u screens: M after the root, on its line, whole (%u px; 'dim' %u); "
                 "%u findings, %u cut", views, wM, wdim, nfind - f0, nell - e0);
        check(name, views >= 3u && okv && wM > 0u && wM < wdim && nfind == f0 && nell == e0);
        snprintf(name, sizeof name, "major 'M': step entry's line 'G#M' beside 'step 64/64' (%u screens): whole", steps);
        check(name, steps >= 1u && oks && nfind == f0 && nell == e0);
    }

    {   /* Options > Chord Names (0.20, docs/CHORD-NAMES.md): every chord the engine can name (7 qualities x 16 extension
         * sets), under the two widest roots (G#, A#), in both settings, the three strings as cr_ui.c cu_name copies
         * them: (1) as the big name of every kind of screen that names a chord (the Chord view's 104 px, the Keyboard
         * view's 40, the arp's 44, the PERF layer's arp band's 20, Geek Out's 40): whole, on the screen, over nothing
         * else, never narrower than the squeeze's floor; (2) as the Scope view's name in the header and the Notes
         * view's title; (3) on the looper's name line with a voicing "-12" beside "step 64/64"; (4) as the loop
         * editor's playing name (78 px, 13 / 11 / 10 px). Nothing ellipsised, 0 lint findings. The widest of each go to
         * the report and to the PPMs chord_names_* (in Standard: the widest of its own names; the 28 cells with both
         * sevenths are Orchid's names there, checked with the rest and measured in the Orchid pass) */
        static const uint8_t ROOT[2] = {8, 10};
        static const char *const MODE[2] = {"Orchid", "Standard"};
        enum { NK = 5 };
        uint32_t mode, k, i;
        for (mode = 0; mode < 2u; mode++) {
            uint32_t f0 = nfind, e0 = nell, kinds = 0, seen = 0, cells = 0, okv = 1, lines = 0, nows = 0, okl = 1, okn = 1, now10 = 0;
            uint32_t texts = 0, okt = 1, tcells = 0;
            int32_t wline = 0, wnow = 0, wtext = 0;
            char widest_line[40] = "", widest_now[40] = "", widest_text[40] = "", low[NK][40];
            int32_t lowsq[NK];
            uint32_t kindpx[NK];
            cr_chord_names_set((int)mode);
            for (k = 0; k < CR_NSCREENS; k++) {
                cr_screen_t s = CR_SCREENS[k];
                uint32_t r, q, x, kk = 0, px = 0, maxw = 224, id = s.kind, role;
                enum { R_NAME = 1, R_TEXT, R_LOOP };
                s.anim = 0;
                if (s.kind == CR_K_LOOPMENU) role = s.lm_mid == CR_LM_STEPS && !lines ? R_LOOP : 0;
                else if (s.kind == CR_K_SCOPE || s.kind == CR_K_TEXT) role = R_TEXT;
                else if (s.kind == CR_K_KNOBROW) { role = s.kr_band == 0 ? R_NAME : 0; }
                else role = s.kind == CR_K_KEYBOARD || s.kind == CR_K_ARP || s.kind == CR_K_GEEK || (s.kind == CR_K_CHORD && s.name.root[0]) ? R_NAME : 0;
                if (!role || (role != R_LOOP && (seen & (1u << id)))) continue;
                seen |= 1u << id;
                if (role == R_NAME) {
                    kk = kinds++;
                    if (kk >= NK) { okv = 0; break; }
                    if (s.kind == CR_K_KNOBROW) {                  /* the PERF layer while notes are stepped: the arp band */
                        s.kr_band = 2;
                        s.n_notes = 3;
                        snprintf(s.note[0].t, sizeof s.note[0].t, "G#3");
                        snprintf(s.note[1].t, sizeof s.note[1].t, "C4");
                        snprintf(s.note[2].t, sizeof s.note[2].t, "D#4");
                        s.pos = 0;
                        maxw = 200;
                    }
                    px = s.kind == CR_K_CHORD ? (s.size ? s.size : 104u) : s.kind == CR_K_ARP ? (s.size ? s.size : 44u)
                         : s.kind == CR_K_KNOBROW ? 20u : 40u;
                    if (s.kind == CR_K_GEEK) maxw = CR_GEEK_NAME_W;
                    kindpx[kk] = px;
                    lowsq[kk] = 4096;
                    snprintf(low[kk], sizeof low[kk], "none");
                }
                if (role == R_TEXT) texts++;
                for (r = 0; r < 2u; r++)
                    for (q = 1; q < CR_Q_COUNT; q++)
                        for (x = 0; x < 16u; x++) {
                            char rt[3], ql[5], sp[CR_SUP_MAX], all[40], slug[48];
                            uint32_t j, got = 0, cut = 0, own = !(mode == 1u && (x & 6u) == 6u);   /* (Standard: both sevenths
                                                                     * keep Orchid's name: checked here, measured there) */
                            cr_chord_name(ROOT[r], (uint8_t)q, (uint8_t)x, rt, ql, sp);
                            snprintf(all, sizeof all, "%s%s%s", rt, ql, sp);
                            if (role == R_LOOP) {                   /* (3) and (4) */
                                int32_t x1 = 0;
                                snprintf(s.sub, sizeof s.sub, "%s\t-12", all);
                                snprintf(s.value, sizeof s.value, "step 64/64");
                                snprintf(s.lm_now, sizeof s.lm_now, "%s", all);
                                if (strlen(all) >= sizeof s.lm_now) okn = 0;
                                render(&s, 0);
                                snprintf(slug, sizeof slug, "chord_names_%s_line", MODE[mode]);
                                lint(slug, &s);
                                for (j = 0; j < nbox; j++) {
                                    if (strcmp(boxes[j].s, all)) continue;
                                    if (boxes[j].x1 <= 84 && boxes[j].y0 < 92) {            /* the playing name */
                                        got |= 1u; x1 = boxes[j].x1;
                                    } else {
                                        got |= 2u;
                                        if (own && boxes[j].x1 - boxes[j].x0 > wline) { wline = boxes[j].x1 - boxes[j].x0; snprintf(widest_line, sizeof widest_line, "%s", all); }
                                    }
                                    cut |= boxes[j].flags & 1u;
                                }
                                if (got != 3u || cut) { okl = 0; fprintf(rep, "chord names (%s): '%s' on the looper's lines: got %u cut %u\n", MODE[mode], all, got, cut); }
                                if (own && cr_tw(all, 13, 1) > P8(CR_LM_NOW_W) && cr_tw(all, 11, 1) > P8(CR_LM_NOW_W)) now10++;
                                if (own && cr_tw(all, 10, 1) > wnow) { wnow = cr_tw(all, 10, 1); snprintf(widest_now, sizeof widest_now, "%s", all); }
                                if (x1 > 84 || cr_tw(all, 10, 1) > P8(CR_LM_NOW_W)) okn = 0;
                                nows++;
                            } else if (role == R_TEXT) {            /* (2) */
                                if (s.kind == CR_K_SCOPE) { snprintf(s.mid, sizeof s.mid, "%s", all); if (strlen(all) >= sizeof s.mid) okt = 0; }
                                else { snprintf(s.title, sizeof s.title, "%s", all); if (strlen(all) >= sizeof s.title) okt = 0; }
                                render(&s, 0);
                                snprintf(slug, sizeof slug, "chord_names_%s_%s", MODE[mode], s.kind == CR_K_SCOPE ? "scope" : "notes");
                                lint(slug, &s);
                                for (j = 0; j < nbox; j++) {
                                    if (strcmp(boxes[j].s, all)) continue;
                                    got = 1; cut |= boxes[j].flags & 1u;
                                    if (own && boxes[j].x1 - boxes[j].x0 > wtext) { wtext = boxes[j].x1 - boxes[j].x0; snprintf(widest_text, sizeof widest_text, "%s", all); }
                                }
                                if (!got || cut) { okt = 0; fprintf(rep, "chord names (%s): '%s' on %s: got %u cut %u\n", MODE[mode], all, CR_SCREEN_SLUGS[k], got, cut); }
                                tcells++;
                            } else {                                /* (1) */
                                uint32_t qs = (px * 58u + 50u) / 100u, ss = (px * 38u + 50u) / 100u;
                                int32_t w, sq, bx0 = 9999, bx1 = -9999;
                                snprintf(s.name.root, sizeof s.name.root, "%s", rt);
                                snprintf(s.name.quality, sizeof s.name.quality, "%s", ql);
                                snprintf(s.name.sup, sizeof s.name.sup, "%s", sp);
                                if (strcmp(s.name.sup, sp) || strcmp(s.name.quality, ql)) okv = 0;      /* (the screen's arrays hold it whole) */
                                w = cr_tw(rt, px, 1) + (ql[0] ? cr_tw(ql, qs, 1) + P8(2) : 0) + (sp[0] ? cr_tw(sp, ss, 1) + P8(3) : 0);
                                sq = P8(maxw) < w ? (int32_t)((P8(maxw) << 12) / w) : 4096;
                                if (own && mode == 1u && px == 104u && r == 0u && sq < 4096)
                                    fprintf(rep, "chord names (Standard): at 104 px '%s' is squeezed to %d of 4096 (%d px wide)\n", all, (int)sq, (int)(w >> 8));
                                if (own && sq < lowsq[kk]) { lowsq[kk] = sq; snprintf(low[kk], sizeof low[kk], "%s", all); s.from = s.name; }
                                render(&s, 0);
                                snprintf(slug, sizeof slug, "chord_names_%s_%upx", MODE[mode], px);
                                lint(slug, &s);
                                for (j = 0; j < nbox; j++) {
                                    int hit = (!strcmp(boxes[j].s, rt) && !(got & 1u)) ? 1 : (ql[0] && !strcmp(boxes[j].s, ql) && !(got & 2u)) ? 2
                                              : (sp[0] && !strcmp(boxes[j].s, sp) && !(got & 4u)) ? 4 : 0;
                                    if (!hit) continue;
                                    got |= (uint32_t)hit;
                                    cut |= boxes[j].flags & 1u;
                                    if (boxes[j].x0 < bx0) bx0 = boxes[j].x0;
                                    if (boxes[j].x1 > bx1) bx1 = boxes[j].x1;
                                }
                                if (got != (1u | (ql[0] ? 2u : 0u) | (sp[0] ? 4u : 0u)) || cut || bx0 < 0 || bx1 > 240 || sq < 300) {
                                    okv = 0;
                                    fprintf(rep, "chord names (%s): '%s' at %u px on %s: got %u cut %u x %d..%d squeeze %d\n", MODE[mode], all, px,
                                            CR_SCREEN_SLUGS[k], got, cut, (int)bx0, (int)bx1, (int)sq);
                                }
                                cells++;
                            }
                        }
                if (role == R_LOOP) {                               /* the pictures: the widest of each */
                    lines++;
                    snprintf(s.sub, sizeof s.sub, "%s\t-12", widest_line);
                    snprintf(s.lm_now, sizeof s.lm_now, "%s", widest_now);
                    render(&s, 0);
                    snprintf(name, sizeof name, "chord_names_%s_looper", MODE[mode]);
                    write_ppm(dir, name);
                } else if (role == R_NAME) {
                    if (lowsq[kk] < 4096) s.name = s.from;          /* (the most squeezed; none: the last, G#/A#m add4 WTF) */
                    memset(&s.from, 0, sizeof s.from);
                    render(&s, 0);
                    snprintf(name, sizeof name, "chord_names_%s_%upx_%s", MODE[mode], px, s.kind == CR_K_GEEK ? "geek" : s.kind == CR_K_KEYBOARD ? "keyboard"
                             : s.kind == CR_K_ARP ? "arp" : s.kind == CR_K_KNOBROW ? "perf" : "chord");
                    write_ppm(dir, name);
                }
            }
            for (i = 0; i < kinds && i < NK; i++)
                fprintf(rep, "chord names (%s): at %u px the most squeezed is '%s', to %d of 4096\n", MODE[mode], kindpx[i], low[i], (int)lowsq[i]);
            fprintf(rep, "chord names (%s): the looper's name line: the widest '%s' %d px at 15 px; the playing name: the widest '%s' %d px at 10 px (of %d), "
                    "%u of %u names at 10 px; the Scope / Notes views: the widest '%s' %d px\n", MODE[mode], widest_line, (int)wline, widest_now,
                    (int)((wnow + 255) >> 8), CR_LM_NOW_W, now10, nows, widest_text, (int)wtext);
            snprintf(name, sizeof name, "chord names, %s: 224 names as the big name on %u kinds of screen (%u renders): whole, on screen; %u findings, %u cut",
                     MODE[mode], kinds, cells, nfind - f0, nell - e0);
            check(name, kinds == NK && okv && cells == kinds * 224u && nfind == f0 && nell == e0);
            snprintf(name, sizeof name, "chord names, %s: the Scope view's header and the Notes view's title (%u screens, widest '%s' %d px): whole",
                     MODE[mode], texts, widest_text, (int)wtext);
            check(name, texts == 2u && tcells == 448u && okt && nfind == f0 && nell == e0);
            snprintf(name, sizeof name, "chord names, %s: the looper's name line with '-12' beside 'step 64/64' (widest '%s') and the playing name "
                     "(widest '%s', %d of %d px): whole", MODE[mode], widest_line, widest_now, (int)((wnow + 255) >> 8), CR_LM_NOW_W);
            check(name, lines == 1u && nows == 224u && okl && okn && nfind == f0 && nell == e0);
        }
        cr_chord_names_set(0);
    }

    {   /* Options > 6th with 7th (0.20, docs/CHORD-NAMES.md "The 6th above a seventh"): the entry as cr_ui.c
         * cu_options_screen builds it (the fourth of 26: the list's first window, Secret Chords above it, Velocity
         * below), with each of its two values and the line under it: every text whole, nothing over another */
        static const char *const ITEMS[8] = {"Play Style", "Extension Addition", "Secret Chords", "6th with 7th", "Velocity",
                                             "Bass Behaviour", "Single Notes", "Split Point"};
        static const char *const VAL[2] = {"In place", "Octave up"};
        static const char *const LAB[2] = {"6 + 7: the 6th under the 7th", "6 + 7: the 6th on top, a 13th"};
        uint32_t v, k, j, f0 = nfind, e0 = nell, okv = 1, found = 0;
        char name[200];
        for (k = 0; k < CR_NSCREENS; k++)
            if (!strcmp(CR_SCREEN_SLUGS[k], "12_opt_tapped_the_options_menu")) break;
        for (v = 0; v < 2u && k < CR_NSCREENS; v++) {
            cr_screen_t s = CR_SCREENS[k];
            uint32_t got = 0;
            memset(s.item, 0, sizeof s.item);
            for (j = 0; j < 8u; j++) snprintf(s.item[j], sizeof s.item[j], "%s", ITEMS[j]);
            s.n_items = 26;
            s.item0 = 0;
            s.sel = 3;
            snprintf(s.value, sizeof s.value, "%s", VAL[v]);
            snprintf(s.label, sizeof s.label, "%s", LAB[v]);
            if (strcmp(s.item[3], ITEMS[3]) || strcmp(s.value, VAL[v]) || strcmp(s.label, LAB[v])) okv = 0;   /* (the arrays hold them whole) */
            render(&s, 0);
            lint(v ? "options_6th_with_7th_octave_up" : "options_6th_with_7th_in_place", &s);
            for (j = 0; j < nbox; j++) {
                uint32_t hit = !strcmp(boxes[j].s, ITEMS[3]) ? 1u : !strcmp(boxes[j].s, VAL[v]) ? 2u : !strcmp(boxes[j].s, LAB[v]) ? 4u : 0u;
                if (!hit) continue;
                got |= hit;
                if ((boxes[j].flags & 1u) || boxes[j].x0 < 0 || boxes[j].x1 > 240) okv = 0;
            }
            if (got != 7u) okv = 0;
            found++;
            write_ppm(dir, v ? "options_6th_with_7th_octave_up" : "options_6th_with_7th_in_place");
        }
        snprintf(name, sizeof name, "Options > 6th with 7th: the entry with 'In place' and 'Octave up' and their lines: whole, on screen; %u findings, %u cut",
                 nfind - f0, nell - e0);
        check(name, found == 2u && okv && nfind == f0 && nell == e0);
    }

    {   /* Options > Version (3y66): the unit's ID on the line under the version, as cr_ui.c cu_options_screen builds it
         * (the 24th of 26: the list's last window), with the longest version that shows whole (23 characters) and
         * the widest digits, and without an ID: every text whole, nothing over another */
        static const char *const ITEMS[8] = {"USB Record", "USB Level", "USB Rate", "Dual Core", "Output", "Version",
                                             "Calibrate", "Reset Settings"};
        static const char *const LAB[3] = {"unit ID DDDDDDDD", "unit ID A1B2C3D4", "no unit ID"};
        static const char VAL[] = "ChoralRoot 0.22-beta1 1";
        uint32_t v, k, j, f0 = nfind, e0 = nell, okv = 1, found = 0;
        char name[200];
        for (k = 0; k < CR_NSCREENS; k++)
            if (!strcmp(CR_SCREEN_SLUGS[k], "12_opt_tapped_the_options_menu")) break;
        for (v = 0; v < 3u && k < CR_NSCREENS; v++) {
            cr_screen_t s = CR_SCREENS[k];
            uint32_t got = 0;
            memset(s.item, 0, sizeof s.item);
            for (j = 0; j < 8u; j++) snprintf(s.item[j], sizeof s.item[j], "%s", ITEMS[j]);
            s.n_items = 26;
            s.item0 = 18;
            s.sel = 23;
            s.anim = 0;                                  /* (settled: no slide from the entry before) */
            snprintf(s.value, sizeof s.value, "%s", VAL);
            snprintf(s.label, sizeof s.label, "%s", LAB[v]);
            if (strcmp(s.value, VAL) || strcmp(s.label, LAB[v])) okv = 0;   /* (the arrays hold them whole) */
            render(&s, 0);
            lint(v == 2u ? "options_version_no_unit_id" : "options_version_unit_id", &s);
            for (j = 0; j < nbox; j++) {
                uint32_t hit = !strcmp(boxes[j].s, ITEMS[5]) ? 1u : !strcmp(boxes[j].s, VAL) ? 2u : !strcmp(boxes[j].s, LAB[v]) ? 4u : 0u;
                if (!hit) continue;
                got |= hit;
                if ((boxes[j].flags & 1u) || boxes[j].x0 < 0 || boxes[j].x1 > 240) okv = 0;
            }
            if (got != 7u) okv = 0;
            found++;
            if (v != 1u)
                write_ppm(dir, v == 2u ? "options_version_no_unit_id" : "options_version_unit_id");
        }
        snprintf(name, sizeof name, "Options > Version: the unit's ID under the version (and 'no unit ID'): whole, on screen; %u findings, %u cut",
                 nfind - f0, nell - e0);
        check(name, found == 3u && okv && nfind == f0 && nell == e0);
    }

#if CR_LAYC_N
    rows_screens(dir);
#endif
    p5cell_screens(dir);

    fprintf(rep, "\n%u lint findings, %u ellipsised texts\n", nfind, nell);
    fclose(rep);
    printf("cr_draw: %u screens x 2 frames rendered to %s/ppm; %u lint findings, %u ellipsised; report %s\n",
           CR_NSCREENS, dir, nfind, nell, path);
    return nfind || nfail ? 1 : 0;
}
