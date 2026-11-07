/* SPDX-License-Identifier: GPL-3.0-only */
/* The link layer, one connection, LE 1M (Core Specification Vol 6 Part B): the peripheral, and with BLE_CENTRAL the
 * scanner, the initiator and the master of a connection (ble_ll_central.c). The baseband engine
 * (ble_hw.h) does the per-event timing, hopping, acknowledgement and instants; this file does the rest:
 *   - advertising PDUs (ADV_IND, SCAN_RSP) and CONNECT_IND checks (4.4, 4.5.1, 2.3.3.1);
 *   - the TX queues (control PDUs first, then L2CAP frames cut to the data length in use) and RX hand-over;
 *   - the supervision timeout and the six-interval establishment rule (4.5.2), the 40 s procedure timeout (5.2);
 *   - the control procedures (5.1): connection update and channel map with instants, termination, feature
 *     exchange (also started by us), version exchange, ping, data length, PHY update (1M only), connection
 *     parameters request (accepted when valid), encryption start (BLE_LL_ENC, else refused); LL_UNKNOWN_RSP for
 *     the rest. */
#include "ble_ll.h"
#include "ble_hw.h"
#include "ble_util.h"
#include "ble_prim.h"
#include "ble_diag.h"
#if BLE_LL_ENC
#include "ble_aes.h"
#endif

#define LL_RING_MASK (BLE_LL_TX_RING - 1u)
#define LL_PROC_US 40000000u                       /* the LL response timeout: 40 s */
enum { LL_OFF, LL_ADV, LL_CONN, LL_SCAN, LL_INIT };
/* procedures waiting on the peer: ours (lproc) and the peer's (rproc); P_VER: our version exchange (as master) */
enum { P_NONE, P_FEAT, P_LEN, P_TERM, P_UPD, P_PHY, P_ENC, P_VER };

#define LL_OUR_FEAT ((BLE_LL_ENC ? BLE_FEAT_ENC | BLE_FEAT_PING : 0u) | BLE_FEAT_CONN_PARAM | BLE_FEAT_EXT_REJECT | \
                     BLE_FEAT_PERIPH_FEAT | (BLE_LL_MAX_OCTETS > 27u ? BLE_FEAT_DLE : 0u))

static struct {
    uint8_t state, enabled, addr_rand;
    uint8_t scan_want;                             /* BLE_CENTRAL: scan instead of advertising (ble_ll_scan) */
    uint8_t adv_off;                               /* Melodee: no advertising when idle (ble_ll_advertise) */
    uint8_t addr[6];
    uint8_t adv[2 + 37], adv_len, sr[2 + 37], sr_len;
    /* the connection */
    uint16_t interval, timeout, last_evt;
    uint8_t win_size, established, chm[5];
    uint16_t win_offset;
    uint32_t t_start, t_rx;
    uint8_t max_tx, max_rx;                        /* the data length in use (octets) */
    uint16_t peer_len[4];                          /* the central's last MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime */
    uint8_t peer_feat, feat_known, ver_sent, len_done;
    uint8_t peer[6], peer_rand;                    /* the peer's address and type: a central's InitA / TxAdd from its
                                                    * CONNECT_IND; as master, the AdvA we connected to */
    uint8_t central;                               /* BLE_CENTRAL: we are the master of this connection */
    uint8_t upd_pending, chm_pending;
    struct ble_hw_conn_upd upd;
    uint8_t new_chm[5];
    uint16_t chm_instant;
    uint8_t lproc, rproc;
    uint32_t lproc_t, rproc_t;
    uint8_t term_rx, term_reason;
    uint8_t handed, acked, term_seq, term_out;     /* PDUs given to the engine / acknowledged; LL_TERMINATE_IND's
                                                    * number, and that it went out */
    /* TX: control PDUs, then L2CAP frames in a byte ring */
    uint8_t ctrl[BLE_LL_CTRL_Q][24], ctrl_len[BLE_LL_CTRL_Q], ctrl_rd, ctrl_n;
    uint8_t ring[BLE_LL_TX_RING];
    uint32_t wr, rd, off;                          /* free-running ring indexes; off: octets of the head frame sent */
#if BLE_LL_ENC
    uint8_t enc_rx, enc_tx, tx_paused;
    struct ble_ccm ccm_rx, ccm_tx;
    uint8_t rxbuf[BLE_LL_MAX_OCTETS + 4u];
#endif
} bll;

/* ------------------------------------------------------------------------------------------ advertising --- */

BLE_API void ble_ll_init(const uint8_t addr[6], uint8_t addr_random)
{
    ble_zero((uint8_t *)&bll, sizeof bll);
    ble_cpy(bll.addr, addr, 6);
    bll.addr_rand = addr_random ? 1u : 0u;
    ble_ll_set_adv_data(0, 0, 0, 0);
}

static uint8_t ll_adv_pdu(uint8_t *pdu, uint8_t type, const uint8_t *d, uint8_t n)
{
    if (n > 31u)
        n = 31u;
    pdu[0] = (uint8_t)(type | bll.addr_rand << 6);   /* TxAdd; ChSel 0: we only know Algorithm #1 */
    pdu[1] = (uint8_t)(6u + n);
    ble_cpy(pdu + 2, bll.addr, 6);
    ble_cpy(pdu + 8, d, n);
    return (uint8_t)(8u + n);
}

