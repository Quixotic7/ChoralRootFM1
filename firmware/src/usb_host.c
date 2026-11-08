/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Retains USB host enumeration and input contributed by recursivefieldinstruments (2026). */
/* USB MIDI host on USB0 (CR_USB_HOST), included by usb.c after the SIE helpers and ep1_tx.
 *
 * ChoralRoot (HISTORY 3y260): Melodee 3fe570d's usb_host.c, ported. The changes: ChoralRoot's switches (CR_USB_HOST,
 * FELUCCA_UAC, FELUCCA_LOADER); host OUT drains ChoralRoot's one MIDI out ring (midi_out_q: the clock and transport
 * are in it, in order, where Melodee keeps them in a ring of their own); a session that ends raises
 * midi_in_overflow, so the audio interrupt's recovery releases every part (Melodee releases the USB notes' owners);
 * Options > USB Host is the switch (cr_ui.c), its messages the UI's toasts.
 *
 * The port is either the computer-facing device (usb.c) or, when the menu's USB HOST is on, the host of a class
 * compliant USB-MIDI controller, alone, behind a hub, or behind a hub on a hub's port (many hubs are two hub chips
 * in one case: the first chip's last port is the second chip). The choice is never saved: every boot is a device, so
 * the update paths always work. Switching is the main loop's (uh_task); usb.up is 0 while the port is a host.
 *
 *   main loop (uh_task)   waits for a device, resets the bus, and enumerates it with blocking EP0 transfers
 *                         (hub: powers the ports, resets those with something on them, and tries each device
 *                         until one has a USB-MIDI streaming interface; a hub found there is walked the same way,
 *                         one level only). Failed direct probes wait for unplug; idle hubs are polled for
 *                         downstream connection changes, and a hub port whose device did not finish enumerating
 *                         is tried again a few times. The ISR does nothing until uh.run.
 *   TIMER5 (uh_poll)      once uh.run is 1 the SIE is the ISR's alone: optional IN and OUT endpoints share host
 *                         EP1 in separate directions. IN is asked for a packet (ReqPkt), read when RxPktRdy, and its channel / clock events
 *                         go to the same ring as USB and TRS MIDI (source 1). A stall or a run of errors sets
 *                         uh.fault; the main loop then resets the bus and looks for a device again.
 *
 * Register sequences follow the AC79 SDK's USB0 host code (usb_h_sie_init, usb_h_chirp_and_reset, usb0_h_ep_read,
 * usb_ctlXfer): EP0 data moves through ep0buf / EP0_CNT as in the device driver, the target is FADDR (USB0 has no
 * per-endpoint function address), RXMAXP is 0xFF as everywhere in this firmware. Not driven: VBUS (the port
 * needs outside power, e.g. a powered hub), low-speed devices. OUT drains the MIDI out ring only after the target
 * ACKs; no SysEx/editor traffic is sent to attached devices.
 * Host OUT is tested against the SDK register definitions and a SIE model, not on hardware: see BUILDING.md. */
#if CR_USB_HOST
#include "usb_host_desc.h"

enum { S_DEVCTL = 15, S_TXTYPE = 24, S_TXINTERVAL = 25, S_RXTYPE = 26, S_RXINTERVAL = 27 };
/* Events for main.c to tell the user (main.c UH_TEXT holds their words). ON / OFF are the header's HOST tag only.
 * MIDI / LOST: a device started or stopped; the others say why a search found no MIDI (uh_result picks one). */
enum { UH_MSG_NONE, UH_MSG_ON, UH_MSG_OFF, UH_MSG_MIDI, UH_MSG_NOMIDI, UH_MSG_LOW, UH_MSG_LOST, UH_MSG_NOLINE,
       UH_MSG_NOANSWER, UH_MSG_HUB_EMPTY, UH_MSG_HUB_NOMIDI };
#define UH_FIRST_ADDR 1u
#define UH_MAX_ERRORS 16u
#define UH_HUB_LEVELS 2u                                /* the hub on the port, and one hub on its ports */
#define UH_HUB_PORTS 7u                                 /* ports used per hub */
#define UH_ROOT_TRIES 5u                                /* resets of a device that does not answer at all */
#define UH_PORT_TRIES 4u                                /* later rounds for hub ports whose device did not enumerate */
#define UH_PORT_RETRY_MS 1000u
#define UH_NOLINE_MS 3000u                              /* nothing on the line this long after HOST: said once */

static uint8_t uh_rx[64 + 4] __attribute__((aligned(4)));
typedef struct {
    uint8_t addr, mps, ports;                           /* addr 0: no hub at this level */
    uint8_t port;                                       /* level 1: the level-0 port it is on */
    uint8_t retry;                                      /* bit p: the device on port p did not finish enumerating */
} uh_hub_t;

