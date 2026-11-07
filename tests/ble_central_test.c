/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the BLE stack's central role (BLE_CENTRAL: firmware/src/ble/ble_ll_central.c, ble_gattc.c, ble_central.c,
 * ble_smp_init.c) against a simulated BLE-MIDI peripheral behind a fake baseband (ble_hw.h): the mirror of
 * tests/ble_stack_test.c's simulated central. One connection event: the master's PDUs are taken (all acknowledged),
 * then the peripheral's are delivered, then the event ends.
 *   initiator     CONNECT_IND: InitA / AdvA / TxAdd / RxAdd, a valid random AA, a random CRCInit, WinSize 2,
 *                 WinOffset in [Interval / 2, Interval - 1], interval 9, latency 0, timeout 200, 37 channels, hop
 *                 5..16, SCA 0; advertising stopped first; cancel back to advertising
 *   master LL     we start the version and feature exchanges and the data length; the peripheral's feature exchange,
 *                 PHY request (no change), connection parameters request (an update at counter + 8..11), L2CAP
 *                 connection parameter update request; our channel map update (+ 7..10); LL encryption as master
 *   GATT client   MTU, Find By Type Value, Read By Type, Find Information, the CCCD write, notifications in, MIDI out
 *                 as Write Commands; a server without Find By Type Value; no BLE-MIDI service
 *   SMP           Insufficient Authentication -> legacy Just Works as initiator (c1 / s1 computed on the peripheral's
 *                 side), the STK encryption, its keys then ours, the keys to the firmware, the CCCD again; a Security
 *                 Request; a wrong Sconfirm; Pairing Failed; a bond reused (ENC_REQ with its EDIV / Rand); a bond
 *                 the peripheral lost (Key Missing -> pairing); an iPhone-like peripheral (blell-dev3: Insufficient
 *                 Authentication after Just Works, a late Pairing Failed): NEED_MITM, then legacy passkey entry with us
 *                 displaying (its user types the passkey, TK = the passkey), the authenticated bond reused, a bond it
 *                 lost (straight to the passkey), a passkey typed wrong, a peer that cannot type, one that wants Secure
 *                 Connections (AUTH, no loop), the 30 s SMP timeout while nobody types, a Security Request asking MITM
 *   RPA           ah() with the Core spec's sample (Vol 3 Part H D.7)
 *   endings       no packet from the peripheral (0x3E), its terminate, ours
 * Built at each level of BLE_CENTRAL (ble_cfg.h; tests/run_tests.sh): 1 all of the above; 3 (no pairing) a peer that asks for nothing ready as at every level, one
 * that asks for security (Insufficient Authentication, a Security Request) left with NEED_PAIR, nothing of SMP sent but
 * the Pairing Failed that answers a Security Request; with BLE_BOND=1 too (the responder kept beside it). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#include "../firmware/src/ble/ble_prim.c"
#include "../firmware/src/ble/ble_aes.c"
#include "../firmware/src/ble/ble_ll.c"
#include "../firmware/src/ble/ble_smp.c"
#include "../firmware/src/ble/ble_host.c"
#include "../firmware/src/ble/ble_att.c"
#include "../firmware/src/ble/ble_midi.c"
#include "../firmware/src/ble/ble_diag.c"
#include "../firmware/src/ble/ble_store.c"
#include "../firmware/src/ble/ble_scan.c"
#include "../firmware/src/ble/ble_gattc.c"
#include "../firmware/src/ble/ble_central.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#if !BLE_SMP_LEGACY
/* BLE_CENTRAL 3 without BLE_BOND: the stack has no c1 / s1 (no SMP at all); the simulated responder must never need
 * them, as nothing of ours pairs: reaching one is a failure */
static void ble_smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
                       uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16])
{
    (void)k, (void)r, (void)preq, (void)pres, (void)iat, (void)ia, (void)rat, (void)ra;
    memset(out, 0, 16);
    check("no pairing at this level: the responder's c1 never reached", 0);
}
static void ble_smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16])
{
    (void)k, (void)r1, (void)r2;
    memset(out, 0, 16);
    check("no pairing at this level: the responder's s1 never reached", 0);
}
#endif

/* ------------------------------------------------------------------------------------------ the fake driver */
static struct {
    int adv_on, conn_on, init_on, init_starts, init_stops, upd_n, chm_n;
    struct ble_hw_init in;
    uint8_t cind[36];
    struct ble_hw_conn_upd upd;
    uint8_t chm[5];
    uint16_t chm_instant;
    uint8_t rnd_fill;
} hw;
static uint32_t now_us = 1000000, rng = 0x2468ACE1u;

void ble_hw_adv_start(const struct ble_hw_adv *a) { (void)a, hw.adv_on = 1; }
void ble_hw_adv_stop(void) { hw.adv_on = 0; }
void ble_hw_conn_start(const struct ble_hw_conn *c) { (void)c, hw.conn_on = 1; }
void ble_hw_conn_stop(void) { hw.conn_on = 0; }
void ble_hw_scan_start(const struct ble_hw_scan *s) { (void)s; }
void ble_hw_scan_stop(void) {}
void ble_hw_init_start(const struct ble_hw_init *i)
{
    hw.adv_on = 0;
    hw.init_on = 1;
    hw.init_starts++;
    hw.in = *i;
    memcpy(hw.cind, i->cind, 36);
}
void ble_hw_init_stop(void) { hw.init_on = 0, hw.init_stops++; }
void ble_hw_conn_update(const struct ble_hw_conn_upd *u) { hw.upd = *u, hw.upd_n++; }
void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant) { memcpy(hw.chm, chm, 5), hw.chm_instant = instant, hw.chm_n++; }
void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx) { (void)max_tx, (void)max_rx; }
void ble_hw_tx_kick(void) {}
uint32_t ble_hw_time_us(void) { return now_us; }
uint32_t ble_hw_diag_now(void) { return now_us; }
static const uint8_t OWN[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
uint8_t ble_hw_addr(uint8_t addr[6]) { memcpy(addr, OWN, 6); return 1; }
void ble_hw_rand(uint8_t *out, uint8_t n)
{
    while (n--) {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        *out++ = (uint8_t)rng;
    }
}

/* ------------------------------------------------------------------------------------------ the firmware */
static uint32_t out_q[64], out_r, out_w, in_pkt[64], in_n, state_calls;
int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    if (out_r == out_w)
        return 0;
    *pkt = out_q[out_r % 64];
    *t = 500;
    return 1;
}
void ble_app_midi_pop(void) { out_r++; }
void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last) { (void)ts, (void)last, in_pkt[in_n++ % 64] = pkt; }
void ble_app_state(void) { state_calls++; }
void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand, (void)ediv, (void)ltk; }
void ble_app_peer_id(const uint8_t irk[16], const uint8_t a[6], uint8_t r) { (void)irk, (void)a, (void)r; }
static struct ble_keys got_keys;
static int got_keys_n;
void ble_app_central_keys(const struct ble_keys *k) { got_keys = *k, got_keys_n++; }

/* ------------------------------------------------------------------------------------- the peripheral */
static const uint8_t PADDR[6] = {0x37, 0x37, 0x4B, 0x65, 0x79, 0xC4};   /* random static */
static const uint8_t PID[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xC9};     /* its identity (Identity Address Info.) */
static const uint8_t MIDI_IO[16] = {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB,
                                    0xE5, 0x72, 0x77};
enum { PH_GAP = 1, PH_NAME_D, PH_NAME, PH_BAT, PH_BAT_D, PH_BAT_V, PH_MIDI, PH_MIDI_D, PH_MIDI_V, PH_CCCD, PH_DESC,
       PH_DIS, PH_DIS_D, PH_DIS_V, PH_LAST = PH_DIS_V };
