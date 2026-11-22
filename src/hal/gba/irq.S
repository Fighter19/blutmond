@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Copyright (C) 2025 Blutmond contributors
@
@ Interrupt handler, called by the BIOS (IRQ mode, ARM). Acknowledges the interrupts in
@ IF and in the BIOS's copy (for VBlankIntrWait), counts VBlanks, starts the frame's sound
@ (hal_gba.c) and runs the link handler.

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
    beq     2f
    ldr     r2, =bm_vblank_count
    ldr     r3, [r2]
    add     r3, r3, #1
    str     r3, [r2]
    @ Sound: DMA 1 and 2 start again on the pair handed over, or on silence if none was.
    ldr     r2, =bm_hal_audio_next
    ldr     r3, [r2, #8]            @ on?
    cmp     r3, #0
    beq     2f
    ldmia   r2, {r3, r12}           @ left, right
    cmp     r3, #0
    ldreq   r3, =bm_hal_audio_silence
    moveq   r12, r3
    mov     r2, #0
    strh    r2, [r0, #0xC6]         @ DMA1CNT_H, DMA2CNT_H: off
    strh    r2, [r0, #0xD2]
    ldr     r2, =bm_hal_audio_next
    str     r3, [r0, #0xBC]         @ DMA1SAD
    str     r12, [r0, #0xC8]        @ DMA2SAD
    mov     r3, #0
    str     r3, [r2]                @ taken
    ldr     r3, =0xB640             @ on, at FIFO requests, 32 bits, repeating, fixed destination
    strh    r3, [r0, #0xC6]
    strh    r3, [r0, #0xD2]
2:
    tst     r1, #2                  @ H-blank: the third line table (hal_gba.c)
    beq     1f
    ldrh    r3, [r0, #6]            @ VCOUNT: the line just drawn
    cmp     r3, #160
    bcs     1f                      @ not in the vertical blank
    ldr     r2, =bm_hal_line2
    ldr     r12, [r2]               @ the register
    cmp     r12, #0
    beq     1f
    add     r3, r2, r3, lsl #1
    ldrh    r3, [r3, #10]           @ values[line + 1] (values start at offset 8)
    strh    r3, [r12]
1:
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
