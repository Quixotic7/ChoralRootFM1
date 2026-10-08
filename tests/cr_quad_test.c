/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the QUAD engine (firmware/src/eng_quad.c, docs/QUAD.md), no HAL: the engine on core.h and dsp.c with a
 * minimal voice driver (voice.c's control tick for an ownenv engine: done, then render), as tests/cr_va_test.c.
 *   sh tests/run_cr_tests.sh          (built with -fsanitize=signed-integer-overflow: an int32 wrap aborts)
 *   build/host/cr_quad_test cpu       (also the CPU figure against the VA's, same driver; macOS)
 * Checks: the blob round trip of 2000 random patches; bad blobs -> the init patch; the page table against the ranges
 * and sections; set() clamps; the macros both ways; every preset's blob and macros; the ratio tables (C / B steps, A
 * steps, the B pair sequence and its names); the routing table against the designer's ALGOS; every algorithm and
 * four presets against tests/quad_ref.py's renders (tests/quad_goldens: SNR over the first 2048 samples > 36 dB AND
 * every 10 ms block's RMS within 1.5 dB over 0.3 s); the envelopes' times (ATK, DEC to END, DELAY, LEV, TRIG, RESET,
 * the amp's release); the filter types and the base-width window; the LFOs' rates (within 2 %), direction, start
 * phase, ONE / HALF, waves; no int32 wrap at full feedback / levels / LFOs on every algorithm; voices end after the
 * release; a 6-note chord for 1.4 s on every preset: peak < 0.9 FS at LEVEL 92. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#define __attribute__(x)
#define FELUCCA_VA 1             /* (the VA too: the CPU comparison) */
#define FELUCCA_CZ 1
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1
#endif
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/eng_va.c"
#include "../firmware/src/eng_quad.c"
#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#endif

static int fails, checks;
#define CHECK(c, ...)                                   \
    do {                                                \
        checks++;                                       \
        if (!(c)) {                                     \
            fails++;                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static uint32_t rs = 12345u;
static uint32_t rnd(void)
{
    rs = rs * 1664525u + 1013904223u;
    return rs >> 8;
}
static double db(double a, double b) { return 20.0 * log10((a + 1e-9) / (b + 1e-9)); }
static double tms(uint32_t ticks) { return ticks * CTL * 1000.0 / FS; }

/* ------------------------------------------------------- voice driver --- */
static void drv_reset(track_t *t)
{
    memset(t, 0, sizeof *t);
    memset(quad_vs, 0, sizeof quad_vs);
    memset(quad_lfo, 0, sizeof quad_lfo);
    t->p[P_VOICE] = V_POLY;
    t->eng_req = t->engine = ENGI_QUAD;
    song.g[G_BPM] = 120;
}
static void drv_patch(track_t *t, const int8_t *p)
{
    memcpy(quad_patch[quad_tr(t)], p, QP_NP);
    quad_macros_out(t);
}
static void drv_on(track_t *t, uint32_t i, uint32_t note, uint32_t vel)
{
    voice_t *v = &t->v[i];
    int sounding = v->active;
    v->note = (uint8_t)note;
    v->vel = v->mvel = (uint8_t)vel;
    v->gate = v->active = 1;
    v->stage = 1;
    v->age = i + 1u;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    t->m_vi = (uint8_t)i;
    if (!sounding)
        v->env = v->env_out = 0;
    quad_note_on(t, v);
}
/* one control tick of the part: block, then each voice (done, then render) into out; returns the voices rendered */
static uint32_t drv_tick(track_t *t, int32_t *out)
{
    uint32_t i, nr = 0;
    memset(out, 0, CTL * sizeof *out);
    quad_block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        if (!v->active)
            continue;
        if (quad_done(t, v)) {
            v->active = v->gate = 0;
            v->env = v->env_out = 0;
            continue;
        }
        v->env = 1 << 24;
        memset(&m, 0, sizeof m);
        m.amp0 = v->env_out;
        m.amp1 = v->env_out = 32767;
        m.pitch16 = v->pitch_cur;
        m.inc = pitch_inc((uint32_t)m.pitch16);
        m.shape = 64 << 8;
        quad_render(t, v, out, CTL, &m);
        nr++;
    }
    return nr;
}

static void rnd_patch(int8_t *p)
{
    uint32_t i;
    for (i = 0; i < QP_NP; i++) {
        quad_rng_t r = quad_range(i);
        p[i] = (int8_t)(r.min + (int32_t)(rnd() % (uint32_t)(r.max - r.min + 1)));
    }
}

/* ------------------------------------------------------------- checks --- */
static void t_blob(void)
{
    uint8_t b[QUAD_BLOB], b2[QUAD_BLOB];
    int8_t p[QP_NP], q[QP_NP], init[QP_NP];
    uint32_t n, i;
    quad_init_patch(init);
    CHECK(QUAD_DEEP.blob_size == QUAD_BLOB && QUAD_BLOB == 80u && QP_NP == 71, "blob %u bytes, %d values",
          QUAD_DEEP.blob_size, QP_NP);
    for (n = 0; n < 2000u; n++) {
        rnd_patch(p);
        quad_pack(p, b);
        CHECK(quad_blob_ok(b), "random patch %u: its blob is not valid", n);
        CHECK(quad_unpack(b, q) == 1 && !memcmp(p, q, QP_NP), "random patch %u: round trip differs", n);
        quad_pack(q, b2);
        CHECK(!memcmp(b, b2, QUAD_BLOB), "random patch %u: blob round trip differs", n);
        CHECK(b[0] == 'Q' && b[1] == 1u, "magic / version");
        for (i = 2u + QP_NP; i < QUAD_BLOB; i++)
            CHECK(!b[i], "padding byte %u not 0", i);
    }
    quad_pack(p, b);
    CHECK(quad_unpack(0, q) == 0 && !memcmp(q, init, QP_NP), "NULL blob: not the init patch");
    memcpy(b2, b, QUAD_BLOB);
    b2[0] ^= 1;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP), "bad magic: not the init patch");
    memcpy(b2, b, QUAD_BLOB);
    b2[1] = QUAD_VER + 1u;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP), "bad version: not the init patch");
    for (i = 0; i < QP_NP; i++) {                /* every value one past its range */
        quad_rng_t r = quad_range(i);
        memcpy(b2, b, QUAD_BLOB);
        b2[2 + i] = (uint8_t)(r.max - r.min + 1);
        CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP), "value %u out of range: not the init patch", i);
    }
    memcpy(b2, b, QUAD_BLOB);
    b2[QUAD_BLOB - 1u] = 1;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP), "padding not 0: not the init patch");
    memset(b2, 0xFF, sizeof b2);
    CHECK(quad_unpack(b2, q) == 0, "erased flash (0xFF): taken as a patch");
    {
        track_t *t = &trk[0];
        memset(t, 0, sizeof *t);
        b2[0] = 0;
        quad_blob_set(t, b2);
        CHECK(!memcmp(quad_patch[0], init, QP_NP), "blob_set(bad): not the init patch");
        CHECK(t->p[P_E0] == 1 && t->p[P_E1] == 3 && t->p[P_E3] == QUAD_RB_DEF, "blob_set(bad): macros %d %d %d",
              t->p[P_E0], t->p[P_E1], t->p[P_E3]);
        quad_blob_set(t, 0);
        CHECK(!memcmp(quad_patch[0], init, QP_NP), "blob_set(0): not the init patch");
        rnd_patch(p);
        quad_pack(p, b);
        quad_blob_set(t, b);
        quad_blob_get(t, b2);
        CHECK(!memcmp(b, b2, QUAD_BLOB), "blob_set / blob_get round trip");
        for (i = 0; i < 8u; i++)
            CHECK(t->p[P_E0 + i] == p[i], "blob_set: macro %u %d, patch %d", i, t->p[P_E0 + i], p[i]);
        quad_blob_get(&trk[2], b2);               /* a part without a patch: the init patch */
        CHECK(quad_unpack(b2, q) == 1 && !memcmp(q, init, QP_NP), "part 3's blob: not the init patch");
    }
}