static struct {
    int alive, need_auth, no_fbtv, no_midi, sec_req, wrong_conf, fail_pair, lost_bond;
    uint8_t q[96][2 + 255];
    int qn;
    uint8_t l2[700];
    int have, need;
    uint8_t ctrl[96][32];
    int ctrl_n;
    uint8_t att_ops[64];
    int att_n;
    uint16_t evt;
    int enc_rx, enc_tx;
    struct ble_ccm ctx, crx;
    uint8_t ltk[16], rand[8], irk[16];          /* its bond as handed out, and its IRK */
    uint8_t our_irk[16];                        /* the IRK we (the master) sent it */
    uint8_t our_ltk[16], our_edrand[10];        /* the key we sent it: what it asks for when it connects to us */
    uint16_t ediv;
    uint8_t preq[7], pres[7], mconf[16], srand[16], stk[16];
    int smp_st, paired, keys_from_master;
    int cccd, writes, midi_ok;
    uint8_t wcmd[8];
    int ltk_used;
    /* an iPhone app's MIDI characteristic (blell-dev3, docs/BLE-STACK.md §9.2): authenticated encryption needed */
    int mitm_need, iphone, sc_only, io;        /* CCCD only with an authenticated key; its Pairing Response AuthReq 01
                                                * and a Pairing Failed after a Just Works pairing; refused even after a
                                                * legacy passkey (a Secure Connections-only peer); its IO capability */
    int pk, enc_auth, ltk_auth;                /* passkey entry chosen; the link's key authenticated; its bond's */
    uint8_t preq_first[7];                     /* the link's first Pairing Request */
    int pair_reqs, pause_n;                    /* Pairing Requests on this link; our LL_PAUSE_ENC_REQs */
    int enc_stk;                               /* the link's key is this pairing's STK (its keys may go) */
    int no_repair, mute_repair, no_pause;      /* a pairing on the encrypted link: refused / unanswered; the
                                                * encryption pause unknown to it */
    int late_after, late_pending;              /* its late Pairing Failed only after its next ATT error */
    long typed;                                /* the passkey its user typed (-1: not yet) */
    uint8_t tk[16];
    /* the encryption start (Core Vol 6 Part B 5.1.3.1) as the iPhone kept it (blell-dev6): from our LL_ENC_REQ to our
     * LL_START_ENC_RSP no other PDU of ours, else it leaves with MIC Failure (0x3D), as NimBLE / Zephyr do */
    int enc_start, dying, mic_terms;           /* in the start; its TERMINATE_IND queued; those sent */
    int own_len, len_always, len_done;         /* its LL_LENGTH_REQ on our LL_ENC_REQ when no length exchange was
                                                * made yet (the iPhone, blell-dev6) / always; one was made */
    int enc_wait, mic_on_enc;                  /* events until its LL_ENC_RSP (its host's LTK lookup); it leaves
                                                * with 0x3D on our LL_ENC_REQ (a key it cannot use) */
    uint8_t enc_req[23];
} P;

static void p_reset(void)
{
    uint8_t keep_ltk[16], keep_rand[8];
    uint16_t ediv = P.ediv;
    int lost = P.lost_bond, la = P.ltk_auth;
    memcpy(keep_ltk, P.ltk, 16), memcpy(keep_rand, P.rand, 8);
    memset(&P, 0, sizeof P);
    memcpy(P.ltk, keep_ltk, 16), memcpy(P.rand, keep_rand, 8);
    P.ediv = ediv;
    P.lost_bond = lost;
    P.ltk_auth = la;
    P.typed = -1;
    P.io = 0x03;                               /* NoInputNoOutput: a controller (Just Works); iphone(): it can type */
    P.alive = 1;
    memset(P.irk, 0xA7, 16);
}

static void p_pdu(uint8_t llid, const uint8_t *d, int n)
{
    uint8_t *q = P.q[P.qn++];
    q[0] = llid;
    q[1] = (uint8_t)n;
    memcpy(q + 2, d, n);
    if (P.enc_tx && n) {
        ble_ccm_encrypt(&P.ctx, q[0], q + 2, (uint8_t)n);
        q[1] = (uint8_t)(n + 4);
    }
}
static void p_ctrl(const uint8_t *d, int n) { p_pdu(3, d, n); }
static void p_l2(uint16_t cid, const uint8_t *d, int n)
{
    uint8_t f[600];
    int off = 0;
    f[0] = (uint8_t)n, f[1] = (uint8_t)(n >> 8), f[2] = (uint8_t)cid, f[3] = (uint8_t)(cid >> 8);
    memcpy(f + 4, d, n);
    while (off < n + 4) {
        int k = n + 4 - off < 27 ? n + 4 - off : 27;
        p_pdu(off ? 1 : 2, f + off, k);
        off += k;
    }
}
static void p_att(const uint8_t *d, int n) { p_l2(4, d, n); }
static void p_att_err(uint8_t op, uint16_t h, uint8_t e)
{
    uint8_t d[5] = {0x01, op, (uint8_t)h, (uint8_t)(h >> 8), e};
    p_att(d, 5);
}

/* it leaves with MIC Failure (0x3D): its TERMINATE_IND, then nothing more from it */
static void p_mic_term(void)
{
    static const uint8_t t[2] = {LL_TERMINATE_IND, 0x3D};
    p_ctrl(t, 2);
    P.mic_terms++;
    P.dying = 1;
    P.enc_start = P.enc_wait = 0;
}
static void p_enc_answer(const uint8_t *p);

/* its LL: the master's control PDUs */
static void p_ll(const uint8_t *p, int n)
{
    uint8_t d[32];
    memcpy(P.ctrl[P.ctrl_n % 96], p, n < 32 ? n : 32);
    P.ctrl_n++;
    switch (p[0]) {
    case LL_VERSION_IND:
        d[0] = LL_VERSION_IND, d[1] = 10, d[2] = 0x4C, d[3] = 0, d[4] = 1, d[5] = 0;
        p_ctrl(d, 6);
        return;
    case LL_FEATURE_REQ:
        memset(d, 0, 9);
        d[0] = LL_FEATURE_RSP, d[1] = 0x21;     /* encryption, DLE */
        p_ctrl(d, 9);
        return;
    case LL_LENGTH_REQ:
        P.len_done = 1;
        d[0] = LL_LENGTH_RSP, d[1] = 251, d[2] = 0, d[3] = 0x48, d[4] = 0x08, d[5] = 251, d[6] = 0, d[7] = 0x48, d[8] = 0x08;
        p_ctrl(d, 9);
        return;
    case LL_LENGTH_RSP:
        P.len_done = 1;
        return;
    case LL_ENC_REQ:
        if (P.mic_on_enc) {
            p_mic_term();
            return;
        }
        P.enc_start = 1;
        if ((P.own_len && !P.len_done) || P.len_always) {
            d[0] = LL_LENGTH_REQ, d[1] = 251, d[2] = 0, d[3] = 0x48, d[4] = 0x08, d[5] = 251, d[6] = 0, d[7] = 0x48,
            d[8] = 0x08;
            p_ctrl(d, 9);                      /* (its own, crossing ours) */
            memcpy(P.enc_req, p, 23);
            P.enc_wait = 3;                    /* (its LL_ENC_RSP a few events later: the LTK from its host) */
            return;
        }
        p_enc_answer(p);
        return;
    case LL_START_ENC_RSP:
        P.enc_start = 0;
        P.enc_tx = 1;
        d[0] = LL_START_ENC_RSP;
        p_ctrl(d, 1);
        return;
    case LL_PAUSE_ENC_REQ:                     /* (5.1.3.2) ours encrypted, then it receives plain */
        P.pause_n++;
        if (P.no_pause) {
            d[0] = LL_UNKNOWN_RSP, d[1] = LL_PAUSE_ENC_REQ;
            p_ctrl(d, 2);
            return;
        }
        d[0] = LL_PAUSE_ENC_RSP;
        p_ctrl(d, 1);
        P.enc_rx = 0;
        return;
    case LL_PAUSE_ENC_RSP:                     /* the master's, plain: it sends plain too */
        P.enc_tx = 0;
        return;
    case LL_TERMINATE_IND:
        P.alive = 0;
        return;
    default:
        return;
    }
}

