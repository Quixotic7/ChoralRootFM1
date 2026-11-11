/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the drums' voices (firmware/src/cr_drum.c, the DRUMS plan Phase 3's CPU gate), no HAL: on core.h,
 * dsp.c and felucca_tables.h.
 *   sh tests/run_cr_tests.sh          (or: cc -std=c99 -O2 -Ibuild/gen -o build/host/cr_drum_test tests/cr_drum_test.c
 *                                      && build/host/cr_drum_test --cpu)
 * Checks: each voice at Character 0, 64 and 127 sounds (peak above -30 dBFS), stays under full scale, rings out (stops
 * computing: live 0) within its time and leaves the mix untouched once it has; the same bytes for the same hit; the
 * closed hat chokes the open one; the snare's Character past 64 fades the snare out as the clap comes in; the test
 * pattern for 8 bars. --cpu: the host's instructions per sample (kernel-counted, macOS) of each voice while it rings,
 * of all five at once, and of the test pattern (information: the unit's figure is Geek Out's). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#define __attribute__(x)
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#define CR_DRUM_TEST 1
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/cr_drum.c"

static int fails, oks;
static void check(int c, const char *what)
{
    printf("%s %s\n", c ? "ok  " : "FAIL", what);
    if (c) oks++; else fails++;
}

static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static int32_t L[CTL], R[CTL];
static uint32_t render(uint32_t blocks, int32_t *peak, uint64_t *hash)   /* blocks rendered while anything rang */
{
    uint32_t b, i, live = 0;
    for (b = 0; b < blocks; b++) {
        int any = 0;
        for (i = 0; i < CRV_N; i++) any |= crd_v[i].live || crd_v[i].trig;
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        cr_drum_mix(L, R, CTL);
        live += (uint32_t)any;
        for (i = 0; i < CTL; i++) {
            int32_t a = L[i] < 0 ? -L[i] : L[i];
            if (peak && a > *peak) *peak = a;
            if (hash) *hash = (*hash ^ (uint32_t)L[i]) * 0x100000001B3ull;
            if (L[i] != R[i]) fails++;
        }
    }
    return live;
}

static const char *const VN[CRV_N] = {"kick", "snare", "clap", "hat closed", "hat open"};
static const uint8_t VD[CRV_N] = {CRD_KICK, CRD_SNARE, CRD_SNARE, CRD_HAT, CRD_HAT};

