/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the VA engine (firmware/src/eng_va.c, docs/VA.md), no HAL: the engine on core.h and dsp.c with a
 * minimal voice driver (voice.c's control tick for an ownenv engine: done, then render).
 *   sh tests/run_cr_tests.sh
 * Checks: blob pack / unpack round trip of random patches; a bad blob -> the init patch; the deep page table against
 * the patch ranges; set() clamps; the macros both ways (P_E -> patch in va_block, set -> P_E); every preset's blob
 * valid and its macros as its preset_t; the matrix at its extremes (no overflow); the envelopes reach sustain and
 * end; the LFO SYNC divisions; 1 s of a 6-note chord on every preset: no int32 wrap, peak < 0.9 FS at the chord
 * part's default level. Version 2: a version-1 blob and a version-1 store (va_store.c va_store_v1) import with every
 * old value; MORPH at each shape's position against the BASIC wave and its continuity; the noise types' spectra;
 * FILTER MORPH at its segment ends = the discrete types; SPREAD 0 (render2) = the mono render bit for bit; the
 * mode-dependent columns (desc) and set()'s NOISE rules. */
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
/* va_store.c's needs from upreset.c (its version-1 import is tested; the flash is not: FELUCCA_FLASH 0) */
#define UP_PER_BANK 16u
typedef struct { uint8_t used, ver, engine, np; } up_rec_t;
static struct { up_rec_t r[UP_PER_BANK]; } up_bank[UP_SLOTS / UP_PER_BANK];
static int16_t up_value(const up_rec_t *r, uint32_t i) { (void)r; (void)i; return 0; }
#include "../firmware/src/va_store.c"

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
/* one control tick of the part: block, then each voice (done, then render) into out; returns the voices rendered.
 * drv_side: render2 with the side into drv_sbuf (drv_sret: a voice added to it) */
static int drv_side, drv_sret;
static int32_t drv_sbuf[CTL];
static uint32_t drv_tick(track_t *t, int32_t *out)
{
    uint32_t i, nr = 0;
    memset(out, 0, CTL * sizeof *out);
    memset(drv_sbuf, 0, sizeof drv_sbuf);
    drv_sret = 0;
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
        if (drv_side)
            drv_sret |= va_render(t, v, out, drv_sbuf, CTL, &m);
        else
            va_render_mono(t, v, out, CTL, &m);
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
    CHECK(VA_DEEP.npages == 32u, "pages %u", VA_DEEP.npages);
    CHECK(VA_DEEP.section[0] == 0 && VA_DEEP.section[1] == 8 && VA_DEEP.section[2] == 10 && VA_DEEP.section[3] == 18 &&
              VA_DEEP.section[4] == 24 && VA_DEEP.section[5] == 0xFF, "sections");
    for (pg = 0; pg < 4u; pg++)                   /* OSC n+: MODE first; FILTER+: MORPH, SPREAD; VOICE: USPREAD */
        CHECK(VA_MAP[2 * pg + 1][0] == VA_OMODE0 + pg && VA_MAP[2 * pg + 1][1] == VA_OSC(pg, VO_SHAPE) &&
                  VA_MAP[2 * pg + 1][2] == VA_OSC(pg, VO_KTRK), "OSC %u+ columns", pg + 1);
    CHECK(VA_MAP[3][3] == VA_SYNC2 && VA_MAP[7][3] == VA_RING4, "SYNC / RING columns");
    CHECK(VA_MAP[9][2] == VA_FMORPH && VA_MAP[9][3] == VA_FSPREAD && !strcmp(VA_PAGES[9].title, "FILTER+"), "FILTER+");
    CHECK(VA_MAP[23][0] == VA_USPREAD && !strcmp(VA_PAGES[23].title, "VOICE") && !strcmp(VA_PAGES[24].title, "MOD 1"),
          "VOICE page");
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
    va_set(t, 24, 2, 100);                        /* MOD 1 AMT */
    CHECK(va_get(t, 24, 2) == 63, "set AMT 100: %d", va_get(t, 24, 2));
    va_set(t, 1, 3, 5);                           /* an empty column: nothing */
    CHECK(va_get(t, 1, 3) == 0, "empty column reads %d", va_get(t, 1, 3));
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
            int64_t a = llabs((int64_t)out[i]) + llabs((int64_t)drv_sbuf[i]);
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

/* ------------------------------------------------------------ version 2 --- */
static void rnd_v1(int8_t *p, uint8_t *b)       /* a random version-1 patch and its version-1 blob */
{
    uint32_t i;
    memset(b, 0, VA_BLOB1);
    b[0] = VA_MAGIC;
    b[1] = 1;
    for (i = 0; i < VA_NP1; i++) {
        va_rng_t r = va_range(i);
        int32_t mx = r.max;
        if (i >= VA_MOD0 && i < VA_OMIX && (i - VA_MOD0) % VM_N == VM_DST)
            mx = VD_FMORPH - 1;                  /* (version 1's destinations) */
        p[i] = (int8_t)(r.min + (int32_t)(rnd() % (uint32_t)(mx - r.min + 1)));
        b[2 + i] = (uint8_t)(p[i] - r.min);
    }
}

static void t_v1(void)
{
    uint8_t b1[VA_BLOB], b2[VA_BLOB];
    int8_t p[VA_NP], q[VA_NP], init[VA_NP];
    uint32_t n, i, k;
    va_init_patch(init);
    for (n = 0; n < 1000u; n++) {
        rnd_v1(p, b1);
        for (i = VA_BLOB1; i < VA_BLOB; i++)
            b1[i] = (uint8_t)rnd();              /* (past a version-1 blob: never read) */
        CHECK(va_blob_ok(b1) && va_unpack(b1, q) == 1, "v1 blob %u not taken", n);
        CHECK(!memcmp(p, q, VA_NP1), "v1 blob %u: an old value changed", n);
        CHECK(!memcmp(q + VA_NP1, init + VA_NP1, VA_NP - VA_NP1), "v1 blob %u: the new values are not the init", n);
        va_pack(q, b2);
        CHECK(b2[1] == VA_VER && va_blob_ok(b2) && va_unpack(b2, p) && !memcmp(p, q, VA_NP), "v1 blob %u -> v2", n);
    }
    rnd_v1(p, b1);
    b1[VA_BLOB1 - 1u] = 1;
    CHECK(!va_unpack(b1, q), "v1 blob with its padding not 0: taken");
    /* a version-1 store in the mirror (as st_load leaves it): converted in place, every patch kept */
    {
        static int8_t keep[UP_SLOTS][VA_NP];
        uint8_t *raw = (uint8_t *)&va_store, b[VA_BLOB];
        memset(&va_store, 0, sizeof va_store);
        va_store.magic = VA_STORE_MAGIC;
        va_store.ver = 1;
        va_store.nslot = UP_SLOTS;
        va_store.blob = VA_BLOB1;
        for (k = 0; k < UP_SLOTS; k++) {
            rnd_v1(keep[k], b1);
            if (k % 3u == 1u)
                continue;                        /* an empty slot */
            if (k == 5u)
                b1[2 + VA_CUT] = 200;            /* a bad patch: dropped */
            memcpy(raw + 16u + k * VA_BLOB1, b1, VA_BLOB1);
            va_store.used |= 1u << k;
        }
        CHECK(va_store_v1((int)(16u + UP_SLOTS * VA_BLOB1)) == 1, "store v1: not converted");
        CHECK(va_store.ver == 2u && va_store.blob == VA_BLOB && va_store_valid(&va_store), "store v1 -> v2: not valid");
        for (k = 0; k < UP_SLOTS; k++) {
            int has = !va_store_get(k, b);
            if (k % 3u == 1u || k == 5u) {
                CHECK(!has, "store v1 slot %u: should be empty", k);
                continue;
            }
            CHECK(has && va_unpack(b, q) && b[1] == VA_VER && !memcmp(q, keep[k], VA_NP1) &&
                      !memcmp(q + VA_NP1, init + VA_NP1, VA_NP - VA_NP1), "store v1 slot %u: patch changed", k);
        }
        CHECK(!va_store_v1((int)sizeof va_store), "store v2 taken as v1");
        memset(&va_store, 0, sizeof va_store);
    }
}

static void t_modes(void)
{
    track_t *t = &trk[0];
    const param_desc_t *d;
    memset(t, 0, sizeof *t);
    va_blob_set(t, 0);
    CHECK(!va_desc(t, 0, 0) && !va_desc(t, 1, 1) && !va_desc(t, 8, 0) && !va_desc(t, 1, 0), "BASIC: the table's columns");
    va_set(t, 0, 0, VW_PWM);
    va_set(t, 1, 0, VOM_NOISE);                  /* OSC 1+ MODE */
    CHECK(va_get(t, 0, 0) == VN_WHITE && va_get(t, 1, 2) == 0, "-> NOISE: WAVE %d (WHITE), KTRK %d (off)",
          va_get(t, 0, 0), va_get(t, 1, 2));
    va_set(t, 0, 0, 5);
    CHECK(va_get(t, 0, 0) == VN_VINYL, "NOISE: WAVE 5 -> %d (VINYL)", va_get(t, 0, 0));
    d = va_desc(t, 0, 0);
    CHECK(d && !strcmp(d->label, "NTYPE") && d->max == VN_N - 1 && !strcmp(d->names[VN_VINYL], "VINYL"), "NTYPE desc");
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "DENS"), "VINYL's SHAPE desc");
    va_set(t, 0, 0, VN_BROWN);
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "COLOR"), "BROWN's SHAPE desc");
    va_set(t, 1, 2, 1);                          /* KTRK on: MODE NOISE again keeps it */
    va_set(t, 1, 0, VOM_NOISE);
    CHECK(va_get(t, 1, 2) == 1, "NOISE again: KTRK %d", va_get(t, 1, 2));
    va_set(t, 1, 0, VOM_MORPH);
    {
        static const struct { int sh; const char *nm; } NM[] = {{0, "SIN"}, {12, "SIN>TRI"}, {24, "TRI"}, {48, "SAW"},
            {60, "SAW>RMP"}, {72, "RAMP"}, {84, "RMP>SQR"}, {96, "SQR"}, {110, "SQR>PLS"}, {127, "PULSE"}};
        uint32_t j;
        for (j = 0; j < NELEM(NM); j++) {
            va_set(t, 1, 1, NM[j].sh);
            d = va_desc(t, 0, 0);
            CHECK(d && !strcmp(d->label, "MORPH") && !strcmp(d->names[va_get(t, 0, 0)], NM[j].nm),
                  "MORPH %d: preview %s, want %s", NM[j].sh, d ? d->names[va_get(t, 0, 0)] : "-", NM[j].nm);
        }
    }
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "MORPH"), "MORPH's SHAPE desc");
    CHECK(!va_desc(t, 2, 0) && !va_desc(t, 3, 1), "OSC 2: still BASIC");
}

