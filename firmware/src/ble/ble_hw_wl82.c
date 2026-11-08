/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee integration changes Copyright (C) 2026 Kerem Kilic (Ellic Studio).
 * Retains Optimist code from w0ts/optimist at 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66.
 * Provenance and local changes: docs/BLE_MIDI.md. */
/* The baseband driver (ble_hw.h) on the AC791N / WL82 BLE engine, route C (docs/BLE-STACK.md §7, the interface §3; FELUCCA_BLE).
 *
 * Clean room: written from the hardware fact sheet docs/BLE-HW-FACTS.md (feat/ble-facts 41b7374, TX: §8.2 at a3b2f06; "HW §n"
 * below), the Bluetooth Core Specification (Vol 6 Part B) and the answers the emulator's engine model gave from
 * stock V15 running in it (fm1-emulator feat/ble-engine 6531e20; "model" below). No vendor code, IR or disassembly.
 * The registers are reached through hal/fm1_ble.h; this file owns the baseband RAM block and the control block,
 * plain SRAM the engine reads and writes by DMA (HW §3, §4).
 *
 * What it does:
 *   - one static baseband RAM block: the instance table, link 0's 324-byte control block, two RX and two TX
 *     buffers (each a 20-byte software header then the payload, the pointers at the payload: HW §4);
 *   - advertising (HW §6): ADV_IND in TX buffer 0, SCAN_RSP in TX buffer 1 (the engine answers SCAN_REQ by itself);
 *   - the CONNECT_IND in the RX interrupt, handed to the link layer, which calls ble_hw_conn_start() in it: state 2
 *     to 7 before the interrupt returns (HW §7; the engine stops advertising by itself, model);
 *   - per event: RX delivery with the software SN check, the event counter, the instants (HW §8, §9); the TX
 *     service (acknowledgement and refill, HW §8.2) at the end of each connection RX interrupt; the supervision
 *     timeout is the link layer's (HW §7 step 15).
 *
 * Interrupts: IRQ 45 (event) and IRQ 29 (RX) at BLE_HW_IRQ_PRIO, below the audio (IRQ 11, 3) and the TIMER5 tick
 * (4), as stock (HW §5.5 step 5, §10: priority 2). Both run at one priority, so they never nest in each other, the
 * single context ble_hw.h asks for. The engine keeps the radio timing in hardware; software deadlines are long:
 * state 7 before the transmit window (>= 1.25 ms after the CONNECT_IND), a TX refill and the instant writes within
 * one interval (>= 7.5 ms). An audio half (the audio ISR, docs/BLE-STACK.md §7, "Interrupts") fits inside them, so
 * the audio is never delayed by BLE; BLE waits for the audio instead.
 *
 * What the engine model settled (the agent that wrote it, from stock V15): a repeated SN is dropped by the engine
 * (the default) but software checks SN as well, and EVTCOUNT still counts the repeat as a reception; TXBUFnCNTL
 * bit0 going back to 1 is the acknowledgement; one packet pair per event by default; the first anchor is the
 * CONNECT_IND's end + 1.25 ms + WinOffset; leaving advertising is column 2 going from state 2 to 7; the new
 * interval is written in event instant - 1; the time base is the 24-bit link clock (columns 0 / 14); the engine
 * fills the RX buffer RXTOG selects whatever RXBUFnCNTL bit0 says; IRQ 29 fires for empty PDUs too.
 * What the FM-1 said instead (blell3, 9a90c7d, 2026-10-08): while advertising RXBUFnCNTL stays 00 and the packet is
 * in the buffer RXTOG has moved past (hw_adv_find); in a connection RXBUFnCNTL bit0 = 1 does mark the filled buffer
 * (470 packets, no desync); and the slot clock read through columns 0 / 14 steps backwards, so the link layer's timers
 * run on TIMER4 (ble_hw_time_us). TX follows the vendor's contract, HW §8.2 (bit0 = 1 empty; see hw_tx_service). */
#include "ble_hw.h"
#include "ble_vm.h"                     /* the stored trims rf_init takes (ble_vm.c) */
#include "ble_diag.h"                   /* console blell: counters only, no behaviour */
#ifndef BLE_HW_PLATFORM_INCLUDED
#include "fm1_ble.h"
#endif
#include "rf_share.h"                   /* the radio started once, shared with Bluetooth audio (bt/bt_inq.c) */

#ifndef BLE_HW_IRQ_PRIO
#define BLE_HW_IRQ_PRIO 2u              /* below the audio (3) and TIMER5 (4): stock's (HW §10) */
#endif
#define HW_LINK 0u                      /* one peripheral link: link 0 (HW §2.3, §3) */
#define HW_RXBUF 264u                   /* RX payload room: advertising 263, connection 255 (HW §4) */
#define HW_TXBUF 256u                   /* TX payload room: 251 + a 4-octet MIC */
#define HW_SWHDR 20u                    /* the software header before each payload (HW §4) */
#define HW_WIN_NORMAL 50u               /* WINCNTL2, the normal receive window in us (HW §3 0x112, §6 step 1) */

/* ---- the control block (HW §3: offsets [M:s]/[M:d], meanings as tagged there) */
struct ble_cb {
    volatile uint16_t anchor;          /* 0x000 bit15 set at connection, low bits dead time (4 on 1M) */
    volatile uint16_t bitoff;          /* 0x002 */
    volatile uint16_t advidx;          /* 0x004 [15:14] PDUs per advertising event, [13:0] their spacing in us */
    volatile uint16_t format;          /* 0x006 bit2 advertising, bit3 local address programmed, bit8 ignore SCAN_REQ */
    volatile uint16_t optcntl;         /* 0x008 bit4 local-address match disable */
    volatile uint16_t bdaddr[2];       /* 0x00A access address */
    volatile uint16_t txtog;           /* 0x00E bit0 the engine's TX buffer (moved by it only) */
    volatile uint16_t rxtog;           /* 0x010 bit0 the RX buffer filled next (engine) */
    volatile uint16_t txptr[2];        /* 0x012 TX payload offsets in the block */
    volatile uint16_t txahdr[2];       /* 0x016 advertising header: [3:0] type, bit4 TxAdd, bit5 RxAdd */
    volatile uint16_t txdhdr[2];       /* 0x01A [1:0] LLID, bit2 SN (engine's), bit3 MD, [15:8] length */
    volatile uint16_t rxptr[2];        /* 0x01E RX payload offsets */
    volatile uint16_t wincntl[2];      /* 0x022 receive window, us, low / high */
    volatile uint16_t rxahdr[2];       /* 0x026 received advertising header (Core layout) */
    volatile uint16_t rxdhdr[2];       /* 0x02A [1:0] LLID, bit2 NESN, bit3 SN, bit4 MD, [15:8] length */
    volatile uint16_t chmap[3];        /* 0x02E map bits 0-36, CHMAP2 [15:5] used channels */
    volatile uint16_t lastchmap;       /* 0x034 */
    volatile uint16_t rxmaxbuf;        /* 0x036 */
    volatile uint16_t rxstat[2];       /* 0x038 [3:0] = 1 good; bit8 the peer acknowledged our last */
    volatile uint16_t filtercntl;      /* 0x03C */
    volatile uint16_t whitelist[3];    /* 0x03E */
    volatile uint16_t crcword[2];      /* 0x044 CRC init [15:0], [23:16] */
    volatile uint16_t widen[2];        /* 0x048 whole slots; 0x5000 | us mod 625 */
    volatile uint16_t targetadr[3];    /* 0x04C */
    volatile uint16_t localadr[3];     /* 0x052 */
    volatile uint16_t evtcount;        /* 0x058 event counter of the last reception (engine) */
    volatile uint16_t rxbit;           /* 0x05A */
    volatile uint16_t ifscnt;          /* 0x05C */
    volatile uint16_t rfpriostat;      /* 0x05E */
    volatile uint16_t rfpriocntl;      /* 0x060 */
    volatile uint16_t intframe;        /* 0x062 bits 1, 2 at init; [5:4] 01 connected; bit6 MD of the TX PDU */
    volatile uint8_t txbufcntl[2];     /* 0x064 bit0: 1 empty / finished, 0 handed to the engine (HW §8.2) */
    volatile uint8_t rxbufcntl[2];     /* 0x066 bit0: 1 filled by the engine, 0 armed */
    volatile uint8_t frq_idx0[40];     /* 0x068 */
    volatile uint8_t frq_idx1[40];     /* 0x090 used data channels packed, then 37-39 */
    volatile uint8_t frq_tbl0[40];     /* 0x0B8 MHz - 2402 of channel index i */
    volatile uint8_t frq_tbl1[40];     /* 0x0E0 */
    volatile uint16_t ext[5];          /* 0x108 */
    volatile uint16_t wincntl2;        /* 0x112 */
    volatile uint16_t ext2[3];         /* 0x114 */
    volatile uint16_t unused[3];       /* 0x11A */
    volatile uint32_t tx_pwer;         /* 0x120 level + 16 */
    volatile uint32_t rx_gain0;        /* 0x124 */
    volatile uint32_t tx_set;          /* 0x128 */
    volatile uint32_t rx_set;          /* 0x12C */
    volatile uint32_t pll_comp;        /* 0x130 */
    volatile uint32_t mdm_set;         /* 0x134 */
    volatile uint16_t rssi[4];         /* 0x138 */
    volatile uint32_t anl_out;         /* 0x140 */
};
_Static_assert(sizeof(struct ble_cb) == 324u, "HW §3: the control block is 324 bytes");
_Static_assert(__builtin_offsetof(struct ble_cb, evtcount) == 0x58u, "HW §3 EVTCOUNT");
_Static_assert(__builtin_offsetof(struct ble_cb, txbufcntl) == 0x64u, "HW §3 TXBUF0CNTL");
_Static_assert(__builtin_offsetof(struct ble_cb, wincntl2) == 0x112u, "HW §3 WINCNTL2");
_Static_assert(__builtin_offsetof(struct ble_cb, tx_pwer) == 0x120u, "HW §3 TX_PWER");

