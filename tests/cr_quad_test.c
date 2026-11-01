/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the QUAD engine (firmware/src/eng_quad.c, docs/QUAD.md), no HAL: the engine on core.h and dsp.c with a
 * minimal voice driver (voice.c's control tick for an ownenv engine: done, then render), as tests/cr_va_test.c.
 *   sh tests/run_cr_tests.sh          (built with -fsanitize=signed-integer-overflow: an int32 wrap aborts)
 *   build/host/cr_quad_test cpu       (also the CPU figure against the VA's, same driver; macOS)
 * Checks: the blob round trip of 2000 random patches; bad blobs -> the init patch; the page table against the ranges
 * and sections; set() clamps; the macros both ways; every preset's blob and macros; the ratio tables (C / B steps, A
 * steps, the B pair sequence and its names); RATIO B's walk (0..360 one pair a step: B1 first, the carry into B2, the
 * ends, the texts); version-1 blobs and macros (RATIO B = BR x 19 + B1) converted; the routing table against the designer's ALGOS; every algorithm and
 * four presets against tests/quad_ref.py's renders (tests/quad_goldens: SNR over the first 2048 samples > 36 dB AND
 * every 10 ms block's RMS within 1.5 dB over 0.3 s); the envelopes' times against quad_times.h's tables (the Digitone's:
 * each envelope's ATK, DEC to END, DELAY = v / 32 beats at 120 and 80 BPM, the amp's release; tools/digitone/
 * test_quad_times.py checks the tables against the Digitone's readings), LEV, TRIG, RESET; REL INF holds, 126 decays, a
 * kill frees it); the filter types and the base-width window; the LFOs' rates (within 2 %), direction, start
 * phase, ONE / HALF, waves; no int32 wrap at full feedback / levels / LFOs on every algorithm; voices end after the
 * release; a 6-note chord for 1.4 s on every preset: peak < 0.9 FS at LEVEL 92; no DC (every preset held 1 s, a plain
 * feedback operator at FDBK 0..127: the mean of 0.3..1 s under 2 % / 1 % of the RMS; regress's limit is 1.2 % FS). */
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
/* Version 4 (2026-10-14): the hi-res values in hundredths. This test's hand-made patches stay in version 3's units
 * (whole numbers, FDBK 0..120: quad_range_v3), the engine gets them converted (quad_v4_of_v3, as an old blob's are):
 * quad_init_patch / quad_preset_patch below are version 3's; the engine's own are quad_init_patch4 / quad_preset_patch4,
 * drv_patch4 loads a version-4 patch as it is */
static void (*const quad_init_patch4)(int16_t *) = quad_init_patch;
static void (*const quad_preset_patch4)(uint32_t, int16_t *) = quad_preset_patch;
static void t_init3(int16_t *p)
{
    uint32_t i;
    for (i = 0; i < QP_NP; i++)
        p[i] = quad_range_v3(i).def;
}
static void t_preset3(uint32_t k, int16_t *p)
{
    const uint8_t *e;
    t_init3(p);
    for (e = QUAD_PRESET_EDITS[k]; *e != 0xFFu; e += 2) {
        quad_rng_t r = quad_range_v3(e[0]);
        p[e[0]] = (int16_t)clamp((int8_t)e[1], r.min, r.max);
    }
}
#define quad_init_patch t_init3
#define quad_preset_patch t_preset3
/* a version-3 value -> the engine's (version 5: through version 4's, as an old blob's) */
static int32_t v5_of_v3(uint32_t i, int32_t v) { return quad_clampv(i, quad_v5_of_v4(i, quad_v4_of_v3(i, v))); }
static void to_v4(const int16_t *p, int16_t *q)
{
    uint32_t i;
    for (i = 0; i < QP_NP; i++)
        q[i] = (int16_t)v5_of_v3(i, p[i]);
}
static void drv_patch4(track_t *t, const int16_t *p)
{
    memcpy(quad_patch[quad_tr(t)], p, QP_NP * sizeof(int16_t));
    quad_macros_out(t);
}
static void drv_patch(track_t *t, const int16_t *p)
{
    int16_t q[QP_NP];
    to_v4(p, q);
    drv_patch4(t, q);
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

static void rnd_patch(int16_t *p)
{
    uint32_t i;
    for (i = 0; i < QP_NP; i++) {
        quad_rng_t r = quad_range(i);
        p[i] = (int16_t)(r.min + (int32_t)(rnd() % (uint32_t)(r.max - r.min + 1)));
    }
}

/* a random version-2 patch (version 2's ranges and meanings) and its blob of version ver (1: RATIO B one value) */
static void rnd_patch_v2(int16_t *p)
{
    uint32_t i;
    quad_init_patch(p);
    for (i = 0; i < QUAD_NP_V2; i++) {
        quad_rng_t r = quad_range_v2(i);
        p[i] = (int16_t)(r.min + (int32_t)(rnd() % (uint32_t)(r.max - r.min + 1)));
    }
}
static void pack_v2(const int16_t *p, uint8_t *b, uint32_t ver)
{
    uint32_t i, np = ver == 1u ? QUAD_NP_V1 : QUAD_NP_V2;
    memset(b, 0, QUAD_BLOB);
    b[0] = QUAD_MAGIC;
    b[1] = (uint8_t)ver;
    for (i = 0; i < np; i++)
        b[2 + i] = (uint8_t)(p[i] - quad_range_v2(i).min);
}

/* a version-3 blob of a version-3 patch (as the 0.22 betas wrote it: a byte a value) */
static void pack_v3(const int16_t *p, uint8_t *b)
{
    uint32_t i;
    memset(b, 0, QUAD_BLOB);
    b[0] = QUAD_MAGIC;
    b[1] = 3u;
    for (i = 0; i < QP_NP5; i++)
        b[2 + i] = (uint8_t)(p[i] - quad_range_v3(i).min);
}
/* the byte offset of value i in a version-4 blob */
static uint32_t v4_off(uint32_t i)
{
    uint32_t k, o = 2;
    for (k = 0; k < i; k++)
        o += quad_hires(k) ? 2u : 1u;
    return o;
}
/* the blob check of the firmware before version 4 (QUAD_VER 3): its first test (the version) */
static int old_fw_takes(const uint8_t *b) { return b && b[0] == QUAD_MAGIC && b[1] >= 1u && b[1] <= 3u; }

/* ------------------------------------------------------------- checks --- */
static void t_blob(void)
{
    uint8_t b[QUAD_BLOB], b2[QUAD_BLOB];
    int16_t p[QP_NP], q[QP_NP], init[QP_NP];
    uint32_t n, i, nh = 0, w;
    quad_init_patch4(init);
    for (i = 0; i < QP_NP; i++)
        nh += (uint32_t)quad_hires(i);
    CHECK(QUAD_DEEP.blob_size == QUAD_BLOB && QUAD_BLOB == 96u && QUAD_BLOB_V3 == 80u && QP_NP5 == 73 && QP_NP == 83 &&
              nh == QUAD_NHIRES && nh == 12u && QUAD_V5_LEN == 87u && v4_off(QP_NP5) == QUAD_V5_LEN && QUAD_VER == 6u &&
              QUAD_V6_LEN == 92u,
          "blob %u bytes, %d values, %u hi-res", QUAD_DEEP.blob_size, QP_NP, nh);
    for (n = 0; n < 2000u; n++) {
        rnd_patch(p);
        quad_pack(p, b);
        CHECK(quad_blob_ok(b), "random patch %u: its blob is not valid", n);
        CHECK(quad_unpack(b, q) == 1 && !memcmp(p, q, QP_NP * sizeof(int16_t)), "random patch %u: round trip differs", n);
        quad_pack(q, b2);
        CHECK(!memcmp(b, b2, QUAD_BLOB), "random patch %u: blob round trip differs", n);
        CHECK(b[0] == 'Q' && (b[1] == 5u || b[1] == 6u), "magic / version");
        for (i = b[1] == 6u ? QUAD_V6_LEN : QUAD_V5_LEN; i < QUAD_BLOB; i++)
            CHECK(!b[i], "padding byte %u not 0", i);
    }
    for (w = 0; w < 3u; w++) {                   /* every value at its min, its max, its default */
        quad_init_patch4(p);
        for (i = 0; i < QP_NP; i++) {
            quad_rng_t r = quad_range(i);
            p[i] = w == 0u ? r.min : w == 1u ? r.max : r.def;
        }
        quad_pack(p, b);
        CHECK(quad_blob_ok(b) && quad_unpack(b, q) == 1 && !memcmp(p, q, QP_NP * sizeof(int16_t)),
              "every value at its %s: round trip", w == 0u ? "min" : w == 1u ? "max" : "def");
        for (i = 0; i < QP_NP; i++)
            if (quad_hires(i)) {
                uint32_t o = v4_off(i), u = (uint32_t)(p[i] - quad_range(i).min);
                CHECK(b[o] == (uint8_t)u && b[o + 1u] == (uint8_t)(u >> 8), "value %u: u16le at %u", i, o);
            }
    }
    quad_pack(p, b);
    CHECK(!old_fw_takes(b) && old_fw_takes((const uint8_t *)"Q\003"), "an older firmware takes a version-4 blob");
    CHECK(quad_unpack(0, q) == 0 && !memcmp(q, init, QP_NP * sizeof(int16_t)), "NULL blob: not the init patch");
    memcpy(b2, b, QUAD_BLOB);
    b2[0] ^= 1;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP * sizeof(int16_t)), "bad magic: not the init patch");
    memcpy(b2, b, QUAD_BLOB);
    b2[1] = QUAD_VER + 1u;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP * sizeof(int16_t)), "bad version: not the init patch");
    quad_init_patch4(p);
    quad_pack(p, b);                             /* (version 5: the values 0..72) */
    for (i = 0; i < QP_NP5; i++) {               /* every value one past its range */
        quad_rng_t r = quad_range(i);
        uint32_t o = v4_off(i), u = (uint32_t)(r.max - r.min + 1);
        memcpy(b2, b, QUAD_BLOB);
        b2[o] = (uint8_t)u;
        if (quad_hires(i))
            b2[o + 1u] = (uint8_t)(u >> 8);
        CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP * sizeof(int16_t)), "value %u out of range: not the init patch", i);
    }
    memcpy(b2, b, QUAD_BLOB);
    b2[QUAD_BLOB - 1u] = 1;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, QP_NP * sizeof(int16_t)), "padding not 0: not the init patch");
    memset(b2, 0xFF, sizeof b2);
    CHECK(quad_unpack(b2, q) == 0, "erased flash (0xFF): taken as a patch");
    {   /* version 3 (the 0.22 betas): every value carried over, the hi-res x 100, FDBK 0..120 -> 0..127 */
        uint32_t bad = 0;
        for (n = 0; n < 500u; n++) {
            int16_t e[QP_NP];
            t_init3(p);
            for (i = 0; i < QP_NP5; i++) {
                quad_rng_t r = quad_range_v3(i);
                p[i] = (int16_t)(r.min + (int32_t)(rnd() % (uint32_t)(r.max - r.min + 1)));
            }
            pack_v3(p, b);
            to_v4(p, e);
            if (!quad_blob_ok(b) || quad_unpack(b, q) != 1 || memcmp(q, e, QP_NP * sizeof(int16_t)))
                bad++;
        }
        CHECK(!bad, "v3: %u random patches not converted", bad);
        t_init3(p);
        p[QP_HARM] = -19, p[QP_DTUN] = 127, p[QP_FDBK] = 120, p[QP_FREQ] = 42, p[QP_EREL] = 127, p[QP_LEVEL] = 100;
        p[QP_LFO(1, QL_SPEED)] = -64, p[QP_LFO(2, QL_DEPTH)] = 63, p[QP_FDEPTH] = -64, p[QP_MIX] = -64;
        pack_v3(p, b);
        CHECK(quad_unpack(b, q) == 1 && q[QP_HARM] == -1900 && q[QP_DTUN] == 12700 && q[QP_FDBK] == 127 &&
                  q[QP_FREQ] == 4200 && q[QP_EREL] == 127 && q[QP_LEVEL] == 100 && q[QP_LFO(1, QL_SPEED)] == -6400 &&
                  q[QP_LFO(2, QL_DEPTH)] == 6300 && q[QP_FDEPTH] == -6400 && q[QP_MIX] == -64,
              "v3: the values carried over (HARM %d FDBK %d)", q[QP_HARM], q[QP_FDBK]);
        b[2 + QP_FDBK] = 121;                    /* past version 3's FDBK */
        CHECK(!quad_blob_ok(b), "v3: FDBK 121 taken");
        pack_v3(p, b);
        b[QUAD_BLOB_V3 - 1u] = 1;
        CHECK(!quad_blob_ok(b), "v3: padding not 0 taken");
        pack_v3(p, b);
        b[QUAD_BLOB_V3] = 1;                     /* (beyond a version-3 blob: not its business) */
        CHECK(quad_blob_ok(b), "v3: a byte past its 80 refused");
    }
    {
        track_t *t = &trk[0];
        memset(t, 0, sizeof *t);
        b2[0] = 0;
        quad_blob_set(t, b2);
        CHECK(!memcmp(quad_patch[0], init, QP_NP * sizeof(int16_t)), "blob_set(bad): not the init patch");
        CHECK(t->p[P_E0] == 1 && t->p[P_E1] == 3 && t->p[P_E3] == 3 && quad_get(t, 0, 3) == QUAD_RB_DEF,
              "blob_set(bad): macros %d %d %d",
              t->p[P_E0], t->p[P_E1], t->p[P_E3]);
        quad_blob_set(t, 0);
        CHECK(!memcmp(quad_patch[0], init, QP_NP * sizeof(int16_t)), "blob_set(0): not the init patch");
        rnd_patch(p);
        quad_pack(p, b);
        quad_blob_set(t, b);
        quad_blob_get(t, b2);
        CHECK(!memcmp(b, b2, QUAD_BLOB), "blob_set / blob_get round trip");
        for (i = 0; i < 8u; i++)
            CHECK(t->p[P_E0 + i] == quad_mac_val(i, p[QUAD_MAC[i]]), "blob_set: macro %u %d, patch %d", i,
                  t->p[P_E0 + i], p[QUAD_MAC[i]]);
        quad_blob_get(&trk[2], b2);               /* a part without a patch: the init patch */
        CHECK(quad_unpack(b2, q) == 1 && !memcmp(q, init, QP_NP * sizeof(int16_t)), "part 3's blob: not the init patch");
    }
}

static uint32_t names_n(const char *const *n)
{
    uint32_t k = 0;
    while (n[k])
        k++;
    return k;
}
/* RATIO B's pair v as the editor shows it (cr_edit.c ce_pair: the column's QUAD_NRCB names over QUAD_NRCB^2 values,
 * names[v % 19] = B2 (the fast hand, first) over names[v / 19] = B1): "B2/B1" */
