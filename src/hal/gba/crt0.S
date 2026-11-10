@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Copyright (C) 2025 Blutmond contributors
@
@ Start code for cartridge and multiboot images (BM_GBA_MULTIBOOT defined). The header
@ is filled in by tools/gbafix.py (title, checksum; the logo only if one is supplied).

    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       start_rom

    @ 0x04: logo (156 bytes), 0xA0: title, game code, maker, fixed value, ...
    .fill   156, 1, 0
    .fill   12, 1, 0            @ title
    .ascii  "BMND"              @ game code
    .ascii  "00"                @ maker code
    .byte   0x96                @ fixed value
    .byte   0                   @ main unit code
    .byte   0                   @ device type
    .fill   7, 1, 0             @ reserved
    .byte   0                   @ software version
    .byte   0                   @ complement check, set by gbafix
    .fill   2, 1, 0             @ reserved

#ifdef BM_GBA_MULTIBOOT
    @ 0xC0: multiboot entry point. Only multiboot images have it; emulators use the
    @ branch here to tell multiboot images from cartridges.
    b       start_rom
    .byte   0                   @ boot mode, written by the BIOS
    .byte   0                   @ slave ID, written by the BIOS
    .fill   26, 1, 0
    @ 0xE0: joybus entry point
    b       start_rom
    @ Keeps start_rom at 0xE8. With start_rom at 0xE4 the branch at 0xC0 would have the
    @ offset that emulators treat as an old toolchain's loop and stop detecting multiboot.
    .word   0
#endif

start_rom:
    @ IRQ stack, then system mode with the user stack.
    mov     r0, #0x12
    msr     cpsr_c, r0
    ldr     sp, =__sp_irq
    mov     r0, #0x1F
    msr     cpsr_c, r0
    ldr     sp, =__sp_usr

    @ Copy initialised sections from their load address.
    ldr     r0, =__iwram_lma
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
    bl      copy_words
    ldr     r0, =__ewram_lma
    ldr     r1, =__ewram_start
    ldr     r2, =__ewram_end
    bl      copy_words

    @ Clear .bss (IWRAM) and .sbss (EWRAM).
    mov     r0, #0
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
    bl      fill_words
    mov     r0, #0
    ldr     r1, =__sbss_start
    ldr     r2, =__sbss_end
    bl      fill_words

    ldr     r3, =main
    mov     lr, pc
    bx      r3
hang:
    b       hang

@ r0 = source, r1 = destination, r2 = destination end
copy_words:
    cmp     r1, r2
    bxhs    lr
    cmp     r0, r1
    bxeq    lr
1:  ldr     r3, [r0], #4
    str     r3, [r1], #4
    cmp     r1, r2
    blo     1b
    bx      lr

@ r0 = value, r1 = destination, r2 = destination end
fill_words:
    cmp     r1, r2
    strlo   r0, [r1], #4
    blo     fill_words
    bx      lr

    .pool
