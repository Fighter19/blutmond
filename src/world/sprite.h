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
  // Version 2: boxes (NULL for a version 1 chunk: no boxes).
  const uint8_t *pBoxIndex, *pBoxes, *pBoxNames;
  uint16_t boxCount;
  uint8_t boxNameCount;
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

/// The same with the sprites' palette bank given instead of the sheet's own (a hit flash).
uint8_t bmSpriteSheetDrawBank(const BmSpriteSheet *pSheet, uint16_t frame, int32_t x, int32_t y, bool hflip, uint8_t priority,
                              uint8_t bank, uint8_t firstOam, uint8_t maxOam);

/// The same, and upside down with `vflip`: mirrored about the anchor's line (what was above
/// it is below).
uint8_t bmSpriteSheetDrawFlipped(const BmSpriteSheet *pSheet, uint16_t frame, int32_t x, int32_t y, bool hflip, bool vflip,
                                 uint8_t priority, uint8_t bank, uint8_t firstOam, uint8_t maxOam);

/// Draws frame `frame` sheared and scaled about its anchor at (x, y): a point (u, v) of the
/// frame from the anchor appears at (x + u * xs / 256, y + (u * ys + v * vs) / 256). The pieces
/// are affine sprites (double size: each may grow to twice its size) sharing the matrix
/// `matrix` (0..31), which this sets; |xs| is at least 16 (1/16), `vs` 64 to 512. Returns the
/// OAM entries used.
uint8_t bmSpriteSheetDrawSheared(const BmSpriteSheet *pSheet, uint16_t frame, int32_t x, int32_t y, int32_t xs, int32_t ys, int32_t vs,
                                 uint8_t matrix, uint8_t priority, uint8_t firstOam, uint8_t maxOam);

/// The frame's hitbox relative to the anchor (mirrored with `hflip`).
BmSpriteBox bmSpriteSheetHitbox(const BmSpriteSheet *pSheet, uint16_t frame, bool hflip);

/// Kinds of boxes (docs/plans/hitboxes.md §2).
typedef enum BmBoxKind { BM_BOX_HURT = 0, BM_BOX_HIT = 1, BM_BOX_GUARD = 2 } BmBoxKind;

typedef struct BmSpriteNamedBox
{
  BmSpriteBox box;    // relative to the anchor, mirrored with hflip
  uint8_t kind;       // BmBoxKind
  uint16_t name;      // string id of the box's name, 0xFFFF none
} BmSpriteNamedBox;

/// The hurt, hit and guard boxes of a frame (at most `max`); returns how many it has.
uint8_t bmSpriteSheetBoxes(const BmSpriteSheet *pSheet, uint16_t frame, bool hflip, BmSpriteNamedBox *pBoxes, uint8_t max);
