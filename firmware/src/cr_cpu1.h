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
 * Reliability (docs/DUALCORE.md, after Melodee PR 21 "fix(dual-core): raise the core supply and survive CPU1
 * misreads, faults and stalls", Kerem Kilic / Melodee, GPL-3.0): with Dual Core On the core rails go up at power-on
 * (hal/fm1_power.h: SYSVDD 14 = 1.35 V, VDC14 5 = 1.50 V) before core 1 starts; core 1 runs only a request it read
 * right (hal/fm1_cpu1.h fm1_c1_poll; a misread is counted, never run); a fault about core 1 holds it and core 0 goes
 * on (fm1_c1_fault_c). The escalation (cr_c1_assess): a first misread is counted and core 1 keeps working; a second
 * misread (a later block that sees new ones), or any fault or stall: C1_GAVE_UP, cr_dbg.c1_fallback = 1. The rails
 * are never raised past 14 / 5 (device safety: hal/fm1_power.h has the reports' evidence for VDC14 5).
 * The persistent fallback (ChoralRoot#9, fm1-x0x#21: some units' misreads hit core 0 too, which none of the above
 * catches): a crash on core 0 beside a running core 1, a watchdog reset while core 1 ran, or a give-up leave a .noinit
 * record (hal/fm1_cpu1.h fm1_c1_bar); the next boot keeps core 1 off and saves Dual Core Off (cr_c1_bar_boot,
 * cr_settings.c), and the player is told (cr_c1_barred: the message after the splash, Options, GEEK OUT).
 * While core 1 is alive (C1_OK) each of parts 0 and 1 has its own budget of VBUDGET units (voice.c voices_busy).
 * CR_CPU1 0 (felucca.c, tests/hostsim.c, tests/regress.c): none of this, the code as before. The emulator builds it
 * with 1 and a stub core that runs the job at once, when handed (before part 0: the split must not depend on the
 * order of the two renders). */
#pragma once
#include <stdint.h>
#include "cr_bootguard.h"                /* (BG_RST_*: cr_c1_bar_decide) */
#ifndef CR_CPU1
#define CR_CPU1 0
#endif
#ifndef CR_SINGLE_CORE
#define CR_SINGLE_CORE 0                 /* 1: the single-core package (cr_ui.h): cr_c1_boot never starts core 1,
                                          * whatever the settings say; everything runs on the one-core rules */
#endif
#ifndef CR_CORE1_TEST
#define CR_CORE1_TEST 0                  /* 1: SysEx 75 CORE1_TEST injects core 1 failures (tools/core1_fault_test.py,
                                          * cr_backup.c); a test build only, never a release */
#endif

enum { C1_OFF, C1_OK, C1_FAILED, C1_GAVE_UP };
/* the audio ISR's stages: the engine tick (cr_out.c: the engine, the looper, the events, the voices' allocation),
 * part 0's render, part 1's (core 1's job time when it renders it), the rest of the mix (the parts' dist, level,
 * sends, parts 2..3, the FX buses), the master (limiter, the click, the USB tap), core 0's wait for core 1 */
enum { CRP_TICK, CRP_PART0, CRP_PART1, CRP_FX, CRP_MASTER, CRP_WAIT, CRP_N };
/* inside the fx stage (cr_fx.c xf_block): the sends' down-sampler, the delay's loop, the reverb's, the wet's
 * up-sampler */
enum { CRX_DOWN, CRX_DELAY, CRX_REVERB, CRX_UP, CRX_N };
typedef struct {
    uint8_t c1;                      /* C1_*: off (CR_CPU1 0, SAFE MODE, Options > Dual Core Off), ok, failed at start, gave up on a job */
    uint8_t win_reset;               /* audio.c cpu_window took the window: the ISR starts a new one */
    uint8_t prof_on;                 /* the stage counters run (device: always; emulator: EMU_STAGES=1) */
    uint32_t c1_jobs;                /* jobs core 1 rendered */
    uint32_t c1_stack;               /* bytes of core 1's stack used at most (its "C1MK" fill, cr_c1_stack_used) */
    uint32_t c1_start_us;            /* how long core 1 took to answer at power-on */
    uint8_t c1_fallback;             /* 1: core 1 was given up this session (a second misread, a fault, a stall); RAM only */
    uint8_t c1_misreads;             /* audio blocks that found new misreads (cr_c1_assess): the first is forgiven */
    uint8_t rail_boot[2], rail_now[2];   /* SYSVDD, VDC14 levels: the boot loader's, now (hal/fm1_power.h; host 11, 3) */
    uint32_t mark;                   /* the last stage mark (prof units) */
    uint32_t cur[CRP_N];             /* this half buffer's stages (prof units, its blocks summed) */
    uint32_t win_sum[CRP_N], win_max[CRP_N], win_n;   /* the window being filled */
    uint32_t avg_us[CRP_N], max_us[CRP_N];             /* the last full second (console `cpu`, GEEK OUT) */
    uint64_t tot_sum[CRP_N];         /* since power-on (the emulator's summary, tools/emu/perf.sh) */
    uint32_t tot_max[CRP_N], tot_n;
    /* THE WORST CASES (0.18, docs/FX.md "What the unit says"): since the last 73 DEBUG read (cr_backup.c crb_debug
     * clears them: play, read, and the numbers are that stretch's) and, for the emulator, since power-on (x_tot_*) */
    uint32_t pk[CRP_N];              /* each stage's largest half (prof units) */
    uint32_t pk_all, pk_n;           /* the largest whole half (us: audio.c's measure, TIMER5 nested in it); halves */
    uint32_t last[CRP_N];            /* the half just rendered */
    uint32_t late_n, late_stage, late_us;   /* late halves; the latest one's largest stage (CRP_* + 1) and its us */
    uint32_t xmark, xcur[CRX_N];     /* the fx stage's parts: this half's */
    uint32_t x_pk[CRX_N], x_busy;    /* .. their largest half, and the halves in which any of them ran */
    uint64_t x_sum[CRX_N];           /* .. summed over those halves */
    uint64_t x_tot_sum[CRX_N];
    uint32_t x_tot_max[CRX_N], x_tot_busy;
    /* WHEN THE ISR GOT TO START (0.18's third build, docs/FX.md "Late is not slow"): a half is late when it ends
     * after its boundary, and that is its render plus what kept the ISR from starting. All in TIMER4 ticks */
    uint32_t lat_on, lat_tb, lat_min, lat_n;   /* the boundary estimate (cr_prof_entry) */
    uint32_t lat;                    /* this half's lag behind its boundary */
    uint32_t pk_lat, pk_wall, pk_t5; /* the largest lag, whole half (TIMER5 nested in it) and TIMER5 nested in one half */
    uint32_t stale;                  /* halves in which the DAC played something old: a boundary nobody served (the
                                      * ISR more than a half behind) or a half finished after the DAC had reached
                                      * its last block. THE AUDIBLE ONES; a half that is merely late is not */
    uint32_t pk_t5gap_ms;                  /* 3y48: fm1_ms when the largest gap ended (cleared with it) */
    uint32_t t5_last, pk_t5gap, pk_t5pc;   /* TIMER5's largest gap between two entries (100 us when nothing masks
                                      * the interrupts) and where it came back to after it (main.c) */
    /* WHAT THE OVERLOAD SHED DID since the last 78 PEAK read (3y44; audio.c shed_check / shed_voice, cr_backup.c
     * crb_peak): the halves above 85 %, the time of the half that asked for the latest shed, the latest voice shed
     * (0 none, else 0x10000 | part << 12 | voice << 8 | note) and why (1 released, 2 held, 3 none to give); and the
     * counts at the last read of audio.c's shed_count and voice.c's voice_kills / voice_steals */
    uint32_t shed_over_n, shed_us, shed_last, shed_why, shed0, kill0, steal0;
    /* P5 ON TWO CORES since the last 78 PEAK read (3y46; fx.c p5x_run, voice.c p5_pool_*): the most P5 voices core 0
     * and core 1 rendered in one block (core 1's: the bass part's P5 voices and its share of the chord part's), the
     * most P5 voices of the bass part, and voice.c's p5_bass_gave at the last read */
    uint32_t p5_c0, p5_c1, p5_bass, gave0;
    /* 3y50, EACH CORE'S HALF (78 PEAK's values from 55; fx.c c1_part1_job, c1_fx_job, mix_fx_split): this half's
     * time core 1 was busy (its voices' job and the reverb's), of it the voices' job (that is in CRP_PART1 too, and
     * not core 0's), core 1 idle between the two jobs, core 0's wait at the effects' join (in CRP_WAIT too), and
     * which core ran which effect (fxmap: 1 the chorus, 2 the delay, 4 the reverb ran, 8 the reverb on core 1);
     * c1_end the moment core 1's voices were done (0: not this block). Their peaks since the last read: core 0's
     * busy time (the half's stages but CRP_WAIT, without core 1's job), core 1's, core 0's waits (both joins), the
     * effects' join alone, core 1's idle stretch, and the map of the largest whole half (pk_wall's); the reverb jobs
     * core 1 ran */
    uint32_t c1_busy_cur, c1v_cur, w1_cur, fxw_cur, fxmap_cur, fxmap_last, c1_end;
    uint32_t pk_c0_busy, pk_c1_busy, pk_w0, pk_fxw, pk_w1, pk_fxmap, c1x_jobs;
    /* core 1's busy time (c1_busy_cur: its voices' job and the reverb's) over the window the stages use, and the last
     * full second's average and largest half in us: GEEK OUT 2's CORE 1 (cr_ui.c cu_geek2) */
    uint32_t c1w_sum, c1w_max, c1_avg_us, c1_max_us;
} cr_dbg_t;
static cr_dbg_t cr_dbg;
/* Options > Dual Core Off (settings v12; On is the default of a unit with no record since 0.22, Off was from 0.16;
 * read by cr_settings_boot before cr_c1_boot): core 1 is never started this power-on, cr_dbg.c1 stays C1_OFF
 * ("c1 off"), the shared budget. A change in Options waits for the next power-on. 1 until the record says On: a
 * build without the settings (or a boot that never reads them) is one core */
static uint8_t cr_c1_off = 1;
/* why this boot kept core 1 off although the record said On (cr_c1_bar_boot), or gave it up this session (cr_ui.c);
 * C1_BAR_NONE otherwise. The UI reads it (the message after the splash, Options > Dual Core, GEEK OUT) */
enum { C1_BAR_NONE, C1_BAR_CRASH, C1_BAR_HANG, C1_BAR_GAVE_UP };
static uint8_t cr_c1_barred;
#ifndef FM1_C1_BAR_MAGIC
#define FM1_C1_BAR_MAGIC 0x43314241u     /* "C1BA": hal/fm1_cpu1.h's record (the host's mock below) */
#endif
static const char *const CRP_NAME[CRP_N] = {"tick", "part0", "part1", "fx", "master", "wait"};
static const char *const CRX_NAME[CRX_N] = {"down", "delay", "reverb", "up"};

#if CR_CPU1
/* ------------------------------------------------ the platform: the core and the clock --- */
#ifdef FM1_HAVE_CPU1                 /* the device (hal/fm1_cpu1.h, included before core.h by choralroot.c) */
#define CR_PROF_PER_US FM1_TICKS_PER_US   /* TIMER4 ticks (24 MHz) */
static inline uint32_t cr_prof_now(void) { return fm1_ticks(); }
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
static int cr_c1_wait_core(void) { return fm1_cpu1_wait(); }
#define CR_C1_SELF() (fm1_cnum() != 0u)      /* 1: this code runs on core 1 (the cnum register, hal/fm1_cpu1.h) */
static void cr_c1_hold(void) { fm1_cpu1_hold(); }
static inline uint32_t cr_c1_rejected(void) { return fm1_c1_rejected; }
static inline uint32_t cr_c1_faults(void) { return fm1_c1_fault.count; }
static struct fm1_c1_bar *cr_c1_bar_rec(void) { return &fm1_c1_bar; }   /* the bar (.noinit) */
static void cr_c1_bar_set(uint32_t why) { fm1_cpu1_bar(why); }
#if CR_CORE1_TEST
static uint8_t cr_c1_injected;       /* test builds: 75 CORE1_TEST injected a failure this session (cr_backup.c): a give-up
                                      * is then not the unit's, cr_ui.c does not save Dual Core Off for it */
#define CR_C1_INJECTED() (cr_c1_injected != 0)
#endif
static void cr_rails_record(void)                   /* the boot loader's levels (P33 reads only) */
{
    fm1_power_record();
    cr_dbg.rail_boot[0] = cr_dbg.rail_now[0] = fm1_power.boot[FM1_RAIL_SYSVDD];   /* (now = boot until a raise:
                                                                                    * Dual Core Off read 0 before) */
    cr_dbg.rail_boot[1] = cr_dbg.rail_now[1] = fm1_power.boot[FM1_RAIL_VDC14];
}
static void cr_rails_now(void)
{
    cr_dbg.rail_now[0] = fm1_power.now[FM1_RAIL_SYSVDD];
    cr_dbg.rail_now[1] = fm1_power.now[FM1_RAIL_VDC14];
}
static void cr_rails_init(void) { fm1_power_init(); cr_rails_now(); }       /* IRQs off: power-on */
#define CR_C1_TEST_NOSPLIT 0
#define CR_C1_TEST_SHARE 0
#define CR_C1_TEST_FX0 0
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
static uint32_t cr_c1_test_rejected; /* tests: core 1's misread count (the device's fm1_c1_rejected) */
static uint8_t cr_c1_test_nosplit;   /* tests (3y46, 3y177): the chord voices of P5 or the VA are not shared out: all
                                      * in track_render_x, in their order, on core 0 (the reference the split's sum
                                      * must equal) */
static uint8_t cr_c1_test_share;     /* tests: k + 1: core 1 takes k of the chord part's voices (all there are, if
                                      * fewer), whatever the balance says: any split gives the same samples */
#define CR_C1_TEST_NOSPLIT cr_c1_test_nosplit
#define CR_C1_TEST_SHARE cr_c1_test_share
static uint8_t cr_c1_test_fx0;       /* tests (3y50): the effects all on core 0, in xf_block's order (the reference the
                                      * reverb on core 1 must equal) */
#define CR_C1_TEST_FX0 cr_c1_test_fx0
static uint32_t cr_c1_test_faults;   /* tests: faults about core 1 (the device's fm1_c1_fault.count) */
static void (*cr_c1_fn)(uint32_t);
static uint32_t cr_c1_arg;
struct fm1_c1_bar { uint32_t magic, ran, barred; };
static struct fm1_c1_bar cr_c1_bar_mock;  /* tests: the device's .noinit bar record (hal/fm1_cpu1.h fm1_c1_bar) */
static struct fm1_c1_bar *cr_c1_bar_rec(void) { return &cr_c1_bar_mock; }
static void cr_c1_bar_set(uint32_t why) { cr_c1_bar_mock.magic = FM1_C1_BAR_MAGIC; cr_c1_bar_mock.barred = why; }
/* THE JOB ON A REAL THREAD (3y45; cr_c1_test_thread: the emulator's EMU_C1=thread, tests/cr_c1_thread_test.c): the
 * host's stub runs core 1's job inline, so no host run could show two cores writing one thing. With this the job
 * runs on a second thread from the hand-over to the wait, at the same time as core 0's part 0, as on the unit. What it
 * is for: the samples must be the inline job's, bit for bit, however the two threads fall; and a build with
 * -fsanitize=thread names every word both sides touch. Not a model of the unit's timing or of its caches */
#if !defined(__EMSCRIPTEN__)
#include <pthread.h>
#define CR_C1_THREAD 1
static uint8_t cr_c1_test_thread;
static pthread_t cr_c1_th;
static pthread_mutex_t cr_c1_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cr_c1_cv = PTHREAD_COND_INITIALIZER;
static uint32_t cr_c1_th_posted, cr_c1_th_done;
static uint8_t cr_c1_th_on;
static _Thread_local uint8_t cr_c1_th_self;        /* 1 on the job's thread: the device's cnum */
#define CR_C1_SELF() (cr_c1_th_self != 0u)
static void *cr_c1_th_main(void *x)
{
    (void)x;
    cr_c1_th_self = 1;
    for (;;) {
        void (*fn)(uint32_t);
        uint32_t arg;
        pthread_mutex_lock(&cr_c1_mx);
        while (cr_c1_th_done == cr_c1_th_posted)
            pthread_cond_wait(&cr_c1_cv, &cr_c1_mx);
        fn = cr_c1_fn;
        arg = cr_c1_arg;
        pthread_mutex_unlock(&cr_c1_mx);
        fn(arg);
        pthread_mutex_lock(&cr_c1_mx);
        cr_c1_th_done++;
        pthread_cond_broadcast(&cr_c1_cv);
        pthread_mutex_unlock(&cr_c1_mx);
    }
    return 0;
}
static int cr_c1_th_run(void (*fn)(uint32_t), uint32_t arg)
{
    pthread_mutex_lock(&cr_c1_mx);
    cr_c1_fn = fn;
    cr_c1_arg = arg;
    cr_c1_th_posted++;
    pthread_cond_broadcast(&cr_c1_cv);
    pthread_mutex_unlock(&cr_c1_mx);
    if (!cr_c1_th_on) {
        cr_c1_th_on = 1;
        if (pthread_create(&cr_c1_th, 0, cr_c1_th_main, 0)) {   /* no thread: inline, as without the switch */
            cr_c1_th_on = 0;
            cr_c1_test_thread = 0;
            pthread_mutex_lock(&cr_c1_mx);
            cr_c1_th_done = cr_c1_th_posted;
            pthread_mutex_unlock(&cr_c1_mx);
            fn(arg);
        }
    }
    return 1;
}
static void cr_c1_th_wait(void)
{
    pthread_mutex_lock(&cr_c1_mx);
    while (cr_c1_th_done != cr_c1_th_posted)
        pthread_cond_wait(&cr_c1_cv, &cr_c1_mx);
    pthread_mutex_unlock(&cr_c1_mx);
}
#else
#define CR_C1_THREAD 0
#define CR_C1_SELF() 0
#endif
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
#if CR_C1_THREAD
    if (cr_c1_test_thread && !cr_c1_test_hang && !cr_c1_test_late)
        return cr_c1_th_run(fn, arg);
#endif
    cr_c1_fn = fn;
    cr_c1_arg = arg;
    if (!cr_c1_test_hang && !cr_c1_test_late)
        fn(arg);
    return 1;
}
static int cr_c1_wait_core(void)
{
    if (cr_c1_test_faults)
        return -2;
    if (cr_c1_test_hang)
        return -1;
#if CR_C1_THREAD
    if (cr_c1_test_thread && !cr_c1_test_late) {
        cr_c1_th_wait();
        return 0;
    }
#endif
    if (cr_c1_test_late)
        cr_c1_fn(cr_c1_arg);
    return 0;
}
static void cr_c1_hold(void) {}
static inline uint32_t cr_c1_rejected(void) { return cr_c1_test_rejected; }
static inline uint32_t cr_c1_faults(void) { return cr_c1_test_faults; }
static uint32_t cr_c1_stack_used(void) { return 0; }
/* the rails on the host: P33 reads the boot loader's defaults (SYSVDD 11, VDC14 3) */
static void cr_rails_record(void) { cr_dbg.rail_boot[0] = cr_dbg.rail_now[0] = 11; cr_dbg.rail_boot[1] = cr_dbg.rail_now[1] = 3; }
static void cr_rails_init(void)
{
    if (cr_dbg.rail_now[0] < 14u) cr_dbg.rail_now[0] = 14;
    if (cr_dbg.rail_now[1] < 5u) cr_dbg.rail_now[1] = 5;
}
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

