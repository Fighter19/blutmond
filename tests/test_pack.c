#include "pack.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

static void testCrc32KnownValue(void)
{
  TEST_ASSERT(bmCrc32(0, "123456789", 9) == 0xCBF43926u);
  // Incremental use gives the same result.
  TEST_ASSERT(bmCrc32(bmCrc32(0, "1234", 4), "56789", 5) == 0xCBF43926u);
}

static void testWriteAndRead(void)
{
  BmPackWriter w;
  bmPackWriterInit(&w, BM_FOURCC('G', 'B', 'A', ' '), BM_PACK_LAYOUT_ROM);
  const char room[] = "room data";
  const uint8_t tiles[6] = { 1, 2, 3, 4, 5, 6 };
  TEST_ASSERT(bmPackWriterAdd(&w, BM_CHUNK_ROOM, BM_CATEGORY_LEVEL, 256, room, sizeof(room)) == 0);
  TEST_ASSERT(bmPackWriterAdd(&w, BM_CHUNK_TILE, BM_CATEGORY_GRAPHICS, 1, tiles, sizeof(tiles)) == 1);
  TEST_ASSERT(bmPackWriterAdd(&w, BM_CHUNK_TILE, BM_CATEGORY_GRAPHICS, 1, tiles, 2) == 2);
  uint32_t size = 0;
  uint8_t *data = bmPackWriterFinish(&w, &size);
  bmPackWriterFree(&w);

  TEST_ASSERT(size % 4 == 0);
  BmPack pack;
  TEST_ASSERT(bmPackOpen(&pack, data, size, true) == BM_PACK_OK);
  TEST_ASSERT(bmPackChunkCount(&pack) == 3);
  TEST_ASSERT(pack.header.profile == BM_FOURCC('G', 'B', 'A', ' '));
  TEST_ASSERT(pack.header.tocOffset == BM_PACK_HEADER_SIZE);

  BmChunkEntry e;
  TEST_ASSERT(bmPackGetChunk(&pack, 1, &e));
  TEST_ASSERT(e.id == BM_CHUNK_TILE && e.size == 6 && e.group == 1 && e.category == BM_CATEGORY_GRAPHICS);
  TEST_ASSERT(e.offset % 4 == 0);
  TEST_ASSERT(e.hash == bmCrc32(0, tiles, 6));

  uint32_t chunkSize = 0;
  const uint8_t *p = bmPackChunkData(&pack, 0, &chunkSize);
  TEST_ASSERT(p != NULL && chunkSize == sizeof(room) && memcmp(p, room, sizeof(room)) == 0);

  TEST_ASSERT(bmPackFindChunk(&pack, BM_CHUNK_TILE, 0) == 1);
  TEST_ASSERT(bmPackFindChunk(&pack, BM_CHUNK_TILE, 2) == 2);
  TEST_ASSERT(bmPackFindChunk(&pack, BM_CHUNK_COLL, 0) == -1);
  TEST_ASSERT(!bmPackGetChunk(&pack, 3, &e));
  free(data);
}

static void testRejectsDamagedPacks(void)
{
  BmPackWriter w;
  bmPackWriterInit(&w, BM_FOURCC('G', 'B', 'A', ' '), BM_PACK_LAYOUT_ROM);
  bmPackWriterAdd(&w, BM_CHUNK_ROOM, BM_CATEGORY_LEVEL, 256, "abcd", 4);
  uint32_t size = 0;
  uint8_t *data = bmPackWriterFinish(&w, &size);
  bmPackWriterFree(&w);

  BmPack pack;
  TEST_ASSERT(bmPackOpen(&pack, data, 10, false) == BM_PACK_ERROR_TOO_SMALL);
  TEST_ASSERT(bmPackOpen(&pack, data, size - 4, false) == BM_PACK_ERROR_SIZE);

  data[size - 1] ^= 0xFF;  // damage the chunk data
  TEST_ASSERT(bmPackOpen(&pack, data, size, false) == BM_PACK_OK);
  TEST_ASSERT(bmPackOpen(&pack, data, size, true) == BM_PACK_ERROR_CRC);
  data[size - 1] ^= 0xFF;

  data[BM_PACK_HEADER_SIZE + 4] = 0xFF;  // chunk offset far outside the pack
  data[BM_PACK_HEADER_SIZE + 7] = 0x7F;
  TEST_ASSERT(bmPackOpen(&pack, data, size, false) == BM_PACK_ERROR_TOC);

  data[4] = 9;  // unknown major version
  TEST_ASSERT(bmPackOpen(&pack, data, size, false) == BM_PACK_ERROR_VERSION);
  data[0] = 'X';
  TEST_ASSERT(bmPackOpen(&pack, data, size, false) == BM_PACK_ERROR_MAGIC);
  free(data);
}

