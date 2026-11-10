// Game tests on the cooked test level (path to the pack as the first argument).
#include "autopilot.h"
#include "device.h"
#include "game.h"
#include "hal_host.h"
#include "physical_device.h"
#include "video.h"

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_data;
static uint32_t s_size;
static BmPack s_pack;
static BmGame s_game;

static void start(void)
{
  bmHalHostReset();
  BmDeviceHandle device = NULL;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  bmPackOpen(&s_pack, s_data, s_size, true);
  bmGameStart(&s_game, &s_pack, NULL);
}

static void frames(int n, uint16_t keys)
{
  for (int i = 0; i < n; i++)
  {
    bmVideoWaitFrame();
    bmGameFrame(&s_game, keys);
  }
}

static void testAutopilotPlaysThroughTheLevel(void)
{
  start();
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0);
  BmAutopilot pilot = {0};
  int frame = 0;
  for (; frame < 1500 && strcmp(bmGameRoomId(&s_game), "test_room") == 0; frame++)
  {
    bmVideoWaitFrame();
    bmGameFrame(&s_game, bmAutopilotKeys(&pilot, &s_game));
  }
  printf("    reached %s after %d frames (deaths %u, hurts %u)\n", bmGameRoomId(&s_game), frame,
         (unsigned)s_game.deaths, (unsigned)s_game.hurts);
  // Both pits jumped, the ladder and the stone block climbed, the door used.
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  TEST_ASSERT(s_game.deaths == 0);
  TEST_ASSERT(s_game.roomChanges == 1);
}

static void testFallingIntoAPitRespawnsOnSafeGround(void)
{
  start();
  frames(30, BM_KEY_RIGHT);  // walk on the ground: the safe position follows
  int32_t safeX = s_game.player.safeX;
  // Put the player over the second pit (x 640..687, water at the bottom) and let it fall
  // to the kill zone below the room.
  bmBodyPlace(&s_game.player, 664, 120);
  s_game.player.safeX = safeX;
  s_game.player.safeY = 160;
  frames(120, 0);
  TEST_ASSERT(s_game.deaths == 1);
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) == safeX);
  TEST_ASSERT(s_game.player.onGround && BM_FIX_INT(s_game.player.y) == 160);
}

static void testDoorsLeadBothWays(void)
{
  start();
  // Stand next to the east door on the stone block and walk into it.
  bmBodyPlace(&s_game.player, 990, 80);
  frames(30, BM_KEY_RIGHT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  frames(40, 0);
  for (int i = 0; i < 200 && strcmp(bmGameRoomId(&s_game), "test_room_2") == 0; i++)
  {
    frames(1, BM_KEY_LEFT);  // back through the west door
  }
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0);
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) > 960);  // arrived at the east door
  TEST_ASSERT(s_game.roomChanges == 2);
}

static void testSavePointWritesASaveGame(void)
{
  start();
  memset(bmHalHostSaveMemory(), 0xFF, BM_HAL_SAVE_SIZE);
  BmGame fresh;
  // Nothing saved yet: no save game to continue from.
  fresh = s_game;
  TEST_ASSERT(!bmGameReadSaveGame(&fresh));
  // Walk onto the save point at x 400 (on the hill's plateau, y 144).
  bmBodyPlace(&s_game.player, 380, 144);
  frames(20, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.saves == 1);

  // A new game continues there.
  start();
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0);
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) >= 380 && BM_FIX_INT(s_game.player.x) <= 412);  // at the save point
  TEST_ASSERT(s_game.saves == 1);
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: test_game <pack>\n");
    return 2;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END);
  s_size = (uint32_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  s_data = malloc(s_size);
  if (fread(s_data, 1, s_size, f) != s_size) return 1;
  fclose(f);

  RUN_TEST(testAutopilotPlaysThroughTheLevel);
  RUN_TEST(testFallingIntoAPitRespawnsOnSafeGround);
  RUN_TEST(testDoorsLeadBothWays);
  RUN_TEST(testSavePointWritesASaveGame);
  free(s_data);
  return TEST_RESULT();
}
