/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot In at 48 kHz (0.19; docs/USB-AUDIO.md "48 kHz"): firmware/src/usb.c with FELUCCA_UAC=1 as
 * tests/cr_usbaudio_test.c builds it (which keeps the 44.1 kHz stream: its "golden" digest says that rate is bit for
 * bit what it was). The SIE is never touched. After SLOOP 2.5's tests/uac_test.c (its sine fit and its three 48 kHz
 * tests), for six channels and with the spectrum.
 *   descriptors  the format's two discrete rates, the endpoint's size against a 49-frame packet bit-stuffed, the
 *                sampling-frequency control; the streaming descriptors against SLOOP 2.5's (the source: the same
 *                bytes but the channel count, the terminal, the endpoint's number and size); bcdDevice 3.23
 *   requests     GET_CUR / MIN / MAX / RES, SET_CUR (the nearer rate; the same rate again leaves the stream alone; a
 *                change empties the ring, drops the packet packed ahead, restarts the open's count), a bus reset
 *   exact        160 frames out for every 147 in, 34 or 35 for a block of 32, whatever the blocks' sizes; the C code
 *                against a plain reference of the same table, bit for bit; DC exact; full scale saturates, no wrap;
 *                six channels independent
 *   quality      tones 100 Hz .. 20 kHz: gain, SNR, and the spectrum's largest product that is not the tone (the
 *                image at f + 3900 Hz); the table goes into docs/USB-AUDIO.md
 *   stream       the I2S fast, slow and nominal against the host's 1 kHz frames at 48 kHz: packets of 47, 48 or 49
 *                frames, 48.0 a ms on the host's clock, no underrun or overrun, what the host records one unbroken
 *                sine across the packets, all six channels
 *   switch       44.1 -> 48 -> 44.1 while streaming: silence, then the new rate, every frame in order again; a block
 *                resampled for the rate the host just left never enters the ring; not fed while the host reads not yet
 *   cc -std=gnu11 -O2 -o build/host/cr_usbaudio48_test tests/cr_usbaudio48_test.c -lm   (tests/run_cr_tests.sh)
 *   --table      the quality figures as a Markdown table */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 1
#define FELUCCA_UAC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb.c"

static int fails, passes, table_only;
static void check(const char *what, int ok)
{
    if (table_only)
        return;
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (ok)
        passes++;
    else
        fails++;
}

#define CH UA_CAP_CHANNELS

/* ------------------------------------------------------------------ descriptors --- */
/* SLOOP 2.5's streaming interface (its firmware/src/usb.c with FELUCCA_UAC_48K: alternate 1's class descriptors and
 * the endpoint; Felucca 1.1.5's form): stereo on EP4, 49 x 4 bytes */
static const uint8_t SLOOP_AS[] = {
    7, 0x24, 1, 2, 1, 0x01, 0x00,                                      /* AS general: terminal 2, delay 1, PCM */
    14, 0x24, 2, 1, 2, 2, 16, 2, 0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00,   /* type I: 2 ch, 16 bit, 44100 / 48000 */
    9, 5, 0x84, 0x05, 196, 0, 1, 0, 0,                                 /* EP4 IN isochronous async, 1 ms */
    7, 0x25, 1, 0x01, 0, 0, 0,                                         /* CS endpoint: sampling frequency control */
};

