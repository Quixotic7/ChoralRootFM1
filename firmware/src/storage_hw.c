/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware side of storage.c (FELUCCA_FLASH): flash reads in short IRQ-off windows so the
 * audio keeps up, erases and writes only inside the storage areas, the audio buffer silenced
 * first. The host tests (storage_test.c) provide their own st_* on a simulated NOR. */
static uint8_t flash_ok;                 /* JEDEC id matched at boot (persist_boot) */
/* 3y60: every flash read since power-on, calls and bytes (78 PEAK's st_reads_ps: the UI must not read per frame) */
#define ST_READS_COUNTED 1
static uint32_t st_reads, st_read_bytes;
#if BOARD_SMK37
#define ST_FEED() fm1_wdt_feed()                 /* storage.c st_smk_clear: many erases in a row */
#endif
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    st_reads++;
    st_read_bytes += n;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void audio_silence(void)                 /* IRQs off: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}
static uint32_t st_erases;                      /* erases done: each one a silence of a sector's erase time (17.8 ms
                                                 * measured, 45 by the data sheet; console `cpu`, 78 PEAK) */
/* the quiet writes' counters (78 PEAK, docs/FX.md): st_auto is set by cr_settings.c around what the unit writes BY
 * ITSELF (the poll's saves, the erase ahead); an erase then that starts while a voice is active or within 2 s of an
 * input is counted (st_near: the rule is 0; ST_NEAR_NOW is cr_ui.c's, asked here, at the erase itself). st_prog_us:
 * the longest page program (the driver's own measure of the wait for the chip, the interrupts masked: the SPI
 * transfer of the page before it is not in it) */
static uint8_t st_auto;
static uint32_t st_near, st_prog_us;
/* 3y265: erases and page programs that finished late (past 500 ms / 10 ms, done: fm1_flash_late.h), since power-on,
 * and the longest in us; any caller's (saves, the OTA staging), counted by the RAM driver itself */
#define st_late_ops fl_late_ops
#define st_late_max_us fl_late_max_us
#ifndef ST_NEAR_NOW
#define ST_NEAR_NOW() 0                           /* (choralroot.c: cr_ui.c cr_erase_near) */
#endif
#if BOARD_SMK37
static int st_stock_release(void);              /* storage.c: the stock mapping's end marker */
static int st_in_usrflash(uint32_t off);
#endif
static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
#if BOARD_SMK37
    if (!FL_STORE_OK(off, 0x1000u) && !FL_STOCKTAG_OK(off, 0x1000u))
        return -8;
    if (st_in_usrflash(off) && st_stock_release())
        return -10;                             /* before the first erase in USRFLASH: see storage.c */
#else
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
#endif
    st_erases++;
    st_holes += (uint32_t)audio_loud();
    if (st_auto && ST_NEAR_NOW())
        st_near++;
    f = irq_save();
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *p = src;
    if (!FL_STORE_OK(off, n))                   /* only inside the storage areas */
        return -8;
#if BOARD_SMK37
    if (st_in_usrflash(off) && st_stock_release())
        return -10;                             /* a sector the stock layout leaves erased is written unerased */
#endif
    while (n) {                                 /* (fm1_flash.h fl_write, with the chip's time kept) */
        uint32_t chunk = 256u - (off & 0xFFu), took = 0, f;
        int rc;
        if (chunk > n)
            chunk = n;
        f = irq_save();
        rc = FL_FAR(fl_prog_ram)(off, p, chunk, &took);
        irq_restore(f);
        if (rc)
            return rc;
        if (took > st_prog_us)
            st_prog_us = took;
        off += chunk;
        p += chunk;
        n -= chunk;
    }
    return 0;
}
