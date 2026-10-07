/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's USB audio (Melodee's, docs/USB-AUDIO.md): firmware/src/usb.c with FELUCCA_UAC=1 and
 * FELUCCA_CDC=1, its usb_audio.c, usb_audio_stream.c and usb_audio_desc.h. The SIE is never touched (no usb_poll,
 * ua_hw_poll, SET_INTERFACE); what runs is what a host sees and what the two ISRs exchange:
 *   descriptors  every presentation (both devices, Out only, In only, both off = the console) as a host parses it:
 *                lengths, interface numbering, IADs, the AC headers' stream lists, terminals, formats, endpoint
 *                sizes against the packets the code produces, the strings (product, devices, channel names)
 *   routing      the capture frames (master, CHORD, BASS) into the ring as staged, the host's playback x MASTER into
 *                the output after them (no loopback), saturation, PCM16 / PCM24 decoding, bad packets
 *   packing      capture packets: PCM16 little-endian, six channels in order, silence until the ring is primed
 *   drift        20 s of the I2S clock (44,117.6 Hz, or a slow one) against the host's 1 kHz frames: renders of 128
 *                frames at random times inside their half, the host following the explicit feedback: no underrun or
 *                overrun after the start, every frame once and in order both ways, the rates matched
 *   recovery     the host stops reading / sending: the overrun / underrun is counted, the stream re-primes, and
 *                runs in order again
 *   --bench      host instructions for a half's USB work (both streams; macOS) and the device estimate (perf.sh)
 *   cc -std=gnu11 -O2 -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c   (tests/run_cr_tests.sh) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 1
#define FELUCCA_UAC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb.c"

static int fails, passes;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (ok)
        passes++;
    else
        fails++;
}

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

/* ------------------------------------------------------------------ descriptors --- */
static int str_is(uint32_t k, const char *want)          /* string k decodes (UTF-16LE) to want */
{
    const uint8_t *d;
    uint16_t l;
    uint32_t i, n = (uint32_t)strlen(want);
    if (!get_desc(0x0300u | k, &d, &l) || l != 2u + 2u * n || d[0] != l || d[1] != 3)
        return 0;
    for (i = 0; i < n; i++)
        if (d[2 + 2 * i] != (uint8_t)want[i] || d[3 + 2 * i])
            return 0;
    return 1;
}

static void present(uint8_t off)                          /* as usb_start: the console with both devices off */
{
    ua_off = off;
    usb_cdc_on = (uint8_t)((ua_off & (UA_OFF_OUT | UA_OFF_IN)) == (UA_OFF_OUT | UA_OFF_IN));
    ua_reset();
    ua_cfg_build();
}

