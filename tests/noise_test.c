/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* NOISE engine test (src/eng_noise.c) on the Mac, through hostsim.c as regress.c.
 *   build/host/noise_test [DEMODIR]          (run_tests.sh: build/noise_demo)
 * 1. colour: the slope of the spectrum (power per Hz, 150 Hz .. 6 kHz, the filter open) at COLR 0 / 64 / 127:
 *    white 0 +-1 dB/oct, pink -3 +-1, brown -6 +-1.5.
 * 2. the filter follows the key: ANLG at RES 127 (band-pass), TRK 127: the spectral peak an octave apart for
 *    notes an octave apart (+-6 %), and at FREQ's frequency at C4 (+-1 semitone).
 * 3. the register clock follows the key: LFSR (L7, its period audible) counts twice the transitions an octave up;
 *    META is periodic at the key's pitch (autocorrelation at one period >= 0.95) and LFSR L23 is not (< 0.3).
 * 4. no DC: every preset, 16 notes held for 1 s each (|mean| < 1 % of a voice's full scale); no clipping: every mode at the corners of
 *    COLR / FREQ / RES / DENS / CRSH, 8 notes at velocity 127, LEVEL 127: no voice sum beyond 2 x VOICE_FS a
 *    voice and no mix sample at full scale.
 * 5. determinism: a note from silence renders the same samples again.
 * 6. cost: host instructions per sample per voice (8 notes held, the idle mix taken off) for every preset and
 *    the heaviest corner, the device estimate (1.7 % per 100, drum_test's ratio); fails above NOISE_COST_MAX.
 * 7. demos into DEMODIR: every preset playing its suggested pattern, and a sweep of the modes. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define NOISE_COST_MAX 235.0      /* host instructions per sample and voice (~4 % on the device) */
#define NFFT 8192

static int bad;
static void check(const char *what, int ok)
{
    printf("noise: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t eng_noise(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] == &ENG_NOISE)
            return e;
    return 0;
}

/* preset pi's E values (int8_t in preset_t) */
static const int16_t *pe(uint32_t pi)
{
    static int16_t e[8];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        e[i] = ENG_NOISE.presets[pi].e[i];
    return e;
}

/* track 0 the NOISE engine with e[0..7], a flat envelope (ATK 0, SUS full), no sends, POLY */
static void setup(const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_noise(), 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}

/* the voices' sum (track_render, before the mix) of note for n samples, after 0.1 s */
static void voice_render(const int16_t *e, uint32_t note, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k, skip = FS / 10u / CTL;
    setup(e);
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < skip; i++)
        track_render(&trk[0], b, CTL);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k] / 32768.0;
    }
}

/* ------------------------------------------------------------ spectra --- */
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (i = 0; i < n; i += len)
            for (j = 0; j < len / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j + len / 2] = re[i + j] - xr, im[i + j + len / 2] = im[i + j] - xi;
                re[i + j] += xr, im[i + j] += xi;
            }
    }
}

/* the averaged power spectrum (Hann) of y (nf frames of NFFT) into ps[NFFT / 2] */
static void spectrum(const double *y, uint32_t nf, double *ps)
{
    static double re[NFFT], im[NFFT];
    uint32_t f, i;
    memset(ps, 0, sizeof(double) * NFFT / 2);
    for (f = 0; f < nf; f++) {
        for (i = 0; i < NFFT; i++) {
            re[i] = y[f * NFFT + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT));
            im[i] = 0;
        }
        fft(re, im, NFFT);
        for (i = 0; i < NFFT / 2; i++)
            ps[i] += re[i] * re[i] + im[i] * im[i];
    }
}

