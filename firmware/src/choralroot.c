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
 * GRAIN, DRUM, ANALOG, and since 0.18 PHASE, LOFI, VOICE, TRIO, NOISE (retired placeholders keep the engine numbers,
 * engines.c); Felucca's icon atlas and keycaps are not
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
#if BOARD_SMK37
#define FELUCCA_UART 0           /* SMK-37: the TRS jack is a MIDI OUT (UART1 TX, PC7), there is no MIDI IN; PH8 is a
                                  * key-matrix row there and must never become UART RX */
#else
#define FELUCCA_UART 1           /* TRS MIDI IN on UART1 / PH8 */
#endif
#endif
#if BOARD_SMK37 && FELUCCA_UART
#error "BOARD_SMK37: midi_uart.c is the FM-1 MIDI IN driver (RX on PH8, a key-matrix row on the SMK)"
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
#if BOARD_SMK37 && (FELUCCA_SAMPLE || FELUCCA_SLICE)
#error "BOARD_SMK37: no user sample flash (eng_sample.c's slots at 0xA0000.. are the SMK's store, storage.c)"
#endif
#ifndef FELUCCA_GRAIN
#define FELUCCA_GRAIN 1          /* the GRAIN engine (engine 8), 0.22: grains over material it renders itself from four
                                  * knobs (eng_grain.c, grain_src.c, docs/GRAIN.md); two 32 KB buffers of POOL. Retired
                                  * with SAMPLE from 2026-10-07 until then */
#endif
#ifndef GRAIN_ASYNC
#define GRAIN_ASYNC 1            /* GRAIN renders its buffers in slices from main.c's idle loop (grain_poll) */
#endif
#ifndef FELUCCA_DRUM
#define FELUCCA_DRUM 0           /* no DRUM engine (engine 10: a retired slot; frees its kit's POOL) */
#endif
#ifndef FELUCCA_ANALOG
#define FELUCCA_ANALOG 0         /* no ANALOG engine (engine 0: a retired slot; the VA replaces it, its records load there) */
#endif
#ifndef FELUCCA_PHASE
#define FELUCCA_PHASE 0          /* no PHASE, LOFI, VOICE, TRIO, NOISE engines since 0.18 (engines 2, 3, 5, 6, 11: retired
                                  * slots; a record of one loads as the VA's init sound, cr_ui.c cu_load_user) */
#endif
#ifndef FELUCCA_LOFI
#define FELUCCA_LOFI 0
#endif
#ifndef FELUCCA_VOICE
#define FELUCCA_VOICE 0
#endif
#ifndef FELUCCA_TRIO
#define FELUCCA_TRIO 0
#endif
#ifndef FELUCCA_NOISE
#define FELUCCA_NOISE 0
#endif
#ifndef FELUCCA_PHYS
#define FELUCCA_PHYS 0           /* no PHYS engine since 0.18 (engine 9: a retired slot, as the five above; the engine state
                                  * of a part is FM6's size without its three string slots, engines.c eng_state) */
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
#ifndef FELUCCA_PROPHET
#define FELUCCA_PROPHET 1        /* P5: Melodee's Prophet-5 engine (eng_prophet_test.c, engine 16; docs/PROPHET.md). 0: the image
                                  * without it, byte for byte as before it came. FELUCCA_P5_FACTORY (default 1 there):
                                  * Sequential's factory programs of the shipped list in its PRESETS; 0: INIT alone */
#endif
#ifndef FM6_POLY
#define FM6_POLY 8               /* FM6's voices per part (eng_fm6.c; Melodee plays Dexed's 16): ChoralRoot's chords use <= 7,
                                  * and at 16 the release tails of perf.sh (c) / (e) cost up to 59 / 62 % of a block
                                  * (8: 40 / 43 %; docs/FM6.md, Polyphony). Still one budget unit a voice */
#endif
#ifndef CR_FX
#define CR_FX 1                  /* the delay and the reverb are ChoralRoot's (cr_fx.c, docs/FX.md: stereo, the delay at 44.1 kHz
                                  * and the reverb at half of it, their parameters global); Felucca's mono delay and ROOM / SPRING are not in the image */
#endif
#ifndef FELUCCA_PERFORM
#define FELUCCA_PERFORM 0        /* no FX hold layer (perform.c: REPEAT, REVERSE, TAPE STOP ... on the master): its keys and
                                  * knobs are Felucca's sequencer's and UI's, which this unit has not (FELUCCA_SEQ 0); the
                                  * per-block calls and its 5 KB leave the audio ISR (fx.c) */
