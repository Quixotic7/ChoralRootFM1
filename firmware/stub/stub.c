/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); the register sequences are Felucca's
 * (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments), copied from firmware/hal */
/* The boot stub, device side (docs/BOOT-SAFETY.md "As built"; the decision is stub_core.h). The SPL enters
 * crt0_stub.S on every boot; stub_main returns to it for "boot the app" and never returns otherwise.
 *
 * Self-contained on purpose: every register access below is a copy of code that runs on the unit in the app or the
 * loader (the source function is named at each), not an include of firmware/hal, so a change in the HAL cannot change
 * the stub's bytes: sector 0x4000 stays identical from build to build and the loader never rewrites it
 * (tools/build.py compares the stub with firmware/stub/stub.lock). No .data, no .bss, no libcalls, no division; the
 * stacks are the app's (app.ld _ustack_top / _sstack_top); interrupts stay off; the second core is never started.
 *
 * RAM the stub owns: 0x01C7C000..0x01C7C01F (app.ld keeps it out of NOINIT; hal/fm1_sys.h fm1_stub_word reads it):
 *   +0  the rescue word (stub_core.h STUB_RSC_MAGIC | attempts): must survive a core reset, the SPL and the loader;
 *       or a request the app wrote before a restart (STUB_REQ_TEST / STUB_REQ_RESCUE, hal/fm1_sys.h fm1_stub_request)
 *   +4  info: "SB", the stub's version, its decision        +8  TIMER4 ticks the stub took (24 per us)
 *   +12 detail: marker code, loader code << 8, rescue attempts found << 16, bit 24 rescue key, 25 / 26 a request
 *   +16 the 32 key samples with OCT-'s column latched (bit i = sample i, 1 = PA8 low)   +20 with OCT+'s column
 *   +24 with no column latched   +28 the whole PA input register, OCT-'s column latched, at the last sample
 * The panel rail: PA2 is pulled low before the keys are sampled (shw_rail: the matrix and the LEDs hang on it) and
 * stays low into the app, which sets it low again in lcd_init; before a hand-over to the loader or the ROM it is
 * driven high again, the state the SPL leaves it in at power-on.
 * Not read: the reset-reason registers (P3_RST_SRC, RST_SRC: the app's boot guard reads them after us, untouched),
 * the settings, any flash data, the app's .noinit. */
#include <stdint.h>

#define R32(a) (*(volatile uint32_t *)(a))
#define SHW_TICKS_PER_US 24u

/* ------------------------------------------------------------------ P33, watchdog (hal/fm1_sys.h) --- */
#define SHW_P33_CON R32(0x13E08u)
#define SHW_P33_DAT R32(0x13E0Cu)

static uint8_t shw_p33_xfer(uint8_t b)               /* fm1__p33_xfer, without its timeout counter (.bss) */
{
    uint32_t n = 100000u;
    SHW_P33_DAT = b;
    SHW_P33_CON |= 1u << 4;
    (void)SHW_P33_CON;                               /* stands in for csync */
    while (SHW_P33_CON & (1u << 1))
        if (--n == 0)
            break;
    return (uint8_t)SHW_P33_DAT;
}

static void shw_p33_op(uint32_t op, uint32_t a, uint8_t d)   /* fm1__p33_cs + fm1__p33_op: 0 wr, 1 or, 2 and */
{
    SHW_P33_CON &= ~(1u << 8);                       /* (never the RTC domain: a < 0x8000) */
    SHW_P33_CON |= 1u << 0;
    shw_p33_xfer((uint8_t)((op << 5) | ((a >> 8) & 3u)));
    shw_p33_xfer((uint8_t)a);
    shw_p33_xfer(d);
    SHW_P33_CON &= ~((1u << 0) | (1u << 8));
}