static struct {
    uint8_t want;                                       /* the menu (main loop) */
    uint8_t on;                                         /* the port is a host */
    volatile uint8_t run;                               /* TIMER5 owns the active MIDI endpoints */
    volatile uint8_t fault;                             /* TIMER5 -> main loop: reset the bus and look again */
    uint8_t seen, next_addr, dev, tried;
    uint8_t answered;                                   /* the device on the port answered its first request */
    uh_hub_t hub[UH_HUB_LEVELS];
    uint8_t hub_poll_level, hub_poll_port;
    uint8_t port_rounds;                                /* retry rounds done since the last hub scan or port change */
    uint8_t low;                                        /* a low-speed device was skipped on a hub port */
    uint8_t stage;                                      /* the step the last probe stopped at (uh_dg letters) */
    uint8_t line_said, dg_n;
    uint8_t dg_fail, dg_nomidi;                         /* the search met a device that did not answer / was not MIDI */
    uint8_t dg_cls[3];                                  /* the last device's bDeviceClass, first interface class / subclass */
    char dg[28];                                        /* what the last search saw, in letters (uh_dg): tests, bench */
    uint8_t in_ep, out_mps, tx_count, tx_errors, line_idle;
    uint8_t msg;                                        /* one UH_MSG_* for main.c to show */
    volatile uint8_t errors;
    uint32_t t_ms, retry_ms;
    uint32_t tx_ms, hub_poll_ms, port_retry_ms, line_ms;
    volatile uint32_t pkts, events;
} uh;

/* ------------------------------------------------ main loop: control --- */
static void uh_wait(uint32_t ms)                        /* main loop only */
{
    uint32_t t = fm1_ms;
    while (fm1_ms - t < ms)
        fm1_wdt_feed();
}

/* CSR0 (host) until want_set bits are all set and want_clr are all clear: >= 0 the CSR0, < 0 a stall / error / timeout */
static int uh_csr0(uint32_t want_set, uint32_t want_clr)
{
    uint32_t t = fm1_ms, v;
    for (;;) {
        v = sie_rd(S_CSR0);
        if (v & 0x04u) {                                /* RxStall */
            sie_wr(S_CSR0, 0);
            return -2;
        }
        if (v & 0x10u) {                                /* Error: no answer */
            sie_wr(S_CSR0, 0);
            return -1;
        }
        if ((v & want_set) == want_set && !(v & want_clr))
            return (int)v;
        if (fm1_ms - t > 500u)
            return -3;
        fm1_wdt_feed();
    }
}

/* One control transfer to `addr`: SETUP, the IN data stage if any, the status stage. Bytes read, or < 0.
 * Only requests without an OUT data stage (every one used here). The data goes to `out`, or with `walk` to the
 * descriptor walk, which may end the data stage early once it has found what it needs. */
static int uh_xfer(uint8_t addr, uint8_t rt, uint8_t rq, uint16_t val, uint16_t idx, uint16_t len, uint8_t *out,
                   uh_desc_t *walk, uint8_t mps)
{
    uint32_t got = 0, n, k, i, stop = 0;
    int r;
    sie_wr(S_FADDR, addr);
    sie_wr(S_INDEX, 0);
    ep0buf[0] = rt;
    ep0buf[1] = rq;
    ep0buf[2] = (uint8_t)val;
    ep0buf[3] = (uint8_t)(val >> 8);
    ep0buf[4] = (uint8_t)idx;
    ep0buf[5] = (uint8_t)(idx >> 8);
    ep0buf[6] = (uint8_t)len;
    ep0buf[7] = (uint8_t)(len >> 8);
    fm1_usb_ep0_send(ep0buf, 8);
    sie_wr(S_CSR0, 0x0Au);                              /* SetupPkt | TxPktRdy */
    r = uh_csr0(0, 0x02u);
    if (r < 0)
        return r;
    if (len && (rt & 0x80u)) {
        while (got < len) {
            sie_wr(S_CSR0, 0x20u);                      /* ReqPkt */
            r = uh_csr0(0x01u, 0);
            if (r < 0)
                return r;
            n = sie_rd(S_COUNT0);
            if (n > 64u)
                n = 64u;
            fm1_usb_rx_sync();
            k = got + n > len ? len - got : n;
            if (walk)
                stop = (uint32_t)uh_desc_feed(walk, ep0buf, k);
            else
                for (i = 0; i < k; i++)
                    out[got + i] = ep0buf[i];
            sie_wr(S_CSR0, 0);                          /* RxPktRdy done */
            got += n;
            if (n < mps || stop)
                break;
        }
        if (got > len)
            got = len;
        sie_wr(S_CSR0, 0x42u);                          /* status: StatusPkt | TxPktRdy (zero-length OUT) */
        r = uh_csr0(0, 0x02u);
    } else {
        sie_wr(S_CSR0, 0x60u);                          /* status: StatusPkt | ReqPkt (zero-length IN) */
        r = uh_csr0(0x01u, 0);
        if (r >= 0)
            sie_wr(S_CSR0, 0);
    }
    return r < 0 ? r : (int)got;
}

