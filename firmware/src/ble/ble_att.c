/* SPDX-License-Identifier: GPL-3.0-only */
/* The ATT server (Core Specification Vol 3 Part F) over a static GATT database (Part G), and the BLE-MIDI
 * service's data path.
 *
 * Handles (fixed: Service Changed is never indicated, the database never changes while we run; docs/BLE-STACK.md §5):
 *    1  GAP service 0x1800           2,3 Device Name (read)     4,5 Appearance (read)
 *    6,7 Peripheral Preferred Connection Parameters (read)
 *    8  GATT service 0x1801          9,10 Service Changed (indicate)   11 its CCCD
 *   12  BLE-MIDI service 03B80E5A-…  13,14 MIDI I/O 7772E5DB-… (read: empty, write without response, notify)
 *   15  its CCCD
 * Requests: MTU exchange, Find Information, Find By Type Value, Read By Type, Read, Read Blob, Read By Group Type,
 * Write Request and Command; notifications and the Service Changed indication with its confirmation. Anything
 * else: Request Not Supported (commands: ignored). No attribute needs security, but with BLE_MIDI_NEED_ENC the MIDI
 * I/O value and its CCCD (Insufficient Authentication until the link is encrypted). */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_midi.h"
#include "ble_util.h"
#include "ble_diag.h"

/* the MIDI I/O characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3, least significant octet first */
static const uint8_t ATT_UUID_MIDI_IO[16] = {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
                                             0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77};
/* the Bluetooth base UUID 0000xxxx-0000-1000-8000-00805F9B34FB, octets 12 and 13 left for the 16-bit value */
static const uint8_t ATT_BASE[16] = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                     0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

enum { H_GAP = 1, H_NAME_D, H_NAME, H_APP_D, H_APP, H_PPCP_D, H_PPCP, H_GATT, H_SC_D, H_SC, H_SC_CCC, H_MIDI,
       H_MIDI_D, H_MIDI_IO, H_MIDI_CCC, H_LAST = H_MIDI_CCC };
enum { A_R = 1, A_W = 2, A_SVC = 4, A_128 = 8 };   /* readable, writable, a service declaration, 128-bit type */
enum { ATT_ERR_HANDLE = 0x01, ATT_ERR_READ = 0x02, ATT_ERR_WRITE = 0x03, ATT_ERR_PDU = 0x04, ATT_ERR_AUTHEN = 0x05,
       ATT_ERR_NOT_SUPP = 0x06, ATT_ERR_OFFSET = 0x07, ATT_ERR_NOT_FOUND = 0x0A, ATT_ERR_VAL_LEN = 0x0D,
       ATT_ERR_GROUP = 0x10 };

static const uint8_t V_GAP[] = {0x00, 0x18}, V_GATT[] = {0x01, 0x18};
static const uint8_t V_NAME_D[] = {0x02, H_NAME, 0, 0x00, 0x2A}, V_APP_D[] = {0x02, H_APP, 0, 0x01, 0x2A};
static const uint8_t V_PPCP_D[] = {0x02, H_PPCP, 0, 0x04, 0x2A}, V_SC_D[] = {0x20, H_SC, 0, 0x05, 0x2A};
static const uint8_t V_APP[] = {BLE_APPEARANCE & 0xFF, BLE_APPEARANCE >> 8};
static const uint8_t V_PPCP[] = {BLE_CONN_MIN & 0xFF, BLE_CONN_MIN >> 8, BLE_CONN_MAX & 0xFF, BLE_CONN_MAX >> 8,
                                 BLE_CONN_LATENCY & 0xFF, BLE_CONN_LATENCY >> 8, BLE_CONN_TIMEOUT & 0xFF,
                                 BLE_CONN_TIMEOUT >> 8};
static const uint8_t V_SC[] = {0x01, 0x00, 0xFF, 0xFF};
static const uint8_t V_MIDI_D[] = {0x16, H_MIDI_IO, 0, 0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
                                   0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77};   /* read, write w/o rsp, notify */