/* one presentation parsed as a host does; play / cap: the function is expected */
static void parse(const char *tag, uint8_t off, int play, int cap, int cdc)
{
    const uint8_t *dev, *c, *d;
    uint16_t dl, n;
    uint32_t off_b, nif = 0, i;
    int lens = 1, eps = 1, contig = 1, iads = 1, aclist = 1, sizes = 1, sync = 1, fmt_cap = !cap, fmt_play = !play;
    int cur_if = -1, cur_neps = 0, got = 0, cur_sub = 0, cur_alt = 0, n_iad = 0, has_cdc = 0, n_midi_ep = 0;
    uint8_t seen[16] = {0}, sub_of[16] = {0};
    uint8_t lists[4][8], nlists = 0, nlist[4] = {0};
    char s[128];
    present(off);
    get_desc(0x0100, &dev, &dl);
    get_desc(0x0200, &c, &n);
    snprintf(s, sizeof s, "%s: device: 18 bytes, misc / IAD class, bcdDevice %s", tag, cdc ? "3.21" : "3.20");
    check(s, dl == 18 && dev[0] == 18 && dev[4] == 0xEF && dev[5] == 2 && dev[6] == 1 && dev[13] == 3 &&
             dev[12] == (cdc ? 0x21 : 0x20) && dev[14] == 1 && dev[15] == 2);
    snprintf(s, sizeof s, "%s: configuration: wTotalLength = the %u bytes sent", tag, n);
    check(s, c[1] == 2 && le16(c + 2) == n);
    for (off_b = 0; off_b < n; off_b += d[0]) {
        d = c + off_b;
        if (d[0] < 2 || off_b + d[0] > n) {
            lens = 0;
            break;
        }
        if (d[1] == 0x0B) {                                /* IAD: its interfaces follow, from the next number */
            n_iad++;
            if (d[0] != 8 || d[2] != (cur_if < 0 ? 0 : cur_if + 1))
                iads = 0;
        } else if (d[1] == 4) {
            if (cur_if >= 0 && got != cur_neps)
                eps = 0;
            if (d[2] != cur_if && d[2] != cur_if + 1)
                contig = 0;
            cur_if = d[2];
            cur_alt = d[3];
            cur_neps = d[4];
            cur_sub = d[5] == 1 ? d[6] : d[5] == 2 || d[5] == 0x0A ? 0x80 : 0;
            got = 0;
            if (cur_if < 16 && !seen[cur_if]) {
                seen[cur_if] = 1;
                sub_of[cur_if] = (uint8_t)cur_sub;
                nif++;
            }
            if (d[5] == 2 || d[5] == 0x0A)
                has_cdc = 1;
        } else if (d[1] == 5) {
            got++;
            if (d[2] == 0x01 || d[2] == 0x81)
                n_midi_ep++;
            if (cur_sub == 2 && (d[2] == 0x02 || d[2] == 0x82)) {   /* the audio data endpoints */
                uint32_t mp = le16(d + 4);
                if (d[0] != 9 || (d[3] & 3u) != 1 || d[6] != 1 || mp > 1023u)
                    sizes = 0;
                if (d[2] == 0x82 && mp < UA_PACKET)
                    sizes = 0;                             /* capture: ua_transmit's largest */
                if (d[2] == 0x02 && mp < UA_MAX_FRAMES * 2u * (cur_alt == 2 ? 3u : 2u))
                    sizes = 0;                             /* playback: ua_receive's largest */
                if (d[2] == 0x02 && (d[3] != 0x05 || d[8] != 0x83))
                    sync = 0;                              /* async, fed back on EP3 */
            }
            if (d[2] == 0x83 && cur_sub == 2 && !(d[3] == 0x11 && le16(d + 4) == 3))
                sync = 0;                                  /* explicit 10.14 feedback */
        } else if (d[1] == 0x24 && cur_sub == 1 && d[2] == 1 && nlists < 4) {   /* an AC header: its stream list */
            nlist[nlists] = d[7];
            for (i = 0; i < d[7] && i < 8; i++)
                lists[nlists][i] = d[8 + i];
            nlists++;
        } else if (d[1] == 0x24 && cur_sub == 2 && d[2] == 2) {                /* type I format */
            uint32_t rate = d[8] | d[9] << 8 | (uint32_t)d[10] << 16;
            if (d[4] == 6 && d[5] == 2 && d[6] == 16 && rate == 44100)
                fmt_cap = 1;
            if (d[4] == 2 && (d[5] == 2 || d[5] == 3) && rate == 44100)
                fmt_play = 1;
        }
    }
    if (cur_if >= 0 && got != cur_neps)
        eps = 0;
    for (i = 0; i < nlists; i++) {                         /* each AC header lists streaming interfaces that exist */
        uint32_t k;
        for (k = 0; k < nlist[i]; k++)
            if (lists[i][k] >= 16 || !seen[lists[i][k]] || (sub_of[lists[i][k]] != 2 && sub_of[lists[i][k]] != 3))
                aclist = 0;
    }
    snprintf(s, sizeof s, "%s: descriptor lengths, endpoints per interface", tag);
    check(s, lens && eps);
    snprintf(s, sizeof s, "%s: %u interfaces numbered 0.. in order = bNumInterfaces", tag, nif);
    check(s, contig && nif == c[4] && nif == ua_nif);
    snprintf(s, sizeof s, "%s: IADs (%d) start at the next interface; AC headers list existing streams", tag, n_iad);
    check(s, iads && aclist && n_iad == 1 + play + cap + cdc);
    snprintf(s, sizeof s, "%s: MIDI on EP1 OUT / IN%s", tag, cdc ? ", the console (CDC) present" : ", no console");
    check(s, n_midi_ep == 2 && has_cdc == cdc);
    snprintf(s, sizeof s, "%s: streams: Out %s (IF %d), In %s (IF %d)", tag, play ? "on" : "off",
             ua_if_play == UA_NO_IF ? -1 : ua_if_play, cap ? "on" : "off", ua_if_cap == UA_NO_IF ? -1 : ua_if_cap);
    check(s, (ua_if_play != UA_NO_IF) == play && (ua_if_cap != UA_NO_IF) == cap &&
             (!play || sub_of[ua_if_play] == 2) && (!cap || sub_of[ua_if_cap] == 2));
    snprintf(s, sizeof s, "%s: formats (In: 6 x PCM16; Out: 2 x PCM16 / PCM24, 44.1 kHz), packet sizes, feedback", tag);
    check(s, fmt_cap && fmt_play && sizes && sync);
}

