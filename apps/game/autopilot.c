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

  // The dialogue box: A, now and then.
  if (g->dialogue != 0)
  {
    a->idle++;
    return (a->idle & 63) == 0 ? BM_KEY_A : 0;
  }
  const BmActors *t = &g->actors;

  // In the second room: wait below the bat, and fight it when it comes near: turn to it and
  // swing every 8 frames (only does something with a weapon equipped). Once it is gone, on
  // east like in the first room.
  if (room && strcmp(room, "test_room_2") == 0)
  {
    a->idle++;
    for (uint8_t i = 0; t->hasModule && i < t->count; i++)
    {
      if (strcmp(g->objects[t->object[i]].className, "Bat") != 0 || t->hidden[i] || t->health[i] <= 0) continue;
      int32_t dx = BM_FIX_INT(t->x[i]) - x, dy = BM_FIX_INT(t->y[i]) - feet;
      if (dx >= -40 && dx <= 40 && dy >= -48 && dy <= 8 && BM_FIX_INT(t->y[i]) > BM_FIX_INT(t->homeY[i]) + 4)
      {
        if ((dx < 0) != (b->facing < 0) && dx != 0) return dx < 0 ? BM_KEY_LEFT : BM_KEY_RIGHT;
        return (a->idle & 7) == 0 ? BM_KEY_B : 0;
      }
      // Its place to wait: a little left of where it hangs.
      int32_t wait = BM_FIX_INT(t->homeX[i]) - 36;
      return x < wait - 4 ? BM_KEY_RIGHT : x > wait + 4 ? BM_KEY_LEFT : 0;
    }
    // The bat is gone: a bag it left is taken; then a rest (the tests look at frame 1000 and
    // want a still picture), then on east.
    for (uint8_t i = 0; t->hasModule && i < t->count; i++)
    {
      if (!t->spawned[i] || t->hidden[i]) continue;
      int32_t dx = BM_FIX_INT(t->x[i]) - x;
      return dx < -2 ? BM_KEY_LEFT : dx > 2 ? BM_KEY_RIGHT : 0;
    }
    if (a->idle < 500) return 0;
  }

  // The boss hall: stand in the middle and strike the moon bat when it comes down; take what
  // it leaves; put it on (Start, Equipment, Head, the helm).
  if (room && strcmp(room, "boss_hall") == 0)
  {
    a->idle++;
    if (g->screens.count != 0 || a->menu != 0)
    {
      // Start, A (Equipment), Down (Head), A, Down (the helm), A, B, B: one every 8 frames.
      static const uint16_t steps[] = { BM_KEY_START, BM_KEY_A, BM_KEY_DOWN, BM_KEY_A, BM_KEY_DOWN, BM_KEY_A, BM_KEY_B, BM_KEY_B };
      if (a->menu >= 8 * 8) return 0;
      uint16_t k = (a->menu % 8) == 0 ? steps[a->menu / 8] : 0;
      a->menu++;
      return k;
    }
    for (uint8_t i = 0; t->hasModule && i < t->count; i++)
    {
      const char *cls = t->object[i] < g->objectCount ? g->objects[t->object[i]].className : NULL;
      if (t->hidden[i]) continue;
      if (t->spawned[i])
      {
        // A bag on the floor: go to it.
        const char *sheet = t->sheet[i] >= 0 ? bmPackString(g->pPack, g->sheets[t->sheet[i]].nameId) : NULL;
        if (sheet != NULL && strcmp(sheet, "drop") == 0)
        {
          int32_t dx = BM_FIX_INT(t->x[i]) - x;
          return dx < -2 ? BM_KEY_LEFT : dx > 2 ? BM_KEY_RIGHT : 0;
        }
        continue;
      }
      if (cls == NULL || strcmp(cls, "MoonBat") != 0) continue;
      int32_t dx = BM_FIX_INT(t->x[i]) - x, dy = BM_FIX_INT(t->y[i]) - feet;
      if (t->health[i] > 0 && dx >= -30 && dx <= 30 && dy >= -56 && dy <= 8)
      {
        if ((dx < 0) != (b->facing < 0) && dx != 0) return dx < 0 ? BM_KEY_LEFT : BM_KEY_RIGHT;
        return (a->idle & 7) == 0 ? BM_KEY_B : 0;
      }
      // Wait in the middle of the hall.
      return x < 236 ? BM_KEY_RIGHT : x > 244 ? BM_KEY_LEFT : 0;
    }
    // The boss is gone and so is the bag: put on what it left (the Iron Helm), once.
    uint32_t helm = 2166136261u;
    for (const char *n = "IronHelm"; *n; n++) helm = (helm ^ (uint8_t)*n) * 16777619u;
    if (g->inventory.equipped[2] == 0 && bmInventoryCount(&g->inventory, helm) != 0)
    {
      a->menu = 1;
      return BM_KEY_START;
    }
    return 0;
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

static const BmStateField s_pilotFields[] = {
  BM_STATE_FIELD(BmAutopilot, jumpHold, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  BM_STATE_FIELD(BmAutopilot, idle, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  BM_STATE_FIELD(BmAutopilot, menu, BM_TYPE_U16, BM_STATE_SNAPSHOT),
};

BmStateBlock bmAutopilotStateBlock(BmAutopilot *pPilot)
{
  return (BmStateBlock){ "autopilot", s_pilotFields, 3, pPilot };
}