/* ------------------------------------------------ the escalation (after Melodee PR 21, no boost) --- */
static uint32_t cr_c1_seen;          /* core 1's misread count at the last look */
/* core 0 (the audio ISR): what core 1's counters say. 0 = keep core 1, -1 = give it up. New misreads in a first block:
 * counted, core 1 kept (one block's burst is one event); new misreads in a later block, or any fault or stall: give up */
static int cr_c1_assess(uint32_t rejected, uint32_t faults, int stalled)
{
    if (faults || stalled)
        return -1;
    if (rejected == cr_c1_seen)
        return 0;
    cr_c1_seen = rejected;
    return ++cr_dbg.c1_misreads > 1u ? -1 : 0;
}
static void cr_c1_give_up(void)
{
    cr_c1_hold();
    cr_dbg.c1 = C1_GAVE_UP;
    cr_dbg.c1_fallback = 1;
    cr_c1_bar_set(C1_BAR_GAVE_UP);                      /* (a reset before Dual Core Off is saved: still off next boot) */
}

/* ------------------------------------------------ the bar: core 1 kept off after a crash beside it --- */
/* the next boot's decision on the bar record (hal/fm1_cpu1.h fm1_c1_bar) and this boot's reset class (cr_bootguard.h):
 * not the record (a power-on's garbage) -> none; a reason left (a crash beside core 1, a give-up) -> that; core 1 ran
 * and the watchdog reset the unit -> a hang (never intentional, whatever the uptime); a soft reset without a reason
 * (a reboot, an update) or a power-on -> none */
