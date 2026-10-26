/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot's delay and reverb (firmware/src/cr_fx.c, docs/FX.md) on the host, through hostsim.c with CR_FX 1:
 *   build/host/cr_fx_test [OUTDIR]            (sh tests/run_cr_tests.sh; OUTDIR: build/fx-review)
 *  1. the coefficient arithmetic: e^-x, the fourth root, the knobs' curves;
 *  2. tempo sync: the two sides' times in samples for a table of tempi, divisions and spreads, and the first echo of
 *     an impulse where the table says;
 *  3. the half rate: the delay path's frequency response (flat to 9 kHz, gone by 12.5 kHz), what a 15 kHz send leaves
 *     below 11 kHz (the alias), and the path's noise under a -12 dB sine;
 *  4. stability: every corner of the parameters, an impulse and 10 s of the loudest send (noise, then a square), then
 *     silence, 60 s in all: bounded, no offset, and exactly 0 at the end (DECAY Inf: never louder than it was);
 *  5. the reverb's bound: at DECAY Inf with the lines swinging and SIZE swept, the lines' energy never grows without
 *     an input;
 *  6. smooth changes: Time, Spread, Sync, Mode, Size, Pre-delay swept under a held tone: no sample step above what
 *     the steady signal has;
 *  7. the level against Felucca's delay and ROOM at their defaults (a copy below), the same send: the reverb's wet
 *     under a held chord and the delay's first repeat (both sides' energy, Stereo and ping-pong) within 1 dB;
 *  8. mono: Spread 0 in Stereo mode is the same sample left and right; ping-pong's sum has every repeat;
 *  9. silence in, silence out: after the tail both effects switch off (exact zeros) and the block costs almost
 *     nothing; the decay times against the knob;
 * 10. the cost: host instructions a 128-frame block with both fully on, and emu.c's device estimate (budget: 350 us);
 * 11. the master filter (0.22, cr_fx.c xq_block): Off is bit-exact (the default, FX off, the cuts at their Off ends,
 *     and again after a cut was in), the response of each cut at a few corners and both slopes against the one-pole's
 *     own formula, the extremes (both cuts in at their far ends, a loud square and noise over the clamp: bounded,
 *     exact zeros after silence), a cutoff swept and the filter and the slope toggled under a held tone (no step
 *     above the tone's own), and the cost a half of one cut and of both;
 * 12. the routing (cr_fx.c THE ROUTING, the second design; the FX layer's Routing page): Parallel at every one of the
 *     24 orders is the render without it, every sample; Chain Chorus > Delay > Reverb > Filter with the chord part's
 *     sends 100 / 100 / 0: the delay's wet carries the chorus, the reverb's is exactly 0; the filter's place (a high
 *     tone, the High cut at its lowest: the reverb's wet far smaller with the filter before it); a full-scale square
 *     at full sends and five orders bounded, exact zeros after the tail; FX off in Chain the dry mix bit for bit; the
 *     cost a half of Parallel and Chain against no routing.
 * WAVs for tools/fx_plots.py into OUTDIR: the reverb's impulse responses (small, default, large, Inf), the delay's
 * repeats (stereo, ping-pong, spread).
 *   build/host/cr_fx_test --chorus-listen DIR   the chorus's listening files of 0.21 (tools/chorus_glide_listen.sh) */
#define CR_FX 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("cr_fx: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static uint32_t xs = 0x1234567u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}
static const char *outdir = "build/fx-review";

/* ------------------------------------------------------------------ driving --- */
static void fx_reset(void)
{
    memset(&xf, 0, sizeof xf);
    memset(&xq, 0, sizeof xq);
    fx_off = 0;
    memset(xd_buf, 0, sizeof xd_buf);
    memset(xr_buf, 0, sizeof xr_buf);
    memset(xr_pbuf, 0, sizeof xr_pbuf);
    memset(&xg, 0, sizeof xg);
    xf_defaults();
    xf_bpm = 120;
}
#define MAXS (122u * 44100u)
static int32_t *WL, *WR;                                /* the wet, a run */
/* a run of `n` samples (a multiple of CTL): gen(i) is the send's sample; which: bit 0 into the delay, bit 1 the reverb.
 * hook (if any) runs before every block (its first sample's index) */
typedef int32_t (*gen_t)(uint32_t i);
typedef void (*hook_t)(uint32_t i);
static void run(uint32_t n, gen_t gen, uint32_t which, hook_t hook)
{
    static int32_t sd[CTL], sr[CTL], ml[CTL], mr[CTL];
    uint32_t i, k;
    for (i = 0; i < n && i + CTL <= MAXS; i += CTL) {       /* (whole blocks: up to CTL - 1 samples past n) */
        if (hook)
            hook(i);
        for (k = 0; k < CTL; k++) {
            int32_t v = gen ? gen(i + k) : 0;
            sd[k] = which & 1u ? v : 0;
            sr[k] = which & 2u ? v : 0;
            ml[k] = mr[k] = 0;
        }
        xf_block(sd, sr, ml, mr, CTL);
        memcpy(WL + i, ml, sizeof ml);
        memcpy(WR + i, mr, sizeof mr);
    }
}
static void wav_write(const char *name, uint32_t n, double gain)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s", outdir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    wav_hdr(f, n);
    for (i = 0; i < n; i++)
        wav_put(f, (int32_t)(WL[i] * gain), (int32_t)(WR[i] * gain));
    fclose(f);
}
static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)(b - a));
}
static double db(double x) { return 20.0 * log10(x > 1e-9 ? x : 1e-9); }

/* the sends of the tests */
static int32_t g_imp(uint32_t i) { return i == 64u ? 80000 : 0; }
static double g_f = 1000.0, g_a = 20000.0;
static uint32_t g_len = 0xFFFFFFFFu;
static int32_t g_sine(uint32_t i) { return i < g_len ? (int32_t)(g_a * sin(2.0 * M_PI * g_f * i / 44100.0)) : 0; }
static int32_t g_noise(uint32_t i) { return i < g_len ? noise((int32_t)g_a) : 0; }
/* the loudest send: 4 parts' clamps (+-524287 each); noise, then a 110 Hz square */
static int32_t g_loud(uint32_t i)
{
    if (i == 64u) return 2097148;
    if (i < 5u * 44100u) return noise(2097148);
    if (i < 10u * 44100u) return (i / 200u) & 1u ? 2097148 : -2097148;
    return 0;
}
/* a held chord (D3 F#3 A3 D4, twelve harmonics each, 1 / n), ~+-14000 at its peaks: a loud pad's send */
static int32_t g_chord(uint32_t i)
{
    static const double F[4] = {146.83, 185.0, 220.0, 293.66};
    double s = 0, t = i / 44100.0, e;
    uint32_t k, h;
    if (i >= g_len)
        return 0;
    for (k = 0; k < 4u; k++)
        for (h = 1; h <= 12u; h++)
            s += sin(2.0 * M_PI * F[k] * h * t + k * 1.3 + h * 0.7) / h;
    e = t < 0.01 ? t / 0.01 : 1.0;
    return (int32_t)(s * e * 3000.0);
}

/* -------------------------------------------- Felucca's buses, for the level --- */
static int16_t old_dly[65536], old_comb[1116 + 1188 + 1277 + 1356], old_ap[556 + 441];
static void old_run(uint32_t n, gen_t gen, uint32_t which, int32_t *wet)
{
    static const uint16_t RC[4] = {1116, 1188, 1277, 1356}, RA[2] = {556, 441};
    uint32_t dly_w = 0, ci[4] = {0}, ai[2] = {0}, i, k, dl = 44100u * 60u / 120u / 2u;   /* 1/8 at 120 */
    int32_t dly_lp = 0, clp[4] = {0};
    int32_t fb = 60 * 230, col = 2000 + 70 * 240, dmix = 90 * 258, size = 25000 + 90 * 50, damp = 32767 - 60 * 200;
    memset(old_dly, 0, sizeof old_dly);
    memset(old_comb, 0, sizeof old_comb);
    memset(old_ap, 0, sizeof old_ap);
    for (i = 0; i < n; i++) {
        int32_t s = gen(i), y = 0, x, a = 0, in;
        int16_t *c = old_comb;
        if (which & 1u) {
            x = old_dly[(dly_w - dl) & 65535u];
            dly_lp += mulq15(x - dly_lp, col);
            old_dly[dly_w & 65535u] = (int16_t)clamp((s >> 1) + mulq15(dly_lp, fb), -32768, 32767);
            dly_w++;
            y += mulq15(x << 1, dmix);
        }
        if (which & 2u) {
            in = mulq15(s, 2580);
            for (k = 0; k < 4u; k++) {
                int32_t o = c[ci[k]];
                clp[k] = o + mulq15(clp[k] - o, 32767 - damp);
                c[ci[k]] = (int16_t)clamp(in + mulq15(clp[k], size), -32768, 32767);
                if (++ci[k] >= RC[k]) ci[k] = 0;
                a += o;
                c += RC[k];
            }
            c = old_ap;
            for (k = 0; k < 2u; k++) {
                int32_t o = c[ai[k]], v = a + (o >> 1);
                c[ai[k]] = (int16_t)clamp(v, -32768, 32767);
                a = o - a;
                if (++ai[k] >= RA[k]) ai[k] = 0;
                c += RA[k];
            }
            y += a;
        }
        wet[i] = y;
    }
}

/* ----------------------------------------------------------------- the tests --- */
#define DSD CR_FX_DELAY_DS
#define DSR CR_FX_REVERB_DS
static const char *sfx = "";                            /* the demos' file suffix (argv[2]: "_full", "_half") */

static void t_maths(void)
{
    uint32_t y, ok = 1, p;
    double worst = 0;
    for (y = 0; y < 8u * 65536u; y += 97u) {
        double want = exp(-(double)y / 65536.0) * 32768.0, got = xf_expn(y), e = (fabs(got - want) - 1.5) / want;
        if (e > worst) worst = e;
    }
    printf("cr_fx:   e^-x: the worst error %.3f %% past 1.5 of 32768\n", worst * 100.0);
    check("e^-x (the lines' gains, the filters' coefficients) within 1.5 / 32768 + 0.3 % over 0 .. 8", worst < 0.003);
    for (y = 1; y < 32768u; y += 13u) {
        double want = pow(y / 32768.0, 0.25) * 32768.0, got = xf_isqrt(xf_isqrt(y << 15) << 15);
        if (fabs(got - want) > 3.0 + want * 0.001) ok = 0;
    }
    check("the fourth root (the reverb's input gain against its decay)", ok);
    ok = xf_ms(0) == 16u && xf_ms(127) <= XD_MS_MAX && xf_ms(127) >= 740u && xf_rt_ms(0) == 300u && xf_rt_ms(126) > 60000u &&
         xf_lowcut_hz(0) == 20u && xf_lowcut_hz(127) == 500u;
    for (p = 1; p < 128u; p++)
        if (xf_ms(p) < xf_ms(p - 1u) || xf_rt_ms(p) <= xf_rt_ms(p - 1u) || xf_lowcut_hz(p) < xf_lowcut_hz(p - 1u)) ok = 0;
    printf("cr_fx:   the delay at %u Hz: a line of %u samples, the longest time %u ms; the reverb at %u Hz\n", (unsigned)XD_FS,
           (unsigned)XD_N, (unsigned)XD_MS_MAX, (unsigned)XR_FS);
    check("the knobs' curves rise: time 16 ms .. the line, decay 0.3 .. 72 s, low cut 20 .. 500 Hz", ok);
    /* the rings' layout, as the block-wise loops need it (cr_fx.c xr_run): every write offset a multiple of a block (a
     * block's writes never wrap), every line its length and its guard clear of the line before it, every diffuser's
     * read a block clear of what the stage before it writes */
    ok = XR_BASE[0] >= XR_LEN[0] + XR_GUARD && XR_BASE[7] + 16u <= 16384u + XR_BASE[0] - XR_LEN[0] - XR_GUARD;
    for (p = 0; p < 8u; p++)
        if (XR_BASE[p] % 16u || (p && XR_BASE[p] < XR_BASE[p - 1u] + 16u + XR_LEN[p] + XR_GUARD - 16u)) ok = 0;
    for (p = 0; p < 4u; p++)
        if (XR_APB[p] % 16u || XR_APB[p] < (p ? XR_APB[p - 1u] : 0u) + 16u + XR_AP[p]) ok = 0;
    if (XR_APB[3] + XR_PRE_MAX + 2u > 4096u) ok = 0;
    check("the rings: write offsets in whole blocks, each line and diffuser clear of the one before it", ok);
    {   /* the one-poles' coefficients: the corner where it is asked */
        double worstk = 0;
        static const uint32_t HZ[6] = {20, 40, 500, 2000, 8000, 12000};
        for (p = 0; p < 6u; p++) {
            double want = (1.0 - exp(-2.0 * M_PI * HZ[p] / XD_FS)) * 32768.0, got = xf_k(HZ[p], XD_FS);
            if (fabs(got - want) / want > worstk) worstk = fabs(got - want) / want;
        }
        check("a one-pole's coefficient for 20 Hz .. 12 kHz within 2 %", worstk < 0.02);
    }
}

