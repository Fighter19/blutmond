#include "body.h"
#include "collision.h"

#include "test.h"

#include <limits.h>
#include <string.h>

// A 16x8 cell grid built from a picture: '#' solid, '=' jump-through, '/' slope up, '\' slope
// down, 'H' ladder (top cell jump-through), '~' water, '^' spikes, '_' half block (bottom).
#define GW 16
#define GH 8
static uint8_t s_cells[8 + GW * GH * 2];
static BmCollision s_coll;
static BmRoomPhysics s_room;
static BmBodyParams s_params;

static void build(const char *rows[GH])
{
  memset(s_cells, 0, sizeof(s_cells));
  s_cells[0] = GW;
  s_cells[2] = GH;
  s_cells[4] = 16;
  for (int y = 0; y < GH; y++)
  {
    for (int x = 0; x < GW; x++)
    {
      uint8_t *c = &s_cells[8 + (y * GW + x) * 2];
      switch (rows[y][x])
      {
      case '#': c[0] = BM_SHAPE_FULL | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES; break;
      case '=': c[0] = BM_SHAPE_FULL | BM_CELL_SOLID_TOP; break;
      case '/': c[0] = BM_SHAPE_SLOPE_1_1 | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES; break;
      case '\\': c[0] = BM_SHAPE_SLOPE_1_1 | BM_CELL_HFLIP | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES; break;
      case '_': c[0] = BM_SHAPE_HALF_BOTTOM | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES; break;
      case 'H': c[1] = BM_MATERIAL_LADDER; if (y == 0 || rows[y - 1][x] != 'H') c[0] = BM_SHAPE_FULL | BM_CELL_SOLID_TOP; break;
      case '~': c[1] = BM_MATERIAL_WATER; break;
      case '^': c[1] = BM_MATERIAL_DAMAGE; break;
      default: break;
      }
    }
  }
  bmCollisionInit(&s_coll, s_cells, sizeof(s_cells), true);
  s_room = (BmRoomPhysics){ BM_FIX(0.25), BM_FIX(4), BM_FIX(0.08), BM_FIX(1.5) };
  bmBodyDefaultParams(&s_params);
}

static uint32_t run(BmBody *b, int frames, BmBodyInput in)
{
  uint32_t events = 0;
  for (int i = 0; i < frames; i++)
  {
    events |= bmBodyStep(b, &s_params, &s_coll, &s_room, in);
    in.jumpPressed = false;
  }
  return events;
}

static const BmBodyInput NONE = {0, 0, false, false};

static void testShapeHeightsAndFlips(void)
{
  TEST_ASSERT(bmShapeHeight(BM_SHAPE_SLOPE_1_1, 0) == 1 && bmShapeHeight(BM_SHAPE_SLOPE_1_1, 15) == 16);
  TEST_ASSERT(bmShapeHeight(BM_SHAPE_SLOPE_1_1 | BM_CELL_HFLIP, 0) == 16);
  TEST_ASSERT(bmShapeHeight(BM_SHAPE_HALF_LEFT, 7) == 16 && bmShapeHeight(BM_SHAPE_HALF_LEFT, 8) == 0);
}

static void testFallsAndLandsOnFloor(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "................", "................", "################", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 20);
  uint32_t ev = run(&b, 60, NONE);
  TEST_ASSERT(ev & BM_BODY_LANDED);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 96);
}

static void testWallStopsAndRoomEdgesAreSolid(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "........#.......", "........#.......", "################", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 96);
  run(&b, 80, (BmBodyInput){1, 0, false, false});
  TEST_ASSERT(BM_FIX_INT(b.x) + s_params.halfWidth == 128);  // touching the wall at x 128
  run(&b, 200, (BmBodyInput){-1, 0, false, false});
  TEST_ASSERT(BM_FIX_INT(b.x) - s_params.halfWidth == 0);    // left room edge
}