static void ll_adv_start(void)
{
    struct ble_hw_adv a;
    a.adv = bll.adv;
    a.adv_len = bll.adv_len;
    a.scan_rsp = bll.sr;
    a.scan_rsp_len = bll.sr_len;
    a.interval = BLE_ADV_INTERVAL;
    a.channels = BLE_ADV_CHANNELS;
    bll.state = LL_ADV;
    ble_hw_adv_start(&a);
}

BLE_API void ble_ll_set_adv_data(const uint8_t *ad, uint8_t ad_len, const uint8_t *sr, uint8_t sr_len)
{
    bll.adv_len = ll_adv_pdu(bll.adv, 0x0u, ad, ad_len);           /* ADV_IND */
    bll.sr_len = ll_adv_pdu(bll.sr, 0x4u, sr, sr_len);              /* SCAN_RSP */
    if (bll.state == LL_ADV)
        ll_adv_start();
}

BLE_API int ble_ll_connected(void) { return bll.state == LL_CONN; }
BLE_API uint16_t ble_ll_interval(void) { return bll.state == LL_CONN ? bll.interval : 0; }
BLE_API uint16_t ble_ll_timeout(void) { return bll.state == LL_CONN ? bll.timeout : 0; }
BLE_API uint8_t ble_ll_peer_features(void) { return bll.feat_known ? bll.peer_feat : 0; }
BLE_API uint8_t ble_ll_tx_octets(void) { return bll.state == LL_CONN ? bll.max_tx : 0; }
BLE_API uint16_t ble_ll_peer_len(uint32_t i) { return bll.state == LL_CONN ? bll.peer_len[i & 3u] : 0; }

BLE_API uint8_t ble_ll_addrs(uint8_t own[6], uint8_t peer[6])
{
    ble_cpy(own, bll.addr, 6);
    ble_cpy(peer, bll.peer, 6);
    return (uint8_t)(bll.addr_rand | bll.peer_rand << 1);
}

#if BLE_LL_ENC
BLE_API int ble_ll_encrypted(void) { return bll.state == LL_CONN && bll.enc_tx; }
#else
BLE_API int ble_ll_encrypted(void) { return 0; }
#endif

/* the LL's encryption PDUs in blell's protocol ring (ble_diag_pdu), both ways; LL_ENC_REQ as op, EDIV, Rand's low 5 */
#if BLE_DIAG
static void ll_diag_enc(uint8_t tx, const uint8_t *p, uint8_t n)
{
    uint8_t op = p[0], v[8];
    if (!(op >= LL_ENC_REQ && op <= LL_UNKNOWN_RSP) && op != LL_PAUSE_ENC_REQ && op != LL_PAUSE_ENC_RSP &&
        op != LL_REJECT_IND && op != LL_REJECT_EXT_IND)
        return;
    if (op == LL_ENC_REQ && n >= 11u && !tx) {
        v[0] = op;
        v[1] = p[9];
        v[2] = p[10];
        ble_cpy(v + 3, p + 1, 5);
        p = v;
        n = 8u;
        ble_dg.enc_req_n++;
    }
    ble_diag_pdu((uint8_t)(BDP_LL | (tx ? BDP_TX : 0u)), p, n);
}
#else
#define ll_diag_enc(tx, p, n) ((void)0)
#endif

static void ll_idle(void);

static void ll_close_by(uint8_t reason, uint8_t by)
{
#if BLE_DIAG
    uint32_t now = ble_hw_time_us();
#else
    (void)ble_hw_time_us();                                /* (the link layer's clock still steps) */
#endif
    BLE_DG(ble_dg.closes++);
    BLE_DG(ble_dg.close_reason = reason);
    BLE_DG(ble_dg.close_by = by);
    BLE_DG(ble_dg.close_evt = bll.last_evt);
    BLE_DG(ble_dg.close_since_rx_us = now - bll.t_rx);
    BLE_DG(ble_dg.close_since_start_us = now - bll.t_start);
    if (by == BDC_SUPERVISION)
        BLE_DG(ble_dg.sup_timeouts++);
    else if (by == BDC_ESTABLISH)
        BLE_DG(ble_dg.estab_fails++);
    else if (by == BDC_PEER)
        BLE_DG(ble_dg.peer_terms++);
    ble_diag_ev(BDE_CLOSE, (uint32_t)reason | (uint32_t)by << 8);
#if BLE_CENTRAL && BLE_DIAG
    if (bll.central) {                                     /* (the master's endings, C3) */
        ble_dgc.m_closes++;
        ble_dgc.m_close_reason = reason;
        ble_dgc.m_close_by = by;
        ble_dgc.m_estab_fails += by == BDC_ESTABLISH;
        ble_dgc.m_sup_timeouts += by == BDC_SUPERVISION;
    }
#endif
    ble_hw_conn_stop();
    bll.state = LL_OFF;
    ble_host_disconnected(reason);
    ll_idle();
}

