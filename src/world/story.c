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
#include "light.h"
#include "room.h"
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

// --- The snowy world: the room's colours wintry, snow lying on what is open to the sky --------

// A colour (BGR555) under snow, `level` 1 to 4 (0: as it is): its saturation taken away, three
// quarters at most (no green grass, no warm brick: grey-blue), lifted to a cold blue-white, and the light
// colours further toward white (the snow lying everywhere throws the light back).
uint16_t bmGameSnowColour(uint8_t level, uint16_t c)
{
  if (level == 0) return c;
  int32_t ch[3] = { c & 31, (c >> 5) & 31, (c >> 10) & 31 };
  int32_t luma = (ch[0] * 5 + ch[1] * 9 + ch[2] * 2) >> 4;
  static const int8_t lift[3] = { 2, 3, 5 };
  for (int i = 0; i < 3; i++)
  {
    int32_t v = ch[i] + (luma - ch[i]) * (int32_t)level * 3 / 16;     // grey, three quarters at level 4
    v += lift[i] * (int32_t)level / 4;                                // cold
    if (luma >= 14) v += (31 - v) * (luma - 13) * (int32_t)level / 96; // the light ones whiter
    ch[i] = v;
  }
  // Green (grass, leaves): under snow, lighter by half again and as white as the rest.
  int32_t green = (c >> 5 & 31) - ((c & 31) > (c >> 10 & 31) ? (c & 31) : (c >> 10 & 31));
  if (green > 0)
  {
    int32_t w = green * 4 * (int32_t)level;   // of 64
    if (w > 48) w = 48;
    int32_t snow = luma * 3 / 2 + 2;
    for (int i = 0; i < 3; i++) ch[i] += (snow + lift[i] - ch[i]) * w / 64;
  }
  for (int i = 0; i < 3; i++) ch[i] = ch[i] < 0 ? 0 : ch[i] > 31 ? 31 : ch[i];
  return (uint16_t)(ch[0] | ch[1] << 5 | ch[2] << 10);
}

// Snow lying on the room's tiles: the tiles of the main and foreground layers that lie open to
// the sky (nothing above them in their layer) more often than not get snow over their top: in
// each pixel column the first three drawn pixels from the top (four in some, uneven), in the
// lightest colour of their palette bank under snow. Their copies in video memory change (the
// room's next showing brings them back as they were); at most SNOW_CAPS tiles.
#define SNOW_CAPS 160
#define SNOW_CAP_LEVEL 2   // from a snow of 26 % on

// Per background bank the colour the snow lying is drawn in (0: none): white under snow.
static uint8_t s_snowWhite[16] BM_EWRAM_BSS;

uint16_t bmGameSnowColourAt(const BmGame *g, uint8_t bank, uint8_t index, uint16_t c)
{
  if (g->snowLevel == 0) return c;
  if (bank < 16 && index != 0 && s_snowWhite[bank] == index)
  {
    // Snow: a cold white, not as bright as a lamp (it is night).
    static const uint8_t white[3] = { 22, 24, 29 };
    int32_t ch[3] = { c & 31, (c >> 5) & 31, (c >> 10) & 31 };
    for (int i = 0; i < 3; i++) ch[i] += (white[i] - ch[i]) * (int32_t)g->snowLevel / 4;
    return (uint16_t)(ch[0] | ch[1] << 5 | ch[2] << 10);
  }
  return bmGameSnowColour(g->snowLevel, c);
}

typedef struct SnowCap
{
  uint16_t tile;
  uint8_t bank;
  uint8_t open, covered;   // its cells open to the sky, under something (each to 255)
} SnowCap;

static uint16_t layerCell(const BmRoomLayer *l, int32_t x, int32_t y)
{
  if (x < 0 || y < 0 || x >= l->width8 || y >= l->height8) return 0;
  return bmRead16(l->pEntries + ((uint32_t)y * l->width8 + (uint32_t)x) * 2u);
}

