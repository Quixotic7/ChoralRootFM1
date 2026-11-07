/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* The Security Manager, peripheral (responder) side (Core Specification Vol 3 Part H); with BLE_CENTRAL 1
 * (BLE_CEN_PAIR) also the initiator, for the links we are the master of (ble_smp_init.c, included below). BLE_CENTRAL
 * 3 pairs nothing as master: smp_cen_refuse.
 *
 * BLE_SMP_LEGACY=0: a Pairing Request gets Pairing Failed, Pairing Not Supported (3.5.5); nothing else is answered.
 * BLE_SMP_LEGACY=1: LE legacy pairing with Just Works (2.3.5.2: we have no input and no output, TK = 0):
 *   Pairing Request -> our Pairing Response (NoInputNoOutput, no OOB, bonding if the central bonds, no MITM, no
 *   Secure Connections: so legacy pairing is used), then Mconfirm -> our Sconfirm = c1(TK, Srand, ...), Mrand
 *   (checked against Mconfirm) -> our Srand; STK = s1(TK, Srand, Mrand) cut to the key size. The central starts
 *   the LL encryption with the STK (EDIV 0, Rand 0: ble_host_ltk); once it runs, phase 3: we hand out our LTK,
 *   EDIV and Rand (Encryption Information, Master Identification) and, when the central asks for it, our identity
 *   (Identity Information: an IRK, Identity Address Information: our static address; we never use a private
 *   address, so the IRK only lets the central file us by identity: smp_own_irk), then take the central's keys:
 *   its IRK and identity address go to the firmware (ble_app_peer_id: what finds a device behind a resolvable
 *   private address, docs/BLE-DEVICES-DESIGN.md §4.3, docs/BLE-STACK.md §9.2); its LTK and CSRK are not kept (we find a returning
 *   central by EDIV / Rand). Our LTK comes from ble_host_new_key, which needs nothing kept for each peer; its secret goes
 *   to the firmware to keep across power-offs (ble_app_bond).
 *   BLE_SMP_SEC_REQ=1: a Security Request (bonding) at the start of every connection.
 * Not done (as responder): the 30 s SMP timeout (a stalled pairing just waits for the next Pairing Request or the
 * link's end), LE Secure Connections, passkey / OOB, signing. The initiator (ble_smp_init.c) also does legacy passkey
 * entry, with us displaying, for a peripheral that needs an authenticated link. */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_hw.h"
#include "ble_util.h"
#include "ble_diag.h"
#if BLE_SMP_LEGACY
#include "ble_aes.h"
#endif

enum { SMP_PAIR_REQ = 0x01, SMP_PAIR_RSP, SMP_CONFIRM, SMP_RANDOM, SMP_FAILED, SMP_ENC_INFO, SMP_MASTER_ID,
       SMP_ID_INFO, SMP_ID_ADDR, SMP_SIGN_INFO, SMP_SEC_REQ };
/* Pairing Failed reasons (3.5.5) */
enum { SMP_E_AUTH_REQ = 0x03, SMP_E_CONFIRM = 0x04, SMP_E_NOT_SUPP = 0x05, SMP_E_KEY_SIZE = 0x06, SMP_E_CMD = 0x07,
       SMP_E_UNSPEC = 0x08, SMP_E_INVALID = 0x0A };

static void smp_send(const uint8_t *p, uint16_t n) { ble_ll_send(L2CAP_CID_SMP, p, n); }

static void smp_fail(uint8_t why)
{
    uint8_t f[2];
    f[0] = SMP_FAILED;
    f[1] = why;
    smp_send(f, 2);
}

#if BLE_CENTRAL && !BLE_CEN_PAIR
/* BLE_CENTRAL 3: no pairing from the central role. On a link we are the master of, the peripheral's Security Request
 * (it wants the link paired or encrypted) gets Pairing Failed, Pairing Not Supported (Vol 3 Part H 2.4.6, 3.5.5) and the
 * host leaves the link ("FAILED: NEEDS PAIRING", ble_central_no_pairing); a responder's other PDUs cannot come, as no
 * pairing of ours runs. -> 1: the link is ours (the PDU taken here), 0: the peripheral side's */
static int smp_cen_refuse(const uint8_t *p, uint16_t n)
{
    if (!ble_ll_central())
        return 0;
    if (n && p[0] == SMP_SEC_REQ) {
        smp_fail(SMP_E_NOT_SUPP);
        ble_central_no_pairing(0);
    }
    return 1;
}
#endif

#if !BLE_SMP_LEGACY

BLE_API void ble_smp_reset(void) {}
BLE_API void ble_smp_connected(void) {}

BLE_API void ble_smp_rx(const uint8_t *p, uint16_t n)
{
#if BLE_CENTRAL
    if (smp_cen_refuse(p, n))
        return;
#endif
    if (n && p[0] == SMP_PAIR_REQ)
        smp_fail(SMP_E_NOT_SUPP);
}

#else

enum { S_IDLE, S_CONFIRM, S_RANDOM, S_ENC, S_KEYS };
#define SMP_AUTH_BOND 0x01u                 /* AuthReq: Bonding_Flags = Bonding */
#define SMP_AUTH_MITM 0x04u                 /* AuthReq: MITM protection asked */
#define SMP_KD_ENC 0x01u                    /* key distribution: EncKey (LTK, EDIV, Rand) */
#define SMP_KD_ID 0x02u                     /* IdKey (IRK, identity address) */
#define SMP_KD_SIGN 0x04u                   /* SignKey (CSRK) */

static struct {
    uint8_t st, key_size, bond, ours, theirs;   /* S_*; the key size; bonding; our keys to send / theirs to come */
    uint8_t preq[7], pres[7];                   /* the Pairing Request and Response as sent (c1) */
    uint8_t mconf[16], srand[16], stk[16];
    uint8_t irk[16], has_irk;                   /* the central's Identity Information, until its address comes */
#if BLE_CEN_PAIR
    uint8_t init;                               /* we are the initiator (the link's master: ble_smp_init.c) */
    uint8_t mrand[16];                          /* our random; mconf holds the responder's confirm then */
    uint8_t tk[16];                             /* the TK: 0 (Just Works) or the passkey (passkey entry) */
    uint8_t mitm, auth;                         /* MITM asked; the method is passkey entry */
    uint32_t t0;                               /* our last SMP command (the 30 s SMP timeout, 3.4) */
    struct ble_keys keys;                       /* the responder's keys as they come */
#endif
} bsmp;
#if BLE_CEN_PAIR
/* the passkey on screen while a passkey pairing of ours waits for the peer's user (BLE_NO_PASSKEY: none). A variable
 * of its own (a symbol): the emulator's virtual phone user reads it there (fm1-emulator FM1_BLE_PASSKEY_AT) */
static uint32_t smp_passkey = BLE_NO_PASSKEY;
#endif

BLE_API void ble_smp_reset(void)
{
    ble_zero((uint8_t *)&bsmp, sizeof bsmp);
#if BLE_CEN_PAIR
    smp_passkey = BLE_NO_PASSKEY;
#endif
}

BLE_API void ble_smp_connected(void)
{
    ble_smp_reset();
#if BLE_CENTRAL
    if (ble_ll_central())
        return;                                 /* (as initiator: the host starts a pairing when one is needed; none
                                                 * at BLE_CENTRAL 3) */
#endif
#if BLE_SMP_SEC_REQ
    {
        static const uint8_t req[2] = {SMP_SEC_REQ, SMP_AUTH_BOND};
        smp_send(req, 2);
    }
#endif
}

static void smp_cut(uint8_t k[16])              /* a key to the negotiated size: its most significant octets 0 */
{
    ble_zero(k + bsmp.key_size, 16u - bsmp.key_size);
}

static void smp_c1(const uint8_t r[16], uint8_t out[16])
{
    static const uint8_t tk[16] = {0};          /* Just Works: TK = 0 */
    uint8_t own[6], peer[6], t = ble_ll_addrs(own, peer);
#if BLE_CEN_PAIR
    if (bsmp.init) {                            /* the initiator's address is ours (ia), the responder's the peer's */
        ble_smp_c1(bsmp.tk, r, bsmp.preq, bsmp.pres, (uint8_t)(t & 1u), own, (uint8_t)(t >> 1 & 1u), peer, out);
        return;
    }
#endif
    ble_smp_c1(tk, r, bsmp.preq, bsmp.pres, (uint8_t)(t >> 1 & 1u), peer, (uint8_t)(t & 1u), own, out);
}

/* our IRK: the same at every pairing, in either role, made from our static address (no private address of ours
 * exists, so it only names us). A peer that bonded us refuses a new IRK for the same identity address: macOS,
 * "already paired, with a different irk ... Unpair first" (2026-10-18), and the FM-1 then asked for the passkey at
 * every power-on */
static void smp_own_irk(uint8_t irk[16])
{
    static const uint8_t K[16] = {'O', 'p', 't', 'i', 'm', 'i', 's', 't', ' ', 'I', 'R', 'K', ' ', 'v', '1', 0};
    uint8_t own[6], peer[6], in[16];
    (void)ble_ll_addrs(own, peer);
    ble_zero(in, 16);
    ble_cpy(in, own, 6);
    ble_aes128(K, in, irk);
}

#if BLE_CEN_PAIR
#include "ble_smp_init.c"                       /* the initiator (the master of the link) */
#endif

static void smp_pair_req(const uint8_t *p)
{
    uint8_t *r = bsmp.pres;
    ble_smp_reset();
    if (p[4] < 7u || p[4] > 16u) {
        smp_fail(SMP_E_KEY_SIZE);
        return;
    }
    bsmp.bond = (p[3] & 3u) == SMP_AUTH_BOND;
    bsmp.key_size = p[4];
    ble_cpy(bsmp.preq, p, 7);
    r[0] = SMP_PAIR_RSP;
    r[1] = 0x03;                                /* IO capability: NoInputNoOutput */
    r[2] = 0x00;                                /* no OOB data */
    r[3] = bsmp.bond ? SMP_AUTH_BOND : 0u;      /* no MITM, no Secure Connections, no keypress */
    r[4] = 16;                                  /* our largest key size */
    r[5] = bsmp.bond ? (uint8_t)(p[5] & (SMP_KD_ENC | SMP_KD_ID | SMP_KD_SIGN)) : 0u;   /* the central's: taken */
    r[6] = bsmp.bond ? (uint8_t)(p[6] & (SMP_KD_ENC | SMP_KD_ID)) : 0u;                /* ours: LTK, identity */
    bsmp.theirs = r[5];
    bsmp.ours = r[6];
    smp_send(r, 7);
    bsmp.st = S_CONFIRM;
}

static void smp_confirm(const uint8_t *p)
{
    uint8_t c[17];
    ble_cpy(bsmp.mconf, p + 1, 16);
    ble_hw_rand(bsmp.srand, 16);
    c[0] = SMP_CONFIRM;
    smp_c1(bsmp.srand, c + 1);
    smp_send(c, 17);
    bsmp.st = S_RANDOM;
}

static void smp_random(const uint8_t *p)
{
    static const uint8_t tk[16] = {0};
    uint8_t c[17];
    smp_c1(p + 1, c + 1);
    if (!ble_eq(c + 1, bsmp.mconf, 16)) {
        bsmp.st = S_IDLE;
        smp_fail(SMP_E_CONFIRM);
        return;
    }
    c[0] = SMP_RANDOM;
    ble_cpy(c + 1, bsmp.srand, 16);
    smp_send(c, 17);
    ble_smp_s1(tk, bsmp.srand, p + 1, bsmp.stk);   /* STK = s1(TK, Srand, Mrand) */
    smp_cut(bsmp.stk);
    bsmp.st = S_ENC;
}

static void smp_done_if(void)
{
    if (bsmp.st == S_KEYS && !bsmp.theirs)
        bsmp.st = S_IDLE;                       /* (the bond is kept already: smp_our_keys) */
}

/* phase 3, the link encrypted with the STK: our LTK, EDIV, Rand (made now) */
static void smp_our_keys(void)
{
    uint8_t e[17], m[11];
    if (bsmp.ours & SMP_KD_ENC) {
        e[0] = SMP_ENC_INFO;
        m[0] = SMP_MASTER_ID;
        ble_host_new_key(bsmp.key_size, m + 1, e + 1);   /* EDIV (2), Rand (8); the LTK */
        smp_send(e, 17);
        smp_send(m, 11);
    }
    if (bsmp.ours & SMP_KD_ID) {                /* (Core Vol 3 Part H 3.6.4, 3.6.5) */
        uint8_t own[6], peer[6], t = ble_ll_addrs(own, peer);
        e[0] = SMP_ID_INFO;
        smp_own_irk(e + 1);
        m[0] = SMP_ID_ADDR;
        m[1] = (uint8_t)(t & 1u);               /* 0 public, 1 random static */
        ble_cpy(m + 2, own, 6);
        smp_send(e, 17);
        smp_send(m, 8);
    }
    bsmp.ours = 0;
}

BLE_API void ble_host_encrypted(void)
{
#if BLE_CEN_PAIR
    if (ble_ll_central()) {
        smp_init_encrypted();                   /* (a pairing's STK: the key distribution; a bond's LTK: done) */
        return;
    }
#endif
    if (bsmp.st != S_ENC)
        return;                                 /* (a returning central's LTK: nothing to do) */
    bsmp.st = S_KEYS;
    smp_our_keys();
    smp_done_if();
}

/* the STK while a pairing waits for its encryption (the LL asks with EDIV 0, Rand 0) */
BLE_API int ble_smp_stk(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16])
{
    static const uint8_t zero[8] = {0};
    if (bsmp.st != S_ENC || ediv || !ble_eq(rand, zero, 8))
        return 0;
    ble_cpy(ltk, bsmp.stk, 16);
    return 1;
}

