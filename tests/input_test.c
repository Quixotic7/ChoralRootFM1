/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the key / button debounce in hal/fm1_input.h (fm1__key, fm1__frame), against a model
 * of the scan and of bouncing contacts. The scan is the TIMER5 one (fm1_input_tick): one column every
 * 100 us, sampled one tick after it was latched; a frame (the debounce step) every 11 ticks.
 * A contact closes at time T, bounces (closed / open runs of 30..700 us) for up to 3 ms, stays closed,
 * and on the way up bounces again for up to 5 ms. Checked:
 *   - a clean press counts at the FM1_DEB_PRESS-th scan that sees it (<= 2.3 ms of the contact) and
 *     the press stat measures it;
 *   - 2000 bouncy presses of every note key and button: exactly one note-on (press edge) and one
 *     release each, no note ending while the key is held, none hanging 12 ms after the last bounce;
 *   - a stray closed sample (a 150 us glitch) plays nothing;
 *   - fast repeats (40 ms apart) are all heard;
 *   - the encoders still count one step per detent (their decoder is not touched);
 *   - the LED scan through fm1_input_tick: lit LEDs all of their tick, dim ones a pulse over the start of the
 *     595 shift (no wait) that settles to FM1_LED_DIM_NS +-30 % on every column whatever the bus speed, every
 *     frame, each only on its own column, the lines dark at every latch; the cost of a tick.
 * The GPIO / timer helpers of the header are compiled, never called. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static uint32_t host_now;                          /* TIMER4 ticks (24 MHz) */
#define FM1_INPUT_NOW() host_now
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_time.h"
#include "../firmware/hal/fm1_gpio.h"
static volatile uint32_t host_reg[16][64];        /* the GPIO registers, as memory (the LED test below) */
static uint32_t host_step = 2400u;                /* TIMER4 ticks per read of the clock (the LED test: finer) */
static uint32_t host_ticks(void) { return host_now += host_step; }
/* every write of the LED lines (fm1__led_lines), with the time it happened */
#define LED_NW 8
static uint32_t led_w[LED_NW], led_wt[LED_NW], led_nw, led_now, latch_lit, latches;
#define FM1_LED_TRACE(m) (led_now = (m), led_nw < LED_NW ? (led_wt[led_nw] = host_now, led_w[led_nw++] = (m)) : 0u)
#define FM1_SR_LATCH_TRACE() (latches++, latch_lit |= led_now)   /* the lines at the 595 latch: dark */
/* the GPIO as memory; each access costs host_bus TIMER4 ticks (the LED test: the bus time of the 595 shift) */
static uint32_t host_bus;
#undef FM1_PR
#define FM1_PR(p, r) (*(host_now += host_bus, &host_reg[(p) & 15u][((r) / 4u) & 63u]))
#define fm1_ticks host_ticks
#include "../firmware/hal/fm1_input.h"

#define TICK_US 100u
static int fails;
static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rng = 0x12345u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

/* one contact: a list of times (us) at which it toggles, starting open */
#define NEDGE 1024
static struct { uint32_t t[NEDGE], n; } ct;
static int contact_at(uint32_t us)
{
    uint32_t i, s = 0;
    for (i = 0; i < ct.n && ct.t[i] <= us; i++)
        s ^= 1u;
    return (int)s;
}
static void add_edge(uint32_t us) { if (ct.n < NEDGE) ct.t[ct.n++] = us; }
/* a bouncy run starting at t (first edge = the change), the contact settles to `final` within
 * `span` us; returns the time of the last edge */
static uint32_t bounce(uint32_t t, uint32_t span, int final)
{
    uint32_t end = t + span, last = t;
    int s = final;
    add_edge(t);                                   /* the first touch / the first break */
    while (span) {
        uint32_t run = 30u + rnd(670u);
        if (t + run >= end)
            break;
        t += run;
        add_edge(t);
        s ^= 1;
        last = t;
    }
    if (s != final) {                              /* settle */
        t += 30u + rnd(200u);
        add_edge(t);
        last = t;
    }
    return last;
}

