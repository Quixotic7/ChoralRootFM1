/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the emulator runs: the ONE include list (included once, by emu_fw.c).
 * The sound side through tests/hostsim.c (engines, voices, FX, usb.c MIDI queues; FELUCCA_SEQ 0: without
 * Felucca's sequencer and hostsim's own renders), then the emulator's HAL (emu_hal_fw.h), the audio ISR (audio.c),
 * then the instrument's UI.
 * The instrument is ChoralRoot (docs/INTEGRATION.md): its engine (cr_engine.c) ticks in the audio ISR through
 * the mix_block shim below (cr_out.c cr_audio_block, then fx.c's mix), its UI (cr_ui.c) draws cr_draw.c's
 * screens. As choralroot.c: no seq.c (cr_out.c's events_block and midi_event), no SAMPLE / GRAIN / DRUM engines, no
 * Felucca icons or keycaps. The hook bodies are in emu_fw.c. */

/* ------------------------------------------------ sound, MIDI, sequencer --- */
#include <stddef.h>
#include <stdint.h>
#define main hostsim_main
#ifndef FELUCCA_SEQ
#define FELUCCA_SEQ 0                                 /* as choralroot.c: no Felucca sequencer */
#endif
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0                               /* as choralroot.c: no SLICE engine, so engine numbers match the device */
#endif
#ifndef FELUCCA_VA
#define FELUCCA_VA 1                                  /* as choralroot.c: the VA engine, its patch store */
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 1                                  /* as choralroot.c: the CZ-1 engine (14), its tone store and banks */
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1                                /* as choralroot.c: the QUAD engine (15), its patch store */
#endif
#ifndef FELUCCA_SAMPLE
#define FELUCCA_SAMPLE 0                              /* as choralroot.c: SAMPLE, GRAIN, DRUM retired */
#endif
#ifndef FELUCCA_GRAIN
#define FELUCCA_GRAIN 0
#endif
#ifndef FELUCCA_DRUM
#define FELUCCA_DRUM 0
#endif
#ifndef FELUCCA_ANALOG
#define FELUCCA_ANALOG 0                              /* as choralroot.c: ANALOG retired (engine 0) */
#endif
#ifndef FELUCCA_SLICER
#define FELUCCA_SLICER 1                              /* (as felucca.c; the device unit has it off) */
#endif
#define mix_block fx_mix_block                        /* fx.c's mix; audio.c gets ChoralRoot's below */
#ifndef FM6_POLY
#define FM6_POLY 8                                    /* as choralroot.c: FM6 capped at 8 voices (docs/FM6.md) */
#endif
#include "../../tests/hostsim.c"
#undef mix_block
#undef main

/* ------------------------------------------------------- the emulator HAL --- */
#include "emu_hal_fw.h"
/* --------------------------------------------- the ChoralRoot engine --- */
/* cr_engine.h's cr_param_t (the perform parameter enum) and cr_screen.h's (a params column) share a name: the
 * engine's is renamed in this unit (cr_out.c / cr_ui.c call it cr_eparam_t) */
#define cr_param_t cr_eparam_t
#include "../../firmware/src/cr_engine.h"
#include "../../firmware/src/cr_engine.c"
#undef cr_param_t
#include "../../firmware/src/cr_loop.h"
#include "../../firmware/src/cr_loop.c"               /* the looper (ticked with the engine) */
#include "../../firmware/src/cr_out.c"                /* streams -> parts 0 / 1 and MIDI; the engine's clock */
static void mix_block(int32_t *out, uint32_t n)        /* the audio ISR's block: the engine first, then the mix */
{
    cr_audio_block(n);
    fx_mix_block(out, n);
    cr_click_mix(out, n);                              /* the metronome / count-in click */
}
#include "../../firmware/src/audio.c"                 /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define FELUCCA_FLASH 1                               /* the file-backed NOR (emu_hal_fw.h, README): settings,
                                                         * user sounds, loops persist (--flash) */
#ifndef FELUCCA_KEYCAPS
#define FELUCCA_KEYCAPS 0                             /* as choralroot.c: no Felucca keycaps (gfx.c) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "EMU"
#endif
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/cr_gfx.c"                /* (includes build/gen/cr_fonts.h) */
#include "../../firmware/src/cr_draw.c"               /* (includes cr_screen.h) */
#include "../../firmware/src/cr_anim.c"
#include "../../firmware/src/storage.c"               /* (before the sounds: upreset.c's banks, the loop slots) */
#define CR_TRACE 1                                    /* cr_ui.c: "param: / page: / save: .." lines in the logs */
#include "../../firmware/src/cr_bank.c"               /* upreset.c (the flash file) and the factory bank */
#include "../../firmware/src/cr_pages.c"
#include "../../firmware/src/cr_name.c"
#define CR_HAVE_SETTINGS 1                            /* cr_ui_init loads the settings record */
/* Options > Flash Data's reboot (SAFE MODE): the emulator says so and ends (a headless script checks the flash file
 * with a second run, tools/emu/test_cr.sh) */
#define CR_REBOOT() do { bootguard_settled(&bootguard); printf("reboot: guard failed %u pending %u\n", (unsigned)bootguard.failed, \
                                (unsigned)bootguard.pending); fflush(stdout); exit(0); } while (0)
#include "../../firmware/src/cr_ui.c"
#include "../../firmware/src/cr_settings.c"           /* the settings record (after cr_ui.c and storage.c) */
#include "../../firmware/src/fm6_store.c"             /* FM6: DX7 SysEx (Melodee's; after cr_ui.c: the editor's part) */
#include "../../firmware/src/cz_store.c"              /* CZ-1: Casio tone SysEx (Melodee's; after cr_ui.c) */
#include "../../firmware/src/cr_backup.c"             /* backup / restore SysEx (the handler: no transport here;
                                                         * tests/cr_backup_test.c drives it) */
