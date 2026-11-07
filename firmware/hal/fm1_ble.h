/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* FM-1 (AC791N / WL82) BLE baseband: the registers, the link-column port, the interrupt lines and the radio
 * bring-up, for the route C driver (src/ble/ble_hw_wl82.c, FELUCCA_BLE). Written only from the clean-room fact
 * sheet docs/BLE-HW-FACTS.md (branch feat/ble-facts, 41b7374; §14-§17 from d907ce3): every register access names its
 * section there as "HW §n". Nothing in this file ran on a real FM-1; the emulator models only the baseband engine
 * (fm1-emulator feat/ble-engine), not the radio analog side.
 *
 *   fm1_ble_rf_init(trims)      the radio in stock's second-boot order (HW §16): the captured Wi-Fi front end with
 *                               the stored trims (hal/fm1_ble_rf.h), clocks and power, the BT analog block (HW §5.1,
 *                               §5.4); what stays TODO(hardware) is listed in fm1_ble_rf.h
 *   fm1_ble_bb_init(base, size) the BLE baseband (HW §5.5): enable, timing words, the baseband RAM window
 *   fm1_ble_col_wr / _rd        the per-link column port (HW §2.1 0x2801C/20/24, §2.3)
 *   fm1_ble_irq_attach(prio)    IRQ 45 (event) and IRQ 29 (RX) to the driver's bodies (HW §10)
 *   fm1_ble_rng32()             the random-number source (HW §1 0x13B00/04) */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_irq.h"
#include "fm1_time.h"
#include "fm1_ble_rf.h"   /* the captured start-up program (build/gen/ble_rf_tables.h) */

/* ---- BLE baseband registers (HW §2.1), all 32-bit accesses */
#define FM1_BLE_CON      (*(volatile uint32_t *)0x28000u)   /* bit0 enable, 13/14 init, 4 cleared */
#define FM1_BLE_T08      (*(volatile uint32_t *)0x28008u)   /* [9:0] 70, [15:12] 0xF */
#define FM1_BLE_T0C      (*(volatile uint32_t *)0x2800Cu)   /* [9:0] 70: a path delay in us [I] */
#define FM1_BLE_T10      (*(volatile uint32_t *)0x28010u)   /* 62 | 62 << 8 */
#define FM1_BLE_T14      (*(volatile uint32_t *)0x28014u)   /* 30 */
#define FM1_BLE_T18      (*(volatile uint32_t *)0x28018u)   /* 20 | 20 << 8 */
#define FM1_BLE_COLCMD   (*(volatile uint32_t *)0x2801Cu)   /* column << 10 | link << 4 | op (5 write, 2 read) */
#define FM1_BLE_COLDATA  (*(volatile uint32_t *)0x28020u)   /* the data word for op 5 */
#define FM1_BLE_COLRES   (*(volatile uint32_t *)0x28024u)   /* the result of op 2 */
#define FM1_BLE_IEN      (*(volatile uint32_t *)0x28028u)   /* bit n event (IRQ 45), bit 8+n RX (IRQ 29) */
#define FM1_BLE_IACK     (*(volatile uint32_t *)0x2802Cu)   /* write 1 to acknowledge */
#define FM1_BLE_IPND     (*(volatile uint32_t *)0x28030u)   /* pending, the same bits */
#define FM1_BLE_T34      (*(volatile uint32_t *)0x28034u)   /* bit0 1, [15:8] a parameter (stock 0x0A) */
#define FM1_BLE_STAT     (*(volatile uint32_t *)0x28038u)   /* bit1 busy; bit7 flag, acknowledged by bit6 */
#define FM1_BLE_G2EN     (*(volatile uint32_t *)0x2804Cu)   /* second group: bit n enable, bit 8+n acknowledge */
#define FM1_BLE_G2PND    (*(volatile uint32_t *)0x28050u)   /* second group pending */

