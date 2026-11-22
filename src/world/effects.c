/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "effects.h"

#include <string.h>

#include "game.h"
#include "screens.h"
#include "hal.h"
#include "ppu.h"
#include "sprite.h"
#include "video.h"

// sin(2 pi i / 256) * 64 for the first quarter; the rest by symmetry.
static const int8_t s_sinQuarter[64] = {
  0, 2, 3, 5, 6, 8, 9, 11, 12, 14, 16, 17, 19, 20, 22, 23, 24, 26, 27, 29, 30, 32, 33, 34, 36, 37, 38, 39, 41, 42, 43, 44,
  45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 56, 57, 58, 59, 59, 60, 60, 61, 61, 62, 62, 62, 63, 63, 63, 64, 64, 64,
};

// -64..64 for a phase of 256 to a turn.
static int32_t isin(uint32_t phase)
{
  phase &= 255;
  uint32_t q = phase & 63;
  int32_t v = (phase & 64) ? (q == 0 ? 64 : s_sinQuarter[64 - q]) : s_sinQuarter[q];
  return phase & 128 ? -v : v;
}

static uint16_t s_lines[3][BM_HAL_SCREEN_HEIGHT + 1] BM_EWRAM_BSS;

int32_t bmEffectNamed(const char *name)
{
  static const char *const names[] = { "none", "wave", "spotlight", "pulse", "gradient" };
  for (int32_t i = 0; name != NULL && i < (int32_t)(sizeof(names) / sizeof(names[0])); i++)
    if (strcmp(name, names[i]) == 0) return i;
  return -1;
}

void bmEffectsSet(BmGame *g, BmEffect effect, int32_t a, int32_t b)
{
  // The defaults of each effect.
  static const int16_t defaults[][2] = { { 0, 0 }, { 6, 3 }, { 44, 12 }, { 8, 2 }, { 10, 0 } };
  if ((uint32_t)effect >= sizeof(defaults) / sizeof(defaults[0])) effect = BM_EFFECT_NONE;
  g->fxKind = (uint8_t)effect;
  g->fxA = (int16_t)(a != 0 ? a : defaults[effect][0]);
  g->fxB = (int16_t)(b != 0 ? b : defaults[effect][1]);
}

void bmEffectsEnterRoom(BmGame *g)
{
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (strcmp(o->className, "@room") != 0) continue;
    int32_t kind = bmEffectNamed(bmGameObjectProperty(g, o, "effect"));
    const char *a = bmGameObjectProperty(g, o, "effect_a"), *b = bmGameObjectProperty(g, o, "effect_b");
    int32_t av = 0, bv = 0;
    for (; a != NULL && *a >= '0' && *a <= '9'; a++) av = av * 10 + (*a - '0');
    for (; b != NULL && *b >= '0' && *b <= '9'; b++) bv = bv * 10 + (*b - '0');
    bmEffectsSet(g, kind > 0 ? (BmEffect)kind : BM_EFFECT_NONE, av, bv);
    return;
  }
  bmEffectsSet(g, BM_EFFECT_NONE, 0, 0);
}

bool bmEffectsTeleport(BmGame *g, uint32_t roomIndex, int32_t x, int32_t y)
{
  if (g->teleTimer != 0 || roomIndex >= bmRoomCount(g->pPack)) return false;
  g->teleTimer = BM_TELEPORT_FRAMES;
  g->teleRoom = roomIndex;
  g->teleX = (int16_t)x;
  g->teleY = (int16_t)y;
  bmSoundPlayEffect(&g->sound, "teleport");
  return true;
}

void bmEffectsFrame(BmGame *g)
{
  if (g->teleTimer == 0) return;
  // Begun from a menu (an item used): the menus close, the room plays on.
  if (g->teleTimer == BM_TELEPORT_FRAMES)
    while (g->screens.count != 0) bmScreensClose(g);
  g->teleTimer--;
  // Held still and safe while it lasts.
  if (g->lockTimer < 2) g->lockTimer = 2;
  if (g->hurtTimer < 2) g->hurtTimer = 2;
  g->player.vx = g->player.vy = 0;
  if (g->teleTimer == BM_TELEPORT_FRAMES / 2) bmGameMovePlayer(g, g->teleRoom, g->teleX, g->teleY);
}

// How strong a teleport's wave is now, 0..32 (the middle).
static int32_t teleportPhase(const BmGame *g)
{
  if (g->teleTimer == 0) return 0;
  int32_t t = g->teleTimer, half = BM_TELEPORT_FRAMES / 2;
  return t > half ? BM_TELEPORT_FRAMES - t : t;
}

