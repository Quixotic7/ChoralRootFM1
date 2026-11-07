/* SPDX-License-Identifier: GPL-3.0-only */
/* The WL82 driver's central role (BLE_CENTRAL; docs/BLE-HW-FACTS.md §21), part of ble_hw_wl82.c (included there: it
 * shares the control block, drv and the helpers): scanning (state 1, §21.2), initiating (state 3, §21.3) and the
 * switch to the master of the connection (state 6, §21.3 / §21.4). The master's connection then runs on the
 * peripheral's code (the RX rule of §8, the TX rule of §8.2, the event service) with the differences of §21.4 there
 * (drv.master). Clean room: the fact sheet, the Core Specification; no vendor code. */

/* ------------------------------------------------------------------------------------------- scanning --- */

/* HW §21.2 in the vendor's order: one link (link 0) in state 1. The engine sends the SCAN_REQ by itself in T_IFS after
 * an ADV_IND / ADV_SCAN_IND while FORMAT bit8 is clear (§21.1 [I, strong]); both TX buffers hold that SCAN_REQ with
 * six zero octets where AdvA goes, which the engine must fill in (C1). Software moves the channel 37 -> 38 -> 39 in
 * each event interrupt (the value written is for the next window, C12) and keeps the backoff. No column read (op 2)
 * on this path, as on the advertising one. */
#define HW_SCAN_WIN2 50u                /* WINCNTL2 while scanning (HW §21.2 step 4) */

static void hw_scan_backoff_reset(void)
{
    drv.bo_upper = 1;
    drv.bo_count = 1;
    drv.bo_sent = drv.bo_succ = drv.bo_fail = 0;
}

BLE_API void ble_hw_scan_start(const struct ble_hw_scan *s)
{
    uint32_t iv = s->interval, win = s->window, b, i;
    hw_link_open();
    cb_rfprio(17u);                                        /* 1 */
    fm1_ble_col_wr(HW_LINK, 8, 0);                         /* 2: advDelay off */
    if (win + 4u > iv) {                                   /* 3: the vendor's adjustment */
        if (iv > 4u)
            win = iv - 4u;
        else
            iv = 5u, win = 1u;
    }
    cb_window(win * 625u);                                 /* 4 */
    CB->wincntl2 = HW_SCAN_WIN2;
    fm1_ble_col_wr(HW_LINK, 1, iv & 0xFFFFu);
    fm1_ble_col_wr(HW_LINK, 15, iv >> 16);                 /* bit15 = 0 for scanning (its meaning: C8) */
    CB->filtercntl &= (uint16_t)~8u;                       /* 5: filter policy 0 (no whitelist) */
    drv.scan_active = s->active ? 1u : 0u;
    CB->format = (uint16_t)(drv.scan_active ? 0u : 0x100u);   /* 6: bit8 0 = SCAN_REQ allowed */
    fm1_ble_col_wr(HW_LINK, 6, 0x2100u | 37u);             /* 7: channel 37 first */
    CB->rxptr[0] = BB_OFF(bb.rx[0].buf + HW_SWHDR);        /* 8: two RX buffers (264 octets of room) */
    CB->rxptr[1] = BB_OFF(bb.rx[1].buf + HW_SWHDR);
    CB->rxbufcntl[0] &= (uint8_t)~1u;
    CB->rxbufcntl[1] &= (uint8_t)~1u;
    hw_rx_wipe(0);
    hw_rx_wipe(1);
    for (b = 0; b < 2u; b++) {                             /* 9: both TX buffers hold the SCAN_REQ */
        uint8_t *p = bb.tx[b].buf + HW_SWHDR;
        for (i = 0; i < 6u; i++) {
            p[i] = s->own[i];                              /* ScanA */
            p[6u + i] = 0;                                 /* AdvA: the engine's (C1) */
        }
        CB->txahdr[b] = (uint16_t)(3u | (uint32_t)(s->own_rand & 1u) << 4);
        CB->txdhdr[b] = (uint16_t)(12u << 8);
    }
    CB->txtog = 0;
    fm1_ble_sync();
    fm1_ble_col_wr(HW_LINK, 2, 0x1000u);                   /* 10: state 1 */
    CB->optcntl &= (uint16_t)~0x200u;                      /* 11 */
    CB->optcntl |= 0xC00u;
    hw_scan_backoff_reset();                               /* 12 */
    drv.scan_ch = 37u;
    drv.scan_evts = 0;
    fm1_ble_sync();
    fm1_ble_link_irqs_on(HW_LINK);                         /* 13: the only open link: start at once */
    fm1_ble_col_wr(HW_LINK, 7, 0);
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0);
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0x8000u);
    drv.state = HW_SCAN;
    drv.gen++;
    BLE_DG(ble_dgs.starts++);
    BLE_DG(ble_dgs.ch = 37u);
}

