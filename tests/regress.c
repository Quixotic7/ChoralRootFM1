/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Regression suite of the firmware's sound side on the Mac (same sources as the firmware, through hostsim.c, in
 * ChoralRoot's configuration: core.h's defaults; one core, Felucca's FX buses (CR_FX 0) as the goldens were made).
 *   build/host/regress [GOLDEN_FILE CPU_FILE]      (run_tests.sh builds and runs it)
 *
 * 1. golden renders: every engine x factory preset, the voice modes (POLY / MONO / LEGATO / UNISON) of the VA, FM6
 *    and P5, the FX sends on the VA, and the SLICER's GATE (slicer.c) on the phrase. Each render plays a fixed phrase
 *    (notes, an overlap, a chord, note-offs, the release tail) and is reduced to a 64-bit FNV-1a hash of its output
 *    samples, compared with GOLDEN_FILE (tests/golden.txt). Every render runs in its own fork()ed child of a process
 *    that never touched the DSP state, so all state starts as at boot: the noise / RAND / unison-phase seeds (libc.c
 *    rng_state, voice.c's unison seed), the FX buffers, the limiter. (Felucca's engines, its GM kit, its 4-track
 *    sequencer mix and their 58 renders left with Felucca's tree in 3y232.)
 * 2. health checks on every render: no sample beyond 16 bits, none near full scale, DC offset, the
 *    peak level, every voice free after the release, silence at the end. Fails with the numbers.
 * 3. CPU budget: per engine x preset (8 notes held) and two mixes, the instructions
 *    per sample counted by the kernel (proc_pid_rusage ri_instructions: the same on every run within
 *    ~1 %, unlike wall time), compared with CPU_FILE (tests/cpu_baseline.txt) at +-25 %; ns per
 *    sample printed for information. A count over the budget fails, one far under it only warns.
 * 4. voices (voice.c, on ChoralRoot's two sounding parts, chord and bass): the shared budget, stolen voices fade (no
 *    step), MONO / LEGATO / UNISON keep their note under pressure, P5's cap of 4 voices a part on one core, release
 *    frees voices, a key-up releases after a live mode change.
 * env: GOLDEN_UPDATE=1 rewrites GOLDEN_FILE, BUDGET_UPDATE=1 rewrites CPU_FILE (on purpose: review the
 * diff), VERBOSE=1 prints every render's numbers, JOBS=n children at once (default 8). */
/* the SLICER's render: the VA's SYNC BASS (its ACID line, pattern 6) */
#define R_ACID_E ENGI_VA
#define R_ACID_P 19u             /* VA SYNC BASS */
#define R_ACID_NAME "VA_SYNC_BASS"
#define R_PLAT ENGI_VA           /* the platform engine of the voice checks (8 voices, two budget units each) */
#ifndef FM6_POLY
#define FM6_POLY 8               /* as choralroot.c: FM6 at ChoralRoot's 8 voices (eng_fm6.c; Melodee: 16) */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
/* the keys' and MIDI's way in: the note itself (ChoralRoot's parts have no ARP) */
#define input_on trk_note_on
#define input_off trk_note_off
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

/* ------------------------------------------------------------- limits --- */
#define LIM_NEAR 30000            /* |sample| at or above: "near full scale" (the limiter holds ~18000) */
#define LIM_DC 400                /* |mean| of the sounding part, 16-bit units (1.2 %; the master blocks DC) */
#define LIM_PEAK_MIN 250          /* quieter than this (-42 dBFS): something is missing */
#define LIM_TAIL 6                /* AC peak of the last 0.25 s, after the voices are free and the FX rang out */
#define LIM_TAIL_DC 2             /* its constant part: the master DC blocker settles to 0 (error feedback; it
                                   * used to leave up to +-31), what is left is the midpoint of the AC tail */
#define FREE_CAP_S 12             /* the voices must be free this long after the last note-off */
#ifndef TAIL_S
#define TAIL_S 3                  /* FX tail rendered after the voices are free */
#endif
#define CPU_TOL 0.25              /* +-25 %, and at least CPU_SLACK instructions (light presets) */
#define CPU_SLACK 20

/* ------------------------------------------------------------ results --- */
typedef struct {
    uint64_t hash;
    uint32_t frames, over, near, vmax, ok;     /* ok: a scenario's own checks passed */
    int32_t peak, tail_peak, tail_dc, tmin, tmax;
    double dc, free_s;                         /* free_s: s from the last note-off until all voices free, -1 never */
    double ipc, ns;                            /* CPU: instructions and ns per sample */
    char msg[512];
} res_t;

static res_t R;
static uint32_t fpos, rel_at, dc_end, tail_from;
static int64_t dcs[2];

static int parts_free(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    return 1;
}

static uint32_t sounding(void)                 /* part voices sounding (not the ones fading for another part) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

/* one block of the mix into the hash and the numbers */
static int32_t last_out[2 * CTL];
static void blk(void)
{
    int32_t *o = last_out;
    uint32_t i, k;
    mix_block(o, CTL);
    for (i = 0; i < 2u * CTL; i++) {
        int32_t x = o[i], a = x < 0 ? -x : x;
        uint32_t u = (uint32_t)x;
        for (k = 0; k < 4u; k++) {
            R.hash ^= (u >> (8u * k)) & 0xFFu;
            R.hash *= 0x100000001B3ull;
        }
        if (a > R.peak)
            R.peak = a;
        R.over += a > 32767;
        R.near += a >= LIM_NEAR;
        if (fpos < dc_end)
            dcs[i & 1u] += x;
        if (tail_from && fpos >= tail_from) {
            R.tmin = x < R.tmin ? x : R.tmin;
            R.tmax = x > R.tmax ? x : R.tmax;
        }
    }
    k = busy_now();
    if (k > R.vmax)
        R.vmax = k;
    fpos += CTL;
}

static uint32_t at(double s) { return (uint32_t)(s * FS) / CTL * CTL; }
static void run_to(uint32_t f) { while (fpos < f) blk(); }

