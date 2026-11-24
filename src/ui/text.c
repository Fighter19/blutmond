/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "text.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

#define HEADER 8
#define ENTRY 8

// The LANG chunk of `language`, or NULL.
static const uint8_t *languageChunk(const BmPack *pPack, uint8_t language, uint32_t *pSize)
{
  uint8_t n = 0;
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_LANG, 0); c >= 0; c = bmPackFindChunk(pPack, BM_CHUNK_LANG, (uint32_t)c + 1))
  {
    if (bmPackChunkData(pPack, (uint32_t)c, NULL) == NULL) continue;   // left out (an update brings it)
    if (n++ != language) continue;
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)c, pSize);
    return p != NULL && *pSize >= HEADER && *pSize >= HEADER + (uint32_t)rd16(p + 4) * ENTRY ? p : NULL;
  }
  return NULL;
}

uint8_t bmTextLanguageCount(const BmPack *pPack)
{
  uint8_t n = 0;
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_LANG, 0); c >= 0 && n < 255; c = bmPackFindChunk(pPack, BM_CHUNK_LANG, (uint32_t)c + 1))
    if (bmPackChunkData(pPack, (uint32_t)c, NULL) != NULL) n++;
  return n == 0 ? 1 : n;
}

uint16_t bmTextLanguageCode(const BmPack *pPack, uint8_t language)
{
  uint32_t size;
  const uint8_t *p = languageChunk(pPack, language, &size);
  return p != NULL ? rd16(p) : 0xFFFF;
}

uint16_t bmTextLanguageName(const BmPack *pPack, uint8_t language)
{
  uint32_t size;
  const uint8_t *p = languageChunk(pPack, language, &size);
  return p != NULL ? rd16(p + 2) : 0xFFFF;
}

int32_t bmTextFindLanguage(const BmPack *pPack, const char *code)
{
  for (uint8_t i = 0; i < bmTextLanguageCount(pPack); i++)
  {
    const char *c = bmPackString(pPack, bmTextLanguageCode(pPack, i));
    if (c != NULL && code != NULL && strcmp(c, code) == 0) return i;
  }
  return -1;
}

const char *bmText(const BmPack *pPack, uint8_t language, uint16_t id)
{
  uint32_t size;
  const uint8_t *p = language != 0 ? languageChunk(pPack, language, &size) : NULL;
  if (p != NULL)
  {
    // Entries by string id: { u16 id, u16 0, u32 offset of the text in the chunk }.
    int32_t lo = 0, hi = (int32_t)rd16(p + 4) - 1;
    while (lo <= hi)
    {
      int32_t mid = (lo + hi) / 2;
      const uint8_t *e = p + HEADER + (uint32_t)mid * ENTRY;
      uint16_t at = rd16(e);
      if (at == id)
      {
        uint32_t offset = rd32(e + 4);
        return offset < size ? (const char *)p + offset : bmPackString(pPack, id);
      }
      if (at < id) lo = mid + 1;
      else hi = mid - 1;
    }
  }
  return bmPackString(pPack, id);
}

uint32_t bmTextNextChar(const char **ppText)
{
  const uint8_t *s = (const uint8_t *)*ppText;
  uint32_t c = s[0];
  int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
  if (more < 0)
  {
    *ppText += 1;
    return '?';
  }
  c = more == 0 ? c : more == 1 ? c & 0x1F : more == 2 ? c & 0x0F : c & 0x07;
  for (int i = 1; i <= more; i++)
  {
    if ((s[i] & 0xC0) != 0x80)
    {
      *ppText += i;
      return '?';
    }
    c = (c << 6) | (s[i] & 0x3F);
  }
  *ppText += 1 + more;
  return c;
}
