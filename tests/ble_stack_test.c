/* SPDX-License-Identifier: GPL-3.0-only */
/* End-to-end host test of the BLE stack (firmware/src/ble/: link layer, L2CAP, signalling, SMP, ATT / GATT,
 * BLE-MIDI) driven by a simulated central through a fake baseband (ble_hw.h). One connection event: the
 * central's queued PDUs are delivered, then the peripheral's are taken (all acknowledged), then the event ends.
 *
 *   advertising   ADV_IND (AdvA, flags, the BLE-MIDI UUID), SCAN_RSP (the name); CONNECT_IND checks: another
 *                 AdvA, a bad hop, interval, window, channel map, the advertising AA: ignored
 *   LL            version (once), feature exchange (octet 0 the AND), our own feature exchange when the central
 *                 is silent, data length (both ways, the driver told), PHY 1M only, connection parameters
 *                 request accepted / rejected, ping, unknown and short opcodes, encryption refused
 *                 (BLE_LL_ENC=0) or the Core spec's sample encryption start (BLE_LL_ENC=1: SK, the central's
 *                 START_ENC_RSP 0F 05 9F CD A7 F4 48, our data packet = the sample's)
 *   GATT          MTU 517 -> 247, discovery as CoreBluetooth runs it (primary services, characteristics,
 *                 descriptors), reads, blobs, errors, no Service Changed indication on subscription (blell10-12)
 *   MIDI          CCCD on -> L2CAP connection parameter request {6, 9, 0, 100}, rejected -> {12, 12}; the
 *                 central's connection update with an instant: the driver told, the LL moves at the instant;
 *                 notifications with real timestamps; a write in, decoded with its timestamps
 *   channel map   with an instant; an instant already passed: connection lost (0x28)
 *   endings       the central's TERMINATE_IND, ours (acknowledged), supervision timeout, no packet in six
 *                 intervals (0x3E), a 40 s procedure timeout (0x22); advertising again after each
 *   L2CAP         frames over many fragments both ways, Command Reject for unknown signalling; blell's protocol
 *                 ring; SMP (BLE_SMP_LEGACY=1: a Mac-like legacy Just Works pairing, the bond on reconnection), else Pairing Not
 *                 Supported to a Pairing Request */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/ble/ble_prim.c"
#include "../firmware/src/ble/ble_aes.c"
#include "../firmware/src/ble/ble_ll.c"
#include "../firmware/src/ble/ble_smp.c"
#include "../firmware/src/ble/ble_host.c"
#include "../firmware/src/ble/ble_att.c"
#include "../firmware/src/ble/ble_midi.c"
#include "../firmware/src/ble/ble_diag.c"
#include "../firmware/src/ble/ble_store.c"   /* (the stack's plain parts: built here too) */
#if BLE_CENTRAL
#include "../firmware/src/ble/ble_scan.c"
#include "../firmware/src/ble/ble_gattc.c"
#include "../firmware/src/ble/ble_central.c"
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ------------------------------------------------------------------------------------- the fake driver */
static struct {
    int adv_on, adv_starts, conn_on, conn_starts, conn_stops, upd_n, chm_n, kicks;
    uint8_t adv[39], adv_len, sr[39], sr_len;
    struct ble_hw_adv a;
    struct ble_hw_conn conn;
    struct ble_hw_conn_upd upd;
    uint8_t chm[5];
    uint16_t chm_instant;
    uint8_t max_tx, max_rx;
    uint8_t rnd[16];
} hw;
static uint32_t now_us = 1000000;

void ble_hw_adv_start(const struct ble_hw_adv *a)
{
    hw.adv_on = 1;
    hw.adv_starts++;
    hw.a = *a;
    memcpy(hw.adv, a->adv, a->adv_len);
    hw.adv_len = a->adv_len;
    memcpy(hw.sr, a->scan_rsp, a->scan_rsp_len);
    hw.sr_len = a->scan_rsp_len;
}
void ble_hw_adv_stop(void) { hw.adv_on = 0; }
void ble_hw_conn_start(const struct ble_hw_conn *c)
{
    hw.adv_on = 0;
    hw.conn_on = 1;
    hw.conn_starts++;
    hw.conn = *c;
    hw.max_tx = hw.max_rx = 27;
}
void ble_hw_conn_stop(void)
{
    hw.conn_on = 0;
    hw.conn_stops++;
}
#if BLE_CENTRAL
static struct {
    int on, starts, stops;
    struct ble_hw_scan s;
    uint8_t own[6];
} hws;
void ble_hw_scan_start(const struct ble_hw_scan *s)
{
    hws.on = 1;
    hws.starts++;
    hws.s = *s;
    memcpy(hws.own, s->own, 6);
}
void ble_hw_scan_stop(void)
{
    hws.on = 0;
    hws.stops++;
}
void ble_hw_init_start(const struct ble_hw_init *i) { (void)i; }
void ble_hw_init_stop(void) {}
void ble_app_central_keys(const struct ble_keys *k) { (void)k; }
#endif
void ble_hw_conn_update(const struct ble_hw_conn_upd *u)
{
    hw.upd = *u;
    hw.upd_n++;
}
void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant)
{
    memcpy(hw.chm, chm, 5);
    hw.chm_instant = instant;
    hw.chm_n++;
}
void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx)
{
    hw.max_tx = max_tx;
    hw.max_rx = max_rx;
}
void ble_hw_tx_kick(void) { hw.kicks++; }
uint32_t ble_hw_time_us(void) { return now_us; }
uint32_t ble_hw_diag_now(void) { return now_us; }
uint8_t ble_hw_addr(uint8_t addr[6])
{
    static const uint8_t a[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
    memcpy(addr, a, 6);
    return 1;
}
void ble_hw_rand(uint8_t *out, uint8_t n) { memcpy(out, hw.rnd, n); }

/* ------------------------------------------------------------------------------------------ the firmware */
static uint32_t out_q[512], out_t[512];
static unsigned out_r, out_w;
static uint32_t in_pkt[512];
static uint16_t in_ts[512], in_last[512];
static unsigned in_n, state_calls;
int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    if (out_r == out_w)
        return 0;
    *pkt = out_q[out_r % 512];
    *t = out_t[out_r % 512];
    return 1;
}
void ble_app_midi_pop(void) { out_r++; }
void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    in_pkt[in_n % 512] = pkt;
    in_ts[in_n % 512] = ts;
    in_last[in_n % 512] = last;
    in_n++;
}
void ble_app_state(void) { state_calls++; }
#if BLE_SMP_LEGACY
static struct {
    int n;
    uint8_t rand[8], ltk[16];
    uint16_t ediv;
} bond;
void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    bond.n++;
    memcpy(bond.rand, rand, 8);
    memcpy(bond.ltk, ltk, 16);
    bond.ediv = ediv;
}
static struct {
    int n;
    uint8_t irk[16], addr[6], rnd;
} peer_id;
void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand)
{
    peer_id.n++;
    memcpy(peer_id.irk, irk, 16);
    memcpy(peer_id.addr, addr, 6);
    peer_id.rnd = addr_rand;
}
#endif
static void app_out(uint32_t pkt, uint32_t t)
{
    out_q[out_w % 512] = pkt;
    out_t[out_w % 512] = t;
    out_w++;
}

/* ------------------------------------------------------------------------------------- the central */
#define C_FRAMES 128
static struct {
    uint8_t q[64][2 + 255];
    int qn;
    uint16_t evt, interval;
    int alive, max_frag;
    uint8_t ctrl[64][32];
    int ctrl_n;
    uint8_t l2[1024];
    int l2_have, l2_need;
    uint8_t fr[C_FRAMES][600];
    uint16_t fr_len[C_FRAMES];
    uint8_t fr_used[C_FRAMES];
    int fr_n;
    int enc;
    struct ble_ccm ctx, crx;
    uint8_t last_pdu[2 + 255];
    int last_pdu_len;
    int pdus;
} C;

static void c_reset(void)
{
    memset(&C, 0, sizeof C);
    C.alive = 1;
    C.max_frag = 27;
}

static void c_pdu(uint8_t llid, const uint8_t *p, int n)
{
    uint8_t *q = C.q[C.qn++];
    q[0] = llid;
    q[1] = (uint8_t)n;
    memcpy(q + 2, p, n);
    if (C.enc && n) {
        ble_ccm_encrypt(&C.ctx, q[0], q + 2, (uint8_t)n);
        q[1] = (uint8_t)(n + 4);
    }
}
static void c_ctrl(const uint8_t *p, int n) { c_pdu(3, p, n); }
static void c_l2cap(uint16_t cid, const uint8_t *p, int n)
{
    uint8_t f[600];
    int off = 0;
    f[0] = (uint8_t)n, f[1] = (uint8_t)(n >> 8), f[2] = (uint8_t)cid, f[3] = (uint8_t)(cid >> 8);
    memcpy(f + 4, p, n);
    while (off < n + 4) {
        int k = n + 4 - off < C.max_frag ? n + 4 - off : C.max_frag;
        c_pdu(off ? 1 : 2, f + off, k);
        off += k;
    }
}

