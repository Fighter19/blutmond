/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "hal.h"
#include "hal_gba_regs.h"
#include "ppu.h"
#include "shared.h"

// Provided by the linker script: the parts of EWRAM and IWRAM not used by the program.
extern uint8_t __ewram_heap_start[], __ewram_heap_end[];
extern uint8_t __iwram_heap_start[], __iwram_heap_end[];

static uint32_t s_frame;

// Line table: register offset and its values. One extra entry because the H-blank DMA of the
// last line reads one value past the end.
static uint16_t s_lineReg = 0xFFFF;
static uint16_t s_lineValues[GBA_SCREEN_HEIGHT + 1] BM_EWRAM_BSS;

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

static void restartLineTable(void)
{
  GBA_REG_DMA0CNT_H = 0;
  if (s_lineReg == 0xFFFF)
  {
    return;
  }
  // Line 0 gets its value now; every H-blank then copies the value for the next line.
  GBA_REG16(s_lineReg) = s_lineValues[0];
  GBA_REG_DMA0SAD = (uint32_t)&s_lineValues[1];
  GBA_REG_DMA0DAD = GBA_IO + s_lineReg;
  GBA_REG_DMA0CNT_L = 1;
  GBA_REG_DMA0CNT_H = GBA_DMA_ENABLE | GBA_DMA_START_HBLANK | GBA_DMA_REPEAT | GBA_DMA_DEST_FIXED;
}

uint32_t bmHalVideoWaitVBlank(void)
{
  // Busy-wait for now: leave the current VBlank, then wait for the next one.
  // Will be replaced by the BIOS VBlankIntrWait once the interrupt handler exists.
  while (GBA_REG_VCOUNT >= GBA_SCREEN_HEIGHT) { }
  while (GBA_REG_VCOUNT < GBA_SCREEN_HEIGHT) { }
  restartLineTable();
  return ++s_frame;
}

void bmHalVideoWriteReg(uint16_t offset, uint16_t value)
{
  if (offset < BM_PPU_REG_SIZE)
  {
    GBA_REG16(offset) = value;
  }
}

uint16_t bmHalVideoReadReg(uint16_t offset)
{
  // Most video registers are write-only on the GBA; only DISPCNT and VCOUNT read back.
  return offset < BM_PPU_REG_SIZE ? GBA_REG16(offset) : 0;
}

void bmHalVramWrite(uint32_t offset, const void *pData, uint32_t size)
{
  if (offset >= GBA_VRAM_SIZE || size > GBA_VRAM_SIZE - offset || ((offset | size) & 1))
  {
    return;
  }
  volatile uint16_t *pDst = (volatile uint16_t *)(GBA_VRAM + offset);
  const uint8_t *pSrc = (const uint8_t *)pData;
  for (uint32_t i = 0; i < size / 2; i++)
  {
    pDst[i] = (uint16_t)(pSrc[2 * i] | (pSrc[2 * i + 1] << 8));
  }
}

void bmHalOamWrite(uint32_t first, const uint16_t *pData, uint32_t count)
{
  if (first >= BM_PPU_OAM_SIZE || count > BM_PPU_OAM_SIZE - first)
  {
    return;
  }
  for (uint32_t i = 0; i < count; i++)
  {
    GBA_OAM[first + i] = pData[i];
  }
}

void bmHalLineTableSet(uint32_t slot, uint16_t reg, const uint16_t *pValues)
{
  if (slot != 0)
  {
    return;
  }
  if (pValues == NULL || reg >= BM_PPU_REG_SIZE)
  {
    s_lineReg = 0xFFFF;
    GBA_REG_DMA0CNT_H = 0;
    return;
  }
  for (uint32_t y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    s_lineValues[y] = pValues[y];
  }
  s_lineValues[GBA_SCREEN_HEIGHT] = pValues[GBA_SCREEN_HEIGHT - 1];
  s_lineReg = reg;
}