BLE_API void ble_hw_scan_stop(void)
{
    hw_stop(BDS_SCAN, HW_STOP_WAIT_US);                    /* HW §21.2 Stop: column 14 = 0, interrupts off, idle */
    BLE_DG(ble_dgs.stops++);
    drv.state = HW_OFF;
    drv.gen++;
}

/* Core Vol 6 Part B 4.4.3.2 as the vendor keeps it (HW §21.2): each ADV_IND / ADV_SCAN_IND counts the backoff down; at
 * 0 a SCAN_REQ went out (the engine answered that packet): if the one before got no SCAN_RSP, a failure (two in a row:
 * upperLimit doubles, at most 256); the count is drawn again in 1..upperLimit. A SCAN_RSP is a success (two in a row:
 * upperLimit halves). FORMAT bit8 = 0 only while the count is 1: the engine's SCAN_REQ armed for the next one. */
static void hw_scan_backoff(uint8_t type)
{
    uint8_t r;
    if (!drv.scan_active)
        return;
    if (type == 0x4u) {
        if (drv.bo_sent) {
            drv.bo_sent = 0;
            drv.bo_fail = 0;
            if (++drv.bo_succ >= 2u) {
                drv.bo_succ = 0;
                drv.bo_upper = (uint16_t)(drv.bo_upper > 1u ? drv.bo_upper / 2u : 1u);
            }
            BLE_DG(ble_dgs.rsp_ok++);
        }
        return;
    }
    if (type != 0x0u && type != 0x6u)
        return;
    if (--drv.bo_count == 0) {
        if (drv.bo_sent) {
            drv.bo_succ = 0;
            BLE_DG(ble_dgs.req_fail++);
            if (++drv.bo_fail >= 2u) {
                drv.bo_fail = 0;
                drv.bo_upper = (uint16_t)(drv.bo_upper < 256u ? drv.bo_upper * 2u : 256u);
            }
        }
        drv.bo_sent = 1;
        BLE_DG(ble_dgs.req_armed++);
        ble_hw_rand(&r, 1);
        drv.bo_count = (uint8_t)(1u + r % drv.bo_upper);   /* (upper 256: 1..256 as 1..255 + 0 -> a uint8 of 1..255) */
        if (!drv.bo_count)
            drv.bo_count = 1;
#if BLE_DIAG
        if (drv.bo_upper > ble_dgs.upper_max)
            ble_dgs.upper_max = drv.bo_upper;
#endif
    }
    CB->format = (uint16_t)((CB->format & ~0x100u) | (drv.bo_count == 1u ? 0u : 0x100u));
}

