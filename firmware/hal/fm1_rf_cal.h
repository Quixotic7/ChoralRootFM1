/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2024-2026 Zhuhai Jieli Technology Co., Ltd.
 * Copyright (C) 2026 Charles Vestal */
/* FM-1 (AC791N / WL82): the 2.4 GHz radio's own calibration, run at every boot before the BLE radio starts, so a
 * unit with no stored calibration (no SDK VM, no record 187) still gets a working radio. It follows JieLi's
 * calibration in wl_rf_common.a (wf_rf_cal.c, wf_rf_init.c, wf_phy_mac_lib.c) from the AC79 SDK,
 * https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK (cpu/wl82/liba/), Apache-2.0.
 *
 * Changed from the original (Apache-2.0 section 4(b)): rewritten in C for felucca-bsp from the library's code, with
 * timeouts added on every hardware wait and the Wi-Fi-only steps left out (RX IQ, the 187 save, the prints).
 *
 * The spec is docs/bt/RF-CALIBRATION.md ("§n" below). Included by fm1_ble.h after fm1_ble_rf.h, whose helpers it uses:
 * fm1_bbp (one BBP port transaction), fm1_bbp_win_wr / _rd (the windows), FM1_RF_REG, fm1_delay_us, fm1_ticks.
 *
 * What the replay (fm1_ble_rf.h) calls, in boot order:
 *   fm1_rf_cal_scan     where its VCO scan op is (§16.1 group 7): the VCO bank scan as wfa_pll_bank_scan(121)
 *                       does it (§3.2). Keeps the bank table for the RF-die patch (§6).
 *   fm1_rf_cal_trim     after group 8's PLL set-up for 2442 MHz, before the first write that carries a 187 field:
 *                       wf_rf_trim without RX IQ (§4), then the 187 record built from what it measured (§9 item 5).
 *   fm1_rf_cal_reg      instead of the captured 187 fields: a register write with the record's fields put in at
 *   fm1_rf_cal_win      their true widths (quick_start_wifi_trim's writes, §4.5); the same for a window write.
 *   fm1_rf_cal_rfd_w0   the RF-die table: word 0 of entries 128-223 (RX DC, §6) and word 1 of entries 0-127
 *   fm1_rf_cal_rfd_w1   (the VCO bank of each entry's frequency code, §6, through fm1_rf_cal_bank).
 *   fm1_rf_cal_bt_trims the BT modem's TX IQ / DC compensation from the record (§5).
 *
 * Where the library's code and the spec differ, the code wins (all checked against the IR, 2026-10-26):
 *   - the BBP port (§3.5): the library's bbp_set polls bit 17 before its two writes, and bbp_rd does not poll at all;
 *     fm1_bbp polls after each command. The words written and their order are the same, so fm1_bbp is used; it only
 *     never starts a command while one is running.
 *   - wf_rx_tune_test (§4.1b): bit 0 of 0x11968 is not cleared with [9:1]; the codes are written into [9:1] with the
 *     mask 0x3FE. The result is a second wlrf_get_(1) after the done bit, not the word that showed it.
 *   - the midbank search (§3.2 step 2) sets fbds 73 once, before the first mid.
 *   - get_wfa_pll_bank (§3.2 step 4) also compares limit[47] (= top) for i = 47.
 *   - the RF-die word-0 patch (§6) puts in rx_dc_tune[g][1] << 22 unmasked (16 bits shifted: only its low 10 stay).
 *
 * Library behaviour we take as bugs, and what this file does:
 *   - get_wfa_pll_midbank: when no mid shows a change, it returns the last comparator result | 15, which is 15 (the
 *     scan then walks banks 15 down to 225 & 127). Here: top FM1_RF_CAL_TOP_FALLBACK (63), and the scan returns
 *     FM1_RF_CAL_E_MIDBANK after finishing.
 *   - txiq_tune_soft steps 5 and 6 (FM1_RF_CAL_FULL only): the best gain offset (-423..423) and phase (-200..200)
 *     are kept in a signed byte, so one beyond +-127 wraps (and the wrapped gain offset picks the wrong side). Here
 *     they keep their full value. The TX DC sweeps keep the library's byte: they only visit -128..127.
 *   - the library's waits (the RC tune engine, the estimator) have no timeout: here each is bounded by TIMER4 and
 *     counted; a step that times out stops, puts back its neutral values and the trim returns its code.
 *
 * Interrupts: the SDK runs all of this with interrupts on (§8); so does this file. Nothing else may drive the radio
 * meanwhile (the trim transmits into its own receiver). */
#pragma once
#include <stdint.h>

#ifndef FM1_RF_CAL_FULL
#define FM1_RF_CAL_FULL 0              /* 1: also TX DC, gain and phase (§4.2 steps 4-6, ~2,000 more reads) */
#endif
#ifndef FM1_RF_CAL_TUNE_US
#define FM1_RF_CAL_TUNE_US 5000u       /* the RC tune engine, a run (§4.1b; "well under a millisecond" [L]) */
#endif
#ifndef FM1_RF_CAL_DEM_US
#define FM1_RF_CAL_DEM_US 5000u        /* the estimator of one metric read (§4.6; its length unknown, R2) */
#endif
#define FM1_RF_CAL_TOP_FALLBACK 63u    /* the scan's top when no mid bank shows a change (see the bugs above) */

enum {
    FM1_RF_CAL_OK = 0,
    FM1_RF_CAL_E_MIDBANK = -1,         /* the scan found no mid bank; it ran from FM1_RF_CAL_TOP_FALLBACK */
    FM1_RF_CAL_E_FILTER = -2,          /* the RC tune engine never finished: the filter registers left as found */
    FM1_RF_CAL_E_TXLO = -3,            /* an estimator read timed out in the TX LO search: txlo 64 / 64 */
    FM1_RF_CAL_E_TXIQ = -4,            /* the same in TX DC / gain / phase (FM1_RF_CAL_FULL): neutral values */
    FM1_RF_CAL_E_RXDC = -5             /* the same in RX DC: the rest of the steps 0 (0x200) */
};

struct fm1_rf_cal {
    /* the scan (§3.2) */
    uint8_t limit[48];                 /* wfa_pll_bank_limit: [i] the fbds where bank top - i starts; [47] = top */
    uint8_t top;                       /* mid | 15 (our unit: 0x3F) */
    uint8_t mid;                       /* the mid bank found (` 2394 bank: 37`); 0 when none */
    uint8_t mid_found;
    uint8_t bank121;                   /* the bank set with fbds 121 (`setbak:33`) */
    /* the trim (§4) */
    uint8_t flt[3];                    /* the RC tune codes for 6, 10, 20 MHz (§4.1b) */
    uint8_t txlo_i, txlo_q;            /* the TX LO search's best (§4.2 step 3) */
    uint8_t abort;                     /* (internal: an estimator timeout stops the running step) */
    uint16_t rx_dc[4][2];              /* per gain step, I then Q: bit 9 = non-negative, [8:0] magnitude (§4.3) */
    int32_t tx[6];                     /* what is in window D: I gain, I phase, Q gain, Q phase, I DC, Q DC */
    /* microseconds per step (TIMER4) */
    uint32_t us_scan, us_filter, us_txlo, us_txiq, us_rxdc, us_total;
    /* failures */
    uint32_t to_filter, to_dem;        /* RC tune engine / estimator timeouts */
    uint32_t bbp_timeouts;             /* fm1_bbp's timeouts during the trim */
    uint32_t dem_reads;                /* estimator reads made by the trim */
    int32_t err;                       /* the trim's first error (FM1_RF_CAL_E_*) */
};

