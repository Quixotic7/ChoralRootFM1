/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA memory map.
 * Code + constants execute in place from XIP. app.bin offset 0 = 0x02000120 is the boot stub (firmware/stub,
 * 0xEE0 bytes: flash 0x4120..0x4FFF); the app is linked from the next sector, XIP 0x02001000 = flash 0x5000, up
 * to the commit marker's sector at flash 0x91000 (docs/BOOT-SAFETY.md "As built").
 * 0x01C00000..0x01C06000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver); 0x01C06000..0x01C08000 stays for RAM-run debug.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7C000..0x01C7C01F is the boot stub's (its rescue word and mailbox, firmware/stub/stub.c): NOINIT starts
 * after it.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = 0x8C000
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C020, LENGTH = 0x3D30
    /* the resource region's data (firmware/src/cr_res.h, HISTORY 3y234): flash 0x93040..0x9EFFF, XIP through the
     * plain window; not in app.bin: tools/build.py takes .res alone, tools/fm1pkg_make.py --res puts it after the
     * slot with its header at 0x93000. The SMK-37's (3y295): flash 0x9D040..0xA8FFF, XIP 0x02099040: tools/build.py
     * links BOARD=smk37 with a copy of this file whose RES line has that origin (the length is the same) */
    RES    (r)   : ORIGIN = 0x0208F040, LENGTH = 0xBFC0
    /* the FM-1's second resource region's data (cr_res.h CR_RES2, HISTORY 3y296): flash 0xDC040..0xDFFFF, XIP
     * through the plain window; not in app.bin: tools/build.py takes .res2 alone, tools/fm1pkg_make.py --res2 packs
     * it as the package's res2.bin with its header at 0xDC000. The SMK-37 has none (.res2 empty: tools/build.py) */
    RES2   (r)   : ORIGIN = 0x020D8040, LENGTH = 0x3FC0
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    /* .ram_text*: the flash driver (it runs with the flash off: no call leaves it, tools/build.py checks) and what
     * P5_RAM puts beside it. .rhot.*: the audio functions a CR_RAM_* switch moves (firmware/src/cr_ram.h, docs/FX.md
     * "Code in RAM": tools/build.py renames their .text.NAME sections in the object, so no file has such a section
     * unless a switch is on). They run with the flash on and call into XIP through a register (hal/fm1_cc.h
     * FM1_FAR). Not KEEPed: what nothing refers to leaves, as in .text. _rt_hot: where they start (the check's
     * boundary) */
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_hot = .; *(.rhot.*) . = ALIGN(4); } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    /* the second core's waiting loop (hal/fm1_cpu1.h, docs/DUALCORE.md): right after .ram_text, in RAM and in the
     * image; main.c copies both in one run and the guard's RAM code window (fm1_guard.h) covers both */
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    _rt_end = _c1_end;
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    .res : { _res_start = .; KEEP(*(.res.*)) . = ALIGN(4); _res_end = .; } > RES
    .res2 : { _res2_start = .; KEEP(*(.res2.*)) . = ALIGN(4); _res2_end = .; } > RES2
    /DISCARD/ : { *(.comment) *(.note*) }
}
