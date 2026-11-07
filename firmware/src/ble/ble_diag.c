/* SPDX-License-Identifier: GPL-3.0-only */
/* The console's `blell` (docs/BLE-STACK.md §11): ble_diag.h's counters, the link layer's state and, on the FM-1, the
 * engine's registers, one "key value" line each in the console's style. Included after ble_ll.c (it reads bll). */
#include "ble_diag.h"

#if BLE_DIAG                                  /* (without it: nothing, the console has no blell either) */

typedef void (*ble_diag_put)(const char *s);

static void bd_hex(ble_diag_put put, uint32_t v, uint32_t digits)
{
    char b[9];
    uint32_t i;
    for (i = 0; i < digits && i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> ((digits - 1u - i) * 4u)) & 15u];
    b[i] = 0;
    put(b);
}

static void bd_dec(ble_diag_put put, uint32_t v)
{
    char b[11];
    uint32_t n = 10;
    b[10] = 0;
    do
        b[--n] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0 && n);
    put(b + n);
}

static void bd_kv(ble_diag_put put, const char *k, uint32_t v)
{
    put(k);
    put(" ");
    bd_dec(put, v);
    put("\r\n");
}

static void bd_kx(ble_diag_put put, const char *k, uint32_t v, uint32_t digits)
{
    put(k);
    put(" ");
    bd_hex(put, v, digits);
    put("\r\n");
}

/* the last n (at most BLE_DIAG_LAST) of a ring of opcodes, oldest first */
static void bd_ops(ble_diag_put put, const char *k, const uint8_t *ring, uint32_t total)
{
    uint32_t i, n = total < BLE_DIAG_LAST ? total : BLE_DIAG_LAST;
    bd_kv(put, k, total);
    put(k);
    put("_last");
    for (i = total - n; i != total; i++) {
        put(" ");
        bd_hex(put, ring[i & (BLE_DIAG_LAST - 1u)], 2);
    }
    put("\r\n");
}

static const char *const BD_EV[BDE_COUNT] = {
    "-", "enable", "adv_start", "adv_stop", "adv_drop", "cind_rx", "cind_ok", "cind_rej", "conn_set", "first_evt",
    "first_rx", "rx_bad", "rx_desync", "c3_zero", "ctl_rx", "ctl_tx", "instant", "close", "busy", "init", "master", "init_hit"};
static const char *const BD_LL[5] = {"off", "adv", "conn", "scan", "init"};
static const char *const BD_STOP[BDS_COUNT] = {"adv", "scan", "init", "conn", "open"};
static const char *const BD_HW[5] = {"off", "adv", "conn", "scan", "init"};

static void bd_regs(ble_diag_put put, const struct ble_diag_regs *r)
{
    static const char *const COL[BLE_DIAG_COLS] = {"col0", "col1", "col2", "col3", "col4", "col5", "col6", "col14",
                                                   "col15"};
    uint32_t i;
    put("hw_state ");
    put(BD_HW[r->hw_state % 5u]);
    put("\r\n");
    for (i = 0; i < BLE_DIAG_COLS; i++)
        bd_kx(put, COL[i], r->col[i], 4);
    bd_kv(put, "col2_state", r->col[2] >> 12 & 7u);       /* HW §2.5: 2 advertising, 7 peripheral */
    bd_kx(put, "clock", r->clock, 6);
    bd_kx(put, "ien", r->ien, 4);
    bd_kx(put, "ipnd", r->ipnd, 4);
    bd_kx(put, "g2en", r->g2en, 4);
    bd_kx(put, "g2pnd", r->g2pnd, 4);
    bd_kx(put, "bbstat", r->stat, 4);
    bd_kx(put, "txtog", r->txtog, 4);
    bd_kx(put, "rxtog", r->rxtog, 4);
    bd_kv(put, "rx_next", r->rx_next);
    bd_kv(put, "rx_sn", r->rx_sn);
    bd_kv(put, "tx_n", r->tx_n);
    bd_kv(put, "win_wide", r->win_wide);
    bd_kx(put, "txbufcntl", (uint32_t)r->txbufcntl[0] << 8 | r->txbufcntl[1], 4);
    bd_kx(put, "rxbufcntl", (uint32_t)r->rxbufcntl[0] << 8 | r->rxbufcntl[1], 4);
    bd_kx(put, "rxstat", (uint32_t)r->rxstat[0] << 16 | r->rxstat[1], 8);
    bd_kx(put, "rxahdr", (uint32_t)r->rxahdr[0] << 16 | r->rxahdr[1], 8);
    bd_kx(put, "rxdhdr", (uint32_t)r->rxdhdr[0] << 16 | r->rxdhdr[1], 8);
    bd_kx(put, "txdhdr", (uint32_t)r->txdhdr[0] << 16 | r->txdhdr[1], 8);
    bd_kx(put, "intframe", r->intframe, 4);
    bd_kx(put, "format", r->format, 4);
    bd_kx(put, "optcntl", r->optcntl, 4);
    bd_kv(put, "evtcount", r->evtcount);
    bd_kv(put, "wincntl0", r->wincntl0);
}