/* ---- small register helpers ---- */

static void fm1_rfc_rmw(uint32_t a, uint32_t mask, uint32_t v)
{
    FM1_RF_REG(a) = (FM1_RF_REG(a) & ~mask) | (v & mask);
}
static void fm1_rfc_bit(uint32_t a, uint32_t bit, uint32_t on)
{
    fm1_rfc_rmw(a, 1u << bit, (on & 1u) << bit);
}
static uint32_t fm1_rfc_us_since(uint32_t t0) { return (fm1_ticks() - t0) / FM1_TICKS_PER_US; }

/* the windows by the library's names (§3.5, HW §16.3) */
#define FM1_RFC_C  0u                  /* "cca" */
#define FM1_RFC_D  2u                  /* "tx_backend" */
#define FM1_RFC_DP 3u                  /* "rx_frontend" */
#define fm1_rfc_c_wr(e, v)  fm1_bbp_win_wr(FM1_RFC_C, (e), (v))
#define fm1_rfc_c_rd(e)     fm1_bbp_win_rd(FM1_RFC_C, (e))
#define fm1_rfc_d_wr(e, v)  fm1_bbp_win_wr(FM1_RFC_D, (e), (v))
#define fm1_rfc_d_rd(e)     fm1_bbp_win_rd(FM1_RFC_D, (e))
#define fm1_rfc_dp_wr(e, v) fm1_bbp_win_wr(FM1_RFC_DP, (e), (v))
#define fm1_rfc_dp_rd(e)    fm1_bbp_win_rd(FM1_RFC_DP, (e))

/* the record's fields, little-endian (§4.1) */
static uint32_t fm1_rfc_u32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void fm1_rfc_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static int32_t fm1_rfc_sext10(uint32_t v)   /* a 10-bit field as the library reads it back (ashr 22) */
{
    v &= 0x3FFu;
    return (v & 0x200u) ? (int32_t)v - 0x400 : (int32_t)v;
}

/* ---- the analog side (wf_rf_init.c) ---- */

/* wlrf_get_: the analog read port (§3.2): 0x11968 [31:28] = sel, 0x11978 written 1 eight times then 0, read back */
static uint32_t fm1_rfc_get(uint32_t sel)
{
    uint32_t i;
    FM1_RF_REG(0x11968u) = (FM1_RF_REG(0x11968u) & 0x0FFFFFFFu) | sel << 28;
    for (i = 0; i < 8u; i++)
        FM1_RF_REG(0x11978u) = 1;
    FM1_RF_REG(0x11978u) = 0;
    return FM1_RF_REG(0x11978u);
}

/* wf_rf_en_set (§3.6): a group of enables, each set to one of six flags, in the library's order.
 * Entry: (register - 0x11900) / 4 << 8 | bit << 3 | flag. Flags: A = receive | RX cal | TX cal | RX DC cal,
 * RX = receive, R = receive | RX cal | RX DC cal, T = transmit | RX cal | TX cal, TXC = TX cal, RXC = RX cal. */
enum { FM1_RFC_F_A, FM1_RFC_F_RX, FM1_RFC_F_R, FM1_RFC_F_T, FM1_RFC_F_TXC, FM1_RFC_F_RXC };
#define FM1_RFC_EN(off, bit, f) ((off) / 4u << 8 | (bit) << 3 | (f))
static const uint16_t fm1_rfc_en_tab[29] = {
    FM1_RFC_EN(0x00, 23, FM1_RFC_F_A),  FM1_RFC_EN(0x04, 15, FM1_RFC_F_RX), FM1_RFC_EN(0x0C, 4, FM1_RFC_F_R),
    FM1_RFC_EN(0x0C, 5, FM1_RFC_F_R),   FM1_RFC_EN(0x0C, 20, FM1_RFC_F_R),  FM1_RFC_EN(0x10, 12, FM1_RFC_F_A),
    FM1_RFC_EN(0x10, 27, FM1_RFC_F_A),  FM1_RFC_EN(0x14, 11, FM1_RFC_F_A),  FM1_RFC_EN(0x14, 18, FM1_RFC_F_A),
    FM1_RFC_EN(0x18, 0, FM1_RFC_F_A),   FM1_RFC_EN(0x18, 1, FM1_RFC_F_A),   FM1_RFC_EN(0x14, 26, FM1_RFC_F_A),
    FM1_RFC_EN(0x08, 6, FM1_RFC_F_T),   FM1_RFC_EN(0x18, 19, FM1_RFC_F_T),  FM1_RFC_EN(0x1C, 0, FM1_RFC_F_T),
    FM1_RFC_EN(0x1C, 4, FM1_RFC_F_T),   FM1_RFC_EN(0x20, 9, FM1_RFC_F_T),   FM1_RFC_EN(0x24, 5, FM1_RFC_F_T),
    FM1_RFC_EN(0x24, 7, FM1_RFC_F_T),   FM1_RFC_EN(0x24, 8, FM1_RFC_F_T),   FM1_RFC_EN(0x24, 27, FM1_RFC_F_T),
    FM1_RFC_EN(0x28, 12, FM1_RFC_F_T),  FM1_RFC_EN(0x28, 26, FM1_RFC_F_T),  FM1_RFC_EN(0x38, 26, FM1_RFC_F_R),
    FM1_RFC_EN(0x38, 27, FM1_RFC_F_T),  FM1_RFC_EN(0x10, 13, FM1_RFC_F_TXC), FM1_RFC_EN(0x2C, 14, FM1_RFC_F_TXC),
    FM1_RFC_EN(0x2C, 11, FM1_RFC_F_RXC), FM1_RFC_EN(0x08, 29, FM1_RFC_F_RXC),
};
/* mode: 0 receive (and any other value), 1 transmit, 2 TX cal, 3 RX cal, 4 RX DC cal, 5 none (synthesizer only) */
static void fm1_rfc_en_set(uint32_t mode)
{
    uint32_t rx = mode == 0 || mode > 5, tx = mode == 1, txc = mode == 2, rxc = mode == 3, rxdc = mode == 4, i, e;
    uint32_t f[6];
    f[FM1_RFC_F_A] = rxc | rx | txc | rxdc;
    f[FM1_RFC_F_RX] = rx;
    f[FM1_RFC_F_R] = rxc | rx | rxdc;
    f[FM1_RFC_F_T] = rxc | tx | txc;
    f[FM1_RFC_F_TXC] = txc;
    f[FM1_RFC_F_RXC] = rxc;
    for (i = 0; i < sizeof fm1_rfc_en_tab / sizeof fm1_rfc_en_tab[0]; i++) {
        e = fm1_rfc_en_tab[i];
        fm1_rfc_bit(0x11900u + 4u * (e >> 8), e >> 3 & 31u, f[e & 7u]);
    }
}

