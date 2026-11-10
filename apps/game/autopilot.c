/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "autopilot.h"

#include "hal.h"

#include <limits.h>
#include <string.h>

uint16_t bmAutopilotKeys(BmAutopilot *a, const BmGame *g)
{
  const BmBody *b = &g->player;
  const BmCollision *c = &g->collision;
  int32_t x = BM_FIX_INT(b->x), feet = BM_FIX_INT(b->y);
  int32_t hw = g->playerParams.halfWidth;
  const char *room = bmGameRoomId(g);

  // In the second room: walk in a little, then stand still.
  if (room && strcmp(room, "test_room_2") == 0)
  {
    return ++a->idle < 40 ? BM_KEY_RIGHT : 0;
  }

  // Ladders: climb while on one and not yet on top.
  bool ladder = bmCollisionMaterial(c, x, feet - 14) == BM_MATERIAL_LADDER || bmCollisionMaterial(c, x, feet - 1) == BM_MATERIAL_LADDER;
  bool wallAhead = bmCollisionSolidSides(c, x + hw + 4, feet - 12);
  if (ladder && (wallAhead || b->climbing))
  {
    bool onTop = bmCollisionMaterial(c, x, feet - 1) != BM_MATERIAL_LADDER && b->onGround;
    if (!onTop) return BM_KEY_UP;
  }

  uint16_t keys = BM_KEY_RIGHT;
  if (a->jumpHold)
  {
    a->jumpHold--;
    return keys | BM_KEY_A;
  }
  if (!b->onGround) return keys;

  // Decide to jump: a wall, a gap or something harmful ahead, or a platform up ahead that
  // leads further (the step before the stone block).
  bool ladderAhead = bmCollisionMaterial(c, x + hw + 6, feet - 12) == BM_MATERIAL_LADDER;
  bool gapAhead = bmCollisionFloor(c, x + hw + 10, feet - 2, feet + 24, true) == INT32_MAX;
  bool hazardAhead = false;
  for (int32_t d = 2; d <= 14; d += 4)
  {
    uint8_t m = bmCollisionMaterial(c, x + hw + d, feet - 2);
    if (m == BM_MATERIAL_DAMAGE || m == BM_MATERIAL_DEATH || m == BM_MATERIAL_WATER) hazardAhead = true;
  }
  if (!ladderAhead && ((wallAhead && !ladder) || gapAhead || hazardAhead))
  {
    a->jumpHold = 18;
    return keys | BM_KEY_A;
  }
  return keys;
}
