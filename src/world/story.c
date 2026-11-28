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
#include "puzzle.h"
#include "collision.h"
#include "script_api.h"
#include "video.h"
#include "ui.h"
#include "figure.h"
#include "ppu.h"
#include "hal.h"

#include <stdlib.h>
#include <string.h>

// Objects drawn larger (set_scale): percent, 0 as they are; cleared as a room is entered.
static uint8_t s_scale[BM_ACTORS_MAX] BM_EWRAM_BSS;
#define SCALE_MATRIX 28   // the affine matrix of the one drawn larger (reflect.h has 30 and 31)

// A beam (set_beam): one at a time, from an object; where it got to last frame (its end, and
// whether that end is on something: the spray there).
static struct
{
  uint8_t actor;   // 0xFF: none
  uint8_t angle, damage;
  uint16_t reach;
  bool shown;      // its window is on
  bool hits;       // its end is on something (the ground, a solid object)
  int16_t x0, y0, x1, y1;   // room pixels, last frame
} s_beam BM_EWRAM_BSS;   // (zero: reach 0, off)
static uint16_t s_beamLines[BM_HAL_SCREEN_HEIGHT + 1] BM_EWRAM_BSS;

// Solid objects (set_solid): width and height of a box standing on the anchor (0: not solid).
static uint8_t s_solid[BM_ACTORS_MAX][2] BM_EWRAM_BSS;
static BmSolidBox s_boxes[BM_PUZZLE_BLOCKS + BM_ACTORS_MAX] BM_EWRAM_BSS;

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
  if (api == BM_SCRIPT_API_set_beam)
  {
    // From the calling object (its anchor), along `angle` from the way it faces (256 a turn,
    // clockwise on the screen: 64 straight down), at most `reach` pixels; 0 ends it.
    if (current < BM_ACTORS_MAX)
    {
      s_beam.actor = args[1] > 0 ? current : 0xFF;
      s_beam.angle = (uint8_t)args[0];
      s_beam.reach = (uint16_t)(args[1] < 0 ? 0 : args[1] > 400 ? 400 : args[1]);
      s_beam.damage = (uint8_t)(args[2] < 0 ? 0 : args[2] > 99 ? 99 : args[2]);
    }
    return 0;
  }
  if (api == BM_SCRIPT_API_set_solid)
  {
    if (current < BM_ACTORS_MAX)
    {
      s_solid[current][0] = (uint8_t)(args[0] < 0 ? 0 : args[0] > 64 ? 64 : args[0]);
      s_solid[current][1] = (uint8_t)(args[1] < 0 ? 0 : args[1] > 64 ? 64 : args[1]);
    }
    return 0;
  }
  if (api == BM_SCRIPT_API_set_scale)
  {
    if (current < BM_ACTORS_MAX) s_scale[current] = (uint8_t)(args[0] <= 100 ? 0 : args[0] > 200 ? 200 : args[0]);
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
#define SPRAY 6

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
  // Where a beam is on something: a spray of snow (SPRAY flakes thrown up and out, again and
  // again), after the snow's flakes.
  uint8_t spray = s_beam.shown && s_beam.hits ? SPRAY : 0;
  if (n + spray > max) n = (uint8_t)(max > spray ? max - spray : 0);
  if (n + spray > max) spray = (uint8_t)(max - n);
  if (n + spray == 0) return 0;
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
  for (uint8_t k = 0; k < spray; k++)
  {
    // Each flake its own arc of 16 frames, begun at its own time: up and to its side.
    uint32_t t = (f + k * 5u) % 16u;
    int32_t side = (int32_t)k - (int32_t)SPRAY / 2;
    int32_t x = s_beam.x1 - g->room.cameraX + side * (int32_t)t / 3 - 4;
    int32_t y = s_beam.y1 - g->room.cameraY - (int32_t)(t * (18u - t)) / 5 - 4;
    BmSprite s = { .x = (int16_t)x, .y = (int16_t)y, .shape = BM_SPRITE_SQUARE, .size = 0,
                   .tile = (uint16_t)(g->builtinTile + SNOW_TILE), .paletteBank = 15, .priority = 1, .visible = true };
    bmSpriteSet((uint8_t)(first + n + k), &s);
  }
  return (uint8_t)(n + spray);
}

// A scene (game.h: sceneLock) holds the player: nothing strikes them meanwhile, as through a
// door; one left behind (the player carried out of its room) ends with the room.
void bmGameSceneFrame(BmGame *g, bool roomLeft)
{
  if (roomLeft)
  {
    memset(s_scale, 0, sizeof(s_scale));
    memset(s_solid, 0, sizeof(s_solid));
    s_beam.actor = 0xFF, s_beam.reach = 0;
  }
  if (g->lockTimer == 0) g->sceneLock = false;
  if (!g->sceneLock) return;
  if (roomLeft) g->lockTimer = 0, g->sceneLock = false;
  else if (g->hurtTimer < 2) g->hurtTimer = 2;
}

