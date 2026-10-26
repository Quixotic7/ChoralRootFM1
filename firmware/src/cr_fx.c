/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot's delay and reverb (CR_FX 1; docs/FX.md), included by fx.c in the place of Felucca's mono tempo delay and
 * its ROOM / SPRING reverb, which stay as they are for Felucca's units (CR_FX 0: its goldens). Both take a mono
 * send bus of fx.c and add a stereo wet to the mix. Since 0.19 the chorus's return is here too (xc_block: two taps
 * on Felucca's line, left and right; SLOOP 2.6's design, credited there and in LICENSING.md).
 *
 * THE RATES (build flags; docs/FX.md "Half rate or full"). CR_FX_DELAY_DS 1: the delay runs at the full 44.1 kHz,
 * straight on the send and into the mix: its repeats are heard beside the dry sound and keep its band (0.18 .. 0.21;
 * since 0.22 the default is 2, the half rate in half the line: the owner's ear, 65 KB of POOL). CR_FX_REVERB_DS
 * 2: the reverb runs at 22.05 kHz between a half-band filter down (xf_down) and a pair of them up (xf_up): a
 * two-path polyphase all-pass (tools/fx_halfband.py: 2 + 2 first-order sections, one multiply each, -54 dB above
 * 12.5 kHz, flat to 9.5 kHz). Either can be built the other way (1 or 2) to compare.
 *
 * THE DELAY (xd_*), in the spirit of the Moog Matriarch's stereo analogue delay: two lines, L and R, of XD_N
 * samples each (0.74 s at the full rate). Each side's read is between two samples (linear; at rest the sample
 * itself) at a position that GLIDES to its target (a lag of ~46 ms, at most half a sample a sample: a new time
 * bends the pitch of what is in the line, as a tape or a BBD clock does, and never clicks) plus the WOW (one slow
 * and one faster sine, the same on both sides so that Spread 0 stays mono). The loop: the read -> a one-pole low-pass (TONE) -> a one-pole low cut
 * (40 Hz) -> the feedback gain -> + the input -> a saturator that is a straight line up to half of the line's range
 * and a soft knee above it (xd_sat) -> the line. STEREO: each side feeds itself. PING-PONG: the input goes into L
 * only, L feeds R and R feeds L. SPREAD is R's time as a part of L's (1 .. 1/2 free; 1:1, 3:4, 2:3, 1:2 synced).
 * SYNC: a division of the tempo (xf_bpm; a division longer than the line plays at half), else milliseconds.
 *
 * THE REVERB (xr_*): a feedback delay network of eight lines (after Jot and Chaigne 1991, and Stautner and Puckette
 * 1982), mixed by an 8 x 8 Hadamard matrix (three stages of butterflies, no multiply), behind a pre-delay and four
 * Schroeder all-pass diffusers (Schroeder 1962). Per line: a linear-interpolated read at a gliding length (SIZE: a
 * quarter to the whole of XR_LEN; four of the eight also swing on two slow sines, MOD), a one-pole low-pass (DAMP)
 * and the gain that gives every line the same RT60 (DECAY: 0.3 .. 72 s, the top position "Inf": 1 - 2^-13). The
 * input: a two-pole low cut (LOW CUT, 20 .. 500 Hz) in front of the pre-delay, scaled by (1 - g^2)^(1/4). The
 * output: four lines a side with alternating signs, and WIDTH (mid / side).
 *
 * THE ARITHMETIC (the first build rounded every product towards zero, and the delay's saturator was dsp.c's tanh
 * table, whose first step is 255 / 256: every pass round a loop took a unit or two off every sample, a tail fell in
 * a straight line instead of dying away, and at a short delay time, 47 passes a second, a repeat at -40 dB was gone
 * in half a second whatever the feedback: "the delay just drops out"). Now every product inside a loop KEEPS ITS
 * REMAINDER for the next sample (xf_rs: first-order error feedback, as fx.c's dc_block does), and the saturators are
 * exactly 1 below their knee: nothing is lost but what the feedback and the filters take, a tail decays
 * exponentially down to the last unit of the 16-bit line (-90 dBFS of it) and goes on below that as ever sparser
 * single units until none is left. The loops' gains are below 1 (feedback <= 0.988; the Hadamard matrix times
 * 181 / 512 < 1 / sqrt 8 and each line's gain <= 32764 / 32768), so they cannot grow.
 *
 * WHERE A TAIL ENDS: at one unit of the line. When a whole block has written nothing larger than +-1 (d_q, r_q: -90
 * dBFS of the 16-bit line; with the wet's gain under -77 dBFS at the output at full MASTER, every line of the reverb
 * at once: in practice -84), the loop's products are rounded towards zero for the next block instead of keeping
 * their remainders: the last single units are not sent round again and the lines reach exactly 0 one pass later. A
 * block that writes anything larger is back to the remainders at once. Nothing above one unit is ever cut.
 * NOTHING SOUNDS, NOTHING RUNS. A counter per effect (the samples since the last non-zero one it wrote or held) then
 * switches its loop off until a send is non-zero again: a loop is skipped only when every sample of its lines and
 * every state of its filters is 0.
 *
 * Fixed point: lines int16, states and sums int32, coefficients Q15 (the half-band's Q11), positions Q8 samples.
 * No division, no 64-bit and no call in the per-sample loops (the four XF_HOT functions: with CR_FX_RAM 1 they are
 * placed in RAM, out of the XIP cache's way); the coefficients are worked out when a parameter or the tempo changes
 * (xf_coefs). Memory, all in .pool: 2 x 32768 x 2 (delay) + 16384 x 2 + 4096 x 2 (reverb) + the state. */

#ifndef CR_FX_DELAY_DS
#define CR_FX_DELAY_DS 2                 /* 2: the delay at FS / 2 (0.22: the owner heard it on the unit 2026-10-14,
                                          * "sounded good"; 65 KB of POOL back, XD_N below); 1: at FS, 0.18 .. 0.21's
                                          * (CR_FX_DELAY_DS=1 ./build.sh) */
#endif
#ifndef CR_FX_REVERB_DS
#define CR_FX_REVERB_DS 2                /* 2: the reverb at FS / 2; 1: at FS (twice the lines' memory and work) */
#endif
#ifndef CR_FX_LITE
#define CR_FX_LITE 0                     /* 1: the lighter reverb, to hear beside the other: two diffusers of the four
                                          * and one swinging line a side of the two (docs/FX.md "The cost on the unit") */
#endif
#define XR_NAP (CR_FX_LITE ? 2u : 4u)
#define XR_NMOD (CR_FX_LITE ? 2u : 4u)
#ifndef CR_FX_RAM
#define CR_FX_RAM 0                      /* 1 (device builds only): the four per-sample loops in .ram_text, called
                                          * through a pointer as the flash driver is: no XIP cache miss in them */
#endif
#include "cr_ram.h"
#if CR_FX_RAM && CR_RAM_FX
#error "cr_fx.c: CR_FX_RAM (the four loops alone, 0.18's measurement) or CR_RAM_FX (the stage: cr_ram.h), not both"
#endif
#if (CR_FX_DELAY_DS != 1 && CR_FX_DELAY_DS != 2) || (CR_FX_REVERB_DS != 1 && CR_FX_REVERB_DS != 2)
#error "cr_fx.c: CR_FX_DELAY_DS and CR_FX_REVERB_DS are 1 or 2"
#endif
#define XF_HALF (CR_FX_DELAY_DS == 2 || CR_FX_REVERB_DS == 2)   /* something runs at the half rate */
#define XF_AI static inline __attribute__((always_inline))
#if CR_FX_RAM                            /* (as hal/fm1_flash.h's RAMFN and FL_FAR: XIP and RAM are too far apart for a
                                          * direct call, so through a pointer the compiler cannot fold back) */
#define XF_HOT static __attribute__((section(".ram_text"), noinline, used))
static inline void *xf_far(void *p) { void *volatile q = p; return q; }
#define XF_CALL(fn) ((__typeof__(&fn))xf_far((void *)&fn))
#else
#define XF_HOT static __attribute__((noinline))
#define XF_CALL(fn) fn
#endif

/* ------------------------------------------------------------- the parameters --- */
/* global (the session's FX block, cr_settings.h CSS_FXP_*), as the sends are (cr_ui.c): no sound load touches them */
enum { XP_DTIME,                 /* the delay's time, free: 0..127 = 16 ms .. XD_MS_MAX (xf_ms) */
       XP_DDIV,                  /* .. synced: XF_DIV's index */
       XP_DSPREAD,               /* R's time as a part of L's, free: 0..127 = 1 .. 0.5 */
       XP_DRATIO,                /* .. synced: 0 1:1, 1 3:4, 2 2:3, 3 1:2 */
       XP_DFDBK,                 /* feedback 0..127 = 0 .. 0.988 */
       XP_DSYNC,                 /* 0 free (ms), 1 the tempo's divisions */
       XP_DMODE,                 /* 0 stereo, 1 ping-pong */
       XP_DTONE,                 /* the loop's low-pass: 0..127 = 400 Hz .. 22 kHz (an octave every 22) */
       XP_DWOW,                  /* the time's wobble: 0..127 = 0 .. 2 ms */
       XP_RSIZE,                 /* the lines' lengths: 0..127 = 1/4 .. 1 of XR_LEN */
       XP_RDAMP,                 /* the loop's low-pass: 0 open .. 127 ~660 Hz */
       XP_RDECAY,                /* RT60: 0..126 = 0.3 .. 72 s, 127 "Inf" */
       XP_RPRE,                  /* pre-delay: 0..127 = 0 .. 138 ms */
       XP_RMOD,                  /* the modulated lines' swing: 0..127 = 0 .. 0.9 ms */
       XP_RLOW,                  /* the input's low cut: 0..127 = 20 .. 500 Hz */
       XP_RWIDTH,                /* 0 mono .. 127 full */
       XP_CWIDTH,                /* the chorus's right tap: its LFO 0 .. a quarter turn ahead of the left's (0: both
                                  * ears the same tap, the mono return of 0.18; 127: a quarter). KNOB 3 of the
                                  * Chorus page since 0.21 (cr_ui.c CU_FXP), kept in the session (CSS_FXP_XC_WIDTH);
                                  * xc_block glides to it */
       XP_FLOW,                  /* THE MASTER FILTER's (0.22, "The master filter" below; the FX layer's Filter page):
                                  * the low cut, 0 Off, 1..127 = 20 Hz .. 2 kHz (xq_low_hz) */
       XP_FHIGH,                 /* .. the high cut, 0..126 = 200 Hz .. 16 kHz (xq_high_hz), 127 Off */
       XP_FSLOPE,                /* .. 0: 6 dB an octave (one stage a cut), 1: 12 dB (two) */
       XP_FON,                   /* .. 0 Off, 1 On (the cell on KNOB 4; a cutoff turned switches it on) */
       XP_ORDER,                 /* THE ROUTING (GitHub issue 21, second design; "The routing" below; the FX layer's
                                  * Routing page): the order of the FOUR chain blocks, XF_ORD's index 0..23 (0: Chorus,
                                  * Delay, Reverb, Filter). Kept in both modes; in Parallel only a picture */
       XP_RMODE,                 /* .. the mode: 0 Parallel (the buses fed by the parts' sends, bit for bit 0.26), 1
                                  * Chain (a serial path through the four, the chord part's sends the amounts). Tags
                                  * 35 / 36 of the session; b1e1222's Order 0..5 and Feed (36) never shipped (one dev
                                  * unit): an old Feed > 1 is out of range and falls to Parallel */
       XP_N };
enum { XFE_CHORUS, XFE_DELAY, XFE_REVERB, XFE_FILTER, XFE_N };   /* the blocks a chain position holds (XF_ORD) */
#define XFE_NFX XFE_FILTER        /* the three effects (the filter is not a bus) */
static const uint8_t XF_ORD[24][XFE_N] = {            /* XP_ORDER -> the block at position 0..3, lexicographic */
    {XFE_CHORUS, XFE_DELAY, XFE_REVERB, XFE_FILTER}, {XFE_CHORUS, XFE_DELAY, XFE_FILTER, XFE_REVERB}, {XFE_CHORUS, XFE_REVERB, XFE_DELAY, XFE_FILTER},
    {XFE_CHORUS, XFE_REVERB, XFE_FILTER, XFE_DELAY}, {XFE_CHORUS, XFE_FILTER, XFE_DELAY, XFE_REVERB}, {XFE_CHORUS, XFE_FILTER, XFE_REVERB, XFE_DELAY},
    {XFE_DELAY, XFE_CHORUS, XFE_REVERB, XFE_FILTER}, {XFE_DELAY, XFE_CHORUS, XFE_FILTER, XFE_REVERB}, {XFE_DELAY, XFE_REVERB, XFE_CHORUS, XFE_FILTER},
    {XFE_DELAY, XFE_REVERB, XFE_FILTER, XFE_CHORUS}, {XFE_DELAY, XFE_FILTER, XFE_CHORUS, XFE_REVERB}, {XFE_DELAY, XFE_FILTER, XFE_REVERB, XFE_CHORUS},
    {XFE_REVERB, XFE_CHORUS, XFE_DELAY, XFE_FILTER}, {XFE_REVERB, XFE_CHORUS, XFE_FILTER, XFE_DELAY}, {XFE_REVERB, XFE_DELAY, XFE_CHORUS, XFE_FILTER},
    {XFE_REVERB, XFE_DELAY, XFE_FILTER, XFE_CHORUS}, {XFE_REVERB, XFE_FILTER, XFE_CHORUS, XFE_DELAY}, {XFE_REVERB, XFE_FILTER, XFE_DELAY, XFE_CHORUS},
    {XFE_FILTER, XFE_CHORUS, XFE_DELAY, XFE_REVERB}, {XFE_FILTER, XFE_CHORUS, XFE_REVERB, XFE_DELAY}, {XFE_FILTER, XFE_DELAY, XFE_CHORUS, XFE_REVERB},
    {XFE_FILTER, XFE_DELAY, XFE_REVERB, XFE_CHORUS}, {XFE_FILTER, XFE_REVERB, XFE_CHORUS, XFE_DELAY}, {XFE_FILTER, XFE_REVERB, XFE_DELAY, XFE_CHORUS}};
#define XP_NFX XP_FLOW            /* the delay's, the reverb's and the chorus's (xf_prep's hash); the filter's own after */
static const struct { int16_t min, max, def; } XFP[XP_N] = {
    [XP_DTIME] = {0, 127, 91}, [XP_DDIV] = {0, 12, 6}, [XP_DSPREAD] = {0, 127, 0}, [XP_DRATIO] = {0, 3, 0},
    [XP_DFDBK] = {0, 127, 58}, [XP_DSYNC] = {0, 1, 1}, [XP_DMODE] = {0, 1, 1}, [XP_DTONE] = {0, 127, 70},
    [XP_DWOW] = {0, 127, 24}, [XP_RSIZE] = {0, 127, 88}, [XP_RDAMP] = {0, 127, 56}, [XP_RDECAY] = {0, 127, 52},
    [XP_RPRE] = {0, 127, 16}, [XP_RMOD] = {0, 127, 56}, [XP_RLOW] = {0, 127, 60}, [XP_RWIDTH] = {0, 127, 127},
    [XP_CWIDTH] = {0, 127, 127},
    [XP_FLOW] = {0, 127, 0}, [XP_FHIGH] = {0, 127, 127}, [XP_FSLOPE] = {0, 1, 1}, [XP_FON] = {0, 1, 0},
    [XP_ORDER] = {0, 23, 0}, [XP_RMODE] = {0, 1, 0}};
static int16_t xfp[XP_N];                 /* the values (xf_defaults at power-on, then the session's) */
static uint16_t xf_bpm = 120;             /* the tempo the synced delay follows (cr_ui.c: the engine's, 20..300) */

/* the synced divisions, in 48ths of a quarter note; cr_ui.c's names in the same order */
#define XF_NDIV 13u
static const uint8_t XF_DIV[XF_NDIV] = {6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96};
static const uint8_t XF_RATIO[4][2] = {{1, 1}, {3, 4}, {2, 3}, {1, 2}};

_Static_assert(CTL == 32, "cr_fx.c: a block is 32 samples (the ramps' shifts)");
#define XD_FS (FS / CR_FX_DELAY_DS)       /* the delay's rate, its samples a block and their log2 */
#define XD_NB (CTL / CR_FX_DELAY_DS)
#define XD_SH (CR_FX_DELAY_DS == 1 ? 5 : 4)
#define XR_FS (FS / CR_FX_REVERB_DS)
#define XR_NB (CTL / CR_FX_REVERB_DS)
#define XR_SH (CR_FX_REVERB_DS == 1 ? 5 : 4)
#define XR_X (2u / CR_FX_REVERB_DS)       /* the reverb's lengths against the half-rate table below */
#define XF_NH (CTL / 2u)                  /* a block at the half rate */
#define XF_IN 1u                          /* the lines' scale: the send >> 1 (xd_sat's comment) */

#ifndef XD_N
#define XD_N (32768u / CR_FX_DELAY_DS)    /* a delay line, samples (int16): 0.743 s at 44.1 kHz; at the half rate
                                           * (CR_FX_DELAY_DS 2) half the samples for the same 0.743 s, 65 KB of POOL
                                           * back (XD_N=32768 with it: 1.49 s in the same memory) */
#endif
#define XD_M (XD_N - 1u)
#define XD_MAX (XD_N - 128u / CR_FX_DELAY_DS)   /* the longest time: the wow's swing (<= 89 samples at 44.1 kHz: a
                                           * time, so half as many at the half rate) and the interpolator's points
                                           * above it */