/* its answer to our LL_ENC_REQ (p): the key by EDIV / Rand (the STK, its bond) or Key Missing */
static void p_enc_answer(const uint8_t *p)
{
    uint8_t d[32], key[16], skd[16], i;
    static const uint8_t zero[8] = {0};
    uint16_t ediv = (uint16_t)(p[9] | p[10] << 8);
    if (!memcmp(p + 1, zero, 8) && !ediv && P.smp_st == 4) {
        memcpy(key, P.stk, 16);
        P.enc_auth = P.pk;
        P.enc_stk = 1;
    } else if (!P.lost_bond && ediv == P.ediv && !memcmp(p + 1, P.rand, 8) && P.ediv) {
        memcpy(key, P.ltk, 16);
        P.ltk_used++;
        P.enc_auth = P.ltk_auth;
    } else {
        d[0] = LL_REJECT_EXT_IND, d[1] = LL_ENC_REQ, d[2] = 0x06;
        p_ctrl(d, 3);
        P.enc_start = 0;
        return;
    }
    d[0] = LL_ENC_RSP;
    for (i = 0; i < 12; i++)
        d[1 + i] = (uint8_t)(0x90 + i);     /* SKDs, IVs */
    p_ctrl(d, 13);
    for (i = 0; i < 8; i++)
        skd[i] = d[1 + 7 - i], skd[8 + i] = p[11 + 7 - i];
    for (i = 0; i < 16; i++)
        P.ctx.key[i] = key[15 - i];
    ble_aes128(P.ctx.key, skd, P.ctx.key);
    memcpy(P.ctx.iv, p + 19, 4);
    memcpy(P.ctx.iv + 4, d + 9, 4);
    P.ctx.ctr = 0, P.ctx.ctr_hi = 0;
    P.crx = P.ctx;
    P.ctx.dir = 0, P.crx.dir = 1;
    d[0] = LL_START_ENC_REQ;
    p_ctrl(d, 1);
    P.enc_rx = 1;
}

/* its SMP responder (legacy Just Works, bonding): c1 / s1 from the stack's primitives (checked against the Core
 * spec's samples in ble_prim_test.c) */
/* its Sconfirm (with a passkey: once its user typed it, TK = what was typed) */
static void p_sconfirm(void)
{
    uint8_t c[17];
    memset(P.tk, 0, 16);
    if (P.pk) {
        P.tk[0] = (uint8_t)P.typed, P.tk[1] = (uint8_t)(P.typed >> 8), P.tk[2] = (uint8_t)(P.typed >> 16);
    }
    memset(P.srand, 0x3C, 16);
    c[0] = 0x03;
    ble_smp_c1(P.tk, P.srand, P.preq, P.pres, 1, OWN, 1, PADDR, c + 1);
    if (P.wrong_conf)
        c[1] ^= 1;
    p_l2(6, c, 17);
    P.smp_st = 2;
}

static void p_smp(const uint8_t *p, int n)
{
    uint8_t c[17];
    (void)n;
    switch (p[0]) {
    case 0x01:                                 /* Pairing Request */
        if (!P.pair_reqs++)
            memcpy(P.preq_first, p, 7);
        P.enc_stk = 0;
        if (P.enc_rx && P.mute_repair)
            return;                            /* (on the encrypted link: no answer) */
        if (P.fail_pair || (P.enc_rx && P.no_repair)) {
            c[0] = 0x05, c[1] = 0x08;
            p_l2(6, c, 2);
            return;
        }
        memcpy(P.preq, p, 7);
        P.pres[0] = 0x02, P.pres[1] = (uint8_t)(P.io ? P.io : 0x04), P.pres[2] = 0,
        P.pres[3] = P.iphone ? 0x01 : 0x0D, P.pres[4] = 16, P.pres[5] = p[5] & 3, P.pres[6] = p[6] & 3;
        /* KeyboardDisplay, bonding + MITM + SC asked (an iPhone: bonding only, as blell-dev3 saw): legacy, with ours */
        P.pk = ((p[3] | P.pres[3]) & 0x04) && (p[1] == 0x00 || p[1] == 0x01 || p[1] == 0x04) &&
               (P.pres[1] == 0x02 || P.pres[1] == 0x04);   /* (Table 2.8: the initiator displays, its user types) */
        p_l2(6, P.pres, 7);
        P.smp_st = 1;
        return;
    case 0x03:                                 /* Mconfirm */
        memcpy(P.mconf, p + 1, 16);
        if (P.pk && P.typed < 0) {
            P.smp_st = 10;                     /* (its user has not typed the passkey yet) */
            return;
        }
        p_sconfirm();
        return;
    case 0x04:                                 /* Mrand */
        ble_smp_c1(P.tk, p + 1, P.preq, P.pres, 1, OWN, 1, PADDR, c);
        if (memcmp(c, P.mconf, 16)) {
            c[0] = 0x05, c[1] = 0x04;
            p_l2(6, c, 2);
            P.smp_st = -100;
            return;
        }
        c[0] = 0x04;
        memcpy(c + 1, P.srand, 16);
        p_l2(6, c, 17);
        ble_smp_s1(P.tk, P.srand, p + 1, P.stk);
        P.smp_st = 4;
        return;
    case 0x06: case 0x07: case 0x08: case 0x09:   /* the master's keys (phase 3) */
        P.keys_from_master++;
        if (p[0] == 0x06)
            memcpy(P.our_ltk, p + 1, 16);
        if (p[0] == 0x07)
            memcpy(P.our_edrand, p + 1, 10);
        if (p[0] == 0x08)
            memcpy(P.our_irk, p + 1, 16);
        if (p[0] == 0x09) {
            P.paired = 1;
            if (P.iphone && !P.pk && P.late_after)
                P.late_pending = 1;
            else if (P.iphone && !P.pk) {      /* (iOS after a Just Works pairing, blell-dev3 pdu 121 / 160) */
                c[0] = 0x05, c[1] = 0x08;
                p_l2(6, c, 2);
            }
        }
        return;
    case 0x05:                                 /* Pairing Failed from the master */
        P.smp_st = -(int)p[1];
        return;
    default:
        return;
    }
}

static void p_keys(void)                       /* phase 3 (after the STK encryption): its keys first */
{
    uint8_t c[17];
    int i;
    for (i = 0; i < 16; i++)
        P.ltk[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 8; i++)
        P.rand[i] = (uint8_t)(0x70 + i);
    P.ediv = 0xBEEF;
    P.ltk_auth = P.pk;                         /* (its bond is as authenticated as the pairing was) */
    P.lost_bond = 0;
    c[0] = 0x06, memcpy(c + 1, P.ltk, 16), p_l2(6, c, 17);
    c[0] = 0x07, c[1] = 0xEF, c[2] = 0xBE, memcpy(c + 3, P.rand, 8), p_l2(6, c, 11);
    if (P.pres[6] & 2) {                       /* (its identity: only when agreed) */
        c[0] = 0x08, memcpy(c + 1, P.irk, 16), p_l2(6, c, 17);
        c[0] = 0x09, c[1] = 1, memcpy(c + 2, PID, 6), p_l2(6, c, 8);
    }
    P.smp_st = 5;
}