static uint32_t names_n(const char *const *n)
{
    uint32_t k = 0;
    while (n[k])
        k++;
    return k;
}

static void t_pages(void)
{
    uint32_t pg, c, i, used[QP_NP] = {0};
    static const char *const TITLES[20] = {"SYN 1", "SYN 1+", "SYN 2", "FILTER", "FILTER+", "FILT 2", "FILT 2+", "ENV A",
        "ENV B", "ENV 2", "ENV 2+", "ENV 3", "AMP", "AMP+", "LFO 1", "LFO 1+", "LFO 2", "LFO 2+", "LFO 3", "LFO 3+"};
    CHECK(QUAD_DEEP.npages == 20u, "pages %u", QUAD_DEEP.npages);
    CHECK(QUAD_DEEP.section[0] == 0 && QUAD_DEEP.section[1] == 3 && QUAD_DEEP.section[2] == 7 &&
              QUAD_DEEP.section[3] == 14 && QUAD_DEEP.section[4] == 0xFF, "sections");
    for (pg = 0; pg < QUAD_NPAGES; pg++) {
        CHECK(!strcmp(QUAD_PAGES[pg].title, TITLES[pg]) && strlen(TITLES[pg]) <= 7u, "page %u title %s", pg,
              QUAD_PAGES[pg].title);
        for (c = 0; c < 4u; c++) {
            const param_desc_t *d = &QUAD_PAGES[pg].col[c];
            uint32_t ix = QUAD_MAP[pg][c];
            CHECK((ix == QUAD_X) == !d->label, "page %u col %u: label and map disagree", pg, c);
            if (ix == QUAD_X)
                continue;
            CHECK(d->min < d->max && d->def >= d->min && d->def <= d->max, "page %u col %u range", pg, c);
            if (d->fmt == F_ENUM)
                CHECK(d->names && names_n(d->names) >= (uint32_t)(d->max + 1), "page %u col %u: names", pg, c);
            if (d->fmt == F_INT && d->names)
                CHECK(names_n(d->names) == (uint32_t)(d->max - d->min + 1), "page %u col %u: %u names for %d values",
                      pg, c, names_n(d->names), d->max - d->min + 1);
            if (ix >= QP_NP) {
                CHECK(ix == QUAD_XPAN || ix == QUAD_XDIST, "page %u col %u: index %u", pg, c, ix);
                continue;
            }
            used[ix]++;
            {
                quad_rng_t r = quad_range(ix);
                CHECK(r.min == d->min && r.max == d->max && r.def == d->def, "value %u: range", ix);
            }
        }
    }
    for (i = 0; i < QP_NP; i++)
        CHECK(used[i] == 1u, "patch value %u on %u page columns", i, used[i]);
    for (i = 0; i < 8u; i++) {                   /* the macros = SYN 1's eight, as ENG_QUAD.edit */
        const param_desc_t *d = &QUAD_PAGES[i >> 2].col[i & 3u], *e = &ENG_QUAD.edit[i];
        CHECK(QUAD_MAP[i >> 2][i & 3u] == i, "SYN 1 column %u is not macro %u", i, i);
        CHECK(e->min == d->min && e->max == d->max && e->def == d->def && !strcmp(e->label, d->label) && e->fmt == d->fmt,
              "macro %u: edit[] differs from its page column", i);
    }
    CHECK(ENG_QUAD.knob[0] == P_E2 && ENG_QUAD.knob[1] == P_E4 && ENG_QUAD.knob[2] == P_E6 && ENG_QUAD.knob[3] == P_E7,
          "HOME knobs");
    CHECK(ENG_QUAD.ownenv && ENG_QUAD.done && ENG_QUAD.poly == 8 && !ENG_QUAD.render2 && ENG_QUAD.legato, "contract");
    /* the value texts */
    CHECK(!strcmp(N_QUAD_RCB[3], "1.00") && !strcmp(N_QUAD_RCB[18], "16.00") && !strcmp(N_QUAD_RA[7], "2.00") &&
              !strcmp(N_QUAD_RB[QUAD_RB_DEF], "1.00/1.00") && !strcmp(N_QUAD_RB[3 * QUAD_NRCB + 1], "0.50/1.00") &&
              !strcmp(N_QUAD_OFS[100], "+0.00") && !strcmp(N_QUAD_OFS[101], "+0.01") && !strcmp(N_QUAD_OFS[0], "-1.00"),
          "value names");
    CHECK(!strcmp(N_QUAD_MULT[4], "x16") && !strcmp(N_QUAD_MULT[11], "x2k") && QUAD_PAGES[1].col[0].fmt == F_OFS &&
              QUAD_PAGES[0].col[0].fmt == F_INT, "Mult / Harm / Algo formats");
}

