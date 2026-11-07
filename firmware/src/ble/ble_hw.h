/* SPDX-License-Identifier: GPL-3.0-only */
/* The baseband driver's interface: what the link layer (ble_ll.c) asks of the radio, and what the driver
 * reports back. Designed for a link-controller engine that does the timing of each event in hardware:
 *   advertising on 37 / 38 / 39 with an automatic SCAN_RSP; anchors, the transmit window, CSA #1 hopping, the
 *   access-address match, CRC (software gives the CRC init), SN / NESN with acknowledgement and retransmission
 *   from two alternating TX buffers (and two RX buffers), the connection event counter and the instant compare.
 * The link layer keeps the queues, the control procedures, the supervision timeout and the encryption.
 *
 * Context: every ble_ll_hw_* call comes from one context, the driver's BLE interrupts (the event IRQ and the
 * RX IRQ at one priority, never nested in each other); every ble_hw_* call is made from inside one of them,
 * except ble_hw_adv_start() at start-up (ble_enable() with the BLE IRQs not yet on, or masked).
 *
 * PDUs are passed as they are on air without access address and CRC: the 2-octet header (octet 0: type / LLID
 * and flags, octet 1: length) followed by the payload. Multi-octet fields inside are least significant first.
 *
 * How this maps onto the AC791N engine (docs/BLE-HW-FACTS.md, sections in brackets; the driver's job):
 *   - one link (n = 0): its 324-byte control block and the TX / RX payload buffers in the baseband RAM block
 *     (3, 4); columns through the port at 0x2801C / 0x28020 (2.3); IRQ 45 = event, IRQ 29 = RX (8, 10);
 *   - ble_hw_adv_start: ADV_IND into TX buffer 0, SCAN_RSP into TX buffer 1 (the engine answers SCAN_REQ by
 *     itself), interval in 625 us slots in columns 1 / 15, state 2 (6). Our header octet 0 is the Core layout
 *     (TxAdd bit 6); TXAHDR wants TxAdd in bit 4: the driver moves it;
 *   - ble_ll_hw_connect_ind is called from the RX IRQ that holds the CONNECT_IND, and ble_hw_conn_start is
 *     called inside it: the same link goes from state 2 to 7 before the IRQ returns, with the window widening
 *     from interval and SCA, and the first anchor 1.25 ms + WinOffset after the CONNECT_IND (7). The link
 *     layer does nothing slow on that path (the host only resets its state);
 *   - ble_ll_hw_tx fills the free fixed TX buffer: TXDHDR = length << 8 | MD << 3 | LLID, keeping the engine's
 *     bit 2; our MD is header bit 4 (Core layout); INTFRAME bit 6 = MD; TXBUFnCNTL bit 0 cleared last (8.2). At the
 *     end of each connection RX IRQ, a buffer we loaded whose bit 0 reads 1 again (empty) is ble_ll_hw_tx_acked,
 *     then ble_ll_hw_tx for the next (8.2);
 *   - ble_ll_hw_rx: the header rebuilt from RXDHDRn (Core layout already), the payload from the RX buffer; packets
 *     with RXSTAT [3:0] != 1 are not passed on (8);
 *   - ble_ll_hw_event_end from IRQ 45 with column 3 - 1 as the counter (8);
 *   - ble_hw_conn_update / ble_hw_chmap_update: column 5 = instant at once; the new window, widening, interval
 *     (columns 1, 2, 4, 15) or the channel tables are written in the event IRQ of event instant - 1 (9);
 *   - the supervision timeout is software (7): the link layer's, from ble_hw_time_us;
 *   - ble_hw_addr: the stored BLE address (VM id 104, public in stock: TxAdd 0) (11). */
#ifndef BLE_HW_H
#define BLE_HW_H
#include <stdint.h>
#include "ble_cfg.h"

/* ---- the link layer asks (the driver implements these) ---- */

