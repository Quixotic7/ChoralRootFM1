/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot on the M-Vave SMK-37, the TRS jack's MIDI OUT (hal/smk_uart_tx.h, from Skiff; GitHub issue 42), on a
 * modelled UART (build with -DBOARD_SMK37=1): the USB-MIDI packets usb.c midi_out_event hands it become the MIDI bytes,
 * in order; the realtime bytes go ahead; a burst bigger than the ring never blocks, never cuts a message and never
 * keeps out what releases a note; Options > TRS MIDI Out Off lets only that through; a UART that never answers stalls
 * nothing; the jack's pace (a byte every fourth 10 kHz tick). The register side (smk_uart_tx_init) is the unit's. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif

/* the UART: CON0 bit 15 TPND (set when the byte has gone), bit 13 written 1: TPND cleared; BUF written: a byte out */
static uint32_t ut_con0, ut_buf;
#define SMK_UT_HOST 1
#define SMK_UT1_CON0 ut_con0
#define SMK_UT1_BUF ut_buf
#include "smk_uart_tx.h"

static int fails, checks;
static void ok(int c, const char *what)
{
    checks++;
    if (!c) {
        fails++;
        printf("FAIL: %s\n", what);
    }
}

static uint8_t wire[4096];                       /* the bytes as they left, in order */
static uint32_t nwire, t_us, wire_end, dead;     /* dead: TPND never comes (a UART that does not answer) */
static void tick(void)                           /* one TIMER5 tick (100 us) of the modelled UART */
{
    uint32_t sent = smk_ut_sent;
    if (!dead && wire_end && t_us >= wire_end)
        ut_con0 |= 0x8000u;                      /* the byte has gone (320 us at 31250 baud) */
    smk_uart_tx_tick();
    if (ut_con0 & 0x2000u)
        ut_con0 &= ~0xA000u;
    if (smk_ut_sent != sent) {
        if (nwire < sizeof wire)
            wire[nwire++] = (uint8_t)ut_buf;
        wire_end = t_us + 320u;
    }
    t_us += 100u;
}
static uint32_t drain(void)                      /* ticks until the ring, the realtime slot and the wire are empty */
{
    uint32_t n = 0;
    while ((smk_ut_r != smk_ut_w || smk_ut_rt || smk_ut_busy) && n < 100000u) {
        tick();
        n++;
    }
    return n;
}
static void reset(void)
{
    drain();
    nwire = 0;
    smk_ut_drops = smk_ut_timeouts = 0;
    smk_ut_off = 0;
    dead = 0;
}
static uint32_t pkt(uint32_t st, uint32_t d1, uint32_t d2)   /* cr_out.c cr_midi's packet */
{
    return (st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
}
static uint32_t used(void) { return (uint8_t)(smk_ut_w - smk_ut_r); }
/* the wire as MIDI: every status byte followed by its data bytes (realtime anywhere); 1: no message cut */
static int whole(uint32_t *nmsg)
{
    uint32_t i, need = 0;
    *nmsg = 0;
    for (i = 0; i < nwire; i++) {
        uint32_t b = wire[i];
        if (b >= 0xF8u)
            continue;
        if (b & 0x80u) {
            if (need)
                return 0;
            need = (b & 0xE0u) == 0xC0u ? 1u : b >= 0xF0u ? 0u : 2u;
            (*nmsg)++;
        } else if (!need--) {
            return 0;
        }
    }
    return !need;
}

int main(void)
{
    uint32_t i, n, m;
    static const uint8_t two[] = {0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00};

    /* a note on and off, as usb.c midi_out_event hands them over */
    smk_uart_tx_pkt(pkt(0x90, 60, 100));
    smk_uart_tx_pkt(pkt(0x80, 60, 0));
    n = drain();
    ok(nwire == 6 && !memcmp(wire, two, 6), "a note on and off: 90 3C 64 80 3C 00, in order, no running status");
    ok(n == 6 * 4 + 1, "six bytes in 25 ticks: a byte every fourth tick (320 us on the wire), the last seen gone");

    /* the pace: 99 bytes */
    reset();
    for (i = 0; i < 33; i++)
        smk_uart_tx_pkt(pkt(0xB0, 1, i));
    ok(used() == 99, "33 wheel steps queued (99 bytes, under the 104 a non-release message may fill)");
    n = drain();
    ok(nwire == 99 && n == 99 * 4 + 1, "99 bytes in 397 ticks: 2,500 bytes a second");

    /* the clock goes ahead of the queued bytes, between any two */
    reset();
    smk_uart_tx_pkt(pkt(0x90, 60, 100));
    smk_uart_tx_pkt(pkt(0x90, 64, 100));
    tick();                                      /* 0x90 on the wire */
    smk_uart_tx_pkt(0x0Fu | 0xF8u << 8);         /* cr_out.c's clock pulse */
    drain();
    ok(nwire == 7 && wire[0] == 0x90 && wire[1] == 0xF8 && wire[2] == 0x3C && whole(&m) && m == 2,
       "a clock pulse goes out next, ahead of the five bytes queued, and the notes stay whole");
    reset();
    smk_uart_tx_pkt(pkt(0x90, 60, 100));
    tick();
    smk_uart_tx_pkt(0x0Fu | 0xFAu << 8);         /* Start, then a pulse before Start has gone: */
    smk_uart_tx_pkt(0x0Fu | 0xF8u << 8);         /* it queues behind the bytes waiting (one waits ahead) */
    drain();
    ok(nwire == 5 && wire[1] == 0xFA && wire[4] == 0xF8, "a second realtime byte while one waits: queued, never lost");

    /* a burst bigger than the ring: it never blocks, never cuts a message, the releases still get in */
    reset();
    for (i = 0; i < 100; i++)
        smk_uart_tx_pkt(pkt(0x90, i, 100));
    ok(used() == 105 && smk_ut_drops == 65, "100 note-ons at once: 35 taken (until 24 bytes are left), 65 dropped whole");
    smk_uart_tx_pkt(pkt(0xB0, 64, 0));           /* the sustain up */
    smk_uart_tx_pkt(pkt(0xB0, 123, 0));          /* all notes off */
    smk_uart_tx_pkt(pkt(0xE0, 0, 64));           /* the bend back to the centre */
    for (i = 0; i < 4; i++)
        smk_uart_tx_pkt(pkt(0x80, i, 0));
    ok(used() == 126, "the sustain up, all notes off, the bend centred and four note-offs: all in the last 24 bytes");
    smk_uart_tx_pkt(pkt(0x90, 99, 0));           /* a note-on at velocity 0 (a note-off): 2 bytes free, 3 needed */
    smk_uart_tx_pkt(pkt(0xB0, 1, 64));           /* a wheel step */
    ok(used() == 126 && smk_ut_drops == 67, "the ring full: a release that does not fit is dropped whole, not cut");
    smk_uart_tx_pkt(0x0Fu | 0xF8u << 8);
    ok(smk_ut_rt == 0xF8, "the ring full: the clock pulse still waits in its own slot");
    n = drain();
    ok(whole(&m) && m == 42 && nwire == 127, "the burst drained: 42 whole messages and the pulse, nothing cut");
    ok(wire[0] == 0xF8 && wire[106] == 0xB0 && wire[107] == 64 && wire[109] == 0xB0 && wire[110] == 123 &&
       wire[112] == 0xE0 && wire[115] == 0x80, "the pulse first; the releases after the notes they release, in order");

    /* Options > TRS MIDI Out Off: only what releases a note */
    reset();
    smk_ut_off = 1;
    smk_uart_tx_pkt(pkt(0x90, 60, 100));
    smk_uart_tx_pkt(pkt(0xB0, 1, 30));
    smk_uart_tx_pkt(pkt(0xB0, 64, 127));
    smk_uart_tx_pkt(pkt(0xE0, 0, 70));
    smk_uart_tx_pkt(0x0Fu | 0xF8u << 8);
    smk_uart_tx_pkt(pkt(0xC0, 5, 0));
    ok(used() == 0 && !smk_ut_rt, "Off: a note-on, a wheel, the sustain down, a bend, the clock, a program: none");
    smk_uart_tx_pkt(pkt(0x80, 60, 0));
    smk_uart_tx_pkt(pkt(0x90, 61, 0));
    smk_uart_tx_pkt(pkt(0xB0, 64, 0));
    smk_uart_tx_pkt(pkt(0xB0, 120, 0));
    smk_uart_tx_pkt(pkt(0xB0, 123, 0));
    smk_uart_tx_pkt(pkt(0xE0, 0, 64));
    drain();
    ok(nwire == 18 && whole(&m) && m == 6, "Off: the note-offs, the sustain up, all sound / notes off, the bend centred");

    /* the packets' sizes (USB-MIDI CINs) */
    reset();
    smk_uart_tx_pkt(0x00u | 0x90u << 8);         /* CIN 0, 1: reserved, cable events: nothing */
    smk_uart_tx_pkt(0x01u | 0x90u << 8);
    smk_uart_tx_pkt(pkt(0xC3, 5, 0));            /* a program change: 2 bytes */
    smk_uart_tx_pkt(pkt(0xD3, 77, 0));           /* channel pressure: 2 */
    smk_uart_tx_pkt(pkt(0xA3, 60, 50));          /* poly pressure: 3 */
    smk_uart_tx_pkt(0x0Fu | 0xF6u << 8);         /* a single byte that is not realtime: in turn */
    drain();
    ok(nwire == 8 && wire[0] == 0xC3 && wire[2] == 0xD3 && wire[4] == 0xA3 && wire[7] == 0xF6 && whole(&m) && m == 4,
       "CIN sizes: none for 0 / 1, 2 for C / D, 3 for A, 1 for F, in turn");

    /* a UART that never answers: a byte every 2 ms, counted, nothing stalls */
    reset();
    dead = 1;
    for (i = 0; i < 3; i++)
        smk_uart_tx_pkt(pkt(0x90, 60 + i, 100));
    n = drain();
    ok(nwire == 9 && smk_ut_timeouts == 9 && n == 9 * 20 + 1,
       "TPND never set: each byte given up on after 20 ticks (2 ms), counted, the next goes");
    dead = 0;

    printf("%s: %d of %d checks\n", fails ? "FAIL" : "ok", checks - fails, checks);
    return fails != 0;
}
