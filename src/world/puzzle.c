/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "puzzle.h"

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "sprite.h"

#define STEP 16          // a push moves a block this far
#define PUSH_SPEED 1     // pixels a frame
#define FALL_SPEED 4
#define GATE_SPEED 2

static const char *const s_animNames[4] = { "block", "gate", "plate", "plate_down" };

static bool overlaps(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t bx, int32_t by, int32_t bw, int32_t bh)
{
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

void bmPuzzleEnterRoom(BmGame *g, bool fresh)
{
  BmPuzzle *p = &g->puzzle;
  p->blockCount = p->gateCount = p->plateCount = 0;
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (strcmp(o->className, "PushBlock") == 0 && p->blockCount < BM_PUZZLE_BLOCKS)
    {
      uint8_t b = p->blockCount++;
      p->blockObject[b] = i;
      const char *frames = bmGameObjectProperty(g, o, "push_frames");
      p->blockPushFrames[b] = (uint8_t)(frames != NULL ? atoi(frames) : 12);
      if (fresh)
      {
        p->blockX[b] = p->blockToX[b] = o->x;
        p->blockY[b] = p->blockToY[b] = o->y;
        p->blockPush[b] = 0;
      }
    }
    else if (strcmp(o->className, "Gate") == 0 && p->gateCount < BM_PUZZLE_GATES)
    {
      uint8_t k = p->gateCount++;
      p->gateObject[k] = i;
      if (fresh) p->gateLift[k] = 0;
    }
  }
  // Plates after the gates: they name theirs.
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (strcmp(o->className, "PressurePlate") != 0 || p->plateCount >= BM_PUZZLE_PLATES) continue;
    uint8_t k = p->plateCount++;
    p->plateObject[k] = i;
    p->plateGate[k] = 0xFF;
    const char *gate = bmGameObjectProperty(g, o, "gate");
    for (uint8_t q = 0; gate != NULL && q < p->gateCount; q++)
      if (strcmp(g->objects[p->gateObject[q]].name, gate) == 0) p->plateGate[k] = q;
  }
  if (fresh) p->pressed = 0;
  p->sheet = -1;
  for (uint8_t s = 0; s < g->sheetCount; s++)
  {
    const char *name = bmPackString(g->pPack, g->sheets[s].nameId);
    if (name != NULL && strcmp(name, "puzzle") == 0) p->sheet = (int8_t)s;
  }
  for (uint8_t a = 0; a < 4; a++)
  {
    int32_t anim = p->sheet >= 0 ? bmSpriteSheetAnim(&g->sheets[p->sheet], g->pPack, s_animNames[a]) : -1;
    p->anims[a] = (int8_t)anim;
  }
  bmPuzzleOverrides(g);
}

// --- Where things are -----------------------------------------------------------------------

// The cells (x0, y0, x1, y1: x1, y1 not in it) a block covers, where it is and where it goes.
static void blockCells(const BmGame *g, uint8_t b, int32_t *c)
{
  const BmPuzzle *p = &g->puzzle;
  const BmObjectView *o = &g->objects[p->blockObject[b]];
  int32_t cs = g->collision.cellSize;
  int32_t x0 = p->blockX[b] < p->blockToX[b] ? p->blockX[b] : p->blockToX[b];
  int32_t y0 = p->blockY[b] < p->blockToY[b] ? p->blockY[b] : p->blockToY[b];
  int32_t x1 = (p->blockX[b] > p->blockToX[b] ? p->blockX[b] : p->blockToX[b]) + o->w;
  int32_t y1 = (p->blockY[b] > p->blockToY[b] ? p->blockY[b] : p->blockToY[b]) + o->h;
  c[0] = x0 / cs;
  c[1] = y0 / cs;
  c[2] = (x1 + cs - 1) / cs;
  c[3] = (y1 + cs - 1) / cs;
}

static bool gateClosed(const BmGame *g, uint8_t k)
{
  return g->puzzle.gateLift[k] < g->objects[g->puzzle.gateObject[k]].h;
}

