/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "body.h"

#include <limits.h>

void bmBodyDefaultParams(BmBodyParams *p)
{
  p->halfWidth = 6;
  p->height = 28;
  p->runSpeed = BM_FIX(2.0);
  p->groundAccel = BM_FIX(0.5);
  p->airAccel = BM_FIX(0.25);
  p->friction = BM_FIX(0.5);
  p->jumpSpeed = (bm_fix)1357;  // 5.3 px/frame
  p->jumpCut = BM_FIX(0.5);
  p->climbSpeed = BM_FIX(1.0);
  p->gravityScale = BM_FIX(1.0);
  p->stepHeight = 8;
  p->snapDistance = 4;
  p->coyoteFrames = 6;
  p->jumpBufferFrames = 6;
}

void bmBodyPlace(BmBody *pBody, int32_t x, int32_t y)
{
  *pBody = (BmBody){0};
  pBody->x = x * 256;
  pBody->y = y * 256;
  pBody->facing = 1;
  pBody->safeX = x;
  pBody->safeY = y;
}

static bm_fix fixMul(bm_fix a, bm_fix b)
{
  return (bm_fix)(((int64_t)a * b) >> 8);
}

static bm_fix approach(bm_fix v, bm_fix target, bm_fix step)
{
  if (v < target) return v + step > target ? target : v + step;
  if (v > target) return v - step < target ? target : v - step;
  return v;
}

static int32_t floorDiv(int32_t v, int32_t d)
{
  return v >= 0 ? v / d : -((-v + d - 1) / d);
}

// Topmost floor under the body's feet sensors (left and right edge, inset by one pixel).
static int32_t findFloor(const BmBody *b, const BmBodyParams *p, const BmCollision *c, int32_t yFrom, int32_t yTo, bool jumpThrough)
{
  int32_t x = BM_FIX_INT(b->x);
  int32_t best = INT32_MAX;
  int32_t columns[2] = { x - p->halfWidth + 1, x + p->halfWidth - 2 };
  for (int i = 0; i < 2; i++)
  {
    int32_t f = bmCollisionFloor(c, columns[i], yFrom, yTo, jumpThrough);
    if (f < best) best = f;
  }
  return best;
}

// Wall check for a column: from just above step height up to the head.
static bool wallAt(const BmCollision *c, int32_t x, int32_t feet, const BmBodyParams *p)
{
  int32_t top = feet - p->height;
  for (int32_t y = feet - p->stepHeight - 1; y > top; y -= 7)
  {
    if (bmCollisionSolidSides(c, x, y)) return true;
  }
  return bmCollisionSolidSides(c, x, top + 1);
}