static void snowCaps(BmGame *g)
{
  const BmRoom *r = &g->room;
  uint32_t tilesSize = 0, palSize = 0;
  const uint8_t *tiles = bmPackChunkData(r->pPack, r->tilesChunk, &tilesSize);
  const uint8_t *pal = bmPackChunkData(r->pPack, r->paletteChunk, &palSize);
  if (tiles == NULL || pal == NULL || tilesSize < 4 || palSize < 4 || palSize < 4u + pal[2] * 32u) return;
  uint32_t tileCount = bmRead16(tiles);
  static SnowCap caps[SNOW_CAPS] BM_EWRAM_BSS;
  uint16_t n = 0;
  // Open to the sky: the cells with an empty one above (not flipped upside down).
  for (uint8_t li = 0; li < r->layerCount; li++)
  {
    const BmRoomLayer *l = &r->layers[li];
    if (l->role != BM_ROOM_ROLE_MAIN && l->role != BM_ROOM_ROLE_FOREGROUND) continue;
    for (int32_t y = 0; y < l->height8; y++)
      for (int32_t x = 0; x < l->width8; x++)
      {
        uint16_t e = layerCell(l, x, y);
        uint16_t t = e & 0x3FF;
        if (t == 0 || (e & 0x800) || t >= tileCount) continue;
        bool open = (layerCell(l, x, y - 1) & 0x3FF) == 0;
        uint16_t k = 0;
        while (k < n && caps[k].tile != t) k++;
        if (k == n)
        {
          if (!open || n == SNOW_CAPS) continue;
          caps[n++] = (SnowCap){ t, (uint8_t)(e >> 12), 0, 0 };
        }
        if (open && caps[k].open < 255) caps[k].open++;
        if (!open && caps[k].covered < 255) caps[k].covered++;
      }
  }
  // (A tile first seen under something and later open counts only from then: near enough.)
  for (uint16_t k = 0; k < n; k++)
  {
    if (caps[k].covered > caps[k].open) continue;
    uint8_t bank = caps[k].bank;
    if (bank < pal[1] || bank >= pal[1] + pal[2]) continue;
    // The lightest colour of its bank under snow.
    const uint8_t *colours = pal + 4 + (bank - pal[1]) * 32u;
    uint8_t white = 1;
    int32_t best = -1;
    for (uint8_t i = 1; i < 16; i++)
    {
      // (Lit: as light as it is shown, by its level; not what glows.)
      uint8_t level = bmLightLevelOf(g, bank, i);
      if (level == 0xFF) continue;
      uint16_t c = bmGameSnowColour(4, bmRead16(colours + i * 2));
      int32_t luma = (c & 31) * 5 + ((c >> 5) & 31) * 9 + ((c >> 10) & 31) * 2;
      if (level != 0xFE) luma *= level + 1;
      if (luma > best) best = luma, white = i;
    }
    s_snowWhite[bank] = white;
    uint8_t px[32];
    memcpy(px, tiles + 4 + caps[k].tile * 32u, 32);
    for (int32_t x = 0; x < 8; x++)
    {
      int32_t deep = (x * 5 + caps[k].tile) % 3 == 0 ? 4 : 3;
      for (int32_t y = 0; y < 6 && deep > 0; y++)
      {
        uint8_t *b = &px[y * 4 + x / 2];
        uint8_t v = (x & 1) ? (uint8_t)(*b >> 4) : (uint8_t)(*b & 15);
        if (v == 0) continue;
        *b = (x & 1) ? (uint8_t)((*b & 0x0F) | white << 4) : (uint8_t)((*b & 0xF0) | white);
        deep--;
      }
    }
    bmTilesLoadBg(0, caps[k].tile, px, 32);
  }
}

// The room's colours under snow at `level` (a room without the light: the light shades them
// itself, light.c).
static void snowPalette(BmGame *g, uint8_t level)
{
  if (g->light.mode == BM_LIGHT_PALETTE)
  {
    g->light.fresh = true;   // shaded anew, under snow
    return;
  }
  uint32_t size = 0;
  const uint8_t *pal = bmPackChunkData(g->room.pPack, g->room.paletteChunk, &size);
  if (pal == NULL || size < 4 || size < 4u + pal[2] * 32u) return;
  (void)level;
  for (uint32_t i = 0; i < pal[2] * 16u; i++)
  {
    BmColor c = { bmGameSnowColourAt(g, (uint8_t)(pal[1] + i / 16), (uint8_t)(i & 15), bmRead16(pal + 4 + i * 2)) };
    bmPaletteLoadBg((uint16_t)(pal[1] * 16u + i), &c, 1);
  }
}

uint8_t bmGameDrawSnow(BmGame *g, uint8_t first, uint8_t max)
{
  bmGameSceneFrame(g, false);   // (the story's each frame: here, after the frame's game)
  // The world under snow: its colours by how much it snows (in four steps), the snow lying.
  uint8_t level = (uint8_t)((g->snow + 24) / 25);
  if (!g->snowCaps) memset(s_snowWhite, 0, sizeof(s_snowWhite));   // (another room's)
  if (level >= SNOW_CAP_LEVEL && !g->snowCaps)
  {
    g->snowCaps = true;
    snowCaps(g);
    g->snowLevel = 0xFF;   // the colours again, the snow's white with them
  }
  if (level != g->snowLevel)
  {
    g->snowLevel = level;
    snowPalette(g, level);
  }
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

// A scene (game.h: sceneLock) holds the player: nothing strikes them meanwhile, as through a
// door; one left behind (the player carried out of its room) ends with the room.
void bmGameSceneFrame(BmGame *g, bool roomLeft)
{
  if (g->lockTimer == 0) g->sceneLock = false;
  if (!g->sceneLock) return;
  if (roomLeft) g->lockTimer = 0, g->sceneLock = false;
  else if (g->hurtTimer < 2) g->hurtTimer = 2;
}