/* ---- the baseband RAM block (HW §4): everything the engine reaches, at 16-bit offsets from its base */
struct ble_bb {
    volatile uint16_t inst[8];         /* 0x000 instance table: 0x8000 | link n's control block offset */
    uint8_t sw[0x30];                  /* 0x010 software area (unused) */
    struct ble_cb cb;                  /* 0x040 link 0 */
    struct { uint8_t buf[HW_SWHDR + HW_RXBUF]; } rx[2];   /* buf[18..19]: the PDU header for ble_ll_hw_rx */
    struct { uint8_t buf[HW_SWHDR + HW_TXBUF]; } tx[2];   /* buf[18..19]: the header ble_ll_hw_tx writes */
};
_Static_assert(__builtin_offsetof(struct ble_bb, cb) == 0x40u, "HW §4: link 0's block at +0x40, as stock");
#ifndef BLE_DMA_MEMORY
#define BLE_DMA_MEMORY __attribute__((aligned(4)))
#endif
static struct ble_bb bb BLE_DMA_MEMORY;

#define BB_OFF(p) ((uint16_t)((uintptr_t)(p) - (uintptr_t)&bb))   /* an engine offset (HW §3: below 65,535) */
#define CB (&bb.cb)

enum { HW_OFF, HW_ADV, HW_CONN, HW_SCAN, HW_INIT };
enum { UPD_CONN = 1, UPD_CHM = 2 };

static struct {
    uint8_t state, gen;                /* gen: moves on every state change (a callback may stop / restart the link) */
    uint8_t rx_next, rx_sn, rx_seen, rx_any;
    uint32_t rx_us;                     /* ChoralRoot 3y245: ble_hw_time_us of the last RX interrupt in this link */
    uint8_t win_wide, upd, sca;
    uint16_t interval, last_evt, wide_from, upd_instant, chm_instant;
    struct ble_hw_conn_upd u;
    uint8_t chm[5];
    struct ble_hw_adv adv;             /* the advertising set (the link layer's PDUs stay where they are) */
    uint8_t t_conn;                    /* t_us was taken in a connection (clk_step_max) */
    uint8_t tx_rec[2];                 /* a PDU of ours recorded in TX buffer b (handed over, not yet finished) */
    uint8_t tx_md[2];                  /* its MD bit */
    uint16_t tx_at[2];                 /* the event it was loaded in (tx_ack_evt_max) */
    uint32_t t_us;                     /* the last ble_hw_time_us */
#if BLE_CENTRAL
    uint8_t scan_ch, scan_active;      /* scanning: the channel the next event programs (HW §21.2), SCAN_REQ on */
    uint8_t bo_count, bo_sent, bo_succ, bo_fail;   /* the active-scan backoff (Core Vol 6 Part B 4.4.3.2) */
    uint16_t bo_upper;
    uint32_t scan_evts;
    /* initiating (state 3) and the master (state 6, HW §21.3 / §21.4) */
    uint8_t master;                    /* the connection is ours (state 6) */
    uint8_t init_hit, init_own_rand, init_peer_rand;   /* the target's ADV_IND seen: switch at the next event IRQ */
    uint8_t init_own[6], init_peer[6];
    uint32_t init_t_hit;               /* (diagnostics: its RX IRQ, ticks) */
    uint32_t init_t0;                  /* the engine listening for the target from here (ticks): ble_hw_init_start */
    struct ble_hw_conn ic;             /* our CONNECT_IND's LLData */
#endif
} drv;

/* counters for the console and the emulator test (read-only for the rest of the firmware) */
struct ble_hw_stats {
    uint32_t events, rx, rx_repeat, rx_error, tx, acked, connects, late_instant, isr_max_ticks;
};
static struct ble_hw_stats ble_hw_stat;

/* ------------------------------------------------------------------------------- diagnostics (blell) --- */

static struct {
    uint32_t base, us;                  /* TIMER4 ticks already counted, microseconds since the radio first started */
    uint32_t isr_t0, conn_t0;           /* this ISR's entry, state 7 written (ticks) */
    uint8_t first_rx, c3_seen;          /* per connection: first RX IRQ / first zero column 3 recorded */
} hwd;

/* microseconds since the first BLUETOOTH ON (ble_hw_wl82_start), from TIMER4 (24 MHz, wraps in 179 s: called at every
 * BLE interrupt, at least every advertising interval while the radio runs) */
BLE_API uint32_t ble_hw_diag_now(void)
{
    uint32_t d = (fm1_ticks() - hwd.base) / FM1_TICKS_PER_US;
    hwd.base += d * FM1_TICKS_PER_US;
    hwd.us += d;
    return hwd.us;
}

/* Stopping link 0 (HW §2.1 0x28038 bit1, §21.2 "Stop"): column 14 = 0, its interrupts off, then wait until the engine is
 * idle before anything programs the link again. On the FM-1 (blell-dev3 / dev4, 2026-10-09) bit1 was still set 2,000
 * polls (~0.75 ms) after every stop of a scanning link: the scan window that was open goes on to its end [I], so the
 * stops the main loop makes (advertising, scanning, initiating: ble_ll_scan, ble_ll_connect, ble_ll_enable) wait up to
 * a whole scanning window; a connection's stop (its end, in the BLE interrupts) up to one event; a link opened right
 * after a stop of ours finds it idle. Measured per path for `blell` (stop_*, busy_max in us, busy_timeouts). */
#define HW_STOP_WAIT_US ((uint32_t)BLE_SCAN_WINDOW * 625u + 2500u)   /* 40 ms: a scanning / initiating window */
#define HW_STOP_CONN_US 3000u           /* a connection event (the master's ended with its last packet) */
#define HW_STOP_OPEN_US 200u            /* hw_link_open: the stop before it waited already */

static void hw_stop(uint8_t path, uint32_t max_us)
{
    uint32_t us;
    fm1_ble_bc.stop = 0x5D000000u | (uint32_t)path << 16;  /* the breadcrumb: waiting (a watchdog reset keeps it) */
    us = fm1_ble_link_stop(HW_LINK, max_us);
    fm1_ble_bc.stop = 0x5E000000u | (uint32_t)path << 16 | (us > 0xFFFFu ? 0xFFFFu : us);
#if BLE_DIAG
    uint16_t u16 = (uint16_t)(us > 0xFFFFu ? 0xFFFFu : us);
    ble_dg.stop_n[path]++;
    ble_dg.stop_last = path;
    if (us)
        ble_dg.stop_busy[path]++;
    if (u16 > ble_dg.stop_us_max[path])
        ble_dg.stop_us_max[path] = u16;
    if (u16 > ble_dg.busy_max)
        ble_dg.busy_max = u16;
#endif
    if (us >= max_us)
        BLE_DG(ble_dg.busy_timeouts++);
    if (us >= 50u || us >= max_us)                         /* "busy <path> <us>" in the ring */
        ble_diag_ev(BDE_BUSY, (uint32_t)path << 13 | (us >> 3 > 0x1FFFu ? 0x1FFFu : us >> 3));
}

static void hw_cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    while (n--)
        *d++ = *s++;
}

/* ------------------------------------------------------------------------------------------- time base --- */

/* The link layer's timers (supervision, the 40 s procedure timeout, the establishment rule): TIMER4 microseconds,
 * monotonic (ble_hw_diag_now), not the engine's slot clock. On the FM-1 (blell3, 9a90c7d, 2026-10-08) the slot clock
 * read through columns 0 / 14 (op 2) stepped BACKWARDS inside a connection: clk_step_max 16777215, and
 * close_since_start_us 1,895,854,158 = (2^24 - k) x 625 mod 2^32 + the real time, i.e. a step back of k ~ 267 slots
 * read as a jump of ~10,486 s forward, so the LL response timeout (0x22, 40 s) fired 0.2-2.1 s into every connection
 * with a feature exchange pending. TIMER4 is read with a plain load (no column op), so the ISRs also issue three
 * column reads fewer per call. The engine keeps its anchors on its own clock. clk_step_max now holds the largest step
 * of this clock between two calls in a connection (us; a sanity check: about one interval). */
