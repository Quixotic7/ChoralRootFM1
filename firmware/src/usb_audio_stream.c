/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, the capture ring's size, 16-bit capture, the playback removed; 0.19:
 * 48 kHz for six channels, after SLOOP 2.5's and Felucca 1.1.5's stereo resampler)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors */
/* UAC1 six-channel capture ("ChoralRoot In"): Melodee's usb_audio_stream.c (Melodee 0.11.1) without its playback
 * (the computer's audio through the FM-1: removed in 0.14, docs/USB-AUDIO.md). No hardware dependencies: callers
 * serialize USB service and each short audio block (the host test tests/cr_usbaudio_test.c builds this file alone).
 *
 * Capture: master L R, CHORD L R, BASS L R (fx.c ua_stage). Capture is PCM16 only: the mix is 16-bit (Q15), a
 * 24-bit format would only pad a zero byte; 45 frames x 6 channels x 2 bytes = 540 bytes the largest packet at
 * 44.1 kHz (49 frames, 588 bytes, at 48 kHz).
 *
 * The I2S clock (~44,117.6 Hz) is independent of USB SOF; the endpoint is asynchronous (the host follows the
 * packets' lengths). The lengths are a fixed 44.1-frame pattern (a 45-frame packet every 10th), the ring's drift
 * against it taken by a rare one-frame nudge (ua_transmit). (Melodee's fill servo, gone in 0.14: its lengths wandered
 * around 44.07 after every ring reset.) The capture ring is half of Melodee's (512 frames, a 256-frame
 * prime: 5.8 ms), to fit the six channels in the POOL. docs/USB-AUDIO.md, "What went wrong and why".
 *
 * 48 kHz (0.19; SLOOP 2.5, https://github.com/isod89/sloop-fm1, after Felucca 1.1.5, Leo Kuroshita: "the FM-1's USB
 * audio input now also runs at 48 kHz when the host asks for it"): the format has two discrete rates and the host
 * picks one with SET_CUR on the endpoint (ua_rate_set). The synth stays at 44.1 kHz. At 48 kHz the audio ISR resamples
 * each block's six channels x 160 / 147 (ua_resample: SLOOP's polyphase FIR, uac_fir.h, 12 taps a phase in Q15;
 * Felucca's is 24 taps in Q30; both are stereo) and the ring holds 48 kHz frames; a packet is 48 frames, 47 or 49
 * for the drift. At 44.1 kHz, the rate until a host asks for the other, nothing is resampled and the packets are
 * what they were, byte for byte (tests/cr_usbaudio_test.c "golden"). docs/USB-AUDIO.md "48 kHz". */
#include <stdint.h>
#include "uac_fir.h"
#define UA_RATE 44100u
#define UA_RATE48 48000u
#ifndef UA_CAP_CHANNELS
#define UA_CAP_CHANNELS 6u                      /* (fx.c defines it in the firmware) */
#endif
#define UA_MAX_FRAMES 45u                       /* 44 or 45 frames a packet (44.1 kHz) */
#define UA_FRAMES48 48u                         /* 48 kHz: 48 frames a packet, 47 or 49 on a nudge */
#define UA_PACKET ((UA_FRAMES48 + 1u) * UA_CAP_CHANNELS * 2u)   /* capture: PCM16; the largest packet (49 frames) */
#define UA_CAP_RING 512u                        /* capture ring, frames (a power of two) */
#define UA_CAP_TARGET 256u                      /* the prime: data once the ring holds this much */
#define UA_PATTERN ((UA_RATE * 16384u) / 1000u) /* 44.1 frames a packet, 10.14 (722534: 44.09998) */
/* the drift: +-1 frame on a packet (a 44 made 45, a 45 made 44: packets stay 44 or 45), at most once in
 * UA_NUDGE_EVERY packets and only while the fill is outside UA_FILL_LO..UA_FILL_HI. At the I2S's +400 ppm the fill
 * reaches 384 after ~7 s and takes a nudge in ~57 packets; a -1000 ppm clock is held too (the host tests). The same
 * band and pace at 48 kHz (a 48 made 49 or 47) */
