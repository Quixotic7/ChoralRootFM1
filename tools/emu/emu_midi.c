/* SPDX-License-Identifier: GPL-3.0-only */
/* CoreMIDI for the emulator: a virtual source "ChoralRoot FM-1" (the firmware's MIDI out, the packets
 * usb.c would send on EP1 IN) and a virtual destination "ChoralRoot FM-1 In" (bytes from a host, packed
 * into USB-MIDI event packets as a USB host driver would, then usb.c midi_in_event -> midi_enqueue).
 * The legacy MIDIPacketList API: simple, and still supported. --midi-log prints every event. */
#include <CoreMIDI/CoreMIDI.h>
#include <stdio.h>
#include <string.h>
#include "emu.h"

static MIDIClientRef client;
static MIDIEndpointRef src, dst;
static int logging, seen_input;

/* host -> firmware: SPSC ring of USB-MIDI packets (CoreMIDI thread -> the timer thread) */
#define RQ 1024u
static uint32_t rq[RQ];
static volatile uint32_t rq_w, rq_r;
static uint32_t held_pkt;
static int held;

static void log_pkt(const char *dir, uint32_t pkt)
{
    if (logging)
        printf("midi %s: %02X %02X %02X  (cin %X)\n", dir, (unsigned)(pkt >> 8) & 0xFFu,
               (unsigned)(pkt >> 16) & 0xFFu, (unsigned)(pkt >> 24) & 0xFFu, (unsigned)pkt & 15u);
}

static void put(uint32_t pkt)
{
    uint32_t w = __atomic_load_n(&rq_w, __ATOMIC_RELAXED);
    if (w - __atomic_load_n(&rq_r, __ATOMIC_ACQUIRE) >= RQ)
        return;                                       /* (full: dropped) */
    rq[w % RQ] = pkt;
    __atomic_store_n(&rq_w, w + 1u, __ATOMIC_RELEASE);
    log_pkt("in ", pkt);
}

/* the byte parser (running status, SysEx in 3-byte packets, real-time anywhere) */
static uint8_t run_st, need, have, dat[2], sx[3], sxn, in_sx;
static uint32_t pk(uint32_t cin, uint32_t a, uint32_t b, uint32_t c) { return cin | a << 8 | b << 16 | c << 24; }
static void byte_in(uint8_t b)
{
    if (b >= 0xF8u) {                                 /* real-time */
        put(pk(0xFu, b, 0, 0));
        return;
    }
    if (b == 0xF0u) {
        in_sx = 1;
        sx[0] = b;
        sxn = 1;
        run_st = 0;
        return;
    }
    if (in_sx) {
        if (b == 0xF7u) {
            sx[sxn++] = b;
            put(pk(sxn == 1u ? 5u : sxn == 2u ? 6u : 7u, sx[0], sxn > 1u ? sx[1] : 0u, sxn > 2u ? sx[2] : 0u));
            in_sx = 0;
            sxn = 0;
            return;
        }
        if (b & 0x80u) {                              /* a status ends SysEx (dropped) */
            in_sx = 0;
            sxn = 0;
        } else {
            sx[sxn++] = b;
            if (sxn == 3u) {
                put(pk(4u, sx[0], sx[1], sx[2]));
                sxn = 0;
            }
            return;
        }
    }
    if (b & 0x80u) {
        have = 0;
        if (b < 0xF0u) {
            run_st = b;
            need = (b & 0xF0u) == 0xC0u || (b & 0xF0u) == 0xD0u ? 1u : 2u;
        } else {                                      /* system common */
            run_st = 0;
            if (b == 0xF6u)
                put(pk(5u, b, 0, 0));
            else if (b == 0xF1u || b == 0xF3u) {
                run_st = b;
                need = 1;
            } else if (b == 0xF2u) {
                run_st = b;
                need = 2;
            }
        }
        return;
    }
    if (!run_st)
        return;
    dat[have++] = b;
    if (have < need)
        return;
    have = 0;
    if (run_st >= 0xF0u) {
        put(pk(need == 1u ? 2u : 3u, run_st, dat[0], need > 1u ? dat[1] : 0u));
        run_st = 0;
    } else {
        put(pk(run_st >> 4, run_st, dat[0], need > 1u ? dat[1] : 0u));
    }
}

static void read_proc(const MIDIPacketList *pl, void *ref, void *src_ref)
{
    const MIDIPacket *p = &pl->packet[0];
    UInt32 i, j;
    (void)ref;
    (void)src_ref;
    if (!seen_input) {
        seen_input = 1;
        printf("MIDI: a host is sending to \"ChoralRoot FM-1 In\"\n");
    }
    for (i = 0; i < pl->numPackets; i++) {
        for (j = 0; j < p->length; j++)
            byte_in(p->data[j]);
        p = MIDIPacketNext(p);
    }
}

static void notify_proc(const MIDINotification *n, void *ref)
{
    (void)ref;
    if (n->messageID == kMIDIMsgSetupChanged)
        printf("MIDI: setup changed (a host connected, disconnected or rescanned)\n");
}

int emu_midi_open(int log)
{
    OSStatus e;
    logging = log;
    e = MIDIClientCreate(CFSTR("ChoralRoot FM-1 emulator"), notify_proc, NULL, &client);
    if (e == noErr)
        e = MIDISourceCreate(client, CFSTR("ChoralRoot FM-1"), &src);
    if (e == noErr)
        e = MIDIDestinationCreate(client, CFSTR("ChoralRoot FM-1 In"), read_proc, NULL, &dst);
    if (e != noErr) {
        printf("MIDI: CoreMIDI failed (%d); no MIDI%s\n", (int)e, log ? " (events still logged)" : "");
        return -1;
    }
    {   /* stable ids: hosts remember the ports across runs */
        MIDIObjectSetIntegerProperty(src, kMIDIPropertyUniqueID, 0x43524631);   /* "CRF1" */
        MIDIObjectSetIntegerProperty(dst, kMIDIPropertyUniqueID, 0x43524649);   /* "CRFI" */
    }
    printf("MIDI: virtual source \"ChoralRoot FM-1\" (out), destination \"ChoralRoot FM-1 In\" (in)\n");
    return 0;
}

void emu_midi_close(void)
{
    if (src)
        MIDIEndpointDispose(src);
    if (dst)
        MIDIEndpointDispose(dst);
    if (client)
        MIDIClientDispose(client);
    src = dst = 0;
    client = 0;
}

void emu_midi_send(uint32_t pkt)
{
    static const uint8_t LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    uint8_t b[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
    uint32_t n = LEN[pkt & 15u];
    Byte buf[64];
    MIDIPacketList *pl = (MIDIPacketList *)buf;
    MIDIPacket *p;
    log_pkt("out", pkt);
    if (!src || !n)
        return;
    p = MIDIPacketListInit(pl);
    p = MIDIPacketListAdd(pl, sizeof buf, p, 0, n, b);
    if (p)
        MIDIReceived(src, pl);
}

int emu_midi_take(uint32_t *pkt)
{
    uint32_t r;
    if (held) {
        held = 0;
        *pkt = held_pkt;
        return 1;
    }
    r = __atomic_load_n(&rq_r, __ATOMIC_RELAXED);
    if (r == __atomic_load_n(&rq_w, __ATOMIC_ACQUIRE))
        return 0;
    *pkt = rq[r % RQ];
    __atomic_store_n(&rq_r, r + 1u, __ATOMIC_RELEASE);
    return 1;
}

void emu_midi_unget(uint32_t pkt)
{
    held_pkt = pkt;
    held = 1;
}
