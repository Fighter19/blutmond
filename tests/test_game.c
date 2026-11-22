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

static uint8_t *s_data, *s_windData;
static uint32_t s_size, s_windSize;
static BmPack s_pack;
static BmGame s_game;

static void startWith(uint8_t *data, uint32_t size, const char *room)
{
  bmHalHostReset();
  BmDeviceHandle device = NULL;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  bmPackOpen(&s_pack, data, size, true);
  bmGameStart(&s_game, &s_pack, room);
}

static void start(void)
{
  startWith(s_data, s_size, NULL);
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

// --- Scripted objects (assets/test_room/scripts) ---------------------------------------------

static int actorOfClass(const char *cls)
{
  for (uint8_t i = 0; i < s_game.actors.count; i++)
    if (strcmp(s_game.objects[s_game.actors.object[i]].className, cls) == 0) return i;
  return -1;
}

static const char *stateOf(int actor)
{
  const BmActors *a = &s_game.actors;
  if (a->state[actor] == 0xFF) return "";
  uint16_t cls = 0;
  const BmVmModule *m = bmActorsModule(a, actor, &cls);
  return m != NULL ? bmPackString(&s_pack, bmVmStateName(m, cls, a->state[actor])) : NULL;
}

static void testTheBatChasesHitsAndGoesHome(void)
{
  bmHalHostReset();
  BmDeviceHandle device = NULL;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  bmPackOpen(&s_pack, s_data, s_size, true);
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "test_room_2") == BM_SUCCESS);
  TEST_ASSERT(s_game.actors.hasModule);
  int bat = actorOfClass("Bat");
  TEST_ASSERT(bat >= 0);
  TEST_ASSERT(s_game.actors.sheet[bat] >= 0);
  TEST_ASSERT(strcmp(stateOf(bat), "Sleeping") == 0);

  // Far away it sleeps.
  bmBodyPlace(&s_game.player, 40, 160);
  frames(60, 0);
  TEST_ASSERT(strcmp(stateOf(bat), "Sleeping") == 0);
  TEST_ASSERT(s_game.actors.x[bat] == s_game.actors.homeX[bat]);

  // The player jumps up below it: it wakes up and comes closer every frame.
  bmBodyPlace(&s_game.player, 200, 140);
  frames(2, 0);
  TEST_ASSERT(strcmp(stateOf(bat), "Chasing") == 0);
  int32_t y0 = s_game.actors.y[bat];
  frames(20, 0);
  TEST_ASSERT(s_game.actors.y[bat] > y0);

  // A save state in the middle of the chase continues exactly the same.
  static uint8_t snap[2048];
  BmStateBlock block = bmGameStateBlock(&s_game);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  printf("    snapshot with scripted objects: %u bytes\n", (unsigned)size);
  frames(40, 0);
  int32_t x1 = s_game.actors.x[bat], y1 = s_game.actors.y[bat];
  uint8_t state1 = s_game.actors.state[bat];
  uint32_t hurts1 = s_game.hurts;
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK);
  TEST_ASSERT(bmGameAfterLoad(&s_game) == BM_SUCCESS);
  frames(40, 0);
  TEST_ASSERT(s_game.actors.x[bat] == x1 && s_game.actors.y[bat] == y1 && s_game.actors.state[bat] == state1 && s_game.hurts == hurts1);

  // It reaches the player, hurts them once and flies home to rest.
  uint32_t hurtsBefore = s_game.hurts;
  int frame = 0;
  for (; frame < 300 && s_game.hurts == hurtsBefore; frame++) frames(1, 0);
  TEST_ASSERT(s_game.hurts == hurtsBefore + 1);
  TEST_ASSERT(strcmp(stateOf(bat), "Returning") == 0);
  bmBodyPlace(&s_game.player, 40, 160);
  for (frame = 0; frame < 400 && strcmp(stateOf(bat), "Resting") != 0; frame++) frames(1, 0);
  TEST_ASSERT(strcmp(stateOf(bat), "Resting") == 0);
  TEST_ASSERT(s_game.actors.x[bat] == s_game.actors.homeX[bat] && s_game.actors.y[bat] == s_game.actors.homeY[bat]);
  frames(95, 0);
  TEST_ASSERT(strcmp(stateOf(bat), "Sleeping") == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

static void testAPickupStaysTaken(void)
{
  start();
  int pickup = actorOfClass("Pickup");
  TEST_ASSERT(pickup >= 0);
  TEST_ASSERT(!s_game.actors.hidden[pickup] && s_game.inventory.count == 0);
  // Onto the pickup: taken, hidden, the item is the player's.
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[pickup]), BM_FIX_INT(s_game.actors.y[pickup]));
  frames(2, 0);
  TEST_ASSERT(s_game.actors.hidden[pickup]);
  TEST_ASSERT(s_game.inventory.count == 1);
  TEST_ASSERT(s_game.actors.storeCount == 1);

  // In the save game: a new game that loads it finds the pickup gone and the item kept.
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  bmGameStart(&s_game, &s_pack, NULL);
  pickup = actorOfClass("Pickup");
  TEST_ASSERT(!s_game.actors.hidden[pickup] && s_game.inventory.count == 0);
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  pickup = actorOfClass("Pickup");
  TEST_ASSERT(s_game.actors.hidden[pickup]);
  TEST_ASSERT(s_game.inventory.count == 1);
  TEST_ASSERT(bm_script_errors == 0);
}

