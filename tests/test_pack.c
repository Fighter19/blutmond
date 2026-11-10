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

int main(void)
{
  RUN_TEST(testCrc32KnownValue);
  RUN_TEST(testWriteAndRead);
  RUN_TEST(testRejectsDamagedPacks);
  return TEST_RESULT();
}
