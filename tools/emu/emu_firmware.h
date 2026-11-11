/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the emulator runs: the ONE include list (included once, by emu_fw.c).
 * The sound side through tests/hostsim.c (engines, voices, FX, usb.c MIDI queues; HOST_CR_OUT: without hostsim's
 * stand-ins and helpers, cr_out.c follows), then the emulator's HAL (emu_hal_fw.h), the audio ISR (audio.c),
 * then the instrument's UI.
 * The instrument is ChoralRoot (docs/INTEGRATION.md): its engine (cr_engine.c) ticks in the audio ISR through
 * the mix_block shim below (cr_out.c cr_audio_block, then fx.c's mix), its UI (cr_ui.c) draws cr_draw.c's
 * screens. As choralroot.c: cr_out.c's events_block and midi_event (Felucca's sequencer and retired engines left the
 * tree in 3y232: core.h's defaults are ChoralRoot's), no Felucca keycaps. The hook bodies are in emu_fw.c. */

/* ------------------------------------------------ sound, MIDI, sequencer --- */
#include <stddef.h>
#include <stdint.h>
#define main hostsim_main
#define HOST_CR_OUT 1                                 /* (cr_out.c follows: its events_block, midi_event) */
#ifndef FELUCCA_VA
#define FELUCCA_VA 1                                  /* as choralroot.c: the VA engine, its patch store */
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 1                                  /* as choralroot.c: the CZ-1 engine (14), its tone store and banks */
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1                                /* as choralroot.c: the QUAD engine (15), its patch store */
#endif
#ifndef FELUCCA_PROPHET
#define FELUCCA_PROPHET 1                             /* as choralroot.c: Melodee's Prophet-5 engine (16) */
#endif
#ifndef FELUCCA_GRAIN
#define FELUCCA_GRAIN 1                               /* as choralroot.c: GRAIN (8) over its own material (0.22) */
#endif
#ifndef GRAIN_ASYNC
#define GRAIN_ASYNC 1                                 /* as choralroot.c: its renders from the idle hook (emu_fw_idle) */
#endif
#ifndef FELUCCA_SLICER
#define FELUCCA_SLICER 1                              /* (the emulator has the SLICER; the device unit has it off) */
#endif
#ifndef CR_FX
#define CR_FX 1                                       /* as choralroot.c: ChoralRoot's delay and reverb (cr_fx.c) */
#endif
#ifndef CR_CHORUS_MONO
#define CR_CHORUS_MONO 2                              /* fx.c: the unit's stereo chorus, and 0.18's mono return when
                                                         * asked (EMU_CHORUS=mono: emu_fw.c, tools/emu/classic_exact.sh) */
#endif
#ifndef CR_CPU1
#define CR_CPU1 1                                     /* as choralroot.c: the bass part as the second core's job; here the
                                                         * stub core (firmware/src/cr_cpu1.h) runs it when handed */
#endif
#ifndef CR_DRUMS
#define CR_DRUMS 1                                    /* as choralroot.c: the drums (cr_drum.c, through hostsim.c) */
#endif
static void fm1_irq_off(void);                        /* (emu_hal_fw.h's; cr_drum.c's copies take them) */
static void fm1_irq_on(void);
#define mix_block fx_mix_block                        /* fx.c's mix; audio.c gets ChoralRoot's below */
#ifndef FM6_POLY
#define FM6_POLY 8                                    /* as choralroot.c: FM6 capped at 8 voices (docs/FM6.md) */
#endif
#if EMU_SMK37
#define fm1_in hostsim_fm1_in                         /* (hostsim.c's two-field stand-in; the SMK's is smk_input.h's) */
#endif
#include "../../tests/hostsim.c"
#if EMU_SMK37
#undef fm1_in
#endif
#undef mix_block
#undef main

/* ------------------------------------------------------- the emulator HAL --- */
#include "emu_hal_fw.h"
/* --------------------------------------------- the ChoralRoot engine --- */
/* cr_engine.h's cr_param_t (the perform parameter enum) and cr_screen.h's (a params column) share a name: the
 * engine's is renamed in this unit (cr_out.c / cr_ui.c call it cr_eparam_t) */
