/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* GRAIN's listening renders for MORPH as a mix and the render in place (docs/GRAIN.md, HISTORY 3y167 .. 3y169): the
 * engine (firmware/src/eng_grain.c, or another copy of it by GRAIN_ENGINE: the one before, for the comparison) under
 * a small voice driver, its render scheduler (GRAIN_ASYNC 1: the unit's path, grain_tick then grain_poll) under a
 * model of the unit's main loop, dry, mono, 44.1 kHz WAVs.
 *
 *   cc -std=c99 -O2 -w -Ibuild/gen -o build/host/grain_listen tools/grain_listen.c -lm
 *   cc -std=c99 -O2 -w -Ibuild/gen -Ifirmware/src -DGRAIN_ENGINE='"OLD/eng_grain.c"' -DGRAIN_CORE='"OLD/core.h"' \
 *      -o build/host/grain_listen_old tools/grain_listen.c -lm       (OLD: git show's of the engine, grain_src.c,
 *                                                                   va_common.c and core.h before the change)
 *   build/host/grain_listen build/grain-mix new        (writes build/grain-mix/NAME-new.wav, prints the measurements)
 *
 * The main loop's model: each audio block (CTL samples, 0.73 ms) gives the polls SHARE of its time on the unit, at
 * 205 instructions a microsecond over the unit's factor 1.5 (docs/GRAIN.md "Budgets"); a poll costs its slice's
 * samples times the family's instructions a sample (docs/GRAIN.md's table) plus the finish's (the old engine's
 * gs_finish once, 24 a sample; the new engine's measuring and writing passes, GL_MEASURE / GL_APPLY a sample). The
 * polls run while the block's budget lasts (one may overdraw it: the next block's pays). The platform's ADSR is a
 * plain linear one (the preset's A D S R as times: listening only).
 *
 * The click measurement (the new engine): every block, each grain sounding whose read point the write head passed
 * (the head overtook it between two blocks, or the grain read across the head inside one) is a splice: the sample it
 * reads next is the new material's where it had the old's; the step is |new - old| at that place, mixed by MORPH, times
 * the grain's level then (window x 1 / sqrt(overlap)). Printed against the material's own steps (|x[i+1] - x[i]| of
 * the finished buffer) at the same level: a splice no larger than the steps the material takes anyway is a
 * discontinuity the ear does not single out. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define __attribute__(x)
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#include "felucca_tables.h"
#ifndef GRAIN_CORE
#define GRAIN_CORE "../firmware/src/core.h"
#endif
#include GRAIN_CORE
#include "../firmware/src/dsp.c"
#define GRAIN_ASYNC 1
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#ifndef GRAIN_ENGINE
#define GRAIN_ENGINE "../firmware/src/eng_grain.c"
#endif
#include GRAIN_ENGINE
#ifdef GRAIN_SLICE_MAX
#define NEW_ENGINE 1
#else
#define NEW_ENGINE 0
#endif

#define SHARE 0.5                /* the main loop's share of the unit's time for the polls */
#define IPU (205.0 / 1.5)        /* instructions a microsecond, the unit's factor in */
#define GL_MEASURE 11            /* the measuring pass's high-pass and peak, a sample (build/choralroot.dis grain_poll) */
#define GL_APPLY 22              /* the writing pass's high-pass, gain and store (20), the copy into the buffer (2) */
static const int FAM[GS_NTYPE] = {58, 327, 100, 129, 103, 240, 296, 187};

/* ---- the voice driver (tests/cr_grain_test.c's, with a linear ADSR) */
static int32_t envl[NVOICE], envd[NVOICE];
static uint8_t envst[NVOICE];   /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
static int32_t adsr[4];          /* the preset's, in blocks (S: Q15) */
static int32_t blocks_of(int v) { return 1 + (int32_t)(v * v * 3.0 / CTL); }   /* v^2 x 3 samples / CTL: 0 .. 1.1 s */
static void drv_reset(uint32_t preset)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(gp_parts, 0, sizeof gp_parts);
    song.g[G_BPM] = 120;
    trk[0].p[P_VOICE] = V_POLY;
    trk[1].p[P_VOICE] = V_MONO;
    trk[0].eng_req = trk[0].engine = ENGI_GRAIN;
    trk[1].eng_req = trk[1].engine = 12u;
    grs.req = 0;
#if !NEW_ENGINE
    grs.lock = grs.ack = 0;
#endif
    grs.stage = 0;
    memset(grs.cur, 0xFF, sizeof grs.cur);
    grain_blob_preset(&trk[0], preset);
    adsr[0] = blocks_of(GRAIN_PRESETS[preset].env[0]);
    adsr[1] = blocks_of(GRAIN_PRESETS[preset].env[1]);
    adsr[2] = GRAIN_PRESETS[preset].env[2] * 258;
    adsr[3] = blocks_of(GRAIN_PRESETS[preset].env[3]);
    for (i = 0; i < NVOICE; i++)
        envst[i] = 0, envl[i] = 0;
}
static void drv_on(uint32_t i, uint32_t note)
{
    voice_t *v = &trk[0].v[i];
    int sounding = v->active;
    v->note = (uint8_t)note;
    v->vel = v->mvel = 100;
    v->gate = v->active = 1;
    v->stage = 1;
    v->age = i + 1u;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    if (!sounding)
        v->env = v->env_out = 0;
    grain_note_on(&trk[0], v);
    v->env = 1 << 24;
    envst[i] = 1;
    trk[0].m_vi = (uint8_t)i;
}
static void drv_off(uint32_t i)
{
    trk[0].v[i].gate = 0;
    envst[i] = 4;
    envd[i] = envl[i] / adsr[3] + 1;
}
static int32_t env_step(uint32_t i)
{
    switch (envst[i]) {
    case 1: envl[i] += 32767 / adsr[0] + 1; if (envl[i] >= 32767) { envl[i] = 32767; envst[i] = 2; } break;
    case 2: envl[i] -= (32767 - adsr[2]) / adsr[1] + 1; if (envl[i] <= adsr[2]) { envl[i] = adsr[2]; envst[i] = 3; } break;
    case 4: envl[i] -= envd[i]; if (envl[i] <= 0) { envl[i] = 0; envst[i] = 0; trk[0].v[i].active = 0; } break;
    default: break;
    }
    return envl[i];
}
static void (*probe_hook)(void);                /* the click measurement, between the tick and the voices */
static void drv_block(int32_t *out)
{
    uint32_t i;
    memset(out, 0, CTL * sizeof *out);
    grain_tick();
    grain_block(&trk[0]);
    if (probe_hook)
        probe_hook();
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &trk[0].v[i];
        vmod_t m;
        if (!v->active)
            continue;
        memset(&m, 0, sizeof m);
        m.amp0 = envl[i];
        m.amp1 = env_step(i);
        m.envq15 = m.amp1;
        m.pitch16 = v->pitch_cur;
        m.inc = pitch_inc((uint32_t)m.pitch16);
        m.shape = 64 << 8;
        if (v->active)
            grain_render(&trk[0], v, out, CTL, &m);
    }
}