static void test_descriptors(void)
{
    const uint8_t *dev, *c, *as = 0;
    uint16_t dl, n;
    uint32_t o, i, diff = 0, cur_if = 0, cur_alt = 0, mp;
    char m[200];
    ua_off = 0;
    usb_cdc_on = 0;
    ua_reset();
    ua_cfg_build();
    get_desc(0x0100, &dev, &dl);
    get_desc(0x0200, &c, &n);
    check("bcdDevice 3.23: a host that cached 3.22's one-rate descriptors reads them again", dev[12] == 0x23 &&
          dev[13] == 3);
    check("the device's identity is unchanged: VID 1209, PID 0001, the same strings' indices", dev[8] == 0x09 &&
          dev[9] == 0x12 && dev[10] == 0x01 && dev[11] == 0x00 && dev[14] == 1 && dev[15] == 2);
    for (o = 0; o < n; o += c[o]) {
        if (c[o + 1] == 4) {
            cur_if = c[o + 2];
            cur_alt = c[o + 3];
        } else if (c[o + 1] == 0x24 && c[o + 2] == 1 && cur_if == ua_if_cap && cur_alt == 1)
            as = c + o;                                    /* the AS general descriptor of alternate 1 */
    }
    check("the streaming interface's alternate 1 has its class descriptors", as && as + sizeof SLOOP_AS == c + n);
    if (!as)
        return;
    for (i = 0; i < sizeof SLOOP_AS; i++)
        if (as[i] != SLOOP_AS[i])
            diff |= 1u << (i < 7 ? 0 : i < 21 ? 1 : i < 30 ? 2 : 3);
    /* expected differences: the terminal's id (3), bNrChannels (11), the endpoint's address and wMaxPacketSize */
    check("the same bytes as SLOOP 2.5's but the terminal, the channel count, the endpoint's number and size", ({
        int ok = 1;
        for (i = 0; i < sizeof SLOOP_AS; i++)
            if (as[i] != SLOOP_AS[i] && i != 3 && i != 11 && i != 23 && i != 25 && i != 26)
                ok = 0;
        ok; }) && as[3] == 4 && as[11] == 6 && as[23] == 0x82);
    check("type I format: 14 bytes, 6 ch, 2-byte subframe, 16 bit, two discrete rates 44100 and 48000", as[7] == 14 &&
          as[9] == 2 && as[10] == 1 && as[11] == 6 && as[12] == 2 && as[13] == 16 && as[14] == 2 &&
          (as[15] | as[16] << 8 | as[17] << 16) == 44100 && (as[18] | as[19] << 8 | as[20] << 16) == 48000);
    mp = as[25] | as[26] << 8;
    snprintf(m, sizeof m, "endpoint: isochronous async IN, 1 ms; wMaxPacketSize %u >= 49 frames x 12 x 7 / 6 = %u", mp,
             49u * 12u * 7u / 6u);
    check(m, as[21] == 9 && as[22] == 5 && as[24] == 0x05 && as[27] == 1 && mp == UA_EP_MAXP && mp >= 686u && mp <= 1023u &&
             UA_PACKET == 588u);
    check("the endpoint's class descriptor: sampling-frequency control (bmAttributes bit 0)", as[30] == 7 &&
          as[31] == 0x25 && as[32] == 1 && (as[33] & 1u));
    check("wTotalLength 211 = the bytes sent; the console's presentation is as it was (175 bytes, 3.21)", n == 211 && ({
        const uint8_t *d2, *c2;
        uint16_t l2, n2;
        ua_off = UA_OFF_IN;
        usb_cdc_on = 1;
        ua_cfg_build();
        get_desc(0x0100, &d2, &l2);
        get_desc(0x0200, &c2, &n2);
        ua_off = 0;
        usb_cdc_on = 0;
        ua_cfg_build();
        n2 == 175 && d2[12] == 0x21; }));
    (void)diff;
}

/* ------------------------------------------------------------------ requests --- */
static int set_cur(uint32_t hz, uint32_t nbytes)           /* SET_CUR's data stage as usb.c hands it over */
{
    uint8_t p[4] = {(uint8_t)hz, (uint8_t)(hz >> 8), (uint8_t)(hz >> 16), 0};
    ua_rate_pending = 0x82;                                /* (ua_control_setup's SETUP stage: SIE, not run here) */
    return ua_control_data(p, nbytes);
}

