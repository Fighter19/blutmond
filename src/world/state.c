/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "state.h"

#include "pack.h"

#include <string.h>

#define STATE_MAGIC BM_FOURCC('B', 'M', 'S', 'T')
#define STATE_VERSION 1

uint32_t bmStateTypeSize(uint8_t type)
{
  switch (type)
  {
  case BM_TYPE_U8: case BM_TYPE_S8: case BM_TYPE_BOOL: return 1;
  case BM_TYPE_U16: case BM_TYPE_S16: return 2;
  default: return 4;
  }
}

// The hash only depends on the blocks' field tables, which do not change: worked out once per
// set of tables (over every field's name it cost a GBA a good part of a frame per save game).
#define HASH_CACHE 4
#define HASH_BLOCKS 4
static struct
{
  uint32_t blockCount;
  const BmStateField *fields[HASH_BLOCKS];
  uint16_t fieldCount[HASH_BLOCKS];
  const char *name[HASH_BLOCKS];
  uint32_t hash;
} s_hashCache[HASH_CACHE] BM_EWRAM_BSS;
static uint8_t s_hashNext BM_EWRAM_BSS;

static uint32_t schemaHash(const BmStateBlock *pBlocks, uint32_t blockCount);

uint32_t bmStateSchemaHash(const BmStateBlock *pBlocks, uint32_t blockCount)
{
  if (blockCount == 0 || blockCount > HASH_BLOCKS) return schemaHash(pBlocks, blockCount);
  for (uint32_t c = 0; c < HASH_CACHE; c++)
  {
    bool same = s_hashCache[c].blockCount == blockCount;
    for (uint32_t b = 0; same && b < blockCount; b++)
      same = s_hashCache[c].fields[b] == pBlocks[b].pFields && s_hashCache[c].fieldCount[b] == pBlocks[b].fieldCount &&
             s_hashCache[c].name[b] == pBlocks[b].name;
    if (same) return s_hashCache[c].hash;
  }
  uint32_t hash = schemaHash(pBlocks, blockCount);
  uint8_t c = s_hashNext;
  s_hashNext = (uint8_t)((s_hashNext + 1) % HASH_CACHE);
  s_hashCache[c].blockCount = blockCount;
  for (uint32_t b = 0; b < blockCount; b++)
  {
    s_hashCache[c].fields[b] = pBlocks[b].pFields;
    s_hashCache[c].fieldCount[b] = pBlocks[b].fieldCount;
    s_hashCache[c].name[b] = pBlocks[b].name;
  }
  s_hashCache[c].hash = hash;
  return hash;
}

static uint32_t schemaHash(const BmStateBlock *pBlocks, uint32_t blockCount)
{
  uint32_t crc = 0;
  for (uint32_t b = 0; b < blockCount; b++)
  {
    crc = bmCrc32(crc, pBlocks[b].name, strlen(pBlocks[b].name) + 1);
    for (uint16_t i = 0; i < pBlocks[b].fieldCount; i++)
    {
      const BmStateField *f = &pBlocks[b].pFields[i];
      uint8_t meta[4] = { f->type, f->flags, (uint8_t)f->count, (uint8_t)(f->count >> 8) };
      crc = bmCrc32(crc, f->name, strlen(f->name) + 1);
      crc = bmCrc32(crc, meta, sizeof(meta));
    }
  }
  return crc;
}

static bool selected(const BmStateField *f, uint8_t mask)
{
  // save and link imply snapshot (state.md section 2); transient and derived are never written.
  uint8_t flags = f->flags;
  if (flags & (BM_STATE_TRANSIENT | BM_STATE_DERIVED)) return false;
  if (flags & (BM_STATE_SAVE | BM_STATE_LINK)) flags |= BM_STATE_SNAPSHOT;
  if (flags == 0) flags = BM_STATE_SNAPSHOT;
  return (flags & mask) != 0;
}