/* ---- BT analog / configuration block (HW §2.2) */
#define FM1_BT_C00       (*(volatile uint32_t *)0x2FC00u)
#define FM1_BT_C04       (*(volatile uint32_t *)0x2FC04u)
#define FM1_BT_C08       (*(volatile uint32_t *)0x2FC08u)   /* [9:0], [19:10] stored I / Q trims */
#define FM1_BT_C0C       (*(volatile uint32_t *)0x2FC0Cu)
#define FM1_BT_C10       (*(volatile uint32_t *)0x2FC10u)   /* bytes 0..2 stored trims */
#define FM1_BT_C14       (*(volatile uint32_t *)0x2FC14u)
#define FM1_BT_C18       (*(volatile uint32_t *)0x2FC18u)
#define FM1_BT_C1C       (*(volatile uint32_t *)0x2FC1Cu)
#define FM1_BT_C20       (*(volatile uint32_t *)0x2FC20u)
#define FM1_BT_C24       (*(volatile uint32_t *)0x2FC24u)
#define FM1_BT_C28       (*(volatile uint32_t *)0x2FC28u)
#define FM1_BT_C40       (*(volatile uint32_t *)0x2FC40u)   /* BT block enable / config [I] */
#define FM1_BT_C48       (*(volatile uint32_t *)0x2FC48u)
#define FM1_BT_C78       (*(volatile uint32_t *)0x2FC78u)
#define FM1_BT_C7C       (*(volatile uint32_t *)0x2FC7Cu)
#define FM1_BT_BLE_EN    (*(volatile uint32_t *)0x2FC80u)   /* bit0 BLE block enable [I] */
#define FM1_BT_BB_BASE   (*(volatile uint32_t *)0x2FC84u)   /* baseband RAM base (an SRAM address) */
#define FM1_BT_PLL_TBL   (*(volatile uint32_t *)0x2FC88u)   /* the PLL channel table (an SRAM address) */
#define FM1_BT_C98       (*(volatile uint32_t *)0x2FC98u)
#define FM1_BT_C9C       (*(volatile uint32_t *)0x2FC9Cu)
#define FM1_BT_CA0       (*(volatile uint32_t *)0x2FCA0u)
#define FM1_BT_BB_END    (*(volatile uint32_t *)0x2FCBCu)   /* baseband RAM end = base + size */
#define FM1_BT_AGC0      (*(volatile uint32_t *)0x2FD80u)
#define FM1_BT_AGC(i)    (*(volatile uint32_t *)(0x2FD84u + 4u * (i)))   /* 0x2FD84 .. 0x2FD94 */
#define FM1_BT_BRCON     (*(volatile uint32_t *)0x20000u)   /* BR/EDR baseband control (HW §5.1 step 4) */
#define FM1_WL_BTEN      (*(volatile uint32_t *)0x14000u)   /* BT / RF enable (HW §5.1 step 3) */
#define FM1_CLK_CON1_BT  (*(volatile uint32_t *)0x10010u)   /* bits 14-15: BT domain power / reset (HW §5.1) */
#define FM1_RNG_LO       (*(volatile uint32_t *)0x13B00u)   /* random source (HW §1) */
#define FM1_RNG_HI       (*(volatile uint32_t *)0x13B04u)

enum { FM1_IRQ_BLE_RX = 29, FM1_IRQ_BLE_EVENT = 45 };       /* HW §10 */

/* Values the fact sheet does not settle, named so nobody mistakes them for measurements. */
#define FM1_BLE_STEP_DELAY_US 100u      /* HW §5.1 step 3: "a 240-unit delay" and "delays between steps", unit and
                                         * length unknown [M:s]; 100 us is a guess, generous for a power switch */
#define FM1_BLE_T34_VALUE 0x0A01u       /* HW §5.5 step 3: stock V15 0x0A01, demo_ble 0x0901; [15:8] unknown (U5) */

