/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* The Security Manager as initiator (BLE_CENTRAL 1, BLE_CEN_PAIR: we are the master of the link; Core Specification Vol 3 Part H),
 * part of ble_smp.c (included there: it shares bsmp, smp_send, smp_c1, smp_cut). LE legacy pairing with bonding, and
 * only when the peripheral asks (a Security Request, Insufficient Authentication / Encryption: ble_central.c). Our
 * Pairing Request is always DisplayOnly, bonding + MITM (2.3.5.3, Table 2.8); the responder's IO capability picks:
 *   passkey entry   a responder that can type (KeyboardOnly / KeyboardDisplay: a Mac, a phone) gets a passkey we show
 *                   (0..999999, random, TK = the passkey); its user types it. The keys are authenticated (MITM
 *                   protection, security mode 1 level 3, Vol 3 Part C 10.2.1). Asked in the first pairing, not after
 *                   a Just Works one: macOS drops an alert that comes right after another (2026-10-18, the window
 *                   vanished before the passkey could be typed);
 *   Just Works      a responder that cannot type (another FM-1, most controllers): TK = 0, nothing on either screen
 *                   of ours. Unless the host knows the peer needs MITM (mitm 1): then we refuse with Authentication
 *                   Requirements (0x03) instead of a pairing it will refuse anyway.
 * Neither side asks for Secure Connections, so legacy pairing is used:
 *   Pairing Request -> the responder's Pairing Response (a key size under 7 fails) -> our Mconfirm = c1(TK, Mrand, ...)
 *   -> its Sconfirm (with a passkey: once its user typed it) -> our Mrand -> its Srand, checked against Sconfirm ->
 *   STK = s1(TK, Srand, Mrand) cut to the key size -> the LL encryption with the STK started by us (EDIV 0, Rand 0) ->
 *   phase 3: the responder's keys first (its LTK, EDIV, Rand: the bond; its IRK and identity address: finding it again
 *   behind a resolvable private address), then ours (an LTK, EDIV and Rand it asks for when it connects to us, Mode VISIBLE:
 *   ble_host_new_key; our IRK (smp_own_irk) and our static identity address) -> the keys to the firmware (ble_app_central_keys, with
 *   BLE_KEYS_AUTH after a passkey) and the host told (ble_central_*).
 * The 30 s SMP timeout (3.4, from our last command: the user types within it) ends the link. A Pairing Failed either
 * way ends the pairing (the host decides). Context: the BLE interrupts' (the passkey is read by the main loop). */

enum { I_RSP = 10, I_CONFIRM, I_RANDOM, I_ENC, I_KEYS };
enum { IO_DISPLAY_ONLY = 0x00, IO_KEYBOARD_ONLY = 0x02, IO_KEYBOARD_DISPLAY = 0x04 };

/* (ble_central.c) */
static void ble_central_paired(int ok, uint8_t reason, uint8_t auth);
static void ble_central_sec_req(uint8_t auth_req);
static void ble_central_encrypted(void);
static int ble_central_late_fail(uint8_t reason, int at);

static int smp_init_busy(void) { return bsmp.init && bsmp.st >= I_RSP; }

static void smp_init_send(const uint8_t *p, uint16_t n)
{
    smp_send(p, n);
    bsmp.t0 = ble_hw_time_us();                 /* (3.4: the timer restarts with each command queued) */
}

static void smp_init_fail(uint8_t why, int send)
{
    if (send)
        smp_fail(why);
    bsmp.st = S_IDLE;
    smp_passkey = BLE_NO_PASSKEY;
    BLE_DG(ble_dgc.si_last_fail = why);
    if (send)
        BLE_DG(ble_dgc.si_fail_tx++);
    ble_central_paired(0, why, bsmp.auth);     /* (auth: a passkey was shown) */
}

/* start a pairing as initiator (the host: a Security Request, an authentication error, a bond the peer lost); mitm:
 * the peer needs an authenticated link (Just Works refused), else a passkey only if it can type */
static void ble_smp_pair(int mitm)
{
    uint8_t *r = bsmp.preq;
    if (smp_init_busy())
        return;
    ble_smp_reset();
    bsmp.init = 1;
    bsmp.mitm = (uint8_t)(mitm != 0);
    r[0] = SMP_PAIR_REQ;
    r[1] = IO_DISPLAY_ONLY;                     /* IO capability: a display (the FM-1's screen) */
    r[2] = 0x00;                                /* no OOB data */
    r[3] = SMP_AUTH_BOND | SMP_AUTH_MITM;       /* no Secure Connections, no keypress */
    r[4] = 16;                                  /* our largest key size */
    r[5] = SMP_KD_ENC | SMP_KD_ID;              /* ours to them (initiator key distribution) */
    r[6] = SMP_KD_ENC | SMP_KD_ID;              /* theirs to us (responder key distribution) */
    smp_init_send(r, 7);
    bsmp.st = I_RSP;
    BLE_DG(ble_dgc.si_pair_req++);
    if (mitm)
        BLE_DG(ble_dgc.si_mitm_req++);
}

