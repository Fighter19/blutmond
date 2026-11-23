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

static const BmBodyInput NONE = {0, 0, false, false, false};

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
  run(&b, 80, (BmBodyInput){1, 0, false, false, false});
  TEST_ASSERT(BM_FIX_INT(b.x) + s_params.halfWidth == 128);  // touching the wall at x 128
  run(&b, 200, (BmBodyInput){-1, 0, false, false, false});
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
    bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){1, 0, false, false, false});
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
  run(&b, 30, (BmBodyInput){1, 0, false, false, false});
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
  bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, true, true, false});
  for (int i = 0; i < 60; i++)
  {
    bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, false, true, false});
    if (BM_FIX_INT(b.y) < top) top = BM_FIX_INT(b.y);
  }
  TEST_ASSERT(112 - top >= 48 && 112 - top <= 57);  // about 3.5 tiles

  const char *low[GH] = { "................", "................", "................", "................",
                          "..######........", "................", "................", "################" };
  build(low);
  bmBodyPlace(&b, 40, 112);
  run(&b, 2, NONE);
  uint32_t ev = bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, true, true, false});
  ev |= run(&b, 10, (BmBodyInput){0, 0, false, true, false});
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
  bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 0, true, true, false});
  run(&b, 60, (BmBodyInput){0, 0, false, true, false});
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 80);  // jumped up through it, landed on top

  // Down + jump drops through.
  bmBodyStep(&b, &s_params, &s_coll, &s_room, (BmBodyInput){0, 1, true, true, false});
  run(&b, 40, NONE);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 112);
}

// Whether a solid pixel (blocking sideways) lies inside the body's box.
static bool insideSolid(const BmBody *b)
{
  int32_t x = BM_FIX_INT(b->x), feet = BM_FIX_INT(b->y);
  for (int32_t y = feet - s_params.height; y < feet; y++)
    for (int32_t px = x - s_params.halfWidth; px < x + s_params.halfWidth; px++)
      if (bmCollisionSolidSides(&s_coll, px, y)) return true;
  return false;
}

// Jumping diagonally onto a block (and against its side): the body never ends up inside it, at
// any frame, from many starting places.
static void testDiagonalJumpsNeverSinkIntoABlock(void)
{
  static const char *map[GH] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "..........######",
    "..........######",
    "################",
  };
  build(map);
  int bad = 0;
  for (int start = 60; start < 150; start += 3)
  {
    for (int hold = 0; hold < 12; hold += 3)
    {
      BmBody b;
      bmBodyPlace(&b, start, 112);
      run(&b, 2, NONE);
      for (int f = 0; f < 70; f++)
      {
        BmBodyInput in = { 1, 0, f == hold, f >= hold && f < hold + 20, false };
        bmBodyStep(&b, &s_params, &s_coll, &s_room, in);
        if (insideSolid(&b)) { if (bad == 0) printf("    inside: start %d hold %d frame %d at %d,%d v %d,%d ground %d\n", start, hold, f, (int)BM_FIX_INT(b.x), (int)BM_FIX_INT(b.y), (int)b.vx, (int)b.vy, b.onGround); bad++; break; }
      }
    }
  }
  TEST_ASSERT(bad == 0);
}

// Gravity flipped: a platform (solid only on top) is stood on from below and passed going the
// other way; up (toward the feet) + jump drops through it, as down + jump does the usual way.
static void testPlatformsWorkWithGravityFlipped(void)
{
  static const char *map[GH] = {
    "################",
    "................",
    "................",
    "....======......",
    "................",
    "................",
    "................",
    "################",
  };
  build(map);
  BmBody b;
  bmBodyPlace(&b, 104, 112);
  b.flipped = true;
  run(&b, 60, NONE);
  // Under the platform: the feet (the box's top) at its underside, y 64.
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 64 + s_params.height);
  // A jump (down, away from it) through the empty space and back onto it.
  run(&b, 1, (BmBodyInput){0, 0, true, true, false});
  run(&b, 80, (BmBodyInput){0, 0, false, true, false});
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 64 + s_params.height);
  // Up + jump: through it, up to the ceiling (its underside at y 16).
  run(&b, 1, (BmBodyInput){0, -1, true, true, false});
  run(&b, 60, NONE);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 16 + s_params.height);
  // From the ceiling a jump goes down past the platform and comes back up onto its underside.
  run(&b, 1, (BmBodyInput){0, 0, true, true, false});
  run(&b, 80, (BmBodyInput){0, 0, false, true, false});
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 64 + s_params.height);
}