// Whether cell (cx, cy) holds something a block cannot move into: the room's edges (and its
// bottom), the map's solid cells unless a broken wall left them empty, other blocks, closed
// gates.
static bool cellTaken(const BmGame *g, int32_t cx, int32_t cy, uint8_t self)
{
  const BmCollision *c = &g->collision;
  if (cx < 0 || cy < 0 || cx >= c->width || cy >= c->height) return true;
  const BmPuzzle *p = &g->puzzle;
  for (uint8_t b = 0; b < p->blockCount; b++)
  {
    int32_t r[4];
    if (b == self) continue;
    blockCells(g, b, r);
    if (cx >= r[0] && cx < r[2] && cy >= r[1] && cy < r[3]) return true;
  }
  int32_t cs = c->cellSize;
  for (uint8_t k = 0; k < p->gateCount; k++)
  {
    const BmObjectView *o = &g->objects[p->gateObject[k]];
    if (gateClosed(g, k) && overlaps(cx * cs, cy * cs, cs, cs, o->x, o->y, o->w, o->h)) return true;
  }
  for (uint8_t k = 0; k < g->breakCount; k++)
  {
    const BmCellOverride *o = &g->breakCells[k];
    if (cx >= o->x0 && cx < o->x1 && cy >= o->y0 && cy < o->y1) return (o->shape & 15) != 0;
  }
  return (c->pCells[((uint32_t)cy * c->width + (uint32_t)cx) * 2u] & 15) != 0;
}

// Whether a block of object o's size at (x, y) fits: no cell of it taken, not into the player.
static bool fits(const BmGame *g, uint8_t b, int32_t x, int32_t y)
{
  const BmObjectView *o = &g->objects[g->puzzle.blockObject[b]];
  int32_t cs = g->collision.cellSize;
  if (x < 0 || x + o->w > g->collision.width * cs) return false;
  for (int32_t cy = y / cs; cy < (y + o->h + cs - 1) / cs; cy++)
    for (int32_t cx = x / cs; cx < (x + o->w + cs - 1) / cs; cx++)
      if (cellTaken(g, cx, cy, b)) return false;
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  return !overlaps(x, y, o->w, o->h, px - hw, py - h, hw * 2, h);
}

static int32_t towards(int32_t v, int32_t to, int32_t speed)
{
  if (v < to) return v + speed < to ? v + speed : to;
  return v - speed > to ? v - speed : to;
}

// --- A frame --------------------------------------------------------------------------------

static void moveBlock(BmGame *g, uint8_t b, int8_t dx)
{
  BmPuzzle *p = &g->puzzle;
  const BmObjectView *o = &g->objects[p->blockObject[b]];
  if (p->blockX[b] != p->blockToX[b] || p->blockY[b] != p->blockToY[b])
  {
    bool falling = p->blockY[b] != p->blockToY[b];
    p->blockX[b] = (int16_t)towards(p->blockX[b], p->blockToX[b], PUSH_SPEED);
    p->blockY[b] = (int16_t)towards(p->blockY[b], p->blockToY[b], FALL_SPEED);
    if (!falling || p->blockY[b] != p->blockToY[b]) return;
  }
  // Nothing under it: it falls a cell.
  if (fits(g, b, p->blockX[b], p->blockY[b] + STEP))
  {
    p->blockToY[b] = (int16_t)(p->blockY[b] + STEP);
    p->blockPush[b] = 0;
    return;
  }
  // The player on the ground walking into its side, as tall as it there: pushed.
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  int32_t bx = p->blockX[b], by = p->blockY[b];
  bool against = g->player.onGround && dx != 0 && py - h < by + o->h && py > by + 2 &&
                 (dx > 0 ? abs(px + hw - bx) <= 1 : abs(px - hw - (bx + o->w)) <= 1);
  if (!against)
  {
    p->blockPush[b] = 0;
    return;
  }
  if (p->blockPush[b] < 255) p->blockPush[b]++;
  if (p->blockPush[b] >= p->blockPushFrames[b] && fits(g, b, bx + dx * STEP, by))
  {
    p->blockToX[b] = (int16_t)(bx + dx * STEP);
    p->blockPush[b] = 0;
  }
}