static void testLz77(void)
{
  // Runs (overlapping copies), repeats far back, noise, and sizes around the group of 8.
  static uint8_t in[9000], packed[9000 + 9000 / 8 + 8], out[9000];
  uint32_t seed = 1;
  for (uint32_t i = 0; i < sizeof(in); i++)
  {
    seed = seed * 1664525u + 1013904223u;
    in[i] = i < 3000 ? (uint8_t)(i / 700) : i < 6000 ? in[i - 2900] : (uint8_t)(seed >> 24);
  }
  const uint32_t sizes[] = { 0, 1, 2, 3, 8, 9, 17, 1000, 6000, 9000 };
  for (uint32_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
  {
    uint32_t n = bmLz77Pack(in, sizes[k], packed);
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT(bmLz77Unpack(packed, n, out, sizeof(out)) == sizes[k]);
    TEST_ASSERT(memcmp(in, out, sizes[k]) == 0);
  }
  uint32_t n = bmLz77Pack(in, 3000, packed);
  TEST_ASSERT(n < 3000 / 6);                                    // the runs compress well (18 bytes a match at most)
  TEST_ASSERT(bmLz77Unpack(packed, n, out, 2999) == 0);          // larger than the room for it
  TEST_ASSERT(bmLz77Unpack(packed, n - 1, out, sizeof(out)) == 0);   // cut short
  packed[0] = 0x11;
  TEST_ASSERT(bmLz77Unpack(packed, n, out, sizeof(out)) == 0);   // another method
}

static void testCompressedChunk(void)
{
  static uint8_t map[4000];
  for (uint32_t i = 0; i < sizeof(map); i++) map[i] = (uint8_t)(i % 64 < 8 ? i : 0);
  BmPackWriter w;
  bmPackWriterInit(&w, BM_FOURCC('G', 'B', 'A', ' '), BM_PACK_LAYOUT_ROM);
  bmPackWriterAdd(&w, BM_CHUNK_TMAP, BM_CATEGORY_LEVEL, 256, map, sizeof(map));
  bmPackWriterAdd(&w, BM_CHUNK_COLL, BM_CATEGORY_LEVEL, 256, "ab", 2);
  TEST_ASSERT(bmPackWriterCompress(&w, 0));
  TEST_ASSERT(!bmPackWriterCompress(&w, 1));                     // no smaller: stays as it is
  uint32_t size = 0;
  uint8_t *data = bmPackWriterFinish(&w, &size);
  bmPackWriterFree(&w);

  BmPack pack;
  TEST_ASSERT(bmPackOpen(&pack, data, size, true) == BM_PACK_OK);
  BmChunkEntry e;
  TEST_ASSERT(bmPackGetChunk(&pack, 0, &e));
  TEST_ASSERT(e.compression == BM_PACK_COMPRESSION_LZ77 && e.unpackedSize == sizeof(map) && e.size < sizeof(map) / 4);
  TEST_ASSERT(e.hash == bmCrc32(0, pack.pData + e.offset, e.size));
  TEST_ASSERT(bmPackChunkData(&pack, 0, NULL) == NULL);          // not readable in place

  static uint8_t buffer[sizeof(map)];
  uint32_t unpacked = 0;
  TEST_ASSERT(bmPackChunkUnpack(&pack, 0, buffer, sizeof(buffer), &unpacked) == buffer);
  TEST_ASSERT(unpacked == sizeof(map) && memcmp(buffer, map, sizeof(map)) == 0);
  TEST_ASSERT(bmPackChunkUnpack(&pack, 0, buffer, sizeof(buffer) - 1, &unpacked) == NULL);
  const uint8_t *p = bmPackChunkUnpack(&pack, 1, buffer, sizeof(buffer), &unpacked);
  TEST_ASSERT(p != buffer && unpacked == 2 && memcmp(p, "ab", 2) == 0);   // in place
  free(data);
}

// Pack sets (pack-format.md §0): a base with a chunk absent, an update over it holding that
// chunk and pointing into the base for the others; chunk indices stay what they are.
static void testPackSets(void)
{
  static const char a[] = "first chunk, in both", b[] = "the second, only in the update", st[] = "strings";
  BmPackWriter w;
  bmPackWriterInit(&w, BM_FOURCC('G', 'B', 'A', ' '), BM_PACK_LAYOUT_ROM);
  bmPackWriterAdd(&w, BM_CHUNK_ROOM, BM_CATEGORY_LEVEL, 256, a, sizeof(a));
  bmPackWriterAdd(&w, BM_CHUNK_LANG, BM_CATEGORY_TEXT, 0, b, sizeof(b));
  bmPackWriterAdd(&w, BM_CHUNK_SONG, BM_CATEGORY_MUSIC, 0, st, sizeof(st));
  uint32_t fullSize = 0;
  uint8_t *full = bmPackWriterFinish(&w, &fullSize);
  // The base: the second chunk absent (its entry kept: the same index for it).
  BmPackWriterChunk second = w.pChunks[1];
  w.pChunks[1].entry.flags = BM_PACK_ENTRY_ABSENT;
  uint32_t baseSize = 0;
  uint8_t *base = bmPackWriterFinish(&w, &baseSize);
  bmPackWriterFree(&w);
  BmPack pf, pb, pu;
  TEST_ASSERT(bmPackOpen(&pf, full, fullSize, true) == BM_PACK_OK && bmPackOpen(&pb, base, baseSize, true) == BM_PACK_OK);
  TEST_ASSERT(baseSize < fullSize && bmPackChunkCount(&pb) == 3);
  TEST_ASSERT(bmPackFindChunk(&pb, BM_CHUNK_LANG, 0) == 1 && bmPackChunkData(&pb, 1, NULL) == NULL && bmPackChunkStored(&pb, 1, NULL) == NULL);
  uint32_t n = 0;
  TEST_ASSERT(memcmp(bmPackChunkData(&pb, 0, &n), a, sizeof(a)) == 0 && n == sizeof(a));
  // The base's chunks as the live link lists them: hash, size, offset (not the absent one).
  uint32_t listed[6], count = 0;
  for (uint32_t i = 0; i < 3; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&pb, i, &e);
    if (e.flags & BM_PACK_ENTRY_ABSENT) continue;
    listed[3 * count] = e.hash, listed[3 * count + 1] = e.size, listed[3 * count + 2] = e.offset;
    count++;
  }
  uint32_t updateSize = 0;
  uint8_t *update = bmPackMakeUpdate(&pf, listed, count, &updateSize);
  TEST_ASSERT(update != NULL && updateSize < fullSize);
  // Only with its base.
  TEST_ASSERT(bmPackOpen(&pu, update, updateSize, true) == BM_PACK_ERROR_TOC);
  TEST_ASSERT(bmPackOpenSet(&pu, update, updateSize, true, &pb) == BM_PACK_OK && (pu.header.flags & BM_PACK_FLAG_UPDATE));
  const uint8_t *pa = bmPackChunkData(&pu, 0, &n);
  TEST_ASSERT(pa >= base && pa < base + baseSize && memcmp(pa, a, sizeof(a)) == 0);   // read in the base
  const uint8_t *pbb = bmPackChunkData(&pu, 1, &n);
  TEST_ASSERT(pbb >= update && pbb < update + updateSize && n == sizeof(b) && memcmp(pbb, b, sizeof(b)) == 0);
  BmChunkEntry e;
  TEST_ASSERT(bmPackGetChunk(&pu, 1, &e) && e.hash == second.entry.hash && e.flags == 0);
  // An update is no base.
  BmPack pu2;
  TEST_ASSERT(bmPackOpenSet(&pu2, update, updateSize, true, &pu) == BM_PACK_ERROR_TOC);
  free(full);
  free(base);
  free(update);
}

int main(void)
{
  RUN_TEST(testCrc32KnownValue);
  RUN_TEST(testWriteAndRead);
  RUN_TEST(testRejectsDamagedPacks);
  RUN_TEST(testLz77);
  RUN_TEST(testCompressedChunk);
  RUN_TEST(testPackSets);
  return TEST_RESULT();
}