static void shw_wdt_arm(void)                        /* fm1_wdt_arm(0x0D): ~8 s, plain reset; main.c fm1_cstart */
{
    shw_p33_op(0, 0x80u, 0);                         /* P3_WDT_CON */
    shw_p33_op(2, 0x17u, (uint8_t)~0x40u);           /* P3_VLD_KEEP: WDT_EXPT_EN = 0 */
    shw_p33_op(0, 0x80u, 0x0Du | 0x40u);             /* timeout, reset mode, clear */
    shw_p33_op(1, 0x80u, 0x10);                      /* enable */
}

/* ------------------------------------------------------------------ TIMER4 (hal/fm1_time.h) --- */
#define SHW_T4_CON R32(0x10800u)
#define SHW_T4_CNT R32(0x10804u)
#define SHW_T4_PRD R32(0x10808u)

static void shw_time_init(void)                      /* fm1_time_init */
{
    if ((SHW_T4_CON & 0xFu) == ((2u << 2) | 1u))
        return;
    SHW_T4_CON = 0;
    SHW_T4_PRD = 0xFFFFFFFFu;
    SHW_T4_CNT = 0;
    SHW_T4_CON = (2u << 2) | 1u | (1u << 14);
}

static void shw_delay_us(uint32_t us)                /* fm1_delay_us */
{
    uint32_t start = SHW_T4_CNT, span = us * SHW_TICKS_PER_US;
    while ((uint32_t)(SHW_T4_CNT - start) < span)
        ;
}

/* ------------------------------------------------------------------ the panel rail (hal/fm1_lcd_hw.h) --- */
/* PA2, the LCD's "backlight" line, active low. Found on the unit (2026-10-10): it enables a rail that also feeds
 * the two 595s and the LEDs. With it high (as the SPL leaves it: stub versions 1 and 2 read PA2 high at boot, and
 * the app's screen-off, which drives it high, left the unit without keys until it was low again) no key reads
 * closed and no LED lights. on: fm1_lcd_hw_init's first two lines (low, an output), which the app runs before its
 * first key scan; off: fm1_lcd_backlight(0), the app's screen-off */
#if BOARD_SMK37
/* SMK-37: no such rail (the keyboard's matrix and LEDs are always powered) and PA2 is a matrix row there: never an
 * output. Nothing to do */
static void shw_rail(uint32_t on) { (void)on; }
#else
static void shw_rail(uint32_t on)
{
    if (on) {
        R32(0x50000u) &= ~(1u << 2);                 /* FM1_LCD_PA_OUT &= ~FM1_LCD_BL */
        R32(0x50008u) &= ~(1u << 2);                 /* FM1_LCD_PA_DIR &= ~FM1_LCD_BL */
    } else {
        R32(0x50000u) |= 1u << 2;                    /* FM1_LCD_PA_OUT |= FM1_LCD_BL */
    }
}
#endif

/* ------------------------------------------------------------------ key matrix, LEDs (hal/fm1_input.h) --- */
#define SHW_PR(p, r) R32(0x50000u + (p) * 0x40u + (r))   /* hal/fm1_gpio.h FM1_PR */
enum { SHW_PA = 0, SHW_PB = 1, SHW_PH = 7 };
enum { SHW_OUT = 0x00, SHW_IN = 0x04, SHW_DIR = 0x08, SHW_DIE = 0x0C, SHW_PU = 0x10, SHW_PD = 0x14, SHW_HD0 = 0x18,
       SHW_HD = 0x1C };

#if BOARD_SMK37
/* SMK-37 (hal/smk_input.h, the keyboard's own HAL): four 595s (32 bits) on PA3 (clock) / PA4 (data), latched by PA1,
 * PA0 their output enable (low); the app shifts them with SPI2 (group C, IOMAP_CON1 bits 17..16), the stub by hand,
 * the same bits in the same order (the DMA's four bytes from memory, little-endian, each msb first). Word bits
 * 31..24: columns 0..7 (active low, one-hot); bits 15..0: the button LED lines (active low). OCTAVE- is button 4 at
 * column 6, OCTAVE+ button 5 at column 3, both on row 6 = PH8 (input, pull-up, low = closed: smk_input.h SMK_BTNC,
 * smk__rows). The only pins touched: PA0, PA1, PA3, PA4 (outputs, as the app's) and PH8 (an input, as the app's);
 * never PA2, PA5..PA9, PH6, PH9 (rows: inputs, left as they are) */