static void ll_close(uint8_t reason) { ll_close_by(reason, BDC_PROTOCOL); }

#if BLE_CENTRAL
static void ll_scan_start(void);        /* (ble_ll_central.c) */
#endif

/* no link: scan (BLE_CENTRAL, the DEVICES list open), else advertise when enabled, else nothing */
static void ll_idle(void)
{
#if BLE_CENTRAL
    if (bll.enabled && bll.scan_want) {
        ll_scan_start();
        return;
    }
#endif
    if (bll.enabled && !bll.adv_off)
        ll_adv_start();
}

/* Melodee: advertise when idle (on, the default after ble_ll_init) or stay quiet; a connection is left as it is */
BLE_API void ble_ll_advertise(int on)
{
    bll.adv_off = on ? 0u : 1u;
    if (!on && bll.state == LL_ADV) {
        ble_hw_adv_stop();
        bll.state = LL_OFF;
    } else if (on && bll.enabled && bll.state == LL_OFF)
        ll_idle();
}

BLE_API void ble_ll_enable(int on)
{
    bll.enabled = on ? 1u : 0u;
    ble_diag_ev(BDE_ENABLE, (uint32_t)bll.enabled | (uint32_t)bll.state << 8);
    if (on && bll.state == LL_OFF)
        ll_idle();
    else if (!on && bll.state == LL_ADV) {
        ble_hw_adv_stop();
        bll.state = LL_OFF;
#if BLE_CENTRAL
    } else if (!on && bll.state == LL_SCAN) {
        ble_hw_scan_stop();
        bll.state = LL_OFF;
    } else if (!on && bll.state == LL_INIT) {
        ble_hw_init_stop();
        bll.state = LL_OFF;
#endif
    } else if (!on && bll.state == LL_CONN)
        ble_ll_disconnect(BLE_ERR_REMOTE_USER);
}

/* timeout x 10 ms must exceed (1 + latency) x interval x 1.25 ms x 2 */
static int ll_params_ok(uint32_t interval, uint32_t latency, uint32_t timeout)
{
    return interval >= 6u && interval <= 3200u && timeout >= 10u && timeout <= 3200u && latency <= 499u &&
           timeout * 4u > (1u + latency) * interval;
}

/* why a CONNECT_IND is refused (BDR_*, 0: taken): the same tests ble_ll_hw_connect_ind always made, named for blell */
static uint8_t ll_cind_check(const uint8_t *pdu, uint8_t len, const struct ble_hw_conn *c, struct ble_chmap *m)
{
    const uint8_t *p = pdu + 2;
    if (bll.state != LL_ADV)
        return BDR_NOT_ADV;
    if (len < 2u + 34u || (pdu[0] & 0x0Fu) != 0x5u || pdu[1] != 34u)
        return BDR_FORMAT;
    if ((pdu[0] >> 7) != bll.addr_rand)
        return BDR_RXADD;
    if (!ble_eq(p + 6, bll.addr, 6))
        return BDR_ADVA;
    if (!ll_params_ok(c->interval, c->latency, c->timeout))
        return BDR_PARAMS;
    if (c->win_size < 1u || c->win_size > 8u || c->win_size >= c->interval)
        return BDR_WINSIZE;
    if (c->win_offset > c->interval)
        return BDR_WINOFF;
    if (c->hop < 5u || c->hop > 16u)
        return BDR_HOP;
    if (!ble_chmap_set(m, c->chm))
        return BDR_CHM;
    if (c->aa == BLE_ADV_AA)
        return BDR_AA;
    return BDR_OK;
}

#if BLE_DIAG
static void ll_cind_seen(uint8_t hdr, const struct ble_hw_conn *c)
{
    BLE_DG(ble_dg.cind_hdr = hdr);
    BLE_DG(ble_dg.cind_aa = c->aa);
    BLE_DG(ble_dg.cind_crc = c->crc_init);
    BLE_DG(ble_dg.cind_win_size = c->win_size);
    BLE_DG(ble_dg.cind_win_off = c->win_offset);
    BLE_DG(ble_dg.cind_interval = c->interval);
    BLE_DG(ble_dg.cind_latency = c->latency);
    BLE_DG(ble_dg.cind_timeout = c->timeout);
    BLE_DG(ble_cpy(ble_dg.cind_chm, c->chm, 5));
    BLE_DG(ble_dg.cind_hop = c->hop);
    BLE_DG(ble_dg.cind_sca = c->sca);
}
#else
#define ll_cind_seen(hdr, c) ((void)0)
#endif