BLE_API uint32_t ble_hw_time_us(void)
{
    uint32_t t = ble_hw_diag_now();
#if BLE_DIAG
    uint32_t step = t - drv.t_us;
    if (drv.state == HW_CONN && drv.t_conn && step > ble_dg.clk_step_max)
        ble_dg.clk_step_max = step;
#endif
    drv.t_us = t;
    drv.t_conn = drv.state == HW_CONN;
    return t;
}

BLE_API void ble_hw_rand(uint8_t *out, uint8_t n)
{
    while (n) {
        uint32_t r = fm1_ble_rng32(), k;
        for (k = 0; k < 4u && n; k++, n--, r >>= 8)
            *out++ = (uint8_t)r;
    }
}

/* VM id 104 (HW §11): a provisioned BLE address, public. The VM can be read now (ble_vm.c, HW §14), but stock V15
 * writes no 104 and a hand-made one changed nothing (HW §14.5): always "absent", and the caller gets a fresh random
 * static address (the firmware keeps it in its settings: midi_ble.c). */
static int hw_vm_addr(uint8_t addr[6])
{
    (void)addr;
    return 0;
}

BLE_API uint8_t ble_hw_addr(uint8_t addr[6])
{
    uint32_t a, b;
    if (hw_vm_addr(addr))
        return 0;
    do {
        a = fm1_ble_rng32();
        b = fm1_ble_rng32() & 0xFFFFu;
    } while ((a == 0 && (b & 0x3FFFu) == 0) || (a == 0xFFFFFFFFu && (b & 0x3FFFu) == 0x3FFFu));
    b |= 0xC000u;                       /* random static: the top two bits 11, the rest not all 0 / 1 (Core Vol 6
                                         * Part B 1.3.2.1) */
    addr[0] = (uint8_t)a;
    addr[1] = (uint8_t)(a >> 8);
    addr[2] = (uint8_t)(a >> 16);
    addr[3] = (uint8_t)(a >> 24);
    addr[4] = (uint8_t)b;
    addr[5] = (uint8_t)(b >> 8);
    return 1;
}

/* ------------------------------------------------------------------------------------- control block --- */

static void cb_rfprio(uint16_t p)       /* "RFPRIO n" (HW §6, §7, §9): both words, as stock's dump shows */
{
    CB->rfpriostat = p;
    CB->rfpriocntl = p;
}

/* the channel tables for map m (HW §6 end, §9): FRQ_IDX0/TBL0 every index, FRQ_IDX1/TBL1 the used data channels
 * packed then 37-39 at their own places, CHMAP0/1/2 the map and the used count */
static uint8_t hw_freq(uint32_t i)      /* MHz - 2402 of channel index i (HW §3 0x0B8) */
{
    if (i == 37u)
        return 0;
    if (i == 38u)
        return 24u;
    if (i == 39u)
        return 78u;
    return (uint8_t)(i <= 10u ? 2u * (i + 1u) : 2u * (i + 1u) + 2u);
}

static void cb_channels(const uint8_t chm[5])
{
    uint32_t i, used = 0;
    for (i = 0; i < 40u; i++) {
        CB->frq_idx0[i] = (uint8_t)i;
        CB->frq_tbl0[i] = hw_freq(i);
    }
    for (i = 0; i < 37u; i++)
        if (chm[i >> 3] >> (i & 7u) & 1u) {
            CB->frq_idx1[used] = (uint8_t)i;
            CB->frq_tbl1[used] = hw_freq(i);
            used++;
        }
    for (i = used; i < 40u; i++) {
        CB->frq_idx1[i] = (uint8_t)(i >= 37u ? i : 0u);
        CB->frq_tbl1[i] = i >= 37u ? hw_freq(i) : 0u;
    }
    CB->chmap[0] = (uint16_t)(chm[0] | chm[1] << 8);
    CB->chmap[1] = (uint16_t)(chm[2] | chm[3] << 8);
    CB->chmap[2] = (uint16_t)((chm[4] & 0x1Fu) | used << 5);
}

/* window widening, us (HW §7): 2 + 2 x floor(interval_us x (SCA ppm + 200) x (latency + 1) / 10^6), latency 0 */
static uint32_t hw_widening(uint16_t interval, uint8_t sca)
{
    static const uint16_t PPM[8] = {500, 250, 150, 100, 75, 50, 30, 20};   /* Core Vol 6 Part B 2.3.3.1 */
    return 2u + 2u * ((uint32_t)interval * 1250u * (PPM[sca & 7u] + 200u) / 1000000u);
}

static void cb_widen(uint32_t us)       /* HW §7 step 10 */
{
    CB->widen[0] = (uint16_t)(us / 625u);
    CB->widen[1] = (uint16_t)(0x5000u | us % 625u);
}

static void cb_window(uint32_t us)      /* WINCNTL0/1 (HW §3, §7 step 13) */
{
    CB->wincntl[0] = (uint16_t)us;
    CB->wincntl[1] = (uint16_t)(us >> 16);
}

