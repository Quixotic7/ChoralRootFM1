/* SPDX-License-Identifier: GPL-3.0-only
 * The AC79's second core, after fm1-x0x (GPL-3.0) and Melodee (GPL-3.0, github.com/keremimo/melodee), following
 * JieLi's AC79 SDK EnableOtherCpu (Apache-2.0). ChoralRoot's use of it: docs/DUALCORE.md (the bass part's render,
 * one job per audio block; firmware/src/cr_cpu1.h). The job validation and the fault path: ported from Melodee's PR 21
 * "fix(dual-core): raise the core supply and survive CPU1 misreads, faults and stalls" (Kerem Kilic / Melodee, GPL-3.0). */
/* The AC79's second core (fm1-x0x's docs/plans/2026-10-04-dual-core-and-clock.md), with what Melodee's
 * dual-core audio (github.com/keremimo/melodee, GPL-3.0) found on the device.
 *
 * Start (the AC79 SDK's EnableOtherCpu): core 1's interrupt bank off (0x1EEF300..), its entry
 * address into the word at 0x01C7FFF8, bit 3 of 0x10008 held while it starts, C1_CON (0x1EEE004)
 * bit 3 set (enabled) and bit 1 cleared (out of hold). It boots through the chip's ROM, which the
 * PC limits refuse, so they are open until it has reported in from fm1_c1_main. Hold is bit 1
 * set, then bit 3 cleared.
 *
 * Work: one job at a time, a function and an argument; core 0 hands it over (c1_job) and waits
 * for it (c1_done), single writer each way, csync around each (the cores share the caches).
 *
 * Flash: the waiting loop is in RAM (.c1_text), so an idle core 1 never fetches from the flash;
 * it works only for the audio interrupt, which waits for it before it returns, and core 0 turns
 * the flash off only with interrupts off. So the flash is never off under it. */
/* The AC79's second core (fm1-x0x's docs/plans/2026-10-04-dual-core-and-clock.md), with what Melodee's
 * dual-core audio (github.com/keremimo/melodee, GPL-3.0) found on the device.
 *
 * Start (the AC79 SDK's EnableOtherCpu): core 1's interrupt bank off (0x1EEF300..), its entry
 * address into the word at 0x01C7FFF8, bit 3 of 0x10008 held while it starts, C1_CON (0x1EEE004)
 * bit 3 set (enabled) and bit 1 cleared (out of hold). It boots through the chip's ROM, which the
 * PC limits refuse, so they are open until it has reported in from fm1_c1_main. Hold is bit 1
 * set, then bit 3 cleared.
 *
 * Work: one job at a time, a function and an argument; core 0 hands it over (fm1_c1_post: fn, arg, a check word,
 * then the sequence number fm1_c1_job) and waits for it (fm1_c1_done), single writer each way, csync around each
 * (the cores share the caches). Some units' core 1 now and then misreads SRAM (Melodee PR 21: 0x00200000 where RAM
 * holds 0; one misread of the mailbox at rest called a null job). So core 1 runs a request only when its sequence
 * is exactly the next one (the count of finished jobs stays in its own register) and the check word matches the
 * fn, arg and sequence it read; anything else is counted (fm1_c1_rejected) and never run. The mailbox never holds a
 * null fn: at rest it is fm1_c1_idle. What core 0 does about the count: cr_cpu1.h (the escalation).
 *
 * Faults: a fault about core 1 must not reset the unit (fm1_c1_fault_c, called from fm1_vec.S before the crash
 * report): if core 1 took the exception itself, it records it and holds itself; if the debug unit raised it on
 * core 0 for core 1 (DBG_MSG's core 1 bits only: its PC limit, its write limits, its bus errors), core 1 is held,
 * the fault recorded and core 0 resumes where it was. Core 0's own faults still end in the crash report.
 *
 * Flash: the waiting loop is in RAM (.c1_text), so an idle core 1 never fetches from the flash;
 * it works only for the audio interrupt, which waits for it before it returns, and core 0 turns
 * the flash off only with interrupts off. So the flash is never off under it.
 *
 * FM1_CPU1_LOGIC_ONLY (tests/cr_cpu1_test.c): the mailbox and its validation alone, for the host. */
#pragma once
#include <stdint.h>

#ifndef FM1_C1_SYNC
#define FM1_C1_SYNC() __asm__ volatile("csync" ::: "memory")
#endif
#ifndef FM1_C1_TEXT
#define FM1_C1_TEXT __attribute__((section(".c1_text")))
#endif
/* the check word of a request: the fn, the arg and the sequence as core 0 wrote them (a misread of any fails it) */
#define FM1_C1_CHECK(fn, arg, seq) \
    ((uint32_t)(uintptr_t)(fn) ^ (uint32_t)(arg) ^ (uint32_t)(seq) * 0x9E3779B1u ^ 0xC15A11EDu)