#define UA_NUDGE_EVERY 10u
#define UA_FILL_LO 128u
#define UA_FILL_HI 384u
/* wMaxPacketSize (usb_audio_desc.h): the bus time the host reserves for a packet. The data is 528 / 540 bytes, but
 * bit stuffing (a 0 after six 1s: small negative samples, 0xFFxx, are full of them) stretches it on the wire up to
 * 7/6. Reserving exactly 540, through a USB 2 hub's split transactions, the host's schedule was overrun and macOS
 * restarted the stream every 0.1-0.5 s while audio flowed (the device, 2026-10-10: all-0xFFFF samples the worst,
 * zeros never; docs/USB-AUDIO.md). 640 covered the worst case at 44.1 kHz (540 x 7 / 6 = 630). 0.19: the one
 * endpoint serves both rates and a 48 kHz packet is up to 49 frames, 588 bytes: 588 x 7 / 6 = 686, so 688; the
 * full-speed maximum is 1023 */
#define UA_EP_MAXP 688u
#define UA_EP_MAXP44 640u                       /* with 48 kHz not offered (usb_audio.c UA_OFF_48): 0.18's, 45 frames */
_Static_assert(UA_EP_MAXP >= UA_PACKET * 7u / 6u && UA_EP_MAXP <= 1023u,
               "wMaxPacketSize: the largest packet, bit-stuffed .. 1023");
/* the resampler (48 kHz): the audio ISR's block is CTL = 32 frames; 32 x 160 / 147 = 34.8: 34 or 35 frames out */
#define UA_RS_BLOCK 32u
#define UA_RS_OUT ((UA_RS_BLOCK * UAC_FIR_L + UAC_FIR_M - 1u) / UAC_FIR_M + 1u)   /* 36: a block's output and one */
#define UA_RS_HIST (UAC_FIR_N - 1u)             /* frames before a block that its first windows reach back to */
_Static_assert(UA_FILL_HI + UA_RS_OUT + UA_FRAMES48 + 1u < UA_CAP_RING && UA_FILL_LO > UA_FRAMES48 + 1u,
               "the ring holds the band, a block and a packet (48 kHz)");
#ifdef __APPLE__
#define UA_POOL                                 /* (the host tests: Mach-O has no such section) */
#else
#define UA_POOL __attribute__((section(".pool")))   /* the ring, 6 KiB: the pool (zeroed at boot), not RAM */
#endif

static struct {
    uint8_t cap_alt, cap_ready;
    uint8_t cap_hold;                           /* the stream is open but the host has not taken a packet yet (or
                                                 * stopped taking them): the ring is not fed (usb_audio.c ua_tx_fill) */
    int8_t cap_pend;                            /* a +-1 frame nudge waiting for its packet */
    uint32_t cap_nudge, cap_nudges_up, cap_nudges_down;   /* packets since the last nudge; nudges made */
    uint32_t cw, cr, cap_frac;
    int32_t cap_fill_q8;                        /* the fill, low-passed per frame (diagnostics: --debug) */
    uint32_t cap_underruns, cap_overruns;
    uint32_t cap_reset_ms;                      /* 3y48: fm1_ms of the latest under- or overrun (0: none) */
    uint32_t tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t cap[UA_CAP_RING * UA_CAP_CHANNELS];
    /* 0.19, 48 kHz (behind the ring: what was here keeps its place) */
    uint8_t r48;                                /* the host's rate (SET_CUR, ua_rate_set): 1 = 48 kHz; 0 = 44.1 kHz,
                                                 * also after a bus reset or a new configuration (ua_reset) */
    uint8_t rs_on;                              /* the resampler's history is this stream's (0: it starts from silence) */
    uint32_t rs_ph;                             /* the next output frame's phase, 0..159, Q16 (a fractional step) */
    int32_t rs_adj;                             /* the step's trim, Q16 phases (ua_hc_steer): the I2S's frames
                                                 * resampled to the host's own 48 kHz, so every packet is 48 frames */
    int32_t rs_int, rs_lp;                      /* the trim's integral (the learned ratio, kept over streams);
                                                 * the ring's fill low-passed, Q8 */
    uint32_t rate_sets, rate_changes;           /* SET_CUR sampling-frequency requests; those that changed the rate */
    uint32_t rs_blocks, rs_ticks, rs_max_ticks; /* blocks resampled; their time (usb_audio.c ua_block48: halved with
                                                 * rs_blocks at 65536, so rs_ticks / rs_blocks is a recent average)
                                                 * and the longest, TIMER5 nested in it included */
    /* the resampler's history, six channels a frame: the last UA_RS_HIST frames of the block before, then room for
     * the first UA_RS_HIST of the block being resampled (ua_resample) */
    int16_t rs_in[2u * UA_RS_HIST * UA_CAP_CHANNELS];
    int16_t rs_out[UA_RS_OUT * UA_CAP_CHANNELS];   /* a block at 48 kHz, for ua_feed */
} ua UA_POOL;

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.rs_lp = (int32_t)(UA_CAP_TARGET << 8);       /* the trim's low-pass starts on target (ua_hc_steer) */
    ua.cap_nudge = 0;
    ua.cap_pend = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_CAP_TARGET * 256;
}

