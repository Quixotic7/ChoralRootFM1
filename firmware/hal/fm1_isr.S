/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* IRQ entry wrappers: save what a C call may
 * clobber, call the plain C body, restore, csync, rti. Our clang never emits
 * the csync stock ISRs have before rti, so ISRs are not written in C directly. */
	.macro FM1_ISR name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	call \body
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR isr_timer5, fm1_timer5_irq
	FM1_ISR isr_alnk0, fm1_alnk0_irq