/* rx_flt_vga_gain (wf_rf_init.c): the RX filter / VGA gain steps, 4 bytes each:
 * byte 0 -> 0x11910 [24:22], byte 1 -> 0x11910 [26:25], byte 2 -> 0x11914 [17:15] (bits 0-2) and [14:12]
 * (bits 4-6), byte 3 -> 0x11914 [7:5] */
static const uint8_t fm1_rfc_vga[42][4] = {
    {1, 0, 0x10, 0}, {1, 0, 0x11, 0}, {1, 0, 0x12, 0}, {1, 0, 0x13, 0}, {1, 0, 0x14, 0}, {1, 0, 0x15, 0},
    {1, 0, 0x20, 1}, {1, 0, 0x21, 1}, {1, 0, 0x22, 1}, {1, 0, 0x23, 1}, {1, 0, 0x24, 1}, {1, 0, 0x25, 1},
    {1, 1, 0x20, 2}, {1, 1, 0x21, 2}, {1, 1, 0x22, 2}, {1, 1, 0x23, 2}, {1, 1, 0x24, 2}, {1, 1, 0x25, 2},
    {2, 1, 0x20, 3}, {2, 1, 0x21, 3}, {2, 1, 0x22, 3}, {2, 1, 0x23, 3}, {2, 1, 0x24, 3}, {2, 1, 0x25, 3},
    {2, 1, 0x40, 4}, {2, 1, 0x41, 4}, {2, 1, 0x42, 4}, {2, 1, 0x43, 4}, {2, 1, 0x44, 4}, {2, 1, 0x45, 4},
    {2, 2, 0x40, 5}, {2, 2, 0x41, 5}, {2, 2, 0x42, 5}, {2, 2, 0x43, 5}, {2, 2, 0x44, 5}, {2, 2, 0x45, 5},
    {4, 2, 0x40, 6}, {4, 2, 0x41, 6}, {4, 2, 0x42, 6}, {4, 2, 0x43, 6}, {4, 2, 0x44, 6}, {4, 2, 0x45, 6},
};
/* wfrf_flt_vga_gain: one bit or field at a time, as the library writes them */
static void fm1_rfc_vga_gain(uint32_t g)
{
    const uint8_t *v = fm1_rfc_vga[g < 42u ? g : 41u];
    fm1_rfc_bit(0x11910u, 22, v[0]);
    fm1_rfc_bit(0x11910u, 23, v[0] >> 1);
    fm1_rfc_bit(0x11910u, 24, v[0] >> 2);
    fm1_rfc_bit(0x11910u, 25, v[1]);
    fm1_rfc_bit(0x11910u, 26, v[1] >> 1);
    fm1_rfc_rmw(0x11914u, 7u << 15, (uint32_t)v[2] << 15);
    fm1_rfc_bit(0x11914u, 12, v[2] >> 4);
    fm1_rfc_bit(0x11914u, 13, v[2] >> 5);
    fm1_rfc_bit(0x11914u, 14, v[2] >> 6);
    fm1_rfc_rmw(0x11914u, 7u << 5, (uint32_t)v[3] << 5);
}

/* wfrf_hp_open / _close: 0x11914 [2:0] = 1, bits 3 and 4 set / all cleared */
static void fm1_rfc_hp_open(void)
{
    fm1_rfc_rmw(0x11914u, 7u, 1u);
    FM1_RF_REG(0x11914u) |= 1u << 3;
    FM1_RF_REG(0x11914u) |= 1u << 4;
}
static void fm1_rfc_hp_close(void)
{
    FM1_RF_REG(0x11914u) &= ~7u;
    FM1_RF_REG(0x11914u) &= ~(1u << 3);
    FM1_RF_REG(0x11914u) &= ~(1u << 4);
}

/* rx_tx_cal_attenuation (§3.6): the loop-back attenuators; t = 1 for the TX trim, 0 after it */
static void fm1_rfc_cal_att(uint32_t t)
{
    FM1_RF_REG(0x11920u) &= ~(1u << 20);
    FM1_RF_REG(0x11920u) |= 1u << 21;
    if (t == 1u) {
        FM1_RF_REG(0x11920u) &= ~(1u << 22);
        FM1_RF_REG(0x11920u) |= 1u << 23;
    } else {
        FM1_RF_REG(0x11920u) |= 1u << 22;
        FM1_RF_REG(0x11920u) &= ~(1u << 23);
    }
    fm1_rfc_rmw(0x11924u, 7u << 28, 5u << 28);
    FM1_RF_REG(0x11928u) |= 3u << 13;
    fm1_rfc_rmw(0x11928u, 3u << 27, (t == 1u ? 3u : 1u) << 27);
    fm1_rfc_rmw(0x11910u, 0xFu << 8, 9u << 8);
}

/* ---- the PLL and the VCO bank scan (wf_rf_cal.c, §3.2) ---- */

static void fm1_rfc_fbds(uint32_t fbds) { fm1_rfc_rmw(0x1193Cu, 0xFFu << 5, fbds << 5); }        /* set_pll_fbds */
static void fm1_rfc_cap(uint32_t cap) { fm1_rfc_rmw(0x11938u, 0x7Fu << 19, (cap & 0x7Fu) << 19); } /* set_pll_cap */

/* set_pll_rn_en: restart the loop, then 100 us to settle */
static void fm1_rfc_rn_en(void)
{
    FM1_RF_REG(0x11938u) &= ~(1u << 16);
    FM1_RF_REG(0x11938u) &= ~(1u << 18);
    FM1_RF_REG(0x11934u) &= ~(1u << 24);
    fm1_delay_us(5);
    FM1_RF_REG(0x11938u) |= 1u << 16;
    fm1_delay_us(5);
    FM1_RF_REG(0x11934u) |= 1u << 24;
    FM1_RF_REG(0x11938u) |= 1u << 18;
    fm1_delay_us(100);
}

/* get_pll_vc_res: the control-voltage detector, bit 18 -> 1, bit 17 -> 2 */
static uint32_t fm1_rfc_vc_res(void)
{
    uint32_t r = fm1_rfc_get(1);
    return (r >> 18 & 1u) | (r >> 16 & 2u);
}

static void fm1_rfc_vc_det(uint32_t on) { fm1_rfc_bit(0x11940u, 24, on); }   /* open_ / close_rf_pll_vc_det */

/* get_wfa_pll_midbank: at fbds 73, the first mid (7, 23, ..., 119) whose neighbours mid - 7 and mid + 8 read
 * otherwise than mid itself -> mid | 15 */
static uint32_t fm1_rfc_midbank(struct fm1_rf_cal *c)
{
    uint32_t mid, m, l, h;
    fm1_rfc_fbds(73);
    for (mid = 7; mid < 120u; mid += 16u) {
        fm1_rfc_cap(mid);
        fm1_rfc_rn_en();
        m = fm1_rfc_vc_res();
        fm1_rfc_cap(mid - 7u);
        fm1_rfc_rn_en();
        l = fm1_rfc_vc_res();
        fm1_rfc_cap(mid + 8u);
        fm1_rfc_rn_en();
        h = fm1_rfc_vc_res();
        if (h != m || l != m) {
            c->mid = (uint8_t)mid;
            c->mid_found = 1;
            return mid | 15u;
        }
    }
    c->mid = 0;                                             /* (the library: the last result | 15 = 15) */
    c->mid_found = 0;
    return FM1_RF_CAL_TOP_FALLBACK;
}

