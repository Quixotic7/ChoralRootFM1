/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* PHYS engine test (src/eng_phys.c, src/phys_dsp.c, phys_symp.c) on the Mac, through hostsim.c
 * as regress.c.
 *   build/host/phys_test [DEMODIR]          (run_tests.sh: build/phys_demo)
 * 1. stability: every model over notes 0..127 (C-1 .. G9)
 *    and the corners of STRC, BRIT, DAMP, POS, BOW and EXC (full accent), one strike / pluck / hit each: the
 *    model output (Q20, before the engine's gain and knee) stays within +-16 (the strings +-24: they clip at
 *    +-20 by design, DaisySP's clamp; SYMP +-32: that string and its sympathetic strings), the mode states
 *    never reach their clamp; from C1 no DC above 2 % of the peak, and from C3 with DAMP 0 (not bowed) the
 *    sound has decayed by 20 dB in 1.5 s (SYMP 14 dB: its sympathetic strings ring on) (no hang; how
 *    fast is DaisySP's: a bright string rings on, its loop filter opens with BRIGHTNESS); then per model a
 *    6 s torture render: every parameter and the pitch swept over their whole range with a retrigger every
 *    0.1 s and the model switching under the notes.
 * 2. cost: the heaviest settings of each model (8 notes asked, the engine's 3 voices, as regress.c's CPU
 *    check: instructions per sample of the whole mix less the idle mix) against PHASE WIRE, the heaviest
 *    factory preset (tests/cpu_baseline.txt): at most that; the models added in 1.0 (MEMB, SYMP) at
 *    most MODAL struck (the cost measured on the device, ~13 % per voice).
 * 3. demos: each factory preset playing a phrase, each model over C1 .. C7, into DEMODIR. (The drums, PHYS's
 *    MODEL DRUM before 1.0, are the DRUM engine: tests/drum_test.c.) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int fails;
static const char *const MN[PM_COUNT] = {"MODAL", "STRING", "MEMB", "SYMP"};

/* ------------------------------------------------------------ stability --- */
typedef struct { int32_t peak; int64_t sum; uint32_t n, clamp; int32_t tail; } st_t;

/* one model, direct DSP calls (as eng_phys.c phys_render, without its gain): n samples of note at the params
 * (Q16 0..1: STRC BRIT DAMP POS ACC BOW EXC), struck / plucked at 0 */
static void stab_render(uint32_t md, uint32_t note, const int32_t *q, uint32_t n, st_t *r)
{
    static phys_slot_t S;
    uint32_t f0 = pitch_inc(note * 16u > 2047u ? 2047u : note * 16u), i, k, fs[PX_NSYMP];
    int32_t y[CTL], ax[CTL];
    memset(r, 0, sizeof *r);
    phys_reset(&S, md, 0x2468ACEu + note);
    if (md == PM_STRING || md == PM_SYMP) {
        S.u.s.trig = 1;
    } else {
        S.u.m.trig = 1;
    }
    for (k = 0; k < PX_NSYMP; k++) {                    /* SYMP's strings, as phys_render (ROOT: C) */
        uint32_t c = (uint32_t)q[0] * 8u >> 16;
        int32_t p16 = PHYS_DETUNE[k] + (c < 7u ? (int32_t)note * 16 + ((PHYS_CHORDS[c][k] * 41) >> 8)
                             : (48 + (k == 1u ? 7 : k == 2u ? 12 : 0)) * 16);
        fs[k] = pitch_inc(p16 < 0 ? 0 : p16 > 2047 ? 2047 : p16);
    }
    for (i = 0; i < n; i += CTL) {
        for (k = 0; k < CTL; k++)
            ax[k] = 0;
        if (md == PM_MODAL || md == PM_MEMB) {
            px_modal_blk_t B;
            if (md == PM_MODAL)
                px_modal_block(&B, &S.u.m, f0, q[0], q[1], q[2], q[4], q[3] >> 2, q[5]);
            else
                px_memb_block(&B, &S.u.m, f0, q[0], q[1], q[2], q[4], q[3], q[5] ? 12 << 16 : 0);
            px_modal_run(&B, &S.u.m, y, ax, CTL);
            for (k = 0; k < B.n; k++)
                r->clamp += S.u.m.s[k][0] == (1 << 28) || S.u.m.s[k][0] == -(1 << 28) ||
                            S.u.m.s[k][1] == (1 << 28) || S.u.m.s[k][1] == -(1 << 28);
        } else if (md == PM_STRING) {
            px_string_blk_t B;
            px_string_block(&B, &S.u.s, f0, q[0], q[1], q[2], q[4], q[5], q[3]);
            px_string_excite(&B, &S.u.s, ax, CTL);
            px_string_run(&B, &S.u.s, ax, y, CTL);
        } else {
            px_string_blk_t B;
            px_string_block(&B, &S.u.y.s, f0, q[5] ? 0 : 16384, q[1], q[2], q[4], 0, 0);
            px_string_excite(&B, &S.u.y.s, ax, CTL);
            px_string_run(&B, &S.u.y.s, ax, y, CTL);
            px_symp_block(&S.u.y, fs, q[1], q[2], q[3]);
            px_symp_run(&S.u.y, y, y, CTL);
        }
        for (k = 0; k < CTL; k++) {
            int32_t s = y[k] + px_m(ax[k], q[6] >> 1, 15), a = s < 0 ? -s : s;
            if (a > r->peak)
                r->peak = a;
            r->sum += s;
            r->n++;
            if (i + k >= n - n / 6u && a > r->tail)       /* the last sixth */
                r->tail = a;
        }
    }
}

static void stability(void)
{
    static const uint8_t NOTES[] = {0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 127};
    static const int32_t STRC[] = {0, 16384, 17039, 39321, 65535}, C2[] = {0, 65535};
    uint32_t md, ni, a, b, c, d, e, cases = 0, bad = 0;
    int32_t worst[PM_COUNT] = {0};
    for (md = 0; md < PM_COUNT; md++)
        for (ni = 0; ni < sizeof NOTES; ni++)
            for (a = 0; a < 5u; a++)
                for (b = 0; b < 2u; b++)
                    for (c = 0; c < 3u; c++)
                        for (d = 0; d < 2u; d++)
                            for (e = 0; e < 2u; e++) {
                                /* STRC, BRIT, DAMP, POS, ACC, BOW, EXC */
                                int32_t q[7] = {STRC[a], C2[b], (int32_t)c * 32767, C2[d], 65535, C2[e] * 2 / 5, 65535};
                                uint32_t note = NOTES[ni], strg = md == PM_STRING || md == PM_SYMP;
                                st_t r;
                                char why[160] = "";
                                uint32_t n = FS * 3u / 2u;
                                stab_render(md, note, q, n, &r);
                                cases++;
                                if (r.peak > worst[md])
                                    worst[md] = r.peak;
                                if (r.peak > (md == PM_SYMP ? 32 << 20 : strg ? 24 << 20 : 16 << 20))
                                    snprintf(why + strlen(why), sizeof why - strlen(why), " peak %.1f;",
                                             r.peak / 1048576.0);
                                if (r.clamp)
                                    snprintf(why + strlen(why), sizeof why - strlen(why), " %u states clamped;", r.clamp);
                                if (note >= 24u && llabs(r.sum / (int64_t)r.n) > r.peak / 50 + 64)
                                    snprintf(why + strlen(why), sizeof why - strlen(why), " DC %.4f (peak %.3f);",
                                             (double)(r.sum / (int64_t)r.n) / 1048576.0, r.peak / 1048576.0);
                                if (note >= 48u && !c && (!e || md >= PM_MEMB) && r.tail * (md == PM_SYMP ? 5 : 10) > r.peak && r.tail > 64)
                                    snprintf(why + strlen(why), sizeof why - strlen(why), " not decayed (tail %.4f of %.4f);",
                                             r.tail / 1048576.0, r.peak / 1048576.0);
                                if (why[0]) {
                                    if (bad++ < 20u || getenv("ALLFAIL"))
                                        printf("phys_test: STABILITY FAIL %s note %u STRC %d BRIT %d DAMP %d POS %d BOW %d:%s\n",
                                               MN[md], note, q[0], q[1], q[2], q[3], q[5], why);
                                }
                            }
    printf("phys_test: stability: %u renders (%u models x notes 0..127 x STRC, BRIT, DAMP, POS, BOW corners), largest output",
           cases, (uint32_t)PM_COUNT);
    for (md = 0; md < PM_COUNT; md++)
        printf(" %s %.2f", MN[md], worst[md] / 1048576.0);
    printf(" (limit 16, strings 24, SYMP 32): %s\n", bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* a factory preset of each model (torture's starting sound) */
static uint32_t preset_of(uint32_t md)
{
    uint32_t i;
    for (i = 0; i < ENG_PHYS.npresets; i++)
        if ((uint32_t)ENG_PHYS.presets[i].e[0] == md)
            return i;
    return 0;
}

/* the whole engine (phys_note_on / phys_render through voice.c): every parameter and the pitch swept, a new
 * note every 0.1 s, the model switching under the notes, 8 notes asked */
static void torture(void)
{
    uint32_t md, f, bad = 0;
    int32_t peak_all = 0;
    for (md = 0; md < PM_COUNT; md++) {
        uint32_t frames = FS * 6u, nk = 0;
        int32_t peak = 0;
        host_tracks_init();
        host_preset(&trk[0], 9, preset_of(md));
        trk[0].p[P_SUS] = 127;
        for (f = 0; f < frames; f += CTL) {
            int32_t o[2 * CTL];
            uint32_t k, ph = f * 7u / FS;
            if (f % (FS / 10u) < CTL) {                     /* a new note every 0.1 s, the oldest off */
                uint32_t note = (f / (FS / 10u)) * 37u % 128u;
                if (nk >= 8u)
                    trk_note_off(&trk[0], (((f / (FS / 10u)) - 8u) * 37u) % 128u);
                trk_note_on(&trk[0], note, 1u + (note * 5u) % 127u);
                nk++;
            }
            for (k = 1; k < 8u; k++)                        /* STRC .. EXC: triangles of different rates */
                trk[0].p[P_E0 + k] = (int16_t)(((f * (k + 2u) / 2048u) % 254u) < 127u ? (f * (k + 2u) / 2048u) % 254u
                                                                                  : 253u - (f * (k + 2u) / 2048u) % 254u);
            trk[0].p[P_E0] = (int16_t)((md + (ph > 5u)) % PM_COUNT);   /* the model switches after 5 / 7 */
            trk[0].p[P_ED_PIT] = (int16_t)(((f / 64u) % 128u) - 64);  /* pitch swept by the envelope */
            trk[0].p[P_ED_FLT] = 63;
            mix_block(o, CTL);
            for (k = 0; k < 2u * CTL; k++)
                if (abs(o[k]) > peak)
                    peak = abs(o[k]);
        }
        if (peak > peak_all)
            peak_all = peak;
        if (peak > 32767 || !peak) {
            printf("phys_test: torture %s: output peak %d\n", MN[md], peak);
            bad++;
        }
    }
    printf("phys_test: torture (all parameters and the pitch swept, retriggers, model switches, %u x 6 s): "
           "output peak %d %s\n", (uint32_t)PM_COUNT, peak_all, bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* ----------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static const uint8_t CHORD8[8] = {48, 52, 55, 59, 60, 64, 67, 71};

/* instructions per sample of the mix with part 1 playing engine e (preset pi, the E values from pe if not 0),
 * 8 notes held (as regress.c job_cpu), 0 notes = idle */
static double cpu(uint32_t e, uint32_t pi, const int16_t *pe, uint32_t notes)
{
    uint32_t k, nb = FS / CTL, i;
    uint64_t i0 = 0;
    int32_t o[2 * CTL];
    pid_t pid;
    int fd[2];
    double r = 0;
    if (pipe(fd))
        return 0;
    pid = fork();                                       /* a fresh process: the state as at boot */
    if (!pid) {
        host_tracks_init();
        host_preset(&trk[0], e, pi);
        if (pe)
            for (i = 0; i < 8u; i++)
                trk[0].p[P_E0 + i] = pe[i];
        trk[0].p[P_VOICE] = V_POLY;
        trk[0].p[P_SUS] = 127;
        trk[0].p[P_AMODE] = 0;
        for (i = 0; i < notes; i++)
            trk_note_on(&trk[0], CHORD8[i], 100);
        for (k = 0; k < nb / 2u + nb; k++) {
            if (k == nb / 2u)
                i0 = instr_now();
            mix_block(o, CTL);
        }
        r = i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
        if (write(fd[1], &r, sizeof r) != sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &r, sizeof r) != sizeof r)
        r = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return r;
}

static void cost(void)
{
    static const struct { const char *name; uint8_t md; int16_t e[8]; } W[] = {
        /* MODEL STRC BRIT DAMP POS ACC BOW EXC */
        {"MODAL struck, 12 modes", PM_MODAL, {PM_MODAL, 40, 127, 127, 64, 127, 0, 127}},
        {"MODAL bowed, 12 modes ringing", PM_MODAL, {PM_MODAL, 40, 127, 127, 64, 127, 127, 127}},
        {"STRING bowed, dispersion", PM_STRING, {PM_STRING, 127, 127, 127, 64, 127, 127, 127}},
        {"STRING plucked, curved bridge", PM_STRING, {PM_STRING, 0, 127, 127, 64, 127, 0, 127}},
        {"MEMB struck, 10 modes, bend", PM_MEMB, {PM_MEMB, 64, 127, 127, 64, 127, 127, 127}},
        {"SYMP, 3 sympathetic strings", PM_SYMP, {PM_SYMP, 64, 127, 127, 127, 127, 0, 127}},
        {"SYMP, curved bridge", PM_SYMP, {PM_SYMP, 127, 127, 127, 127, 127, 127, 127}},
    };
    uint32_t i, ok = 1;
    double idle = cpu(0, 0, 0, 0), wire = cpu(2, 5, 0, 8) - idle, worst = 0, modal = 0;
    if (idle <= 0) {
        printf("phys_test: cost: no instruction counter on this host (proc_pid_rusage); not checked\n");
        return;
    }
    printf("phys_test: cost, instructions per sample over the idle mix (%.0f), 8 notes asked (PHYS plays 3):\n", idle);
    printf("phys_test:   %-34s %6.0f  (the limit)\n", "PHASE WIRE (heaviest preset)", wire);
    for (i = 0; i < sizeof W / sizeof W[0]; i++) {
        double c = cpu(9, 0, W[i].e, 8) - idle, lim = wire;
        if (!i)
            modal = c;
        if (W[i].md >= PM_MEMB)                         /* the new models: at most MODAL struck */
            lim = modal;
        if (c > worst)
            worst = c;
        printf("phys_test:   %-34s %6.0f  %4.0f %% of MODAL struck  %s\n", W[i].name, c, modal > 0 ? 100 * c / modal : 0,
               c <= lim * 1.02 ? "ok" : "OVER");
        ok &= c <= lim * 1.02;                           /* (the count varies ~1 % run to run) */
    }
    printf("phys_test: cost: worst PHYS %.0f vs PHASE WIRE %.0f; MEMB, SYMP <= MODAL struck: %s\n", worst,
           wire, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ---------------------------------------------------------------- demos --- */
static void demo_write(const char *dir, const char *name, uint32_t secs, void (*play)(uint32_t f, uint32_t arg),
                       uint32_t arg)
{
    char path[512];
    FILE *w;
    uint32_t f, frames = FS * secs;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    w = fopen(path, "wb");
    if (!w)
        return;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t k;
        play(f, arg);
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(w, o[2 * k], o[2 * k + 1]);
    }
    fclose(w);
}

/* a phrase per preset: (time in 1/8 s, note, length in 1/8 s, velocity); chords share a time */
#define NPHRASE 9
static const uint8_t PHRASE[NPHRASE][24][4] = {
    {{0, 84, 6, 110}, {6, 91, 6, 80}, {12, 96, 6, 100}, {20, 88, 8, 70}, {28, 79, 10, 110}, {29, 86, 10, 90},
     {30, 91, 10, 90}, {44, 84, 12, 120}},                                                  /* BELL TREE */
    {{0, 60, 2, 110}, {2, 64, 2, 90}, {4, 67, 2, 90}, {6, 72, 2, 110}, {8, 71, 2, 90}, {10, 67, 2, 80},
     {12, 64, 2, 100}, {14, 55, 4, 110}, {18, 48, 4, 120}, {18, 60, 4, 90}, {24, 76, 2, 90}, {26, 79, 2, 90},
     {28, 84, 8, 120}},                                                                     /* MARIMBA */
    {{0, 48, 2, 110}, {2, 55, 2, 90}, {4, 60, 2, 100}, {6, 64, 2, 90}, {8, 67, 4, 110}, {12, 64, 2, 80},
     {14, 60, 2, 90}, {16, 36, 6, 120}, {16, 43, 6, 100}, {24, 72, 2, 110}, {26, 76, 2, 90}, {28, 79, 8, 110}},   /* PLUCK */
    {{0, 48, 20, 90}, {0, 55, 20, 90}, {4, 64, 16, 80}, {8, 71, 12, 70}, {24, 50, 18, 100}, {24, 57, 18, 90},
     {28, 65, 14, 80}},                                                                     /* BOWED METAL */
    {{0, 72, 2, 110}, {2, 76, 2, 90}, {4, 79, 2, 100}, {6, 84, 4, 110}, {10, 79, 2, 80}, {12, 76, 2, 90},
     {14, 74, 4, 100}, {20, 67, 2, 110}, {22, 72, 2, 90}, {24, 76, 6, 120}},                 /* KALIMBA */
    {{0, 62, 1, 120}, {2, 62, 1, 80}, {3, 69, 1, 100}, {4, 50, 2, 120}, {6, 62, 1, 90}, {7, 69, 1, 100},
     {8, 62, 1, 120}, {10, 50, 1, 100}, {11, 62, 1, 80}, {12, 69, 2, 110}, {14, 50, 2, 120}, {16, 62, 1, 120},
     {18, 62, 1, 80}, {19, 69, 1, 100}, {20, 50, 2, 120}, {22, 74, 1, 90}, {23, 69, 1, 100}},   /* HAND DRUM */
    {{0, 50, 2, 120}, {2, 48, 2, 110}, {4, 45, 2, 110}, {6, 43, 2, 120}, {8, 40, 4, 120}, {16, 52, 1, 100},
     {17, 50, 1, 100}, {18, 48, 1, 110}, {19, 45, 1, 110}, {20, 43, 2, 120}, {22, 40, 6, 127}},   /* TOMS */
    {{0, 60, 8, 110}, {8, 62, 4, 90}, {12, 63, 4, 100}, {16, 67, 8, 110}, {24, 65, 4, 90}, {28, 63, 4, 100},
     {32, 62, 4, 90}, {36, 60, 12, 120}},                                                   /* DRONE STRING */
    {{0, 48, 6, 100}, {1, 55, 6, 90}, {2, 60, 6, 90}, {3, 64, 6, 100}, {4, 67, 6, 90}, {5, 72, 6, 100},
     {12, 53, 6, 100}, {13, 57, 6, 90}, {14, 60, 6, 90}, {15, 65, 6, 100}, {16, 69, 6, 90}, {17, 72, 8, 110},
     {24, 76, 8, 110}},                                                                     /* HARP */
};
static void play_preset(uint32_t f, uint32_t pi)
{
    uint32_t k;
    if (!f) {
        host_tracks_init();
        host_preset(&trk[0], 9, pi);
    }
    if (f % (FS / 8u) >= CTL)
        return;
    for (k = 0; k < 24u && PHRASE[pi][k][3]; k++) {
        uint32_t t = PHRASE[pi][k][0] * (FS / 8u);
        if (f / (FS / 8u) == PHRASE[pi][k][0] && f >= t)
            trk_note_on(&trk[0], PHRASE[pi][k][1], PHRASE[pi][k][3]);
        if (f / (FS / 8u) == (uint32_t)PHRASE[pi][k][0] + PHRASE[pi][k][2])
            trk_note_off(&trk[0], PHRASE[pi][k][1]);
    }
}
/* a model's defaults (edit[] def) over C1 .. C7: a note every 0.6 s, one octave apart */
static void play_model(uint32_t f, uint32_t md)
{
    uint32_t i, step = FS * 6u / 10u, n = f / step, cnt = 7u, base = 24u, iv = 12u;
    if (!f) {
        host_tracks_init();
        host_preset(&trk[0], 9, 0);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = ENG_PHYS.edit[i].def;
        trk[0].p[P_E0] = (int16_t)md;
        trk[0].p[P_REL] = 70;
    }
    if (f % step < CTL && n < cnt)
        trk_note_on(&trk[0], base + iv * n, 110);
    if (f % step < CTL && n >= 1u && n <= cnt)
        trk_note_off(&trk[0], base + iv * (n - 1u));
}

static void demos(const char *dir)
{
    static const char *const MD[PM_COUNT] = {"model_modal", "model_string", "model_memb", "model_symp"};
    uint32_t i;
    char name[64];
    for (i = 0; i < ENG_PHYS.npresets && i < NPHRASE; i++) {
        uint32_t k, j = 0;
        for (k = 0; ENG_PHYS.presets[i].name[k] && j + 1u < sizeof name; k++)
            name[j++] = ENG_PHYS.presets[i].name[k] == ' ' ? '_' : (char)(ENG_PHYS.presets[i].name[k] | 0x20);
        name[j] = 0;
        demo_write(dir, name, 8, play_preset, i);
    }
    for (i = 0; i < PM_COUNT; i++)
        demo_write(dir, MD[i], 5, play_model, i);
    printf("phys_test: demos in %s: %u presets, %u models\n", dir, ENG_PHYS.npresets, (uint32_t)PM_COUNT);
}

int main(int argc, char **argv)
{
    if (ENG_PHYS.npresets > NPHRASE) {
        printf("phys_test: %u factory presets, %u demo phrases: add one\n", ENG_PHYS.npresets, NPHRASE);
        fails++;
    }
    if (!getenv("NOCOST"))
        cost();                                         /* first: its children start from a clean state */
    stability();
    torture();
    if (argc > 1)
        demos(argv[1]);
    printf("phys_test: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
