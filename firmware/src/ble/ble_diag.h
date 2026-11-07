/* SPDX-License-Identifier: GPL-3.0-only */
/* Link-layer diagnostics for the console's `blell` (docs/BLE-STACK.md §11): plain counters, the last CONNECT_IND,
 * the last control and ATT opcodes, and a ring of the last 32 timestamped events. RAM only, a few hundred bytes.
 * Written only from the BLE interrupts' context (ble_hw.h: the driver's ISRs, or the main loop with them held), so
 * nothing here needs a lock; the console reads it (a value may be one event stale) and `blell clear` zeroes it with
 * the BLE interrupts held. Recording never changes what the stack does. */
#ifndef BLE_DIAG_H
#define BLE_DIAG_H
#include <stdint.h>
#include "ble_cfg.h"

/* BLE_DIAG: 1 keeps the counters, the rings and the console's `blell`; 0 compiles them out (builder item BLE_DIAG,
 * FELUCCA_BLE_DIAG, docs/BLE-STACK.md §11). The host tests, which build this stack without felucca.c, keep it on.
 * BLE_DG(statement) is a recording; with BLE_DIAG 0 it is nothing, and recording never changes what the stack does. */
#ifndef BLE_DIAG
#ifdef FELUCCA_BLE_DIAG
#define BLE_DIAG FELUCCA_BLE_DIAG
#else
#define BLE_DIAG 1
#endif
#endif
#if BLE_DIAG
#define BLE_DG(...) __VA_ARGS__
#else
#define BLE_DG(...) ((void)0)
#endif

#define BLE_DIAG_MAGIC 0x4C454C42u      /* "BLEL": tests/ble_emu_test.py finds the block in a RAM dump by it */
#define BLE_DIAG_RING 32u               /* events kept (a power of two) */
#define BLE_DIAG_LAST 8u                /* opcodes kept (a power of two) */
#define BLE_DIAG_RXS 8u                 /* RX snapshots kept while advertising (a power of two) */
#define BLE_DIAG_TXS 8u                 /* TX decisions kept in a connection (a power of two) */
#ifndef BLE_DIAG_PDUS
#define BLE_DIAG_PDUS 64u               /* host PDUs kept (ATT, L2CAP signalling, SMP, LL encryption; a power of two) */
#endif

/* how an advertising-state RX was found (struct ble_diag_rxs.found; 0: nothing) */
enum { BDF_NONE, BDF_CNTL, BDF_CNTL_OTHER, BDF_TOG_PREV, BDF_TOG_CUR };
/* a TX snapshot's reason (struct ble_diag_txs.what; 1 was the obsolete "pol", kept free so load / ack keep their codes) */
enum { BTX_NONE, BTX_LOAD = 2, BTX_ACK = 3 };

/* event codes of the ring (ble_diag.c prints their names) */
enum {
    BDE_NONE, BDE_ENABLE, BDE_ADV_START, BDE_ADV_STOP, BDE_ADV_DROP, BDE_CIND_RX, BDE_CIND_OK, BDE_CIND_REJ,
    BDE_CONN_SET, BDE_FIRST_EVT, BDE_FIRST_RX, BDE_RX_BAD, BDE_RX_DESYNC, BDE_C3_ZERO, BDE_CTRL_RX, BDE_CTRL_TX,
    BDE_INSTANT, BDE_CLOSE, BDE_BUSY, BDE_INIT_START, BDE_MASTER, BDE_INIT_HIT, BDE_COUNT
};
/* why a CONNECT_IND was not taken (cind_rej_why) */
enum {
    BDR_OK, BDR_NOT_ADV, BDR_FORMAT, BDR_RXADD, BDR_ADVA, BDR_PARAMS, BDR_WINSIZE, BDR_WINOFF, BDR_HOP, BDR_CHM,
    BDR_AA
};
/* a protocol PDU's channel (struct ble_diag_pdu.ch, bits 0-6; bit 7: sent by us) */
enum { BDP_ATT = 1, BDP_SIG = 2, BDP_SMP = 3, BDP_LL = 4, BDP_TX = 0x80 };
/* the driver's link stops (ble_hw_wl82.c hw_stop; blell stop_*): advertising, scanning, initiating, a connection, the
 * stop before a link is opened */