static uint32_t cr_c1_bar_decide(uint32_t magic, uint32_t ran, uint32_t barred, uint32_t rst_cls)
{
    if (magic != FM1_C1_BAR_MAGIC)
        return C1_BAR_NONE;
    if (barred >= C1_BAR_CRASH && barred <= C1_BAR_GAVE_UP)
        return barred;
    if (ran && rst_cls == BG_RST_WDT)
        return C1_BAR_HANG;
    return C1_BAR_NONE;
}
/* once a boot (cr_settings.c cr_settings_boot, after cr_c1_off is read from the settings, before cr_c1_boot; not in
 * SAFE MODE). rst_cls: bootguard.cls. The record cleared for this boot's `ran`; a bar with the record On: core 1 off
 * this boot, cr_c1_barred says why (cr_settings.c then saves Dual Core Off) */
static void cr_c1_bar_boot(uint32_t rst_cls)
{
    struct fm1_c1_bar *r = cr_c1_bar_rec();
    uint32_t d = cr_c1_bar_decide(r->magic, r->ran, r->barred, rst_cls);
    r->ran = r->barred = 0;
    r->magic = FM1_C1_BAR_MAGIC;
#if CR_SINGLE_CORE
    d = C1_BAR_NONE;                                    /* the single-core package: core 1 never starts, so nothing to
                                                         * bar and no Off to save (a record left by the Standard package
                                                         * is cleared above; its Dual Core stays as that one set it) */
#endif
    if (d != C1_BAR_NONE && !cr_c1_off) {
        cr_c1_off = 1;
        cr_c1_barred = (uint8_t)d;
    }
}
/* each block before the hand-over (fx.c): 1 = core 1 may take it */
static int cr_c1_check(void)
{
    if (cr_dbg.c1 != C1_OK)
        return 0;
    if (cr_c1_assess(cr_c1_rejected(), cr_c1_faults(), 0) < 0) {
        cr_c1_give_up();
        return 0;
    }
    return 1;
}
/* the wait (fx.c): 0 = done; else core 1 is held for good (a stall or a fault), C1_GAVE_UP */
static int cr_c1_wait(void)
{
    int rc = cr_c1_wait_core();
    if (rc) {
        (void)cr_c1_assess(cr_c1_rejected(), cr_c1_faults(), rc == -1);
        cr_c1_give_up();
    }
    return rc;
}
/* this block: part 1 renders as core 1's job (its own scratch where an engine has shared scratch: fm6_core.c). The
 * scratch is the core's: core 1 (CR_C1_SELF, 3y187) renders the bass part and then its share of the chord's voices
 * one after the other, so one scratch serves both; core 0 renders the chord's other voices with its own. (The part's
 * test is 3y45's: the host's stub runs the job inline, on core 0's thread.) */