static void c_take(uint8_t *pdu, int len)
{
    uint8_t llid = pdu[0] & 3, n = pdu[1], *p = pdu + 2;
    C.pdus++;
    memcpy(C.last_pdu, pdu, len);
    C.last_pdu_len = len;
    if (C.enc && n) {
        if (!ble_ccm_decrypt(&C.crx, pdu[0], p, n)) {
            printf("central: MIC failure\n");
            fails++;
            return;
        }
        n -= 4;
    }
    if (llid == 3) {
        memcpy(C.ctrl[C.ctrl_n % 64], p, n < 32 ? n : 32);
        C.ctrl_n++;
        return;
    }
    if (llid == 2) {
        C.l2_have = 0;
        C.l2_need = (p[0] | p[1] << 8) + 4;
    }
    if (!C.l2_need)
        return;
    memcpy(C.l2 + C.l2_have, p, n);
    C.l2_have += n;
    if (C.l2_have == C.l2_need) {
        memcpy(C.fr[C.fr_n % C_FRAMES], C.l2, C.l2_have);
        C.fr_len[C.fr_n % C_FRAMES] = (uint16_t)C.l2_have;
        C.fr_used[C.fr_n % C_FRAMES] = 0;
        C.fr_n++;
        C.l2_need = 0;
    }
}

/* one connection event; returns the PDUs the peripheral sent in it */
static int c_event(void)
{
    int i, sent = 0;
    uint8_t pdu[2 + 255];
    if (!hw.conn_on)
        return 0;
    if (C.alive)
        for (i = 0; i < C.qn; i++)
            ble_ll_hw_rx(C.q[i], (uint8_t)(2 + C.q[i][1]));
    C.qn = 0;
    while (C.alive && hw.conn_on && sent < 32) {
        uint8_t n = ble_ll_hw_tx(pdu);
        if (!n)
            break;
        c_take(pdu, n);
        sent++;
        ble_ll_hw_tx_acked();
    }
    if (!hw.conn_on)
        return sent;
    now_us += (uint32_t)C.interval * 1250u;
    ble_ll_hw_event_end(C.evt, (uint8_t)C.alive);
    C.evt++;
    if (hw.upd_n && C.evt == hw.upd.instant)
        C.interval = hw.upd.interval;
    return sent;
}
static void c_events(int n)
{
    while (n-- > 0)
        c_event();
}

static const uint8_t *c_ctrl_last(uint8_t op)
{
    int i;
    for (i = C.ctrl_n - 1; i >= 0 && i >= C.ctrl_n - 64; i--)
        if (C.ctrl[i % 64][0] == op)
            return C.ctrl[i % 64];
    return 0;
}
static int c_ctrl_count(uint8_t op)
{
    int i, k = 0;
    for (i = 0; i < C.ctrl_n && i < 64; i++)
        k += C.ctrl[i][0] == op;
    return k;
}

/* a control PDU op among those taken since C.ctrl_n was `from` */
static int c_ctrl_since(int from, uint8_t op)
{
    int i;
    for (i = from; i < C.ctrl_n; i++)
        if (C.ctrl[i % 64][0] == op)
            return 1;
    return 0;
}

/* the oldest frame on cid not yet looked at (running events until one comes, at most 8) */
static const uint8_t *c_frame(uint16_t cid, int *n)
{
    int tries, i;
    for (tries = 0; tries < 9; tries++) {
        for (i = C.fr_n > C_FRAMES ? C.fr_n - C_FRAMES : 0; i < C.fr_n; i++) {
            uint8_t *f = C.fr[i % C_FRAMES];
            if (!C.fr_used[i % C_FRAMES] && (f[2] | f[3] << 8) == cid) {
                C.fr_used[i % C_FRAMES] = 1;
                *n = C.fr_len[i % C_FRAMES] - 4;
                return f + 4;
            }
        }
        c_event();
    }
    *n = 0;
    return 0;
}
static void c_frames_done(void)
{
    memset(C.fr_used, 1, sizeof C.fr_used);
}
static const uint8_t *c_att(const uint8_t *req, int n, int *rn)
{
    c_l2cap(4, req, n);
    return c_frame(4, rn);
}

/* ---------------------------------------------------------------------------------------- the scenarios */
static const uint8_t ADDR[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};   /* random static */
static const uint8_t ALL[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};

static void connect_ind(uint8_t *pdu, uint32_t aa, uint8_t win, uint16_t off, uint16_t iv, uint16_t lat, uint16_t to,
                        const uint8_t chm[5], uint8_t hop)
{
    static const uint8_t init_a[6] = {1, 2, 3, 4, 5, 6};
    pdu[0] = 0x05 | 0x80 | 0x40;               /* CONNECT_IND, RxAdd random (ours), TxAdd random */
    pdu[1] = 34;
    memcpy(pdu + 2, init_a, 6);
    memcpy(pdu + 8, ADDR, 6);
    pdu[14] = (uint8_t)aa, pdu[15] = (uint8_t)(aa >> 8), pdu[16] = (uint8_t)(aa >> 16), pdu[17] = (uint8_t)(aa >> 24);
    pdu[18] = 0x56, pdu[19] = 0x34, pdu[20] = 0x12;
    pdu[21] = win;
    pdu[22] = (uint8_t)off, pdu[23] = (uint8_t)(off >> 8);
    pdu[24] = (uint8_t)iv, pdu[25] = (uint8_t)(iv >> 8);
    pdu[26] = (uint8_t)lat, pdu[27] = (uint8_t)(lat >> 8);
    pdu[28] = (uint8_t)to, pdu[29] = (uint8_t)(to >> 8);
    memcpy(pdu + 30, chm, 5);
    pdu[35] = (uint8_t)(hop | 1 << 5);         /* SCA 1 */
}

static int connect(uint16_t iv, uint16_t to)
{
    uint8_t pdu[36];
    connect_ind(pdu, 0x50654A6B, 2, 3, iv, 0, to, ALL, 7);
    c_reset();
    C.interval = iv;
    hw.upd_n = hw.chm_n = 0;
    return ble_ll_hw_connect_ind(pdu, 36);
}

static void test_advertising(void)
{
    static const uint8_t ad[] = {2, 1, 6, 17, 7, 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED,
                                 0x5A, 0x0E, 0xB8, 0x03};
    uint8_t pdu[36];
    int ok = 1;
    {
        uint8_t a[6], rnd = ble_hw_addr(a);
        ble_init(a, rnd);
    }
    ble_enable(1);
    check("advertising: started, 100 ms, 37/38/39", hw.adv_on && hw.a.interval == 160 && hw.a.channels == 7);
    check("ADV_IND: header (type 0, TxAdd random, ChSel 0), AdvA, flags + MIDI UUID",
          hw.adv[0] == 0x40 && hw.adv[1] == 6 + sizeof ad && !memcmp(hw.adv + 2, ADDR, 6) &&
              !memcmp(hw.adv + 8, ad, sizeof ad));
    check("SCAN_RSP: 0E 09 \"Optimist 2211\" (the last four hex digits of ..:22:11)",
          hw.sr[0] == 0x44 && hw.sr[1] == 6 + 15 && hw.sr[8] == 14 && hw.sr[9] == 9 && !memcmp(hw.sr + 10, "Optimist 2211", 13));
    connect_ind(pdu, 0x50654A6B, 2, 3, 24, 0, 72, ALL, 7);
    pdu[8] ^= 1;
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 3, 24, 0, 72, ALL, 4);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 3, 24, 0, 72, ALL, 17);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 3, 5, 0, 72, ALL, 7);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 9, 3, 24, 0, 72, ALL, 7);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 25, 24, 0, 72, ALL, 7);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 3, 24, 10, 20, ALL, 7);   /* timeout too short for the latency */
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 3, 24, 0, 72, (const uint8_t[5]){0x01, 0, 0, 0, 0}, 7);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x8E89BED6, 2, 3, 24, 0, 72, ALL, 7);
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    connect_ind(pdu, 0x50654A6B, 2, 3, 24, 0, 72, ALL, 7);
    pdu[0] &= 0x7F;                            /* RxAdd public: not our address */
    ok &= !ble_ll_hw_connect_ind(pdu, 36);
    check("CONNECT_IND ignored: AdvA, hop 4 / 17, interval 5, window 9, offset > interval, timeout, map, AA, RxAdd",
          ok && hw.adv_on && !hw.conn_on);
    check("blell: ten CONNECT_INDs seen, ten refused, the last for its RxAdd, its fields kept",
          ble_dg.cind_rx == 10 && ble_dg.cind_rej == 10 && !ble_dg.cind_ok && ble_dg.cind_rej_why == BDR_RXADD &&
              ble_dg.cind_aa == 0x50654A6B && ble_dg.cind_crc == 0x123456 && ble_dg.cind_interval == 24 &&
              ble_dg.cind_win_size == 2 && ble_dg.cind_win_off == 3 && ble_dg.cind_timeout == 72 &&
              ble_dg.cind_hop == 7 && ble_dg.cind_sca == 1);
}

