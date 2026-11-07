/* SPDX-License-Identifier: GPL-3.0-only */
/* The link layer's central role (BLE_CENTRAL; Core Specification Vol 6 Part B, docs/BLE-STACK.md §9.1, HW §21),
 * part of ble_ll.c (included there: it shares bll and the TX queues):
 *   - scanning for the DEVICES list: the raw reports' ring between the RX interrupt and the main loop (4.4.3);
 *   - initiating: our CONNECT_IND built before the engine starts (2.3.3.1; the access address by 2.1.2, a random
 *     CRCInit, WinSize 2, WinOffset in [Interval / 2, Interval - 1], Hop 5..16, all 37 channels), the engine sends it
 *     to the one target (HW §21.3), the driver turns the link into the master (ble_ll_hw_master_start);
 *   - the master's side of the control procedures (5.1): we start the version exchange, the feature exchange, the
 *     encryption (LL_ENC_REQ with a bond's or a pairing's key) and the data length; we answer the peripheral's
 *     feature exchange, its connection parameters request (with an LL_CONNECTION_UPDATE_IND at an instant 8..11
 *     events ahead, HW §21.4), its PHY request (no change: 1M only); connection and channel map updates we start
 *     (ble_ll_conn_update, ble_ll_chmap_update). The rest (TX / RX, acknowledgement, supervision, termination) is
 *     the peripheral's code: the engine's TX and RX rules are the same in states 6 and 7 (HW §21.4). */

/* (defined further down ble_ll.c) */
static void ll_features(uint8_t op, uint8_t peer);
static void ll_lengths(const uint8_t *d);
static void ll_our_lengths(uint8_t *d);
static int ll_version_send(void);
static void ll_rx_ctrl(const uint8_t *p, uint8_t n);

/* ------------------------------------------------------------------------------------------- scanning --- */

#define LL_REP_MAX (2u + 37u)
static struct {
    uint8_t pdu[BLE_SCAN_RING][LL_REP_MAX], len[BLE_SCAN_RING];
    uint16_t rssi[BLE_SCAN_RING];
    volatile uint32_t w, r;                        /* free-running: w the RX interrupt's, r the main loop's */
} bscan;

static void ll_scan_start(void)
{
    struct ble_hw_scan s;
    s.interval = BLE_SCAN_INTERVAL;
    s.window = BLE_SCAN_WINDOW;
    s.active = BLE_SCAN_ACTIVE;
    s.own = bll.addr;
    s.own_rand = bll.addr_rand;
    bll.state = LL_SCAN;
    ble_hw_scan_start(&s);
}

BLE_API int ble_ll_scanning(void) { return bll.state == LL_SCAN; }

/* RX interrupt: only ADV_IND and SCAN_RSP can make a list entry (ble_scan.c); the rest is dropped here */
BLE_API void ble_ll_hw_adv_report(const uint8_t *pdu, uint8_t len, uint16_t rssi, uint8_t ch)
{
    uint32_t i, t = pdu[0] & 0x0Fu;
    (void)ch;
    if (bll.state != LL_SCAN || len < 8u || len > LL_REP_MAX)
        return;
    if (t != 0x0u && t != 0x4u) {
        BLE_DG(ble_dgs.rep_other++);
        return;
    }
    if (bscan.w - bscan.r >= BLE_SCAN_RING) {
        BLE_DG(ble_dgs.ring_full++);
        return;
    }
    if (t == 0x0u)
        BLE_DG(ble_dgs.rep_adv_ind++);
    else
        BLE_DG(ble_dgs.rep_scan_rsp++);
    i = bscan.w & (BLE_SCAN_RING - 1u);
    ble_cpy(bscan.pdu[i], pdu, len);
    bscan.len[i] = len;
    bscan.rssi[i] = rssi;
    BLE_BARRIER();                                 /* the slot before the index */
    bscan.w++;
}

BLE_API uint8_t ble_ll_scan_take(uint8_t *pdu, uint16_t *rssi)
{
    uint32_t i;
    uint8_t n;
    if (bscan.r == bscan.w)
        return 0;
    BLE_BARRIER();                                 /* the index before the slot */
    i = bscan.r & (BLE_SCAN_RING - 1u);
    n = bscan.len[i];
    ble_cpy(pdu, bscan.pdu[i], n);
    *rssi = bscan.rssi[i];
    BLE_BARRIER();
    bscan.r++;
    return n;
}

