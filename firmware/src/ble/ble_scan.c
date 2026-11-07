/* SPDX-License-Identifier: GPL-3.0-only */
/* The AD parser and the found-devices table (ble_scan.h). Written from the Core Specification Supplement Part A
 * (AD types 0x01, 0x06, 0x07, 0x08, 0x09) and Core Vol 6 Part B 2.3 (advertising PDUs). No vendor code. */
#include "ble_scan.h"
#include "ble_host.h"                    /* BLE_UUID_MIDI_SVC */
#include "ble_util.h"

enum { SC_ADV_IND = 0, SC_SCAN_RSP = 4 };

BLE_API void ble_ad_parse(const uint8_t *ad, uint32_t n, struct ble_ad *out)
{
    uint32_t i = 0;
    ble_zero((uint8_t *)out, sizeof *out);
    while (i < n) {
        uint32_t len = ad[i], k;
        const uint8_t *d = ad + i + 2;
        if (!len)
            break;                       /* the significant part ends (the rest is padding) */
        if (i + 1u + len > n) {
            out->bad = 1;
            break;
        }
        switch (ad[i + 1]) {
        case 0x01:
            if (len >= 2u) {
                out->flags = d[0];
                out->has_flags = 1;
            }
            break;
        case 0x06:                       /* incomplete / complete list of 128-bit service UUIDs */
        case 0x07:
            for (k = 0; k + 16u <= len - 1u; k += 16u)
                if (ble_eq(d + k, BLE_UUID_MIDI_SVC, 16))
                    out->midi = 1;
            break;
        case 0x08:
        case 0x09:
            if (out->name_type != 0x09u) {   /* the complete name wins over a shortened one */
                out->name_type = ad[i + 1];
                out->name = d;
                out->name_len = (uint8_t)(len - 1u);
            }
            break;
        default:
            break;
        }
        i += 1u + len;
    }
}

/* a name as the screen's Latin-1 font shows it: ASCII as it is, a two-octet UTF-8 Latin-1 character decoded, anything
 * else '?'; at most BLE_NAME_MAX characters, terminated */
static void sc_name(char *dst, const uint8_t *s, uint32_t n)
{
    uint32_t i = 0, o = 0;
    while (i < n && o < BLE_NAME_MAX) {
        uint8_t c = s[i++];
        if (c >= 0x20u && c < 0x7Fu)
            dst[o++] = (char)c;
        else if ((c == 0xC2u || c == 0xC3u) && i < n && (s[i] & 0xC0u) == 0x80u) {
            uint8_t l = (uint8_t)((c & 3u) << 6 | (s[i++] & 0x3Fu));
            dst[o++] = (char)(l >= 0xA0u ? l : '?');
        } else if (c >= 0x20u || c == 0) {
            if (c == 0)
                break;                   /* (a padded name) */
            dst[o++] = '?';
            while (i < n && (s[i] & 0xC0u) == 0x80u)
                i++;                     /* (the rest of that UTF-8 character) */
        }
    }
    dst[o] = 0;
}

BLE_API void ble_scan_clear(struct ble_scan_tab *t)
{
    uint32_t gen = t->gen;
    ble_zero((uint8_t *)t, sizeof *t);
    t->gen = gen + 1u;
}

static int sc_find(const struct ble_scan_tab *t, const uint8_t *a, uint8_t rnd)
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_N; i++)
        if (t->e[i].used && t->e[i].addr_rand == rnd && ble_eq(t->e[i].addr, a, 6))
            return (int)i;
    return -1;
}

/* a free slot, else the oldest unconfirmed one; a BLE-MIDI device may also take the slot of the listed device heard
 * least recently when that one is not heard for 2 s. -1: none */
static int sc_slot(const struct ble_scan_tab *t, int midi, uint32_t now_ms)
{
    uint32_t i;
    int un = -1, old = -1;
    for (i = 0; i < BLE_SCAN_N; i++) {
        const struct ble_found *e = &t->e[i];
        if (!e->used)
            return (int)i;
        if (!e->midi && (un < 0 || e->seen_ms < t->e[un].seen_ms))
            un = (int)i;
        if (e->midi && (old < 0 || e->seen_ms < t->e[old].seen_ms))
            old = (int)i;
    }
    if (un >= 0)
        return un;
    if (midi && old >= 0 && now_ms - t->e[old].seen_ms > 2000u)
        return old;
    return -1;
}

static void sc_listed(struct ble_scan_tab *t, struct ble_found *e)
{
    e->midi = 1;
    e->order = ++t->next_order;          /* new ones go to the bottom of the list */
    t->gen++;
}