static int uh_ctl(uint8_t addr, uint8_t rt, uint8_t rq, uint16_t val, uint16_t idx, uint16_t len, uint8_t *out,
                  uint8_t mps)
{
    return uh_xfer(addr, rt, rq, val, idx, len, out, 0, mps);
}

/* ---------------------------------------------------- enumeration --- */
/* What the last search met, in letters (uh.dg). It is never shown: the tests check it, and it is the first thing to
 * read in a debugger when a device is not found. The user is told uh_result's plain message instead.
 *   a device   D no answer to its first request, A SET_ADDRESS failed, C configuration header failed,
 *              W configuration walk failed, N answered without USB-MIDI, S SET_CONFIGURATION / SET_INTERFACE failed,
 *              H a hub (without MIDI below it), K MIDI found
 *   a hub      Q its hub descriptor failed (S: its SET_CONFIGURATION)
 *   a hub port - empty, L low speed, R reset never completed, E not enabled after reset, X status unreadable,
 *              else the letter of the device on it
 * Examples: "HUB4 -D(K---)-" a 4-port hub, port 2 a device with no answer, port 3 a second hub whose port 1 has
 * MIDI; "DEV D3" the device on the FM-1's port did not answer 3 bus resets; "DEV N1 000806" it answered and is not
 * MIDI, with its bDeviceClass, first interface class and subclass in hex (here a mass-storage stick);
 * "RETRY2 D" / "PLUG N" a retry round or a hot-plug on a hub port. */
static void uh_dg(char c)
{
    if (uh.dg_n < sizeof uh.dg - 1u) {
        uh.dg[uh.dg_n++] = c;
        uh.dg[uh.dg_n] = 0;
    }
}
static void uh_dg_s(const char *s) { while (*s) uh_dg(*s++); }
static void uh_dg_clear(void) { uh.dg_n = uh.dg_fail = uh.dg_nomidi = 0; uh.dg[0] = 0; }

/* One device's or port's outcome: its letter in uh.dg, and whether it failed to answer or was simply not MIDI,
 * which is what uh_result tells the user. */
static void uh_note(char c)
{
    uh_dg(c);
    if (c == 'N')
        uh.dg_nomidi = 1;
    else if (c != '-' && c != 'L' && c != 'H' && c != 'K')
        uh.dg_fail = 1;                                 /* D A C W S Q R E X: it did not finish answering */
}

/* The search that found no MIDI, as one plain message. Most useful first: a device that does not answer can be
 * replugged; a low-speed one cannot be used at all; then what the hub (if any) had on it. */
static uint8_t uh_result(void)
{
    if (uh.dg_fail)
        return UH_MSG_NOANSWER;
    if (uh.low)
        return UH_MSG_LOW;
    if (uh.hub[0].addr)
        return uh.dg_nomidi ? UH_MSG_HUB_NOMIDI : UH_MSG_HUB_EMPTY;
    return UH_MSG_NOMIDI;
}

static int uh_hub_probe(uint8_t level, uint8_t addr, uint8_t mps, uint8_t cfgval, uh_midi_t *m);

/* A device on port p (1..7) of the hub at `level` gets its own address: 2..8 below the first hub, 9..15 below the
 * second, so a hub's devices never take an address the other hub's tree uses. */
static uint8_t uh_port_addr(uint8_t level, uint8_t p) { return (uint8_t)(UH_FIRST_ADDR + level * UH_HUB_PORTS + p); }

/* The device at address 0 (just reset): give it an address, read its configuration, and if it has a USB-MIDI
 * streaming interface select it. 1 = found, 0 = it answered and has none (including a hub, whose own ports are
 * retried, and a hub two levels down), -1 = it did not finish enumerating (worth another try later). */
