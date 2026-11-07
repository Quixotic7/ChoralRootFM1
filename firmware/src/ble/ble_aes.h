/* SPDX-License-Identifier: GPL-3.0-only */
/* AES-128 (FIPS-197, encryption only) and the LL's AES-CCM (Core Specification Vol 6 Part E, sample data in
 * Vol 6 Part C 1). Byte order: AES's own (the Bluetooth security function e() with its most significant octet
 * first); the LL turns the least-significant-first fields it receives around before calling it.
 *
 * Replacing the software AES with the chip's AES block: build with BLE_AES_HW=1 and give the driver a
 * ble_aes128() with this signature; ble_aes.c then leaves its own out. CCM stays here, on top of it. */
#ifndef BLE_AES_H
#define BLE_AES_H
#include <stdint.h>
#include "ble_cfg.h"

BLE_API void ble_aes128(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/* one direction of an encrypted link */
struct ble_ccm {
    uint8_t key[16];                     /* the session key SK, most significant octet first */
    uint8_t iv[8];                       /* IV = IVm || IVs as the nonce takes it: IVm's octets (least significant
                                          * first) then IVs's */
    uint32_t ctr;                        /* packetCounter, low 32 bits */
    uint8_t ctr_hi;                      /* ... and bits 32..38 */
    uint8_t dir;                         /* directionBit: 1 central -> peripheral, 0 peripheral -> central */
};
/* p: the payload (len octets, 1..251 - 4); the MIC goes after it. The counter steps on. */
BLE_API void ble_ccm_encrypt(struct ble_ccm *c, uint8_t hdr, uint8_t *p, uint8_t len);
/* p: payload and MIC (len octets, 5..255); 1 if the MIC is right (p then holds len - 4 octets of plain text,
 * and the counter steps on), 0 if not */
BLE_API int ble_ccm_decrypt(struct ble_ccm *c, uint8_t hdr, uint8_t *p, uint8_t len);

#if BLE_SMP_LEGACY
/* LE legacy pairing's c1 and s1 (Core Vol 3 Part H 2.2.3, 2.2.4); every value least significant octet first, as SMP
 * sends it; preq / pres: the Pairing Request / Response PDUs (opcode first); iat / rat: 1 for a random address */
BLE_API void ble_smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
                        uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16]);
BLE_API void ble_smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]);
/* passkey entry's TK (2.3.5.3): the passkey 0..999999 as a 128-bit value, least significant octet first */
BLE_API void ble_smp_passkey_tk(uint32_t passkey, uint8_t tk[16]);
#endif

#endif