static void test_descriptors(void)
{
    const uint8_t *d;
    uint16_t l;
    parse("Out + In", 0, 1, 1, 0);
    check("Out + In: 368 bytes, 6 interfaces (MIDI 0-1, Out 2-3, In 4-5)",
          ua_cfg_len == 368 && ua_if_play == 3 && ua_if_cap == 5);
    parse("Out only", UA_OFF_IN, 1, 0, 0);
    parse("In only", UA_OFF_OUT, 0, 1, 0);
    check("In only: the recording moves down to IF 2-3", ua_if_cap == 3 && ua_nif == 4);
    parse("both off", UA_OFF_OUT | UA_OFF_IN, 0, 0, 1);
    present(0);
    check("strings: the product \"ChoralRoot FM-1\" (the MIDI port), the maker", str_is(2, "ChoralRoot FM-1") &&
          str_is(1, "ChoralRoot"));
    check("strings: the devices \"ChoralRoot Out\" / \"ChoralRoot In\"", str_is(3, "ChoralRoot Out") &&
          str_is(4, "ChoralRoot In"));
    check("strings: the channel names 5..10, none after", str_is(5, "Master L") && str_is(6, "Master R") &&
          str_is(7, "Chord L") && str_is(8, "Chord R") && str_is(9, "Bass L") && str_is(10, "Bass R") &&
          !get_desc(0x030B, &d, &l));
    check("the In terminal names its channels from string 5", ({
        uint32_t o, found = 0;
        for (o = 0; o < ua_cfg_len; o += ua_cfg[o])
            if (ua_cfg[o + 1] == 0x24 && ua_cfg[o + 2] == 2 && ua_cfg[o] == 12 && le16(ua_cfg + o + 4) == 0x0713)
                found = ua_cfg[o + 7] == 6 && ua_cfg[o + 10] == 5;
        found; }));
}

/* ------------------------------------------------------------------ routing --- */
static int16_t stage[32 * UA_CAP_CHANNELS];

static void pcm16(uint8_t *p, uint32_t frames, int16_t l, int16_t r)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        p[4 * i] = (uint8_t)l;
        p[4 * i + 1] = (uint8_t)((uint16_t)l >> 8);
        p[4 * i + 2] = (uint8_t)r;
        p[4 * i + 3] = (uint8_t)((uint16_t)r >> 8);
    }
}