static void t_set_macros(void)
{
    track_t *t = &trk[0];
    uint32_t pg, c;
    drv_reset(t);
    quad_blob_set(t, 0);
    for (pg = 0; pg < QUAD_NPAGES; pg++)         /* set clamps at both ends */
        for (c = 0; c < 4u; c++) {
            const param_desc_t *d = &QUAD_PAGES[pg].col[c];
            if (!d->label)
                continue;
            quad_set(t, pg, c, d->max + 50);
            CHECK(quad_get(t, pg, c) == d->max, "page %u col %u: set above max -> %d", pg, c, quad_get(t, pg, c));
            quad_set(t, pg, c, d->min - 50);
            CHECK(quad_get(t, pg, c) == d->min, "page %u col %u: set below min -> %d", pg, c, quad_get(t, pg, c));
        }
    quad_set(t, 13, 1, 20);                      /* PAN / DRIVE: the part's */
    quad_set(t, 13, 2, 77);
    CHECK(t->p[P_PAN] == 20 && t->p[P_DIST] == 77 && quad_get(t, 13, 1) == 20 && quad_get(t, 13, 2) == 77, "PAN / DRIVE");
    quad_set(&trk[3], 13, 1, -5);
    CHECK(trk[3].p[P_PAN] == -5, "PAN on a part without a patch");
    /* set -> P_E */
    quad_set(t, 0, 2, 9);
    quad_set(t, 1, 0, -7);
    CHECK(t->p[P_E2] == 9 && t->p[P_E4] == -7 && quad_patch[0][QP_RA] == 9, "set -> macros");
    /* P_E -> patch in quad_block */
    t->p[P_E6] = 99;
    t->p[P_E3] = 120;                            /* beyond RATIO B's range: clamped */
    quad_block(t);
    CHECK(quad_patch[0][QP_FDBK] == 99 && quad_patch[0][QP_RB] == QUAD_NRB - 1, "macros -> patch: %d %d",
          quad_patch[0][QP_FDBK], quad_patch[0][QP_RB]);
    quad_set(t, 0, 0, 5);
    quad_block(t);
    CHECK(quad_patch[0][QP_ALGO] == 5 && t->p[P_E0] == 5, "set then block: the macro holds");
}

static void t_presets(void)
{
    track_t *t = &trk[0];
    uint8_t b[QUAD_BLOB];
    uint32_t k, i;
    CHECK(QUAD_NPRESETS == 16u && ENG_QUAD.npresets == 16u, "presets %u", QUAD_NPRESETS);
    for (k = 0; k < QUAD_NPRESETS; k++) {
        const preset_t *pr = &QUAD_PRESETS[k];
        drv_reset(t);
        quad_blob_preset(t, k);
        quad_blob_get(t, b);
        CHECK(quad_blob_ok(b), "%s: blob not valid", pr->name);
        for (i = 0; i < 8u; i++)
            CHECK(quad_patch[0][i] == pr->e[i] && t->p[P_E0 + i] == pr->e[i], "%s: macro %u patch %d preset %d",
                  pr->name, i, quad_patch[0][i], pr->e[i]);
        CHECK(quad_patch[0][QP_EATK] == pr->env[0] && quad_patch[0][QP_EDEC] == pr->env[1] &&
                  quad_patch[0][QP_ESUS] == pr->env[2] && quad_patch[0][QP_EREL] == pr->env[3],
              "%s: preset_t.env is not the amp envelope", pr->name);
        for (i = 0; QUAD_PRESET_EDITS[k][i] != 0xFFu; i += 2)
            CHECK(QUAD_PRESET_EDITS[k][i] < QP_NP && quad_patch[0][QUAD_PRESET_EDITS[k][i]] ==
                      (int8_t)QUAD_PRESET_EDITS[k][i + 1], "%s: edit %u out of range", pr->name, i / 2);
        /* a load with the preset's macros on the track: its patch */
        memset(quad_patch[0], 0, QP_NP);
        t->preset = (uint8_t)k;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pr->e[i];
        quad_track_loaded(t);
        {
            int8_t p[QP_NP];
            quad_preset_patch(k, p);
            CHECK(!memcmp(p, quad_patch[0], QP_NP), "%s: track_loaded is not the preset's patch", pr->name);
        }
    }
}

