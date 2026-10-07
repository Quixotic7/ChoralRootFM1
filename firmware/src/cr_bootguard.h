/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The boot guard (main.c fm1_cstart): a pure decision on a .noinit record, so the host can test it
 * (tests/cr_bootguard_test.c) and the emulator can start in any state (tools/emu/emu_fw.c --boot-fail / --reset-reason).
 *
 * The reset reason (hal/fm1_sys.h fm1_reset_reason, read first thing at every reset):
 *   P3_RST_SRC (P33 0x12): bit0 power-on, 1 VDDIO low, 2 WDT, 3 VCM, 4 long press, 5 1.2 V low, 6 soft (P33:
 *     fm1_reboot / fm1_enter_uboot, P3_PR_PWR bit 4; fm1_fault's reboot after a crash screen is one)
 *   RST_SRC (0x100C0): bit5 soft (PWR_CON: fm1_core_reset, the M-UPGRADE commit fm1_enter_update)
 * bootguard_reason packs them in one word: P3_RST_SRC in bits 0..7, RST_SRC bit5 as BG_RST_CORE (bit 8).
 *
 * Classes, the first that applies: WDT (bit 2: a hang, or a crash whose screen hung) and SOFT (bit 6 or the core
 * reset: a crash's reboot, or an intentional reboot) are CRASH-type; power-on (bit 0) clears the guard; anything else
 * (brown-outs, VCM, the long press, no bit) is OTHER: never counted, never clears.
 * A crash-type reset counts as a failed boot only while `pending` is set, i.e. the previous boot neither ran 30 s
 * (bootguard_settled) nor announced an intentional reset (pending = 0 before fm1_reboot / UBOOT / the update commit).
 *
 *   failed 0..1 -> BOOT_NORMAL;  failed 2..3 -> BOOT_SAFE (no flash object loads, main.c);  failed 4 -> BOOT_UBOOT
 *   (the guard cleared first: the ROM's USB boot "WL82 UBOOT1.00", left with a power cycle)
 * A power-on always gives BOOT_NORMAL with the guard clear. 30 s of uptime in any mode clears `failed` and `pending`
 * (a safe-mode session stays safe until the next reset: `mode` keeps BOOT_SAFE). */
#ifndef CR_BOOTGUARD_H
#define CR_BOOTGUARD_H
#include <stdint.h>

#define BOOTGUARD_MAGIC 0x42475233u      /* "BGR3": the record below (Felucca 1.0's 3-word one used 0x42475244) */
#define BG_SAFE_AT 2u                    /* failed boots in a row -> SAFE MODE */
#define BG_UBOOT_AT 4u                   /* .. -> ROM boot (SAFE MODE itself failed twice) */
#define BG_SETTLE_MS 30000u              /* uptime after which a boot is good */

#define BG_P3_POWERON 0x01u
#define BG_P3_WDT 0x04u
#define BG_P3_SOFT 0x40u
#define BG_RST_CORE 0x100u               /* RST_SRC bit5 (PWR_CON soft reset), moved to bit 8 of the reason word */

enum { BOOT_NORMAL, BOOT_SAFE, BOOT_UBOOT };
enum { BG_RST_OTHER, BG_RST_POWERON, BG_RST_WDT, BG_RST_SOFT };

struct bootguard {
    uint32_t magic;
    uint32_t failed;                     /* crash-type resets in a row, each within 30 s of its boot */
    uint32_t pending;                    /* 1 from the boot until 30 s of uptime or an intentional reset */
    uint32_t mode;                       /* this boot's BOOT_* */
    uint32_t reason;                     /* this boot's reset reason word (bootguard_reason) */
    uint32_t cls;                        /* .. its BG_RST_* */
    uint32_t counted;                    /* 1: this boot followed a failed one (the reset counted) */
};

static inline uint32_t bootguard_reason(uint32_t p3_rst, uint32_t rst_src)
{
    return (p3_rst & 0xFFu) | ((rst_src & 0x20u) ? BG_RST_CORE : 0u);
}

static inline uint32_t bootguard_class(uint32_t reason)
{
    if (reason & BG_P3_WDT)
        return BG_RST_WDT;
    if (reason & (BG_P3_SOFT | BG_RST_CORE))
        return BG_RST_SOFT;
    if (reason & BG_P3_POWERON)
        return BG_RST_POWERON;
    return BG_RST_OTHER;
}

static inline void bootguard_clear(struct bootguard *g)
{
    g->magic = BOOTGUARD_MAGIC;
    g->failed = g->pending = g->counted = 0;
    g->mode = BOOT_NORMAL;
    g->reason = 0;
    g->cls = BG_RST_OTHER;
}

/* at every reset, before anything else runs: this boot's mode */
static inline uint32_t bootguard_step(struct bootguard *g, uint32_t reason)
{
    uint32_t c = bootguard_class(reason);
    if (g->magic != BOOTGUARD_MAGIC || g->failed > BG_UBOOT_AT || g->pending > 1u)
        bootguard_clear(g);              /* power-on garbage, or another firmware's record */
    g->reason = reason;
    g->cls = c;
    g->counted = 0;
    if (c == BG_RST_POWERON)
        g->failed = 0;                   /* switched on: nothing to count, whatever happened before */
    else if (g->pending && (c == BG_RST_WDT || c == BG_RST_SOFT)) {
        g->failed++;
        g->counted = 1;
    }
    g->pending = 1;
    if (g->failed >= BG_UBOOT_AT) {
        g->failed = 0;                   /* the next reset (leaving ROM boot) starts clean */
        g->pending = 0;
        g->mode = BOOT_UBOOT;
    } else
        g->mode = g->failed >= BG_SAFE_AT ? BOOT_SAFE : BOOT_NORMAL;
    return g->mode;
}

/* 30 s of uptime: this boot is good (a safe-mode session stays safe: `mode` is kept) */
static inline void bootguard_settled(struct bootguard *g)
{
    g->pending = 0;
    g->failed = 0;
}

/* an intentional reset follows (UBOOT, an update's commit, a reboot asked for): not a failed boot */
static inline void bootguard_intentional(struct bootguard *g) { g->pending = 0; }

/* the last boot was not clean: it followed a counted crash, or this is SAFE MODE */
static inline int bootguard_unclean(const struct bootguard *g) { return g->counted || g->mode != BOOT_NORMAL; }

static inline const char *bootguard_class_name(uint32_t c)
{
    switch (c) {
    case BG_RST_POWERON: return "power-on";
    case BG_RST_WDT: return "wdt";
    case BG_RST_SOFT: return "soft";
    default: return "other";
    }
}
static inline const char *bootguard_mode_name(uint32_t m)
{
    return m == BOOT_SAFE ? "safe" : m == BOOT_UBOOT ? "uboot" : "normal";
}

/* the boot breadcrumb (felucca_dbg.stage, .noinit): where the firmware was when it reset. 1 / 2 / 9: the main loop
 * (main.c); 10..: the boot steps, in order (main.c fm1_cstart / fm1_main, cr_shim.c persist_boot, upreset.c up_boot,
 * cr_ui.c cr_ui_init) */
enum {
    BS_LOOP_INPUT = 1, BS_LOOP_DRAW = 2, BS_LOOP_IDLE = 9,
    BS_GUARD = 10, BS_FLASH, BS_USER_SOUNDS, BS_FM6_BANK, BS_VA_STORE, BS_FM6_STORE, BS_SETTINGS, BS_OTA_CLEANUP,
    BS_LCD, BS_SPLASH, BS_INPUT, BS_SOUNDS, BS_AUDIO, BS_USB, BS_UART, BS_IRQ, BS_CALIB, BS_UI_INIT,
    BS_UI_SETTINGS, BS_LOOPS, BS_N
};
static inline const char *bootguard_stage_name(uint32_t s)
{
    switch (s) {
    case 0: return "none";
    case BS_LOOP_INPUT: return "loop: input";
    case BS_LOOP_DRAW: return "loop: draw";
    case 3: case 4: return "loop: draw";
    case BS_LOOP_IDLE: return "loop: idle";
    case BS_GUARD: return "guard";
    case BS_FLASH: return "flash probe";
    case BS_USER_SOUNDS: return "user sounds";
    case BS_FM6_BANK: return "fm6 bank";
    case BS_VA_STORE: return "va store";
    case BS_FM6_STORE: return "fm6 store";
    case BS_SETTINGS: return "settings";
    case BS_OTA_CLEANUP: return "update cleanup";
    case BS_LCD: return "lcd";
    case BS_SPLASH: return "splash";
    case BS_INPUT: return "input";
    case BS_SOUNDS: return "sounds";
    case BS_AUDIO: return "audio";
    case BS_USB: return "usb";
    case BS_UART: return "uart";
    case BS_IRQ: return "irqs";
    case BS_CALIB: return "calibration";
    case BS_UI_INIT: return "ui init";
    case BS_UI_SETTINGS: return "ui settings";
    case BS_LOOPS: return "loops";
    default: return "?";
    }
}
#endif
