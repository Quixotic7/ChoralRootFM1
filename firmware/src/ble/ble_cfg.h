/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE stack configuration (route C, docs/BLE-STACK.md). Every value can be set with -D; the defaults are the
 * FM-1 build's. Sizes are in bytes; times in the Bluetooth units named.
 *
 * Written from the Bluetooth Core Specification (v5.x, LE 1M only) and the MIDI Association's
 * "Specification for MIDI over Bluetooth Low Energy" 1.0. No vendor code. */
#ifndef BLE_CFG_H
#define BLE_CFG_H

#ifndef BLE_API
#define BLE_API                          /* the stack's functions: extern; static in the firmware's unity build */
#endif

/* ---- GAP */
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "Optimist"       /* the advertised name and GAP Device Name: "Optimist XXXX", XXXX the last four hex
                                          * digits of the device address (BLE_NAME_HEX; docs/BLE-DEVICES-DESIGN.md §1;
                                          * stock V15's was "FM-1_BLE", Optimist's before 2026-10-16 "FM-1 XXXX"; at most 24 characters; not "Optimist (Felucca)" as the
                                          * USB name: a scanned device's name is kept to BLE_NAME_MAX = 16 characters, which would cut the XXXX; the
                                          * editors match /felucca|optimist/i) */
#endif
#ifndef BLE_NAME_HEX
#define BLE_NAME_HEX 1                   /* 1: " XXXX" after BLE_DEVICE_NAME; 0: the name as it is */
#endif
#ifndef BLE_APPEARANCE
#define BLE_APPEARANCE 0x0000u           /* GAP Appearance: 0 = unknown (no category is claimed) */
#endif
#ifndef BLE_ADV_INTERVAL
#define BLE_ADV_INTERVAL 160u            /* x 0.625 ms = 100 ms (stock: 0xA0) */
#endif
#ifndef BLE_ADV_CHANNELS
#define BLE_ADV_CHANNELS 7u              /* bit 0: channel 37, bit 1: 38, bit 2: 39 */
#endif

/* ---- connection parameters the peripheral asks for once the MIDI notifications are on (stock: {6, 9, 0, 100},
 * then {12, 12, 0, 100} once). Interval x 1.25 ms, timeout x 10 ms. Also the GAP Peripheral Preferred
 * Connection Parameters characteristic. */
#ifndef BLE_CONN_MIN
#define BLE_CONN_MIN 6u
#endif
#ifndef BLE_CONN_MAX
#define BLE_CONN_MAX 9u
#endif
#ifndef BLE_CONN_LATENCY
#define BLE_CONN_LATENCY 0u
#endif
#ifndef BLE_CONN_TIMEOUT
#define BLE_CONN_TIMEOUT 100u
#endif
#ifndef BLE_CONN_RETRY_MIN
#define BLE_CONN_RETRY_MIN 12u           /* the second request after a reject (0 = no second request) */
#endif
#ifndef BLE_CONN_RETRY_MAX
#define BLE_CONN_RETRY_MAX 12u
#endif
/* while Bluetooth audio streams (ble_host.c ble_sig_audio): 30..45 ms, 2 s (Apple's rules: min >= 15 ms, max >= min +
 * 15 ms, timeout 2..6 s). At 7.5..11.25 ms a connection event, which wins the radio over the audio's 3.75 ms packets
 * (bt/bt_inq.c BTI_ACL_PRIO), leaves room for about one audio packet between two: the stream's 115 a second with no
 * margin, crackles on the XM4 with the screen mirror on (2026-10-24) */
#ifndef BLE_CONN_AUDIO_MIN
#define BLE_CONN_AUDIO_MIN 24u
#endif
#ifndef BLE_CONN_AUDIO_MAX
#define BLE_CONN_AUDIO_MAX 36u
#endif
#ifndef BLE_CONN_AUDIO_TIMEOUT
#define BLE_CONN_AUDIO_TIMEOUT 200u
#endif
#ifndef BLE_SIG_AUDIO_RETRY_US
#define BLE_SIG_AUDIO_RETRY_US 5000000u  /* the link still fast while the audio streams: asked again this long after */
#endif

/* ---- link layer */
#ifndef BLE_LL_MAX_OCTETS
#define BLE_LL_MAX_OCTETS 251u           /* largest LL payload sent or received (27..251; 27 = no data length ext.) */
#endif
#ifndef BLE_LL_TX_RING
#define BLE_LL_TX_RING 1024u             /* queued L2CAP frames (bytes, a power of two) */
#endif
#ifndef BLE_LL_CTRL_Q
#define BLE_LL_CTRL_Q 4u                 /* queued LL control PDUs */
#endif
#ifndef BLE_LL_VERSION
#define BLE_LL_VERSION 9u                /* VersNr: 9 = Core 5.0 */
#endif
#ifndef BLE_LL_COMPANY
#define BLE_LL_COMPANY 0xFFFFu           /* CompId: 0xFFFF, the value for no assigned company identifier */
#endif
#ifndef BLE_LL_SUBVERSION
#define BLE_LL_SUBVERSION 0x0001u
#endif
#ifndef BLE_LL_PERIPH_FEAT
#define BLE_LL_PERIPH_FEAT 1             /* start the feature exchange ourselves when the central has not */
#endif
#ifndef BLE_CENTRAL
#if defined(FELUCCA_BLE_CENTRAL)
#define BLE_CENTRAL FELUCCA_BLE_CENTRAL  /* builder item BLE_CENTRAL */
#else
#define BLE_CENTRAL 0                    /* 1 or 3: the central role (docs/BLE-DEVICES-DESIGN.md): the scanner for the
                                          * DEVICES list, connecting out to a BLE-MIDI peripheral (initiator, master
                                          * link, GATT client, SMP initiator) and reconnecting to LAST. One link,
                                          * time-sliced with advertising. Level 1 brings bonding (BLE_LL_ENC, SMP)
                                          * with it (its levels: below) */