/* its ATT server: GAP 1-3, Battery 4-6, BLE-MIDI 7-11 (value 9, CCCD 10, a description 11), Device Information 12-14 */
static void p_att_rx(const uint8_t *p, int n)
{
    uint8_t r[64];
    uint16_t s, e;
    P.att_ops[P.att_n++ % 64] = p[0];
    switch (p[0]) {
    case 0x0A:
        /* A peer with an unsupported initial read must still allow CCC setup. */
        p_att_err(0x0A, (uint16_t)(p[1] | p[2] << 8), 0x02);
        return;
    case 0x02:
        r[0] = 0x03, r[1] = 185, r[2] = 0;
        p_att(r, 3);
        return;
    case 0x06:                                 /* Find By Type Value: primary service, the MIDI UUID */
        if (P.no_fbtv) {
            p_att_err(0x06, 0, 0x06);
            return;
        }
        if (P.no_midi || n != 23 || memcmp(p + 7, BLE_UUID_MIDI_SVC, 16)) {
            p_att_err(0x06, 1, 0x0A);
            return;
        }
        r[0] = 0x07, r[1] = PH_MIDI, r[2] = 0, r[3] = PH_DESC, r[4] = 0;
        p_att(r, 5);
        return;
    case 0x10:                                 /* Read By Group Type (primary services) */
        s = (uint16_t)(p[1] | p[2] << 8);
        if (s <= PH_GAP) {
            r[0] = 0x11, r[1] = 6, r[2] = PH_GAP, r[3] = 0, r[4] = PH_NAME, r[5] = 0, r[6] = 0x00, r[7] = 0x18;
            r[8] = PH_BAT, r[9] = 0, r[10] = PH_BAT_V, r[11] = 0, r[12] = 0x0F, r[13] = 0x18;
            p_att(r, 14);
        } else if (s <= PH_MIDI && !P.no_midi) {
            r[0] = 0x11, r[1] = 20, r[2] = PH_MIDI, r[3] = 0, r[4] = PH_DESC, r[5] = 0;
            memcpy(r + 6, BLE_UUID_MIDI_SVC, 16);
            p_att(r, 22);
        } else if (s <= PH_DIS) {
            r[0] = 0x11, r[1] = 6, r[2] = PH_DIS, r[3] = 0, r[4] = PH_DIS_V, r[5] = 0, r[6] = 0x0A, r[7] = 0x18;
            p_att(r, 8);
        } else
            p_att_err(0x10, s, 0x0A);
        return;
    case 0x08:                                 /* Read By Type 0x2803 */
        s = (uint16_t)(p[1] | p[2] << 8), e = (uint16_t)(p[3] | p[4] << 8);
        if (s <= PH_MIDI_D && e >= PH_MIDI_D) {
            r[0] = 0x09, r[1] = 21, r[2] = PH_MIDI_D, r[3] = 0, r[4] = 0x16, r[5] = PH_MIDI_V, r[6] = 0;
            memcpy(r + 7, MIDI_IO, 16);
            p_att(r, 23);
        } else
            p_att_err(0x08, s, 0x0A);
        return;
    case 0x04:                                 /* Find Information */
        s = (uint16_t)(p[1] | p[2] << 8);
        if (s <= PH_CCCD) {
            r[0] = 0x05, r[1] = 1, r[2] = PH_CCCD, r[3] = 0, r[4] = 0x02, r[5] = 0x29, r[6] = PH_DESC, r[7] = 0,
            r[8] = 0x01, r[9] = 0x29;
            p_att(r, 10);
        } else
            p_att_err(0x04, s, 0x0A);
        return;
    case 0x12:                                 /* Write Request: the CCCD */
        if (p[1] != PH_CCCD) {
            p_att_err(0x12, p[1], 0x03);
            return;
        }
        if ((P.need_auth && !P.enc_rx) || (P.mitm_need && (!P.enc_rx || !P.enc_auth || P.sc_only))) {
            p_att_err(0x12, PH_CCCD, 0x05);    /* Insufficient Authentication (Apple's peripherals, QA1831) */
            if (P.late_pending) {              /* (its late Pairing Failed after the refusal: our MITM request out) */
                static const uint8_t f[2] = {0x05, 0x08};
                P.late_pending = 0;
                p_l2(6, f, 2);
            }
            return;
        }
        P.cccd = p[3] & 1;
        r[0] = 0x13;
        p_att(r, 1);
        return;
    case 0x52:                                 /* Write Command: the FM-1's MIDI out */
        if (p[1] == PH_MIDI_V) {
            P.writes++;
            memcpy(P.wcmd, p + 3, n - 3 < 8 ? n - 3 : 8);
        }
        return;
    default:
        return;
    }
}

static void p_take(uint8_t *pdu, int len)
{
    uint8_t llid = pdu[0] & 3, n = pdu[1], *p = pdu + 2;
    (void)len;
    if (P.enc_rx && n) {
        if (!ble_ccm_decrypt(&P.crx, pdu[0], p, n)) {
            printf("peripheral: MIC failure\n");
            fails++;
            return;
        }
        n = (uint8_t)(n - 4);
    }
    if (P.enc_start && n && (llid != 3 || (p[0] != LL_START_ENC_RSP && p[0] != LL_TERMINATE_IND))) {
        p_mic_term();                          /* (anything else of ours during the start: it leaves) */
        return;
    }
    if (llid == 3) {
        p_ll(p, n);
        return;
    }
    if (llid == 2) {
        P.have = 0;
        P.need = (p[0] | p[1] << 8) + 4;
    }
    if (!P.need || P.have + n > (int)sizeof P.l2)
        return;
    memcpy(P.l2 + P.have, p, n);
    P.have += n;
    if (P.have == P.need) {
        uint16_t cid = (uint16_t)(P.l2[2] | P.l2[3] << 8);
        P.need = 0;
        if (cid == 4)
            p_att_rx(P.l2 + 4, P.have - 4);
        else if (cid == 6)
            p_smp(P.l2 + 4, P.have - 4);
    }
}

/* one connection event (the master transmits first) */
static void p_event(void)
{
    uint8_t pdu[2 + 255];
    int i, k;
    if (!hw.conn_on)
        return;
    for (k = 0; k < 32 && hw.conn_on; k++) {
        uint8_t n = ble_ll_hw_tx(pdu);
        if (!n)
            break;
        if (P.alive) {
            p_take(pdu, n);
            ble_ll_hw_tx_acked();
        }
    }
    if (P.enc_wait && !--P.enc_wait)
        p_enc_answer(P.enc_req);               /* (its host's LTK came) */
    if (P.smp_st == 10 && P.typed >= 0)
        p_sconfirm();                          /* (its user typed the passkey) */
    if (P.smp_st == 4 && P.enc_tx && P.enc_stk)
        p_keys();                              /* (phase 3: once encrypted with the STK, not an older key) */
    if (P.alive)
        for (i = 0; i < P.qn && hw.conn_on; i++)
            ble_ll_hw_rx(P.q[i], (uint8_t)(2 + P.q[i][1]));
    P.qn = 0;
    if (!hw.conn_on)
        return;
    now_us += 9u * 1250u;
    ble_ll_hw_event_end(P.evt++, (uint8_t)P.alive);
    if (P.dying)
        P.alive = P.dying = 0;                 /* (its TERMINATE_IND went: gone) */
}
static void p_events(int n)
{
    while (n-- > 0)
        p_event();
}
static int p_ctrl_seen(uint8_t op)
{
    int i;
    for (i = 0; i < P.ctrl_n && i < 96; i++)
        if (P.ctrl[i][0] == op)
            return i + 1;
    return 0;
}

/* connect to the peripheral (bonded: with its bond; sec: BLE_PEER_*) and let the engine make us the master */
static int connect_sec(int bonded, uint8_t sec)
{
    struct ble_peer pr;
    memset(&pr, 0, sizeof pr);
    memcpy(pr.addr, PADDR, 6);
    pr.addr_rand = 1;
    pr.sec = sec;
    if (bonded) {
        pr.bonded = 1;
        memcpy(pr.ltk, got_keys.ltk, 16);
        memcpy(pr.rand, got_keys.rand, 8);
        pr.ediv = got_keys.ediv;
    }
    p_reset();
    if (!ble_central_connect(&pr) || !hw.init_on)
        return 0;
    ble_ll_hw_master_start();
    hw.conn_on = 1;
    hw.init_on = 0;
    return ble_ll_central() && ble_central_state() == BLE_CS_SETUP;
}
static int connect(int bonded) { return connect_sec(bonded, 0); }

