/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's USB audio recording (Melodee's, without its playback; docs/USB-AUDIO.md):
 * firmware/src/usb.c with FELUCCA_UAC=1 and FELUCCA_CDC=1, its usb_audio.c, usb_audio_stream.c and
 * usb_audio_desc.h. The SIE is never touched (no usb_poll, ua_hw_poll, SET_INTERFACE); what runs is what a host
 * sees and what the two ISRs exchange:
 *   descriptors  both presentations (USB Record on; Off = the console) as a host parses them: lengths, interface
 *                numbering, IADs, the AC header's stream list, terminals, the format, the endpoint size against the
 *                packets the code produces, no playback (no OUT or feedback endpoint), the strings (product, the
 *                device, channel names)
 *   routing      the capture frames (master, CHORD, BASS) into the ring as staged; none with the stream closed
 *   packing      capture packets: PCM16 little-endian, six channels in order, silence until the ring is primed
 *   drift        20 s of the I2S clock (44,117.6 Hz, or a slow one) against the host's 1 kHz frames: renders of 128
 *                frames at random times inside their half: no underrun or overrun after the start, every frame once
 *                and in order, the rates matched
 *   recovery     the host stops reading: the overrun is counted, the stream re-primes, and runs in order again
 *   --bench      host instructions for a half's USB work (macOS) and the device estimate (perf.sh)
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

static void present(uint8_t off)                          /* as usb_start: the console with USB Record Off */
{
    ua_off = off;
    usb_cdc_on = (uint8_t)((ua_off & UA_OFF_IN) != 0);
    ua_reset();
    ua_cfg_build();
}

/* one presentation parsed as a host does; cap: the recording is expected */
static void parse(const char *tag, uint8_t off, int cap, int cdc)
{
    const uint8_t *dev, *c, *d;
    uint16_t dl, n;
    uint32_t off_b, nif = 0, i;
    int lens = 1, eps = 1, contig = 1, iads = 1, aclist = 1, sizes = 1, noplay = 1, fmt_cap = !cap, fmt_other = 0;
    int cur_if = -1, cur_neps = 0, got = 0, cur_sub = 0, cur_alt = 0, n_iad = 0, has_cdc = 0, n_midi_ep = 0;
    uint8_t seen[16] = {0}, sub_of[16] = {0};
    uint8_t lists[4][8], nlists = 0, nlist[4] = {0};
    char s[128];
    present(off);
    get_desc(0x0100, &dev, &dl);
    get_desc(0x0200, &c, &n);
    snprintf(s, sizeof s, "%s: device: 18 bytes, misc / IAD class, bcdDevice %s", tag, cdc ? "3.21" : "3.22");
    check(s, dl == 18 && dev[0] == 18 && dev[4] == 0xEF && dev[5] == 2 && dev[6] == 1 && dev[13] == 3 &&
             dev[12] == (cdc ? 0x21 : 0x22) && dev[14] == 1 && dev[15] == 2);
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
            if (cur_sub == 2) {                            /* an audio streaming endpoint: the capture's only */
                uint32_t mp = le16(d + 4);
                if (d[2] != 0x82)
                    noplay = 0;                            /* (an OUT or a feedback endpoint: playback) */
                else if (d[0] != 9 || d[3] != 0x05 || d[6] != 1 || mp > 1023u || mp < UA_PACKET || cur_alt != 1)
                    sizes = 0;                             /* isochronous async, ua_transmit's largest fits */
            }
        } else if (d[1] == 0x24 && cur_sub == 1 && d[2] == 1 && nlists < 4) {   /* an AC header: its stream list */
            nlist[nlists] = d[7];
            for (i = 0; i < d[7] && i < 8; i++)
                lists[nlists][i] = d[8 + i];
            nlists++;
        } else if (d[1] == 0x24 && cur_sub == 2 && d[2] == 2) {                /* type I format */
            uint32_t rate = d[8] | d[9] << 8 | (uint32_t)d[10] << 16;
            if (d[4] == 6 && d[5] == 2 && d[6] == 16 && rate == 44100)
                fmt_cap = 1;
            else
                fmt_other = 1;
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
    check(s, iads && aclist && n_iad == 1 + cap + cdc);
    snprintf(s, sizeof s, "%s: MIDI on EP1 OUT / IN%s", tag, cdc ? ", the console (CDC) present" : ", no console");
    check(s, n_midi_ep == 2 && has_cdc == cdc);
    snprintf(s, sizeof s, "%s: the recording %s (IF %d)", tag, cap ? "on" : "off", ua_if_cap == UA_NO_IF ? -1 :
             ua_if_cap);
    check(s, (ua_if_cap != UA_NO_IF) == cap && (!cap || sub_of[ua_if_cap] == 2));
    snprintf(s, sizeof s, "%s: format (6 x PCM16, 44.1 kHz) and packet size; no playback (OUT, feedback, format)", tag);
    check(s, fmt_cap && !fmt_other && sizes && noplay);
}

