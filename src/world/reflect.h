/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// A puddle that mirrors the room (room-format.md §1 "Puddle", runtime-core.md §12): an
// object of class `Puddle` whose top edge is the water line. The room's pixels inside it are
// left empty (transparent in every room layer), and a free hardware background shows the
// mirrored layer there: it shares that layer's map and tiles, and a line table sets its
// vertical scroll line by line so that each line of the puddle shows a row above the water
// line, `squash` rows up per line (a puddle seen from the side is thin: what it mirrors is
// squeezed). It lies behind the room's layers, so it shows only where they are empty, and is
// blended over the backdrop (the water's colour) on the puddle's lines (a second line table,
// BLDALPHA). The player and the actors over it are mirrored by affine sprites, squeezed the
// same way (game.c, actors.c).
//
// Needs a free background (a room of at most two layers, without a light layer) and line table
// slots 0 and 1, so it is off while a room effect or a teleport uses them.

#include "shared.h"

struct BmGame;

// The affine matrices of what is mirrored: one facing right, one facing left (the doors use
// the others, 0 to 29).
#define BM_REFLECT_MATRIX(hflip) ((uint8_t)((hflip) ? 30 : 31))

typedef struct BmReflection
{
  bool on;                // the room has a puddle and a background free for it
  bool shown;             // its tables are set (this frame)
  uint8_t bg;             // the hardware background showing the mirror
  uint8_t layer;          // the room layer it mirrors
  int16_t x, y, w, h;     // the puddle in room pixels; y is the water line
  uint8_t squash;         // rows above the water line per line of the puddle, 16 = 1.0
  uint8_t ripple;         // how far the rows waver, in pixels
  uint8_t eva, evb;       // the blend on the puddle's lines (sixteenths): mirror, water
  int32_t builtCamera;    // the camera row the blend table was built for (-1: none)
} BmReflection;

/// Finds the room's puddle (the first `Puddle` object) and sets up the background for it.
void bmReflectEnterRoom(struct BmGame *pGame);

/// Every frame after the room's scroll and the light: the mirror's scroll per line and the
/// blend on its lines (or nothing while effects use the line tables).
void bmReflectPresent(struct BmGame *pGame);

/// Whether something at room x from `left` to `right` and standing at room y could be mirrored;
/// `pSurface` the water line on screen and `pSquash` the squeeze then.
bool bmReflectMirrors(const struct BmGame *pGame, int32_t left, int32_t right, int32_t y, int32_t *pSurface, uint8_t *pSquash);
