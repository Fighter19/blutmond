@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Copyright (C) 2025 Blutmond contributors
@
@ Where the program lands when it reaches a breakpoint (hal.h, bmHalDebug*). The ARM7TDMI has
@ no debug hardware a program can use, so a breakpoint is a branch written over an ARM
@ instruction in RAM. Each breakpoint slot has its own landing place here, which is how the
@ handler knows which one was reached. Everything is saved to bm_debug_regs; when the handler
@ returns, everything is loaded from there again, so the debugger can change registers and
@ the place to go on at.

    .section .iwram.text, "ax"
    .arm
    .align 2

    .global bm_debug_landings
bm_debug_landings:
    .set slot, 0
    .rept 8
    stmfd   sp!, {r0, lr}           @ below sp nothing is safe (the serial interrupt uses this stack)
    mov     r0, #slot
    b       reached
    .set slot, slot + 1
    .endr

reached:
    ldr     lr, =bm_debug_regs
    add     lr, lr, #4
    stmia   lr, {r1-r12}            @ r1..r12
    sub     lr, lr, #4
    mrs     r1, cpsr                @ nothing so far changed the flags
    str     r1, [lr, #64]
    ldmfd   sp!, {r1, r2}           @ the program's r0 and lr
    str     r1, [lr, #0]
    str     r2, [lr, #56]
    str     sp, [lr, #52]           @ sp is the program's again
    bic     sp, sp, #7              @ C wants the stack on 8 bytes
    ldr     r1, =bm_hal_debug_stopped
    mov     lr, pc
    bx      r1
    @ Continue: flags, sp, then the place to go on at pushed and popped (see above), all else
    @ in between.
    ldr     lr, =bm_debug_regs
    ldr     r0, [lr, #64]
    msr     cpsr_f, r0
    ldr     sp, [lr, #52]
    ldr     r0, [lr, #60]
    str     r0, [sp, #-4]!
    ldmia   lr, {r0-r12}
    ldr     lr, [lr, #56]
    ldr     pc, [sp], #4

@ void bmHalDebugBreak(void): stops here, as if a breakpoint had been reached. The registers
@ shown are the caller's; when the program continues it returns to the caller (changes the
@ debugger made to registers are not taken over).
    .global bmHalDebugBreak
bmHalDebugBreak:
    stmfd   sp!, {r4, lr}
    ldr     r4, =bm_debug_regs
    stmia   r4, {r0-r12}
    ldr     r0, [sp]                @ the caller's r4
    str     r0, [r4, #16]
    add     r0, sp, #8
    str     r0, [r4, #52]
    ldr     r0, [sp, #4]
    str     r0, [r4, #56]
    bic     r1, r0, #1
    str     r1, [r4, #60]           @ it goes on at the return address
    mrs     r1, cpsr
    tst     r0, #1
    orrne   r1, r1, #0x20           @ the caller is Thumb code
    str     r1, [r4, #64]
    mvn     r0, #0
    ldr     r1, =bm_hal_debug_stopped
    mov     lr, pc
    bx      r1
    ldmfd   sp!, {r4, lr}
    bx      lr
    .pool
