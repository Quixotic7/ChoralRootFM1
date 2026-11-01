/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Code in RAM (cr_ram.h, docs/FX.md "Code in RAM"): the veneers. Included by choralroot.c only when a CR_RAM_*
 * switch is 1 (device measurement builds); no other build compiles this file.
 *
 * XIP and RAMTEXT are out of a direct call's reach, so a call between a function a switch moved and one that stays
 * goes through a veneer on the caller's side: cr_v_NAME takes NAME's arguments and calls NAME by its address in a
 * register (hal/fm1_cc.h FM1_FAR). The compiler writes it; nothing here is assembly.
 *
 * The veneer does NOT name its target. It calls `cr_t_NAME`, a symbol nothing defines, and tools/ram_code.py place
 * rewrites two kinds of relocation in the compiled object:
 *   - cr_v_NAME's reference to cr_t_NAME                       -> NAME
 *   - every direct call of NAME from the other side            -> cr_v_NAME
 * That is on purpose. A veneer that named NAME, or a call site that went through a pointer in the source, would make
 * NAME's address escape in the compiler's eyes, and the compiler would stop using what it knows of NAME's callers
 * (clean_block is compiled for n == CTL because every caller passes it; with the address taken it grew the general
 * path back, and xd_run, mod_begin, fm6_control each came out different). With the redirection done on the object,
 * every function is compiled exactly as in a build without the switch: tools/ram_code.py cmp shows it.
 *
 * A veneer lives where its callers do: in flash for a target in RAM, in RAM (.rhot.cr_v_NAME, the same tool) for a
 * target in flash. One is needed for every NAME with a direct caller on the other side; the linker refuses the build
 * if one is missing ("relocation truncated to fit"). Functions reached only through pointers (the engines' render,
 * the mix loops) need none. */
#define CR_VENEER(ret, name, params, args) \
    extern ret cr_t_##name params; \
    __attribute__((noinline, used)) ret cr_v_##name params { return FM1_FAR(cr_t_##name) args; }

#if CR_RAM_FX
/* the audio ISR (flash) -> the chorus and the effects' block */
CR_VENEER(void, xc_block, (const int32_t *in, int32_t *ml, int32_t *mr, uint32_t n), (in, ml, mr, n))
CR_VENEER(void, xf_block, (const int32_t *d, const int32_t *r, int32_t *ml, int32_t *mr, uint32_t n), (d, r, ml, mr, n))
/* xf_block (RAM) -> the coefficients, when a parameter moved (flash: its divides and tables) */
CR_VENEER(void, xf_coefs, (void), ())
#endif
#if CR_RAM_MASTER
/* the audio ISR (flash) -> the output chain of the block */
CR_VENEER(void, master_block, (int32_t *o, uint32_t n), (o, n))
CR_VENEER(void, clean_block, (int32_t *o, uint32_t n), (o, n))
CR_VENEER(void, clean_post17, (int32_t *o, uint32_t n), (o, n))
CR_VENEER(void, clean_split, (int32_t *o, uint32_t n), (o, n))
CR_VENEER(void, usb_fixed_dac, (int32_t *o, uint32_t n), (o, n))
#endif
#if CR_RAM_CORE
/* the audio ISR (flash: mix_block, mix_part, mix_parts_split, inlined there) -> a part's block */
CR_VENEER(int, mod_begin, (track_t *t), (t))
CR_VENEER(void, track_lfo_tick, (track_t *t), (t))
CR_VENEER(uint32_t, track_render_x, (track_t *t, int32_t *out, int32_t *side, uint8_t *side_on, uint32_t n,
                                     const int32_t *lfo_pre), (t, out, side, side_on, n, lfo_pre))
CR_VENEER(void, mix_part_out, (track_t *t, int32_t *b, int32_t *side, uint8_t side_on, uint32_t nr, uint32_t n),
          (t, b, side, side_on, nr, n))
CR_VENEER(void, c1_part1_job, (uint32_t n), (n))
#endif
#if CR_RAM_VA
/* memset (libc.c) goes to RAM with va_render, which clears its 48 bytes of matrix sums with it every voice and
 * block (the compiler's own call: `dosc[3][4] = {{0}}`); the callers that stay in flash (the stores, the settings)
 * reach it through this */
CR_VENEER(void *, memset, (void *d, int c, unsigned n), (d, c, n))
/* va_render (RAM) -> the filter's type weights (va_common.c, flash: GRAIN calls it too, so since 3y118 it is no longer
 * inlined into va_render; once a voice and block, between two types, SPREAD or USPREAD) */
CR_VENEER(va_fw_t, va_fweights, (uint32_t ty, int32_t ff), (ty, ff))
#endif
#if CR_RAM_FM6
/* fm6_block (flash: the at-rest voice's control tick, inlined there) -> fm6_control */
CR_VENEER(void, fm6_control, (track_t *t, voice_t *v, fm6_voice_t *s, const vmod_t *m), (t, v, s, m))
/* fm6_control, fm6_eg_step (RAM) -> an envelope's next segment (flash: note-on and the patch sync use them too) */
CR_VENEER(void, fm6_eg_go, (fm6_eg_t *e, const uint8_t *op, int32_t ol, int32_t rs, uint32_t ix), (e, op, ol, rs, ix))
CR_VENEER(void, fm6_peg_go, (fm6_peg_t *e, uint32_t ix), (e, ix))
/* fm6_block (flash: the AMS shares figured before the hand-over since 3y187, fm6_ams_pt inlined there) -> fm6_mulhi */
CR_VENEER(uint64_t, fm6_mulhi, (uint64_t a, uint64_t b), (a, b))
#endif
#if CR_RAM_NOTE
/* the tick (flash: the audio ISR's note dispatch and engine_block's waiting notes, inlined there; the callbacks of
 * MIDI in) -> a note's start and end */
CR_VENEER(void, trk_note_on, (track_t *t, uint32_t note, uint32_t vel), (t, note, vel))
CR_VENEER(void, trk_note_off, (track_t *t, uint32_t note), (t, note))
/* trk_all_off (flash: a panic, a preset) -> the waiting notes dropped */
CR_VENEER(void, vsq_drop, (track_t *t, uint32_t note), (t, note))
/* shed_voice (flash: the overload's) -> the bass protected */
CR_VENEER(uint32_t, lowest_held, (const track_t *t), (t))
#if FELUCCA_PROPHET
/* P5 once a block (flash: p5_pool_enforce, shed_p5_bass, p5x_run and its counts) -> the pool's own reads */
CR_VENEER(uint32_t, trk_nvoice, (const track_t *t), (t))
CR_VENEER(uint32_t, voice_units, (const track_t *t), (t))
CR_VENEER(uint32_t, p5_sounding, (const track_t *t), (t))
CR_VENEER(uint32_t, p5_pool_used, (void), ())
CR_VENEER(uint32_t, p5_part_victim, (const track_t *t, int last), (t, last))
CR_VENEER(uint32_t, p5_pool_victim, (const track_t *self, int soft, track_t **pp), (self, soft, pp))
CR_VENEER(void, p5_pool_take, (const track_t *self, track_t *vp, uint32_t k), (self, vp, k))
#endif
#endif
