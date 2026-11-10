/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Low-level video API: display mode, background layers, palettes, tiles, maps, sprites and
// line tables. It describes the hardware as data (docs/PLAN.md section 5) and works the same
// on the GBA and on the host, where the software PPU renders it.

#include "shared.h"
#include "color.h"

#define BM_BG_COUNT 4
#define BM_SPRITE_COUNT 128

typedef enum BmBgSize {
  BM_BG_SIZE_256x256 = 0,
  BM_BG_SIZE_512x256 = 1,
  BM_BG_SIZE_256x512 = 2,
  BM_BG_SIZE_512x512 = 3,
} BmBgSize;

typedef struct BmBgInfo
{
  /// 16 KB block holding the tiles (0-3).
  uint8_t charBlock;
  /// 2 KB block holding the map (0-31).
  uint8_t screenBlock;
  BmBgSize size;
  /// 256 colours (8 bpp) instead of 16 colours per palette bank (4 bpp).
  bool colors256;
  /// 0 (front) to 3 (back).
  uint8_t priority;
} BmBgInfo;

/// Resets the video state: mode 0, all layers and sprites off, line tables off.
void bmVideoInit(void);

/// Shows or hides background layers (bit n = BG n) and sprites.
void bmVideoSetLayers(uint8_t bgMask, bool sprites);

/// Sprite tile mapping: true = 1D (tiles of a sprite follow each other), false = 2D.
void bmVideoSetSpriteMapping1D(bool oneDimensional);

BmResult bmBgSetup(uint8_t bg, const BmBgInfo *pInfo);
void bmBgSetScroll(uint8_t bg, int16_t x, int16_t y);

/// Per-line horizontal scroll of a background (wave, parallax) through a line table.
/// `pOffsets` holds one value per line; NULL switches it off again.
BmResult bmBgSetLineScrollX(uint8_t bg, const int16_t *pOffsets);

/// Palettes: `first` is the entry within the BG or OBJ palette (0-255).
void bmPaletteLoadBg(uint16_t first, const BmColor *pColors, uint16_t count);
void bmPaletteLoadObj(uint16_t first, const BmColor *pColors, uint16_t count);

/// Tile data for backgrounds: `tileIndex` counts tiles of the given depth from the start of
/// the char block. 4 bpp tiles are 32 bytes, 8 bpp tiles 64 bytes.
BmResult bmTilesLoadBg(uint8_t charBlock, uint16_t tileIndex, const void *pData, uint32_t size);

/// Tile data for sprites: `tileIndex` counts 32-byte units from the start of sprite VRAM.
BmResult bmTilesLoadObj(uint16_t tileIndex, const void *pData, uint32_t size);

/// Map entries: tile (bits 0-9), hflip (10), vflip (11), palette bank (12-15).
BmResult bmMapLoad(uint8_t screenBlock, uint16_t firstEntry, const uint16_t *pEntries, uint32_t count);

static inline uint16_t bmMapEntry(uint16_t tile, bool hflip, bool vflip, uint8_t paletteBank)
{
  return (uint16_t)((tile & 0x3FF) | (hflip ? 0x400 : 0) | (vflip ? 0x800 : 0) | ((paletteBank & 15) << 12));
}

typedef enum BmSpriteShape {
  BM_SPRITE_SQUARE = 0,
  BM_SPRITE_WIDE = 1,
  BM_SPRITE_TALL = 2,
} BmSpriteShape;

typedef struct BmSprite
{
  int16_t x, y;
  BmSpriteShape shape;
  /// 0-3; with the shape gives the size (square: 8, 16, 32, 64 px).
  uint8_t size;
  uint16_t tile;
  uint8_t paletteBank;
  uint8_t priority;
  bool colors256;
  bool hflip, vflip;
  bool visible;
} BmSprite;

/// Sets a sprite in the shadow OAM; it appears after the next bmVideoWaitFrame().
BmResult bmSpriteSet(uint8_t index, const BmSprite *pSprite);
void bmSpriteHide(uint8_t index);

/// Waits for the next frame and copies the shadow OAM to the hardware.
uint32_t bmVideoWaitFrame(void);
