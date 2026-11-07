/* SPDX-License-Identifier: GPL-3.0-only */
/* The BLE-MIDI peripheral as the firmware sees it (route C: our own stack, docs/BLE-STACK.md).
 *
 *   ble_init(addr, random)     once at start, before the driver's interrupts are on
 *   ble_enable(on)             advertise as BLE_DEVICE_NAME with the BLE-MIDI service / stop and disconnect
 *   ble_midi_ready()           a central is connected and has turned the MIDI notifications on
 *
 * The firmware gives the stack its MIDI through four functions (io/midi/midi_ble.c, or a test's fakes). All
 * are called from the BLE interrupts' context (ble_hw.h); they must only touch single-producer rings. */
#ifndef BLE_H
#define BLE_H
#include <stdint.h>
#include "ble_cfg.h"

BLE_API void ble_init(const uint8_t addr[6], uint8_t addr_random);
BLE_API void ble_enable(int on);
BLE_API int ble_midi_ready(void);
BLE_API int ble_connected(void);

/* ---- the firmware implements these ---- */
/* the oldest MIDI event waiting to go out (a USB-MIDI event packet and the millisecond it happened): 1, or 0
 * when there is none. The stack takes it with ble_app_midi_pop() once it is in a notification. */
BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms);
BLE_API void ble_app_midi_pop(void);
/* one message received (a USB-MIDI event packet), with its 13-bit timestamp and the last timestamp of the
 * BLE packet it came in (the newest: the difference is how long before the packet's end it happened) */
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts);
/* a central connected (1) or left (0); its MIDI notifications on or off (ble_midi_ready()) */
BLE_API void ble_app_state(void);
#if BLE_SMP_LEGACY
/* a central bonded: its key (Rand, EDIV, LTK least significant octet first) to keep across power-offs and give back
 * with ble_host_set_key (ble_host.h) after ble_init. BLE interrupts' context: copy it and save it later */
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]);
/* the bonded peer's identity (SMP Identity Information + Identity Address Information): its IRK and identity address
 * (least significant octet first; addr_rand 1 random static, 0 public). BLE interrupts' context: copy it */
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand);
#endif

#if BLE_CENTRAL
/* ---- connecting out as central (BLE_CENTRAL, docs/BLE-DEVICES-DESIGN.md §4; ble_central.c) */
struct ble_peer {
    uint8_t addr[6], addr_rand;          /* its AdvA as heard now (least significant octet first; an RPA stays one) */
    uint8_t bonded;                      /* ltk / rand / ediv are its bond: the link is encrypted with them */
    uint8_t sec;                         /* BLE_PEER_*: what is known of its security */
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
};
#define BLE_PEER_AUTH 1u                 /* the bond is authenticated (a passkey pairing: MITM protection) */
#define BLE_PEER_MITM 2u                 /* it needs an authenticated link: a pairing goes straight to the passkey */
/* the keys a pairing as initiator took from the peripheral (Core Vol 3 Part H 3.6) */
#define BLE_KEYS_LTK 1u                  /* ltk / rand / ediv: the bond (Encryption Information, Master Identification) */
#define BLE_KEYS_ID 2u                   /* irk / id / id_rand: its identity (Identity Information + Address) */
#define BLE_KEYS_AUTH 4u                 /* the pairing was authenticated (passkey entry): the LTK has MITM protection */
struct ble_keys {
    uint8_t has;                         /* BLE_KEYS_* */
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
    uint8_t irk[16], id[6], id_rand;
};
enum { BLE_CS_IDLE, BLE_CS_CONNECTING, BLE_CS_SETUP, BLE_CS_READY };   /* ble_central_state */
enum { BLE_CF_NONE, BLE_CF_LOST, BLE_CF_NO_MIDI, BLE_CF_PAIRING, BLE_CF_AUTH, BLE_CF_GATT,
       BLE_CF_NEED_MITM, BLE_CF_KEY, BLE_CF_NEED_PAIR };   /* ble_central_fail; NEED_MITM: it needs a passkey and
                              * refused the pairing with MITM on the link (Just Works refused before): connect again
                              * with BLE_PEER_MITM, without the unauthenticated bond; KEY: it left while our encryption
                              * with p's bond was starting (MIC Failure 0x3D, Key Missing 0x06: the bond is no good to
                              * it); NEED_PAIR (BLE_CENTRAL 3): it asked for security (a Security Request, Insufficient Authentication / Encryption),
                              * and this level does not pair */
/* initiate to p (advertising or scanning stop: one link) -> 1 started, 0 not now (off, or a link is up). From there:
 * connected as master, the BLE-MIDI characteristic found and subscribed (pairing or encrypting with p's bond when the
 * peripheral asks, or wants it), MIDI both ways: BLE_CS_READY. Called like ble_enable (the BLE interrupts held) */
BLE_API int ble_central_connect(const struct ble_peer *p);
BLE_API void ble_central_cancel(void);   /* stop initiating, or leave our central link (a peripheral link stays) */
BLE_API uint8_t ble_central_state(void);
BLE_API uint8_t ble_central_fail(void);  /* why the last connection out ended before / after READY (BLE_CF_*) */
BLE_API uint8_t ble_central_code(void);  /* ... its code: the LL reason, the ATT error or the SMP reason */
#if BLE_CEN_PAIR
BLE_API int ble_central_pairing(void);   /* 1: a pairing of ours runs on the link */
BLE_API int ble_central_prompted(void);  /* 1: a pairing of ours started on the last attempt's link (a phone asked its
                                          * user): the firmware does not make that link again by itself */
BLE_API int ble_central_mitm(void);      /* 1: the last attempt's peer needs an authenticated link (a passkey) */
#endif
/* the passkey to show while a passkey pairing waits for the peer's user (Core Vol 3 Part H 2.3.5.3: we display, the
 * phone's user types it), 0..999999; BLE_NO_PASSKEY when there is none (always at BLE_CENTRAL 3) */
#define BLE_NO_PASSKEY 0xFFFFFFFFu
#if BLE_CEN_PAIR
BLE_API uint32_t ble_central_passkey(void);
/* 1: addr is a resolvable private address made from irk (ah, Core Vol 3 Part H 2.2.2; least significant first) */
BLE_API int ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6]);
/* the firmware implements: a pairing as initiator ended with these keys of the peripheral (BLE interrupts' context:
 * copy them) */
BLE_API void ble_app_central_keys(const struct ble_keys *k);
#endif
#endif

#endif
