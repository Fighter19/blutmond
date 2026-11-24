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
  e->compression = (uint8_t)(p[16] & ~BM_PACK_ENTRY_FLAGS);
  e->flags = (uint8_t)(p[16] & BM_PACK_ENTRY_FLAGS);
  e->category = p[17];
  e->group = bmRead16(p + 18);
  e->hash = bmRead32(p + 20);
}

BmPackError bmPackOpen(BmPack *pPack, const void *pData, uint32_t size, bool verifyCrc)
{
  return bmPackOpenSet(pPack, pData, size, verifyCrc, NULL);
}

BmPackError bmPackOpenSet(BmPack *pPack, const void *pData, uint32_t size, bool verifyCrc, const BmPack *pBase)
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
  // An update needs a base that is not an update itself; its in-base entries lie inside it.
  bool update = (h.flags & BM_PACK_FLAG_UPDATE) != 0;
  if (update && (pBase == NULL || pBase->pData == NULL || (pBase->header.flags & BM_PACK_FLAG_UPDATE))) return BM_PACK_ERROR_TOC;
  uint32_t baseSize = update ? pBase->size : 0;
  for (uint32_t i = 0; i < h.chunkCount; i++)
  {
    BmChunkEntry e;
    readEntry(p + h.tocOffset + i * BM_PACK_CHUNK_ENTRY_SIZE, &e);
    uint32_t within = (e.flags & BM_PACK_ENTRY_IN_BASE) ? baseSize : (e.flags & BM_PACK_ENTRY_ABSENT) ? 0 : h.totalSize;
    if ((e.flags & BM_PACK_ENTRY_IN_BASE) && !update) return BM_PACK_ERROR_TOC;
    if (e.offset > within || e.size > within - e.offset)
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
  pPack->pBase = update ? pBase->pData : NULL;
  pPack->baseSize = baseSize;
  pPack->pStrings = NULL;
  pPack->stringsSize = 0;
  int32_t strings = bmPackFindChunk(pPack, BM_CHUNK_STRS, 0);
  if (strings >= 0) pPack->pStrings = bmPackChunkData(pPack, (uint32_t)strings, &pPack->stringsSize);
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
  // Only the ids are read (the whole entry, byte by byte, made a room's scripts slow to find).
  if (pPack->pData == NULL) return -1;
  const uint8_t *p = pPack->pData + pPack->header.tocOffset;
  for (uint32_t i = start; i < pPack->header.chunkCount; i++)
  {
    if (bmRead32(p + i * BM_PACK_CHUNK_ENTRY_SIZE) == id)
    {
      return (int32_t)i;
    }
  }
  return -1;
}

// Where an entry's bytes lie: in the pack, in its base, nowhere (absent).
static const uint8_t *storedAt(const BmPack *pPack, const BmChunkEntry *e)
{
  if (e->flags & BM_PACK_ENTRY_ABSENT) return NULL;
  if (e->flags & BM_PACK_ENTRY_IN_BASE) return pPack->pBase != NULL ? pPack->pBase + e->offset : NULL;
  return pPack->pData + e->offset;
}

const uint8_t *bmPackChunkStored(const BmPack *pPack, uint32_t index, uint32_t *pSize)
{
  BmChunkEntry e;
  if (!bmPackGetChunk(pPack, index, &e)) return NULL;
  const uint8_t *p = storedAt(pPack, &e);
  if (p != NULL && pSize != NULL) *pSize = e.size;
  return p;
}

const uint8_t *bmPackChunkData(const BmPack *pPack, uint32_t index, uint32_t *pSize)
{
  BmChunkEntry e;
  if (!bmPackGetChunk(pPack, index, &e) || e.compression != BM_PACK_COMPRESSION_NONE)
  {
    return NULL;
  }
  const uint8_t *p = storedAt(pPack, &e);
  if (p != NULL && pSize != NULL)
  {
    *pSize = e.size;
  }
  return p;
}

