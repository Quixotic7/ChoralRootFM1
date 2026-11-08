/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Retains the USB host SIE/bus fixture contributed by recursivefieldinstruments (2026).
 * Adapted for ChoralRoot (HISTORY 3y260): Melodee 3fe570d's tests/usb_host_test.c against ChoralRoot's usb.c. */
/* USB MIDI host (firmware/src/usb_host.c, usb_host_desc.h): the production code against a model of the SIE and of
 * what hangs on the cable. The model answers EP0 requests the way a keyboard, a hub and a mass-storage stick do,
 * so this tests the enumeration order, the hub handling, the packet filter and the fault paths. It does not test
 * the controller: the register sequences come from the AC79 SDK's USB0 host code and need a bench.
 * ChoralRoot: usb.c with CR_USB_HOST 1 (choralroot.c's), FELUCCA_UAC 1. Host OUT drains ChoralRoot's one MIDI out ring
 * (midi_out_q: notes, clock and transport leave in the order they were queued; Melodee keeps the clock in a ring of
 * its own, ahead of the notes); a session that ends (unplug, fault, USB Host Off) raises midi_in_overflow, which the
 * audio interrupt's recovery (cr_out.c events_block) consumes: the queue dropped, every part released.
 * FELUCCA_OTA=1 (run_tests.sh's second build): the device side after USB Host Off, a SysEx sent whole before the
 * notes and the clock that wait behind it.
 *   cc -std=gnu11 -O1 -fsanitize=address,undefined -o build/host/usb_host_test tests/usb_host_test.c (run_tests.sh) */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_usb.h"
#pragma GCC diagnostic pop

/* ------------------------------------------------------- the devices --- */
enum { KBD, HUB, STICK, HUB2, HUB3, NDEV };
typedef struct {
    uint8_t addr, cfg, alt, present, in_default;
    uint8_t cls, mps0;
    const uint8_t *cfgd;
    uint16_t cfglen;
    uint8_t ports;                      /* hub: downstream devices by port (1-based), 0 = empty */
    uint8_t port_dev[8], port_pow[8], port_en[8], port_chg[8], port_ls[8];
    uint8_t fail_probes;                /* the next GET_DESCRIPTOR(device) requests go unanswered (still booting) */
    uint32_t seen_set_cfg, seen_set_if, seen_probe;
} dev_t_;
static dev_t_ dev[NDEV];
static uint8_t kbd_cfg[128], stick_cfg[64], hub_cfg[32];
static uint16_t kbd_cfg_len;
static int root;                        /* the device on the root port, -1 none */
static dev_t_ *dflt;                    /* the device answering at address 0 */

static uint16_t build_kbd_cfg(uint8_t *d, int with_out)
{
    static const uint8_t body[] = {
        9, 4, 0, 0, 0, 1, 1, 0, 0,                      /* IF0 audio control */
        9, 0x24, 1, 0, 1, 9, 0, 1, 1,
        9, 4, 1, 0, 2, 1, 3, 0, 0,                      /* IF1 MIDI streaming, two endpoints */
        7, 0x24, 1, 0, 1, 0x41, 0,
        6, 0x24, 2, 1, 1, 0,
        9, 0x24, 3, 1, 3, 1, 2, 1, 0,
        9, 5, 0x02, 2, 64, 0, 0, 0, 0,                  /* OUT first, as many devices list it */
        5, 0x25, 1, 1, 1,
        9, 5, 0x81, 2, 64, 0, 0, 0, 0,                  /* EP1 IN bulk */
        5, 0x25, 1, 1, 3,
    };
    uint16_t n = 9u + sizeof body;
    static const uint8_t head[9] = {9, 2, 0, 0, 2, 1, 0, 0x80, 50};
    memcpy(d, head, 9);
    memcpy(d + 9, body, sizeof body);
    if (!with_out) {
        memmove(d + 58, d + 72, n - 72u);              /* remove OUT and its class-specific descriptor */
        n -= 14;
        d[31] = 1;
    }
    d[2] = (uint8_t)n;
    d[3] = (uint8_t)(n >> 8);
    return n;
}

static void devices_init(void)
{
    static const uint8_t stick[] = {9, 2, 32, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 2, 8, 6, 0x50, 0,
                                    7, 5, 0x81, 2, 64, 0, 0, 7, 5, 0x02, 2, 64, 0, 0};
    static const uint8_t hub[] = {9, 2, 25, 0, 1, 1, 0, 0xE0, 50, 9, 4, 0, 0, 1, 9, 0, 0, 0,
                                  7, 5, 0x81, 3, 1, 0, 12};
    memset(dev, 0, sizeof dev);
    kbd_cfg_len = build_kbd_cfg(kbd_cfg, 1);
    memcpy(stick_cfg, stick, sizeof stick);
    memcpy(hub_cfg, hub, sizeof hub);
    dev[KBD] = (dev_t_){.present = 1, .cls = 0, .mps0 = 64, .cfgd = kbd_cfg, .cfglen = kbd_cfg_len};
    dev[HUB] = (dev_t_){.present = 1, .cls = 9, .mps0 = 64, .cfgd = hub_cfg, .cfglen = 25, .ports = 4};
    dev[STICK] = (dev_t_){.present = 1, .cls = 0, .mps0 = 8, .cfgd = stick_cfg, .cfglen = 32};
    dev[HUB2] = (dev_t_){.present = 1, .cls = 9, .mps0 = 64, .cfgd = hub_cfg, .cfglen = 25, .ports = 4};
    dev[HUB3] = (dev_t_){.present = 1, .cls = 9, .mps0 = 64, .cfgd = hub_cfg, .cfglen = 25, .ports = 4};
    root = -1;
    dflt = 0;
}

/* --------------------------------------------------- the SIE and bus --- */
static uint8_t regs[4][32], common[16], index_reg, rsp[2048];
static uint32_t rsp_len, rsp_pos, ep_cnt[4];
static void *ep_radr[4];
static void *ep_tadr[4];
static uint32_t USB_CON0, line_state;
static uint8_t bus_dead;                /* nothing answers (cable pulled) */
static uint8_t ep0_timeout;             /* SETUP stays busy until the driver's deadline */
static uint8_t devctl_ls;               /* the device is low speed */
static uint32_t slow_until;             /* nothing answers before this many bus resets (a hub still starting) */
static uint32_t ctl_requests, setup_requests, rx_toggle_clears;
static uint32_t tx_toggle_clears, off_count;
static uint32_t root_resets, host_attaches, port_resets[8];
static uint32_t wire[512], wire_n;
static dev_t_ *pend_dev;
static uint8_t pend_addr;
static uint8_t rx_q[8][64];
static uint8_t rx_n[8];
static uint32_t rx_head, rx_tail;

static dev_t_ *by_addr(uint8_t a)
{
    int i;
    if (a == 0)
        return dflt;
    for (i = 0; i < NDEV; i++)
        if (dev[i].present && dev[i].addr == a && !dev[i].in_default)
            return &dev[i];
    return 0;
}