static void test_requests(void)
{
    static const struct { uint32_t hz; uint8_t r48; } T[] = {
        {44100, 0}, {48000, 1}, {48000, 1}, {44100, 0}, {32000, 0}, {46049, 0}, {46050, 1}, {96000, 1}, {0, 0}};
    static int16_t st[32 * CH];
    uint8_t s[8] = {0xA2, 0x81, 0, 1, 0x82, 0, 3, 0};
    uint32_t rate = 1, i, ok = 1, sets, changes;
    memset(&ua, 0, sizeof ua);
    memset(&ua_dbg, 0, sizeof ua_dbg);
    ua_off = 0;
    usb_cdc_on = 0;
    ua_reset();
    ua_cfg_build();
    usb.config = 1;
    check("a sampling-frequency request to EP2 IN is ours; another endpoint, control or length is not", ua_control_is(s) && ({
        uint8_t a[8], b[8], c[8], d[8];
        memcpy(a, s, 8); memcpy(b, s, 8); memcpy(c, s, 8); memcpy(d, s, 8);
        a[4] = 0x81; b[3] = 2; c[6] = 4; d[2] = 1;
        !ua_control_is(a) && !ua_control_is(b) && !ua_control_is(c) && !ua_control_is(d); }));
    usb.config = 0;
    check("  not before the configuration is set", !ua_control_is(s));
    usb.config = 1;
    check("GET_CUR after a reset: 44100", ua_control_get(s, &rate) && rate == 44100);
    s[1] = 0x82;
    check("GET_MIN 44100", ua_control_get(s, &rate) && rate == 44100);
    s[1] = 0x83;
    check("GET_MAX 48000", ua_control_get(s, &rate) && rate == 48000);
    s[1] = 0x84;
    check("GET_RES 0 (discrete rates); GET_MEM is refused", ua_control_get(s, &rate) && rate == 0 && ({
        s[1] = 0x85;
        !ua_control_get(s, &rate); }));
    s[1] = 0x81;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint8_t was = ua.r48;
        ua.cap_alt = 1;
        ua.cw = 300;                                       /* (a running ring) */
        ua.cr = 100;
        ok &= set_cur(T[i].hz, 3) == 1 && ua.r48 == T[i].r48;
        ok &= ua_control_get(s, &rate) && rate == (T[i].r48 ? 48000u : 44100u);
        if (T[i].r48 != was)
            ok &= ua.cw == 0 && ua.cr == 0 && !ua.cap_ready;   /* a change empties the ring: primed again */
        else
            ok &= ua.cw == 300 && ua.cr == 100;            /* the same rate again: the stream is left alone */
    }
    check("SET_CUR: 44100 / 48000, the nearer of the two; GET_CUR follows; a change empties the ring, a repeat does not",
          ok);
    check("  counted: 9 SET_CURs, 4 changes", ua.rate_sets == 9 && ua.rate_changes == 4);
    sets = ua.rate_sets;
    changes = ua.rate_changes;
    check("SET_CUR with a data stage that is not three bytes is refused (stalled), nothing changes", !set_cur(48000, 2) &&
          !set_cur(48000, 4) && !ua.r48 && ua.rate_sets == sets && ua.rate_changes == changes && !ua_rate_pending);
    check("  and a data stage nobody asked for", ({ uint8_t p[3] = {0x80, 0xBB, 0}; ua_rate_pending = 0;
          !ua_control_data(p, 3) && !ua.r48; }));
    /* a change while the stream is open: the packet packed ahead at the old rate is dropped, the open's count restarts */
    ua_reset();
    ua.cap_alt = 1;
    ua_tx_bytes = 528;
    ua_dbg.frames_open = 12345;
    ua_dbg.open_at = 5;
    fm1_ms = 1000;
    set_cur(48000, 3);
    check("a change while open: the packet packed ahead is dropped, the open's frames count from here", ua.r48 &&
          !ua_tx_bytes && ua_dbg.frames_open == 0 && ua_dbg.open_at == 1000);
    ua_tx_bytes = 576;
    ua_dbg.frames_open = 777;
    set_cur(48000, 3);
    check("  the same rate again: nothing is dropped or restarted", ua_tx_bytes == 576 && ua_dbg.frames_open == 777);
    ua_tx_bytes = 0;
    /* a block made for the rate the host just left: never in the ring */
    for (i = 0; i < 32 * CH; i++)
        st[i] = 1000;
    ua.cap_hold = 0;
    {
        uint32_t k = ua_block48(st, 32);
        set_cur(44100, 3);
        ua_feed(ua.rs_out, k, 1);
        check("a block resampled for 48 kHz as the host goes back to 44.1: dropped, not fed", k >= 34 && ua.cw == 0);
        ua_feed(st, 32, 0);
        check("  the next 44.1 kHz block is fed as it is", ua.cw == 32 && ua.cap[0] == 1000);
    }
    set_cur(48000, 3);
    ua.cap_hold = 1;
    check("48 kHz, the host not reading yet (hold): nothing resampled, nothing fed", ua_block48(st, 32) == 0 && ({
        ua_feed(ua.rs_out, 0, 1);
        ua.cw == 0 && !ua.rs_on; }));
    ua_reset();
    check("a bus reset / new configuration (ua_reset): 44.1 kHz again", !ua.r48 && ({
        ua_control_get(s, &rate);
        rate == 44100; }));
    fm1_ms = 0;
}