uint8_t bmGameDrawActor(BmGame *g, uint8_t i, uint16_t frame, uint8_t bank, uint8_t firstOam, uint8_t maxOam)
{
  const BmActors *a = &g->actors;
  BmSpriteSheet *s = &g->sheets[a->sheet[i]];
  int32_t x = BM_FIX_INT(a->x[i]) - g->room.cameraX, y = BM_FIX_INT(a->y[i]) - g->room.cameraY;
  if (s_scale[i] == 0) return bmSpriteSheetDrawBank(s, frame, x, y, a->facing[i] < 0, 1, bank, firstOam, maxOam);
  // Larger about the anchor: the sheared draw (its pieces in the sheet's bank: this one's for it).
  int32_t k = s_scale[i] * 256 / 100;
  uint8_t own = s->bank;
  s->bank = bank;
  uint8_t used = bmSpriteSheetDrawSheared(s, frame, x, y, a->facing[i] < 0 ? -k : k, 0, k, SCALE_MATRIX, 1, firstOam, maxOam);
  s->bank = own;
  return used;
}

// --- Solid objects and beams (set_solid, set_beam) ----------------------------------------------

static bool alive(const BmGame *g, uint8_t i)
{
  return i < g->actors.count && !g->actors.gone[i];
}

void bmGameStoryBoxes(BmGame *g)
{
  // The push blocks' boxes (puzzle.c made them this frame), then the solid objects'.
  uint8_t n = 0;
  for (uint8_t k = 0; k < g->collision.boxCount && n < BM_PUZZLE_BLOCKS; k++) s_boxes[n++] = g->collision.pBoxes[k];
  uint8_t own = n;
  for (uint8_t i = 0; i < BM_ACTORS_MAX; i++)
  {
    if (s_solid[i][0] == 0 || !alive(g, i) || g->actors.hidden[i]) continue;
    int32_t x = BM_FIX_INT(g->actors.x[i]), y = BM_FIX_INT(g->actors.y[i]);
    s_boxes[n++] = (BmSolidBox){ (int16_t)(x - s_solid[i][0] / 2), (int16_t)(y - s_solid[i][1]), s_solid[i][0], s_solid[i][1] };
  }
  if (n == own) return;
  g->collision.pBoxes = s_boxes;
  g->collision.boxCount = n;
  // One that came up where the player is: the player on top of it (not stuck in it).
  BmBody *p = &g->player;
  int32_t px = BM_FIX_INT(p->x), py = BM_FIX_INT(p->y), hw = g->playerParams.halfWidth;
  for (uint8_t k = own; k < n; k++)
  {
    const BmSolidBox *b = &s_boxes[k];
    if (px + hw > b->x && px - hw < b->x + b->w && py > b->y && py - g->playerParams.height < b->y + b->h)
    {
      p->y = BM_FIX(b->y);
      if (p->vy > 0) p->vy = 0;
    }
  }
}

// Whether the room pixel (x, y) stops a beam: the ground, a wall, a solid object.
static bool stopsBeam(const BmGame *g, int32_t x, int32_t y)
{
  if (x < 0 || y < 0 || x >= g->room.widthPx || y >= g->room.heightPx) return true;
  if (bmCollisionSolidSides(&g->collision, x, y) || bmCollisionSolidPlatform(&g->collision, x, y)) return true;
  for (uint8_t k = 0; k < g->collision.boxCount; k++)
  {
    const BmSolidBox *b = &g->collision.pBoxes[k];
    if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) return true;
  }
  return false;
}

#define BEAM_HALF 3   // half the beam's thickness, pixels

uint16_t bmGameBeamShown(const BmGame *g)
{
  (void)g;
  return s_beam.shown ? (uint16_t)(abs(s_beam.x1 - s_beam.x0) + abs(s_beam.y1 - s_beam.y0)) : 0;
}

