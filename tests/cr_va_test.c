/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the VA engine (firmware/src/eng_va.c, docs/VA.md), no HAL: the engine on core.h and dsp.c with a
 * minimal voice driver (voice.c's control tick for an ownenv engine: done, then render).
 *   sh tests/run_cr_tests.sh
 * Checks: blob pack / unpack round trip of random patches; a bad blob -> the init patch; the deep page table against
 * the patch ranges; set() clamps; the macros both ways (P_E -> patch in va_block, set -> P_E); every preset's blob
 * valid and its macros as its preset_t; the matrix at its extremes (no overflow); the envelopes reach sustain and
 * end; the LFO SYNC divisions; 1 s of a 6-note chord on every preset: no int32 wrap, peak < 0.9 FS at the chord
 * part's default level. */
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
#define FELUCCA_VA 1
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/eng_va.c"

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

/* ------------------------------------------------------- voice driver --- */
static void drv_reset(track_t *t)
{
    memset(t, 0, sizeof *t);
    memset(va_vs, 0, sizeof va_vs);
    memset(va_lfo, 0, sizeof va_lfo);
    t->p[P_VOICE] = V_POLY;
    song.g[G_BPM] = 120;
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
    va_note_on(t, v);
}
/* one control tick of the part: block, then each voice (done, then render) into out; returns the voices rendered */
static uint32_t drv_tick(track_t *t, int32_t *out)
{
    uint32_t i, nr = 0;
    memset(out, 0, CTL * sizeof *out);
    va_block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        if (!v->active)
            continue;
        if (va_done(t, v)) {
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
        va_render(t, v, out, CTL, &m);
        nr++;
    }
    return nr;
}

/* ------------------------------------------------------------- checks --- */
static void t_blob(void)
{
    uint8_t b[VA_BLOB], b2[VA_BLOB];
    int8_t p[VA_NP], q[VA_NP], init[VA_NP];
    uint32_t n, i;
    va_init_patch(init);
    for (n = 0; n < 2000u; n++) {
        for (i = 0; i < VA_NP; i++) {
            va_rng_t r = va_range(i);
            p[i] = (int8_t)(r.min + (int32_t)(rnd() % (uint32_t)(r.max - r.min + 1)));
        }
        va_pack(p, b);
        CHECK(va_blob_ok(b), "random patch %u: its blob is not valid", n);
        CHECK(va_unpack(b, q) == 1 && !memcmp(p, q, VA_NP), "random patch %u: round trip differs", n);
        va_pack(q, b2);
        CHECK(!memcmp(b, b2, VA_BLOB), "random patch %u: blob round trip differs", n);
        for (i = 2; i < 2u + VA_NP; i++)
            CHECK(b[i] < 128u, "blob byte %u not 7-bit", i);
    }
    /* bad blobs -> init */
    va_pack(p, b);
    CHECK(va_unpack(0, q) == 0 && !memcmp(q, init, VA_NP), "NULL blob: not the init patch");
    memcpy(b2, b, VA_BLOB);
    b2[0] ^= 1;
    CHECK(va_unpack(b2, q) == 0 && !memcmp(q, init, VA_NP), "bad magic: not the init patch");
    memcpy(b2, b, VA_BLOB);
    b2[1] = VA_VER + 1u;
    CHECK(va_unpack(b2, q) == 0 && !memcmp(q, init, VA_NP), "bad version: not the init patch");
    memcpy(b2, b, VA_BLOB);
    b2[2 + VA_OSC(0, VO_WAVE)] = 99;
    CHECK(va_unpack(b2, q) == 0 && !memcmp(q, init, VA_NP), "out-of-range value: not the init patch");
    memcpy(b2, b, VA_BLOB);
    b2[VA_BLOB - 1u] = 1;
    CHECK(va_unpack(b2, q) == 0 && !memcmp(q, init, VA_NP), "padding not 0: not the init patch");
    memset(b2, 0xFF, sizeof b2);
    CHECK(va_unpack(b2, q) == 0, "erased flash (0xFF): taken as a patch");
    {   /* blob_set on a track: bad -> init, macros out */
        static track_t t0;
        memset(&t0, 0, sizeof t0);
        trk[0] = t0;
        b2[0] = 0;
        va_blob_set(&trk[0], b2);
        CHECK(!memcmp(va_patch[0], init, VA_NP), "blob_set(bad): not the init patch");
        CHECK(trk[0].p[P_E0] == 100 && trk[0].p[P_E4] == 64, "blob_set(bad): macros CUT %d MIX %d", trk[0].p[P_E0],
              trk[0].p[P_E4]);
        va_blob_set(&trk[0], 0);
        CHECK(!memcmp(va_patch[0], init, VA_NP), "blob_set(0): not the init patch");
    }
}

static void t_pages(void)
{
    uint32_t pg, c, i, used[VA_NP] = {0};
    CHECK(VA_DEEP.npages == 31u, "pages %u", VA_DEEP.npages);
    CHECK(VA_DEEP.section[0] == 0 && VA_DEEP.section[1] == 8 && VA_DEEP.section[2] == 10 && VA_DEEP.section[3] == 18 &&
              VA_DEEP.section[4] == 23 && VA_DEEP.section[5] == 0xFF, "sections");
    for (pg = 0; pg < VA_NPAGES; pg++) {
        CHECK(strlen(VA_PAGES[pg].title) <= 7u, "page %u title too long", pg);
        for (c = 0; c < 4u; c++) {
            const param_desc_t *d = &VA_PAGES[pg].col[c];
            i = VA_MAP[pg][c];
            CHECK((i == VA_X) == (d->label == 0), "page %u col %u: label and map disagree", pg, c);
            if (i == VA_X)
                continue;
            used[i]++;
            CHECK(d->min == va_range(i).min && d->max == va_range(i).max, "page %s col %u: range %d..%d, patch %d..%d",
                  VA_PAGES[pg].title, c, d->min, d->max, va_range(i).min, va_range(i).max);
            if (i != VA_OSC(0, VO_LEVEL) && (i < VA_ENV(1, 0) || i >= VA_VEL || (i - VA_ENV0) % VE_N != VE_SUS))
                CHECK(d->def == va_range(i).def, "page %s col %u: def %d, init %d", VA_PAGES[pg].title, c, d->def,
                      va_range(i).def);
        }
    }
    for (i = 0; i < VA_NP; i++)
        CHECK(used[i] == (i == VA_OMIX || i == VA_DETUNE ? 0u : 1u), "patch value %u on %u page columns", i, used[i]);
}

static void t_set_macros(void)
{
    track_t *t = &trk[0];
    int8_t init[VA_NP];
    va_init_patch(init);
    memset(t, 0, sizeof *t);
    va_blob_set(t, 0);
    va_set(t, 8, 1, 500);                         /* FILTER CUT: clamped */
    CHECK(va_get(t, 8, 1) == 127 && t->p[P_E0] == 127, "set CUT 500: %d, P_E0 %d", va_get(t, 8, 1), t->p[P_E0]);
    va_set(t, 9, 1, -300);                        /* FENV */
    CHECK(va_get(t, 9, 1) == -64 && t->p[P_E2] == -64, "set FENV -300: %d", va_get(t, 9, 1));
    va_set(t, 0, 0, 9);                           /* OSC 1 WAVE */
    CHECK(va_get(t, 0, 0) == VW_N - 1, "set WAVE 9: %d", va_get(t, 0, 0));
    va_set(t, 23, 2, 100);                        /* MOD 1 AMT */
    CHECK(va_get(t, 23, 2) == 63, "set AMT 100: %d", va_get(t, 23, 2));
    va_set(t, 1, 2, 5);                           /* an empty column: nothing */
    CHECK(va_get(t, 1, 2) == 0, "empty column reads %d", va_get(t, 1, 2));
    va_set(t, 99, 0, 5);
    CHECK(va_get(t, 99, 0) == 0, "page 99");
    t->p[P_E1] = 77;                              /* a knob: va_block takes it into the patch */
    t->p[P_E6] = 12;
    va_block(t);
    CHECK(va_patch[0][VA_RES] == 77 && va_patch[0][VA_ENV(0, VE_ATK)] == 12, "macros -> patch: RES %d ATK %d",
          va_patch[0][VA_RES], va_patch[0][VA_ENV(0, VE_ATK)]);
    va_set(t, 8, 2, 20);                          /* the deep page writes the macro back: va_block sees no change */
    va_block(t);
    CHECK(t->p[P_E1] == 20 && va_patch[0][VA_RES] == 20, "RES set: P_E1 %d patch %d", t->p[P_E1], va_patch[0][VA_RES]);
    /* va_track_loaded: a preset's macros -> its patch; INIT's -> init; anything else: init + the macros */
    {
        uint32_t k;
        int8_t p[VA_NP];
        t->eng_req = (uint8_t)ENGI_VA;
        t->preset = 3;
        for (k = 0; k < 8u; k++)
            t->p[P_E0 + k] = VA_PRESETS[3].e[k];
        va_track_loaded(t);
        va_preset_patch(3, p);
        CHECK(!memcmp(va_patch[0], p, VA_NP), "track_loaded: preset 3's patch");
        for (k = 0; k < 8u; k++)
            t->p[P_E0 + k] = ENG_VA.edit[k].def;
        va_track_loaded(t);
        CHECK(!memcmp(va_patch[0], init, VA_NP), "track_loaded: INIT");
        t->p[P_E0] = 33;
        va_track_loaded(t);
        CHECK(va_patch[0][VA_CUT] == 33 && va_patch[0][VA_OSC(0, VO_LEVEL)] == init[VA_OSC(0, VO_LEVEL)],
              "track_loaded: init + macros");
    }
}

static void t_presets(void)
{
    uint32_t k, j;
    for (k = 0; k < VA_NPRESETS; k++) {
        int8_t p[VA_NP], q[VA_NP];
        uint8_t b[VA_BLOB];
        va_preset_patch(k, p);
        va_pack(p, b);
        CHECK(va_blob_ok(b) && va_unpack(b, q) && !memcmp(p, q, VA_NP), "preset %s: blob", VA_PRESETS[k].name);
        for (j = 0; j < 8u; j++)
            CHECK(VA_PRESETS[k].e[j] == p[VA_MACRO[j]], "preset %s: macro %u is %d, the patch %d", VA_PRESETS[k].name,
                  j, VA_PRESETS[k].e[j], p[VA_MACRO[j]]);
        for (j = 0; VA_PRESET_EDITS[k][j] != 0xFFu; j += 2) {
            va_rng_t r = va_range(VA_PRESET_EDITS[k][j]);
            int8_t v = (int8_t)VA_PRESET_EDITS[k][j + 1];
            CHECK(VA_PRESET_EDITS[k][j] < VA_NP && v >= r.min && v <= r.max, "preset %s: edit %u (%u = %d) out of range",
                  VA_PRESETS[k].name, j / 2u, VA_PRESET_EDITS[k][j], v);
        }
        CHECK(strlen(VA_PRESETS[k].name) <= 12u, "preset name %s too long", VA_PRESETS[k].name);
    }
}

static void t_env(void)
{
    int8_t p[VA_NP];
    va_voice_t s;
    uint32_t n, k;
    va_init_patch(p);
    for (k = 0; k < 4u; k++) {
        p[VA_ENV(k, VE_ATK)] = (int8_t)(20 + 20 * k);
        p[VA_ENV(k, VE_HOLD)] = (int8_t)(k * 30);
        p[VA_ENV(k, VE_DEC)] = 60;
        p[VA_ENV(k, VE_SUS)] = (int8_t)(30 * k + 10);
        p[VA_ENV(k, VE_REL)] = 70;
    }
    memset(&s, 0, sizeof s);
    for (k = 0; k < 4u; k++)
        s.stage[k] = 1;
    for (n = 0; n < 3u * FS / CTL; n++)           /* 3 s held */
        for (k = 0; k < 4u; k++)
            va_env_tick(p, &s, k, 1);
    for (k = 0; k < 4u; k++) {
        int32_t want = p[VA_ENV(k, VE_SUS)] << 17;
        CHECK(s.stage[k] == 3 && abs(s.env[k] - want) < (1 << 16), "env %u: stage %u level %d, sustain %d", k,
              s.stage[k], s.env[k], want);
    }
    for (n = 0; n < 2u * FS / CTL; n++)           /* REL 70 ~ 0.16 s: well within 2 s */
        for (k = 0; k < 4u; k++)
            va_env_tick(p, &s, k, 0);
    for (k = 0; k < 4u; k++)
        CHECK(s.stage[k] == 0 && s.env[k] == 0, "env %u did not end: stage %u level %d", k, s.stage[k], s.env[k]);
    /* attack time: ATK 64 = ~104 ms to the top */
    memset(&s, 0, sizeof s);
    s.stage[0] = 1;
    p[VA_ENV(0, VE_ATK)] = 64;
    p[VA_ENV(0, VE_HOLD)] = 64;
    for (n = 0; s.stage[0] == 1 && n < 10000u; n++)
        va_env_tick(p, &s, 0, 1);
    CHECK(fabs(n * CTL * 1000.0 / FS - TIME_MS_X10[64] / 10.0) < 2.0, "ATK 64: %.1f ms", n * CTL * 1000.0 / FS);
    for (k = 0; s.stage[0] == 2 && k < 10000u; k++)
        va_env_tick(p, &s, 0, 1);
    CHECK(fabs(k * CTL * 1000.0 / FS - TIME_MS_X10[64] / 10.0) < 2.0, "HOLD 64: %.1f ms", k * CTL * 1000.0 / FS);
}

static void t_lfo_sync(void)
{
    static const double BEATS[VA_NDIV] = {32, 16, 8, 4, 2, 1.5, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
    track_t *t = &trk[0];
    uint32_t d, bpm;
    for (bpm = 60; bpm <= 180; bpm += 60)
        for (d = 0; d < VA_NDIV; d++) {
            uint32_t n, wraps = 0, first = 0, last = 0, prev;
            double want = BEATS[d] * 60.0 / bpm * FS / CTL, got;
            memset(t, 0, sizeof *t);
            va_blob_set(t, 0);
            va_patch[0][VA_LFO(0, VL_SYNC)] = 1;
            va_patch[0][VA_LFO(0, VL_RATE)] = (int8_t)((d * 128u + VA_NDIV - 1u) / VA_NDIV);
            CHECK(((uint32_t)va_patch[0][VA_LFO(0, VL_RATE)] * VA_NDIV >> 7) == d, "RATE of division %u", d);
            song.g[G_BPM] = (int16_t)bpm;
            va_lfo[0].ph[0] = 0;
            va_rate_off[0][0] = 0;
            prev = 0;
            for (n = 1; n < (uint32_t)(want * 3.2) + 10u; n++) {
                va_block(t);
                if (va_lfo[0].ph[0] < prev) {
                    if (!wraps)
                        first = n;
                    last = n;
                    wraps++;
                }
                prev = va_lfo[0].ph[0];
            }
            got = wraps > 1u ? (double)(last - first) / (wraps - 1u) : (double)first;
            CHECK(wraps >= 2u && fabs(got - want) / want < 0.01, "SYNC %s at %u BPM: %.1f ticks a cycle, want %.1f",
                  N_VA_DIV[d], bpm, got, want);
        }
}

/* render secs of a 6-note chord (or one note for a bass) on preset k; peak of the part at level 92, the largest
 * |sum| before the mix */
static void chord(uint32_t k, double secs, const int8_t *patch, int32_t *peak, int64_t *maxabs, uint32_t *voices_left)
{
    static const uint8_t CH[6] = {62, 66, 69, 71, 73, 76};
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t n, i, nn = VA_PRESETS[k < VA_NPRESETS ? k : 0].mono ? 1u : 6u;
    drv_reset(t);
    if (patch) {
        memcpy(va_patch[0], patch, VA_NP);
        va_macros_out(t);
    } else {
        va_blob_preset(t, k);
    }
    *peak = 0;
    *maxabs = 0;
    for (i = 0; i < nn; i++)
        drv_on(t, i, nn == 1u ? 38u : CH[i], 100);
    for (n = 0; n < (uint32_t)(secs * FS / CTL); n++) {
        if (n == (uint32_t)(secs * 0.7 * FS / CTL))
            for (i = 0; i < nn; i++)
                t->v[i].gate = 0;               /* release for the last 30 % */
        drv_tick(t, out);
        for (i = 0; i < CTL; i++) {
            int64_t a = llabs((int64_t)out[i]);
            int32_t x = ((out[i] >> 2) * LEVEL_Q12[92]) >> 10;
            x = x < 0 ? -x : x;
            *maxabs = a > *maxabs ? a : *maxabs;
            *peak = x > *peak ? x : *peak;
        }
    }
    *voices_left = 0;
    for (n = 0; n < 15u * FS / CTL && drv_tick(t, out); n++)
        ;
    for (i = 0; i < NVOICE; i++)
        *voices_left += t->v[i].active;
}

static void t_render(void)
{
    uint32_t k, left;
    int32_t peak;
    int64_t mx;
    for (k = 0; k < VA_NPRESETS; k++) {
        chord(k, 1.4, 0, &peak, &mx, &left);
        if (getenv("VERBOSE"))
            printf("  %-13s peak %5.1f %% FS (part at LEVEL 92)\n", VA_PRESETS[k].name, peak * 100.0 / 32768);
        CHECK(mx < (1 << 30), "%s: |sum| %lld near the int32 wrap", VA_PRESETS[k].name, (long long)mx);
        CHECK(peak < 0.9 * 32768, "%s: peak %d >= 0.9 FS", VA_PRESETS[k].name, peak);
        CHECK(peak > 300, "%s: silent (peak %d)", VA_PRESETS[k].name, peak);
        CHECK(!left, "%s: %u voices still active 15 s after the release", VA_PRESETS[k].name, left);
    }
    /* the matrix at its extremes: every slot at +-63 onto pitch / levels / cutoff / resonance, all oscillators,
     * every wave, drive, high resonance: no overflow */
    {
        int8_t p[VA_NP];
        uint32_t w, sgn, f;
        for (w = 0; w < VW_N; w++)
            for (sgn = 0; sgn < 2u; sgn++)
                for (f = 0; f < 4u; f++) {
                    static const uint8_t D[8] = {VD_PITCH, VD_LVL1, VD_LVL1 + 3, VD_CUT, VD_RES, VD_SHP1, VD_SHP1 + 2,
                                                 VD_PIT1 + 1};
                    uint32_t s;
                    va_init_patch(p);
                    for (s = 0; s < 4u; s++) {
                        p[VA_OSC(s, VO_WAVE)] = (int8_t)((w + s) % VW_N);
                        p[VA_OSC(s, VO_LEVEL)] = 127;
                        p[VA_OSC(s, VO_SHAPE)] = 127;
                        p[VA_OSC(s, VO_COARSE)] = (int8_t)(sgn ? 24 : -24);
                    }
                    p[VA_SYNC2] = p[VA_RING4] = 1;
                    p[VA_FTYPE] = (int8_t)f;
                    p[VA_RES] = 127;
                    p[VA_DRIVE] = 127;
                    p[VA_FENV] = (int8_t)(sgn ? 63 : -64);
                    p[VA_ENV(0, VE_SUS)] = 127;
                    for (s = 0; s < 8u; s++) {
                        p[VA_MOD(s, VM_SRC)] = (int8_t)(1u + (s + w) % (VS_N - 1u));
                        p[VA_MOD(s, VM_DST)] = (int8_t)D[s];
                        p[VA_MOD(s, VM_AMT)] = (int8_t)(sgn ? 63 : -64);
                    }
                    chord(0, 0.5, p, &peak, &mx, &left);
                    CHECK(mx < (1 << 30), "matrix extremes w%u s%u f%u: |sum| %lld", w, sgn, f, (long long)mx);
                    CHECK(!left, "matrix extremes w%u s%u f%u: voices left", w, sgn, f);
                }
    }
}

int main(void)
{
    t_blob();
    t_pages();
    t_set_macros();
    t_presets();
    t_env();
    t_lfo_sync();
    t_render();
    printf("cr_va_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
