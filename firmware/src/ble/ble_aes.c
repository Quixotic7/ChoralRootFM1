/* SPDX-License-Identifier: GPL-3.0-only */
/* AES-128 encryption (FIPS-197) for size: the round keys are made on the fly, one S-box, no T-tables. CCM as the
 * LL uses it (Core Specification Vol 6 Part E 2): M = 4 (MIC), L = 2, a 13-octet nonce of the 39-bit packet
 * counter, the direction bit and the IV; one octet of additional data, the PDU header with NESN, SN and MD
 * cleared. */
#include "ble_aes.h"
#include "ble_util.h"

#ifndef BLE_AES_HW
static const uint8_t ble_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static uint8_t ble_xt(uint32_t x) { return (uint8_t)(x << 1 ^ (x >> 7) * 0x1Bu); }

BLE_API void ble_aes128(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], k[16], t[16];
    uint32_t i, r, rc = 1;
    for (i = 0; i < 16u; i++) {
        k[i] = key[i];
        s[i] = (uint8_t)(in[i] ^ key[i]);
    }
    for (r = 1; r <= 10u; r++) {
        for (i = 0; i < 16u; i++)                /* SubBytes and ShiftRows (column-major: row = i & 3) */
            t[i] = ble_sbox[s[(i + 4u * (i & 3u)) & 15u]];
        for (i = 0; i < 16u; i += 4) {           /* MixColumns (not in the last round) */
            uint8_t a0 = t[i], a1 = t[i + 1], a2 = t[i + 2], a3 = t[i + 3], x = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
            if (r == 10u) {
                s[i] = a0, s[i + 1] = a1, s[i + 2] = a2, s[i + 3] = a3;
                continue;
            }
            s[i] = (uint8_t)(a0 ^ x ^ ble_xt(a0 ^ a1));
            s[i + 1] = (uint8_t)(a1 ^ x ^ ble_xt(a1 ^ a2));
            s[i + 2] = (uint8_t)(a2 ^ x ^ ble_xt(a2 ^ a3));
            s[i + 3] = (uint8_t)(a3 ^ x ^ ble_xt(a3 ^ a0));
        }
        k[0] ^= (uint8_t)(ble_sbox[k[13]] ^ rc); /* the next round key */
        k[1] ^= ble_sbox[k[14]];
        k[2] ^= ble_sbox[k[15]];
        k[3] ^= ble_sbox[k[12]];
        for (i = 4; i < 16u; i++)
            k[i] ^= k[i - 4u];
        rc = ble_xt(rc);
        for (i = 0; i < 16u; i++)
            s[i] ^= k[i];
    }
    ble_cpy(out, s, 16);
}
#endif

/* block 0 of the counter (A_i, flags 0x01) or MAC (B_0, flags 0x49) sequence */
static void ble_ccm_block(const struct ble_ccm *c, uint8_t flags, uint32_t tail, uint8_t b[16])
{
    b[0] = flags;
    b[1] = (uint8_t)c->ctr;
    b[2] = (uint8_t)(c->ctr >> 8);
    b[3] = (uint8_t)(c->ctr >> 16);
    b[4] = (uint8_t)(c->ctr >> 24);
    b[5] = (uint8_t)((c->ctr_hi & 0x7Fu) | (uint32_t)c->dir << 7);
    ble_cpy(b + 6, c->iv, 8);
    b[14] = (uint8_t)(tail >> 8);                /* the length (B_0) or the block counter (A_i), big-endian */
    b[15] = (uint8_t)tail;
}

/* the MIC over plain text p (len octets): T xor S_0 */
static void ble_ccm_mic(const struct ble_ccm *c, uint8_t hdr, const uint8_t *p, uint32_t len, uint8_t mic[4])
{
    uint8_t x[16], b[16];
    uint32_t i, j;
    ble_ccm_block(c, 0x49u, len, b);
    ble_aes128(c->key, b, x);
    x[0] ^= 0x00;                                /* B_1: the additional data's length (1) and the header */
    x[1] ^= 0x01;
    x[2] ^= (uint8_t)(hdr & 0xE3u);
    ble_aes128(c->key, x, x);
    for (i = 0; i < len; i += 16) {
        for (j = 0; j < 16u && i + j < len; j++)
            x[j] ^= p[i + j];
        ble_aes128(c->key, x, x);
    }
    ble_ccm_block(c, 0x01u, 0, b);
    ble_aes128(c->key, b, b);
    for (i = 0; i < 4u; i++)
        mic[i] = (uint8_t)(x[i] ^ b[i]);
}