uint32_t bmLz77Unpack(const uint8_t *pIn, uint32_t inSize, uint8_t *pOut, uint32_t outSize)
{
  if (inSize < 4 || pIn[0] != 0x10) return 0;
  uint32_t size = bmRead32(pIn) >> 8, i = 4, o = 0;
  if (size > outSize) return 0;
#ifdef BM_PLATFORM_GBA
  // The BIOS (LZ77UnCompReadNormalWrite8bit): this C, as Thumb code from EWRAM, took 44 cycles
  // a byte (880,000 for a room); IWRAM has no room for it. It wants the data 4-byte aligned
  // (chunks are) and keeps interrupts as they are. It writes no more than the header says.
  if (((uintptr_t)pIn & 3) == 0)
  {
    register const uint8_t *r0 __asm__("r0") = pIn;
    register uint8_t *r1 __asm__("r1") = pOut;
    __asm__ volatile("swi 0x11" : "+r"(r0), "+r"(r1) : : "r2", "r3", "memory");
    (void)inSize;
    return size;
  }
#endif
  while (o < size)
  {
    if (i >= inSize) return 0;
    uint32_t flags = pIn[i++];
    for (uint32_t b = 0; b < 8 && o < size; b++, flags <<= 1)
    {
      if (!(flags & 0x80))
      {
        if (i >= inSize) return 0;
        pOut[o++] = pIn[i++];
        continue;
      }
      if (i + 1 >= inSize) return 0;
      uint32_t length = (pIn[i] >> 4) + 3u, distance = (((pIn[i] & 15u) << 8) | pIn[i + 1]) + 1u;
      i += 2;
      if (distance > o) return 0;
      if (length > size - o) length = size - o;
      // Byte by byte: a copy may overlap what it writes (a run).
      for (const uint8_t *pFrom = pOut + o - distance; length != 0; length--) pOut[o++] = *pFrom++;
    }
  }
  return size;
}

const uint8_t *bmPackChunkUnpack(const BmPack *pPack, uint32_t index, uint8_t *pOut, uint32_t outSize, uint32_t *pSize)
{
  BmChunkEntry e;
  if (!bmPackGetChunk(pPack, index, &e)) return NULL;
  const uint8_t *p = storedAt(pPack, &e);
  if (p == NULL) return NULL;
  if (e.compression == BM_PACK_COMPRESSION_NONE)
  {
    if (pSize != NULL) *pSize = e.size;
    return p;
  }
  if (e.compression != BM_PACK_COMPRESSION_LZ77 || pOut == NULL || e.unpackedSize > outSize) return NULL;
  if (bmLz77Unpack(p, e.size, pOut, outSize) != e.unpackedSize) return NULL;
  if (pSize != NULL) *pSize = e.unpackedSize;
  return pOut;
}

