/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 128 fatal-vector stubs, 6 bytes each: VEC[n] = fm1_fatal_stubs + 6*n.
 * The call leaves rets = stub + 6, so common code recovers n. */
	.section .text.fm1vec,"ax",@progbits
	.globl fm1_fatal_stubs
	.balign 2
fm1_fatal_stubs:
	.rept 128
	[--sp] = {rets}
	call fm1_fatal_common
	.endr

	.section .text.fm1vec_common,"ax",@progbits
	.globl fm1_fatal_common
fm1_fatal_common:
	trigger
	[--sp] = {sp, ssp, usp, icfg, psr, rets, retx, rete, reti}
	[--sp] = {r15-r0}
	r0 = sp				/* frame: r0..r15, reti, rete, retx, rets(stub+6), psr, icfg, usp, ssp, sp, rets(orig) */
	call fm1_fault_c
1:	goto 1b