static void FM1_C1_TEXT fm1_c1_idle(uint32_t unused) { (void)unused; }   /* the mailbox at rest: never null */
static volatile uint32_t fm1_c1_alive, fm1_c1_job, fm1_c1_done, fm1_c1_arg, fm1_c1_chk;
static volatile uint32_t fm1_c1_rejected;       /* requests core 1 read wrong and refused (written by core 1 only) */
static volatile uint32_t fm1_c1_misread;        /* the last refused sequence, as core 1 read it */
static void (*volatile fm1_c1_fn)(uint32_t) = fm1_c1_idle;

/* core 0: hand over a request (the next sequence number, published last) */
static inline void fm1_c1_post(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t seq = fm1_c1_job + 1u;
    fm1_c1_fn = fn;
    fm1_c1_arg = arg;
    fm1_c1_chk = FM1_C1_CHECK(fn, arg, seq);
    FM1_C1_SYNC();
    fm1_c1_job = seq;
    FM1_C1_SYNC();
}

/* core 1: one look at the mailbox. *done: the last sequence it finished (a register, never re-read from SRAM).
 * 1 = it ran a job, 0 = nothing to do, -1 = a request refused (counted, not run) */
static inline __attribute__((always_inline)) int fm1_c1_poll(uint32_t *done)
{
    uint32_t j = fm1_c1_job;
    FM1_C1_SYNC();
    if (j == *done)
        return 0;
    if (j == *done + 1u) {
        void (*fn)(uint32_t) = fm1_c1_fn;
        uint32_t arg = fm1_c1_arg, chk = fm1_c1_chk;
        if (fn && chk == FM1_C1_CHECK(fn, arg, j)) {
            fn(arg);                            /* the work itself runs from the flash */
            FM1_C1_SYNC();
            *done = j;
            fm1_c1_done = j;
            FM1_C1_SYNC();
            return 1;
        }
    }
    fm1_c1_misread = j;
    fm1_c1_rejected = fm1_c1_rejected + 1u;
    FM1_C1_SYNC();
    return -1;
}

/* DBG_MSG's sources (SDK cpu/wl82/debug.c debug_msg[], as Melodee PR 21) by core: the MMU read/write, the PC and
 * write limits, the bus-invalid fetch/read/write; and the other masters' (FM1_DBG_PRP): the watchdog (0), the
 * peripherals' MMU and write limit (1..3), an AXI master's invalid read / write (4, 5: a DMA, the USB, the DAC), the
 * DMC and DBG pendings (14, 15) */
#define FM1_DBG_C1 ((1u << 6) | (1u << 8) | (1u << 10) | (1u << 11) | (7u << 16))
#define FM1_DBG_C0 ((1u << 7) | (1u << 9) | (1u << 12) | (1u << 13) | (7u << 19))
#define FM1_DBG_PRP (0x3Fu | (3u << 14))
/* a crash of core 0's own beside a running core 1 bars it (docs/DUALCORE.md "The persistent fallback"): the misreads
 * the reports describe come as core 0's debug sources (fc79's records: DBG_MSG 0x1000 the PC limit, 0x200000 a bus
 * read) or as a plain CPU exception with no debug source at all (EMU_MSG says what: an illegal instruction read from
 * RAM). A fault that only another master raised is not core 0 misreading: seen on the owner's unit, axi_rd_inv (bit 4)
 * in the restart's soft reset itself (fm1_reboot's P33 write, uptime 37.6 s: a DMA still reading as the unit leaves),
 * which barred Dual Core for nothing. The crash report either way; the bar only for core 0's kinds */
static inline int fm1_c1_crash_bars(uint32_t dbg)
{
    return (dbg & FM1_DBG_C0) || !(dbg & FM1_DBG_PRP);
}

#ifndef FM1_CPU1_LOGIC_ONLY
#define FM1_HAVE_CPU1 1                 /* firmware/src/cr_cpu1.h: the real core, not the host's inline stub */
#include "fm1_power.h"              /* the core rails (cr_cpu1.h: up before core 1 starts) */

#define FM1_C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define FM1_C1_CLK (*(volatile uint32_t *)0x00010008u)
#define FM1_C1_T4 (*(volatile uint32_t *)0x10804u)       /* TIMER4, 24 MHz */

extern void fm1_c1_entry(void);
extern uint32_t _c1_ustack[];
#define FM1_C1_STACK_WORDS (2816u / 4u)
#define FM1_C1_MARK 0x43314D4Bu                  /* "C1MK": how deep core 1's stack has gone */
volatile uint32_t fm1_c1_trace;                 /* breadcrumbs (fm1_cpu1.S, fm1_c1_main) */
static uint8_t fm1_c1_on;                       /* started and answering: core 0 may hand it work */
/* faults about core 1 (fm1_c1_fault_c): count, the echoes of one core 1 took itself, which core took it, DBG_MSG,
 * the faulting core's EMU_MSG, its pc and rets */