void bmGameStoryPresent(BmGame *g)
{
  bool on = s_beam.reach > 0 && s_beam.actor != 0xFF && alive(g, s_beam.actor);
  if (!on)
  {
    s_beam.actor = 0xFF, s_beam.reach = 0;
    s_beam.hits = false;
    if (s_beam.shown)
    {
      bmWindowEnable(0);
      bmVideoSetLineTable(0, 0, NULL);
      bmUiBlend();
      s_beam.shown = false;
    }
    return;
  }
  // Along its line from the anchor, a pixel at a time (24.8), until something stops it.
  const BmActors *a = &g->actors;
  uint8_t i = s_beam.actor;
  int32_t dx = bmAngleCos(s_beam.angle) * (a->facing[i] < 0 ? -1 : 1), dy = bmAngleSin(s_beam.angle);
  int32_t x = a->x[i], y = a->y[i];
  int32_t n = 0;
  s_beam.hits = false;
  for (; n < s_beam.reach; n++)
  {
    if (stopsBeam(g, BM_FIX_INT(x + dx), BM_FIX_INT(y + dy)))
    {
      s_beam.hits = true;
      break;
    }
    x += dx, y += dy;
  }
  s_beam.x0 = (int16_t)BM_FIX_INT(a->x[i]), s_beam.y0 = (int16_t)BM_FIX_INT(a->y[i]);
  s_beam.x1 = (int16_t)BM_FIX_INT(x), s_beam.y1 = (int16_t)BM_FIX_INT(y);
  // It strikes the player it touches (their box within its half thickness of the line).
  const BmBody *p = &g->player;
  int32_t px = BM_FIX_INT(p->x), pTop = BM_FIX_INT(p->y) - g->playerParams.height, pBottom = BM_FIX_INT(p->y);
  int32_t hw = g->playerParams.halfWidth + BEAM_HALF;
  int32_t tx = a->x[i], ty = a->y[i];
  for (int32_t k = 0; k <= n; k += 2, tx += 2 * dx, ty += 2 * dy)
  {
    int32_t bx = BM_FIX_INT(tx), by = BM_FIX_INT(ty);
    if (bx >= px - hw && bx < px + hw && by >= pTop - BEAM_HALF && by < pBottom + BEAM_HALF)
    {
      bmGameStrikePlayerWith(g, s_beam.damage, a->x[i], 2, 0xFF);   // (element 2: ice, items.ms)
      break;
    }
  }
  // Drawn: window 0 one span a line around the segment, brightened (the colour effect inside
  // it only). A line y crosses the thick segment where it is within BEAM_HALF of the line and
  // between its ends: two ranges of x, each from a linear inequality.
  int32_t sx0 = s_beam.x0 - g->room.cameraX, sy0 = s_beam.y0 - g->room.cameraY;
  int32_t ux = dx, uy = dy;   // the direction, 256 long
  int32_t len = n * 256;      // its length, along u (x 256)
  for (int32_t ly = 0; ly <= BM_HAL_SCREEN_HEIGHT; ly++)
  {
    // For x on line ly: across = (x - sx0) * uy - (ly - sy0) * ux (x 256): |across| <= BEAM_HALF * 256;
    // along = (x - sx0) * ux + (ly - sy0) * uy: 0 to len. Each: lo <= c + x * m <= hi.
    int32_t lo = 0, hi = 240;
    int32_t yy = ly - sy0;
    int32_t bounds[2][4] = { { -yy * ux, uy, -BEAM_HALF * 256, BEAM_HALF * 256 }, { yy * uy, ux, -BEAM_HALF * 256, len + BEAM_HALF * 256 } };
    bool any = true;
    for (int b = 0; b < 2 && any; b++)
    {
      int32_t c = bounds[b][0], m = bounds[b][1], l = bounds[b][2], h = bounds[b][3];
      if (m == 0)
      {
        if (c < l || c > h) any = false;
        continue;
      }
      // l <= c + (x - sx0) m <= h  ->  x in [sx0 + (l - c) / m, sx0 + (h - c) / m] (ordered by m's sign)
      int32_t a1 = (l - c) / m, a2 = (h - c) / m;
      if (a1 > a2) { int32_t t = a1; a1 = a2; a2 = t; }
      if (sx0 + a1 > lo) lo = sx0 + a1;
      if (sx0 + a2 + 1 < hi) hi = sx0 + a2 + 1;
    }
    if (!any || hi <= lo || lo >= 240 || hi <= 0) s_beamLines[ly] = 0;
    else s_beamLines[ly] = (uint16_t)(((lo < 0 ? 0 : lo) << 8) | (hi > 240 ? 240 : hi));
  }
  const uint8_t all = (uint8_t)(BM_LAYER_BG(0) | BM_LAYER_BG(1) | BM_LAYER_BG(2) | BM_LAYER_BG(3) | BM_LAYER_OBJ);
  bmWindowSet(0, 0, 0, 240, 160);
  bmVideoSetLineTable(0, BM_PPU_WIN0H, s_beamLines);
  bmWindowLayers((uint8_t)(all | BM_LAYER_EFFECTS), all, all, all);
  bmWindowEnable(1);
  bmBlendSet(BM_BLEND_BRIGHTEN, (uint8_t)(all | BM_LAYER_BACKDROP), 0, 0, 0, 12);
  s_beam.shown = true;
}