BLE_API void ble_ll_scan(int on)
{
    bll.scan_want = on ? 1u : 0u;
    if (on && bll.enabled && bll.state == LL_ADV) {
        ble_hw_adv_stop();                         /* one link: advertising stops, the scan takes the link */
        ll_scan_start();
    } else if (on && bll.enabled && bll.state == LL_OFF)
        ll_scan_start();
    else if (!on && bll.state == LL_SCAN) {
        ble_hw_scan_stop();
        bll.state = LL_OFF;
        if (bll.enabled && !bll.adv_off)
            ll_adv_start();
    }                                              /* (initiating or connected: only the wish is kept) */
}

/* ----------------------------------------------------------------------------------------- initiating --- */

static struct {
    uint8_t cind[2 + 34];                          /* our CONNECT_IND, as the engine sends it */
    struct ble_hw_conn c;                          /* its LLData */
    uint8_t peer[6], peer_rand;                    /* the target (its AdvA as heard: an RPA stays an RPA) */
    uint8_t upd_want;                              /* the peripheral asked for these: an update when we are free */
    uint16_t upd_interval, upd_timeout;
#if BLE_CEN_PAIR
    uint8_t enc_want, ltk[16], rand[8], skdm[8], ivm[4];   /* ble_ll_start_enc: LL_ENC_REQ when we are free */
    uint16_t ediv;
    uint8_t pausing;                               /* our LL_PAUSE_ENC_REQ is out (a new key on an encrypted link) */
    uint8_t held[27], held_n;                      /* the peripheral's control PDU held while our encryption starts */
#endif
} llc;

static uint32_t llc_rand32(void)
{
    uint8_t b[4];
    ble_hw_rand(b, 4);
    return ble_rd32(b);
}

BLE_API int ble_ll_initiating(void) { return bll.state == LL_INIT; }
BLE_API int ble_ll_central(void) { return bll.state == LL_CONN && bll.central; }