static const char *rb_txt(int32_t v)
{
    static char b[4][16];
    static uint32_t k;
    char *s = b[k++ & 3u];
    const char *const *n = QUAD_PAGES[0].col[3].names;
    v = clamp(v, 0, QUAD_NRB - 1);
    snprintf(s, 16, "%s/%s", n[v % QUAD_NRCB], n[v / QUAD_NRCB]);
    return s;
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
                CHECK(names_n(d->names) == (uint32_t)(d->max - d->min + 1) ||   /* (or a pair: k names over k x k) */
                          names_n(d->names) * names_n(d->names) == (uint32_t)(d->max - d->min + 1),
                      "page %u col %u: %u names for %d values",
                      pg, c, names_n(d->names), d->max - d->min + 1);
            if (ix >= QP_NP) {
                CHECK(ix == QUAD_XPAN || ix == QUAD_XDIST, "page %u col %u: index %u", pg, c, ix);
                continue;
            }
            used[ix]++;
            if (ix == QP_RB1) {                  /* RATIO B: the pair (B1 and B2, a C/B step each) */
                used[QP_RB2]++;
                CHECK(d->min == 0 && d->max == QUAD_NRB - 1 && QUAD_NRB == 361 && d->def == QUAD_RB_DEF &&
                          QUAD_RB_DEF == 3 * QUAD_NRCB + 3 && quad_range(QP_RB1).max == QUAD_NRCB - 1 &&
                          quad_range(QP_RB2).max == QUAD_NRCB - 1 && quad_range(QP_RB2).def == 3 && d->names == N_QUAD_RCB,
                      "RATIO B: the pair's column");
                continue;
            }
            {
                quad_rng_t r = quad_range(ix);
                CHECK(r.min == d->min && r.max == d->max && r.def == d->def, "value %u: range", ix);
            }
        }
    }
    for (i = 0; i < QP_NP; i++)
        CHECK(i < QP_NP5 ? used[i] == 1u : used[i] <= 1u, "patch value %u on %u page columns", i, used[i]);
    for (i = 0; i < 8u; i++) {                   /* the macros = SYN 1's eight, as ENG_QUAD.edit */
        const param_desc_t *d = &QUAD_PAGES[i >> 2].col[i & 3u], *e = &ENG_QUAD.edit[i];
        CHECK(QUAD_MAP[i >> 2][i & 3u] == i && (i == 3u ? QUAD_MAC[i] == QP_RB2 : QUAD_MAC[i] == i),
              "SYN 1 column %u is not macro %u", i, i);
        if (i == QP_RB1) {                       /* the macro: B2's step (the fast hand) */
            CHECK(!strcmp(e->label, d->label) && e->fmt == F_INT && e->min == 0 && e->max == QUAD_NRCB - 1 && e->def == 3 &&
                      e->names == N_QUAD_RCB, "macro 3: B2's step");
            continue;
        }
        if (i == QP_HARM || i == QP_DTUN) {      /* a hi-res value's macro: its coarse part (quad_mac_val) */
            CHECK(d->fmt == F_FIX2 && e->min == d->min / 100 && e->max == d->max / 100 && e->def == d->def / 100 &&
                      !strcmp(e->label, d->label) && e->fmt == (i == QP_HARM ? F_OFS : F_INT),
                  "macro %u: edit[] is not its page column's coarse part", i);
            continue;
        }
        CHECK(e->min == d->min && e->max == d->max && e->def == d->def && !strcmp(e->label, d->label) && e->fmt == d->fmt,
              "macro %u: edit[] differs from its page column", i);
    }
    CHECK(ENG_QUAD.knob[0] == P_E2 && ENG_QUAD.knob[1] == P_E4 && ENG_QUAD.knob[2] == P_E6 && ENG_QUAD.knob[3] == P_E7,
          "HOME knobs");
    CHECK(ENG_QUAD.ownenv && ENG_QUAD.done && ENG_QUAD.poly == 8 && !ENG_QUAD.render2 && ENG_QUAD.legato, "contract");
    /* the value texts */
    CHECK(!strcmp(N_QUAD_RCB[3], "1.00") && !strcmp(N_QUAD_RCB[18], "16.00") && !strcmp(N_QUAD_RA[7], "2.00") &&
              !strcmp(rb_txt(QUAD_RB_DEF), "1.00/1.00") && !strcmp(rb_txt(3 * QUAD_NRCB + 1), "0.50/1.00") &&
              !strcmp(N_QUAD_OFS[100], "+0.00") && !strcmp(N_QUAD_OFS[101], "+0.01") && !strcmp(N_QUAD_OFS[0], "-1.00"),
          "value names");
    CHECK(!strcmp(N_QUAD_MULT[4], "16") && !strcmp(N_QUAD_MULT[11], "2k") && !strcmp(N_QUAD_MULT[12], "F1") &&
              !strcmp(N_QUAD_MULT[23], "F2k") && QUAD_PAGES[1].col[0].fmt == F_FIX2 && QUAD_PAGES[0].col[0].fmt == F_INT,
          "Mult / Harm / Algo formats");
    {   /* the enums and ranges of the contract (docs/QUAD.md) */
        uint32_t k;
        for (k = 0; k < QD_N; k++)
            CHECK(strlen(N_QUAD_DEST[k]) <= 5u && N_QUAD_DEST[k][0], "DEST %u: \"%s\"", k, N_QUAD_DEST[k]);
        for (k = 0; k < QUAD_NMULT; k++)
            CHECK(strlen(N_QUAD_MULT[k]) <= 4u, "MULT %u", k);
        CHECK(QD_N == 58 && !strcmp(N_QUAD_DEST[QD_PITCH], "PITCH") && !strcmp(N_QUAD_DEST[QD_PAN], "PAN") &&
                  !strcmp(N_QUAD_FTYPE[0], "OFF") && !strcmp(N_QUAD_FTYPE[3], "LP24") && !strcmp(N_QUAD_PHRT[4], "A+B2") &&
                  quad_range(QP_FDBK).max == 127 && quad_range(QP_MIX).min == -64 && quad_range(QP_MIX).max == 63 &&
                  quad_range(QP_HARM).min == -2600 && quad_range(QP_LFO(0, QL_SPEED)).max == 6399 &&
                  quad_range(QP_LFO(1, QL_FADE)).min == -64 && quad_range(QP_LFO(2, QL_MULT)).max == 23 &&
                  quad_range(QP_PHRT).def == QR_ALL && quad_range(QP_FTYPE).def == QF_LP12 && quad_range(QP_B2KEY).max == 127,
              "the contract's enums and ranges");
        CHECK(QUAD_SCREENS[6].pg[0] == 11 && !strcmp(QUAD_SCREENS[6].label[0][2], "B2 Key"), "ENV 3: three key cells");
    }
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
    quad_set(t, 1, 0, -749);                     /* HARM -7.49: its macro the coarse -7 */
    CHECK(t->p[P_E2] == 9 && t->p[P_E4] == -7 && quad_patch[0][QP_RA] == 9 && quad_patch[0][QP_HARM] == -749,
          "set -> macros");
    quad_block(t);
    CHECK(quad_patch[0][QP_HARM] == -749, "block: the coarse macro does not round the fine value");
    t->p[P_E4] = 12;                             /* a macro move (a CC, the matrix): coarse 12, fine 0 */
    quad_block(t);
    CHECK(quad_patch[0][QP_HARM] == 1200, "macro -> HARM 12.00: %d", quad_patch[0][QP_HARM]);
    /* P_E -> patch in quad_block */
    t->p[P_E6] = 99;
    quad_set(t, 0, 3, 5 * QUAD_NRCB + 2);       /* RATIO B 0.75/3.00 */
    CHECK(quad_patch[0][QP_RB1] == 5 && quad_patch[0][QP_RB2] == 2 && t->p[P_E3] == 2, "set RATIO B 3.00 / 0.75");
    t->p[P_E3] = 120;                            /* beyond the macro's range (B2): clamped, B1 kept */
    quad_block(t);
    CHECK(quad_patch[0][QP_FDBK] == 99 && quad_patch[0][QP_RB2] == QUAD_NRCB - 1 && quad_patch[0][QP_RB1] == 5,
          "macros -> patch: %d %d %d", quad_patch[0][QP_FDBK], quad_patch[0][QP_RB1], quad_patch[0][QP_RB2]);
    CHECK(quad_get(t, 0, 3) == 5 * QUAD_NRCB + 18 && !strcmp(rb_txt(quad_get(t, 0, 3)), "16.00/3.00"),
          "the macro moved B2 only: %d", quad_get(t, 0, 3));
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
        for (i = 0; i < 8u; i++) {               /* (the tables in version 3's units, the patch version 4's) */
            int32_t w = v5_of_v3(QUAD_MAC[i], pr->e[i]);
            CHECK(quad_patch[0][QUAD_MAC[i]] == w && t->p[P_E0 + i] == quad_mac_val(i, w), "%s: macro %u patch %d preset %d",
                  pr->name, i, quad_patch[0][QUAD_MAC[i]], pr->e[i]);
        }
        CHECK(quad_patch[0][QP_EATK] == pr->env[0] && quad_patch[0][QP_EDEC] == pr->env[1] &&
                  quad_patch[0][QP_ESUS] == pr->env[2] && quad_patch[0][QP_EREL] == pr->env[3],
              "%s: preset_t.env is not the amp envelope", pr->name);
        for (i = 0; QUAD_PRESET_EDITS[k][i] != 0xFFu; i += 2)
            CHECK(QUAD_PRESET_EDITS[k][i] < QP_NP && quad_patch[0][QUAD_PRESET_EDITS[k][i]] ==
                      v5_of_v3(QUAD_PRESET_EDITS[k][i], (int8_t)QUAD_PRESET_EDITS[k][i + 1]),
                  "%s: edit %u out of range", pr->name, i / 2);
        {   /* the conversion of the version-3 preset (best effort): valid, every value in range, not the init patch */
            int16_t i4[QP_NP];
            uint32_t j, same = 1;
            quad_init_patch4(i4);
            for (j = 0; j < QP_NP; j++) {
                quad_rng_t r = quad_range(j);
                CHECK(quad_patch[0][j] >= r.min && quad_patch[0][j] <= r.max, "%s: value %u out of range", pr->name, j);
                same &= quad_patch[0][j] == i4[j];
            }
            CHECK(!same && quad_patch[0][QP_LEVEL] > 0, "%s: converted to the init patch / silent", pr->name);
        }
        /* a load with the preset's macros on the track: its patch */
        memset(quad_patch[0], 0, QP_NP * sizeof(int16_t));
        t->preset = (uint8_t)k;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pr->e[i];
        quad_track_loaded(t);
        {
            int16_t p[QP_NP];
            quad_preset_patch4(k, p);
            CHECK(!memcmp(p, quad_patch[0], QP_NP * sizeof(int16_t)), "%s: track_loaded is not the preset's patch", pr->name);
        }
    }
}

