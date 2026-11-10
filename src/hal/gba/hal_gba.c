/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "hal.h"
#include "hal_gba_regs.h"

// Provided by the linker script: the parts of EWRAM and IWRAM not used by the program.
extern uint8_t __ewram_heap_start[], __ewram_heap_end[];
extern uint8_t __iwram_heap_start[], __iwram_heap_end[];

static uint32_t s_frame;

const char *bmHalPlatformName(void)
{
  return "gba";
}

void bmHalGetMemoryRanges(BmHalMemoryRange *pRanges, uint32_t count)
{
  const BmHalMemoryRange ranges[3] = {
    { __ewram_heap_start, (size_t)(__ewram_heap_end - __ewram_heap_start) },
    { __iwram_heap_start, (size_t)(__iwram_heap_end - __iwram_heap_start) },
    { (void *)GBA_VRAM, GBA_VRAM_SIZE },
  };
  for (uint32_t i = 0; i < count; i++)
  {
    pRanges[i] = i < 3 ? ranges[i] : (BmHalMemoryRange){ NULL, 0 };
  }
}

void bmHalVideoInit(void)
{
  // Mode 0, no layers enabled, forced blank off: only the backdrop colour is visible.
  GBA_REG_DISPCNT = 0;
}

void bmHalPaletteWrite(uint16_t index, uint16_t bgr555)
{
  if (index < BM_HAL_PALETTE_SIZE)
  {
    GBA_PALETTE[index] = bgr555 & 0x7FFF;
  }
}

uint16_t bmHalPaletteRead(uint16_t index)
{
  return index < BM_HAL_PALETTE_SIZE ? GBA_PALETTE[index] : 0;
}

uint32_t bmHalVideoWaitVBlank(void)
{
  // Busy-wait for now: leave the current VBlank, then wait for the next one.
  // Will be replaced by the BIOS VBlankIntrWait once the interrupt handler exists.
  while (GBA_REG_VCOUNT >= GBA_SCREEN_HEIGHT) { }
  while (GBA_REG_VCOUNT < GBA_SCREEN_HEIGHT) { }
  return ++s_frame;
}