static void bd_cind(ble_diag_put put, const struct ble_diag *d)
{
    uint32_t chm = (uint32_t)d->cind_chm[0] | (uint32_t)d->cind_chm[1] << 8 | (uint32_t)d->cind_chm[2] << 16 |
                   (uint32_t)d->cind_chm[3] << 24;
    bd_kv(put, "cind_rx", d->cind_rx);
    bd_kv(put, "cind_ok", d->cind_ok);
    bd_kv(put, "cind_rej", d->cind_rej);
    bd_kv(put, "cind_rej_why", d->cind_rej_why);
    bd_kx(put, "cind_hdr", d->cind_hdr, 2);
    bd_kx(put, "cind_aa", d->cind_aa, 8);
    bd_kx(put, "cind_crc", d->cind_crc, 6);
    bd_kv(put, "cind_win_size", d->cind_win_size);
    bd_kv(put, "cind_win_off", d->cind_win_off);
    bd_kv(put, "cind_interval", d->cind_interval);
    bd_kv(put, "cind_latency", d->cind_latency);
    bd_kv(put, "cind_timeout", d->cind_timeout);
    bd_kv(put, "sa_calls", d->sa_calls);
    bd_kv(put, "sa_on", d->sa_on);
    bd_kv(put, "sa_sent", d->sa_sent);
    bd_kv(put, "sa_full", d->sa_full);
    bd_kv(put, "sa_iv", d->sa_iv);
    bd_kv(put, "sa_fast", d->sa_fast);
    bd_kv(put, "sa_slow", d->sa_slow);
    put("cind_chm ");
    bd_hex(put, d->cind_chm[4], 2);
    bd_hex(put, chm, 8);
    put("\r\n");
    bd_kv(put, "cind_hop", d->cind_hop);
    bd_kv(put, "cind_sca", d->cind_sca);
    bd_kv(put, "cind_isr_us", d->cind_isr_us);
    bd_kv(put, "first_evt", d->first_evt);
    bd_kv(put, "first_rx_us", d->first_rx_us);
    bd_kv(put, "first_rx_evt", d->first_rx_evt);
}

static void bd_conn(ble_diag_put put, const struct ble_diag *d)
{
    bd_kv(put, "evt_irqs", d->evt_irqs);
    bd_kv(put, "rx_irqs", d->rx_irqs);
    bd_kv(put, "conn_events", d->conn_events);
    bd_kv(put, "c3_zero", d->c3_zero);
    bd_kv(put, "evt_same", d->evt_same);
    bd_kv(put, "last_evt", d->last_evt);
    bd_kv(put, "rx_good", d->rx_good);
    bd_kv(put, "rx_crc_bad", d->rx_crc_bad);
    bd_kx(put, "rx_bad_stat", d->rx_bad_stat, 4);
    bd_kv(put, "rx_repeat", d->rx_repeat);
    bd_kv(put, "rx_empty", d->rx_empty);
    bd_kv(put, "rx_nothing", d->rx_nothing);
    bd_kv(put, "rx_desync", d->rx_desync);
    bd_kv(put, "tx_queued", d->tx_queued);
    bd_kv(put, "tx_acked", d->tx_acked);
    bd_kv(put, "tx_none", d->tx_none);
    bd_kv(put, "clk_step_max", d->clk_step_max);
    bd_ops(put, "ctl_rx", d->ctl_rx, d->ctl_rx_n);
    bd_ops(put, "ctl_tx", d->ctl_tx, d->ctl_tx_n);
    bd_ops(put, "att_rx", d->att_rx, d->att_rx_n);
    bd_kv(put, "closes", d->closes);
    bd_kx(put, "close_reason", d->close_reason, 2);
    bd_kv(put, "close_by", d->close_by);
    bd_kv(put, "close_evt", d->close_evt);
    bd_kv(put, "close_since_rx_us", d->close_since_rx_us);
    bd_kv(put, "close_since_start_us", d->close_since_start_us);
    bd_kv(put, "sup_timeouts", d->sup_timeouts);
    bd_kv(put, "estab_fails", d->estab_fails);
    bd_kv(put, "peer_terms", d->peer_terms);
}

