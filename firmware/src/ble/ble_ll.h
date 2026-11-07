/* SPDX-License-Identifier: GPL-3.0-only */
/* The link layer (one connection, LE 1M; the peripheral, and with BLE_CENTRAL the master): its side towards the
 * host. The driver side is ble_hw.h. Everything runs in the BLE interrupts' context (ble_hw.h): the host's
 * callbacks below are called from there, and the host calls ble_ll_send() etc. from there too. */
#ifndef BLE_LL_H
#define BLE_LL_H
#include <stdint.h>
#include "ble_cfg.h"

/* LL control opcodes (Core Vol 6 Part B 2.4.2) */
enum {
    LL_CONNECTION_UPDATE_IND = 0x00, LL_CHANNEL_MAP_IND = 0x01, LL_TERMINATE_IND = 0x02, LL_ENC_REQ = 0x03,
    LL_ENC_RSP = 0x04, LL_START_ENC_REQ = 0x05, LL_START_ENC_RSP = 0x06, LL_UNKNOWN_RSP = 0x07,
    LL_FEATURE_REQ = 0x08, LL_FEATURE_RSP = 0x09, LL_PAUSE_ENC_REQ = 0x0A, LL_PAUSE_ENC_RSP = 0x0B,
    LL_VERSION_IND = 0x0C, LL_REJECT_IND = 0x0D, LL_PERIPHERAL_FEATURE_REQ = 0x0E, LL_CONNECTION_PARAM_REQ = 0x0F,
    LL_CONNECTION_PARAM_RSP = 0x10, LL_REJECT_EXT_IND = 0x11, LL_PING_REQ = 0x12, LL_PING_RSP = 0x13,
    LL_LENGTH_REQ = 0x14, LL_LENGTH_RSP = 0x15, LL_PHY_REQ = 0x16, LL_PHY_RSP = 0x17, LL_PHY_UPDATE_IND = 0x18,
    LL_MIN_USED_CHANNELS_IND = 0x19
};
/* error codes (Core Vol 1 Part F) the link layer uses */
enum {
    BLE_ERR_UNKNOWN_CMD = 0x01, BLE_ERR_PIN_KEY_MISSING = 0x06, BLE_ERR_CONN_TIMEOUT = 0x08,
    BLE_ERR_REMOTE_USER = 0x13, BLE_ERR_LOCAL_HOST = 0x16, BLE_ERR_UNSUPP_REMOTE = 0x1A,
    BLE_ERR_INVALID_LL_PARAMS = 0x1E, BLE_ERR_LL_RSP_TIMEOUT = 0x22, BLE_ERR_INSTANT_PASSED = 0x28,
    BLE_ERR_UNSPECIFIED = 0x1F, BLE_ERR_UNACCEPT_PARAMS = 0x3B, BLE_ERR_MIC_FAILURE = 0x3D, BLE_ERR_CONN_FAILED = 0x3E
};
/* feature bits we may claim (FeatureSet, octet 0 bits) */
#define BLE_FEAT_ENC (1u << 0)
#define BLE_FEAT_CONN_PARAM (1u << 1)
#define BLE_FEAT_EXT_REJECT (1u << 2)
#define BLE_FEAT_PERIPH_FEAT (1u << 3)
#define BLE_FEAT_PING (1u << 4)
#define BLE_FEAT_DLE (1u << 5)