/* the method (2.3.5.1, Table 2.8 with us DisplayOnly and MITM asked): passkey entry when the responder can type */
static int smp_init_passkey_method(const uint8_t *rsp)
{
    return rsp[1] == IO_KEYBOARD_ONLY || rsp[1] == IO_KEYBOARD_DISPLAY;
}

static void smp_init_rsp(const uint8_t *p)
{
    uint8_t c[17], k[4];
    int pk = smp_init_passkey_method(p);
    BLE_DG(ble_dgc.si_rsp_io = p[1]);
    BLE_DG(ble_dgc.si_rsp_auth = p[3]);
    if (p[4] < 7u || p[4] > 16u) {
        smp_init_fail(SMP_E_KEY_SIZE, 1);
        return;
    }
    BLE_DG(ble_dgc.si_pair_rsp++);
    if (bsmp.mitm && !pk) {
        smp_init_fail(SMP_E_AUTH_REQ, 1);       /* (Just Works would not satisfy it: refused at once) */
        return;
    }
    ble_cpy(bsmp.pres, p, 7);
    bsmp.key_size = p[4];
    bsmp.bond = (p[3] & 3u) == SMP_AUTH_BOND;
    bsmp.ours = (uint8_t)(p[5] & bsmp.preq[5]);  /* what the responder agreed to take from us / give us */
    bsmp.theirs = (uint8_t)(p[6] & bsmp.preq[6]);
    if (pk) {                                   /* passkey entry: we display, its user types (TK = the passkey) */
        ble_hw_rand(k, 4);
        smp_passkey = ((uint32_t)k[0] | (uint32_t)k[1] << 8 | (uint32_t)k[2] << 16 | (uint32_t)k[3] << 24) % 1000000u;
        ble_smp_passkey_tk(smp_passkey, bsmp.tk);
        bsmp.auth = 1;
        BLE_DG(ble_dgc.si_passkey++);
    }
    ble_hw_rand(bsmp.mrand, 16);
    c[0] = SMP_CONFIRM;
    smp_c1(bsmp.mrand, c + 1);
    smp_init_send(c, 17);
    bsmp.st = I_CONFIRM;
}

static void smp_init_confirm(const uint8_t *p)
{
    uint8_t c[17];
    ble_cpy(bsmp.mconf, p + 1, 16);             /* (the responder's Sconfirm: its user typed the passkey) */
    c[0] = SMP_RANDOM;
    ble_cpy(c + 1, bsmp.mrand, 16);
    smp_init_send(c, 17);
    bsmp.st = I_RANDOM;
}

static void smp_init_random(const uint8_t *p)
{
    static const uint8_t zero[8] = {0};
    uint8_t c[16];
    smp_c1(p + 1, c);
    if (!ble_eq(c, bsmp.mconf, 16)) {           /* (a passkey typed wrong ends here) */
        smp_init_fail(SMP_E_CONFIRM, 1);
        return;
    }
    smp_passkey = BLE_NO_PASSKEY;
    BLE_DG(ble_dgc.si_confirm_ok++);
    ble_smp_s1(bsmp.tk, p + 1, bsmp.mrand, bsmp.stk);   /* STK = s1(TK, Srand, Mrand) */
    smp_cut(bsmp.stk);
    bsmp.st = I_ENC;
    BLE_DG(ble_dgc.si_stk_enc++);
    ble_ll_start_enc(bsmp.stk, zero, 0);
}

/* phase 3, after theirs: our keys as agreed, then done */
static void smp_init_our_keys(void)
{
    uint8_t e[17], m[11];
    if (bsmp.ours & SMP_KD_ENC) {
        e[0] = SMP_ENC_INFO;
        m[0] = SMP_MASTER_ID;
        ble_host_new_key(bsmp.key_size, m + 1, e + 1);
        smp_send(e, 17);
        smp_send(m, 11);
        BLE_DG(ble_dgc.si_keys_tx += 2);
    }
    if (bsmp.ours & SMP_KD_ID) {
        uint8_t own[6], peer[6], t = ble_ll_addrs(own, peer);
        e[0] = SMP_ID_INFO;
        smp_own_irk(e + 1);
        m[0] = SMP_ID_ADDR;
        m[1] = (uint8_t)(t & 1u);
        ble_cpy(m + 2, own, 6);
        smp_send(e, 17);
        smp_send(m, 8);
        BLE_DG(ble_dgc.si_keys_tx += 2);
    }
    bsmp.ours = 0;
    bsmp.st = S_IDLE;
    BLE_DG(ble_dgc.si_done++);
    if (bsmp.auth)
        BLE_DG(ble_dgc.si_auth_done++);
    if (bsmp.bond && bsmp.keys.has) {
        if (bsmp.auth)
            bsmp.keys.has |= BLE_KEYS_AUTH;
        ble_app_central_keys(&bsmp.keys);
    }
    ble_central_paired(1, 0, bsmp.auth);
}

static void smp_init_keys_if_done(void)
{
    if (bsmp.st == I_KEYS && !(bsmp.theirs & (SMP_KD_ENC | SMP_KD_ID | SMP_KD_SIGN)))
        smp_init_our_keys();
}