/* ---- the main loop's model */
static double budget;
static uint32_t polls_run;
static void main_loop(void)
{
    budget += CTL / 44100.0 * 1e6 * SHARE * IPU;
    while (budget > 0) {
        uint32_t st = grs.stage, p0 = grs.g.pos, r0 = grs.renders, q0 = grs.req, ty, d = 0;
        double c = 200;
        grain_poll();
        ty = grs.g.type;
        if (st == grs.stage && p0 == grs.g.pos && r0 == grs.renders && q0 == grs.req)
            break;                               /* nothing to do (or the old engine waiting for its fade) */
        polls_run++;
#if NEW_ENGINE
        if (st == grs.stage)
            d = grs.g.pos - p0;
        else if (st == 0u)
            d = grs.g.pos;
        else
            d = GS_N - p0;
        c += (double)d * (FAM[ty] + (st == 2u || (st == 1u && grs.stage == 0u) ? GL_APPLY : GL_MEASURE) + 2);
#else
        if (grs.stage == 2u && st == 2u && grs.g.pos >= p0)
            d = grs.g.pos - p0;
        else if (grs.stage == 2u)
            d = grs.g.pos;
        else if (st == 2u)
            d = GS_N - p0;
        c += (double)d * FAM[ty];
        if (grs.renders != r0)
            c += GS_N * 24.0;
#endif
        budget -= c;
    }
    if (budget > 0)
        budget = 0;                              /* (idle time is not saved up) */
}