/* one report out of RX buffer b: header from RXAHDR / RXDHDR, status RXSTAT, RSSI2, the channel LASTCHMAP (HW §21.2) */
static void hw_scan_take(uint32_t b)
{
    uint16_t ah = CB->rxahdr[b], dh = CB->rxdhdr[b], st = CB->rxstat[b], rssi = CB->rssi[2];
    uint8_t n = (uint8_t)(dh >> 8), type = (uint8_t)(ah & 0x0Fu), ch = (uint8_t)CB->lastchmap, *pdu;
    CB->rxahdr[b] = CB->rxdhdr[b] = CB->rxstat[b] = 0;     /* (a buffer found again by RXTOG alone is a new packet) */
    BLE_DG(ble_dgs.last_ahdr = ah);
    BLE_DG(ble_dgs.last_dhdr = dh);
    BLE_DG(ble_dgs.last_rssi = rssi);
    BLE_DG(ble_dgs.last_ch = ch);
    if ((st & 0xFu) != 1u) {
        BLE_DG(ble_dgs.rx_bad_stat++);
        return;
    }
    if (n < 6u || n > 37u) {
        BLE_DG(ble_dgs.rx_bad_len++);
        return;
    }
    if (type == 0x3u || type == 0x5u)
        return;                                            /* SCAN_REQ / CONNECT_IND: dropped in state 1 */
    pdu = &bb.rx[b].buf[HW_SWHDR - 2u];
    pdu[0] = (uint8_t)ah;                                  /* Core layout: type, ChSel, TxAdd, RxAdd */
    pdu[1] = n;
    hw_scan_backoff(type);
    ble_ll_hw_adv_report(pdu, (uint8_t)(n + 2u), rssi, ch);
}

/* the RX interrupt while scanning: the connection path of HW §8 (§21.2): RXTOG read until stable, the RXTOG buffer
 * then the other, each if RXBUFnCNTL bit0 = 1, cleared after it. Whether the engine sets bit0 while scanning is C2;
 * when neither has it, the buffer RXTOG moved past is taken if its header was written since it was last taken (the
 * advertising rule the FM-1 showed, §8.1). Counted: blell scan rxf_cntl / rxf_tog / rxf_none. */
static void hw_rx_scan(void)
{
    uint32_t t1, t2, k, took = 0, tries = 0;
    uint8_t c0, c1;
    do {
        t1 = CB->rxtog & 1u;
        c0 = CB->rxbufcntl[0];
        c1 = CB->rxbufcntl[1];
        t2 = CB->rxtog & 1u;
    } while (t1 != t2 && ++tries < 4u);
    BLE_DG(ble_dgs.last_cntl = (uint8_t)((c0 & 1u) | (c1 & 1u) << 1));
    BLE_DG(ble_dgs.last_tog = (uint8_t)t2);
    for (k = 0; k < 2u; k++) {
        uint32_t b = t2 ^ k;
        if ((b ? c1 : c0) & 1u) {
            BLE_DG(ble_dgs.rxf_cntl++);
            hw_scan_take(b);
            CB->rxbufcntl[b] &= (uint8_t)~1u;
            took++;
        }
    }
    if (!took && (CB->rxdhdr[t2 ^ 1u] || CB->rxstat[t2 ^ 1u])) {
        BLE_DG(ble_dgs.rxf_tog++);
        hw_scan_take(t2 ^ 1u);
        took++;
    }
    if (!took)
        BLE_DG(ble_dgs.rxf_none++);
}

/* the event interrupt while scanning: the next window's channel (HW §21.2 "Each event"), RFPRIO 17, 26 every 6th */
static void hw_scan_event(void)
{
    fm1_ble_col_wr(HW_LINK, 6, 0x2100u | drv.scan_ch);
    drv.scan_ch = (uint8_t)(drv.scan_ch >= 39u ? 37u : drv.scan_ch + 1u);
    cb_rfprio(++drv.scan_evts % 6u ? 17u : 26u);
    BLE_DG(ble_dgs.events++);
    BLE_DG(ble_dgs.ch = drv.scan_ch);
}

/* ----------------------------------------------------------------------------------------------- initiating --- */

