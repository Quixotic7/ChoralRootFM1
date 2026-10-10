/* SPDX-License-Identifier: GPL-3.0-only */
/* ChoralRoot on the FM-1's second core (docs/DUALCORE.md), and the audio ISR's stage profile (cr_dbg).
 * The core's bring-up is hal/fm1_cpu1.{h,S} (after fm1-x0x (GPL-3.0) and Melodee (GPL-3.0), following JieLi's AC79
 * SDK EnableOtherCpu (Apache-2.0)); what runs there, the budgets, the meter and the fail-safes are ChoralRoot's:
 *
 *   power-on (main.c cr_c1_boot, before audio_init; not in SAFE MODE): core 1 started, 20 ms to answer, else held
 *     for the session: cr_dbg.c1 = C1_FAILED, single core, the shared voice budget;
 *   each audio block (fx.c mix_block, cr_c1_split): core 0 does everything that touches shared state first (the
 *     events, part 1's voice allocation and stealing, both parts' matrix and LFO), hands part 1's render to core 1
 *     (one job: track_render of trk[1] into its own buffer), renders part 0, waits, then mixes every part in the
 *     order of one core, so the samples are one core's bit for bit;
 *   a job not done in 8 ms (fm1_cpu1_wait): core 1 held for the session, core 0 renders part 1 itself,
 *     cr_dbg.c1 = C1_GAVE_UP, the shared budget again.
 * While core 1 is alive (C1_OK) each of parts 0 and 1 has its own budget of VBUDGET units (voice.c voices_busy).
 * CR_CPU1 0 (felucca.c, tests/hostsim.c, tests/regress.c): none of this, the code as before. The emulator builds it
 * with 1 and a stub core that runs the job at once, when handed (before part 0: the split must not depend on the
 * order of the two renders). */
#pragma once
#include <stdint.h>
#ifndef CR_CPU1
#define CR_CPU1 0
#endif

enum { C1_OFF, C1_OK, C1_FAILED, C1_GAVE_UP };
/* the audio ISR's stages: the engine tick (cr_out.c: the engine, the looper, the events, the voices' allocation),
 * part 0's render, part 1's (core 1's job time when it renders it), the rest of the mix (the parts' dist, level,
 * sends, parts 2..3, the FX buses), the master (limiter, the click, the USB tap), core 0's wait for core 1 */
enum { CRP_TICK, CRP_PART0, CRP_PART1, CRP_FX, CRP_MASTER, CRP_WAIT, CRP_N };
typedef struct {
    uint8_t c1;                      /* C1_*: off (CR_CPU1 0, SAFE MODE, Options > Dual Core Off), ok, failed at start, gave up on a job */
    uint8_t win_reset;               /* audio.c cpu_window took the window: the ISR starts a new one */
    uint8_t prof_on;                 /* the stage counters run (device: always; emulator: EMU_STAGES=1) */
    uint32_t c1_jobs;                /* jobs core 1 rendered */
    uint32_t c1_stack;               /* bytes of core 1's stack used at most (its "C1MK" fill, cr_c1_stack_used) */
    uint32_t c1_start_us;            /* how long core 1 took to answer at power-on */
    uint32_t mark;                   /* the last stage mark (prof units) */
    uint32_t cur[CRP_N];             /* this half buffer's stages (prof units, its blocks summed) */
    uint32_t win_sum[CRP_N], win_max[CRP_N], win_n;   /* the window being filled */
    uint32_t avg_us[CRP_N], max_us[CRP_N];             /* the last full second (console `cpu`, GEEK OUT) */
    uint64_t tot_sum[CRP_N];         /* since power-on (the emulator's summary, tools/emu/perf.sh) */
    uint32_t tot_max[CRP_N], tot_n;
} cr_dbg_t;
static cr_dbg_t cr_dbg;
/* Options > Dual Core Off (settings v11, read by cr_settings_boot before cr_c1_boot): core 1 is never started this
 * power-on, cr_dbg.c1 stays C1_OFF ("c1 off"), the shared budget. A change in Options waits for the next power-on */
static uint8_t cr_c1_off;
static const char *const CRP_NAME[CRP_N] = {"tick", "part0", "part1", "fx", "master", "wait"};

#if CR_CPU1
/* ------------------------------------------------ the platform: the core and the clock --- */
#ifdef FM1_HAVE_CPU1                 /* the device (hal/fm1_cpu1.h, included before core.h by choralroot.c) */
#define CR_PROF_PER_US FM1_TICKS_PER_US   /* TIMER4 ticks (24 MHz) */
static inline uint32_t cr_prof_now(void) { return fm1_ticks(); }
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
static int cr_c1_wait(void) { return fm1_cpu1_wait(); }
static uint32_t cr_c1_stack_used(void)              /* the C1MK fill: the deepest word core 1's usp reached */
{
    uint32_t i = 0;
    while (i < FM1_C1_STACK_WORDS && _c1_ustack[i] == FM1_C1_MARK)
        i++;
    return (FM1_C1_STACK_WORDS - i) * 4u;
}
#else                                /* the host (the emulator, the tests): the job runs when handed */
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#define CR_PROF_PER_US 259u          /* host instructions per device us (tools/emu/emu.c DEV_INSTR_PER_US) */
static uint8_t cr_c1_test_hang;      /* tests: the next jobs never answer (cr_c1_wait times out) */
static uint8_t cr_c1_test_nostart;   /* tests: core 1 does not answer at start */
static uint8_t cr_c1_test_late;      /* tests: the job runs at the wait (after part 0), not when handed (before it) */
static void (*cr_c1_fn)(uint32_t);
static uint32_t cr_c1_arg;
static inline uint32_t cr_prof_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return (uint32_t)ri.ri_instructions;
#endif
    return 0;
}
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    cr_c1_fn = fn;
    cr_c1_arg = arg;
    if (!cr_c1_test_hang && !cr_c1_test_late)
        fn(arg);
    return 1;
}
static int cr_c1_wait(void)
{
    if (cr_c1_test_hang)
        return -1;
    if (cr_c1_test_late)
        cr_c1_fn(cr_c1_arg);
    return 0;
}
static uint32_t cr_c1_stack_used(void) { return 0; }
#endif