static volatile uint8_t cr_c1_split;
#define CR_C1_IDX(t) (CR_C1_SELF() || (cr_c1_split && (t) == &trk[1]))
#define CR_PROF_START() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)
#define CR_PROF_SKIP() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)   /* (not counted) */
#define CR_PROF(k) do { if (cr_dbg.prof_on) { uint32_t t_ = cr_prof_now(); cr_dbg.cur[k] += cr_prof_less(t_ - cr_dbg.mark); \
                                              cr_dbg.mark = t_; } } while (0)
/* .. and the fx stage's parts, on their own mark (inside CRP_FX's time: the stage's figure includes them and their
 * five timer reads a block, on the device ~0.1 us each) */
#define CR_PROFX_START() do { if (cr_dbg.prof_on) { cr_dbg.xmark = cr_prof_now(); cr_dbg.mark += cr_prof_cost; } } while (0)
#define CR_PROFX(k) do { if (cr_dbg.prof_on) { uint32_t t_ = cr_prof_now(); cr_dbg.xcur[k] += cr_prof_less(t_ - cr_dbg.xmark); \
                                               cr_dbg.xmark = t_; cr_dbg.mark += cr_prof_cost; } } while (0)   /* (the
                                                * host's mark costs 6.6 k instructions: not the outer stage's) */