static void delay_plain(void)                           /* Stereo, no feedback, no wow, Tone open, a short free time */
{
    fx_reset();
    xfp[XP_DMODE] = 0; xfp[XP_DFDBK] = 0; xfp[XP_DWOW] = 0; xfp[XP_DTONE] = 127; xfp[XP_DSYNC] = 0; xfp[XP_DTIME] = 20;
}
static void t_sync(void)
{
    static const uint16_t BPM[] = {20, 60, 90, 100, 120, 121, 140, 174, 240, 300};
    static const char *const DN[XF_NDIV] = {"1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4",
                                            "1/2T", "1/4.", "1/2"};
    uint32_t b, d, q, ok = 1, l, r, shown = 0;
    for (b = 0; b < sizeof BPM / sizeof BPM[0]; b++)
        for (d = 0; d < XF_NDIV; d++)
            for (q = 0; q < 4u; q++) {
                double want = (double)XD_FS * 60.0 / BPM[b] * XF_DIV[d] / 48.0, wr;
                fx_reset();
                xf_bpm = BPM[b];
                xfp[XP_DSYNC] = 1;
                xfp[XP_DDIV] = (int16_t)d;
                xfp[XP_DRATIO] = (int16_t)q;
                xf_delay_times(&l, &r);
                while (want > XD_MAX + 0.5) want /= 2.0;
                wr = want * XF_RATIO[q][0] / XF_RATIO[q][1];
                if (fabs(l - want) > 1.01 || fabs(r - wr) > 1.51 || l > XD_MAX || r > l) {
                    ok = 0;
                    printf("cr_fx:   %u BPM %s ratio %u: %u / %u samples, want %.1f / %.1f\n", BPM[b], DN[d], q, l, r, want, wr);
                }
                if (BPM[b] == 120 && (d == 6u || d == 8u || d == 9u) && shown++ < 12u)
                    printf("cr_fx:   120 BPM %-5s %u:%u  L %5u R %5u samples (%.1f / %.1f ms)\n", DN[d], XF_RATIO[q][0],
                           XF_RATIO[q][1], l, r, l * 1000.0 / XD_FS, r * 1000.0 / XD_FS);
            }
    check("sync: 10 tempi x 13 divisions x 4 spreads: L the division within a sample (halved to fit the line), R its part", ok);
    {   /* the echo is where the table says: an impulse, Stereo, no feedback; the first repeat's peak */
        static const struct { uint16_t bpm, div, ratio; } C[4] = {{120, 6, 0}, {120, 8, 1}, {90, 9, 2}, {140, 3, 3}};
        uint32_t k, i;
        ok = 1;
        for (k = 0; k < 4u; k++) {
            uint32_t pl = 0, pr = 0;
            int32_t ml = 0, mr = 0;
            delay_plain();
            xf_bpm = C[k].bpm;
            xfp[XP_DSYNC] = 1;
            xfp[XP_DDIV] = (int16_t)C[k].div;
            xfp[XP_DRATIO] = (int16_t)C[k].ratio;
            xf_delay_times(&l, &r);
            run(2u * 44100u, g_imp, 1, 0);
            for (i = 0; i < 2u * 44100u; i++) {
                if (abs(WL[i]) > ml) ml = abs(WL[i]), pl = i;
                if (abs(WR[i]) > mr) mr = abs(WR[i]), pr = i;
            }
            /* (the impulse is at sample 64; at the half rate the two half-band filters delay it a few samples) */
            if (abs((int32_t)pl - (int32_t)(64u + DSD * l)) > 14 || abs((int32_t)pr - (int32_t)(64u + DSD * r)) > 14) ok = 0;
            printf("cr_fx:   %u BPM %s %u:%u: the echo's peak at %u / %u (the times: %u / %u at 44.1 kHz)\n", C[k].bpm,
                   DN[C[k].div], XF_RATIO[C[k].ratio][0], XF_RATIO[C[k].ratio][1], pl - 64u, pr - 64u, DSD * l, DSD * r);
        }
        check("sync: an impulse's first repeat lands on each side's time (within 14 samples)", ok);
    }
}

/* the amplitude of frequency f in x[a..b) (a projection on its sine and cosine) */
static double tone_amp(const int32_t *x, uint32_t a, uint32_t b, double f)
{
    double s = 0, c = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        s += x[i] * sin(2.0 * M_PI * f * i / 44100.0);
        c += x[i] * cos(2.0 * M_PI * f * i / 44100.0);
    }
    return 2.0 * sqrt(s * s + c * c) / (double)(b - a);
}
static void t_band(void)
{
    static const double F[] = {50, 100, 200, 1000, 3000, 5000, 7000, 9000, 10000, 12000, 15000, 18000};
    double ref = 0, g[12], lo = 0, hi = 0, top = DSD == 1 ? 15000 : 9000;
    uint32_t k, n = 44100u, a = 22050u;
    char line[160];
    printf("cr_fx:   the delay path (Tone open, no feedback, one repeat), dB re 1 kHz:");
    for (k = 0; k < 12u; k++) {
        delay_plain();
        g_f = F[k];
        g_a = 8000;
        g_len = 0xFFFFFFFFu;
        run(n, g_sine, 1, 0);
        g[k] = tone_amp(WL, a, n, F[k]);
        if (F[k] == 1000) ref = g[k];
    }
    for (k = 0; k < 12u; k++) {
        double d = db(g[k] / ref);
        printf(" %g:%.1f", F[k], d);
        if (F[k] >= 200 && F[k] <= top) { if (d < lo) lo = d; if (d > hi) hi = d; }
    }
    printf("\n");
    snprintf(line, sizeof line, "the delay's band: 200 Hz .. %g kHz within +0.5 / -%s dB with Tone open", top / 1000.0,
             DSD == 1 ? "2.5" : "1.5");
    check(line, hi < 0.5 && lo > (DSD == 1 ? -2.5 : -1.5));
    if (DSD == 2) {   /* a 15 kHz send: what folds below 11 kHz (15 kHz would land on 7.05 kHz); loud, to see it at all */
        double al;
        g_f = 15000;
        g_a = 100000;
        delay_plain();
        run(n, g_sine, 1, 0);
        al = tone_amp(WL, a, n, 22050.0 - 15000.0);
        printf("cr_fx:   a 15 kHz send leaves %.1f dB at 7.05 kHz (re a 1 kHz send's repeat)\n", db(al / (ref * 100000.0 / 8000.0)));
        check("the half rate: the alias of a 15 kHz send is below -50 dB", db(al / (ref * 100000.0 / 8000.0)) < -50.0);
    }
    {   /* distortion: a 1 kHz send's repeat at -12 dB and at 0 dB of the line (whole cycles in the window) */
        static const double LV[2] = {16384.0, 65534.0};
        double hd[2];
        uint32_t q, h;
        for (q = 0; q < 2u; q++) {
            double e = 0, sig;
            delay_plain();
            g_f = 1000;
            g_a = LV[q];
            run(n, g_sine, 1, 0);
            sig = tone_amp(WL, a, n, 1000.0);
            for (h = 2; h <= 9u; h++) e += pow(tone_amp(WL, a, n, 1000.0 * h), 2);
            hd[q] = db(sqrt(e) / sig);
        }
        printf("cr_fx:   a 1 kHz send's repeat: harmonics %.1f dB at -12 dB of the line (under the saturator's knee), %.1f dB at "
               "full scale (in it)\n", hd[0], hd[1]);
        check("the delay is clean under its knee: a -12 dB repeat's harmonics below -80 dB", hd[0] < -80.0);
    }
}

/* THE DROP-OUT (the owner, on the unit: "the delay just abruptly drops out even with the feedback turned up ... even
 * with a short delay time"): a 50 ms burst into the delay, its tail's level in 250 ms windows. A loop that loses
 * nothing but its gain falls by the same number of dB every window; the first build's fell faster and faster and was
 * gone at -45 dBFS. Here: from the second window down to -84 dBFS of the output no window falls more than 1.6 x the
 * tail's own first slope + 1 dB, and the tail is still there when a straight exponential says it should be */
static double g_bamp = 40000.0;
static int32_t g_tone_burst(uint32_t i) { return i < 2205u ? (int32_t)(g_bamp * sin(2.0 * M_PI * 440.0 * i / 44100.0)) : 0; }
static void t_dropout(void)
{
    static const struct { const char *name; int16_t time, fb; double amp; uint32_t secs; } C[] = {
        {"16 ms, feedback 127, a loud burst", 0, 127, 40000.0, 14}, {"16 ms, feedback 127, a quiet burst (-30 dB)", 0, 127, 1265.0, 12},
        {"16 ms, feedback 118", 0, 118, 40000.0, 8}, {"60 ms, feedback 127, a quiet burst", 40, 127, 1265.0, 30},
        {"250 ms, feedback 110", 91, 110, 12000.0, 40}, {"the longest time, feedback 120, a quiet burst", 127, 120, 4000.0, 110}};
    uint32_t c, q;
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        double lv[440], first = 0, worst = 0, floor_db = -84.0;
        uint32_t nq = C[c].secs * 4u, n = nq * 11040u, last = 0, ok = 1, q0 = C[c].time >= 100 ? 8u : 1u, per = C[c].time >= 100 ? 8u : 1u;
        char line[200];
        fx_reset();
        xfp[XP_DSYNC] = 0; xfp[XP_DTIME] = C[c].time; xfp[XP_DFDBK] = C[c].fb; xfp[XP_DTONE] = 127; xfp[XP_DWOW] = 0;
        xfp[XP_DMODE] = 0;
        g_bamp = C[c].amp;
        run(n, g_tone_burst, 1, 0);
        nq /= per;                                       /* (a long time: windows of two seconds hold whole repeats) */
        for (q = 0; q < nq; q++)
            lv[q] = db(rms(WL, q * per * 11040u, (q + 1u) * per * 11040u) / 32768.0);
        (void)q0;
        first = (lv[1] - lv[5]) / 4.0;
        for (q = 1; q + 1u < nq && lv[q + 1u] > floor_db; q++) {
            double fall = lv[q] - lv[q + 1u];
            if (fall > worst) worst = fall;
            if (fall > 1.6 * first + 1.0) ok = 0;
            last = q + 1u;
        }
        snprintf(line, sizeof line, "no drop-out: %s: %.1f dB a %s all the way to %.0f dBFS (the steepest %.1f), %.1f s of tail",
                 C[c].name, first, per == 1u ? "quarter second" : "two seconds", lv[last], worst, (last + 1u) * 0.25 * per);
        check(line, ok && lv[last] < floor_db + 12.0 && first > 0.05);
    }
}

/* THE TAIL'S NOISE: the effect run on a send and on the same send upside down; the two wets are each other's mirror
 * but for what the rounding adds, so half their sum is the noise (and any limit cycle). In 100 ms windows while the
 * tail falls from -20 to -80 dBFS: the noise's level, and how far under the tail it is */
static int32_t g_neg(uint32_t i) { return -g_tone_burst(i); }
static int32_t g_chord_stab(uint32_t i);
static int32_t g_chord_neg(uint32_t i);
static void t_noise(void)
{
    static int32_t *al, *ar;
    uint32_t which, n = 30u * 44100u, i, w;
    al = malloc(n * sizeof *al);
    ar = malloc(n * sizeof *ar);
    for (which = 1; which <= 2u; which++) {
        double nmax = -200, worst_snr = 200, at20 = 0, at50 = 0, at80 = 0;
        char line[200];
        fx_reset();
        if (which == 1u) { xfp[XP_DFDBK] = 110; xfp[XP_DWOW] = 24; }
        else { xfp[XP_RDECAY] = 72; }
        run(n, g_chord_stab, which, 0);
        memcpy(al, WL, n * sizeof *al);
        memcpy(ar, WR, n * sizeof *ar);
        fx_reset();
        if (which == 1u) { xfp[XP_DFDBK] = 110; xfp[XP_DWOW] = 24; }
        else { xfp[XP_RDECAY] = 72; }
        run(n, g_chord_neg, which, 0);
        for (w = 4; w < n / 4410u; w++) {
            double s = 0, e = 0, sd, nd;
            for (i = w * 4410u; i < (w + 1u) * 4410u; i++) {
                double x = al[i], y = (al[i] + WL[i]) / 2.0;
                s += x * x;
                e += y * y;
            }
            sd = db(sqrt(s / 4410.0) / 32768.0);
            nd = db(sqrt(e / 4410.0) / 32768.0);
            if (sd > -20.0 || sd < -80.0)
                continue;
            if (nd > nmax) nmax = nd;
            if (sd - nd < worst_snr) worst_snr = sd - nd;
            if (!at20) at20 = nd;
            if (sd < -50.0 && !at50) at50 = nd;
            at80 = nd;
        }
        snprintf(line, sizeof line, "%s tail from -20 to -80 dBFS: its noise %.0f / %.0f / %.0f dBFS at -20 / -50 / -80 (the loudest "
                 "%.0f), never less than %.0f dB under the tail", which == 1u ? "the delay's" : "the reverb's", at20, at50, at80, nmax,
                 worst_snr);
        check(line, nmax < (which == 1u ? -86.0 : -83.0) && worst_snr > 2.5);
    }
    free(al);
    free(ar);
}