static void test_descriptors(void)
{
    const uint8_t *d;
    uint16_t l;
    parse("USB Record on", 0, 1, 0);
    check("USB Record on: 208 bytes, 4 interfaces (MIDI 0-1, the recording 2-3)",
          ua_cfg_len == 208 && ua_if_cap == 3 && ua_nif == 4);
    parse("USB Record off (the console)", UA_OFF_IN, 0, 1);
    present(0);
    check("strings: the product \"ChoralRoot FM-1\" (the MIDI port), the maker", str_is(2, "ChoralRoot FM-1") &&
          str_is(1, "ChoralRoot"));
    check("strings: the device \"ChoralRoot In\" (3), and its IAD and control interface name it", str_is(3,
          "ChoralRoot In") && ({
        uint32_t o, iad = 0, ac = 0;
        for (o = 0; o < ua_cfg_len; o += ua_cfg[o]) {
            if (ua_cfg[o + 1] == 0x0B && ua_cfg[o + 2] == 2)
                iad = ua_cfg[o + 7] == 3;
            if (ua_cfg[o + 1] == 4 && ua_cfg[o + 2] == 2)
                ac = ua_cfg[o + 8] == 3;
        }
        iad && ac; }));
    check("strings: the channel names 4..9, none after", str_is(4, "Master L") && str_is(5, "Master R") &&
          str_is(6, "Chord L") && str_is(7, "Chord R") && str_is(8, "Bass L") && str_is(9, "Bass R") &&
          !get_desc(0x030A, &d, &l));
    check("the In terminal names its channels from string 4", ({
        uint32_t o, found = 0;
        for (o = 0; o < ua_cfg_len; o += ua_cfg[o])
            if (ua_cfg[o + 1] == 0x24 && ua_cfg[o + 2] == 2 && ua_cfg[o] == 12 && le16(ua_cfg + o + 4) == 0x0713)
                found = ua_cfg[o + 7] == 6 && ua_cfg[o + 10] == 4;
        found; }));
}

/* ------------------------------------------------------------------ routing --- */
static int16_t stage[32 * UA_CAP_CHANNELS];

static void test_routing(void)
{
    uint8_t cp[UA_PACKET];
    uint32_t i, ch, ok = 1, n;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    for (i = 0; i < 32; i++)
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
            stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(100 * (ch + 1) * (ch & 1 ? -1 : 1));
    ua_audio(stage, 32);
    for (i = 0; i < 32 * UA_CAP_CHANNELS; i++)
        ok &= ua.cap[i] == stage[i];
    check("capture: the six staged channels into the ring as they are", ok && ua.cw == 32);
    ua.cap_alt = 0;
    ua_audio(stage, 32);
    check("the host closed the stream during the block (alternate 0): nothing captured", ua.cw == 32);
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
    for (i = 0; i < UA_CAP_TARGET / 32; i++)
        ua_audio(stage, 32);
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
    int stop_cap_ms;                             /* the host stops reading for 50 ms from here (-1: never) */
    /* results */
    uint32_t cap_frames, cap_pkts, cap_bad_order, cap_glitch;
    uint32_t cap_lo, cap_hi;                     /* the ring's fill after the start, when a packet goes */
};