static void bus_reset(void)
{
    int i;
    root_resets++;
    for (i = 0; i < NDEV; i++) {
        dev[i].addr = 0;
        dev[i].cfg = 0;
        memset(dev[i].port_pow, 0, sizeof dev[i].port_pow);
        memset(dev[i].port_en, 0, sizeof dev[i].port_en);
        memset(dev[i].port_chg, 0, sizeof dev[i].port_chg);
    }
    dflt = root >= 0 ? &dev[root] : 0;
}

static int hub_port_dev(dev_t_ *h, uint32_t port)
{
    return port >= 1 && port <= h->ports ? (int)h->port_dev[port] - 1 : -1;
}

/* a SETUP packet to `d`: fill rsp, return 1 if answered, 0 if stalled */
static int answer(dev_t_ *d, const uint8_t *s)
{
    uint8_t rt = s[0], rq = s[1];
    uint16_t val = (uint16_t)(s[2] | s[3] << 8), idx = (uint16_t)(s[4] | s[5] << 8), len = (uint16_t)(s[6] | s[7] << 8);
    uint32_t n = 0;
    rsp_len = rsp_pos = 0;
    ctl_requests++;
    if (rt == 0x80 && rq == 6 && (val >> 8) == 1) {
        d->seen_probe++;
        if (d->fail_probes) {
            d->fail_probes--;
            return 0;
        }
        uint8_t dd[18] = {18, 1, 0x00, 0x02, d->cls, 0, 0, d->mps0, 0x09, 0x12, 1, 0, 0, 1, 1, 2, 0, 1};
        memcpy(rsp, dd, 18);
        n = 18;
    } else if (rt == 0x80 && rq == 6 && (val >> 8) == 2) {
        memcpy(rsp, d->cfgd, d->cfglen);
        n = d->cfglen;
    } else if (rt == 0xA0 && rq == 6 && val == 0x2900 && d->ports) {
        uint8_t hd[9] = {9, 0x29, d->ports, 0, 0, 10, 0, 0, 0xFF};
        memcpy(rsp, hd, 9);
        n = 9;
    } else if (rt == 0x00 && rq == 5) {
        pend_dev = d;                                   /* takes effect after the status stage */
        pend_addr = (uint8_t)val;
    } else if (rt == 0x00 && rq == 9) {
        d->cfg = (uint8_t)val;
        d->seen_set_cfg++;
    } else if (rt == 0x01 && rq == 11) {
        d->alt = (uint8_t)val;
        d->seen_set_if++;
    } else if (rt == 0x23 && rq == 3 && d->ports) {
        int c = hub_port_dev(d, idx);
        if (val == 8)
            d->port_pow[idx] = 1;
        if (val == 4 && d->port_pow[idx]) {
            port_resets[idx]++;
            d->port_en[idx] = 1;
            d->port_chg[idx] |= 0x10;
            if (c >= 0) {
                dev[c].addr = 0;
                dev[c].in_default = 1;
                dflt = &dev[c];
            }
        }
    } else if (rt == 0x23 && rq == 1 && d->ports) {
        if (val == 16)
            d->port_chg[idx] &= (uint8_t)~1u;
        if (val == 20)
            d->port_chg[idx] &= (uint8_t)~0x10u;
    } else if (rt == 0xA3 && rq == 0 && d->ports) {
        int c = hub_port_dev(d, idx);
        uint32_t st = (c >= 0 && d->port_pow[idx] ? 1u : 0u) | (d->port_en[idx] ? 2u : 0u) | 0x100u |
                      (d->port_ls[idx] ? 0x200u : 0u);
        rsp[0] = (uint8_t)st;
        rsp[1] = (uint8_t)(st >> 8);
        rsp[2] = d->port_chg[idx];
        rsp[3] = 0;
        n = 4;
    } else {
        return 0;
    }
    rsp_len = n < len ? n : len;
    return 1;
}

static void deliver_rx(void)            /* an IN request is pending on EP1 and the keyboard has something */
{
    dev_t_ *d;
    if (regs[1][20] & 1u || !(regs[1][20] & 0x20u))
        return;
    if (bus_dead) {
        regs[1][20] = 0x04;
        return;
    }
    d = by_addr(common[0]);
    if (!d || d != &dev[KBD] || rx_head == rx_tail)
        return;
    memcpy(ep_radr[1], rx_q[rx_tail % 8], rx_n[rx_tail % 8]);
    regs[1][22] = rx_n[rx_tail % 8];
    regs[1][23] = 0;
    rx_tail++;
    regs[1][20] = 1;
}

static void mock_write(uint32_t r, uint32_t v)
{
    if (r == 14) {
        index_reg = (uint8_t)v;
    } else if (r == 1 && (v & 8u)) {                    /* POWER: bus reset */
        bus_reset();
        common[r] = (uint8_t)v;
    } else if (r < 16) {
        common[r] = (uint8_t)v;
    } else if (index_reg == 0 && r == 17) {             /* EP0 CSR, host */
        dev_t_ *d = by_addr(common[0]);
        if (v == 0x0A) {
            setup_requests++;
            uint8_t s[8];
            memcpy(s, ep_radr[0], 8);
            if (ep0_timeout)
                regs[0][17] = 2;
            else if (bus_dead || !d || root_resets < slow_until || !answer(d, s))
                regs[0][17] = 0x10;                     /* no answer */
            else
                regs[0][17] = 0;
        } else if (v == 0x20) {
            uint32_t mps = d && d->mps0 ? d->mps0 : 8u, n = rsp_len - rsp_pos;
            if (n > mps)
                n = mps;
            memcpy(ep_radr[0], rsp + rsp_pos, n);
            rsp_pos += n;
            regs[0][22] = (uint8_t)n;
            regs[0][17] = 1;
        } else if (v == 0x60) {
            regs[0][22] = 0;
            regs[0][17] = bus_dead || !d ? 0x10 : 1;
            if (pend_dev) {
                pend_dev->addr = pend_addr;
                pend_dev->in_default = 0;
                if (pend_dev == dflt)
                    dflt = 0;
                pend_dev = 0;
            }
        } else if (v == 0x42) {
            regs[0][17] = bus_dead || !d ? 0x10 : 0;
        } else {
            regs[0][17] = (uint8_t)v;
        }
    } else if (index_reg == 1 && r == 17) {
        if (v & 0x40u) tx_toggle_clears++;
        regs[1][17] = v & 1u ? 3u : 0u;               /* TXPKTRDY/FIFONotEmpty until an explicit ACK */
    } else if (index_reg == 1 && r == 20) {
        if (v == 0x20) {
            regs[1][20] = 0x20;
            deliver_rx();
        } else if (v == 0x10) {
            regs[1][20] &= (uint8_t)~1u;
        } else if (v == 0x80) {
            rx_toggle_clears++;                         /* ClrDataTog with nothing else */
            regs[1][20] = 0;
        } else {
            regs[1][20] = (uint8_t)v;
        }
    } else {
        regs[index_reg][r] = (uint8_t)v;
    }
}
static uint8_t last_read;
static void mock_rd(uint32_t r) { last_read = (uint8_t)r; }
static uint32_t mock_dt(void)
{
    uint32_t r = last_read;
    if (r == 15)
        return devctl_ls ? 0x20u : 0x40u;
    return r < 16 ? common[r] : regs[index_reg][r];
}
static void mock_enable(uint32_t eps) { (void)eps; }
static void mock_txbuf(uint32_t ep, void *p) { ep_tadr[ep] = p; }
static void mock_rxbuf(uint32_t ep, void *p) { ep_radr[ep] = p; }
static void mock_send(uint32_t ep, void *p, uint32_t n) { ep_tadr[ep] = p; ep_cnt[ep] = n; }
static void mock_ep0_send(void *p, uint32_t n) { mock_send(0, p, n); }
static void mock_ep0_buf(void *p) { ep_radr[0] = p; }
static void mock_nop(void) {}
static void mock_off(void) { off_count++; USB_CON0 = 0; }
static void mock_attach(void *p) { (void)p; }
static void mock_host_attach(void *p) { host_attaches++; ep_radr[0] = p; USB_CON0 = 0x25; }
static uint32_t mock_one(void) { return 1; }
static uint32_t mock_zero(void) { return 0; }
static uint32_t mock_line(void) { return line_state; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }

