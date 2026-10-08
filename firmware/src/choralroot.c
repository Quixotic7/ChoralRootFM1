/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca by Leo Kuroshita, Hügelton Instruments) */
/* CHORALROOT FM-1: one compilation unit (the HAL is header-only), replacing felucca.c (docs/INTEGRATION.md
 * section 1). Felucca's order, Felucca's platform; Felucca's instrument (ui*.c, project.c, editor*.c, favorites.c,
 * ui_name.c) dropped for ChoralRoot's: the engine (cr_engine.c), its streams to the parts and MIDI (cr_out.c), the
 * screens (cr_gfx.c, cr_draw.c), the motion (cr_anim.c), the UI (cr_ui.c) and the names main.c and the kept files
 * call (cr_shim.c). Of editor*.c's SysEx only the backup / restore subset stays, on ChoralRoot's objects (cr_backup.c).
 *
 * Dropped as in the emulator (tools/emu/emu_firmware.h, the same order): Felucca's sequencer, seq.c with
 * song_chain.c, chord.c, motion.c, midi_control.c and midi_clock.c (FELUCCA_SEQ 0). cr_out.c provides what the kept
 * files call of it under the same names: events_block (fx.c, every block: the engine switches, voice.c engine_block,
 * and the releases a sound load asks for, panic_req), midi_event (MIDI in on the CHORD / BASS channels plays parts
 * 1 / 2, INTEGRATION section 2) and the few names upreset.c, main.c and audio.c still use. Dropped engines: SAMPLE,
 * GRAIN, DRUM (retired placeholders keep the engine numbers, engines.c); Felucca's icon atlas and keycaps are not
 * in the image (FELUCCA_ICONS, FELUCCA_KEYCAPS).
 *
 * Host check (no device toolchain): cc -fsyntax-only -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal
 * firmware/src/choralroot.c. The device build is tools/build.py with this file as the unit (see the Status section
 * of docs/INTEGRATION.md for what that still needs). */

/* ---------------------------------------------------- build options --- */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1          /* flash driver + storage.c: settings, user presets */
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1            /* M-UPGRADE update entry; needs FELUCCA_FLASH */
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1            /* USB CDC-ACM serial console (with FELUCCA_UAC: presented while both USB audio devices
                                  * are off, and in SAFE MODE; usb.c usb_cdc_on) */
#endif
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 1            /* USB audio recording (Melodee's, docs/USB-AUDIO.md): "ChoralRoot In" records the
                                  * master, CHORD and BASS (six channels) */
#endif
#ifndef FELUCCA_UART
#define FELUCCA_UART 1           /* TRS MIDI IN on UART1 / PH8 */
#endif
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0          /* no SLICE engine (ChoralRoot: melodic engines only) */
#endif
#ifndef FELUCCA_SLICER
#define FELUCCA_SLICER 0         /* no SLICER insert (slicer.c stubs; frees its 32 KB POOL buffer) */
#endif
#ifndef FELUCCA_FM4
#define FELUCCA_FM4 0
#endif
#ifndef FELUCCA_SAMPLE
#define FELUCCA_SAMPLE 0         /* no SAMPLE engine, no sample sets in the image (engine 4: a retired slot) */
#endif
#ifndef FELUCCA_GRAIN
#define FELUCCA_GRAIN 0          /* no GRAIN engine (engine 8: a retired slot; frees its 27 KB POOL) */
#endif
#ifndef FELUCCA_DRUM
#define FELUCCA_DRUM 0           /* no DRUM engine (engine 10: a retired slot; frees its kit's POOL) */
#endif
#ifndef FELUCCA_SEQ
#define FELUCCA_SEQ 0            /* no Felucca sequencer (seq.c and its satellites): cr_out.c's events_block, midi_event */
#endif
#ifndef FELUCCA_VA
#define FELUCCA_VA 1             /* the VA engine (eng_va.c, engine 13) and its patch store (va_store.c) */
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 1             /* Melodee's CZ-1 engine (eng_cz.c, engine 14), its tone store and banks (docs/CZ1.md) */
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1           /* the QUAD engine (eng_quad.c, engine 15: four-operator FM) and its patch store (quad_store.c,
                                  * docs/QUAD.md) */
#endif
#ifndef FM6_POLY
#define FM6_POLY 8               /* FM6's voices per part (eng_fm6.c; Melodee plays Dexed's 16): ChoralRoot's chords use <= 7,
                                  * and at 16 the release tails of perf.sh (c) / (e) cost up to 59 / 62 % of a block
                                  * (8: 40 / 43 %; docs/FM6.md, Polyphony). Still one budget unit a voice */
