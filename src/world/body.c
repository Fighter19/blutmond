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
  p->airJumps = 0;
  p->wallSlide = false;
  p->wallSlideSpeed = BM_FIX(1);
  p->wallJumpPush = BM_FIX(2);
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

// Topmost floor under the body's feet sensors (the box's left and right column).
static int32_t findFloor(const BmBody *b, const BmBodyParams *p, const BmCollision *c, int32_t yFrom, int32_t yTo, bool jumpThrough)
{
  int32_t x = BM_FIX_INT(b->x);
  int32_t best = INT32_MAX;
  int32_t columns[2] = { x - p->halfWidth, x + p->halfWidth - 1 };
  for (int i = 0; i < 2; i++)
  {
    int32_t f = bmCollisionFloor(c, columns[i], yFrom, yTo, jumpThrough);
    if (f < best) best = f;
  }
  return best;
}

// Flipped: the lowest ceiling over the feet (now on top) for the two sensors, scanning from
// yFrom up to yTo (yTo <= yFrom): the y just below the first solid pixel, or INT32_MIN. With
// `jumpThrough` the platforms solid only on top count too (their underside, as a floor is
// their top the usual way up).
static int32_t findCeiling(const BmBody *b, const BmBodyParams *p, const BmCollision *c, int32_t yFrom, int32_t yTo, bool jumpThrough)
{
  int32_t x = BM_FIX_INT(b->x);
  int32_t best = INT32_MIN;
  int32_t columns[2] = { x - p->halfWidth, x + p->halfWidth - 1 };
  for (int i = 0; i < 2; i++)
  {
    for (int32_t y = yFrom - 1; y >= yTo - 1; y--)
    {
      // A platform only by its underside (its last row): passing through it from inside, the
      // body does not catch in it (as a floor counts only by its top).
      if (!bmCollisionSolidSides(c, columns[i], y) &&
          !(jumpThrough && bmCollisionSolidPlatform(c, columns[i], y) && !bmCollisionSolidPlatform(c, columns[i], y + 1)))
        continue;
      if (y + 1 > best) best = y + 1;
      break;
    }
  }
  return best;
}

// Wall check for a column: from just past step height to the head (s: 1, or -1 flipped). In
// the air there is no stepping up: from the feet (else a diagonal jump moves the body into a
// block's top corner, up to step height, before the floor is found under it).
static bool wallAt(const BmCollision *c, int32_t x, int32_t feet, const BmBodyParams *p, int32_t s, bool onGround)
{
  for (int32_t k = onGround ? p->stepHeight + 1 : 1; k < p->height; k += 7)
  {
    if (bmCollisionSolidSides(c, x, feet - s * k)) return true;
  }
  return bmCollisionSolidSides(c, x, feet - s * (p->height - 1));
}

