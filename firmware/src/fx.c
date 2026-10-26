/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). Mono buses, stereo dry mix.
 * ChoralRoot 0.19: the chorus returns in stereo in ChoralRoot's unit (cr_fx.c xc_block; this file's one tap is
 * Felucca's, for its builds and goldens). MASTER is read before the audio starts (fx_master_now, master.c: Felucca
 * 1.1.5's fix). Options > Output's Clean chain (clean_block). Its limiter's look-ahead, one audio block with the
 * gain a straight line across the block, is the design of SLOOP 2.6's master (github.com/isod89/sloop-fm1,
 * firmware/src/fx.c lim_block; GPL-3.0-only, a fork of Felucca): the design, written anew, no code copied
 * (LICENSING.md, docs/AUDIO-QUALITY.md "The look-ahead"). */
#ifndef CR_FX
#define CR_FX 0                  /* 1 (choralroot.c, the emulator): the delay and the reverb are ChoralRoot's (cr_fx.c,
                                  * docs/FX.md: a stereo delay and a large reverb at half the rate, their parameters
                                  * global); 0: Felucca's below, as its golden renders have them */
#endif
#ifndef FELUCCA_PERFORM
#define FELUCCA_PERFORM 1        /* 0 (choralroot.c, the emulator): no FX hold layer (perform.c). Its keys are seq.c's
                                  * (keyboard_block: perf_press) and its knobs, solo and kill ui_layer.c's and
                                  * ui_input.c's, none of them in a unit without Felucca's sequencer and UI
                                  * (FELUCCA_SEQ 0): there perf_begin could only ever return 0 and pf.mute stay 0, so
                                  * the mix is the same without the calls, and perform.c leaves the image */
#endif
#if !FELUCCA_PERFORM && defined(FELUCCA_SEQ) && FELUCCA_SEQ
#error "FELUCCA_PERFORM 0 needs FELUCCA_SEQ 0 (seq.c and ui_layer.c drive perform.c)"
#endif
#define CHO_LEN 2048u
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
#ifndef CR_CHORUS_MONO
#define CR_CHORUS_MONO 0         /* ChoralRoot's unit (CR_FX 1), a TEST switch: 0, the unit: the chorus returns in stereo
                                  * (cr_fx.c xc_block, 0.19); 1: 0.18's return, one tap to both sides; 2 (the emulator):
                                  * stereo unless cho_mono is set (EMU_CHORUS=mono: tools/emu/classic_exact.sh compares
                                  * Classic with 0.18 bit for bit on scripts that have a chorus send) */
#endif
#if CR_CHORUS_MONO
static uint8_t cho_mono;
#endif
#if !CR_FX
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
#endif
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
    uint16_t dly_dl, dly_old, dly_xf;    /* DELAY: the tap in use, the one it fades from, samples left of the fade */
} fx;
#define DLY_XF 512u                      /* fx_smooth: a new delay time (TIME, the tempo, a MIDI clock) crossfades in
                                          * 11.6 ms: the read index jumping was a click on every tempo step while it rang */

/* SPRING (G_RTYPE 1): one spring of a spring tank, mono like the other buses, in the ROOM's own buffers (no
 * RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass) ->
 * SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#if !CR_FX
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");
#endif

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain (straight into tanh over the full band, it would sound like a
 * broken digital fuzz). State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
#if FELUCCA_QUAD
    d = quad_drive(t, d);                               /* eng_quad.c: FM TONE's LFO -> DRIVE */
#endif
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* ChoralRoot (cr_out.c sets it; 0 in Felucca, its golden renders and reverb_test's bit-exact buses): the delay's
 * new times crossfade (DLY_XF), and the limiter is smoothed. A 6-note chord with the bass keeps the limiter
 * working (pre-limiter peaks ~2x LIM_T on ANALOG / WHEEL sounds, 60 % of the samples limited), and its 4-sample
 * attack re-attacking on every new peak of the beating chord modulated the gain at audio rate: -49 dB of
 * sidebands above 1 kHz, a crackle on held chords, louder with the FX sends. Smoothed (lim_g): -57 dB, the same
 * loudness; the peaks the slower gain lets through (to ~25000) go into the soft clipper's near-linear part */
static uint8_t fx_smooth;
#if CR_FX
static volatile uint8_t fx_off;       /* ChoralRoot's FX tap at off (cr_ui.c cu_fx_apply): no part sends into the buses */
static int32_t fx_sg[NPART][3];       /* each part's send gains as the last block left them (Q15: chorus, delay, reverb):
                                       * a new send, FX on / off, a CC is reached as a ramp over the block, not as a step
                                       * in what the delay and the reverb are fed (a click at every detent of Send) */
#endif
static int32_t lim_g = 32768;
/* Options > USB Level = Fixed (Felucca 1.0.5's MENU > USB LEVEL FIXED, #42: record over USB with the speaker turned
 * down): the mix goes to master_out at the full MASTER level, USB audio's master pair takes that (choralroot.c's
 * mix_block shim, usb_audio_stream.c), and only then does MASTER scale what the DAC gets (usb_fixed_dac, audio.c).
 * 0 (Master, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
static uint8_t fx_out_blk;              /* Options > Output: 1, this block went through the Clean chain (below) */
static void clean_post(int32_t *out, uint32_t n);
CR_HOT_MASTER static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after the USB tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    if (fx_out_blk) {                               /* Output Clean: MASTER into the 24-bit word */
        clean_post(out, n);
        return;
    }
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings: 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the small speaker):
                                        * 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+: what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is clipped at its
 * own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and added.
 * The low-pass has 4 poles (Felucca 1.0.5, #42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself,
 * late, and cancelled up to 6 dB of it; now under 0.5 dB) */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
enum { SB_LP1, SB_LP2, SB_LP3, SB_LP4, SB_ENV, SB_H1, SB_H2, SB_HL, SB_N };   /* (the states above, in a block's copy) */
static inline int32_t spk_bass(int32_t *sb, int32_t m)
{
    int32_t a, t, u;
    sb[SB_LP1] += ((m - sb[SB_LP1]) * 692) >> 15;
    sb[SB_LP2] += ((sb[SB_LP1] - sb[SB_LP2]) * 692) >> 15;
    sb[SB_LP3] += ((sb[SB_LP2] - sb[SB_LP3]) * 692) >> 15;
    sb[SB_LP4] += ((sb[SB_LP3] - sb[SB_LP4]) * 692) >> 15;
    a = sb[SB_LP4] < 0 ? -sb[SB_LP4] : sb[SB_LP4];
    if (a > sb[SB_ENV])
        sb[SB_ENV] += (a - sb[SB_ENV]) >> 2;
    else if (sb[SB_ENV] > 0)
        sb[SB_ENV] -= (sb[SB_ENV] >> 11) + 1;
    t = clamp(sb[SB_LP4] * 8, -sb[SB_ENV], sb[SB_ENV]);
    sb[SB_H1] += (t - sb[SB_H1]) >> 5;
    u = t - sb[SB_H1];
    sb[SB_H2] += (u - sb[SB_H2]) >> 5;
    u -= sb[SB_H2];
    sb[SB_HL] += (u - sb[SB_HL]) >> 3;
    return sb[SB_HL] * 3;
}

/* The master over a block, in place on its frames (o: left, right, ...): the DC blocker, LOWCUT / BASS+, the limiter,
 * the soft clip. The states are the globals above, held in locals over the block (a call a sample loaded and stored
 * them every sample); lowcut and smooth are constants where a loop is built for them (master_block), so a sample has
 * no test of the settings. */
static inline __attribute__((always_inline)) void master_loop(int32_t *o, uint32_t n, uint32_t lowcut, int smooth)
{
    int32_t dl = dc_l, el = dce_l, dr = dc_r, er = dce_r, env = lim_env, lg = lim_g;
    int32_t l1 = lc_l1, l2 = lc_l2, r1 = lc_r1, r2 = lc_r2, e0 = lce[0], e1 = lce[1], e2 = lce[2], e3 = lce[3];
    int32_t sb[SB_N] = {sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl};
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u], al, ar, a;
        l = dc_block(l, &dl, &el);
        r = dc_block(r, &dr, &er);
        if (lowcut) {                 /* two one-pole high-passes, error feedback as dc_block (the */
            uint32_t sh = lowcut == 2u ? 5u : 6u;       /* rounded step stopped at |x - lc| < 32: an offset) */
            int32_t b = lowcut == 2u ? spk_bass(sb, (l + r) >> 1) : 0;
            l = lowcut1(l, &l1, &e0, sh);
            l = lowcut1(l, &l2, &e1, sh) + b;
            r = lowcut1(r, &r1, &e2, sh);
            r = lowcut1(r, &r2, &e3, sh) + b;
        }
        al = l < 0 ? -l : l;
        ar = r < 0 ? -r : r;
        a = al > ar ? al : ar;
        if (smooth) {                /* ChoralRoot: the peak held at once, the gain eased down over ~1.5 ms */
            int32_t gt;
            if (a > env)
                env = a;
            else if (env > LIM_T)
                env -= ((env - LIM_T) >> 12) + 1;
            gt = env > LIM_T ? (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)env) : 32768;
            if (gt < lg)
                lg += (gt - lg) >> 6;
            else
                lg = gt;
            if (lg < 32768) {
                l = ((l >> 4) * lg) >> 11;
                r = ((r >> 4) * lg) >> 11;
            }
        } else {
            if (a > env)
                env += (a - env) >> 2;
            else if (env > LIM_T)
                env -= ((env - LIM_T) >> 12) + 1;
            if (env > LIM_T) {
                int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)env);   /* < 32768 */
                l = ((l >> 4) * g) >> 11;                    /* >> 4 first: |l| may be far above Q15 */
                r = ((r >> 4) * g) >> 11;
            }
        }
        o[2u * i] = softclip(l);
        o[2u * i + 1u] = softclip(r);
    }
    dc_l = dl;
    dce_l = el;
    dc_r = dr;
    dce_r = er;
    lim_env = env;
    lim_g = lg;
    if (lowcut) {
        lc_l1 = l1;
        lc_l2 = l2;
        lc_r1 = r1;
        lc_r2 = r2;
        lce[0] = e0;
        lce[1] = e1;
        lce[2] = e2;
        lce[3] = e3;
    }
    if (lowcut == 2u) {
        sb_lp1 = sb[SB_LP1];
        sb_lp2 = sb[SB_LP2];
        sb_lp3 = sb[SB_LP3];
        sb_lp4 = sb[SB_LP4];
        sb_env = sb[SB_ENV];
        sb_h1 = sb[SB_H1];
        sb_h2 = sb[SB_H2];
        sb_hl = sb[SB_HL];
    }
}
CR_HOT_MASTER static __attribute__((noinline)) void master_block(int32_t *o, uint32_t n)
{
    uint32_t lowcut = fx_lowcut;                        /* (the main loop's: it does not run under this block) */
    if (fx_smooth) {
        if (!lowcut)
            master_loop(o, n, 0u, 1);
        else if (lowcut == 2u)
            master_loop(o, n, 2u, 1);
        else
            master_loop(o, n, 1u, 1);
    } else {
        if (!lowcut)
            master_loop(o, n, 0u, 0);
        else if (lowcut == 2u)
            master_loop(o, n, 2u, 0);
        else
            master_loop(o, n, 1u, 0);
    }
}

