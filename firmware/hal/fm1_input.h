/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 input HAL: key/button/encoder matrix and LEDs.
 *
 * One 11-column x 6-row diode matrix behind a 2x74HC595 chain (PA4 SER, PA3
 * SRCLK, PA1 RCLK), bit-banged and polled; rows PA0, PA5..PA8, PB7 with
 * pull-ups (low = closed). LED lines PH6/PH9/PA9/PA10 light the LED on the
 * same column, row PA7/PA8/PA5/PA6 respectively.
 *
 *   fm1_input_init();
 *   polled:  for (;;) { fm1_input_scan(); ... }
 *   IRQ:     call fm1_input_tick() from a ~10 kHz timer ISR; it advances one
 *            column per call (stock-style pipeline: rows are sampled one tick
 *            after the column was latched, the LEDs stay lit in between), debounces
 *            that column's keys at once and the encoders every FM1_NCOL ticks
 *            (a frame). The main loop then only
 *            reads fm1_in.notes / buttons and takes edges/steps with
 *            fm1_input_edges() / fm1_enc_take(), which are IRQ-safe.
 *
 * fm1_input_scan() runs one full frame (11 columns, ~0.6 ms) and calls
 * FM1_INPUT_IDLE() while it waits.
 * Keys/buttons: asymmetric debounce. A press counts after FM1_DEB_PRESS frames closed in a row
 * (1.1-2.2 ms from the contact with the TIMER5 scan): the matrix has diodes and no ghosting, so a closed sample is a
 * closed key; two in a row keep one stray sample from playing a note. A release needs
 * FM1_DEB_RELEASE frames open in a row (~9 ms): a contact bouncing open on the way down, or
 * chattering on the way up, never ends the note early or plays it twice. A key still bouncing
 * when it is let go (open, closed, open..) holds its note until it has been open for that long.
 * Encoders: stock quadrature decoder (2-sample filter, tables 0x2814/0x4182,
 * sign flipped so + = clockwise on the hardware), plus detent counting. An FM-1
 * detent is one full quadrature cycle (4 transitions, M0g) and the knob rests in
 * one state: the one seen at power-on (relearned after FM1_REST_FRAMES still
 * elsewhere). Steps are emitted on arriving back at it, the net transitions
 * rounded to whole cycles (>= 2 counts one: a lost transition or two is
 * forgiven; a two-state jump counts on in the direction of travel). So one
 * click = one step at any speed, bounce and back-and-forth cancel out.
 * fm1_enc_take() returns the steps.
 * LEDs: set fm1_led[col] (packed row bits, bit1 PA5..bit4 PA8); they are lit
 * while that column is selected. fm1_led_key/btn helpers address them by id.
 * A dim glow (fm1_input_tick only): fm1_led_dim[col] are lit for a short pulse at the end of their column's
 * tick, on every frame (~910 Hz, no flicker): ~1/30 of a lit LED (~95 us) at FM1_LED_DIM_NS 3.2 us (the eye
 * is logarithmic: 1/4 and 1/6 read as nearly lit, #35). No wait: the pulse rides on the 595 shift of the next
 * column. Its 16 bits go out while the 595 still drives column p (its outputs change only at the latch), so
 * the lines go lit | dim of p before the shift and dark after its first fm1__dim_k bits, then the rest
 * shifts and latches with the lines dark as before (the read-modify-write edges, 04d7180): nothing reaches
 * another column, and the key read (before it, the lines dark) is unchanged. The shift is the same code on
 * every column, so the pulse is the same width on each; TIMER4 measures it on every pulse and fm1__dim_k
 * follows FM1_LED_DIM_NS (one bit up or down a tick: the widths straddle the target by a bit's time). Only
 * if the whole shift were shorter than the pulse would the rest be waited (console `inp`: dim_pulse_ns,
 * dim_bits). An LED in both is fully lit. FM1_LED_DIM_DIV > 1 also skips frames (keep >= 200 Hz).
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"

#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_INPUT_NOW
#define FM1_INPUT_NOW() fm1_ticks()   /* the press stats' clock (input_test.c: simulated) */
#endif
#ifndef FM1_LED_US
#define FM1_LED_US 40u           /* LED on-time per column (brightness vs scan rate) */
#endif
#define FM1_DEB_PRESS 2u          /* frames closed in a row: a press (a frame = 11 ticks, ~1.1 ms) */
#define FM1_DEB_RELEASE 8u        /* frames open in a row: a release (~9 ms) */
#define FM1_INPUT_LAT 1           /* the press latency stats below (seq.c, console `inp`) */
#define FM1_SETTLE_US 10u
#ifndef FM1_LED_DIM_NS
#define FM1_LED_DIM_NS 3200u      /* a dim LED's pulse per frame (ns; a lit one ~95 us): ~1/30 the brightness */
#endif
#ifndef FM1_LED_DIM_DIV
#define FM1_LED_DIM_DIV 1u        /* a dim LED: the pulse on 1 frame in DIV (1: every frame, ~910 Hz) */
#endif
#ifndef FM1_LED_TRACE
#define FM1_LED_TRACE(rowmask) ((void)0)   /* input_test.c: every write of the LED lines */
#endif
#ifndef FM1_SR_LATCH_TRACE
#define FM1_SR_LATCH_TRACE() ((void)0)     /* input_test.c: the 595 latch */
#endif
#define FM1_REST_FRAMES 900u      /* ~1 s still off the detent state: that is the detent (power-on) */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u


/* key id at (physical column, packed row bit), -1 = none */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
/* encoder i: A at (col, row bit), B at (col, row bit) */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };


static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    uint8_t raw[FM1_NCOL];       /* last frame, packed rows, 1 = closed */
    uint8_t cnt[FM1_NKEY];       /* frames in a row against the debounced state */
    uint32_t note_t0[27];        /* TIMER4 tick of the first scan that saw note key n closed */
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];  /* the detent state (0..3) */
    uint16_t enc_still[FM1_NENC]; /* frames since the last state change */
    int8_t enc_sub[FM1_NENC];    /* net transitions since the last rest state */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    uint32_t frames;
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];