#define fm1_usb_ep_enable mock_enable
#define fm1_usb_ep_txbuf mock_txbuf
#define fm1_usb_ep_rxbuf mock_rxbuf
#define fm1_usb_ep_send mock_send
#define fm1_usb_ep0_send mock_ep0_send
#define fm1_usb_ep0_buf mock_ep0_buf
#define fm1_usb_rx_sync mock_nop
#define fm1_usb_sie_on mock_one
#define fm1_usb_sie_done mock_one
#define fm1_usb_sie_wr_start mock_write
#define fm1_usb_sie_rd_start mock_rd
#define fm1_usb_sie_data mock_dt
#define fm1_usb_reset mock_nop
#define fm1_usb_off mock_off
#define fm1_usb_attach mock_attach
#define fm1_usb_host_attach mock_host_attach
#define fm1_usb_line mock_line
#define fm1_usb_sof_take mock_zero
#define fm1_wdt_feed() (fm1_ms++)                       /* the host code's waits run on this clock */
#define RING_PUBLISH() ((void)0)
#define FELUCCA_UAC 1
#define FELUCCA_CDC 0
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 0                                   /* (run_tests.sh also builds it 1: SysEx out) */
#endif
#define CR_USB_HOST 1                                   /* choralroot.c's */
#include "../firmware/src/usb.c"
#if FELUCCA_OTA
static uint32_t ota_now_ms(void) { return fm1_ms; }   /* ota.c's (ota_wire_send's wait: never called here) */
static void ota_idle(void) {}
#endif

/* ChoralRoot's MIDI out is midi_out_event (the audio ISR; it says nothing back): queued = the ring took it */
static int midi_track_emit(uint32_t pkt)
{
    uint32_t w = mo_w;
    midi_out_event(pkt);
    return mo_w != w;
}
static void midi_sync_emit(uint8_t status) { midi_out_event(0x0Fu | (uint32_t)status << 8); }   /* cr_out.c's way */

/* a session ended: the release request is up; the audio ISR's recovery (cr_out.c events_block) takes it */
static void isr_recovery(void)
{
    assert(midi_in_overflow);
    mi_r = mi_w;
    midi_in_overflow = 0;
}

/* ---------------------------------------------------------- the tests --- */
static void connect(int which)
{
    devices_init();
    root = which;
    bus_dead = devctl_ls = ep0_timeout = 0;
    slow_until = 0;
    memset(common, 0, sizeof common);
    memset(regs, 0, sizeof regs);
    dflt = 0;
    rx_head = rx_tail = 0;
    mi_r = mi_w = 0;
    mo_r = mo_w = 0;
    wire_n = 0;
    root_resets = host_attaches = 0;
    memset(port_resets, 0, sizeof port_resets);
    midi_in_overflow = 0;
    if (which == HUB) {
        dev[HUB].port_dev[1] = STICK + 1;               /* port 1: a mass-storage stick, port 3: the keyboard */
        dev[HUB].port_dev[3] = KBD + 1;
    }
    line_state = 2;
    uh.pkts = uh.events = 0;
}

static void host_on(void)
{
    uh.want = 1;
    uh_task(fm1_ms);
    assert(uh.on && uh.msg == UH_MSG_ON && !usb.up);
    assert(!midi_in_overflow);                          /* switching the host on ends no session: nothing to release */
    uh.msg = 0;
}

static int enumerate(void)                              /* the main loop until run or give-up */
{
    int k;
    for (k = 0; k < 400 && !uh.run; k++) {
        fm1_ms += 20;
        uh_task(fm1_ms);
    }
    return uh.run;
}

static void push_packet(uint32_t pkt)
{
    uint8_t *q = rx_q[rx_head % 8];
    q[0] = (uint8_t)pkt;
    q[1] = (uint8_t)(pkt >> 8);
    q[2] = (uint8_t)(pkt >> 16);
    q[3] = (uint8_t)(pkt >> 24);
    rx_n[rx_head % 8] = 4;
    rx_head++;
}

static void poll_n(int n)
{
    while (n--) {
        usb_poll();
        deliver_rx();
    }
}

static void test_parse(void)
{
    uh_midi_t m;
    uint8_t cut[128];
    assert(uh_parse_midi(kbd_cfg, kbd_cfg_len, &m));
    assert(m.found && m.cfg == 1 && m.iface == 1 && m.alt == 0 && m.in_ep == 1 && m.in_type == 2 &&
           m.in_mps == 64 && m.out_ep == 2);
    assert(!uh_parse_midi(stick_cfg, 32, &m) && !m.found);
    assert(!uh_parse_midi(hub_cfg, 25, &m));
    memcpy(cut, kbd_cfg, kbd_cfg_len);
    assert(uh_parse_midi(cut, 76, &m) && m.out_ep && !m.in_ep); /* OUT remains usable when IN is truncated */
    assert(uh_parse_midi(cut, kbd_cfg_len, &m));
    cut[72] = 0;                                            /* a zero-length descriptor ends the walk safely */
    assert(uh_parse_midi(cut, kbd_cfg_len, &m) && m.out_ep && !m.in_ep);
    assert(uh_midi_ok(0x643C9009u) == 0x643C9009u);        /* note on ch0 */
    assert(uh_midi_ok(0x643C9019u) == 0x643C9009u);        /* cable 1 is dropped */
    assert(uh_midi_ok(0x0000F80Fu) == 0x0000F80Fu);        /* clock */
    assert(uh_midi_ok(0x0201F203u) == 0x0201F203u);        /* Song Position */
    assert(uh_midi_ok(0x0280F203u) == 0 && uh_midi_ok(0x8001F203u) == 0 && uh_midi_ok(0x0201F303u) == 0);   /* bad data, other common */
    assert(uh_midi_ok(0x0000F70Fu) == 0 && uh_midi_ok(0x4F7DF004u) == 0);   /* SysEx, other realtime */
    assert(uh_midi_ok(0x643C8009u) == 0);                  /* CIN does not match status */
    assert(uh_midi_ok(0x003CC00Cu) == 0x003CC00Cu);        /* program change */
    assert(uh_midi_ok(0x64BC9009u) == 0 && uh_midi_ok(0xE43C9009u) == 0);   /* data bytes with bit 7 */
    puts("USB host: descriptor walk and packet filter: OK");
}

