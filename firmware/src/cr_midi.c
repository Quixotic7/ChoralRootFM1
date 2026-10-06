/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: MIDI in, the pure part (no firmware state; tests/cr_midi_test.c). cr_out.c includes it and does
 * the plumbing (docs/INTEGRATION.md section 2):
 *   crm_clock_*   the clock-in tempo follower: 24 PPQN pulses (ms stamps) -> BPM 20..300, averaged over a beat
 *   crm_map       a channel message on the CHORD / BASS channels -> what ChoralRoot does with it */
#include <stdint.h>

/* ---------------------------------------------------- the clock follower --- */
#define CRM_WIN 24u                      /* pulses averaged: one beat (a tempo jump settles within ~1 beat) */
typedef struct {
    uint32_t t[CRM_WIN + 1u];            /* the last pulses' stamps (ms), a ring */
    uint32_t n;                          /* pulses in the window (0..CRM_WIN + 1) */
    uint32_t w;                          /* the next write */
    uint16_t bpm;                        /* the tempo reported (0: none yet) */
    uint16_t bpm10;                      /* the window's last measure (tenths of a BPM) */
} crm_clock_t;

static void crm_clock_reset(crm_clock_t *c)
{
    uint32_t i;
    for (i = 0; i <= CRM_WIN; i++)
        c->t[i] = 0;
    c->n = c->w = 0;
    c->bpm = c->bpm10 = 0;
}

/* one 0xF8 at ms. Returns the tempo to apply (20..300), or 0: no change. A gap longer than a 20 BPM pulse (or
 * pulses closer than a 300 BPM one's half) restarts the window: garbage never leaves 20..300. */
static int crm_clock_pulse(crm_clock_t *c, uint32_t ms)
{
    uint32_t prev, span, k, b10;
    if (c->n) {
        prev = c->t[(c->w + CRM_WIN) % (CRM_WIN + 1u)];
        span = ms - prev;
        if (span > 150u) {                  /* (a 20 BPM pulse is 125 ms): a stop, a cable: start again */
            c->n = 0;
        } else if (span < 2u && c->n > 1u) {
            return 0;                       /* a duplicate / burst: ignored (a 300 BPM pulse is 8.3 ms) */
        }
    }
    c->t[c->w] = ms;
    c->w = (c->w + 1u) % (CRM_WIN + 1u);
    if (c->n <= CRM_WIN)
        c->n++;
    if (c->n < 7u)                          /* 6 intervals (a quarter beat) before the first tempo */
        return 0;
    k = c->n - 1u;                          /* intervals in the window */
    span = ms - c->t[(c->w + CRM_WIN + 1u - c->n) % (CRM_WIN + 1u)];
    if (!span)
        return 0;
    b10 = (600000u * k / 24u + span / 2u) / span;        /* tenths of a BPM */
    b10 = b10 < 200u ? 200u : b10 > 3000u ? 3000u : b10;
    c->bpm10 = (uint16_t)b10;
    k = (b10 + 5u) / 10u;
    if (k == c->bpm)
        return 0;
    if (c->bpm && (b10 > c->bpm * 10u ? b10 - c->bpm * 10u : c->bpm * 10u - b10) < 8u)
        return 0;                           /* within the stamps' jitter of the tempo applied: no clock reset */
    c->bpm = (uint16_t)k;
    return (int)k;
}

/* ------------------------------------------------- channel messages --- */
enum {
    CRM_NONE,        /* ignored */
    CRM_FORWARD,     /* to Felucca's MIDI in (midi_control.c) as the part's channel: notes, pedal, bend, CC 123 */
    CRM_LEVEL,       /* v: the part's LEVEL 0..127 */
    CRM_SEND_REV, CRM_SEND_CHO, CRM_SEND_DLY,   /* v: the part's send 0..127 */
    CRM_PROGRAM      /* v: the sound list position (PRESETS / ALGORITHM) */
};
typedef struct { uint8_t kind, part; uint8_t v; } crm_act_t;

/* status (0x80..0xEF), d1, d2; the CHORD and BASS channels (0..15, en 0: Off). Up to two actions (one channel can
 * be both: part 0 and part 1 then). Returns the number written to a[]. */
static int crm_map(uint32_t status, uint32_t d1, uint32_t d2, const uint8_t en[2], const uint8_t ch[2],
                   crm_act_t a[2])
{
    uint32_t st = status & 0xF0u, c = status & 0x0Fu, p;
    int n = 0;
    for (p = 0; p < 2u; p++) {
        uint8_t k = CRM_NONE;
        if (!en[p] || (ch[p] & 15u) != c)
            continue;
        if (st == 0xB0u)
            k = d1 == 7u ? CRM_LEVEL : d1 == 91u ? CRM_SEND_REV : d1 == 93u ? CRM_SEND_CHO
              : d1 == 94u ? CRM_SEND_DLY : CRM_FORWARD;
        else if (st == 0xC0u)
            k = CRM_PROGRAM;
        else if (st == 0x80u || st == 0x90u || st == 0xA0u || st == 0xD0u || st == 0xE0u)
            k = CRM_FORWARD;
        if (k == CRM_NONE)
            continue;
        a[n].kind = k;
        a[n].part = (uint8_t)p;
        a[n].v = (uint8_t)((k == CRM_PROGRAM ? d1 : d2) & 0x7Fu);
        n++;
    }
    return n;
}

/* ----------------------------------------------- start / stop (Clock In) --- */
/* 0xFA start / 0xFB continue / 0xFC stop against the loop: playing (1/0), stopped with a loop (1/0), a take in
 * progress (1/0). Returns how many LOOP taps (cr_loop_play toggles) to apply: 0, 1, or 2 (FA while playing: stop and
 * start again, from the top). */
static int crm_transport(uint32_t st, int playing, int stopped, int capturing)
{
    if (capturing)
        return 0;                           /* (a take in progress is the player's) */
    if (st == 0xFCu)
        return playing ? 1 : 0;
    if (st == 0xFAu)
        return playing ? 2 : stopped ? 1 : 0;
    if (st == 0xFBu)
        return !playing && stopped ? 1 : 0;
    return 0;
}
