/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff) SMK-37 Pro: the TRS jack is MIDI OUT, UART1 TX on PC7 (the FM-1's UART1 is MIDI IN on PH8, fm1_uart.h, which
 * the SMK never sets up: PH8 is a key-matrix row there). Set up as the stock firmware does (the SMK-37 hardware notes,
 * github.com/genkigenki/fm1-to-smk37): IOMAP_CON3 bit 7 clear and [23:20] = 1 (UT1 TX onto its output), PC7 pull-up and
 * pull-down, output low, input enabled; UART1 at 31250 baud from PLL48M, TX only, no IRQ. Not yet checked on a unit.
 *
 *   smk_uart_tx_init()        once, before TIMER5 starts
 *   smk_uart_tx_put(b)        queue a byte (TIMER5 only: usb.c mo_trs, (2.2) THRU; dropped when the ring is full)
 *   smk_uart_tx_rt(b)         (2.2) a realtime byte (the MIDI clock): it goes ahead of the bytes queued (one waits; a
 *                             second one queues), as MIDI lets a realtime byte go between any two
 *   smk_uart_tx_tick()        TIMER5 (10 kHz): the next byte once the last has gone (TPND), or after a timeout, so a
 *                             UART that never answers stalls nothing
 * A byte takes 320 us at 31250 baud, about 3 ticks: the ring drains at the wire's speed. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

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
#define SMK_UT_TX_RING 128u                                /* a power of two */
#define SMK_UT_TIMEOUT 20u                                 /* ticks (2 ms) a byte may take before the next goes anyway */

static volatile uint8_t smk_ut_ring[SMK_UT_TX_RING];
static volatile uint8_t smk_ut_w, smk_ut_r, smk_ut_busy;   /* busy: ticks the byte on the wire has had */
static volatile uint8_t smk_ut_rt;                         /* (2.2) the realtime byte waiting (0: none) */
static volatile uint32_t smk_ut_sent, smk_ut_drops, smk_ut_timeouts;

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

static void smk_uart_tx_put(uint32_t b)
{
    uint8_t w = smk_ut_w;
    if ((uint8_t)(w - smk_ut_r) >= SMK_UT_TX_RING) {
        smk_ut_drops++;
        return;
    }
    smk_ut_ring[w % SMK_UT_TX_RING] = (uint8_t)b;
    __asm__ volatile("" ::: "memory");
    smk_ut_w = (uint8_t)(w + 1u);
}

static void smk_uart_tx_rt(uint32_t b)
{
    if (smk_ut_rt)
        smk_uart_tx_put(b);
    else
        smk_ut_rt = (uint8_t)b;
}

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