/* a new connection (central: we are its master): everything after the advertising state starts at zero */
static void ll_conn_begin(const struct ble_hw_conn *c, const uint8_t peer[6], uint8_t peer_rand, uint8_t central)
{
    ble_zero((uint8_t *)&bll.interval, (uint32_t)((uint8_t *)&bll.ring - (uint8_t *)&bll.interval));
    bll.wr = bll.rd = bll.off = 0;
#if BLE_LL_ENC
    bll.enc_rx = bll.enc_tx = bll.tx_paused = 0;
#endif
    ble_cpy(bll.chm, c->chm, 5);
    ble_cpy(bll.peer, peer, 6);
    bll.peer_rand = peer_rand;
    bll.central = central;
    bll.interval = c->interval;
    bll.timeout = c->timeout;
    bll.win_size = c->win_size;
    bll.win_offset = c->win_offset;
    bll.last_evt = 0xFFFFu;                         /* the first event is counter 0 */
    bll.max_tx = bll.max_rx = 27u;
    ble_zero((uint8_t *)bll.peer_len, sizeof bll.peer_len);   /* (its bytes: ble_zero takes a byte pointer) */
    bll.state = LL_CONN;
    bll.t_start = bll.t_rx = ble_hw_time_us();
}

BLE_API int ble_ll_hw_connect_ind(const uint8_t *pdu, uint8_t len)
{
    const uint8_t *p = pdu + 2;
    struct ble_hw_conn c;
    struct ble_chmap m;
    uint8_t why;
    BLE_DG(ble_dg.cind_rx++);
    ble_diag_ev(BDE_CIND_RX, (uint32_t)pdu[0] | (uint32_t)(len > 1u ? pdu[1] : 0u) << 8);
    if (len < 2u + 34u) {
        BLE_DG(ble_dg.cind_rej++);
        BLE_DG(ble_dg.cind_rej_why = BDR_FORMAT);
        ble_diag_ev(BDE_CIND_REJ, BDR_FORMAT);
        return 0;
    }
    c.aa = ble_rd32(p + 12);
    c.crc_init = ble_rd24(p + 16);
    c.win_size = p[19];
    c.win_offset = ble_rd16(p + 20);
    c.interval = ble_rd16(p + 22);
    c.latency = ble_rd16(p + 24);
    c.timeout = ble_rd16(p + 26);
    ble_cpy(c.chm, p + 28, 5);
    c.chm[4] &= 0x1Fu;
    c.hop = p[33] & 0x1Fu;
    c.sca = p[33] >> 5;
    ll_cind_seen(pdu[0], &c);
    why = ll_cind_check(pdu, len, &c, &m);
    if (why != BDR_OK) {
        BLE_DG(ble_dg.cind_rej++);
        BLE_DG(ble_dg.cind_rej_why = why);
        ble_diag_ev(BDE_CIND_REJ, why);
        return 0;
    }
    BLE_DG(ble_dg.cind_ok++);
    BLE_DG(ble_dg.cind_rej_why = BDR_OK);
    ble_diag_ev(BDE_CIND_OK, c.interval);
    ll_conn_begin(&c, p, (uint8_t)(pdu[0] >> 6 & 1u), 0);
    c.latency = 0;                                 /* we listen at every event */
    ble_hw_conn_start(&c);
    ble_host_connected();
    return 1;
}

/* ------------------------------------------------------------------------------------------- TX queues --- */

static int ll_ctrl(uint8_t op, const uint8_t *d, uint8_t n)
{
    uint8_t i;
    if (bll.ctrl_n >= BLE_LL_CTRL_Q)
        return 0;
    i = (uint8_t)((bll.ctrl_rd + bll.ctrl_n) % BLE_LL_CTRL_Q);
    bll.ctrl[i][0] = op;
    ble_cpy(bll.ctrl[i] + 1, d, n);
    bll.ctrl_len[i] = (uint8_t)(n + 1u);
    bll.ctrl_n++;
    ble_hw_tx_kick();
    return 1;
}

static void ll_unknown(uint8_t op) { ll_ctrl(LL_UNKNOWN_RSP, &op, 1); }

/* refuse the central's procedure op: LL_REJECT_EXT_IND when the central knows it, else LL_REJECT_IND */
static void ll_reject(uint8_t op, uint8_t err)
{
    uint8_t d[2];
    d[0] = op;
    d[1] = err;
    if (bll.feat_known && (bll.peer_feat & BLE_FEAT_EXT_REJECT))
        ll_ctrl(LL_REJECT_EXT_IND, d, 2);
    else
        ll_ctrl(LL_REJECT_IND, d + 1, 1);
}

BLE_API uint32_t ble_ll_tx_room(void)
{
    uint32_t used = bll.wr - bll.rd;
    if (bll.state != LL_CONN || used + 4u >= BLE_LL_TX_RING)
        return 0;
    return BLE_LL_TX_RING - used - 4u;
}

BLE_API int ble_ll_send(uint16_t cid, const uint8_t *p, uint16_t n)
{
    uint8_t h[4];
    uint32_t i;
    if (n > ble_ll_tx_room() || bll.lproc == P_TERM)
        return 0;
    ble_wr16(h, n);
    ble_wr16(h + 2, cid);
    for (i = 0; i < 4u; i++)
        bll.ring[(bll.wr + i) & LL_RING_MASK] = h[i];
    for (i = 0; i < n; i++)
        bll.ring[(bll.wr + 4u + i) & LL_RING_MASK] = p[i];
    bll.wr += 4u + n;
    if (cid >= 4u && cid <= 6u)                    /* (blell's protocol ring: ATT, signalling, SMP) */
        ble_diag_pdu((uint8_t)(BDP_TX | (cid - 3u)), p, n);
    ble_hw_tx_kick();
    return 1;
}