#define XD_MS_MAX (XD_MAX * 1000u / XD_FS > 1000u ? 1000u : XD_MAX * 1000u / XD_FS)   /* 740 ms (half rate: 1000) */

static void xf_defaults(void)
{
    uint32_t i;
    for (i = 0; i < XP_N; i++)
        xfp[i] = XFP[i].def;
}

/* base << (p / per), linear within the octave: an exponential knob without a table */
static uint32_t xf_oct(uint32_t base, uint32_t p, uint32_t per)
{
    return ((base << (p / per)) * (per + p % per)) / per;
}
static uint32_t xf_ms(uint32_t p)         /* XP_DTIME -> ms: 16 .. XD_MS_MAX */
{
    uint32_t ms = xf_oct(16, p, 23);
    return ms > XD_MS_MAX ? XD_MS_MAX : ms;
}
static uint32_t xf_rt_ms(uint32_t p)      /* XP_RDECAY -> RT60 in ms (127: "Inf", not this) */
{
    return xf_oct(300, p, 16);
}
static uint32_t xf_lowcut_hz(uint32_t p)  /* XP_RLOW -> Hz: 20 .. 500 */
{
    return 20u + p * p * 480u / 16129u;
}
static uint32_t xf_pre_ms(uint32_t p)     /* XP_RPRE -> ms: 0 .. 138 (24 samples at 22.05 kHz a step) */
{
    return p * 24000u / 22050u;
}
static uint32_t xq_low_hz(uint32_t p)     /* XP_FLOW 1..127 -> Hz: 20 .. 2088 (an octave every 19 steps) */
{
    return xf_oct(20, p ? p - 1u : 0u, 19);
}
static uint32_t xq_high_hz(uint32_t p)    /* XP_FHIGH 0..126 -> Hz: 200 .. 16640 (an octave every 20 steps) */
{
    return xf_oct(200, p > 126u ? 126u : p, 20);
}

/* ------------------------------------------------------------------ the state --- */
static int16_t xd_buf[2][XD_N] __attribute__((section(".pool")));

/* the reverb's eight lines at SIZE 127, in samples at 22.05 kHz (57 .. 125 ms; primes, a ratio of ~1.12 from one to
 * the next; x XR_X at the full rate), in ONE ring: line i writes at w + base[i] and reads up to its length and
 * XR_GUARD behind it */
#define XR_N (16384u * XR_X)
#define XR_M (XR_N - 1u)
#define XR_GUARD 24u                      /* the modulation's swing (<= 20), the interpolator's point, a spare */
static const uint16_t XR_LEN[8] = {1259, 1433, 1607, 1801, 2003, 2221, 2477, 2767};
static const uint16_t XR_BASE[8] = {1296, 2768, 4400, 6240, 8272, 10528, 13040, 15840};   /* (multiples of a block,
                                           * 16: a block's writes never wrap; tests/cr_fx_test.c checks the gaps) */
_Static_assert(15840 + 16 <= 16384 + 1296 - 1259 - 24, "the eight lines and their guards fit the ring");
static int16_t xr_buf[XR_N] __attribute__((section(".pool")));
/* the pre-delay (behind the write index) and the four diffusers (ahead of it), one ring */
#define XR_PN (4096u * XR_X)
#define XR_PM (XR_PN - 1u)
#define XR_PRE_MAX (127u * 24u)           /* 3048 samples at 22.05 kHz: 138 ms */
static const uint16_t XR_AP[4] = {113, 173, 263, 397};          /* 5.1, 7.8, 11.9, 18.0 ms */
static const uint16_t XR_APB[4] = {144, 336, 624, 1040};        /* their write offsets: multiples of a block (16: a
                                           * block's writes never wrap), and each reads a block clear of what the
                                           * stage before it writes (xr_run goes stage by stage over a block) */
_Static_assert(144 - 113 >= 16 && 336 - 173 >= 144 + 16 && 624 - 263 >= 336 + 16 && 1040 - 397 >= 624 + 16,
               "a diffuser's read is a block clear of the writes before it");
_Static_assert(1040 + XR_PRE_MAX + 2 <= 4096, "the pre-delay and the diffusers fit their ring (the pre-delay's read"
               " of a block comes before the last diffuser's write of it)");
_Static_assert(XR_NB == 16u * XR_X && XD_NB == (1u << XD_SH) && XR_NB == (1u << XR_SH), "a block of the reverb, of the delay");
static int16_t xr_pbuf[XR_PN] __attribute__((section(".pool")));

typedef struct { int32_t x[4], y[4], e[4]; } xf_hb_t;           /* a half-band filter: each section's last in, out
                                                                 * and its product's remainder */
