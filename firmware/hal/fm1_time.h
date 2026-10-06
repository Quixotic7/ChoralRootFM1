/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 time base: TIMER4 free-running from the 24 MHz crystal.
 * Measured on hardware: under mask-ROM UBOOT the crystal runs and TIMER4 counts
 * 24 MHz, matching the host's measurement of the same interval to 0.3 %.
 * TIMER4 is not used by the ROM or the SPL. No interrupts, no ROM calls.
 *
 * JL_TIMER4 = 0x10800: CON [1:0]=1 run, [3:2]=2 OSC source, [7:4]=0 /1,
 * bit14 = clear pending; CNT +4; PRD +8 (32-bit, set to 0xFFFFFFFF).
 * The counter wraps every 178.9 s; use differences (unsigned arithmetic).
 */
#pragma once
#include <stdint.h>

#define FM1_T4_CON (*(volatile uint32_t *)0x10800u)
#define FM1_T4_CNT (*(volatile uint32_t *)0x10804u)
#define FM1_T4_PRD (*(volatile uint32_t *)0x10808u)
#define FM1_TICKS_PER_US 24u

/* Start TIMER4 unless it already runs, so time stays continuous across
 * RAM-run jumps. */
static inline void fm1_time_init(void)
{
    if ((FM1_T4_CON & 0xFu) == ((2u << 2) | 1u))
        return;
    FM1_T4_CON = 0;
    FM1_T4_PRD = 0xFFFFFFFFu;
    FM1_T4_CNT = 0;
    FM1_T4_CON = (2u << 2) | 1u | (1u << 14);
}

static inline uint32_t fm1_ticks(void) { return FM1_T4_CNT; }

/* Microseconds, wrapping every ~178.9 s. */
static inline uint32_t fm1_micros(void) { return FM1_T4_CNT / FM1_TICKS_PER_US; }

static inline void fm1_delay_us(uint32_t us)
{
    uint32_t start = FM1_T4_CNT, span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(FM1_T4_CNT - start) < span)
        ;
}

static inline void fm1_delay_ms(uint32_t ms)
{
    while (ms--)
        fm1_delay_us(1000u);
}