static uint32_t ll_head_len(void)                 /* the head frame's octets, header included */
{
    return 4u + (bll.ring[bll.rd & LL_RING_MASK] | (uint32_t)bll.ring[(bll.rd + 1u) & LL_RING_MASK] << 8);
}

static int ll_data_ready(void)
{
#if BLE_LL_ENC
    if (bll.tx_paused)
        return 0;
#endif
    return bll.rd != bll.wr;
}

BLE_API uint8_t ble_ll_hw_tx(uint8_t *pdu)
{
    uint32_t n, i, room = bll.max_tx;               /* (the data length counts the payload without the MIC) */
    if (bll.state != LL_CONN)
        return 0;
    if (bll.ctrl_n) {
        n = bll.ctrl_len[bll.ctrl_rd];
        pdu[0] = 3u;
        ble_cpy(pdu + 2, bll.ctrl[bll.ctrl_rd], n);
        BLE_DG(ble_diag_last(ble_dg.ctl_tx, &ble_dg.ctl_tx_n, pdu[2]));
        ll_diag_enc(1, pdu + 2, (uint8_t)n);
        ble_diag_ev(BDE_CTRL_TX, pdu[2]);
        if (pdu[2] == LL_TERMINATE_IND) {
            bll.term_seq = (uint8_t)(bll.handed + 1u);
            bll.term_out = 1;
        }
        bll.ctrl_rd = (uint8_t)((bll.ctrl_rd + 1u) % BLE_LL_CTRL_Q);
        bll.ctrl_n--;
    } else if (ll_data_ready()) {
        uint32_t total = ll_head_len();
        n = ble_min(total - bll.off, room);
        pdu[0] = bll.off ? 1u : 2u;
        for (i = 0; i < n; i++)
            pdu[2 + i] = bll.ring[(bll.rd + bll.off + i) & LL_RING_MASK];
        bll.off += n;
        if (bll.off == total) {
            bll.rd += total;
            bll.off = 0;
        }
    } else
        return 0;
    if (bll.ctrl_n || ll_data_ready())
        pdu[0] |= 0x10u;                           /* MD: more queued */
    pdu[1] = (uint8_t)n;
#if BLE_LL_ENC
    if (bll.enc_tx) {
        ble_ccm_encrypt(&bll.ccm_tx, pdu[0], pdu + 2, (uint8_t)n);
        pdu[1] = (uint8_t)(n + 4u);
    }
#endif
    bll.handed++;
    return (uint8_t)(2u + pdu[1]);
}

BLE_API int ble_ll_hw_tx_pending(void)
{
    return bll.state == LL_CONN && (bll.ctrl_n || ll_data_ready());
}

BLE_API void ble_ll_hw_tx_acked(void)
{
    if (bll.state != LL_CONN || bll.acked == bll.handed)
        return;
    bll.acked++;
    if (bll.term_out && bll.acked == bll.term_seq)
        ll_close_by(BLE_ERR_LOCAL_HOST, BDC_LOCAL);
}

BLE_API void ble_ll_disconnect(uint8_t reason)
{
    if (bll.state != LL_CONN || bll.lproc == P_TERM)
        return;
    bll.ctrl_n = 0;                                 /* (anything not yet sent goes) */
    bll.rd = bll.wr;
    bll.off = 0;
    bll.lproc = P_TERM;
    bll.lproc_t = ble_hw_time_us();
    ll_ctrl(LL_TERMINATE_IND, &reason, 1);
}

#if BLE_CENTRAL
#include "ble_ll_central.c"          /* scanning, initiating, the master's procedures */
#endif

/* -------------------------------------------------------------------------------- control procedures --- */

static void ll_lengths(const uint8_t *d)          /* the central's MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime */
{
    uint32_t rx_o = ble_rd16(d), rx_t = ble_rd16(d + 2), tx_o = ble_rd16(d + 4), tx_t = ble_rd16(d + 6), i;
    for (i = 0; i < 4u; i++)
        bll.peer_len[i] = ble_rd16(d + 2u * i);    /* (as received: the screen mirror's LINK report) */
    uint32_t rx_by_t = rx_t >= 328u ? rx_t / 8u - 14u : 27u, tx_by_t = tx_t >= 328u ? tx_t / 8u - 14u : 27u;
    rx_o = ble_min(ble_min(rx_o < 27u ? 27u : rx_o, rx_by_t), BLE_LL_MAX_OCTETS);
    tx_o = ble_min(ble_min(tx_o < 27u ? 27u : tx_o, tx_by_t), BLE_LL_MAX_OCTETS);
    bll.max_tx = (uint8_t)rx_o;                     /* we send at most what the central receives */
    bll.max_rx = (uint8_t)tx_o;
    bll.len_done = 1;
    ble_hw_set_lengths(bll.max_tx, bll.max_rx);
}

