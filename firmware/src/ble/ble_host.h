/* SPDX-License-Identifier: GPL-3.0-only */
/* Inside the host: L2CAP (ble_host.c: fixed channels, LE signalling, SMP, GAP start-up) and the ATT server with
 * its GATT database (ble_att.c). */
#ifndef BLE_HOST_H
#define BLE_HOST_H
#include <stdint.h>
#include "ble_cfg.h"

enum { L2CAP_CID_ATT = 4, L2CAP_CID_SIG = 5, L2CAP_CID_SMP = 6 };

/* the BLE-MIDI service UUID 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, least significant octet first */
static const uint8_t BLE_UUID_MIDI_SVC[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                              0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};

/* the frames ble_att.c may queue: a response must always find room, so notifications leave this much */
#define BLE_ATT_RESERVE (BLE_ATT_MTU_MAX + 8u)

/* ble_host.c: the GAP Device Name (also the advertised name), set by ble_init -> its length */
BLE_API uint8_t ble_gap_name(const uint8_t **p);

/* ble_att.c */
BLE_API void ble_att_rx(const uint8_t *p, uint16_t n);  /* an ATT PDU from the client */
BLE_API void ble_att_reset(void);                       /* a new connection, or none */
BLE_API void ble_att_event(void);                       /* each connection event: indications, MIDI out */

BLE_API void ble_att_set_mtu(uint16_t client_mtu);     /* the other side's MTU (the smaller one is used) */
BLE_API uint16_t ble_att_mtu(void);
BLE_API void ble_att_midi_packet(const uint8_t *v, uint16_t n);   /* a BLE-MIDI packet in (a write, a notification) */

#if BLE_CENTRAL
/* ble_gattc.c: the ATT client of the central role (finds the BLE-MIDI characteristic, subscribes) */
BLE_API void ble_gattc_reset(void);
BLE_API void ble_gattc_start(void);
BLE_API void ble_gattc_retry(void);                     /* the link is encrypted: the request that asked again */
BLE_API int ble_gattc_rx(const uint8_t *p, uint16_t n); /* 1: a PDU for the client (responses, notifications) */
BLE_API uint16_t ble_gattc_midi(void);                  /* the MIDI I/O value handle once subscribed, else 0 */
BLE_API void ble_gattc_event(uint32_t now);
/* ble_central.c: the host's side of the central role */
BLE_API void ble_central_connected(void);
BLE_API void ble_central_disconnected(uint8_t reason);
BLE_API void ble_central_event(void);
BLE_API uint8_t ble_central_sig(const uint8_t *d, uint16_t n);   /* a Connection Parameter Update Request -> result */
#if !BLE_CEN_PAIR
/* BLE_CENTRAL 3: the peripheral asked for security (0: its SMP Security Request; else the ATT error): the link is left */
BLE_API void ble_central_no_pairing(uint8_t code);
#endif
#endif

/* ble_host.c: ask the central for BLE_CONN_MIN..MAX (L2CAP Connection Parameter Update Request), once per
 * connection, unless the interval in use is in that range already */
BLE_API void ble_sig_want_fast(void);
/* ble_host.c: Bluetooth audio streams (1) or not (0): the slower interval BLE_CONN_AUDIO_MIN..MAX asked for meanwhile */
BLE_API void ble_sig_audio(int on);
#if BLE_LL_ENC
/* the key the link layer may use (LTK least significant octet first, for Rand and EDIV) */
BLE_API void ble_host_set_key(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]);
/* 1 once after a central asked to encrypt with a key we do not know (it was refused Key Missing): forget us on it */
BLE_API int ble_host_key_refused(void);
#if BLE_SMP_LEGACY
/* a key to hand out in a pairing, good from now on when that peer connects to us: EDIV (2), Rand (8), LTK cut to size */
BLE_API void ble_host_new_key(uint8_t size, uint8_t ediv_rand[10], uint8_t ltk[16]);
#endif
#endif

/* ble_smp.c: an SMP PDU from the central; a new connection / none */
BLE_API void ble_smp_rx(const uint8_t *p, uint16_t n);
BLE_API void ble_smp_connected(void);
BLE_API void ble_smp_reset(void);
#if BLE_SMP_LEGACY
/* the short-term key while a pairing waits for the encryption (EDIV 0, Rand 0): 1 given, 0 none */
BLE_API int ble_smp_stk(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16]);
#endif

#endif