static void test_routing(void)
{
    uint8_t pk[UA_PLAY_PACKET], cp[UA_PACKET];
    int32_t out[64];
    uint32_t i, ch, ok = 1, n;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = ua.cap_alt = 1;
    pcm16(pk, 32, 2000, -2000);
    for (i = 0; i < 16; i++)                               /* 512 frames: playback primed */
        ua_receive(pk, 128);
    for (i = 0; i < 32; i++) {
        out[2 * i] = 1000;
        out[2 * i + 1] = -1000;
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
            stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(100 * (ch + 1) * (ch & 1 ? -1 : 1));
        stage[i * UA_CAP_CHANNELS] = 1000;                 /* the master pair as tapped: before the playback */
        stage[i * UA_CAP_CHANNELS + 1] = -1000;
    }
    ua_audio(out, stage, 32, 4096);
    check("playback: the host's audio x MASTER added to the output", out[0] == 3000 && out[1] == -3000 &&
          out[62] == 3000 && out[63] == -3000);
    for (i = 0; i < 32 * UA_CAP_CHANNELS; i++)
        ok &= ua.cap[i] == stage[i];
    check("capture: the six staged channels into the ring as they are (no loopback)", ok);
    for (i = 0; i < 32; i++)
        out[2 * i] = out[2 * i + 1] = 0;
    ua_audio(out, stage, 32, 2048);
    check("playback follows MASTER (half: half the level)", out[0] == 1000 && out[1] == -1000);
    n = ua.cw;
    for (i = 0; i < 32; i++)
        out[2 * i] = out[2 * i + 1] = 0;
    ua_audio(out, 0, 32, 4096);
    check("a block not staged (the recording started during it): no capture, the playback still", ua.cw == n &&
          out[0] == 2000 && out[1] == -2000);
    pcm16(pk, 32, 32767, -32768);
    for (i = 0; i < 4; i++)
        ua_receive(pk, 128);
    for (n = 0; n < 15; n++) {                             /* through the 2000s to the loud ones */
        for (i = 0; i < 32; i++) {
            out[2 * i] = 30000;
            out[2 * i + 1] = -30000;
        }
        ua_audio(out, stage, 32, 4096);
    }
    check("playback + output saturate at 16 bits", out[0] == 32767 && out[1] == -32768);
    /* PCM24: the top two bytes */
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = 2;
    for (i = 0; i < 45; i++) {
        pk[6 * i] = 0x55;                                  /* the dropped low byte */
        pk[6 * i + 1] = 0x34;
        pk[6 * i + 2] = 0x12;
        pk[6 * i + 3] = 0xAA;
        pk[6 * i + 4] = 0xCD;
        pk[6 * i + 5] = 0xAB;
    }
    ua_receive(pk, 45 * 6);
    check("PCM24 playback: the top 16 bits, little-endian", ua.play[0] == 0x1234 && ua.play[1] == (int16_t)0xABCD &&
          ua.pw == 45);
    n = ua.bad_packets;
    ua_receive(pk, 46 * 6);
    ua_receive(pk, 45 * 6 - 1);
    check("a packet over 45 frames or not whole frames: dropped, counted", ua.bad_packets == n + 2 && ua.pw == 45);
    ua.play_alt = 0;
    ua_receive(pk, 6);
    check("alternate 0: a packet is ignored", ua.pw == 45);
    /* packing */
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    n = ua_transmit(cp);
    for (ok = 1, i = 0; i < n; i++)
        ok &= cp[i] == 0;
    check("capture packets: silence until the ring holds its target", n == 44u * 12u && ok);
    for (i = 0; i < 32; i++)
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
            stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(i * 16 + ch - 300);
    for (i = 0; i < UA_CAP_TARGET / 32; i++) {
        int32_t o[64] = {0};
        ua_audio(o, stage, 32, 4096);
    }
    n = ua_transmit(cp);
    for (ok = 1, i = 0; i < n / 12u; i++)
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++) {
            int16_t v = (int16_t)(cp[12 * i + 2 * ch] | cp[12 * i + 2 * ch + 1] << 8);
            ok &= v == (int16_t)((i % 32) * 16 + ch - 300);
        }
    check("capture packets: PCM16 LE, frames of master L R, CHORD L R, BASS L R", ok && (n == 44u * 12u ||
          n == 45u * 12u));
}

/* ------------------------------------------------------------------ drift --- */
static uint32_t rng = 12345;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

struct sim {
    double fs;                                   /* the I2S rate */
    uint32_t ms, start_ms;                       /* the run, and the end of the start (no glitch counted before) */
    int stop_cap_ms, stop_play_ms;               /* the host stops reading / sending for 50 ms from here (-1: never) */
    /* results */
    uint32_t cap_frames, cap_pkts, cap_bad_order, play_frames, play_bad_order, cap_glitch, play_glitch;
    double fb_sum;
    uint32_t fb_n;
    uint32_t cap_lo, cap_hi, play_lo, play_hi;   /* the rings' fill after the start: capture when a packet goes,
                                                  * playback when a block takes its frames */
};

