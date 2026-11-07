/* SPDX-License-Identifier: GPL-3.0-only */
/* Link-layer primitives from the Core Specification, Vol 6 Part B (LE 1M):
 *   CRC24 (3.1.1), data whitening (3.2), Channel Selection Algorithm #1 with the channel map (4.5.8.2),
 *   access-address rules (2.1.2).
 * The FM-1's baseband does CRC, hopping and (as far as known) whitening itself: these are here for a driver
 * whose engine leaves one of them to software, for checks, and for the tests' simulated central. */
#ifndef BLE_PRIM_H
#define BLE_PRIM_H
#include <stdint.h>
#include "ble_cfg.h"

#define BLE_ADV_AA 0x8E89BED6u           /* the advertising channels' access address */
#define BLE_ADV_CRC_INIT 0x555555u

/* CRC over a PDU (header and payload, as sent): the register after the last bit (bit 0 = position 0) */
BLE_API uint32_t ble_crc24(uint32_t init, const uint8_t *p, uint32_t n);
/* the three CRC octets in the order they go on air (position 23 first, each octet least significant bit first) */
BLE_API void ble_crc24_air(uint32_t crc, uint8_t out[3]);

/* whitening (and de-whitening: the same) of n octets in place, for channel index ch (0..39) */
BLE_API void ble_whiten(uint8_t ch, uint8_t *p, uint32_t n);

/* the channel map: ChM as in CONNECT_IND / LL_CHANNEL_MAP_IND (37 bits, octet 0 bit 0 = channel 0) */
struct ble_chmap {
    uint8_t used[37];                    /* the used channels, ascending */
    uint8_t n;                           /* how many (a valid map has at least 2) */
};
BLE_API int ble_chmap_set(struct ble_chmap *m, const uint8_t chm[5]);   /* 1 valid (>= 2 used, bits 37..39 ignored) */
BLE_API int ble_chmap_has(const uint8_t chm[5], uint8_t ch);
/* Algorithm #1: the next event's channel; *last is lastUnmappedChannel (0 before the first event) */
BLE_API uint8_t ble_csa1_next(uint8_t *last, uint8_t hop, const uint8_t chm[5], const struct ble_chmap *m);

/* 1 if aa is a valid data-channel access address for LE 1M (2.1.2) */
BLE_API int ble_aa_valid(uint32_t aa);

#endif
