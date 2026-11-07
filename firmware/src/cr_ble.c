/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * Adapted from Melodee's firmware/src/midi_ble.c (3fe570d): Copyright (C) 2026 Kerem Kilic (Ellic Studio),
 * GPL-3.0-only; its peripheral half (the rings, the bond, the radio's start, the calibration's choice). */
/* Bluetooth LE MIDI, the glue (cr_ble.h says what and why; docs/ble/ how the stack works).
 *
 *   in    the BLE interrupts (ble_app_midi_in) -> a private ring -> TIMER5 (cr_ble_drain) -> usb.c's MIDI input queue
 *         as source 3, stamped with the packet's own time (the peer's 13-bit timestamps: the spacing inside a
 *         packet kept), so it plays exactly as USB and TRS do: the Perform and Bass channels, MIDI Keys, program
 *         changes, the CCs, clock and transport for Clock In. Channel messages, clock, start / continue / stop and
 *         Song Position only; SysEx and the rest stay on USB.
 *   out   usb.c midi_out_event (the audio ISR) -> cr_ble_emit -> a ring the BLE interrupts empty: whatever goes out
 *         over USB MIDI (Perform, Bass, Raw Chord, the clock with Clock Out). Clock and transport in a ring of their
 *         own, sent ahead of the notes (Melodee 4c40bb5).
 *   loss  the notes a peer left on (held[]) end with note-offs through the same queue when the link goes or
 *         Bluetooth goes Off; USB and TRS notes keep sounding.
 *   start the radio's start-up holds the interrupts (the program from the resource region, and ~160 ms of
 *         calibration when no trims are stored): only at power-on, the last step before the interrupts come on
 *         (cr_ble_start_boot; main.c), so its interrupts are served at once, core 1 waits in its RAM loop and no
 *         audio half is due. Never while the unit runs: 3y236 started it there when switched On and a unit froze
 *         (the watchdog: "Dual Core Hang"), and a start at power-on before the audio, USB and core 1 left the
 *         interrupts off for a second after it and the unit was never seen (3y238). Switched On while running: saved,
 *         started at the next power-on. Once started it stays started; Off stops the link and the advertising
 *         (ble_enable), On again resumes them.
 *   store the address, the flags (Bluetooth On), the bond and a copy of the radio's trims in their own object
 *         (storage.c OBJ_BLE, 0xA0000 / 0xA1000): never in a backup (a unit's pairing is not a musical setting),
 *         saved only when nothing sounds and no loop runs. SAFE MODE: never read, never started. */
#include "ble/ble_stack.c"
#include "ble/ble_vm.c"

#define CRBL_MAGIC 0x31454C42u            /* "BLE1" */
#define CRBL_ON 1u                        /* flags: Bluetooth On (started at power-on) */
#define CRBL_Q 64u                        /* packets each way (a power of two) */
#define CRBL_RT 16u                       /* clock and transport out */
typedef struct {
    uint32_t magic;
    uint8_t ver, flags, valid, rsv0;
    uint8_t addr[6], rsv1[2];             /* a random static address, made once */
    uint8_t bond[28];                     /* 0xB5, 1, rand 8, ediv 2, ltk 16 */
    uint8_t rf[BLE_RF_COPY_SIZE];         /* the radio's trims as found in the stock VM (ble_vm.c), checked */
} crbl_rec_t;
_Static_assert(sizeof(crbl_rec_t) <= ST_PAYLOAD_MAX, "the Bluetooth record fits one storage object");
static crbl_rec_t crbl_rec;               /* as flash holds it, or will */
static struct {
    uint32_t rx[CRBL_Q], rx_ms[CRBL_Q], tx[CRBL_Q], rt[CRBL_RT];
    volatile uint32_t rw, rr, tw, tr, rtw, rtr;
    volatile uint8_t ready, connected, lost, overflow, closing;
    uint8_t peeked_rt;
    uint8_t on, started, cal, pending, dirty, booted;   /* pending: On, the radio at the next power-on */
    uint8_t boot_go;                      /* cr_ble_boot: start the radio at cr_ble_start_boot */
    uint8_t supplies;                     /* 0 not asked, 1 stock's supplies applied, 2 a P33 timeout (no radio) */
    uint8_t cal_from;                     /* 0 none, 1 the stock VM, 2 our copy, 3 measured at start (fm1_rf_cal.h) */
    uint32_t start_us, rx_n, tx_n, losses; /* (81 BLE: the start's time with the interrupts held, packets, links lost) */
    uint8_t releasing;                    /* TIMER5: note-offs owed for what the peer held (the link went) */
    uint16_t held[16][8];                 /* bit n of [ch][k]: note k * 16 + n held by the peer */
    struct ble_rf_trims trims;
} crbl;