enum { BDS_ADV, BDS_SCAN, BDS_INIT, BDS_CONN, BDS_OPEN, BDS_COUNT };

/* who closed a connection (close_by) */
enum { BDC_LOCAL, BDC_PEER, BDC_SUPERVISION, BDC_ESTABLISH, BDC_PROC_TIMEOUT, BDC_PROTOCOL, BDC_TERM_UNACKED };

/* the driver's diagnostic clock: microseconds since the radio was first started (ble_hw.h; the link layer's timer too) */
BLE_API uint32_t ble_hw_diag_now(void);

#if BLE_DIAG
struct ble_diag {
    uint32_t magic;
    /* advertising (driver) */
    uint32_t adv_starts, adv_events, adv_rx, scan_req, adv_drop;
    uint16_t adv_drop_stat, adv_drop_hdr;          /* the last dropped one: RXSTAT, RXAHDR */
    uint16_t busy_max;                             /* the longest 0x28038 bit1 wait when a link stopped (us) */
    uint32_t busy_timeouts;                        /* ... that ran out (the engine still busy) */
    /* CONNECT_IND (driver + link layer) */
    uint32_t cind_rx, cind_ok, cind_rej;
    uint8_t cind_rej_why, cind_hdr, cind_win_size, cind_hop, cind_sca, cind_chm[5];
    uint32_t cind_aa, cind_crc;
    uint16_t cind_win_off, cind_interval, cind_latency, cind_timeout;
    uint32_t cind_isr_us;                          /* RX IRQ entry -> state 7 written (us) */
    /* ble_host.c ble_sig_audio (the audio's slower interval): its calls, those with the audio streaming, the requests
     * sent and those the full link queue held back; what it last saw (the interval, our request's state, slow) */
    uint32_t sa_calls, sa_on, sa_sent, sa_full;
    uint16_t sa_iv;
    uint8_t sa_fast, sa_slow;
    uint32_t first_rx_us;                          /* state 7 written -> the first data PDU's RX IRQ (us) */
    uint16_t first_rx_evt, first_evt;              /* their event counters (0xFFFF: none yet) */
    /* the connection (driver) */
    uint32_t evt_irqs, rx_irqs, conn_events, c3_zero, evt_same;
    uint32_t rx_good, rx_crc_bad, rx_repeat, rx_empty, rx_nothing, rx_desync;
    uint16_t rx_bad_stat, last_evt;
    uint32_t tx_queued, tx_acked, tx_none;         /* tx_none: a free buffer, nothing queued (the engine sends empty) */
    uint32_t clk_step_max;                         /* the largest step of the LL clock (TIMER4) between two reads in a
                                                    * connection (us); before 2026-10-08 the slot clock's, in slots */
    /* the link layer */
    uint32_t ctl_rx_n, ctl_tx_n, att_rx_n;
    uint8_t ctl_rx[BLE_DIAG_LAST], ctl_tx[BLE_DIAG_LAST], att_rx[BLE_DIAG_LAST];
    uint32_t closes, sup_timeouts, estab_fails, peer_terms;
    uint8_t close_reason, close_by;
    uint16_t close_evt;                            /* the event counter at the last close */
    uint32_t close_since_rx_us, close_since_start_us;   /* now - the last RX, now - CONNECT_IND, at that close */
    /* the event ring */
    uint32_t now_us, ev_n;
    struct { uint32_t t_us; uint8_t code, pad; uint16_t arg; } ev[BLE_DIAG_RING];
    /* RX while advertising (the WL82 driver, ble_hw_wl82.c hw_adv_find): which rule found the packet. The engine's
     * RX semantics are open (HW §3 RXTOG / RXBUFnCNTL [I], U7): the driver tries each rule and counts the one that
     * matched, so a hardware run tells which is true */
    uint32_t rxf_cntl;                             /* RXBUFnCNTL bit0 = 1 on rx_next (the model's and our rule) */
    uint32_t rxf_cntl_other;                       /* ... on the other buffer */
    uint32_t rxf_tog_prev;                         /* content only (CNTL 0), in the buffer RXTOG has moved past */
    uint32_t rxf_tog_cur;                          /* content only, in the buffer RXTOG still points at */
    uint32_t rxf_wait;                             /* found only after the RX ISR polled RAM (the IRQ came early) */
    uint32_t rxf_late;                             /* found by the event ISR, not the RX ISR */
    uint32_t rxf_none;                             /* nothing by any rule, after the poll */
    uint32_t rxl_cb;                               /* layout: payload at RXPTR, header in RXAHDR/RXDHDR (the sheet) */
    uint32_t rxl_buf;                              /* layout: the 2 header bytes at RXPTR, the payload after them */
    uint32_t rxl_none;                             /* CNTL said filled but no PDU to our AdvA in either layout */
    uint32_t rxh_synth;                            /* RXAHDR held no 3 / 5: the header was rebuilt from the content */
    uint32_t rx_stat_zero;                         /* a PDU to us with RXSTAT 0 (never written): passed on */
    uint32_t rx_stat_bad_valid;                    /* a PDU to us with RXSTAT [3:0] not 0 / 1: dropped */
    uint32_t rx_wait_us_max;                       /* the longest RX ISR poll that found something (us) */
    uint32_t rxs_n;                                /* snapshots taken (rxs: the last 8; rxs_first: the first find) */
    struct ble_diag_rxs {                          /* the RX state as the ISR saw it, control block and RAM only */
        uint32_t t_us;
        uint16_t rxtog, ifscnt, stat[2], ahdr[2], dhdr[2];
        uint8_t cntl[2], b[2][4];                  /* RXBUFnCNTL; the first 4 bytes at RXPTRn */
        uint8_t where;                             /* 0 RX ISR entry, 1 RX ISR after the poll, 2 event ISR */
        uint8_t found;                             /* BDF_* */
        uint8_t rx_next, layout;                   /* layout: 0 none, 1 RXAHDR + RXPTR, 2 header at RXPTR */
        uint16_t wait_us;
    } rxs[BLE_DIAG_RXS], rxs_first;
    /* the connection's RX rule against RXTOG: a buffer RXBUFnCNTL said filled, RXTOG past it / still on it */
    uint32_t rxc_tog_past, rxc_tog_at;
    /* TX in a connection (ble_hw_wl82.c hw_tx_service, HW §8.2: TXBUFnCNTL bit0 1 = empty, 0 = the engine's):
     * service passes that found a buffer with bit0 0 and no PDU of ours in it (the engine's own, seen at the first
     * event on the FM-1); the most events from loading a PDU to its bit0 reading 1 again (its acknowledgement) */
    uint32_t tx_eng_held, tx_ack_evt_max;
    uint32_t txs_n;                                /* snapshots taken (txs: the last 8; txs_first: the first load) */
    struct ble_diag_txs {
        uint32_t t_us;
        uint16_t evt, txtog, txdhdr[2], intframe;
        uint16_t txptr[2], rxdhdr;                 /* TXPTR0/1; the central's last RXDHDR (NESN bit2, SN bit3) */
        uint8_t cntl[2];                           /* TXBUFnCNTL after the step */
        uint8_t what, b, n, snap;                  /* BTX_*, the buffer; n: bit b = a PDU of ours in buffer b; snap: the
                                                    * service's snapshot, bit0 TXTOG bit0, bit1 / bit2 TXBUF0 / 1CNTL bit0 */
    } txs[BLE_DIAG_TXS], txs_first;
    /* the host's protocol, both ways (ble_diag_pdu): every ATT PDU but MIDI's (notifications 0x1B and Write Commands
     * 0x52, only counted), every L2CAP signalling and SMP PDU, and the LL's encryption PDUs (ENC_REQ / RSP, START_ENC,
     * PAUSE_ENC, and a reject or unknown answer about them). The PDU's first 8 octets (opcode first) as they went,
     * the length, the event counter */
    uint32_t pdu_n, att_ntf_n, att_wcmd_n, enc_req_n, enc_on_n;
    uint32_t isr_max_us;                           /* the longest BLE interrupt (RX or event, us; the WL82 driver) */
    struct ble_diag_pdu {
        uint32_t t_us;
        uint16_t evt;                              /* the driver's last event counter (ble_dg.last_evt) */
        uint8_t ch, n;                             /* BDP_* (| BDP_TX), the length (255: or more) */
        uint8_t b[8];
    } pdu[BLE_DIAG_PDUS];
    /* BLE-MIDI in (ble_att.c; the app's side, the ring into the router: midi_ble.c ble_mdg): Write Commands and
     * Requests on the MIDI I/O value and on any other handle (the last one), packets decoded / with no valid header,
     * the decoded messages by kind, the last 4 packets (their first 12 octets, the length) and the last 4 messages
     * (USB-MIDI event packets) */
    uint32_t mi_wcmd, mi_wreq, mi_w_other, mi_pkts, mi_bad_hdr;
    uint32_t mi_on, mi_off, mi_cc, mi_clock, mi_sense, mi_other;
    uint16_t mi_w_other_h, mi_pad;
    uint32_t mi_raw_n, mi_msg_n;
    uint8_t mi_raw[4][12], mi_raw_len[4];
    uint32_t mi_msg[4];
    /* the link stops (hw_stop), per BDS_* path: stops, those that found the engine busy (0x28038 bit1), the longest
     * wait (us), the last path */
    uint32_t stop_n[BDS_COUNT], stop_busy[BDS_COUNT];
    uint16_t stop_us_max[BDS_COUNT];
    uint8_t stop_last, stop_pad;
};

