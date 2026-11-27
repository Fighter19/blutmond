/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The choice of playable character (src/world/character.c) left out of the multiboot images:
// everything of one shares 256 KB, and their packs hold one character (Mitsuki and the
// Character menu are left out of them). Linked before the engine's library, these take the
// place of character.c, which is then not pulled in: the player is the figure "player", who
// says the player's lines.

#include "game.h"
#include "script_api.h"

const char *bmGameChosenCharacter(const BmGame *g)
{
  (void)g;
  return NULL;
}

uint32_t bmGameCharacterSpeaker(const BmGame *g, uint32_t speaker)
{
  (void)g;
  return speaker;
}

int32_t bmGameCharacterCall(BmGame *g, uint32_t api, int32_t arg)
{
  (void)g;
  (void)arg;
  // character_count() 1, character() none, has_character() and set_character() false.
  return api == BM_SCRIPT_API_character_count ? 1 : api == BM_SCRIPT_API_character ? 0xFFFF : 0;
}