static void simulate(struct sim *s)
{
    double t_us = 0, next_render = 0, half_us = 128e6 / s->fs;
    uint32_t ms, blk_done = 4, blk_t[4], i, ch;
    uint16_t cap_seq = 0, cap_expect = 0;
    int cap_started = 0;
    uint32_t base_cu = 0, base_co = 0;
    uint8_t cp[UA_PACKET];
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    s->cap_lo = 0xFFFFFFFFu;
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
                for (i = 0; i < 32; i++) {
                    for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                        stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(ch ? -(int)ch : (int16_t)cap_seq);
                    cap_seq++;
                }
                ua_audio(stage, 32);
                blk_done++;
            }
            t_us += 20;
        }
        /* the USB frame: SOF, a capture packet taken */
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
        if (ms == s->start_ms) {
            base_cu = ua.cap_underruns;
            base_co = ua.cap_overruns;
        }
    }
    s->cap_glitch = ua.cap_underruns - base_cu + ua.cap_overruns - base_co;
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
        s.stop_cap_ms = -1;
        simulate(&s);
        snprintf(t, sizeof t, "I2S %.1f Hz, 20 s, renders at random times: no glitch after 2 s (%u)", fs[k],
                 s.cap_glitch);
        check(t, s.cap_glitch == 0);
        snprintf(t, sizeof t, "  every frame once and in order (%u frames)", s.cap_frames);
        check(t, !s.cap_bad_order && s.cap_frames > 19u * 44000u);
        snprintf(t, sizeof t, "  fill: %u..%u of %u (a packet: 45, a block: 32)", s.cap_lo, s.cap_hi, UA_CAP_RING);
        check(t, s.cap_lo >= 45u + 32u && s.cap_hi + 32u <= UA_CAP_RING);
        snprintf(t, sizeof t, "  capture %.3f frames a packet (I2S %.3f / ms)", (double)s.cap_frames / s.cap_pkts,
                 fs[k] / 1000.0);
        check(t, (double)s.cap_frames / s.cap_pkts > fs[k] / 1000.0 - 0.2 &&
                 (double)s.cap_frames / s.cap_pkts < fs[k] / 1000.0 + 0.2);
    }
    memset(&s, 0, sizeof s);
    s.fs = 44117.647;
    s.ms = 8000;
    s.start_ms = 6000;                                     /* (glitches counted from 6 s: after the recovery) */
    s.stop_cap_ms = 3000;
    simulate(&s);
    check("the host stops reading for 50 ms: overrun counted, re-primed", ua.cap_overruns >= 1);
    check("  it runs again in order, no glitch after", s.cap_glitch == 0 && !s.cap_bad_order);
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
/* a half's (128 frames, 2.9 ms) USB recording work on the host: per block (x4) fx.c's ua_stage clear and the part
 * captures (two parts x 32 frames, as mix_part writes them), the master tap (choralroot.c) and the ring copy
 * (ua_audio); per USB frame (x2.9) the packet out (ua_transmit) and the SOF servo. Device us = host instructions /
 * 259 (emu.c's ratio) */