/* ------------------------------------------------------------------------------------------------------------
 * Options > Output: Classic / Clean (ChoralRoot 0.19, docs/AUDIO-QUALITY.md; GitHub issue 3).
 * Classic is the master above, untouched: 0.18's sound bit for bit. Clean is another chain on the same mix:
 *   the mix at a FIXED gain (CLN_TRIM: MASTER is not in front of anything any more), with the two bits the Classic
 *   line shifts out kept (the chain's samples are Q17: a quarter of a Q15 unit) -> the same DC blocker -> a limiter
 *   that is a safety, not a part of the sound (threshold CLN_T instead of LIM_T: the knee below leaves it the room;
 *   the peak HELD for CLN_HOLD samples and released slowly, so a bass note's own period no longer moves the gain;
 *   the gain applied to all the sample's bits) -> a knee that is a straight line up to CLN_K and a tanh only above it
 *   (to full scale) -> MASTER, last, into the DAC's 24-bit word (out_frac: the 7 bits under the Q15 sample, which
 *   Classic leaves at 0): the knob turns the level down and nothing else, a quiet setting keeps its resolution, and
 *   the limiter's work no longer depends on it.
 * No LOWCUT / BASS+ in it: ChoralRoot has no control for them and holds them off (cr_settings.c cr_settings_boot).
 * fx_out_want is what Options asks for (the main loop writes it), fx_out_mode what runs: a change is taken at a
 * block's start, the mix faded to nothing over OUT_FADE_BLK blocks, the chain changed at that zero with its limiter
 * at rest, and faded in again: no step in the sound. Felucca's builds never ask: Classic. */
#ifndef CLN_TRIM
#define CLN_TRIM 3072                   /* Q12, a multiple of 16: the mix into the Clean chain, -2.5 dB (x 0.75) */
#endif
#ifndef CLN_T
#define CLN_T 26000                     /* the limiter's threshold in Q15 units (LIM_T + 3.2 dB) */
#endif
#define CLN_K 26000                     /* the knee: a straight line up to here (-2.0 dBFS: the limiter's threshold, so
                                         * nothing the limiter lets through meets the curve), then
                                         * K + R tanh((a - K) / R), R = 32767 - K: to full scale. A safety net only */
/* The hold (0.19's third round, docs/AUDIO-QUALITY.md "Round 3"). What Clean had left on a loud chord was the envelope
 * letting go between the chord's beats and stepping the level 0.1 to 2 dB at the next one. So the envelope is held
 * CLN_HOLD (200 ms) and held again by every block's peak within 1.2 dB of it (CLN_REHOLD): a chord, a wet tail, an
 * arp keep one gain. A HIT, a peak more than 3.5 dB above the envelope (CLN_HIT), gets the short hold of the first
 * rounds (CLN_HOLD_HIT, 40 ms: a period of 25 Hz) until the envelope has begun to let go: a kick or a slammed chord
 * is released as it was (12 dB down: back within half a dB after 0.47 s), not held down for the long one. */
#ifndef CLN_HOLD
#define CLN_HOLD 8820u                  /* 200 ms */
#endif
#ifndef CLN_HOLD_HIT
#define CLN_HOLD_HIT 1764u              /* 40 ms: after a hit */
#endif
#ifndef CLN_HIT
#define CLN_HIT 1                       /* a hit: a peak more than env >> CLN_HIT (3.5 dB) above the envelope */
#endif
#ifndef CLN_REHOLD
#define CLN_REHOLD 3                    /* a block's peak within env >> CLN_REHOLD (1.2 dB) of the envelope holds it again */
#endif
#ifndef CLN_CREEP
#define CLN_CREEP 10                    /* .. and, once CLN_HOLD samples of such blocks have gone by without the
                                         * top itself (a peak within 0.03 dB of the envelope), draws the envelope a
                                         * 1024th of the way down to itself: an envelope held up to 1.2 dB above a
                                         * steady sound (a release that stopped in the window) comes down to it
                                         * within a second or two, too slowly to be a step; a sound whose top comes
                                         * back (a bass note's period, a chord's beat) keeps its gain exactly */
#endif
#ifndef CLN_REL
#define CLN_REL 13                     /* the release: (env - T) >> CLN_REL a sample, 186 ms */
#endif
/* The look-ahead (0.19, docs/AUDIO-QUALITY.md "The look-ahead"). The chain's output is its input CLN_LA samples late,
 * and the limiter's gain is a straight line across each block of CLN_LA_B samples, from what the last block ended on
 * to the mean of the last CLN_LA_K blocks' gains, each of them the gain that fits the loudest sample of its own block
 * and of the CLN_LA_K before it under the threshold. So the gain has arrived before the peak comes out of the delay:
 * no sample leaves the limiter above CLN_T (cr_output_test.c proves it), where the gain used to ease down over 1.5 ms
 * AFTER the peak and the first samples went into the knee. One block of look-ahead and the linear glide between
 * blocks are Sloop 2.6's design (sloop-fm1 firmware/src/fx.c lim_block, GPL-3.0: LICENSING.md); the hold, the
 * release, the mean over CLN_LA_K blocks, the block maximum kept from block to block instead of a scan of the delay,
 * and the arithmetic are ChoralRoot's. CLN_LA_B divides CTL (the audio ISR's block: the block's peak is known before
 * its first sample goes out); a call with fewer samples (tests) takes a slower, safe way. */
#ifndef CLN_LA_B
#define CLN_LA_B 32u                    /* a block of the look-ahead, samples */
#endif
#ifndef CLN_LA_K
#define CLN_LA_K 1u                     /* blocks of look-ahead */
#endif
#define CLN_LA (CLN_LA_B * CLN_LA_K)    /* the delay: 32 samples, 0.73 ms */
#define CLN_DLEN (CLN_LA <= 16u ? 16u : CLN_LA <= 32u ? 32u : CLN_LA <= 64u ? 64u : 128u)
#ifndef CLN_MAKEUP
#define CLN_MAKEUP 4096                 /* Q12 after the knee, with MASTER: 4096 = none, the DAC word stays under the
                                         * Q15 sample's full scale as in every release. The word has 6 dB above that
                                         * (audio.c OUT_SHIFT): a build with 5461 gives the trim back there. NOT the
                                         * default: nobody has heard this unit's output stage up there
                                         * (docs/AUDIO-QUALITY.md "Proposed") */
#endif
#define CLN_T4 (4 * CLN_T)              /* (Q17) */
#define CLN_K4 (4 * CLN_K)
#define CLN_R4 (4 * (32767 - CLN_K))    /* the knee's range above K (Q17) */
#define CLN_KS (268435456 / CLN_R4)     /* 32768 / R4 in Q13: the tanh table's own scale */
#define CLN_G1 ((int32_t)(CLN_LA_K << 15))             /* the sum of CLN_LA_K block gains at rest */
#define OUT_FADE_BLK 16u                /* 512 samples, 11.6 ms, each way */
#define OUT_FADE_FLUSH ((CLN_LA + CTL - 1u) / CTL)     /* silent blocks between: the delay runs out */
_Static_assert(OUT_FADE_BLK * CTL == 512u, "the fade's step is 4096 / 512");
_Static_assert(!(CLN_TRIM & 15) && CLN_T <= CLN_K && CLN_K <= 30000 && CLN_MAKEUP <= 8192, "the Clean chain's ranges");
_Static_assert(CLN_LA_K >= 1u && CLN_LA_K <= 8u && CLN_LA <= 128u && CTL % CLN_LA_B == 0u, "the look-ahead's blocks");
static volatile uint8_t fx_out_want;
static uint8_t fx_out_mode, fx_out_fade;
static int32_t cln_dl[2u * CLN_DLEN];   /* the delay: the chain's samples (Q17, after the DC blocker), L R. In RAM
                                         * (.bss, 256 bytes): the ISR reads and writes it every sample; the pool is
                                         * the tight resource (docs/FX.md "The cost on the unit") */
static uint32_t cln_w, cln_hold, cln_hlen = CLN_HOLD, cln_top;
static int32_t cln_pk[CLN_LA_K];        /* the peaks of the blocks in the delay, the newest first */
static int32_t cln_wh[CLN_LA_K];        /* their gains (Q15), a ring; cln_gs is their sum */
static uint32_t cln_wi;
static int32_t cln_env = CLN_T4, cln_wg = 32768, cln_gs = CLN_G1, cln_g = 32768, cln_mq;
static uint8_t out_frac[2u * CTL];      /* Clean: the bits under each of the block's Q15 samples: the DAC word's 7
                                         * (audio.c), or between clean_block and a later clean_post the chain's 2 */
#ifdef CLN_GAIN_TAP                     /* (tests/master_measure.c: the gain of each sample of the last call) */
static int32_t cln_gtap[CTL];
#endif

/* the limiter at rest and its delay empty (power-on; Options > Output changed, at the fade's zero) */
static void clean_rest(void)
{
    uint32_t i;
    for (i = 0; i < 2u * CLN_DLEN; i++)
        cln_dl[i] = 0;
    for (i = 0; i < CLN_LA_K; i++) {
        cln_pk[i] = 0;
        cln_wh[i] = 32768;
    }
    cln_w = cln_wi = cln_hold = cln_top = 0;
    cln_hlen = CLN_HOLD;
    cln_env = CLN_T4;
    cln_wg = cln_g = 32768;
    cln_gs = CLN_G1;
}