const char *bmPackString(const BmPack *pPack, uint16_t id)
{
  const uint8_t *p = pPack->pStrings;
  uint32_t size = pPack->stringsSize;
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

uint32_t bmPackWriterAddEntry(BmPackWriter *pWriter, const BmChunkEntry *pEntry, uint8_t flags)
{
  if (pWriter->chunkCount == pWriter->chunkCapacity)
  {
    uint32_t capacity = pWriter->chunkCapacity ? pWriter->chunkCapacity * 2 : 16;
    pWriter->pChunks = realloc(pWriter->pChunks, capacity * sizeof(BmPackWriterChunk));
    pWriter->chunkCapacity = capacity;
  }
  BmPackWriterChunk *c = &pWriter->pChunks[pWriter->chunkCount];
  memset(c, 0, sizeof(*c));
  c->entry = *pEntry;
  c->entry.flags = (uint8_t)(flags & BM_PACK_ENTRY_FLAGS);
  return pWriter->chunkCount++;
}

uint32_t bmLz77Pack(const uint8_t *pIn, uint32_t size, uint8_t *pOut)
{
  put32(pOut, 0x10u | (size << 8));
  uint32_t o = 4, i = 0;
  while (i < size)
  {
    uint32_t flagsAt = o++;
    uint8_t flags = 0;
    for (uint32_t b = 0; b < 8 && i < size; b++)
    {
      // The longest match in the 4096 bytes before (the nearest of equal length), 3 to 18.
      uint32_t best = 0, bestDistance = 0;
      for (uint32_t d = 1; d <= 4096 && d <= i && best < 18; d++)
      {
        uint32_t n = 0;
        while (n < 18 && i + n < size && pIn[i + n] == pIn[i + n - d]) n++;
        if (n > best)
        {
          best = n;
          bestDistance = d;
        }
      }
      if (best >= 3)
      {
        flags |= (uint8_t)(0x80u >> b);
        pOut[o++] = (uint8_t)(((best - 3) << 4) | ((bestDistance - 1) >> 8));
        pOut[o++] = (uint8_t)(bestDistance - 1);
        i += best;
      }
      else
      {
        pOut[o++] = pIn[i++];
      }
    }
    pOut[flagsAt] = flags;
  }
  return o;
}

bool bmPackWriterCompress(BmPackWriter *pWriter, uint32_t index)
{
  if (index >= pWriter->chunkCount) return false;
  BmPackWriterChunk *c = &pWriter->pChunks[index];
  if (c->entry.compression != BM_PACK_COMPRESSION_NONE) return false;
  uint32_t size = c->entry.size;
  uint8_t *pPacked = malloc(4 + size + size / 8 + 1);
  uint32_t packed = bmLz77Pack(c->pData, size, pPacked);
  if (packed >= size)
  {
    free(pPacked);
    return false;
  }
  free(c->pData);
  c->pData = pPacked;
  c->entry.size = packed;
  c->entry.unpackedSize = size;
  c->entry.compression = BM_PACK_COMPRESSION_LZ77;
  c->entry.hash = bmCrc32(0, pPacked, packed);
  return true;
}

uint8_t *bmPackMakeUpdate(const BmPack *pFull, const uint32_t *pBaseChunks, uint32_t count, uint32_t *pSize)
{
  if (pFull->header.flags & BM_PACK_FLAG_UPDATE) return NULL;
  BmPackWriter w;
  bmPackWriterInit(&w, pFull->header.profile, (BmPackLayout)(pFull->header.flags & 15));
  for (uint32_t i = 0; i < bmPackChunkCount(pFull); i++)
  {
    BmChunkEntry e = {0};
    bmPackGetChunk(pFull, i, &e);
    if (e.flags & BM_PACK_ENTRY_ABSENT)
    {
      bmPackWriterAddEntry(&w, &e, BM_PACK_ENTRY_ABSENT);
      continue;
    }
    uint32_t k = 0;
    while (k < count && !(pBaseChunks[3 * k] == e.hash && pBaseChunks[3 * k + 1] == e.size && e.size != 0)) k++;
    if (k < count)
    {
      e.offset = pBaseChunks[3 * k + 2];
      bmPackWriterAddEntry(&w, &e, BM_PACK_ENTRY_IN_BASE);
      continue;
    }
    // Its own bytes, as they are (compressed or not).
    uint32_t n = 0;
    const uint8_t *data = bmPackChunkStored(pFull, i, &n);
    uint32_t at = bmPackWriterAdd(&w, e.id, (BmChunkCategory)e.category, e.group, data, n);
    w.pChunks[at].entry.compression = e.compression;
    w.pChunks[at].entry.unpackedSize = e.unpackedSize;
    w.pChunks[at].entry.hash = e.hash;
  }
  uint8_t *out = bmPackWriterFinish(&w, pSize);
  bmPackWriterFree(&w);
  return out;
}

uint8_t *bmPackWriterFinish(const BmPackWriter *pWriter, uint32_t *pSize)
{
  uint32_t tocOffset = BM_PACK_HEADER_SIZE;
  uint32_t dataOffset = tocOffset + pWriter->chunkCount * BM_PACK_CHUNK_ENTRY_SIZE;
  uint32_t total = dataOffset;
  bool update = false;
  for (uint32_t i = 0; i < pWriter->chunkCount; i++)
  {
    if (pWriter->pChunks[i].entry.flags == 0) total += (pWriter->pChunks[i].entry.size + 3u) & ~3u;
    update |= (pWriter->pChunks[i].entry.flags & BM_PACK_ENTRY_IN_BASE) != 0;
  }

  uint8_t *p = calloc(1, total);
  uint32_t offset = dataOffset;
  for (uint32_t i = 0; i < pWriter->chunkCount; i++)
  {
    const BmPackWriterChunk *c = &pWriter->pChunks[i];
    uint8_t *e = p + tocOffset + i * BM_PACK_CHUNK_ENTRY_SIZE;
    bool absent = (c->entry.flags & BM_PACK_ENTRY_ABSENT) != 0, inBase = (c->entry.flags & BM_PACK_ENTRY_IN_BASE) != 0;
    put32(e + 0, c->entry.id);
    put32(e + 4, absent ? 0 : inBase ? c->entry.offset : offset);
    put32(e + 8, absent ? 0 : c->entry.size);
    put32(e + 12, c->entry.unpackedSize);
    e[16] = (uint8_t)(c->entry.compression | c->entry.flags);
    e[17] = c->entry.category;
    put16(e + 18, c->entry.group);
    put32(e + 20, c->entry.hash);
    if (absent || inBase) continue;
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
  put16(p + 28, (uint16_t)(pWriter->layout | (update ? BM_PACK_FLAG_UPDATE : 0)));
  put16(p + 30, 0);
  put32(p + 32, bmCrc32(0, p + BM_PACK_HEADER_SIZE, total - BM_PACK_HEADER_SIZE));

  *pSize = total;
  return p;
}
#endif
