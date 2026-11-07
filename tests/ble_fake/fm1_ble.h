/* SPDX-License-Identifier: GPL-3.0-only */
/* A host stand-in for hal/fm1_ble.h, for tests/ble_driver_test.c: the WL82 driver (firmware/src/ble/ble_hw_wl82.c)
 * built against a fake engine. The column port and TIMER4 are plain variables the test drives; the column reads
 * are counted per column, and the slot clock (columns 0 / 14) runs backwards now and then, as the FM-1's did
 * (blell3, 2026-10-08). No register is real. */
#ifndef FM1_BLE_FAKE_H
#define FM1_BLE_FAKE_H
#include <stdint.h>

#define FM1_TICKS_PER_US 24u
enum { FM1_BLE_STEP_NONE, FM1_BLE_STEP_RF_INIT, FM1_BLE_STEP_BB_INIT, FM1_BLE_STEP_LINK_STOP, FM1_BLE_STEP_LINK_OPEN,
       FM1_BLE_STEP_ADV_PROG, FM1_BLE_STEP_ADV_STARTED };

static struct {
    uint32_t ticks;                     /* TIMER4 */
    uint32_t tick_per_read;             /* TIMER4 moves this much on every read (the driver's busy waits end) */
    uint32_t col[17];                   /* what was last written to each column */
    uint32_t col3;                      /* column 3 as read: event counter + 1 (0: no connection event yet) */
    uint32_t slots;                     /* the slot clock columns 0 / 14 read */
    uint32_t col_reads[17];             /* op-2 reads per column */
    uint32_t irqs, rx_pending;
    uint32_t log[64], log_n;            /* every column write, (column << 16 | data), the last 64 */
} fk;

static uint8_t fm1_rf_cal_on;                     /* (hal/fm1_ble_rf.h: measure the radio calibration at start) */
static inline void fm1_ble_sync(void) {}
static inline uint32_t fm1_ticks(void) { return fk.ticks += fk.tick_per_read; }
static inline void fm1_ble_col_wr(uint32_t link, uint32_t col, uint32_t data)
{
    (void)link;
    fk.col[col <= 16u ? col : 16u] = data & 0xFFFFu;
    fk.log[fk.log_n++ & 63u] = col << 16 | (data & 0xFFFFu);
    if (col == 0u || col == 14u)
        fk.slots = 0;                   /* the start (column 0 / 14 written) restarts the clock */
}
static inline uint32_t fm1_ble_col_rd(uint32_t link, uint32_t col)
{
    (void)link;
    fk.col_reads[col <= 16u ? col : 16u]++;
    if (col == 3u)
        return fk.col3;
    if (col == 0u)
        return fk.slots & 0xFFFFu;
    if (col == 14u)
        return 0x8000u | (fk.slots >> 16 & 0xFFu);
    return fk.col[col <= 16u ? col : 16u];
}
static inline uint32_t fm1_ble_clock(uint32_t link) { return fm1_ble_col_rd(link, 14) << 16 & 0xFF0000u | fm1_ble_col_rd(link, 0); }
static inline void fm1_ble_step(uint32_t s) { (void)s; }
static struct { uint32_t stop; } fm1_ble_bc;      /* the link-stop breadcrumb (hal/fm1_ble_rf.h: the rest unused here) */
static inline uint32_t fm1_ble_rng32(void) { static uint32_t x = 0x12345678u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
static struct { uint32_t busy_us, stops, last_max, busy_max_us; } fkb;   /* the engine busy this long after the next
                                                                           * stop; that stop's bound */
static inline uint32_t fm1_ble_link_stop(uint32_t link, uint32_t max_us)
{
    uint32_t w = fkb.busy_us < max_us ? fkb.busy_us : max_us;
    (void)link;
    fk.col[14] = 0;
    fkb.stops++;
    fkb.last_max = max_us;
    if (fkb.busy_us)
        fkb.busy_max_us = max_us;
    fkb.busy_us = 0;
    fk.ticks += w * FM1_TICKS_PER_US;
    return w;
}
static inline void fm1_ble_link_irqs_on(uint32_t link) { (void)link; }
static inline void fm1_ble_rx_ack(uint32_t link) { (void)link; }
static inline void fm1_ble_event_ack(uint32_t link) { (void)link; }
static inline int fm1_ble_rx_pending(uint32_t link) { (void)link; return (int)fk.rx_pending; }
static inline void fm1_ble_event_tail(uint32_t link) { (void)link; }
static inline void fm1_ble_irqs_hold(int hold) { (void)hold; }
static inline void fm1_ble_irq_attach(uint32_t prio) { (void)prio; }
static inline void fm1_ble_rf_init(const void *a, const void *b, const void *c, const void *d) { (void)a; (void)b; (void)c; (void)d; }
static inline void fm1_ble_bb_init(uint32_t base, uint32_t size) { (void)base; (void)size; }
static inline void fm1_ble_irq_regs(uint32_t r[5]) { r[0] = r[1] = r[2] = r[3] = r[4] = 0; }
#define fm1_ble_crumb_irqs fk.irqs
#endif
