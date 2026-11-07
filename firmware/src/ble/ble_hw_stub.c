/* SPDX-License-Identifier: GPL-3.0-only */
/* A stand-in for the baseband driver (ble_hw.h): it accepts every request and never calls back, so the stack links,
 * sits idle in advertising and costs no time. FELUCCA_BLE_STUB=1 builds it instead of the real driver
 * (ble_hw_wl82.c); FELUCCA_BLE_HW leaves both out for a build that brings its own. */
#include "ble_hw.h"

BLE_API void ble_hw_adv_start(const struct ble_hw_adv *a) { (void)a; }
BLE_API void ble_hw_adv_stop(void) {}
BLE_API void ble_hw_conn_start(const struct ble_hw_conn *c) { (void)c; }
BLE_API void ble_hw_conn_stop(void) {}
BLE_API void ble_hw_conn_update(const struct ble_hw_conn_upd *u) { (void)u; }
BLE_API void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant)
{
    (void)chm;
    (void)instant;
}
BLE_API void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx)
{
    (void)max_tx;
    (void)max_rx;
}
BLE_API void ble_hw_tx_kick(void) {}
BLE_API uint8_t ble_hw_addr(uint8_t addr[6])
{
    static const uint8_t a[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0xC0};   /* random static (top bits 11): a placeholder */
    uint32_t i;
    for (i = 0; i < 6u; i++)
        addr[i] = a[i];
    return 1;
}
BLE_API uint32_t ble_hw_time_us(void) { return 0; }
BLE_API uint32_t ble_hw_diag_now(void) { return 0; }
BLE_API void ble_hw_rand(uint8_t *out, uint8_t n)
{
    while (n--)
        *out++ = 0;
}
#if BLE_CENTRAL
BLE_API void ble_hw_scan_start(const struct ble_hw_scan *s) { (void)s; }
BLE_API void ble_hw_scan_stop(void) {}
BLE_API void ble_hw_init_start(const struct ble_hw_init *i) { (void)i; }
BLE_API void ble_hw_init_stop(void) {}
#endif
