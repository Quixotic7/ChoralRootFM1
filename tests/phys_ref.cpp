/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* PHYS reference test: the fixed-point models of src/phys_dsp.c against the float originals of DaisySP
 * (DaisySP, MIT; not part of this tree: run_tests.sh skips the test without it).
 *   c++ -O2 -I $DAISYSP/Source ... phys_ref.cpp <DaisySP .cpp> phys_fixed.o -o phys_ref
 *   phys_ref [WAVDIR]        WAVDIR: also write each case, float | fixed as a stereo WAV. VERBOSE=1: every case.
 * Cases: ModalVoice (struck) and StringVoice (plucked) over notes C1..C7 and the corners of STRUCTURE,
 * BRIGHTNESS and DAMPING, rendered for a few seconds from one strike / pluck. The fixed string is played with
 * the float original's own exciter output (StringVoice::GetAux): the burst is random, so this compares the
 * strings themselves; the exciter's filter (Svf) is compared on its own, on the same noise.
 *   MODAL   the three strongest spectral peaks of the float render are found in the fixed one (Hann window,
 *           parabolic interpolation): within PEAK_CT cents
 *   STRING  the fundamental (the strongest float peak within 3 semitones of the note, the nearest fixed one):
 *           within PEAK_CT cents
 *   both    the decay: the energy of 0.05..0.35 s over that of the last 0.6..0.3 s, in dB: within DECAY_TOL
 *           of it (+ 1 dB); the level (RMS of the first quarter, printed); no sample beyond +-64 (a blow-up), no
 *           DC above 1 % of the peak in the fixed render
 *   Svf     low-pass (the string exciter) on white noise: RMS error below -40 dB (the band-pass case went with DUST)
 * With dispersion noise (STRUCTURE above 0.82) the float model differs from itself run to run: the decay
 * tolerance is at least 1.5 x the spread of six float renders. Content below 30 Hz is left out of the
 * peaks (the dispersive strings have a sub-audio loop resonance near 24 Hz, in both).
 * Informational only (printed, never a failure), where the port departs on purpose: MODAL with STRUCTURE
 * below 0.25 (the partials crowd together: DaisySP's modes 17..24, which the port leaves out, fall low and
 * ring), STRING with STRUCTURE below 0.24 (the curved bridge: DaisySP clamps the non-linearity to 0..1 and so
 * never reaches it; the port does, as DaisySP's header describes it) and STRING below 88 Hz (the port's
 * 512-sample line runs resampled there, DaisySP's 1024-sample one only below 43 Hz: the loop filter and the
 * dispersion all-pass act per string step). */
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "PhysicalModeling/modalvoice.h"
#include "PhysicalModeling/stringvoice.h"
#include "Filters/svf.h"

extern "C" void phys_fixed_render(int model, double hz, double structure, double brightness, double damping,
                                  double accent, double bow, const float *exc, float *out, int n);
extern "C" void phys_fixed_svf(double f, double res, double pre_drive, int band, const float *in, float *out, int n);

static const double FS = 44100.0;
#define PEAK_CT 5.0
#define DECAY_TOL 0.10