#if defined(BLE_HW_WL82) && BLE_HW_WL82
static const uint8_t BLE_DIAG_COL[BLE_DIAG_COLS] = {0, 1, 2, 3, 4, 5, 6, 14, 15};   /* the columns read (op 2) */
/* the engine's side (the WL82 driver, ble_hw_wl82.c, in the same unity build): column reads (op 2), the interrupt
 * registers, the control block words the link runs on. Main loop, the BLE interrupts held, only after
 * ble_hw_wl82_start. */
static void ble_hw_diag_regs(struct ble_diag_regs *r)
{
    uint32_t i, w[5];
    r->valid = 1;
    r->hw_state = drv.state;
    r->rx_next = drv.rx_next;
    r->rx_sn = drv.rx_sn;
    r->tx_n = (uint8_t)(drv.tx_rec[0] + drv.tx_rec[1]);   /* PDUs of ours handed to the engine */
    r->win_wide = drv.win_wide;
    for (i = 0; i < BLE_DIAG_COLS; i++)
        r->col[i] = (uint16_t)fm1_ble_col_rd(HW_LINK, BLE_DIAG_COL[i]);
    r->clock = fm1_ble_clock(HW_LINK);
    fm1_ble_irq_regs(w);
    r->ien = w[0];
    r->ipnd = w[1];
    r->g2en = w[2];
    r->g2pnd = w[3];
    r->stat = w[4];
    r->txtog = CB->txtog;
    r->rxtog = CB->rxtog;
    r->intframe = CB->intframe;
    r->format = CB->format;
    r->optcntl = CB->optcntl;
    r->evtcount = CB->evtcount;
    r->wincntl0 = CB->wincntl[0];
    for (i = 0; i < 2u; i++) {
        r->rxstat[i] = CB->rxstat[i];
        r->rxahdr[i] = CB->rxahdr[i];
        r->rxdhdr[i] = CB->rxdhdr[i];
        r->txdhdr[i] = CB->txdhdr[i];
        r->txbufcntl[i] = CB->txbufcntl[i];
        r->rxbufcntl[i] = CB->rxbufcntl[i];
    }
}
#endif

/* "rxsnap N: t=.. w=.. f=.. nx=.. lay=.. wait=.. tog=.. cntl0=.. cntl1=.. stat0=.. stat1=.. ahdr0=.. ahdr1=.. dhdr0=..
 * dhdr1=.. ifs=.. b0=xx xx xx xx b1=xx xx xx xx" (struct ble_diag_rxs: the advertising RX state an ISR saw) */
static void bd_kxs(ble_diag_put put, const char *k, uint32_t v, uint32_t digits)
{
    put(" ");
    put(k);
    put("=");
    bd_hex(put, v, digits);
}

static void bd_rxsnap(ble_diag_put put, const char *name, uint32_t n, const struct ble_diag_rxs *x)
{
    uint32_t b, i;
    put(name);
    put(" ");
    bd_dec(put, n);
    put(": t=");
    bd_dec(put, x->t_us);
    put(" w=");
    bd_dec(put, x->where);
    put(" f=");
    bd_dec(put, x->found);
    put(" nx=");
    bd_dec(put, x->rx_next);
    put(" lay=");
    bd_dec(put, x->layout);
    put(" wait=");
    bd_dec(put, x->wait_us);
    bd_kxs(put, "tog", x->rxtog, 4);
    bd_kxs(put, "cntl0", x->cntl[0], 2);
    bd_kxs(put, "cntl1", x->cntl[1], 2);
    bd_kxs(put, "stat0", x->stat[0], 4);
    bd_kxs(put, "stat1", x->stat[1], 4);
    bd_kxs(put, "ahdr0", x->ahdr[0], 4);
    bd_kxs(put, "ahdr1", x->ahdr[1], 4);
    bd_kxs(put, "dhdr0", x->dhdr[0], 4);
    bd_kxs(put, "dhdr1", x->dhdr[1], 4);
    bd_kxs(put, "ifs", x->ifscnt, 4);
    for (b = 0; b < 2u; b++) {
        put(b ? " b1=" : " b0=");
        for (i = 0; i < 4u; i++) {
            if (i)
                put(" ");
            bd_hex(put, x->b[b][i], 2);
        }
    }
    put("\r\n");
}