#define SHW_IOMAP_CON1 R32(0x51020u)
#define SHW_USBIO_CON R32(0x16A0Cu)                      /* the button LEDs' two commons (smk_input.h smk__common) */
#define SHW_COLW(c) (~(0x01000000u << (c)))             /* smk_input.h SMK_COLW, columns 0..7 */
enum { SHW_COL_OCTDN = 6, SHW_COL_OCTUP = 3, SHW_ROW_PH = 8, SHW_LED_OCTDN = 13 };   /* (LED group 0, bit 13) */

static void shw_sr_word(uint32_t w)                  /* smk_input.h smk__shift (by hand) + smk__latch */
{
    uint32_t i;
    for (i = 0; i < 32u; i++) {
        if ((w >> ((i & ~7u) + 7u - (i & 7u))) & 1u)  /* byte i / 8 of the word, its bit 7 first */
            SHW_PR(SHW_PA, SHW_OUT) |= 1u << 4;
        else
            SHW_PR(SHW_PA, SHW_OUT) &= ~(1u << 4);
        SHW_PR(SHW_PA, SHW_OUT) |= 1u << 3;
        SHW_PR(SHW_PA, SHW_OUT) &= ~(1u << 3);
    }
    SHW_PR(SHW_PA, SHW_OUT) |= 1u << 1;
    SHW_PR(SHW_PA, SHW_OUT) &= ~(1u << 1);
}

static void shw_common(uint32_t grp_on)              /* smk__common: 0 / 1 that group's common low, 2 both off */
{
    uint32_t v = SHW_USBIO_CON | 0x1000Fu | (1u << 17) | (1u << 18);
    if (grp_on < 2u)
        v &= ~(1u << (17u + grp_on));
    SHW_USBIO_CON = v;
}

static void shw_input_init(void)                     /* fm1_input_init's pins (smk_input.h; the app repeats it later) */
{
    const uint32_t out = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4), row = 1u << SHW_ROW_PH;
    SHW_IOMAP_CON1 &= ~0x30000u;                     /* PA3 / PA4 as GPIO (a warm reset keeps the app's SPI2) */
    SHW_PR(SHW_PA, SHW_DIE) |= out;                  /* smk__pin_out: PA0 /OE, PA1 latch, PA3, PA4 low */
    SHW_PR(SHW_PA, SHW_PU) &= ~out;
    SHW_PR(SHW_PA, SHW_PD) &= ~out;
    SHW_PR(SHW_PA, SHW_OUT) &= ~out;
    SHW_PR(SHW_PA, SHW_DIR) &= ~out;
    SHW_PR(SHW_PH, SHW_DIE) |= row;                  /* smk__pin_in_pu: PH8 */
    SHW_PR(SHW_PH, SHW_DIR) |= row;
    SHW_PR(SHW_PH, SHW_PD) &= ~row;
    SHW_PR(SHW_PH, SHW_PU) |= row;
    shw_common(2);
    shw_sr_word(0xFFFFFFFFu);
}

/* one sample: bit0 OCTAVE- (column 6, row PH8), bit1 OCTAVE+ (column 3, row PH8), bit2 the same row with no column
 * latched, 1 = the row low; 100 us to settle as on the FM-1. *pa: the PH inputs as read for bit0 */
static uint32_t shw_keys(uint32_t *pa)
{
    static const uint8_t COL[2] = {SHW_COL_OCTDN, SHW_COL_OCTUP};
    uint32_t p, k = 0, in;
    for (p = 0; p < 3u; p++) {
        shw_sr_word(p < 2u ? SHW_COLW(COL[p]) : 0xFFFFFFFFu);
        shw_delay_us(100);
        in = SHW_PR(SHW_PH, SHW_IN);
        if (!p)
            *pa = in;
        k |= ((~in >> SHW_ROW_PH) & 1u) << p;
    }
    shw_sr_word(0xFFFFFFFFu);
    return k;
}