#define BLE_DIAG_MIDI_KIND(d, pkt)                                                                                 \
    do {                                                                                                           \
        uint32_t st_ = ((pkt) >> 8) & 0xFFu;                                                                       \
        if ((st_ & 0xF0u) == 0x90u && ((pkt) >> 24) & 0x7Fu)                                                      \
            (d).mi_on++;                                                                                           \
        else if ((st_ & 0xF0u) == 0x80u || (st_ & 0xF0u) == 0x90u)                                                 \
            (d).mi_off++;                                                                                          \
        else if ((st_ & 0xF0u) == 0xB0u)                                                                           \
            (d).mi_cc++;                                                                                           \
        else if (st_ == 0xF8u)                                                                                     \
            (d).mi_clock++;                                                                                        \
        else if (st_ == 0xFEu)                                                                                     \
            (d).mi_sense++;                                                                                        \
        else                                                                                                       \
            (d).mi_other++;                                                                                        \
        (d).mi_msg[(d).mi_msg_n++ & 3u] = (pkt);                                                                   \
    } while (0)

static struct ble_diag ble_dg = {.magic = BLE_DIAG_MAGIC, .first_rx_evt = 0xFFFFu, .first_evt = 0xFFFFu};

/* scanning (BLE_CENTRAL; docs/BLE-STACK.md §9.1, HW §21.2), a block of its own so ble_dg's layout (which
 * tests/ble_emu_test.py reads) stays as it is. Driver: starts .. upper_max; link layer: rep_* .. ring_full */