/* ---- the host asks ---- */
BLE_API void ble_ll_init(const uint8_t addr[6], uint8_t addr_random);   /* addr least significant octet first */
/* the AD structures of the advertisement and of the scan response (each at most 31 octets) */
BLE_API void ble_ll_set_adv_data(const uint8_t *ad, uint8_t ad_len, const uint8_t *sr, uint8_t sr_len);
/* advertise whenever not connected (1), or stop advertising and leave any connection (0) */
BLE_API void ble_ll_enable(int on);
BLE_API void ble_ll_advertise(int on);                    /* Melodee: 0 = no advertising when idle (called like ble_ll_enable) */
BLE_API int ble_ll_connected(void);
/* queue one L2CAP basic frame for channel cid (payload n octets): 1 queued, 0 no room (nothing queued) */
BLE_API int ble_ll_send(uint16_t cid, const uint8_t *p, uint16_t n);
/* the payload octets a frame queued now could carry (0 when not connected) */
BLE_API uint32_t ble_ll_tx_room(void);
/* leave the connection: LL_TERMINATE_IND with reason, then ble_host_disconnected(BLE_ERR_LOCAL_HOST) */
BLE_API void ble_ll_disconnect(uint8_t reason);
BLE_API uint16_t ble_ll_interval(void);                   /* connInterval in use (x 1.25 ms), 0 when not connected */
BLE_API uint16_t ble_ll_timeout(void);                    /* the supervision timeout in use (x 10 ms), 0 when not connected */
BLE_API uint8_t ble_ll_peer_features(void);               /* the central's FeatureSet octet 0 (0 until exchanged) */
BLE_API uint8_t ble_ll_tx_octets(void);                   /* the data length we send (27 .. 251), 0 when not connected */
BLE_API uint16_t ble_ll_peer_len(uint32_t i);             /* the central's last LL_LENGTH values: 0 MaxRxOctets, 1 MaxRxTime (us),
                                                           * 2 MaxTxOctets, 3 MaxTxTime; 0 before one came */
/* our address and the central's (its CONNECT_IND's InitA), least significant octet first; bit 0: ours is random,
 * bit 1: the central's is random (TxAdd) */
BLE_API uint8_t ble_ll_addrs(uint8_t own[6], uint8_t peer[6]);
BLE_API int ble_ll_encrypted(void);                       /* the link is encrypted both ways (0 without BLE_LL_ENC) */
#if BLE_CENTRAL
/* scan instead of advertising while on (the DEVICES list is open), when enabled and not connected; a connection
 * that ends while on goes back to scanning. Off: advertising again (when enabled). Called like ble_ll_enable */
BLE_API void ble_ll_scan(int on);
BLE_API int ble_ll_scanning(void);
/* the main loop: the oldest raw report (pdu: room for 2 + 37) -> its length, 0 when none; *rssi its RSSI word */
BLE_API uint8_t ble_ll_scan_take(uint8_t *pdu, uint16_t *rssi);
/* connect to a peripheral (its AdvA and TxAdd) as central: initiate until it advertises, then master of the link
 * (ble_host_connected; ble_ll_central() 1). 0: not now (off or connected). Called like ble_ll_enable */
BLE_API int ble_ll_connect(const uint8_t peer[6], uint8_t peer_rand);
BLE_API void ble_ll_connect_cancel(void);        /* stop initiating: advertise (or scan) again */
BLE_API int ble_ll_initiating(void);
BLE_API int ble_ll_central(void);                 /* connected, and we are the master */
/* as master: a connection update to an interval in [imin, imax] (ours when it fits) / a channel map update, each at
 * an instant (HW §21.4) -> 1 started now */
BLE_API int ble_ll_conn_update(uint16_t imin, uint16_t imax, uint16_t timeout);
BLE_API int ble_ll_chmap_update(const uint8_t chm[5]);
#if BLE_CEN_PAIR
/* as master: start the encryption with this key (EDIV / Rand: 0 for a pairing's STK) */
BLE_API void ble_ll_start_enc(const uint8_t ltk[16], const uint8_t rand[8], uint16_t ediv);
#endif
#endif

/* ---- the link layer tells the host (ble_l2cap.c) ---- */
BLE_API void ble_host_connected(void);
BLE_API void ble_host_disconnected(uint8_t reason);
BLE_API int ble_host_audio(void);              /* Bluetooth audio streams: a central's faster interval refused */
/* a data PDU's payload: start = 1 for the start of an L2CAP frame (LLID 10b), 0 for a continuation */
BLE_API void ble_host_rx(const uint8_t *p, uint8_t len, uint8_t start);
/* once per connection event, after the link layer's own work: the host may queue frames */
BLE_API void ble_host_event(void);
#if BLE_LL_ENC
/* the LTK (least significant octet first) for Rand and EDIV from LL_ENC_REQ: 1 found, 0 none */
BLE_API int ble_host_ltk(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16]);
/* the encryption started (our LL_START_ENC_RSP queued: the link is encrypted both ways from here) */
BLE_API void ble_host_encrypted(void);
#if BLE_CEN_PAIR
/* as master: the peripheral refused our LL_ENC_REQ (err: PIN or Key Missing 0x06 when it lost the bond) */
BLE_API void ble_host_enc_failed(uint8_t err);
#endif
#endif

#endif
