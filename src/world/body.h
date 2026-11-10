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
} BmBodyParams;

/// Defaults from the spec (PlatformerControl).
void bmBodyDefaultParams(BmBodyParams *pParams);

typedef struct BmBodyInput
{
  int8_t dx, dy;      // -1, 0, 1 (D-pad)
  bool jumpPressed;   // pressed this frame
  bool jumpHeld;
} BmBodyInput;

enum {
  BM_BODY_LANDED = 1 << 0,
  BM_BODY_JUMPED = 1 << 1,
  BM_BODY_DAMAGE = 1 << 2,   // touched a damage material
  BM_BODY_DEATH = 1 << 3,    // touched a death material
  BM_BODY_HIT_WALL = 1 << 4,
  BM_BODY_HIT_CEILING = 1 << 5,
};

typedef struct BmBody
{
  bm_fix x, y, vx, vy;
  bool onGround, climbing, inWater, jumpCutDone;
  int8_t facing;               // -1 left, 1 right
  uint8_t coyote, jumpBuffer, dropThrough;
  int32_t safeX, safeY;        // last position standing on normal ground (respawn)
} BmBody;

void bmBodyPlace(BmBody *pBody, int32_t x, int32_t y);

/// Advances the body by one frame. Returns BM_BODY_* events.
uint32_t bmBodyStep(BmBody *pBody, const BmBodyParams *pParams, const BmCollision *pCollision,
                    const BmRoomPhysics *pRoom, BmBodyInput input);
