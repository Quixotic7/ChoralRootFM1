/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL -> FM6 (src/fm4_convert.c) on the Mac: the retired four-operator engine against its conversion.
 * Built with -DFELUCCA_FM4=1 (run_tests.sh), so DIGITAL itself renders here (src/eng_digital.c) through hostsim.c.
 *   build/host/fm4_test [DEMODIR]          (run_tests.sh: build/fm4_demo)
 * 1. the routing: for each of DIGITAL's 8 algorithms the FM6 algorithm it becomes has DIGITAL's carriers and
 *    modulation routes between the operators it maps to, op 4 lands on the operator with the feedback, the other
 *    two operators are silent (level 0).
 * 2. the presets: each DIGITAL factory preset converts with its name and PTCH = the FM6 preset that covers it.
 * 3. the sound: the 8 DIGITAL factory presets and one sound per algorithm (plus two with operator envelopes), C4
 *    at velocity 100 held 1.2 s, then released: DIGITAL's voice against the converted FM6 voice (no sends):
 *      pitch      the fundamental (autocorrelation, 0.3 s in): within FM4_CENTS
 *      centroid   the spectral centroid at 0.05, 0.3 and 1.0 s: their mean |log2(FM6 / DIGITAL)| within FM4_OCT
 *      envelope   the RMS envelope (10 ms frames, dB, where either is within 50 dB of its peak): the level offset
 *                 (FM6 - DIGITAL over the note) within FM4_LEVEL_LO .. FM4_LEVEL_HI, the mean |difference| after
 *                 that offset within FM4_SHAPE_DB
 *    printed as a table. Close, not exact: the two engines
 *    differ (six operators with exponential stages and msfa's levels against four with DIGITAL's curves). Three
 *    sounds with a noisy feedback (FDBK 28: DIGITAL's op 4 turns to noise whatever its index, FM6's feedback
 *    follows its operator's level) are printed, not checked: a known limit of the conversion.
 * 4. demos into DEMODIR: each sound as <name>_digital.wav and <name>_fm6.wav. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#if !FELUCCA_FM4
#error "fm4_test renders DIGITAL itself: build it with -DFELUCCA_FM4=1"
#endif

#define FM4_CENTS 5.0             /* the fundamental */
#define FM4_OCT 0.75              /* the centroids: mean |log2 ratio| */
#define FM4_LEVEL_LO (-6.0)       /* dB: a lone carrier is up to 6 dB lower in FM6 (fm4_convert.c) */
#define FM4_LEVEL_HI 3.0
#define FM4_SHAPE_DB 3.0          /* mean |dB difference| of the RMS envelopes after the offset */

#define DUR (FS * 2u)
#define REL (FS * 12u / 10u)
#define FRAME 441u

static int bad;
static void check(const char *what, int ok)
{
    printf("fm4: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ------------------------------------------------------------ helpers --- */
static void reset(void)
{
    memset(trk, 0, sizeof trk);
    memset(eng_state, 0, sizeof eng_state);         /* (the parts' engine states: Melodee's voice model) */
    memset(fm6_eff, 0, sizeof fm6_eff);
    memset(digital_env, 0, sizeof digital_env);
    memset(digital_stage, 0, sizeof digital_stage);
    host_tracks_init();
}

static void render(uint32_t note, uint32_t vel, double *y)   /* track 1's voices (dry), note held until REL */
{
    static int32_t b[CTL];
    uint32_t i, k;
    trk_note_on(&trk[0], note, vel);
    for (i = 0; i < DUR; i += CTL) {
        if (i == REL)
            trk_note_off(&trk[0], note);
        memset(b, 0, sizeof b);
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < DUR; k++)
            y[i + k] = b[k];
    }
}

#define NFFT 4096
static double centroid(const double *y, uint32_t a)   /* Hz, of y[a .. a + NFFT) (Hann) */
{
    static double re[NFFT], im[NFFT];
    uint32_t i, j, len;
    double num = 0, den = 0;
    for (i = 0; i < NFFT; i++) {
        re[i] = y[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT));
        im[i] = 0;
    }
    for (i = 1, j = 0; i < NFFT; i++) {
        uint32_t bit = NFFT >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= NFFT; len <<= 1) {
        double ang = -2 * M_PI / len;
        for (i = 0; i < NFFT; i += len)
            for (j = 0; j < len / 2; j++) {
                double wr = cos(ang * j), wi = sin(ang * j);
                double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j + len / 2] = re[i + j] - xr, im[i + j + len / 2] = im[i + j] - xi;
                re[i + j] += xr, im[i + j] += xi;
            }
    }
    for (i = 1; i < NFFT / 2; i++) {
        double p = re[i] * re[i] + im[i] * im[i];
        num += p * i * FS / NFFT;
        den += p;
    }
    return den > 0 ? num / den : 0;
}