/* the corners */
typedef struct { const char *name; int16_t p[XP_N]; uint16_t bpm; } corner_t;
#define DL(time, div, spr, rat, fb, sync, mode, tone, wow) time, div, spr, rat, fb, sync, mode, tone, wow
#define RV(size, damp, decay, pre, mod, low, width) size, damp, decay, pre, mod, low, width
static const corner_t CORNERS[] = {
    {"defaults", {DL(91, 6, 0, 0, 58, 1, 1, 70, 24), RV(88, 56, 52, 16, 56, 60, 127)}, 120},
    {"delay: the longest, feedback max, tone open, wow max, stereo; reverb: size max, decay 126, damp 0, mod max",
     {DL(127, 12, 0, 0, 127, 0, 0, 127, 127), RV(127, 0, 126, 127, 127, 0, 127)}, 120},
    {"delay: 16 ms, feedback max, tone shut, ping-pong, spread max; reverb: size 0, decay 126, damp max",
     {DL(0, 0, 127, 3, 127, 0, 1, 0, 0), RV(0, 127, 126, 0, 0, 127, 0)}, 120},
    {"delay: synced 1/2 at 20 BPM (folds), ratio 1:2, feedback max, wow max; reverb: decay 0, size max, low cut max",
     {DL(64, 12, 64, 3, 127, 1, 1, 127, 127), RV(127, 64, 0, 127, 127, 127, 64)}, 20},
    {"delay: synced 1/32 at 300 BPM, feedback 100, tone open; reverb: size 0, decay 0, damp 0, mod max",
     {DL(64, 0, 0, 0, 100, 1, 0, 127, 127), RV(0, 0, 0, 0, 127, 0, 127)}, 300},
    {"reverb: decay Inf, size max, damp 0, mod 0 (the frozen hall); delay: feedback 0",
     {DL(80, 6, 0, 0, 0, 1, 1, 84, 0), RV(127, 0, 127, 0, 0, 0, 127)}, 120},
    {"reverb: decay Inf, size max, damp 0, mod max, pre max; delay: feedback max, the longest",
     {DL(127, 12, 0, 0, 127, 0, 1, 127, 127), RV(127, 0, 127, 127, 127, 0, 127)}, 120},
    {"reverb: decay Inf, size 0, damp max, mod max",
     {DL(80, 6, 0, 0, 58, 1, 1, 84, 24), RV(0, 127, 127, 0, 127, 127, 127)}, 120},
};
static void t_corners(void)
{
    uint32_t k, i, n = 60u * 44100u;
    for (k = 0; k < sizeof CORNERS / sizeof CORNERS[0]; k++) {
        const corner_t *c = &CORNERS[k];
        double pk = 0, mean = 0, r10, r60, step = 0;
        uint32_t zero_from = n, live;
        int lng = c->p[XP_RDECAY] >= 80 || (c->p[XP_DFDBK] >= 120 && c->p[XP_DTONE] >= 64), ok;
        char line[256];
        fx_reset();
        memcpy(xfp, c->p, sizeof xfp);
        xf_bpm = c->bpm;
        xs = 0x1234567u;
        run(n, g_loud, 3, 0);
        for (i = 0; i < n; i++) {
            double a = fabs((double)WL[i]) > fabs((double)WR[i]) ? fabs((double)WL[i]) : fabs((double)WR[i]);
            if (a > pk) pk = a;
            if (WL[i] | WR[i]) zero_from = n;
            else if (zero_from == n) zero_from = i;
            if (i >= 5u * 44100u && i < 10u * 44100u && i && fabs((double)WL[i] - WL[i - 1u]) > step) step = fabs((double)WL[i] - WL[i - 1u]);
        }
        for (i = n - 10u * 44100u; i < n; i++) mean += WL[i] + WR[i];
        mean /= 2.0 * 10.0 * 44100.0;
        r10 = rms(WL, 10u * 44100u, 11u * 44100u);
        r60 = rms(WL, 59u * 44100u, 60u * 44100u);
        live = xf.d_live + xf.r_live + xf.up_live;
        /* each effect's wet is bounded by its saturated lines: the delay's 65534 x 4.1, the reverb's 131068 x 1.2 */
        ok = pk <= 65534.0 * 4.1 + 131068.0 * 1.3 && (lng ? r60 <= r10 * 1.001 + 1.0 && fabs(mean) < 8.0 + r60 * 0.03
                                                         : zero_from < n && !live);
        snprintf(line, sizeof line, "stable 60 s at +36 dB (4 parts' sends at their clamps): %.60s", c->name);
        check(line, ok);
        if (lng)
            printf("cr_fx:   peak %.0f; the wet 1 s after the input %.0f rms, 50 s later %.0f rms (%.1f dB), the last 10 s' mean %.2f\n",
                   pk, r10, r60, db(r60 / (r10 > 1 ? r10 : 1)), mean);
        else
            printf("cr_fx:   peak %.0f; exactly 0 from %.2f s after the input stopped, both effects off\n", pk, zero_from / 44100.0 - 10.0);
    }
}

/* the reverb's bound: the lines' energy, sampled once a second with no input */
static double lines_energy(void)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < XR_N; i++)
        e += (double)xr_buf[i] * xr_buf[i];
    return e;
}
static uint32_t sweep_from, sweep_ms = 30;
static void h_size_sweep(uint32_t i)
{
    if (i >= sweep_from && !((i - sweep_from) % (sweep_ms * 44100u / 1000u / CTL * CTL))) {
        uint32_t k = (i - sweep_from) / (sweep_ms * 44100u / 1000u / CTL * CTL);
        xfp[XP_RSIZE] = (int16_t)((k / 127u) & 1u ? 127 - k % 127u : k % 127u);
    }
}
static void t_bound(void)
{
    uint32_t s, ok = 1;
    double e0, e = 0, worst = 0, e30 = 0;
    fx_reset();
    xfp[XP_RDECAY] = 127; xfp[XP_RDAMP] = 0; xfp[XP_RMOD] = 0; xfp[XP_RSIZE] = 127; xfp[XP_RLOW] = 0;
    g_a = 30000;
    g_len = 3u * 44100u;
    xs = 99u;
    run(3u * 44100u, g_noise, 2, 0);
    run(44096u, 0, 2, 0);
    e0 = lines_energy();
    for (s = 0; s < 30u; s++) {                          /* Mod 0: the frozen hall holds */
        run(44096u, 0, 2, 0);
        e = lines_energy();
        if (e > e0 * 1.02) ok = 0;                       /* (the lines' own rounding: +-0.1 dB) */
    }
    e30 = e;
    printf("cr_fx:   DECAY Inf, MOD 0, no input: the lines' energy after 30 s %.2f dB of its start (1 - 2^-13 a pass: -0.7 dB)\n",
           10.0 * log10(e30 / e0));
    check("Inf holds: with Mod 0 the frozen hall loses under 1.5 dB in 30 s, and never gains", ok && e30 > e0 * 0.70);
    xfp[XP_RMOD] = 127;
    sweep_from = 0;
    e0 = lines_energy();
    ok = 1;
    for (s = 0; s < 40u; s++) {
        run(44096u, 0, 2, s >= 10u && s < 25u ? h_size_sweep : 0);
        e = lines_energy();
        if (e / e0 > worst) worst = e / e0;
        if (e > e0 * 1.26) ok = 0;                       /* (1 dB: a second's swing of the modulated lines) */
    }
    printf("cr_fx:   DECAY Inf, MOD 127, no input for 40 s (SIZE swept 0 .. 127 .. 0 from 10 to 25 s): the lines' energy at most "
           "%.2f dB of where it started, %.1f dB at the end\n", 10.0 * log10(worst), 10.0 * log10(e / e0));
    check("the reverb's loop never gains: the lines' energy with no input stays within 1 dB of its start, and falls", ok && e < e0);
}

/* smooth changes. A step of d in a signal shows as a second difference of d; a smooth signal's second difference
 * is a small part of its first (a 220 Hz sine: 3 %; played 1.5 x as fast by a glide: 5 %). The measure: the largest
 * second difference of the wet as a part of the largest first difference in the window (d1 out) */
static double max_step(uint32_t a, uint32_t b, double *d1)
{
    double m1 = 0, m2 = 0;
    uint32_t i;
    for (i = a + 2u; i < b; i++) {
        double p = fabs((double)WL[i] - WL[i - 1u]), q = fabs((double)WR[i] - WR[i - 1u]);
        double p2 = fabs((double)WL[i] - 2.0 * WL[i - 1u] + WL[i - 2u]), q2 = fabs((double)WR[i] - 2.0 * WR[i - 1u] + WR[i - 2u]);
        if (p > m1) m1 = p;
        if (q > m1) m1 = q;
        if (p2 > m2) m2 = p2;
        if (q2 > m2) m2 = q2;
    }
    if (d1)
        *d1 = m1;
    return m2 / (m1 > 1.0 ? m1 : 1.0);
}
static uint32_t sw_par, sw_lo, sw_hi, sw_ms;
static void h_sweep(uint32_t i)                         /* a detent every sw_ms, up then down; a toggle every 400 ms */
{
    uint32_t per = (sw_hi - sw_lo <= 3u ? 400u : sw_ms) * 44100u / 1000u / CTL * CTL, k;
    if (per < CTL) per = CTL;
    if (i < sweep_from || (i - sweep_from) % per)
        return;
    k = (i - sweep_from) / per;
    if (sw_hi - sw_lo <= 3u)
        xfp[sw_par] = (int16_t)(sw_lo + k % (sw_hi - sw_lo + 1u));
    else {
        uint32_t span = sw_hi - sw_lo, ph = k % (2u * span);
        xfp[sw_par] = (int16_t)(sw_lo + (ph < span ? ph : 2u * span - ph));
    }
}
static void h_tempo(uint32_t i)                         /* the tempo 120 -> 60 -> 180 in steps of 1 BPM every 30 ms */
{
    uint32_t per = 30u * 44100u / 1000u / CTL * CTL, k;
    if (i < sweep_from || (i - sweep_from) % per)
        return;
    k = (i - sweep_from) / per;
    xf_bpm = (uint16_t)(k < 60u ? 120u - k : k < 180u ? 60u + (k - 60u) : 180u);
}
static void t_smooth(void)
{
    static const struct { const char *name; uint8_t which, par, lo, hi, sync; } S[] = {
        {"Time (free)", 1, XP_DTIME, 0, 127, 0}, {"Time (synced divisions)", 1, XP_DDIV, 0, 12, 1},
        {"Spread (free)", 1, XP_DSPREAD, 0, 127, 0}, {"Spread (synced)", 1, XP_DRATIO, 0, 3, 1},
        {"Sync on / off", 1, XP_DSYNC, 0, 1, 1}, {"Mode stereo / ping-pong", 1, XP_DMODE, 0, 1, 1},
        {"Tone", 1, XP_DTONE, 0, 127, 1}, {"Wow", 1, XP_DWOW, 0, 127, 1}, {"Feedback", 1, XP_DFDBK, 0, 127, 1},
        {"Size", 2, XP_RSIZE, 0, 127, 1}, {"Pre-delay", 2, XP_RPRE, 0, 127, 1}, {"Decay", 2, XP_RDECAY, 0, 127, 1},
        {"Damp", 2, XP_RDAMP, 0, 127, 1}, {"Mod", 2, XP_RMOD, 0, 127, 1}, {"Low cut", 2, XP_RLOW, 0, 127, 1},
        {"Width", 2, XP_RWIDTH, 0, 127, 1}};
    static const uint16_t SPEED[3] = {30, 6, 1};        /* ms a detent: a hand, a fast hand, faster than any knob */
    uint32_t k, n = 12u * 44100u, sp, bad_n = 0;
    double base[3] = {0, 0, 0}, d1, worst = 0;
    const char *wname = "";
    g_f = 220.0;
    g_a = 16000.0;
    g_len = 0xFFFFFFFFu;
    for (k = 1; k <= 2u; k++) {                         /* the steady signal's own, each effect */
        fx_reset();
        run(4u * 44100u, g_sine, k, 0);
        base[k] = max_step(3u * 44100u, 4u * 44100u, &d1);
    }
    printf("cr_fx:   a held 220 Hz send, steady: the wet's largest second difference is %.1f %% (delay), %.1f %% (reverb) of its "
           "largest step\n", base[1] * 100.0, base[2] * 100.0);
    for (sp = 0; sp < 3u; sp++)
        for (k = 0; k < sizeof S / sizeof S[0]; k++) {
            double m;
            fx_reset();
            xfp[XP_DSYNC] = S[k].sync;
            sw_par = S[k].par;
            sw_lo = S[k].lo;
            sw_hi = S[k].hi;
            sw_ms = SPEED[sp];
            sweep_from = 2u * 44096u;
            run(n, g_sine, S[k].which, h_sweep);
            m = max_step(2u * 44096u, n, &d1);
            if (m > worst) { worst = m; wname = S[k].name; }
            if (m > 0.25) {
                bad_n++;
                printf("cr_fx:   %s at %u ms a detent: second difference %.1f %% of the largest step (%.0f)\n", S[k].name, SPEED[sp],
                       m * 100.0, d1);
            }
        }
    {
        char line[200];
        snprintf(line, sizeof line, "no click: 16 parameters swept under a held tone and its tail at 30, 6 and 1 ms a detent: the "
                 "worst second difference %.1f %% of the largest step (%s)", worst * 100.0, wname);
        check(line, !bad_n);
    }
    fx_reset();
    sweep_from = 2u * 44096u;
    run(n, g_sine, 1, h_tempo);
    {
        double m = max_step(2u * 44096u, n, &d1);
        char line[200];
        snprintf(line, sizeof line, "no click: the tempo stepped 120 -> 60 -> 180 BPM under a synced delay: second difference %.1f %%",
                 m * 100.0);
        check(line, m <= 0.25);
    }
}

/* overdrive: a 220 Hz send 12 dB over full scale into both effects at their defaults: every stage saturates softly
 * or at a clamp, nothing wraps (a wrap is a step of a whole word: the largest sample step stays a small part of the
 * peak), and the wet is the same shape upside down for the send upside down */
static double g_od = 131068.0;
static int32_t g_over(uint32_t i) { return (int32_t)(g_od * sin(2.0 * M_PI * 220.0 * i / 44100.0)); }
static void t_overdrive(void)
{
    uint32_t i, n = 4u * 44100u;
    double pk = 0, step = 0;
    fx_reset();
    run(n, g_over, 3, 0);
    for (i = 44100u; i < n; i++) {
        double a = fabs((double)WL[i]), d = fabs((double)WL[i] - WL[i - 1u]);
        if (a > pk) pk = a;
        if (d > step) step = d;
    }
    printf("cr_fx:   a 220 Hz send at +12 dB into both: the wet peaks at %.0f (%.1f x full scale), its largest sample step %.0f "
           "(%.1f %% of the peak)\n", pk, pk / 32768.0, step, step / pk * 100.0);
    check("overdrive: +12 dB into both effects saturates without a wrap or an edge (the largest step under 12 % of the peak)",
          pk < 65534.0 * 4.1 + 131068.0 * 1.3 && step < pk * 0.12);
}