struct ble_hw_adv {
    const uint8_t *adv;          /* ADV_IND PDU (header + AdvA + AdvData), 8..39 octets */
    uint8_t adv_len;
    const uint8_t *scan_rsp;     /* SCAN_RSP PDU (header + AdvA + ScanRspData): sent by the engine to a SCAN_REQ
                                  * whose AdvA is ours */
    uint8_t scan_rsp_len;
    uint16_t interval;           /* advInterval x 0.625 ms (32..16384); the engine adds advDelay (0..10 ms) */
    uint8_t channels;            /* bit 0: 37, bit 1: 38, bit 2: 39 */
};
/* start (or restart with new PDUs) connectable undirected advertising. A CONNECT_IND to our AdvA is handed
 * to ble_ll_hw_connect_ind(); the driver keeps advertising until ble_hw_conn_start() or ble_hw_adv_stop(). */
BLE_API void ble_hw_adv_start(const struct ble_hw_adv *a);
BLE_API void ble_hw_adv_stop(void);

struct ble_hw_conn {
    uint32_t aa;                 /* access address */
    uint32_t crc_init;           /* 24 bits */
    uint8_t win_size;            /* transmitWindowSize x 1.25 ms */
    uint16_t win_offset;         /* transmitWindowOffset x 1.25 ms */
    uint16_t interval;           /* connInterval x 1.25 ms (6..3200) */
    uint16_t latency;            /* connPeripheralLatency: the link layer always passes 0 (listen every event) */
    uint16_t timeout;            /* connSupervisionTimeout x 10 ms (for the driver's information) */
    uint8_t chm[5];              /* channel map, 37 bits */
    uint8_t hop;                 /* hopIncrement 5..16, Channel Selection Algorithm #1 */
    uint8_t sca;                 /* the central's sleep clock accuracy (0..7): widening the receive window */
};
/* called from inside ble_ll_hw_connect_ind(), before it returns: the engine leaves advertising and listens
 * for the first packet in the transmit window 1.25 ms + win_offset after the end of the CONNECT_IND it
 * received. Event counter 0 is that first event. */
BLE_API void ble_hw_conn_start(const struct ble_hw_conn *c);
/* leave the connection now (the engine stops; no more ble_ll_hw_* calls for it) */
BLE_API void ble_hw_conn_stop(void);

/* (as master, HW §21.4: the same call; the driver writes the master's values at instant - 1) */
struct ble_hw_conn_upd {
    uint8_t win_size;
    uint16_t win_offset;
    uint16_t interval;
    uint16_t latency;            /* (0 again) */
    uint16_t timeout;
    uint16_t instant;            /* the event counter value from which the new parameters apply */
};
/* LL_CONNECTION_UPDATE_IND: the engine moves to the new parameters at the instant (the old interval ends at
 * the instant's old anchor, then the transmit window starts win_offset later) */
BLE_API void ble_hw_conn_update(const struct ble_hw_conn_upd *u);
/* LL_CHANNEL_MAP_IND: the new map from the event whose counter is instant on */
BLE_API void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant);
/* the data length in use (LL_LENGTH_REQ / RSP): the largest payload either side sends from now on, 27..251 */
BLE_API void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx);
/* new data queued: a free TX buffer can be filled now (ble_ll_hw_tx), or at the next event */
BLE_API void ble_hw_tx_kick(void);

#if BLE_CENTRAL
/* scanning (HW §21.2): the same link in state 1, on 37 -> 38 -> 39 (software moves the channel in each event
 * interrupt), the RX interrupt's reports handed to ble_ll_hw_adv_report(). One link only: advertising is stopped
 * first (ble_hw_adv_stop) and started again after ble_hw_scan_stop (docs/BLE-DEVICES-DESIGN.md §1, docs/BLE-STACK.md §9.1). */
struct ble_hw_scan {
    uint16_t interval, window;   /* x 0.625 ms; window + 4 <= interval */
    uint8_t active;              /* 1: the engine sends SCAN_REQ on an ADV_IND (FORMAT bit8 0), with the Core's backoff */
    uint8_t own_rand;            /* our address type (the SCAN_REQ's TxAdd) */
    const uint8_t *own;          /* our address (the SCAN_REQ's ScanA), 6 octets */
};
BLE_API void ble_hw_scan_start(const struct ble_hw_scan *s);
BLE_API void ble_hw_scan_stop(void);
/* an advertising-channel PDU received while scanning (header 2 + AdvA + AdvData, CRC good), the engine's RSSI word
 * (0: none) and the channel (37..39). RX interrupt: copy it and return */