/* after the last note-off (rel_at): until the voices are free, then the FX tail */
static uint32_t free_cap_s = FREE_CAP_S;        /* (job_preset: CZ-1's ringing tones get longer) */
static void finish(void)
{
    uint32_t cap = rel_at + free_cap_s * FS;
    dc_end = dc_end ? dc_end : fpos;
    while (!parts_free() && fpos < cap)
        blk();
    R.free_s = parts_free() ? (double)(fpos - rel_at) / FS : -1;
    tail_from = fpos + TAIL_S * FS - FS / 4u;
    R.tmin = 0x7FFFFFFF;
    R.tmax = -0x7FFFFFFF;
    run_to(fpos + TAIL_S * FS);
    R.tail_peak = (R.tmax - R.tmin + 1) / 2;
    R.tail_dc = (R.tmax + R.tmin) / 2;
    R.frames = fpos;
    R.dc = (double)(llabs(dcs[0]) > llabs(dcs[1]) ? dcs[0] : dcs[1]) / (double)(dc_end ? dc_end : 1);
}

/* --------------------------------------------------------- the phrase --- */
/* notes (with an overlap: legato / steal), a 5-note chord, note-offs, the tail. Through input_on /
 * input_off (the keys' and MIDI's way in: a preset's ARP plays). */
static void phrase(track_t *t, uint32_t base)
{
    static const uint8_t CH[5] = {0, 7, 12, 16, 19};
    uint32_t i;
    input_on(t, base, 100);
    run_to(at(0.30));
    input_off(t, base);
    run_to(at(0.35));
    input_on(t, base + 4u, 80);
    run_to(at(0.60));
    input_on(t, base + 7u, 120);               /* overlaps: MONO / LEGATO hand over, POLY two voices */
    run_to(at(0.70));
    input_off(t, base + 4u);
    run_to(at(0.95));
    input_off(t, base + 7u);
    run_to(at(1.00));
    for (i = 0; i < 5u; i++)
        input_on(t, base - 12u + CH[i], 90 + 5 * i);
    run_to(at(1.80));
    for (i = 0; i < 5u; i++)
        input_off(t, base - 12u + CH[i]);
    rel_at = fpos;
    dc_end = fpos + FS / 2u;
    finish();
}

/* ------------------------------------------------------------- jobs --- */
enum { J_PRESET, J_MODE, J_SENDS, J_CPU, J_CHECK, J_SLICER };
typedef struct {
    char name[64];
    uint8_t kind, e, pi, arg, cpu_notes;
    const uint8_t (*parts)[3];                 /* J_CPU mixes */
    int (*check)(char *msg, uint32_t n);       /* J_CHECK */
    res_t r;
    int crashed;
} job_t;

static void job_preset(const job_t *j)
{
    track_t *t = &trk[0];
    host_tracks_init();
    host_preset(t, j->e, j->pi);
#if FELUCCA_CZ
    /* Casio's tones with no DCA sustain point play their whole envelope regardless of the key (as the CZ-1 does:
     * docs/CZ1.md): BELLS, SITAR and JET ROAR ring on long after the note-off */
    if (j->e == ENGI_CZ && (!strcmp(ENGINES[j->e]->presets[j->pi].name, "BELLS") ||
                            !strcmp(ENGINES[j->e]->presets[j->pi].name, "SITAR") ||
                            !strcmp(ENGINES[j->e]->presets[j->pi].name, "JET ROAR")))
        free_cap_s = 90;
#endif
#if FELUCCA_PROPHET
    /* a Prophet program's release is exponential with the time constant 10000^(raw / 127) ms (tools/gen_tables.py
     * P5_ENV_EXP): -60 dB after 6.9 of them, up to 69 s at 127 (the bank's longest is 120: 41 s) */
    if (j->e == ENGI_PROPHET)
        free_cap_s = 12u + (uint32_t)(9.0 * pow(10000.0, p5_patch_of(t)->raw[P5_RELEASE_AMP] / 127.0) / 1000.0);
#endif
    phrase(t, 60);
}

static void job_mode(const job_t *j)            /* arg: the voice mode, ARP off, glide on for MONO / LEGATO */
{
    track_t *t = &trk[0];
    host_tracks_init();
    host_preset(t, j->e, j->pi);
    t->p[P_VOICE] = j->arg;
    t->p[P_AMODE] = 0;
    t->p[P_GLIDE] = j->arg == V_MONO || j->arg == V_LEGATO ? 30 : 0;
    t->p[P_DETUNE] = 40;
    phrase(t, 60);
}

static void job_sends(const job_t *j)           /* arg: 0 dry, 1 chorus, 2 delay, 3 reverb, 4 all, 5 DIST */
{
    static const int16_t S[6][4] = {{0, 0, 0, 0}, {0, 110, 0, 0}, {0, 0, 110, 0}, {0, 0, 0, 110},
                                    {40, 80, 80, 80}, {110, 0, 0, 30}};
    track_t *t = &trk[0];
    uint32_t i;
    host_tracks_init();
    host_preset(t, j->e, j->pi);
    t->p[P_AMODE] = 0;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = S[j->arg][i];
    phrase(t, 60);
}

/* the SLICER on the phrase (its clock free-running, no transport): arg 0 GATE (VA SYNC BASS: R_ACID_*, pattern 6,
 * 1/16), 1 STUT (pattern 7, 1/32, DEPTH 100 %) */
static void job_slicer(const job_t *j)
{
    static const int16_t S[2][4] = {{SL_GATE, 6, 1, 120}, {SL_STUT, 7, 2, 127}};
    track_t *t = &trk[0];
    uint32_t i;
    host_tracks_init();
    host_preset(t, j->e, j->pi);
    t->p[P_AMODE] = 0;
    for (i = 0; i < 4u; i++)
        t->p[P_SLCR + i] = S[j->arg][i];
    phrase(t, 60);
}

/* CPU: parts[k] = {engine, preset, notes} (POLY, SUS 127, no ARP); 0.5 s to settle, then 1 s counted */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static void job_cpu(const job_t *j)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    const uint8_t (*parts)[3] = j->parts;
    uint32_t p, i, k, nb = FS / CTL;
    uint64_t i0, t0;
    host_tracks_init();
    for (p = 0; p < NPART; p++) {
        host_preset(&trk[p], parts[p][0], parts[p][1]);
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_AMODE] = 0;
        for (i = 0; i < parts[p][2]; i++)
            trk_note_on(&trk[p], NOTES[i] + 12u * p, 100);
    }
    for (k = 0; k < nb / 2u + nb; k++) {
        if (k == nb / 2u) {
            i0 = instr_now();
            t0 = now_ns();
        }
        mix_block(last_out, CTL);
    }
    R.ns = (double)(now_ns() - t0) / (nb * CTL);
    R.ipc = i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
}