static void test_keyboard(int via_hub)
{
    connect(via_hub ? HUB : KBD);
    host_on();
    assert(enumerate());
    assert(uh.run && uh.msg == UH_MSG_MIDI);
    uh.msg = 0;
    assert(dev[KBD].cfg == 1 && dev[KBD].addr == uh.dev && dev[KBD].addr != 0);
    if (via_hub) {
        assert(dev[HUB].cfg == 1 && dev[HUB].addr == 1);
        assert(dev[STICK].addr != 0 && dev[STICK].addr != dev[KBD].addr);   /* the stick was addressed, then passed over */
        assert(dev[KBD].addr == 4);                    /* stable address per hub port */
    } else {
        assert(dev[KBD].addr == 1);
    }
    assert(common[0] == dev[KBD].addr);                 /* FADDR is the keyboard's for the endpoint */
    assert(regs[1][26] == (2 << 4 | 1));                /* RXTYPE: bulk, endpoint 1 */
    assert(regs[1][19] == 0xFF);

    /* events: note on, SysEx (dropped), clock, note on from cable 1 */
    push_packet(0x643C9009u);                           /* cin 9: 90 3C 64 */
    push_packet(0x4F7DF004u);                           /* SysEx start: dropped */
    push_packet(0x0000F80Fu);                           /* clock */
    push_packet(0x7F409119u);                           /* cable 1 note on ch1 */
    poll_n(8);
    assert(mi_w - mi_r == 3);
    assert(midi_in_q[mi_r % MQ] == 0x643C9009u && midi_in_source[mi_r % MQ] == 1);
    assert(midi_in_q[(mi_r + 1) % MQ] == 0x0000F80Fu);
    assert(midi_in_q[(mi_r + 2) % MQ] == 0x7F409109u);  /* the cable bits are gone */
    assert(uh.pkts == 4 && uh.events == 3);

    /* a full ring holds the packet back instead of overflowing */
    mi_r = mi_w = 0;
    mi_w = MQ - 10u;                                    /* fewer than EP1_ROOM free */
    mi_r = 0;
    push_packet(0x643C9009u);
    poll_n(4);
    assert(mi_w == MQ - 10u && !midi_in_overflow);
    mi_r = mi_w;                                        /* the main loop drained it */
    poll_n(2);
    assert(mi_w == mi_r + 1u);
    mi_r = mi_w;

    /* the cable goes: errors, then a fault the main loop recovers from */
    bus_dead = 1;
    line_state = 0;
    rx_toggle_clears = 0;
    poll_n(40);
    assert(!uh.run && uh.fault);
    assert(rx_toggle_clears == 0);                      /* errors never reset the data toggle */
    fm1_ms += 20;
    uh_task(fm1_ms);
    assert(!uh.fault && !uh.run && uh.msg == UH_MSG_LOST);
    uh.msg = 0;
    isr_recovery();                                     /* the unplug released every part */
    for (int k = 0; k < 100; k++) {                     /* nothing attached: nothing happens */
        fm1_ms += 20;
        uh_task(fm1_ms);
    }
    assert(!uh.run && uh.msg == 0);

    /* plugged in again */
    bus_dead = 0;
    bus_reset();
    line_state = 2;
    assert(enumerate() && uh.msg == UH_MSG_MIDI);
    uh.msg = 0;
    assert(!midi_in_overflow);                          /* replugged: nothing more to release */
    push_packet(0x643C9009u);
    poll_n(4);
    assert(mi_w - mi_r == 1 && midi_in_q[mi_r % MQ] == 0x643C9009u);
    mi_r = mi_w;

    /* back to a device: the host leaves the port, and what the device was playing is released */
    uh.want = 0;
    uh_task(fm1_ms);
    assert(!uh.on && !uh.run && uh.msg == UH_MSG_OFF);
    uh.msg = 0;
    isr_recovery();
    usb.up = 0;
    usb_retry(fm1_ms + 5000u);                          /* usb_start again as a device */
    assert(usb.up);
    usb.up = 0;
    printf("USB host: keyboard %s: enumeration, events, ring back-pressure, unplug, replug, back to device: OK\n",
           via_hub ? "behind a hub" : "direct");
}

static void test_no_midi(void)
{
    connect(STICK);
    host_on();
    assert(!enumerate());
    assert(uh.msg == UH_MSG_NOMIDI);                    /* said once, then quiet */
    assert(root_resets == 1 && host_attaches == 1 && dev[STICK].seen_probe == 1);
    uh.msg = 0;
    {
        int k;
        for (k = 0; k < 400; k++) {
            fm1_ms += 20;
            uh_task(fm1_ms);
        }
    }
    assert(uh.msg == 0 && !uh.run);
    assert(root_resets == 1 && host_attaches == 1 && dev[STICK].seen_probe == 1);
    /* A real root unplug rearms detection, even after an unsupported device. */
    line_state = 0;
    for (int k = 0; k < 5; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    root = KBD; line_state = 2;
    assert(enumerate() && uh.msg == UH_MSG_MIDI && root_resets == 2);
    uh.want = 0;
    uh_task(fm1_ms);
    uh.msg = 0;

    connect(KBD);
    devctl_ls = 1;
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_LOW);
    assert(!root_resets && host_attaches == 1);
    uh.msg = 0;
    for (int k = 0; k < 600; k++) {                     /* 12 s with the device still attached: said once */
        fm1_ms += 20;
        uh_task(fm1_ms);
    }
    assert(uh.msg == 0 && !uh.run);
    assert(!root_resets && host_attaches == 1);
    uh.want = 0;
    uh_task(fm1_ms);
    uh.msg = 0;
    puts("USB host: a stick without MIDI, a low-speed device: reported, no endpoint started: OK");
}

/* Idle hub polls may use EP0, but must never reset/reconfigure devices or enter any timed waits. */
static void idle_hub(int ticks)
{
    uint32_t resets = root_resets, attaches = host_attaches, probes = dev[STICK].seen_probe;
    uint32_t configs = dev[HUB].seen_set_cfg;
    uint32_t ports[8];
    memcpy(ports, port_resets, sizeof ports);
    while (ticks--) {
        fm1_ms += 20;
        uint32_t now = fm1_ms;
        uh_task(now);
        assert(fm1_ms == now && !uh.run && !uh.msg);
    }
    assert(root_resets == resets && host_attaches == attaches && dev[STICK].seen_probe == probes);
    assert(!memcmp(ports, port_resets, sizeof ports));
    assert(dev[HUB].cfg == 1 && dev[HUB].seen_set_cfg == configs);
}