static struct { volatile uint32_t count; uint32_t echoes, cpu, dbg, emu, pc, rets; } fm1_c1_fault;
static volatile uint32_t fm1_c1_stalls;         /* jobs not done in 8 ms */
/* the bar (docs/DUALCORE.md "Reliability"; ChoralRoot#9, fm1-x0x#21): survives a warm reset (.noinit, as fm1_crash in
 * fm1_irq.h), read by cr_cpu1.h cr_c1_bar_boot at the next boot. ran: core 1 was started and answered in this boot;
 * barred: why core 1 must not start at the next boot (0 none, else cr_cpu1.h's C1_BAR_*: 1 crash, 2 hang, 3 gave up) */
#define FM1_C1_BAR_MAGIC 0x43314241u            /* "C1BA" */
struct fm1_c1_bar { uint32_t magic, ran, barred; };
struct fm1_c1_bar fm1_c1_bar __attribute__((section(".noinit")));

void fm1_c1_main(void);
void FM1_C1_TEXT __attribute__((noreturn, used)) fm1_c1_main(void)
{
    uint32_t done = fm1_c1_done;
    fm1_c1_trace = 0xC1000002;
    fm1_c1_alive = 1;
    FM1_C1_SYNC();
    for (;;)
        (void)fm1_c1_poll(&done);
}

static void fm1_cpu1_hold(void)
{
    FM1_C1_CON |= 0x2u;
    FM1_C1_CON &= ~0x8u;
    FM1_C1_SYNC();
    fm1_c1_on = 0;
}

/* main.c fm1_cstart, first thing at every reset, before the .bss / .pool / RAM-text copies: a core 1 the reset did
 * not hold would read the RAM being cleared (Jangada, fm1-x0x#10; fm1-x0x does the same). Registers only */
static void fm1_cpu1_halt(void) { fm1_cpu1_hold(); }

/* core 1 must not start at the next boot: why (cr_cpu1.h C1_BAR_*) */
static void fm1_cpu1_bar(uint32_t why)
{
    fm1_c1_bar.magic = FM1_C1_BAR_MAGIC;
    fm1_c1_bar.barred = why;
}

/* 0 = core 1 runs and answered; else it is held again. The top of RAM must be writable
 * (fm1_guard_unlock_top) and the PC limits open (fm1_guard_pc_open): the caller's */
static int fm1_cpu1_start(void)
{
    uint32_t saved, t0, i;
    if (fm1_c1_on)
        return 0;
    fm1_cpu1_hold();
    for (i = 0; i < FM1_C1_STACK_WORDS; i++)
        _c1_ustack[i] = FM1_C1_MARK;
    fm1_c1_alive = 0;
    fm1_c1_trace = 0;
    fm1_c1_fn = fm1_c1_idle;
    fm1_c1_done = fm1_c1_job;
    fm1_c1_rejected = 0;
    for (i = 0; i < 32u; i++)                   /* its interrupt bank: all off (core 0's is at 0x1EEF100) */
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)&fm1_c1_entry;
    saved = FM1_C1_CLK;
    FM1_C1_CLK = saved | 0x8u;
    FM1_C1_SYNC();
    FM1_C1_CON |= 0x8u;
    FM1_C1_CON &= ~0x2u;
    t0 = FM1_C1_T4;
    while (!fm1_c1_alive && FM1_C1_T4 - t0 < 24000u * 20u)   /* 20 ms */
        FM1_C1_SYNC();
    FM1_C1_CLK = saved;
    if (!fm1_c1_alive) {
        fm1_cpu1_hold();
        return -1;
    }
    fm1_c1_bar.magic = FM1_C1_BAR_MAGIC;        /* (a crash or a hang from here on is one beside core 1) */
    fm1_c1_bar.ran = 1;
    fm1_c1_on = 1;
    return 0;
}

/* hand core 1 a job: 1 = it took it (fm1_cpu1_wait before touching what it works on) */
static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1_c1_on || fm1_c1_fault.count)
        return 0;
    fm1_c1_post(fn, arg);
    return 1;
}

/* 0 = done; -1 = it did not finish in 8 ms, -2 = core 1 faulted: core 1 is held from now on (the caller does the
 * work: a job partly done is never rerun on its state, the caller renders the block from what core 0 owns) */
static inline int fm1_cpu1_wait(void)
{
    uint32_t t0 = FM1_C1_T4;
    for (;;) {
        FM1_C1_SYNC();
        if (fm1_c1_done == fm1_c1_job)
            return 0;
        if (fm1_c1_fault.count) {
            fm1_cpu1_hold();
            return -2;
        }
        if (FM1_C1_T4 - t0 > 24000u * 8u) {     /* 8 ms: far past any stretch */
            fm1_cpu1_hold();
            fm1_c1_stalls++;
            return -1;
        }
    }
}