static const struct {
    uint16_t type;                       /* 16-bit UUID (A_128: the MIDI I/O characteristic's) */
    uint8_t flags, len;
    const uint8_t *val;                  /* (0: a value kept in RAM) */
} ATT_DB[H_LAST + 1] = {
    {0, 0, 0, 0},
    {0x2800, A_R | A_SVC, 2, V_GAP},
    {0x2803, A_R, 5, V_NAME_D},
    {0x2A00, A_R, 1, 0},                 /* (the name in RAM: ble_gap_name, "Optimist XXXX") */
    {0x2803, A_R, 5, V_APP_D},
    {0x2A01, A_R, 2, V_APP},
    {0x2803, A_R, 5, V_PPCP_D},
    {0x2A04, A_R, 8, V_PPCP},
    {0x2800, A_R | A_SVC, 2, V_GATT},
    {0x2803, A_R, 5, V_SC_D},
    {0x2A05, 0, 4, V_SC},
    {0x2902, A_R | A_W, 2, 0},
    {0x2800, A_R | A_SVC, 16, BLE_UUID_MIDI_SVC},
    {0x2803, A_R, 19, V_MIDI_D},
    {0, A_R | A_W | A_128, 0, 0},
    {0x2902, A_R | A_W, 2, 0},
};

static struct {
    uint16_t mtu;
    uint8_t ccc_sc, ccc_midi;            /* the client's CCCD values (bit 0 notify, bit 1 indicate) */
    uint8_t sc_due, ind_wait;            /* a Service Changed indication to send / waiting for its confirmation */
    struct ble_midi_enc enc;
    struct ble_midi_dec dec;
    uint8_t buf[BLE_ATT_MTU_MAX];        /* responses and notifications */
    uint8_t dyn[2];                      /* a RAM value read out */
} batt;

BLE_API void ble_att_reset(void)
{
    batt.mtu = 23;
    batt.ccc_sc = batt.ccc_midi = batt.sc_due = batt.ind_wait = 0;
    ble_midi_enc_reset(&batt.enc);
    ble_midi_dec_reset(&batt.dec);
}

BLE_API int ble_midi_ready(void)
{
#if BLE_CENTRAL
    if (ble_ll_central())
        return ble_gattc_midi() != 0;           /* as central: the peripheral's notifications are on */
#endif
    return ble_ll_connected() && (batt.ccc_midi & 1u);
}

BLE_API void ble_att_set_mtu(uint16_t client_mtu)
{
    batt.mtu = (uint16_t)ble_min(client_mtu < 23u ? 23u : client_mtu, BLE_ATT_MTU_MAX);
}

BLE_API uint16_t ble_att_mtu(void) { return batt.mtu; }

static void att_send(uint16_t n) { ble_ll_send(L2CAP_CID_ATT, batt.buf, n); }

/* BLE_MIDI_NEED_ENC: the MIDI I/O value and its CCCD only over an encrypted link */
static int att_need_enc(uint16_t h)
{
    return BLE_MIDI_NEED_ENC && (h == H_MIDI_IO || h == H_MIDI_CCC) && !ble_ll_encrypted();
}

static void att_error(uint8_t op, uint16_t h, uint8_t err)
{
    batt.buf[0] = 0x01;
    batt.buf[1] = op;
    ble_wr16(batt.buf + 2, h);
    batt.buf[4] = err;
    att_send(5);
}

static uint8_t att_len(uint16_t h, const uint8_t **v)   /* an attribute's value */
{
    if (ATT_DB[h].val || !ATT_DB[h].len) {
        *v = ATT_DB[h].val;
        return ATT_DB[h].len;
    }
    if (h == H_NAME)
        return ble_gap_name(v);
    batt.dyn[0] = h == H_SC_CCC ? batt.ccc_sc : batt.ccc_midi;
    batt.dyn[1] = 0;
    *v = batt.dyn;
    return 2;
}

/* the request's UUID (2 or 16 octets) as a 16-bit one; 0 for a 128-bit UUID that is not a base one */
static uint16_t att_uuid16(const uint8_t *u, uint16_t n)
{
    if (n == 2u)
        return ble_rd16(u);
    if (n == 16u && ble_eq(u, ATT_BASE, 12) && !u[14] && !u[15])
        return ble_rd16(u + 12);
    return 0;
}

static int att_type_is(uint16_t h, const uint8_t *u, uint16_t n)
{
    if (ATT_DB[h].flags & A_128)
        return n == 16u && ble_eq(u, ATT_UUID_MIDI_IO, 16);
    return ATT_DB[h].type == att_uuid16(u, n) && ATT_DB[h].type;
}

