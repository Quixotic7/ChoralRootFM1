/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, the capture ring's size, 16-bit capture) Copyright (C) 2026 ChoralRoot
 * FM-1 contributors */
/* UAC1 stereo playback ("ChoralRoot Out") and six-channel capture ("ChoralRoot In"): Melodee's usb_audio_stream.c
 * (Melodee 0.11.1). No hardware dependencies: callers serialize USB service and each short audio block (the host
 * test tests/cr_usbaudio_test.c builds this file alone).
 *
 * Capture: master L R, CHORD L R, BASS L R (fx.c ua_stage). The master pair is taken before the host's playback is
 * mixed in, so DAW monitoring cannot feed itself through capture. Capture is PCM16 only: the mix is 16-bit (Q15),
 * a 24-bit format would only pad a zero byte, and 45 frames x 6 channels x 2 bytes = 540 bytes per packet, the size
 * of Melodee's largest (4 channels x 24 bit), well inside a full-speed host's isochronous budget with playback.
 * Playback: PCM16 or packed PCM24 (the low byte dropped), added to the output after the master limiter, scaled by
 * MASTER, saturated (Melodee's choice: the computer's audio is not limited or coloured by ChoralRoot's master).
 *
 * The I2S clock (~44,117.6 Hz) is independent of USB SOF. A low-pass ring-fill servo adjusts capture packet lengths
 * and the playback endpoint's explicit 10.14 feedback. USB and I2S both run at the native 44.1 kHz rate; no
 * resampler is needed. The capture ring is half of Melodee's (512 frames, a 256-frame target: 5.8 ms; Felucca 1.0's
 * stereo capture held its fill as low as 80 frames under load without a glitch), to fit the six channels in the
 * POOL; playback keeps Melodee's 1024 / 512. */
#include <stdint.h>
#define UA_RATE 44100u
#ifndef UA_CAP_CHANNELS
#define UA_CAP_CHANNELS 6u                      /* (fx.c defines it in the firmware) */
#endif
#define UA_MAX_FRAMES 45u                       /* ceil(44.1 + maximum feedback correction) */
#define UA_PLAY_PACKET (UA_MAX_FRAMES * 2u * 3u)
#define UA_PACKET (UA_MAX_FRAMES * UA_CAP_CHANNELS * 2u)   /* capture: PCM16 */
#define UA_RING 1024u                           /* playback ring, frames (a power of two) */
#define UA_TARGET 512u
#define UA_CAP_RING 512u                        /* capture ring, frames (a power of two) */
#define UA_CAP_TARGET 256u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)
#ifdef __APPLE__
#define UA_POOL                                 /* (the host tests: Mach-O has no such section) */
#else
#define UA_POOL __attribute__((section(".pool")))   /* the rings, 10 KiB: the pool (zeroed at boot), not RAM */
#endif

static struct {
    uint8_t play_alt, cap_alt, play_ready, cap_ready;
    uint32_t pw, pr, cw, cr, cap_frac;
    int32_t play_fill_q8, cap_fill_q8;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets;
    uint32_t rx_packets, tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t play[UA_RING * 2u], cap[UA_CAP_RING * UA_CAP_CHANNELS];
} ua UA_POOL;

static int32_t ua_clip(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

static uint32_t ua_sample_bytes(uint8_t alt) { return alt == 2u ? 3u : 2u; }

/* Packed little-endian PCM24 uses the same full-scale level as PCM16: the low byte is discarded on playback */
static int16_t ua_decode(const uint8_t *p, uint32_t width)
{
    p += width - 2u;
    return (int16_t)(p[0] | (uint16_t)p[1] << 8);
}

static void ua_play_reset(void)
{
    ua.pw = ua.pr = 0;
    ua.play_ready = 0;
    ua.play_fill_q8 = UA_TARGET * 256;
}

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_CAP_TARGET * 256;
}

static void ua_reset(void)
{
    ua.play_alt = ua.cap_alt = 0;
    ua_play_reset();
    ua_cap_reset();
}

static void ua_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t i, width = ua_sample_bytes(ua.play_alt), frame = width * 2u;
    uint32_t n = bytes / frame;
    if (!ua.play_alt)
        return;
    if (n > UA_MAX_FRAMES || bytes % frame) {
        ua.bad_packets++;
        return;
    }
    if (ua.pw - ua.pr + n > UA_RING) {
        ua.play_overruns++;
        ua_play_reset();                       /* re-prime, never replay stale data */
    }
    for (i = 0; i < n; i++) {
        uint32_t at = (ua.pw & (UA_RING - 1u)) * 2u;
        ua.play[at] = ua_decode(p + frame * i, width);
        ua.play[at + 1u] = ua_decode(p + frame * i + width, width);
        ua.pw++;
    }
    ua.rx_packets++;
}

/* Called once per observed USB frame, not once per poll or audio callback. */
static void ua_sof(void)
{
    if (ua.play_ready)
        ua.play_fill_q8 += ((int32_t)(ua.pw - ua.pr) * 256 - ua.play_fill_q8) / 32;
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

static uint32_t ua_feedback(void)
{
    return ua_rate(UA_TARGET * 256 - ua.play_fill_q8);
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

/* The audio ISR, once per block of CTL (32) frames, with USB service excluded during this copy (the IRQs off):
 * out is the stereo Q15 output (the DAC's, MASTER applied), stage the block's capture frames (fx.c ua_stage: the
 * master pair before this playback, the parts; 0: none staged this block, the recording started during it).
 * Capture first, then the host's playback x MASTER into out. */
static void ua_audio(int32_t *out, const int16_t *stage, uint32_t n, uint32_t master_q12)
{
    uint32_t i, capture = ua.cap_alt && stage, playback = 0;
    if (capture && ua.cw - ua.cr + n > UA_CAP_RING) {
        ua.cap_overruns++;
        ua_cap_reset();
    }
    if (ua.play_alt) {
        if (!ua.play_ready && ua.pw - ua.pr >= UA_TARGET)
            ua.play_ready = 1;
        if (ua.play_ready) {
            if (ua.pw - ua.pr >= n)
                playback = 1;
            else {
                ua.play_underruns++;
                ua_play_reset();
            }
        }
    }
    if (capture) {
        for (i = 0; i < n; i++) {
            uint32_t ch, ci = ((ua.cw + i) & (UA_CAP_RING - 1u)) * UA_CAP_CHANNELS;
            for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                ua.cap[ci + ch] = stage[i * UA_CAP_CHANNELS + ch];
        }
        ua.cw += n;
    }
    if (playback) {
        for (i = 0; i < n; i++) {
            uint32_t pi = ((ua.pr + i) & (UA_RING - 1u)) * 2u;
            out[2u * i] = ua_clip(out[2u * i] + ((ua.play[pi] * (int32_t)master_q12) >> 12));
            out[2u * i + 1u] = ua_clip(out[2u * i + 1u] + ((ua.play[pi + 1u] * (int32_t)master_q12) >> 12));
        }
        ua.pr += n;
    }
}
