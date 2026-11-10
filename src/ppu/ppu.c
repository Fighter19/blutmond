/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "ppu.h"

#include <string.h>

#define OBJ_VRAM_BASE 0x10000u
#define BG_VRAM_LIMIT 0x10000u  // backgrounds can only read the first 64 KB in mode 0

void bmPpuReset(BmPpuState *pState)
{
  memset(pState, 0, sizeof(*pState));
  for (int i = 0; i < BM_PPU_LINE_TABLES; i++)
  {
    pState->lineTables[i].reg = 0xFFFF;
  }
}

static uint8_t vram8(const BmPpuState *pState, uint32_t offset)
{
  return offset < BM_PPU_VRAM_SIZE ? pState->vram[offset] : 0;
}

static uint16_t vram16(const BmPpuState *pState, uint32_t offset)
{
  return (uint16_t)(vram8(pState, offset) | (vram8(pState, offset + 1) << 8));
}

// Colour index (0 = transparent) of pixel (px, py) of a tile, 4 or 8 bpp.
static uint8_t tilePixel(const BmPpuState *pState, uint32_t tileAddr, bool bpp8, int px, int py)
{
  if (bpp8)
  {
    return vram8(pState, tileAddr + (uint32_t)(py * 8 + px));
  }
  uint8_t b = vram8(pState, tileAddr + (uint32_t)(py * 4 + px / 2));
  return (px & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 15);
}

typedef struct LinePixel
{
  uint16_t color;
  uint8_t priority;
  bool opaque;
} LinePixel;

// Background n of a mode 0 screen, one line.
static void renderTextBg(const BmPpuState *pState, const uint16_t *regs, int n, int y, LinePixel out[BM_PPU_WIDTH])
{
  uint16_t cnt = regs[BM_PPU_BGCNT(n) / 2];
  uint32_t charBase = ((cnt >> 2) & 3u) * 0x4000u;
  uint32_t screenBase = ((cnt >> 8) & 31u) * 0x800u;
  bool bpp8 = (cnt & 0x80) != 0;
  uint8_t priority = cnt & 3;
  int size = (cnt >> 14) & 3;
  int widthPx = (size & 1) ? 512 : 256;
  int heightPx = (size & 2) ? 512 : 256;

  int hofs = regs[BM_PPU_BGHOFS(n) / 2] & 0x1FF;
  int vofs = regs[BM_PPU_BGVOFS(n) / 2] & 0x1FF;
  int ty = (y + vofs) & (heightPx - 1);

  for (int x = 0; x < BM_PPU_WIDTH; x++)
  {
    int tx = (x + hofs) & (widthPx - 1);

    // 32x32-entry screen blocks: [0][1] side by side, [2][3] below for 512x512.
    int block = (tx >> 8) + ((ty >> 8) * (widthPx >> 8));
    uint32_t entryAddr = screenBase + (uint32_t)block * 0x800u + (uint32_t)(((ty & 255) >> 3) * 32 + ((tx & 255) >> 3)) * 2u;
    uint16_t entry = vram16(pState, entryAddr);

    int px = tx & 7, py = ty & 7;
    if (entry & 0x0400) px = 7 - px;
    if (entry & 0x0800) py = 7 - py;

    uint32_t tile = entry & 0x3FF;
    uint32_t tileAddr = charBase + tile * (bpp8 ? 64u : 32u);
    if (tileAddr >= BG_VRAM_LIMIT)
    {
      continue;
    }
    uint8_t index = tilePixel(pState, tileAddr, bpp8, px, py);
    if (index == 0)
    {
      continue;
    }
    uint16_t paletteIndex = bpp8 ? index : (uint16_t)(((entry >> 12) << 4) | index);
    out[x] = (LinePixel){ pState->palette[paletteIndex] & 0x7FFF, priority, true };
  }
}

static const uint8_t s_objSizes[3][4][2] = {
  // shape 0: square, 1: horizontal, 2: vertical; size 0..3; {width, height}
  { {8, 8}, {16, 16}, {32, 32}, {64, 64} },
  { {16, 8}, {32, 8}, {32, 16}, {64, 32} },
  { {8, 16}, {8, 32}, {16, 32}, {32, 64} },
};