/* our connection's parameters (HW §21.3, ble_cfg.h BLE_CENTRAL_*), and the CONNECT_IND that carries them */
static void llc_cind(const uint8_t peer[6], uint8_t peer_rand)
{
    static const uint8_t ALL[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
    struct ble_hw_conn *c = &llc.c;
    uint8_t *p = llc.cind;
    uint32_t r, k, iv = BLE_CENTRAL_INTERVAL;
    for (k = 1, c->aa = llc_rand32(); !ble_aa_valid(c->aa); k++)   /* the Core's rules (2.1.2), in ble_prim.c */
        c->aa = llc_rand32() ^ k * 0x9E3779B1u;    /* (stirred: a generator stuck on one value still ends) */
    c->crc_init = llc_rand32() & 0xFFFFFFu;        /* random, as the Core asks (the vendor keeps 0x1983AE) */
    r = llc_rand32();
    c->win_size = BLE_CENTRAL_WINSIZE;
    c->win_offset = (uint16_t)(iv / 2u + r % (iv - iv / 2u));   /* [Interval / 2, Interval - 1], as the vendor */
    c->interval = (uint16_t)iv;
    c->latency = 0;
    c->timeout = BLE_CENTRAL_TIMEOUT;
    ble_cpy(c->chm, ALL, 5);
    c->hop = (uint8_t)(5u + (r >> 8) % 12u);       /* 5..16 */
    c->sca = BLE_CENTRAL_SCA;
    p[0] = (uint8_t)(0x05u | bll.addr_rand << 6 | (peer_rand & 1u) << 7);   /* CONNECT_IND, TxAdd, RxAdd, ChSel 0 */
    p[1] = 34;
    ble_cpy(p + 2, bll.addr, 6);                   /* InitA */
    ble_cpy(p + 8, peer, 6);                       /* AdvA */
    p[14] = (uint8_t)c->aa, p[15] = (uint8_t)(c->aa >> 8), p[16] = (uint8_t)(c->aa >> 16), p[17] = (uint8_t)(c->aa >> 24);
    p[18] = (uint8_t)c->crc_init, p[19] = (uint8_t)(c->crc_init >> 8), p[20] = (uint8_t)(c->crc_init >> 16);
    p[21] = c->win_size;
    ble_wr16(p + 22, c->win_offset);
    ble_wr16(p + 24, c->interval);
    ble_wr16(p + 26, c->latency);
    ble_wr16(p + 28, c->timeout);
    ble_cpy(p + 30, c->chm, 5);
    p[35] = (uint8_t)(c->hop | c->sca << 5);
}

/* start initiating to peer (its AdvA and TxAdd): advertising or scanning stops (one link). 0: not now (off, or a
 * connection is up) */
BLE_API int ble_ll_connect(const uint8_t peer[6], uint8_t peer_rand)
{
    struct ble_hw_init in;
    if (!bll.enabled || bll.state == LL_CONN)
        return 0;
    if (bll.state == LL_ADV)
        ble_hw_adv_stop();
    else if (bll.state == LL_SCAN)
        ble_hw_scan_stop();
    else if (bll.state == LL_INIT)
        ble_hw_init_stop();
    llc_cind(peer, peer_rand);
    ble_cpy(llc.peer, peer, 6);
    llc.peer_rand = (uint8_t)(peer_rand & 1u);
    in.interval = BLE_INIT_INTERVAL;
    in.window = BLE_INIT_WINDOW;
    in.cind = llc.cind;
    in.conn = llc.c;
    bll.state = LL_INIT;
    BLE_DG(ble_dgc.connects++);
    ble_diag_ev(BDE_INIT_START, llc.c.win_offset);
    ble_hw_init_start(&in);
    return 1;
}

/* stop initiating (the caller's timeout, NONE picked): advertising or the scan again */
BLE_API void ble_ll_connect_cancel(void)
{
    if (bll.state != LL_INIT)
        return;
    ble_hw_init_stop();
    bll.state = LL_OFF;
    BLE_DG(ble_dgc.cancels++);
    ll_idle();
}

/* the driver: the engine sent our CONNECT_IND and the link is the master now (event counter 0 is the first anchor) */
BLE_API void ble_ll_hw_master_start(void)
{
    if (bll.state != LL_INIT)
        return;
    ll_conn_begin(&llc.c, llc.peer, llc.peer_rand, 1);
    llc.upd_want = 0;
#if BLE_CEN_PAIR
    llc.enc_want = llc.pausing = llc.held_n = 0;
#endif
    ble_diag_ev(BDE_MASTER, llc.c.interval);
    ble_host_connected();
}

/* ------------------------------------------------------------------------------- the master's procedures --- */

/* an LL_CONNECTION_UPDATE_IND from us: WinSize 1, WinOffset = Interval / 2 and the instant 8..11 events ahead (the
 * vendor's plain case, HW §21.4); the driver writes the master's values at instant - 1 */
static int llc_update(uint16_t interval, uint16_t timeout, uint32_t now)
{
    uint8_t d[11];
    uint16_t instant = (uint16_t)(bll.last_evt + 8u + llc_rand32() % 4u);
    if (bll.lproc != P_NONE || bll.upd_pending || bll.chm_pending)
        return 0;
    d[0] = 1;
    ble_wr16(d + 1, interval / 2u);
    ble_wr16(d + 3, interval);
    ble_wr16(d + 5, 0);
    ble_wr16(d + 7, timeout);
    ble_wr16(d + 9, instant);
    if (!ll_ctrl(LL_CONNECTION_UPDATE_IND, d, 11))
        return 0;
    bll.upd.win_size = 1;
    bll.upd.win_offset = (uint16_t)(interval / 2u);
    bll.upd.interval = interval;
    bll.upd.latency = 0;
    bll.upd.timeout = timeout;
    bll.upd.instant = instant;
    bll.upd_pending = 1;
    bll.lproc = P_UPD;                             /* (done at the instant: ble_ll_hw_event_end) */
    bll.lproc_t = now;
    ble_hw_conn_update(&bll.upd);
    BLE_DG(ble_dgc.m_upd_tx++);
    return 1;
}

/* as master: move to a new interval in [imin, imax] (ours when it fits) with this supervision timeout -> 1 started,
 * 0 not valid or not now (another procedure: kept for later when it came from the peripheral) */
BLE_API int ble_ll_conn_update(uint16_t imin, uint16_t imax, uint16_t timeout)
{
    uint16_t iv = (uint16_t)(BLE_CENTRAL_INTERVAL < imin ? imin : BLE_CENTRAL_INTERVAL > imax ? imax : BLE_CENTRAL_INTERVAL);
    if (!ble_ll_central() || imin < 6u || imin > imax || !ll_params_ok(iv, 0, timeout))
        return 0;
    if (llc_update(iv, timeout, ble_hw_time_us()))
        return 1;
    llc.upd_want = 1;                              /* busy: when the current procedure is over */
    llc.upd_interval = iv;
    llc.upd_timeout = timeout;
    return 0;
}

/* as master: a new channel map from the event counter + 6..9 on (HW §21.4) -> 1 started */
BLE_API int ble_ll_chmap_update(const uint8_t chm[5])
{
    struct ble_chmap m;
    uint8_t d[7];
    uint16_t instant = (uint16_t)(bll.last_evt + 7u + llc_rand32() % 4u);
    if (!ble_ll_central() || bll.lproc != P_NONE || bll.upd_pending || bll.chm_pending)
        return 0;
    ble_cpy(d, chm, 5);
    d[4] &= 0x1Fu;
    if (!ble_chmap_set(&m, d))
        return 0;
    ble_wr16(d + 5, instant);
    if (!ll_ctrl(LL_CHANNEL_MAP_IND, d, 7))
        return 0;
    ble_cpy(bll.new_chm, d, 5);
    bll.chm_instant = instant;
    bll.chm_pending = 1;
    bll.lproc = P_UPD;
    bll.lproc_t = ble_hw_time_us();
    ble_hw_chmap_update(bll.new_chm, instant);
    BLE_DG(ble_dgc.m_chm_tx++);
    return 1;
}

#if BLE_CEN_PAIR
/* as master: encrypt the link with ltk (a bond's, EDIV / Rand as the peripheral handed them out; or a pairing's STK,
 * EDIV 0 / Rand 0), at the first moment no other procedure of ours runs. On a link encrypted already (a pairing
 * again, with MITM, on it) the encryption is paused first (5.1.3.2), then started with the new
 * key. Done:
 * ble_host_encrypted(); refused: ble_host_enc_failed() */
BLE_API void ble_ll_start_enc(const uint8_t ltk[16], const uint8_t rand[8], uint16_t ediv)
{
    if (!ble_ll_central())
        return;
    ble_cpy(llc.ltk, ltk, 16);
    ble_cpy(llc.rand, rand, 8);
    llc.ediv = ediv;
    llc.enc_want = 1;
}

static int llc_enc_req(uint32_t now)
{
    uint8_t d[22];
    ble_cpy(d, llc.rand, 8);
    ble_wr16(d + 8, llc.ediv);
    ble_hw_rand(llc.skdm, 8);
    ble_hw_rand(llc.ivm, 4);
    ble_cpy(d + 10, llc.skdm, 8);
    ble_cpy(d + 18, llc.ivm, 4);
    if (!ll_ctrl(LL_ENC_REQ, d, 22))
        return 0;
    llc.enc_want = 0;
    bll.enc_rx = bll.enc_tx = 0;
    bll.tx_paused = 1;                             /* no data until the encryption runs (5.1.3.1) */
    bll.lproc = P_ENC;
    bll.lproc_t = now;
    BLE_DG(ble_dgc.m_enc_req_tx++);
    return 1;
}

/* the encryption pause (Core Vol 6 Part B 5.1.3.2), the master's side: our LL_PAUSE_ENC_REQ (encrypted), the
 * peripheral's LL_PAUSE_ENC_RSP (encrypted), then ours unencrypted and both directions plain; the encryption start
 * with the new key follows at once (LL_ENC_REQ). No data meanwhile */
static int llc_pause_req(uint32_t now)
{
    if (!ll_ctrl(LL_PAUSE_ENC_REQ, llc.ivm, 0))
        return 0;
    llc.enc_want = 0;
    llc.pausing = 1;
    bll.tx_paused = 1;
    bll.lproc = P_ENC;
    bll.lproc_t = now;
    BLE_DG(ble_dgc.m_pause_tx++);
    return 1;
}

static void llc_enc_failed(uint8_t err);

static void llc_pause_rsp(void)
{
    uint8_t d[1];
    llc.pausing = 0;
    BLE_DG(ble_dgc.m_pause_rsp_rx++);
    bll.enc_rx = bll.enc_tx = 0;                   /* (ours goes unencrypted, and all after it) */
    if (!ll_ctrl(LL_PAUSE_ENC_RSP, d, 0) || !llc_enc_req(ble_hw_time_us()))
        llc_enc_failed(BLE_ERR_UNSPECIFIED);
}

/* LL_ENC_RSP: SK = e(LTK, SKDs || SKDm), IV = IVm || IVs; directionBit 1 for what we send */
static void llc_enc_rsp(const uint8_t *p)
{
    uint8_t skd[16], i;
    for (i = 0; i < 8u; i++) {                     /* most significant octet first */
        skd[i] = p[1 + 7 - i];
        skd[8 + i] = llc.skdm[7 - i];
    }
    for (i = 0; i < 16u; i++)
        bll.ccm_tx.key[i] = llc.ltk[15 - i];
    ble_aes128(bll.ccm_tx.key, skd, bll.ccm_tx.key);
    ble_cpy(bll.ccm_tx.iv, llc.ivm, 4);
    ble_cpy(bll.ccm_tx.iv + 4, p + 9, 4);
    bll.ccm_tx.ctr = 0;
    bll.ccm_tx.ctr_hi = 0;
    bll.ccm_rx = bll.ccm_tx;
    bll.ccm_tx.dir = 1;
    bll.ccm_rx.dir = 0;
    BLE_DG(ble_dgc.m_enc_rsp_rx++);
}

static void llc_enc_failed(uint8_t err)
{
    bll.lproc = P_NONE;
    bll.tx_paused = 0;
    if (llc.pausing)
        llc.pausing = 0;                           /* (the pause refused: still encrypted with the old key) */
    else
        bll.enc_rx = bll.enc_tx = 0;
    BLE_DG(ble_dgc.m_enc_rej++);
    BLE_DG(ble_dgc.m_enc_rej_err = err);
    ble_host_enc_failed(err);
}
#endif

/* our procedures as master, one at a time once the link is up: the version exchange, the feature exchange, the
 * encryption the host asked for, an update the peripheral asked for, the data length */
static void llc_start_procs(uint32_t now)
{
    uint8_t d[8];
#if BLE_CEN_PAIR
    if (llc.held_n) {                              /* the peripheral's procedure held during our encryption start */
        d[0] = llc.held_n;
        llc.held_n = 0;
        ll_rx_ctrl(llc.held, d[0]);
    }
#endif
    if (!bll.ver_sent) {
        if (ll_version_send()) {
            bll.lproc = P_VER;
            bll.lproc_t = now;
        }
        return;
    }
    if (!bll.feat_known) {
        ble_zero(d, 8);
        d[0] = (uint8_t)LL_OUR_FEAT;
        if (ll_ctrl(LL_FEATURE_REQ, d, 8)) {
            bll.lproc = P_FEAT;
            bll.lproc_t = now;
        }
        return;
    }
    if (BLE_LL_MAX_OCTETS > 27u && !bll.len_done && (bll.peer_feat & BLE_FEAT_DLE)) {
        ll_our_lengths(d);                         /* (before the encryption: else the iPhone sends its own
                                                    * LL_LENGTH_REQ across our LL_ENC_REQ, blell-dev6) */
        if (ll_ctrl(LL_LENGTH_REQ, d, 8)) {
            bll.lproc = P_LEN;
            bll.lproc_t = now;
        }
        return;
    }
#if BLE_CEN_PAIR
    if (llc.enc_want) {
        if (bll.enc_tx)
            llc_pause_req(now);                    /* a new key on an encrypted link: paused first */
        else
            llc_enc_req(now);
        return;
    }
#endif
    if (llc.upd_want && llc_update(llc.upd_interval, llc.upd_timeout, now))
        llc.upd_want = 0;
}

/* a control PDU from the peripheral that the master handles otherwise -> 1 handled (else ble_ll.c's own code). While
 * our encryption starts (from our LL_ENC_REQ or LL_PAUSE_ENC_REQ to its LL_START_ENC_RSP) we send nothing but that
 * procedure's PDUs (Core Vol 6 Part B 5.1.3.1): a procedure the peripheral starts meanwhile is held, answered once the
 * link is encrypted (the iPhone left with MIC Failure 0x3D when our LL_LENGTH_RSP came then, blell-dev6) */
static int llc_rx_ctrl(uint8_t op, const uint8_t *p, uint8_t n)
{
    uint8_t d[8];
#if BLE_CEN_PAIR
    if (bll.lproc == P_ENC && op != LL_ENC_RSP && op != LL_START_ENC_REQ && op != LL_START_ENC_RSP &&
        op != LL_PAUSE_ENC_RSP && op != LL_REJECT_IND && op != LL_REJECT_EXT_IND && op != LL_UNKNOWN_RSP &&
        op != LL_TERMINATE_IND && n <= sizeof llc.held) {
        ble_cpy(llc.held, p, n);
        llc.held_n = n;
        BLE_DG(ble_dgc.m_held++);
        return 1;
    }
#else
    (void)n;
#endif
    switch (op) {
    case LL_CONNECTION_UPDATE_IND:                 /* a master's PDUs: a peripheral never sends them */
    case LL_CHANNEL_MAP_IND:
    case LL_ENC_REQ:
    case LL_PHY_UPDATE_IND:
    case LL_FEATURE_REQ:
        ll_unknown(op);
        return 1;
    case LL_VERSION_IND:
        BLE_DG(ble_dgc.m_ver_rx++);
        return 0;
    case LL_FEATURE_RSP:
        BLE_DG(ble_dgc.m_feat_rsp++);
        return 0;
    case LL_LENGTH_RSP:
        BLE_DG(ble_dgc.m_len_done++);
        return 0;
    case LL_PERIPHERAL_FEATURE_REQ:                /* the peripheral starts the feature exchange */
        ll_features(LL_FEATURE_RSP, p[1]);
        return 1;
    case LL_PHY_REQ:                               /* 1M only: LL_PHY_UPDATE_IND with no change (5.1.10) */
        BLE_DG(ble_dgc.m_phy_req_rx++);
        ble_zero(d, 4);
        ll_ctrl(LL_PHY_UPDATE_IND, d, 4);
        return 1;
    case LL_CONNECTION_PARAM_REQ:                  /* answered with our LL_CONNECTION_UPDATE_IND (5.1.7.2) */
        BLE_DG(ble_dgc.m_param_req_rx++);
        if (!ll_params_ok(ble_rd16(p + 3), ble_rd16(p + 5), ble_rd16(p + 7)) || ble_rd16(p + 1) > ble_rd16(p + 3) ||
            ble_rd16(p + 1) < 6u) {
            ll_reject(op, BLE_ERR_INVALID_LL_PARAMS);
            return 1;
        }
        ble_ll_conn_update(ble_rd16(p + 1), ble_rd16(p + 3), ble_rd16(p + 7));
        return 1;
    case LL_CONNECTION_PARAM_RSP:                  /* (we never ask) */
        return 1;
#if BLE_CEN_PAIR
    case LL_ENC_RSP:
        if (bll.lproc == P_ENC)
            llc_enc_rsp(p);
        return 1;
    case LL_PAUSE_ENC_RSP:                         /* (encrypted): ours plain, then the start with the new key */
        if (bll.lproc == P_ENC && llc.pausing)
            llc_pause_rsp();
        return 1;
    case LL_START_ENC_REQ:                         /* (sent plain; our answer and everything after: encrypted) */
        if (bll.lproc != P_ENC)
            return 1;
        BLE_DG(ble_dgc.m_start_enc_rx++);
        bll.enc_rx = bll.enc_tx = 1;
        ll_ctrl(LL_START_ENC_RSP, d, 0);
        return 1;
    case LL_START_ENC_RSP:
        if (bll.lproc != P_ENC || !bll.enc_rx)
            return 1;
        bll.lproc = P_NONE;
        bll.tx_paused = 0;
        BLE_DG(ble_dg.enc_on_n++);
        BLE_DG(ble_dgc.m_enc_on++);
        ble_host_encrypted();
        return 1;
    case LL_REJECT_IND:
        if (bll.lproc != P_ENC)
            return 0;
        llc_enc_failed(p[1]);
        return 1;
    case LL_REJECT_EXT_IND:
        if (bll.lproc != P_ENC || (p[1] != LL_ENC_REQ && p[1] != LL_PAUSE_ENC_REQ))
            return 0;
        llc_enc_failed(p[2]);
        return 1;
    case LL_UNKNOWN_RSP:
        if (bll.lproc != P_ENC || (p[1] != LL_ENC_REQ && p[1] != LL_PAUSE_ENC_REQ))
            return 0;
        llc_enc_failed(BLE_ERR_UNSUPP_REMOTE);
        return 1;
#endif
    default:
        return 0;
    }
}
