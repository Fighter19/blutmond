/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "reflect.h"

#include <string.h>

#include "figure.h"
#include "hal.h"
#include "game.h"
#include "ppu.h"
#include "ui.h"
#include "video.h"

// The tables (EWRAM: IWRAM is full); a line table keeps its own copy (bmVideoSetLineTable).
static uint16_t s_scroll[BM_HAL_SCREEN_HEIGHT] BM_EWRAM_BSS;
static uint16_t s_blend[BM_HAL_SCREEN_HEIGHT] BM_EWRAM_BSS;

static int32_t number(const char *s, int32_t fallback)
{
  if (s == NULL || *s < '0' || *s > '9') return fallback;
  int32_t v = 0;
  for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
  return v;
}

static void off(BmGame *g)
{
  BmReflection *r = &g->reflect;
  if (r->shown)
  {
    bmVideoSetLineTable(0, 0, NULL);
    bmVideoSetLineTable(1, 0, NULL);
    r->shown = false;
  }
  r->builtCamera = -1;
}

void bmReflectEnterRoom(BmGame *g)
{
  BmReflection *r = &g->reflect;
  if (r->on) bmVideoShowBg(r->bg, false);
  off(g);
  bmUiSetReflectionLayer(-1);
  memset(r, 0, sizeof(*r));
  r->builtCamera = -1;
  const BmObjectView *puddle = NULL;
  for (uint16_t i = 0; i < g->objectCount && puddle == NULL; i++)
    if (g->objects[i].kind == BM_OBJECT_PUDDLE && g->objects[i].w > 0 && g->objects[i].h > 0) puddle = &g->objects[i];
  // A free background: the room's layers take the first ones, the UI the last.
  if (puddle == NULL || g->room.layerCount >= BM_UI_BG)
  {
    bmUiBlend();
    return;
  }
  int8_t layer = -1;
  for (uint8_t i = 0; i < g->room.layerCount; i++)
    if (g->room.layers[i].role == BM_ROOM_ROLE_BACKGROUND && layer < 0) layer = (int8_t)i;
  if (layer < 0)
  {
    bmUiBlend();
    return;
  }
  r->on = true;
  r->bg = g->room.layerCount;
  r->layer = (uint8_t)layer;
  r->x = puddle->x;
  r->y = puddle->y;
  r->w = (int16_t)puddle->w;
  r->h = (int16_t)puddle->h;
  int32_t squash = number(bmGameObjectProperty(g, puddle, "squash"), 3);   // rows up per line
  r->squash = (uint8_t)(squash < 1 ? 16 : squash > 8 ? 128 : squash * 16);
  r->ripple = (uint8_t)number(bmGameObjectProperty(g, puddle, "ripple"), 1);
  int32_t strength = number(bmGameObjectProperty(g, puddle, "strength"), 10);
  r->eva = (uint8_t)(strength > 16 ? 16 : strength);
  r->evb = (uint8_t)(16 - r->eva / 2);
  // The mirror: the mirrored layer's map and tiles, behind it at its priority (the lower
  // background wins a tie), so it shows only where every room layer is empty.
  const BmRoomLayer *src = &g->room.layers[r->layer];
  bmBgSetup(r->bg, &(BmBgInfo){
    .charBlock = 0,
    .screenBlock = (uint8_t)(BM_ROOM_FIRST_SCREEN_BLOCK + src->bg * 2),
    .size = BM_BG_SIZE_512x256,
    .priority = src->priority,
  });
  bmUiSetReflectionLayer((int8_t)r->bg);
  bmUiBlend();
}

bool bmReflectMirrors(const BmGame *g, int32_t left, int32_t right, int32_t y, int32_t *pSurface, uint8_t *pSquash)
{
  const BmReflection *r = &g->reflect;
  if (!r->shown || right <= r->x || left >= r->x + r->w || y > r->y + 2) return false;
  *pSurface = r->y - g->room.cameraY;
  *pSquash = r->squash;
  return true;
}

void bmReflectPresent(BmGame *g)
{
  BmReflection *r = &g->reflect;
  if (!r->on) return;
  // Effects (a room's, a teleport's) take the line tables; the mirror waits.
  bool free = g->fxKind == 0 && g->teleTimer == 0;
  int32_t camY = g->room.cameraY;
  int32_t top = r->y - camY, bottom = top + r->h;
  if (!free || bottom <= 0 || top >= BM_HAL_SCREEN_HEIGHT)
  {
    bmVideoShowBg(r->bg, false);
    off(g);
    return;
  }
  const BmRoomLayer *src = &g->room.layers[r->layer];
  int32_t ly = (int32_t)(((int64_t)camY * src->parallaxY) >> 8);
  bmBgSetScroll(r->bg, src->scrollX, (int16_t)ly);
  // Line y of the puddle (d lines under the water line) shows the row d × squash above it,
  // wavering by `ripple` pixels.
  for (int32_t y = 0; y < BM_HAL_SCREEN_HEIGHT; y++)
  {
    int32_t v = ly;
    if (y >= top && y < bottom)
    {
      int32_t d = y - top;
      int32_t row = r->y - 1 - ((d * r->squash + 8) >> 4);
      row += (bmAngleSin((uint8_t)(d * 24 + g->frame * 3)) * r->ripple + 128) >> 8;
      v = ly + (row - camY) - y;
    }
    s_scroll[y] = (uint16_t)(v & 0x1FF);
  }
  bmVideoSetLineTable(1, (uint16_t)BM_PPU_BGVOFS(r->bg), s_scroll);
  if (camY != r->builtCamera)
  {
    // The puddle's lines blend the mirror over the water; the others keep the UI's weights.
    for (int32_t y = 0; y < BM_HAL_SCREEN_HEIGHT; y++)
      s_blend[y] = (uint16_t)(y >= top && y < bottom ? (r->eva | r->evb << 8) : (BM_UI_BLEND_UI | BM_UI_BLEND_BEHIND << 8));
    r->builtCamera = camY;
  }
  bmVideoSetLineTable(0, BM_PPU_BLDALPHA, s_blend);
  bmVideoShowBg(r->bg, true);
  r->shown = true;
}