static void testWalksUpAndDownSlopesWithoutLeavingTheGround(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "................", "....../\\........", "...../##\\.......", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 112);
  int airborne = 0, maxUp = 0;
  for (int i = 0; i < 120; i++)
  {
    bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){1, 0, false, false});
    airborne += b.onGround ? 0 : 1;
    if (112 - BM_FIX_INT(b.y) > maxUp) maxUp = 112 - BM_FIX_INT(b.y);
  }
  TEST_ASSERT(airborne == 0);        // slope snapping keeps the feet on the ground
  TEST_ASSERT(maxUp == 32);          // reached the top of the hill (two cells up)
  TEST_ASSERT(BM_FIX_INT(b.y) == 112);  // and came down the other side
}

static void testStepsUpHalfBlocks(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "................", "................", "......__........", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 112);
  run(&b, 30, (BmBodyInput){1, 0, false, false});
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 104);
}

static void testJumpHeightAndCeiling(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "................", "................", "................", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 112);
  run(&b, 2, NONE);
  int top = 112;
  bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, true, true});
  for (int i = 0; i < 60; i++)
  {
    bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, false, true});
    if (BM_FIX_INT(b.y) < top) top = BM_FIX_INT(b.y);
  }
  TEST_ASSERT(112 - top >= 48 && 112 - top <= 57);  // about 3.5 tiles

  const char *low[GH] = { "................", "................", "................", "................",
                          "..######........", "................", "................", "################" };
  build(low);
  bmBodyPlace(&b, 40, 112);
  run(&b, 2, NONE);
  uint32_t ev = bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, true, true});
  ev |= run(&b, 10, (BmBodyInput){0, 0, false, true});
  TEST_ASSERT(ev & BM_BODY_HIT_CEILING);
  TEST_ASSERT(BM_FIX_INT(b.y) - s_params.height >= 80);  // head stayed below the block
}

static void testJumpThroughPlatforms(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "................", "..======........", "................", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 50, 112);
  run(&b, 2, NONE);
  bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, true, true});
  run(&b, 60, (BmBodyInput){0, 0, false, true});
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 80);  // jumped up through it, landed on top

  // Down + jump drops through.
  bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 1, true, true});
  run(&b, 40, NONE);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 112);
}

static void testLadderClimbAndStandOnTop(void)
{
  const char *rows[GH] = { "................", "................", "................", "...H............",
                           "...H............", "...H............", "...H............", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 56, 112);
  run(&b, 2, NONE);
  run(&b, 120, (BmBodyInput){0, -1, false, false});
  run(&b, 5, NONE);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 48);  // standing on the ladder's top cell
  run(&b, 120, (BmBodyInput){0, 1, false, false});
  run(&b, 5, NONE);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 112);  // climbed back down through it
}

static void testWaterSlowsTheFall(void)
{
  const char *rows[GH] = { "................", "................", "~~~~~~~~~~~~~~~~", "~~~~~~~~~~~~~~~~",
                           "~~~~~~~~~~~~~~~~", "~~~~~~~~~~~~~~~~", "~~~~~~~~~~~~~~~~", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 70);
  run(&b, 60, NONE);
  TEST_ASSERT(b.inWater);
  TEST_ASSERT(b.vy <= BM_FIX(1.5));
}

static void testDamageMaterialReported(void)
{
  const char *rows[GH] = { "................", "................", "................", "................",
                           "................", "................", "......^^........", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 40, 112);
  uint32_t ev = run(&b, 40, (BmBodyInput){1, 0, false, false});
  TEST_ASSERT(ev & BM_BODY_DAMAGE);
}

int main(void)
{
  RUN_TEST(testShapeHeightsAndFlips);
  RUN_TEST(testFallsAndLandsOnFloor);
  RUN_TEST(testWallStopsAndRoomEdgesAreSolid);
  RUN_TEST(testWalksUpAndDownSlopesWithoutLeavingTheGround);
  RUN_TEST(testStepsUpHalfBlocks);
  RUN_TEST(testJumpHeightAndCeiling);
  RUN_TEST(testJumpThroughPlatforms);
  RUN_TEST(testLadderClimbAndStandOnTop);
  RUN_TEST(testWaterSlowsTheFall);
  RUN_TEST(testDamageMaterialReported);
  return TEST_RESULT();
}