/* scan diagnostics (console `inp`, read and cleared by the main loop): the gap between ticks
 * = the on-time of the column lit in it, in TIMER4 ticks (24 MHz) */
#define FM1_GAP_BINS 8u                /* < 0.15, 0.3, 0.6, 1.2, 2.4, 4.8, 9.6 ms, longer */
static volatile struct {
    uint32_t last, gap_max, gap_hist[FM1_GAP_BINS];
    uint32_t on[FM1_NCOL], on_max[FM1_NCOL];
    uint32_t cost_sum, cost_max, ticks;
    uint32_t enc_moves[FM1_NENC], enc_lost[FM1_NENC];   /* accepted transitions, 2-state jumps */
    uint32_t press_n, press_sum, press_max;   /* note keys: first closed scan -> debounced press (ticks) */
    /* seq.c keyboard_block, from the same first closed scan: to the note-on in the render, and to
     * the note's first sample leaving the I2S DMA (the half it renders starts one half later) */
    uint32_t kb_n, kb_sum, kb_max, dac_sum, dac_max;
    uint32_t dim_n, dim_sum;     /* the dim pulses and their widths (TIMER4 ticks) */
} fm1_in_stat;

static void fm1__led_lines(uint32_t rowmask)       /* row bit 1 PA9, 2 PA10, 3 PH6, 4 PH9 */
{
    FM1_LED_TRACE(rowmask);
    uint32_t a = FM1_PR(FM1_PA, FM1_OUT) & ~((1u << 9) | (1u << 10));
    uint32_t h = FM1_PR(FM1_PH, FM1_OUT) & ~((1u << 6) | (1u << 9));
    FM1_PR(FM1_PA, FM1_OUT) = a | (rowmask & 2u) << 8 | (rowmask & 4u) << 8;
    FM1_PR(FM1_PH, FM1_OUT) = h | (rowmask & 8u) << 3 | (rowmask & 16u) << 5;
}

/* bits i0 .. i1-1 of w (msb first) into the 595; the outputs stay as they are until fm1__sr_latch */
static void fm1__sr_bits(uint32_t w, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    /* read-modify-write per edge on purpose: write-only edges were too short for the
     * 595 on the board and latched the neighbouring column (LEDs and keys copied one column over) */
    for (i = i0; i < i1; i++) {
        if (w & (0x8000u >> i))
            FM1_PR(FM1_PA, FM1_OUT) |= 1u << 4;
        else
            FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 4);
        FM1_PR(FM1_PA, FM1_OUT) |= 1u << 3;
        FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 3);
    }
}
static void fm1__sr_latch(void)
{
    FM1_SR_LATCH_TRACE();
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}
static void fm1__sr_word(uint32_t w)
{
    fm1__sr_bits(w, 0, 16u);
    fm1__sr_latch();
}

static uint32_t fm1__rows(void)
{
    uint32_t a = FM1_PR(FM1_PA, FM1_IN), b = FM1_PR(FM1_PB, FM1_IN);
    return (~((a & 1u) | ((a >> 4) & 0x1Eu) | ((b >> 2) & 0x20u))) & 0x3Fu;
}

static void fm1__wait(uint32_t us)
{
    uint32_t t0 = fm1_ticks(), span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(fm1_ticks() - t0) < span)
        FM1_INPUT_IDLE();
}