FM1_INLINE void fm1_ble_sync(void) { __asm__ volatile("csync" ::: "memory"); }

/* the column port (HW §2.1 0x2801C/20, §2.3): data first, csync, then the command */
FM1_INLINE void fm1_ble_col_wr(uint32_t link, uint32_t col, uint32_t data)
{
    FM1_BLE_COLDATA = data & 0xFFFFu;
    fm1_ble_sync();
    FM1_BLE_COLCMD = col << 10 | link << 4 | 5u;
}
FM1_INLINE uint32_t fm1_ble_col_rd(uint32_t link, uint32_t col)   /* HW §2.1 0x28024: op 2 */
{
    FM1_BLE_COLCMD = col << 10 | link << 4 | 2u;
    fm1_ble_sync();
    return FM1_BLE_COLRES & 0xFFFFu;
}

/* the 24-bit running slot count of link n (HW §2.3: column 0 low 16 bits, column 14 [7:0] bits 23:16) */
static uint32_t fm1_ble_clock(uint32_t link)
{
    uint32_t hi = fm1_ble_col_rd(link, 14) & 0xFFu, lo = fm1_ble_col_rd(link, 0), hi2 = fm1_ble_col_rd(link, 14) & 0xFFu;
    if (hi2 != hi)                          /* the low half wrapped between the reads */
        lo = fm1_ble_col_rd(link, 0);
    return hi2 << 16 | lo;
}

/* stop link n: column 14 = 0 (HW §2.3), its interrupts off (HW §5.5 end), then wait until the engine is idle (HW §2.1
 * 0x28038 bit1 [M:s]) for at most max_us, and acknowledge what was raised meanwhile (the window that was open when the
 * link stopped ends with its event interrupt: none is left pending for the next link) -> the microseconds it waited
 * (>= max_us: still busy when the wait gave up; console 'blell' busy_*, stop_*). The FM-1 (blell-dev3 / dev4,
 * 2026-10-09): bit1 still set 2,000 polls (~0.75 ms) after a scanning link stopped, every time.
 * The wait is bounded twice: by TIMER4 (max_us), and by a count of reads that no CPU clock gets through in max_us
 * (FM1_BLE_STOP_POLLS_PER_US: one read of 0x28038 and of TIMER4 per pass, each a bus access), so a TIMER4 that does
 * not count (stopped, or its source gated) cannot hold the caller: boot, the main loop or a BLE interrupt. */
#define FM1_BLE_STOP_POLLS_PER_US 64u
static uint32_t fm1_ble_link_stop(uint32_t link, uint32_t max_us)
{
    uint32_t t0, d = 0, span = max_us * FM1_TICKS_PER_US, polls = max_us * FM1_BLE_STOP_POLLS_PER_US + 1u;
    fm1_ble_col_wr(link, 14, 0);
    FM1_BLE_IEN &= ~(0x101u << link);                       /* HW §5.5: enables cleared when a link is opened */
    FM1_BLE_G2EN &= ~(0x101u << link);
    FM1_BLE_IACK = 0x101u << link;                         /* HW §2.1: write 1 to acknowledge */
    t0 = fm1_ticks();
    while ((FM1_BLE_STAT & 2u) && (d = fm1_ticks() - t0) < span && --polls)
        ;
    if (!polls)
        d = span;                                          /* (the reads ran out first: counted as a timeout) */
    FM1_BLE_IACK = 0x101u << link;                         /* (raised while the engine finished) */
    return d / FM1_TICKS_PER_US;
}

/* link n's interrupts on, in stock's order (HW §6 step 11 [M:t]) */
static void fm1_ble_link_irqs_on(uint32_t link)
{
    FM1_BLE_IACK = 0x001u << link;
    FM1_BLE_IACK = 0x101u << link;
    FM1_BLE_G2EN = 0x100u << link;
    FM1_BLE_IEN = FM1_BLE_IEN | 0x001u << link;
    FM1_BLE_IEN = FM1_BLE_IEN | 0x101u << link;
    FM1_BLE_G2EN = 0x101u << link;
}

