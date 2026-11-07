/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 illuminus5 (Skiff, github.com/illuminus5/skiff, GPL-3.0-only: its firmware/hal/smk_uart_tx.h of
 * Skiff 2.2, commit 76b5f9c), brought to ChoralRoot by finlaysteven1-netizen (ChoralRootFM1 issue 42); modified for
 * ChoralRoot 2026-11-06 (LICENSING.md): the packet's bytes (smk_uart_tx_pkt, from the issue's usb.c midi_out_trs), the
 * option, the messages that always get in, a host test (tests/smk_trs_test.c) */
/* SMK-37: the TRS jack is MIDI OUT, UART1 TX on PC7 (the FM-1's UART1 is MIDI IN on PH8, fm1_uart.h, which the SMK
 * never sets up: PH8 is a key-matrix row there, choralroot.c FELUCCA_UART). Set up as the stock firmware does (the
 * SMK-37 hardware notes, github.com/genkigenki/fm1-to-smk37 docs/01-hardware.md section 9): IOMAP_CON3 bit 7 clear and
 * [23:20] = 1 (UT1 TX onto its output), PC7 pull-up and pull-down, output low, input enabled; UART1 at 31250 baud from
 * PLL48M, TX only, no IRQ. PC7 is the FM-1's LCD CS; the SMK's LCD is on PC8 / PB5..7 and its pad LEDs on PC10
 * (fm1_lcd_hw.h), so nothing else drives it. Seen working on an SMK-37 Pro (issue 42: into a Behringer TD-3).
 *
 *   smk_uart_tx_init()        once, before TIMER5 starts (main.c)
 *   smk_uart_tx_pkt(pkt)      a USB-MIDI event packet -> its MIDI bytes (usb.c midi_out_event, through MIDI_OUT_TRS:
 *                             the audio ISR, the ring's only producer). Everything USB MIDI OUT sends, with or without
 *                             a USB host: the parts' notes, wheels, pedal, all-notes-off, and the clock (Options >
 *                             MIDI Clock: OUT). SysEx (backups, sound dumps) goes its own way (usb.c sx_out_q), never
 *                             here, and there is no MIDI thru (ChoralRoot has none)
 *   smk_uart_tx_tick()        TIMER5 (10 kHz, above the audio ISR: the only consumer): the next byte once the last has
 *                             gone (TPND), or after a timeout, so a UART that never answers stalls nothing
 *   smk_ut_off                Options > TRS MIDI Out (cr_ui.c; the session's byte 25, cr_settings.h smk_trs_off):
 *                             0 On (the default), 1 Off: only what releases a note goes out (below), so a note that
 *                             went out before the switch is never left on
 *
 * Never blocking: a full ring drops, it never waits. A byte takes 320 us at 31250 baud and the tick sees it gone on its
 * fourth tick, so the jack carries about 2,500 bytes a second (a note in 1.2 ms); the ring holds 128. Realtime (the
 * clock, Start / Stop) goes ahead of the queued bytes, as MIDI lets a realtime byte go between any two. Any other
 * message goes in whole or not at all: what releases a note (a note-off or a note-on at velocity 0, the sustain pedal
 * up, CC 120 / 123 all sound / notes off, the pitch bend back to the centre) whenever its bytes fit; the rest only
 * while 24 bytes would stay free for those (a note-on, a wheel's step, rather than a stuck note). No running status:
 * every message carries its status byte, so a dropped one never leaves the next under the wrong status. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#ifndef SMK_UT_HOST                                        /* (tests/smk_trs_test.c: its modelled UART instead) */
#define SMK_UT1_CON0   (*(volatile uint32_t *)0x12100u)   /* b0 UTEN, b13 clear TPND, b15 TPND */
#define SMK_UT1_CON1   (*(volatile uint32_t *)0x12104u)
#define SMK_UT1_BAUD   (*(volatile uint32_t *)0x12108u)
#define SMK_UT1_BUF    (*(volatile uint32_t *)0x1210Cu)
#define SMK_UT_CLK_CON1 (*(volatile uint32_t *)0x10010u)  /* [11:10] UART clock: 1 = PLL48M */
#define SMK_UT_IOMAP3  (*(volatile uint32_t *)0x51028u)   /* UT1: b7 fixed IO, [23:20] TX output */
#define SMK_UT_PC_OUT  (*(volatile uint32_t *)0x50080u)
#define SMK_UT_PC_DIR  (*(volatile uint32_t *)0x50088u)
#define SMK_UT_PC_DIE  (*(volatile uint32_t *)0x5008Cu)
#define SMK_UT_PC_PU   (*(volatile uint32_t *)0x50090u)
#define SMK_UT_PC_PD   (*(volatile uint32_t *)0x50094u)
#endif
#define SMK_UT_TX_RING 128u                                /* a power of two */
#define SMK_UT_TIMEOUT 20u                                 /* ticks (2 ms) a byte may take before the next goes anyway */
#define SMK_UT_KEEP    24u                                 /* bytes kept free for what releases a note */

