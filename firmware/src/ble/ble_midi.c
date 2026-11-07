/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE-MIDI packets to and from USB-MIDI event packets (ble_midi.h). */
#include "ble_midi.h"
#include "ble_cfg.h"

#define NO_LO 0xFFu                                /* no timestamp octet written in this packet yet */

/* octets in a USB-MIDI event packet by CIN (0, 1: reserved, cable events) */
static const uint8_t BM_CIN_LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};

/* data octets after a status (channel messages and system common) */
static uint8_t bm_data_len(uint8_t st)
{
    if (st < 0xF0u)
        return (st & 0xE0u) == 0xC0u ? 1u : 2u;   /* Cx, Dx: one */
    return st == 0xF2u ? 2u : (st == 0xF1u || st == 0xF3u) ? 1u : 0u;
}

BLE_API void ble_midi_enc_reset(struct ble_midi_enc *e) { e->sysex = 0; }

BLE_API void ble_midi_enc_begin(struct ble_midi_enc *e, uint8_t *buf, uint16_t cap)
{
    e->buf = buf;
    e->cap = cap;
    e->len = 0;
    e->lo = NO_LO;
    e->rs = 0;
}

BLE_API uint16_t ble_midi_enc_len(const struct ble_midi_enc *e) { return e->len > 1u ? e->len : 0; }

/* the message's octets checked against its CIN; returns its kind: 'c' channel, 's' system common, 'r' real time,
 * 'x' SysEx octets (start, continuation or end), 0 not a message */
static char bm_kind(uint32_t cin, const uint8_t *b, uint32_t n, uint8_t open)
{
    uint32_t i, last = n - 1u;
    if (cin >= 8u)
        return cin == 0xFu ? (b[0] >= 0xF8u ? 'r' : b[0] == 0xF6u ? 's' : 0)
                           : ((b[0] >> 4) == cin && !((b[1] | (n > 2u ? b[2] : 0u)) & 0x80u) ? 'c' : 0);
    if (cin == 2u || cin == 3u || (cin == 5u && b[0] != 0xF7u))
        return bm_data_len(b[0]) + 1u == n && b[0] >= 0xF1u && b[0] <= 0xF6u && b[0] != 0xF4u && b[0] != 0xF5u &&
                       !(n > 1u && (b[1] & 0x80u)) &&
                       !(n > 2u && (b[2] & 0x80u)) ? 's' : 0;
    for (i = 0; i < n; i++) {                      /* SysEx: F0 only first, F7 only last (CIN 5..7), data else */
        if (b[i] == 0xF0u ? i != 0 : b[i] == 0xF7u ? (cin == 4u || i != last) : (b[i] & 0x80u) != 0)
            return 0;
    }
    return b[0] == 0xF0u || open ? 'x' : 0;        /* (a continuation needs an open SysEx) */
}

