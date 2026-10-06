/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the USB audio input in src/usb.c (FELUCCA_UAC):
 *   descriptors  the configuration parsed as a host does: lengths, interface and endpoint counts,
 *                class codes, the UAC1 chain (AC header collection, terminals, AS general, type I
 *                format, the isochronous endpoint), the IADs; with and without CDC (-DT_CDC=0/1)
 *   ring         uac_render_start / uac_tap (the audio ISR) against uac_packet (TIMER5): renders of 256
 *                frames at the I2S rate, as late as the load makes them, packets at 1 kHz: sizes 43..46,
 *                44.1 on average, every frame delivered in order, then an underrun (repeats), an
 *                overrun (drops, the ring never overfills) and a restart.
 * The SIE is never touched (uac_service and usb_poll are not called). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef HALF_FRAMES
#error "-DHALF_FRAMES=n (src/core.h; run_tests.sh passes it)"
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC T_CDC
#define FELUCCA_UAC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

/* ------------------------------------------------------------------ descriptors --- */
static void test_descriptors(void)
{
    const uint8_t *c = CFG_DESC;
    uint32_t total = le16(c + 2), n = sizeof CFG_DESC, off, nif = 0, i;
    int lens_ok = 1, eps_ok = 1, ac_ok = 0, it_ok = 0, ot_ok = 0, asg_ok = 0, fmt_ok = 0, iso_ok = 0, csep_ok = 0;
    int alt0_ok = 0, iad_ok = 1, midi_ok = 0, dup_ok = 1, coll_ok = 0;
    uint8_t seen_if[16] = {0}, if_class[16] = {0}, if_sub[16] = {0};
    int cur_if = -1, cur_alt = -1, cur_neps = 0, got_eps = 0, ac_len_left = -1, ac_total = 0, ac_wt = -1;
    uint8_t coll[8], ncoll = 0, iad_n = 0;
    uint32_t ep_seen[32] = {0};
    char name[96];

    check("device descriptor: 18 bytes, type 1, EP0 64", DEV_DESC[0] == 18 && DEV_DESC[1] == 1 && DEV_DESC[7] == 64);
    check("bcdDevice bumped for the audio input (x.1x)", (DEV_DESC[12] & 0xF0u) == 0x10u && DEV_DESC[13] == 3);
    check(T_CDC ? "device class misc / IAD (EF 02 01)" : "device class 0 (per interface)",
          T_CDC ? DEV_DESC[4] == 0xEF && DEV_DESC[5] == 2 && DEV_DESC[6] == 1 : DEV_DESC[4] == 0);
    check("configuration: type 2, wTotalLength = the bytes sent", c[1] == 2 && total == n);

    for (off = 0; off < n;) {
        const uint8_t *d = c + off;
        if (d[0] < 2 || off + d[0] > n) {
            lens_ok = 0;
            break;
        }
        if (ac_len_left > 0 && d[1] == 0x24) {
            ac_total += d[0];
            ac_len_left -= d[0];
        } else if (ac_len_left > 0) {
            ac_len_left = -2;                          /* the CS AC block ended early */
        }
        switch (d[1]) {
        case 0x0B:                                     /* IAD */
            iad_n++;
            if (d[0] != 8 || d[2] + d[3] > 16)
                iad_ok = 0;
            if (iad_n == 1 && !(d[2] == 0 && d[3] == 3 && d[4] == 1))
                iad_ok = 0;                            /* audio: IF 0..2 */
            if (iad_n == 2 && !(d[2] == 3 && d[3] == 2 && d[4] == 2))
                iad_ok = 0;                            /* CDC: IF 3..4 */
            break;
        case 4:
            if (cur_if >= 0 && got_eps != cur_neps)
                eps_ok = 0;
            cur_if = d[2];
            cur_alt = d[3];
            cur_neps = d[4];
            got_eps = 0;
            if (cur_if < 16) {
                if (!seen_if[cur_if])
                    nif++;
                seen_if[cur_if] = 1;
                if_class[cur_if] = d[5];
                if_sub[cur_if] = d[6];
            }
            if (d[5] == 1 && d[6] == 2 && cur_alt == 0 && d[4] == 0)
                alt0_ok = 1;
            break;
        case 5:
            got_eps++;
            if (d[2] == 0x84) {
                iso_ok = d[0] == 9 && d[3] == 0x05 && le16(d + 4) == UA_MAXF * 4u && le16(d + 4) <= 1023u &&
                         d[6] == 1 && cur_alt == 1 && if_sub[cur_if] == 2;
            }
            if ((d[2] == 0x01 || d[2] == 0x81) && d[3] == 2 && if_sub[cur_if] == 3)
                midi_ok++;
            if (ep_seen[(d[2] & 15u) + (d[2] >> 7) * 16u]++)
                dup_ok = 0;                            /* each address once (no alt shares one here) */
            break;
        case 0x24:
            if (if_class[cur_if] == 1 && if_sub[cur_if] == 1) {      /* audio control */
                if (d[2] == 1) {
                    ac_ok = d[0] == 8u + d[7] && le16(d + 3) == 0x0100u;
                    ac_len_left = ac_wt = (int)le16(d + 5);
                    ac_total = 0;
                    ac_len_left -= d[0];
                    ac_total += d[0];
                    ncoll = d[7];
                    for (i = 0; i < ncoll && i < sizeof coll; i++)
                        coll[i] = d[8 + i];
                } else if (d[2] == 2) {
                    it_ok = d[0] == 12 && d[3] == 1 && d[7] == 2 && le16(d + 8) == 3u && le16(d + 4) != 0x0101u;
                } else if (d[2] == 3) {
                    ot_ok = d[0] == 9 && d[3] == 2 && le16(d + 4) == 0x0101u && d[7] == 1;
                }
            } else if (if_class[cur_if] == 1 && if_sub[cur_if] == 2) {   /* audio streaming */
                if (d[2] == 1)
                    asg_ok = d[0] == 7 && d[3] == 2 && le16(d + 5) == 1u;
                else if (d[2] == 2)
                    fmt_ok = d[0] == 11 && d[3] == 1 && d[4] == 2 && d[5] == 2 && d[6] == 16 && d[7] == 1 &&
                             (d[8] | d[9] << 8 | d[10] << 16) == 44100;
            }
            break;
        case 0x25:
            if (d[2] == 1 && if_sub[cur_if] == 2)
                csep_ok = d[0] == 7 && (d[3] & 1u);
            break;
        }
        off += d[0];
    }
    if (cur_if >= 0 && got_eps != cur_neps)
        eps_ok = 0;
    coll_ok = ncoll == 2;
    for (i = 0; i < ncoll && i < sizeof coll; i++)
        if (coll[i] >= 16 || if_class[coll[i]] != 1 || (if_sub[coll[i]] != 2 && if_sub[coll[i]] != 3))
            coll_ok = 0;

    check("descriptor lengths add up to wTotalLength", lens_ok && off == n);
    check("bNumInterfaces = the interfaces present", c[4] == nif && nif == (T_CDC ? 5u : 3u));
    check("each interface setting has bNumEndpoints endpoints", eps_ok);
    check("endpoint addresses unique", dup_ok);
    check("AC header: UAC 1.00, length 8 + collection", ac_ok);
    check("AC header wTotalLength = the class-specific AC descriptors", ac_len_left == 0 && ac_total == ac_wt);
    check("AC collection: the MIDI and audio streaming interfaces", coll_ok);
    check("input terminal 1: 2 ch, L R, not USB streaming", it_ok);
    check("output terminal 2: USB streaming, source 1", ot_ok);
    check("AS alt 0 has no endpoint", alt0_ok);
    check("AS general: terminal 2, PCM", asg_ok);
    check("type I format: 2 ch, 2-byte subframe, 16 bit, 44100 Hz only", fmt_ok);
    check("EP 0x84: isochronous async, 184 B (46 frames), every frame, alt 1", iso_ok);
    check("CS endpoint: sampling frequency control", csep_ok);
    check("MIDI bulk endpoints 0x01 / 0x81 unchanged", midi_ok == 2);
    snprintf(name, sizeof name, "IADs: %s", T_CDC ? "audio IF 0-2, CDC IF 3-4" : "none");
    check(name, iad_ok && iad_n == (T_CDC ? 2 : 0));
}