static struct {
    /* the coefficients (xf_coefs) */
    uint32_t hash;
    int32_t d_tgt[2], d_fb, d_k, d_kh, d_wow;   /* delay: the sides' times (Q8), feedback, the tone's and the low
                                                 * cut's k (Q15), the wow's swing (Q8) */
    uint8_t d_ping, on;                   /* .. ping-pong; on: the coefficients are set (power-on: no glides) */
    uint8_t d_q, r_q;                     /* the delay's, the reverb's last block wrote nothing above one unit (above) */
    int32_t r_tgt[8], r_g[8];             /* reverb: the lines' lengths (Q8) and gains (Q15) */
    int32_t r_k, r_ig, r_lk, r_pre, r_mod, r_w;   /* damp's k (Q15), the input gain (Q15), the low cut's k; pre-delay,
                                                   * swing (Q8); width (Q8) */
    uint16_t r_base[8], r_step[8], r_ap[4], r_apb[4];   /* XR_BASE, the glide's step, XR_AP, XR_APB at this rate (in
                                                         * RAM: the loops read no constant from the flash) */
    /* the delay */
    uint32_t d_w, d_live;                 /* the write index; samples until the lines are known silent */
    int32_t d_cur[2], d_wv, d_wt;         /* the gliding times (Q8); the wow now, its target */
    int32_t d_lp[2], d_hp[2], d_e[2][3];  /* the filters; the three products' remainders a side */
    int32_t d_fbc, d_kc, d_pm;            /* the feedback and the tone's k in use (eased a block at a time);
                                           * ping-pong's part of the routing, 0 .. 256 (eased a sample at a time: a
                                           * mode change writes no step into the lines) */
    uint32_t d_ph[2];
    /* the reverb */
    uint32_t r_w0, r_pw, r_live;
    int32_t r_cur[8], r_mv[4], r_mt[4];   /* the gliding lengths; the four swings now, their targets */
    int32_t r_lp[8], r_e1[8], r_e2[8];    /* the lines' low-passes; their products' and the lines'
                                                   * remainders */
    int32_t r_hp[2], r_eh[2], r_pcur;
    int32_t r_gc[8], r_igc, r_kc, r_wc;   /* the gains, the input gain, damp's k (eased a block at a time), the width
                                           * (a sample) */
    uint32_t r_ph[2];
    /* the half rate */
    xf_hb_t dn[2], up[2];
    uint32_t up_live;
    int32_t t[4][XF_NH];                  /* a block's sends at the half rate, and its wet */
    int32_t tf[2][CTL];                   /* xd_run: a block of each side's filtered read */
    int32_t tr[CTL * 8u], tx[CTL];        /* xr_run: a block of the eight lines, sample by sample (a line's read, then
                                           * what goes back into it: the mix has its eight side by side), and of the
                                           * diffused input */
} xf __attribute__((section(".pool")));

XF_AI int32_t xf_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
XF_AI int32_t xf_tz15(int32_t v) { return (v + ((v >> 31) & 32767)) >> 15; }   /* / 32768 towards 0 (feed-forward) */
XF_AI int32_t xf_tz14(int32_t v) { return (v + ((v >> 31) & 16383)) >> 14; }
XF_AI int32_t xf_tz8(int32_t v) { return (v + ((v >> 31) & 255)) >> 8; }
XF_AI int32_t xf_tz3(int32_t v) { return (v + ((v >> 31) & 7)) >> 3; }
/* v / 2^sh with the remainder kept in *e for the next sample (0 .. 2^sh - 1): nothing is lost over time, and with
 * v at 0 the result is 0 (the remainder stays where it is) */
XF_AI int32_t xf_rs(int32_t v, int32_t *e, uint32_t sh)
{
    v += *e;
    *e = v & (int32_t)((1u << sh) - 1u);
    return v >> sh;
}
/* a one-pole low-pass step, y += k (x - y) with its remainder: y reaches x exactly (|x|, |y| <= 32767 and
 * k <= 32767: 65534 x 32767 + 32767 < 2^31) */
XF_AI int32_t xf_lp(int32_t y, int32_t x, int32_t k, int32_t *e) { return y + xf_rs((x - y) * k, e, 15); }

/* ------------------------------------------------------------- the half-band --- */
/* Q11 (tools/fx_halfband.py: the search's Q13 values / 4): path 0's two sections 199 and 1264, path 1's 695 and
 * 1785. They run on the sends and the wet at their full resolution (|x| <= 131068 = 4 x 32767): the second section
 * of path 1 sees at most (1 + 2 a) of that and its difference x - y at most 6.3 x 131068, times 1785 < 2^31. Each
 * product keeps its remainder (no dead band, no offset) */
XF_AI int32_t xf_ap(xf_hb_t *h, uint32_t k, int32_t a, int32_t x)
{
    int32_t y = xf_rs((x - h->y[k]) * a, &h->e[k], 11) + h->x[k];
    h->x[k] = x;
    h->y[k] = y;
    return y;
}
/* n samples of a send -> n / 2 at half its level (the lines' scale: XF_IN), saturated */
CR_HOT_FX XF_HOT void xf_down(xf_hb_t *h, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t a = xf_clamp(in[i], -131068, 131068), b = xf_clamp(in[i + 1u], -131068, 131068);
        out[i >> 1] = xf_clamp((xf_ap(h, 1, 1264, xf_ap(h, 0, 199, b)) + xf_ap(h, 3, 1785, xf_ap(h, 2, 695, a))) >> (1u + XF_IN),
                               -32767, 32767);
    }
}
/* nh samples of wet (saturated at +-131068) -> 2 nh, added to out */
CR_HOT_FX XF_HOT void xf_up(xf_hb_t *h, const int32_t *in, int32_t *out, uint32_t nh)
{
    uint32_t j;
    xf_hb_t s = *h;                                     /* (the states in registers for the block: 49 instructions a
                                                         * sample for 57; the same in xf_down is 67 for 65) */
    for (j = 0; j < nh; j++) {
        int32_t x = xf_clamp(in[j], -131068, 131068);
        out[2u * j] += xf_ap(&s, 1, 1264, xf_ap(&s, 0, 199, x));
        out[2u * j + 1u] += xf_ap(&s, 3, 1785, xf_ap(&s, 2, 695, x));
    }
    *h = s;
}

/* ------------------------------------------------------------ coefficients --- */
/* e^-(y / 65536), Q15: e^-n from a table, the fraction as (1 - u + u^2 / 2 - u^3 / 6)^4 with u a quarter of it */
static int32_t xf_expn(uint32_t y)
{
    static const uint16_t E[8] = {32768, 12055, 4435, 1631, 600, 221, 81, 30};
    uint32_t n = y >> 16, u = (y & 0xFFFFu) >> 2, u2 = (u * u) >> 16, p;
    if (n >= 8u)
        return 0;
    p = 65536u - u + (u2 >> 1) - (u2 * u / 6u >> 16);
    p = (uint32_t)(((uint64_t)p * p) >> 16);
    p = (uint32_t)(((uint64_t)p * p) >> 16);
    p = (p * E[n]) >> 16;
    return p > 32767u ? 32767 : (int32_t)p;
}
static uint32_t xf_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    for (; b; b >>= 2) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
    }
    return r;
}
/* a one-pole's k (Q15) for a corner of hz at the rate fs: 1 - e^(-2 pi hz / fs) (2 pi x 65536 = 16 x 25736) */
static int32_t xf_k(uint32_t hz, uint32_t fs)
{
    int32_t k = 32768 - xf_expn(hz * 25736u / (fs / 16u));
    return k > 32767 ? 32767 : k < 1 ? 1 : k;
}
#define XR_G_INF 32764                    /* DECAY 127: 1 - 2^-13 a pass: an RT60 of about an hour */

/* the delay's two times in its own samples (the unit tests' table): L, R */
static void xf_delay_times(uint32_t *l, uint32_t *r)
{
    uint32_t t, num = 1, den = 1;
    if (xfp[XP_DSYNC]) {
        uint32_t bpm = xf_bpm < 20u ? 20u : xf_bpm > 300u ? 300u : xf_bpm;
        const uint8_t *q = XF_RATIO[xfp[XP_DRATIO] & 3];
        /* a quarter note is XD_FS * 60 / bpm; the division 48ths of it, rounded */
        t = (XD_FS * 60u * XF_DIV[(uint32_t)xfp[XP_DDIV] % XF_NDIV] + 24u * bpm) / (48u * bpm);
        while (t > XD_MAX)
            t >>= 1;                       /* longer than the line: at half (1/4 at 60 BPM plays as 1/8) */
        num = q[0];
        den = q[1];
    } else {
        t = xf_ms((uint32_t)xfp[XP_DTIME]) * XD_FS / 1000u;
        num = 254u - (uint32_t)xfp[XP_DSPREAD];
        den = 254u;
    }
    *l = t;
    *r = (t * num + den / 2u) / den;
}
/* the lines' lengths at XP_RSIZE, Q8: 64 .. 254 / 256 of XR_LEN */
static uint32_t xr_size_q8(void) { return xf_oct(64, (uint32_t)xfp[XP_RSIZE], 64); }

static __attribute__((noinline)) void xf_coefs(void)   /* (when a parameter moves: its divides out of xf_block) */
{
    uint32_t l, r, i, k, sz = xr_size_q8(), e;
    int32_t gm;
    xf_delay_times(&l, &r);
    xf.d_tgt[0] = (int32_t)(l << 8);
    xf.d_tgt[1] = (int32_t)(r << 8);
    xf.d_fb = xfp[XP_DFDBK] * 255;
    xf.d_k = xf_k(xf_oct(400, (uint32_t)xfp[XP_DTONE], 22), XD_FS);
    xf.d_kh = xf_k(40, XD_FS);
    xf.d_wow = xfp[XP_DWOW] * (int32_t)(XD_FS * 2u * 256u / 1000u / 127u);   /* 0 .. 2 ms, Q8 samples */
    xf.d_ping = xfp[XP_DMODE] != 0;

    for (i = 0; i < 8u; i++) {
        int32_t t = (int32_t)((XR_LEN[i] * XR_X * sz + 128u) & ~255u);   /* whole samples: a line at rest reads one
                                                                           * sample, and loses no treble to the
                                                                           * interpolation */
        xf.r_tgt[i] = t;
        /* g = 10^(-3 L / RT60) = e^-(6.9078 L / RT60), L and RT60 in samples (6.9078 x 65536 = 452709) */
        xf.r_g[i] = xfp[XP_RDECAY] >= 127 ? XR_G_INF
                  : xf_expn((uint32_t)(((uint32_t)t >> 8) * (452709u / XR_X) /
                                       (xf_rt_ms((uint32_t)xfp[XP_RDECAY]) * (XR_FS / 50u) / (20u * XR_X))));
        if (xf.r_g[i] > XR_G_INF) xf.r_g[i] = XR_G_INF;
        xf.r_base[i] = (uint16_t)(XR_BASE[i] * XR_X);
        xf.r_step[i] = (uint16_t)(XR_LEN[i] >> 6);
    }
    for (i = 0; i < 4u; i++) {
        xf.r_ap[i] = (uint16_t)(XR_AP[i] * XR_X);
        xf.r_apb[i] = (uint16_t)(XR_APB[i] * XR_X);
    }
    k = (uint32_t)xfp[XP_RDAMP];           /* the corner: 12 kHz, an octave down every 30, to 660 Hz; 0: open. At
                                            * DECAY Inf the damping is out of the loop: a one-pole takes a little of
                                            * everything at every pass (at 3.4 kHz: 1 kHz is gone in 15 s), and Inf
                                            * holds what is in it */
    xf.r_k = k && xfp[XP_RDECAY] < 127 ? xf_k((12000u >> (k / 30u)) - (12000u >> (k / 30u + 1u)) * (k % 30u) / 30u, XR_FS) : 32767;
    gm = xf.r_g[4];
    e = 32768u - (uint32_t)((gm * gm) >> 15);                    /* 1 - g^2, Q15 */
    xf.r_ig = (int32_t)xf_isqrt(xf_isqrt(e << 15) << 15);         /* its fourth root, Q15 */
    if (xf.r_ig > 32767) xf.r_ig = 32767;
    xf.r_lk = xf_k(xf_lowcut_hz((uint32_t)xfp[XP_RLOW]), XR_FS);
    xf.r_pre = xfp[XP_RPRE] * 24 * 256 * (int32_t)XR_X;
    xf.r_mod = xfp[XP_RMOD] * 40 * (int32_t)XR_X;
    xf.r_w = (xfp[XP_RWIDTH] * 256 + 63) / 127;
}