/* get_wfa_pll_bank: the bank for an fbds code from the scan's table */
static uint32_t fm1_rf_cal_bank(const struct fm1_rf_cal *c, uint32_t fbds)
{
    uint32_t i, f = fbds & 0xFFu;
    for (i = 1; i < 48u; i++)
        if (c->limit[i] > f)
            return (uint8_t)(c->limit[47] - (i - 1u));
    return (uint8_t)(c->limit[47] - 47u);
}

/* wfa_pll_bank_scan(121) (§3.2 steps 1-5): for each bank from top down, step fbds up from the last "below" (2)
 * until "above" (1) or past 171; the bank's limit is (the previous bank's "above" + this one's last "below" - 3) / 2.
 * Then fbds 121 with its bank. The comparator needs no wait (the loop restart has its delays), so
 * nothing here can time out. -> 0, or FM1_RF_CAL_E_MIDBANK (the scan still ran, from the fallback top) */
static int fm1_rf_cal_scan(struct fm1_rf_cal *c)
{
    uint32_t t0 = fm1_ticks(), top, prev_h = 73, lo = 73, cur_lo, h, f, r, i;
    fm1_rfc_vc_det(1);
    top = fm1_rfc_midbank(c);
    c->top = (uint8_t)top;
    c->limit[47] = (uint8_t)top;
    for (i = 0; i < 47u; i++) {
        fm1_rfc_cap((top - i) & 0xFFu);
        f = cur_lo = lo;
        for (;;) {
            fm1_rfc_fbds(f);
            fm1_rfc_rn_en();
            r = fm1_rfc_vc_res();
            if (r == 1u) {
                h = f;
                break;
            }
            if (r == 2u)
                cur_lo = f;
            if (f + 1u > 171u) {
                h = 171;                                    /* (after a 2 at 171, cur_lo is 171 too) */
                break;
            }
            f++;
        }
        c->limit[i] = (uint8_t)((prev_h + cur_lo - 3u) / 2u);
        prev_h = h;
        lo = cur_lo;
    }
    fm1_rfc_fbds(121);
    c->bank121 = (uint8_t)fm1_rf_cal_bank(c, 121);
    fm1_rfc_cap(c->bank121);
    fm1_rfc_vc_det(0);
    c->us_scan = fm1_rfc_us_since(t0);
    return c->mid_found ? FM1_RF_CAL_OK : FM1_RF_CAL_E_MIDBANK;
}

/* ---- the PHY (wf_phy_mac_lib.c, §3.5) ---- */

#define FM1_RFC_MAC_1004 0x31004u          /* MAC register 0x1004 (the MAC at 0x30000) */

static void fm1_rfc_phy_rst(void)          /* wf_phy_rst */
{
    FM1_RF_REG(FM1_RFC_MAC_1004) = 12;
    fm1_bbp(0x16, 0, 0);
    fm1_bbp(0x15, 0x31, 0);
    fm1_bbp(0x15, 0x30, 0);
    fm1_bbp(0x19, 0, 0);
}
static void fm1_rfc_loop_test(void)        /* wf_phy_loop_always_test: TX into RX */
{
    fm1_rfc_phy_rst();
    FM1_RF_REG(FM1_RFC_MAC_1004) = 12;
    fm1_bbp(0x19, 0x18, 0);
}
static void fm1_rfc_cca_for_trim(void)     /* confg_cca_for_trim */
{
    fm1_rfc_c_wr(0x40, fm1_rfc_c_rd(0x40) & 0x7Fu);
    fm1_rfc_c_wr(0x41, fm1_rfc_c_rd(0x41) & 0x7Fu);
    fm1_rfc_c_wr(0x28, 0);
    fm1_rfc_c_wr(0x27, 5);
    fm1_bbp(0x04, fm1_bbp(0x04, 0, 1) & ~0x18u, 0);
}

/* dem_filter_dc (§4.6): start the estimator, wait for it, read the result for sel:
 * 0, 3: I^2 + Q^2 (16-bit I at D' 0x81:0x82, Q at 0x84:0x85); 1: the 24-bit I mean (0x81-0x83); 2: the Q mean
 * (0x8A-0x8C); other: I^2 + Q^2 from 0x87:0x88 and 0x8A:0x8B. A timeout sets c->abort and returns 0. */
static int32_t fm1_rfc_dem(struct fm1_rf_cal *c, uint32_t sel)
{
    uint32_t t0, a, b, hi, mid;
    int32_t i, q;
    c->dem_reads++;
    fm1_rfc_dp_wr(0x15, 1);
    fm1_rfc_dp_wr(0x16, 1);
    fm1_rfc_dp_wr(0x15, 3);
    fm1_rfc_dp_wr(0x16, 3);
    t0 = fm1_ticks();
    while (fm1_rfc_dp_rd(0x8D) & 3u)
        if (fm1_ticks() - t0 > FM1_RF_CAL_DEM_US * FM1_TICKS_PER_US) {
            c->to_dem++;
            c->abort = 1;
            return 0;
        }
    if (sel == 1u || sel == 2u) {
        a = sel == 1u ? 0x81u : 0x8Au;
        hi = fm1_rfc_dp_rd(a);
        mid = fm1_rfc_dp_rd(a + 1u);
        b = fm1_rfc_dp_rd(a + 2u);
        return (int32_t)(hi << 24 | mid << 16 | b << 8) / 256;
    }
    a = sel == 0u || sel == 3u ? 0x81u : 0x87u;
    b = sel == 0u || sel == 3u ? 0x84u : 0x8Au;
    hi = fm1_rfc_dp_rd(a);
    i = (int16_t)(hi << 8 | fm1_rfc_dp_rd(a + 1u));
    hi = fm1_rfc_dp_rd(b);
    q = (int16_t)(hi << 8 | fm1_rfc_dp_rd(b + 1u));
    return (int32_t)((uint32_t)(i * i) + (uint32_t)(q * q));  /* (as the library: a full-scale pair wraps) */
}

/* rd_adc_peak: the ADC peak over 1 ms */
static uint32_t fm1_rfc_adc_peak(void)
{
    uint32_t p;
    fm1_rfc_dp_wr(0x17, 0x82);
    fm1_delay_us(1000);
    p = fm1_rfc_dp_rd(0x80);
    fm1_rfc_dp_wr(0x17, 2);
    return p;
}

/* adc_pp_tune(0): raise the VGA gain step until the peak (the second of two reads) is above 47, at most step 36 */
static uint32_t fm1_rfc_adc_pp_tune(void)
{
    uint32_t g = 0, p;
    fm1_rfc_vga_gain(0);
    fm1_rfc_adc_peak();
    p = fm1_rfc_adc_peak();
    while (p <= 47u && g <= 35u) {
        g++;
        fm1_rfc_vga_gain(g);
        fm1_rfc_adc_peak();
        p = fm1_rfc_adc_peak();
    }
    fm1_rfc_vga_gain(g);
    return g;
}

