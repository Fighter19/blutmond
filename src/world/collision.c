/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "collision.h"

#include "pack.h"

#include <limits.h>

// Height profiles, docs/spec/room-format.md section 3.3: height of the solid part per pixel
// column, from the bottom. One table in ROM; flips mirror the lookup.
static const uint8_t s_heights[16][16] = {
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                 // empty
  {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16}, // full
  {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},                 // half top (with vflip)
  {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},                 // half bottom
  {16, 16, 16, 16, 16, 16, 16, 16, 0, 0, 0, 0, 0, 0, 0, 0},         // half left
  {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},          // slope 1:1
  {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8},                 // slope 1:2 a
  {8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16},    // slope 1:2 b
  {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4},                 // slope 1:4 a
  {4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8},                 // slope 1:4 b
  {8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12},        // slope 1:4 c
  {12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16}, // slope 1:4 d
  {2, 4, 6, 8, 10, 12, 14, 16, 16, 16, 16, 16, 16, 16, 16, 16},     // slope 2:1 low
  {0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 6, 8, 10, 12, 14, 16},             // slope 2:1 high
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                 // custom (not yet)
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                 // reserved
};

bool bmCollisionInit(BmCollision *pCollision, const uint8_t *pChunk, uint32_t size, bool edgesSolid)
{
  if (pChunk == NULL || size < 8)
  {
    return false;
  }
  pCollision->width = bmRead16(pChunk);
  pCollision->height = bmRead16(pChunk + 2);
  pCollision->cellSize = pChunk[4];
  pCollision->edgesSolid = edgesSolid ? 1 : 0;
  pCollision->pCells = pChunk + 8;
  return (pCollision->cellSize == 16 || pCollision->cellSize == 8) &&
         size >= 8u + (uint32_t)pCollision->width * pCollision->height * 2u;
}

void bmCollisionCell(const BmCollision *pCollision, int32_t cx, int32_t cy, uint8_t *pShape, uint8_t *pMaterial)
{
  if (cx < 0 || cy < 0 || cx >= pCollision->width || cy >= pCollision->height)
  {
    // Outside: walls at the left, right and top edge when the room says so; open at the bottom.
    bool wall = pCollision->edgesSolid && cy < pCollision->height;
    *pShape = wall ? (uint8_t)(BM_SHAPE_FULL | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES) : 0;
    *pMaterial = 0;
    return;
  }
  const uint8_t *p = pCollision->pCells + ((uint32_t)cy * pCollision->width + (uint32_t)cx) * 2u;
  *pShape = p[0];
  *pMaterial = p[1];
}

uint8_t bmShapeHeight(uint8_t cellByte0, int32_t lx)
{
  uint8_t shape = cellByte0 & 15;
  if (cellByte0 & BM_CELL_HFLIP)
  {
    lx = 15 - lx;
  }
  return s_heights[shape][lx & 15];
}

static int32_t floorDiv(int32_t v, int32_t d)
{
  return v >= 0 ? v / d : -((-v + d - 1) / d);
}

// Scale a pixel column inside a cell to the 16-column profile.
static int32_t profileColumn(const BmCollision *pCollision, int32_t x)
{
  int32_t lx = x - floorDiv(x, pCollision->cellSize) * pCollision->cellSize;
  return pCollision->cellSize == 16 ? lx : lx * 2;
}

static int32_t scaledHeight(const BmCollision *pCollision, uint8_t b0, int32_t x)
{
  int32_t h = bmShapeHeight(b0, profileColumn(pCollision, x));
  return pCollision->cellSize == 16 ? h : h / 2;
}

int32_t bmCollisionFloor(const BmCollision *pCollision, int32_t x, int32_t yFrom, int32_t yTo, bool jumpThrough)
{
  int32_t cs = pCollision->cellSize;
  int32_t cx = floorDiv(x, cs);
  for (int32_t cy = floorDiv(yFrom, cs); cy <= floorDiv(yTo, cs); cy++)
  {
    uint8_t b0, material;
    bmCollisionCell(pCollision, cx, cy, &b0, &material);
    if ((b0 & 15) == BM_SHAPE_EMPTY || (b0 & BM_CELL_VFLIP))
    {
      continue;  // ceiling shapes have no floor on top
    }
    bool top = (b0 & BM_CELL_SOLID_TOP) != 0;
    bool sides = (b0 & BM_CELL_SOLID_SIDES) != 0;
    if (!top || (!sides && !jumpThrough))
    {
      continue;
    }
    int32_t h = scaledHeight(pCollision, b0, x);
    if ((b0 & 15) == BM_SHAPE_HALF_TOP)
    {
      h = 0;  // a half_top cell without vflip is treated as having no floor
    }
    if (h == 0)
    {
      continue;
    }
    int32_t surface = cy * cs + cs - h;
    if (surface >= yFrom && surface <= yTo)
    {
      return surface;
    }
    // A surface above yFrom is out of reach (a wall for this column); keep looking below.
  }
  return INT32_MAX;
}

bool bmCollisionSolidSides(const BmCollision *pCollision, int32_t x, int32_t y)
{
  int32_t cs = pCollision->cellSize;
  int32_t cx = floorDiv(x, cs), cy = floorDiv(y, cs);
  uint8_t b0, material;
  bmCollisionCell(pCollision, cx, cy, &b0, &material);
  if (!(b0 & BM_CELL_SOLID_SIDES) || (b0 & 15) == BM_SHAPE_EMPTY)
  {
    return false;
  }
  int32_t h = scaledHeight(pCollision, b0, x);
  int32_t ly = y - cy * cs;
  bool fromTop = (b0 & BM_CELL_VFLIP) != 0 || (b0 & 15) == BM_SHAPE_HALF_TOP;
  return fromTop ? ly < h : ly >= cs - h;
}

uint8_t bmCollisionMaterial(const BmCollision *pCollision, int32_t x, int32_t y)
{
  uint8_t b0, material;
  bmCollisionCell(pCollision, floorDiv(x, pCollision->cellSize), floorDiv(y, pCollision->cellSize), &b0, &material);
  return material & 15;
}