/* ------------------------------------------------------------------ the delay --- */
/* THE LINES' SCALE: half the send (>> XF_IN). A loud chord's send is ~+-40000 (the limiter's threshold is 18000): 20000
 * in the line, a little into the delay's knee, as Felucca's delay took it; a quarter (the first build) left the wet 12 dB
 * of make-up gain and the 16-bit line's noise that much nearer: -85 dBFS at the output, so: -91 */
/* the line's saturator: itself up to +-16384 (half of the line's range),
 * then a parabola whose slope falls from 1 to 0 at +-49150 in, +-32767 out: no edge, and nothing lost below it */
XF_AI int32_t xd_sat(int32_t v)
{
    int32_t a;
    if ((uint32_t)(v + 16384) <= 32768u)                /* (nearly always: one compare) */
        return v;
    a = v < 0 ? -v : v;
    a = a >= 49150 ? 32767 : a - (int32_t)(((uint32_t)(a - 16384) * (uint32_t)(a - 16384)) >> 16);
    if (a > 32767)
        a = 32767;
    return v < 0 ? -a : a;
}
/* the wet's level, Q12 (tests/cr_fx_test.c, against Felucca's delay at its defaults and the same send): in Stereo each
 * side plays a repeat as Felucca's mono delay played it on both; in ping-pong the repeat is on one side only, so
 * 3 dB up (XD_WET + 256 XD_WPP): the same power in the room */
#define XD_WET 6120
#define XD_WPP 10
#define XD_DMIN ((int32_t)((CTL + 4u) << 8))   /* the shortest read: a block and the interpolator's point behind the write
                                                * index (the loop below reads a whole block before it writes one) */
/* a buffer (a line, a block's scratch, the caller's block) as a pointer the compiler keeps in a register: told the
 * address (a constant: the global's, or the one caller's argument), it builds it again in every pass of a loop (two to
 * four instructions a sample, build/choralroot.dis) */
XF_AI void *xf_reg(const void *p) { const void *volatile q = p; return (void *)q; }
/* how many of `left` samples a ring of m + 1 gives from index p before it wraps: the loops below walk a ring in one or
 * two straight pieces (a pointer and an index, no mask in the sample loop) */
XF_AI uint32_t xf_seg(uint32_t p, uint32_t m, uint32_t left)
{
    uint32_t k = m + 1u - (p & m);
    return k < left ? k : left;
}
/* n samples of the send (>> sh: XF_IN, or 0 behind the down-sampler) -> the stereo wet, added to ol, orr. n is a
 * block of the delay (XD_NB = 1 << XD_SH), and the write index a multiple of it: a block's writes never wrap.
 * THE SHAPE OF THE LOOPS (0.18's third build. The unit runs ~200 instructions a microsecond, and the first builds'
 * per-sample form was ~250 instructions a sample there: docs/FX.md "The cost on the unit"): a block at a time and a
 * side at a time, every state in a register: first each side's read and its two filters over the whole block (xf.tf),
 * then both sides' feedback, saturator and write. The lines are longer than a block, so a block's reads never see its
 * own writes: the samples are the per-sample loop's. A side at rest (its time reached, no wow, a whole number of
 * samples) reads straight from the line; a side that moves reads between two samples (linear: the first builds' cubic
 * was 35 instructions a read), its time a ramp over the block: the glide's step and the wow's, once a block */
CR_HOT_FX XF_HOT void xd_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t n, uint32_t sh)
{
    uint32_t w = xf.d_w, j, k, e;
    int32_t pk = 0, st = 0, fb, kc, kh = xf.d_kh, pt = xf.d_ping ? 256 : 0, q = xf.d_q, pm = xf.d_pm;
    int32_t wv = xf.d_wv, dw = xf.d_wt - wv;            /* the wow: to this block's target over the block */
    xf.d_fbc += xf_clamp(xf.d_fb - xf.d_fbc, -512, 512);
    xf.d_kc += xf_clamp(xf.d_k - xf.d_kc, -96, 96);
    fb = xf.d_fbc;
    kc = xf.d_kc;
    xf.d_wv = xf.d_wt;
    in = xf_reg(in);
    for (k = 0; k < 2u; k++) {
        const int16_t *b = xf_reg(xd_buf[k]);
        int32_t *f = xf_reg(xf.tf[k]), lp = xf.d_lp[k], hp = xf.d_hp[k], e0 = xf.d_e[k][0], e1 = xf.d_e[k][1];
        int32_t cur = xf.d_cur[k], d = xf.d_tgt[k] - cur, s, t, a;
        if (d) {                                        /* the glide: a lag of 46 ms, at most half a sample a sample
                                                         * (the last 8 samples of the way: 1 / 256 a sample) */
            s = d >> (6 + XD_SH);
            s = s > 128 ? 128 : s < -128 ? -128 : s;
            d = s ? s * (int32_t)n : d > (int32_t)n ? (int32_t)n : d < -(int32_t)n ? -(int32_t)n : d;
            xf.d_cur[k] = cur + d;
        }
        s = d + dw;                                     /* the read's whole move over this block, Q8 */
        cur += wv;
        if (!s && !(cur & 255) && cur >= XD_DMIN) {     /* at rest */
            uint32_t p = w - ((uint32_t)cur >> 8);
            for (j = 0; j < n;) {
                const int16_t *bp = b + ((p + j) & XD_M) - j;
                e = j + xf_seg(p + j, XD_M, n - j);
                for (; j < e; j++) {
                    a = bp[j];
                    t = (a - lp) * kc + e0;             /* the tone's low-pass, its remainder kept (xf_lp) */
                    e0 = t & 32767;
                    lp += t >> 15;
                    t = (lp - hp) * kh + e1;            /* the low cut */
                    e1 = t & 32767;
                    hp += t >> 15;
                    f[j] = lp - hp;
                }
            }
        } else if (((uint32_t)(cur + (s >> XD_SH)) >> 8) == ((uint32_t)(cur + s) >> 8) && cur + s >= XD_DMIN &&
                   cur + (s >> XD_SH) >= XD_DMIN) {
            /* drifting (the wow: an eighth of a sample a block at its most): between the same two samples all
             * block, the fraction a ramp */
            int32_t acc = cur << XD_SH;                 /* (< 2^23 << 5) */
            uint32_t p = w - ((uint32_t)(cur + s) >> 8);
            for (j = 0; j < n;) {
                const int16_t *bp = b + ((p + j) & XD_M) - j, *bq = b + ((p + j - 1u) & XD_M) - j;
                e = (p + j) & XD_M ? j + xf_seg(p + j, XD_M, n - j) : j + 1u;   /* (the ring's first sample: its
                                                         * neighbour is the last) */
                for (; j < e; j++) {
                    acc += s;
                    a = bp[j];
                    a += xf_tz8((bq[j] - a) * ((acc >> XD_SH) & 255));
                    t = (a - lp) * kc + e0;
                    e0 = t & 32767;
                    lp += t >> 15;
                    t = (lp - hp) * kh + e1;
                    e1 = t & 32767;
                    hp += t >> 15;
                    f[j] = lp - hp;
                }
            }
        } else {                                        /* gliding (TIME, a division, the tempo): anywhere */
            int32_t acc = cur << XD_SH;
            for (j = 0; j < n; j++) {
                uint32_t p;
                acc += s;
                d = acc >> XD_SH;
                if (d < XD_DMIN)
                    d = XD_DMIN;
                p = w + j - ((uint32_t)d >> 8);
                a = b[p & XD_M];
                a += xf_tz8((b[(p - 1u) & XD_M] - a) * (d & 255));
                t = (a - lp) * kc + e0;
                e0 = t & 32767;
                lp += t >> 15;
                t = (lp - hp) * kh + e1;
                e1 = t & 32767;
                hp += t >> 15;
                f[j] = lp - hp;
            }
        }
        xf.d_lp[k] = lp;
        xf.d_hp[k] = hp;
        xf.d_e[k][0] = e0;
        xf.d_e[k][1] = e1;
        st |= lp | hp;
    }
    {
        const int32_t *f0 = xf_reg(xf.tf[0]), *f1 = f0 + CTL;
        int16_t *b0 = xf_reg(&xd_buf[0][w & XD_M]), *b1 = b0 + XD_N;
        int32_t e0 = xf.d_e[0][2], e1 = xf.d_e[1][2], x, t, wl, wr;
        if (pm == pt && !q) {                           /* the mode at rest: stereo (each side its own return) .. */
            int32_t g = XD_WET + pm * XD_WPP;           /* (65534 x 8680 < 2^31) */
            int32_t xm = pm ? 0 : -1;                   /* (R takes the input in stereo only) */
            if (pm) {                                   /* .. or ping-pong (L: the input and R's return; R: L's) */
                const int32_t *f2 = f0;
                int32_t *o2 = ol;
                f0 = f1, f1 = f2;
                ol = orr, orr = o2;                     /* (f0's side is heard on the right) */
            }
            for (j = 0; j < n; j++) {
                x = xf_clamp(in[j] >> sh, -32767, 32767);
                t = f0[j] * fb + e0;                    /* the feedback, its remainder kept */
                e0 = t & 32767;
                wl = xd_sat(x + (t >> 15));
                t = f1[j] * fb + e1;
                e1 = t & 32767;
                wr = xd_sat((x & xm) + (t >> 15));
                b0[j] = (int16_t)wl;
                b1[j] = (int16_t)wr;
                pk |= (wl ^ (wl >> 31)) | (wr ^ (wr >> 31));
                ol[j] += (f0[j] * g) >> 12;
                orr[j] += (f1[j] * g) >> 12;
            }
        } else {                                        /* the mode crossing over 256 samples, or the tail's last
                                                         * units (q: towards zero, not sent round again) */
            for (j = 0; j < n; j++) {
                int32_t a = f0[j], c = f1[j], g;
                if (pm != pt)
                    pm += pm < pt ? 1 : -1;
                x = xf_clamp(in[j] >> sh, -32767, 32767);
                wl = a + xf_tz8((c - a) * pm);
                wr = a + c - wl;
                wl = xd_sat(x + (q ? xf_tz15(wl * fb) : xf_rs(wl * fb, &e0, 15)));
                wr = xd_sat(x - xf_tz8(x * pm) + (q ? xf_tz15(wr * fb) : xf_rs(wr * fb, &e1, 15)));
                b0[j] = (int16_t)wl;
                b1[j] = (int16_t)wr;
                pk |= (wl ^ (wl >> 31)) | (wr ^ (wr >> 31));
                g = XD_WET + pm * XD_WPP;
                ol[j] += (a * g) >> 12;
                orr[j] += (c * g) >> 12;
            }
            xf.d_pm = pm;
        }
        xf.d_e[0][2] = e0;
        xf.d_e[1][2] = e1;
    }
    xf.d_w = w + n;
    xf.d_q = pk < 2;
    if (pk | st)
        xf.d_live = XD_N + 2u * XD_NB;
    else
        xf.d_live = xf.d_live > n ? xf.d_live - n : 0u;
}

/* ----------------------------------------------------------------- the reverb --- */
#define XR_WET 4800                       /* Q12 (tests/cr_fx_test.c: Felucca's ROOM at its defaults, the same send) */
XF_AI int32_t xr_sat(int32_t v)           /* itself up to 3/4 of the line's range, a knee above, then its limit */
{
    if ((uint32_t)(v + 24576) <= 49152u)                /* (nearly always: one compare) */
        return v;
    if (v > 24576)
        v = 24576 + ((v - 24576) >> 2);
    else
        v = -24576 - ((-v - 24576) >> 2);
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}
/* one all-pass diffuser (g = 5 / 8) over a block of x, in place: its ring's read in straight pieces, its write at
 * bw (a block's writes never wrap: XR_APB) */