static void ua_reset(void)
{
    ua.rs_int = ua.rs_adj = 0;                   /* a new connection: the clocks' ratio learned again */
    ua.cap_alt = 0;
    ua.r48 = 0;                                 /* (a bus reset, a new configuration, usb_start: 44.1 kHz again) */
    ua.rs_on = 0;
    ua_cap_reset();
}

/* SET_CUR sampling frequency (usb_audio.c ua_control_data; any time, before or after SET_INTERFACE): the nearer of
 * the two rates (UAC1 rounds to a supported one). A change empties the ring: silence packets until it is primed at
 * the new rate, and the resampler starts from silence (ua_resample). Returns 1 if the rate changed; the same rate
 * again leaves a running stream alone */
static int ua_rate_set(uint32_t hz)
{
    uint8_t r48 = (uint8_t)(hz >= (UA_RATE + UA_RATE48) / 2u);
    ua.rate_sets++;
    if (r48 == ua.r48)
        return 0;
    ua.r48 = r48;
    ua.rs_on = 0;
    ua.rate_changes++;
    ua_cap_reset();
    return 1;
}

/* Called once per observed USB frame, not once per poll or audio callback (the fill's low-pass: diagnostics). */
static void ua_sof(void)
{
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

/* the next capture packet into p (PCM16, little-endian): 44 or 45 frames by the pattern and the nudge (at 48 kHz:
 * 48 frames, 49 or 47 on a nudge); silence until the ring is primed. Returns its length in bytes */
/* 48 kHz ON THE HOST'S CLOCK (felucca-bsp FELUCCA_UAC_HOST_CLOCK's idea, for ChoralRoot's six channels and its
 * 160 / 147 resampler): the I2S runs ~400 ppm fast (24 MHz / 544), so the ring gained a frame every ~52 ms and a
 * 49-frame packet took it. macOS follows the packets' lengths; an iPhone recording video does not: it dropped a
 * chunk every ~6 s (a click in the video's sound; an iPhone filming at 44.1 and 48 kHz). Here the resampler's step is trimmed instead, so the
 * ring holds its fill and every packet is 48 frames: a PI loop on the fill, once a packet, low-passed (1/64 a packet),
 * critically damped near wn 0.56 rad/s (the plant: 48000 / (147 << 16) frames a second per unit of trim), the
 * integral the clocks' learned ratio. The nudge stays as a safety outside the band */
#define UA_HC_KI_SH 9                            /* the integral's term: rs_int >> 9 */
#define UA_HC_MAX 19270                          /* +-2000 ppm of the step */
#define UA_HC_FAR 24                             /* frames off the target: the fast gains (Felucca's HC_E1) */
static void ua_hc_steer(uint32_t fill)
{
    int32_t e, a, far;
    ua.rs_lp += ((int32_t)(fill << 8) - ua.rs_lp) >> 6;
    e = (ua.rs_lp >> 8) - (int32_t)UA_CAP_TARGET;
    far = e > UA_HC_FAR || e < -UA_HC_FAR;
    ua.rs_int += far ? e << 5 : e;               /* fast: wn ~0.56 rad/s (the ratio learned in seconds); near the
                                                  * target slow, wn ~0.1 rad/s: the pitch steady (tens of ppm) */
    if (ua.rs_int > (UA_HC_MAX << UA_HC_KI_SH)) ua.rs_int = UA_HC_MAX << UA_HC_KI_SH;
    if (ua.rs_int < -(UA_HC_MAX << UA_HC_KI_SH)) ua.rs_int = -(UA_HC_MAX << UA_HC_KI_SH);
    a = e * (far ? 200 : 40) + (ua.rs_int >> UA_HC_KI_SH);
    ua.rs_adj = a > UA_HC_MAX ? UA_HC_MAX : a < -UA_HC_MAX ? -UA_HC_MAX : a;
}
static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0;
    if (ua.r48)
        n = UA_FRAMES48;                        /* 48 frames a ms: no pattern */
    else {
        ua.cap_frac += UA_PATTERN;
        n = ua.cap_frac >> 14;
        ua.cap_frac &= 16383u;
        if (n > UA_MAX_FRAMES)
            n = UA_MAX_FRAMES;                  /* (the pattern keeps it at 45: belt and braces) */
    }
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_CAP_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready && ua.r48)
        ua_hc_steer(ua.cw - ua.cr);
    if (ua.cap_ready) {                         /* the drift: one frame more or fewer, rarely, on a 44 or a 45 */
        uint32_t f = ua.cw - ua.cr;
        if (ua.cap_nudge < UA_NUDGE_EVERY)
            ua.cap_nudge++;
        else if (!ua.cap_pend && (f > UA_FILL_HI || f < UA_FILL_LO))
            ua.cap_pend = f > UA_FILL_HI ? 1 : -1;
        if (ua.r48) {                           /* (48 kHz: every packet is a 48, so the next one takes it) */
            if (ua.cap_pend) {
                if (ua.cap_pend > 0) {
                    n++;
                    ua.cap_nudges_up++;
                } else {
                    n--;
                    ua.cap_nudges_down++;
                }
                ua.cap_pend = 0;
                ua.cap_nudge = 0;
            }
        } else if (ua.cap_pend > 0 && n == UA_MAX_FRAMES - 1u) {
            n++;
            ua.cap_pend = 0;
            ua.cap_nudge = 0;
            ua.cap_nudges_up++;
        } else if (ua.cap_pend < 0 && n == UA_MAX_FRAMES) {
            n--;
            ua.cap_pend = 0;
            ua.cap_nudge = 0;
            ua.cap_nudges_down++;
        }
    }
    if (ua.cap_ready) {
        if (ua.cw - ua.cr >= n)
            take = 1;
        else {
            ua.cap_underruns++;
            ua.cap_reset_ms = fm1_ms | 1u;
            ua_cap_reset();
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t ch;
        uint8_t *o = p + i * UA_CAP_CHANNELS * 2u;
        if (take) {
            const int16_t *f = &ua.cap[(ua.cr++ & (UA_CAP_RING - 1u)) * UA_CAP_CHANNELS];
            for (ch = 0; ch < UA_CAP_CHANNELS; ch++) {
                o[2u * ch] = (uint8_t)f[ch];
                o[2u * ch + 1u] = (uint8_t)((uint16_t)f[ch] >> 8);
            }
        } else {
            for (ch = 0; ch < 2u * UA_CAP_CHANNELS; ch++)
                o[ch] = 0;
        }
    }
    return n * 2u * UA_CAP_CHANNELS;
}

