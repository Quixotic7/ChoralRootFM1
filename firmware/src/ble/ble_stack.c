/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* The BLE stack as part of the firmware's unity build (felucca.c, FELUCCA_BLE=1): every stack function is static
 * here (BLE_API), so what this build never calls is left out (the app is linked without --gc-sections).
 * A baseband driver built as a unit of its own would need BLE_API empty instead. */
#ifndef BLE_API
#define BLE_API static
#endif
#include "ble_prim.c"
#if BLE_LL_ENC
#include "ble_aes.c"
#endif
#include "ble_midi.c"
#include "ble_ll.c"
#include "ble_smp.c"
#include "ble_host.c"
#include "ble_att.c"
#include "ble_store.c"              /* the one remembered device (the DEVICES list's LAST) */
#if BLE_CENTRAL
#include "ble_scan.c"               /* the AD parser and the nearby devices' table */
#include "ble_gattc.c"              /* the ATT client: the peripheral's BLE-MIDI characteristic */
#include "ble_central.c"            /* connecting out: security, the client, the end of it */
#endif
#if defined(FELUCCA_BLE_HW)
#define BLE_HW_WL82 0               /* the build brings its own driver */
#elif defined(FELUCCA_BLE_STUB) && FELUCCA_BLE_STUB
#define BLE_HW_WL82 0
#include "ble_hw_stub.c"            /* a stand-in that never advertises (the stack alone) */
#else
#define BLE_HW_WL82 1
#include "ble_hw_wl82.c"            /* the AC791N / WL82 baseband engine (hal/fm1_ble.h, docs/BLE-STACK.md §7) */
#endif