static uint32_t sim_us, sim_col;
static int key_col, key_row;
static void locate(uint32_t id)
{
    int r, p;
    for (r = 0; r < 6; r++)
        for (p = 0; p < (int)FM1_NCOL; p++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                key_row = r;
                key_col = p;
            }
}
/* one TIMER5 tick: as fm1_input_tick, column p's rows are read and its keys debounced, the frame
 * (the encoders) runs after the last column */
static void tick(void)
{
    uint32_t p = sim_col;
    fm1_in.raw[p] = (uint8_t)((int)p == key_col && contact_at(sim_us) ? 1u << key_row : 0u);
    sim_col = p + 1u == FM1_NCOL ? 0u : p + 1u;
    host_now = sim_us * 24u;
    fm1__keys(p);
    if (sim_col == 0u)
        fm1__frame();
    sim_us += TICK_US;
}
static uint32_t state_of(uint32_t id)
{
    return id >= 14u ? (fm1_in.notes >> (id - 14u)) & 1u : (fm1_in.buttons >> id) & 1u;
}
static void reset(void)
{
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    memset((void *)&fm1_in_stat, 0, sizeof fm1_in_stat);
    memset(&ct, 0, sizeof ct);
    {
        uint32_t i;
        for (i = 0; i < FM1_NENC; i++)
            fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
    }
}

/* run until `until` us; count rises / falls of id's state and the press edges */
static uint32_t rises, falls, edges, first_rise_us, last_fall_us, early_fall;
static uint32_t held_from, held_to;               /* the key is down (settled) in [held_from, held_to) */
static void run(uint32_t id, uint32_t until)
{
    while (sim_us < until) {
        uint32_t was = state_of(id), now;
        tick();
        now = state_of(id);
        if (now && !was) {
            rises++;
            if (!first_rise_us)
                first_rise_us = sim_us;
        }
        if (!now && was) {
            falls++;
            last_fall_us = sim_us;
            if (sim_us > held_from && sim_us < held_to)
                early_fall++;
        }
        if (id >= 14u ? fm1_in.notes_pressed : fm1_in.pressed) {
            edges++;
            fm1_in.notes_pressed = fm1_in.pressed = 0;
        }
    }
}

