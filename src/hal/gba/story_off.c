/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The story functions (src/world/story.c) left out of the multiboot images: everything of one
// shares 256 KB, and their packs have no scene that uses them (Mitsuki's prologue is left
// out). Linked before the engine's library, this takes the place of story.c: flags are off,
// owner_turn, set_light, set_glow, draw_in_front and set_snow do nothing, and no scene holds the
// player (bmGameSceneFrame); objects are drawn as they are (no set_scale), none is solid (set_solid), no beam
// (set_beam).

#include "game.h"

int32_t bmGameStoryCall(BmGame *g, uint8_t current, uint32_t api, const int32_t *pArgs)
{
  (void)g;
  (void)current;
  (void)api;
  (void)pArgs;
  return 0;
}

uint8_t bmGameDrawSnow(BmGame *g, uint8_t first, uint8_t max)
{
  (void)g;
  (void)first;
  (void)max;
  return 0;
}

void bmGameSceneFrame(BmGame *g, bool roomLeft)
{
  (void)g;
  (void)roomLeft;
}

uint8_t bmGameDrawActor(BmGame *g, uint8_t i, uint16_t frame, uint8_t bank, uint8_t firstOam, uint8_t maxOam)
{
  const BmActors *a = &g->actors;
  return bmSpriteSheetDrawBank(&g->sheets[a->sheet[i]], frame, BM_FIX_INT(a->x[i]) - g->room.cameraX, BM_FIX_INT(a->y[i]) - g->room.cameraY,
                               a->facing[i] < 0, 1, bank, firstOam, maxOam);
}

void bmGameStoryBoxes(BmGame *g)
{
  (void)g;
}

void bmGameStoryPresent(BmGame *g)
{
  (void)g;
}