static void t_ratios(void)
{
    uint32_t i;
    static const double CB[19] = {0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const double BRV[QUAD_NBR] = {0.5, 1, 1.5, 2, 3, 4};
    for (i = 0; i < QUAD_NRCB; i++)
        CHECK(QUAD_RCB_Q16[i] == (int32_t)(CB[i] * 65536), "C/B step %u", i);
    for (i = 0; i < QUAD_NRA; i++)
        CHECK(QUAD_RA_Q16[i] == (int32_t)((i + 1) * 16384), "A step %u", i);
    CHECK(names_n(QUAD_PAGES[0].col[3].names) == QUAD_NRCB && QUAD_NRB == QUAD_NRCB * QUAD_NRCB,
          "RATIO B: %u names over %u values (a pair)", names_n(QUAD_PAGES[0].col[3].names), QUAD_NRB);
    for (i = 0; i < QUAD_NRB; i++) {             /* the pair: B1 cycles through its steps, then B2 steps on */
        char s[24];
        snprintf(s, sizeof s, "%.2f/%.2f", CB[i % QUAD_NRCB], CB[i / QUAD_NRCB]);
        CHECK(!strcmp(s, rb_txt(i)), "pair %u: %s, want %s", i, rb_txt(i), s);
    }
    for (i = 0; i < QUAD_NBR; i++)               /* (version 1's B2 / B1: its loader) */
        CHECK(QUAD_BR_Q16[i] == (int32_t)(BRV[i] * 65536), "BR %u", i);
    CHECK(QRC(1) == 0 && QRC(4) == 3 && QRC(8) == 4 && QRC(64) == 18 && QRA(64) == 63 && QRB(4, 4) == QUAD_RB_DEF &&
              QRB(1, 1) == 0 && QRB(64, 64) == QUAD_NRB - 1 && QRB(4, 8) == 3 * QUAD_NRCB + 4,
          "the preset macros' ratio helpers");
    /* increments: ratio 2 = twice, an offset, the detune in cents */
    {
        uint32_t base = pitch_inc(60 * 16), x;
        double c;
        CHECK(quad_inc(base, QUAD_RCB_Q16[4], 0) == 2u * base, "ratio 2.00: %u, base %u", quad_inc(base, QUAD_RCB_Q16[4], 0), base);
        x = quad_inc(base, quad_ratio(QUAD_RA_Q16[3], 37), 0);
        CHECK(fabs((double)x / base - 1.37) < 1e-4, "ratio 1.00 + 0.37: %.5f", (double)x / base);
        CHECK(quad_ratio(QUAD_RCB_Q16[0], -100) == 0, "a negative ratio clamps at 0");
        c = 1200.0 * log2((double)quad_inc(base, 65536, QUAD_DT_UP[127]) / base);
        CHECK(fabs(c - 50.0) < 1.0, "DTUNE 127: A +%.2f cents", c);
        c = 1200.0 * log2((double)quad_inc(base, 65536, QUAD_DT_DN[127]) / base);
        CHECK(fabs(c + 50.0) < 1.0, "DTUNE 127: B2 %.2f cents", c);
        c = 1200.0 * log2((double)quad_inc(base, 65536, QUAD_DT_UP[64]) / base);
        CHECK(c > 5.0 && c < 7.0, "DTUNE 64: A %.2f cents (the slight segment)", c);
        c = 1200.0 * log2((double)quad_inc(base, 65536, QUAD_DT_UP[32]) / base);
        CHECK(c > 2.5 && c < 3.5, "DTUNE 32: A %.2f cents", c);
        c = 1200.0 * log2((double)quad_inc(base, 65536, QUAD_DT_UP[96]) / base);
        CHECK(c > 15.0 && c < 25.0, "DTUNE 96: A %.2f cents (the wide segment)", c);
    }
}

/* RATIO B, the pair: one step a detent walks B1 through its 19 steps, then B2 steps on (both directions); the ends
 * hold; the texts; version-1 blobs and macros (BR x 19 + B1, B2 = B1 x BR) come in as B1, the nearest B2 and OFS B2 */
static void t_ratio_b(void)
{
    static const double CB[19] = {0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    track_t *t = &trk[0];
    int32_t i, v, bad = 0;
    char s[24];
    drv_reset(t);
    quad_blob_set(t, 0);
    CHECK(quad_get(t, 0, 3) == QUAD_RB_DEF && !strcmp(rb_txt(QUAD_RB_DEF), "1.00/1.00"), "init: RATIO B 1.00/1.00");
    quad_set(t, 0, 3, 0);
    CHECK(quad_get(t, 0, 3) == 0 && !strcmp(rb_txt(0), "0.25/0.25") && quad_patch[0][QP_RB1] == 0 &&
              quad_patch[0][QP_RB2] == 0, "the first pair 0.25/0.25");
    for (v = 0, i = 0; i < 400; i++) {           /* up: a step a detent (cr_edit: v0 + 1), as the editor turns */
        int32_t w = clamp(quad_get(t, 0, 3) + 1, 0, QUAD_NRB - 1), b1, b2;
        quad_set(t, 0, 3, w);
        v = quad_get(t, 0, 3);
        b1 = quad_patch[0][QP_RB1], b2 = quad_patch[0][QP_RB2];
        snprintf(s, sizeof s, "%.2f/%.2f", CB[b2], CB[b1]);   /* B2 first */
        if (v != w || b1 != w / 19 || b2 != w % 19 || t->p[P_E3] != b2 || strcmp(rb_txt(v), s))
            bad++;
    }
    CHECK(!bad && v == QUAD_NRB - 1 && !strcmp(rb_txt(v), "16.00/16.00"), "walk up: %d bad, ends at %d", bad, v);
    quad_set(t, 0, 3, 18);                       /* the carry (B2 16.00 under B1 0.25) + 1 = B2 0.25 under B1 0.50 */
    CHECK(!strcmp(rb_txt(quad_get(t, 0, 3)), "16.00/0.25") && quad_patch[0][QP_RB1] == 0 && quad_patch[0][QP_RB2] == 18,
          "B2's last step under B1 0.25");
    quad_set(t, 0, 3, quad_get(t, 0, 3) + 1);
    CHECK(quad_get(t, 0, 3) == 19 && quad_patch[0][QP_RB1] == 1 && quad_patch[0][QP_RB2] == 0 &&
              !strcmp(rb_txt(19), "0.25/0.50"), "the carry into B1");
    quad_set(t, 0, 3, quad_get(t, 0, 3) - 1);
    CHECK(quad_get(t, 0, 3) == 18 && quad_patch[0][QP_RB1] == 0 && quad_patch[0][QP_RB2] == 18, "the carry back");
    for (bad = 0, i = 0; i < 400; i++) {         /* down from the top */
        int32_t w = clamp(quad_get(t, 0, 3) - 1, 0, QUAD_NRB - 1);
        if (i == 0)
            quad_set(t, 0, 3, QUAD_NRB - 1), w = QUAD_NRB - 2;
        quad_set(t, 0, 3, w);
        v = quad_get(t, 0, 3);
        if (v != w || quad_patch[0][QP_RB1] != w / 19 || quad_patch[0][QP_RB2] != w % 19)
            bad++;
    }
    CHECK(!bad && v == 0, "walk down: %d bad, ends at %d", bad, v);
    quad_set(t, 0, 3, -1);
    CHECK(quad_get(t, 0, 3) == 0, "below the first pair: held");
    quad_set(t, 0, 3, QUAD_NRB);
    CHECK(quad_get(t, 0, 3) == QUAD_NRB - 1, "past the last pair: held (no wrap)");
    /* QRB(b1, b2): B1 0.50, B2 2.00 */
    quad_set(t, 0, 3, QRB(2, 8));
    CHECK(QUAD_RCB_Q16[quad_patch[0][QP_RB2]] == 2 * 65536 && QUAD_RCB_Q16[quad_patch[0][QP_RB1]] == 32768,
          "0.50/2.00");
    /* version 1: a blob whose RATIO B is BR x 19 + B1 (and version 2's other meanings: converted as version 2's) */
    {
        static const struct { int32_t v1, ofs, b1, b2, ofs2; } V1[] = {
            {1 * 19 + 3, 0, 3, 3, 0},             /* 1.00 x 1 */
            {4 * 19 + 4, 7, 4, 8, 7},             /* BELL: 2.00 x 3 = 6.00 */
            {2 * 19 + 9, 0, 9, 12, 50},           /* METAL: 7 x 1.5 = 10.5: 10 + 0.50 */
            {2 * 19 + 3, 0, 3, 3, 50},            /* DRONE: 1 x 1.5 = 1.5: 1 + 0.50 (halfway: the lower) */
            {2 * 19 + 3, 80, 3, 3, 100},          /* .. OFS B2 clamped */
            {0, 0, 0, 0, -13},                    /* 0.25 x 0.5 = 0.125: 0.25 - 0.13 */
            {5 * 19 + 18, 0, 18, 18, 100},        /* 16 x 4 = 64: 16 + 1.00 (as far as it goes) */
            {0 * 19 + 7, 0, 7, 4, 50},            /* 5 x 0.5 = 2.5: 2 + 0.50 */
        };
        uint8_t b[QUAD_BLOB];
        int16_t p[QP_NP], q[QP_NP], e[QP_NP];
        uint32_t k, j;
        for (k = 0; k < NELEM(V1); k++) {
            rnd_patch_v2(p);
            p[QP_OFSB2] = (int16_t)V1[k].ofs;
            pack_v2(p, b, 1u);                   /* version 1: 71 values, RATIO B one, no B2 */
            b[2 + QP_RB1] = (uint8_t)V1[k].v1;
            CHECK(quad_blob_ok(b), "v1 %u: not valid", k);
            memcpy(e, p, QP_NP * sizeof(int16_t));
            quad_v2_to_v3(e);
            to_v4(e, e);
            CHECK(quad_unpack(b, q) == 1 && q[QP_RB1] == V1[k].b1 && q[QP_RB2] == V1[k].b2 && q[QP_OFSB2] == V1[k].ofs2,
                  "v1 %u (RATIO B %d): B1 %d B2 %d OFS B2 %d, want %d %d %d", k, V1[k].v1, q[QP_RB1], q[QP_RB2],
                  q[QP_OFSB2], V1[k].b1, V1[k].b2, V1[k].ofs2);
            for (j = 0; j < QP_NP; j++)
                if (j != QP_RB1 && j != QP_RB2 && j != QP_OFSB2)
                    CHECK(q[j] == e[j], "v1 %u: value %u %d, want %d", k, j, q[j], e[j]);
            quad_blob_set(t, b);                 /* -> the patch, the macro B2; blob_get writes version 3 */
            quad_blob_get(t, b);
            CHECK(t->p[P_E3] == V1[k].b2 && b[1] == 5u && quad_blob_ok(b), "v1 %u: blob_set / get", k);
        }
        rnd_patch_v2(p);
        pack_v2(p, b, 1u);
        b[2 + QP_RB1] = QUAD_NRB_V1;              /* past version 1's range */
        CHECK(!quad_blob_ok(b), "v1: RATIO B 114 taken");
        b[2 + QP_RB1] = 0;
        b[2 + QP_RB2] = 1;                       /* version 1's padding starts at B2's place */
        CHECK(!quad_blob_ok(b), "v1: padding not 0 taken");
    }
    /* version-1 macros (a project or record from before: P_E3 = BR x 19 + B1): B1, B2, OFS B2 as a v1 blob's */
    {
        drv_reset(t);
        t->preset = 0xFF;
        for (i = 0; i < 8; i++)
            t->p[P_E0 + i] = (int16_t)quad_range(QUAD_MAC[i]).def;
        t->p[P_E3] = 2 * 19 + 3;                 /* 1 x 1.5 */
        quad_track_loaded(t);
        CHECK(quad_patch[0][QP_RB1] == 3 && quad_patch[0][QP_RB2] == 3 && quad_patch[0][QP_OFSB2] == 50 &&
                  t->p[P_E3] == 3, "v1 macros: 1.00/1.50 as 1.00/1.00 + 0.50");
    }
}

/* version 6 (2026-10-31, FIZZ's engine values): a sound with none of them set packs as version 5, byte for byte what
 * 0.27 wrote (older firmware and the web's tools read it); one set: version 6, its five bytes; an older firmware (QUAD_VER
 * 5) refuses it (INIT); each value round trips alone and at its ends; FTYPE 4..7 only in version 6 */
static int v5_fw_takes(const uint8_t *b) { return b && b[0] == QUAD_MAGIC && b[1] >= 1u && b[1] <= 5u; }
static void t_blob6(void)
{
    uint8_t b[QUAD_BLOB], b2[QUAD_BLOB];
    int16_t p[QP_NP], q[QP_NP], init[QP_NP];
    uint32_t i, w, n;
    quad_init_patch4(init);
    for (i = QP_NP5; i < QP_NP; i++)
        CHECK(init[i] == quad_range(i).def, "value %u: init %d", i, init[i]);
    CHECK(init[QP_HOLD] == 127 && init[QP_CRPRE] == 0 && init[QP_SENDS] == 0 && init[QP_FGAIN] == 0, "v6 defaults");
    quad_pack(init, b);
    CHECK(b[1] == 5u && v5_fw_takes(b), "the init patch: version %u", b[1]);
    for (i = QUAD_V5_LEN; i < QUAD_BLOB; i++)
        CHECK(!b[i], "init: byte %u not 0", i);
    for (n = 0; n < 500u; n++) {                 /* version 5 as before: the new values at their defaults */
        rnd_patch(p);
        for (i = QP_NP5; i < QP_NP; i++)
            p[i] = quad_range(i).def;
        quad_pack(p, b);
        CHECK(b[1] == 5u && quad_unpack(b, q) == 1 && !memcmp(p, q, sizeof p), "v5 patch %u: not version 5", n);
    }
    for (i = QP_NP5; i < QP_NP; i++)             /* each value alone at its min, its max: version 6, round trip */
        for (w = 0; w < 2u; w++) {
            quad_rng_t r = quad_range(i);
            memcpy(p, init, sizeof p);
            p[i] = w ? r.max : r.min;
            quad_pack(p, b);
            CHECK(b[1] == (p[i] == r.def ? 5u : 6u) && quad_blob_ok(b) && quad_unpack(b, q) == 1 &&
                      !memcmp(p, q, sizeof p), "value %u at %d: version %u, round trip", i, p[i], b[1]);
            CHECK(b[1] == 5u || !v5_fw_takes(b), "value %u: an older firmware takes version 6", i);
            for (n = QUAD_V6_LEN; n < QUAD_BLOB; n++)
                CHECK(!b[n], "value %u: byte %u not 0", i, n);
        }
    memcpy(p, init, sizeof p);
    p[QP_AMOD] = 1, p[QP_HOLD] = 40, p[QP_DRPRE] = 1, p[QP_FSHAPE] = 99, p[QP_FGAIN] = -7, p[QP_BWPOST] = 1;
    p[QP_BITS] = 12, p[QP_SRR] = 127, p[QP_CRPRE] = 1, p[QP_SENDS] = 1;
    quad_pack(p, b);
    CHECK(b[1] == 6u && b[87] == (87u | 128u) && b[88] == (99u | 128u) && b[89] == (121u | 128u) &&
              b[90] == (12u | 128u) && b[91] == 255u, "the layout: %02x %02x %02x %02x %02x", b[87], b[88], b[89], b[90], b[91]);
    CHECK(quad_unpack(b, q) == 1 && !memcmp(p, q, sizeof p), "every v6 value: round trip");
    memcpy(b2, b, QUAD_BLOB);
    b2[QUAD_V6_LEN] = 1;
    CHECK(quad_unpack(b2, q) == 0 && !memcmp(q, init, sizeof q), "v6: byte 92 not 0 taken");
    {
        uint32_t o = v4_off(QP_FTYPE);
        memcpy(b2, b, QUAD_BLOB);
        b2[o] = QUAD_FT6;
        CHECK(quad_blob_ok(b2), "v6: FTYPE 7 refused");
        b2[o] = QUAD_FT6 + 1;
        CHECK(!quad_blob_ok(b2), "v6: FTYPE 8 taken");
        quad_pack(init, b2);
        b2[o] = 4;
        CHECK(!quad_blob_ok(b2), "v5: FTYPE 4 taken");
    }
    for (n = 0; n < 2000u; n++) {                /* every value random: the round trip */
        rnd_patch(p);
        quad_pack(p, b);
        CHECK(quad_unpack(b, q) == 1 && !memcmp(p, q, sizeof p), "random v6 patch %u", n);
    }
}

/* DRIVE ROUTE PRE (3y205, version 6's DRPRE): off or DRIVE 0 the voice as before; on, every voice driven alone before
 * its filter (a chord = its notes rendered one by one: no intermodulation), the LFOs' DRIVE on it, the part's drive
 * after the voices none (quad_drive_pre); FREQ / RESO turned on a sounding voice glide (~7 ms), a note at its own
 * values as before; quad_bwk at 127.00 its last entry */
static uint32_t pre_render(const int16_t *p, int16_t dist, const uint8_t *notes, uint32_t nn, uint32_t nt, int32_t *buf)
{
    track_t *t = &trk[0];
    uint32_t i, k;
    drv_reset(t);
    drv_patch4(t, p);
    t->p[P_DIST] = dist;
    for (i = 0; i < nn; i++)
        drv_on(t, i, notes[i], 110);
    for (k = 0; k < nt; k++)
        drv_tick(t, buf + k * CTL);
    return nt * CTL;
}
static void t_drive_pre(void)
{
    static int32_t a[200 * CTL], b[200 * CTL], c[200 * CTL], d[200 * CTL];
    static const uint8_t CH[3] = {48, 55, 64};
    int16_t p[QP_NP];
    uint32_t i, n, diff = 0, sum_bad = 0;
    double e0 = 0, e1 = 0;
    quad_preset_patch4(9, p);                    /* WOBBLE: LP24, resonance, two LFOs */
    p[QP_LFO(0, QL_DEPTH)] = p[QP_LFO(1, QL_DEPTH)] = 0;   /* (no LFO: the per-voice sum below is exact) */
    n = pre_render(p, 0, CH, 3, 200, a);
    pre_render(p, 90, CH, 3, 200, b);            /* DRPRE 0: the part's drive is fx.c's, the voices the same */
    for (i = 0; i < n; i++)
        diff += a[i] != b[i];
    CHECK(!diff && !quad_drive_pre(&trk[0]), "DRPRE 0: DRIVE 90 leaves the voices (%u samples differ)", diff);
    p[QP_DRPRE] = 1;
    pre_render(p, 0, CH, 3, 200, b);
    for (i = diff = 0; i < n; i++)
        diff += a[i] != b[i];
    CHECK(!diff && quad_drive_pre(&trk[0]), "DRPRE 1, DRIVE 0: as before, the part's drive off (%u differ)", diff);
    pre_render(p, 90, CH, 3, 200, b);
    for (i = diff = 0; i < n; i++) {
        diff += a[i] != b[i];
        e0 += (double)a[i] * a[i], e1 += (double)b[i] * b[i];
    }
    CHECK(diff > n / 2 && e1 > 0 && e0 > 0, "DRPRE 1, DRIVE 90: the voices driven (%u of %u differ, %.1f dB)", diff, n,
          db(sqrt(e1), sqrt(e0)));
    memset(d, 0, sizeof d);                      /* the chord = its three notes driven alone, summed */
    for (i = 0; i < 3u; i++) {
        uint32_t j;
        pre_render(p, 90, CH + i, 1, 200, c);
        for (j = 0; j < n; j++)
            d[j] += c[j];
    }
    for (i = 0; i < n; i++)
        sum_bad += b[i] != d[i];
    CHECK(!sum_bad, "DRPRE: a chord is its notes driven one by one (%u samples differ: no intermodulation)", sum_bad);
    p[QP_LFO(0, QL_DEST)] = QD_DRIVE, p[QP_LFO(0, QL_DEPTH)] = 6000;   /* LFO 1 on DRIVE */
    pre_render(p, 90, CH, 3, 200, c);
    for (i = diff = 0; i < n; i++)
        diff += c[i] != b[i];
    CHECK(diff > n / 4, "DRPRE: LFO 1 on DRIVE moves the voices' drive (%u differ)", diff);
    {   /* FREQ turned on a sounding note: a quarter of the way a block, there within a few blocks */
        track_t *t = &trk[0];
        int32_t o[CTL];
        uint32_t k;
        quad_init_patch4(p);
        p[QP_FREQ] = 4000;
        drv_reset(t);
        drv_patch4(t, p);
        drv_on(t, 0, 60, 100);
        drv_tick(t, o);
        CHECK(quad_vs[0][0].fsm == 4000, "a note starts at its FREQ (%d)", quad_vs[0][0].fsm);
        quad_patch[0][QP_FREQ] = 8000;
        drv_tick(t, o);
        CHECK(quad_vs[0][0].fsm == 5000, "FREQ 40.00 -> 80.00: a quarter of the way in a block (%d)", quad_vs[0][0].fsm);
        for (k = 0; k < 40u && quad_vs[0][0].fsm != 8000; k++)
            drv_tick(t, o);
        CHECK(quad_vs[0][0].fsm == 8000 && k < 30u, "FREQ there in %u more blocks", k);
        drv_on(t, 0, 62, 100);
        CHECK(quad_vs[0][0].fsm == 8000 && quad_vs[0][0].rsm == p[QP_RESO], "a new note at its own FREQ / RESO");
    }
    CHECK(quad_bwk(12700) == QUAD_BW_K[127] && quad_bwk(12600) == QUAD_BW_K[126] && quad_bwk(0) == QUAD_BW_K[0],
          "quad_bwk: 127.00 its last entry, no read past it");
}

/* version 2 (2026-10-08, before the Digitone review) -> 3: every changed meaning */
static void t_blob_v2(void)
{
    uint8_t b[QUAD_BLOB];
    int16_t p[QP_NP], q[QP_NP];
    uint32_t n, j, bad = 0;
    for (n = 0; n < 500u; n++) {                 /* random version-2 patches: valid, converted value for value */
        int16_t e[QP_NP];
        rnd_patch_v2(p);
        pack_v2(p, b, 2u);
        memcpy(e, p, QP_NP * sizeof(int16_t));
        quad_v2_to_v3(e);
        to_v4(e, e);
        if (!quad_blob_ok(b) || quad_unpack(b, q) != 1 || memcmp(q, e, QP_NP * sizeof(int16_t)))
            bad++;
        for (j = 0; j < QP_NP; j++) {
            quad_rng_t r = quad_range(j);
            if (q[j] < r.min || q[j] > r.max)
                bad++;
        }
    }
    CHECK(!bad, "v2: %u random patches not converted into range", bad);
    quad_init_patch(p);                          /* (a version-2 init patch has v2's meanings) */
    p[QP_FTYPE] = 2;                             /* BP -> LP12 */
    p[QP_HARM] = -26;                            /* all harmonics, table 7 -> the saw build-up's last (-7) */
    p[QP_PHRT] = 0;                              /* off -> OFF */
    p[QP_B1KEY] = 77;                            /* B KEY -> B1 and B2 KEY */
    p[QP_MIX] = -63;
    p[QP_FDBK] = 127;
    p[QP_LFO(0, QL_SPEED)] = 64, p[QP_LFO(0, QL_FADE)] = 40, p[QP_LFO(0, QL_DEST)] = 6, p[QP_LFO(0, QL_MULT)] = 11;
    p[QP_LFO(1, QL_FADE)] = 0, p[QP_LFO(1, QL_DEST)] = 12, p[QP_LFO(2, QL_DEST)] = 2;
    pack_v2(p, b, 2u);
    CHECK(quad_blob_ok(b) && quad_unpack(b, q) == 1, "v2: not taken");
    CHECK(q[QP_FTYPE] == QF_LP12 && q[QP_HARM] == -700 && q[QP_PHRT] == QR_OFF && q[QP_B1KEY] == 77 && q[QP_B2KEY] == 77 &&
              q[QP_MIX] == -63, "v2: FTYPE %d HARM %d PHRT %d KEY %d %d MIX %d", q[QP_FTYPE], q[QP_HARM], q[QP_PHRT],
          q[QP_B1KEY], q[QP_B2KEY], q[QP_MIX]);
    CHECK(q[QP_FDBK] >= 104 && q[QP_FDBK] <= 112, "v2: FDBK 127 -> %d (the same beta, clamped; 0..127)", q[QP_FDBK]);
    CHECK(q[QP_LFO(0, QL_SPEED)] == 6300 && q[QP_LFO(0, QL_FADE)] == -20 && q[QP_LFO(0, QL_DEST)] == QD_RB &&
              q[QP_LFO(0, QL_MULT)] == 11 && q[QP_LFO(1, QL_FADE)] == 0 && q[QP_LFO(1, QL_DEST)] == QD_BLEV &&
              q[QP_LFO(2, QL_DEST)] == QD_DTUN, "v2: the LFOs");
    p[QP_HARM] = 26;                             /* the odd series -> the square build-up's last, on C */
    p[QP_PHRT] = 1;
    p[QP_FTYPE] = 1;
    p[QP_FDBK] = 64;                             /* the old saw-like setting: beta 1.57 rad -> ~30 */
    pack_v2(p, b, 2u);
    CHECK(quad_unpack(b, q) == 1 && q[QP_HARM] == -1900 && q[QP_PHRT] == QR_ALL && q[QP_FTYPE] == QF_HP12 &&
              q[QP_FDBK] >= 29 && q[QP_FDBK] <= 33, "v2: HARM +26 -> %d, PHRT, HP, FDBK 64 -> %d", q[QP_HARM], q[QP_FDBK]);
    CHECK(quad_harm_v2(0) == 0 && quad_harm_v2(-4) == -1 && quad_harm_v2(4) == -15, "v2: HARM's small steps");
    b[2 + QP_LFO(0, QL_MULT)] = 12;              /* past version 2's MULT */
    CHECK(!quad_blob_ok(b), "v2: MULT 12 taken");
    pack_v2(p, b, 2u);
    b[2 + QP_B2KEY] = 1;                         /* version 2's padding starts at B2 KEY's place */
    CHECK(!quad_blob_ok(b), "v2: padding not 0 taken");
}

/* the contract's routings (docs/QUAD.md, the manual's Appendix A.3): modulator -> target pairs, feedback, X, Y; a
 * carrier with "*" is enveloped (at its envelope x level), else direct */
static void t_algo_table(void)
{
    static const char *const D[8][4] = {
        {"A>C B2>B1 B1>C", "A", "C", "B1"}, {"A>C B2>B1", "B2", "C", "B1"}, {"A>C A>B2 A>B1", "A", "C B2", "B1"},
        {"B2>B1 B1>A A>C", "B2", "C", "B1"}, {"B1>A B2>A A>C", "B1", "C", "A"}, {"A>C A>B1 B2>C B2>B1", "A", "C", "B1"},
        {"A>C B2>B1", "A", "C A*", "B1* B2*"}, {"A>C", "B1", "C B2*", "B1*"}};
    static const char *const OP[4] = {"C", "A", "B1", "B2"};
    uint32_t a, o, k;
    for (a = 0; a < 8u; a++) {
        uint8_t mod[4] = {0}, x = 0, y = 0, en = 0, fb = 0xFF;
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
            char w[8], we[8];
            uint32_t r;
            if (!strcmp(D[a][1], OP[k]))
                fb = (uint8_t)k;
            snprintf(w, sizeof w, " %s ", OP[k]);
            snprintf(we, sizeof we, " %s* ", OP[k]);
            for (r = 0; r < 2u; r++) {
                snprintf(buf, sizeof buf, " %s ", D[a][2 + r]);
                if (strstr(buf, w) || strstr(buf, we))
                    *(r ? &y : &x) |= (uint8_t)(1u << k);
                if (strstr(buf, we))
                    en |= (uint8_t)(1u << k);
            }
        }
        for (o = 0; o < 4u; o++)
            CHECK(QUAD_ALGO[a].mod[o] == mod[o], "algo %u: %s's modulators %x, the contract %x", a + 1, OP[o],
                  QUAD_ALGO[a].mod[o], mod[o]);
        CHECK(QUAD_ALGO[a].fb == fb && QUAD_ALGO[a].x == x && QUAD_ALGO[a].y == y && QUAD_ALGO[a].env == en,
              "algo %u: fb / X / Y / enveloped", a + 1);
        CHECK(QUAD_ALGO[a].x & 1u, "algo %u: C is not on X", a + 1);
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
        int16_t p[QP_NP];
        uint32_t nf, nr, np, nt = (uint32_t)(0.3 * FS / CTL), rel = (uint32_t)(0.2 * FS / CTL), n = nt * CTL;
        double se = 0, sr = 0, worst = 0, snr;
        snprintf(fn, sizeof fn, "tests/quad_goldens/%s.json", G[g]);
        s = slurp(fn);
        CHECK(s != 0, "%s: missing (python3 tests/quad_ref.py)", fn);
        if (!s)
            continue;
        np = jarr(s, "patch", pv, QP_NP5);
        nf = jarr(s, "first", first, 2048);
        nr = jarr(s, "rms", rms, 64);
        free(s);
        CHECK(np == QP_NP5 && nf == 2048u && nr == 29u, "%s: patch %u, first %u, rms %u", fn, np, nf, nr);
        if (np != QP_NP5 || nf != 2048u || !nr)
            continue;
        quad_init_patch4(p);                     /* (the model's 73 values; version 6's at their defaults) */
        for (i = 0; i < QP_NP5; i++)
            p[i] = (int16_t)pv[i];
        if (g >= 8u) {                           /* a preset: the model's copy is the firmware's */
            int16_t q[QP_NP];
            for (k = 0; k < QUAD_NPRESETS && strcmp(QUAD_PRESETS[k].name, GP[g - 8u]); k++)
                ;
            quad_preset_patch4(k, q);
            CHECK(k < QUAD_NPRESETS && !memcmp(p, q, QP_NP * sizeof(int16_t)), "%s: quad_ref.py's patch is not eng_quad.c's", GP[g - 8u]);
        }
        drv_reset(t);
        drv_patch4(t, p);                        /* (the goldens' patches: version 4's) */
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
/* the times of quad_times.h's entries (the Digitone's, docs/QUAD.md "Times"), ms: an attack's full rise, a decay's T60 */
static double atk_ms(const uint32_t *tab, uint32_t v) { return (double)(1u << 24) / tab[v] * CTL * 1000.0 / FS; }
static double t60_ms(const uint32_t *tab, uint32_t v) { return -3.0 * CTL * 1000.0 / FS / log10(1.0 - tab[v] / 16777216.0); }
static uint32_t ticks_of(double ms) { return (uint32_t)(ms * FS / 1000.0 / CTL + 0.5); }
/* an operator attack's (linear in dB from QUAD_OP_FLOOR to the top: the growth a tick) */
static double oatk_ms(uint32_t v)
{
    return CTL * 1000.0 / FS * log(16777216.0 / QUAD_OP_FLOOR) / log(1.0 + QUAD_T_OATK[v] / 16777216.0);
}

static void t_env(void)
{
    quad_voice_t s;
    uint32_t n, k;
    int32_t want;
    /* ATK 64: to the top in its table's time (the operators' linear in dB from QUAD_OP_FLOOR: QUAD_T_OATK; the
     * filter's and the amp's linear: QUAD_T_FATK, QUAD_T_AATK), within a tick */
    for (k = 0; k < 4u; k++) {
        static const uint32_t *const TA[4] = {QUAD_T_OATK, QUAD_T_OATK, QUAD_T_FATK, QUAD_T_AATK};
        memset(&s, 0, sizeof s);
        s.stage[k] = 2;
        for (n = 0; s.stage[k] == 2 && n < 1000000u; n++)
            quad_env_tick(&s, k, 64, 60, 20 << 17, 0, 0, 1, 0);
        CHECK(fabs(tms(n) - (k < 2u ? oatk_ms(64) : atk_ms(TA[k], 64))) < 1.5 + tms(n) / 1000.0,   /* (+ 0.1 %: the
                                                  * operators' growth a tick, truncated) */
              "env %u ATK 64: %.1f ms, want %.1f", k,
              tms(n), k < 2u ? oatk_ms(64) : atk_ms(TA[k], 64));
    }
    /* DEC 70 to END 20: 99 % of the way (-40 dB: 2/3 of the T60) */
    memset(&s, 0, sizeof s);
    s.stage[0] = 2;
    for (n = 0; s.stage[0] == 2 && n < 1000000u; n++)
        quad_env_tick(&s, 0, 64, 60, 20 << 17, 0, 0, 1, 0);
    want = 20 << 17;
    for (n = 0; abs(s.env[0] - want) > ((1 << 24) - want) / 100 && n < 1000000u; n++)
        quad_env_tick(&s, 0, 64, 70, want, 0, 0, 1, 0);
    CHECK(fabs(tms(n) / (t60_ms(QUAD_T_ODEC, 70) * 2.0 / 3.0) - 1.0) < 0.01, "A DEC 70: %.1f ms to 99 %%, want %.1f",
          tms(n), t60_ms(QUAD_T_ODEC, 70) * 2.0 / 3.0);
    for (n = 0; n < 40000u; n++)                 /* held at END, gate or not */
        quad_env_tick(&s, 0, 64, 70, want, 0, 0, 1, 0);
    CHECK(s.stage[0] == 3 && abs(s.env[0] - want) < (1 << 12), "END held: %d, want %d", s.env[0], want);
    /* DELAY 50: 50 / 32 beats (781.25 ms at 120 BPM, 1171.9 at 80), then the attack */
    for (k = 0; k < 2u; k++) {
        song.g[G_BPM] = (int16_t)(k ? 80 : 120);
        memset(&s, 0, sizeof s);
        s.stage[1] = 1;
        for (n = 0; s.stage[1] == 1 && n < 100000u; n++)
            quad_env_tick(&s, 1, 0, 60, 0, 0, 50, 1, 0);
        CHECK(fabs(tms(n) - 50.0 / 32.0 * 60000.0 / song.g[G_BPM]) < 0.8 && s.env[1] == 0, "B DLY 50 at %d BPM: %.1f ms, want %.1f",
              song.g[G_BPM], tms(n), 50.0 / 32.0 * 60000.0 / song.g[G_BPM]);
    }
    song.g[G_BPM] = 120;
    /* the amp: sustain, release (T60 QUAD_T_ADEC[70]: to 1/4096, -72 dB, in 1.2 of it), off */
    memset(&s, 0, sizeof s);
    s.stage[3] = 2;
    for (n = 0; n < 3u * FS / CTL; n++)
        quad_env_tick(&s, 3, 20, 30, 90 << 17, 70, 0, 1, 1);
    CHECK(s.stage[3] == 3 && abs(s.env[3] - (90 << 17)) < (1 << 16), "amp sustain %d", s.env[3]);
    for (n = 0; s.stage[3] && n < 100000u; n++)
        quad_env_tick(&s, 3, 20, 30, 90 << 17, 70, 0, 0, 1);
    want = (int32_t)(t60_ms(QUAD_T_ADEC, 70) * 20.0 * log10((90 << 17) / 4096.0) / 60.0);   /* (SUS 90 -> 2^12) */
    CHECK(!s.stage[3] && !s.env[3] && fabs(tms(n) / want - 1.0) < 0.03, "amp REL 70 ended after %.1f ms (to 1/4096), want %d",
          tms(n), want);
    /* in a voice: LEV reached after the attack (velocity 127), TRIG and RESET on a retrigger */
    {
        track_t *t = &trk[0];
        int32_t out[CTL], lvl, e0;
        int16_t p[QP_NP];
        quad_init_patch(p);
        p[QP_AATK] = 30, p[QP_ADEC] = 100, p[QP_AEND] = 127, p[QP_ALEV] = 100;
        p[QP_BATK] = 30, p[QP_BDEC] = 90, p[QP_BEND] = 30, p[QP_BLEV] = 90;
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 127);
        for (n = 0; n < ticks_of(oatk_ms(30)) + 4u; n++)
            drv_tick(t, out);
        lvl = quad_lev(100 * 4096 / 127);
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
        quad_patch[0][QP_PHRT] = QR_OFF;         /* PHRT OFF: the phases run on */
        drv_tick(t, out);
        e0 = (int32_t)quad_vs[0][0].ph[0];
        drv_on(t, 0, 67, 127);
        CHECK((int32_t)quad_vs[0][0].ph[0] == e0 && e0, "PHASE RESET off: phase reset");
        quad_patch[0][QP_PHRT] = QR_ALL;         /* PHRT ALL on a voice that sounds: what is heard runs on (C, the
                                                  * direct carrier B1, A and B2 modulating), a silent operator restarts
                                                  * (docs/QUAD.md "Pops") */
        drv_tick(t, out);
        {
            uint32_t ph0[4];
            memcpy(ph0, quad_vs[0][0].ph, sizeof ph0);
            drv_on(t, 0, 69, 127);
            CHECK(!memcmp(quad_vs[0][0].ph, ph0, sizeof ph0) && ph0[0], "PHASE RESET on a sounding voice: restarted");
            quad_patch[0][QP_BLEV] = 0;          /* B2 silent (B LEV 0: B1 at 0 too, but B1 is algorithm 1's direct Y) */
            for (n = 0; n < 4u; n++)
                drv_tick(t, out);
            memcpy(ph0, quad_vs[0][0].ph, sizeof ph0);
            drv_on(t, 0, 71, 127);
            CHECK(quad_vs[0][0].ph[0] == ph0[0] && quad_vs[0][0].ph[1] == ph0[1] && quad_vs[0][0].ph[2] == ph0[2] &&
                  !quad_vs[0][0].ph[3] && ph0[3], "PHASE RESET on, B2 silent: C %u A %u B1 %u B2 %u", quad_vs[0][0].ph[0],
                  quad_vs[0][0].ph[1], quad_vs[0][0].ph[2], quad_vs[0][0].ph[3]);
            quad_patch[0][QP_BLEV] = 90;
            t->v[0].gate = 0;                    /* released down to under -40 dB (still sounding): all restart */
            for (n = 0; n < 20000u && (quad_vs[0][0].amp >= 328 || !quad_vs[0][0].amp); n++)
                drv_tick(t, out);
            CHECK(quad_vs[0][0].amp > 0 && quad_vs[0][0].amp < 328 && quad_vs[0][0].stage[3], "the release: amp %d",
                  quad_vs[0][0].amp);
            drv_on(t, 0, 72, 127);
            CHECK(!quad_vs[0][0].ph[0] && !quad_vs[0][0].ph[1] && !quad_vs[0][0].ph[2] && !quad_vs[0][0].ph[3],
                  "PHASE RESET on a quiet voice: phases %u %u %u %u", quad_vs[0][0].ph[0], quad_vs[0][0].ph[1],
                  quad_vs[0][0].ph[2], quad_vs[0][0].ph[3]);
        }
        quad_patch[0][QP_ATRIG] = 1;             /* legato with TRIG on: A restarts */
        for (n = 0; n < 200u; n++)
            drv_tick(t, out);
        quad_legato(t, &t->v[0]);
        CHECK(quad_vs[0][0].stage[0] == 2 && !quad_vs[0][0].env[0], "legato, A TRIG on: not restarted");
        quad_patch[0][QP_BTRIG] = 0;
        for (n = 0; n < ticks_of(oatk_ms(30)) + 4u; n++)
            drv_tick(t, out);
        quad_legato(t, &t->v[0]);
        CHECK(quad_vs[0][0].stage[1] == 3, "legato, B TRIG off: restarted");
    }
}

/* REL INF (127, the Digitone's 0..126 + INF): the amp and filter envelopes hold their level after the note-off (stage
 * 5) for 2 s within 1 LSB, the voice sounds on (done 0) and is endless (engine_t.endless: voice.c trk_all_off, the
 * panic / all notes off, kills it); REL 126 decays; a retrigger (a steal of the part's own voice) restarts it; the
 * platform's kill (voice.c: active 0) frees the slot for a fresh note */
static void t_rel_inf(void)
{
    track_t *t = &trk[0];
    int32_t out[CTL];
    int16_t p[QP_NP];
    uint32_t n, r;
    for (r = 126; r <= 127u; r++) {
        int32_t e0, f0, e1, f1;
        double sq = 0.0;
        quad_init_patch(p);
        p[QP_EATK] = 0, p[QP_EDEC] = 40, p[QP_ESUS] = 100, p[QP_EREL] = (int16_t)r;
        p[QP_FATK] = 0, p[QP_FDEC] = 40, p[QP_FSUS] = 90, p[QP_FREL] = (int16_t)r, p[QP_FDEPTH] = 30, p[QP_FREQ] = 60;
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 100);
        for (n = 0; n < FS / 2u / CTL; n++)      /* 0.5 s: at SUS */
            drv_tick(t, out);
        t->v[0].gate = 0;                        /* the note-off */
        drv_tick(t, out);
        e0 = quad_vs[0][0].env[3] >> 9, f0 = quad_vs[0][0].env[2] >> 9;
        for (n = 0; n < 2u * FS / CTL; n++)      /* 2 s later */
            drv_tick(t, out);
        for (n = 0; n < CTL; n++)
            sq += (double)out[n] * out[n];
        e1 = quad_vs[0][0].env[3] >> 9, f1 = quad_vs[0][0].env[2] >> 9;
        if (r == 127u) {
            CHECK(abs(e1 - e0) <= 1 && e0 > 20000 && quad_vs[0][0].stage[3] == 5u && t->v[0].active && !quad_done(t, &t->v[0]),
                  "AMP REL INF: amp %d -> %d after 2 s (stage %u, active %u)", e0, e1, quad_vs[0][0].stage[3], t->v[0].active);
            CHECK(abs(f1 - f0) <= 1 && f0 > 20000 && quad_vs[0][0].stage[2] == 5u, "F REL INF: filter env %d -> %d (stage %u)",
                  f0, f1, quad_vs[0][0].stage[2]);
            CHECK(sqrt(sq / CTL) > 100.0, "REL INF: silent 2 s after the note-off (RMS %.1f)", sqrt(sq / CTL));
            CHECK(ENG_QUAD.endless == quad_endless && quad_endless(t, &t->v[0]), "REL INF: not endless (the panic would not end it)");
            quad_patch[0][QP_EREL] = 30;           /* REL turned down from INF: it decays and ends */
            for (n = 0; n < 4u * FS / CTL && drv_tick(t, out); n++)
                ;
            CHECK(!t->v[0].active && !quad_vs[0][0].live, "REL INF -> 30: the held voice did not end");
            quad_patch[0][QP_EREL] = 127;        /* held again; a retrigger (the part's own steal) restarts it */
            drv_on(t, 0, 60, 100);
            for (n = 0; n < FS / 4u / CTL; n++)
                drv_tick(t, out);
            t->v[0].gate = 0;
            for (n = 0; n < FS / 4u / CTL; n++)
                drv_tick(t, out);
            CHECK(quad_vs[0][0].stage[3] == 5u, "REL INF: not held (stage %u)", quad_vs[0][0].stage[3]);
            drv_on(t, 0, 64, 100);
            CHECK(quad_vs[0][0].stage[3] == 2u && t->v[0].gate, "REL INF: a retrigger did not restart the amp envelope");
            t->v[0].gate = 0;
            for (n = 0; n < FS / 4u / CTL; n++)
                drv_tick(t, out);
            t->v[0].active = 0;                  /* the platform's kill (voice.c env_tick stage 4: a steal, the panic) */
            CHECK(!drv_tick(t, out), "REL INF: a killed voice still rendered");
            drv_on(t, 0, 67, 100);               /* .. its slot a fresh voice */
            CHECK(quad_vs[0][0].stage[3] == 2u && !quad_vs[0][0].env[3], "REL INF: the slot after a kill not fresh");
        } else {
            CHECK(e1 < e0 * 9 / 10 && f1 < f0 * 9 / 10 && quad_vs[0][0].stage[3] == 4u && !quad_endless(t, &t->v[0]),
                  "REL 126: amp %d -> %d, filter %d -> %d after 2 s (want a decay)", e0, e1, f0, f1);
        }
    }
}

/* ------------------------------------------------------------ filters --- */
/* the RMS of the last 0.2 s of 0.4 s of one held note (a sine on C: X only, A and B silent) with these edits */
static double tone_rms(uint32_t note, const uint8_t *ed)
{
    track_t *t = &trk[0];
    int16_t p[QP_NP];
    int32_t out[CTL];
    uint32_t n, i;
    double a = 0;
    quad_init_patch(p);
    p[QP_ALGO] = 8, p[QP_MIX] = -64, p[QP_ALEV] = 0, p[QP_BLEV] = 0;   /* X = C alone */
    for (; ed && *ed != 0xFFu; ed += 2)
        p[ed[0]] = (int16_t)ed[1];
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
    static const uint8_t HP_LO[] = {QP_FTYPE, QF_HP12, QP_FREQ, 30, 0xFF}, HP_HI[] = {QP_FTYPE, QF_HP12, QP_FREQ, 110, 0xFF};
    static const uint8_t L4_LO[] = {QP_FTYPE, QF_LP24, QP_FREQ, 30, 0xFF}, L4_HI[] = {QP_FTYPE, QF_LP24, QP_FREQ, 110, 0xFF};
    static const uint8_t OFF_LO[] = {QP_FTYPE, QF_OFF, QP_FREQ, 0, QP_RESO, 127, 0xFF};
    static const uint8_t BW_LP24[] = {QP_BASE, 90, QP_WIDTH, 37, QP_FTYPE, QF_LP24, QP_FREQ, 30, 0xFF};
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
    {   /* (2 kHz under a 580 Hz cutoff: LP24 twice LP12's slope; far below, dsp.c's Q13 SVF has a floor) */
        static const uint8_t L2_60[] = {QP_FREQ, 60, 0xFF}, L4_60[] = {QP_FTYPE, QF_LP24, QP_FREQ, 60, 0xFF};
        double t0 = tone_rms(96, 0), l2 = tone_rms(96, L2_60);
        r = tone_rms(96, L4_60);
        CHECK(db(l2, t0) < -15.0 && db(r, l2) < -15.0, "LP24 at 580 Hz on 2.1 kHz: %.1f dB (LP12 %.1f dB)", db(r, t0),
              db(l2, t0));
        r = tone_rms(72, L4_LO);
        CHECK(db(r, r0) < -24.0, "LP24 at 132 Hz on 523 Hz: %.1f dB", db(r, r0));
    }
    r = tone_rms(72, L4_HI);
    CHECK(fabs(db(r, r0)) < 1.0, "LP24 at 7 kHz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, OFF_LO);
    CHECK(fabs(db(r, r0)) < 0.1, "TYPE OFF (FREQ 0, RESO 127): %.2f dB", db(r, r0));
    r = tone_rms(72, BW_LP24);                   /* base-width before the multimode: both cut */
    CHECK(db(r, r0) < -30.0, "base-width HP 2.5 kHz then LP24 132 Hz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BW_HP);
    CHECK(db(r, r0) < -9.0, "base-width: BASE 2.5 kHz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BW_LP);
    CHECK(db(r, r0) < -9.0, "base-width: BASE + WIDTH 85 Hz on 523 Hz: %.1f dB", db(r, r0));
    r = tone_rms(72, BW_IN);
    CHECK(fabs(db(r, r0)) < 1.5, "base-width: 523 Hz inside 68 Hz .. 4 kHz: %.1f dB", db(r, r0));
    {   /* HARM on C (-): every wave near the sine's level; + leaves C alone */
        static uint8_t HM[] = {QP_HARM, 0, 0xFF};
        static const uint8_t HP26[] = {QP_HARM, 26, 0xFF};
        double rp = tone_rms(72, HP26), worst = 0;
        int32_t h;
        for (h = 1; h <= 26; h++) {
            HM[1] = (uint8_t)-h;
            r = tone_rms(72, HM);
            worst = fabs(db(r, r0)) > worst ? fabs(db(r, r0)) : worst;
        }
        CHECK(worst < 4.5 && fabs(db(rp, r0)) < 0.01, "HARM -1..-26 on C: within %.1f dB of the sine; +26: %.2f dB", worst,
              db(rp, r0));
    }
}

/* ---------------------------------------------------------------- LFOs --- */
static void t_lfo(void)
{
    /* f = SPEED x MULT / 128 x BPM / 240 Hz (SPEED x MULT 128 = a bar); MULT 12..23 at 120 BPM whatever the tempo */
    static const int16_t SP[9] = {16, 32, 24, 63, -40, 1, 32, 16, 32};
    static const uint8_t MU[9] = {3, 2, 4, 8, 6, 11, 2, 15, 14};
    static const int16_t BPM[9] = {120, 120, 120, 120, 120, 120, 90, 90, 200};
    track_t *t = &trk[0];
    uint32_t k, n;
    for (k = 0; k < 9u; k++) {                  /* FREE: the part's phase over 20 s */
        double want = SP[k] * (double)(1u << (MU[k] % 12u)) / 128.0 * (MU[k] < 12u ? BPM[k] : 120) / 240.0, cyc = 0;
        uint32_t nt = (uint32_t)(20.0 * FS / CTL);
        drv_reset(t);
        song.g[G_BPM] = BPM[k];
        quad_blob_set(t, 0);
        quad_patch[0][QP_LFO(0, QL_SPEED)] = (int16_t)(SP[k] * 100);   /* (hundredths) */
        quad_patch[0][QP_LFO(0, QL_MULT)] = (int16_t)MU[k];
        for (n = 0; n < nt; n++) {
            uint32_t old = quad_lfo[0].ph[0];
            quad_block(t);
            cyc += (int32_t)(quad_lfo[0].ph[0] - old) / 4294967296.0;
        }
        cyc /= 20.0;
        CHECK(fabs(cyc / want - 1.0) < 0.02, "LFO SPEED %d MULT %s at %d BPM: %.4f Hz, want %.4f", SP[k],
              N_QUAD_MULT[MU[k]], BPM[k], cyc, want);
        if (k == 1u)
            CHECK(fabs(cyc - 0.5) < 0.01, "SPEED 32 x 4 = one bar at 120 BPM: %.4f Hz (want 0.5)", cyc);
    }
    song.g[G_BPM] = 120;
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
        quad_patch[0][QP_LFO(0, QL_SPEED)] = -2000;
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
            CHECK(cyc < 0 && fabs(-cyc / 4.0 / (20.0 * 32 / 256.0) - 1.0) < 0.02, "TRIG -20 x32: %.3f Hz", cyc / 4.0);
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
static void chord(const int16_t *p, uint32_t mono, double secs, const uint8_t *notes, int32_t *peak, int64_t *maxabs,
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
        int16_t p[QP_NP];
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
                    for (f = 0; f < QF_N; f++) {
                        int16_t p[QP_NP];
                        quad_init_patch(p);
                        p[QP_ALGO] = (int16_t)a;
                        p[QP_RC] = 18, p[QP_RA] = (int16_t)(h ? 63 : 0), p[QP_RB1] = 18, p[QP_RB2] = (int16_t)(18 - h * 2);
                        p[QP_HARM] = (int16_t)(h ? 26 : -26), p[QP_FDBK] = 120, p[QP_DTUN] = 127;
                        p[QP_MIX] = (int16_t)(mi == 0 ? -64 : mi == 1 ? 63 : 0);
                        p[QP_OFSC] = 100, p[QP_OFSB2] = -100;
                        p[QP_ALEV] = p[QP_BLEV] = p[QP_AEND] = p[QP_BEND] = 127;
                        p[QP_AKEY] = p[QP_B1KEY] = p[QP_B2KEY] = (int16_t)(h ? 127 : 0), p[QP_VEL] = 0;
                        p[QP_PHRT] = (int16_t)((a + mi) % QR_N);
                        p[QP_FTYPE] = (int16_t)f, p[QP_RESO] = 127, p[QP_FREQ] = (int16_t)(f == QF_HP12 ? 0 : 90);
                        p[QP_FDEPTH] = (int16_t)(h ? 63 : -64), p[QP_FKTRK] = 127, p[QP_FSUS] = 127;
                        p[QP_BASE] = (int16_t)(f * 20), p[QP_WIDTH] = 60, p[QP_LEVEL] = 127, p[QP_ESUS] = 127;
                        for (k = 0; k < 3u; k++)    /* (every destination over the 192 patches) */
                            p[QP_LFO(k, QL_DEST)] = (int16_t)((a * 5u + k * 13u + h * 7u + mi * 3u + f) % QD_N);
                        p[QP_LFO(2, QL_FADE)] = (int16_t)(h ? 20 : -64);
                        for (k = 0; k < 3u; k++) {
                            p[QP_LFO(k, QL_DEPTH)] = (int16_t)(h ? 63 : -64);
                            p[QP_LFO(k, QL_WAVE)] = (int16_t)(k == 2u ? QW_RAND : QW_SQR);
                            p[QP_LFO(k, QL_PHASE)] = (int16_t)(k == 2u ? 127 : 0);
                            p[QP_LFO(k, QL_SPEED)] = (int16_t)(h ? 63 : -64), p[QP_LFO(k, QL_MULT)] = (int16_t)(11 + 12 * h);
                        }
                        chord(p, 0, 0.3, NOTES, &peak, &mx, &left);
                        CHECK(mx < (1 << 30), "extremes a%u h%u m%u f%u: |sum| %lld", a, h, mi, f, (long long)mx);
                        CHECK(!left, "extremes a%u h%u m%u f%u: voices left", a, h, mi, f);
                    }
    }
}

/* RATIO B heard: BELL (algorithm 2: A > C on X, B2 > B1 on Y, B1 a direct carrier), MIX +63 (Y only), B LEV 50 (B2
 * a small index: B1's phase runs monotonic, its zero crossings count its frequency); a note at RATIO B 1.00/1.00, the
 * editor's path to B1 2.00 (cr_edit.c ce_knob: eng_deep_t.set, then .get; the macro P_E3 = B2 mirrored by quad_set,
 * quad_block the next block), a note again: Y an octave up, and live: the same edit while a note sounds moves it
 * within a block. B2 2.00 changes the timbre, not the pitch. B LEV 0: Y (B1 direct) as loud as at B LEV 50 */
static int16_t rb_buf[2][FS / 2];
static double rb_note(track_t *t, uint32_t b, int32_t edit_at, int32_t edit_to, double *zc2, double *rms)
{
    int16_t *buf = rb_buf[b & 1u];
    const eng_deep_t *dd = ENG_QUAD.deep;
    int32_t out[CTL];
    uint32_t n, i, ns = 0, z1 = 0, z2 = 0, nt = (uint32_t)(FS / 2 / CTL), h0 = 0, h1 = 0;
    double e = 0;
    memset(quad_vs, 0, sizeof quad_vs);
    memset(t->v, 0, sizeof t->v);
    drv_on(t, 0, 60, 100);
    for (n = 0; n < nt; n++) {
        if ((int32_t)n == edit_at) {
            dd->set(t, 0, 3, edit_to);            /* (the editor's sequence: set, then get for the trace) */
            (void)dd->get(t, 0, 3);
        }
        drv_tick(t, out);
        for (i = 0; i < CTL; i++)
            buf[ns++] = (int16_t)clamp(out[i] >> 4, -32768, 32767);
    }
    memset(buf + ns, 0, (FS / 2 - ns) * sizeof *buf);
    for (i = ns / 8u; i + 1u < ns; i++) {         /* (past the attack) zero crossings: the first half, the second */
        int c = (buf[i] < 0) != (buf[i + 1] < 0);
        if (i < ns / 2u - ns / 16u)
            z1 += (uint32_t)c, h0 = h0 ? h0 : i;
        else if (i >= ns / 2u + ns / 16u)
            z2 += (uint32_t)c, h1 = h1 ? h1 : i;
        e += (double)buf[i] * buf[i];
    }
    *rms = sqrt(e / (double)ns);
    *zc2 = z2 * (double)FS / 2.0 / (double)(ns - 1u - h1);
    return z1 * (double)FS / 2.0 / (double)(ns / 2u - ns / 16u - h0);
}
static void t_ratio_b_heard(void)
{
    const eng_deep_t *dd = ENG_QUAD.deep;
    track_t *t = &trk[0];
    double f1, f2, fl1, fl2, z, r1, r2, r0;
    uint32_t k;
    for (k = 0; k < QUAD_NPRESETS && strcmp(QUAD_PRESETS[k].name, "BELL"); k++)
        ;
    drv_reset(t);
    t->preset = (uint8_t)k;
    quad_blob_preset(t, k);
    dd->set(t, 0, 0, 2);                          /* ALGO 2 (BELL's) */
    dd->set(t, 1, 3, 63);                         /* MIX +63: Y only */
    dd->set(t, 8, 3, 50);                         /* B LEV 50: B2 0.17 */
    dd->set(t, 0, 3, QRB(4, 4));                  /* 1.00/1.00 */
    CHECK(quad_get(t, 1, 3) == 63 && t->p[P_E7] == 63 && quad_get(t, 0, 3) == QRB(4, 4), "BELL, MIX +63, 1.00/1.00");
    f1 = rb_note(t, 0, -1, 0, &z, &r1);
    dd->set(t, 0, 3, QRB(8, 4));                  /* the edit: B1 2.00, B2 1.00 */
    CHECK(quad_get(t, 0, 3) == QRB(8, 4) && quad_get(t, 0, 3) == 4 * 19 + 3 && quad_patch[0][QP_RB1] == 4 &&
              quad_patch[0][QP_RB2] == 3 && t->p[P_E3] == 3 && !strcmp(rb_txt(quad_get(t, 0, 3)), "1.00/2.00"),
          "B1 2.00 / B2 1.00 in the patch and the macro (text B2/B1)");
    quad_block(t);                                /* (a block: the macro mirror must not undo it) */
    CHECK(quad_patch[0][QP_RB1] == 4 && quad_patch[0][QP_RB2] == 3, "a block after the edit kept B1 2.00");
    f2 = rb_note(t, 1, -1, 0, &z, &r2);
    if (getenv("VERBOSE"))
        printf("  RATIO B heard: Y at 1.00/1.00 %.1f Hz, at 2.00/1.00 %.1f Hz (RMS %.0f, %.0f)\n", f1, f2, r1, r2);
    CHECK(r1 > 5.0 && r2 > 5.0 && memcmp(rb_buf[0], rb_buf[1], sizeof rb_buf[0]), "Y silent or the same (RMS %.0f, %.0f)",
          r1, r2);
    CHECK(fabs(f1 - 261.6) < 8.0 && fabs(f2 / f1 - 2.0) < 0.04, "Y: %.1f Hz at 1.00/1.00, %.1f Hz at 2.00/1.00 (want x2)",
          f1, f2);
    dd->set(t, 0, 3, QRB(4, 4));                  /* live: 1.00/1.00, the edit to 2.00/1.00 halfway through a note */
    fl1 = rb_note(t, 0, (int32_t)(0.25 * FS / CTL), QRB(8, 4), &fl2, &z);
    CHECK(fabs(fl1 - f1) < 0.06 * f1 && fabs(fl2 / f1 - 2.0) < 0.05, "live: %.1f Hz, then %.1f Hz (want %.1f, x2)", fl1,
          fl2, f1);
    dd->set(t, 0, 3, QRB(8, 8));                  /* B2 2.00: the timbre (the same pitch, other samples) */
    {
        double f3 = rb_note(t, 0, -1, 0, &z, &r0), d = 0;
        uint32_t i;
        for (i = 0; i < NELEM(rb_buf[0]); i++)
            d += fabs((double)rb_buf[0][i] - rb_buf[1][i]);
        CHECK(fabs(f3 / f2 - 1.0) < 0.03 && d / NELEM(rb_buf[0]) > 0.1 * r2,
              "B2 1.00 -> 2.00: %.1f Hz (want %.1f), the samples %.1f apart (RMS %.1f)", f3, f2, d / NELEM(rb_buf[0]), r2);
    }
    if (getenv("VERBOSE")) {                      /* BELL as it is: X alone, Y alone, MIX +36 */
        double rx, ry, rm;
        quad_blob_preset(t, k);
        dd->set(t, 1, 3, -63);
        (void)rb_note(t, 0, -1, 0, &z, &rx);
        dd->set(t, 1, 3, 63);
        (void)rb_note(t, 0, -1, 0, &z, &ry);
        dd->set(t, 1, 3, 36);
        (void)rb_note(t, 0, -1, 0, &z, &rm);
        printf("  BELL algorithm 4: RMS X alone %.0f, Y alone %.0f (%.1f dB), MIX +36 %.0f\n", rx, ry, db(ry, rx), rm);
        dd->set(t, 1, 3, 63);
    }
    dd->set(t, 0, 3, QRB(4, 4));
    dd->set(t, 8, 3, 50);
    (void)rb_note(t, 0, -1, 0, &z, &r1);
    dd->set(t, 8, 3, 0);                          /* B LEV 0: B1 is a direct carrier, at full level */
    (void)rb_note(t, 0, -1, 0, &z, &r0);
    CHECK(fabs(db(r0, r1)) < 1.0 && r0 > 100.0, "B LEV 0: Y (B1 direct) RMS %.1f, at B LEV 50 %.1f", r0, r1);
}

/* the mean and the RMS (AC) of 0.3..1 s of one note (60, velocity 100) held 1 s on patch p */
static void held_dc(const int16_t *p, double *mean, double *rms)
{
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t n, i, cnt = 0, nt = FS / CTL;
    double s = 0, s2 = 0;
    drv_reset(t);
    drv_patch(t, p);
    drv_on(t, 0, 60, 100);
    for (n = 0; n < nt; n++) {
        drv_tick(t, out);
        if (n >= nt * 3 / 10)
            for (i = 0; i < CTL; i++, cnt++)
                s += out[i], s2 += (double)out[i] * out[i];
    }
    *mean = s / cnt;
    *rms = sqrt(s2 / cnt - *mean * *mean);
}

/* the operators are not DC-free (the feedback's lagged average skews its saw; at 1:1 a modulator's phase offset or
 * DTUNE's drift is 0 Hz in the carrier): the DC blocker after them (QUAD_DC_K). Without it BRASS held +57 % of its RMS,
 * FEEDBACK -44 %, EP +13 %, a plain sine at FDBK 100 -61 % */
static void t_dc(void)
{
    uint32_t k;
    double m, r;
    int16_t p[QP_NP];
    for (k = 0; k < QUAD_NPRESETS; k++) {
        quad_preset_patch(k, p);
        held_dc(p, &m, &r);
        if (getenv("VERBOSE"))
            printf("  %-12s DC %7.1f RMS %8.1f (%.2f %%)\n", QUAD_PRESETS[k].name, m, r, 100.0 * m / (r + 1e-9));
        CHECK(r < 100 || fabs(m) < 0.02 * r, "%s: DC %.1f, RMS %.1f", QUAD_PRESETS[k].name, m, r);
    }
    for (k = 0; k <= 120u; k += 8u) {            /* algorithm 8's Y: B1 (feedback) direct */
        quad_init_patch(p);
        p[QP_ALGO] = 8, p[QP_MIX] = 63, p[QP_ALEV] = 0, p[QP_BLEV] = p[QP_BEND] = 127, p[QP_FDBK] = (int16_t)k;
        held_dc(p, &m, &r);
        CHECK(fabs(m) < 0.01 * r, "plain feedback operator at FDBK %u: DC %.1f, RMS %.1f", k, m, r);
    }
}

/* ----------------------------------------------------------------- CPU --- */
#ifdef __APPLE__
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
/* host instructions a sample of 8 voices held (an 8-note chord, 1 s after 0.2 s), the part's block included; cpu_dist:
 * the part's DRIVE (a version-5 patch, cpu_v5: as it is) */
static int16_t cpu_dist, cpu_v5;
static double cpu_quad(const int16_t *p)
{
    static const uint8_t CH[8] = {50, 57, 62, 66, 69, 71, 73, 76};
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t i, n;
    uint64_t i0;
    drv_reset(t);
    if (cpu_v5)
        drv_patch4(t, p);
    else
        drv_patch(t, p);
    t->p[P_DIST] = cpu_dist;
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
        int16_t p[QP_NP];
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
        int16_t p[QP_NP];
        double c;
        quad_preset_patch(11, p);
        p[QP_HARM] = 11, p[QP_FTYPE] = QF_LP24, p[QP_RESO] = 40, p[QP_BASE] = 10, p[QP_WIDTH] = 100, p[QP_ALGO] = 7;
        for (k = 0; k < 3u; k++)
            p[QP_LFO(k, QL_DEST)] = (int16_t)(QD_HARM + k), p[QP_LFO(k, QL_DEPTH)] = 20, p[QP_LFO(k, QL_TRIG)] = QT_TRIG;
        c = cpu_quad(p);
        printf("    QUAD worst case (algo 7, HARM + on A and B1, LP24, base-width, 3 LFOs) %6.0f  %5.1f %%\n", c, dev_pct(c));
    }
    {   /* DRIVE ROUTE PRE (3y205): WOBBLE's 8 voices each driven (DRIVE 80) against WOBBLE as it is (its drive the
         * part's, fx.c: not in this figure) */
        int16_t p[QP_NP];
        double c0, c1;
        quad_preset_patch4(9, p);
        cpu_v5 = 1;
        c0 = cpu_quad(p);
        p[QP_DRPRE] = 1;
        cpu_dist = 80;
        c1 = cpu_quad(p);
        cpu_v5 = 0, cpu_dist = 0;
        printf("    QUAD WOBBLE, DRIVE 80 PRE (8 voices driven) %6.0f  %5.1f %% (+%.1f %% = +%.0f us of the half)\n", c1,
               dev_pct(c1), dev_pct(c1) - dev_pct(c0), (dev_pct(c1) - dev_pct(c0)) * 29.02);
    }
}
#else
static void t_cpu(void) { printf("  CPU: macOS only (proc_pid_rusage)\n"); }
#endif

/* ------------------------------------------------ the Digitone review --- */
/* one note (60, velocity 127) for nt ticks of patch p: the samples into buf (nt x CTL) */
static void note_buf(const int16_t *p, uint32_t note, uint32_t nt, int32_t *buf)
{
    track_t *t = &trk[0];
    uint32_t n;
    drv_reset(t);
    drv_patch(t, p);
    drv_on(t, 0, note, 127);
    for (n = 0; n < nt; n++)
        drv_tick(t, buf + n * CTL);
}
static double buf_rms(const int32_t *b, uint32_t from, uint32_t to)
{
    double a = 0;
    uint32_t i;
    for (i = from; i < to; i++)
        a += (double)b[i] * b[i];
    return sqrt(a / (to - from + 1e-9));
}
/* a patch where every operator is audible or modulating at full level, held: distinct ratios (C 1, A 2.00, B1 3.00,
 * B2 5.00), LEV A 127, B LEV 127 (B1 and B2 at full), the envelopes at END 127; algorithm al, MIX m */
static void full_patch(int16_t *p, uint32_t al, int32_t m)
{
    quad_init_patch(p);
    p[QP_ALGO] = (int16_t)al, p[QP_MIX] = (int16_t)m;
    p[QP_RA] = QRA(8), p[QP_RB1] = QRC(12), p[QP_RB2] = QRC(20);
    p[QP_ALEV] = 40, p[QP_BLEV] = 127, p[QP_AEND] = p[QP_BEND] = 127;
}

/* the routings heard: for each algorithm, output X (MIX -64) and Y (MIX 63), a small ratio offset on each operator in
 * turn changes that output exactly when the operator reaches it (a carrier on it, or a modulator of something that
 * reaches it: QUAD_ALGO, checked against the contract by t_algo_table); otherwise the samples stay the same */
static uint32_t reaches(uint32_t a, uint32_t op, uint32_t outs)
{
    uint32_t k;
    if ((outs >> op) & 1u)
        return 1;
    for (k = 0; k < 4u; k++)
        if (((QUAD_ALGO[a].mod[k] >> op) & 1u) && reaches(a, k, outs))
            return 1;
    return 0;
}
static void t_routing(void)
{
    static int32_t b0[40 * CTL], b1[40 * CTL];
    static const uint8_t OFS[4] = {QP_OFSC, QP_OFSA, QP_OFSB1, QP_OFSB2};
    static const char *const OP[4] = {"C", "A", "B1", "B2"};
    uint32_t a, o, xy;
    for (a = 0; a < 8u; a++)
        for (xy = 0; xy < 2u; xy++) {
            int16_t p[QP_NP];
            full_patch(p, a + 1u, xy ? 63 : -64);
            note_buf(p, 60, 40, b0);
            CHECK(buf_rms(b0, 20 * CTL, 40 * CTL) > 300.0, "algo %u %s: silent", a + 1, xy ? "Y" : "X");
            for (o = 0; o < 4u; o++) {
                uint32_t want = reaches(a, o, xy ? QUAD_ALGO[a].y : QUAD_ALGO[a].x), same;
                full_patch(p, a + 1u, xy ? 63 : -64);
                p[OFS[o]] = 7;
                note_buf(p, 60, 40, b1);
                same = !memcmp(b0, b1, sizeof b0);
                CHECK(same == !want, "algo %u %s: %s's ratio %s it", a + 1, xy ? "Y" : "X", OP[o],
                      want ? "does not move" : "moves");
            }
        }
}

/* the spectrum's magnitude at f Hz over samples [from, to) (a Goertzel, a Hann window) */
static double mag_at(const int32_t *b, uint32_t from, uint32_t to, double f)
{
    double w = 2.0 * M_PI * f / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    uint32_t i, n = to - from;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / n), s0 = b[from + i] * h + c * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/* sidebands: algorithm 2, X = C under A: A 1.00 -> 2.00 moves C's sidebands (C 1, A 2: odd harmonics only, the 2nd
 * gone); direct carriers ignore their LEV and envelope (algo 2's Y = B1, algo 5's Y = A); enveloped ones follow
 * (algo 7's X = C + A, Y = B1 + B2; algo 8's X = C + B2) */
static void t_carriers(void)
{
    static int32_t b0[100 * CTL], b1[100 * CTL];
    int16_t p[QP_NP];
    double f0 = 261.63, h2a, h2b, h3b, h1b, r0, r1;
    quad_init_patch(p);
    p[QP_ALGO] = 2, p[QP_MIX] = -64, p[QP_ALEV] = 50, p[QP_AEND] = 127;   /* X: C under A 1.00 */
    note_buf(p, 60, 100, b0);
    p[QP_RA] = QRA(8);                           /* A 2.00 */
    note_buf(p, 60, 100, b1);
    h2a = mag_at(b0, 50 * CTL, 100 * CTL, 2 * f0) / mag_at(b0, 50 * CTL, 100 * CTL, f0);
    h1b = mag_at(b1, 50 * CTL, 100 * CTL, f0);
    h2b = mag_at(b1, 50 * CTL, 100 * CTL, 2 * f0) / h1b;
    h3b = mag_at(b1, 50 * CTL, 100 * CTL, 3 * f0) / h1b;
    CHECK(h2a > 0.1 && h2b < 0.01 && h3b > 0.1, "algo 2: A 1:1 -> 2:1 moves C's sidebands (h2 %.3f -> %.4f, h3 %.3f)",
          h2a, h2b, h3b);
    /* direct: algo 2's Y = B1; B LEV 0..43 (B2 off) and B's envelope do not change it */
    quad_init_patch(p);
    p[QP_ALGO] = 2, p[QP_MIX] = 63, p[QP_BLEV] = 0;
    note_buf(p, 60, 60, b0);
    p[QP_BLEV] = 43, p[QP_BDEC] = 10, p[QP_BEND] = 0, p[QP_BATK] = 90;
    note_buf(p, 60, 60, b1);
    CHECK(!memcmp(b0, b1, 60 * CTL * sizeof *b0) && buf_rms(b0, 0, 60 * CTL) > 300.0,
          "algo 2: Y (B1 direct) moved with B LEV / ENV B");
    quad_init_patch(p);                          /* algo 5's Y = A, direct: A LEV only changes X */
    p[QP_ALGO] = 5, p[QP_MIX] = 63, p[QP_ALEV] = 0;
    note_buf(p, 60, 60, b0);
    p[QP_ALEV] = 127, p[QP_AATK] = 80;
    note_buf(p, 60, 60, b1);
    CHECK(!memcmp(b0, b1, 60 * CTL * sizeof *b0) && buf_rms(b0, 0, 60 * CTL) > 300.0, "algo 5: Y (A direct) moved with A LEV");
    /* enveloped: algo 7's Y = B1 x ENV B x its level + B2 x ..: B LEV 0 silent, 43 B1 alone, decaying with ENV B */
    quad_init_patch(p);
    p[QP_ALGO] = 7, p[QP_MIX] = 63, p[QP_BLEV] = 0, p[QP_BEND] = 127;
    note_buf(p, 60, 60, b0);
    r0 = buf_rms(b0, 0, 60 * CTL);
    p[QP_BLEV] = 43;
    note_buf(p, 60, 60, b1);
    r1 = buf_rms(b1, 20 * CTL, 60 * CTL);
    CHECK(r0 < 1.0 && r1 > 300.0, "algo 7: Y (enveloped) %.1f at B LEV 0, %.1f at 43", r0, r1);
    p[QP_BEND] = 0, p[QP_BDEC] = 20;
    {
        static int32_t b2[400 * CTL];
        note_buf(p, 60, 400, b2);
        CHECK(buf_rms(b2, 350 * CTL, 400 * CTL) < 0.05 * buf_rms(b2, 0, 20 * CTL), "algo 7: Y did not follow ENV B");
    }
    quad_init_patch(p);                          /* algo 7's X = C + A x ENV A x A LEV */
    p[QP_ALGO] = 7, p[QP_MIX] = -64, p[QP_ALEV] = 0;
    note_buf(p, 60, 60, b0);
    p[QP_ALEV] = 60, p[QP_AEND] = 127;
    note_buf(p, 60, 60, b1);
    CHECK(memcmp(b0, b1, 60 * CTL * sizeof *b0), "algo 7: X did not take A");
    quad_init_patch(p);                          /* algo 8's X = C + B2 (enveloped), Y = B1 (enveloped) */
    p[QP_ALGO] = 8, p[QP_MIX] = -64, p[QP_ALEV] = 0, p[QP_BLEV] = 0, p[QP_RB2] = QRC(12);
    note_buf(p, 60, 60, b0);
    p[QP_BLEV] = 127, p[QP_BEND] = 127;
    note_buf(p, 60, 60, b1);
    CHECK(mag_at(b0, 20 * CTL, 60 * CTL, 3 * f0) < 2.0 && mag_at(b1, 20 * CTL, 60 * CTL, 3 * f0) > 100.0,
          "algo 8: X's B2 (3.00) %.1f at B LEV 0, %.1f at 127", mag_at(b0, 20 * CTL, 60 * CTL, 3 * f0),
          mag_at(b1, 20 * CTL, 60 * CTL, 3 * f0));
    p[QP_MIX] = 63, p[QP_BLEV] = 0, p[QP_BEND] = 127;
    note_buf(p, 60, 60, b0);
    p[QP_BLEV] = 43;                             /* B1 full (B2 0) */
    note_buf(p, 60, 60, b1);
    CHECK(buf_rms(b0, 0, 60 * CTL) < 1.0 && buf_rms(b1, 20 * CTL, 60 * CTL) > 300.0,
          "algo 8: Y (B1 enveloped) %.1f at B LEV 0, %.1f at 43", buf_rms(b0, 0, 60 * CTL), buf_rms(b1, 20 * CTL, 60 * CTL));
    p[QP_BLEV] = 64;                             /* the law: B1 0.55 -> its level 0.30 (-10.4 dB) */
    note_buf(p, 60, 60, b0);
    CHECK(fabs(db(buf_rms(b0, 20 * CTL, 60 * CTL), buf_rms(b1, 20 * CTL, 60 * CTL)) + 10.4) < 1.0,
          "algo 8: Y at B LEV 64 %.1f dB under 43", db(buf_rms(b0, 20 * CTL, 60 * CTL), buf_rms(b1, 20 * CTL, 60 * CTL)));
    p[QP_BLEV] = 43, p[QP_BEND] = 0, p[QP_BDEC] = 20;
    {
        static int32_t b2[400 * CTL];
        note_buf(p, 60, 400, b2);
        CHECK(buf_rms(b2, 350 * CTL, 400 * CTL) < 0.05 * buf_rms(b2, 0, 20 * CTL), "algo 8: Y did not follow ENV B");
    }
}

/* the B LEV law (the manual's graph) at 0 / 43 / 64 / 85 / 127, then squared per operator; in a voice */
static void t_blev(void)
{
    static const struct { int32_t v, u1, u2; } P[] = {{0, 0, 0}, {43, 4096, 0}, {64, 2253, 2048}, {85, 410, 4096},
                                                      {127, 4096, 4096}, {21, 2000, 0}, {106, 2253, 4096}};
    uint32_t k;
    for (k = 0; k < NELEM(P); k++) {
        int32_t u1, u2;
        quad_blev(P[k].v, &u1, &u2);
        CHECK(abs(u1 - P[k].u1) < 8 && abs(u2 - P[k].u2) < 8, "B LEV %d: B1 %d B2 %d (Q12), want %d %d", P[k].v, u1, u2,
              P[k].u1, P[k].u2);
    }
    {   /* monotonic pieces: B1 up, down to 0.1, up; B2 0, up, 1 */
        int32_t v, u1, u2, l1 = -1, l2 = -1, bad = 0;
        for (v = 0; v <= 127; v++) {
            quad_blev(v, &u1, &u2);
            if ((v <= 43 && (u1 < l1 || u2)) || (v > 43 && v <= 85 && (u1 > l1 || u2 < l2)) || (v > 85 && (u1 < l1 || u2 != 4096)))
                bad++;
            l1 = u1, l2 = u2;
        }
        CHECK(!bad, "B LEV law: %d values out of shape", bad);
    }
    CHECK(quad_lev(4096) == 32767 && quad_lev(2048) == 8192 && quad_lev(410) < 400, "the squaring");
    {   /* in a voice: B1's and B2's levels after the attack (velocity 127, END 127) */
        track_t *t = &trk[0];
        int16_t p[QP_NP];
        int32_t out[CTL];
        uint32_t n;
        quad_init_patch(p);
        p[QP_BEND] = 127;
        for (k = 0; k < 5u; k++) {
            int32_t u1, u2;
            p[QP_BLEV] = (int16_t)P[k].v;
            drv_reset(t);
            drv_patch(t, p);
            drv_on(t, 0, 60, 127);
            for (n = 0; n < 10u; n++)
                drv_tick(t, out);
            quad_blev(P[k].v, &u1, &u2);
            CHECK(abs(quad_vs[0][0].lv[1] - quad_lev(u1)) <= quad_lev(u1) / 100 + 1 &&
                      abs(quad_vs[0][0].lv[2] - quad_lev(u2)) <= quad_lev(u2) / 100 + 1,
                  "B LEV %d in a voice: B1 %d B2 %d", P[k].v, quad_vs[0][0].lv[1], quad_vs[0][0].lv[2]);
        }
    }
}

/* HARM per operator: - shapes C only, + A and B1 only, B2 never; the 26 waves are distinct and interpolate */
static void t_harm(void)
{
    static int32_t b0[30 * CTL], b1[30 * CTL];
    int16_t p[QP_NP];
    uint32_t k;
#define HSAME(al, mix, a, b, setup) (quad_init_patch(p), p[QP_ALGO] = (al), p[QP_MIX] = (mix), setup, p[QP_HARM] = (a), \
                                     note_buf(p, 60, 30, b0), p[QP_HARM] = (b), note_buf(p, 60, 30, b1),              \
                                     !memcmp(b0, b1, sizeof b0))
    /* C alone (algo 8, X, A and B off) */
    CHECK(!HSAME(8, -64, 0, -10, (p[QP_ALEV] = 0, p[QP_BLEV] = 0)), "HARM -10: C not shaped");
    CHECK(HSAME(8, -64, 0, 26, (p[QP_ALEV] = 0, p[QP_BLEV] = 0)), "HARM +26: C shaped");
    /* B1 alone (algo 2, Y, B2 off), A alone (algo 5, Y, its modulators off) */
    CHECK(!HSAME(2, 63, 0, 10, (p[QP_BLEV] = 0)), "HARM +10: B1 not shaped");
    CHECK(HSAME(2, 63, 0, -26, (p[QP_BLEV] = 0)), "HARM -26: B1 shaped");
    CHECK(!HSAME(5, 63, 0, 10, (p[QP_BLEV] = 0)), "HARM +10: A not shaped");
    CHECK(HSAME(5, 63, 0, -26, (p[QP_BLEV] = 0)), "HARM -26: A shaped");
    /* A as a modulator (algo 2's X: C under A): + changes C's spectrum through A */
    CHECK(!HSAME(2, -64, 0, 12, (p[QP_ALEV] = 60, p[QP_AEND] = 127)), "HARM +12: A (a modulator) not shaped");
    /* B2 never: algo 8's X = C + B2 (enveloped), A off: HARM + leaves it */
    CHECK(HSAME(8, -64, 0, 26, (p[QP_ALEV] = 0, p[QP_BLEV] = 127, p[QP_BEND] = 127)), "HARM +26: B2 shaped");
    CHECK(HSAME(3, -64, 0, 20, (p[QP_ALEV] = 0)), "HARM +20: algo 3's X (C + B2) shaped");
#undef HSAME
    /* the tables (the first half of a cycle stored, read with a mirror by quad_hw; w the whole, as quad_init built it
     * into POOL until 3y150: the stored half, then the half reversed and negated, then the first point): the
     * fundamental first, each wave its own, a step between two is between them, the stored ends are the odd
     * symmetry's zeros */
    static int16_t w[QUAD_NHARM][QUAD_HN + 1];
    for (k = 0; k < QUAD_NHARM; k++) {
        uint32_t i;
        for (i = 0; i <= QUAD_HN / 2u; i++)
            w[k][i] = QUAD_HARM[k][i];
        for (i = 1; i < QUAD_HN / 2u; i++)
            w[k][QUAD_HN - i] = (int16_t)-QUAD_HARM[k][i];
        w[k][QUAD_HN] = w[k][0];
    }
    for (k = 1; k < QUAD_NHARM; k++) {
        double a1 = 0, d = 0;
        uint32_t i;
        for (i = 0; i < QUAD_HN; i++) {
            a1 += w[k][i] * sin(2.0 * M_PI * i / QUAD_HN);
            d += fabs((double)w[k][i] - w[k - 1][i]);
        }
        CHECK(a1 > 0.3 * 32767 * QUAD_HN / 2 && d / QUAD_HN > 100.0 && QUAD_HARM[k][0] == 0 && QUAD_HARM[k][QUAD_HN / 2u] == 0,
              "HARM wave %u: fundamental %.2f, apart %.0f", k, a1 / (32767.0 * QUAD_HN / 2), d / QUAD_HN);
    }
    {   /* the mirror read: every point of every wave, at fractions across the step and with its neighbour's share, is
         * bit for bit the whole cycle's interpolation (the read before 3y150) */
        static const int32_t FR[] = {0, 1, 2, 255, 256, 8191, 16384, 24575, 32766, 32767};
        static const int32_t SH[] = {0, 1, 128, 16384, 32640, 32767};
        uint32_t i, j, q, bad = 0, n = 0;
        for (k = 0; k < QUAD_NHARM; k++) {
            uint32_t k1 = k < QUAD_NHARM - 1u ? k + 1u : k;
            for (i = 0; i < QUAD_HN; i++)
                for (j = 0; j < sizeof FR / sizeof FR[0]; j++)
                    for (q = 0; q < sizeof SH / sizeof SH[0]; q++) {
                        uint32_t ph = (i << 23) | ((uint32_t)FR[j] << 8) | ((i * 37u + j * 11u + q) & 255u);
                        int32_t fr = FR[j], f = SH[q], x0 = w[k][i], y0 = w[k1][i], x, y, r;
                        x = x0 + (((w[k][i + 1u] - x0) * fr) >> 15);
                        y = y0 + (((w[k1][i + 1u] - y0) * fr) >> 15);
                        r = f ? x + (((y - x) * f) >> 15) : x;
                        bad += quad_hw(QUAD_HARM[k], QUAD_HARM[k1], f, ph) != r;
                        n++;
                    }
        }
        CHECK(!bad, "HARM mirror read: %u of %u points differ from the whole cycles", bad, n);
    }
    {
        int32_t x = quad_hw(QUAD_HARM[19], QUAD_HARM[20], 16384, 0x10000000u), a = quad_hw(QUAD_HARM[19], QUAD_HARM[19], 0, 0x10000000u),
                b = quad_hw(QUAD_HARM[20], QUAD_HARM[20], 0, 0x10000000u);
        CHECK(abs(x - (a + b) / 2) <= 1, "HARM halfway: %d between %d and %d", x, a, b);
    }
}

/* DTUN: A up and B2 down, C and B1 untouched (the increments after a tick); FDBK on the algorithm's operator */
static void t_dtun_fdbk(void)
{
    track_t *t = &trk[0];
    int16_t p[QP_NP];
    int32_t out[CTL];
    uint32_t ph[2][4], k, a;
    for (k = 0; k < 2u; k++) {
        quad_init_patch(p);
        p[QP_DTUN] = (int16_t)(k ? 127 : 0);
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 100);
        drv_tick(t, out);
        memcpy(ph[k], quad_vs[0][0].ph, sizeof ph[k]);
    }
    CHECK(ph[1][0] == ph[0][0] && ph[1][2] == ph[0][2] && ph[1][1] > ph[0][1] && ph[1][3] < ph[0][3],
          "DTUN: C %u/%u B1 %u/%u, A %u > %u, B2 %u < %u", ph[1][0], ph[0][0], ph[1][2], ph[0][2], ph[1][1], ph[0][1],
          ph[1][3], ph[0][3]);
    CHECK(fabs(1200.0 * log2((double)ph[1][1] / ph[0][1]) - 50.0) < 1.5, "DTUN 127: A +%.1f cents",
          1200.0 * log2((double)ph[1][1] / ph[0][1]));
    /* FDBK: with nothing modulating, the feedback memory is the feedback operator's last wave value (FDBK 0) */
    for (a = 0; a < 8u; a++) {
        uint32_t ph1[4], ph2[4], fo = QUAD_ALGO[a].fb, o, hit = 0;
        full_patch(p, a + 1u, 0);
        p[QP_ALEV] = 0, p[QP_BLEV] = 0;
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 100);
        drv_tick(t, out);
        memcpy(ph1, quad_vs[0][0].ph, sizeof ph1);
        drv_tick(t, out);
        memcpy(ph2, quad_vs[0][0].ph, sizeof ph2);
        for (o = 0; o < 4u; o++) {
            uint32_t inc = (ph2[o] - ph1[o]) / CTL;
            if (abs(quad_vs[0][0].fb1 - sine_i(ph2[o] - inc)) < 8)
                hit |= 1u << o;
        }
        CHECK(hit == 1u << fo, "algo %u: the feedback memory is operator %x's, want %u", a + 1, hit, fo);
    }
    {   /* FDBK 35: the feedback operator's wave near a saw (h2 / h1 ~ 0.5, h3 / h1 ~ 0.33); algo 8's Y = B1 */
        static int32_t b[100 * CTL];
        double f0 = 261.63, h1, h2, h3;
        quad_init_patch(p);
        p[QP_ALGO] = 8, p[QP_MIX] = 63, p[QP_FDBK] = 35, p[QP_BLEV] = 43, p[QP_BEND] = 127;   /* Y = B1 at full */
        note_buf(p, 60, 100, b);
        h1 = mag_at(b, 40 * CTL, 100 * CTL, f0);
        h2 = mag_at(b, 40 * CTL, 100 * CTL, 2 * f0) / h1;
        h3 = mag_at(b, 40 * CTL, 100 * CTL, 3 * f0) / h1;
        if (getenv("VERBOSE"))
            printf("  FDBK 35: h2 / h1 %.3f, h3 / h1 %.3f\n", h2, h3);
        CHECK(h2 > 0.4 && h2 < 0.6 && h3 > 0.22 && h3 < 0.4, "FDBK 35: h2 / h1 %.3f h3 / h1 %.3f (a saw: 0.5, 0.33)", h2, h3);
    }
}

