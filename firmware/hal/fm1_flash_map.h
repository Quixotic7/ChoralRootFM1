/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The flash map: which ranges the RAM flash driver (fm1_flash.h) may erase / program, per board
 * (BOARD_SMK37, fm1_cc.h). Macros and compile-time checks only, so host tests can include it. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* The flash chip the storage accepts (JEDEC id from 0x9F: project.c, cr_shim.c); extend it here for another part.
 * FM-1: 0x856014 (Puya P25Q80, 1 MiB) in the units up to 2026; 0xB36014 (another maker's 1 MiB part, same type and
 * capacity bytes) in units with the logo on the front, found 2026-11-02 (HISTORY 3y222: a unit that ran ChoralRoot
 * RAM-only, "no unit ID", and took no update). The driver uses only generic SPI NOR commands (9F, 4B, 05/35, 06,
 * 20, 02, 0B), which the SDK firmware those units shipped with uses on the same chip. */
#if BOARD_SMK37
/* SMK-37: 0xEB6014, 1 MiB, read from a unit over the mask-ROM UBOOT. */
#define FL_JEDEC_OK(id) ((id) == 0xEB6014u || (id) == 0x856014u || (id) == 0xB36014u)
#else
#define FL_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)
#endif

#if BOARD_SMK37
/* ---- SMK-37 flash map (see the fm1-to-smk37 guide) ----
 *   0x00000-0x03FFF  head (SPL, isd_config)         NEVER
 *   0x04000-0x9CFFF  app area (stock PRCT end)      update loader only (FL_APP): the boot stub, the app and the
 *                    commit marker at the FM-1's places (0x4120, 0x5000, 0x91FE0: docs/BOOT-SAFETY.md)
 *   (the SMK-37 has no resource region (cr_res.h): its fonts stay in the image; if it needs one, 0x9D000..0xA8FFF is
 *   48 KB that nothing reads since HISTORY 3y231, inside its plain window)
 *   0x9D000-0xBAFFF  VM: ChoralRoot's store (settings, the sound stores, the user sound banks, the FM6 bank,
 *                    the session, the owner tag, the favourites, the sound pool's sectors 0..4; storage.c)  FL_DATA
 *   0xBB000-0xBFFFF  VM: M-UPGRADE loader staging + update record (ota.c)          FL_OTA
 *   0xC0000-0xC0FFF  BTIF (BT pairing)              NEVER
 *   0xC1000-0xC1FFF  USRTRIM (key/pad/wheel calibration, "trimdat")   NEVER
 *   0xC2000-0xE9FFF  USRFLASH: the CZ-1 banks, the loop slots, the sound pool's sectors 5..8 (storage.c, cr_ui.c)
 *                    (the stock mapping until ChoralRoot first writes there)              FL_SMP
 *   0xEA000-0xEAFFF  USRFLASH's last sector: the stock end marker "usrdata" at 0xEAFF8; erased once before the
 *                    first write into FL_SMP, no data of ChoralRoot's                    FL_STOCKTAG (erase only)
 *   0xEB000-0xF1FFF  the free tail (unlisted): the sound pool's sectors 9..15 (0.24)       FL_TAIL
 *   0xF2000-0xF3FFF  free (unlisted), unused
 *   0xF4000-0xFDFFF  USR (stock resources; kept for a rollback)   NEVER
 *   0xFE000-0xFEFFF  free, unused
 *   0xFF000-0xFFFFF  key_mac                        NEVER
 * Everything below is an explicit allow-list; the asserts keep each allowed window out of the
 * protected ranges at compile time. */
#define FL_HEAD_HI      0x00004000u                /* protected: [0, FL_HEAD_HI) */
#define FL_CAL_LO       0x000C0000u                /* protected: BTIF + USRTRIM */
#define FL_CAL_HI       0x000C2000u
#define FL_USRRES_LO    0x000F4000u                /* protected: stock USR resources */
#define FL_USRRES_HI    0x000FE000u
#define FL_KEYMAC_LO    0x000FF000u                /* protected: key_mac and above */
#define FL_APP_LO       0x00004000u                /* the update loader: app area */
#define FL_APP_HI       0x0009D000u
#define FL_REC_LO       0x0009D000u                /* the update loader: where it may drop records (VM only) */
#define FL_REC_HI       0x000C0000u
#define FL_PLAIN_LO     0x0009D000u                /* plain XIP window from the VM start */
#define FL_DATA_LO      0x0009D000u                /* ChoralRoot's store */
#define FL_DATA_HI      0x000BB000u
#define FL_OTA_LO       0x000BB000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000C0000u
#define FL_SMP_LO       0x000C2000u                /* USRFLASH: the CZ-1 banks, the loop slots */
#define FL_SMP_HI       0x000EA000u
#define FL_TAIL_LO      0x000EB000u                /* the free tail after USRFLASH: the sound pool's last 7 sectors */
#define FL_TAIL_HI      0x000F2000u
/* USRFLASH is the stock firmware's mapping (global settings 0xC2000, 8 presets from 0xC3000) with its end marker
 * "usrdata\0" at 0xEAFF8: while the marker is there the stock firmware trusts the area and never rebuilds it.
 * Before ChoralRoot's first write into USRFLASH the marker sector is erased (storage.c st_stock_release), so a
 * return to stock rebuilds the factory mapping. That one sector is erased whole and never programmed. */