static uint16_t att_group_end(uint16_t h)       /* the last handle of the service declared at h */
{
    while (h < H_LAST && !(ATT_DB[h + 1u].flags & A_SVC))
        h++;
    return h;
}

static int att_range(const uint8_t *p, uint16_t *s, uint16_t *e)
{
    *s = ble_rd16(p + 1);
    *e = ble_rd16(p + 3);
    if (!*s || *s > *e) {
        att_error(p[0], *s, ATT_ERR_HANDLE);
        return 0;
    }
    return 1;
}

static void att_find_info(const uint8_t *p)
{
    uint16_t s, e, h, k = 2;
    if (!att_range(p, &s, &e))
        return;
    batt.buf[0] = 0x05;
    for (h = s; h <= e && h <= H_LAST; h++) {
        uint8_t fmt = ATT_DB[h].flags & A_128 ? 2u : 1u, ul = fmt == 2u ? 16u : 2u;
        if (k == 2u)
            batt.buf[1] = fmt;
        else if (batt.buf[1] != fmt)
            break;
        if (k + 2u + ul > batt.mtu)
            break;
        ble_wr16(batt.buf + k, h);
        if (fmt == 2u)
            ble_cpy(batt.buf + k + 2u, ATT_UUID_MIDI_IO, 16);
        else
            ble_wr16(batt.buf + k + 2u, ATT_DB[h].type);
        k = (uint16_t)(k + 2u + ul);
    }
    if (k == 2u)
        att_error(p[0], s, ATT_ERR_NOT_FOUND);
    else
        att_send(k);
}

static void att_find_by_type(const uint8_t *p, uint16_t n)
{
    uint16_t s, e, h, k = 1, type = ble_rd16(p + 5);
    if (!att_range(p, &s, &e))
        return;
    batt.buf[0] = 0x07;
    for (h = s; h <= e && h <= H_LAST && k + 4u <= batt.mtu; h++) {
        const uint8_t *v;
        uint8_t vl;
        if ((ATT_DB[h].flags & A_128) || ATT_DB[h].type != type)
            continue;
        vl = att_len(h, &v);
        if (vl != n - 7u || !ble_eq(v, p + 7, vl))
            continue;
        ble_wr16(batt.buf + k, h);
        ble_wr16(batt.buf + k + 2u, (ATT_DB[h].flags & A_SVC) ? att_group_end(h) : h);
        k = (uint16_t)(k + 4u);
    }
    if (k == 1u)
        att_error(p[0], s, ATT_ERR_NOT_FOUND);
    else
        att_send(k);
}

/* Read By Type (0x08) and Read By Group Type (0x10): handle (+ group end) + value, all of one length */
static void att_read_by(const uint8_t *p, uint16_t n)
{
    uint16_t s, e, h, k = 2, grp = p[0] == 0x10u, ul = (uint16_t)(n - 5u), t;
    if (ul != 2u && ul != 16u) {
        att_error(p[0], 0, ATT_ERR_PDU);
        return;
    }
    if (!att_range(p, &s, &e))
        return;
    t = att_uuid16(p + 5, ul);
    if (grp && t != 0x2800u && t != 0x2801u) {
        att_error(p[0], s, ATT_ERR_GROUP);
        return;
    }
    batt.buf[0] = (uint8_t)(p[0] + 1u);
    for (h = s; h <= e && h <= H_LAST; h++) {
        const uint8_t *v;
        uint32_t vl, el;
        if (!att_type_is(h, p + 5, ul))
            continue;
        if (!(ATT_DB[h].flags & A_R)) {
            if (k == 2u)
                att_error(p[0], h, ATT_ERR_READ);
            else
                break;
            return;
        }
        vl = ble_min(att_len(h, &v), (uint32_t)batt.mtu - 2u - (grp ? 4u : 2u));
        vl = ble_min(vl, 251u);
        el = (grp ? 4u : 2u) + vl;
        if (k == 2u)
            batt.buf[1] = (uint8_t)el;
        else if (batt.buf[1] != el || k + el > batt.mtu)
            break;
        ble_wr16(batt.buf + k, h);
        if (grp)
            ble_wr16(batt.buf + k + 2u, att_group_end(h));
        ble_cpy(batt.buf + k + el - vl, v, vl);
        k = (uint16_t)(k + el);
    }
    if (k == 2u)
        att_error(p[0], s, ATT_ERR_NOT_FOUND);
    else
        att_send(k);
}

