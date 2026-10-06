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
        if (b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) finding(scr, "off the screen", b, 0);
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