/* RX while advertising: which rule found the packets (ble_hw_wl82.c hw_adv_find), the snapshots */
static void bd_rxadv(ble_diag_put put, const struct ble_diag *d)
{
    uint32_t i, n = d->rxs_n < BLE_DIAG_RXS ? d->rxs_n : BLE_DIAG_RXS;
    bd_kv(put, "rxf_cntl", d->rxf_cntl);
    bd_kv(put, "rxf_cntl_other", d->rxf_cntl_other);
    bd_kv(put, "rxf_tog_prev", d->rxf_tog_prev);
    bd_kv(put, "rxf_tog_cur", d->rxf_tog_cur);
    bd_kv(put, "rxf_wait", d->rxf_wait);
    bd_kv(put, "rxf_late", d->rxf_late);
    bd_kv(put, "rxf_none", d->rxf_none);
    bd_kv(put, "rxl_cb", d->rxl_cb);
    bd_kv(put, "rxl_buf", d->rxl_buf);
    bd_kv(put, "rxl_none", d->rxl_none);
    bd_kv(put, "rxh_synth", d->rxh_synth);
    bd_kv(put, "rx_stat_zero", d->rx_stat_zero);
    bd_kv(put, "rx_stat_bad_valid", d->rx_stat_bad_valid);
    bd_kv(put, "rx_wait_us_max", d->rx_wait_us_max);
    bd_kv(put, "rxsnaps", d->rxs_n);
    if (d->rxs_first.found)
        bd_rxsnap(put, "rxsnap_first", 0, &d->rxs_first);
    for (i = d->rxs_n - n; i != d->rxs_n; i++)
        bd_rxsnap(put, "rxsnap", i, &d->rxs[i & (BLE_DIAG_RXS - 1u)]);
}

/* "txsnap N: t=.. evt=.. what=.. snap=.. b=.. n=.. tog=.. cntl0=.. cntl1=.. dhdr0=.. dhdr1=.. ifr=.. ptr0=.. ptr1=.. rxh=.." */
static void bd_txsnap(ble_diag_put put, const char *name, uint32_t n, const struct ble_diag_txs *x)
{
    static const char *const WHAT[] = {"-", "-", "load", "ack"};
    put(name);
    put(" ");
    bd_dec(put, n);
    put(": t=");
    bd_dec(put, x->t_us);
    put(" evt=");
    bd_dec(put, x->evt);
    put(" what=");
    put(x->what < sizeof WHAT / sizeof WHAT[0] ? WHAT[x->what] : "?");
    bd_kxs(put, "snap", x->snap, 1);
    put(" b=");
    bd_dec(put, x->b);
    put(" n=");
    bd_dec(put, x->n);
    bd_kxs(put, "tog", x->txtog, 4);
    bd_kxs(put, "cntl0", x->cntl[0], 2);
    bd_kxs(put, "cntl1", x->cntl[1], 2);
    bd_kxs(put, "dhdr0", x->txdhdr[0], 4);
    bd_kxs(put, "dhdr1", x->txdhdr[1], 4);
    bd_kxs(put, "ifr", x->intframe, 4);
    bd_kxs(put, "ptr0", x->txptr[0], 4);
    bd_kxs(put, "ptr1", x->txptr[1], 4);
    bd_kxs(put, "rxh", x->rxdhdr, 4);
    put("\r\n");
}

/* TX in a connection (ble_hw_wl82.c hw_tx_service) and the connection RX rule against RXTOG */
static void bd_tx(ble_diag_put put, const struct ble_diag *d)
{
    uint32_t i, n = d->txs_n < BLE_DIAG_TXS ? d->txs_n : BLE_DIAG_TXS;
    bd_kv(put, "rxc_tog_past", d->rxc_tog_past);
    bd_kv(put, "rxc_tog_at", d->rxc_tog_at);
    bd_kv(put, "tx_eng_held", d->tx_eng_held);
    bd_kv(put, "tx_ack_evt_max", d->tx_ack_evt_max);
    bd_kv(put, "txsnaps", d->txs_n);
    if (d->txs_first.what)
        bd_txsnap(put, "txsnap_first", 0, &d->txs_first);
    for (i = d->txs_n - n; i != d->txs_n; i++)
        bd_txsnap(put, "txsnap", i, &d->txs[i & (BLE_DIAG_TXS - 1u)]);
}

/* the host's protocol ring, oldest first: "pdu N: t=.. evt=.. rx|tx att|sig|smp|ll n=LEN: xx xx .." (at most the
 * first 8 octets, opcode first; ble_diag.h ble_diag_pdu) */