static void test_connect_ll(void)
{
    uint8_t d[32];
    const uint8_t *r;
    int sent;
    check("CONNECT_IND taken: the driver armed with AA, CRC init, window, interval, map, hop",
          connect(24, 72) && hw.conn_on && hw.conn.aa == 0x50654A6B && hw.conn.crc_init == 0x123456 &&
              hw.conn.win_size == 2 && hw.conn.win_offset == 3 && hw.conn.interval == 24 && hw.conn.timeout == 72 &&
              hw.conn.hop == 7 && hw.conn.sca == 1 && hw.conn.latency == 0 && !memcmp(hw.conn.chm, ALL, 5) &&
              ble_connected());
    d[0] = LL_VERSION_IND, d[1] = 10, d[2] = 0x59, d[3] = 0, d[4] = 1, d[5] = 0;
    c_ctrl(d, 6);
    c_event();
    r = c_ctrl_last(LL_VERSION_IND);
    check("VERSION_IND answered: 5.0, company 0xFFFF", r && r[1] == 9 && r[2] == 0xFF && r[3] == 0xFF);
    c_ctrl(d, 6);
    c_event();
    check("VERSION_IND again: not answered twice", c_ctrl_count(LL_VERSION_IND) == 1);
    memset(d, 0, 9);
    d[0] = LL_FEATURE_REQ, d[1] = 0xFF, d[2] = 0x01;   /* the central: everything in octet 0 */
    c_ctrl(d, 9);
    c_event();
    r = c_ctrl_last(LL_FEATURE_RSP);
    check("FEATURE_REQ -> FEATURE_RSP: octet 0 = both (params req, ext reject, periph feat, DLE)",
          r && r[1] == (0x2E | (BLE_LL_ENC ? 0x11 : 0)) && !r[2]);
    c_events(8);
    check("no PERIPHERAL_FEATURE_REQ once the central exchanged features", !c_ctrl_count(LL_PERIPHERAL_FEATURE_REQ));
    r = c_ctrl_last(LL_LENGTH_REQ);
    check("we start the data length update (DLE both sides): 251 / 2120 us",
          r && r[1] == 251 && r[3] == (2120 & 0xFF) && r[4] == 2120 >> 8 && r[5] == 251);
    d[0] = LL_LENGTH_RSP, d[1] = 200, d[2] = 0, d[3] = 0x48, d[4] = 0x08, d[5] = 251, d[6] = 0, d[7] = 0x48, d[8] = 0x08;
    c_ctrl(d, 9);
    c_event();
    check("LENGTH_RSP: we send <= 200 (its MaxRx), receive <= 251; the driver told",
          hw.max_tx == 200 && hw.max_rx == 251 && bll.lproc == P_NONE);
    d[0] = LL_LENGTH_REQ, d[1] = 251, d[2] = 0, d[3] = 0x48, d[4] = 0x08, d[5] = 100, d[6] = 0, d[7] = 0x48, d[8] = 0x08;
    c_ctrl(d, 9);
    c_event();
    r = c_ctrl_last(LL_LENGTH_RSP);
    check("LENGTH_REQ from the central -> LENGTH_RSP 251; we send <= 251, receive <= 100",
          r && r[1] == 251 && hw.max_tx == 251 && hw.max_rx == 100);
    C.max_frag = 100;
    d[0] = LL_PING_REQ;
    c_ctrl(d, 1);
    c_event();
    check("PING_REQ -> PING_RSP", c_ctrl_count(LL_PING_RSP) == 1);
    d[0] = LL_PHY_REQ, d[1] = 3, d[2] = 3;
    c_ctrl(d, 3);
    c_event();
    r = c_ctrl_last(LL_PHY_RSP);
    check("PHY_REQ (1M | 2M) -> PHY_RSP 1M / 1M", r && r[1] == 1 && r[2] == 1);
    d[0] = LL_PHY_UPDATE_IND, d[1] = 0, d[2] = 0, d[3] = (uint8_t)(C.evt + 6), d[4] = (uint8_t)((C.evt + 6) >> 8);
    c_ctrl(d, 5);
    c_events(2);
    check("PHY_UPDATE_IND with no change: still connected", hw.conn_on && bll.rproc == P_NONE);
    d[0] = 0x7E;
    c_ctrl(d, 1);
    d[0] = LL_FEATURE_REQ;
    c_ctrl(d, 3);                              /* too short */
    c_event();
    r = c_ctrl_last(LL_UNKNOWN_RSP);
    check("unknown opcode -> UNKNOWN_RSP; a known one too short -> UNKNOWN_RSP",
          c_ctrl_count(LL_UNKNOWN_RSP) == 2 && r && r[1] == LL_FEATURE_REQ);
    memset(d, 0, 24);
    d[0] = LL_CONNECTION_PARAM_REQ, d[1] = 6, d[3] = 12, d[5] = 0, d[7] = 200;   /* 7.5..15 ms, 2 s */
    c_ctrl(d, 24);
    c_event();
    r = c_ctrl_last(LL_CONNECTION_PARAM_RSP);
    check("CONNECTION_PARAM_REQ valid -> CONNECTION_PARAM_RSP as asked", r && r[1] == 6 && r[3] == 12 && r[7] == 200);
    d[0] = LL_CONNECTION_UPDATE_IND, d[1] = 1, d[2] = 0, d[3] = 0, d[4] = 12, d[5] = 0, d[6] = 0, d[7] = 0, d[8] = 200,
    d[9] = 0, d[10] = (uint8_t)(C.evt + 6), d[11] = (uint8_t)((C.evt + 6) >> 8);
    c_ctrl(d, 12);
    c_event();
    check("CONNECTION_UPDATE_IND -> the driver gets interval 12 at the instant", hw.upd_n == 1 &&
          hw.upd.interval == 12 && hw.upd.instant == (uint16_t)(C.evt + 5) && bll.rproc == P_NONE && bll.interval == 24);
    c_events(6);
    check("after the instant the link layer runs at 12 (supervision 2 s)", bll.interval == 12 && bll.timeout == 200);
    memset(d, 0, 24);
    d[0] = LL_CONNECTION_PARAM_REQ, d[1] = 12, d[3] = 6, d[7] = 200;           /* min > max */
    c_ctrl(d, 24);
    c_event();
    r = c_ctrl_last(LL_REJECT_EXT_IND);
    check("CONNECTION_PARAM_REQ invalid -> REJECT_EXT_IND (0x0F, 0x1E)", r && r[1] == 0x0F && r[2] == 0x1E);
    memset(d, 0, 23);
    d[0] = LL_ENC_REQ;
    c_ctrl(d, 23);
    sent = C.ctrl_n;
    c_event();
    r = c_ctrl_last(LL_REJECT_EXT_IND);
#if !BLE_LL_ENC
    check("ENC_REQ (no encryption built) -> REJECT_EXT_IND (0x03, 0x1A)",
          C.ctrl_n == sent + 1 && r && r[1] == 0x03 && r[2] == 0x1A);
#else
    check("ENC_REQ with no key -> ENC_RSP, REJECT_EXT_IND (0x03, 0x06)",
          C.ctrl_n == sent + 2 && r && r[1] == 0x03 && r[2] == 0x06 && !bll.enc_rx);
#endif
    d[0] = LL_CHANNEL_MAP_IND, d[1] = 0x0F, d[2] = 0xF0, d[3] = 0, d[4] = 0, d[5] = 0x10,
    d[6] = (uint8_t)(C.evt + 4), d[7] = (uint8_t)((C.evt + 4) >> 8);
    c_ctrl(d, 8);
    c_event();
    check("CHANNEL_MAP_IND -> the driver gets the map and the instant", hw.chm_n == 1 && hw.chm[0] == 0x0F &&
          hw.chm[4] == 0x10 && hw.chm_instant == (uint16_t)(C.evt + 3));
    c_events(4);
    check("after the instant the link layer keeps the new map", bll.chm[1] == 0xF0 && !bll.chm_pending);
}

