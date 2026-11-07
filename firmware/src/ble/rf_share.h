/* SPDX-License-Identifier: GPL-3.0-only */
/* The one radio, BLE's and BR/EDR's (Bluetooth MIDI and Bluetooth audio at the same time; docs/BT-CLASSIC.md §9.1).
 * Both stacks start it the first time they need it; whichever comes first runs the start-up, the other finds it up:
 *   rfs_rf_up(trims)   the radio's start-up (hal/fm1_ble.h fm1_ble_rf_init), once per boot -> 1 run now, 0 up already.
 *                      BLE (ble_hw_wl82.c ble_hw_wl82_start) and BR/EDR (bt/bt_inq.c bti_radio_up) both call it: a
 *                      second start-up under a live link would retune the radio under it
 *   rfs_le_ble(b, n)   BLE's baseband window (fm1_ble_bb_init with the driver's RAM): always, it is the LE block's owner;
 *                      after BR/EDR's stand-in window it takes the block over (that window carries no link)
 *   rfs_le_br(b, n)    BR/EDR's LE enable (stock V15 enables the LE block beside BR/EDR, BR §14): only while BLE has
 *                      not set its own window -> 1 done now. Over a live BLE it would move the window to a dummy and
 *                      clear BLE's interrupt enables (fm1_ble_bb_init's end), so it is skipped: the block is on already
 * Included by ble/ble_hw_wl82.c and bt/bt_inq.c after fm1_ble.h (the host tests: tests/bt_coex_test.c with counting
 * stand-ins). One byte of RAM. */
#ifndef FELUCCA_RF_SHARE_H
#define FELUCCA_RF_SHARE_H
enum { RFS_RF = 1u, RFS_LE_BLE = 2u, RFS_LE_BR = 4u };
static uint8_t rfs;                                     /* RFS_*: what has run this boot */

static int rfs_rf_up(const uint8_t *x106, const uint8_t *x107, const uint8_t *x108, const uint8_t *x187)
{
    if (rfs & RFS_RF)
        return 0;
    fm1_ble_rf_init(x106, x107, x108, x187);
    rfs |= RFS_RF;
    return 1;
}
static int rfs_is_up(void) { return (rfs & RFS_RF) != 0u; }
static void rfs_le_ble(uint32_t base, uint32_t size)
{
    fm1_ble_bb_init(base, size);
    rfs |= RFS_LE_BLE;
}
static int rfs_le_br(uint32_t base, uint32_t size)
{
    if (rfs & (RFS_LE_BLE | RFS_LE_BR))
        return 0;
    fm1_ble_bb_init(base, size);
    rfs |= RFS_LE_BR;
    return 1;
}
#endif
