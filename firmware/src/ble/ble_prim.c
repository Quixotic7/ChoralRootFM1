/* SPDX-License-Identifier: GPL-3.0-only */
/* Link-layer primitives (ble_prim.h), bit-serial as the Core Specification draws them (Vol 6 Part B): small, and
 * only on a path the baseband engine does not cover. */
#include "ble_prim.h"

BLE_API uint32_t ble_crc24(uint32_t init, const uint8_t *p, uint32_t n)
{
    uint32_t reg = init & 0xFFFFFFu;
    while (n--) {
        uint32_t b = *p++, i;
        for (i = 0; i < 8u; i++, b >>= 1) {      /* each octet least significant bit first */
            uint32_t fb = ((reg >> 23) ^ b) & 1u;
            reg = (reg << 1) & 0xFFFFFFu;
            if (fb)
                reg ^= 0x00065Bu;                /* x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1 */
        }
    }
    return reg;
}

BLE_API void ble_crc24_air(uint32_t crc, uint8_t out[3])
{
    uint32_t i, r = 0;
    for (i = 0; i < 24u; i++)                    /* position 23 goes first: reverse the 24 bits */
        r |= ((crc >> i) & 1u) << (23u - i);
    out[0] = (uint8_t)r;
    out[1] = (uint8_t)(r >> 8);
    out[2] = (uint8_t)(r >> 16);
}

BLE_API void ble_whiten(uint8_t ch, uint8_t *p, uint32_t n)
{
    uint32_t reg = 1u, i;                        /* position 0 = 1, positions 1..6 = channel index MSB..LSB */
    for (i = 0; i < 6u; i++)
        reg |= ((ch >> (5u - i)) & 1u) << (i + 1u);
    while (n--) {
        uint32_t b = *p, o = 0;
        for (i = 0; i < 8u; i++) {
            uint32_t r6 = (reg >> 6) & 1u;
            o |= (((b >> i) & 1u) ^ r6) << i;
            reg = (reg << 1) & 0x7Fu;
            if (r6)
                reg ^= 0x11u;                    /* x^7 + x^4 + 1: position 0 and position 4 take position 6 */
        }
        *p++ = (uint8_t)o;
    }
}

BLE_API int ble_chmap_has(const uint8_t chm[5], uint8_t ch)
{
    return ch < 37u && (chm[ch >> 3] >> (ch & 7u)) & 1u;
}

BLE_API int ble_chmap_set(struct ble_chmap *m, const uint8_t chm[5])
{
    uint8_t ch;
    m->n = 0;
    for (ch = 0; ch < 37u; ch++)
        if (ble_chmap_has(chm, ch))
            m->used[m->n++] = ch;
    return m->n >= 2u;
}

BLE_API uint8_t ble_csa1_next(uint8_t *last, uint8_t hop, const uint8_t chm[5], const struct ble_chmap *m)
{
    uint8_t un = (uint8_t)((*last + hop) % 37u);
    *last = un;
    if (ble_chmap_has(chm, un) || !m->n)
        return un;
    return m->used[un % m->n];                   /* remapping index into the used channels, ascending */
}

BLE_API int ble_aa_valid(uint32_t aa)
{
    uint32_t i, run = 1, trans = 0, d = aa ^ BLE_ADV_AA;
    if (aa == BLE_ADV_AA || !(d & (d - 1u)))     /* not the advertising AA, nor one bit away from it */
        return 0;
    if ((aa & 0xFFu) == (aa >> 8 & 0xFFu) && (aa & 0xFFu) == (aa >> 16 & 0xFFu) && (aa & 0xFFu) == aa >> 24)
        return 0;                                /* four equal octets */
    for (i = 1; i < 32u; i++) {
        if (((aa >> i) ^ (aa >> (i - 1u))) & 1u) {
            trans++;
            run = 1;
        } else if (++run > 6u)
            return 0;                            /* more than six equal bits in a row */
    }
    if (trans > 24u)
        return 0;
    for (i = 27, trans = 0; i < 32u; i++)        /* the six most significant bits: at least two transitions */
        trans += ((aa >> i) ^ (aa >> (i - 1u))) & 1u;
    return trans >= 2u;
}
