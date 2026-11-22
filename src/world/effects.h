/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Screen effects of a room (docs/spec/runtime-core.md §12), made of the GBA's line tables,
// windows and colour effects, and the teleport that uses the wave:
//
//   wave       the room's backgrounds sway, line by line (a: pixels, at most 8; b: speed)
//   spotlight  darkness except for a circle around the player (a: radius, b: darkness 0-16)
//   pulse      the room slowly brightens and darkens (a: strength 0-16, b: speed)
//   gradient   a dusk: darker towards the top, line by line (a: darkness at the top 0-16)
//
// A room chooses one with its map property `effect` (and `effect_a`, `effect_b`), a script with
// screen_effect(#name, a, b) (#none ends it). What is shown follows from the game state alone;
// the UI layer (background 3, the HUD and menus) is never touched.
//
// A teleport (teleport(#room, x, y)) takes BM_TELEPORT_FRAMES: the screen waves more and more
// and turns white, the player is moved at the middle (into another room if asked), then the
// wave calms down. The player cannot walk or be hurt meanwhile.

#include "shared.h"

struct BmGame;

#define BM_TELEPORT_FRAMES 64
#define BM_EFFECT_WAVE_MAX 8   // pixels: what the room's streamed map has to spare

typedef enum BmEffect {
  BM_EFFECT_NONE = 0,
  BM_EFFECT_WAVE,
  BM_EFFECT_SPOTLIGHT,
  BM_EFFECT_PULSE,
  BM_EFFECT_GRADIENT,
} BmEffect;

/// The effect by name ("wave", ...), or -1.
int32_t bmEffectNamed(const char *name);

/// Sets the room's effect (a and b 0: the effect's defaults).
void bmEffectsSet(struct BmGame *pGame, BmEffect effect, int32_t a, int32_t b);

/// A room was entered fresh: its `effect` property, else none.
void bmEffectsEnterRoom(struct BmGame *pGame);

/// Starts a teleport to room `roomIndex` (the room the player is in: within it) at (x, y).
/// False if one is under way or there is no such room.
bool bmEffectsTeleport(struct BmGame *pGame, uint32_t roomIndex, int32_t x, int32_t y);

/// Once a frame, before the player moves: a teleport's progress (the move at its middle).
void bmEffectsFrame(struct BmGame *pGame);

/// Once a frame after the camera and the sprites: line tables, windows and colour effects as
/// the effect (or a teleport) wants them. Also after a state was loaded.
void bmEffectsPresent(struct BmGame *pGame);

/// Doors with a panel (the sprite their `panel` property names, its anchor the hinge at the
/// bottom): drawn as if turning about the hinge, an affine sprite scaled in x only, standing on
/// the ground under the door. Shut but while the player goes through (game.h, doorSeq) and
/// while it closes behind them (doorArrive): `direction` east and west doors stand in the side
/// wall (narrow: their depth shows 0.4 wide) and swing into the room; others (north) face the
/// screen and turn away, narrowing, in front of the dark of the opening (the sprite's frame 1,
/// if any). Scaled to the door's height.
/// Returns the OAM entries used, from `firstOam`.
uint8_t bmEffectsDrawDoors(struct BmGame *pGame, uint8_t firstOam, uint8_t maxOam);

/// How open a door is now: 0 shut to 256 open (12 frames into a walk through it; closing in 16
/// behind the player).
int32_t bmEffectsDoorOpening(const struct BmGame *pGame, uint16_t object);

/// How far the wave pulls the room's backgrounds left on screen line `y` now, 0 to
/// BM_EFFECT_WAVE_MAX (the player's sprite sways with it).
int32_t bmEffectsWaveAt(const struct BmGame *pGame, int32_t y);
