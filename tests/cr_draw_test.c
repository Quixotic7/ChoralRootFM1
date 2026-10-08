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
#include "../firmware/src/cr_draw.c"
#include "cr_screens_gen.h"

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

static uint32_t mod_index(void)
{
    uint32_t i;
    for (i = 0; i < NPALETTES; i++)
        if (!strcmp(UI_PALETTES[i].name, "MOD")) return i;
    return NPALETTES;
}

int main(int argc, char **argv)
{
    static uint16_t a[240 * 240], b[240 * 240];
    const char *dir = argc > 1 ? argv[1] : "build/cr_screens";
    char path[512], name[160];
    uint32_t i, k, pal = mod_index(), worst = 0;

    snprintf(path, sizeof path, "%s/report.txt", dir);
    if (!(rep = fopen(path, "w"))) { fprintf(stderr, "cr_draw_test: cannot write %s (make %s/ppm first)\n", path, dir); return 1; }
    check("the MOD palette is one of the UI palettes", pal < NPALETTES);
    palette_set(pal < NPALETTES ? pal : 0u);

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
        check(name, cr_ring.state == 1u && diffs == 0);
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
            {CR_G_SPRING, "spring"}, {CR_G_MIX, "mix"}, {CR_G_GATE, "gate"}, {CR_G_RANGE, "range"},
            {CR_G_ARROW, "arrow"}, {CR_G_SHIFT, "shift"}};
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
        {   /* room: the far wall's width (from the centre to its left edge on the middle row) shrinks with pct */
            int32_t w[3], k, x;
            for (k = 0; k < 3; k++) {
                PIC(CR_G_ROOM, k * 127, 128);
                for (x = BX + BW / 2; x > BX && PX(x, BY + BH / 2) == bg; x--) {}
                w[k] = BX + BW / 2 - x;
            }
            snprintf(name, sizeof name, "room: the far wall shrinks as SIZE grows (half widths %d %d %d px)", w[0], w[1], w[2]);
            check(name, w[0] > w[1] && w[1] > w[2]);
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
        if (ia >= 0) {   /* algo: four boxes where the table puts them (outline columns inked, the letter inside), 8 pictures */
            static uint16_t pic[8][240 * 100];
            cr_screen_t s = CR_SCREENS[ia];
            uint32_t n, boxes4 = 1, differ = 1, j, thin, thick;
            for (n = 1; n <= 8u; n++) {
                uint32_t found = 0;
                s.wv[0] = (uint8_t)n;
                render(&s, 0);
                memcpy(pic[n - 1], host_screen + 22u * 240u, sizeof pic[0]);
                for (k = 0; k < 4u; k++) {
                    uint32_t ox = 60u + CR_ALGOS[n - 1].pos[k][1] * 154u / 100u, oy = 24u + 13u + 24u * CR_ALGOS[n - 1].pos[k][0];
                    uint32_t el, er, in;
                    COUNT(el, ox - 8u, oy - 6u, ox - 6u, oy + 7u, HS(x, y) != bg);
                    COUNT(er, ox + 6u, oy - 6u, ox + 8u, oy + 7u, HS(x, y) != bg);
                    COUNT(in, ox - 4u, oy - 4u, ox + 5u, oy + 5u, HS(x, y) != bg);
                    found += el >= 13u && er >= 13u && in >= 8u;
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
            s.wv[0] = 3;                                         /* B2's loop at the right of its box */
            s.wv[1] = 0;
            render(&s, 0);
            COUNT(thin, 60u + 70u * 154u / 100u + 7u, 30u, 60u + 70u * 154u / 100u + 15u, 45u, HS(x, y) != bg);
            s.wv[1] = 255;
            render(&s, 0);
            COUNT(thick, 60u + 70u * 154u / 100u + 7u, 30u, 60u + 70u * 154u / 100u + 15u, 45u, HS(x, y) != bg);
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

    fprintf(rep, "\n%u lint findings, %u ellipsised texts\n", nfind, nell);
    fclose(rep);
    printf("cr_draw: %u screens x 2 frames rendered to %s/ppm; %u lint findings, %u ellipsised; report %s\n",
           CR_NSCREENS, dir, nfind, nell, path);
    return nfind || nfail ? 1 : 0;
}