/* The audio ISR, once per block of CTL (32) frames while the computer records, with USB service excluded during
 * this copy (the IRQs off): stage is the block's capture frames at the rate they were made for (r48: 0 = fx.c
 * ua_stage as it is, the master pair and the parts; 1 = ua.rs_out, the same resampled). A block made for the other
 * rate (the host changed it meanwhile) is dropped */
static inline __attribute__((always_inline)) void ua_feed(const int16_t *stage, uint32_t n, uint32_t r48)
{
    uint32_t i, ch;
    if (!ua.cap_alt || ua.cap_hold || r48 != ua.r48)
        return;                                 /* (the host closed the stream during the block, or reads not yet) */
    if (ua.cw - ua.cr + n > UA_CAP_RING) {
        ua.cap_overruns++;
        ua.cap_reset_ms = fm1_ms | 1u;
        ua_cap_reset();
    }
    for (i = 0; i < n; i++) {
        uint32_t ci = ((ua.cw + i) & (UA_CAP_RING - 1u)) * UA_CAP_CHANNELS;
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
            ua.cap[ci + ch] = stage[i * UA_CAP_CHANNELS + ch];
    }
    ua.cw += n;
}

static inline void ua_audio(const int16_t *stage, uint32_t n)   /* a 44.1 kHz block (the stream as it always was) */
{
    ua_feed(stage, n, 0);
}