static void simulate(struct sim *s)
{
    double t_us = 0, next_render = 0, half_us = 128e6 / s->fs, fb_acc = 0;
    uint32_t ms, blk_done = 4, blk_t[4], i, ch;
    uint16_t cap_seq = 0, cap_expect = 0, play_seq = 0, play_expect = 0;
    int cap_started = 0, play_started = 0;
    uint32_t base_cu = 0, base_co = 0, base_pu = 0, base_po = 0;
    uint8_t pk[UA_PLAY_PACKET], cp[UA_PACKET];
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = 1;
    ua.cap_alt = 1;
    s->cap_lo = s->play_lo = 0xFFFFFFFFu;
    for (ms = 0; ms < s->ms; ms++) {
        double frame_end = (ms + 1) * 1000.0;
        while (t_us < frame_end) {                         /* the audio ISR's blocks in this ms */
            if (blk_done == 4 && t_us >= next_render) {    /* a half starts: its 4 blocks within ~2.5 ms */
                uint32_t span = 300u + rnd(2200u);
                for (i = 0; i < 4; i++)
                    blk_t[i] = (uint32_t)(next_render + span * (i + 1) / 4);
                blk_done = 0;
                next_render += half_us;
            }
            if (blk_done < 4 && t_us >= blk_t[blk_done]) {
                int32_t out[64];
                for (i = 0; i < 32; i++) {
                    for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                        stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(ch ? -(int)ch : (int16_t)cap_seq);
                    cap_seq++;
                    out[2 * i] = out[2 * i + 1] = 0;
                }
                if (ms > s->start_ms) {
                    uint32_t f = ua.pw - ua.pr;
                    s->play_lo = f < s->play_lo ? f : s->play_lo;
                    s->play_hi = f > s->play_hi ? f : s->play_hi;
                }
                ua_audio(out, stage, 32, 4096);
                for (i = 0; i < 32; i++) {                 /* the host's counter, frame by frame, once playing */
                    uint16_t v = (uint16_t)out[2 * i];
                    if (out[2 * i] == 0 && out[2 * i + 1] == 0)
                        continue;                          /* not primed (or re-priming) */
                    if (play_started && v != play_expect && ms > s->start_ms)
                        s->play_bad_order++;
                    play_started = 1;
                    play_expect = (uint16_t)(v + 1);
                    s->play_frames++;
                }
                blk_done++;
            }
            t_us += 20;
        }
        /* the USB frame: SOF, a capture packet taken, a playback packet given */
        ua_sof();
        if (!(s->stop_cap_ms >= 0 && ms >= (uint32_t)s->stop_cap_ms && ms < (uint32_t)s->stop_cap_ms + 50u)) {
            uint32_t n;
            if (ms > s->start_ms) {
                uint32_t f = ua.cw - ua.cr;
                s->cap_lo = f < s->cap_lo ? f : s->cap_lo;
                s->cap_hi = f > s->cap_hi ? f : s->cap_hi;
            }
            n = ua_transmit(cp) / (2u * UA_CAP_CHANNELS);
            s->cap_pkts++;
            for (i = 0; i < n; i++) {
                int16_t v = (int16_t)(cp[12 * i] | cp[12 * i + 1] << 8), c5 = (int16_t)(cp[12 * i + 10] | cp[12 * i + 11] << 8);
                if (c5 == 0)
                    continue;                              /* silence: not primed (or re-priming) */
                if (cap_started && (uint16_t)v != cap_expect && ms > s->start_ms)
                    s->cap_bad_order++;
                cap_started = 1;
                cap_expect = (uint16_t)(v + 1);
                s->cap_frames++;
            }
        }
        if (!(s->stop_play_ms >= 0 && ms >= (uint32_t)s->stop_play_ms && ms < (uint32_t)s->stop_play_ms + 50u)) {
            uint32_t fb = ua_feedback(), n;
            fb_acc += fb / 16384.0;                        /* the host follows the feedback */
            n = (uint32_t)fb_acc;
            fb_acc -= n;
            for (i = 0; i < n; i++, play_seq++) {
                pk[4 * i] = (uint8_t)play_seq;
                pk[4 * i + 1] = (uint8_t)(play_seq >> 8);
                pk[4 * i + 2] = 1;                         /* (R: never zero, so a frame is never "silence") */
                pk[4 * i + 3] = 0;
            }
            ua_receive(pk, n * 4u);
            if (ms > s->start_ms) {
                s->fb_sum += fb;
                s->fb_n++;
            }
        }
        if (ms == s->start_ms) {
            base_cu = ua.cap_underruns;
            base_co = ua.cap_overruns;
            base_pu = ua.play_underruns;
            base_po = ua.play_overruns;
        }
    }
    s->cap_glitch = ua.cap_underruns - base_cu + ua.cap_overruns - base_co;
    s->play_glitch = ua.play_underruns - base_pu + ua.play_overruns - base_po;
}