BLE_API void ble_ll_hw_adv_report(const uint8_t *pdu, uint8_t len, uint16_t rssi, uint8_t ch);

/* initiating (HW §21.3): the same link in state 3 with our CONNECT_IND complete in both TX buffers before it starts;
 * the engine sends it by itself T_IFS after an ADV_IND (or an ADV_DIRECT_IND to us) from the one target address
 * (WHITELIST0 / TARGETADR, FILTERCNTL bits 0 / 4 / 8). The driver sees the target's ADV_IND in the RX interrupt and,
 * in the event interrupt after it, turns the link into the master of the connection (state 6, the anchor counter
 * 2 x WinOffset + 4 slots), then calls ble_ll_hw_master_start(); from there the link is a connection like a
 * peripheral's (ble_ll_hw_rx / tx / tx_acked / event_end, ble_hw_conn_update, ...). Advertising is stopped first
 * (one link, ble_hw_adv_stop / ble_hw_scan_stop) */
struct ble_hw_init {
    uint16_t interval, window;   /* x 0.625 ms, as scanning */
    const uint8_t *cind;         /* the CONNECT_IND PDU: header (type 5, TxAdd, RxAdd) + InitA + AdvA + LLData, 36 */
    struct ble_hw_conn conn;     /* its LLData, as sent (the master's set-up) */
};
BLE_API void ble_hw_init_start(const struct ble_hw_init *i);
BLE_API void ble_hw_init_stop(void);
/* the event interrupt: the link is the master of the connection our CONNECT_IND made (event counter 0 is its first
 * anchor). The link layer starts its connection state here */
BLE_API void ble_ll_hw_master_start(void);
#endif

/* the device address (least significant octet first): 0 public, 1 random static */
BLE_API uint8_t ble_hw_addr(uint8_t addr[6]);
/* a free-running microsecond clock (wraps at 2^32) */
BLE_API uint32_t ble_hw_time_us(void);
/* n random octets (the encryption's SKDs and IVs: BLE_LL_ENC only) */
BLE_API void ble_hw_rand(uint8_t *out, uint8_t n);

/* ---- the driver reports (the link layer implements these) ---- */

/* a CONNECT_IND to our AdvA (pdu: header + 34 octets). Returns 1 when the link layer took the connection
 * (ble_hw_conn_start() was called), 0 when it was ignored (invalid parameters): advertising goes on. */
BLE_API int ble_ll_hw_connect_ind(const uint8_t *pdu, uint8_t len);
/* a new data-channel PDU received in the current event: CRC good, not a retransmission (the engine's SN
 * check), empty PDUs may be left out. pdu[0] carries LLID, NESN, SN, MD as received. */
BLE_API void ble_ll_hw_rx(const uint8_t *pdu, uint8_t len);
/* a TX buffer is free: the link layer writes the next PDU into pdu (room for 2 + 255 octets:
 * 251 and an encrypted PDU's MIC) and returns its
 * length (2 + payload), or 0 when there is nothing to send (the engine sends an empty PDU). The link layer
 * sets LLID and MD (more queued); the engine sets NESN and SN. The PDU is final: the engine retransmits it as
 * it is until it is acknowledged. */
BLE_API uint8_t ble_ll_hw_tx(uint8_t *pdu);
/* 1 when ble_ll_hw_tx() would return a PDU now (control or data queued); nothing is taken */
BLE_API int ble_ll_hw_tx_pending(void);
/* the oldest PDU returned by ble_ll_hw_tx() and not yet reported was acknowledged by the central */
BLE_API void ble_ll_hw_tx_acked(void);
/* a connection event closed: counter = its connEventCounter; rx_ok = at least one packet with a good CRC was
 * received in it. Called for every event, also the ones where nothing was received. */
BLE_API void ble_ll_hw_event_end(uint16_t counter, uint8_t rx_ok);

#endif