/* ---- the WAV */
static int16_t *wav;
static uint32_t wn, wcap;
static void wav_put(const int32_t *out)
{
    uint32_t i;
    if (wn + CTL > wcap) {
        wcap = wcap ? wcap * 2 : 1u << 20;
        wav = realloc(wav, wcap * sizeof *wav);
    }
    for (i = 0; i < CTL; i++) {
        int32_t x = ((out[i] >> 2) * LEVEL_Q12[92]) >> 10;
        wav[wn++] = (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x);
    }
}
static void wav_write(const char *dir, const char *name, const char *tag)
{
    char path[512];
    FILE *f;
    uint32_t bytes = wn * 2u, v;
    uint8_t h[44];
    snprintf(path, sizeof path, "%s/%s-%s.wav", dir, name, tag);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    memcpy(h, "RIFF", 4);
    v = 36 + bytes; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    v = 44100; memcpy(h + 24, &v, 4);
    v = 88200; memcpy(h + 28, &v, 4);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    fwrite(wav, 2, wn, f);
    fclose(f);
    printf("  %s: %.1f s\n", path, wn / 44100.0);
    wn = 0;
}

/* ---- the measurements: silence (blocks of a held chord under 1 % of its level) and the splices */
static uint32_t silent_blocks, loud_blocks;
static double level_ref;
static void level_note(const int32_t *out)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < CTL; i++)
        e += (double)out[i] * out[i];
    e = sqrt(e / CTL);
    if (level_ref > 0 && e < level_ref * 0.01)
        silent_blocks++;
    else
        loud_blocks++;
}
#if NEW_ENGINE
static int16_t shadow[2][GS_N];  /* the buffers as the grains last saw them */
static uint32_t nsplice, nover, nacross;
static double sp_sum, sp_max, sp_hist[64], nat_sum, nat_max;
static uint32_t nat_n;
static void splice_note(double step, double lev)
{
    double x = step * lev / 32768.0 / GS_PEAK * 100.0;   /* % of the grain's own full level */
    int b = (int)(x * 2);
    nsplice++;
    sp_sum += x;
    sp_max = x > sp_max ? x : sp_max;
    sp_hist[b < 63 ? b : 63]++;
}
static double sp_pct(double q)                   /* the q-th quantile of the splices, % */
{
    double n = 0;
    int b;
    for (b = 0; b < 64; b++) {
        n += sp_hist[b];
        if (n >= q * nsplice)
            return (b + 1) / 2.0;
    }
    return 32;
}
/* after the polls, before the block: each grain whose next sample changed under it (the head overtook it) */
static void splice_over(void)
{
    gp_part_t *P = grain_part(0);
    uint32_t k;
    for (k = 0; k < GP_NG; k++) {
        const gp_grain_t *g = &P->g[k];
        uint32_t x = g->rev ? (g->pos ? g->pos - 1u : 0u) : g->pos;
        int32_t mq, o, nw;
        if (!g->owner || x >= GS_N || (shadow[0][x] == grain_buf[0][x] && shadow[1][x] == grain_buf[1][x]))
            continue;
        mq = trk[0].v[(g->owner - 1u) % NVOICE].s[1];
        o = shadow[0][x] + (((shadow[1][x] - shadow[0][x]) * mq) >> 15);
        nw = grain_buf[0][x] + (((grain_buf[1][x] - grain_buf[0][x]) * mq) >> 15);
        splice_note(fabs((double)(nw - o)), (double)mulq15(gp_win(g->wph), g->gain));
        nover++;
    }
}
/* after the block: each grain that read across the write head inside it (new below the head, old from it on) */
static void splice_across(const uint32_t *before)
{
    gp_part_t *P = grain_part(0);
    uint32_t k, h = grs.wpos, b = grs.which;
    if (grs.stage != 2u || !h || h >= GS_N)
        return;
    for (k = 0; k < GP_NG; k++) {
        const gp_grain_t *g = &P->g[k];
        uint32_t lo = before[k] < g->pos ? before[k] : g->pos, hi = before[k] < g->pos ? g->pos : before[k];
        int32_t mq, o, nw;
        if (!g->owner || !(lo < h && h < hi))
            continue;
        mq = trk[0].v[(g->owner - 1u) % NVOICE].s[1];
        (void)b;
        o = grain_buf[0][h] + (((grain_buf[1][h] - grain_buf[0][h]) * mq) >> 15);
        nw = grain_buf[0][h - 1] + (((grain_buf[1][h - 1] - grain_buf[0][h - 1]) * mq) >> 15);
        splice_note(fabs((double)(nw - o)), (double)mulq15(gp_win(g->wph), g->gain));
        nacross++;
    }
}
/* what each grain plays: a copy of it run through gp_run (MORPH held: no ramp), its peak second difference over the
 * block and across the boundary from the last, in % of the grain's full level; in the histogram of the blocks with a
 * splice (its held samples rewritten, or the write head crossed while it read) or of the clean ones */
