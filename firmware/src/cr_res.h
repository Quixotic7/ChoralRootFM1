/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The resource region (HISTORY 3y234; docs/BLE-LINK.md step 1; docs/BOOT-SAFETY.md "The resource region"):
 * read-only data that lives outside the app slot, at flash FL_RES_LO..FL_RES_HI (0x93000..0x9EFFF on the FM-1,
 * 0x9D000..0xA8FFF on the SMK-37 since HISTORY 3y295; hal/fm1_flash_map.h), read through the plain XIP window (XIP
 * 0x0208F000.. / 0x02099000..). Today: the glyph data of the fonts M,
 * CRX and CRB (tools/gen_aa_font.py RES: their AF_<N>_DATA carry AF_RES, the section .res.font, firmware/app.ld
 * .res), Casio's 64 CZ-1 tones packed (tools/gen_cz1_factory.py: CZ_FACTORY_LZ4 carries CZ_RES, the section .res.cz,
 * HISTORY 3y293); later the radio's start-up program (the header's ble_off / ble_len).
 *
 *   the package   tools/fm1pkg_make.py --res: flash.bin runs on past the app area (PRCT: 0x93000, SMK 0x9D000) to
 *                 FL_RES_HI, the header at FL_RES_LO,
 *                 the data from FL_RES_DATA (plain bytes: never SFC-encrypted, the plain window reads them as they are)
 *   the update    firmware/loader/ldr_core.c writes those sectors with the body, after the commit marker's sector went
 *                 blank and before it is written again: a cut install leaves an uncommitted body the stub rescues, and
 *                 a committed one always has its region (an older package, flash.bin PRCT long, writes none)
 *   the app       cr_shim.c cr_res_boot, once at power-on after fl_plain_window_init: the header's magic, format, own
 *                 CRC, the data's length (this build's .res) and CRC-32, and the body's CRC-32, which must be the one
 *                 in this build's commit marker (0x91FE0): a region of another build is refused. Until then, and when
 *                 refused, cr_res_ok is 0: gfx.c and cr_gfx.c draw no glyph whose data lies in the region (it would be
 *                 another build's bytes or erased flash), the crash screen and the UBOOT lines use S (CR_FONT_M), and
 *                 the splash says FONTS MISSING in S; a factory CZ-1 tone is not loaded (eng_cz.c cz_factory_bank:
 *                 the part keeps its tone, the message box says "install ChoralRoot again"; BANK A..D never saved
 *                 stay empty). Nothing else reads the region.
 * Only CR_RES 1 (choralroot.c: the device builds, the FM-1's and since 3y295 the SMK-37's, which carries the fonts
 * alone) has a region; the emulator and the host tests keep every font in their image (AF_RES empty, cr_res_ok the
 * constant 1).
 *
 * The second region (HISTORY 3y296; docs/BOOT-SAFETY.md "The second resource region"): FM-1 only (CR_RES2 1,
 * choralroot.c), flash FL_RES2_LO..FL_RES2_HI (0xDC000..0xDFFFF, 16 KB: 0.21's user banks, free since 3y231), XIP
 * 0x020D8040.. for its data. Read-only tables that only the main loop reads, by section (firmware/app.ld .res2):
 *   .res2.kern    the kerning pairs of M and CRB (AF_M_KB / KD, AF_CRB_KB / KD: tools/gen_aa_font.py RES2, the
 *                 attribute AF_RES2); their row tables stay in the image, and S's kerning, the fallback font's
 *   .res2.preset  the factory presets' lists of FM6, VA, CZ-1 and PROPHET (engine_t.presets: RES2_PRESET)
 *   .res2.fm6     FM6's factory voices F1..F24 (FM6_FACTORY, FM6_ROM)
 *   .res2.drum    reserved for the drum kits that do not fit the first region (RES2_DRUM; 514 B a kit)
 * The package carries it as the entry res2.bin (type CR_RES2_TYPE, tools/fm1pkg_make.py --res2), the whole 16 KB:
 * this header (cr_res_hdr_t with CR_RES2_MAGIC, its ble fields 0), the data from FL_RES2_DATA, erased bytes after.
 * The update loader writes its four sectors with the body, after the first region's and before the commit marker
 * (ldr_core.c), so a cut install is uncommitted and a committed one has it; a package without the entry leaves the
 * sectors as they were. cr_shim.c cr_res2_boot checks it as cr_res_boot the first; when refused, cr_res2_ok is 0 and
 * every reader goes without (CR_RES2_GONE): no kerning on M and CRB (gfx.c kern: 0), each factory preset of a list in
 * the region reads as eng_preset's stand-in (engines.c: "NO PRESET", the engine's defaults) and FM6's factory voices
 * as the init voice (eng_fm6.c fm6_slot_get); the splash says PRESETS MISSING. Nothing on the audio side or the second
 * core reads the region. The host, the emulator and the SMK-37 keep these tables in their image (the attributes empty,
 * cr_res2_ok the constant 1). */
#pragma once
#include <stdint.h>

#ifndef CR_RES
#define CR_RES 0
#endif
#ifndef CR_RES2
#define CR_RES2 0
#endif
#ifndef AF_RES2
#define AF_RES2                             /* (choralroot.c: the second region's sections on the FM-1) */
#endif
#ifndef RES2_PRESET
#define RES2_PRESET
#endif
#ifndef RES2_FM6
#define RES2_FM6
#endif
#ifndef RES2_DRUM
#define RES2_DRUM
#endif
#if CR_RES || CR_RES2
#include "../hal/fm1_flash_map.h"
#include "../hal/fm1_xip.h"
#endif

#define CR_RES_MAGIC 0x53455243u            /* "CRES" */
#define CR_RES_FORMAT 1u
#define CR_RES_MARK_OFF 0x91FE0u            /* the stub's commit marker (stub_core.h): magic, format, length, CRC-32 */
#define CR_RES_MARK_MAGIC 0x4D425243u       /* "CRBM" */
#define CR_RES2_MAGIC 0x32455243u           /* "CRE2": the second region's header (else as the first's) */
#define CR_RES2_TYPE 200u                   /* its UFW entry's type in the package (flash.bin 0, ota.bin 100) */
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
#if CR_RES2
static uint8_t cr_res2_ok;                  /* 1: the second region is this build's (cr_shim.c cr_res2_boot) */
#ifndef CR_RES2_IN                          /* (tests/cr_res2_test.c: its own, the tables by name, on the host) */
#define CR_RES2_IN(p) ((uint32_t)(uintptr_t)(p) >= FM1_XIP(FL_RES2_LO) && (uint32_t)(uintptr_t)(p) < FM1_XIP(FL_RES2_HI))
#endif
#define CR_RES2_GONE(p) (!cr_res2_ok && CR_RES2_IN(p))   /* a table in a second region that is not there: go without */
#else
#define cr_res2_ok 1
#define CR_RES2_GONE(p) 0
#endif