/* HW §21.3 in the vendor's order: link 0 in state 3, our CONNECT_IND complete in both TX buffers before the link
 * starts; the one target in WHITELIST0 / TARGETADR with FILTERCNTL bit0 (entry valid), bit4 (CONNECT_IND only to it)
 * and bit8 (its type); the engine sends the CONNECT_IND T_IFS after the target's ADV_IND by itself (C3) and leaves
 * initiating (column 9 = 1). The windows and channels as scanning (the event interrupt moves the channel). No column
 * read (op 2) on this path. */
static void hw_adr(volatile uint16_t *r, const uint8_t a[6])   /* an address into three little-endian halves */
{
    r[0] = (uint16_t)(a[0] | a[1] << 8);
    r[1] = (uint16_t)(a[2] | a[3] << 8);
    r[2] = (uint16_t)(a[4] | a[5] << 8);
}

BLE_API void ble_hw_init_start(const struct ble_hw_init *in)
{
    const uint8_t *p = in->cind;
    uint32_t iv = in->interval, win = in->window, b, i;
    hw_cpy(drv.init_own, p + 2, 6);
    hw_cpy(drv.init_peer, p + 8, 6);
    drv.init_own_rand = (uint8_t)(p[0] >> 6 & 1u);
    drv.init_peer_rand = (uint8_t)(p[0] >> 7 & 1u);
    drv.ic = in->conn;
    hw_link_open();
    cb_rfprio(26u);                                        /* 1 */
    fm1_ble_col_wr(HW_LINK, 8, 0);                         /* 2: as scanning (§21.2 steps 3-4), column 6 = 37 */
    if (win + 4u > iv) {
        if (iv > 4u)
            win = iv - 4u;
        else
            iv = 5u, win = 1u;
    }
    cb_window(win * 625u);
    CB->wincntl2 = HW_SCAN_WIN2;
    fm1_ble_col_wr(HW_LINK, 1, iv & 0xFFFFu);
    fm1_ble_col_wr(HW_LINK, 15, iv >> 16);
    fm1_ble_col_wr(HW_LINK, 6, 0x2100u | 37u);
    hw_adr(CB->localadr, drv.init_own);                    /* 3: our address, its match on */
    CB->format = 0x0008u;
    CB->optcntl &= (uint16_t)~0x10u;
    hw_adr(CB->whitelist, drv.init_peer);                  /* 4: the target */
    CB->filtercntl = (uint16_t)((CB->filtercntl & 0x7Eu) | 1u | (uint32_t)drv.init_peer_rand << 8);
    CB->filtercntl |= 0x10u;
    CB->optcntl &= (uint16_t)~0x08u;
    hw_adr(CB->targetadr, drv.init_peer);
    CB->rxptr[0] = BB_OFF(bb.rx[0].buf + HW_SWHDR);        /* 5: two RX buffers */
    CB->rxptr[1] = BB_OFF(bb.rx[1].buf + HW_SWHDR);
    CB->rxbufcntl[0] &= (uint8_t)~1u;
    CB->rxbufcntl[1] &= (uint8_t)~1u;
    hw_rx_wipe(0);
    hw_rx_wipe(1);
    for (b = 0; b < 2u; b++) {                             /* 6: both TX buffers the CONNECT_IND */
        uint8_t *q = bb.tx[b].buf + HW_SWHDR;
        for (i = 0; i < 34u; i++)
            q[i] = p[2 + i];
        CB->txahdr[b] = (uint16_t)(5u | (uint32_t)drv.init_own_rand << 4 | (uint32_t)drv.init_peer_rand << 5);
        CB->txdhdr[b] = (uint16_t)(34u << 8);
    }
    CB->txtog = 0;
    fm1_ble_sync();
    fm1_ble_col_wr(HW_LINK, 2, 0x3000u);                   /* 7: state 3 */
    drv.scan_ch = 37u;                                     /* (the first event writes 37 again, as scanning) */
    drv.scan_evts = 0;
    drv.init_hit = 0;
    drv.master = 0;
    fm1_ble_sync();
    fm1_ble_link_irqs_on(HW_LINK);                         /* 8: the only open link: start at once */
    fm1_ble_col_wr(HW_LINK, 7, 0);
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0);
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0x8000u);
    fm1_ble_col_wr(HW_LINK, 9, 1);                         /* "init end": the engine leaves state 3 after it */
    drv.state = HW_INIT;
    drv.gen++;
    drv.init_t0 = fm1_ticks();                             /* (after the start: a packet heard ends later than this) */
    BLE_DG(ble_dgc.init_last_ch = 37u);
}

