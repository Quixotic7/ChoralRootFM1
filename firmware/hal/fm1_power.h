/* SPDX-License-Identifier: GPL-3.0-only
 * The FM-1's supply rails, ported from Melodee's PR 21 "fix(dual-core): raise the core supply and survive CPU1
 * misreads, faults and stalls" (Kerem Kilic / Melodee, GPL-3.0, github.com/keremimo/melodee: hal/fm1_power.h), the
 * register layout after JieLi's AC79 SDK p33.h (Apache-2.0). ChoralRoot's use of it: docs/DUALCORE.md "Reliability". */
/* The chip runs at ~360 MHz, above JieLi's 320 MHz table, on the boot loader's SYSVDD 1.26 V with the 1.4 V rail on
 * its LDO; JieLi's own settings above 320 MHz are DVDD 1.35 V / DCDC14 1.45 V. With both cores running, some units'
 * SRAM reads go wrong at the boot loader's levels (Melodee's crash reports). VDC14 is the rail that matters
 * (zednaked's table, ChoralRoot#9 / fm1-x0x#21: 11/3 .. 13/3 fault in ~6 s, 11/4 and 14/4 marginal, 11/5 and 15/5 clean
 * for 5 and 15 minutes; fc79: 14/4 still crashed after 30+ minutes); fm1-x0x 1.0.7 ships VDC14 5 (1.50 V), the AC791N
 * datasheet allows 1.55 V on this rail and the stock M-VAVE firmware runs it at 1.60 V (7). So with Dual Core On:
 *
 *   fm1_power_record()       boot: the levels the boot loader left (boot[] = now[]), P33 reads only
 *   fm1_power_init()         boot, IRQs off, before core 1 starts: VDC14 to FM1_VDC14_LEVEL, then SYSVDD to
 *                            FM1_SYSVDD_LEVEL, one step at a time; never lowers a rail
 *   fm1_power_raise(rail, l) one rail up to l (capped at 14 / 5), a step every 100 us (IRQs off: P33 is not
 *                            reentrant); never lowers
 *   fm1_power_step(rail, l)  one step towards l, up only: 1 = it stepped
 *   fm1_power_get(rail)      the level in the register; fm1_power.now[] what we left (no P33 access)
 *
 * Levels (P33 analog controls, SDK p33.h): SYSVDD (P3_ANA_CON9 [3:0], the core and SRAM rail, isd_config's "DVDD")
 * 0.93 V + 30 mV a step: 11 1.26 V (the boot loader's), 14 1.35 V (our maximum). VDC14 (P3_ANA_CON6
 * [2:0], "DCDC14") 1.25 V + 50 mV a step: 3 1.40 V (the boot loader's), 5 1.50 V (our maximum). VDDIO (P3_ANA_CON5 [2:0])
 * 2.8 V + 100 mV a step: read only here.
 * The host tests define FM1_POWER_MOCK and their own fm1_p33_read / fm1_p33_write / fm1_delay_us. */
#pragma once
#include <stdint.h>
#ifndef FM1_POWER_MOCK
#include "fm1_sys.h"
#include "fm1_time.h"
#endif

enum { FM1_RAIL_SYSVDD, FM1_RAIL_VDC14, FM1_RAIL_VDDIO, FM1_RAILS };
static const uint8_t fm1_rail_reg[FM1_RAILS] = {0x09, 0x06, 0x05};   /* P3_ANA_CON9, 6, 5 */
static const uint8_t fm1_rail_max[FM1_RAILS] = {15, 7, 7};           /* the field masks [3:0], [2:0], [2:0] */

#ifndef FM1_SYSVDD_LEVEL
#define FM1_SYSVDD_LEVEL 14u            /* 1.35 V: JieLi's DVDD above 320 MHz */
#endif
#ifndef FM1_VDC14_LEVEL
#define FM1_VDC14_LEVEL 5u              /* 1.50 V: the clean level in the reports (JieLi's 1.45 V above 320 MHz was not) */
#endif
/* hard maxima (device safety, after Charles Vestal's review of Melodee PR 21): SYSVDD at JieLi's own level for this
 * clock; VDC14 at 1.50 V, the level the reports found clean (above), under the datasheet's 1.55 V and the stock
 * firmware's 1.60 V. No rail is ever set above them, whatever asks (fm1_power_step, fm1_power_goto) */
#define FM1_SYSVDD_MAX 14u
#define FM1_VDC14_MAX 5u
static const uint8_t fm1_rail_cap[FM1_RAILS] = {FM1_SYSVDD_MAX, FM1_VDC14_MAX, 0};   /* (VDDIO: never written) */

