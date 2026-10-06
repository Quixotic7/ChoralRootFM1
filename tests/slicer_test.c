/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the SLICER (firmware/src/slicer.c), same sources as the firmware (through hostsim.c).
 *   build/host/slicer_test [DEMO_DIR]          (run_tests.sh: build/slicer_demo)
 * 1. no clicks: a 110 Hz sine through GATE / STUT at several rates, tempi, swings, and with the mode,
 *    pattern and depth turned while it runs: the largest sample step of the output stays within a few
 *    times the sine's own (a hard gate edge would be ~60 x).
 * 2. timing: a constant signal through GATE at 97 BPM with swing: every step's middle is open or
 *    closed as the pattern says, every closing ramp ends on the step boundary (sample exact, from the
 *    swung step lengths of seq.c).
 * 3. sync: the 4-track song with the transport: at every block the SLICER's step is the sequencer's.
 * 4. OFF is transparent (the signal untouched), STUT repeats what the live step played.
 * 5. cost: instructions per sample of the mix, 4 SLICERs on against off (proc_pid_rusage).
 * Demos (WAV, 44.1 kHz) into DEMO_DIR: dry / gated / stuttered versions of a pad, the acid line, the
 * drums (track 4: SAMPLE PERC), and the song. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int check(const char *what, int ok)
{
    printf("slicer: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void set_slicer(track_t *t, int mode, int pat, int rate, int depth)
{
    t->p[P_SLCR] = (int16_t)mode;
    t->p[P_SLPAT] = (int16_t)pat;
    t->p[P_SLRATE] = (int16_t)rate;
    t->p[P_SLDEPTH] = (int16_t)depth;
}

/* ---------------------------------------------------------- 1. clicks --- */
/* n seconds of a 110 Hz sine (amplitude 20000) through track 1's SLICER; turn: change the settings
 * while it runs. Returns the largest |step| of the output; *in_step: the sine's own */
static int32_t sine_run(double secs, int turn, int32_t *in_step, int64_t *closed_energy)
{
    uint32_t f, i, frames = (uint32_t)(secs * FS) / CTL * CTL;
    int32_t prev = 0, pin = 0, mx = 0, mi = 0;
    double ph = 0, dph = 2 * M_PI * 110.0 / FS;
    track_t *t = &trk[0];
    *closed_energy = 0;
    slicer_start();
    for (f = 0; f < frames; f += CTL) {
        int32_t b[CTL];
        if (turn && f % 3008u == 0u) {          /* modes, patterns, depths, rates while it runs */
            uint32_t k = f / 3008u;
            set_slicer(t, (int)(k % 3u), (int)(1u + k * 7u % 16u), (int)(k % 6u), (int)(40u + k * 37u % 88u));
        }
        for (i = 0; i < CTL; i++) {
            b[i] = (int32_t)lrint(20000.0 * sin(ph));
            ph += dph;
            if (f + i) {
                int32_t d = abs(b[i] - pin);
                mi = d > mi ? d : mi;
            }
            pin = b[i];
        }
        slicer_track(t, b, CTL);
        for (i = 0; i < CTL; i++) {
            if (f + i) {
                int32_t d = abs(b[i] - prev);
                mx = d > mx ? d : mx;
            }
            prev = b[i];
            if (!sl[0].bit && sl[0].pos > SL_RAMP + CTL)
                *closed_energy += (int64_t)b[i] * b[i];
        }
    }
    *in_step = mi;
    return mx;
}

static int test_clicks(void)
{
    static const struct { const char *name; int mode, pat, rate, depth, bpm, swing; double secs; } C[] = {
        {"GATE pattern 1, 1/16, 120 BPM, 100 %", SL_GATE, 1, 1, 127, 120, 0, 4},
        {"GATE pattern 12, 1/32, 240 BPM, 100 %", SL_GATE, 12, 2, 127, 240, 0, 3},
        {"GATE pattern 2, 32T, 240 BPM, swing 50 %", SL_GATE, 2, 5, 127, 240, 50, 3},
        {"GATE pattern 5, 8T, 90 BPM, 60 %", SL_GATE, 5, 3, 76, 90, 10, 4},
        {"STUT pattern 7, 1/16, 120 BPM, 100 %", SL_STUT, 7, 1, 127, 120, 0, 4},
        {"STUT pattern 3, 32T, 200 BPM, 70 %, swing 30 %", SL_STUT, 3, 5, 90, 200, 30, 3},
        {"STUT pattern 9, 1/8, 40 BPM (loop shorter than the step)", SL_STUT, 9, 0, 127, 40, 0, 8},
        {"STUT pattern 13, 16T, 133 BPM", SL_STUT, 13, 4, 127, 133, 0, 4},
    };
    uint32_t c;
    int bad = 0;
    char what[160];
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        int32_t in, out;
        int64_t ce;
        host_tracks_init();
        memset(sl, 0, sizeof sl);
        song.g[G_BPM] = (int16_t)C[c].bpm;
        trk[0].p[P_SSWING] = (int16_t)C[c].swing;
        set_slicer(&trk[0], C[c].mode, C[c].pat, C[c].rate, C[c].depth);
        out = sine_run(C[c].secs, 0, &in, &ce);
        snprintf(what, sizeof what, "no clicks: %s: largest step %d (sine %d, %.1f x)", C[c].name, out, in,
                 (double)out / in);
        bad += check(what, out <= 4 * in);
        if (C[c].mode == SL_GATE && C[c].depth == 127) {
            snprintf(what, sizeof what, "  closed steps silent (rms %.2f)", sqrt((double)ce / (FS * C[c].secs)));
            bad += check(what, ce < (int64_t)(FS * C[c].secs));
        }
    }
    {
        int32_t in, out;
        int64_t ce;
        host_tracks_init();
        memset(sl, 0, sizeof sl);
        out = sine_run(10, 1, &in, &ce);
        snprintf(what, sizeof what, "no clicks: mode / pattern / depth / rate turned while it runs: %d (%.1f x)", out,
                 (double)out / in);
        bad += check(what, out <= 4 * in);
    }
    return bad;
}

/* ---------------------------------------------------------- 2. timing --- */
static int test_timing(void)
{
    const uint32_t steps = 64u, pat = 8u;
    uint32_t f = 0, k, b0 = 0, bad_mid = 0, bad_edge = 0, n = 0;
    static int32_t out[16u * 44100u];
    int bad = 0;
    char what[160];
    track_t *t = &trk[0];
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_BPM] = 97;
    song.g[G_SWING] = 10;
    t->p[P_SSWING] = 20;
    set_slicer(t, SL_GATE, (int)pat, 1, 127);
    slicer_start();
    while (f + CTL <= sizeof out / sizeof out[0]) {
        uint32_t i;
        for (i = 0; i < CTL; i++)
            out[f + i] = 16384;
        slicer_track(t, out + f, CTL);
        f += CTL;
    }
    for (k = 0; k < steps; k++) {                 /* the step lengths as seq.c step_samples has them */
        uint32_t base = (uint32_t)FS * 60u / 97u / 4u, len, idx = k & 15u, on = (SL_PAT[pat - 1u] >> idx) & 1u;
        uint32_t non = (SL_PAT[pat - 1u] >> ((idx + 1u) & 15u)) & 1u;
        int32_t sw = (20 + 10) * (int32_t)base / 250;
        len = base + (uint32_t)((idx & 1u) ? -sw : sw);
        if (b0 + len >= f)
            break;
        n++;
        if (on ? out[b0 + len / 2u] != 16384 : abs(out[b0 + len / 2u]) > 2)
            bad_mid++;
        if (on && !non && (abs(out[b0 + len - 1u]) > 300 || out[b0 + len - 1u - SL_RAMP] != 16384))
            bad_edge++;                           /* closed by the boundary, not earlier */
        if (!on && non && (abs(out[b0 + len]) > 300 || out[b0 + len + SL_RAMP] != 16384))
            bad_edge++;                           /* opens from the boundary */
        b0 += len;
    }
    snprintf(what, sizeof what, "timing: GATE at 97 BPM, swing 20 + 10 %%: %u steps open / closed as the pattern says",
             n);
    bad += check(what, !bad_mid && n > 32u);
    bad += check("timing: every ramp ends / starts on the swung step boundary (sample exact)", !bad_edge);
    return bad;
}

