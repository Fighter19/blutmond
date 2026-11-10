@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Copyright (C) 2025 Blutmond contributors
@
@ Interrupt handler, called by the BIOS (IRQ mode, ARM). Acknowledges the interrupts in
@ IF and in the BIOS's copy (for VBlankIntrWait), counts VBlanks and runs the link handler.

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
    tst     r1, #0x80               @ serial?
    bxeq    lr
    @ The link handler is C: run it in system mode on the user stack (the IRQ stack is
    @ small). Interrupts stay off; the BIOS saved r0-r3, r12 and lr.
    mrs     r3, spsr
    stmfd   sp!, {r3, lr}
    mov     r3, #0x9F               @ system mode, IRQ disabled
    msr     cpsr_c, r3
    stmfd   sp!, {r2, lr}           @ keeps the stack 8-byte aligned
    ldr     r3, =bm_hal_serial_irq
    mov     lr, pc
    bx      r3
    ldmfd   sp!, {r2, lr}
    mov     r3, #0x92               @ back to IRQ mode, IRQ disabled
    msr     cpsr_c, r3
    ldmfd   sp!, {r3, lr}
    msr     spsr_cxsf, r3
    bx      lr
    .pool

    .section .bss
    .align 2
    .global bm_vblank_count
bm_vblank_count:
    .word   0
