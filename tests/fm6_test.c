/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6 engine test (src/eng_fm6.c, src/fm6_core.c) on the Mac, through hostsim.c as regress.c. Renders track 1
 * alone (track_render: no FX, no master). Sample-for-sample agreement with Dexed is tests/fm6_parity.sh's; this
 * test checks FM6 against the DX7 and the engine around it:
 *   build/host/fm6_test [DEMODIR]          (run_tests.sh: build/fm6_demo)
 * 1. SysEx: a DX7 frame waits for the main loop, editor traffic and a second frame do not replace it.
 * 2. algorithms: the 32 against the DX7 diagrams (modulators, carriers, feedback); each sounds, bounded.
 * 3. patches: the 24 factory patches pack to 7-bit DX7 data and back; any 128 bytes unpack into range; a
 *    32-voice bank and a single-voice SysEx made here parse back into the same patches.
 * 4. pitch: A4, ratios, fixed frequencies on any key, detune, transpose.
 * 5. levels: OUTPUT steps, the velocity curve, key level scaling (the DX7's dB).
 * 6. envelopes: attack, decay time, sustain level, release; the voice ends (engine_t.done) after it, and not
 *    while its key is held (Dexed keeps a decayed note keyed).
 * 7. modulation: feedback, the modulation index (Bessel), LFO vibrato and tremolo, the pitch envelope.
 * 8. no DC, no clipping: every factory patch, C1..C7 at velocity 30 and 127.
 * 9. macros: neutral at 0 (the patch's own samples); MLVL, MRAT, FB brighter; MEG keeps it up longer; VMOD
 *    makes velocity count more; DTUN spreads the carriers (beats); ALG puts another algorithm in; PTCH loads its
 *    patch (main loop).
 * 10. voices: POLY takes Dexed's 16, all free some seconds after their release.
 * 11. cost: host instructions per sample and voice with six voices held; fails above FM6_COST_MAX.
 * 12. demos into DEMODIR: every factory preset playing its suggested pattern. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define FM6_COST_MAX 420.0        /* host instructions per sample and voice (MARK I, six carriers) */

static track_t *const T = &trk[0];
static int bad;
static void check(const char *what, int ok)
{
    printf("fm6: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ------------------------------------------------------------ helpers --- */
#define NS (FS * 2)
static double wave[NS];
static uint8_t ED[FP_SIZE + 1u];                          /* the patch under test, put before each note */

static uint8_t *opp(uint32_t n) { return &ED[(6u - n) * FP_OP]; }   /* OP n (1..6) */

static void op_set(uint8_t *v, uint32_t opn, const int *r, const int *l, int ol, int fc)   /* opn 1..6 */
{
    uint8_t *o = v + (6u - opn) * FP_OP;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        o[FP_R1 + i] = (uint8_t)r[i];
        o[FP_L1 + i] = (uint8_t)l[i];
    }
    o[FP_OL] = (uint8_t)ol;
    o[FP_FC] = (uint8_t)fc;
    o[FP_DET] = 7;
    o[FP_BP] = 39;
}

/* track 1 = FM6 with patch v and macros e (0: all 0), POLY, the function settings Dexed's */
static void setup(const uint8_t *v, const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(eng_state, 0, sizeof eng_state);
    memset(fm6_eff, 0, sizeof fm6_eff);
    fm6_fn_reset();
    host_tracks_init();
    host_preset(T, ENGI_FM6, 0);
    for (i = 0; i < 8u; i++)
        T->p[P_E0 + i] = e ? e[i] : 0;
    fm6_put_patch(0, v, 1);
    fm6_slot[0] = (uint8_t)T->p[P_E7];
    T->p[P_VOICE] = V_POLY;
    T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    memcpy(ED, fm6_patch[0], sizeof ED);
}

static void fresh(void)                                  /* track 1 = FM6 with the init voice, nothing else */
{
    uint8_t v[FP_SIZE + 1u];
    fm6_unpack(FM6_INIT, v);
    setup(v, 0);
}

static void note_on(uint32_t note, uint32_t vel)          /* ED as the track's patch (an edit), then the key */
{
    fm6_put_patch(0, ED, 0);
    trk_note_on(T, note, vel);
}

static void render(double *y, uint32_t frames)           /* track 1's dry output, mono */
{
    int32_t b[CTL];
    uint32_t i, k;
    for (i = 0; i < frames; i += CTL) {
        track_render(T, b, CTL);
        for (k = 0; k < CTL && i + k < frames; k++)
            y[i + k] = b[k];
    }
}

/* the voices' sum of note at vel for n samples (from the note-on); release after rel samples */
static void voice_render(uint32_t note, uint32_t vel, double *y, uint32_t n, uint32_t rel)
{
    int32_t b[CTL];
    uint32_t i, k;
    trk_note_on(T, note, vel);
    for (i = 0; i < n; i += CTL) {
        if (i == (rel / CTL) * CTL && rel)
            trk_note_off(T, note);
        track_render(T, b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
}

static double freq(const double *w, uint32_t a, uint32_t b)   /* from the rising zero crossings */
{
    uint32_t i, first = 0, last = 0, n = 0;
    double f0 = 0, f1 = 0;
    for (i = a + 1; i < b; i++)
        if (w[i - 1] < 0 && w[i] >= 0) {
            double x = i - 1 + -w[i - 1] / (w[i] - w[i - 1]);
            if (!n++)
                f0 = x, first = i;
            f1 = x, last = i;
        }
    (void)first;
    (void)last;
    return n > 1 ? (n - 1) * (double)FS / (f1 - f0) : 0;
}

static double peak(const double *w, uint32_t a, uint32_t b)
{
    double p = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        p = fabs(w[i]) > p ? fabs(w[i]) : p;
    return p;
}

static double tone(const double *w, uint32_t a, uint32_t b, double f)   /* amplitude at f (Hann window) */
{
    double re = 0, im = 0;
    uint32_t i, n = b - a;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2 * M_PI * i / n), ph = 2 * M_PI * f * i / FS;
        re += w[a + i] * h * cos(ph);
        im += w[a + i] * h * sin(ph);
    }
    return sqrt(re * re + im * im) * 4 / n;
}

static double db(double x) { return 20 * log10(x); }

static double rms(const double *y, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += y[i] * y[i];
    return sqrt(s / (b - a));
}

#define NFFT 4096
static double centroid(const double *y, uint32_t a)       /* the spectral centroid (Hz) of y[a .. a + NFFT) (Hann) */
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

/* -------------------------------------------------------------- SysEx --- */
static void sysex_pending(void)
{
    static const uint8_t voice[] = {0xF0, 0x43, 0, 0, 1, 0x1B, 42, 0xF7};
    static const uint8_t ping[] = {0xF0, 0x7D, 0x46, 0x4C, 25, 0xF7};
    uint32_t i;
    fm6_rx_ready = fm6_rx_on = 0;
    fm6_rx_n = 0;
    for (i = 0; i < sizeof voice; i++) fm6_sx_byte(voice[i]);
    check("SysEx: a DX7 frame waits for the main loop", fm6_rx_ready && fm6_rx_n == sizeof voice);
    for (i = 0; i < sizeof ping; i++) fm6_sx_byte(ping[i]);
    check("SysEx: editor traffic does not discard a pending DX7 frame",
          fm6_rx_ready && fm6_rx_n == sizeof voice && !memcmp(fm6_rx, voice, sizeof voice));
    fm6_sx_byte(0xF0); fm6_sx_byte(0x43); fm6_sx_byte(0xF7);
    check("SysEx: a second Yamaha frame does not discard the pending frame",
          fm6_rx_ready && fm6_rx_n == sizeof voice && !memcmp(fm6_rx, voice, sizeof voice));
    fm6_rx_ready = 0;                                    /* main loop consumed it */
    fm6_sx_byte(0xF0); fm6_sx_byte(0x43); fm6_sx_byte(0xF8); fm6_sx_byte(0xF7);
    check("SysEx: the next frame after it (a clock byte inside is not part of it)",
          fm6_rx_ready && fm6_rx_n == 3 && fm6_rx[2] == 0xF7);
    fm6_rx_ready = 0;
}

/* --------------------------------------------------------- algorithms --- */
/* each algorithm as the DX7 manual draws it: the operators modulating each operator, the carriers, the operator
 * with feedback (4, 6: a loop through OP6; FM6 feeds OP6 back) */
static const struct { uint8_t mod[7]; uint8_t car, fb; } DIAGRAM[32] = {
    /* mod[n]: bit m = OP m modulates OP n; car: bit n = OP n is a carrier */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 3, 6},                       /* 1 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 3, 2},                       /* 2 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 4, 6},                       /* 3 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 4, 6},                       /* 4 */
    {{0, 1 << 2, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1 | 1 << 3 | 1 << 5, 6},                   /* 5 */
    {{0, 1 << 2, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1 | 1 << 3 | 1 << 5, 6},                   /* 6 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5, 0, 1 << 6, 0}, 1 << 1 | 1 << 3, 6},                   /* 7 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5, 0, 1 << 6, 0}, 1 << 1 | 1 << 3, 4},                   /* 8 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5, 0, 1 << 6, 0}, 1 << 1 | 1 << 3, 2},                   /* 9 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 4, 3},                   /* 10 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 4, 6},                   /* 11 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5 | 1 << 6, 0, 0, 0}, 1 << 1 | 1 << 3, 2},               /* 12 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5 | 1 << 6, 0, 0, 0}, 1 << 1 | 1 << 3, 6},               /* 13 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 3, 6},                   /* 14 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 3, 2},                   /* 15 */
    {{0, 1 << 2 | 1 << 3 | 1 << 5, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1, 6},                   /* 16 */
    {{0, 1 << 2 | 1 << 3 | 1 << 5, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1, 2},                   /* 17 */
    {{0, 1 << 2 | 1 << 3 | 1 << 4, 0, 0, 1 << 5, 1 << 6, 0}, 1 << 1, 3},                   /* 18 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 4 | 1 << 5, 6},              /* 19 */
    {{0, 1 << 3, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 2 | 1 << 4, 3},          /* 20 */
    {{0, 1 << 3, 1 << 3, 0, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 2 | 1 << 4 | 1 << 5, 3},     /* 21 */
    {{0, 1 << 2, 0, 1 << 6, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 3 | 1 << 4 | 1 << 5, 6},     /* 22 */
    {{0, 0, 1 << 3, 0, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 2 | 1 << 4 | 1 << 5, 6},          /* 23 */
    {{0, 0, 0, 1 << 6, 1 << 6, 1 << 6, 0}, 0x3E, 6},                                       /* 24 */
    {{0, 0, 0, 0, 1 << 6, 1 << 6, 0}, 0x3E, 6},                                            /* 25 */
    {{0, 0, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 2 | 1 << 4, 6},               /* 26 */
    {{0, 0, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 2 | 1 << 4, 3},               /* 27 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5, 0, 0}, 1 << 1 | 1 << 3 | 1 << 6, 5},                   /* 28 */
    {{0, 0, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1 | 1 << 2 | 1 << 3 | 1 << 5, 6},               /* 29 */
    {{0, 0, 0, 1 << 4, 1 << 5, 0, 0}, 1 << 1 | 1 << 2 | 1 << 3 | 1 << 6, 5},               /* 30 */
    {{0, 0, 0, 0, 0, 1 << 6, 0}, 0x3E, 6},                                                 /* 31 */
    {{0, 0, 0, 0, 0, 0, 0}, 0x7E, 6},                                                      /* 32 */
};

static void algorithms(void)
{
    uint32_t a, k, ok = 1, sound = 1;
    for (a = 0; a < 32u; a++) {
        uint8_t bus[3] = {0, 0, 0}, mod[7] = {0}, car = 0, fb = 0;
        for (k = 0; k < 6u; k++) {                       /* run the routing on operator sets */
            uint32_t f = FM6_ALG[a][k], op = 6u - k, dst = f & 3u, src = (f >> 4) & 3u;
            mod[op] = src ? bus[src] : 0;
            if (f & 0x40u)
                fb = (uint8_t)op;
            if (!dst)
                car |= (uint8_t)(1u << op);
            else
                bus[dst] = (uint8_t)((f & 4u ? bus[dst] : 0) | 1u << op);
        }
        ok &= !memcmp(mod, DIAGRAM[a].mod, 7) && car == DIAGRAM[a].car && fb == DIAGRAM[a].fb;
        for (k = 1; k <= 6u; k++)                        /* (fm6_carriers: bit 6 - n for OP n) */
            ok &= fm6_carrier(a, k) == (int)((car >> k) & 1u) && ((fm6_carriers(a) >> (6u - k)) & 1u) == ((car >> k) & 1u);
    }
    check("algorithms: the 32 against the DX7 diagrams (modulators, carriers, feedback)", ok);
    for (a = 0; a < 32u; a++) {                          /* every operator at 99, feedback 7, four keys */
        double pk;
        uint32_t n;
        fresh();
        ED[FP_ALG] = (uint8_t)a;
        ED[FP_FB] = 7;
        for (n = 1; n <= 6u; n++) {
            opp(n)[FP_OL] = 99;
            opp(n)[FP_FC] = (uint8_t)n;
        }
        for (k = 0; k < 4u; k++)
            note_on(48 + 7 * k, 127);
        render(wave, FS / 4);
        pk = peak(wave, 0, FS / 4);
        sound &= pk > 1000 && pk <= 4 * 4.0 * VOICE_FS;  /* 16 unit sines a voice: 4 x VOICE_FS */
    }
    check("algorithms: each sounds at full levels and feedback, bounded", sound);
}

/* ----------------------------------------------------------- patches --- */
static uint32_t chk(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0, i;
    for (i = 0; i < n; i++)
        s += p[i];
    return (0x80u - (s & 0x7Fu)) & 0x7Fu;
}

static void patches(void)
{
    uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED], pk2[FM6_PACKED];
    static uint8_t bank[4104], single[163];
    uint32_t i, k, ok = 1, inrange = 1, seed = 12345;
    for (k = 0; k < FM6_NFAC; k++) {
        fm6_factory(k, pk);
        for (i = 0; i < FM6_PACKED; i++)
            ok &= pk[i] < 128u;
        fm6_unpack(pk, v);
        memset(pk2, 0, sizeof pk2);
        fm6_pack(v, pk2);
        ok &= !memcmp(pk, pk2, FM6_PACKED);
    }
    fm6_unpack(FM6_INIT, v);
    fm6_pack(v, pk);
    ok &= !memcmp(pk, FM6_INIT, FM6_PACKED);
    check("patches: the 24 factory patches and the init voice: 7-bit, pack(unpack(x)) == x", ok);
    for (k = 0; k < 2000u; k++) {
        for (i = 0; i < FM6_PACKED; i++) {
            seed = seed * 1103515245u + 12345u;
            pk[i] = (uint8_t)(seed >> 16);
        }
        fm6_unpack(pk, v);
        for (i = 0; i < FP_SIZE; i++)
            inrange &= i >= FP_NAME ? (v[i] >= 32u && v[i] <= 126u) : v[i] <= fm6_max(i);
    }
    check("patches: any 128 bytes unpack into range (2000 random records)", inrange);
    bank[0] = 0xF0; bank[1] = 0x43; bank[2] = 0x00; bank[3] = 0x09; bank[4] = 0x20; bank[5] = 0x00;
    for (k = 0; k < 32u; k++)
        fm6_factory(k % FM6_NFAC, bank + 6 + k * 128u);
    bank[4102] = (uint8_t)chk(bank + 6, 4096);
    bank[4103] = 0xF7;
    ok = (uint32_t)chk(bank + 6, 4096) == bank[4102];
    for (k = 0; k < 32u; k++) {
        fm6_unpack(bank + 6 + k * 128u, v);
        memset(pk, 0, sizeof pk);
        fm6_pack(v, pk);
        fm6_factory(k % FM6_NFAC, pk2);
        ok &= !memcmp(pk, pk2, 128);
    }
    check("patches: a 32-voice bank SysEx: checksum, its 32 records back as they were", ok);
    fm6_factory(3, pk2);
    fm6_unpack(pk2, v);
    single[0] = 0xF0; single[1] = 0x43; single[2] = 0x00; single[3] = 0x00; single[4] = 0x01; single[5] = 0x1B;
    memcpy(single + 6, v, FP_SIZE);
    single[161] = (uint8_t)chk(single + 6, FP_SIZE);
    single[162] = 0xF7;
    memcpy(v, single + 6, FP_SIZE);
    fm6_sanitize(v);
    memset(pk, 0, sizeof pk);
    fm6_pack(v, pk);
    check("patches: a single-voice SysEx (155 bytes): back to the same packed record", !memcmp(pk, pk2, 128) &&
          single[161] == chk(single + 6, FP_SIZE));
}

/* -------------------------------------------------------------- pitch --- */
static void pitch(void)
{
    double f;
    uint32_t ok = 1, note;
    fresh();
    note_on(69, 100);
    render(wave, FS / 2);
    f = freq(wave, 2000, FS / 2);
    check("pitch: A4 at 440 Hz", fabs(f - 440) < 0.2);
    fresh();
    opp(1)[FP_FC] = 3;
    opp(1)[FP_FF] = 17;                                  /* 3 x 1.17 = 3.51 */
    note_on(69, 100);
    render(wave, FS / 2);
    f = freq(wave, 2000, FS / 2);
    check("pitch: ratio 3.51", fabs(f / (440 * 3.51) - 1) < 0.001);
    fresh();
    opp(1)[FP_FC] = 0;                                   /* 0.5 */
    note_on(69, 100);
    render(wave, FS / 2);
    f = freq(wave, 2000, FS / 2);
    check("pitch: ratio 0.5", fabs(f - 220) < 0.2);
    for (note = 40; note <= 80u; note += 40u) {
        fresh();
        opp(1)[FP_MODE] = 1;
        opp(1)[FP_FC] = 2;                               /* 100 Hz at any key */
        note_on(note, 100);
        render(wave, FS / 2);
        f = freq(wave, 2000, FS / 2);
        ok &= fabs(f - 100) < 0.2;
    }
    check("pitch: fixed 100 Hz on notes 40 and 80", ok);
    fresh();
    opp(1)[FP_MODE] = 1;
    opp(1)[FP_FC] = 3;
    opp(1)[FP_FF] = 30;                                  /* 1 kHz x 10^0.3 */
    note_on(60, 100);
    render(wave, FS / 2);
    f = freq(wave, 2000, FS / 2);
    check("pitch: fixed 1995 Hz", fabs(f / 1995.26 - 1) < 0.002);
    fresh();
    opp(1)[FP_DET] = 14;                                 /* +7 */
    note_on(69, 100);
    render(wave, FS / 2);
    f = freq(wave, 2000, FS / 2);
    check("pitch: detune +7 at A4", f > 441.0 && f < 443.5);
    fresh();
    ED[FP_TRNSP] = 36;                                   /* +12 */
    note_on(69, 100);
    render(wave, FS / 2);
    f = freq(wave, 2000, FS / 2);
    check("pitch: transpose +12", fabs(f - 880) < 0.4);
}

/* ------------------------------------------------------------- levels --- */
static void levels(void)
{
    double a99, a90, lo, hi;
    char what[160];
    fresh();
    note_on(69, 100);
    render(wave, FS / 4);
    a99 = peak(wave, FS / 8, FS / 4);
    fresh();
    opp(1)[FP_OL] = 90;
    note_on(69, 100);
    render(wave, FS / 4);
    a90 = peak(wave, FS / 8, FS / 4);
    snprintf(what, sizeof what, "levels: OUTPUT 99 -> 90: %.2f dB (DX7: -6.77)", db(a90 / a99));
    check(what, fabs(db(a90 / a99) + 6.77) < 0.2);
    fresh();
    opp(1)[FP_KVS] = 7;
    note_on(69, 127);
    render(wave, FS / 4);
    hi = peak(wave, FS / 8, FS / 4);
    fresh();
    opp(1)[FP_KVS] = 7;
    note_on(69, 64);
    render(wave, FS / 4);
    lo = peak(wave, FS / 8, FS / 4);
    snprintf(what, sizeof what, "levels: velocity 127 -> 64 at sensitivity 7: %.2f dB (DX7: -15.8)", db(lo / hi));
    check(what, fabs(db(lo / hi) + 15.81) < 0.3);
    fresh();                                             /* -LIN right depth 99 from C4: down above it */
    opp(1)[FP_BP] = 39;
    opp(1)[FP_RD] = 99;
    opp(1)[FP_RC] = 0;
    note_on(84, 100);
    render(wave, FS / 4);
    lo = peak(wave, FS / 8, FS / 4);
    fresh();
    note_on(84, 100);
    render(wave, FS / 4);
    hi = peak(wave, FS / 8, FS / 4);
    /* two octaves over the break point: group (84 - 39 - 17 + 1) / 3 = 9, 9 x 99 x 329 >> 12 = 71 steps of 0.75 dB */
    snprintf(what, sizeof what, "levels: key scaling -LIN 99, two octaves up: %.2f dB", db(lo / hi));
    check(what, fabs(db(lo / hi) + 71 * 32 / 256.0 * 6.0206) < 0.3);
}

/* ---------------------------------------------------------- envelopes --- */
static void envelopes(void)
{
    uint32_t i, t60 = 0;
    double top;
    char what[160];
    fresh();
    opp(1)[FP_R1 + 1] = 50;
    opp(1)[FP_L1 + 1] = 0;                               /* decay from L1 99 to 0 at RATE 50 */
    note_on(69, 100);
    render(wave, NS);
    top = peak(wave, 0, 2048);
    for (i = 0; i + 512u < NS; i += 256u)
        if (peak(wave, i, i + 512u) < top / 1000) {
            t60 = i;
            break;
        }
    snprintf(what, sizeof what, "envelopes: RATE 50 decay to -60 dB: %.3f s (0.93)", (double)t60 / FS);
    check(what, t60 > FS * 85 / 100 && t60 < FS * 103 / 100);
    fresh();
    opp(1)[FP_R1 + 3] = 60;
    note_on(69, 100);
    render(wave, FS / 4);
    trk_note_off(T, 69);
    for (i = 0; i < FS * 3u && T->v[0].active; i += CTL)
        render(wave, CTL);
    snprintf(what, sizeof what, "envelopes: RATE 60 release ends the voice: %.3f s", (double)i / FS);
    check(what, !T->v[0].active && i > FS / 4 && i < FS / 2);
    fresh();                                             /* the attack: RATE 99 at the top within 3 ms */
    note_on(69, 100);
    render(wave, FS / 8);
    check("envelopes: RATE 99 attack", peak(wave, 0, FS * 3 / 1000) > 0.9 * peak(wave, FS / 16, FS / 8));
    fresh();                                             /* a held L3 holds */
    opp(1)[FP_R1 + 1] = 70;
    opp(1)[FP_L1 + 1] = 80;
    opp(1)[FP_L1 + 2] = 80;
    note_on(69, 100);
    render(wave, NS);
    snprintf(what, sizeof what, "envelopes: sustain at L3 80: %.2f dB", db(peak(wave, FS, NS) / top));
    check(what, fabs(db(peak(wave, FS, FS * 3 / 2) / peak(wave, FS * 3 / 2, NS))) < 0.1 &&
                fabs(db(peak(wave, FS, NS) / top) + 9 * 64 / 256.0 * 6.0206) < 0.3);
    fresh();                                             /* L3 0: decayed away, but keyed while held (as Dexed) */
    opp(1)[FP_R1 + 1] = 70;
    opp(1)[FP_L1 + 1] = 0;
    opp(1)[FP_L1 + 2] = 0;
    note_on(69, 100);
    render(wave, FS * 2u);
    top = T->v[0].active;
    trk_note_off(T, 69);
    for (i = 0; i < FS && T->v[0].active; i += CTL)
        render(wave, CTL);
    check("envelopes: a note decayed to L3 0 stays keyed while held (Dexed's), ends after its release",
          top && !T->v[0].active);
}

/* --------------------------------------------------------- modulation --- */
static void modulation(void)
{
    double h0, h7, f, lo, hi;
    uint32_t i;
    char what[200];
    fresh();                                             /* OP6 alone (algorithm 32 feeds it back) */
    ED[FP_ALG] = 31;
    opp(1)[FP_OL] = 0;
    opp(6)[FP_OL] = 99;
    opp(6)[FP_L1 + 3] = 0;
    note_on(57, 100);
    render(wave, FS / 2);
    h0 = tone(wave, FS / 8, FS / 2, 440) / tone(wave, FS / 8, FS / 2, 220);
    fresh();
    ED[FP_ALG] = 31;
    ED[FP_FB] = 7;
    opp(1)[FP_OL] = 0;
    opp(6)[FP_OL] = 99;
    note_on(57, 100);
    render(wave, FS / 2);
    h7 = tone(wave, FS / 8, FS / 2, 440) / tone(wave, FS / 8, FS / 2, 220);
    snprintf(what, sizeof what, "modulation: feedback 0 / 7: 2nd harmonic %.1f / %.1f dB", db(h0), db(h7));
    check(what, db(h0) < -60 && db(h7) > -12);
    fresh();                                             /* OP2 -> OP1 at 1:1: brighter with OP2's level */
    opp(2)[FP_OL] = 70;
    note_on(57, 100);
    render(wave, FS / 2);
    lo = tone(wave, FS / 8, FS / 2, 440) / tone(wave, FS / 8, FS / 2, 220);
    fresh();
    opp(2)[FP_OL] = 85;
    note_on(57, 100);
    render(wave, FS / 2);
    hi = tone(wave, FS / 8, FS / 2, 440) / tone(wave, FS / 8, FS / 2, 220);
    {   /* OUTPUT 70 at the top of its envelope: 2^(2912 / 256 - 14) cycles of phase, a 1.02 rad index;
         * OUTPUT 85: 3392, 3.74 rad. A 1:1 pair puts J1 + J3 at 2f, J0 - J2 at f */
        double b70 = 6.283185307179586 * pow(2, 2912 / 256.0 - 14), b85 = 6.283185307179586 * pow(2, 3392 / 256.0 - 14);
        double w70 = fabs((jn(1, b70) + jn(3, b70)) / (jn(0, b70) - jn(2, b70)));
        double w85 = fabs((jn(1, b85) + jn(3, b85)) / (jn(0, b85) - jn(2, b85)));
        snprintf(what, sizeof what, "modulation: index: 2f/f %.2f dB at OUTPUT 70 (Bessel %.2f), %.2f dB at 85 (%.2f)",
                 db(lo), db(w70), db(hi), db(w85));
        check(what, fabs(db(lo) - db(w70)) < 0.3 && fabs(db(hi) - db(w85)) < 0.5);
    }
    fresh();                                             /* vibrato: PMD 99, PMS 3, no delay */
    ED[FP_LFS] = 35;
    ED[FP_LPMD] = 99;
    ED[FP_LPMS] = 3;
    ED[FP_LFW] = 4;
    note_on(69, 100);
    render(wave, NS);
    lo = 1e9;
    hi = 0;
    for (i = FS / 4; i + 1024u < NS; i += 512u) {
        f = freq(wave, i, i + 1024u);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }
    /* PMD 99 x PMS 3: 255 x 33 x 2^47 >> 39 = 0.128 octave each way */
    snprintf(what, sizeof what, "modulation: LFO vibrato: %.1f .. %.1f Hz", lo, hi);
    check(what, hi / lo > 1.05 && hi / lo < pow(2, 2 * 0.128) + 0.01);
    fresh();                                             /* tremolo: AMD 99 on AMS 3 */
    ED[FP_LFS] = 35;
    ED[FP_LAMD] = 99;
    ED[FP_LFW] = 0;
    opp(1)[FP_AMS] = 3;
    note_on(69, 100);
    render(wave, NS);
    lo = 1e9;
    hi = 0;
    for (i = FS / 4; i + 256u < NS; i += 256u) {
        f = peak(wave, i, i + 256u);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }
    snprintf(what, sizeof what, "modulation: LFO tremolo (AMS 3): %.1f dB", db(lo / hi));
    check(what, db(lo / hi) < -12);
    fresh();                                             /* pitch envelope: from +1 oct (L4, L1) back to the note */
    ED[FP_PL1 + 3] = 82;                                 /* +32/32: an octave; the envelope starts at L4 */
    ED[FP_PL1 + 0] = 82;
    ED[FP_PR1 + 1] = 70;                                 /* back down in 0.27 s */
    ED[FP_PL1 + 1] = 50;
    ED[FP_PL1 + 2] = 50;
    note_on(57, 100);
    render(wave, FS);
    f = freq(wave, 0, 400);
    check("modulation: pitch envelope at L4 / L1 82: an octave up", f > 440 * 0.97 && f < 440 * 1.001);
    f = freq(wave, FS / 2, FS);
    check("modulation: pitch envelope back to the note", fabs(f - 220) < 0.3);
}

/* ------------------------------------------------------- DC, clipping --- */
static void dc_clip(void)
{
    static double y[FS];
    uint32_t pi, note, v, ok = 1, okdc = 1;
    double worst = 0, wdc = 0, lim;
    uint8_t pk[FM6_PACKED], p[FP_SIZE + 1u];
    for (pi = 0; pi < FM6_NFAC; pi++)
        for (note = 24; note <= 96; note += 12)
            for (v = 0; v < 2u; v++) {
                double s = 0, m = 0;
                uint32_t i;
                fm6_factory(pi, pk);
                fm6_unpack(pk, p);
                setup(p, 0);
                voice_render(note, v ? 127 : 30, y, FS, FS * 6u / 10u);
                for (i = 0; i < FS; i++) {
                    s += y[i];
                    m = fabs(y[i]) > m ? fabs(y[i]) : m;
                }
                lim = 2.0 * VOICE_FS;
                ok &= m < lim;
                if (m >= lim)
                    printf("fm6:   %s note %u vel %u: peak %.0f\n", ENG_FM6.presets[pi].name, note, v ? 127 : 30, m);
                okdc &= fabs(s / FS) < 0.01 * VOICE_FS;
                worst = m > worst ? m : worst;
                wdc = fabs(s / FS) > wdc ? fabs(s / FS) : wdc;
            }
    printf("fm6: factory patches C1..C7, velocity 30 / 127: peak %.0f (VOICE_FS %d), |mean| at most %.1f\n", worst,
           VOICE_FS, wdc);
    check("no clipping: every factory patch, C1..C7, velocity 30 and 127: a voice below 2 x VOICE_FS", ok);
    check("no DC: |mean| below 1 % of VOICE_FS", okdc);
}

/* ------------------------------------------------------------- macros --- */
static uint8_t base[FP_SIZE + 1u];

static double centroid_of(const uint8_t *v, const int16_t *e, uint32_t vel, uint32_t at)
{
    static double y[FS];
    setup(v, e);
    voice_render(48, vel, y, at + NFFT, 0);
    return centroid(y, at);
}

static void macros(void)
{
    static const int16_t E0[8] = {0};
    int16_t e[8];
    double c0, c1, c2;
    {   /* neutral: the macros at 0 leave the patch as it is (the samples Dexed renders) */
        static double y0[FS / 2], y1[FS / 2];
        uint8_t pk[FM6_PACKED];
        fm6_factory(9, pk);
        fm6_unpack(pk, base);
        setup(base, E0);
        voice_render(60, 100, y0, FS / 2, FS / 4);
        fm6_eff_build(T);
        check("macros at 0: the patch as it is", !memcmp(fm6_eff[0].p, base, FP_SIZE) && !fm6_eff[0].dt[0]);
        setup(base, E0);
        voice_render(60, 100, y1, FS / 2, FS / 4);
        check("macros at 0: a note from silence renders the same samples twice", !memcmp(y0, y1, sizeof y0));
    }
    {   /* a plain two-operator voice (algorithm 1: OP2 -> OP1), the others off: the macros' effect is clear */
        static const int RC[4] = {99, 30, 30, 50}, LC[4] = {99, 95, 90, 0}, RM[4] = {99, 45, 30, 50},
                         LM[4] = {99, 80, 60, 0}, Z[4] = {99, 99, 99, 99}, LZ[4] = {0, 0, 0, 0};
        uint32_t k;
        fm6_unpack(FM6_INIT, base);
        op_set(base, 1, RC, LC, 99, 1);
        op_set(base, 2, RM, LM, 70, 1);
        base[(6u - 2u) * FP_OP + FP_KVS] = 2;
        for (k = 3; k <= 6u; k++)
            op_set(base, k, Z, LZ, 0, 1);
        base[FP_ALG] = 0;
    }
    c0 = centroid_of(base, E0, 100, FS / 10);
    memcpy(e, E0, sizeof e); e[2] = 40;
    c1 = centroid_of(base, e, 100, FS / 10);
    e[2] = -40;
    c2 = centroid_of(base, e, 100, FS / 10);
    printf("fm6: MLVL -40 / 0 / +40: centroid %.0f / %.0f / %.0f Hz\n", c2, c0, c1);
    check("MLVL: + brighter, - darker", c1 > c0 * 1.1 && c2 < c0 * 0.95);
    memcpy(e, E0, sizeof e); e[3] = 3;
    c1 = centroid_of(base, e, 100, FS / 10);
    printf("fm6: MRAT 0 / +3: centroid %.0f / %.0f Hz\n", c0, c1);
    check("MRAT: + raises the modulators' ratios (brighter)", c1 > c0 * 1.1);
    {   /* MEG: slower modulator envelopes keep the brightness later */
        double l0, l1;
        l0 = centroid_of(base, E0, 100, FS / 2);
        memcpy(e, E0, sizeof e); e[4] = 50;
        l1 = centroid_of(base, e, 100, FS / 2);
        printf("fm6: MEG 0 / +50: centroid at 0.5 s %.0f / %.0f Hz\n", l0, l1);
        check("MEG: + the modulators decay slower (brighter later)", l1 > l0 * 1.1);
    }
    {   /* VMOD: velocity changes the brightness more */
        double s0 = centroid_of(base, E0, 127, FS / 10) / centroid_of(base, E0, 40, FS / 10);
        memcpy(e, E0, sizeof e); e[5] = 5;
        c1 = centroid_of(base, e, 127, FS / 10) / centroid_of(base, e, 40, FS / 10);
        printf("fm6: VMOD 0 / +5: centroid ratio vel 127 / 40 %.2f / %.2f\n", s0, c1);
        check("VMOD: + velocity moves the brightness more", c1 > s0 * 1.05);
    }
    {   /* FB on the organ's feedback operator (alg 32: OP6) */
        static const int R[4] = {99, 99, 99, 99}, L[4] = {99, 99, 99, 0}, LZ[4] = {0, 0, 0, 0};
        uint8_t v[FP_SIZE + 1u];
        uint32_t k;
        fm6_unpack(FM6_INIT, v);
        for (k = 1; k <= 6u; k++)
            op_set(v, k, R, k == 6u ? L : LZ, k == 6u ? 99 : 0, 1);
        v[FP_ALG] = 31;
        c0 = centroid_of(v, E0, 100, FS / 10);
        memcpy(e, E0, sizeof e); e[1] = 6;
        c1 = centroid_of(v, e, 100, FS / 10);
        printf("fm6: FB 0 / +6 (OP6 alone): centroid %.0f / %.0f Hz\n", c0, c1);
        check("FB: + more feedback (brighter)", c1 > c0 * 1.05);
    }
    {   /* ALG: 32 (six carriers) instead of the patch's */
        uint32_t a32, apat;
        memcpy(e, E0, sizeof e); e[0] = 32;
        setup(base, e);
        fm6_eff_build(T);
        a32 = fm6_eff[0].p[FP_ALG];
        setup(base, E0);
        fm6_eff_build(T);
        apat = fm6_eff[0].p[FP_ALG];
        check("ALG: 1..32 replaces the patch's algorithm, PAT (0) keeps it", a32 == 31u && apat == base[FP_ALG]);
    }
    {   /* DTUN: the carriers apart -> beating: the envelope of the sum varies more */
        static double y[FS];
        uint32_t i, k;
        static const int R[4] = {99, 99, 99, 99}, L[4] = {99, 99, 99, 0};
        uint8_t v[FP_SIZE + 1u];
        double m[2];
        fm6_unpack(FM6_INIT, v);                         /* algorithm 32: OP1..OP3 carriers at the same ratio */
        for (k = 1; k <= 6u; k++)
            op_set(v, k, R, L, k <= 3u ? 90 : 0, 1);
        v[FP_ALG] = 31;
        for (k = 0; k < 2u; k++) {
            double lo = 1e18, hi = 0;
            memcpy(e, E0, sizeof e); e[6] = k ? 127 : 0;
            setup(v, e);
            voice_render(48, 100, y, FS, 0);
            for (i = FS / 4; i + 2048 <= FS; i += 2048) {
                double r = rms(y, i, i + 2048);
                lo = r < lo ? r : lo;
                hi = r > hi ? r : hi;
            }
            m[k] = hi / lo;
        }
        printf("fm6: DTUN 0 / 127 (3 carriers): level swing over 50 ms windows %.3f / %.3f\n", m[0], m[1]);
        check("DTUN: the carriers apart: they beat", m[1] > m[0] * 1.05);
    }
    {   /* PTCH: the main loop loads the slot's patch */
        setup(base, 0);
        T->p[P_E7] = 4;
        fm6_poll();
        check("PTCH F5 loads the fifth factory patch (main loop)", fm6_slot[0] == 4u &&
              !memcmp(fm6_patch[0] + FP_NAME, "SOFT PAD  ", 10));
        T->p[P_E7] = 8;
        fm6_poll();
        check("PTCH F9 loads Melodee's first factory voice", !memcmp(fm6_patch[0] + FP_NAME, "TINE EP   ", 10));
        T->p[P_E7] = (int16_t)(FM6_NFAC + 3);
        fm6_poll();
        check("PTCH B4 of an empty bank: the init voice", !memcmp(fm6_patch[0] + FP_NAME, "INIT VOICE", 10));
    }
}

/* ------------------------------------------------------------- voices --- */
static void voices(void)
{
    uint32_t i, n = 0;
    int32_t b[CTL];
    uint8_t pk[FM6_PACKED];
    fm6_factory(4, pk);
    fm6_unpack(pk, base);
    setup(base, 0);
    for (i = 0; i < 18u; i++)
        trk_note_on(T, 40 + 2 * i, 100);
    track_render(T, b, CTL);
    for (i = 0; i < NVOICE; i++)
        n += T->v[i].active;
    check("voices: 18 keys in POLY: 16 voices (Dexed's), each one budget unit", n == 16u && ENG_FM6.poly == 16u &&
          voices_busy() == 16u);
    for (i = 0; i < 18u; i++)
        trk_note_off(T, 40 + 2 * i);
    for (i = 0; i < FS * 8u / CTL; i++)
        track_render(T, b, CTL);
    for (i = 0, n = 0; i < NVOICE; i++)
        n += T->v[i].active;
    check("voices: all free some seconds after the release (the operator envelopes end them)", n == 0);
}

/* --------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static double mix_cost(const uint8_t *v, uint32_t notes, uint32_t eng)
{
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS * 2u / CTL;
    uint64_t i0;
    setup(v, 0);
    fm6_fn[0][FN_ENGINE] = (uint8_t)eng;
    for (i = 0; i < notes; i++)
        trk_note_on(T, 48 + 3 * i, 100);
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (i = 0; i < nb; i++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (nb * CTL);
}

static void cost(void)
{
    uint8_t heavy[FP_SIZE + 1u], pk[FM6_PACKED];
    double idle, c, worst = 0;
    uint32_t pi, k, eng;
    char what[220];
    static const char *const ENG[3] = {"MODERN", "MARK I", "OPL"};
    if (!instr_now()) {
        printf("fm6: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    fm6_unpack(FM6_INIT, heavy);                         /* alg 32, six carriers at full, sustained, feedback 7 */
    for (k = 1; k <= 6u; k++) {
        static const int R[4] = {99, 99, 99, 99}, L[4] = {99, 99, 99, 0};
        op_set(heavy, k, R, L, 99, (int)k);
    }
    heavy[FP_ALG] = 31;
    heavy[FP_FB] = 7;
    printf("fm6: cost, host instructions per sample and voice (6 voices held):\n");
    for (eng = 0; eng < 3u; eng++) {
        idle = mix_cost(heavy, 0, eng);
        for (pi = 0; pi <= FM6_NFAC; pi++) {
            uint8_t v[FP_SIZE + 1u];
            if (pi < FM6_NFAC) {
                fm6_factory(pi, pk);
                fm6_unpack(pk, v);
            }
            c = (mix_cost(pi < FM6_NFAC ? v : heavy, 6, eng) - idle) / 6;
            if (pi == FM6_NFAC || eng == FM6_MARK1)
                printf("fm6:   %-7s %-12s %5.0f\n", ENG[eng], pi < FM6_NFAC ? ENG_FM6.presets[pi].name : "(6 carriers)", c);
            worst = c > worst ? c : worst;
        }
    }
    fm6_fn_reset();
    snprintf(what, sizeof what, "cost: at most %.0f host instructions a sample and voice (limit %.0f)", worst,
             FM6_COST_MAX);
    check(what, worst <= FM6_COST_MAX);
}

/* -------------------------------------------------------------- demos --- */
static void demo(const char *dir, uint32_t pi)
{
    static int32_t o[2 * CTL];
    const preset_t *pr = &ENG_FM6.presets[pi];
    uint32_t pat = pr->pat ? pr->pat - 1u : 4u, step = FS / 8u, s, i, held = 0, frames = 0;
    char path[512], nm[32];
    FILE *w;
    for (i = 0; pr->name[i] && i < 31u; i++)
        nm[i] = pr->name[i] == ' ' ? '_' : pr->name[i];
    nm[i] = 0;
    snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, pi, nm);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, 0);
    memset(trk, 0, sizeof trk);
    memset(eng_state, 0, sizeof eng_state);
    fm6_fn_reset();
    host_tracks_init();
    host_preset(T, ENGI_FM6, pi);
    for (s = 0; s < 48u; s++) {                       /* three times through the pattern's 16 steps, 120 BPM 1/16 */
        uint32_t k = s % 16u, note = PATTERNS[pat].note[k], fl = PATTERNS[pat].flags[k];
        if (!(fl & 4u) && held) {
            trk_note_off(T, held);
            held = 0;
        }
        if (note && !(fl & 4u)) {
            trk_note_on(T, note, fl & 1u ? 120 : 90);
            held = note;
        }
        for (i = 0; i < step; i += CTL) {
            uint32_t j;
            mix_block(o, CTL);
            for (j = 0; j < CTL; j++)
                wav_put(w, o[2 * j], o[2 * j + 1]);
            frames += CTL;
        }
    }
    if (held)
        trk_note_off(T, held);
    for (i = 0; i < FS * 2u; i += CTL) {
        uint32_t j;
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
        frames += CTL;
    }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
}

int main(int argc, char **argv)
{
    uint32_t pi;
    sysex_pending();
    algorithms();
    patches();
    pitch();
    levels();
    envelopes();
    modulation();
    dc_clip();
    macros();
    voices();
    if (!getenv("NOCOST"))
        cost();
    if (argc > 1) {
        for (pi = 0; pi < ENG_FM6.npresets; pi++)
            demo(argv[1], pi);
        printf("fm6: demos in %s: the %u presets, each playing its suggested pattern\n", argv[1],
               (uint32_t)ENG_FM6.npresets);
    }
    printf("fm6_test: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