/* the period (Hz) of y[a .. a + NFFT) near f: the autocorrelation's highest peak within a semitone and a half of
 * f's period, refined (a parabola); 0 = none (a pitch moved that far, or silence) */
static double pitch(const double *y, uint32_t a, double f)
{
    uint32_t lag, lo = (uint32_t)(FS / f / 1.09), hi = (uint32_t)(FS / f * 1.09) + 1u, i, best = 0;
    double r[3], top = -1, r0 = 0;
    for (i = 0; i < NFFT; i++)
        r0 += y[a + i] * y[a + i];
    if (r0 <= 0)
        return 0;
    for (lag = lo + 1; lag < hi; lag++) {
        double s[3];
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            s[k] = 0;
            for (i = 0; i + lag + 1u < NFFT; i++)
                s[k] += y[a + i] * y[a + i + lag + k - 1u];
        }
        if (s[1] >= s[0] && s[1] >= s[2] && s[1] > top) {
            top = s[1];
            best = lag;
            memcpy(r, s, sizeof r);
        }
    }
    if (!best || top <= 0)
        return 0;
    {
        double d = r[0] - 2 * r[1] + r[2];
        return (double)FS / (best + (d ? 0.5 * (r[0] - r[2]) / d : 0));
    }
}

static void env_db(const double *y, double *e)          /* DUR / FRAME frames */
{
    uint32_t f, i;
    for (f = 0; f < DUR / FRAME; f++) {
        double s = 0;
        for (i = 0; i < FRAME; i++)
            s += y[f * FRAME + i] * y[f * FRAME + i];
        e[f] = 10 * log10(s / FRAME + 1e-6);
    }
}

static void wav(const char *dir, const char *name, const char *kind, const double *y)
{
    char path[512];
    FILE *f;
    uint32_t i;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s_%s.wav", dir, name, kind);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, DUR);
    for (i = 0; i < DUR; i++) {
        int32_t s = (int32_t)(y[i] * 0.7);
        wav_put(f, s, s);
    }
    fclose(f);
}

/* ------------------------------------------------------------ routing --- */
/* the modulators of each FM6 operator (bit k: operator k + 1) of algorithm a, its carriers, and its feedback one,
 * read from fm6_core.c FM6_ALG as the renderer walks it (the sixth operator first, buses written then read) */
static void fm6_routes(uint32_t a, uint32_t *mods, uint32_t *car, uint32_t *fbop)
{
    uint32_t bus[3] = {0, 0, 0}, k;
    *car = 0;
    *fbop = 0;
    for (k = 0; k < 6u; k++) {
        uint32_t f = FM6_ALG[a][k], op = 6u - k, in = (f >> 4) & 3u, out = f & 3u;
        mods[op] = in ? bus[in] : 0u;
        if ((f & 0xc0u) == 0xc0u)
            *fbop = op;
        if (out)
            bus[out] = (f & 4u ? bus[out] : 0u) | 1u << (op - 1u);
        else
            *car |= 1u << (op - 1u);
    }
}