#define XR_DIFFUSE(OUT) \
    for (j = 0; j < n;) { \
        const int16_t *bp = pb + ((prd + j) & XR_PM) - j; \
        e = j + xf_seg(prd + j, XR_PM, n - j); \
        for (; j < e; j++) { \
            z = bp[j]; \
            u = xf_clamp(x[j] - xf_tz3(z * 5), -32767, 32767); \
            bw[j] = (int16_t)u; \
            u = z + xf_tz3(u * 5); \
            OUT; \
        } \
    }
/* n samples of the send -> the stereo wet, added to ol, orr. n is a block of the reverb (XR_NB = 1 << XR_SH), the
 * write indices multiples of it. Block-wise as the delay (xd_run's note; the unit ran the per-sample form, eight
 * lines wide in every sample, at ~1050 instructions a sample: 340 us a half): stage by stage and line by line over
 * the block, each loop's states in registers. Every line and every diffuser is longer than a block and reads a block
 * clear of what the stage before it writes (XR_APB, XR_BASE), so a block's reads never see its own writes. The
 * lines' two gains are one multiply: g_i x 181 / 512 (the Hadamard's scale) goes in front of the mix (gm, Q17), and
 * the one rounding a pass is the line's own */
CR_HOT_FX XF_HOT void xr_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t n, uint32_t sh)
{
    uint32_t w = xf.r_w0, pw = xf.r_pw, j, i, e;
    int32_t act = 0, pk = 0, kc, lk = xf.r_lk, ig, q = xf.r_q, t;
    int32_t *x = xf_reg(xf.tx), *tr = xf_reg(xf.tr);
    int16_t *pb = xf_reg(xr_pbuf), *lb = xf_reg(xr_buf);   /* the two rings */
    for (i = 0; i < 8u; i++)
        xf.r_gc[i] += xf_clamp(xf.r_g[i] - xf.r_gc[i], -96, 96);
    xf.r_igc += xf_clamp(xf.r_ig - xf.r_igc, -512, 512);
    xf.r_kc += xf_clamp(xf.r_k - xf.r_kc, -96, 96);
    ig = xf.r_igc;
    kc = xf.r_kc;
    {   /* the low cut (two poles), into the pre-delay's ring */
        int32_t h0 = xf.r_hp[0], h1 = xf.r_hp[1], e0 = xf.r_eh[0], e1 = xf.r_eh[1], v;
        int16_t *bw = pb + (pw & XR_PM);
        for (j = 0; j < n; j++) {
            v = xf_clamp(in[j] >> sh, -32767, 32767);
            t = (v - h0) * lk + e0;
            e0 = t & 32767;
            h0 += t >> 15;
            v = xf_clamp(v - h0, -32767, 32767);
            t = (v - h1) * lk + e1;
            e1 = t & 32767;
            h1 += t >> 15;
            v = xf_clamp(v - h1, -32767, 32767);
            bw[j] = (int16_t)v;
            act |= v;
        }
        xf.r_hp[0] = h0;
        xf.r_hp[1] = h1;
        xf.r_eh[0] = e0;
        xf.r_eh[1] = e1;
        act |= h0 | h1;
    }
    if (xf.r_pcur == xf.r_pre && !(xf.r_pcur & 255)) {  /* the pre-delay at rest .. */
        uint32_t p = pw - ((uint32_t)xf.r_pcur >> 8);
        for (j = 0; j < n;) {
            const int16_t *bp = pb + ((p + j) & XR_PM) - j;
            e = j + xf_seg(p + j, XR_PM, n - j);
            for (; j < e; j++)
                x[j] = bp[j];
        }
    } else {                                            /* .. or gliding (a quarter of a sample a sample) */
        int32_t pc = xf.r_pcur, d;
        for (j = 0; j < n; j++) {
            uint32_t p;
            int32_t a;
            d = xf.r_pre - pc;
            pc += d > 64 ? 64 : d < -64 ? -64 : d;
            p = pw + j - ((uint32_t)pc >> 8);
            a = pb[p & XR_PM];
            x[j] = a + xf_tz8((pb[(p - 1u) & XR_PM] - a) * (pc & 255));
        }
        xf.r_pcur = pc;
    }
    for (i = XR_NAP == 2u ? 1u : 0u; i < 4u; i += XR_NAP == 2u ? 2u : 1u) {   /* the diffusers (CR_FX_LITE: the second
                                                         * and the fourth); the last one's loop has the input gain. A
                                                         * diffuser that holds anything puts something out (|z| over
                                                         * what 25 / 64 of it takes back), so `act` is the last one's */
        uint32_t prd = pw + xf.r_apb[i] - xf.r_ap[i];
        int16_t *bw = pb + ((pw + xf.r_apb[i]) & XR_PM);
        int32_t z, u;
        if (i < 3u) {
            XR_DIFFUSE(x[j] = u)
        } else {
            XR_DIFFUSE(act |= u; x[j] = xf_tz15(u * ig))   /* (|u| <= 53246, x 32768: fits) */
        }
    }
    /* the eight lines' reads (r, for the output). A line's length moves as a ramp over the block: SIZE's glide (at
     * most r_step a sample) and, on the four that swing, MOD's way to this block's target */
    for (i = 0; i < 8u; i++) {
        int32_t *r = tr + i, cur = xf.r_cur[i], d = xf.r_tgt[i] - cur, a;
        uint32_t wb = w + xf.r_base[i], p;
        if (d) {
            int32_t lim = xf.r_step[i] * (int32_t)n;
            d = d > lim ? lim : d < -lim ? -lim : d;
            xf.r_cur[i] = cur + d;
        }
        if ((i & 1u) && (i >> 1) < XR_NMOD) {
            uint32_t m = i >> 1;
            cur += xf.r_mv[m];
            d += xf.r_mt[m] - xf.r_mv[m];
            xf.r_mv[m] = xf.r_mt[m];
        }
        if (!d && !(cur & 255)) {                       /* at rest: the line's own samples */
            p = wb - ((uint32_t)cur >> 8);
            for (j = 0; j < n;) {
                const int16_t *bp = lb + ((p + j) & XR_M) - j;
                e = j + xf_seg(p + j, XR_M, n - j);
                for (; j < e; j++)
                    r[8u * j] = bp[j];
            }
        } else if (((uint32_t)(cur + (d >> XR_SH)) >> 8) == ((uint32_t)(cur + d) >> 8)) {
            /* swinging (MOD: far under a sample a block): between the same two samples all block, the fraction a ramp */
            int32_t acc = cur << XR_SH;                 /* (< 2^21 << 5) */
            p = wb - ((uint32_t)(cur + d) >> 8);
            for (j = 0; j < n;) {
                const int16_t *bp = lb + ((p + j) & XR_M) - j, *bq = lb + ((p + j - 1u) & XR_M) - j;
                e = (p + j) & XR_M ? j + xf_seg(p + j, XR_M, n - j) : j + 1u;   /* (the ring's first sample: its
                                                         * neighbour is the last) */
                for (; j < e; j++) {
                    acc += d;
                    a = bp[j];
                    r[8u * j] = a + xf_tz8((bq[j] - a) * ((acc >> XR_SH) & 255));
                }
            }
        } else {                                        /* gliding (SIZE): anywhere */
            int32_t acc = cur << XR_SH;
            for (j = 0; j < n; j++) {
                acc += d;
                t = acc >> XR_SH;
                p = wb + j - ((uint32_t)t >> 8);
                a = lb[p & XR_M];
                r[8u * j] = a + xf_tz8((lb[(p - 1u) & XR_M] - a) * (t & 255));
            }
        }
    }
    {   /* out: two still and two swinging lines a side (CR_FX_LITE: three and one); width */
        const int32_t *r = tr;
        int32_t wc = xf.r_wc, wt = xf.r_w, l, rr;
        if (wc == wt) {                                 /* (m + s, m - s of the form below as two gains: one rounding) */
            int32_t ga = (XR_WET * (256 + wc)) >> 9, gb = XR_WET - ga;
            for (j = 0; j < n; j++, r += 8) {
                l = r[0] - r[1] + r[4] - r[5];
                rr = r[2] - r[3] + r[6] - r[7];
                ol[j] += (l * ga + rr * gb) >> 12;      /* (|l|, |rr| <= 4 x 32767, ga + gb = 4800: fits) */
                orr[j] += (l * gb + rr * ga) >> 12;
            }
        } else {                                        /* WIDTH moving: a step a sample */
            int32_t m, s;
            for (j = 0; j < n; j++, r += 8) {
                l = r[0] - r[1] + r[4] - r[5];
                rr = r[2] - r[3] + r[6] - r[7];
                if (wc != wt)
                    wc += wc < wt ? 1 : -1;
                m = (l + rr) >> 1;
                s = (((l - rr) >> 1) * wc) >> 8;
                ol[j] += ((m + s) * XR_WET) >> 12;      /* (|m +- s| <= 4 x 32767: fits) */
                orr[j] += ((m - s) * XR_WET) >> 12;
            }
            xf.r_wc = wc;
        }
    }
    for (i = 0; i < 8u; i++) {                          /* each line's low-pass (DAMP) and gain, in place */
        int32_t *r = tr + i, lp = xf.r_lp[i], e1 = xf.r_e1[i], gm = (xf.r_gc[i] * 181) >> 7;   /* Q17: g x 181 / 512 */
        for (j = 0; j < n; j++, r += 8) {
            t = (*r - lp) * kc + e1;
            e1 = t & 32767;
            lp += t >> 15;
            *r = (lp * gm) >> 5;                        /* Q12 (32767 x 46340 < 2^31; eight of them summed: < 2^29) */
        }
        xf.r_lp[i] = lp;
        xf.r_e1[i] = e1;
        act |= lp;
    }
    {   /* Hadamard: three stages of butterflies, eight values a sample in registers */
        int32_t *r = tr;
        for (j = 0; j < n; j++, r += 8) {
            int32_t a0 = r[0] + r[1], a1 = r[0] - r[1], a2 = r[2] + r[3], a3 = r[2] - r[3];
            int32_t a4 = r[4] + r[5], a5 = r[4] - r[5], a6 = r[6] + r[7], a7 = r[6] - r[7];
            int32_t b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
            int32_t b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
            r[0] = b0 + b4; r[1] = b1 + b5; r[2] = b2 + b6; r[3] = b3 + b7;
            r[4] = b0 - b4; r[5] = b1 - b5; r[6] = b2 - b6; r[7] = b3 - b7;
        }
    }
    for (i = 0; i < 8u; i++) {                          /* the lines' writes: the one rounding a pass, its remainder
                                                         * kept (q, the tail's last units: towards zero) */
        const int32_t *r = tr + i, *xp = x;
        int16_t *bw = lb + ((w + xf.r_base[i]) & XR_M);
        int32_t e2 = xf.r_e2[i], s;
        if (!q) {
            for (j = 0; j < n; j++, r += 8) {
                t = *r + e2;
                e2 = t & 4095;
                s = xr_sat((t >> 12) + *xp++);
                *bw++ = (int16_t)s;
                pk |= s ^ (s >> 31);
            }
            xf.r_e2[i] = e2;
        } else {
            for (j = 0; j < n; j++, r += 8) {
                t = *r;
                s = xr_sat(((t + ((t >> 31) & 4095)) >> 12) + *xp++);
                *bw++ = (int16_t)s;
                pk |= s ^ (s >> 31);
            }
        }
    }
    xf.r_w0 = w + n;
    xf.r_pw = pw + n;
    xf.r_q = pk < 2;
    if (act | pk)
        xf.r_live = XR_N + 2u * XR_NB;
    else
        xf.r_live = xf.r_live > n ? xf.r_live - n : 0u;
}

