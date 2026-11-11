/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the drums' voices (firmware/src/cr_drum.c: CoralFlower's morph drum synth, the kits X Club and X Deep;
 * the DRUMS plan Phase 3's CPU gate), no HAL: on core.h, dsp.c and felucca_tables.h.
 *   sh tests/run_cr_tests.sh          (or: cc -std=c99 -O2 -Ibuild/gen -o build/host/cr_drum_test tests/cr_drum_test.c
 *                                      && build/host/cr_drum_test --cpu)
 * Checks, both kits: each sound at Character 0, 64 and 127 sounds (peak above -30 dBFS), stays under full scale and
 * rings out (stops computing) within 8 s (X Deep's kick: up to 4); the same bytes for the same hit at VARY 0; nothing ringing leaves the mix as
 * it was; the closed hat chokes the open one; the snare's Character past 64 fades the snare out as the clap comes in;
 * Tune and Decay move the kick; the test pattern for 8 bars. --cpu: the host's instructions per sample (kernel-counted,
 * macOS) of each sound while it rings, of all five at once, and of the test pattern (information: the unit's figure is
 * Geek Out's). */
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
#include "../firmware/src/cr_res.h"
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
static int lr_differ;
static uint32_t render(uint32_t blocks, int32_t *peak, uint64_t *hash)   /* blocks rendered while anything rang */
{
    uint32_t b, i, live = 0;
    for (b = 0; b < blocks; b++) {
        int any = 0;
        for (i = 0; i < DSS_N; i++) any |= crd_v[i].live;
        if (!any) break;
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        cr_drum_mix(L, R, CTL);
        live++;
        for (i = 0; i < CTL; i++) {
            int32_t a = L[i] < 0 ? -L[i] : L[i];
            if (peak && a > *peak) *peak = a;
            if (hash) *hash = (*hash ^ (uint32_t)L[i]) * 0x100000001B3ull;
            lr_differ |= L[i] != R[i];
        }
    }
    return live;
}

static const char *const SN[DSS_N] = {"kick", "snare", "clap", "hat closed", "hat open"};
static const uint8_t SD[DSS_N] = {CRD_KICK, CRD_SNARE, CRD_SNARE, CRD_HAT, CRD_HAT};
static void knobs(uint32_t chr)
{
    for (uint32_t k = 0; k < CRD_N; k++) {
        crd_knob[k].tune = crd_knob[k].decay = 64;
        crd_knob[k].chr = (uint8_t)chr;
        crd_knob[k].level = 100;
    }
}
static void quiet(void) { memset(crd_v, 0, sizeof crd_v); }

int main(int argc, char **argv)
{
    int cpu = argc > 1 && !strcmp(argv[1], "--cpu");
    char s[200];
    uint32_t v, c, kit;
    static const uint8_t CH[3] = {0, 64, 127};
    for (kit = 0; kit < CRD_NKIT; kit++) {
        crd_kit_load(kit);
        for (v = 0; v < DSS_N; v++)
            for (c = 0; c < 3u; c++) {
                int32_t peak = 0;
                uint64_t h1 = 1469598103934665603ull, h2 = h1;
                uint32_t blocks, ch = v == DSS_CLAP ? 127u : v == DSS_SNARE ? CH[c] / 2u : CH[c];   /* (the clap alone
                                                          * at 127, the snare alone up to 64) */
                if (v == DSS_CLAP && c)
                    continue;
                crd_kit.vary = 0;
                knobs(ch);
                quiet();
                crd_hit(v);
                blocks = render(44100u * 8u / CTL, &peak, &h1);
                snprintf(s, sizeof s, "%s: %s at Character %u: peak %d (%.1f dBFS), rings %.0f ms, then stops",
                         CRD_KIT_NAME[kit], SN[v], ch, peak,
                         peak ? 20.0 * __builtin_log10((double)peak / 32768.0) : -999.0, blocks * CTL * 1000.0 / 44100.0);
                check(peak > 1036 && peak < 32768 && blocks < 44100u * 8u / CTL && !crd_v[v].live, s);
                quiet();
                crd_hit(v);
                render(44100u * 8u / CTL, 0, &h2);
                if (h1 != h2) check(0, "the same hit at VARY 0 renders the same bytes");
            }
    }
    check(!lr_differ, "left and right the same (the drums are mono)");
    {   /* rung out: the mix untouched */
        int32_t i, same = 1;
        quiet();
        for (i = 0; i < CTL; i++) L[i] = R[i] = i * 7 - 50;
        cr_drum_mix(L, R, CTL);
        for (i = 0; i < CTL; i++) same &= L[i] == i * 7 - 50 && R[i] == i * 7 - 50;
        check(same, "nothing ringing: the mix as it was");
    }
    {   /* the choke */
        uint32_t b;
        crd_kit_load(0);
        knobs(32);
        quiet();
        crd_hit(DSS_HATO);
        render(20, 0, 0);
        crd_hit(DSS_HATC);
        for (b = 0; b < 200u && crd_v[DSS_HATO].live; b++) render(1, 0, 0);
        snprintf(s, sizeof s, "the closed hat chokes the open one: silent %u blocks (%.1f ms) after", b,
                 b * CTL * 1000.0 / 44100.0);
        check(b > 0u && b < 20u, s);
    }
    {   /* the snare's Character: the snare alone to 64, both, the clap alone at 127 */
        static const uint8_t C[3] = {64, 96, 127};
        static const uint8_t WANT[3][2] = {{1, 0}, {1, 1}, {0, 1}};
        for (c = 0; c < 3u; c++) {
            quiet();
            crd_knob[CRD_SNARE].chr = C[c];
            crd_hit(DSS_SNARE);
            crd_hit(DSS_CLAP);
            snprintf(s, sizeof s, "snare Character %u: snare %s, clap %s", C[c], crd_v[DSS_SNARE].live ? "on" : "off",
                     crd_v[DSS_CLAP].live ? "on" : "off");
            check(crd_v[DSS_SNARE].live == WANT[c][0] && crd_v[DSS_CLAP].live == WANT[c][1], s);
        }
    }
    {   /* Tune and Decay on the kick */
        uint32_t i0, i1, b0, b1;
        crd_kit.vary = 0;
        knobs(32);
        quiet(); crd_hit(DSS_KICK); i0 = crd_v[DSS_KICK].inc; b0 = render(44100u * 8u / CTL, 0, 0);
        crd_knob[CRD_KICK].tune = 127; crd_knob[CRD_KICK].decay = 127;
        quiet(); crd_hit(DSS_KICK); i1 = crd_v[DSS_KICK].inc; b1 = render(44100u * 8u / CTL, 0, 0);
        snprintf(s, sizeof s, "kick Tune 127: x%.2f the pitch; Decay 127: rings %u blocks for %u", (double)i1 / i0, b1, b0);
        check(i1 > i0 * 19u / 10u && i1 < i0 * 21u / 10u && b1 > b0, s);
    }
    {   /* the test pattern: 8 bars at 120 BPM (a 16th = 5512.5 samples), kit X Club at its VARY */
        int32_t peak = 0;
        uint32_t t;
        crd_kit_load(0);
        crd_test_boot();
        crd_test_step = ~0u;
        quiet();
        for (t = 0; t < 8u * 16u * 5512u; t += CTL) {
            int any = 0;
            cr_drum_test_clock(t, 5512u);
            memset(L, 0, sizeof L);
            cr_drum_mix(L, R, CTL);
            for (v = 0; v < CTL; v++) if ((L[v] < 0 ? -L[v] : L[v]) > peak) peak = L[v] < 0 ? -L[v] : L[v];
            (void)any;
        }
        snprintf(s, sizeof s, "the test pattern, 8 bars at 120 BPM: peak %d (%.1f dBFS)", peak,
                 20.0 * __builtin_log10((double)peak / 32768.0));
        check(peak < 32768 && peak > 8000, s);
    }
    if (cpu) {   /* each sound while it rings (40 hits, one count), all five at once, the test pattern: the drums alone */
        uint64_t i0, tot = 0, smp;
        uint32_t k, b, t, nsmp;
        for (kit = 0; kit < CRD_NKIT; kit++) {
            tot = 0;
            for (v = 0; v < DSS_N; v++) {
                crd_kit_load(kit);
                knobs(crd_kit.morph);
                if (v == DSS_CLAP) crd_knob[CRD_SNARE].chr = 127;
                quiet();
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
                printf("cpu: %s %-10s %4.0f instructions / sample while it rings (Character %u, %.0f ms a hit)\n",
                       CRD_KIT_NAME[kit], SN[v], (double)i0 / smp, crd_knob[SD[v]].chr, smp * 1000.0 / 40.0 / 44100.0);
                tot += i0 / smp;
            }
            knobs(crd_kit.morph);
            crd_knob[CRD_SNARE].chr = 96;
            quiet();
            i0 = instr_now();
            for (k = 0; k < 400u; k++) {
                for (v = 0; v < DSS_N; v++) crd_hit(v);
                crd_v[DSS_HATO].choke = 0;
                for (b = 0; b < 8u; b++)                 /* 5.8 ms: all five ring */
                    cr_drum_mix(L, R, CTL);
            }
            i0 = instr_now() - i0;
            printf("cpu: %s all five ringing at once (the snare at 96): %.0f instructions / sample (the five alone "
                   "summed: %llu)\n", CRD_KIT_NAME[kit], (double)i0 / (400.0 * 8.0 * CTL), (unsigned long long)tot);
        }
        quiet();
        i0 = instr_now();
        for (k = 0; k < 20000u; k++) cr_drum_mix(L, R, CTL);
        i0 = instr_now() - i0;
        printf("cpu: nothing ringing: %.1f instructions / sample\n", (double)i0 / (20000.0 * CTL));
        crd_kit_load(0);
        crd_test_boot();
        crd_test_step = ~0u;
        quiet();
        i0 = instr_now();
        for (t = 0, nsmp = 0; t < 8u * 16u * 5512u; t += CTL, nsmp += CTL) {
            cr_drum_test_clock(t, 5512u);
            cr_drum_mix(L, R, CTL);
        }
        i0 = instr_now() - i0;
        printf("cpu: the test pattern (X Club, 8 bars at 120 BPM, hits included): %.0f instructions / sample on "
               "average\n", (double)i0 / nsmp);
    }
    printf("cr drum tests %s: %d checks, %d failed\n", fails ? "FAILED" : "passed", oks + fails, fails);
    return fails != 0;
}
