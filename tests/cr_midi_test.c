/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host tests of ChoralRoot's MIDI in (firmware/src/cr_midi.c): the clock follower and the CC / PC map. */
#include <stdio.h>
#include <stdlib.h>
#include "../firmware/src/cr_midi.c"

static int fails, passes;
static void ok(int c, const char *what)
{
    if (c) passes++;
    else { fails++; printf("FAIL: %s\n", what); }
}

static crm_clock_t K;
static int applied;
static double clk_t;
static void pulses(double bpm, int n)           /* n pulses at bpm, ms stamps as the queue takes them */
{
    int i, b;
    for (i = 0; i < n; i++) {
        b = crm_clock_pulse(&K, (uint32_t)(clk_t + .5));
        if (b) applied = b;
        clk_t += 60000.0 / (bpm * 24.0);
    }
}

static void t_clock(void)
{
    int i, lo = 1000, hi = 0, changes = 0, last;
    uint32_t ms = 1000, seed = 12345;
    crm_clock_reset(&K);
    applied = 0;
    clk_t = 1000.0;
    pulses(120, 6);
    ok(applied == 0, "clock: no tempo before 6 intervals");
    pulses(120, 48);
    ok(applied >= 119 && applied <= 121, "clock: 120 BPM pulses -> 120 +-1");
    last = applied;
    for (i = 0; i < 96; i++) {                  /* steady: no clock resets from the ms rounding */
        pulses(120, 1);
        if (applied != last) changes++;
        last = applied;
    }
    ok(changes == 0, "clock: steady 120 does not flicker");
    pulses(90, 48);                             /* two beats at 90 */
    ok(applied >= 89 && applied <= 91, "clock: a jump to 90 settles within 2 beats");
    pulses(300, 96);
    ok(applied >= 299 && applied <= 300, "clock: 300 BPM");
    pulses(20, 48);
    ok(applied >= 20 && applied <= 21, "clock: 20 BPM");
    crm_clock_reset(&K);
    for (i = 0; i < 20000; i++) {               /* garbage spacing */
        int b;
        seed = seed * 1103515245u + 12345u;
        ms += (seed >> 16) % 7u == 0 ? (seed >> 8) % 400u : (seed >> 16) % 30u;
        b = crm_clock_pulse(&K, ms);
        if (b) {
            if (b < lo) lo = b;
            if (b > hi) hi = b;
        }
    }
    ok(lo >= 20 && hi <= 300 && hi > 0, "clock: garbage spacing stays in 20..300");
    printf("  clock: garbage range %d..%d\n", lo, hi);
}

static void t_map(void)
{
    uint8_t en[2] = {1, 1}, ch[2] = {0, 1};
    crm_act_t a[2];
    int n;
    n = crm_map(0xB0, 7, 100, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_LEVEL && a[0].part == 0 && a[0].v == 100, "map: CC 7 ch 1 -> part 0 level");
    n = crm_map(0xB1, 91, 50, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_SEND_REV && a[0].part == 1 && a[0].v == 50, "map: CC 91 ch 2 -> part 1 reverb");
    n = crm_map(0xB0, 93, 1, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_SEND_CHO, "map: CC 93 -> chorus");
    n = crm_map(0xB0, 94, 1, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_SEND_DLY, "map: CC 94 -> delay");
    n = crm_map(0xB0, 123, 0, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_FORWARD && a[0].part == 0, "map: CC 123 -> Felucca's all notes off on the part");
    n = crm_map(0xC0, 5, 0, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_PROGRAM && a[0].part == 0 && a[0].v == 5, "map: PC ch 1 -> chord sound 5");
    n = crm_map(0xC1, 3, 0, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_PROGRAM && a[0].part == 1 && a[0].v == 3, "map: PC ch 2 -> bass sound 3");
    n = crm_map(0x91, 60, 100, en, ch, a);
    ok(n == 1 && a[0].kind == CRM_FORWARD && a[0].part == 1, "map: a note on ch 2 -> part 1");
    ok(crm_map(0xB5, 7, 1, en, ch, a) == 0 && crm_map(0x95, 60, 1, en, ch, a) == 0, "map: other channels ignored");
    ch[0] = 9; ch[1] = 3;
    n = crm_map(0x99, 60, 100, en, ch, a);
    ok(n == 1 && a[0].part == 0, "map: the configured channel (10) plays part 0");
    ok(crm_map(0x90, 60, 100, en, ch, a) == 0, "map: channel 1 no longer does");
    en[0] = 0;
    ok(crm_map(0x99, 60, 100, en, ch, a) == 0, "map: a channel set Off is ignored");
    en[0] = 1; ch[1] = 9;
    ok(crm_map(0xB9, 7, 1, en, ch, a) == 2 && a[0].part == 0 && a[1].part == 1, "map: one channel for both parts");
}

static void t_transport(void)
{
    ok(crm_transport(0xFA, 0, 1, 0) == 1, "transport: FA plays a stopped loop");
    ok(crm_transport(0xFA, 1, 0, 0) == 2, "transport: FA while playing restarts from the top");
    ok(crm_transport(0xFA, 0, 0, 0) == 0, "transport: FA with no loop: nothing");
    ok(crm_transport(0xFB, 0, 1, 0) == 1 && crm_transport(0xFB, 1, 0, 0) == 0, "transport: FB plays if stopped only");
    ok(crm_transport(0xFC, 1, 0, 0) == 1 && crm_transport(0xFC, 0, 1, 0) == 0, "transport: FC stops a playing loop only");
    ok(crm_transport(0xFA, 0, 1, 1) == 0 && crm_transport(0xFC, 1, 0, 1) == 0, "transport: a take in progress is left alone");
}

int main(void)
{
    t_transport();
    t_clock();
    t_map();
    printf("cr_midi_test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
