/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of GRAIN (firmware/src/grain_src.c, the generator: docs/GRAIN.md step 1; firmware/src/eng_grain.c, the
 * engine and its render scheduler: steps 2 and 3), no HAL: on core.h, dsp.c and felucca_tables.h with a minimal voice
 * driver (voice.c's block: the engine's block, then each voice's render under the platform's amplitude).
 *   sh tests/run_cr_tests.sh          (or: cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Ibuild/gen
 *                                      -o build/host/cr_grain_test tests/cr_grain_test.c -lm && build/host/cr_grain_test)
 * Checks: every family at a grid of TONE x MOTION x SEED (800 renders) renders a whole buffer whose peak is GS_PEAK
 * (within the Q12 gain's rounding, or below it when the gain capped at 8), the mean under 1 % of full scale (the
 * finish pass's high-pass leaves what drifts slower than 20 Hz: VOWEL's formants settling, up to 0.6 %), RMS above 1 %
 * (not silent), the same bytes
 * twice (deterministic), and the same bytes in slices of 512 as in one call; slices of one sample for one setting a
 * family; the root pitch a family reports; the knobs' masking (TYPE wraps, TONE / MOTION / SEED take seven bits);
 * eight seeds a family give eight different buffers; the 24 sketch settings (tools/grain_sketch.c SRC_SET) hash to
 * the goldens of tests/cr_grain_golden.txt (GOLDEN_UPDATE=1 rewrites it: on purpose, review the diff) and, where
 * build/grain-sketch/src-*.wav exists, equal the WAV heard byte for byte; the host time a render (information).
 * The engine (sections 5 ..): the blob's round trip and a bad blob; the pages' map, get / set, the macros both ways;
 * the presets' macros as their patches; the engine on two static buffers: no grain reads outside its buffer at
 * extreme knobs, the pool's shares, MORPH as a mix (0: A alone, 127: B alone, 64: the two outputs mixed sample for
 * sample; SHP the knob's path; LFO 1 -> MORPH swings it and ramps a sample), the rate's cap, the window's continuity
 * over block boundaries (a DC buffer: the output is the window), 1.4 s of a 6-note chord on every preset (no int32
 * wrap, peak under 0.9 FS at LEVEL 92, the grains gone after the voices); the scheduler (GRAIN_ASYNC 1, the unit's
 * path, the audio tick and the poll called in turn), the render in place: a load renders both sources, a knob
 * change measures (nothing written) then writes over the old material while the grains play on (never none, never
 * a silent block; new below the head, old above), the finished buffer the generator's whole render byte for byte
 * (every family), the grains under the writes step smoothly, latest wins at a render's end (a change mid-render lets
 * it finish, then the latest; ten changes in a row: two renders; a knob turned on and on: the renders land as the
 * polls go, the other source's one change next), a second part on GRAIN shares and does not render, the old
 * Felucca record's mapping. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#define __attribute__(x)
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#define GRAIN_ASYNC 1
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#include "../firmware/src/eng_grain.c"
#ifndef ENGI_FM6
#define ENGI_FM6 12u
#endif
static uint32_t rs = 12345u;
static uint32_t rnd(void)
{
    rs = rs * 1664525u + 1013904223u;
    return rs >> 8;
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

static int16_t bufa[GS_N], bufb[GS_N];

static uint64_t fnv(const int16_t *b, uint32_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint8_t)b[i];
        h *= 0x100000001b3ull;
        h ^= (uint8_t)(b[i] >> 8);
        h *= 0x100000001b3ull;
    }
    return h;
}
/* a whole render, in one call or in slices of `slice` samples; returns gs_finish's gain */
static int32_t render(int16_t *buf, gs_t *g, uint32_t type, uint32_t tone, uint32_t motion, uint32_t seed, uint32_t slice)
{
    gs_begin(g, type, tone, motion, seed);
    while (gs_step(g, buf, slice) < GS_N)
        ;
    return gs_finish(buf);
}
static void stats(const int16_t *b, int32_t *peak, double *dc, double *rms)
{
    uint32_t i;
    double sum = 0, sq = 0;
    int32_t pk = 0;
    for (i = 0; i < GS_N; i++) {
        int32_t a = b[i] < 0 ? -b[i] : b[i];
        pk = a > pk ? a : pk;
        sum += b[i];
        sq += (double)b[i] * b[i];
    }
    *peak = pk;
    *dc = sum / GS_N;
    *rms = sqrt(sq / GS_N);
}

/* ---- 1. the grid: whole, deterministic, sliced, the levels */
static void t_grid(void)
{
    static const uint8_t TONES[5] = {0, 28, 64, 104, 127}, MOTIONS[5] = {0, 20, 64, 104, 127}, SEEDS[4] = {0, 3, 11, 127};
    uint32_t ty, a, b, c, n = 0, capped = 0;
    gs_t g;
    for (ty = 0; ty < GS_NTYPE; ty++)
        for (a = 0; a < 5; a++)
            for (b = 0; b < 5; b++)
                for (c = 0; c < 4; c++) {
                    int32_t gain = render(bufa, &g, ty, TONES[a], MOTIONS[b], SEEDS[c], GS_N), peak;
                    double dc, rms;
                    stats(bufa, &peak, &dc, &rms);
                    n++;
                    if (gain == 8 * 4096) {
                        capped++;
                        CHECK(peak <= GS_PEAK, "%s %u/%u/%u: capped gain, peak %d", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c], peak);
                    } else
                        CHECK(peak >= GS_PEAK - 8 && peak <= GS_PEAK, "%s %u/%u/%u: peak %d (gain %d)", GS_TYPE_NAMES[ty],
                              TONES[a], MOTIONS[b], SEEDS[c], peak, gain);
                    CHECK(fabs(dc) < 328.0, "%s %u/%u/%u: DC %.1f", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c], dc);
                    CHECK(rms > 328.0, "%s %u/%u/%u: RMS %.1f (silent?)", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c], rms);
                    render(bufb, &g, ty, TONES[a], MOTIONS[b], SEEDS[c], GS_N);
                    CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s %u/%u/%u: not deterministic", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c]);
                    render(bufb, &g, ty, TONES[a], MOTIONS[b], SEEDS[c], 512u);
                    CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s %u/%u/%u: slices of 512 differ", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c]);
                }
    printf("grid: %u renders, %u with the gain capped at 8\n", n, capped);
    for (ty = 0; ty < GS_NTYPE; ty++) {                      /* slices of one sample */
        render(bufa, &g, ty, 64, 64, 0, GS_N);
        render(bufb, &g, ty, 64, 64, 0, 1u);
        CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s: slices of 1 differ", GS_TYPE_NAMES[ty]);
        CHECK(g.pos == GS_N && gs_step(&g, bufb, 100u) == GS_N, "%s: a step past the end", GS_TYPE_NAMES[ty]);
    }
}