BLE_API void ble_hw_init_stop(void)
{
    hw_stop(BDS_INIT, HW_STOP_WAIT_US);                    /* as any link: column 14 = 0, interrupts off, idle */
    drv.state = HW_OFF;
    drv.gen++;
}

/* one state-3 report in buffer b: the target's ADV_IND (AdvA, TxAdd) or an ADV_DIRECT_IND from it to us (TargetA,
 * RxAdd) marks the hit; the engine has sent our CONNECT_IND after it (§21.3 "Reception"); anything else is dropped.
 * A report that ends sooner after ble_hw_init_start than its own airtime ((1 + 4 + 2 + n + 3) x 8 us at 1 Mb/s) was
 * in the air before the engine listened for the target: the scan's packet that was being received when the pick
 * stopped it. No CONNECT_IND follows it, so it is no hit: taken as one, the event IRQ at the window's end switched to
 * the master with nothing sent (state 6 with no link, never left; the "mac" pick of tests/ble_emu_central_test.py
 * when the pick landed during the Mac's ADV_IND: c4_rx_to_evt 37.9 ms, a whole window, where a real hit is 0.4 ms) */
static int hw_init_stale(uint32_t n)
{
    return (fm1_ticks() - drv.init_t0) / FM1_TICKS_PER_US < (10u + n) * 8u;
}
static void hw_init_take(uint32_t b)
{
    uint16_t ah = CB->rxahdr[b], dh = CB->rxdhdr[b], st = CB->rxstat[b];
    const uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    uint8_t n = (uint8_t)(dh >> 8), type = (uint8_t)(ah & 0x0Fu);
    CB->rxahdr[b] = CB->rxdhdr[b] = CB->rxstat[b] = 0;
    BLE_DG(ble_dgc.init_last_ahdr = ah);
    BLE_DG(ble_dgc.init_last_dhdr = dh);
    if ((st & 0xFu) != 1u || n < 6u || n > 37u) {
        BLE_DG(ble_dgc.init_rx_bad++);
        return;
    }
    if (hw_init_stale(n)) {
        BLE_DG(ble_dgc.init_rx_stale++);
        return;
    }
    if ((type == 0x0u || (type == 0x1u && n == 12u && ble_eq(p + 6, drv.init_own, 6) &&
                          (uint8_t)(ah >> 7 & 1u) == drv.init_own_rand)) &&
        ble_eq(p, drv.init_peer, 6) && (uint8_t)(ah >> 6 & 1u) == drv.init_peer_rand) {
        if (!drv.init_hit) {
            drv.init_hit = 1;
            drv.init_t_hit = fm1_ticks();
            BLE_DG(ble_dgc.init_chsel = (uint8_t)(ah >> 5 & 1u));
            ble_diag_ev(BDE_INIT_HIT, ah);
        }
        BLE_DG(ble_dgc.init_rx_target++);
    } else
        BLE_DG(ble_dgc.init_rx_other++);
}

/* the RX interrupt in state 3: the vendor reads the buffer other than RXTOG's without RXBUFnCNTL (the advertising
 * path, §21.3); each buffer is taken by bit0 if the engine sets it, else by RXTOG having moved past a written header */
static void hw_rx_init(void)
{
    uint32_t t = CB->rxtog & 1u, k, took = 0;
    BLE_DG(ble_dgc.init_rx++);
    for (k = 0; k < 2u; k++)
        if (CB->rxbufcntl[k] & 1u) {
            BLE_DG(ble_dgc.init_rxf_cntl++);
            hw_init_take(k);
            CB->rxbufcntl[k] &= (uint8_t)~1u;
            took++;
        }
    if (!took && (CB->rxdhdr[t ^ 1u] || CB->rxstat[t ^ 1u])) {
        BLE_DG(ble_dgc.init_rxf_tog++);
        hw_init_take(t ^ 1u);
    }
}