static uint32_t crbl_lock_depth;
static void crbl_lock(void)               /* the BLE interrupts held (TIMER5 may enter meanwhile) */
{
    uint32_t ie = fm1_icfg();
    fm1_irq_off();
    if (!crbl_lock_depth++ && crbl.started)
        fm1_ble_irqs_hold(1);
    fm1_icfg_set(ie);
}
static void crbl_unlock(void)
{
    uint32_t ie = fm1_icfg();
    fm1_irq_off();
    if (--crbl_lock_depth == 0u && crbl.started)
        fm1_ble_irqs_hold(0);
    fm1_icfg_set(ie);
}

/* ---------------------------------------------------- the stack's calls (the BLE interrupts' context) --- */
BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *ms)
{
    crbl.peeked_rt = crbl.rtr != crbl.rtw;
    if (crbl.peeked_rt) {
        RING_PUBLISH();
        *pkt = crbl.rt[crbl.rtr % CRBL_RT];
        *ms = fm1_ms;
        return 1;
    }
    if (crbl.tr == crbl.tw)
        return 0;
    RING_PUBLISH();
    *pkt = crbl.tx[crbl.tr % CRBL_Q];
    *ms = fm1_ms;
    return 1;
}
BLE_API void ble_app_midi_pop(void)
{
    RING_PUBLISH();
    crbl.tx_n++;
    if (crbl.peeked_rt)
        crbl.rtr++;
    else
        crbl.tr++;
}
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    uint32_t status = (pkt >> 8) & 255u, at, back;
    if (!crbl.on || crbl.closing || !((status >= 0x80u && status < 0xF0u) || status == 0xF2u || status == 0xF8u ||
                                      status == 0xFAu || status == 0xFBu || status == 0xFCu))
        return;                                   /* (MIDI only: no SysEx, no system exclusive framing) */
    if (crbl.rw - crbl.rr >= CRBL_Q) {
        crbl.overflow = 1;
        return;
    }
    at = crbl.rw % CRBL_Q;
    crbl.rx_n++;
    back = (uint32_t)(last - ts) & 0x1FFFu;       /* how long before the packet's newest event this one was */
    crbl.rx[at] = pkt;
    crbl.rx_ms[at] = fm1_ms - (back < 1000u ? back : 0u);
    RING_PUBLISH();
    crbl.rw++;
}
BLE_API void ble_app_state(void)
{
    uint8_t ready = (uint8_t)(ble_midi_ready() && !crbl.closing), connected = (uint8_t)ble_connected();
    if ((crbl.ready && !ready) || (crbl.connected && !connected)) {
        crbl.lost = 1;
        crbl.losses++;
    }
    if (!connected)
        crbl.closing = 0;
    crbl.connected = connected;
    crbl.ready = ready;
    if (!ready) {                                 /* nothing queued for a peer that is gone */
        crbl.tr = crbl.tw;
        crbl.rtr = crbl.rtw;
    }
}
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    if (!crbl.on || crbl.closing)
        return;
    crbl_rec.bond[0] = 0xB5u;
    crbl_rec.bond[1] = 1u;
    ble_cpy(crbl_rec.bond + 2, rand, 8);
    ble_wr16(crbl_rec.bond + 10, ediv);
    ble_cpy(crbl_rec.bond + 12, ltk, 16);
    crbl.dirty = 1;                               /* (saved by cr_ble_poll when the unit is quiet) */
}
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t rnd)
{
    (void)irk; (void)addr; (void)rnd;
}