/* (Q17) */
static inline int32_t clean_knee(int32_t y)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= CLN_K4)
        return y;
    a -= CLN_K4;
    if (a > 2 * CLN_R4)                 /* (the table ends at tanh 2) */
        a = 2 * CLN_R4;
    a = CLN_K4 + ((softclip((a * CLN_KS) >> 13) * CLN_R4) >> 15);
    return y < 0 ? -a : a;
}
/* x * g / 32768 with every bit of x (|x| < 2^23, g Q15, 32768 the identity): the limiter's gain (Classic's
 * (x >> 4) * g >> 11 drops four bits and, rounding down from them, shifts the sample by up to 16 while it limits) */
static inline int32_t clean_gain(int32_t x, int32_t g)
{
    return (((x >> 8) * g) >> 7) + (((x & 255) * g) >> 15);
}

/* the slow way's part (a call that is not whole blocks: tests): every peak the limiter knows of and the delay's own */
static __attribute__((noinline)) int32_t clean_short(int32_t p)
{
    uint32_t i;
    for (i = 0; i < CLN_LA_K; i++)
        if (cln_pk[i] > p)
            p = cln_pk[i];
    for (i = 0; i < 2u * CLN_DLEN; i++) {
        int32_t a = cln_dl[i] < 0 ? -cln_dl[i] : cln_dl[i];
        if (a > p)
            p = a;
    }
    for (i = 0; i < CLN_LA_K; i++)
        cln_pk[i] = p;
    return p;
}

CR_HOT_MASTER static __attribute__((noinline)) void clean_block(int32_t *o, uint32_t n)
{
    int32_t dl = dc_l, el = dce_l, dr = dc_r, er = dce_r;
    uint32_t w = cln_w;
#ifdef CLN_GAIN_TAP
    uint32_t tap = 0;
#endif
    while (n) {
        uint32_t m = n < CLN_LA_B ? n : CLN_LA_B, i;
        int32_t p = 0, wm, env = cln_env, g0 = cln_gs, g1;
        /* in: through the DC blocker into the delay, the block's peak; out: what went in CLN_LA samples ago */
        for (i = 0; i < m; i++) {
            int32_t l = dc_block(o[2u * i], &dl, &el), r = dc_block(o[2u * i + 1u], &dr, &er);
            int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            uint32_t rd = (w + CLN_DLEN - CLN_LA) & (CLN_DLEN - 1u);
            if (al > p)
                p = al;
            if (ar > p)
                p = ar;
            o[2u * i] = cln_dl[2u * rd];
            o[2u * i + 1u] = cln_dl[2u * rd + 1u];
            cln_dl[2u * w] = l;
            cln_dl[2u * w + 1u] = r;
            w = (w + 1u) & (CLN_DLEN - 1u);
        }
        if (p > 0x7FFFFF) {             /* (clean_gain's range; 36 dB over the threshold: no sound gets there) */
            for (i = 1; i <= m; i++) {
                uint32_t j = (w - i) & (CLN_DLEN - 1u);
                cln_dl[2u * j] = clamp(cln_dl[2u * j], -0x7FFFFF, 0x7FFFFF);
                cln_dl[2u * j + 1u] = clamp(cln_dl[2u * j + 1u], -0x7FFFFF, 0x7FFFFF);
            }
            p = 0x7FFFFF;
        }
        /* the loudest of this block and of those still in the delay */
        if (m == CLN_LA_B) {
            wm = p;
            for (i = CLN_LA_K; i-- > 0u;) {
                if (cln_pk[i] > wm)
                    wm = cln_pk[i];
                cln_pk[i] = i ? cln_pk[i - 1u] : p;
            }
        } else {
            wm = p = clean_short(p);
        }
        /* the envelope: a new peak at once, held (CLN_HOLD; after a hit CLN_HOLD_HIT), held again by every peak
         * within 1.2 dB of it, then released */
        if (wm > env) {
            if (wm - env > (env >> CLN_HIT))            /* a hit: the short hold, until the envelope has let go */
                cln_hlen = CLN_HOLD_HIT;
            env = wm;
            cln_hold = cln_hlen;
            cln_top = 0;
        } else if (p > env - (env >> CLN_REHOLD)) {
            if (cln_hold < cln_hlen)
                cln_hold = cln_hlen;
            if (p >= env - (env >> 8))                  /* the top itself again: nothing to creep to */
                cln_top = 0;
            else if (cln_top < CLN_HOLD)
                cln_top += m;
            else                                        /* (never under the delay's own peak) */
                env -= (env - (wm > CLN_T4 ? wm : CLN_T4)) >> CLN_CREEP;
        } else if (cln_hold) {
            cln_hold = cln_hold > m ? cln_hold - m : 0u;
        } else if (env > CLN_T4) {
            cln_hlen = CLN_HOLD;                        /* (letting go: from here on the long hold again) */
            env -= (((env - CLN_T4) >> CLN_REL) + 4) * (int32_t)m;
            if (env < wm)
                env = wm;
            if (env < CLN_T4)
                env = CLN_T4;
        }
        if (env != cln_env) {           /* (one division a block, and only while the envelope moves) */
            cln_env = env;
            cln_wg = env > CLN_T4 ? (int32_t)(((uint32_t)CLN_T4 << 15) / (uint32_t)env) : 32768;
        }
        g1 = g0 - cln_wh[cln_wi] + cln_wg;              /* the mean of the last CLN_LA_K gains, as their sum */
        cln_wh[cln_wi] = cln_wg;
        cln_wi = cln_wi + 1u < CLN_LA_K ? cln_wi + 1u : 0u;
        if (m != CLN_LA_B) {            /* (the slow way: no older gain above this one) */
            for (i = 0, g1 = 0; i < CLN_LA_K; i++) {
                if (cln_wh[i] > cln_wg)
                    cln_wh[i] = cln_wg;
                g1 += cln_wh[i];
            }
        }
        cln_gs = g1;
        if (g0 != CLN_G1 || g1 != CLN_G1) {             /* limiting: the gain glides from g0 to g1 over the block */
            int32_t acc = g0 * (int32_t)m, d = g1 - g0;
            for (i = 0; i < m; i++) {
                int32_t g;
                acc += d;
                g = m == CLN_LA_B ? (int32_t)((uint32_t)acc / (CLN_LA_K * CLN_LA_B))
                                  : (int32_t)((uint32_t)acc / (CLN_LA_K * m));
                o[2u * i] = clean_knee(clean_gain(o[2u * i], g));
                o[2u * i + 1u] = clean_knee(clean_gain(o[2u * i + 1u], g));
#ifdef CLN_GAIN_TAP
                cln_gtap[(tap + i) & (CTL - 1u)] = g;
#endif
            }
        }
#ifdef CLN_GAIN_TAP
        else
            for (i = 0; i < m; i++)
                cln_gtap[(tap + i) & (CTL - 1u)] = 32768;
        tap += m;
#endif
        /* (at rest nothing in the delay is over the threshold: the samples go out as they are, under the knee) */
        o += 2u * m;
        n -= m;
    }
    dc_l = dl;
    dce_l = el;
    dc_r = dr;
    dce_r = er;
    cln_w = w;
    cln_g = cln_gs / (int32_t)CLN_LA_K;
}
/* Clean's last stage: MASTER (eased over the block: a turned knob is a ramp, not 32-sample steps) on the block, kept
 * as the DAC's 24-bit word: out = its upper 17 bits (the Q15 sample: what USB, the scope and the click see), out_frac
 * the 7 under them (audio.c adds them to the word). q17: the block is the chain's own (Q17); else it is Q15 samples
 * with the chain's two bits in out_frac (clean_split: USB Level = Fixed, where USB's tap and the click come between) */
static inline __attribute__((always_inline)) void clean_post_loop(int32_t *out, uint32_t n, int q17)
{
    int32_t m = ((int32_t)song.master_q12 * CLN_MAKEUP >> 12) << 8, mq = cln_mq, d = (m - mq) >> CTL_LOG2;
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t j = (2u * i) & (2u * CTL - 1u);
        int32_t g, l = out[2u * i], r = out[2u * i + 1u];
        if (!q17) {
            l = l * 4 + out_frac[j];
            r = r * 4 + out_frac[j + 1u];
        }
        mq += d;
        g = mq >> 8;
        l = (l * g) >> 7;                               /* Q17 x Q12 -> Q22 (|l| < 2^18) */
        r = (r * g) >> 7;
        out[2u * i] = l >> 7;
        out[2u * i + 1u] = r >> 7;
        out_frac[j] = (uint8_t)(l & 127);
        out_frac[j + 1u] = (uint8_t)(r & 127);
    }
    cln_mq = m;
}
CR_HOT_MASTER static __attribute__((noinline)) void clean_post(int32_t *out, uint32_t n) { clean_post_loop(out, n, 0); }
CR_HOT_MASTER static __attribute__((noinline)) void clean_post17(int32_t *out, uint32_t n) { clean_post_loop(out, n, 1); }
CR_HOT_MASTER static __attribute__((noinline)) void clean_split(int32_t *out, uint32_t n)   /* Q17 -> Q15 and the two bits under */
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++) {
        out_frac[i & (2u * CTL - 1u)] = (uint8_t)(out[i] & 3);
        out[i] >>= 2;
    }
}
/* Options changed Output: this block of the chain's input (o, n = CTL) faded, and the chain changed at the zero: the
 * mix down to nothing over OUT_FADE_BLK blocks, then OUT_FADE_FLUSH silent blocks through the old chain (Clean's
 * delay plays its last samples out: nothing of the fade's end is cut off), the other chain from rest with an empty
 * delay, and up again */