/* txlo_cali_set / txlo_calq_set: the TX LO leakage trims, 0x1191C [14:8] and 0x11920 [6:0] */
static void fm1_rfc_txlo_i(uint32_t v) { fm1_rfc_rmw(0x1191Cu, 0x7Fu << 8, (v & 0x7Fu) << 8); }
static void fm1_rfc_txlo_q(uint32_t v) { fm1_rfc_rmw(0x11920u, 0x7Fu, v & 0x7Fu); }

/* set_anl_rxdc_i / _q: the analog RX DC trims in 0x11908, I [15:7] with bit 16 = non-negative, Q [25:17] with
 * bit 26 -> the encoded value: v | 0x200 when v >= 0, else -v (the library does not mask it) */
static uint32_t fm1_rfc_anl_rxdc(uint32_t shift, uint32_t sbit, int32_t dc)
{
    int16_t d = (int16_t)dc;
    if (d >= 0) {
        fm1_rfc_rmw(0x11908u, 0x1FFu << shift, (uint32_t)d << shift);
        FM1_RF_REG(0x11908u) |= 1u << sbit;
        return (uint16_t)(d | 0x200);
    }
    fm1_rfc_rmw(0x11908u, 0x1FFu << shift, (uint32_t)(-d) << shift);
    FM1_RF_REG(0x11908u) &= ~(1u << sbit);
    return (uint16_t)-d;
}
#define fm1_rfc_anl_rxdc_i(dc) fm1_rfc_anl_rxdc(7, 16, (dc))
#define fm1_rfc_anl_rxdc_q(dc) fm1_rfc_anl_rxdc(17, 26, (dc))

/* set_dig_tx_i_gain .. set_dig_tx_q_dc: k = 0 I gain, 1 I phase, 2 Q gain, 3 Q phase, 4 I DC, 5 Q DC. The 10-bit
 * value's [9:2] into D 0x04 + k, its [1:0] into a pair of D 0x61 (k 0-3) or 0x62 (k 4-5), read-modify-write. */
static void fm1_rfc_tx_set(struct fm1_rf_cal *c, uint32_t k, int32_t v)
{
    uint32_t e = k < 4u ? 0x61u : 0x62u, s = 2u * (k & 3u), u = (uint32_t)v, r;
    fm1_rfc_d_wr(0x04u + k, u >> 2 & 0xFFu);
    r = fm1_rfc_d_rd(e);
    fm1_rfc_d_wr(e, (r & ~(3u << s)) | (u & 3u) << s);
    c->tx[k] = v;
}
enum { FM1_RFC_TX_IG, FM1_RFC_TX_IP, FM1_RFC_TX_QG, FM1_RFC_TX_QP, FM1_RFC_TX_IDC, FM1_RFC_TX_QDC };

/* ---- the trim (§4) ---- */

/* wf_rx_tune_test (§4.1b): the RC tuning engine three times (6, 10, 20 MHz), the codes into the filter registers.
 * A run that does not finish stops the step before any filter register is written. */
static int fm1_rfc_rx_tune(struct fm1_rf_cal *c)
{
    static const uint16_t code[3] = {0x1FC, 0x122, 0x0A0};
    uint32_t k, t0, r6, r10, r20;
    fm1_rfc_rmw(0x11904u, 0xFu << 8, 8u << 8);
    FM1_RF_REG(0x11950u) |= 1u << 27;
    for (k = 0; k < 3u; k++) {
        fm1_rfc_rmw(0x11968u, 0x3FEu, code[k]);
        fm1_rfc_rmw(0x11968u, 0x7Fu << 11, 15u << 11);
        FM1_RF_REG(0x11968u) &= ~(0x1Fu << 18);
        FM1_RF_REG(0x11968u) &= ~(1u << 23);
        FM1_RF_REG(0x11968u) |= 1u;                         /* start */
        t0 = fm1_ticks();
        while (!(fm1_rfc_get(1) & 0x20u))
            if (fm1_ticks() - t0 > FM1_RF_CAL_TUNE_US * FM1_TICKS_PER_US) {
                c->to_filter++;
                FM1_RF_REG(0x11968u) &= ~1u;
                FM1_RF_REG(0x11950u) &= ~(1u << 27);
                return FM1_RF_CAL_E_FILTER;
            }
        c->flt[k] = (uint8_t)(fm1_rfc_get(1) >> 8);
        FM1_RF_REG(0x11968u) &= ~1u;
    }
    r6 = c->flt[0];
    r10 = c->flt[1];
    r20 = c->flt[2];
    FM1_RF_REG(0x11950u) &= ~(1u << 27);
    fm1_rfc_rmw(0x11920u, 0xFFu << 10, r10 << 10);
    fm1_rfc_rmw(0x11910u, 0xFFu << 14, r10 << 14);
    FM1_RF_REG(0x1195Cu) |= 0xFFu;
    fm1_rfc_rmw(0x1195Cu, 0xFFu << 8, r6 << 8);
    fm1_rfc_rmw(0x1195Cu, 0xFFu << 16, r10 << 16);
    fm1_rfc_rmw(0x1195Cu, 0xFFu << 24, r20 << 24);
    FM1_RF_REG(0x11960u) |= 0xFFu;
    fm1_rfc_rmw(0x11960u, 0xFFu << 8, r6 << 8);
    fm1_rfc_rmw(0x11960u, 0xFFu << 16, r10 << 16);
    fm1_rfc_rmw(0x11960u, 0xFFu << 24, r20 << 24);
    return FM1_RF_CAL_OK;
}

/* the TX LO search (§4.2 step 3): best of 0..127 for one trim, starting from 64 at metric(0); only a strictly
 * smaller metric wins -> the best, or -1 on an estimator timeout */
static int32_t fm1_rfc_txlo_search(struct fm1_rf_cal *c, void (*set)(uint32_t))
{
    int32_t best, m;
    uint32_t i, b = 64;
    set(64);
    best = fm1_rfc_dem(c, 0);
    for (i = 0; i < 128u && !c->abort; i++) {
        set(i);
        m = fm1_rfc_dem(c, 0);
        if (m < best) {
            best = m;
            b = i;
        }
    }
    if (c->abort)
        return -1;
    set(b);
    return (int32_t)b;
}

#if FM1_RF_CAL_FULL
/* a TX search for FM1_RF_CAL_FULL (§4.2 steps 4-6): sweep x = lo..hi through put(c, x), metric(0), the best (from
 * best0 at the current setting) -> the best x, or INT32_MIN on a timeout */
static int32_t fm1_rfc_tx_search(struct fm1_rf_cal *c, int32_t lo, int32_t hi, int32_t b,
                                 void (*put)(struct fm1_rf_cal *, int32_t))
{
    int32_t best = fm1_rfc_dem(c, 0), m, x;
    for (x = lo; x <= hi && !c->abort; x++) {
        put(c, x);
        m = fm1_rfc_dem(c, 0);
        if (m < best) {
            best = m;
            b = x;
        }
    }
    return c->abort ? INT32_MIN : b;
}
/* the TX DC sweeps visit (int8)x, as the library's do (x -200..200: -128..127, some twice) */
static void fm1_rfc_put_idc(struct fm1_rf_cal *c, int32_t x) { fm1_rfc_tx_set(c, FM1_RFC_TX_IDC, (int8_t)x); }
static void fm1_rfc_put_qdc(struct fm1_rf_cal *c, int32_t x) { fm1_rfc_tx_set(c, FM1_RFC_TX_QDC, (int8_t)x); }
static void fm1_rfc_put_gain(struct fm1_rf_cal *c, int32_t x)   /* x < 0: I gain 1023 + x, else Q gain 1023 - x */
{
    fm1_rfc_tx_set(c, FM1_RFC_TX_IG, x < 0 ? 1023 + x : 1023);
    fm1_rfc_tx_set(c, FM1_RFC_TX_QG, x < 0 ? 1023 : 1023 - x);
}
static void fm1_rfc_put_iph(struct fm1_rf_cal *c, int32_t x) { fm1_rfc_tx_set(c, FM1_RFC_TX_IP, x); }
static void fm1_rfc_put_qph(struct fm1_rf_cal *c, int32_t x) { fm1_rfc_tx_set(c, FM1_RFC_TX_QP, x); }
#endif