/* --------------------------------------------------------------------- the ring --- */
/* time in ns. The producer renders HALF_FRAMES frames at FS_DEV, a render taking `rend` ns in
 * HALF_FRAMES / 32 blocks of 32 (the samples count up: L = n, R = ~n, so the order can be checked); the consumer
 * takes one packet per ms. */
#define FS_DEV 44117.6
static uint32_t prod_n, cons_n, cons_bad, cons_rep, cons_zero, sizes[64], npk;
static uint32_t last_frame;
static int32_t blk[64];

static void render_block(void)
{
    uint32_t i;
    for (i = 0; i < 32u; i++, prod_n++) {
        blk[2 * i] = (int16_t)prod_n;
        blk[2 * i + 1] = (int16_t)~prod_n;
    }
    uac_tap(blk, 32);
}

static void take_packet(void)
{
    uint32_t d[UA_MAXF + 2], n = uac_packet(d), i;
    sizes[n]++;
    npk++;
    for (i = 0; i < n; i++) {
        uint16_t l = (uint16_t)d[i], r = (uint16_t)(d[i] >> 16);
        if (d[i] == 0 && !uac.go) {
            cons_zero++;
            continue;
        }
        if ((uint16_t)~l != r) {
            if (d[i] == 0) {                           /* primed silence ahead of the first render */
                cons_zero++;
                continue;
            }
            cons_bad++;
            continue;
        }
        if (cons_n && l == (uint16_t)last_frame)
            cons_rep++;                                 /* a repeated frame (underrun) */
        else if (cons_n && l != (uint16_t)(last_frame + 1u))
            cons_bad++;
        last_frame = l;
        cons_n++;
    }
}