/* OCTAVE-'s LED, n times on for ms (rescue only) */
static void shw_blink(uint32_t n, uint32_t ms)
{
    while (n--) {
        shw_sr_word(0xFFFFFFFFu & ~(1u << SHW_LED_OCTDN));
        shw_common(0);
        shw_delay_us(ms * 1000u);
        shw_common(2);
        shw_sr_word(0xFFFFFFFFu);
        shw_delay_us(120000u);
    }
}
#else
static void shw_led_lines(uint32_t rowmask)          /* fm1__led_lines: row bit 1 PA9, 2 PA10, 3 PH6, 4 PH9 */
{
    uint32_t a = SHW_PR(SHW_PA, SHW_OUT) & ~((1u << 9) | (1u << 10));
    uint32_t h = SHW_PR(SHW_PH, SHW_OUT) & ~((1u << 6) | (1u << 9));
    SHW_PR(SHW_PA, SHW_OUT) = a | (rowmask & 2u) << 8 | (rowmask & 4u) << 8;
    SHW_PR(SHW_PH, SHW_OUT) = h | (rowmask & 8u) << 3 | (rowmask & 16u) << 5;
}

static void shw_sr_word(uint32_t w)                  /* fm1__sr_bits(w, 0, 16) + fm1__sr_latch: the 595 chain */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {                      /* read-modify-write per edge, as there (the 595's timing) */
        if (w & (0x8000u >> i))
            SHW_PR(SHW_PA, SHW_OUT) |= 1u << 4;
        else
            SHW_PR(SHW_PA, SHW_OUT) &= ~(1u << 4);
        SHW_PR(SHW_PA, SHW_OUT) |= 1u << 3;
        SHW_PR(SHW_PA, SHW_OUT) &= ~(1u << 3);
    }
    SHW_PR(SHW_PA, SHW_OUT) |= 1u << 1;
    SHW_PR(SHW_PA, SHW_OUT) &= ~(1u << 1);
}

static void shw_input_init(void)                     /* fm1_input_init's pins (the app repeats it later) */
{
    static const uint8_t LEDP[4][2] = {{SHW_PH, 6}, {SHW_PH, 9}, {SHW_PA, 9}, {SHW_PA, 10}};
    const uint32_t rows_a = (1u << 0) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    const uint32_t sr = (1u << 1) | (1u << 3) | (1u << 4), row_b = 1u << 7;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t p = LEDP[i][0], m = 1u << LEDP[i][1];
        SHW_PR(p, SHW_DIE) |= m;
        SHW_PR(p, SHW_OUT) &= ~m;
        SHW_PR(p, SHW_DIR) &= ~m;
        SHW_PR(p, SHW_HD0) |= m;
        SHW_PR(p, SHW_HD) |= m;
    }
    SHW_PR(SHW_PA, SHW_DIE) |= rows_a;
    SHW_PR(SHW_PA, SHW_DIR) |= rows_a;
    SHW_PR(SHW_PA, SHW_PD) &= ~rows_a;
    SHW_PR(SHW_PA, SHW_PU) |= rows_a;
    SHW_PR(SHW_PB, SHW_DIE) |= row_b;
    SHW_PR(SHW_PB, SHW_DIR) |= row_b;
    SHW_PR(SHW_PB, SHW_PD) &= ~row_b;
    SHW_PR(SHW_PB, SHW_PU) |= row_b;
    SHW_PR(SHW_PA, SHW_DIE) |= sr;
    SHW_PR(SHW_PA, SHW_PU) &= ~sr;
    SHW_PR(SHW_PA, SHW_PD) &= ~sr;
    SHW_PR(SHW_PA, SHW_OUT) &= ~sr;
    SHW_PR(SHW_PA, SHW_DIR) &= ~sr;
    shw_sr_word(0xFFFFu);
}