int main(int argc, char **argv)
{
    int cpu = argc > 1 && !strcmp(argv[1], "--cpu");
    char s[160];
    uint32_t v, c;
    static const uint8_t CH[3] = {0, 64, 127};
    /* each voice at three Characters: sounds, under full scale, rings out, silent after */
    for (v = 0; v < CRV_N; v++)
        for (c = 0; c < 3u; c++) {
            int32_t peak = 0;
            uint64_t h1 = 1469598103934665603ull, h2 = h1;
            uint32_t blocks, ch = v == CRV_CLAP ? 127u : v == CRV_SNARE ? CH[c] / 2u : CH[c];   /* (the clap alone at
                                                          * 127, the snare alone up to 64) */
            if (v == CRV_CLAP && c)
                continue;
            crd_init();
            crd_knob[VD[v]].chr = (uint8_t)ch;
            crd_setup(VD[v]);
            crd_hit(v);
            blocks = render(44100u * 4u / CTL, &peak, &h1);
            snprintf(s, sizeof s, "%s at Character %u: peak %d (%.1f dBFS), rings %.0f ms, then stops", VN[v], ch, peak,
                     peak ? 20.0 * __builtin_log10((double)peak / 32768.0) : -999.0, blocks * CTL * 1000.0 / 44100.0);
            check(peak > 1036 && peak < 32768 && blocks < 44100u * 4u / CTL && !crd_v[v].live, s);
            crd_init();
            crd_knob[VD[v]].chr = (uint8_t)ch;
            crd_setup(VD[v]);
            crd_hit(v);
            render(44100u * 4u / CTL, 0, &h2);
            if (h1 != h2) check(0, "the same hit renders the same bytes");
        }
    {   /* rung out: the mix untouched */
        int32_t i, same = 1;
        for (i = 0; i < CTL; i++) L[i] = R[i] = i * 7 - 50;
        cr_drum_mix(L, R, CTL);
        for (i = 0; i < CTL; i++) same &= L[i] == i * 7 - 50 && R[i] == i * 7 - 50;
        check(same, "nothing ringing: the mix as it was");
    }
    {   /* the choke */
        uint32_t b;
        crd_init();
        crd_hit(CRV_HATO);
        render(20, 0, 0);
        crd_hit(CRV_HATC);
        for (b = 0; b < 60u && crd_v[CRV_HATO].live; b++) render(1, 0, 0);
        snprintf(s, sizeof s, "the closed hat chokes the open one: silent %u blocks (%.1f ms) after", b, b * CTL * 1000.0 / 44100.0);
        check(b > 0u && b < 20u, s);
    }
    {   /* the snare's Character: snare alone to 64, both, the clap alone at 127 */
        crd_init();
        crd_knob[CRD_SNARE].chr = 64; crd_setup(CRD_SNARE);
        check(crd_c[CRV_SNARE].out > 0 && crd_c[CRV_CLAP].out == 0, "snare Character 64: the snare alone");
        crd_knob[CRD_SNARE].chr = 96; crd_setup(CRD_SNARE);
        check(crd_c[CRV_SNARE].out > 0 && crd_c[CRV_CLAP].out > 0, "snare Character 96: the snare and the clap");
        crd_knob[CRD_SNARE].chr = 127; crd_setup(CRD_SNARE);
        check(crd_c[CRV_SNARE].out == 0 && crd_c[CRV_CLAP].out > 0, "snare Character 127: the clap alone");
        crd_hit(CRV_SNARE);
        check(!crd_v[CRV_SNARE].trig, "a voice at no level is not struck");
    }
    {   /* the test pattern: 8 bars at 120 BPM (a 16th = 5512.5 samples) */
        int32_t peak = 0;
        uint32_t t, n = 0;
        uint64_t i0;
        crd_test_step = ~0u;
        i0 = instr_now();
        for (t = 0; t < 8u * 16u * 5512u; t += CTL, n += CTL) {
            cr_drum_test_clock(t, 5512u);
            render(1, &peak, 0);
        }
        i0 = instr_now() - i0;
        snprintf(s, sizeof s, "the test pattern, 8 bars at 120 BPM: peak %d (%.1f dBFS)", peak,
                 20.0 * __builtin_log10((double)peak / 32768.0));
        check(peak < 32768 && peak > 8000, s);
    }
    if (cpu) {   /* each voice while it rings (40 hits, one count), all five at once, the test pattern: the drums alone */
        static const uint8_t DEF[CRD_N] = {64, 32, 64};
        uint64_t i0, tot = 0, smp;
        uint32_t k, b, t, any;
        for (v = 0; v < CRV_N; v++) {
            for (k = 0; k < CRD_N; k++) crd_knob[k].chr = DEF[k];
            if (v == CRV_CLAP) crd_knob[CRD_SNARE].chr = 127;
            crd_init();
            smp = 0;
            i0 = instr_now();
            for (k = 0; k < 40u; k++) {
                crd_hit(v);
                do {
                    cr_drum_mix(L, R, CTL);
                    smp += CTL;
                } while (crd_v[v].live);
            }
            i0 = instr_now() - i0;
            printf("cpu: %-10s %4.0f instructions / sample while it rings (Character %u%s)\n", VN[v], (double)i0 / smp,
                   crd_knob[VD[v]].chr, v >= CRV_HATC ? ", with the metal source" : "");
            tot += i0 / smp;
        }
        for (k = 0; k < CRD_N; k++) crd_knob[k].chr = DEF[k];
        crd_knob[CRD_SNARE].chr = 96;
        crd_init();
        i0 = instr_now();
        for (k = 0; k < 400u; k++) {
            for (v = 0; v < CRV_N; v++) crd_hit(v);
            for (b = 0; b < 8u; b++)                     /* 5.8 ms: all five ring */
                cr_drum_mix(L, R, CTL);
        }
        i0 = instr_now() - i0;
        printf("cpu: all five ringing at once (the snare at 96): %.0f instructions / sample (the five alone summed: %llu)\n",
               (double)i0 / (400.0 * 8.0 * CTL), (unsigned long long)tot);
        crd_init();
        i0 = instr_now();
        for (k = 0; k < 20000u; k++) cr_drum_mix(L, R, CTL);
        i0 = instr_now() - i0;
        printf("cpu: nothing ringing: %.1f instructions / sample\n", (double)i0 / (20000.0 * CTL));
        crd_test_step = ~0u;
        i0 = instr_now();
        for (t = 0, any = 0; t < 8u * 16u * 5512u; t += CTL, any += CTL) {
            cr_drum_test_clock(t, 5512u);
            cr_drum_mix(L, R, CTL);
        }
        i0 = instr_now() - i0;
        printf("cpu: the test pattern (8 bars at 120 BPM): %.0f instructions / sample on average\n", (double)i0 / any);
    }
    printf("cr drum tests %s: %d checks, %d failed\n", fails ? "FAILED" : "passed", oks + fails, fails);
    return fails != 0;
}
