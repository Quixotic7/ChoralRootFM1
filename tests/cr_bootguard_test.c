/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the boot guard's decision (firmware/src/cr_bootguard.h, main.c fm1_cstart): power-on resets never
 * escalate and clear; two crash-type resets in a row -> SAFE; a power-on after SAFE -> NORMAL, the guard clear; four ->
 * UBOOT (then clear); the 30 s clear; intentional resets; the magic / init path; the reason word and its classes. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/cr_bootguard.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                                                 printf(__VA_ARGS__); printf("\n"); } } while (0)

#define PWR bootguard_reason(BG_P3_POWERON, 0)
#define WDT bootguard_reason(BG_P3_WDT, 0)
#define SOFT bootguard_reason(BG_P3_SOFT, 0)
#define CORE bootguard_reason(0, 0x20u)
#define LONGPRESS bootguard_reason(0x10u, 0)
#define BROWNOUT bootguard_reason(0x02u, 0)

int main(void)
{
    struct bootguard g;
    uint32_t i, m;

    /* the reason word and its classes (hal/fm1_sys.h: P3_RST_SRC bit0 power-on, 2 WDT, 6 soft; RST_SRC bit5 soft) */
    CHECK(bootguard_class(PWR) == BG_RST_POWERON, "power-on");
    CHECK(bootguard_class(WDT) == BG_RST_WDT, "wdt");
    CHECK(bootguard_class(SOFT) == BG_RST_SOFT, "soft P33");
    CHECK(CORE == BG_RST_CORE && bootguard_class(CORE) == BG_RST_SOFT, "soft PWR_CON");
    CHECK(bootguard_class(LONGPRESS) == BG_RST_OTHER && bootguard_class(BROWNOUT) == BG_RST_OTHER &&
          bootguard_class(0) == BG_RST_OTHER, "other");
    CHECK(bootguard_class(BG_P3_POWERON | BG_P3_WDT) == BG_RST_WDT, "power-on + wdt bits: wdt wins");
    CHECK(bootguard_class(BG_P3_POWERON | BG_P3_SOFT) == BG_RST_SOFT, "power-on + soft bits: soft wins");
    CHECK(bootguard_reason(0x1FFu, 0xFFFFFFDFu) == 0xFFu, "only P3 bits 0..7 and RST_SRC bit5");

    /* the magic / init path: garbage (a power-on's RAM) or Felucca 1.0's record -> clean, whatever the reason */
    memset(&g, 0xA5, sizeof g);
    m = bootguard_step(&g, WDT);
    CHECK(m == BOOT_NORMAL && g.magic == BOOTGUARD_MAGIC && g.failed == 0 && g.pending == 1 && !g.counted,
          "garbage + wdt: normal, clean (failed %u)", (unsigned)g.failed);
    memset(&g, 0, sizeof g);
    g.magic = 0x42475244u;                       /* Felucca 1.0's magic, its 3 words */
    g.failed = 1;
    g.pending = 1;
    CHECK(bootguard_step(&g, WDT) == BOOT_NORMAL && g.failed == 0, "an old record is not trusted");
    bootguard_clear(&g);
    g.failed = 9;                                /* out of range */
    CHECK(bootguard_step(&g, WDT) == BOOT_NORMAL && g.failed == 0, "failed out of range: cleared");
    bootguard_clear(&g);
    g.pending = 7;
    CHECK(bootguard_step(&g, WDT) == BOOT_NORMAL && g.failed == 0, "pending out of range: cleared");

    /* power-on resets never escalate: 20 quick on/off cycles (each boot under 30 s) */
    bootguard_clear(&g);
    for (i = 0; i < 20u; i++) {
        m = bootguard_step(&g, PWR);
        CHECK(m == BOOT_NORMAL && g.failed == 0 && g.pending == 1, "power-on %u: mode %u failed %u", (unsigned)i,
              (unsigned)m, (unsigned)g.failed);
    }
    /* brown-outs and the long press: never counted (and never clear) */
    bootguard_clear(&g);
    for (i = 0; i < 10u; i++)
        CHECK(bootguard_step(&g, i & 1u ? BROWNOUT : LONGPRESS) == BOOT_NORMAL && g.failed == 0, "other %u", (unsigned)i);
    bootguard_clear(&g);
    bootguard_step(&g, PWR);
    bootguard_step(&g, WDT);                      /* failed 1 */
    CHECK(bootguard_step(&g, BROWNOUT) == BOOT_NORMAL && g.failed == 1 && !g.counted, "brown-out keeps failed 1");

    /* two crash-type resets in a row -> SAFE (the first boot's pending is the power-on's) */
    bootguard_clear(&g);
    CHECK(bootguard_step(&g, PWR) == BOOT_NORMAL, "power-on");
    m = bootguard_step(&g, WDT);
    CHECK(m == BOOT_NORMAL && g.failed == 1 && g.counted && bootguard_unclean(&g), "1st crash: normal, failed 1");
    m = bootguard_step(&g, SOFT);
    CHECK(m == BOOT_SAFE && g.failed == 2 && g.mode == BOOT_SAFE, "2nd crash (soft: fm1_fault's reboot): SAFE");
    m = bootguard_step(&g, CORE);
    CHECK(m == BOOT_SAFE && g.failed == 3, "3rd (safe mode crashed once): still SAFE");
    /* a power-on after SAFE -> NORMAL with the guard clear */
    m = bootguard_step(&g, PWR);
    CHECK(m == BOOT_NORMAL && g.failed == 0 && g.pending == 1 && !g.counted && !bootguard_unclean(&g),
          "power-on after SAFE: normal, clear (failed %u)", (unsigned)g.failed);

    /* four crash-type resets -> UBOOT, the record cleared (the reset that leaves ROM boot starts clean) */
    bootguard_clear(&g);
    bootguard_step(&g, PWR);
    for (i = 1; i <= 4u; i++) {
        m = bootguard_step(&g, WDT);
        CHECK(m == (i < 2u ? BOOT_NORMAL : i < 4u ? BOOT_SAFE : BOOT_UBOOT), "crash %u: mode %u", (unsigned)i, (unsigned)m);
    }
    CHECK(g.failed == 0 && g.pending == 0 && g.mode == BOOT_UBOOT, "UBOOT: cleared");
    CHECK(bootguard_step(&g, WDT) == BOOT_NORMAL && g.failed == 0, "after UBOOT, even a wdt: normal");
    bootguard_clear(&g);
    bootguard_step(&g, PWR);
    bootguard_step(&g, WDT);
    bootguard_step(&g, WDT);
    bootguard_step(&g, WDT);
    CHECK(bootguard_step(&g, PWR) == BOOT_NORMAL && g.failed == 0, "a power-on before the 4th: normal again");

    /* the 30 s clear: a crash after 30 s of uptime is not counted; in SAFE MODE it clears failed, keeps the mode */
    bootguard_clear(&g);
    bootguard_step(&g, PWR);
    bootguard_step(&g, WDT);
    CHECK(g.failed == 1, "failed 1");
    bootguard_settled(&g);
    CHECK(g.failed == 0 && g.pending == 0, "30 s: clear");
    CHECK(bootguard_step(&g, WDT) == BOOT_NORMAL && g.failed == 0 && !g.counted, "a crash after 30 s: not counted");
    bootguard_step(&g, WDT);                      /* (that boot died early) failed 1 */
    CHECK(bootguard_step(&g, WDT) == BOOT_SAFE, "two early ones: SAFE");
    bootguard_settled(&g);
    CHECK(g.mode == BOOT_SAFE && g.failed == 0 && g.pending == 0, "SAFE 30 s: failed 0, the session stays safe");
    CHECK(bootguard_step(&g, SOFT) == BOOT_NORMAL && g.failed == 0, "the reboot after a good safe session: normal");
    CHECK(bootguard_step(&g, WDT) == BOOT_NORMAL && g.failed == 1, "the loads crash again: counted");
    CHECK(bootguard_step(&g, WDT) == BOOT_SAFE, ".. and back to SAFE, not ROM boot");

    /* intentional resets (OCT- + OCT+ 5 s, the SysEx key, the update commit, Flash Data's reboot): not counted */
    bootguard_clear(&g);
    bootguard_step(&g, PWR);
    for (i = 0; i < 6u; i++) {
        bootguard_intentional(&g);
        m = bootguard_step(&g, i & 1u ? CORE : SOFT);
        CHECK(m == BOOT_NORMAL && g.failed == 0, "intentional %u: normal", (unsigned)i);
    }

    /* names */
    CHECK(!strcmp(bootguard_class_name(BG_RST_WDT), "wdt") && !strcmp(bootguard_mode_name(BOOT_SAFE), "safe") &&
          !strcmp(bootguard_stage_name(BS_FM6_BANK), "fm6 bank") && !strcmp(bootguard_stage_name(999), "?"), "names");
    for (i = BS_GUARD; i < BS_N; i++)
        CHECK(strcmp(bootguard_stage_name(i), "?") != 0, "stage %u has a name", (unsigned)i);

    printf("cr_bootguard_test: %d of %d checks failed\n", fails, checks);
    if (!fails)
        printf("cr_bootguard_test passed\n");
    return fails != 0;
}