#endif
#endif
/* BLE_CENTRAL's level (the builder's choice, docs/BLE-DEVICES-DESIGN.md §4.5; `#if BLE_CENTRAL`: any level):
 *   1 FULL         pairing as initiator with Just Works or a passkey we display (an iPhone app that needs MITM), the
 *                  pairing again with the passkey on the same link, LAST found behind a resolvable private address;
 *   3 MINIMAL      no pairing and no encryption from the central role: no SMP initiator, no LL encryption as master, no
 *                  bonds; a peer that asks for security is left ("FAILED: NEEDS PAIRING"). Controllers and FM-1 to
 *                  FM-1 need none. BLE_BOND (the peripheral's pairing as responder) stays as it is at every level
 * (2 was a Just Works only level, removed: it saved 2.2 KB over FULL; the builder refuses it) */
#if BLE_CENTRAL < 0 || BLE_CENTRAL > 3 || BLE_CENTRAL == 2
#error "BLE_CENTRAL: 0 off, 1 full, 3 minimal"
#endif
#ifndef BLE_CEN_PAIR
#define BLE_CEN_PAIR (BLE_CENTRAL == 1)  /* the central pairs (SMP initiator, LL encryption as master, passkey, RPA search) */
#endif
#if (defined(FELUCCA_BLE_BOND) && FELUCCA_BLE_BOND) || BLE_CEN_PAIR
/* builder item BLE_BOND: bonding (SMP Just Works + LL encryption); BLE_CENTRAL level 1 needs it too: Apple's
 * BLE-MIDI peripherals (QA1831) and many controllers ask the central to pair, so connecting out pairs as initiator */
#ifndef BLE_LL_ENC
#define BLE_LL_ENC 1
#endif
#ifndef BLE_SMP_LEGACY
#define BLE_SMP_LEGACY 1
#endif
#endif
#ifndef BLE_LL_ENC
#define BLE_LL_ENC 0                     /* LL encryption (needs keys: a pairing method in ble_smp.c); 0 = refused */
#endif

/* ---- security (ble_smp.c) */
#ifndef BLE_SMP_LEGACY
#define BLE_SMP_LEGACY 0                 /* LE legacy pairing, Just Works, with bonding (needs BLE_LL_ENC); 0: a Pairing
                                          * Request gets Pairing Failed, Pairing Not Supported */
#endif
#ifndef BLE_SMP_SEC_REQ
#define BLE_SMP_SEC_REQ 0                /* send an SMP Security Request (bonding) when a connection starts, as stock V15
                                          * is reported to; Apple's Accessory Design Guidelines (58.10) advise against */
#endif
#ifndef BLE_MIDI_NEED_ENC
#define BLE_MIDI_NEED_ENC 0              /* the MIDI I/O value and its CCCD need an encrypted link: Insufficient
                                          * Authentication (0x05) until then, the way Apple's guidelines (58.10) ask a
                                          * peripheral to start pairing */
#endif
#if BLE_SMP_LEGACY && !BLE_LL_ENC
#error "BLE_SMP_LEGACY needs BLE_LL_ENC=1"
#endif
#if (BLE_SMP_SEC_REQ || BLE_MIDI_NEED_ENC) && !BLE_SMP_LEGACY
#error "BLE_SMP_SEC_REQ / BLE_MIDI_NEED_ENC need BLE_SMP_LEGACY=1"
#endif
#if BLE_CEN_PAIR && !BLE_SMP_LEGACY
#error "BLE_CENTRAL 1 (the central pairs) needs BLE_SMP_LEGACY=1"
#endif

/* ---- central role, part 1: scanning for the DEVICES list (docs/BLE-STACK.md §9.1, HW §21.2) */
#ifndef BLE_SCAN_INTERVAL
#define BLE_SCAN_INTERVAL 64u            /* x 0.625 ms = 40 ms: the channel moves on 37 -> 38 -> 39 every interval */
#endif
#ifndef BLE_SCAN_WINDOW
#define BLE_SCAN_WINDOW 60u              /* x 0.625 ms: window + 4 <= interval (the vendor's rule, HW §21.2 step 3) */
#endif
#ifndef BLE_SCAN_ACTIVE
#define BLE_SCAN_ACTIVE 1                /* 1: SCAN_REQ (sent by the engine, with the Core's backoff) to get the names
                                          * most controllers put in their scan response; 0: passive */