/* one voice of patch p (note 48, velocity 100) for nt ticks into out (and side): the render2 results ORed */
static int one_voice(const int8_t *p, uint32_t vi, uint32_t mode, uint32_t nt, int32_t *out, int32_t *side)
{
    track_t *t = &trk[0];
    uint32_t n, r = 0;
    drv_reset(t);
    memcpy(va_patch[0], p, VA_NP);
    va_macros_out(t);
    t->p[P_VOICE] = (int16_t)mode;
    drv_on(t, vi, 48, 100);
    drv_side = side != 0;
    for (n = 0; n < nt; n++) {
        drv_tick(t, out + n * CTL);
        if (side)
            memcpy(side + n * CTL, drv_sbuf, sizeof drv_sbuf);
        r |= (uint32_t)drv_sret;
    }
    drv_side = 0;
    return (int)r;
}

#define NT 400u                                  /* 0.29 s */
static int32_t o1[NT * CTL], o2[NT * CTL], s1[NT * CTL], s2[NT * CTL];

static void t_morph(void)
{
    uint32_t inc = pitch_inc(69 * 16), j, k, big = 0, tot = 0;
    int32_t wsin = 0, wtri = 0, wramp = 0, jump = 0;
    int8_t p[VA_NP];
    for (j = 0; j < 50000u; j++) {               /* each shape's position against the BASIC wave (aligned) */
        uint32_t ph = rnd() << 8 ^ rnd();
        int32_t d = abs(va_morph(ph, inc, VA_MP(0)) + sine_i(ph));   /* SIN: BASIC SIN half a cycle on: -sin */
        wsin = d > wsin ? d : wsin;
        d = abs(va_morph(ph + 0x40000000u, inc, VA_MP(1)) - osc_tri(ph));   /* TRI: BASIC TRI a quarter on */
        wtri = d > wtri ? d : wtri;
        d = abs(va_morph(ph + 0x80000000u, inc, VA_MP(3)) + osc_saw(ph, inc));   /* RAMP: the saw reversed */
        wramp = d > wramp ? d : wramp;
    }
    CHECK(wsin <= 1 && wtri <= 1 && wramp <= 1, "MORPH at SIN / TRI / RAMP: off by %d / %d / %d", wsin, wtri, wramp);
    for (j = 0; j < 50000u; j++) {               /* PULSE: BASIC SQR at SHAPE 127 */
        uint32_t ph = rnd() << 8 ^ rnd();
        int32_t d = abs(va_morph(ph, inc, 127 * 258) - osc_pulse(ph, inc, VA_PULSE_PW(127 * 258)));
        wsin = d > wsin ? d : wsin;
    }
    CHECK(wsin <= 1, "MORPH at PULSE: off by %d", wsin);
    /* SAW and SQR: the BASIC waves themselves, bit for bit through the whole voice */
    {
        static const struct { int sh, w, bsh; } EQ[] = {{48, VW_SAW, 0}, {96, VW_SQR, 0}};
        for (k = 0; k < NELEM(EQ); k++) {
            va_init_patch(p);
            p[VA_OSC(0, VO_WAVE)] = (int8_t)EQ[k].w;
            p[VA_OSC(0, VO_SHAPE)] = (int8_t)EQ[k].bsh;
            p[VA_CUT] = 110;
            p[VA_RES] = 40;
            one_voice(p, 0, V_POLY, NT, o1, 0);
            p[VA_OMODE0] = VOM_MORPH;
            p[VA_OSC(0, VO_WAVE)] = VW_TRI;      /* (ignored in MORPH) */
            p[VA_OSC(0, VO_SHAPE)] = (int8_t)EQ[k].sh;
            one_voice(p, 0, V_POLY, NT, o2, 0);
            CHECK(!memcmp(o1, o2, sizeof o1), "MORPH %d: not BASIC %s at SHAPE %d", EQ[k].sh, N_VA_WAVE[EQ[k].w], EQ[k].bsh);
        }
    }
    /* continuity: a step of the position (Q15) moves no sample by more than 64 up to the square; the pulse's width
     * (as BASIC SQR's SHAPE) moves its edges only: few samples change much */
    for (j = 0; j < 64u; j++) {
        uint32_t ph = j * 0x04000000u + 0x01234567u;
        int32_t sh, prev = va_morph(ph, inc, 0), x;
        for (sh = 1; sh <= 32766; sh++) {
            x = va_morph(ph, inc, sh);
            if (sh <= VA_MP(4)) {
                int32_t d = abs(x - prev);
                jump = d > jump ? d : jump;
            } else {
                tot++;
                big += abs(x - prev) > 2000;
            }
            prev = x;
        }
    }
    CHECK(jump <= 64, "MORPH: a jump of %d between neighbouring positions", jump);
    CHECK(big * 100u < tot * 2u, "MORPH pulse: %u of %u steps jump", big, tot);
}