static void bd_pdus(ble_diag_put put, const struct ble_diag *d)
{
    static const char *const CH[5] = {"?", "att", "sig", "smp", "ll"};
    uint32_t i, k, n = d->pdu_n < BLE_DIAG_PDUS ? d->pdu_n : BLE_DIAG_PDUS;
    bd_kv(put, "att_ntf", d->att_ntf_n);
    bd_kv(put, "att_wcmd", d->att_wcmd_n);
    bd_kv(put, "enc_req", d->enc_req_n);
    bd_kv(put, "enc_on", d->enc_on_n);
    bd_kv(put, "isr_max_us", d->isr_max_us);
    bd_kv(put, "pdus", d->pdu_n);
    for (i = d->pdu_n - n; i != d->pdu_n; i++) {
        const struct ble_diag_pdu *x = &d->pdu[i & (BLE_DIAG_PDUS - 1u)];
        put("pdu ");
        bd_dec(put, i);
        put(": t=");
        bd_dec(put, x->t_us);
        put(" evt=");
        bd_dec(put, x->evt);
        put(x->ch & BDP_TX ? " tx " : " rx ");
        put(CH[(x->ch & 0x7Fu) < 5u ? x->ch & 0x7Fu : 0u]);
        put(" n=");
        bd_dec(put, x->n);
        put(":");
        for (k = 0; k < 8u && k < x->n; k++) {
            put(" ");
            bd_hex(put, x->b[k], 2);
        }
        put("\r\n");
    }
}

/* BLE-MIDI in: the writes, the packets, the messages by kind, the last 4 packets and messages, oldest first */
static void bd_midi(ble_diag_put put, const struct ble_diag *d)
{
    uint32_t i, k, n;
    bd_kv(put, "mi_wcmd", d->mi_wcmd);
    bd_kv(put, "mi_wreq", d->mi_wreq);
    bd_kv(put, "mi_w_other", d->mi_w_other);
    bd_kx(put, "mi_w_other_h", d->mi_w_other_h, 4);
    bd_kv(put, "mi_pkts", d->mi_pkts);
    bd_kv(put, "mi_bad_hdr", d->mi_bad_hdr);
    bd_kv(put, "mi_note_on", d->mi_on);
    bd_kv(put, "mi_note_off", d->mi_off);
    bd_kv(put, "mi_cc", d->mi_cc);
    bd_kv(put, "mi_clock", d->mi_clock);
    bd_kv(put, "mi_sense", d->mi_sense);
    bd_kv(put, "mi_other", d->mi_other);
    n = d->mi_raw_n < 4u ? d->mi_raw_n : 4u;
    for (i = d->mi_raw_n - n; i != d->mi_raw_n; i++) {
        put("mi_raw n=");
        bd_dec(put, d->mi_raw_len[i & 3u]);
        put(":");
        for (k = 0; k < 12u && k < d->mi_raw_len[i & 3u]; k++) {
            put(" ");
            bd_hex(put, d->mi_raw[i & 3u][k], 2);
        }
        put("\r\n");
    }
    n = d->mi_msg_n < 4u ? d->mi_msg_n : 4u;
    for (i = d->mi_msg_n - n; i != d->mi_msg_n; i++)
        bd_kx(put, "mi_msg", d->mi_msg[i & 3u], 8);   /* cin | status << 8 | d1 << 16 | d2 << 24 */
}

#if BLE_CENTRAL
/* scanning (ble_dgs: the driver's and the link layer's counters; docs/BLE-STACK.md §9.1) */
static void bd_scan(ble_diag_put put)
{
    const struct ble_diag_scan *s = &ble_dgs;
    bd_kv(put, "scan_starts", s->starts);
    bd_kv(put, "scan_stops", s->stops);
    bd_kv(put, "scan_events", s->events);
    bd_kv(put, "scan_rx_irqs", s->rx_irqs);
    bd_kv(put, "scan_rxf_cntl", s->rxf_cntl);       /* C2: RXBUFnCNTL bit0 marked the report */
    bd_kv(put, "scan_rxf_tog", s->rxf_tog);         /* ... or only RXTOG moving past it did */
    bd_kv(put, "scan_rxf_none", s->rxf_none);
    bd_kv(put, "scan_rx_bad_stat", s->rx_bad_stat);
    bd_kv(put, "scan_rx_bad_len", s->rx_bad_len);
    bd_kv(put, "scan_adv_ind", s->rep_adv_ind);
    bd_kv(put, "scan_scan_rsp", s->rep_scan_rsp);   /* C1: SCAN_RSPs mean the engine filled the SCAN_REQ's AdvA */
    bd_kv(put, "scan_other", s->rep_other);
    bd_kv(put, "scan_ring_full", s->ring_full);
    bd_kv(put, "scan_req_armed", s->req_armed);
    bd_kv(put, "scan_req_fail", s->req_fail);
    bd_kv(put, "scan_rsp_ok", s->rsp_ok);
    bd_kv(put, "scan_upper_max", s->upper_max);
    bd_kv(put, "scan_ch_next", s->ch);
    bd_kv(put, "scan_last_ch", s->last_ch);
    bd_kx(put, "scan_last_rssi", s->last_rssi, 4);
    bd_kx(put, "scan_last_ahdr", s->last_ahdr, 4);
    bd_kx(put, "scan_last_dhdr", s->last_dhdr, 4);
    bd_kx(put, "scan_last_cntl", s->last_cntl, 2);
    bd_kv(put, "scan_last_tog", s->last_tog);
}

