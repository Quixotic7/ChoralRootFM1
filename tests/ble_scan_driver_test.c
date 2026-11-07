/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 driver's scanning (firmware/src/ble/ble_hw_wl82.c, BLE_CENTRAL; docs/BLE-HW-FACTS.md §21.2) with
 * the stack, the scanner's table and the fake engine of tests/ble_fake/fm1_ble.h:
 *   start     from advertising, in the vendor's order: RFPRIO 17, column 8 = 0, window / interval (and the vendor's
 *             window adjustment), WINCNTL2 50, column 15 bit15 = 0, FILTERCNTL bit3 0, FORMAT bit8 (active 0 /
 *             passive 1), column 6 = 0x2100 | 37, both TX buffers a SCAN_REQ (ScanA ours, AdvA six zeros, TXAHDR 3 |
 *             TxAdd << 4, length 12), state 1, OPTCNTL bit9 0 / bits 10-11 1, the start (column 14 = 0x8000 last)
 *   events    the channel written each event interrupt: 37 again, then 38, 39, 37 ...; RFPRIO 26 every 6th, else 17
 *   RX        a report found by RXBUFnCNTL bit0 (the vendor's connection path) or, without it, by RXTOG having moved
 *             past it (as the FM-1 advertises, §8.1); header from RXAHDR / RXDHDR, RSSI2, LASTCHMAP; RXSTAT != 1 and
 *             odd lengths dropped; a stale buffer not taken twice; the names into the DEVICES table
 *   backoff   Core Vol 6 Part B 4.4.3.2 as §21.2 has it: FORMAT bit8 0 only while the count is 1, upperLimit doubling
 *             after two failures, halving after two SCAN_RSPs
 *   rules     no column read (op 2) on the scan path; stop = column 14 = 0; advertising again (state 2) afterwards;
 *             passive scanning never arms a SCAN_REQ
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#include "../firmware/src/ble/ble_stack.c"
#include "../firmware/src/ble/ble_diag.c"
#if BLE_DIAG
#define DG(x) (x)
#else
#define DG(x) 1
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("%-118s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t) { (void)pkt, (void)t; return 0; }
static void ble_app_midi_pop(void) {}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last) { (void)pkt, (void)ts, (void)last; }
static void ble_app_state(void) {}
#if BLE_SMP_LEGACY
static void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand, (void)ediv, (void)ltk; }
static void ble_app_peer_id(const uint8_t irk[16], const uint8_t a[6], uint8_t r) { (void)irk, (void)a, (void)r; }
#endif
#if BLE_CENTRAL
static void ble_app_central_keys(const struct ble_keys *k) { (void)k; }
#endif