static double hf_ratio(const int32_t *x, uint32_t n)   /* first-difference energy / energy: ~2 for white noise */
{
    double e = 0, d = 0;
    uint32_t i;
    for (i = 1; i < n; i++) {
        e += (double)x[i] * x[i];
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    }
    return e > 0 ? d / e : 0;
}
static double rms(const int32_t *x, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return sqrt(e / n);
}

static void t_noise(void)
{
    static int32_t w[FS], b[FS], b2[FS], vy[5 * FS];
    int32_t nst = 0x2545F491, y = 0, y2 = 0, c = 0, h = 0;
    uint32_t i, ev = 0, loud = 0, ev127 = 0;
    for (i = 0; i < FS; i++)
        w[i] = (int32_t)(noise32(&nst) >> 16) - 32768;
    for (i = 0; i < FS; i++)
        b[i] = va_brown(&nst, &y, VA_BROWN_K[0], VA_BROWN_G[0]);
    for (i = 0; i < FS; i++)
        b2[i] = va_brown(&nst, &y2, VA_BROWN_K[16], VA_BROWN_G[16]);
    if (getenv("VERBOSE"))
        printf("  noise: white hf %.3f rms %.0f, brown(0) hf %.4f rms %.0f, brown(127) hf %.3f rms %.0f\n",
               hf_ratio(w, FS), rms(w, FS), hf_ratio(b, FS), rms(b, FS), hf_ratio(b2, FS), rms(b2, FS));
    CHECK(hf_ratio(b, FS) < hf_ratio(w, FS) / 50 && hf_ratio(b2, FS) < hf_ratio(w, FS) / 2 &&
              hf_ratio(b, FS) < hf_ratio(b2, FS), "BROWN: HF energy %.4f / %.4f, white %.3f", hf_ratio(b, FS),
          hf_ratio(b2, FS), hf_ratio(w, FS));
    CHECK(rms(b, FS) > 0.25 * rms(w, FS) && rms(b, FS) < rms(w, FS) && rms(b2, FS) > 0.25 * rms(w, FS) &&
              rms(b2, FS) < rms(w, FS), "BROWN: level %.0f / %.0f, white %.0f", rms(b, FS), rms(b2, FS), rms(w, FS));
    for (i = 0; i < 5u * FS; i++) {               /* VINYL at SHAPE 40: sparse clicks over a quiet hiss */
        vy[i] = va_vinyl(&nst, &c, &h, 2u + 40u * 40u * 40u / 7000u);
        loud += abs(vy[i]) > 8000;
        ev += i && abs(vy[i] - vy[i - 1]) > 6000;
    }
    for (i = 0; i < 5u * FS; i++) {
        int32_t prev = i ? vy[i - 1] : 0;
        vy[i] = va_vinyl(&nst, &c, &h, 2u + 127u * 127u * 127u / 7000u);
        ev127 += i && abs(vy[i] - prev) > 6000;
    }
    if (getenv("VERBOSE"))
        printf("  vinyl: %.1f clicks / s (SHAPE 40), %.1f (127), %.2f %% of the samples loud\n", ev / 5.0, ev127 / 5.0,
               loud * 100.0 / (5 * FS));
    CHECK(ev >= 10u && ev <= 100u && loud * 100u < 5u * FS * 2u, "VINYL: %u clicks in 5 s, %u loud samples", ev, loud);
    CHECK(ev127 > 5u * ev, "VINYL: SHAPE 127 %u clicks, 40 %u", ev127, ev);
}