static void test_gatt(void)
{
    uint8_t q[64];
    const uint8_t *r;
    int n, i, ok, nsvc = 0;
    uint16_t svc_s[8], svc_e[8], midi_val = 0, midi_ccc = 0, sc_ccc = 0, s;
    r = c_att((const uint8_t[]){0x02, 0x05, 0x02}, 3, &n);
    check("ATT MTU: the central 517 -> we answer 247", r && n == 3 && r[0] == 0x03 && r[1] == 247 && batt.mtu == 247);
    for (s = 1; s && nsvc < 8;) {              /* primary services */
        q[0] = 0x10, q[1] = (uint8_t)s, q[2] = (uint8_t)(s >> 8), q[3] = 0xFF, q[4] = 0xFF, q[5] = 0x00, q[6] = 0x28;
        r = c_att(q, 7, &n);
        if (!r || r[0] != 0x11)
            break;
        for (i = 2; i + r[1] <= n; i += r[1]) {
            svc_s[nsvc] = (uint16_t)(r[i] | r[i + 1] << 8);
            svc_e[nsvc] = (uint16_t)(r[i + 2] | r[i + 3] << 8);
            nsvc++;
            s = (uint16_t)(svc_e[nsvc - 1] + 1);
        }
    }
    check("Read By Group Type: three services (GAP 1-7, GATT 8-11, MIDI 12-15), then Attribute Not Found",
          nsvc == 3 && svc_s[0] == 1 && svc_e[0] == 7 && svc_s[1] == 8 && svc_e[1] == 11 && svc_s[2] == 12 &&
              svc_e[2] == 15 && r && r[0] == 0x01 && r[1] == 0x10 && r[4] == 0x0A);
    for (i = 0, ok = 1; i < nsvc; i++) {       /* characteristics, then descriptors */
        uint16_t h = svc_s[i], last_val = 0;
        for (;;) {
            int j;
            q[0] = 0x08, q[1] = (uint8_t)h, q[2] = (uint8_t)(h >> 8), q[3] = (uint8_t)svc_e[i], q[4] = 0, q[5] = 0x03,
            q[6] = 0x28;
            r = c_att(q, 7, &n);
            if (!r || r[0] != 0x09)
                break;
            for (j = 2; j + r[1] <= n; j += r[1]) {
                uint16_t decl = (uint16_t)(r[j] | r[j + 1] << 8), val = (uint16_t)(r[j + 3] | r[j + 4] << 8);
                if (r[1] == 21 && r[j + 2] == 0x16 && !memcmp(r + j + 5, ATT_UUID_MIDI_IO, 16))
                    midi_val = val;
                ok &= val == decl + 1;
                last_val = val;
                h = (uint16_t)(val + 1);
            }
        }
        if (last_val && last_val < svc_e[i]) {
            q[0] = 0x04, q[1] = (uint8_t)(last_val + 1), q[2] = 0, q[3] = (uint8_t)svc_e[i], q[4] = 0;
            r = c_att(q, 5, &n);
            if (r && r[0] == 0x05 && r[1] == 1 && r[4] == 0x02 && r[5] == 0x29)
                (i == 1 ? &sc_ccc : &midi_ccc)[0] = (uint16_t)(r[2] | r[3] << 8);
        }
    }
    check("characteristics: MIDI I/O (read, write w/o rsp, notify) at 14; CCCDs at 11 and 15",
          ok && midi_val == 14 && midi_ccc == 15 && sc_ccc == 11);
    r = c_att((const uint8_t[]){0x0A, 3, 0}, 3, &n);
    check("Read Device Name: Optimist 2211, the advertised name", r && r[0] == 0x0B && n == 14 && !memcmp(r + 1, "Optimist 2211", 13));
    r = c_att((const uint8_t[]){0x0C, 3, 0, 9, 0}, 5, &n);
    check("Read Blob at 9: 2211", r && r[0] == 0x0D && n == 5 && !memcmp(r + 1, "2211", 4));
    r = c_att((const uint8_t[]){0x0C, 3, 0, 14, 0}, 5, &n);
    check("Read Blob past the end: Invalid Offset", r && r[0] == 0x01 && r[4] == 0x07);
    r = c_att((const uint8_t[]){0x0A, 14, 0}, 3, &n);
    check("Read MIDI I/O: empty", r && r[0] == 0x0B && n == 1);
    r = c_att((const uint8_t[]){0x0A, 10, 0}, 3, &n);
    check("Read Service Changed: Read Not Permitted", r && r[0] == 0x01 && r[4] == 0x02);
    r = c_att((const uint8_t[]){0x0A, 16, 0}, 3, &n);
    check("Read handle 16: Invalid Handle", r && r[0] == 0x01 && r[2] == 16 && r[4] == 0x01);
    r = c_att((const uint8_t[]){0x08, 1, 0, 0xFF, 0xFF, 0x00, 0x2A}, 7, &n);
    check("Read By Type 0x2A00 (name by UUID)", r && r[0] == 0x09 && r[1] == 15 && r[2] == 3 && !memcmp(r + 4, "Optimist 2211", 13));
    r = c_att((const uint8_t[]){0x08, 1, 0, 0xFF, 0xFF, 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0, 0, 0x80, 0, 0x10, 0, 0, 0x04,
                              0x2A, 0, 0}, 21, &n);
    check("Read By Type with a 128-bit base UUID (PPCP): 6, 9, 0, 100",
          r && r[0] == 0x09 && r[1] == 10 && r[2] == 7 && r[4] == 6 && r[6] == 9 && r[10] == 100);
    {
        uint8_t fbt[23] = {0x06, 1, 0, 0xFF, 0xFF, 0x00, 0x28};
        memcpy(fbt + 7, BLE_UUID_MIDI_SVC, 16);
        r = c_att(fbt, 23, &n);
        check("Find By Type Value (primary service = MIDI): 12..15", r && r[0] == 0x07 && n == 5 && r[1] == 12 && r[3] == 15);
    }
    r = c_att((const uint8_t[]){0x10, 1, 0, 0xFF, 0xFF, 0x03, 0x28}, 7, &n);
    check("Read By Group Type 0x2803: Unsupported Group Type", r && r[0] == 0x01 && r[4] == 0x10);
    r = c_att((const uint8_t[]){0x04, 5, 0, 4, 0}, 5, &n);
    check("Find Information start > end: Invalid Handle", r && r[0] == 0x01 && r[4] == 0x01);
    r = c_att((const uint8_t[]){0x12, 3, 0, 'x'}, 4, &n);
    check("Write the name: Write Not Permitted", r && r[0] == 0x01 && r[4] == 0x03);
    r = c_att((const uint8_t[]){0x12, 15, 0, 1}, 4, &n);
    check("Write a CCCD with one octet: Invalid Attribute Value Length", r && r[0] == 0x01 && r[4] == 0x0D);
    r = c_att((const uint8_t[]){0x16, 14, 0, 0, 0, 1}, 6, &n);
    check("Prepare Write: Request Not Supported", r && r[0] == 0x01 && r[1] == 0x16 && r[4] == 0x06);
    r = c_att((const uint8_t[]){0x12, 11, 0, 2, 0}, 5, &n);
    check("Service Changed CCCD on: Write Response", r && r[0] == 0x13);
    r = c_frame(4, &n);
#if BLE_SC_ON_SUBSCRIBE
    check("then the Service Changed indication 0x0001..0xFFFF",
          r && r[0] == 0x1D && r[1] == 10 && r[3] == 1 && r[5] == 0xFF && r[6] == 0xFF && batt.ind_wait);
    c_l2cap(4, (const uint8_t[]){0x1E}, 1);
    c_event();
    check("confirmed", !batt.ind_wait);
#else
    check("then no Service Changed indication: the database never changes (the Mac stopped after one, blell10-12)",
          !r && !batt.ind_wait && !batt.sc_due);
    r = c_att((const uint8_t[]){0x0A, 11, 0}, 3, &n);
    check("Read the Service Changed CCCD: 2 (indications on)", r && r[0] == 0x0B && n == 3 && r[1] == 2);
#endif
    r = c_att((const uint8_t[]){0x0A, 15, 0}, 3, &n);
    check("Read the MIDI CCCD: 0", r && r[0] == 0x0B && n == 3 && r[1] == 0);
}

static void test_midi(void)
{
    const uint8_t *r;
    int n, i, ok;
    uint8_t w[64];
    unsigned st = state_calls;
    r = c_att((const uint8_t[]){0x12, 15, 0, 1, 0}, 5, &n);
    check("MIDI CCCD on: Write Response, the firmware told, ble_midi_ready()", r && r[0] == 0x13 &&
          state_calls == st + 1 && ble_midi_ready());
    /* the interval is 12 (15 ms): outside 6..9, so the signalling request follows */
    r = c_frame(5, &n);
    check("L2CAP Connection Parameter Update Request {6, 9, 0, 100}",
          r && r[0] == 0x12 && r[2] == 8 && r[4] == 6 && r[6] == 9 && r[8] == 0 && r[10] == 100);
    c_l2cap(5, (const uint8_t[]){0x13, r ? r[1] : 0, 2, 0, 1, 0}, 6);   /* rejected */
    r = c_frame(5, &n);
    check("rejected: asked once more {12, 12, 0, 100}", r && r[0] == 0x12 && r[4] == 12 && r[6] == 12 && bhs.fast == 2);
    c_l2cap(5, (const uint8_t[]){0x13, r ? r[1] : 0, 2, 0, 0, 0}, 6);   /* accepted */
    c_event();
    check("accepted: done", bhs.fast == 3);
    /* notifications: a note on, a running-status note on, a note off with real timestamps */
    app_out(0x09 | 0x90 << 8 | 60 << 16 | 100 << 24, 1000);
    app_out(0x09 | 0x90 << 8 | 64 << 16 | 90 << 24, 1003);
    app_out(0x08 | 0x80 << 8 | 60 << 16 | 0 << 24, 1130);       /* 1130 = 8 << 7 | 106: the high part steps */
    c_event();
    r = c_frame(4, &n);
    {
        static const uint8_t want[] = {0x1B, 14, 0, 0x80 | (1000 >> 7), 0x80 | (1000 & 0x7F), 0x90, 60, 100,
                                       0x80 | (1003 & 0x7F), 64, 90, 0x80 | (1130 & 0x7F), 0x80, 60, 0};
        check("notification: header, timestamps (1000, 1003, 1130), running status",
              r && n == sizeof want && !memcmp(r, want, sizeof want) && out_r == out_w);
    }
    for (i = 0; i < 300; i++)                  /* a flood: packed up to MTU - 3, two notifications an event */
        app_out(0x0B | 0xB0 << 8 | (uint32_t)(i & 0x7F) << 16 | 5u << 24, 2000 + i / 50);
    c_event();                                 /* (queued at the end of an event, sent in the next) */
    n = C.fr_n;
    c_event();
    check("a flood of 300 CCs: two notifications in one event, each <= 244 octets of MIDI",
          C.fr_n == n + 2 && C.fr_len[n % C_FRAMES] <= 4 + 247 && C.fr_len[n % C_FRAMES] > 200);
    c_events(6);
    check("... all sent within a few events", out_r == out_w);
    c_frames_done();
    /* the central writes MIDI: two packets, the second continuing a SysEx */
    in_n = 0;
    w[0] = 0x52, w[1] = 14, w[2] = 0;
    {
        static const uint8_t p1[] = {0x85, 0x81, 0x90, 0x3C, 0x64, 0x3E, 0x64, 0x85, 0xF0, 0x7E, 0x7F};
        static const uint8_t p2[] = {0x85, 0x06, 0x01, 0x86, 0xF7};
        memcpy(w + 3, p1, sizeof p1);
        c_l2cap(4, w, 3 + sizeof p1);
        memcpy(w + 3, p2, sizeof p2);
        c_l2cap(4, w, 3 + sizeof p2);
        c_event();
    }
    ok = in_n == 4 && in_pkt[0] == (0x09u | 0x90 << 8 | 0x3C << 16 | 0x64u << 24) && in_ts[0] == (5 << 7 | 1) &&
         in_pkt[1] == (0x09u | 0x90 << 8 | 0x3E << 16 | 0x64u << 24) && in_last[0] == (5 << 7 | 5) &&
         in_pkt[2] == (0x04u | 0xF0 << 8 | 0x7E << 16 | 0x7Fu << 24) &&
         in_pkt[3] == (0x07u | 0x06 << 8 | 0x01 << 16 | 0xF7u << 24) && in_ts[3] == (5 << 7 | 6);
    check("write without response: notes (running status), a SysEx over two packets", ok);
    r = c_att((const uint8_t[]){0x12, 14, 0, 0x80, 0x80, 0xFA}, 6, &n);
    check("write request on MIDI I/O: Write Response, Start (FA) decoded",
          r && r[0] == 0x13 && in_n == 5 && in_pkt[4] == (0x0Fu | 0xFA << 8));
    r = c_att((const uint8_t[]){0x12, 15, 0, 0, 0}, 5, &n);
    for (i = 0; i < 5; i++)
        app_out(0x09 | 0x90 << 8 | 60 << 16 | 1 << 24, 3000);
    c_events(2);
    check("MIDI CCCD off: nothing sent, the queue drained", r && r[0] == 0x13 && !ble_midi_ready() && out_r == out_w &&
          c_frame(4, &n) == 0);
    c_att((const uint8_t[]){0x12, 15, 0, 1, 0}, 5, &n);
}