BLE_API int ble_midi_enc_add(struct ble_midi_enc *e, uint32_t pkt, uint32_t t_ms)
{
    uint8_t b[3], o[8], hi = (uint8_t)((t_ms >> 7) & 0x3Fu), lo = (uint8_t)(t_ms & 0x7Fu);
    uint32_t cin = pkt & 15u, n = BM_CIN_LEN[cin], i, k = 0, ts = 0, fresh = e->len == 0;
    char kind;
    b[0] = (uint8_t)(pkt >> 8);
    b[1] = (uint8_t)(pkt >> 16);
    b[2] = (uint8_t)(pkt >> 24);
    if (!n || !(kind = bm_kind(cin, b, n, e->sysex)))
        return -1;
    if (kind == 'c' && BLE_MIDI_RUNNING_STATUS && e->rs == b[0] && !fresh) {
        o[k++] = (uint8_t)(0x80u | lo);            /* running status: timestamp and data */
        ts = 1;
        for (i = 1; i < n; i++)
            o[k++] = b[i];
    } else {
        for (i = 0; i < n; i++) {
            if (b[i] & 0x80u) {                    /* every status octet (F0 and F7 too) after a timestamp */
                o[k++] = (uint8_t)(0x80u | lo);
                ts = 1;
            }
            o[k++] = b[i];
        }
    }
    if (ts && !fresh) {                            /* the time must fit this packet's header */
        uint8_t up = (uint8_t)((e->hi + 1u) & 0x3Fu);
        if (e->lo == NO_LO ? hi != e->hi : !((hi == e->hi && lo >= e->lo) || (hi == up && lo < e->lo)))
            return 0;
    }
    if (e->len + fresh + k > e->cap)
        return 0;
    if (fresh) {
        e->buf[e->len++] = (uint8_t)(0x80u | hi);
        e->hi = hi;
    }
    for (i = 0; i < k; i++)
        e->buf[e->len++] = o[i];
    if (ts) {
        e->hi = hi;
        e->lo = lo;
    }
    if (kind == 'x') {
        e->sysex = b[n - 1u] != 0xF7u;
        e->rs = 0;
    } else if (kind != 'r') {
        e->sysex = 0;                              /* (a status ends an unfinished SysEx) */
        e->rs = kind == 'c' ? b[0] : 0u;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ decoder */

BLE_API void ble_midi_dec_reset(struct ble_midi_dec *d)
{
    d->rs = d->need = d->got = d->cur = d->sysex = d->sx_n = 0;
}

static void bm_sx_put(struct ble_midi_dec *d, uint8_t b, uint16_t ts, ble_midi_sink sink, void *ctx)
{
    d->sx[d->sx_n++] = b;
    if (b == 0xF7u) {                              /* the end: CIN 5 / 6 / 7 by the octets left */
        uint32_t pkt = 4u + d->sx_n, i;
        for (i = 0; i < d->sx_n; i++)
            pkt |= (uint32_t)d->sx[i] << (8u + 8u * i);
        sink(ctx, pkt, ts);
        d->sx_n = 0;
        d->sysex = 0;
    } else if (d->sx_n == 3u) {
        sink(ctx, 4u | (uint32_t)d->sx[0] << 8 | (uint32_t)d->sx[1] << 16 | (uint32_t)d->sx[2] << 24, ts);
        d->sx_n = 0;
    }
}

static void bm_emit(struct ble_midi_dec *d, uint16_t ts, ble_midi_sink sink, void *ctx)
{
    uint32_t st = d->cur, cin = st < 0xF0u ? st >> 4 : d->need == 2u ? 3u : d->need == 1u ? 2u : 5u;
    sink(ctx, cin | st << 8 | (uint32_t)d->d[0] << 16 | (uint32_t)d->d[1] << 24, ts);
    d->got = 0;
    d->d[0] = d->d[1] = 0;
    if (st >= 0xF0u)
        d->cur = 0;                                /* system common: no running status */
}

static void bm_status(struct ble_midi_dec *d, uint8_t b, uint16_t ts, ble_midi_sink sink, void *ctx)
{
    if (b >= 0xF8u) {                              /* real time: anywhere, nothing else changes */
        sink(ctx, 0xFu | (uint32_t)b << 8, ts);
        return;
    }
    if (d->sysex) {
        if (b == 0xF7u) {
            bm_sx_put(d, b, ts, sink, ctx);
            return;
        }
        d->sysex = 0;                              /* any other status cuts the SysEx short: dropped */
        d->sx_n = 0;
    }
    d->got = 0;
    d->d[0] = d->d[1] = 0;
    if (b == 0xF0u) {
        d->rs = d->cur = 0;
        d->sysex = 1;
        bm_sx_put(d, b, ts, sink, ctx);
        return;
    }
    if (b == 0xF7u || b == 0xF4u || b == 0xF5u) {  /* a lone F7, undefined system common */
        d->rs = d->cur = 0;
        return;
    }
    d->cur = b;
    d->need = bm_data_len(b);
    d->rs = b < 0xF0u ? b : 0u;
    if (!d->need)
        bm_emit(d, ts, sink, ctx);                 /* F6 */
}

static void bm_data(struct ble_midi_dec *d, uint8_t b, uint16_t ts, ble_midi_sink sink, void *ctx)
{
    if (d->sysex) {
        bm_sx_put(d, b, ts, sink, ctx);
        return;
    }
    if (!d->cur) {
        if (!d->rs)
            return;                                /* a stray data octet */
        d->cur = d->rs;
        d->need = bm_data_len(d->rs);
        d->got = 0;
    }
    d->d[d->got++] = b;
    if (d->got == d->need)
        bm_emit(d, ts, sink, ctx);
}

BLE_API int ble_midi_dec(struct ble_midi_dec *d, const uint8_t *p, uint16_t n, ble_midi_sink sink, void *ctx)
{
    uint32_t i, hi, lo = NO_LO, ts, after_ts = 0;
    if (n < 1u || (p[0] & 0xC0u) != 0x80u)
        return 0;
    hi = p[0] & 0x3Fu;
    ts = hi << 7;
    for (i = 1; i < n; i++) {
        uint8_t b = p[i];
        if (!(b & 0x80u)) {
            bm_data(d, b, (uint16_t)ts, sink, ctx);
            after_ts = 0;
        } else if (!after_ts) {                    /* a timestamp octet */
            if (lo != NO_LO && (b & 0x7Fu) < lo)
                hi = (hi + 1u) & 0x3Fu;
            lo = b & 0x7Fu;
            ts = hi << 7 | lo;
            after_ts = 1;
        } else {
            bm_status(d, b, (uint16_t)ts, sink, ctx);
            after_ts = 0;
        }
    }
    if (!d->sysex) {                               /* only SysEx goes on in the next packet: a message cut */
        d->cur = 0;                                /* at the end is lost (running status is kept) */
        d->got = 0;
    }
    return 1;
}

BLE_API uint16_t ble_midi_last_ts(const uint8_t *p, uint16_t n)
{
    uint32_t i, hi, lo = NO_LO, after_ts = 0;
    if (!n)
        return 0;
    hi = p[0] & 0x3Fu;
    for (i = 1; i < n; i++) {
        if (!(p[i] & 0x80u))
            after_ts = 0;
        else if (!after_ts) {
            if (lo != NO_LO && (p[i] & 0x7Fu) < lo)
                hi = (hi + 1u) & 0x3Fu;
            lo = p[i] & 0x7Fu;
            after_ts = 1;
        } else
            after_ts = 0;
    }
    return (uint16_t)(hi << 7 | (lo == NO_LO ? 0u : lo));
}