/* a link opened (HW §5.5 end): every column 0, column 14 again, interrupts off; the block as stock initialises it */
static void hw_link_open(void)
{
    static const uint8_t ALL[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
    uint32_t c;
    uint8_t *p = (uint8_t *)&bb.sw;
    fm1_ble_step(FM1_BLE_STEP_LINK_STOP);
    hw_stop(BDS_OPEN, HW_STOP_OPEN_US);
    fm1_ble_step(FM1_BLE_STEP_LINK_OPEN);
    for (c = 0; c <= 16u; c++)
        fm1_ble_col_wr(HW_LINK, c, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0);
    for (c = 0; c < sizeof bb.sw; c++)
        p[c] = 0;
    {   /* the control block from zero (plain stores: the link is stopped) */
        volatile uint8_t *q = (volatile uint8_t *)&bb.cb;
        for (c = 0; c < sizeof bb.cb; c++)
            q[c] = 0;
    }
    bb.inst[HW_LINK] = (uint16_t)(0x8000u | BB_OFF(CB));  /* HW §4 instance table */
    CB->anchor = 0;                                        /* HW §5.5 list [M:s] */
    CB->bitoff = 0;
    CB->advidx = 0xD388u;
    CB->widen[0] = 0;
    CB->widen[1] = 0x51E7u;
    CB->bdaddr[0] = 0xBED6u;                               /* the advertising access address 0x8E89BED6 */
    CB->bdaddr[1] = 0x8E89u;
    CB->crcword[0] = 0x5555u;
    CB->crcword[1] = 0x0055u;
    CB->optcntl = 0x28u;
    CB->optcntl |= 0x200u;
    CB->txptr[0] = BB_OFF(bb.tx[0].buf + HW_SWHDR);        /* (stock: placeholder 1 until advertising) */
    CB->txptr[1] = BB_OFF(bb.tx[1].buf + HW_SWHDR);
    CB->intframe |= 2u;
    CB->intframe |= 4u;
    CB->rxbufcntl[0] &= (uint8_t)~1u;
    CB->rxbufcntl[1] &= (uint8_t)~1u;
    CB->tx_pwer = 0x16u;                                   /* level 6 + 16, as stock (U10: dBm unknown) */
    CB->tx_set = 0x16u;
    CB->pll_comp = 0;                                      /* TODO(hardware) HW §3 0x130: -(offset x 2^20) / 24,000
                                                            * from VM 110 (format unknown, U3 / U14) */
    CB->rfpriocntl = 0x101u;
    CB->rfpriostat = 1u;
    CB->txtog = 0;
    CB->rxtog = 0;
    CB->txahdr[0] = CB->txahdr[1] = 0;
    CB->txdhdr[0] = CB->txdhdr[1] = 1u;
    CB->rxmaxbuf = 255u;
    CB->ifscnt = 0x8295u;
    CB->filtercntl = 0;
    cb_channels(ALL);
}

/* ---------------------------------------------------------------------------------------- advertising --- */

#define HW_RX_WIPE 40u                  /* bytes zeroed from 2 before RXPTR: header + the 34-octet CONNECT_IND + 4 */

/* an advertising RX buffer zeroed with its RXAHDR / RXDHDR / RXSTAT (hw_adv_find: a new packet from an old one) */
static void hw_rx_wipe(uint32_t b)
{
    uint8_t *p = &bb.rx[b].buf[HW_SWHDR - 2u];
    uint32_t i;
    for (i = 0; i < HW_RX_WIPE; i++)
        p[i] = 0;
    CB->rxahdr[b] = 0;
    CB->rxdhdr[b] = 0;
    CB->rxstat[b] = 0;
}


static void hw_adv_buffer(uint32_t b, const uint8_t *pdu, uint8_t len)   /* HW §6 step 8 */
{
    uint8_t n = (uint8_t)(len - 2u);
    hw_cpy(bb.tx[b].buf + HW_SWHDR, pdu + 2, n);
    CB->txahdr[b] = (uint16_t)((pdu[0] & 0x0Fu) | ((pdu[0] >> 6) & 3u) << 4);   /* TxAdd / RxAdd: Core bits 6 / 7 -> 4 / 5 */
    CB->txdhdr[b] = (uint16_t)(n << 8);
}

static void hw_adv_program(void)
{
    const struct ble_hw_adv *a = &drv.adv;
    const uint8_t *adva = a->adv + 2;
    hw_link_open();
    fm1_ble_step(FM1_BLE_STEP_ADV_PROG);
    cb_rfprio(26u);                                        /* HW §6 step 1 */
    CB->crcword[0] = 0x5555u;
    CB->crcword[1] = 0x0055u;
    cb_window(HW_WIN_NORMAL);
    CB->wincntl2 = HW_WIN_NORMAL;
    fm1_ble_col_wr(HW_LINK, 1, a->interval);              /* step 2: interval, event enable */
    fm1_ble_col_wr(HW_LINK, 15, 0x8000u | (uint32_t)a->interval >> 16);
    CB->filtercntl = 0;                                    /* step 3: policy 0 */
    CB->advidx = (uint16_t)(3u << 14 | 1250u);             /* step 4: 3 PDUs, 1,250 us apart */
    fm1_ble_col_wr(HW_LINK, 8, 0xC000u);                   /* step 5: advDelay on */
    /* step 6: 37 first, step 1, all three. Bits 6 / 7 come from the channel mask (U15): only stock's value for all
     * three channels is known, so a.channels other than 7 still advertises on all three */
    fm1_ble_col_wr(HW_LINK, 6, 0x41A5u);
    CB->rxptr[0] = BB_OFF(bb.rx[0].buf + HW_SWHDR);        /* step 7: two RX buffers armed */
    CB->rxptr[1] = BB_OFF(bb.rx[1].buf + HW_SWHDR);
    CB->rxbufcntl[0] &= (uint8_t)~1u;
    CB->rxbufcntl[1] &= (uint8_t)~1u;
    hw_rx_wipe(0);                                         /* (ours: a new packet tells itself from an old one) */
    hw_rx_wipe(1);
    drv.rx_next = 0;
    hw_adv_buffer(0, a->adv, a->adv_len);                  /* step 8: ADV_IND, SCAN_RSP */
    hw_adv_buffer(1, a->scan_rsp, a->scan_rsp_len);
    CB->txtog = 0;
    CB->optcntl &= (uint16_t)~0x10u;                       /* step 9: local-address match on */
    CB->localadr[0] = (uint16_t)(adva[0] | adva[1] << 8);
    CB->localadr[1] = (uint16_t)(adva[2] | adva[3] << 8);
    CB->localadr[2] = (uint16_t)(adva[4] | adva[5] << 8);
    CB->format = 0x000Cu;
    fm1_ble_sync();
    fm1_ble_col_wr(HW_LINK, 2, 0x2000u);                   /* step 10: state 2 */
    fm1_ble_link_irqs_on(HW_LINK);                         /* step 11 */
    fm1_ble_col_wr(HW_LINK, 7, 0);                         /* step 12: start (column 0 = start slot - 1 = 0) */
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0);
    fm1_ble_col_wr(HW_LINK, 0, 0);
    fm1_ble_col_wr(HW_LINK, 14, 0x8000u);
    drv.state = HW_ADV;
    drv.gen++;
    fm1_ble_step(FM1_BLE_STEP_ADV_STARTED);
    BLE_DG(ble_dg.adv_starts++);                                   /* (no column read here: see below) */
    ble_diag_ev(BDE_ADV_START, a->interval);
    /* 1f0ab85 read columns 2, 14, 15 (op 2) right here, the command port's next command straight after the start
     * (column 14 = 0x8000): the first BLUETOOTH ON from the menu then hung the FM-1 until its watchdog reset it
     * (2026-10-08, prev_stage 9, prev_rst 0x04), where 1cc6e04 without the reads advertised. Op 2 is known from
     * static analysis only (HW §2.1, never traced), so the start path and the interrupts issue no column read that
     * 1cc6e04 did not; `blell regs` reads them on request. */
}

BLE_API void ble_hw_adv_start(const struct ble_hw_adv *a)
{
    drv.adv = *a;
    hw_adv_program();
}

BLE_API void ble_hw_adv_stop(void)
{
    hw_stop(drv.state == HW_CONN ? BDS_CONN : BDS_ADV, drv.state == HW_CONN ? HW_STOP_CONN_US : HW_STOP_WAIT_US);
    ble_diag_ev(BDE_ADV_STOP, drv.state);
    drv.state = HW_OFF;
    drv.gen++;
}

#if BLE_CENTRAL
#include "ble_hw_wl82_central.c"      /* scanning, initiating, the master's set-up (HW §21) */
#endif

/* ----------------------------------------------------------------------------------------- connection --- */

/* called by the link layer from inside ble_ll_hw_connect_ind (the RX interrupt), HW §7 in the vendor's order */
BLE_API void ble_hw_conn_start(const struct ble_hw_conn *c)
{
    uint16_t h0;
    cb_rfprio(28u);                                        /* 1 */
    CB->anchor = 0x8000u;
    CB->txtog &= (uint16_t)~2u;
    CB->rxtog = 0;
    CB->bdaddr[0] = (uint16_t)c->aa;                       /* 2 */
    CB->bdaddr[1] = (uint16_t)(c->aa >> 16);
    CB->crcword[0] = (uint16_t)c->crc_init;
    CB->crcword[1] = (uint16_t)(c->crc_init >> 16 & 0xFFu);
    CB->optcntl |= 4u;                                     /* 3 */
    CB->optcntl &= (uint16_t)~0x200u;
    CB->optcntl |= 0xC00u;
    CB->optcntl |= 0x1000u;
    CB->intframe = (uint16_t)((CB->intframe & ~0x30u) | 0x10u);   /* 4 */
    CB->txbufcntl[0] |= 1u;                                /* 5: both TX buffers empty (bit0 = 1: the only 1-writes) */
    CB->txbufcntl[1] |= 1u;
    CB->anchor = (uint16_t)((CB->anchor & 0x8000u) | 4u);  /* 6: 1M dead time */
    fm1_ble_col_wr(HW_LINK, 5, 0);                         /* 7: no instant */
    fm1_ble_col_wr(HW_LINK, 4, c->win_offset ? 0x8000u | (2u * c->win_offset - 1u) : 0u);   /* 8 */
    fm1_ble_col_wr(HW_LINK, 2, 7u << 12);                  /* 9: state 7, latency 0 (we listen every event) */
    cb_widen(hw_widening(c->interval, c->sca));            /* 10 */
    fm1_ble_col_wr(HW_LINK, 8, 0);                         /* 11: advDelay off */
    fm1_ble_col_wr(HW_LINK, 3, 0);                         /* 12 */
    CB->evtcount = 0;
    cb_window((uint32_t)c->win_size * 1250u + 1250u);      /* 13: the first receive window */
    CB->wincntl2 = HW_WIN_NORMAL;
    fm1_ble_col_wr(HW_LINK, 1, 2u * c->interval);         /* 14 */
    fm1_ble_col_wr(HW_LINK, 15, 0x8000u | (2u * c->interval) >> 16);
    cb_channels(c->chm);                                   /* 16 */
    fm1_ble_col_wr(HW_LINK, 6, 0x8000u | (uint32_t)c->hop << 8 | c->hop);
    CB->txahdr[0] = CB->txahdr[1] = 0;                     /* 17: empty PDUs, opposite SN */
    h0 = (uint16_t)((CB->txtog & 1u) << 2 | 1u);
    CB->txdhdr[0] = h0;
    CB->txdhdr[1] = (uint16_t)(h0 ^ 4u);                  /* (§7 reads XOR 5: LLID 0 on one; §8.2: LLID 1 on both) */
    /* 18: the TX buffers stay the advertising ones; 19: no channel selection #2 (our ADV_IND has ChSel 0) */
    fm1_ble_sync();
    drv.state = HW_CONN;
#if BLE_CENTRAL
    drv.master = 0;
#endif
    drv.gen++;
    drv.rx_next = 0;
    drv.rx_sn = 0;
    drv.rx_seen = drv.rx_any = 0;
    drv.rx_us = ble_hw_time_us();
    drv.tx_rec[0] = drv.tx_rec[1] = 0;                     /* no PDU recorded in either buffer (HW §8.2 point 7) */
    drv.tx_md[0] = drv.tx_md[1] = 0;
    drv.upd = 0;
    drv.win_wide = 1;
    drv.wide_from = 0;
    drv.interval = c->interval;
    drv.sca = c->sca;
    drv.last_evt = 0xFFFFu;
    ble_hw_stat.connects++;
    hwd.conn_t0 = fm1_ticks();                             /* (diagnostics: after the engine has state 7) */
    hwd.first_rx = hwd.c3_seen = 0;
    BLE_DG(ble_dg.cind_isr_us = (hwd.conn_t0 - hwd.isr_t0) / FM1_TICKS_PER_US);
    BLE_DG(ble_dg.first_rx_us = 0);
    BLE_DG(ble_dg.first_rx_evt = ble_dg.first_evt = 0xFFFFu);
    BLE_DG(ble_diag_ev(BDE_CONN_SET, ble_dg.cind_isr_us));
}

