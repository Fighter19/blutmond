/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "video.h"

#include "hal.h"
#include "ppu.h"

#define CHAR_BLOCK_SIZE 0x4000u
#define SCREEN_BLOCK_SIZE 0x800u
#define OBJ_VRAM_OFFSET 0x10000u
#define OBJ_VRAM_SIZE 0x8000u

// Shadow copies: most video registers are write-only on the GBA, and OAM should only be
// written during VBlank.
static uint16_t s_dispcnt;
static uint16_t s_oam[BM_SPRITE_COUNT * 4] BM_EWRAM_BSS;
static bool s_oamDirty;

static void writeDispcnt(void)
{
  bmHalVideoWriteReg(BM_PPU_DISPCNT, s_dispcnt);
}

void bmVideoInit(void)
{
  bmHalVideoInit();
  s_dispcnt = 0;
  writeDispcnt();
  for (int bg = 0; bg < BM_BG_COUNT; bg++)
  {
    bmHalVideoWriteReg(BM_PPU_BGCNT(bg), 0);
    bmHalVideoWriteReg(BM_PPU_BGHOFS(bg), 0);
    bmHalVideoWriteReg(BM_PPU_BGVOFS(bg), 0);
  }
  for (uint32_t i = 0; i < BM_HAL_LINE_TABLES; i++)
  {
    bmHalLineTableSet(i, 0, NULL);
  }
  for (int i = 0; i < BM_SPRITE_COUNT; i++)
  {
    s_oam[i * 4 + 0] = 0x0200;  // hidden
    s_oam[i * 4 + 1] = 0;
    s_oam[i * 4 + 2] = 0;
    s_oam[i * 4 + 3] = 0;
  }
  bmHalOamWrite(0, s_oam, BM_SPRITE_COUNT * 4);
  s_oamDirty = false;
}

void bmVideoSetLayers(uint8_t bgMask, bool sprites)
{
  s_dispcnt &= (uint16_t)~(0x0F00 | BM_PPU_DISPCNT_OBJ);
  s_dispcnt |= (uint16_t)((bgMask & 15) << 8);
  if (sprites)
  {
    s_dispcnt |= BM_PPU_DISPCNT_OBJ;
  }
  writeDispcnt();
}

void bmVideoSetSpriteMapping1D(bool oneDimensional)
{
  if (oneDimensional)
    s_dispcnt |= BM_PPU_DISPCNT_OBJ_1D;
  else
    s_dispcnt &= (uint16_t)~BM_PPU_DISPCNT_OBJ_1D;
  writeDispcnt();
}

BmResult bmBgSetup(uint8_t bg, const BmBgInfo *pInfo)
{
  if (bg >= BM_BG_COUNT || pInfo == NULL || pInfo->charBlock > 3 || pInfo->screenBlock > 31 ||
      pInfo->priority > 3 || (unsigned)pInfo->size > 3)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  uint16_t cnt = (uint16_t)(pInfo->priority | (pInfo->charBlock << 2) | (pInfo->colors256 ? 0x80 : 0) |
                            (pInfo->screenBlock << 8) | ((unsigned)pInfo->size << 14));
  bmHalVideoWriteReg(BM_PPU_BGCNT(bg), cnt);
  return BM_SUCCESS;
}

void bmBgSetScroll(uint8_t bg, int16_t x, int16_t y)
{
  if (bg < BM_BG_COUNT)
  {
    bmHalVideoWriteReg(BM_PPU_BGHOFS(bg), (uint16_t)x & 0x1FF);
    bmHalVideoWriteReg(BM_PPU_BGVOFS(bg), (uint16_t)y & 0x1FF);
  }
}

BmResult bmBgSetLineScrollX(uint8_t bg, const int16_t *pOffsets)
{
  if (bg >= BM_BG_COUNT)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  if (pOffsets == NULL)
  {
    bmHalLineTableSet(0, 0, NULL);
    return BM_SUCCESS;
  }
  uint16_t values[BM_HAL_SCREEN_HEIGHT];
  for (int y = 0; y < BM_HAL_SCREEN_HEIGHT; y++)
  {
    values[y] = (uint16_t)pOffsets[y] & 0x1FF;
  }
  bmHalLineTableSet(0, BM_PPU_BGHOFS(bg), values);
  return BM_SUCCESS;
}

