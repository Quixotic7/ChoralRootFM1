/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE-MIDI packets (MIDI Association, "Specification for MIDI over Bluetooth Low Energy" 1.0) to and from
 * USB-MIDI event packets, the firmware's router format: cin | status << 8 | data1 << 16 | data2 << 24
 * (cable 0; CIN 4 SysEx start / continue, 5 / 6 / 7 SysEx end with 1 / 2 / 3 octets or a single-octet
 * system common, 2 / 3 system common, 8..E channel messages, F a single octet: real time).
 *
 * A packet: header 10hhhhhh (timestamp bits 12..7), then per message a timestamp octet 1lllllll (bits 6..0)
 * and the message. Inside a packet a repeated status may be left out (running status), with or without its
 * own timestamp octet; real-time messages may come anywhere, SysEx included, each after a timestamp octet.
 * SysEx goes on over packets: a continuation packet holds the header and then data at once; the closing F7
 * has a timestamp octet before it. A low timestamp smaller than the one before it in the packet means the
 * high part went one up. Timestamps: 13 bits of milliseconds. */
#ifndef BLE_MIDI_H
#define BLE_MIDI_H
#include <stdint.h>
#include "ble_cfg.h"

/* ---- encoder: packs messages into one packet until it is full */
struct ble_midi_enc {
    uint8_t *buf;
    uint16_t cap, len;
    uint8_t hi, lo;                      /* the packet's high timestamp part, the last low part written */
    uint8_t rs;                          /* running status in this packet (0: none) */
    uint8_t sysex;                       /* a SysEx is open (it goes on in the next packet) */
};
/* a new packet in buf (cap octets: the ATT MTU - 3); an open SysEx stays open */
BLE_API void ble_midi_enc_begin(struct ble_midi_enc *e, uint8_t *buf, uint16_t cap);
/* add one USB-MIDI event at t_ms (any millisecond count: 13 bits are kept). Returns 1 added, 0 no room or its
 * time does not fit this packet (send it, begin a new one, add again), -1 not a message (dropped). Events must
 * come in time order. */
BLE_API int ble_midi_enc_add(struct ble_midi_enc *e, uint32_t pkt, uint32_t t_ms);
/* the packet's length, 0 if it holds nothing worth sending */
BLE_API uint16_t ble_midi_enc_len(const struct ble_midi_enc *e);
/* forget an open SysEx (a new connection) */
BLE_API void ble_midi_enc_reset(struct ble_midi_enc *e);

/* ---- decoder: one packet at a time, state kept between packets (SysEx, running status) */
typedef void (*ble_midi_sink)(void *ctx, uint32_t pkt, uint16_t ts);
struct ble_midi_dec {
    uint8_t rs, need, got, d[2];         /* running status; the message being read: data octets wanted / read */
    uint8_t cur;                         /* its status (0: none) */
    uint8_t sysex, sx_n, sx[3];          /* inside SysEx; octets waiting for a USB-MIDI packet */
};
BLE_API void ble_midi_dec_reset(struct ble_midi_dec *d);
/* decode one packet: every complete message goes to sink with its 13-bit timestamp. Returns 0 for a packet
 * without a valid header (ignored), else 1. */
BLE_API int ble_midi_dec(struct ble_midi_dec *d, const uint8_t *p, uint16_t n, ble_midi_sink sink, void *ctx);
/* the 13-bit timestamp of the last timestamp octet in a packet (the header's alone when there is none) */
BLE_API uint16_t ble_midi_last_ts(const uint8_t *p, uint16_t n);

#endif