/* ------------------------------------------------------------------------------------------------ 48 kHz --- */
static inline int16_t ua_rs_sat(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* three channels of one output frame: the UAC_FIR_N taps c[0], c[step], .. over the window w (oldest frame first,
 * six channels a frame), Q15, rounded. The sums are 64 bits wide for the unit's sake, not for their size (a phase's
 * taps sum to 1.0 and their absolute values to under 2.0: 32 bits hold them): its compiler then multiplies and adds
 * in one instruction (r7_r6 += r10 * r4), and with three sums the taps and the samples stay in registers. Written
 * out tap by tap: no loop, 13 instructions a tap for three channels. Six sums at once, or a loop over the taps, cost
 * a third more on the unit (docs/USB-AUDIO.md "48 kHz": the count from the disassembly) */
#define UA_TAP3(j) do { \
        int32_t t = c[(int32_t)(j) * step]; \
        a0 += (int64_t)t * w[(j) * UA_CAP_CHANNELS]; \
        a1 += (int64_t)t * w[(j) * UA_CAP_CHANNELS + 1u]; \
        a2 += (int64_t)t * w[(j) * UA_CAP_CHANNELS + 2u]; \
    } while (0)
static inline __attribute__((always_inline)) void ua_fir3(const int16_t *w, const int16_t *c, int32_t step, int16_t *o)
{
    int64_t a0, a1, a2;
    a0 = a1 = a2 = 1 << (UAC_FIR_Q - 1);
    UA_TAP3(0); UA_TAP3(1); UA_TAP3(2); UA_TAP3(3); UA_TAP3(4); UA_TAP3(5);
    UA_TAP3(6); UA_TAP3(7); UA_TAP3(8); UA_TAP3(9); UA_TAP3(10); UA_TAP3(11);
    o[0] = ua_rs_sat((int32_t)(a0 >> UAC_FIR_Q));
    o[1] = ua_rs_sat((int32_t)(a1 >> UAC_FIR_Q));
    o[2] = ua_rs_sat((int32_t)(a2 >> UAC_FIR_Q));
}
_Static_assert(UAC_FIR_N == 12u && UA_CAP_CHANNELS == 6u, "ua_fir3 is written out for 12 taps, twice three channels");

/* one output frame of six channels from the window w with the taps c[0..11] (a phase of the table) or c[0], c[-1],
 * .. (its mirror: phase 159 - p is phase p read backwards, the prototype is symmetric). Functions of their own, not
 * inlined into ua_resample: there the unit's compiler ran out of registers and kept the taps on the stack (277
 * instructions a frame for these 230) */
