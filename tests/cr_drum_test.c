/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the drums (firmware/src/cr_drum.c: CoralFlower's morph drum synth, its six kits; the beat, Mutable
 * Instruments Grids' pattern generator; the DRUMS plan), no HAL: on core.h, dsp.c and felucca_tables.h.
 *   sh tests/run_cr_tests.sh          (or: cc -std=c99 -O2 -Ibuild/gen -o build/host/cr_drum_test tests/cr_drum_test.c
 *                                      && build/host/cr_drum_test --cpu)
 * Checks, every kit: each sound at Character 0, 64 and 127 sounds (peak above -30 dBFS), stays under full scale and
 * rings out (stops computing) within 8 s; the same bytes for the same hit at VARY 0; an accent louder than a plain hit;
 * nothing ringing leaves the mix as it was; the closed hat chokes the open one; the snare's Character past 64 fades
 * the snare out as the clap comes in; Tune and Decay move the kick. The beat: the blend at every 16th place of X and Y
 * against a reference written from Grids' pattern_generator.cc (drum_map, ReadDrumMap, avrlib's U8Mix) on its
 * resources.cc nodes; the corners are the nodes; Fill 0 plays nothing, Fill and Density full play every step with a
 * level; each step's hits as crd_beat_at says (Chaos 0); an accented hat is the open one, a snare hit strikes the clap
 * with it; Chaos only ever adds hits; 8 bars of a busy beat under full scale. --cpu: the host's instructions per sample
 * (kernel-counted, macOS) of each sound while it rings, of all five at once, and of the busy beat (information: the
 * unit's figure is Geek Out's). */
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
#define CRD_IRQ_OFF() ((void)0)
#define CRD_IRQ_ON() ((void)0)
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

static int32_t L[CTL], R[CTL], SD[CTL], SR[CTL];   /* (the delay's and the reverb's buses: the sends) */
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
        cr_drum_mix(L, R, SD, SR, 0, CTL);
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
static const uint8_t SDRUM[DSS_N] = {CRD_KICK, CRD_SNARE, CRD_SNARE, CRD_HAT, CRD_HAT};
static void knobs(uint32_t chr)
{
    for (uint32_t k = 0; k < CRD_N; k++) {
        crd_knob[k].tune = 12;
        crd_knob[k].decay = 64;
        crd_knob[k].chr = (uint8_t)chr;
        crd_knob[k].fill = 64;
    }
    crd_level = 100;
}
/* Grids (pattern_generator.cc, resources.cc; GPL-3.0-or-later, Emilie Gillet), as written there: the reference */
static const uint8_t REF_MAP[5][5] = {{10, 8, 0, 9, 11}, {15, 7, 13, 12, 6}, {18, 14, 4, 5, 3}, {23, 16, 21, 1, 2},
                                      {24, 19, 17, 20, 22}};