uint32_t bmBodyStep(BmBody *b, const BmBodyParams *p, const BmCollision *c, const BmRoomPhysics *room, BmBodyInput in)
{
  uint32_t events = 0;
  int32_t cs = c->cellSize;
  // s: which way is up (1 as usual, -1 with gravity flipped); fy: the feet (the box's top when
  // flipped), what this works with; b->y is set from it at the end.
  int32_t s = b->flipped ? -1 : 1;
  bm_fix fy = b->flipped ? b->y - (bm_fix)p->height * 256 : b->y;
  int32_t px = BM_FIX_INT(b->x), feet = BM_FIX_INT(fy);
  bool wasOnGround = b->onGround;

  // Surroundings
  int32_t centerY = feet - s * p->height / 2;
  b->inWater = bmCollisionMaterial(c, px, centerY) == BM_MATERIAL_WATER;
  bool onLadder = !b->flipped && (bmCollisionMaterial(c, px, centerY) == BM_MATERIAL_LADDER ||
                                  bmCollisionMaterial(c, px, feet - 1) == BM_MATERIAL_LADDER);
  bool ladderBelow = !b->flipped && bmCollisionMaterial(c, px, feet + 1) == BM_MATERIAL_LADDER;
  if (b->flipped) b->climbing = false;

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
    b->airJumpsLeft = p->airJumps;
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
    else if (in.coast) {}   // keeps its speed
    else if (wasOnGround) b->vx = approach(b->vx, 0, p->friction);
    else b->vx = approach(b->vx, 0, p->airAccel / 2);

    // Back on the ground (or a ladder, in water): the jumps in the air are there again.
    if (wasOnGround || b->inWater) b->airJumpsLeft = p->airJumps;
    // Jumping (with coyote time and a jump buffer); down + jump drops through platforms.
    if (b->jumpBuffer && (wasOnGround || b->coyote))
    {
      // Toward the feet (down; up with gravity flipped) and jump: through the platform.
      bool onJumpThroughOnly = wasOnGround && (b->flipped ? findCeiling(b, p, c, feet, feet, false) == INT32_MIN
                                                          : findFloor(b, p, c, feet, feet, false) == INT32_MAX);
      if (s * in.dy > 0 && onJumpThroughOnly)
      {
        b->dropThrough = 8;
      }
      else
      {
        b->vy = -s * p->jumpSpeed;
        b->jumpCutDone = false;
        events |= BM_BODY_JUMPED;
      }
      b->onGround = false;
      wasOnGround = false;
      b->coyote = 0;
      b->jumpBuffer = 0;
    }
    else if (b->jumpBuffer && b->wallSide != 0 && p->wallSlide)
    {
      // Off the wall slid on: up, and pushed away from it.
      b->vy = -s * p->jumpSpeed;
      b->vx = -b->wallSide * p->wallJumpPush;
      b->facing = (int8_t)-b->wallSide;
      b->jumpCutDone = false;
      b->jumpBuffer = 0;
      b->wallSide = 0;
      events |= BM_BODY_JUMPED | BM_BODY_WALL_JUMPED;
    }
    else if (b->jumpBuffer && b->airJumpsLeft != 0)
    {
      // A jump in the air (Double Jump), as high as one from the ground.
      b->vy = -s * p->jumpSpeed;
      b->airJumpsLeft--;
      b->jumpCutDone = false;
      b->jumpBuffer = 0;
      events |= BM_BODY_JUMPED | BM_BODY_AIR_JUMPED;
    }
    if (!in.jumpHeld && s * b->vy < 0 && !b->jumpCutDone)
    {
      b->vy = fixMul(b->vy, p->jumpCut);
      b->jumpCutDone = true;
    }

    // Gravity
    bm_fix g = fixMul(b->inWater ? room->waterGravity : room->gravity, p->gravityScale);
    bm_fix maxFall = b->inWater ? room->waterMaxFallSpeed : room->maxFallSpeed;
    b->vy += s * g;
    if (s * b->vy > maxFall) b->vy = s * maxFall;
    // Sliding down a wall held against: slowly.
    if (p->wallSlide && b->wallSide != 0 && s * b->vy > p->wallSlideSpeed) b->vy = s * p->wallSlideSpeed;
  }
  if (b->jumpBuffer) b->jumpBuffer--;

  // --- Horizontal movement and walls ---
  bm_fix nx = b->x + b->vx;
  if (b->vx != 0)
  {
    int32_t nPx = BM_FIX_INT(nx);
    int32_t edge = b->vx > 0 ? nPx + p->halfWidth - 1 : nPx - p->halfWidth;
    if (wallAt(c, edge, feet, p, s, wasOnGround))
    {
      // Against a cell: at its edge; against a solid box (a push block): at the box's.
      int32_t wallX = floorDiv(edge, cs) * cs, wallW = cs;
      BmSolidBox box;
      int32_t top = s > 0 ? feet - p->height + 1 : feet, bottom = s > 0 ? feet - 1 : feet + p->height - 1;
      if (bmCollisionBoxAt(c, edge, top, bottom, &box))
      {
        wallX = box.x;
        wallW = box.w;
      }
      nx = b->vx > 0 ? (bm_fix)(wallX - p->halfWidth) * 256 : (bm_fix)(wallX + wallW + p->halfWidth) * 256;
      b->vx = 0;
      events |= BM_BODY_HIT_WALL;
    }
  }
  b->x = nx;
  px = BM_FIX_INT(b->x);
  // A wall slide: in the air, falling (or about to), pushing against a wall.
  b->wallSide = 0;
  if (p->wallSlide && !wasOnGround && !b->climbing && !b->inWater && in.dx != 0 && s * b->vy >= 0)
  {
    int32_t edge = in.dx > 0 ? px + p->halfWidth : px - p->halfWidth - 1;
    if (wallAt(c, edge, feet, p, s, false)) b->wallSide = in.dx;
  }

  // --- Vertical movement, floors and ceilings ---
  bm_fix ny = fy + b->vy;
  if (b->flipped)
  {
    // Mirrored: rising is moving down, the "floor" is a ceiling (solid from below).
    if (b->vy > 0)
    {
      int32_t head = BM_FIX_INT(ny) + p->height - 1;
      int32_t columns[3] = { px - p->halfWidth, px, px + p->halfWidth - 1 };
      for (int i = 0; i < 3; i++)
      {
        if (bmCollisionSolidSides(c, columns[i], head))
        {
          ny = (bm_fix)(floorDiv(head, cs) * cs - p->height) * 256;
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
      int32_t from = wasOnGround ? feet + p->stepHeight : feet;
      int32_t to = newFeet - (wasOnGround ? p->snapDistance : 0);
      // Platforms only count when the feet were not past them yet (as a jump-through floor).
      int32_t ceiling = findCeiling(b, p, c, from, to, false);
      if (!b->dropThrough)
      {
        int32_t jt = findCeiling(b, p, c, feet, to, true);
        if (jt != INT32_MIN && jt > ceiling) ceiling = jt;
      }
      if (ceiling != INT32_MIN)
      {
        ny = (bm_fix)ceiling * 256;
        b->vy = 0;
        if (!wasOnGround) events |= BM_BODY_LANDED;
        b->onGround = true;
      }
      else
      {
        b->onGround = false;
      }
    }
  }
  else if (b->vy < 0)
  {
    int32_t head = BM_FIX_INT(ny) - p->height;
    int32_t columns[3] = { px - p->halfWidth, px, px + p->halfWidth - 1 };
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
  b->y = b->flipped ? ny + (bm_fix)p->height * 256 : ny;
  feet = BM_FIX_INT(ny);

  if (b->onGround) b->coyote = p->coyoteFrames;
  else if (b->coyote) b->coyote--;

  // Materials touched (feet and body centre)
  uint8_t mFeet = bmCollisionMaterial(c, px, b->flipped ? feet : feet - 1);
  uint8_t mBody = bmCollisionMaterial(c, px, feet - s * p->height / 2);
  if (mFeet == BM_MATERIAL_DAMAGE || mBody == BM_MATERIAL_DAMAGE) events |= BM_BODY_DAMAGE;
  if (mFeet == BM_MATERIAL_DEATH || mBody == BM_MATERIAL_DEATH) events |= BM_BODY_DEATH;
  if (!b->flipped && b->onGround && mFeet == BM_MATERIAL_NORMAL && bmCollisionMaterial(c, px, feet) == BM_MATERIAL_NORMAL)
  {
    b->safeX = px;
    b->safeY = feet;
  }
  return events;
}
