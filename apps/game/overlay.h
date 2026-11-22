/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The debug overlay: the first user of the UI layer (src/ui/ui.h). A frame at the top with
// the room, the game frame, the player's position and health, and how much of the frame the
// game took (the lines the display drew meanwhile; only the GBA measures that). Holding L and R
// and pressing Select shows or hides it.

#include "game.h"

/// After the game started (the video is set up). `shown`: on from the start.
void bmOverlayInit(const BmPack *pPack, bool shown);

/// After each game frame: the buttons held (for the toggle) and the lines the frame took.
void bmOverlayFrame(const BmGame *pGame, uint16_t keys, uint32_t lines);
