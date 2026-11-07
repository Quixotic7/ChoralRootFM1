/* SPDX-License-Identifier: GPL-3.0-only */
/* Little helpers shared by the BLE stack: little-endian fields (Bluetooth sends the least significant octet
 * first) and byte copies. No C library: the firmware builds freestanding (-fno-builtin). */
#ifndef BLE_UTIL_H
#define BLE_UTIL_H
#include <stdint.h>

static inline uint16_t ble_rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t ble_rd24(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
static inline uint32_t ble_rd32(const uint8_t *p) { return ble_rd24(p) | (uint32_t)p[3] << 24; }
static inline void ble_wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void ble_cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    while (n--)
        *d++ = *s++;
}
static inline void ble_zero(uint8_t *d, uint32_t n)
{
    while (n--)
        *d++ = 0;
}
static inline int ble_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}
/* a ring's slot written / read before its index moves (one producer interrupt, one consumer: the compiler's order
 * is enough on the one core both run on) */
#ifndef BLE_BARRIER
#define BLE_BARRIER() __asm__ volatile("" ::: "memory")
#endif
static inline uint32_t ble_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

#endif