#define FL_STOCKTAG_LO  0x000EA000u
#define FL_STOCKTAG_HI  0x000EB000u
#define FL_STOCKTAG_OFF 0x000EAFF8u
#define FL_STOCKTAG_OK(off, n) ((uint32_t)(off) == FL_STOCKTAG_LO && (uint32_t)(n) == FL_STOCKTAG_HI - FL_STOCKTAG_LO)
/* a window [lo, hi) is allowed: sector aligned, non-empty, and clear of every protected range */
#define FL_WIN_SAFE(lo, hi) ((lo) % 0x1000u == 0u && (hi) % 0x1000u == 0u && (lo) < (hi) && \
                             (lo) >= FL_HEAD_HI && (hi) <= FL_KEYMAC_LO && \
                             ((hi) <= FL_CAL_LO || (lo) >= FL_CAL_HI) && \
                             ((hi) <= FL_USRRES_LO || (lo) >= FL_USRRES_HI))
_Static_assert(FL_WIN_SAFE(FL_DATA_LO, FL_DATA_HI), "SMK: FL_DATA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_OTA_LO, FL_OTA_HI), "SMK: FL_OTA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_SMP_LO, FL_SMP_HI), "SMK: FL_SMP overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_TAIL_LO, FL_TAIL_HI) && FL_TAIL_HI <= FL_USRRES_LO,
               "SMK: FL_TAIL overlaps a protected range (the stock resources from 0xF4000)");
_Static_assert(FL_WIN_SAFE(FL_STOCKTAG_LO, FL_STOCKTAG_HI) && FL_STOCKTAG_LO == FL_SMP_HI &&
               FL_STOCKTAG_OFF + 8u == FL_STOCKTAG_HI, "SMK: the stock marker sector");
_Static_assert(FL_TAIL_LO == FL_STOCKTAG_HI, "SMK: FL_TAIL starts after the stock marker's sector (0xEA000)");
_Static_assert(FL_WIN_SAFE(FL_APP_LO, FL_APP_HI), "SMK: FL_APP overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_REC_LO, FL_REC_HI), "SMK: FL_REC overlaps a protected range");
_Static_assert(FL_REC_HI == FL_CAL_LO && FL_OTA_HI <= FL_REC_HI && FL_OTA_LO >= FL_REC_LO,
               "SMK: the update record (OTA area) must lie in the loader's record window, below BTIF");
_Static_assert(FL_DATA_HI <= FL_OTA_LO && FL_APP_HI <= FL_DATA_LO, "SMK: windows overlap");
/* the update loader (loader.c): the app area, and records only in the VM below BTIF */
#define FL_LDR_RANGE_OK(off, n) (FL_IN(off, n, FL_APP_LO, FL_APP_HI) || FL_IN(off, n, FL_REC_LO, FL_REC_HI))
/* ChoralRoot's own store: FL_DATA, USRFLASH and the tail after its last sector */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_SMP_LO, FL_SMP_HI) || \
                             FL_IN(off, n, FL_TAIL_LO, FL_TAIL_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_STOCKTAG_OK(off, n))
#endif
/* second, independent net in the RAM driver: [off, off + n) touches a protected range (also when a
 * caller supplies its own FL_RANGE_OK, as the update loader does) */
#define FL_HITS(off, n, lo, hi) ((uint32_t)(off) < (hi) && \
                                 ((uint32_t)(off) >= (lo) || (uint32_t)(n) > (lo) - (uint32_t)(off)))
#define FL_PROTECTED(off, n) (FL_HITS(off, n, 0u, FL_HEAD_HI) || FL_HITS(off, n, FL_CAL_LO, FL_CAL_HI) || \
                              FL_HITS(off, n, FL_USRRES_LO, FL_USRRES_HI) || (uint32_t)(off) >= FL_KEYMAC_LO || \
                              (uint32_t)(n) > FL_KEYMAC_LO - (uint32_t)(off))
#define FL_GUARD(off, n) (FL_RANGE_OK(off, n) && !FL_PROTECTED(off, n))
#else /* FM-1 */
#define FL_PLAIN_LO     0x00093000u                /* plain XIP window from the FM-1 VM start */
/* The resource region (HISTORY 3y234, docs/BLE-LINK.md step 1): read-only data the update package carries beside the
 * app (flash.bin past 0x93000) and the update loader writes with the body, before the commit marker. The app reads it
 * through the plain window (XIP 0x0208F000..) and never writes it: it lies outside FL_RANGE_OK below. Its first 64
 * bytes are the header (firmware/src/cr_res.h), the data from FL_RES_DATA. 0x93000..0x96FFF were never used; the rest
 * held 0.21's sound objects until 3y231 */
#define FL_RES_LO       0x00093000u
#define FL_RES_HI       0x0009F000u
#define FL_RES_DATA     (FL_RES_LO + 0x40u)
#define FL_DATA_LO      0x0009F000u                /* ChoralRoot's store (0x97000 until 3y234: the region took its
                                                    * first 8 sectors, free since 3y231) */
#define FL_DATA_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
/* Felucca's own store (projects, user samples; settings) */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI))
/* Where the RAM driver may erase / program. The app build allows only its own
 * data regions; the update loader (firmware/loader) defines its own window. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
#define FL_GUARD(off, n) FL_RANGE_OK(off, n)
_Static_assert(FL_RES_LO == FL_PLAIN_LO && FL_RES_HI <= FL_DATA_LO && FL_RES_HI <= FL_OTA_LO,
               "FM-1: the resource region lies in the plain window, below the store and the OTA staging");
#endif /* BOARD_SMK37 */