static __attribute__((noinline)) void out_fade(int32_t *o, uint32_t n)
{
    uint32_t i, k;
    if (!fx_out_fade)
        fx_out_fade = 1;
    k = fx_out_fade - 1u;
    for (i = 0; i < n; i++) {
        int32_t g = k < OUT_FADE_BLK ? 4096 - 8 * (int32_t)(k * CTL + i + 1u)
                  : k < OUT_FADE_BLK + OUT_FADE_FLUSH ? 0
                  : 8 * (int32_t)((k - OUT_FADE_BLK - OUT_FADE_FLUSH) * CTL + i + 1u);
        g = clamp(g, 0, 4096);
        o[2u * i] = ((clamp(o[2u * i], -0x7FFFFF, 0x7FFFFF) >> 4) * g) >> 8;
        o[2u * i + 1u] = ((clamp(o[2u * i + 1u], -0x7FFFFF, 0x7FFFFF) >> 4) * g) >> 8;
    }
    if (fx_out_fade == OUT_FADE_BLK + OUT_FADE_FLUSH)   /* silent now: the other chain from the next block on */
        fx_out_mode = !fx_out_mode;                     /* (out_master puts both at rest after this one) */
    fx_out_fade = fx_out_fade == 2u * OUT_FADE_BLK + OUT_FADE_FLUSH ? 0u : (uint8_t)(fx_out_fade + 1u);
}
/* the mix (l, r, and Felucca's mono wet w: its chorus, delay and reverb; 0 where the mix already holds every return,
 * ChoralRoot's unit: mix_block) through its chain into out: fx_out_mode's, read once for the block */
static inline __attribute__((always_inline)) void out_master(int32_t *out, const int32_t *l, const int32_t *r,
                                                             const int32_t *w, uint32_t n)
{
    uint32_t i, mode = fx_out_mode;
    if (mode) {
        for (i = 0; i < n; i++) {                       /* (x 3 for the default trim: nothing shifted out) */
            int32_t ww = w ? w[i] : 0;
            out[2u * i] = ((l[i] + ww) * (CLN_TRIM >> 4)) >> 6;
            out[2u * i + 1u] = ((r[i] + ww) * (CLN_TRIM >> 4)) >> 6;
        }
    } else {
        int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB Level Fixed: MASTER after) */
        for (i = 0; i < n; i++) {
            int32_t ww = w ? w[i] : 0;
            out[2u * i] = (((l[i] + ww) >> 2) * mg) >> 10;
            out[2u * i + 1u] = (((r[i] + ww) >> 2) * mg) >> 10;
        }
    }
    if (fx_out_fade || fx_out_want != mode)
        out_fade(out, n);
    fx_out_blk = (uint8_t)mode;
    if (!mode) {
        master_block(out, n);
    } else {
        clean_block(out, n);
        if (fx_usb_fixed)
            clean_split(out, n);
        else
            clean_post17(out, n);
    }
    if (fx_out_mode != mode) {                          /* the fade's zero: this was the old chain's last block. Both
                                                         * limiters at rest and Clean's delay empty for the new one */
        lim_env = LIM_T;
        lim_g = 32768;
        clean_rest();
        cln_mq = ((int32_t)song.master_q12 * CLN_MAKEUP >> 12) << 8;
    }
}
/* Options > Output (cr_ui.c) and the settings at boot (now: before the first block, no fade) */
static uint8_t fx_out_pin;              /* the emulator's EMU_OUTPUT=classic (the proof that Classic is 0.18's, bit for
                                         * bit, on scripts whose settings say Clean): Classic whatever is asked */
static void fx_output_set(uint32_t clean, int now)
{
    if (fx_out_pin)
        clean = 0;
    fx_out_want = (uint8_t)(clean != 0u);
    if (now) {
        fx_out_mode = fx_out_want;
        fx_out_fade = 0;
        clean_rest();
        cln_mq = ((int32_t)song.master_q12 * CLN_MAKEUP >> 12) << 8;
    }
}
/* MASTER at once, no ramp (master.c master_boot: the pot as it stands before the audio starts, 0.19; Felucca 1.1.5's
 * fix for its #137): song.master_q12, and Output Clean's eased copy of it, which would glide from where it was over
 * the first block. master_pot_q12: the pot's level + 1 as master.c last read it (0: never read: a host test, a unit
 * whose ADC did not answer); cr_out.c cr_out_init goes back to it after the UI's power-on defaults */
static uint32_t master_pot_q12;
static void fx_master_now(uint32_t q12)
{
    song.master_q12 = q12;
    master_pot_q12 = q12 + 1u;
    cln_mq = ((int32_t)q12 * CLN_MAKEUP >> 12) << 8;
}

/* one frame through the master (tests/speaker_test.c: its response sample by sample) */
static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t o[2];
    o[0] = *l;
    o[1] = *r;
    master_block(o, 1u);
    *l = o[0];
    *r = o[1];
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#if FELUCCA_PERFORM
#include "perform.c"                                 /* the FX hold layer's effects (the master) */
#endif

#if CR_FX
#include "cr_fx.c"                                   /* ChoralRoot's delay and reverb (xf_block, after the chorus) */
#else
static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB: level as before the allpass fix */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.sp / 4u; i++)
        rev_u.sp[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
}

#endif

static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */

/* process the three buses for one block; sends in, wet stereo-equal out. (CR_FX 1, ChoralRoot's unit: only the
 * chorus is left in here, and since 0.19 that unit's chorus is cr_fx.c xc_block, two taps, left and right: this one
 * tap to both sides then runs only where CR_CHORUS_MONO asks for 0.18's return, mix_block) */
static __attribute__((unused)) void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in,
                                             int32_t *wet, uint32_t n)
{
    uint32_t i;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
#if CR_FX                                               /* the chorus alone: the delay and the reverb are xf_block's */
    (void)dly_in;
    (void)rev_in;
#else
    uint32_t dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258, rt;
    if (dl != fx.dly_dl) {                              /* ChoralRoot (fx_smooth): a new delay time crossfades */
        if (fx.dly_dl && fx_smooth) {
            fx.dly_old = fx.dly_xf > DLY_XF / 2u ? fx.dly_old : fx.dly_dl;   /* (mid-fade: the nearer of the two) */
            fx.dly_xf = DLY_XF;
        }
        fx.dly_dl = (uint16_t)dl;
    }
#endif
    for (i = 0; i < n; i++) {
        int32_t y = 0, r;
#if !CR_FX
        int32_t x;
#endif
        /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        fx.cho_w++;
#if !CR_FX
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        if (fx.dly_xf) {                                /* from the old tap to the new one, linear */
            int32_t xo = dly_buf[(fx.dly_w - fx.dly_old) & (DLY_LEN - 1u)];
            x += (int32_t)(((xo - x) * (int32_t)fx.dly_xf) / (int32_t)DLY_XF);
            fx.dly_xf--;
        }
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
#endif
        wet[i] = y;
    }
#if !CR_FX
    rt = song.g[G_RTYPE] == 1;
    if (rt != fx.rtype) {                               /* the model changed: the old one's block fades out, */
        int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;   /* its buffers are cleared, the new */
        for (i = 0; i < n; i++)                                     /* one starts from silence */
            t[i] = 0;
        if (fx.rtype)
            rev_spring(rev_in, t, n);
        else
            rev_room(rev_in, t, n);
        for (i = 0; i < n; i++, g -= d)
            wet[i] += mulq16(t[i], (uint32_t)g);
        rev_clear();
        fx.rtype = (uint8_t)rt;
        return;
    }
    if (rt)
        rev_spring(rev_in, wet, n);
    else
        rev_room(rev_in, wet, n);
#endif
}

/* one block of the whole mix (shared with hostsim.c): events -> each part (with its modulation matrix)
 * -> dist -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL];
#ifndef FX_UA_STAGE
#define FX_UA_STAGE FELUCCA_UAC                         /* (1 without USB: tests/cr_align_test.c, the staging alone) */
#endif
#if FX_UA_STAGE
/* USB audio's "ChoralRoot In" (usb_audio_stream.c ua_audio), one block's frames of six channels: 0-1 the master
 * (choralroot.c's mix_block shim: before the click), 2-3 the CHORD part, 4-5 the BASS part,
 * each part's dry stereo as it goes into the mix (after DIST, LEVEL, pan and the stereo side, before the sends, the
 * FX buses and MASTER), at half level (-6 dB: a loud chord there is ~2x the limiter's threshold) and saturated to
 * 16 bits. Filled only while the computer records (ua_stage_on: the shim sets it for the block from ChoralRoot In's
 * alternate setting); else the ISR does none of this */
#define UA_CAP_CHANNELS 6u
#define UA_PART_CAPTURED 2u                             /* parts 0 (CHORD) and 1 (BASS) */
static int16_t ua_stage[CTL * UA_CAP_CHANNELS] __attribute__((aligned(4)));
static uint8_t ua_stage_on;
static inline int16_t ua_sat(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }
/* Output Clean (0.19): the master pair is the chain's output, CLN_LA samples (0.73 ms) behind the mix; the two part
 * pairs are taken from the mix. So that the three stems of a recording line up to the sample, the part pairs wait
 * the same CLN_LA samples in a line of their own (4 x int16 a frame: 256 bytes of RAM, written only while the
 * computer records through Clean). Classic has no delay and none here. The line starts empty with each recording
 * and each change of the chain (ua_dl_on), as the master's does. */
typedef uint32_t __attribute__((may_alias)) ua_pair_t;  /* a part's stereo frame, two int16, moved as one word (the
                                                         * stage is 4-aligned and a frame is 12 bytes: its part pairs
                                                         * are at 4 and 8) */
static uint32_t ua_dl[CLN_DLEN * UA_PART_CAPTURED];
static uint32_t ua_dl_w;
static uint8_t ua_dl_on;
_Static_assert(UA_PART_CAPTURED == 2u && UA_CAP_CHANNELS == 6u, "ua_parts_late's frame");
static __attribute__((noinline)) void ua_parts_late(int16_t *st, uint32_t n)
{
    uint32_t i, w = ua_dl_w;
    if (!ua_dl_on) {
        for (i = 0; i < CLN_DLEN * UA_PART_CAPTURED; i++)
            ua_dl[i] = 0;
        ua_dl_on = 1;
    }
    for (i = 0; i < n; i++) {
        ua_pair_t *s = (ua_pair_t *)(void *)(st + i * UA_CAP_CHANNELS + 2u);
        uint32_t *in = ua_dl + 2u * w, *o = ua_dl + 2u * ((w + CLN_DLEN - CLN_LA) & (CLN_DLEN - 1u));
        uint32_t c = s[0], b = s[1];                    /* (CLN_LA = CLN_DLEN: the same cell, read before written) */
        s[0] = o[0];
        s[1] = o[1];
        in[0] = c;
        in[1] = b;
        w = (w + 1u) & (CLN_DLEN - 1u);
    }
    ua_dl_w = w;
}
#endif