static void test_idle_hub(int with_stick)
{
    connect(HUB);
    dev[HUB].port_dev[3] = 0;
    if (!with_stick) dev[HUB].port_dev[1] = 0;
    host_on();
    assert(!enumerate() && uh.msg == (with_stick ? UH_MSG_HUB_NOMIDI : UH_MSG_HUB_EMPTY));
    assert(root_resets == 1 && host_attaches == 1);
    assert(dev[STICK].seen_probe == (uint32_t)with_stick);
    uh.msg = 0;
    idle_hub(3000);                                    /* one minute: no reset, power-good or reset waits */

    /* An unsupported downstream attachment is tried once, and its change is acknowledged. */
    dev[HUB].port_dev[1] = STICK + 1;
    dev[HUB].port_chg[1] |= 1;
    uint32_t probes = dev[STICK].seen_probe;
    assert(!enumerate() && dev[STICK].seen_probe == probes + 1 && !dev[HUB].port_chg[1]);
    assert(uh.msg == UH_MSG_HUB_NOMIDI); uh.msg = 0;   /* what was plugged in is said, not skipped silently */
    idle_hub(600);
    dev[HUB].port_dev[1] = 0;
    dev[HUB].port_en[1] = 0;
    dev[HUB].port_chg[1] |= 1;                         /* disconnects are acknowledged without port reset */
    idle_hub(100);
    assert(!dev[HUB].port_chg[1]);

    /* An idle MIDI hot-plug resets only its port, preserving the powered hub and the stick. */
    uint32_t stick_resets = port_resets[1];
    dev[HUB].port_dev[3] = KBD + 1;
    dev[HUB].port_chg[3] |= 1;
    assert(enumerate() && uh.msg == UH_MSG_MIDI);
    assert(root_resets == 1 && host_attaches == 1 && port_resets[1] == stick_resets);
    assert(dev[KBD].addr == 4 && port_resets[3] == 1 && !dev[HUB].port_chg[3]);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    printf("USB host: idle hub %s: no repeated resets/waits, downstream hot-plug: OK\n",
           with_stick ? "with a stick" : "empty");
}

static void test_failed_probe(void)
{
    connect(-1);                                      /* a powered line with nothing answering EP0 */
    ep0_timeout = 1;
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_NOANSWER && !strcmp(uh.dg, "DEV D5"));
    uh.msg = 0;
    uint32_t requests = setup_requests;
    for (int k = 0; k < 3000; k++) {
        fm1_ms += 20;
        uint32_t now = fm1_ms;
        uh_task(now);
        assert(fm1_ms == now && !uh.run && !uh.msg);
    }
    assert(root_resets == UH_ROOT_TRIES && host_attaches == 1 && setup_requests == requests);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    connect(HUB);
    memset(dev[HUB].port_dev, 0, sizeof dev[HUB].port_dev);
    host_on(); assert(!enumerate()); uh.msg = 0;
    ep0_timeout = 1;                                  /* configured hub stops answering without line drop */
    for (int k = 0; k < 30; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(!uh.hub[0].addr && root_resets == 1);
    requests = setup_requests;
    idle_hub(600);
    assert(setup_requests == requests);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    puts("USB host: failed EP0 probe and unresponsive idle hub wait for unplug/replug: OK");
}

static void test_hub_synth_unplug(void)
{
    static const uint8_t synth[] = {9,2,25,0,1,1,0,0x80,50, 9,4,1,0,1,1,3,0,0, 7,5,2,2,8,0,0};
    connect(HUB);
    memcpy(kbd_cfg, synth, sizeof synth);
    dev[KBD].cfglen = sizeof synth;
    host_on(); assert(enumerate() && !uh.in_ep && uh.out_mps);
    assert(midi_track_emit(0x643c9009u)); usb_poll();
    dev[HUB].port_dev[3] = 0;
    dev[HUB].port_en[3] = 0;
    dev[HUB].port_chg[3] |= 1;
    dev[KBD].present = 0;
    /* OUT errors while the root hub remains on the line. */
    for (uint32_t k = 0; k < UH_MAX_ERRORS; k++) { regs[1][17] = 4; usb_poll(); }
    assert(uh.fault && !uh.run && line_state == 2);
    uh_task(fm1_ms); assert(!uh.fault && uh.msg == UH_MSG_LOST && mo_r == mo_w);
    uh.msg = 0;
    isr_recovery();                                    /* the note it was sent is released */
    assert(!enumerate() && uh.msg == UH_MSG_HUB_NOMIDI);
    uh.msg = 0;
    assert(root_resets == 2 && host_attaches == 2);
    idle_hub(3000);
    dev[KBD].present = 1;
    dev[HUB].port_dev[3] = KBD + 1;
    dev[HUB].port_chg[3] |= 1;
    assert(enumerate() && !uh.in_ep && uh.out_mps && uh.msg == UH_MSG_MIDI);
    assert(root_resets == 2 && host_attaches == 2 && dev[KBD].addr == 4);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    puts("USB host: unplugged OUT-only synth leaves hub idle without retry resets; replug works: OK");
}

static void ack_tx(void)
{
    const uint8_t *p = ep_tadr[1];
    assert(regs[1][17] & 1u);
    assert(ep_cnt[1] && ep_cnt[1] <= uh.out_mps && !(ep_cnt[1] & 3u));
    for (uint32_t i = 0; i < ep_cnt[1]; i += 4u) {
        assert(wire_n < sizeof wire / sizeof wire[0]);
        wire[wire_n++] = (uint32_t)p[i] | (uint32_t)p[i+1] << 8 | (uint32_t)p[i+2] << 16 | (uint32_t)p[i+3] << 24;
    }
    regs[1][17] = 0;                                   /* ACK empties the FIFO and clears TXPKTRDY */
}

static void recover_host(void)
{
    uh_task(fm1_ms);
    assert(!uh.run && !uh.fault && mo_r == mo_w);
    assert(!midi_track_emit(0x643c9009u));              /* nothing admitted until the device is back */
    isr_recovery();                                    /* midi_in_overflow: the audio ISR releases every part */
    assert(enumerate());
}

