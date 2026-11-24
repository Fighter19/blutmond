/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The light (src/world/light.h) left out of the multiboot images: everything of one shares
// 256 KB, and their packs have no lit room (the torch halls are left out of them). Linked
// before the engine's library, these take the place of light.c, which is then not pulled in.
// A lit room pushed to such an image is shown unlit: the palettes as drawn, no light layer.

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
