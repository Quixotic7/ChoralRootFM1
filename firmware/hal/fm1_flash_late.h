/* SPDX-License-Identifier: GPL-3.0-only */
/* A late erase or page program counts as done (docs/HISTORY.md 3y265; the owner, 2026-11-08: "Treat late as success").
 * fl_wait_issued (fm1_flash_wait.h, felucca-bsp's, unchanged) returns FL_ERR_LATE_READY (-4) for a part that read idle
 * after the normal bound (500 ms an erase, 10 ms a page) but before the extended one (2 s, 50 ms): the operation
 * finished, the part is idle, XIP is safe again. fl_late_ok turns that into 0 for every caller (the app's saves, the
 * OTA staging, the update loader: all go through fm1_flash.h's fl_erase4k_ram / fl_prog_ram) and counts it:
 * fl_late_ops since power-on, fl_late_max_us the longest (73/78 --debug: flash_late_ops, flash_late_max_us). Still
 * busy at the extended bound never gets here: fl_wait_issued resets the part and the chip.
 * The includer supplies RAMINL and FL_ERR_LATE_READY; the host test (tests/flash_late_test.c) includes it too.
 * Counters in RAM (.bss): the RAM driver may write them with XIP off. */
#pragma once
#include <stdint.h>
static volatile uint32_t fl_late_ops, fl_late_max_us;
RAMINL int fl_late_ok(int rc, uint32_t took_us)
{
    if (rc != FL_ERR_LATE_READY)
        return rc;
    fl_late_ops++;
    if (took_us > fl_late_max_us)
        fl_late_max_us = took_us;
    return 0;
}