#define cr_param_t cr_eparam_t
/* the key sensing (firmware/src/cr_sense.c, docs/VELOCITY.md), as the unit has it, fed by emu_fw.c from the script's
 * velocities and leans instead of the scan's hooks; the scan's analog state it would switch is a stub here */
#if !EMU_SMK37                                        /* (the SMK-37: the keys' two contacts, emu_smk_hal.h) */
#define CR_SENSE_NO_HOOKS 1
#include "../../firmware/src/cr_sense.c"
static struct { uint8_t drive, probe; } fm1_an;
#endif
#include "../../firmware/src/cr_engine.h"
#include "../../firmware/src/cr_engine.c"
#undef cr_param_t
#include "../../firmware/src/cr_loop.h"
#include "../../firmware/src/cr_loop.c"               /* the looper (ticked with the engine) */
#include "../../firmware/src/cr_out.c"                /* streams -> parts 0 / 1 and MIDI; the engine's clock */
static void mix_block(int32_t *out, uint32_t n)        /* the audio ISR's block: the engine first, then the mix */
{
    CR_PROF_START();             /* the ISR's stages (cr_cpu1.h): the tick from here to fx.c's parts */
    cr_audio_block(n);
    if (crl.dstep) {                                   /* the drums' step (as choralroot.c's) */
        crd_beat_step(crl.dstep - 1u);
        crl.dstep = 0;
    } else if (!crl.drum_run) {
        crd_now = 0;
    }
    fx_mix_block(out, n);
    cr_click_mix(out, n);                              /* the metronome / count-in click */
    CR_PROF(CRP_MASTER);         /* the master, the USB tap, the click */
}
/* A UNIT THAT DOES NOT KEEP UP, MODELLED (docs/PROPHET.md "The crackle"; README "Overload"): the host renders a half
 * in microseconds, so audio.c's shed never runs here. With one of these set (emu_fw.c reads the environment) the time
 * shed_check is given for a half is 90 % of the budget whenever the model says the unit would be over:
 *   EMU_SHED_OVER=N    a half that ends with N or more voices sounding (all parts; a fading one is not counted)
 *   EMU_SHED_AT=A:B    every half between A and B ms of audio since power-on
 * and the half's own time otherwise. What is modelled is the shed's DECISION (two such halves in a row fade a voice,
 * one more each half while it lasts); nothing here makes a half late or stale */
static uint32_t emu_shed_over, emu_shed_at0, emu_shed_at1, emu_shed_halves;
static uint32_t emu_shed_model(uint32_t all)
{
    uint32_t p, i, n = 0, ms = (uint32_t)((uint64_t)emu_shed_halves++ * HALF_FRAMES * 1000u / FS);
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    if ((emu_shed_over && n >= emu_shed_over) || (emu_shed_at1 && ms >= emu_shed_at0 && ms < emu_shed_at1))
        return (HALF_FRAMES * 1000000u / FS) * 90u / 100u * FM1_TICKS_PER_US;
    return all;
}
#define CR_SHED_MODEL(all) (emu_shed_over || emu_shed_at1 ? emu_shed_model(all) : (all))
#include "../../firmware/src/audio.c"                 /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define FELUCCA_FLASH 1                               /* the file-backed NOR (emu_hal_fw.h, README): settings,
                                                         * user sounds, loops persist (--flash) */
#ifndef FELUCCA_KEYCAPS
#define FELUCCA_KEYCAPS 0                             /* as choralroot.c: no Felucca keycaps (gfx.c) */
#endif
#ifndef CV_ROWS
#define CV_ROWS 120u                                  /* as choralroot.c: gfx.c's canvas is cr_draw.c's three strips */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "EMU"                         /* the host tests that build this unit (tests/cr_backup_test.c
                                                         * reads it back from INFO). The emulators pass the firmware's:
                                                         * tools/emu/build.sh, web/build_web.sh (test_cr.sh checks) */