static void fm1_rfc_tx_neutral(struct fm1_rf_cal *c)   /* gains 1023, phases and DCs 0 */
{
    fm1_rfc_tx_set(c, FM1_RFC_TX_IG, 1023);
    fm1_rfc_tx_set(c, FM1_RFC_TX_QG, 1023);
    fm1_rfc_tx_set(c, FM1_RFC_TX_IP, 0);
    fm1_rfc_tx_set(c, FM1_RFC_TX_QP, 0);
    fm1_rfc_tx_set(c, FM1_RFC_TX_IDC, 0);
    fm1_rfc_tx_set(c, FM1_RFC_TX_QDC, 0);
}

/* txiq_tune_soft (§4.2): the loop-back set-up, then the TX LO search (steps 1-3); with FM1_RF_CAL_FULL also TX DC,
 * gain balance and phase (steps 4-6) */
static int fm1_rfc_txiq_tune(struct fm1_rf_cal *c)
{
    uint32_t t0 = fm1_ticks();
    int32_t r;
    fm1_rfc_phy_rst();
    fm1_rfc_c_wr(0x15, 0);
    fm1_rfc_c_wr(0x14, 0);
    fm1_rfc_cca_for_trim();
    fm1_rfc_loop_test();
    fm1_rfc_d_wr(0x00, 3);                                  /* a test tone [M] */
    fm1_rfc_d_wr(0x5F, 16);
    fm1_rfc_d_wr(0x01, 20);
    fm1_rfc_d_wr(0x02, 0);
    fm1_rfc_d_wr(0x03, 0);
    fm1_rfc_dp_wr(0x00, 62);
    fm1_rfc_dp_wr(0x13, 30);
    fm1_rfc_dp_wr(0x14, 56);
    fm1_rfc_dp_wr(0x14, 52);
    fm1_rfc_tx_neutral(c);
    fm1_rfc_txlo_i(64);
    fm1_rfc_txlo_q(64);
    fm1_rfc_adc_pp_tune();
    c->abort = 0;
    r = fm1_rfc_txlo_search(c, fm1_rfc_txlo_i);
    if (r >= 0) {
        c->txlo_i = (uint8_t)r;
        r = fm1_rfc_txlo_search(c, fm1_rfc_txlo_q);
        c->txlo_q = (uint8_t)r;
    }
    c->us_txlo = fm1_rfc_us_since(t0);
    if (r < 0) {
        fm1_rfc_txlo_i(64);
        fm1_rfc_txlo_q(64);
        c->txlo_i = c->txlo_q = 64;
        return FM1_RF_CAL_E_TXLO;
    }
#if FM1_RF_CAL_FULL
    t0 = fm1_ticks();
    fm1_rfc_tx_set(c, FM1_RFC_TX_IDC, 0);                   /* step 4: TX DC */
    fm1_rfc_tx_set(c, FM1_RFC_TX_QDC, 0);
    fm1_rfc_adc_pp_tune();
    fm1_rfc_tx_set(c, FM1_RFC_TX_IDC, 0);
    r = fm1_rfc_tx_search(c, -200, 200, 0, fm1_rfc_put_idc);
    if (r != INT32_MIN) {
        fm1_rfc_tx_set(c, FM1_RFC_TX_IDC, (int8_t)r);
        fm1_rfc_tx_set(c, FM1_RFC_TX_QDC, 0);
        r = fm1_rfc_tx_search(c, -200, 200, 0, fm1_rfc_put_qdc);
    }
    if (r != INT32_MIN) {
        fm1_rfc_tx_set(c, FM1_RFC_TX_QDC, (int8_t)r);
        fm1_rfc_dp_wr(0x13, 60);                            /* step 5: gain balance, on the image [M] */
        fm1_rfc_tx_set(c, FM1_RFC_TX_IG, 1023);
        fm1_rfc_tx_set(c, FM1_RFC_TX_QG, 1023);
        r = fm1_rfc_tx_search(c, -423, 423, 0, fm1_rfc_put_gain);
    }
    if (r != INT32_MIN) {
        int32_t q = r >= 0;                                 /* the side the gain went to: phase on the same */
        fm1_rfc_put_gain(c, r);                             /* (the library: r cut to a signed byte) */
        fm1_rfc_tx_set(c, FM1_RFC_TX_IP, 0);                /* step 6: phase */
        fm1_rfc_tx_set(c, FM1_RFC_TX_QP, 0);
        r = fm1_rfc_tx_search(c, -200, 200, 0, q ? fm1_rfc_put_qph : fm1_rfc_put_iph);
        if (r != INT32_MIN)
            fm1_rfc_tx_set(c, q ? FM1_RFC_TX_QP : FM1_RFC_TX_IP, r);
    }
    c->us_txiq = fm1_rfc_us_since(t0);
    if (r == INT32_MIN) {
        fm1_rfc_tx_neutral(c);
        return FM1_RF_CAL_E_TXIQ;
    }
#endif
    return FM1_RF_CAL_OK;
}

/* rxdc_tune_soft (§4.3): for each of four front-end gain steps, a signed successive approximation of the analog RX
 * DC trims against the I and Q means: twelve rounds, step 256 halving down to 1; the guard is checked before the
 * step, so a trim stays within +-510. The last step's trims stay in 0x11908. */
