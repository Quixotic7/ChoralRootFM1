/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the motion helpers (PLAN.md section 5 "Motion", docs/INTEGRATION.md section 4). Integer only.
 *
 * cr_draw.c's animations are pure functions of (screen, anim_ms): the screen builder (cr_ui.c) restarts the clock
 * (cr_anim_mark) when something that animates changes (the chord name: the squeeze; a picker's selection: the slide;
 * a meter's value: the fill; the stripes appearing: their BPM phase) and gives cr_draw the clock through
 * cr_anim_ms, which applies Options > Motion: FULL as drawn, CALM twice as fast (every duration halved, no spring),
 * OFF settled at once (no motion at all). cr_tween and cr_spring serve the motion the renderer does not do itself
 * (a meter's number springing in, the octave slide of the select-key keyboard). */

enum { CR_MOTION_FULL, CR_MOTION_CALM, CR_MOTION_OFF, CR_MOTION_N };
static uint8_t cr_motion = CR_MOTION_FULL;

#define CR_ANIM_SETTLED 60000u                   /* past every duration of cr_draw.c (the stripes: a frozen phase) */

/* ease-out from `from` to `to` over dur ms from t0 (1 - (1 - x)^2); |to - from| < 2^19 (Q12 products in 32 bits) */
static int32_t cr_tween(int32_t from, int32_t to, uint32_t t0, uint32_t dur, uint32_t now)
{
    uint32_t t = now - t0;
    int32_t u;
    if (cr_motion == CR_MOTION_OFF || !dur)
        return to;
    if (cr_motion == CR_MOTION_CALM)
        dur = dur / 2u ? dur / 2u : 1u;
    if (t >= dur)
        return to;
    u = 4096 - (int32_t)((t << 12) / dur);       /* 1 - x, Q12 */
    return from + (((to - from) * (4096 - ((u * u) >> 12))) >> 12);
}

/* a damped spring from `from` to `to`, started at t0: overshoots once by ~20 % and settles in ~dur ms.
 * Piecewise: an ease-out to 1.2 over the first 45 %, then back to 1.0 with an ease-in-out. CALM / OFF: a tween */
static int32_t cr_spring(int32_t from, int32_t to, uint32_t t0, uint32_t dur, uint32_t now)
{
    uint32_t t = now - t0, a = dur * 45u / 100u;
    int32_t over = to + (to - from) / 5;
    if (cr_motion != CR_MOTION_FULL)
        return cr_tween(from, to, t0, dur, now);
    if (t >= dur)
        return to;
    if (t < a)
        return cr_tween(from, over, t0, a, now);
    {
        int32_t x = (int32_t)(((t - a) << 12) / (dur - a));            /* 0..4096 */
        int32_t s = x < 2048 ? (x * x) >> 11 : 4096 - (((4096 - x) * (4096 - x)) >> 11);   /* smoothstep-ish */
        return over + (((to - over) * s) >> 12);
    }
}

/* the clock of the screen's current animations */
typedef struct { uint32_t t0; } cr_anim_t;
static void cr_anim_mark(cr_anim_t *a, uint32_t now) { a->t0 = now; }
static uint32_t cr_anim_ms(const cr_anim_t *a, uint32_t now)
{
    uint32_t t = now - a->t0;
    if (cr_motion == CR_MOTION_OFF)
        return CR_ANIM_SETTLED;
    if (cr_motion == CR_MOTION_CALM)
        return t > CR_ANIM_SETTLED / 2u ? CR_ANIM_SETTLED : t * 2u;
    return t;
}

/* the stripes' cycle: one bar of 4 beats at the tempo (cr_draw.c slides them a cycle per period_ms) */
static uint16_t cr_anim_bar_ms(uint32_t bpm)
{
    uint32_t p = bpm ? 240000u / bpm : 2000u;
    return (uint16_t)(p > 12000u ? 12000u : p);
}