static void t_level(void)
{
    static int32_t ow[4u * 44100u];
    uint32_t n = 4u * 44100u - (4u * 44100u) % CTL, a = 44100u, b = 3u * 44100u;
    double o, nw;
    g_len = 3u * 44100u;
    /* the reverb */
    old_run(n, g_chord, 2, ow);
    o = rms(ow, a, b);
    fx_reset();
    run(n, g_chord, 2, 0);
    nw = sqrt((pow(rms(WL, a, b), 2) + pow(rms(WR, a, b), 2)) / 2.0);
    printf("cr_fx:   a held chord's reverb, 1 .. 3 s: Felucca's ROOM %.0f rms, the new one %.0f rms a side (%+.2f dB)\n", o, nw, db(nw / o));
    check("level: the reverb's wet at its defaults within 1 dB of ROOM's at its defaults (the same send)", fabs(db(nw / o)) < 1.0);
    /* the delay: a repeat's power in the room. Felucca's played each repeat on both sides; Stereo does the same, and
     * ping-pong plays it on one side 3 dB up. The first repeat of a 10 ms burst of the chord, both sides' energy */
    {
        static const char *const MD[2] = {"Stereo", "ping-pong"};
        uint32_t m, i, dl = 44100u * 60u / 120u / 2u;   /* 1/8 at 120 BPM */
        double eo = 0, en;
        g_len = 441u;
        old_run(n, g_chord, 1, ow);
        for (i = dl - 100u; i < dl + 1500u; i++) eo += 2.0 * (double)ow[i] * ow[i];
        for (m = 0; m < 2u; m++) {
            char line[160];
            fx_reset();
            xfp[XP_DMODE] = (int16_t)m;
            xfp[XP_DWOW] = 0;
            run(n, g_chord, 1, 0);
            for (en = 0, i = dl - 100u; i < dl + 1500u; i++) en += (double)WL[i] * WL[i] + (double)WR[i] * WR[i];
            snprintf(line, sizeof line, "level: the delay's first repeat in %s: %+.2f dB of Felucca's (both sides' energy, the same send)",
                     MD[m], 10.0 * log10(en / eo));
            check(line, fabs(10.0 * log10(en / eo)) < 1.0);
        }
    }
}

static int32_t g_burst(uint32_t i) { return i < 4410u ? g_chord(i) : 0; }
static int32_t g_chord_stab(uint32_t i) { g_len = 0xFFFFFFFFu; return i < 22050u ? g_chord(i) : 0; }
static int32_t g_chord_neg(uint32_t i) { return -g_chord_stab(i); }
static void t_mono(void)
{
    uint32_t i, n = 4u * 44100u - (4u * 44100u) % CTL, same = 1;
    double sum, l, r;
    g_len = 0xFFFFFFFFu;
    fx_reset();
    xfp[XP_DMODE] = 0;
    xfp[XP_DWOW] = 127;
    xfp[XP_DFDBK] = 100;
    run(n, g_burst, 1, 0);
    for (i = 0; i < n; i++)
        if (WL[i] != WR[i]) same = 0;
    check("mono: Stereo mode at Spread 0 is the same sample left and right (the wow at its deepest too)", same && rms(WL, 0, n) > 100.0);
    fx_reset();
    xfp[XP_DWOW] = 0;
    run(n, g_burst, 1, 0);
    {   /* ping-pong: L + R holds both sides' energy (the repeats do not overlap, nothing cancels) */
        static int32_t m[4u * 44100u];
        for (i = 0; i < n; i++) m[i] = WL[i] + WR[i];
        sum = rms(m, 0, n);
        l = rms(WL, 0, n);
        r = rms(WR, 0, n);
    }
    printf("cr_fx:   ping-pong at Spread 0: L %.0f rms, R %.0f rms, L + R %.0f rms (their power sum: %.0f)\n", l, r, sum, sqrt(l * l + r * r));
    check("mono: ping-pong's L + R keeps every repeat (within 0.5 dB of the two sides' power)", fabs(db(sum / sqrt(l * l + r * r))) < 0.5);
}

/* RT60 from an impulse response: Schroeder's backward integral, the -5 .. -35 dB slope */
static double rt60(uint32_t n)
{
    static double e[MAXS / 64u];
    uint32_t nb = n / 64u, i, k, a = 0, b = 0;
    double tot = 0, acc = 0;
    for (k = 0; k < nb; k++) {
        e[k] = 0;
        for (i = 0; i < 64u; i++)
            e[k] += (double)WL[k * 64u + i] * WL[k * 64u + i] + (double)WR[k * 64u + i] * WR[k * 64u + i];
        tot += e[k];
    }
    for (k = 0; k < nb; k++) {
        double d = 10.0 * log10((tot - acc) / tot + 1e-30);
        acc += e[k];
        if (!a && d < -5.0) a = k;
        if (!b && d < -35.0) b = k;
    }
    return b > a ? (b - a) * 64.0 / 44100.0 * 2.0 : 0.0;
}
static int32_t g_click(uint32_t i) { return i >= 64u && i < 68u ? 120000 : 0; }
static int32_t g_bang(uint32_t i) { return i < 2205u ? noise(120000) : 0; }   /* 50 ms, the loudest a line takes */
static uint64_t cost_of(const int32_t *d, const int32_t *r, uint32_t reps)
{
    static int32_t ml[CTL], mr[CTL];
    uint64_t t0 = instr_now();
    uint32_t j;
    for (j = 0; j < reps; j++) xf_block(d, r, ml, mr, CTL);
    return (instr_now() - t0) * 4u / reps;
}
static void t_silence(void)
{
    static const struct { uint8_t decay, size, damp, mod; double lo, hi; } R[] = {   /* (DAMP 0, MOD 0: the gains alone) */
        {0, 88, 0, 0, 0.22, 0.41}, {32, 88, 0, 0, 0.89, 1.62}, {48, 88, 0, 0, 1.78, 3.24}, {64, 88, 0, 0, 3.55, 6.5},
        {80, 127, 0, 0, 7.1, 13.0}, {48, 0, 0, 0, 1.78, 3.24}, {48, 127, 0, 0, 1.78, 3.24},
        {48, 88, 56, 56, 1.0, 3.24}, {48, 88, 127, 56, 0.3, 2.4}};
    uint32_t k, n, i, ok = 1;
    for (k = 0; k < sizeof R / sizeof R[0]; k++) {
        double t;
        fx_reset();
        xfp[XP_RDECAY] = R[k].decay;
        xfp[XP_RSIZE] = R[k].size;
        xfp[XP_RDAMP] = R[k].damp;
        xfp[XP_RMOD] = R[k].mod;
        xfp[XP_RLOW] = 0;
        n = (uint32_t)(xf_rt_ms(R[k].decay) * 1.6 * 44.1) + 2u * 44100u;
        n -= n % CTL;
        run(n, g_click, 2, 0);
        t = rt60(n);
        printf("cr_fx:   DECAY %3u (%.1f s), SIZE %3u, DAMP %3u, MOD %3u: RT60 of a single click %.2f s\n", R[k].decay,
               xf_rt_ms(R[k].decay) / 1000.0, R[k].size, R[k].damp, R[k].mod, t);
        if (t < R[k].lo || t > R[k].hi) ok = 0;
    }
    check("the reverb's RT60 is DECAY's (0.3 .. 9.6 s within a factor of 1.35, DAMP and MOD 0, a click) at any SIZE; DAMP shortens it", ok);
    {   /* every tail ends: DECAY up to 19 s x SIZE x DAMP x MOD, a loud 50 ms burst, then silence for 10 RT60 + 20 s */
        static const uint8_t DC[4] = {0, 40, 72, 96}, SZ[3] = {0, 64, 127}, DM[2] = {0, 127}, MD[2] = {0, 127};
        uint32_t a, b, c, d, cases = 0, ended = 0;
        double worst = 0;
        for (a = 0; a < 4u; a++)
            for (b = 0; b < 3u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++) {
                        fx_reset();
                        xfp[XP_RDECAY] = DC[a];
                        xfp[XP_RSIZE] = SZ[b];
                        xfp[XP_RDAMP] = DM[c];
                        xfp[XP_RMOD] = MD[d];
                        xfp[XP_RLOW] = 0;
                        n = (uint32_t)(xf_rt_ms(DC[a]) * 10.0 * 44.1) + 20u * 44100u;
                        if (n > MAXS - CTL) n = MAXS - CTL;
                        run(n, g_bang, 2, 0);
                        for (i = n; i > 0u && !(WL[i - 1u] | WR[i - 1u]); i--) {}
                        cases++;
                        if (!xf.r_live && !xf.up_live && i < n) ended++;
                        else printf("cr_fx:   DECAY %u SIZE %u DAMP %u MOD %u: not over after %.0f s\n", DC[a], SZ[b], DM[c], MD[d], n / 44100.0);
                        if (i / 44100.0 / (xf_rt_ms(DC[a]) / 1000.0) > worst) worst = i / 44100.0 / (xf_rt_ms(DC[a]) / 1000.0);
                    }
        printf("cr_fx:   %u of %u tails ended in exact silence, the reverb off; the longest took %.1f x its RT60\n", ended, cases, worst);
        check("every tail ends: DECAY 0.3 .. 19 s x SIZE x DAMP x MOD after a loud burst: exactly 0 and the reverb off", ended == cases);
    }
    /* the defaults, a chord stab into both: when do they switch off, and what does a block cost then */
    fx_reset();
    g_len = 0xFFFFFFFFu;
    n = 60u * 44100u - (60u * 44100u) % CTL;
    run(n, g_burst, 3, 0);
    for (i = n; i > 0u && !(WL[i - 1u] | WR[i - 1u]); i--) {}
    printf("cr_fx:   a 0.1 s chord stab into both at the defaults: the last non-zero wet sample at %.2f s; live counters %u %u %u\n",
           i / 44100.0, xf.d_live, xf.r_live, xf.up_live);
    check("silence in, silence out: the wet is exactly 0 and both effects are off within 60 s of a stab (defaults)",
          i < n - 44100u && !xf.d_live && !xf.r_live && !xf.up_live);
    {
        static int32_t z[CTL], s[CTL];
        uint32_t j;
        uint64_t idle, cd, cr, both;
        for (j = 0; j < CTL; j++) s[j] = noise(20000);
        idle = cost_of(z, z, 20000);
        if (idle) {
            fx_reset();
            cd = cost_of(s, z, 20000);
            fx_reset();
            cr = cost_of(z, s, 20000);
            fx_reset();
            both = cost_of(s, s, 20000);
            /* (the host's instructions: a guard against a loop growing, not the unit's time. The unit runs its own
             * ~205 instructions a microsecond and needs more of them than this host for the same C: docs/FX.md "The
             * cost on the unit" counts them in build/choralroot.dis, tools/fx_loops.py. emu.c's 259 a us read 1.4 to
             * 2.9 times low on the first builds' loops, which this host ran eight lines at a time) */
            printf("cr_fx:   COST, host instructions a 128-frame half (a guard; the unit's time is docs/FX.md's count of its "
                   "own): off %llu; the delay alone (%s rate) %llu; the reverb alone (%s rate, with its two rate changes) %llu; "
                   "both %llu\n", (unsigned long long)idle, DSD == 1 ? "full" : "half", (unsigned long long)cd,
                   DSR == 1 ? "full" : "half", (unsigned long long)cr, (unsigned long long)both);
            check("the cost: off <= 1300 host instructions a half; the delay <= 24000, the reverb <= 52000 (full rate: twice)",
                  idle <= 1300u && cd <= 24000u * (DSD == 1 ? 1u : 2u) && cr <= 52000u * (DSR == 1 ? 2u : 1u));
        } else {
            printf("cr_fx:   (no instruction counter on this host: the cost is tools/emu/perf.sh's)\n");
        }
    }
}

/* ------------------------------------------------------------ the master filter --- */
static int32_t *WI;                                     /* the master into the filter, a run */
static void fq_reset(void)
{
    memset(&xq, 0, sizeof xq);
    xf_defaults();
    fx_off = 0;
    g_f = 1000.0;
    g_a = 20000.0;
    g_len = 0xFFFFFFFFu;
}
/* n samples of gen (a multiple of CTL) through the master filter, the same on both sides: WI the input, WL / WR the
 * output; hook before every block */
