/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The reverb bus's two models (src/fx.c: REVERB TYPE, G_RTYPE) on the Mac, through hostsim.c as regress.c.
 *   build/host/reverb_test [DEMODIR]          (run_tests.sh: build/fx_demo)
 * 1. ROOM bit-identical: fx_buses against a copy of the buses as they were before SPRING (chorus, delay and the
 *    4-comb room in one loop), on noise sends with SIZE / DAMP / the delay and chorus settings changing, sample
 *    for sample. (The goldens of regress.c, all rendered with ROOM, say the same for the whole mix.)
 * 2. SPRING decay: the impulse response's RT60 (Schroeder integral, -5 .. -35 dB) rises with SIZE, within
 *    0.15 .. 1 s at SIZE 0 and 2 .. 6 s at 127.
 * 3. SPRING dispersion: the group delay of the first arrival rises with frequency (1 .. 5 kHz, each band later
 *    than the one below, 5 kHz at least 2 ms after 1 kHz; 500 Hz printed: the loop's low cut delays it a
 *    little), and the second arrival (one more pass round the loop) is more spread than the first: the
 *    chirp grows with each echo.
 * 4. SPRING stable at the corners: SIZE 127 with DAMP 0 and 127, 2 s of full-scale noise or square waves (the
 *    sum of four tracks' largest sends) then silence: bounded, and the tail dies away (no limit cycle, no
 *    offset).
 * 5. level: SPRING's tail within 6 dB of ROOM's (RMS of the first 1.5 s after a noise burst, defaults).
 * 6. a model change while the bus rings: no click (the old one's block fades out), the new one starts silent.
 * 7. cost: host instructions per sample of the reverb alone (rev_room, rev_spring): SPRING at most ROOM + 30 %;
 *    and of the whole bus stage (fx_buses), with the device estimate (1.7 % per 100, drum_test's ratio).
 * Demos (WAV) into DEMODIR: a drum pattern and a pluck through SPRING, the drums through ROOM to compare. */
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
    printf("reverb: %-92s %s\n", what, ok ? "ok" : "FAIL");
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

/* ------------------------------------------------- the buses before SPRING --- */
static int16_t ref_dly[DLY_LEN], ref_cho[CHO_LEN], ref_comb[1116 + 1188 + 1277 + 1356], ref_ap[556 + 441];
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
} rf;
static void ref_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet, uint32_t n)
{
    uint32_t i, k, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    for (i = 0; i < n; i++) {
        int32_t y = 0, x, r, a;
        ref_cho[rf.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        rf.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(rf.cho_ph) + 32768) * cdepth >> 8);
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = ref_cho[(rf.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = ref_cho[(rf.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        rf.cho_w++;
        x = ref_dly[(rf.dly_w - dl) & (DLY_LEN - 1u)];
        rf.dly_lp += mulq15(x - rf.dly_lp, col);
        ref_dly[rf.dly_w & (DLY_LEN - 1u)] = (int16_t)clamp((dly_in[i] >> 1) + mulq15(rf.dly_lp, fb), -32768, 32767);
        rf.dly_w++;
        y += mulq15(x << 1, dmix);
        a = 0;
        {
            int16_t *c = ref_comb;
            int32_t in = mulq15(rev_in[i], 2580);
            for (k = 0; k < 4u; k++) {
                int32_t o = c[rf.comb_i[k]];
                rf.comb_lp[k] = o + mulq15(rf.comb_lp[k] - o, 32767 - damp);
                c[rf.comb_i[k]] = (int16_t)clamp(in + mulq15(rf.comb_lp[k], size), -32768, 32767);
                if (++rf.comb_i[k] >= REV_COMB[k])
                    rf.comb_i[k] = 0;
                a += o;
                c += REV_COMB[k];
            }
            c = ref_ap;
            for (k = 0; k < 2u; k++) {
                int32_t o = c[rf.ap_i[k]];
                int32_t v = a + (o >> 1);
                c[rf.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
                a = o - a;
                if (++rf.ap_i[k] >= REV_AP[k])
                    rf.ap_i[k] = 0;
                c += REV_AP[k];
            }
        }
        y += a;
        wet[i] = y;
    }
}

static void test_room_identical(void)
{
    static int32_t c[CTL], d[CTL], r[CTL], w0[CTL], w1[CTL];
    uint32_t b, i, diff = 0, nb = 30u * FS / CTL;
    host_tracks_init();
    for (b = 0; b < nb; b++) {
        if (b % 700u == 0u) {                                   /* the settings move now and then */
            song.g[G_RSIZE] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
            song.g[G_RDAMP] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
            song.g[G_DTIME] = (int16_t)((uint32_t)noise(1 << 30) % 6u);
            song.g[G_DFDBK] = (int16_t)((uint32_t)noise(1 << 30) % 121u);
            song.g[G_CDEPTH] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
        }
        for (i = 0; i < CTL; i++) {
            int32_t on = (b / 300u) % 3u != 2u;                 /* bursts and silences: the tails too */
            c[i] = on ? noise(60000) : 0;
            d[i] = on ? noise(60000) : 0;
            r[i] = on ? noise(b % 2000u < 1000u ? 90000 : 4000) : 0;
        }
        fx_buses(c, d, r, w0, CTL);
        ref_buses(c, d, r, w1, CTL);
        for (i = 0; i < CTL; i++)
            diff += w0[i] != w1[i];
    }
    check("ROOM: the buses bit for bit as before SPRING (30 s of noise sends, settings changing)", !diff && !fx.rtype);
}

/* ------------------------------------------------------------- SPRING --- */
#define IRN (12u * FS / CTL * CTL)
static int32_t ir[IRN];
static void spring_reset(int32_t size, int32_t damp)
{
    song.g[G_RTYPE] = 1;
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = (int16_t)damp;
    rev_clear();
    fx.rtype = 1;
    fx.sp_w = 0;
    fx.sp_size = 0;
    fx.sp_ph = 0;
}
/* the spring's response to x[] (len samples, the rest silence) into ir[0 .. n) */
static void spring_run(const int32_t *x, uint32_t len, uint32_t n)
{
    static int32_t in[CTL];
    uint32_t t, i;
    for (t = 0; t < n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            in[i] = t + i < len ? x[t + i] : 0;
            ir[t + i] = 0;
        }
        rev_spring(in, ir + t, CTL);
    }
}
static double rt60(const int32_t *h, uint32_t n)       /* Schroeder: -5 .. -35 dB, x2 */
{
    static double e[IRN];
    double s = 0, t5 = -1, t35 = -1;
    uint32_t i;
    for (i = n; i-- > 0;) {
        s += (double)h[i] * h[i];
        e[i] = s;
    }
    for (i = 0; i < n; i++) {
        double db = 10 * log10(e[i] / e[0] + 1e-30);
        if (t5 < 0 && db <= -5)
            t5 = i;
        if (t35 < 0 && db <= -35) {
            t35 = i;
            break;
        }
    }
    return t35 < 0 ? 99 : (t35 - t5) * 2.0 / FS;
}
/* group delay (samples) of h[a .. b) at f Hz: Re(sum n h e^-jwn / sum h e^-jwn) */
static double gdelay(const int32_t *h, uint32_t a, uint32_t b, double f)
{
    double w = 2 * M_PI * f / FS, ar = 0, ai = 0, br = 0, bi = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        double c = cos(w * i), s = -sin(w * i), v = h[i];
        ar += v * c;
        ai += v * s;
        br += i * v * c;
        bi += i * v * s;
    }
    return (br * ar + bi * ai) / (ar * ar + ai * ai);
}

static void test_spring(void)
{
    static int32_t imp[1] = {200000}, burst[2u * FS];
    static const int32_t SZ[3] = {0, 64, 127};
    double r[3];
    char what[200];
    uint32_t k, i;
    host_tracks_init();
    for (k = 0; k < 3u; k++) {
        spring_reset(SZ[k], 60);
        spring_run(imp, 1, IRN);
        r[k] = rt60(ir, IRN);
    }
    snprintf(what, sizeof what, "SPRING decay rises with SIZE: RT60 %.2f s (SIZE 0), %.2f (64), %.2f (127)", r[0], r[1], r[2]);
    check(what, r[0] < r[1] && r[1] < r[2] && r[0] >= 0.15 && r[0] <= 1.0 && r[2] >= 2.0 && r[2] <= 6.0);
    {   /* the chirp: the first arrival (main pickup at L / 2, before the second at 3 L / 4), and the next one */
        static const double F[6] = {500, 1000, 2000, 3000, 4000, 5000};
        double g1[6], g2[6];
        uint32_t L = 1323u + ((127u * 1323u) >> 7), a = L / 2u - 64u, b = (L * 3u) / 4u - 16u, rise = 1;
        spring_reset(127, 0);
        spring_run(imp, 1, 3u * L);
        for (i = 0; i < 6u; i++) {
            g1[i] = gdelay(ir, a, b, F[i]) - L / 2.0;
            g2[i] = gdelay(ir, a + L, b + L, F[i]) - L * 1.5;
            if (i >= 2u && (g1[i] <= g1[i - 1] + 0.5 || g2[i] <= g2[i - 1] + 0.5))
                rise = 0;
        }
        snprintf(what, sizeof what, "SPRING chirp: 1st arrival's group delay %.0f %.0f %.0f %.0f %.0f %.0f samples (0.5 1 2 3 4 5 kHz)",
                 g1[0], g1[1], g1[2], g1[3], g1[4], g1[5]);
        check(what, rise && (g1[5] - g1[1]) * 1000 / FS >= 2.0);
        snprintf(what, sizeof what, "  the next pass more spread: 2nd arrival 0.5 -> 5 kHz %.1f ms (1st %.1f)",
                 (g2[5] - g2[0]) * 1000 / FS, (g1[5] - g1[0]) * 1000 / FS);
        check(what, g2[5] - g2[0] > 1.5 * (g1[5] - g1[0]));
    }
    for (k = 0; k < 4u; k++) {   /* the corners: full-scale noise or square waves, then silence */
        int32_t pk = 0, tail = 0;
        for (i = 0; i < 2u * FS; i++)                   /* beyond any send (4 tracks' at most ~2^17 each) */
            burst[i] = k < 2u ? noise(1 << 19) : (i / (k == 2u ? 7u : 53u)) & 1u ? 1 << 19 : -(1 << 19);
        spring_reset(127, k & 1u ? 127 : 0);
        spring_run(burst, 2u * FS, IRN);
        for (i = 0; i < IRN; i++) {
            int32_t v = abs(ir[i]);
            pk = v > pk ? v : pk;
            if (i >= IRN - FS)
                tail = v > tail ? v : tail;
        }
        snprintf(what, sizeof what, "SPRING at SIZE 127 DAMP %d: 2 s of %s: peak %d, the last second %d",
                 k & 1u ? 127 : 0, k < 2u ? "full-scale noise" : k == 2u ? "a full-scale 3.2 kHz square" : "a 416 Hz square",
                 pk, tail);
        check(what, pk <= 6 * 32768 && tail <= 4);
    }
    {   /* the level against ROOM: the same noise burst, defaults */
        static int32_t in[CTL], o[CTL];
        double er = 0, es = 0;
        uint32_t t, m;
        for (m = 0; m < 2u; m++) {
            host_tracks_init();
            rev_clear();
            fx.sp_size = 0;
            xs = 99;
            for (t = 0; t < 2u * FS; t += CTL) {
                for (i = 0; i < CTL; i++) {
                    in[i] = t < FS / 4u ? noise(20000) : 0;
                    o[i] = 0;
                }
                if (m)
                    rev_spring(in, o, CTL);
                else
                    rev_room(in, o, CTL);
                for (i = 0; i < CTL; i++)
                    *(m ? &es : &er) += (double)o[i] * o[i];
            }
        }
        snprintf(what, sizeof what, "level: SPRING's tail %.1f dB from ROOM's (the same burst, SIZE 90 DAMP 60)", 10 * log10(es / er));
        check(what, fabs(10 * log10(es / er)) <= 6.0);
        rev_clear();
    }
}

/* --------------------------------------------------------- model change --- */
static void test_switch(void)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t b, i;
    int32_t prev = 0, step = 0, own = 0, after = 0;
    char what[200];
    host_tracks_init();
    rev_clear();
    memset(dly_buf, 0, sizeof dly_buf);                         /* (the delay and chorus quiet: only the reverb) */
    memset(cho_buf, 0, sizeof cho_buf);
    fx.rtype = 0;
    for (b = 0; b < 4u * FS / CTL; b++) {
        if (b == 2u * FS / CTL)
            song.g[G_RTYPE] = 1;
        for (i = 0; i < CTL; i++) {
            c[i] = d[i] = 0;
            r[i] = b < FS / CTL ? (int32_t)(30000 * sin(2 * M_PI * 220 * (b * CTL + i) / FS)) : 0;
        }
        fx_buses(c, d, r, w, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t s = abs(w[i] - prev);
            if (b >= 2u * FS / CTL - 4u && b <= 2u * FS / CTL + 4u)
                step = s > step ? s : step;
            else if (b > FS / CTL + 100u && b < 2u * FS / CTL - 4u)
                own = s > own ? s : own;
            if (b > 2u * FS / CTL)
                after = abs(w[i]) > after ? abs(w[i]) : after;
            prev = w[i];
        }
    }
    snprintf(what, sizeof what, "ROOM -> SPRING while the tail rings: largest step %d (the tail's own %d), silent after: %d",
             step, own, after);
    check(what, step <= 2 * own + 64 && after == 0 && fx.rtype == 1);
    song.g[G_RTYPE] = 0;
    fx_buses(c, d, r, w, CTL);
    check("  and back to ROOM", fx.rtype == 0);
}

/* ---------------------------------------------------------------- cost --- */
static double cost_of(int what)              /* 0 rev_room, 1 rev_spring, 2 fx_buses ROOM, 3 fx_buses SPRING */
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t b, i, nb = 4u * FS / CTL;
    uint64_t i0;
    host_tracks_init();
    rev_clear();
    song.g[G_RTYPE] = (int16_t)(what & 1);
    fx.rtype = (uint8_t)(what & 1);
    for (i = 0; i < CTL; i++) {
        c[i] = noise(30000);
        d[i] = noise(30000);
        r[i] = noise(30000);
    }
    i0 = instr_now();
    for (b = 0; b < nb; b++) {
        if (what >= 2)
            fx_buses(c, d, r, w, CTL);
        else if (what)
            rev_spring(r, w, CTL);
        else
            rev_room(r, w, CTL);
    }
    return i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
}
static void test_cost(void)
{
    double room = cost_of(0), spr = cost_of(1), broom = cost_of(2), bspr = cost_of(3);
    char what[200];
    if (!room) {
        printf("reverb: cost: no instruction counter on this host\n");
        return;
    }
    snprintf(what, sizeof what, "cost: the reverb alone ROOM %.0f, SPRING %.0f instructions / sample (+%.0f %%, limit +30 %%)",
             room, spr, (spr / room - 1) * 100);
    check(what, spr <= room * 1.30);
    snprintf(what, sizeof what, "  the bus stage (chorus, delay, reverb) ROOM %.0f (~%.1f %%), SPRING %.0f (~%.1f %%): +%.0f (~%.2f %% CPU)",
             broom, broom * 0.017, bspr, bspr * 0.017, bspr - broom, (bspr - broom) * 0.017);
    check(what, (bspr - broom) * 0.017 <= 2.0);
}

