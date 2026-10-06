/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SPEAKER BASS+ (fx.c master_out): a 55 Hz tone gains energy at 250 Hz..1 kHz, the sub is cut, the output
 * settles back to 0 after the tone and nothing reaches full scale. */
#include <math.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static double goertzel_db(const int32_t *x, uint32_t n, double f)
{
    double w = 2.0 * cos(2.0 * M_PI * f / FS), s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double s0 = x[i] + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 10.0 * log10(s1 * s1 + s2 * s2 - w * s1 * s2 + 1.0);
}

static int32_t buf[3][FS];

int main(void)
{
    uint32_t mode, i, fails = 0;
    double h[3], sub[3];
    int32_t peak = 0, tail = 0;
    for (mode = 0; mode < 3u; mode++) {
        fx_lowcut = (uint8_t)mode;
        lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;
        for (i = 0; i < FS; i++) {
            int32_t l = i < FS / 2u ? (int32_t)(12000.0 * sin(2.0 * M_PI * 55.0 * i / FS)) : 0, r = l;
            master_out(&l, &r);
            buf[mode][i] = l;
            if (mode == 2u) {
                peak = l < 0 ? (-l > peak ? -l : peak) : (l > peak ? l : peak);
                if (i > FS - 2000u)
                    tail = abs(l) > tail ? abs(l) : tail;
            }
        }
        sub[mode] = goertzel_db(buf[mode] + 4000, FS / 2u - 4000u, 55.0);
        h[mode] = 0;
        for (i = 5; i <= 17u; i += 2)               /* the odd harmonics 275 .. 935 Hz */
            h[mode] += pow(10.0, goertzel_db(buf[mode] + 4000, FS / 2u - 4000u, 55.0 * i) / 10.0);
        h[mode] = 10.0 * log10(h[mode]);
    }
    printf("speaker: 55 Hz tone, 275..935 Hz harmonics OFF %.1f LOWCUT %.1f BASS+ %.1f dB; 55 Hz OFF %.1f BASS+ %.1f dB; "
           "peak %d, tail %d\n", h[0], h[1], h[2], sub[0], sub[2], peak, tail);
    fails += h[2] < h[0] + 20.0;                    /* the harmonics are there */
    fails += sub[2] > sub[0] - 6.0;                 /* the sub is cut */
    fails += peak >= 32767 || tail > 4;
    printf("speaker: BASS+ %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
