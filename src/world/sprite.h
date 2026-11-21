/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Sprites from a pack (SPRT chunks, docs/spec/sprite-format.md): a sheet holds 16-colour tiles,
// one palette bank, frames made of hardware sprite pieces, and animations. The sheet is read
// in place from the pack; loading it copies its tiles and palette into sprite memory.

#include "pack.h"
#include "shared.h"

typedef struct BmSpriteSheet
{
  const uint8_t *pData;
  uint16_t nameId;
  uint16_t frameCount, animCount, tileCount;
  const uint8_t *pFrames, *pPieces, *pAnims, *pSteps, *pPalette, *pTiles;
  // Where it was loaded.
  uint16_t tileBase;
  uint8_t bank;
} BmSpriteSheet;

/// Position in an animation. Plain data, so it can be part of the game state.
typedef struct BmAnimState
{
  uint8_t anim;
  uint8_t finished;  // a non-looping animation reached its end
  uint16_t step;
  uint16_t timer;    // frames the current step has been shown
} BmAnimState;

typedef struct BmSpriteBox
{
  int8_t x, y;
  uint8_t w, h;
} BmSpriteBox;

/// Chunk index of the `n`-th sprite sheet of a pack, or -1.
int32_t bmSpriteSheetChunk(const BmPack *pPack, uint32_t n);
/// Opens the sheet in chunk `chunk`.
BmResult bmSpriteSheetOpen(BmSpriteSheet *pSheet, const BmPack *pPack, uint32_t chunk);
/// Opens the sheet called `pName` (compared without regard to case). BM_ERROR_NOT_FOUND if
/// the pack has none.
BmResult bmSpriteSheetFind(BmSpriteSheet *pSheet, const BmPack *pPack, const char *pName);

/// Copies the tiles to sprite memory from tile `tileBase` and the palette into OBJ bank `bank`.
BmResult bmSpriteSheetLoad(BmSpriteSheet *pSheet, uint16_t tileBase, uint8_t bank);

/// Index of the animation called `pName`, or -1.
int32_t bmSpriteSheetAnim(const BmSpriteSheet *pSheet, const BmPack *pPack, const char *pName);

/// Starts animation `anim` unless it is the one playing (`restart` starts it over anyway).
void bmAnimPlay(BmAnimState *pState, uint8_t anim, bool restart);
/// Advances by one frame.
void bmAnimTick(const BmSpriteSheet *pSheet, BmAnimState *pState);
/// The frame to show.
uint16_t bmAnimFrame(const BmSpriteSheet *pSheet, const BmAnimState *pState);
/// The frame a looping animation shows at time `time` (for things without their own state).
uint16_t bmAnimFrameAt(const BmSpriteSheet *pSheet, uint8_t anim, uint32_t time);

/// Draws frame `frame` with its anchor at (x, y) on screen, using OAM entries from `firstOam`
/// (at most `maxOam`). Returns how many it used; pieces off screen use none.
uint8_t bmSpriteSheetDraw(const BmSpriteSheet *pSheet, uint16_t frame, int32_t x, int32_t y, bool hflip, uint8_t priority,
                          uint8_t firstOam, uint8_t maxOam);

/// The frame's hitbox relative to the anchor (mirrored with `hflip`).
BmSpriteBox bmSpriteSheetHitbox(const BmSpriteSheet *pSheet, uint16_t frame, bool hflip);