BLE_API void ble_hw_conn_stop(void)
{
    ble_hw_adv_stop();                                     /* column 14 = 0: the link stops (HW §2.3) */
}

BLE_API void ble_hw_conn_update(const struct ble_hw_conn_upd *u)
{
    drv.u = *u;
    drv.upd_instant = u->instant;
    drv.upd |= UPD_CONN;
    fm1_ble_col_wr(HW_LINK, 5, u->instant);                /* HW §9: column 5 = instant at once */
    cb_rfprio(30u);
}

BLE_API void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant)
{
    hw_cpy(drv.chm, chm, 5);
    drv.chm_instant = instant;
    drv.upd |= UPD_CHM;
    fm1_ble_col_wr(HW_LINK, 5, instant);
    cb_rfprio(30u);
}

/* the data length: RXMAXBUF stays 255 (HW §3), the TX buffers hold 255; nothing to program. Whether the engine
 * times 251-octet PDUs is open (docs/BLE-STACK.md §12); stock never uses DLE (model) */
BLE_API void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx)
{
    (void)max_tx;
    (void)max_rx;
}

/* from the link layer, always inside one of our interrupts: the next connection RX interrupt loads it (HW §8.2: the
 * service never runs from the event interrupt, and the RX interrupt's ends with it) */
BLE_API void ble_hw_tx_kick(void) {}

/* ------------------------------------------------------------------------------------ event servicing --- */

/* ---- TX: the vendor's contract, docs/BLE-HW-FACTS.md §8.2 (a3b2f06). TXBUFnCNTL bit0 is the buffer's "empty" flag:
 * 1 = empty (software may fill it; a PDU software had put there is finished: acknowledged), 0 = handed to the engine.
 * conn_start sets both to 1 (HW §7 step 5) and software never sets bit0 again; it clears bit0 as the last write of a
 * load. The engine sends from the buffer TXTOG bit0 names and moves TXTOG itself; it sets bit0 back to 1 when it is
 * done with the PDU. Software never writes TXTOG, never changes TXDHDR bit2 (the set-up value, the engine's), never
 * moves a PDU between the buffers and never frees a buffer the engine holds; the only acknowledgement is bit0 back to 1
 * on a buffer we loaded. The service runs at the end of every connection RX interrupt, after the RX buffers (never
 * from the event interrupt): a consistent snapshot of TXTOG bit0 and both bit0s, then the TXTOG buffer, then the other.
 * Earlier drivers (5008663 .. 51792b7) had the polarity backwards (1 = loaded), hence blell4-9's stalls (§8.2 point 6).
 * RAM only (blell txs_*). */
#if BLE_DIAG
static void hw_tx_snap(uint8_t what, uint32_t b, uint32_t s)
{
    struct ble_diag_txs *x = &ble_dg.txs[ble_dg.txs_n++ & (BLE_DIAG_TXS - 1u)];
    x->t_us = ble_hw_diag_now();
    x->evt = drv.last_evt;
    x->txtog = CB->txtog;
    x->txdhdr[0] = CB->txdhdr[0];
    x->txdhdr[1] = CB->txdhdr[1];
    x->intframe = CB->intframe;
    x->txptr[0] = CB->txptr[0];
    x->txptr[1] = CB->txptr[1];
    x->rxdhdr = CB->rxdhdr[(CB->rxtog & 1u) ^ 1u];      /* the central's last header: its NESN (bit2) / SN (bit3) */
    x->cntl[0] = CB->txbufcntl[0];
    x->cntl[1] = CB->txbufcntl[1];
    x->what = what;
    x->b = (uint8_t)b;
    x->n = (uint8_t)(drv.tx_rec[0] | drv.tx_rec[1] << 1);
    x->snap = (uint8_t)s;
    if (what == BTX_LOAD && !ble_dg.txs_first.what)
        ble_dg.txs_first = *x;
}
#else
#define hw_tx_snap(what, b, s) ((void)(s))
#endif

/* bit0 TXTOG bit0, bit1 TXBUF0CNTL bit0, bit2 TXBUF1CNTL bit0 */
static uint32_t hw_tx_read(void)
{
    return (CB->txtog & 1u) | (CB->txbufcntl[0] & 1u) << 1 | (CB->txbufcntl[1] & 1u) << 2;
}

/* buffer b reads empty: our PDU in it is acknowledged; then the next PDU (control first: ble_ll_hw_tx) goes in.
 * 0: the link went with the acknowledgement (our LL_TERMINATE_IND) */
static int hw_tx_empty(uint32_t b, uint32_t s)
{
    uint8_t g = drv.gen, n, md, *pdu;
    if (drv.tx_rec[b]) {
        drv.tx_rec[b] = 0;
#if BLE_DIAG
        uint16_t age = (uint16_t)(drv.last_evt - drv.tx_at[b]);
        if (age != 0xFFFFu && age > ble_dg.tx_ack_evt_max)
            ble_dg.tx_ack_evt_max = age;
#endif
        hw_tx_snap(BTX_ACK, b, s);
        ble_hw_stat.acked++;
        BLE_DG(ble_dg.tx_acked++);
        ble_ll_hw_tx_acked();
        if (drv.gen != g || drv.state != HW_CONN)
            return 0;
    }
    pdu = &bb.tx[b].buf[HW_SWHDR - 2u];
    n = ble_ll_hw_tx(pdu);
    if (!n) {
        CB->intframe &= (uint16_t)~0x40u;                  /* nothing queued: bit0 stays 1, the engine sends empty */
        BLE_DG(ble_dg.tx_none++);
        return 1;
    }
    md = (uint8_t)(pdu[0] >> 4 & 1u);                      /* the link layer's MD: another PDU still queued */
    CB->txahdr[b] = 0;
    CB->txdhdr[b] = (uint16_t)((CB->txdhdr[b] & 4u) | (uint32_t)pdu[1] << 8 | (uint32_t)md << 3 | (pdu[0] & 3u));
    CB->intframe = (uint16_t)((CB->intframe & ~0x40u) | (uint32_t)md << 6);
    RING_PUBLISH();
    fm1_ble_sync();                                        /* payload and header in SRAM before the hand-over */
    CB->txbufcntl[b] &= (uint8_t)~1u;                      /* the last write: the engine's now */
    drv.tx_rec[b] = 1;
    drv.tx_md[b] = md;
    drv.tx_at[b] = drv.last_evt;
    ble_hw_stat.tx++;
    BLE_DG(ble_dg.tx_queued++);
    hw_tx_snap(BTX_LOAD, b, s);
    return 1;
}

/* buffer b is the engine's: untouched, except MD on our own PDU in it when more has been queued since */
static void hw_tx_held(uint32_t b)
{
    if (!drv.tx_rec[b]) {
        BLE_DG(ble_dg.tx_eng_held++);                              /* bit0 0 with nothing of ours (seen at the first event) */
        return;
    }
    if (!drv.tx_md[b] && ble_ll_hw_tx_pending()) {
        drv.tx_md[b] = 1;
        CB->txdhdr[b] = (uint16_t)(CB->txdhdr[b] | 8u);   /* (bit2 and the rest as they are) */
    }
}

static void hw_tx_service(void)
{
    uint32_t s1, s2, s, t, k;
    if (drv.state != HW_CONN)
        return;
    s1 = hw_tx_read();
    s2 = hw_tx_read();
    s = ((s1 ^ s2) & 1u) ? s2 : s1;                        /* TXTOG moved between the reads: the second */
    t = s & 1u;
    for (k = 0; k < 2u; k++) {
        uint32_t b = t ^ k;
        if (s >> (1u + b) & 1u) {
            if (!hw_tx_empty(b, s))
                return;
        } else
            hw_tx_held(b);
    }
}

/* ---- RX while advertising. On the FM-1 (510e616, 2026-10-08) every RX IRQ while a Mac scanned and connected found
 * RXBUFnCNTL bit0 = 0 on rx_next and on the other buffer (rx_irqs 14, rx_nothing 14): the engine's RX semantics are
 * not the model's. HW §3 marks RXTOG and the CNTL direction [I] (U7), and where an advertising PDU's header and length
 * go is not in the sheet. So the driver looks for the packet by several rules, in this order, and counts the one that
 * found it (blell rxf_*): CNTL bit0 = 1 on rx_next (the model), on the other buffer, then the content alone: a
 * SCAN_REQ / CONNECT_IND carries our AdvA at payload offset 6 (Core Vol 6 Part B 2.3.2), so a buffer holding our
 * AdvA there (payload at RXPTR, the sheet's layout) or at offset 8 (the 2 header bytes at RXPTR) holds a packet to us.
 * To tell a new packet from an old one without CNTL, every RX buffer's first 40 bytes and its RXAHDR / RXDHDR /
 * RXSTAT are zeroed when armed. RAM only: no column read (op 2) on this path (see hw_adv_program's note). */
