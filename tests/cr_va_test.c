/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the VA engine (firmware/src/eng_va.c, docs/VA.md), no HAL: the engine on core.h and dsp.c with a
 * minimal voice driver (voice.c's control tick for an ownenv engine: done, then render).
 *   sh tests/run_cr_tests.sh
 * Checks: blob pack / unpack round trip of random patches; a bad blob -> the init patch; the deep page table against
 * the patch ranges; set() clamps; the macros both ways (P_E -> patch in va_block, set -> P_E); every preset's blob
 * valid and its macros as its preset_t; the matrix at its extremes (no overflow); the envelopes reach sustain and
 * end; the LFO SYNC divisions; 1 s of a 6-note chord on every preset: no int32 wrap, peak < 0.9 FS at the chord
 * part's default level. Version 2: a version-1 blob imports with every old value (0.21's whole-store conversions
 * went with the store, HISTORY 3y231); MORPH at each shape's position against the BASIC wave and its continuity; the
 * noise types' spectra;
 * FTYPE at 0 32 64 96 = the discrete types; SPREAD 0 (render2) = the mono render bit for bit; the mode-dependent
 * columns (desc: OSC n's WAVE column follows MODE, an alias of SHAPE in MORPH) and set()'s NOISE rules. Version 3:
 * FTYPE (TYPE and MORPH in one value): a version-2 blob imports as FTYPE = TYPE x 32 + MORPH; the page
 * layout (FILTER: CUT RES FTYPE FENV, FILTER+: KTRK - SPREAD DRIVE). */
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
static const char *const N_VA_FTYPE[] = {"LP", "BP", "HP", "NOTCH"};
/* version 2's morph position names (eng_va.c va_morph_name, before the 128-name list): the reference */
static const char *const OLD_MPV[11] = {"SIN", "SIN>TRI", "TRI", "TRI>SAW", "SAW", "SAW>RMP", "RAMP", "RMP>SQR", "SQR",
                                        "SQR>PLS", "PULSE"};
static uint32_t old_morph_name(int32_t sh)
{
    int32_t seg = sh / 24, f = sh % 24;
    if (sh >= 96)
        return sh < 100 ? 8u : sh < 124 ? 9u : 10u;
    return (uint32_t)(f < 4 ? 2 * seg : f > 20 ? 2 * seg + 2 : 2 * seg + 1);
}

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
    b2[VA_BLOB - 1u] = 2;                        /* (version 5: slot 8's fine flag, 0..1) */
    CHECK(va_unpack(b2, q) == 0 && !memcmp(q, init, VA_NP), "slot 8's fine flag 2: not the init patch");
    memcpy(b2, b, VA_BLOB);
    b2[1] = 4;
    b2[VA_BLOB - 2u] = 1;
    b2[VA_BLOB - 1u] = 0;
    CHECK(va_unpack(b2, q) == 0 && !memcmp(q, init, VA_NP), "version 4, padding not 0: not the init patch");
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
    for (pg = 0; pg < 4u; pg++)                   /* OSC n+: MODE first; VOICE: USPREAD */
        CHECK(VA_MAP[2 * pg + 1][0] == VA_OMODE0 + pg && VA_MAP[2 * pg + 1][1] == VA_OSC(pg, VO_SHAPE) &&
                  VA_MAP[2 * pg + 1][2] == VA_OSC(pg, VO_KTRK), "OSC %u+ columns", pg + 1);
    CHECK(VA_MAP[3][3] == VA_SYNC2 && VA_MAP[7][3] == VA_RING4, "SYNC / RING columns");
    /* FILTER: CUT RES FTYPE FENV; FILTER+: KTRK, an empty column, SPREAD, DRIVE */
    CHECK(VA_MAP[8][0] == VA_CUT && VA_MAP[8][1] == VA_RES && VA_MAP[8][2] == VA_FTYPE && VA_MAP[8][3] == VA_FENV &&
              !strcmp(VA_PAGES[8].title, "FILTER"), "FILTER");
    CHECK(!strcmp(VA_PAGES[8].col[0].label, "CUT") && !strcmp(VA_PAGES[8].col[1].label, "RES") &&
              !strcmp(VA_PAGES[8].col[2].label, "FTYPE") && !strcmp(VA_PAGES[8].col[3].label, "FENV"), "FILTER labels");
    CHECK(VA_MAP[9][0] == VA_FKTRK && VA_MAP[9][1] == VA_X && VA_MAP[9][2] == VA_FSPREAD && VA_MAP[9][3] == VA_DRIVE &&
              !strcmp(VA_PAGES[9].title, "FILTER+"), "FILTER+");
    CHECK(!strcmp(VA_PAGES[9].col[0].label, "KTRK") && !VA_PAGES[9].col[1].label &&
              !strcmp(VA_PAGES[9].col[2].label, "SPREAD") && !strcmp(VA_PAGES[9].col[3].label, "DRIVE"), "FILTER+ labels");
    {   /* FTYPE: F_INT 0..127, a name per value (params.c's F_INT name list) */
        const param_desc_t *d = &VA_PAGES[8].col[2];
        uint32_t v;
        for (v = 0; v < 128u && d->names[v]; v++)
            ;
        CHECK(d->fmt == F_INT && d->min == 0 && d->max == 127 && d->def == 0 && v == 128u && !d->names[128], "FTYPE desc");
        CHECK(!strcmp(d->names[0], "LP") && !strcmp(d->names[32], "BP") && !strcmp(d->names[64], "HP") &&
                  !strcmp(d->names[96], "NOTCH") && !strcmp(d->names[1], "LP>BP") && !strcmp(d->names[63], "BP>HP") &&
                  !strcmp(d->names[80], "HP>NT") && !strcmp(d->names[127], "NT>LP"), "FTYPE names");
        CHECK(!strcmp(N_VA_DST[VD_FTYPE], "FTYPE"), "the matrix's FTYPE");
    }
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
            if (i == VA_FENV)                    /* (hundredths: F_FIX2, the patch's range x 100, VA_FENVF the rest) */
                CHECK(d->fmt == F_FIX2 && d->min == va_range(i).min * 100 && d->max == va_range(i).max * 100 + 99 &&
                      va_range(VA_FENVF).max == 99, "FENV hi-res range %d..%d", d->min, d->max);
            else if (pg >= 24u && c == 2u)       /* (AMT: hundredths, the patch's whole range x 100; version 5) */
                CHECK(d->fmt == F_FIX2 && d->min == va_range(i).min * 100 && d->max == va_range(i).max * 100 &&
                      va_range(VA_MFINE0).max == 127 && va_range(VA_MFINE1).max == 1, "AMT hi-res range %d..%d",
                      d->min, d->max);
            else
                CHECK(d->min == va_range(i).min && d->max == va_range(i).max, "page %s col %u: range %d..%d, patch %d..%d",
                      VA_PAGES[pg].title, c, d->min, d->max, va_range(i).min, va_range(i).max);
            if (i != VA_OSC(0, VO_LEVEL) && (i < VA_ENV(1, 0) || i >= VA_VEL || (i - VA_ENV0) % VE_N != VE_SUS))
                CHECK(d->def == va_range(i).def, "page %s col %u: def %d, init %d", VA_PAGES[pg].title, c, d->def,
                      va_range(i).def);
        }
    }
    for (i = 0; i < VA_NP; i++)
        CHECK(used[i] == (i == VA_OMIX || i == VA_DETUNE || i == VA_FRSV || i >= VA_MFINE0 ? 0u : 1u),
              "patch value %u on %u page columns",
              i, used[i]);
}