/* A part's block b (sd: its stereo side, when side) at its level into the mix: the peak, the three sends (sg: their
 * gains, each moving by its dg a sample: ChoralRoot's ramp; Felucca's: the gains, dg 0), the pan, USB audio's tap
 * (pc). k: the block's values (MX_*). side, sends and tap are constants where a loop is built for them (the
 * mix_loop_* below): a part that sends nothing has no send in its loop, one that is not recorded no tap. Returns the
 * peak. */
enum { MX_LVL, MX_GL, MX_GR, MX_XMAX, MX_PK, MX_SG, MX_DG = MX_SG + 3, MX_N = MX_DG + 3 };
static inline __attribute__((always_inline)) int32_t mix_part_loop(const int32_t *b, const int32_t *sd, uint32_t n,
                                                                   const int32_t *k, int16_t *pc, int side, int sends,
                                                                   int tap)
{
    int32_t lvl = k[MX_LVL], gl = k[MX_GL], gr = k[MX_GR], xmax = k[MX_XMAX], pk = k[MX_PK];
    int32_t s0 = k[MX_SG], s1 = k[MX_SG + 1], s2 = k[MX_SG + 2], d0 = k[MX_DG], d1 = k[MX_DG + 1], d2 = k[MX_DG + 2];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x, y = 0, a, l, rr;
        if (side) {
            x = ((b[i] >> 2) * lvl) >> 10;
            y = ((sd[i] >> 2) * lvl) >> 10;
        } else {
            /* pre-shift: 8 loud voices; saturate where the level (+7.5 dB) or the pan would overflow (FM6's 16
             * voices keep Dexed's headroom; Melodee's) */
            x = clamp(((clamp(b[i], -884000, 884000) >> 2) * lvl) >> 10, -524287, 524287);
        }
        a = x < 0 ? -x : x;
        if (a > pk)
            pk = a;
        if (sends) {
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            s0 += d0;
            if (s0)
                send_c[i] += mulq15(xs, s0);
            s1 += d1;
            if (s1)
                send_d[i] += mulq15(xs, s1);
            s2 += d2;
            if (s2)
                send_r[i] += mulq15(xs, s2);
        }
        if (side) {
            l = ((x - y) * gl) >> 12;
            rr = ((x + y) * gr) >> 12;
        } else {
            l = (x * gl) >> 12;
            rr = (x * gr) >> 12;
        }
        mix_l[i] += l;
        mix_r[i] += rr;
#if FX_UA_STAGE
        if (tap) {
            pc[i * UA_CAP_CHANNELS] = ua_sat(l >> 1);
            pc[i * UA_CAP_CHANNELS + 1u] = ua_sat(rr >> 1);
        }
#endif
    }
    (void)pc;
    (void)tap;
    return pk;
}
/* the loops: a function each (two calls of one function that differ in a constant are folded into one by the
 * compiler before it inlines, and the constant with them); S: with the side, F: with the sends, U: with USB's tap */
#define MIX_LOOP_FN(name, side_, sends_, tap_) \
    static __attribute__((noinline)) int32_t name(const int32_t *b, const int32_t *sd, uint32_t n, const int32_t *k, \
                                                  int16_t *pc) \
    { \
        return mix_part_loop(b, sd, n, k, pc, side_, sends_, tap_); \
    }
CR_HOT_FX MIX_LOOP_FN(mix_loop, 0, 0, 0)                /* (cr_ram.h: the marker names the function for the build) */
CR_HOT_FX MIX_LOOP_FN(mix_loop_f, 0, 1, 0)
CR_HOT_FX MIX_LOOP_FN(mix_loop_s, 1, 0, 0)
CR_HOT_FX MIX_LOOP_FN(mix_loop_sf, 1, 1, 0)
#if FX_UA_STAGE
CR_HOT_FX MIX_LOOP_FN(mix_loop_u, 0, 0, 1)
CR_HOT_FX MIX_LOOP_FN(mix_loop_fu, 0, 1, 1)
CR_HOT_FX MIX_LOOP_FN(mix_loop_su, 1, 0, 1)
CR_HOT_FX MIX_LOOP_FN(mix_loop_sfu, 1, 1, 1)
#else
#define mix_loop_u mix_loop
#define mix_loop_fu mix_loop_f
#define mix_loop_su mix_loop_s
#define mix_loop_sfu mix_loop_sf
#endif
#undef MIX_LOOP_FN

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
/* (mix_part's second half: the part rendered into b, its side into side when side_on, nr voices) */
CR_HOT_CORE static void mix_part_out(track_t *t, int32_t *b, int32_t *side, uint8_t side_on, uint32_t nr, uint32_t n)
{
    int32_t *sd;
    int16_t *pc = 0;
#if FX_UA_STAGE
    if (ua_stage_on && (uint32_t)(t - trk) < UA_PART_CAPTURED)
        pc = ua_stage + 2u + 2u * (uint32_t)(t - trk);
#endif
    if (nr)
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (MODP(t).on)
            mod_end(t);
        return;
    }
    {
        int32_t lv = t->p[P_LEVEL] & 127, lvl, pan = t->p[P_PAN];
        if (t->trim && lv)                              /* the sound's trim (ChoralRoot), after LEVEL: 0.5 dB steps */
            lv = clamp(lv + t->trim, 1, 127);
        lvl = LEVEL_Q12[lv];
#if FELUCCA_VA
        pan = va_pan(t, pan);                           /* eng_va.c: VA's matrix PAN */
#endif
#if FELUCCA_QUAD
        pan = quad_pan(t, pan);                         /* eng_quad.c: QUAD's LFO -> PAN */
#endif
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
#if FELUCCA_QUAD
        quad_sends(t, &c, &d, &r);                      /* eng_quad.c: FM TONE's LFO -> the sends (ramped below) */
#endif
        int32_t xmax, k[MX_N], *g = k + MX_SG, *dg = k + MX_DG;
        int sends;
        dg[0] = dg[1] = dg[2] = 0;
#if CR_FX
        int32_t *sg = fx_sg[t - trk];
        if (fx_off)
            c = d = r = 0;
        dg[0] = (c - sg[0]) >> CTL_LOG2;                /* (CTL samples a block; the ramp's last fraction: a step of
                                                         * under 32 / 32768) */
        dg[1] = (d - sg[1]) >> CTL_LOG2;
        dg[2] = (r - sg[2]) >> CTL_LOG2;
        g[0] = sg[0];
        g[1] = sg[1];
        g[2] = sg[2];
        xmax = c > sg[0] ? c : sg[0];
        xmax = d > xmax ? d : sg[1] > xmax ? sg[1] : xmax;
        xmax = r > xmax ? r : sg[2] > xmax ? sg[2] : xmax;
        xmax = 0x7FFFFFFF / (xmax | 1);
        sends = (c | d | r | sg[0] | sg[1] | sg[2]) != 0;   /* (all 0: the ramps stay at 0, no bus gets a sample) */
#define FX_SEND_END() (sg[0] = c, sg[1] = d, sg[2] = r)
#else
        g[0] = c;
        g[1] = d;
        g[2] = r;
        xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        sends = (c | d | r) != 0;
#define FX_SEND_END() ((void)0)
#endif
        /* a stereo side (voice.c part_side: VA's SPREAD / USPREAD): DIST works on the mid, the SLICER plays the part
         * mono, the FX layer's mute fades both; the sends take the mid */
        sd = side_on && !t->p[P_SLCR] && !slicer_busy(t) ? side : 0;
        track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
#if FELUCCA_PERFORM
        if ((pf.mute >> (t - trk)) & 1u) {
            if (sd) {
                int32_t mg = pf.mg[t - trk];
                perf_mute((uint32_t)(t - trk), sd, n);
                pf.mg[t - trk] = mg;
            }
            perf_mute((uint32_t)(t - trk), b, n);       /* perform.c: a black key in the FX layer */
        }
#endif
        k[MX_LVL] = lvl;
        k[MX_GL] = gl;
        k[MX_GR] = gr;
        k[MX_XMAX] = xmax;
        k[MX_PK] = pk;
        /* the loop of this block: with or without the side, the sends, the tap */
        if (sd)
            pk = sends ? (pc ? mix_loop_sfu : mix_loop_sf)(b, sd, n, k, pc) : (pc ? mix_loop_su : mix_loop_s)(b, sd, n, k, pc);
        else
            pk = sends ? (pc ? mix_loop_fu : mix_loop_f)(b, sd, n, k, pc) : (pc ? mix_loop_u : mix_loop)(b, sd, n, k, pc);
        t->peak = pk;
        FX_SEND_END();
#undef FX_SEND_END
    }
    if (MODP(t).on)
        mod_end(t);                                     /* the stored values back */
}

static void mix_part(track_t *t, uint32_t n)
{
    uint32_t nr;
    CR_PROF(CRP_FX);
    mod_begin(t);                                       /* the matrix's per-block values into t->p (mod.c) */
    nr = track_render(t, part_buf, n);
    CR_PROF(t == &trk[0] ? CRP_PART0 : t == &trk[1] ? CRP_PART1 : CRP_FX);
    mix_part_out(t, part_buf, part_side, part_side_on, nr, n);
}

/* The parts that sound: Felucca's four; ChoralRoot's units play the chord and the bass part only (engines.c
 * ENG_STATE_NPART: a part above them has no engine state and takes no note: cr_out.c's midi_event and the engine's
 * routes are parts 0 / 1), so their matrix, render and mix have nothing to do. What a silent part still does to the
 * others stays: its LFO's tick (voice.c lfo_rand: part 3's S&H draws from the shared generator, whose sequence the
 * engines' noise and the notes' RAND follow) and the SLICER's step clock (slicer.c: a stub in the unit). */
#define MIX_NPART ENG_STATE_NPART
static void mix_part_idle(track_t *t, uint32_t n)
{
    track_lfo_tick(t);
    slicer_track(t, 0, n);
}

#if CR_CPU1
/* ChoralRoot's second core (cr_cpu1.h, docs/DUALCORE.md): part 1's render as core 1's job, into its own buffers.
 * It touches trk[1] (its voices, its LFO's value, its patch, P_E0..7 during an engine fade), its engine's state
 * (eng_state[1], the engine's per-part arrays, fm6_core.c's second scratch) and these; nothing else */