static int run_job_body(job_t *j)
{
    memset(&R, 0, sizeof R);
    switch (j->kind) {
    case J_PRESET:
        job_preset(j);
        break;
    case J_MODE:
        job_mode(j);
        break;
    case J_SENDS:
        job_sends(j);
        break;
    case J_SLICER:
        job_slicer(j);
        break;
    case J_CPU:
        job_cpu(j);
        break;
    case J_CHECK:
        R.ok = (uint32_t)j->check(R.msg, sizeof R.msg);
        break;
    }
    return 0;
}

/* run jobs [0, n) in children, up to nj at once; results come back through pipes */
static void run_jobs(job_t *jobs, uint32_t n, uint32_t nj)
{
    uint32_t i, k;
    for (i = 0; i < n; i += nj) {
        int fd[64];
        pid_t pid[64];
        uint32_t m = n - i < nj ? n - i : nj;
        for (k = 0; k < m; k++) {
            int p[2];
            if (pipe(p)) {
                perror("pipe");
                exit(2);
            }
            fflush(stdout);
            if (!(pid[k] = fork())) {
                close(p[0]);
                run_job_body(&jobs[i + k]);
                if (write(p[1], &R, sizeof R) != sizeof R)
                    _exit(1);
                _exit(0);
            }
            close(p[1]);
            fd[k] = p[0];
        }
        for (k = 0; k < m; k++) {
            size_t got = 0;
            ssize_t r;
            int st = 0;
            while (got < sizeof(res_t) && (r = read(fd[k], (char *)&jobs[i + k].r + got, sizeof(res_t) - got)) > 0)
                got += (size_t)r;
            close(fd[k]);
            waitpid(pid[k], &st, 0);
            jobs[i + k].crashed = got != sizeof(res_t) || !WIFEXITED(st) || WEXITSTATUS(st);
        }
    }
}

/* ---------------------------------------------------------- voice checks --- */
/* ChoralRoot sounds two parts (chord: 0, bass: 1; core.h ENG_STATE_NPART, voice.c VB_NPART): they share the budget of
 * VBUDGET units on one core (an FM6 voice one unit, a VA voice two, a P5 voice three) */
#define CP 2u                                      /* the parts these checks play */
static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t n)
{
    lcg = lcg * 1103515245u + 12345u;
    return (lcg >> 8) % n;
}

/* the shared budget: the VA (part 1) and FM6 (part 2), POLY, play random notes on and off for 6 s, up to 8 held each;
 * after every block: at most NVOICE voices active, none still fading (a stolen voice fades within its one block);
 * then all off: every voice free */
static int chk_budget(char *msg, uint32_t n)
{
    static const uint8_t E[CP][2] = {{R_PLAT, 1}, {ENGI_FM6, 1}};
    uint8_t held[CP][128] = {{0}};
    uint32_t p, k, worst = 0, fading = 0, kills0 = voice_kills;
    host_tracks_init();
    for (p = 0; p < CP; p++) {
        host_preset(&trk[p], E[p][0], E[p][1]);
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_AMODE] = 0;
        trk[p].p[P_SUS] = 100;
    }
    for (k = 0; k < 6u * FS / CTL; k++) {
        uint32_t i, a = 0;
        if (k % 4u == 0u) {
            uint32_t note = 36u + rnd(48);
            p = rnd(CP);
            if (held[p][note]) {
                trk_note_off(&trk[p], note);
                held[p][note] = 0;
            } else {
                trk_note_on(&trk[p], note, 60u + rnd(60));
                held[p][note] = 1;
            }
        }
        blk();
        for (p = 0; p < CP; p++)
            for (i = 0; i < NVOICE; i++) {
                a += trk[p].v[i].active;
                fading += trk[p].v[i].active && trk[p].v[i].stage == 4u;
            }
        worst = a > worst ? a : worst;
    }
    for (p = 0; p < CP; p++)
        for (k = 0; k < 128u; k++)
            if (held[p][k])
                trk_note_off(&trk[p], k);
    rel_at = fpos;
    finish();
    snprintf(msg, n, "VA and FM6 POLY, random notes: at most %u voices active (budget %u units), %u voices taken, "
             "%u still fading after their block, all free %.2f s after the note-offs",
             worst, VBUDGET, voice_kills - kills0, fading, R.free_s);
    return worst <= NVOICE && !fading && R.free_s >= 0 && voice_kills > kills0;
}

static int gated(const track_t *t, uint32_t note)   /* a voice of t sounds note, its key down */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}

/* notes in one audio block can reuse a voice before its stolen tail fades: the budget full (the VA's 4 notes on each
 * part), a released note of part 1 taken by part 2's new one, then part 1 plays again (round 0: the same pitch; round 1:
 * P5 on part 1, its cap of 4 full, a new pitch replaces one of its own) */
static int chk_retrigger_budget(char *msg, uint32_t n)
{
    static const uint8_t NOTES[4] = {48, 52, 55, 60};
    uint32_t round, p, i, bad = 0, worst = 0;
    for (round = 0; round < 2u; round++) {
        voice_t *old = 0;
        host_tracks_init();
        for (p = 0; p < CP; p++) {
            memset(trk[p].v, 0, sizeof trk[p].v);
            host_preset(&trk[p], p == 0 && round ? ENGI_PROPHET : R_PLAT, 0);
            trk[p].p[P_VOICE] = V_POLY;
            trk[p].p[P_AMODE] = 0;
            trk[p].p[P_SUS] = 127;
        }
        for (i = 0; i < NELEM(NOTES); i++) {           /* the budget full: 4 x 2 + 4 x 2 units, or P5's 4 x 3 + 2 x 2 */
            trk_note_on(&trk[0], NOTES[i], 100);
            if (i < (round ? 2u : 4u))
                trk_note_on(&trk[1], 64u + 3u * i, 100);
        }
        blk();
        for (i = 0; i < NVOICE; i++)
            if (trk[0].v[i].active && trk[0].v[i].note == 52u)
                old = &trk[0].v[i];
        trk_note_off(&trk[0], 52);
        trk_note_on(&trk[1], 79, 100);
        bad += !old || old->stage != 4u || voices_busy() > VBUDGET;
        trk_note_on(&trk[0], round ? 57u : 52u, 100);
        i = voices_busy();
        worst = i > worst ? i : worst;
        bad += i > VBUDGET;
        blk();
        bad += voices_busy() > VBUDGET || !gated(&trk[1], 79);   /* (part 2's newest note sounds on) */
        for (p = 0; p < CP; p++)
            trk_all_off(&trk[p]);
        for (i = 0; i < 4u * FS / CTL && !parts_free(); i++)
            blk();
        bad += !parts_free();
    }
    snprintf(msg, n, "same-note retrigger and a full P5 cap's replacement before the fade: at most %u units (budget %u), "
             "all released", worst, VBUDGET);
    return !bad;
}