static volatile uint8_t smk_ut_ring[SMK_UT_TX_RING];
static volatile uint8_t smk_ut_w, smk_ut_r, smk_ut_busy;   /* busy: ticks the byte on the wire has had */
static volatile uint8_t smk_ut_rt;                         /* the realtime byte waiting (0: none) */
static volatile uint8_t smk_ut_off;                        /* Options > TRS MIDI Out: 1 Off */
static volatile uint32_t smk_ut_sent, smk_ut_drops, smk_ut_timeouts;

#ifndef SMK_UT_HOST
FM1_INLINE void smk_uart_tx_init(void)
{
    SMK_UT1_CON0 = 0x3400u;                                /* off, pendings cleared */
    SMK_UT1_CON1 = 0;
    SMK_UT_CLK_CON1 = (SMK_UT_CLK_CON1 & ~(3u << 10)) | (1u << 10);
    SMK_UT_IOMAP3 = (SMK_UT_IOMAP3 & ~0x80u & ~(0xFu << 20)) | (1u << 20);
    SMK_UT_PC_PU |= 1u << 7;
    SMK_UT_PC_PD |= 1u << 7;
    SMK_UT_PC_OUT &= ~(1u << 7);
    SMK_UT_PC_DIE |= 1u << 7;
    SMK_UT_PC_DIR &= ~(1u << 7);
    SMK_UT1_BAUD = 48000000u / 31250u / 4u - 1u;           /* 383 */
    __asm__ volatile("csync" ::: "memory");
    SMK_UT1_CON0 |= 1u;                                    /* UTEN */
}
#endif

static void smk_uart_tx_put(uint32_t b)
{
    uint8_t w = smk_ut_w;
    if ((uint8_t)(w - smk_ut_r) >= SMK_UT_TX_RING) {
        smk_ut_drops++;
        return;
    }
    smk_ut_ring[w % SMK_UT_TX_RING] = (uint8_t)b;
    __asm__ volatile("" ::: "memory");                     /* (one core: the tick preempts us, never the reverse) */
    smk_ut_w = (uint8_t)(w + 1u);
}

static void smk_uart_tx_rt(uint32_t b)
{
    if (smk_ut_rt)
        smk_uart_tx_put(b);
    else
        smk_ut_rt = (uint8_t)b;
}

static void smk_uart_tx_pkt(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    uint32_t nb = (0x7AFFE7E0u >> (2u * cin)) & 3u;     /* bytes of a CIN: 2/3 system common, 8..E channel, F one */
    uint32_t freeb = SMK_UT_TX_RING - (uint8_t)(smk_ut_w - smk_ut_r);
    uint32_t prio = cin == 8u || (cin == 9u && !d2) ||
                    (cin == 0xBu && (d1 == 120u || d1 == 123u || (d1 == 64u && d2 < 64u))) ||
                    (cin == 0xEu && !d1 && d2 == 64u);
    if (smk_ut_off && !prio)
        return;
    if (nb == 1u && st >= 0xF8u) {
        smk_uart_tx_rt(st);
        return;
    }
    if (!nb || freeb < (prio ? nb : SMK_UT_KEEP)) {
        if (nb)
            smk_ut_drops++;
        return;
    }
    smk_uart_tx_put(st);
    if (nb > 1u)
        smk_uart_tx_put(d1);
    if (nb > 2u)
        smk_uart_tx_put(d2);
}
#define MIDI_OUT_TRS(pkt) smk_uart_tx_pkt(pkt)             /* (usb.c midi_out_event; not in the update loader, the
                                                            * emulator or the FM-1 build: none includes this file) */

FM1_INLINE void smk_uart_tx_tick(void)
{
    if (smk_ut_busy) {                                     /* the last byte: gone, or given up on */
        if (!(SMK_UT1_CON0 & 0x8000u) && smk_ut_busy < SMK_UT_TIMEOUT) {
            smk_ut_busy++;
            return;
        }
        if (!(SMK_UT1_CON0 & 0x8000u))
            smk_ut_timeouts++;
        smk_ut_busy = 0;
    }
    if (smk_ut_r == smk_ut_w && !smk_ut_rt)
        return;
    SMK_UT1_CON0 |= 1u << 13;                              /* clear TPND */
    if (smk_ut_rt) {
        SMK_UT1_BUF = smk_ut_rt;
        smk_ut_rt = 0;
    } else {
        SMK_UT1_BUF = smk_ut_ring[smk_ut_r % SMK_UT_TX_RING];
        smk_ut_r = (uint8_t)(smk_ut_r + 1u);
    }
    smk_ut_busy = 1;
    smk_ut_sent++;
}