static int32_t c1_buf[CTL] __attribute__((section(".pool"))), c1_side[CTL] __attribute__((section(".pool")));
static uint8_t c1_side_on;
static uint32_t c1_nr;
static int32_t c1_lfo[2];                               /* the parts' LFO values before this block's tick */
#if P5_SPLIT
static int32_t p5x_buf[CTL] __attribute__((section(".pool")));   /* core 1's sum of the chord part's voices it renders */
static int32_t p5x_side[CTL] __attribute__((section(".pool")));  /* .. their stereo side (render2: the VA's SPREAD /
                                                                  * USPREAD, 3y177) */
static uint8_t p5x_side_on;                                      /* .. one of them added to it */
#endif
/* core 1's job of a block: the bass part, then (3y46, 3y177) the chord part's voices that p5x_run gave it (p5x.it[]
 * with c1: each its own state, into p5x_buf and, with render2, p5x_side). CRP_PART1 is the whole job's time */
CR_HOT_CORE static void c1_part1_job(uint32_t n)
{
    uint32_t t0 = cr_dbg.prof_on ? cr_prof_now() : 0u;
    c1_nr = track_render_x(&trk[1], c1_buf, c1_side, &c1_side_on, n, &c1_lfo[1]);
#if P5_SPLIT
    p5x_side_on = 0;
    if (p5x.n1) {
        const engine_t *e = ENGINES[trk[0].engine];
        uint32_t i;
        for (i = 0; i < n; i++)
            p5x_buf[i] = 0;
        if (e->render2)
            for (i = 0; i < n; i++)
                p5x_side[i] = 0;
        for (i = 0; i < p5x.n; i++)
            if (p5x.it[i].c1) {
                if (e->render2)
                    p5x_side_on |= (uint8_t)e->render2(&trk[0], p5x.it[i].v, p5x_buf, p5x_side, n, &p5x.it[i].m);
                else
                    e->render(&trk[0], p5x.it[i].v, p5x_buf, n, &p5x.it[i].m);
            }
    }
#endif
    if (cr_dbg.prof_on) {
        uint32_t t1 = cr_prof_now(), d = cr_prof_less(t1 - t0);
        cr_dbg.cur[CRP_PART1] += d;
        if (cr_c1_split) {                              /* (on core 1: its busy time, and when it went idle) */
            cr_dbg.c1_busy_cur += d;
            cr_dbg.c1v_cur += d;
            cr_dbg.c1_end = t1;
        }
    }
}
#if CR_FX && XF_SPLIT
/* 3y50, THE REVERB ON CORE 1 (docs/DUALCORE.md "What each core runs"): once both parts are in the mix the sends are
 * whole; core 1, done with its voices, takes the reverb (its half-band, its lines, the wet's up-sampler: the largest
 * of the three effects, 202 us of a half on average and 273 at most on the unit) into c1x_l / c1x_r while core 0 runs
 * the chorus and the delay; core 0 then waits for it and adds the two buffers into the mix. Sums of integers: the
 * mix is the one core's, bit for bit (tests/cr_c1_thread_test.c "effects"). The job touches xf's reverb state alone
 * (cr_fx.c xf_reverb), the sends it reads are not written again before the next block, and these.
 * The delay at the half rate (XF_SPLIT_HALF, CR_FX_DELAY_DS 2: the default since 0.22) shares the half-rate wet and
 * its up-sampler with the reverb: core 1's job is then the reverb's half-band and lines alone, into c1h_l / c1h_r at
 * the half rate; core 0 runs the delay into xf.t[2..3], adds core 1's after the join and up-samples the sum (the
 * up-sampler's tail alone, with no lines to run, stays on core 0: no job). The same sums of integers as xf_block */
#if XF_SPLIT_HALF
static int32_t c1h_l[XF_NH] __attribute__((section(".pool"))), c1h_r[XF_NH] __attribute__((section(".pool")));
#define C1X_BITS 2u                                     /* (xf_prep's: the reverb's lines run) */
#else
static int32_t c1x_l[CTL] __attribute__((section(".pool"))), c1x_r[CTL] __attribute__((section(".pool")));
#define C1X_BITS 6u                                     /* (the reverb's lines or its up-sampler's tail run) */
#endif
static uint8_t c1x_rev;                                 /* this block's reverb runs its lines (else the tail alone) */
static uint32_t c1x_tm[CRX_N];                          /* the job's parts' times (cr_fx.c xf_reverb's tm) */
CR_HOT_CORE static void c1_fx_job(uint32_t n)
{
    uint32_t i, t0 = cr_dbg.prof_on ? cr_prof_now() : 0u, mk = t0;
#if XF_SPLIT_HALF
    for (i = 0; i < (n >> 1); i++)
        c1h_l[i] = c1h_r[i] = 0;
    xf_reverb(send_r, 0, 0, n, c1x_rev, cr_dbg.prof_on ? c1x_tm : 0, &mk, c1h_l, c1h_r, 0);
#else
    for (i = 0; i < n; i++)
        c1x_l[i] = c1x_r[i] = 0;
    xf_reverb(send_r, c1x_l, c1x_r, n, c1x_rev, cr_dbg.prof_on ? c1x_tm : 0, &mk, xf.t[2], xf.t[3], 1);
#endif
    if (cr_dbg.prof_on && cr_c1_split) {
        uint32_t t1 = cr_prof_now();
        cr_dbg.c1_busy_cur += cr_prof_less(t1 - t0);
        if (cr_dbg.c1_end && (int32_t)(t0 - cr_dbg.c1_end) > 0)
            cr_dbg.w1_cur += t0 - cr_dbg.c1_end;        /* (core 1 idle from its voices' end to this job) */
        cr_dbg.c1_end = 0;
    }
}
/* the effects of a block with core 1 alive: the reverb there when it has something to do (above), the chorus and the
 * delay here, the join, the reverb's buffers added. Core 1 refusing, failing or given up: everything here, as
 * xf_block. Returns with the stage marks as mix_block's serial path leaves them (the join's wait is CRP_WAIT's) */
static __attribute__((noinline)) void mix_fx_split(uint32_t n)
{
    uint32_t f = xf_prep(send_d, send_r, n), i, k, mk = 0;
    int ran = 0;
    if ((f & C1X_BITS) && cr_c1_check()) {              /* the reverb's lines (or, at the full-rate delay, its
                                                         * up-sampler's tail) run */
        c1x_rev = (uint8_t)(f >> 1 & 1u);
        for (k = 0; k < CRX_N; k++)
            c1x_tm[k] = 0;
        cr_c1_split = 1;
        ran = cr_c1_run(c1_fx_job, n);
        if (!ran)
            cr_c1_split = 0;
    }
    if (!ran)
        cr_dbg.c1_end = 0;                              /* (no reverb job: core 1's idle stretch ends with the block) */
    xc_block(send_c, mix_l, mix_r, n);                  /* (cr_fx.c: the chorus, two taps, into the mix) */
    cr_dbg.fxmap_cur |= 1u | (f & 1u) << 1 | (f & 6u ? 4u : 0u) | (ran ? 8u : 0u);
    if (!f)
        return;
#if XF_SPLIT_HALF
    for (i = 0; i < (n >> 1); i++)                      /* (the delay's wet and the reverb's share it: from 0, as
                                                         * xf_block; core 1's lines are in c1h_l / c1h_r) */
        xf.t[2][i] = xf.t[3][i] = 0;
#endif
    CR_PROFX_START();
    if (f & 1u)
        xf_delay(send_d, mix_l, mix_r, n);
    if (ran) {
        CR_PROF(CRP_FX);
        if (cr_c1_wait())                               /* 8 ms or a fault: core 1 held for good (C1_GAVE_UP) */
            ran = 0;
        if (cr_dbg.prof_on) {                           /* the join's wait: CRP_WAIT's, and on its own */
            uint32_t t_ = cr_prof_now(), d = cr_prof_less(t_ - cr_dbg.mark);
            cr_dbg.cur[CRP_WAIT] += d;
            cr_dbg.fxw_cur += d;
            cr_dbg.mark = t_;
        }
        cr_c1_split = 0;
        if (ran) {
            cr_dbg.c1x_jobs++;
            for (k = 0; k < CRX_N; k++)
                cr_dbg.xcur[k] += c1x_tm[k];
#if XF_SPLIT_HALF
            for (i = 0; i < (n >> 1); i++) {            /* the reverb's wet onto the delay's, at the half rate */
                xf.t[2][i] += c1h_l[i];
                xf.t[3][i] += c1h_r[i];
            }
            CR_PROFX_START();
            xf_reverb(send_r, mix_l, mix_r, n, 0, 0, &mk, xf.t[2], xf.t[3], 1);   /* (rev 0: the up-sampler alone) */
#else
            for (i = 0; i < n; i++) {
                mix_l[i] += c1x_l[i];
                mix_r[i] += c1x_r[i];
            }
#endif
            return;
        }
        cr_dbg.fxmap_cur &= ~8u;                        /* (core 1 given up: core 0 runs the job, as c1_part1_job) */
        CR_PROFX_START();
    }
    xf_reverb(send_r, mix_l, mix_r, n, (int)(f >> 1 & 1u), 0, &mk, xf.t[2], xf.t[3], 1);   /* (here; with nothing for
                                                         * the reverb: the up-sampler's states cleared, as xf_block
                                                         * does) */
}
#endif
#if P5_SPLIT
/* WHICH CORE RENDERS WHICH OF THE CHORD PART'S VOICES (3y46 for P5, weighed per engine since 3y177; docs/DUALCORE.md
 * "What each core runs"). Core 0 WAITS for core 1 before the effects and the master, so the half's time is the tick +
 * the LONGER of the two cores' renders + the effects + the master: what counts is the longer render. A chord voice
 * weighs its engine's engine_t.split x 10 us (P5 440, the VA 280: the unit's worst). The bass part is core 1's (its
 * job, as before): its P5 voices count there as P5X_VOICE_US each, a bass of another engine as P5X_UNIT_US a budget
 * unit. Core 0's side counts x P5X_C0_PCT / 100: TIMER5 nests in its audio ISR, about 7 % of core 0 (Melodee's "CPU0
 * service allowance", 5fca0a4 / 6ca918d, the idea alone; docs/notes/melodee-p5-dualcore-review.md). So: of nc voices
 * core 1 takes the k that makes the longer side shortest, a tie to core 1 (core 0 has the rest of the half), and for
 * P5 at most P5_CORE P5 voices a core (the owner's "4 per core"). -DP5_FILL_C1_FIRST=1 (P5): core 1 takes voices
 * until it has four, core 0 the rest (the first plan: for a four-note chord that is one core's time again, core 0
 * waiting for it) */
