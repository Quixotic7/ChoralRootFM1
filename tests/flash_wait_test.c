/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* From felucca-bsp 2d37ec8 (tests/flash_wait_test.c), on ChoralRoot's firmware/hal/fm1_flash_wait.h; run by
 * tests/run_tests.sh (docs/HISTORY.md 3y264). */
/* Host model of chip/fm1_flash_wait.h, the completion wait of fm1_flash.h's erase and page program: a NOR part that
 * stays busy for a set time after the command (or for ever), TIMER4 advancing with each status read, and the
 * driver's sequence around the wait (command, fl_wait_issued, then XIP back on). Checks that XIP never comes back
 * while the part is busy, the return codes at the normal bound, the reset at (and only at) the extended bound, and
 * the same across the TIMER4 wrap.
 *
 *   tests/run_tests.sh (with the sanitizers, as the USB host test)
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#define RAMINL static inline
static uint32_t now;                    /* TIMER4: 24 ticks per us */
static uint32_t ready_at;               /* the tick BUSY clears at */
static int stuck;                       /* never clears */
static uint32_t poll_ticks = 24;        /* what one status read costs */
static uint32_t status_reads;
static jmp_buf reset_point;

static int part_busy(void) { return stuck || (int32_t)(ready_at - now) > 0; }
static uint8_t fl_rdsr(uint8_t cmd)
{
    uint8_t sr = part_busy() ? 0x03u : 0x00u;   /* BUSY | WEL while the operation runs */
    (void)cmd;
    status_reads++;
    now += poll_ticks;                  /* the read itself takes time; the status is the one at its start */
    return sr;
}
#define T4_CNT now
static void fl_reset_busy(void) { longjmp(reset_point, 1); }
#include "../firmware/hal/fm1_flash_wait.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef struct { int reset, rc, xip_on_while_busy; uint32_t took, elapsed_us; } result_t;

/* fm1_flash.h's erase / program: T4_CNT read before the command, the command and the cache invalidation, the wait,
 * then fl_exit (XIP back on) */
static result_t operation(uint32_t start, uint32_t busy_us, int never, uint32_t normal, uint32_t extended)
{
    result_t r = {0, 0, 0, 0xFFFFFFFFu, 0};
    uint32_t issued;
    now = start;
    stuck = never;
    issued = T4_CNT;
    now += 24u * 20u;                           /* command bytes and invalidation, before the wait polls */
    ready_at = issued + 24u * busy_us;
    if (setjmp(reset_point)) {
        r.reset = 1;
        r.xip_on_while_busy = 0;                /* the reset path never reaches fl_exit */
        r.elapsed_us = (now - issued) / 24u;
        return r;
    }
    r.rc = fl_wait_issued(issued, normal, extended, &r.took);
    r.xip_on_while_busy = part_busy();         /* fl_exit here: the part must be idle */
    r.elapsed_us = (now - issued) / 24u;
    return r;
}

static void bounds(uint32_t normal, uint32_t extended, uint32_t start)
{
    result_t r;
    r = operation(start, 100u, 0, normal, extended);                 /* quick */
    CHECK(!r.reset && r.rc == 0 && !r.xip_on_while_busy && r.took >= 100u && r.took <= 102u);
    r = operation(start, normal - 2u, 0, normal, extended);          /* seen idle by the normal bound */
    CHECK(!r.reset && r.rc == 0 && !r.xip_on_while_busy && r.took <= normal);
    r = operation(start, normal + 2u, 0, normal, extended);          /* just past it: late, but only once idle */
    CHECK(!r.reset && r.rc == FL_ERR_LATE_READY && !r.xip_on_while_busy && r.took > normal);
    r = operation(start, extended - 3u, 0, normal, extended);        /* just inside the extended bound */
    CHECK(!r.reset && r.rc == FL_ERR_LATE_READY && !r.xip_on_while_busy && r.took < extended);
    r = operation(start, 0, 1, normal, extended);                    /* stuck: reset at the extended bound */
    CHECK(r.reset && r.elapsed_us >= extended && r.elapsed_us <= extended + 2u);
    r = operation(start, extended + 1000u, 0, normal, extended);     /* would clear later: reset all the same */
    CHECK(r.reset && r.elapsed_us >= extended && r.elapsed_us <= extended + 2u);
}

int main(void)
{
    uint32_t seed = 12345u, i;
    CHECK(FL_ERR_LATE_READY == -4);             /* fl_wait_ready's code for a part busy at the bound */
    bounds(500000u, 2000000u, 1000u);           /* sector erase */
    bounds(10000u, 50000u, 1000u);              /* page program */
    bounds(500000u, 2000000u, 0xFFFFFFFFu - 24u * 300000u);   /* TIMER4 wraps during the wait */
    bounds(10000u, 50000u, 0xFFFFFFFFu - 24u * 5000u);
    /* any busy time and poll cost: never XIP while busy; reset only with the part busy at or past the extended
     * bound; 0 exactly when the part was seen idle within the normal bound */
    for (i = 0; i < 20000u; i++) {
        uint32_t busy, start, extended = i & 1u ? 2000000u : 50000u, normal = i & 1u ? 500000u : 10000u;
        result_t r;
        seed = seed * 1664525u + 1013904223u; busy = seed % (extended + extended / 4u);
        seed = seed * 1664525u + 1013904223u; start = seed;
        seed = seed * 1664525u + 1013904223u; poll_ticks = 24u + seed % (24u * 500u);   /* a status read takes at least 1 us (and keeps
                                                                       * the run quick under sanitizers) */
        r = operation(start, busy, (seed >> 20) % 50u == 0u, normal, extended);
        CHECK(!r.xip_on_while_busy);
        if (r.reset)
            CHECK(r.elapsed_us >= extended && (stuck || busy >= extended - 500u));
        else
            CHECK(r.took >= busy && r.took < extended + 500u &&
                  r.rc == (r.took <= normal ? 0 : FL_ERR_LATE_READY));
        if (fails) break;
    }
    printf(fails ? "%d failed\n" : "flash_wait: all checks passed (%u status reads)\n",
           fails ? (unsigned)fails : (unsigned)status_reads);
    return fails != 0;
}
