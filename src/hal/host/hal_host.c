/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "hal.h"
#include "hal_host.h"
#include "ppu_snapshot_part.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

// The host back end models the GBA's memories as plain arrays of the same sizes, so memory
// budgets behave the same on the PC and on the device. Video memory, registers, palette, OAM
// and line tables live in a BmPpuState that the software PPU renders.
static uint8_t s_ewram[256 * 1024];
static uint8_t s_iwram[32 * 1024];
static BmPpuState s_ppu;
static uint32_t s_frame;
static uint16_t s_keys;
static uint8_t s_save[BM_HAL_SAVE_SIZE];
static int s_initialised;

static void ensureInit(void)
{
  if (!s_initialised)
  {
    bmPpuReset(&s_ppu);
    // Power-on: forced blank until the program sets up the display.
    bmPpuSetReg(&s_ppu, BM_PPU_DISPCNT, BM_PPU_DISPCNT_FORCED_BLANK);
    s_initialised = 1;
  }
}

const char *bmHalPlatformName(void)
{
  return "host";
}

void bmHalGetMemoryRanges(BmHalMemoryRange *pRanges, uint32_t count)
{
  ensureInit();
  const BmHalMemoryRange ranges[3] = {
    { s_ewram, sizeof(s_ewram) },
    { s_iwram, sizeof(s_iwram) },
    { s_ppu.vram, sizeof(s_ppu.vram) },
  };
  for (uint32_t i = 0; i < count; i++)
  {
    pRanges[i] = i < 3 ? ranges[i] : (BmHalMemoryRange){ NULL, 0 };
  }
}

void bmHalVideoInit(void)
{
  ensureInit();
  bmPpuSetReg(&s_ppu, BM_PPU_DISPCNT, 0);
}

void bmHalPaletteWrite(uint16_t index, uint16_t bgr555)
{
  ensureInit();
  if (index < BM_HAL_PALETTE_SIZE)
  {
    s_ppu.palette[index] = bgr555 & 0x7FFF;
  }
}

uint16_t bmHalPaletteRead(uint16_t index)
{
  ensureInit();
  return index < BM_HAL_PALETTE_SIZE ? s_ppu.palette[index] : 0;
}

uint32_t bmHalVideoWaitVBlank(void)
{
  ensureInit();
  return ++s_frame;
}

void bmHalVideoWriteReg(uint16_t offset, uint16_t value)
{
  ensureInit();
  bmPpuSetReg(&s_ppu, offset, value);
}

uint16_t bmHalVideoReadReg(uint16_t offset)
{
  ensureInit();
  return offset < BM_PPU_REG_SIZE ? bmPpuReg(&s_ppu, offset) : 0;
}

void bmHalVramWrite(uint32_t offset, const void *pData, uint32_t size)
{
  ensureInit();
  if (offset < BM_PPU_VRAM_SIZE && size <= BM_PPU_VRAM_SIZE - offset)
  {
    memcpy(s_ppu.vram + offset, pData, size);
  }
}

void bmHalOamWrite(uint32_t first, const uint16_t *pData, uint32_t count)
{
  ensureInit();
  if (first < BM_PPU_OAM_SIZE && count <= BM_PPU_OAM_SIZE - first)
  {
    memcpy(&s_ppu.oam[first], pData, count * sizeof(uint16_t));
  }
}

void bmHalLineTableSet(uint32_t slot, uint16_t reg, const uint16_t *pValues)
{
  ensureInit();
  if (slot >= BM_PPU_LINE_TABLES)
  {
    return;
  }
  BmPpuLineTable *pTable = &s_ppu.lineTables[slot];
  if (pValues == NULL || reg >= BM_PPU_REG_SIZE)
  {
    pTable->reg = 0xFFFF;
    return;
  }
  pTable->reg = reg;
  memcpy(pTable->values, pValues, sizeof(pTable->values));
}