static void t_set_macros(void)
{
    track_t *t = &trk[0];
    int8_t init[VA_NP];
    va_init_patch(init);
    memset(t, 0, sizeof *t);
    va_blob_set(t, 0);
    va_set(t, 8, 0, 500);                         /* FILTER CUT: clamped */
    CHECK(va_get(t, 8, 0) == 127 && t->p[P_E0] == 127, "set CUT 500: %d, P_E0 %d", va_get(t, 8, 0), t->p[P_E0]);
    va_set(t, 8, 3, -30000);                      /* FENV (hundredths): clamped */
    CHECK(va_get(t, 8, 3) == -6400 && t->p[P_E2] == -64, "set FENV -300.00: %d", va_get(t, 8, 3));
    va_set(t, 8, 3, 4);                           /* 0.04: the whole part 0 (the macro), the hundredths 4 */
    CHECK(va_get(t, 8, 3) == 4 && t->p[P_E2] == 0 && va_patch[0][VA_FENV] == 0 && va_patch[0][VA_FENVF] == 4,
          "set FENV 0.04: %d", va_get(t, 8, 3));
    va_set(t, 8, 3, -1);                          /* -0.01: -1 + 0.99 */
    CHECK(va_get(t, 8, 3) == -1 && va_patch[0][VA_FENV] == -1 && va_patch[0][VA_FENVF] == 99, "set FENV -0.01");
    va_set(t, 8, 3, 6399);
    CHECK(va_get(t, 8, 3) == 6399 && t->p[P_E2] == 63, "set FENV 63.99");
    t->p[P_E2] = 20;                              /* the macro moved: FENV 20.00 */
    va_block(t);
    CHECK(va_get(t, 8, 3) == 2000, "FENV macro 20: %d", va_get(t, 8, 3));
    va_set(t, 8, 3, -6400);
    va_set(t, 9, 3, 90);                          /* FILTER+ DRIVE */
    CHECK(va_get(t, 9, 3) == 90 && t->p[P_E3] == 90, "set DRIVE 90: %d, P_E3 %d", va_get(t, 9, 3), t->p[P_E3]);
    va_set(t, 8, 2, 200);                         /* FTYPE: clamped */
    CHECK(va_get(t, 8, 2) == 127 && va_patch[0][VA_FTYPE] == 127, "set FTYPE 200: %d", va_get(t, 8, 2));
    va_set(t, 8, 2, 0);
    va_set(t, 9, 1, 5);                           /* FILTER+'s empty column */
    CHECK(va_get(t, 9, 1) == 0 && va_patch[0][VA_FRSV] == 0, "FILTER+ empty column");
    va_set(t, 0, 0, 9);                           /* OSC 1 WAVE */
    CHECK(va_get(t, 0, 0) == VW_N - 1, "set WAVE 9: %d", va_get(t, 0, 0));
    va_set(t, 24, 2, 9999);                       /* MOD 1 AMT (hundredths): clamped */
    CHECK(va_get(t, 24, 2) == 6300 && va_patch[0][VA_MOD(0, VM_AMT)] == 63, "set AMT 99.99: %d", va_get(t, 24, 2));
    va_set(t, 24, 2, 0);
    va_set(t, 1, 3, 5);                           /* an empty column: nothing */
    CHECK(va_get(t, 1, 3) == 0, "empty column reads %d", va_get(t, 1, 3));
    va_set(t, 99, 0, 5);
    CHECK(va_get(t, 99, 0) == 0, "page 99");
    t->p[P_E1] = 77;                              /* a knob: va_block takes it into the patch */
    t->p[P_E6] = 12;
    va_block(t);
    CHECK(va_patch[0][VA_RES] == 77 && va_patch[0][VA_ENV(0, VE_ATK)] == 12, "macros -> patch: RES %d ATK %d",
          va_patch[0][VA_RES], va_patch[0][VA_ENV(0, VE_ATK)]);
    va_set(t, 8, 1, 20);                          /* the deep page writes the macro back: va_block sees no change */
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

/* the version-4 sync list's lengths: beats (a note or bars), < 0: a free period in -ms */
static double sync_beats(uint32_t i)
{
    static const double B[13] = {32, 16, 12, 8, 4, 2, 1, 0.5, 0.25, 0.125, 1.0 / 16, 1.0 / 32, 1.0 / 64};
    static const double M[3] = {1.5, 1, 2.0 / 3};
    if (i >= 39u)
        return -(double)(4u - (i - 39u));
    return B[i / 3u] * M[i % 3u];
}

static void t_lfo_sync(void)
{
    static const double BEATS[VA_NDIV3] = {32, 16, 8, 4, 2, 1.5, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
    track_t *t = &trk[0];
    uint32_t d, bpm, i;
    /* every entry: its name, its period at 120 BPM within 0.1 % (60 and 300 BPM too), phase-locked over 8 bars */
    CHECK(N_VA_SYNC[VA_NSYNC] == 0 && !strcmp(N_VA_SYNC[0], "8BARD") && !strcmp(N_VA_SYNC[19], "1/4") &&
          !strcmp(N_VA_SYNC[38], "256T") && !strcmp(N_VA_SYNC[41], "2ms"), "sync names");
    for (i = 0; i < VA_NSYNC; i++) {
        CHECK(N_VA_SYNC[i] && strlen(N_VA_SYNC[i]) <= 5u, "sync name %u too long", i);
        for (bpm = 60; bpm <= 300; bpm += bpm == 120u ? 180u : 60u) {
            double b = sync_beats(i), want = (b > 0 ? b * 60.0 / bpm : -b / 1000.0) * FS / CTL, got;
            int8_t p[VA_NP];
            uint32_t inc;
            va_init_patch(p);
            p[VA_LFO(0, VL_SYNC)] = 1;
            song.g[G_BPM] = (int16_t)bpm;
            inc = va_lfo_inc(p, 0, (int32_t)i, 0);
            got = 4294967296.0 / inc;
            CHECK(fabs(got - want) / want < 0.001, "sync %s at %u BPM: %.3f ticks a cycle, want %.3f", N_VA_SYNC[i], bpm,
                  got, want);
            if (bpm == 120u && b > 0) {          /* 8 bars at 120 BPM = 16 s = 22050 ticks: the cycles it holds */
                double cyc = 22050.0 * inc / 4294967296.0, wc = 32.0 / b;
                CHECK(fabs(cyc - wc) < 0.001 * wc + 0.002, "sync %s: %.4f cycles in 8 bars, want %.4f", N_VA_SYNC[i],
                      cyc, wc);
            }
        }
    }
    /* phase lock through va_block: over 8 bars at 120 BPM 1/4 ends where it started, 8BARD (12 bars, the slowest)
     * 2/3 of its cycle on (+-0.1 % of a cycle) */
    for (i = 0; i < 2u; i++) {
        uint32_t n;
        memset(t, 0, sizeof *t);
        va_blob_set(t, 0);
        va_patch[0][VA_LFO(0, VL_SYNC)] = 1;
        va_patch[0][VA_LFO(0, VL_RATE)] = (int8_t)(i ? 0 : 19);
        song.g[G_BPM] = 120;
        va_lfo[0].ph[0] = 0;
        va_rate_off[0][0] = 0;
        for (n = 0; n < 22050u; n++)
            va_block(t);
        {
            double ph = va_lfo[0].ph[0] / 4294967296.0, want = i ? 2.0 / 3 : 0.0, e = fabs(ph - want);
            e = e > 0.5 ? 1.0 - e : e;
            CHECK(e < 0.001, "sync %s: phase %.5f after 8 bars, want %.5f", N_VA_SYNC[i ? 0 : 19], ph, want);
        }
    }
    /* the matrix's RATEn on a synced LFO moves along the list (+127: to its end) */
    {
        int8_t p[VA_NP];
        va_init_patch(p);
        p[VA_LFO(0, VL_SYNC)] = 1;
        song.g[G_BPM] = 120;
        CHECK(va_lfo_inc(p, 0, 19, 127) == va_lfo_inc(p, 0, 41, 0) && va_lfo_inc(p, 0, 19, -127) ==
              va_lfo_inc(p, 0, 0, 0), "synced RATE + the matrix's RATE1");
    }
    /* version 3's divisions -> the same length in the new list; a version-3 blob converts them */
    for (d = 0; d < VA_NDIV3; d++)
        CHECK(fabs(sync_beats(VA_DIV3_TO_SYNC[d]) - BEATS[d]) < 1e-9, "v3 division %u -> %s", d,
              N_VA_SYNC[VA_DIV3_TO_SYNC[d]]);
    for (d = 0; d < 128u; d++) {
        int8_t p[VA_NP], q[VA_NP];
        uint8_t b[VA_BLOB];
        va_init_patch(p);
        p[VA_LFO(1, VL_SYNC)] = 1;
        p[VA_LFO(1, VL_RATE)] = (int8_t)d;
        p[VA_LFO(2, VL_RATE)] = (int8_t)d;       /* (free: kept) */
        va_pack(p, b);
        b[1] = 3;
        CHECK(va_unpack(b, q) && q[VA_LFO(1, VL_RATE)] == VA_DIV3_TO_SYNC[d * VA_NDIV3 >> 7] &&
              q[VA_LFO(2, VL_RATE)] == (int8_t)d, "v3 synced RATE %u", d);
    }
    /* the deep page: a synced RATE is the list (F_ENUM, its names), a free one F_LFOHZ */
    {
        const param_desc_t *ds;
        memset(t, 0, sizeof *t);
        va_blob_set(t, 0);
        CHECK(va_desc(t, 18, 0) == 0, "free RATE: the table's column");
        va_set(t, 22, 0, 1);                     /* SYNC on: RATE 70 (of 128) -> 22 (of 42, 1/8) */
        CHECK(va_get(t, 18, 0) == 22, "SYNC on: RATE %d", va_get(t, 18, 0));
        va_set(t, 22, 0, 0);
        CHECK(va_get(t, 18, 0) == 67, "SYNC off: RATE %d", va_get(t, 18, 0));
        va_set(t, 22, 0, 1);
        ds = va_desc(t, 18, 0);
        CHECK(ds && ds->fmt == F_ENUM && ds->max == (int32_t)VA_NSYNC - 1 && ds->names == N_VA_SYNC, "synced RATE desc");
        va_set(t, 18, 0, 100);
        CHECK(va_get(t, 18, 0) == (int32_t)VA_NSYNC - 1, "synced RATE clamps to the list");
        va_set(t, 18, 0, 26);
        CHECK(!strcmp(ds->names[va_get(t, 18, 0)], "1/16T"), "synced RATE text %s", ds->names[va_get(t, 18, 0)]);
        CHECK(va_desc(t, 19, 0) == 0, "LFO 2 stays free");
    }
    for (bpm = 60; bpm <= 180; bpm += 60)
        for (d = 0; d < VA_NDIV3; d++) {
            uint32_t n, wraps = 0, first = 0, last = 0, prev;
            double want = BEATS[d] * 60.0 / bpm * FS / CTL, got;
            memset(t, 0, sizeof *t);
            va_blob_set(t, 0);
            va_patch[0][VA_LFO(0, VL_SYNC)] = 1;
            va_patch[0][VA_LFO(0, VL_RATE)] = (int8_t)VA_DIV3_TO_SYNC[d];
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
                  N_VA_SYNC[VA_DIV3_TO_SYNC[d]], bpm, got, want);
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

/* version 4: FENV in hundredths reaches the filter. One note through LP CUT 40, ENV 2 a decay, FENV 0.00 / 0.04 /
 * 1.00: three different sounds, 0.04 nearer 0.00 than 1.00 is; the blob carries the hundredths; every factory preset
 * as a version-3 blob loads the same patch and sounds */
static int32_t hr_out[3][200 * CTL];
static void t_hires(void)
{
    static const int32_t F[3] = {0, 4, 100};
    track_t *t = &trk[0];
    int8_t p[VA_NP], q[VA_NP];
    uint8_t b[VA_BLOB];
    uint32_t k, n;
    double d4 = 0, d100 = 0;
    for (k = 0; k < 3u; k++) {
        drv_reset(t);
        va_init_patch(p);
        p[VA_CUT] = 40;
        p[VA_ENV(1, VE_DEC)] = 90;
        p[VA_ENV(1, VE_SUS)] = 0;
        memcpy(va_patch[0], p, VA_NP);
        va_macros_out(t);
        va_set(t, 8, 3, F[k]);
        CHECK(va_get(t, 8, 3) == F[k], "FENV %d", F[k]);
        drv_on(t, 0, 48, 100);
        for (n = 0; n < 200u; n++)
            drv_tick(t, hr_out[k] + n * CTL);
    }
    for (n = 0; n < 200u * CTL; n++) {
        d4 += fabs((double)hr_out[1][n] - hr_out[0][n]);
        d100 += fabs((double)hr_out[2][n] - hr_out[0][n]);
    }
    CHECK(d4 > 0 && d100 > 2 * d4,"FENV 0.04 against 0.00: %.0f, 1.00: %.0f", d4, d100);
    CHECK(memcmp(hr_out[1], hr_out[2], sizeof hr_out[1]), "FENV 0.04 = 1.00");
    va_blob_get(t, b);                           /* (FENV 1.00 now) */
    va_set(t, 8, 3, -1234);
    va_blob_get(t, b);
    CHECK(b[1] == 4u && va_blob_ok(b) && va_unpack(b, q) && q[VA_FENV] == -13 && q[VA_FENVF] == 66, "blob -12.34");
    b[2 + VA_FENVF] = 100;
    CHECK(!va_blob_ok(b), "FENV hundredths 100 taken");
    b[2 + VA_FENVF] = 0;
    b[1] = VA_VER + 1u;
    CHECK(!va_blob_ok(b), "version 6 taken");
    for (k = 0; k < VA_NPRESETS; k++) {
        int32_t peak;
        int64_t mx;
        uint32_t left;
        va_preset_patch(k, p);
        va_pack(p, b);
        b[1] = 3;                                /* (the same preset as version 3 stored it) */
        CHECK(va_blob_ok(b) && va_unpack(b, q) && !memcmp(p, q, VA_NP), "preset %s as a v3 blob (%d)", VA_PRESETS[k].name,
              va_blob_ok(b));
        chord(k, 0.4, q, &peak, &mx, &left);
        CHECK(peak > 1000, "preset %s from v3: silent (%d)", VA_PRESETS[k].name, peak);
    }
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
                    p[VA_FTYPE] = (int8_t)(32u * f);
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
            mx = VD_FTYPE - 1;                   /* (version 1's destinations) */
        if (i == VA_FTYPE)
            mx = 3;                              /* (version 1's TYPE) */
        if (i >= VA_LFO0 && i < VA_MOD0 && (i - VA_LFO0) % VL_N == VL_SYNC)
            mx = 0;                              /* (free LFOs: a synced RATE converts, t_lfo_sync checks it) */
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
        CHECK(q[VA_FTYPE] == 32 * p[VA_FTYPE], "v1 blob %u: TYPE %d -> FTYPE %d", n, p[VA_FTYPE], q[VA_FTYPE]);
        p[VA_FTYPE] = q[VA_FTYPE];
        CHECK(!memcmp(p, q, VA_NP1), "v1 blob %u: an old value changed", n);
        CHECK(!memcmp(q + VA_NP1, init + VA_NP1, VA_NP - VA_NP1), "v1 blob %u: the new values are not the init", n);
        va_pack(q, b2);
        CHECK(b2[1] == 4u && va_blob_ok(b2) && va_unpack(b2, p) && !memcmp(p, q, VA_NP), "v1 blob %u -> v2", n);
    }
    rnd_v1(p, b1);
    b1[VA_BLOB1 - 1u] = 1;
    CHECK(!va_unpack(b1, q), "v1 blob with its padding not 0: taken");
}

/* a random version-2 patch (TYPE 0..3, MORPH 0..127 at VA_FRSV) and its version-2 blob */
static void rnd_v2(int8_t *p, uint8_t *b)
{
    uint32_t i;
    memset(b, 0, VA_BLOB);
    b[0] = VA_MAGIC;
    b[1] = 2;
    for (i = 0; i < VA_NP; i++) {
        va_rng_t r = va_range(i);
        int32_t mx = i == VA_FTYPE ? 3 : i == VA_FRSV ? 127 : i >= VA_MFINE0 ? 0 : r.max;   /* (MFINE: padding) */
        if (i >= VA_LFO0 && i < VA_MOD0 && (i - VA_LFO0) % VL_N == VL_SYNC)
            mx = 0;                              /* (free LFOs: a synced RATE converts, t_lfo_sync checks it) */
        p[i] = (int8_t)(r.min + (int32_t)(rnd() % (uint32_t)(mx - r.min + 1)));
        b[2 + i] = (uint8_t)(p[i] - r.min);
    }
}

static void t_v2(void)
{
    uint8_t b2[VA_BLOB], b3[VA_BLOB];
    int8_t p[VA_NP], q[VA_NP], r[VA_NP];
    uint32_t n, ty, m, k;
    for (ty = 0; ty < 4u; ty++)                  /* every TYPE and MORPH: FTYPE = TYPE x 32 + MORPH, round the cycle */
        for (m = 0; m < 128u; m++) {
            rnd_v2(p, b2);
            b2[2 + VA_FTYPE] = (uint8_t)ty;
            b2[2 + VA_FRSV] = (uint8_t)m;
            p[VA_FTYPE] = (int8_t)ty;
            CHECK(va_blob_ok(b2) && va_unpack(b2, q) == 1, "v2 blob %s MORPH %u not taken", N_VA_FTYPE[ty], m);
            CHECK(q[VA_FTYPE] == (int8_t)((ty * 32u + m) & 127u) && q[VA_FRSV] == 0, "v2 %s MORPH %u: FTYPE %d",
                  N_VA_FTYPE[ty], m, q[VA_FTYPE]);
            p[VA_FTYPE] = q[VA_FTYPE];
            p[VA_FRSV] = 0;
            CHECK(!memcmp(p, q, VA_NP), "v2 %s MORPH %u: another value changed", N_VA_FTYPE[ty], m);
            va_pack(q, b3);
            CHECK(b3[1] == 4u && va_blob_ok(b3) && va_unpack(b3, r) && !memcmp(q, r, VA_NP), "v2 -> v3 round trip");
        }
    rnd_v2(p, b2);
    b2[2 + VA_FTYPE] = 4;
    CHECK(!va_blob_ok(b2), "v2 blob with TYPE 4: taken");
    rnd_v2(p, b2);
    b2[1] = 3;
    b2[2 + VA_FRSV] = 5;
    CHECK(!va_blob_ok(b2), "v3 blob with its reserved value not 0: taken");
}

static void t_modes(void)
{
    track_t *t = &trk[0];
    const param_desc_t *d;
    uint32_t v, k;
    memset(t, 0, sizeof *t);
    va_blob_set(t, 0);
    for (k = 0; k < 4u; k++) {                   /* BASIC: complete descriptors, the table's */
        d = va_desc(t, 2 * k, 0);
        CHECK(d && !strcmp(d->label, "WAVE") && d->fmt == F_ENUM && d->max == VW_N - 1 && d->names == N_VA_WAVE,
              "OSC %u BASIC: WAVE desc", k + 1);
        d = va_desc(t, 2 * k + 1, 1);
        CHECK(d && !strcmp(d->label, "SHAPE") && d->fmt == F_PCT && d->max == 127, "OSC %u BASIC: SHAPE desc", k + 1);
    }
    CHECK(!va_desc(t, 8, 0) && !va_desc(t, 1, 0) && !va_desc(t, 0, 1), "desc: only WAVE / SHAPE are mode-dependent");
    va_set(t, 0, 0, VW_PWM);
    CHECK(va_get(t, 0, 0) == VW_PWM && va_patch[0][VA_OSC(0, VO_WAVE)] == VW_PWM, "BASIC: WAVE is the wave");
    va_set(t, 1, 0, VOM_NOISE);                  /* OSC 1+ MODE */
    CHECK(va_get(t, 0, 0) == VN_WHITE && va_get(t, 1, 2) == 0, "-> NOISE: WAVE %d (WHITE), KTRK %d (off)",
          va_get(t, 0, 0), va_get(t, 1, 2));
    va_set(t, 0, 0, 5);
    CHECK(va_get(t, 0, 0) == VN_VINYL && va_patch[0][VA_OSC(0, VO_WAVE)] == VN_VINYL, "NOISE: WAVE 5 -> %d (VINYL)",
          va_get(t, 0, 0));
    d = va_desc(t, 0, 0);
    CHECK(d && !strcmp(d->label, "NOISE") && d->fmt == F_ENUM && d->min == 0 && d->max == VN_N - 1 &&
              !strcmp(d->names[VN_WHITE], "WHITE") && !strcmp(d->names[VN_BROWN], "BROWN") &&
              !strcmp(d->names[VN_VINYL], "VINYL"), "NOISE desc");
    va_patch[0][VA_OSC(0, VO_WAVE)] = VW_PWM;    /* (a stored wave above VINYL: shows WHITE, as it plays) */
    CHECK(va_get(t, 0, 0) == VN_WHITE, "NOISE: a stored wave 4 reads %d", va_get(t, 0, 0));
    va_set(t, 0, 0, VN_VINYL);
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "DENS"), "VINYL's SHAPE desc");
    va_set(t, 0, 0, VN_BROWN);
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "COLOR"), "BROWN's SHAPE desc");
    va_set(t, 0, 0, VN_WHITE);
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "SHAPE"), "WHITE's SHAPE desc");
    va_set(t, 0, 0, VN_BROWN);
    va_set(t, 1, 2, 1);                          /* KTRK on: MODE NOISE again keeps it */
    va_set(t, 1, 0, VOM_NOISE);
    CHECK(va_get(t, 1, 2) == 1, "NOISE again: KTRK %d", va_get(t, 1, 2));
    /* MORPH: the WAVE column is the morph position, SHAPE's value (both columns), F_INT 0..127 named by position */
    va_set(t, 1, 0, VOM_MORPH);
    d = va_desc(t, 0, 0);
    CHECK(d && !strcmp(d->label, "MORPH") && d->fmt == F_INT && d->min == 0 && d->max == 127 && d->names,
          "MORPH: WAVE desc");
    for (v = 0; d && v < 128u && d->names[v]; v++)
        CHECK(!strcmp(d->names[v], OLD_MPV[old_morph_name((int32_t)v)]), "MORPH %u: named %s, want %s", v, d->names[v],
              OLD_MPV[old_morph_name((int32_t)v)]);
    CHECK(v == 128u && !d->names[128], "MORPH: 128 names");
    {
        static const struct { int sh; const char *nm; } NM[] = {{0, "SIN"}, {12, "SIN>TRI"}, {24, "TRI"}, {48, "SAW"},
            {60, "SAW>RMP"}, {72, "RAMP"}, {84, "RMP>SQR"}, {96, "SQR"}, {110, "SQR>PLS"}, {127, "PULSE"}};
        uint32_t j;
        for (j = 0; j < NELEM(NM); j++) {
            va_set(t, 1, 1, NM[j].sh);           /* SHAPE on OSC 1+: the WAVE column follows */
            CHECK(va_get(t, 0, 0) == NM[j].sh && !strcmp(d->names[va_get(t, 0, 0)], NM[j].nm),
                  "MORPH %d: WAVE column %d %s, want %s", NM[j].sh, va_get(t, 0, 0), d->names[va_get(t, 0, 0)], NM[j].nm);
        }
    }
    va_set(t, 0, 0, 77);                         /* KNOB 1 on the WAVE column morphs: SHAPE, the wave kept */
    CHECK(va_get(t, 0, 0) == 77 && va_get(t, 1, 1) == 77 && va_patch[0][VA_OSC(0, VO_SHAPE)] == 77 &&
              va_patch[0][VA_OSC(0, VO_WAVE)] == VN_BROWN, "MORPH: set WAVE column 77: SHAPE %d, wave %d",
          va_patch[0][VA_OSC(0, VO_SHAPE)], va_patch[0][VA_OSC(0, VO_WAVE)]);
    va_set(t, 0, 0, 300);
    CHECK(va_get(t, 0, 0) == 127 && va_get(t, 1, 1) == 127, "MORPH: WAVE column clamps to 127");
    va_set(t, 0, 0, -5);
    CHECK(va_get(t, 0, 0) == 0, "MORPH: WAVE column clamps to 0");
    d = va_desc(t, 1, 1);
    CHECK(d && !strcmp(d->label, "MORPH") && d->max == 127, "MORPH's SHAPE desc");
    d = va_desc(t, 2, 0);
    CHECK(d && !strcmp(d->label, "WAVE") && !strcmp(va_desc(t, 3, 1)->label, "SHAPE"), "OSC 2: still BASIC");
    va_set(t, 1, 0, VOM_BASIC);                  /* back to BASIC: the wave as it was */
    CHECK(va_get(t, 0, 0) == VN_BROWN && !strcmp(va_desc(t, 0, 0)->label, "WAVE"), "BASIC again: WAVE %d",
          va_get(t, 0, 0));
    {   /* the deep get / set through ENG_VA.deep (the editor's path) */
        const eng_deep_t *dp = ENG_VA.deep;
        dp->set(t, 7, 0, VOM_MORPH);             /* OSC 4+ MODE */
        dp->set(t, 6, 0, 40);
        CHECK(dp->get(t, 6, 0) == 40 && dp->get(t, 7, 1) == 40 && !strcmp(dp->desc(t, 6, 0)->label, "MORPH"),
              "OSC 4 MORPH through deep");
    }
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
    /* FTYPE 0 32 64 96 = the discrete types, bit for bit: the plain loop (the type's switch) against the crossfade
     * loop at the same position (forced by USPREAD: UNISON voice 0, its mid as without) */
    for (ty = 0; ty < 4u; ty++) {
        p[VA_FTYPE] = (int8_t)(32u * ty);
        p[VA_USPREAD] = 0;
        one_voice(p, 0, V_UNISON, NT, o1, 0);
        p[VA_USPREAD] = 127;
        memset(s2, 0, sizeof s2);
        CHECK(one_voice(p, 0, V_UNISON, NT, o2, s2), "FTYPE %u: USPREAD no side", 32u * ty);
        CHECK(!memcmp(o1, o2, sizeof o1), "FTYPE %u: not the discrete %s", 32u * ty, N_VA_FTYPE[ty]);
        CHECK(rms(o1, NT * CTL) > 100, "FTYPE %u: silent", 32u * ty);
    }
    p[VA_USPREAD] = 0;
    /* the crossfade: FTYPE 16 lies between LP and BP (it differs from both); 127 next to LP (close to it) */
    {
        static int32_t lp[NT * CTL], bp[NT * CTL];
        double dl, db, dn;
        p[VA_FTYPE] = 0;
        one_voice(p, 0, V_POLY, NT, lp, 0);
        p[VA_FTYPE] = 32;
        one_voice(p, 0, V_POLY, NT, bp, 0);
        p[VA_FTYPE] = 16;
        one_voice(p, 0, V_POLY, NT, o1, 0);
        for (i = 0; i < NT * CTL; i++) {
            s1[i] = o1[i] - lp[i];
            s2[i] = o1[i] - bp[i];
        }
        dl = rms(s1, NT * CTL);
        db = rms(s2, NT * CTL);
        CHECK(dl > 0 && db > 0 && dl < rms(lp, NT * CTL) + rms(bp, NT * CTL), "FTYPE 16: LP %.0f BP %.0f apart", dl, db);
        p[VA_FTYPE] = 127;
        one_voice(p, 0, V_POLY, NT, o1, 0);
        p[VA_FTYPE] = 100;
        one_voice(p, 0, V_POLY, NT, o2, 0);
        for (i = 0; i < NT * CTL; i++) {
            s1[i] = o1[i] - lp[i];
            s2[i] = o2[i] - lp[i];
        }
        dn = rms(s1, NT * CTL);
        CHECK(dn < rms(s2, NT * CTL), "FTYPE 127 (%.0f from LP) not nearer LP than 100 (%.0f)", dn, rms(s2, NT * CTL));
    }
    p[VA_FTYPE] = 0;
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