/* the link encrypted (ble_host_encrypted, as master): with our STK -> the key distribution; with a bond's LTK -> the
 * host carries on */
static void smp_init_encrypted(void)
{
    if (bsmp.init && bsmp.st == I_ENC) {
        bsmp.st = I_KEYS;
        smp_init_keys_if_done();
        return;
    }
    BLE_DG(ble_dgc.si_ltk_enc++);
    ble_central_encrypted();
}

static void smp_init_their_key(const uint8_t *p)
{
    uint8_t op = p[0];
    BLE_DG(ble_dgc.si_keys_rx++);
    switch (op) {
    case SMP_ENC_INFO:
        ble_cpy(bsmp.keys.ltk, p + 1, 16);
        return;
    case SMP_MASTER_ID:
        bsmp.keys.ediv = ble_rd16(p + 1);
        ble_cpy(bsmp.keys.rand, p + 3, 8);
        bsmp.keys.has |= BLE_KEYS_LTK;
        bsmp.theirs &= (uint8_t)~SMP_KD_ENC;
        break;
    case SMP_ID_INFO:
        ble_cpy(bsmp.keys.irk, p + 1, 16);
        return;
    case SMP_ID_ADDR:
        ble_cpy(bsmp.keys.id, p + 2, 6);
        bsmp.keys.id_rand = (uint8_t)(p[1] & 1u);
        bsmp.keys.has |= BLE_KEYS_ID;
        bsmp.theirs &= (uint8_t)~SMP_KD_ID;
        break;
    default:                                    /* (Signing Information: not kept) */
        bsmp.theirs &= (uint8_t)~SMP_KD_SIGN;
        break;
    }
    smp_init_keys_if_done();
}

/* an SMP PDU from the responder */
static void smp_init_rx(const uint8_t *p, uint16_t n)
{
    static const uint8_t LEN[12] = {0, 0, 7, 17, 17, 2, 17, 11, 17, 8, 17, 2};
    uint8_t op = p[0], want;
    if (op >= sizeof LEN || !LEN[op]) {         /* a Pairing Request (a responder never sends it), SC's PDUs */
        smp_fail(SMP_E_CMD);
        return;
    }
    if (n != LEN[op]) {
        if (smp_init_busy())
            smp_init_fail(SMP_E_INVALID, 1);
        return;
    }
    switch (op) {
    case SMP_SEC_REQ:                           /* the peripheral asks for security: encrypt with the bond, or pair */
        BLE_DG(ble_dgc.si_sec_req_rx++);
        if (!smp_init_busy())
            ble_central_sec_req(p[1]);
        return;
    case SMP_FAILED:
        BLE_DG(ble_dgc.si_fail_rx++);
        BLE_DG(ble_dgc.si_last_fail = p[1]);
        if (ble_central_late_fail(p[1], !smp_init_busy() ? 0 : bsmp.st == I_RSP ? 1 : 2))
            BLE_DG(ble_dgc.si_fail_late++);    /* (after the pairing: iOS did so after a Just Works one, §13.9) */
        else if (smp_init_busy())
            smp_init_fail(p[1], 0);
        return;
    case SMP_PAIR_RSP:
    case SMP_CONFIRM:
    case SMP_RANDOM:
        want = op == SMP_PAIR_RSP ? I_RSP : op == SMP_CONFIRM ? I_CONFIRM : I_RANDOM;
        if (!bsmp.init || bsmp.st != want) {
            if (smp_init_busy())
                smp_init_fail(SMP_E_UNSPEC, 1);
            return;
        }
        if (op == SMP_PAIR_RSP)
            smp_init_rsp(p);
        else if (op == SMP_CONFIRM)
            smp_init_confirm(p);
        else
            smp_init_random(p);
        return;
    default:                                    /* the responder's keys (phase 3, encrypted) */
        if (bsmp.init && bsmp.st == I_KEYS && ble_ll_encrypted())
            smp_init_their_key(p);
        return;
    }
}

/* a pairing request on an encrypted link (ble_central.c: with MITM after Just Works) left unanswered this long: the
 * pairing is dropped (the host makes a new link for it) -> 1 */
#define SMP_RSP_LATE_US 5000000u
static int smp_init_rsp_late(uint32_t now)
{
    if (!bsmp.init || bsmp.st != I_RSP || now - bsmp.t0 <= SMP_RSP_LATE_US)
        return 0;
    bsmp.st = S_IDLE;
    return 1;
}

/* the 30 s SMP timeout (3.4): no more SMP on this link; the host ends it */
static int smp_init_timed_out(uint32_t now)
{
    if (!smp_init_busy() || now - bsmp.t0 <= BLE_GATTC_TIMEOUT_US)
        return 0;
    bsmp.st = S_IDLE;
    smp_passkey = BLE_NO_PASSKEY;
    return 1;
}

BLE_API uint32_t ble_central_passkey(void)
{
    return bsmp.init && smp_init_busy() ? smp_passkey : BLE_NO_PASSKEY;
}