/* ------------------------------------------------------------------------- out (the audio ISR) --- */
static int cr_ble_emit(uint32_t pkt)
{
    uint32_t status = (pkt >> 8) & 255u;
    if (!crbl.ready || !crbl.on)
        return 0;
    if (status >= 0xF8u || status == 0xF2u) {     /* clock and transport: their own ring, ahead of the notes */
        if (crbl.rtw - crbl.rtr >= CRBL_RT)
            return 0;
        crbl.rt[crbl.rtw % CRBL_RT] = pkt;
        RING_PUBLISH();
        crbl.rtw++;
        return 1;
    }
    if (crbl.tw - crbl.tr >= CRBL_Q)
        return 0;
    crbl.tx[crbl.tw % CRBL_Q] = pkt;
    RING_PUBLISH();
    crbl.tw++;
    return 1;
}

/* ----------------------------------------------------------------------------------- in (TIMER5) --- */
static void crbl_note(uint32_t pkt)               /* held[] follows the peer's notes */
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 127u, v = (pkt >> 24) & 127u;
    if (st == 0x90u && v)
        crbl.held[ch][n >> 4] |= (uint16_t)(1u << (n & 15u));
    else if (st == 0x80u || st == 0x90u)
        crbl.held[ch][n >> 4] &= (uint16_t)~(1u << (n & 15u));
    else if (st == 0xB0u && (n == 120u || n == 123u))
        memset(crbl.held[ch], 0, sizeof crbl.held[ch]);
}
static int crbl_release(void)                     /* the link went: note-offs for what the peer held; 1 when done */
{
    uint32_t ch, k, b;
    for (ch = 0; ch < 16u; ch++)
        for (k = 0; k < 8u; k++)
            for (b = 0; crbl.held[ch][k] && b < 16u; b++)
                if ((crbl.held[ch][k] >> b) & 1u) {
                    if (!midi_enqueue(0x08u | (0x80u | ch) << 8 | (k * 16u + b) << 16, 3u))
                        return 0;                 /* (the queue full: the rest at the next tick) */
                    crbl.held[ch][k] &= (uint16_t)~(1u << b);
                }
    return 1;
}
static void cr_ble_drain(void)                    /* TIMER5 (10 kHz): the input queue's producer context, as usb.c's.
                                                   * Radio not started: one load and back */
{
    uint32_t n, at;
    if (!crbl.started)
        return;
    if (crbl.lost || crbl.overflow) {
        crbl_lock();
        crbl.rr = crbl.rw;
        crbl.lost = crbl.overflow = 0;
        crbl_unlock();
        crbl.releasing = 1;
    }
    for (n = 0; crbl.rr != crbl.rw && n < CRBL_Q; n++) {
        RING_PUBLISH();
        at = crbl.rr % CRBL_Q;
        if (!midi_enqueue(crbl.rx[at], 3u))
            break;                                /* (full: usb.c raised midi_in_overflow; cr_out.c releases) */
        midi_in_ms[(mi_w - 1u) % MQ] = crbl.rx_ms[at];
        crbl_note(crbl.rx[at]);
        crbl.rr++;
    }
    if (crbl.releasing)
        crbl.releasing = (uint8_t)!crbl_release();
}

