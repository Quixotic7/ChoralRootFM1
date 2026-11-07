/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The resource region (HISTORY 3y234; docs/BLE-LINK.md step 1; docs/BOOT-SAFETY.md "The resource region"):
 * read-only data that lives outside the app slot, at flash FL_RES_LO..FL_RES_HI (0x93000..0x9EFFF on the FM-1,
 * hal/fm1_flash_map.h), read through the plain XIP window (XIP 0x0208F000..). Today: the glyph data of the fonts M,
 * CRX and CRB (tools/gen_aa_font.py RES: their AF_<N>_DATA carry AF_RES, the section .res.font, firmware/app.ld
 * .res); later the radio's start-up program (the header's ble_off / ble_len).
 *
 *   the package   tools/fm1pkg_make.py --res: flash.bin runs on past 0x93000 to FL_RES_HI, the header at FL_RES_LO,
 *                 the data from FL_RES_DATA (plain bytes: never SFC-encrypted, the plain window reads them as they are)
 *   the update    firmware/loader/ldr_core.c writes those sectors with the body, after the commit marker's sector went
 *                 blank and before it is written again: a cut install leaves an uncommitted body the stub rescues, and
 *                 a committed one always has its region (an older package, flash.bin 0x93000 long, writes none)
 *   the app       cr_shim.c cr_res_boot, once at power-on after fl_plain_window_init: the header's magic, format, own
 *                 CRC, the data's length (this build's .res) and CRC-32, and the body's CRC-32, which must be the one
 *                 in this build's commit marker (0x91FE0): a region of another build is refused. Until then, and when
 *                 refused, cr_res_ok is 0: gfx.c and cr_gfx.c draw no glyph whose data lies in the region (it would be
 *                 another build's bytes or erased flash), the crash screen and the UBOOT lines use S (CR_FONT_M), and
 *                 the splash says FONTS MISSING in S. Nothing else reads the region.
 * Only CR_RES 1 (choralroot.c, the FM-1 device build) has a region; the SMK-37 build, the emulator and the host
 * tests keep every font in their image (AF_RES empty, cr_res_ok the constant 1). */
#pragma once
#include <stdint.h>

#ifndef CR_RES
#define CR_RES 0
#endif
#if CR_RES
#include "../hal/fm1_flash_map.h"
#include "../hal/fm1_xip.h"
#endif

#define CR_RES_MAGIC 0x53455243u            /* "CRES" */
#define CR_RES_FORMAT 1u
#define CR_RES_MARK_OFF 0x91FE0u            /* the stub's commit marker (stub_core.h): magic, format, length, CRC-32 */
#define CR_RES_MARK_MAGIC 0x4D425243u       /* "CRBM" */
typedef struct {
    uint32_t magic, format;
    uint32_t len;                           /* data bytes from FL_RES_DATA (this build's .res) */
    uint32_t crc;                           /* their CRC-32 (zlib's) */
    uint32_t body_crc;                      /* the app body's CRC-32, as the package's commit marker holds it */
    uint32_t ble_off, ble_len;              /* the radio program's slot in the data (docs/BLE-LINK.md): 0, 0 until then */
    uint32_t rsv[8];                        /* 0 */
    uint32_t hcrc;                          /* CRC-32 of the 60 bytes before it */
} cr_res_hdr_t;
_Static_assert(sizeof(cr_res_hdr_t) == 64u, "the resource region's header: 64 bytes (FL_RES_DATA - FL_RES_LO)");

#if CR_RES
static uint8_t cr_res_ok;                   /* 1: the region is this build's (cr_shim.c cr_res_boot) */
#define CR_RES_IN(p) ((uint32_t)(uintptr_t)(p) >= FM1_XIP(FL_RES_LO) && (uint32_t)(uintptr_t)(p) < FM1_XIP(FL_RES_HI))
#define CR_RES_GONE(p) (!cr_res_ok && CR_RES_IN(p))   /* data in a region that is not there: draw nothing from it */
#define CR_FONT_M (cr_res_ok ? &AF_M : &AF_S)         /* the crash screen and UBOOT: legible without the region */
#else
#define cr_res_ok 1
#define CR_RES_GONE(p) 0
#define CR_FONT_M (&AF_M)
#endif