static uint16_t stringId(const char *text)
{
  for (uint32_t id = 0; id < 0xFFFF; id++)
  {
    const char *s = bmPackString(&s_pack, (uint16_t)id);
    if (s == NULL) break;
    if (strcmp(s, text) == 0) return (uint16_t)id;
  }
  return 0xFFFF;
}

// How high a jump from standing goes (fix): settle, press A, hold it, land again.
static int32_t jumpHeight(void)
{
  frames(40, 0);
  int32_t ground = s_game.player.y, top = ground;
  frames(1, BM_KEY_A);
  for (int i = 0; i < 60; i++)
  {
    frames(1, BM_KEY_A);
    if (s_game.player.y < top) top = s_game.player.y;
  }
  frames(60, 0);
  return ground - top;
}

// attachable-scripts.md step 3: an attachment on the player, with game buttons, events of what
// the player did, a lifetime, and its state in a snapshot.
static void testAnAttachmentMakesThePlayerJumpHigher(void)
{
  start();
  BmAttachments *t = &s_game.attachments;
  int32_t plain = jumpHeight();
  TEST_ASSERT(plain > BM_FIX(16));
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 2, stringId("HighJump"), 0));
  TEST_ASSERT(t->classKey[2] != 0 && t->cls[2] >= 0);
  int32_t boosted = jumpHeight();
  printf("    jump %d px, with HighJump %d px\n", (int)BM_FIX_INT(plain), (int)BM_FIX_INT(boosted));
  TEST_ASSERT(boosted > plain + BM_FIX(8));
  TEST_ASSERT(t->fields[2 * BM_SCRIPT_MAX_FIELDS + 1] == 1);   // Jumps
  TEST_ASSERT(t->fields[2 * BM_SCRIPT_MAX_FIELDS + 2] == 1);   // Presses: the game button Jump
  // A snapshot taken with it attached brings it back, bound to its class.
  static uint8_t snap[2048];
  BmStateBlock block = bmGameStateBlock(&s_game);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  bmAttachmentsDetach(&s_game, 2);
  TEST_ASSERT(t->classKey[2] == 0);
  TEST_ASSERT(jumpHeight() == plain);                          // detached: as before
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK && bmGameAfterLoad(&s_game) == BM_SUCCESS);
  TEST_ASSERT(t->classKey[2] != 0 && t->cls[2] >= 0 && t->fields[2 * BM_SCRIPT_MAX_FIELDS + 1] == 1);
  TEST_ASSERT(jumpHeight() == boosted);
  // A lifetime: gone after that many frames.
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 5, stringId("HighJump"), 3));
  frames(2, 0);
  TEST_ASSERT(t->classKey[5] != 0);
  frames(1, 0);
  TEST_ASSERT(t->classKey[5] == 0);
  // Only Attachment classes can be attached.
  TEST_ASSERT(!bmAttachmentsAttach(&s_game, 1, stringId("Bat"), 0) && !bmAttachmentsAttach(&s_game, 1, stringId("Nothing"), 0));
  TEST_ASSERT(!bmAttachmentsAttach(&s_game, BM_ATTACH_SLOTS, stringId("HighJump"), 0));
  TEST_ASSERT(bm_script_errors == 0);
}

