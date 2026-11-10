#include "state.h"

#include "test.h"

#include <string.h>

typedef struct Sample
{
  int32_t x;
  uint16_t hp;
  bool alive;
  int8_t facing;
  uint8_t timer;
  uint32_t list[3];
  uint32_t cache;
} Sample;

static const BmStateField s_fields[] = {
  BM_STATE_FIELD(Sample, x, BM_TYPE_FIX, BM_STATE_SAVE),
  BM_STATE_FIELD(Sample, hp, BM_TYPE_U16, BM_STATE_LINK),
  BM_STATE_FIELD(Sample, alive, BM_TYPE_BOOL, 0),
  BM_STATE_FIELD(Sample, facing, BM_TYPE_S8, BM_STATE_SNAPSHOT),
  BM_STATE_FIELD(Sample, timer, BM_TYPE_U8, BM_STATE_TRANSIENT),
  BM_STATE_ARRAY(Sample, list, BM_TYPE_U32, BM_STATE_SAVE, 3),
  BM_STATE_FIELD(Sample, cache, BM_TYPE_U32, BM_STATE_DERIVED),
};

static BmStateBlock block(Sample *s)
{
  return (BmStateBlock){ "sample", s_fields, sizeof(s_fields) / sizeof(s_fields[0]), s };
}

static void testSnapshotRoundTrip(void)
{
  Sample a = { -12345, 77, true, -1, 9, {1, 2, 3}, 42 };
  BmStateBlock ba = block(&a);
  uint8_t buf[128];
  uint32_t n = bmStateWrite(&ba, 1, BM_STATE_SNAPSHOT, buf, sizeof(buf));
  // header + x(4) + hp(2) + alive(1) + facing(1) + list(12); timer and cache are not written
  TEST_ASSERT(n == BM_STATE_HEADER_SIZE + 4 + 2 + 1 + 1 + 12);
  Sample b;
  memset(&b, 0, sizeof(b));
  b.timer = 5;
  b.cache = 99;
  BmStateBlock bb = block(&b);
  TEST_ASSERT(bmStateRead(&bb, 1, buf, n) == BM_STATE_OK);
  TEST_ASSERT(b.x == -12345 && b.hp == 77 && b.alive && b.facing == -1);
  TEST_ASSERT(b.list[0] == 1 && b.list[2] == 3);
  TEST_ASSERT(b.timer == 5 && b.cache == 99);  // transient and derived untouched
}

static void testSaveGameHoldsOnlySaveFields(void)
{
  Sample a = { 1000, 5, true, 1, 0, {7, 8, 9}, 0 };
  BmStateBlock ba = block(&a);
  uint8_t buf[128];
  uint32_t n = bmStateWrite(&ba, 1, BM_STATE_SAVE, buf, sizeof(buf));
  TEST_ASSERT(n == BM_STATE_HEADER_SIZE + 4 + 12);
  Sample b = { 0, 33, false, 0, 0, {0, 0, 0}, 0 };
  BmStateBlock bb = block(&b);
  TEST_ASSERT(bmStateRead(&bb, 1, buf, n) == BM_STATE_OK);
  TEST_ASSERT(b.x == 1000 && b.list[1] == 8);
  TEST_ASSERT(b.hp == 33);  // not a save field
}

static void testRejectsOtherSchemasAndDamage(void)
{
  Sample a = { 1, 2, true, 1, 0, {0, 0, 0}, 0 };
  BmStateBlock ba = block(&a);
  uint8_t buf[128];
  uint32_t n = bmStateWrite(&ba, 1, BM_STATE_SNAPSHOT, buf, sizeof(buf));

  // A different layout: one field's flags changed.
  BmStateField other[sizeof(s_fields) / sizeof(s_fields[0])];
  memcpy(other, s_fields, sizeof(other));
  other[1].flags = BM_STATE_SAVE;
  BmStateBlock bo = { "sample", other, sizeof(other) / sizeof(other[0]), &a };
  TEST_ASSERT(bmStateSchemaHash(&bo, 1) != bmStateSchemaHash(&ba, 1));
  TEST_ASSERT(bmStateRead(&bo, 1, buf, n) == BM_STATE_OTHER_SCHEMA);

  buf[n - 1] ^= 1;
  TEST_ASSERT(bmStateRead(&ba, 1, buf, n) == BM_STATE_BAD_CRC);
  buf[0] = 'X';
  TEST_ASSERT(bmStateRead(&ba, 1, buf, n) == BM_STATE_BAD_HEADER);
  TEST_ASSERT(bmStateWrite(&ba, 1, BM_STATE_SNAPSHOT, buf, 10) == 0);  // too small
}

int main(void)
{
  RUN_TEST(testSnapshotRoundTrip);
  RUN_TEST(testSaveGameHoldsOnlySaveFields);
  RUN_TEST(testRejectsOtherSchemasAndDamage);
  return TEST_RESULT();
}