/* ------------------------------------------------------------------ the resampler --- */
/* the reference: output k sits at input time k x 147 / 160: phase (147 k) % 160, newest input floor(147 k / 160), the
 * 12 frames ending there (silence before the first) times the table's phase (its mirror above 79), rounded, clipped */
static int16_t ref_out(const int16_t *x, uint32_t nin, uint32_t ch, uint32_t k)
{
    uint32_t ph = (uint32_t)(((uint64_t)k * UAC_FIR_M) % UAC_FIR_L), newest = (uint32_t)(((uint64_t)k * UAC_FIR_M) / UAC_FIR_L), j;
    int64_t a = 1 << (UAC_FIR_Q - 1);
    for (j = 0; j < UAC_FIR_N; j++) {
        int64_t idx = (int64_t)newest - (int64_t)(UAC_FIR_N - 1u) + j;
        int32_t c = ph < UAC_FIR_L / 2u ? UAC_FIR[ph][j] : UAC_FIR[UAC_FIR_L - 1u - ph][UAC_FIR_N - 1u - j];
        if (idx >= 0 && (uint32_t)idx < nin)
            a += (int64_t)c * x[(uint32_t)idx * CH + ch];
    }
    a >>= UAC_FIR_Q;
    return (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
}

#define RS_IN 14700u                                       /* 100 x 147 frames in: 16000 out */
static int16_t rs_x[RS_IN * CH], rs_y[(RS_IN * 160u / 147u + 64u) * CH], rs_y2[(RS_IN * 160u / 147u + 64u) * CH];

/* x through the resampler in blocks of blk frames (0: sizes 1..32 at random); returns the frames out */
static uint32_t rs_run(const int16_t *x, uint32_t nin, uint32_t blk, int16_t *y, uint32_t *kmin, uint32_t *kmax)
{
    uint32_t at = 0, n = 0, r = 77u;
    ua.rs_on = 0;
    if (kmin)
        *kmin = 99, *kmax = 0;
    while (at < nin) {
        uint32_t b = blk, k;
        if (!b) {
            r = r * 1103515245u + 12345u;
            b = 1u + (r >> 16) % 32u;
        }
        if (b > nin - at)
            b = nin - at;
        k = ua_resample(x + at * CH, b);
        memcpy(y + n * CH, ua.rs_out, k * CH * sizeof(int16_t));
        if (kmin && b == 32u) {
            *kmin = k < *kmin ? k : *kmin;
            *kmax = k > *kmax ? k : *kmax;
        }
        n += k;
        at += b;
    }
    return n;
}

static void test_exact(void)
{
    uint32_t i, ch, n, n2, kmin, kmax, bad = 0, lcg = 4242u;
    char m[200];
    for (i = 0; i < RS_IN * CH; i++) {
        lcg = lcg * 1664525u + 1013904223u;
        rs_x[i] = (int16_t)(lcg >> 16);
    }
    n = rs_run(rs_x, RS_IN, 32, rs_y, &kmin, &kmax);
    snprintf(m, sizeof m, "%u frames in (100 x 147): %u out (100 x 160), a block of 32 gives %u or %u", RS_IN, n, kmin, kmax);
    check(m, n == 16000u && kmin == 34u && kmax == 35u && UA_RS_OUT >= 36u);
    check("  the phase is back at 0 after every 147 frames: no drift, ever", ua.rs_ph == 0);
    for (i = 0; i < n; i++)
        for (ch = 0; ch < CH; ch++)
            bad += rs_y[i * CH + ch] != ref_out(rs_x, RS_IN, ch, i);
    snprintf(m, sizeof m, "noise in six channels: the C code = the reference of the same table, bit for bit (%u differ)", bad);
    check(m, bad == 0);
    n2 = rs_run(rs_x, RS_IN, 0, rs_y2, 0, 0);
    check("blocks of 1..32 frames at random: the same output, bit for bit (continuous across blocks)", n2 == n &&
          !memcmp(rs_y, rs_y2, n * CH * sizeof(int16_t)));
    n2 = rs_run(rs_x, RS_IN, 7, rs_y2, 0, 0);
    check("  and in blocks of 7", n2 == n && !memcmp(rs_y, rs_y2, n * CH * sizeof(int16_t)));
    /* DC: each phase's taps sum to exactly 1.0 */
    for (i = 0; i < RS_IN; i++)
        for (ch = 0; ch < CH; ch++)
            rs_x[i * CH + ch] = (int16_t)(ch & 1 ? -12345 - (int)ch : 12345 + (int)ch);
    n = rs_run(rs_x, RS_IN, 32, rs_y, 0, 0);
    for (bad = 0, i = 16; i < n; i++)
        for (ch = 0; ch < CH; ch++)
            bad += rs_y[i * CH + ch] != rs_x[ch];
    check("DC in = DC out, exactly, in every phase (the taps of a phase sum to 1.0)", bad == 0);
    /* full scale: +-32767 / -32768 squares at the worst rates: clipped at 16 bits, never wrapped */
    {
        uint32_t per, wraps = 0, clipped = 0;
        for (per = 2; per <= 24; per++) {
            for (i = 0; i < 1470u; i++)
                for (ch = 0; ch < CH; ch++)
                    rs_x[i * CH + ch] = (i / (per / 2u ? per / 2u : 1u)) & 1u ? -32768 : 32767;
            n = rs_run(rs_x, 1470u, 32, rs_y, 0, 0);
            for (i = 0; i < n; i++) {
                int16_t r = ref_out(rs_x, 1470u, 0, i);
                wraps += rs_y[i * CH] != r;
                clipped += r == 32767 || r == -32768;
            }
        }
        snprintf(m, sizeof m, "full-scale squares: 16-bit clipping where the filter overshoots (%u samples), no wrap (%u)",
                 clipped, wraps);
        check(m, wraps == 0 && clipped > 0);
    }
    /* the channels do not leak: one channel alone gives what it gives among the others */
    {
        static int16_t one[1470u * CH], y1[1700u * CH];
        uint32_t c2;
        for (i = 0; i < 1470u * CH; i++) {
            lcg = lcg * 1664525u + 1013904223u;
            rs_x[i] = (int16_t)(lcg >> 16);
        }
        n = rs_run(rs_x, 1470u, 32, rs_y, 0, 0);
        for (bad = 0, ch = 0; ch < CH; ch++) {
            memset(one, 0, sizeof one);
            for (i = 0; i < 1470u; i++)
                one[i * CH + ch] = rs_x[i * CH + ch];
            rs_run(one, 1470u, 32, y1, 0, 0);
            for (i = 0; i < n; i++)
                for (c2 = 0; c2 < CH; c2++)
                    bad += y1[i * CH + c2] != (c2 == ch ? rs_y[i * CH + c2] : 0);
        }
        check("six channels, each filtered alone: no leak between them", bad == 0);
    }
}

/* a sine of f Hz at fs in y[0..n) (every step-th value): the fitted amplitude and the SNR (the residual after a sine
 * + DC fit). SLOOP 2.5's tests/uac_test.c sine_fit */
static void sine_fit(const int16_t *y, uint32_t n, uint32_t step, double f, double fs, double *amp, double *snr)
{
    double a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0, m = 0, c1, c2, det, e = 0, p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m += y[i * step];
    m /= n;
    for (i = 0; i < n; i++) {
        double s = sin(2 * M_PI * f * i / fs), c = cos(2 * M_PI * f * i / fs), v = y[i * step] - m;
        a11 += s * s; a12 += s * c; a22 += c * c; b1 += s * v; b2 += c * v;
    }
    det = a11 * a22 - a12 * a12;
    c1 = (b1 * a22 - b2 * a12) / det;
    c2 = (b2 * a11 - b1 * a12) / det;
    for (i = 0; i < n; i++) {
        double s = sin(2 * M_PI * f * i / fs), c = cos(2 * M_PI * f * i / fs), fit = c1 * s + c2 * c;
        p += fit * fit;
        e += (y[i * step] - m - fit) * (y[i * step] - m - fit);
    }
    *amp = sqrt(c1 * c1 + c2 * c2);
    *snr = 10 * log10(p / (e > 1e-9 ? e : 1e-9));
}

/* the spectrum of y[0..4800) at 48 kHz (10 Hz bins, a 4-term Blackman-Harris window: side lobes under -92 dB): the
 * largest bin more than 60 Hz from the tone, in dB below the tone's, and where */
#define SP_N 4800u
static double spur(const int16_t *y, uint32_t step, double f, double *at_hz)
{
    static double w[SP_N], v[SP_N];
    double tone = 0, worst = 0;
    uint32_t i, b;
    for (i = 0; i < SP_N; i++) {
        double t = 2 * M_PI * i / SP_N;
        w[i] = 0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t);
        v[i] = w[i] * y[i * step];
    }
    for (b = 1; b < SP_N / 2u; b++) {
        double re = 0, im = 0, p, hz = b * 10.0;
        for (i = 0; i < SP_N; i++) {
            /* (exact phase: b x i mod 4800) */
            double a = 2 * M_PI * (double)((b * i) % SP_N) / SP_N;
            re += v[i] * cos(a);
            im -= v[i] * sin(a);
        }
        p = re * re + im * im;
        if (fabs(hz - f) <= 60.0) {
            if (p > tone)
                tone = p;
        } else if (p > worst) {
            worst = p;
            *at_hz = hz;
        }
    }
    return 10 * log10((worst > 1e-9 ? worst : 1e-9) / tone);
}