/* ------------------------------------------------------------ the song --- */
static void song_setup(void)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t ACIDF[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
    static const uint8_t AM[4] = {57, 60, 64, 67}, FMI[4] = {53, 57, 60, 64};
    static const uint8_t LEAD[12] = {76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0};
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = &trk[3];
    uint32_t i;
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_preset(t3, 3, 0);
    host_legacy_sample_perc(td);                   /* saved SAMPLE PERC sound */
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i];
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, ACIDF[i]);
    }
    t2->p[P_SLEN] = 32;
    t2->p[P_SGATE] = 120;
    for (i = 0; i < 32u; i++)
        put_step(t2, i, i % 16u == 0u ? 4u : 0u, i < 16u ? AM : FMI, i % 16u == 0u ? ST_NOTE : i % 16u < 14u ? ST_TIE : ST_REST, 0);
    t3->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        uint8_t n = LEAD[i];
        put_step(t3, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        if (i % 2u == 0u)
            n[k++] = i == 14u ? 46 : 42;
        put_step(td, i, k, n, k ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
}

/* ------------------------------------------------------------ 3. sync --- */
static int test_sync(void)
{
    uint32_t f, k, blocks = 0, miss = 0;
    char what[160];
    song_setup();
    song.g[G_SWING] = 15;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SSWING] = (int16_t)(10 * k);
        trk[k].p[P_SLEN] = 16;                    /* 16 steps of 1/16: the SLICER's bar */
        set_slicer(&trk[k], k & 1u ? SL_STUT : SL_GATE, (int)(3u + k), 1, 127);
    }
    song.g[G_BPM] = 131;
    transport_req = 1;
    for (f = 0; f < 12u * FS; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        blocks++;
        for (k = 0; k < NTRK; k++) {              /* the SLICER step that holds the block's first sample */
            uint32_t idx = sl[k].pos >= CTL ? sl[k].idx : (sl[k].idx + 15u) & 15u;
            miss += idx != trk[k].seq_idx % 16u;
        }
    }
    transport_req = 2;
    snprintf(what, sizeof what, "sync: 4 tracks, 131 BPM, swing per track: the sequencer's step at %u blocks x 4", blocks);
    return check(what, !miss);
}