static void test_host_output(int via_hub)
{
    connect(via_hub ? HUB : KBD);
    host_on(); assert(enumerate());
    assert(usb_midi_out_ready() && !usb.config);
    line_state = 0; usb_poll(); assert(uh.run);        /* an EOP sample is not an unplug */
    line_state = 2; usb_poll(); assert(uh.run && !uh.line_idle);
    assert(regs[1][24] == 0x22 && regs[1][25] == 0 && regs[1][18] == 0x20);
    uint32_t first = mo_r, toggles = tx_toggle_clears;
    for (uint32_t i = 0; i < MQ; i++)
        assert(midi_track_emit(0x09u | (0x90u | (i & 15u)) << 8 | i << 16 | 93u << 24));
    assert(!midi_track_emit(0x003c8008u));               /* full: releases remain CPU0's responsibility */
    usb_poll();
    assert(uh.tx_count == 16 && mo_r == first && ep_cnt[1] == 64);
    uint8_t pending[64]; memcpy(pending, ep_tadr[1], 64);
    push_packet(0x643c9009u); poll_n(8);                /* input continues while output is busy */
    assert(mi_w == 1 && mo_r == first && !memcmp(pending, ep_tadr[1], 64));
    regs[1][17] = 0x80; usb_poll();                    /* NAK: retain ring and data */
    assert(mo_r == first && tx_toggle_clears == toggles);
    regs[1][17] = 0x04; usb_poll();                    /* transient bus error: same packet/toggle */
    assert(mo_r == first && !memcmp(pending, ep_tadr[1], 64) && tx_toggle_clears == toggles);
    while (mo_r != mo_w) { ack_tx(); usb_poll(); }
    assert(wire_n == MQ && !uh.tx_count);
    for (uint32_t i = 0; i < MQ; i++)
        assert(wire[i] == (0x09u | (0x90u | (i & 15u)) << 8 | i << 16 | 93u << 24));
    /* Ring wrap and short packets preserve release/controller order. */
    assert(midi_track_emit(0x003c8008u)); assert(midi_track_emit(0x000040bbu));
    usb_poll(); assert(ep_cnt[1] == 8 && mo_w - mo_r == 2);
    ack_tx(); usb_poll(); assert(wire[MQ] == 0x003c8008u && wire[MQ+1] == 0x000040bbu);
    /* Clock and transport share ChoralRoot's one ring: they leave in the order queued, keeping slots until ACK. */
    uint32_t sent = wire_n, pulse = 0xFu | 0xF8u << 8, start = 0xFu | 0xFAu << 8;
    assert(midi_track_emit(0x003d8008u));
    midi_sync_emit(0xF8u); midi_sync_emit(0xFAu);
    usb_poll();
    assert(ep_cnt[1] == 12 && uh.tx_count == 3 && mo_w - mo_r == 3);
    midi_sync_emit(0xF8u);                             /* arrives while that packet is out */
    regs[1][17] = 0x80; usb_poll();                    /* NAK: every slot retained */
    assert(mo_w - mo_r == 4 && uh.tx_count == 3);
    ack_tx(); usb_poll();
    assert(wire[sent] == 0x003d8008u && wire[sent + 1] == pulse && wire[sent + 2] == start);
    assert(mo_w - mo_r == 1 && ep_cnt[1] == 4 && uh.tx_count == 1);
    ack_tx(); usb_poll();
    assert(wire[sent + 3] == pulse && mo_r == mo_w && !uh.tx_count);
    /* A stall discards old queued note-ons and requests cleanup, then re-enumerates. */
    assert(midi_track_emit(0x643c9009u)); usb_poll(); regs[1][17] = 0x20; usb_poll();
    assert(uh.fault && !uh.run && !midi_track_emit(0x643d9009u)); recover_host();
    assert(mo_r == mo_w && !uh.tx_count);
    /* Persistent errors and an unresponsive/NAKing output have bounded recovery. */
    assert(midi_track_emit(0x643c9009u)); usb_poll();
    for (uint32_t i = 0; i < UH_MAX_ERRORS; i++) { regs[1][17] = 4; usb_poll(); }
    assert(uh.fault); recover_host();
    assert(midi_track_emit(0x643c9009u)); usb_poll(); fm1_ms += 500; usb_poll();
    assert(uh.fault); recover_host();
    /* Device audio/capture preferences wait for HOST OFF, never resetting active host DMA. */
    uint32_t off = off_count, type = regs[1][24];
    ua_off_set(UA_OFF_IN, !!(ua_off & UA_OFF_IN), fm1_ms); fm1_ms += 1000;
    assert(!ua_off_apply(fm1_ms) && uh.run && off_count == off && regs[1][24] == type);
    usb_replug(fm1_ms); assert(uh.run && off_count == off);
    uh.want = 0; uh_task(fm1_ms);
    isr_recovery();                                    /* USB Host Off releases what the device was playing */
    assert(ua_off_apply(fm1_ms) == 1 && !uh.on && !usb_midi_out_ready());
    usb_retry(fm1_ms + 1000); usb.config = 1;
    assert(midi_track_emit(0x643c9009u));               /* ordinary computer-facing output remains available */
    midi_sync_emit(0xF8u);                             /* ... the clock behind the note, as queued; never in a SysEx */
#if FELUCCA_OTA
    /* A real-time byte inside a SysEx would reach the host's clients as part of the message: ep1_tx sends the SysEx
     * ring first and holds the MIDI ring while one is being composed (sx_busy). */
    sx_busy = 1; sx_out_q[so_w++ % SXQ] = 0x467DF004u;
    regs[1][17] = 0; usb_poll();
    assert(ep_cnt[1] == 4 && so_r == so_w && mo_w - mo_r == 2);
    regs[1][17] = 0; usb_poll();                       /* (still composing it) */
    assert(mo_w - mo_r == 2);
    sx_busy = 0;
#endif
    regs[1][17] = 0; usb_poll();
    assert(mo_r == mo_w && ep_cnt[1] == 8);
    const uint8_t *out = ep_tadr[1];
    assert(out[0] == 0x09u && out[4] == 0x0Fu && out[5] == 0xF8u);
    usb.config = usb.up = 0; mo_r = mo_w;
    printf("USB host OUT %s: duplex, ACK ownership, NAK/error retries, bounded faults, mode settings: OK\n",
           via_hub ? "behind a hub" : "direct");
}

static void test_endpoint_selection(void)
{
    uh_midi_t m;
    uint8_t d[] = {9,2,41,0,1,1,0,0x80,50,
                  9,4,2,1,1,1,3,0,0, 7,5,2,2,8,0,0,
                  9,4,2,2,1,1,3,0,0, 7,5,0x81,2,64,0,0};
    assert(uh_parse_midi(d, sizeof d, &m) && m.alt == 1 && m.out_ep == 2 && m.out_mps == 8 && !m.in_ep);
    d[14] = 0;                                        /* first alternate isn't MIDI: use second */
    assert(uh_parse_midi(d, sizeof d, &m) && m.alt == 2 && m.in_ep == 1 && !m.out_ep);
    d[32] = 2;                                        /* MIDI 2.0 protocol is not a MIDI 1.0 event stream */
    assert(!uh_parse_midi(d, sizeof d, &m));
    d[32] = 0; d[38] = 0;                             /* invalid packet size */
    assert(!uh_parse_midi(d, sizeof d, &m));
    /* A receive-only controller rejects output; an output-only synth is still enumerated. */
    connect(KBD); kbd_cfg_len = build_kbd_cfg(kbd_cfg, 0);
    host_on(); assert(enumerate() && uh.in_ep && !uh.out_mps && !midi_track_emit(0x643c9009u));
    uh.want = 0; uh_task(fm1_ms);
    connect(KBD);
    kbd_cfg_len = 25; memcpy(kbd_cfg, d, 25); kbd_cfg[14] = 1;
    kbd_cfg[2] = 25;
    host_on(); assert(enumerate() && !uh.in_ep && uh.out_mps == 8 && dev[KBD].alt == 1);
    for (uint32_t i = 0; i < 3; i++) assert(midi_track_emit(0x643c9009u + (i << 16)));
    usb_poll(); assert(ep_cnt[1] == 8); ack_tx(); usb_poll();
    assert(ep_cnt[1] == 4); ack_tx(); usb_poll(); assert(wire_n == 3 && mo_r == mo_w);
    line_state = 0; poll_n(16); assert(!uh.run && uh.fault); /* idle output-only unplug still detected */
    assert(!midi_track_emit(0x643c9009u));
    uh_task(fm1_ms); assert(uh.msg == UH_MSG_LOST && !uh.fault); uh.msg = 0;
    isr_recovery();                                    /* the session's end: every part released */
    uh.want = 0; uh_task(fm1_ms);
    puts("USB host: output-only synth, input-only controller, alternate boundaries, protocol/packet validation: OK");
}

/* An audio interface with MIDI: IAD, audio control, two audio streaming interfaces of three formats with eight
 * rates each, then the MIDI streaming interface, then DFU. *midi_at = where the MIDI interface starts. */
