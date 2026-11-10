/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Packs: the cooked, platform-specific form of content (docs/spec/pack-format.md).
// This header holds the binary layout, a reader for packs in memory (ROM or RAM) and, for host
// tools, a writer. All data is little endian; the reader does not depend on the host's byte
// order or alignment.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BM_PACK_FORMAT_MAJOR 0
#define BM_PACK_FORMAT_MINOR 1

#define BM_PACK_HEADER_SIZE 36
#define BM_PACK_CHUNK_ENTRY_SIZE 24

/// Four-character code as stored in a pack ("ROOM" -> bytes 'R','O','O','M').
#define BM_FOURCC(a, b, c, d) \
  ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum {
  BM_CHUNK_META = BM_FOURCC('M', 'E', 'T', 'A'),
  BM_CHUNK_VLAY = BM_FOURCC('V', 'L', 'A', 'Y'),
  BM_CHUNK_PALE = BM_FOURCC('P', 'A', 'L', 'E'),
  BM_CHUNK_TILE = BM_FOURCC('T', 'I', 'L', 'E'),
  BM_CHUNK_TMAP = BM_FOURCC('T', 'M', 'A', 'P'),
  BM_CHUNK_ROOM = BM_FOURCC('R', 'O', 'O', 'M'),
  BM_CHUNK_COLL = BM_FOURCC('C', 'O', 'L', 'L'),
  BM_CHUNK_SHAP = BM_FOURCC('S', 'H', 'A', 'P'),
  BM_CHUNK_OBJS = BM_FOURCC('O', 'B', 'J', 'S'),
  BM_CHUNK_STRS = BM_FOURCC('S', 'T', 'R', 'S'),
};

/// Storage layout (header flags bits 0-3), docs/spec/pack-format.md §2.6.
typedef enum BmPackLayout {
  BM_PACK_LAYOUT_ROM = 0,
  BM_PACK_LAYOUT_ROOM_CHAIN = 1,
  BM_PACK_LAYOUT_SEQUENTIAL = 2,
} BmPackLayout;

typedef enum BmPackCompression {
  BM_PACK_COMPRESSION_NONE = 0,
  BM_PACK_COMPRESSION_LZ77 = 1,
  BM_PACK_COMPRESSION_LZ4 = 2,
} BmPackCompression;

/// Chunk categories, docs/spec/streaming.md §3.
typedef enum BmChunkCategory {
  BM_CATEGORY_LEVEL = 0,
  BM_CATEGORY_GRAPHICS = 1,
  BM_CATEGORY_OBJECTS = 2,
  BM_CATEGORY_ACTORS = 3,
  BM_CATEGORY_PLAYER = 4,
  BM_CATEGORY_SCRIPTS = 5,
  BM_CATEGORY_MUSIC = 6,
  BM_CATEGORY_SFX = 7,
  BM_CATEGORY_TEXT = 8,
  BM_CATEGORY_META = 9,
} BmChunkCategory;

typedef struct BmPackHeader
{
  uint32_t magic;          // "BMPK"
  uint16_t formatMajor, formatMinor;
  uint16_t abiMajor, abiMinor;
  uint32_t profile;        // "GBA ", "SNES", "HOST"
  uint32_t totalSize;
  uint32_t chunkCount;
  uint32_t tocOffset;
  uint16_t flags;
  uint16_t reserved;
  uint32_t crc32;
} BmPackHeader;

typedef struct BmChunkEntry
{
  uint32_t id;
  uint32_t offset;
  uint32_t size;
  uint32_t unpackedSize;
  uint8_t compression;
  uint8_t category;
  uint16_t group;
  uint32_t hash;
} BmChunkEntry;

typedef enum BmPackError {
  BM_PACK_OK = 0,
  BM_PACK_ERROR_TOO_SMALL,
  BM_PACK_ERROR_MAGIC,
  BM_PACK_ERROR_VERSION,
  BM_PACK_ERROR_SIZE,
  BM_PACK_ERROR_TOC,
  BM_PACK_ERROR_CRC,
} BmPackError;

/// A pack in memory. Chunk data is used in place.
typedef struct BmPack
{
  const uint8_t *pData;
  uint32_t size;
  BmPackHeader header;
} BmPack;

/// CRC-32 (IEEE 802.3), as used by packs, chunks and the link protocol.
uint32_t bmCrc32(uint32_t crc, const void *pData, size_t size);

/// Checks the header and table of contents. With `verifyCrc` the whole pack is checksummed
/// (slow on the GBA; the cooker and the live link check it instead).
BmPackError bmPackOpen(BmPack *pPack, const void *pData, uint32_t size, bool verifyCrc);

uint32_t bmPackChunkCount(const BmPack *pPack);
bool bmPackGetChunk(const BmPack *pPack, uint32_t index, BmChunkEntry *pEntry);

/// Index of the first chunk with `id` at or after `start`, or -1.
int32_t bmPackFindChunk(const BmPack *pPack, uint32_t id, uint32_t start);

/// In-place data of an uncompressed chunk, or NULL (bad index, compressed chunk).
const uint8_t *bmPackChunkData(const BmPack *pPack, uint32_t index, uint32_t *pSize);

/// String `id` from the pack's string table (STRS), NUL-terminated, or NULL.
const char *bmPackString(const BmPack *pPack, uint16_t id);

/// Reads little-endian values from chunk data (no alignment needed).
static inline uint16_t bmRead16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t bmRead32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#ifdef BM_PLATFORM_HOST
// --- Writer (host tools) -----------------------------------------------------------------

typedef struct BmPackWriterChunk
{
  BmChunkEntry entry;
  uint8_t *pData;
} BmPackWriterChunk;

typedef struct BmPackWriter
{
  uint32_t profile;
  BmPackLayout layout;
  BmPackWriterChunk *pChunks;
  uint32_t chunkCount, chunkCapacity;
} BmPackWriter;

void bmPackWriterInit(BmPackWriter *pWriter, uint32_t profile, BmPackLayout layout);
void bmPackWriterFree(BmPackWriter *pWriter);

/// Adds a chunk (data is copied); returns its index in the table of contents.
uint32_t bmPackWriterAdd(BmPackWriter *pWriter, uint32_t id, BmChunkCategory category, uint16_t group,
                         const void *pData, uint32_t size);

/// Builds the pack. Returns a malloc'ed buffer (caller frees) and its size.
uint8_t *bmPackWriterFinish(const BmPackWriter *pWriter, uint32_t *pSize);
#endif