/* ------------------------------------------------------- 4. OFF / STUT --- */
static int test_off_stut(void)
{
    uint32_t f, i, same = 1, rep_ok = 0, rep_n = 0;
    track_t *t = &trk[0];
    int bad = 0;
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    slicer_start();
    for (f = 0; f < FS; f += CTL) {               /* OFF: untouched */
        int32_t b[CTL], c[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = c[i] = (int32_t)((f + i) * 2654435761u >> 17) - 16384;
        slicer_track(t, b, CTL);
        same &= !memcmp(b, c, sizeof b);
    }
    bad += check("OFF: the signal is not touched", same);
    /* STUT, pattern 7 (x...), 1/16 at 120 BPM, 100 %: the repeat steps play the live step again */
    set_slicer(t, SL_STUT, 7, 1, 127);
    slicer_start();
    {
        static int32_t in[4u * 44100u], out[4u * 44100u];
        uint32_t len = (uint32_t)FS * 60u / 120u / 4u;
        for (f = 0; f < sizeof in / sizeof in[0]; f++)
            in[f] = out[f] = (int32_t)(12000.0 * sin(2 * M_PI * 150.0 * f / FS));   /* 18.75 periods a step */
        for (f = 0; f + CTL <= sizeof in / sizeof in[0]; f += CTL)
            slicer_track(t, out + f, CTL);
        for (f = 0; f + 4u * len < sizeof in / sizeof in[0]; f += 4u * len)   /* steps 1..3 of every 4 = step 0 */
            for (i = 1; i < 4u; i++) {
                uint32_t a = f + i * len + len / 2u, b = f + len / 2u;
                rep_n++;
                rep_ok += abs(out[a] - in[b]) < 400;   /* (22 kHz recording, read between samples) */
            }
    }
    {
        char what[160];
        snprintf(what, sizeof what, "STUT: the repeat steps play the live step (%u of %u checked)", rep_ok, rep_n);
        bad += check(what, rep_ok == rep_n && rep_n > 8u);
    }
    return bad;
}

/* ------------------------------------------------------------ 5. cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static double cost_run(int on)
{
    uint32_t f, k;
    uint64_t i0;
    int32_t o[2 * CTL];
    song_setup();
    for (k = 0; k < NTRK; k++)
        set_slicer(&trk[k], on ? (k & 1u ? SL_GATE : SL_STUT) : 0, 4, 1, 100);
    transport_req = 1;
    for (f = 0; f < FS; f += CTL)
        mix_block(o, CTL);
    i0 = instr_now();
    for (f = 0; f < 2u * FS; f += CTL)
        mix_block(o, CTL);
    transport_req = 2;
    return i0 ? (double)(instr_now() - i0) / (2.0 * FS) : 0;
}

static int test_cost(void)
{
    double off = cost_run(0), on = cost_run(1);
    char what[160];
    if (!off) {
        printf("slicer: cost: no instruction counter on this host\n");
        return 0;
    }
    snprintf(what, sizeof what, "cost: the song, 4 SLICERs on: %.0f instructions / sample (off %.0f): +%.0f, %.0f a track",
             on, off, on - off, (on - off) / 4);
    return check(what, (on - off) / 4 < 60);
}

/* ------------------------------------------------------------ demos --- */
static void demo_song(const char *dir, const char *name, const int16_t (*s)[4], uint32_t solo)
{
    char path[512];
    FILE *w;
    uint32_t f, k, frames = 8u * FS;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(w = fopen(path, "wb")))
        return;
    song_setup();
    for (k = 0; k < NTRK; k++) {
        set_slicer(&trk[k], s[k][0], s[k][1], s[k][2], s[k][3]);
        if (solo && k + 1u != solo)
            trk[k].p[P_LEVEL] = 0;
    }
    transport_req = 1;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t i;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
    }
    transport_req = 2;
    fclose(w);
}

