/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 driver's central role (firmware/src/ble/ble_hw_wl82_central.c, BLE_CENTRAL; docs/BLE-HW-FACTS.md
 * §21.3, §21.4) with the stack and the fake engine of tests/ble_fake/fm1_ble.h:
 *   initiating   state 3 in the vendor's order: RFPRIO 26, column 8 = 0, the window / interval as scanning, column 6 =
 *                0x2100 | 37, LOCALADR + FORMAT bit3 + OPTCNTL bit4 0, WHITELIST0 / TARGETADR = the target,
 *                FILTERCNTL bit0 | bit4 | type << 8, OPTCNTL bit3 0, both TX buffers the CONNECT_IND (TXAHDR 5 | TxAdd
 *                << 4 | RxAdd << 5, length 34), column 2 = 0x3000, the start (column 14 = 0x8000), then column 9 = 1;
 *                the event interrupt moves the channel (37 again, 38, 39 ...) with RFPRIO 26 (30 every 6th)
 *   RX           another advertiser's ADV_IND ignored; the target's ADV_IND (found by RXBUFnCNTL bit0 or by RXTOG)
 *                marks the hit, an ADV_DIRECT_IND to us too
 *   master       in the event interrupt after the hit (§21.3 "Switch to master"): AA / CRC, OPTCNTL, both TX buffers
 *                empty, column 4 = 0, the anchor counter (column 0 = 2 x WinOffset + 3, column 14 = 0x8000, twice),
 *                column 2 = 0x6000, WINCNTL = WinSize x 1,250 + 1,250, column 1 = 2 x Interval, column 6 = 0x8000 | hop
 *                << 8 | hop, TXDHDR of the TXTOG buffer bit2 0, the other 1; the link layer connected as master
 *   events       the master transmits first: our LL_VERSION_IND, FEATURE_REQ, LENGTH_REQ and the ATT MTU request go
 *                out by the TX rule of §8.2 and are answered; after the first event with a packet WINCNTL = 0 and
 *                WINCNTL2 = 30; a connection update at its instant - 1 writes column 4 = 0x8000 | 2 x WinOffset and
 *                column 2 = 0x6000, and the 0 / 30 window two events after the instant
 *   rules        no column read (op 2) on the initiating path; the peripheral silent -> 0x3E, advertising again
 *   late set-up  the event interrupt 0.2 / 1.9 / 4.8 / 20 ms after the hit (the FM-1, blell-dev3 / dev4: 0x3E after
 *                1.9 and 4.8 ms): the anchor counter shortened by the slots it came late, so the first master packet
 *                stays 0.6..1.4 ms into the 2.5 ms transmit window; too late for the window: counted, the minimum
 *   stops        the engine busy after a stop (0x28038 bit1, as the FM-1 after a scan): the main loop's stops wait
 *                for it (up to a scanning window), a connection's up to an event; counted per path */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#include "../firmware/src/ble/ble_stack.c"
#include "../firmware/src/ble/ble_diag.c"
#if BLE_DIAG
#define DG(x) (x)
#else
#define DG(x) 1
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("%-112s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t) { (void)pkt, (void)t; return 0; }
static void ble_app_midi_pop(void) {}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last) { (void)pkt, (void)ts, (void)last; }
static void ble_app_state(void) {}
static void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand, (void)ediv, (void)ltk; }
static void ble_app_peer_id(const uint8_t irk[16], const uint8_t a[6], uint8_t r) { (void)irk, (void)a, (void)r; }
static void ble_app_central_keys(const struct ble_keys *k) { (void)k; }

