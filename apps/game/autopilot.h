/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Plays the test level by itself (for tests and demos): runs right, jumps over pits, walls and
// spikes, climbs ladders; in the second room it fights the bat, then goes on east; in the boss
// hall it presses A through the dialogue, fights the moon bat, takes what it leaves and puts it
// on in the Equipment menu. It only reads the game state and returns buttons, so the same
// sequence of frames happens on the host and on the GBA.

#include "game.h"
#include "state.h"

typedef struct BmAutopilot
{
  uint16_t jumpHold;   // frames the jump button stays held
  uint16_t idle;       // frames spent in the last room before stopping
  uint16_t menu;       // the step of putting on what the boss left
} BmAutopilot;

uint16_t bmAutopilotKeys(BmAutopilot *pPilot, const BmGame *pGame);

/// The autopilot's state as a schema block, so snapshots continue the same run.
BmStateBlock bmAutopilotStateBlock(BmAutopilot *pPilot);
