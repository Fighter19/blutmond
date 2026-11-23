/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Platformer physics for one body (docs/spec/room-format.md section 4). Positions and speeds
// are fixed point 24.8; the anchor is the bottom centre of the hitbox (the feet).

#include "collision.h"

typedef int32_t bm_fix;
#define BM_FIX(v) ((bm_fix)((v) * 256))
#define BM_FIX_INT(f) ((int32_t)((f) >> 8))

typedef struct BmRoomPhysics
{
  bm_fix gravity, maxFallSpeed, waterGravity, waterMaxFallSpeed;
} BmRoomPhysics;

typedef struct BmBodyParams
{
  int16_t halfWidth, height;        // hitbox in pixels: [x - halfWidth, x + halfWidth) x [y - height, y)
  bm_fix runSpeed, groundAccel, airAccel, friction;
  bm_fix jumpSpeed, jumpCut;        // jumpCut: factor on upward speed when the button is released
  bm_fix climbSpeed, gravityScale;
  uint8_t stepHeight, snapDistance; // pixels
  uint8_t coyoteFrames, jumpBufferFrames;
  // Capabilities (the stats AirJumps and WallSlide of the player, first-items.md §5):
  uint8_t airJumps;                 // jumps in the air before landing again
  bool wallSlide;                   // sliding down walls held against, jumping off them
  bm_fix wallSlideSpeed;            // fastest fall while sliding
  bm_fix wallJumpPush;              // speed away from the wall of a jump off it
} BmBodyParams;

/// Defaults from the spec (PlatformerControl).
void bmBodyDefaultParams(BmBodyParams *pParams);

typedef struct BmBodyInput
{
  int8_t dx, dy;      // -1, 0, 1 (D-pad)
  bool jumpPressed;   // pressed this frame
  bool jumpHeld;
  bool coast;         // no steering (an attack): keep the speed, no friction or air drag
} BmBodyInput;

enum {
  BM_BODY_LANDED = 1 << 0,
  BM_BODY_JUMPED = 1 << 1,
  BM_BODY_DAMAGE = 1 << 2,   // touched a damage material
  BM_BODY_DEATH = 1 << 3,    // touched a death material
  BM_BODY_HIT_WALL = 1 << 4,
  BM_BODY_HIT_CEILING = 1 << 5,
  BM_BODY_AIR_JUMPED = 1 << 6,   // with BM_BODY_JUMPED: a jump in the air
  BM_BODY_WALL_JUMPED = 1 << 7,  // with BM_BODY_JUMPED: off a wall
};

typedef struct BmBody
{
  bm_fix x, y, vx, vy;
  bool onGround, climbing, inWater, jumpCutDone;
  int8_t facing;               // -1 left, 1 right
  uint8_t coyote, jumpBuffer, dropThrough;
  int32_t safeX, safeY;        // last position standing on normal ground (respawn)
  // Gravity pulls up: the body stands on ceilings. Its box stays [y - height, y]; its feet are
  // at the top then. Platforms are stood on from below and passed going down (up + jump drops
  // through them); no ladders, no safe positions while flipped.
  bool flipped;
  uint8_t airJumpsLeft;        // jumps in the air left until the ground (or a ladder, water)
  int8_t wallSide;             // sliding on a wall: -1 on the left, 1 on the right, 0 not
  int8_t wallTouch;            // in the air within 3 pixels of a wall (wall slide): its side, 0 none
  uint8_t wallCoyote;          // frames a jump off the wall last touched is still possible
  int8_t wallCoyoteSide;       // that wall's side
} BmBody;

void bmBodyPlace(BmBody *pBody, int32_t x, int32_t y);

/// Advances the body by one frame. Returns BM_BODY_* events.
uint32_t bmBodyStep(BmBody *pBody, const BmBodyParams *pParams, const BmCollision *pCollision,
                    const BmRoomPhysics *pRoom, BmBodyInput input);
