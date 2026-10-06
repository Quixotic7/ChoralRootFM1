/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the MIDI input parsers: the running-status parser in
 * firmware/src/midi_uart.c (um_byte) and the USB-MIDI SysEx path of firmware/src/usb.c
 * (sysex_byte frame assembly, ota_wire_send packetising). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"

static uint32_t now_ms;
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }

static int check(const char *what, int ok)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void feed(const uint8_t *p, uint32_t n)
{
    while (n--)
        sysex_byte(*p++);
}

/* the queued SysEx packets back to bytes, as the host's USB-MIDI driver does */
static uint32_t drain(uint8_t *out, int *cin_ok)
{
    uint32_t n = 0;
    *cin_ok = 1;
    while (so_r != so_w) {
        uint32_t pkt = sx_out_q[so_r++ % SXQ], cin = pkt & 15u, k;
        uint32_t nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : cin == 5u ? 1u : 0u;
        if (!nb || (cin == 4u) != (so_r != so_w))      /* 4 except the last, which ends (5/6/7) */
            *cin_ok = 0;
        for (k = 0; k < nb; k++)
            out[n++] = (uint8_t)(pkt >> (8u * (k + 1u)));
    }
    return n;
}

static int test_usb_sysex(void)
{
    static uint8_t msg[700], got[700];
    uint32_t n, i;
    int bad = 0, enc_ok = 1, rt_ok = 1, cin_ok;
    usb.config = 1;
    for (n = 2; n <= 40u; n++) {                       /* every length mod 3, through the ring */
        msg[0] = 0xF0;
        for (i = 1; i + 1u < n; i++)
            msg[i] = (uint8_t)((i * 37u + n) & 0x7Fu);
        msg[n - 1] = 0xF7;
        if (ota_wire_send(msg, n) || drain(got, &cin_ok) != n || memcmp(got, msg, n) || !cin_ok || sx_busy)
            enc_ok = 0;
        feed(got, n);
        if (n > 2u && (!sx_ready || sx_frame_len != n - 2u || memcmp(sx_frame, msg + 1, n - 2u)))
            rt_ok = 0;
        ota_frame_done();
    }
    bad += check("usb: ota_wire_send packets (CIN 4.. then 5/6/7)", enc_ok);
    bad += check("usb: received frame == the bytes between F0 and F7", rt_ok);

    feed((const uint8_t *)"\xF0\x01\x02\xF7", 4);
    feed((const uint8_t *)"\xF0\x03\x04\x05\xF7", 5);
    bad += check("usb: a frame while one is pending is dropped",
                 sx_ready && sx_frame_len == 2u && sx_frame[0] == 1 && sx_frame[1] == 2);
    ota_frame_done();
    feed((const uint8_t *)"\xF0\x11\x12", 3);           /* cut off by the next F0 */
    feed((const uint8_t *)"\xF0\x21\xF7", 3);
    bad += check("usb: a truncated frame is replaced by the next one",
                 sx_ready && sx_frame_len == 1u && sx_frame[0] == 0x21);
    ota_frame_done();
    msg[0] = 0xF0;
    memset(msg + 1, 0x55, sizeof msg - 2u);
    msg[sizeof msg - 1u] = 0xF7;
    feed(msg, sizeof msg);
    bad += check("usb: an oversized frame (698 B) is not taken", !sx_ready);
    feed((const uint8_t *)"\xF0\x22\x24\x35\x7D\xF7", 6);
    bad += check("usb: soft key -> uboot_req, not a frame", usb.uboot_req && !sx_ready);
    feed((const uint8_t *)"\xF0\x22\x24\x35\x7F\xF7", 6);
    bad += check("usb: upgrade key -> ota_req, not a frame", usb.ota_req && !sx_ready);
    feed((const uint8_t *)"\x22\x24\xF7", 3);           /* no F0: ignored */
    bad += check("usb: bytes outside F0..F7 are ignored", !sx_ready);

    so_w = so_r + SXQ;                                  /* ring full ... */
    usb.config = 0;                                     /* ... and the host gone */
    bad += check("usb: send with a full ring and no host fails at once",
                 ota_wire_send(msg, 9) == -1 && !sx_busy && now_ms == 0);
    usb.config = 1;
    bad += check("usb: send with a full ring times out (200 ms)",
                 ota_wire_send(msg, 9) == -1 && !sx_busy && now_ms > 200u && now_ms < 210u);
    so_r = so_w;
    return bad;
}

int main(void)
{
    static const uint8_t in[] = {
        0x90, 60, 100, 62, 101,          /* note on + running status */
        0xF8, 64, 0xFE, 102,             /* realtime inside a message */
        0xC1, 5, 6,                      /* program change + running */
        0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7, 70, 71,   /* SysEx dropped; cancels running status */
        0xB0, 7, 0x7F, 0xF2, 1, 2, 9, 9, /* CC, song position (dropped), data without status */
        0x80, 60, 0,
    };
    static const uint32_t want[] = {
        0x643C9009u, 0x653E9009u, 0x0000F80Fu, 0x66409009u, 0x0005C10Cu, 0x0006C10Cu, 0x7F07B00Bu, 0x003C8008u,
    };
    uint32_t i, bad = 0, n = sizeof want / sizeof want[0];
    for (i = 0; i < sizeof in; i++)
        um_byte(in[i]);
    if (mi_w != n) {
        printf("got %u packets, want %u\n", mi_w, n);
        bad = 1;
    }
    for (i = 0; i < n && i < mi_w; i++)
        if (midi_in_q[i] != want[i]) {
            printf("pkt %u: %08x want %08x\n", i, midi_in_q[i], want[i]);
            bad = 1;
        }
    bad += (uint32_t)check("uart: running status, realtime, SysEx, system common", !bad);
    {
        uint32_t w0 = mi_w;
        fm1_ms = 1234u;
        um_byte(0xFAu);
        midi_in_event(0x0000FB0Fu);
        midi_in_event(0x0000FE0Fu);                  /* active sensing ignored */
        bad += (uint32_t)check("clock: timestamps and USB/TRS source, no active sensing",
            mi_w == w0 + 2u && midi_in_ms[w0 % MQ] == 1234u && midi_in_source[w0 % MQ] == 2u &&
            midi_in_source[(w0 + 1u) % MQ] == 1u);
    }
    {   /* 4-track routing reads the channel from the packet as for USB-MIDI: cable 0, CIN = status >> 4 */
        static const uint8_t chs[] = {0x90, 60, 1, 0x91, 61, 2, 0x92, 62, 3, 0x99, 36, 4, 0x9F, 63, 5, 0x89, 36, 0};
        uint32_t w0 = mi_w, ok = 1;
        for (i = 0; i < sizeof chs; i++)
            um_byte(chs[i]);
        for (i = 0; i < 6u; i++) {
            uint32_t pkt = midi_in_q[(w0 + i) % MQ], st = chs[3u * i];
            ok &= mi_w == w0 + 6u && (pkt & 0xFFu) == (st >> 4) && ((pkt >> 8) & 0xFFu) == st &&
                  ((pkt >> 16) & 0x7Fu) == chs[3u * i + 1u] && (pkt >> 24) == chs[3u * i + 2u];
        }
        bad += (uint32_t)check("uart: channels 1, 2, 3, 10, 16 -> USB-MIDI packets", ok);
    }
    bad += (uint32_t)test_usb_sysex();
    printf("%s\n", bad ? "MIDI PARSER TEST FAILED" : "midi parser test passed");
    return (int)bad;
}