static uint16_t build_audio_cfg(uint8_t *d, uint32_t *midi_at)
{
    static const uint8_t head[] = {9, 2, 0, 0, 5, 1, 0, 0x80, 250, 8, 0x0B, 0, 4, 1, 0, 0, 0};
    static const uint8_t ac[] = {
        9, 4, 0, 0, 0, 1, 1, 0, 0,
        10, 0x24, 1, 0, 1, 71, 0, 2, 1, 2,
        12, 0x24, 2, 1, 1, 1, 0, 2, 3, 0, 0, 0,         /* USB streaming in, line out */
        12, 0x24, 2, 4, 1, 2, 0, 2, 3, 0, 0, 0,
        9, 0x24, 3, 3, 1, 3, 0, 1, 0,
        9, 0x24, 3, 6, 1, 1, 0, 4, 0,
        10, 0x24, 6, 2, 1, 1, 1, 0, 0, 0,
    };
    static const uint8_t midi[] = {
        9, 4, 3, 0, 2, 1, 3, 0, 0,
        7, 0x24, 1, 0, 1, 0x41, 0,
        6, 0x24, 2, 1, 1, 0,
        9, 0x24, 3, 1, 3, 1, 2, 1, 0,
        9, 5, 0x02, 2, 64, 0, 0, 0, 0,
        5, 0x25, 1, 1, 1,
        9, 5, 0x81, 2, 64, 0, 0, 0, 0,
        5, 0x25, 1, 1, 3,
    };
    static const uint8_t dfu[] = {9, 4, 4, 0, 0, 0xFE, 1, 1, 0, 9, 0x21, 1, 0xFF, 0, 0, 4, 0x10, 1};
    uint32_t n = 0, f, k, q;
    memcpy(d, head, sizeof head);
    n += sizeof head;
    memcpy(d + n, ac, sizeof ac);
    n += sizeof ac;
    for (f = 1; f <= 2; f++) {
        uint8_t alt0[9] = {9, 4, (uint8_t)f, 0, 0, 1, 2, 0, 0};
        memcpy(d + n, alt0, 9);
        n += 9;
        for (k = 1; k <= 3; k++) {
            uint8_t as[9 + 7] = {9, 4, (uint8_t)f, (uint8_t)k, 1, 1, 2, 0, 0, 7, 0x24, 1, (uint8_t)(f == 1 ? 1 : 6), 1, 1, 0};
            uint8_t ep[9 + 7] = {9, 5, (uint8_t)(f == 1 ? 0x03 : 0x84), 0x09, 0x20, 0x02, 1, 0, 0, 7, 0x25, 1, 1, 0, 0, 0};
            memcpy(d + n, as, sizeof as);
            n += sizeof as;
            d[n++] = 8 + 8 * 3;                         /* format type I: 2/3/4-byte samples, eight rates */
            d[n++] = 0x24; d[n++] = 2; d[n++] = 1; d[n++] = 2; d[n++] = (uint8_t)(k + 1); d[n++] = (uint8_t)(8 * (k + 1));
            d[n++] = 8;
            for (q = 0; q < 8; q++) {
                uint32_t hz = 8000u * (q + 1u);
                d[n++] = (uint8_t)hz; d[n++] = (uint8_t)(hz >> 8); d[n++] = (uint8_t)(hz >> 16);
            }
            memcpy(d + n, ep, sizeof ep);
            n += sizeof ep;
        }
    }
    *midi_at = n;
    memcpy(d + n, midi, sizeof midi);
    n += sizeof midi;
    memcpy(d + n, dfu, sizeof dfu);
    n += sizeof dfu;
    d[2] = (uint8_t)n;
    d[3] = (uint8_t)(n >> 8);
    return (uint16_t)n;
}

static void test_audio_midi_device(void)
{
    static uint8_t cfg[1024];
    uh_midi_t m;
    uh_desc_t w;
    uint32_t midi_at, i;
    uint16_t n = build_audio_cfg(cfg, &midi_at);
    assert(n <= sizeof cfg && midi_at > 320u);          /* past the 320 bytes the probe used to keep */
    assert(uh_parse_midi(cfg, n, &m) && m.cfg == 1 && m.iface == 3 && m.alt == 0 && m.in_ep == 1 &&
           m.in_mps == 64 && m.out_ep == 2 && m.out_mps == 64);
    uh_desc_start(&w, &m);                              /* a byte at a time: every descriptor split */
    for (i = 0; i < n && !uh_desc_feed(&w, cfg + i, 1); i++)
        ;
    assert(m.found && m.iface == 3 && m.in_ep == 1 && m.out_ep == 2);
    assert(i + 1u < n && cfg[i - 8] == 9 && cfg[i - 7] == 4 && cfg[i - 6] == 4);   /* ends at the DFU interface */
    for (int via_hub = 0; via_hub < 2; via_hub++)
        for (int small = 0; small < 2; small++) {
            connect(via_hub ? HUB : KBD);
            dev[KBD].cfgd = cfg;
            dev[KBD].cfglen = n;
            dev[KBD].mps0 = small ? 8 : 64;             /* 8: the descriptor arrives in 8-byte packets */
            host_on();
            assert(enumerate() && uh.msg == UH_MSG_MIDI);
            uh.msg = 0;
            assert(dev[KBD].cfg == 1 && dev[KBD].addr == uh.dev && uh.in_ep == 1 && uh.out_mps == 64);
            push_packet(0x643C9009u);
            poll_n(4);
            assert(mi_w - mi_r == 1 && midi_in_q[mi_r % MQ] == 0x643C9009u);
            mi_r = mi_w;
            uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
        }
    printf("USB host: audio+MIDI device, %u-byte configuration walked as it arrives: OK\n", n);
}

/* Two hub chips in one case: the second on the first's last port, the keyboard on the second. */
static void cascade(void)
{
    connect(HUB);                                       /* port 1: the stick */
    dev[HUB].port_dev[3] = 0;
    dev[HUB].port_dev[4] = HUB2 + 1;
    dev[HUB2].port_dev[2] = KBD + 1;
}