/* Voices held before a mode change must remain reclaimable except for the lead. */
static int chk_cap_mode_budget(char *msg, uint32_t n)
{
    static const uint8_t MODE[3] = {V_MONO, V_LEGATO, V_UNISON};
    uint32_t round, p, i, bad = 0, worst = 0;
    for (round = 0; round < NELEM(MODE); round++) {
        host_tracks_init();
        for (p = 0; p < CP; p++) {
            memset(trk[p].v, 0, sizeof trk[p].v);
            host_preset(&trk[p], R_PLAT, 0);
            trk[p].p[P_VOICE] = p ? V_MONO : V_POLY;
            trk[p].p[P_AMODE] = 0;
            trk[p].p[P_SUS] = 127;
        }
        for (i = 0; i < NVOICE; i++)
            trk_note_on(&trk[0], 48u + i * 2u, 100);
        trk[0].p[P_VOICE] = MODE[round];
        if (round == 2u) {
            /* A lower cap must not protect old upper slots while the lower extras fade. */
            host_preset(&trk[0], ENGI_PROPHET, 0);
            trk[0].p[P_VOICE] = V_UNISON;
            for (i = 1; i < trk_nvoice(&trk[0]); i++)
                voice_kill(&trk[0].v[i]);
        }
        trk_note_on(&trk[1], 73, 100);
        i = voices_busy();
        worst = i > worst ? i : worst;
        bad += i > VBUDGET || !trk[0].v[0].gate || trk[0].v[0].note != 48u;
        trk[1].p[P_VOICE] = V_POLY;
        trk_note_on(&trk[1], 84, 100);
        i = voices_busy();
        worst = i > worst ? i : worst;
        bad += i > VBUDGET || !trk[0].v[0].gate;
        blk();
        bad += voices_busy() > VBUDGET || !trk[0].v[0].gate || !trk[1].v[0].gate;
        for (p = 0; p < CP; p++)
            trk_all_off(&trk[p]);
        for (i = 0; i < 4u * FS / CTL && !parts_free(); i++)
            blk();
        bad += !parts_free();
    }
    snprintf(msg, n, "POLY -> MONO / LEGATO and P5's lower UNISON cap: at most %u units, the lead kept, all released",
             worst);
    return !bad;
}

/* A live mode change can leave an old chord in slots above the new lead. */
static int chk_mode_budget(char *msg, uint32_t n)
{
    uint32_t mode, initial, p, i, bad = 0;
    for (initial = 0; initial < 2u; initial++)
        for (mode = V_MONO; mode <= V_UNISON; mode++) {
            host_tracks_init();
            for (p = 0; p < CP; p++) {
                memset(trk[p].v, 0, sizeof trk[p].v);
                host_preset(&trk[p], R_PLAT, 0);
                trk[p].p[P_VOICE] = V_MONO;
                trk[p].p[P_AMODE] = 0;
            }
            trk[0].p[P_VOICE] = initial ? V_UNISON : V_POLY;
            if (initial)
                trk_note_on(&trk[0], 48, 100);
            else
                for (i = 0; i < NVOICE; i++) trk_note_on(&trk[0], 48u + 2u * i, 100);
            bad += voices_busy() != VBUDGET;
            trk[0].p[P_VOICE] = (int16_t)mode;
            trk_note_on(&trk[1], 73, 100);
            bad += voices_busy() > VBUDGET || !trk[0].v[0].gate || !trk[1].v[0].gate;
            blk();
            bad += voices_busy() > VBUDGET;
            for (p = 0; p < CP; p++) trk_all_off(&trk[p]);
        }
    snprintf(msg, n, "POLY / UNISON chord -> MONO / LEGATO / UNISON: extra voices reclaimed, leads kept, budget %u",
             VBUDGET);
    return !bad;
}

static void mode_release_reset(uint32_t engine, uint32_t mode)
{
    uint32_t p;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    for (p = 0; p < CP; p++) {
        host_preset(&trk[p], p ? R_PLAT : engine, 0);
        trk[p].p[P_VOICE] = p ? V_POLY : mode;
        trk[p].p[P_AMODE] = trk[p].p[P_REL] = 0;
        trk[p].p[P_SUS] = 127;
    }
}

static uint32_t mode_release_gates(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += trk[0].v[i].gate != 0;
    return n;
}

/* A key-up must release its voices even after a live VCE change. */
static int chk_mode_release(char *msg, uint32_t n)
{
    uint32_t from, to, engine, order, i, mode, priority, bad = 0, cases = 0;
    for (engine = 0; engine < 2u; engine++)
        for (from = V_POLY; from <= V_UNISON; from++)
            for (to = V_POLY; to <= V_UNISON; to++)
                for (order = 0; order < 2u; order++) {
                    mode_release_reset(engine ? ENGI_PROPHET : R_PLAT, from);
                    trk_note_on(&trk[0], 60, 100);
                    trk_note_on(&trk[0], 64, 100);
                    for (i = 0; i < 8u; i++) blk();
                    trk[0].p[P_VOICE] = (int16_t)to;
                    trk_note_off(&trk[0], order ? 60u : 64u);
                    trk_note_off(&trk[0], order ? 64u : 60u);
                    bad += mode_release_gates() != 0 || trk[0].nmono != 0 || voices_busy() > VBUDGET;
                    for (i = 0; i < 4u * FS / CTL && !parts_free(); i++) blk();   /* (the VA's own release) */
                    bad += !parts_free();
                    cases++;
                }
    for (mode = V_MONO; mode <= V_UNISON; mode++) {
        mode_release_reset(R_PLAT, mode);
        trk_note_on(&trk[0], 60, 100);
        trk_note_on(&trk[0], 64, 100);
        trk[0].p[P_VOICE] = V_POLY;
        trk_note_off(&trk[0], 64);
        trk[0].p[P_VOICE] = (int16_t)mode;
        trk_note_off(&trk[0], 60);
        trk_note_on(&trk[0], 72, 100);
        trk_note_off(&trk[0], 72);
        bad += mode_release_gates() != 0 || trk[0].nmono != 0;
        cases++;
        for (priority = 0; priority < 3u; priority++) {
            uint32_t lead = priority == 1u ? 72u : 80u, other = priority == 1u ? 80u : 72u;
            uint32_t want = mode == V_UNISON ? trk_nvoice(&trk[0]) : 1u;   /* (Melodee's voice model) */
            mode_release_reset(R_PLAT, V_POLY);
            trk_note_on(&trk[0], 48, 100);
            trk_note_on(&trk[0], 64, 100);
            trk[0].p[P_VOICE] = (int16_t)mode;
            trk[0].p[P_PRIO] = (int16_t)priority;
            trk_note_on(&trk[0], 72, 100);
            trk_note_on(&trk[0], 76, 100);
            trk_note_on(&trk[0], 80, 100);
            trk_note_off(&trk[0], 48);
            trk_note_off(&trk[0], 64);
            trk_note_off(&trk[0], other);
            bad += mode_release_gates() != want || trk[0].v[0].note != lead || trk[0].mono_note != lead;
            trk_note_off(&trk[0], lead);
            bad += mode_release_gates() != want || trk[0].v[0].note != 76u || trk[0].mono_note != 76u;
            trk_note_off(&trk[0], 76);
            bad += mode_release_gates() != 0 || trk[0].nmono != 0 || voices_busy() > VBUDGET;
            cases++;
        }
    }
    snprintf(msg, n, "%u cases: all 16 VCE pairs, both key-up orders and caps 8 / 4 (the VA, P5); no stale fallback, "
             "MONO / LEGATO / UNISON priority leads and fallback preserved", cases);
    return !bad;
}