/* a stage's time without the reading of the clock (the host's counter costs ~6.6 k instructions a reading: measured
 * once, cr_prof_calibrate; the device's timer read costs nothing worth it) */
static uint32_t cr_prof_cost;
static inline uint32_t cr_prof_less(uint32_t d) { return d > cr_prof_cost ? d - cr_prof_cost : 0u; }
static void cr_prof_calibrate(void)
{
    uint32_t i, a, b, best = 0xFFFFFFFFu;
    for (i = 0; i < 16u; i++) {
        a = cr_prof_now();
        b = cr_prof_now();
        if (b - a < best)
            best = b - a;
    }
    cr_prof_cost = best == 0xFFFFFFFFu ? 0u : best;
}
static inline int cr_dualcore_active(void) { return cr_dbg.c1 == C1_OK; }
/* this block: part 1 renders as core 1's job (its own scratch where an engine has shared scratch: fm6_core.c) */
static volatile uint8_t cr_c1_split;
#define CR_C1_IDX(t) (cr_c1_split && (t) == &trk[1])
#define CR_PROF_START() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)
#define CR_PROF_SKIP() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)   /* (not counted) */
#define CR_PROF(k) do { if (cr_dbg.prof_on) { uint32_t t_ = cr_prof_now(); cr_dbg.cur[k] += cr_prof_less(t_ - cr_dbg.mark); \
                                              cr_dbg.mark = t_; } } while (0)

/* power-on (main.c fm1_main, before audio_init and the timers; not in SAFE MODE): start core 1. It boots through the
 * chip's ROM, which the PC limits refuse, and its entry word is in the top of RAM: both open while it starts.
 * Not answering in 20 ms: held for the session (C1_FAILED), single core, the shared budget */
static void cr_c1_boot(void)
{
    int rc;
    if (cr_c1_off) {                                    /* Options > Dual Core Off: one core this session */
        cr_dbg.c1 = C1_OFF;
        return;
    }
#ifdef FM1_HAVE_CPU1
    {
    uint32_t t0 = fm1_ticks();
    fm1_guard_unlock_top();                             /* (fm1_main locks it after the IRQs are attached) */
    fm1_guard_pc_open();
    rc = fm1_cpu1_start();
    fm1_guard_enable(FM1_GUARD_PC);
    cr_dbg.c1_start_us = (fm1_ticks() - t0) / CR_PROF_PER_US;
    }
#else
    rc = cr_c1_test_nostart ? -1 : 0;
#endif
    cr_dbg.c1 = rc ? C1_FAILED : C1_OK;
}

/* end of a half buffer (audio.c): the stages into the window and the totals */
static void cr_prof_half(void)
{
    uint32_t k;
    if (!cr_dbg.prof_on)
        return;
    if (cr_dbg.win_reset) {
        for (k = 0; k < CRP_N; k++)
            cr_dbg.win_sum[k] = cr_dbg.win_max[k] = 0;
        cr_dbg.win_n = 0;
        cr_dbg.win_reset = 0;
    }
    for (k = 0; k < CRP_N; k++) {
        uint32_t c = cr_dbg.cur[k];
        cr_dbg.win_sum[k] += c;
        if (c > cr_dbg.win_max[k])
            cr_dbg.win_max[k] = c;
        cr_dbg.tot_sum[k] += c;
        if (c > cr_dbg.tot_max[k])
            cr_dbg.tot_max[k] = c;
        cr_dbg.cur[k] = 0;
    }
    cr_dbg.win_n++;
    cr_dbg.tot_n++;
}
/* the main loop, once a second (audio.c cpu_window): the window closed into avg_us / max_us */
static void cr_prof_window(void)
{
    uint32_t k;
    if (cr_dbg.win_reset || !cr_dbg.win_n)
        return;
    for (k = 0; k < CRP_N; k++) {
        cr_dbg.avg_us[k] = cr_dbg.win_sum[k] / cr_dbg.win_n / CR_PROF_PER_US;
        cr_dbg.max_us[k] = cr_dbg.win_max[k] / CR_PROF_PER_US;
    }
    cr_dbg.win_reset = 1;
}
#else
#define cr_dualcore_active() 0
#define CR_C1_IDX(t) 0
#define CR_PROF_START() ((void)0)
#define CR_PROF(k) ((void)0)
#define CR_PROF_SKIP() ((void)0)
#define cr_prof_half() ((void)0)
#define cr_prof_window() ((void)0)
#define cr_c1_boot() ((void)0)
#define cr_c1_stack_used() 0u
#endif

/* GEEK OUT's / the console's word for core 1 */
static const char *cr_c1_name(void)
{
    static const char *const N[4] = {"off", "ok", "failed", "gave up"};
    return N[cr_dbg.c1 & 3u];
}