#endif
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/cr_gfx.c"                /* (includes build/gen/cr_fonts.h) */
#include "../../firmware/src/cr_layout.c"             /* the compiled layouts (build/gen/cr_layouts.h) */
#include "../../firmware/src/cr_draw.c"               /* (includes cr_screen.h) */
#include "../../firmware/src/cr_anim.c"
#define ST_AHEAD 1                                    /* (choralroot.c's: the erase ahead) */
#include "../../firmware/src/storage.c"               /* (before the sounds: upreset.c's banks, the loop slots) */
#define CR_TRACE 1                                    /* cr_ui.c: "param: / page: / save: .." lines in the logs */
#include "../../firmware/src/cr_favs.c"               /* the favourites (before the sounds) */
#include "../../firmware/src/cr_bank.c"               /* upreset.c (the flash file) and the factory bank */
#include "../../firmware/src/cr_pages.c"
#include "../../firmware/src/cr_name.c"
#define CR_HAVE_SETTINGS 1                            /* cr_ui_init loads the settings record */
/* Options > Flash Data's reboot (SAFE MODE): the emulator says so and ends (a headless script checks the flash file
 * with a second run, tools/emu/test_cr.sh) */
#define CR_REBOOT() do { bootguard_settled(&bootguard); printf("reboot: guard failed %u pending %u\n", (unsigned)bootguard.failed, \
                                (unsigned)bootguard.pending); fflush(stdout); exit(0); } while (0)
/* GEEK OUT 2 (cr_ui.c cu_geek2). RAM and POOL: the device's link map (app.ld); the host has none: "n/a", in the grey.
 * CORE 0: audio.c's measure is the host's wall time here (a fast host: about 0 %); with EMU_STAGES=1 the ISR's stages
 * in device us instead (emu_fw.c: host instructions / 259), core 1's job left out, the wait in (cu_geek2 takes it
 * off); the peak the stages' peaks summed (an upper bound) */
#define CR_MEM_USED(r, p) ((r) = (p) = 0xFFFFFFFFu)
#define CR_CORE0_US(avg, max) emu_core0_us(&(avg), &(max))
static void emu_core0_us(uint32_t *avg, uint32_t *mx)
{
    *avg = cpu_last.avg_us;
    *mx = cpu_last.max_us;
#if CR_CPU1
    if (cr_dbg.prof_on) {
        uint32_t k;
        *avg = *mx = 0;
        for (k = 0; k < CRP_N; k++)
            if (k != CRP_WAIT || cr_dbg.c1 == C1_OK)
                if (k != CRP_PART1 || cr_dbg.c1 != C1_OK) {
                    *avg += cr_dbg.avg_us[k];
                    *mx += cr_dbg.max_us[k];
                }
    }
#endif
}
#include "../../firmware/src/cr_ui.c"
#include "../../firmware/src/cr_settings.c"           /* the settings record (after cr_ui.c and storage.c) */
#include "../../firmware/src/fm6_store.c"             /* FM6: DX7 SysEx (Melodee's; after cr_ui.c: the editor's part) */
#include "../../firmware/src/cz_store.c"              /* CZ-1: Casio tone SysEx (Melodee's; after cr_ui.c) */
/* the update gesture's leaving (cr_backup.c cr_update_poll): the emulator says what the unit would do and ends.
 * EMU_STUB=N: a boot stub of version N ran before this boot (the unit: 3; unset: none, as a firmware started by
 * another loader). The host tests that build this unit define their own hooks before it */
#ifndef CRB_STUB_VER
#define CRB_STUB_VER() ((unsigned)(getenv("EMU_STUB") ? atoi(getenv("EMU_STUB")) : 0))
#define CRB_STUB_GO(w) do { bootguard.pending = 0; /* (as the device's: an intentional restart) */ \
                            printf("restart: stub request %08X (guard pending %u)\n", (unsigned)(w), (unsigned)bootguard.pending); \
                            fflush(stdout); if ((w) == 0x52514552u) exit(0); } while (0)
#define CRB_UPDATE_SCREEN(t) printf("update: screen '%s'\n", (t))
#define CRB_UBOOT_GO() do { bootguard_intentional(&bootguard); printf("restart: ROM USB boot (fm1_enter_uboot; guard pending %u)\n", \
                                   (unsigned)bootguard.pending); fflush(stdout); exit(0); } while (0)
#endif
#include "../../firmware/src/cr_backup.c"             /* backup / restore SysEx (the handler: no transport here;
                                                         * tests/cr_backup_test.c drives it) */
