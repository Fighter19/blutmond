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
#define BM_PACK_FORMAT_MINOR 2

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
  BM_CHUNK_SPRT = BM_FOURCC('S', 'P', 'R', 'T'),
  BM_CHUNK_MODB = BM_FOURCC('M', 'O', 'D', 'B'),
  BM_CHUNK_SONG = BM_FOURCC('S', 'O', 'N', 'G'),
  BM_CHUNK_MODN = BM_FOURCC('M', 'O', 'D', 'N'),   // native script module (src/script/native.h)
  BM_CHUNK_DTAB = BM_FOURCC('D', 'T', 'A', 'B'),   // records of one structure (src/data/data.h)
  BM_CHUNK_FONT = BM_FOURCC('F', 'O', 'N', 'T'),   // glyphs of a font (src/ui/ui.h)
  BM_CHUNK_LANG = BM_FOURCC('L', 'A', 'N', 'G'),   // translations into one language (src/ui/text.h)
  BM_CHUNK_WRLD = BM_FOURCC('W', 'R', 'L', 'D'),   // the world map (src/world/map.h)
  BM_CHUNK_MIGR = BM_FOURCC('M', 'I', 'G', 'R'),   // save layouts of earlier versions (src/world/state.h)
  BM_CHUNK_FIGR = BM_FOURCC('F', 'I', 'G', 'R'),   // a figure: sprites in layers (src/world/figure.h)
  BM_CHUNK_LITE = BM_FOURCC('L', 'I', 'T', 'E'),   // the light of a room (src/world/light.h)
  BM_CHUNK_CCOR = BM_FOURCC('C', 'C', 'O', 'R'),   // a colour correction for a screen (src/graphics/public/color.h)
};

/// Storage layout (header flags bits 0-3), docs/spec/pack-format.md §2.6.
typedef enum BmPackLayout {
  BM_PACK_LAYOUT_ROM = 0,
  BM_PACK_LAYOUT_ROOM_CHAIN = 1,
  BM_PACK_LAYOUT_SEQUENTIAL = 2,
} BmPackLayout;

/// Header flags: bit 4: an update, whose entries may lie in a base pack (pack sets,
/// pack-format.md §0).
#define BM_PACK_FLAG_UPDATE 0x10

/// Chunk entry flags, in the top bits of the compression byte (format minor 2): the chunk is
/// absent (not in this pack: an update brings it; offset and size 0), or it lies in the base
/// pack (offset and size are the base's).
#define BM_PACK_ENTRY_ABSENT 0x40
#define BM_PACK_ENTRY_IN_BASE 0x80
#define BM_PACK_ENTRY_FLAGS 0xC0

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
  uint8_t compression;     // the method (the entry flags taken out)
  uint8_t category;
  uint16_t group;
  uint32_t hash;
  uint8_t flags;           // BM_PACK_ENTRY_ABSENT, BM_PACK_ENTRY_IN_BASE (from the compression byte)
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
  // The string table, found once when the pack is opened (strings are looked up often).
  const uint8_t *pStrings;
  uint32_t stringsSize;
  // An update's base pack (pack sets): entries flagged in-base lie there. NULL: none.
  const uint8_t *pBase;
  uint32_t baseSize;
} BmPack;

/// CRC-32 (IEEE 802.3), as used by packs, chunks and the link protocol.
uint32_t bmCrc32(uint32_t crc, const void *pData, size_t size);

/// Checks the header and table of contents. With `verifyCrc` the whole pack is checksummed
/// (slow on the GBA; the cooker and the live link check it instead).
BmPackError bmPackOpen(BmPack *pPack, const void *pData, uint32_t size, bool verifyCrc);

/// The same for a pack set: an update (header flag BM_PACK_FLAG_UPDATE) over `pBase`, a pack
/// that is not an update itself; chunks flagged in-base are read from the base. Without a
/// base, or with an update as the base, an update is refused (BM_PACK_ERROR_TOC); a pack that
/// is not an update ignores the base. The CRC covers the update's own bytes.
BmPackError bmPackOpenSet(BmPack *pPack, const void *pData, uint32_t size, bool verifyCrc, const BmPack *pBase);

