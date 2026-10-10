/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, the capture ring's size, 16-bit capture, the playback removed)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors */
/* UAC1 six-channel capture ("ChoralRoot In"): Melodee's usb_audio_stream.c (Melodee 0.11.1) without its playback
 * (the computer's audio through the FM-1: removed in 0.14, docs/USB-AUDIO.md). No hardware dependencies: callers
 * serialize USB service and each short audio block (the host test tests/cr_usbaudio_test.c builds this file alone).
 *
 * Capture: master L R, CHORD L R, BASS L R (fx.c ua_stage). Capture is PCM16 only: the mix is 16-bit (Q15), a
 * 24-bit format would only pad a zero byte; 45 frames x 6 channels x 2 bytes = 540 bytes the largest packet.
 *
 * The I2S clock (~44,117.6 Hz) is independent of USB SOF; the endpoint is asynchronous (the host follows the
 * packets' lengths). The lengths are a fixed 44.1-frame pattern (a 45-frame packet every 10th), the ring's drift
 * against it taken by a rare one-frame nudge (ua_transmit). (Melodee's fill servo, gone in 0.14: its lengths wandered
 * around 44.07 after every ring reset.) No resampler. The capture ring is half of Melodee's (512 frames, a 256-frame
 * prime: 5.8 ms), to fit the six channels in the POOL. docs/USB-AUDIO.md, "What went wrong and why". */
#include <stdint.h>
#define UA_RATE 44100u
#ifndef UA_CAP_CHANNELS
#define UA_CAP_CHANNELS 6u                      /* (fx.c defines it in the firmware) */
#endif
#define UA_MAX_FRAMES 45u                       /* 44 or 45 frames a packet */
#define UA_PACKET (UA_MAX_FRAMES * UA_CAP_CHANNELS * 2u)   /* capture: PCM16 */
#define UA_CAP_RING 512u                        /* capture ring, frames (a power of two) */
#define UA_CAP_TARGET 256u                      /* the prime: data once the ring holds this much */
#define UA_PATTERN ((UA_RATE * 16384u) / 1000u) /* 44.1 frames a packet, 10.14 (722534: 44.09998) */
/* the drift: +-1 frame on a packet (a 44 made 45, a 45 made 44: packets stay 44 or 45), at most once in
 * UA_NUDGE_EVERY packets and only while the fill is outside UA_FILL_LO..UA_FILL_HI. At the I2S's +400 ppm the fill
 * reaches 384 after ~7 s and takes a nudge in ~57 packets; a -1000 ppm clock is held too (the host tests) */
#define UA_NUDGE_EVERY 10u
#define UA_FILL_LO 128u
#define UA_FILL_HI 384u
/* wMaxPacketSize (usb_audio_desc.h): the bus time the host reserves for a packet. The data is 528 / 540 bytes, but
 * bit stuffing (a 0 after six 1s: small negative samples, 0xFFxx, are full of them) stretches it on the wire up to
 * 7/6. Reserving exactly 540, through a USB 2 hub's split transactions, the host's schedule was overrun and macOS
 * restarted the stream every 0.1-0.5 s while audio flowed (the device, 2026-10-10: all-0xFFFF samples the worst,
 * zeros never; docs/USB-AUDIO.md). 640 covers the worst case (540 x 7 / 6 = 630); the full-speed maximum is 1023 */
#define UA_EP_MAXP 640u
_Static_assert(UA_EP_MAXP >= UA_PACKET && UA_EP_MAXP <= 1023u, "wMaxPacketSize: the largest packet .. 1023");
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
    uint32_t tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t cap[UA_CAP_RING * UA_CAP_CHANNELS];
} ua UA_POOL;

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_nudge = 0;
    ua.cap_pend = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_CAP_TARGET * 256;
}

static void ua_reset(void)
{
    ua.cap_alt = 0;
    ua_cap_reset();
}

/* Called once per observed USB frame, not once per poll or audio callback (the fill's low-pass: diagnostics). */
static void ua_sof(void)
{
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

/* the next capture packet into p (PCM16, little-endian): 44 or 45 frames by the pattern and the nudge; silence until
 * the ring is primed. Returns its length in bytes */
static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0;
    ua.cap_frac += UA_PATTERN;
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
    if (n > UA_MAX_FRAMES)
        n = UA_MAX_FRAMES;                      /* (the pattern keeps it at 45: belt and braces) */
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_CAP_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready) {                         /* the drift: one frame more or fewer, rarely, on a 44 or a 45 */
        uint32_t f = ua.cw - ua.cr;
        if (ua.cap_nudge < UA_NUDGE_EVERY)
            ua.cap_nudge++;
        else if (!ua.cap_pend && (f > UA_FILL_HI || f < UA_FILL_LO))
            ua.cap_pend = f > UA_FILL_HI ? 1 : -1;
        if (ua.cap_pend > 0 && n == UA_MAX_FRAMES - 1u) {
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
 * this copy (the IRQs off): stage is the block's capture frames (fx.c ua_stage: the master pair, the parts) */
static void ua_audio(const int16_t *stage, uint32_t n)
{
    uint32_t i, ch;
    if (!ua.cap_alt || ua.cap_hold)
        return;                                 /* (the host closed the stream during the block, or reads not yet) */
    if (ua.cw - ua.cr + n > UA_CAP_RING) {
        ua.cap_overruns++;
        ua_cap_reset();
    }
    for (i = 0; i < n; i++) {
        uint32_t ci = ((ua.cw + i) & (UA_CAP_RING - 1u)) * UA_CAP_CHANNELS;
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
            ua.cap[ci + ch] = stage[i * UA_CAP_CHANNELS + ch];
    }
    ua.cw += n;
}