/* key scaling: A / B1 / B2 KEY reduce their operator's modulation above C3 (60), raise it below; neutral at 60 */
static void t_keys(void)
{
    track_t *t = &trk[0];
    static const uint8_t NT[3] = {36, 60, 84};
    int16_t p[QP_NP];
    int32_t out[CTL], lv[3][3];
    uint32_t k, n, j;
    quad_init_patch(p);
    p[QP_ALEV] = 64, p[QP_AEND] = 127, p[QP_BLEV] = 64, p[QP_BEND] = 127;
    p[QP_AKEY] = 127, p[QP_B1KEY] = 64;          /* B2 KEY 0 */
    for (k = 0; k < 3u; k++) {
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, NT[k], 127);
        for (n = 0; n < 10u; n++)
            drv_tick(t, out);
        for (j = 0; j < 3u; j++)
            lv[k][j] = quad_vs[0][0].lv[j];
    }
    CHECK(lv[0][0] > lv[1][0] && lv[1][0] > lv[2][0] && abs(lv[2][0] * 4 - lv[1][0]) < lv[1][0] / 50,
          "A KEY 127: %d %d %d at C1 C3 C5 (want x 1/2 an octave)", lv[0][0], lv[1][0], lv[2][0]);
    CHECK(lv[0][1] > lv[1][1] && lv[1][1] > lv[2][1] && lv[2][1] > lv[2][0] * 0 && abs(lv[2][1] * 2 - lv[1][1]) < lv[1][1] / 50,
          "B1 KEY 64: %d %d %d", lv[0][1], lv[1][1], lv[2][1]);
    CHECK(lv[0][2] == lv[1][2] && lv[1][2] == lv[2][2], "B2 KEY 0: %d %d %d (not flat)", lv[0][2], lv[1][2], lv[2][2]);
    {
        int16_t q[QP_NP];
        int32_t l0;
        memcpy(q, p, QP_NP * sizeof(int16_t));
        q[QP_AKEY] = q[QP_B1KEY] = 0;
        drv_reset(t);
        drv_patch(t, q);
        drv_on(t, 0, 60, 127);
        for (n = 0; n < 10u; n++)
            drv_tick(t, out);
        l0 = quad_vs[0][0].lv[0];
        CHECK(abs(l0 - lv[1][0]) <= 1, "KEY neutral at C3: %d, without %d", lv[1][0], l0);
    }
    CHECK(abs(quad_exp2(0) - 32768) <= 8 && abs(quad_exp2(4096) - 65536) <= 16 && abs(quad_exp2(-4096) - 16384) <= 8 &&
              fabs(quad_exp2(2048) / 32768.0 - sqrt(2.0)) < 0.004, "exp2");
}