static int uh_probe(uint8_t depth, uh_midi_t *m)
{
    uint8_t d8[8], c9[9], mps, cls, addr = uh.next_addr++;
    uint32_t total;
    uh_desc_t walk;
    int r;
    uh.stage = 'D';
    r = uh_ctl(0, 0x80, 6, 0x0100, 0, 8, d8, 8);        /* GET_DESCRIPTOR(device), first 8 bytes */
    if (r < 8)
        return -1;
    uh.stage = 'A';
    if (!depth)
        uh.answered = 1;
    mps = d8[7] == 16u || d8[7] == 32u || d8[7] == 64u ? d8[7] : 8u;
    cls = d8[4];
    if (uh_ctl(0, 0x00, 5, addr, 0, 0, 0, mps) < 0)     /* SET_ADDRESS */
        return -1;
    uh_wait(10);                                        /* the device settles to its new address */
    uh.stage = 'C';
    r = uh_ctl(addr, 0x80, 6, 0x0200, 0, 9, c9, mps);
    if (r < 9)
        return -1;
    total = (uint32_t)(c9[2] | c9[3] << 8);             /* the whole configuration, walked as it arrives */
    uh_desc_start(&walk, m);
    uh.stage = 'W';
    if (total > 9u && uh_xfer(addr, 0x80, 6, 0x0200, 0, (uint16_t)total, 0, &walk, mps) < 9)
        return -1;
    uh.dg_cls[0] = cls;
    uh.dg_cls[1] = walk.if_cls;
    uh.dg_cls[2] = walk.if_sub;
    if (cls == 9u || walk.hub) {                        /* a hub (some give class 9 on the interface only) */
        r = depth < UH_HUB_LEVELS && uh_hub_probe(depth, addr, mps, c9[5], m) ? 1 : 0;
        uh.stage = r ? 'K' : 'H';
        return r;
    }
    uh.stage = 'N';
    if (!m->found)
        return 0;
    uh.stage = 'S';
    if (uh_ctl(addr, 0x00, 9, m->cfg, 0, 0, 0, mps) < 0)   /* SET_CONFIGURATION */
        return -1;
    if (m->alt && uh_ctl(addr, 0x01, 11, m->alt, m->iface, 0, 0, mps) < 0)   /* SET_INTERFACE */
        return -1;
    uh.dev = addr;
    uh.stage = 'K';
    return 1;
}

/* Probe only this downstream port of the hub at `level`; keep the hubs and the other devices configured. Each
 * port reuses its address on reconnect, so repeated hot-plugs cannot exhaust the USB address space. A port whose
 * device did not finish enumerating (still booting, a missed request) is marked for the idle retry rounds; one
 * that answered without MIDI, or is low speed, is not. */
static int uh_hub_port_probe(uint8_t level, uint8_t p, uint32_t st, uh_midi_t *m)
{
    uint8_t b[4], addr = uh.hub[level].addr, mps = uh.hub[level].mps, bit = (uint8_t)(1u << p);
    uint32_t t;
    int r = -1;
    char c = 'R';
    uh.hub[level].retry &= (uint8_t)~bit;
    if (!(st & 0x0001u)) {                              /* nothing there */
        uh_note('-');
        return 0;
    }
    if (st & 0x0200u) {                                 /* low speed: not supported */
        uh.low = 1;
        uh_note('L');
        return 0;
    }
    if (!level && uh.hub[1].addr && uh.hub[1].port == p)
        uh.hub[1].addr = 0;                             /* the reset takes the second hub's address and its tree */
    if (uh_ctl(addr, 0x23, 3, 4, p, 0, 0, mps) < 0)    /* SET_FEATURE(PORT_RESET) */
        goto out;
    t = fm1_ms;
    for (;;) {
        if (uh_ctl(addr, 0xA3, 0, 0, p, 4, b, mps) >= 4 && (b[2] & 0x10u))   /* C_PORT_RESET */
            break;
        if (fm1_ms - t > 500u)
            goto out;
        uh_wait(10);
    }
    uh_ctl(addr, 0x23, 1, 20, p, 0, 0, mps);            /* CLEAR_FEATURE(C_PORT_RESET) */
    c = 'E';
    if (uh_ctl(addr, 0xA3, 0, 0, p, 4, b, mps) < 4 || !(b[0] & 0x02u))   /* enabled? */
        goto out;
    uh_wait(20);                                        /* reset recovery */
    uh.next_addr = uh_port_addr(level, p);
    r = uh_probe((uint8_t)(level + 1u), m);
    c = (char)uh.stage;
    if (!level && uh.hub[1].addr == uh_port_addr(0, p))
        uh.hub[1].port = p;                             /* a second hub was found here, configured even if idle */
out:
    uh_note(c);
    if (r < 0 && uh.hub[level].addr)
        uh.hub[level].retry |= bit;
    return r > 0;
}

/* Read and acknowledge connection changes, including disconnects and unsupported devices. An unresponsive
 * hub is left alone until root unplug/replug (the first hub) or a change on its port (the second), rather than
 * repeating a blocking control timeout forever. */