static void test_quality(void)
{
    static const double FQ[] = {100, 1000, 5000, 10000, 12000, 14000, 16000, 18000, 20000};
    static int16_t x[48000u * CH], y[53000u * CH];
    uint32_t q, i, ch, n;
    char s[200];
    if (table_only)
        printf("| Tone in | Gain | SNR (all that is not the tone) | Largest other product | at |\n| --- | --- | --- | --- | --- |\n");
    for (q = 0; q < sizeof FQ / sizeof FQ[0]; q++) {
        double amp, snr, amp5, snr5, g, sp, at = 0;
        for (i = 0; i < 48000u; i++) {                     /* -1 dBFS in the master pair, half of it inverted in the parts */
            double v = sin(2 * M_PI * FQ[q] * i / 44100.0);
            for (ch = 0; ch < CH; ch++)
                x[i * CH + ch] = (int16_t)lrint((ch < 2 ? 29204.0 : -14602.0) * v);
        }
        ua.rs_on = 0;
        for (n = 0, i = 0; i < 48000u; i += 32) {
            uint32_t k = ua_resample(x + i * CH, 32);
            memcpy(y + n * CH, ua.rs_out, k * CH * sizeof(int16_t));
            n += k;
        }
        sine_fit(y + 200 * CH, n - 400, CH, FQ[q], 48000.0, &amp, &snr);
        sine_fit(y + 200 * CH + 5, n - 400, CH, FQ[q], 48000.0, &amp5, &snr5);
        g = 20 * log10(amp / 29204.0);
        sp = spur(y + 1000 * CH, CH, FQ[q], &at);
        if (table_only) {
            printf("| %g Hz | %+.2f dB | %.1f dB | %.1f dB | %g Hz |\n", FQ[q], g, snr, sp, at);
            continue;
        }
        snprintf(s, sizeof s, "48 kHz: %5.0f Hz: gain %+.2f dB, SNR %.1f dB (Bass R %.1f), largest other %.1f dB at %.0f Hz",
                 FQ[q], g, snr, snr5, sp, at);
        if (FQ[q] <= 1000)
            check(s, n == (160u * 48000u - 1u) / 147u + 1u && fabs(g) < 0.02 && snr > 80.0 && snr5 > 74.0 && sp < -85.0);
        else if (FQ[q] <= 10000)
            check(s, fabs(g) < 0.02 && snr > 72.0 && sp < -72.0);
        else if (FQ[q] <= 12000)
            check(s, g > -0.05 && g < 0.02 && snr > 72.0 && sp < -72.0);
        else if (FQ[q] <= 14000)
            check(s, g > -0.3 && snr > 72.0 && sp < -72.0);
        else if (FQ[q] <= 16000)
            check(s, g > -1.1 && sp < -50.0);              /* 16 kHz: -1 dB, its image at 19.9 kHz */
        else if (FQ[q] <= 18000)
            check(s, g > -3.0 && g < -2.0 && fabs(at - 21900.0) < 30.0);   /* the image above 20 kHz: not heard */
        else
            check(s, g > -6.5 && fabs(at - 23900.0) < 30.0);
    }
}

