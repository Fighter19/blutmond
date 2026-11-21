/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Single steps are worked out on the PC (tools/bmlink/gdb_bridge.h): for each kind of ARM
// instruction that can change where the program goes, the address it continues at.

#include <string.h>

#include "gdb_bridge.h"
#include "test.h"

#define PC 0x03001000u

static uint32_t s_insn;
static uint32_t s_memory[8];          // at 0x03007F00
#define STACK 0x03007F00u

static bool read(void *pContext, uint32_t address, uint32_t *pWord)
{
  (void)pContext;
  if (address == PC) { *pWord = s_insn; return true; }
  if (address >= STACK && address < STACK + sizeof(s_memory)) { *pWord = s_memory[(address - STACK) / 4]; return true; }
  return false;
}

// Flags: N 8, Z 4, C 2, V 1.
static uint32_t next(uint32_t insn, const uint32_t *pRegs, uint32_t flags)
{
  uint32_t regs[17], result = 0;
  memcpy(regs, pRegs, 15 * sizeof(uint32_t));
  regs[15] = PC;
  regs[16] = (flags << 28) | 0x1F;
  s_insn = insn;
  return bmGdbNextArm(regs, read, NULL, &result) ? result : 0xFFFFFFFFu;
}

static void testWhereInstructionsGo(void)
{
  uint32_t r[15] = { 0 };
  r[0] = 3; r[1] = 0x03002000; r[2] = 0x08000101; r[13] = STACK + 8; r[14] = 0x03003000;
  s_memory[0] = 0x11111111; s_memory[1] = 0x03004000; s_memory[2] = 0x03005000; s_memory[3] = 0x03006003; s_memory[4] = 0x03007000;

  TEST_ASSERT(next(0xE1A00000, r, 0) == PC + 4);                    // mov r0, r0
  TEST_ASSERT(next(0xEA000010, r, 0) == PC + 8 + 0x40);             // b forward
  TEST_ASSERT(next(0xEAFFFFFE, r, 0) == PC);                        // b to itself
  TEST_ASSERT(next(0xEB000001, r, 0) == PC + 12);                   // bl
  TEST_ASSERT(next(0x0A000010, r, 4) == PC + 8 + 0x40);             // beq, Z set
  TEST_ASSERT(next(0x0A000010, r, 0) == PC + 4);                    // beq, Z clear
  TEST_ASSERT(next(0xBA000010, r, 8) == PC + 8 + 0x40);             // blt: N != V
  TEST_ASSERT(next(0xBA000010, r, 9) == PC + 4);
  TEST_ASSERT(next(0xCA000010, r, 0) == PC + 8 + 0x40);             // bgt: !Z and N == V
  TEST_ASSERT(next(0x8A000010, r, 2) == PC + 8 + 0x40);             // bhi: C and !Z
  TEST_ASSERT(next(0x8A000010, r, 6) == PC + 4);
  TEST_ASSERT(next(0xE12FFF1E, r, 0) == 0x03003000);                // bx lr
  TEST_ASSERT(next(0xE12FFF12, r, 0) == 0x08000101);                // bx r2: into Thumb code (bit 0)
  TEST_ASSERT(next(0x112FFF1E, r, 4) == PC + 4);                    // bxne lr, Z set
  TEST_ASSERT(next(0xE1A0F00E, r, 0) == 0x03003000);                // mov pc, lr
  TEST_ASSERT(next(0xE1A0F001, r, 0) == 0x03002000);                // mov pc, r1
  TEST_ASSERT(next(0xE08FF100, r, 0) == PC + 8 + 12);               // add pc, pc, r0, lsl #2 (a jump table)
  TEST_ASSERT(next(0xE281F008, r, 0) == 0x03002008);                // add pc, r1, #8
  TEST_ASSERT(next(0xE241F010, r, 0) == 0x03001FF0);                // sub pc, r1, #16
  TEST_ASSERT(next(0xE49DF004, r, 0) == 0x03005000);                // ldr pc, [sp], #4 (pop)
  TEST_ASSERT(next(0xE59DF004, r, 0) == 0x03006000);                // ldr pc, [sp, #4]
  TEST_ASSERT(next(0xE51DF004, r, 0) == 0x03004000);                // ldr pc, [sp, #-4]
  TEST_ASSERT(next(0xE8BD8000, r, 0) == 0x03005000);                // ldmfd sp!, {pc}
  TEST_ASSERT(next(0xE8BD8010, r, 0) == 0x03006000);                // ldmfd sp!, {r4, pc}
  TEST_ASSERT(next(0xE8BD9FF0, r, 0) == 0xFFFFFFFFu);               // ... {r4-r12, pc}: beyond what can be read
  TEST_ASSERT(next(0xE91D8000, r, 0) == 0x03004000);                // ldmdb sp, {pc}
  TEST_ASSERT(next(0xE8BD0010, r, 0) == PC + 4);                    // ldmfd sp!, {r4}: pc not in the list
  TEST_ASSERT(next(0xE15F0000, r, 0) == PC + 4);                    // cmp pc, r0 writes nothing
  TEST_ASSERT(next(0xE0000090, r, 0) == PC + 4);                    // mul r0, r0, r0 is not data processing
  TEST_ASSERT(next(0xE59F0000, r, 0) == PC + 4);                    // ldr r0, [pc]
}

int main(void)
{
  RUN_TEST(testWhereInstructionsGo);
  return TEST_RESULT();
}