static int uh_hub_status(uint8_t level, uint8_t p, uint8_t *b)
{
    uh_hub_t *h = &uh.hub[level];
    if (uh_ctl(h->addr, 0xA3, 0, 0, p, 4, b, h->mps) < 4 ||
        ((b[2] & 1u) && uh_ctl(h->addr, 0x23, 1, 16, p, 0, 0, h->mps) < 0)) {
        uh_note('X');
        h->addr = 0;
        uh.hub[1].addr = 0;                             /* (the second hub is below the first) */
        return 0;
    }
    return 1;
}

/* Configure and power a hub once (level 0: the one on the port, 1: one on its ports). After the initial scan,
 * idle polling uses connection-change bits. */
static int uh_hub_probe(uint8_t level, uint8_t addr, uint8_t mps, uint8_t cfgval, uh_midi_t *m)
{
    uint8_t b[16], ports, p;                            /* (the hub descriptor is 9 bytes with <= 7 ports) */
    uint32_t good;
    uh_hub_t *h = &uh.hub[level];
    int r = 0;
    uh_dg_s(level ? "(" : "HUB");
    if (uh_ctl(addr, 0x00, 9, cfgval, 0, 0, 0, mps) < 0) {
        uh_note('S');
        uh_dg(')');
        return 0;
    }
    if (uh_ctl(addr, 0xA0, 6, 0x2900, 0, 9, b, mps) < 7) {   /* hub descriptor */
        uh_note('Q');
        uh_dg(')');
        return 0;
    }
    ports = b[2] > UH_HUB_PORTS ? UH_HUB_PORTS : b[2];
    if (!level) {
        uh_dg((char)('0' + (b[2] > 9u ? 9u : b[2])));
        uh_dg(' ');
    }
    good = (uint32_t)b[5] * 2u;                         /* bPwrOn2PwrGood, in 2 ms */
    for (p = 1; p <= ports; p++)
        uh_ctl(addr, 0x23, 3, 8, p, 0, 0, mps);         /* SET_FEATURE(PORT_POWER) */
    uh_wait((good < 100u ? 100u : good) + 100u);        /* power good, then the devices announce themselves */
    h->addr = addr;
    h->mps = mps;
    h->ports = ports;
    h->retry = 0;
    if (!level) {
        uh.hub[1].addr = uh.hub[1].retry = 0;
        uh.hub_poll_level = 0;
        uh.hub_poll_port = 1;
        uh.port_rounds = 0;
    }
    uh.port_retry_ms = fm1_ms;
    uh.hub_poll_ms = fm1_ms;
    for (p = 1; p <= ports; p++) {
        if (!uh_hub_status(level, p, b))
            break;
        if (uh_hub_port_probe(level, p, (uint32_t)(b[0] | b[1] << 8), m)) {
            r = 1;
            break;
        }
    }
    if (level)
        uh_dg(')');
    return r;
}

/* the next hub port the idle poll reads: every port of the first hub, then of the second if there is one */
static void uh_hub_poll_next(void)
{
    uint8_t l = uh.hub_poll_level;
    if (uh.hub_poll_port < uh.hub[l].ports && uh.hub[l].addr) {
        uh.hub_poll_port++;
        return;
    }
    uh.hub_poll_level = !l && uh.hub[1].addr && uh.hub[1].ports ? 1u : 0u;
    uh.hub_poll_port = 1;
}

/* One idle retry round: every hub port marked as not having finished enumerating is reset and probed again, at
 * most UH_PORT_TRIES rounds UH_PORT_RETRY_MS apart, so a device that was still starting at the hub scan is found
 * without unplugging it. 1 = a MIDI device was found (in *m). */
static int uh_hub_retry(uh_midi_t *m)
{
    uint8_t b[4], l, p;
    for (l = 0; l < UH_HUB_LEVELS; l++)
        for (p = 1; p <= uh.hub[l].ports && uh.hub[l].addr; p++) {
            if (!(uh.hub[l].retry & (1u << p)))
                continue;
            if (!uh_hub_status(l, p, b))
                return 0;
            if (uh_hub_port_probe(l, p, (uint32_t)(b[0] | b[1] << 8), m))
                return 1;
        }
    return 0;
}

static int uh_hub_retry_due(uint32_t now)
{
    return (uh.hub[0].retry || uh.hub[1].retry) && uh.port_rounds < UH_PORT_TRIES &&
           (uint32_t)(now - uh.port_retry_ms) >= UH_PORT_RETRY_MS;
}