uint32_t bmStateSize(const BmStateBlock *pBlocks, uint32_t blockCount, uint8_t flagMask)
{
  uint32_t size = BM_STATE_HEADER_SIZE;
  for (uint32_t b = 0; b < blockCount; b++)
    for (uint16_t i = 0; i < pBlocks[b].fieldCount; i++)
      if (selected(&pBlocks[b].pFields[i], flagMask))
        size += bmStateTypeSize(pBlocks[b].pFields[i].type) * pBlocks[b].pFields[i].count;
  return size;
}

static void put(uint8_t *p, uint32_t v, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get(const uint8_t *p, uint32_t n)
{
  uint32_t v = 0;
  for (uint32_t i = 0; i < n; i++) v |= (uint32_t)p[i] << (8 * i);
  return v;
}

static uint32_t loadValue(const uint8_t *src, uint8_t type)
{
  switch (type)
  {
  case BM_TYPE_U8: case BM_TYPE_S8: return *src;
  case BM_TYPE_BOOL: return *(const bool *)(const void *)src ? 1 : 0;
  case BM_TYPE_U16: case BM_TYPE_S16: { uint16_t v; memcpy(&v, src, 2); return v; }
  default: { uint32_t v; memcpy(&v, src, 4); return v; }
  }
}

static void storeValue(uint8_t *dst, uint8_t type, uint32_t v)
{
  switch (type)
  {
  case BM_TYPE_U8: case BM_TYPE_S8: *dst = (uint8_t)v; break;
  case BM_TYPE_BOOL: *(bool *)(void *)dst = v != 0; break;
  case BM_TYPE_U16: case BM_TYPE_S16: { uint16_t x = (uint16_t)v; memcpy(dst, &x, 2); break; }
  default: memcpy(dst, &v, 4); break;
  }
}

uint32_t bmStateWrite(const BmStateBlock *pBlocks, uint32_t blockCount, uint8_t flagMask, uint8_t *pOut, uint32_t capacity)
{
  uint32_t size = bmStateSize(pBlocks, blockCount, flagMask);
  if (size > capacity) return 0;
  uint8_t *p = pOut + BM_STATE_HEADER_SIZE;
  for (uint32_t b = 0; b < blockCount; b++)
  {
    const uint8_t *base = (const uint8_t *)pBlocks[b].pBase;
    for (uint16_t i = 0; i < pBlocks[b].fieldCount; i++)
    {
      const BmStateField *f = &pBlocks[b].pFields[i];
      if (!selected(f, flagMask)) continue;
      uint32_t n = bmStateTypeSize(f->type);
      uint32_t stride = f->type == BM_TYPE_BOOL ? sizeof(bool) : n;
      for (uint16_t k = 0; k < f->count; k++)
      {
        put(p, loadValue(base + f->offset + k * stride, f->type), n);
        p += n;
      }
    }
  }
  uint32_t payload = size - BM_STATE_HEADER_SIZE;
  put(pOut + 0, STATE_MAGIC, 4);
  pOut[4] = STATE_VERSION;
  pOut[5] = flagMask;
  put(pOut + 6, payload, 2);
  put(pOut + 8, bmStateSchemaHash(pBlocks, blockCount), 4);
  put(pOut + 12, bmCrc32(0, pOut + BM_STATE_HEADER_SIZE, payload), 4);
  return size;
}

BmStateReadResult bmStateRead(const BmStateBlock *pBlocks, uint32_t blockCount, const uint8_t *pIn, uint32_t size)
{
  if (size < BM_STATE_HEADER_SIZE || get(pIn, 4) != STATE_MAGIC || pIn[4] != STATE_VERSION) return BM_STATE_BAD_HEADER;
  uint8_t mask = pIn[5];
  uint32_t payload = get(pIn + 6, 2);
  if (get(pIn + 8, 4) != bmStateSchemaHash(pBlocks, blockCount) ||
      payload + BM_STATE_HEADER_SIZE != bmStateSize(pBlocks, blockCount, mask))
  {
    return BM_STATE_OTHER_SCHEMA;
  }
  if (size < payload + BM_STATE_HEADER_SIZE || get(pIn + 12, 4) != bmCrc32(0, pIn + BM_STATE_HEADER_SIZE, payload))
  {
    return BM_STATE_BAD_CRC;
  }
  const uint8_t *p = pIn + BM_STATE_HEADER_SIZE;
  for (uint32_t b = 0; b < blockCount; b++)
  {
    uint8_t *base = (uint8_t *)pBlocks[b].pBase;
    for (uint16_t i = 0; i < pBlocks[b].fieldCount; i++)
    {
      const BmStateField *f = &pBlocks[b].pFields[i];
      if (!selected(f, mask)) continue;
      uint32_t n = bmStateTypeSize(f->type);
      uint32_t stride = f->type == BM_TYPE_BOOL ? sizeof(bool) : n;
      for (uint16_t k = 0; k < f->count; k++)
      {
        storeValue(base + f->offset + k * stride, f->type, get(p, n));
        p += n;
      }
    }
  }
  return BM_STATE_OK;
}

uint32_t bmStateWrittenSchema(const uint8_t *pIn, uint32_t size)
{
  if (size < BM_STATE_HEADER_SIZE || get(pIn, 4) != STATE_MAGIC || pIn[4] != STATE_VERSION) return 0;
  return get(pIn + 8, 4);
}

static uint32_t nameHash(const char *block, const char *field)
{
  uint32_t h = 2166136261u;
  for (const char *p = block; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  h = (h ^ (uint8_t)'.') * 16777619u;
  for (const char *p = field; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

// A value of an earlier type as today's: signed ones keep their sign.
static uint32_t widen(uint32_t v, uint8_t from)
{
  if (from == BM_TYPE_S8) return (uint32_t)(int32_t)(int8_t)v;
  if (from == BM_TYPE_S16) return (uint32_t)(int32_t)(int16_t)v;
  return v;
}

BmStateReadResult bmStateMigrate(const BmStateBlock *pBlocks, uint32_t blockCount, const uint8_t *pIn, uint32_t size,
                                 const uint8_t *pLayout, uint16_t fieldCount)
{
  if (size < BM_STATE_HEADER_SIZE || get(pIn, 4) != STATE_MAGIC || pIn[4] != STATE_VERSION) return BM_STATE_BAD_HEADER;
  uint8_t mask = pIn[5];
  uint32_t payload = get(pIn + 6, 2), need = 0;
  for (uint16_t i = 0; i < fieldCount; i++) need += bmStateTypeSize(pLayout[i * 8 + 4]) * get(pLayout + i * 8 + 6, 2);
  if (need != payload) return BM_STATE_OTHER_SCHEMA;
  if (size < payload + BM_STATE_HEADER_SIZE || get(pIn + 12, 4) != bmCrc32(0, pIn + BM_STATE_HEADER_SIZE, payload)) return BM_STATE_BAD_CRC;
  const uint8_t *p = pIn + BM_STATE_HEADER_SIZE;
  for (uint16_t i = 0; i < fieldCount; i++)
  {
    uint32_t name = get(pLayout + i * 8, 4), oldCount = get(pLayout + i * 8 + 6, 2);
    uint8_t oldType = pLayout[i * 8 + 4];
    uint32_t oldSize = bmStateTypeSize(oldType);
    // Today's field of that name (one the same flags would write).
    const BmStateField *now = NULL;
    uint8_t *base = NULL;
    for (uint32_t b = 0; b < blockCount && now == NULL; b++)
      for (uint16_t k = 0; k < pBlocks[b].fieldCount && now == NULL; k++)
      {
        const BmStateField *f = &pBlocks[b].pFields[k];
        if (selected(f, mask) && nameHash(pBlocks[b].name, f->name) == name)
        {
          now = f;
          base = (uint8_t *)pBlocks[b].pBase;
        }
      }
    for (uint32_t k = 0; k < oldCount; k++)
    {
      if (now != NULL && k < now->count)
      {
        uint32_t stride = now->type == BM_TYPE_BOOL ? sizeof(bool) : bmStateTypeSize(now->type);
        storeValue(base + now->offset + k * stride, now->type, widen(get(p, oldSize), oldType));
      }
      p += oldSize;
    }
  }
  return BM_STATE_OK;
}