// Regular (non-affine) sprites, one line. Per pixel the sprite with the lowest OAM index wins;
// its priority is then compared with the backgrounds (as on the GBA).
static void renderObjs(const BmPpuState *pState, const uint16_t *regs, int y, LinePixel out[BM_PPU_WIDTH])
{
  bool map1D = (regs[BM_PPU_DISPCNT / 2] & BM_PPU_DISPCNT_OBJ_1D) != 0;

  for (int i = 0; i < 128; i++)
  {
    uint16_t a0 = pState->oam[i * 4 + 0];
    uint16_t a1 = pState->oam[i * 4 + 1];
    uint16_t a2 = pState->oam[i * 4 + 2];

    if (a0 & 0x0100)
    {
      continue;  // affine: not supported yet
    }
    if (a0 & 0x0200)
    {
      continue;  // hidden
    }
    int shape = (a0 >> 14) & 3;
    if (shape == 3)
    {
      continue;
    }
    int sizeIndex = (a1 >> 14) & 3;
    int w = s_objSizes[shape][sizeIndex][0];
    int h = s_objSizes[shape][sizeIndex][1];

    int sy = a0 & 0xFF;
    if (sy >= BM_PPU_HEIGHT) sy -= 256;
    int row = y - sy;
    if (row < 0 || row >= h)
    {
      continue;
    }
    int sx = a1 & 0x1FF;
    if (sx >= 256) sx -= 512;

    bool bpp8 = (a0 & 0x2000) != 0;
    bool hflip = (a1 & 0x1000) != 0;
    bool vflip = (a1 & 0x2000) != 0;
    uint32_t tile = a2 & 0x3FF;
    uint8_t priority = (a2 >> 10) & 3;
    uint16_t bank = (a2 >> 12) & 15;

    int ty = vflip ? h - 1 - row : row;
    for (int col = 0; col < w; col++)
    {
      int x = sx + col;
      if (x < 0 || x >= BM_PPU_WIDTH || out[x].opaque)
      {
        continue;
      }
      int tx = hflip ? w - 1 - col : col;

      // Tile numbers count 32-byte units; an 8 bpp tile takes two of them.
      uint32_t tileStep = bpp8 ? 2u : 1u;
      uint32_t rowStride = map1D ? (uint32_t)(w / 8) * tileStep : 32u;
      uint32_t t = tile + (uint32_t)(ty / 8) * rowStride + (uint32_t)(tx / 8) * tileStep;
      uint32_t tileAddr = OBJ_VRAM_BASE + (t & 0x3FF) * 32u;

      uint8_t index = tilePixel(pState, tileAddr, bpp8, tx & 7, ty & 7);
      if (index == 0)
      {
        continue;
      }
      uint16_t paletteIndex = (uint16_t)(256 + (bpp8 ? index : ((bank << 4) | index)));
      out[x] = (LinePixel){ pState->palette[paletteIndex] & 0x7FFF, priority, true };
    }
  }
}

void bmPpuRenderLine(const BmPpuState *pState, int y, uint16_t out[BM_PPU_WIDTH])
{
  // Registers for this line: the frame's values with the line tables applied.
  uint16_t regs[BM_PPU_REG_SIZE / 2];
  memcpy(regs, pState->regs, sizeof(regs));
  for (int i = 0; i < BM_PPU_LINE_TABLES; i++)
  {
    const BmPpuLineTable *pTable = &pState->lineTables[i];
    if (pTable->reg < BM_PPU_REG_SIZE)
    {
      regs[pTable->reg / 2] = pTable->values[y];
    }
  }

  uint16_t dispcnt = regs[BM_PPU_DISPCNT / 2];
  if (dispcnt & BM_PPU_DISPCNT_FORCED_BLANK)
  {
    for (int x = 0; x < BM_PPU_WIDTH; x++) out[x] = 0x7FFF;
    return;
  }

  uint16_t backdrop = pState->palette[0] & 0x7FFF;
  for (int x = 0; x < BM_PPU_WIDTH; x++) out[x] = backdrop;

  int mode = dispcnt & BM_PPU_DISPCNT_MODE_MASK;
  if (mode != 0)
  {
    return;  // other modes not supported yet: backdrop only
  }

  LinePixel bg[4][BM_PPU_WIDTH];
  memset(bg, 0, sizeof(bg));
  for (int n = 0; n < 4; n++)
  {
    if (dispcnt & BM_PPU_DISPCNT_BG(n))
    {
      renderTextBg(pState, regs, n, y, bg[n]);
    }
  }

  LinePixel obj[BM_PPU_WIDTH];
  memset(obj, 0, sizeof(obj));
  if (dispcnt & BM_PPU_DISPCNT_OBJ)
  {
    renderObjs(pState, regs, y, obj);
  }

  for (int x = 0; x < BM_PPU_WIDTH; x++)
  {
    // Lower priority value is in front. Sprites win against backgrounds of the same priority,
    // lower-numbered backgrounds win against higher-numbered ones.
    int bestPriority = 4;
    uint16_t color = backdrop;
    for (int n = 3; n >= 0; n--)
    {
      if (bg[n][x].opaque && bg[n][x].priority <= bestPriority)
      {
        bestPriority = bg[n][x].priority;
        color = bg[n][x].color;
      }
    }
    if (obj[x].opaque && obj[x].priority <= bestPriority)
    {
      color = obj[x].color;
    }
    out[x] = color;
  }
}

