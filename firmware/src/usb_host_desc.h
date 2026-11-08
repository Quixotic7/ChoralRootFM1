/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Retains USB host descriptor parsing contributed by recursivefieldinstruments (2026). */
/* USB host: what Melodee reads from an attached device, with no register access (usb_host.c uses it; the host
 * tests include it alone). ChoralRoot (HISTORY 3y260): Melodee 3fe570d's file, unchanged.
 *   uh_desc_feed    a configuration descriptor, fed as it arrives -> the first MIDI 1.0 streaming alternate
 *                   setting with usable IN and/or OUT endpoints (an output-only synth is usable too)
 *   uh_parse_midi   the same walk over a descriptor already in memory
 *   uh_midi_ok      one USB-MIDI event packet -> 1 when it is a channel message or a clock / transport byte */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t found;               /* a usable endpoint of a MIDI streaming interface was found */
    uint8_t cfg;                 /* bConfigurationValue */
    uint8_t iface, alt;          /* its interface and alternate setting */
    uint8_t in_ep;               /* endpoint number (without the direction bit) */
    uint8_t in_type;             /* 2 bulk, 3 interrupt */
    uint8_t in_interval;         /* bInterval (interrupt only) */
    uint16_t in_mps;
    uint8_t out_ep;              /* the OUT endpoint of the same interface, 0 = none */
    uint8_t out_type, out_interval;
    uint16_t out_mps;
} uh_midi_t;

/* The configuration descriptor walked as it arrives, one EP0 packet at a time, so its length is not bounded by a
 * buffer: an audio+MIDI device lists its audio interfaces first and the MIDI streaming one last. Only the first
 * nine bytes of each descriptor are kept; a descriptor cut short at the end is ignored. */
typedef struct {
    uh_midi_t *m;
    uint8_t hd[9];               /* the first bytes of the descriptor being read */
    uint8_t pos, len;            /* its bytes seen so far, its bLength */
    uint8_t cfg, in_midi, done;  /* done: nothing more to find (the next interface, or a zero-length descriptor) */
    uint8_t hub;                 /* an interface of class 9: a hub, whatever bDeviceClass says */
    uint8_t if_cls, if_sub, nif; /* the first interface's class and subclass, the interfaces seen (diagnostics) */
} uh_desc_t;

static void uh_desc_start(uh_desc_t *w, uh_midi_t *m)
{
    *w = (uh_desc_t){.m = m};
    *m = (uh_midi_t){0};
}

/* one whole descriptor, its first nine bytes (or fewer) in w->hd */
static void uh_desc_one(uh_desc_t *w)
{
    const uint8_t *d = w->hd;
    uint32_t l = w->len;
    uh_midi_t *m = w->m;
    if (d[1] == 2u && l >= 9u) {
        w->cfg = d[5];
    } else if (d[1] == 4u && l >= 9u) {
        if (!w->nif++) {
            w->if_cls = d[5];
            w->if_sub = d[6];
        }
        if (d[5] == 9u)
            w->hub = 1;
        if (m->found) {                                 /* never combine interfaces or alternate settings */
            w->done = 1;
            return;
        }
        m->cfg = w->cfg;
        m->iface = d[2];
        m->alt = d[3];
        w->in_midi = d[5] == 1u && d[6] == 3u && d[7] == 0u;
    } else if (d[1] == 5u && l >= 7u && w->in_midi) {
        uint8_t ea = d[2], type = d[3] & 3u;
        uint16_t mps = (uint16_t)(d[4] | d[5] << 8);
        /* Full-speed USB-MIDI 1.0 events are four bytes. The buffers hold at most 64 bytes. */
        if ((ea & 15u) && !(ea & 0x70u) && mps >= 4u && mps <= 64u &&
            ((type == 2u && (mps == 8u || mps == 16u || mps == 32u || mps == 64u)) ||
             (type == 3u && d[6]))) {
            if ((ea & 0x80u) && !m->in_ep) {
                m->found = 1;
                m->in_ep = ea & 0x0Fu;
                m->in_type = type;
                m->in_interval = d[6];
                m->in_mps = mps;
            } else if (!(ea & 0x80u) && !m->out_ep) {
                m->found = 1;
                m->out_ep = ea & 0x0Fu;
                m->out_type = type;
                m->out_interval = d[6];
                m->out_mps = mps & ~3u;          /* send whole event packets only */
            }
        }
    }
}

/* the next n bytes of the configuration; 1 once the walk is done (the rest need not be read) */
static int uh_desc_feed(uh_desc_t *w, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && !w->done; i++) {
        if (!w->pos) {
            w->len = d[i];
            if (w->len < 2u) {
                w->done = 1;
                break;
            }
        }
        if (w->pos < sizeof w->hd)
            w->hd[w->pos] = d[i];
        if (++w->pos == w->len) {
            uh_desc_one(w);
            w->pos = 0;
        }
    }
    return w->done;
}

/* d / len: a whole configuration descriptor in memory (the host tests) */
static int uh_parse_midi(const uint8_t *d, uint32_t len, uh_midi_t *m)
{
    uh_desc_t w;
    uh_desc_start(&w, m);
    uh_desc_feed(&w, d, len);
    return m->found;
}

/* A USB-MIDI event packet: cable / CIN, then the MIDI bytes. Channel messages (CIN 8..E whose status matches)
 * and the one-byte clock, start, continue and stop are passed on; SysEx and everything else is not: a
 * controller has no business with the SysEx commands (update, editor) that usb.c's own parser answers. The
 * cable number is dropped. Returns the packet to enqueue, or 0. */
static uint32_t uh_midi_ok(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    pkt &= ~0xF0u;
    if (cin == 0xFu)
        return st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu ? pkt : 0u;
    if (cin == 3u)                                      /* Song Position */
        return st == 0xF2u && !(pkt & 0x80800000u) ? pkt : 0u;
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        ((cin == 0xCu || cin == 0xDu) || !(pkt & 0x80000000u)))
        return pkt;
    return 0u;
}