static inline uint32_t fm1_cnum(void) { uint32_t v; __asm__ volatile("%0 = cnum" : "=r"(v)); return v; }

/* 1: a fault about core 1, handled (core 0 resumes); 0: core 0's own (the crash report). After Melodee PR 21's
 * fm1_core1_fault_c. Core 1 enables no interrupt, so the debug unit's errors about it (a fetch outside the PC limits,
 * a bus error) raise the exception on core 0, whose own state is intact: before, the crash report reset the unit
 * for them. Core 1 can also take the exception itself (Melodee's idle unit: pc 00000002, core 1 calling a null job):
 * it records it and holds itself. f: fm1_fatal_common's frame (fm1_irq.h fm1_fault_c) */
static int fm1_c1_fault_take(const uint32_t *f)
{
    uint32_t cpu = fm1_cnum(), dbg = FM1_DBG_MSG, was;
    if (!cpu && (dbg & FM1_DBG_C0))
        return 0;                                   /* core 0's own */
    if (!cpu && !(dbg & FM1_DBG_C1)) {              /* core 1 took it too and cleared DBG_MSG first: */
        if (fm1_c1_fault.echoes >= fm1_c1_fault.count)
            return 0;                               /* (once per core 1 fault) else core 0's own */
        fm1_c1_fault.echoes++;
        return 1;
    }
    if (!cpu && fm1_c1_fault.count >= 16u)
        return 0;                                   /* a hold that did not take: the crash report */
    if (!cpu)
        fm1_cpu1_hold();
    fm1_c1_fault.cpu = cpu;
    fm1_c1_fault.dbg = dbg;
    fm1_c1_fault.emu = *(volatile uint32_t *)(cpu ? 0x1EEF2D4u : 0x1EEF0D4u);   /* the faulting core's EMU_MSG */
    fm1_c1_fault.pc = f[16];
    fm1_c1_fault.rets = f[25];
    fm1_c1_fault.count++;                           /* (before DBG_MSG clears: see the echo above) */
    FM1_C1_SYNC();
    was = FM1_DBG_WR_EN & 1u;                       /* (writing 0xE7 toggles the unlock) */
    if (!was)
        FM1_DBG_WR_EN = 0xE7u;
    FM1_DBG_MSG_CLR = dbg;
    if (!was)
        FM1_DBG_WR_EN = 0xE7u;
    if (cpu) {                                      /* core 1 itself: hold, and stay here until held */
        FM1_C1_CON |= 0x2u;
        FM1_C1_CON &= ~0x8u;
        for (;;)
            ;
    }
    return 1;
}

/* test builds (cr_backup.c CORE1_TEST, IRQs off): what an affected unit's core 1 reads, for 1 ms: the sequence as
 * +0x00200000 (null_job 0), or a null fn at the next sequence (null_job 1: the idle crash Melodee decoded); the check
 * word stays the last job's, so either is refused. And a job that never returns, one that calls address 0 */
static void fm1_c1_test_misread(int null_job)
{
    uint32_t j = fm1_c1_job, t0 = FM1_C1_T4;
    void (*fn)(uint32_t) = fm1_c1_fn;
    if (null_job)
        fm1_c1_fn = 0;
    fm1_c1_job = null_job ? j + 1u : j + 0x00200000u;
    FM1_C1_SYNC();
    while (FM1_C1_T4 - t0 < 24000u)
        ;
    fm1_c1_job = j;
    FM1_C1_SYNC();
    fm1_c1_fn = fn;
    FM1_C1_SYNC();
}
static void fm1_c1_test_stall(uint32_t u) { (void)u; for (;;) FM1_C1_SYNC(); }
static void fm1_c1_test_fault(uint32_t u) { void (*volatile z)(uint32_t) = 0; z(u); }

/* from fm1_fatal_common (fm1_vec.S): returns only to resume core 0 after a fault about core 1 */
void fm1_c1_fault_c(uint32_t *f);
void fm1_c1_fault_c(uint32_t *f)
{
    if (!fm1_c1_fault_take(f)) {
        if (fm1_c1_on) {                            /* beside a running core 1: held before the report */
            fm1_cpu1_hold();
            if (fm1_c1_crash_bars(FM1_DBG_MSG))     /* core 0's own kinds: the misread the reports describe
                                                     * (ChoralRoot#9, fm1-x0x#21); the next boot keeps core 1 off and
                                                     * saves Dual Core Off (cr_cpu1.h cr_c1_bar_boot). Another master's
                                                     * fault (fm1_c1_crash_bars): the report alone */
                fm1_cpu1_bar(1u /* C1_BAR_CRASH */);
        }
        fm1_fault_c(f);                             /* the crash report: never returns */
    }
}
#endif