/* least-squares slope, dB per octave, of the power per Hz over lo..hi Hz (one point per third of an octave) */
static double slope(const double *ps, double lo, double hi)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0, f;
    uint32_t n = 0;
    for (f = lo; f < hi; f *= 1.2599) {
        uint32_t a = (uint32_t)(f / FS * NFFT), b = (uint32_t)(f * 1.2599 / FS * NFFT), i;
        double p = 0, x, yv;
        for (i = a; i < b; i++)
            p += ps[i];
        p /= (b - a);
        x = log2(f * 1.12);
        yv = 10 * log10(p + 1e-30);
        sx += x, sy += yv, sxx += x * x, sxy += x * yv, n++;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

static double peak_hz(const double *ps, double lo, double hi)
{
    uint32_t i, a = (uint32_t)(lo / FS * NFFT), b = (uint32_t)(hi / FS * NFFT), best = a;
    double sm[NFFT / 2];
    for (i = a; i < b; i++) {                         /* a little smoothing: noise spectra are ragged */
        int32_t k;
        sm[i] = 0;
        for (k = -3; k <= 3; k++)
            sm[i] += ps[i + k];
        if (sm[i] > sm[best])
            best = i;
    }
    return (double)best * FS / NFFT;
}

static double rms(const double *y, uint32_t n, double *mean)
{
    double s = 0, m = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m += y[i];
    m /= n;
    for (i = 0; i < n; i++)
        s += (y[i] - m) * (y[i] - m);
    if (mean)
        *mean = m;
    return sqrt(s / n);
}

static double autocorr(const double *y, uint32_t n, double lag)
{
    double a = 0, b = 0, c = 0;
    uint32_t i, l = (uint32_t)(lag + 0.5);
    for (i = 0; i + l < n; i++) {
        a += y[i] * y[i + l];
        b += y[i] * y[i];
        c += y[i + l] * y[i + l];
    }
    return a / sqrt(b * c + 1e-30);
}

/* -------------------------------------------------------------- tests --- */
#define NS (NFFT * 12)
static double Y[NS], PS[NFFT / 2];

static void colour(void)
{
    static const struct { int16_t c; double want, tol; const char *name; } C[] = {
        {0, 0, 1, "white"}, {64, -3, 1, "pink"}, {127, -6, 1.5, "brown"}};
    uint32_t i, m;
    char what[128];
    for (m = 0; m < 2u; m++)                          /* ANLG, and the LFSR clocked at the sample rate */
        for (i = 0; i < 3u; i++) {
            int16_t e[8] = {m ? 2 : 0, C[i].c, 127, 0, 0, 0, 0, m ? 127 : 0};
            double s;
            voice_render(e, 60, Y, NS);
            spectrum(Y, NS / NFFT, PS);
            s = slope(PS, 150, 6000);
            snprintf(what, sizeof what, "%s COLR %3d (%s): %+.2f dB/oct (want %+.0f +-%.1f)", m ? "LFSR" : "ANLG",
                     C[i].c, C[i].name, s, C[i].want, C[i].tol);
            check(what, fabs(s - C[i].want) <= C[i].tol);
        }
}

static void tracking(void)
{
    int16_t e[8] = {0, 0, 64, 127, 127, 0, 0, 0};
    double f[3], want = CUTOFF_HZ[64];
    uint32_t k;
    char what[160];
    for (k = 0; k < 3u; k++) {
        voice_render(e, 48 + 12 * k, Y, NS);
        spectrum(Y, NS / NFFT, PS);
        f[k] = peak_hz(PS, 40, 12000);
    }
    snprintf(what, sizeof what, "filter tracks the key: peaks %.0f / %.0f / %.0f Hz at C3 C4 C5 (FREQ %.0f Hz at C4)",
             f[0], f[1], f[2], want);
    check(what, fabs(f[1] / f[0] - 2) < 0.12 && fabs(f[2] / f[1] - 2) < 0.12 && fabs(log2(f[1] / want) * 12) < 1);
}

static uint32_t transitions(const double *y, uint32_t n)
{
    uint32_t i, c = 0;
    for (i = 1; i < n; i++)
        c += (y[i] > 0) != (y[i - 1] > 0);
    return c;
}

static void clock_test(void)
{
    int16_t lf[8] = {2, 0, 127, 0, 0, 127, 0, 24}, lg[8] = {2, 0, 127, 0, 0, 0, 0, 24};
    int16_t me[8] = {3, 0, 127, 0, 0, 32, 0, 0};
    uint32_t t1, t2, n = FS;
    double a, b, per = FS / (440.0 * pow(2, (60 - 69) / 12.0));
    char what[160];
    voice_render(lf, 48, Y, n);
    t1 = transitions(Y, n);
    voice_render(lf, 60, Y, n);
    t2 = transitions(Y, n);
    snprintf(what, sizeof what, "LFSR clock follows the key: %u transitions at C3, %u at C4 (x%.2f, want 2)", t1, t2,
             (double)t2 / t1);
    check(what, fabs((double)t2 / t1 - 2) < 0.15);
    voice_render(me, 60, Y, n);
    a = autocorr(Y, n, per * 9);                     /* 9 periods: 1517.05 samples, close to whole */
    voice_render(lg, 60, Y, n);
    b = autocorr(Y, n, per * 9);
    snprintf(what, sizeof what, "META (93) periodic at the key: r = %.3f at 9 periods of C4; LFSR L23 r = %.3f", a, b);
    check(what, a >= 0.95 && fabs(b) < 0.3);
}

static void dc_and_clip(void)
{
    static int32_t o[2 * CTL];
    uint32_t pi, i, k, m, worst_p = 0;
    double worst = 0;
    int32_t vmax = 0, omax = 0;
    char what[160];
    for (pi = 0; pi < ENG_NOISE.npresets; pi++) {
        double mean = 0, r = 0, m1;
        for (k = 0; k < 16u; k++) {                   /* 16 notes (16 seeds): brown noise's own slow wander averages out */
            voice_render(pe(pi), 48 + k, Y, FS);
            r += rms(Y, FS, &m1) / 16;
            mean += m1 / 16;
        }
        if (getenv("VERBOSE"))
            printf("noise:   %-10s RMS %6.1f dB  mean %+.5f\n", ENG_NOISE.presets[pi].name, 20 * log10(r + 1e-12), mean);
        if (fabs(mean) * 32768.0 / VOICE_FS > worst)
            worst = fabs(mean) * 32768.0 / VOICE_FS, worst_p = pi;
    }
    snprintf(what, sizeof what, "no DC: worst |mean| %.2f %% of a voice's full scale (%s)", worst * 100,
             ENG_NOISE.presets[worst_p].name);
    check(what, worst < 0.01);
    for (m = 0; m < 4u; m++)
        for (k = 0; k < 64u; k++) {                   /* the corners of COLR FREQ RES DENS CRSH TRK */
            int16_t e[8] = {(int16_t)m, k & 1 ? 127 : 0, k & 2 ? 127 : 0, k & 4 ? 127 : 0, k & 32 ? 127 : 0,
                            k & 8 ? 127 : 0, 0, k & 16 ? 127 : 0};
            static int32_t b[CTL];
            setup(e);
            trk[0].p[P_LEVEL] = 127;
            for (i = 0; i < 8u; i++)
                trk_note_on(&trk[0], 24 + 12 * i, 127);
            for (i = 0; i < FS / CTL; i++) {
                uint32_t j;
                track_render(&trk[0], b, CTL);
                for (j = 0; j < CTL; j++)
                    vmax = abs(b[j]) > vmax ? abs(b[j]) : vmax;
            }
            setup(e);
            trk[0].p[P_LEVEL] = 127;
            for (i = 0; i < 8u; i++)
                trk_note_on(&trk[0], 24 + 12 * i, 127);
            for (i = 0; i < FS / CTL; i++) {
                mix_block(o, CTL);
                for (uint32_t j = 0; j < 2u * CTL; j++)
                    omax = abs(o[j]) > omax ? abs(o[j]) : omax;
            }
        }
    snprintf(what, sizeof what, "no clipping at the corners: voice sum peak %d (8 voices, limit %d), mix peak %d", vmax,
             8 * 2 * VOICE_FS, omax);
    check(what, vmax < 8 * 2 * VOICE_FS && omax < 32767);
}

static void determinism(void)
{
    static int32_t a[FS / 2], b[CTL];
    uint32_t pi, i, k, same = 1;
    for (pi = 0; pi < ENG_NOISE.npresets; pi++) {
        const int16_t *e = pe(pi);
        setup(e);
        trk_note_on(&trk[0], 62, 100);
        for (i = 0; i < FS / 2 / CTL; i++) {
            track_render(&trk[0], b, CTL);
            for (k = 0; k < CTL; k++)
                a[i * CTL + k] = b[k];
        }
        trk_note_off(&trk[0], 62);
        for (i = 0; i < 4 * FS / CTL; i++)            /* released, silent */
            track_render(&trk[0], b, CTL);
        trk_note_on(&trk[0], 62, 100);
        for (i = 0; i < FS / 2 / CTL; i++) {
            track_render(&trk[0], b, CTL);
            for (k = 0; k < CTL; k++)
                same &= a[i * CTL + k] == b[k];
        }
    }
    check("a note from silence renders the same samples again (every preset)", same);
}

/* ---------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static double mix_cost(const int16_t *e, uint32_t notes)
{
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS * 2u / CTL;
    uint64_t i0;
    setup(e);
    for (i = 0; i < notes; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (i = 0; i < nb; i++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (nb * CTL);
}

static void cost(void)
{
    static const int16_t HEAVY[8] = {0, 127, 64, 127, 127, 127, 127, 127};   /* ANLG: dust, brown, drift, crush */
    double idle, c, worst = 0;
    uint32_t pi;
    char what[160];
    if (!instr_now()) {
        printf("noise: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    idle = mix_cost(HEAVY, 0);
    printf("noise: cost, host instructions per sample and voice (8 held; device estimate 1.7 %% per 100):\n");
    for (pi = 0; pi <= ENG_NOISE.npresets; pi++) {
        const int16_t *e = pi < ENG_NOISE.npresets ? pe(pi) : HEAVY;
        c = (mix_cost(e, 8) - idle) / 8;
        printf("noise:   %-10s %5.0f  ~%.2f %%\n", pi < ENG_NOISE.npresets ? ENG_NOISE.presets[pi].name : "(corner)", c,
               c * 0.017);
        worst = c > worst ? c : worst;
    }
    snprintf(what, sizeof what, "cost: at most %.0f instructions a sample and voice (~%.1f %% on the device; limit %.0f)",
             worst, worst * 0.017, NOISE_COST_MAX);
    check(what, worst <= NOISE_COST_MAX);
}

/* --------------------------------------------------------------- demos --- */
static void demo(const char *dir, uint32_t pi)
{
    static int32_t o[2 * CTL];
    const preset_t *pr = &ENG_NOISE.presets[pi];
    uint32_t pat = pr->pat ? pr->pat - 1u : 4u, step = FS / 8u, s, i, held = 0, frames = 0;
    char path[512], nm[32];
    FILE *w;
    for (i = 0; pr->name[i] && i < 31u; i++)
        nm[i] = pr->name[i] == ' ' ? '_' : pr->name[i];
    nm[i] = 0;
    snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, pi, nm);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, 0);
    host_tracks_init();
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_noise(), pi);
    for (s = 0; s < 48u; s++) {                       /* three times through the pattern's 16 steps, 120 BPM 1/16 */
        uint32_t k = s % 16u, note = PATTERNS[pat].note[k], fl = PATTERNS[pat].flags[k];
        if (!(fl & 4u) && held) {
            trk_note_off(&trk[0], held);
            held = 0;
        }
        if (note && !(fl & 4u)) {
            trk_note_on(&trk[0], note, fl & 1u ? 120 : 90);
            held = note;
        }
        for (i = 0; i < step; i += CTL) {
            uint32_t j;
            mix_block(o, CTL);
            for (j = 0; j < CTL; j++)
                wav_put(w, o[2 * j], o[2 * j + 1]);
            frames += CTL;
        }
    }
    if (held)
        trk_note_off(&trk[0], held);
    for (i = 0; i < FS * 2u; i += CTL) {
        uint32_t j;
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
        frames += CTL;
    }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
}

static void demos(const char *dir)
{
    uint32_t pi;
    for (pi = 0; pi < ENG_NOISE.npresets; pi++)
        demo(dir, pi);
    printf("noise: demos in %s: the %u presets, each playing its suggested pattern\n", dir, (uint32_t)ENG_NOISE.npresets);
}

int main(int argc, char **argv)
{
    colour();
    tracking();
    clock_test();
    dc_and_clip();
    determinism();
    if (!getenv("NOCOST"))
        cost();
    if (argc > 1)
        demos(argv[1]);
    printf("noise_test: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