static int32_t pv1[GP_NG], pv2[GP_NG];
static uint32_t pvok[GP_NG], pvwph[GP_NG];
static double d2h[2][400], d2n[2];
static void probe(void)
{
    gp_part_t *P = grain_part(0);
    uint32_t k, i;
    for (k = 0; k < GP_NG; k++) {
        const gp_grain_t *g = &P->g[k];
        gp_grain_t c;
        int32_t tmp[CTL], mq, x1, x2, pk = 0, full;
        uint32_t sp, h, lo, hi;
        double d;
        int b;
        if (!g->owner) {
            pvok[k] = 0;
            continue;
        }
        if (g->wph < pvwph[k])
            pvok[k] = 0;
        mq = trk[0].v[(g->owner - 1u) % NVOICE].s[1];
        c = *g;
        memset(tmp, 0, sizeof tmp);
        gp_run(&c, tmp, CTL, mq, 0);
        h = g->rev ? g->pos : g->pos - 2u;
        sp = grs.thi > grs.tlo && h + 1u >= grs.tlo && h < grs.thi;
        lo = g->pos < c.pos ? g->pos : c.pos;
        hi = g->pos < c.pos ? c.pos : g->pos;
        sp |= grs.stage == 2u && lo < grs.wpos && grs.wpos < hi;
        x1 = pvok[k] ? pv1[k] : tmp[0];
        x2 = pvok[k] ? pv2[k] : tmp[0];
        for (i = 0; i < CTL; i++) {
            int32_t e = tmp[i] - 2 * x1 + x2;
            e = e < 0 ? -e : e;
            if (i >= 2u || pvok[k])
                pk = e > pk ? e : pk;
            x2 = x1;
            x1 = tmp[i];
        }
        pv1[k] = x1;
        pv2[k] = x2;
        pvok[k] = 1;
        pvwph[k] = g->wph;
        full = (int32_t)(((int64_t)g->gain * GS_PEAK) >> 15);
        d = pk * 100.0 / (full ? full : 1);
        b = (int)(d * 4);
        d2h[sp][b < 399 ? b : 399]++;
        d2n[sp]++;
    }
}
static double d2q(int sp, double q)
{
    double n = 0;
    int b;
    for (b = 0; b < 400; b++) {
        n += d2h[sp][b];
        if (n >= q * d2n[sp])
            return (b + 1) / 4.0;
    }
    return 100;
}
static double d2over(int sp, double x)            /* the share above x, % */
{
    double n = 0;
    int b;
    for (b = (int)(x * 4); b < 400; b++)
        n += d2h[sp][b];
    return d2n[sp] ? n * 100.0 / d2n[sp] : 0;
}
static double d2max(int sp)
{
    int b;
    for (b = 399; b >= 0; b--)
        if (d2h[sp][b])
            return (b + 1) / 4.0;
    return 0;
}
static void natural_note(void)                   /* the material's own steps at the level of a grain (the finished A) */
{
    gp_part_t *P = grain_part(0);
    uint32_t i, k;
    for (k = 0; k < GP_NG; k++) {
        const gp_grain_t *g = &P->g[k];
        double lev;
        if (!g->owner)
            continue;
        lev = mulq15(gp_win(g->wph), g->gain);
        for (i = 0; i < 64; i++) {
            uint32_t y = (g->pos + i * 97u) % (GS_N - 1u);
            double x = fabs((double)(grain_buf[0][y + 1] - grain_buf[0][y])) * lev / 32768.0 / GS_PEAK * 100.0;
            nat_sum += x;
            nat_max = x > nat_max ? x : nat_max;
            nat_n++;
        }
    }
}
#endif