static void ll_our_lengths(uint8_t *d)
{
    ble_wr16(d, BLE_LL_MAX_OCTETS);
    ble_wr16(d + 2, (BLE_LL_MAX_OCTETS + 14u) * 8u);
    ble_wr16(d + 4, BLE_LL_MAX_OCTETS);
    ble_wr16(d + 6, (BLE_LL_MAX_OCTETS + 14u) * 8u);
}

static void ll_features(uint8_t op, uint8_t peer)
{
    uint8_t d[8];
    ble_zero(d, 8);
    bll.peer_feat = peer;
    bll.feat_known = 1;
    d[0] = (uint8_t)(LL_OUR_FEAT & peer);          /* octet 0: the features both support */
    if (op)
        ll_ctrl(op, d, 8);
}

static int ll_version_send(void)                  /* our LL_VERSION_IND, once per connection -> 1 queued now */
{
    uint8_t d[5];
    if (bll.ver_sent)
        return 0;
    d[0] = BLE_LL_VERSION;
    ble_wr16(d + 1, BLE_LL_COMPANY);
    ble_wr16(d + 3, BLE_LL_SUBVERSION);
    bll.ver_sent = (uint8_t)ll_ctrl(LL_VERSION_IND, d, 5);
    return bll.ver_sent;
}

/* 1 if the instant is still ahead of (or at) the current event: (instant - counter) mod 65536 < 32767 */
static int ll_instant_ok(uint16_t instant)
{
    return (uint16_t)(instant - (uint16_t)(bll.last_evt + 1u)) < 32767u;
}

