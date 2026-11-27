/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The playable characters (game.h): the figure "player" and any figure "player_<name>" can be
// the player; which one is an option (BmGame::character), kept in the save memory with the
// language. The multiboot images leave this out (src/hal/gba/character_off.c).

#include "game.h"
#include "data.h"
#include "script_api.h"

#include <string.h>

// Whether a figure's name makes it a playable character: "player" or "player_<name>".
static bool playableName(const char *n)
{
  return n != NULL && strncmp(n, "player", 6) == 0 && (n[6] == '\0' || n[6] == '_');
}

bool bmGameHasCharacter(const BmGame *g, uint16_t name)
{
  BmFigure f;
  const char *n = bmPackString(g->pPack, name);
  if (!playableName(n) || bmFigureFind(&f, g->pPack, n) != BM_SUCCESS) return false;
  // Every sprite of it in the pack (not left out of it: an absent asset).
  for (uint8_t i = 0; i < f.layerCount; i++)
  {
    bool have = f.layers[i].sprite == 0xFFFF;
    for (uint8_t k = 0; k < g->sheetCount && !have; k++) have = g->sheets[k].nameId == f.layers[i].sprite;
    if (!have) return false;
  }
  return true;
}

uint8_t bmGameCharacterCount(const BmGame *g)
{
  uint8_t n = 0;
  for (int32_t c = bmPackFindChunk(g->pPack, BM_CHUNK_FIGR, 0); c >= 0; c = bmPackFindChunk(g->pPack, BM_CHUNK_FIGR, (uint32_t)c + 1))
  {
    const uint8_t *p = bmPackChunkData(g->pPack, (uint32_t)c, NULL);
    n += p != NULL && bmGameHasCharacter(g, bmRead16(p));
  }
  return n;
}

const char *bmGameChosenCharacter(const BmGame *g)
{
  return g->character != 0xFFFF && bmGameHasCharacter(g, g->character) ? bmPackString(g->pPack, g->character) : NULL;
}

int32_t bmGameCharacterCall(BmGame *g, uint32_t api, int32_t arg)
{
  switch (api)
  {
  case BM_SCRIPT_API_character: return g->playerFigure.layerCount != 0 ? g->playerFigure.nameId : 0xFFFF;
  case BM_SCRIPT_API_set_character: return bmGameSetCharacter(g, (uint16_t)arg);
  case BM_SCRIPT_API_has_character: return bmGameHasCharacter(g, (uint16_t)arg);
  default: return bmGameCharacterCount(g);
  }
}

// The pack's data table of the structure `Character` (characters.ms in the test level).
static bool characterTable(const BmPack *pPack, BmDataTable *t)
{
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_DTAB, 0); c >= 0; c = bmPackFindChunk(pPack, BM_CHUNK_DTAB, (uint32_t)c + 1))
  {
    uint32_t size = 0;
    const uint8_t *d = bmPackChunkData(pPack, (uint32_t)c, &size);
    const char *n = d != NULL && bmDataTableOpen(t, d, size) ? bmPackString(pPack, t->structure) : NULL;
    if (n != NULL && strcmp(n, "Character") == 0) return true;
  }
  return false;
}

static bool tableField(const BmPack *pPack, const BmDataTable *t, const char *name, BmDataField *f)
{
  for (uint16_t i = 0; i < t->fieldCount; i++)
  {
    *f = bmDataTableField(t, i);
    const char *n = bmPackString(pPack, f->name);
    if (n != NULL && strcmp(n, name) == 0) return true;
  }
  return false;
}

uint32_t bmGameCharacterSpeaker(const BmGame *g, uint32_t speaker)
{
  BmDataTable t;
  BmDataField figure, says;
  if (speaker == 0 || g->playerFigure.layerCount == 0 || !characterTable(g->pPack, &t) ||
      !tableField(g->pPack, &t, "figure", &figure) || !tableField(g->pPack, &t, "speaker", &says))
    return speaker;
  // The speaker of the player's own character (figure #player) and of the one playing.
  uint32_t own = 0, playing = 0;
  for (uint16_t i = 0; i < t.recordCount; i++)
  {
    const uint8_t *r = bmDataRecord(&t, bmDataRecordKey(&t, i));
    if (r == NULL) continue;
    uint16_t fig = (uint16_t)bmDataRead(&t, r, figure.offset, (BmDataType)figure.type);
    uint32_t s = (uint32_t)bmDataRead(&t, r, says.offset, (BmDataType)says.type);
    const char *n = bmPackString(g->pPack, fig);
    if (n != NULL && strcmp(n, "player") == 0) own = s;
    if (fig == g->playerFigure.nameId) playing = s;
  }
  return speaker == own && playing != 0 ? playing : speaker;
}

bool bmGameSetCharacter(BmGame *g, uint16_t name)
{
  if (!bmGameHasCharacter(g, name)) return false;
  if (g->playerFigure.layerCount != 0 && g->playerFigure.nameId == name) return true;
  g->character = name;
  bmGameWriteOptions(g);
  bmGameReloadPlayer(g);
  return true;
}