static void routing(void)
{
    /* DIGITAL (eng_digital.c switch (alg)): per op 1..4 the ops modulating it (bit k: op k + 1), the carriers */
    static const uint8_t DMOD[8][5] = {{0, 2, 4, 8, 0}, {0, 2, 12, 0, 0}, {0, 10, 4, 0, 0}, {0, 6, 0, 8, 0},
                                       {0, 2, 0, 8, 0}, {0, 8, 8, 8, 0}, {0, 0, 0, 8, 0}, {0, 0, 0, 0, 0}};
    static const uint8_t DCAR[8] = {1, 1, 1, 1, 5, 7, 7, 15};
    uint32_t a, k, j, routes = 1, carriers = 1, fb = 1, silent = 1;
    for (a = 0; a < 8u; a++) {
        int16_t p[P_COUNT];
        uint8_t v[FP_SIZE + 1u];
        uint32_t mods[7], car, fbop, used = 0;
        const uint8_t *map = FM4_ALG[a].op;
        for (k = 0; k < P_COUNT; k++)
            p[k] = param_desc_of(ENGI_DIGITAL, k)->def;
        p[P_E0] = (int16_t)a;
        fm4_convert(p, v);
        fm6_routes(v[FP_ALG], mods, &car, &fbop);
        for (k = 0; k < 4u; k++) {
            uint32_t want = 0, got = 0;
            used |= 1u << (map[k] - 1u);
            for (j = 0; j < 4u; j++) {
                want |= ((DMOD[a][k + 1] >> j) & 1u) << j;
                got |= ((mods[map[k]] >> (map[j] - 1u)) & 1u) << j;
            }
            routes &= want == got;
            carriers &= ((DCAR[a] >> k) & 1u) == ((car >> (map[k] - 1u)) & 1u);
        }
        fb &= fbop == map[3];
        for (k = 1; k <= 6u; k++)
            if (!((used >> (k - 1u)) & 1u))
                silent &= v[(6u - k) * FP_OP + FP_OL] == 0;
    }
    check("routing: each of the 8 algorithms keeps DIGITAL's modulation routes between the mapped operators", routes);
    check("routing: .. and its carriers", carriers);
    check("routing: op 4 lands on the FM6 operator with the feedback", fb);
    check("routing: the two operators left over are silent (level 0)", silent);
}

static void presets(void)
{
    uint32_t k, ok = 1, names = 1;
    for (k = 0; k < FM4_NPRESETS; k++) {
        int16_t p[P_COUNT];
        uint8_t v[FP_SIZE + 1u];
        uint32_t i;
        for (i = 0; i < P_COUNT; i++)
            p[i] = TP[i < P_E0 ? i : 0].def;
        fm4_preset_values(p, k);
        ok &= fm4_convert(p, v) == FM4_TO_FM6[k] && p[P_E7] == FM4_TO_FM6[k] && !p[P_E0] && !p[P_E6];
        for (i = 0; i < 10u && DIGITAL_PRESETS[k].name[i]; i++)
            names &= v[FP_NAME + i] == (uint8_t)DIGITAL_PRESETS[k].name[i];
    }
    check("presets: each converts to PTCH = the FM6 preset that covers it, the macros neutral", ok);
    check("presets: the patch is named after the DIGITAL preset", names);
}

/* --------------------------------------------------------------- sound --- */
typedef struct {
    const char *name;
    int preset;                  /* DIGITAL factory preset, or -1: alg / e / env / op below */
    int16_t e[7], env[4];
    int16_t op[4][5];            /* the OP ENV values (all 0: the defaults) */
    uint8_t report;              /* 1: printed, not checked (a known limit) */
} case_t;

