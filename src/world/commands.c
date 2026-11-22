/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "commands.h"

#include <string.h>

#include "attachments.h"
#include "data.h"
#include "game.h"
#include "hal.h"

#define LAST_STEP_FRAMES 10   // from the last direction to the button
#define STEP_FRAMES 12        // from one direction to the next

typedef struct Fields
{
  int16_t input, cost, weapon, charm, behaviour, frames;   // field indices, -1 none
} Fields;

static Fields fieldsOf(const BmGame *g, const BmDataTable *t);

// The table of the structure named Command and its fields, found once per pack.
static const uint8_t *s_from;
static uint32_t s_crc;
static bool s_have;
static BmDataTable s_table;
static Fields s_fields;

static bool commandTable(const BmGame *g, BmDataTable *pTable)
{
  if (s_from != g->pPack->pData || s_crc != g->pPack->header.crc32)
  {
    s_from = g->pPack->pData;
    s_crc = g->pPack->header.crc32;
    s_have = false;
    for (int32_t c = bmPackFindChunk(g->pPack, BM_CHUNK_DTAB, 0); c >= 0 && !s_have; c = bmPackFindChunk(g->pPack, BM_CHUNK_DTAB, (uint32_t)c + 1))
    {
      uint32_t size = 0;
      const uint8_t *p = bmPackChunkData(g->pPack, (uint32_t)c, &size);
      if (p == NULL || size < 2) continue;
      const char *name = bmPackString(g->pPack, bmRead16(p));
      if (name != NULL && strcmp(name, "Command") == 0) s_have = bmDataFindTable(g->pPack, bmRead16(p), &s_table);
    }
    if (s_have) s_fields = fieldsOf(g, &s_table);
  }
  *pTable = s_table;
  return s_have;
}

void bmCommandsPrepare(BmGame *g)
{
  BmDataTable t;
  commandTable(g, &t);
}

static Fields fieldsOf(const BmGame *g, const BmDataTable *t)
{
  Fields f = { -1, -1, -1, -1, -1, -1 };
  for (uint16_t i = 0; i < t->fieldCount; i++)
  {
    const char *n = bmPackString(g->pPack, bmDataTableField(t, i).name);
    if (n == NULL) continue;
    if (strcmp(n, "input") == 0) f.input = (int16_t)i;
    else if (strcmp(n, "cost") == 0) f.cost = (int16_t)i;
    else if (strcmp(n, "weapon") == 0) f.weapon = (int16_t)i;
    else if (strcmp(n, "charm") == 0) f.charm = (int16_t)i;
    else if (strcmp(n, "behaviour") == 0) f.behaviour = (int16_t)i;
    else if (strcmp(n, "frames") == 0) f.frames = (int16_t)i;
  }
  return f;
}

static int32_t readField(const BmDataTable *t, const uint8_t *record, int16_t index)
{
  if (index < 0) return 0;
  BmDataField f = bmDataTableField(t, (uint16_t)index);
  return bmDataRead(t, record, f.offset, (BmDataType)f.type);
}

// The way the d-pad points, as on a numpad (5: none, 6: right, 8: up).
static uint8_t directionOf(uint16_t keys)
{
  int x = (keys & BM_KEY_RIGHT) ? 1 : (keys & BM_KEY_LEFT) ? -1 : 0;
  int y = (keys & BM_KEY_UP) ? 1 : (keys & BM_KEY_DOWN) ? -1 : 0;
  return (uint8_t)(5 + x + 3 * y);
}

// A token of an input: a direction (numpad, facing right), a button (BM_KEY_*) or neither.
static bool tokenDirection(const char *t, size_t n, uint8_t *pDir)
{
  static const struct { const char *name; uint8_t dir; } dirs[] = {
    { "Up", 8 }, { "Down", 2 }, { "Fwd", 6 }, { "Back", 4 },
    { "UpFwd", 9 }, { "UpBack", 7 }, { "DownFwd", 3 }, { "DownBack", 1 },
  };
  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    if (strlen(dirs[i].name) == n && strncmp(t, dirs[i].name, n) == 0)
    {
      *pDir = dirs[i].dir;
      return true;
    }
  return false;
}

static uint16_t tokenButton(const char *t, size_t n)
{
  static const struct { const char *name; uint16_t key; } buttons[] = {
#define X(name, key) { #name, key },
    BM_ATTACH_BUTTONS(X)
#undef X
  };
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    if (strlen(buttons[i].name) == n && strncmp(t, buttons[i].name, n) == 0) return buttons[i].key;
  return 0;
}

