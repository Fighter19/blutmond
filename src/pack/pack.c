/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "pack.h"

#include <string.h>

#ifdef BM_PLATFORM_HOST
#  include <stdlib.h>
#endif

// CRC-32 with a 16-entry table (four bits at a time): small enough for ROM, fast enough for
// what the runtime checks.
static const uint32_t s_crcNibble[16] = {
  0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
  0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

uint32_t bmCrc32(uint32_t crc, const void *pData, size_t size)
{
  const uint8_t *p = (const uint8_t *)pData;
  crc = ~crc;
  for (size_t i = 0; i < size; i++)
  {
    crc ^= p[i];
    crc = (crc >> 4) ^ s_crcNibble[crc & 15];
    crc = (crc >> 4) ^ s_crcNibble[crc & 15];
  }
  return ~crc;
}

static void readHeader(const uint8_t *p, BmPackHeader *h)
{
  h->magic = bmRead32(p + 0);
  h->formatMajor = bmRead16(p + 4);
  h->formatMinor = bmRead16(p + 6);
  h->abiMajor = bmRead16(p + 8);
  h->abiMinor = bmRead16(p + 10);
  h->profile = bmRead32(p + 12);
  h->totalSize = bmRead32(p + 16);
  h->chunkCount = bmRead32(p + 20);
  h->tocOffset = bmRead32(p + 24);
  h->flags = bmRead16(p + 28);
  h->reserved = bmRead16(p + 30);
  h->crc32 = bmRead32(p + 32);
}

static void readEntry(const uint8_t *p, BmChunkEntry *e)
{
  e->id = bmRead32(p + 0);
  e->offset = bmRead32(p + 4);
  e->size = bmRead32(p + 8);
  e->unpackedSize = bmRead32(p + 12);
  e->compression = p[16];
  e->category = p[17];
  e->group = bmRead16(p + 18);
  e->hash = bmRead32(p + 20);
}

BmPackError bmPackOpen(BmPack *pPack, const void *pData, uint32_t size, bool verifyCrc)
{
  memset(pPack, 0, sizeof(*pPack));
  if (pData == NULL || size < BM_PACK_HEADER_SIZE)
  {
    return BM_PACK_ERROR_TOO_SMALL;
  }
  const uint8_t *p = (const uint8_t *)pData;
  BmPackHeader h;
  readHeader(p, &h);
  if (h.magic != BM_FOURCC('B', 'M', 'P', 'K'))
  {
    return BM_PACK_ERROR_MAGIC;
  }
  if (h.formatMajor != BM_PACK_FORMAT_MAJOR)
  {
    return BM_PACK_ERROR_VERSION;
  }
  if (h.totalSize > size || h.totalSize < BM_PACK_HEADER_SIZE)
  {
    return BM_PACK_ERROR_SIZE;
  }
  if (h.tocOffset < BM_PACK_HEADER_SIZE || h.tocOffset > h.totalSize ||
      h.chunkCount > (h.totalSize - h.tocOffset) / BM_PACK_CHUNK_ENTRY_SIZE)
  {
    return BM_PACK_ERROR_TOC;
  }
  for (uint32_t i = 0; i < h.chunkCount; i++)
  {
    BmChunkEntry e;
    readEntry(p + h.tocOffset + i * BM_PACK_CHUNK_ENTRY_SIZE, &e);
    if (e.offset > h.totalSize || e.size > h.totalSize - e.offset)
    {
      return BM_PACK_ERROR_TOC;
    }
  }
  if (verifyCrc && bmCrc32(0, p + BM_PACK_HEADER_SIZE, h.totalSize - BM_PACK_HEADER_SIZE) != h.crc32)
  {
    return BM_PACK_ERROR_CRC;
  }
  pPack->pData = p;
  pPack->size = h.totalSize;
  pPack->header = h;
  return BM_PACK_OK;
}

uint32_t bmPackChunkCount(const BmPack *pPack)
{
  return pPack->header.chunkCount;
}

bool bmPackGetChunk(const BmPack *pPack, uint32_t index, BmChunkEntry *pEntry)
{
  if (pPack->pData == NULL || index >= pPack->header.chunkCount)
  {
    return false;
  }
  readEntry(pPack->pData + pPack->header.tocOffset + index * BM_PACK_CHUNK_ENTRY_SIZE, pEntry);
  return true;
}

int32_t bmPackFindChunk(const BmPack *pPack, uint32_t id, uint32_t start)
{
  BmChunkEntry e;
  for (uint32_t i = start; bmPackGetChunk(pPack, i, &e); i++)
  {
    if (e.id == id)
    {
      return (int32_t)i;
    }
  }
  return -1;
}

const uint8_t *bmPackChunkData(const BmPack *pPack, uint32_t index, uint32_t *pSize)
{
  BmChunkEntry e;
  if (!bmPackGetChunk(pPack, index, &e) || e.compression != BM_PACK_COMPRESSION_NONE)
  {
    return NULL;
  }
  if (pSize != NULL)
  {
    *pSize = e.size;
  }
  return pPack->pData + e.offset;
}

const char *bmPackString(const BmPack *pPack, uint16_t id)
{
  int32_t index = bmPackFindChunk(pPack, BM_CHUNK_STRS, 0);
  uint32_t size = 0;
  const uint8_t *p = index >= 0 ? bmPackChunkData(pPack, (uint32_t)index, &size) : NULL;
  if (p == NULL || size < 4 || id >= bmRead16(p) || 4u + id * 4u + 4u > size)
  {
    return NULL;
  }
  uint32_t offset = bmRead32(p + 4 + id * 4u);
  return offset < size ? (const char *)p + offset : NULL;
}

#ifdef BM_PLATFORM_HOST
// --- Writer ------------------------------------------------------------------------------

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
  put16(p, (uint16_t)v);
  put16(p + 2, (uint16_t)(v >> 16));
}