/* the matrix at its extremes with the version-2 modes: MORPH / NOISE oscillators, FTYPE (and its
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
                static const uint8_t D[8] = {VD_PITCH, VD_LVL1, VD_FTYPE, VD_CUT, VD_RES, VD_SHP1, VD_SHP1 + 2,
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
                p[VA_FTYPE] = (int8_t)((32u * f + w * 25u) & 127u);
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

/* the destinations appended for the editor's quick mapping (DRIVE SPRD FENV DEP1..4) and eng_deep_t.mod_dst */
static void t_dst2(void)
{
    static const uint8_t ND[8] = {VD_DRIVE, VD_SPREAD, VD_FENV, VD_DEP1, VD_DEP1 + 1, VD_DEP1 + 2, VD_DEP1 + 3, VD_FTYPE};
    static const char *const NN[8] = {"DRIVE", "SPRD", "FENV", "DEP1", "DEP2", "DEP3", "DEP4", "FTYPE"};
    track_t *t = &trk[0];
    int8_t p[VA_NP];
    uint32_t k, i, pg;
    for (k = 0; k < 8u; k++)
        CHECK(!strcmp(N_VA_DST[ND[k]], NN[k]), "DST %u is %s", ND[k], N_VA_DST[ND[k]]);
    CHECK(VD_FTYPE == 22 && VD_N == 30, "the DST numbering appended (FTYPE 22, %d destinations)", VD_N);
    /* mod_dst: a page column's destination, the MOD pages' DST numbering */
    drv_reset(t);
    va_init_patch(va_patch[0]);
    CHECK(VA_DEEP.mod_dst == va_mod_dst, "VA_DEEP.mod_dst");
    CHECK(va_mod_dst(t, 8, 0) == VD_CUT && va_mod_dst(t, 8, 1) == VD_RES && va_mod_dst(t, 8, 2) == VD_FTYPE &&
              va_mod_dst(t, 8, 3) == VD_FENV, "mod_dst FILTER");
    CHECK(va_mod_dst(t, 9, 0) == -1 && va_mod_dst(t, 9, 1) == -1 && va_mod_dst(t, 9, 2) == VD_SPREAD &&
              va_mod_dst(t, 9, 3) == VD_DRIVE, "mod_dst FILTER+");
    for (k = 0; k < 4u; k++) {
        CHECK(va_mod_dst(t, 2 * k, 0) == -1 && va_mod_dst(t, 2 * k, 1) == (int32_t)(VD_LVL1 + k) &&
                  va_mod_dst(t, 2 * k, 2) == (int32_t)(VD_PIT1 + k) && va_mod_dst(t, 2 * k, 3) == (int32_t)(VD_PIT1 + k),
              "mod_dst OSC %u", k + 1u);
        CHECK(va_mod_dst(t, 2 * k + 1, 0) == -1 && va_mod_dst(t, 2 * k + 1, 1) == (int32_t)(VD_SHP1 + k) &&
                  va_mod_dst(t, 2 * k + 1, 2) == -1, "mod_dst OSC %u+", k + 1u);
        CHECK(va_mod_dst(t, 18 + k, 0) == (int32_t)(VD_RATE1 + k) && va_mod_dst(t, 18 + k, 1) == -1 &&
                  va_mod_dst(t, 18 + k, 2) == (int32_t)(VD_DEP1 + k) && va_mod_dst(t, 18 + k, 3) == -1, "mod_dst LFO %u",
              k + 1u);
    }
    va_patch[0][VA_OMODE0 + 1] = VOM_MORPH;      /* MORPH: OSC 2's WAVE column is its SHAPE, the position */
    CHECK(va_mod_dst(t, 2, 0) == VD_SHP1 + 1, "mod_dst OSC 2 MORPH");
    for (pg = 10; pg < VA_NPAGES; pg++)
        if (pg < 18 || pg >= 22)
            for (k = 0; k < 4u; k++)
                CHECK(va_mod_dst(t, pg, k) == -1, "mod_dst page %u col %u", pg, k);
    CHECK(va_mod_dst(t, ENG_MOD_TRK, P_LEVEL) == VD_AMP && va_mod_dst(t, ENG_MOD_TRK, P_PAN) == VD_PAN &&
              va_mod_dst(t, ENG_MOD_TRK, P_TRANS) == VD_PITCH && va_mod_dst(t, ENG_MOD_TRK, P_GLIDE) == -1,
          "mod_dst track parameters");
    /* each new destination moves the sound: VEL (a constant) by +63 against none */
    for (k = 0; k < 4u; k++) {
        double d;
        va_init_patch(p);
        p[VA_OSC(0, VO_LEVEL)] = 100;
        p[VA_CUT] = 60;
        p[VA_RES] = 40;
        p[VA_FENV] = 20;
        p[VA_DRIVE] = 10;
        p[VA_FSPREAD] = 10;
        p[VA_ENV(1, VE_SUS)] = 64;
        p[VA_LFO(0, VL_DEPTH)] = 0;              /* DEP1: LFO 1 at depth 0 onto the pitch, the matrix opens it */
        p[VA_LFO(0, VL_RATE)] = 110;
        p[VA_MOD(1, VM_SRC)] = VS_LFO1;
        p[VA_MOD(1, VM_DST)] = VD_PITCH;
        p[VA_MOD(1, VM_AMT)] = 63;
        one_voice(p, 0, V_POLY, NT, o1, s1);
        p[VA_MOD(0, VM_SRC)] = VS_VEL;
        p[VA_MOD(0, VM_DST)] = (int8_t)ND[k];
        p[VA_MOD(0, VM_AMT)] = 63;
        one_voice(p, 0, V_POLY, NT, o2, s2);
        for (i = 0; i < NT * CTL; i++)
            o2[i] -= o1[i], s2[i] -= s1[i];
        d = rms(o2, NT * CTL) + rms(s2, NT * CTL);
        CHECK(d > 0.01 * rms(o1, NT * CTL), "VEL -> %s: the sound unchanged (%.1f)", NN[k], d);
        CHECK(rms(o1, NT * CTL) > 100, "VEL -> %s: silent", NN[k]);
    }
    /* their extremes: no overflow, voices end */
    {
        int32_t peak;
        int64_t mx;
        uint32_t left, sgn;
        for (sgn = 0; sgn < 2u; sgn++) {
            va_init_patch(p);
            for (k = 0; k < 4u; k++) {
                p[VA_OSC(k, VO_LEVEL)] = 127;
                p[VA_LFO(k, VL_DEPTH)] = (int8_t)(sgn ? 127 : 0);
            }
            p[VA_RES] = 127;
            p[VA_DRIVE] = 127;
            p[VA_FSPREAD] = 127;
            p[VA_FENV] = (int8_t)(sgn ? 63 : -64);
            for (k = 0; k < 8u; k++) {
                p[VA_MOD(k, VM_SRC)] = (int8_t)(k < 4u ? VS_LFO1 + k : VS_ENV1 + k - 4u);
                p[VA_MOD(k, VM_DST)] = (int8_t)ND[k];
                p[VA_MOD(k, VM_AMT)] = (int8_t)(sgn ? 63 : -64);
            }
            drv_side = 1;
            chord(0, 0.5, p, &peak, &mx, &left);
            drv_side = 0;
            CHECK(mx < (1 << 30), "new destinations' extremes s%u: |sum| %lld", sgn, (long long)mx);
            CHECK(!left, "new destinations' extremes s%u: voices left", sgn);
        }
    }
}

