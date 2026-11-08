/* SPDX-License-Identifier: GPL-3.0-only */
/* A late erase or page program is done (hal/fm1_flash_late.h, docs/HISTORY.md 3y265), on felucca-bsp's completion
 * wait (hal/fm1_flash_wait.h) and the same host model of the part and TIMER4 as tests/flash_wait_test.c: the driver's
 * sequence (T4 read, the command, fl_wait_issued, fl_late_ok, then XIP back on), as fm1_flash.h's fl_erase4k_ram and
 * fl_prog_ram run it for every caller (the app's saves, the OTA staging, the update loader). Checks: within the
 * normal bound 0, not counted; past it, idle before the extended bound: 0 (success), counted, the longest kept, never
 * XIP while busy; still busy at the extended bound: the reset, never a return; other codes pass through unchanged.
 *   tests/run_tests.sh */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#define RAMINL static inline
static uint32_t now;                    /* TIMER4: 24 ticks per us */
static uint32_t ready_at;
static int stuck;
static jmp_buf reset_point;

static int part_busy(void) { return stuck || (int32_t)(ready_at - now) > 0; }
static uint8_t fl_rdsr(uint8_t cmd)
{
    uint8_t sr = part_busy() ? 0x03u : 0x00u;
    (void)cmd;
    now += 24u * 7u;                    /* a status read: 7 us */
    return sr;
}
#define T4_CNT now
static void fl_reset_busy(void) { longjmp(reset_point, 1); }
#include "../firmware/hal/fm1_flash_wait.h"
#include "../firmware/hal/fm1_flash_late.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef struct { int reset, rc, xip_while_busy; uint32_t took; } res_t;

/* fm1_flash.h's sequence around the wait */
static res_t op(uint32_t busy_us, int never, uint32_t normal, uint32_t extended)
{
    res_t r = {0, 0, 0, 0};
    uint32_t issued;
    now = 0x89ABCDEFu;
    stuck = never;
    issued = T4_CNT;
    now += 24u * 20u;
    ready_at = issued + 24u * busy_us;
    if (setjmp(reset_point)) {
        r.reset = 1;
        return r;
    }
    r.rc = fl_wait_issued(issued, normal, extended, &r.took);
    r.rc = fl_late_ok(r.rc, r.took);
    r.xip_while_busy = part_busy();
    return r;
}

int main(void)
{
    res_t r;
    uint32_t n;
    /* the sector erase: 500 ms / 2 s */
    r = op(18000u, 0, 500000u, 2000000u);                       /* the Puya part's ~18 ms */
    CHECK(!r.reset && r.rc == 0 && !r.xip_while_busy && fl_late_ops == 0 && fl_late_max_us == 0);
    r = op(499000u, 0, 500000u, 2000000u);                      /* just inside the bound */
    CHECK(!r.reset && r.rc == 0 && fl_late_ops == 0);
    r = op(731000u, 0, 500000u, 2000000u);                      /* late: done, counted */
    CHECK(!r.reset && r.rc == 0 && !r.xip_while_busy && r.took >= 731000u && fl_late_ops == 1 &&
          fl_late_max_us == r.took);
    n = fl_late_max_us;
    r = op(600000u, 0, 500000u, 2000000u);                      /* a shorter late one: counted, the longest kept */
    CHECK(!r.reset && r.rc == 0 && fl_late_ops == 2 && fl_late_max_us == n);
    r = op(1999000u, 0, 500000u, 2000000u);                     /* the latest that still finishes */
    CHECK(!r.reset && r.rc == 0 && !r.xip_while_busy && fl_late_ops == 3 && fl_late_max_us >= 1999000u);
    n = fl_late_ops;
    r = op(0, 1, 500000u, 2000000u);                            /* stuck: the reset, never a return, not counted */
    CHECK(r.reset && fl_late_ops == n);
    r = op(2100000u, 0, 500000u, 2000000u);                     /* would finish after 2 s: the reset all the same */
    CHECK(r.reset && fl_late_ops == n);
    /* the page program: 10 ms / 50 ms */
    r = op(1972u, 0, 10000u, 50000u);                           /* the unit's longest page */
    CHECK(!r.reset && r.rc == 0 && fl_late_ops == n);
    r = op(30000u, 0, 10000u, 50000u);                          /* late page: done, counted */
    CHECK(!r.reset && r.rc == 0 && !r.xip_while_busy && fl_late_ops == n + 1u);
    r = op(0, 1, 10000u, 50000u);
    CHECK(r.reset && fl_late_ops == n + 1u);
    /* the other codes are the callers' as before */
    CHECK(fl_late_ok(0, 5u) == 0 && fl_late_ok(-1, 5u) == -1 && fl_late_ok(-2, 5u) == -2 && fl_late_ok(-3, 5u) == -3);
    CHECK(fl_late_ops == n + 1u);
    printf(fails ? "%d failed\n" : "flash_late: all checks passed (%u late operations counted)\n",
           fails ? (unsigned)fails : (unsigned)fl_late_ops);
    return fails != 0;
}