static void test_cascaded_hub(void)
{
    cascade();
    host_on();
    assert(enumerate() && uh.msg == UH_MSG_MIDI);
    uh.msg = 0;
    assert(dev[HUB].addr == 1 && dev[STICK].addr == 2 && dev[HUB2].addr == 5 && dev[KBD].addr == 10);
    assert(uh.dev == 10 && common[0] == 10 && dev[HUB2].cfg == 1 && dev[KBD].cfg == 1);
    assert(uh.hub[0].addr == 1 && uh.hub[1].addr == 5 && uh.hub[1].port == 4 && uh.hub[1].ports == 4);
    push_packet(0x643C9009u);
    poll_n(4);
    assert(mi_w - mi_r == 1 && midi_in_q[mi_r % MQ] == 0x643C9009u);
    mi_r = mi_w;

    /* The keyboard goes from the second hub: lost, the tree is walked again, then the idle poll of the second
     * hub's ports finds it on replug without another bus reset. */
    assert(midi_track_emit(0x643c9009u)); usb_poll();
    dev[HUB2].port_dev[2] = 0;
    dev[HUB2].port_en[2] = 0;
    dev[KBD].present = 0;
    for (uint32_t k = 0; k < UH_MAX_ERRORS; k++) { regs[1][17] = 4; usb_poll(); }
    assert(uh.fault);
    uh_task(fm1_ms); assert(uh.msg == UH_MSG_LOST); uh.msg = 0;
    isr_recovery();
    assert(!enumerate() && uh.msg == UH_MSG_HUB_NOMIDI && root_resets == 2);
    uh.msg = 0;
    assert(uh.hub[1].addr == 5 && dev[HUB2].addr == 5);
    for (int k = 0; k < 100; k++) { fm1_ms += 20; uh_task(fm1_ms); assert(!uh.msg); }
    dev[KBD].present = 1;
    dev[HUB2].port_dev[2] = KBD + 1;
    dev[HUB2].port_chg[2] |= 1;
    assert(enumerate() && uh.msg == UH_MSG_MIDI && root_resets == 2 && dev[KBD].addr == 10);
    uh.msg = 0;
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    /* The second hub unplugged from the first while idle: its state goes, nothing is probed there. */
    cascade();
    dev[HUB2].port_dev[2] = 0;
    host_on();
    assert(!enumerate() && uh.hub[1].addr == 5);
    uh.msg = 0;
    dev[HUB].port_dev[4] = 0;
    dev[HUB].port_en[4] = 0;
    dev[HUB].port_chg[4] |= 1;
    dev[HUB2].present = 0;
    for (int k = 0; k < 100; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(!uh.hub[1].addr && uh.hub[0].addr == 1 && !dev[HUB].port_chg[4] && root_resets == 1 && !uh.msg);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    /* A hub on the second hub is one level too many: passed over, never configured. */
    cascade();
    dev[HUB2].port_dev[2] = 0;
    dev[HUB2].port_dev[1] = HUB3 + 1;
    dev[HUB3].port_dev[1] = KBD + 1;
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_HUB_NOMIDI);
    assert(dev[HUB3].addr == 9 && !dev[HUB3].cfg && !dev[KBD].seen_probe);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    puts("USB host: a hub on a hub's port (two chips in one case): addresses, events, replug, unplug, depth limit: OK");
}

static void test_slow_hub(void)
{
    connect(HUB);
    slow_until = 3;                                     /* answers after the third reset (400 ms recovery) */
    host_on();
    assert(enumerate() && uh.msg == UH_MSG_MIDI && root_resets == 3 && dev[KBD].addr == 4);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    connect(HUB);
    slow_until = UH_ROOT_TRIES + 1u;                    /* never in time: reported, then quiet until unplug */
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_NOANSWER && root_resets == UH_ROOT_TRIES);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    puts("USB host: a slow hub gets longer resets, a device that answered is not reset again: OK");
}

static void test_device_mode_untouched(void)
{
    /* with the host off, usb_poll is the device driver and uh_task does nothing */
    connect(KBD);
    uh.want = uh.on = uh.run = 0;
    usb.up = 0;
    uh_task(100);
    assert(!uh.on && !uh.msg);
    usb_retry(fm1_ms + 5000u);
    assert(usb.up);
    usb.up = 0;
    puts("USB host: off at boot, the device side starts as before: OK");
}

/* A device behind a hub that misses the scan (still starting) is found by the retry rounds without unplugging it;
 * one that never answers is given up after UH_PORT_TRIES rounds; a low-speed one is said, not skipped silently. */
static void test_hub_port_retry(void)
{
    connect(HUB);
    dev[HUB].port_dev[1] = 0;
    dev[KBD].fail_probes = 2;                          /* the scan and the first retry round miss it */
    host_on();
    for (int k = 0; k < 400 && !uh.msg; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(!uh.run && uh.msg == UH_MSG_NOANSWER && (uh.hub[0].retry & (1u << 3)));
    uh.msg = 0;
    for (int k = 0; k < 200 && !uh.run; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(uh.run && uh.msg == UH_MSG_MIDI && dev[KBD].addr == 4 && root_resets == 1 && host_attaches == 1);
    assert(dev[KBD].seen_probe == 3 && port_resets[3] == 3 && !uh.hub[0].retry);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    connect(HUB);
    dev[HUB].port_dev[1] = 0;
    dev[KBD].fail_probes = 255;                        /* never answers: bounded rounds, then quiet */
    host_on();
    for (int k = 0; k < 400 && !uh.msg; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(!uh.run && uh.msg == UH_MSG_NOANSWER);
    uh.msg = 0;
    assert(!strcmp(uh.dg, "HUB4 --D-"));             /* the toast's detail: port 3 did not answer */
    int said = 0;
    for (int k = 0; k < 3000; k++) {
        fm1_ms += 20; uh_task(fm1_ms); assert(!uh.run);
        if (uh.msg) { assert(uh.msg == UH_MSG_NOANSWER && !said++); uh.msg = 0; }
    }
    assert(said == 1 && !strcmp(uh.dg, "RETRY4 D"));  /* said once, after the last round */
    assert(port_resets[3] == 1u + UH_PORT_TRIES && root_resets == 1);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    connect(HUB);
    dev[HUB].port_dev[1] = 0;
    dev[HUB].port_ls[3] = 1;                           /* a low-speed controller on a hub port */
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_LOW && !port_resets[3] && !strcmp(uh.dg, "HUB4 --L-"));
    uh.msg = 0;
    idle_hub(600);
    dev[HUB].port_chg[3] |= 1;                         /* replugged: said again */
    fm1_ms += 200; for (int k = 0; k < 10 && !uh.msg; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(uh.msg == UH_MSG_LOW && !port_resets[3] && !uh.run);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    connect(-1);                                       /* nothing pulls the line up: said once after 3 s */
    line_state = 0;
    host_on();
    for (int k = 0; k < 140; k++) { fm1_ms += 20; uh_task(fm1_ms); assert(!uh.msg); }
    for (int k = 0; k < 20 && !uh.msg; k++) { fm1_ms += 20; uh_task(fm1_ms); }
    assert(uh.msg == UH_MSG_NOLINE); uh.msg = 0;
    for (int k = 0; k < 600; k++) { fm1_ms += 20; uh_task(fm1_ms); assert(!uh.msg); }
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;
    connect(HUB);                                      /* a hub that says class 9 on its interface only */
    dev[HUB].cls = 0;
    host_on();
    assert(enumerate() && uh.msg == UH_MSG_MIDI && dev[KBD].addr == 4 && uh.hub[0].addr);
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    connect(STICK);                                    /* no MIDI: its classes in the toast's detail */
    host_on();
    assert(!enumerate() && uh.msg == UH_MSG_NOMIDI && !strcmp(uh.dg, "DEV N1 000806"));
    uh.want = 0; uh_task(fm1_ms); uh.msg = 0;

    puts("USB host: hub port retried while still starting, bounded, low speed said behind a hub, nothing on the port said, interface-class hub: OK");
}

int main(void)
{
    devices_init();
    test_parse();
    test_device_mode_untouched();
    test_keyboard(0);
    test_keyboard(1);
    test_no_midi();
    test_idle_hub(0);
    test_idle_hub(1);
    test_failed_probe();
    test_hub_synth_unplug();
    test_host_output(0);
    test_host_output(1);
    test_endpoint_selection();
    test_audio_midi_device();
    test_cascaded_hub();
    test_slow_hub();
    test_hub_port_retry();
    puts("USB host: all OK");
    return 0;
}
