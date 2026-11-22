/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "game_live.h"

#include <string.h>

uint32_t bmGameLiveStateBlocks(BmGameLive *gl, BmStateBlock *pBlocks, uint32_t max)
{
  uint32_t n = 0;
  if (n < max) pBlocks[n++] = bmGameStateBlock(gl->pGame);
  if (gl->pPilot != NULL && n < max) pBlocks[n++] = bmAutopilotStateBlock(gl->pPilot);
  return n;
}

static uint32_t stateBlocks(void *pContext, BmStateBlock *pBlocks, uint32_t max)
{
  return bmGameLiveStateBlocks(pContext, pBlocks, max);
}

static bool usePack(void *pContext, const BmPack *pPack, BmLiveMode mode, const char *roomId)
{
  BmGameLive *gl = pContext;
  BmGame *g = gl->pGame;
  if (gl->released)
  {
    // The old pack is gone: nothing to fall back to. On failure the game keeps waiting.
    // Started without a pack (a runtime image waiting for one): any mode starts the game.
    bool neverStarted = g->pPack == NULL;
    *gl->pPack = *pPack;
    if (mode == BM_LIVE_RESTART || neverStarted)
    {
      if (bmGameStart(g, gl->pPack, roomId) != BM_SUCCESS) return false;
      if (gl->pPilot != NULL) memset(gl->pPilot, 0, sizeof(*gl->pPilot));
    }
    else
    {
      // What bmActorsPrepareReload kept at the release serves a module reload only.
      if (mode != BM_LIVE_MODULE) g->actors.keep = false;
      int32_t index = bmGameFindRoom(pPack, gl->releasedRoom);
      if (index < 0) return false;
      g->roomIndex = (uint32_t)index;
      if (bmGameAfterLoad(g) != BM_SUCCESS) return false;
    }
    gl->released = false;
    return true;
  }
  if (mode == BM_LIVE_RESTART)
  {
    BmPack old = *gl->pPack;
    *gl->pPack = *pPack;
    if (bmGameStart(g, gl->pPack, roomId) == BM_SUCCESS)
    {
      if (gl->pPilot != NULL) memset(gl->pPilot, 0, sizeof(*gl->pPilot));
      return true;
    }
    *gl->pPack = old;
    bmGameStart(g, gl->pPack, NULL);
    return false;
  }
  // Hot: state kept, the current room (found again by its id) reloaded around the player.
  // Module: the same, and scripted objects whose class did not change keep running.
  if (mode == BM_LIVE_MODULE) bmActorsPrepareReload(g);
  char room[64];
  const char *current = bmGameRoomId(g);
  strncpy(room, current != NULL ? current : "", sizeof(room) - 1);
  room[sizeof(room) - 1] = 0;
  int32_t index = bmGameFindRoom(pPack, room);
  if (index < 0) return false;
  BmPack old = *gl->pPack;
  uint32_t oldIndex = g->roomIndex;
  *gl->pPack = *pPack;
  g->roomIndex = (uint32_t)index;
  if (bmGameAfterLoad(g) == BM_SUCCESS) return true;
  *gl->pPack = old;
  g->roomIndex = oldIndex;
  bmGameAfterLoad(g);
  return false;
}

static void releasePack(void *pContext)
{
  BmGameLive *gl = pContext;
  BmGame *g = gl->pGame;
  const char *current = bmGameRoomId(g);
  strncpy(gl->releasedRoom, current != NULL ? current : "", sizeof(gl->releasedRoom) - 1);
  gl->releasedRoom[sizeof(gl->releasedRoom) - 1] = 0;
  bmActorsPrepareReload(g);   // the objects' classes and layouts, for a module reload
  gl->released = true;
}

static bool afterStateLoad(void *pContext)
{
  BmGameLive *gl = pContext;
  return bmGameAfterLoad(gl->pGame) == BM_SUCCESS;
}

static void status(void *pContext, uint32_t *pFrame, uint32_t *pRoom, int16_t *pX, int16_t *pY)
{
  BmGameLive *gl = pContext;
  *pFrame = gl->pGame->frame;
  *pRoom = gl->pGame->roomIndex;
  *pX = (int16_t)BM_FIX_INT(gl->pGame->player.x);
  *pY = (int16_t)BM_FIX_INT(gl->pGame->player.y);
}

BmLiveHooks bmGameLiveHooks(BmGameLive *gl)
{
  return (BmLiveHooks){ gl, stateBlocks, usePack, releasePack, afterStateLoad, status };
}