static void ble_ccm_ctr(const struct ble_ccm *c, uint8_t *p, uint32_t len)
{
    uint8_t s[16];
    uint32_t i, j;
    for (i = 0; i < len; i += 16) {
        ble_ccm_block(c, 0x01u, i / 16u + 1u, s);
        ble_aes128(c->key, s, s);
        for (j = 0; j < 16u && i + j < len; j++)
            p[i + j] ^= s[j];
    }
}

static void ble_ccm_step(struct ble_ccm *c)
{
    if (!++c->ctr)
        c->ctr_hi = (uint8_t)((c->ctr_hi + 1u) & 0x7Fu);
}

BLE_API void ble_ccm_encrypt(struct ble_ccm *c, uint8_t hdr, uint8_t *p, uint8_t len)
{
    ble_ccm_mic(c, hdr, p, len, p + len);
    ble_ccm_ctr(c, p, len);
    ble_ccm_step(c);
}

BLE_API int ble_ccm_decrypt(struct ble_ccm *c, uint8_t hdr, uint8_t *p, uint8_t len)
{
    uint8_t mic[4];
    if (len < 5u)
        return 0;
    len = (uint8_t)(len - 4u);
    ble_ccm_ctr(c, p, len);
    ble_ccm_mic(c, hdr, p, len, mic);
    if (!ble_eq(mic, p + len, 4)) {
        ble_ccm_ctr(c, p, len);                  /* leave the cipher text as it came */
        return 0;
    }
    ble_ccm_step(c);
    return 1;
}

#if BLE_SMP_LEGACY
/* e() on values as SMP sends them, least significant octet first */
static void ble_e_le(const uint8_t k[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t kb[16], ib[16], ob[16];
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        kb[i] = k[15u - i];
        ib[i] = in[15u - i];
    }
    ble_aes128(kb, ib, ob);
    for (i = 0; i < 16u; i++)
        out[i] = ob[15u - i];
}

/* the confirm value function c1 (Core Vol 3 Part H 2.2.3), everything least significant octet first:
 * p1 = pres || preq || rat' || iat', p2 = padding || ia || ra, c1 = e(k, e(k, r ^ p1) ^ p2) */
BLE_API void ble_smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
                        uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16])
{
    uint8_t p[16], x[16];
    uint32_t i;
    p[0] = iat;
    p[1] = rat;
    ble_cpy(p + 2, preq, 7);
    ble_cpy(p + 9, pres, 7);
    for (i = 0; i < 16u; i++)
        x[i] = (uint8_t)(r[i] ^ p[i]);
    ble_e_le(k, x, x);
    ble_cpy(p, ra, 6);
    ble_cpy(p + 6, ia, 6);
    ble_zero(p + 12, 4);
    for (i = 0; i < 16u; i++)
        x[i] ^= p[i];
    ble_e_le(k, x, out);
}

/* the key generation function s1 (Core Vol 3 Part H 2.2.4): e(k, r1' || r2'), r1' and r2' the low 64 bits */
BLE_API void ble_smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16])
{
    uint8_t x[16];
    ble_cpy(x, r2, 8);
    ble_cpy(x + 8, r1, 8);
    ble_e_le(k, x, out);
}

BLE_API void ble_smp_passkey_tk(uint32_t passkey, uint8_t tk[16])
{
    ble_zero(tk, 16);
    tk[0] = (uint8_t)passkey;
    tk[1] = (uint8_t)(passkey >> 8);
    tk[2] = (uint8_t)(passkey >> 16);
}
#endif
