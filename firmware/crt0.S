/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 standalone app entry. The SPL maps app.bin
 * at XIP 0x02000120 and jumps to its first byte with r0 = boot-parameter block
 * (0x01C7FE08, inside the vector table we are about to own). The SPL expects
 * `goto +4; rts` as the first instruction. Before anything else the core state
 * the SPL left is saved for the boot probe; then interrupts off, our stacks. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	r1 = icfg
	r2 = psr
	cli
	r3 = 0x1C07F00		/* STATUS (debug area) */
	[r3 + 0x20] = r1	/* icfg as handed over */
	[r3 + 0x24] = r2	/* psr */
	[r3 + 0x28] = r0	/* boot-parameter pointer */
	r1 = sp
	[r3 + 0x2C] = r1
	r1 = ssp
	[r3 + 0x30] = r1
	sp = _ustack_top
	ssp = _sstack_top
	call fm1_cstart
	goto .