static void t_fmorph_spread(void)
{
    int8_t p[VA_NP];
    uint32_t ty, j, k, i;
    va_init_patch(p);
    p[VA_OSC(1, VO_WAVE)] = VW_SQR;
    p[VA_OSC(1, VO_LEVEL)] = 80;
    p[VA_OSC(1, VO_COARSE)] = 7;
    p[VA_CUT] = 70;
    p[VA_RES] = 60;
    p[VA_DRIVE] = 20;
    for (ty = 0; ty < 4u; ty++)                  /* MORPH 32 j from TYPE ty = the discrete type ty + j, bit for bit */
        for (j = 1; j < 4u; j++) {
            p[VA_FTYPE] = (int8_t)((ty + j) & 3u);
            p[VA_FMORPH] = 0;
            one_voice(p, 0, V_POLY, NT, o1, 0);
            p[VA_FTYPE] = (int8_t)ty;
            p[VA_FMORPH] = (int8_t)(32u * j);
            one_voice(p, 0, V_POLY, NT, o2, 0);
            CHECK(!memcmp(o1, o2, sizeof o1), "FILTER %s MORPH %u: not %s", N_VA_FTYPE[ty], 32u * j,
                  N_VA_FTYPE[(ty + j) & 3u]);
        }
    p[VA_FTYPE] = VF_LP;
    p[VA_FMORPH] = 0;
    /* SPREAD 0: render2 = the mono render, bit for bit, the side untouched; every preset */
    for (k = 0; k < VA_NPRESETS; k++) {
        int8_t q[VA_NP];
        va_preset_patch(k, q);
        if (q[VA_FSPREAD])
            continue;
        one_voice(q, 0, V_POLY, NT, o1, 0);
        memset(s2, 0, sizeof s2);
        CHECK(!one_voice(q, 0, V_POLY, NT, o2, s2), "%s: render2 says side", VA_PRESETS[k].name);
        for (i = 0; i < NT * CTL && !s2[i]; i++)
            ;
        CHECK(!memcmp(o1, o2, sizeof o1) && i == NT * CTL, "%s: render2 (SPREAD 0) differs from the mono render",
              VA_PRESETS[k].name);
    }
    /* SPREAD 127: a side; the right channel (mid + side) brighter than the left (mid - side) */
    {
        static int32_t l[NT * CTL], r[NT * CTL];
        p[VA_FSPREAD] = 127;
        p[VA_RES] = 0;
        CHECK(one_voice(p, 0, V_POLY, NT, o1, s1), "SPREAD 127: no side");
        for (i = 0; i < NT * CTL; i++) {
            l[i] = o1[i] - s1[i];
            r[i] = o1[i] + s1[i];
        }
        if (getenv("VERBOSE"))
            printf("  spread: L hf %.4f rms %.0f, R hf %.4f rms %.0f, side rms %.0f\n", hf_ratio(l, NT * CTL),
                   rms(l, NT * CTL), hf_ratio(r, NT * CTL), rms(r, NT * CTL), rms(s1, NT * CTL));
        CHECK(hf_ratio(r, NT * CTL) > 1.5 * hf_ratio(l, NT * CTL) && rms(s1, NT * CTL) > 0.05 * rms(o1, NT * CTL),
              "SPREAD: R hf %.4f, L %.4f", hf_ratio(r, NT * CTL), hf_ratio(l, NT * CTL));
        p[VA_FSPREAD] = 0;
    }
    /* USPREAD: UNISON voice 0 hard left (side = -mid), voice 7 right (side = +mid), POLY: no side */
    p[VA_USPREAD] = 127;
    CHECK(!one_voice(p, 0, V_POLY, NT, o1, s1), "USPREAD in POLY: a side");
    CHECK(one_voice(p, 0, V_UNISON, NT, o1, s1), "USPREAD voice 0: no side");
    for (i = 0; i < NT * CTL && s1[i] == -o1[i]; i++)
        ;
    CHECK(i == NT * CTL, "USPREAD voice 0: not hard left at sample %u (%d, %d)", i, o1[i], s1[i]);
    one_voice(p, 7, V_UNISON, NT, o1, s1);
    for (i = 0; i < NT * CTL && abs(s1[i] - o1[i]) <= abs(o1[i]) / 1000 + 1; i++)
        ;
    CHECK(i == NT * CTL, "USPREAD voice 7: not hard right at sample %u (%d, %d)", i, o1[i], s1[i]);
}