/* run for `ms`; renders take `rend_ns` (or vary with the load when 0); consumer / producer can be paused */
static uint64_t now_ns, next_half, next_pkt;
static uint32_t blocks_left;
static uint64_t next_block, rend_len;
static uint32_t lcg = 12345;
/* render times: 7 % .. 85 % of a half (the overload shedding starts above 85 %) */
#define REND_MIN ((uint64_t)(HALF_FRAMES * 1e9 / FS_DEV * 0.07))
#define REND_MAX ((uint64_t)(HALF_FRAMES * 1e9 / FS_DEV * 0.85))

static void run(uint32_t ms, uint64_t rend_ns, int produce, int consume)
{
    uint64_t end = now_ns + (uint64_t)ms * 1000000u;
    while (now_ns < end) {
        uint64_t t = end;
        if (next_half < t)
            t = next_half;
        if (blocks_left && next_block < t)
            t = next_block;
        if (next_pkt < t)
            t = next_pkt;
        now_ns = t;
        if (now_ns >= end)
            break;
        if (now_ns == next_half) {
            next_half += (uint64_t)((double)HALF_FRAMES / FS_DEV * 1e9);
            if (produce) {
                lcg = lcg * 1103515245u + 12345u;
                rend_len = rend_ns ? rend_ns : REND_MIN + (lcg >> 8) % (REND_MAX - REND_MIN);
                uac_render_start();
                blocks_left = HALF_FRAMES / 32u;
                next_block = now_ns + rend_len / blocks_left;
            }
        } else if (blocks_left && now_ns == next_block) {
            render_block();
            blocks_left--;
            next_block += rend_len / (HALF_FRAMES / 32u);
        } else if (now_ns == next_pkt) {
            lcg = lcg * 1103515245u + 12345u;
            next_pkt = (now_ns / 1000000u + 1u) * 1000000u + (lcg >> 8) % 500000u;   /* in the next frame */
            if (consume)
                take_packet();
        }
    }
}