static void att_read(const uint8_t *p, uint16_t n)
{
    uint16_t h = ble_rd16(p + 1), off = p[0] == 0x0Cu ? ble_rd16(p + 3) : 0u;
    const uint8_t *v;
    uint32_t vl;
    if (n < (p[0] == 0x0Cu ? 5u : 3u)) {
        att_error(p[0], 0, ATT_ERR_PDU);
        return;
    }
    if (!h || h > H_LAST) {
        att_error(p[0], h, ATT_ERR_HANDLE);
        return;
    }
    if (!(ATT_DB[h].flags & A_R)) {
        att_error(p[0], h, ATT_ERR_READ);
        return;
    }
    if (att_need_enc(h)) {
        att_error(p[0], h, ATT_ERR_AUTHEN);
        return;
    }
    vl = att_len(h, &v);
    if (off > vl) {
        att_error(p[0], h, ATT_ERR_OFFSET);
        return;
    }
    vl = ble_min(vl - off, (uint32_t)batt.mtu - 1u);
    batt.buf[0] = (uint8_t)(p[0] + 1u);
    ble_cpy(batt.buf + 1, v + off, vl);
    att_send((uint16_t)(1u + vl));
}

static void att_midi_in(void *ctx, uint32_t pkt, uint16_t ts)
{
    BLE_DIAG_MIDI_KIND(ble_dg, pkt);
    ble_app_midi_in(pkt, ts, *(const uint16_t *)ctx);
}

/* one BLE-MIDI packet in: a central's write to our MIDI I/O, or (as central) the peripheral's notification */
BLE_API void ble_att_midi_packet(const uint8_t *v, uint16_t n)
{
    uint16_t last = ble_midi_last_ts(v, n);
#if BLE_DIAG
    uint32_t i, r = ble_dg.mi_raw_n++ & 3u;
    for (i = 0; i < 12u; i++)
        ble_dg.mi_raw[r][i] = i < n ? v[i] : 0u;
    ble_dg.mi_raw_len[r] = (uint8_t)(n > 255u ? 255u : n);
    if (ble_midi_dec(&batt.dec, v, n, att_midi_in, &last))
        ble_dg.mi_pkts++;
    else
        ble_dg.mi_bad_hdr++;
#else
    (void)ble_midi_dec(&batt.dec, v, n, att_midi_in, &last);
#endif
}

/* returns an ATT error code, 0 when written */
static uint8_t att_write(uint16_t h, const uint8_t *v, uint16_t n)
{
    if (!h || h > H_LAST)
        return ATT_ERR_HANDLE;
    if (!(ATT_DB[h].flags & A_W))
        return ATT_ERR_WRITE;
    if (att_need_enc(h))
        return ATT_ERR_AUTHEN;
    if (h == H_MIDI_IO) {
        ble_att_midi_packet(v, n);
        return 0;
    }
    if (n != 2u)
        return ATT_ERR_VAL_LEN;
    if (h == H_SC_CCC) {
        batt.ccc_sc = v[0] & 2u;                /* indications only */
        batt.sc_due = BLE_SC_ON_SUBSCRIBE && batt.ccc_sc;
    } else {
        uint8_t was = batt.ccc_midi;
        batt.ccc_midi = v[0] & 1u;              /* notifications only */
        if (batt.ccc_midi != was) {
            if (batt.ccc_midi) {
                ble_midi_enc_reset(&batt.enc);
                ble_sig_want_fast();           /* stock asks for 7.5..11.25 ms here too */
            }
            ble_app_state();
        }
    }
    return 0;
}