#define BLE_DIAG_SCAN_MAGIC 0x4E414353u /* "SCAN" */
struct ble_diag_scan {
    uint32_t magic;
    uint32_t starts, stops, events, rx_irqs;       /* scans started / stopped, event and RX interrupts while scanning */
    uint32_t rxf_cntl, rxf_tog, rxf_none;          /* a report found by RXBUFnCNTL bit0 (the vendor's connection-path
                                                    * rule, HW §21.2) / by RXTOG moved past it (§8.1 advertising) / none
                                                    * (C2 settles which the engine does while scanning) */
    uint32_t rx_bad_stat, rx_bad_len;              /* RXSTAT [3:0] != 1; a length outside 6..37 */
    uint32_t rep_adv_ind, rep_scan_rsp, rep_other, ring_full;   /* reports by type into the ring; lost to a full ring */
    uint32_t req_armed, req_fail, rsp_ok;          /* active scan: SCAN_REQs allowed (FORMAT bit8 0), failures, SCAN_RSPs */
    uint16_t upper_max, last_rssi;                 /* the backoff's largest upperLimit; the last report's RSSI word */
    uint16_t last_ahdr, last_dhdr;                 /* the last report's RXAHDR / RXDHDR */
    uint8_t ch, last_ch, last_cntl, last_tog;      /* the channel programmed next; the last report's LASTCHMAP, the
                                                    * RXBUFnCNTL pair and RXTOG it was found with */
};
static struct ble_diag_scan ble_dgs = {.magic = BLE_DIAG_SCAN_MAGIC};

#if BLE_CENTRAL
/* connecting out (BLE_CENTRAL; docs/BLE-DEVICES-DESIGN.md §4, HW §21.3 / §21.4 / §21.9), a block of its own as
 * ble_dgs: each step of the initiator, the master link, its procedures, the GATT client, the SMP initiator and the
 * reconnection, so a hardware run shows where a connection stops. c3_..c6_ / c7: the fact sheet's §21.9 questions */