/* LFO FADE (- in, + out) seen through PAN (the latest voice's), PHRT (OFF keeps the phases of a fresh voice) */
/* a filter switched in or out within a note, and MIX moved, make no step (docs/QUAD.md "Pops"): the largest sample step
 * within a block of each switch against the largest anywhere else in the note. Before 2026-10-16 the multimode came
 * back with its states at 0 (the output to 0 at once: 2 of the 256 Digitone factory sounds, A067, A210), the
 * base-width high-pass let its low band back at once (A126), MIX stepped at a block's edge (A087's RAMP) */
static double sw_ratio(const int16_t *p, uint32_t note, uint32_t bit, uint32_t *nsw)
{
    track_t *t = &trk[0];
    static int32_t o[FS * 6u / 5u + CTL];
    static uint8_t near[FS * 6u / 5u / CTL + 2u];
    int32_t out[CTL], dn = 0, de = 0;
    uint32_t nb = FS * 6u / 5u / CTL, b, i, f0 = 0;
    drv_reset(t);
    drv_patch(t, p);
    drv_on(t, 0, note, 127);
    memset(near, 0, sizeof near);
    *nsw = 0;
    for (b = 0; b < nb; b++) {
        int32_t g = quad_vs[0][0].mixp;
        drv_tick(t, out);
        memcpy(o + b * CTL, out, sizeof out);
        if (b && (bit ? ((quad_vs[0][0].fon ^ f0) & bit) != 0 : quad_vs[0][0].mixp != g)) {
            (*nsw)++;
            near[b] = near[b + 1u] = 1;
            near[b - 1u] = 1;
        }
        f0 = quad_vs[0][0].fon;
    }
    for (i = (uint32_t)(0.05 * FS); i < nb * CTL; i++) {   /* (after the attack) */
        int32_t d = abs(o[i] - o[i - 1u]);
        if (near[i / CTL])
            dn = d > dn ? d : dn;
        else
            de = d > de ? d : de;
    }
    return de ? (double)dn / de : 99;
}
static void t_switch(void)
{
    int16_t p[QP_NP];
    uint32_t n;
    double r;
    quad_init_patch(p);                          /* the multimode: the envelope takes LP12 past its top, then down */
    p[QP_FTYPE] = QF_LP12, p[QP_FREQ] = 60, p[QP_RESO] = 0, p[QP_FDEPTH] = 63;   /* (version-3 units) */
    p[QP_FATK] = 0, p[QP_FDEC] = 40, p[QP_FSUS] = 0;
    r = sw_ratio(p, 36, 1u, &n);
    if (getenv("VERBOSE"))
        printf("  switch: multimode %u x %.2f\n", n, r);
    CHECK(n >= 1u && r < 1.25, "the multimode switched in: %u switches, the step near them x %.2f", n, r);
    quad_init_patch(p);                          /* the base-width high-pass: an LFO takes BASE through 0, four times */
    p[QP_FTYPE] = QF_OFF;
    p[QP_LFO(0, QL_DEST)] = QD_BASE, p[QP_LFO(0, QL_WAVE)] = QW_TRI, p[QP_LFO(0, QL_SPEED)] = 63;
    p[QP_LFO(0, QL_MULT)] = 4, p[QP_LFO(0, QL_DEPTH)] = 10;
    r = sw_ratio(p, 36, 2u, &n);
    if (getenv("VERBOSE"))
        printf("  switch: high-pass %u x %.2f\n", n, r);
    CHECK(n >= 4u && r < 1.5, "the high-pass switched: %u switches, the step near them x %.2f", n, r);
    p[QP_BASE] = 20, p[QP_WIDTH] = 100;          /* .. its low-pass: BASE + WIDTH through the top */
    p[QP_LFO(0, QL_DEST)] = QD_WIDTH, p[QP_LFO(0, QL_DEPTH)] = 63;
    r = sw_ratio(p, 36, 4u, &n);
    if (getenv("VERBOSE"))
        printf("  switch: low-pass %u x %.2f\n", n, r);
    CHECK(n >= 4u && r < 1.5, "the low-pass switched: %u switches, the step near them x %.2f", n, r);
    quad_init_patch(p);                          /* MIX by a square LFO: X (C) and Y (B1 an octave up) in turns */
    p[QP_FTYPE] = QF_OFF, p[QP_RB1] = 4;
    p[QP_LFO(0, QL_DEST)] = QD_MIX, p[QP_LFO(0, QL_WAVE)] = QW_SQR, p[QP_LFO(0, QL_SPEED)] = 63;
    p[QP_LFO(0, QL_MULT)] = 4, p[QP_LFO(0, QL_DEPTH)] = 63;
    r = sw_ratio(p, 84, 0u, &n);
    if (getenv("VERBOSE"))
        printf("  switch: MIX %u x %.2f\n", n, r);
    CHECK(n >= 4u && r < 1.5, "MIX by a square: %u changes, the step near them x %.2f", n, r);
}