/* Host EP1 has independent RX/TX DMA slots. Both target endpoints belong to this device and alternate setting. */
static void uh_start_ep(const uh_midi_t *m)
{
    sie_wr(S_FADDR, uh.dev);
    sie_wr(S_INDEX, 1);
    uh.in_ep = m->in_ep;
    uh.out_mps = (uint8_t)m->out_mps;
    uh.tx_count = uh.tx_errors = 0;
    uh.line_idle = 0;
    if (m->in_ep) {
        fm1_usb_ep_rxbuf(1, uh_rx);
        sie_wr(S_RXMAXP, 0xFF);
        sie_wr(S_RXCSR1, 0x90);                         /* clear data toggle, flush */
        sie_wr(S_RXCSR2, 0);
        sie_wr(S_RXTYPE, (uint32_t)(m->in_type << 4 | m->in_ep));
        sie_wr(S_RXINTERVAL, m->in_type == 3u ? m->in_interval : 0u);
        sie_wr(S_RXCSR1, 0x20u);                        /* ReqPkt */
    }
    if (m->out_ep) {
        fm1_usb_ep_txbuf(1, ep1tx);
        sie_wr(S_TXMAXP, 0xFF);                         /* AC79 USB0's DMA maximum, as in the device driver */
        sie_wr(S_TXCSR1, 0x48);                         /* ClrDataTog | FlushFIFO, only at endpoint start */
        sie_wr(S_TXCSR2, 0x20);                         /* MODE: TX half of a bidirectional endpoint */
        sie_wr(S_TXTYPE, (uint32_t)(m->out_type << 4 | m->out_ep));
        sie_wr(S_TXINTERVAL, m->out_type == 3u ? m->out_interval : 0u);
    }
    fm1_usb_ep_enable(1u << 1);
    uh.errors = 0;
    uh.fault = 0;
    RING_PUBLISH();
    uh.run = 1;
}

/* Stop admitting output before discarding queued notes; the audio interrupt's overflow recovery (cr_out.c) then
 * releases every part, the notes the device left on among them */
static void uh_session_end(void)
{
    uh.run = 0;
    uh.out_mps = 0;
    uh.tx_count = 0;
    mo_r = mo_w;
    RING_PUBLISH();
    if (uh.on)
        midi_in_overflow = 1;
}

/* the controller's port as a host with nothing attached yet */
static void uh_host_up(void)
{
    usb.timeouts = 0;
    fm1_usb_reset();
    uh_wait(25);
    fm1_usb_host_attach(ep0buf);
    uh_wait(25);
    sie_wr(S_INTRUSBE, 0);
    sie_wr(S_INTRTX1E, 0);
    sie_wr(S_INTRTX2E, 0);
    sie_wr(S_INTRRX1E, 0);
    sie_wr(S_INTRRX2E, 0);
    fm1_usb_ep_enable(0x1Fu);
    sie_wr(S_FADDR, 0);
    sie_wr(S_DEVCTL, 0x03u);                            /* session + host request */
    uh.seen = 0;
    uh.line_idle = 0;
    uh.hub[0].addr = uh.hub[1].addr = 0;
}

/* a device is on the line: reset the bus and find the MIDI endpoint */
static void uh_connect(void)
{
    uh_midi_t m;
    uint32_t t;
    int ok;
    if (sie_rd(S_DEVCTL) & 0x20u) {                     /* low speed (FSDev is bit 6, LSDev bit 5) */
        if (!uh.tried)
            uh.msg = UH_MSG_LOW;                        /* said once per attach, as NO MIDI is */
        uh.tried = 1;
        return;
    }
    /* A root port reset lasts >= 50 ms (USB 2.0 TDRSTR). Some hubs answer only after 400 ms of recovery, so a
     * device that does not answer at all is reset again with a longer wait; one that answered is not. */
    for (t = 0;; t++) {
        sie_wr(S_POWER, 0x08u);                         /* bus reset */
        uh_wait(50);
        sie_wr(S_POWER, 0);
        uh_wait(100u << t);                             /* 100, 200, 400, 800, 1600 ms */
        uh.next_addr = UH_FIRST_ADDR;
        uh.answered = 0;
        uh.low = 0;
        uh_dg_clear();
        if ((ok = uh_probe(0, &m) > 0) || uh.answered || t + 1u == UH_ROOT_TRIES)
            break;
    }
    if (ok) {
        uh_start_ep(&m);
        uh.msg = UH_MSG_MIDI;
        uh.tried = 0;
    } else {
        if (!uh.dg_n) {                                 /* not a hub: the device on the port, and the resets tried */
            static const char hex[] = "0123456789ABCDEF";
            uh_dg_s("DEV ");
            uh_note((char)uh.stage);
            uh_dg((char)('0' + t + 1u));
            if (uh.stage == 'N') {                      /* answered: its classes, device / interface / subclass */
                uh_dg(' ');
                for (uint32_t i = 0; i < 3u; i++) {
                    uh_dg(hex[uh.dg_cls[i] >> 4]);
                    uh_dg(hex[uh.dg_cls[i] & 15u]);
                }
            }
        }
        if (!uh.tried)                                  /* said once per attach */
            uh.msg = uh_result();
        uh.tried = 1;
    }
}

