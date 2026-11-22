/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "data.h"

#include "script_api.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

uint32_t bmDataTypeSize(BmDataType type)
{
  switch (type)
  {
  case BM_DATA_BOOL: case BM_DATA_BYTE: case BM_DATA_UBYTE: case BM_DATA_ENUM: return 1;
  case BM_DATA_SHORT: case BM_DATA_USHORT: case BM_DATA_NAME: case BM_DATA_TEXT:
  case BM_DATA_SPRITE: case BM_DATA_SOUND: case BM_DATA_ATTACH: return 2;
  case BM_DATA_INT: case BM_DATA_UINT: case BM_DATA_FIX: case BM_DATA_RECORD: return 4;
  default: return 0;
  }
}

bool bmDataTableOpen(BmDataTable *pTable, const uint8_t *pData, uint32_t size)
{
  if (pData == NULL || size < BM_DATA_HEADER_SIZE) return false;
  BmDataTable t;
  t.structure = rd16(pData);
  t.recordCount = rd16(pData + 2);
  t.recordSize = rd16(pData + 4);
  t.fieldCount = rd16(pData + 6);
  t.layoutHash = rd32(pData + 8);
  if (t.recordSize == 0 || t.recordSize % 4 != 0 || t.recordSize > BM_DATA_MAX_RECORD_SIZE) return false;
  if (t.fieldCount > BM_DATA_MAX_FIELDS) return false;
  uint32_t need = BM_DATA_HEADER_SIZE + (uint32_t)t.fieldCount * BM_DATA_FIELD_SIZE + (uint32_t)t.recordCount * BM_DATA_KEY_SIZE +
                  (uint32_t)(t.recordCount + 1) * t.recordSize;
  if (need > size) return false;
  t.pFields = pData + BM_DATA_HEADER_SIZE;
  t.pKeys = t.pFields + t.fieldCount * BM_DATA_FIELD_SIZE;
  t.pDefaults = t.pKeys + t.recordCount * BM_DATA_KEY_SIZE;
  t.pRecords = t.pDefaults + t.recordSize;
  for (uint16_t f = 0; f < t.fieldCount; f++)
  {
    BmDataField d = bmDataTableField(&t, f);
    uint32_t element = bmDataTypeSize((BmDataType)d.type);
    if (d.type >= BM_DATA_TYPE_COUNT || d.count == 0) return false;
    if (element != 0 && d.offset + element * d.count > t.recordSize) return false;
    if (element == 0 && d.offset >= t.recordSize) return false;
  }
  for (uint16_t r = 1; r < t.recordCount; r++)
    if (bmDataRecordKey(&t, (uint16_t)(r - 1)) >= bmDataRecordKey(&t, r)) return false;
  *pTable = t;
  return true;
}

// Tables found lately: looking one up walks the pack's chunks and checks the table, too slow to
// do for every record a script reads (several each frame). An entry holds for one pack: the
// same place, size and checksum; a pack pushed over the link has another checksum.
#define CACHE_SIZE 4
static struct
{
  const uint8_t *pPackData;
  uint32_t packSize, packCrc;
  uint16_t structure;
  BmDataTable table;
} s_cache[CACHE_SIZE];
static uint8_t s_cacheNext;

bool bmDataFindTable(const BmPack *pPack, uint16_t structure, BmDataTable *pTable)
{
  for (uint8_t i = 0; i < CACHE_SIZE; i++)
  {
    if (s_cache[i].pPackData == pPack->pData && s_cache[i].pPackData != NULL && s_cache[i].packSize == pPack->size &&
        s_cache[i].packCrc == pPack->header.crc32 && s_cache[i].structure == structure)
    {
      *pTable = s_cache[i].table;
      return true;
    }
  }
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_DTAB, 0); c >= 0; c = bmPackFindChunk(pPack, BM_CHUNK_DTAB, (uint32_t)c + 1))
  {
    uint32_t size = 0;
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)c, &size);
    if (p == NULL || size < 2 || rd16(p) != structure) continue;
    if (!bmDataTableOpen(pTable, p, size)) return false;
    s_cache[s_cacheNext].pPackData = pPack->pData;
    s_cache[s_cacheNext].packSize = pPack->size;
    s_cache[s_cacheNext].packCrc = pPack->header.crc32;
    s_cache[s_cacheNext].structure = structure;
    s_cache[s_cacheNext].table = *pTable;
    s_cacheNext = (uint8_t)((s_cacheNext + 1) % CACHE_SIZE);
    return true;
  }
  return false;
}