/* ------------------------------------------------------------------ the stream --- */
#define ST_MS 12000u
static int16_t st_rec[(ST_MS * 49u + 64u) * CH];
/* the producer at fs Hz (halves of four 32-frame blocks: a 997 Hz sine at its own rate in the master pair, inverted
 * and halved in the parts), resampled and fed as audio.c does; the host takes one packet a ms. Returns the frames the
 * host got after the first second into st_rec; sizes[] counts the packets by frames */
static uint32_t stream48(double fs, uint32_t ms_total, uint32_t *sizes, uint32_t *produced)
{
    static int16_t st[32 * CH];
    static uint8_t pk[UA_PACKET];
    double next_half = 0, half_us = 128e6 / fs;
    uint32_t ms, i, ch, n, bytes, got = 0, seq = 0;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    ua_rate_set(48000);
    *produced = 0;
    for (ms = 0; ms < ms_total; ms++) {
        while (next_half <= ms * 1000.0) {
            for (n = 0; n < 4; n++) {
                uint32_t k;
                for (i = 0; i < 32; i++, seq++) {
                    double v = sin(2 * M_PI * 997.0 * seq / 44100.0);
                    for (ch = 0; ch < CH; ch++)
                        st[i * CH + ch] = (int16_t)lrint((ch < 2 ? 29204.0 : -14602.0) * v);
                }
                k = ua_block48(st, 32);
                ua_feed(ua.rs_out, k, 1);
                if (ms >= 1000u)
                    *produced += 32;
            }
            next_half += half_us;
        }
        ua_sof();
        bytes = ua_transmit(pk);
        if (ms < 1000u)
            continue;
        n = bytes / (2u * CH);
        if (n < 64u)
            sizes[n]++;
        for (i = 0; i < n * CH; i++)
            st_rec[got * CH + i] = (int16_t)(pk[2 * i] | pk[2 * i + 1] << 8);
        got += n;
    }
    return got;
}