/* acknowledge and test the pending bits (HW §8: serviced when enable AND pending) */
FM1_INLINE int fm1_ble_rx_pending(uint32_t link) { return (FM1_BLE_IEN & FM1_BLE_IPND & (0x100u << link)) != 0; }
FM1_INLINE void fm1_ble_rx_ack(uint32_t link) { FM1_BLE_IACK = 0x100u << link; }      /* HW §8 IRQ 29 step 1 */
FM1_INLINE int fm1_ble_event_pending(uint32_t link) { return (FM1_BLE_IEN & FM1_BLE_IPND & (1u << link)) != 0; }
FM1_INLINE void fm1_ble_event_ack(uint32_t link) { FM1_BLE_IACK = 1u << link; }       /* HW §8 IRQ 45 step 1 */
/* the end of the event ISR (HW §8 IRQ 45 steps 4, 5): the second group and the 0x28038 flag */
static void fm1_ble_event_tail(uint32_t link)
{
    if (FM1_BLE_G2EN & FM1_BLE_G2PND & (1u << link))
        FM1_BLE_G2EN |= 0x100u << link;                     /* acknowledge; its "link timeout" is ours (ble_ll.c) */
    if (FM1_BLE_STAT & 0x80u)
        FM1_BLE_STAT = 0x40u;
}

/* the interrupt and status registers as they read now, for the console (HW §2.1): enables, pending, the second group's
 * enables and pending, 0x28038 */
static void fm1_ble_irq_regs(uint32_t r[5])
{
    r[0] = FM1_BLE_IEN;
    r[1] = FM1_BLE_IPND;
    r[2] = FM1_BLE_G2EN;
    r[3] = FM1_BLE_G2PND;
    r[4] = FM1_BLE_STAT;
}

FM1_INLINE uint32_t fm1_ble_rng32(void) { return FM1_RNG_LO ^ (FM1_RNG_HI * 0x9E3779B9u); }   /* HW §1 */

/* ---- radio bring-up (HW §5.1 - §5.4, §16). Unverified on hardware in every step. */

/* HW §2.4: 81 entries x 3 words, one per 1 MHz channel; stock V15's form {i | i << 8, 0, 0} [M:d], meaning [I] */
static uint32_t fm1_ble_pll_tbl[81 * 3] __attribute__((section(".pool"), aligned(4)));

#define FM1_BT_AGC_IDX   (*(volatile uint32_t *)0x2FD98u)   /* HW §2.2: the AGC table's write index (0) */
#define FM1_BT_AGC_DATA  (*(volatile uint32_t *)0x2FD9Cu)   /* HW §2.2: its data port, 128 words */

/* The radio in stock's second-boot order (HW §16.1), with the stored trims (the data of VM 106, 107, 108, 187:
 * src/ble/ble_vm.c found them in the VM or in Optimist's copy; without them the caller never gets here, §15.4):
 *   HW §16.1 groups 2-13   the Wi-Fi front end the BT RF init runs first (HW §5.2), from the captured program
 *                          (hal/fm1_ble_rf.h, build/gen/ble_rf_tables.h), the trim fields byte by byte (§16.4),
 *                          our own VCO scan (group 7);
 *   HW §5.1 steps 3, 4     the BT domain;
 *   HW §5.4 / §16.1 g. 14  the PLL channel table, the AGC table (captured) and configuration, the BT analog words.
 * The clock words of HW §16.1 group 1 (0x10010 bit 10 = our UART's clock, hal/fm1_uart.h; 0x10008 bit 3 = the
 * second core's start, hal/fm1_dual.h) are not the radio's: left as the firmware set them. */