/* Main loop. The menu's USB HOST (uh.want) switches the port; as a host this finds and keeps a controller. */
static void uh_task(uint32_t now)
{
    if (uh.want != uh.on) {
        usb.up = 0;                                     /* TIMER5 leaves the SIE to this context */
        usb.config = 0;
        usb.suspended = 0;
        uh_session_end();
        uh.fault = 0;
        uh.tried = 0;
        if (uh.want) {
#if FELUCCA_UAC
            ua_hw_stop();
#endif
            usb.sx_on = 0;
            fm1_usb_off();
            uh.on = 1;
            uh_host_up();
            uh.retry_ms = now;
            uh.line_ms = now;
            uh.line_said = 0;
            uh_dg_clear();
            uh.msg = UH_MSG_ON;
        } else {
            uh.on = 0;
            fm1_usb_off();
            usb.retry_ms = now - 1000u;                 /* usb_retry makes the port a device again at once */
            uh.msg = UH_MSG_OFF;
        }
        return;
    }
    if (!uh.on)
        return;
    if (uh.fault) {                                     /* stall, or the controller stopped answering (TIMER5 cleared run) */
        uh_session_end();
        uh.fault = 0;
        uh_host_up();
        uh.retry_ms = now + 500u;
        uh.msg = UH_MSG_LOST;
        return;
    }
    if (uh.run)
        return;
    if ((int32_t)(now - uh.t_ms) < 20 || (int32_t)(now - uh.retry_ms) < 0)
        return;
    uh.t_ms = now;
    if (!fm1_usb_line()) {
        uh.seen = 0;
        if (!uh.line_said && (uint32_t)(now - uh.line_ms) >= UH_NOLINE_MS) {
            uh.line_said = 1;                           /* the port sees no device pull-up at all */
            uh.msg = UH_MSG_NOLINE;
        }
        if (uh.line_idle < 5u) uh.line_idle++;
        if (uh.line_idle >= 5u) {                       /* debounce root disconnect while idle too */
            uh.tried = 0;
            uh.hub[0].addr = uh.hub[1].addr = 0;
        }
        return;
    }
    uh.line_idle = 0;
    uh.line_said = 1;
    if (uh.tried) {
        if (uh.hub[0].addr && uh_hub_retry_due(now)) {
            uh_midi_t m;
            uh.port_rounds++;
            uh.port_retry_ms = now;
            uh.low = 0;
            uh_dg_clear();
            uh_dg_s("RETRY");
            uh_dg((char)('0' + uh.port_rounds));
            uh_dg(' ');
            if (uh_hub_retry(&m)) {
                uh_start_ep(&m);
                uh.msg = UH_MSG_MIDI;
                uh.tried = 0;
            } else if ((!uh.hub[0].retry && !uh.hub[1].retry) || uh.port_rounds == UH_PORT_TRIES) {
                uh.msg = uh_result();                  /* settled, or the last round: what it found */
            }
            return;
        }
        if (uh.hub[0].addr && uh.hub[0].ports && (uint32_t)(now - uh.hub_poll_ms) >= 100u) {
            uint8_t b[4], l = uh.hub_poll_level, p = uh.hub_poll_port;
            uh_midi_t m;
            uh.hub_poll_ms = now;
            uh_hub_poll_next();
            if (!uh.hub[l].addr)                       /* the second hub went since this port was chosen */
                return;
            if (!uh_hub_status(l, p, b) || !(b[2] & 1u))
                return;
            if (!l && uh.hub[1].addr && uh.hub[1].port == p)
                uh.hub[1].addr = 0;                    /* the second hub's port changed: it and its tree are gone */
            uh.hub[l].retry &= (uint8_t)~(1u << p);   /* a change on the port replaces its pending retries */
            if (b[0] & 1u) {
                uh_wait(100);                          /* newly attached device settles before port reset */
                uh.low = 0;
                uh_dg_clear();
                uh_dg_s("PLUG ");
                if (uh_hub_status(l, p, b) && uh_hub_port_probe(l, p, (uint32_t)(b[0] | b[1] << 8), &m)) {
                    uh_start_ep(&m);
                    uh.msg = UH_MSG_MIDI;
                    uh.tried = 0;
                } else {
                    uh.msg = uh_result();              /* what was plugged in, said, not skipped silently */
                    if (uh.hub[l].retry & (1u << p)) {
                        uh.port_rounds = 0;            /* a fresh attach gets its own retry rounds */
                        uh.port_retry_ms = now;
                    }
                }
            }
        }
        return;                                        /* direct failures wait for root unplug/replug */
    }
    if (uh.seen < 5u) {                                 /* 100 ms of a steady line before the bus reset */
        uh.seen++;
        return;
    }
    uh.seen = 0;
    uh_connect();
}