static void test_stream(void)
{
    static const double FS[3] = {44117.647, 44056.0, 44100.0};
    uint32_t k, i, ch;
    char m[220];
    for (k = 0; k < 3; k++) {
        uint32_t sizes[64] = {0}, produced, got, npk = 0, sum = 0;
        double amp, amp5, snr, worst = 999, want;
        got = stream48(FS[k], ST_MS, sizes, &produced);
        for (i = 0; i < 64; i++) {
            npk += sizes[i];
            sum += sizes[i] * i;
        }
        want = 48.0;                                       /* frames a ms on the host's clock: the I2S's resampled
                                                            * to it (ua_hc_steer), every packet a 48 */
        snprintf(m, sizeof m, "48 kHz stream, I2S %.1f Hz, 11 s: packets of 47 / 48 / 49 frames (%u / %u / %u), mean %.4f "
                 "(%.4f)", FS[k], sizes[47], sizes[48], sizes[49], (double)sum / npk, want);
        check(m, sizes[47] + sizes[48] + sizes[49] == npk && npk == ST_MS - 1000u &&
                 fabs((double)sum / npk - want) < 0.03 && (k != 2 || sizes[48] > npk - 30u));
        snprintf(m, sizeof m, "  %u frames out for %u in = %.6f (48000 / I2S = %.6f): resampled to the host's clock",
                 got, produced, (double)got / produced, 48000.0 / FS[k]);
        check(m, fabs((double)got - produced * 48000.0 / FS[k]) < 300.0);
        check("  no underrun, no overrun", ua.cap_underruns == 0 && ua.cap_overruns == 0);
        /* one unbroken sine across every packet boundary, fitted 10 ms at a time: the steered ratio moves the
         * pitch by some ppm (under a cent), so one fixed frequency over 11 s no longer fits; a dropped or doubled frame
         * in a window is -18 dB there. The fit of 10 ms reads ~47 dB on the fixed-ratio resampler itself: its floor */
        {
            uint32_t w0;
            double lvl = 0, nw = 0;
            for (ch = 0; ch < CH; ch++)
                for (w0 = 0; w0 + 480u <= got; w0 += 480u) {
                    sine_fit(st_rec + w0 * CH + ch, 480u, CH, 997.0 * produced / got, 44100.0, &amp, &snr);
                    worst = snr < worst ? snr : worst;
                    if (!ch) { lvl += amp; nw++; }
                }
            amp = lvl / nw;
            {   /* (the windows' SNR over time, channel 0: where the worst ones are) */
                double mn[11] = {999,999,999,999,999,999,999,999,999,999,999};
                for (w0 = 0; w0 + 480u <= got; w0 += 480u) {
                    uint32_t sec = w0 / 48000u;
                    sine_fit(st_rec + w0 * CH, 480u, CH, 997.0 * produced / got, 44100.0, &amp5, &snr);
                    if (sec < 11u && snr < mn[sec]) mn[sec] = snr;
                }
                printf("     worst 10 ms SNR per second:");
                for (w0 = 0; w0 < 10u; w0++) printf(" %.0f", mn[w0]);
                printf("\n");
            }
        }
        snprintf(m, sizeof m, "  what the host records: an unbroken 997 Hz sine in all six channels, the worst 10 ms "
                 "SNR %.1f dB, level %+.2f dB", worst, 20 * log10(amp / 29204.0));
        check(m, worst > 35.0 && fabs(20 * log10(amp / 29204.0)) < 0.05);   /* (the old fixed-ratio resampler reads 47 dB by
                                                    * this measure: the 10 ms fit's own floor) */
    }
}