/* connecting out (ble_dgc; docs/BLE-DEVICES-DESIGN.md §4, HW §21.9 C3-C6): a step that stops shows where */
static void bd_central(ble_diag_put put)
{
    const struct ble_diag_central *c = &ble_dgc;
    bd_kv(put, "cen_connects", c->connects);
    bd_kv(put, "cen_cancels", c->cancels);
    bd_kv(put, "init_events", c->init_events);
    bd_kv(put, "init_rx", c->init_rx);
    bd_kv(put, "init_rx_target", c->init_rx_target);   /* the target's ADV_IND heard while initiating */
    bd_kv(put, "init_rx_other", c->init_rx_other);
    bd_kv(put, "init_rx_bad", c->init_rx_bad);
    bd_kv(put, "init_rx_stale", c->init_rx_stale);     /* a packet already in the air when initiating started: no hit */
    bd_kv(put, "init_rxf_cntl", c->init_rxf_cntl);
    bd_kv(put, "init_rxf_tog", c->init_rxf_tog);
    bd_kx(put, "init_last_ahdr", c->init_last_ahdr, 4);
    bd_kx(put, "init_last_dhdr", c->init_last_dhdr, 4);
    bd_kv(put, "init_last_ch", c->init_last_ch);
    bd_kv(put, "init_chsel", c->init_chsel);
    bd_kv(put, "master_starts", c->master_starts);
    bd_kv(put, "c4_rx_to_evt_us", c->c4_rx_to_evt_us); /* C4: the event IRQ after the engine's CONNECT_IND */
    bd_kv(put, "c4_rx_to_evt_max", c->c4_rx_to_evt_max);
    bd_kv(put, "m_setup_us", c->m_setup_us);           /* the hit -> the anchor counter written (the last master) */
    bd_kv(put, "m_anchor_adj", c->m_anchor_adj);       /* slots the first anchor was brought forward (last / max) */
    bd_kv(put, "m_anchor_adj_max", c->m_anchor_adj_max);
    bd_kv(put, "m_anchor_late", c->m_anchor_late);     /* too late for the transmit window even so */
    bd_kv(put, "m_events", c->m_events);
    bd_kv(put, "m_events_rx", c->m_events_rx);         /* C6: the peripheral answered our anchor packet */
    bd_kv(put, "m_first_rx_us", c->m_first_rx_us);     /* C5: state 6 written -> its first packet */
    bd_kv(put, "m_first_rx_evt", c->m_first_rx_evt);
    bd_kv(put, "m_first_evt", c->m_first_evt);
    bd_kv(put, "m_estab_fails", c->m_estab_fails);     /* C3: 0x3E right after a switch: no CONNECT_IND reached it */
    bd_kv(put, "m_sup_timeouts", c->m_sup_timeouts);
    bd_kv(put, "m_closes", c->m_closes);
    bd_kx(put, "m_close_reason", c->m_close_reason, 2);
    bd_kv(put, "m_close_by", c->m_close_by);
    bd_kv(put, "m_ver_rx", c->m_ver_rx);
    bd_kv(put, "m_feat_rsp", c->m_feat_rsp);
    bd_kv(put, "m_len_done", c->m_len_done);
    bd_kv(put, "m_upd_tx", c->m_upd_tx);
    bd_kv(put, "m_chm_tx", c->m_chm_tx);
    bd_kv(put, "m_param_req_rx", c->m_param_req_rx);
    bd_kv(put, "m_l2_upd_rx", c->m_l2_upd_rx);
    bd_kv(put, "m_phy_req_rx", c->m_phy_req_rx);
    bd_kv(put, "m_enc_req_tx", c->m_enc_req_tx);
    bd_kv(put, "m_enc_rsp_rx", c->m_enc_rsp_rx);
    bd_kv(put, "m_start_enc_rx", c->m_start_enc_rx);
    bd_kv(put, "m_enc_on", c->m_enc_on);
    bd_kv(put, "m_enc_rej", c->m_enc_rej);
    bd_kx(put, "m_enc_rej_err", c->m_enc_rej_err, 2);
    bd_kv(put, "m_held", c->m_held);                   /* its procedures held while our encryption started */
    bd_kv(put, "gc_starts", c->gc_starts);
    bd_kv(put, "gc_state", c->gc_state);               /* 1 MTU 2 service 3 services walked 4 chars 5 descs 6 CCCD 7 ready
                                                        * 8 failed */
    bd_kv(put, "gc_mtu", c->gc_mtu);
    bd_kx(put, "gc_svc", (uint32_t)c->gc_svc_s << 16 | c->gc_svc_e, 8);
    bd_kx(put, "gc_val", c->gc_val, 4);
    bd_kx(put, "gc_cccd", c->gc_cccd, 4);
    bd_kv(put, "gc_subscribed", c->gc_subscribed);
    bd_kv(put, "gc_errs", c->gc_errs);
    bd_kx(put, "gc_last_err", (uint32_t)c->gc_last_err_op << 24 | (uint32_t)c->gc_last_err_h << 8 | c->gc_last_err, 8);
    bd_kv(put, "gc_auth_errs", c->gc_auth_errs);
    bd_kv(put, "gc_retries", c->gc_retries);
    bd_kv(put, "gc_ntf_rx", c->gc_ntf_rx);
    bd_kv(put, "gc_ind_rx", c->gc_ind_rx);
    bd_kv(put, "gc_wcmd_tx", c->gc_wcmd_tx);
    bd_kv(put, "gc_timeouts", c->gc_timeouts);
    bd_kv(put, "gc_no_midi", c->gc_no_midi);
    bd_kv(put, "si_sec_req_rx", c->si_sec_req_rx);
    bd_kv(put, "si_pair_req", c->si_pair_req);
    bd_kv(put, "si_pair_rsp", c->si_pair_rsp);
    bd_kv(put, "si_confirm_ok", c->si_confirm_ok);
    bd_kv(put, "si_stk_enc", c->si_stk_enc);
    bd_kv(put, "si_keys_rx", c->si_keys_rx);
    bd_kv(put, "si_keys_tx", c->si_keys_tx);
    bd_kv(put, "si_done", c->si_done);
    bd_kv(put, "si_fail_rx", c->si_fail_rx);
    bd_kv(put, "si_fail_tx", c->si_fail_tx);
    bd_kx(put, "si_last_fail", c->si_last_fail, 2);
    bd_kv(put, "si_ltk_enc", c->si_ltk_enc);
    bd_kv(put, "si_fail_late", c->si_fail_late);
    bd_kx(put, "si_rsp_io", c->si_rsp_io, 2);          /* the responder's IO capability (04 KeyboardDisplay) */
    bd_kx(put, "si_rsp_auth", c->si_rsp_auth, 2);      /* ... and AuthReq (bit 2 MITM, bit 3 SC) */
    bd_kv(put, "si_mitm_req", c->si_mitm_req);
    bd_kv(put, "si_passkey", c->si_passkey);
    bd_kv(put, "si_auth_done", c->si_auth_done);
    bd_kv(put, "cen_need_mitm", c->cen_need_mitm);
    bd_kv(put, "si_repair", c->si_repair);             /* MITM pairing again on the encrypted link */
    bd_kv(put, "si_repair_fallback", c->si_repair_fallback);   /* ... refused before a passkey: a new link */
    bd_kv(put, "m_pause_tx", c->m_pause_tx);           /* LL_PAUSE_ENC_REQ (the new key on the same link) */
    bd_kv(put, "m_pause_rsp_rx", c->m_pause_rsp_rx);
    bd_kv(put, "rc_phase", c->rc_phase);               /* 0 off 1 wait 2 scan 3 initiate 4 a pick 5 linked 6 held */
    bd_kv(put, "rc_picks", c->picks);
    bd_kv(put, "rc_tries", c->rc_tries);
    bd_kv(put, "rc_scans", c->rc_scans);
    bd_kv(put, "rc_rpa_seen", c->rc_rpa_seen);
    bd_kv(put, "rc_rpa_ok", c->rc_rpa_ok);
    bd_kv(put, "rc_ok", c->rc_ok);
    bd_kv(put, "rc_fails", c->rc_fails);
    bd_kv(put, "rc_retries", c->rc_retries);           /* the link made again after a link failure (0x3E ...) */
    bd_kv(put, "rc_try", c->rc_try);                   /* the attempt's number, 1..6 */
    bd_kv(put, "rc_last_fail", c->rc_last_fail);       /* BLE_CF_*: 1 lost 2 no MIDI 3 pairing 4 auth 5 GATT
                                                        * 6 needs a passkey 7 the bond refused (KEY) */
}
#endif