/* ------------------------------------------------------------------ the block --- */
static void xf_hb_clear(xf_hb_t *h)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        h->x[i] = h->y[i] = 0;
}
/* --------------------------------------------------------------- the routing --- */
/* THE ROUTING (GitHub issue 21, the second design; docs/FX.md "The routing"; the FX layer's Routing page). Two modes
 * (XP_RMODE) and an order of FOUR blocks (XP_ORDER, XF_ORD: the chorus, the delay, the reverb and the master filter).
 * b1e1222's first design (an order of the three and a Feed of each return into the next) never shipped: one dev unit
 * had it; its session tags 35 / 36 are this order and this mode (a Feed > 1 left there is out of range: Parallel).
 * PARALLEL (0, the default) IS 0.26, BIT FOR BIT, whatever the order says: the three buses fed by the parts' sends,
 * their returns added into the mix, the master filter after everything (fx.c mix_block: nothing here is called; the
 * order is kept for Chain and drawn). CHAIN (1) is a serial path, the OEM way. C_0 is the dry mix (the parts mixed:
 * their levels, pans, drives) and the block at position k makes C_{k+1} of C_k: the filter F(C_k) (the master
 * filter's two cuts as set; Off: C_k itself), an effect C_k + W_k, its wet from amt x mono(C_k) (mono: (l + r) / 2).
 * The output is C_4. amt is the CHORD part's send for that effect (cr_ui.c cu_fx_apply writes the FX layer's amounts
 * there; the bass part's own sends rest in Chain: the bass is in the dry mix and goes through the chain with the
 * chords), Q15 as fx.c scales a send (x 258) and ramped over the block as fx.c's fx_sg are (a detent never steps).
 * FX off: the amounts go to 0 and the filter is out (xq_coefs), so the output is the dry mix, the tails ringing out as
 * a closed send's do in Parallel. The drive stays per part, before the mix, in both modes.
 * THE PREVIOUS BLOCK. An effect at position k >= 1 takes mono C_k of the LAST block (xg.sn: CTL samples late, 0.73
 * ms, not heard as a delay); the one at position 0 this block's dry. So the inputs are whole before any effect runs
 * (xg_in, core 0, into the sends' buffers: xf_prep counts them as it counts the sends, and the effects run as in
 * Parallel and on the same cores, fx.c mix_fx_chain: the reverb on core 1, the chorus and the delay on core 0), each
 * into its OWN stereo wet (xg.w; at the half rate the delay and the reverb each with their own half-rate pair and
 * up-sampler: the delay's is xg.ud, the reverb's xf.up as in Parallel). After the join core 0 assembles (xg_join):
 * acc = dry, then each position in order: the filter on acc in place (xq_block), or the effect's wet added; mono acc
 * after positions 0, 1, 2 is the next block's C_1, C_2, C_3. Sums of integers in a fixed order: one core's samples
 * and two cores' the same (tests/cr_c1_thread_test.c, Chain at every order).
 * A mode or order change switches at a block's start: the snapshots, the delay's up-sampler and the amounts start
 * from 0 (xg.key; the amounts then ramp in). After the tail every buffer is exact zeros, the effects switch off as in
 * Parallel. POOL: xg, 1 396 bytes at the default build (the wets 768, the snapshots 384, the reverb's half-rate pair
 * 128, the up-sampler pair 96, the rest 20; the full-rate build 1 172) */
#if XF_HALF
#define XG_HALF 1
#else
#define XG_HALF 0
#endif
static struct {
    uint32_t key;                         /* 1 + the order the chain's state is for (0: none: cleared at the next block) */
    uint32_t ud_live;                     /* the delay's up-sampler's tail (as xf.up_live) */
    int32_t g[XFE_NFX];                   /* each effect's amount in use, Q15 (the chord part's send as the last block
                                           * left it) */
    int32_t w[XFE_NFX][2][CTL];           /* each effect's wet this block, left and right, at the full rate */
    int32_t sn[XFE_N - 1u][CTL];          /* mono C_1, C_2, C_3 of the last block */
#if XG_HALF
    int32_t rh[2][XF_NH];                 /* the reverb's half-rate wet when core 0 runs it */
    xf_hb_t ud[2];                        /* the delay's own up-sampler, left and right */
#endif
} xg __attribute__((section(".pool")));
static uint32_t xg_ord(void) { return (uint32_t)xfp[XP_ORDER] < 24u ? (uint32_t)xfp[XP_ORDER] : 0u; }
/* the chain's inputs (core 0, after the parts, before the effects): into sc, sd, sr (the sends' buffers, whatever the
 * parts put there replaced), from ml / mr (the dry mix, this block's) and the snapshots. amt: the three amounts' targets,
 * Q15 (fx.c: the chord part's sends, 0 with FX off). Also clears the wets */
static __attribute__((noinline)) void xg_in(int32_t *sc, int32_t *sd, int32_t *sr, const int32_t *ml, const int32_t *mr,
                                            const int32_t *amt, uint32_t n)
{
    uint32_t o = xg_ord(), k, i;
    if (xg.key != o + 1u) {                             /* a new chain (or Chain just chosen): from rest */
        xg.key = o + 1u;
        xg.ud_live = 0;
        for (k = 0; k < XFE_NFX; k++)
            xg.g[k] = 0;
        for (k = 0; k < XFE_N - 1u; k++)
            for (i = 0; i < CTL; i++)
                xg.sn[k][i] = 0;
#if XG_HALF
        xf_hb_clear(&xg.ud[0]);
        xf_hb_clear(&xg.ud[1]);
#endif
    }
    for (k = 0; k < XFE_NFX; k++)
        for (i = 0; i < n; i++)
            xg.w[k][0][i] = xg.w[k][1][i] = 0;
    for (k = 0; k < XFE_N; k++) {
        uint32_t e = XF_ORD[o][k];
        int32_t *in, g, t, dg, xmax;
        const int32_t *s = k ? xg.sn[k - 1u] : 0;
        if (e == XFE_FILTER)
            continue;
        in = e == XFE_CHORUS ? sc : e == XFE_DELAY ? sd : sr;
        g = xg.g[e];
        t = amt[e];
        xg.g[e] = t;
        if (!g && !t) {                                 /* (closed: nothing in) */
            for (i = 0; i < n; i++)
                in[i] = 0;
            continue;
        }
        dg = n == CTL ? (t - g) >> CTL_LOG2 : (t - g) / (int32_t)n;   /* (fx.c's ramp, a straight line across the block) */
        xmax = 0x7FFFFFFF / ((t > g ? t : g) | 1);
        for (i = 0; i < n; i++) {
            int32_t m = s ? s[i] : (ml[i] + mr[i]) >> 1;
            g += dg;
            in[i] = (xf_clamp(m, -xmax, xmax) * g) >> 15;
        }
    }
}
#if XG_HALF
/* a half-rate wet up into a full-rate pair (xf_reverb's up-sampler, the same steps: the tail counted, the states
 * cleared at rest), for the chain's delay and reverb each */
static __attribute__((noinline)) void xg_up(xf_hb_t *h, uint32_t *live, const int32_t *l, const int32_t *r, int32_t *ml,
                                            int32_t *mr, uint32_t nh)
{
    uint32_t i;
    int32_t any = 0;
    for (i = 0; i < nh; i++)
        any |= l[i] | r[i];
    if (any)
        *live = 256u;
    else if (*live)
        *live = *live > nh ? *live - nh : 0u;
    if (any || *live) {
        XF_CALL(xf_up)(&h[0], l, ml, nh);
        XF_CALL(xf_up)(&h[1], r, mr, nh);
    } else {
        xf_hb_clear(&h[0]);
        xf_hb_clear(&h[1]);
    }
}
#endif
/* ---------------------------------------------------------------- the chorus --- */
/* THE CHORUS, STEREO (0.19; docs/FX.md "The chorus, stereo"). Felucca's chorus (fx.c fx_buses) reads its line at one
 * point that an LFO moves (400 + 0 .. 6 x DEPTH samples back: 9 .. 17 ms at the default) and returns that one tap to
 * both sides: beside the dry sound the same comb in both ears. Here the same line (fx.c cho_buf, written as before)
 * is read at TWO points: the left's is Felucca's own, the right's the same LFO a quarter turn ahead (XP_CWIDTH: 0 ..
 * a quarter). Each ear still has the comb 0.18 had, but never the same one, and the two wets are unrelated over the
 * band; the mono sum's deepest notch is 0.18's (at the two moments of a turn where the taps cross) and less
 * everywhere else, and it keeps 71 % of the chorus's motion (half a turn apart, as SLOOP has it, is as wide and
 * leaves the sum a comb that stands still: tools/chorus_response.py). Rate, Depth and the send are what they were.
 * Two taps on the one line, left and right, and the LFO taken once a block with the read points a straight line
 * across it (no sine a sample) are the design of SLOOP 2.6's chorus (github.com/isod89/sloop-fm1 firmware/src/fx.c
 * fx_buses; GPL-3.0-only, a fork of Felucca): the design, written anew, no code copied (LICENSING.md). ChoralRoot's
 * own: the quarter turn and its width parameter, the line ending exactly on the next block's start, the return
 * straight into the mix, and the rest below.
 * xc_live: samples for which the line still holds something. A part that sends nothing costs a look at the block
 * (and nothing at all once the line has run empty: no write, no read). */
#ifndef XC_TURN
#define XC_TURN 0x40000000u               /* the right tap's lead at Width 127: a quarter of the LFO's turn */
#endif
static uint32_t xc_live;
/* 0.21: NO STEP IN THE READ POINT, WHATEVER MOVES (the owner: "This FX pops and crackles when adjusting the depth").
 * To 0.20 each block's line ran from XC_POS(ph0) to XC_POS(ph1) with the Depth and the Width of that block: it ended
 * where the next began only while both stood still. A step of Depth moved the next block's first point by up to 6
 * samples (1536 Q8) and a step of Width the right tap's by up to 4.7: a click a detent. Now
 *  - each tap's last read point is kept (xc.ra, xc.rb: Q8, the point the last block's line aimed at) and the next
 *    block's line starts there: the read point is continuous by construction;
 *  - the depth (xc.dc, a 16th of the 6 x DEPTH unit) and the right tap's lead (xc.oc, the LFO's phase) go to what is
 *    set by a 1/2^XC_GL of what is left a block, rounded away from zero: a one-pole of 2^XC_GL blocks (64: 46 ms)
 *    that LANDS on the target. At rest dc is 16 x the old cdepth and (s x 16 c) >> 12 is (s x c) >> 8: the block is
 *    0.20's to the bit (tests/cr_fx_test.c "chorus glide" holds 0.20's block beside it);
 *  - an empty line (xc_live 0) glides nothing: the first block with a send starts on what is set, as 0.20's did.
 * Rate needs none of it: it is the LFO's increment, and the phase runs on. */