static void t_fade_phrt(void)
{
    track_t *t = &trk[0];
    int16_t p[QP_NP];
    int32_t out[CTL], pan[3][2];
    uint32_t k, n;
    static const int16_t FD[3] = {0, -30, 30};
    for (k = 0; k < 3u; k++) {
        quad_init_patch(p);
        p[QP_LFO(0, QL_DEST)] = QD_PAN, p[QP_LFO(0, QL_WAVE)] = QW_SQR, p[QP_LFO(0, QL_SPEED)] = 0;
        p[QP_LFO(0, QL_TRIG)] = QT_TRIG, p[QP_LFO(0, QL_DEPTH)] = 63, p[QP_LFO(0, QL_FADE)] = FD[k];
        drv_reset(t);
        drv_patch(t, p);
        drv_on(t, 0, 60, 100);
        drv_tick(t, out);
        pan[k][0] = quad_pan_off[0];
        for (n = 0; n < 32767u * 65536u / QUAD_T_FADE[30] + 10u; n++)   /* (FADE 30's ticks) */
            drv_tick(t, out);
        pan[k][1] = quad_pan_off[0];
    }
    CHECK(pan[0][0] > 55 && pan[0][1] > 55, "FADE 0: %d %d", pan[0][0], pan[0][1]);
    CHECK(pan[1][0] < 5 && pan[1][1] > 55, "FADE -30 (in): %d -> %d", pan[1][0], pan[1][1]);
    CHECK(pan[2][0] > 55 && pan[2][1] < 2, "FADE +30 (out): %d -> %d", pan[2][0], pan[2][1]);
    /* PHRT: a fresh voice (the last note fully released) */
    {
        static const uint8_t MASK[QR_N] = {0, 0xF, 0x1, 0xE, 0xA};
        for (k = 0; k < QR_N; k++) {
            uint32_t o, z = 0, ph0[4];
            quad_init_patch(p);
            p[QP_PHRT] = (int16_t)k, p[QP_EREL] = 0;
            drv_reset(t);
            drv_patch(t, p);
            drv_on(t, 0, 60, 100);
            for (n = 0; n < 7u; n++)
                drv_tick(t, out);
            t->v[0].gate = 0;
            for (n = 0; n < 2000u && drv_tick(t, out); n++)
                ;
            memcpy(ph0, quad_vs[0][0].ph, sizeof ph0);
            CHECK(!t->v[0].active && !quad_vs[0][0].live, "PHRT %s: the voice did not end", N_QUAD_PHRT[k]);
            drv_on(t, 0, 64, 100);
            for (o = 0; o < 4u; o++)
                z |= (quad_vs[0][0].ph[o] == 0 ? 1u : 0u) << o;
            CHECK(z == MASK[k] && (k != QR_OFF || !memcmp(ph0, quad_vs[0][0].ph, sizeof ph0)),
                  "PHRT %s: operators at 0 %x, want %x", N_QUAD_PHRT[k], z, MASK[k]);
        }
    }
}

