/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, the capture ring's size, 16-bit capture, the playback removed)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors */
/* UAC1 six-channel capture ("ChoralRoot In"): Melodee's usb_audio_stream.c (Melodee 0.11.1) without its playback
 * (the computer's audio through the FM-1: removed in 0.14, docs/USB-AUDIO.md). No hardware dependencies: callers
 * serialize USB service and each short audio block (the host test tests/cr_usbaudio_test.c builds this file alone).
 *
 * Capture: master L R, CHORD L R, BASS L R (fx.c ua_stage). Capture is PCM16 only: the mix is 16-bit (Q15), a
 * 24-bit format would only pad a zero byte, and 45 frames x 6 channels x 2 bytes = 540 bytes per packet, the size
 * of Melodee's largest (4 channels x 24 bit), well inside a full-speed host's isochronous budget.
 *
 * The I2S clock (~44,117.6 Hz) is independent of USB SOF. A low-pass ring-fill servo adjusts the capture packet
 * lengths (asynchronous: the host follows the packets). USB and I2S both run at the native 44.1 kHz rate; no
 * resampler is needed. The capture ring is half of Melodee's (512 frames, a 256-frame target: 5.8 ms; Felucca 1.0's
 * stereo capture held its fill as low as 80 frames under load without a glitch), to fit the six channels in the
 * POOL. */
#include <stdint.h>
#define UA_RATE 44100u
#ifndef UA_CAP_CHANNELS
#define UA_CAP_CHANNELS 6u                      /* (fx.c defines it in the firmware) */
#endif
#define UA_MAX_FRAMES 45u                       /* ceil(44.1 + the servo's maximum correction) */
#define UA_PACKET (UA_MAX_FRAMES * UA_CAP_CHANNELS * 2u)   /* capture: PCM16 */
#define UA_CAP_RING 512u                        /* capture ring, frames (a power of two) */
#define UA_CAP_TARGET 256u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)
#ifdef __APPLE__
#define UA_POOL                                 /* (the host tests: Mach-O has no such section) */
#else
#define UA_POOL __attribute__((section(".pool")))   /* the ring, 6 KiB: the pool (zeroed at boot), not RAM */
#endif

static struct {
    uint8_t cap_alt, cap_ready;
    uint32_t cw, cr, cap_frac;
    int32_t cap_fill_q8;
    uint32_t cap_underruns, cap_overruns;
    uint32_t tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t cap[UA_CAP_RING * UA_CAP_CHANNELS];
} ua UA_POOL;

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_CAP_TARGET * 256;
}

static void ua_reset(void)
{
    ua.cap_alt = 0;
    ua_cap_reset();
}

/* Called once per observed USB frame, not once per poll or audio callback. */
static void ua_sof(void)
{
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

static uint32_t ua_rate(int32_t error_q8)
{
    int32_t correction = error_q8 / 16;         /* fill error / 1024, in 10.14 */
    if (correction > 8192)
        correction = 8192;
    if (correction < -8192)
        correction = -8192;
    return (uint32_t)((int32_t)UA_NOMINAL + correction);
}

/* the next capture packet into p (PCM16, little-endian): 44 or 45 frames, one more or fewer as the servo asks;
 * silence until the ring is primed. Returns its length in bytes */
static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0;
    ua.cap_frac += ua_rate(ua.cap_fill_q8 - UA_CAP_TARGET * 256);
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
    if (n > UA_MAX_FRAMES)
        n = UA_MAX_FRAMES;                      /* (the servo's bound keeps it at 45: belt and braces) */
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_CAP_TARGET)
        ua.cap_ready = 1;
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
    if (!ua.cap_alt)
        return;                                 /* (the host closed the stream during the block) */
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
