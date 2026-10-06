/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Drum voice test (src/drum_voice.c, and the DRUM engine around it: src/eng_drum.c) on the Mac, through
 * hostsim.c as regress.c.
 *   build/host/drum_test [DEMODIR]          (run_tests.sh: build/drum_demo)
 * 1. targets: every voice (the 12 types: the 8 lanes and their variants) at its designed parameters, one hit:
 *    pitch (the dominant frequency of 20..300 ms, the kicks' of 150..400 ms: the end of the sweep) within
 *    +-10 cents of the design, t-30 (10 ms RMS frames, from the loudest to the first 30 dB below) within
 *    +-15 % + 10 ms, the spectral centroid of the first 100 ms within +-20 %, the RMS level of 0..0.2 s
 *    within +-1.5 dB of the targets in TGT below (Felucca's own numbers, set by ear and measured).
 * 2. controls: TUNE up -> pitch up, DECAY up -> t-30 up, TONE up -> centroid up (snare, clap, hats, cymbal),
 *    each extra control changes the sound, the kick's DRIVE adds harmonics.
 * 3. safety: every type at the corners of DECAY / TONE / extra / TUNE with LEVEL 127 and full accent: no sample
 *    at full scale (the Q15 output never reaches +-32767), DC under 1 % of the peak, the voice stops (live 0)
 *    within 2 x its t-30 + 0.1 s (at the designed settings); a retrigger renders the same samples again.
 * 4. the hat choke: a closed hat 100 ms into an open one brings the open one below -60 dB of its level within
 *    10 ms; the DRUM engine does it between its lanes too.
 * 5. the kick on a small speaker: energy above 150 Hz at least -12 dB of the whole (PUNCH and ROUND at the
 *    defaults), and the fundamental intact (pitch above).
 * 6. the key map of the DRUM engine: key 7 (the first C) = KICK, one lane per key, KIT's and KICK's variants.
 * 7. the kit through the engine: the 8 lanes struck at once all sound together (8 voices), a lane struck again
 *    by another note cuts its last hit (one voice), a released key does not end a hit and the voice frees
 *    itself when the drum has rung out, VOICE MONO still plays the kit.
 * 8. cost: host instructions per sample per voice while it sounds (retriggered), the metal source on its own,
 *    and a device estimate (1.7 % per 100 instructions per sample, the PHYS measurements' ratio).
 * 9. demos into DEMODIR: every voice and variant (designed, then DECAY / TONE / extra swept), the kit through
 *    the DRUM engine (the 8 keys), a beat. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int fails;
static const char *const TN[DVT_COUNT] = {"KICK PUNCH", "KICK ROUND", "SNARE", "CLAP", "HAT CL", "HAT OP",
                                          "TOM", "CONGA", "RIM", "CLAVE", "BELL", "CYM"};
#define NMAX (FS * 12)

/* ------------------------------------------------------------- render --- */
typedef struct {
    float y[NMAX];
    uint32_t n, over, knee, stop;               /* samples; at full scale; in the soft knee; where live went 0 */
    int32_t peak;
} buf_t;

/* one hit of p (the voice's output, Q15 -> +-1), n samples; at: a second trigger (0: none) */
static void render(const dv_param_t *p, uint32_t n, uint32_t at, buf_t *b)
{
    static dv_coef_t c;
    static dv_voice_t v;
    static dv_metal_t mb;
    uint32_t i, k;
    int32_t y[CTL], m[CTL];
    dv_setup(&c, p);
    dv_init(&v, c.type);
    memset(&mb, 0, sizeof mb);
    dv_metal_tune(&mb, &c);
    dv_trigger(&v);
    b->n = n, b->over = 0, b->knee = 0, b->peak = 0, b->stop = 0;
    for (i = 0; i < n; i += CTL) {
        if (at && i == at) {
            memset(&mb, 0, sizeof mb);
            dv_metal_tune(&mb, &c);
            dv_trigger(&v);
        }
        dv_metal_run(&mb, m, CTL);
        dv_run(&c, &v, m, y, CTL);
        for (k = 0; k < CTL && i + k < n; k++) {
            int32_t s = y[k], a = s < 0 ? -s : s;
            if (a >= 32767)
                b->over++;
            if (a > 16384)
                b->knee++;
            if (a > b->peak)
                b->peak = a;
            b->y[i + k] = s / 32768.0f;
        }
        if (!v.live && !b->stop && !v.trig)
            b->stop = i + CTL;
    }
}

/* ----------------------------------------------------------- measures --- */
static double rms_db(const buf_t *b, uint32_t a, uint32_t e)
{
    double s = 0;
    uint32_t i;
    if (e > b->n)
        e = b->n;
    for (i = a; i < e; i++)
        s += (double)b->y[i] * b->y[i];
    return 10 * log10(s / (e - a) + 1e-20);
}

/* t-30: from the loudest 10 ms frame to the first frame 30 dB below it (s from the hit) */
static double t30(const buf_t *b)
{
    uint32_t f, nf = b->n / 441u, best = 0;
    double mx = -999, l;
    for (f = 0; f < nf; f++)
        if ((l = rms_db(b, f * 441u, f * 441u + 441u)) > mx)
            mx = l, best = f;
    for (f = best; f < nf; f++)
        if (rms_db(b, f * 441u, f * 441u + 441u) < mx - 30)
            return f * 0.01;
    return nf * 0.01;
}

/* |DTFT|^2 of y[a..e) (Hann) at f Hz */
static double dtft(const buf_t *b, uint32_t a, uint32_t e, double f)
{
    double re = 0, im = 0, w = 2 * M_PI * f / FS;
    uint32_t i, n = e - a;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2 * M_PI * i / n), x = b->y[a + i] * h;
        re += x * cos(w * i);
        im -= x * sin(w * i);
    }
    return re * re + im * im;
}

/* the dominant frequency of y[a..e): a coarse scan (1/8 semitone from 30 Hz to 12 kHz), then golden section */
static double pitch(const buf_t *b, uint32_t a, uint32_t e, double lo, double hi)
{
    double f, best = lo, bm = -1, x0, x1, gr = 0.6180339887;
    for (f = lo; f < hi; f *= 1.00724) {
        double m = dtft(b, a, e, f);
        if (m > bm)
            bm = m, best = f;
    }
    x0 = best / 1.00724, x1 = best * 1.00724;
    while (x1 - x0 > best * 1e-5) {
        double c = x1 - gr * (x1 - x0), d = x0 + gr * (x1 - x0);
        if (dtft(b, a, e, c) > dtft(b, a, e, d))
            x1 = d;
        else
            x0 = c;
    }
    return (x0 + x1) / 2;
}

/* radix-2 FFT, in place (n a power of 2) */
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j = 0, len, k;
    for (i = 1; i < n; i++) {
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
            for (k = 0; k < len / 2u; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr, im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr, im[i + k + len / 2] = ui - vi;
            }
    }
}