/* the sound of every saved matrix, bit for bit: a hash (FNV-1a) of render2's mid and side for every factory preset
 * and 64 random patches with all 8 slots active (random SRC, DST, AMT -64..63, whole), 0.25 s of a 3-note chord
 * each. VA_GOLD was computed at 5e1e8a7 (0.22-beta4, before the fine amounts, 3y110): the fine amounts must not move
 * a whole one. (Its own generator: the patches do not depend on the tests before it.) */
#define VA_GOLD 0xe63bae86u
#define VA_NP4 106u                              /* (version 4's values: the random patches use these) */
static uint32_t gr_s = 777u;
static uint32_t gr_rnd(void)
{
    gr_s = gr_s * 1664525u + 1013904223u;
    return gr_s >> 8;
}
static uint32_t gold_hash(const int8_t *patch, uint32_t k, uint32_t h)
{
    static const uint8_t CH[3] = {57, 64, 71};
    track_t *t = &trk[0];
    int32_t out[CTL];
    uint32_t n, i;
    drv_reset(t);
    if (patch) {
        memcpy(va_patch[0], patch, VA_NP);
        va_macros_out(t);
    } else {
        va_blob_preset(t, k);
    }
    for (i = 0; i < 3u; i++)
        drv_on(t, i, CH[i], 40u + 40u * i);
    for (n = 0; n < (uint32_t)(0.25 * FS / CTL); n++) {
        drv_tick(t, out);
        for (i = 0; i < CTL; i++) {
            uint32_t a = (uint32_t)out[i], b = (uint32_t)drv_sbuf[i], j;
            for (j = 0; j < 4u; j++)
                h = (h ^ ((a >> (8u * j)) & 255u)) * 16777619u;
            for (j = 0; j < 4u; j++)
                h = (h ^ ((b >> (8u * j)) & 255u)) * 16777619u;
        }
    }
    return h;
}
static void t_coarse_golden(void)
{
    int8_t p[VA_NP];
    uint32_t h = 2166136261u, k, i;
    drv_side = 1;
    for (k = 0; k < VA_NPRESETS; k++)
        h = gold_hash(0, k, h);
    for (k = 0; k < 64u; k++) {
        va_init_patch(p);
        for (i = 0; i < VA_NP4; i++) {
            va_rng_t r = va_range(i);
            p[i] = (int8_t)(r.min + (int32_t)(gr_rnd() % (uint32_t)(r.max - r.min + 1)));
        }
        p[VA_ENV(0, VE_ATK)] = 0;                /* (heard from the start) */
        p[VA_ENV(0, VE_SUS)] = 127;
        p[VA_OSC(0, VO_LEVEL)] = 110;
        for (i = 0; i < 8u; i++) {
            p[VA_MOD(i, VM_SRC)] = (int8_t)(1u + gr_rnd() % (VS_N - 1u));
            p[VA_MOD(i, VM_DST)] = (int8_t)(1u + gr_rnd() % (VD_N - 1u));
            p[VA_MOD(i, VM_AMT)] =
                (int8_t)(gr_rnd() % 2u ? 1 + (int32_t)(gr_rnd() % 63u) : -1 - (int32_t)(gr_rnd() % 64u));
        }
        h = gold_hash(p, 0, h);
    }
    drv_side = 0;
    if (getenv("VERBOSE") || VA_GOLD == 0u)
        printf("  coarse golden: 0x%08x\n", h);
    CHECK(h == VA_GOLD, "the whole amounts' sound moved: hash 0x%08x, 5e1e8a7's 0x%08x", h, VA_GOLD);
}