static void demos(const char *dir)
{
    static const int16_t DRY[4][4] = {{0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t PAD_GATE[4][4] = {{0, 1, 1, 127}, {SL_GATE, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t PAD_GATE2[4][4] = {{0, 1, 1, 127}, {SL_GATE, 15, 2, 110}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t ACID_STUT[4][4] = {{SL_STUT, 7, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t DRUM_STUT[4][4] = {{0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {SL_STUT, 12, 2, 127}};
    static const int16_t DRUM_GATE[4][4] = {{0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {SL_GATE, 3, 1, 127}};
    static const int16_t ALL[4][4] = {{SL_STUT, 9, 1, 127}, {SL_GATE, 1, 1, 127}, {SL_GATE, 14, 2, 90},
                                      {SL_STUT, 12, 2, 110}};
    demo_song(dir, "song_dry.wav", DRY, 0);
    demo_song(dir, "song_slicers.wav", ALL, 0);
    demo_song(dir, "pad_dry.wav", DRY, 2);
    demo_song(dir, "pad_gate_p1_16th.wav", PAD_GATE, 2);
    demo_song(dir, "pad_gate_p15_32nd.wav", PAD_GATE2, 2);
    demo_song(dir, "acid_dry.wav", DRY, 1);
    demo_song(dir, "acid_stut_p7.wav", ACID_STUT, 1);
    demo_song(dir, "drums_dry.wav", DRY, 4);
    demo_song(dir, "drums_gate_p3.wav", DRUM_GATE, 4);
    demo_song(dir, "drums_stut_p12_32nd.wav", DRUM_STUT, 4);
    printf("slicer: demos in %s (song_dry / song_slicers, pad, acid, drums: dry and sliced)\n", dir);
}

int main(int argc, char **argv)
{
    int bad = 0;
    bad += test_clicks();
    bad += test_timing();
    bad += test_sync();
    bad += test_off_stut();
    bad += test_cost();
    if (argc > 1)
        demos(argv[1]);
    printf("%s\n", bad ? "SLICER TEST FAILED" : "slicer test passed");
    return bad != 0;
}
