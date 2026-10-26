/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The bass part's job on a REAL second thread (HISTORY 3y45, docs/DUALCORE.md "Two threads on the host"): the host's
 * stub core runs core 1's job inline, so until this test nothing on the host could show the two cores writing one
 * thing. Here fx.c's mix_parts_split hands part 1's render to a second thread (cr_cpu1.h cr_c1_test_thread) that runs
 * it while this one renders part 0, as the unit's two cores do, on the firmware as the emulator builds it (ALL 200 P5
 * programs when built with -Ibuild/host/p5_all first).
 *   build/host/cr_c1_thread_test          every pair of engines on the two parts, a phrase with steals, a UNISON bass,
 *                                         the wheel, the bend and a sound change under held notes: the output with
 *                                         the job on a thread must be the inline job's, every sample (each run in a
 *                                         child process: the same start)
 *   build/host/cr_c1_thread_tsan --tsan   the same phrases with the job on the thread, in this process, built with
 *                                         -fsanitize=thread: the sanitizer names every word both threads touch
 *                                         without a lock between them (tests/run_cr_tests.sh fails on any report)
 * 3y46, P5's CHORD VOICES ON BOTH CORES (fx.c p5x_run): with P5 on the chord part core 1's job is the bass part AND a
 * share of the chord part's voices, while this thread renders the rest. The phrase's chords are up to eight notes.
 * For every pair with a P5 chord the output must also be (d) the UNSPLIT render's: every chord voice in
 * track_render_x, in voice order, on one thread (cr_c1_test_nosplit: the same allocation, no sharing out), and
 * (e), (f) that of a split of its own (cr_c1_test_share: every voice to core 1; none), the job on the thread.
 * 3y177, THE VA CHORD'S VOICES ON BOTH CORES (engine_t.split): the same (d), (e), (f) for every pair with a VA chord
 * (a VA, P5, FM6, CZ-1 bass, and MORPH PAD over PUNCH BASS: the unit's heaviest), the stereo side (SPREAD, USPREAD)
 * summed as the mid is; each core rendered chord voices of the VA's in some block (the list's counts, p5x).
 * 3y187, FM6's TOO: the same for every pair with an FM6 chord (an FM6, VA, P5, CZ-1 bass; TINE EP over DEEP SUB, the
 * power-on sounds; a patch whose operators take the LFO's amplitude modulation, AMS: the part's shares, which
 * fm6_block now figures before the hand-over so that the voices only read them; core 1's scratch is its own).
 * What it cannot show: the unit's timing, its caches and its flash (a core that reads a word wrong: cr_cpu1.h's
 * validation), an interrupt on core 0 during the job, the main loop writing a parameter under both. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#ifdef C1_PLANT                 /* the proof that the sanitizer sees this test's two threads: a word that both parts'
                                 * P5 renders write (eng_prophet_test.c's tap), planted; run_cr_tests.sh wants a report */
static volatile int c1_plant;
#define P5_TAP(k, x) (c1_plant += (int)(x))
#endif
#include "../tools/emu/emu_firmware.h"

static int fails;
typedef struct { uint8_t ce, be, usp; uint16_t cpos, bpos; const char *what; } pair_t;   /* usp: a VA chord in
                                                     * UNISON with USPREAD (its voices panned: the side) */
static uint32_t rs, vmax[2], pmax;                  /* pmax: P5's pool at its fullest (voice.c p5_pool_used) */
static uint32_t sx[2], sxs, sxa;                    /* sxs: blocks in which core 1's VA voices added a side; sxa:
                                                     * blocks in which the FM6 chord's AMS shares were in use */
static void usp_on(const pair_t *p)                 /* (after a sound load: the VA chord's UNISON and USPREAD; the
                                                     * FM6 chord's AMS 1..3 on its operators, the LFO's AMD) */
{
    if (p->usp == 1u && p->ce == ENGI_VA) {
        trk[0].p[P_VOICE] = V_UNISON;
        va_patch[0][VA_USPREAD] = 100;
    }
    if (p->usp == 3u && p->ce == ENGI_FM6) {
        uint32_t k;
        uint8_t *ed = fm6_patch[0];                     /* (the stored patch: fm6_eff is made from it, fm6_pgen) */
        for (k = 0; k < 6u; k++)
            ed[k * FP_OP + FP_AMS] = (uint8_t)(1u + k % 3u);
        ed[FP_LAMD] = 90;
        ed[FP_LFS] = 60;
        ed[FP_LFD] = 0;
        fm6_pgen[0]++;
    }
}                              /* 3y177: the most chord voices of a split engine (not P5: its
                                                     * own counts are cr_dbg's) that core 0 / core 1 rendered in a
                                                     * block (fx.c p5x_run's list, as the block left it) */
static uint32_t rnd(uint32_t n)
{
    rs = rs * 1664525u + 1013904223u;
    return (rs >> 8) % n;
}
/* a phrase on both parts: chords of five and six notes on the chord part (more than P5's four voices: steals, the
 * fade and the deferred note), bass notes under them on part 1 (a UNISON program: the stack), the wheel, the
 * pressure, a bend on both, a sound change on each part under held notes. Returns the digest of every sample */
/* mode: 0 the job inline, 1 on the thread, 2 inline and P5's chord voices not shared out (the reference), 3 / 4 on
 * the thread with every chord voice given to core 1 / to core 0; 5 inline with the effects all on core 0 (3y50: the
 * reference for the reverb on core 1, fx.c mix_fx_split); 6 / 7 as 1 / 5 in THE ROUTING's Chain (cr_fx.c: the serial path,
 * the chord part's sends the amounts; each pair its share of the 24 orders, every order run by some pair: each
 * effect's wet is written by the core that ran it, the assembly and the snapshots are core 0's after the join, so the
 * split is the one core's, bit for bit). Every phrase sends to the three effects from both parts
 * (3y50: the reverb's job runs on the thread in every block that has something for it) */
#define NKEYS 8u
static int c1_ord;                                  /* modes 6, 7: Chain's order (cr_fx.c XF_ORD) */
static void sends_up(void)                          /* the three effects, both parts: up */
{
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        trk[k].p[P_CHOR] = (int16_t)(70 + 20 * k);
        trk[k].p[P_DLY] = (int16_t)(90 - 20 * k);
        trk[k].p[P_REV] = 110;
    }
}
static uint64_t phrase(const pair_t *p, uint32_t nb, int mode)
{
    int32_t out[2u * CTL];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i, k;
    uint8_t held[NKEYS] = {0}, bass = 0;
    rs = 0x1234567u + p->ce * 131u + p->be * 17u + p->cpos;
    vmax[0] = vmax[1] = pmax = sx[0] = sx[1] = sxs = sxa = 0;
    cr_dbg.c1 = C1_OK;
    cr_dbg.p5_c0 = cr_dbg.p5_c1 = cr_dbg.p5_bass = cr_dbg.c1x_jobs = 0;
    cr_c1_test_thread = (uint8_t)(mode == 1 || (mode >= 3 && mode != 5 && mode != 7));
    cr_c1_test_nosplit = (uint8_t)(mode == 2);
    cr_c1_test_share = (uint8_t)(mode == 3 ? P5_POLY + 1u : mode == 4 ? 1u : 0u);
    cr_c1_test_fx0 = (uint8_t)(mode == 5 || mode == 7);
    xfp[XP_RMODE] = (int16_t)(mode >= 6);               /* (THE ROUTING: Chain, at the order given) */
    xfp[XP_ORDER] = (int16_t)(mode >= 6 ? c1_ord : 0);
    cu_pool_load(0, p->ce, p->cpos);
    cu_pool_load(1, p->be, p->bpos);
    usp_on(p);
    sends_up();
    for (b = 0; b < nb; b++) {
        if (!(b % 37u)) {                               /* a new chord over the old one's notes */
            for (i = 0; i < NKEYS; i++)
                if (held[i] && rnd(3)) {
                    trk_note_off(&trk[0], held[i]);
                    held[i] = 0;
                }
            for (i = 0; i < NKEYS; i++)
                if (!held[i] && rnd(4)) {
                    held[i] = (uint8_t)(48u + rnd(30));
                    trk_note_on(&trk[0], held[i], 60u + rnd(67));
                }
        }
        if (!(b % 53u)) {
            if (bass)
                trk_note_off(&trk[1], bass);
            bass = rnd(5) ? (uint8_t)(28u + rnd(20)) : 0u;
            if (bass)
                trk_note_on(&trk[1], bass, 60u + rnd(67));
        }
        if (!(b % 11u)) {
            trk[0].mw = (uint8_t)rnd(128);
            trk[1].mw = (uint8_t)rnd(128);
            trk[rnd(2)].at = (uint8_t)rnd(128);
            midi_bend_target[rnd(2)] = (int32_t)rnd(512) - 256;
        }
        if (b == nb / 2u)                               /* another sound of each engine under the held notes */
            for (k = 0; k < 2u; k++)
                cu_pool_load(k, k ? p->be : p->ce, 1u + rnd(pool_count(k ? p->be : p->ce) - 1u));
        if (b == nb / 2u) {
            usp_on(p);
            sends_up();
        }
        mix_block(out, CTL);
        if (!P5_IS(&trk[0]) && p5x.n) {
            uint32_t c0 = 0;
            for (i = 0; i < p5x.n; i++)
                c0 += !p5x.it[i].c1;
            if (c0 > sx[0])
                sx[0] = c0;
            if (p5x.n1 > sx[1])
                sx[1] = p5x.n1;
            sxs += p5x_side_on;
            sxa += ENGINES[trk[0].engine] == &ENG_FM6 && FM6P(&trk[0]) && FM6P(&trk[0])->pt.pok;
        }
        if ((P5_IS(&trk[0]) || P5_IS(&trk[1])) && p5_pool_used() > pmax)
            pmax = p5_pool_used();
        for (k = 0; k < 2u; k++) {
            uint32_t n = 0;
            for (i = 0; i < NVOICE; i++)
                n += trk[k].v[i].active;
            if (n > vmax[k])
                vmax[k] = n;
        }
        for (i = 0; i < 2u * CTL; i++)
            for (k = 0; k < 4u; k++)
                h = (h ^ (((uint32_t)out[i] >> (8u * k)) & 255u)) * 1099511628211ull;
    }
    return h;
}
static uint64_t fork_phrase(const pair_t *p, uint32_t nb, int thread)
{
    int fd[2];
    uint64_t h = 0;
    uint32_t c[8];
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    pid = fork();
    if (!pid) {
        h = phrase(p, nb, thread);
        c[0] = cr_dbg.p5_c0;
        c[1] = cr_dbg.p5_c1;
        c[2] = pmax;
        c[3] = sx[0];
        c[4] = sx[1];
        c[5] = vmax[0];
        c[6] = sxs;
        c[7] = sxa;
        (void)!write(fd[1], &h, sizeof h);
        (void)!write(fd[1], c, sizeof c);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h || read(fd[0], c, sizeof c) != (ssize_t)sizeof c)
        h = 0;
    else {
        cr_dbg.p5_c0 = c[0];                            /* (the child's counters: the most P5 voices a core rendered) */
        cr_dbg.p5_c1 = c[1];
        pmax = c[2];
        sx[0] = c[3];
        sx[1] = c[4];
        vmax[0] = c[5];
        sxs = c[6];
        sxa = c[7];
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

int main(int argc, char **argv)
{
    int tsan = argc >= 2 && !strcmp(argv[1], "--tsan");
    uint32_t i, uni = 0, k, nb = tsan ? 900u : 3000u, nsplit = 0, nvsplit = 0, nfsplit = 0, nfx = 0, nrt = 0, nro = 0, jobs0 = 0, mpad = 0, pbass = 0;
    uint32_t tine = 0, dsub = 0, fams = 0;
    pair_t P[24];
    uint32_t np = 0;
    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    for (k = 1; k < pool_count(ENGI_PROPHET) && !uni; k++) {   /* a P5 program with the UNISON switch: the bass's stack */
        p5_patch_t q;
        p5_preset_patch(pool_entry(ENGI_PROPHET, k).fk, &q);
        if (q.raw[P5_UNISON])
            uni = k;
    }
    for (k = 1; k < pool_count(ENGI_VA); k++) {     /* the unit's heaviest VA pair (tools/perf_unit.py va) */
        char nm[16];
        pool_name(ENGI_VA, k, nm);
        if (!strcmp(nm, "MORPH PAD"))
            mpad = k;
        if (!strcmp(nm, "PUNCH BASS"))
            pbass = k;
        if (!strcmp(nm, "DEEP SUB"))
            dsub = k;
    }
    for (k = 1; k < pool_count(ENGI_FM6); k++) {    /* tools/perf_unit.py fm6's chord */
        char nm[16];
        pool_name(ENGI_FM6, k, nm);
        if (!strcmp(nm, "TINE EP"))
            tine = k;
    }
#define PAIR(c, cp, b, bp, w) (P[np].usp = 0, P[np].ce = (uint8_t)(c), P[np].cpos = (uint16_t)(cp), P[np].be = (uint8_t)(b), P[np].bpos = (uint16_t)(bp), P[np++].what = (w))
    PAIR(ENGI_PROPHET, 1, ENGI_PROPHET, uni ? uni : 2u, "P5 chord, P5 bass (a UNISON program)");
    PAIR(ENGI_PROPHET, 2, ENGI_PROPHET, 3, "P5 chord, P5 bass");
    PAIR(ENGI_PROPHET, 1, ENGI_VA, 3, "P5 chord, VA bass");
    PAIR(ENGI_PROPHET, 3, ENGI_FM6, 2, "P5 chord, FM6 bass");
    PAIR(ENGI_PROPHET, 4, ENGI_CZ, 4, "P5 chord, CZ-1 bass");
    PAIR(ENGI_PROPHET, 5, ENGI_QUAD, 2, "P5 chord, FM TONE bass");
    PAIR(ENGI_PROPHET, 6, 7u, 1, "P5 chord, WHEEL bass");
    PAIR(ENGI_FM6, 2, ENGI_PROPHET, uni ? uni : 2u, "FM6 chord, P5 bass");
    PAIR(ENGI_VA, 2, ENGI_PROPHET, 4, "VA chord, P5 bass");
    PAIR(ENGI_FM6, 5, ENGI_FM6, 3, "FM6 chord, FM6 bass");
    PAIR(ENGI_VA, 4, ENGI_VA, 2, "VA chord, VA bass");
    PAIR(ENGI_CZ, 3, ENGI_CZ, 5, "CZ-1 chord, CZ-1 bass");
    PAIR(ENGI_QUAD, 2, ENGI_QUAD, 4, "FM TONE chord, FM TONE bass");
    PAIR(ENGI_FM6, 1, ENGI_VA, 1, "FM6 chord, VA bass (the power-on pair's engines)");
    PAIR(ENGI_CZ, 2, ENGI_QUAD, 3, "CZ-1 chord, FM TONE bass");
    PAIR(ENGI_VA, 3, ENGI_FM6, 2, "VA chord, FM6 bass");
    PAIR(ENGI_VA, 5, ENGI_CZ, 4, "VA chord, CZ-1 bass");
    PAIR(ENGI_VA, mpad ? mpad : 1u, ENGI_VA, pbass ? pbass : 2u, "VA chord MORPH PAD (SPREAD), VA bass PUNCH BASS (the unit's heaviest)");
    P[np - 1u].usp = 2;                                 /* (2: the side asserted, MORPH PAD's SPREAD) */
    PAIR(ENGI_VA, 1, ENGI_VA, 3, "VA chord in UNISON with USPREAD, VA bass");
    P[np - 1u].usp = 1;
    PAIR(ENGI_FM6, 4, ENGI_CZ, 4, "FM6 chord, CZ-1 bass");
    PAIR(ENGI_FM6, tine ? tine : 1u, ENGI_VA, dsub ? dsub : 1u, "FM6 chord TINE EP, VA bass DEEP SUB (the power-on sounds)");
    PAIR(ENGI_FM6, 3, ENGI_VA, 2, "FM6 chord with AMS and the LFO's AMD, VA bass");
    P[np - 1u].usp = 3;                                 /* (3: the FM6 chord's patch gets AMS on every operator) */
    fams = np - 1u;
    printf("cr_c1_thread: the bass part's job on a second thread (%u pairs, %u blocks each%s)\n", (unsigned)np, (unsigned)nb,
           tsan ? "; the sanitizer's run" : "");
    for (i = 0; i < np; i++) {
        if (tsan) {
            uint64_t h = phrase(&P[i], nb, 1);
            uint32_t fxj = cr_dbg.c1x_jobs;
            int p5c = P[i].ce == ENGI_PROPHET, vac = P[i].ce == ENGI_VA || P[i].ce == ENGI_FM6;
            int ok = (!vac || (sx[0] >= 1u && sx[1] >= 1u)) && vmax[0] >= 3u && vmax[1] >= 1u && (i || !uni || vmax[1] == P5_STACK) && cr_c1_th_done == cr_c1_th_posted &&
                     cr_c1_th_done - jobs0 == nb + fxj && fxj > nb / 2u &&
                     pmax <= P5_POLY && (!p5c || (vmax[0] > P5_CORE && cr_dbg.p5_c0 >= 2u && cr_dbg.p5_c0 <= P5_CORE && cr_dbg.p5_c1 >= 2u &&
                               cr_dbg.p5_c1 <= P5_CORE));
            fails += !ok;
            printf("  %s %s: %016llx (voices: chord %u, bass %u; P5 voices in a block at most: core 0 %u, core 1 %u; jobs "
                   "on the thread: %u)\n", ok ? "ran " : "FAIL", P[i].what, (unsigned long long)h, (unsigned)vmax[0],
                   (unsigned)vmax[1], (unsigned)cr_dbg.p5_c0, (unsigned)cr_dbg.p5_c1, (unsigned)cr_c1_th_done);
            if (vac)
                printf("  %s chord voices in a block at most: core 0 %u, core 1 %u\n", P[i].ce == ENGI_FM6 ? "FM6" : "VA",
                       (unsigned)sx[0], (unsigned)sx[1]);
            printf("  fx: %u of %u blocks ran the reverb on the thread\n", (unsigned)fxj, (unsigned)nb);
            jobs0 = cr_c1_th_done;
        } else {
            uint64_t a = fork_phrase(&P[i], nb, 0), b = fork_phrase(&P[i], nb, 1), c = fork_phrase(&P[i], nb, 1);
            uint32_t c0 = cr_dbg.p5_c0, c1 = cr_dbg.p5_c1, s0 = sx[0], s1 = sx[1], ss = sxs;
            uint64_t z = fork_phrase(&P[i], nb, 5), r6, r7;
            int ok = a && a == b && a == c && a == z && pmax <= P5_POLY, rok = 1;
            uint32_t o;
            for (o = i; o < 24u || o == i; o += np) {   /* (this pair's orders: i, i + np, ..; all 24 over the pairs) */
                c1_ord = (int)(o % 24u);
                r6 = fork_phrase(&P[i], nb, 6);
                r7 = fork_phrase(&P[i], nb, 7);
                if (!(r6 && r6 == r7 && r6 != a)) {     /* (and Chain is heard: not Parallel's samples) */
                    printf("  FAIL %s: Chain (order %d): on the thread %016llx, all on core 0 %016llx\n", P[i].what,
                           c1_ord, (unsigned long long)r6, (unsigned long long)r7);
                    rok = 0;
                } else {
                    nro++;
                }
            }
            ok = ok && rok;
            nfx += a == z;
            nrt += rok;
            if (P[i].ce == ENGI_PROPHET) {              /* the unsplit render, and two splits of the test's own */
                uint64_t d = fork_phrase(&P[i], nb, 2), e = fork_phrase(&P[i], nb, 3), f = fork_phrase(&P[i], nb, 4);
                ok = ok && a == d && a == e && a == f && c0 >= 2u && c0 <= P5_CORE && c1 >= 2u && c1 <= P5_CORE;
                nsplit += ok;
                printf("  %s %s: inline %016llx, on a thread %016llx, again %016llx; UNSPLIT on one thread %016llx; every chord "
                       "voice on core 1 %016llx, none %016llx (P5 voices in a block at most: core 0 %u, core 1 %u; the pool %u)\n",
                       ok ? "ok  " : "FAIL", P[i].what, (unsigned long long)a, (unsigned long long)b, (unsigned long long)c,
                       (unsigned long long)d, (unsigned long long)e, (unsigned long long)f, (unsigned)c0, (unsigned)c1, (unsigned)pmax);
                fails += !ok;
                continue;
            }
            if (P[i].ce == ENGI_VA || P[i].ce == ENGI_FM6) {   /* 3y177, 3y187: the same three for the VA's and FM6's */
                uint64_t d = fork_phrase(&P[i], nb, 2), e = fork_phrase(&P[i], nb, 3), f = fork_phrase(&P[i], nb, 4);
                uint32_t sa = sxa;
                ok = ok && a == d && a == e && a == f && s0 >= 2u && s1 >= 2u &&
                     (P[i].ce == ENGI_FM6 ? (P[i].usp != 3u || sa > nb / 4u) : (!P[i].usp || ss > nb / 4u));
                if (P[i].ce == ENGI_FM6)
                    nfsplit += ok;
                else
                    nvsplit += ok;
                printf("  %s %s: inline %016llx, on a thread %016llx, again %016llx; UNSPLIT on one thread %016llx; every chord "
                       "voice on core 1 %016llx, none %016llx (chord voices in a block at most: core 0 %u, core 1 %u; chord "
                       "voices %u; blocks with core 1's side %u, with FM6's AMS shares %u)\n", ok ? "ok  " : "FAIL", P[i].what, (unsigned long long)a, (unsigned long long)b,
                       (unsigned long long)c, (unsigned long long)d, (unsigned long long)e, (unsigned long long)f, (unsigned)s0,
                       (unsigned)s1, (unsigned)vmax[0], (unsigned)ss, (unsigned)sa);
                fails += !ok;
                continue;
            }
            fails += !ok;
            printf("  %s %s: inline %016llx, on a thread %016llx, again %016llx\n", ok ? "ok  " : "FAIL", P[i].what,
                   (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
        }
    }
    if (fails) {
        printf("cr_c1_thread: %d FAILED\n", fails);
        return 1;
    }
    if (!tsan)
        printf("split: %u pairs with P5 on the chord part: the two cores' sums are the unsplit render's samples\n", (unsigned)nsplit);
    if (!tsan)
        printf("fm6 split: %u pairs with FM6 on the chord part: the two cores' sums are the unsplit render's samples\n",
               (unsigned)nfsplit);
    if (!tsan)
        printf("va split: %u pairs with the VA on the chord part: the two cores' sums (the mid and the side) are the unsplit "
               "render's samples\n", (unsigned)nvsplit);
    if (!tsan)
        printf("effects: %u pairs, three sends up: the reverb on core 1 gives the samples of the effects all on core 0\n", (unsigned)nfx);
    if (!tsan)
        printf("routing: %u pairs in Chain, %u orders run (all 24 among them): the reverb on core 1 gives the samples of the effects "
               "all on core 0\n", (unsigned)nrt, (unsigned)nro);
    printf(tsan ? "cr_c1_thread: ran (the sanitizer's verdict is its own: no report above, exit 0)\n"
                : "cr_c1_thread: all tests passed (the job on a thread renders the inline job's samples)\n");
    return 0;
}