/* ------------------------------------------------------------- demos --- */
static void demo(const char *dir, const char *name, int rtype, int pluck)
{
    static const uint8_t PL[16] = {64, 0, 67, 0, 71, 0, 0, 74, 72, 0, 67, 0, 64, 0, 62, 0};
    char path[512];
    FILE *f;
    uint32_t t, i, frames = 8u * FS;
    int32_t o[2 * CTL];
    track_t *tr = &trk[0];
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    rev_clear();
    fx.rtype = (uint8_t)rtype;
    song.g[G_RTYPE] = (int16_t)rtype;
    song.g[G_BPM] = 100;
    if (pluck) {
        host_preset(tr, 0, 8);                                     /* ANALOG PLUCK */
        for (i = 0; i < 16u; i++)
            put_step(tr, i, PL[i] ? 1u : 0u, &PL[i], PL[i] ? ST_NOTE : ST_REST, 0);
    } else {
        host_legacy_sample_perc(tr);                               /* the GM kit */
        for (i = 0; i < 16u; i++) {
            uint8_t dn[3], k = 0;
            if (i % 4u == 0u || i == 10u) dn[k++] = 36;
            if (i == 4u || i == 12u) dn[k++] = 38;
            if (i % 2u == 0u) dn[k++] = 42;
            put_step(tr, i, k, dn, k ? ST_NOTE : ST_REST, 0);
        }
    }
    tr->p[P_REV] = 90;
    tr->p[P_DLY] = tr->p[P_CHOR] = 0;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, frames);
    transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (t == 6u * FS)
            transport_req = 2;                                     /* the last 2 s: the tail alone */
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    printf("reverb: demo %s\n", path);
}

int main(int argc, char **argv)
{
    test_room_identical();
    test_spring();
    test_switch();
    test_cost();
    if (argc > 1) {
        demo(argv[1], "spring_drums", 1, 0);
        demo(argv[1], "spring_pluck", 1, 1);
        demo(argv[1], "room_drums", 0, 0);
    }
    printf("%s\n", bad ? "REVERB TEST FAILED" : "reverb test passed");
    return bad != 0;
}