static __attribute__((noinline)) void ua_fir6_fwd(const int16_t *w, const int16_t *c, int16_t *o)
{
    ua_fir3(w, c, 1, o);
    ua_fir3(w + 3, c, 1, o + 3);
}

static __attribute__((noinline)) void ua_fir6_bwd(const int16_t *w, const int16_t *c, int16_t *o)
{
    ua_fir3(w, c, -1, o);
    ua_fir3(w + 3, c, -1, o + 3);
}

static inline void ua_rs_copy(int16_t *d, const int16_t *s, uint32_t frames)   /* (d below s if they overlap) */
{
    for (; frames; frames--, d += UA_CAP_CHANNELS, s += UA_CAP_CHANNELS) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
        d[4] = s[4];
        d[5] = s[5];
    }
}

/* 44.1 -> 48 kHz (x 160 / 147), one block of n <= UA_RS_BLOCK frames of six channels into ua.rs_out; returns the
 * frames made (n x 160 / 147: 34 or 35 for 32, exactly 160 for every 147 in). SLOOP 2.5's uac_tap48 (after Felucca
 * 1.1.5) for six channels: each input frame is the newest of the 1 or 2 output frames at phase ph, whose window is
 * the UAC_FIR_N frames ending at it. The windows of the block's first UA_RS_HIST frames reach back into the block
 * before: they run over ua.rs_in (that block's last frames, then copies of these first ones); the others run over
 * the block where it lies. The audio ISR only, interrupts on (TIMER5 and its USB service nest); the first block of
 * a stream, or after a rate change, starts from a silent history */
static __attribute__((noinline)) uint32_t ua_resample(const int16_t *in, uint32_t n)
{
    uint32_t i, k = 0, ph = ua.rs_ph, m, step = (UAC_FIR_M << 16) + (uint32_t)ua.rs_adj;   /* (trimmed) */
    int16_t *o = ua.rs_out;
    const int16_t *x;
    if (n > UA_RS_BLOCK)
        n = UA_RS_BLOCK;
    if (!ua.rs_on) {
        ua.rs_on = 1;
        for (i = 0; i < UA_RS_HIST * UA_CAP_CHANNELS; i++)
            ua.rs_in[i] = 0;
        ph = 0;
    }
    m = n < UA_RS_HIST ? n : UA_RS_HIST;
    ua_rs_copy(ua.rs_in + UA_RS_HIST * UA_CAP_CHANNELS, in, m);
    x = ua.rs_in;                                       /* x: the window that ends at input frame i */
    for (i = 0; i < n; i++, x += UA_CAP_CHANNELS) {
        if (i == m)
            x = in;                                     /* (frame UA_RS_HIST's window is the block's first 12) */
        do {
            uint32_t q = (ph + 0x8000u) >> 16;          /* the nearest of the 160 phases: at trim 0 exactly
                                                         * the phase it always was (ph a multiple of 1 << 16) */
            if (q > UAC_FIR_L - 1u)
                q = UAC_FIR_L - 1u;
            if (q < UAC_FIR_L / 2u)
                ua_fir6_fwd(x, UAC_FIR[q], o);
            else
                ua_fir6_bwd(x, &UAC_FIR[UAC_FIR_L - 1u - q][UAC_FIR_N - 1u], o);
            o += UA_CAP_CHANNELS;
            k++;
            ph += step;
        } while (ph < (UAC_FIR_L << 16));
        ph -= UAC_FIR_L << 16;
    }
    if (n >= UA_RS_HIST)                                /* the next block's history: this one's last frames */
        ua_rs_copy(ua.rs_in, in + (n - UA_RS_HIST) * UA_CAP_CHANNELS, UA_RS_HIST);
    else                                                /* (a short block, the tests': the last 11 of both) */
        ua_rs_copy(ua.rs_in, ua.rs_in + n * UA_CAP_CHANNELS, UA_RS_HIST);
    ua.rs_ph = ph;
    return k;
}
