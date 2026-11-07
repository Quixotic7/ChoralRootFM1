/* SPDX-License-Identifier: GPL-3.0-only */
/* The one remembered device (docs/BLE-DEVICES-DESIGN.md §3, with the user's ruling of 2026-10-09: LAST is the device
 * the FM-1 itself connected to, as a central, and reconnects to; a Mac or phone that connects to the FM-1 is accepted
 * as always and never becomes LAST). 70 octets at the end of the settings record (storage/project.c persist_t.ble_dev,
 * the CRC-checked settings object: never the SDK's VM area 0xE7000-0xE9FFF), plus the DEVICES choice NONE / LAST.
 *
 * Plain data: the firmware keeps the raw octets with the settings and decodes them once at start-up (ble_store_load);
 * an older record (zeros), a foreign one or a damaged one reads as "nothing remembered, NONE". */
#ifndef BLE_STORE_H
#define BLE_STORE_H
#include <stdint.h>
#include "ble_cfg.h"
#include "ble_scan.h"                    /* BLE_NAME_MAX, BLE_KIND_* */

#define BLE_DEV_MARK 0xB6u               /* a valid store */
#define BLE_DEV_VER 1u
#define BLE_DEV_STORE_SIZE 70u           /* header 4 + entry 66 */
enum { BLE_SEL_NONE = 0, BLE_SEL_LAST = 1 };
/* struct ble_dev.info */
#define BLE_DEV_RANDOM 0x01u             /* addr is a random (static) address; else public */
#define BLE_DEV_CENTRAL 0x02u            /* we connected to it (role C): always set for LAST (the ruling) */
#define BLE_DEV_BONDED 0x04u             /* ltk / rand / ediv valid */
#define BLE_DEV_IRK 0x08u                /* irk valid: find it behind a resolvable private address */
#define BLE_DEV_KIND_SHIFT 4u            /* bits 4-5: BLE_KIND_MIDI / BLE_KIND_FM1 */
#define BLE_DEV_USED 0x80u

struct ble_dev {                         /* 66 octets, multi-octet fields least significant first */
    uint8_t addr[6];                     /* its identity address once bonded, else its AdvA */
    uint8_t info;
    char name[BLE_NAME_MAX];             /* NUL-padded (not terminated when 16 long) */
    uint8_t ltk[16];                     /* the bond (its keys, taken as initiator: the next round) */
    uint8_t rand[8];
    uint8_t ediv[2];
    uint8_t irk[16];
    uint8_t sec;                         /* BLE_DEV_SEC_* (0 in an older record: nothing known) */
};
/* struct ble_dev.sec */
#define BLE_DEV_SEC_AUTH 0x01u           /* the bond is authenticated (a passkey pairing) */
#define BLE_DEV_SEC_MITM 0x02u           /* it needs an authenticated link: pair with a passkey straight away */
struct ble_dev_store {
    uint8_t mark, ver, sel, rsv;
    struct ble_dev dev;
};
_Static_assert(sizeof(struct ble_dev_store) == BLE_DEV_STORE_SIZE, "the device store is 70 octets");

BLE_API void ble_store_reset(struct ble_dev_store *s);
/* from the settings record's octets (zeros: an older record) -> 1 something valid was there */
BLE_API int ble_store_load(struct ble_dev_store *s, const uint8_t raw[BLE_DEV_STORE_SIZE]);
BLE_API void ble_store_save(const struct ble_dev_store *s, uint8_t raw[BLE_DEV_STORE_SIZE]);
BLE_API int ble_store_has_last(const struct ble_dev_store *s);
/* the device we connected to becomes LAST (the one entry, replacing any other; its bond kept only when it is the same
 * device again) */
BLE_API void ble_store_set_last(struct ble_dev_store *s, const uint8_t addr[6], uint8_t addr_rand, const char *name,
                                uint8_t kind);
/* its bond; auth: from a passkey pairing (BLE_DEV_SEC_AUTH) */
BLE_API void ble_store_set_bond(struct ble_dev_store *s, const uint8_t ltk[16], const uint8_t rand[8], uint16_t ediv,
                                int auth);
/* it needs an authenticated link (learned from its refusal after Just Works): kept until FORGET or another LAST */
BLE_API void ble_store_set_mitm(struct ble_dev_store *s);
/* its bond refused by it (KEY: it forgot it, or never kept it): the next link pairs afresh */
BLE_API void ble_store_drop_bond(struct ble_dev_store *s);
/* its IRK and identity address (SMP Identity Information / Identity Address Information) */
BLE_API void ble_store_set_id(struct ble_dev_store *s, const uint8_t irk[16], const uint8_t id[6], uint8_t id_rand);
BLE_API void ble_store_select(struct ble_dev_store *s, uint8_t sel);
/* FORGET: the entry and its keys go, NONE is picked */
BLE_API void ble_store_forget(struct ble_dev_store *s);
/* an address as shown when there is no name: "C4:7F:..:A1:B2" (14 characters, terminated) */
BLE_API void ble_addr_text(const uint8_t a[6], char out[15]);
/* LAST's name as shown (terminated), else its address (ble_addr_text) */
BLE_API void ble_store_name(const struct ble_dev_store *s, char out[BLE_NAME_MAX + 1u]);

#endif