void bmPaletteLoadBg(uint16_t first, const BmColor *pColors, uint16_t count)
{
  for (uint16_t i = 0; i < count && first + i < 256; i++)
  {
    bmHalPaletteWrite((uint16_t)(first + i), pColors[i].color);
  }
}

void bmPaletteLoadObj(uint16_t first, const BmColor *pColors, uint16_t count)
{
  for (uint16_t i = 0; i < count && first + i < 256; i++)
  {
    bmHalPaletteWrite((uint16_t)(BM_HAL_PALETTE_OBJ_OFFSET + first + i), pColors[i].color);
  }
}

BmResult bmTilesLoadBg(uint8_t charBlock, uint16_t tileIndex, const void *pData, uint32_t size)
{
  // 4 bpp tile size as the unit; callers loading 8 bpp tiles pass 2 * index.
  uint32_t offset = charBlock * CHAR_BLOCK_SIZE + tileIndex * 32u;
  if (charBlock > 3 || pData == NULL || (size & 1) || offset + size > OBJ_VRAM_OFFSET)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  bmHalVramWrite(offset, pData, size);
  return BM_SUCCESS;
}

BmResult bmTilesLoadObj(uint16_t tileIndex, const void *pData, uint32_t size)
{
  uint32_t offset = tileIndex * 32u;
  if (pData == NULL || (size & 1) || offset + size > OBJ_VRAM_SIZE)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  bmHalVramWrite(OBJ_VRAM_OFFSET + offset, pData, size);
  return BM_SUCCESS;
}

BmResult bmMapLoad(uint8_t screenBlock, uint16_t firstEntry, const uint16_t *pEntries, uint32_t count)
{
  uint32_t offset = screenBlock * SCREEN_BLOCK_SIZE + firstEntry * 2u;
  if (screenBlock > 31 || pEntries == NULL || offset + count * 2u > OBJ_VRAM_OFFSET)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  bmHalVramWrite(offset, pEntries, count * 2u);
  return BM_SUCCESS;
}

BmResult bmSpriteSet(uint8_t index, const BmSprite *pSprite)
{
  if (index >= BM_SPRITE_COUNT || pSprite == NULL || pSprite->shape > BM_SPRITE_TALL || pSprite->size > 3 ||
      pSprite->priority > 3)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  uint16_t *a = &s_oam[index * 4];
  if (!pSprite->visible)
  {
    a[0] = 0x0200;
  }
  else
  {
    a[0] = (uint16_t)(((uint16_t)pSprite->y & 0xFF) | (pSprite->colors256 ? 0x2000 : 0) |
                      ((unsigned)pSprite->shape << 14));
    a[1] = (uint16_t)(((uint16_t)pSprite->x & 0x1FF) | (pSprite->hflip ? 0x1000 : 0) |
                      (pSprite->vflip ? 0x2000 : 0) | ((unsigned)pSprite->size << 14));
    a[2] = (uint16_t)((pSprite->tile & 0x3FF) | ((pSprite->priority & 3) << 10) |
                      ((pSprite->paletteBank & 15) << 12));
  }
  s_oamDirty = true;
  return BM_SUCCESS;
}

void bmSpriteHide(uint8_t index)
{
  if (index < BM_SPRITE_COUNT)
  {
    s_oam[index * 4] = 0x0200;
    s_oamDirty = true;
  }
}

uint32_t bmVideoWaitFrame(void)
{
  uint32_t frame = bmHalVideoWaitVBlank();
  if (s_oamDirty)
  {
    bmHalOamWrite(0, s_oam, BM_SPRITE_COUNT * 4);
    s_oamDirty = false;
  }
  return frame;
}