/* a plain held sine (CZ-1's INIT TONE: a sine, its DCA held; no sends): smooth, so the largest sample step is set by
 * the pitches */
static void plain_sine(track_t *t)
{
    host_preset(t, ENGI_CZ, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_AMODE] = 0;
}
/* a stolen voice fades: plain sines on the two parts; part 1 holds 7 notes, part 2 one (the budget full), then part 2
 * takes voices from part 1 in turn. The largest sample step in the 2 blocks of each take must stay within 1.5 x the
 * largest one in the 0.25 s before it (a hard cut is several times larger) */
static int chk_steal_fade(char *msg, uint32_t n)
{
    static const uint8_t N1[7] = {48, 52, 55, 59, 62, 65, 69};
    const uint32_t frames = 4u * FS;
    int32_t *L = calloc(frames + CTL, sizeof *L);
    uint32_t f, i, p, k, ev[8], nev = 0, bad = 0;
    double worst = 0;
    int32_t wat = 0, wcalm = 0;
    host_tracks_init();
    for (p = 0; p < CP; p++)
        plain_sine(&trk[p]);
    for (f = 0; f < frames; f += CTL) {
        uint32_t ms = f * 1000u / FS, k0 = voice_kills;
        if (f == 0)
            for (i = 0; i < 7u; i++)
                trk_note_on(&trk[0], N1[i], 90);
        if (f == at(0.5))
            trk_note_on(&trk[1], 74, 90);
        if (ms >= 1000u && f % (FS / 2u) < CTL && ms < 3500u)
            trk_note_on(&trk[1], 76u + ms / 250u, 90);
        blk();
        if (voice_kills != k0 && nev < 8u)
            ev[nev++] = f;
        for (i = 0; i < CTL; i++)
            L[f + i] = last_out[2 * i];
    }
    for (k = 0; k < nev; k++) {
        int32_t calm = 1, a = 0;
        for (i = ev[k] - FS / 4u; i < ev[k] - 2u * CTL; i++)
            calm = abs(L[i] - L[i - 1]) > calm ? abs(L[i] - L[i - 1]) : calm;
        for (i = ev[k]; i < ev[k] + 2u * CTL; i++)
            a = abs(L[i] - L[i - 1]) > a ? abs(L[i] - L[i - 1]) : a;
        if ((double)a / calm > worst) {
            worst = (double)a / calm;
            wat = a;
            wcalm = calm;
        }
        bad += a * 2 > calm * 3;
    }
    free(L);
    snprintf(msg, n, "%u takes; largest sample step at a take %d vs %d before it (%.2f x, limit 1.50 x)", nev, wat,
             wcalm, worst);
    return nev >= 4u && !bad;
}
/* MONO / LEGATO / UNISON keep their note: part 1 (the VA) holds note 40 in mode m while part 2 (the VA, POLY) floods
 * the budget with new notes for 2 s (the budget full: part 2 takes its own voices, or UNISON's extras of part 1);
 * part 1's voice 0 must sound note 40, gate on, every block */
static int chk_keep(char *msg, uint32_t n, uint32_t mode)
{
    static const char *const MN[4] = {"POLY", "MONO", "LEGATO", "UNISON"};
    uint32_t k, lost = 0, full = 0, kills0 = voice_kills;
    host_tracks_init();
    host_preset(&trk[0], R_PLAT, 0);
    trk[0].p[P_VOICE] = (int16_t)mode;
    trk[0].p[P_AMODE] = 0;
    trk[0].p[P_SUS] = 100;
    host_preset(&trk[1], R_PLAT, 1);
    trk[1].p[P_VOICE] = V_POLY;
    trk[1].p[P_AMODE] = 0;
    trk[1].p[P_SUS] = 100;
    trk_note_on(&trk[0], 40, 100);
    for (k = 0; k < 2u * FS / CTL; k++) {
        if (k % 3u == 0u)
            trk_note_on(&trk[1], 50u + k / 3u % 40u, 100);
        full += voices_busy() >= VBUDGET;
        blk();
        {
            const voice_t *v = &trk[0].v[0];
            lost += !(v->active && v->gate && v->note == 40u && v->stage != 4u);
        }
    }
    snprintf(msg, n, "%s part held its note with the budget full in %u blocks, %u voices taken from it: lost in %u "
             "blocks", MN[mode], full, voice_kills - kills0, lost);
    return !lost && full > 100u && (mode != V_UNISON || voice_kills > kills0);
}
static int chk_keep_mono(char *m, uint32_t n) { return chk_keep(m, n, V_MONO); }
static int chk_keep_legato(char *m, uint32_t n) { return chk_keep(m, n, V_LEGATO); }
static int chk_keep_unison(char *m, uint32_t n) { return chk_keep(m, n, V_UNISON); }

