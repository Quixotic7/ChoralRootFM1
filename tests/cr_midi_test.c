/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host tests of ChoralRoot's MIDI in (firmware/src/cr_midi.c): the clock follower and the CC / PC map; the CCs on the
 * sounds' parameters (3y269): which, the editor's blocks, the scaling both ways, the echo guard, MIDI CC In off. The
 * cells they reach on each engine: tests/cr_cc_test.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    n = crm_map(0xB0, 7, 100, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_LEVEL && a[0].part == 0 && a[0].v == 100, "map: CC 7 ch 1 -> part 0 level");
    n = crm_map(0xB1, 91, 50, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_SEND_REV && a[0].part == 1 && a[0].v == 50, "map: CC 91 ch 2 -> part 1 reverb");
    n = crm_map(0xB0, 93, 1, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_SEND_CHO, "map: CC 93 -> chorus");
    n = crm_map(0xB0, 94, 1, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_SEND_DLY, "map: CC 94 -> delay");
    n = crm_map(0xB0, 123, 0, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_FORWARD && a[0].part == 0, "map: CC 123 -> Felucca's all notes off on the part");
    n = crm_map(0xC0, 5, 0, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_PROGRAM && a[0].part == 0 && a[0].v == 5, "map: PC ch 1 -> chord sound 5");
    n = crm_map(0xC1, 3, 0, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_PROGRAM && a[0].part == 1 && a[0].v == 3, "map: PC ch 2 -> bass sound 3");
    n = crm_map(0x91, 60, 100, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_FORWARD && a[0].part == 1, "map: a note on ch 2 -> part 1");
    ok(crm_map(0xB5, 7, 1, en, ch, 1, a) == 0 && crm_map(0x95, 60, 1, en, ch, 1, a) == 0, "map: other channels ignored");
    ch[0] = 9; ch[1] = 3;
    n = crm_map(0x99, 60, 100, en, ch, 1, a);
    ok(n == 1 && a[0].part == 0, "map: the configured channel (10) plays part 0");
    ok(crm_map(0x90, 60, 100, en, ch, 1, a) == 0, "map: channel 1 no longer does");
    en[0] = 0;
    ok(crm_map(0x99, 60, 100, en, ch, 1, a) == 0, "map: a channel set Off is ignored");
    en[0] = 1; ch[1] = 9;
    ok(crm_map(0xB9, 7, 1, en, ch, 1, a) == 2 && a[0].part == 0 && a[1].part == 1, "map: one channel for both parts");
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

/* 0.24: MIDI Keys (crm_key; docs/CHORD-LAYOUT.md "MIDI in"): notes 53 .. 79 on the keys' channel are the keys F3 .. G5 */
static void t_keys(void)
{
    uint32_t ev = 0;
    ok(crm_key(0x94, 56, 100, 5, &ev) == 1 && ev == ((56u - 53u) | 100u << 5 | 1u << 12), "keys: ch 5 note-on 56 = key 3 (G#3), down, its velocity");
    ok(crm_key(0x84, 56, 0, 5, &ev) == 1 && ev == (3u | 0u << 5), "keys: note-off: up");
    ok(crm_key(0x94, 56, 0, 5, &ev) == 1 && !((ev >> 12) & 1u), "keys: note-on at velocity 0: up");
    ok(crm_key(0x94, 53, 1, 5, &ev) == 1 && (ev & 31u) == 0u && crm_key(0x94, 79, 1, 5, &ev) == 1 && (ev & 31u) == 26u,
       "keys: 53 is F3, 79 is G5");
    ok(crm_key(0x94, 52, 1, 5, &ev) == 0 && crm_key(0x94, 80, 1, 5, &ev) == 0, "keys: 52 and 80: nothing");
    ok(crm_key(0xB4, 123, 0, 5, &ev) == 1 && ev == CRM_KEY_ALLUP && crm_key(0xB4, 120, 0, 5, &ev) == 1 && ev == CRM_KEY_ALLUP,
       "keys: CC 123 and CC 120: every key up");
    ok(crm_key(0xB4, 7, 100, 5, &ev) == 0 && crm_key(0xC4, 3, 0, 5, &ev) == 0 && crm_key(0xE4, 0, 64, 5, &ev) == 0,
       "keys: other messages on the keys' channel do nothing");
    ok(crm_key(0x90, 60, 100, 5, &ev) == -1 && crm_key(0x94, 60, 100, 0, &ev) == -1, "keys: another channel, or MIDI Keys Off: not the keys'");
}

