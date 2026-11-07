/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* The ATT client of the central role (BLE_CENTRAL; Core Specification Vol 3 Part F / G), finding a peripheral's
 * BLE-MIDI characteristic and turning its notifications on (MIDI Association, "Specification for MIDI over Bluetooth
 * Low Energy" 1.0):
 *   1. Exchange MTU (ours BLE_ATT_MTU_MAX; the smaller one is used both ways, ble_att_set_mtu);
 *   2. Find By Type Value: the primary service with the BLE-MIDI UUID -> its handle range (a server that does not
 *      support it: Read By Group Type over all primary services instead);
 *   3. Read By Type 0x2803 in that range -> the MIDI I/O characteristic (its 128-bit UUID) and where it ends (the next
 *      declaration, or the service's end);
 *   4. Find Information after its value -> its Client Characteristic Configuration descriptor (0x2902);
 *   5. Write Request CCCD = 0x0001 (notifications) -> ready: notifications on the value are MIDI in (ble_att.c's
 *      decoder), MIDI out goes as Write Without Response (ble_att.c att_midi_out).
 * One request outstanding, the 30 s transaction timeout. An Error Response asking for security (Insufficient
 * Authentication 0x05, Insufficient Encryption 0x0F, Insufficient Encryption Key Size 0x0C) hands over to the host
 * (ble_central.c: encrypt with the bond, else pair) and the same request is sent again once the link is encrypted. */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_util.h"
#include "ble_diag.h"

enum { G_IDLE, G_MTU, G_SVC, G_SVC_GRP, G_CHR, G_DSC, G_READ, G_CCCD, G_READY, G_FAIL };
enum { GE_AUTHEN = 0x05, GE_NOT_SUPP = 0x06, GE_KEY_SIZE = 0x0C, GE_NOT_FOUND = 0x0A, GE_ENC = 0x0F };

/* the MIDI I/O characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3, least significant octet first */
static const uint8_t GC_UUID_MIDI_IO[16] = {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
                                            0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77};

/* (ble_central.c) */
static void ble_central_gattc_ready(void);
static void ble_central_gattc_fail(uint8_t why, uint8_t code);
static int ble_central_gattc_auth(uint8_t code);

static struct {
    uint8_t st, wait_sec, due;                 /* G_*; a request waits for the link's security; one waits for room */
    uint8_t req[23], req_n;                    /* the request outstanding (sent again after security) */
    uint32_t t_req;                            /* when it went (the 30 s timeout) */
    uint16_t svc_s, svc_e, val, end, cccd, next;   /* the service, the value, the characteristic's end, its CCCD, the
                                                * next handle to look from */
} bgc;

static void gc_send(void)
{
    if (!ble_ll_send(L2CAP_CID_ATT, bgc.req, bgc.req_n)) {
        bgc.due = 1;                           /* no room now: the next event */
        return;
    }
    bgc.due = 0;
    bgc.t_req = ble_hw_time_us();
}

static void gc_req_range(uint8_t op, uint16_t s, uint16_t e, uint16_t type)
{
    bgc.req[0] = op;
    ble_wr16(bgc.req + 1, s);
    ble_wr16(bgc.req + 3, e);
    bgc.req_n = 5;
    if (type) {
        ble_wr16(bgc.req + 5, type);
        bgc.req_n = 7;
    }
    gc_send();
}

static void gc_fail(uint8_t why, uint8_t code)
{
    bgc.st = G_FAIL;
    BLE_DG(ble_dgc.gc_state = G_FAIL);
    ble_central_gattc_fail(why, code);
}

static void gc_step(uint8_t st)
{
    bgc.st = st;
    BLE_DG(ble_dgc.gc_state = st);
}

static void gc_find_service(void)              /* Find By Type Value: primary service = the BLE-MIDI UUID */
{
    gc_step(G_SVC);
    bgc.req[0] = 0x06;
    ble_wr16(bgc.req + 1, 1);
    ble_wr16(bgc.req + 3, 0xFFFF);
    ble_wr16(bgc.req + 5, 0x2800);
    ble_cpy(bgc.req + 7, BLE_UUID_MIDI_SVC, 16);
    bgc.req_n = 23;
    gc_send();
}

static void gc_chars(uint16_t from)
{
    gc_step(G_CHR);
    gc_req_range(0x08, from, bgc.svc_e, 0x2803);
}

static void gc_descs(uint16_t from)
{
    if (from > bgc.end) {
        gc_fail(BLE_CF_NO_MIDI, 0);            /* the MIDI I/O characteristic has no CCCD */
        return;
    }
    gc_step(G_DSC);
    gc_req_range(0x04, from, bgc.end, 0);
}

static void gc_subscribe(void)
{
    gc_step(G_CCCD);
    bgc.req[0] = 0x12;                         /* Write Request */
    ble_wr16(bgc.req + 1, bgc.cccd);
    bgc.req[3] = 0x01;                         /* notifications */
    bgc.req[4] = 0x00;
    bgc.req_n = 5;
    gc_send();
}

static void gc_handshake(void)
{
#if defined(BLE_MIDI_INITIAL_READ) && BLE_MIDI_INITIAL_READ
    gc_step(G_READ); bgc.req[0] = 0x0A; ble_wr16(bgc.req + 1, bgc.val);
    bgc.req_n = 3; gc_send();
#else
    gc_subscribe();
#endif
}
static void gc_service(uint16_t s, uint16_t e)
{
    bgc.svc_s = s;
    bgc.svc_e = e;
    BLE_DG(ble_dgc.gc_svc_s = s);
    BLE_DG(ble_dgc.gc_svc_e = e);
    bgc.val = 0;
    gc_chars(s);
}

/* Read By Type Response (0x2803 declarations): handle, properties, value handle, UUID */
static void gc_chars_rsp(const uint8_t *p, uint16_t n)
{
    uint16_t i, el = p[1], last = 0;
    if (el < 7u)
        return;
    for (i = 2; i + el <= n; i = (uint16_t)(i + el)) {
        uint16_t decl = ble_rd16(p + i), v = ble_rd16(p + i + 3);
        last = decl;
        if (bgc.val && decl > bgc.val) {        /* the declaration after ours: our characteristic ends before it */
            bgc.end = (uint16_t)(decl - 1u);
            gc_descs((uint16_t)(bgc.val + 1u));
            return;
        }
        if (!bgc.val && el == 21u && ble_eq(p + i + 5, GC_UUID_MIDI_IO, 16) && (p[i + 2] & 0x10u)) {
            bgc.val = v;                       /* (notify among its properties) */
            BLE_DG(ble_dgc.gc_val = v);
        }
    }
    if (last >= bgc.svc_e || !last) {           /* the service's end */
        if (!bgc.val) {
            gc_fail(BLE_CF_NO_MIDI, 0);
            return;
        }
        bgc.end = bgc.svc_e;
        gc_descs((uint16_t)(bgc.val + 1u));
        return;
    }
    gc_chars((uint16_t)(last + 1u));            /* more declarations in the service */
}

/* Find Information Response: handle + UUID pairs (format 1: 16-bit, 2: 128-bit) */
static void gc_descs_rsp(const uint8_t *p, uint16_t n)
{
    uint16_t i, el = p[1] == 1u ? 4u : 18u, last = 0;
    for (i = 2; i + el <= n; i = (uint16_t)(i + el)) {
        last = ble_rd16(p + i);
        if (el == 4u && ble_rd16(p + i + 2) == 0x2902u) {
            bgc.cccd = last;
            BLE_DG(ble_dgc.gc_cccd = last);
            gc_handshake();
            return;
        }
    }
    gc_descs((uint16_t)(last + 1u));
}

/* Read By Group Type Response (primary services; the fallback): handle, end, UUID */
static void gc_groups_rsp(const uint8_t *p, uint16_t n)
{
    uint16_t i, el = p[1], last = 0;
    for (i = 2; i + el <= n; i = (uint16_t)(i + el)) {
        last = ble_rd16(p + i + 2);
        if (el == 20u && ble_eq(p + i + 4, BLE_UUID_MIDI_SVC, 16)) {
            gc_service(ble_rd16(p + i), last);
            return;
        }
    }
    if (!last || last == 0xFFFFu) {
        gc_fail(BLE_CF_NO_MIDI, 0);
        return;
    }
    gc_req_range(0x10, (uint16_t)(last + 1u), 0xFFFF, 0x2800);
}

static void gc_error(const uint8_t *p)
{
    uint8_t code = p[4];
    BLE_DG(ble_dgc.gc_errs++);
    BLE_DG(ble_dgc.gc_last_err_op = p[1]);
    BLE_DG(ble_dgc.gc_last_err_h = ble_rd16(p + 2));
    BLE_DG(ble_dgc.gc_last_err = code);
    if (code == GE_AUTHEN || code == GE_ENC || code == GE_KEY_SIZE) {
        BLE_DG(ble_dgc.gc_auth_errs++);
        if (ble_central_gattc_auth(code))
            bgc.wait_sec = 1;                  /* sent again once encrypted (ble_gattc_retry) */
        else if (bgc.st == G_READ) gc_subscribe();
        else gc_fail(BLE_CF_AUTH, code);
        return;
    }
    switch (bgc.st) {
    case G_READ:
        if (code == 0x02u || code == GE_NOT_SUPP) gc_subscribe();
        else gc_fail(BLE_CF_GATT, code);
        return;
    case G_SVC:
        if (code == GE_NOT_SUPP) {             /* no Find By Type Value: walk the primary services */
            gc_step(G_SVC_GRP);
            gc_req_range(0x10, 1, 0xFFFF, 0x2800);
        } else
            gc_fail(code == GE_NOT_FOUND ? BLE_CF_NO_MIDI : BLE_CF_GATT, code);
        return;
    case G_SVC_GRP:
        gc_fail(code == GE_NOT_FOUND ? BLE_CF_NO_MIDI : BLE_CF_GATT, code);
        return;
    case G_CHR:
        if (code == GE_NOT_FOUND && bgc.val) { /* no declaration after ours: it runs to the service's end */
            bgc.end = bgc.svc_e;
            gc_descs((uint16_t)(bgc.val + 1u));
        } else
            gc_fail(code == GE_NOT_FOUND ? BLE_CF_NO_MIDI : BLE_CF_GATT, code);
        return;
    case G_DSC:
        gc_fail(code == GE_NOT_FOUND ? BLE_CF_NO_MIDI : BLE_CF_GATT, code);
        return;
    default:
        gc_fail(BLE_CF_GATT, code);
        return;
    }
}

BLE_API void ble_gattc_reset(void)
{
    ble_zero((uint8_t *)&bgc, sizeof bgc);
}

/* a link as central is up: MTU first */
BLE_API void ble_gattc_start(void)
{
    ble_gattc_reset();
    BLE_DG(ble_dgc.gc_starts++);
    gc_step(G_MTU);
    bgc.req[0] = 0x02;
    ble_wr16(bgc.req + 1, BLE_ATT_MTU_MAX);
    bgc.req_n = 3;
    gc_send();
}

/* the link is encrypted now: the request that asked for it again */
BLE_API void ble_gattc_retry(void)
{
    if (!bgc.wait_sec)
        return;
    bgc.wait_sec = 0;
    BLE_DG(ble_dgc.gc_retries++);
    gc_send();
}

BLE_API uint16_t ble_gattc_midi(void) { return bgc.st == G_READY ? bgc.val : 0u; }

/* an ATT PDU from the peripheral's server: 1 taken (a response to us, a notification or an indication), 0 not ours
 * (a request from its client to our server: ble_att.c) */
BLE_API int ble_gattc_rx(const uint8_t *p, uint16_t n)
{
    uint8_t op = p[0];
    if (op == 0x1Bu || op == 0x1Du) {          /* Handle Value Notification / Indication */
        if (op == 0x1Du) {
            static const uint8_t conf = 0x1E;
            ble_ll_send(L2CAP_CID_ATT, &conf, 1);
            BLE_DG(ble_dgc.gc_ind_rx++);
        } else
            BLE_DG(ble_dgc.gc_ntf_rx++);
        if (n >= 3u && bgc.st == G_READY && ble_rd16(p + 1) == bgc.val)
            ble_att_midi_packet(p + 3, (uint16_t)(n - 3u));
        return 1;
    }
    if (op != 0x01u && (!(op & 1u) || op > 0x19u))
        return 0;                              /* requests and commands: the server's */
    if (bgc.st == G_IDLE || bgc.st == G_READY || bgc.st == G_FAIL || bgc.wait_sec)
        return 1;                              /* (nothing of ours outstanding) */
    bgc.t_req = 0;
    if (op == 0x01u) {
        if (n == 5u && p[1] == bgc.req[0])
            gc_error(p);
        return 1;
    }
    if (op != (uint8_t)(bgc.req[0] + 1u))
        return 1;                              /* (not the answer to ours) */
    switch (bgc.st) {
    case G_MTU:
        if (n == 3u) {
            uint16_t m = ble_rd16(p + 1);
            ble_att_set_mtu(m < 23u ? 23u : m);
            BLE_DG(ble_dgc.gc_mtu = ble_att_mtu());
        }
        gc_find_service();
        return 1;
    case G_SVC:
        if (n >= 5u)
            gc_service(ble_rd16(p + 1), ble_rd16(p + 3));
        else
            gc_fail(BLE_CF_GATT, 0);
        return 1;
    case G_SVC_GRP:
        if (n >= 2u)
            gc_groups_rsp(p, n);
        return 1;
    case G_CHR:
        if (n >= 2u)
            gc_chars_rsp(p, n);
        return 1;
    case G_DSC:
        if (n >= 2u)
            gc_descs_rsp(p, n);
        return 1;
    case G_READ:
        gc_subscribe(); return 1;
    case G_CCCD:                               /* Write Response: subscribed */
        if (n != 1u) return 1;
        gc_step(G_READY);
        BLE_DG(ble_dgc.gc_subscribed++);
        ble_central_gattc_ready();
        return 1;
    default:
        return 1;
    }
}

/* each connection event: a request waiting for room; the 30 s timeout */
BLE_API void ble_gattc_event(uint32_t now)
{
    if (bgc.st == G_IDLE || bgc.st == G_READY || bgc.st == G_FAIL || bgc.wait_sec)
        return;
    if (bgc.due) {
        gc_send();
        return;
    }
    if (bgc.t_req && now - bgc.t_req > BLE_GATTC_TIMEOUT_US) {
        BLE_DG(ble_dgc.gc_timeouts++);
        gc_fail(BLE_CF_GATT, 0xFF);
    }
}