/* the destinations appended 2026-10-14 (the Digitone's full list, docs/QUAD.md "LFO"): each one by an LFO 2 held at
 * +full (SQR, SPEED 0, TRIG: +32255 = +125 of a 0..127 range) is the stored value moved by that much (rendered
 * bit-identically to the patch that stores it), DEPTH 0 changes nothing; the AB macros = the A and the B destination
 * together; LFO 1's own parameters seen through LFO 1 -> PAN; DRIVE and the sends: the part's offsets */
#define DN_T 120u
static int32_t dn_a[DN_T * CTL], dn_b[DN_T * CTL];
static int16_t dn_pan[2][DN_T];
static void dn_hold(int16_t *p, uint32_t k, uint32_t d, int32_t dep)
{
    p[QP_LFO(k, QL_DEST)] = (int16_t)d, p[QP_LFO(k, QL_WAVE)] = QW_SQR, p[QP_LFO(k, QL_SPEED)] = 0;
    p[QP_LFO(k, QL_TRIG)] = QT_TRIG, p[QP_LFO(k, QL_DEPTH)] = (int16_t)dep;
}
static int dn_v4;                                /* dn_pans: p is a version-4 patch (else version 3's units) */
static void dn_pans(const int16_t *p, int16_t *pan)
{
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t n;
    drv_reset(t);
    if (dn_v4)
        drv_patch4(t, p);
    else
        drv_patch(t, p);
    drv_on(t, 0, 60, 100);
    for (n = 0; n < DN_T; n++) {
        drv_tick(t, out);
        pan[n] = quad_pan_off[0];
    }
}
static void dn_base(int16_t *p)                   /* audible everywhere: filter, operators, envelopes */
{
    full_patch(p, 1, 0);
    p[QP_FTYPE] = QF_LP12, p[QP_FREQ] = 40, p[QP_FDEPTH] = 50, p[QP_FDLY] = 1, p[QP_FATK] = 20, p[QP_FDEC] = 40;
    p[QP_AATK] = p[QP_BATK] = 10, p[QP_ADEC] = p[QP_BDEC] = 50, p[QP_AEND] = p[QP_BEND] = 40;
    p[QP_ALEV] = p[QP_BLEV] = 60, p[QP_ADLY] = p[QP_BDLY] = 1;
    p[QP_RC] = 0, p[QP_RA] = 0, p[QP_RB1] = 0, p[QP_RB2] = 0;
}
static void t_dests(void)
{
    int16_t p[QP_NP], q[QP_NP];
    uint32_t k, n, seen[QD_N] = {0};
    CHECK(QD_N == 58 && QD_N_V3A == 40u && QD_PAN == 39 && QD_FDLY == 57, "DEST: 40 kept + 18 appended");
    for (k = 0; k < QD_N; k++)
        seen[QUAD_DEST_ORDER[k]]++;
    for (k = 0; k < QD_N; k++)
        CHECK(seen[k] == 1u, "the display order: DEST %u (%s) %u times", k, N_QUAD_DEST[k], seen[k]);
    CHECK(QUAD_DEST_ORDER[0] == QD_NONE && QUAD_DEST_ORDER[1] == QD_L1SPD && QUAD_DEST_ORDER[8] == QD_ALGO &&
              QUAD_DEST_ORDER[26] == QD_PITCH && QUAD_DEST_ORDER[QD_N - 1] == QD_FDLY, "the display order: the Digitone's");
    /* the stored-value equivalents (the held +125 on a 0..127, +17 of 18, +62 of 63) */
    {
        static const struct { uint8_t d; uint8_t i[4]; int16_t v[4]; } EQ[] = {
            {QD_ABLEV, {QP_ALEV, QP_BLEV, 0xFF, 0}, {127, 127, 0, 0}},
            {QD_ABATK, {QP_AATK, QP_BATK, 0xFF, 0}, {127, 127, 0, 0}},
            {QD_ABDEC, {QP_ADEC, QP_BDEC, 0xFF, 0}, {127, 127, 0, 0}},
            {QD_ABEND, {QP_AEND, QP_BEND, 0xFF, 0}, {127, 127, 0, 0}},
            {QD_ABDLY, {QP_ADLY, QP_BDLY, 0xFF, 0}, {126, 126, 0, 0}},
            {QD_RALL, {QP_RC, QP_RA, QP_RB1, QP_RB2}, {17, 62, 17, 17}},
            {QD_FDLY, {QP_FDLY, 0xFF, 0, 0}, {126, 0, 0, 0}},
        };
        for (k = 0; k < NELEM(EQ); k++) {
            uint32_t j, diff;
            dn_base(p);
            note_buf(p, 60, DN_T, dn_b);           /* unmodulated */
            memcpy(q, p, QP_NP * sizeof(int16_t));
            dn_hold(p, 1, EQ[k].d, 63);
            note_buf(p, 60, DN_T, dn_a);
            for (j = 0; j < 4u && EQ[k].i[j] != 0xFFu; j++)
                q[EQ[k].i[j]] = EQ[k].v[j];
            diff = memcmp(dn_a, dn_b, sizeof dn_a) != 0;
            note_buf(q, 60, DN_T, dn_b);
            CHECK(diff && !memcmp(dn_a, dn_b, sizeof dn_a), "DEST %s: not the stored value moved by the LFO (%u)",
                  N_QUAD_DEST[EQ[k].d], diff);
            dn_hold(p, 1, EQ[k].d, 0);
            note_buf(p, 60, DN_T, dn_a);
            dn_base(q);
            note_buf(q, 60, DN_T, dn_b);
            CHECK(!memcmp(dn_a, dn_b, sizeof dn_a), "DEST %s at DEPTH 0: the sound changed", N_QUAD_DEST[EQ[k].d]);
        }
    }
    /* the AB macros = A's and B's destination together (an LFO each, a moving sine) */
    {
        static const uint8_t AB[5][3] = {{QD_ABLEV, QD_ALEV, QD_BLEV}, {QD_ABATK, QD_AATK, QD_BATK},
            {QD_ABDEC, QD_ADEC, QD_BDEC}, {QD_ABEND, QD_AEND, QD_BEND}, {QD_ABDLY, QD_ADLY, QD_BDLY}};
        for (k = 0; k < 5u; k++) {
            uint32_t j;
            dn_base(p);
            p[QP_ALEV] = p[QP_BLEV] = 80;
            memcpy(q, p, QP_NP * sizeof(int16_t));
            dn_hold(p, 1, AB[k][0], -40);
            dn_hold(q, 1, AB[k][1], -40);
            dn_hold(q, 2, AB[k][2], -40);
            for (j = 1; j < 3u; j++) {
                p[QP_LFO(j, QL_WAVE)] = q[QP_LFO(j, QL_WAVE)] = QW_SINE;
                p[QP_LFO(j, QL_SPEED)] = q[QP_LFO(j, QL_SPEED)] = 20;
            }
            note_buf(p, 60, DN_T, dn_a);
            note_buf(q, 60, DN_T, dn_b);
            CHECK(!memcmp(dn_a, dn_b, sizeof dn_a), "DEST %s: not %s + %s", N_QUAD_DEST[AB[k][0]], N_QUAD_DEST[AB[k][1]],
                  N_QUAD_DEST[AB[k][2]]);
        }
    }
    /* LFO 1's parameters by LFO 2 (and 3): LFO 1 -> PAN, the trace = LFO 1 storing the moved value */
    {
        static const struct { uint8_t d, f; int16_t base, want; } L1[] = {
            {QD_L1SPD, QL_SPEED, 0, 63}, {QD_L1MUL, QL_MULT, 3, 11}, {QD_L1FAD, QL_FADE, 0, 63},
            {QD_L1WAV, QL_WAVE, QW_TRI, QW_EXP}, {QD_L1SPH, QL_PHASE, 0, 125}, {QD_L1MOD, QL_TRIG, QT_TRIG, QT_HALF},
            {QD_L1DEP, QL_DEPTH, 0, 63},
        };
        for (k = 0; k < NELEM(L1); k++) {
            uint32_t lfo;
            for (lfo = 1; lfo < 3u; lfo++) {
                quad_init_patch(p);
                p[QP_LFO(0, QL_DEST)] = QD_PAN, p[QP_LFO(0, QL_WAVE)] = QW_TRI, p[QP_LFO(0, QL_SPEED)] = 8;
                p[QP_LFO(0, QL_TRIG)] = QT_TRIG, p[QP_LFO(0, QL_DEPTH)] = 40, p[QP_LFO(0, QL_MULT)] = 3;
                p[QP_LFO(0, L1[k].f)] = L1[k].base;
                memcpy(q, p, QP_NP * sizeof(int16_t));
                q[QP_LFO(0, L1[k].f)] = L1[k].want;
                dn_hold(p, lfo, L1[k].d, 63);
                dn_pans(p, dn_pan[0]);
                if (quad_hires(QP_LFO(0, L1[k].f))) {   /* SPEED, DEPTH (hundredths): moved to their top, 63.99 */
                    to_v4(q, q);
                    q[QP_LFO(0, L1[k].f)] = quad_range(QP_LFO(0, L1[k].f)).max;
                    dn_v4 = 1;
                }
                dn_pans(q, dn_pan[1]);
                dn_v4 = 0;
                if (quad_hires(QP_LFO(0, L1[k].f))) {   /* (back to version 3's: p without LFO lfo) */
                    memcpy(q, p, QP_NP * sizeof(int16_t));
                    q[QP_LFO(lfo, QL_DEST)] = QD_NONE;
                }
                CHECK(!memcmp(dn_pan[0], dn_pan[1], DN_T), "DEST %s (LFO %u): LFO 1 not moved to %d", N_QUAD_DEST[L1[k].d],
                      lfo + 1u, L1[k].want);
                p[QP_LFO(lfo, QL_DEPTH)] = 0;
                q[QP_LFO(0, L1[k].f)] = L1[k].base;
                dn_pans(p, dn_pan[0]);
                dn_pans(q, dn_pan[1]);
                CHECK(!memcmp(dn_pan[0], dn_pan[1], DN_T), "DEST %s at DEPTH 0: LFO 1 moved", N_QUAD_DEST[L1[k].d]);
            }
            quad_init_patch(p);                  /* LFO 1 on its own parameter: nothing */
            dn_hold(p, 0, L1[k].d, 63);
            dn_pans(p, dn_pan[0]);
            for (n = 0; n < DN_T; n++)
                CHECK(dn_pan[0][n] == 0, "LFO 1 -> %s: it moved something (pan %d)", N_QUAD_DEST[L1[k].d], dn_pan[0][n]);
        }
        /* FREE: SPEED moved runs LFO 1 at the moved rate (the voice's lead on the part's phase) */
        quad_init_patch(p);
        p[QP_LFO(0, QL_DEST)] = QD_PAN, p[QP_LFO(0, QL_WAVE)] = QW_TRI, p[QP_LFO(0, QL_SPEED)] = 0;
        p[QP_LFO(0, QL_TRIG)] = QT_FREE, p[QP_LFO(0, QL_DEPTH)] = 63;
        to_v4(p, q);
        q[QP_LFO(0, QL_SPEED)] = 6399;
        dn_hold(p, 1, QD_L1SPD, 63);
        dn_pans(p, dn_pan[0]);
        dn_v4 = 1;
        dn_pans(q, dn_pan[1]);
        dn_v4 = 0;
        CHECK(!memcmp(dn_pan[0], dn_pan[1], DN_T) && dn_pan[0][DN_T / 4] != dn_pan[0][0],
              "L1SPD on a FREE LFO 1: %d %d, want %d %d", dn_pan[0][0], dn_pan[0][DN_T / 4], dn_pan[1][0],
              dn_pan[1][DN_T / 4]);
    }
    /* DRIVE and the sends: the part's, offset by the latest voice's LFOs, read by fx.c */
    {
        static const uint8_t FXD[4] = {QD_DRIVE, QD_SREV, QD_SDLY, QD_SCHO};
        track_t *t = &trk[0];
        int32_t c, d, r;
        for (k = 0; k < 4u; k++) {
            int32_t dep;
            for (dep = 63; dep >= -64; dep -= 127) {
                uint32_t j;
                int32_t lo = dep > 0 ? 127 : 0;
                quad_init_patch(p);
                dn_hold(p, 1, FXD[k], dep);
                dn_pans(p, dn_pan[0]);
                for (j = 0; j < 4u; j++)
                    CHECK(quad_fx_off[0][j] == (j == k ? (dep > 0 ? 125 : -127) : 0), "DEST %s depth %d: offset %u = %d",
                          N_QUAD_DEST[FXD[k]], dep, j, quad_fx_off[0][j]);
                t->p[P_DIST] = 10, t->p[P_REV] = 20, t->p[P_DLY] = 30, t->p[P_CHOR] = 40;
                c = 40 * 258, d = 30 * 258, r = 20 * 258;
                quad_sends(t, &c, &d, &r);
                CHECK(quad_drive(t, 10) == (k ? 10 : lo) && r == (k == 1u ? lo : 20) * 258 &&
                          d == (k == 2u ? lo : 30) * 258 && c == (k == 3u ? lo : 40) * 258,
                      "DEST %s depth %d: drive %d sends %d %d %d", N_QUAD_DEST[FXD[k]], dep, quad_drive(t, 10), r / 258,
                      d / 258, c / 258);
            }
            quad_init_patch(p);
            dn_hold(p, 1, FXD[k], 0);
            dn_pans(p, dn_pan[0]);
            t->p[P_DIST] = 10, t->p[P_REV] = 20, t->p[P_DLY] = 30, t->p[P_CHOR] = 40;
            c = 40 * 258, d = 30 * 258, r = 20 * 258;
            quad_sends(t, &c, &d, &r);
            CHECK(!quad_fx_off[0][0] && !quad_fx_off[0][1] && !quad_fx_off[0][2] && !quad_fx_off[0][3] &&
                      quad_drive(t, 10) == 10 && c == 40 * 258 && d == 30 * 258 && r == 20 * 258,
                  "DEST %s at DEPTH 0: an offset", N_QUAD_DEST[FXD[k]]);
            dn_hold(p, 1, FXD[k], 63);
            dn_pans(p, dn_pan[0]);
            t->engine = 0;                       /* not on FM TONE: the part's own values */
            CHECK(quad_drive(t, 10) == 10, "DRIVE off FM TONE");
            t->engine = ENGI_QUAD;
        }
    }
    /* a version-3 blob with an appended destination: this firmware takes it; an older one (40 destinations) refuses it
     * (quad_blob_ok's range: the init patch, the slot's bytes untouched) */
    {
        uint8_t b[QUAD_BLOB];
        quad_init_patch(p);
        p[QP_LFO(1, QL_DEST)] = QD_FDLY;
        quad_pack(p, b);
        CHECK(b[1] == 5u && quad_blob_ok(b) && quad_unpack(b, q) && q[QP_LFO(1, QL_DEST)] == QD_FDLY,
              "a blob with DEST F DLY: version 5, taken");
        CHECK(b[v4_off(QP_LFO(1, QL_DEST))] > QD_N_V3A - 1u && !old_fw_takes(b), "an older firmware refuses it");
        pack_v3(p, b);
        CHECK(quad_blob_ok(b) && quad_unpack(b, q) && q[QP_LFO(1, QL_DEST)] == QD_FDLY, "a version-3 blob with F DLY");
    }
}