static const uint8_t OWN[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
static const uint8_t PEER[6] = {0x37, 0x37, 0x4B, 0x65, 0x79, 0xC4};

/* the engine stores an advertising PDU from AdvA a (header hdr), RXTOG moves past it; cntl: RXBUFnCNTL bit0 too */
static void eng_adv(uint8_t hdr, const uint8_t a[6], const uint8_t *target, int cntl)
{
    uint32_t b = CB->rxtog & 1u;
    uint8_t *p = bb.rx[b].buf + HW_SWHDR, n = target ? 12u : 9u;
    memcpy(p, a, 6);
    if (target)
        memcpy(p + 6, target, 6);
    else
        p[6] = 2, p[7] = 1, p[8] = 6;
    fk.ticks += (10u + n) * 8u * FM1_TICKS_PER_US;   /* (its airtime: received over it, the engine listening) */
    CB->rxahdr[b] = hdr;
    CB->rxdhdr[b] = (uint16_t)(n << 8);
    CB->rxstat[b] = 1;
    if (cntl)
        CB->rxbufcntl[b] |= 1u;
    CB->rxtog ^= 1u;
    ble_wl82_rx_irq();
}

static uint32_t col_writes(uint32_t col, uint32_t *v, uint32_t max)   /* the logged writes to a column, oldest first */
{
    uint32_t i, n = 0, from = fk.log_n > 64u ? fk.log_n - 64u : 0u;
    for (i = from; i < fk.log_n && n < max; i++)
        if ((fk.log[i & 63u] >> 16) == col)
            v[n++] = fk.log[i & 63u] & 0xFFFFu;
    return n;
}
static int col_pos(uint32_t col, uint32_t val)   /* the log position of a write (+1), 0 none */
{
    uint32_t i, from = fk.log_n > 64u ? fk.log_n - 64u : 0u;
    for (i = from; i < fk.log_n; i++)
        if (fk.log[i & 63u] == (col << 16 | val))
            return (int)(i - from + 1u);
    return 0;
}

/* ---- the peripheral and the engine's master side: one PDU pair per event, the §8.2 TX rule */
static struct {
    int tsn, nesn, psn, pnesn, in_flight, last_b;
    uint8_t q[8][40], qn;                       /* the peripheral's PDUs (header 2 + payload) */
    int ver, feat, len, mtu, silent, events_rx;
} m;

static void per_take(const uint8_t *pdu)
{
    const uint8_t *p = pdu + 2;
    uint8_t *r;
    if ((pdu[0] & 3u) == 3u) {
        r = m.q[m.qn++ % 8];
        if (p[0] == LL_VERSION_IND)
            m.ver++, r[0] = 3, r[1] = 6, r[2] = LL_VERSION_IND, r[3] = 10, r[4] = 0x4C, r[5] = 0, r[6] = 1, r[7] = 0;
        else if (p[0] == LL_FEATURE_REQ)
            m.feat++, memset(r, 0, 11), r[0] = 3, r[1] = 9, r[2] = LL_FEATURE_RSP, r[3] = 0x21;
        else if (p[0] == LL_LENGTH_REQ)
            m.len++, r[0] = 3, r[1] = 9, r[2] = LL_LENGTH_RSP, r[3] = 27, r[4] = 0, r[5] = 0x48, r[6] = 1, r[7] = 27,
            r[8] = 0, r[9] = 0x48, r[10] = 1;
        else
            m.qn--;
    } else if ((pdu[0] & 3u) == 2u && pdu[1] >= 7u && p[2] == 4 && p[4] == 0x02) {
        r = m.q[m.qn++ % 8];
        m.mtu++, r[0] = 2, r[1] = 7, r[2] = 3, r[3] = 0, r[4] = 4, r[5] = 0, r[6] = 0x03, r[7] = 185, r[8] = 0;
    }
}

/* one master event: our PDU (TXTOG buffer if loaded, else empty) out, the peripheral's answer in, the IRQs */
static void master_event(uint32_t e)
{
    uint32_t t = CB->txtog & 1u, rb = CB->rxtog & 1u;
    uint8_t pdu[40];
    int loaded = !(CB->txbufcntl[t] & 1u);
    if (!m.silent) {
        if (loaded && !m.in_flight) {          /* the engine sends buffer t */
            pdu[0] = (uint8_t)(CB->txdhdr[t] & 3u);
            pdu[1] = (uint8_t)(CB->txdhdr[t] >> 8);
            memcpy(pdu + 2, bb.tx[t].buf + HW_SWHDR, pdu[1]);
            per_take(pdu);
            m.in_flight = 1;
            m.last_b = (int)t;
        }
        {                                      /* the peripheral's answer: its next PDU or an empty one */
            uint8_t *p = bb.rx[rb].buf + HW_SWHDR, *q = m.qn ? m.q[0] : 0, n = q ? q[1] : 0;
            if (q)
                memcpy(p, q + 2, n);
            CB->rxdhdr[rb] = (uint16_t)(n << 8 | (uint32_t)m.psn << 3 | (q ? q[0] & 3u : 1u));
            CB->rxstat[rb] = 1;
            CB->rxbufcntl[rb] |= 1u;
            CB->rxtog ^= 1u;
            m.psn ^= 1;
            if (q) {
                memmove(m.q, m.q + 1, sizeof m.q[0] * 7);
                m.qn--;
            }
        }
        if (m.in_flight) {                     /* it acknowledged ours: the buffer empty again, TXTOG on */
            CB->txbufcntl[m.last_b] |= 1u;
            CB->txtog = (uint16_t)((CB->txtog & ~1u) | (uint32_t)(m.last_b ^ 1));
            m.in_flight = 0;
        }
        m.events_rx++;
        ble_wl82_rx_irq();
    }
    fk.ticks += 9u * 1250u * FM1_TICKS_PER_US;
    fk.col3 = e + 1u;
    ble_wl82_event_irq();
}

static void t_central(void)
{
    struct ble_rf_trims tr;
    struct ble_peer pr;
    uint32_t v[16], k, e, wo;
    static const uint8_t other[6] = {9, 9, 9, 9, 9, 0xC9};
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    fk.tick_per_read = FM1_TICKS_PER_US;
    ble_hw_wl82_start(&tr);
    ble_init(OWN, 1);
    ble_enable(1);
    memset(&pr, 0, sizeof pr);
    memcpy(pr.addr, PEER, 6);
    pr.addr_rand = 1;
    memset(fk.col_reads, 0, sizeof fk.col_reads);
    fk.log_n = 0;
    check("initiating: ble_central_connect -> state 3 (column 2 = 0x3000), the start, then column 9 = 1",
          ble_central_connect(&pr) && drv.state == HW_INIT && fk.col[2] == 0x3000u && fk.col[9] == 1u &&
              col_pos(14, 0x8000u) && col_pos(9, 1u) > col_pos(14, 0x8000u) && col_pos(2, 0x3000u) < col_pos(14, 0x8000u));
    check("initiating: RFPRIO 26, column 8 = 0, interval 64 / window 60 x 625 us as scanning, column 6 = 0x2100 | 37",
          CB->rfpriocntl == 26u && fk.col[8] == 0u && fk.col[1] == 64u && fk.col[15] == 0u && CB->wincntl[0] == 37500u &&
              fk.col[6] == 0x2125u);
    check("initiating: LOCALADR ours, FORMAT bit3, OPTCNTL bits 3 / 4 cleared (address matches on)",
          CB->localadr[0] == 0x2211u && CB->localadr[2] == 0xC655u && CB->format == 0x0008u && !(CB->optcntl & 0x18u));
    check("initiating: WHITELIST0 = TARGETADR = the target, FILTERCNTL bit0 | bit4 | type << 8 (random)",
          CB->whitelist[0] == 0x3737u && CB->whitelist[1] == 0x654Bu && CB->whitelist[2] == 0xC479u &&
              CB->targetadr[0] == 0x3737u && CB->targetadr[2] == 0xC479u && (CB->filtercntl & 0x111u) == 0x111u);
    {
        int ok = 1;
        for (k = 0; k < 2u; k++)
            ok &= CB->txahdr[k] == (5u | 1u << 4 | 1u << 5) && CB->txdhdr[k] == 34u << 8 &&
                  !memcmp(bb.tx[k].buf + HW_SWHDR, OWN, 6) && !memcmp(bb.tx[k].buf + HW_SWHDR + 6, PEER, 6) &&
                  !memcmp(bb.tx[k].buf + HW_SWHDR + 12, bll.state == LL_INIT ? llc.cind + 14 : OWN, 22);
        check("initiating: both TX buffers our CONNECT_IND (TXAHDR 5 | TxAdd | RxAdd, length 34, InitA, AdvA, LLData)", ok);
    }
    {   /* the stopped scan's packet, in the air when initiating started: its report ends before its own airtime
         * since the start (the "mac" pick of ble_emu_central_test.py): no hit */
        uint32_t b = CB->rxtog & 1u;
        uint8_t *p = bb.rx[b].buf + HW_SWHDR;
        memcpy(p, PEER, 6);
        p[6] = 2, p[7] = 1, p[8] = 6;
        CB->rxahdr[b] = 0x40;
        CB->rxdhdr[b] = (uint16_t)(9u << 8);
        CB->rxstat[b] = 1;
        CB->rxbufcntl[b] |= 1u;
        CB->rxtog ^= 1u;
        ble_wl82_rx_irq();
        check("initiating: the target's ADV_IND already in the air at the start (stale): no hit, still initiating",
              !drv.init_hit && drv.state == HW_INIT && DG(ble_dgc.init_rx_stale == 1u && ble_dgc.init_rx_target == 0u));
    }
    wo = llc.c.win_offset;
    fk.log_n = 0;
    for (k = 0; k < 6u; k++)
        ble_wl82_event_irq();
    k = col_writes(6, v, 16);
    check("initiating: each window's event interrupt moves the channel (37 again, 38, 39, 37 ...), RFPRIO 30 on the 6th",
          k == 6u && v[0] == 0x2125u && v[1] == 0x2126u && v[2] == 0x2127u && v[3] == 0x2125u && CB->rfpriocntl == 30u &&
              DG(ble_dgc.init_events == 6u));
    eng_adv(0x40, other, 0, 1);
    ble_wl82_event_irq();
    check("RX: another advertiser's ADV_IND: no hit, still initiating", drv.state == HW_INIT && !drv.init_hit &&
          DG(ble_dgc.init_rx_other == 1u));
    eng_adv(0xC1, PEER, OWN, 0);                    /* (an ADV_DIRECT_IND: TxAdd and RxAdd random, TargetA ours) */
    check("RX: the target's ADV_DIRECT_IND to us (found by RXTOG alone) is a hit", drv.init_hit &&
          DG(ble_dgc.init_rx_target == 1u && ble_dgc.init_rxf_tog == 1u));
    drv.init_hit = 0;
    eng_adv(0x40, PEER, 0, 1);
    check("RX: the target's ADV_IND (RXBUFnCNTL bit0) is a hit; nothing switched before the event interrupt",
          drv.init_hit && drv.state == HW_INIT && DG(ble_dgc.init_rxf_cntl >= 1u));
    fk.log_n = 0;
    ble_wl82_event_irq();
    k = col_writes(0, v, 16);
    check("master: the anchor counter: column 0 = 2 x WinOffset + 3 with column 14 = 0x8000, written twice",
          k == 3u && v[0] == 0u && v[1] == 2u * wo + 3u && v[2] == 2u * wo + 3u &&
              col_pos(2, 0x6000u) > col_pos(0, 2u * wo + 3u) && col_pos(4, 0u));
    check("master: column 2 = 0x6000 (state 6), column 1 = 2 x 9 slots, column 15 = 0x8000, column 6 = 0x8000 | hop",
          fk.col[2] == 0x6000u && fk.col[1] == 18u && fk.col[15] == 0x8000u &&
              fk.col[6] == (0x8000u | (uint32_t)llc.c.hop << 8 | llc.c.hop));
    check("master: AA and CRCInit, both TX buffers empty, TXDHDR of the TXTOG buffer bit2 0 / the other 1, WINCNTL 3,750",
          CB->bdaddr[0] == (uint16_t)llc.c.aa && CB->bdaddr[1] == (uint16_t)(llc.c.aa >> 16) &&
              CB->crcword[0] == (uint16_t)llc.c.crc_init && (CB->txbufcntl[0] & 1u) && (CB->txbufcntl[1] & 1u) &&
              CB->txdhdr[CB->txtog & 1u] == 1u && CB->txdhdr[(CB->txtog & 1u) ^ 1u] == 5u && CB->wincntl[0] == 3750u &&
              CB->rfpriocntl == 28u);
    check("master: the link layer connected as master (state conn, central)", drv.state == HW_CONN && drv.master &&
          ble_ll_central() && DG(ble_dgc.master_starts == 1u));
    check("rules: no column read (op 2) on the initiating path and the switch",
          fk.col_reads[0] + fk.col_reads[1] + fk.col_reads[2] + fk.col_reads[3] + fk.col_reads[6] + fk.col_reads[14] == 0u);
    for (e = 0; e < 40u; e++)
        master_event(e);
    check("events: our VERSION_IND, FEATURE_REQ, LENGTH_REQ and the ATT MTU request out by the TX rule, answered",
          m.ver == 1 && m.feat == 1 && m.len == 1 && m.mtu == 1 && bll.feat_known && batt.mtu == 185);
    check("events: after the first packet WINCNTL = 0, WINCNTL2 = 30 (HW §21.3 step 4)", CB->wincntl[0] == 0u &&
          CB->wincntl[1] == 0u && CB->wincntl2 == 30u && !drv.win_wide && DG(ble_dgc.m_events_rx >= 30u));
    ble_ll_conn_update(12, 12, 300);
    for (; e < 60u; e++) {
        master_event(e);
        if (drv.upd == 0 && fk.col[2] == 0x6000u && CB->wincntl[0] == 1250u + 625u && fk.col[1] == 24u)
            break;
    }
    check("update as master: at instant - 1 column 4 = 0x8000 | 2 x 6, WINCNTL 1,875, column 2 = 0x6000, interval 24",
          fk.col[4] == (0x8000u | 12u) && CB->wincntl[0] == 1875u && fk.col[1] == 24u && fk.col[2] == 0x6000u);
    for (k = 0; k < 6u; k++, e++)
        master_event(e);
    check("update as master: the 0 / 30 window again two events after the instant", CB->wincntl[0] == 0u &&
          CB->wincntl2 == 30u && fk.col[4] == 0u && bll.interval == 12);
    m.silent = 1;
    for (k = 0; k < 320u; k++, e++)
        master_event(e);
    check("the peripheral gone: the supervision timeout (3 s), advertising again (state 2)", !ble_connected() &&
          drv.state == HW_ADV && fk.col[2] == 0x2000u);
}

/* the first anchor's place in the transmit window (us from its start) for a set-up `late` us after the hit, from the
 * counter the driver wrote: the CONNECT_IND ends ~502 us after the hit (T_IFS + 352 us), the window opens 1.25 ms +
 * WinOffset x 1.25 ms after that, the counter fires (column 0 + 1) slots after the set-up */
static int32_t anchor_in_window(uint32_t late, uint32_t col0, uint32_t wo)
{
    int32_t anchor = (int32_t)late + (int32_t)(col0 + 1u) * 625, open = 502 + 1250 + (int32_t)wo * 1250;
    return anchor - open;
}

static void t_late(void)
{
    static const uint32_t LATE[4] = {200u, 1897u, 4785u, 20000u};   /* (the FM-1's: c4_rx_to_evt_*) */
    struct ble_peer pr;
    uint32_t i, wo, v[16], k, adj_old;
    memset(&pr, 0, sizeof pr);
    memcpy(pr.addr, PEER, 6);
    pr.addr_rand = 1;
    for (i = 0; i < 4u; i++) {
        int32_t at;
        if (ble_ll_central() || ble_ll_initiating())
            ble_central_cancel();
        memset(&m, 0, sizeof m);
        fkb.busy_us = i == 1u ? 900u : 0u;      /* (the engine still busy when advertising stops: waited for) */
        fkb.stops = 0;
        ble_central_connect(&pr);
        if (i == 1u)
            check("stops: the engine busy 900 us after advertising stopped (main loop): waited for, up to a scanning "
                  "window (40 ms), counted (stop_adv), no timeout", fkb.stops >= 1u && DG(ble_dg.stop_us_max[BDS_ADV] >=
                  900u && ble_dg.stop_busy[BDS_ADV] >= 1u && ble_dg.busy_timeouts == 0u && ble_dg.busy_max >= 900u));
        wo = llc.c.win_offset;
        eng_adv(0x40, PEER, 0, 1);
        fk.ticks += LATE[i] * FM1_TICKS_PER_US;
        fk.log_n = 0;
        adj_old = DG(ble_dgc.m_anchor_late);
        ble_wl82_event_irq();
        k = col_writes(0, v, 16);
        at = k == 3u ? anchor_in_window(LATE[i], v[1], wo) : -99999;
        printf("    (set-up %5u us after the hit: column 0 = %u of 2 x %u + 3, the first packet %d us into the window)\n",
               LATE[i], k == 3u ? v[1] : 0u, wo, at);
        if (i < 3u)
            check(i == 0u ? "late set-up: 0.2 ms (the FM-1's good ones): 2 x WinOffset + 3, inside the transmit window" :
                  i == 1u ? "late set-up: 1.9 ms (0x3E on the FM-1): the counter shortened, the packet inside the window" :
                            "late set-up: 4.8 ms (0x3E on the FM-1, blell-dev4): shortened by 7 slots, inside the window",
                  k == 3u && v[1] == v[2] && at >= 600 && at <= 1400 && drv.master && (i || v[1] == 2u * wo + 3u) &&
                      DG(ble_dgc.m_anchor_late == adj_old));
        else
            check("late set-up: 20 ms (the window gone): the counter at its minimum, counted (m_anchor_late); the link "
                  "fails to be established and the host tries again", k == 3u && v[1] == 2u && drv.master &&
                  DG(ble_dgc.m_anchor_late == adj_old + 1u));
        m.silent = 1;
        for (k = 0; k < 8u; k++)
            master_event(k);
        check(i == 3u ? "... no packet in six intervals: 0x3E, advertising again" : "... (left)", i < 3u ||
              (!ble_connected() && ble_central_fail() == BLE_CF_LOST && ble_central_code() == BLE_ERR_CONN_FAILED));
    }
    ble_central_connect(&pr);
    eng_adv(0x40, PEER, 0, 1);
    ble_wl82_event_irq();
    fkb.busy_us = 2500u;                        /* (the next stop: the connection's end) */
    fkb.busy_max_us = 0;
    m.silent = 1;
    for (k = 0; k < 8u && ble_connected(); k++)
        master_event(k);
    check("stops: a connection's end (in the BLE interrupts) waits at most 3 ms for the engine (stop_conn)",
          !ble_connected() && fkb.busy_max_us == 3000u && DG(ble_dg.stop_us_max[BDS_CONN] == 2500u));
    check("stops: the link-stop breadcrumb (fm1_ble_bc.stop) names the last stop, done: 5E, its path, the wait",
          fm1_ble_bc.stop >> 24 == 0x5Eu && (fm1_ble_bc.stop >> 16 & 0xFFu) < BDS_COUNT);
}

int main(void)
{
    t_central();
    t_late();
    printf("%s\n", fails ? "BLE central driver: FAILED" : "BLE central driver: all passed");
    return fails != 0;
}