static void fm1_input_init(void)
{
    static const uint8_t LEDP[4][2] = {{FM1_PH, 6}, {FM1_PH, 9}, {FM1_PA, 9}, {FM1_PA, 10}};
    const uint32_t rows_a = (1u << 0) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    const uint32_t sr = (1u << 1) | (1u << 3) | (1u << 4), row_b = 1u << 7;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t p = LEDP[i][0], m = 1u << LEDP[i][1];
        FM1_PR(p, FM1_DIE) |= m;
        FM1_PR(p, FM1_OUT) &= ~m;
        FM1_PR(p, FM1_DIR) &= ~m;
        FM1_PR(p, FM1_HD0) |= m;
        FM1_PR(p, FM1_HD) |= m;
    }
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a;
    FM1_PR(FM1_PA, FM1_DIR) |= rows_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~rows_a;
    FM1_PR(FM1_PA, FM1_PU) |= rows_a;
    FM1_PR(FM1_PB, FM1_DIE) |= row_b;
    FM1_PR(FM1_PB, FM1_DIR) |= row_b;
    FM1_PR(FM1_PB, FM1_PD) &= ~row_b;
    FM1_PR(FM1_PB, FM1_PU) |= row_b;
    FM1_PR(FM1_PA, FM1_DIE) |= sr;
    FM1_PR(FM1_PA, FM1_PU) &= ~sr;
    FM1_PR(FM1_PA, FM1_PD) &= ~sr;
    FM1_PR(FM1_PA, FM1_OUT) &= ~sr;
    FM1_PR(FM1_PA, FM1_DIR) &= ~sr;
    fm1__sr_word(0xFFFFu);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;   /* seeded by the first frame */
}

static void fm1__key(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[id];
    uint32_t note = id >= 14u, bit = note ? 1u << (id - 14u) : 1u << id;
    uint32_t on = ((note ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
    if (closed == on) {                            /* agrees with the state: start over */
        *c = 0;
        return;
    }
    if (!on && note && *c == 0u)
        fm1_in.note_t0[id - 14u] = FM1_INPUT_NOW();
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    if (note) {
        if (!on) {
            uint32_t d = FM1_INPUT_NOW() - fm1_in.note_t0[id - 14u];
            fm1_in.notes_pressed |= bit;
            fm1_in_stat.press_n++;
            fm1_in_stat.press_sum += d;
            if (d > fm1_in_stat.press_max)
                fm1_in_stat.press_max = d;
        }
        fm1_in.notes ^= bit;
    } else {
        fm1_in.buttons ^= bit;
        if (!on)
            fm1_in.pressed |= bit;
        else
            fm1_in.released |= bit;
    }
}

static void fm1__keys(uint32_t p)                  /* the keys of column p, just read */
{
    uint32_t r, raw = fm1_in.raw[p];
    for (r = 1; r < 5u; r++)
        if (FM1_KEYMAP[r][p] >= 0)
            fm1__key((uint32_t)FM1_KEYMAP[r][p], (raw >> r) & 1u);
}

static void fm1__frame(void);

static void fm1_input_scan(void)
{
    uint32_t p;
    for (p = 0; p < FM1_NCOL; p++) {
        fm1__led_lines(0);
        fm1__sr_word(0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u));
        fm1__wait(FM1_SETTLE_US);
        fm1_in.raw[p] = (uint8_t)fm1__rows();
        fm1__keys(p);
        fm1__led_lines(fm1_led[p]);
        fm1__wait(FM1_LED_US);
    }
    fm1__led_lines(0);
    fm1__frame();
}

static void fm1__frame(void)
{
    uint32_t e;
    for (e = 0; e < FM1_NENC; e++) {               /* stock SOFT2 decoder + detents */
        const uint8_t *m = FM1_ENC[e];
        uint32_t cur = ((fm1_in.raw[m[0]] >> m[1]) & 1u) << 1 | ((fm1_in.raw[m[2]] >> m[3]) & 1u);
        uint32_t idx;
        volatile int8_t *sub = &fm1_in.enc_sub[e];
        if (cur != fm1_in.enc_last[e]) {
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
            continue;
        }
        if (fm1_in.enc_prev[e] == 0xFF) {          /* first frame: the knob rests here */
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)cur;
        }
        if (fm1_in.enc_still[e] < 0xFFFFu && ++fm1_in.enc_still[e] == FM1_REST_FRAMES &&
            cur != fm1_in.enc_rest[e]) {
            /* parked a long time off the detent state (held at power-on): that is the detent.
             * Never a second state: a knob held mid-click taught the complement of the detent as
             * one more rest and every click then counted twice (#23); short mid-click pauses of a
             * slow turn taught the mid states and the knob went dead */
            fm1_in.enc_rest[e] = (uint8_t)cur;
            *sub = 0;
        }
        if (cur == fm1_in.enc_prev[e])
            continue;
        idx = (uint32_t)fm1_in.enc_prev[e] << 2 | cur;
        if ((0x4182u >> idx) & 1u) {
            (*sub)++;
            fm1_in_stat.enc_moves[e]++;
        } else if ((0x2814u >> idx) & 1u) {
            (*sub)--;
            fm1_in_stat.enc_moves[e]++;
        } else {                                   /* two states in one sample: a fast turn, */
            fm1_in_stat.enc_lost[e]++;             /* the way it was going */
            if (*sub > 0)
                *sub = (int8_t)(*sub + 2);
            else if (*sub < 0)
                *sub = (int8_t)(*sub - 2);
        }
        fm1_in.enc_prev[e] = (uint8_t)cur;
        if (*sub > 100 || *sub < -100)
            *sub = 0;                              /* (never off the detent that long) */
        if (cur == fm1_in.enc_rest[e]) {          /* back on the detent: whole cycles, a lost transition */
            int32_t n = *sub < 0 ? -*sub : *sub;   /* or two forgiven (one click = 4 transitions) */
            n = n >= 2 ? (n + 2) / 4 : 0;
            fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + (*sub < 0 ? -n : n));
            *sub = 0;
        }
    }
    fm1_in.frames++;
}