static void t_ratios(void)
{
    uint32_t i;
    static const double CB[19] = {0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const double BRV[6] = {0.5, 1, 1.5, 2, 3, 4};
    for (i = 0; i < QUAD_NRCB; i++)
        CHECK(QUAD_RCB_Q16[i] == (int32_t)(CB[i] * 65536), "C/B step %u", i);
    for (i = 0; i < QUAD_NRA; i++)
        CHECK(QUAD_RA_Q16[i] == (int32_t)((i + 1) * 16384), "A step %u", i);
    for (i = 0; i < QUAD_NRB; i++) {             /* the pair: B1 cycles through its steps, then B2 steps on */
        char s[24];
        double b1 = CB[i % QUAD_NRCB], b2 = b1 * BRV[i / QUAD_NRCB];
        snprintf(s, sizeof s, "%.2f/%.2f", b1, b2);
        CHECK(!strcmp(s, N_QUAD_RB[i]) || (fabs(b2 - 0.125) < 1e-9 && !strcmp(N_QUAD_RB[i], "0.25/0.12")),
              "pair %u: %s, want %s", i, N_QUAD_RB[i], s);
        CHECK(QUAD_BR_Q16[i / QUAD_NRCB] == (int32_t)(BRV[i / QUAD_NRCB] * 65536), "BR %u", i / QUAD_NRCB);
    }
    CHECK(QRC(1) == 0 && QRC(4) == 3 && QRC(8) == 4 && QRC(64) == 18 && QRA(64) == 63 && QRB(4, 1) == QUAD_RB_DEF,
          "the preset macros' ratio helpers");
    /* increments: ratio 2 = twice, an offset, the detune in cents */
    {
        uint32_t base = pitch_inc(60 * 16), x;
        double c;
        CHECK(quad_inc(base, QUAD_RCB_Q16[4], 0) == 2u * base, "ratio 2.00: %u, base %u", quad_inc(base, QUAD_RCB_Q16[4], 0), base);
        x = quad_inc(base, quad_ratio(QUAD_RA_Q16[3], 37), 0);
        CHECK(fabs((double)x / base - 1.37) < 1e-4, "ratio 1.00 + 0.37: %.5f", (double)x / base);
        CHECK(quad_ratio(QUAD_RCB_Q16[0], -100) == 0, "a negative ratio clamps at 0");
        c = 1200.0 * log2((double)quad_inc(base, 65536, (127 * 15) >> 1) / quad_inc(base, 65536, -((127 * 15) >> 1)));
        CHECK(fabs(c - 50.0) < 1.0, "DTUNE 127: B1 / B2 %.2f cents apart", c);
        c = 1200.0 * log2((double)quad_inc(base, 65536, (127 * 15) >> 3) / base);
        CHECK(c > 5.0 && c < 7.0, "DTUNE 127: A %.2f cents", c);
    }
}

/* the designer's ALGOS (../ChoralRootFM1Designer/index.html): modulator -> target pairs, feedback, X, Y */
static void t_algo_table(void)
{
    static const char *const D[8][4] = {
        {"A>C B1>A B2>B1", "B2", "C", "C"}, {"A>C B1>C B2>B1", "B2", "C", "C"}, {"A>C B1>A B2>A", "B2", "C", "C"},
        {"A>C B2>B1", "B2", "C", "B1"}, {"A>C B1>A", "B2", "C", "B2"}, {"A>C A>B1 B2>B1", "B2", "C", "B1"},
        {"B2>B1", "A", "C A", "B1"}, {"", "B2", "C A", "B1 B2"}};
    static const char *const OP[4] = {"C", "A", "B1", "B2"};
    uint32_t a, o, k;
    for (a = 0; a < 8u; a++) {
        uint8_t mod[4] = {0}, x = 0, y = 0, fb = 0xFF;
        char buf[64], *tok;
        strcpy(buf, D[a][0]);
        for (tok = strtok(buf, " "); tok; tok = strtok(0, " ")) {
            char *gt = strchr(tok, '>');
            uint32_t s = 4, d = 4;
            *gt = 0;
            for (k = 0; k < 4u; k++) {
                if (!strcmp(tok, OP[k]))
                    s = k;
                if (!strcmp(gt + 1, OP[k]))
                    d = k;
            }
            mod[d] |= (uint8_t)(1u << s);
        }
        for (k = 0; k < 4u; k++) {
            char w[8];
            if (!strcmp(D[a][1], OP[k]))
                fb = (uint8_t)k;
            snprintf(w, sizeof w, " %s ", OP[k]);
            snprintf(buf, sizeof buf, " %s ", D[a][2]);
            if (strstr(buf, w))
                x |= (uint8_t)(1u << k);
            snprintf(buf, sizeof buf, " %s ", D[a][3]);
            if (strstr(buf, w))
                y |= (uint8_t)(1u << k);
        }
        for (o = 0; o < 4u; o++)
            CHECK(QUAD_ALGO[a].mod[o] == mod[o], "algo %u: %s's modulators %x, designer %x", a + 1, OP[o],
                  QUAD_ALGO[a].mod[o], mod[o]);
        CHECK(QUAD_ALGO[a].fb == fb && QUAD_ALGO[a].x == x && QUAD_ALGO[a].y == y, "algo %u: fb / X / Y", a + 1);
    }
}

/* ------------------------------------------------- against the model --- */
static char *slurp(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    long n;
    char *s;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s = malloc((size_t)n + 1u);
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        s = 0;
    }
    if (s)
        s[n] = 0;
    fclose(f);
    return s;
}
/* the numbers of the JSON array "key": up to max into out, returns how many */
static uint32_t jarr(const char *s, const char *key, double *out, uint32_t max)
{
    char k[32];
    const char *p;
    uint32_t n = 0;
    snprintf(k, sizeof k, "\"%s\":[", key);
    if (!(p = strstr(s, k)))
        return 0;
    p += strlen(k);
    while (*p && *p != ']' && n < max) {
        char *e;
        double v = strtod(p, &e);
        if (e == p)
            break;
        out[n++] = v;
        p = *e == ',' ? e + 1 : e;
    }
    return n;
}

static void t_golden(void)
{
    static const char *const G[12] = {"algo1", "algo2", "algo3", "algo4", "algo5", "algo6", "algo7", "algo8", "ep",
                                      "bass", "glass_pad", "wobble"};
    static const char *const GP[4] = {"EP", "BASS", "GLASS PAD", "WOBBLE"};
    static double first[2048], rms[64], pv[QP_NP];
    static int32_t buf[13230 + CTL];
    uint32_t g, i, k;
    for (g = 0; g < 12u; g++) {
        char fn[128], *s;
        track_t *t = &trk[0];
        int8_t p[QP_NP];
        uint32_t nf, nr, np, nt = (uint32_t)(0.3 * FS / CTL), rel = (uint32_t)(0.2 * FS / CTL), n = nt * CTL;
        double se = 0, sr = 0, worst = 0, snr;
        snprintf(fn, sizeof fn, "tests/quad_goldens/%s.json", G[g]);
        s = slurp(fn);
        CHECK(s != 0, "%s: missing (python3 tests/quad_ref.py)", fn);
        if (!s)
            continue;
        np = jarr(s, "patch", pv, QP_NP);
        nf = jarr(s, "first", first, 2048);
        nr = jarr(s, "rms", rms, 64);
        free(s);
        CHECK(np == QP_NP && nf == 2048u && nr == 29u, "%s: patch %u, first %u, rms %u", fn, np, nf, nr);
        if (np != QP_NP || nf != 2048u || !nr)
            continue;
        for (i = 0; i < QP_NP; i++)
            p[i] = (int8_t)pv[i];
        if (g >= 8u) {                           /* a preset: the model's copy is the firmware's */
            int8_t q[QP_NP];
            for (k = 0; k < QUAD_NPRESETS && strcmp(QUAD_PRESETS[k].name, GP[g - 8u]); k++)
                ;
            quad_preset_patch(k, q);
            CHECK(k < QUAD_NPRESETS && !memcmp(p, q, QP_NP), "%s: quad_ref.py's patch is not eng_quad.c's", GP[g - 8u]);
        }
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 100);
        drv_on(t, 1, 67, 100);
        for (k = 0; k < nt; k++) {
            if (k == rel)
                t->v[0].gate = t->v[1].gate = 0;
            drv_tick(t, buf + k * CTL);
        }
        for (i = 0; i < 2048u; i++) {
            se += (buf[i] - first[i]) * (buf[i] - first[i]);
            sr += first[i] * first[i];
        }
        snr = 10.0 * log10(sr / (se + 1e-9));
        for (k = 0; k < nr; k++) {
            double a = 0;
            for (i = 0; i < 441u; i++)
                a += (double)buf[k * 441u + i] * buf[k * 441u + i];
            a = sqrt(a / 441.0);
            if (rms[k] > 30.0 && fabs(db(a, rms[k])) > worst)
                worst = fabs(db(a, rms[k]));
        }
        if (getenv("VERBOSE"))
            printf("  %-10s SNR %5.1f dB, block RMS within %.2f dB\n", G[g], snr, worst);
        CHECK(snr > 36.0, "%s: SNR %.1f dB against the model", G[g], snr);
        CHECK(worst < 1.5, "%s: a block's RMS %.2f dB off the model", G[g], worst);
        (void)n;
    }
}

