/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of SWING (issue #31), same sources as the firmware (through hostsim.c):
 *   build/host/swing_test
 * 1. the sum: a track's SWG plus the global SWG is at most 100 (core.h track_swing).
 * 2. the sequencer: with both at 100 the steps play 1.4 x / 0.6 x the straight length (not 1.8 x /
 *    0.2 x), measured from the transport; with the sum at 100 or below nothing changes.
 * 3. the SLICER: its step lengths are the sequencer's (one helper), and it stays on the sequencer's
 *    step at the extremes.
 * 4. the display: SWG (0..100) shows its value, 100 is "100" (not 79); 0..127 percents unchanged. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int check(const char *what, int ok)
{
    printf("swing: %-75s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void set_swing(track_t *t, int own, int global)
{
    t->p[P_SSWING] = (int16_t)own;
    song.g[G_SWING] = (int16_t)global;
}

/* ------------------------------------------------------------ 1. sum --- */
static int test_sum(void)
{
    static const int C[][3] = {{0, 0, 0}, {30, 20, 50}, {60, 40, 100}, {100, 0, 100}, {0, 100, 100},
                               {70, 50, 100}, {100, 100, 100}};
    uint32_t k, bad = 0;
    host_tracks_init();
    for (k = 0; k < sizeof C / sizeof C[0]; k++) {
        set_swing(&trk[0], C[k][0], C[k][1]);
        bad += track_swing(&trk[0]) != C[k][2];
    }
    return check("track SWG + global SWG, at most 100 (0+0 30+20 60+40 100+0 0+100 70+50 100+100)", !bad);
}

/* ----------------------------------------------------- 2. sequencer --- */
/* the sample where each step of track 1 starts (block exact: CTL), n steps from the transport start */
static uint32_t seq_starts(uint32_t *at, uint32_t n)
{
    uint32_t f, got = 0, last = 0xFFFFFFFFu;
    transport_req = 1;
    for (f = 0; got < n && f < 20u * FS; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        if (trk[0].seq_idx != last) {
            last = trk[0].seq_idx;
            at[got++] = f;
        }
    }
    transport_req = 2;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    return got;
}

static int test_seq(void)
{
    static const int C[][3] = {{100, 100, 100}, {100, 0, 100}, {0, 100, 100}, {30, 20, 50}, {0, 0, 0}};
    uint32_t c, k, at[17];
    int bad = 0;
    char what[160];
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        uint32_t base, longs = 0, shorts = 0, err = 0, got;
        host_tracks_init();
        song.g[G_BPM] = 120;
        trk[0].p[P_SLEN] = 16;
        set_swing(&trk[0], C[c][0], C[c][1]);
        base = div_samples((uint32_t)trk[0].p[P_SDIV]);
        got = seq_starts(at, 17);
        for (k = 1; k < got; k++) {
            uint32_t len = at[k] - at[k - 1], idx = (k - 1u) & 1u;     /* step 0 is even (long) */
            int32_t want = (int32_t)base + ((idx ? -1 : 1) * C[c][2] * (int32_t)base / 250);
            err += abs((int32_t)len - want) > CTL;
            if (idx)
                shorts += len;
            else
                longs += len;
        }
        snprintf(what, sizeof what, "sequencer: SWG %d + global %d -> %d: steps %.2f x / %.2f x, pairs = 2 steps",
                 C[c][0], C[c][1], C[c][2], (double)longs / 8.0 / base, (double)shorts / 8.0 / base);
        bad += check(what, got == 17 && !err && abs((int32_t)(longs + shorts) - 16 * (int32_t)base) <= (int32_t)CTL);
    }
    /* the extremes: 1.4 x and 0.6 x, never past */
    host_tracks_init();
    set_swing(&trk[0], 100, 100);
    bad += check("step_samples at 100 + 100: 1.4 x and 0.6 x of the straight step (was 1.8 x / 0.2 x)",
                 step_samples(&trk[0], 6000, 0) == 8400 && step_samples(&trk[0], 6000, 1) == 3600);
    return bad;
}

/* --------------------------------------------------------- 3. SLICER --- */
static int test_slicer(void)
{
    static const int C[][2] = {{100, 100}, {100, 0}, {0, 100}, {80, 70}, {0, 0}};
    uint32_t c, k, f, miss = 0, lens = 0, blocks = 0;
    int bad = 0;
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        sl_t s;
        host_tracks_init();
        song.g[G_BPM] = 97;
        set_swing(&trk[0], C[c][0], C[c][1]);
        trk[0].p[P_SLRATE] = 1;
        memset(&s, 0, sizeof s);
        s.idx = 15;
        for (k = 0; k < 32u; k++) {
            sl_enter(&trk[0], &s);
            lens += s.len != step_samples(&trk[0], s.base, s.idx);
        }
    }
    bad += check("SLICER step lengths == the sequencer's (one helper) at 100+100, 100+0, 0+100, 80+70, 0+0", !lens);

    host_tracks_init();
    song.g[G_BPM] = 131;
    for (k = 0; k < NTRK; k++) {                     /* every track past 100 in sum, the SLICER on */
        trk[k].p[P_SLEN] = 16;
        trk[k].p[P_SLRATE] = 1;
        trk[k].p[P_SLCR] = SL_GATE;
        trk[k].p[P_SLPAT] = (int16_t)(3u + k);
        trk[k].p[P_SLDEPTH] = 127;
        trk[k].p[P_SSWING] = (int16_t)(40 + 20 * k);
    }
    song.g[G_SWING] = 100;
    memset(sl, 0, sizeof sl);
    transport_req = 1;
    for (f = 0; f < 8u * FS; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        blocks++;
        for (k = 0; k < NTRK; k++) {                 /* the SLICER step that holds the block's first sample */
            uint32_t idx = sl[k].pos >= CTL ? sl[k].idx : (sl[k].idx + 15u) & 15u;
            miss += idx != trk[k].seq_idx % 16u;
        }
    }
    transport_req = 2;
    bad += check("SLICER on the sequencer's step, SWG 40..100 + global 100, 131 BPM, 4 tracks", !miss && blocks > 1000u);
    return bad;
}

/* -------------------------------------------------------- 4. display --- */
static const char *shown(const param_desc_t *d, int32_t v)
{
    static char val[12];
    const char *unit;
    param_format(d, v, val, &unit);
    return val;
}

static int test_display(void)
{
    int ok = 1;
    const param_desc_t *sw[3] = {&TP[P_SSWING], &GP[G_SWING], &TP[P_ASWING]};
    uint32_t k;
    for (k = 0; k < 3u; k++)
        ok &= sw[k]->fmt == F_PCT && sw[k]->min == 0 && sw[k]->max == 100 && !strcmp(shown(sw[k], 100), "100") &&
              !strcmp(shown(sw[k], 50), "50") && !strcmp(shown(sw[k], 0), "0");
    ok &= !strcmp(shown(&TP[P_SUS], 127), "100") && !strcmp(shown(&TP[P_SUS], 64), "50") &&
          !strcmp(shown(&TP[P_DLY], 0), "0");
    return check("display: SWG / global SWG / ARP SWG 100 -> 100 %, 50 -> 50 %; 0..127 percents unchanged", ok);
}

int main(void)
{
    int bad = test_sum() + test_seq() + test_slicer() + test_display();
    printf("swing: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