/* P5's cap on one core (eng_prophet_test.c p5_cap): 8 keys in POLY and in UNISON on the chord part (alone: the budget
 * does not limit it), at most its P5_CORE voices in POLY and its stack in UNISON; all freed after the release */
static int chk_voice_cap(char *msg, uint32_t n)
{
    uint32_t mode, k, i, most[2] = {0, 0}, cap[2] = {0, 0};
    for (mode = 0; mode < 2u; mode++) {
        track_t *t = &trk[0];
        host_tracks_init();
        host_preset(t, ENGI_PROPHET, 0);
        t->p[P_VOICE] = mode ? V_UNISON : V_POLY;
        t->p[P_AMODE] = 0;
        cap[mode] = trk_nvoice(t);
        for (k = 0; k < 8u; k++)
            trk_note_on(t, 60u + 2u * k, 100);
        for (k = 0; k < FS / 2u / CTL; k++) {
            uint32_t a = 0;
            blk();
            for (i = 0; i < NVOICE; i++)
                a += t->v[i].active;
            most[mode] = a > most[mode] ? a : most[mode];
        }
        for (k = 0; k < 8u; k++)
            trk_note_off(t, 60u + 2u * k);
        for (k = 0; k < 4u * FS / CTL && !parts_free(); k++)
            blk();
        if (!parts_free())
            most[mode] = 99;
    }
    snprintf(msg, n, "P5, 8 keys: at most %u voices in POLY (cap %u), %u in UNISON (cap %u); freed after release",
             most[0], cap[0], most[1], cap[1]);
    return most[0] == cap[0] && cap[0] == P5_CORE && most[1] == cap[1] && cap[1] <= NPOLY;
}

/* ------------------------------------------------------------ files --- */
typedef struct { char name[64]; char val[64]; } kv_t;
static uint32_t load_kv(const char *path, kv_t *kv, uint32_t max)
{
    FILE *f = fopen(path, "r");
    char line[256];
    uint32_t n = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f) && n < max) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%63s %63s", kv[n].name, kv[n].val) == 2)
            n++;
    }
    fclose(f);
    return n;
}
static const char *kv_get(const kv_t *kv, uint32_t n, const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (!strcmp(kv[i].name, name))
            return kv[i].val;
    return 0;
}

static void slug(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; *s && i + 1 < n; s++)
        d[i++] = (*s == ' ' || *s == '/') ? '_' : *s;
    d[i] = 0;
}

/* ------------------------------------------------------------- main --- */
#define MAXJ 512
static job_t J[MAXJ];
static uint32_t nj;
static job_t *add(uint32_t kind, const char *name)
{
    job_t *j = &J[nj++];
    memset(j, 0, sizeof *j);
    j->kind = (uint8_t)kind;
    snprintf(j->name, sizeof j->name, "%s", name);
    return j;
}

int main(int argc, char **argv)
{
    const char *gpath = argc > 1 ? argv[1] : "tests/golden.txt";
    const char *cpath = argc > 2 ? argv[2] : "tests/cpu_baseline.txt";
    int gupd = getenv("GOLDEN_UPDATE") != 0, cupd = getenv("BUDGET_UPDATE") != 0, verbose = getenv("VERBOSE") != 0;
    uint32_t jobs_at_once = getenv("JOBS") ? (uint32_t)atoi(getenv("JOBS")) : 8u;
    static const char *const MN[4] = {"POLY", "MONO", "LEGATO", "UNISON"};
    static const char *const SN[6] = {"dry", "chorus", "delay", "reverb", "all", "dist"};
    static const uint8_t MODE_E[3][2] = {{R_PLAT, 0}, {ENGI_FM6, 1}, {ENGI_PROPHET, 0}};   /* engine, preset */
    static const uint8_t SEND_E[1][2] = {{R_PLAT, 3}};
    char en[24];
    static uint8_t cpu_parts[MAXJ][NPART][3];
    static kv_t gold[MAXJ], cpu[MAXJ];
    uint32_t ng, nc, e, pi, i, g0, g1, c0, c1, k0, ncpu = 0;
    uint32_t g_changed = 0, g_new = 0, g_gone = 0, h_fail = 0, c_fail = 0, c_warn = 0, k_fail = 0, crash = 0;
    double heavy[NENGINES] = {0}, heavy_ns[NENGINES] = {0}, idle_now, idle_base;
    uint32_t heavy_p[NENGINES] = {0};
    uint64_t t_start = now_ns();
    char name[64], s[64];
    if (jobs_at_once < 1u || jobs_at_once > 64u)
        jobs_at_once = 8;

    /* 1 + 2: the golden renders */
    g0 = nj;
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            job_t *j;
            slug(s, ENGINES[e]->presets[pi].name, sizeof s);
            slug(en, ENGINES[e]->name, sizeof en);   /* ("FM TONE": FM_TONE; the golden file splits at spaces) */
            snprintf(name, sizeof name, "preset/%s/%02u_%s", en, pi, s);
#if FELUCCA_PROPHET
            /* P5's programs are a list that changes (assets/prophet5-factory/shipped.txt): an entry is named by the
             * program's Sequential number (000: INIT), so a list changed adds and drops lines and moves none */
            if (e == ENGI_PROPHET)
                snprintf(name, sizeof name, "preset/%s/%03u_%s", en, (unsigned)p5_preset_id(pi), s);
#endif
            j = add(J_PRESET, name);
            j->e = (uint8_t)e;
            j->pi = (uint8_t)pi;
        }
    for (i = 0; i < 3u; i++)
        for (k0 = 0; k0 < 4u && eng_ok(MODE_E[i][0]); k0++) {
            job_t *j;
            slug(en, ENGINES[MODE_E[i][0]]->name, sizeof en);
            snprintf(name, sizeof name, "mode/%s/%s", en, MN[k0]);
            j = add(J_MODE, name);
            j->e = MODE_E[i][0];
            j->pi = MODE_E[i][1];
            j->arg = (uint8_t)k0;
        }
    for (i = 0; i < NELEM(SEND_E); i++)
        for (k0 = 0; k0 < 6u && eng_ok(SEND_E[i][0]); k0++) {
            job_t *j;
            slug(en, ENGINES[SEND_E[i][0]]->name, sizeof en);
            snprintf(name, sizeof name, "sends/%s/%s", en, SN[k0]);
            j = add(J_SENDS, name);
            j->e = SEND_E[i][0];
            j->pi = SEND_E[i][1];
            j->arg = (uint8_t)k0;
        }
    {   /* the SLICER (slicer.c: in the emulator; the unit builds it out, FELUCCA_SLICER 0) */
        job_t *j = add(J_SLICER, "slicer/gate/" R_ACID_NAME);
        j->e = R_ACID_E, j->pi = R_ACID_P, j->arg = 0;
    }
    g1 = nj;
    {   /* determinism: the first preset render once more */
        job_t *j = add(J_PRESET, "repeat");
        *j = J[g0];
        snprintf(j->name, sizeof j->name, "repeat");
    }

    /* 4: the voice checks */
    k0 = nj;
    add(J_CHECK, "voices: the budget across the chord and bass parts")->check = chk_budget;
    add(J_CHECK, "voices: reuse before the stolen tail fades")->check = chk_retrigger_budget;
    add(J_CHECK, "voices: budget after a live mode change")->check = chk_mode_budget;
    add(J_CHECK, "voices: budget after voice mode changes")->check = chk_cap_mode_budget;
    add(J_CHECK, "voices: note-offs after live voice mode changes")->check = chk_mode_release;
    add(J_CHECK, "voices: a stolen voice fades")->check = chk_steal_fade;
    add(J_CHECK, "voices: MONO keeps its note")->check = chk_keep_mono;
    add(J_CHECK, "voices: LEGATO keeps its note")->check = chk_keep_legato;
    add(J_CHECK, "voices: UNISON keeps its note")->check = chk_keep_unison;
    add(J_CHECK, "voices: P5's cap on one core")->check = chk_voice_cap;
    run_jobs(J, nj, jobs_at_once);

    /* 3: CPU, one child at a time (the ns are for information; the instruction counts do not care) */
    c0 = nj;
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            job_t *j;
            slug(s, ENGINES[e]->presets[pi].name, sizeof s);
            slug(en, ENGINES[e]->name, sizeof en);
            snprintf(name, sizeof name, "cpu/%s/%02u_%s", en, pi, s);
