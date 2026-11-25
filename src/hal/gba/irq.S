@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Copyright (C) 2025 Blutmond contributors
@
@ Interrupt handler, called by the BIOS (IRQ mode, ARM). Acknowledges the interrupts in
@ IF and in the BIOS's copy (for VBlankIntrWait), counts VBlanks, restarts the line tables and
@ starts the frame's sound (hal_gba.c), feeds the third line table and runs the link handler.

@ Line table \slot at the vertical blank (bm_hal_lines, hal_gba.c): next becomes shown, its
@ register gets line 0's value, and DMA channel \dma (at I/O offset \cnt: SAD, DAD, CNT_L,
@ CNT_H) copies the next line's at every H-blank. r0: the I/O base; r2, r3, r12 are used.
.macro restart_line_table slot, cnt
    ldr     r2, =bm_hal_lines + \slot * 8
    ldmia   r2, {r3, r12}           @ next: register, values
    add     r2, r2, #24
    stmia   r2, {r3, r12}           @ shown
    .if \cnt
    mov     r2, #0
    strh    r2, [r0, #\cnt + 10]    @ CNT_H: off
    .endif
    cmp     r3, #0
    beq     9f
    ldrh    r2, [r12], #2           @ line 0's value now; the DMA from line 1 on
    strh    r2, [r3]
    .if \cnt
    str     r12, [r0, #\cnt]        @ SAD
    str     r3, [r0, #\cnt + 4]     @ DAD
    mov     r2, #1
    strh    r2, [r0, #\cnt + 8]     @ CNT_L: one halfword
    ldr     r2, =0xA240             @ on, at H-blank, repeating, fixed destination
    strh    r2, [r0, #\cnt + 10]
    .endif
9:
.endm

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
    restart_line_table 0, 0xB0      @ DMA 0
    restart_line_table 1, 0xD4      @ DMA 3
    restart_line_table 2, 0         @ the H-blank interrupt below
#if BM_LIGHT_EXPERIMENTAL
    @ The palette stream (bm_hal_stream, hal_gba.c), from ROM: once a frame, and IWRAM is full.
    stmfd   sp!, {lr}               @ the BIOS's return address
    ldr     r2, =bm_hal_stream_vblank
    mov     lr, pc
    bx      r2
    ldmfd   sp!, {lr}
#endif
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
    ldr     r2, =bm_hal_lines + 40      @ shown[2]
    ldmia   r2, {r2, r12}           @ the register, the values
    cmp     r2, #0
    beq     1f
    add     r3, r12, r3, lsl #1
    ldrh    r3, [r3, #2]            @ values[line + 1]
    strh    r3, [r2]
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

#if BM_LIGHT_EXPERIMENTAL
@ The palette stream at the vertical blank (bm_hal_stream, hal_gba.c): next becomes shown; row 0
@ into palette memory now, DMA channel 3 (off since line table 1 restarted) the next rows at
@ every H-blank. r0: the I/O base, kept; r1 kept.
    .section .text.bm_hal_stream_vblank, "ax"
    .arm
    .align 2
    .global bm_hal_stream_vblank
bm_hal_stream_vblank:
    stmfd   sp!, {r1, r4, r5, r6, lr}
    ldr     r2, =bm_hal_stream
    ldmia   r2, {r3, r4, r5}        @ next: rows, first, count
    add     r6, r2, #12
    stmia   r6, {r3, r4, r5}        @ shown
    cmp     r3, #0
    beq     8f
    mov     r6, #0x05000000
    add     r6, r6, r4, lsl #1      @ palette entry `first`
    mov     r12, r5
7:
    ldrh    r2, [r3], #2            @ row 0
    strh    r2, [r6], #2
    subs    r12, r12, #1
    bne     7b
    sub     r6, r6, r5, lsl #1      @ back to entry `first`; r3 is at row 1
    str     r3, [r0, #0xD4]         @ DMA3SAD
    str     r6, [r0, #0xD8]         @ DMA3DAD
    strh    r5, [r0, #0xDC]         @ DMA3CNT_L: a row
    ldr     r2, =0xA260             @ on, at H-blank, repeating, destination incremented and reloaded
    strh    r2, [r0, #0xDE]
8:
    ldmfd   sp!, {r1, r4, r5, r6, lr}
    bx      lr
    .ltorg
#endif