static void leave(void)
{
    if (ble_ll_central()) {
        ble_central_cancel();
        p_events(3);
    }
}

/* ---------------------------------------------------------------------------------------------- the scenarios */

static void test_initiator(void)
{
    struct ble_peer pr;
    const uint8_t *c = hw.cind;
    uint32_t aa, crc;
    uint16_t off;
    ble_init(OWN, 1);
    ble_enable(1);
    memset(&pr, 0, sizeof pr);
    memcpy(pr.addr, PADDR, 6);
    pr.addr_rand = 1;
    check("initiator: advertising first", hw.adv_on && !ble_ll_initiating());
    check("initiator: ble_central_connect: advertising stops, initiating (state 3) starts, CONNECTING",
          ble_central_connect(&pr) && hw.init_on && !hw.adv_on && ble_ll_initiating() &&
              ble_central_state() == BLE_CS_CONNECTING && hw.in.interval == 64 && hw.in.window == 60);
    aa = (uint32_t)c[14] | (uint32_t)c[15] << 8 | (uint32_t)c[16] << 16 | (uint32_t)c[17] << 24;
    crc = (uint32_t)c[18] | (uint32_t)c[19] << 8 | (uint32_t)c[20] << 16;
    off = (uint16_t)(c[22] | c[23] << 8);
    check("CONNECT_IND: type 5, TxAdd 1 (ours random), RxAdd 1 (its), length 34, InitA ours, AdvA its",
          c[0] == 0xC5 && c[1] == 34 && !memcmp(c + 2, OWN, 6) && !memcmp(c + 8, PADDR, 6));
    check("CONNECT_IND: a valid random access address (Core 2.1.2), a random CRCInit (not the vendor's 0x1983AE)",
          ble_aa_valid(aa) && crc != 0x1983AEu && crc <= 0xFFFFFFu && hw.in.conn.aa == aa && hw.in.conn.crc_init == crc);
    check("CONNECT_IND: WinSize 2, WinOffset in [4, 8], interval 9 (11.25 ms), latency 0, timeout 200 (2 s)",
          c[21] == 2 && off >= 4 && off <= 8 && c[24] == 9 && !c[25] && !c[26] && !c[27] && c[28] == 200 && !c[29]);
    check("CONNECT_IND: all 37 channels, hop 5..16, SCA 0 (251-500 ppm)",
          c[30] == 0xFF && c[31] == 0xFF && c[32] == 0xFF && c[33] == 0xFF && c[34] == 0x1F && (c[35] & 31) >= 5 &&
              (c[35] & 31) <= 16 && !(c[35] >> 5));
    ble_central_cancel();
    check("initiator: cancelled: state 3 stopped, advertising again, IDLE",
          !hw.init_on && hw.init_stops == 1 && hw.adv_on && !ble_ll_initiating() && ble_central_state() == BLE_CS_IDLE);
    ble_central_connect(&pr);
    check("initiator: a second CONNECT_IND draws another AA and CRCInit",
          hw.in.conn.aa != aa && hw.in.conn.crc_init != crc && ble_aa_valid(hw.in.conn.aa));
    ble_central_cancel();
    check("blell: two connects, two cancels counted", ble_dgc.connects == 2 && ble_dgc.cancels == 2);
}

static void test_master_and_gatt(void)
{
    uint8_t d[24];
    int i, v, f, l;
    uint16_t ev;
    check("master: the engine made us the master: connected as central, SETUP, the GATT client starts",
          connect(0) && ble_connected() && !hw.adv_on);
    p_events(40);
    v = p_ctrl_seen(LL_VERSION_IND), f = p_ctrl_seen(LL_FEATURE_REQ), l = p_ctrl_seen(LL_LENGTH_REQ);
    check("master LL: we start the version exchange, then the feature exchange, then the data length",
          v && f && l && v < f && f < l && bll.feat_known && bll.peer_feat == 0x21 && bll.len_done);
    check("GATT client: MTU (185), Find By Type Value, Read By Type, Find Information, the CCCD write",
          P.att_ops[0] == 0x02 && P.att_ops[1] == 0x06 && P.att_ops[2] == 0x08 && batt.mtu == 185 && P.cccd == 1 &&
              ble_dgc.gc_svc_s == PH_MIDI && ble_dgc.gc_svc_e == PH_DESC && ble_dgc.gc_val == PH_MIDI_V &&
              ble_dgc.gc_cccd == PH_CCCD);
    check("ready: BLE-MIDI subscribed, ble_midi_ready(), the firmware told", ble_central_state() == BLE_CS_READY &&
          ble_midi_ready() && ble_gattc_midi() == PH_MIDI_V && state_calls >= 2);
    {
        static const uint8_t ntf[] = {0x1B, PH_MIDI_V, 0, 0x80, 0x80, 0x90, 0x3C, 0x64};
        in_n = 0;
        p_att(ntf, sizeof ntf);
        p_event();
        check("MIDI in: the peripheral's notification decoded (note on 3C 64)",
              in_n == 1 && in_pkt[0] == (0x09u | 0x90u << 8 | 0x3Cu << 16 | 0x64u << 24));
    }
    out_q[out_w++ % 64] = 0x09u | 0x90u << 8 | 0x40u << 16 | 0x50u << 24;
    p_events(3);
    check("MIDI out: a Write Command (Write Without Response) to its MIDI I/O value: 90 40 50",
          P.writes == 1 && P.wcmd[2] == 0x90 && P.wcmd[3] == 0x40 && P.wcmd[4] == 0x50 && ble_dgc.gc_wcmd_tx == 1);
    memset(d, 0, 9);
    d[0] = LL_PERIPHERAL_FEATURE_REQ, d[1] = 0x21;
    p_ctrl(d, 9);
    d[0] = LL_PHY_REQ, d[1] = 3, d[2] = 3;
    p_ctrl(d, 3);
    i = P.ctrl_n;
    p_events(3);
    check("the peripheral's feature exchange -> FEATURE_RSP; its PHY_REQ -> PHY_UPDATE_IND with no change",
          p_ctrl_seen(LL_FEATURE_RSP) && p_ctrl_seen(LL_PHY_UPDATE_IND) &&
              !P.ctrl[p_ctrl_seen(LL_PHY_UPDATE_IND) - 1][1] && !P.ctrl[p_ctrl_seen(LL_PHY_UPDATE_IND) - 1][2] &&
              P.ctrl_n >= i + 2);
    memset(d, 0, 24);
    d[0] = LL_CONNECTION_PARAM_REQ, d[1] = 12, d[3] = 12, d[7] = 0x2C, d[8] = 1;   /* 15 ms, 3 s */
    p_ctrl(d, 24);
    ev = P.evt;
    p_events(2);
    {
        int k = p_ctrl_seen(LL_CONNECTION_UPDATE_IND);
        const uint8_t *u = k ? P.ctrl[k - 1] : d;
        uint16_t inst = (uint16_t)(u[10] | u[11] << 8);
        check("its CONNECTION_PARAM_REQ -> our CONNECTION_UPDATE_IND: interval 12, timeout 300, WinSize 1, WinOffset "
              "6, the instant 8..11 events ahead, the driver told",
              k && u[4] == 12 && u[8] == 0x2C && u[9] == 1 && u[1] == 1 && u[2] == 6 && inst >= ev + 8 &&
                  inst <= ev + 11 + 2 && hw.upd_n == 1 && hw.upd.interval == 12 && hw.upd.instant == inst);
        p_events(16);
        check("after the instant the link runs at 12; our procedure is over", bll.interval == 12 && bll.timeout == 300 &&
              bll.lproc == P_NONE);
    }
    {
        static const uint8_t req[] = {0x12, 0x09, 8, 0, 6, 0, 9, 0, 0, 0, 200, 0};
        int n0 = P.ctrl_n;
        P.att_n = 0;
        p_l2(5, req, sizeof req);
        p_events(3);
        check("its L2CAP Connection Parameter Update Request {6, 9, 0, 200}: accepted, then an LL update to 9",
              hw.upd_n == 2 && hw.upd.interval == 9 && hw.upd.timeout == 200 && P.ctrl_n > n0 &&
                  ble_dgc.m_l2_upd_rx == 1);
        p_events(16);
    }
    {
        static const uint8_t map[5] = {0xFF, 0x0F, 0xF0, 0xFF, 0x1F};
        ev = P.evt;
        check("our channel map update (as master) at counter + 7..10, the driver told",
              ble_ll_chmap_update(map) && hw.chm_n == 1 && hw.chm_instant >= ev + 6 && hw.chm_instant <= ev + 10);
        p_events(14);
        check("... the LL keeps the new map after its instant", !memcmp(bll.chm, map, 5) && bll.lproc == P_NONE);
    }
    leave();
    check("we leave: TERMINATE_IND to the peripheral, the link gone, advertising again",
          p_ctrl_seen(LL_TERMINATE_IND) && !ble_connected() && hw.adv_on && ble_central_state() == BLE_CS_IDLE);
}