/* 3y269: the CCs on the sounds' parameters (Options > MIDI CC; docs/MANUAL.md section 15) */
static void t_cc(void)
{
    uint8_t en[2] = {1, 1}, ch[2] = {0, 1};
    crm_act_t a[2];
    crm_echoes_t e;
    uint32_t cc, n, lane, knob, bad, slots = 0, seen[CRM_NCC] = {0};
    int32_t lo, hi, v, g;
    int list;
    /* which */
    for (cc = 0, bad = 0; cc < 128u; cc++) {
        int32_t k = crm_cc_slot(cc);
        int want = cc == 10u || (cc >= 16u && cc <= 63u) || (cc >= 71u && cc <= 79u && cc != 78u) ||
                   (cc >= 80u && cc <= 90u) || cc == 92u || (cc >= 102u && cc <= 113u);
        if (want != crm_cc_eng(cc) || (want ? k < 0 || k >= (int32_t)CRM_NCC || seen[k]++ : k != -1))
            bad++;
        slots += (uint32_t)want;
    }
    ok(!bad && slots == CRM_NCC, "cc: 10, 16..63, 71..77, 79, 80..90, 92, 102..113 (81) each its own slot; 78 and the rest not");
    for (cc = 0, bad = 0; cc < 128u; cc++)       /* the existing CCs never become a sound's (38: cr_out.c forwards it
                                                  * while RPN 0 is selected, its data entry LSB) */
        if ((cc == 1u || cc == 2u || cc == 4u || cc == 5u || cc == 6u || cc == 7u || cc == 11u || cc == 64u || cc == 65u ||
             cc == 91u || cc == 93u || cc == 94u || (cc >= 98u && cc <= 101u) || cc >= 120u) && crm_cc_eng(cc))
            bad++;
    ok(!bad, "cc: 1 2 4 5 6 7 11 64 65 91 93 94 98..101 120..127 are not the sounds'");
    n = crm_map(0xB0, 74, 99, en, ch, 1, a);
    ok(n == 1 && (a[0].kind & CRM_CC) && (a[0].kind & 0x7Fu) == 74u && a[0].part == 0 && a[0].v == 99,
       "map: CC 74 on MIDI Perform's channel, In on -> part 0's cutoff (CRM_CC | 74, 99)");
    n = crm_map(0xB1, 16, 5, en, ch, 1, a);
    ok(n == 1 && a[0].kind == (CRM_CC | 16u) && a[0].part == 1, "map: CC 16 on MIDI Bass's -> part 1");
    n = crm_map(0xB0, 74, 99, en, ch, 0, a);
    ok(n == 1 && a[0].kind == CRM_FORWARD, "map: MIDI CC In off: CC 74 forwarded as before");
    n = crm_map(0xB0, 7, 99, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_LEVEL, "map: CC 7 still the level with In on");
    n = crm_map(0xB0, 78, 1, en, ch, 1, a);
    ok(n == 1 && a[0].kind == CRM_FORWARD, "map: CC 78 (no meaning) forwarded");
    ch[1] = 0;
    ok(crm_map(0xB0, 74, 1, en, ch, 1, a) == 2 && a[0].kind == (CRM_CC | 74u) && a[1].kind == (CRM_CC | 74u) && a[1].part == 1,
       "map: one channel for both parts: both sounds");
    /* the blocks */
    g = crm_cc_block(16, &lane, &knob);
    ok(g == 0 && lane == 0 && knob == 0, "block: 16 = OSC lane 1 KNOB 1");
    g = crm_cc_block(31, &lane, &knob);
    ok(g == 0 && lane == 3 && knob == 3, "block: 31 = OSC lane 4 KNOB 4");
    g = crm_cc_block(38, &lane, &knob);
    ok(g == 1 && lane == 1 && knob == 2, "block: 38 = FILT lane 2 KNOB 3");
    g = crm_cc_block(48, &lane, &knob);
    ok(g == 2 && lane == 0 && knob == 0, "block: 48 = ENV lane 1 KNOB 1");
    g = crm_cc_block(113, &lane, &knob);
    ok(g == 3 && lane == 2 && knob == 3, "block: 113 = LFO lane 3 KNOB 4");
    g = crm_cc_block(90, &lane, &knob);
    ok(g == 4 && lane == 2 && knob == 2, "block: 90 = MOD lane 3 KNOB 3 (the block's last)");
    ok(crm_cc_block(10, &lane, &knob) < 0 && crm_cc_block(74, &lane, &knob) < 0 && crm_cc_block(92, &lane, &knob) < 0,
       "block: the common CCs are no block's");
    for (cc = 0, bad = 0; cc < 128u; cc++)
        if ((g = crm_cc_block(cc, &lane, &knob)) >= 0 && crm_cc_of((uint32_t)g, lane, knob) != cc)
            bad++;
    ok(!bad && crm_cc_of(4, 2, 3) == 0 && crm_cc_of(3, 3, 0) == 0 && crm_cc_of(0, 4, 0) == 0,
       "block: lane and knob back to the CC; past a block's end: none");
    /* the scaling */
    ok(crm_cc_val(0, 0, 127, 0) == 0 && crm_cc_val(127, 0, 127, 0) == 127 && crm_cc_val(64, 0, 127, 0) == 64,
       "scale: 0..127 is the CC");
    ok(crm_cc_val(0, -64, 63, 0) == -64 && crm_cc_val(127, -64, 63, 0) == 63 && crm_cc_val(64, -64, 63, 0) == 0,
       "scale: -64..63: CC 64 is 0");
    ok(crm_cc_val(0, 0, 12700, 0) == 0 && crm_cc_val(127, 0, 12700, 0) == 12700 && crm_cc_val(1, 0, 12700, 0) == 100,
       "scale: hundredths 0..127.00 (F_FIX2): CC n = n.00");
    ok(crm_cc_val(0, 0, 3, 1) == 0 && crm_cc_val(31, 0, 3, 1) == 0 && crm_cc_val(32, 0, 3, 1) == 1 &&
       crm_cc_val(127, 0, 3, 1) == 3 && crm_cc_val(63, 1, 2, 1) == 1 && crm_cc_val(64, 1, 2, 1) == 2,
       "scale: a list in equal slices (4 names: 32 CCs each; 2: below 64 / from 64)");
    ok(crm_cc_val(99, 5, 5, 0) == 5 && crm_cc_out(5, 5, 5, 0) == 0, "scale: a one-value range");
    for (list = 0, bad = 0; list < 2; list++)
        for (lo = -64; lo <= 1; lo += 65)
            for (hi = lo + 1; hi <= lo + 127; hi++)
                for (v = lo; v <= hi; v++) {
                    uint32_t c = crm_cc_out(v, lo, hi, list);
                    if (c > 127u || crm_cc_val(c, lo, hi, list) != v)
                        bad++;
                }
    ok(!bad, "scale: value -> CC -> value is the value for every range of 2..128 steps (linear and lists, from 0 / 1 / -64)");
    for (v = 0, bad = 0; v <= 12700; v++) {     /* a wider range: the CC back gives the nearest of 128 values */
        uint32_t c = crm_cc_out(v, 0, 12700, 0);
        int32_t w = crm_cc_val(c, 0, 12700, 0);
        if (c > 127u || w - v > 50 || v - w > 50)
            bad++;
    }
    ok(!bad, "scale: 0..12700: the CC's value within half a CC step (50) of the value");
    for (cc = 0, bad = 0; cc < 128u; cc++)
        if (crm_cc_out(crm_cc_val(cc, 0, 12700, 0), 0, 12700, 0) != cc || crm_cc_out(crm_cc_val(cc, -6400, 6399, 0), -6400, 6399, 0) != cc)
            bad++;
    ok(!bad, "scale: CC -> value -> CC is the CC (wide ranges)");
    /* the echo guard */
    memset(&e, 0, sizeof e);
    ok(!crm_echo_hit(&e, 0, 0, 0, 5000), "echo: nothing sent, nothing ignored (CC 0 = 0 on ch 1 too)");
    crm_echo_note(&e, 0, 74, 99, 10000);
    ok(crm_echo_hit(&e, 0, 74, 99, 10010) && crm_echo_hit(&e, 0, 74, 99, 10990), "echo: the value sent, back within a second: ignored");
    ok(!crm_echo_hit(&e, 0, 74, 98, 10010) && !crm_echo_hit(&e, 1, 74, 99, 10010) && !crm_echo_hit(&e, 0, 73, 99, 10010),
       "echo: another value, channel or CC: applied");
    ok(!crm_echo_hit(&e, 0, 74, 99, 11100), "echo: after a second: applied");
    for (v = 0; v < 8; v++)
        crm_echo_note(&e, 2, 20, (uint32_t)v, 20000u + (uint32_t)v * 5u);
    ok(crm_echo_hit(&e, 2, 20, 0, 20100) && crm_echo_hit(&e, 2, 20, 7, 20100),
       "echo: a knob's last eight values: an echo arriving late (an older one) is ignored too");
    crm_echo_note(&e, 2, 20, 8, 20100);
    ok(!crm_echo_hit(&e, 2, 20, 0, 20100) && crm_echo_hit(&e, 2, 20, 8, 20100), "echo: the ninth pushes the oldest out");
    crm_echo_note(&e, 3, 30, 1, 0xFFFFFF00u);
    ok(crm_echo_hit(&e, 3, 30, 1, 0x00000100u), "echo: across the ms counter's wrap");
}

int main(void)
{
    t_transport();
    t_keys();
    t_clock();
    t_map();
    t_cc();
    printf("cr_midi_test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
