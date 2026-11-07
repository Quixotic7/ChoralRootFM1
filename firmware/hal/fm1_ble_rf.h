/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* FM-1 (AC791N / WL82) radio start-up, the part before the BT block: the shared Wi-Fi front end that the BT RF
 * init runs first (docs/BLE-HW-FACTS.md §5.2, §16; branch feat/ble-facts d907ce3, "HW §n" below). Included by
 * hal/fm1_ble.h in a BLE build with the real driver.
 *
 * The long constant parts (§16.1 groups 2-6, 8-13: about 34,000 register words on a second boot) are a small program
 * in ble_rf_tables_v15.h, captured by tools/ble_rf_capture.py from the stock V15 firmware in the emulator and committed
 * (tools/build.py puts it, or a fresh capture, at build/gen/ble_rf_tables.h). This file runs it, in boot-2 order:
 *
 *   register write           §16.1 groups 2, 3, 5, 6, 8, 9, 12 (0x11900-0x11964, 0x14040-0x1405C, the MAC window);
 *                            bit 14 of 0x11900 kept as found (§16.2: a periodic routine's bit, not a trim)
 *   BBP window write / read  §16.3: entry address, data, commit 1 / address, commit 2, the read-back register;
 *                            direct BBP registers; the port 0x3101C (§5.2: bit 17 start, bit 16 read)
 *   RF-die LUT words         §5.2 SPI port 0x14028 / 0x1402C, the five-high five-low kick (§16.1 group 11)
 *   trim field               §14.5, §16.4: the next write takes a field from VM 106 / 107 / 108 / 187 (the bytes
 *                            given to fm1_ble_rf_run); a read-modify-write field goes into the last read-back
 *   delay                    the gaps of 20 us or more in stock's trace
 *   VCO scan                 §16.1 group 7: our own search (fm1_ble_vco_scan below), not stock's
 *   replay                   §16.1 group 10: the BBP reload, the same as the first load (group 4)
 *   skipped                  §16.2: the window-D read-back loop (DC / IQ [I]), TODO(hardware)
 *   section marker           where a §16.1 group starts (found by register pattern: ble_rf_capture.py sections()):
 *                            2 analog word, 3 radio config, 4 BBP first load, 5 crystal trim (VM 106), 6 analog init,
 *                            7 VCO scan, 8 post-scan set-up, 10 BBP second phase, 11 RF-die LUT, 13 VM 108; then
 *                            fm1_ble_rf_init marks 14 (the BT block, hal/fm1_ble.h) and 15 (done). The console's
 *                            'bletrim' prints the last one entered and the set: on a unit where the start-up stops,
 *                            the group it stopped in
 *
 * TODO(hardware), the unknowns in HW §12 and §16.2:
 *   - the window-D read-back loop (§16.2, 3,537 transactions in stock's boot 2): left out; its results depend on
 *     read-backs the emulator returns as 0 (U17). §18.3 step 3, the read-back build: write a window entry and read
 *     it back through 0xD3 (a read-back console command still to write, HW §18), then decide how to measure and
 *     write those entries;
 *   - the RF-die LUT words a stored trim changes (VM 107 -> entries 0xE0-0xFF of both words, VM 187 bytes 48-63, the
 *     scan -> word-1 entries 0x00-0x7F): written as captured (the emulator's calibration), their mapping unknown.
 *     §16.5 row 1: after the scan, read the LUT back (SPI command 0x6);
 *   - the VCO band and the final 0x11934-0x11940: the scan below finds an in-range band, but stock's final writes
 *     (constants of the capture) are kept; compare with the emulator's on hardware (§18.3 step 3);
 *   - the BT TX trims (0x2FC08, 0x2FC10: §16.2, live from BBP read-backs): hal/fm1_ble.h keeps stock's first values;
 *   - delays: stock's gaps in the emulator, not a datasheet's settle times. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_irq.h"
#include "fm1_time.h"
#include "ble_rf_tables.h"   /* build/gen: tools/build.py copies firmware/hal/ble_rf_tables_v15.h or a fresh capture there */

#if !defined(BLE_RF_TABLES_FORMAT) || BLE_RF_TABLES_FORMAT != 3
#error "build/gen/ble_rf_tables.h is from another tools/ble_rf_capture.py: capture it again or update ble_rf_tables_v15.h"
#endif

#define FM1_RF_REG(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define FM1_BBP_PORT      (*(volatile uint32_t *)0x3101Cu)   /* HW §5.2: bit17 start, bit16 read, [15:8] reg, [7:0] data */
#define FM1_RFSPI_CTL     (*(volatile uint32_t *)0x14028u)   /* HW §5.2: [22:20] busy, 0x10000 | address << 8 | command */
#define FM1_RFSPI_DATA    (*(volatile uint32_t *)0x1402Cu)
#define FM1_RFSPI_WAIT    (*(volatile uint32_t *)0x14034u)   /* written 0 while waiting (HW §5.2) */
#define FM1_WL_A00        0x11900u                          /* HW §16.2: bit 14 kept */
#define FM1_WL_VCO_EN     (*(volatile uint32_t *)0x11934u)   /* HW §16.1 group 7: bit 24 pulsed per step */
#define FM1_WL_VCO_BAND   (*(volatile uint32_t *)0x11938u)   /* [25:19] the band; bits 16, 18 pulsed per step */
#define FM1_WL_VCO_FINE   (*(volatile uint32_t *)0x1193Cu)
#define FM1_WL_CAL_START  (*(volatile uint32_t *)0x11968u)   /* bit 28: start a comparator measurement */
#define FM1_WL_CAL_STROBE (*(volatile uint32_t *)0x11978u)   /* strobe 1 x 8 then 0; read: bit 17 low, bit 18 high */

/* values the sheet does not settle, named so nobody mistakes them for measurements */
#define FM1_BBP_POLLS 1000u               /* the BBP start bit polled to 0 (stock reads it once; the bound is ours) */
#define FM1_RFSPI_POLLS 1000u             /* HW §5.2 busy [22:20]; the bound is ours */
#define FM1_VCO_BANDS 64u                 /* stock's scan used bands 0..63 (§16.1 group 7, 0x11938 [25:19]) */
#define FM1_VCO_EDGE_US 2u                /* between the step's pulse edges (stock: ~2 us in the emulator) [I] */
#define FM1_VCO_SETTLE_US 50u             /* before a measurement (stock: ~45 us in the emulator) [I] */

/* §16.1 groups run as one burst: decoded first (trim fields applied), then written by fm1_rf_burst_run from RAM
 * with the interrupts off, so nothing reads flash between the group's first and last write. Group 5, the 0x11930
 * ramp (1, 3, 7, 0xF, 0x1F, 0x5F, 0x25F, then the VM 106 trims): on hardware, rf_init hung in group 5 in exactly the
 * builds where a flash data-cache line of ble_rf_prog began right after the write of 3 (ble_rf_prog at 8 mod 32:
 * 1f0ab85, 3a153c3), and ran in the builds where the first line began after 0x5F (24 mod 32: 1cc6e04, 510e616), the
 * code identical. [I] the ramp's first steps leave the flash path (XIP) unusable until it is complete, so a flash
 * access there never returns and the watchdog resets. A group that holds anything but register writes and trim
 * fields, or more than FM1_RF_BURST_MAX writes, runs op by op as before (rf_burst 0 in 'bletrim'). */
#define FM1_RF_BURST_GROUPS (1u << 5)
#define FM1_RF_BURST_MAX 32u              /* group 5 has 16 writes (§16.1: 14 + 2) */

struct fm1_ble_rf_stat {                  /* what the start-up did, for the console (io/console.c 'bletrim') */
    uint32_t ops, bbp_timeouts, spi_timeouts, lut_words, trims, skipped, delay_us;
    uint32_t sections;                    /* bit g: §16.1 group g entered (section markers) */
    uint32_t scan_result;                 /* the last comparator word */
    uint32_t group_op;                    /* ops when the last group was entered */
    uint8_t ran, scan_found, scan_band, scan_steps, bad_op;
    uint8_t burst;                        /* writes of the last burst group (FM1_RF_BURST_GROUPS); 0: none ran */
    volatile uint8_t section;             /* the last group entered (0: not started) */
};
#define FM1_RF_SECT_BT   14u              /* the BT block (hal/fm1_ble.h fm1_ble_rf_init, HW §16.1 group 14) */
#define FM1_RF_SECT_DONE 15u

/* The BLE breadcrumb: kept in .noinit, so it survives a watchdog reset (not a power cycle). main.c moves it to
 * prev / prev_irqs at boot (fm1_ble_crumb_boot) and the console's 'dbg' prints both. now: bits [31:24] 0xB1 (the word
 * is valid), [23:16] the step of the BLUETOOTH ON path (FM1_BLE_STEP_*), [15:8] rf_ops / 256 at the last rf_init
 * group, [7:0] that group (§16.1). irqs: the BLE interrupts taken since that ON (a storm shows as a huge count).
 * op: rf_init's op count (rf_ops) when the last op began; gop: that op's index within its group (the group's marker
 * is 0; in a burst group, FM1_RF_BURST_GROUPS: op is the group's first op, gop the write, 1..rf_burst); addr: the last access begun, so a hang names it: a register's address, 0xBB0000 | BBP register << 8 | data
 * for a BBP transaction (port 0x3101C), 0x5B0000 | command << 8 | address for an RF-die SPI command (port 0x14028).
 * stop: the last link stop (hal/fm1_ble.h fm1_ble_link_stop through ble_hw_wl82.c hw_stop): 0x5D000000 | path << 16
 * (BDS_*) while it waits for the engine, 0x5E000000 | path << 16 | the microseconds it waited (at most 0xFFFF) once
 * done: a hang in that wait names it.
 * (New words go last: a reflash moves none of the older ones.) */
enum {
    FM1_BLE_STEP_NONE, FM1_BLE_STEP_SET_ON, FM1_BLE_STEP_STACK_INIT, FM1_BLE_STEP_RF_INIT, FM1_BLE_STEP_BB_INIT,
    FM1_BLE_STEP_STARTED, FM1_BLE_STEP_ENABLE, FM1_BLE_STEP_LINK_STOP, FM1_BLE_STEP_LINK_OPEN, FM1_BLE_STEP_ADV_PROG,
    FM1_BLE_STEP_ADV_STARTED, FM1_BLE_STEP_IRQS_ON, FM1_BLE_STEP_RUNNING, FM1_BLE_STEP_SET_OFF
};
static volatile struct {
    uint32_t now, irqs, prev, prev_irqs;
    uint32_t op, gop, addr, prev_op, prev_gop, prev_addr;
    uint32_t stop, prev_stop;
} fm1_ble_bc __attribute__((section(".noinit.ble")));
#if defined(FELUCCA_DUAL) && FELUCCA_DUAL
/* hal/fm1_dual.h fm1_dual_snap: where the BLE start-up / link was when an exception came (2026-10-15: CPU1 faults only
 * in BLE builds) */
#define FM1_DUAL_SNAP_BLE 1
static void fm1_dual_snap_ble(fm1_c1_snap_t *s)
{
    s->ble[0] = fm1_ble_bc.now;
    s->ble[1] = fm1_ble_bc.irqs;
    s->ble[2] = fm1_ble_bc.op;
    s->ble[3] = fm1_ble_bc.gop;
    s->ble[4] = fm1_ble_bc.addr;
}
#endif
#define fm1_ble_crumb fm1_ble_bc.now
#define fm1_ble_crumb_irqs fm1_ble_bc.irqs
static uint32_t fm1_ble_step_at[16];         /* TIMER4 / 24000 (ms) when each step was first reached (0: not yet; the
                                              * console's boot: against CPU1's start, 2026-10-15) */
FM1_INLINE void fm1_ble_step(uint32_t step)
{
    fm1_ble_crumb = 0xB1000000u | (step & 0xFFu) << 16 | (fm1_ble_crumb & 0xFFFFu);
    if (!fm1_ble_step_at[step & 15u])
        fm1_ble_step_at[step & 15u] = 1u + fm1_ticks() / (1000u * FM1_TICKS_PER_US);
}
static void fm1_ble_crumb_boot(void)       /* at boot (main.c): what the last run left, then from zero */
{
    fm1_ble_bc.prev = fm1_ble_bc.now >> 24 == 0xB1u ? fm1_ble_bc.now : 0u;   /* (a power cycle: RAM noise -> 0) */
    fm1_ble_bc.prev_irqs = fm1_ble_bc.prev ? fm1_ble_bc.irqs : 0u;
    fm1_ble_bc.prev_op = fm1_ble_bc.prev ? fm1_ble_bc.op : 0u;
    fm1_ble_bc.prev_gop = fm1_ble_bc.prev ? fm1_ble_bc.gop : 0u;
    fm1_ble_bc.prev_addr = fm1_ble_bc.prev ? fm1_ble_bc.addr : 0u;
    fm1_ble_bc.prev_stop = fm1_ble_bc.prev ? fm1_ble_bc.stop : 0u;
    fm1_ble_bc.now = fm1_ble_bc.irqs = 0;
    fm1_ble_bc.op = fm1_ble_bc.gop = fm1_ble_bc.addr = fm1_ble_bc.stop = 0;
}

static struct fm1_ble_rf_stat fm1_ble_rf_stat;
static void fm1_ble_rf_section(uint32_t g)
{
    fm1_ble_rf_stat.section = (uint8_t)g;
    fm1_ble_rf_stat.group_op = fm1_ble_rf_stat.ops;
    fm1_ble_crumb = (fm1_ble_crumb & 0xFFFF0000u) | (fm1_ble_rf_stat.ops >> 8 & 0xFFu) << 8 | (g & 0xFFu);
    fm1_ble_rf_stat.sections |= 1u << (g & 31u);
}

static uint32_t fm1_bbp(uint32_t reg, uint32_t data, uint32_t rd)   /* one BBP transaction -> the port's [7:0] */
{
    uint32_t c = BLE_RF_BBP_FLAGS | rd << 16 | reg << 8 | data, i;
    fm1_ble_bc.addr = 0xBB0000u | (reg & 0xFFu) << 8 | (data & 0xFFu);
    FM1_BBP_PORT = c;
    FM1_BBP_PORT = c | 1u << 17;                            /* HW §5.2: the command, then with the start bit */
    for (i = 0; i < FM1_BBP_POLLS && (FM1_BBP_PORT & 1u << 17); i++)
        ;
    if (i == FM1_BBP_POLLS)
        fm1_ble_rf_stat.bbp_timeouts++;
    return FM1_BBP_PORT & 0xFFu;
}

/* the BBP windows (HW §16.3): window w's commit, address, data, read-back = 0xC8 + 4w .. 0xCB + 4w */
static void fm1_bbp_win_wr(uint32_t w, uint32_t entry, uint32_t v)
{
    fm1_bbp(0xC9u + 4u * w, entry, 0);
    fm1_bbp(0xCAu + 4u * w, v & 0xFFu, 0);
    fm1_bbp(0xC8u + 4u * w, 1, 0);
}
static uint32_t fm1_bbp_win_rd(uint32_t w, uint32_t entry)   /* [I] that it returns the entry: U17 */
{
    fm1_bbp(0xC9u + 4u * w, entry, 0);
    fm1_bbp(0xC8u + 4u * w, 2, 0);
    return fm1_bbp(0xCBu + 4u * w, 0, 1);
}

static void fm1_rfspi(uint32_t addr, uint32_t cmd, uint32_t data)   /* HW §5.2: one RF-die SPI command */
{
    uint32_t i;
    fm1_ble_bc.addr = 0x5B0000u | (cmd & 0xFu) << 8 | (addr & 0xFFu);
    for (i = 0; i < FM1_RFSPI_POLLS && (FM1_RFSPI_CTL >> 20 & 7u); i++)
        FM1_RFSPI_WAIT = 0;
    if (i == FM1_RFSPI_POLLS)
        fm1_ble_rf_stat.spi_timeouts++;
    FM1_RFSPI_DATA = data;
    FM1_RFSPI_CTL = 0x10000u | (addr & 0xFFu) << 8 | (cmd & 0xFu);
    for (i = 0; i < 5u; i++)                                /* the kick: bit 4 five times high, five times low */
        FM1_RFSPI_CTL |= 0x10u;
    for (i = 0; i < 5u; i++)
        FM1_RFSPI_CTL &= ~0x10u;
}

/* Our own calibration (fm1_rf_cal.h, JieLi's, Apache-2.0): with fm1_rf_cal_on set before fm1_ble_rf_run, the replay
 * measures this unit's radio instead of taking a stored record 187: the stock VCO scan at group 7's scan op, the trim
 * before the first write that carries a 187 field (after group 8's PLL set-up), then every 187 field, the RF-die
 * table's banks (word 1, entries 0-127) and RX DC (word 0, 128-223) from what it measured. The x187 given to
 * fm1_ble_rf_run is ignored then. docs/bt/RF-CALIBRATION.md. */
#include "fm1_rf_cal.h"
#ifndef FM1_RF_CAL_MEMORY
#define FM1_RF_CAL_MEMORY                                   /* the firmware: the zeroed pool (its data RAM is full) */
#endif
static uint8_t fm1_rf_cal_on;                               /* set by the firmware: measure (1), or the stored 187 (0) */
static struct fm1_rf_cal fm1_rfc FM1_RF_CAL_MEMORY;         /* what was measured (the log reads it) */
static uint8_t fm1_rfc_x187[68] FM1_RF_CAL_MEMORY;          /* the record built from it */
static uint8_t fm1_rfc_scanned, fm1_rfc_trimmed;
static int fm1_rfc_scan_rc, fm1_rfc_trim_rc;

/* the rest of the replay with measured values: a 187 field's write gets the record's true fields (the captured ones
 * are 1-2-bit slices found by perturbation) */
static int fm1_rfc_is187(const uint8_t *trim, uint32_t nt)
{
    uint32_t i;
    for (i = 0; i < nt; i++)
        if ((ble_rf_fields[4u * trim[i]] & 3u) == 3u)
            return 1;
    return 0;
}
static void fm1_rfc_before_187(void)                        /* the first 187 field: measure now */
{
    if (fm1_rf_cal_on && !fm1_rfc_trimmed) {
        fm1_rfc_trimmed = 1;
        fm1_rfc_trim_rc = fm1_rf_cal_trim(&fm1_rfc, fm1_rfc_x187);
    }
}
static uint32_t fm1_rfc_lut(uint32_t entry, uint32_t cmd, uint32_t w)   /* an RF-die word, patched */
{
    if (!fm1_rf_cal_on)
        return w;
    if (cmd == 0xDu && entry < 128u && fm1_rfc_scanned)
        return fm1_rf_cal_rfd_w1(&fm1_rfc, entry, w);
    if (cmd == 0xEu && entry >= 128u && entry < 224u && fm1_rfc_trimmed)
        return fm1_rf_cal_rfd_w0(entry, w, fm1_rfc_x187);
    return w;
}

/* §16.1 group 7, our own search (not stock's): with the fine code stock starts from, find a band whose comparator
 * reads neither low (bit 17) nor high (bit 18): bisection over FM1_VCO_BANDS, a higher band when low. The step:
 * the band into 0x11938 [25:19], 0x11938 bits 16 / 18 and 0x11934 bit 24 pulsed low then high, 0x11968 bit 28,
 * eight strobes of 0x11978 then 0, the comparator in 0x11978 (the registers and the order as stock's boot-2 trace
 * shows them: BLE-STACK.md §8.1). Its result is reported only: stock's final band and fine code follow as constants
 * (TODO(hardware) above). */
static void fm1_ble_vco_scan(uint32_t fine0)
{
    uint32_t lo = 0, hi = FM1_VCO_BANDS - 1u, b, r, i;
    FM1_WL_VCO_FINE = fine0;
    fm1_ble_rf_stat.scan_found = 0;
    fm1_ble_rf_stat.scan_steps = 0;
    while (lo <= hi) {
        b = (lo + hi) / 2u;
        FM1_WL_VCO_BAND = (FM1_WL_VCO_BAND & ~(0x7Fu << 19)) | b << 19;
        FM1_WL_VCO_BAND &= ~(1u << 16);
        FM1_WL_VCO_BAND &= ~(1u << 18);
        FM1_WL_VCO_EN &= ~(1u << 24);
        fm1_delay_us(FM1_VCO_EDGE_US);
        FM1_WL_VCO_BAND |= 1u << 16;
        fm1_delay_us(FM1_VCO_EDGE_US);
        FM1_WL_VCO_EN |= 1u << 24;
        FM1_WL_VCO_BAND |= 1u << 18;
        fm1_delay_us(FM1_VCO_SETTLE_US);
        FM1_WL_CAL_START |= 1u << 28;
        for (i = 0; i < 8u; i++)
            FM1_WL_CAL_STROBE = 1;
        FM1_WL_CAL_STROBE = 0;
        r = FM1_WL_CAL_STROBE;
        fm1_ble_rf_stat.scan_steps++;
        fm1_ble_rf_stat.scan_result = r;
        fm1_ble_rf_stat.scan_band = (uint8_t)b;
        if (r & 1u << 17)
            lo = b + 1u;                                    /* low: a higher band */
        else if (r & 1u << 18) {
            if (!b)
                break;
            hi = b - 1u;                                    /* high: a lower band */
        } else {
            fm1_ble_rf_stat.scan_found = 1;
            break;
        }
    }
}

static uint32_t fm1_rf_field(uint32_t v, uint32_t f, const uint8_t *const rec[4])   /* §14.5 / §16.4 */
{
    const uint8_t *d = &ble_rf_fields[4u * f];
    uint32_t x = rec[d[0] & 3u][d[1]], s = d[2], w = d[3] & 0x7Fu, m;
    if (d[3] & 0x80u)                                       /* VM 108: a non-zero byte replaces the default */
        return x ? x : v;
    m = ((1u << w) - 1u) << s;
    return (v & ~m) | (x << s & m);
}

static uint32_t fm1_rf_u32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the burst's writes, from RAM: no flash fetch, no flash data (a and v are on the caller's stack, fm1_ble_bc is RAM)
 * between the first write and the last. tools/build.py checks it links into .ram_text. */
__attribute__((section(".ram_text"), noinline, used))
static void fm1_rf_burst_run(const uint32_t *a, const uint32_t *v, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        fm1_ble_bc.gop = i + 1u;
        fm1_ble_bc.addr = a[i];
        FM1_RF_REG(a[i]) = v[i];
    }
}

/* a group of FM1_RF_BURST_GROUPS, from its first op at pc: decode its register writes and trim fields up to the next
 * section marker, then write them in one burst with the interrupts off (icfg bit 9 is the global enable that
 * cli / sti clear and set: the emulator's model [I]). -> the pc after the group, or pc itself when the group holds
 * anything else (the caller then runs it op by op) */
static uint32_t fm1_ble_rf_burst(uint32_t pc, uint32_t end, const uint8_t *const rec[4])
{
    const uint8_t *p = ble_rf_prog;
    uint32_t a[FM1_RF_BURST_MAX], v[FM1_RF_BURST_MAX], n = 0, ops = 0, trims = 0, q = pc, i, ie;
    void (*volatile run)(const uint32_t *, const uint32_t *, uint32_t) = fm1_rf_burst_run;   /* (XIP -> RAM: a
                                                         * direct call does not reach, hal/fm1_clock.h fm1_clk_far) */
    uint8_t trim[8], nt = 0;
    while (q < end && p[q] != 0x90u) {
        if (p[q] < 0x80u) {
            if (n == FM1_RF_BURST_MAX || q + 5u > end || ble_rf_addr[p[q]] == FM1_WL_A00)
                return pc;                                  /* (0x11900 needs a read: not in a burst) */
            a[n] = ble_rf_addr[p[q]];
            v[n] = fm1_rf_u32(p + q + 1);
            for (i = 0; i < nt; i++)
                v[n] = fm1_rf_field(v[n], trim[i], rec);
            n++;
            nt = 0;
            q += 5;
        } else if (p[q] == 0x8Bu && q + 3u <= end && nt < sizeof trim && p[q + 1] < BLE_RF_NFIELDS) {
            trim[nt++] = p[q + 1];                          /* (a read-back field: only window writes use it) */
            trims++;
            q += 3;
        } else
            return pc;
        ops++;
    }
    if (nt)
        return pc;                                          /* a trim field for an op outside the group */
    fm1_ble_bc.op = fm1_ble_rf_stat.ops + 1u;
    ie = fm1_icfg() & 0x200u;
    fm1_irq_off();
    run(a, v, n);
    if (ie)
        fm1_irq_on();
    fm1_ble_rf_stat.ops += ops;
    fm1_ble_rf_stat.trims += trims;
    fm1_ble_rf_stat.burst = (uint8_t)n;
    return q;
}

/* one op more for the console and the breadcrumb (each write of a packed op counts as the op it replaced) */
static void fm1_ble_rf_tick(void)
{
    fm1_ble_rf_stat.ops++;
    fm1_ble_bc.op = fm1_ble_rf_stat.ops;
    fm1_ble_bc.gop = fm1_ble_rf_stat.ops - fm1_ble_rf_stat.group_op;
}

/* the packed ops (capture format 3, tools/ble_rf_pack.py), write for write the format-2 ops they replace:
 * OP_LUT2 0x91: cmd A, cmd B, first entry, count - 1, nA, nB, the deltas (4 bytes each), then count pairs of delta
 * indices (A's word, then B's, for each entry; a word is its command's last + the delta, from 0) -> the bytes used */
static uint32_t fm1_ble_rf_lut2(const uint8_t *p)
{
    uint32_t n = p[4] + 1u, na = p[5], wa = 0, wb = 0, i;
    const uint8_t *da = p + 7, *db = da + 4u * na, *ix = db + 4u * p[6];
    for (i = 0; i < n; i++) {
        if (i)
            fm1_ble_rf_tick();
        wa += fm1_rf_u32(da + 4u * ix[2u * i]);
        fm1_rfspi(p[3] + i, p[1], fm1_rfc_lut(p[3] + i, p[1], wa));
        fm1_ble_rf_tick();
        wb += fm1_rf_u32(db + 4u * ix[2u * i + 1u]);
        fm1_rfspi(p[3] + i, p[2], fm1_rfc_lut(p[3] + i, p[2], wb));
    }
    fm1_ble_rf_stat.lut_words += 2u * n;
    return (uint32_t)(ix + 2u * n - p);
}

/* OP_WWG 0x92: window, n, n entries, records r, then r x n values: the window writes of the n entries, r times */
static uint32_t fm1_ble_rf_wwg(const uint8_t *p)
{
    uint32_t n = p[2], r = p[3u + n], k, j;
    const uint8_t *v = p + 4u + n;
    for (k = 0; k < r; k++)
        for (j = 0; j < n; j++) {
            if (k | j)
                fm1_ble_rf_tick();
            fm1_bbp_win_wr(p[1] & 3u, p[3u + j], *v++);
        }
    return (uint32_t)(v - p);
}

/* run ble_rf_prog[pc, end) (one level of replay); rec = the data of VM 106, 107, 108, 187 */
static void fm1_ble_rf_exec(uint32_t pc, uint32_t end, const uint8_t *const rec[4], int depth)
{
    const uint8_t *p = ble_rf_prog;
    uint8_t trim[8], rmw = 0, nt = 0;
    uint32_t last = 0, v, a, i, n, resume_pc = 0, resume_end = 0;
    while (pc < end || resume_end) {
        if (pc >= end) {
            pc = resume_pc; end = resume_end; resume_end = 0; depth = 0;
            last = 0; rmw = nt = 0;
        }
        uint32_t op = p[pc];
        fm1_ble_rf_tick();
        if (op < 0x80u) {                                   /* a register write */
            a = ble_rf_addr[op];
            v = fm1_rf_u32(p + pc + 1);
            fm1_ble_bc.addr = a;
            if (a == FM1_WL_A00)
                v = (v & ~(1u << 14)) | (FM1_RF_REG(a) & 1u << 14);
            if (fm1_rf_cal_on && fm1_rfc_is187(trim, nt)) {
                fm1_rfc_before_187();
                for (i = 0; i < nt; i++)
                    if ((ble_rf_fields[4u * trim[i]] & 3u) != 3u)
                        v = fm1_rf_field(v, trim[i], rec);
                v = fm1_rf_cal_reg(a, v, fm1_rfc_x187);
            } else
                for (i = 0; i < nt; i++)
                    v = fm1_rf_field(v, trim[i], rec);
            if (fm1_rf_cal_on && fm1_rfc_scanned && a == (uint32_t)(uintptr_t)&FM1_WL_VCO_BAND)
                v = (v & ~(0x7Fu << 19)) | (uint32_t)fm1_rfc.bank121 << 19;   /* our scan's bank, not the capture's */
            FM1_RF_REG(a) = v;
            nt = rmw = 0;
            pc += 5;
        } else if ((op & 0xFCu) == 0x80u) {                 /* a window write */
            v = rmw ? last : p[pc + 2];
            if (fm1_rf_cal_on && fm1_rfc_is187(trim, nt)) {
                fm1_rfc_before_187();
                for (i = 0; i < nt; i++)
                    if ((ble_rf_fields[4u * trim[i]] & 3u) != 3u)
                        v = fm1_rf_field(v, trim[i], rec);
                v = fm1_rf_cal_win(op & 3u, p[pc + 1], v, fm1_rfc_x187);
            } else
                for (i = 0; i < nt; i++)
                    v = fm1_rf_field(v, trim[i], rec);
            fm1_bbp_win_wr(op & 3u, p[pc + 1], v);
            nt = rmw = 0;
            pc += 3;
        } else if ((op & 0xFCu) == 0x84u) {                 /* a window read */
            last = fm1_bbp_win_rd(op & 3u, p[pc + 1]);
            pc += 2;
        } else if (op == 0x88u) {
            fm1_bbp(p[pc + 1], p[pc + 2], 0);
            pc += 3;
        } else if (op == 0x89u) {
            last = fm1_bbp(p[pc + 1], 0, 1);
            pc += 2;
        } else if (op == 0x8Au) {                           /* LUT words: command, first entry, count, words */
            n = p[pc + 3];
            for (i = 0; i < n; i++)
                fm1_rfspi(p[pc + 2] + i, p[pc + 1], fm1_rfc_lut(p[pc + 2] + i, p[pc + 1], fm1_rf_u32(p + pc + 4 + 4u * i)));
            fm1_ble_rf_stat.lut_words += n;
            pc += 4u + 4u * n;
        } else if (op == 0x8Bu) {                           /* a trim field for the next write */
            if (nt < sizeof trim && p[pc + 1] < BLE_RF_NFIELDS)
                trim[nt++] = p[pc + 1];
            else
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            rmw |= p[pc + 2] & 1u;
            fm1_ble_rf_stat.trims++;
            pc += 3;
        } else if (op == 0x8Cu) {
            n = p[pc + 1] | p[pc + 2] << 8;
            fm1_delay_us(n);
            fm1_ble_rf_stat.delay_us += n;
            pc += 3;
        } else if (op == 0x8Du) {
            if (fm1_rf_cal_on) {                            /* the stock scan (fm1_rf_cal.h) */
                fm1_rfc_scan_rc = fm1_rf_cal_scan(&fm1_rfc);
                fm1_rfc_scanned = 1;
            } else
                fm1_ble_vco_scan(fm1_rf_u32(p + pc + 1));
            pc += 5;
        } else if (op == 0x8Eu) {
            a = p[pc + 1] | p[pc + 2] << 8;
            n = p[pc + 3] | p[pc + 4] << 8;
            if (!depth && a + n <= sizeof ble_rf_prog) {
                resume_pc = pc + 5u; resume_end = end;
                pc = a; end = a + n; depth = 1;
                last = 0; rmw = nt = 0;
                continue;
            }
            fm1_ble_rf_stat.bad_op = (uint8_t)op;
            pc += 5;
        } else if (op == 0x8Fu) {
            fm1_ble_rf_stat.skipped += p[pc + 1] | p[pc + 2] << 8;   /* TODO(hardware): the read-back loop */
            pc += 3;
        } else if (op == 0x91u) {                           /* the LUT words of two commands (packed) */
            pc += fm1_ble_rf_lut2(p + pc);
        } else if (op == 0x92u) {                           /* window writes repeating n entries (packed) */
            pc += fm1_ble_rf_wwg(p + pc);
            nt = rmw = 0;
        } else if (op == 0x90u) {                           /* a section marker (§16.1 group) */
            fm1_ble_rf_section(p[pc + 1]);
            pc += 2;
            if (p[pc - 1] < 32u && (FM1_RF_BURST_GROUPS >> p[pc - 1] & 1u))
                pc = fm1_ble_rf_burst(pc, end, rec);        /* (unchanged: run op by op below) */
        } else {                                            /* 0xFF: the end (anything else: stop there) */
            if (op != 0xFFu) fm1_ble_rf_stat.bad_op = (uint8_t)op;
            if (resume_end) { pc = end; continue; }
            return;
        }
    }
}

/* HW §16.1 groups 2-13 with the stored trims (the data of VM 106, 107, 108, 187, in that order) */
static void fm1_ble_rf_run(const uint8_t *x106, const uint8_t *x107, const uint8_t *x108, const uint8_t *x187)
{
    const uint8_t *const rec[4] = {x106, x107, x108, x187};
    fm1_ble_rf_exec(0, sizeof ble_rf_prog, rec, 0);
    if (fm1_rf_cal_on && fm1_rfc_trimmed)
        fm1_rfc_cal_att(0);                                 /* the attenuators as JieLi's trim leaves them (its last
                                                             * rx_tx_cal_attenuation(0)); the replay's words are a
                                                             * start without a trim (measured against the SDK build) */
    fm1_ble_rf_stat.ran = 1;
}