/* version 5, the matrix's fine amounts (issue 13): the AMT column in hundredths, tenths below 6, whole numbers from
 * 6; a slow detent a tenth below 6, a whole above; the fine flags in the blob; the pitch depth in cents at a full
 * source (VEL 127 -> PITCH): a whole step 18.75 ct as before, a tenth 1.9 ct; AMP by a tenth; the extremes */
static int32_t fine_inc(int32_t amt)            /* OSC 1's increment, note 69, VEL 127 -> PITCH by amt (hundredths) */
{
    track_t *t = &trk[0];
    int32_t out[CTL];
    drv_reset(t);
    va_init_patch(va_patch[0]);
    va_macros_out(t);
    va_set(t, 24, 0, VS_VEL);
    va_set(t, 24, 1, VD_PITCH);
    va_set(t, 24, 2, amt);
    drv_on(t, 0, 69, 127);
    drv_tick(t, out);
    return t->v[0].s[3];
}
static double fine_rms(int32_t amt)             /* VEL 127 -> AMP by amt: the level after the attack */
{
    track_t *t = &trk[0];
    int32_t out[CTL];
    double e = 0;
    uint32_t n, i;
    drv_reset(t);
    va_init_patch(va_patch[0]);
    va_macros_out(t);
    va_set(t, 24, 0, VS_VEL);
    va_set(t, 24, 1, VD_AMP);
    va_set(t, 24, 2, amt);
    drv_on(t, 0, 57, 127);
    for (n = 0; n < 60u; n++) {
        drv_tick(t, out);
        for (i = 0; n >= 20u && i < CTL; i++)
            e += (double)out[i] * out[i];
    }
    return sqrt(e / (40.0 * CTL));
}
static void t_fine(void)
{
    /* turns: from, by -> to (hundredths); a slow detent is 1 (0.01), a quick one 635 (the range / 20) */
    static const int16_t TURN[][3] = {{0, 1, 10}, {10, 1, 20}, {20, -1, 10}, {10, -1, 0}, {0, -1, -10}, {590, 1, 600},
        {600, 1, 700}, {700, -1, 600}, {600, -1, 590}, {-590, -1, -600}, {-600, -1, -700}, {-700, 1, -600},
        {-600, 1, -590}, {0, 635, 600}, {600, 635, 1200}, {0, 1270, 1200}, {0, -635, -600}, {1200, -635, 570},
        {6300, 1, 6300}, {6200, 635, 6300}, {-6400, -1, -6400}, {300, 0, 300}, {0, 45, 40}, {0, 999, 900},
        {0, 55, 50}, {0, 3333, 3300}};
    /* the pitch depth at a full source: whole amounts as before (18.75 ct a step, 63: 1181 ct), tenths a tenth */
    static const int16_t PIT[] = {10, 50, 90, 100, 590, 600, 1200, 6300, -10, -590, -6400};
    track_t *t = &trk[0];
    int8_t p[VA_NP], q[VA_NP];
    uint8_t b[VA_BLOB];
    uint32_t k, s, d;
    int32_t inc0, left, peak;
    int64_t mx;
    double r0, r, last = 0;
    drv_reset(t);
    va_init_patch(va_patch[0]);
    for (k = 0; k < NELEM(TURN); k++) {
        va_set(t, 25, 2, TURN[k][0]);
        va_set(t, 25, 2, TURN[k][0] + TURN[k][1]);
        CHECK(va_get(t, 25, 2) == TURN[k][2], "AMT %d by %d: %d, want %d", TURN[k][0], TURN[k][1], va_get(t, 25, 2),
              TURN[k][2]);
        CHECK(va_patch[0][VA_MFINE0] == (TURN[k][2] % 100 ? 2 : 0) && !va_patch[0][VA_MFINE1] &&
                  va_patch[0][VA_MOD(1, VM_AMT)] == (TURN[k][2] % 100 ? TURN[k][2] / 10 : TURN[k][2] / 100),
              "AMT %d: slot 2's flag %d, AMT %d", TURN[k][2], va_patch[0][VA_MFINE0], va_patch[0][VA_MOD(1, VM_AMT)]);
    }
    va_set(t, 25, 2, 0);
    for (s = 0; s < 8u; s++)                     /* each slot's own flag: MFINE0 bits 0..6, MFINE1 bit 0 */
        va_set(t, 24u + s, 2, s & 1u ? -(int32_t)(10u + 10u * s) : (int32_t)(10u + 10u * s));
    CHECK(va_patch[0][VA_MFINE0] == 127 && va_patch[0][VA_MFINE1] == 1, "8 fine slots: flags %d %d",
          va_patch[0][VA_MFINE0], va_patch[0][VA_MFINE1]);
    va_blob_get(t, b);
    CHECK(b[1] == 5u && b[2 + VA_MFINE0] == 127 && b[2 + VA_MFINE1] == 1 && va_blob_ok(b) && va_unpack(b, q) &&
              !memcmp(q, va_patch[0], VA_NP), "the fine flags through the blob");
    memcpy(p, va_patch[0], VA_NP);
    va_init_patch(va_patch[0]);
    va_blob_set(t, b);
    for (s = 0; s < 8u; s++)
        CHECK(va_get(t, 24u + s, 2) == (s & 1u ? -(int32_t)(10u + 10u * s) : (int32_t)(10u + 10u * s)),
              "slot %u from the blob: %d", s + 1u, va_get(t, 24u + s, 2));
    va_set(t, 31, 2, 1200);
    CHECK(va_patch[0][VA_MFINE1] == 0 && va_patch[0][VA_MFINE0] == 127, "slot 8 whole: its flag off, the others kept");
    /* a sound without a fine amount: version 4, byte for byte as 0.22-beta4 wrote it, and read back as it is (a synced
     * RATE past version 3's divisions, FENV's hundredths: no version-3 conversion) */
    va_init_patch(p);
    p[VA_LFO(0, VL_SYNC)] = 1;
    p[VA_LFO(0, VL_RATE)] = 30;
    p[VA_FENVF] = 66;
    p[VA_MOD(0, VM_SRC)] = VS_LFO1;
    p[VA_MOD(0, VM_DST)] = VD_PITCH;
    p[VA_MOD(0, VM_AMT)] = 3;
    va_pack(p, b);
    CHECK(b[1] == 4u && !b[2 + VA_MFINE0] && !b[2 + VA_MFINE1] && va_blob_ok(b) && va_unpack(b, q) &&
              !memcmp(p, q, VA_NP), "no fine amount: a version-4 blob, read back as it is");
    /* the pitch (the render's own resolution: 1/256 st floored, then the fine factor's linear step: within 1 ct) */
    inc0 = fine_inc(0);
    for (k = 0; k < NELEM(PIT); k++) {
        double c = 1200.0 * log2((double)fine_inc(PIT[k]) / inc0), want = PIT[k] / 100.0 * 18.75;
        if (getenv("VERBOSE"))
            printf("  AMT %+6.2f -> PITCH %+8.2f ct (%+8.2f)\n", PIT[k] / 100.0, c, want);
        CHECK(fabs(c - want) < 1.0 + fabs(want) * 0.002, "AMT %.2f: %.2f ct, want %.2f", PIT[k] / 100.0, c, want);
        CHECK(k == 0 || PIT[k] < 0 || c > last, "AMT %.2f: %.2f ct, not above AMT %.2f's %.2f", PIT[k] / 100.0, c,
              PIT[k - 1] / 100.0, last);
        last = c;
    }
    /* AMP: VEL 127 closing it by 0.5, 5.9, 6 */
    r0 = fine_rms(0);
    r = fine_rms(-50) / r0;
    CHECK(fabs(r - (1 - 0.5 / 64)) < 0.002, "AMP -0.5: level %.4f, want %.4f", r, 1 - 0.5 / 64);
    r = fine_rms(-590) / r0;
    CHECK(fabs(r - (1 - 5.9 / 64)) < 0.002, "AMP -5.9: level %.4f, want %.4f", r, 1 - 5.9 / 64);
    r = fine_rms(-600) / r0;
    CHECK(fabs(r - (1 - 6.0 / 64)) < 0.002, "AMP -6: level %.4f, want %.4f", r, 1 - 6.0 / 64);
    /* the extremes: every destination from every slot in tenths, a blob's -6.4 and +6.3 too (no overflow, the voices
     * end) */
    drv_side = 1;
    for (d = 1; d < VD_N; d++)
        for (k = 0; k < 2u; k++) {
            va_init_patch(p);
            p[VA_ENV(0, VE_SUS)] = 127;
            for (s = 0; s < 8u; s++) {
                p[VA_MOD(s, VM_SRC)] = (int8_t)(1u + (s + d) % (VS_N - 1u));
                p[VA_MOD(s, VM_DST)] = (int8_t)d;
                p[VA_MOD(s, VM_AMT)] = (int8_t)(k ? 63 : -64);
            }
            p[VA_MFINE0] = 127;
            p[VA_MFINE1] = 1;
            chord(0, 0.3, p, &peak, &mx, (uint32_t *)&left);
            CHECK(mx < (1 << 30) && !left, "fine extremes DST %s %s: |sum| %lld, voices %d", N_VA_DST[d], k ? "+" : "-",
                  (long long)mx, left);
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
    t_hires();
    t_v1();
    t_v2();
    t_modes();
    t_morph();
    t_noise();
    t_fmorph_spread();
    t_extremes2();
    t_dst2();
    t_coarse_golden();
    t_fine();
    printf("cr_va_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