/* a control PDU from the central (p: opcode and CtrData, n octets) */
static void ll_rx_ctrl(const uint8_t *p, uint8_t n)
{
    static const uint8_t LEN[] = {12, 8, 2, 23, 13, 1, 1, 2, 9, 9, 1, 1, 6, 2, 9, 24, 24, 3, 1, 1, 9, 9, 3, 3, 5, 3};
    uint8_t op = p[0], d[24];
    if (!n)
        return;
    BLE_DG(ble_diag_last(ble_dg.ctl_rx, &ble_dg.ctl_rx_n, op));
    ll_diag_enc(0, p, n);
    ble_diag_ev(BDE_CTRL_RX, op | (uint32_t)n << 8);
    if (op >= sizeof LEN || n != LEN[op]) {        /* unknown, or not its length */
        ll_unknown(op);
        return;
    }
#if BLE_CENTRAL
    if (bll.central && llc_rx_ctrl(op, p, n))         /* the master's side of the procedures (ble_ll_central.c) */
        return;
#endif
    switch (op) {
    case LL_CONNECTION_UPDATE_IND: {
        uint16_t instant = ble_rd16(p + 10);
        if (!ll_instant_ok(instant)) {
            ll_close(BLE_ERR_INSTANT_PASSED);
            return;
        }
        bll.upd.win_size = p[1];
        bll.upd.win_offset = ble_rd16(p + 2);
        bll.upd.interval = ble_rd16(p + 4);
        bll.upd.latency = 0;
        bll.upd.timeout = ble_rd16(p + 8);
        bll.upd.instant = instant;
        if (!ll_params_ok(bll.upd.interval, ble_rd16(p + 6), bll.upd.timeout)) {
            ll_close(BLE_ERR_INVALID_LL_PARAMS);
            return;
        }
        bll.upd_pending = 1;
        ble_hw_conn_update(&bll.upd);
        if (bll.rproc == P_UPD)
            bll.rproc = P_NONE;
        return;
    }
    case LL_CHANNEL_MAP_IND: {
        struct ble_chmap m;
        uint16_t instant = ble_rd16(p + 6);
        if (!ll_instant_ok(instant)) {
            ll_close(BLE_ERR_INSTANT_PASSED);
            return;
        }
        ble_cpy(bll.new_chm, p + 1, 5);
        bll.new_chm[4] &= 0x1Fu;
        if (!ble_chmap_set(&m, bll.new_chm)) {
            ll_close(BLE_ERR_INVALID_LL_PARAMS);
            return;
        }
        bll.chm_instant = instant;
        bll.chm_pending = 1;
        ble_hw_chmap_update(bll.new_chm, instant);
        return;
    }
    case LL_TERMINATE_IND:
        bll.term_rx = 1;                            /* closed after this event: the engine acknowledges it */
        bll.term_reason = p[1];
        return;
    case LL_FEATURE_REQ:
        ll_features(LL_FEATURE_RSP, p[1]);
        return;
    case LL_FEATURE_RSP:
        ll_features(0, p[1]);
        if (bll.lproc == P_FEAT)
            bll.lproc = P_NONE;
        return;
    case LL_VERSION_IND:
        if (bll.lproc == P_VER)
            bll.lproc = P_NONE;                     /* (the answer to ours, as master) */
        ll_version_send();                          /* once per connection */
        return;
    case LL_PING_REQ:
        ll_ctrl(LL_PING_RSP, d, 0);
        return;
    case LL_LENGTH_REQ:
        ll_lengths(p + 1);
        ll_our_lengths(d);
        ll_ctrl(LL_LENGTH_RSP, d, 8);
        return;
    case LL_LENGTH_RSP:
        ll_lengths(p + 1);
        if (bll.lproc == P_LEN)
            bll.lproc = P_NONE;
        return;
    case LL_PHY_REQ:
        d[0] = 1u;                                 /* LE 1M only, both ways */
        d[1] = 1u;
        ll_ctrl(LL_PHY_RSP, d, 2);
        bll.rproc = P_PHY;
        bll.rproc_t = ble_hw_time_us();
        return;
    case LL_PHY_UPDATE_IND:
        if (bll.rproc == P_PHY)
            bll.rproc = P_NONE;
        if ((p[1] | p[2]) & ~1u)                   /* anything but 1M (0: no change): we cannot follow */
            ll_close(BLE_ERR_UNSUPP_REMOTE);
        return;
    case LL_CONNECTION_PARAM_REQ:
        if (!ll_params_ok(ble_rd16(p + 3), ble_rd16(p + 5), ble_rd16(p + 7)) || ble_rd16(p + 1) > ble_rd16(p + 3) ||
            ble_rd16(p + 1) < 6u) {
            ll_reject(op, BLE_ERR_INVALID_LL_PARAMS);
            return;
        }
        if (ble_host_audio() && ble_rd16(p + 3) < BLE_CONN_AUDIO_MIN) {   /* (Bluetooth audio streams: a faster interval
                                                                         * leaves it short of air, ble_host.c) */
            ll_reject(op, BLE_ERR_UNACCEPT_PARAMS);
            return;
        }
        ll_ctrl(LL_CONNECTION_PARAM_RSP, p + 1, 23);   /* accepted as asked */
        bll.rproc = P_UPD;
        bll.rproc_t = ble_hw_time_us();
        return;
    case LL_UNKNOWN_RSP:
    case LL_REJECT_IND:
    case LL_REJECT_EXT_IND:
        if (bll.lproc == P_FEAT || bll.lproc == P_LEN)
            bll.lproc = P_NONE;                     /* the central does not do it: carry on without */
        if (op == LL_UNKNOWN_RSP && p[1] == LL_PERIPHERAL_FEATURE_REQ)
            bll.feat_known = 1;
        if (op == LL_UNKNOWN_RSP && p[1] == LL_LENGTH_REQ)
            bll.len_done = 1;
        return;
#if BLE_LL_ENC
    case LL_ENC_REQ: {
        uint8_t ltk[16], skd[16], i;
        bll.tx_paused = 1;
        ble_hw_rand(d, 12);                        /* SKDs (8), IVs (4) */
        ll_ctrl(LL_ENC_RSP, d, 12);
        if (!ble_host_ltk(p + 1, ble_rd16(p + 9), ltk)) {
            bll.tx_paused = 0;
            ll_reject(op, BLE_ERR_PIN_KEY_MISSING);
            return;
        }
        for (i = 0; i < 8u; i++) {                 /* SKD = SKDs || SKDm, most significant octet first */
            skd[i] = d[7 - i];
            skd[8 + i] = p[11 + 7 - i];
        }
        for (i = 0; i < 16u; i++)
            bll.ccm_rx.key[i] = ltk[15 - i];
        ble_aes128(bll.ccm_rx.key, skd, bll.ccm_rx.key);
        ble_cpy(bll.ccm_rx.iv, p + 19, 4);          /* IV = IVm || IVs */
        ble_cpy(bll.ccm_rx.iv + 4, d + 8, 4);
        bll.ccm_rx.ctr = 0;
        bll.ccm_rx.ctr_hi = 0;
        bll.ccm_tx = bll.ccm_rx;
        bll.ccm_rx.dir = 1;
        bll.ccm_tx.dir = 0;
        ll_ctrl(LL_START_ENC_REQ, d, 0);
        bll.enc_rx = 1;
        bll.rproc = P_ENC;
        bll.rproc_t = ble_hw_time_us();
        return;
    }
    case LL_START_ENC_RSP:
        if (bll.rproc != P_ENC || !bll.enc_rx)
            return;
        bll.enc_tx = 1;
        ll_ctrl(LL_START_ENC_RSP, d, 0);
        bll.tx_paused = 0;
        bll.rproc = P_NONE;
        BLE_DG(ble_dg.enc_on_n++);
        ble_host_encrypted();                       /* (SMP: the key distribution may start) */
        return;
#else
    case LL_ENC_REQ:
        ll_reject(op, BLE_ERR_UNSUPP_REMOTE);
        return;
#endif
    default:                                       /* peripheral-to-central PDUs, PAUSE_ENC, ... */
        ll_unknown(op);
        return;
    }
}