/* Bluetooth audio streaming (op_bt.c bt_poll -> ble_sig_audio): the slower interval asked for, the fast one after */
static void test_audio_interval(void)
{
    const uint8_t *r;
    int n;
    ble_sig_audio(1);
    r = c_frame(5, &n);
    check("audio on at 12: L2CAP Connection Parameter Update Request {24, 36, 0, 200}",
          r && r[0] == 0x12 && r[2] == 8 && r[4] == 24 && r[6] == 36 && r[8] == 0 && r[10] == 200 && bhs.slow);
    c_l2cap(5, (const uint8_t[]){0x13, r ? r[1] : 0, 2, 0, 0, 0}, 6);   /* accepted */
    c_event();
    ble_sig_audio(1);
    r = c_frame(5, &n);
    check("audio still on: asked once", !r && bhs.fast == 3);
    bll.interval = 24;                         /* (the central moved the link to 30 ms) */
    ble_sig_audio(0);
    r = c_frame(5, &n);
    check("audio off at 24: {6, 9, 0, 100} again", r && r[0] == 0x12 && r[4] == 6 && r[6] == 9 && r[10] == 100 && !bhs.slow);
    c_l2cap(5, (const uint8_t[]){0x13, r ? r[1] : 0, 2, 0, 0, 0}, 6);
    c_event();
    ble_sig_audio(0);
    r = c_frame(5, &n);
    check("audio still off: nothing more", !r);
    ble_sig_audio(1);
    r = c_frame(5, &n);
    check("audio on at 24 already: nothing asked", !r && bhs.slow);
    bll.interval = 12;
    bhs.slow = 0;
    bhs.fast = 1;                              /* (our fast request under way) */
    ble_sig_audio(1);
    r = c_frame(5, &n);
    check("audio on while a request is under way: it waits", !r && !bhs.slow);
    bhs.fast = 3;
    {                                          /* the link's queue full (MIDI clock, the mirror): kept for the next loop */
        uint8_t fill[64] = {0};
        while (ble_ll_send(4, fill, sizeof fill))
            ;
        while (ble_ll_send(4, fill, 1))
            ;
        ble_sig_audio(1);
        check("audio on, the link's queue full: not sent, not marked as asked", !bhs.asked && bll.wr - bll.rd + 4u >= BLE_LL_TX_RING - 16u);
        bll.rd = bll.wr;                       /* (the queue drained) */
        ble_sig_audio(1);
        r = c_frame(5, &n);
        check("... the queue drained: asked then {24, 36, 0, 200}", r && r[0] == 0x12 && r[4] == 24 && bhs.asked);
        ble_sig_audio(0);
        (void)c_frame(5, &n);
    }
    {                                          /* a Mac: connected at 30 ms, then its own LL request for 7.5 ms */
        uint8_t d[24];
        uint32_t rej;
        uint16_t to = bll.timeout;
        bll.interval = 24;
        bll.timeout = 200;
        ble_sig_audio(1);
        r = c_frame(5, &n);
        check("audio on at 30 ms, 2 s supervision: nothing asked", !r && bhs.slow && !bhs.asked);
        bhs.slow = bhs.asked = 0;
        bll.timeout = 72;                      /* (the Mac's 720 ms at connect: a 750 ms gap ended the link, 2026-10-25) */
        ble_sig_audio(1);
        r = c_frame(5, &n);
        check("audio on at 30 ms, 720 ms supervision: {24, 36, 0, 200} asked (2 s)",
              r && r[0] == 0x12 && r[4] == 24 && r[6] == 36 && r[10] == 200 && bhs.asked);
        bhs.asked = 0;
        bll.timeout = to;
        memset(d, 0, sizeof d);
        d[0] = LL_CONNECTION_PARAM_REQ, d[1] = 6, d[3] = 6, d[7] = 200;   /* 7.5 ms, 2 s */
        rej = c_ctrl_count(LL_REJECT_EXT_IND);
        c_ctrl(d, 24);
        c_event();
        r = c_ctrl_last(LL_REJECT_EXT_IND);
        check("... the central's CONNECTION_PARAM_REQ for 7.5 ms: REJECT_EXT_IND (0x0F, 0x3B), still 30 ms",
              c_ctrl_count(LL_REJECT_EXT_IND) == rej + 1u && r && r[1] == 0x0F && r[2] == 0x3B && bll.rproc == P_NONE &&
              bll.interval == 24);
        bll.interval = 6;                      /* (moved anyway: an UPDATE_IND of the central's own) */
        ble_sig_audio(1);
        r = c_frame(5, &n);
        check("... the link at 7.5 ms all the same: {24, 36, 0, 200} asked", r && r[0] == 0x12 && r[4] == 24 && bhs.asked);
        ble_sig_audio(1);
        check("... not again at once", !c_frame(5, &n));
        now_us += BLE_SIG_AUDIO_RETRY_US;
        ble_sig_audio(1);
        r = c_frame(5, &n);
        check("... still 7.5 ms 5 s later: asked again", r && r[0] == 0x12 && r[4] == 24);
        ble_sig_audio(0);
        r = c_frame(5, &n);
        check("audio off at 7.5 ms: nothing to ask (the fast range already)", !r && !bhs.slow);
        c_ctrl(d, 24);
        c_event();
        r = c_ctrl_last(LL_CONNECTION_PARAM_RSP);
        check("... and the central's 7.5 ms request accepted again", r && r[1] == 6 && r[3] == 6);
        bll.rproc = P_NONE;
        bll.interval = 12;
    }
}

static void test_l2cap(void)
{
    const uint8_t *r;
    int n;
    uint8_t big[200];
    C.max_frag = 27;
    c_l2cap(5, (const uint8_t[]){0x14, 7, 10, 0, 0x80, 0, 0x40, 0, 0x17, 0, 0x64, 0, 5, 0}, 14);   /* LE CBFC req */
    r = c_frame(5, &n);
    check("LE Credit Based Connection Request -> Command Reject (not understood)",
          r && r[0] == 0x01 && r[1] == 7 && r[2] == 2 && r[4] == 0 && r[5] == 0);
#if !BLE_SMP_LEGACY
    c_l2cap(6, (const uint8_t[]){0x01, 0x03, 0x00, 0x01, 0x10, 0x07, 0x07}, 7);
    r = c_frame(6, &n);
    check("SMP Pairing Request -> Pairing Failed, Pairing Not Supported", r && n == 2 && r[0] == 0x05 && r[1] == 0x05);
#else
    c_l2cap(6, (const uint8_t[]){0x01, 0x03, 0x00, 0x01, 0x06, 0x07, 0x07}, 7);
    r = c_frame(6, &n);
    check("SMP Pairing Request with a 6-octet key -> Pairing Failed, Encryption Key Size",
          r && n == 2 && r[0] == 0x05 && r[1] == 0x06);
    c_l2cap(6, (const uint8_t[]){0x03, 1, 2}, 3);
    r = c_frame(6, &n);
    check("SMP Pairing Confirm too short -> Pairing Failed, Invalid Parameters", r && n == 2 && r[0] == 0x05 &&
                                                                                  r[1] == 0x0A);
    c_l2cap(6, (const uint8_t[]){0x0C, 0, 0}, 3);
    r = c_frame(6, &n);
    check("SMP Pairing Public Key (Secure Connections) -> Pairing Failed, Command Not Supported", r && n == 2 &&
                                                                                                 r[1] == 0x07);
#endif
    {
        static const uint8_t ping[] = {0x0A, 3, 0};
        int k, before = (int)ble_dg.pdu_n;
        c_att(ping, 3, &n);
        k = (int)ble_dg.pdu_n - 1;
        check("blell's protocol ring: the Read (rx att 0A 03 00) and its response (tx att 0B ...) in order",
              before + 2 == (int)ble_dg.pdu_n && ble_dg.pdu[(k - 1) & (BLE_DIAG_PDUS - 1)].ch == BDP_ATT &&
                  ble_dg.pdu[(k - 1) & (BLE_DIAG_PDUS - 1)].b[0] == 0x0A &&
                  ble_dg.pdu[(k - 1) & (BLE_DIAG_PDUS - 1)].b[1] == 3 && ble_dg.pdu[(k - 1) & (BLE_DIAG_PDUS - 1)].n == 3 &&
                  ble_dg.pdu[k & (BLE_DIAG_PDUS - 1)].ch == (BDP_ATT | BDP_TX) &&
                  ble_dg.pdu[k & (BLE_DIAG_PDUS - 1)].b[0] == 0x0B && ble_dg.pdu[k & (BLE_DIAG_PDUS - 1)].n == 14);
    }
    memset(big, 0, sizeof big);
    big[0] = 0x52, big[1] = 14, big[2] = 0, big[3] = 0x80;
    {
        int i;
        for (i = 4; i + 4 <= 196; i += 4)      /* 48 note-ons each with a timestamp */
            big[i] = 0x81, big[i + 1] = 0x90, big[i + 2] = 0x40, big[i + 3] = 0x7F;
    }
    in_n = 0;
    c_l2cap(4, big, 196);                      /* 200 octets in 8 fragments */
    c_event();
    check("a 196-octet write in 8 fragments of 27: 48 notes", in_n == 48);
    {   /* a frame over our MTU (+4) is passed over, the next one is read */
        uint8_t f[304];
        int off;
        memset(f, 0x11, sizeof f);
        f[0] = 0x2C, f[1] = 0x01, f[2] = 4, f[3] = 0;   /* 300 octets */
        C.qn = 0;
        for (off = 0; off < 304; off += 27)
            c_pdu(off ? 1 : 2, f + off, off + 27 <= 304 ? 27 : 304 - off);
        in_n = 0;
        c_l2cap(4, big, 196);
        c_event();
        check("an over-long frame is skipped whole, the next frame is read", in_n == 48);
    }
}