#ifndef XC_GL
#define XC_GL 6                           /* the glide: 2^6 blocks of 32, 46 ms (docs/FX.md has the measurements) */
#endif
static struct { int32_t dc, oc, ra, rb; } xc;
#define XC_POS(ph, d) ((400 << 8) + (((osc_sine(ph) + 32768) * (d)) >> 12))   /* Q8 samples back (fx_buses's own; d: x16) */
#define XC_GLIDE(c, t) ((c) + (((t) - (c) + ((t) > (c) ? (1 << XC_GL) - 1 : 0)) >> XC_GL))
CR_HOT_FX static __attribute__((noinline)) void xc_block(const int32_t *in, int32_t *ml, int32_t *mr, uint32_t n)
{
    int32_t dt = song.g[G_CDEPTH] * (6 << 4), ot = xfp[XP_CWIDTH] * (int32_t)(XC_TURN / 127u);
    int32_t any = 0, dc, oc, ra, rb, da, db, ea, eb;
    uint32_t i, w = fx.cho_w, ph0 = fx.cho_ph, ph1 = ph0 + (LFO_INC[song.g[G_CRATE] & 127] / CTL) * n;
    fx.cho_ph = ph1;                                    /* (the LFO runs on while nothing is sent, as it always did) */
    fx.cho_w = w + n;
    if (!xc_live) {
        for (i = 0; i < n; i++)
            any |= in[i];
        if (!any)
            return;                                     /* nothing sent and the line empty: zeros in, zeros out */
        xc.dc = dt;                                     /* the first block after rest (and after power-on, a load): */
        xc.oc = ot;                                     /* in place, no glide */
        xc.ra = XC_POS(ph0, dt);
        xc.rb = XC_POS(ph0 + (uint32_t)ot, dt);
    }
    dc = xc.dc;
    oc = xc.oc;
    if (dc != dt)
        xc.dc = dc = XC_GLIDE(dc, dt);
    if (oc != ot)
        xc.oc = oc = XC_GLIDE(oc, ot);
    ea = XC_POS(ph1, dc);                               /* the read points, a 32nd of Q8: a line from where the last */
    eb = XC_POS(ph1 + (uint32_t)oc, dc);                /* block's ended to this block's end, where the next begins */
    da = ea - xc.ra;
    db = eb - xc.rb;
    ra = xc.ra << 5;
    rb = xc.rb << 5;
    xc.ra = ea;
    xc.rb = eb;
    if (n != CTL) {                                     /* (the ISR's block is CTL: the step is the difference) */
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
    xc_live = any ? CHO_LEN : xc_live > n ? xc_live - n : 0u;
}

/* a block of the two sends (the mono buses of fx.c) -> the stereo wet added to ml, mr. n: CTL.
 * 3y50, THE REVERB ON EITHER CORE (fx.c mix_fx_split, docs/DUALCORE.md "What each core runs"): the block is three
 * pieces. xf_prep (core 0: the coefficients, what runs), xf_delay (the delay's line, into ml / mr) and xf_reverb (the
 * sends' half-band, the reverb's lines, the wet's up-sampler, into its own ml / mr). Each of the two writes only its
 * own state (xf.d_*, xd_buf, xf.tf; xf.r_*, xf.dn[1], xf.up, xf.up_live, xf.t[1..3], xf.tr, xf.tx, xr_buf, xr_pbuf)
 * and ADDS its wet into the mix: a sum of integers, the same whatever runs first, so the reverb can run on core 1
 * into buffers of its own that core 0 adds afterwards, and the samples are one core's, bit for bit. xf_block is the
 * three in a row, as to 3y48. With the delay at the half rate (CR_FX_DELAY_DS 2, the default since 0.22) the delay
 * writes the reverb's wet buffer too (xf.t[2..3], one up-sampler for both): then core 1's job is the reverb's lines
 * alone, into half-rate buffers of its own (XF_SPLIT_HALF: xf_reverb with up 0), and core 0 runs the delay into
 * xf.t[2..3], adds core 1's after the join and runs the up-sampler (xf_reverb with rev 0): the same integer sums as
 * the three in a row, bit for bit. The delay at the half rate with the reverb at the full one (a build switch):
 * the block whole on core 0 (XF_SPLIT 0) */
#define XF_SPLIT_HALF (CR_FX_DELAY_DS == 2 && CR_FX_REVERB_DS == 2)
#define XF_SPLIT (CR_FX_DELAY_DS == 1 || XF_SPLIT_HALF)
static uint32_t xf_prep(const int32_t *dly_in, const int32_t *rev_in, uint32_t n)   /* bit 0: the delay, bit 1: the
                                                         * reverb, bit 2: the up-sampler's tail; 0: nothing runs */
{
    uint32_t i, h = xf_bpm;
    int32_t in_d = 0, in_r = 0;
    for (i = 0; i < XP_NFX; i++)                        /* (the filter's four are xq_block's own hash) */
        h = h * 31u + (uint16_t)xfp[i];
    if (h != xf.hash || !xf.on) {                       /* a parameter or the tempo moved (or power-on) */
        xf.hash = h;
        xf_coefs();
        if (!xf.on) {                                   /* power-on: everything that glides starts where it is set */
            for (i = 0; i < 8u; i++) {
                xf.r_cur[i] = xf.r_tgt[i];
                xf.r_gc[i] = xf.r_g[i];
            }
            xf.d_cur[0] = xf.d_tgt[0];
            xf.d_cur[1] = xf.d_tgt[1];
            xf.d_fbc = xf.d_fb;
            xf.d_kc = xf.d_k;
            xf.r_kc = xf.r_k;
            xf.d_pm = xf.d_ping ? 256 : 0;
            xf.r_igc = xf.r_ig;
            xf.r_wc = xf.r_w;
            xf.r_pcur = xf.r_pre;
            xf.on = 1;
        }
    }
    if (!xf.d_live || !xf.r_live)                       /* (both running: nothing to look for) */
        for (i = 0; i < n; i++) {
            in_d |= dly_in[i];
            in_r |= rev_in[i];
        }
    return (uint32_t)(in_d || xf.d_live) | (uint32_t)(in_r || xf.r_live) << 1 | (uint32_t)!!xf.up_live << 2;   /* (in
                                                         * Chain mode the sends are THE ROUTING's chain inputs) */
}
/* the delay (its line at FS: into ml, mr; at FS / 2: into the half-rate wet, xf.t[2..3]) */
CR_HOT_FX static __attribute__((noinline)) void xf_delay(const int32_t *dly_in, int32_t *ml, int32_t *mr, uint32_t n)
{
    int32_t *hd = xf.t[0], *ol = xf.t[2], *orr = xf.t[3];
    (void)hd; (void)ol; (void)orr;
    /* the wow, once a block: a 0.33 Hz sine and an eighth as much of a 3.1 Hz one */
    xf.d_ph[0] += (uint32_t)(((uint64_t)330u << 32) * CTL / (1000u * FS));
    xf.d_ph[1] += (uint32_t)(((uint64_t)3100u << 32) * CTL / (1000u * FS));
    xf.d_wt = ((osc_sine(xf.d_ph[0]) + (osc_sine(xf.d_ph[1]) >> 3)) * (xf.d_wow >> 3)) >> 12;
#if CR_FX_DELAY_DS == 2
    XF_CALL(xf_down)(&xf.dn[0], dly_in, hd, n);
    CR_PROFX(CRX_DOWN);
    XF_CALL(xd_run)(hd, ol, orr, n >> 1, 0);
#else
    XF_CALL(xd_run)(dly_in, ml, mr, n, XF_IN);
#endif
    CR_PROFX(CRX_DELAY);
    if (!xf.d_live) {                                   /* silent: the half-band's last fractions with it */
        xf_hb_clear(&xf.dn[0]);
        xf.d_lp[0] = xf.d_lp[1] = xf.d_hp[0] = xf.d_hp[1] = 0;
    }
}
/* the reverb and the half rate's up-sampler, into ml, mr. tm 0: its parts on the fx stage's marks (core 0); else
 * (core 1's job, fx.c c1_fx_job) its own: the down-sampler's, the lines' and the up-sampler's time into tm[CRX_*],
 * which core 0 adds to the stage's parts after the join (the marks are core 0's words). ol / orr: the half-rate wet
 * (xf.t[2..3]; core 1's own with XF_SPLIT_HALF); up 0: the lines alone (core 1's job with XF_SPLIT_HALF: core 0 runs
 * the up-sampler after the join, rev 0) */
#if CR_CPU1
#define XR_MARK(k) do { if (tm) { uint32_t t_ = cr_prof_now(); tm[k] += cr_prof_less(t_ - *mk); *mk = t_; } else CR_PROFX(k); } while (0)
#else
#define XR_MARK(k) ((void)tm, (void)mk)
#endif
CR_HOT_FX static __attribute__((noinline)) void xf_reverb(const int32_t *rev_in, int32_t *ml, int32_t *mr, uint32_t n,
                                                          int rev, uint32_t *tm, uint32_t *mk, int32_t *ol,
                                                          int32_t *orr, int up)
{
    uint32_t i, nh = n >> 1;
    int32_t any = 0;
    int32_t *hr = xf.t[1];
#if XF_HALF && CR_FX_DELAY_DS == 1
    for (i = 0; i < nh; i++)                            /* (the delay at FS: the wet buffer is the reverb's alone) */
        ol[i] = orr[i] = 0;
#endif
    if (rev) {
        /* the four swings, once a block: lines 1 and 5 on a 0.31 Hz sine and its quarter turn, 3 and 7 on a 0.47 Hz one */
        xf.r_ph[0] += (uint32_t)(((uint64_t)310u << 32) * CTL / (1000u * FS));
        xf.r_ph[1] += (uint32_t)(((uint64_t)470u << 32) * CTL / (1000u * FS));
        for (i = 0; i < 4u; i++)
            xf.r_mt[i] = (osc_sine(xf.r_ph[i >> 1] + ((i & 1u) << 30)) * xf.r_mod) >> 15;
#if CR_FX_REVERB_DS == 2
        XF_CALL(xf_down)(&xf.dn[1], rev_in, hr, n);
        XR_MARK(CRX_DOWN);
        XF_CALL(xr_run)(hr, ol, orr, nh, 0);
#else
        XF_CALL(xr_run)(rev_in, ml, mr, n, XF_IN);
#endif
        XR_MARK(CRX_REVERB);
        if (!xf.r_live)
            xf_hb_clear(&xf.dn[1]);
    }
#if XF_HALF
    if (!up)                                            /* (core 1's job, XF_SPLIT_HALF: core 0 up-samples the sum) */
        return;
    for (i = 0; i < nh; i++)
        any |= ol[i] | orr[i];
    if (any)
        xf.up_live = 256u;
    else if (xf.up_live)
        xf.up_live = xf.up_live > nh ? xf.up_live - nh : 0u;
    if (any || xf.up_live) {
        XF_CALL(xf_up)(&xf.up[0], ol, ml, nh);
        XF_CALL(xf_up)(&xf.up[1], orr, mr, nh);
        XR_MARK(CRX_UP);
    } else {
        xf_hb_clear(&xf.up[0]);
        xf_hb_clear(&xf.up[1]);
    }
#else
    (void)any; (void)hr; (void)ol; (void)orr; (void)nh; (void)up;
#endif
    (void)tm; (void)mk;
}
CR_HOT_FX static __attribute__((noinline)) void xf_block(const int32_t *dly_in, const int32_t *rev_in, int32_t *ml,
                                               int32_t *mr, uint32_t n)
{
    uint32_t f = xf_prep(dly_in, rev_in, n);
    if (!f)
        return;                                         /* nothing sent, the tails over: nothing runs */
#if XF_HALF && CR_FX_DELAY_DS == 2                     /* (the delay's wet and the reverb's share it: from 0) */
    {
        uint32_t i;
        for (i = 0; i < (n >> 1); i++)
            xf.t[2][i] = xf.t[3][i] = 0;
    }
#endif
    CR_PROFX_START();                                   /* (cr_cpu1.h: the stage's parts, for 73 DEBUG and perf.sh) */
    if (f & 1u)
        xf_delay(dly_in, ml, mr, n);
    xf_reverb(rev_in, ml, mr, n, (int)(f >> 1 & 1u), 0, 0, xf.t[2], xf.t[3], 1);
}
/* ------------------------------------------------------------ the master filter --- */
/* THE MASTER FILTER (0.22; GitHub issue 16: "many of the patches contain a lot of harsh upper tones", "a simple single
 * knob lpf/hpf"; docs/FX.md "The master filter"). A LOW CUT and a HIGH CUT on the stereo master, after the effects
 * (the reverb's and the delay's wash is tamed with the parts) and before Options > Output's chain (the limiter and the
 * clip see what the filter leaves). The FX layer's Filter page (cr_ui.c CU_FXP): Low cut (Off, 20 Hz .. 2 kHz), High
 * cut (200 Hz .. 16 kHz, Off), Slope (6 or 12 dB an octave), Filter (Off / On; a cutoff turned switches it on).
 * A cut is one stage (6 dB) or two in a row (12 dB) of a one-pole, the low cut's stage x minus its low-pass; the knob
 * reads the cut's -3 dB corner, so a 12 dB cut's two poles sit at 1.554 times it (two identical poles are 3 dB down at
 * 0.6436 of their own corner). Every product keeps its remainder (xf_rs), as the loops above do: no dead band at a
 * low k, no offset, exact zeros after silence. The k is Q12 and the mix into a cut is held to +-XQ_MAX (3 x full
 * scale, 15 dB over the Classic limiter's threshold: a sample out there is 15 dB into the limiter's work and in the
 * clip): the products fit 32 bits with the second stage's difference at 4 x XQ_MAX.
 * OFF TOUCHES NOTHING: with FX off, Filter Off, or both cuts at their Off ends (the default) the block returns before
 * it reads a sample, and its states are at rest: the master is bit for bit what it was (cr_fx_test.c proves it). A
 * cut switched in or out FADES: its wet share w (Q12) goes 0 .. 4096 by XQ_STEP a block, a straight line across each
 * block (16 blocks of CTL, 11.6 ms: Options > Output's fade), the cut's stages running from rest under the fade; a cut
 * whose w is 0 and wanted 0 does not run. A cutoff turned EASES: the k in use moves an eighth of itself towards the
 * knob's each block (xq_ease: an octave in 6 blocks; no zipper). The slope eases the same way: the second stage's k
 * goes to 4096 (y = x, exactly) when 12 dB goes to 6, and the stage is skipped once there; 6 to 12 starts it at its
 * stage's input and eases its k down (a quarter of the way a block there stepped the stage's slew by a third at
 * once: a click under a tone; cr_fx_test.c). Only a cut that is in costs anything.
 * COST on the unit (the listing, tools/fx_loops.py; docs/FX.md "The master filter"): a cut's loop is ~20
 * instructions a sample at 12 dB (~13 at 6 dB) a side: one 12 dB cut ~25 us a half, both ~50; 6 dB half of that. */
#define XQ_MAX 98304                      /* the input's clamp: 3 x 32768 */
#define XQ_STEP 256                       /* a cut fades in or out over 4096 / XQ_STEP = 16 blocks */
#define XQ_KS 3                           /* a k eases an eighth of itself a block (xq_ease): an octave in 6 */
typedef struct { int32_t y[2], e[2]; } xq_st_t;       /* a cut's two stages on one side: the one-poles, their remainders */
static struct {
    uint32_t hash;                        /* the four parameters and fx_off as last seen */
    uint8_t on;                           /* the coefficients are set (power-on: no fades, no eases) */
    uint8_t in[2];                        /* each cut wanted in: 0 the low cut, 1 the high cut */
    int32_t kt[2][2], k[2][2];            /* each cut's two stages' k (Q12): the knob's, and the one in use */
    int32_t w[2];                         /* each cut's wet share, 0 .. 4096 */
    xq_st_t s[2][2];                      /* [cut][side] */
    int32_t t[CTL];                       /* a fade's block of the wet */
} xq __attribute__((section(".pool")));

static int32_t xq_k12(uint32_t hz)        /* a one-pole's k (Q12) for the corner hz: 1 .. 4095 */
{
    int32_t k = (xf_k(hz, FS) + 4) >> 3;
    return k < 1 ? 1 : k > 4095 ? 4095 : k;
}
static __attribute__((noinline)) void xq_coefs(void)   /* (when a parameter or FX moved: the divides out of the block) */
{
    uint32_t lo = (uint32_t)xfp[XP_FLOW], hi = (uint32_t)xfp[XP_FHIGH], two = xfp[XP_FSLOPE] != 0, c;
    int act = xfp[XP_FON] && !fx_off;
    xq.in[0] = (uint8_t)(act && lo > 0u);
    xq.in[1] = (uint8_t)(act && hi < 127u);
    for (c = 0; c < 2u; c++) {
        uint32_t hz = c ? xq_high_hz(hi) : xq_low_hz(lo);
        if (two)
            hz = hz * 1554u / 1000u;      /* the pair's corner at the knob's: each pole at 1 / 0.6436 of it */
        xq.kt[c][0] = xq_k12(hz);
        xq.kt[c][1] = two ? xq.kt[c][0] : 4096;     /* (4096: the second stage is y = x: skipped once it is there) */
    }
}
/* a cut over a block of one side, in place: hp 0 the high cut (the low-pass), 1 the low cut (x minus it); two: the
 * second stage runs (the states in registers for the block) */
XF_AI void xq_loop(int32_t *b, uint32_t n, xq_st_t *p, int32_t k1, int32_t k2, int hp, int two)
{
    int32_t y1 = p->y[0], e1 = p->e[0], y2 = p->y[1], e2 = p->e[1];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = xf_clamp(b[i], -XQ_MAX, XQ_MAX), y;
        y1 += xf_rs((x - y1) * k1, &e1, 12);
        y = hp ? x - y1 : y1;
        if (two) {
            y2 += xf_rs((y - y2) * k2, &e2, 12);
            y = hp ? y - y2 : y2;
        }
        b[i] = y;
    }
    p->y[0] = y1;
    p->e[0] = e1;
    p->y[1] = y2;
    p->e[1] = e2;
}
CR_HOT_FX static __attribute__((noinline)) void xq_cut(int32_t *b, uint32_t n, xq_st_t *p, int32_t k1, int32_t k2, uint32_t hp)
{
    if (k2 == 4096) {
        if (hp)
            xq_loop(b, n, p, k1, k2, 1, 0);
        else
            xq_loop(b, n, p, k1, k2, 0, 0);
        p->y[1] = hp ? 0 : p->y[0];                     /* (the second stage at its input: it may start from here) */
        p->e[1] = 0;
    } else if (hp) {
        xq_loop(b, n, p, k1, k2, 1, 1);
    } else {
        xq_loop(b, n, p, k1, k2, 0, 1);
    }
}
/* .. under a fade: the wet share a straight line from w0 to w1 across the block (CTL samples: n == CTL) */
CR_HOT_FX static __attribute__((noinline)) void xq_fade(int32_t *b, uint32_t n, xq_st_t *p, int32_t k1, int32_t k2,
                                                        uint32_t hp, int32_t w0, int32_t w1)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        xq.t[i] = b[i];
    xq_cut(xq.t, n, p, k1, k2, hp);
    for (i = 0; i < n; i++) {
        int32_t x = xf_clamp(b[i], -XQ_MAX, XQ_MAX), w = w0 + (((w1 - w0) * (int32_t)(i + 1u)) >> CTL_LOG2);
        b[i] = x + (((xq.t[i] - x) * w) >> 12);
    }
}
static int32_t xq_ease(int32_t k, int32_t t)          /* a 2^-XQ_KS of ITSELF a block (an octave in 2^XQ_KS x 0.7
                                                       * blocks either way: the stage's slew never changes by more than
                                                       * that in a block, a slope change included), and it arrives */
{
    if (k < t) {
        k += (k >> XQ_KS) + 1;
        return k > t ? t : k;
    }
    if (k > t) {
        k -= (k >> XQ_KS) + 1;
        return k < t ? t : k;
    }
    return k;
}
/* the master's block (fx.c mix_block: after the effects, before out_master); ml, mr: the mix, in place */
CR_HOT_FX static __attribute__((noinline)) void xq_block(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t h = (uint32_t)fx_off, c, i;
    for (i = XP_NFX; i <= XP_FON; i++)                  /* (the filter's four: not the routing's two after them) */
        h = h * 31u + (uint16_t)xfp[i];
    if (h != xq.hash || !xq.on) {
        xq.hash = h;
        xq_coefs();
        if (!xq.on) {                                   /* power-on: as set, no fade, no ease */
            for (c = 0; c < 2u; c++) {
                xq.k[c][0] = xq.kt[c][0];
                xq.k[c][1] = xq.kt[c][1];
                xq.w[c] = xq.in[c] ? 4096 : 0;
            }
            xq.on = 1;
        }
    }
    for (c = 0; c < 2u; c++) {
        int32_t w0 = xq.w[c], wt = xq.in[c] ? 4096 : 0, w1 = w0 + xf_clamp(wt - w0, -XQ_STEP, XQ_STEP), k1, k2;
        if (!w0 && !w1) {                               /* out: nothing runs, nothing read; at rest for the next time */
            if (xq.k[c][0] != xq.kt[c][0] || xq.k[c][1] != xq.kt[c][1] || xq.s[c][0].y[0] | xq.s[c][0].y[1] | xq.s[c][1].y[0] | xq.s[c][1].y[1]) {
                xq.k[c][0] = xq.kt[c][0];
                xq.k[c][1] = xq.kt[c][1];
                for (i = 0; i < 2u; i++)
                    xq.s[c][i].y[0] = xq.s[c][i].y[1] = xq.s[c][i].e[0] = xq.s[c][i].e[1] = 0;
            }
            continue;
        }
        k1 = xq.k[c][0] = xq_ease(xq.k[c][0], xq.kt[c][0]);
        k2 = xq.k[c][1] = xq_ease(xq.k[c][1], xq.kt[c][1]);
        if (w0 == 4096 && w1 == 4096) {
            xq_cut(ml, n, &xq.s[c][0], k1, k2, c == 0u);
            xq_cut(mr, n, &xq.s[c][1], k1, k2, c == 0u);
        } else {
            xq_fade(ml, n, &xq.s[c][0], k1, k2, c == 0u, w0, w1);
            xq_fade(mr, n, &xq.s[c][1], k1, k2, c == 0u, w0, w1);
        }
        xq.w[c] = w1;
    }
}

/* THE ROUTING's assembly (above), core 0 after the join: ml / mr hold the dry mix and become the chain's output; each
 * position in order, the filter in place or an effect's wet added, and the snapshots of C_1 .. C_3 (the next block's
 * inputs) where an effect follows */
static __attribute__((noinline)) void xg_join(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t o = xg_ord(), k, i;
    for (k = 0; k < XFE_N; k++) {
        uint32_t e = XF_ORD[o][k];
        if (e == XFE_FILTER) {
            xq_block(ml, mr, n);
        } else {
            const int32_t *wl = xg.w[e][0], *wr = xg.w[e][1];
            for (i = 0; i < n; i++) {
                ml[i] += wl[i];
                mr[i] += wr[i];
            }
        }
        if (k + 1u < XFE_N && XF_ORD[o][k + 1u] != XFE_FILTER)
            for (i = 0; i < n; i++)
                xg.sn[k][i] = (ml[i] + mr[i]) >> 1;
    }
}