static void fq_run(uint32_t n, gen_t gen, hook_t hook)
{
    static int32_t ml[CTL], mr[CTL];
    uint32_t i, k;
    for (i = 0; i < n && i + CTL <= MAXS; i += CTL) {
        if (hook)
            hook(i);
        for (k = 0; k < CTL; k++)
            ml[k] = mr[k] = WI[i + k] = gen ? gen(i + k) : 0;
        xq_block(ml, mr, CTL);
        memcpy(WL + i, ml, sizeof ml);
        memcpy(WR + i, mr, sizeof mr);
    }
}
static int fq_same(uint32_t a, uint32_t b)              /* WL == WI == WR over [a, b) */
{
    uint32_t i;
    for (i = a; i < b; i++)
        if (WL[i] != WI[i] || WR[i] != WI[i])
            return 0;
    return 1;
}
static int fq_rest(void)                                /* every state of the filter at 0 (a remainder may stay: xf_rs) */
{
    uint32_t c, i;
    for (c = 0; c < 2u; c++)
        for (i = 0; i < 2u; i++)
            if (xq.s[c][i].y[0] | xq.s[c][i].y[1])
                return 0;
    return 1;
}
/* the one-pole y += k (x - y), k Q12, at f: its gain (hp: of x minus it), squared */
static double fq_pole2(int32_t k12, double f, int hp)
{
    double k = k12 / 4096.0, w = 2.0 * M_PI * f / 44100.0, re = 1.0 - (1.0 - k) * cos(w), im = (1.0 - k) * sin(w);
    double d = re * re + im * im, hr = k * re / d, hi = -k * im / d;   /* H = k / (1 - (1 - k) e^-jw) */
    if (hp) {
        hr = 1.0 - hr;
        hi = -hi;
    }
    return hr * hr + hi * hi;
}
static uint32_t fq_sw_ms;
static int32_t fq_sw_v;
static void h_fq_sweep(uint32_t i)                      /* the high cut a detent every fq_sw_ms, 126 -> 0 -> 126 */
{
    uint32_t step = i / (441u * fq_sw_ms / 10u);
    xfp[XP_FHIGH] = (int16_t)(step < 126u ? 126u - step : step < 252u ? step - 126u : 126u);
}
static void h_fq_toggle(uint32_t i)                     /* Filter off / on every 300 ms, Slope every 700 ms */
{
    xfp[XP_FON] = (int16_t)((i / 13230u) & 1u);
    xfp[XP_FSLOPE] = (int16_t)((i / 30870u) & 1u);
}
static double fq_maxstep(const int32_t *x, uint32_t a, uint32_t b)
{
    double m = 0;
    uint32_t i;
    for (i = a + 1u; i < b; i++) {
        double d = fabs((double)x[i] - x[i - 1u]);
        if (d > m)
            m = d;
    }
    return m;
}
static void t_filter(void)
{
    static const uint8_t LO[3] = {20, 70, 120}, HI[3] = {10, 60, 110};
    uint32_t n = 44100u, i, c, sl, k;
    int ok;
    /* Off is bit-exact: the defaults; FX off with the cuts set; the Filter cell Off with the cuts set; On with both
     * cuts at their Off ends. The states stay at rest and the block costs almost nothing */
    for (k = 0, ok = 1; k < 4u; k++) {
        fq_reset();
        if (k == 1u) {
            xfp[XP_FON] = 1;
            xfp[XP_FLOW] = 60;
            xfp[XP_FHIGH] = 60;
            fx_off = 1;
        } else if (k == 2u) {
            xfp[XP_FLOW] = 60;
            xfp[XP_FHIGH] = 60;
        } else if (k == 3u) {
            xfp[XP_FON] = 1;
        }
        g_a = 120000.0;
        g_len = 0xFFFFFFFFu;
        fq_run(n, g_chord, 0);
        ok = ok && fq_same(0, n) && fq_rest();
    }
    check("filter: Off is bit for bit the mix (the defaults; FX off; Filter Off; On with both cuts Off), its states at rest", ok);
    /* in, then out again: after the fade the output is the mix again, bit for bit, the states cleared */
    fq_reset();
    xfp[XP_FON] = 1;
    xfp[XP_FLOW] = 60;
    xfp[XP_FHIGH] = 60;
    fq_run(n, g_chord, 0);
    ok = !fq_same(n / 2u, n);
    xfp[XP_FON] = 0;
    fq_run(n, g_chord, 0);
    check("filter: switched Off under a chord: within the fade's 16 blocks the mix comes back bit for bit, the states cleared",
          ok && !fq_same(0, CTL) && fq_same(17u * CTL, n) && fq_rest());
    /* each cut at three corners, both slopes: a sine at a quarter of, at, and at four times the corner: the gain is
     * the one-pole formula's (one stage, or two at 1.554 x the corner), within 0.3 dB; the same on both sides */
    for (c = 0, ok = 1; c < 2u; c++)
        for (sl = 0; sl < 2u; sl++)
            for (i = 0; i < 3u; i++) {
                uint32_t p = c ? HI[i] : LO[i], hz = c ? xq_high_hz(p) : xq_low_hz(p), hp = c == 0u;
                int32_t k12 = xq_k12(sl ? hz * 1554u / 1000u : hz);
                for (k = 0; k < 3u; k++) {
                    double f = hz * (k == 0u ? 0.25 : k == 1u ? 1.0 : 4.0), want, got, r;
                    if (f > 20000.0)
                        continue;
                    fq_reset();
                    xfp[XP_FON] = 1;
                    xfp[XP_FSLOPE] = (int16_t)sl;
                    xfp[c ? XP_FHIGH : XP_FLOW] = (int16_t)p;
                    g_f = f;
                    g_a = 20000.0;
                    fq_run(n, g_sine, 0);
                    want = fq_pole2(k12, f, (int)hp);
                    if (sl)
                        want *= want;
                    got = rms(WL, n / 2u, n) / rms(WI, n / 2u, n);
                    r = fabs(db(got) - 10.0 * log10(want));
                    if (r > 0.3 || memcmp(WL, WR, n * sizeof *WL)) {
                        printf("cr_fx:   filter: %s %u Hz %s dB at %.0f Hz: %.2f dB for %.2f\n", c ? "high cut" : "low cut", (unsigned)hz,
                               sl ? "12" : "6", f, db(got), 10.0 * log10(want));
                        ok = 0;
                    }
                }
            }
    check("filter: each cut at 3 corners x 2 slopes x 3 frequencies: the one-pole formula's gain within 0.3 dB, both sides the same", ok);
    /* the extremes: both cuts in at their far ends (2 kHz low cut into a 200 Hz high cut), a square at the clamp and
     * noise over it, both slopes: bounded by 4 x the clamp; then silence: exact zeros within 2 s, the states at rest */
    for (sl = 0, ok = 1; sl < 2u; sl++)
        for (k = 0; k < 2u; k++) {
            int32_t m = 0;
            fq_reset();
            xfp[XP_FON] = 1;
            xfp[XP_FSLOPE] = (int16_t)sl;
            xfp[XP_FLOW] = 127;
            xfp[XP_FHIGH] = 0;
            g_a = k ? 131068.0 : 98304.0;
            g_f = 55.0;
            g_len = 2u * n;
            fq_run(2u * n, k ? g_noise : g_loud, 0);
            for (i = 0; i < 2u * n; i++) {
                int32_t a = WL[i] < 0 ? -WL[i] : WL[i];
                if (a > m)
                    m = a;
            }
            fq_run(2u * n, 0, 0);
            for (i = n; i < 2u * n; i++)
                if (WL[i] | WR[i])
                    break;
            if (m > 4 * XQ_MAX || i < 2u * n || !fq_rest()) {
                printf("cr_fx:   filter: extremes (%s, %s): peak %d, silent from %u, rest %d\n", sl ? "12 dB" : "6 dB",
                       k ? "noise over the clamp" : "a square at it", (int)m, (unsigned)i, fq_rest());
                ok = 0;
            }
        }
    check("filter: both cuts at their far ends, a square at the clamp and noise over it: bounded by 4 x the clamp; silence: exact 0 within 1 s",
          ok);
    /* smooth: the high cut swept 126 -> 0 -> 126 a detent every 20 ms under a held 440 Hz tone, then Filter and
     * Slope toggled under it with the high cut at 500 Hz: no sample step above 1.3 x the tone's own */
    {
        double ref, sw, tg;
        fq_reset();
        xfp[XP_FON] = 1;
        g_f = 440.0;
        g_a = 20000.0;
        g_len = 0xFFFFFFFFu;
        fq_sw_ms = 20;
        fq_run(6u * n, g_sine, h_fq_sweep);
        ref = fq_maxstep(WI, 0, 6u * n);
        sw = fq_maxstep(WL, 0, 6u * n);
        fq_reset();
        xfp[XP_FON] = 1;
        xfp[XP_FHIGH] = 27;                             /* ~500 Hz */
        g_f = 440.0;
        fq_run(4u * n, g_sine, h_fq_toggle);
        tg = fq_maxstep(WL, 0, 4u * n);
        printf("cr_fx:   filter: the largest sample step: the tone's %.0f, under the sweep %.0f, under the toggles %.0f\n", ref, sw, tg);
        check("filter: the high cut swept 126 -> 0 -> 126 and Filter / Slope toggled under a 440 Hz tone: no step above 1.3 x the tone's",
              sw < 1.3 * ref && tg < 1.3 * ref);
    }
    /* the cost: host instructions a half (a guard: the unit's time is the listing's count, docs/FX.md) */
    {
        uint64_t off, one, both, t0;
        static int32_t ml[CTL], mr[CTL];
        fq_reset();
        g_a = 20000.0;
        fq_run(CTL, g_chord, 0);
        t0 = instr_now();
        for (i = 0; i < 2000u; i++)
            xq_block(ml, mr, CTL);
        off = (instr_now() - t0) / 2000u;
        fq_reset();
        xfp[XP_FON] = 1;
        xfp[XP_FHIGH] = 60;
        fq_run(20u * CTL, g_chord, 0);
        t0 = instr_now();
        for (i = 0; i < 2000u; i++)
            xq_block(ml, mr, CTL);
        one = (instr_now() - t0) / 2000u;
        xfp[XP_FLOW] = 60;
        fq_run(20u * CTL, g_chord, 0);
        t0 = instr_now();
        for (i = 0; i < 2000u; i++)
            xq_block(ml, mr, CTL);
        both = (instr_now() - t0) / 2000u;
        if (off) {
            printf("cr_fx:   filter COST, host instructions a half: off %llu, one 12 dB cut %llu, both %llu\n",
                   (unsigned long long)off, (unsigned long long)one, (unsigned long long)both);
            check("filter: the cost: off <= 200 host instructions a half, one 12 dB cut <= 6000, both <= 12000",
                  off <= 200u && one <= 6000u && both <= 12000u);
        }
    }
    fq_reset();
}

/* the reverb's impulse responses and the delay's repeats, for tools/fx_plots.py and the ear */
static void demos(void)
{
    static const struct { const char *file; int16_t size, decay, damp; uint32_t secs; } IR[] = {
        {"ir_reverb_small", 20, 32, 40, 4}, {"ir_reverb_default", 88, 52, 56, 8}, {"ir_reverb_large", 127, 80, 30, 20},
        {"ir_reverb_inf", 127, 127, 20, 30}};
    static const struct { const char *file; int16_t mode, sync, div, ratio, fb; } DL_[] = {
        {"ir_delay_stereo", 0, 1, 6, 0, 80}, {"ir_delay_pingpong", 1, 1, 6, 0, 80},
        {"ir_delay_pingpong_dotted", 1, 1, 8, 1, 80}};
    uint32_t k, n;
    char name[96];
    for (k = 0; k < 4u; k++) {
        fx_reset();
        xfp[XP_RSIZE] = IR[k].size;
        xfp[XP_RDECAY] = IR[k].decay;
        xfp[XP_RDAMP] = IR[k].damp;
        n = IR[k].secs * 44100u - (IR[k].secs * 44100u) % CTL;
        run(n, g_click, 2, 0);
        snprintf(name, sizeof name, "%s%s.wav", IR[k].file, sfx);
        wav_write(name, n, 4.0);
    }
    for (k = 0; k < 3u; k++) {
        fx_reset();
        xfp[XP_DMODE] = DL_[k].mode;
        xfp[XP_DSYNC] = DL_[k].sync;
        xfp[XP_DDIV] = DL_[k].div;
        xfp[XP_DRATIO] = DL_[k].ratio;
        xfp[XP_DFDBK] = DL_[k].fb;
        n = 6u * 44100u - (6u * 44100u) % CTL;
        run(n, g_burst, 1, 0);
        snprintf(name, sizeof name, "%s%s.wav", DL_[k].file, sfx);
        wav_write(name, n, 1.0);
    }
    /* the comparison's material (docs/FX.md "Half rate or full"): a bright stab (the chord, 12 harmonics a note) through
     * each effect alone at its defaults with more feedback / decay */
    fx_reset();
    xfp[XP_DFDBK] = 90;
    xfp[XP_DTONE] = 127;
    n = 8u * 44100u - (8u * 44100u) % CTL;
    run(n, g_chord_stab, 1, 0);
    snprintf(name, sizeof name, "cmp_delay%s.wav", sfx);
    wav_write(name, n, 1.0);
    fx_reset();
    xfp[XP_RDAMP] = 0;
    xfp[XP_RDECAY] = 64;
    run(n, g_chord_stab, 2, 0);
    snprintf(name, sizeof name, "cmp_reverb%s.wav", sfx);
    wav_write(name, n, 1.0);
}

/* ------------------------------------------------------------------ the chorus --- */
/* THE CHORUS, STEREO (0.19, cr_fx.c xc_block; docs/FX.md "The chorus, stereo"): the left tap is 0.18's own, the right
 * one differs, Width 0 is mono, the mono sum's comb is never deeper than 0.18's, Rate and Depth mean what they did,
 * nothing sent costs nothing and returns nothing. */
