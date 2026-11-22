/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Hits (docs/plans/hitboxes.md §3, §4): what a figure's script arms, and the step that decides,
// once a frame after scripts and animations, whom it strikes.
//
// Figures are numbered: 0 the player, 1 + i the room's scripted object i. A hit is either the
// hit boxes of one name on the frames its figure shows (`arm`) or a rectangle ahead of the
// figure for some frames (`hit_box`). Each strikes a figure at most once; a struck figure loses
// health, cannot be struck again for a while (the player 60 frames, others 20) and its own hits
// end. The target gets hit(amount), the source - the object, or the attachment that armed it -
// struck(amount).
//
// Not yet (hitboxes.md steps 3 and 4): guard boxes, #Pierce, #Multi, hit stop, knock back from
// a hit, projectiles, elements in the damage rule, defence.

#include "shared.h"

#define BM_HITS_MAX 8
#define BM_HITS_FREE 0xFF          // source of an unused hit
#define BM_HITS_PLAYER 0           // figure number of the player
#define BM_HITS_NO_SLOT 0xFF
#define BM_HITS_PLAYER_TEAM 1
#define BM_HITS_PLAYER_INVULNERABLE 60
#define BM_HITS_INVULNERABLE 20

typedef struct BmHits
{
  // State (snapshot).
  uint8_t source[BM_HITS_MAX];     // figure, or BM_HITS_FREE
  uint8_t slot[BM_HITS_MAX];       // the attachment that made it, or BM_HITS_NO_SLOT
  uint16_t name[BM_HITS_MAX];      // box name (string id); 0xFFFF: a rectangle (hit_box)
  int16_t damage[BM_HITS_MAX];
  uint8_t element[BM_HITS_MAX];
  uint8_t anim[BM_HITS_MAX];       // the source's animation when it was armed
  uint16_t frames[BM_HITS_MAX];    // a rectangle: frames left
  int16_t reach[BM_HITS_MAX];      // a rectangle: how far ahead
  uint16_t struck[BM_HITS_MAX];    // a bit per figure already struck
} BmHits;

struct BmGame;

/// No hits (a room is entered anew).
void bmHitsClear(struct BmGame *pGame);

/// Starts a hit of figure `source` (made by the attachment in `slot`, or BM_HITS_NO_SLOT):
/// `name` the boxes, or 0xFFFF and `reach`/`frames` for a rectangle. A hit of the same source
/// and name is replaced (arming again is a new hit). False if all are in use.
bool bmHitsArm(struct BmGame *pGame, uint8_t source, uint8_t slot, uint16_t name, int32_t damage, uint8_t element, int32_t reach, int32_t frames);

/// Ends the hits of `source` with box name `name`.
void bmHitsDisarm(struct BmGame *pGame, uint8_t source, uint16_t name);

/// The decision step: every live hit against the figures of other teams, then invulnerability
/// and the rectangles' frames count down.
void bmHitsFrame(struct BmGame *pGame);

/// Hits that are live now (for tests and the status display).
uint8_t bmHitsLive(const struct BmGame *pGame);