BLE_API void ble_att_rx(const uint8_t *p, uint16_t n)
{
    uint8_t op, err;
    if (!n)
        return;
    op = p[0];
    BLE_DG(ble_diag_last(ble_dg.att_rx, &ble_dg.att_rx_n, op));
#if BLE_CENTRAL
    if (ble_ll_central() && ble_gattc_rx(p, n))
        return;                                /* (a response to our client, a notification: ble_gattc.c) */
#endif
    switch (op) {
    case 0x02:                                 /* Exchange MTU */
        if (n != 3u)
            break;
        batt.mtu = (uint16_t)ble_min(ble_rd16(p + 1) < 23u ? 23u : ble_rd16(p + 1), BLE_ATT_MTU_MAX);
        batt.buf[0] = 0x03;
        ble_wr16(batt.buf + 1, BLE_ATT_MTU_MAX);
        att_send(3);
        return;
    case 0x04:
        if (n != 5u)
            break;
        att_find_info(p);
        return;
    case 0x06:
        if (n < 7u)
            break;
        att_find_by_type(p, n);
        return;
    case 0x08:
    case 0x10:
        if (n < 7u)
            break;
        att_read_by(p, n);
        return;
    case 0x0A:
    case 0x0C:
        att_read(p, n);
        return;
    case 0x12:                                 /* Write Request */
    case 0x52:                                 /* Write Command */
        if (n < 3u)
            break;
        if (ble_rd16(p + 1) != H_MIDI_IO) {
            BLE_DG(ble_dg.mi_w_other++);
            BLE_DG(ble_dg.mi_w_other_h = ble_rd16(p + 1));
        } else if (op == 0x52)
            BLE_DG(ble_dg.mi_wcmd++);
        else
            BLE_DG(ble_dg.mi_wreq++);
        err = att_write(ble_rd16(p + 1), p + 3, (uint16_t)(n - 3u));
        if (op == 0x52)
            return;
        if (err)
            att_error(op, ble_rd16(p + 1), err);
        else {
            batt.buf[0] = 0x13;
            att_send(1);
        }
        return;
    case 0x1E:                                 /* Handle Value Confirmation */
        batt.ind_wait = 0;
        return;
    default:
        if (!(op & 0x40u) && (op & 1u) == 0 && op != 0x1Eu)
            att_error(op, 0, ATT_ERR_NOT_SUPP);   /* requests (even opcodes) we do not serve */
        return;
    }
    att_error(op, 0, ATT_ERR_PDU);
}

/* the queued MIDI events as notifications, at most BLE_MIDI_PER_EVENT a connection event */
static void att_midi_out(void)
{
    uint32_t pkt, t, k;
    uint16_t h = H_MIDI_IO;
    uint8_t op = 0x1B, on = batt.ccc_midi & 1u;   /* Handle Value Notification to the central that listens */
#if BLE_CENTRAL
    if (ble_ll_central()) {                    /* as central: Write Without Response to the peripheral's MIDI I/O */
        h = ble_gattc_midi();
        op = 0x52;
        on = h != 0;
    }
#endif
    if (!on) {
        while (ble_app_midi_peek(&pkt, &t))   /* nobody listening: nothing goes stale */
            ble_app_midi_pop();
        return;
    }
    for (k = 0; k < BLE_MIDI_PER_EVENT; k++) {
        uint16_t len;
        if (!ble_app_midi_peek(&pkt, &t) || ble_ll_tx_room() < batt.mtu + BLE_ATT_RESERVE)
            return;
        ble_midi_enc_begin(&batt.enc, batt.buf + 3, (uint16_t)(batt.mtu - 3u));
        while (ble_app_midi_peek(&pkt, &t)) {
            int r = ble_midi_enc_add(&batt.enc, pkt, t);
            if (!r)
                break;
            ble_app_midi_pop();                /* (added, or not a message: dropped) */
        }
        len = ble_midi_enc_len(&batt.enc);
        if (!len)
            return;
        batt.buf[0] = op;                       /* Handle Value Notification / Write Command */
        ble_wr16(batt.buf + 1, h);
        att_send((uint16_t)(3u + len));
#if BLE_CENTRAL
        BLE_DG(ble_dgc.gc_wcmd_tx += op == 0x52);
#endif
    }
}

BLE_API void ble_att_event(void)
{
    if (batt.sc_due && !batt.ind_wait && ble_ll_tx_room() >= 7u + BLE_ATT_RESERVE) {
        batt.buf[0] = 0x1D;                     /* Handle Value Indication: Service Changed, 0x0001..0xFFFF */
        ble_wr16(batt.buf + 1, H_SC);
        ble_cpy(batt.buf + 3, V_SC, 4);
        att_send(7);
        batt.sc_due = 0;
        batt.ind_wait = 1;
    }
    att_midi_out();
}