#endif
#ifndef CR_CPU1
#define CR_CPU1 1                /* the bass part's render on the second core (cr_cpu1.h, hal/fm1_cpu1.h, docs/DUALCORE.md):
                                  * started at power-on; if it does not answer, single core with the shared budget */
#endif
#ifndef CV_ROWS
#define CV_ROWS 120u             /* gfx.c's canvas: cr_draw.c's strip and its two DMA buffers, 3 x 40 rows, are all this
                                  * unit draws on it (Felucca's graph strip took 124: 1920 B of POOL) */
#endif
#ifndef FELUCCA_ICONS
#define FELUCCA_ICONS 0          /* no Felucca icon atlas (icons.c, ui_icons.h): cr_draw.c draws its header glyphs */
#endif
#ifndef FELUCCA_KEYCAPS
#define FELUCCA_KEYCAPS 0        /* no Felucca keycaps (gfx.c's key hints, ui_keycaps.h): no cr_*.c draws one */
#endif
#ifndef FELUCCA_ID
#if BOARD_SMK37
#define FELUCCA_ID "SMK-37 Pro_920"   /* package identity (build.py: the .fwsc marker string) */
#else
#define FELUCCA_ID "FM-1_920"    /* package identity (build.py: the .fwsc marker string) */
#endif
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "ChoralRoot 0.27"   /* the dev build reads as the next tag; --release X.Y sets it */
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
#if BOARD_SMK37
#include "smk_input.h"           /* SMK-37: keys, buttons, encoders, LEDs, pads, faders, wheels, battery */
#else
/* the key sensing's hooks into the scan (cr_sense.c, below): velocity from the analog rows (docs/VELOCITY.md) */
static void cr_sense_hook(uint32_t col, uint32_t rowbit, uint32_t v, uint32_t driven);
static int cr_sense_drive_hook(uint32_t col, uint32_t rowbit);
static int cr_sense_led_ok_hook(uint32_t col, uint32_t rowbit);
static void cr_sense_probe_hook(uint32_t col, uint32_t v, uint32_t pressed);
#define FM1_INPUT_PROBE(c, v, p) cr_sense_probe_hook(c, v, p)
#define FM1_INPUT_ANALOG(c, r, v, d) cr_sense_hook(c, r, v, d)
#define FM1_INPUT_DRIVE(c, r) cr_sense_drive_hook(c, r)
#define FM1_INPUT_LED_OK(c, r) cr_sense_led_ok_hook(c, r)
#include "fm1_input.h"
#endif
#include "fm1_timer.h"
#include "fm1_audio.h"
#if !BOARD_SMK37
#include "fm1_adc.h"
#endif
#include "fm1_lcd_hw.h"
#if CR_CPU1
#include "fm1_cpu1.h"            /* the second core (docs/DUALCORE.md) */
#endif
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
#if !BOARD_SMK37                 /* (SMK-37: the keys' two contacts give the velocity, hal/smk_input.h) */
#include "cr_sense.c"             /* the keys' velocity and pressure from their contact resistance (Velo's sensing) */
#endif
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
    CR_PROF_START();             /* the ISR's stages (cr_cpu1.h): the tick from here to fx.c's parts */
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
    CR_PROF(CRP_MASTER);         /* the master, the USB tap, the click */
}
#include "audio.c"

/* -------------------------------------------------------------- UI --- */
#include "panel.c"
#if FELUCCA_ICONS
#include "icons.c"               /* (1: Felucca's parameter icons; nothing of ChoralRoot's UI calls them) */
#endif
#include "cr_gfx.c"              /* scalable text + shapes on gfx.c (build/gen/cr_fonts.h) */
#include "cr_layout.c"            /* the compiled layouts (build/gen/cr_layouts.h): P5's panel */
#include "cr_draw.c"             /* the screens (cr_screen.h) */
#include "cr_anim.c"
#if FELUCCA_FLASH                /* storage before the sounds: upreset.c reads / writes its banks */
#define FL_UID 1                 /* fm1_flash.h fl_uid_ram: the flash's unique ID, the unit's ID (cr_shim.c persist_boot) */
#include "fm1_flash.h"
static int cr_erase_near(void);  /* cr_ui.c: a voice is active or an input came within 2 s (78 PEAK erases_near) */
#define ST_NEAR_NOW() cr_erase_near()
#define ST_AHEAD 1               /* storage.c: the settings', the session's and the favourites' spare copies erased ahead */
#include "storage_hw.c"
#include "storage.c"
#endif
#include "cr_favs.c"             /* the favourites: a mark on a sound (before the sounds: upreset.c tells it of a slot) */
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
#if CR_RAM_ANY                    /* a CR_RAM_* measurement build (cr_ram.h): the veneers of the calls across */
#include "cr_ram.c"
#endif