using cd = std::complex<double>;
static void fft(std::vector<cd> &a)
{
    size_t n = a.size(), j = 0;
    for (size_t i = 1; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / (double)len;
        cd wl(cos(ang), sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1);
            for (size_t k = 0; k < len / 2; k++) {
                cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct peak { double f, db; };

/* spectral peaks (dB, refined) of x[start .. start + N) above max - floor_db, strongest first */
static std::vector<peak> peaks(const std::vector<float> &x, size_t start, size_t N, double floor_db)
{
    std::vector<cd> a(N);
    std::vector<double> m(N / 2);
    std::vector<peak> out;
    double mx = -1e9;
    for (size_t i = 0; i < N; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * (double)i / (double)N);
        a[i] = start + i < x.size() ? x[start + i] * w : 0.0;
    }
    fft(a);
    size_t lo = (size_t)(30.0 * (double)N / FS) + 2;   /* from 30 Hz: sub-audio content does not count */
    for (size_t i = 0; i < N / 2; i++) {
        m[i] = 20 * log10(std::abs(a[i]) + 1e-12);
        if (i >= lo)
            mx = std::max(mx, m[i]);
    }
    for (size_t i = lo; i + 2 < N / 2; i++)
        if (m[i] > m[i - 1] && m[i] >= m[i + 1] && m[i] > mx - floor_db) {
            double d = 0.5 * (m[i - 1] - m[i + 1]) / (m[i - 1] - 2 * m[i] + m[i + 1]);
            out.push_back({((double)i + d) * FS / (double)N, m[i] - 0.25 * (m[i - 1] - m[i + 1]) * d});
        }
    std::sort(out.begin(), out.end(), [](const peak &p, const peak &q) { return p.db > q.db; });
    return out;
}

/* decay: the energy of 0.05..0.35 s over that of [end - 0.6 s, end - 0.3 s), dB. The late energy counts at least
 * as an RMS of FLOOR: the fixed render settles at ~1e-5 of full scale (the rounding of the lowest modes, below one
 * LSB of the 16-bit output), the float one goes on down */
#define FLOOR 3e-5
static double decay(const std::vector<float> &x)
{
    double a = 0, b = 0;
    size_t n = x.size(), i, w = (size_t)(0.3 * FS);
    for (i = (size_t)(0.05 * FS); i < (size_t)(0.35 * FS); i++)
        a += (double)x[i] * x[i];
    for (i = n - (size_t)(0.6 * FS); i < n - (size_t)(0.3 * FS); i++)
        b += (double)x[i] * x[i];
    return 10 * log10((a + 1e-20) / std::max(b, FLOOR * FLOOR * (double)w));
}

static double rms(const std::vector<float> &x, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b && i < x.size(); i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)(b - a));
}