void bmPuzzleFrame(BmGame *g, int8_t dx)
{
  BmPuzzle *p = &g->puzzle;
  if (p->blockCount == 0 && p->gateCount == 0 && p->plateCount == 0) return;
  for (uint8_t b = 0; b < p->blockCount; b++) moveBlock(g, b, dx);
  // Plates: a block on them, or the player standing.
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  uint8_t open = 0;
  p->pressed = 0;
  for (uint8_t k = 0; k < p->plateCount; k++)
  {
    const BmObjectView *o = &g->objects[p->plateObject[k]];
    bool down = g->player.onGround && px >= o->x && px < o->x + o->w && py > o->y && py <= o->y + o->h;
    for (uint8_t b = 0; b < p->blockCount && !down; b++)
    {
      const BmObjectView *bo = &g->objects[p->blockObject[b]];
      down = overlaps(p->blockX[b], p->blockY[b], bo->w, bo->h, o->x, o->y, o->w, o->h);
    }
    if (!down) continue;
    p->pressed |= (uint8_t)(1u << k);
    if (p->plateGate[k] != 0xFF) open |= (uint8_t)(1u << p->plateGate[k]);
  }
  // Gates: up while held open; down otherwise, unless the player is under them.
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  for (uint8_t k = 0; k < p->gateCount; k++)
  {
    const BmObjectView *o = &g->objects[p->gateObject[k]];
    bool under = overlaps(px - hw, py - h, hw * 2, h, o->x, o->y, o->w, o->h);
    if ((open & (1u << k)) || (under && p->gateLift[k] != 0))
      p->gateLift[k] = (uint8_t)towards(p->gateLift[k], o->h, GATE_SPEED);
    else
      p->gateLift[k] = (uint8_t)towards(p->gateLift[k], 0, GATE_SPEED);
  }
  bmPuzzleOverrides(g);
}

void bmPuzzleOverrides(BmGame *g)
{
  const BmPuzzle *p = &g->puzzle;
  uint8_t n = 0;
  for (uint8_t k = 0; k < g->breakCount && n < BM_GAME_MAX_OVERRIDES; k++) g->overrides[n++] = g->breakCells[k];
  const uint8_t solid = BM_SHAPE_FULL | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES;
  for (uint8_t b = 0; b < p->blockCount && n < BM_GAME_MAX_OVERRIDES; b++)
  {
    int32_t r[4];
    blockCells(g, b, r);
    g->overrides[n++] = (BmCellOverride){ (int16_t)r[0], (int16_t)r[1], (int16_t)r[2], (int16_t)r[3], solid, 0 };
  }
  int32_t cs = g->collision.cellSize;
  for (uint8_t k = 0; k < p->gateCount && n < BM_GAME_MAX_OVERRIDES; k++)
  {
    if (!gateClosed(g, k)) continue;
    const BmObjectView *o = &g->objects[p->gateObject[k]];
    g->overrides[n++] = (BmCellOverride){ (int16_t)(o->x / cs), (int16_t)(o->y / cs), (int16_t)((o->x + o->w + cs - 1) / cs),
                                          (int16_t)((o->y + o->h + cs - 1) / cs), solid, 0 };
  }
  g->collision.pOverrides = g->overrides;
  g->collision.overrideCount = n;
}

uint8_t bmPuzzleDraw(const BmGame *g, uint8_t firstOam, uint8_t maxOam)
{
  const BmPuzzle *p = &g->puzzle;
  if (p->sheet < 0) return 0;
  const BmSpriteSheet *s = &g->sheets[p->sheet];
  int32_t camX = g->room.cameraX, camY = g->room.cameraY;
  uint8_t used = 0;
  for (uint8_t b = 0; b < p->blockCount && used < maxOam && p->anims[0] >= 0; b++)
    used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrameAt(s, (uint8_t)p->anims[0], 0), p->blockX[b] - camX, p->blockY[b] - camY,
                                             false, 1, (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
  for (uint8_t k = 0; k < p->plateCount && used < maxOam; k++)
  {
    int8_t anim = p->anims[(p->pressed & (1u << k)) ? 3 : 2];
    if (anim < 0) continue;
    const BmObjectView *o = &g->objects[p->plateObject[k]];
    used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrameAt(s, (uint8_t)anim, 0), o->x - camX, o->y - camY, false, 1,
                                             (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
  }
  // Gates behind the main layer: lifted, they go up into the wall above them.
  for (uint8_t k = 0; k < p->gateCount && used < maxOam && p->anims[1] >= 0; k++)
  {
    const BmObjectView *o = &g->objects[p->gateObject[k]];
    if (p->gateLift[k] >= o->h) continue;
    used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrameAt(s, (uint8_t)p->anims[1], 0), o->x - camX, o->y - p->gateLift[k] - camY,
                                             false, 2, (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
  }
  return used;
}