#define BLE_DIAG_CENT_MAGIC 0x544E4543u /* "CENT" */
struct ble_diag_central {
    uint32_t magic;
    /* the initiator (ble_ll_connect, the driver's state 3) */
    uint32_t connects, cancels, init_events;       /* initiations started / cancelled; event IRQs in state 3 */
    uint32_t init_rx, init_rx_target, init_rx_other, init_rx_bad;   /* RX IRQs in state 3: the target's ADV_IND /
                                                    * ADV_DIRECT_IND, another advertiser, nothing usable */
    uint32_t init_rxf_cntl, init_rxf_tog;          /* how it was found: RXBUFnCNTL bit0 / RXTOG moved past it */
    uint32_t master_starts;                        /* the switch to state 6 (the event IRQ after the target's ADV_IND) */
    uint32_t c4_rx_to_evt_us, c4_rx_to_evt_max;    /* C4: the target's RX IRQ -> the event IRQ that switched (last /
                                                    * max, us): the event IRQ's place after the engine's CONNECT_IND */
    uint16_t init_last_ahdr, init_last_dhdr;       /* the last state-3 report's RXAHDR / RXDHDR */
    uint8_t init_chsel, init_last_ch, pad0[2];     /* the target's ChSel bit (0: CSA #1); the last report's channel */
    /* the master link (HW §21.4) */
    uint32_t m_events, m_events_rx;                /* events closed / with a packet from the peripheral (C6: it answered
                                                    * our anchor packet inside WINCNTL2 30 us) */
    uint32_t m_first_rx_us;                        /* C5: state 6 written -> the peripheral's first packet (us) */
    uint16_t m_first_rx_evt, m_first_evt;          /* C5: their event counters (0xFFFF: none yet) */
    uint32_t m_estab_fails, m_sup_timeouts, m_closes;   /* C3: an establishment failure (0x3E) right after a switch
                                                    * means no CONNECT_IND reached the peripheral (or a wrong anchor) */
    uint8_t m_close_reason, m_close_by, pad1[2];
    /* the master's LL procedures */
    uint32_t m_ver_rx, m_feat_rsp, m_len_done, m_upd_tx, m_chm_tx, m_param_req_rx, m_phy_req_rx, m_l2_upd_rx;
    uint32_t m_enc_req_tx, m_enc_rsp_rx, m_start_enc_rx, m_enc_on, m_enc_rej;
    uint8_t m_enc_rej_err, m_held, pad2[2];        /* m_held: its control PDUs held while our encryption started */
    /* the GATT client (ble_gattc.c) */
    uint32_t gc_starts, gc_mtu, gc_subscribed, gc_errs, gc_auth_errs, gc_retries, gc_ntf_rx, gc_ind_rx, gc_wcmd_tx;
    uint32_t gc_timeouts, gc_no_midi;
    uint16_t gc_svc_s, gc_svc_e, gc_val, gc_cccd;  /* the BLE-MIDI service's range, the MIDI I/O value, its CCCD */
    uint16_t gc_last_err_h;
    uint8_t gc_state, gc_last_err_op, gc_last_err;
    /* the SMP initiator (ble_smp.c) */
    uint8_t si_last_fail;                          /* the last Pairing Failed reason, either way */
    uint32_t si_sec_req_rx, si_pair_req, si_pair_rsp, si_confirm_ok, si_fail_rx, si_fail_tx, si_stk_enc;
    uint32_t si_keys_rx, si_keys_tx, si_done, si_ltk_enc;   /* their keys taken, ours sent (PDUs); bonds; links
                                                    * encrypted with a stored LTK */
    uint32_t si_mitm_req, si_passkey, si_auth_done; /* pairings asking MITM; passkeys shown; authenticated pairings */
    uint32_t si_fail_late, cen_need_mitm;          /* a Pairing Failed after the pairing ended; refused again after
                                                    * Just Works (the link left to pair with a passkey) */
    uint8_t si_rsp_io, si_rsp_auth, pad4[2];       /* the last Pairing Response's IO capability and AuthReq */
    /* the reconnection to LAST and the picks (io/midi/ble_devices.c) */
    uint32_t rc_tries, rc_scans, rc_rpa_seen, rc_rpa_ok, rc_ok, rc_fails, picks;
    uint8_t rc_phase, rc_last_fail, pad3[2];
    /* 2026-10-09 (blell-dev4): the first anchor made up for a late set-up, the master's re-pairing on the link, the
     * link retries */
    uint32_t m_setup_us;                           /* the target's RX IRQ -> the anchor counter written (us, last) */
    uint8_t m_anchor_adj, m_anchor_adj_max, pad5[2];   /* slots the counter was shortened by (last / max) */
    uint32_t m_anchor_late;                        /* set-ups too late to reach the transmit window */
    uint32_t m_pause_tx, m_pause_rsp_rx;           /* LL_PAUSE_ENC_REQ sent; the peripheral's LL_PAUSE_ENC_RSP */
    uint32_t si_repair, si_repair_fallback;        /* re-pairings with MITM on the encrypted link; those the peer
                                                    * refused before any passkey (-> a new link with MITM) */
    uint32_t rc_retries;                           /* link attempts made again after a link failure (no pairing) */
    uint8_t rc_try, pad6[3];                       /* the attempt's number (1..BLE_RC_TRIES) */
    uint32_t init_rx_stale;                        /* 2026-10-13: state-3 reports of a packet already in the air when
                                                    * initiating started (the stopped scan's): no hit (hw_init_stale) */
};
static struct ble_diag_central ble_dgc = {.magic = BLE_DIAG_CENT_MAGIC, .m_first_rx_evt = 0xFFFFu,
                                          .m_first_evt = 0xFFFFu};
