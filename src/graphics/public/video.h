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

/// Shows or hides one background layer, leaving the others as they are.
void bmVideoShowBg(uint8_t bg, bool shown);

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

typedef enum BmSpriteMode {
  BM_SPRITE_NORMAL = 0,
  BM_SPRITE_SEMI_TRANSPARENT = 1,
  BM_SPRITE_OBJ_WINDOW = 2,
} BmSpriteMode;

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
  /// What the sprite is: drawn normally, semi-transparent (blended with what is behind it with
  /// the blend weights, bmBlendSet), or a part of the OBJ window (drawn not at all).
  BmSpriteMode mode;
} BmSprite;

/// Sets a sprite in the shadow OAM; it appears after the next bmVideoWaitFrame().
BmResult bmSpriteSet(uint8_t index, const BmSprite *pSprite);
void bmSpriteHide(uint8_t index);

// --- Windows and colour effects -------------------------------------------------------------
//
// Layers are named by bits: BG n is bit n, sprites BM_LAYER_OBJ, the backdrop BM_LAYER_BACKDROP
// (only as a blend target). A window's layer mask may also have BM_LAYER_EFFECTS: colour
// effects happen there.

#define BM_LAYER_BG(n) (1u << (n))
#define BM_LAYER_OBJ 0x10u
#define BM_LAYER_BACKDROP 0x20u
#define BM_LAYER_EFFECTS 0x20u

/// Window 0 or 1 covers [left, right) x [top, bottom) (screen pixels; 0 to 240 and 0 to 160).
/// A window whose left is beyond its right wraps around the screen's edge.
void bmWindowSet(uint8_t window, uint8_t left, uint8_t top, uint8_t right, uint8_t bottom);

/// Which windows are on: bit 0 window 0, bit 1 window 1, bit 2 the OBJ window (made of the
/// sprites in BM_SPRITE_OBJ_WINDOW mode). With none on, every layer shows everywhere.
void bmWindowEnable(uint8_t mask);

/// What shows inside window 0, inside window 1, outside all windows and in the OBJ window
/// (masks of BM_LAYER_*). Window 0 wins over window 1, both over the OBJ window.
void bmWindowLayers(uint8_t inside0, uint8_t inside1, uint8_t outside, uint8_t objWindow);

typedef enum BmBlendEffect {
  BM_BLEND_NONE = 0,
  BM_BLEND_ALPHA = 1,     // first * eva/16 + second * evb/16, where a second target is below
  BM_BLEND_BRIGHTEN = 2,  // first towards white by evy/16
  BM_BLEND_DARKEN = 3,    // first towards black by evy/16
} BmBlendEffect;

/// Colour effect on the front layer where it is one of `first` (BM_LAYER_* mask); alpha blending
/// needs one of `second` right behind it. Weights in sixteenths, 0 to 16.
void bmBlendSet(BmBlendEffect effect, uint8_t first, uint8_t second, uint8_t eva, uint8_t evb, uint8_t evy);

/// A register (byte offset, ppu.h) that takes the value `pValues[y]` on line y, in line-table
/// slot 0 to 2 (BM_HAL_LINE_TABLES), from the next frame on; NULL switches the slot off.
/// bmBgSetLineScrollX uses slot 0.
BmResult bmVideoSetLineTable(uint8_t slot, uint16_t reg, const uint16_t *pValues);

/// Waits for the next frame and copies the shadow OAM to the hardware.
uint32_t bmVideoWaitFrame(void);
