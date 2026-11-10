/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Collision grid of a room (docs/spec/room-format.md section 3): two bytes per cell, shapes
// resolved through 16-entry height profiles. All coordinates are pixels in the room.

#include "shared.h"

enum {
  BM_SHAPE_EMPTY = 0,
  BM_SHAPE_FULL = 1,
  BM_SHAPE_HALF_TOP = 2,
  BM_SHAPE_HALF_BOTTOM = 3,
  BM_SHAPE_HALF_LEFT = 4,
  BM_SHAPE_SLOPE_1_1 = 5,
  BM_SHAPE_CUSTOM = 14,
};

enum {
  BM_MATERIAL_NORMAL = 0,
  BM_MATERIAL_WATER = 1,
  BM_MATERIAL_DAMAGE = 2,
  BM_MATERIAL_DEATH = 3,
  BM_MATERIAL_CONVEYOR = 4,
  BM_MATERIAL_ICE = 5,
  BM_MATERIAL_LADDER = 6,
  BM_MATERIAL_BOUNCE = 7,
  BM_MATERIAL_STICKY = 8,
};

#define BM_CELL_HFLIP 0x10
#define BM_CELL_VFLIP 0x20
#define BM_CELL_SOLID_TOP 0x40
#define BM_CELL_SOLID_SIDES 0x80

typedef struct BmCollision
{
  const uint8_t *pCells;   // 2 bytes per cell, row major (in the pack)
  uint16_t width, height;  // in cells
  uint8_t cellSize;        // 16 or 8
  uint8_t edgesSolid;      // room edges (left, right, top) act as walls
} BmCollision;

/// Reads a COLL chunk. Returns false if it is malformed.
bool bmCollisionInit(BmCollision *pCollision, const uint8_t *pChunk, uint32_t size, bool edgesSolid);

/// Cell bytes at cell (cx, cy); outside the grid: empty (or full at solid edges, except below).
void bmCollisionCell(const BmCollision *pCollision, int32_t cx, int32_t cy, uint8_t *pShape, uint8_t *pMaterial);

/// Height of the shape's solid part in pixel column `lx` (0..15), measured from the bottom
/// (or from the top for vflipped shapes), flips applied.
uint8_t bmShapeHeight(uint8_t cellByte0, int32_t lx);

/// Topmost floor surface (y of the first solid pixel) in pixel column x, searching from yFrom
/// down to yTo (inclusive). `jumpThrough`: also count cells that are only solid on top.
/// Returns INT32_MAX if there is none.
int32_t bmCollisionFloor(const BmCollision *pCollision, int32_t x, int32_t yFrom, int32_t yTo, bool jumpThrough);

/// True if the pixel blocks sideways or upward movement (walls, ceilings).
bool bmCollisionSolidSides(const BmCollision *pCollision, int32_t x, int32_t y);

/// Material at a pixel.
uint8_t bmCollisionMaterial(const BmCollision *pCollision, int32_t x, int32_t y);