static void cho_reset(void)
{
    memset(cho_buf, 0, sizeof cho_buf);
    fx.cho_w = fx.cho_ph = 0;
    xc_live = 0;
    song.g[G_CRATE] = GP[G_CRATE].def;
    song.g[G_CDEPTH] = GP[G_CDEPTH].def;
    xfp[XP_CWIDTH] = XFP[XP_CWIDTH].def;
}
/* n samples of gen through the chorus into WL, WR; mono: Felucca's one tap (fx.c fx_buses), the same into both */
static void cho_run(uint32_t n, gen_t gen, int mono)
{
    static int32_t sc[CTL], z[CTL], ml[CTL], mr[CTL];
    uint32_t i, k;
    for (i = 0; i < n && i + CTL <= MAXS; i += CTL) {
        for (k = 0; k < CTL; k++) {
            sc[k] = gen ? gen(i + k) : 0;
            ml[k] = mr[k] = 0;
        }
        if (mono) {
            fx_buses(sc, z, z, ml, CTL);
            memcpy(mr, ml, sizeof mr);
        } else {
            xc_block(sc, ml, mr, CTL);
        }
        memcpy(WL + i, ml, sizeof ml);
        memcpy(WR + i, mr, sizeof mr);
    }
}
static int32_t g_imp2(uint32_t i) { return i == 64u ? 20000 : 0; }
static void t_chorus(void)
{
    static const double F[8] = {110.0, 220.0, 311.1, 440.0, 880.0, 1244.5, 1760.0, 3520.0};
    const uint32_t n = 4u * 44100u / CTL * CTL;
    const double g = 45 * 258 / 32768.0;                /* the default send's gain (mix_part_out) */
    int32_t *ol = malloc(n * sizeof *ol);
    double e = 0, p = 0, sl = 0, sr = 0, slr = 0, lo_s = 9, lo_m = 9, hi_s = 0, bound_ok = 1;
    uint32_t i, k, same = 1, ph_mono, ph_st, first[2] = {0, 0}, last[2] = {0, 0}, nz = 0, touched = 0, big = 0;
    /* 1. left is 0.18's tap (the LFO a line across the block for a sine a sample); right is another */
    g_a = 12000.0;
    g_len = 0xFFFFFFFFu;
    xs = 0x1234567u;
    cho_reset();
    cho_run(n, g_noise, 1);
    memcpy(ol, WL, n * sizeof *ol);
    ph_mono = fx.cho_ph;
    xs = 0x1234567u;
    cho_reset();
    cho_run(n, g_noise, 0);
    ph_st = fx.cho_ph;
    for (i = 4096; i < n; i++) {
        e += (double)(WL[i] - ol[i]) * (WL[i] - ol[i]);
        p += (double)ol[i] * ol[i];
        sl += (double)WL[i] * WL[i];
        sr += (double)WR[i] * WR[i];
        slr += (double)WL[i] * WR[i];
    }
    printf("cr_fx:   the chorus on noise, 4 s: the left tap against 0.18's one tap %.1f dB; left %.2f dB and right %.2f dB re "
           "0.18's; left against right: correlation %.3f\n", 10 * log10(e / p), 10 * log10(sl / p), 10 * log10(sr / p),
           slr / sqrt(sl * sr));
    check("chorus: the left tap is 0.18's (the LFO a straight line across a block: under -40 dB of it apart)",
          10 * log10(e / p) < -40.0);
    check("chorus: the right tap is as loud (0.2 dB) and another signal: left against right under 0.3 of correlation",
          fabs(10 * log10(sr / p)) < 0.2 && fabs(10 * log10(sl / p)) < 0.2 && fabs(slr / sqrt(sl * sr)) < 0.3);
    check("chorus: Rate is what it was: the LFO has turned exactly as far as 0.18's after 4 s", ph_mono == ph_st);
    /* 2. Width 0: one tap, both sides */
    xs = 0x1234567u;
    cho_reset();
    xfp[XP_CWIDTH] = 0;
    cho_run(n, g_noise, 0);
    for (i = 0; i < n; i++)
        if (WL[i] != WR[i])
            same = 0;
    check("chorus: Width 0 is mono: the same sample left and right", same);
    /* 3. the mono sum beside the dry sound: its comb is never deeper than 0.18's (1 - g), for held tones over two
     * turns of the LFO; the level in windows of 1024 samples */
    for (k = 0; k < 8u; k++) {
        uint32_t pass;
        g_f = F[k];
        g_a = 20000.0 * g;
        for (pass = 0; pass < 2u; pass++) {
            cho_reset();
            cho_run(n, g_sine, (int)pass);
            for (i = 8192; i + 1024u <= n; i += 512u) {
                double sd = 0, sm = 0, v;
                uint32_t j;
                for (j = i; j < i + 1024u; j++) {
                    double dry = 20000.0 * sin(2.0 * M_PI * g_f * j / 44100.0);
                    double sum = dry + (WL[j] + WR[j]) / 2.0;
                    sd += dry * dry;
                    sm += sum * sum;
                }
                v = sqrt(sm / sd);
                if (pass) {
                    if (v < lo_m) lo_m = v;
                } else {
                    if (v < lo_s) lo_s = v;
                    if (v > hi_s) hi_s = v;
                    if (v < 1.0 - g - 0.02 || v > 1.0 + g + 0.02) bound_ok = 0;
                }
            }
        }
    }
    printf("cr_fx:   dry + chorus at the default send (wet at %.1f dB), 8 tones, 4 s each: the mono sum's level between "
           "%.2f and %+.2f dB of the dry (0.18's one tap: down to %.2f dB; the bound 1 - g: %.2f dB)\n", 20 * log10(g),
           20 * log10(lo_s), 20 * log10(hi_s), 20 * log10(lo_m), 20 * log10(1.0 - g));
    check("chorus: the mono sum (L + R) / 2 beside the dry sound never falls under 0.18's deepest notch (1 - g) nor over 1 + g",
          bound_ok && lo_s > lo_m - 0.02);
    /* 4. Depth: an impulse comes back 400 samples later with Depth 0 (both sides, the sample itself), and inside
     * 400 .. 400 + 6 x 127 with Depth 127 */
    cho_reset();
    song.g[G_CDEPTH] = 0;
    cho_run(4096, g_imp2, 0);
    check("chorus: Depth 0: the send comes back 400 samples later, the sample itself, on both sides",
          WL[464] == 20000 && WR[464] == 20000 && WL[463] == 0 && WL[465] == 0 && WR[465] == 0);
    cho_reset();
    song.g[G_CDEPTH] = 127;
    fx.cho_ph = 0x30000000u;                            /* (somewhere on the LFO's way) */
    cho_run(4096, g_imp2, 0);
    for (i = 0; i < 4096u; i++) {
        if (WL[i]) { if (!first[0]) first[0] = i; last[0] = i; }
        if (WR[i]) { if (!first[1]) first[1] = i; last[1] = i; }
    }
    printf("cr_fx:   Depth 127: an impulse comes back %u .. %u samples later on the left, %u .. %u on the right\n",
           first[0] - 64u, last[0] - 64u, first[1] - 64u, last[1] - 64u);
    check("chorus: Depth 127: both taps inside 400 .. 1163 samples (9 .. 26 ms), not the same one",
          first[0] >= 464u && last[0] <= 64u + 1163u && first[1] >= 464u && last[1] <= 64u + 1163u && first[0] != first[1]);
    /* 5. nothing sent: nothing returned, nothing touched; a send, then silence: exact zeros once the line has run out */
    cho_reset();
    {
        static int32_t z[CTL], ml[CTL], mr[CTL];
        for (i = 0; i < CTL; i++)
            ml[i] = mr[i] = 7;
        xc_block(z, ml, mr, CTL);
        for (i = 0; i < CTL; i++)
            touched |= ml[i] != 7 || mr[i] != 7;
    }
    cho_reset();
    song.g[G_CDEPTH] = 127;
    cho_run(8192, g_imp2, 0);
    for (i = 64u + 1164u; i < 8192u; i++)
        nz |= WL[i] != 0 || WR[i] != 0;
    check("chorus: nothing sent, an empty line: the mix is not touched; after a send the line runs out to exact zeros and rests",
          !touched && !nz && xc_live == 0u && (WL[464 + 0] || first[0]));
    /* 6. the loudest send: the line saturates (16 bits), the return stays inside twice that */
    g_a = 2097148.0;
    xs = 0x7654321u;
    cho_reset();
    cho_run(44096, g_noise, 0);
    for (i = 0; i < 44096u; i++)
        if (WL[i] > 65534 || WL[i] < -65536 || WR[i] > 65534 || WR[i] < -65536)
            big = 1;
    check("chorus: the loudest send: the return is bounded by the saturated line (+-65536)", !big);
    g_a = 20000.0;
    g_f = 1000.0;
    cho_reset();
    free(ol);
}

/* ------------------------------------------------------- the chorus's glide --- */
/* THE CHORUS WHILE A KNOB TURNS (0.21, cr_fx.c xc_block; docs/FX.md "The chorus, stereo"; the owner: "This FX pops and
 * crackles when adjusting the depth"). xc_block_ref is the block of 0.19 and 0.20 (f9db021), kept here word for word:
 * it is what the new one must equal to the bit while nothing moves, and what these checks must FAIL on while
 * something does (each one says what the old block measures: the test would have caught it). */
static uint32_t ref_live;
#define XC_POS0(ph) ((400 << 8) + (((osc_sine(ph) + 32768) * cdepth) >> 8))
static void xc_block_ref(const int32_t *in, int32_t *ml, int32_t *mr, uint32_t n)
{
    int32_t cdepth = song.g[G_CDEPTH] * 6, any = 0, ra, rb, da, db;
    uint32_t i, w = fx.cho_w, ph0 = fx.cho_ph, ph1 = ph0 + (LFO_INC[song.g[G_CRATE] & 127] / CTL) * n;
    uint32_t off = (uint32_t)xfp[XP_CWIDTH] * (XC_TURN / 127u);
    fx.cho_ph = ph1;
    fx.cho_w = w + n;
    if (!ref_live) {
        for (i = 0; i < n; i++)
            any |= in[i];
        if (!any)
            return;
    }
    ra = XC_POS0(ph0) << 5;
    rb = XC_POS0(ph0 + off) << 5;
    da = XC_POS0(ph1) - (ra >> 5);
    db = XC_POS0(ph1 + off) - (rb >> 5);
    if (n != CTL) {
        da = da * (int32_t)CTL / (int32_t)n;
        db = db * (int32_t)CTL / (int32_t)n;
    }
    for (i = 0; i < n; i++, w++) {
        int32_t x = in[i], r, c0, c1;
        uint32_t k;
        any |= x;
        cho_buf[w & (CHO_LEN - 1u)] = (int16_t)clamp(x >> 1, -32768, 32767);
        ra += da;
        r = ra >> 5;
        k = w - ((uint32_t)r >> 8);
        c0 = cho_buf[k & (CHO_LEN - 1u)];
        c1 = cho_buf[(k - 1u) & (CHO_LEN - 1u)];
        ml[i] += (c0 + (((c1 - c0) * (r & 255)) >> 8)) * 2;
        rb += db;
        r = rb >> 5;
        k = w - ((uint32_t)r >> 8);
        c0 = cho_buf[k & (CHO_LEN - 1u)];
        c1 = cho_buf[(k - 1u) & (CHO_LEN - 1u)];
        mr[i] += (c0 + (((c1 - c0) * (r & 255)) >> 8)) * 2;
    }
    ref_live = any ? CHO_LEN : ref_live > n ? ref_live - n : 0u;
}
/* a run: sch(b) before block b (b 0: before anything, so the first block starts on what it sets); mixed: blocks of
 * 32, 8, 24, 16 samples in turn instead of CTL. Returns the samples run */
typedef void (*sch_t)(uint32_t b);
static int gl_mixed;
static uint32_t gl_run(uint32_t n, gen_t gen, int ref, sch_t sch)
{
    static const uint8_t NP[4] = {32, 8, 24, 16};
    static int32_t sc[CTL], ml[CTL], mr[CTL];
    uint32_t i = 0, k, b = 0, m;
    cho_reset();
    ref_live = 0;
    while (i + CTL <= n && i + CTL <= MAXS) {
        m = gl_mixed ? NP[b & 3u] : CTL;
        if (sch)
            sch(b);
        for (k = 0; k < m; k++) {
            sc[k] = gen ? gen(i + k) : 0;
            ml[k] = mr[k] = 0;
        }
        (ref ? xc_block_ref : xc_block)(sc, ml, mr, m);
        memcpy(WL + i, ml, m * sizeof *ml);
        memcpy(WR + i, mr, m * sizeof *mr);
        i += m;
        b++;
    }
    return i;
}
/* the schedules: gs_par 0 Depth, 1 Width, 2 Rate; the others as gs_fix has them */
#define GS_T 400u                                       /* the block of the first move */
static int gs_par, gs_fix[3] = {60, 127, 64};
static void gs_set(int par, int v)
{
    if (par == 0) song.g[G_CDEPTH] = v;
    else if (par == 1) xfp[XP_CWIDTH] = (int16_t)v;
    else song.g[G_CRATE] = v;
}
static void gs_init(int v)
{
    gs_set(0, gs_fix[0]);
    gs_set(1, gs_fix[1]);
    gs_set(2, gs_fix[2]);
    gs_set(gs_par, v);
}
static void s_static(uint32_t b) { if (!b) gs_init(gs_fix[gs_par]); }
static void s_step(uint32_t b)                          /* one detent, 60 -> 61, the LFO at its top (the left tap's) */
{
    if (!b) {
        gs_init(60);
        fx.cho_ph = 0x40000000u - GS_T * (LFO_INC[song.g[G_CRATE] & 127] / CTL) * CTL;
    }
    if (b == GS_T)
        gs_set(gs_par, 61);
}
static void s_jump(uint32_t b)                          /* the whole range at once */
{
    if (!b) gs_init(0);
    if (b == GS_T) gs_set(gs_par, 127);
}
static void s_sweep(uint32_t b)                         /* by hand: 0 .. 127 a step every 3 blocks (0.28 s), a rest, */
{                                                       /* back a step every 2 blocks */
    const uint32_t up = GS_T + 3u * 127u, dn = up + 300u;
    if (!b) gs_init(0);
    if (b >= GS_T && b <= up) gs_set(gs_par, (int)((b - GS_T) / 3u));
    if (b >= dn && b <= dn + 2u * 127u) gs_set(gs_par, 127 - (int)((b - dn) / 2u));
}
#define GS_SWEEP_N ((GS_T + 3u * 127u + 300u + 2u * 127u + 400u) * CTL)
/* THE READ POINT, FROM THE OUTPUT: the send is a ramp (32 a sample, 2040 samples long, again and again), so a tap's
 * output / 32 is the time it reads at and i - that its delay, to a 16th of a sample. Where the tap reads across the
 * ramp's fall the figure means nothing: a second run with the ramp half a period on has its fall elsewhere, and of the
 * two figures the one nearer the last sample's is taken (they agree everywhere else). Returns the largest change of
 * a tap's delay from one sample to the next, in samples (either tap; lo / hi: the range of the delay) */