int32_t bmEffectsWaveAt(const BmGame *g, int32_t y)
{
  int32_t amp = 0, speed = 3;
  int32_t phase = teleportPhase(g);
  if (phase != 0)
  {
    amp = phase * BM_EFFECT_WAVE_MAX / (BM_TELEPORT_FRAMES / 2);
    speed = 6;
  }
  else if (g->fxKind == BM_EFFECT_WAVE)
  {
    amp = g->fxA;
    speed = g->fxB;
  }
  amp = amp < 0 ? 0 : amp > BM_EFFECT_WAVE_MAX ? BM_EFFECT_WAVE_MAX : amp;
  if (amp == 0) return 0;
  // 0 to amp, never less: the hardware map has columns to spare only right of the screen (the
  // room streams 32 columns from the camera's), so the picture may only be pulled left.
  return amp * (isin((uint32_t)(y * 4 + (int32_t)g->frame * speed)) + 64) / 128;
}

// Depth in the view: a pixel of depth is drawn 0.4 px wide.
#define DEPTH_X 102
#define DOOR_OPENING_FRAMES 12

int32_t bmEffectsDoorOpening(const BmGame *g, uint16_t object)
{
  // Only the door being walked through opens (in DOOR_OPENING_FRAMES), and the one just come
  // through closes behind the player.
  if (object >= g->objectCount) return 0;
  if (g->doorSeq == object + 1) return g->doorTimer >= DOOR_OPENING_FRAMES ? 256 : g->doorTimer * 256 / DOOR_OPENING_FRAMES;
  if (g->doorArrive == object + 1) return g->doorArriveTimer * 256 / 16;
  return 0;
}

uint8_t bmEffectsDrawDoors(BmGame *g, uint8_t firstOam, uint8_t maxOam)
{
  uint8_t used = 0, matrix = 0;
  for (uint16_t i = 0; i < g->objectCount && used < maxOam && matrix < 32; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (o->kind != BM_OBJECT_DOOR || g->objectSheet[i] < 0) continue;
    // Off screen (with room for a panel swung open): nothing to do.
    int32_t ox = o->x - g->room.cameraX, oy = o->y - g->room.cameraY;
    if (ox + o->w < -64 || ox > BM_HAL_SCREEN_WIDTH + 64 || oy + o->h < 0 || oy > BM_HAL_SCREEN_HEIGHT + 64) continue;
    const BmSpriteSheet *s = &g->sheets[g->objectSheet[i]];
    BmSpriteBox box = bmSpriteSheetHitbox(s, 0, false);
    int32_t ph = box.h, pw = box.h > 0 ? box.w : 32;
    int32_t scale = ph > 0 ? o->h * 256 / ph : 256;   // the panel as tall as the door
    int32_t open = bmEffectsDoorOpening(g, i);
    // The panel's angle from the screen's x axis (256 to a turn), its hinge on screen.
    int32_t angle, hx;
    int32_t closedWidth = pw * scale / 256 * DEPTH_X / 256;
    uint8_t side = g->objectDoorSide[i];
    if (side == 1)
    {
      angle = 64 + open * 56 / 256;   // along the depth, swung into the room (to the left)
      hx = o->x + o->w - closedWidth;
    }
    else if (side == 2)
    {
      angle = 64 - open * 56 / 256;   // along the depth, swung into the room (to the right)
      hx = o->x + o->w - closedWidth;
    }
    else
    {
      angle = open * 52 / 256;        // facing the screen, away into the depth
      hx = o->x;
    }
    int32_t c = isin((uint32_t)(angle + 64)), sn = isin((uint32_t)angle);
    // A point u along the panel: x = u cos a, and for a door in a side wall, whose depth shows,
    // + u 0.4 sin a; times the scale. Only scaled in x: the bottom stays on the ground.
    int32_t xs = (c * 256 + (side != 0 ? sn * DEPTH_X : 0)) / 64 * scale / 256;
    int32_t sx = hx - g->room.cameraX, sy = g->objectDoorGround[i] - g->room.cameraY;
    uint8_t n = bmSpriteSheetDrawSheared(s, bmAnimFrameAt(s, 0, g->frame), sx, sy, xs, 0, scale, matrix, 1,
                                         (uint8_t)(firstOam + used), (uint8_t)(maxOam - used));
    if (n != 0) matrix++;
    used = (uint8_t)(used + n);
    // Behind a door in the back wall, the dark of the opening (the panel's frame 1): after the
    // panel in OAM, so behind it.
    if (side == 0 && s->frameCount > 1 && matrix < 32 && used < maxOam)
    {
      n = bmSpriteSheetDrawSheared(s, 1, sx, sy, scale, 0, scale, matrix, 1, (uint8_t)(firstOam + used), (uint8_t)(maxOam - used));
      if (n != 0) matrix++;
      used = (uint8_t)(used + n);
    }
  }
  return used;
}

#define ROOM_LAYERS (BM_LAYER_BG(0) | BM_LAYER_BG(1) | BM_LAYER_BG(2))
#define ALL_LAYERS (ROOM_LAYERS | BM_LAYER_BG(3) | BM_LAYER_OBJ)

