/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The PHYS engine's fixed-point models (src/phys_dsp.c) as plain C calls for tests/phys_ref.cpp, which
 * compares them with the float originals of DaisySP. Built by run_tests.sh with the firmware sources
 * (through hostsim.c, as regress.c). Parameters 0..1 as DaisySP takes them; the output as float. */
#define main hostsim_main
#include "hostsim.c"
#undef main

/* model 0 modal, 1 string; one strike / pluck at sample 0 (bow > 0: sustained by Dust instead), n samples.
 * exc (string, may be 0): the exciter's output to play the string with instead of its own burst (the float
 * original's, so that the two strings get the same input) */
void phys_fixed_render(int model, double hz, double structure, double brightness, double damping, double accent,
                       double bow, const float *exc, float *out, int n)
{
    static phys_slot_t S;
    int32_t y[CTL], ax[CTL];
    uint32_t f0 = (uint32_t)(hz / FS * 4294967296.0), i, k;
    int32_t st = (int32_t)(structure * 65536), br = (int32_t)(brightness * 65536), dm = (int32_t)(damping * 65536);
    int32_t ac = (int32_t)(accent * 65536), bw = (int32_t)(bow * 65536);
    phys_reset(&S, model ? PM_STRING : PM_MODAL, 0x13579BDFu);
    if (model)
        S.u.s.trig = 1;
    else
        S.u.m.trig = 1;
    for (i = 0; i < (uint32_t)n; i += CTL) {
        uint32_t m = (uint32_t)n - i < CTL ? (uint32_t)n - i : CTL;
        if (model) {
            px_string_blk_t B;
            px_string_block(&B, &S.u.s, f0, st, br, dm, ac, bw, 0);
            px_string_excite(&B, &S.u.s, ax, m);
            if (exc)
                for (k = 0; k < m; k++)
                    ax[k] = (int32_t)lrint(exc[i + k] * (1 << 20));
            px_string_run(&B, &S.u.s, ax, y, m);
        } else {
            px_modal_blk_t B;
            px_modal_block(&B, &S.u.m, f0, st, br, dm, ac, 0, bw);
            px_modal_run(&B, &S.u.m, y, ax, m);
        }
        for (k = 0; k < m; k++)
            out[i + k] = (float)y[k] / (1 << 20);
    }
}

/* the double-sampled Svf: n samples of in through it (low-pass out, or band-pass with band); f cycles, res and
 * pre_drive 0..1 as Svf::SetRes / the drive before SetRes */
void phys_fixed_svf(double f, double res, double pre_drive, int band, const float *in, float *out, int n)
{
    px_csvf_t c;
    px_csvf_st_t s = {0, 0};
    int i;
    px_csvf_coef(&c, (uint32_t)(f * 4294967296.0), (int32_t)(res * 65536), (int32_t)(pre_drive * 65536));
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)lrint(in[i] * (1 << 20)), a, b;
        px_csvf_pass(&c, &s, x);
        a = band ? s.band : s.low;
        px_csvf_pass(&c, &s, x);
        b = band ? s.band : s.low;
        out[i] = (float)((a + b) >> 1) / (1 << 20);
    }
}