/* one sample: bit0 OCT- (key 0: column 0, row PA8), bit1 OCT+ (key 1: column 1, row PA8), bit2 the same row with
 * no column latched, 1 = the row low. The column word is fm1_input_scan's, the row read fm1__rows' PA8 bit; the
 * rows settle 100 us (the TIMER5 scan's tick, the pace the unit runs at). *pa: the PA inputs as read for bit0 */
static uint32_t shw_keys(uint32_t *pa)
{
    uint32_t p, k = 0, in;
    for (p = 0; p < 3u; p++) {
        shw_sr_word(p < 2u ? 0xFFFFu ^ (1u << p) ^ (1u << (11u + p)) : 0xFFFFu);
        shw_delay_us(100);
        in = SHW_PR(SHW_PA, SHW_IN);
        if (!p)
            *pa = in;
        k |= ((~in >> 8) & 1u) << p;
    }
    return k;
}

/* the LEDs of the first key column, n times on for ms (rescue only; the scan's LED drive, one column latched) */
static void shw_blink(uint32_t n, uint32_t ms)
{
    shw_sr_word(0xFFFFu ^ 1u ^ (1u << 11));
    while (n--) {
        shw_led_lines(0x1Eu);
        shw_delay_us(ms * 1000u);
        shw_led_lines(0);
        shw_delay_us(120000u);
    }
    shw_sr_word(0xFFFFu);
}

#endif /* BOARD_SMK37 */

/* ------------------------------------------------------------------ the plain XIP window (hal/fm1_flash.h) --- */
#define SHW_SFCENC_CON (*(volatile uint8_t *)0x40300u)
#define SHW_SFCENC_UNENC_H R32(0x40308u)
#define SHW_SFCENC_UNENC_L R32(0x4030Cu)
#define SHW_CACHE_CON R32(0x1EEE008u)
#define SHW_XIP(off) (0x02000000u + (uint32_t)(off) - 0x4000u)   /* hal/fm1_xip.h FM1_XIP */
#if BOARD_SMK37
#define SHW_PLAIN_LO 0x9D000u                        /* hal/fm1_flash_map.h FL_PLAIN_LO (stub_core.h STUB_PLAIN_LO) */
#else
#define SHW_PLAIN_LO 0x93000u
#endif

/* fl_plain_window_init with its fl_inval: flash from 0x93000 (SMK: 0x9D000) reads undecrypted (the staged loader is
 * stored plain). Interrupts are off already and stay off (no cli / sti here) */
static void shw_plain_window(void)
{
    uint32_t a = SHW_XIP(SHW_PLAIN_LO) & ~31u, e = SHW_XIP(SHW_PLAIN_LO) + (0x100000u - SHW_PLAIN_LO);
    SHW_SFCENC_CON &= (uint8_t)~2u;
    SHW_SFCENC_UNENC_L = SHW_XIP(SHW_PLAIN_LO);
    SHW_SFCENC_UNENC_H = 0x07FFFFFFu;
    SHW_SFCENC_CON |= 2u;
    while (!(SHW_CACHE_CON & 0x4000u))
        ;
    for (; a < e; a += 32u) {
        __asm__ volatile("csync" ::: "memory");
        __asm__ volatile("flushinv [%0]" :: "r"(a) : "memory");
        __asm__ volatile("csync" ::: "memory");
        while (!(SHW_CACHE_CON & 0x4000u))
            ;
    }
    __asm__ volatile("csync" ::: "memory");
}

/* ------------------------------------------------------------------ the ways out (hal/fm1_sys.h) --- */
static void shw_limits_off(void)                     /* fm1_enter_uboot / fm1_enter_update: CPU0 write limits */
{
    if (!(R32(0x1EEE240u) & 1u))
        R32(0x1EEE240u) = 0xE7u;
    R32(0x1EEE348u) = 0;
}

