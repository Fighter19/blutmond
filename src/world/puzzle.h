/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Physics puzzles (docs/spec/room-format.md §3.9, §5): blocks the player pushes, plates the
// blocks (or the player) press, gates the plates hold open.
//
// PushBlock: a rectangle, solid to the pixel (a solid box of the collision grid). Held against
// (the player on the ground walks into its side) for `push_frames` frames (6 unless the object
// says), it moves that way at 3/4 of a pixel a frame for as long as the player keeps pushing,
// up to what stops it (a wall, another block, a closed gate, the room's edge); with nothing
// under any part of it, it falls with gravity (up to 4 pixels a frame) and lands on what is
// below. It never moves into the player.
// PressurePlate: a rectangle on a floor, pressed while a block or the player standing stands
// on it; its `gate` names the Gate it opens. Gate: a rectangle of whole cells, solid while
// closed; open (a plate pressed) it slides up out of the way at 2 pixels a frame, and back down
// when no plate holds it, unless the player is under it.
//
// Drawn with the sprite "puzzle" (animations block, gate, plate, plate_down; frames anchored
// at their top left). Where things are is state (snapshots), not kept in the save game: a
// puzzle is as it was made when its room is entered again.

#include "shared.h"
#include "collision.h"

#define BM_PUZZLE_BLOCKS 4
#define BM_PUZZLE_GATES 4
#define BM_PUZZLE_PLATES 4

struct BmGame;

typedef struct BmPuzzle
{
  // Of the room's objects (found when it is entered).
  uint8_t blockCount, gateCount, plateCount;
  uint16_t blockObject[BM_PUZZLE_BLOCKS];
  uint8_t blockPushFrames[BM_PUZZLE_BLOCKS];
  uint16_t gateObject[BM_PUZZLE_GATES];
  uint16_t plateObject[BM_PUZZLE_PLATES];
  uint8_t plateGate[BM_PUZZLE_PLATES];   // the gate it opens, 0xFF: none
  int8_t sheet;                          // the sprite "puzzle", or -1
  int8_t anims[4];                       // block, gate, plate, plate_down
  // State: where the blocks are (their top left, pixels, and 1/256 of a pixel across), how
  // fast each falls (1/256 pixels a frame), how long the player has pushed each; how far each
  // gate is lifted; which plates are pressed (bits).
  int16_t blockX[BM_PUZZLE_BLOCKS], blockY[BM_PUZZLE_BLOCKS];
  uint8_t blockFraction[BM_PUZZLE_BLOCKS];
  int16_t blockFall[BM_PUZZLE_BLOCKS];
  uint8_t blockPush[BM_PUZZLE_BLOCKS];
  uint8_t gateLift[BM_PUZZLE_GATES];
  uint8_t pressed;
  // Not state: the way the player pushes a block this frame (-1, 1; 0: none), for their
  // animation; the blocks as solid boxes for the collision grid.
  int8_t pushing;
  BmSolidBox boxes[BM_PUZZLE_BLOCKS];
} BmPuzzle;

/// The room's blocks, plates and gates when it is entered; `fresh` (not a state just loaded):
/// they are where the room puts them.
void bmPuzzleEnterRoom(struct BmGame *pGame, bool fresh);

/// A frame, before the player moves: `dx` is the way the player walks (-1, 0, 1). Blocks are
/// pushed and fall, plates are pressed, gates open and close; the collision grid's overrides
/// are made again (also the broken walls': bmPuzzleOverrides).
void bmPuzzleFrame(struct BmGame *pGame, int8_t dx);

/// Makes the collision overrides again (broken walls, closed gates) and the blocks' solid
/// boxes.
void bmPuzzleOverrides(struct BmGame *pGame);

/// Draws blocks, plates and gates; returns the hardware sprites used from `firstOam`.
uint8_t bmPuzzleDraw(struct BmGame *pGame, uint8_t firstOam, uint8_t maxOam);
