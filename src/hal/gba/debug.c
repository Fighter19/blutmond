/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Breakpoints, registers and memory for the debugger (hal.h). The landing places and the
// saving and loading of registers are in debug_trap.S.

#include <string.h>

#include "hal.h"
#include "hal_gba_regs.h"

BmHalDebugRegs bm_debug_regs;
extern const uint32_t bm_debug_landings[];   // 3 instructions per slot

typedef struct Break
{
  uint32_t address;    // 0: free
  uint32_t original;
} Break;
static Break s_breaks[BM_HAL_DEBUG_SLOTS];
static BmHalDebugHandler s_handler;
static void *s_context;

static bool inRam(uint32_t address, uint32_t size)
{
  uint32_t end = address + size;
  if (end < address) return false;
  return (address >= 0x02000000u && end <= 0x02040000u) || (address >= 0x03000000u && end <= 0x03008000u);
}

static bool readable(uint32_t address, uint32_t size)
{
  uint32_t end = address + size;
  if (end < address) return false;
  return inRam(address, size) || (address >= 0x04000000u && end <= 0x04000400u) || (address >= 0x05000000u && end <= 0x05000400u) ||
         (address >= 0x06000000u && end <= 0x06018000u) || (address >= 0x07000000u && end <= 0x07000400u) ||
         (address >= 0x08000000u && end <= 0x0A000000u);
}

bool bmHalDebugInit(BmHalDebugHandler handler, void *pContext)
{
  s_handler = handler;
  s_context = pContext;
  return true;
}

bool bmHalDebugSetBreak(uint32_t slot, uint32_t address)
{
  if (slot >= BM_HAL_DEBUG_SLOTS || (address & 3) != 0 || !inRam(address, 4)) return false;
  bmHalDebugClearBreak(slot);
  uint32_t landing = (uint32_t)(uintptr_t)bm_debug_landings + slot * 12;
  volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)address;
  s_breaks[slot].address = address;
  s_breaks[slot].original = *p;
  // B landing: the distance counts from two instructions on, in words, 24 bits.
  *p = 0xEA000000u | (((landing - (address + 8)) >> 2) & 0x00FFFFFFu);
  return true;
}

void bmHalDebugClearBreak(uint32_t slot)
{
  if (slot >= BM_HAL_DEBUG_SLOTS || s_breaks[slot].address == 0) return;
  *(volatile uint32_t *)(uintptr_t)s_breaks[slot].address = s_breaks[slot].original;
  s_breaks[slot].address = 0;
}

void bmHalDebugClearAll(void)
{
  for (uint32_t i = 0; i < BM_HAL_DEBUG_SLOTS; i++) bmHalDebugClearBreak(i);
}

bool bmHalDebugRead(uint32_t address, void *pOut, uint32_t size)
{
  if (!readable(address, size)) return false;
  const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)address;
  for (uint32_t i = 0; i < size; i++) ((uint8_t *)pOut)[i] = p[i];
  return true;
}

bool bmHalDebugWrite(uint32_t address, const void *pIn, uint32_t size)
{
  if (!inRam(address, size)) return false;
  volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)address;
  for (uint32_t i = 0; i < size; i++) p[i] = ((const uint8_t *)pIn)[i];
  return true;
}

// Called from debug_trap.S with the registers saved.
void bm_hal_debug_stopped(int slot)
{
  if (slot >= 0 && slot < BM_HAL_DEBUG_SLOTS) bm_debug_regs.r[15] = s_breaks[slot].address;
  // Sound off while stopped: nothing refills the buffers.
  uint16_t sound = GBA_REG_SOUNDCNT_X;
  GBA_REG_SOUNDCNT_X = 0;
  if (s_handler != NULL) s_handler(s_context, &bm_debug_regs, slot);
  GBA_REG_SOUNDCNT_X = (uint16_t)(sound & 0x80);
}