/* the matrix at its extremes with the version-2 modes: MORPH / NOISE oscillators, FILTER MORPH (and its
 * destination), SPREAD and USPREAD, through render2: no overflow, voices end */
static void t_extremes2(void)
{
    int8_t p[VA_NP];
    uint32_t w, sgn, f, s, left;
    int32_t peak;
    int64_t mx;
    drv_side = 1;
    for (w = 0; w < 6u; w++)
        for (sgn = 0; sgn < 2u; sgn++)
            for (f = 0; f < 4u; f++) {
                static const uint8_t D[8] = {VD_PITCH, VD_LVL1, VD_FMORPH, VD_CUT, VD_RES, VD_SHP1, VD_SHP1 + 2,
                                             VD_SHP1 + 3};
                va_init_patch(p);
                for (s = 0; s < 4u; s++) {
                    p[VA_OMODE0 + s] = (int8_t)(1u + (w + s) % 2u);   /* MORPH / NOISE */
                    p[VA_OSC(s, VO_WAVE)] = (int8_t)((w + s) % VW_N);
                    p[VA_OSC(s, VO_LEVEL)] = 127;
                    p[VA_OSC(s, VO_SHAPE)] = (int8_t)(sgn ? 127 : 0);
                    p[VA_OSC(s, VO_KTRK)] = (int8_t)(f & 1u);
                    p[VA_OSC(s, VO_COARSE)] = (int8_t)(sgn ? 24 : -24);
                }
                p[VA_SYNC2] = p[VA_RING4] = 1;
                p[VA_FTYPE] = (int8_t)f;
                p[VA_FMORPH] = (int8_t)(w * 25u);
                p[VA_FSPREAD] = (int8_t)(sgn ? 127 : 60);
                p[VA_USPREAD] = 127;
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
                CHECK(mx < (1 << 30), "v2 extremes w%u s%u f%u: |sum| %lld", w, sgn, f, (long long)mx);
                CHECK(!left, "v2 extremes w%u s%u f%u: voices left", w, sgn, f);
            }
    drv_side = 0;
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
    t_v1();
    t_modes();
    t_morph();
    t_noise();
    t_fmorph_spread();
    t_extremes2();
    printf("cr_va_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