/* power-on (main.c fm1_main, before audio_init and the timers; not in SAFE MODE): start core 1. It boots through the
 * chip's ROM, which the PC limits refuse, and its entry word is in the top of RAM: both open while it starts.
 * Not answering in 20 ms: held for the session (C1_FAILED), single core, the shared budget */
static void cr_c1_boot(void)
{
    int rc;
    cr_rails_record();                                  /* (the boot loader's levels, reported either way) */
    if (cr_c1_off || CR_SINGLE_CORE) {                  /* Options > Dual Core Off: one core this session, the boot
                                                         * loader's rails (the stock behaviour); the single-core
                                                         * package: always (cr_c1_off held at 1) */
        cr_c1_off = 1;
        cr_dbg.c1 = C1_OFF;
        return;
    }
    cr_rails_init();                                    /* the core rails up before core 1 starts (IRQs off) */
#if defined(FM1_HAVE_CPU1) && !CR_SINGLE_CORE           /* (the single-core package: the start compiled out) */
    {
    uint32_t t0 = fm1_ticks();
    fm1_guard_unlock_top();                             /* (fm1_main locks it after the IRQs are attached) */
    fm1_guard_pc_open();
    rc = fm1_cpu1_start();
    fm1_guard_enable(FM1_GUARD_PC);
    cr_dbg.c1_start_us = (fm1_ticks() - t0) / CR_PROF_PER_US;
    }
#elif defined(FM1_HAVE_CPU1)
    rc = -1;                                            /* (the single-core package: never reached, returned above) */
#else
    rc = cr_c1_test_nostart ? -1 : 0;
#endif
    cr_dbg.c1 = rc ? C1_FAILED : C1_OK;
}