/* The first anchor (HW §21.3 "First anchor", C5): the anchor counter runs 2 x WinOffset + 4 slots from its start, which
 * puts the first master packet WinOffset x 1.25 ms + 2.5 ms after this set-up. The peripheral listens for it only in the
 * transmit window, WinSize (2) x 1.25 ms long, 1.25 ms + WinOffset x 1.25 ms after the CONNECT_IND's end; the CONNECT_IND
 * ends about 0.5 ms after the target's ADV_IND that the hit is (T_IFS + its 352 us), so the packet lands
 * (set-up - hit) + 0.75 ms into the 2.5 ms window. On the FM-1 the event interrupt that runs this set-up came 0.2 and
 * 0.5 ms after the hit in the connections that were made, 1.9 and 4.8 ms in the two that failed with 0x3E (blell-dev3,
 * dev4: c4_rx_to_evt_*): the packet after the window, never heard. So the counter is shortened by the slots the set-up
 * came late (whole slots past HW_ANCHOR_AIM_US), which keeps the packet 0.6..1.4 ms into the window; it never goes
 * under HW_ANCHOR_MIN_SLOTS (later than that the window is gone: the link fails to be established, 0x3E, and the host
 * tries again). Counted: m_anchor_adj (slots, last / max), m_anchor_late. */
#define HW_ANCHOR_AIM_US 500u
#define HW_ANCHOR_MIN_SLOTS 3u

static uint32_t hw_first_anchor(const struct ble_hw_conn *c)   /* -> the anchor counter's column 0 value */
{
    uint32_t n = 2u * c->win_offset + 4u, late = (fm1_ticks() - drv.init_t_hit) / FM1_TICKS_PER_US, adj = 0;
    if (late > 625u)
        adj = (late - HW_ANCHOR_AIM_US + 624u) / 625u;
    if (adj + HW_ANCHOR_MIN_SLOTS > n) {
        adj = n - HW_ANCHOR_MIN_SLOTS;
        BLE_DG(ble_dgc.m_anchor_late++);
    }
#if BLE_DIAG
    ble_dgc.m_setup_us = late;
    ble_dgc.m_anchor_adj = (uint8_t)adj;
    if (adj > ble_dgc.m_anchor_adj_max)
        ble_dgc.m_anchor_adj_max = (uint8_t)adj;
#endif
    return n - adj - 1u;                                   /* (the counter fires one slot after the value) */
}