static int fm1_rfc_rxdc_tune(struct fm1_rf_cal *c)
{
    static const uint8_t gs[4][3] = {{0, 0, 127}, {1, 0, 127}, {1, 3, 69}, {1, 9, 14}};  /* 0x1190C [7:6],
                                                         * 0x11910 [11:8], 0x11910 [6:0] */
    uint32_t t0 = fm1_ticks(), g, j;
    int32_t bi, bq, s, m;
    fm1_rfc_phy_rst();
    fm1_rfc_c_wr(0x15, 0);
    fm1_rfc_c_wr(0x14, 0);
    fm1_rfc_cca_for_trim();
    fm1_rfc_loop_test();
    fm1_rfc_d_wr(0x00, 3);
    fm1_rfc_d_wr(0x5F, 16);
    fm1_rfc_dp_wr(0x00, 62);
    fm1_rfc_dp_wr(0x1B, 56);
    fm1_rfc_dp_wr(0x08, 0xC0);
    fm1_rfc_dp_wr(0x13, 0);
    fm1_rfc_dp_wr(0x14, 56);
    fm1_rfc_dp_wr(0x14, 52);
    fm1_rfc_vga_gain(40);
    c->abort = 0;
    for (g = 0; g < 4u && !c->abort; g++) {
        fm1_rfc_rmw(0x1190Cu, 3u << 6, (uint32_t)gs[g][0] << 6);
        fm1_rfc_rmw(0x11910u, 0xFu << 8, (uint32_t)gs[g][1] << 8);
        fm1_rfc_rmw(0x11910u, 0x7Fu, gs[g][2]);
        fm1_rfc_anl_rxdc_i(0);
        fm1_rfc_anl_rxdc_q(0);
        bi = bq = 0;
        s = 256;
        for (j = 0; j < 12u && !c->abort; j++) {
            m = fm1_rfc_dem(c, 1);
            if (c->abort)
                break;
            if (m > 0 && bi > -510)
                bi -= s;
            else if (m < 0 && bi < 510)
                bi += s;
            fm1_rfc_anl_rxdc_i(bi);
            m = fm1_rfc_dem(c, 2);
            if (m > 0 && bq > -510)
                bq -= s;
            else if (m < 0 && bq < 510)
                bq += s;
            fm1_rfc_anl_rxdc_q(bq);
            s = s > 1 ? s >> 1 : 1;
        }
        if (c->abort)
            break;
        c->rx_dc[g][0] = (uint16_t)fm1_rfc_anl_rxdc_i(bi);
        c->rx_dc[g][1] = (uint16_t)fm1_rfc_anl_rxdc_q(bq);
    }
    c->us_rxdc = fm1_rfc_us_since(t0);
    if (c->abort) {
        fm1_rfc_anl_rxdc_i(0);
        fm1_rfc_anl_rxdc_q(0);
        for (; g < 4u; g++)
            c->rx_dc[g][0] = c->rx_dc[g][1] = 0x200;
        return FM1_RF_CAL_E_RXDC;
    }
    return FM1_RF_CAL_OK;
}