/* main.c fm1_main (and the emulator's): core 1 at power-on. NOT in SAFE MODE, as before. And (0.22, Dual Core On by
 * default) NOT WHEN THE BOOT BEFORE THIS ONE FAILED: a reset within 30 s of a boot that the boot guard counted
 * (cr_bootguard.h `counted`: a crash's reboot or the watchdog, whatever the stage; a hang in the start itself leaves
 * prev_stage "core 1"). That boot runs on one core ("c1 failed"; Options > Dual Core reads "On . restart"), loads and
 * saves as any other, and the player can switch Dual Core Off there. Without this a unit whose second core crashes
 * the start would crash twice at every power-on and come up in SAFE MODE, where nothing is saved: with On the
 * default it could not be switched Off. This net writes nothing by itself: the next power-on tries the core again
 * (a hang in the start leaves no bar: core 1 never answered. A crash or a hang beside a core 1 that ran is the bar's,
 * cr_c1_bar_boot, which cr_settings_boot ran before this: cr_c1_off already 1, Dual Core saved Off) */
static void cr_c1_power_on(int safe, int counted)
{
#if CR_SINGLE_CORE                                      /* the single-core package: never, whatever the record says */
    (void)safe; (void)counted;
    cr_c1_off = 1;
    cr_c1_boot();                                       /* (the rails' record; C1_OFF) */
#else
    if (safe) {
        cr_dbg.c1 = C1_OFF;
        cr_c1_off = 1;                                  /* (the Options' value then reads "On . restart") */
        return;
    }
    if (counted && !cr_c1_off) {
        cr_c1_off = 1;
        cr_c1_boot();                                   /* (the rails' record; C1_OFF) */
        cr_dbg.c1 = C1_FAILED;
        return;
    }
    cr_c1_boot();
#endif
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
        cr_dbg.c1w_sum = cr_dbg.c1w_max = 0;
        cr_dbg.win_n = 0;
        cr_dbg.win_reset = 0;
    }
    {   /* 3y50: each core's half (above) */
        uint32_t c0 = 0;
        for (k = 0; k < CRP_N; k++)
            if (k != CRP_WAIT)
                c0 += cr_dbg.cur[k];
        c0 = c0 > cr_dbg.c1v_cur ? c0 - cr_dbg.c1v_cur : 0u;
        if (c0 > cr_dbg.pk_c0_busy)
            cr_dbg.pk_c0_busy = c0;
        if (cr_dbg.c1_busy_cur > cr_dbg.pk_c1_busy)
            cr_dbg.pk_c1_busy = cr_dbg.c1_busy_cur;
        cr_dbg.c1w_sum += cr_dbg.c1_busy_cur;           /* (GEEK OUT 2's CORE 1: cr_prof_window) */
        if (cr_dbg.c1_busy_cur > cr_dbg.c1w_max)
            cr_dbg.c1w_max = cr_dbg.c1_busy_cur;
        if (cr_dbg.cur[CRP_WAIT] > cr_dbg.pk_w0)
            cr_dbg.pk_w0 = cr_dbg.cur[CRP_WAIT];
        if (cr_dbg.fxw_cur > cr_dbg.pk_fxw)
            cr_dbg.pk_fxw = cr_dbg.fxw_cur;
        if (cr_dbg.w1_cur > cr_dbg.pk_w1)
            cr_dbg.pk_w1 = cr_dbg.w1_cur;
        cr_dbg.fxmap_last = cr_dbg.fxmap_cur;
        cr_dbg.c1_busy_cur = cr_dbg.c1v_cur = cr_dbg.w1_cur = cr_dbg.fxw_cur = cr_dbg.fxmap_cur = 0;
    }
    for (k = 0; k < CRP_N; k++) {
        uint32_t c = cr_dbg.cur[k];
        cr_dbg.win_sum[k] += c;
        if (c > cr_dbg.win_max[k])
            cr_dbg.win_max[k] = c;
        cr_dbg.tot_sum[k] += c;
        if (c > cr_dbg.tot_max[k])
            cr_dbg.tot_max[k] = c;
        if (c > cr_dbg.pk[k])
            cr_dbg.pk[k] = c;
        cr_dbg.last[k] = c;
        cr_dbg.cur[k] = 0;
    }
    if (cr_dbg.xcur[CRX_DOWN] | cr_dbg.xcur[CRX_DELAY] | cr_dbg.xcur[CRX_REVERB] | cr_dbg.xcur[CRX_UP]) {
        for (k = 0; k < CRX_N; k++) {
            uint32_t c = cr_dbg.xcur[k];
            cr_dbg.x_sum[k] += c;
            cr_dbg.x_tot_sum[k] += c;
            if (c > cr_dbg.x_pk[k])
                cr_dbg.x_pk[k] = c;
            if (c > cr_dbg.x_tot_max[k])
                cr_dbg.x_tot_max[k] = c;
            cr_dbg.xcur[k] = 0;
        }
        cr_dbg.x_busy++;
        cr_dbg.x_tot_busy++;
    }
    cr_dbg.win_n++;
    cr_dbg.tot_n++;
}
/* the main loop, once a second (audio.c cpu_window): the window closed into avg_us / max_us */
/* audio.c, after a half: its whole time (us, as felucca_dbg.max_us measures it) and whether the DMA had moved on */
static void cr_prof_whole(uint32_t us, int late, uint32_t all, uint32_t t5, uint32_t per)
{
    uint32_t k, m = 0;
    if (us > cr_dbg.pk_all)
        cr_dbg.pk_all = us;
    if (all > cr_dbg.pk_wall) {
        cr_dbg.pk_wall = all;
        cr_dbg.pk_fxmap = cr_dbg.fxmap_last;             /* (3y50: who ran which effect in the largest half) */
    }
    if (t5 > cr_dbg.pk_t5)
        cr_dbg.pk_t5 = t5;
    /* the DAC needs block k of the four at the next boundary + k quarters of a half, and block k is written by
     * lat + (k + 1) quarters of `all`: both straight lines in k, so the first and the last block decide */
    if (cr_dbg.lat + all / 4u > per || cr_dbg.lat + all > per + per / 4u * 3u)
        cr_dbg.stale++;
    cr_dbg.pk_n++;
    if (late) {
        for (k = 1; k < CRP_N; k++)
            if (cr_dbg.last[k] > cr_dbg.last[m])
                m = k;
        cr_dbg.late_n++;
        cr_dbg.late_stage = m + 1u;
        cr_dbg.late_us = us;
    }
}
/* audio.c, first thing in a half: t0 its entry, per a half in ticks -> its lag behind its boundary. Nobody tells
 * the ISR when the boundary was, so it is estimated: one every `per` from the earliest entry seen (an entry is never
 * early), moved up by the smallest lag of every 64 halves, 43 us at most (the two clocks drift, `per` is rounded). A lag of a
 * whole half or more: that many boundaries went unserved (one pending bit), and the DAC replays what it played two
 * halves before: stale */