static uint8_t ref_u8mix(uint8_t a, uint8_t b, uint8_t bal) { return (uint8_t)((a * (255 - bal) + b * bal) >> 8); }
static uint8_t ref_read(uint8_t step, uint8_t inst, uint8_t x, uint8_t y)
{
    uint8_t i = x >> 6, j = y >> 6, off = (uint8_t)(inst * 32 + step);
    uint8_t a = CRG_NODE[REF_MAP[i][j]][off], b = CRG_NODE[REF_MAP[i + 1][j]][off];
    uint8_t c = CRG_NODE[REF_MAP[i][j + 1]][off], d = CRG_NODE[REF_MAP[i + 1][j + 1]][off];
    return ref_u8mix(ref_u8mix(a, b, (uint8_t)(x << 2)), ref_u8mix(c, d, (uint8_t)(x << 2)), (uint8_t)(y << 2));
}
/* a bar of the beat (32 steps, crd_beat_step as the loop's grid calls it) with the mix run between: the hits seen */
static uint32_t bar_hits[3], bar_open, bar_clap;
static void beat_bar(int32_t *peak)
{
    uint32_t st, d, b;
    bar_hits[0] = bar_hits[1] = bar_hits[2] = bar_open = bar_clap = 0;
    for (st = 0; st < 32u; st++) {
        uint8_t h0[3];
        for (d = 0; d < 3u; d++) h0[d] = crd_hitn[d];
        crd_beat_step(st);
        for (d = 0; d < 3u; d++)
            if (crd_hitn[d] != h0[d]) bar_hits[d] |= 1u << st;
        if ((bar_hits[2] >> st) & 1u && crd_acc[2]) bar_open |= 1u << st;
        if ((bar_hits[1] >> st) & 1u && crd_v[DSS_CLAP].live) bar_clap |= 1u << st;
        for (b = 0; b < 6u; b++) {                   /* (a 32nd at 120 BPM: 172 samples) */
            memset(L, 0, sizeof L);
            cr_drum_mix(L, R, SD, SR, 0, CTL);
            for (d = 0; d < CTL; d++) if (peak && (L[d] < 0 ? -L[d] : L[d]) > *peak) *peak = L[d] < 0 ? -L[d] : L[d];
        }
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
                crd_hit(v, 1);
                blocks = render(44100u * 8u / CTL, &peak, &h1);
                snprintf(s, sizeof s, "%s: %s at Character %u: peak %d (%.1f dBFS), rings %.0f ms, then stops",
                         CRD_KIT_NAME[kit], SN[v], ch, peak,
                         peak ? 20.0 * __builtin_log10((double)peak / 32768.0) : -999.0, blocks * CTL * 1000.0 / 44100.0);
                check(peak > 1036 && peak < 32768 && blocks < 44100u * 8u / CTL && !crd_v[v].live, s);
                quiet();
                crd_hit(v, 1);
                render(44100u * 8u / CTL, 0, &h2);
                if (h1 != h2) check(0, "the same hit at VARY 0 renders the same bytes");
                if (c == 1u) {                       /* a plain hit: quieter than the accent */
                    int32_t pk = 0;
                    quiet();
                    crd_hit(v, 0);
                    render(44100u * 8u / CTL, &pk, 0);
                    if (!(pk < peak)) check(0, "a plain hit quieter than an accented one");
                }
            }
    }
    check(!lr_differ, "left and right the same (the drums are mono)");
    {   /* rung out: the mix untouched */
        int32_t i, same = 1;
        quiet();
        for (i = 0; i < CTL; i++) L[i] = R[i] = i * 7 - 50;
        cr_drum_mix(L, R, SD, SR, 0, CTL);
        for (i = 0; i < CTL; i++) same &= L[i] == i * 7 - 50 && R[i] == i * 7 - 50;
        check(same, "nothing ringing: the mix as it was");
    }
    {   /* the FX sends (the Mix page): none by default; the snare's into the delay and the reverb alike, the dry mix the
         * same; FX off: none */
        int32_t i, sd = 0, dry0[CTL], same = 1, eq = 1;
        crd_kit_load(0);
        knobs(32);
        crd_send[0] = crd_send[1] = crd_send[2] = 0;
        crd_kit.vary = 0;                          /* (each hit the same) */
        quiet();
        crd_hit(DSS_SNARE, 1);
        memset(SD, 0, sizeof SD); memset(SR, 0, sizeof SR); memset(L, 0, sizeof L);
        cr_drum_mix(L, R, SD, SR, 0, CTL);
        for (i = 0; i < CTL; i++) { sd |= SD[i] | SR[i]; dry0[i] = L[i]; }
        check(!sd, "sends 0 (the default): nothing into the effects");
        crd_send[1] = 100;
        quiet();
        crd_hit(DSS_SNARE, 1);
        memset(SD, 0, sizeof SD); memset(SR, 0, sizeof SR); memset(L, 0, sizeof L);
        cr_drum_mix(L, R, SD, SR, 0, CTL);
        for (i = 0, sd = 0; i < CTL; i++) { sd |= SD[i]; eq &= SD[i] == SR[i]; same &= L[i] == dry0[i]; }
        check(sd && eq && same, "the snare's send 100: into the delay and the reverb alike, the dry mix as it was");
        quiet();
        crd_hit(DSS_SNARE, 1);
        memset(SD, 0, sizeof SD); memset(SR, 0, sizeof SR);
        cr_drum_mix(L, R, SD, SR, 1, CTL);
        for (i = 0, sd = 0; i < CTL; i++) sd |= SD[i] | SR[i];
        check(!sd, "FX off: no send");
        crd_send[1] = 0;
    }
    {   /* the choke */
        uint32_t b;
        crd_kit_load(0);
        knobs(32);
        quiet();
        crd_hit(DSS_HATO, 1);
        render(20, 0, 0);
        crd_hit(DSS_HATC, 1);
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
            crd_hit(DSS_SNARE, 1);
            crd_hit(DSS_CLAP, 1);
            snprintf(s, sizeof s, "snare Character %u: snare %s, clap %s", C[c], crd_v[DSS_SNARE].live ? "on" : "off",
                     crd_v[DSS_CLAP].live ? "on" : "off");
            check(crd_v[DSS_SNARE].live == WANT[c][0] && crd_v[DSS_CLAP].live == WANT[c][1], s);
        }
    }
    {   /* Tune and Decay on the kick */
        uint32_t i0, i1, b0, b1;
        crd_kit.vary = 0;
        knobs(32);
        quiet(); crd_hit(DSS_KICK, 1); i0 = crd_v[DSS_KICK].inc; b0 = render(44100u * 8u / CTL, 0, 0);
        crd_knob[CRD_KICK].tune = 24; crd_knob[CRD_KICK].decay = 127;
        quiet(); crd_hit(DSS_KICK, 1); i1 = crd_v[DSS_KICK].inc; b1 = render(44100u * 8u / CTL, 0, 0);
        snprintf(s, sizeof s, "kick Tune +12: x%.2f the pitch; Decay 127: rings %u blocks for %u", (double)i1 / i0, b1, b0);
        check(i1 > i0 * 19u / 10u && i1 < i0 * 21u / 10u && b1 > b0, s);
    }
    {   /* the blend against Grids */
        uint32_t x, y, d, st, bad = 0, n = 0;
        for (x = 0; x < 256u; x += 16u)
            for (y = 0; y < 256u; y += 16u) {
                crd_beat_blend(x, y);
                for (d = 0; d < 3u; d++)
                    for (st = 0; st < 32u; st++, n++)
                        bad += crd_blend[d][st] != ref_read((uint8_t)st, (uint8_t)d, (uint8_t)x, (uint8_t)y);
            }
        crd_beat_blend(254, 254);
        for (d = 0; d < 3u; d++)
            for (st = 0; st < 32u; st++, n++)
                bad += crd_blend[d][st] != ref_read((uint8_t)st, (uint8_t)d, 254, 254);
        snprintf(s, sizeof s, "the blend: %u levels at 257 places of X / Y as Grids' ReadDrumMap, %u differ", n, bad);
        check(!bad && crd_map_ok, s);
        crd_beat_blend(0, 0);                      /* the corner: node 10, through U8Mix's a * 255 >> 8 twice */
        for (st = 0, bad = 0; st < 32u; st++)
            bad += crd_blend[0][st] != ((CRG_NODE[10][st] * 255 >> 8) * 255 >> 8);
        check(!bad, "X 0, Y 0: node_10 (drum_map[0][0])");
    }
    {   /* the beat: Fill, Density, accents, the clap, Chaos */
        uint32_t d, st, want[3] = {0, 0, 0}, wacc = 0, lv = 0;
        int32_t peak = 0;
        crd_kit_load(0);
        knobs(32);
        crd_rand = 0;
        quiet();
        crd_beat_blend(128, 128);
        for (d = 0; d < 3u; d++) crd_knob[d].fill = 0;
        crd_beat_dens(0);
        beat_bar(0);
        check(!bar_hits[0] && !bar_hits[1] && !bar_hits[2], "Fill 0, Density 0: no hit");
        for (d = 0; d < 3u; d++) crd_knob[d].fill = 127;
        crd_beat_dens(127);
        beat_bar(0);
        for (d = 0, lv = 1; d < 3u; d++)
            for (st = 0; st < 32u; st++)
                lv &= ((bar_hits[d] >> st) & 1u) == (crd_blend[d][st] > 0u);
        check(lv, "Fill and Density full: a hit on every step with a level, none on the others");
        for (d = 0; d < 3u; d++) crd_knob[d].fill = (uint8_t)(56 + d * 8);
        crd_beat_dens(64);
        for (d = 0; d < 3u; d++)
            for (st = 0; st < 32u; st++) {
                if (crd_beat_at(d, st)) want[d] |= 1u << st;
                if (d == 2u && crd_beat_at(d, st) == 2u) wacc |= 1u << st;
            }
        beat_bar(&peak);
        snprintf(s, sizeof s, "Fill 56 64 72: the bar as crd_beat_at says (kick %08X snare %08X hats %08X, open %08X)",
                 (unsigned)bar_hits[0], (unsigned)bar_hits[1], (unsigned)bar_hits[2], (unsigned)bar_open);
        check(bar_hits[0] == want[0] && bar_hits[1] == want[1] && bar_hits[2] == want[2] && bar_open == wacc &&
              (want[0] | want[1] | want[2]) != 0u, s);
        crd_knob[CRD_SNARE].chr = 96;
        beat_bar(0);
        check(bar_clap == bar_hits[1] && bar_hits[1], "the snare's Character 96: every snare hit strikes the clap");
        crd_knob[CRD_SNARE].chr = 32;
        crd_rand = 254;
        for (st = 0, lv = 1; st < 8u; st++) {      /* Chaos: hits only ever added (the perturbation is >= 0) */
            beat_bar(&peak);
            for (d = 0; d < 3u; d++) lv &= (bar_hits[d] & want[d]) == want[d];
        }
        snprintf(s, sizeof s, "Chaos full: 8 bars, every hit of the bar without it kept; peak %d (%.1f dBFS)", peak,
                 20.0 * __builtin_log10((double)peak / 32768.0));
        check(lv && peak < 32768 && peak > 8000, s);
        crd_rand = 0;
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
                    crd_hit(v, 1);
                    do {
                        cr_drum_mix(L, R, SD, SR, 0, CTL);
                        smp += CTL;
                    } while (crd_v[v].live);
                }
                i0 = instr_now() - i0;
                printf("cpu: %s %-10s %4.0f instructions / sample while it rings (Character %u, %.0f ms a hit)\n",
                       CRD_KIT_NAME[kit], SN[v], (double)i0 / smp, crd_knob[SDRUM[v]].chr, smp * 1000.0 / 40.0 / 44100.0);
                tot += i0 / smp;
            }
            knobs(crd_kit.morph);
            crd_knob[CRD_SNARE].chr = 96;
            quiet();
            i0 = instr_now();
            for (k = 0; k < 400u; k++) {
                for (v = 0; v < DSS_N; v++) crd_hit(v, 1);
                crd_v[DSS_HATO].choke = 0;
                for (b = 0; b < 8u; b++)                 /* 5.8 ms: all five ring */
                    cr_drum_mix(L, R, SD, SR, 0, CTL);
            }
            i0 = instr_now() - i0;
            printf("cpu: %s all five ringing at once (the snare at 96): %.0f instructions / sample (the five alone "
                   "summed: %llu)\n", CRD_KIT_NAME[kit], (double)i0 / (400.0 * 8.0 * CTL), (unsigned long long)tot);
        }
        quiet();
        i0 = instr_now();
        for (k = 0; k < 20000u; k++) cr_drum_mix(L, R, SD, SR, 0, CTL);
        i0 = instr_now() - i0;
        printf("cpu: nothing ringing: %.1f instructions / sample\n", (double)i0 / (20000.0 * CTL));
        crd_kit_load(0);                           /* a busy beat: the map's middle, Fill and Density high */
        knobs(32);
        crd_beat_blend(128, 128);
        for (k = 0; k < 3u; k++) crd_knob[k].fill = 100;
        crd_beat_dens(90);
        quiet();
        i0 = instr_now();
        for (t = 0, nsmp = 0; t < 8u * 32u; t++) {   /* 8 bars at 120 BPM: a 32nd 172 samples */
            crd_beat_step(t & 31u);
            for (b = 0; b < 6u; b++, nsmp += CTL)
                cr_drum_mix(L, R, SD, SR, 0, CTL);
        }
        i0 = instr_now() - i0;
        printf("cpu: a busy beat (Club, 8 bars at 120 BPM, hits included): %.0f instructions / sample on average\n",
               (double)i0 / nsmp);
    }
    printf("cr drum tests %s: %d checks, %d failed\n", fails ? "FAILED" : "passed", oks + fails, fails);
    return fails != 0;
}