uint32_t bmHalVideoSnapshotPart(uint32_t offset, uint8_t *pOut, uint32_t size)
{
  ensureInit();
  BmPpuSnapshotSource source = { s_ppu.regs, s_ppu.vram, s_ppu.palette, s_ppu.oam, { 0 }, { NULL } };
  for (int t = 0; t < BM_PPU_LINE_TABLES; t++)
  {
    source.lineRegs[t] = s_ppu.lineTables[t].reg;
    source.pLineValues[t] = s_ppu.lineTables[t].values;
  }
  return bmPpuSnapshotPart(&source, offset, pOut, size);
}

const BmPpuState *bmHalHostGetPpu(void)
{
  ensureInit();
  return &s_ppu;
}

uint32_t bmHalHostFrame(void)
{
  return s_frame;
}

void bmHalHostRender(uint16_t *pOut)
{
  ensureInit();
  bmPpuRender(&s_ppu, pOut);
}

void bmHalHostReset(void)
{
  s_initialised = 0;
  s_frame = 0;
  s_keys = 0;
  ensureInit();
}

uint16_t bmHalInputKeys(void)
{
  return s_keys;
}

void bmHalHostSetKeys(uint16_t keys)
{
  s_keys = keys;
}

void bmHalSaveRead(uint32_t offset, void *pData, uint32_t size)
{
  if (offset <= BM_HAL_SAVE_SIZE && size <= BM_HAL_SAVE_SIZE - offset)
  {
    memcpy(pData, s_save + offset, size);
  }
}

void bmHalSaveWrite(uint32_t offset, const void *pData, uint32_t size)
{
  if (offset <= BM_HAL_SAVE_SIZE && size <= BM_HAL_SAVE_SIZE - offset)
  {
    memcpy(s_save + offset, pData, size);
  }
}

uint8_t *bmHalHostSaveMemory(void)
{
  return s_save;
}

// The link handler may run on another thread (a socket thread in the game, the way the serial
// interrupt runs on the GBA). Lock and handler exclude each other: bmHalLinkLock waits for a
// running handler to finish, and a handler does not start while the link is locked.
static BmHalLinkHandler s_linkHandler;
static void *s_linkContext;
static uint32_t s_linkPrepared;
static atomic_uint s_linkLocked;
static atomic_bool s_linkInHandler;

void bmHalLinkStart(BmHalLinkHandler handler, void *pContext, uint32_t firstWord)
{
  s_linkContext = pContext;
  s_linkPrepared = firstWord;
  s_linkHandler = handler;
}

void bmHalLinkStop(void)
{
  s_linkHandler = NULL;
}

uint32_t bmHalLinkLock(void)
{
  uint32_t state = atomic_fetch_add(&s_linkLocked, 1u);
  while (atomic_load(&s_linkInHandler)) {}
  return state;
}

void bmHalLinkUnlock(uint32_t state)
{
  atomic_store(&s_linkLocked, state);
}

static bool tryExchange(uint32_t masterWord, uint32_t *pIn)
{
  atomic_store(&s_linkInHandler, true);
  if (atomic_load(&s_linkLocked) != 0)
  {
    atomic_store(&s_linkInHandler, false);
    return false;
  }
  *pIn = s_linkPrepared;
  s_linkPrepared = s_linkHandler(s_linkContext, masterWord);
  atomic_store(&s_linkInHandler, false);
  return true;
}

uint32_t bmHalHostLinkExchange(uint32_t masterWord)
{
  // A locked link behaves like a slave that is not ready: the word is lost.
  uint32_t in;
  if (s_linkHandler == NULL || !tryExchange(masterWord, &in)) return 0xFFFFFFFFu;
  return in;
}

uint32_t bmHalHostLinkExchangeWait(uint32_t masterWord)
{
  uint32_t in;
  if (s_linkHandler == NULL) return 0xFFFFFFFFu;
  while (!tryExchange(masterWord, &in)) {}
  return in;
}