/* one block of everything: the main loop's polls, then the audio block (the ISR runs after the main loop's share:
 * the order inside a block does not matter to the model) */
static void step(int32_t *out, int measure)
{
#if NEW_ENGINE
    uint32_t gb[GP_NG], k;
    gp_part_t *P = grain_part(0);
#endif
    main_loop();
#if NEW_ENGINE
    if (measure) {
        splice_over();
        memcpy(shadow, grain_buf, sizeof shadow);
        natural_note();
        for (k = 0; k < GP_NG; k++)
            gb[k] = P->g[k].pos;
    }
#endif
    drv_block(out);
#if NEW_ENGINE
    if (measure)
        splice_across(gb);
#endif
    wav_put(out);
}

static const uint8_t CHORD[6] = {48, 55, 60, 64, 67, 72};
static void settle(void)                         /* the tick and the poll in turn until nothing is pending */
{
    uint32_t n;
    for (n = 0; n < 200000u && (grs.req || grs.stage || !n); n++) {
        grain_tick();
        grain_poll();
    }
}
static void chord_on(void)
{
    uint32_t i;
    for (i = 0; i < 6; i++)
        drv_on(i, CHORD[i]);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/grain-mix", *tag = argc > 2 ? argv[2] : (NEW_ENGINE ? "new" : "old");
    int32_t out[CTL];
    uint32_t n, N;
    char cmd[600];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    if (system(cmd))
        return 1;
    printf("grain_listen (%s engine), main loop share %.0f %%:\n", NEW_ENGINE ? "the new" : "the old", SHARE * 100);

    /* 1. MORPH swept 0 -> 127 over 16 s under a held chord (CLOUD PAD: DRONE / BELL), 1 s either side */
    drv_reset(0);
    grain_patch[0][GQ_MORPH] = 0;
    grain_macros_out(&trk[0]);
    settle();
    chord_on();
    N = (uint32_t)(18.0 * 44100 / CTL);
    for (n = 0; n < N; n++) {
        double t = n * (double)CTL / 44100 - 1.0;
        int32_t m = t < 0 ? 0 : t > 16 ? 127 : (int32_t)(t / 16 * 127);
        if (m != grain_patch[0][GQ_MORPH]) {
            grain_patch[0][GQ_MORPH] = (int8_t)m;
            grain_macros_out(&trk[0]);
        }
        step(out, 0);
    }
    wav_write(dir, "morph-sweep", tag);

    /* 2. TONE of A turned 0 -> 127 over 8 s, then SEED 0 -> 60 over 8 s, a detent at a time, under a held chord
     * (CLOUD PAD at MORPH 0: A, a DRONE, alone: the heaviest render) */
    drv_reset(0);
    grain_patch[0][GQ_MORPH] = 0;
    grain_patch[0][GQ_TONE1] = 0;
    if (getenv("GL_TYPE"))                      /* (the measurement on another family: GL_TYPE=2 a BELL) */
        grain_patch[0][GQ_TYPE1] = (int8_t)atoi(getenv("GL_TYPE"));
    grain_macros_out(&trk[0]);
    settle();
    chord_on();
    {
        double e = 0;
        uint32_t c = 0, i;
        for (n = 0; n < (uint32_t)(1.0 * 44100 / CTL); n++) {   /* the chord's level, before any turn */
            drv_block(out);
            for (i = 0; n > 300 && i < CTL; i++, c++)
                e += (double)out[i] * out[i];
            wav_put(out);
        }
        level_ref = sqrt(e / c);
    }
    silent_blocks = loud_blocks = 0;
#if NEW_ENGINE
    probe_hook = probe;
#endif
    {
        uint32_t r0 = grs.renders, polls0 = polls_run;
        N = (uint32_t)(18.0 * 44100 / CTL);
        for (n = 0; n < N; n++) {
            double t = n * (double)CTL / 44100;
            if (t < 8) {
                int32_t v = (int32_t)(t / 8 * 127);
                if (v != grain_patch[0][GQ_TONE1]) {
                    grain_patch[0][GQ_TONE1] = (int8_t)v;
                    grain_macros_out(&trk[0]);
                }
            } else if (t >= 9 && t < 17) {
                int32_t v = (int32_t)((t - 9) / 8 * 60);
                if (v != grain_patch[0][GQ_SEED1]) {
                    grain_patch[0][GQ_SEED1] = (int8_t)v;
                    grain_macros_out(&trk[0]);
                }
            }
            step(out, 1);
            level_note(out);
        }
        printf("  tone-seed: %u renders landed in 18 s of turning (127 TONE and 60 SEED detents), %u polls; "
               "%u of %u blocks under 1 %% of the chord's level (%.2f s silent)\n", grs.renders - r0, polls_run - polls0,
               silent_blocks, silent_blocks + loud_blocks, silent_blocks * (double)CTL / 44100);
#if NEW_ENGINE
        printf("  splices: %u (%u overtaken by the head, %u read across it), mean %.2f %%, median %.1f %%, 99th %.1f %%, max %.1f %% "
               "of the grain's level; the material's own steps: mean %.2f %%, max %.1f %%\n", nsplice, nover, nacross,
               nsplice ? sp_sum / nsplice : 0, sp_pct(0.5), sp_pct(0.99), sp_max, nat_n ? nat_sum / nat_n : 0, nat_max);
#endif
    }
#if NEW_ENGINE
    probe_hook = 0;
    printf("  what the grains play (peak second difference a block, %% of a grain's level): %.0f clean grain-blocks: "
           "99th %.2f, 99.9th %.2f, max %.2f; %.0f with a splice: median %.2f, 99th %.2f, max %.2f, %.2f %% of them above the "
           "clean ones' 99.9th\n", d2n[0], d2q(0, 0.99), d2q(0, 0.999), d2max(0), d2n[1], d2q(1, 0.5), d2q(1, 0.99), d2max(1),
           d2over(1, d2q(0, 0.999)));
#endif
    wav_write(dir, "tone-seed", tag);

    /* 3. STRUM: four strums of the chord, 1.6 s each (1.2 held) */
    drv_reset(4);
    settle();
    for (n = 0; n < 4u; n++) {
        uint32_t i, b;
        chord_on();
        for (b = 0; b < (uint32_t)(1.6 * 44100 / CTL); b++) {
            if (b == (uint32_t)(1.2 * 44100 / CTL))
                for (i = 0; i < 6; i++)
                    drv_off(i);
            step(out, 0);
        }
    }
    for (n = 0; n < (uint32_t)(1.0 * 44100 / CTL); n++)
        step(out, 0);
    wav_write(dir, "strum", tag);

    /* 4. SHIMMER: the chord held 8 s, released */
    drv_reset(1);
    settle();
    chord_on();
    for (n = 0; n < (uint32_t)(10.0 * 44100 / CTL); n++) {
        if (n == (uint32_t)(8.0 * 44100 / CTL)) {
            uint32_t i;
            for (i = 0; i < 6; i++)
                drv_off(i);
        }
        step(out, 0);
    }
    wav_write(dir, "shimmer", tag);
    return 0;
}