#endif

/* the engine's side, read when `blell` prints (the WL82 driver's ble_hw_diag_regs, the BLE interrupts held) */
#define BLE_DIAG_COLS 9u                /* columns 0-6, 14, 15 (ble_diag.c) */
struct ble_diag_regs {
    uint8_t valid, hw_state, rx_next, rx_sn, tx_n, win_wide;
    uint16_t col[BLE_DIAG_COLS];
    uint32_t ien, ipnd, g2en, g2pnd, stat, clock;
    uint16_t txtog, rxtog, intframe, format, optcntl, evtcount, wincntl0, rxstat[2], rxahdr[2], rxdhdr[2], txdhdr[2];
    uint8_t txbufcntl[2], rxbufcntl[2];
};

static inline void ble_diag_ev(uint8_t code, uint32_t arg)
{
    uint32_t i = ble_dg.ev_n++ & (BLE_DIAG_RING - 1u);
    ble_dg.now_us = ble_hw_diag_now();
    ble_dg.ev[i].t_us = ble_dg.now_us;
    ble_dg.ev[i].code = code;
    ble_dg.ev[i].arg = (uint16_t)arg;
}

/* one protocol PDU (ch: BDP_*, | BDP_TX when ours), p: its payload from the opcode on, n octets */
static inline void ble_diag_pdu(uint8_t ch, const uint8_t *p, uint32_t n)
{
    struct ble_diag_pdu *x;
    uint32_t i;
    if ((ch & 0x7Fu) == BDP_ATT && n && (p[0] == 0x1Bu || p[0] == 0x52u)) {
        if (p[0] == 0x1Bu)
            ble_dg.att_ntf_n++;
        else
            ble_dg.att_wcmd_n++;
        return;
    }
    x = &ble_dg.pdu[ble_dg.pdu_n++ & (BLE_DIAG_PDUS - 1u)];
    ble_dg.now_us = ble_hw_diag_now();
    x->t_us = ble_dg.now_us;
    x->evt = ble_dg.last_evt;
    x->ch = ch;
    x->n = (uint8_t)(n > 255u ? 255u : n);
    for (i = 0; i < 8u; i++)
        x->b[i] = i < n ? p[i] : 0u;
}

static inline void ble_diag_last(uint8_t *ring, uint32_t *n, uint8_t v)
{
    ring[(*n)++ & (BLE_DIAG_LAST - 1u)] = v;
}

#else /* !BLE_DIAG: the events and the opcode rings are nothing (the counters go through BLE_DG) */
#define ble_diag_ev(code, arg) ((void)0)
#define ble_diag_pdu(ch, p, n) ((void)0)
#define BLE_DIAG_MIDI_KIND(d, pkt) ((void)0)
#endif

#endif