static void smp_their_key(const uint8_t *p)
{
    uint8_t op = p[0];
    if (bsmp.st != S_KEYS)
        return;
    if (op == SMP_ID_INFO) {
        ble_cpy(bsmp.irk, p + 1, 16);
        bsmp.has_irk = 1;
    } else if (op == SMP_ID_ADDR && bsmp.has_irk && (bsmp.theirs & SMP_KD_ID))
        ble_app_peer_id(bsmp.irk, p + 2, (uint8_t)(p[1] & 1u));
    if (op == SMP_MASTER_ID)
        bsmp.theirs &= (uint8_t)~SMP_KD_ENC;    /* (Encryption Information comes before it) */
    else if (op == SMP_ID_ADDR)
        bsmp.theirs &= (uint8_t)~SMP_KD_ID;
    else if (op == SMP_SIGN_INFO)
        bsmp.theirs &= (uint8_t)~SMP_KD_SIGN;
    smp_done_if();
}

/* the length of each command we take (0: not taken) */
static const uint8_t SMP_LEN[12] = {0, 7, 0, 17, 17, 2, 17, 11, 17, 8, 17, 0};

BLE_API void ble_smp_rx(const uint8_t *p, uint16_t n)
{
    uint8_t op;
    if (!n)
        return;
    op = p[0];
#if BLE_CEN_PAIR
    if (ble_ll_central()) {
        smp_init_rx(p, n);                      /* we are the initiator (ble_smp_init.c) */
        return;
    }
#elif BLE_CENTRAL
    if (smp_cen_refuse(p, n))
        return;
#endif
    if (op >= sizeof SMP_LEN || !SMP_LEN[op]) {
        if (op != SMP_SEC_REQ && op != SMP_PAIR_RSP)
            smp_fail(SMP_E_CMD);                /* (Secure Connections' public key etc.: not supported) */
        return;
    }
    if (n != SMP_LEN[op]) {
        bsmp.st = S_IDLE;
        smp_fail(SMP_E_INVALID);
        return;
    }
    switch (op) {
    case SMP_PAIR_REQ:
        smp_pair_req(p);
        return;
    case SMP_CONFIRM:
    case SMP_RANDOM:
        if (bsmp.st != (op == SMP_CONFIRM ? S_CONFIRM : S_RANDOM)) {
            bsmp.st = S_IDLE;
            smp_fail(SMP_E_UNSPEC);
            return;
        }
        if (op == SMP_CONFIRM)
            smp_confirm(p);
        else
            smp_random(p);
        return;
    case SMP_FAILED:
        bsmp.st = S_IDLE;
        return;
    default:                                    /* the central's keys (phase 3) */
        smp_their_key(p);
        return;
    }
}

#endif