/* ------------------------------------------------------ TIMER5 ISR --- */
static void uh_fail(void)
{
    uh.run = 0;
    uh.fault = 1;
}

/* Retain ring slots and the DMA buffer until ACK. A NAK or transient error retries the same data toggle;
 * a stall, repeated errors or 500 ms without completion hands recovery to the main loop. Never wait here. */
static void uh_tx_poll(void)
{
    uint32_t csr = sie_rd(S_TXCSR1), n = 0;
    if (uh.tx_count) {
        if ((csr & 0x20u) || fm1_ms - uh.tx_ms >= 500u) {
            uh_fail();
            return;
        }
        if (csr & 0x84u) {                              /* Error / NAK timeout */
            if ((csr & 4u) && ++uh.tx_errors >= UH_MAX_ERRORS) {
                uh_fail();
                return;
            }
            sie_wr(S_TXCSR1, 0x01u);                    /* retry retained FIFO, preserving data toggle */
            return;
        }
        if (csr & 0x03u) return;                        /* TxPktRdy / FIFONotEmpty: no ACK yet */
        RING_PUBLISH();
        mo_r += uh.tx_count;
        uh.tx_count = uh.tx_errors = 0;
    }
    while (mo_r + n / 4u != mo_w && n + 4u <= uh.out_mps) {
        uint32_t p = midi_out_q[(mo_r + n / 4u) % MQ];
        ep1tx[n] = (uint8_t)p;
        ep1tx[n + 1] = (uint8_t)(p >> 8);
        ep1tx[n + 2] = (uint8_t)(p >> 16);
        ep1tx[n + 3] = (uint8_t)(p >> 24);
        n += 4u;
    }
    if (!n) return;
    uh.tx_count = (uint8_t)(n / 4u);
    uh.tx_ms = fm1_ms;
    fm1_usb_ep_send(1, ep1tx, n);
    sie_wr(S_TXCSR1, 0x01u);
}

static USB_OUTLINE void uh_poll(void)                   /* (out of line: tests/target_budget.py watches usb_poll) */
{
    uint32_t csr, n, i;
    if (!fm1_usb_line()) {
        /* EOP also drives SE0 briefly. Require 8 ms of idle samples, not one sample during a packet. */
        if (++uh.line_idle >= 16u) uh_fail();
        return;
    }
    uh.line_idle = 0;
    sie_wr(S_INDEX, 1);
    if (uh.out_mps) uh_tx_poll();
    if (!uh.run || !uh.in_ep) return;
    csr = sie_rd(S_RXCSR1);
    if (csr & 0x40u) {                                  /* RxStall: the endpoint is gone */
        uh_fail();
        return;
    }
    if (csr & 0x0Cu) {                                  /* Error / DataError: no usable answer */
        if (++uh.errors >= UH_MAX_ERRORS) {
            uh_fail();
            return;
        }
        sie_wr(S_RXCSR1, 0x20u);                        /* the flags clear (written 0) and the request goes again; the
                                                         * data toggle stays: it is reset when the endpoint starts only */
        return;
    }
    if (csr & 0x01u) {                                  /* a packet is in uh_rx */
        n = sie_rd(S_RXCOUNT1) | (sie_rd(S_RXCOUNT2) << 8);
        if (n > 64u)
            n = 64u;
        fm1_usb_rx_sync();
#ifndef FELUCCA_LOADER
        if (MQ - (mi_w - mi_r) < EP1_ROOM)
            return;                                     /* ring too full: the packet stays, retried next poll */
#endif
        for (i = 0; i + 3u < n; i += 4u) {
            uint32_t p = uh_midi_ok((uint32_t)uh_rx[i] | (uint32_t)uh_rx[i + 1] << 8 |
                                    (uint32_t)uh_rx[i + 2] << 16 | (uint32_t)uh_rx[i + 3] << 24);
            if (p) {
                midi_enqueue(p, 1u);
                uh.events++;
            }
        }
        uh.pkts++;
        uh.errors = 0;
        sie_wr(S_RXCSR1, 0x10u);                        /* release the packet */
        sie_wr(S_RXCSR1, 0x20u);                        /* and ask for the next */
        return;
    }
    if (!(csr & 0x20u))                                 /* nothing pending: ask */
        sie_wr(S_RXCSR1, 0x20u);
}
#endif /* CR_USB_HOST */