static void wav(const char *dir, const char *name, const std::vector<float> &l, const std::vector<float> &r)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s.wav", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f)
        return;
    uint32_t n = (uint32_t)l.size(), b = n * 4u, v = 36 + b, h[4] = {16, 0x00020001u, 44100u, 44100u * 4u},
             h2 = 0x00100004u;
    fwrite("RIFF", 1, 4, f);
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(h, 4, 4, f);
    fwrite(&h2, 4, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&b, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) {
        int16_t s[2] = {(int16_t)std::max(-32767.f, std::min(32767.f, l[i] * 8000.f)),
                        (int16_t)std::max(-32767.f, std::min(32767.f, r[i] * 8000.f))};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

static int svf_cases(int *cases)
{
    static const double F[] = {0.002, 0.01, 0.05, 0.2, 0.33};
    std::vector<float> in(44100), sc(44100), a(44100), b(44100);
    uint32_t r = 1;
    double worst = -300;
    int fails = 0;
    for (auto &x : in) {
        r ^= r << 13, r ^= r >> 17, r ^= r << 5;
        x = (float)((double)r / 2147483648.0 - 1.0);
    }
    for (int mode = 0; mode < 2; mode++)        /* string exciter: res 0.5 / 1, drive 0.5 (mode 2, band: DUST's, gone) */
        for (double f : F) {
            double res = mode == 0 ? 0.5 : mode == 1 ? 1.0 : 0.9, pd = mode == 2 ? 0.07 : 0.5, e = 0, s = 0;
            float g = mode == 2 ? 0.3f : 1.0f;
            daisysp::Svf v;
            v.Init((float)FS);
            if (mode == 2)
                v.SetDrive(0.7f);
            v.SetFreq((float)(f * FS));
            v.SetRes((float)res);
            for (size_t i = 0; i < in.size(); i++) {
                sc[i] = in[i] * g;
                v.Process(sc[i]);
                a[i] = mode == 2 ? v.Band() : v.Low();
            }
            phys_fixed_svf(f, res, pd, mode == 2, sc.data(), b.data(), (int)in.size());
            for (size_t i = 0; i < in.size(); i++) {
                e += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
                s += (double)a[i] * a[i];
            }
            double db = 10 * log10(e / (s + 1e-20) + 1e-30);
            worst = std::max(worst, db);
            (*cases)++;
            if (db > -40 || getenv("VERBOSE"))
                printf("phys_ref: svf %s f %.3f res %.1f: error %.1f dB %s\n", mode == 2 ? "band" : "low ", f, res, db,
                       db > -40 ? "FAIL" : "ok");
            fails += db > -40;
        }
    printf("phys_ref: Svf (the string exciter) on noise: worst error %.1f dB of the output (limit -40)\n", worst);
    return fails;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    static const int NOTES[] = {24, 36, 48, 60, 72, 96};
    static const double ST_M[] = {0.0, 0.27, 0.5, 0.95}, ST_S[] = {0.1, 0.25, 0.6, 0.9};
    static const double BR[] = {0.3, 0.8}, DM[] = {0.3, 0.7, 0.97};
    int fails = 0, cases = 0, info = 0;
    double worst_ct = 0, worst_dec = 0, lvl_lo = 1e9, lvl_hi = -1e9;
    for (int model = 0; model < 2; model++)
        for (int note : NOTES)
            for (double st : model ? ST_S : ST_M)
                for (double br : BR)
                    for (double dm : DM) {
                        double hz = 440.0 * pow(2.0, (note - 69) / 12.0), acc = 0.5;
                        int n = (int)(FS * (dm > 0.9 ? 4.0 : 2.0));
                        std::vector<float> a(n), b(n), ex(n), a2;
                        char why[256] = "", name[96];
                        int strict = model ? st >= 0.24 && hz >= 88 : st >= 0.25;
                        double noise_tol = 0;
                        if (model) {
                            daisysp::StringVoice v;
                            v.Init((float)FS);
                            v.SetFreq((float)hz);
                            v.SetStructure((float)st);
                            v.SetBrightness((float)br);
                            v.SetDamping((float)dm);
                            v.SetAccent((float)acc);
                            v.Trig();
                            for (int i = 0; i < n; i++) {
                                a[i] = v.Process(false);
                                ex[i] = v.GetAux();
                            }
                            if (st > 0.82) {   /* dispersion noise (random): the float model against itself, */
                                double lo = decay(a), hi = lo;   /* 5 more renders: the spread of its decay */
                                a2.resize(n);
                                for (int r = 0; r < 5; r++) {
                                    daisysp::StringVoice w;
                                    w.Init((float)FS);
                                    w.SetFreq((float)hz);
                                    w.SetStructure((float)st);
                                    w.SetBrightness((float)br);
                                    w.SetDamping((float)dm);
                                    w.SetAccent((float)acc);
                                    w.Trig();
                                    for (int i = 0; i < n; i++)
                                        a2[i] = w.Process(false);
                                    lo = std::min(lo, decay(a2));
                                    hi = std::max(hi, decay(a2));
                                }
                                noise_tol = 1.5 * (hi - lo);
                            }
                        } else {
                            daisysp::ModalVoice v;
                            v.Init((float)FS);
                            v.SetFreq((float)hz);
                            v.SetStructure((float)st);
                            v.SetBrightness((float)br);
                            v.SetDamping((float)dm);
                            v.SetAccent((float)acc);
                            v.Trig();
                            for (int i = 0; i < n; i++)
                                a[i] = v.Process(false);
                        }
                        phys_fixed_render(model, hz, st, br, dm, acc, 0, model ? ex.data() : 0, b.data(), n);
                        cases++;
                        double peak_b = 0, dc = 0, ct = 0, da, db;
                        for (int i = 0; i < n; i++) {
                            peak_b = std::max(peak_b, (double)fabs(b[i]));
                            dc += b[i];
                        }
                        dc /= n;
                        if (peak_b > 64)
                            snprintf(why + strlen(why), sizeof why - strlen(why), " blow-up (peak %.1f);", peak_b);
                        if (fabs(dc) > 0.01 * peak_b + 1e-4)
                            snprintf(why + strlen(why), sizeof why - strlen(why), " DC %.4f;", dc);
                        size_t N = note < 40 ? 131072 : 65536;
                        auto pa = peaks(a, 441, N, model ? 50 : 40), pb = peaks(b, 441, N, 60);
                        if (model) {   /* the fundamental */
                            double fa = 0, ma = -1e9, best = 1e9;
                            for (auto &p : pa)
                                if (fabs(1200 * log2(p.f / hz)) < 300 && p.db > ma)
                                    ma = p.db, fa = p.f;
                            for (auto &p : pb)
                                if (fa > 0 && fabs(1200 * log2(p.f / fa)) < fabs(best))
                                    best = 1200 * log2(p.f / fa);
                            if (fa > 0 && fabs(best) < 1e8)
                                ct = best;
                            else
                                snprintf(why + strlen(why), sizeof why - strlen(why), " no fundamental;");
                        } else {       /* the three strongest float peaks, each found in the fixed render */
                            for (size_t k = 0; k < pa.size() && k < 3; k++) {
                                double best = 1e9;
                                if (pa[k].f > 0.45 * FS)
                                    continue;
                                for (auto &p : pb)
                                    if (fabs(1200 * log2(p.f / pa[k].f)) < fabs(best))
                                        best = 1200 * log2(p.f / pa[k].f);
                                if (fabs(best) > fabs(ct))
                                    ct = best;
                            }
                        }
                        if (fabs(ct) > PEAK_CT)
                            snprintf(why + strlen(why), sizeof why - strlen(why), " peak off by %.1f ct;", ct);
                        da = decay(a);
                        db = decay(b);
                        if (fabs(da - db) > std::max(DECAY_TOL * std::max(fabs(da), fabs(db)) + 1.0, noise_tol))
                            snprintf(why + strlen(why), sizeof why - strlen(why), " decay %.1f vs %.1f dB;", db, da);
                        double la = rms(a, 0, (size_t)n / 4), lb = rms(b, 0, (size_t)n / 4), lr = 20 * log10(lb / la);
                        if (strict) {
                            worst_ct = std::max(worst_ct, fabs(ct));
                            if (fabs(da) > 3 && !noise_tol)
                                worst_dec = std::max(worst_dec, fabs(db / da - 1));
                            lvl_lo = std::min(lvl_lo, lr);
                            lvl_hi = std::max(lvl_hi, lr);
                        }
                        snprintf(name, sizeof name, "%s_n%d_s%02d_b%02d_d%02d", model ? "string" : "modal", note,
                                 (int)(st * 100), (int)(br * 100), (int)(dm * 100));
                        if (why[0] || getenv("VERBOSE"))
                            printf("phys_ref: %-30s peak %+6.2f ct  decay %5.1f / %5.1f dB  level %+5.1f dB  %s%s\n", name,
                                   ct, db, da, lr, why[0] ? (strict ? "FAIL" : "(info)") : "ok", why);
                        fails += why[0] && strict;
                        info += !strict;
                        if (dir)
                            wav(dir, name, a, b);
                    }
    fails += svf_cases(&cases);
    printf("phys_ref: %d cases (DaisySP float vs fixed point, %d informational): worst peak %.2f ct (limit %.0f), "
           "worst decay %.1f %% (limit %.0f %% + 1 dB; without the dispersion noise), level %+.1f .. %+.1f dB; %d failed\n", cases, info, worst_ct,
           PEAK_CT, worst_dec * 100, DECAY_TOL * 100, lvl_lo, lvl_hi, fails);
    return fails ? 1 : 0;
}
