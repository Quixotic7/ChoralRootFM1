/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 UART1 as MIDI IN: PH8 -> input
 * channel 1 -> UART1 RX, 31250 baud, RX DMA into a ring, polled (no IRQ).
 * The MIDI parser is midi_uart.c. RX verified on hardware.
 *
 *   fm1_uart1_midi_init(ring, len)   len a power of two, ring aligned 16;
 *                                    before TIMER5 starts (PORTH RMW)
 *   fm1_uart1_rx_take()              bytes DMA'd since the last call
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_UT1_CON0   (*(volatile uint32_t *)0x12100u)
#define FM1_UT1_CON1   (*(volatile uint32_t *)0x12104u)
#define FM1_UT1_BAUD   (*(volatile uint32_t *)0x12108u)
#define FM1_UT1_OTCNT  (*(volatile uint32_t *)0x12110u)
#define FM1_UT1_RXSADR (*(volatile uint32_t *)0x1211Cu)
#define FM1_UT1_RXEADR (*(volatile uint32_t *)0x12120u)
#define FM1_UT1_RXCNT  (*(volatile uint32_t *)0x12124u)
#define FM1_UT1_HRXCNT (*(volatile uint32_t *)0x12128u)
#define FM1_UM_CLK_CON1  (*(volatile uint32_t *)0x10010u)   /* [11:10] UART clock: 1 = PLL48M */
#define FM1_UM_IOMAP2    (*(volatile uint32_t *)0x51024u)   /* input ch1 source [13:8] */
#define FM1_UM_IOMAP3    (*(volatile uint32_t *)0x51028u)   /* UT1: b7 fixed IO, [6:4] RX select */
#define FM1_UM_PH_DIR    (*(volatile uint32_t *)0x501C8u)
#define FM1_UM_PH_DIE    (*(volatile uint32_t *)0x501CCu)
#define FM1_UM_PH_PU     (*(volatile uint32_t *)0x501D0u)
#define FM1_UM_PH_PD     (*(volatile uint32_t *)0x501D4u)

FM1_INLINE void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len)
{
    FM1_UT1_CON0 = 0x3400u;                            /* off, pendings cleared */
    FM1_UT1_CON1 = 0;
    FM1_UM_CLK_CON1 = (FM1_UM_CLK_CON1 & ~(3u << 10)) | (1u << 10);
    FM1_UM_IOMAP3 &= ~0x80u;
    FM1_UM_IOMAP3 = (FM1_UM_IOMAP3 & ~0x70u) | (5u << 4);   /* UT1 RX = input ch 1 */
    FM1_UM_IOMAP2 = (FM1_UM_IOMAP2 & ~0x3F00u) | (49u << 8);/* ch 1 = PH8 */
    FM1_UM_PH_PD &= ~(1u << 8);
    FM1_UM_PH_DIE |= 1u << 8;
    FM1_UM_PH_DIR |= 1u << 8;
    FM1_UM_PH_PU |= 1u << 8;
    FM1_UT1_RXSADR = (uint32_t)(uintptr_t)ring;
    FM1_UT1_RXEADR = (uint32_t)(uintptr_t)ring + len;
    FM1_UT1_RXCNT = len;
    FM1_UT1_BAUD = 48000000u / 31250u / 4u - 1u;       /* 383 */
    FM1_UT1_OTCNT = 60000u;
    __asm__ volatile("csync" ::: "memory");
    FM1_UT1_CON0 = 0x40u | 0x80u | 0x1000u | 0x400u;   /* RXDMA, RDC, clear R/OT; no IEs */
    __asm__ volatile("csync" ::: "memory");
    FM1_UT1_CON0 |= 1u;                                /* UTEN last */
}

FM1_INLINE uint32_t fm1_uart1_rx_take(void)
{
    FM1_UT1_CON0 |= 0x80u;                             /* RDC: latch the count */
    __asm__ volatile("csync" ::: "memory");
    FM1_UT1_CON0 |= 0x1400u;                           /* clear RPND / OTPND */
    return FM1_UT1_HRXCNT & 0xFFFFu;
}