/* ------------------------------------------------------- envelopes --- */
static void t_env(void)
{
    quad_voice_t s;
    uint32_t n;
    int32_t want;
    /* ATK 64: linear to the top in TIME_MS_X10[64] */
    memset(&s, 0, sizeof s);
    s.stage[0] = 2;
    for (n = 0; s.stage[0] == 2 && n < 100000u; n++)
        quad_env_tick(&s, 0, 64, 60, 20 << 17, 0, 0, 1, 0);
    CHECK(fabs(tms(n) - TIME_MS_X10[64] / 10.0) < 2.0, "A ATK 64: %.1f ms, want %.1f", tms(n), TIME_MS_X10[64] / 10.0);
    /* DEC 70 to END 20: 99 % of the way in TIME_MS_X10[70] */
    want = 20 << 17;
    for (n = 0; abs(s.env[0] - want) > ((1 << 24) - want) / 100 && n < 100000u; n++)
        quad_env_tick(&s, 0, 64, 70, want, 0, 0, 1, 0);
    CHECK(fabs(tms(n) / (TIME_MS_X10[70] / 10.0) - 1.0) < 0.1, "A DEC 70: %.1f ms to 99 %%, want %.1f", tms(n),
          TIME_MS_X10[70] / 10.0);
    for (n = 0; n < 20000u; n++)                 /* held at END, gate or not */
        quad_env_tick(&s, 0, 64, 70, want, 0, 0, 1, 0);
    CHECK(s.stage[0] == 3 && abs(s.env[0] - want) < (1 << 12), "END held: %d, want %d", s.env[0], want);
    /* DELAY 50, then the attack */
    memset(&s, 0, sizeof s);
    s.stage[1] = 1;
    for (n = 0; s.stage[1] == 1 && n < 100000u; n++)
        quad_env_tick(&s, 1, 0, 60, 0, 0, 50, 1, 0);
    CHECK(fabs(tms(n) - TIME_MS_X10[50] / 10.0) < 2.0 && s.env[1] == 0, "B DLY 50: %.1f ms, want %.1f", tms(n),
          TIME_MS_X10[50] / 10.0);
    /* the amp: sustain, release, off */
    memset(&s, 0, sizeof s);
    s.stage[3] = 2;
    for (n = 0; n < 3u * FS / CTL; n++)
        quad_env_tick(&s, 3, 20, 60, 90 << 17, 70, 0, 1, 1);
    CHECK(s.stage[3] == 3 && abs(s.env[3] - (90 << 17)) < (1 << 16), "amp sustain %d", s.env[3]);
    for (n = 0; s.stage[3] && n < 100000u; n++)
        quad_env_tick(&s, 3, 20, 60, 90 << 17, 70, 0, 0, 1);
    CHECK(!s.stage[3] && !s.env[3] && tms(n) < 2.0 * TIME_MS_X10[70] / 10.0, "amp REL 70 ended after %.1f ms (to 1/4096)", tms(n));
    /* in a voice: LEV reached after the attack (velocity 127), TRIG and RESET on a retrigger */
    {
        track_t *t = &trk[0];
        int32_t out[CTL], lvl, e0;
        int8_t p[QP_NP];
        quad_init_patch(p);
        p[QP_AATK] = 30, p[QP_ADEC] = 100, p[QP_AEND] = 127, p[QP_ALEV] = 100;
        p[QP_BATK] = 30, p[QP_BDEC] = 90, p[QP_BEND] = 30, p[QP_BLEV] = 90;
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 127);
        for (n = 0; n < (uint32_t)(TIME_MS_X10[30] / 10.0 * FS / 1000 / CTL) + 4u; n++)
            drv_tick(t, out);
        lvl = 100 * 100 * 2;
        CHECK(abs(quad_vs[0][0].lv[0] - lvl) < lvl / 100, "A LEV 100: level %d, want %d", quad_vs[0][0].lv[0], lvl);
        for (n = 0; n < 20u; n++)
            drv_tick(t, out);
        e0 = quad_vs[0][0].env[1];
        quad_patch[0][QP_BTRIG] = 0;             /* TRIG off: a retrigger holds B */
        drv_on(t, 0, 62, 127);
        CHECK(quad_vs[0][0].env[1] == e0 && quad_vs[0][0].stage[1] == 3, "B TRIG off: restarted");
        quad_patch[0][QP_BTRIG] = 1;
        quad_patch[0][QP_BRST] = 0;              /* TRIG on, RESET off: the attack from where it is */
        drv_on(t, 0, 64, 127);
        CHECK(quad_vs[0][0].env[1] == e0 && quad_vs[0][0].stage[1] == 2, "B RESET off: env %d stage %u",
              quad_vs[0][0].env[1], quad_vs[0][0].stage[1]);
        quad_patch[0][QP_BRST] = 1;              /* RESET on: from 0 */
        drv_on(t, 0, 65, 127);
        CHECK(quad_vs[0][0].env[1] == 0 && quad_vs[0][0].stage[1] == 2, "B RESET on: env %d", quad_vs[0][0].env[1]);
        quad_patch[0][QP_PHRST] = 0;             /* PHASE RESET off: the phases run on */
        drv_tick(t, out);
        e0 = (int32_t)quad_vs[0][0].ph[0];
        drv_on(t, 0, 67, 127);
        CHECK((int32_t)quad_vs[0][0].ph[0] == e0 && e0, "PHASE RESET off: phase reset");
        quad_patch[0][QP_PHRST] = 1;
        drv_on(t, 0, 69, 127);
        CHECK(!quad_vs[0][0].ph[0] && !quad_vs[0][0].ph[2], "PHASE RESET on: phases %u", quad_vs[0][0].ph[0]);
        quad_patch[0][QP_ATRIG] = 1;             /* legato with TRIG on: A restarts */
        for (n = 0; n < 200u; n++)
            drv_tick(t, out);
        quad_legato(t, &t->v[0]);
        CHECK(quad_vs[0][0].stage[0] == 2 && !quad_vs[0][0].env[0], "legato, A TRIG on: not restarted");
        quad_patch[0][QP_BTRIG] = 0;
        for (n = 0; n < 200u; n++)
            drv_tick(t, out);
        quad_legato(t, &t->v[0]);
        CHECK(quad_vs[0][0].stage[1] == 3, "legato, B TRIG off: restarted");
    }
}