/* everything, in this order (docs/BLE-STACK.md §11 names the first fields to read); r: the engine's registers (r->valid 0: none) */
static void ble_diag_print(ble_diag_put put, const struct ble_diag_regs *r)
{
    const struct ble_diag *d = &ble_dg;
    uint32_t i, n;
    put("ll_state ");
    put(BD_LL[bll.state % 5u]);
    put("\r\n");
    bd_kv(put, "ll_enabled", bll.enabled);
    bd_kv(put, "ll_established", bll.state == LL_CONN && bll.established);
    bd_kv(put, "ll_interval", bll.state == LL_CONN ? bll.interval : 0u);
    bd_kv(put, "ll_lproc", bll.lproc);
    bd_kv(put, "ll_rproc", bll.rproc);
    bd_kv(put, "now_us", d->now_us);
    if (r && r->valid)
        bd_regs(put, r);
    bd_kv(put, "adv_starts", d->adv_starts);
    bd_kv(put, "adv_events", d->adv_events);
    bd_kv(put, "adv_rx", d->adv_rx);
    bd_kv(put, "scan_req", d->scan_req);
    bd_kv(put, "adv_drop", d->adv_drop);
    bd_kx(put, "adv_drop_stat", d->adv_drop_stat, 4);
    bd_kx(put, "adv_drop_hdr", d->adv_drop_hdr, 4);
    bd_kv(put, "busy_max", d->busy_max);
    bd_kv(put, "busy_timeouts", d->busy_timeouts);
    for (i = 0; i < BDS_COUNT; i++) {              /* "stop_PATH stops busy max_us" */
        put("stop_");
        put(BD_STOP[i]);
        put(" ");
        bd_dec(put, d->stop_n[i]);
        put(" ");
        bd_dec(put, d->stop_busy[i]);
        put(" ");
        bd_dec(put, d->stop_us_max[i]);
        put("\r\n");
    }
    put("stop_last ");
    put(BD_STOP[d->stop_last % BDS_COUNT]);
    put("\r\n");
    bd_cind(put, d);
    bd_conn(put, d);
    bd_rxadv(put, d);
    bd_tx(put, d);
    bd_pdus(put, d);
    bd_midi(put, d);
#if BLE_CENTRAL
    bd_scan(put);
    bd_central(put);
#endif
    n = d->ev_n < BLE_DIAG_RING ? d->ev_n : BLE_DIAG_RING;
    bd_kv(put, "events", d->ev_n);
    for (i = d->ev_n - n; i != d->ev_n; i++) {       /* "ev T_US NAME ARG", oldest first */
        uint32_t k = i & (BLE_DIAG_RING - 1u);
        put("ev ");
        bd_dec(put, d->ev[k].t_us);
        put(" ");
        put(d->ev[k].code < BDE_COUNT ? BD_EV[d->ev[k].code] : "?");
        put(" ");
        if (d->ev[k].code == BDE_BUSY) {           /* "busy PATH US" (hw_stop: path << 13 | us / 8) */
            put(BD_STOP[(d->ev[k].arg >> 13) % BDS_COUNT]);
            put(" ");
            bd_dec(put, (uint32_t)(d->ev[k].arg & 0x1FFFu) * 8u);
        } else
            bd_hex(put, d->ev[k].arg, 4);
        put("\r\n");
    }
}

/* `blell clear` (the BLE interrupts held): the counters and the ring from zero; the clock goes on */
static void ble_diag_clear(void)
{
    uint8_t *p = (uint8_t *)&ble_dg;
    uint32_t i;
    for (i = 0; i < sizeof ble_dg; i++)
        p[i] = 0;
    ble_dg.magic = BLE_DIAG_MAGIC;
    ble_dg.first_rx_evt = ble_dg.first_evt = 0xFFFFu;
    p = (uint8_t *)&ble_dgs;
    for (i = 0; i < sizeof ble_dgs; i++)
        p[i] = 0;
    ble_dgs.magic = BLE_DIAG_SCAN_MAGIC;
#if BLE_CENTRAL
    p = (uint8_t *)&ble_dgc;
    for (i = 0; i < sizeof ble_dgc; i++)
        p[i] = 0;
    ble_dgc.magic = BLE_DIAG_CENT_MAGIC;
    ble_dgc.m_first_rx_evt = ble_dgc.m_first_evt = 0xFFFFu;
#endif
}

#endif /* BLE_DIAG */