static void bench(void)
{
#ifdef __APPLE__
    enum { HALVES = 20000 };
    uint8_t cp[UA_PACKET];
    int32_t out[64], part[64];
    uint64_t t0, t1, t2;
    uint32_t h, b, i, f = 0;
    double per_half_isr, per_half_t5;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
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
            if (ua.cw - ua.cr > 300u)
                ua.cr = ua.cw - 256u;
            ua_audio(stage, 32);
            sink += ua.cap[5];
        }
    }
    t1 = instr();
    for (h = 0; h < HALVES; h++) {
        uint32_t frames = (h % 10u) < 9u ? 3u : 2u;        /* 2.9 USB frames a half */
        for (f = 0; f < frames; f++) {
            ua_sof();
            if (ua.cw - ua.cr < 300u)
                ua.cw += 128u;
            sink += (int32_t)ua_transmit(cp);
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

/* ------------------------------------------------------------ handoff --- */
/* The endpoint hand-off as usb_audio.c does it (ua_tx_fill, then ua_tx_prepare: one packet queued, the next packed
 * ahead in the other slot), ua_service at 4 kHz, the host's IN token once per ms reading the queued slot at the token
 * (the DMA reads the buffer then, not when it is armed), producer: 128-frame halves at the I2S rate. Over 10 s every
 * frame the host gets is the next one produced: none twice, none skipped once primed. */
static void test_handoff(void)
{
    uint8_t slot = 0, pktrdy = 0, q_slot = 0;
    uint32_t bytes = 0, q_bytes = 0, tick, i, ch, n, dup = 0, skip = 0, got = 0, pk = 0, maxrep = 0, rep = 0;
    uint16_t seq = 0, expect = 0, last = 0;
    int started = 0;
    double next_half = 0, half_us = 128e6 / 44117.647;
    static uint8_t buf[2][UA_PACKET];
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    for (tick = 0; tick < 40000u; tick++) {                /* 250 us ticks, 10 s */
        double t = tick * 250.0;
        while (next_half <= t) {                           /* a half: 4 blocks of 32 */
            for (n = 0; n < 4; n++) {
                for (i = 0; i < 32; i++, seq++)
                    for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                        stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(ch ? -(int)ch : (int16_t)seq);
                ua_audio(stage, 32);
            }
            next_half += half_us;
        }
        if ((tick & 3u) == 0) {                            /* the host: SOF, then the IN token */
            ua_sof();
            if (pktrdy) {
                const uint8_t *p = buf[q_slot];
                pk++;
                for (i = 0; i < q_bytes / (2u * UA_CAP_CHANNELS); i++) {
                    uint16_t v = (uint16_t)(p[12 * i] | p[12 * i + 1] << 8);
                    if ((int16_t)(p[12 * i + 10] | p[12 * i + 11] << 8) == 0)
                        continue;                          /* silence: priming */
                    if (started && v == last)
                        rep++;
                    if (started && (int16_t)(v - expect) < 0)
                        dup++;
                    else if (started && v != expect)
                        skip++;
                    started = 1;
                    last = v;
                    expect = (uint16_t)(v + 1);
                    got++;
                }
                pktrdy = 0;
            }
        }
        if (!pktrdy) {                                     /* ua_tx_fill */
            if (!bytes)
                bytes = ua_transmit(buf[slot]);
            q_slot = slot;
            q_bytes = bytes;
            pktrdy = 1;
            slot ^= 1u;
            bytes = 0;
        }
        if (!bytes)                                        /* ua_tx_prepare */
            bytes = ua_transmit(buf[slot]);
        if (rep > maxrep)
            maxrep = rep;
    }
    {
        char m[160];
        snprintf(m, sizeof m, "hand-off, 10 s: %u frames in %u packets, none twice (%u), none skipped (%u), "
                 "under %u over %u", got, pk, dup, skip, ua.cap_underruns, ua.cap_overruns);
        check(m, !dup && !skip && !maxrep && got > 9u * 44000u && !ua.cap_underruns && !ua.cap_overruns);
    }
}

/* QuickTime: the host reads one packet every 1.000 ms and does no drift correction; the producer at rate Hz in
 * 128-frame halves; 60 s: after the first second no silence gap over 30 ms, and no resets */
static void test_quicktime(double rate)
{
    static uint8_t buf[UA_PACKET];
    double next_half = 0, half_us = 128e6 / rate;
    uint32_t ms, i, ch, gap = 0, maxgap = 0, frames = 0, n;
    uint16_t seq = 0;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    for (ms = 0; ms < 60000u; ms++) {
        double t = ms * 1000.0;
        while (next_half <= t) {
            for (n = 0; n < 4; n++) {
                for (i = 0; i < 32; i++, seq++)
                    for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                        stage[i * UA_CAP_CHANNELS + ch] = (int16_t)(ch ? -(int)ch : (int16_t)seq);
                ua_audio(stage, 32);
            }
            next_half += half_us;
        }
        ua_sof();
        n = ua_transmit(buf) / (2u * UA_CAP_CHANNELS);
        frames += n;
        for (i = 0; i < n; i++) {
            if ((int16_t)(buf[12 * i + 10] | buf[12 * i + 11] << 8) == 0)
                gap++;
            else
                gap = 0;
            if (ms >= 1000u && gap > maxgap)
                maxgap = gap;
        }
    }
    {
        char m[200];
        snprintf(m, sizeof m, "QuickTime 60 s, producer %.1f Hz: %u frames, longest silence %u, under %u over %u, "
                 "fill %d", rate, frames, maxgap, ua.cap_underruns, ua.cap_overruns, ua.cap_fill_q8 >> 8);
        check(m, maxgap <= 1323u && !ua.cap_underruns && !ua.cap_overruns);
    }
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
    test_handoff();
    test_quicktime(44117.647);
    test_quicktime(44100.0 * 1.001);
    test_quicktime(44100.0 * 0.999);
    test_quicktime(44100.0);
    printf("cr_usbaudio: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