static void test_drift(void)
{
    struct sim s;
    char t[160];
    double fs[2] = {44117.647, 44070.0};
    uint32_t k;
    for (k = 0; k < 2; k++) {
        memset(&s, 0, sizeof s);
        s.fs = fs[k];
        s.ms = 20000;
        s.start_ms = 2000;
        s.stop_cap_ms = s.stop_play_ms = -1;
        simulate(&s);
        snprintf(t, sizeof t, "I2S %.1f Hz, 20 s, renders at random times: no glitch after 2 s (In %u, Out %u)", fs[k],
                 s.cap_glitch, s.play_glitch);
        check(t, s.cap_glitch == 0 && s.play_glitch == 0);
        snprintf(t, sizeof t, "  every frame once and in order, both ways (In %u frames, Out %u frames)", s.cap_frames,
                 s.play_frames);
        check(t, !s.cap_bad_order && !s.play_bad_order && s.cap_frames > 19u * 44000u && s.play_frames > 19u * 44000u);
        snprintf(t, sizeof t, "  fill: In %u..%u of %u (a packet: 45), Out %u..%u of %u (a block: 32)", s.cap_lo,
                 s.cap_hi, UA_CAP_RING, s.play_lo, s.play_hi, UA_RING);
        check(t, s.cap_lo >= 45u + 32u && s.cap_hi + 32u <= UA_CAP_RING && s.play_lo >= 64u && s.play_hi + 45u <= UA_RING);
        snprintf(t, sizeof t, "  capture %.3f frames a packet, the host's playback %.3f (I2S %.3f / ms)",
                 (double)s.cap_frames / s.cap_pkts, s.fb_sum / s.fb_n / 16384.0, fs[k] / 1000.0);
        check(t, (double)s.cap_frames / s.cap_pkts > fs[k] / 1000.0 - 0.2 &&
                 (double)s.cap_frames / s.cap_pkts < fs[k] / 1000.0 + 0.2 &&
                 s.fb_sum / s.fb_n / 16384.0 > fs[k] / 1000.0 - 0.01 && s.fb_sum / s.fb_n / 16384.0 < fs[k] / 1000.0 + 0.01);
    }
    memset(&s, 0, sizeof s);
    s.fs = 44117.647;
    s.ms = 8000;
    s.start_ms = 6000;                                     /* (glitches counted from 6 s: after the recovery) */
    s.stop_cap_ms = 3000;
    s.stop_play_ms = 4000;
    simulate(&s);
    check("the host stops reading In for 50 ms: overrun counted, re-primed", ua.cap_overruns >= 1);
    check("the host stops sending Out for 50 ms: underrun counted, re-primed", ua.play_underruns >= 1);
    check("  both run again in order, no glitch after", s.cap_glitch == 0 && s.play_glitch == 0 && !s.cap_bad_order &&
          !s.play_bad_order);
}