static const uint8_t OWN[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
static const uint8_t MIDI_UUID[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                      0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};

/* the engine stores an advertising-channel PDU: payload at RXPTRn, header in RXAHDR / RXDHDR, RXSTAT, RSSI2, LASTCHMAP;
 * cntl: also RXBUFnCNTL bit0 = 1 (the vendor's rule) or not (the FM-1's advertising one); RXTOG moves past it */
static void eng_rx(uint8_t hdr, uint8_t a0, const uint8_t *ad, uint8_t n, uint16_t stat, uint16_t rssi, uint8_t ch,
                   int cntl)
{
    uint32_t b = CB->rxtog & 1u;
    uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    p[0] = a0, p[1] = 0x11, p[2] = 0x22, p[3] = 0x33, p[4] = 0x44, p[5] = 0xC5;
    memcpy(p + 6, ad, n);
    CB->rxahdr[b] = hdr;
    CB->rxdhdr[b] = (uint16_t)((6u + n) << 8);
    CB->rxstat[b] = stat;
    CB->rssi[2] = rssi;
    CB->lastchmap = ch;
    if (cntl)
        CB->rxbufcntl[b] |= 1u;
    CB->rxtog ^= 1u;
    ble_wl82_rx_irq();
}

static uint32_t col6_writes(uint32_t *v, uint32_t max)   /* the column 6 writes logged, oldest first */
{
    uint32_t i, n = 0, from = fk.log_n > 64u ? fk.log_n - 64u : 0u;
    for (i = from; i < fk.log_n && n < max; i++)
        if ((fk.log[i & 63u] >> 16) == 6u)
            v[n++] = fk.log[i & 63u] & 0xFFFFu;
    return n;
}

static void t_scan(void)
{
    static struct ble_scan_tab tab;
    struct ble_rf_trims tr;
    uint8_t ad[31], q[40], n;
    uint16_t rssi;
    uint32_t v[16], k;
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    fk.tick_per_read = FM1_TICKS_PER_US;
    ble_hw_wl82_start(&tr);
    ble_init(OWN, 1);
    ble_enable(1);
    check("advertising first (state 2)", drv.state == HW_ADV && fk.col[2] == 0x2000u);
    memset(fk.col_reads, 0, sizeof fk.col_reads);
    fk.log_n = 0;
    ble_ll_scan(1);
    check("scan: state 1 (column 2 = 0x1000), the link started last (column 14 = 0x8000), column 8 = 0",
          drv.state == HW_SCAN && fk.col[2] == 0x1000u && fk.col[14] == 0x8000u && fk.col[8] == 0u &&
              (fk.log[(fk.log_n - 1u) & 63u] >> 16) == 14u);
    check("scan: interval 64 slots (column 1), column 15 bit15 = 0, window 60 x 625 us, WINCNTL2 50",
          fk.col[1] == 64u && fk.col[15] == 0u && CB->wincntl[0] == 37500u && CB->wincntl[1] == 0u &&
              CB->wincntl2 == 50u);
    check("scan: RFPRIO 17, FILTERCNTL bit3 0, FORMAT bit8 0 (active: SCAN_REQ allowed), OPTCNTL bit9 0, bits 10-11 1",
          CB->rfpriocntl == 17u && CB->rfpriostat == 17u && !(CB->filtercntl & 8u) && !(CB->format & 0x100u) &&
              !(CB->optcntl & 0x200u) && (CB->optcntl & 0xC00u) == 0xC00u);
    n = (uint8_t)col6_writes(v, 16);
    check("scan: column 6 = 0x2100 | 37 at the start (after the link's columns are cleared)", n >= 1u &&
          v[n - 1u] == 0x2125u && fk.col[6] == 0x2125u);
    {
        static const uint8_t zero6[6] = {0};
        int ok = 1;
        for (k = 0; k < 2u; k++)
            ok &= CB->txahdr[k] == 0x13u && CB->txdhdr[k] == 0x0C00u &&
                  !memcmp(bb.tx[k].buf + HW_SWHDR, OWN, 6) && !memcmp(bb.tx[k].buf + HW_SWHDR + 6, zero6, 6);
        check("scan: both TX buffers a SCAN_REQ: TXAHDR 3 | TxAdd 1 << 4, length 12, ScanA ours, AdvA zeros (C1: the "
              "engine's)", ok);
    }
    fk.log_n = 0;
    for (k = 0; k < 7u; k++) {
        ble_wl82_event_irq();
        if (k == 4u)
            check("scan: RFPRIO 17 on events 1-5", CB->rfpriocntl == 17u);
        if (k == 5u)
            check("scan: RFPRIO 26 on the 6th event", CB->rfpriocntl == 26u);
    }
    n = (uint8_t)col6_writes(v, 16);
    check("scan: the event interrupt writes the next window's channel: 37 again, 38, 39, 37, 38, 39, 37",
          n == 7u && v[0] == 0x2125u && v[1] == 0x2126u && v[2] == 0x2127u && v[3] == 0x2125u && v[6] == 0x2125u);
    /* RX: a BLE-MIDI controller's ADV_IND (the UUID), then its SCAN_RSP (the name) */
    ad[0] = 2, ad[1] = 0x01, ad[2] = 0x06, ad[3] = 17, ad[4] = 0x07;
    memcpy(ad + 5, MIDI_UUID, 16);
    eng_rx(0x40, 0x01, ad, 21, 1, 0x0A12, 37, 1);
    check("RX: the ADV_IND found by RXBUFnCNTL bit0 (the vendor's rule), bit0 cleared after",
          DG(ble_dgs.rxf_cntl == 1u && ble_dgs.rxf_tog == 0u) && !(CB->rxbufcntl[0] & 1u) && !(CB->rxbufcntl[1] & 1u));
    n = ble_ll_scan_take(q, &rssi);
    check("RX: handed over whole: header 0x40, length 27, AdvA, the AD; RSSI2 0x0A12",
          n == 29u && q[0] == 0x40u && q[1] == 27u && q[2] == 0x01u && q[7] == 0xC5u && !memcmp(q + 13, MIDI_UUID, 16) &&
              rssi == 0x0A12u);
    ble_scan_clear(&tab);
    ble_scan_add(&tab, q, n, rssi, 100);
    ad[0] = 11, ad[1] = 0x09;
    memcpy(ad + 2, "KeyStep 37", 10);
    eng_rx(0x44, 0x01, ad, 12, 1, 0x0A13, 38, 0);
    check("RX: the SCAN_RSP found by RXTOG having moved past it (no RXBUFnCNTL bit0: the FM-1's advertising rule)",
          DG(ble_dgs.rxf_tog == 1u) && DG(ble_dgs.rsp_ok == 1u));
    n = ble_ll_scan_take(q, &rssi);
    ble_scan_add(&tab, q, n, rssi, 110);
    {
        uint8_t idx[BLE_SCAN_N];
        check("RX -> the DEVICES table: KeyStep 37 listed", ble_scan_list(&tab, idx) == 1u &&
              !strcmp(tab.e[idx[0]].name, "KeyStep 37") && tab.e[idx[0]].rssi == 0x0A13u);
    }
    ble_wl82_rx_irq();
    check("RX: an interrupt with nothing new: nothing taken twice (counted)", DG(ble_dgs.rxf_none == 1u) &&
          ble_ll_scan_take(q, &rssi) == 0u);
    eng_rx(0x40, 0x02, ad, 4, 0x9805, 0, 39, 1);
    check("RX: RXSTAT [3:0] != 1: dropped (counted)", DG(ble_dgs.rx_bad_stat == 1u) && ble_ll_scan_take(q, &rssi) == 0u);
    eng_rx(0x43, 0x02, ad, 6, 1, 0, 39, 1);
    check("RX: a SCAN_REQ (type 3) heard: dropped in state 1", ble_ll_scan_take(q, &rssi) == 0u);
    eng_rx(0x40, 0x02, ad, 0, 1, 0, 39, 1);
    {
        uint32_t b = CB->rxtog & 1u;               /* a length past an advertising PDU */
        CB->rxahdr[b] = 0x40;
        CB->rxdhdr[b] = (uint16_t)(40u << 8);
        CB->rxstat[b] = 1;
        CB->rxbufcntl[b] |= 1u;
        CB->rxtog ^= 1u;
        ble_wl82_rx_irq();
    }
    n = ble_ll_scan_take(q, &rssi);
    check("RX: an ADV_IND with no AD taken; a length of 40 dropped (counted)", n == 8u && DG(ble_dgs.rx_bad_len == 1u) &&
          ble_ll_scan_take(q, &rssi) == 0u);
    check("scan: no column read (op 2) anywhere on the scan path (start, events, RX)",
          (fk.col_reads[0] + fk.col_reads[1] + fk.col_reads[2] + fk.col_reads[3] + fk.col_reads[6] +
                   fk.col_reads[14] + fk.col_reads[15]) == 0u);
    /* backoff */
    hw_scan_backoff_reset();
    CB->format = 0;
    for (k = 0; k < 6u; k++)
        eng_rx(0x40, (uint8_t)(0x10 + k), ad, 0, 1, 0, 37, 1);   /* ADV_INDs, never a SCAN_RSP */
    while (ble_ll_scan_take(q, &rssi))
        ;
    check("backoff: SCAN_REQs with no SCAN_RSP: upperLimit doubles after two failures in a row",
          drv.bo_upper >= 2u && DG(ble_dgs.req_fail >= 2u) && DG(ble_dgs.upper_max >= 2u));
    check("backoff: FORMAT bit8 is 0 exactly when the count is 1 (the next ADV_IND gets a SCAN_REQ)",
          ((CB->format & 0x100u) != 0) == (drv.bo_count != 1u));
    {
        uint16_t up = drv.bo_upper;
        int guard = 0;
        for (k = 0; k < 2u && guard < 600; guard++) {
            eng_rx(0x40, 0x20, ad, 0, 1, 0, 37, 1);
            if (drv.bo_sent) {                     /* (a SCAN_REQ went out with that ADV_IND: its SCAN_RSP) */
                eng_rx(0x44, 0x20, ad, 0, 1, 0, 37, 1);
                k++;
            }
            while (ble_ll_scan_take(q, &rssi))
                ;
        }
        check("backoff: two SCAN_RSPs in a row halve upperLimit", k == 2u && drv.bo_upper == (up > 1u ? up / 2u : 1u));
    }
    /* stop: advertising again */
    fk.log_n = 0;
    ble_ll_scan(0);
    check("stop: the link stopped, then advertising set up again (state 2, column 14 = 0x8000)",
          drv.state == HW_ADV && fk.col[2] == 0x2000u && fk.col[14] == 0x8000u && DG(ble_dgs.stops == 1u));
    eng_rx(0x40, 0x30, ad, 0, 1, 0, 37, 1);
    check("stop: a report after it is not a scan report", ble_ll_scan_take(q, &rssi) == 0u);
    ble_enable(0);
}

static void t_passive(void)
{
    struct ble_hw_scan s;
    uint8_t ad[4] = {0};
    uint32_t k;
    s.interval = 6;                                /* the vendor's adjustment: window 4 > interval - 4 -> 2 */
    s.window = 4;
    s.active = 0;
    s.own = OWN;
    s.own_rand = 1;
    ble_hw_scan_start(&s);
    check("passive: FORMAT bit8 1 (no SCAN_REQ); window cut to interval - 4 (2 x 625 us)", (CB->format & 0x100u) &&
          CB->wincntl[0] == 1250u && fk.col[1] == 6u);
    for (k = 0; k < 4u; k++)
        eng_rx(0x40, 0x01, ad, 0, 1, 0, 37, 1);
    check("passive: ADV_INDs never arm a SCAN_REQ", (CB->format & 0x100u) && DG(ble_dgs.req_armed == 0u));
    ble_hw_scan_stop();
    s.interval = 4;
    s.window = 4;
    ble_hw_scan_start(&s);
    check("an interval of 4 or less: interval 5, window 1 (the vendor's rule)", fk.col[1] == 5u && CB->wincntl[0] == 625u);
    ble_hw_scan_stop();
    check("stopped: state off", drv.state == HW_OFF);
}

int main(void)
{
    t_scan();
#if BLE_DIAG
    ble_diag_clear();
#endif
    t_passive();
    printf("%s\n", fails ? "BLE scan driver: FAILED" : "BLE scan driver: all passed");
    return fails != 0;
}