#define SAW_P 2040u
static uint32_t g_so;
static int32_t g_saw(uint32_t i) { return 32 * (int32_t)((i + g_so) % SAW_P); }
static double saw_d(int32_t out, uint32_t i, uint32_t o)
{
    return fmod((double)(i + o) - out / 32.0 + 4.0 * SAW_P, (double)SAW_P);
}
static double pos_step(uint32_t n, int ref, sch_t sch, double *lo, double *hi)
{
    int32_t *a[2];
    double worst = 0;
    uint32_t i, c, m;
    a[0] = malloc(n * sizeof **a);
    a[1] = malloc(n * sizeof **a);
    g_so = 0;
    m = gl_run(n, g_saw, ref, sch);
    memcpy(a[0], WL, m * sizeof **a);
    memcpy(a[1], WR, m * sizeof **a);
    g_so = SAW_P / 2u;
    gl_run(n, g_saw, ref, sch);
    *lo = 1e9;
    *hi = 0;
    for (c = 0; c < 2u; c++) {
        const int32_t *b = c ? WR : WL;
        double prev = -1;
        for (i = 4096; i < m; i++) {
            double da = saw_d(a[c][i], i, 0), db2 = saw_d(b[i], i, SAW_P / 2u), d;
            if (prev < 0) {
                if (fabs(da - db2) < 0.2)
                    prev = da;
                continue;
            }
            d = fabs(da - db2) < 0.2 || fabs(da - prev) <= fabs(db2 - prev) ? da : db2;
            if (fabs(d - prev) > worst) worst = fabs(d - prev);
            if (d < *lo) *lo = d;
            if (d > *hi) *hi = d;
            prev = d;
        }
    }
    free(a[0]);
    free(a[1]);
    return worst;
}
/* the largest second difference of either side's output, samples a .. b */
static double dd2(uint32_t a, uint32_t b)
{
    double w = 0;
    uint32_t i;
    for (i = a + 2u; i < b; i++) {
        double l = fabs((double)WL[i] - 2.0 * WL[i - 1] + WL[i - 2]), r = fabs((double)WR[i] - 2.0 * WR[i - 1] + WR[i - 2]);
        if (l > w) w = l;
        if (r > w) w = r;
    }
    return w;
}
/* new against old, the same run: the first sample that differs (n: none) */
static uint32_t gl_same(uint32_t n, gen_t gen, sch_t sch, uint32_t seed, uint32_t from)
{
    int32_t *l = malloc(n * sizeof *l), *r = malloc(n * sizeof *r);
    uint32_t i, m;
    xs = seed;
    m = gl_run(n, gen, 1, sch);
    memcpy(l, WL, m * sizeof *l);
    memcpy(r, WR, m * sizeof *r);
    xs = seed;
    gl_run(n, gen, 0, sch);
    for (i = from; i < m; i++)
        if (l[i] != WL[i] || r[i] != WR[i])
            break;
    free(l);
    free(r);
    return i < m ? i : n;
}
static uint32_t gi_live;
static int32_t g_bursts(uint32_t i) { return i < 320u || (i >= 9600u && i < 9920u) ? noise(12000) : 0; }
static void s_idle(uint32_t b)                          /* a burst, rest, both knobs turned at rest, a burst */
{
    if (!b) { gs_set(0, 20); gs_set(1, 127); gs_set(2, 64); }
    if (b == 200u) {
        gi_live = xc_live | ref_live;
        gs_set(0, 110);
        gs_set(1, 30);
    }
}
static void t_chorus_glide(void)
{
    static const int SET[6][3] = {{60, 127, 64}, {0, 0, 0}, {127, 127, 127}, {127, 64, 30}, {1, 1, 100}, {60, 0, 64}};
    static const char *const PN[3] = {"Depth", "Width", "Rate"};
    static const sch_t SCH[3] = {s_step, s_sweep, s_jump};
    static const char *const SN[3] = {"60 -> 61", "0 .. 127 .. 0 by hand", "0 -> 127 at once"};
    const uint32_t n2 = 2u * 44100u / CTL * CTL;
    double st_pos[3], st_dd[3], lo, hi;
    uint32_t i, k, same = 1, mixed_same = 1, land = 1, rate_same = 1, idle_same;
    int pos_ok = 1, pos_old = 1, dd_ok = 1, dd_old = 1, rng_ok = 1;
    /* 1. at rest: the block of 0.20, to the bit (noise and a sine, six corners, the ISR's block and odd lengths) */
    for (k = 0; k < 6u; k++) {
        gs_fix[0] = SET[k][0]; gs_fix[1] = SET[k][1]; gs_fix[2] = SET[k][2];
        gs_par = 0;
        g_a = 12000.0;
        g_f = 110.0;
        gl_mixed = 0;
        same &= gl_same(n2, g_noise, s_static, 0x1234567u + k, 0) == n2 && gl_same(n2, g_sine, s_static, 1, 0) == n2;
        gl_mixed = 1;
        mixed_same &= gl_same(n2, g_noise, s_static, 0x7654321u + k, 0) == n2;
        gl_mixed = 0;
    }
    check("chorus glide: Depth, Width and Rate at rest: 0.20's block to the bit (noise and a sine, 2 s, six settings)", same);
    check("chorus glide: .. and with blocks of 32, 8, 24, 16 samples in turn (n != CTL)", mixed_same);
    /* 2. Rate never needed a glide: the old block and the new one, swept, are the same samples */
    gs_fix[0] = 127; gs_fix[1] = 127; gs_fix[2] = 64;
    gs_par = 2;
    for (k = 0; k < 3u; k++)
        rate_same &= gl_same(GS_SWEEP_N, g_noise, SCH[k], 77u + k, 0) == GS_SWEEP_N;
    check("chorus glide: Rate stepped, swept and thrown: the samples of 0.20's block (the phase runs on: nothing to glide)", rate_same);
    /* 3. the read point: no step, whatever turns. The steady figure first (the LFO's own slope at these settings) */
    for (k = 0; k < 3u; k++) {
        gs_par = (int)k;
        gs_fix[0] = k ? 127 : 60; gs_fix[1] = 127; gs_fix[2] = 64;
        if (k == 2u) gs_fix[2] = 127;                   /* (the fastest LFO at the deepest Depth: the steepest there is) */
        st_pos[k] = pos_step(n2, 0, s_static, &lo, &hi);
        g_a = 20000.0;
        g_f = 110.0;
        gl_run(n2, g_sine, 0, s_static);
        st_dd[k] = dd2(4096, n2);
        gs_fix[2] = 64;
    }
    printf("cr_fx:   the chorus at rest: a tap's delay moves at most %.3f samples a sample (Depth 60), %.3f (Depth 127), "
           "%.3f (Depth 127, Rate 127); a 110 Hz sine's second difference at most %.0f, %.0f, %.0f\n",
           st_pos[0], st_pos[1], st_pos[2], st_dd[0], st_dd[1], st_dd[2]);
    for (k = 0; k < 3u; k++) {                          /* the parameter */
        uint32_t s;
        gs_par = (int)k;
        gs_fix[0] = k ? 127 : 60; gs_fix[1] = 127; gs_fix[2] = 64;
        for (s = 0; s < 3u; s++) {                      /* the move */
            double po, pn, d_o, dn2, bound_p, bound_d;
            po = pos_step(GS_SWEEP_N, 1, SCH[s], &lo, &hi);
            pn = pos_step(GS_SWEEP_N, 0, SCH[s], &lo, &hi);
            rng_ok &= lo > 399.0 && hi < 1164.0;
            g_a = 20000.0;
            g_f = 110.0;
            gl_run(GS_SWEEP_N, g_sine, 1, SCH[s]);
            d_o = dd2(4096, GS_SWEEP_N);
            gl_run(GS_SWEEP_N, g_sine, 0, SCH[s]);
            dn2 = dd2(4096, GS_SWEEP_N);
            printf("cr_fx:   %s %s: a tap's delay steps at most %.3f samples a sample (0.20's block: %.3f); a 110 Hz "
                   "sine's second difference at most %.0f (0.20's: %.0f)\n", PN[k], SN[s], pn, po, dn2, d_o);
            /* the bounds. Depth, Width, a detent or a sweep by hand: the read point no faster than a quarter of a
             * sample a sample (the steady chorus at Depth 127: an eighth; the measure's grain is a 16th) and the
             * sine's curve no more than twice the steady one's. The whole range at once is a slide (the glide's
             * first block: 1/64 of 762 samples in 32 = 0.37 a sample): half a sample a sample, 150. Rate: no
             * faster than the steady chorus at Rate 127, and nothing 0.20's block did not do */
            bound_p = k == 2u ? st_pos[2] + 0.07 : s == 2u ? 0.5 : 0.25;
            bound_d = s == 2u ? 150.0 : 2.0 * st_dd[1] + 8.0;
            pos_ok &= pn <= bound_p;
            if (k < 2u) {                               /* (the old block: a step every time, of samples) */
                dd_ok &= dn2 <= bound_d;
                pos_old &= po > 3.0;
                dd_old &= d_o > 150.0;
            } else {                                    /* (Rate: the LFO's speed changes at once, as it did: a */
                dd_ok &= dn2 == d_o && pn == po;        /* corner in the read point's path, never a step) */
            }
        }
    }
    check("chorus glide: Depth, Width, Rate turned (a detent, a sweep by hand, the range at once): no step in a tap's read point", pos_ok);
    check("chorus glide: .. and a 110 Hz sine through it keeps its curve (second difference inside the stated bounds)", dd_ok);
    check("chorus glide: .. where 0.20's block stepped by samples and clicked, for Depth and for Width (the test sees it)", pos_old && dd_old);
    check("chorus glide: the taps stay inside 400 .. 1163 samples through every move", rng_ok);
    /* 4. the glide lands: 1400 blocks (1.0 s) after the whole range at once the samples are 0.20's again, to the bit */
    for (k = 0; k < 2u; k++) {
        const uint32_t from = (GS_T + 1400u) * CTL, n = from + 44100u;
        gs_par = (int)k;
        gs_fix[0] = 127; gs_fix[1] = 127; gs_fix[2] = 64;
        g_a = 12000.0;
        land &= gl_same(n, g_noise, s_jump, 5u + k, from) == n && gl_same(n, g_noise, s_step, 9u + k, from) == n;
    }
    check("chorus glide: the glide lands exactly: a second after a move the samples are 0.20's again, to the bit", land);
    /* 5. at rest nothing glides: both knobs turned while the line is empty, the next note starts in place */
    gi_live = 1;
    idle_same = gl_same(16384, g_bursts, s_idle, 0x2468ACEu, 0) == 16384u;
    check("chorus glide: Depth and Width turned while the line is empty: the next send starts in place (0.20's samples)",
          idle_same && !gi_live);
    gs_fix[0] = 60; gs_fix[1] = 127; gs_fix[2] = 64;
    g_a = 20000.0;
    g_f = 1000.0;
    cho_reset();
}

/* LISTENING FILES for 0.21's chorus (tools/chorus_glide_listen.sh: cr_fx_test --chorus-listen DIR): a held chord (g_chord)
 * beside its chorus at send 105 (the wet 1.6 dB under the dry: the chorus easy to hear), the knobs turned in detents
 * as the FX layer turns them (2 a detent), through 0.20's block ("before") and the new one ("after") */
#define LI_BPS (44100u / CTL)                           /* blocks a second */
static uint32_t li_n;
static int li_w = 127;
static int32_t li_dry(uint32_t i)
{
    const uint32_t off = li_n - 3u * 44100u / 2u;       /* the chord: in over 20 ms, out over 0.4 s, 1.1 s of rest */
    double e = i < 882u ? i / 882.0 : i < off ? 1.0 : i < off + 17640u ? 1.0 - (i - off) / 17640.0 : 0.0;
    return e > 0.0 ? (int32_t)(g_chord(i) * e) : 0;
}
static int32_t li_send(uint32_t i) { return li_dry(i) * (105 * 258) / 32768; }
static void li_det(uint32_t b, uint32_t at, uint32_t every, uint32_t count, int par, int d)   /* count detents of d from block at */
{
    if (b >= at && (b - at) % every == 0u && (b - at) / every < count) {
        int v = (par ? xfp[XP_CWIDTH] : song.g[G_CDEPTH]) + d;
        gs_set(par, v < 0 ? 0 : v > 127 ? 127 : v);
    }
}
static void li_depth(uint32_t b)                        /* 60 .. 100 slowly (a detent every 125 ms), 100 .. 0 and */
{                                                       /* 0 .. 127 as a quick turn (a detent every 20 ms) */
    if (!b) { gs_set(0, 60); gs_set(1, 127); gs_set(2, GP[G_CRATE].def); }
    li_det(b, 3u * LI_BPS / 2u, LI_BPS / 8u, 20, 0, 2);
    li_det(b, 5u * LI_BPS, LI_BPS / 50u, 50, 0, -2);
    li_det(b, 7u * LI_BPS, LI_BPS / 50u, 64, 0, 2);
}
static void li_width(uint32_t b)                        /* 127 .. 0 .. 127, a detent every 40 ms */
{
    if (!b) { gs_set(0, 60); gs_set(1, 127); gs_set(2, GP[G_CRATE].def); }
    li_det(b, 3u * LI_BPS / 2u, LI_BPS / 25u, 64, 1, -2);
    li_det(b, 5u * LI_BPS, LI_BPS / 25u, 64, 1, 2);
}
static void li_fixed(uint32_t b) { if (!b) { gs_set(0, 60); gs_set(1, li_w); gs_set(2, GP[G_CRATE].def); } }
static void li_render(const char *name, uint32_t secs, int ref, sch_t sch)
{
    uint32_t i, m;
    li_n = secs * 44100u / CTL * CTL;
    g_len = 0xFFFFFFFFu;
    m = gl_run(li_n, li_send, ref, sch);
    for (i = 0; i < m; i++) {
        int32_t d = li_dry(i);
        WL[i] += d;
        WR[i] += d;
    }
    wav_write(name, m, 1.0);
    printf("cr_fx: wrote %s/%s (%u s)\n", outdir, name, (unsigned)secs);
}
static void chorus_listen(void)
{
    li_render("depth-steps-before-0.20.wav", 10, 1, li_depth);
    li_render("depth-steps-after-0.21.wav", 10, 0, li_depth);
    li_render("width-sweep-127-0-127.wav", 9, 0, li_width);
    li_render("width-sweep-127-0-127-without-the-glide.wav", 9, 1, li_width);
    li_w = 0;
    li_render("width-000.wav", 6, 0, li_fixed);
    li_w = 64;
    li_render("width-064.wav", 6, 0, li_fixed);
    li_w = 127;
    li_render("width-127.wav", 6, 0, li_fixed);
    cho_reset();
}