/* ---- 2. the roots, the knobs' masking, the seeds */
static void t_knobs(void)
{
    uint32_t ty, s;
    gs_t g;
    for (ty = 0; ty < GS_NTYPE; ty++) {
        uint64_t h[8];
        render(bufa, &g, ty, 64, 64, 0, GS_N);
        CHECK(g.root16 == (ty == GT_PLUCK ? GS_PLUCK_ROOT16 : GS_ROOT16), "%s: root16 %d", GS_TYPE_NAMES[ty], g.root16);
        render(bufb, &g, ty + GS_NTYPE, 64 + 128, 64 + 128, 0 + 128, GS_N);   /* TYPE wraps, the others take 7 bits */
        CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s: the knobs' masking", GS_TYPE_NAMES[ty]);
        for (s = 0; s < 8; s++) {
            uint32_t k;
            render(bufb, &g, ty, 64, 64, s, GS_N);
            h[s] = fnv(bufb, GS_N);
            for (k = 0; k < s; k++)
                CHECK(h[k] != h[s], "%s: seeds %u and %u render the same", GS_TYPE_NAMES[ty], k, s);
        }
    }
    gs_begin(&g, 0, 0, 0, 0);
    CHECK(g.pos == 0 && gs_step(&g, bufa, 0) == 0, "an empty step");
}

/* ---- 3. the sketch settings' goldens, and the WAVs heard */
static const struct { const char *name; uint8_t tone, motion, seed; } SRC_SET[3] = {
    {"mid", 64, 64, 0}, {"dark-slow", 28, 20, 3}, {"bright-fast", 104, 104, 11}};