/* ---------------------------------------------------------------------------- start, on / off --- */
static int crbl_flash_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;
    return st_read(off, dst, n);
}
static int crbl_quiet(void)                       /* nothing sounds, no loop runs: an IRQ hold or an erase is safe */
{
    return !cr_parts_busy() && cr_snap.lstate != CRL_PLAYING && cr_snap.lcap == CRL_CAP_NONE && !crl_stage_busy;
}
static void crbl_start(void)                      /* the radio, once a power-on: interrupts held while it starts */
{
    uint32_t ie, i;
    if (!crbl_rec.valid) {                        /* a random static address (top bits 11), kept */
        for (i = 0; i < 6u; i++)
            crbl_rec.addr[i] = (uint8_t)fm1_ble_rng32();
        crbl_rec.addr[5] |= 0xC0u;
        crbl_rec.valid = 1;
        crbl.dirty = 1;
    }
    ie = fm1_icfg();
    fm1_irq_off();
    i = fm1_ticks();
    ble_hw_wl82_start(&crbl.trims);               /* (hal/fm1_ble_rf.h: the program from the resource region) */
    crbl.start_us = (fm1_ticks() - i) / FM1_TICKS_PER_US;
    fm1_icfg_set(ie);
    ble_init(crbl_rec.addr, 1);
    ble_ll_advertise(1);                          /* visible: "ChoralRoot XXXX" */
    if (crbl_rec.bond[0] == 0xB5u && crbl_rec.bond[1] == 1u)
        ble_host_set_key(crbl_rec.bond + 2, ble_rd16(crbl_rec.bond + 10), crbl_rec.bond + 12);
    crbl.started = 1;
    crbl_lock();
    ble_enable(crbl.on);
    crbl_unlock();
}
static void cr_ble_boot(void)                     /* power-on: before the audio and core 1, interrupts off */
{
    struct ble_vm_info info;
    struct ble_rf_trims vm;
    int save = 0, complete;
    if (!flash_ok || cr_safe)
        return;
    if (st_load(OBJ_BLE, &crbl_rec, sizeof crbl_rec) != (int)sizeof crbl_rec || crbl_rec.magic != CRBL_MAGIC)
        memset(&crbl_rec, 0, sizeof crbl_rec);
    memset(&info, 0, sizeof info);
    complete = ble_vm_scan(crbl_flash_read, 0, &info, &vm, 0, 0);
    crbl.cal_from = (uint8_t)ble_rf_choose(complete, &vm, info.area, crbl_rec.rf, &crbl.trims, &save);
    crbl.cal = (uint8_t)(crbl.cal_from != BLE_RF_NONE);
    if (save)
        crbl.dirty = 1;
    if (!crbl.cal) {                              /* none stored: the radio measures its own at start (fm1_rf_cal.h,
                                                   * ~160 ms); 106..108 the SDK board's constants (Melodee 89b746c) */
        static const struct ble_rf_trims sdk = {{0x0B, 0x0B}, {1, 7, 4, 7, 11, 1, 7}, {0}, {0}};
        crbl.trims = sdk;
        fm1_rf_cal_on = 1;
        crbl.cal = 1;
        crbl.cal_from = 3;
    }
    ble_hw_wl82_attach();                         /* the BLE vectors, masked */
    fm1_ble_crumb_boot();
    crbl.booted = 1;
    crbl.on = (uint8_t)!!(crbl_rec.flags & CRBL_ON);
    /* On saved, the region this build's (the program), and no failed start-up counted (a start that hung ends in the
     * watchdog: the boot guard counts it, and the next boot leaves the radio off; Off in the Options then) */
    crbl.boot_go = (uint8_t)(crbl.on && cr_res_ok && ble_boot_radio(1, bootguard.failed, crbl.cal));
    crbl.pending = (uint8_t)(crbl.on && !crbl.boot_go);
}
static void cr_ble_start_boot(void)               /* main.c: the interrupts still off, about to come on */
{
    if (!crbl.boot_go || (cr_ble_test() & CRBL_T_LATE))
        return;
    crbl.boot_go = 0;
    crbl_start();
}
static int cr_ble_running(void) { return crbl.started; }
static int cr_ble_will_start(void) { return crbl.boot_go && !(cr_ble_test() & CRBL_T_NOSUPPLY); }
static uint32_t cr_ble_test(void)                 /* (crbl_rec.rsv0: a release build reads 0) */
{
    return BLE_DIAG && crbl.booted ? crbl_rec.rsv0 : 0u;
}
static int cr_ble_set_test(uint32_t m)            /* 82 BLE_TEST: saved, applied at the next power-on */
{
    if (!BLE_DIAG || !crbl.booted || m > 31u)
        return 1;
    crbl_rec.rsv0 = (uint8_t)m;
    crbl.dirty = 1;
    return 0;
}
static void cr_ble_supplies(int ok)               /* (a failed supply sequence: no radio; owed to the next power-on) */
{
    crbl.supplies = (uint8_t)(ok ? 1 : 2);
    if (!ok) {
        crbl.boot_go = 0;
        crbl.pending = crbl.on;
    }
}
/* 81 BLE (cr_backup.c; fm1_install.py --debug DEBUG_BLE, in this order, only ever appended): On, started, a start owed
 * at the next power-on, where the trims came from (0 none, 1 the stock VM at 0xE8000, 2 our copy, 3 measured at the
 * start), the start's time in us with the interrupts held, a peer connected, MIDI ready (subscribed), the packets in
 * and out, the links lost; then the radio's breadcrumb (hal/fm1_ble_rf.h fm1_ble_bc, .noinit): now and its interrupt
 * count, and what the last run left when a reset (not a power-on) ended it: its word, interrupts, op, burst op,
 * address, stop; the boot guard's failed count; the BLE start-up step reached (the word's bits 16..23) */