/* the hi-res values' fine part reaches the sound: FREQ 42.00 and 42.01 render differently (and closer to each other
 * than 42 and 43), so do DTUNE 30.00 / 30.50, BASE, HARM, RESO (AMP REL and LEVEL: whole numbers since version 5) */
static void t_fine(void)
{
    static int32_t a[200 * CTL], b[200 * CTL], c[200 * CTL];
    static const struct { uint8_t i; int16_t v0, v1; const char *what; } F[] = {
        {QP_FREQ, 4200, 4201, "FREQ 42.00 / 42.01"}, {QP_DTUN, 3000, 3050, "DTUNE 30.00 / 30.50"},
        {QP_BASE, 4000, 4050, "BASE 40.00 / 40.50"}, {QP_HARM, -700, -750, "HARM -7.00 / -7.50"},
        {QP_RESO, 6000, 6050, "RESO 60.00 / 60.50"}};
    int16_t p[QP_NP];
    uint32_t k, n;
    for (k = 0; k < NELEM(F); k++) {
        double dab = 0, dac = 0;
        int16_t v2 = (int16_t)(F[k].v0 + (F[k].v0 < 0 ? -100 : 100));
        for (n = 0; n < 3u; n++) {
            track_t *t = &trk[0];
            int32_t *o = n == 0u ? a : n == 1u ? b : c;
            uint32_t j;
            quad_init_patch4(p);
            p[QP_ALGO] = 1, p[QP_ALEV] = 90, p[QP_FDBK] = 20, p[QP_FTYPE] = QF_LP12, p[QP_FREQ] = 7000, p[QP_RESO] = 3000;
            p[QP_DTUN] = 2000, p[QP_HARM] = -500;
            p[F[k].i] = n == 0u ? F[k].v0 : n == 1u ? F[k].v1 : v2;
            drv_reset(t);
            drv_patch4(t, p);
            drv_on(t, 0, 48, 100);
            for (j = 0; j < 200u; j++) {
                if (j == 60u)
                    t->v[0].gate = 0;            /* (the release: AMP REL) */
                drv_tick(t, o + j * CTL);
            }
        }
        for (n = 0; n < 200u * CTL; n++) {
            dab += fabs((double)a[n] - b[n]);
            dac += fabs((double)a[n] - c[n]);
        }
        CHECK(dab > 0 && dab < dac, "%s: renders the same (|a-b| %.0f, a whole step %.0f)", F[k].what, dab, dac);
    }
}

/* version 5 (the owner's corrections, 2026-10-14): the ranges; a version-4 blob converted (FREQ DTUN RESO BASE
 * clamped to 127.00, AMP REL 126.85 -> 126, 127.xx -> 127 INF, LEVEL rounded); AMP DEC 127 = INF holds the level where
 * the decay starts (not SUS); HARM / DTUN 12.34 kept through a save and a load (the blob), the macro its whole part */
static void v4_pack(const int16_t *p4, uint8_t *b)  /* a version-4 blob of a version-4 patch (as 3y61 wrote it) */
{
    uint32_t i, o = 2;
    memset(b, 0, QUAD_BLOB);
    b[0] = QUAD_MAGIC;
    b[1] = 4u;
    for (i = 0; i < QP_NP5; i++) {
        uint32_t u = (uint32_t)(p4[i] - quad_range_v4(i).min);
        b[o++] = (uint8_t)u;
        if (quad_hires_v4(i))
            b[o++] = (uint8_t)(u >> 8);
    }
}
static void t_v5(void)
{
    static const struct { uint8_t i; int16_t lo, hi, def; } RG[] = {
        {QP_FREQ, 0, 12700, 12700}, {QP_DTUN, 0, 12700, 0}, {QP_RESO, 0, 12700, 0}, {QP_BASE, 0, 12700, 0},
        {QP_HARM, -2600, 2600, 0}, {QP_FDEPTH, -6400, 6399, 0}, {QP_EREL, 0, 127, 24}, {QP_EDEC, 0, 127, 32},
        {QP_LEVEL, 0, 127, 110}, {QP_FDBK, 0, 127, 0}};
    static const struct { int16_t rel, lev, dec, freq; int16_t wrel, wlev, wfreq; } CV[] = {
        {12685, 11040, 127, 12799, 126, 110, 12700}, {12700, 11050, 64, 12750, 127, 111, 12700},
        {12799, 0, 0, 4201, 127, 0, 4201}, {12649, 12799, 126, 0, 126, 127, 0}, {4049, 4950, 10, 12700, 40, 50, 12700},
        {0, 49, 5, 1, 0, 0, 1}};
    track_t *t = &trk[0];
    int16_t p[QP_NP], q[QP_NP];
    uint8_t b[QUAD_BLOB];
    int32_t out[CTL];
    uint32_t k, n;
    for (k = 0; k < NELEM(RG); k++) {
        quad_rng_t r = quad_range(RG[k].i);
        CHECK(r.min == RG[k].lo && r.max == RG[k].hi && r.def == RG[k].def, "v5: value %u range %d..%d def %d", RG[k].i,
              r.min, r.max, r.def);
    }
    CHECK(QUAD_PAGES[12].col[1].fmt == F_INTI && QUAD_PAGES[12].col[3].fmt == F_INTI &&
              QUAD_PAGES[13].col[0].fmt == F_INT && !quad_hires(QP_EREL) && !quad_hires(QP_LEVEL),
          "v5: AMP DEC / REL F_INTI, LEVEL F_INT");
    for (k = 0; k < NELEM(CV); k++) {           /* version 4 -> 5 */
        int16_t p4[QP_NP];
        for (n = 0; n < QP_NP; n++)
            p4[n] = quad_range_v4(n).def;
        p4[QP_EREL] = CV[k].rel, p4[QP_LEVEL] = CV[k].lev, p4[QP_EDEC] = CV[k].dec, p4[QP_FREQ] = CV[k].freq;
        p4[QP_DTUN] = CV[k].freq, p4[QP_RESO] = CV[k].freq, p4[QP_BASE] = CV[k].freq, p4[QP_HARM] = -1234;
        v4_pack(p4, b);
        CHECK(quad_blob_ok(b) && quad_unpack(b, q) == 1 && q[QP_EREL] == CV[k].wrel && q[QP_LEVEL] == CV[k].wlev &&
                  q[QP_EDEC] == CV[k].dec && q[QP_FREQ] == CV[k].wfreq && q[QP_DTUN] == CV[k].wfreq &&
                  q[QP_RESO] == CV[k].wfreq && q[QP_BASE] == CV[k].wfreq && q[QP_HARM] == -1234,
              "v4 %u: REL %d LEVEL %d FREQ %d -> %d %d %d", k, CV[k].rel, CV[k].lev, CV[k].freq, q[QP_EREL], q[QP_LEVEL],
              q[QP_FREQ]);
    }
    {   /* version 3 -> 5: REL 127 INF stays, LEVEL 100, DTUN 127 -> 127.00 */
        t_init3(p);
        p[QP_EREL] = 127, p[QP_LEVEL] = 100, p[QP_DTUN] = 127, p[QP_EDEC] = 127;
        pack_v3(p, b);
        CHECK(quad_unpack(b, q) == 1 && q[QP_EREL] == 127 && q[QP_LEVEL] == 100 && q[QP_DTUN] == 12700 &&
                  q[QP_EDEC] == 127, "v3 -> v5: REL %d LEVEL %d DTUN %d", q[QP_EREL], q[QP_LEVEL], q[QP_DTUN]);
    }
    {   /* AMP DEC INF: the level held at 1 (the decay's start), not moving to SUS; DEC 126 decays to SUS */
        uint32_t d;
        for (d = 126; d <= 127u; d++) {
            int32_t e0, e1;
            quad_init_patch4(p);
            p[QP_EATK] = 0, p[QP_EDEC] = (int16_t)d, p[QP_ESUS] = 20, p[QP_EREL] = 40;
            drv_reset(t);
            drv_patch4(t, p);
            drv_on(t, 0, 60, 100);
            for (n = 0; n < FS / 10u / CTL; n++)
                drv_tick(t, out);
            e0 = quad_vs[0][0].env[3] >> 9;
            for (n = 0; n < 2u * FS / CTL; n++)
                drv_tick(t, out);
            e1 = quad_vs[0][0].env[3] >> 9;
            if (d == 127u)
                CHECK(e0 >= 32760 && e1 == e0 && quad_vs[0][0].stage[3] == 3u, "DEC INF: amp %d -> %d after 2 s", e0, e1);
            else
                CHECK(e1 < e0, "DEC 126: no decay (%d -> %d)", e0, e1);
        }
        t->v[0].gate = 0;                        /* DEC INF, then the note-off: REL 40 releases */
        for (n = 0; n < 4u * FS / CTL && drv_tick(t, out); n++)
            ;
        CHECK(!t->v[0].active, "DEC INF: the release did not end the voice");
    }
    {   /* HARM / DTUN 12.34: the deep value, the macro (its whole part), a save (blob) and a load keep 12.34 */
        track_t *u = &trk[1];
        drv_reset(t);
        quad_blob_preset(t, 0);
        quad_set(t, 1, 0, 1234);                 /* SYN 1+: HARM, DTUNE */
        quad_set(t, 1, 1, 1234);
        quad_block(t);
        CHECK(quad_get(t, 1, 0) == 1234 && quad_get(t, 1, 1) == 1234 && t->p[P_E4] == 12 && t->p[P_E5] == 12,
              "HARM / DTUN 12.34: deep %d %d, macro %d %d", quad_get(t, 1, 0), quad_get(t, 1, 1), t->p[P_E4], t->p[P_E5]);
        quad_blob_get(t, b);
        memset(u, 0, sizeof *u);
        u->eng_req = u->engine = ENGI_QUAD;
        u->p[P_E4] = 12, u->p[P_E5] = 12;        /* (a record's macro bytes: the whole part) */
        quad_blob_set(u, b);
        quad_block(u);
        CHECK(quad_patch[1][QP_HARM] == 1234 && quad_patch[1][QP_DTUN] == 1234 && u->p[P_E4] == 12,
              "HARM / DTUN 12.34 after a save and a load: %d %d", quad_patch[1][QP_HARM], quad_patch[1][QP_DTUN]);
    }
}

int main(int argc, char **argv)
{
    t_blob();
    t_blob6();
    t_v5();
    t_pages();
    t_set_macros();
    t_presets();
    t_ratios();
    t_ratio_b();
    t_blob_v2();
    t_ratio_b_heard();
    t_algo_table();
    t_golden();
    t_fine();
    t_env();
    t_rel_inf();
    t_filters();
    t_lfo();
    t_render();
    t_dc();
    t_routing();
    t_carriers();
    t_blev();
    t_harm();
    t_dtun_fdbk();
    t_keys();
    t_fade_phrt();
    t_switch();
    t_dests();
    t_drive_pre();
    if (argc > 1 && !strcmp(argv[1], "cpu"))
        t_cpu();
    printf("cr_quad_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