static const case_t CASES[] = {
    {"E.PIANO", 0}, {"BELL", 1}, {"BASS", 2}, {"BRASS", 3}, {"ORGAN", 4}, {"PAD", 5}, {"MARIMBA", 6},
    {"FUNK_KEY", 7},
    {"ALG1", -1, {0, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG2", -1, {1, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG3", -1, {2, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG4", -1, {3, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG5", -1, {4, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG6", -1, {5, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG7", -1, {6, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"ALG8", -1, {7, 1, 3, 1, 64, 70, 12}, {2, 80, 90, 50}},
    {"NOISYFB_A1", -1, {0, 1, 3, 1, 64, 70, 28}, {2, 80, 90, 50}, {{0}}, 1},
    {"NOISYFB_A3", -1, {2, 1, 3, 1, 64, 70, 28}, {2, 80, 90, 50}, {{0}}, 1},
    {"NOISYFB_A8", -1, {7, 1, 3, 1, 64, 70, 28}, {2, 80, 90, 50}, {{0}}, 1},
    {"OPENV_PLUCK", -1, {0, 1, 2, 1, 80, 60, 10}, {0, 70, 100, 40},
     {{0, 70, 30, 30, 127}, {0, 50, 0, 20, 110}, {0, 0, 127, 0, 127}, {0, 0, 127, 0, 127}}},
    {"OPENV_SWELL", -1, {4, 2, 1, 3, 60, 80, 0}, {10, 60, 110, 70},
     {{60, 0, 127, 0, 127}, {80, 0, 127, 0, 90}, {0, 0, 127, 0, 127}, {30, 40, 60, 50, 127}}},
};
#define NCASE (sizeof CASES / sizeof CASES[0])

static double yd[DUR], yf[DUR], ed[DUR / FRAME], ef[DUR / FRAME];

static void sound(const char *dir)
{
    uint32_t c, k, n_ok[4] = {0}, nchk = 0;
    double worst[4] = {0};
    printf("fm4: %-12s %8s %8s %6s | %7s %7s %7s %6s | %6s %6s %5s\n", "sound", "f0 DIG", "f0 FM6", "cents",
           "c.05", "c.3", "c1.0", "oct", "level", "shape", "corr");
    for (c = 0; c < NCASE; c++) {
        const case_t *cs = &CASES[c];
        track_t *t = &trk[0];
        int16_t p[P_COUNT];
        uint8_t v[FP_SIZE + 1u];
        double f0d, f0f, cents, oct = 0, lvl, shape = 0, corr, sd = 0, sf = 0, md = 0, mf = 0, sdd = 0, sff = 0,
               sdf = 0, pk = -200;
        static const uint32_t AT[3] = {FS / 20u, FS * 3u / 10u, FS};
        uint32_t n = 0;
        /* DIGITAL */
        reset();
        host_preset(t, ENGI_DIGITAL, cs->preset < 0 ? 0u : (uint32_t)cs->preset);
        if (cs->preset < 0) {
            for (k = 0; k < 7u; k++)
                t->p[P_E0 + k] = cs->e[k];
            for (k = 0; k < 4u; k++)
                t->p[P_ATK + k] = cs->env[k];
            t->p[P_ED_FLT] = 0;
            for (k = 0; k < 20u; k++)
                if (cs->op[0][4] || cs->op[1][4])   /* (a case with OP ENV values) */
                    t->p[P_FM1_ATK + k] = cs->op[k / 5u][k % 5u];
        }
        t->p[P_VOICE] = V_POLY;
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        memcpy(p, t->p, sizeof p);
        render(60, 100, yd);
        /* FM6, converted */
        reset();
        host_preset(t, ENGI_FM6, 0);
        memcpy(t->p, p, sizeof p);
        fm4_convert(t->p, v);
        t->eng_req = t->engine = ENGI_FM6;
        fm6_set_patch(0, v);
        fm6_slot[0] = (uint8_t)t->p[P_E7];
        render(60, 100, yf);
        wav(dir, cs->name, "digital", yd);
        wav(dir, cs->name, "fm6", yf);
        /* pitch, centroids */
        f0d = pitch(yd, FS * 3u / 10u, 261.63);
        f0f = pitch(yf, FS * 3u / 10u, 261.63);
        cents = f0d > 0 && f0f > 0 ? 1200 * log2(f0f / f0d) : 9999;
        for (k = n = 0; k < 3u; k++) {               /* (where both have gone silent: not counted) */
            double a = centroid(yd, AT[k]), b = centroid(yf, AT[k]);
            if (a > 0 || b > 0)
                oct += a > 0 && b > 0 ? fabs(log2(b / a)) : 9, n++;
        }
        oct /= n ? n : 1;
        n = 0;

        /* the RMS envelopes */
        env_db(yd, ed);
        env_db(yf, ef);
        for (k = 0; k < DUR / FRAME; k++)
            pk = ed[k] > pk ? ed[k] : ef[k] > pk ? ef[k] : pk;
        for (k = 0; k < REL / FRAME; k++)            /* the level over the note */
            if (ed[k] > pk - 50 || ef[k] > pk - 50) {
                sd += pow(10, ed[k] / 10);
                sf += pow(10, ef[k] / 10);
            }
        lvl = 10 * log10((sf + 1e-9) / (sd + 1e-9));
        for (k = 0; k < DUR / FRAME; k++)
            if (ed[k] > pk - 50 || ef[k] > pk - 50) {
                double a = ed[k] < pk - 60 ? pk - 60 : ed[k], b = (ef[k] - lvl) < pk - 60 ? pk - 60 : ef[k] - lvl;
                shape += fabs(b - a);
                md += a, mf += b, n++;
            }
        shape /= n ? n : 1;
        md /= n ? n : 1;
        mf /= n ? n : 1;
        for (k = 0; k < DUR / FRAME; k++)
            if (ed[k] > pk - 50 || ef[k] > pk - 50) {
                double a = (ed[k] < pk - 60 ? pk - 60 : ed[k]) - md;
                double b = ((ef[k] - lvl) < pk - 60 ? pk - 60 : ef[k] - lvl) - mf;
                sdd += a * a, sff += b * b, sdf += a * b;
            }
        corr = sdd > 0 && sff > 0 ? sdf / sqrt(sdd * sff) : 1;
        printf("fm4: %-12s %8.2f %8.2f %6.1f | %7.0f %7.0f %7.0f %6.2f | %+6.1f %6.2f %5.2f%s\n", cs->name, f0d, f0f, cents,
               centroid(yd, AT[0]), centroid(yd, AT[1]), centroid(yd, AT[2]), oct, lvl, shape, corr,
               cs->report ? "  (reported)" : "");
        printf("fm4: %-12s %8s %8s %6s | %7.0f %7.0f %7.0f %6s |\n", "", "", "(FM6)", "", centroid(yf, AT[0]),
               centroid(yf, AT[1]), centroid(yf, AT[2]), "");
        if (cs->report)
            continue;
        nchk++;
        n_ok[0] += fabs(cents) <= FM4_CENTS;
        n_ok[1] += oct <= FM4_OCT;
        n_ok[2] += lvl >= FM4_LEVEL_LO && lvl <= FM4_LEVEL_HI;
        n_ok[3] += shape <= FM4_SHAPE_DB;
        worst[0] = fabs(cents) > worst[0] ? fabs(cents) : worst[0];
        worst[1] = oct > worst[1] ? oct : worst[1];
        worst[2] = fabs(lvl) > worst[2] ? fabs(lvl) : worst[2];
        worst[3] = shape > worst[3] ? shape : worst[3];
    }
    {
        char b[160];
        snprintf(b, sizeof b, "pitch: %u of %u within %.0f cents (worst %.1f)", n_ok[0], nchk, FM4_CENTS, worst[0]);
        check(b, n_ok[0] == nchk);
        snprintf(b, sizeof b, "centroid: %u of %u within %.2f octave on average (worst %.2f)", n_ok[1], nchk, FM4_OCT,
                 worst[1]);
        check(b, n_ok[1] == nchk);
        snprintf(b, sizeof b, "level: %u of %u within %+.0f .. %+.0f dB (largest |offset| %.1f dB)", n_ok[2], nchk,
                 FM4_LEVEL_LO, FM4_LEVEL_HI, worst[2]);
        check(b, n_ok[2] == nchk);
        snprintf(b, sizeof b, "RMS envelope: %u of %u within %.0f dB mean difference after the offset (worst %.2f)", n_ok[3],
                 nchk, FM4_SHAPE_DB, worst[3]);
        check(b, n_ok[3] == nchk);

    }
}

#ifndef FM4_NO_MAIN                 /* (an experiment can include this file) */
int main(int argc, char **argv)
{
    routing();
    presets();
    sound(argc > 1 ? argv[1] : 0);
    printf("fm4: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
#endif