static void shw_c1_hold(void)                        /* the same two writes before every reset there */
{
    R32(0x1EEE004u) |= 0x2u;
    R32(0x1EEE004u) &= ~0x8u;
}

static void shw_enter_uboot(void)                    /* fm1_enter_uboot: the ROM's "WL82 UBOOT1.00" */
{
    static const char k[16] = "usb_update_mode";
    uint32_t i;
    shw_c1_hold();
    shw_limits_off();
    for (i = 0; i < 16u; i++)
        ((volatile uint8_t *)0x01C7FD80u)[i] = (uint8_t)k[i];
    shw_p33_op(1, 0xA0u, 0x10);                      /* P3_PR_PWR bit 4: chip reset */
    for (;;)
        ;
}

static void shw_enter_update(const uint8_t *parm)    /* fm1_enter_update + fm1_core_reset: the SPL runs the loader */
{
    uint32_t i;
    shw_limits_off();
    for (i = 0; i < 112u; i++)
        ((volatile uint8_t *)0x01C7FD88u)[i] = parm[i];
    shw_c1_hold();
    R32(0x10000u) |= 0x10u;                          /* PWR_CON: core reset */
    for (;;)
        ;
}

/* ------------------------------------------------------------------ the decision --- */
#define STUB_PLAIN_WINDOW() shw_plain_window()
#include "stub_core.h"

#define STUB_RAM ((volatile uint32_t *)0x01C7C000u)

static uint32_t stub_key(void)                       /* stub_core.h stub_key_held: 32 samples, 48 ms, every boot */
{
    uint32_t n, dn = 0, up = 0, idle = 0, pa = 0;
    for (n = 0; n < STUB_KEY_SAMPLES; n++) {
        uint32_t k = shw_keys(&pa);
        dn |= (k & 1u) << n;
        up |= ((k >> 1) & 1u) << n;
        idle |= (k >> 2) << n;
        shw_delay_us(1150);                          /* (a sample: three shifts and settles, ~0.35 ms) */
    }
    STUB_RAM[4] = dn;
    STUB_RAM[5] = up;
    STUB_RAM[6] = idle;
    STUB_RAM[7] = pa;
    return stub_key_held(dn, up, idle);
}

void stub_main(void)
{
    stub_crc_t crc;                                  /* 4 KiB of the app's main stack */
    stub_view_t v;
    stub_result_t r;
    uint8_t parm[STUB_PARM_LEN];
    uint32_t t0, key;
    shw_wdt_arm();                                   /* first, as fm1_cstart: a hang below resets in 8 s */
    shw_time_init();
    t0 = SHW_T4_CNT;
    shw_rail(1);                                     /* first: the key window's first 30 ms are its time to come up */
    shw_input_init();
    key = stub_key();
    v.mark = (const uint8_t *)SHW_XIP(STUB_MARK_OFF);
    v.body = (const uint32_t *)SHW_XIP(STUB_BODY_OFF);
    v.ldr = (const uint8_t *)SHW_XIP(STUB_LDR_OFF);
    stub_decide(&v, &crc, key, STUB_RAM[0], &r);
    STUB_RAM[0] = r.word;
    STUB_RAM[1] = stub_info(r.decision);
    STUB_RAM[2] = SHW_T4_CNT - t0;
    STUB_RAM[3] = stub_detail(&r, key);
    if (r.decision == STUB_BOOT)
        return;                                      /* crt0_stub.S: the app's entry */
    if (r.decision == STUB_LOADER) {
        shw_blink(3, 80);                            /* three short: update mode follows (ota-FM-1 on USB) */
        shw_rail(0);                                 /* (the screen dark through update mode, as from a cold boot) */
        stub_record(parm);
        shw_enter_update(parm);
    }
    shw_blink(1, 600);                               /* one long: the ROM's USB boot follows (the WL82 disk) */
    shw_rail(0);
    shw_enter_uboot();
}