static uint32_t cr_ble_debug(uint32_t v[CRBL_DBG_N])
{
    uint32_t k = 0;
    v[k++] = crbl.on;
    v[k++] = crbl.started;
    v[k++] = crbl.pending;
    v[k++] = crbl.cal_from;
    v[k++] = crbl.start_us;
    v[k++] = crbl.connected;
    v[k++] = crbl.ready;
    v[k++] = crbl.rx_n;
    v[k++] = crbl.tx_n;
    v[k++] = crbl.losses;
#ifndef FM1_BLE_FAKE_H                            /* (tests/ble_fake: no breadcrumb) */
    v[k++] = fm1_ble_bc.now;
    v[k++] = fm1_ble_bc.irqs;
    v[k++] = fm1_ble_bc.prev;
    v[k++] = fm1_ble_bc.prev_irqs;
    v[k++] = fm1_ble_bc.prev_op;
    v[k++] = fm1_ble_bc.prev_gop;
    v[k++] = fm1_ble_bc.prev_addr;
    v[k++] = fm1_ble_bc.prev_stop;
#else
    for (; k < 18u; k++)
        v[k] = 0;
#endif
    v[k++] = bootguard.failed;
    v[k++] = v[10] >> 16 & 0xFFu;
    v[k++] = crbl.supplies;
#if BLE_DIAG
    v[k++] = cr_ble_test();                       /* the bisection's mask in force this boot (CRBL_T_*) */                                      /* (a BLE_DIAG=1 build: the driver's counters, ble_diag.h ble_dg) */
    v[k++] = ble_dg.adv_starts;                   /* advertising started */
    v[k++] = ble_dg.adv_events;                   /* advertising events ended (each one's three channels sent) */
    v[k++] = ble_dg.adv_rx;                       /* packets received while advertising */
    v[k++] = ble_dg.scan_req;                     /* SCAN_REQs a scanner sent us (the engine answered): we are heard */
    v[k++] = ble_dg.adv_drop;
    v[k++] = (uint32_t)ble_dg.adv_drop_stat | (uint32_t)ble_dg.adv_drop_hdr << 16;
    v[k++] = ble_dg.cind_rx;                      /* CONNECT_INDs: received, accepted, refused (and why) */
    v[k++] = ble_dg.cind_ok;
    v[k++] = ble_dg.cind_rej;
    v[k++] = ble_dg.cind_rej_why;
    v[k++] = ble_dg.evt_irqs;                     /* the connection: event and RX interrupts, events, good packets */
    v[k++] = ble_dg.rx_irqs;
    v[k++] = ble_dg.conn_events;
    v[k++] = ble_dg.rx_good;
    v[k++] = ble_dg.rx_crc_bad;
    v[k++] = ble_dg.closes;
    v[k++] = (uint32_t)ble_dg.close_reason | (uint32_t)ble_dg.close_by << 8;
    v[k++] = ble_dg.busy_timeouts;
#endif
    return k;
}
static int cr_ble_set(int on)
{
    uint8_t f;
    if (on && (!crbl.booted || !cr_res_ok || !crbl.cal))
        return 0;
    f = (uint8_t)(on ? crbl_rec.flags | CRBL_ON : crbl_rec.flags & ~CRBL_ON);
    if (f != crbl_rec.flags) {
        crbl_rec.flags = f;
        crbl.dirty = 1;
    }
    crbl_lock();
    crbl.on = (uint8_t)!!on;
    if (!on)
        crbl.lost = 1;                            /* (the peer's notes end at the next tick) */
    if (crbl.started)
        ble_enable(on);
    crbl_unlock();
    crbl.pending = (uint8_t)(on && !crbl.started); /* (the radio at the next power-on: never started while running) */
    return 1;
}
static int cr_ble_on(void) { return crbl.on; }
static void cr_ble_forget(void)                   /* the bond goes (the peer forgets ChoralRoot too, by hand) */
{
    crbl_lock();
    crbl.closing = (uint8_t)ble_connected();
    memset(crbl_rec.bond, 0, sizeof crbl_rec.bond);
    if (crbl.started) {
        memset(&bhs_key, 0, sizeof bhs_key);
        ble_smp_reset();
        ble_enable(0);
        ble_enable(crbl.on);
    }
    crbl.lost = 1;
    crbl.dirty = 1;
    crbl_unlock();
}
static void cr_ble_poll(void)                     /* the main loop */
{
    if (!crbl.booted)
        return;
    if (crbl.boot_go && (cr_ble_test() & CRBL_T_LATE)) {   /* (the bisection: Melodee's place, the main loop) */
        crbl.boot_go = 0;
        crbl_start();
    }
    if (crbl.dirty && flash_ok && !cr_safe && crbl_quiet()) {
        crbl_rec.magic = CRBL_MAGIC;
        crbl_rec.ver = 1;
        if (st_save(OBJ_BLE, &crbl_rec, sizeof crbl_rec) == 0)
            crbl.dirty = 0;
    }
}
static void cr_ble_status(char *d, uint32_t n)    /* the line under Options > Bluetooth */
{
    const uint8_t *p;
    uint32_t k, i;
    if (!n)
        return;
    d[0] = 0;
    if (!cr_res_ok) { cu_cpy(d, "install ChoralRoot again", n); return; }
    if (!crbl.on) { cu_cpy(d, "MIDI over Bluetooth: off", n); return; }
    if (!crbl.started) { cu_cpy(d, "on at the next power-on", n); return; }
    if (ble_host_key_refused()) { cu_cpy(d, "forget ChoralRoot on the peer", n); return; }
    if (crbl.ready) { cu_cpy(d, "connected", n); return; }
    k = ble_gap_name(&p);                         /* the advertised name: ChoralRoot and 4 digits */
    for (i = 0; i < k && i + 1u < n; i++)
        d[i] = (char)p[i];
    d[i] = 0;
    cu_cat(d, crbl.connected ? ": waiting" : ": visible", n);
}
