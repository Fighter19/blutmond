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
#include "video.h"

#include <string.h>

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
  if (api == BM_SCRIPT_API_set_snow)
  {
    g->snow = (uint8_t)(args[0] < 0 ? 0 : args[0] > 100 ? 100 : args[0]);
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

// The snow (set_snow): up to SNOW_FLAKES flakes of the built-in snowflake tile (after the
// figure and the placeholder, game.c), each falling at its own speed, drifting with the wind
// and swaying, wrapping round the screen; they move against the camera a little (far snow).
#define SNOW_FLAKES 24
#define SNOW_TILE 12

uint8_t bmGameDrawSnow(BmGame *g, uint8_t first, uint8_t max)
{
  uint8_t n = (uint8_t)(g->snow * SNOW_FLAKES / 100);
  if (n > max) n = max;
  if (n == 0) return 0;
  if (!g->snowTile)
  {
    // A soft flake: a plus of white (colour 2 of the placeholder's bank), 3 x 3 in an 8 x 8 tile.
    uint8_t flake[32];
    memset(flake, 0, sizeof(flake));
    flake[0 * 4 + 0] = 0x20;   // (1, 0)
    flake[1 * 4 + 0] = 0x22;   // (0, 1), (1, 1)
    flake[1 * 4 + 1] = 0x02;   // (2, 1)
    flake[2 * 4 + 0] = 0x20;   // (1, 2)
    bmTilesLoadObj((uint16_t)(g->builtinTile + SNOW_TILE), flake, sizeof(flake));
    g->snowTile = true;
  }
  uint32_t f = g->frame;
  int32_t cam = g->room.cameraX;
  for (uint8_t i = 0; i < n; i++)
  {
    uint32_t speed = 2u + (i * 7u) % 3u;                  // quarter pixels a frame: 2 to 4
    int32_t sway = (int32_t)(((f / 12u + i) & 7u));        // 0..7, a triangle
    if (sway > 4) sway = 8 - sway;
    int32_t x = (int32_t)((i * 97u + f / 6u) % 256u) - cam / 2 + sway;
    int32_t y = (int32_t)((i * 61u + f * speed / 4u) % 176u) - 8;
    x = ((x % 256) + 256) % 256 - 8;
    BmSprite s = { .x = (int16_t)x, .y = (int16_t)y, .shape = BM_SPRITE_SQUARE, .size = 0,
                   .tile = (uint16_t)(g->builtinTile + SNOW_TILE), .paletteBank = 15, .priority = 1, .visible = true };
    bmSpriteSet((uint8_t)(first + i), &s);
  }
  return n;
}