uint32_t bmBodyStep(BmBody *b, const BmBodyParams *p, const BmCollision *c, const BmRoomPhysics *room, BmBodyInput in)
{
  uint32_t events = 0;
  int32_t cs = c->cellSize;
  int32_t px = BM_FIX_INT(b->x), feet = BM_FIX_INT(b->y);
  bool wasOnGround = b->onGround;

  // Surroundings
  int32_t centerY = feet - p->height / 2;
  b->inWater = bmCollisionMaterial(c, px, centerY) == BM_MATERIAL_WATER;
  bool onLadder = bmCollisionMaterial(c, px, centerY) == BM_MATERIAL_LADDER ||
                  bmCollisionMaterial(c, px, feet - 1) == BM_MATERIAL_LADDER;
  bool ladderBelow = bmCollisionMaterial(c, px, feet + 1) == BM_MATERIAL_LADDER;

  if (in.dx) b->facing = in.dx;
  if (in.jumpPressed) b->jumpBuffer = p->jumpBufferFrames;
  if (b->dropThrough) b->dropThrough--;

  // Ladders: up/down attaches, jump detaches, leaving the ladder detaches.
  if (!b->climbing && ((in.dy < 0 && onLadder) || (in.dy > 0 && (onLadder || ladderBelow) && !in.jumpPressed)))
  {
    b->climbing = true;
    if (in.dy > 0 && ladderBelow) b->dropThrough = 8;  // climb down through the platform on top
  }
  if (b->climbing && (!onLadder && !ladderBelow))
  {
    b->climbing = false;
  }

  if (b->climbing)
  {
    b->vx = 0;
    b->vy = in.dy * p->climbSpeed;
    if (b->jumpBuffer)
    {
      b->climbing = false;
      b->jumpBuffer = 0;
      b->vy = -fixMul(p->jumpSpeed, BM_FIX(0.75));
      b->jumpCutDone = false;
      events |= BM_BODY_JUMPED;
    }
    else if (in.dx)
    {
      // Step off the ladder sideways.
      b->climbing = false;
    }
  }

  if (!b->climbing)
  {
    // Horizontal speed
    bm_fix target = in.dx * p->runSpeed;
    if (b->inWater) target /= 2;
    if (in.dx) b->vx = approach(b->vx, target, wasOnGround ? p->groundAccel : p->airAccel);
    else if (wasOnGround) b->vx = approach(b->vx, 0, p->friction);
    else b->vx = approach(b->vx, 0, p->airAccel / 2);

    // Jumping (with coyote time and a jump buffer); down + jump drops through platforms.
    if (b->jumpBuffer && (wasOnGround || b->coyote))
    {
      bool onJumpThroughOnly = wasOnGround && findFloor(b, p, c, feet, feet, false) == INT32_MAX;
      if (in.dy > 0 && onJumpThroughOnly)
      {
        b->dropThrough = 8;
      }
      else
      {
        b->vy = -p->jumpSpeed;
        b->jumpCutDone = false;
        events |= BM_BODY_JUMPED;
      }
      b->onGround = false;
      wasOnGround = false;
      b->coyote = 0;
      b->jumpBuffer = 0;
    }
    if (!in.jumpHeld && b->vy < 0 && !b->jumpCutDone)
    {
      b->vy = fixMul(b->vy, p->jumpCut);
      b->jumpCutDone = true;
    }

    // Gravity
    bm_fix g = fixMul(b->inWater ? room->waterGravity : room->gravity, p->gravityScale);
    bm_fix maxFall = b->inWater ? room->waterMaxFallSpeed : room->maxFallSpeed;
    b->vy += g;
    if (b->vy > maxFall) b->vy = maxFall;
  }
  if (b->jumpBuffer) b->jumpBuffer--;

  // --- Horizontal movement and walls ---
  bm_fix nx = b->x + b->vx;
  if (b->vx != 0)
  {
    int32_t nPx = BM_FIX_INT(nx);
    int32_t edge = b->vx > 0 ? nPx + p->halfWidth - 1 : nPx - p->halfWidth;
    if (wallAt(c, edge, feet, p))
    {
      int32_t cellX = floorDiv(edge, cs) * cs;
      nx = b->vx > 0 ? (bm_fix)(cellX - p->halfWidth) * 256 : (bm_fix)(cellX + cs + p->halfWidth) * 256;
      b->vx = 0;
      events |= BM_BODY_HIT_WALL;
    }
  }
  b->x = nx;
  px = BM_FIX_INT(b->x);

  // --- Vertical movement, floors and ceilings ---
  bm_fix ny = b->y + b->vy;
  if (b->vy < 0)
  {
    int32_t head = BM_FIX_INT(ny) - p->height;
    int32_t columns[3] = { px - p->halfWidth + 1, px, px + p->halfWidth - 2 };
    for (int i = 0; i < 3; i++)
    {
      if (bmCollisionSolidSides(c, columns[i], head))
      {
        int32_t cellBottom = (floorDiv(head, cs) + 1) * cs;
        ny = (bm_fix)(cellBottom + p->height) * 256;
        b->vy = 0;
        events |= BM_BODY_HIT_CEILING;
        break;
      }
    }
    b->onGround = false;
  }
  else
  {
    int32_t newFeet = BM_FIX_INT(ny);
    // Solid floors may be up to a step above the feet when walking; jump-through floors only
    // count when the feet were above them.
    int32_t from = wasOnGround ? feet - p->stepHeight : feet;
    int32_t to = newFeet + (wasOnGround && !b->climbing ? p->snapDistance : 0);
    int32_t floor = findFloor(b, p, c, from, to, false);
    if (!b->dropThrough)
    {
      int32_t jt = findFloor(b, p, c, feet, to, true);
      if (jt < floor) floor = jt;
    }
    if (floor != INT32_MAX)
    {
      ny = (bm_fix)floor * 256;
      b->vy = 0;
      if (!wasOnGround) events |= BM_BODY_LANDED;
      b->onGround = true;
      if (b->climbing && b->dropThrough == 0 && in.dy > 0) b->climbing = false;
    }
    else
    {
      b->onGround = false;
    }
  }
  b->y = ny;
  feet = BM_FIX_INT(b->y);

  if (b->onGround) b->coyote = p->coyoteFrames;
  else if (b->coyote) b->coyote--;

  // Materials touched (feet and body centre)
  uint8_t mFeet = bmCollisionMaterial(c, px, feet - 1);
  uint8_t mBody = bmCollisionMaterial(c, px, feet - p->height / 2);
  if (mFeet == BM_MATERIAL_DAMAGE || mBody == BM_MATERIAL_DAMAGE) events |= BM_BODY_DAMAGE;
  if (mFeet == BM_MATERIAL_DEATH || mBody == BM_MATERIAL_DEATH) events |= BM_BODY_DEATH;
  if (b->onGround && mFeet == BM_MATERIAL_NORMAL && bmCollisionMaterial(c, px, feet) == BM_MATERIAL_NORMAL)
  {
    b->safeX = px;
    b->safeY = feet;
  }
  return events;
}