/* the master's set-up (§21.3 "Switch to master", the vendor's order), in the event interrupt after the hit */
static void hw_master_setup(void)
{
    const struct ble_hw_conn *c = &drv.ic;
    uint32_t a, k;
    cb_rfprio(28u);                                        /* 1: as the slave's (§7) */
    CB->anchor = 0x8000u;
    CB->txtog &= (uint16_t)~2u;
    CB->rxtog = 0;
    CB->bdaddr[0] = (uint16_t)c->aa;
    CB->bdaddr[1] = (uint16_t)(c->aa >> 16);
    CB->crcword[0] = (uint16_t)c->crc_init;
    CB->crcword[1] = (uint16_t)(c->crc_init >> 16 & 0xFFu);
    CB->optcntl |= 4u;
    CB->optcntl &= (uint16_t)~0x200u;
    CB->optcntl |= 0xC00u;
    CB->optcntl |= 0x1000u;
    CB->intframe = (uint16_t)((CB->intframe & ~0x30u) | 0x10u);
    CB->txbufcntl[0] |= 1u;                                /* both TX buffers empty (the only 1-writes, §8.2) */
    CB->txbufcntl[1] |= 1u;
    CB->anchor = (uint16_t)((CB->anchor & 0x8000u) | 4u);
    fm1_ble_col_wr(HW_LINK, 5, 0);
    fm1_ble_col_wr(HW_LINK, 4, 0);                         /* 2: master only */
    fm1_ble_col_wr(HW_LINK, 7, 0);
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0);
    a = hw_first_anchor(c);                                /* 2 x WinOffset + 4 slots, less the set-up's lateness */
    for (k = 0; k < 2u; k++) {                             /* (written twice with the same value on 1M) */
        fm1_ble_col_wr(HW_LINK, 0, a & 0xFFFFu);
        fm1_ble_col_wr(HW_LINK, 14, 0x8000u | a >> 16);
    }
    fm1_ble_col_wr(HW_LINK, 2, 0x6000u);                   /* state 6, latency 0 */
    fm1_ble_col_wr(HW_LINK, 8, 0);                         /* 3: the shared tail */
    fm1_ble_col_wr(HW_LINK, 3, 0);
    CB->evtcount = 0;
    cb_window((uint32_t)c->win_size * 1250u + 1250u);
    CB->wincntl2 = HW_WIN_NORMAL;
    fm1_ble_col_wr(HW_LINK, 1, 2u * c->interval);
    fm1_ble_col_wr(HW_LINK, 15, 0x8000u | (2u * c->interval) >> 16);
    cb_channels(c->chm);
    fm1_ble_col_wr(HW_LINK, 6, 0x8000u | (uint32_t)c->hop << 8 | c->hop);
    CB->txahdr[0] = CB->txahdr[1] = 0;
    CB->txdhdr[CB->txtog & 1u] = 1u;                      /* the TXTOG buffer: LLID 1, bit2 0; the other bit2 1 */
    CB->txdhdr[(CB->txtog & 1u) ^ 1u] = 5u;
    fm1_ble_sync();
    drv.state = HW_CONN;
    drv.master = 1;
    drv.gen++;
    drv.rx_next = 0;
    drv.rx_sn = 0;
    drv.rx_seen = drv.rx_any = 0;
    drv.tx_rec[0] = drv.tx_rec[1] = 0;                     /* no PDU recorded in either buffer */
    drv.tx_md[0] = drv.tx_md[1] = 0;
    drv.upd = 0;
    drv.win_wide = 1;                                      /* the first window, until the first packet */
    drv.wide_from = 0;
    drv.interval = c->interval;
    drv.sca = c->sca;
    drv.last_evt = 0xFFFFu;
    ble_hw_stat.connects++;
    hwd.conn_t0 = fm1_ticks();
    hwd.first_rx = hwd.c3_seen = 0;
#if BLE_DIAG
    {
        uint32_t us = (hwd.conn_t0 - drv.init_t_hit) / FM1_TICKS_PER_US;
        ble_dgc.c4_rx_to_evt_us = us;
        if (us > ble_dgc.c4_rx_to_evt_max)
            ble_dgc.c4_rx_to_evt_max = us;
        ble_dgc.master_starts++;
        ble_dgc.m_first_rx_evt = ble_dgc.m_first_evt = 0xFFFFu;
        ble_dgc.m_first_rx_us = 0;
    }
#endif
    ble_ll_hw_master_start();
}

/* the event interrupt in state 3: after the hit, the master; else the next window's channel, RFPRIO 26 (30 every 6th:
 * §21.6) */
static void hw_init_event(void)
{
    BLE_DG(ble_dgc.init_events++);
    if (drv.init_hit) {
        hw_master_setup();
        return;
    }
    fm1_ble_col_wr(HW_LINK, 6, 0x2100u | drv.scan_ch);
    BLE_DG(ble_dgc.init_last_ch = drv.scan_ch);
    drv.scan_ch = (uint8_t)(drv.scan_ch >= 39u ? 37u : drv.scan_ch + 1u);
    cb_rfprio(++drv.scan_evts % 6u ? 26u : 30u);
}