static void fm1_ble_rf_init(const uint8_t *x106, const uint8_t *x107, const uint8_t *x108, const uint8_t *x187)
{
    uint32_t i;
    fm1_ble_rf_run(x106, x107, x108, x187);                /* HW §16.1 groups 2-13 */
    fm1_ble_rf_section(FM1_RF_SECT_BT);                    /* HW §16.1 group 14 */
    /* HW §5.1 step 3: BT domain power / reset, then the BT / RF enable */
    FM1_CLK_CON1_BT &= ~(3u << 14);                        /* bits 14-15 cleared [M:s] */
    fm1_delay_us(FM1_BLE_STEP_DELAY_US);
    FM1_WL_BTEN |= 1u << 7;                                /* -> 0x000C0081 in three steps [M:t] */
    fm1_delay_us(FM1_BLE_STEP_DELAY_US);
    FM1_WL_BTEN = (FM1_WL_BTEN & ~(0x1Fu << 16)) | 0x0Cu << 16;
    fm1_delay_us(FM1_BLE_STEP_DELAY_US);
    FM1_WL_BTEN |= 1u;
    fm1_delay_us(FM1_BLE_STEP_DELAY_US);
    /* HW §5.1 step 4 [M:t] */
    FM1_BT_C40 |= 1u << 10;
    FM1_BT_BRCON = 0xC0000000u;
    FM1_BT_C40 = 0xFFFFu;
    FM1_BT_C40 = 0xFCFDu;
    FM1_BT_C78 = 0x0Fu;
    FM1_BT_C78 = 0x3FFu;
    /* (VM 110, the carrier offset -> PLL_COMP: V15 writes no 110 and a hand-made one changed nothing in the
     * emulator, HW §14.5; U14) */
    /* HW §5.4: the PLL channel table, then the AGC */
    for (i = 0; i < 81u; i++) {
        fm1_ble_pll_tbl[3u * i] = i | i << 8;
        fm1_ble_pll_tbl[3u * i + 1u] = 0;
        fm1_ble_pll_tbl[3u * i + 2u] = 0;
    }
    FM1_BT_PLL_TBL = (uint32_t)(uintptr_t)fm1_ble_pll_tbl;
    FM1_BT_AGC_IDX = 0;                                    /* HW §2.2, §5.4: the AGC table (captured, §17) */
    for (i = 0; i < 128u; i++)
        FM1_BT_AGC_DATA = ble_rf_agc[i];
    FM1_BT_C20 = 0;                                        /* HW §5.4 AGC config [M:t] */
    FM1_BT_AGC0 = 0x000F0000u;
    FM1_BT_AGC(0) = 0x1872BF14u;
    FM1_BT_AGC(1) = 0x1872BF55u;
    FM1_BT_AGC(2) = 0x1872BF00u;
    FM1_BT_AGC(3) = 0;
    FM1_BT_AGC(4) = 0;
    FM1_BT_AGC0 = 0x000F0001u;
    FM1_BT_C7C |= 1u;
    FM1_BT_C48 = 0xB9u;                                    /* HW §2.2 (0xAD after the BR/EDR init, not done here) */
    FM1_BT_C00 = 0x00FFD144u;                              /* HW §2.2, §5.4: 0x2FC00 - 0x2FC28 [M:t] */
    FM1_BT_C04 = 0x4E143CDFu;
    {   /* the BT TX IQ / DC trims: bt_rf_analog_init's base values, with our calibration the compensation from the
         * measured record (fm1_rf_cal.h, RF-CALIBRATION.md §5) */
        uint32_t c08 = 0x03100000u, c10 = 0x80008080u;
        if (fm1_rf_cal_on && fm1_rfc_trimmed)
            fm1_rf_cal_bt_trims(fm1_rfc_x187, &c08, &c10);
        FM1_BT_C08 = c08;
        FM1_BT_C0C = 0x80808080u;
        FM1_BT_C10 = c10;
    }
    FM1_BT_C14 = 0x80u;
    FM1_BT_C18 = 0;
    FM1_BT_C1C = 0;
    FM1_BT_C20 = 0x36555537u;
    FM1_BT_C24 = 0x00034041u;
    FM1_BT_C28 = 0x0005C000u;
    FM1_BT_C98 = 0x02360236u;                              /* HW §2.2: constants [M:s] */
    FM1_BT_C9C = 0x36360008u;
    FM1_BT_CA0 = 0x00003636u;
    /* HW §5.4 then initialises the BR/EDR baseband (0x2FC44, 0x2000C ... 0x20168) and the slot timer. Not done: a
     * BLE-only link layer probably needs neither (HW §10), but whether the engine needs the BT clock running is U12 */
    fm1_ble_rf_section(FM1_RF_SECT_DONE);
}