#ifndef P5_FILL_C1_FIRST
#define P5_FILL_C1_FIRST 0
#endif
#define P5X_VOICE_US P5_VOICE_US        /* a P5 voice at the peak (the unit: four are 1533 .. 1760) */
#define P5X_UNIT_US 75u                 /* a budget unit of another engine (16 units: 1070 .. 1230) */
#ifndef P5X_C0_PCT
#define P5X_C0_PCT 107u                 /* core 0's side, in % of its voices' time: TIMER5's ticks (6.6 .. 7.3 us at
                                         * 10 kHz dry, docs/INTEGRATION.md "TIMER5"); -DP5X_C0_PCT=100: 3y46's */
#endif
static uint32_t p5x_assign(const track_t *t0, uint32_t nc, uint32_t *nbp)
{
    const track_t *bt = &trk[P5_PART_BASS];
    uint32_t vu = ENGINES[t0->engine]->split * 10u, p5 = P5_IS(t0);
    uint32_t nb = p5_sounding(bt), bl, cap1, k, best = 0, bm = 0xFFFFFFFFu;   /* (a voice given up fades on its
                                                         * last sample's line: no voice's work) */
    if (P5_IS(bt)) {
        bl = nb * P5X_VOICE_US;
        k = nb;
        *nbp = nb;
    } else {
        bl = nb * voice_units(bt) * P5X_UNIT_US;
        k = nb * voice_units(bt) / P5_BASS_UNITS;
        *nbp = 0;
    }
    if (CR_C1_TEST_SHARE)                               /* (the host's tests: a share of their own) */
        return CR_C1_TEST_SHARE - 1u < nc ? CR_C1_TEST_SHARE - 1u : nc;
    cap1 = !p5 ? nc : k < P5_CORE ? P5_CORE - k : 0u;  /* (the four a core are P5's) */
    if (cap1 > nc)
        cap1 = nc;
    if (P5_FILL_C1_FIRST && p5)
        return cap1;
    for (k = 0; k <= cap1; k++) {
        uint32_t l1 = (bl + k * vu) * 100u, l0 = (nc - k) * vu * P5X_C0_PCT, m = l1 > l0 ? l1 : l0;
        if (p5 && nc - k > P5_CORE && k < cap1)
            continue;                                   /* (core 0 above its four while core 1 has room) */
        if (m <= bm) {
            bm = m;
            best = k;
        }
    }
    return best;
}
/* one core: its P5 voices of this block (78 PEAK: p5_core0_voices, p5_bass_voices) */
static __attribute__((noinline)) void p5x_one_core(void)
{
    uint32_t c = P5_IS(&trk[0]) ? p5_sounding(&trk[0]) : 0u, b = P5_IS(&trk[P5_PART_BASS]) ? p5_sounding(&trk[P5_PART_BASS]) : 0u;
    if (c + b > cr_dbg.p5_c0)
        cr_dbg.p5_c0 = c + b;
    if (b > cr_dbg.p5_bass)
        cr_dbg.p5_bass = b;
}
static __attribute__((noinline)) void p5x_bass_count(void)   /* two cores, a P5 bass under another engine's chord */
{
    uint32_t nb = p5_sounding(&trk[P5_PART_BASS]);
    if (nb > cr_dbg.p5_c1)
        cr_dbg.p5_c1 = nb;
    if (nb > cr_dbg.p5_bass)
        cr_dbg.p5_bass = nb;
}
/* voice.c track_render_x(trk[0]) calls it when every voice of the chord part is on the list (p5x.it): the hand-over
 * (the bass part and core 1's share of the list: ONE job a block, as before), core 0's share, the wait, the two sums
 * added (the mid into out, and with render2 the side into side). Sums of 32-bit integers: whatever the split, out[]
 * and side[] are what one core's render of the voices in their order gives, bit for bit (tests/cr_c1_thread_test.c).
 * The part's DC blocker runs after this, on core 0 (track_render_x). Core 1 not answering, refusing or given up: core
 * 0 runs the same job itself, in this block */
static __attribute__((noinline)) void p5x_run(track_t *t0, int32_t *out, int32_t *side, uint8_t *side_on, uint32_t n)
{
    const engine_t *e = ENGINES[t0->engine];
    uint32_t i, nb = 0, nc = 0, k, p5 = P5_IS(t0);
    int ran;
    /* P5: a whole voice's work, or (a voice given up, in its last block: p5_render) the line of its last sample to 0,
     * which is no work to share out and stays here. Another engine: every voice on the list (a VA voice given up
     * renders its fade whole; one that ended returns at once: eng_va.c va_render) */
    for (i = 0; i < p5x.n; i++) {
        const p5_voice_t *s = p5 ? p5_voice(t0, p5x.it[i].v) : 0;
        p5x.it[i].c1 = (uint8_t)(!p5 || p5x.it[i].v->active || p5x.it[i].m.amp1 || (s && s->steal));
        nc += p5x.it[i].c1;
    }
    k = p5x_assign(t0, nc, &nb);
    p5x.n1 = (uint8_t)k;
    for (i = 0; i < p5x.n; i++)                         /* core 1's: the first k whole voices of the list */
        if (p5x.it[i].c1) {
            if (k)
                k--;
            else
                p5x.it[i].c1 = 0;
        }
    if (p5) {                                           /* (78 PEAK's counts are P5 voices) */
        if (nc - p5x.n1 > cr_dbg.p5_c0)
            cr_dbg.p5_c0 = nc - p5x.n1;
        if (p5x.n1 + nb > cr_dbg.p5_c1)
            cr_dbg.p5_c1 = p5x.n1 + nb;
        if (nb > cr_dbg.p5_bass)
            cr_dbg.p5_bass = nb;
    } else if (P5_IS(&trk[P5_PART_BASS])) {
        p5x_bass_count();
    }
    CR_PROF(CRP_PART0);
    ran = cr_c1_check() && cr_c1_run(c1_part1_job, n);   /* (a second misread, a fault: given up) */
    CR_PROF_SKIP();                                     /* (the host's stub ran it here: counted as part1) */
    for (i = 0; i < p5x.n; i++)
        if (!p5x.it[i].c1) {
            if (e->render2)
                *side_on |= (uint8_t)e->render2(t0, p5x.it[i].v, out, side, n, &p5x.it[i].m);
            else
                e->render(t0, p5x.it[i].v, out, n, &p5x.it[i].m);
        }
    CR_PROF(CRP_PART0);
    if (ran && cr_c1_wait())                            /* 8 ms or a fault: core 1 held for good (C1_GAVE_UP) */
        ran = 0;
    CR_PROF(CRP_WAIT);
    cr_c1_split = 0;
    if (ran)
        cr_dbg.c1_jobs++;
    else
        c1_part1_job(n);                                /* core 0 does it: the bass part and the voices */
    CR_PROF_SKIP();
    if (p5x.n1)
        for (i = 0; i < n; i++)
            out[i] += p5x_buf[i];
    if (p5x_side_on) {                                  /* (render2: the side only when a voice of core 1's added to
                                                         * it, as track_render_x's side_on) */
        for (i = 0; i < n; i++)
            side[i] += p5x_side[i];
        *side_on = 1;
    }
    p5x.on = 0;
}
#endif
/* the split is one core's samples bit for bit unless part 0's render draws from the shared generator itself
 * (PHYS: a model change under a note) or both parts play VOICE (one breath-noise generator, eng_formant.c): then
 * this block renders on core 0 alone, as before */
static int c1_split_ok(void)
{
    const engine_t *e0 = ENGINES[trk[0].engine], *e1 = ENGINES[trk[1].engine];
#if FELUCCA_PHYS
    if (e0 == &ENG_PHYS)
        return 0;
#endif                                             /* (ChoralRoot since 0.18: no PHYS, FELUCCA_PHYS 0) */
    return !(e0 == &ENG_FORMANT && e1 == &ENG_FORMANT);
}
/* parts 0 and 1: everything shared first on core 0 (the matrices, the LFOs: their draws from the shared generator in
 * one core's order), part 1 to core 1, part 0 here, then both mixed in one core's order */
static void mix_parts_split(uint32_t n)
{
    track_t *t0 = &trk[0], *t1 = &trk[1];
    uint32_t nr0;
    int ran;
    CR_PROF(CRP_FX);
    cr_dbg.c1_end = 0;                                  /* (core 1's voices end stamp: this block's, c1_fx_job) */
    mod_begin(t0);
    c1_lfo[0] = mulq15(t0->lfo_val, t0->lfo_fade);
    track_lfo_tick(t0);
    mod_begin(t1);
    c1_lfo[1] = mulq15(t1->lfo_val, t1->lfo_fade);
    track_lfo_tick(t1);
    cr_c1_split = 1;
#if P5_SPLIT
    p5x.n = p5x.n1 = 0;
    if (ENGINES[t0->engine]->split && !CR_C1_TEST_NOSPLIT) {   /* P5 or the VA on the chord part: its voices on
                                                         * both cores. The job (the bass part and core 1's voices) is
                                                         * handed over and waited for inside the render: p5x_run */
        p5x.on = 1;
        nr0 = track_render_x(t0, part_buf, part_side, &part_side_on, n, &c1_lfo[0]);
        CR_PROF(CRP_PART0);
    } else
#endif
    {
#if P5_SPLIT
    if (P5_IS(t1))                                      /* (a P5 bass under another engine's chord: core 1's voices) */
        p5x_bass_count();
#endif
    ran = cr_c1_check() && cr_c1_run(c1_part1_job, n);   /* (a second misread, a fault: given up) */
    CR_PROF_SKIP();                                     /* (the host's stub ran it here: counted as part1) */
    nr0 = track_render_x(t0, part_buf, part_side, &part_side_on, n, &c1_lfo[0]);
    CR_PROF(CRP_PART0);
    if (ran && cr_c1_wait())                            /* 8 ms or a fault: core 1 held for good (C1_GAVE_UP),
                                                         * core 0 does it */
        ran = 0;
    CR_PROF(CRP_WAIT);
    cr_c1_split = 0;
    if (ran)
        cr_dbg.c1_jobs++;
    else
        c1_part1_job(n);
    CR_PROF_SKIP();
    }
    mix_part_out(t0, part_buf, part_side, part_side_on, nr0, n);
    mix_part_out(t1, c1_buf, c1_side, c1_side_on, c1_nr, n);
}
#endif