static struct { uint8_t boot[FM1_RAILS], now[FM1_RAILS], recorded; } fm1_power;

static uint32_t fm1_power_get(uint32_t rail)
{
    return fm1_p33_read(fm1_rail_reg[rail]) & fm1_rail_max[rail];
}

static void fm1_power_record(void)
{
    uint32_t i;
    if (fm1_power.recorded)
        return;
    for (i = 0; i < FM1_RAILS; i++)
        fm1_power.boot[i] = fm1_power.now[i] = (uint8_t)fm1_power_get(i);
    fm1_power.recorded = 1;
}

/* one step of one level towards `level`, upwards only (read-modify-write of the field): 1 = it stepped */
static int fm1_power_step(uint32_t rail, uint32_t level)
{
    uint32_t reg = fm1_rail_reg[rail], mask = fm1_rail_max[rail], now = fm1_power_get(rail);
    if (level > fm1_rail_cap[rail])
        level = fm1_rail_cap[rail];
    if (now >= level) {
        fm1_power.now[rail] = (uint8_t)now;
        return 0;
    }
    now++;
    fm1_p33_write(reg, (uint8_t)((fm1_p33_read(reg) & ~mask) | now));
    fm1_power.now[rail] = (uint8_t)now;
    return 1;
}

/* up to `level`, a step every 100 us: the regulators slew without a current surge */
static void fm1_power_raise(uint32_t rail, uint32_t level)
{
    while (fm1_power_step(rail, level))
        fm1_delay_us(100);
}

static void fm1_power_init(void)
{
    fm1_power_record();
    fm1_power_raise(FM1_RAIL_VDC14, FM1_VDC14_LEVEL);     /* the 1.4 V rail first: SYSVDD's LDO hangs off it */
    fm1_power_raise(FM1_RAIL_SYSVDD, FM1_SYSVDD_LEVEL);
    fm1_delay_us(1000);
}

/* The stock supply setup, for a boot that starts the radio (Bluetooth On: firmware/src/cr_ble.c, HISTORY 3y240).
 * After Melodee 5e66df3 "fix(power): match stock V15 supply startup" (Kerem Kilic, GPL-3.0-only,
 * github.com/keremimo/melodee: hal/fm1_power.h, BUILDING.md "The stock supply setup"): the supply part of stock V15's
 * power_init (FM-1_015, app 0x02004F90..0x0200509A), which runs the 2.4 GHz radio on DCDC13 with VDC14 at 1.60 V. Melodee
 * 1.6.1, whose Bluetooth works on these units with both cores (3y235), applies it at every boot; ChoralRoot's ladder
 * above leaves the 1.3 V rail on its LDO and VDC14 at 1.50 V, and 3y239's unit crashed when it started the radio with
 * both cores on it. The sequence: LDO13 on, DCDC13 on, LDO13 off (stock's 1,000-pass waits), the analog words 1, 5, 6, 7,
 * 8, SYSVDD drive, VDDIOM 3.2 V, VDC14, VDDIOW 3.2 V, stock's reads 6, 5, 5, RC_250k_EN, analog keep, VDDOK_DIS,
 * register 0x17 bit 5 cleared, SYSVDD last. VDC14 and SYSVDD are stock's 7 / 15 only with FM1_POWER_STOCK_LEVELS 1;
 * by default ChoralRoot's caps hold (FM1_VDC14_MAX 5, FM1_SYSVDD_MAX 14: Charles Vestal's review) and the rest is
 * stock's. Never lowers a rail below what it found. IRQs off, core 1 not started. 1 = done, 0 = a P33 transfer timed
 * out (the sequence stops there) */