static void testLadderClimbAndStandOnTop(void)
{
  const char *rows[GH] = { "................", "................", "................", "...H............",
                           "...H............", "...H............", "...H............", "################" };
  build(rows);
  BmBody b;
  bmBodyPlace(&b, 56, 112);
  run(&b, 2, NONE);
  run(&b, 120, (BmBodyInput){0, -1, false, false, false});
  run(&b, 5, NONE);
  TEST_ASSERT(b.onGround && BM_FIX_INT(b.y) == 48);  // standing on the ladder's top cell
  run(&b, 120, (BmBodyInput){0, 1, false, false, false});
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
  uint32_t ev = run(&b, 40, (BmBodyInput){1, 0, false, false, false});
  TEST_ASSERT(ev & BM_BODY_DAMAGE);
}

// Wall jumps and the double jump (runtime-core.md §5): a wall at hand is jumped off whether the
// body rises or falls and whatever is held, before a jump in the air is spent; a wall does not
// give the jumps in the air back (only the ground does); a wall just left can still be jumped
// off for a few frames.
static void testWallJumpsAndTheDoubleJump(void)
{
  const char *rows[GH] = { "..........#.....", "..........#.....", "..........#.....", "..........#.....",
                           "..........#.....", "..........#.....", "################", "################" };
  build(rows);
  s_params.airJumps = 1;
  s_params.wallSlide = 1;
  BmBody b;
  // Standing against the wall (x 160), a jump: still rising beside it, not holding towards
  // it, a second press jumps off the wall, and the double jump is still there.
  bmBodyPlace(&b, 160 - s_params.halfWidth, 96);
  run(&b, 5, NONE);
  BmBodyInput jump = {0, 0, true, true, false};
  run(&b, 1, jump);
  BmBodyInput hold = {0, 0, false, true, false};
  run(&b, 4, hold);
  TEST_ASSERT(!b.onGround && b.vy < 0 && b.wallTouch == 1);
  uint32_t ev = run(&b, 1, jump);
  TEST_ASSERT((ev & BM_BODY_WALL_JUMPED) && !(ev & BM_BODY_AIR_JUMPED) && b.vx < 0 && b.airJumpsLeft == 1);
  // Away from the wall: the double jump; back at the wall it is not given back, but the wall
  // can still be jumped off.
  run(&b, 12, hold);
  ev = run(&b, 1, jump);
  TEST_ASSERT((ev & BM_BODY_AIR_JUMPED) && b.airJumpsLeft == 0);
  BmBodyInput right = {1, 0, false, true, false};
  for (int i = 0; i < 60 && b.wallTouch == 0; i++) run(&b, 1, right);
  run(&b, 1, right);
  TEST_ASSERT(b.wallTouch == 1 && b.airJumpsLeft == 0);
  // Just left (holding away for a few frames, out of reach): still a wall jump, not the double
  // jump; much later, the double jump.
  BmBodyInput left = {-1, 0, false, false, false};
  int away = 0;
  for (; away < 10 && b.wallTouch != 0; away++) run(&b, 1, left);
  TEST_ASSERT(b.wallTouch == 0 && b.wallCoyote != 0);
  BmBodyInput leftJump = {-1, 0, true, true, false};
  ev = run(&b, 1, leftJump);
  TEST_ASSERT((ev & BM_BODY_WALL_JUMPED) && b.airJumpsLeft == 0);
  // Much later, out of reach: no jump left in the air (spent before the wall); on the ground
  // it is back.
  run(&b, 20, left);
  ev = run(&b, 1, leftJump);
  TEST_ASSERT(!(ev & BM_BODY_JUMPED));
  run(&b, 120, NONE);
  TEST_ASSERT(b.onGround && b.airJumpsLeft == 1);
  // Not spent before the wall: still there after the wall jump.
  bmBodyPlace(&b, 160 - s_params.halfWidth, 96);
  run(&b, 5, NONE);
  run(&b, 1, jump);
  run(&b, 4, hold);
  run(&b, 1, jump);
  run(&b, 12, hold);
  ev = run(&b, 1, jump);
  TEST_ASSERT((ev & BM_BODY_AIR_JUMPED) && b.airJumpsLeft == 0);
  // Without the ability: no wall jumps, the wall gives nothing back.
  s_params.wallSlide = 0;
  bmBodyPlace(&b, 160 - s_params.halfWidth, 96);
  run(&b, 5, NONE);
  run(&b, 1, jump);
  run(&b, 4, hold);
  ev = run(&b, 1, jump);
  TEST_ASSERT(!(ev & BM_BODY_WALL_JUMPED) && (ev & BM_BODY_AIR_JUMPED) && b.wallTouch == 0);
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
  RUN_TEST(testDiagonalJumpsNeverSinkIntoABlock);
  RUN_TEST(testPlatformsWorkWithGravityFlipped);
  RUN_TEST(testLadderClimbAndStandOnTop);
  RUN_TEST(testWaterSlowsTheFall);
  RUN_TEST(testDamageMaterialReported);
  RUN_TEST(testWallJumpsAndTheDoubleJump);
  return TEST_RESULT();
}
