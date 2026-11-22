/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The game's side of live reload: what the live module (src/link/live.h) needs from it.

#include "autopilot.h"
#include "game.h"
#include "live.h"

typedef struct BmGameLive
{
  BmGame *pGame;
  BmPack *pPack;           // the pack the game uses (pGame->pPack points here)
  BmAutopilot *pPilot;     // may be NULL
  // While the pack is released for one arriving over it (BmLiveHooks.releasePack): what was
  // taken from it beforehand.
  bool released;
  char releasedRoom[64];   // the room's id, to find it again in the new pack
} BmGameLive;

uint32_t bmGameLiveStateBlocks(BmGameLive *pLive, BmStateBlock *pBlocks, uint32_t max);
BmLiveHooks bmGameLiveHooks(BmGameLive *pLive);