bool bmDataFindRecord(const BmPack *pPack, uint32_t key, BmDataTable *pTable, const uint8_t **ppRecord)
{
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_DTAB, 0); key != 0 && c >= 0; c = bmPackFindChunk(pPack, BM_CHUNK_DTAB, (uint32_t)c + 1))
  {
    uint32_t size = 0;
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)c, &size);
    if (p == NULL || !bmDataTableOpen(pTable, p, size)) continue;
    *ppRecord = bmDataRecord(pTable, key);
    if (*ppRecord != NULL) return true;
  }
  return false;
}

uint16_t bmDataRecordBehaviour(const BmDataTable *pTable, const uint8_t *pRecord)
{
  for (uint16_t f = 0; f < pTable->fieldCount; f++)
  {
    BmDataField field = bmDataTableField(pTable, f);
    if (field.type == BM_DATA_ATTACH) return (uint16_t)bmDataRead(pTable, pRecord, field.offset, BM_DATA_ATTACH);
  }
  return BM_DATA_NONE_ID;
}

BmDataField bmDataTableField(const BmDataTable *pTable, uint16_t index)
{
  const uint8_t *p = pTable->pFields + (uint32_t)index * BM_DATA_FIELD_SIZE;
  BmDataField f = { rd16(p), p[2], p[3], rd16(p + 4), rd16(p + 6) };
  return f;
}

uint32_t bmDataRecordKey(const BmDataTable *pTable, uint16_t index)
{
  return rd32(pTable->pKeys + (uint32_t)index * BM_DATA_KEY_SIZE);
}

uint16_t bmDataRecordName(const BmDataTable *pTable, uint16_t index)
{
  return rd16(pTable->pKeys + (uint32_t)index * BM_DATA_KEY_SIZE + 4);
}

const uint8_t *bmDataRecord(const BmDataTable *pTable, uint32_t key)
{
  uint32_t lo = 0, hi = pTable->recordCount;
  while (lo < hi)
  {
    uint32_t mid = (lo + hi) / 2;
    uint32_t k = bmDataRecordKey(pTable, (uint16_t)mid);
    if (k == key) return pTable->pRecords + mid * pTable->recordSize;
    if (k < key) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
}

const uint8_t *bmDataRecordOrDefaults(const BmDataTable *pTable, uint32_t key)
{
  const uint8_t *p = key != 0 ? bmDataRecord(pTable, key) : NULL;
  return p != NULL ? p : pTable->pDefaults;
}

int32_t bmDataRead(const BmDataTable *pTable, const uint8_t *pRecord, uint16_t offset, BmDataType type)
{
  uint32_t size = bmDataTypeSize(type);
  if (size == 0 || offset + size > pTable->recordSize) return 0;
  const uint8_t *p = pRecord + offset;
  switch (type)
  {
  case BM_DATA_BYTE: return (int8_t)p[0];
  case BM_DATA_SHORT: return (int16_t)rd16(p);
  case BM_DATA_BOOL: case BM_DATA_UBYTE: case BM_DATA_ENUM: return p[0];
  case BM_DATA_USHORT: case BM_DATA_NAME: case BM_DATA_TEXT: case BM_DATA_SPRITE: case BM_DATA_SOUND: case BM_DATA_ATTACH: return rd16(p);
  default: return (int32_t)rd32(p);
  }
}

bool bmDataScriptCall(const BmPack *pPack, uint16_t api, const int32_t *pArgs, int32_t *pResult)
{
  if (api != BM_SCRIPT_API_record_get && api != BM_SCRIPT_API_record_count && api != BM_SCRIPT_API_record_at) return false;
  *pResult = 0;
  BmDataTable t;
  if (pPack == NULL || !bmDataFindTable(pPack, (uint16_t)pArgs[0], &t)) return true;
  if (api == BM_SCRIPT_API_record_count) *pResult = t.recordCount;
  else if (api == BM_SCRIPT_API_record_at)
    *pResult = pArgs[1] >= 0 && pArgs[1] < t.recordCount ? (int32_t)bmDataRecordKey(&t, (uint16_t)pArgs[1]) : 0;
  else
  {
    uint32_t field = (uint32_t)pArgs[2];
    *pResult = bmDataRead(&t, bmDataRecordOrDefaults(&t, (uint32_t)pArgs[1]), (uint16_t)(field & 0xFFFF), (BmDataType)(field >> 16));
  }
  return true;
}