#if FELUCCA_PERFORM
/* the master with the FX layer's effects between its level and master_out (perform.c) */
static __attribute__((noinline)) void perf_master(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB Level Fixed: MASTER after) */
    for (i = 0; i < n; i++) {
        mix_l[i] = (((mix_l[i] + wet[i]) >> 2) * mg) >> 10;
        mix_r[i] = (((mix_r[i] + wet[i]) >> 2) * mg) >> 10;
    }
    perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        out[2u * i] = mix_l[i];
        out[2u * i + 1u] = mix_r[i];
    }
    master_block(out, n);                               /* (Options > Output is ChoralRoot's, which has no FX layer) */
}
#endif

#if CR_FX
/* THE ROUTING's Chain (cr_fx.c, docs/FX.md "The routing"): mix_block's effects stage with XP_RMODE 1, the master filter
 * inside the chain. The inputs from the dry mix and the last block's snapshots (xg_in: the chord part's sends the
 * amounts), the effects as mix_fx_split / xf_block run them (the reverb on core 1 when it is alive, the chorus and the
 * delay here) but each into its own wet (cr_fx.c xg.w), the join, then the assembly (xg_join) in place in the mix */
#define CR_FX_CHORD 0u                                 /* (cr_out.c CR_PART_CHORD: the chord part) */
static __attribute__((noinline)) void mix_fx_chain(uint32_t n)
{
    const track_t *t = &trk[CR_FX_CHORD];
    int32_t amt[XFE_NFX], *wd_l = xg.w[XFE_DELAY][0], *wd_r = xg.w[XFE_DELAY][1];
    int32_t *wr_l = xg.w[XFE_REVERB][0], *wr_r = xg.w[XFE_REVERB][1];
    uint32_t f, i, mk = 0;
    int ran = 0, done = 0;
#if XG_HALF
    int32_t *rl = xg.rh[0], *rr = xg.rh[1];
#endif
    amt[XFE_CHORUS] = fx_off ? 0 : t->p[P_CHOR] * 258;
    amt[XFE_DELAY] = fx_off ? 0 : t->p[P_DLY] * 258;
    amt[XFE_REVERB] = fx_off ? 0 : t->p[P_REV] * 258;
    xg_in(send_c, send_d, send_r, mix_l, mix_r, amt, n);
    f = xf_prep(send_d, send_r, n);
#if CR_CPU1 && XF_SPLIT
    if (cr_dualcore_active() && !CR_C1_TEST_FX0 && (f & C1X_BITS) && cr_c1_check()) {   /* (mix_fx_split's) */
        uint32_t k;
        c1x_rev = (uint8_t)(f >> 1 & 1u);
        for (k = 0; k < CRX_N; k++)
            c1x_tm[k] = 0;
        cr_c1_split = 1;
        ran = cr_c1_run(c1_fx_job, n);
        if (!ran)
            cr_c1_split = 0;
    }
    if (!ran)
        cr_dbg.c1_end = 0;
    cr_dbg.fxmap_cur |= 1u | (f & 1u) << 1 | (f & 6u ? 4u : 0u) | (ran ? 8u : 0u);
#endif
    xc_block(send_c, xg.w[XFE_CHORUS][0], xg.w[XFE_CHORUS][1], n);
#if XF_HALF && CR_FX_DELAY_DS == 2
    for (i = 0; i < (n >> 1); i++)                      /* (the delay's half-rate wet: its own here, from 0) */
        xf.t[2][i] = xf.t[3][i] = 0;
#endif
    CR_PROFX_START();
    if (f & 1u)
        xf_delay(send_d, wd_l, wd_r, n);
#if XF_HALF && CR_FX_DELAY_DS == 2
    xg_up(xg.ud, &xg.ud_live, xf.t[2], xf.t[3], wd_l, wd_r, n >> 1);
#endif
#if CR_CPU1 && XF_SPLIT
    if (ran) {
        CR_PROF(CRP_FX);
        if (cr_c1_wait())                               /* 8 ms or a fault: core 1 held for good (C1_GAVE_UP) */
            ran = 0;
        if (cr_dbg.prof_on) {
            uint32_t t_ = cr_prof_now(), d = cr_prof_less(t_ - cr_dbg.mark);
            cr_dbg.cur[CRP_WAIT] += d;
            cr_dbg.fxw_cur += d;
            cr_dbg.mark = t_;
        }
        cr_c1_split = 0;
        if (ran) {
            uint32_t k;
            cr_dbg.c1x_jobs++;
            for (k = 0; k < CRX_N; k++)
                cr_dbg.xcur[k] += c1x_tm[k];
#if XF_SPLIT_HALF
            rl = c1h_l;                                 /* (core 1's lines, at the half rate: up-sampled below) */
            rr = c1h_r;
#else
            for (i = 0; i < n; i++) {                   /* (core 1's wet, whole) */
                wr_l[i] = c1x_l[i];
                wr_r[i] = c1x_r[i];
            }
#endif
            done = 1;
        } else {
            cr_dbg.fxmap_cur &= ~8u;
        }
        CR_PROFX_START();
    }
#endif
    if (!done) {                                        /* the reverb here: its lines into its own half-rate pair */
#if XG_HALF
        for (i = 0; i < (n >> 1); i++)
            rl[i] = rr[i] = 0;
#endif
        if (f & 2u)
#if XG_HALF
            xf_reverb(send_r, wr_l, wr_r, n, 1, 0, &mk, rl, rr, 0);
#else
            xf_reverb(send_r, wr_l, wr_r, n, 1, 0, &mk, xf.t[2], xf.t[3], 0);
#endif
    }
#if XF_HALF && CR_FX_REVERB_DS == 2
#if CR_CPU1 && XF_SPLIT && !XF_SPLIT_HALF
    if (!done)                                          /* (core 1's job up-sampled its own) */
#endif
        xg_up(xf.up, &xf.up_live, rl, rr, wr_l, wr_r, n >> 1);
#endif
    (void)done; (void)mk; (void)wr_l; (void)wr_r;
    xg_join(mix_l, mix_r, n);
}
#endif

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
#if FELUCCA_PERFORM
    int perf;
#endif
#if FX_UA_STAGE
    if (ua_stage_on)
        for (i = 0; i < n * UA_CAP_CHANNELS; i++)
            ua_stage[i] = 0;                            /* (a silent part: mix_part leaves its channels) */
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    events_block(n);
#if FELUCCA_GRAIN
    grain_tick();                                       /* GRAIN's render requests and fades, on core 0 before any part
                                                         * renders (eng_grain.c, docs/GRAIN.md) */
#endif
#if FELUCCA_PERFORM
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
#endif
    CR_PROF(CRP_TICK);                                  /* (the engine tick before it: the mix_block shim) */
    i = 0;
#if CR_CPU1
    if (cr_dualcore_active() && c1_split_ok()) {
        mix_parts_split(n);
        i = 2;
    }
#endif
#if P5_SPLIT
    else if (P5_IS(&trk[0]) || P5_IS(&trk[P5_PART_BASS]))
        p5x_one_core();                                 /* (78 PEAK's counts; out of line: the ISR as it was laid) */
#endif
    for (; i < MIX_NPART; i++)
        mix_part(&trk[i], n);
    for (; i < NPART; i++)
        mix_part_idle(&trk[i], n);
#if FELUCCA_PERFORM
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);
#endif
#if CR_FX
    if (xfp[XP_RMODE] == 1)
        mix_fx_chain(n);                                /* (cr_fx.c THE ROUTING: Chain, the master filter inside it) */
    else {
    xg.key = 0;                                         /* (Parallel, 0.26 to the bit: a Chain chosen later starts from
                                                         * rest) */
#if CR_CHORUS_MONO
    if (CR_CHORUS_MONO == 1 || cho_mono) {              /* 0.18's return: one tap, the same to both sides (a test's
                                                         * reference: the sums are 0.18's, bit for bit) */
        fx_buses(send_c, send_d, send_r, wet, n);
        for (i = 0; i < n; i++) {
            mix_l[i] += wet[i];
            mix_r[i] += wet[i];
            wet[i] = 0;
        }
        xc_live = CHO_LEN;
    } else
#endif
#if CR_CPU1 && XF_SPLIT
    if (cr_dualcore_active() && !CR_C1_TEST_FX0)
        mix_fx_split(n);                                /* (3y50: the reverb on core 1, the chorus and the delay here) */
    else
#endif
    {
        xc_block(send_c, mix_l, mix_r, n);              /* (cr_fx.c: the chorus, two taps, into the mix) */
        xf_block(send_d, send_r, mix_l, mix_r, n);      /* (cr_fx.c: stereo, into the mix; the FX layer's master
                                                         * effects and the limiter after it, as the chorus) */
    }
#else
    fx_buses(send_c, send_d, send_r, wet, n);
#endif
#if CR_FX
    xq_block(mix_l, mix_r, n);                          /* (cr_fx.c: the master filter, 0.22: after the effects, before
                                                         * the master; Off, the default: nothing read or written) */
    }
#endif
    CR_PROF(CRP_FX);
#if FELUCCA_PERFORM
    if (perf) {
        perf_master(out, n);
        return;
    }
#endif
#ifdef FX_MIX_HOOK
    FX_MIX_HOOK(mix_l, mix_r, n);                       /* (tests/cr_align_test.c: a sample put into the mix) */
#endif
    out_master(out, mix_l, mix_r, CR_FX ? (const int32_t *)0 : wet, n);
#if FX_UA_STAGE
    if (ua_stage_on && fx_out_blk)                      /* Output Clean: the part pairs as late as the master pair */
        ua_parts_late(ua_stage, n);
    else
        ua_dl_on = 0;
#endif
}
