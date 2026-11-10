@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Copyright (C) 2025 Blutmond contributors
@
@ Interrupt handler, called by the BIOS (IRQ mode, ARM). Acknowledges the interrupts in
@ IF and in the BIOS's copy (for VBlankIntrWait) and counts VBlanks.

    .section .iwram.text, "ax"
    .arm
    .align 2
    .global bm_irq_handler
bm_irq_handler:
    mov     r0, #0x04000000
    ldr     r1, [r0, #0x200]        @ IE in the low half, IF in the high half
    and     r1, r1, r1, lsr #16     @ pending and enabled
    add     r2, r0, #0x200
    strh    r1, [r2, #2]            @ acknowledge in IF (0x04000202)
    ldrh    r2, [r0, #-8]           @ BIOS interrupt flags (0x03007FF8 through the mirror)
    orr     r2, r2, r1
    strh    r2, [r0, #-8]
    tst     r1, #1                  @ VBlank?
    ldrne   r2, =bm_vblank_count
    ldrne   r3, [r2]
    addne   r3, r3, #1
    strne   r3, [r2]
    bx      lr
    .pool

    .section .bss
    .align 2
    .global bm_vblank_count
bm_vblank_count:
    .word   0
