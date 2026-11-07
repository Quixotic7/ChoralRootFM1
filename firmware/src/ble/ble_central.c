/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* The host's side of the central role (BLE_CENTRAL; docs/BLE-STACK.md §9.2, docs/BLE-DEVICES-DESIGN.md §4): one connection out to a
 * BLE-MIDI peripheral, from the initiator to "ready" (MIDI both ways):
 *   ble_central_connect   initiate (ble_ll_connect) to the peer's AdvA; connected as master -> the GATT client
 *                         (ble_gattc.c) starts, and with the peer's bond the link is encrypted at once (Apple's
 *                         peripherals want it: QA1831), so the BLE-MIDI characteristic is found and subscribed;
 *   security              the peripheral's SMP Security Request, an Insufficient Authentication / Encryption answer,
 *                         or a bond the peripheral lost (LL_REJECT with PIN or Key Missing) -> encrypt with the bond,
 *                         else pair as initiator (ble_smp_init.c); the GATT request that asked goes again once the
 *                         link is encrypted (or the pairing done and our keys sent). Refused again after a Just Works
 *                         pairing (an iPhone app's MIDI characteristic): paired again with MITM on the same link
 *                         (the passkey; the LL pauses the encryption and restarts it with the new key); a peer that
 *                         refuses that pairing before any passkey is shown -> BLE_CF_NEED_MITM (the firmware makes a
 *                         new link with BLE_PEER_MITM);
 *   its requests          an L2CAP Connection Parameter Update Request is answered and carried out as an
 *                         LL_CONNECTION_UPDATE_IND (ble_ll_conn_update);
 *   the end               a failure (no BLE-MIDI service, pairing failed, authentication refused, a GATT error or
 *                         timeout) leaves the link; ble_central_fail / _code say why until the next attempt.
 * RPA resolution (ah, Core Vol 3 Part H 2.2.2) for finding LAST behind a private address: ble_rpa_resolve.
 * The levels (ble_cfg.h): BLE_CENTRAL 1 all of the above; 3 (no BLE_CEN_PAIR) no security at all: whatever asks for it
 * (the Security Request, Insufficient Authentication / Encryption) leaves the link with BLE_CF_NEED_PAIR
 * (ble_central_no_pairing). */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_aes.h"
#include "ble_util.h"
#include "ble_diag.h"

static struct {
    uint8_t st, fail, code;                    /* BLE_CS_*; BLE_CF_* and its code (the last attempt) */
#if BLE_CEN_PAIR
    uint8_t enc_tried, pairing, encrypted;     /* the bond's LTK tried; a pairing of ours runs; the link encrypted */
    uint8_t mitm, auth;                        /* pair with a passkey (the peer needs it); the link's key is
                                                * authenticated */
#endif
    uint8_t leave;                             /* bcen_fail: leave once the TX queue is empty (or 1 s) */
#if BLE_CEN_PAIR
    uint8_t repair;                            /* 1: pairing again with MITM on this encrypted link, 2: done */
    uint8_t prompted;                          /* a pairing of ours started on the link (a prompt on a phone) */
    uint8_t gatt_after_keys;                   /* the GATT request goes again once our keys have gone */
    uint8_t late_due;                          /* a Just Works pairing ended on the link: iOS may still send its
                                                * Pairing Failed for it (blell-dev3 / dev4: 3 events after our keys) */
#endif
    uint8_t settle_n, settle_wait;
    uint32_t settle_at;
    uint32_t leave_t;
    struct ble_peer peer;
} bcen;

BLE_API uint8_t ble_central_state(void) { return bcen.st; }
BLE_API uint8_t ble_central_fail(void) { return bcen.fail; }
BLE_API uint8_t ble_central_code(void) { return bcen.code; }

BLE_API int ble_central_connect(const struct ble_peer *p)
{
    if (!ble_ll_connect(p->addr, p->addr_rand))
        return 0;
#if BLE_CEN_PAIR
    bcen.prompted = 0;
#endif
    bcen.peer = *p;
    bcen.st = BLE_CS_CONNECTING;
    bcen.fail = BLE_CF_NONE;
    bcen.code = 0;
    return 1;
}

BLE_API void ble_central_cancel(void)
{
    if (ble_ll_initiating()) {
        ble_ll_connect_cancel();
        bcen.st = BLE_CS_IDLE;
    } else if (ble_ll_central())
        ble_ll_disconnect(BLE_ERR_REMOTE_USER);   /* (ble_central_disconnected when it is gone) */
}

/* leave the link for this reason (it is kept until the next attempt), once what is queued has gone (a Pairing Failed
 * of ours: ble_ll_disconnect drops what was not sent), at the next event */
static void bcen_fail(uint8_t why, uint8_t code)
{
    if (bcen.fail == BLE_CF_NONE) {
        bcen.fail = why;
        bcen.code = code;
    }
    if (!bcen.leave) {
        bcen.leave = 1;
        bcen.leave_t = ble_hw_time_us();
    }
}

BLE_API void ble_central_connected(void)
{
    if (!ble_ll_central())
        return;
    bcen.st = BLE_CS_SETUP;
#if BLE_CEN_PAIR
    bcen.enc_tried = bcen.pairing = bcen.encrypted = bcen.leave = bcen.auth = 0;
    bcen.repair = bcen.prompted = bcen.gatt_after_keys = bcen.late_due = 0;
    bcen.mitm = (uint8_t)(bcen.peer.sec & BLE_PEER_MITM ? 1u : 0u);
#else
    bcen.leave = 0;
#endif
    bcen.settle_n = bcen.settle_wait = 0;
    ble_gattc_start();
#if BLE_CEN_PAIR
    if (bcen.peer.bonded) {                    /* a bonded peer: encrypted before it has to ask */
        bcen.enc_tried = 1;
        ble_ll_start_enc(bcen.peer.ltk, bcen.peer.rand, bcen.peer.ediv);
    }
#endif
}

BLE_API void ble_central_disconnected(uint8_t reason)
{
    if (bcen.st == BLE_CS_SETUP || bcen.st == BLE_CS_READY) {
        if (bcen.fail == BLE_CF_NONE) {        /* gone without a reason of ours: lost (it left, or out of range); */
            bcen.fail = BLE_CF_LOST;           /* it left with MIC Failure / Key Missing while our encryption with */
            bcen.code = reason;                /* the bond started: the bond is no good to it (KEY) */
#if BLE_CEN_PAIR
            if (bcen.peer.bonded && bcen.enc_tried && !bcen.encrypted && !bcen.pairing &&
                (reason == BLE_ERR_MIC_FAILURE || reason == BLE_ERR_PIN_KEY_MISSING))
                bcen.fail = BLE_CF_KEY;
#endif
        }
        bcen.st = BLE_CS_IDLE;
    }
    ble_gattc_reset();
}

#if BLE_CEN_PAIR
static void bcen_pair(void)
{
    bcen.pairing = 1;
    bcen.prompted = 1;
    ble_smp_pair(bcen.mitm);
}
#endif

/* security is needed (the peripheral asked, or refused a GATT request with this ATT error; 0: a Security Request):
 * the bond's key first, else a pairing (Just Works unless the peer is known to need a passkey). Encrypted already and
 * refused with Insufficient Authentication by a key without MITM protection: the peer needs a passkey. It is asked for
 * on this link (Core Vol 3 Part H 2.4: a pairing on an encrypted link; the STK replaces the key through the LL's
 * encryption pause, Vol 6 Part B 5.1.3.2), once: no disconnection, no new connection to establish, the phone's user
 * sees the passkey prompt as with a new link. 1: something started (wait for it), 0: nothing left to try */
#if BLE_CEN_PAIR
static int bcen_secure(uint8_t code)
{
    if (bcen.pairing)
        return 1;
    if (bcen.encrypted) {
        /* Saadet: iOS may reject ATT until its bond/key distribution settles.
         * Retry five times at 200 ms, without another pairing prompt. */
        if (code && bcen.settle_n < 5u) {
            bcen.settle_n++; bcen.settle_wait = 1; bcen.settle_at = ble_hw_time_us();
            return 1;
        }
        if (code == 0x05u && !bcen.auth && !bcen.mitm && !bcen.repair) {
            BLE_DG(ble_dgc.cen_need_mitm++);
            BLE_DG(ble_dgc.si_repair++);
            bcen.mitm = 1;
            bcen.repair = 1;
            bcen_pair();
            return 1;
        }
        return 0;
    }
    if (bcen.peer.bonded) {
        if (!bcen.enc_tried) {
            bcen.enc_tried = 1;
            ble_ll_start_enc(bcen.peer.ltk, bcen.peer.rand, bcen.peer.ediv);
        }
        return 1;                              /* (on its way; refused: ble_host_enc_failed pairs) */
    }
    bcen_pair();
    return 1;
}

BLE_API int ble_central_pairing(void) { return bcen.pairing; }
BLE_API int ble_central_prompted(void) { return bcen.prompted; }
BLE_API int ble_central_mitm(void) { return bcen.mitm; }
#else
/* BLE_CENTRAL 3: the peripheral wants security (0: its Security Request, answered Pairing Not Supported by ble_smp.c;
 * else the ATT error it refused a GATT request with): this level has none, the link is left (NEEDS PAIRING, held by
 * the firmware until the user acts) */
BLE_API void ble_central_no_pairing(uint8_t code)
{
    BLE_DG(ble_dgc.si_sec_req_rx += code == 0u);
    bcen_fail(BLE_CF_NEED_PAIR, code);
}

static int bcen_secure(uint8_t code)
{
    ble_central_no_pairing(code);
    return 0;
}
#endif

/* (ble_gattc.c) */
static void ble_central_gattc_ready(void)
{
    bcen.st = BLE_CS_READY;
    ble_app_state();
}

static void ble_central_gattc_fail(uint8_t why, uint8_t code)
{
    if (why == BLE_CF_NO_MIDI)
        BLE_DG(ble_dgc.gc_no_midi++);
    bcen_fail(why, code);
}

static int ble_central_gattc_auth(uint8_t code)
{
    return bcen_secure(code);
}

#if BLE_CEN_PAIR
/* (ble_smp_init.c) a Security Request: its AuthReq's MITM bit asks for a passkey */
static void ble_central_sec_req(uint8_t auth_req)
{
    if (auth_req & 0x04u)
        bcen.mitm = 1;
    if (!bcen_secure(0))
        ble_gattc_retry();
}

static void ble_central_encrypted(void)        /* encrypted with the bond's LTK */
{
    bcen.encrypted = 1;
    bcen.auth = (uint8_t)(bcen.peer.sec & BLE_PEER_AUTH ? 1u : 0u);
    if (!bcen.pairing)
        ble_gattc_retry();
}

/* the pairing ended: ok, or failed with reason; auth: the keys are authenticated (ok), a passkey was shown (failed) */
static void ble_central_paired(int ok, uint8_t reason, uint8_t auth)
{
    bcen.pairing = 0;
    if (!ok) {                                 /* (Authentication Requirements, either way: AUTH) */
        if (bcen.repair == 1 && !auth && reason != 0x03u) {
            BLE_DG(ble_dgc.si_repair_fallback++);   /* (refused on this link before any passkey: a new link) */
            bcen_fail(BLE_CF_NEED_MITM, reason);
            return;
        }
        bcen_fail(reason == 0x03u ? BLE_CF_AUTH : BLE_CF_PAIRING, reason);
        return;
    }
    if (bcen.repair == 1)
        bcen.repair = 2;
    bcen.late_due = (uint8_t)!auth;
    bcen.encrypted = 1;                        /* (with the STK, the keys exchanged) */
    bcen.auth = auth;
    bcen.gatt_after_keys = 1;                  /* (in a later event than our keys: ble_central_event) */
}

/* (ble_smp_init.c) a Pairing Failed from the peripheral, at: 0 no pairing of ours runs, 1 ours waits for its Pairing
 * Response, 2 ours is further on -> 1: late (counted, ignored): with none of ours running; or iOS's Unspecified Reason
 * for the Just Works pairing that ended on this link (it came 3 events after our keys, blell-dev3 / dev4) arriving
 * after our Pairing Request with MITM went (its Pairing Response is still awaited). 0: it ends the pairing that runs */
static int ble_central_late_fail(uint8_t reason, int at)
{
    int late = bcen.late_due && reason == 0x08u;
    if (at == 2)
        return 0;
    if (late)
        bcen.late_due = 0;
    return at == 0 || late;
}

/* as master, our LL_ENC_REQ was refused: the peripheral lost the bond (Key Missing) -> pair afresh */
BLE_API void ble_host_enc_failed(uint8_t err)
{
    if (!ble_ll_central())
        return;
    if (bcen.pairing) {                        /* (the STK refused: the pairing is over) */
        bcen.pairing = 0;
        bcen_fail(BLE_CF_PAIRING, err);
        return;
    }
    bcen.peer.bonded = 0;
    bcen_pair();
}
#endif

BLE_API void ble_central_event(void)
{
    uint32_t now;
    if (!ble_ll_central())
        return;
    now = ble_hw_time_us();
    if (bcen.leave) {
        if (ble_ll_tx_room() + 4u >= BLE_LL_TX_RING || now - bcen.leave_t > 1000000u) {
            bcen.leave = 0;
            ble_ll_disconnect(BLE_ERR_REMOTE_USER);
        }
        return;
    }
#if BLE_CEN_PAIR
    if (bcen.settle_wait && now - bcen.settle_at >= 200000u) {
        bcen.settle_wait = 0; ble_gattc_retry();
    }
#endif
    ble_gattc_event(now);
#if BLE_CEN_PAIR
    if (bcen.gatt_after_keys && ble_ll_tx_room() + 4u >= BLE_LL_TX_RING) {
        bcen.gatt_after_keys = 0;              /* (our keys handed to the engine: the request again; after the
                                                * GATT timeout check, whose `now` is older than this request) */
        ble_gattc_retry();
    }
    if (bcen.repair == 1 && smp_init_rsp_late(now)) {   /* no answer to the pairing on this link: a new link */
        bcen.pairing = 0;
        BLE_DG(ble_dgc.si_repair_fallback++);
        bcen_fail(BLE_CF_NEED_MITM, 0xFF);
    } else if (smp_init_timed_out(now)) {
        bcen.pairing = 0;
        bcen_fail(BLE_CF_PAIRING, 0xFF);
    }
#endif
}

/* an L2CAP Connection Parameter Update Request (Core Vol 3 Part A 4.20): 0 accepted (carried out with an
 * LL_CONNECTION_UPDATE_IND at an instant), 1 rejected */
BLE_API uint8_t ble_central_sig(const uint8_t *d, uint16_t n)
{
    uint16_t imin, imax, lat, to;
    BLE_DG(ble_dgc.m_l2_upd_rx++);
    if (n < 8u)
        return 1;
    imin = ble_rd16(d), imax = ble_rd16(d + 2), lat = ble_rd16(d + 4), to = ble_rd16(d + 6);
    if (imin < 6u || imin > imax || imax > 3200u || lat > 499u || to < 10u || to > 3200u ||
        (uint32_t)to * 4u <= (1u + lat) * imax)
        return 1;
    ble_ll_conn_update(imin, imax, to);        /* (later when a procedure of ours runs: kept) */
    return 0;
}

#if BLE_CEN_PAIR
/* ah(k, r) = e(k, 0^104 || r) mod 2^24 against the hash in an RPA's low 24 bits (top two bits 01) */
BLE_API int ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6])
{
    uint8_t k[16], x[16], i;
    if ((addr[5] >> 6) != 1u)
        return 0;
    for (i = 0; i < 16u; i++)
        k[i] = irk[15 - i];                    /* (e() takes its key most significant octet first) */
    ble_zero(x, 13);
    x[13] = addr[5];                           /* prand, most significant octet first */
    x[14] = addr[4];
    x[15] = addr[3];
    ble_aes128(k, x, x);
    return x[15] == addr[0] && x[14] == addr[1] && x[13] == addr[2];
}
#endif