static void test_ring(void)
{
    uint32_t i, sum, base_n, base_p;
    char s[120];
    usb.config = 1;
    memset(&uac, 0, sizeof uac);
    uac_stream(1);
    next_half = 300000;
    next_pkt = 1000000;
    run(3, 0, 1, 1);                                    /* alt 1 set: no IN token yet */
    check("not fed before the host reads (no overruns while it waits)", uac.overruns == 0 && ua_w == 0);
    uac.flowing = 1;                                    /* (uac_service: the first packet went) */
    run(2000, 0, 1, 1);
    memset(sizes, 0, sizeof sizes);
    npk = 0;
    base_n = cons_n;
    base_p = uac.underruns + uac.overruns;
    run(10000, 0, 1, 1);
    for (i = sum = 0; i < 64u; i++)
        sum += sizes[i] * i;
    snprintf(s, sizeof s, "10 s, random render times: sizes 43..46 (44:%u 45:%u 46:%u 43:%u)", sizes[44], sizes[45],
             sizes[46], sizes[43]);
    check(s, sizes[44] + sizes[45] + sizes[46] + sizes[43] == npk);
    snprintf(s, sizeof s, "  mean packet %.4f frames (I2S %.1f Hz)", (double)sum / npk, FS_DEV);
    check(s, (double)sum / npk > 44.05 && (double)sum / npk < 44.2);
    check("  every frame once, in order; no underrun / overrun", cons_bad == 0 && cons_rep == 0 &&
          uac.underruns + uac.overruns == base_p && cons_n - base_n == sum);
    snprintf(s, sizeof s, "  fill at render start stays %u..%u (band %u..%u)", uac.fill_lo, uac.fill_hi, UA_LO, UA_HI);
    check(s, uac.fill_lo >= 46u && uac.fill_hi + HALF_FRAMES <= UA_N);
    memset(sizes, 0, sizeof sizes);
    npk = 0;
    for (i = 0; i < 1000u; i++) {                      /* 1000 frames worst case: renders of 85 % */
        run(1, REND_MAX, 1, 1);
    }
    for (i = sum = 0; i < 64u; i++)
        sum += sizes[i] * i;
    snprintf(s, sizeof s, "1000 packets, %.1f ms renders: mean %.3f, no glitch", REND_MAX / 1e6, (double)sum / npk);
    check(s, (double)sum / npk > 44.0 && (double)sum / npk < 44.25 && cons_bad == 0 && cons_rep == 0 &&
          uac.underruns + uac.overruns == base_p);

    base_p = uac.underruns;
    run(30, 0, 0, 1);                                   /* the render stops (a crash would; a stuck ISR) */
    check("producer stopped 30 ms: underruns counted, packets keep their size", uac.underruns > base_p &&
          cons_bad == 0);
    check("  the empty ring repeats the last frame", cons_rep > 0);
    base_p = uac.overruns;
    run(40, 0, 1, 0);                                   /* the consumer stops (flowing still 1) */
    check("consumer stopped 40 ms: overruns counted, the ring never overfills", uac.overruns > base_p &&
          ua_w - ua_r <= UA_N);
    uac_stream(0);
    run(10, 0, 1, 1);
    check("alt 0: nothing fed, nothing taken", ua_w - ua_r <= UA_N && !uac.feed);
    uac_stream(1);
    uac.flowing = 1;
    cons_n = 0;
    cons_bad = cons_rep = 0;
    base_p = uac.underruns + uac.overruns;
    run(3000, 0, 1, 1);
    check("restart: primed again, in order, no glitch", cons_n > 3000u * 44u && cons_bad == 0 && cons_rep == 0 &&
          uac.underruns + uac.overruns == base_p);
}

int main(void)
{
    printf("-- USB audio input, CDC %s\n", T_CDC ? "on" : "off");
    test_descriptors();
    test_ring();
    printf(fails ? "UAC TEST FAILED (%d)\n" : "uac: all ok\n", fails);
    return fails != 0;
}