/* ChoralRoot 3y245: EVTCOUNT alone keeps a link alive only this long after the last RX interrupt. On the FM-1 with
 * both cores the receiver went deaf while EVTCOUNT kept following the events: the link layer saw a reception in every
 * event, its supervision timer never ran, and the unit stayed "connected" to a Mac that had dropped it, never
 * advertising again. A healthy link takes ~50 RX interrupts a second (7.5 ms, a Mac). */
#define HW_RX_ALIVE_US 1000000u
#define HW_RX_POLL_US 600u              /* RX ISR poll for a late fill: a CONNECT_IND is 352 us on air at 1M */
#define HW_RX_SETTLE_US 400u            /* a content-only find this soon after the IRQ waits for the packet's end */

struct hw_adv_pdu {
    uint8_t *pdu;                       /* header (2) then payload, as ble_ll_hw_connect_ind takes it */
    uint8_t type, len, layout;
};

static int hw_any(const uint8_t *p, uint32_t n)
{
    while (n--)
        if (*p++)
            return 1;
    return 0;
}

/* a SCAN_REQ (3, 12 octets) or CONNECT_IND (5, 34) to our AdvA in buffer b, in either layout; 0: none */
static int hw_adv_parse(uint32_t b, struct hw_adv_pdu *o)
{
    uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    const uint8_t *adva = drv.adv.adv + 2;
    uint8_t t = (uint8_t)(CB->rxahdr[b] & 0x0Fu), n = (uint8_t)(CB->rxdhdr[b] >> 8);
    if (hw_any(p, 6) && ble_eq(p + 6, adva, 6)) {          /* layout 1: payload at RXPTR (HW §4, the model) */
        o->pdu = p - 2;
        o->layout = 1;
        if (t != 3u && t != 5u) {                          /* RXAHDR not written: the type from the length or the
                                                            * LLData (AA, CRCInit... never all 0) */
            t = n == 12u ? 3u : n == 34u ? 5u : hw_any(p + 12, 22) ? 5u : 3u;
            /* RxAdd (bit7) = our TxAdd; TxAdd (bit6) unknown, 1 assumed (a central's random address) */
            o->pdu[0] = (uint8_t)(t | 0x40u | (drv.adv.adv[0] >> 6 & 1u) << 7);
        } else
            o->pdu[0] = (uint8_t)CB->rxahdr[b];
        o->type = t;
        o->len = t == 3u ? 12u : 34u;
        o->pdu[1] = o->len;
        return 1;
    }
    t = (uint8_t)(p[0] & 0x0Fu);                           /* layout 2: the header bytes at RXPTR */
    if (((t == 3u && p[1] == 12u) || (t == 5u && p[1] == 34u)) && hw_any(p + 2, 6) && ble_eq(p + 8, adva, 6)) {
        o->pdu = p;
        o->layout = 2;
        o->type = t;
        o->len = p[1];
        return 1;
    }
    return 0;
}

#if BLE_DIAG
static void hw_rx_snap(uint8_t where, uint8_t found, uint8_t layout, uint32_t wait_us)
{
    struct ble_diag_rxs *s = &ble_dg.rxs[ble_dg.rxs_n++ & (BLE_DIAG_RXS - 1u)];
    uint32_t b, i;
    s->t_us = ble_hw_diag_now();
    s->rxtog = CB->rxtog;
    s->ifscnt = CB->ifscnt;
    for (b = 0; b < 2u; b++) {
        s->stat[b] = CB->rxstat[b];
        s->ahdr[b] = CB->rxahdr[b];
        s->dhdr[b] = CB->rxdhdr[b];
        s->cntl[b] = CB->rxbufcntl[b];
        for (i = 0; i < 4u; i++)
            s->b[b][i] = bb.rx[b].buf[HW_SWHDR + i];
    }
    s->where = where;
    s->found = found;
    s->rx_next = drv.rx_next;
    s->layout = layout;
    s->wait_us = (uint16_t)(wait_us > 0xFFFFu ? 0xFFFFu : wait_us);
    if (found && !ble_dg.rxs_first.found)
        ble_dg.rxs_first = *s;
}
#else
#define hw_rx_snap(where, found, layout, wait_us) ((void)(layout))   /* (layout: hw_adv_parse runs as before) */
#endif

/* the buffer holding a new advertising-channel PDU, by the rules in this order (*found: BDF_*), else -1. Measured on
 * the FM-1 (blell3, 9a90c7d: 54 of 54 finds, rxf_tog_prev 50 + rxf_late 5 by the event ISR's look, cntl 0, layout 0 =
 * payload at RXPTR, header in RXAHDR / RXDHDR): the packet is in the buffer RXTOG has moved PAST, and RXBUFnCNTL stays
 * 00 while advertising. So that rule goes first; CNTL on rx_next, CNTL on the other buffer and the content of RXTOG's
 * own buffer stay as fallbacks, each counted (the emulator's model sets CNTL and also moves RXTOG past the buffer) */
static int hw_adv_find(uint8_t *found)
{
    struct hw_adv_pdu o;
    uint32_t n = drv.rx_next & 1u, prev = (CB->rxtog & 1u) ^ 1u;
    if (hw_adv_parse(prev, &o)) {
        *found = BDF_TOG_PREV;
        return (int)prev;
    }
    if (CB->rxbufcntl[n] & 1u) {
        *found = BDF_CNTL;
        return (int)n;
    }
    if (CB->rxbufcntl[n ^ 1u] & 1u) {
        *found = BDF_CNTL_OTHER;
        return (int)(n ^ 1u);
    }
    if (hw_adv_parse(prev ^ 1u, &o)) {
        *found = BDF_TOG_CUR;
        return (int)(prev ^ 1u);
    }
    *found = BDF_NONE;
    return -1;
}

/* a CONNECT_IND (or a stored SCAN_REQ) while advertising, in buffer b (HW §7) */
static void hw_rx_adv(uint32_t b, uint8_t found)
{
    struct hw_adv_pdu o;
    uint16_t ah = CB->rxahdr[b], st = CB->rxstat[b];
    uint8_t s = (uint8_t)(st & 0xFu);
    int ok = hw_adv_parse(b, &o);
    switch (found) {
    case BDF_CNTL: BLE_DG(ble_dg.rxf_cntl++); break;
    case BDF_CNTL_OTHER: BLE_DG(ble_dg.rxf_cntl_other++); break;
    case BDF_TOG_PREV: BLE_DG(ble_dg.rxf_tog_prev++); break;
    default: BLE_DG(ble_dg.rxf_tog_cur++); break;
    }
    if (ok && o.layout == 2u)
        BLE_DG(ble_dg.rxl_buf++);
    else if (ok)
        BLE_DG(ble_dg.rxl_cb++);
    else
        BLE_DG(ble_dg.rxl_none++);
    if (ok && o.layout == 1u && (ah & 0x0Fu) != 3u && (ah & 0x0Fu) != 5u)
        BLE_DG(ble_dg.rxh_synth++);
    CB->rxbufcntl[b] &= (uint8_t)~1u;                      /* re-armed: conn_start below does not read RX */
    drv.rx_next = (uint8_t)(b ^ 1u);
    BLE_DG(ble_dg.adv_rx++);
    if (ok && s == 0u)
        BLE_DG(ble_dg.rx_stat_zero++);                             /* RXSTAT never written: the content decides */
    else if (ok && s != 1u)
        BLE_DG(ble_dg.rx_stat_bad_valid++);
    if (!ok || (s != 0u && s != 1u) || o.type != 0x5u) {
        if (ok && o.type == 0x3u && (s == 0u || s == 1u))
            BLE_DG(ble_dg.scan_req++);                             /* a stored SCAN_REQ: the engine answered it */
        else {
            BLE_DG(ble_dg.adv_drop++);                             /* not passed on, and advertising not restarted */
            BLE_DG(ble_dg.adv_drop_stat = st);
            BLE_DG(ble_dg.adv_drop_hdr = ah);
            ble_diag_ev(BDE_ADV_DROP, (uint32_t)(st & 0xFFu) | (uint32_t)(ah & 0xFFu) << 8);
        }
        hw_rx_wipe(b);
        return;
    }
    CB->rxahdr[b] = CB->rxdhdr[b] = CB->rxstat[b] = 0;     /* (the payload is wiped when advertising re-arms) */
    if (!ble_ll_hw_connect_ind(o.pdu, (uint8_t)(o.len + 2u)) && drv.state == HW_ADV)
        hw_adv_program();               /* not taken: the engine stopped advertising on it (model), so start again */
}