int main(void)
{
    uint32_t id, k, lat_max = 0, lat_sum = 0, lat_n = 0, worst_rel = 0, bad = 0;
    printf("debounce: press %u frames, release %u frames, frame %u us\n",
           (unsigned)FM1_DEB_PRESS, (unsigned)FM1_DEB_RELEASE, (unsigned)(FM1_NCOL * TICK_US));

    /* a clean press of every note key and button, at every phase of the scan */
    for (id = 0; id < FM1_NKEY; id++) {
        uint32_t ph;
        locate(id);
        for (ph = 0; ph < FM1_NCOL * TICK_US; ph += 37u) {
            uint32_t t0;
            reset();
            sim_us = 0;
            sim_col = 0;
            t0 = 10000u + ph;
            add_edge(t0);
            add_edge(t0 + 100000u);
            rises = falls = edges = first_rise_us = last_fall_us = early_fall = 0;
            held_from = t0;
            held_to = t0 + 100000u;
            run(id, t0 + 130000u);
            if (rises != 1u || falls != 1u || edges != 1u)
                bad++;
            if (first_rise_us - t0 > lat_max)
                lat_max = first_rise_us - t0;
            lat_sum += first_rise_us - t0;
            lat_n++;
            if (last_fall_us - (t0 + 100000u) > worst_rel)
                worst_rel = last_fall_us - (t0 + 100000u);
        }
    }
    printf("clean press: contact -> press avg %u us, max %u us; release -> note off max %u us\n",
           (unsigned)(lat_sum / lat_n), (unsigned)lat_max, (unsigned)worst_rel);
    check("clean presses: one press and one release each, every key and button", bad == 0u);
    check("clean press: counted within 2.3 ms of the contact (2 frames)", lat_max <= 2300u);
    check("clean release: the note ends within 10 ms", worst_rel <= 10000u);
    {
        uint32_t stat = fm1_in_stat.press_n ? fm1_in_stat.press_sum / fm1_in_stat.press_n / 24u : 0u;
        printf("press stat (first closed frame -> press): avg %u us over %u\n", (unsigned)stat,
               (unsigned)fm1_in_stat.press_n);
        check("the press stat counts the note keys' presses (one frame after the first)",
              fm1_in_stat.press_n == 1u && stat >= 1000u && stat <= 1200u);
    }

    /* bouncy presses and releases */
    {
        uint32_t trials = 0, onebad = 0, early = 0, hang = 0;
        uint32_t blat_max = 0, blat_sum = 0, rel_max = 0, settle_max = 0;
        for (k = 0; k < 2000u; k++) {
            uint32_t t0, tb, hold, tr, tl;
            id = rnd(FM1_NKEY);
            locate(id);
            reset();
            sim_us = rnd(1100u);
            sim_col = rnd(FM1_NCOL);
            t0 = 5000u + rnd(1100u);
            tb = bounce(t0, rnd(3000u), 1);        /* settled closed from tb */
            hold = 15000u + rnd(300000u);
            tr = tb + hold;
            tl = bounce(tr, rnd(5000u), 0);        /* settled open from tl */
            rises = falls = edges = first_rise_us = last_fall_us = early_fall = 0;
            held_from = tb;
            held_to = tr;
            run(id, tl + 12000u);
            trials++;
            if (rises != 1u || falls != 1u || edges != 1u)
                onebad++;
            early += early_fall;
            if (state_of(id))
                hang++;
            if (first_rise_us - t0 > blat_max)
                blat_max = first_rise_us - t0;
            blat_sum += first_rise_us - t0;
            if (first_rise_us > tb && first_rise_us - tb > settle_max)
                settle_max = first_rise_us - tb;
            if (last_fall_us > tl && last_fall_us - tl > rel_max)
                rel_max = last_fall_us - tl;
        }
        printf("bouncy press (<= 3 ms of bounce): first touch -> press avg %u us, max %u us; "
               "settled -> press max %u us; last bounce -> note off max %u us\n",
               (unsigned)(blat_sum / trials), (unsigned)blat_max, (unsigned)settle_max, (unsigned)rel_max);
        check("2000 bouncy presses: exactly one note-on / press and one release each", onebad == 0u);
        check("no note ends while its key is held", early == 0u);
        check("no hanging note 12 ms after the last bounce of a release", hang == 0u);
        check("bouncy press: counted within 2.3 ms of the contact settling, or sooner", settle_max <= 2300u);
        check("bouncy release: the note ends within 10 ms of the last bounce", rel_max <= 10000u);
    }

    /* a stray closed sample: no note */
    {
        uint32_t g, any = 0;
        id = 20;
        locate(id);
        for (g = 0; g < FM1_NCOL * TICK_US; g += 13u) {
            reset();
            sim_us = 0;
            sim_col = 0;
            add_edge(5000u + g);
            add_edge(5150u + g);
            rises = falls = edges = first_rise_us = 0;
            held_from = held_to = 0;
            run(id, 40000u);
            any += rises + edges;
        }
        check("a 150 us glitch plays no note", any == 0u);
    }

    /* fast repeats */
    {
        uint32_t t;
        id = 30;
        locate(id);
        reset();
        sim_us = 0;
        sim_col = 0;
        for (t = 0; t < 10u; t++) {
            bounce(5000u + t * 40000u, 1500u, 1);
            bounce(5000u + t * 40000u + 20000u, 2000u, 0);
        }
        rises = falls = edges = first_rise_us = 0;
        held_from = held_to = 0;
        run(id, 5000u + 10u * 40000u + 20000u);
        check("10 notes 40 ms apart (20 ms held, bouncy): 10 note-ons, 10 releases",
              rises == 10u && edges == 10u && falls == 10u);
    }

    /* encoders: one clockwise detent cycle of encoder 0 = one step (decoder unchanged) */
    {
        static const uint8_t SEQ[] = {0, 1, 3, 2, 0};   /* quadrature states A<<1|B, from the rest */
        uint32_t i, f;
        const uint8_t *m = FM1_ENC[0];
        int32_t s;
        reset();
        for (i = 0; i < 5u; i++)
            for (f = 0; f < 4u; f++) {
                memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
                fm1_in.raw[m[0]] |= (uint8_t)(((SEQ[i] >> 1) & 1u) << m[1]);
                fm1_in.raw[m[2]] |= (uint8_t)((SEQ[i] & 1u) << m[3]);
                fm1__frame();
            }
        s = fm1_in.enc_steps[0];
        printf("encoder 0: one cycle -> %d step(s)\n", (int)s);
        check("an encoder detent cycle is one step", s == 1 || s == -1);
    }

    {   /* the LEDs through fm1_input_tick (the GPIO as memory): each tick writes the lines dark (the key read),
         * then, a dim-only LED on column p: lit | dim of p, the first bits of the shift, dark; the rest of the shift,
         * the latch (the lines dark), the lit LEDs of column n. Over bus speeds from 0 (the shift shorter than the
         * pulse: the rest waited) to 4 TIMER4 ticks an access (a bit ~1 us): the pulse within 30 % of
         * FM1_LED_DIM_NS on every column once settled, nothing of another column, ever */
        static const uint32_t BUS[] = {0u, 1u, 2u, 4u};
        uint32_t bi;
        for (bi = 0; bi < sizeof BUS / sizeof BUS[0]; bi++) {
            uint32_t t, col, prev, lit[FM1_NCOL][5], dimw[FM1_NCOL][5], frames = 400u, bad = 0, badw = 0, r;
            uint32_t pulse_min[FM1_NCOL], pulse_max = 0, pulses = 0, waits = 0, kmin = 16u, kmax = 0u, c, pmin = ~0u;
            const uint32_t T = (FM1_LED_DIM_NS * FM1_TICKS_PER_US + 500u) / 1000u;
            char what[96];
            memset(lit, 0, sizeof lit);
            memset(dimw, 0, sizeof dimw);
            for (c = 0; c < FM1_NCOL; c++)
                pulse_min[c] = ~0u;
            reset();
            FM1_PR(FM1_PA, FM1_IN) = FM1_PR(FM1_PB, FM1_IN) = 0xFFFFFFFFu;   /* rows open */
            memset(fm1_led, 0, sizeof fm1_led);
            memset(fm1_led_dim, 0, sizeof fm1_led_dim);
            fm1_led[3] = 1u << 2;                     /* lit: column 3 row 2 */
            fm1_led_dim[3] = 1u << 2 | 1u << 4;       /* dim: column 3 rows 2 (also lit) and 4 */
            fm1_led_dim[4] = 0x1Eu;                   /* the next column: every row dim */
            fm1_led_dim[10] = 1u << 1;
            fm1_led_dim[0] = 1u << 3;
            fm1_led[7] = 1u << 3;                     /* a column with a lit LED and no dim one */
            host_step = 1u;                           /* a clock read: 1 tick (~42 ns) */
            host_bus = BUS[bi];
            latch_lit = latches = 0;
            for (t = 0; t < frames * FM1_NCOL; t++) {
                uint32_t want, d;
                prev = fm1__tick_col;
                led_nw = 0;
                fm1_input_tick();
                col = fm1__tick_col;
                want = fm1_led[col];
                d = fm1_led_dim[prev] & ~fm1_led[prev];
#if FM1_LED_DIM_DIV > 1
                if (fm1__dim_ph)                      /* (frames skipped: not the dim LEDs' turn) */
                    d = 0;
#endif
                /* the writes: 0 (the key read), [lit | dim of p, 0 (the pulse)], lit of n */
                badw += led_nw != (d ? 4u : 2u) || led_w[0] != 0u || led_w[led_nw - 1u] != want ||
                        (d && (led_w[1] != (fm1_led[prev] | d) || led_w[2] != 0u));
                if (d) {
                    uint32_t wt = led_wt[2] - led_wt[1];
                    bad += (led_w[1] & ~(uint32_t)(fm1_led[prev] | fm1_led_dim[prev])) != 0u;   /* column p's only */
                    pulses++;
                    if (t >= 20u * FM1_NCOL) {            /* settled (20 frames; 4 dim columns here) */
                        pulse_min[prev] = wt < pulse_min[prev] ? wt : pulse_min[prev];
                        pulse_max = wt > pulse_max ? wt : pulse_max;
                        pmin = wt < pmin ? wt : pmin;
                        kmin = fm1__dim_k < kmin ? fm1__dim_k : kmin;
                        kmax = fm1__dim_k > kmax ? fm1__dim_k : kmax;
                    }
                    for (r = 1; r < 5u; r++)
                        dimw[prev][r] += (d >> r) & 1u;
                }
                bad += (want & ~(uint32_t)fm1_led[col]) != 0u;   /* column n's lit only */
                for (r = 1; r < 5u; r++)
                    lit[col][r] += (led_w[led_nw - 1u] >> r) & 1u;
            }
            waits = kmax == 16u;
            for (c = 0; c < FM1_NCOL; c++)
                bad += pulse_min[c] != ~0u && (pulse_min[c] * 10u < T * 7u);
            snprintf(what, sizeof what, "LEDs, bus %u tick(s) an access: lit every frame, own column only, dark at the latch",
                     (unsigned)host_bus);
            check(what, !bad && !badw && !latch_lit && latches == frames * FM1_NCOL && lit[3][2] == frames &&
                  lit[7][3] == frames && !lit[2][2] && !lit[3][4] && !lit[4][1]);
            snprintf(what, sizeof what, "  dim: a pulse every frame on its own column, %u..%u ns (target %u +-30 %%)",
                     (unsigned)(pmin * 1000u / FM1_TICKS_PER_US), (unsigned)(pulse_max * 1000u / FM1_TICKS_PER_US),
                     (unsigned)FM1_LED_DIM_NS);
            check(what, pulses == frames / FM1_LED_DIM_DIV * 4u && dimw[3][4] == frames / FM1_LED_DIM_DIV &&
                  dimw[4][1] == frames / FM1_LED_DIM_DIV && dimw[4][4] == frames / FM1_LED_DIM_DIV &&
                  dimw[10][1] == frames / FM1_LED_DIM_DIV && dimw[0][3] == frames / FM1_LED_DIM_DIV &&
                  !dimw[3][2] && !dimw[5][1] && !dimw[9][1] && !dimw[7][3] &&
                  pmin * 10u >= T * 7u && pulse_max * 10u <= T * 13u);
            printf("LEDs, bus %u: the pulse spans %u..%u bits of the shift%s\n", (unsigned)host_bus, (unsigned)kmin,
                   (unsigned)kmax, waits ? " (the shift shorter than the pulse: the rest waited)" : " (no wait)");
        }
        host_bus = 0;
        host_step = 2400u;
        printf("LEDs: refresh %u Hz lit, %u Hz dim\n", (unsigned)(1000000u / (FM1_NCOL * TICK_US)),
               (unsigned)(1000000u / (FM1_NCOL * TICK_US * FM1_LED_DIM_DIV)));
        {   /* the host cost of a tick, the dim LEDs on and off (no bus time: the shift waits out the pulse) */
            uint32_t k, t, n = 2000000u;
            double ns[2];
            host_step = 1000u;                       /* (the clock far ahead on each read: no wait on the host) */
            for (k = 0; k < 2u; k++) {
                clock_t c0;
                memset(fm1_led_dim, k ? 0x1E : 0, sizeof fm1_led_dim);
                c0 = clock();
                for (t = 0; t < n; t++) {
                    led_nw = 0;
                    fm1_input_tick();
                }
                ns[k] = (double)(clock() - c0) * 1e9 / CLOCKS_PER_SEC / n;
            }
            host_step = 2400u;
            printf("LEDs: host %.1f ns per tick without dim LEDs, %.1f ns with\n", ns[0], ns[1]);
        }
    }

    if (fails) {
        printf("INPUT TEST FAILED (%d)\n", fails);
        return 1;
    }
    printf("input debounce: all ok\n");
    return 0;
}