// The room as an owner (attachable-scripts.md §8): the second room of the wind pack has
// `attach = Wind` and `Wind.Strength = 1.0`. While the player is there the wind blows them
// along; leaving detaches it; coming back attaches it again, and its `save` variable counted
// both visits.
static void testTheRoomsAttachmentsComeAndGoWithTheRoom(void)
{
  if (s_windData == NULL) return;
  startWith(s_windData, s_windSize, "test_room_2");
  BmAttachments *t = &s_game.attachments;
  const uint8_t slot = BM_ATTACH_ROOM_FIRST;
  int32_t *fields = &t->fields[slot * BM_SCRIPT_MAX_FIELDS];
  TEST_ASSERT(t->classKey[slot] != 0 && t->cls[slot] >= 0);
  TEST_ASSERT(fields[0] == BM_FIX(1) && fields[1] == 1);   // Strength from the room, Visits
  frames(20, 0);
  int32_t x0 = s_game.player.x;
  frames(30, 0);
  printf("    blown %d px in 30 frames\n", (int)BM_FIX_INT(s_game.player.x - x0));
  TEST_ASSERT(s_game.player.x > x0 + BM_FIX(8));
  TEST_ASSERT(fields[2] >= 50);   // Ticks
  // Through the west door: the wind stays behind.
  bmBodyPlace(&s_game.player, 8, 160);
  frames(2, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0);
  TEST_ASSERT(t->classKey[slot] == 0 && (t->occupied >> BM_ATTACH_ROOM_FIRST) == 0);
  // Back through the east door: attached again, the visits counted on.
  frames(60, 0);
  bmBodyPlace(&s_game.player, 990, 80);
  frames(30, BM_KEY_RIGHT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  TEST_ASSERT(t->classKey[slot] != 0 && fields[1] == 2 && fields[0] == BM_FIX(1));
  // A script's room_attach and room_detach work on the same slots: here by hand.
  bmAttachmentsDetach(&s_game, slot);
  TEST_ASSERT(t->classKey[slot] == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

static uint8_t *readFile(const char *path, uint32_t *pSize)
{
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return NULL; }
  fseek(f, 0, SEEK_END);
  *pSize = (uint32_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc(*pSize);
  if (fread(data, 1, *pSize, f) != *pSize) { fclose(f); free(data); return NULL; }
  fclose(f);
  return data;
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: test_game <pack> [pack with wind]\n");
    return 2;
  }
  s_data = readFile(argv[1], &s_size);
  if (s_data == NULL) return 1;
  if (argc > 2) s_windData = readFile(argv[2], &s_windSize);

  RUN_TEST(testAutopilotPlaysThroughTheLevel);
  RUN_TEST(testFallingIntoAPitRespawnsOnSafeGround);
  RUN_TEST(testDoorsLeadBothWays);
  RUN_TEST(testSavePointWritesASaveGame);
  RUN_TEST(testTheBatChasesHitsAndGoesHome);
  RUN_TEST(testAPickupStaysTaken);
  RUN_TEST(testAnAttachmentMakesThePlayerJumpHigher);
  RUN_TEST(testTheRoomsAttachmentsComeAndGoWithTheRoom);
  free(s_data);
  free(s_windData);
  return TEST_RESULT();
}