static uint32_t cr_prof_entry(uint32_t t0, uint32_t per)
{
    uint32_t lag;
    if (!cr_dbg.lat_on) {
        cr_dbg.lat_on = 1;
        cr_dbg.lat_tb = t0;
        cr_dbg.lat_min = 0xFFFFFFFFu;
        cr_dbg.lat_n = 0;
    } else {
        cr_dbg.lat_tb += per;
    }
    lag = t0 - cr_dbg.lat_tb;
    if ((int32_t)lag < 0) {                              /* (before the estimate: the estimate was late) */
        cr_dbg.lat_tb = t0;
        lag = 0;
    }
    if (lag >= per) {
        uint32_t k = lag / per;
        cr_dbg.stale += k;
        cr_dbg.lat_tb += k * per;
        lag -= k * per;
    }
    if (lag < cr_dbg.lat_min)
        cr_dbg.lat_min = lag;
    if (++cr_dbg.lat_n >= 64u) {
        uint32_t d = cr_dbg.lat_min > 1024u ? 1024u : cr_dbg.lat_min;   /* (at most 43 us a window: the drift is a
                                                          * few; 64 halves that were ALL behind are an overload, and
                                                          * its lag is not taken for drift) */
        cr_dbg.lat_tb += d;
        lag -= d;
        cr_dbg.lat_min = 0xFFFFFFFFu;
        cr_dbg.lat_n = 0;
    }
    if (lag > cr_dbg.pk_lat)
        cr_dbg.pk_lat = lag;
    cr_dbg.lat = lag;
#ifdef FM1_IRQ_TARGET
    return lag;
#else
    return 0u;                                           /* (the emulator's halves come when the host's sound card asks
                                                          * for them, in bursts: no boundary to be behind, and
                                                          * audio.c's shed must not take the bursts for an overload) */
#endif
}
/* main.c, TIMER5's entry (10 kHz, above everything but a fault): the gap since the last one. 100 us, unless the
 * interrupts were masked: then the gap is the masked stretch (to the tick: +-100 us) and pc where it ended (the
 * instruction after the sti: build/choralroot.dis names the section) */