void bmPpuRender(const BmPpuState *pState, uint16_t out[BM_PPU_WIDTH * BM_PPU_HEIGHT])
{
  for (int y = 0; y < BM_PPU_HEIGHT; y++)
  {
    bmPpuRenderLine(pState, y, out + y * BM_PPU_WIDTH);
  }
}

// --- Snapshots ---------------------------------------------------------------------------

#define SNAPSHOT_HEADER 8

size_t bmPpuSnapshotSize(void)
{
  return SNAPSHOT_HEADER + BM_PPU_REG_SIZE + BM_PPU_VRAM_SIZE + BM_PPU_PALETTE_SIZE * 2 +
         BM_PPU_OAM_SIZE * 2 + BM_PPU_LINE_TABLES * (2 + BM_PPU_HEIGHT * 2);
}

static uint8_t *put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  return p + 2;
}

static uint16_t get16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

void bmPpuSnapshotWrite(const BmPpuState *pState, uint8_t *p)
{
  memcpy(p, "BMPS", 4);
  p = put16(p + 4, BM_PPU_SNAPSHOT_VERSION);
  p = put16(p, BM_PPU_LINE_TABLES);
  for (int i = 0; i < BM_PPU_REG_SIZE / 2; i++) p = put16(p, pState->regs[i]);
  memcpy(p, pState->vram, BM_PPU_VRAM_SIZE);
  p += BM_PPU_VRAM_SIZE;
  for (int i = 0; i < BM_PPU_PALETTE_SIZE; i++) p = put16(p, pState->palette[i]);
  for (int i = 0; i < BM_PPU_OAM_SIZE; i++) p = put16(p, pState->oam[i]);
  for (int t = 0; t < BM_PPU_LINE_TABLES; t++)
  {
    p = put16(p, pState->lineTables[t].reg);
    for (int y = 0; y < BM_PPU_HEIGHT; y++) p = put16(p, pState->lineTables[t].values[y]);
  }
}

bool bmPpuSnapshotRead(BmPpuState *pState, const uint8_t *p, size_t size)
{
  if (size < SNAPSHOT_HEADER || memcmp(p, "BMPS", 4) != 0 || get16(p + 4) != BM_PPU_SNAPSHOT_VERSION)
  {
    return false;
  }
  uint16_t tables = get16(p + 6);
  size_t expected = SNAPSHOT_HEADER + BM_PPU_REG_SIZE + BM_PPU_VRAM_SIZE + BM_PPU_PALETTE_SIZE * 2 +
                    BM_PPU_OAM_SIZE * 2 + (size_t)tables * (2 + BM_PPU_HEIGHT * 2);
  if (size < expected)
  {
    return false;
  }
  bmPpuReset(pState);
  p += SNAPSHOT_HEADER;
  for (int i = 0; i < BM_PPU_REG_SIZE / 2; i++, p += 2) pState->regs[i] = get16(p);
  memcpy(pState->vram, p, BM_PPU_VRAM_SIZE);
  p += BM_PPU_VRAM_SIZE;
  for (int i = 0; i < BM_PPU_PALETTE_SIZE; i++, p += 2) pState->palette[i] = get16(p);
  for (int i = 0; i < BM_PPU_OAM_SIZE; i++, p += 2) pState->oam[i] = get16(p);
  for (int t = 0; t < tables; t++)
  {
    uint16_t reg = get16(p);
    p += 2;
    for (int y = 0; y < BM_PPU_HEIGHT; y++, p += 2)
    {
      if (t < BM_PPU_LINE_TABLES) pState->lineTables[t].values[y] = get16(p);
    }
    if (t < BM_PPU_LINE_TABLES) pState->lineTables[t].reg = reg;
  }
  return true;
}