/* 44.1 -> 48 -> 44.1 while the host reads: a counter in channel 0 at 44.1 kHz (every frame once and in order), DC in
 * all channels at 48 kHz (its output is the same DC, exactly) */
static void test_switch(void)
{
    static int16_t st[32 * CH];
    static uint8_t pk[UA_PACKET];
    double next_half = 0, half_us = 128e6 / 44117.647;
    uint32_t ms, i, ch, n, bytes, bad44 = 0, bad48 = 0, badlen = 0, f44 = 0, f48 = 0, silent = 0, frames44b = 0;
    uint16_t seq = 0, expect = 0;
    int started = 0;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    usb.config = 1;
    for (ms = 0; ms < 9000u; ms++) {
        uint32_t r48;
        if (ms == 3000u)
            set_cur(48000, 3);
        if (ms == 6000u) {
            set_cur(44100, 3);
            started = 0;
        }
        while (next_half <= ms * 1000.0) {
            for (n = 0; n < 4; n++) {
                const int16_t *src = st;
                uint32_t fn = 32;
                r48 = ua.r48;
                for (i = 0; i < 32; i++, seq++)
                    for (ch = 0; ch < CH; ch++)
                        st[i * CH + ch] = r48 ? (int16_t)(1000 + ch) : (int16_t)(ch ? 100 + ch : (int16_t)(seq * 2u + 1u));
                if (r48) {                                 /* audio.c audio_block */
                    fn = ua_block48(st, 32);
                    src = ua.rs_out;
                }
                ua_feed(src, fn, r48);
            }
            next_half += half_us;
        }
        ua_sof();
        r48 = ua.r48;
        bytes = ua_transmit(pk);
        n = bytes / 12u;
        badlen += bytes % 12u != 0 || (r48 ? n < 47u || n > 49u : n < 44u || n > 45u);
        for (i = 0; i < n; i++) {
            int16_t v0 = (int16_t)(pk[12 * i] | pk[12 * i + 1] << 8), v5 = (int16_t)(pk[12 * i + 10] | pk[12 * i + 11] << 8);
            if (!v5) {
                silent += ms >= 3000u && ms < 3100u;
                continue;                                  /* silence: priming */
            }
            if (r48) {
                /* (the first frames after the change run the filter in from silence: DC from the 12th on) */
                if (ms >= 3020u)
                    bad48 += v0 != 1000 || v5 != 1005;
                f48++;
            } else {
                bad44 += v5 != 105;
                if (started && (uint16_t)v0 != expect)
                    bad44++;
                started = 1;
                expect = (uint16_t)((uint16_t)v0 + 2u);
                f44++;
                frames44b += ms >= 6000u;
            }
        }
    }
    {
        char m[220];
        snprintf(m, sizeof m, "44.1 -> 48 -> 44.1 while streaming: packets 44/45 then 47..49 then 44/45 (%u bad lengths)", badlen);
        check(m, badlen == 0);
        snprintf(m, sizeof m, "  at 44.1 kHz every frame in order before and after (%u frames, %u wrong; %u after)", f44, bad44,
                 frames44b);
        check(m, bad44 == 0 && frames44b > 2900u * 44u && f44 > 5900u * 44u);
        snprintf(m, sizeof m, "  at 48 kHz the resampled signal (%u frames, %u wrong), silence while the ring primes (%u frames)",
                 f48, bad48, silent);
        check(m, bad48 == 0 && f48 > 2900u * 48u && silent > 100u && silent < 800u);
        check("  two changes, no underrun or overrun from them", ua.rate_changes == 2 && ua.cap_underruns == 0 &&
              ua.cap_overruns == 0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--table")) {
        table_only = 1;
        test_quality();
        return 0;
    }
    test_descriptors();
    test_requests();
    test_exact();
    test_quality();
    test_stream();
    test_switch();
    printf("cr_usbaudio48: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
