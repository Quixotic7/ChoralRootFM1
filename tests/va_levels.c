/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The VA presets' levels against the master limiter (docs/INTEGRATION.md Defaults, docs/VA.md): a 6-note chord
 * held (D4 F#4 A4 B4 C#5 E5, as perf.sh (a)'s D6/9maj7) on each VA chord preset, part 0 at LEVEL 92, alone and with
 * the bank's SUB BASS (ANALOG, MONO, D2) on part 1 at 92; ChoralRoot's mix (fx_smooth, voice_fade_steal, MASTER as at
 * power-on: 2047); the share of 0.3..5.9 s under the limiter's gain (lim_g < 1), the post-master peak and RMS. The
 * basses: one note held, with TINE EP's chord. Not a pass / fail suite: run it after changing a preset's levels.
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/va_levels tests/va_levels.c -lm && build/host/va_levels
 * With "fm6": the bank's eight FM6 rows (cr_bank.c, by preset name; docs/FM6.md Levels; FM6_POLY 8 as choralroot.c;
 * PIANO, a preset of ChoralRoot's unit only (FELUCCA_SAMPLE 0), as TINE EP with PIANO's macros and sends):
 *   build/host/va_levels fm6
 * With "cz": the bank's six CZ-1 rows (docs/CZ1.md Levels; their trims as cr_bank.c sets them):
 *   build/host/va_levels cz */
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#ifndef FM6_POLY
#define FM6_POLY 8               /* as choralroot.c */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <libproc.h>
#include <sys/resource.h>
static const int8_t *ovr_e;                      /* "fm6": PIANO's macros and sends over TINE EP's (preset_t's) */
static const uint8_t *ovr_fx;
static int8_t ovr_trim;                          /* "cz": the bank row's trim (track_t.trim) */
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
static double cpu_ips;                           /* host instructions a sample over the hold (measure_in) */

static void measure_in(uint32_t chord_e, uint32_t chord_p, int with_bass, uint32_t bass_e, uint32_t bass_p,
                    double *lim, double *pk, double *rms)
{
    static const uint8_t CH[6] = {62, 66, 69, 71, 73, 76};
    int32_t out[2 * CTL];
    uint32_t i, n, nb = 6u * FS / CTL, b0 = (uint32_t)(0.3 * FS / CTL), b1 = (uint32_t)(5.9 * FS / CTL), under = 0, cnt = 0;
    double sq = 0, peak = 0;
    host_tracks_init();
    fx_smooth = 1;
    voice_fade_steal = 1;
    song.master_q12 = 2047;
    song.g[G_BPM] = 120;
    host_preset(&trk[0], chord_e, chord_p);
    if (ovr_e)
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = ovr_e[i];
    if (ovr_fx)
        for (i = 0; i < 4u; i++)
            trk[0].p[P_DIST + i] = (int16_t)(ovr_fx[i] - 1);
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_LEVEL] = 92;
    trk[0].trim = ovr_trim;
    for (i = 1; i < NPART; i++)
        trk[i].p[P_LEVEL] = 0;
    if (with_bass) {
        host_preset(&trk[1], bass_e, bass_p);
        trk[1].p[P_VOICE] = V_MONO;
        trk[1].p[P_LEVEL] = 92;
        for (i = P_DIST; i <= P_REV; i++)       /* (the bass part's sends: ChoralRoot's FX are part 0's) */
            trk[1].p[i] = trk[0].p[i];
    }
    for (i = 0; i < 6u; i++)
        trk_note_on(&trk[0], CH[i], 100);
    if (with_bass)
        trk_note_on(&trk[1], 38, 100);
    uint64_t i0 = 0;
    for (n = 0; n < nb; n++) {
        if (n == b0)
            i0 = instr_now();
        if (n == b1)
            cpu_ips = (double)(instr_now() - i0) / ((b1 - b0) * CTL);
        mix_block(out, CTL);
        if (n < b0 || n >= b1)
            continue;
        for (i = 0; i < CTL; i++) {
            double l = out[2 * i] / 32768.0, r = out[2 * i + 1] / 32768.0;
            sq += (l * l + r * r) / 2;
            peak = fabs(l) > peak ? fabs(l) : peak;
            peak = fabs(r) > peak ? fabs(r) : peak;
            cnt++;
        }
        under += lim_g < 32768;                  /* (per block: the gain at its end) */
    }
    *lim = 100.0 * under / (b1 - b0);
    *pk = 20 * log10(peak + 1e-9);
    *rms = 10 * log10(sq / cnt + 1e-12);
}

