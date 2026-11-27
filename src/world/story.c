/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// What scripts tell a story with (game.h, bmGameStoryCall): the player turned to a side by a
// scene, the room's light changed (a lamp switched on, a window lit: light.h), and story flags (docs/plans/dialogue.md §5, a first part). A flag is on while the
// inventory holds one of its key, the FNV-1a hash of its name changed so that no record has
// it (no menu lists it): the same in every room, kept in the save game.

#include "actors.h"
#include "game.h"
#include "inventory.h"
#include "script_api.h"

int32_t bmGameStoryCall(BmGame *g, uint8_t current, uint32_t api, const int32_t *args)
{
  if (api == BM_SCRIPT_API_set_light)
  {
    bmLightSet(g, args[0], args[1], args[2]);
    return 0;
  }
  if (api == BM_SCRIPT_API_set_glow)
  {
    bmLightGlow(g, args[0]);
    return 0;
  }
  if (api == BM_SCRIPT_API_draw_in_front)
  {
    if (current < BM_ACTORS_MAX) g->actors.front[current] = args[0] != 0;
    return 0;
  }
  if (api == BM_SCRIPT_API_owner_turn)
  {
    // From an attachment, the player's or the room's (a scene drawing the player in a place).
    if ((current == BM_ACTORS_OWNER || current == BM_ACTORS_ROOM) && args[0] != 0) g->player.facing = (int8_t)(args[0] < 0 ? -1 : 1);
    return 0;
  }
  uint32_t h = 2166136261u;
  for (const char *p = bmPackString(g->pPack, (uint16_t)args[0]); p != NULL && *p != 0; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  h ^= 0x464C4147u;   // "FLAG"
  bool on = bmInventoryCount(&g->inventory, h) != 0;
  if (api == BM_SCRIPT_API_flag) return on;
  if (args[1] != 0 && !on) bmInventoryGive(&g->inventory, h, 1);
  else if (args[1] == 0 && on) bmInventoryTake(&g->inventory, h, 1);
  return 0;
}