/* CRC-16/XMODEM (the library's CRC16 over bytes 0-63, §4.1) */
static uint32_t fm1_rfc_crc16(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0, i, b;
    for (i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 8;
        for (b = 0; b < 8u; b++)
            crc = crc & 0x8000u ? (crc << 1 ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
    }
    return crc;
}

/* the 187 record (§4.1) from what the trim left: TX values as written to window D (exact: we wrote them), the TX LO,
 * RX DC and filter registers read back as rd_tx_iq_set / rd_rx_iq_set / rd_bw_set do, RX IQ neutral (not measured:
 * Wi-Fi only), rx_dc_tune as measured, then the check */
static void fm1_rfc_record(const struct fm1_rf_cal *c, uint8_t x187[68])
{
    uint32_t k, r, mag;
    for (k = 0; k < 68u; k++)
        x187[k] = 0;
    for (k = 0; k < 6u; k++) {                              /* gains unsigned 10 bits, the rest signed */
        r = (uint32_t)c->tx[k] & 0x3FFu;
        fm1_rfc_put32(x187 + 4u * k, (k == FM1_RFC_TX_IG || k == FM1_RFC_TX_QG) ? r : (uint32_t)fm1_rfc_sext10(r));
    }
    x187[24] = (uint8_t)(FM1_RF_REG(0x1191Cu) >> 8 & 0x7Fu);
    x187[25] = (uint8_t)(FM1_RF_REG(0x11920u) & 0x7Fu);
    x187[26] = 255;                                         /* dig_rx_i_gain, _q_gain; phases 0 */
    x187[27] = 255;
    r = FM1_RF_REG(0x11908u);
    mag = r >> 7 & 0x1FFu;
    fm1_rfc_put32(x187 + 32, r & 1u << 16 ? mag : 0u - mag);
    mag = r >> 17 & 0x1FFu;
    fm1_rfc_put32(x187 + 36, r & 1u << 26 ? mag : 0u - mag);
    fm1_rfc_put32(x187 + 40, FM1_RF_REG(0x1195Cu));
    fm1_rfc_put32(x187 + 44, FM1_RF_REG(0x11960u));
    for (k = 0; k < 8u; k++) {
        x187[48u + 2u * k] = (uint8_t)c->rx_dc[k >> 1][k & 1u];
        x187[49u + 2u * k] = (uint8_t)(c->rx_dc[k >> 1][k & 1u] >> 8);
    }
    r = fm1_rfc_crc16(x187, 64);                            /* (0 would read as "no record" to the SDK: 1 in 65536) */
    x187[64] = (uint8_t)r;
    x187[65] = (uint8_t)(r >> 8);
}

/* wf_rf_trim without RX IQ (§4, the SDK's order): the RX filter, TX cal mode and attenuation, TX LO (and with
 * FM1_RF_CAL_FULL the TX DC / IQ), RX DC in receive mode with bit 15 of 0x11904 cleared, then RX cal mode and
 * attenuation as the library leaves them (wf_rf_init's wf_rf_en_set(0) after the trim is the replay's). Then x187
 * is filled. A step that times out puts back its neutral values and the rest still runs.
 * -> 0, or the first step's FM1_RF_CAL_E_* */
static int fm1_rf_cal_trim(struct fm1_rf_cal *c, uint8_t x187[68])
{
    uint32_t t0 = fm1_ticks(), t, bbp0 = fm1_ble_rf_stat.bbp_timeouts, g;
    int r;
    c->err = FM1_RF_CAL_OK;
    c->dem_reads = 0;
    c->abort = 0;
    c->txlo_i = c->txlo_q = 64;
    for (g = 0; g < 4u; g++)
        c->rx_dc[g][0] = c->rx_dc[g][1] = 0x200;
    c->tx[FM1_RFC_TX_IG] = c->tx[FM1_RFC_TX_QG] = 1023;
    c->tx[FM1_RFC_TX_IP] = c->tx[FM1_RFC_TX_QP] = c->tx[FM1_RFC_TX_IDC] = c->tx[FM1_RFC_TX_QDC] = 0;
    c->us_txiq = 0;

    t = fm1_ticks();
    r = fm1_rfc_rx_tune(c);
    c->us_filter = fm1_rfc_us_since(t);
    if (r && !c->err)
        c->err = r;
    fm1_rfc_en_set(2);
    fm1_rfc_cal_att(1);
    r = fm1_rfc_txiq_tune(c);
    if (r && !c->err)
        c->err = r;
    fm1_rfc_hp_close();
    fm1_rfc_en_set(0);
    FM1_RF_REG(0x11904u) &= ~(1u << 15);
    r = fm1_rfc_rxdc_tune(c);
    if (r && !c->err)
        c->err = r;
    fm1_rfc_hp_open();
    fm1_rfc_en_set(3);
    fm1_rfc_cal_att(0);

    fm1_rfc_record(c, x187);
    c->bbp_timeouts = fm1_ble_rf_stat.bbp_timeouts - bbp0;
    c->us_total = fm1_rfc_us_since(t0);
    return (int)c->err;
}

/* ---- the apply side: the replay's writes with the record's fields (quick_start_wifi_trim, §4.5) ---- */

/* the 0x11908 field pair as set_anl_rxdc_i / _q put a signed value in */
static uint32_t fm1_rfc_anl_field(uint32_t v, uint32_t shift, uint32_t sbit, int32_t dc)
{
    int16_t d = (int16_t)dc;
    v &= ~(0x1FFu << shift);
    if (d >= 0)
        return v | ((uint32_t)d & 0x1FFu) << shift | 1u << sbit;
    return (v | ((uint32_t)(-d) & 0x1FFu) << shift) & ~(1u << sbit);
}

/* a register write of the replay, with the record's fields at their true widths: 0x1191C [14:8] txlo_cali,
 * 0x11920 [6:0] txlo_calq and [17:10] flt_bw_1 byte 2, 0x11910 [21:14] flt_bw_1 byte 2, 0x11908 anl_rxdc_i
 * ([15:7], bit 16) and anl_rxdc_q ([25:17], bit 26), 0x1195C / 0x11960 the whole flt_bw_1 / _2. Other addresses:
 * v unchanged. (The quick start also sets 0x11904 [11:8] = 8, a constant the replay carries.) */
static uint32_t fm1_rf_cal_reg(uint32_t addr, uint32_t v, const uint8_t x187[68])
{
    uint32_t bw2 = x187[42];
    switch (addr) {
    case 0x1191Cu:
        return (v & ~(0x7Fu << 8)) | (uint32_t)(x187[24] & 0x7Fu) << 8;
    case 0x11920u:
        return (v & ~(0x7Fu | 0xFFu << 10)) | (x187[25] & 0x7Fu) | bw2 << 10;
    case 0x11910u:
        return (v & ~(0xFFu << 14)) | bw2 << 14;
    case 0x11908u:
        v = fm1_rfc_anl_field(v, 7, 16, (int32_t)fm1_rfc_u32(x187 + 32));
        return fm1_rfc_anl_field(v, 17, 26, (int32_t)fm1_rfc_u32(x187 + 36));
    case 0x1195Cu:
        return fm1_rfc_u32(x187 + 40);
    case 0x11960u:
        return fm1_rfc_u32(x187 + 44);
    default:
        return v;
    }
}

/* a window write of the replay, the same: D (w 2) entries 0x04-0x09 = the six TX values >> 2, 0x61 = their low
 * bit pairs (I gain [1:0], I phase [3:2], Q gain [5:4], Q phase [7:6]), 0x62 [1:0] / [3:2] = the DCs' (bits 7-4
 * of v kept); D' (w 3) 0x09 / 0x0B = RX I / Q gain, 0x0A / 0x0C = RX I / Q phase. Others: v unchanged. */
static uint32_t fm1_rf_cal_win(uint32_t w, uint32_t entry, uint32_t v, const uint8_t x187[68])
{
    uint32_t k;
    if (w == FM1_RFC_D) {
        if (entry >= 0x04u && entry <= 0x09u)
            return fm1_rfc_u32(x187 + 4u * (entry - 4u)) >> 2 & 0xFFu;
        if (entry == 0x61u) {
            for (v = 0, k = 0; k < 4u; k++)
                v |= (fm1_rfc_u32(x187 + 4u * k) & 3u) << 2u * k;
            return v;
        }
        if (entry == 0x62u)
            return (v & ~0xFu) | (fm1_rfc_u32(x187 + 16) & 3u) | (fm1_rfc_u32(x187 + 20) & 3u) << 2;
    } else if (w == FM1_RFC_DP) {
        if (entry == 0x09u)
            return x187[26];
        if (entry == 0x0Bu)
            return x187[27];
        if (entry == 0x0Au)
            return x187[28];
        if (entry == 0x0Cu)
            return x187[29];
    }
    return v;
}

/* the RF-die table (wl_rfd_actl_ram_init, §6): word 0 of entries 128-223 keeps [11:0] and takes rx_dc_tune[g]:
 * I in [21:12], Q << 22; g = 0 for 128-132, 1 for 133-179, 2 for 180-185, 3 for 186-223. Others: w0 unchanged. */
static uint32_t fm1_rf_cal_rfd_w0(uint32_t entry, uint32_t w0, const uint8_t x187[68])
{
    uint32_t g, i, q;
    if (entry < 128u || entry > 223u)
        return w0;
    g = entry < 133u ? 0u : entry < 180u ? 1u : entry < 186u ? 2u : 3u;
    i = x187[48u + 4u * g] | (uint32_t)x187[49u + 4u * g] << 8;
    q = x187[50u + 4u * g] | (uint32_t)x187[51u + 4u * g] << 8;
    return (w0 & 0xFFFu) | (i & 0x3FFu) << 12 | q << 22;
}

/* word 1 of entries 0-127: OR in the bank of the entry's fbds code (word 1 [28:21]) << 9. Others: w1 unchanged. */
static uint32_t fm1_rf_cal_rfd_w1(const struct fm1_rf_cal *c, uint32_t entry, uint32_t w1)
{
    if (entry >= 128u)
        return w1;
    /* (the SDK's table has [15:9] zero and ORs the bank in; the replay's words carry the emulator's bank there:
     * replaced, not ORed) */
    return (w1 & ~(0x7Fu << 9)) | fm1_rf_cal_bank(c, w1 >> 21 & 0xFFu) << 9;
}

/* ---- the BT TX trims (keep_tx_iq_set and analog.c's update_mdm_txiq_cmp, §5) ---- */

/* from a 187 record: CMPUK1 / 2 = I / Q gain >> 3, CMPUK3 = (I phase + Q phase) / 8, OFSI / OFSQ = I / Q DC / 4
 * (divisions toward zero). Read-modify-write of the caller's values (bt_rf_analog_init's base: 0x2FC08 =
 * 0x03100000, 0x2FC10 = 0x80008080): 0x2FC10 [7:0] CMPUK1, [15:8] CMPUK2, [23:16] -CMPUK3; 0x2FC08 [9:0] OFSI,
 * [19:10] OFSQ. Our unit (` BT_TX_IQ: 127 123 -3`, ` BT_OF: 0 -1`): 0x80037B7F, 0x031FFC00. */
static void fm1_rf_cal_bt_trims(const uint8_t x187[68], uint32_t *w2fc08, uint32_t *w2fc10)
{
    uint32_t k1 = fm1_rfc_u32(x187 + 0) >> 3 & 0xFFu, k2 = fm1_rfc_u32(x187 + 8) >> 3 & 0xFFu;
    int32_t k3 = ((int32_t)fm1_rfc_u32(x187 + 4) + (int32_t)fm1_rfc_u32(x187 + 12)) / 8;
    int32_t ofsi = (int32_t)fm1_rfc_u32(x187 + 16) / 4, ofsq = (int32_t)fm1_rfc_u32(x187 + 20) / 4;
    uint32_t v = *w2fc10;
    v = (v & ~0xFFu) | k1;
    v = (v & ~0xFF00u) | k2 << 8;
    v = (v & ~0xFF0000u) | ((0u - ((uint32_t)k3 & 0xFFu)) & 0xFFu) << 16;
    *w2fc10 = v;
    v = *w2fc08;
    v = (v & ~0x3FFu) | ((uint32_t)ofsi & 0x3FFu);
    v = (v & ~(0x3FFu << 10)) | ((uint32_t)ofsq & 0x3FFu) << 10;
    *w2fc08 = v;
}