/* the power spectrum of y[a..a + n) (zero beyond the render), n a power of 2: centroid (magnitude-weighted)
 * and the share of the power above fc (dB) */
static void spectrum(const buf_t *b, uint32_t a, uint32_t n, double fc, double *centroid, double *above)
{
    static double re[1 << 17], im[1 << 17];
    uint32_t i;
    double sm = 0, sf = 0, p = 0, pa = 0;
    for (i = 0; i < n; i++)
        re[i] = a + i < b->n ? b->y[a + i] : 0, im[i] = 0;
    fft(re, im, n);
    for (i = 1; i < n / 2u; i++) {
        double f = (double)i * FS / n, q = re[i] * re[i] + im[i] * im[i], m = sqrt(q);
        sm += m, sf += m * f, p += q;
        if (f >= fc)
            pa += q;
    }
    *centroid = sf / (sm + 1e-20);
    *above = 10 * log10(pa / (p + 1e-30) + 1e-30);
}

/* the centroid of the first 100 ms between lo and hi Hz (the clap's band) */
static double band_centroid(const buf_t *b, double lo, double hi)
{
    static double re[4096], im[4096];
    uint32_t i;
    double sm = 0, sf = 0;
    for (i = 0; i < 4096u; i++)
        re[i] = b->y[i], im[i] = 0;
    fft(re, im, 4096);
    for (i = 1; i < 2048u; i++) {
        double f = (double)i * FS / 4096, m = sqrt(re[i] * re[i] + im[i] * im[i]);
        if (f >= lo && f <= hi)
            sm += m, sf += m * f;
    }
    return sf / (sm + 1e-20);
}

static double centroid100(const buf_t *b)
{
    double c, x;
    spectrum(b, 0, 4096, 0, &c, &x);                    /* (93 ms) */
    return c;
}

static double dc_share(const buf_t *b)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < b->n; i++)
        s += b->y[i];
    return fabs(s / b->n) / (b->peak / 32768.0 + 1e-9);
}


/* ----------------------------------------------------------- targets --- */
static double hz_of(int32_t p16) { return 440.0 * pow(2.0, (p16 / 16.0 - 69) / 12); }

/* Felucca's targets at the designed parameters (LEVEL 100, no accent): t-30 (s), centroid of the first
 * 100 ms (Hz), RMS level of 0..0.2 s (dBFS) */
static const struct {
    double t30, centroid, level;
} TGT[DVT_COUNT] = {
    {0.21, 760, -14.5},                         /* KICK PUNCH: hold 12 ms + 3.45 tau (50 ms), the drive's sustain */
    {0.36, 1520, -12.5},                        /* KICK ROUND: tau 90 ms, strong 3rd harmonic */
    {0.18, 6050, -18.2},                        /* SNARE */
    {0.20, 7180, -24.2},                        /* CLAP: teeth, then the tail */
    {0.06, 10700, -27.4},                       /* HAT CL: tau 16 ms, the sizzle on top */
    {0.34, 9530, -23.8},                        /* HAT OP: 3.45 tau (98 ms) */
    {0.67, 1360, -13.6},                        /* TOM: 3.45 tau (190 ms) */
    {0.37, 2160, -13.5},                        /* CONGA: 3.45 tau (95 ms) */
    {0.02, 2550, -24.3},                        /* RIM */
    {0.03, 5800, -20.8},                        /* CLAVE */
    {0.35, 1990, -20.0},                        /* BELL */
    {2.28, 6050, -23.5},                        /* CYM: the strike, then the ring (tau 0.95 s) */
};

static uint32_t len_of(uint32_t t) { return t == DVT_CYM ? NMAX : FS * 2u; }
static int pitched(uint32_t t) { return t != DVT_CLAP && t != DVT_HATC && t != DVT_HATO && t != DVT_CYM; }
/* the designed frequency (BELL: its upper square, 1.48 f, the band-pass's partial) */
static double design_hz(uint32_t t, int32_t tune)
{
    int32_t p16 = DV_DEF[t].p16 + tune;
    p16 = p16 < DV_DEF[t].lo ? DV_DEF[t].lo : p16 > DV_DEF[t].hi ? DV_DEF[t].hi : p16;
    return hz_of(p16 + (t == DVT_BELL ? 109 : 0));
}
/* the dominant frequency where it is the voice's pitch: the kicks 150..400 ms (the sweep is over), RIM /
 * CLAVE their first 30 ms, the snare's shell 40..200 ms (after its glide), the others 20..300 ms */
