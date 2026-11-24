/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Figures (FIGR chunks, docs/spec/sprite-format.md "Figures"; docs/plans/layered-sprites.md §4):
// what an object shows, up to 4 layers, each a sprite with its own frame, placed at a point of
// another layer's current frame. The first layer is the root, placed at the object. A layer
// animates on its own (`own`: its own animation state, the animation named like its parent's)
// or in step with its parent (`sync`: the animation named like the parent's, at the parent's
// step). Layers are drawn in their order, each in front of those before it; a layer marked
// `behind`, or hanging on a point marked so on the parent's current frame, goes right behind
// its parent instead.

#include "pack.h"
#include "shared.h"
#include "sprite.h"

#define BM_FIGURE_MAX_LAYERS 4

enum { BM_FIGURE_OWN = 0, BM_FIGURE_SYNC = 1 };

typedef struct BmFigureLayer
{
  uint16_t name, sprite;   // string ids; sprite 0xFFFF: none
  uint16_t point;          // the parent's point it is placed at (0xFFFF: the parent's anchor)
  uint16_t palette;        // string id of the palette's name, 0xFFFF the default
  uint8_t parent;          // 0xFF: the root
  uint8_t mode;            // BM_FIGURE_OWN, BM_FIGURE_SYNC
  bool behind;
} BmFigureLayer;

typedef struct BmFigure
{
  uint16_t nameId;
  uint8_t layerCount;
  BmFigureLayer layers[BM_FIGURE_MAX_LAYERS];
} BmFigure;

/// Opens the figure called `pName` (compared without regard to case). BM_ERROR_NOT_FOUND if the
/// pack has none.
BmResult bmFigureFind(BmFigure *pFigure, const BmPack *pPack, const char *pName);

/// Where each layer is and what it shows, worked out from the layers' animation states.
typedef struct BmFigurePose
{
  uint8_t count;                              // layers to draw, in `order`
  uint8_t order[BM_FIGURE_MAX_LAYERS];        // front to back (the order to give out OAM)
  uint16_t frame[BM_FIGURE_MAX_LAYERS];
  int16_t x[BM_FIGURE_MAX_LAYERS], y[BM_FIGURE_MAX_LAYERS];   // from the figure's position
  uint8_t angle[BM_FIGURE_MAX_LAYERS];        // of the point it hangs on (0 for the root)
} BmFigurePose;

/// `pSheets[i]` is layer i's sheet (NULL: none; it is not drawn and what hangs on it is placed
/// at its place), `pStates[i]` its animation state (the root's and every `own` layer's; a
/// `sync` layer's is worked out). Mirrored with `hflip` and `vflip` as a sprite is.
void bmFigurePose(const BmFigure *pFigure, const BmSpriteSheet *const pSheets[BM_FIGURE_MAX_LAYERS],
                  const BmAnimState pStates[BM_FIGURE_MAX_LAYERS], bool hflip, bool vflip, BmFigurePose *pPose);

/// The animation layer `layer` of a figure plays when its parent plays `parentAnim` of
/// `pParent`: the one of the same name in `pSheet`, or -1.
int32_t bmFigureLayerAnim(const BmSpriteSheet *pParent, uint8_t parentAnim, const BmSpriteSheet *pSheet);