BLE_API void ble_ll_hw_rx(const uint8_t *pdu, uint8_t len)
{
    uint8_t n, llid;
    const uint8_t *p = pdu + 2;
    if (bll.state != LL_CONN || len < 2u || pdu[1] > len - 2u)
        return;
    n = pdu[1];
    llid = pdu[0] & 3u;
    bll.established = 1;
#if BLE_LL_ENC
    if (bll.enc_rx && n) {
        if (n > 4u + BLE_LL_MAX_OCTETS)
            return;
        ble_cpy(bll.rxbuf, p, n);
        if (!ble_ccm_decrypt(&bll.ccm_rx, pdu[0], bll.rxbuf, n)) {
            ll_close(BLE_ERR_MIC_FAILURE);
            return;
        }
        p = bll.rxbuf;
        n = (uint8_t)(n - 4u);
    }
#endif
    if (n > BLE_LL_MAX_OCTETS)
        return;
    if (llid == 3u)
        ll_rx_ctrl(p, n);
    else if (llid && n && bll.lproc != P_TERM)
        ble_host_rx(p, n, llid == 2u);
}

/* our own procedures, one at a time, once the link is up: the feature exchange, then the data length */
static void ll_start_procs(uint32_t now)
{
    uint8_t d[8];
    if (bll.lproc != P_NONE || !bll.established)
        return;
#if BLE_CENTRAL
    if (bll.central) {
        llc_start_procs(now);                      /* as master: version, features, encryption, data length */
        return;
    }
#endif
    if (BLE_LL_MAX_OCTETS > 27u && !bll.len_done && (!bll.feat_known || (bll.peer_feat & BLE_FEAT_DLE)) &&
        (uint16_t)(bll.last_evt + 1u) >= 6u) {
        ll_our_lengths(d);                         /* long packets asked for first, before the feature exchange (the screen
                                                    * mirror over Bluetooth ran at 27 octets an event, 2026-10-23); a central
                                                    * without them answers LL_UNKNOWN_RSP: len_done */
        if (ll_ctrl(LL_LENGTH_REQ, d, 8)) {
            bll.lproc = P_LEN;
            bll.lproc_t = now;
        }
    } else if (!bll.feat_known && BLE_LL_PERIPH_FEAT && (uint16_t)(bll.last_evt + 1u) >= 6u) {
        ble_zero(d, 8);
        d[0] = (uint8_t)LL_OUR_FEAT;
        if (ll_ctrl(LL_PERIPHERAL_FEATURE_REQ, d, 8)) {
            bll.lproc = P_FEAT;
            bll.lproc_t = now;
        }
    } else if (BLE_LL_MAX_OCTETS > 27u && bll.feat_known && !bll.len_done && (bll.peer_feat & BLE_FEAT_DLE)) {
        ll_our_lengths(d);
        if (ll_ctrl(LL_LENGTH_REQ, d, 8)) {
            bll.lproc = P_LEN;
            bll.lproc_t = now;
        }
    }
}

BLE_API void ble_ll_hw_event_end(uint16_t counter, uint8_t rx_ok)
{
    uint32_t now = ble_hw_time_us();
    if (bll.state != LL_CONN)
        return;
    bll.last_evt = counter;
    if (rx_ok) {
        bll.t_rx = now;
        bll.established = 1;
    }
    if (bll.term_rx) {
        ll_close_by(bll.term_reason, BDC_PEER);
        return;
    }
    if (bll.upd_pending && (uint16_t)(counter - bll.upd.instant) < 0x8000u) {
        bll.upd_pending = 0;                        /* the instant's event is over: the new parameters hold */
        bll.interval = bll.upd.interval;
        bll.timeout = bll.upd.timeout;
        bll.t_rx = now;                             /* (the supervision timer starts again with them) */
        if (bll.lproc == P_UPD)
            bll.lproc = P_NONE;                     /* (ours, as master: done at its instant) */
    }
    if (bll.chm_pending && (uint16_t)(counter - bll.chm_instant) < 0x8000u) {
        bll.chm_pending = 0;
        ble_cpy(bll.chm, bll.new_chm, 5);
        if (bll.lproc == P_UPD)
            bll.lproc = P_NONE;
    }
    if (!bll.established) {                         /* six intervals after the transmit window, nothing heard */
        if (now - bll.t_start > (1u + bll.win_offset + bll.win_size + 6u * (uint32_t)bll.interval) * 1250u) {
            ll_close_by(BLE_ERR_CONN_FAILED, BDC_ESTABLISH);
            return;
        }
    } else if (now - bll.t_rx > (uint32_t)bll.timeout * 10000u) {
        ll_close_by(BLE_ERR_CONN_TIMEOUT, BDC_SUPERVISION);
        return;
    }
    if (bll.lproc == P_TERM && now - bll.lproc_t > (uint32_t)bll.timeout * 10000u) {
        ll_close_by(BLE_ERR_LOCAL_HOST, BDC_TERM_UNACKED);   /* our LL_TERMINATE_IND never acknowledged */
        return;
    }
    if ((bll.lproc != P_NONE && bll.lproc != P_TERM && now - bll.lproc_t > LL_PROC_US) ||
        (bll.rproc != P_NONE && now - bll.rproc_t > LL_PROC_US)) {
        ll_close_by(BLE_ERR_LL_RSP_TIMEOUT, BDC_PROC_TIMEOUT);
        return;
    }
    ll_start_procs(now);
    if (bll.lproc != P_TERM)
        ble_host_event();
}