BLE_API void ble_scan_add(struct ble_scan_tab *t, const uint8_t *pdu, uint32_t len, uint16_t rssi, uint32_t now_ms)
{
    struct ble_ad ad;
    struct ble_found *e;
    uint8_t type, rnd;
    int i;
    t->reports++;
    if (len < 8u || pdu[1] < 6u || pdu[1] > 37u || len < 2u + pdu[1]) {
        t->bad_ad++;
        return;
    }
    type = (uint8_t)(pdu[0] & 0x0Fu);
    rnd = (uint8_t)(pdu[0] >> 6 & 1u);   /* TxAdd */
    if (type != SC_ADV_IND && type != SC_SCAN_RSP) {
        t->ignored_type++;               /* not connectable (or directed elsewhere): never a list entry */
        return;
    }
    ble_ad_parse(pdu + 8, pdu[1] - 6u, &ad);
    if (ad.bad)
        t->bad_ad++;
    i = sc_find(t, pdu + 2, rnd);
    if (i < 0) {
        if (type != SC_ADV_IND)
            return;                      /* a scan response is only news about an advertiser already seen */
        i = sc_slot(t, ad.midi, now_ms);
        if (i < 0) {
            t->dropped_full++;
            return;
        }
        e = &t->e[i];
        if (e->used && e->midi)
            t->gen++;                    /* (a listed one gave its place) */
        ble_zero((uint8_t *)e, sizeof *e);
        e->used = 1;
        e->addr_rand = rnd;
        ble_cpy(e->addr, pdu + 2, 6);
    }
    e = &t->e[i];
    t->kept++;
    e->seen_ms = now_ms;
    if (rssi)
        e->rssi = rssi;
    if (e->hits < 255u)
        e->hits++;
    if (ad.name_type && (ad.name_type == 0x09u || e->name_type != 0x09u)) {
        char nm[BLE_NAME_MAX + 1u];
        uint32_t k;
        sc_name(nm, ad.name, ad.name_len);
        for (k = 0; k <= BLE_NAME_MAX && nm[k] == e->name[k] && nm[k]; k++)
            ;
        if (nm[k] != e->name[k]) {
            ble_cpy((uint8_t *)e->name, (const uint8_t *)nm, sizeof nm);
            if (e->midi)
                t->gen++;
        }
        e->name_type = ad.name_type;
    }
    e->kind = (uint8_t)(BLE_KIND_MIDI | (ble_eq((const uint8_t *)e->name, (const uint8_t *)"Optimist ", 9) ||
                                                 ble_eq((const uint8_t *)e->name, (const uint8_t *)"FM-1 ", 5) ?
                                         BLE_KIND_FM1 : 0u));   /* (our name; "FM-1 " is that of the builds before 2026-10-16) */
    if (ad.midi && !e->midi)
        sc_listed(t, e);
}

BLE_API void ble_scan_age(struct ble_scan_tab *t, uint32_t now_ms)
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_N; i++) {
        struct ble_found *e = &t->e[i];
        if (!e->used)
            continue;
        if (now_ms - e->seen_ms > (e->midi ? BLE_SCAN_AGE_MS : BLE_SCAN_AGE_UNCONF_MS)) {
            if (e->midi)
                t->gen++;
            e->used = 0;
            continue;
        }
        e->hits = (uint8_t)((e->hits + 1u) / 2u);
    }
}

BLE_API uint32_t ble_scan_list(const struct ble_scan_tab *t, uint8_t idx[BLE_SCAN_N])
{
    uint32_t n = 0, i, k;
    for (i = 0; i < BLE_SCAN_N; i++)
        if (t->e[i].used && t->e[i].midi) {
            for (k = n; k > 0 && t->e[idx[k - 1u]].order > t->e[i].order; k--)
                idx[k] = idx[k - 1u];    /* (insertion by first-listed order) */
            idx[k] = (uint8_t)i;
            n++;
        }
    return n;
}

BLE_API uint32_t ble_scan_strength(uint16_t rssi)
{
    /* RSSI dBm = 21 - (RSSI2 & 63) - T[(RSSI2 >> 8) & 63] (HW §3 0x138) with T not transcribed: a bigger fine part
     * or gain index is taken to mean a weaker signal [I] */
    return 255u - ((rssi & 63u) + (rssi >> 8 & 63u));
}

static uint32_t sc_metric(const struct ble_scan_tab *t, uint32_t i, int by_rssi)
{
    return by_rssi ? ble_scan_strength(t->e[i].rssi) : t->e[i].hits;
}

BLE_API uint32_t ble_scan_bars(const struct ble_scan_tab *t, uint32_t i)
{
    uint8_t idx[BLE_SCAN_N];
    uint32_t n = ble_scan_list(t, idx), k, lo = 0xFFFFFFFFu, hi = 0, m;
    int by_rssi = 1;
    if (i >= BLE_SCAN_N || !t->e[i].used || !t->e[i].midi)
        return 0;
    for (k = 0; k < n; k++)
        if (!t->e[idx[k]].rssi)
            by_rssi = 0;                 /* any without an RSSI word: all by how often they were heard */
    for (k = 0; k < n; k++) {
        m = sc_metric(t, idx[k], by_rssi);
        lo = m < lo ? m : lo;
        hi = m > hi ? m : hi;
    }
    if (hi == lo)
        return 3;
    m = sc_metric(t, i, by_rssi);
    return 1u + (2u * (m - lo) + (hi - lo) / 2u) / (hi - lo);
}