/* ------------------------------------------------------------------ the routing --- */
/* THE ROUTING (cr_fx.c, docs/FX.md "The routing"; the second design). Parallel: the three effects and the filter as
 * fx.c's mix_block runs them in that mode (rt_par; absent: without the mode's one store), the sends given. Chain:
 * fx.c's mix_fx_chain itself, the dry mix given (both sides the same), the chord part's sends the amounts; each
 * effect's wet (cr_fx.c xg.w) hashed and its energy summed (rt_wh, rt_we) */
static uint64_t rt_hash, rt_wh[XFE_NFX];
static double rt_we[XFE_NFX];
static void rt_reset(void)
{
    fx_reset();
    cho_reset();
    memset(&xg, 0, sizeof xg);
    trk[CR_FX_CHORD].p[P_CHOR] = trk[CR_FX_CHORD].p[P_DLY] = trk[CR_FX_CHORD].p[P_REV] = 0;
}
static void rt_amt(int c, int d, int r)
{
    trk[CR_FX_CHORD].p[P_CHOR] = (int16_t)c;
    trk[CR_FX_CHORD].p[P_DLY] = (int16_t)d;
    trk[CR_FX_CHORD].p[P_REV] = (int16_t)r;
}
static void rt_par(int32_t *sc, int32_t *sd, int32_t *sr, int32_t *ml, int32_t *mr, int absent)
{
    if (!absent)
        xg.key = 0;
    xc_block(sc, ml, mr, CTL);
    xf_block(sd, sr, ml, mr, CTL);
    xq_block(ml, mr, CTL);
}
static uint64_t rt_fnv(uint64_t h, const int32_t *l, const int32_t *r)
{
    uint32_t k;
    for (k = 0; k < CTL; k++)
        h = (h ^ (uint32_t)l[k] ^ ((uint64_t)(uint32_t)r[k] << 32)) * 1099511628211ull;
    return h;
}
static void rt_run(uint32_t n, gen_t gen, int absent)   /* Parallel: the signal into the three sends */
{
    static int32_t sc[CTL], sd[CTL], sr[CTL], ml[CTL], mr[CTL];
    uint32_t i, k;
    rt_hash = 1469598103934665603ull;
    for (i = 0; i < n && i + CTL <= MAXS; i += CTL) {
        for (k = 0; k < CTL; k++) {
            sc[k] = sd[k] = sr[k] = gen(i + k);
            ml[k] = mr[k] = 0;
        }
        rt_par(sc, sd, sr, ml, mr, absent);
        rt_hash = rt_fnv(rt_hash, ml, mr);
        memcpy(WL + i, ml, sizeof ml);
        memcpy(WR + i, mr, sizeof mr);
    }
}
static void ch_block(const int32_t *d)                  /* Chain: a block of dry through fx.c's stage */
{
    uint32_t k;
    for (k = 0; k < CTL; k++) {
        mix_l[k] = mix_r[k] = d[k];
        send_c[k] = send_d[k] = send_r[k] = 0;
    }
    mix_fx_chain(CTL);
}
static void ch_run(uint32_t n, gen_t gen)
{
    static int32_t d[CTL];
    uint32_t i, k, e;
    rt_hash = 1469598103934665603ull;
    for (e = 0; e < XFE_NFX; e++) {
        rt_wh[e] = 1469598103934665603ull;
        rt_we[e] = 0;
    }
    xfp[XP_RMODE] = 1;
    for (i = 0; i < n && i + CTL <= MAXS; i += CTL) {
        for (k = 0; k < CTL; k++)
            d[k] = gen(i + k);
        ch_block(d);
        rt_hash = rt_fnv(rt_hash, mix_l, mix_r);
        for (e = 0; e < XFE_NFX; e++) {
            rt_wh[e] = rt_fnv(rt_wh[e], xg.w[e][0], xg.w[e][1]);
            for (k = 0; k < CTL; k++)
                rt_we[e] += (double)xg.w[e][0][k] * xg.w[e][0][k] + (double)xg.w[e][1][k] * xg.w[e][1][k];
        }
        memcpy(WL + i, mix_l, CTL * sizeof *WL);
        memcpy(WR + i, mix_r, CTL * sizeof *WR);
    }
}
static int32_t g_rsq(uint32_t i) { return i < 3u * 44100u ? ((i / 100u) & 1u ? 131068 : -131068) : 0; }
static int32_t g_rhi(uint32_t i) { return i < 44100u ? (int32_t)(30000.0 * sin(2.0 * M_PI * 9000.0 * i / 44100.0)) : 0; }
static uint32_t rt_ordof(uint32_t a, uint32_t b, uint32_t c, uint32_t d)   /* XF_ORD's index of a chain */
{
    uint32_t o;
    for (o = 0; o < 24u; o++)
        if (XF_ORD[o][0] == a && XF_ORD[o][1] == b && XF_ORD[o][2] == c && XF_ORD[o][3] == d)
            return o;
    return 0;
}
static void t_route(void)
{
    uint32_t o, n = 3u * 44100u;
    uint64_t ref;
    int ok;
    char line[240];
    g_len = 0xFFFFFFFFu;
    /* (a) Parallel at every order: the render with no routing, every sample */
    rt_reset();
    rt_run(n, g_chord, 1);
    ref = rt_hash;
    ok = 1;
    for (o = 0; o < 24u; o++) {
        rt_reset();
        xfp[XP_ORDER] = (int16_t)o;
        rt_run(n, g_chord, 0);
        ok = ok && rt_hash == ref;
    }
    check("routing: Parallel at each of the 24 orders renders the chord through the three and the filter as with no routing", ok);
    /* (b) Chain C D R F, chorus 100 delay 100 reverb 0: the delay's wet carries the chorus, the reverb's is 0 */
    {
        uint64_t h1;
        double e1;
        rt_reset();
        rt_amt(100, 100, 0);
        ch_run(n, g_chord);
        h1 = rt_wh[XFE_DELAY];
        e1 = rt_we[XFE_DELAY];
        ok = e1 > 0 && rt_we[XFE_REVERB] == 0 && rt_we[XFE_CHORUS] > 0;
        rt_reset();
        rt_amt(0, 100, 0);
        ch_run(n, g_chord);
        ok = ok && rt_wh[XFE_DELAY] != h1 && rt_we[XFE_REVERB] == 0;
        check("routing: Chain Chorus > Delay > Reverb > Filter, sends 100 / 100 / 0: the delay's wet carries the chorus (differs at "
              "chorus 0), the reverb's wet is exactly 0", ok);
    }
    /* (c) the filter's place: High cut at its lowest, a 9 kHz tone, Filter before Reverb against after */
    {
        double eb, ea;
        rt_reset();
        xfp[XP_FON] = 1;
        xfp[XP_FHIGH] = 0;
        xfp[XP_ORDER] = (int16_t)rt_ordof(XFE_CHORUS, XFE_DELAY, XFE_FILTER, XFE_REVERB);
        rt_amt(0, 0, 127);
        ch_run(n, g_rhi);
        eb = rt_we[XFE_REVERB];
        rt_reset();
        xfp[XP_FON] = 1;
        xfp[XP_FHIGH] = 0;
        xfp[XP_ORDER] = (int16_t)rt_ordof(XFE_CHORUS, XFE_DELAY, XFE_REVERB, XFE_FILTER);
        rt_amt(0, 0, 127);
        ch_run(n, g_rhi);
        ea = rt_we[XFE_REVERB];
        snprintf(line, sizeof line, "routing: Chain, High cut at 200 Hz, a 9 kHz tone: the reverb's wet with the filter before it %.1f dB "
                 "under the filter after it", 10.0 * log10((ea + 1e-9) / (eb + 1e-9)));
        check(line, ea > 0 && eb * 100.0 < ea);
    }
    /* (d) a loud square at full sends, Chain, a few orders: bounded, exact zeros and everything off after the tail */
    {
        static const uint32_t OS[5] = {0, 7, 13, 18, 23};
        double peak = 0;
        uint32_t j;
        ok = 1;
        for (j = 0; j < 5u; j++) {
            uint32_t i, z = 1, m = 40u * 44100u;
            rt_reset();
            xfp[XP_ORDER] = (int16_t)OS[j];
            xfp[XP_FON] = 1;
            xfp[XP_FLOW] = 40;
            rt_amt(127, 127, 127);
            ch_run(m, g_rsq);
            for (i = 0; i < m; i++) {
                double a = fabs((double)WL[i]) > fabs((double)WR[i]) ? fabs((double)WL[i]) : fabs((double)WR[i]);
                if (a > peak) peak = a;
            }
            for (i = m - 44100u; i < m; i++)
                z = z && !WL[i] && !WR[i];
            ok = ok && z && !xc_live && !xf.d_live && !xf.r_live && !xf.up_live && !xg.ud_live;
        }
        snprintf(line, sizeof line, "routing: a full-scale square, Chain at full sends, five orders: bounded (peak %.0f), exact zeros and "
                 "every effect off after the tail", peak);
        check(line, ok && peak < 2097152.0);
    }
    /* (e) FX off in Chain: the dry mix, every sample (the sends up, the filter on) */
    {
        uint32_t i;
        rt_reset();
        fx_off = 1;
        xfp[XP_FON] = 1;
        xfp[XP_FLOW] = 60;
        rt_amt(127, 127, 127);
        ch_run(n, g_chord);
        ok = 1;
        for (i = 0; i + CTL <= n; i++)
            ok = ok && WL[i] == g_chord(i) && WR[i] == g_chord(i);
        fx_off = 0;
        check("routing: FX off in Chain (the sends up, the filter on): the output is the dry mix, every sample", ok);
    }
    /* (f) the cost of a block: no routing, Parallel, Chain (full sends) */
    if (instr_now()) {
        static int32_t s[CTL], sc[CTL], sd[CTL], sr[CTL], ml[CTL], mr[CTL];
        uint64_t ca, cp, cc, t0;
        uint32_t j, rep = 20000u;
        for (j = 0; j < CTL; j++) s[j] = noise(20000);
        rt_reset();
        t0 = instr_now();
        for (j = 0; j < rep; j++) {
            memcpy(sc, s, sizeof sc); memcpy(sd, s, sizeof sd); memcpy(sr, s, sizeof sr);
            rt_par(sc, sd, sr, ml, mr, 1);
        }
        ca = (instr_now() - t0) * 4u / rep;
        rt_reset();
        t0 = instr_now();
        for (j = 0; j < rep; j++) {
            memcpy(sc, s, sizeof sc); memcpy(sd, s, sizeof sd); memcpy(sr, s, sizeof sr);
            rt_par(sc, sd, sr, ml, mr, 0);
        }
        cp = (instr_now() - t0) * 4u / rep;
        rt_reset();
        rt_amt(127, 127, 127);
        xfp[XP_RMODE] = 1;
        t0 = instr_now();
        for (j = 0; j < rep; j++)
            ch_block(s);
        cc = (instr_now() - t0) * 4u / rep;
        printf("cr_fx:   route COST, host instructions a 128-frame half, the three effects on: no routing %llu; Parallel %llu; "
               "Chain %llu (+%lld)\n", (unsigned long long)ca, (unsigned long long)cp, (unsigned long long)cc,
               (long long)cc - (long long)ca);
        check("routing: the cost: Parallel within 2 % of no routing; Chain (its own up-sampler for the delay) within 25 % of it", cp * 100u <= ca * 102u && cc * 100u <= ca * 125u);
    }
    rt_reset();
}

int main(int argc, char **argv)
{
    if (argc > 1)
        outdir = argv[1];
    if (argc > 2)
        sfx = argv[2];
    WL = calloc(MAXS, sizeof *WL);
    WR = calloc(MAXS, sizeof *WR);
    WI = calloc(MAXS, sizeof *WI);
    if (!WL || !WR || !WI)
        return 2;
    if (argc > 2 && !strcmp(argv[1], "--chorus-listen")) {   /* (tools/chorus_glide_listen.sh: the files, no tests) */
        outdir = argv[2];
        chorus_listen();
        return 0;
    }
    printf("cr_fx: the delay at %s rate, the reverb at %s rate\n", DSD == 1 ? "full" : "half", DSR == 1 ? "full" : "half");
    t_chorus();
    t_chorus_glide();
    t_maths();
    t_sync();
    t_band();
    t_dropout();
    t_noise();
    t_level();
    t_mono();
    t_overdrive();
    t_smooth();
    t_silence();
    t_bound();
    t_corners();
    t_filter();
    t_route();
    demos();
    printf(bad ? "cr_fx_test: %d FAILED\n" : "cr_fx_test: all ok\n", bad);
    return bad != 0;
}