/* RX ISR, advertising: snapshot, look, and when nothing is there yet poll RAM a little (the IRQ may come at the
 * access address, before the packet's end); a content-only find waits for the packet's end (HW_RX_SETTLE_US) */
static void hw_rx_adv_isr(uint32_t t0)
{
    uint8_t g = drv.gen, f;
    uint32_t k, w = 0;
    int b = hw_adv_find(&f);
    hw_rx_snap(0, f, 0, 0);
    if (!(CB->rxbufcntl[drv.rx_next & 1u] & 1u))
        BLE_DG(ble_dg.rx_nothing++);                               /* (the old rule's verdict, kept for comparison) */
    while (b < 0 && (w = (fm1_ticks() - t0) / FM1_TICKS_PER_US) < HW_RX_POLL_US)
        b = hw_adv_find(&f);
    if (b < 0) {
        BLE_DG(ble_dg.rxf_none++);
        hw_rx_snap(1, BDF_NONE, 0, w);
        return;
    }
    while (f >= BDF_TOG_PREV && !(CB->rxbufcntl[b] & 1u) && (fm1_ticks() - t0) / FM1_TICKS_PER_US < HW_RX_SETTLE_US)
        ;                                                  /* content only: let the engine finish the packet */
    if (w) {
        struct hw_adv_pdu o;
        BLE_DG(ble_dg.rxf_wait++);
#if BLE_DIAG
        if (w > ble_dg.rx_wait_us_max)
            ble_dg.rx_wait_us_max = w;
#endif
        hw_rx_snap(1, f, (uint8_t)(hw_adv_parse((uint32_t)b, &o) ? o.layout : 0u), w);
    }
    for (k = 0; k < 2u && b >= 0 && drv.state == HW_ADV && drv.gen == g; k++) {
        hw_rx_adv((uint32_t)b, f);
        if (drv.gen != g || drv.state != HW_ADV)
            return;
        b = hw_adv_find(&f);
    }
}

/* event ISR, advertising: a packet the RX ISR did not see (an IRQ missed, or filled after its poll) */
static void hw_rx_adv_late(void)
{
    struct hw_adv_pdu o;
    uint8_t g = drv.gen, f;
    uint32_t k;
    int b = hw_adv_find(&f);
    if (b < 0)
        return;
    hw_rx_snap(2, f, (uint8_t)(hw_adv_parse((uint32_t)b, &o) ? o.layout : 0u), 0);
    for (k = 0; k < 2u && b >= 0 && drv.state == HW_ADV && drv.gen == g; k++) {
        BLE_DG(ble_dg.rxf_late++);
        hw_rx_adv((uint32_t)b, f);
        if (drv.gen != g || drv.state != HW_ADV)
            return;
        b = hw_adv_find(&f);
    }
}

/* new packets in the RX buffers, in the engine's order (HW §8 IRQ 29 steps 2-4) */
static void hw_rx_service(void)
{
    uint8_t g = drv.gen;
    if (drv.state == HW_ADV) {
        RING_PUBLISH();
        hw_rx_adv_late();                                  /* (from the event ISR: the RX ISR takes its own) */
        return;
    }
    while (drv.state == HW_CONN && (CB->rxbufcntl[drv.rx_next] & 1u)) {
        uint32_t b = drv.rx_next;
        uint16_t dh, st;
        RING_PUBLISH();
        dh = CB->rxdhdr[b];
        st = CB->rxstat[b];
        if ((CB->rxtog & 1u) != b)
            BLE_DG(ble_dg.rxc_tog_past++);                         /* (diagnostics) RXTOG moved past the filled buffer */
        else
            BLE_DG(ble_dg.rxc_tog_at++);
        drv.rx_next ^= 1u;
        drv.rx_seen = drv.rx_any = 1;
        drv.rx_us = ble_hw_time_us();
        if (!hwd.first_rx) {                               /* (diagnostics) the first packet of this connection */
            hwd.first_rx = 1;
            BLE_DG(ble_dg.first_rx_us = (fm1_ticks() - hwd.conn_t0) / FM1_TICKS_PER_US);
            BLE_DG(ble_dg.first_rx_evt = CB->evtcount);
#if BLE_CENTRAL && BLE_DIAG
            if (drv.master) {                              /* C5: the master's first anchor reached the peripheral */
                ble_dgc.m_first_rx_us = ble_dg.first_rx_us;
                ble_dgc.m_first_rx_evt = CB->evtcount;
            }
#endif
            ble_diag_ev(BDE_FIRST_RX, st);
        }
        if ((st & 0xFu) != 1u) {
            ble_hw_stat.rx_error++;                        /* HW §8 step 4: errored, length 0 */
            BLE_DG(ble_dg.rx_crc_bad++);
            BLE_DG(ble_dg.rx_bad_stat = st);
            BLE_DG(if (ble_dg.rx_crc_bad <= 4u) ble_diag_ev(BDE_RX_BAD, st));
        } else if ((dh >> 3 & 1u) != drv.rx_sn) {
            ble_hw_stat.rx_repeat++;                       /* the central's retransmission: already delivered */
            BLE_DG(ble_dg.rx_repeat++);
        } else {
            uint8_t *pdu = &bb.rx[b].buf[HW_SWHDR - 2u];
            drv.rx_sn ^= 1u;
            ble_hw_stat.rx++;
            BLE_DG(ble_dg.rx_good++);
            if (!(dh >> 8) && (dh & 3u) == 1u)
                BLE_DG(ble_dg.rx_empty++);
            pdu[0] = (uint8_t)(dh & 0x1Fu);
            pdu[1] = (uint8_t)(dh >> 8);
            if (pdu[1] || (pdu[0] & 3u) != 1u)            /* empty PDUs are not passed on */
                ble_ll_hw_rx(pdu, (uint8_t)(pdu[1] + 2u));
            if (drv.gen != g)
                return;
        }
        CB->rxbufcntl[b] &= (uint8_t)~1u;                  /* re-armed (HW §8 step 3) */
    }
}

/* instant - 1: the new parameters into the engine (HW §9) */
static void hw_instants(uint16_t counter)
{
    if ((drv.upd & UPD_CHM) && (int16_t)(drv.chm_instant - counter) <= 1) {
        if ((int16_t)(drv.chm_instant - counter) < 1)
            ble_hw_stat.late_instant++;
        cb_channels(drv.chm);
        drv.upd &= (uint8_t)~UPD_CHM;
        ble_diag_ev(BDE_INSTANT, drv.chm_instant);
    }
    if ((drv.upd & UPD_CONN) && (int16_t)(drv.upd_instant - counter) <= 1) {
        const struct ble_hw_conn_upd *u = &drv.u;
        if ((int16_t)(drv.upd_instant - counter) < 1)
            ble_hw_stat.late_instant++;
#if BLE_CENTRAL
        if (drv.master) {                                  /* HW §21.4: the master's values, no widening; the receive
                                                            * window back to 0 / 30 two events after the instant */
            fm1_ble_col_wr(HW_LINK, 4, u->win_offset ? 0x8000u | 2u * u->win_offset : 0u);
            cb_window((uint32_t)u->win_size * 1250u + 625u);
            CB->wincntl2 = HW_WIN_NORMAL;
            fm1_ble_col_wr(HW_LINK, 2, 0x6000u);
            fm1_ble_col_wr(HW_LINK, 1, 2u * u->interval);
            fm1_ble_col_wr(HW_LINK, 15, 0x8000u | (2u * u->interval) >> 16);
            drv.interval = u->interval;
            drv.win_wide = 1;
            drv.wide_from = (uint16_t)(drv.upd_instant + 2u);
            drv.upd &= (uint8_t)~UPD_CONN;
            ble_diag_ev(BDE_INSTANT, drv.upd_instant);
            return;
        }
#endif
        cb_window((uint32_t)u->win_size * 1250u + 625u);
        CB->wincntl2 = HW_WIN_NORMAL;
        fm1_ble_col_wr(HW_LINK, 2, 7u << 12);              /* latency 0 */
        cb_widen(hw_widening(u->interval, drv.sca));
        fm1_ble_col_wr(HW_LINK, 4, u->win_offset ? 0x8000u | (2u * u->win_offset - 1u) : 0u);
        fm1_ble_col_wr(HW_LINK, 1, 2u * u->interval);
        fm1_ble_col_wr(HW_LINK, 15, 0x8000u | (2u * u->interval) >> 16);
        drv.interval = u->interval;
        drv.win_wide = 1;
        drv.wide_from = drv.upd_instant;
        drv.upd &= (uint8_t)~UPD_CONN;
        ble_diag_ev(BDE_INSTANT, drv.upd_instant);
    }
}

