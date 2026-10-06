/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 audio out: ALNK0 I2S -> external codec, double-buffered DMA
 * The application owns the buffer: two halves
 * of half_words int32 (L, R, 24-bit left-justified), zeroed before init.
 *
 *   fm1_audio_init(buf, half_words, isr, prio)  codec and ALNK0 bring-up;
 *                         isr = asm wrapper (fm1_isr.S) for FM1_IRQ_ALNK0
 *   in the ISR:   p = fm1_audio_pending();  fm1_audio_ack_aux(p);
 *                 if (p & FM1_AUDIO_HALF) { fill fm1_audio_free_half();
 *                                           fm1_audio_ack_half(); }
 *   fm1_audio_stop()      DMA off (crash screen, UBOOT): no looping buzz
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"
#include "fm1_irq.h"
#include "fm1_gpio.h"

#define FM1_ALNK 0x12E00u
#define FM1_ALNK_CON0 (*(volatile uint16_t *)(FM1_ALNK + 0x00u))
#define FM1_ALNK_CON1 (*(volatile uint16_t *)(FM1_ALNK + 0x04u))
#define FM1_ALNK_CON2 (*(volatile uint8_t *)(FM1_ALNK + 0x08u))
#define FM1_ALNK_CON3 (*(volatile uint8_t *)(FM1_ALNK + 0x0Cu))
#define FM1_ALNK_ADR3 (*(volatile uint32_t *)(FM1_ALNK + 0x1Cu))
#define FM1_ALNK_LEN (*(volatile uint16_t *)(FM1_ALNK + 0x20u))
#define FM1_CLK_CON2 (*(volatile uint32_t *)0x10014u)
#define FM1_IOMAP_CON5 (*(volatile uint32_t *)0x51030u)
#define FM1_AUDIO_HALF 0x80u      /* CON2 pending: a half buffer is free */

static void fm1__pc_out(uint32_t bit, int v)   /* codec control lines on PC0/1/2/6 */
{
    uint32_t m = 1u << bit;
    FM1_PR(FM1_PC, FM1_DIE) |= m;
    if (v)
        FM1_PR(FM1_PC, FM1_OUT) |= m;
    else
        FM1_PR(FM1_PC, FM1_OUT) &= ~m;
    FM1_PR(FM1_PC, FM1_DIR) &= ~m;
}

FM1_INLINE void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    fm1__pc_out(6, 0);
    fm1_delay_ms(5);
    FM1_ALNK_CON0 = 0;
    FM1_ALNK_CON1 = 0;
    FM1_ALNK_CON2 = 0;
    FM1_ALNK_CON3 = 0;
    FM1_IOMAP_CON5 &= ~0xC0u;
    FM1_ALNK_CON3 |= 3u;
    fm1__pc_out(0, 1);
    FM1_ALNK_CON0 |= 0x100u;
    fm1__pc_out(2, 1);
    fm1__pc_out(1, 1);
    FM1_ALNK_CON0 |= 0x80u;
    FM1_ALNK_CON0 &= ~0x40u;
    FM1_ALNK_LEN = half_words;
    FM1_ALNK_CON0 &= ~0x400u;
    FM1_ALNK_CON0 &= ~0x200u;
    FM1_CLK_CON2 &= 0xFFFFF0FFu;
    FM1_ALNK_CON3 &= ~0x1Cu;
    FM1_ALNK_CON3 = (uint8_t)((FM1_ALNK_CON3 & 0x1Fu) | 0x80u);
    FM1_ALNK_CON1 |= 1u << 12;
    FM1_ALNK_CON1 |= 1u << 14;
    FM1_ALNK_ADR3 = (uint32_t)(uintptr_t)buf;
    fm1__pc_out(6, 1);
    FM1_ALNK_CON1 &= ~(1u << 15);
    FM1_ALNK_CON2 = 0x0Fu;
    fm1_irq_attach(FM1_IRQ_ALNK0, isr, prio);
    FM1_ALNK_CON0 |= 0x800u;
    fm1_delay_ms(5);
    fm1__pc_out(6, 1);
}

FM1_INLINE uint8_t fm1_audio_pending(void) { return FM1_ALNK_CON2; }
FM1_INLINE void fm1_audio_ack_aux(uint8_t p)   /* the other three pendings */
{
    if (p & 0x10u)
        FM1_ALNK_CON2 |= 1u;
    if (p & 0x20u)
        FM1_ALNK_CON2 |= 2u;
    if (p & 0x40u)
        FM1_ALNK_CON2 |= 4u;
}
FM1_INLINE uint32_t fm1_audio_free_half(void) { return ((FM1_ALNK_CON0 >> 15) & 1u) ^ 1u; }   /* 0 / 1 */
FM1_INLINE void fm1_audio_ack_half(void) { FM1_ALNK_CON2 |= 0x08u; }
FM1_INLINE void fm1_audio_stop(void) { FM1_ALNK_CON0 &= ~0x800u; }