#if BLE_CEN_PAIR
static void test_pairing(void)
{
    int n0 = got_keys_n;
    connect(0);
    P.need_auth = 1;
    p_events(80);
    check("pairing: the CCCD write answered Insufficient Authentication -> legacy Just Works as initiator",
          ble_dgc.gc_auth_errs >= 1 && ble_dgc.si_pair_req >= 1 && ble_dgc.si_pair_rsp >= 1 && ble_dgc.si_confirm_ok >= 1);
    check("pairing: the STK encryption (EDIV 0, Rand 0) started by us, the link encrypted both ways",
          ble_ll_encrypted() && P.enc_tx && P.enc_rx && ble_dgc.m_enc_on >= 1);
    check("pairing: its keys taken (LTK, EDIV / Rand, IRK, identity), ours sent after (4 PDUs), to the firmware",
          got_keys_n == n0 + 1 && got_keys.has == (BLE_KEYS_LTK | BLE_KEYS_ID) && got_keys.ltk[0] == 0x40 &&
              got_keys.ediv == 0xBEEF && got_keys.rand[7] == 0x77 && got_keys.irk[0] == 0xA7 &&
              !memcmp(got_keys.id, PID, 6) && got_keys.id_rand == 1 && P.keys_from_master == 4 && P.paired);
    {
        uint8_t irk[16];
        smp_own_irk(irk);
        check("pairing: our IRK is our own fixed one (from our address, not the random generator): a Mac that "
              "bonded once refuses a new IRK for the same identity address", !memcmp(P.our_irk, irk, 16));
    }
    {
        /* the iPad, 2026-11-03: paired with Melodee connecting out, then Mode VISIBLE: it connects to us and asks for
         * the key we gave it. Thrown away, that was Key Missing and iOS dropped the link */
        uint8_t k[16], first_ltk[16], first[10], other[16] = {0};
        memcpy(first, P.our_edrand, 10), memcpy(first_ltk, P.our_ltk, 16);
        check("pairing: the key we handed out as initiator is known when that peer connects to us (its EDIV, Rand)",
              ble_host_ltk(first + 2, (uint16_t)(first[0] | first[1] << 8), k) && !memcmp(k, first_ltk, 16) &&
                  memcmp(first_ltk, other, 16) && !ble_host_key_refused());
        uint8_t second[10], second_ltk[16];
        ble_host_new_key(16, second, second_ltk);
        check("pairing: a second peer's key is good, and the first peer's still is",
              memcmp(second_ltk, first_ltk, 16) && ble_host_ltk(second + 2, (uint16_t)(second[0] | second[1] << 8), k) &&
                  !memcmp(k, second_ltk, 16) && ble_host_ltk(first + 2, (uint16_t)(first[0] | first[1] << 8), k) &&
                  !memcmp(k, first_ltk, 16));
        ble_host_new_key(7, second, second_ltk);
        check("pairing: a short key comes back cut as it was handed out",
              !second_ltk[7] && !second_ltk[15] && ble_host_ltk(second + 2, (uint16_t)(second[0] | second[1] << 8), k) &&
                  !memcmp(k, second_ltk, 16));
        first[5]++;                             /* (one nonce octet of Rand: not a key of ours any more) */
        check("pairing: a key we did not hand out is refused, and that is told once",
              !ble_host_ltk(first + 2, (uint16_t)(first[0] | first[1] << 8), k) && ble_host_key_refused() &&
                  !ble_host_key_refused());
    }
    check("pairing: the CCCD written again once paired: ready", ble_central_state() == BLE_CS_READY && P.cccd == 1 &&
          ble_dgc.gc_retries >= 1);
    leave();
    connect(1);
    P.need_auth = 1;
    p_events(60);
    check("a bonded peer: encrypted at once with the bond's LTK (its EDIV / Rand), no pairing, ready",
          P.ltk_used == 1 && P.smp_st == 0 && ble_central_state() == BLE_CS_READY && ble_dgc.si_ltk_enc >= 1);
    leave();
    P.lost_bond = 1;
    connect(1);
    P.lost_bond = 1;
    P.need_auth = 1;
    p_events(90);
    check("a bond the peripheral lost: LL_ENC_REQ refused (Key Missing) -> paired afresh, ready",
          ble_dgc.m_enc_rej >= 1 && got_keys_n == n0 + 2 && ble_central_state() == BLE_CS_READY);
    leave();
    connect(0);
    {
        static const uint8_t sec[2] = {0x0B, 0x01};
        p_l2(6, sec, 2);
    }
    p_events(80);
    check("its SMP Security Request: we pair (no bond), then ready", ble_dgc.si_sec_req_rx == 1 && P.paired &&
          ble_central_state() == BLE_CS_READY);
    leave();
    connect(0);
    P.need_auth = 1;
    P.wrong_conf = 1;
    p_events(40);
    check("a Sconfirm that does not match its Srand: our Pairing Failed (Confirm Value Failed), the link left",
          P.smp_st == -4 && !ble_connected() && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0x04);
    connect(0);
    P.need_auth = 1;
    P.fail_pair = 1;
    p_events(40);
    check("its Pairing Failed (Unspecified): the link left, PAIRING FAILED",
          !ble_connected() && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0x08);
}
#endif

#if BLE_CEN_PAIR
/* p_events, watching whether a passkey was ever shown */
static int shown;
static void p_events_watch(int n)
{
    while (n-- > 0) {
        p_event();
        shown |= ble_central_passkey() != BLE_NO_PASSKEY;
    }
}

/* an iPhone app's MIDI characteristic (blell-dev3, docs/BLE-STACK.md §9.2): Insufficient Authentication even after a Just
 * Works pairing; a passkey pairing (we display, its user types) is what it takes */
static void iphone(void)
{
    P.need_auth = P.mitm_need = P.iphone = P.own_len = 1;
    P.io = 0x04;                               /* KeyboardDisplay, as a Mac and an iPhone answer */
}

/* an unauthenticated bond with it (a Just Works pairing, as the builds before 2026-10-18 made with a Mac or a phone),
 * for a reconnection that finds it needs a passkey after all */
static void unauth_bond(void)
{
    connect(0);
    P.need_auth = 1;
    p_events(80);
    leave();
}