/* HW §5.5: the BLE baseband, steps 1-4 and 6-7 (step 5, the interrupts, is fm1_ble_irq_attach) */
static void fm1_ble_bb_init(uint32_t base, uint32_t size)
{
    FM1_BT_BLE_EN |= 1u;                                   /* step 1 */
    FM1_BLE_CON = 1u;                                      /* step 2 */
    FM1_BLE_T34 = FM1_BLE_T34_VALUE;                       /* step 3 */
    FM1_BT_BB_BASE = base;                                 /* step 4 (the block is zeroed by the caller) */
    FM1_BT_BB_END = base + size;
    FM1_BLE_CON |= 0x2000u;                                /* step 6 -> 0x6001 */
    FM1_BLE_CON |= 0x4000u;
    FM1_BLE_CON &= ~0x10u;
    FM1_BLE_T08 = (FM1_BLE_T08 & ~0x3FFu) | 70u;           /* step 7 */
    FM1_BLE_T08 |= 0xF000u;
    FM1_BLE_T0C = 70u;
    FM1_BLE_T10 = 62u | 62u << 8;
    FM1_BLE_T14 = 30u;
    FM1_BLE_T18 = 20u | 20u << 8;
    FM1_BLE_IEN = 0;                                       /* HW §5.5 end: enables cleared */
    FM1_BLE_G2EN = 0;
}

/* The interrupt entries (as fm1_isr.S), here so a build without BLE carries none of it. The C bodies are the
 * driver's (src/ble/ble_hw_wl82.c). */
void ble_wl82_event_irq(void);
void ble_wl82_rx_irq(void);
__asm__(".section .text.isr_ble_event,\"ax\",@progbits\n"
        "\t.globl isr_ble_event\n"
        "isr_ble_event:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall ble_wl82_event_irq\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
__asm__(".section .text.isr_ble_rx,\"ax\",@progbits\n"
        "\t.globl isr_ble_rx\n"
        "isr_ble_rx:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall ble_wl82_rx_irq\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
extern void isr_ble_event(void);
extern void isr_ble_rx(void);

/* the two BLE interrupts masked (hold 1) or let go (0) in the interrupt controller, their priority kept: the main loop
 * holds them while it changes the link layer's state (a pending one is taken when they are let go) */
static void fm1_ble_irqs_hold(int hold)
{
    static const uint8_t irq[2] = {FM1_IRQ_BLE_EVENT, FM1_IRQ_BLE_RX};
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        uint32_t bit = 1u << ((irq[i] & 7u) * 4u);
        if (hold)
            FM1_ICFG(irq[i]) &= ~bit;
        else
            FM1_ICFG(irq[i]) |= bit;
    }
}

/* HW §5.5 step 5, §10: stock registers both at priority 2 on CPU 0. IRQs off. */
static void fm1_ble_irq_attach(uint32_t prio)
{
    fm1_irq_attach(FM1_IRQ_BLE_EVENT, isr_ble_event, prio);
    fm1_irq_attach(FM1_IRQ_BLE_RX, isr_ble_rx, prio);
}