static inline void cr_prof_t5(uint32_t t0, uint32_t pc, uint32_t ms)
{
    uint32_t gap = t0 - cr_dbg.t5_last;
    if (cr_dbg.t5_last && gap > cr_dbg.pk_t5gap) {
        cr_dbg.pk_t5gap = gap;
        cr_dbg.pk_t5pc = pc;
        cr_dbg.pk_t5gap_ms = ms | 1u;
    }
    cr_dbg.t5_last = t0 | 1u;                            /* (never 0: "no entry yet") */
}
/* 73 DEBUG has read the worst cases: the next read's are the stretch from here (the main loop; the ISR may be half
 * way through a half: that half then counts partly, once) */
static void cr_prof_clear(void)
{
    uint32_t k;
    for (k = 0; k < CRP_N; k++)
        cr_dbg.pk[k] = 0;
    for (k = 0; k < CRX_N; k++) {
        cr_dbg.x_pk[k] = 0;
        cr_dbg.x_sum[k] = 0;
    }
    cr_dbg.pk_all = cr_dbg.pk_n = cr_dbg.late_n = cr_dbg.late_stage = cr_dbg.late_us = cr_dbg.x_busy = 0;
    cr_dbg.pk_lat = cr_dbg.pk_wall = cr_dbg.pk_t5 = cr_dbg.stale = cr_dbg.pk_t5gap = cr_dbg.pk_t5pc = cr_dbg.pk_t5gap_ms = 0;
    cr_dbg.pk_c0_busy = cr_dbg.pk_c1_busy = cr_dbg.pk_w0 = cr_dbg.pk_fxw = cr_dbg.pk_w1 = cr_dbg.pk_fxmap = cr_dbg.c1x_jobs = 0;
}
static void cr_prof_window(void)
{
    uint32_t k;
    if (cr_dbg.win_reset || !cr_dbg.win_n)
        return;
    for (k = 0; k < CRP_N; k++) {
        cr_dbg.avg_us[k] = cr_dbg.win_sum[k] / cr_dbg.win_n / CR_PROF_PER_US;
        cr_dbg.max_us[k] = cr_dbg.win_max[k] / CR_PROF_PER_US;
    }
    cr_dbg.c1_avg_us = cr_dbg.c1w_sum / cr_dbg.win_n / CR_PROF_PER_US;
    cr_dbg.c1_max_us = cr_dbg.c1w_max / CR_PROF_PER_US;
    cr_dbg.win_reset = 1;
}
#else
#define cr_dualcore_active() 0
#define CR_C1_IDX(t) 0
#define CR_PROF_START() ((void)0)
#define CR_PROF(k) ((void)0)
#define CR_PROF_SKIP() ((void)0)
#define CR_PROFX_START() ((void)0)
#define CR_PROFX(k) ((void)0)
#define cr_prof_half() ((void)0)
#define cr_prof_whole(us, late, all, t5, per) ((void)0)
#define cr_prof_entry(t0, per) 0u
#define cr_prof_t5(t0, pc, ms) ((void)0)
#define cr_prof_window() ((void)0)
#define cr_c1_boot() ((void)0)
#define cr_c1_power_on(safe, counted) ((void)0)
#define cr_c1_stack_used() 0u
#define cr_c1_rejected() 0u
#define cr_c1_faults() 0u
#endif

#ifndef CR_C1_INJECTED
#define CR_C1_INJECTED() 0               /* (release and host builds: no injection) */
#endif

/* GEEK OUT's / Options' words for why core 1 was kept off (cr_c1_barred) */
static const char *cr_c1_bar_name(void)
{
    static const char *const N[4] = {"", "after a crash", "after a hang", "gave up"};
    return N[cr_c1_barred & 3u];
}
/* GEEK OUT's / the console's word for core 1 */
static const char *cr_c1_name(void)
{
    static const char *const N[4] = {"off", "ok", "failed", "gave up"};
    return N[cr_dbg.c1 & 3u];
}
