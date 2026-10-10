/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the instrument's UI (cr_ui.c), as the main loop and the emulator call it.
 * Felucca style: one compilation unit, so these are the functions cr_ui.c defines (static), listed for the reader
 * and for the files included before it (choralroot.c's main.c shim, tools/emu/emu_fw.c).
 *
 *   cr_ui_init()       power-on: the engine (cr_out_init), the parts' sounds, the settings mirror, the LED map
 *   cr_ui_input()      one input scan (keys, buttons, knobs -> the grammar -> cr_post events); every frame and
 *                      while the main loop waits (Felucca's ui_input cadence)
 *   cr_ui_frame()      one UI frame after the scan: engine snapshot (IRQ off), the LEDs (fm1_led / fm1_led_dim)
 *   cr_ui_draw()       build the cr_screen_t (cr_build_screen) and draw it (cr_draw) with the animation clock
 *
 * Timing: fm1_ms (the 1 ms TIMER5 tick). Nothing here calls a note path of the engine: events go through
 * cr_out.c's queue to the audio ISR. */
#pragma once

#define CR_VERSION "ChoralRoot 0.1"
#define CR_POPUP_MS 900u          /* a knob's meter stays this long after the last turn (INTEGRATION section 5) */
#define CR_PANIC_MS 1100u         /* the red PANIC screen */
#define CR_MSG_MS 1200u           /* a message box */