/* ------------------------------------------------------------ filters --- */
/* the RMS of the last 0.2 s of 0.4 s of one held note (a sine on C: X only, A and B silent) with these edits */
static double tone_rms(uint32_t note, const uint8_t *ed)
{
    track_t *t = &trk[0];
    int8_t p[QP_NP];
    int32_t out[CTL];
    uint32_t n, i;
    double a = 0;
    quad_init_patch(p);
    p[QP_ALGO] = 8, p[QP_MIX] = -63, p[QP_ALEV] = 0, p[QP_BLEV] = 0;
    for (; ed && *ed != 0xFFu; ed += 2)
        p[ed[0]] = (int8_t)ed[1];
    drv_reset(t);
    drv_patch(t, p);
    drv_on(t, 0, note, 127);
    for (n = 0; n < (uint32_t)(0.4 * FS / CTL); n++) {
        drv_tick(t, out);
        if (n >= (uint32_t)(0.2 * FS / CTL))
            for (i = 0; i < CTL; i++)
                a += (double)out[i] * out[i];
    }
    return sqrt(a / ((uint32_t)(0.2 * FS / CTL) * CTL));
}

static void t_filters(void)
{
    double r0 = tone_rms(72, 0), r;          /* 523 Hz, the filter open */
    static const uint8_t LP_LO[] = {QP_FREQ, 30, 0xFF}, LP_HI[] = {QP_FREQ, 110, 0xFF};
    static const uint8_t HP_LO[] = {QP_FTYPE, QF_HP, QP_FREQ, 30, 0xFF}, HP_HI[] = {QP_FTYPE, QF_HP, QP_FREQ, 110, 0xFF};
    static const uint8_t BP_AT[] = {QP_FTYPE, QF_BP, QP_FREQ, 58, 0xFF}, BP_HI[] = {QP_FTYPE, QF_BP, QP_FREQ, 110, 0xFF};
    static const uint8_t BW_HP[] = {QP_BASE, 90, QP_WIDTH, 37, 0xFF}, BW_LP[] = {QP_BASE, 1, QP_WIDTH, 20, 0xFF};
    static const uint8_t BW_IN[] = {QP_BASE, 20, QP_WIDTH, 80, 0xFF};
    CHECK(r0 > 1000.0, "the open tone: rms %.0f", r0);
    r = tone_rms(72, LP_LO);
    CHECK(db(r, r0) < -12.0, "LP at 132 Hz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, LP_HI);
    CHECK(fabs(db(r, r0)) < 1.0, "LP at 7 kHz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, HP_HI);
    CHECK(db(r, r0) < -12.0, "HP at 7 kHz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, HP_LO);
    CHECK(fabs(db(r, r0)) < 1.0, "HP at 132 Hz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BP_AT);
    CHECK(fabs(db(r, r0)) < 3.0, "BP at 523 Hz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BP_HI);
    CHECK(db(r, r0) < -12.0, "BP at 7 kHz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BW_HP);
    CHECK(db(r, r0) < -9.0, "base-width: BASE 2.5 kHz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BW_LP);
    CHECK(db(r, r0) < -9.0, "base-width: BASE + WIDTH 85 Hz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BW_IN);
    CHECK(fabs(db(r, r0)) < 1.5, "base-width: 523 Hz inside 68 Hz .. 4 kHz: %.1f dB", db(r, r0));
    {   /* HARM: + adds odd harmonics (the 2nd stays out), - all of them; the level stays near the sine's */
        static const uint8_t HP26[] = {QP_HARM, 26, 0xFF}, HM26[] = {QP_HARM, (uint8_t)-26, 0xFF};
        double rp = tone_rms(72, HP26), rm = tone_rms(72, HM26);
        CHECK(fabs(db(rp, r0)) < 4.0 && fabs(db(rm, r0)) < 4.0, "HARM +-26: %.1f / %.1f dB", db(rp, r0), db(rm, r0));
    }
}

/* ---------------------------------------------------------------- LFOs --- */
static void t_lfo(void)
{
    static const int8_t SP[6] = {16, 32, 24, 64, -40, 1};
    static const uint8_t MU[6] = {3, 4, 4, 8, 6, 11};
    track_t *t = &trk[0];
    uint32_t k, n;
    for (k = 0; k < 6u; k++) {                  /* FREE: the part's phase over 20 s */
        double want = SP[k] * (double)(1u << MU[k]) / 320.0, cyc = 0;
        uint32_t nt = (uint32_t)(20.0 * FS / CTL);
        drv_reset(t);
        quad_blob_set(t, 0);
        quad_patch[0][QP_LFO(0, QL_SPEED)] = SP[k];
        quad_patch[0][QP_LFO(0, QL_MULT)] = (int8_t)MU[k];
        for (n = 0; n < nt; n++) {
            uint32_t old = quad_lfo[0].ph[0];
            quad_block(t);
            cyc += (int32_t)(quad_lfo[0].ph[0] - old) / 4294967296.0;
        }
        cyc /= 20.0;
        CHECK(fabs(cyc / want - 1.0) < 0.02, "LFO SPEED %d x%u: %.4f Hz, want %.4f", SP[k], 1u << MU[k], cyc, want);
    }
    drv_reset(t);                                /* SPEED 0 stops */
    quad_blob_set(t, 0);
    quad_patch[0][QP_LFO(1, QL_SPEED)] = 0;
    quad_lfo[0].ph[1] = 12345u;
    for (n = 0; n < 100u; n++)
        quad_block(t);
    CHECK(quad_lfo[0].ph[1] == 12345u, "SPEED 0 moved");
    /* the voice's own phase: TRIG starts at PHASE and runs (backwards when negative); ONE and HALF stop */
    {
        int32_t out[CTL];
        uint32_t ph0;
        drv_reset(t);
        quad_blob_set(t, 0);
        quad_patch[0][QP_LFO(0, QL_TRIG)] = QT_TRIG;
        quad_patch[0][QP_LFO(0, QL_PHASE)] = 32;
        quad_patch[0][QP_LFO(0, QL_SPEED)] = -20;
        quad_patch[0][QP_LFO(0, QL_MULT)] = 5;
        quad_patch[0][QP_LFO(1, QL_TRIG)] = QT_ONE;
        quad_patch[0][QP_LFO(1, QL_PHASE)] = 64;
        quad_patch[0][QP_LFO(1, QL_MULT)] = 6;
        quad_patch[0][QP_LFO(2, QL_TRIG)] = QT_HALF;
        quad_patch[0][QP_LFO(2, QL_PHASE)] = 10;
        quad_patch[0][QP_LFO(2, QL_MULT)] = 6;
        drv_on(t, 0, 60, 100);
        ph0 = quad_vs[0][0].lph[0];
        CHECK(ph0 == 32u << 25 && quad_vs[0][0].lph[1] == 64u << 25, "start phase %u", ph0);
        {
            double cyc = 0;
            uint32_t nt = (uint32_t)(4.0 * FS / CTL);
            for (n = 0; n < nt; n++) {
                uint32_t old = quad_vs[0][0].lph[0];
                drv_tick(t, out);
                cyc += (int32_t)(quad_vs[0][0].lph[0] - old) / 4294967296.0;
            }
            CHECK(cyc < 0 && fabs(-cyc / 4.0 / (20.0 * 32 / 320.0) - 1.0) < 0.02, "TRIG -20 x32: %.3f Hz", cyc / 4.0);
        }
        CHECK(quad_vs[0][0].lstop == 6u, "ONE / HALF did not stop: %x", quad_vs[0][0].lstop);
        CHECK(quad_vs[0][0].lph[1] == 64u << 25, "ONE: stopped at %u, want the start", quad_vs[0][0].lph[1]);
        CHECK(quad_vs[0][0].lph[2] == (10u << 25) + 0x80000000u, "HALF: stopped at %u", quad_vs[0][0].lph[2]);
    }
    /* the waves */
    CHECK(abs(quad_lfo_wave(QW_TRI, 0, 0)) < 2 && quad_lfo_wave(QW_TRI, 0x40000000u, 0) > 32700 &&
              quad_lfo_wave(QW_TRI, 0xC0000000u, 0) < -32700, "TRI");
    CHECK(abs(quad_lfo_wave(QW_SINE, 0, 0)) < 2 && quad_lfo_wave(QW_SINE, 0x40000000u, 0) > 32700, "SINE");
    CHECK(quad_lfo_wave(QW_SQR, 1u, 0) == 32767 && quad_lfo_wave(QW_SQR, 0x80000001u, 0) == -32767, "SQR");
    CHECK(abs(quad_lfo_wave(QW_SAW, 0, 0)) < 2 && quad_lfo_wave(QW_SAW, 0x7FFFFFFFu, 0) > 32700, "SAW");
    CHECK(quad_lfo_wave(QW_RAMP, 0, 0) == 32767 && quad_lfo_wave(QW_RAMP, 0xFFFFFFFFu, 0) < 2, "RAMP");
    CHECK(quad_lfo_wave(QW_EXP, 0, 0) > 32700 && quad_lfo_wave(QW_EXP, 0x80000000u, 0) < 2100, "EXP");
    CHECK(quad_lfo_wave(QW_RAND, 0, -1234) == -1234, "RAND");
}

/* ------------------------------------------------------------- renders --- */
/* secs of a chord (6 notes, or one for a mono preset) on patch p; the part's peak at LEVEL 92, the largest |sum| */
static void chord(const int8_t *p, uint32_t mono, double secs, const uint8_t *notes, int32_t *peak, int64_t *maxabs,
                  uint32_t *left)
{
    static const uint8_t CH[6] = {62, 66, 69, 71, 73, 76};
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t n, i, nn = mono ? 1u : 6u;
    if (!notes)
        notes = CH;
    drv_reset(t);
    drv_patch(t, p);
    *peak = 0;
    *maxabs = 0;
    for (i = 0; i < nn; i++)
        drv_on(t, i, mono ? 38u : notes[i], 100);
    for (n = 0; n < (uint32_t)(secs * FS / CTL); n++) {
        if (n == (uint32_t)(secs * 0.7 * FS / CTL))
            for (i = 0; i < nn; i++)
                t->v[i].gate = 0;
        drv_tick(t, out);
        for (i = 0; i < CTL; i++) {
            int64_t a = llabs((int64_t)out[i]);
            int32_t x = ((out[i] >> 2) * LEVEL_Q12[92]) >> 10;
            x = x < 0 ? -x : x;
            *maxabs = a > *maxabs ? a : *maxabs;
            *peak = x > *peak ? x : *peak;
        }
    }
    *left = 0;
    for (n = 0; n < 20u * FS / CTL && drv_tick(t, out); n++)
        ;
    for (i = 0; i < NVOICE; i++)
        *left += t->v[i].active;
}

static void t_render(void)
{
    uint32_t k, left;
    int32_t peak;
    int64_t mx;
    for (k = 0; k < QUAD_NPRESETS; k++) {
        int8_t p[QP_NP];
        quad_preset_patch(k, p);
        chord(p, QUAD_PRESETS[k].mono, 1.4, 0, &peak, &mx, &left);
        if (getenv("VERBOSE"))
            printf("  %-12s peak %5.1f %% FS (part at LEVEL 92)\n", QUAD_PRESETS[k].name, peak * 100.0 / 32768);
        CHECK(mx < (1 << 30), "%s: |sum| %lld near the int32 wrap", QUAD_PRESETS[k].name, (long long)mx);
        CHECK(peak < 0.9 * 32768, "%s: peak %.1f %% FS", QUAD_PRESETS[k].name, peak * 100.0 / 32768);
        CHECK(peak > 300, "%s: silent (peak %d)", QUAD_PRESETS[k].name, peak);
        CHECK(!left, "%s: %u voices still active 20 s after the release", QUAD_PRESETS[k].name, left);
    }
    /* the extremes: every algorithm at full feedback, full levels, HARM +-26, MIX at both ends and the middle, the
     * LFOs at full depth onto feedback / levels / HARM, full resonance, every filter type, key tracks, top and bottom
     * notes: no int32 wrap (-fsanitize=signed-integer-overflow aborts on one), the voices end */
    {
        static const uint8_t NOTES[6] = {12, 36, 60, 96, 115, 127};
        uint32_t a, h, mi, f;
        for (a = 1; a <= 8u; a++)
            for (h = 0; h < 2u; h++)
                for (mi = 0; mi < 3u; mi++)
                    for (f = 0; f < 3u; f++) {
                        int8_t p[QP_NP];
                        quad_init_patch(p);
                        p[QP_ALGO] = (int8_t)a;
                        p[QP_RC] = 18, p[QP_RA] = (int8_t)(h ? 63 : 0), p[QP_RB] = (int8_t)(QUAD_NRB - 1 - h * 40);
                        p[QP_HARM] = (int8_t)(h ? 26 : -26), p[QP_FDBK] = 127, p[QP_DTUN] = 127;
                        p[QP_MIX] = (int8_t)(mi == 0 ? -63 : mi == 1 ? 63 : 0);
                        p[QP_OFSC] = 100, p[QP_OFSB2] = -100;
                        p[QP_ALEV] = p[QP_BLEV] = p[QP_AEND] = p[QP_BEND] = 127;
                        p[QP_AKTRK] = p[QP_BKTRK] = 127, p[QP_VEL] = 0;
                        p[QP_FTYPE] = (int8_t)f, p[QP_RESO] = 127, p[QP_FREQ] = (int8_t)(f == 1 ? 0 : 90);
                        p[QP_FDEPTH] = (int8_t)(h ? 63 : -64), p[QP_FKTRK] = 127, p[QP_FSUS] = 127;
                        p[QP_BASE] = (int8_t)(f * 20), p[QP_WIDTH] = 60, p[QP_LEVEL] = 127, p[QP_ESUS] = 127;
                        p[QP_LFO(0, QL_DEST)] = QD_FDBK, p[QP_LFO(1, QL_DEST)] = QD_ALEV;
                        p[QP_LFO(2, QL_DEST)] = (int8_t)(h ? QD_BLEV : QD_HARM);
                        for (k = 0; k < 3u; k++) {
                            p[QP_LFO(k, QL_DEPTH)] = (int8_t)(h ? 63 : -64);
                            p[QP_LFO(k, QL_WAVE)] = QW_SQR;
                            p[QP_LFO(k, QL_SPEED)] = 64, p[QP_LFO(k, QL_MULT)] = 11;
                        }
                        chord(p, 0, 0.3, NOTES, &peak, &mx, &left);
                        CHECK(mx < (1 << 30), "extremes a%u h%u m%u f%u: |sum| %lld", a, h, mi, f, (long long)mx);
                        CHECK(!left, "extremes a%u h%u m%u f%u: voices left", a, h, mi, f);
                    }
    }
}

/* ----------------------------------------------------------------- CPU --- */
#ifdef __APPLE__
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
/* host instructions a sample of 8 voices held (an 8-note chord, 1 s after 0.2 s), the part's block included */
static double cpu_quad(const int8_t *p)
{
    static const uint8_t CH[8] = {50, 57, 62, 66, 69, 71, 73, 76};
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t i, n;
    uint64_t i0;
    drv_reset(t);
    drv_patch(t, p);
    for (i = 0; i < 8u; i++)
        drv_on(t, i, CH[i], 100);
    for (n = 0; n < (uint32_t)(0.2 * FS / CTL); n++)
        drv_tick(t, out);
    i0 = instr_now();
    for (n = 0; n < (uint32_t)(1.0 * FS / CTL); n++)
        drv_tick(t, out);
    return (double)(instr_now() - i0) / (n * CTL);
}
static double cpu_va(uint32_t k)
{
    static const uint8_t CH[8] = {50, 57, 62, 66, 69, 71, 73, 76};
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t i, n, j;
    uint64_t i0 = 0;
    memset(t, 0, sizeof *t);
    memset(va_vs, 0, sizeof va_vs);
    memset(va_lfo, 0, sizeof va_lfo);
    t->p[P_VOICE] = V_POLY;
    va_blob_preset(t, k);
    for (i = 0; i < 8u; i++) {
        voice_t *v = &t->v[i];
        v->note = CH[i], v->vel = v->mvel = 100, v->gate = v->active = 1, v->age = i + 1u;
        v->pitch16 = v->pitch_cur = CH[i] * 16;
        v->env = v->env_out = 0;
        va_note_on(t, v);
    }
    for (n = 0; n < (uint32_t)(1.2 * FS / CTL); n++) {
        if (n == (uint32_t)(0.2 * FS / CTL))
            i0 = instr_now();
        memset(out, 0, sizeof out);
        va_block(t);
        for (j = 0; j < 8u; j++) {
            voice_t *v = &t->v[j];
            vmod_t m;
            if (!v->active || va_done(t, v))
                continue;
            v->env = 1 << 24;
            memset(&m, 0, sizeof m);
            m.amp0 = v->env_out;
            m.amp1 = v->env_out = 32767;
            m.pitch16 = v->pitch_cur;
            m.inc = pitch_inc((uint32_t)m.pitch16);
            m.shape = 64 << 8;
            va_render_mono(t, v, out, CTL, &m);
        }
    }
    return (double)(instr_now() - i0) / ((uint32_t)(1.0 * FS / CTL) * CTL);
}
/* a half's device estimate: host instructions a sample x 128 / 259 us of the 2902 us half (docs/INTEGRATION.md) */
static double dev_pct(double ips) { return ips * HALF_FRAMES / 259.0 / 2902.0 * 100.0; }
static void t_cpu(void)
{
    uint32_t k, worst = 0;
    double sum = 0, mxc = 0, va = cpu_va(0);
    printf("  CPU, 8 voices held, host instructions a sample -> device %% of the 2.9 ms half:\n");
    printf("    VA LUSH PAD (same driver) %6.0f  %5.1f %%\n", va, dev_pct(va));
    for (k = 0; k < QUAD_NPRESETS; k++) {
        int8_t p[QP_NP];
        double c;
        quad_preset_patch(k, p);
        c = cpu_quad(p);
        printf("    QUAD %-12s %6.0f  %5.1f %%\n", QUAD_PRESETS[k].name, c, dev_pct(c));
        sum += c;
        if (c > mxc) {
            mxc = c;
            worst = k;
        }
    }
    printf("    QUAD average %6.0f (%.1f %%), worst %s %6.0f (%.1f %%); VA LUSH PAD x %.2f\n", sum / QUAD_NPRESETS,
           dev_pct(sum / QUAD_NPRESETS), QUAD_PRESETS[worst].name, mxc, dev_pct(mxc), sum / QUAD_NPRESETS / va);
    {   /* the heaviest a patch gets: algorithm 8 (four HARM carriers between two tables), the SVF, both base-width
         * one-poles, three LFOs */
        int8_t p[QP_NP];
        double c;
        quad_preset_patch(11, p);
        p[QP_HARM] = -11, p[QP_FTYPE] = QF_BP, p[QP_RESO] = 40, p[QP_BASE] = 10, p[QP_WIDTH] = 100;
        for (k = 0; k < 3u; k++)
            p[QP_LFO(k, QL_DEST)] = (int8_t)(QD_HARM + k), p[QP_LFO(k, QL_DEPTH)] = 20, p[QP_LFO(k, QL_TRIG)] = QT_TRIG;
        c = cpu_quad(p);
        printf("    QUAD worst case (algo 8, HARM, BP, base-width, 3 LFOs) %6.0f  %5.1f %%\n", c, dev_pct(c));
    }
}
#else
static void t_cpu(void) { printf("  CPU: macOS only (proc_pid_rusage)\n"); }
#endif

int main(int argc, char **argv)
{
    t_blob();
    t_pages();
    t_set_macros();
    t_presets();
    t_ratios();
    t_algo_table();
    t_golden();
    t_env();
    t_filters();
    t_lfo();
    t_render();
    if (argc > 1 && !strcmp(argv[1], "cpu"))
        t_cpu();
    printf("cr_quad_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
