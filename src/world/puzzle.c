/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "puzzle.h"

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "sprite.h"

#define PUSH_SPEED 192   // 1/256 pixels a frame
#define GRAVITY 64       // 1/256 pixels a frame, each frame
#define FALL_SPEED 1024  // the most, 1/256 pixels a frame
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
      p->blockPushFrames[b] = (uint8_t)(frames != NULL ? atoi(frames) : 6);
      if (fresh)
      {
        p->blockX[b] = o->x;
        p->blockY[b] = o->y;
        p->blockFraction[b] = 0;
        p->blockFall[b] = 0;
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

static bool gateClosed(const BmGame *g, uint8_t k)
{
  return g->puzzle.gateLift[k] < g->objects[g->puzzle.gateObject[k]].h;
}

// Whether a block may be where it would cover (x, y, w, h): every cell there free of what is
// solid at its sides (the map, a broken wall's cells as it left them, closed gates), inside
// the room, no other block, not the player.
static bool freeAt(const BmGame *g, uint8_t self, int32_t x, int32_t y)
{
  const BmPuzzle *p = &g->puzzle;
  const BmObjectView *o = &g->objects[p->blockObject[self]];
  const BmCollision *c = &g->collision;
  int32_t cs = c->cellSize;
  if (x < 0 || y < 0 || x + o->w > c->width * cs || y + o->h > c->height * cs) return false;
  for (int32_t cy = y / cs; cy <= (y + o->h - 1) / cs; cy++)
    for (int32_t cx = x / cs; cx <= (x + o->w - 1) / cs; cx++)
    {
      uint8_t shape, material;
      bmCollisionCell(c, cx, cy, &shape, &material);
      if ((shape & 15) != 0 && (shape & BM_CELL_SOLID_SIDES)) return false;
    }
  for (uint8_t b = 0; b < p->blockCount; b++)
  {
    const BmObjectView *bo = &g->objects[p->blockObject[b]];
    if (b != self && overlaps(x, y, o->w, o->h, p->blockX[b], p->blockY[b], bo->w, bo->h)) return false;
  }
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  return !overlaps(x, y, o->w, o->h, px - hw, py - h, hw * 2, h);
}

// Whether something holds block b up: a floor (a cell solid on top) under any part of it,
// another block, the room's bottom.
static bool held(const BmGame *g, uint8_t b)
{
  const BmPuzzle *p = &g->puzzle;
  const BmObjectView *o = &g->objects[p->blockObject[b]];
  const BmCollision *c = &g->collision;
  int32_t cs = c->cellSize, bottom = p->blockY[b] + o->h;
  if (bottom >= c->height * cs) return true;
  if (bottom % cs == 0)
    for (int32_t cx = p->blockX[b] / cs; cx <= (p->blockX[b] + o->w - 1) / cs; cx++)
    {
      uint8_t shape, material;
      bmCollisionCell(c, cx, bottom / cs, &shape, &material);
      if ((shape & 15) != 0 && (shape & BM_CELL_SOLID_TOP) && !(shape & BM_CELL_VFLIP)) return true;
    }
  for (uint8_t k = 0; k < p->blockCount; k++)
  {
    const BmObjectView *ko = &g->objects[p->blockObject[k]];
    if (k != b && p->blockY[k] == bottom && overlaps(p->blockX[b], 0, o->w, 1, p->blockX[k], 0, ko->w, 1)) return true;
  }
  return false;
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
  // Nothing under it: it falls, faster each frame, a pixel at a time until something holds it.
  if (!held(g, b))
  {
    p->blockPush[b] = 0;
    if (p->blockFall[b] < FALL_SPEED) p->blockFall[b] = (int16_t)(p->blockFall[b] + GRAVITY);
    for (int32_t n = (p->blockFall[b] + 255) / 256; n > 0 && !held(g, b) && freeAt(g, b, p->blockX[b], p->blockY[b] + 1); n--)
      p->blockY[b]++;
    if (held(g, b) || !freeAt(g, b, p->blockX[b], p->blockY[b] + 1)) p->blockFall[b] = 0;
    return;
  }
  p->blockFall[b] = 0;
  // The player on the ground walking into its side, as tall as it there: pushed, after a
  // moment, for as long as they keep at it.
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  int32_t bx = p->blockX[b], by = p->blockY[b];
  bool against = g->player.onGround && dx != 0 && py - h < by + o->h && py > by + 2 &&
                 (dx > 0 ? abs(px + hw - bx) <= 1 : abs(px - hw - (bx + o->w)) <= 1);
  if (!against)
  {
    p->blockPush[b] = 0;
    p->blockFraction[b] = 0;
    return;
  }
  p->pushing = dx;
  if (p->blockPush[b] < p->blockPushFrames[b])
  {
    p->blockPush[b]++;
    return;
  }
  int32_t f = p->blockFraction[b] + PUSH_SPEED;
  for (; f >= 256; f -= 256)
  {
    if (!freeAt(g, b, p->blockX[b] + dx, p->blockY[b]))
    {
      f = 0;
      break;
    }
    p->blockX[b] = (int16_t)(p->blockX[b] + dx);
  }
  p->blockFraction[b] = (uint8_t)f;
}

void bmPuzzleFrame(BmGame *g, int8_t dx)
{
  BmPuzzle *p = &g->puzzle;
  p->pushing = 0;
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
  BmPuzzle *p = &g->puzzle;
  uint8_t n = 0;
  for (uint8_t k = 0; k < g->breakCount && n < BM_GAME_MAX_OVERRIDES; k++) g->overrides[n++] = g->breakCells[k];
  const uint8_t solid = BM_SHAPE_FULL | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES;
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
  for (uint8_t b = 0; b < p->blockCount; b++)
  {
    const BmObjectView *o = &g->objects[p->blockObject[b]];
    p->boxes[b] = (BmSolidBox){ p->blockX[b], p->blockY[b], (int16_t)o->w, (int16_t)o->h };
  }
  g->collision.pBoxes = p->boxes;
  g->collision.boxCount = p->blockCount;
}

uint8_t bmPuzzleDraw(BmGame *g, uint8_t firstOam, uint8_t maxOam)
{
  const BmPuzzle *p = &g->puzzle;
  if (bmGameSheetBank(g, p->sheet, 0) < 0) return 0;
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
