/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the bass part on the second core (firmware/src/cr_cpu1.h, fx.c mix_parts_split, docs/DUALCORE.md),
 * on the sound side as tests/regress.c builds it (tests/hostsim.c) with CR_CPU1 1 and cr_cpu1.h's stub core, which
 * runs the job when it is handed (before core 0 renders part 0: the split must not depend on the order):
 *   sh tests/run_cr_tests.sh
 * 1. bit for bit (the output and the USB capture's staged CHORD / BASS channels): the split (core 1 alive) renders what one core renders, the samples hashed, for every pair of
 *    three presets (VA, FM6, CZ-1) on parts 0 and 1, with an S&H LFO on the matrix of both parts (the shared
 *    generator's order), a chord change and releases; under the budget, so both runs keep the same voices;
 * 2. the budgets: with core 1 alive 8 + 8 VA voices sound (parts 0 and 1, 16 units each), none given up; with one
 *    core 8 in all (the shared 16), the rest given up;
 * 3. the fail-safes: a job that never answers -> core 0 renders part 1 itself in that block (the same samples as
 *    one core), c1 "gave up", the shared budget back; core 1 not answering at start -> "failed", one core;
 *    (3y303) every render has the drums ringing, their three sends up: core 1's job renders them, and core 0 when it
 *    reruns the job;
 * 4. reliability (after Melodee PR 21, docs/DUALCORE.md): core 1's mailbox validation (hal/fm1_cpu1.h fm1_c1_poll: a
 *    wrong sequence or check word, a null fn: refused, counted, never run), the escalation (cr_cpu1.h: a first
 *    misread is counted and keeps core 1, a second, a fault or a stall gives up), the rails' steps on a mock P33
 *    (hal/fm1_power.h: one step a write, never lower, never above 14 / 5);
 * 5. the bar (cr_cpu1.h cr_c1_bar_decide / cr_c1_bar_boot, the device's .noinit record mocked): a crash beside core 1,
 *    a watchdog reset while it ran or a give-up keep it off at the next boot; a reboot or a power-on do not. */
#define CR_CPU1 1
#define CR_FX 1                         /* as choralroot.c: ChoralRoot's delay and reverb after the parts (cr_fx.c) */
#define FELUCCA_UAC 1                   /* the USB capture's six channels (fx.c ua_stage) hashed too */
#ifndef FELUCCA_VA
#define FELUCCA_VA 1
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 1
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1
#endif
#ifndef FM6_POLY
#define FM6_POLY 8
#endif
#define CR_DRUMS 1                      /* as choralroot.c (3y303): the drums, on core 1 with it (fx.c c1_part1_job) */
#define CRD_IRQ_OFF() ((void)0)
#define CRD_IRQ_ON() ((void)0)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* the device's mailbox and its validation (hal/fm1_cpu1.h), alone on the host */
#define FM1_CPU1_LOGIC_ONLY 1
#define FM1_C1_SYNC() ((void)0)
#define FM1_C1_TEXT
#include "../firmware/hal/fm1_cpu1.h"
/* the rails (hal/fm1_power.h) on a mock P33: the registers, every write logged */
#define FM1_POWER_MOCK 1
static uint8_t p33[256];
static uint8_t p33_log[64][2];
static uint32_t p33_delays;
static uint32_t fm1_p33_timeouts, p33_fail_at = 0xFFFFFFFFu, p33_settles;   /* (from that write on, reads time out) */
static uint32_t p33_writes;
static uint8_t fm1_p33_read(uint32_t a) { if (p33_writes >= p33_fail_at) { fm1_p33_timeouts++; return 0xFF; } return p33[a & 255u]; }
static void fm1_p33_write(uint32_t a, uint8_t d)
{
    if (p33_writes < 64u) { p33_log[p33_writes][0] = (uint8_t)a; p33_log[p33_writes][1] = d; }
    p33_writes++;
    p33[a & 255u] = d;
}
static void fm1_delay_us(uint32_t us) { (void)us; p33_delays++; }
static void fm1_p33_or(uint32_t a, uint8_t d) { fm1_p33_write(a, (uint8_t)(p33[a & 255u] | d)); }
static void fm1_p33_and(uint32_t a, uint8_t d) { fm1_p33_write(a, (uint8_t)(p33[a & 255u] & d)); }
static void fm1_power_settle(void) { p33_settles++; }
#include "../firmware/hal/fm1_power.h"

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static const struct { uint32_t e, pi; const char *name; } PRESETS[3] = {
    {ENGI_VA, 19u, "VA"}, {ENGI_FM6, 0u, "FM6"}, {ENGI_CZ, 0u, "CZ-1"}};

static uint64_t hash, given_up;
static uint32_t drn, drb;                           /* (3y303: the drums' hits, one every 7 blocks; the blocks they
                                                     * rang in) */
static void blocks(uint32_t nb)
{
    int32_t out[2 * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        if (!(++drn % 7u))
            crd_hit(drn / 7u % DSS_N, (int)(drn & 1u));
        ua_stage_on = 1;                            /* the computer records: master, CHORD, BASS staged */
        mix_block(out, CTL);
        drb += crd_out != 0;
        for (i = 0; i < 2u * CTL; i++)
            hash = (hash ^ (uint32_t)out[i]) * 0x100000001B3ull;
        for (i = 0; i < CTL * UA_CAP_CHANNELS; i++)  /* (the CHORD / BASS dry taps; the master pair is choralroot.c's) */
            hash = (hash ^ (uint16_t)ua_stage[i]) * 0x100000001B3ull;
    }
}
static void setup(uint32_t p0, uint32_t p1)
{
    uint32_t k;
    host_tracks_init();
    voice_fade_steal = 1;                           /* as ChoralRoot (cr_out.c) */
    host_preset(&trk[0], PRESETS[p0].e, PRESETS[p0].pi);
    host_preset(&trk[1], PRESETS[p1].e, PRESETS[p1].pi);
    crd_kit_load(1);                                /* the drums (3y303): every drum's three sends up, Levels off 64 */
    for (k = 0; k < 3u; k++) {
        crd_fx[k][0] = (uint8_t)(40 + 30 * k);
        crd_fx[k][1] = (uint8_t)(110 - 20 * k);
        crd_fx[k][2] = 127;
        crd_lvl[k] = (uint8_t)(50 + 25 * k);
        crd_drv[k] = (uint8_t)(20 + 50 * k);       /* (3y304: each drum driven) */
    }
    memset(crd_dist, 0, sizeof crd_dist);
    memset(crd_v, 0, sizeof crd_v);
    drn = drb = 0;
    for (k = 0; k < 2u; k++) {                      /* S&H LFO -> cutoff: a draw from the shared generator per cycle */
        trk[k].p[P_VOICE] = V_POLY;
        trk[k].p[P_LWAVE] = 4;
        trk[k].p[P_LRATE] = (int16_t)(110 + 7 * k);
        trk[k].p[P_M1SRC] = MS_LFO;
        trk[k].p[P_M1DST] = MD_CUT;
        trk[k].p[P_M1AMT] = 40;
        trk[k].p[P_LD_FLT] = 30;                    /* and straight to the filter and the pitch (track_render's) */
        trk[k].p[P_LD_PIT] = 4;
    }
}
/* a chord of 3 on part 0 and a bass note on part 1, a change (3 releasing + 3 new + 1: 7 voices, under the shared
 * budget, so both runs keep the same voices), releases */
static uint64_t render_pair(uint32_t p0, uint32_t p1, uint8_t c1)
{
    static const uint8_t CH1[3] = {60, 64, 67}, CH2[3] = {62, 65, 69};
    uint32_t i;
    setup(p0, p1);
    cr_dbg.c1 = c1;
    hash = 0xCBF29CE484222325ull;
    blocks(4);
    for (i = 0; i < 3u; i++)
        trk_note_on(&trk[0], CH1[i], 100);
    trk_note_on(&trk[1], 36, 110);
    blocks(300);
    for (i = 0; i < 3u; i++) {
        trk_note_off(&trk[0], CH1[i]);
        trk_note_on(&trk[0], CH2[i], 96);
    }
    blocks(300);
    for (i = 0; i < 3u; i++)
        trk_note_off(&trk[0], CH2[i]);
    trk_note_off(&trk[1], 36);
    blocks(400);
    return hash;
}

/* in a child: the global state starts fresh for each render (as regress.c runs its jobs) */
static uint64_t fork_render(uint32_t p0, uint32_t p1, uint8_t c1, uint8_t hang)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (!pid) {
        cr_c1_test_hang = hang;
        h = render_pair(p0, p1, c1);
        if (drb < 500u)
            h = 0;                                  /* (the drums must ring: most of the 1004 blocks) */
        if (hang)
            h ^= (uint64_t)cr_dbg.c1 << 60;       /* (must read "gave up": C1_GAVE_UP = 3) */
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

static uint32_t sounding_of(uint32_t p)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}
static void budget(uint8_t c1, uint32_t *n0, uint32_t *n1)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {                   /* (no voice left from the run before) */
        memset(trk[i].v, 0, sizeof trk[i].v);
        vsq_drop(&trk[i], 128u);
    }
    setup(0, 0);                                    /* VA on both: 2 units a voice */
    trk[1].p[P_M1SRC] = trk[0].p[P_M1SRC] = 0;
    trk[0].p[P_REL] = trk[1].p[P_REL] = 60;
    cr_dbg.c1 = c1;
    voice_kills = 0;
    blocks(2);
    for (i = 0; i < 8u; i++) {                      /* an 8-note chord (a 13th, every extension) */
        static const uint8_t C13[8] = {48, 52, 55, 58, 62, 65, 69, 72};
        trk_note_on(&trk[0], C13[i], 100);
        blocks(1);
    }
    for (i = 0; i < 8u; i++) {                      /* and 8 on the bass part */
        trk_note_on(&trk[1], (uint8_t)(24 + 3 * i), 100);
        blocks(1);
    }
    blocks(20);
    *n0 = sounding_of(0);
    *n1 = sounding_of(1);
}

/* ---- 4. reliability ---- */
static uint32_t ran_n, ran_arg;
static void job_count(uint32_t a) { ran_n++; ran_arg = a; }
static void mailbox(void)
{
    uint32_t done = fm1_c1_done, r0;
    CHECK(fm1_c1_fn == fm1_c1_idle && fm1_c1_fn, "the mailbox at rest holds the idle stub, not null");
    CHECK(fm1_c1_poll(&done) == 0 && !fm1_c1_rejected, "at rest: nothing run, nothing refused");
    fm1_c1_post(job_count, 7u);
    CHECK(fm1_c1_poll(&done) == 1 && ran_n == 1u && ran_arg == 7u && fm1_c1_done == fm1_c1_job && done == fm1_c1_job,
          "the next request with its check word: run once");
    CHECK(fm1_c1_poll(&done) == 0 && ran_n == 1u, "run once only");
    r0 = fm1_c1_rejected;
    fm1_c1_job = done + 0x00200000u;                 /* the sequence misread (as Melodee measured: 0x00200000) */
    CHECK(fm1_c1_poll(&done) == -1 && ran_n == 1u && fm1_c1_rejected == r0 + 1u && fm1_c1_misread == done + 0x00200000u,
          "a wrong sequence: refused, counted, not run");
    fm1_c1_job = done + 1u;                          /* a plausible sequence, the last check word: refused */
    CHECK(fm1_c1_poll(&done) == -1 && ran_n == 1u && fm1_c1_rejected == r0 + 2u, "a stale check word: refused");
    fm1_c1_job = done;
    fm1_c1_post(job_count, 9u);
    fm1_c1_chk ^= 0x00200000u;                       /* the check word misread */
    CHECK(fm1_c1_poll(&done) == -1 && ran_n == 1u && fm1_c1_rejected == r0 + 3u, "a wrong check word: refused");
    fm1_c1_chk ^= 0x00200000u;
    fm1_c1_arg = 8u;                                 /* the argument misread */
    CHECK(fm1_c1_poll(&done) == -1 && ran_n == 1u && fm1_c1_rejected == r0 + 4u, "a wrong argument: refused");
    fm1_c1_arg = 9u;
    CHECK(fm1_c1_poll(&done) == 1 && ran_n == 2u && ran_arg == 9u, "the same request read right: run");
    {   /* a null fn with a check word that matches it: still never called */
        uint32_t seq = done + 1u;
        fm1_c1_fn = 0;
        fm1_c1_arg = 0;
        fm1_c1_chk = FM1_C1_CHECK(0, 0u, seq);
        fm1_c1_job = seq;
        CHECK(fm1_c1_poll(&done) == -1 && fm1_c1_rejected == r0 + 5u && done == seq - 1u, "a null job: refused");
        fm1_c1_job = done;
        fm1_c1_fn = fm1_c1_idle;
    }
    printf("  mailbox: the next request with its check word runs once; a wrong sequence, check word or argument, a null\n"
           "  fn: refused and counted (%u), never run\n", (unsigned)fm1_c1_rejected);
}

static void esc_reset(void)
{
    cr_dbg.c1_misreads = 0;
    cr_dbg.c1_fallback = 0;
    cr_c1_seen = 0;
    cr_c1_test_rejected = cr_c1_test_faults = 0;
    cr_c1_test_hang = 0;
}
static void escalation(void)
{
    /* the state machine: no boost (the rails stay at 14 / 5) */
    esc_reset();
    CHECK(cr_c1_assess(0, 0, 0) == 0 && !cr_dbg.c1_misreads, "healthy: nothing");
    CHECK(cr_c1_assess(3, 0, 0) == 0 && cr_dbg.c1_misreads == 1u, "a first misread (a burst of 3): counted, core 1 kept");
    CHECK(cr_c1_assess(3, 0, 0) == 0, "no new misreads: kept");
    CHECK(cr_c1_assess(4, 0, 0) == -1, "a second misread: give up");
    esc_reset();
    CHECK(cr_c1_assess(0, 1, 0) == -1, "a fault: give up");
    esc_reset();
    CHECK(cr_c1_assess(0, 0, 1) == -1, "a stall: give up");

    /* through the audio blocks (fx.c mix_parts_split) */
    esc_reset();
    setup(0, 0);
    cr_dbg.c1 = C1_OK;
    cr_dbg.rail_now[0] = 14;
    cr_dbg.rail_now[1] = 5;
    trk_note_on(&trk[1], 36, 100);
    cr_c1_test_rejected = 1;
    blocks(3);
    CHECK(cr_dbg.c1 == C1_OK && !cr_dbg.c1_fallback, "a first misread: c1 %s", cr_c1_name());
    cr_c1_test_rejected = 2;
    blocks(1);
    CHECK(cr_dbg.c1 == C1_GAVE_UP && !cr_dualcore_active() && cr_dbg.c1_fallback == 1u, "a second misread: c1 %s", cr_c1_name());
    CHECK(cr_dbg.rail_now[0] == 14u && cr_dbg.rail_now[1] == 5u, "no rail raised past 14 / 5 (%u, %u)", cr_dbg.rail_now[0],
          cr_dbg.rail_now[1]);
    esc_reset();
    cr_dbg.c1 = C1_OK;
    blocks(1);
    cr_c1_test_faults = 1;
    blocks(1);
    CHECK(cr_dbg.c1 == C1_GAVE_UP && cr_dbg.c1_fallback == 1u, "a fault: c1 %s", cr_c1_name());
    esc_reset();
    cr_dbg.c1 = C1_OK;
    cr_c1_test_hang = 1;
    blocks(1);
    CHECK(cr_dbg.c1 == C1_GAVE_UP && cr_dbg.c1_fallback == 1u, "a stall: c1 %s", cr_c1_name());
    esc_reset();
    printf("  escalation: a first misread counted, core 1 kept; a second, a fault or a stall -> \"gave up\", c1_fallback 1;\n"
           "  the rails never above 14 / 5\n");

    /* the boot: Dual Core Off keeps the boot loader's rails; On raises them before core 1 starts */
    cr_dbg.rail_now[0] = cr_dbg.rail_now[1] = 0;
    cr_c1_off = 1;
    cr_c1_boot();
    CHECK(cr_dbg.c1 == C1_OFF && cr_dbg.rail_boot[0] == 11u && cr_dbg.rail_now[0] == 11u && cr_dbg.rail_now[1] == 3u,
          "Dual Core Off: the boot loader's rails (%u, %u)", cr_dbg.rail_now[0], cr_dbg.rail_now[1]);
    cr_c1_off = 0;
    cr_c1_boot();
    CHECK(cr_dbg.c1 == C1_OK && cr_dbg.rail_boot[0] == 11u && cr_dbg.rail_boot[1] == 3u && cr_dbg.rail_now[0] == 14u &&
          cr_dbg.rail_now[1] == 5u, "Dual Core On: the rails 14, 5 (%u, %u)", cr_dbg.rail_now[0], cr_dbg.rail_now[1]);
    printf("  boot: Dual Core Off keeps SYSVDD 11 / VDC14 3; On raises them to 14 / 5 before core 1 starts\n");
}

static void rails(void)
{
    uint32_t i, ok = 1;
    memset(p33, 0, sizeof p33);
    p33[0x09] = 0xA0u | 11u;                        /* SYSVDD 11 with other bits in the register */
    p33[0x06] = 0x58u | 3u;                         /* VDC14 3 */
    p33[0x05] = 4u;
    fm1_power_init();
    CHECK(fm1_power.boot[FM1_RAIL_SYSVDD] == 11u && fm1_power.boot[FM1_RAIL_VDC14] == 3u && fm1_power.boot[FM1_RAIL_VDDIO] == 4u,
          "the boot levels recorded");
    CHECK(p33[0x09] == (0xA0u | 14u) && p33[0x06] == (0x58u | 5u) && p33[0x05] == 4u, "init: SYSVDD 14, VDC14 5, the other bits kept");
    CHECK(p33_writes == 5u && p33_log[0][0] == 0x06u && p33_log[1][0] == 0x06u && p33_log[2][0] == 0x09u,
          "VDC14 first, then SYSVDD: 2 + 3 writes (%u)", (unsigned)p33_writes);
    ok = (p33_log[0][1] & 7u) == 4u && (p33_log[1][1] & 7u) == 5u;
    for (i = 2; i < 5u; i++)
        ok &= p33_log[i][0] == 0x09u && (p33_log[i][1] & 15u) == 10u + i;
    CHECK(ok && p33_delays >= 5u, "VDC14 one step a write (4, 5), then SYSVDD (12, 13, 14), 100 us apart");
    p33[0x09] = 0xA0u | 12u;
    p33_writes = 0;
    CHECK(fm1_power_step(FM1_RAIL_SYSVDD, 15u) == 1 && p33_writes == 1u && (p33[0x09] & 15u) == 13u, "one step per call");
    CHECK(fm1_power_step(FM1_RAIL_SYSVDD, 15u) == 1 && (p33[0x09] & 15u) == 14u, "a second step");
    CHECK(fm1_power_step(FM1_RAIL_SYSVDD, 15u) == 0 && p33_writes == 2u && (p33[0x09] & 15u) == 14u, "capped at 14 (1.35 V)");
    CHECK(fm1_power_step(FM1_RAIL_SYSVDD, 12u) == 0 && (p33[0x09] & 15u) == 14u && p33_writes == 2u, "never lowers");
    fm1_power_raise(FM1_RAIL_SYSVDD, 9u);
    CHECK((p33[0x09] & 15u) == 14u && p33_writes == 2u, "raise to a lower level: nothing");
    p33_writes = 1u;
    fm1_power_raise(FM1_RAIL_VDC14, 7u);
    CHECK((p33[0x06] & 7u) == 5u && p33_writes == 1u, "VDC14 capped at 5 (1.50 V)");
    CHECK(fm1_power_goto(FM1_RAIL_SYSVDD, 15u) == 1 && fm1_power_goto(FM1_RAIL_VDC14, 6u) == 1 &&
          fm1_power_goto(FM1_RAIL_SYSVDD, 5u) == 1 && p33_writes == 1u, "the test op: above the cap or SYSVDD below 6 refused");
    CHECK(fm1_power_goto(FM1_RAIL_SYSVDD, 12u) == 0 && (p33[0x09] & 15u) == 12u, "the test op: down to 12");
    CHECK(fm1_power_goto(FM1_RAIL_SYSVDD, 14u) == 0 && (p33[0x09] & 15u) == 14u, "the test op: back to 14");
    CHECK(fm1_power_goto(FM1_RAIL_VDC14, 3u) == 0 && (p33[0x06] & 7u) == 3u && fm1_power_goto(FM1_RAIL_VDC14, 5u) == 0 &&
          (p33[0x06] & 7u) == 5u, "the test op: VDC14 down to 3 and back to 5 (the cap)");
    fm1_power.recorded = 0;
    p33[0x09] = 15u;                                /* a boot loader that left it higher: kept (never lowered) */
    p33[0x06] = 6u;
    p33_writes = 0;
    fm1_power_init();
    CHECK(p33_writes == 0u && p33[0x09] == 15u && p33[0x06] == 6u, "init never lowers a higher boot level");
    printf("  rails (mock P33): init VDC14 3 -> 5 then SYSVDD 11 -> 14, a step a write, other bits kept; never lower\n");
}

/* the stock supply setup for a radio boot (hal/fm1_power.h fm1_power_stock, 3y240; Melodee 5e66df3) */
static void rails_stock(void)
{
    static const uint8_t regs[] = {0x00, 0x00, 0x00, 0x01, 0x05, 0x06, 0x07, 0x08, 0x09, 0x05, 0x06, 0x05, 0x00, 0x16,
                                   0x02, 0x17, 0x09};
    uint32_t i, ok = 1;
    memset(p33, 0, sizeof p33);
    memset(&fm1_power, 0, sizeof fm1_power);
    p33[0x09] = 11u;
    p33[0x06] = 3u;
    p33[0x05] = 4u;
    p33[0x00] = 0x04u;                             /* the boot loader's LDO13 */
    p33[0x17] = 0x20u;
    p33_writes = p33_settles = 0;
    CHECK(fm1_power_stock() == 1, "stock supplies: done");
    for (i = 0; i < sizeof regs; i++)
        ok &= p33_log[i][0] == regs[i];
    CHECK(ok && p33_writes == sizeof regs && p33_settles == 3u,
          "stock supplies: stock's order of 17 P33 writes (LDO13 / DCDC13 / LDO13 off with 3 waits ... SYSVDD last)");
    CHECK((p33_log[0][1] & 0x04u) && (p33_log[1][1] & 0x0Cu) == 0x0Cu && (p33_log[2][1] & 0x0Cu) == 0x08u,
          "stock supplies: LDO13 kept on until DCDC13 is on, then off");
    CHECK((p33[0x00] & 0x8Cu) == 0x88u && (p33[0x05] & 0x37u) == 0x34u && (p33[0x09] & 0x10u) && (p33[0x02] & 0x40u) &&
          !(p33[0x17] & 0x20u) && !(p33[0x16] & 0x3Fu), "stock supplies: DCDC13, RC_250k, VDDIO 3.2 V, drive, VDDOK_DIS");
    CHECK((p33[0x06] & 7u) == 5u && (p33[0x09] & 15u) == 14u && fm1_power.now[FM1_RAIL_VDC14] == 5u &&
          fm1_power.now[FM1_RAIL_SYSVDD] == 14u && fm1_power.boot[FM1_RAIL_VDC14] == 3u,
          "stock supplies: VDC14 and SYSVDD at ChoralRoot's caps (5 / 14), not stock's 7 / 15 (FM1_POWER_STOCK_LEVELS 0)");
    CHECK((p33_log[5][1] & 7u) == 4u, "stock supplies: register 6's word as stock's (0x0C: VDC14 4) before the field");
    fm1_power.recorded = 0;
    p33[0x09] = 15u;                               /* a soft reset after stock's levels: kept */
    p33[0x06] = 7u;
    CHECK(fm1_power_stock() == 1 && (p33[0x06] & 7u) == 7u && (p33[0x09] & 15u) == 15u,
          "stock supplies: never lowers a rail it found higher");
    memset(&fm1_power, 0, sizeof fm1_power);
    p33[0x09] = 11u;
    p33[0x06] = 3u;
    p33_writes = 0;
    p33_fail_at = 1u;                              /* a P33 transfer times out after the first write */
    CHECK(fm1_power_stock() == 0 && p33_writes < 4u && (p33[0x06] & 7u) == 3u && (p33[0x09] & 15u) == 11u,
          "stock supplies: a P33 timeout stops the sequence (no rail touched, 0: no radio this boot)");
    p33_fail_at = 0xFFFFFFFFu;
    printf("  rails (mock P33): the stock supply setup for a radio boot, in stock's order, capped, never lower\n");
}

/* ---- 5. the bar ---- */
static void bar_mock(uint32_t magic, uint32_t ran, uint32_t barred)
{
    cr_c1_bar_mock.magic = magic;
    cr_c1_bar_mock.ran = ran;
    cr_c1_bar_mock.barred = barred;
}
static void bar(void)
{
    const uint32_t M = FM1_C1_BAR_MAGIC;
    uint32_t w;
    /* the decision */
    CHECK(cr_c1_bar_decide(0u, 1u, C1_BAR_CRASH, BG_RST_SOFT) == C1_BAR_NONE &&
          cr_c1_bar_decide(M ^ 1u, 1u, C1_BAR_CRASH, BG_RST_WDT) == C1_BAR_NONE, "not the record (a power-on's garbage): none");
    for (w = C1_BAR_CRASH; w <= C1_BAR_GAVE_UP; w++)
        CHECK(cr_c1_bar_decide(M, 1u, w, BG_RST_SOFT) == w && cr_c1_bar_decide(M, 0u, w, BG_RST_POWERON) == w,
              "a reason left (%u): that reason, whatever the reset", (unsigned)w);
    CHECK(cr_c1_bar_decide(M, 0u, 9u, BG_RST_SOFT) == C1_BAR_NONE, "a reason out of range: none");
    CHECK(cr_c1_bar_decide(M, 1u, 0u, BG_RST_WDT) == C1_BAR_HANG, "core 1 ran and the watchdog reset: a hang");
    CHECK(cr_c1_bar_decide(M, 1u, 0u, BG_RST_SOFT) == C1_BAR_NONE, "core 1 ran, a soft reset without a reason (a reboot): none");
    CHECK(cr_c1_bar_decide(M, 1u, 0u, BG_RST_POWERON) == C1_BAR_NONE, "core 1 ran, a power-on: none");
    CHECK(cr_c1_bar_decide(M, 0u, 0u, BG_RST_WDT) == C1_BAR_NONE, "core 1 did not run, the watchdog: none");
    /* the boot (cr_settings.c cr_settings_boot: after cr_c1_off is read from the record) */
    cr_c1_off = 0;
    cr_c1_barred = 0;
    bar_mock(M, 1u, 0u);
    bootguard.cls = BG_RST_WDT;
    cr_c1_bar_boot(bootguard.cls);
    CHECK(cr_c1_off == 1u && cr_c1_barred == C1_BAR_HANG && cr_c1_bar_mock.magic == M && !cr_c1_bar_mock.ran &&
          !cr_c1_bar_mock.barred && !strcmp(cr_c1_bar_name(), "after a hang"),
          "a hang beside core 1, Dual Core On: core 1 off, \"%s\", the record cleared", cr_c1_bar_name());
    cr_c1_boot();
    CHECK(cr_dbg.c1 == C1_OFF, "a barred boot: core 1 never started (c1 %s)", cr_c1_name());
    cr_c1_off = 0;
    cr_c1_barred = 0;
    bar_mock(M, 1u, C1_BAR_CRASH);
    bootguard.cls = BG_RST_SOFT;
    cr_c1_bar_boot(bootguard.cls);
    CHECK(cr_c1_off == 1u && cr_c1_barred == C1_BAR_CRASH && !cr_c1_bar_mock.barred && !strcmp(cr_c1_bar_name(), "after a crash"),
          "a crash beside core 1 (the soft reset of the crash report): core 1 off, \"%s\"", cr_c1_bar_name());
    cr_c1_off = 1;
    cr_c1_barred = 0;
    bar_mock(M, 1u, C1_BAR_CRASH);
    cr_c1_bar_boot(BG_RST_SOFT);
    CHECK(cr_c1_off == 1u && cr_c1_barred == C1_BAR_NONE && cr_c1_bar_mock.magic == M && !cr_c1_bar_mock.ran &&
          !cr_c1_bar_mock.barred, "Dual Core already Off: no word of it, the record cleared");
    cr_c1_off = 0;
    bar_mock(0xDEADBEEFu, 1u, 2u);
    cr_c1_bar_boot(BG_RST_POWERON);
    CHECK(cr_c1_off == 0u && cr_c1_barred == C1_BAR_NONE && cr_c1_bar_mock.magic == M && !cr_c1_bar_mock.ran &&
          !cr_c1_bar_mock.barred, "a power-on's garbage: core 1 starts, the record made clean");
    cr_c1_bar_boot(BG_RST_WDT);
    CHECK(cr_c1_off == 0u && cr_c1_barred == C1_BAR_NONE, "a watchdog reset when core 1 had not run: core 1 starts");
    cr_c1_test_nostart = 0;
    cr_c1_boot();
    CHECK(cr_dbg.c1 == C1_OK, "a plain boot: core 1 started (c1 %s)", cr_c1_name());
    /* a give-up (the audio ISR) leaves the reason for the next boot */
    bar_mock(M, 1u, 0u);
    cr_c1_give_up();
    CHECK(cr_dbg.c1 == C1_GAVE_UP && cr_c1_bar_mock.magic == M && cr_c1_bar_mock.barred == C1_BAR_GAVE_UP,
          "a give-up: the record's reason %u", (unsigned)cr_c1_bar_mock.barred);
    cr_c1_off = 0;
    cr_c1_barred = 0;
    cr_c1_bar_boot(BG_RST_SOFT);
    CHECK(cr_c1_off == 1u && cr_c1_barred == C1_BAR_GAVE_UP && !strcmp(cr_c1_bar_name(), "gave up"),
          "the boot after a give-up (a reset before Dual Core Off was saved): core 1 off, \"%s\"", cr_c1_bar_name());
    cr_c1_barred = 0;
    cr_c1_off = 1;
    cr_dbg.c1 = C1_OFF;
    /* which crashes bar (hal/fm1_cpu1.h fm1_c1_crash_bars): core 0's debug sources, or none at all; not another
     * master's alone (the owner's unit: axi_rd_inv in the restart's soft reset) */
    CHECK(fm1_c1_crash_bars(0x1000u) && fm1_c1_crash_bars(0x200000u) && fm1_c1_crash_bars(0u) &&
          fm1_c1_crash_bars(0x1010u), "a crash bars: c0_pc_limit (0x1000), c0_rd_bus_inv (0x200000), a plain CPU exception, c0 + a master");
    CHECK(!fm1_c1_crash_bars(0x10u) && !fm1_c1_crash_bars(0x4u) && !fm1_c1_crash_bars(0x1u) && !fm1_c1_crash_bars(0xC000u),
          "no bar: axi_rd_inv (0x10), prp_wr_limit (0x4), the watchdog (0x1), dmc / dbg pending alone");
    printf("  bar: a crash beside core 1, a watchdog reset while it ran or a give-up keep it off at the next boot (the\n"
           "  reason told, the record cleared); a reboot, a power-on, Dual Core already Off: nothing; a crash bars only\n"
           "  when core 0's own debug sources (or none) raised it, not another master's (axi_rd_inv at a restart)\n");
}

int main(void)
{
    uint32_t a, b, n0, n1;
    printf("cr_cpu1: the bass part on the second core (docs/DUALCORE.md)\n");
    for (a = 0; a < 3u; a++)
        for (b = 0; b < 3u; b++) {
            uint64_t one = fork_render(a, b, C1_OFF, 0), two = fork_render(a, b, C1_OK, 0), late;
            cr_c1_test_late = 1;                    /* (the job after part 0's render: the other order) */
            late = fork_render(a, b, C1_OK, 0);
            cr_c1_test_late = 0;
            CHECK(one && one == two && one == late, "part 0 %s, part 1 %s: split %016llx (late %016llx), one core %016llx",
                  PRESETS[a].name, PRESETS[b].name, (unsigned long long)two, (unsigned long long)late,
                  (unsigned long long)one);
        }
    printf("  bit for bit: 3 x 3 preset pairs on parts 0 / 1, the job before and after part 0 (S&H LFOs on the matrix and\n"
           "  straight to filter and pitch, a chord change, releases; the drums ringing, every send up: 3y303)\n");

    budget(C1_OK, &n0, &n1);
    CHECK(n0 == 8u && n1 == 8u && voice_kills == 0u, "core 1 alive: part 0 %u, part 1 %u voices, %u given up (want 8, 8, 0)",
          n0, n1, (unsigned)voice_kills);
    printf("  core 1 alive: 8 + 8 voices sound (part 0 %u, part 1 %u, given up %u)\n", n0, n1, (unsigned)voice_kills);
    budget(C1_OFF, &n0, &n1);
    CHECK(n0 + n1 == 8u && voice_kills >= 8u, "one core: %u + %u voices, %u given up (want 8 in all)", n0, n1,
          (unsigned)voice_kills);
    printf("  one core: 8 in all (part 0 %u, part 1 %u, given up %u)\n", n0, n1, (unsigned)voice_kills);

    {   /* a job that never answers: core 0 renders it, the same samples as one core, "gave up" */
        uint64_t one = fork_render(1, 0, C1_OFF, 0), hung = fork_render(1, 0, C1_OK, 1);
        CHECK(hung == (one ^ ((uint64_t)C1_GAVE_UP << 60)), "a hung job: %016llx vs one core %016llx",
              (unsigned long long)hung, (unsigned long long)one);
        cr_c1_test_hang = 1;
        setup(0, 0);
        cr_dbg.c1 = C1_OK;
        trk_note_on(&trk[1], 36, 100);
        blocks(1);
        CHECK(cr_dbg.c1 == C1_GAVE_UP && !cr_dualcore_active() && !strcmp(cr_c1_name(), "gave up"),
              "after a hung job: c1 %s", cr_c1_name());
        cr_c1_test_hang = 0;
        printf("  a hung job: core 0 renders part 1, the samples of one core, c1 \"%s\", the shared budget\n",
               cr_c1_name());
    }
    {   /* no settings record read (a boot that never reads them; the record's default is On since 0.22,
         * tests/cr_settings_test.c): core 1 never started */
        cr_dbg.c1 = C1_OK;
        cr_c1_boot();
        CHECK(cr_c1_off == 1u && cr_dbg.c1 == C1_OFF && !cr_dualcore_active(), "no record read: c1 %s", cr_c1_name());
        printf("  start: until a record says On core 1 is not started: c1 \"%s\" (one core)\n", cr_c1_name());
    }
    {   /* 0.22, Dual Core On by default: main.c's cr_c1_power_on. SAFE MODE: never started. THE BOOT BEFORE THIS ONE
         * FAILED (the boot guard counted its reset): not started either, "failed", one core for this boot; the
         * next clean power-on starts it again. A unit whose second core crashes the start is so not left in SAFE MODE */
        cr_c1_off = 0;
        cr_dbg.c1 = C1_OK;
        cr_c1_power_on(1, 0);
        CHECK(cr_dbg.c1 != C1_OK && cr_c1_off == 1u && !cr_dualcore_active(), "SAFE MODE: c1 %s", cr_c1_name());
        cr_c1_off = 0;
        cr_dbg.c1 = C1_OFF;
        cr_dbg.rail_now[0] = 11;
        cr_dbg.rail_now[1] = 3;
        cr_c1_power_on(0, 1);
        CHECK(cr_dbg.c1 == C1_FAILED && cr_c1_off == 1u && !cr_dualcore_active() && !strcmp(cr_c1_name(), "failed") &&
              cr_dbg.rail_now[0] == 11u && cr_dbg.rail_now[1] == 3u,
              "the boot before failed, Dual Core On: c1 %s, rails %u / %u", cr_c1_name(), (unsigned)cr_dbg.rail_now[0], (unsigned)cr_dbg.rail_now[1]);
        cr_c1_off = 1;
        cr_dbg.c1 = C1_OK;
        cr_c1_power_on(0, 1);
        CHECK(cr_dbg.c1 == C1_OFF && cr_c1_off == 1u, "the boot before failed, Dual Core Off: c1 %s", cr_c1_name());
        cr_c1_off = 0;
        cr_dbg.c1 = C1_OFF;
        cr_c1_power_on(0, 0);
        CHECK(cr_dbg.c1 == C1_OK && cr_c1_off == 0u && cr_dualcore_active(), "a clean boot, Dual Core On: c1 %s", cr_c1_name());
        cr_c1_off = 1;
        cr_dbg.c1 = C1_OFF;
        printf("  start (0.22): SAFE MODE: not started; the boot before this one failed: not started (\"failed\", the rails as\n"
               "  the boot loader left them, one core for this boot); a clean boot with Dual Core On: \"ok\"\n");
    }
    {   /* Dual Core On: core 1 not answering at power-on */
        cr_dbg.c1 = C1_OFF;
        cr_c1_off = 0;
        cr_c1_test_nostart = 1;
        cr_c1_boot();
        CHECK(cr_dbg.c1 == C1_FAILED && !cr_dualcore_active(), "no answer at start: c1 %s", cr_c1_name());
        cr_c1_test_nostart = 0;
        cr_c1_boot();
        CHECK(cr_dbg.c1 == C1_OK && cr_dualcore_active(), "an answer at start: c1 %s", cr_c1_name());
        printf("  start: no answer -> \"failed\" (one core), an answer -> \"ok\"\n");
    }
    mailbox();
    escalation();
    rails();
    rails_stock();
    bar();
    printf("cr_cpu1: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