#endif
#ifndef FELUCCA_ICONS
#define FELUCCA_ICONS 0          /* no Felucca icon atlas (icons.c, ui_icons.h): cr_draw.c draws its header glyphs */
#endif
#ifndef FELUCCA_KEYCAPS
#define FELUCCA_KEYCAPS 0        /* no Felucca keycaps (gfx.c's key hints, ui_keycaps.h): no cr_*.c draws one */
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_920"    /* package identity (build.py: the .fwsc marker string) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "ChoralRoot 0.14"   /* the dev build reads as the next tag; --release X.Y sets it */
#endif
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif

#if defined(__APPLE__)            /* the host syntax check only: Mach-O has no ELF sections (.noinit, .pool) */
#define __attribute__(x)
#endif

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "felucca_tables.h"      /* generated by build.py (tools/gen_tables.py) */

/* ----------------------------------------------------- base, display --- */
#include "libc.c"
#include "lcd.c"
#include "gfx.c"

/* ----------------------------------------------------------- sound --- */
#include "core.h"
#include "engines.c"             /* dsp.c, the eng_*.c files */
#include "params.c"
#include "mod.c"
#include "voice.c"
#include "slicer.c"
#define mix_block fx_mix_block   /* fx.c's mix; audio.c gets ChoralRoot's (below) */
#include "fx.c"
#undef mix_block

/* ------------------------------------------------------------ MIDI --- */
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif
#if FELUCCA_SEQ
#include "song_chain.c"
#include "seq.c"                 /* (FELUCCA_SEQ 1 only: Felucca's sequencer, inert under ChoralRoot's UI) */
#endif

/* ------------------------------------------------- the ChoralRoot engine --- */
/* cr_engine.h's cr_param_t (a perform parameter) and cr_screen.h's (a params column) share a name: the engine's is
 * renamed in this unit (cr_out.c / cr_ui.c call it cr_eparam_t) */
#define cr_param_t cr_eparam_t
#include "cr_engine.h"
#include "cr_engine.c"
#undef cr_param_t
#include "cr_loop.h"
#include "cr_loop.c"             /* the looper: the engine's loop voices, ticked with it */
#include "cr_out.c"              /* streams -> parts 0 / 1 and MIDI; the engine's clock and input queue */
static void mix_block(int32_t *out, uint32_t n)   /* the audio ISR's block: the engine first, then the mix */
{
    cr_audio_block(n);
#if FELUCCA_UAC
    ua_stage_on = ua.cap_alt && !USB_CDC_ON;     /* the computer records: fx.c stages the block's capture frames */
#endif
    fx_mix_block(out, n);
#if FELUCCA_UAC
    if (ua_stage_on) {   /* USB audio: ChoralRoot In's master pair (fx.c ua_stage), before the click */
        uint32_t i;
        for (i = 0; i < n; i++) {
            ua_stage[i * UA_CAP_CHANNELS] = ua_sat(out[2u * i]);
            ua_stage[i * UA_CAP_CHANNELS + 1u] = ua_sat(out[2u * i + 1u]);
        }
    }
#endif
    cr_click_mix(out, n);        /* the metronome / count-in click */
}
#include "audio.c"

/* -------------------------------------------------------------- UI --- */
#include "panel.c"
#if FELUCCA_ICONS
#include "icons.c"               /* (1: Felucca's parameter icons; nothing of ChoralRoot's UI calls them) */
#endif
#include "cr_gfx.c"              /* scalable text + shapes on gfx.c (build/gen/cr_fonts.h) */
#include "cr_draw.c"             /* the screens (cr_screen.h) */
#include "cr_anim.c"
#if FELUCCA_FLASH                /* storage before the sounds: upreset.c reads / writes its banks */
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#endif
#include "cr_bank.c"             /* upreset.c's UI names, upreset.c (32 user sounds, fm6_bank.c), the factory bank */
#include "cr_pages.c"            /* the sound pages (EDIT) */
#include "cr_name.c"             /* naming with the root keys (SAVE) */
#if __has_include("cr_settings.c")
#define CR_HAVE_SETTINGS 1       /* (before cr_ui.c: cr_ui_init loads the record) */
#else
#define CR_HAVE_SETTINGS 0
#endif
#include "cr_ui.c"
#if CR_HAVE_SETTINGS
#include "cr_settings.c"         /* the settings record (after cr_ui.c and storage.c) */
#endif
#include "fm6_store.c"           /* FM6: DX7 SysEx in / out, bulk dumps to the bank (Melodee's; after cr_ui.c) */
#include "cz_store.c"            /* CZ-1: Casio tone SysEx in / out (Melodee's; after cr_ui.c) */
#include "cr_backup.c"           /* backup / restore over SysEx (after usb.c, the stores, cr_ui.c, cr_settings.c) */
#include "cr_shim.c"             /* what main.c and the kept files call of Felucca's dropped UI */

/* ----------------------------------------------------------- update --- */
#if FELUCCA_OTA
#include "ota.c"
#include "ota_hw.c"
#endif
#if FELUCCA_CDC
#include "console.c"
#endif
#include "main.c"