#endif
#ifndef BLE_SCAN_RING
#define BLE_SCAN_RING 16u                /* raw reports between the RX interrupt and the main loop (a power of two): a
                                          * burst is ADV_IND + SCAN_RSP on 3 channels; 8 lost 4 reports in
                                          * tests/ble_emu_test.py once the boot dinghy shifted the run's timeline */
#endif
#if BLE_CENTRAL && (BLE_SCAN_RING & (BLE_SCAN_RING - 1u))
#error "BLE_SCAN_RING: a power of two"
#endif

/* ---- central role, part 2: connecting out (docs/BLE-STACK.md §9.1, HW §21.3 / §21.4). Our CONNECT_IND:
 * a random access address (Core Vol 6 Part B 2.1.2) and a random CRCInit (the vendor's fixed 0x1983AE is not used),
 * WinSize 2 and WinOffset random in [Interval / 2, Interval - 1] as the vendor (§21.3), Hop random 5..16, all 37
 * channels, and these: */
#ifndef BLE_CENTRAL_INTERVAL
#define BLE_CENTRAL_INTERVAL 9u          /* x 1.25 ms = 11.25 ms: the top of the 7.5..11.25 ms our peripheral asks for
                                          * (BLE_CONN_MIN / MAX, as stock V15 asks); one PDU pair per event each way is
                                          * ~89 BLE-MIDI packets a second, each holding many messages; 7.5 ms would add
                                          * a third more event and RX interrupts beside the audio while the cost of the
                                          * software AES-CCM in them is unmeasured on the FM-1 (design P1 risks) */
#endif
#ifndef BLE_CENTRAL_TIMEOUT
#define BLE_CENTRAL_TIMEOUT 200u         /* x 10 ms = 2 s supervision timeout */
#endif
#ifndef BLE_CENTRAL_WINSIZE
#define BLE_CENTRAL_WINSIZE 2u           /* x 1.25 ms: the transmit window (the vendor's, §21.3) */
#endif
#ifndef BLE_CENTRAL_SCA
#define BLE_CENTRAL_SCA 0u               /* 251-500 ppm, as the vendor's CONNECT_IND (§21.6): the FM-1's sleep clock
                                          * accuracy is not measured, and claiming the worst only widens the peer's
                                          * window by a few us at 11.25 ms */
#endif
#ifndef BLE_INIT_INTERVAL
#define BLE_INIT_INTERVAL 64u            /* initiating windows: as scanning (x 0.625 ms), the channel moved each */
#endif
#ifndef BLE_INIT_WINDOW
#define BLE_INIT_WINDOW 60u
#endif
#if BLE_CENTRAL_INTERVAL < 6u || BLE_CENTRAL_INTERVAL > 3200u || BLE_CENTRAL_TIMEOUT * 4u <= BLE_CENTRAL_INTERVAL
#error "BLE_CENTRAL_INTERVAL: 6..3200, and BLE_CENTRAL_TIMEOUT x 10 ms more than two intervals"
#endif
#ifndef BLE_GATTC_TIMEOUT_US
#define BLE_GATTC_TIMEOUT_US 30000000u   /* ATT (Core Vol 3 Part F 3.3.3) and SMP (Part H 3.4) transaction timeout */
#endif

/* ---- host */
#ifndef BLE_ATT_MTU_MAX
#define BLE_ATT_MTU_MAX 247u             /* our ATT receive MTU: 247 fills one 251-byte LL PDU (23..517) */
#endif
#ifndef BLE_SC_ON_SUBSCRIBE
#define BLE_SC_ON_SUBSCRIBE 0            /* 1: indicate Service Changed (0x0001..0xFFFF) whenever a client subscribes to
                                          * it. Off: on the FM-1 (blell10-12, 2026-10-09) the Mac confirmed that
                                          * indication and then never discovered the MIDI characteristic; the database
                                          * never changes while we run, so the indication is never due (§5) */
#endif
#ifndef BLE_MIDI_RUNNING_STATUS
#define BLE_MIDI_RUNNING_STATUS 1        /* the encoder drops a repeated status byte inside a packet */
#endif
#ifndef BLE_MIDI_PER_EVENT
#define BLE_MIDI_PER_EVENT 2u            /* notifications queued per connection event at most */
#endif

#if BLE_LL_MAX_OCTETS < 27u || BLE_LL_MAX_OCTETS > 251u
#error "BLE_LL_MAX_OCTETS: 27..251"
#endif
#if BLE_ATT_MTU_MAX < 23u || BLE_ATT_MTU_MAX > 517u
#error "BLE_ATT_MTU_MAX: 23..517"
#endif
#if (BLE_LL_TX_RING & (BLE_LL_TX_RING - 1u)) || BLE_LL_TX_RING < 2u * (BLE_ATT_MTU_MAX + 4u + 2u)
#error "BLE_LL_TX_RING: a power of two, room for two full ATT frames"
#endif

#endif