static void hw_event_service(void)
{
    uint16_t c3, counter;
    uint8_t rx_ok;
    if (drv.state != HW_CONN)
        return;                                            /* advertising: the engine does it all (HW §8 IRQ 45 step 2) */
    c3 = (uint16_t)fm1_ble_col_rd(HW_LINK, 3);
    if (!c3) {
        BLE_DG(ble_dg.c3_zero++);
        if (!hwd.c3_seen) {
            hwd.c3_seen = 1;
            ble_diag_ev(BDE_C3_ZERO, 0);
        }
        return;                                            /* no connection event opened yet (the advertising event
                                                            * the CONNECT_IND ended) */
    }
    counter = (uint16_t)(c3 - 1u);                         /* HW §2.3 column 3: minus 1 [M:s] */
    if (counter == drv.last_evt) {
        BLE_DG(ble_dg.evt_same++);
        return;
    }
    if (drv.last_evt == 0xFFFFu) {
        BLE_DG(ble_dg.first_evt = counter);
        ble_diag_ev(BDE_FIRST_EVT, counter);
    }
    drv.last_evt = counter;
    ble_hw_stat.events++;
    BLE_DG(ble_dg.conn_events++);
    BLE_DG(ble_dg.last_evt = counter);
    /* a reception in this event: an RX interrupt, or EVTCOUNT (also moved by a repeat the engine dropped) */
    rx_ok = (uint8_t)(drv.rx_seen || (drv.rx_any && CB->evtcount == counter &&
                                      ble_hw_time_us() - drv.rx_us < HW_RX_ALIVE_US));
    drv.rx_seen = 0;
    hw_instants(counter);
#if BLE_CENTRAL
    if (drv.master) {
#if BLE_DIAG
        ble_dgc.m_events++;
        ble_dgc.m_events_rx += rx_ok;
        if (ble_dgc.m_first_evt == 0xFFFFu)
            ble_dgc.m_first_evt = counter;
#endif
        if (drv.win_wide && rx_ok && (int16_t)(counter - drv.wide_from) >= 0 && !drv.upd) {
            cb_window(0);                                  /* HW §21.3 step 4 / §21.4: 0, and 30 us after our TX */
            CB->wincntl2 = 30u;
            fm1_ble_col_wr(HW_LINK, 4, 0);
            cb_rfprio(28u);
            drv.win_wide = 0;
        }
        ble_ll_hw_event_end(counter, rx_ok);
        return;
    }
#endif
    if (drv.win_wide && rx_ok && (int16_t)(counter - drv.wide_from) >= 0 && !drv.upd) {
        cb_window(HW_WIN_NORMAL);       /* in step: the normal window (WINCNTL2), the offset spent */
        fm1_ble_col_wr(HW_LINK, 4, 0);
        cb_rfprio(28u);
        drv.win_wide = 0;
    }
    ble_ll_hw_event_end(counter, rx_ok);
}

static void hw_isr_end(uint32_t t0)
{
    uint32_t d = fm1_ticks() - t0;
    if (d > ble_hw_stat.isr_max_ticks)
        ble_hw_stat.isr_max_ticks = d;
#if BLE_DIAG                                           /* (blell: with enc_on, what the software AES-CCM costs) */
    if (d / FM1_TICKS_PER_US > ble_dg.isr_max_us)
        ble_dg.isr_max_us = d / FM1_TICKS_PER_US;
#endif
}

void ble_wl82_rx_irq(void)              /* IRQ 29, via isr_ble_rx (hal/fm1_ble.h) */
{
    uint32_t t0 = fm1_ticks();
    uint8_t conn = drv.state == HW_CONN;  /* a connection RX interrupt: the TX service at its end (HW §8.2 point 3) */
    hwd.isr_t0 = t0;
    fm1_ble_crumb_irqs++;
    fm1_ble_rx_ack(HW_LINK);
    BLE_DG(ble_dg.rx_irqs++);
#if BLE_CENTRAL
    if (drv.state == HW_SCAN) {
        BLE_DG(ble_dgs.rx_irqs++);
        RING_PUBLISH();
        hw_rx_scan();
        hw_isr_end(t0);
        return;
    }
    if (drv.state == HW_INIT) {
        RING_PUBLISH();
        hw_rx_init();
        hw_isr_end(t0);
        return;
    }
#endif
    if (drv.state == HW_ADV) {
        RING_PUBLISH();
        hw_rx_adv_isr(t0);
        hw_isr_end(t0);
        return;
    }
    if (drv.state != HW_OFF && !(CB->rxbufcntl[drv.rx_next] & 1u)) {
        if (CB->rxbufcntl[drv.rx_next ^ 1u] & 1u) {        /* the engine filled the other buffer: we wait on this one */
            BLE_DG(ble_dg.rx_desync++);
            BLE_DG(if (ble_dg.rx_desync <= 4u) ble_diag_ev(BDE_RX_DESYNC, (uint32_t)CB->rxtog | (uint32_t)drv.rx_next << 4 | (uint32_t)drv.state << 8));
        } else
            BLE_DG(ble_dg.rx_nothing++);
    }
    hw_rx_service();
    if (conn)
        hw_tx_service();                                   /* after both RX buffers; never from the event IRQ */
    hw_isr_end(t0);
}

void ble_wl82_event_irq(void)           /* IRQ 45, via isr_ble_event */
{
    uint32_t t0 = fm1_ticks();
    hwd.isr_t0 = t0;
    fm1_ble_crumb_irqs++;
    fm1_ble_event_ack(HW_LINK);
    BLE_DG(ble_dg.evt_irqs++);
    if (drv.state == HW_ADV)
        BLE_DG(ble_dg.adv_events++);
#if BLE_CENTRAL
    if (drv.state == HW_SCAN) {
        if (fm1_ble_rx_pending(HW_LINK)) {                 /* this window's last report first */
            fm1_ble_rx_ack(HW_LINK);
            RING_PUBLISH();
            hw_rx_scan();
        }
        hw_scan_event();
        fm1_ble_event_tail(HW_LINK);
        hw_isr_end(t0);
        return;
    }
    if (drv.state == HW_INIT) {
        if (fm1_ble_rx_pending(HW_LINK)) {                 /* the target's ADV_IND first: it decides */
            fm1_ble_rx_ack(HW_LINK);
            RING_PUBLISH();
            hw_rx_init();
        }
        hw_init_event();                                   /* (the master's set-up after a hit: HW §21.3) */
        fm1_ble_event_tail(HW_LINK);
        hw_isr_end(t0);
        return;
    }
#endif
    if (fm1_ble_rx_pending(HW_LINK)) {                     /* this event's packet first: it counts for rx_ok */
        fm1_ble_rx_ack(HW_LINK);
        hw_rx_service();
    } else if (drv.state == HW_ADV)
        hw_rx_adv_late();                                  /* RAM only: CNTL, then the content (hw_adv_find) */
    else if (drv.state != HW_OFF && (CB->rxbufcntl[drv.rx_next] & 1u))
        hw_rx_service();
    hw_event_service();
    fm1_ble_event_tail(HW_LINK);
    hw_isr_end(t0);
}

/* once at boot, before the interrupts are on (midi_ble.c): the two BLE vectors only, both masked at the interrupt
 * controller until the radio starts. No BLE / RF register is touched. */
static void ble_hw_wl82_attach(void)
{
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    fm1_ble_irq_attach(BLE_HW_IRQ_PRIO);       /* (ChoralRoot: below the audio (3) and TIMER5 (4), always nested;
                                                * Melodee's dualcfg.flat option is not carried) */
#else
    fm1_ble_irq_attach(BLE_HW_IRQ_PRIO);
#endif
    fm1_ble_irqs_hold(1);
}

/* the first time BLUETOOTH is ON (at boot when ON was saved, else when the menu switches it ON; midi_ble.c), and only
 * with the stored trims (VM or Optimist's copy, ble_vm.c; HW §15.4): the radio, the baseband, the block. The two
 * interrupts stay masked: the caller lets them go. */
static void ble_hw_wl82_start(const struct ble_rf_trims *t)
{
    uint8_t *p = (uint8_t *)&bb.sw;
    uint32_t i;
    hwd.base = fm1_ticks();                                /* (blell's clock: 0 at the first BLUETOOTH ON) */
    hwd.us = 0;
    fm1_ble_step(FM1_BLE_STEP_RF_INIT);
    (void)rfs_rf_up(t->x106, t->x107, t->x108, t->x187);   /* (Bluetooth audio started the radio first: up already,
                                                         * not started again under its link; rf_share.h) */
    fm1_ble_step(FM1_BLE_STEP_BB_INIT);
    for (i = 0; i < sizeof bb.inst / 2u; i++)
        bb.inst[i] = 0;
    for (i = 0; i < sizeof bb - sizeof bb.inst; i++)
        p[i] = 0;
    rfs_le_ble((uint32_t)(uintptr_t)&bb, sizeof bb);   /* (BR/EDR's stand-in window, if any, replaced: no link in it) */
    drv.state = HW_OFF;
}