void bmPackWriterInit(BmPackWriter *pWriter, uint32_t profile, BmPackLayout layout)
{
  memset(pWriter, 0, sizeof(*pWriter));
  pWriter->profile = profile;
  pWriter->layout = layout;
}

void bmPackWriterFree(BmPackWriter *pWriter)
{
  for (uint32_t i = 0; i < pWriter->chunkCount; i++)
  {
    free(pWriter->pChunks[i].pData);
  }
  free(pWriter->pChunks);
  memset(pWriter, 0, sizeof(*pWriter));
}

uint32_t bmPackWriterAdd(BmPackWriter *pWriter, uint32_t id, BmChunkCategory category, uint16_t group,
                         const void *pData, uint32_t size)
{
  if (pWriter->chunkCount == pWriter->chunkCapacity)
  {
    uint32_t capacity = pWriter->chunkCapacity ? pWriter->chunkCapacity * 2 : 16;
    pWriter->pChunks = realloc(pWriter->pChunks, capacity * sizeof(BmPackWriterChunk));
    pWriter->chunkCapacity = capacity;
  }
  BmPackWriterChunk *c = &pWriter->pChunks[pWriter->chunkCount];
  memset(c, 0, sizeof(*c));
  c->entry.id = id;
  c->entry.size = size;
  c->entry.unpackedSize = size;
  c->entry.compression = BM_PACK_COMPRESSION_NONE;
  c->entry.category = (uint8_t)category;
  c->entry.group = group;
  c->entry.hash = bmCrc32(0, pData, size);
  c->pData = malloc(size ? size : 1);
  if (size)
  {
    memcpy(c->pData, pData, size);
  }
  return pWriter->chunkCount++;
}

uint8_t *bmPackWriterFinish(const BmPackWriter *pWriter, uint32_t *pSize)
{
  uint32_t tocOffset = BM_PACK_HEADER_SIZE;
  uint32_t dataOffset = tocOffset + pWriter->chunkCount * BM_PACK_CHUNK_ENTRY_SIZE;
  uint32_t total = dataOffset;
  for (uint32_t i = 0; i < pWriter->chunkCount; i++)
  {
    total += (pWriter->pChunks[i].entry.size + 3u) & ~3u;
  }

  uint8_t *p = calloc(1, total);
  uint32_t offset = dataOffset;
  for (uint32_t i = 0; i < pWriter->chunkCount; i++)
  {
    const BmPackWriterChunk *c = &pWriter->pChunks[i];
    uint8_t *e = p + tocOffset + i * BM_PACK_CHUNK_ENTRY_SIZE;
    put32(e + 0, c->entry.id);
    put32(e + 4, offset);
    put32(e + 8, c->entry.size);
    put32(e + 12, c->entry.unpackedSize);
    e[16] = c->entry.compression;
    e[17] = c->entry.category;
    put16(e + 18, c->entry.group);
    put32(e + 20, c->entry.hash);
    memcpy(p + offset, c->pData, c->entry.size);
    offset += (c->entry.size + 3u) & ~3u;
  }

  put32(p + 0, BM_FOURCC('B', 'M', 'P', 'K'));
  put16(p + 4, BM_PACK_FORMAT_MAJOR);
  put16(p + 6, BM_PACK_FORMAT_MINOR);
  put16(p + 8, 0);
  put16(p + 10, 1);
  put32(p + 12, pWriter->profile);
  put32(p + 16, total);
  put32(p + 20, pWriter->chunkCount);
  put32(p + 24, tocOffset);
  put16(p + 28, (uint16_t)pWriter->layout);
  put16(p + 30, 0);
  put32(p + 32, bmCrc32(0, p + BM_PACK_HEADER_SIZE, total - BM_PACK_HEADER_SIZE));

  *pSize = total;
  return p;
}
#endif
