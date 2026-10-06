/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 TIMER5: the 10 kHz system tick (input scan, USB poll, ms count).
 * TIMER4 is the free-running time base (fm1_time.h); both are unused by the
 * ROM and the SPL. JL_TIMER5 = 0x10900: CON, CNT +4, PRD +8.
 *
 *   fm1_timer5_start(isr, prio)   OSC /4 = 6 MHz, PRD 600 -> 10 kHz, IRQ 63
 *   fm1_timer5_ack()              first thing in the ISR
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_irq.h"

#define FM1_T5_CON (*(volatile uint32_t *)0x10900u)
#define FM1_T5_CNT (*(volatile uint32_t *)0x10904u)
#define FM1_T5_PRD (*(volatile uint32_t *)0x10908u)

FM1_INLINE void fm1_timer5_start(void (*isr)(void), uint32_t prio)   /* IRQs off */
{
    FM1_T5_CON = 0x4000u;
    FM1_T5_CNT = 0;
    FM1_T5_PRD = 600u;
    fm1_irq_attach(FM1_IRQ_TIMER5, isr, prio);
    FM1_T5_CON = 0x4019u;
}

FM1_INLINE void fm1_timer5_ack(void) { FM1_T5_CON |= 0x4000u; }