/* ------------------------------------------------------------------ bench --- */
#ifdef __APPLE__
static uint64_t instr(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
#endif
static volatile int32_t sink;
/* a half's (128 frames, 2.9 ms) USB audio work on the host, both streams running: per block (x4) fx.c's ua_stage
 * clear and the part captures (two parts x 32 frames, as mix_part writes them), the master tap (choralroot.c), the
 * ring copy and the playback mix-in (ua_audio); per USB frame (x2.9) the packet in (ua_receive), the packet out
 * (ua_transmit), the SOF servo and the feedback. Device us = host instructions / 259 (emu.c's ratio) */
static void bench(void)
{
#ifdef __APPLE__
    enum { HALVES = 20000 };
    uint8_t pk[UA_PLAY_PACKET], cp[UA_PACKET];
    int32_t out[64], part[64];
    uint64_t t0, t1, t2;
    uint32_t h, b, i, f = 0;
    double per_half_isr, per_half_t5;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = ua.cap_alt = 1;
    memset(pk, 0x11, sizeof pk);
    for (i = 0; i < 64; i++)
        part[i] = (int32_t)(i * 997u) - 30000;
    t0 = instr();
    for (h = 0; h < HALVES; h++) {
        for (b = 0; b < 4; b++) {
            for (i = 0; i < 32 * UA_CAP_CHANNELS; i++)    /* fx.c mix_block: the stage cleared */
                stage[i] = 0;
            for (i = 0; i < 32; i++) {                     /* fx.c mix_part: two parts' stereo, halved, saturated */
                stage[i * 6 + 2] = (int16_t)(part[i] >> 1 > 32767 ? 32767 : part[i] >> 1 < -32768 ? -32768 : part[i] >> 1);
                stage[i * 6 + 3] = (int16_t)(part[i + 32] >> 1 > 32767 ? 32767 : part[i + 32] >> 1 < -32768 ? -32768 : part[i + 32] >> 1);
                stage[i * 6 + 4] = (int16_t)(part[i] >> 1 > 32767 ? 32767 : part[i] >> 1 < -32768 ? -32768 : part[i] >> 1);
                stage[i * 6 + 5] = (int16_t)(part[i + 32] >> 1 > 32767 ? 32767 : part[i + 32] >> 1 < -32768 ? -32768 : part[i + 32] >> 1);
            }
            for (i = 0; i < 64; i++)
                out[i] = part[i];
            for (i = 0; i < 32; i++) {                     /* choralroot.c: the master tap */
                stage[i * 6] = (int16_t)(out[2 * i] > 32767 ? 32767 : out[2 * i] < -32768 ? -32768 : out[2 * i]);
                stage[i * 6 + 1] = (int16_t)(out[2 * i + 1] > 32767 ? 32767 : out[2 * i + 1] < -32768 ? -32768 : out[2 * i + 1]);
            }
            if (ua.pw - ua.pr < 600u)                      /* (keep the playback ring fed and primed) */
                for (i = 0; i < 16; i++)
                    ua_receive(pk, 180);
            if (ua.cw - ua.cr > 300u)
                ua.cr = ua.cw - 256u;
            ua_audio(out, stage, 32, 3000);
            sink += out[5];
        }
    }
    t1 = instr();
    for (h = 0; h < HALVES; h++) {
        uint32_t frames = (h % 10u) < 9u ? 3u : 2u;        /* 2.9 USB frames a half */
        for (f = 0; f < frames; f++) {
            ua_sof();
            ua_receive(pk, 176);
            if (ua.pw - ua.pr > 900u)
                ua.pr = ua.pw - 512u;
            if (ua.cw - ua.cr < 300u)
                ua.cw += 128u;
            sink += (int32_t)ua_transmit(cp) + (int32_t)ua_feedback();
        }
    }
    t2 = instr();
    if (!t0) {
        printf("bench: no instruction counter\n");
        return;
    }
    per_half_isr = (double)(t1 - t0) / HALVES;
    per_half_t5 = (double)(t2 - t1) / HALVES;
    printf("usb audio: host instructions per 128-frame half: audio ISR %.0f, TIMER5 packets %.0f -> device estimate "
           "%.0f us + %.0f us = %.0f us of 2902 (%.1f %%), SIE register access not included\n",
           per_half_isr, per_half_t5, per_half_isr / 259.0, per_half_t5 / 259.0, (per_half_isr + per_half_t5) / 259.0,
           100.0 * (per_half_isr + per_half_t5) / 259.0 / 2902.0);
#else
    printf("bench: macOS only (proc_pid_rusage)\n");
#endif
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--bench")) {
        bench();
        return 0;
    }
    test_descriptors();
    test_routing();
    test_drift();
    printf("cr_usbaudio: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
