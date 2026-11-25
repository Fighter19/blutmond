/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The light (src/world/light.h) and the puddles (reflect.h) left out of the multiboot images:
// everything of one shares 256 KB, and their packs have no lit room and no puddle (the torch
// halls and the rural street are left out of them). Linked before the engine's library, these
// take the place of light.c and reflect.c, which are then not pulled in. A lit room pushed to
// such an image is shown unlit: the palettes as drawn, no light layer; a puddle mirrors nothing.

#include "game.h"

void bmLightEnterRoom(BmGame *g)
{
  g->light.mode = BM_LIGHT_NONE;
  bmUiSetLightLayer(-1);
  bmUiSetLightWeight(16, true);
}

int32_t bmLightIntensity(const BmLight *pLight, uint32_t frame)
{
  (void)frame;
  return (int32_t)pLight->intensity * 4;
}

void bmLightPresent(BmGame *g)
{
  (void)g;
}

bool bmLightOn(const BmGame *g)
{
  (void)g;
  return false;
}

void bmReflectEnterRoom(BmGame *g)
{
  g->reflect.on = false;
  g->reflect.shown = false;
  bmUiSetReflectionLayer(-1);
}

void bmReflectPresent(BmGame *g)
{
  (void)g;
}

bool bmReflectMirrors(const BmGame *g, int32_t left, int32_t right, int32_t y, int32_t *pSurface, uint8_t *pSquash)
{
  (void)g;
  (void)left;
  (void)right;
  (void)y;
  (void)pSurface;
  (void)pSquash;
  return false;
}
