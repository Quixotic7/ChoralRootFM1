/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI IN on the TRS jack: PH8 -> input channel 1 -> UART1 RX, 31250 baud,
 * RX DMA into a 128-byte ring, polled from the TIMER5 ISR (no UART IRQ).
 * FELUCCA_UART=0 disables this input. Channel
 * messages go into midi_in_q next to USB, as
 * USB-MIDI packets (cable 0, CIN = status >> 4), so seq.c routes them by channel
 * exactly as USB (CH1-4 or SEL;
 * host tests: midi_uart_test, hostsim TRACKS trs_test);
 * Clock/Start/Continue/Stop retain timestamps and source; other realtime,
 * system common and SysEx are ignored (no SysEx->UBOOT from DIN). */
#include "../hal/fm1_uart.h"   /* registers; relative, so the host tests find it too */

#define UM_RING 128u
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct {
    uint32_t rd, pend;
    volatile uint32_t bytes, drops, msgs;          /* TIMER5 writes; read-only CDC diagnostics */
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                   /* before timer5_start(): PORTH RMW */
{
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static uint32_t um_len(uint32_t s)                 /* data bytes of a channel status */
{
    return (s & 0xE0u) == 0xC0u ? 1u : 2u;         /* Cx Dx: 1, else 2 */
}

static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {
        if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu) {
            if (midi_enqueue(0xFu | b << 8, 2u))
                um.msgs++;
            else
                um.drops++;
        }
        return;                                    /* realtime keeps running status */
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u ? (uint8_t)b : 0;        /* system common/SysEx cancel running status */
        um.need = (uint8_t)um_len(b);
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        uint32_t pkt = (um.st >> 4) | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24;
        um.got = 0;
        if (midi_enqueue(pkt, 2u)) {
            um.msgs++;
        } else {
            midi_in_overflow = 1;
            um.drops++;
        }
    }
}

static void uart_midi_take(uint32_t n)
{
    um.pend += n;
    if (um.pend > UM_RING) {
        um.drops += um.pend - UM_RING;
        um.rd = (um.rd + um.pend - UM_RING) & (UM_RING - 1u);
        um.pend = UM_RING;
        um.st = um.got = um.sysex = 0;              /* lost bytes: wait for a complete new status */
        midi_in_overflow = 1;
    }
    while (um.pend) {
        um_byte(um_ring[um.rd]);
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.pend--;
        um.bytes++;
    }
}
static void uart_midi_poll(void)                   /* TIMER5 ISR, same context as usb_poll */
{
    uart_midi_take(fm1_uart1_rx_take());
}