static double pitch_of(const buf_t *b, uint32_t t, double f)
{
    if (t <= DVT_ROUND)
        return pitch(b, FS * 15u / 100u, FS * 40u / 100u, f / 1.4, f * 1.4);
    if (t == DVT_RIM || t == DVT_CLAVE)
        return pitch(b, 0, FS * 3u / 100u, f / 1.4, f * 1.4);
    if (t == DVT_SNARE)
        return pitch(b, FS * 4u / 100u, FS / 5u, f / 1.4, f * 1.4);
    return pitch(b, FS / 50u, FS * 3u / 10u, f / 1.4, f * 1.4);
}
/* the parameters a voice's pitch is measured at: designed, BEND 0 for the toms (the bend is a sweep), SNAPPY 0
 * for the snare (its shell alone) */
static void pitch_param(dv_param_t *p, uint32_t t)
{
    dv_default(p, t);
    if (t == DVT_TOM || t == DVT_CONGA || t == DVT_SNARE)
        p->extra = 0;
}

static buf_t B, B2;

static void targets(void)
{
    uint32_t t, bad = 0;
    printf("drum_test: %-10s %9s %7s %7s %8s %8s %6s %6s %6s\n", "voice", "pitch Hz", "ct", "t-30 s", "centr Hz",
           "level dB", "peak", "DC %", "stop s");
    for (t = 0; t < DVT_COUNT; t++) {
        dv_param_t p;
        double f = 0, ct = 0, d, c, l, dc;
        char why[200] = "";
        if (pitched(t)) {
            pitch_param(&p, t);
            render(&p, len_of(t), 0, &B);
            f = pitch_of(&B, t, design_hz(t, 0));
            ct = 1200 * log2(f / design_hz(t, 0));
            if (fabs(ct) > 10)
                snprintf(why + strlen(why), sizeof why - strlen(why), " pitch %+.1f ct;", ct);
        }
        dv_default(&p, t);
        render(&p, len_of(t), 0, &B);
        d = t30(&B), c = centroid100(&B), l = rms_db(&B, 0, FS / 5u), dc = dc_share(&B);
        if (fabs(d - TGT[t].t30) > TGT[t].t30 * 0.15 + 0.01)
            snprintf(why + strlen(why), sizeof why - strlen(why), " t-30 %.3f (target %.3f);", d, TGT[t].t30);
        if (fabs(c / TGT[t].centroid - 1) > 0.2)
            snprintf(why + strlen(why), sizeof why - strlen(why), " centroid %.0f (target %.0f);", c, TGT[t].centroid);
        if (fabs(l - TGT[t].level) > 1.5)
            snprintf(why + strlen(why), sizeof why - strlen(why), " level %.1f (target %.1f);", l, TGT[t].level);
        if (dc > 0.01)
            snprintf(why + strlen(why), sizeof why - strlen(why), " DC %.2f %%;", dc * 100);
        if (!B.stop || B.stop > (uint32_t)((4 * d + 0.5) * FS))
            snprintf(why + strlen(why), sizeof why - strlen(why), " stops at %.2f s;", B.stop / (double)FS);
        printf("drum_test: %-10s %9.2f %+7.1f %7.3f %8.0f %8.1f %6d %6.2f %6.2f %s%s\n", TN[t], f, ct, d, c, l, B.peak,
               dc * 100, B.stop / (double)FS, why[0] ? "FAIL" : "ok", why);
        bad += why[0] != 0;
    }
    printf("drum_test: targets (pitch +-10 ct, t-30 +-15 %% + 10 ms, centroid +-20 %%, level +-1.5 dB, DC under 1 %%, "
           "stop (-84 dB) within 4 x t-30 + 0.5 s): %s\n", bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* ----------------------------------------------------------- controls --- */
/* energy-weighted mean time of the first 60 ms (s): later as the clap's teeth spread */
static double onset_time(const buf_t *b)
{
    double s = 0, st = 0;
    uint32_t i;
    for (i = 0; i < FS * 6u / 100u; i++)
        s += (double)b->y[i] * b->y[i], st += (double)b->y[i] * b->y[i] * i / FS;
    return st / (s + 1e-20);
}

static void controls(void)
{
    uint32_t t, bad = 0;
    for (t = 0; t < DVT_COUNT; t++) {
        dv_param_t p;
        double lo, hi, f0, f1, a, b;
        char why[300] = "", info[300] = "";
        /* TUNE: +-3 semitones */
        pitch_param(&p, t);
        p.tune = -48;
        render(&p, len_of(t), 0, &B);
        p.tune = 48;
        render(&p, len_of(t), 0, &B2);
        if (pitched(t)) {
            f0 = pitch_of(&B, t, design_hz(t, -48)), f1 = pitch_of(&B2, t, design_hz(t, 48));
            snprintf(info + strlen(info), sizeof info - strlen(info), " TUNE -3/+3 st: %.1f / %.1f Hz;", f0, f1);
            if (!(f1 > f0) || fabs(1200 * log2(f1 / f0) - 600) > 20)
                snprintf(why + strlen(why), sizeof why - strlen(why), " TUNE: %.1f -> %.1f Hz;", f0, f1);
        } else {                                        /* (the clap: its band, 300 Hz .. 4 kHz) */
            a = t == DVT_CLAP ? band_centroid(&B, 300, 4000) : centroid100(&B);
            b = t == DVT_CLAP ? band_centroid(&B2, 300, 4000) : centroid100(&B2);
            snprintf(info + strlen(info), sizeof info - strlen(info), " TUNE -3/+3 st: centroid %.0f / %.0f;", a, b);
            if (!(b > a))
                snprintf(why + strlen(why), sizeof why - strlen(why), " TUNE: centroid %.0f -> %.0f;", a, b);
        }
        /* DECAY */
        dv_default(&p, t);
        p.decay = 32;
        render(&p, len_of(t), 0, &B);
        lo = t30(&B);
        p.decay = 96;
        render(&p, len_of(t), 0, &B2);
        hi = t30(&B2);
        snprintf(info + strlen(info), sizeof info - strlen(info), " DECAY 32/96: t-30 %.3f / %.3f;", lo, hi);
        if (!(hi > lo))
            snprintf(why + strlen(why), sizeof why - strlen(why), " DECAY: t-30 %.3f -> %.3f;", lo, hi);
        /* TONE: brighter (the kicks: a deeper sweep, a higher dominant frequency in the first 40 ms) */
        dv_default(&p, t);
        p.tone = 16;
        render(&p, len_of(t), 0, &B);
        p.tone = 112;
        render(&p, len_of(t), 0, &B2);
        if (t <= DVT_ROUND) {
            double f = design_hz(t, 0);
            lo = pitch(&B, 0, FS / 25u, f / 1.4, f * 8), hi = pitch(&B2, 0, FS / 25u, f / 1.4, f * 8);
            snprintf(info + strlen(info), sizeof info - strlen(info), " TONE 16/112: 0..40 ms %.1f / %.1f Hz;", lo, hi);
        } else {
            lo = centroid100(&B), hi = centroid100(&B2);
            snprintf(info + strlen(info), sizeof info - strlen(info), " TONE 16/112: centroid %.0f / %.0f;", lo, hi);
        }
        if (!(hi > lo * 1.03))
            snprintf(why + strlen(why), sizeof why - strlen(why), " TONE: %.1f -> %.1f;", lo, hi);
        /* the extra control, in its own direction */
        dv_default(&p, t);
        p.extra = 0;
        render(&p, len_of(t), 0, &B);
        p.extra = 127;
        render(&p, len_of(t), 0, &B2);
        switch (t) {
        case DVT_PUNCH:
        case DVT_ROUND: {                               /* DRIVE: harmonics of the body (30..123 ms above 150 Hz) */
            double c;
            spectrum(&B, FS * 3u / 100u, 4096, 150, &c, &lo);
            spectrum(&B2, FS * 3u / 100u, 4096, 150, &c, &hi);
            snprintf(info + strlen(info), sizeof info - strlen(info), " DRIVE 0/127: body above 150 Hz %.1f / %.1f dB;", lo, hi);
            break;
        }
        case DVT_CLAP:                                  /* SPREAD: the teeth later */
            lo = onset_time(&B) * 1000, hi = onset_time(&B2) * 1000;
            snprintf(info + strlen(info), sizeof info - strlen(info), " SPREAD 0/127: onset %.1f / %.1f ms;", lo, hi);
            break;
        case DVT_TOM:
        case DVT_CONGA: {                               /* BEND: higher at first */
            double f = design_hz(t, 0);
            lo = pitch(&B, 0, FS / 20u, f / 1.4, f * 1.8), hi = pitch(&B2, 0, FS / 20u, f / 1.4, f * 1.8);
            snprintf(info + strlen(info), sizeof info - strlen(info), " BEND 0/127: 0..50 ms %.1f / %.1f Hz;", lo, hi);
            break;
        }
        case DVT_BELL:
        case DVT_CYM:                                   /* STRIKE: the first 20 ms against 0.1..0.2 s */
            lo = rms_db(&B, 0, FS / 50u) - rms_db(&B, FS / 10u, FS / 5u);
            hi = rms_db(&B2, 0, FS / 50u) - rms_db(&B2, FS / 10u, FS / 5u);
            snprintf(info + strlen(info), sizeof info - strlen(info), " STRIKE 0/127: attack %.1f / %.1f dB;", lo, hi);
            break;
        default:                                        /* SNAPPY, NOISE, DRIVE (rim): brighter */
            lo = centroid100(&B), hi = centroid100(&B2);
            snprintf(info + strlen(info), sizeof info - strlen(info), " extra 0/127: centroid %.0f / %.0f;", lo, hi);
            break;
        }
        if (!(hi > lo))
            snprintf(why + strlen(why), sizeof why - strlen(why), " extra: %.2f -> %.2f;", lo, hi);
        if (getenv("VERBOSE") || why[0])
            printf("drum_test: controls %-10s%s\n", TN[t], info);
        if (why[0])
            printf("drum_test: CONTROL FAIL %s:%s\n", TN[t], why);
        bad += why[0] != 0;
    }
    printf("drum_test: controls (TUNE up -> pitch / centroid up, DECAY up -> t-30 up, TONE up -> brighter, each extra "
           "in its direction): %s\n", bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* ------------------------------------------------------------- safety --- */
static void safety(void)
{
    static const uint8_t C[2] = {0, 127};
    static const int16_t TU[2] = {-400, 400};
    uint32_t t, a, b, c, d, cases = 0, bad = 0, knee = 0;
    int32_t worst = 0;
    double worst_dc = 0;
    for (t = 0; t < DVT_COUNT; t++)
        for (a = 0; a < 2u; a++)
            for (b = 0; b < 2u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++) {
                        dv_param_t p;
                        double dc;
                        dv_default(&p, t);
                        p.decay = C[a], p.tone = C[b], p.extra = C[c], p.tune = TU[d], p.level = 127, p.accent = 127;
                        render(&p, len_of(t), 0, &B);
                        dc = dc_share(&B);
                        cases++;
                        if (B.peak > worst)
                            worst = B.peak;
                        knee += B.knee > 0;
                        if (dc > worst_dc)
                            worst_dc = dc;
                        if (B.over || dc > 0.01) {
                            if (bad++ < 20u)
                                printf("drum_test: SAFETY FAIL %s DECAY %u TONE %u extra %u TUNE %d: peak %d, %u samples at full "
                                       "scale, DC %.2f %%\n", TN[t], C[a], C[b], C[c], TU[d], B.peak, B.over, dc * 100);
                        }
                    }
    printf("drum_test: safety: %u renders (every voice at the corners of DECAY, TONE, extra, TUNE +-25 st, LEVEL 127, full "
           "accent): largest peak %d (full scale 32767; %u renders reach the soft knee), DC at most %.2f %% of the peak: %s\n",
           cases, worst, knee, worst_dc * 100, bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* a retrigger at 0.5 s renders what the first hit rendered */
static void retrigger(void)
{
    uint32_t t, bad = 0, at = CTL * 689u;              /* (0.5 s, on a block) */
    for (t = 0; t < DVT_COUNT; t++) {
        dv_param_t p;
        uint32_t i, diff = 0;
        dv_default(&p, t);
        render(&p, at * 2u, 0, &B);
        render(&p, at * 2u, at, &B2);
        for (i = 0; i < at; i++)
            diff += B.y[i] != B2.y[at + i];
        if (diff) {
            printf("drum_test: RETRIGGER FAIL %s: %u samples differ\n", TN[t], diff);
            bad++;
        }
    }
    printf("drum_test: retrigger (every voice from rest: phases, filters, the noise seed): identical renders %s\n",
           bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* ------------------------------------------------------- choke, kick --- */
static void choke(void)
{
    static dv_coef_t c;
    static dv_voice_t v;
    static dv_metal_t mb;
    dv_param_t p;
    uint32_t i, k, at = CTL * 138u;                     /* 100 ms */
    int32_t y[CTL], m[CTL];
    double before, after;
    uint32_t ok;
    dv_default(&p, DVT_HATO);
    dv_setup(&c, &p);
    dv_init(&v, DVT_HATO);
    memset(&mb, 0, sizeof mb);
    dv_metal_tune(&mb, &c);
    dv_trigger(&v);
    for (i = 0; i < at + FS / 10u; i += CTL) {
        if (i == at)
            dv_choke(&v);
        dv_metal_run(&mb, m, CTL);
        dv_run(&c, &v, m, y, CTL);
        for (k = 0; k < CTL; k++)
            B.y[i + k] = y[k] / 32768.0f;
    }
    B.n = at + FS / 10u;
    before = rms_db(&B, at - 441u, at);
    after = rms_db(&B, at + 441u, at + 882u);
    ok = after < before - 60;
    printf("drum_test: choke: the open hat %.1f dB before, %.1f dB 10..20 ms after (at most -60 dB): %s\n", before, after,
           ok ? "ok" : "FAIL");
    fails += !ok;
    /* the DRUM engine: a closed hat chokes the open hat's lane */
    host_tracks_init();
    host_preset(&trk[0], ENGI_DRUM, 0);
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 46, 100);
    for (i = 0; i < 20u; i++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    trk_note_on(&trk[0], 42, 100);
    for (i = 0; i < 20u; i++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    ok = drum_kit[0][DV_HATO].role == DVT_HATO && drum_kit[0][DV_HATO].v.choke && drum_kit[0][DV_HATO].v.q[0] < DV_QUIET &&
         drum_kit[0][DV_HATC].v.live;
    printf("drum_test: choke in the DRUM engine (note 42 after 46): the open hat's lane choked and silent: %s\n",
           ok ? "ok" : "FAIL");
    fails += !ok;
}

static void kick_speaker(void)
{
    uint32_t t, bad = 0;
    for (t = DVT_PUNCH; t <= DVT_ROUND; t++) {
        dv_param_t p;
        double c, a150, a300;
        dv_default(&p, t);
        render(&p, FS * 2u, 0, &B);
        spectrum(&B, 0, 1 << 16, 150, &c, &a150);
        spectrum(&B, 0, 1 << 16, 300, &c, &a300);
        printf("drum_test: kick %s: energy above 150 Hz %.1f dB, above 300 Hz %.1f dB of the whole (at least -12 dB "
               "above 150 Hz): %s\n", TN[t] + 5, a150, a300, a150 >= -12 ? "ok" : "FAIL");
        bad += a150 < -12;
    }
    fails += bad != 0;
}

/* ------------------------------------------------------------- keys --- */
static void keys(void)
{
    /* the first C (key 7) is the GM kick: key -> drum under KIT STD, HAND, CYM, H+CYM (and KICK ROUND on 1, 3) */
    static const uint8_t KEY[8] = {7, 8, 9, 10, 12, 13, 17, 24};   /* C C# D D# F F# A# F */
    static const uint8_t WANT[4][8] = {
        {DVT_PUNCH, DVT_RIM, DVT_SNARE, DVT_CLAP, DVT_TOM, DVT_HATC, DVT_HATO, DVT_BELL},
        {DVT_ROUND, DVT_CLAVE, DVT_SNARE, DVT_CLAP, DVT_CONGA, DVT_HATC, DVT_HATO, DVT_BELL},
        {DVT_PUNCH, DVT_RIM, DVT_SNARE, DVT_CLAP, DVT_TOM, DVT_HATC, DVT_HATO, DVT_CYM},
        {DVT_ROUND, DVT_CLAVE, DVT_SNARE, DVT_CLAP, DVT_CONGA, DVT_HATC, DVT_HATO, DVT_CYM},
    };
    uint32_t v, i, bad = 0;
    host_tracks_init();
    host_preset(&trk[0], ENGI_DRUM, 0);
    song.octave = 0;
    for (v = 0; v < 4u; v++) {
        trk[0].p[P_E6] = (int16_t)(v & 1u);             /* KICK: PUNCH / ROUND */
        trk[0].p[P_E0] = (int16_t)v;                    /* KIT: STD HAND CYM H+CYM */
        for (i = 0; i < 8u; i++) {
            int32_t st, n = drum_keys(&trk[0], KEY[i]);
            uint32_t got = n < 0 ? 99u : drum_gm(trk[0].p, (uint32_t)n, &st);
            if (got != WANT[v][i]) {
                printf("drum_test: KEYS FAIL KICK %d KIT %d key %u: note %d plays %s, not %s\n", trk[0].p[P_E6],
                       trk[0].p[P_E0], KEY[i], n, got < DVT_COUNT ? TN[got] : "-", TN[WANT[v][i]]);
                bad++;
            }
        }
    }
    bad += drum_keys(&trk[0], 7) != 36;
    bad += ENGINES[ENGI_DRUM] != &ENG_DRUM || ENGINES[ENGI_PHYS] != &ENG_PHYS || ENG_PHYS.keys != 0;
    printf("drum_test: keys: the GM drum map, the first C plays KICK, KICK and KIT pick the variants; DRUM is engine "
           "%u, PHYS (%u) keeps the keyboard: %s\n", ENGI_DRUM, ENGI_PHYS, bad ? "FAIL" : "ok");
    fails += bad != 0;
}

/* ------------------------------------------------------- the engine --- */
static uint32_t voices_on(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

static int32_t blocks_peak(uint32_t nb)          /* nb blocks of the mix, its peak */
{
    int32_t o[2 * CTL], pk = 0;
    uint32_t i, k;
    for (i = 0; i < nb; i++) {
        mix_block(o, CTL);
        for (k = 0; k < 2u * CTL; k++)
            pk = abs(o[k]) > pk ? abs(o[k]) : pk;
    }
    return pk;
}

/* part 1 on the DRUM engine's first kit, nothing sounding (host_tracks_init keeps the voices) */
static void kit_fresh(void)
{
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);
    memset(drum_kit, 0, sizeof drum_kit);
    host_preset(&trk[0], ENGI_DRUM, 0);
}

/* one lane struck alone through the engine: the peak of its first nb blocks */
static int32_t lane_alone(uint32_t note, uint32_t nb)
{
    kit_fresh();
    trk_note_on(&trk[0], note, 110);
    return blocks_peak(nb);
}

/* A tom fill must reuse its lane before asking the shared budget for a voice. */
static void lane_budget(void)
{
    static const uint8_t KIT[6] = {36, 38, 39, 42, 45, 56};
    static const uint8_t TOM[4] = {41, 43, 45, 47};
    uint32_t i, p, owner, kills, bad = 0;
    host_tracks_init();
    memset(drum_kit, 0, sizeof drum_kit);
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, sizeof trk[p].v);
    host_preset(&trk[0], ENGI_DRUM, 0);
    host_preset(&trk[1], 0, 0);
    trk[1].p[P_VOICE] = V_POLY;
    trk[1].p[P_SUS] = 127;
    trk_note_on(&trk[1], 48, 100);
    trk_note_on(&trk[1], 60, 100);
    for (i = 0; i < NELEM(KIT); i++)
        trk_note_on(&trk[0], KIT[i], 110);
    blocks_peak(2);
    owner = drum_kit[0][DV_TOM].owner;
    kills = voice_kills;
    bad += voices_busy() != NVOICE || !owner;
    for (i = 0; i < 16u; i++) {
        trk_note_on(&trk[0], TOM[i % NELEM(TOM)], 110);
        bad += drum_kit[0][DV_TOM].owner != owner || voices_busy() != NVOICE || voice_kills != kills;
        blocks_peak(1);
        bad += voices_on(&trk[1]) != 2u || voices_on(&trk[0]) != NELEM(KIT);
    }
    printf("drum_test: tom fill at the 8-voice budget: same lane voice, both synth notes and other drums kept: %s\n",
           bad ? "FAIL" : "ok");
    fails += bad != 0;
    /* A new track can still take a drum voice: no permanent reservation for the kit. */
    host_preset(&trk[2], 0, 0);
    trk[2].p[P_VOICE] = V_MONO;
    trk_note_off(&trk[0], TOM[3]);
    trk_note_on(&trk[2], 72, 100);
    bad = trk[0].v[owner - 1u].stage != 4u;
    /* Retrigger before the stolen voice's fade block must acquire room again. */
    trk_note_on(&trk[0], TOM[0], 110);
    bad += voices_busy() != NVOICE || drum_kit[0][DV_TOM].owner != owner;
    blocks_peak(2);
    bad += voices_busy() != NVOICE || !trk[2].v[0].active || trk[2].v[0].note != 72;
    printf("drum_test: a synth starts over the kit, a stolen drum retriggers before its fade: budget kept: %s\n",
           bad ? "FAIL" : "ok");
    fails += bad != 0;
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, sizeof trk[p].v);
}

static void engine(void)
{
    /* the 8 lanes: KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL (the closed hat first: it would choke the open one) */
    static const uint8_t KIT[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    uint32_t i, k, bad = 0, nv, live = 0, own = 0, quiet = 0;
    int32_t pk;
    char why[256] = "";
    for (i = 0; i < 8u; i++)                             /* each lane alone sounds */
        quiet += lane_alone(KIT[i], 8) < 200;
    kit_fresh();
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], KIT[i], 110);
    pk = blocks_peak(2);
    nv = voices_on(&trk[0]);
    for (k = 0; k < DV_NLANE; k++) {
        const drum_lane_t *L = &drum_kit[0][k];
        live += L->v.live;
        own += L->owner && trk[0].v[L->owner - 1u].active && (uint32_t)trk[0].v[L->owner - 1u].s[0] == k;
    }
    if (quiet || nv != 8u || live != 8u || own != 8u || pk < 1000) {
        snprintf(why, sizeof why, " (%u lanes silent alone, %u voices, %u lanes live, %u owned, peak %d)", quiet, nv, live,
                 own, pk);
        bad++;
    }
    printf("drum_test: engine: the 8 lanes struck at once ring together (8 voices, each lane live on its own voice)%s: %s\n",
           why, why[0] ? "FAIL" : "ok");
    /* the same lane from two notes (low floor tom, then low tom): the second cuts the first */
    kit_fresh();
    trk_note_on(&trk[0], 41, 110);
    blocks_peak(4);
    trk_note_on(&trk[0], 43, 110);
    blocks_peak(2);
    k = voices_on(&trk[0]) == 1u && drum_kit[0][DV_TOM].owner &&
        trk[0].v[drum_kit[0][DV_TOM].owner - 1u].note == 43 && drum_kit[0][DV_TOM].st == DRUM_GM[43 - 35][1];
    printf("drum_test: engine: a lane is mono, notes 41 then 43 (both TOM): one voice, the second hit: %s\n",
           k ? "ok" : "FAIL");
    bad += !k;
    /* a released key does not end the hit (ADSR at SUS 0, REL 0); the voice frees itself once the drum has rung out */
    kit_fresh();
    trk[0].p[P_SUS] = 0;
    trk[0].p[P_REL] = 0;
    trk[0].p[P_DEC] = 0;
    trk_note_on(&trk[0], 49, 110);                       /* crash */
    blocks_peak(1);
    trk_note_off(&trk[0], 49);
    pk = blocks_peak(FS / 2u / CTL);
    k = voices_on(&trk[0]) == 1u && drum_kit[0][DV_BELL].v.live && blocks_peak(4) > 50;
    for (i = 0; i < 20u * FS / CTL && voices_on(&trk[0]); i++)
        blocks_peak(1);
    printf("drum_test: engine: a crash 0.5 s after its key-off still rings (ADSR SUS 0 REL 0), its voice free after "
           "%.1f s: %s\n", (double)i * CTL / FS + 0.5, k && !voices_on(&trk[0]) ? "ok" : "FAIL");
    bad += !k || voices_on(&trk[0]);
    /* VOICE MONO, GLIDE: the kit still plays POLY */
    kit_fresh();
    trk[0].p[P_VOICE] = V_MONO;
    trk[0].p[P_GLIDE] = 60;
    trk_note_on(&trk[0], 36, 110);
    trk_note_on(&trk[0], 38, 110);
    blocks_peak(2);
    k = voices_on(&trk[0]) == 2u && drum_kit[0][DV_KICK].v.live && drum_kit[0][DV_SNARE].v.live;
    printf("drum_test: engine: VOICE MONO and GLIDE set: kick and snare still sound together: %s\n", k ? "ok" : "FAIL");
    bad += !k;
    fails += bad != 0;
}

/* ------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

/* instructions per sample of a voice of type t while it sounds (struck every 0.25 s); metal: the source too
 * (as the voice needs it); t = DVT_COUNT: the metal source alone */
static double voice_cost(uint32_t t, int with_metal)
{
    static dv_coef_t c;
    static dv_voice_t v;
    static dv_metal_t mb;
    static int32_t y[CTL], m[CTL];
    dv_param_t p;
    uint32_t i, live = 0, nb = FS * 4u / CTL;
    uint64_t i0;
    dv_default(&p, t < DVT_COUNT ? t : DVT_HATC);
    dv_setup(&c, &p);
    dv_init(&v, c.type);
    dv_metal_tune(&mb, &c);
    i0 = instr_now();
    for (i = 0; i < nb; i++) {
        if (i % (FS / 4u / CTL) == 0)
            dv_trigger(&v);
        if (with_metal)
            dv_metal_run(&mb, m, CTL);
        if (t < DVT_COUNT) {
            live += v.live || v.trig;
            dv_run(&c, &v, m, y, CTL);
        } else {
            live++;
        }
        __asm__ volatile("" : : "r"(y) : "memory");
    }
    return live ? (double)(instr_now() - i0) / (live * CTL) : 0;
}

static void cost(void)
{
    uint32_t t;
    double metal = voice_cost(DVT_COUNT, 1), sum = 0;
    if (!instr_now()) {
        printf("drum_test: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    printf("drum_test: cost, host instructions per sample while the voice sounds (device estimate: 1.7 %% per 100):\n");
    printf("drum_test:   %-12s %5.0f  ~%.2f %%\n", "metal source", metal, metal * 0.017);
    for (t = 0; t < DVT_COUNT; t++) {
        double c = voice_cost(t, 0);
        printf("drum_test:   %-12s %5.0f  ~%.2f %%%s\n", TN[t], c, c * 0.017, dv_uses_metal(t) ? "  (+ the metal source)" : "");
        if (t != DVT_ROUND && t != DVT_CONGA && t != DVT_CLAVE && t != DVT_CYM)
            sum += c;
    }
    printf("drum_test:   the 8 lanes at once (PUNCH SNARE CLAP HAT CL HAT OP TOM RIM BELL, one metal source): %.0f, ~%.1f %%\n",
           sum + metal, (sum + metal) * 0.017);
}

/* ------------------------------------------------------------ demos --- */
static FILE *wav_open(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *w;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    w = fopen(path, "wb");
    if (w)
        wav_hdr(w, frames);
    return w;
}

/* a voice: designed, then DECAY, TONE, extra low / high and TUNE -5 / +5 st, a hit every 0.6 s (CYM 1.5 s) */
static void demo_voice(const char *dir, uint32_t t)
{
    static const int16_t V[9][4] = {{-1, -1, -1, 0}, {16, -1, -1, 0}, {112, -1, -1, 0}, {-1, 16, -1, 0}, {-1, 112, -1, 0},
                                    {-1, -1, 0, 0}, {-1, -1, 127, 0}, {-1, -1, -1, -80}, {-1, -1, -1, 80}};
    uint32_t step = t == DVT_CYM ? FS * 3u / 2u : FS * 6u / 10u, i, k, h;
    char name[64];
    FILE *w;
    snprintf(name, sizeof name, "voice_%s", TN[t]);
    for (i = 0; name[i]; i++)
        name[i] = name[i] == ' ' ? '_' : name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] | 0x20) : name[i];
    w = wav_open(dir, name, step * 9u);
    if (!w)
        return;
    for (h = 0; h < 9u; h++) {
        dv_param_t p;
        dv_default(&p, t);
        if (V[h][0] >= 0) p.decay = (uint8_t)V[h][0];
        if (V[h][1] >= 0) p.tone = (uint8_t)V[h][1];
        if (V[h][2] >= 0) p.extra = (uint8_t)V[h][2];
        p.tune = V[h][3];
        render(&p, step, 0, &B);
        for (k = 0; k < step; k++) {
            int32_t s = (int32_t)(B.y[k] * 32768.0f);
            wav_put(w, s, s);
        }
    }
    fclose(w);
}

/* the DRUM engine playing a list of (1/16 step, note, velocity) at 120 BPM, with KICK / KIT */
static void demo_seq(const char *dir, const char *name, const uint8_t (*ev)[3], uint32_t nev, uint32_t steps, int16_t kick,
                     int16_t kit)
{
    uint32_t f, frames = steps * (FS / 8u) + FS, e = 0, off = 0, sixteenth = FS / 8u;
    FILE *w = wav_open(dir, name, frames);
    if (!w)
        return;
    host_tracks_init();
    host_preset(&trk[0], ENGI_DRUM, 0);
    trk[0].p[P_E6] = kick;
    trk[0].p[P_E0] = kit;
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t k;
        while (off < e && (ev[off][0] + 1u) * sixteenth <= f)   /* a 1/16 long (the hit rings on) */
            trk_note_off(&trk[0], ev[off++][1]);
        while (e < nev && ev[e][0] * sixteenth <= f) {
            trk_note_on(&trk[0], ev[e][1], ev[e][2]);
            e++;
        }
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(w, o[2 * k], o[2 * k + 1]);
    }
    fclose(w);
}

static void demos(const char *dir)
{
    static uint8_t kit[16][3];
    static const uint8_t BEAT[][3] = {
        {0, 36, 120}, {0, 42, 90}, {2, 42, 70}, {4, 38, 110}, {4, 42, 90}, {6, 42, 70}, {7, 36, 90}, {8, 36, 120},
        {8, 42, 90}, {10, 46, 90}, {12, 38, 110}, {12, 39, 100}, {14, 42, 80}, {15, 37, 90},
        {16, 36, 120}, {16, 42, 90}, {18, 42, 70}, {19, 56, 90}, {20, 38, 110}, {20, 42, 90}, {22, 42, 70},
        {23, 36, 90}, {24, 36, 120}, {24, 42, 90}, {26, 46, 90}, {27, 37, 90}, {28, 38, 110}, {28, 45, 100},
        {29, 47, 100}, {30, 50, 110}, {31, 39, 100},
        {32, 36, 127}, {32, 49, 100}, {34, 42, 70}, {36, 38, 110}, {36, 42, 90}, {38, 42, 70}, {39, 36, 90},
        {40, 36, 120}, {40, 42, 90}, {42, 46, 90}, {44, 38, 110}, {44, 39, 100}, {46, 42, 80}, {47, 75, 90},
        {48, 36, 120}, {48, 42, 90}, {50, 42, 70}, {51, 56, 90}, {52, 38, 110}, {52, 42, 90}, {54, 63, 90},
        {55, 36, 90}, {56, 36, 120}, {56, 42, 90}, {57, 64, 90}, {58, 46, 90}, {60, 38, 120}, {60, 39, 110},
        {61, 38, 90}, {62, 38, 100}, {63, 38, 110},
    };
    uint32_t t, k;
    for (t = 0; t < DVT_COUNT; t++)
        demo_voice(dir, t);
    for (k = 0; k < 8u; k++) {                          /* the 8 keys, from the lowest, every 1/2 bar */
        int32_t n;
        host_tracks_init();
        host_preset(&trk[0], ENGI_DRUM, 0);
        n = drum_keys(&trk[0], k);
        kit[k][0] = (uint8_t)(k * 4u), kit[k][1] = (uint8_t)n, kit[k][2] = 110;
        n = drum_keys(&trk[0], k);
        kit[8u + k][0] = (uint8_t)(32u + k * 4u), kit[8u + k][1] = (uint8_t)n, kit[8u + k][2] = 110;
    }
    demo_seq(dir, "kit_keys", (const uint8_t(*)[3])kit, 16, 64, 0, DK_HCYM);
    demo_seq(dir, "beat_punch", BEAT, sizeof BEAT / sizeof BEAT[0], 64, 0, DK_STD);
    demo_seq(dir, "beat_round", BEAT, sizeof BEAT / sizeof BEAT[0], 64, 1, DK_STD);
    printf("drum_test: demos in %s: %u voices (designed, DECAY / TONE / extra low and high, TUNE -5 / +5 st), the kit's "
           "keys, a beat (PUNCH, ROUND)\n", dir, (uint32_t)DVT_COUNT);
}

int main(int argc, char **argv)
{
    if (!getenv("NOCOST"))
        cost();
    targets();
    controls();
    safety();
    retrigger();
    choke();
    kick_speaker();
    keys();
    lane_budget();
    engine();
    if (argc > 1)
        demos(argv[1]);
    printf("drum_test: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
