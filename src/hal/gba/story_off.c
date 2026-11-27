/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The story functions (src/world/story.c) left out of the multiboot images: everything of one
// shares 256 KB, and their packs have no scene that uses them (Mitsuki's prologue is left
// out). Linked before the engine's library, this takes the place of story.c: flags are off,
// owner_turn, set_light and set_glow do nothing.

#include "game.h"

int32_t bmGameStoryCall(BmGame *g, uint8_t current, uint32_t api, const int32_t *pArgs)
{
  (void)g;
  (void)current;
  (void)api;
  (void)pArgs;
  return 0;
}
