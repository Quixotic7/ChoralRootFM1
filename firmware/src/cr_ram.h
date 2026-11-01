/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Code in RAM (docs/FX.md "Code in RAM"): build switches that link the audio path's functions at RAMTEXT
 * (0x01C00000, firmware/app.ld .rhot.*) instead of XIP, so that the unit can say what executing from the flash
 * costs each stage. Device builds only (./build.sh takes them from the environment: CR_RAM_FX=1 ./build.sh); a host
 * or emulator build never defines one. All default 0; the P5 engine's sample loop has its own, older switch
 * (P5_RAM, 1 in a device build: eng_prophet_test.c).
 *
 *   CR_RAM_FX      the effects: xf_block and its four loops (the delay, the reverb, the half-band filters), xc_block
 *                  (the chorus), a part's eight mix / send loops (fx.c mix_loop*)                       cr_fx.c, fx.c
 *   CR_RAM_MASTER  the output chains: Clean (clean_block, clean_post17, clean_split, clean_post) and Classic
 *                  (master_block), usb_fixed_dac                                                        fx.c
 *   CR_RAM_CORE    what every part runs every block whatever its engine: track_render_x (the voice loop and the
 *                  envelopes), mix_part_out (drive, level, pan), track_lfo_tick, the matrix (mod_begin, mod_voice,
 *                  mod_voice_add, mod_tsrc, mod_end), c1_part1_job                                      voice.c, fx.c, mod.c
 *   CR_RAM_CZ      CZ-1: cz_native_render and everything it calls                                       cz_native.c, eng_cz.c
 *   CR_RAM_VA      VA: va_render (the oscillators and the filter are inlined in it), va_morph, va_render_mono,
 *                  and memset (libc.c: va_render calls it every voice and block)                        eng_va.c
 *   CR_RAM_FM6     FM6: fm6_render (the operators' kernel is inlined in it), fm6_control, fm6_eg_step, fm6_mulhi
 *   CR_RAM_NOTE    the note path of the tick: trk_note_on (voice_alloc and mod_note inlined in it), voice_start,
 *                  glide_set, mono_play / _pick / _remove, trk_note_off, the budget (trk_nvoice, voice_units,
 *                  voices_busy_of, voice_room, voice_victim, lowest_held), vsq_push / _drop, P5's pool (p5_pool_*
 *                  but _enforce, p5_sounding, p5_part_victim) and FM TONE's quad_note_on: a long release steals
 *                  all 8 voices at every chord change, each note through it twice (3y216)       voice.c, eng_quad.c
 *
 * THE COMPILER IS NOT TOLD. Two things were tried and are not used, because each changed the code it moved:
 *   - a section attribute on the function: this compiler does not inline a function into one that is in another
 *     section unless it is always_inline (it is why P5_HOT_IN and cr_fx.c's XF_AI exist): osc_sine and clamp became
 *     calls, into flash, in the sample loops;
 *   - the calls across written through a pointer in the source: the callee's address then escapes and the compiler
 *     drops what it knew of its callers (cr_ram.c has the cases).
 * So the sources carry only a marker, and the placement is done on the compiled object:
 *
 *   CR_HOT_x       in front of a function's definition, expands to NOTHING in every build. tools/build.py reads the
 *                  markers from these sources: with CR_RAM_x=1 it renames that function's section in the object
 *                  from .text.NAME to .rhot.NAME (tools/ram_code.py place: six bytes of the section-name table, no
 *                  code byte), and app.ld puts .rhot.* in RAMTEXT. The function must be one the compiler emits
 *                  (-ffunction-sections gives it .text.NAME); the build fails if it is not, e.g. because it was
 *                  inlined: what is inlined lives where its caller does.
 *                  `CR_HOT_x MIX_LOOP_FN(name, ..)` names the function that macro defines.
 *   the calls      between a marked function and one that stays (either way) cannot be direct: XIP and RAMTEXT are
 *                  out of a call's reach (hal/fm1_cc.h FM1_FAR), and the linker refuses a call that is missed. Each
 *                  such callee has a veneer in cr_ram.c, and the same tool points the calls at it.
 *
 * The engines' render functions and the mix loops are reached through pointers already (engine_t, fx.c
 * mix_part_out) and need nothing. A build without a switch has no .rhot section, no veneer and an untouched object:
 * it is the build it was before this file.
 * The audio ISR's own body (audio.c fm1_alnk0_irq: 5.6 KB with mix_block, audio_block and the event dispatch inlined)
 * stays in flash under every switch. CR_RAM_VA (since 3y177) and CR_RAM_NOTE (since 3y216) are 1 in a device build
 * (tools/build.py RAM_DEFAULT_ON; =0 for the flash build to measure against). */
#pragma once
#ifndef CR_RAM_FX
#define CR_RAM_FX 0
#endif
#ifndef CR_RAM_MASTER
#define CR_RAM_MASTER 0
#endif
#ifndef CR_RAM_CORE
#define CR_RAM_CORE 0
#endif
#ifndef CR_RAM_CZ
#define CR_RAM_CZ 0
#endif
#ifndef CR_RAM_VA
#define CR_RAM_VA 0
#endif
#ifndef CR_RAM_FM6
#define CR_RAM_FM6 0
#endif
#ifndef CR_RAM_NOTE
#define CR_RAM_NOTE 0
#endif
#define CR_RAM_ANY (CR_RAM_FX || CR_RAM_MASTER || CR_RAM_CORE || CR_RAM_CZ || CR_RAM_VA || CR_RAM_FM6 || CR_RAM_NOTE)
#define CR_HOT_FX                       /* the markers (above): nothing for the compiler */
#define CR_HOT_MASTER
#define CR_HOT_CORE
#define CR_HOT_CZ
#define CR_HOT_VA
#define CR_HOT_FM6
#define CR_HOT_NOTE