static int wav_data(const char *path, int16_t *out, uint32_t n)   /* the WAV's samples, 1 = read */
{
    FILE *f = fopen(path, "rb");
    uint8_t hdr[44];
    if (!f)
        return 0;
    if (fread(hdr, 1, 44, f) != 44 || memcmp(hdr, "RIFF", 4) || fread(out, 2, n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 1;
}
static void t_golden(void)
{
    const char *gpath = "tests/cr_grain_golden.txt";
    int update = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    char line[128], key[64], gold[25 * 64][64];
    uint64_t gh[25 * 64];
    uint32_t ng = 0, ty, s, i, heard = 0;
    gs_t g;
    FILE *f = update ? fopen(gpath, "w") : fopen(gpath, "r");
    CHECK(f != 0, "%s: cannot open", gpath);
    if (!f)
        return;
    if (!update)
        while (fgets(line, sizeof line, f) && ng < 25 * 64)
            if (sscanf(line, "%63s %llx", gold[ng], (unsigned long long *)&gh[ng]) == 2)
                ng++;
    for (ty = 0; ty < GS_NTYPE; ty++)
        for (s = 0; s < 3; s++) {
            uint64_t h;
            char fam[16], path[128];
            render(bufa, &g, ty, SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed, 512u);
            h = fnv(bufa, GS_N);
            snprintf(key, sizeof key, "%s/%u/%u/%u", GS_TYPE_NAMES[ty], SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed);
            if (update)
                fprintf(f, "%s %016llx\n", key, (unsigned long long)h);
            else {
                for (i = 0; i < ng && strcmp(gold[i], key); i++)
                    ;
                CHECK(i < ng, "%s: no golden", key);
                if (i < ng)
                    CHECK(gh[i] == h, "%s: %016llx, golden %016llx", key, (unsigned long long)h, (unsigned long long)gh[i]);
            }
            for (i = 0; GS_TYPE_NAMES[ty][i]; i++)
                fam[i] = (char)(GS_TYPE_NAMES[ty][i] | 32);
            fam[i] = 0;
            snprintf(path, sizeof path, "build/grain-sketch/src-%s-%s.wav", fam, SRC_SET[s].name);
            if (wav_data(path, bufb, GS_N) == 1) {
                heard++;
                CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s: not the WAV heard (%s)", key, path);
            }
        }
    fclose(f);
    printf("goldens: %s%s; %u of 24 sketch WAVs present and compared\n", update ? "written to " : "", gpath, heard);
}

/* ---- 4. the host time a render (information) */
static void t_time(void)
{
    uint32_t ty, k;
    gs_t g;
    printf("host ms a render:");
    for (ty = 0; ty < GS_NTYPE; ty++) {
        clock_t t0 = clock();
        for (k = 0; k < 20; k++)
            render(bufa, &g, ty, 64, 64, k, 1024u);
        printf(" %s %.2f", GS_TYPE_NAMES[ty], 1000.0 * (clock() - t0) / CLOCKS_PER_SEC / 20);
    }
    printf("\n");
}


/* ============================================================================= the engine === */
/* the voice driver: voice.c's block for an engine under the platform's amplitude (the ADSR held open) */
static void drv_reset(void)
{
    memset(trk, 0, sizeof trk);
    memset(gp_parts, 0, sizeof gp_parts);
    song.g[G_BPM] = 120;
    memset(grain_patch, 0, sizeof grain_patch);
    memset(grain_mlast, 0, sizeof grain_mlast);
    trk[0].p[P_VOICE] = V_POLY;
    trk[1].p[P_VOICE] = V_MONO;
    trk[0].eng_req = trk[0].engine = ENGI_GRAIN;
    trk[1].eng_req = trk[1].engine = ENGI_FM6;
    grs.req = 0;
    grs.stage = 0;
    memset(grs.cur, 0xFF, sizeof grs.cur);
    memset(grs.want, 0, sizeof grs.want);
}
static void drv_on(track_t *t, uint32_t i, uint32_t note)
{
    voice_t *v = &t->v[i];
    int sounding = v->active;
    v->note = (uint8_t)note;
    v->vel = v->mvel = 100;
    v->gate = v->active = 1;
    v->stage = 1;
    v->age = i + 1u;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    if (!sounding)
        v->env = v->env_out = 0;
    grain_note_on(t, v);
    v->env = 1 << 24;
}
/* the audio side's block: the tick (the ISR's, core 0), the part's block, its voices; the poll is the test's */
static uint32_t drv_tick(track_t *t, int32_t *out, int32_t shape)
{
    uint32_t i, nr = 0;
    memset(out, 0, CTL * sizeof *out);
    grain_tick();
    grain_block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        if (!v->active)
            continue;
        memset(&m, 0, sizeof m);
        m.amp0 = m.amp1 = 32767;
        m.envq15 = 32767;
        m.pitch16 = v->pitch_cur;
        m.inc = pitch_inc((uint32_t)m.pitch16);
        m.shape = shape;
        grain_render(t, v, out, CTL, &m);
        nr++;
    }
    return nr;
}
/* the main loop and the audio side in turn until nothing is pending (at most n turns); returns the polls it took */
static uint32_t settle(uint32_t n)
{
    uint32_t k;
    int32_t out[CTL];
    for (k = 0; k < n; k++) {                    /* (a first tick always: it is what posts a load's request) */
        drv_tick(&trk[0], out, 64 << 8);
        grain_poll();
        if (!grs.req && !grs.stage)
            break;
    }
    return k + 1u;
}
static uint32_t grains_on(const gp_part_t *P)   /* the part's grains sounding */
{
    uint32_t i, n = 0;
    for (i = 0; i < GP_NG; i++)
        n += P->g[i].owner != 0;
    return n;
}

/* ---- 5. the blob, the pages, the macros, the presets */
static void t_patch(void)
{
    track_t *t = &trk[0];
    uint8_t b[GRAIN_BLOB];
    int8_t p[GQ_NP], q[GQ_NP];
    uint32_t n, i, pg, c;
    drv_reset();
    for (n = 0; n < 500u; n++) {
        for (i = 0; i < GQ_NP; i++) {
            grain_rng_t r = grain_range(i);
            p[i] = (int8_t)(r.min + (int32_t)(rnd() % (uint32_t)(r.max - r.min + 1)));
        }
        grain_pack(p, b);
        CHECK(b[0] == GRAIN_MAGIC && b[1] == GRAIN_VER, "blob head");
        CHECK(grain_unpack(b, q) && !memcmp(p, q, GQ_NP), "blob round trip %u", n);
        for (i = GQ_NP + 2u; i < GRAIN_BLOB; i++)
            CHECK(!b[i], "blob padding");
    }
    {
        int8_t init[GQ_NP];
        uint8_t v1[GRAIN_BLOB_V1];
        grain_init_patch(init);
        b[0] = 0;
        CHECK(!grain_unpack(b, q), "a bad magic");
        grain_blob_set(t, b);
        CHECK(!memcmp(grain_patch[0], init, GQ_NP), "a bad blob -> the init patch");
        grain_blob_set(t, 0);
        CHECK(!memcmp(grain_patch[0], init, GQ_NP), "no blob -> the init patch");
        for (i = 0; i < 8u; i++)
            CHECK(t->p[P_E0 + i] == init[GRAIN_MAC[i]] && t->p[P_E0 + i] == ENG_GRAIN.edit[i].def,
                  "macro %u out: %d (def %d / %d)", i, t->p[P_E0 + i], init[GRAIN_MAC[i]], ENG_GRAIN.edit[i].def);
        /* a version-1 blob (the first build on the unit, 3y136): its 18 values, the rest the defaults, SCAN halved */
        memset(v1, 0, sizeof v1);
        v1[0] = GRAIN_MAGIC;
        v1[1] = 1;
        for (i = 0; i < GQ_NP_V1; i++)
            v1[2 + i] = (uint8_t)(i == GQ_PITCH ? -7 : i == GQ_SCAN ? 60 : 3 + i);
        CHECK(grain_unpack(v1, q), "a version-1 blob unpacks");
        CHECK(q[GQ_TYPE1] == 3 && q[GQ_TONE2] == 8 && q[GQ_PITCH] == -7 && q[GQ_SCAN] == 30 && q[GQ_RES] == 20 &&
              q[GQ_FTYPE] == init[GQ_FTYPE] && q[GQ_KTRK] == 64 && q[GQ_LFO(1, GL_DEPTH)] == 127 && q[GQ_MOD(7, GM_AMT)] == 0,
              "version 1: the 18 kept (SCAN 60 -> 30), the new values their defaults");
        CHECK(grain_range(GQ_SCAN).min == -64 && grain_range(GQ_PITCH).min == -24 && grain_range(GQ_MOD(0, GM_AMT)).min == -64 &&
              grain_range(GQ_MOD(0, GM_SRC)).max == GSR_N - 1 && grain_range(GQ_MOD(0, GM_DST)).max == GD_N - 1 &&
              grain_range(GQ_LFO(0, GL_WAVE)).max == 4 && grain_range(GQ_LFO(0, GL_SYNC)).max == 1, "the ranges");
    }
    /* the pages: every column a patch value in range or a track parameter; get / set round trip; set clamps */
    for (pg = 0; pg < GRAIN_NPAGES; pg++)
        for (c = 0; c < 4u; c++) {
            const param_desc_t *d = &GRAIN_PAGES[pg].col[c];
            uint32_t m = GRAIN_MAP[pg][c];
            if (m == GQ_X) {
                CHECK(!d->label, "page %u col %u: empty column with a label", pg, c);
                continue;
            }
            CHECK(d->label != 0, "page %u col %u: no label", pg, c);
            if (m & 0x80u) {
                CHECK((m & 0x7Fu) < P_E0, "page %u col %u: track id %u", pg, c, m & 0x7Fu);
                grain_set(t, pg, c, d->max + 50);
                CHECK(grain_get(t, pg, c) == d->max && t->p[m & 0x7Fu] == d->max, "page %u col %u: track set clamps", pg, c);
                grain_set(t, pg, c, d->min - 50);
                CHECK(grain_get(t, pg, c) == d->min, "page %u col %u: track set clamps low", pg, c);
                continue;
            }
            if (d->fmt == F_FIX2) {               /* an AMT in hundredths: tenths below 6, whole from 6 (the VA's rule) */
                grain_set(t, pg, c, 0);
                grain_set(t, pg, c, 10);
                CHECK(grain_get(t, pg, c) == 10, "page %u col %u: AMT 0.10 (%d)", pg, c, grain_get(t, pg, c));
                grain_set(t, pg, c, 6350);
                CHECK(grain_get(t, pg, c) == 6300, "page %u col %u: AMT 63 (%d)", pg, c, grain_get(t, pg, c));
                grain_set(t, pg, c, -6400);
                CHECK(grain_get(t, pg, c) == -6400, "page %u col %u: AMT -64 (%d)", pg, c, grain_get(t, pg, c));
                grain_set(t, pg, c, -250);
                CHECK(grain_get(t, pg, c) == -250 && grain_mfine(grain_patch[0], (m - GQ_MOD0) / GM_N),
                      "page %u col %u: AMT -2.5 in tenths (%d)", pg, c, grain_get(t, pg, c));
                grain_set(t, pg, c, 0);
                CHECK(grain_get(t, pg, c) == 0 && !grain_mfine(grain_patch[0], (m - GQ_MOD0) / GM_N), "page %u col %u: AMT 0", pg, c);
                continue;
            }
            {
                grain_rng_t r = grain_range(m);
                CHECK(m < GQ_NP && d->min == r.min && d->max == r.max && d->def == r.def,
                      "page %u col %u: range %d..%d def %d against the patch's %d..%d %d", pg, c, d->min, d->max, d->def,
                      r.min, r.max, r.def);
            }
            grain_set(t, pg, c, d->max + 50);
            CHECK(grain_get(t, pg, c) == d->max, "page %u col %u: set clamps", pg, c);
            grain_set(t, pg, c, d->min - 50);
            CHECK(grain_get(t, pg, c) == d->min, "page %u col %u: set clamps low", pg, c);
            grain_set(t, pg, c, d->def);
            CHECK(grain_get(t, pg, c) == d->def, "page %u col %u: round trip", pg, c);
        }
    CHECK(GRAIN_DEEP.section[0] == GPG_SRC1 && GRAIN_DEEP.section[1] == GPG_FILTER && GRAIN_DEEP.section[2] == GPG_ENV1 &&
          GRAIN_DEEP.section[3] == GPG_LFO1 && GRAIN_DEEP.section[4] == GPG_MOD0 && GRAIN_DEEP.section[5] == 0xFF,
          "the sections OSC FILTER ENV LFO MOD");
    CHECK(GRAIN_DEEP.nscreens == 4u && GRAIN_SCREENS[0].band == ENG_B_WAVE && GRAIN_SCREENS[1].pg[0] == GPG_GRAIN &&
          GRAIN_SCREENS[1].band == ENG_B_WAVE && GRAIN_SCREENS[2].sec == 2 && GRAIN_SCREENS[2].band == ENG_B_ENV &&
          GRAIN_SCREENS[3].pg[0] == GPG_ENV2 && GRAIN_SCREENS[3].pg[1] == 0xFF,
          "the plan: the two OSC screens under the wave band, ENV 1 and ENV 2 under the AHDSR");
    /* LFO 1's RATE: synced, the VA's list as its descriptor and its range; the rescale both ways */
    grain_set(t, GPG_LFO1, 0, 64);
    grain_set(t, GPG_LFOSYN, 0, 1);
    CHECK(grain_desc(t, GPG_LFO1, 0) == &VA_D_SRATE && grain_get(t, GPG_LFO1, 0) == 64 * (int32_t)VA_NSYNC / 128,
          "SYNC on: the list, RATE at the same place (%d)", grain_get(t, GPG_LFO1, 0));
    grain_set(t, GPG_LFO1, 0, 99);
    CHECK(grain_get(t, GPG_LFO1, 0) == (int32_t)VA_NSYNC - 1, "a synced RATE stays on the list");
    grain_set(t, GPG_LFOSYN, 0, 0);
    CHECK(!grain_desc(t, GPG_LFO1, 0) && grain_get(t, GPG_LFO1, 0) == 41 * 128 / (int32_t)VA_NSYNC,
          "SYNC off: the free scale, the list's last at 124 (%d)", grain_get(t, GPG_LFO1, 0));
    CHECK(grain_mod_dst(t, GPG_GRAINP, 0) == GD_MORPH && grain_mod_dst(t, GPG_FILTER, 0) == GD_CUT &&
          grain_mod_dst(t, GPG_LFO2, 2) == GD_DEP2 && grain_mod_dst(t, GPG_SRC1, 1) < 0 && grain_mod_dst(t, GPG_ENV1, 0) < 0,
          "the quick mapping's destinations");
    /* the macros both ways: set writes P_E; a P_E change reaches the patch in block() */
    grain_set(t, 3, 0, 77);                      /* GRAIN+'s MORPH */
    CHECK(t->p[P_E4] == 77 && grain_patch[0][GQ_MORPH] == 77, "set -> P_E4 (MORPH)");
    t->p[P_E5] = 33;                             /* SIZE by the knob */
    grain_block(t);
    CHECK(grain_patch[0][GQ_SIZE] == 33 && grain_get(t, 2, 0) == 33, "P_E5 -> SIZE in block");
    t->p[P_E0] = 99;                             /* TYPE out of range: clamped into the patch */
    grain_block(t);
    CHECK(grain_patch[0][GQ_TYPE1] == GS_NTYPE - 1, "P_E0 clamps to the last type");
    /* the presets: their macros are their patches' */
    for (n = 0; n < GRAIN_NPRESETS; n++) {
        grain_blob_preset(t, n);
        for (i = 0; i < 8u; i++)
            CHECK(t->p[P_E0 + i] == GRAIN_PRESETS[n].e[i], "%s: macro %u", GRAIN_PRESETS[n].name, i);
        for (i = 0; i < GQ_NP; i++)
            CHECK(grain_patch[0][i] >= grain_range(i).min && grain_patch[0][i] <= grain_range(i).max, "%s: value %u out of range",
                  GRAIN_PRESETS[n].name, i);
        for (i = 0; i < 8u; i++) {                /* a slot's source and destination in range */
            int32_t sr = grain_patch[0][GQ_MOD(i, GM_SRC)], d = grain_patch[0][GQ_MOD(i, GM_DST)];
            CHECK(sr >= 0 && sr < (int32_t)GSR_N && d >= 0 && d < (int32_t)GD_N, "%s: slot %u", GRAIN_PRESETS[n].name, i);
        }
    }
    grain_blob_preset(t, 4);                      /* STRUM: ENV 2 -> MORPH */
    CHECK(grain_patch[0][GQ_MOD(0, GM_SRC)] == GSR_ENV2 && grain_patch[0][GQ_MOD(0, GM_DST)] == GD_MORPH &&
          grain_patch[0][GQ_MOD(0, GM_AMT)] == 63 && grain_patch[0][GQ_E2SUS] == 0, "STRUM's ENV 2 on MORPH");
    /* a part above the two: the defaults, nothing written */
    CHECK(grain_get(&trk[2], 0, 0) == grain_range(GQ_TYPE1).def, "part 2 reads the defaults");
    grain_set(&trk[2], 0, 1, 5);
    CHECK(grain_get(&trk[2], 0, 1) == grain_range(GQ_TONE1).def, "part 2 is not written");
    /* track_loaded: a preset's macros on the track bring its patch; INIT's macros the init patch; an old Felucca
     * record (pend, no blob) its grain knobs mapped */
    drv_reset();
    t->preset = 3;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = GRAIN_PRESETS[3].e[i];
    grain_track_loaded(t);
    {
        int8_t pp[GQ_NP];
        grain_preset_patch(3, pp);
        CHECK(!memcmp(grain_patch[0], pp, GQ_NP), "track_loaded: the preset's patch");
    }
    t->preset = 0;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = ENG_GRAIN.edit[i].def;
    t->p[P_E1] = 100;
    grain_track_loaded(t);
    CHECK(grain_patch[0][GQ_TONE1] == 100 && grain_patch[0][GQ_TYPE2] == GT_BELL, "track_loaded: init + the macros");
    grain_user_pending = 1;
    grain_store_read = 0;
    {
        static const int16_t OLD[8] = {2, 50, 92, 88, -7, 40, 14, 100};   /* SRC POS SIZE DENS PTCH SPRD RAND TONE */
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = OLD[i];
    }
    grain_track_loaded(t);
    CHECK(grain_patch[0][GQ_SIZE] == 92 && grain_patch[0][GQ_DENS] == 88 && grain_patch[0][GQ_POS] == 50 &&
          grain_patch[0][GQ_SPRD] == 40 && grain_patch[0][GQ_PITCH] == -7 && grain_patch[0][GQ_RAND] == 14 &&
          grain_patch[0][GQ_CUT] == 100 && grain_patch[0][GQ_TYPE1] == GT_DRONE, "an old record's grain knobs mapped");
    CHECK(!grain_user_pending, "pending cleared");
}

/* ---- 6. the engine on two static buffers */
static void t_engine(void)
{
    track_t *t = &trk[0];
    gp_part_t *P = grain_part(0);
    int32_t out[CTL];
    uint32_t i, n, k;
    static const uint8_t CH[6] = {62, 66, 69, 71, 73, 76};
    drv_reset();
    grain_blob_preset(t, 0);
    n = settle(200);
    CHECK(!grs.req && !grs.stage && grs.renders >= 2u, "a load renders both sources (%u turns)", n);
    CHECK(!memcmp(grs.cur[0], grs.want[0], 4) && !memcmp(grs.cur[1], grs.want[1], 4), "the buffers hold the knobs");
    CHECK(grs.root16[0] == GS_ROOT16 && grs.root16[1] == GS_ROOT16, "roots");
    /* extreme knobs: no grain reads outside its buffer; grains come and go */
    {
        static const int8_t EXT[][6] = {   /* SIZE DENS POS SPRD RAND PITCH */
            {127, 127, 0, 127, 127, 24}, {127, 127, 127, 127, 127, -24}, {0, 127, 0, 0, 0, 24}, {0, 127, 127, 0, 0, -24},
            {127, 0, 64, 0, 127, 0}, {64, 127, 127, 127, 127, 24}};
        uint32_t e, worst = 0;
        for (e = 0; e < NELEM(EXT); e++) {
            int8_t *p = grain_patch[0];
            drv_reset();
            grain_blob_preset(t, 0);
            p[GQ_SIZE] = EXT[e][0];
            p[GQ_DENS] = EXT[e][1];
            p[GQ_POS] = EXT[e][2];
            p[GQ_SPRD] = EXT[e][3];
            p[GQ_RAND] = EXT[e][4];
            p[GQ_PITCH] = EXT[e][5];
            p[GQ_MORPH] = 64;
            p[GQ_SCAN] = (int8_t)(e & 1u ? 63 : -64);
            grain_macros_out(t);
            settle(200);
            for (i = 0; i < 6; i++)
                drv_on(t, i, e & 1u ? 96u : 24u);
            for (n = 0; n < 2u * FS / CTL; n++) {
                drv_tick(t, out, 64 << 8);
                for (k = 0; k < GP_NG; k++)
                    if (P->g[k].owner) {
                        const gp_grain_t *g = &P->g[k];
                        CHECK(g->pos <= GS_N, "ext %u: grain %u pos %u", e, k, g->pos);
                        CHECK(g->step >= 256u && g->step <= GP_STEP_MAX, "ext %u: step %u", e, g->step);
                        worst = grains_on(P) > worst ? grains_on(P) : worst;
                    }
            }
            CHECK(worst <= GP_NG, "the pool");
        }
    }
    /* the pool's shares: one voice may hold the pool; six voices up to 2 .. 16 / 6 each */
    drv_reset();
    grain_blob_preset(t, 0);
    grain_patch[0][GQ_DENS] = 127;
    grain_patch[0][GQ_SIZE] = 127;
    settle(200);
    drv_on(t, 0, 60);
    for (n = 0; n < FS / CTL; n++)
        drv_tick(t, out, 64 << 8);
    CHECK(grains_on(P) >= 8u, "one voice fills the pool (%u)", grains_on(P));
    for (i = 1; i < 6; i++)
        drv_on(t, i, CH[i]);
    for (n = 0; n < FS / CTL; n++) {
        drv_tick(t, out, 64 << 8);
        for (i = 0; i < 6; i++) {
            uint32_t mine = 0;
            for (k = 0; k < GP_NG; k++)
                mine += P->g[k].owner == i + 1u;
            CHECK(mine <= GP_NG, "voice %u: %u grains", i, mine);
        }
    }
    CHECK(grains_on(P) <= GP_NG, "the pool at six voices");
    /* MORPH: a mix. The grains are the same whatever MORPH is (where and when they start does not depend on it), so
     * under a held chord: MORPH 0 is A alone (B's buffer zeroed: the same output), 127 B alone (A's zeroed: the
     * same), and 64 the mix of the two outputs at B's weight 16513 / 32768, sample for sample (to the rounding); SHP
     * and the knob are one path (MORPH 0 + SHP +63 = MORPH 63); the matrix (LFO 1 -> MORPH) moves it */
    {
        static int32_t o[5][FS / 2];
        static int16_t keep[2][GS_N];
        static const int32_t M[5] = {0, 127, 64, 0, 127};
        int64_t err = 0, pk = 0, d01 = 0;
        for (k = 0; k < 5; k++) {
            drv_reset();
            grain_blob_preset(t, 0);
            grain_patch[0][GQ_MORPH] = (int8_t)M[k];
            grain_patch[0][GQ_DENS] = 110;
            grain_macros_out(t);
            settle(400);
            if (!k)
                memcpy(keep, grain_buf, sizeof keep);
            memcpy(grain_buf, keep, sizeof keep);
            if (k == 3)                          /* MORPH 0 with B zeroed */
                memset(grain_buf[1], 0, sizeof grain_buf[1]);
            if (k == 4)                          /* MORPH 127 with A zeroed */
                memset(grain_buf[0], 0, sizeof grain_buf[0]);
            drv_on(t, 0, 60);
            drv_on(t, 1, 67);
            drv_on(t, 2, 72);
            for (n = 0; n < FS / 2 / CTL; n++) {
                drv_tick(t, out, 64 << 8);
                memcpy(&o[k][n * CTL], out, sizeof out);
            }
        }
        memcpy(grain_buf, keep, sizeof keep);
        for (i = 0; i < FS / 2 / CTL * CTL; i++) {
            int64_t mix = o[0][i] + (((int64_t)o[1][i] - o[0][i]) * 16513 >> 15), e = llabs(mix - o[2][i]);
            err = e > err ? e : err;
            pk = llabs((int64_t)o[2][i]) > pk ? llabs((int64_t)o[2][i]) : pk;
            d01 += llabs((int64_t)o[1][i] - o[0][i]);
        }
        CHECK(!memcmp(o[0], o[3], sizeof o[0]), "MORPH 0: A alone (B's buffer does not reach the output)");
        CHECK(!memcmp(o[1], o[4], sizeof o[1]), "MORPH 127: B alone");
        CHECK(d01 > 0 && pk > 1000 && err * 200 < pk, "MORPH 64: the mix of A's and B's outputs (off by %lld of a peak %lld)",
              (long long)err, (long long)pk);
        /* SHP: MORPH 0 and SHP full up (+63) is MORPH 63 */
        for (k = 0; k < 2; k++) {
            drv_reset();
            grain_blob_preset(t, 0);
            grain_patch[0][GQ_MORPH] = (int8_t)(k ? 63 : 0);
            grain_macros_out(t);
            settle(400);
            memcpy(grain_buf, keep, sizeof keep);
            drv_on(t, 0, 60);
            for (n = 0; n < FS / 4 / CTL; n++) {
                drv_tick(t, out, k ? 64 << 8 : 127 << 8);
                memcpy(&o[k][n * CTL], out, sizeof out);
            }
        }
        CHECK(!memcmp(o[0], o[1], FS / 4 / CTL * CTL * sizeof o[0][0]), "SHP +63 on MORPH 0 = MORPH 63");
        /* the matrix: LFO 1 -> MORPH over A = +16000, B = -16000 (DC), one grain at a time, the filter open: the
         * output's sign follows the LFO through MORPH 64, and the mix ramps a sample (a block boundary jumps no more
         * than a sample inside a block) */
        drv_reset();
        grain_blob_preset(t, 0);
        grain_patch[0][GQ_DENS] = 0;
        grain_patch[0][GQ_SIZE] = 127;
        grain_patch[0][GQ_SPRD] = 0;
        grain_patch[0][GQ_RAND] = 0;
        grain_patch[0][GQ_MORPH] = 64;
        grain_patch[0][GQ_SCAN] = 0;
        grain_patch[0][GQ_CUT] = 127;
        grain_patch[0][GQ_RES] = 0;
        grain_patch[0][GQ_KTRK] = 0;
        grain_patch[0][GQ_LFO(0, GL_RATE)] = 110;
        grain_patch[0][GQ_LFO(0, GL_DEPTH)] = 127;
        grain_patch[0][GQ_MOD(0, GM_SRC)] = GSR_LFO1;
        grain_patch[0][GQ_MOD(0, GM_DST)] = GD_MORPH;
        grain_patch[0][GQ_MOD(0, GM_AMT)] = 63;
        for (i = 0; i < GP_NG; i++)
            P->g[i].owner = 0;
        grain_macros_out(t);
        settle(400);
        for (i = 0; i < GS_N; i++) {
            grain_buf[0][i] = 16000;
            grain_buf[1][i] = -16000;
        }
        drv_on(t, 0, 60);
        {
            int32_t prev = 0, jin = 0, jedge = 0, pos = 0, neg = 0;
            for (n = 0; n < (uint32_t)(0.3 * FS / CTL); n++) {
                drv_tick(t, out, 64 << 8);
                for (i = 0; i < CTL; i++) {
                    int32_t d = out[i] - prev;
                    d = d < 0 ? -d : d;
                    if (n > 4u) {                /* (past the first blocks: the window's opening dominates there) */
                        if (i == 0)
                            jedge = d > jedge ? d : jedge;
                        else
                            jin = d > jin ? d : jin;
                    }
                    pos += out[i] > 2000;
                    neg += out[i] < -2000;
                    prev = out[i];
                }
            }
            CHECK(pos > 1000 && neg > 1000, "LFO 1 -> MORPH swings the mix between A and B (+ %d, - %d)", pos, neg);
            CHECK(jedge <= jin * 2 + 2, "MORPH ramps a sample: a block boundary jumps %d, within a block %d", jedge, jin);
        }
    }
    /* the rate's cap: C7 and PITCH +24 reads two source samples an output sample */
    drv_reset();
    grain_blob_preset(t, 0);
    grain_patch[0][GQ_PITCH] = 24;
    grain_patch[0][GQ_RAND] = 0;
    grain_macros_out(t);
    settle(200);
    drv_on(t, 0, 96);
    drv_tick(t, out, 64 << 8);
    CHECK(grains_on(P) == 1u && P->g[0].step == GP_STEP_MAX, "the cap: step %u", P->g[0].step);
    drv_reset();
    grain_blob_preset(t, 0);
    grain_patch[0][GQ_RAND] = 0;
    grain_macros_out(t);
    settle(200);
    drv_on(t, 0, 60);
    drv_tick(t, out, 64 << 8);
    CHECK(P->g[0].step == (uint32_t)(1 << 15), "C4 at the root reads half a source sample an output sample: %u", P->g[0].step);
    drv_on(t, 1, 48);
    drv_tick(t, out, 64 << 8);
    CHECK(P->g[1].step == (uint32_t)(1 << 14), "C3: a quarter: %u", P->g[1].step);
    /* the window's continuity: a DC buffer under one grain at a time, the filter open: the output is the window;
     * the jump at a block boundary is no more than within a block */
    drv_reset();
    grain_blob_preset(t, 0);
    grain_patch[0][GQ_DENS] = 0;                 /* one grain a second */
    grain_patch[0][GQ_SIZE] = 64;
    grain_patch[0][GQ_SPRD] = 0;
    grain_patch[0][GQ_RAND] = 0;
    grain_patch[0][GQ_MORPH] = 0;
    grain_patch[0][GQ_SCAN] = 0;
    grain_patch[0][GQ_CUT] = 127;
    grain_patch[0][GQ_RES] = 0;
    grain_patch[0][GQ_FTYPE] = 0;
    grain_patch[0][GQ_DRIVE] = 0;
    grain_patch[0][GQ_KTRK] = 0;
    grain_macros_out(t);
    settle(200);
    for (i = 0; i < GS_N; i++)
        grain_buf[0][i] = 20000;
    drv_on(t, 0, 60);
    {
        int32_t prev = 0, jin = 0, jedge = 0, peak = 0;
        for (n = 0; n < 20u; n++) {
            drv_tick(t, out, 64 << 8);
            for (i = 0; i < CTL; i++) {
                int32_t d = out[i] - prev;
                d = d < 0 ? -d : d;
                if (n || i) {
                    if (i == 0)
                        jedge = d > jedge ? d : jedge;
                    else
                        jin = d > jin ? d : jin;
                }
                prev = out[i];
                peak = out[i] > peak ? out[i] : peak;
            }
        }
        CHECK(peak > 1000, "the window sounds (peak %d)", peak);
        CHECK(jedge <= jin * 2 + 2, "a block boundary jumps %d, within a block %d", jedge, jin);
    }
    /* every preset: 1.4 s of a 6-note chord at LEVEL 92: no int32 wrap, under 0.9 FS, not silent, then the voices off
     * and the grains gone */
    for (k = 0; k < GRAIN_NPRESETS; k++) {
        int64_t mx = 0;
        int32_t peak = 0;
        drv_reset();
        grain_blob_preset(t, k);
        settle(200);
        for (i = 0; i < 6; i++)
            drv_on(t, i, CH[i]);
        for (n = 0; n < (uint32_t)(1.4 * FS / CTL); n++) {
            drv_tick(t, out, 64 << 8);
            for (i = 0; i < CTL; i++) {
                int64_t a = llabs((int64_t)out[i]);
                int32_t x = ((out[i] >> 2) * LEVEL_Q12[92]) >> 10;
                x = x < 0 ? -x : x;
                mx = a > mx ? a : mx;
                peak = x > peak ? x : peak;
            }
        }
        if (getenv("VERBOSE"))
            printf("  %-11s peak %5.1f %% FS (part at LEVEL 92)\n", GRAIN_PRESETS[k].name, peak * 100.0 / 32768);
        CHECK(mx < (1 << 30), "%s: |sum| %lld near the int32 wrap", GRAIN_PRESETS[k].name, (long long)mx);
        CHECK(peak < 0.9 * 32768, "%s: peak %d >= 0.9 FS", GRAIN_PRESETS[k].name, peak);
        CHECK(peak > 300, "%s: silent (peak %d)", GRAIN_PRESETS[k].name, peak);
        for (i = 0; i < 6; i++)
            t->v[i].active = 0;
        drv_tick(t, out, 64 << 8);
        CHECK(!grains_on(P), "%s: %u grains left after the voices", GRAIN_PRESETS[k].name, grains_on(P));
    }
}

/* ---- 7. the scheduler (the unit's path: the tick and the poll in turn), the render in place */
static void whole(int16_t *buf, const uint8_t *kn)            /* the generator's whole render of four knobs */
{
    gs_t g;
    gs_begin(&g, kn[0], kn[1], kn[2], kn[3]);
    while (gs_step(&g, buf, GS_N) < GS_N)
        ;
    gs_finish(buf);
}
static uint32_t polls_to_land(uint32_t max)                  /* polls (a tick between) until a render lands */
{
    uint32_t r0 = grs.renders, n;
    int32_t out[CTL];
    for (n = 0; n < max && grs.renders == r0; n++) {
        grain_poll();
        drv_tick(&trk[0], out, 64 << 8);
    }
    return n;
}
static void t_scheduler(void)
{
    track_t *t = &trk[0];
    gp_part_t *P = grain_part(0);
    int32_t out[CTL];
    uint32_t i, n, polls, before, ty, nmin, quiet, declicks, spliced;
    static int16_t ref[GS_N], old[2][GS_N];
    drv_reset();
    grain_blob_preset(t, 0);
    settle(200);
    before = grs.renders;
    /* grains sounding on the mix; A's TONE moves: a request; the poll measures (nothing written), then writes over
     * the old material in slices while the grains play on (never fewer than one, never a silent block); the head's
     * prefix is the new material, the rest the old; the finished buffer is the generator's whole render, byte for
     * byte; B untouched */
    grain_patch[0][GQ_MORPH] = 64;
    grain_patch[0][GQ_DENS] = 127;
    grain_macros_out(t);
    drv_on(t, 0, 60);
    drv_on(t, 1, 67);
    for (n = 0; n < FS / 4 / CTL; n++)
        drv_tick(t, out, 64 << 8);
    CHECK(grains_on(P) >= 2u, "grains sounding (%u)", grains_on(P));
    memcpy(old, grain_buf, sizeof old);
    grain_patch[0][GQ_TONE1] = 100;
    grain_macros_out(t);
    drv_tick(t, out, 64 << 8);                   /* the tick sees the knob */
    CHECK(grs.req == 1u && !grs.stage, "a request for A");
    {
        static const uint8_t KN[4] = {GT_DRONE, 100, 64, 0};
        whole(ref, KN);
    }
    nmin = GP_NG;
    quiet = declicks = spliced = 0;
    for (polls = 0; polls < 1000u; polls++) {
        int64_t e = 0;
        grain_poll();
        if (grs.stage == 1u)
            CHECK(!memcmp(grain_buf[0], old[0], sizeof old[0]), "the measuring pass writes nothing");
        if (grs.stage == 2u) {
            uint32_t h = grs.wpos, keep = h >= GRAIN_RAMP ? h - GRAIN_RAMP : 0u;
            CHECK(!memcmp(grain_buf[0], ref, keep * 2u), "the new material below the head (%u)", h);
            CHECK(!memcmp(grain_buf[0] + h, old[0] + h, (GS_N - h) * 2u), "the old above the head (%u)", h);
        }
        drv_tick(t, out, 64 << 8);
        for (i = 0; i < CTL; i++)
            e += (int64_t)out[i] * out[i];
        quiet += !e;
        nmin = grains_on(P) < nmin ? grains_on(P) : nmin;
        spliced += grs.thi > grs.tlo;
        for (i = 0; i < GP_NG; i++)
            declicks += P->g[i].owner && P->g[i].dcn == GP_DECLICK - 1u;   /* (a step taken this block) */
        if (!grs.stage && !grs.req)
            break;
    }
    CHECK(grs.renders == before + 1u, "rendered (%u polls)", polls);
    CHECK(polls + 1u == 2u * GS_N / GRAIN_SLICE_N[GT_DRONE], "%u polls for two passes of %u slices", polls + 1u,
          GS_N / GRAIN_SLICE_N[GT_DRONE]);
    CHECK(!memcmp(ref, grain_buf[0], sizeof ref), "the buffer rendered in place is the whole render's");
    CHECK(!memcmp(old[1], grain_buf[1], sizeof old[1]), "B untouched");
    CHECK(nmin >= 1u && !quiet, "the grains never stopped (at least %u, %u silent blocks)", nmin, quiet);
    CHECK(spliced > 0u && declicks > 0u, "the writes reached the tick (%u blocks) and grains under them stepped smoothly (%u)",
          spliced, declicks);
    CHECK(grs.cur[0][1] == 100 && grs.root16[0] == GS_ROOT16, "the knobs held, the root");
    /* every family in place equals its whole render, PLUCK's string across the slices' history among them; the root
     * a PLUCK reports */
    for (ty = 0; ty < GS_NTYPE; ty++) {
        static const uint8_t TK[3] = {104, 20, 11};
        uint8_t kn[4] = {(uint8_t)ty, TK[0], TK[1], TK[2]};
        grain_patch[0][GQ_TYPE1] = (int8_t)ty;
        grain_patch[0][GQ_TONE1] = (int8_t)TK[0];
        grain_patch[0][GQ_MOTION1] = (int8_t)TK[1];
        grain_patch[0][GQ_SEED1] = (int8_t)TK[2];
        grain_macros_out(t);
        drv_tick(t, out, 64 << 8);
        n = polls_to_land(4000);
        whole(ref, kn);
        CHECK(!memcmp(ref, grain_buf[0], sizeof ref), "%s: in place = whole", GS_TYPE_NAMES[ty]);
        CHECK(n == 2u * GS_N / GRAIN_SLICE_N[ty], "%s: %u polls for two passes of %u", GS_TYPE_NAMES[ty], n, GS_N / GRAIN_SLICE_N[ty]);
        CHECK(grs.root16[0] == (ty == GT_PLUCK ? GS_PLUCK_ROOT16 : GS_ROOT16), "%s: root %d", GS_TYPE_NAMES[ty], grs.root16[0]);
    }
    /* latest wins, at the end of a render: a change during one lets it finish on its knobs, then the latest renders */
    grain_patch[0][GQ_TYPE1] = GT_DRONE;
    grain_patch[0][GQ_TONE1] = 100;
    grain_patch[0][GQ_MOTION1] = 64;
    grain_patch[0][GQ_SEED1] = 0;
    grain_macros_out(t);
    settle(4000);
    before = grs.renders;
    grain_patch[0][GQ_SEED1] = 5;
    grain_macros_out(t);
    drv_tick(t, out, 64 << 8);
    for (n = 0; n < 3u; n++) {
        grain_poll();
        drv_tick(t, out, 64 << 8);
    }
    CHECK(grs.stage == 1u, "mid-render");
    grain_patch[0][GQ_SEED1] = 9;
    grain_macros_out(t);
    polls_to_land(4000);
    {
        static const uint8_t K5[4] = {GT_DRONE, 100, 64, 5}, K9[4] = {GT_DRONE, 100, 64, 9};
        whole(ref, K5);
        CHECK(grs.renders == before + 1u && !memcmp(ref, grain_buf[0], sizeof ref), "the render under way finished on its knobs");
        CHECK((grs.req & 1u) && grs.want[0][3] == 9, "the latest next");
        settle(4000);
        whole(ref, K9);
        CHECK(grs.renders == before + 2u && !memcmp(ref, grain_buf[0], sizeof ref), "then the latest's (%u renders)",
              grs.renders - before);
    }
    /* ten changes in a row, a poll between each: two renders (the one they found under way, the last) */
    before = grs.renders;
    for (i = 0; i < 10u; i++) {
        grain_patch[0][GQ_TONE2] = (int8_t)(10 + 10 * i);
        grain_macros_out(t);
        drv_tick(t, out, 64 << 8);
        grain_poll();
    }
    settle(4000);
    CHECK(grs.renders - before == 2u, "ten changes: %u renders", grs.renders - before);
    CHECK(grs.cur[1][1] == 100, "the last one held");
    /* a knob turned on and on: the renders land as fast as the polls go (each one, on the knobs it started with), the
     * sound moves all the time; B changed once meanwhile is rendered next, not after the turning stops */
    before = grs.renders;
    {
        uint32_t na = 0, nb = 0, a_after_b = 0, polls_all = 0;
        for (i = 0; i < 600u; i++) {
            uint32_t r0 = grs.renders;
            grain_patch[0][GQ_TONE1] = (int8_t)(i % 128u);
            if (i == 100u)
                grain_patch[0][GQ_TONE2] = 33;
            grain_macros_out(t);
            for (n = 0; n < 4u; n++, polls_all++) {
                grain_poll();
                drv_tick(t, out, 64 << 8);
            }
            if (grs.renders != r0) {
                if (grs.last == 1u)
                    nb++;
                else {
                    na++;
                    a_after_b += i > 100u && !nb;
                }
            }
        }
        CHECK(na >= polls_all / (2u * GS_N / GRAIN_SLICE_N[GT_DRONE]) - 3u, "turning: %u of A's renders landed in %u polls", na,
              polls_all);
        CHECK(nb == 1u && a_after_b <= 1u, "B's one change rendered after at most one more of A's (%u B, %u A first)", nb, a_after_b);
    }
    settle(4000);
    /* SCAN: forward moves POS up the buffer, reverse down, 0 holds; the matrix moves grain knobs, LFO 1 synced ticks */
    {
        int32_t s0, s1, s2;
        gp_part_t *Q = grain_part(0);
        drv_reset();
        grain_blob_preset(t, 0);
        settle(200);
        grain_patch[0][GQ_SCAN] = 63;
        drv_on(t, 0, 60);
        drv_tick(t, out, 64 << 8);
        s0 = (int32_t)(Q->scan >> 17);
        for (n = 0; n < 20u; n++)
            drv_tick(t, out, 64 << 8);
        s1 = (int32_t)(Q->scan >> 17);
        CHECK(s1 > s0, "SCAN 63 moves forward (%d -> %d)", s0, s1);
        grain_patch[0][GQ_SCAN] = -64;
        for (n = 0; n < 10u; n++)
            drv_tick(t, out, 64 << 8);
        s2 = (int32_t)(Q->scan >> 17);
        CHECK(s2 < s1, "SCAN -64 moves back (%d -> %d)", s1, s2);
        grain_patch[0][GQ_SCAN] = 0;
        for (n = 0; n < 10u; n++)
            drv_tick(t, out, 64 << 8);
        CHECK((int32_t)(Q->scan >> 17) == s2, "SCAN 0 holds");
        CHECK(grain_scan_inc(1) > 0 && grain_scan_inc(64) > grain_scan_inc(1) * 100 && grain_scan_inc(-1) == -grain_scan_inc(1),
              "SCAN's rate: 1 slow (%d a block), 64 fast (%d), the sign the direction", grain_scan_inc(1), grain_scan_inc(64));
        /* a round at SCAN 1 takes about 110 s, at 64 about half a second */
        CHECK(4294967296.0 / grain_scan_inc(1) * CTL / FS > 90 && 4294967296.0 / grain_scan_inc(1) * CTL / FS < 130,
              "a round at 1: %.0f s", 4294967296.0 / grain_scan_inc(1) * CTL / FS);
        CHECK(4294967296.0 / grain_scan_inc(64) * CTL / FS < 0.6, "a round at 64: %.2f s", 4294967296.0 / grain_scan_inc(64) * CTL / FS);
        /* the matrix: LFO 1 -> SIZE at a full amount: the grains' lengths spread beyond the knob's; ENV 1 -> CUT */
        drv_reset();
        grain_blob_preset(t, 0);
        grain_patch[0][GQ_MOD(0, GM_SRC)] = GSR_LFO1;
        grain_patch[0][GQ_MOD(0, GM_DST)] = GD_SIZE;
        grain_patch[0][GQ_MOD(0, GM_AMT)] = 63;
        grain_patch[0][GQ_LFO(0, GL_RATE)] = 100;
        grain_patch[0][GQ_SIZE] = 64;
        grain_patch[0][GQ_DENS] = 127;
        settle(200);
        drv_on(t, 0, 60);
        {
            uint32_t lmin = 0xFFFFFFFFu, lmax = 0, g2;
            for (n = 0; n < 2u * FS / CTL; n++) {
                drv_tick(t, out, 64 << 8);
                for (g2 = 0; g2 < GP_NG; g2++)
                    if (Q->g[g2].owner && Q->g[g2].wph == Q->g[g2].winc * CTL) {   /* a grain in its first block */
                        uint32_t len = 0xFFFFFFFFu / Q->g[g2].winc;
                        lmin = len < lmin ? len : lmin;
                        lmax = len > lmax ? len : lmax;
                    }
            }
            CHECK(lmin < 2000u && lmax > 8000u, "LFO 1 -> SIZE: grain lengths %u .. %u samples", lmin, lmax);
        }
        drv_reset();
        grain_blob_preset(t, 0);
        grain_patch[0][GQ_LFO(0, GL_SYNC)] = 1;
        grain_patch[0][GQ_LFO(0, GL_RATE)] = 19;  /* 1/4 at 120 BPM: a cycle in 0.5 s */
        settle(200);
        {
            uint32_t ph0 = Q->lph[0];
            for (n = 0; n < (uint32_t)(0.5 * FS / CTL); n++)
                drv_tick(t, out, 64 << 8);
            CHECK((int32_t)(Q->lph[0] - ph0) > -(1 << 26) && (int32_t)(Q->lph[0] - ph0) < (1 << 26),
                  "LFO 1 synced to 1/4 at 120 BPM: a whole cycle in 0.5 s (off by %d / 2^32)", (int32_t)(Q->lph[0] - ph0));
        }
        /* ENV 2 rises and falls with the gate */
        drv_reset();
        grain_blob_preset(t, 4);                  /* STRUM: ENV 2 ATK 0 DEC 50 SUS 0 */
        settle(200);
        drv_on(t, 0, 60);
        for (n = 0; n < 8u; n++)
            drv_tick(t, out, 64 << 8);
        CHECK(t->v[0].s[5] == 3 && t->v[0].s[4] <= (1 << 24) && t->v[0].s[4] > (1 << 23), "ENV 2: the attack over (ATK 0: %d)", t->v[0].s[4]);
        for (n = 0; n < FS / CTL; n++)
            drv_tick(t, out, 64 << 8);
        CHECK(t->v[0].s[4] < (1 << 20), "ENV 2 decays to SUS 0 (%d)", t->v[0].s[4]);
    }
    /* a second part on GRAIN shares: its knobs render nothing */
    trk[1].eng_req = trk[1].engine = ENGI_GRAIN;
    grain_blob_preset(&trk[1], 5);
    before = grs.renders;
    settle(200);
    CHECK(grs.renders == before && grs.owner == 1u, "part 1's sources do not render (owner %u)", grs.owner);
    trk[0].eng_req = trk[0].engine = ENGI_FM6;   /* the chord part leaves GRAIN: the bass part owns */
    drv_tick(t, out, 64 << 8);
    CHECK(grs.owner == 2u && grs.req, "the bass part owns and asks");
    settle(400);
    CHECK(grs.renders == before + 2u && grs.cur[0][0] == GRAIN_PP[5][GQ_TYPE1], "its sources rendered");
    /* a part on GRAIN with the same knobs as the buffers: no render */
    before = grs.renders;
    for (n = 0; n < 10u; n++)
        drv_tick(t, out, 64 << 8);
    CHECK(grs.renders == before && !grs.req, "nothing to render");
}

int main(void)
{
    t_grid();
    t_knobs();
    t_golden();
    t_time();
    t_patch();
    t_engine();
    t_scheduler();
    printf("cr_grain_test: %d checks, %d failed%s\n", checks, fails, fails ? "" : " ok");
    return fails ? 1 : 0;
}