static void test_passkey(void)
{
    int n0 = got_keys_n, i0 = hw.init_starts;
    uint32_t pr0 = ble_dgc.si_pair_req, pk;
    connect(0);
    iphone();
    shown = 0;
    p_events_watch(30);
    pk = ble_central_passkey();
    check("Mac / iPhone-like (it can type): Insufficient Authentication -> ONE pairing, the passkey at once: "
          "DisplayOnly, bonding + MITM, the passkey shown, no Just Works before it (macOS drops an alert that comes "
          "right after another)", ble_connected() && P.pair_reqs == 1 && P.preq_first[1] == 0x00 &&
              P.preq_first[3] == 0x05 && P.pk && pk < 1000000u && ble_dgc.cen_need_mitm == 0 &&
              ble_dgc.si_repair == 0 && ble_dgc.si_passkey == 1 && ble_central_pairing() && P.smp_st == 10 &&
              ble_dgc.si_pair_req == pr0 + 1 && hw.init_starts == i0 + 1 && got_keys_n == n0 &&
              ble_central_state() == BLE_CS_SETUP);
    p_events(400);
    check("... 4.5 s while its user types: still waiting, the same passkey, the link up (the SMP timer runs from our "
          "Mconfirm)", ble_connected() && ble_central_passkey() == pk && ble_central_state() == BLE_CS_SETUP);
    P.typed = (long)pk;
    p_events(80);
    check("... typed: Sconfirm / Srand with TK = the passkey, the STK, keys both ways: authenticated, ready, on the "
          "one link, no encryption pause (it was not encrypted before)",
          ble_central_state() == BLE_CS_READY && P.cccd == 1 && P.enc_auth && ble_dgc.si_auth_done == 1 &&
              P.pause_n == 0 && hw.init_starts == i0 + 1 && got_keys_n == n0 + 1 &&
              (got_keys.has & (BLE_KEYS_AUTH | BLE_KEYS_LTK)) == (BLE_KEYS_AUTH | BLE_KEYS_LTK) && ble_central_prompted());
    check("... the passkey no longer shown; no late Pairing Failed",
          ble_central_passkey() == BLE_NO_PASSKEY && ble_dgc.si_fail_late == 0);
    leave();
    pr0 = ble_dgc.si_pair_req;
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    shown = 0;
    p_events_watch(60);
    check("reconnect with the authenticated bond: encrypted with its LTK, accepted, ready, no pairing, nothing shown",
          P.ltk_used == 1 && P.enc_auth && ble_central_state() == BLE_CS_READY && ble_dgc.si_pair_req == pr0 && !shown);
    check("... the data length exchanged before our LL_ENC_REQ (the iPhone sends its own LL_LENGTH_REQ otherwise, "
          "and our answer during the encryption start made it leave with MIC Failure, blell-dev6)",
          P.mic_terms == 0 && p_ctrl_seen(LL_LENGTH_REQ) && p_ctrl_seen(LL_LENGTH_REQ) < p_ctrl_seen(LL_ENC_REQ) &&
              ble_dgc.m_len_done >= 1);
    leave();
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    P.len_always = 1;
    p_events(60);
    check("its LL_LENGTH_REQ crossing our LL_ENC_REQ: held while the encryption starts (5.1.3.1: no other PDU of "
          "ours), answered once encrypted: no MIC Failure, ready", P.mic_terms == 0 && P.ltk_used == 1 && P.enc_auth &&
              ble_dgc.m_held >= 1 && P.len_done && ble_central_state() == BLE_CS_READY && ble_dgc.si_pair_req == pr0);
    leave();
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    P.mic_on_enc = 1;
    p_events(30);
    check("it leaves with MIC Failure (0x3D) on our LL_ENC_REQ with the bond: KEY (3D), not a link lost, no pairing",
          !ble_connected() && ble_central_fail() == BLE_CF_KEY && ble_central_code() == 0x3D &&
              ble_dgc.si_pair_req == pr0 && P.mic_terms == 1);
    leave();
    P.lost_bond = 1;
    connect_sec(1, BLE_PEER_MITM | BLE_PEER_AUTH);
    iphone();
    P.lost_bond = 1;
    p_events(30);
    check("its bond lost (the phone forgot it): the pairing goes straight to the passkey (MITM known), no Just Works",
          ble_dgc.m_enc_rej >= 1 && P.preq[3] == 0x05 && ble_central_passkey() != BLE_NO_PASSKEY &&
              ble_dgc.si_pair_req == pr0 + 1);
    P.typed = (long)((ble_central_passkey() + 1u) % 1000000u);
    p_events(40);
    check("a passkey typed wrong: its Pairing Failed (Confirm Value Failed) -> PAIRING (4), the link left, no retry",
          !ble_connected() && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0x04 &&
              ble_dgc.si_pair_req == pr0 + 1 && ble_central_passkey() == BLE_NO_PASSKEY);
    P.lost_bond = 0;
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    P.io = 0x03;
    p_events(20);
    check("a passkey needed but it cannot type (NoInputNoOutput): our Pairing Failed (Authentication Requirements) "
          "-> AUTH (3), nothing shown", !ble_connected() && ble_central_fail() == BLE_CF_AUTH &&
              ble_central_code() == 0x03 && P.smp_st == -3 && ble_central_passkey() == BLE_NO_PASSKEY);
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    P.sc_only = 1;
    p_events(10);
    P.typed = (long)ble_central_passkey();
    p_events(200); /* five 200-ms security-settling retries precede failure */
    check("refused even after the passkey (a peer that wants Secure Connections): AUTH (5), no other pairing, no loop",
          !ble_connected() && ble_central_fail() == BLE_CF_AUTH && ble_central_code() == 0x05 &&
              ble_dgc.cen_need_mitm == 0 && ble_dgc.si_auth_done == 2);
    pr0 = ble_dgc.si_pair_req;
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    p_events(2400);
    check("... 27 s and nobody typed: still waiting", ble_connected() && ble_central_passkey() != BLE_NO_PASSKEY);
    p_events(400);
    check("the 30 s SMP timeout (nobody typed): PAIRING (FF), the link left, the passkey gone",
          !ble_connected() && ble_central_fail() == BLE_CF_PAIRING && ble_central_code() == 0xFF &&
              ble_central_passkey() == BLE_NO_PASSKEY && ble_dgc.si_pair_req == pr0 + 1);
    connect(0);
    P.io = 0x04;
    {
        static const uint8_t sec[2] = {0x0B, 0x05};   /* a Security Request: bonding + MITM */
        p_l2(6, sec, 2);
    }
    p_events(10);
    check("its Security Request asking MITM: our pairing asks MITM too, with the passkey",
          P.preq[3] == 0x05 && P.preq[1] == 0x00 && ble_central_passkey() != BLE_NO_PASSKEY);
    leave();    leave();
    unauth_bond();
    connect(1);
    iphone();
    shown = 0;
    p_events_watch(130);
    pk = ble_central_passkey();
    check("an older Just Works bond: encrypted with it, refused (0x05) -> paired again ON THE SAME LINK with the "
          "passkey: DisplayOnly, bonding + MITM, no disconnection", ble_connected() && P.ltk_used == 1 &&
              P.pair_reqs == 1 && P.preq[1] == 0x00 && P.preq[3] == 0x05 && P.pk && pk < 1000000u &&
              ble_dgc.cen_need_mitm == 1 && ble_dgc.si_repair == 1 && ble_central_pairing() && P.smp_st == 10);
    P.typed = (long)pk;
    p_events(80);
    check("... typed: the STK through the LL's encryption pause (PAUSE_ENC_REQ / RSP, a new ENC_REQ), authenticated, "
          "ready on the one link", ble_central_state() == BLE_CS_READY && P.enc_auth && P.pause_n == 1 &&
              ble_dgc.m_pause_tx >= 1 && ble_dgc.m_pause_rsp_rx >= 1 &&
              (got_keys.has & BLE_KEYS_AUTH) && ble_central_mitm());
    leave();
    unauth_bond();
    connect(1);
    iphone();
    P.no_repair = 1;
    shown = 0;
    p_events(30);
    p_events_watch(100);
    check("a peer that refuses the pairing on the encrypted link (before any passkey): NEED_MITM, the link left (a new "
          "link with the passkey from the start is the firmware's), nothing shown",
          !ble_connected() && ble_central_fail() == BLE_CF_NEED_MITM && ble_dgc.si_repair_fallback == 1 && !shown &&
              ble_dgc.si_repair == 2);
    connect_sec(0, BLE_PEER_MITM);
    iphone();
    shown = 0;
    p_events_watch(10);
    pk = ble_central_passkey();
    check("again with BLE_PEER_MITM: a Pairing Request DisplayOnly, bonding + MITM, no SC; the passkey shown (0..999999)",
          P.preq_first[1] == 0x00 && P.preq_first[3] == 0x05 && P.pk && pk < 1000000u && ble_central_pairing() &&
              P.smp_st == 10 && P.pair_reqs == 1);
    P.typed = (long)pk;
    p_events(80);
    check("... typed: authenticated, ready; no pause (the link was not encrypted before)",
          ble_central_state() == BLE_CS_READY && P.enc_auth && P.pause_n == 0 && ble_dgc.si_auth_done == 4);
    leave();
    unauth_bond();
    connect(1);
    iphone();
    P.mute_repair = 1;
    p_events(100);
    check("a peer that never answers the pairing on the encrypted link: still waiting at first",
          ble_connected() && ble_central_passkey() == BLE_NO_PASSKEY);
    p_events(450);
    check("... 5 s: NEED_MITM (FF), the link left: a new link with the passkey instead",
          !ble_connected() && ble_central_fail() == BLE_CF_NEED_MITM && ble_central_code() == 0xFF &&
              ble_dgc.si_repair_fallback == 2);
    unauth_bond();
    connect(1);
    iphone();
    P.no_pause = 1;
    p_events(100);
    P.typed = (long)ble_central_passkey();
    p_events(40);
    check("the encryption pause unknown to it (after the passkey was typed): PAIRING, the link left, no other pairing "
          "(one prompt per user action)", !ble_connected() && ble_central_fail() == BLE_CF_PAIRING &&
              P.pause_n == 1 && P.pair_reqs == 1 && ble_central_passkey() == BLE_NO_PASSKEY);
    P.no_pause = 0;
}
#endif