void bmEffectsPresent(BmGame *g)
{
  int32_t kind = g->teleTimer != 0 ? -1 : g->fxKind;   // -1: the teleport's own
  bool wave = kind == -1 || kind == BM_EFFECT_WAVE;
  uint8_t slots = 0;   // line-table slots in use this frame
  if (wave)
  {
    // Every background of the room sways; the UI layer stays.
    for (uint8_t i = 0; i < g->room.layerCount && i < 3; i++)
    {
      const BmRoomLayer *l = &g->room.layers[i];
      for (int32_t y = 0; y <= BM_HAL_SCREEN_HEIGHT; y++) s_lines[i][y] = (uint16_t)((l->scrollX + bmEffectsWaveAt(g, y)) & 511);
      bmVideoSetLineTable(i, BM_PPU_BGHOFS(l->bg), s_lines[i]);
      slots |= (uint8_t)(1u << i);
    }
  }
  if (kind == BM_EFFECT_SPOTLIGHT)
  {
    // Window 0 is a circle around the player, a span per line (WIN0H); outside it is dark.
    int32_t cx = BM_FIX_INT(g->player.x) - g->room.cameraX, cy = BM_FIX_INT(g->player.y) - 14 - g->room.cameraY;
    int32_t r = g->fxA < 8 ? 8 : g->fxA > 120 ? 120 : g->fxA;
    for (int32_t y = 0; y <= BM_HAL_SCREEN_HEIGHT; y++)
    {
      int32_t dy = y - cy, half = 0;
      if (dy > -r && dy < r)
        while ((half + 1) * (half + 1) + dy * dy <= r * r) half++;
      int32_t left = cx - half, right = cx + half;
      left = left < 0 ? 0 : left > 240 ? 240 : left;
      right = right < 0 ? 0 : right > 240 ? 240 : right;
      s_lines[0][y] = (uint16_t)(half > 0 && right > left ? (left << 8) | right : 0);
    }
    bmWindowSet(0, 0, 0, 240, 160);
    bmVideoSetLineTable(0, BM_PPU_WIN0H, s_lines[0]);
    slots |= 1;
    bmWindowLayers(ALL_LAYERS, ALL_LAYERS, ALL_LAYERS | BM_LAYER_EFFECTS, ALL_LAYERS);
    bmWindowEnable(1);
    bmBlendSet(BM_BLEND_DARKEN, ROOM_LAYERS | BM_LAYER_OBJ | BM_LAYER_BACKDROP, 0, 0, 0, (uint8_t)(g->fxB < 0 ? 0 : g->fxB > 16 ? 16 : g->fxB));
  }
  else
  {
    bmWindowEnable(0);
  }
  if (kind == BM_EFFECT_GRADIENT)
  {
    // Dusk: the top dark, fading out by the middle of the screen, with a slow shimmer.
    int32_t top = g->fxA < 0 ? 0 : g->fxA > 16 ? 16 : g->fxA;
    for (int32_t y = 0; y <= BM_HAL_SCREEN_HEIGHT; y++)
    {
      int32_t v = top - (y * top) / 96 + isin((uint32_t)(y * 2 + (int32_t)g->frame)) / 48;
      s_lines[2][y] = (uint16_t)(v < 0 ? 0 : v > 16 ? 16 : v);
    }
    bmBlendSet(BM_BLEND_DARKEN, ROOM_LAYERS | BM_LAYER_BACKDROP, 0, 0, 0, 0);
    bmVideoSetLineTable(2, BM_PPU_BLDY, s_lines[2]);
    slots |= 4;
  }
  else if (kind == BM_EFFECT_PULSE)
  {
    // Brighter, then darker, and back.
    int32_t s = isin((uint32_t)((int32_t)g->frame * (g->fxB > 0 ? g->fxB : 1))) * (g->fxA > 16 ? 16 : g->fxA) / 64;
    bmBlendSet(s >= 0 ? BM_BLEND_BRIGHTEN : BM_BLEND_DARKEN, ROOM_LAYERS | BM_LAYER_OBJ | BM_LAYER_BACKDROP, 0, 0, 0, (uint8_t)(s < 0 ? -s : s));
  }
  else if (kind == -1)
  {
    // The teleport turns the room white at its middle.
    bmBlendSet(BM_BLEND_BRIGHTEN, ROOM_LAYERS | BM_LAYER_OBJ | BM_LAYER_BACKDROP, 0, 0, 0, (uint8_t)(teleportPhase(g) * 14 / (BM_TELEPORT_FRAMES / 2)));
  }
  else if (kind != BM_EFFECT_SPOTLIGHT)
  {
    bmBlendSet(BM_BLEND_NONE, 0, 0, 0, 0, 0);
  }
  // Slots not used now are switched off (the room's backgrounds keep their plain scroll).
  for (uint8_t i = 0; i < 3; i++)
    if (!(slots & (1u << i)) && (g->fxApplied & (1u << i))) bmVideoSetLineTable(i, 0, NULL);
  g->fxApplied = slots;
}
