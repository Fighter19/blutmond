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

#define BM_SPRITE_MAX_PALETTES 16

// A palette slot's flags (sprite-format.md, version 5).
#define BM_SPRITE_SLOT_EMISSIVE 1   // not lit: stays as drawn
#define BM_SPRITE_SLOT_SHINY 2      // a highlight where it faces between the light and the eye

typedef struct BmSpriteSheet
{
  const uint8_t *pData;
  uint16_t nameId;
  uint16_t frameCount, animCount, tileCount;
  const uint8_t *pFrames, *pPieces, *pAnims, *pSteps, *pPalette, *pTiles;
  const uint8_t *pEnd;   // the chunk's end
  bool packed;           // the tiles are packed (sprite-format.md); unpacked when loaded
  // Version 2: boxes (NULL for a version 1 chunk: no boxes).
  const uint8_t *pBoxIndex, *pBoxes, *pBoxNames;
  uint16_t boxCount;
  uint8_t boxNameCount;
  // Version 3: palettes (the default first), 16 colours each, and their names (string ids).
  uint8_t paletteCount;
  const uint8_t *pPaletteNames;   // NULL: only the default
  // Version 4: points (NULL: none): an index of frameCount + 1 entries, 4 bytes a point, names.
  const uint8_t *pPointIndex, *pPoints, *pPointNames;
  uint8_t pointNameCount;
  // Version 5: per palette slot a normal (s8 x, y, z) and flags (BM_SPRITE_SLOT_*), NULL: none.
  const uint8_t *pNormals;
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

/// Its tiles alone into sprite memory at `tileBase` (sets tileBase).
BmResult bmSpriteSheetLoadTiles(BmSpriteSheet *pSheet, uint16_t tileBase);

/// Its palette `palette` (0: the default; one it does not have: the default) into OBJ bank
/// `bank`.
void bmSpriteSheetLoadPalette(const BmSpriteSheet *pSheet, uint8_t palette, uint8_t bank);

/// The number of its palette named `pName` ("default": 0), or -1.
int32_t bmSpriteSheetPalette(const BmSpriteSheet *pSheet, const BmPack *pPack, const char *pName);

/// Index of the animation called `pName`, or -1.
int32_t bmSpriteSheetAnim(const BmSpriteSheet *pSheet, const BmPack *pPack, const char *pName);
/// Frames animation `anim` lasts, once through (0: none).
uint32_t bmAnimLength(const BmSpriteSheet *pSheet, uint8_t anim);

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

/// Draws frame `frame`, as `bmSpriteSheetDrawFlipped` would at (x, y), mirrored in a water line
/// at screen row `surface` and squeezed: a point `d` rows above the line appears `d * 16 / squash`
/// rows below it. Semi-transparent affine sprites sharing matrix `matrix` (which this sets).
/// Returns the OAM entries used.
uint8_t bmSpriteSheetDrawReflected(const BmSpriteSheet *pSheet, uint16_t frame, int32_t x, int32_t y, bool hflip, int32_t surface,
                                   uint8_t squash, uint8_t matrix, uint8_t priority, uint8_t bank, uint8_t firstOam, uint8_t maxOam);

/// The frame's hitbox relative to the anchor (mirrored with `hflip`).
BmSpriteBox bmSpriteSheetHitbox(const BmSpriteSheet *pSheet, uint16_t frame, bool hflip);

/// A point of a frame (layered-sprites.md §4): where a figure's layer is placed.
typedef struct BmSpritePoint
{
  int8_t x, y;        // relative to the anchor, mirrored with the flips
  uint8_t angle;      // 256 a turn, 0 forward, clockwise on screen; mirrored with the flips
  bool behind;        // what hangs there is drawn behind this layer
} BmSpritePoint;

/// The point named by string id `name` on frame `frame`, mirrored with `hflip` and `vflip`.
/// False if the frame has none of that name.
bool bmSpriteSheetPoint(const BmSpriteSheet *pSheet, uint16_t frame, uint16_t name, bool hflip, bool vflip, BmSpritePoint *pOut);

/// Index of the animation whose name is string id `name`, or -1.
int32_t bmSpriteSheetAnimById(const BmSpriteSheet *pSheet, uint16_t name);
/// The string id of animation `anim`'s name (0xFFFF if there is none).
uint16_t bmSpriteSheetAnimName(const BmSpriteSheet *pSheet, uint8_t anim);
/// Steps of animation `anim`.
uint16_t bmSpriteSheetAnimSteps(const BmSpriteSheet *pSheet, uint8_t anim);
/// Whether animation `anim` is a direction set (layered-sprites.md §6): each step stands for a
/// direction, nothing plays in time (bmAnimTick leaves it alone).
bool bmSpriteSheetAnimIsDirections(const BmSpriteSheet *pSheet, uint8_t anim);
/// The step of direction set `anim` nearest to `angle` (256 a turn, 0 forward, clockwise on
/// screen as drawn, facing right).
uint16_t bmSpriteSheetDirectionStep(const BmSpriteSheet *pSheet, uint8_t anim, uint8_t angle);
/// The first direction set of the sheet, or -1.
int32_t bmSpriteSheetFirstDirections(const BmSpriteSheet *pSheet);

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
