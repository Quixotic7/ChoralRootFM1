/* SPDX-License-Identifier: GPL-3.0-only */
/* The scanner's software half (docs/BLE-DEVICES-DESIGN.md §2, docs/BLE-STACK.md §9.1; BLE-HW-FACTS.md §21.2): the AD structures of an
 * advertising report parsed (Core Specification Supplement Part A: flags, the 128-bit service UUID lists, the
 * shortened and complete local name), and the table of the devices found nearby, which the DEVICES list shows.
 *
 * Plain data, no radio: the link layer hands the raw reports over (ble_ll_scan_take, from the RX interrupt through a
 * ring) and the main loop feeds them here. Only advertisers that are connectable (ADV_IND) and carry the BLE-MIDI
 * service UUID (in the advertisement or in the scan response) are listed; an advertiser seen without it is kept
 * "unconfirmed" until its scan response says, and is the first to go when the table is full. */
#ifndef BLE_SCAN_H
#define BLE_SCAN_H
#include <stdint.h>
#include "ble_cfg.h"

#ifndef BLE_SCAN_N
#define BLE_SCAN_N 8u                    /* devices kept (listed or waiting for their scan response) */
#endif
#ifndef BLE_SCAN_AGE_MS
#define BLE_SCAN_AGE_MS 10000u           /* a listed device not heard for this long goes from the list */
#endif
#ifndef BLE_SCAN_AGE_UNCONF_MS
#define BLE_SCAN_AGE_UNCONF_MS 3000u     /* an unconfirmed one (no BLE-MIDI UUID seen yet) */
#endif
#define BLE_NAME_MAX 16u                 /* a name kept (the store's too), cut longer ones */

/* what one report's AD structures say */
struct ble_ad {
    uint8_t flags, has_flags;            /* AD type 0x01 */
    uint8_t midi;                        /* the BLE-MIDI service UUID in a 128-bit list (0x06 / 0x07) */
    uint8_t name_type;                   /* 0 none, 0x08 shortened, 0x09 complete */
    uint8_t name_len;
    const uint8_t *name;                 /* inside the report (not terminated) */
    uint8_t bad;                         /* a structure ran past the end (the rest ignored) */
};

enum { BLE_KIND_MIDI = 1, BLE_KIND_FM1 = 2 };   /* (the store's KIND values too) */

struct ble_found {
    uint8_t used, midi, kind, addr_rand;
    uint8_t addr[6];                     /* AdvA, least significant octet first */
    uint8_t name_type;                   /* the best name heard: 0x09 over 0x08 */
    char name[BLE_NAME_MAX + 1u];        /* Latin-1, terminated */
    uint16_t rssi;                       /* the engine's last RSSI word (HW §3 RSSI2), 0: none */
    uint8_t hits;                        /* reports heard lately (halved every age pass): the signal without RSSI */
    uint32_t seen_ms, order;             /* last heard; when first listed (the list's order) */
};

struct ble_scan_tab {
    struct ble_found e[BLE_SCAN_N];
    uint32_t next_order;
    uint32_t gen;                        /* moves whenever what the list shows changes (the menu redraws) */
    uint32_t reports, kept, dropped_full, ignored_type, bad_ad;   /* counters (blell) */
};

BLE_API void ble_ad_parse(const uint8_t *ad, uint32_t n, struct ble_ad *out);
BLE_API void ble_scan_clear(struct ble_scan_tab *t);
/* one advertising-channel PDU (header 2 + AdvA 6 + AdvData), its RSSI word (0: none), heard at now_ms */
BLE_API void ble_scan_add(struct ble_scan_tab *t, const uint8_t *pdu, uint32_t len, uint16_t rssi, uint32_t now_ms);
/* drop what was not heard for long; halve the hit counts (call every second or so) */
BLE_API void ble_scan_age(struct ble_scan_tab *t, uint32_t now_ms);
/* the listed devices (BLE-MIDI ones) in the list's order: their indexes into t->e -> how many */
BLE_API uint32_t ble_scan_list(const struct ble_scan_tab *t, uint8_t idx[BLE_SCAN_N]);
/* relative signal bars of t->e[i] among the listed ones: 3 the strongest .. 1 the weakest (0: not listed) */
BLE_API uint32_t ble_scan_bars(const struct ble_scan_tab *t, uint32_t i);
/* a strength from the RSSI word (larger is stronger), with no gain table (HW §3 0x138, U9: [I]) */
BLE_API uint32_t ble_scan_strength(uint16_t rssi);

#endif
