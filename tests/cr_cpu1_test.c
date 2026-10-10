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
 *    one core), c1 "gave up", the shared budget back; core 1 not answering at start -> "failed", one core. */
#define CR_CPU1 1
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
#ifndef FELUCCA_ANALOG
#define FELUCCA_ANALOG 0
#endif
#ifndef FM6_POLY
#define FM6_POLY 8
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

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
static void blocks(uint32_t nb)
{
    int32_t out[2 * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        ua_stage_on = 1;                            /* the computer records: master, CHORD, BASS staged */
        mix_block(out, CTL);
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
           "  straight to filter and pitch, a chord change, releases)\n");

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
    {   /* core 1 not answering at power-on */
        cr_dbg.c1 = C1_OFF;
        cr_c1_test_nostart = 1;
        cr_c1_boot();
        CHECK(cr_dbg.c1 == C1_FAILED && !cr_dualcore_active(), "no answer at start: c1 %s", cr_c1_name());
        cr_c1_test_nostart = 0;
        cr_c1_boot();
        CHECK(cr_dbg.c1 == C1_OK && cr_dualcore_active(), "an answer at start: c1 %s", cr_c1_name());
        printf("  start: no answer -> \"failed\" (one core), an answer -> \"ok\"\n");
    }
    printf("cr_cpu1: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