static void test_endings(void)
{
    uint8_t d[12];
    int starts;
    /* the central terminates */
    d[0] = LL_TERMINATE_IND, d[1] = 0x13;
    c_ctrl(d, 2);
    starts = hw.adv_starts;
    c_event();
    check("TERMINATE_IND from the central: closed after its event, advertising again",
          !hw.conn_on && hw.adv_on && hw.adv_starts == starts + 1 && !ble_connected() && !ble_midi_ready());
    /* supervision timeout */
    check("reconnect", connect(24, 50));
    c_events(3);
    C.alive = 0;
    c_events(16);                              /* 16 x 30 ms = 480 ms < 500 ms */
    check("supervision: still up at 480 ms without packets", hw.conn_on);
    c_events(2);
    check("supervision timeout (500 ms): lost, advertising", !hw.conn_on && hw.adv_on);
    /* never established */
    check("reconnect", connect(24, 300));
    C.alive = 0;
    c_events(6);
    check("no packet: still waiting after 6 events (the window first)", hw.conn_on);
    c_events(2);
    check("no packet in 6 intervals after the window: lost (0x3E)", !hw.conn_on && hw.adv_on);
    /* an instant already passed */
    check("reconnect", connect(24, 300));
    c_events(10);
    d[0] = LL_CONNECTION_UPDATE_IND, d[1] = 1, d[2] = 0, d[3] = 0, d[4] = 12, d[5] = 0, d[6] = 0, d[7] = 0, d[8] = 200,
    d[9] = 0, d[10] = (uint8_t)(C.evt - 2), d[11] = 0;
    c_ctrl(d, 12);
    c_event();
    check("CONNECTION_UPDATE_IND with an instant in the past: lost (Instant Passed)", !hw.conn_on && hw.adv_on);
    /* our own procedures (long packets, then the features), and their 40 s timeout */
    check("reconnect", connect(24, 300));
    c_events(8);
    check("a silent central: after 6 events we ask for long packets first (LL_LENGTH_REQ), without them the features",
          BLE_LL_MAX_OCTETS > 27u ? c_ctrl_count(LL_LENGTH_REQ) == 1 && bll.lproc == P_LEN && !c_ctrl_count(LL_PERIPHERAL_FEATURE_REQ)
                                  : c_ctrl_count(LL_PERIPHERAL_FEATURE_REQ) == 1 && bll.lproc == P_FEAT);
    c_events(40000000 / (24 * 1250) - 4);
    check("no answer for < 40 s: still connected", hw.conn_on);
    c_events(16);
    check("no answer for 40 s: LL response timeout", !hw.conn_on && hw.adv_on);
    /* ours: disconnect */
    check("reconnect", connect(24, 300));
    c_events(2);
    ble_enable(0);
    c_event();
    check("ble_enable(0): TERMINATE_IND (0x13) sent, closed once acknowledged, no advertising",
          !hw.conn_on && !hw.adv_on && c_ctrl_last(LL_TERMINATE_IND) && c_ctrl_last(LL_TERMINATE_IND)[1] == 0x13);
    ble_enable(1);
    check("ble_enable(1): advertising", hw.adv_on);
}

/* the console's blell after every ending above (ble_diag.c, the text the FM-1 prints) */
static char blell_out[32768];
static size_t blell_n;
static void blell_put(const char *t)
{
    size_t n = strlen(t);
    if (blell_n + n < sizeof blell_out) {
        memcpy(blell_out + blell_n, t, n + 1);
        blell_n += n;
    }
}
static int blell_has(const char *line) { return strstr(blell_out, line) != NULL; }

static void test_blell(void)
{
    struct ble_diag_regs r = {0};
    blell_n = 0;
    blell_out[0] = 0;
    ble_diag_print(blell_put, &r);
    check("blell: state, CONNECT_IND counts and the last one's fields",
          blell_has("ll_state adv\r\n") && blell_has("ll_enabled 1\r\n") && blell_has("cind_rej 10\r\n") &&
              blell_has("cind_aa 50654A6B\r\n") && blell_has("cind_crc 123456\r\n") &&
              blell_has("cind_chm 1FFFFFFFFF\r\n") && ble_dg.cind_ok >= 6 && ble_dg.cind_rx == ble_dg.cind_ok + 10);
    check("blell: the endings by kind (central, supervision 0x08, establishment 0x3E), the last by us (0x16)",
          ble_dg.peer_terms == 1 && ble_dg.sup_timeouts == 1 && ble_dg.estab_fails == 1 &&
              blell_has("close_reason 16\r\n") && blell_has("close_by 0\r\n") && ble_dg.closes == ble_dg.cind_ok);
    check("blell: control opcodes both ways, ATT opcodes, the last 8 of each",
          blell_has("ctl_rx_last ") && blell_has("ctl_tx_last ") && ble_dg.ctl_rx_n > 8 && ble_dg.ctl_tx_n > 8 &&
              ble_dg.att_rx_n > 0 && ble_dg.ctl_tx[(ble_dg.ctl_tx_n - 1) & 7] == LL_TERMINATE_IND);
    check("blell: the event ring (32, oldest first, the last: advertising enabled again)",
          blell_has("ev ") && ble_dg.ev_n > 32 && strstr(blell_out, "close 0016") &&
              ble_dg.ev[(ble_dg.ev_n - 1) & 31].code == BDE_ENABLE);
    check("blell: no engine registers on the host (r.valid 0)", !blell_has("\ncol2 ") && !blell_has("hw_state "));
    check("blell: the protocol ring (64 \"pdu N: t=.. evt=.. rx|tx att|sig|smp|ll n=..: ..\" lines), MIDI counted",
          blell_has(" tx att n=") && blell_has(" rx sig n=") && blell_has("\r\npdus ") &&
              ble_dg.pdu_n > 64 && blell_has("att_ntf ") && ble_dg.att_ntf_n > 0 && ble_dg.att_wcmd_n > 0);
    ble_diag_clear();
    check("blell clear: counters and ring zero, the magic kept",
          !ble_dg.cind_rx && !ble_dg.ev_n && !ble_dg.closes && ble_dg.magic == BLE_DIAG_MAGIC &&
              ble_dg.first_evt == 0xFFFFu);
}

#if BLE_LL_ENC
static void hexs(const char *s, uint8_t *o, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        o[i] = (uint8_t)v;
    }
}