/* one column per call, from a timer ISR (see top) */
#define FM1__DIM_T ((FM1_LED_DIM_NS * FM1_TICKS_PER_US + 500u) / 1000u)   /* the pulse in TIMER4 ticks */
static uint8_t fm1__tick_col, fm1__dim_k = 16u;   /* the bits of the shift the dim pulse spans (0..16) */
#if FM1_LED_DIM_DIV > 1
static uint8_t fm1__dim_ph;
#endif
static void fm1_input_tick(void)
{
    uint32_t p = fm1__tick_col, n = p + 1u == FM1_NCOL ? 0u : p + 1u;
    uint32_t t0 = fm1_ticks(), g = t0 - fm1_in_stat.last, b = 0;
    uint32_t w = 0xFFFFu ^ (1u << n) ^ (n < 2u ? 1u << (11u + n) : 0u), lit = fm1_led[p], dim = 0, k = fm1__dim_k;
    fm1__led_lines(0);
    fm1_in_stat.last = t0;
    while (b < FM1_GAP_BINS - 1u && g >= (150u * FM1_TICKS_PER_US << b))
        b++;
    fm1_in_stat.gap_hist[b]++;
    if (g > fm1_in_stat.gap_max)
        fm1_in_stat.gap_max = g;
    fm1_in_stat.on[p] += g;
    if (g > fm1_in_stat.on_max[p])
        fm1_in_stat.on_max[p] = g;
    fm1_in.raw[p] = (uint8_t)fm1__rows();          /* column p has been latched one tick (the lines dark) */
#if FM1_LED_DIM_DIV > 1
    if (p == 0u)                                   /* a new frame: the dim LEDs' turn on 1 in FM1_LED_DIM_DIV */
        fm1__dim_ph = (uint8_t)(fm1__dim_ph + 1u >= FM1_LED_DIM_DIV ? 0u : fm1__dim_ph + 1u);
    if (!fm1__dim_ph)
#endif
        dim = fm1_led_dim[p] & ~lit;
    if (dim) {                                     /* the dim pulse of column p: over the first k bits */
        uint32_t t1, d;
        t1 = fm1_ticks();                          /* (before the write: d spans one write and the bits) */
        fm1__led_lines(lit | dim);
        fm1__sr_bits(w, 0, k);                     /* (the 595 still drives column p) */
        d = fm1_ticks() - t1;
        while (k == 16u && d < FM1__DIM_T)         /* only a shift shorter than the pulse waits */
            d = fm1_ticks() - t1;
        fm1__led_lines(0);
        fm1__dim_k = (uint8_t)(d > FM1__DIM_T ? (k ? k - 1u : 0u) : d < FM1__DIM_T && k < 16u ? k + 1u : k);
        fm1_in_stat.dim_n++;
        fm1_in_stat.dim_sum += d;
    } else {
        k = 0;
    }
    fm1__sr_bits(w, k, 16u);
    fm1__sr_latch();                               /* column n, the lines dark */
    fm1__led_lines(fm1_led[n]);
    fm1__tick_col = (uint8_t)n;
    fm1__keys(p);                                  /* its keys now: no wait for the frame's end */
    if (n == 0u)
        fm1__frame();
    g = fm1_ticks() - t0;
    fm1_in_stat.cost_sum += g;
    fm1_in_stat.ticks++;
    if (g > fm1_in_stat.cost_max)
        fm1_in_stat.cost_max = g;
}

/* main-loop critical section against fm1_input_tick (main loop only: it
 * re-enables interrupts unconditionally) */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* detent steps turned since the last call, + = clockwise */
static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}

/* LED of key id (button 0..13 or note key 14..40) */
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