#if FELUCCA_PROPHET
            if (e == ENGI_PROPHET)
                snprintf(name, sizeof name, "cpu/%s/%03u_%s", en, (unsigned)p5_preset_id(pi), s);
#endif
            j = add(J_CPU, name);
            memset(cpu_parts[ncpu], 0, sizeof cpu_parts[ncpu]);
            cpu_parts[ncpu][0][0] = (uint8_t)e;
            cpu_parts[ncpu][0][1] = (uint8_t)pi;
            cpu_parts[ncpu][0][2] = 8;
            j->parts = (const uint8_t (*)[3])cpu_parts[ncpu++];
            j->e = (uint8_t)e;
            j->pi = (uint8_t)pi;
        }
    {   /* mixes: idle (subtracted from the presets' counts), the chord and bass parts full: FM6 and the VA asking 8 + 8 (the
         * budget keeps 8 VA voices and 8 FM6 ones: 24 units of 16) */
        job_t *j = add(J_CPU, "cpu/mix/idle");
        memset(cpu_parts[ncpu], 0, sizeof cpu_parts[ncpu]);
        j->parts = (const uint8_t (*)[3])cpu_parts[ncpu++];
        j->e = 0xFF;
        j = add(J_CPU, "cpu/mix/2parts_full");
        memset(cpu_parts[ncpu], 0, sizeof cpu_parts[ncpu]);
        cpu_parts[ncpu][0][0] = ENGI_FM6, cpu_parts[ncpu][0][1] = 0, cpu_parts[ncpu][0][2] = 8;
        cpu_parts[ncpu][1][0] = ENGI_VA, cpu_parts[ncpu][1][1] = 0, cpu_parts[ncpu][1][2] = 8;
        j->parts = (const uint8_t (*)[3])cpu_parts[ncpu++];
        j->e = 0xFF;
    }
    c1 = nj;
    run_jobs(J + c0, c1 - c0, 1);

    /* ---- report: goldens and health ---- */
    ng = load_kv(gpath, gold, MAXJ);
    for (i = g0; i < g1; i++) {
        job_t *j = &J[i];
        const res_t *r = &j->r;
        const char *want = kv_get(gold, ng, j->name);
        char hs[24], why[256] = "";
        snprintf(hs, sizeof hs, "%016llx", (unsigned long long)r->hash);
        if (j->crashed) {
            printf("regress: CRASH  %s\n", j->name);
            crash++;
            continue;
        }
        if (!want)
            g_new++;
        else if (strcmp(want, hs)) {
            if (!gupd)
                printf("regress: GOLDEN CHANGED  %s  (%s -> %s)\n", j->name, want, hs);
            g_changed++;
        }
        if (r->over)
            snprintf(why + strlen(why), sizeof why - strlen(why), " %u samples beyond 16 bits;", r->over);
        if (r->near)
            snprintf(why + strlen(why), sizeof why - strlen(why), " %u samples >= %d;", r->near, LIM_NEAR);
        if (fabs(r->dc) > LIM_DC)
            snprintf(why + strlen(why), sizeof why - strlen(why), " DC %.1f (limit %d);", r->dc, LIM_DC);
        if (r->peak < LIM_PEAK_MIN)
            snprintf(why + strlen(why), sizeof why - strlen(why), " peak %d (min %d);", r->peak, LIM_PEAK_MIN);
        if (r->free_s < 0)
            snprintf(why + strlen(why), sizeof why - strlen(why), " voices not free %d s after the note-offs;", FREE_CAP_S);
        if (abs(r->tail_dc) > LIM_TAIL_DC)
            snprintf(why + strlen(why), sizeof why - strlen(why), " DC %d left at the end (limit %d);", r->tail_dc,
                     LIM_TAIL_DC);
        if (r->tail_peak > LIM_TAIL)
            snprintf(why + strlen(why), sizeof why - strlen(why), " not silent at the end: peak %d (limit %d);",
                     r->tail_peak, LIM_TAIL);
        if (r->vmax > NVOICE)
            snprintf(why + strlen(why), sizeof why - strlen(why), " %u voices (budget %u);", r->vmax, NVOICE);
        if (why[0]) {
            printf("regress: HEALTH FAIL  %s:%s\n", j->name, why);
            h_fail++;
        }
        if (verbose)
            printf("  %-44s %s  %5.1f s  peak %5d  dc %6.1f  free %5.2f s  tail %3d dc %3d  voices %u\n", j->name,
                   hs, (double)r->frames / FS, r->peak, r->dc, r->free_s, r->tail_peak, r->tail_dc, r->vmax);
    }
    for (i = 0; i < ng; i++) {                      /* in the file, not rendered any more */
        uint32_t k, found = 0;
        for (k = g0; k < g1 && !found; k++)
            found = !strcmp(J[k].name, gold[i].name);
        if (!found) {
            if (!gupd)
                printf("regress: GOLDEN GONE  %s (no such render now)\n", gold[i].name);
            g_gone++;
        }
    }
    if (J[g1].r.hash != J[g0].r.hash || J[g1].crashed) {
        printf("regress: NOT DETERMINISTIC  %s rendered twice: %016llx, %016llx\n", J[g0].name,
               (unsigned long long)J[g0].r.hash, (unsigned long long)J[g1].r.hash);
        h_fail++;
    }
    if (gupd) {
        FILE *f = fopen(gpath, "w");
        if (!f) {
            perror(gpath);
            return 2;
        }
        fprintf(f, "# ChoralRoot golden renders (tests/regress.c): name, FNV-1a 64 of the output samples.\n"
                   "# Rewritten by GOLDEN_UPDATE=1 tests/run_tests.sh -- only for an intended change of the sound.\n");
        for (i = g0; i < g1; i++)
            fprintf(f, "%s %016llx\n", J[i].name, (unsigned long long)J[i].r.hash);
        fclose(f);
        printf("regress: golden file %s rewritten (%u renders; %u had changed, %u new, %u gone)\n", gpath, g1 - g0,
               g_changed, g_new, g_gone);
    } else if (g_new) {
        printf("regress: %u renders not in %s (new: GOLDEN_UPDATE=1 adds them)\n", g_new, gpath);
    }

    /* ---- voices ---- */
    for (i = k0; i < c0; i++) {
        int ok = !J[i].crashed && J[i].r.ok;
        printf("regress: %s: %s %s\n", J[i].name, J[i].crashed ? "crashed" : J[i].r.msg, ok ? "ok" : "FAIL");
        k_fail += !ok;
    }

    /* ---- CPU ---- */
    /* a preset's own cost: its count less the idle mix's (the mix alone is half of a light preset's),
     * so +25 % means 25 % more engine work; the mixes as they are */
    nc = load_kv(cpath, cpu, MAXJ);
    idle_now = J[c1 - 2u].r.ipc;
    idle_base = kv_get(cpu, nc, J[c1 - 2u].name) ? atof(kv_get(cpu, nc, J[c1 - 2u].name)) : 0;
    for (i = c0; i < c1; i++) {
        const job_t *j = &J[i];
        const char *want = kv_get(cpu, nc, j->name);
        int own = j->e < NENGINES && idle_base > 0;
        double b = want ? atof(want) - (own ? idle_base : 0) : 0, ipc = j->r.ipc - (own ? idle_now : 0);
        if (j->crashed) {
            printf("regress: CRASH  %s\n", j->name);
            crash++;
            continue;
        }
        if (j->e < NENGINES && j->r.ipc > heavy[j->e]) {
            heavy[j->e] = j->r.ipc;
            heavy_ns[j->e] = j->r.ns;
            heavy_p[j->e] = j->pi;
        }
        if (!j->r.ipc || cupd)
            continue;
        if (!want)
            printf("regress: CPU %s: %.0f instructions / sample, no baseline (BUDGET_UPDATE=1 adds it)\n", j->name, ipc);
        else if (ipc > b * (1 + CPU_TOL) && ipc > b + CPU_SLACK) {
            printf("regress: CPU OVER BUDGET  %s: %.0f instructions / sample%s, baseline %.0f (+%.0f %%, limit +%.0f %%)\n",
                   j->name, ipc, own ? " over the idle mix" : "", b, (ipc / b - 1) * 100, CPU_TOL * 100);
            c_fail++;
        } else if (ipc < b * (1 - CPU_TOL) && ipc < b - CPU_SLACK) {
            printf("regress: CPU note  %s: %.0f instructions / sample, baseline %.0f (%.0f %%): faster? "
                   "BUDGET_UPDATE=1 to keep it\n", j->name, ipc, b, (ipc / b - 1) * 100);
            c_warn++;
        }
    }
    if (!J[c0].r.ipc)
        printf("regress: CPU: no instruction counter on this host (proc_pid_rusage); the budget is not checked\n");
    else if (cupd) {
        FILE *f = fopen(cpath, "w");
        if (!f) {
            perror(cpath);
            return 2;
        }
        fprintf(f, "# ChoralRoot host CPU baseline (tests/regress.c): instructions per 44.1 kHz sample, cc -O2 on\n"
                   "# the Mac (kernel-counted, ~1 %% run to run). The check allows +%.0f %%. Rewritten by BUDGET_UPDATE=1.\n",
                CPU_TOL * 100);
        for (i = c0; i < c1; i++)
            fprintf(f, "%s %.0f\n", J[i].name, J[i].r.ipc);
        fclose(f);
        printf("regress: CPU baseline %s rewritten (%u entries)\n", cpath, c1 - c0);
    }
    printf("regress: CPU, one part with 8 notes held, heaviest preset per engine (instructions / ns per sample):\n");
    for (e = 0; e < NENGINES; e++)
        if (eng_ok(e))
            printf("regress:   %-8s %-14s %6.0f instr  %6.1f ns\n", ENGINES[e]->name, ENGINES[e]->presets[heavy_p[e]].name,
                   heavy[e], heavy_ns[e]);
    for (i = c1 - 2u; i < c1; i++)
        printf("regress:   %-23s %6.0f instr  %6.1f ns\n", J[i].name + 8, J[i].r.ipc, J[i].r.ns);

    printf("regress: %u golden renders (%u changed, %u gone), %u health failures, %u voice / routing checks failed, "
           "%u CPU entries over budget (%u notes), %u crashes; %.1f s\n", g1 - g0, gupd ? 0 : g_changed,
           gupd ? 0 : g_gone, h_fail, k_fail, cupd ? 0 : c_fail, c_warn, crash, (double)(now_ns() - t_start) / 1e9);
    if (!gupd && (g_changed || g_gone))
        printf("regress: the sound changed. If that is intended: GOLDEN_UPDATE=1 sh tests/run_tests.sh, "
               "then review and commit tests/golden.txt\n");
    return (!gupd && (g_changed || g_gone)) || h_fail || k_fail || (!cupd && c_fail) || crash;
}