#ifndef FM1_POWER_STOCK_LEVELS
#define FM1_POWER_STOCK_LEVELS 0
#endif
#ifndef FM1_POWER_MOCK
static inline __attribute__((always_inline)) void fm1_power_settle(void)   /* stock's delay(1000): a loop, not 1000 us */
{
    int32_t n = -1000;
    __asm__ volatile("goto 2f\n1:\n\tnop\n\t%0 += 1\n2:\n\tif (%0 != 0) goto 1b" : "+r"(n) :: "memory");
}
#endif
static int fm1_power_field(uint32_t reg, uint32_t start, uint32_t width, uint32_t value)
{
    uint32_t t = fm1_p33_timeouts, mask = ((1u << width) - 1u) << start;
    uint8_t old = fm1_p33_read(reg);
    if (fm1_p33_timeouts != t)
        return 0;
    fm1_p33_write(reg, (uint8_t)((old & ~mask) | ((value << start) & mask)));
    return fm1_p33_timeouts == t;
}
static int fm1_power_stock(void)
{
    uint32_t t = fm1_p33_timeouts, vdc14, sysvdd;
    fm1_power_record();
    vdc14 = FM1_POWER_STOCK_LEVELS ? 7u : FM1_VDC14_MAX;
    sysvdd = FM1_POWER_STOCK_LEVELS ? 15u : FM1_SYSVDD_MAX;
    if (vdc14 < fm1_power.boot[FM1_RAIL_VDC14])
        vdc14 = fm1_power.boot[FM1_RAIL_VDC14];
    if (sysvdd < fm1_power.boot[FM1_RAIL_SYSVDD])
        sysvdd = fm1_power.boot[FM1_RAIL_SYSVDD];
#define FM1_PS_STEP(op) do { op; if (fm1_p33_timeouts != t) return 0; } while (0)
#define FM1_PS_FIELD(r, s, w, v) do { if (!fm1_power_field(r, s, w, v)) return 0; } while (0)
    FM1_PS_FIELD(0x00u, 2, 1, 1);                  /* LDO13 on during the handover */
    fm1_power_settle();
    FM1_PS_FIELD(0x00u, 3, 1, 1);                  /* DCDC13 on */
    fm1_power_settle();
    FM1_PS_FIELD(0x00u, 2, 1, 0);                  /* LDO13 off */
    fm1_power_settle();
    FM1_PS_STEP(fm1_p33_write(0x01u, 0x20));
    FM1_PS_STEP(fm1_p33_write(0x05u, 0x40));
    FM1_PS_STEP(fm1_p33_write(0x06u, (uint8_t)(0x08u | (fm1_power.boot[FM1_RAIL_VDC14] > 4u ?
                                                       fm1_power.boot[FM1_RAIL_VDC14] : 4u))));   /* stock 0x0C: never
                                                                    * below the level found (a soft reset's) */
    FM1_PS_STEP(fm1_p33_write(0x07u, 0xA5));
    FM1_PS_STEP(fm1_p33_write(0x08u, 0x96));
    FM1_PS_FIELD(0x09u, 4, 1, 1);                  /* SYSVDD drive */
    FM1_PS_FIELD(0x05u, 0, 3, 4);                  /* VDDIOM 3.2 V */
    FM1_PS_FIELD(0x06u, 0, 3, vdc14);              /* VDC14: stock 7 (1.60 V), capped 5 (1.50 V) */
    FM1_PS_FIELD(0x05u, 4, 2, 3);                  /* VDDIOW 3.2 V */
    FM1_PS_STEP((void)fm1_p33_read(0x06u));        /* (stock's reads, kept in place) */
    FM1_PS_STEP((void)fm1_p33_read(0x05u));
    FM1_PS_STEP((void)fm1_p33_read(0x05u));
    FM1_PS_STEP(fm1_p33_or(0x00u, 0x80));          /* RC_250k_EN */
    FM1_PS_FIELD(0x16u, 0, 6, 0);                  /* analog keep */
    FM1_PS_STEP(fm1_p33_or(0x02u, 0x40));          /* VDDOK_DIS */
    FM1_PS_STEP(fm1_p33_and(0x17u, 0xDF));
    FM1_PS_FIELD(0x09u, 0, 4, sysvdd);             /* SYSVDD last: stock 15 (1.38 V), capped 14 (1.35 V) */
#undef FM1_PS_FIELD
#undef FM1_PS_STEP
    fm1_power.now[FM1_RAIL_VDC14] = (uint8_t)vdc14;
    fm1_power.now[FM1_RAIL_SYSVDD] = (uint8_t)sysvdd;
    fm1_delay_us(1000);
    return 1;
}

/* test builds only (tools/core1_fault_test.py): a rail to a level up to the cap, down too, a step every 100 us; SYSVDD never
 * below 6 (1.11 V, the lowest a healthy unit ran both cores at). 0 = done */
static int fm1_power_goto(uint32_t rail, uint32_t level)
{
    uint32_t reg, mask, now;
    if (rail > FM1_RAIL_VDC14 || (rail == FM1_RAIL_SYSVDD && level < 6u) || level > fm1_rail_cap[rail])
        return 1;
    reg = fm1_rail_reg[rail];
    mask = fm1_rail_max[rail];
    now = fm1_power_get(rail);
    while (now != level) {
        now += now < level ? 1u : (uint32_t)-1;
        fm1_p33_write(reg, (uint8_t)((fm1_p33_read(reg) & ~mask) | now));
        fm1_delay_us(100);
    }
    fm1_power.now[rail] = (uint8_t)now;
    return 0;
}