/// The bytes a chunk is stored as (compressed or not), wherever it lies; NULL if it is absent
/// (or the index is bad).
const uint8_t *bmPackChunkStored(const BmPack *pPack, uint32_t index, uint32_t *pSize);

uint32_t bmPackChunkCount(const BmPack *pPack);
bool bmPackGetChunk(const BmPack *pPack, uint32_t index, BmChunkEntry *pEntry);

/// Index of the first chunk with `id` at or after `start`, or -1.
int32_t bmPackFindChunk(const BmPack *pPack, uint32_t id, uint32_t start);

/// In-place data of an uncompressed chunk, or NULL (bad index, compressed chunk, absent).
const uint8_t *bmPackChunkData(const BmPack *pPack, uint32_t index, uint32_t *pSize);

/// A chunk's data, compressed or not: an uncompressed one in place, an LZ77 one unpacked into
/// `pOut` (at most `outSize` bytes). NULL if it does not fit, is damaged or of another method.
const uint8_t *bmPackChunkUnpack(const BmPack *pPack, uint32_t index, uint8_t *pOut, uint32_t outSize, uint32_t *pSize);

/// Unpacks LZ77 in the GBA BIOS format (section 4 of pack-format.md): a u32 of 0x10 and the
/// unpacked size << 8, then groups of a flag byte (high bit first) and eight items: a byte,
/// or for a set flag two bytes, length - 3 in the top 4 bits, distance - 1 in the other 12.
/// Returns the unpacked size, 0 if the data is damaged or larger than `outSize`.
uint32_t bmLz77Unpack(const uint8_t *pIn, uint32_t inSize, uint8_t *pOut, uint32_t outSize);

/// The most a room's compressed maps and collision take unpacked, together (each rounded up
/// to 4 bytes): the runtime has a buffer of this size for them (room.h). The cooker leaves
/// the chunks of a larger room uncompressed; those are read in place.
#define BM_PACK_ROOM_UNPACK_BYTES (24u * 1024u)

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
  BmChunkEntry entry;      // entry.flags: absent (no data), in base (entry.offset is the base's)
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

/// Adds an entry without data: `flags` BM_PACK_ENTRY_ABSENT (the chunk comes with an update;
/// `pEntry`'s id, category, group, hash and sizes are kept, offset and size written as 0) or
/// BM_PACK_ENTRY_IN_BASE (it lies in the base pack at `pEntry`'s offset and size; the pack
/// becomes an update). Returns its index.
uint32_t bmPackWriterAddEntry(BmPackWriter *pWriter, const BmChunkEntry *pEntry, uint8_t flags);

/// LZ77-compresses chunk `index` (data, size, hash: of the compressed bytes) if that makes
/// it smaller; returns whether it did.
bool bmPackWriterCompress(BmPackWriter *pWriter, uint32_t index);

/// LZ77 in the BIOS format (bmLz77Unpack); `pOut` takes at least 4 + size + size / 8 + 1
/// bytes. Returns the compressed size.
uint32_t bmLz77Pack(const uint8_t *pIn, uint32_t size, uint8_t *pOut);

/// Builds the pack. Returns a malloc'ed buffer (caller frees) and its size.
uint8_t *bmPackWriterFinish(const BmPackWriter *pWriter, uint32_t *pSize);

/// An update of the full pack `pFull` over a base that holds the chunks `pBaseChunks` lists
/// (`count` triples of hash, size, offset in the base, as the live link's HELLO reports
/// them): the full pack's table, every chunk the base holds (same hash and size) flagged
/// in-base at its offset there, the others' data in the update. Returns a malloc'ed pack
/// (caller frees) and its size, or NULL if `pFull` is an update itself.
uint8_t *bmPackMakeUpdate(const BmPack *pFull, const uint32_t *pBaseChunks, uint32_t count, uint32_t *pSize);
#endif