static void test_encryption(void)
{
    uint8_t d[24], ltk[16], ltk_be[16], payload[23], want[31];
    const uint8_t *r;
    int i, n;
    check("reconnect", connect(24, 300));
    c_events(2);
    hexs("4C68384139F574D836BCF34E9DFB01BF", ltk_be, 16);
    for (i = 0; i < 16; i++)
        ltk[i] = ltk_be[15 - i];
    memset(d, 0, sizeof d);
    d[0] = LL_ENC_REQ;
    d[1] = 0xA1, d[9] = 0x34, d[10] = 0x12;                  /* Rand, EDIV */
    ble_host_set_key(d + 1, 0x1234, ltk);
    hexs("1302F1E0DFCEBDAC", d + 11, 8);                      /* SKDm 0xACBDCEDFE0F10213 */
    hexs("24ABDCBA", d + 19, 4);                              /* IVm 0xBADCAB24 */
    hexs("7968574635241302" "BEBAAFDE", hw.rnd, 12);          /* SKDs 0x0213243546576879, IVs 0xDEAFBABE */
    c_ctrl(d, 23);
    c_event();
    r = c_ctrl_last(LL_ENC_RSP);
    check("ENC_REQ -> ENC_RSP (SKDs, IVs), START_ENC_REQ", r && !memcmp(r + 1, hw.rnd, 12) &&
          C.ctrl[(C.ctrl_n - 1) % 64][0] == LL_START_ENC_REQ && bll.enc_rx && !bll.enc_tx);
    hexs("99AD1B5226A37E3E058E3B8E27C2C666", d, 16);
    check("session key = the sample's SK", !memcmp(bll.ccm_rx.key, d, 16));
    {
        static const uint8_t s0[] = {0x0F, 0x05, 0x9F, 0xCD, 0xA7, 0xF4, 0x48};   /* the sample's packet 0 */
        memcpy(C.q[C.qn++], s0, sizeof s0);
    }
    memcpy(C.ctx.key, d, 16);                                 /* the central's side from here on */
    hexs("24ABDCBABEBAAFDE", C.ctx.iv, 8);
    C.crx = C.ctx;
    C.ctx.dir = 1;
    C.ctx.ctr = 1;
    C.crx.dir = 0;
    i = C.ctrl_n;
    C.enc = 1;
    c_event();
    check("the sample START_ENC_RSP decrypts; ours comes back encrypted (counter 0)",
          bll.enc_tx && C.ctrl_n == i + 1 && C.ctrl[i % 64][0] == LL_START_ENC_RSP && C.last_pdu[1] == 5);
    hexs("3534333231304142434445464748494A4B4C4D4E4F5051", payload, 23);
    hexs("F38881E7BD94C9C369B9A66846DD4786AA8C39CE540D0DAE3ADCDF89B96088", want, 31);
    C.enc = 0;                                                /* look at the raw packet */
    ble_ll_send(0x3637, payload, 23);
    c_event();
    check("our data packet 1 = the sample's (06 1F F3 88 ... 60 88)",
          C.last_pdu[1] == 31 && (C.last_pdu[0] & 3) == 2 && !memcmp(C.last_pdu + 2, want, 31));
    C.enc = 1;
    C.crx.ctr = 2;
    r = c_att((const uint8_t[]){0x0A, 3, 0}, 3, &n);
    check("encrypted both ways: Read Device Name", r && r[0] == 0x0B && n == 14);
    {
        uint8_t bad[2 + 8] = {0x02, 0x05, 1, 2, 3, 4, 5, 6, 7, 8};
        memcpy(C.q[C.qn++], bad, sizeof bad);
        C.enc = 0;
        c_event();
        check("a packet failing its MIC: connection lost", !hw.conn_on && hw.adv_on);
    }
}
#endif

#if BLE_SMP_LEGACY
/* the central starts the LL encryption with ltk (least significant octet first) for rand / ediv; 1 when both ways
 * run encrypted (its side of the session key computed here, independently of ble_ll.c) */
static int c_start_enc(const uint8_t ltk[16], const uint8_t rand[8], uint16_t ediv)
{
    static const uint8_t skdm[8] = {0x13, 0x02, 0xF1, 0xE0, 0xDF, 0xCE, 0xBD, 0xAC}, ivm[4] = {0x24, 0xAB, 0xDC, 0xBA};
    uint8_t d[23], k[16], skd[16];
    const uint8_t *r;
    int i, before;
    d[0] = LL_ENC_REQ;
    memcpy(d + 1, rand, 8);
    d[9] = (uint8_t)ediv, d[10] = (uint8_t)(ediv >> 8);
    memcpy(d + 11, skdm, 8);
    memcpy(d + 19, ivm, 4);
    hexs("7968574635241302" "BEBAAFDE", hw.rnd, 12);
    before = C.ctrl_n;
    c_ctrl(d, 23);
    c_event();
    r = c_ctrl_last(LL_ENC_RSP);
    if (!r || !c_ctrl_since(before, LL_START_ENC_REQ))
        return 0;
    for (i = 0; i < 8; i++) {                  /* SKD = SKDs || SKDm, most significant octet first */
        skd[i] = r[1 + 7 - i];
        skd[8 + i] = skdm[7 - i];
    }
    for (i = 0; i < 16; i++)
        k[i] = ltk[15 - i];
    ble_aes128(k, skd, C.ctx.key);
    memcpy(C.ctx.iv, ivm, 4);
    memcpy(C.ctx.iv + 4, r + 9, 4);
    C.ctx.ctr = 0, C.ctx.ctr_hi = 0;
    C.crx = C.ctx;
    C.ctx.dir = 1;
    C.crx.dir = 0;
    C.enc = 1;
    d[0] = LL_START_ENC_RSP;
    before = C.ctrl_n;
    c_ctrl(d, 1);
    c_event();
    return c_ctrl_since(before, LL_START_ENC_RSP) && ble_ll_encrypted();
}

/* a central pairing as a Mac would (bonding, MITM and Secure Connections asked, every key offered): legacy Just
 * Works is what our NoInputNoOutput leaves; the central's confirm and keys computed with c1 / s1 (checked against the
 * Core spec's samples in ble_prim_test.c) */
static void test_smp(void)
{
    static const uint8_t preq[7] = {0x01, 0x04, 0x00, 0x2D, 0x10, 0x0F, 0x0F}, init_a[6] = {1, 2, 3, 4, 5, 6};
    static const uint8_t zero16[16] = {0};
    uint8_t mrand[16], mconf[16], c[17], stk[16], srand[16], ltk[16], rand[8], want[16];
    uint16_t ediv;
    const uint8_t *r;
    int n, i;
    memset(&bhs_key, 0, sizeof bhs_key);       /* (no secret yet: test_encryption's key is gone) */
    check("reconnect", connect(24, 300));
    c_events(2);
    c_l2cap(6, preq, 7);
    r = c_frame(6, &n);
    check("SMP: Pairing Request (Mac-like) -> Response: NoInputNoOutput, bonding, no MITM / SC, 16, keys 07 / 03",
          r && n == 7 && r[0] == 0x02 && r[1] == 0x03 && r[2] == 0 && r[3] == 0x01 && r[4] == 16 && r[5] == 0x07 &&
              r[6] == 0x03);
    {
        uint8_t pres[7];
        memcpy(pres, r, 7);
        for (i = 0; i < 16; i++)
            mrand[i] = (uint8_t)(0x30 + i * 5);
        ble_smp_c1(zero16, mrand, preq, pres, 1, init_a, 1, ADDR, mconf);
        hexs("00112233445566778899AABBCCDDEEFF", hw.rnd, 16);   /* (our Srand) */
        c[0] = 0x03;
        memcpy(c + 1, mconf, 16);
        c_l2cap(6, c, 17);
        r = c_frame(6, &n);
        check("SMP: Mconfirm -> our Sconfirm", r && n == 17 && r[0] == 0x03 && bsmp.st == S_RANDOM);
        memcpy(want, r ? r + 1 : zero16, 16);
        c[0] = 0x04;
        memcpy(c + 1, mrand, 16);
        c_l2cap(6, c, 17);
        r = c_frame(6, &n);
        check("SMP: Mrand -> our Srand", r && n == 17 && r[0] == 0x04 && !memcmp(r + 1, hw.rnd, 16));
        memcpy(srand, r ? r + 1 : zero16, 16);
        ble_smp_c1(zero16, srand, preq, pres, 1, init_a, 1, ADDR, c);
        check("SMP: Sconfirm = c1(0, Srand, preq, pres, random / random, InitA, AdvA)", !memcmp(c, want, 16));
    }
    ble_smp_s1(zero16, srand, mrand, stk);
    check("SMP: the LL encryption with STK = s1(0, Srand, Mrand), EDIV 0, Rand 0", c_start_enc(stk, zero16, 0));
    r = c_frame(6, &n);
    check("SMP: Encryption Information (our LTK), encrypted", r && n == 17 && r[0] == 0x06 && memcmp(r + 1, zero16, 16));
    memcpy(ltk, r ? r + 1 : zero16, 16);
    r = c_frame(6, &n);
    check("SMP: Master Identification (EDIV, Rand)", r && n == 11 && r[0] == 0x07);
    ediv = r ? (uint16_t)(r[1] | r[2] << 8) : 0;
    memcpy(rand, r ? r + 3 : zero16, 8);
    check("SMP: the secret handed to the firmware once, never the key on air (ble_app_bond)",
          bond.n == 1 && memcmp(bond.ltk, ltk, 16) && bsmp.st == S_KEYS && bsmp.theirs == 0x07);
    {
        uint8_t back[16];
        check("SMP: the key as sent is found again from its EDIV and Rand",
              ble_host_ltk(rand, ediv, back) && !memcmp(back, ltk, 16));
    }
    r = c_frame(6, &n);
    check("SMP: our Identity Information (an IRK), after the EncKey", r && n == 17 && r[0] == 0x08 &&
          memcmp(r + 1, zero16, 16));
    {
        uint8_t irk[16], again[16];
        smp_own_irk(irk);
        memset(hw.rnd, 0xC3, sizeof hw.rnd);
        smp_own_irk(again);
        check("SMP: our IRK is our own fixed one, the same at every pairing (macOS refuses a new IRK for an "
              "identity it bonded: \"already paired, with a different irk\")",
              r && !memcmp(r + 1, irk, 16) && !memcmp(irk, again, 16));
    }
    r = c_frame(6, &n);
    check("SMP: our Identity Address Information: our random static address (type 1)",
          r && n == 8 && r[0] == 0x09 && r[1] == 1 && !memcmp(r + 2, ADDR, 6));
    memset(c, 0x55, sizeof c);
    c[0] = 0x06;                               /* the central's keys: LTK, EDIV / Rand, IRK, identity address, CSRK */
    c_l2cap(6, c, 17);
    c[0] = 0x07;
    c_l2cap(6, c, 11);
    c[0] = 0x08;
    c_l2cap(6, c, 17);
    c[0] = 0x09, c[1] = 0;
    c_l2cap(6, c, 8);
    c[0] = 0x0A;
    c_l2cap(6, c, 17);
    c_event();
    check("SMP: the central's keys taken: pairing done", bsmp.st == S_IDLE && bsmp.theirs == 0 &&
                                                          c_frame(6, &n) == 0);
    {
        uint8_t k55[16];
        memset(k55, 0x55, 16);
        check("SMP: the central's identity handed to the firmware (ble_app_peer_id: its IRK, identity address, public)",
              peer_id.n == 1 && !memcmp(peer_id.irk, k55, 16) && !memcmp(peer_id.addr, k55, 6) && peer_id.rnd == 0);
    }
    r = c_att((const uint8_t[]){0x0A, 3, 0}, 3, &n);
    check("encrypted with the STK: Read Device Name", r && r[0] == 0x0B && n == 14);
    c_ctrl((const uint8_t[]){LL_TERMINATE_IND, 0x13}, 2);
    c_event();
    C.enc = 0;
    check("reconnect (bonded)", connect(24, 300));
    c_events(2);
    check("a returning central: LL encryption with the bond's LTK (EDIV, Rand)", c_start_enc(ltk, rand, ediv));
    r = c_att((const uint8_t[]){0x0A, 3, 0}, 3, &n);
    check("encrypted with the LTK: Read Device Name; no SMP", r && r[0] == 0x0B && n == 14 && c_frame(6, &n) == 0);
    c_ctrl((const uint8_t[]){LL_TERMINATE_IND, 0x13}, 2);
    c_event();
    C.enc = 0;
    check("reconnect", connect(24, 300));
    c_events(2);
    rand[0] ^= 1;
    {
        const uint8_t *rj;
        uint8_t d[23] = {LL_ENC_REQ};
        memcpy(d + 1, rand, 8);
        d[9] = (uint8_t)ediv, d[10] = (uint8_t)(ediv >> 8);
        c_ctrl(d, 23);
        c_event();
        rj = c_ctrl_last(LL_REJECT_IND);
        check("an unknown Rand: ENC_RSP, REJECT_IND PIN or Key Missing (0x06; no feature exchange: not EXT)", rj &&
              rj[1] == 0x06 && c_ctrl_last(LL_ENC_RSP) && !ble_ll_encrypted());
    }
    c_l2cap(6, preq, 7);
    r = c_frame(6, &n);
    memset(c, 0x77, sizeof c);
    c[0] = 0x03;
    c_l2cap(6, c, 17);
    r = c_frame(6, &n);
    c[0] = 0x04;                               /* a random that does not match the confirm */
    c_l2cap(6, c, 17);
    r = c_frame(6, &n);
    check("SMP: a confirm that does not match its random -> Pairing Failed, Confirm Value Failed (0x04)",
          r && n == 2 && r[0] == 0x05 && r[1] == 0x04 && bsmp.st == S_IDLE);
    {
        int dpdus = 0;
        for (i = 0; i < BLE_DIAG_PDUS && i < (int)ble_dg.pdu_n; i++)
            dpdus += (ble_dg.pdu[i].ch & 0x7F) == BDP_SMP || (ble_dg.pdu[i].ch & 0x7F) == BDP_LL;
        check("blell: SMP and LL encryption PDUs in the protocol ring, ENC_REQs and starts counted",
              dpdus > 8 && ble_dg.enc_req_n >= 3 && ble_dg.enc_on_n >= 2);
    }
}
#endif

