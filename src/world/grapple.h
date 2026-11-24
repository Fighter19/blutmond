/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The grapple (docs/spec/runtime-core.md §13): with the stat Grapple, R shoots a hook on a
// chain. It aims up and ahead (Up: straight up; ahead or Down held alone: level ahead) and takes
// the nearest anchor or object within 35 degrees of that and 128 pixels that nothing solid
// hides; with none, it flies the way it aims. It bites:
// - a GrappleAnchor (a point in the room): the player is pulled to it and let go there, still
//   moving a little up and on;
// - a ledge (a wall it hits within 10 pixels of a top with room above it): the player is
//   pulled up beside it and stands on it;
// - an object (not on the player's side): one whose `weight` is at most 2 is pulled to the
//   player; a heavier one (a knight, a boss) holds, and the player is pulled to it.
// Anything else, or 128 pixels without a bite: the hook comes back (a new shot meanwhile goes
// at once: from one anchor to the next; once back, after 12 frames). While it pulls the player
// they do not fall; jump lets go (a jump of 3/4 height). Where the hook is and what it holds
// is state (snapshots).

#include "shared.h"

struct BmGame;

enum {
  BM_GRAPPLE_IDLE = 0,
  BM_GRAPPLE_FLYING,
  BM_GRAPPLE_PULL_PLAYER,
  BM_GRAPPLE_PULL_OBJECT,
  BM_GRAPPLE_BACK,
};

enum {
  BM_GRAPPLE_NOTHING = 0,
  BM_GRAPPLE_ANCHOR,
  BM_GRAPPLE_LEDGE,
  BM_GRAPPLE_OBJECT,
};

#define BM_GRAPPLE_RANGE 128
#define BM_GRAPPLE_LIGHT 2       // objects of this weight or less are pulled; heavier ones hold

typedef struct BmGrapple
{
  uint8_t phase;      // BM_GRAPPLE_*
  uint8_t target;     // what it flies at or holds: BM_GRAPPLE_NOTHING, _ANCHOR, _LEDGE, _OBJECT
  uint8_t object;     // the object (actor index) for BM_GRAPPLE_OBJECT
  uint8_t timer;      // frames in this phase
  uint8_t cooldown;   // frames until it can be shot again
  int8_t side;        // a ledge: 1 the wall is right of the player, -1 left
  int32_t hookX, hookY;      // fix
  int32_t speedX, speedY;    // fix, a frame
  int16_t targetX, targetY;  // an anchor, a ledge's top corner (pixels)
  uint16_t travelled;        // pixels
  // Not state: the room's anchors (object indices), found when it is entered.
  uint8_t anchorCount;
  uint16_t anchors[8];
  int8_t sheet;              // the sprite "grapple" (animations hook, link, anchor), or -1
  int8_t anims[3];
} BmGrapple;

/// The room's anchors and the sprite, when a room is entered; `fresh`: the hook is put away.
void bmGrappleEnterRoom(struct BmGame *pGame, bool fresh);

/// A frame, before the player moves: `pressed` and `keys` as the game's. True while the grapple
/// moves the player (the body step is left out then).
bool bmGrappleFrame(struct BmGame *pGame, uint16_t keys, uint16_t pressed);

/// Draws the anchors and, while it is out, the chain and the hook.
uint8_t bmGrappleDraw(struct BmGame *pGame, uint8_t firstOam, uint8_t maxOam);