#if !BLE_CEN_PAIR
/* BLE_CENTRAL 3 (no pairing): whatever asks for security leaves the link (NEED_PAIR); a peer that asks for nothing works */
static void test_no_pairing(void)
{
    connect(0);
    P.need_auth = 1;
    p_events(40);
    check("no pairing: the CCCD write answered Insufficient Authentication -> no Pairing Request, no LL_ENC_REQ: the "
          "link left, NEED_PAIR (5), advertising again", !ble_connected() && ble_central_fail() == BLE_CF_NEED_PAIR &&
              ble_central_code() == 0x05 && P.pair_reqs == 0 && !p_ctrl_seen(LL_ENC_REQ) && !P.cccd && hw.adv_on);
    connect(0);
    {
        static const uint8_t sec[2] = {0x0B, 0x01};
        p_l2(6, sec, 2);
    }
    p_events(40);
    check("its Security Request: our Pairing Failed (Pairing Not Supported), the link left, NEED_PAIR (0)",
          !ble_connected() && ble_central_fail() == BLE_CF_NEED_PAIR && ble_central_code() == 0 &&
              P.smp_st == -5 && P.pair_reqs == 0 && !p_ctrl_seen(LL_ENC_REQ));
    connect(0);
    p_events(40);
    in_n = 0;
    {
        static const uint8_t ntf[] = {0x1B, PH_MIDI_V, 0, 0x80, 0x80, 0x90, 0x3C, 0x64};
        p_att(ntf, sizeof ntf);
    }
    out_q[out_w++ % 64] = 0x09u | 0x90u << 8 | 0x41u << 16 | 0x50u << 24;
    p_events(3);
    check("again, a peer that asks for nothing (a controller, another FM-1): ready, MIDI both ways, never encrypted",
          ble_central_state() == BLE_CS_READY && P.cccd == 1 && in_n == 1 && P.wcmd[3] == 0x41 && !ble_ll_encrypted() &&
              P.pair_reqs == 0);
    leave();
}
#endif

static void test_gatt_variants(void)
{
    connect(0);
    P.no_fbtv = 1;
    p_events(50);
    check("a server without Find By Type Value: the primary services walked (Read By Group Type), ready",
          ble_central_state() == BLE_CS_READY && ble_dgc.gc_svc_s == PH_MIDI);
    leave();
    connect(0);
    P.no_midi = 1;
    p_events(30);
    check("no BLE-MIDI service: the link left, NO MIDI SERVICE", !ble_connected() &&
          ble_central_fail() == BLE_CF_NO_MIDI && ble_dgc.gc_no_midi >= 1 && hw.adv_on);
}

static void test_endings(void)
{
    connect(0);
    P.alive = 0;
    p_events(8);
    check("the peripheral never answers: no packet in six intervals -> lost (0x3E), advertising again",
          !ble_connected() && ble_central_fail() == BLE_CF_LOST && ble_central_code() == BLE_ERR_CONN_FAILED &&
              hw.adv_on && ble_dgc.m_estab_fails >= 1);
    connect(0);
    p_events(30);
    {
        static const uint8_t t[2] = {LL_TERMINATE_IND, 0x13};
        p_ctrl(t, 2);
    }
    p_events(2);
    check("its TERMINATE_IND after ready: lost (0x13)", !ble_connected() && ble_central_fail() == BLE_CF_LOST &&
          ble_central_code() == 0x13);
}

#if BLE_CEN_PAIR
/* ah(): the Core spec's sample (Vol 3 Part H D.7): IRK ec0234a357c8ad05341010a60a397d9b, prand 708194 -> 0dfbaa */
static void test_rpa(void)
{
    static const uint8_t irk_be[16] = {0xec, 0x02, 0x34, 0xa3, 0x57, 0xc8, 0xad, 0x05,
                                       0x34, 0x10, 0x10, 0xa6, 0x0a, 0x39, 0x7d, 0x9b};
    uint8_t irk[16], a[6] = {0xaa, 0xfb, 0x0d, 0x94, 0x81, 0x70};   /* hash, then prand, least significant first */
    int i;
    for (i = 0; i < 16; i++)
        irk[i] = irk_be[15 - i];
    check("RPA: the Core sample resolves (ah(IRK, 708194) = 0dfbaa)", ble_rpa_resolve(irk, a));
    a[0] ^= 1;
    check("RPA: a wrong hash does not", !ble_rpa_resolve(irk, a));
    a[0] ^= 1, a[5] = 0xF0;
    check("RPA: a static address (top bits 11) is not one", !ble_rpa_resolve(irk, a));
}
#endif

/* the console's blell: the central block printed (ble_diag.c bd_central) */
static char out[65536];
static size_t out_n;
static void put(const char *t)
{
    size_t n = strlen(t);
    if (out_n + n < sizeof out)
        memcpy(out + out_n, t, n + 1), out_n += n;
}
static void test_blell(void)
{
    ble_diag_print(put, 0);
    check("blell: the central counters printed (initiator, master, GATT client, SMP initiator, reconnection)",
          strstr(out, "cen_connects ") && strstr(out, "master_starts ") && strstr(out, "m_events_rx ") &&
              strstr(out, "gc_state ") && strstr(out, "gc_subscribed ") && strstr(out, "si_done ") &&
              strstr(out, "rc_tries ") && strstr(out, "c4_rx_to_evt_us "));
    ble_diag_clear();
    check("blell clear: the central block zeroed, its magic kept", !ble_dgc.connects && !ble_dgc.gc_subscribed &&
          ble_dgc.magic == BLE_DIAG_CENT_MAGIC && ble_dgc.m_first_evt == 0xFFFFu);
}

int main(void)
{
    test_initiator();
    test_master_and_gatt();
#if BLE_CEN_PAIR
    test_pairing();
    test_passkey();
#else
    test_no_pairing();
#endif
    test_gatt_variants();
    test_endings();
#if BLE_CEN_PAIR
    test_rpa();
#endif
    test_blell();
    printf("BLE central (level %d): %s\n", BLE_CENTRAL, fails ? "FAILED" : "all passed");
    return fails != 0;
}