#if BLE_CENTRAL
/* a raw report as the driver hands it over: header, AdvA {a0, 0x11, ..}, n octets of AD */
static uint8_t scan_rep(uint8_t *p, uint8_t type, uint8_t a0, uint8_t n)
{
    uint8_t i;
    p[0] = (uint8_t)(type | 0x40u);
    p[1] = (uint8_t)(6u + n);
    p[2] = a0, p[3] = 0x11, p[4] = 0x22, p[5] = 0x33, p[6] = 0x44, p[7] = 0xC5;
    for (i = 0; i < n; i++)
        p[8 + i] = (uint8_t)(0x30 + i);
    return (uint8_t)(8u + n);
}

/* the scan for the DEVICES list (BLE_CENTRAL, docs/BLE-DEVICES-DESIGN.md §1, docs/BLE-STACK.md §9.1): one link, time-sliced with
 * advertising, never while connected; the reports' ring between the RX interrupt and the main loop */
static void test_scan(void)
{
    uint8_t p[64], q[64], n, k;
    uint16_t rssi;
    int adv0;
    if (ble_ll_connected()) {
        c_ctrl((const uint8_t[]){LL_TERMINATE_IND, 0x13}, 2);
        c_event();
    }
    ble_enable(1);
    check("scan: advertising first", hw.adv_on && !hws.on && !ble_ll_scanning());
    ble_ll_scan(1);
    check("scan: DEVICES open: advertising stops, the scan starts (40 ms, 37.5 ms window, active, our address)",
          !hw.adv_on && hws.on && ble_ll_scanning() && hws.s.interval == 64u && hws.s.window == 60u && hws.s.active &&
          hws.s.own_rand == 1u && !memcmp(hws.own, ADDR, 6));
    n = scan_rep(p, 0x0, 0x01, 20);
    ble_ll_hw_adv_report(p, n, 0x0A12, 37);
    n = scan_rep(p, 0x4, 0x01, 12);
    ble_ll_hw_adv_report(p, n, 0x0A13, 38);
    n = scan_rep(p, 0x3, 0x02, 6);                 /* a SCAN_REQ, an ADV_NONCONN_IND, an ADV_SCAN_IND: dropped */
    ble_ll_hw_adv_report(p, n, 0, 39);
    n = scan_rep(p, 0x2, 0x02, 6);
    ble_ll_hw_adv_report(p, n, 0, 39);
    n = scan_rep(p, 0x6, 0x02, 6);
    ble_ll_hw_adv_report(p, n, 0, 39);
    n = ble_ll_scan_take(q, &rssi);
    check("scan: the ADV_IND taken first, whole, with its RSSI word", n == 28u && q[0] == 0x40u && q[2] == 0x01u &&
          q[8] == 0x30u && rssi == 0x0A12u);
    n = ble_ll_scan_take(q, &rssi);
    check("scan: then its SCAN_RSP", n == 20u && (q[0] & 15u) == 4u && rssi == 0x0A13u);
    check("scan: SCAN_REQ / non-connectable reports dropped in the interrupt (counted)",
          ble_ll_scan_take(q, &rssi) == 0u && ble_dgs.rep_other == 3u && ble_dgs.rep_adv_ind == 1u);
    for (k = 0; k < 20u; k++) {
        n = scan_rep(p, 0x0, (uint8_t)(0x40 + k), 4);
        ble_ll_hw_adv_report(p, n, k, 37);
    }
    for (k = 0; ble_ll_scan_take(q, &rssi); k++)
        ;
    check("scan: the ring holds 16 (BLE_SCAN_RING); the rest counted lost, the main loop gets them in order", k == 16u &&
          ble_dgs.ring_full == 4u && q[2] == 0x4Fu);
    n = scan_rep(p, 0x0, 0x01, 40);
    ble_ll_hw_adv_report(p, (uint8_t)(n > 39u ? 48u : n), 0, 37);
    check("scan: a report longer than an advertising PDU is not taken", ble_ll_scan_take(q, &rssi) == 0u);
    {
        uint8_t ci[36];
        connect_ind(ci, 0x50654A6B, 2, 3, 24, 0, 72, ALL, 7);
        check("scan: a CONNECT_IND while scanning is not taken (not advertising)", !ble_ll_hw_connect_ind(ci, 36));
    }
    adv0 = hw.adv_starts;
    ble_ll_scan(0);
    check("scan: DEVICES left: the scan stops, advertising again", !hws.on && hw.adv_on && hw.adv_starts == adv0 + 1 &&
          !ble_ll_scanning());
    n = scan_rep(p, 0x0, 0x01, 4);
    ble_ll_hw_adv_report(p, n, 0, 37);
    check("scan: a report after the scan stopped is not taken", ble_ll_scan_take(q, &rssi) == 0u);
    ble_ll_scan(1);
    ble_enable(0);
    check("scan: BLUETOOTH OFF while scanning: the radio quiet", !hws.on && !hw.adv_on && !ble_ll_scanning());
    ble_enable(1);
    check("scan: ON again with DEVICES still open: scanning, not advertising", hws.on && !hw.adv_on && ble_ll_scanning());
    ble_ll_scan(0);
    check("scan: connect: advertising first", hw.adv_on && connect(24, 300));
    ble_ll_scan(1);
    check("scan: DEVICES opened while connected: the link stays, no scan", ble_ll_connected() && !hws.on);
    c_ctrl((const uint8_t[]){LL_TERMINATE_IND, 0x13}, 2);
    c_event();
    check("scan: the link ends with DEVICES open: scanning (not advertising)", !ble_ll_connected() && hws.on &&
          !hw.adv_on);
    ble_ll_scan(0);
    check("scan: DEVICES left: advertising", !hws.on && hw.adv_on);
}
#endif

int main(void)
{
    test_advertising();
    test_connect_ll();
    test_gatt();
    test_midi();
    test_audio_interval();
    test_l2cap();
    test_endings();
    test_blell();
#if BLE_LL_ENC
    test_encryption();
#endif
#if BLE_SMP_LEGACY
    test_smp();
#endif
#if BLE_CENTRAL
    test_scan();
#endif
    printf("%s\n", fails ? "BLE stack: FAILED" : "BLE stack: all passed");
    return fails != 0;
}