/* each measurement in a fork()ed child: every state as at boot (voices, FX buffers, the limiter) */
static void measure(uint32_t chord_e, uint32_t chord_p, int with_bass, uint32_t bass_e, uint32_t bass_p,
                    double *lim, double *pk, double *rms)
{
    double r[4] = {0, 0, 0, 0};
    int fd[2];
    fflush(stdout);
    if (pipe(fd))
        exit(2);
    if (!fork()) {
        close(fd[0]);
        measure_in(chord_e, chord_p, with_bass, bass_e, bass_p, &r[0], &r[1], &r[2]);
        r[3] = cpu_ips;
        if (write(fd[1], r, sizeof r) != sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r)
        exit(2);
    close(fd[0]);
    wait(0);
    *lim = r[0];
    *pk = r[1];
    *rms = r[2];
    cpu_ips = r[3];
}

static uint32_t preset_named(uint32_t eng, const char *name)
{
    const engine_t *e = ENGINES[eng];
    uint32_t k;
    for (k = 0; k < e->npresets; k++)
        if (!strcmp(e->presets[k].name, name))
            return k;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t k;
    const engine_t *e = ENGINES[ENGI_VA];
    if (argc > 1 && !strcmp(argv[1], "cz")) {    /* the bank's CZ-1 rows: display name, preset, trim (cr_bank.c) */
        static const struct { const char *row, *pr; int8_t trim; } ROW[] = {
            {"CZ BRASS 1", "BRASS 1", -10}, {"CZ PIANO", "PIANO 1", 0}, {"CZ STRINGS 2", "STRINGS 2", -6},
            {"CZ PIPE", "PIPE ORGAN 1", -12}, {"CZ VIBES", "VIBRAPHONE", 0}, {"CZ BASS", "SYNTH.BASS", 0}};
        printf("%-13s %26s | %26s | %s\n", "CZ-1 bank row", "chord alone: lim  pk  rms", "with SUB BASS: lim  pk  rms",
               "CPU with the bass: host instr / sample, device estimate (1.7 % per 100)");
        for (k = 0; k < NELEM(ROW); k++) {
            double l0, p0, r0, l1, p1, r1;
            uint32_t pr = preset_named(ENGI_CZ, ROW[k].pr);
            ovr_trim = ROW[k].trim;
            if (k == NELEM(ROW) - 1u) {          /* the bass: TINE EP's chord with it */
                ovr_trim = 0;
                measure(ENGI_FM6, 0, 1, ENGI_CZ, pr, &l1, &p1, &r1);
                printf("%-13s %26s | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%  (with TINE EP's chord)\n", ROW[k].row, "",
                       l1, p1, r1, cpu_ips, cpu_ips * 0.017);
                continue;
            }
            measure(ENGI_CZ, pr, 0, 0, 0, &l0, &p0, &r0);
            measure(ENGI_CZ, pr, 1, 0, 7, &l1, &p1, &r1);   /* ANALOG SUB BASS */
            printf("%-13s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n", ROW[k].row, l0, p0, r0,
                   l1, p1, r1, cpu_ips, cpu_ips * 0.017);
        }
        ovr_trim = 0;
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "fm6")) {   /* the bank's FM6 rows: name (display), preset */
        static const char *const ROW[8][2] = {{"TINE EP", "TINE EP"}, {"FM BELL", "BELL"}, {"PIANO", "PIANO"},
                                              {"FM PAD", "PAD"}, {"FM ORGAN", "ORGAN"}, {"MARIMBA", "MARIMBA"},
                                              {"FM PLUCK", "PLUCK"}, {"FM BASS", "FM BASS"}};
        printf("%-13s %26s | %26s | %s\n", "FM6 bank row", "chord alone: lim  pk  rms", "with SUB BASS: lim  pk  rms",
               "CPU with the bass: host instr / sample, device estimate (1.7 % per 100)");
        for (k = 0; k < 8u; k++) {
            double l0, p0, r0, l1, p1, r1;
            static const int8_t PIANO_E[8] = {0, 1, 10, 1, -16, 2, 12, 0};   /* eng_fm6.c's PIANO */
            static const uint8_t PIANO_FX[4] = {1, 36, 16, 46};
            uint32_t pr = preset_named(ENGI_FM6, ROW[k][1]);
            ovr_e = 0;
            ovr_fx = 0;
            if (strcmp(ENGINES[ENGI_FM6]->presets[pr].name, ROW[k][1])) {   /* (PIANO: not in this unit) */
                ovr_e = PIANO_E;
                ovr_fx = PIANO_FX;
                pr = 0;
            }
            if (k == 7u) {                       /* the bass: TINE EP's chord with it */
                measure(ENGI_FM6, 0, 1, ENGI_FM6, pr, &l1, &p1, &r1);
                printf("%-13s %26s | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%  (with TINE EP's chord)\n", ROW[k][0], "",
                       l1, p1, r1, cpu_ips, cpu_ips * 0.017);
                continue;
            }
            measure(ENGI_FM6, pr, 0, 0, 0, &l0, &p0, &r0);
            measure(ENGI_FM6, pr, 1, 0, 7, &l1, &p1, &r1);   /* ANALOG SUB BASS */
            printf("%-13s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n", ROW[k][0], l0, p0, r0,
                   l1, p1, r1, cpu_ips, cpu_ips * 0.017);
        }
        return 0;
    }
    printf("%-13s %26s | %26s | %s\n", "VA preset", "chord alone: lim  pk  rms", "with SUB BASS: lim  pk  rms",
           "CPU with the bass: host instr / sample, device estimate (1.7 % per 100)");
    for (k = 0; k < e->npresets; k++) {
        double l0, p0, r0, l1, p1, r1;
        if (e->presets[k].mono) {               /* a bass: TINE EP's chord (FM6 preset 0) with it */
            measure(ENGI_FM6, 0, 1, ENGI_VA, k, &l1, &p1, &r1);
            printf("%-13s %26s | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%  (with TINE EP's chord)\n", e->presets[k].name, "",
                   l1, p1, r1, cpu_ips, cpu_ips * 0.017);
            continue;
        }
        measure(ENGI_VA, k, 0, 0, 0, &l0, &p0, &r0);
        measure(ENGI_VA, k, 1, 0, 7, &l1, &p1, &r1);   /* ANALOG SUB BASS */
        printf("%-13s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n", e->presets[k].name, l0, p0, r0,
               l1, p1, r1, cpu_ips, cpu_ips * 0.017);
    }
    {   /* references: bank sounds */
        static const uint8_t REF[][2] = {{0, 1}, {0, 11}, {12, 0}, {12, 4}, {12, 8}};   /* .. FM6 PIANO (PRESETS 03) */
        for (k = 0; k < NELEM(REF); k++) {
            double l0, p0, r0, l1, p1, r1;
            measure(REF[k][0], REF[k][1], 0, 0, 0, &l0, &p0, &r0);
            measure(REF[k][0], REF[k][1], 1, 0, 7, &l1, &p1, &r1);
            printf("ref %-9s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n",
                   ENGINES[REF[k][0]]->presets[REF[k][1]].name, l0, p0, r0, l1, p1, r1, cpu_ips, cpu_ips * 0.017);
        }
    }
    return 0;
}