// How many directions the input has if it matches the history for `button` now, else -1.
static int32_t matches(const BmGame *g, const char *input, uint16_t button)
{
  uint8_t dirs[8];
  int32_t n = 0;
  uint16_t key = 0;
  for (const char *p = input; *p != 0;)
  {
    const char *end = strchr(p, '_');
    size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
    uint8_t d;
    if (end == NULL) key = tokenButton(p, len);
    else if (n < 8 && tokenDirection(p, len, &d)) dirs[n++] = d;
    else return -1;
    p = end != NULL ? end + 1 : p + len;
  }
  if (key != button || n == 0) return -1;
  // Facing left, the directions are mirrored.
  if (g->player.facing < 0)
    for (int32_t i = 0; i < n; i++) dirs[i] = (uint8_t)(dirs[i] + 2 - 2 * ((dirs[i] - 1) % 3));
  // From the newest: the last direction began at most LAST_STEP_FRAMES ago, each one before it
  // at most STEP_FRAMES before the next (other directions between them are passed over).
  uint16_t now = (uint16_t)g->frame, next = now, gap = LAST_STEP_FRAMES;
  int32_t want = n - 1;
  for (uint8_t age = 0; age < BM_COMMAND_HISTORY && age < g->cmdCount && want >= 0; age++)
  {
    uint8_t i = (uint8_t)((g->cmdHead + BM_COMMAND_HISTORY - 1 - age) % BM_COMMAND_HISTORY);
    if ((uint16_t)(next - g->cmdTimes[i]) > gap) return -1;
    if (g->cmdDirs[i] != dirs[want]) continue;
    next = g->cmdTimes[i];
    gap = STEP_FRAMES;
    want--;
  }
  return want < 0 ? n : -1;
}

static bool owns(const BmGame *g, uint32_t weapon, uint32_t charm)
{
  if (weapon != 0 && g->inventory.equipped[0] == weapon) return true;
  for (uint8_t s = 1; charm != 0 && s < BM_INVENTORY_EQUIP; s++)
    if (g->inventory.equipped[s] == charm) return true;
  return false;
}

uint32_t bmCommandsMatch(BmGame *g, uint16_t button)
{
  BmDataTable t;
  if (!commandTable(g, &t)) return 0;
  Fields f = s_fields;
  if (f.input < 0 || f.behaviour < 0) return 0;
  uint32_t best = 0;
  int32_t bestLength = 0;
  for (uint16_t r = 0; r < t.recordCount; r++)
  {
    uint32_t key = bmDataRecordKey(&t, r);
    const uint8_t *record = bmDataRecord(&t, key);
    if (record == NULL || !owns(g, (uint32_t)readField(&t, record, f.weapon), (uint32_t)readField(&t, record, f.charm))) continue;
    const char *input = bmPackString(g->pPack, (uint16_t)readField(&t, record, f.input));
    int32_t length = input != NULL ? matches(g, input, button) : -1;
    if (length > bestLength)
    {
      best = key;
      bestLength = length;
    }
  }
  return best;
}

uint16_t bmCommandsFrame(BmGame *g, uint16_t keys, uint16_t previousKeys)
{
  uint8_t dir = directionOf(keys);
  uint8_t last = g->cmdCount != 0 ? g->cmdDirs[(g->cmdHead + BM_COMMAND_HISTORY - 1) % BM_COMMAND_HISTORY] : 5;
  if (dir != last)
  {
    g->cmdDirs[g->cmdHead] = dir;
    g->cmdTimes[g->cmdHead] = (uint16_t)g->frame;
    g->cmdHead = (uint8_t)((g->cmdHead + 1) % BM_COMMAND_HISTORY);
    if (g->cmdCount < BM_COMMAND_HISTORY) g->cmdCount++;
  }
  uint16_t pressed = (uint16_t)(keys & ~previousKeys);
  static const uint16_t buttons[] = {
#define X(name, key) key,
    BM_ATTACH_BUTTONS(X)
#undef X
  };
  uint16_t done = 0;
  if (pressed == 0 || g->dialogue != 0 || g->lockTimer != 0 || g->teleTimer != 0 || g->cmdCount == 0) return 0;
  // No direction began lately: no input can end now (most presses; nothing to look up).
  if ((uint16_t)((uint16_t)g->frame - g->cmdTimes[(g->cmdHead + BM_COMMAND_HISTORY - 1) % BM_COMMAND_HISTORY]) > LAST_STEP_FRAMES) return 0;
  for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++)
  {
    if (!(pressed & buttons[b])) continue;
    uint32_t key = bmCommandsMatch(g, buttons[b]);
    if (key == 0) continue;
    BmDataTable t;
    commandTable(g, &t);
    Fields f = s_fields;
    const uint8_t *record = bmDataRecord(&t, key);
    int32_t cost = readField(&t, record, f.cost);
    if (cost > g->mana) continue;   // not enough: the button does what it does anyway
    int32_t frames = readField(&t, record, f.frames);
    uint16_t behaviour = (uint16_t)readField(&t, record, f.behaviour);
    if (!bmAttachmentsAttachRecord(g, BM_GAME_COMMAND_SLOT, behaviour, (uint16_t)(frames > 0 ? frames : 1), key)) continue;
    g->mana = (int16_t)(g->mana - cost);
    g->cmdLast = key;
    g->cmdDone++;
    done |= buttons[b];
  }
  return done;
}
