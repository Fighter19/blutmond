// Game tests on the cooked test level (path to the pack as the first argument).
#include "autopilot.h"
#include "data.h"
#include "device.h"
#include "effects.h"
#include "game.h"
#include "game_live.h"
#include "hal_host.h"
#include "hits.h"
#include "live.h"
#include "state.h"
#include "physical_device.h"
#include "text.h"
#include "ui.h"
#include "video.h"

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_data, *s_windData;
static uint8_t *readFile(const char *path, uint32_t *pSize);
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
  frames(60, BM_KEY_RIGHT);   // into the door, and the walk through it
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

// versioning.md step 2: save games written by earlier versions (tests/data, made by those
// versions' engines at the save point of the first room) load: their layouts are in the pack's
// MIGR chunk (assets/test_room/save.bmschema), fields are copied by name, new ones keep their
// start values.
static void testOldSaveGames(void)
{
  static const struct { const char *file; bool hasHealth; } saves[] = {
    { "save_milestone-gba-hardware.bin", false }, { "save_311888b.bin", true }, { "save_d80f1cd.bin", true } };
  for (size_t i = 0; i < sizeof(saves) / sizeof(saves[0]); i++)
  {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", BM_TEST_DATA_DIR, saves[i].file);
    uint32_t size = 0;
    uint8_t *data = readFile(path, &size);
    TEST_ASSERT(data != NULL && size == 1024);
    start();
    memcpy(bmHalHostSaveMemory(), data, size);
    free(data);
    TEST_ASSERT(bmStateWrittenSchema(bmHalHostSaveMemory(), 1024) != 0);
    bool loaded = bmGameReadSaveGame(&s_game);
    printf("    %s: %s\n", saves[i].file, loaded ? "loaded" : "refused");
    TEST_ASSERT(loaded);
    TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0 && s_game.saves == 1);
    TEST_ASSERT(BM_FIX_INT(s_game.player.x) >= 380 && BM_FIX_INT(s_game.player.x) <= 420);
    TEST_ASSERT(s_game.health == (saves[i].hasHealth ? 17 : bmGameMaxHealth(&s_game)));
    // The return point came later: older saves keep the start's.
    TEST_ASSERT(s_game.savedRoom == 0 && (!saves[i].hasHealth || s_game.savedX == 400));
    // It plays on, and the next save is today's layout.
    frames(30, 0);
    TEST_ASSERT(bmGameWriteSaveGame(&s_game));
    BmStateBlock block = bmGameStateBlock(&s_game);
    TEST_ASSERT(bmStateWrittenSchema(bmHalHostSaveMemory(), 1024) == bmStateSchemaHash(&block, 1));
  }
  // A layout the pack does not know (a newer version's): refused, the save left as it is.
  start();
  memset(bmHalHostSaveMemory(), 0xFF, BM_HAL_SAVE_SIZE);
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  uint8_t *mem = bmHalHostSaveMemory();
  mem[8] ^= 0x5A;   // another schema hash
  uint8_t copy[64];
  memcpy(copy, mem, sizeof(copy));
  start();
  TEST_ASSERT(!bmGameReadSaveGame(&s_game) && memcmp(copy, bmHalHostSaveMemory(), sizeof(copy)) == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// --- Scripted objects (assets/test_room/scripts) ---------------------------------------------

static int actorOfClass(const char *cls)
{
  // The first that is not gone (objects made at run time have no room object: by class).
  int32_t index = bmActorsClassNamed(&s_game, cls);
  for (uint8_t i = 0; i < s_game.actors.count; i++)
  {
    if (s_game.actors.gone[i]) continue;
    if (s_game.actors.object[i] == 0xFF ? (int32_t)s_game.actors.classIndex[i] == index
                                        : strcmp(s_game.objects[s_game.actors.object[i]].className, cls) == 0)
      return i;
  }
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
  static uint8_t snap[BM_LIVE_STATE_CAPACITY];
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
  // Its bite is a hit (the box `bite` armed while chasing): the player lost its damage.
  TEST_ASSERT(s_game.health == BM_GAME_PLAYER_HEALTH - (int32_t)s_game.hurts);
  frames(1, 0);
  TEST_ASSERT(bmHitsLive(&s_game) == 0);   // Returning disarmed it
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

static uint16_t stringIdOf(const char *text);

// The item tables of first-items.md §3 to §7 in the cooked test level: every table there, the
// numbers as the plan has them, a record copied from another with its changes. (Mitsuki's
// katana and its two moves, assets/mitsuki/home, are one weapon and two commands more.)
static void testTheItemTables(void)
{
  start();
  static const struct { const char *table; uint16_t records; } tables[] = {
    { "Weapon", 8 }, { "Spell", 5 }, { "Ability", 6 }, { "Consumable", 7 }, { "Armour", 10 }, { "KeyItem", 3 }, { "Command", 19 } };
  for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++)
  {
    BmDataTable t;
    TEST_ASSERT(bmDataFindTable(&s_pack, stringIdOf(tables[i].table), &t) && t.recordCount == tables[i].records);
  }
  // Read through scripts' runtime calls, as WeaponSwing will: record_get(table, key, field).
  BmDataTable w;
  TEST_ASSERT(bmDataFindTable(&s_pack, stringIdOf("Weapon"), &w));
  int32_t attack = -1, reach = -1, element = -1, bonus = -1;
  for (uint16_t f = 0; f < w.fieldCount; f++)
  {
    BmDataField d = bmDataTableField(&w, f);
    const char *n = bmPackString(&s_pack, d.name);
    uint32_t key = 2166136261u;
    for (const char *c = "FlameBrand"; *c; c++) key = (key ^ (uint8_t)*c) * 16777619u;
    const uint8_t *r = bmDataRecordOrDefaults(&w, key);
    if (strcmp(n, "attack") == 0) attack = bmDataRead(&w, r, d.offset, (BmDataType)d.type);
    if (strcmp(n, "reach") == 0) reach = bmDataRead(&w, r, d.offset, (BmDataType)d.type);
    if (strcmp(n, "element") == 0) element = bmDataRead(&w, r, d.offset, (BmDataType)d.type);
    if (strcmp(n, "stats") == 0) bonus = bmDataRead(&w, r, (uint16_t)(d.offset + 4), BM_DATA_SHORT);   // stats.attack
  }
  TEST_ASSERT(attack == 5 && reach == 21 && element == 1 && bonus == 2);   // reach from ShortSword
}

static uint16_t stringIdOf(const char *text)
{
  for (uint32_t id = 0; id < 0xFFFF; id++)
  {
    const char *s = bmPackString(&s_pack, (uint16_t)id);
    if (s == NULL) break;
    if (strcmp(s, text) == 0) return (uint16_t)id;
  }
  return 0xFFFF;
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
  static uint8_t snap[BM_LIVE_STATE_CAPACITY];
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

// attachable-scripts.md step 4: capabilities the body step reads from the stats (a jump in the
// air, sliding down a wall and jumping off it), taken away by the runtime when the attachment
// that added them goes; the filters damage_in and damage_out; owner_velocity.
static void testCapabilitiesAndFilters(void)
{
  if (s_windData == NULL) return;
  startWith(s_windData, s_windSize, "test_room_2");
  bmAttachmentsDetach(&s_game, BM_ATTACH_ROOM_FIRST);   // no wind
  BmInventory *inv = &s_game.inventory;
  BmBody *b = &s_game.player;
  frames(30, 0);
  // Without: a second press in the air does nothing.
  frames(1, BM_KEY_A);
  while (b->vy < 0) frames(1, BM_KEY_A);
  frames(1, 0);
  frames(1, BM_KEY_A);
  TEST_ASSERT(b->vy > 0 && !b->onGround);
  frames(60, 0);
  // With Capable: one jump in the air, as high as from the ground, then no more until landing.
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 4, stringId("Capable"), 0));
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_AIR_JUMPS) == 1 && bmInventoryStat(inv, BM_STAT_WALL_SLIDE) == 1);
  frames(2, 0);
  frames(1, BM_KEY_A);
  while (b->vy < 0) frames(1, BM_KEY_A);
  frames(1, 0);
  frames(1, BM_KEY_A);
  TEST_ASSERT(b->vy == -s_game.playerParams.jumpSpeed + s_game.physics.gravity && b->airJumpsLeft == 0);
  while (b->vy < 0) frames(1, BM_KEY_A);
  frames(1, 0);
  frames(1, BM_KEY_A);
  TEST_ASSERT(b->vy > 0);
  frames(60, 0);
  TEST_ASSERT(b->onGround && b->airJumpsLeft == 1);
  // A wall slide: falling against the room's left edge while holding Left, at 1 px a frame.
  bmBodyPlace(b, 8, 40);
  b->vy = 0;
  frames(30, BM_KEY_LEFT);
  TEST_ASSERT(!b->onGround && b->wallSide == -1 && b->vy == BM_FIX(1));
  // Off the wall: up and away from it.
  frames(1, BM_KEY_LEFT | BM_KEY_A);
  TEST_ASSERT(b->vy < 0 && b->facing == 1 && b->wallSide == 0);
  frames(80, 0);
  // Detached: what it added is gone, without a detached() of its own.
  bmAttachmentsDetach(&s_game, 4);
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_AIR_JUMPS) == 0 && bmInventoryStat(inv, BM_STAT_WALL_SLIDE) == 0);
  bmBodyPlace(b, 8, 40);
  b->vy = 0;
  frames(12, BM_KEY_LEFT);
  TEST_ASSERT(b->wallSide == 0 && b->vy > BM_FIX(1));
  // (It falls into the door below: the room again.)
  startWith(s_windData, s_windSize, "test_room_2");
  bmAttachmentsDetach(&s_game, BM_ATTACH_ROOM_FIRST);
  frames(30, 0);

  // Filters: Shield halves what strikes the player and stops fire.
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 3, stringId("Shield"), 0));
  const int32_t *shield = &s_game.attachments.fields[3 * BM_SCRIPT_MAX_FIELDS];   // Filtered, LastIn
  int32_t before = s_game.health;
  TEST_ASSERT(bmGameStrikePlayerWith(&s_game, 10, b->x, 0, 0xFF));
  TEST_ASSERT(s_game.health == before - 5 && shield[0] == 1 && shield[1] == 10);
  s_game.hurtTimer = 0;
  TEST_ASSERT(!bmGameStrikePlayerWith(&s_game, 10, b->x, 1, 0xFF));   // fire
  TEST_ASSERT(s_game.health == before - 5 && shield[0] == 2 && s_game.hurtTimer == 0);
  // And triples what the player's hits do: Jab (2) strikes the dummy for 6.
  int dummy = actorOfClass("Dummy");
  TEST_ASSERT(dummy >= 0 && bmAttachmentsAttach(&s_game, 0, stringId("Jab"), 0));
  const int32_t *dummyFields = &s_game.actors.fields[dummy * BM_SCRIPT_MAX_FIELDS];   // Hits, LastDamage
  bmBodyPlace(b, BM_FIX_INT(s_game.actors.x[dummy]) - 14, 160);
  b->facing = 1;
  frames(30, 0);
  frames(2, BM_KEY_B);
  TEST_ASSERT(dummyFields[0] == 1 && dummyFields[1] == 6);
  TEST_ASSERT(s_game.attachments.fields[3] == 6);   // Jab's LastAmount: what struck
  // owner_velocity: Dash (L) sets the player's speed.
  frames(30, 0);
  frames(1, BM_KEY_L);
  TEST_ASSERT(b->vx > BM_FIX(2));
  TEST_ASSERT(bm_script_errors == 0);
}

// hitboxes.md steps 3 and 4 (the wind pack's test scripts): a guard box stops hits unless they
// pierce, #Multi strikes again and again, #Stop holds the game still, a hit's own push; objects
// the engine moves: a knife thrown at 12 pixels a frame does not pass through what it meets,
// two at most, gone at a wall; a bolt sent back with #Reflect strikes the turret that shot it.
static int spawnAs(const char *cls, int32_t x, int32_t y, uint8_t maker)
{
  BmActors *a = &s_game.actors;
  uint8_t current = a->current;
  a->current = maker;
  bool ok = bmActorsSpawn(&s_game, stringId(cls), BM_FIX(x), BM_FIX(y), 0);
  a->current = current;
  if (!ok) return -1;
  int32_t index = bmActorsClassNamed(&s_game, cls);
  for (int i = a->count - 1; i >= 0; i--)
    if (!a->gone[i] && (int32_t)a->classIndex[i] == index) return i;
  return -1;
}

static void testGuardsAndProjectiles(void)
{
  if (s_windData == NULL) return;
  startWith(s_windData, s_windSize, "test_room_2");
  bmAttachmentsDetach(&s_game, BM_ATTACH_ROOM_FIRST);   // no wind
  BmActors *a = &s_game.actors;
  BmBody *b = &s_game.player;
  frames(30, 0);
  // The knight at 300, its shield ahead (to the right).
  int knight = spawnAs("Knight", 300, 160, BM_ACTORS_ROOM);
  TEST_ASSERT(knight >= 0 && a->team[knight] == 0 && a->madeBy[knight] == BM_HITS_FREE);
  const int32_t *kf = &a->fields[knight * BM_SCRIPT_MAX_FIELDS];   // Hits, LastDamage, Blocked
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 0, stringId("Heavy"), 0));
  const int32_t *heavy = &s_game.attachments.fields[0];             // Strikes, Guarded
  // From the right, into the shield: blocked, the attachment hears guarded().
  bmBodyPlace(b, 318, 160);
  b->facing = -1;
  frames(20, 0);
  frames(2, BM_KEY_B);
  TEST_ASSERT(kf[2] == 1 && kf[0] == 0 && heavy[1] == 1 && heavy[0] == 0);
  TEST_ASSERT(s_game.hitStop == 0);
  // From behind: struck; the game holds still 6 frames (#Stop), and the push is the hit's own.
  frames(30, 0);
  bmBodyPlace(b, 286, 160);
  b->facing = 1;
  frames(20, 0);
  uint32_t frame = s_game.frame;
  int32_t px = b->x;
  frames(1, BM_KEY_B);
  TEST_ASSERT(kf[0] == 1 && kf[1] == 2 && heavy[0] == 1 && s_game.hitStop == 6);
  frames(3, BM_KEY_RIGHT);
  TEST_ASSERT(b->x == px && s_game.hitStop == 3);                    // nothing moves meanwhile
  frames(3, 0);
  TEST_ASSERT(s_game.hitStop == 0 && s_game.frame == frame + 7);
  bmAttachmentsDetach(&s_game, 0);
  // The Drill: through the shield, every 4 frames while it lasts.
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 1, stringId("Drill"), 0));
  bmBodyPlace(b, 318, 160);
  b->facing = -1;
  frames(30, 0);
  int hits = kf[0];
  frames(1, BM_KEY_SELECT);
  frames(24, 0);
  TEST_ASSERT(kf[0] - hits >= 4 && kf[2] == 1);
  bmAttachmentsDetach(&s_game, 1);
  // The shield filters the hurt box: from behind, a blade long enough to reach the shield
  // still strikes (the shield is not between it and the body); from ahead, a short one that
  // touches only the shield is blocked.
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 1, stringId("Jab"), 0));
  int32_t *jab = &s_game.attachments.fields[1 * BM_SCRIPT_MAX_FIELDS];   // Reach, Damage, Strikes
  jab[0] = 32;
  bmBodyPlace(b, 286, 160);
  b->facing = 1;
  frames(30, 0);
  hits = kf[0];
  int blocked = kf[2];
  frames(1, BM_KEY_B);
  frames(4, 0);
  TEST_ASSERT(kf[0] == hits + 1 && kf[2] == blocked && jab[2] == 1);
  jab[0] = 18;
  bmBodyPlace(b, 330, 160);
  b->facing = -1;
  frames(30, 0);
  frames(1, BM_KEY_B);
  frames(4, 0);
  TEST_ASSERT(kf[0] == hits + 1 && kf[2] == blocked + 1 && jab[2] == 1);
  bmAttachmentsDetach(&s_game, 1);

  // Knives: two in the air at most; a fast one still strikes the knight it passes.
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 2, stringId("Thrower"), 0));
  const int32_t *thrower = &s_game.attachments.fields[2 * BM_SCRIPT_MAX_FIELDS];   // Thrown, Strikes
  bmBodyPlace(b, 200, 160);
  b->facing = 1;
  frames(30, 0);
  hits = kf[0];
  frames(1, BM_KEY_R);
  int knife = actorOfClass("Knife");
  TEST_ASSERT(knife >= 0 && a->vx[knife] == BM_FIX(12) && a->team[knife] == BM_HITS_PLAYER_TEAM && a->madeBy[knife] == BM_HITS_PLAYER);
  frames(1, 0);
  frames(1, BM_KEY_R);
  frames(1, 0);
  frames(1, BM_KEY_R);   // a third: not while two are in the air
  TEST_ASSERT(thrower[0] == 2);
  frames(20, 0);
  TEST_ASSERT(kf[0] == hits + 1 && thrower[1] >= 1);                // struck, and the thrower heard of it
  TEST_ASSERT(actorOfClass("Knife") < 0 || a->gone[actorOfClass("Knife")]);
  // From behind, sped up to 20 pixels a frame, wherever in a frame's way the knife first meets
  // the knight (some over body and shield at once): struck. From ahead: blocked, and gone
  // (guarded()).
  for (int off = 0; off < 20; off++)
  {
    bmBodyPlace(b, 240 + off, 160);
    b->facing = 1;
    frames(30, 0);
    hits = kf[0];
    blocked = kf[2];
    frames(1, BM_KEY_R);
    knife = actorOfClass("Knife");
    TEST_ASSERT(knife >= 0);
    a->vx[knife] = BM_FIX(20);
    frames(20, 0);
    TEST_ASSERT(kf[0] == hits + 1 && kf[2] == blocked);
  }
  bmBodyPlace(b, 400, 160);
  b->facing = -1;
  frames(30, 0);
  hits = kf[0];
  frames(1, BM_KEY_R);
  frames(20, 0);
  TEST_ASSERT(kf[0] == hits && kf[2] == blocked + 1);
  TEST_ASSERT(actorOfClass("Knife") < 0 || a->gone[actorOfClass("Knife")]);
  // Thrown at the room's right wall from close by: gone there.
  bmBodyPlace(b, 470, 160);
  b->facing = 1;
  frames(30, 0);
  frames(1, BM_KEY_R);
  frames(10, 0);
  TEST_ASSERT(actorOfClass("Knife") < 0 || a->gone[actorOfClass("Knife")]);
  bmAttachmentsDetach(&s_game, 2);

  // The turret's bolt, parried: back to the turret, which it strikes.
  int turret = spawnAs("Turret", 120, 160, BM_ACTORS_ROOM);
  TEST_ASSERT(turret >= 0);
  bmBodyPlace(b, 200, 160);
  b->facing = -1;
  frames(30, 0);
  int bolt = spawnAs("Bolt", 136, 156, (uint8_t)turret);
  TEST_ASSERT(bolt >= 0 && a->madeBy[bolt] == 1 + turret && a->vx[bolt] == BM_FIX(3));
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 3, stringId("Parry"), 0));
  // Wait until it is close, then parry.
  for (int i = 0; i < 40 && BM_FIX_INT(a->x[bolt]) < 176; i++) frames(1, 0);
  int32_t health = s_game.health;
  frames(1, BM_KEY_B);
  frames(1, 0);
  TEST_ASSERT(a->vx[bolt] == -BM_FIX(3) && a->team[bolt] == BM_HITS_PLAYER_TEAM && a->madeBy[bolt] == BM_HITS_PLAYER);
  frames(40, 0);
  TEST_ASSERT(a->fields[turret * BM_SCRIPT_MAX_FIELDS] == 1 && s_game.health == health);
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
  frames(60, BM_KEY_RIGHT);   // into the door, and the walk through it
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  TEST_ASSERT(t->classKey[slot] != 0 && fields[1] == 2 && fields[0] == BM_FIX(1));
  // A script's room_attach and room_detach work on the same slots: here by hand.
  bmAttachmentsDetach(&s_game, slot);
  TEST_ASSERT(t->classKey[slot] == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// hitboxes.md step 2: a hit strikes a figure once however many frames it overlaps, a second
// hit strikes again once the figure can be struck again, a hit facing away misses; the source
// hears of it (struck), the target too (hit), and health runs out into a new start.
static void testAHitStrikesOnce(void)
{
  // Jab is in the pack with wind (only there): without the wind, the room is the test level's.
  if (s_windData == NULL) return;
  startWith(s_windData, s_windSize, "test_room_2");
  bmAttachmentsDetach(&s_game, BM_ATTACH_ROOM_FIRST);
  int dummy = actorOfClass("Dummy");
  TEST_ASSERT(dummy >= 0 && s_game.actors.health[dummy] == 999);
  const int32_t *dummyFields = &s_game.actors.fields[dummy * BM_SCRIPT_MAX_FIELDS];   // Hits, LastDamage
  TEST_ASSERT(bmAttachmentsAttach(&s_game, 0, stringId("Jab"), 0));
  const int32_t *jab = &s_game.attachments.fields[0];   // Reach, Damage, Strikes, LastAmount, Taken
  TEST_ASSERT(jab[0] == 20 && jab[1] == 2);

  // Just left of the dummy, facing it.
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) - 14, 160);
  s_game.player.facing = 1;
  frames(30, 0);
  frames(6, BM_KEY_B);   // pressed once and held: the rectangle lives 3 frames
  TEST_ASSERT(dummyFields[0] == 1 && dummyFields[1] == 2);
  TEST_ASSERT(jab[2] == 1 && jab[3] == 2);
  TEST_ASSERT(bmHitsLive(&s_game) == 0);
  // Pressed again at once: a new hit, but the dummy cannot be struck yet.
  frames(1, 0);
  frames(1, BM_KEY_B);
  frames(3, 0);
  TEST_ASSERT(dummyFields[0] == 1 && jab[2] == 1);
  // Later it can.
  frames(20, 0);
  frames(1, BM_KEY_B);
  TEST_ASSERT(dummyFields[0] == 2 && jab[2] == 2);
  // Facing away: nothing.
  frames(30, 0);
  s_game.player.facing = -1;
  frames(1, BM_KEY_B);
  frames(4, 0);
  TEST_ASSERT(dummyFields[0] == 2);
  TEST_ASSERT(s_game.actors.health[dummy] == 999);

  // A snapshot in the middle of a hit continues it: the strike comes after loading it too.
  s_game.player.facing = 1;
  frames(30, 0);
  frames(1, BM_KEY_B);
  TEST_ASSERT(dummyFields[0] == 3);
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) - 60, 160);
  frames(22, 0);
  s_game.player.facing = 1;
  frames(1, BM_KEY_B);
  TEST_ASSERT(bmHitsLive(&s_game) == 1);
  static uint8_t snap[BM_LIVE_STATE_CAPACITY];
  BmStateBlock block = bmGameStateBlock(&s_game);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  printf("    snapshot with hits: %u bytes\n", (unsigned)size);
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK && bmGameAfterLoad(&s_game) == BM_SUCCESS);
  TEST_ASSERT(bmHitsLive(&s_game) == 1);

  // The player struck: health, hit() to the attachments; at 0 a new start with all of it.
  uint32_t deaths = s_game.deaths;
  TEST_ASSERT(bmGameStrikePlayer(&s_game, 3, s_game.player.x + BM_FIX(4)));
  TEST_ASSERT(s_game.health == BM_GAME_PLAYER_HEALTH - 3 && jab[4] == 3);
  TEST_ASSERT(s_game.player.vx < 0);                                   // pushed away from it
  TEST_ASSERT(bmHitsLive(&s_game) == 0);                                // the player's hit ended
  TEST_ASSERT(!bmGameStrikePlayer(&s_game, 3, s_game.player.x));        // still blinking
  s_game.hurtTimer = 0;
  TEST_ASSERT(bmGameStrikePlayer(&s_game, 100, s_game.player.x));
  TEST_ASSERT(s_game.deaths == deaths + 1 && s_game.health == BM_GAME_PLAYER_HEALTH);
  TEST_ASSERT(bm_script_errors == 0);
}

static uint32_t keyOf(const char *name)
{
  uint32_t h = 2166136261u;
  for (; *name; name++) h = (h ^ (uint8_t)*name) * 16777619u;
  return h;
}

// first-items.md step 2: the Short Sword from its stand near the start, equipped with its
// script; it strikes the dummy for its attack after its startup, holds the player still while
// it swings and defeats the bat with two strikes; the Dagger replaces it; a save game brings
// the equipped weapon's script back.
static void testTheSwordDefeatsTheBat(void)
{
  start();
  const BmAttachments *t = &s_game.attachments;
  const uint32_t sword = keyOf("ShortSword"), dagger = keyOf("Dagger");
  int stand = actorOfClass("WeaponStand");
  TEST_ASSERT(stand >= 0);
  bmBodyPlace(&s_game.player, 80, 160);
  frames(5, 0);
  TEST_ASSERT(s_game.actors.hidden[stand] && bmInventoryCount(&s_game.inventory, sword) == 1);
  TEST_ASSERT(s_game.inventory.equipped[0] == sword && t->classKey[0] == keyOf("WeaponSwing") && t->cls[0] >= 0);
  const int32_t *swing = &t->fields[0];   // Item, Swings, Strikes
  TEST_ASSERT((uint32_t)swing[0] == sword);

  // Through the door into the second room.
  bmBodyPlace(&s_game.player, 990, 80);
  frames(60, BM_KEY_RIGHT);   // into the door, and the walk through it
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  int dummy = actorOfClass("Dummy"), bat = actorOfClass("Bat");
  TEST_ASSERT(dummy >= 0 && bat >= 0 && s_game.actors.health[bat] == 4);
  const int32_t *dummyFields = &s_game.actors.fields[dummy * BM_SCRIPT_MAX_FIELDS];   // Hits, LastDamage

  // A swing at the dummy: nothing during the startup, then the Short Sword's 3; the player
  // stands still while it lasts, then walks again.
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) - 14, 160);
  s_game.player.facing = 1;
  frames(30, 0);
  int32_t x0 = s_game.player.x;
  frames(1, BM_KEY_B | BM_KEY_RIGHT);
  TEST_ASSERT(swing[1] == 1 && s_game.lockTimer != 0 && dummyFields[0] == 0);
  frames(3, BM_KEY_RIGHT);
  TEST_ASSERT(dummyFields[0] == 0);
  frames(1, BM_KEY_RIGHT);
  TEST_ASSERT(dummyFields[0] == 1 && dummyFields[1] == 3 && swing[2] == 1);
  TEST_ASSERT(s_game.player.x == x0);
  frames(10, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.lockTimer == 0);
  frames(10, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.player.x > x0);

  // The bat: the autopilot's way of fighting (turn to it, swing) until it is defeated.
  bmBodyPlace(&s_game.player, 180, 160);
  BmAutopilot pilot = { .idle = 100 };
  int frame = 0;
  for (; frame < 900 && !s_game.actors.hidden[bat]; frame++)
  {
    bmVideoWaitFrame();
    bmGameFrame(&s_game, bmAutopilotKeys(&pilot, &s_game));
  }
  printf("    bat defeated after %d frames, %d swings, player health %d\n", frame, (int)swing[1], (int)s_game.health);
  TEST_ASSERT(s_game.actors.hidden[bat] && s_game.actors.health[bat] <= 0 && strcmp(stateOf(bat), "Defeated") == 0);
  TEST_ASSERT(swing[2] == 3);   // the dummy once, the bat twice

  // The Dagger from its stand goes into the inventory (a weapon is equipped already); put in the
  // weapon slot it replaces the sword: same script, its numbers.
  frames(40, 0);
  bmBodyPlace(&s_game.player, 400, 160);
  frames(5, 0);
  TEST_ASSERT(bmInventoryCount(&s_game.inventory, dagger) == 1 && s_game.inventory.equipped[0] == sword);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, dagger));
  TEST_ASSERT(s_game.inventory.equipped[0] == dagger && (uint32_t)swing[0] == dagger && swing[1] == 0);
  TEST_ASSERT(bmInventoryCount(&s_game.inventory, sword) == 1);
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) - 10, 160);
  s_game.player.facing = 1;
  frames(30, 0);
  frames(1, BM_KEY_B);
  frames(2, 0);
  TEST_ASSERT(dummyFields[0] == 2 && dummyFields[1] == 2);

  // A save game holds what is equipped; reading it attaches its script again.
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  bmAttachmentsEquip(&s_game, 0, 0);
  TEST_ASSERT(t->classKey[0] == 0 && s_game.inventory.equipped[0] == 0);
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  TEST_ASSERT(s_game.inventory.equipped[0] == dagger && t->classKey[0] == keyOf("WeaponSwing") && (uint32_t)swing[0] == dagger);
  // Only what the player owns can be equipped.
  TEST_ASSERT(!bmAttachmentsEquip(&s_game, 0, keyOf("GreatAxe")) && s_game.inventory.equipped[0] == dagger);
  TEST_ASSERT(bm_script_errors == 0);
}

// The character a UI cell shows (through the glyph cache), ' ' for an empty or filled cell.
static uint32_t cellChar(const BmUi *ui, int x, int y)
{
  uint8_t t = ui->map[y * 32 + x];
  return t >= BM_UI_GLYPH_FIRST ? ui->glyphCode[t - BM_UI_GLYPH_FIRST] : ' ';
}

static void rowText(const BmUi *ui, int x, int y, int n, char *out)
{
  for (int i = 0; i < n; i++) out[i] = (char)cellChar(ui, x + i, y);
  out[n] = 0;
}

// ui.md step 2 and the languages: the UI layer with the engine's font from the pack (ASCII and
// the accented letters, two FONT chunks of one font), frames, labels, numbers, placeholders,
// wrapped text, the glyph cache, and texts in French and German.
static void testTheUiLayer(void)
{
  start();
  static BmUi ui;
  TEST_ASSERT(bmUiInit(&ui, &s_pack, "nothing") == BM_ERROR_NOT_FOUND && !ui.ready);
  TEST_ASSERT(bmUiInit(&ui, &s_pack, "font") == BM_SUCCESS && ui.ready && ui.fontChunkCount == 2);
  bmUiLabel(&ui, 0, 0, "A");
  uint8_t a = ui.map[0];
  TEST_ASSERT(a >= BM_UI_GLYPH_FIRST && cellChar(&ui, 0, 0) == 'A');
  // On a frame's inside: another tile for the same character, and the frame's pieces.
  bmUiFrame(&ui, 2, 2, 6, 4);
  TEST_ASSERT(ui.map[2 * 32 + 2] == 1 && ui.map[2 * 32 + 3] == 2 && ui.map[3 * 32 + 3] == 5 && ui.map[5 * 32 + 7] == 9);
  bmUiLabel(&ui, 3, 3, "AA\nA");
  TEST_ASSERT(cellChar(&ui, 3, 3) == 'A' && ui.map[3 * 32 + 3] != a && ui.map[3 * 32 + 3] == ui.map[4 * 32 + 3]);
  bmUiLabel(&ui, 3, 3, " ");   // a space on the inside is the inside
  TEST_ASSERT(ui.map[3 * 32 + 3] == 5);
  bmUiLabel(&ui, 20, 0, "\x7f");   // not in the font: '?'
  TEST_ASSERT(cellChar(&ui, 20, 0) == '?');
  // UTF-8: "Épée" is four characters.
  TEST_ASSERT(bmUiLabel(&ui, 0, 1, "\xc3\x89p\xc3\xa9" "e") == 4 && cellChar(&ui, 0, 1) == 0xC9 && cellChar(&ui, 2, 1) == 0xE9);
  // Numbers: right-aligned, minus sign, stars when too long.
  char row[16];
  bmUiNumber(&ui, 0, 10, -42, 5);
  rowText(&ui, 0, 10, 5, row);
  TEST_ASSERT(strcmp(row, "  -42") == 0);
  bmUiNumber(&ui, 0, 10, 123456, 3);
  rowText(&ui, 0, 10, 3, row);
  TEST_ASSERT(strcmp(row, "***") == 0);
  // A gauge: 3 of 8 in 2 cells is 6 eighths.
  bmUiGauge(&ui, 0, 11, 2, 3, 8);
  TEST_ASSERT(ui.map[11 * 32] == 10 + 6 && ui.map[11 * 32 + 1] == 10);
  // Placeholders: numbers and texts of the pack, {{ for a brace, unknown ones left out; a
  // character cut at the end is left out whole.
  char text[64];
  BmUiArg args[2] = { bmUiNumberArg(-7), bmUiTextArg(stringId("Dagger")) };
  TEST_ASSERT(bmUiFormat(&s_pack, 0, text, sizeof(text), "{1} x{0} {{ok} {5}", args, 2) == 16 && strcmp(text, "Dagger x-7 {ok} ") == 0);
  TEST_ASSERT(bmUiFormat(&s_pack, 0, text, 5, "{1} x{0}", args, 2) == 4 && strcmp(text, "Dagg") == 0);
  TEST_ASSERT(bmUiFormat(&s_pack, 0, text, 3, "a\xc3\xa9", NULL, 0) == 1 && strcmp(text, "a") == 0);
  // Wrapped at spaces: "A plain blade. The weapon everyone starts with." in 12 x 3 cells.
  bmUiClear(&ui);
  uint16_t blade = stringId("A plain blade. The weapon everyone starts with.");
  TEST_ASSERT(bmUiText(&ui, 1, 12, 12, 3, blade, NULL, 0) == 3);
  char lines[3][13];
  for (int y = 0; y < 3; y++) rowText(&ui, 1, 12 + y, 12, lines[y]);
  TEST_ASSERT(strncmp(lines[0], "A plain", 7) == 0 && strncmp(lines[1], "blade. The", 10) == 0 && strncmp(lines[2], "weapon", 6) == 0);

  // Languages: English (the main one), German, French, in the order of the files.
  TEST_ASSERT(bmTextLanguageCount(&s_pack) == 3);
  TEST_ASSERT(strcmp(bmPackString(&s_pack, bmTextLanguageName(&s_pack, 0)), "English") == 0);
  int32_t de = bmTextFindLanguage(&s_pack, "de"), fr = bmTextFindLanguage(&s_pack, "fr");
  TEST_ASSERT(de == 1 && fr == 2 && bmTextFindLanguage(&s_pack, "ja") == -1);
  TEST_ASSERT(strcmp(bmPackString(&s_pack, bmTextLanguageName(&s_pack, (uint8_t)fr)), "Fran\xc3\xa7" "ais") == 0);
  TEST_ASSERT(strcmp(bmText(&s_pack, (uint8_t)de, stringId("Short Sword")), "Kurzschwert") == 0);
  TEST_ASSERT(strcmp(bmText(&s_pack, (uint8_t)fr, stringId("Short Sword")), "\xc3\x89p\xc3\xa9" "e courte") == 0);
  TEST_ASSERT(strcmp(bmText(&s_pack, (uint8_t)fr, stringId("test_room")), "test_room") == 0);   // not translated
  bmUiSetLanguage(&ui, (uint8_t)de);
  bmUiClear(&ui);
  bmUiText(&ui, 0, 0, 30, 2, blade, NULL, 0);
  rowText(&ui, 0, 0, 7, row);
  TEST_ASSERT(strcmp(row, "Eine sc") == 0);
  // The game keeps the language as an option.
  bmGameSetLanguage(&s_game, (uint8_t)fr);
  TEST_ASSERT(s_game.language == (uint8_t)fr && s_game.ui.language == (uint8_t)fr);
  bmGameStart(&s_game, &s_pack, NULL);
  TEST_ASSERT(s_game.language == (uint8_t)fr);
  bmGameSetLanguage(&s_game, 0);

  // The glyph cache: every character of the font on both grounds is more than it holds; tiles
  // no cell shows any more are taken again.
  bmUiSetLanguage(&ui, 0);
  bmUiClear(&ui);
  bmUiFrame(&ui, 0, 10, 32, 6);
  for (int c = 33; c < 127; c++)
  {
    char one[2] = { (char)c, 0 };
    bmUiLabel(&ui, (c - 33) % 32, (c - 33) / 32, one);
    bmUiLabel(&ui, 1 + (c - 33) % 30, 11 + (c - 33) / 30, one);
  }
  bmUiClear(&ui);
  bmUiLabel(&ui, 0, 0, "\xc3\x84\xc3\x96\xc3\x9c\xc3\x9f \xc3\xa0\xc3\xa7\xc3\xa8 \xe2\x80\x9chi\xe2\x80\x9d");
  TEST_ASSERT(cellChar(&ui, 0, 0) == 0xC4 && cellChar(&ui, 3, 0) == 0xDF && cellChar(&ui, 9, 0) == 0x201C);

  // Present: background 3 shown, the map in screen block 25 with palette bank 15.
  bmUiShow(&ui, true);
  bmUiPresent(&ui);
  const BmPpuState *ppu = bmHalHostGetPpu();
  TEST_ASSERT(ppu->regs[BM_PPU_DISPCNT / 2] & BM_PPU_DISPCNT_BG(3));
  uint16_t e = (uint16_t)(ppu->vram[25 * 0x800] | ppu->vram[25 * 0x800 + 1] << 8);
  TEST_ASSERT(e == (uint16_t)(ui.map[0] | 15 << 12) && ui.dirtyRows == 0);
}

static void press(uint16_t key)
{
  frames(1, key);
  frames(1, 0);
}

// Whether the UI layer shows `text` (ASCII) somewhere.
static bool uiShows(const char *text)
{
  size_t n = strlen(text);
  for (int y = 0; y < BM_UI_ROWS; y++)
    for (int x = 0; x + (int)n <= 32; x++)
    {
      size_t k = 0;
      while (k < n && cellChar(&s_game.ui, x + (int)k, y) == (uint8_t)text[k]) k++;
      if (k == n) return true;
    }
  return false;
}

// The colour correction (pack-format.md §3.3): the pack holds the AGB-001's; on the PC the
// original colours are the default; switched on, every colour the game wrote is corrected in
// palette memory, the choice is kept as an option, and switched off the colours come back as
// they were. The System menu switches it on its last row.
static void testColourCorrection(void)
{
  start();
  int32_t chunk = bmPackFindChunk(&s_pack, BM_CHUNK_CCOR, 0);
  uint32_t size = 0;
  const uint8_t *p = chunk >= 0 ? bmPackChunkData(&s_pack, (uint32_t)chunk, &size) : NULL;
  TEST_ASSERT(p != NULL && size == 4 + sizeof(BmColorCorrection) && p[0] == 1 && p[1] == 1);
  BmColorCorrection cc;
  memcpy(&cc, p + 4, sizeof(cc));
  for (int row = 0; row < 3; row++) TEST_ASSERT(cc.matrix[row * 3] + cc.matrix[row * 3 + 1] + cc.matrix[row * 3 + 2] == 4096);
  TEST_ASSERT(bmColorCorrect(&cc, 0) == 0 && bmColorCorrect(&cc, 0x7FFF) == 0x7FFF);
  TEST_ASSERT(bmColorCorrect(&cc, bmColorRGB(16, 16, 16).color) == bmColorRGB(23, 23, 23).color);   // greys stay grey, brighter
  TEST_ASSERT(bmColorCorrect(&cc, bmColorRGB(31, 0, 0).color) == bmColorRGB(31, 0, 0).color);
  TEST_ASSERT(bmColorCorrect(&cc, bmColorRGB(0, 0, 31).color) == bmColorRGB(16, 0, 31).color);

  TEST_ASSERT(s_game.colorCorrectable && s_game.colorChoice == 0 && !bmGameColorCorrection(&s_game));   // the PC
  frames(2, 0);
  static uint16_t before[BM_HAL_PALETTE_SIZE];
  memcpy(before, bmHalHostGetPpu()->palette, sizeof(before));
  bmGameSetColorCorrection(&s_game, true);
  TEST_ASSERT(bmGameColorCorrection(&s_game) && s_game.colorChoice == 1);
  const uint16_t *now = bmHalHostGetPpu()->palette;
  uint32_t changed = 0;
  for (int i = 1; i < 16; i++) TEST_ASSERT(now[i] == bmColorCorrect(&cc, before[i]));   // the room's first bank
  for (int i = 1; i < 14; i++) TEST_ASSERT(now[BM_UI_PALETTE_BANK * 16 + i] == bmColorCorrect(&cc, before[BM_UI_PALETTE_BANK * 16 + i]));
  for (int i = 0; i < BM_HAL_PALETTE_SIZE; i++) changed += now[i] != before[i];
  printf("    %u colours corrected\n", (unsigned)changed);
  TEST_ASSERT(changed > 50);
  // Kept as an option.
  bmGameStart(&s_game, &s_pack, NULL);
  TEST_ASSERT(s_game.colorChoice == 1 && bmGameColorCorrection(&s_game));
  bmGameSetColorCorrection(&s_game, false);
  TEST_ASSERT(s_game.colorChoice == 2 && !bmGameColorCorrection(&s_game));
  bmGameStart(&s_game, &s_pack, NULL);
  TEST_ASSERT(s_game.colorChoice == 2);
  frames(2, 0);
  TEST_ASSERT(memcmp(before, bmHalHostGetPpu()->palette, sizeof(before)) == 0);

  // The System menu (Start, down to below Map, A): below the languages the colours' row.
  press(BM_KEY_START);
  for (int i = 0; i < 4; i++) press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Language") && uiShows("PC colours"));
  for (int i = 0; i < bmTextLanguageCount(&s_pack); i++) press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(bmGameColorCorrection(&s_game) && uiShows("GBA colours") && !uiShows("PC colours"));
  TEST_ASSERT(bmHalHostGetPpu()->palette[1] == bmColorCorrect(&cc, before[1]));
  press(BM_KEY_A);
  TEST_ASSERT(!bmGameColorCorrection(&s_game) && uiShows("PC colours") && s_game.language == 0);
  press(BM_KEY_B);
  press(BM_KEY_B);
  TEST_ASSERT(s_game.screens.count == 0);
  // Not chosen again, for the other tests.
  s_game.colorChoice = 0;
  bmGameSetLanguage(&s_game, 0);
}

// ui.md steps 3 and 4, first-items.md step 3: the Start menu (a Screen script) pauses the room;
// Equipment puts the Iron Helm on, whose script adds its defence while worn, and the damage
// rule takes it off a hit; Items uses a potion; System switches the language and every open
// menu shows itself again in it; a state taken inside a menu comes back inside it.
static void testTheMenus(void)
{
  start();
  BmInventory *inv = &s_game.inventory;
  const uint32_t helm = keyOf("IronHelm"), potion = keyOf("Potion"), sword = keyOf("ShortSword");
  bmInventoryGive(inv, helm, 1);
  bmInventoryGive(inv, potion, 2);
  bmInventoryGive(inv, sword, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, sword));
  frames(10, 0);

  // Start: the menu, the room stands still.
  int32_t x = s_game.player.x;
  press(BM_KEY_START);
  TEST_ASSERT(s_game.screens.count == 1 && s_game.ui.shown);
  TEST_ASSERT(uiShows("Equipment") && uiShows("Short Sword") && uiShows("Health"));
  frames(20, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.player.x == x);
  // Equipment: down to Head, A, down to the helm, A.
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Weapon"));
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(uiShows("Iron Helm"));
  press(BM_KEY_DOWN);
  TEST_ASSERT(uiShows(">   3"));   // the defence it would give
  press(BM_KEY_A);
  TEST_ASSERT(s_game.inventory.equipped[2] == helm);
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_DEFENCE) == 3 && inv->statBase[BM_STAT_DEFENCE] == 0);
  // Back to the Start menu, then Items: a potion heals 15.
  press(BM_KEY_B);
  TEST_ASSERT(s_game.screens.count == 1);
  s_game.health = 10;
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Potion"));
  press(BM_KEY_A);
  TEST_ASSERT(s_game.health == 25 && bmInventoryCount(inv, potion) == 1);
  press(BM_KEY_B);
  // A state taken in the Start menu comes back in it, drawn again.
  static uint8_t snap[BM_LIVE_STATE_CAPACITY];
  BmStateBlock block = bmGameStateBlock(&s_game);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  printf("    snapshot in a menu: %u bytes\n", (unsigned)size);
  // Commands: the special moves of the weapon in hand (the Short Sword's Lunge) and its input.
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Lunge") && uiShows("Down, Down-Fwd, Fwd + B") && !uiShows("Flurry"));
  // Down and Up move through the list (two moves: Lunge, Rising Slash), and B still goes back.
  uint32_t errors = bm_script_errors;
  press(BM_KEY_DOWN);
  TEST_ASSERT(bm_script_errors == errors && uiShows("Two cuts on the way up."));
  press(BM_KEY_DOWN);
  TEST_ASSERT(bm_script_errors == errors && uiShows("A dash forward"));
  press(BM_KEY_UP);
  TEST_ASSERT(bm_script_errors == errors && uiShows("Two cuts on the way up."));
  press(BM_KEY_B);
  TEST_ASSERT(s_game.screens.count == 1);
  // System (below Map): German. Every open menu shows itself again in it.
  press(BM_KEY_DOWN);
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2);
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.language == 1 && uiShows("Sprache"));
  press(BM_KEY_B);
  TEST_ASSERT(uiShows("Kurzschwert") && uiShows("Ausr"));   // Ausrüstung
  press(BM_KEY_B);
  // The menus closed: the layer shows only the HUD again.
  TEST_ASSERT(s_game.screens.count == 0 && s_game.ui.shown && uiShows("HP") && !uiShows("Ausr"));
  bmGameSetLanguage(&s_game, 0);
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK && bmGameAfterLoad(&s_game) == BM_SUCCESS);
  TEST_ASSERT(s_game.screens.count == 1 && uiShows("Equipment") && uiShows("Short Sword"));
  press(BM_KEY_START);
  TEST_ASSERT(s_game.screens.count == 0);
  // The damage rule: 5 less the helm's 3.
  int32_t before = s_game.health;
  s_game.hurtTimer = 0;
  TEST_ASSERT(bmGameStrikePlayer(&s_game, 5, s_game.player.x));
  TEST_ASSERT(s_game.health == before - 2);
  // Taken off: the defence goes with it.
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 2, 0) && bmInventoryStat(inv, BM_STAT_DEFENCE) == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// The world map (docs/spec/world-format.md): the cells the player has been in are explored,
// the HUD shows a small map around them, the Map page of the Start menu all of it; a room can
// be revealed; it all comes back with the save game; a room that is not on the map has no
// small map.
static bool mapCodeAt(int x, int y, uint16_t *pLook)
{
  uint32_t c = cellChar(&s_game.ui, x, y);
  if (c < 0xE000 || c >= 0xE800) return false;
  if (pLook) *pLook = (uint16_t)(c - 0xE000);
  return true;
}

static int mapCellsIn(int x0, int y0, int w, int h)
{
  int n = 0;
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) n += mapCodeAt(x, y, NULL);
  return n;
}

static int exploredCells(void)
{
  int n = 0;
  for (uint16_t c = 0; c < s_game.map.cellCount; c++) n += bmMapExplored(&s_game, c);
  return n;
}

static void testTheWorldMap(void)
{
  start();
  frames(3, 0);
  // The chasm, the gatehouse and the guard room (2 x 1 cells each), test_room (4 x 1),
  // test_room_2 (2 x 1) and the boss hall (2 x 1) in a row, the hall of abilities (2 x 2)
  // behind it; the debug rooms are hidden.
  TEST_ASSERT(s_game.map.pChunk != NULL && s_game.map.roomCount == 7 && s_game.map.cellCount == 18);
  TEST_ASSERT(s_game.map.room == 0);
  TEST_ASSERT(exploredCells() == 1 && bmMapPercent(&s_game) == 5);
  // The HUD's map: the player's cell in the middle (26, 2), marked, in the room they are in.
  uint16_t look = 0;
  TEST_ASSERT(mapCodeAt(26, 2, &look));
  TEST_ASSERT((look & BM_UI_MAP_MARKER_BIT) && ((look >> 8) & 3) == BM_UI_MAP_HERE_KIND);
  TEST_ASSERT((look & 0x05) == 0x05);   // walls north and south (a room one cell high)
  // Through the east door: its cell and the next room's first are explored.
  bmBodyPlace(&s_game.player, 990, 80);
  frames(60, BM_KEY_RIGHT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  frames(5, 0);
  TEST_ASSERT(s_game.map.room == 1 && exploredCells() == 3 && bmMapPercent(&s_game) == 16);
  // The door shows on the HUD's map: test_room's last cell, left of the player, has a door east.
  TEST_ASSERT(mapCodeAt(25, 2, &look) && (look & 0x22) == 0x22 && ((look >> 8) & 3) == BM_UI_MAP_ROOM_KIND);
  TEST_ASSERT(mapCodeAt(26, 2, &look) && (look & 0x88) == 0x88);   // and west in the player's cell
  // The Map page (Start, down to Map, A): the explored cells, how much in percent.
  press(BM_KEY_START);
  press(BM_KEY_DOWN);
  press(BM_KEY_DOWN);
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Map") && uiShows("16"));
  TEST_ASSERT(mapCellsIn(2, 4, 26, 14) == 3);
  // The player's cell blinks: drawn again with the frame.
  int marked = 0;
  for (int i = 0; i < 40; i++)
  {
    frames(1, 0);
    for (int y = 4; y < 18; y++)
      for (int x = 2; x < 28; x++)
        if (mapCodeAt(x, y, &look) && (look & BM_UI_MAP_MARKER_BIT)) marked |= 1;
    bool any = false;
    for (int y = 4; y < 18; y++)
      for (int x = 2; x < 28; x++) any |= mapCodeAt(x, y, &look) && (look & BM_UI_MAP_MARKER_BIT);
    if (!any) marked |= 2;
  }
  TEST_ASSERT(marked == 3);
  press(BM_KEY_B);
  press(BM_KEY_B);
  TEST_ASSERT(s_game.screens.count == 0);
  // A map station reveals the boss hall: its two cells are shown, not explored.
  TEST_ASSERT(bmMapReveal(&s_game, stringId("boss_hall")));
  TEST_ASSERT(!bmMapReveal(&s_game, stringId("debug_lobby")));   // hidden: not on the map
  TEST_ASSERT(exploredCells() == 3);
  press(BM_KEY_START);
  press(BM_KEY_DOWN);
  press(BM_KEY_DOWN);
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(mapCellsIn(2, 4, 26, 14) == 5);
  int revealed = 0;
  for (int y = 4; y < 18; y++)
    for (int x = 2; x < 28; x++) revealed += mapCodeAt(x, y, &look) && ((look >> 8) & 3) == BM_UI_MAP_REVEALED_KIND;
  TEST_ASSERT(revealed == 2);
  press(BM_KEY_B);
  press(BM_KEY_B);
  // The save game keeps what was explored and revealed.
  memset(bmHalHostSaveMemory(), 0xFF, BM_HAL_SAVE_SIZE);
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  start();
  TEST_ASSERT(exploredCells() <= 1);
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  frames(3, 0);
  TEST_ASSERT(exploredCells() == 3 && (s_game.mapRevealed[0] & 4) != 0);
  // A room that is not on the map: no small map.
  startWith(s_data, s_size, "debug_lobby");
  frames(3, 0);
  TEST_ASSERT(s_game.map.room < 0 && !mapCodeAt(26, 2, NULL) && bmMapPercent(&s_game) == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// How many sprites use palette bank `bank` (the hit flash).
static int spritesInBank(uint8_t bank)
{
  const BmPpuState *ppu = bmHalHostGetPpu();
  int n = 0;
  for (int i = 0; i < 128; i++)
  {
    uint16_t attr0 = ppu->oam[i * 4], attr2 = ppu->oam[i * 4 + 2];
    if ((attr0 & 0x0300) == 0x0200) continue;   // hidden
    if ((attr2 >> 12) == bank) n++;
  }
  return n;
}

// A struck object is pushed away from the one who struck it, slower each frame until it stands
// (a wall stops it sooner), and is drawn in the white flash bank for its first frames; one that
// does not budge (set_knockback(0): the dummy, the boss) only flashes.
static void testKnockbackAndFlash(void)
{
  startWith(s_data, s_size, "test_room_2");
  frames(2, 0);
  int bat = actorOfClass("Bat");
  TEST_ASSERT(bat >= 0);
  BmActors *a = &s_game.actors;
  TEST_ASSERT(spritesInBank(BM_HITS_FLASH_BANK) == 0);
  int32_t x = a->x[bat];
  // Struck from its left: pushed right.
  TEST_ASSERT(bmHitsStrikeObject(&s_game, (uint8_t)bat, 1, 0, a->x[bat] - BM_FIX(10)));
  TEST_ASSERT(a->knock[bat] == BM_HITS_KNOCK);
  // Sprites reach video memory at the start of the frame after they were set.
  frames(2, 0);
  TEST_ASSERT(spritesInBank(BM_HITS_FLASH_BANK) > 0);
  frames(BM_HITS_FLASH_FRAMES + 1, 0);
  TEST_ASSERT(spritesInBank(BM_HITS_FLASH_BANK) == 0);
  frames(10, 0);
  TEST_ASSERT(a->knock[bat] == 0);
  // 2 + 1.75 + ... + 0.25 pixels, as far as the bat's own flying home allows.
  printf("    pushed %d pixels\n", (int)BM_FIX_INT(a->x[bat] - x));
  TEST_ASSERT(a->x[bat] != x);

  // The dummy (in the same room) stands firm, but flashes.
  int dummy = actorOfClass("Dummy");
  TEST_ASSERT(dummy >= 0);
  x = a->x[dummy];
  TEST_ASSERT(bmHitsStrikeObject(&s_game, (uint8_t)dummy, 1, 0, a->x[dummy] - BM_FIX(10)));
  TEST_ASSERT(a->knock[dummy] == 0);
  frames(2, 0);
  TEST_ASSERT(spritesInBank(BM_HITS_FLASH_BANK) > 0 || a->sheet[dummy] < 0);
  frames(12, 0);
  TEST_ASSERT(a->x[dummy] == x);
  TEST_ASSERT(bm_script_errors == 0);
}

// structures.md step 4: the roles of the test level's structures (items.ms). Equipment goes only
// into its slot; what it adds to the stats is the engine's (stat_source), not its script's: it
// counts once after a save game is loaded, and a pushed pack with a changed record changes the
// total at once.
static void testEquipmentRoles(void)
{
  start();
  BmInventory *inv = &s_game.inventory;
  const uint32_t helm = keyOf("IronHelm"), sword = keyOf("ShortSword"), brand = keyOf("FlameBrand"), mail = keyOf("ChainMail");
  bmInventoryGive(inv, helm, 1);
  bmInventoryGive(inv, sword, 1);
  bmInventoryGive(inv, brand, 1);
  bmInventoryGive(inv, mail, 1);
  frames(2, 0);
  // Only into its own slot: the helm on the head (2), weapons in 0.
  TEST_ASSERT(!bmAttachmentsEquip(&s_game, 0, helm) && !bmAttachmentsEquip(&s_game, 3, helm));
  TEST_ASSERT(!bmAttachmentsEquip(&s_game, 2, sword) && inv->equipped[2] == 0 && inv->equipped[0] == 0);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 2, helm) && bmAttachmentsEquip(&s_game, 0, brand));
  // The stats: the helm's defence 3, the Flame Brand's attack 2; nothing in the base values.
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_DEFENCE) == 3 && bmInventoryStat(inv, BM_STAT_ATTACK) == 2);
  TEST_ASSERT(inv->statBase[BM_STAT_DEFENCE] == 0 && inv->statBase[BM_STAT_ATTACK] == 0);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 3, mail) && bmInventoryStat(inv, BM_STAT_DEFENCE) == 8);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, sword) && bmInventoryStat(inv, BM_STAT_ATTACK) == 0);
  // A save game: equipped again when loaded, counted once.
  memset(bmHalHostSaveMemory(), 0xFF, BM_HAL_SAVE_SIZE);
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  start();
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_DEFENCE) == 0);
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  TEST_ASSERT(inv->equipped[2] == helm && inv->equipped[3] == mail && bmInventoryStat(inv, BM_STAT_DEFENCE) == 8);
  // A pack whose Iron Helm gives 7 pushed in (as a hot reload): the total follows.
  BmDataTable table;
  const uint8_t *record;
  TEST_ASSERT(bmDataFindRecord(&s_pack, helm, &table, &record) && table.pRoles != NULL);
  uint16_t defence = (uint16_t)(table.pRoles[12 + 3 * 4] | table.pRoles[12 + 3 * 4 + 1] << 8);   // stat 3, Defence
  uint8_t *changed = malloc(s_size);
  memcpy(changed, s_data, s_size);
  size_t at = (size_t)(record - s_data) + defence;
  changed[at] = 7;
  changed[at + 1] = 0;
  BmPack old = s_pack;
  TEST_ASSERT(bmPackOpen(&s_pack, changed, s_size, false) == BM_PACK_OK);
  TEST_ASSERT(bmGameAfterLoad(&s_game) == BM_SUCCESS);
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_DEFENCE) == 12);
  s_pack = old;
  TEST_ASSERT(bmGameAfterLoad(&s_game) == BM_SUCCESS && bmInventoryStat(inv, BM_STAT_DEFENCE) == 8);
  free(changed);
  TEST_ASSERT(bm_script_errors == 0);
}

// first-items.md step 5: the abilities, from their pedestals in the hall behind the boss
// (assets/test_room/ability_hall.tmx). Each acts while owned (the role ability): Double Jump and
// Wall Jump are only stats the body step reads; Dash, Ground Pound and Second Wind attach their
// scripts in the ability slots. A save game brings them back.
static int slotOfClass(const char *cls)
{
  for (int s = BM_ATTACH_ABILITY_FIRST; s < BM_ATTACH_ABILITY_FIRST + BM_ATTACH_ABILITY_SLOTS; s++)
    if (s_game.attachments.classKey[s] == keyOf(cls)) return s;
  return -1;
}

static void touchAt(int32_t x, int32_t y)
{
  bmBodyPlace(&s_game.player, x, y);
  frames(4, 0);
}

static void testAbilities(void)
{
  startWith(s_data, s_size, "ability_hall");
  BmInventory *inv = &s_game.inventory;
  BmBody *b = &s_game.player;
  frames(30, 0);
  TEST_ASSERT(actorOfClass("Pedestal") >= 0);
  // Double Jump: a stat, no script.
  touchAt(56, 288);
  TEST_ASSERT(bmInventoryCount(inv, keyOf("DoubleJump")) == 1 && bmInventoryStat(inv, BM_STAT_AIR_JUMPS) == 1);
  TEST_ASSERT(s_game.attachments.classKey[BM_ATTACH_ABILITY_FIRST] == 0);
  frames(10, 0);
  TEST_ASSERT(b->onGround && b->airJumpsLeft == 1);
  // Second Wind: a blow that would end the player leaves 1 health, once.
  touchAt(88, 288);
  int wind = slotOfClass("AbilitySecondWind");
  TEST_ASSERT(wind == BM_ATTACH_ABILITY_FIRST);
  uint32_t deaths = s_game.deaths;
  s_game.hurtTimer = 0;
  TEST_ASSERT(bmGameStrikePlayer(&s_game, 100, b->x));
  TEST_ASSERT(s_game.health == 1 && s_game.deaths == deaths);
  s_game.hurtTimer = 0;
  TEST_ASSERT(bmGameStrikePlayer(&s_game, 100, b->x));
  TEST_ASSERT(s_game.deaths == deaths + 1);
  s_game.hurtTimer = 0;
  // Dash, on the ledge: L sends the player ahead.
  touchAt(152, 208);
  int dash = slotOfClass("AbilityDash");
  TEST_ASSERT(dash == BM_ATTACH_ABILITY_FIRST + 1 && bmInventoryStat(inv, BM_STAT_DASH) == 1);
  bmBodyPlace(b, 24, 288);   // the ledge's wall is at 112
  b->facing = 1;
  frames(20, 0);
  int32_t x0 = b->x;
  frames(1, BM_KEY_L);
  frames(11, 0);
  TEST_ASSERT(b->x - x0 >= BM_FIX(40));   // 12 frames at 3.5
  // Wall Jump, beyond the pit: a stat the body step reads.
  touchAt(360, 208);
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_WALL_SLIDE) == 1 && slotOfClass("AbilityWallJump") < 0);
  // Ground Pound, at the top of the shaft: Down and Jump in the air go down fast.
  touchAt(360, 48);
  int pound = slotOfClass("AbilityPound");
  TEST_ASSERT(pound == BM_ATTACH_ABILITY_FIRST + 2);
  bmBodyPlace(b, 464, 100);
  frames(2, 0);
  frames(1, BM_KEY_DOWN | BM_KEY_A);
  frames(2, BM_KEY_DOWN);
  TEST_ASSERT(b->vy == s_game.physics.maxFallSpeed && b->vx == 0 && !b->onGround);   // as fast as the room lets
  frames(60, 0);
  TEST_ASSERT(b->onGround && s_game.attachments.fields[pound * BM_SCRIPT_MAX_FIELDS + 1] == 1);   // Pounds
  // The save game: the abilities come back with their scripts.
  memset(bmHalHostSaveMemory(), 0xFF, BM_HAL_SAVE_SIZE);
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  start();
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_AIR_JUMPS) == 0 && slotOfClass("AbilityDash") < 0);
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  TEST_ASSERT(bmInventoryStat(inv, BM_STAT_AIR_JUMPS) == 1 && bmInventoryStat(inv, BM_STAT_WALL_SLIDE) == 1);
  TEST_ASSERT(slotOfClass("AbilitySecondWind") == wind && slotOfClass("AbilityDash") == dash && slotOfClass("AbilityPound") == pound);
  // An ability lost: its script and stats go, the others move up.
  TEST_ASSERT(bmInventoryTake(inv, keyOf("SecondWind"), 1));
  bmGameInventoryChanged(&s_game);
  TEST_ASSERT(slotOfClass("AbilitySecondWind") < 0 && slotOfClass("AbilityDash") == BM_ATTACH_ABILITY_FIRST);
  TEST_ASSERT(bm_script_errors == 0);
}

// first-items.md steps 3 and 4: the element rule (the Ember Cloak resists fire, the bat is weak
// to holy), Chain Mail slows the player, the Thorn Band strikes back, mana and its refill,
// items used through their behaviour scripts (UseRestore only when it helps, UseReturn back to
// the save point), the quick slots on L and Select, and the Items page putting them there.
static void testElementsArmourAndConsumables(void)
{
  startWith(s_data, s_size, "test_room_2");
  BmInventory *inv = &s_game.inventory;
  const uint32_t cloak = keyOf("EmberCloak"), mail = keyOf("ChainMail"), thorns = keyOf("ThornBand");
  const uint32_t potion = keyOf("Potion"), vial = keyOf("ManaVial"), feather = keyOf("ReturnFeather");
  frames(2, 0);

  // The element rule itself.
  TEST_ASSERT(bmHitsElement(4, 1u << 3, 3) == 6 && bmHitsElement(4, 16u << 1, 1) == 2);
  TEST_ASSERT(bmHitsElement(1, 16u << 1, 1) == 1 && bmHitsElement(5, 1u << 3, 1) == 5 && bmHitsElement(5, 0xFF, 0) == 5);
  // The bat's kind says it is weak to holy.
  int bat = actorOfClass("Bat");
  TEST_ASSERT(bat >= 0 && s_game.actors.affinity[bat] == (1u << 3));

  // The Ember Cloak: defence 2, and fire does half of what is left.
  bmInventoryGive(inv, cloak, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 3, cloak));
  TEST_ASSERT(s_game.resists[1] == 1 && bmInventoryStat(inv, BM_STAT_DEFENCE) == 2);
  int32_t before = s_game.health;
  TEST_ASSERT(bmGameStrikePlayerWith(&s_game, 8, s_game.player.x, 1, 0xFF));
  TEST_ASSERT(s_game.health == before - 3);   // (8 - 2) / 2
  s_game.hurtTimer = 0;
  before = s_game.health;
  TEST_ASSERT(bmGameStrikePlayerWith(&s_game, 8, s_game.player.x, 0, 0xFF));
  TEST_ASSERT(s_game.health == before - 6);
  s_game.hurtTimer = 0;

  // Chain Mail in its place: the cloak's resistance goes, the player runs 6 % slower.
  bmInventoryGive(inv, mail, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 3, mail));
  TEST_ASSERT(s_game.resists[1] == 0 && s_game.speedPercent == -6 && bmInventoryStat(inv, BM_STAT_DEFENCE) == 5);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 3, 0) && s_game.speedPercent == 0 && bmInventoryStat(inv, BM_STAT_DEFENCE) == 0);

  // The Thorn Band: the bat that strikes the player is struck for 2.
  bmInventoryGive(inv, thorns, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 4, thorns));
  int32_t batHealth = s_game.actors.health[bat];
  TEST_ASSERT(batHealth > 2);
  TEST_ASSERT(bmGameStrikePlayerWith(&s_game, 1, s_game.player.x, 0, (uint8_t)(bat + 1)));
  TEST_ASSERT(s_game.actors.health[bat] == batHealth - 2);
  // A hazard has no one to strike back.
  s_game.hurtTimer = 0;
  TEST_ASSERT(bmGameStrikePlayerWith(&s_game, 1, s_game.player.x, 0, 0xFF));
  TEST_ASSERT(s_game.actors.health[bat] == batHealth - 2);
  s_game.hurtTimer = 0;

  // Mana: full at the start, one back every 90 frames.
  TEST_ASSERT(s_game.mana == 20 && bmGameMaxMana(&s_game) == 20);
  s_game.mana = 5;
  uint32_t frame = s_game.frame;
  frames((int)(90 - frame % 90), 0);
  TEST_ASSERT(s_game.mana == 6);

  // Items used through their scripts: the vial restores mana and is used up; a potion at full
  // health is not taken.
  bmInventoryGive(inv, vial, 2);
  bmInventoryGive(inv, potion, 2);
  TEST_ASSERT(bmAttachmentsUse(&s_game, vial));
  TEST_ASSERT(s_game.mana == 20 && bmInventoryCount(inv, vial) == 1);
  s_game.health = (int16_t)bmGameMaxHealth(&s_game);
  TEST_ASSERT(bmAttachmentsUse(&s_game, potion));
  TEST_ASSERT(bmInventoryCount(inv, potion) == 2);
  TEST_ASSERT(!bmAttachmentsUse(&s_game, keyOf("LargePotion")));   // none owned
  frames(2, 0);
  TEST_ASSERT(s_game.attachments.classKey[BM_GAME_ITEM_SLOT] == 0);   // gone after its frame

  // Quick slots: L uses the first.
  s_game.health = 10;
  inv->quick[0] = potion;
  press(BM_KEY_L);
  TEST_ASSERT(s_game.health == 25 && bmInventoryCount(inv, potion) == 1);

  // The Return Feather: back to where the game started (no save point reached yet), in the
  // first room, from the quick slot on Select.
  bmInventoryGive(inv, feather, 1);
  inv->quick[1] = feather;
  press(BM_KEY_SELECT);
  frames(1, 0);
  TEST_ASSERT(s_game.teleTimer != 0);   // with the wave of a teleport
  frames(BM_TELEPORT_FRAMES, 0);
  TEST_ASSERT(bmInventoryCount(inv, feather) == 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);   // it started here
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) == s_game.savedX);

  // The Items page: R puts the chosen item on the second quick slot; it shows mana.
  inv->quick[0] = inv->quick[1] = 0;
  press(BM_KEY_START);
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Mana"));
  press(BM_KEY_R);
  TEST_ASSERT(inv->quick[1] != 0 && bmInventoryCount(inv, inv->quick[1]) > 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// attachable-scripts.md §6, first-items.md §9: objects made at run time. A Drop spawned with a
// potion falls to the floor and is taken on touch; its place is used again; a snapshot brings
// spawned objects back; the 8 places are a limit; leaving the room takes them away.
static void testADropFallsAndIsTaken(void)
{
  startWith(s_data, s_size, "test_room_2");
  BmActors *a = &s_game.actors;
  const uint32_t potion = keyOf("Potion");
  uint8_t room = a->count;
  TEST_ASSERT(a->roomCount == room);
  TEST_ASSERT(!bmActorsSpawn(&s_game, stringId("Nothing"), BM_FIX(300), BM_FIX(100), 0));
  TEST_ASSERT(bmActorsSpawn(&s_game, stringId("Drop"), BM_FIX(300), BM_FIX(100), potion));
  int drop = room;
  TEST_ASSERT(a->count == room + 1 && a->spawned[drop] && a->object[drop] == 0xFF && a->sheet[drop] >= 0);
  TEST_ASSERT((uint32_t)a->fields[drop * BM_SCRIPT_MAX_FIELDS] == potion);
  frames(60, 0);
  TEST_ASSERT(a->y[drop] == BM_FIX(160));   // on the floor
  // A snapshot with it comes back with it.
  static uint8_t snap[BM_LIVE_STATE_CAPACITY];
  BmStateBlock block = bmGameStateBlock(&s_game);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK && bmGameAfterLoad(&s_game) == BM_SUCCESS);
  TEST_ASSERT(a->count == room + 1 && a->spawned[drop] && !a->gone[drop] && a->object[drop] == 0xFF);
  // Touched: the player has it, it is gone.
  bmBodyPlace(&s_game.player, 300, 160);
  frames(3, 0);
  TEST_ASSERT(bmInventoryCount(&s_game.inventory, potion) == 1 && a->gone[drop] && a->hidden[drop]);
  // The HUD at the top left (health and mana), and a notice of what was taken at the bottom
  // left a couple of frames later, gone after two seconds.
  frames(3, 0);
  TEST_ASSERT(s_game.ui.shown && uiShows("HP") && uiShows("MP") && uiShows("Potion"));
  TEST_ASSERT(cellChar(&s_game.ui, 1, 18) == 'P');
  s_game.health = 7;
  frames(1, 0);
  TEST_ASSERT(cellChar(&s_game.ui, 11, 1) == '7' && cellChar(&s_game.ui, 10, 1) != '3');
  frames(BM_GAME_NOTICE_FRAMES, 0);
  TEST_ASSERT(!uiShows("Potion") && uiShows("HP"));
  // Its place is taken by the next one; the rest of the 8 fill up, then no more.
  TEST_ASSERT(bmActorsSpawn(&s_game, stringId("Drop"), BM_FIX(100), BM_FIX(100), potion) && a->count == room + 1 && !a->gone[drop]);
  while (a->count < BM_ACTORS_MAX) TEST_ASSERT(bmActorsSpawn(&s_game, stringId("Drop"), BM_FIX(400), BM_FIX(100), potion));
  TEST_ASSERT(!bmActorsSpawn(&s_game, stringId("Drop"), BM_FIX(400), BM_FIX(100), potion));
  // Through the west door and back: the room's objects only.
  bmBodyPlace(&s_game.player, 8, 160);
  frames(40, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0 && a->spawned[0] == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// The debug lobby: L + R + Start gives the Debug Mirror; used, it teleports the player there
// with the wave (the move at the middle, the player held still meanwhile); each of its doors
// leads into a room with a screen effect, which the room's property chooses; the teleporter
// takes the player back to the start.
static void testTheDebugLobby(void)
{
  start();
  frames(2, 0);
  const uint32_t mirror = keyOf("DebugMirror");
  // L + R + Start: the mirror, and no menu.
  frames(1, BM_KEY_L | BM_KEY_R);
  frames(1, BM_KEY_L | BM_KEY_R | BM_KEY_START);
  frames(1, 0);
  TEST_ASSERT(bmInventoryCount(&s_game.inventory, mirror) == 1 && s_game.screens.count == 0);
  // Used: a teleport; the room changes at its middle.
  TEST_ASSERT(bmAttachmentsUse(&s_game, mirror));
  TEST_ASSERT(s_game.teleTimer == BM_TELEPORT_FRAMES);
  frames(BM_TELEPORT_FRAMES / 2 - 1, BM_KEY_RIGHT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0 && bmEffectsWaveAt(&s_game, 40) >= 0);
  int32_t x = s_game.player.x;
  frames(2, BM_KEY_RIGHT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0 && BM_FIX_INT(s_game.player.x) == 40);
  frames(BM_TELEPORT_FRAMES / 2, 0);
  TEST_ASSERT(s_game.teleTimer == 0 && bmInventoryCount(&s_game.inventory, mirror) == 1);   // not used up
  (void)x;
  // The lobby's doors: a room per effect.
  static const char *const rooms[4][2] = {{"debug_wave", "wave"}, {"debug_spotlight", "spotlight"}, {"debug_pulse", "pulse"}, {"debug_gradient", "gradient"}};
  for (int i = 0; i < 4; i++)
  {
    TEST_ASSERT(bmGameStart(&s_game, &s_pack, rooms[i][0]) == BM_SUCCESS);
    frames(3, 0);
    TEST_ASSERT(s_game.fxKind == (uint8_t)bmEffectNamed(rooms[i][1]));
  }
  // The wave pulls the room left by 0 to 8 pixels, never more.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_wave") == BM_SUCCESS);
  int lo = 99, hi = -99;
  for (int f = 0; f < 90; f++)
  {
    frames(1, 0);
    for (int y = 0; y < 160; y += 7)
    {
      int v = bmEffectsWaveAt(&s_game, y);
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
  }
  TEST_ASSERT(lo == 0 && hi > 0 && hi <= BM_EFFECT_WAVE_MAX);
  // Back from the lobby: its teleporter.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_lobby") == BM_SUCCESS);
  frames(2, 0);
  bmBodyPlace(&s_game.player, 520, 160);
  frames(BM_TELEPORT_FRAMES + 4, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0 && BM_FIX_INT(s_game.player.x) == 40);
  TEST_ASSERT(bm_script_errors == 0);
}

// Commands (commands.h): directions then a button do the special move of the weapon in hand,
// or of a charm worn; the button does not also swing. Too slow, or the wrong weapon: nothing.
static void inputs(const uint16_t *keys, int n)
{
  for (int i = 0; i < n; i++) frames(1, keys[i]);
}

static void testCommands(void)
{
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_wave") == BM_SUCCESS);   // no door in the way
  frames(3, 0);
  BmInventory *inv = &s_game.inventory;
  const BmAttachments *t = &s_game.attachments;
  const uint32_t sword = keyOf("ShortSword"), dagger = keyOf("Dagger");
  bmInventoryGive(inv, sword, 1);
  bmInventoryGive(inv, dagger, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, sword));
  const int32_t *swing = &t->fields[0];   // WeaponSwing: Item, Swings, Strikes
  bmBodyPlace(&s_game.player, 60, 160);
  frames(10, 0);
  s_game.mana = 20;
  // Down, Down-Fwd, Fwd + B: the Short Sword's Lunge, 4 mana, a dash forward; no swing.
  const uint16_t lunge[] = { BM_KEY_DOWN, BM_KEY_DOWN | BM_KEY_RIGHT, BM_KEY_RIGHT, BM_KEY_RIGHT | BM_KEY_B };
  inputs(lunge, 4);
  TEST_ASSERT(s_game.cmdLast == keyOf("Lunge") && s_game.cmdDone == 1 && s_game.mana == 16);
  TEST_ASSERT(t->classKey[BM_GAME_COMMAND_SLOT] == keyOf("CommandStrike") && swing[1] == 0 && s_game.player.vx > BM_FIX(2.0));
  frames(40, 0);
  // Facing left it is mirrored: Down, Down-Back(left), Left.
  const uint16_t lungeLeft[] = { BM_KEY_DOWN, BM_KEY_DOWN | BM_KEY_LEFT, BM_KEY_LEFT, BM_KEY_LEFT | BM_KEY_B };
  inputs(lungeLeft, 4);
  TEST_ASSERT(s_game.cmdDone == 2 && s_game.player.vx < -BM_FIX(2.0));
  frames(40, 0);
  // Too slow (Down held, then the rest): the button swings as usual.
  frames(40, BM_KEY_DOWN);
  frames(1, BM_KEY_DOWN | BM_KEY_RIGHT);
  frames(1, BM_KEY_RIGHT);
  frames(1, BM_KEY_RIGHT | BM_KEY_B);
  TEST_ASSERT(s_game.cmdDone == 2 && swing[1] == 1);
  frames(40, 0);
  // The Dagger has another list: Fwd, Fwd + B; the sword's Lunge is not in it.
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, dagger));
  inputs(lunge, 4);
  TEST_ASSERT(s_game.cmdDone == 2);
  frames(40, 0);
  const uint16_t flurry[] = { BM_KEY_RIGHT, 0, BM_KEY_RIGHT | BM_KEY_B };
  inputs(flurry, 3);
  TEST_ASSERT(s_game.cmdLast == keyOf("Flurry") && s_game.cmdDone == 3);
  frames(40, 0);

  // The Moon Charm, worn, gives Gravity Flip whatever the weapon: Down, Up + A. The player
  // falls up and stands under the top of the room (its edge), upside down.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_lobby") == BM_SUCCESS);
  frames(3, 0);
  const uint32_t moon = keyOf("MoonCharm");
  bmInventoryGive(inv, moon, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 4, moon));
  bmBodyPlace(&s_game.player, 140, 160);   // between two arches: nothing over the way
  frames(10, 0);
  s_game.mana = 20;
  const uint16_t flip[] = { BM_KEY_DOWN, BM_KEY_UP, BM_KEY_UP | BM_KEY_A };
  inputs(flip, 3);
  TEST_ASSERT(s_game.cmdLast == keyOf("GravityFlip") && s_game.player.flipped);
  frames(60, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0);
  TEST_ASSERT(s_game.player.onGround && BM_FIX_INT(s_game.player.y) - s_game.playerParams.height == 0);
  // Walking on the ceiling, and jumping down from it.
  int32_t x = BM_FIX_INT(s_game.player.x);
  frames(10, BM_KEY_RIGHT);
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) > x + 10 && BM_FIX_INT(s_game.player.x) < 170 && s_game.player.onGround);
  frames(1, BM_KEY_A);
  frames(3, BM_KEY_A);
  TEST_ASSERT(!s_game.player.onGround && s_game.player.vy > 0);
  frames(60, 0);
  TEST_ASSERT(s_game.player.onGround && s_game.player.flipped);
  // Again: back on the floor.
  inputs(flip, 3);
  frames(60, 0);
  TEST_ASSERT(!s_game.player.flipped && s_game.player.onGround && BM_FIX_INT(s_game.player.y) == 160);
  // Without the mana it is the button's own (a jump).
  s_game.mana = 1;
  uint16_t done = s_game.cmdDone;
  inputs(flip, 3);
  TEST_ASSERT(s_game.cmdDone == done && !s_game.player.flipped);
  frames(60, 0);

  // The Crimson Charm: Up, Down + R draws blood from what is around and heals the player.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "test_room_2") == BM_SUCCESS);
  frames(3, 0);
  const uint32_t crimson = keyOf("CrimsonCharm");
  bmInventoryGive(inv, crimson, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 4, crimson));
  int dummy = actorOfClass("Dummy");
  TEST_ASSERT(dummy >= 0);
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) + 20, 160);   // behind the player: all around
  s_game.player.facing = 1;
  frames(10, 0);
  s_game.health = 10;
  s_game.mana = 20;
  const uint16_t absorb[] = { BM_KEY_UP, BM_KEY_DOWN, BM_KEY_DOWN | BM_KEY_R };
  inputs(absorb, 3);
  TEST_ASSERT(s_game.cmdLast == keyOf("BloodAbsorb") && s_game.mana == 14);
  frames(90, 0);
  printf("    blood absorb: health 10 -> %d\n", (int)s_game.health);
  TEST_ASSERT(s_game.health == 18);   // four pulses of 2 from the dummy
  TEST_ASSERT(bm_script_errors == 0);
}

// The keys of an input (`Down_DownFwd_Fwd_Attack`) facing right, a frame each; the button with
// the last direction. Returns how many.
static int keysOfInput(const char *input, uint16_t *keys)
{
  static const struct { const char *name; uint16_t keys; } tokens[] = {
    { "Up", BM_KEY_UP }, { "Down", BM_KEY_DOWN }, { "Fwd", BM_KEY_RIGHT }, { "Back", BM_KEY_LEFT },
    { "UpFwd", BM_KEY_UP | BM_KEY_RIGHT }, { "UpBack", BM_KEY_UP | BM_KEY_LEFT },
    { "DownFwd", BM_KEY_DOWN | BM_KEY_RIGHT }, { "DownBack", BM_KEY_DOWN | BM_KEY_LEFT },
    { "Attack", BM_KEY_B }, { "Jump", BM_KEY_A }, { "Spell", BM_KEY_R }, { "Dash", BM_KEY_L },
  };
  int n = 0;
  for (const char *p = input; *p;)
  {
    const char *end = strchr(p, '_');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++)
      if (strlen(tokens[i].name) == len && strncmp(p, tokens[i].name, len) == 0)
      {
        if (end == NULL && n > 0) keys[n - 1] |= tokens[i].keys;   // the button with the last direction
        else
        {
          if (n > 0 && keys[n - 1] == tokens[i].keys) keys[n++] = 0;   // the same twice: let go between
          keys[n++] = tokens[i].keys;
        }
      }
    p = end ? end + 1 : p + len;
  }
  return n;
}

// Every weapon has a list of commands (two at least), and each of them, done with its input,
// strikes the training dummy.
static void testEveryWeaponHasCommands(void)
{
  BmDataTable weapons, commands;
  TEST_ASSERT(bmDataFindTable(&s_pack, stringIdOf("Weapon"), &weapons) && bmDataFindTable(&s_pack, stringIdOf("Command"), &commands));
  int16_t fInput = -1, fWeapon = -1;
  for (uint16_t f = 0; f < commands.fieldCount; f++)
  {
    BmDataField field = bmDataTableField(&commands, f);
    const char *name = bmPackString(&s_pack, field.name);
    if (strcmp(name, "input") == 0) fInput = (int16_t)f;
    if (strcmp(name, "weapon") == 0) fWeapon = (int16_t)f;
  }
  TEST_ASSERT(fInput >= 0 && fWeapon >= 0);
  for (uint16_t w = 0; w < weapons.recordCount; w++)
  {
    uint32_t weapon = bmDataRecordKey(&weapons, w);
    int count = 0;
    for (uint16_t c = 0; c < commands.recordCount; c++)
    {
      uint32_t key = bmDataRecordKey(&commands, c);
      const uint8_t *record = bmDataRecord(&commands, key);
      BmDataField fw = bmDataTableField(&commands, (uint16_t)fWeapon), fi = bmDataTableField(&commands, (uint16_t)fInput);
      if ((uint32_t)bmDataRead(&commands, record, fw.offset, (BmDataType)fw.type) != weapon) continue;
      count++;
      const char *input = bmPackString(&s_pack, (uint16_t)bmDataRead(&commands, record, fi.offset, (BmDataType)fi.type));
      // In the second room, the dummy just ahead; the weapon in hand, mana full.
      TEST_ASSERT(bmGameStart(&s_game, &s_pack, "test_room_2") == BM_SUCCESS);
      frames(2, 0);
      bmInventoryGive(&s_game.inventory, weapon, 1);
      bmAttachmentsEquip(&s_game, 0, weapon);
      s_game.inventory.equipped[0] = weapon;   // also a weapon whose own swing is not written yet
      int dummy = actorOfClass("Dummy");
      TEST_ASSERT(dummy >= 0);
      const int32_t *hits = &s_game.actors.fields[dummy * BM_SCRIPT_MAX_FIELDS];
      bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) - 14, 160);
      s_game.player.facing = 1;
      frames(20, 0);
      s_game.mana = 20;
      int32_t before = hits[0];
      uint16_t keys[16];
      int n = keysOfInput(input, keys);
      for (int i = 0; i < n; i++) frames(1, keys[i]);
      frames(60, 0);
      bool done = s_game.cmdLast == key;
      printf("    %-16s %-40s %s, dummy struck %d times\n", bmPackString(&s_pack, bmDataRecordName(&weapons, w)), input, done ? "done" : "NOT DONE",
             (int)(hits[0] - before));
      TEST_ASSERT(done && hits[0] > before);
    }
    TEST_ASSERT(count >= 2);
  }
  TEST_ASSERT(bm_script_errors == 0);
}

// Aimed swings: Up + attack strikes above the head, Down + attack sweeps low on the ground and
// strikes below the feet in the air (the player bounces off what it strikes). A sword cuts in
// arcs (its frames' blade boxes), a lance thrusts straight (rectangles).
static int16_t swingAt(int bat, int32_t px, int32_t py, int32_t bx, int32_t by, uint16_t keys, bool air)
{
  BmActors *a = &s_game.actors;
  a->health[bat] = 10;   // enough for any strike here
  frames(40, 0);   // the last strike's invulnerability is over
  bmBodyPlace(&s_game.player, px, py);
  if (!air) frames(2, 0);
  s_game.player.facing = 1;
  for (int f = 0; f < 16; f++)
  {
    a->x[bat] = BM_FIX(bx);   // held in place while the swing comes
    a->y[bat] = BM_FIX(by);
    if (air && a->health[bat] == 10)
    {
      s_game.player.x = BM_FIX(px);   // and the player in the air above it, until it strikes
      s_game.player.y = BM_FIX(py);
      s_game.player.vy = 0;
    }
    frames(1, f == 0 ? (uint16_t)(keys | BM_KEY_B) : keys);
  }
  return (int16_t)(10 - a->health[bat]);
}

static void testAimedSwings(void)
{
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "test_room_2") == BM_SUCCESS);
  frames(2, 0);
  const uint32_t sword = keyOf("ShortSword");
  bmInventoryGive(&s_game.inventory, sword, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, sword));
  int bat = actorOfClass("Bat");
  TEST_ASSERT(bat >= 0);
  // The bat over the player's head (its box 8 to 16 px above it): a plain swing misses, with Up
  // it is struck.
  TEST_ASSERT(swingAt(bat, 200, 160, 200, 128, 0, false) == 0);
  TEST_ASSERT(swingAt(bat, 200, 160, 200, 128, BM_KEY_UP, false) == 3);
  TEST_ASSERT(s_game.hits.aimNext[0] == 0);   // the aim is used up by the swing
  // At head height ahead: a plain swing strikes it, the low sweep (Down) passes under it.
  TEST_ASSERT(swingAt(bat, 200, 160, 212, 138, 0, false) == 3);
  TEST_ASSERT(swingAt(bat, 200, 160, 212, 138, BM_KEY_DOWN, false) == 0);
  // At the feet ahead: the low sweep strikes it.
  TEST_ASSERT(swingAt(bat, 200, 160, 212, 160, BM_KEY_DOWN, false) == 3);
  // Below the player in the air: Down cuts it, and the player bounces up.
  TEST_ASSERT(swingAt(bat, 200, 100, 200, 116, BM_KEY_DOWN, true) == 3);
  TEST_ASSERT(!s_game.player.onGround && BM_FIX_INT(s_game.player.y) < 100);   // bounced up
  // The sword cuts in an arc: up, it also strikes ahead of the head and behind it, where a
  // thrust straight up does not reach.
  TEST_ASSERT(swingAt(bat, 200, 160, 212, 126, BM_KEY_UP, false) == 3);
  TEST_ASSERT(swingAt(bat, 200, 160, 191, 126, BM_KEY_UP, false) == 3);
  // The lance thrusts straight: above the head and below the feet, at its reach, but not ahead
  // of the head.
  const uint32_t lance = keyOf("Lance");
  bmInventoryGive(&s_game.inventory, lance, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, lance));
  TEST_ASSERT(swingAt(bat, 200, 160, 200, 112, BM_KEY_UP, false) == 4);   // attack 4, 20 px above the head
  TEST_ASSERT(swingAt(bat, 200, 160, 218, 126, BM_KEY_UP, false) == 0);
  TEST_ASSERT(swingAt(bat, 200, 100, 200, 124, BM_KEY_DOWN, true) == 4);
  TEST_ASSERT(!s_game.player.onGround && BM_FIX_INT(s_game.player.y) < 100);
  TEST_ASSERT(bm_script_errors == 0);
}

// Sounds: each enemy struck sounds its own (the bat's comes from its kind's record), and an item
// obtained has the pickup sound.
static const char *effectPlaying(void)
{
  const BmSoundPlayer *p = &s_game.sound.players[1];
  return p->playing ? bmPackString(&s_pack, p->nameId) : "";
}

static void testHitAndPickupSounds(void)
{
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "test_room_2") == BM_SUCCESS);
  frames(2, 0);
  const uint32_t sword = keyOf("ShortSword");
  bmInventoryGive(&s_game.inventory, sword, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, sword));
  int bat = actorOfClass("Bat"), dummy = actorOfClass("Dummy");
  TEST_ASSERT(bat >= 0 && dummy >= 0);
  // The bat: struck at head height ahead.
  bool batSound = false;
  s_game.actors.health[bat] = 10;
  bmBodyPlace(&s_game.player, 200, 160);
  frames(2, 0);
  s_game.player.facing = 1;
  for (int f = 0; f < 10; f++)
  {
    s_game.actors.x[bat] = BM_FIX(212);
    s_game.actors.y[bat] = BM_FIX(138);
    frames(1, f == 0 ? BM_KEY_B : 0);
    batSound |= strcmp(effectPlaying(), "hit_bat") == 0;
  }
  TEST_ASSERT(batSound && s_game.actors.health[bat] < 10);
  // The dummy: its knock.
  bool dummySound = false;
  frames(30, 0);
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[dummy]) - 14, 160);
  frames(30, 0);
  s_game.player.facing = 1;
  for (int f = 0; f < 10; f++)
  {
    frames(1, f == 0 ? BM_KEY_B : 0);
    dummySound |= strcmp(effectPlaying(), "hit_dummy") == 0;
  }
  TEST_ASSERT(dummySound);
  // An item obtained: the pickup sound (here a drop's).
  frames(30, 0);
  TEST_ASSERT(bmActorsSpawn(&s_game, stringId("Drop"), s_game.player.x, s_game.player.y - BM_FIX(8), keyOf("Potion")));
  bool pickup = false;
  for (int f = 0; f < 20 && !pickup; f++)
  {
    frames(1, 0);
    pickup = strcmp(effectPlaying(), "pickup") == 0;
  }
  TEST_ASSERT(pickup && bmInventoryCount(&s_game.inventory, keyOf("Potion")) >= 1);
  TEST_ASSERT(bm_script_errors == 0);
}

// While an attack holds the player (owner_lock), the controls do not move them but their speed
// is not cut off: on the ground it fades with friction, in the air the player steers, facing
// the way the swing goes. A weapon's record chooses otherwise (ground_move, air_move: the
// Dagger walks), and a script with lock_move.
static void testAttacksAndMovement(void)
{
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_wave") == BM_SUCCESS);
  frames(2, 0);
  const uint32_t sword = keyOf("ShortSword"), dagger = keyOf("Dagger");
  bmInventoryGive(&s_game.inventory, sword, 1);
  bmInventoryGive(&s_game.inventory, dagger, 1);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, sword));
  // Running, then a swing: no more running, the speed fades (not cut off at once).
  bmBodyPlace(&s_game.player, 40, 160);
  frames(5, 0);
  frames(20, BM_KEY_RIGHT);
  bm_fix run = s_game.player.vx;
  frames(1, BM_KEY_RIGHT | BM_KEY_B);
  TEST_ASSERT(s_game.lockTimer != 0 && s_game.player.vx > 0 && s_game.player.vx < run);
  frames(2, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.player.vx > 0 && s_game.player.vx < run);
  frames(4, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.lockTimer != 0 && s_game.player.vx == 0);
  // In the air: the player steers (back to the left here), still facing the swing's way.
  frames(30, 0);
  bmBodyPlace(&s_game.player, 40, 160);
  frames(5, 0);
  frames(20, BM_KEY_RIGHT);
  frames(1, BM_KEY_RIGHT | BM_KEY_A);
  frames(2, BM_KEY_RIGHT | BM_KEY_A);
  frames(1, BM_KEY_RIGHT | BM_KEY_A | BM_KEY_B);
  TEST_ASSERT(s_game.lockTimer != 0 && !s_game.player.onGround);
  bm_fix flying = s_game.player.vx;
  frames(8, BM_KEY_LEFT | BM_KEY_A);
  TEST_ASSERT(s_game.lockTimer != 0 && !s_game.player.onGround && s_game.player.vx <= flying - BM_FIX(1.5) && s_game.player.facing == 1);
  // The Dagger's record walks: running on through the stab.
  frames(60, 0);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, 0, dagger));
  bmBodyPlace(&s_game.player, 40, 160);
  frames(5, 0);
  frames(20, BM_KEY_RIGHT);
  run = s_game.player.vx;
  frames(1, BM_KEY_RIGHT | BM_KEY_B);
  frames(4, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.lockTimer != 0 && s_game.lockGround == BM_LOCK_WALK && s_game.player.vx == run);
  // The other choices: kept (no friction) and stopped at once.
  s_game.lockTimer = 10;
  s_game.lockGround = BM_LOCK_KEEP;
  frames(4, 0);
  TEST_ASSERT(s_game.player.vx == run);
  s_game.lockGround = BM_LOCK_STOP;
  frames(1, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.player.vx == 0);
  // A scene's lock (player_lock) in the air: no steering.
  frames(30, 0);
  frames(1, BM_KEY_A);
  frames(3, BM_KEY_A);
  s_game.lockTimer = 10;
  s_game.lockGround = BM_LOCK_SLIDE;
  s_game.lockAir = BM_LOCK_DRIFT;
  bm_fix vx = s_game.player.vx;
  frames(4, BM_KEY_RIGHT | BM_KEY_A);
  TEST_ASSERT(!s_game.player.onGround && s_game.player.vx <= vx);
  TEST_ASSERT(bm_script_errors == 0);
}

// Doors with a panel open only as the player goes through: touching one starts a walk through
// it (the room changes at its end, or at once with A, B or Start), and on the other side the
// door come through closes behind the player.
static int doorNamed(const char *name)
{
  for (uint16_t i = 0; i < s_game.objectCount; i++)
    if (s_game.objects[i].name != NULL && strcmp(s_game.objects[i].name, name) == 0) return i;
  return -1;
}

static void testDoorsOpenAsThePlayerWalksThrough(void)
{
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_lobby") == BM_SUCCESS);
  frames(2, 0);
  int door = doorNamed("to_debug_wave");
  TEST_ASSERT(door >= 0 && s_game.objectSheet[door] >= 0);   // its `panel`, door_panel
  const BmObjectView *o = &s_game.objects[door];
  TEST_ASSERT(s_game.objectDoorGround[door] == 160);   // the lobby's floor (row 10)
  // Close by: still shut.
  bmBodyPlace(&s_game.player, o->x - 10, 160);
  frames(2, 0);
  TEST_ASSERT(bmEffectsDoorOpening(&s_game, (uint16_t)door) == 0 && s_game.doorSeq == 0);
  // In it: a door in the back wall waits for Up; then it opens while the player stands, then
  // the next room.
  bmBodyPlace(&s_game.player, o->x + 16, 160);
  frames(10, 0);
  TEST_ASSERT(s_game.doorSeq == 0 && strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0);
  frames(1, BM_KEY_UP);
  TEST_ASSERT(s_game.doorSeq == door + 1);
  frames(5, BM_KEY_RIGHT);   // the buttons are not the game's meanwhile
  int32_t half = bmEffectsDoorOpening(&s_game, (uint16_t)door);
  TEST_ASSERT(half > 0 && half < 256 && BM_FIX_INT(s_game.player.x) == o->x + 16);
  TEST_ASSERT(bmEffectsDrawDoors(&s_game, 0, 128) >= 4);   // panels, the dark behind the open one
  frames(20, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_wave") == 0 && s_game.doorSeq == 0);
  // The west door come through closes behind the player.
  int back = doorNamed("exit_west");
  TEST_ASSERT(back >= 0 && s_game.doorArrive == back + 1 && bmEffectsDoorOpening(&s_game, (uint16_t)back) > 0);
  frames(20, 0);
  TEST_ASSERT(bmEffectsDoorOpening(&s_game, (uint16_t)back) == 0);
  // A side door: the player walks on through it, out of the room, as it swings open.
  bmBodyPlace(&s_game.player, 30, 160);
  frames(5, 0);
  frames(1, BM_KEY_LEFT);
  frames(10, BM_KEY_LEFT);
  TEST_ASSERT(s_game.doorSeq == back + 1 && strcmp(bmGameRoomId(&s_game), "debug_wave") == 0);
  int32_t x = BM_FIX_INT(s_game.player.x);
  frames(8, 0);   // let go: the walk goes on by itself
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) < x && bmEffectsDoorOpening(&s_game, (uint16_t)back) == 256);
  frames(20, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0);
  // Jumped into a side door: the player comes down first (the door still shut), then walks.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_wave") == BM_SUCCESS);
  frames(2, 0);
  bmBodyPlace(&s_game.player, 10, 120);   // in the door's box, high above the floor
  s_game.player.onGround = false;
  frames(2, 0);
  TEST_ASSERT(s_game.doorSeq == back + 1 && !s_game.player.onGround);
  int fell = 0;
  while (!s_game.player.onGround && fell < 60)
  {
    TEST_ASSERT(bmEffectsDoorOpening(&s_game, (uint16_t)back) == 0 && s_game.doorTimer == 0);
    frames(1, 0);
    fell++;
  }
  TEST_ASSERT(s_game.player.onGround && BM_FIX_INT(s_game.player.y) == 160 && s_game.doorSeq == back + 1);
  frames(4, 0);
  TEST_ASSERT(s_game.doorTimer > 0 && bmEffectsDoorOpening(&s_game, (uint16_t)back) > 0);
  frames(40, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0);
  // Skipped: B through at once.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_wave") == BM_SUCCESS);
  frames(2, 0);
  bmBodyPlace(&s_game.player, 10, 160);
  frames(2, 0);
  TEST_ASSERT(s_game.doorSeq != 0);
  frames(1, BM_KEY_B);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0 && s_game.doorSeq == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// A room's acoustics (`acoustics`, `echo`, ... : room-format.md §1) go to the sound engine as
// the room is entered, and go again when a room without them is. They are the sound effects'
// (their own echo memory): the music keeps its song's echo (no `echo_music` here).
static void testRoomAcoustics(void)
{
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_spotlight") == BM_SUCCESS);
  frames(2, 0);
  // The cave preset, its echo made 180 ms.
  TEST_ASSERT(s_game.sound.room.on && s_game.sound.room.echoDelay == 180 * BM_SOUND_RATE / 1000 && !s_game.sound.room.music);
  TEST_ASSERT(s_game.sound.room.feedback == 120 && s_game.sound.room.damping == 150 && s_game.sound.room.muffle == 0);
  TEST_ASSERT(s_game.sound.pFxEcho != NULL);
  TEST_ASSERT(s_game.sound.echoDelay == s_game.sound.songEchoDelay && s_game.sound.echoDamping == 0);
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_wave") == BM_SUCCESS);
  frames(2, 0);
  TEST_ASSERT(s_game.sound.room.on && s_game.sound.room.muffle == 200 && s_game.sound.muffle == 0);
  // The lobby has none: the castle tune's own echo again.
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "debug_lobby") == BM_SUCCESS);
  frames(2, 0);
  TEST_ASSERT(!s_game.sound.room.on && s_game.sound.muffle == 0 && s_game.sound.echoDelay == s_game.sound.songEchoDelay);
  TEST_ASSERT(s_game.sound.songEchoDelay != 0);
}

// A line of dialogue appears a letter a frame; A shows the rest at once, the next A closes
// the box. Drawn again after the layer was cleared (a menu, a state), as far as it had come.
static void testALineAppearsLetterByLetter(void)
{
  startWith(s_data, s_size, "boss_hall");
  frames(2, 0);
  bmGameSayLine(&s_game, keyOf("VesperWakes"));
  TEST_ASSERT(s_game.dialogue && s_game.dialogueShown == 0 && s_game.dialogueLength == 34);
  TEST_ASSERT(s_game.dialogueVoice != 0xFFFF && strcmp(bmPackString(&s_pack, s_game.dialogueVoice), "voice_vesper") == 0);
  frames(5, 0);
  TEST_ASSERT(s_game.dialogueShown == 5);
  // "Who w": the first five letters are there, the sixth not yet.
  TEST_ASSERT(cellChar(&s_game.ui, 7, 17) == 'W' && cellChar(&s_game.ui, 11, 17) == 'w' && cellChar(&s_game.ui, 12, 17) != 'a');
  TEST_ASSERT(s_game.sound.players[1].playing);   // the voice
  // A: all of it; again: gone.
  press(BM_KEY_A);
  TEST_ASSERT(s_game.dialogue && s_game.dialogueShown == s_game.dialogueLength && uiShows("moon?"));
  press(BM_KEY_A);
  TEST_ASSERT(s_game.dialogue == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// The first boss (first-items.md, ui.md, the user's request): the autopilot plays from the start
// through both rooms into the boss hall, presses A through the moon bat's words, defeats it,
// takes the Iron Helm it leaves and puts it on in the Equipment menu. The moon bat stays
// defeated: its `save` variable is in the save game.
static void testTheBossIsDefeated(void)
{
  start();
  BmAutopilot pilot = {0};
  const uint32_t helm = keyOf("IronHelm");
  int frame = 0, hall = -1, dialogues = 0, portraits = 0, voices = 0, fadedAt = -1, droneAt = -1, fightAt = -1, enragedAt = -1;
  int moonHits = 0, fanfareAt = -1;
  uint16_t lastLine = 0xFFFF;
  for (; frame < 6000 && s_game.inventory.equipped[2] != helm; frame++)
  {
    bmVideoWaitFrame();
    bmGameFrame(&s_game, bmAutopilotKeys(&pilot, &s_game));
    if (hall < 0 && strcmp(bmGameRoomId(&s_game), "boss_hall") == 0) hall = frame;
    // A line follows another in the same frame A is pressed: count the lines, not the boxes.
    if (s_game.dialogue && s_game.dialogueLine != lastLine)
    {
      dialogues++;
      // Each line has its speaker's portrait (a sprite in front of the box's left end), the
      // name right of it.
      uint8_t first = cellChar(&s_game.ui, 6, 16);
      if (s_game.dialoguePortrait != 0xFFFF && (first == 'V' || first == 'Y')) portraits++;
    }
    if (s_game.dialogue) lastLine = s_game.dialogueLine;
    // A line appears letter by letter, with the speaker's voice.
    if (s_game.dialogue && s_game.sound.players[1].playing && s_game.sound.players[1].nameId == s_game.dialogueVoice) voices++;
    // The music: the castle's tune fades away in the hall, then the moon bat's song grows.
    const BmSound *snd = &s_game.sound;
    const char *song = snd->players[0].playing ? bmPackString(&s_pack, snd->players[0].nameId) : NULL;
    if (hall >= 0 && fadedAt < 0 && song == NULL) fadedAt = frame;
    // Struck, the moon bat sounds its own; defeated, the fanfare plays.
    const char *effect = snd->players[1].playing ? bmPackString(&s_pack, snd->players[1].nameId) : NULL;
    if (effect != NULL && strcmp(effect, "hit_moon") == 0) moonHits++;   // frames it is heard
    if (fanfareAt < 0 && song != NULL && strcmp(song, "fanfare") == 0) fanfareAt = frame;
    if (song != NULL && strcmp(song, "vesper") == 0)
    {
      if (droneAt < 0 && snd->intensity == 0) droneAt = frame;
      if (fightAt < 0 && snd->intensity == 2) fightAt = frame;
      if (enragedAt < 0 && snd->intensity == 3) enragedAt = frame;
    }
  }
  printf("    boss hall at frame %d, helm on at frame %d, %d lines said, deaths %u, hurts %u\n", hall, frame, dialogues,
         (unsigned)s_game.deaths, (unsigned)s_game.hurts);
  // Vesper, the hero's answer, Vesper again, and her last words when defeated.
  TEST_ASSERT(hall > 0 && s_game.inventory.equipped[2] == helm && dialogues == 4 && portraits == 4);
  printf("    music: castle gone at %d, drone at %d, fight at %d, enraged at %d; %d frames with a voice\n", fadedAt, droneAt, fightAt, enragedAt, voices);
  TEST_ASSERT(hall < fadedAt && fadedAt < droneAt && droneAt < fightAt && fightAt < enragedAt && voices > 10);
  // Defeated: its song faded away, and the fanfare played (once: it does not loop).
  printf("    moon bat's hit sound heard in %d frames, fanfare at %d\n", moonHits, fanfareAt);
  TEST_ASSERT(moonHits > 0 && fanfareAt > enragedAt);
  const char *after = s_game.sound.players[0].playing ? bmPackString(&s_pack, s_game.sound.players[0].nameId) : NULL;
  TEST_ASSERT(after == NULL || strcmp(after, "fanfare") == 0);
  TEST_ASSERT(bmInventoryStat(&s_game.inventory, BM_STAT_DEFENCE) == 3);
  int boss = actorOfClass("MoonBat");
  TEST_ASSERT(boss >= 0 && s_game.actors.hidden[boss] && strcmp(stateOf(boss), "Gone") == 0);
  for (int i = 0; i < 40; i++)   // the autopilot closes the menus
  {
    bmVideoWaitFrame();
    bmGameFrame(&s_game, bmAutopilotKeys(&pilot, &s_game));
  }
  // The boss's bar is gone; the HUD stays.
  TEST_ASSERT(s_game.screens.count == 0 && !uiShows("Vesper") && uiShows("HP"));
  // A save game keeps it defeated (the Iron Helm on, too).
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  boss = actorOfClass("MoonBat");
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "boss_hall") == 0 && s_game.actors.hidden[boss] && strcmp(stateOf(boss), "Gone") == 0);
  TEST_ASSERT(s_game.inventory.equipped[2] == helm && bmInventoryStat(&s_game.inventory, BM_STAT_DEFENCE) == 3);
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

// The knight of the guard room (assets/test_room/scripts/knight.ms, west of the first room): its
// shield stops the Short Sword from the front; from behind the sword strikes, it flinches and
// turns; it turns to a player behind it only after a while; its lunge strikes a player close
// ahead.
static void testTheKnightsShield(void)
{
  start();
  const uint32_t sword = keyOf("ShortSword");
  bmBodyPlace(&s_game.player, 80, 160);
  frames(5, 0);
  TEST_ASSERT(s_game.inventory.equipped[0] == sword);
  bmBodyPlace(&s_game.player, 24, 160);
  frames(60, BM_KEY_LEFT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  BmActors *a = &s_game.actors;
  int knight = actorOfClass("Knight");
  TEST_ASSERT(knight >= 0 && a->health[knight] == 9 && a->facing[knight] == 1);
  int32_t kx = BM_FIX_INT(a->x[knight]);
  // From the front, the sword over the shield and the body behind it: blocked. (Its lunge held
  // back meanwhile: Rest, its third variable.)
  int32_t *kf = &a->fields[knight * BM_SCRIPT_MAX_FIELDS];   // Kind, Behind, Rest, Walking
  kf[2] = 40;
  bmBodyPlace(&s_game.player, kx + 16, 160);
  s_game.player.facing = -1;
  frames(2, 0);
  int32_t health = s_game.health;
  frames(1, BM_KEY_B);
  // A spark where the sword met the shield (bmGameSpark), for as long as its animation.
  int sparkAt = -1, sparkFrames = 0;
  for (int i = 0; i < 20; i++)
  {
    frames(1, 0);
    for (int k = 0; k < BM_GAME_EFFECTS; k++)
      if (s_game.effectAge[k] != 0 && s_game.effectSheet[k] == s_game.sparkSheet)
      {
        sparkAt = s_game.effectX[k];
        sparkFrames++;
      }
  }
  TEST_ASSERT(s_game.sparkSheet >= 0 && sparkAt >= kx + 3 && sparkAt <= kx + 11 && sparkFrames == 11);
  TEST_ASSERT(a->health[knight] == 9 && strcmp(stateOf(knight), "Flinching") != 0);
  // Its lunge at the player close ahead.
  frames(60, 0);
  TEST_ASSERT(s_game.health == health - 2);
  // Behind it: it does not turn at once ...
  frames(90, 0);
  kx = BM_FIX_INT(a->x[knight]);
  TEST_ASSERT(a->facing[knight] == 1);
  bmBodyPlace(&s_game.player, kx - 30, 160);   // the tip of the blade at its back
  s_game.player.facing = 1;
  frames(20, 0);
  TEST_ASSERT(a->facing[knight] == 1);
  // ... and the sword strikes its back: it flinches and turns.
  frames(1, BM_KEY_B);
  frames(8, 0);
  TEST_ASSERT(a->health[knight] == 6 && strcmp(stateOf(knight), "Flinching") == 0);
  frames(20, 0);
  TEST_ASSERT(a->facing[knight] == -1);
  // Left alone behind it, it turns after 40 frames.
  frames(60, 0);
  bmBodyPlace(&s_game.player, BM_FIX_INT(a->x[knight]) + 40, 160);
  frames(30, 0);
  TEST_ASSERT(a->facing[knight] == -1);
  frames(20, 0);
  TEST_ASSERT(a->facing[knight] == 1);
  TEST_ASSERT(bm_script_errors == 0);
}

// Through a door and straight back: the player can walk back through it at once.
static void testBackThroughADoorAtOnce(void)
{
  start();
  bmBodyPlace(&s_game.player, 24, 160);
  int f = 0;
  for (; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "test_room") != 0; f++) frames(1, BM_KEY_RIGHT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room") == 0);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
}

// Breakable walls and crates (room-format.md §5, assets/test_room/guard_room.tmx): the lower
// part of the pillar at the guard room's west end breaks to the Short Sword; it opens on the
// alcove with a heart behind it and stays open (the save game keeps it); the crate breaks and
// leaves its potion.
static void goToTheGuardRoom(void)
{
  bmBodyPlace(&s_game.player, 24, 160);
  for (int f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_LEFT);
  frames(20, 0);
}

static bool solidCell(int cx, int cy)
{
  uint8_t shape, material;
  bmCollisionCell(&s_game.collision, cx, cy, &shape, &material);
  return (shape & 15) != 0;
}

static void testBreakableWallsAndCrates(void)
{
  start();
  memset(bmHalHostSaveMemory(), 0xFF, BM_HAL_SAVE_SIZE);
  bmBodyPlace(&s_game.player, 80, 160);
  frames(5, 0);
  goToTheGuardRoom();
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  int wall = doorNamed("secret_wall");
  TEST_ASSERT(wall >= 0 && s_game.objects[wall].kind == BM_OBJECT_BREAKABLE && !(s_game.objectBreak[wall] & 0x80));
  TEST_ASSERT(solidCell(3, 7) && solidCell(3, 9) && solidCell(3, 6));
  // One that only a piercing hit breaks: the sword does not.
  s_game.objectBreak[wall] = BM_BREAK_PIERCE;
  bmBodyPlace(&s_game.player, 72, 160);
  s_game.player.facing = -1;
  frames(2, 0);
  frames(1, BM_KEY_B);
  frames(20, 0);
  TEST_ASSERT(!(s_game.objectBreak[wall] & 0x80) && solidCell(3, 8));
  // Any hit: broken. Its cells are empty, the pillar above stays; debris, a hole in the room.
  s_game.objectBreak[wall] = BM_BREAK_ANY;
  frames(1, BM_KEY_B);
  int debris = 0;
  for (int i = 0; i < 8; i++)
  {
    frames(1, 0);
    for (int k = 0; k < BM_GAME_EFFECTS; k++) debris += s_game.effectAge[k] != 0 && s_game.effectSheet[k] == s_game.debrisSheet;
  }
  TEST_ASSERT(s_game.objectBreak[wall] & 0x80);
  TEST_ASSERT(!solidCell(3, 7) && !solidCell(3, 8) && !solidCell(3, 9) && solidCell(3, 6));
  TEST_ASSERT(s_game.debrisSheet >= 0 && debris >= 3 && s_game.room.holeCount == 1);
  // Through it, to the heart behind.
  int heart = actorOfClass("Pickup");
  TEST_ASSERT(heart >= 0 && !s_game.actors.hidden[heart]);
  for (int f = 0; f < 60 && !s_game.actors.hidden[heart]; f++) frames(1, BM_KEY_LEFT);   // (the gatehouse's door is further on)
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) < 48 && s_game.actors.hidden[heart] && strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  // The crate: broken, its potion left behind.
  int crate = actorOfClass("Crate");
  TEST_ASSERT(crate >= 0 && !s_game.actors.hidden[crate] && actorOfClass("Drop") < 0);
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[crate]) - 14, 160);
  s_game.player.facing = 1;
  frames(2, 0);
  frames(1, BM_KEY_B);
  frames(10, 0);
  TEST_ASSERT(s_game.actors.hidden[crate] && actorOfClass("Drop") >= 0);
  // Kept: a save game, a new game from it, back to the guard room: still open, the crate gone.
  TEST_ASSERT(bmGameWriteSaveGame(&s_game));
  start();
  TEST_ASSERT(bmGameReadSaveGame(&s_game));
  frames(3, 0);
  if (strcmp(bmGameRoomId(&s_game), "test_room") != 0) TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  else goToTheGuardRoom();
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  wall = doorNamed("secret_wall");
  TEST_ASSERT((s_game.objectBreak[wall] & 0x80) && !solidCell(3, 8) && s_game.room.holeCount == 1);
  TEST_ASSERT(s_game.actors.hidden[actorOfClass("Crate")]);
  TEST_ASSERT(bm_script_errors == 0);
}

// The gatehouse (assets/test_room/puzzle_room.tmx, puzzle.h): the stone block on the step is
// pushed left (a cell a push), falls off the step's end, is pushed along the floor onto the
// pressure plate; the gate opens and stays open while the block is on it; over the block,
// through the gate, the key. The player alone on the plate opens it only while standing there.
static void testThePushBlockPuzzle(void)
{
  startWith(s_data, s_size, "puzzle_room");
  frames(5, 0);
  // Late in a game: much kept already (every item of the test level taken, its crates and
  // walls broken: 20 values), and the block's place must still be kept.
  for (uint32_t k = 0; k < 20; k++) TEST_ASSERT(bmActorsStorePut(&s_game, 0x7F000000u | (k << 8), 1));
  const BmPuzzle *p = &s_game.puzzle;
  const BmCollision *c = &s_game.collision;
  TEST_ASSERT(p->blockCount == 1 && p->gateCount == 1 && p->plateCount == 1 && p->plateGate[0] == 0 && p->sheet >= 0);
  TEST_ASSERT(p->blockX[0] == 320 && p->blockY[0] == 112 && p->gateLift[0] == 0);
  // The gate's cells; the block solid to the pixel (320 to 351), not to the cell.
  TEST_ASSERT(solidCell(4, 8) && bmCollisionSolidSides(c, 320, 120) && bmCollisionSolidSides(c, 351, 140) && !bmCollisionSolidSides(c, 352, 120));
  TEST_ASSERT(bmCollisionFloor(c, 330, 100, 150, false) == 112);   // its top is a floor
  // The player alone on the plate: open while there, shut again when gone.
  bmBodyPlace(&s_game.player, 176, 160);
  frames(30, 0);
  TEST_ASSERT(p->pressed == 1 && p->gateLift[0] == 48 && !solidCell(4, 8));
  bmBodyPlace(&s_game.player, 260, 160);
  frames(30, 0);
  TEST_ASSERT(p->pressed == 0 && p->gateLift[0] == 0 && solidCell(4, 8));
  // A moment against it does not move it; held, it moves smoothly (at most a pixel a frame,
  // 3/4 of one on the whole), the player leaning into it with the push animation.
  bmBodyPlace(&s_game.player, 362, 144);
  frames(6, BM_KEY_LEFT);
  TEST_ASSERT(p->blockX[0] == 320 && p->pushing == -1 && strcmp(effectPlaying(), "drag") != 0);   // not sliding yet: quiet
  frames(10, BM_KEY_LEFT);
  int x0 = p->blockX[0], steps = 0, dragging = 0, restarts = 0;
  uint32_t tick = s_game.sound.players[1].tick;
  for (int i = 0; i < 40; i++)
  {
    int before = p->blockX[0];
    frames(1, BM_KEY_LEFT);
    steps += before - p->blockX[0] > 1;
    dragging += strcmp(effectPlaying(), "drag") == 0;   // the stone dragging along, all the while
    restarts += s_game.sound.players[1].tick < tick;    // one held noise, not started again and again
    tick = s_game.sound.players[1].tick;
  }
  TEST_ASSERT(steps == 0 && x0 - p->blockX[0] >= 28 && x0 - p->blockX[0] <= 32 && p->blockY[0] == 112);
  TEST_ASSERT(dragging == 40 && restarts == 0);
  TEST_ASSERT(p->pushing == -1 && s_game.playerAnim.anim == (uint8_t)s_game.playerAnims[BM_PLAYER_ANIM_PUSH]);
  int32_t px = BM_FIX_INT(s_game.player.x);
  TEST_ASSERT(px - s_game.playerParams.halfWidth - (p->blockX[0] + 32) <= 1);   // right against it
  // Let go: it stays.
  int x1 = p->blockX[0];
  frames(20, 0);
  TEST_ASSERT(p->blockX[0] == x1 && p->pushing == 0 && strcmp(effectPlaying(), "drag") != 0);   // let go: the sound stops
  // From the left as well: the player goes along with it, right against it, and the camera
  // that follows them moves a pixel at a time, as from the right (not in jerks of 2).
  for (int dir = 1; dir >= -1; dir -= 2)
  {
    bmBodyPlace(&s_game.player, dir > 0 ? p->blockX[0] - 8 : p->blockX[0] + 40, 144);
    frames(20, 0);
    int xs = p->blockX[0], jumps = 0, apart = 0;
    int32_t cam = s_game.room.cameraX;
    for (int i = 0; i < 60; i++)
    {
      frames(1, dir > 0 ? BM_KEY_RIGHT : BM_KEY_LEFT);
      jumps += abs(s_game.room.cameraX - cam) > 1;
      cam = s_game.room.cameraX;
      int32_t qx = BM_FIX_INT(s_game.player.x), hw = s_game.playerParams.halfWidth;
      if (i >= 15) apart += dir > 0 ? qx + hw != p->blockX[0] : qx - hw != p->blockX[0] + 32;
    }
    TEST_ASSERT(dir * (p->blockX[0] - xs) >= 30 && jumps == 0 && apart == 0);
  }
  // Off the step's end (x 256): it falls, faster and faster, onto the floor.
  bmBodyPlace(&s_game.player, p->blockX[0] + 40, 144);
  frames(10, 0);
  int f = 0;
  for (; f < 600 && p->blockY[0] == 112; f++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(p->blockX[0] + 32 <= 256 && p->blockX[0] >= 220);
  int fallFrames = 0;
  for (; fallFrames < 60 && p->blockY[0] != 128; fallFrames++) frames(1, 0);
  TEST_ASSERT(p->blockY[0] == 128 && fallFrames >= 3 && fallFrames <= 10 && p->blockFall[0] == 0);
  TEST_ASSERT(strcmp(effectPlaying(), "thud") == 0);   // landed
  TEST_ASSERT(bmCollisionSolidSides(c, p->blockX[0] + 4, 140) && !bmCollisionSolidSides(c, p->blockX[0] + 4, 120));
  // Along the floor onto the plate.
  for (f = 0; f < 600 && p->pressed == 0; f++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(p->pressed == 1);
  frames(30, 0);
  TEST_ASSERT(p->gateLift[0] == 48 && !solidCell(4, 8));
  // Over it and through the gate: the key.
  int key = actorOfClass("Pickup");
  TEST_ASSERT(key >= 0 && !s_game.actors.hidden[key]);
  frames(1, BM_KEY_LEFT | BM_KEY_A);
  frames(20, BM_KEY_LEFT | BM_KEY_A);
  frames(90, BM_KEY_LEFT);
  TEST_ASSERT(s_game.actors.hidden[key] && BM_FIX_INT(s_game.player.x) < 64 && p->gateLift[0] == 48);
  // Entered again, the block is where it was left (on the plate), and the gate it holds open
  // is open from the start.
  int leftX = p->blockX[0], leftY = p->blockY[0];
  bmBodyPlace(&s_game.player, 470, 160);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_RIGHT);
  frames(30, 0);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "puzzle_room") != 0; f++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "puzzle_room") == 0 && p->blockX[0] == leftX && p->blockY[0] == leftY);
  TEST_ASSERT(p->gateLift[0] == 48 && p->pressed == 1);
  // And through the west door into the chasm (the grapple's room) and back: still there.
  bmBodyPlace(&s_game.player, 40, 160);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "chasm_room") != 0; f++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "chasm_room") == 0);
  frames(30, 0);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "puzzle_room") != 0; f++) frames(1, BM_KEY_RIGHT);
  frames(5, 0);
  printf("    back from the chasm: block at %d,%d (left at %d,%d), gate %d\n", p->blockX[0], p->blockY[0], leftX, leftY, p->gateLift[0]);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "puzzle_room") == 0 && p->blockX[0] == leftX && p->blockY[0] == leftY);
  TEST_ASSERT(p->gateLift[0] == 48 && p->pressed == 1);
  // A save game keeps it too (the store is saved): a new game from the save has it there.
  uint32_t blockKey = ((s_game.roomIndex & 0xFF) << 24) | ((uint32_t)s_game.objects[p->blockObject[0]].persistentId << 8) | 0xFE;
  bool found = false;
  TEST_ASSERT(bmActorsStoreGet(&s_game, blockKey, &found) == (int32_t)((uint16_t)leftX | (uint32_t)leftY << 16) && found);
  // Left where a bat starts (as if it had been pushed there): the bat starts on top of it.
  bmBodyPlace(&s_game.player, 470, 160);
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_RIGHT);
  frames(30, 0);
  TEST_ASSERT(bmActorsStorePut(&s_game, blockKey, (int32_t)(364 | 40u << 16)));
  for (f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "puzzle_room") != 0; f++) frames(1, BM_KEY_LEFT);
  int bat = -1;
  for (int i = 0; i < s_game.actors.count; i++)
    if (s_game.actors.object[i] != 0xFF && strcmp(s_game.objects[s_game.actors.object[i]].name, "bat") == 0) bat = i;
  TEST_ASSERT(bat >= 0 && BM_FIX_INT(s_game.actors.homeY[bat]) == 40);   // its place (380, 56) was inside the block (364..396, 40..72)
  TEST_ASSERT(bm_script_errors == 0);
}

// layered-sprites.md steps 1 and 2: sheets go into sprite memory room by room, palette banks
// are given out per sheet and palette. In the gatehouse a bat and a red Fast Bat use one
// sheet and two banks (the red one holding the swapped colours); the knight's sheet is not
// loaded there, in the guard room it is and the bats' banks are free again.
static int sheetOf(const char *name)
{
  for (uint8_t k = 0; k < s_game.sheetCount; k++)
  {
    const char *n = bmPackString(&s_pack, s_game.sheets[k].nameId);
    if (n != NULL && strcmp(n, name) == 0) return k;
  }
  return -1;
}

static int banksOf(int sheet, int palette)
{
  int n = 0;
  for (int b = 0; b < BM_GAME_BANKS; b++) n += s_game.bankSheet[b] == sheet && (palette < 0 || s_game.bankPalette[b] == palette);
  return n;
}

static void testSheetsAndPalettesPerRoom(void)
{
  startWith(s_data, s_size, "puzzle_room");
  frames(3, 0);
  const BmActors *a = &s_game.actors;
  int bat = sheetOf("bat"), knight = sheetOf("knight");
  TEST_ASSERT(bat >= 0 && knight >= 0 && s_game.sheets[bat].paletteCount == 2);
  int bats = 0, red = -1;
  for (int i = 0; i < a->count; i++)
    if (a->sheet[i] == bat)
    {
      bats++;
      if (a->palette[i] == 1) red = i;
    }
  TEST_ASSERT(bats == 2 && red >= 0);
  TEST_ASSERT(banksOf(bat, 0) == 1 && banksOf(bat, 1) == 1 && banksOf(bat, -1) == 2);
  // The red bank has the swapped body colour (#b02838 as BGR555).
  int bank = -1;
  for (int b = 0; b < BM_GAME_BANKS; b++)
    if (s_game.bankSheet[b] == bat && s_game.bankPalette[b] == 1) bank = b;
  const BmPpuState *ppu = bmHalHostGetPpu();
  bool swapped = false;
  for (int c = 1; c < 16; c++) swapped |= ppu->palette[256 + bank * 16 + c] == (21 | 5 << 5 | 7 << 10);
  TEST_ASSERT(swapped);
  TEST_ASSERT(s_game.sheetTile[knight] == BM_GAME_NO_TILES && s_game.sheetTile[bat] != BM_GAME_NO_TILES);
  // Into the guard room: the knight's sheet is loaded; the bats' stay (a room change unpacks
  // only what is new).
  bmBodyPlace(&s_game.player, 470, 160);
  for (int f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_RIGHT);
  frames(5, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  TEST_ASSERT(s_game.sheetTile[knight] != BM_GAME_NO_TILES && banksOf(knight, 0) == 1 && banksOf(bat, -1) == 2);
  TEST_ASSERT(s_game.spriteMisses == 0);
  // A room that needs more than is left: everything but the player's is forgotten first. (The
  // knight's sheet as if not loaded, sprite memory as if nearly full; to the first room and back.)
  for (int b = 0; b < BM_GAME_BANKS; b++)
    if (s_game.bankSheet[b] == knight) s_game.bankSheet[b] = 0xFF;
  s_game.sheetTile[knight] = BM_GAME_NO_TILES;
  s_game.tileNext = BM_GAME_OBJ_TILES - 8;
  bmBodyPlace(&s_game.player, 488, 128);
  for (int f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "test_room") != 0; f++) frames(1, BM_KEY_RIGHT);
  frames(30, 0);
  bmBodyPlace(&s_game.player, 24, 160);
  for (int f = 0; f < 120 && strcmp(bmGameRoomId(&s_game), "guard_room") != 0; f++) frames(1, BM_KEY_LEFT);
  frames(5, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "guard_room") == 0);
  // The bat sheet is back for the Iron Bat (red, with its armour), the plain one not.
  TEST_ASSERT(s_game.sheetTile[knight] != BM_GAME_NO_TILES && banksOf(bat, 0) == 0 && banksOf(bat, 1) == 1 && s_game.tileNext < BM_GAME_OBJ_TILES - 200);
  // The player is the figure "player" (legs and torso): both sheets stay through all this, in
  // banks 0 and 1, and both are drawn, the torso in front.
  int legs = sheetOf("player_legs"), torso = sheetOf("player_torso");
  TEST_ASSERT(s_game.playerFigure.layerCount == 2 && s_game.playerSheet == legs && legs >= 0 && torso >= 0);
  TEST_ASSERT(s_game.bankSheet[0] == legs && s_game.bankSheet[1] == torso);
  TEST_ASSERT(s_game.sheetTile[legs] != BM_GAME_NO_TILES && s_game.sheetTile[torso] != BM_GAME_NO_TILES);
  TEST_ASSERT((ppu->oam[2] >> 12) == 1 && (ppu->oam[4 * 1 + 2] >> 12) == 0);
  // set_palette and flash from a script (the wind pack's Painter).
  if (s_windData != NULL)
  {
    startWith(s_windData, s_windSize, "test_room_2");
    int p = spawnAs("Painter", 120, 160, BM_ACTORS_ROOM);
    TEST_ASSERT(p >= 0 && s_game.actors.palette[p] == 1 && s_game.actors.flash[p] == 5);
    frames(3, 0);
    TEST_ASSERT(s_game.actors.flash[p] == 2);
    frames(3, 0);
    TEST_ASSERT(s_game.actors.flash[p] == 0);
  }
  TEST_ASSERT(bm_script_errors == 0);
}

// A kind as a record (layered-sprites.md §5): the Guard Room's iron_bat has no class, only
// kind = IronBat; the record names the class (Bat), its palette (red) and an extra layer (the
// armour, drawn in step with the body at its point `body`), and the bat reads its numbers from
// it.
static void testIronBat(void)
{
  startWith(s_data, s_size, "guard_room");
  frames(3, 0);
  const BmActors *a = &s_game.actors;
  int iron = -1;
  for (int i = 0; i < a->count; i++)
    if (a->kind[i] != 0) iron = i;
  TEST_ASSERT(iron >= 0 && (int32_t)a->classIndex[iron] == bmActorsClassNamed(&s_game, "Bat"));
  int bat = sheetOf("bat"), armour = sheetOf("bat_armour");
  TEST_ASSERT(a->sheet[iron] == bat && a->palette[iron] == 1 && a->extraSheet[iron] == armour && armour >= 0);
  TEST_ASSERT(a->health[iron] == 12);   // IronBat's health, not the Bat's 4
  // Drawn: the armour first (in front), at the body point: (0, -5) from the bat's anchor, the
  // armour's anchor at its own (8, 8) of 16 x 16.
  frames(1, 0);
  const BmPpuState *ppu = bmHalHostGetPpu();
  int armourOam = -1, batOam = -1;
  for (int k = 0; k < 48; k++)
  {
    uint16_t attr2 = ppu->oam[k * 4 + 2];
    uint16_t tile = attr2 & 0x3FF;
    if (s_game.sheetTile[armour] != BM_GAME_NO_TILES && tile >= s_game.sheetTile[armour] && tile < s_game.sheetTile[armour] + s_game.sheets[armour].tileCount && armourOam < 0) armourOam = k;
    if (tile >= s_game.sheetTile[bat] && tile < s_game.sheetTile[bat] + s_game.sheets[bat].tileCount && s_game.bankSheet[attr2 >> 12] == bat && s_game.bankPalette[attr2 >> 12] == 1 && batOam < 0) batOam = k;
  }
  TEST_ASSERT(armourOam >= 0 && batOam > armourOam);
  int ax = ppu->oam[armourOam * 4 + 1] & 0x1FF, ay = ppu->oam[armourOam * 4] & 0xFF;
  int bx = ppu->oam[batOam * 4 + 1] & 0x1FF, by = ppu->oam[batOam * 4] & 0xFF;
  // The armour's top: its anchor (the bat's anchor + (0, -5)) - 8; the bat's: its anchor - 12.
  TEST_ASSERT(ax == bx && ay - by == -1);
  TEST_ASSERT(bm_script_errors == 0);
}

// Equipment that shows (layered-sprites.md §5): the chain mail names the palette `steel`; while
// it is worn the torso (which has the coat) is drawn in it, the legs (no coat: no such palette)
// in their own; taken off, back to the default.
static void testEquipmentShows(void)
{
  startWith(s_data, s_size, NULL);
  frames(3, 0);
  int torso = sheetOf("player_torso"), legs = sheetOf("player_legs");
  int32_t steel = bmSpriteSheetPalette(&s_game.sheets[torso], s_game.pPack, "steel");
  TEST_ASSERT(steel > 0 && bmSpriteSheetPalette(&s_game.sheets[legs], s_game.pPack, "steel") < 0);
  uint32_t mail = keyOf("ChainMail");
  TEST_ASSERT(bmInventoryGive(&s_game.inventory, mail, 1) == 1);
  uint8_t slot = 0;
  BmDataTable table;
  const uint8_t *record;
  TEST_ASSERT(bmDataFindRecord(s_game.pPack, mail, &table, &record));
  slot = bmDataRecordSlot(&table, record);
  TEST_ASSERT(bmAttachmentsEquip(&s_game, slot, mail));
  frames(2, 0);   // the sprites of a frame reach the screen with the next
  TEST_ASSERT(banksOf(torso, steel) == 1 && banksOf(legs, -1) == 1);
  const BmPpuState *ppu = bmHalHostGetPpu();
  bool torsoSteel = false;
  for (int k = 0; k < 12; k++)
  {
    uint16_t attr2 = ppu->oam[k * 4 + 2], b = attr2 >> 12;
    torsoSteel |= s_game.bankSheet[b] == torso && s_game.bankPalette[b] == steel;
  }
  TEST_ASSERT(torsoSteel);
  bmAttachmentsEquip(&s_game, slot, 0);
  frames(2, 0);
  torsoSteel = false;
  for (int k = 0; k < 12; k++)
  {
    uint16_t b = ppu->oam[k * 4 + 2] >> 12;
    torsoSteel |= s_game.bankSheet[b] == torso && s_game.bankPalette[b] == steel;
  }
  TEST_ASSERT(!torsoSteel && s_game.equipPalette == 0xFFFF);
  TEST_ASSERT(bm_script_errors == 0);
}

// The lock (layered-sprites.md step 6; the wind pack's LockOn, Circler and Spark): Select locks
// on to a bat flying a circle; the marker sits on it, the player faces it and the torso turns
// after it through the aiming directions while the legs stand or run; a snapshot taken
// mid-turn goes on the same; a shot from the hand strikes it; Select again lets go. Handles
// go stale when the object is gone.
static void testTheLock(void)
{
  if (s_windData == NULL) return;
  startWith(s_windData, s_windSize, NULL);   // the first room: no wind
  frames(3, 0);
  BmGame *g = &s_game;
  bmBodyPlace(&g->player, 120, 160);
  frames(2, 0);
  // Round the player's middle: it passes over, behind, under and in front.
  int bat = spawnAs("Circler", 120, 160 - 24, BM_ACTORS_ROOM);
  TEST_ASSERT(bat >= 0);
  TEST_ASSERT(bmAttachmentsAttach(g, 3, stringId("LockOn"), 0));
  frames(2, 0);
  frames(1, BM_KEY_SELECT);
  frames(1, 0);
  const BmFigureLayerState *l = &g->playerLayers[1];
  TEST_ASSERT(bmActorsHandleIndex(g, g->markerTarget) == bat && bmActorsHandleIndex(g, l->track) == bat && l->speed == 6);
  TEST_ASSERT(l->flags & BM_LAYER_PLAYED);
  // Round and round: the torso through the directions, facing either way.
  const BmSpriteSheet *ts = &g->sheets[g->playerLayerSheet[1]];
  int32_t aim = bmSpriteSheetAnim(ts, g->pPack, "aim");
  bool seen[9] = { false }, facedLeft = false, facedRight = false;
  for (int f = 0; f < 140; f++)
  {
    frames(1, 0);
    TEST_ASSERT(l->anim.anim == aim && bmActorsHandleIndex(g, l->track) == bat);
    seen[bmSpriteSheetDirectionStep(ts, (uint8_t)aim, bmAngleOfDrawing(l->angle, g->player.facing < 0, false))] = true;
    facedLeft |= g->player.facing < 0;
    facedRight |= g->player.facing > 0;
  }
  int directions = 0;
  for (int k = 0; k < 9; k++) directions += seen[k];
  printf("    %d of 9 directions, faced left %d, right %d\n", directions, facedLeft, facedRight);
  TEST_ASSERT(directions >= 7 && facedLeft && facedRight);
  // The legs run while the torso aims.
  frames(8, BM_KEY_LEFT);
  TEST_ASSERT(g->playerAnim.anim == g->playerAnims[BM_PLAYER_ANIM_RUN] && l->anim.anim == aim);
  frames(4, 0);
  // A snapshot mid-turn goes on the same.
  static uint8_t snap[BM_LIVE_STATE_CAPACITY], a1[BM_LIVE_STATE_CAPACITY], a2[BM_LIVE_STATE_CAPACITY];
  BmStateBlock block = bmGameStateBlock(g);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  printf("    snapshot locked on: %u bytes\n", (unsigned)size);
  frames(30, 0);
  uint32_t n1 = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, a1, sizeof(a1));
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK && bmGameAfterLoad(g) == BM_SUCCESS);
  frames(30, 0);
  uint32_t n2 = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, a2, sizeof(a2));
  {
    uint32_t at = 16;
    for (uint16_t fi = 0; fi < block.fieldCount; fi++)
    {
      const BmStateField *fd = &block.pFields[fi];
      if (!(fd->flags & BM_STATE_SNAPSHOT) && !(fd->flags & (BM_STATE_SAVE | BM_STATE_LINK))) continue;
      uint32_t n = bmStateTypeSize(fd->type) * fd->count;
      for (uint32_t k = at; k < at + n && k < n1; k++)
        if (a1[k] != a2[k]) { printf("field %s differs (at %u)\n", fd->name, k); break; }
      at += n;
    }
  }
  TEST_ASSERT(n1 == n2 && memcmp(a1, a2, n1) == 0);
  // Shots from the hand until one strikes.
  int32_t health = s_game.actors.health[bat];
  for (int shot = 0; shot < 8 && s_game.actors.health[bat] == health; shot++)
  {
    frames(1, BM_KEY_B);
    for (int f = 0; f < 40 && s_game.actors.health[bat] == health; f++) frames(1, 0);
  }
  TEST_ASSERT(s_game.actors.health[bat] < health);
  // Select lets go: the torso follows the legs again, no marker.
  frames(1, BM_KEY_SELECT);
  frames(1, 0);
  TEST_ASSERT(g->markerTarget == 0 && !(l->flags & BM_LAYER_PLAYED));
  // Locked again, then the bat is gone: the handle is stale, the lock and the marker end.
  frames(1, BM_KEY_SELECT);
  frames(1, 0);
  uint32_t handle = g->markerTarget;
  TEST_ASSERT(bmActorsHandleIndex(g, handle) == bat);
  bmActorsDestroy(g, (uint8_t)bat);
  TEST_ASSERT(bmActorsHandleIndex(g, handle) < 0);
  frames(2, 0);
  TEST_ASSERT(g->markerTarget == 0 && l->track == 0 && !(l->flags & BM_LAYER_PLAYED));
  TEST_ASSERT(bm_script_errors == 0);
}

// Aiming (layered-sprites.md step 4; the wind pack's Aimer and Spark): an attachment plays the
// torso's direction set and turns it toward a point by 8 a frame; the torso shows the step
// nearest to its angle while the legs go on with their own animation; the hand point moves
// with it; a shot from the hand flies along the hand's angle; a snapshot taken mid-turn goes
// on the same; layer_hide, layer_show and layer_palette.
static void testAimingTheTorso(void)
{
  if (s_windData == NULL) return;
  startWith(s_windData, s_windSize, "test_room_2");
  frames(3, 0);
  BmGame *g = &s_game;
  int8_t torso = bmGamePlayerLayer(g, stringId("torso"));
  TEST_ASSERT(torso == 1 && g->playerFigure.layerCount == 2);
  TEST_ASSERT(bmAttachmentsAttach(g, 3, stringId("Aimer"), 0));
  const BmFigureLayerState *l = &g->playerLayers[1];
  uint8_t goal = bmAngleOf(64, -80);
  TEST_ASSERT((l->flags & BM_LAYER_PLAYED) && l->angle == 0 && l->goal == goal && l->speed == 8);
  frames(2, 0);
  TEST_ASSERT(l->angle == 256 - 16);   // two frames, 8 each, the shorter way (up)
  // A snapshot mid-turn goes on the same.
  static uint8_t snap[BM_LIVE_STATE_CAPACITY];
  BmStateBlock block = bmGameStateBlock(g);
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SNAPSHOT, snap, sizeof(snap));
  TEST_ASSERT(size > 0);
  frames(20, 0);
  uint8_t after = l->angle;
  TEST_ASSERT(after == goal);
  TEST_ASSERT(bmStateRead(&block, 1, snap, size) == BM_STATE_OK && bmGameAfterLoad(g) == BM_SUCCESS);
  TEST_ASSERT(l->angle == 256 - 16 && (l->flags & BM_LAYER_PLAYED));
  frames(20, 0);
  TEST_ASSERT(l->angle == after);

  // The torso shows the aim step nearest to its angle, the legs their own (idle).
  const BmSpriteSheet *ts = &g->sheets[g->playerLayerSheet[1]];
  int32_t aim = bmSpriteSheetAnim(ts, g->pPack, "aim");
  TEST_ASSERT(l->anim.anim == aim);
  uint16_t step = bmSpriteSheetDirectionStep(ts, (uint8_t)aim, bmAngleOfDrawing(goal, g->player.facing < 0, false));
  int32_t hx = bmGameLayerCall(g, BM_SCRIPT_API_layer_point_x, (int32_t[]){ stringId("torso"), stringId("hand") });
  int32_t hy = bmGameLayerCall(g, BM_SCRIPT_API_layer_point_y, (int32_t[]){ stringId("torso"), stringId("hand") });
  int32_t ha = bmGameLayerCall(g, BM_SCRIPT_API_layer_point_angle, (int32_t[]){ stringId("torso"), stringId("hand") });
  TEST_ASSERT(ha == (uint8_t)(192 + 16 * step));
  TEST_ASSERT(hy < g->player.y - BM_FIX(15) && (g->player.facing > 0 ? hx > g->player.x : hx < g->player.x));
  // Running: the legs run, the torso keeps aiming.
  frames(10, BM_KEY_RIGHT);
  TEST_ASSERT(g->playerAnim.anim == g->playerAnims[BM_PLAYER_ANIM_RUN] && l->anim.anim == aim);
  frames(10, 0);

  // A shot from the hand along its angle.
  int before = s_game.actors.count;
  frames(1, BM_KEY_B);
  frames(1, 0);
  TEST_ASSERT(s_game.actors.count == before + 1);
  int shot = s_game.actors.count - 1;
  ha = bmGameLayerCall(g, BM_SCRIPT_API_layer_point_angle, (int32_t[]){ stringId("torso"), stringId("hand") });
  TEST_ASSERT(s_game.actors.angle[shot] == (uint8_t)ha);
  TEST_ASSERT(s_game.actors.vx[shot] == ((2 * 256 * bmAngleCos((uint8_t)ha)) >> 8) && s_game.actors.vy[shot] == ((2 * 256 * bmAngleSin((uint8_t)ha)) >> 8));
  TEST_ASSERT(s_game.actors.vy[shot] < 0);   // up and ahead

  // Hidden: the torso takes no hardware sprite; shown again with another palette (none: its own).
  bmGameLayerCall(g, BM_SCRIPT_API_layer_hide, (int32_t[]){ stringId("torso") });
  TEST_ASSERT(l->flags & BM_LAYER_HIDDEN);
  bmGameLayerCall(g, BM_SCRIPT_API_layer_show, (int32_t[]){ stringId("torso"), stringId("default") });
  TEST_ASSERT(!(l->flags & BM_LAYER_HIDDEN) && l->sprite == 0xFFFF);
  bmGameLayerCall(g, BM_SCRIPT_API_layer_palette, (int32_t[]){ stringId("torso"), stringId("nothing_like_this") });
  TEST_ASSERT(l->palette == 0xFF);
  // Back to following the legs.
  bmGameLayerCall(g, BM_SCRIPT_API_layer_play, (int32_t[]){ stringId("torso"), stringId("no_such_animation") });
  TEST_ASSERT(!(l->flags & BM_LAYER_PLAYED));
  TEST_ASSERT(bm_script_errors == 0);
}

// Sprite memory across rooms (layered-sprites.md §3): sheets stay loaded across rooms until a
// room needs the space. The debug lobby, the boss hall, then the chasm: the grapple's anchors
// are loaded with the room (they were left out before). The chasm, then the boss hall, then a
// portrait the room did not load beforehand: what earlier rooms left goes, nothing is missing.
static int8_t fightSheets(void)
{
  // What the boss fight draws: the boss, its fireballs, the drops, the two portraits.
  const char *names[] = { "moon_bat", "fireball", "drop", "hero_face", "vesper_face", "spark" };
  int8_t worst = 0;
  for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); k++)
  {
    int s = sheetOf(names[k]);
    int8_t b = s >= 0 ? bmGameSheetBank(&s_game, (int8_t)s, 0) : -1;
    if (b < worst) worst = b;
  }
  return worst;
}

static void testSpritesAcrossRooms(void)
{
  startWith(s_data, s_size, "debug_lobby");
  frames(3, 0);
  int32_t boss = bmGameFindRoom(&s_pack, "boss_hall"), chasm = bmGameFindRoom(&s_pack, "chasm_room");
  int32_t puzzle = bmGameFindRoom(&s_pack, "puzzle_room"), guard = bmGameFindRoom(&s_pack, "guard_room");
  TEST_ASSERT(boss >= 0 && chasm >= 0 && puzzle >= 0 && guard >= 0);
  bmGameMovePlayer(&s_game, (uint32_t)boss, 40, 160);
  frames(3, 0);
  TEST_ASSERT(fightSheets() >= 0);
  frames(2, 0);
  bmGameMovePlayer(&s_game, (uint32_t)chasm, 480, 160);
  frames(3, 0);
  int grapple = sheetOf("grapple");
  TEST_ASSERT(grapple >= 0 && s_game.grapple.anchorCount == 2);
  TEST_ASSERT(s_game.sheetTile[grapple] != BM_GAME_NO_TILES && banksOf(grapple, -1) >= 1 && s_game.spriteMisses == 0);
  // Round the other rooms, then the boss hall and its fight again: nothing missing.
  bmGameMovePlayer(&s_game, (uint32_t)puzzle, 40, 160);
  frames(3, 0);
  bmGameMovePlayer(&s_game, (uint32_t)guard, 40, 160);
  frames(3, 0);
  bmGameMovePlayer(&s_game, (uint32_t)chasm, 480, 160);
  frames(3, 0);
  bmGameMovePlayer(&s_game, (uint32_t)boss, 40, 160);
  frames(3, 0);
  TEST_ASSERT(fightSheets() >= 0);
  frames(2, 0);
  TEST_ASSERT(s_game.spriteMisses == 0);
}

// A door into a room the pack left out (a multiboot GBA's): the game holds still and asks for
// it (bmLiveHooks.wantedRoom); no answer for 3 seconds: the menu, try again or cancel (back
// into the room); when a pack with the room comes, through the door by itself. The pack here
// is the level with the boss hall's ROOM left out.
static void testWaitingForARoom(void)
{
  static uint8_t *trimmed;
  trimmed = malloc(s_size);
  memcpy(trimmed, s_data, s_size);
  BmPack full;
  TEST_ASSERT(bmPackOpen(&full, s_data, s_size, true) == BM_PACK_OK);
  int32_t boss = bmGameFindRoom(&full, "boss_hall");
  uint32_t tocOffset = bmRead32(trimmed + 24), room = 0;
  for (uint32_t i = 0; i < bmPackChunkCount(&full); i++)
  {
    uint8_t *e = trimmed + tocOffset + i * 24;
    if (bmRead32(e) != BM_CHUNK_ROOM || room++ != (uint32_t)boss) continue;
    memset(e + 4, 0, 8);   // offset, size
    e[16] |= BM_PACK_ENTRY_ABSENT;
  }
  uint32_t crc = bmCrc32(0, trimmed + BM_PACK_HEADER_SIZE, s_size - BM_PACK_HEADER_SIZE);
  for (int k = 0; k < 4; k++) trimmed[32 + k] = (uint8_t)(crc >> (8 * k));
  startWith(trimmed, s_size, "test_room_2");
  frames(3, 0);
  TEST_ASSERT(bmGameFindRoom(&s_pack, "boss_hall") < 0);
  // Into the east door.
  bmBodyPlace(&s_game.player, 470, 128);
  int f = 0;
  for (; f < 200 && !bmGameWaitingForRoom(&s_game, NULL, NULL); f++) frames(1, BM_KEY_RIGHT);
  TEST_ASSERT(bmGameWaitingForRoom(&s_game, NULL, NULL) && strcmp(bmPackString(&s_pack, (uint16_t)(s_game.wantRoom - 1)), "boss_hall") == 0);
  BmLiveHooks hooks = bmGameLiveHooks(&(BmGameLive){ &s_game, &s_pack, NULL, false, { 0 } });
  TEST_ASSERT(hooks.wantedRoom(hooks.pContext) == s_game.wantRoom);
  // It holds still: the player does not move, the room stays.
  int32_t x = s_game.player.x;
  bool menu = true;
  frames(BM_GAME_ROOM_WAIT_FRAMES - 10, BM_KEY_LEFT);
  TEST_ASSERT(s_game.player.x == x && strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  TEST_ASSERT(bmGameWaitingForRoom(&s_game, &menu, NULL) && !menu);
  // No answer: the menu. Try again: waiting again; then cancel: back into the room.
  frames(20, 0);
  uint8_t choice = 9;
  TEST_ASSERT(bmGameWaitingForRoom(&s_game, &menu, &choice) && menu && choice == 0);
  frames(1, BM_KEY_A);
  frames(1, 0);
  TEST_ASSERT(bmGameWaitingForRoom(&s_game, &menu, NULL) && !menu);
  frames(BM_GAME_ROOM_WAIT_FRAMES, 0);
  frames(1, BM_KEY_DOWN);
  frames(1, 0);
  TEST_ASSERT(bmGameWaitingForRoom(&s_game, &menu, &choice) && menu && choice == 1);
  frames(1, BM_KEY_A);
  frames(1, 0);
  TEST_ASSERT(!bmGameWaitingForRoom(&s_game, NULL, NULL) && BM_FIX_INT(s_game.player.x) < 480);
  frames(30, BM_KEY_LEFT);
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) < BM_FIX_INT(x) - 20 && strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  // Into it again; this time the whole level comes (as a hot push): through the door.
  frames(70, 0);   // the door rests after a cancel
  bmBodyPlace(&s_game.player, 470, 128);   // back on the ledge before it
  for (f = 0; f < 200 && !bmGameWaitingForRoom(&s_game, NULL, NULL); f++) frames(1, BM_KEY_RIGHT);
  TEST_ASSERT(bmGameWaitingForRoom(&s_game, NULL, NULL));
  frames(10, 0);
  TEST_ASSERT(bmPackOpen(&s_pack, s_data, s_size, true) == BM_PACK_OK && bmGameAfterLoad(&s_game) == BM_SUCCESS);
  frames(3, 0);
  TEST_ASSERT(!bmGameWaitingForRoom(&s_game, NULL, NULL) && strcmp(bmGameRoomId(&s_game), "boss_hall") == 0);
  TEST_ASSERT(bm_script_errors == 0);
  free(trimmed);
}

// A line longer than the dialogue box (the German "Dann bleib. Hier kommst du nicht mehr
// raus." takes three rows of 21): it appears up to the box's last row and waits with a mark;
// A scrolls it up a row and the rest appears; then A closes the box. A snapshot keeps the row.
static void testDialogueScrolls(void)
{
  startWith(s_data, s_size, "test_room");
  frames(3, 0);
  int de = -1;
  for (uint8_t k = 0; k < bmTextLanguageCount(&s_pack); k++)
  {
    const char *code = bmPackString(&s_pack, bmTextLanguageCode(&s_pack, k));
    if (code != NULL && strcmp(code, "de") == 0) de = k;
  }
  TEST_ASSERT(de >= 0);
  bmGameSetLanguage(&s_game, (uint8_t)de);
  bmGameSayLine(&s_game, keyOf("VesperStays"));
  frames(60, 0);
  TEST_ASSERT(s_game.dialogue != 0 && s_game.dialogueShown == s_game.dialogueLength && s_game.dialogueMore && s_game.dialogueRow == 0);
  // The marker bobs: its cell shows the triangle and the one a pixel lower in turn.
  uint8_t cells[2];
  for (int k = 0; k < 2; k++)
  {
    cells[k] = s_game.ui.map[18 * 32 + 28];
    frames(16, 0);
  }
  TEST_ASSERT(cells[0] != cells[1] && cells[0] != 0 && cells[1] != 0 && s_game.ui.map[18 * 32 + 28] == cells[0]);
  uint16_t codes[2];
  for (int k = 0; k < 2; k++) codes[k] = s_game.ui.glyphCode[cells[k] - BM_UI_GLYPH_FIRST];
  TEST_ASSERT((codes[0] == 0x25BC && codes[1] == 0xE800) || (codes[0] == 0xE800 && codes[1] == 0x25BC));
  const BmPpuState *ppu = bmHalHostGetPpu();
  (void)ppu;
  frames(1, BM_KEY_A);
  frames(1, 0);
  TEST_ASSERT(s_game.dialogue != 0 && s_game.dialogueRow == 1);
  frames(60, 0);
  TEST_ASSERT(s_game.dialogueShown == s_game.dialogueLength && !s_game.dialogueMore);
  // All of it was shown: the length counts the whole line now.
  const char *line = bmText(&s_pack, s_game.language, s_game.dialogueLine);
  TEST_ASSERT(line != NULL && strstr(line, "raus") != NULL && s_game.dialogueLength >= 40);
  frames(1, BM_KEY_A);
  frames(1, 0);
  TEST_ASSERT(s_game.dialogue == 0);
  // English, short enough: no mark, A closes at once.
  bmGameSetLanguage(&s_game, 0);
  bmGameSayLine(&s_game, keyOf("VesperFades"));
  frames(60, 0);
  TEST_ASSERT(!s_game.dialogueMore);
  frames(1, BM_KEY_A);
  frames(1, 0);
  TEST_ASSERT(s_game.dialogue == 0 && bm_script_errors == 0);
}

// The grapple (grapple.h) in the chasm (assets/test_room/chasm_room.tmx): nothing without the
// ability, which the pedestal gives; R to the anchor over the chasm and from it to the next,
// held level, and down onto the west floor; up the tower's ledge; the bat (light) pulled to
// the player; one made heavy pulls the player to it; jump lets go; a shot at nothing comes
// back.
static void grappleShot(uint16_t held)
{
  frames(1, (uint16_t)(held | BM_KEY_R));
}

static void untilGrappleIdle(int most, uint16_t held)
{
  for (int i = 0; i < most && s_game.grapple.phase != BM_GRAPPLE_IDLE; i++) frames(1, held);
}

static void testTheGrapple(void)
{
  startWith(s_data, s_size, "chasm_room");
  frames(5, 0);
  const BmGrapple *q = &s_game.grapple;
  BmBody *b = &s_game.player;
  TEST_ASSERT(q->anchorCount == 2 && q->sheet >= 0);
  // Without the ability: nothing.
  bmBodyPlace(b, 380, 160);
  b->facing = -1;
  frames(5, 0);
  grappleShot(0);
  frames(3, 0);
  TEST_ASSERT(q->phase == BM_GRAPPLE_IDLE && bmInventoryStat(&s_game.inventory, BM_STAT_GRAPPLE) == 0);
  // The pedestal gives it.
  bmBodyPlace(b, 456, 160);
  frames(5, 0);
  TEST_ASSERT(bmInventoryStat(&s_game.inventory, BM_STAT_GRAPPLE) == 1);
  // A shot at nothing (level, to the right): out and back.
  b->facing = 1;
  grappleShot(BM_KEY_RIGHT);
  TEST_ASSERT(q->phase == BM_GRAPPLE_FLYING && q->target == BM_GRAPPLE_NOTHING);
  untilGrappleIdle(60, 0);
  TEST_ASSERT(q->phase == BM_GRAPPLE_IDLE && b->onGround);
  frames(15, 0);
  // The bat (light): pulled to the player.
  int bat = actorOfClass("Bat");
  bmBodyPlace(b, 470, 160);
  b->facing = -1;
  frames(5, 0);
  grappleShot(0);
  TEST_ASSERT(q->target == BM_GRAPPLE_OBJECT && q->object == bat);
  int pulled = 0;
  for (int i = 0; i < 60 && q->phase != BM_GRAPPLE_BACK && q->phase != BM_GRAPPLE_IDLE; i++)
  {
    frames(1, 0);
    pulled |= q->phase == BM_GRAPPLE_PULL_OBJECT;
  }
  int32_t bx = BM_FIX_INT(s_game.actors.x[bat]);
  TEST_ASSERT(pulled && bx > 440 && abs(BM_FIX_INT(b->x) - bx) < 32 && BM_FIX_INT(b->x) == 470);
  untilGrappleIdle(60, 0);
  // Made heavy (as the knight, the boss): the player is pulled to it instead.
  s_game.actors.weight[bat] = 5;
  s_game.actors.x[bat] = BM_FIX(410);
  s_game.actors.y[bat] = BM_FIX(100);
  bmBodyPlace(b, 470, 160);
  b->facing = -1;
  frames(15, 0);
  bx = BM_FIX_INT(s_game.actors.x[bat]);
  grappleShot(0);
  int held = 0;
  for (int i = 0; i < 60 && q->phase != BM_GRAPPLE_BACK && q->phase != BM_GRAPPLE_IDLE; i++)
  {
    frames(1, 0);
    held |= q->phase == BM_GRAPPLE_PULL_PLAYER;
  }
  TEST_ASSERT(held && q->target == BM_GRAPPLE_OBJECT && abs(BM_FIX_INT(b->x) - BM_FIX_INT(s_game.actors.x[bat])) < 24);
  s_game.actors.hidden[bat] = 1;   // out of the way from here on
  untilGrappleIdle(60, 0);
  frames(60, 0);
  // The anchors across the chasm: up and left to the first, from there level to the second,
  // and down onto the west floor holding left.
  bmBodyPlace(b, 356, 160);
  b->facing = -1;
  frames(20, 0);
  grappleShot(0);
  TEST_ASSERT(q->target == BM_GRAPPLE_ANCHOR && q->targetX == 280);
  for (int i = 0; i < 60 && q->phase != BM_GRAPPLE_PULL_PLAYER; i++) frames(1, 0);
  int32_t y0 = BM_FIX_INT(b->y);
  for (int i = 0; i < 60 && q->phase == BM_GRAPPLE_PULL_PLAYER; i++) frames(1, 0);
  TEST_ASSERT(abs(BM_FIX_INT(b->x) - 280) <= 8 && BM_FIX_INT(b->y) < y0 && BM_FIX_INT(b->y) < 120 && b->vy < 0);
  // An anchor in the air is no safe place: a fall from there starts again where they stood.
  TEST_ASSERT(b->safeX >= 352 && b->safeY == 160);
  grappleShot(BM_KEY_LEFT);
  TEST_ASSERT(q->target == BM_GRAPPLE_ANCHOR && q->targetX == 160);
  for (int i = 0; i < 80 && q->phase != BM_GRAPPLE_BACK; i++) frames(1, 0);
  TEST_ASSERT(abs(BM_FIX_INT(b->x) - 160) <= 8);
  for (int i = 0; i < 80 && !b->onGround; i++) frames(1, BM_KEY_LEFT);
  TEST_ASSERT(b->onGround && BM_FIX_INT(b->y) == 160 && BM_FIX_INT(b->x) < 128);
  untilGrappleIdle(60, 0);
  frames(15, 0);
  // The tower's ledge (its top at 80): up beside it and onto it.
  bmBodyPlace(b, 124, 160);
  b->facing = -1;
  frames(10, 0);
  grappleShot(0);
  for (int i = 0; i < 60 && q->phase == BM_GRAPPLE_FLYING; i++) frames(1, 0);
  TEST_ASSERT(q->target == BM_GRAPPLE_LEDGE && q->targetY == 80 && q->targetX == 64);
  for (int i = 0; i < 60 && q->phase == BM_GRAPPLE_PULL_PLAYER; i++) frames(1, 0);
  frames(5, 0);
  TEST_ASSERT(b->onGround && BM_FIX_INT(b->y) == 80 && BM_FIX_INT(b->x) < 64);
  untilGrappleIdle(60, 0);
  // Jump lets go: from the tower to the first anchor... a jump instead.
  bmBodyPlace(b, 356, 160);
  b->facing = -1;
  frames(20, 0);
  grappleShot(0);
  for (int i = 0; i < 60 && q->phase != BM_GRAPPLE_PULL_PLAYER; i++) frames(1, 0);
  frames(3, 0);
  frames(1, BM_KEY_A);
  TEST_ASSERT(q->phase == BM_GRAPPLE_BACK && b->vy < 0);
  TEST_ASSERT(bm_script_errors == 0);
}


// The torch halls (docs/plans/lighting.md §7): the same room lit six ways. In both the player is
// brighter right under the torch than left of it, and behind the pillar, as far from the torch
// as on the left, only the ambient light reaches it (the pillar's shadow). The flicker is a pure
// function of the frame. With an output directory the six pictures go there side by side
// (torch_halls.ppm: palettes, light layer shared, fitted, of normals, streamed, hybrid, left to right).
static const char *s_outDir;

static int32_t bankBrightness(uint8_t bank)
{
  const BmPpuState *ppu = bmHalHostGetPpu();
  int32_t sum = 0;
  for (int i = 1; i < 16; i++)
  {
    uint16_t c = ppu->palette[256 + bank * 16 + i];
    sum += (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
  }
  return sum;
}

// A lit sprite is lit as it is seen: its slots' normals are as drawn (facing right), so a figure
// drawn mirrored (facing left) or upside down has them mirrored too. With the torch to the
// player's right, the slot that faces right most is lit facing right and dark facing left.
static void testSpritesAreLitTheWayTheyFace(void)
{
  startWith(s_data, s_size, "torch_hall");
  bmBodyPlace(&s_game.player, 120, 160);   // the torch at x 184: to the right, above
  frames(10, 0);
  int8_t sheet = s_game.playerSheets[0];
  const BmSpriteSheet *sh = &s_game.sheets[sheet];
  TEST_ASSERT(sh->pNormals != NULL);
  int bank = -1;
  for (int b = 0; b < BM_GAME_BANKS; b++)
    if (s_game.bankSheet[b] == (uint8_t)sheet) bank = b;
  TEST_ASSERT(bank >= 0);
  // The slots facing right (as drawn) count for, those facing left against.
  int32_t lean[2];
  for (int f = 0; f < 2; f++)
  {
    s_game.player.facing = (int8_t)(f == 0 ? 1 : -1);
    s_game.frame = 1000;   // the same flicker both times
    frames(1, 0);
    TEST_ASSERT(s_game.player.facing == (f == 0 ? 1 : -1));
    lean[f] = 0;
    for (int i = 1; i < 16; i++)
    {
      int nx = (int8_t)sh->pNormals[i * 4];
      if (nx > -20 && nx < 20) continue;
      uint16_t c = bmHalHostGetPpu()->palette[256 + bank * 16 + i];
      int32_t v = (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
      lean[f] += nx > 0 ? v : -v;
    }
  }
  printf("    slots facing right minus left, as drawn: %d facing right, %d facing left\n", (int)lean[0], (int)lean[1]);
  TEST_ASSERT(lean[0] > lean[1]);
}

static void testTheTorchHalls(void)
{
  static const char *const halls[6] = {"torch_hall", "torch_hall_layer", "torch_hall_fitted", "torch_hall_normals", "torch_hall_stream", "torch_hall_hybrid"};
  // The last three only with the light modes that are not kept (BM_LIGHT_EXPERIMENTAL).
  enum { HALLS = BM_LIGHT_EXPERIMENTAL ? 6 : 3 };
  static const int32_t xs[3] = {96, 184, 272};   // left of the torch, under it, behind the pillar
  static uint16_t shots[6][3][BM_PPU_WIDTH * BM_PPU_HEIGHT];
  int32_t bright[6][3];
  TEST_ASSERT(BM_LIGHT_EXPERIMENTAL || bmGameFindRoom(&s_pack, "torch_hall_normals") < 0);
  for (int h = 0; h < HALLS; h++)
  {
    startWith(s_data, s_size, halls[h]);
    TEST_ASSERT(s_game.light.mode == (h == 0 ? BM_LIGHT_PALETTE : BM_LIGHT_LAYER));
    // The flicker: the same at the same frame, changing over time, around the intensity.
    int32_t base = (int32_t)s_game.light.intensity * 4, lo = base, hi = base;
    TEST_ASSERT(bmLightIntensity(&s_game.light, 60) == bmLightIntensity(&s_game.light, 60));
    for (uint32_t f = 0; f < 600; f++)
    {
      int32_t v = bmLightIntensity(&s_game.light, f);
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
    TEST_ASSERT(hi > lo + base / 8 && lo >= base - base * s_game.light.flicker / 255 - 1 && hi <= base + base * s_game.light.flicker / 255 + 1);
    for (int p = 0; p < 3; p++)
    {
      bmBodyPlace(&s_game.player, xs[p], 160);
      frames(30, 0);           // the camera follows
      s_game.frame = 1000;     // the same flicker in every picture
      frames(1, 0);
      bright[h][p] = bankBrightness(0);
      bmHalHostRender(shots[h][p]);
    }
    printf("    %s: player brightness %d left, %d under the torch, %d behind the pillar\n", halls[h], (int)bright[h][0], (int)bright[h][1], (int)bright[h][2]);
    TEST_ASSERT(bright[h][1] > bright[h][0] && bright[h][0] > bright[h][2]);
    const BmPpuState *ppu = bmHalHostGetPpu();
    TEST_ASSERT(s_game.light.fitted == (h == 1 || h == 2));
    TEST_ASSERT(s_game.light.stream == (h >= 4) && s_game.light.hybrid == (h == 5));
    if (h >= 1 && h <= 3)
    {
      // The light layer is shown and added over the room: first target with the UI, EVA = EVB = 16.
      uint8_t bg = s_game.light.layerBg;
      TEST_ASSERT(bg < 3 && (bmPpuReg(ppu, BM_PPU_DISPCNT) & BM_PPU_DISPCNT_BG(bg)) != 0);
      TEST_ASSERT((bmPpuReg(ppu, BM_PPU_BLDCNT) & (1u << bg)) != 0 && bmPpuReg(ppu, BM_PPU_BLDALPHA) == (16 | 16 << 8));
      // Behind the UI's boxes (the HUD at the top, the room's title at the bottom) it is left
      // out: a window each, without it.
      uint16_t dispcnt = bmPpuReg(ppu, BM_PPU_DISPCNT), winin = bmPpuReg(ppu, BM_PPU_WININ);
      TEST_ASSERT((dispcnt & BM_PPU_DISPCNT_WIN0) != 0 && (winin & (1u << bg)) == 0 && (winin & 8u) != 0);
      TEST_ASSERT((dispcnt & BM_PPU_DISPCNT_WIN1) == 0 || (winin & (0x100u << bg)) == 0);
    }
    if (h == 5)
    {
      // Hybrid: two banks streamed, the rest fixed; the blend weight follows a line table.
      uint8_t bg = s_game.light.layerBg;
      TEST_ASSERT(s_game.light.streamStride == 32 && s_game.light.fixedBanks >= 1);
      TEST_ASSERT(ppu->pStreamRows != NULL && ppu->streamCount == 32);
      bool weights = false;
      for (int t = 0; t < BM_PPU_LINE_TABLES; t++) weights |= ppu->lineTables[t].reg == BM_PPU_BLDALPHA;
      TEST_ASSERT(weights && (bmPpuReg(ppu, BM_PPU_BLDCNT) & (1u << bg)) != 0);
    }
    if (h == 4)
    {
      // Streamed: the light layer's banks come line by line from the rows at the camera's; the
      // flicker is its blend weight (EVA), so the UI is not blended with it.
      uint8_t bg = s_game.light.layerBg;
      uint16_t bldcnt = bmPpuReg(ppu, BM_PPU_BLDCNT), alpha = bmPpuReg(ppu, BM_PPU_BLDALPHA);
      TEST_ASSERT(ppu->pStreamRows == s_game.light.pStream + (uint32_t)s_game.room.cameraY * s_game.light.streamStride);
      TEST_ASSERT(ppu->streamFirst == s_game.light.firstBank * 16 && ppu->streamCount == s_game.light.streamStride);
      TEST_ASSERT((bldcnt & (1u << bg)) != 0 && (bldcnt & 8u) == 0 && (alpha >> 8) == 16 && (alpha & 31) >= 6 && (alpha & 31) <= 16);
    }
  }
  // The halls light the player alike: the sprites are lit the same way in all six.
  for (int p = 0; p < 3; p++)
    for (int h = 1; h < HALLS; h++) TEST_ASSERT(bright[0][p] == bright[h][p]);
  // Out of the halls the player's colours are its own again.
  startWith(s_data, s_size, "torch_hall");
  int32_t lit = bankBrightness(0);
  TEST_ASSERT(bmEffectsTeleport(&s_game, (uint32_t)bmGameFindRoom(&s_pack, "debug_lobby"), 40, 160));
  frames(BM_TELEPORT_FRAMES + 2, 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "debug_lobby") == 0);
  TEST_ASSERT(s_game.light.mode == BM_LIGHT_NONE && bankBrightness(0) > lit);
  if (s_outDir == NULL) return;
  char path[512];
  snprintf(path, sizeof(path), "%s/torch_halls.ppm", s_outDir);
  FILE *f = fopen(path, "wb");
  TEST_ASSERT(f != NULL);
  if (f == NULL) return;
  const int gap = 4, w = BM_PPU_WIDTH * HALLS + gap * (HALLS - 1), hgt = BM_PPU_HEIGHT * 3 + gap * 2;
  fprintf(f, "P6\n%d %d\n255\n", w, hgt);
  for (int y = 0; y < hgt; y++)
    for (int x = 0; x < w; x++)
    {
      int row = y / (BM_PPU_HEIGHT + gap), ry = y % (BM_PPU_HEIGHT + gap);
      int col = x / (BM_PPU_WIDTH + gap), rx = x % (BM_PPU_WIDTH + gap);
      uint8_t rgb[3] = {255, 255, 255};
      if (ry < BM_PPU_HEIGHT && rx < BM_PPU_WIDTH)
      {
        uint16_t c = shots[col][row][ry * BM_PPU_WIDTH + rx];
        for (int k = 0; k < 3; k++)
        {
          uint8_t v = (uint8_t)((c >> (5 * k)) & 31);
          rgb[k] = (uint8_t)((v << 3) | (v >> 2));
        }
      }
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

// The rural street: a flickering gas lamp lights it (mode palette), and the puddle on the road
// mirrors the room (background 2 on background 0's map, its scroll set line by line) and the
// player and the knight standing over it (semi-transparent affine sprites, squeezed). The
// knight has a normal map.
static int mirroredSprites(const BmPpuState *ppu)
{
  int n = 0;
  for (int i = 0; i < 128; i++)
  {
    uint16_t a0 = ppu->oam[i * 4];
    if ((a0 & 0x100) && ((a0 >> 10) & 3) == 1) n++;
  }
  return n;
}

// The street's music follows what the player does (scripts/threat.ms, the room's attachment
// Threat): a step for moving (on average over the last 3 seconds), one for an enemy in sight,
// one for combat; it calms down a step at a time.
static const char *musicPlaying(void)
{
  return s_game.sound.players[0].playing ? bmPackString(&s_pack, s_game.sound.players[0].nameId) : "";
}

// Walks left and right in turn, `frames` in all, `each` frames a way; the highest intensity.
static int pace(int frames_, int each)
{
  int most = 0;
  for (int f = 0; f < frames_; f++)
  {
    frames(1, (f / each) % 2 ? BM_KEY_LEFT : BM_KEY_RIGHT);
    if (s_game.sound.intensity > most) most = s_game.sound.intensity;
  }
  return most;
}

// Frames until the intensity is `level` (standing still), or -1.
static int waitFor(int level, int limit)
{
  for (int f = 0; f < limit; f++)
  {
    if (s_game.sound.intensity == level) return f;
    frames(1, 0);
  }
  return -1;
}

static void testTheStreetsMusicFollowsThePlayer(void)
{
  startWith(s_data, s_size, "rural_street");
  frames(10, 0);
  TEST_ASSERT(strcmp(musicPlaying(), "night_escape") == 0);
  TEST_ASSERT(s_game.attachments.classKey[BM_ATTACH_ROOM_FIRST] != 0);
  TEST_ASSERT(s_game.sound.intensity == 0);
  TEST_ASSERT(s_game.sound.layerVolume[1] == BM_SOUND_LAYER_FULL && s_game.sound.layerVolume[2] == 0);   // calm, not escape
  // Standing about: calm. A few steps are not enough.
  frames(200, 0);
  TEST_ASSERT(s_game.sound.intensity == 0);
  TEST_ASSERT(pace(30, 30) == 0);
  // Moving about for a while, far from the knight: one step up.
  bmBodyPlace(&s_game.player, 40, 128);   // well away from the knight (the jump there is no run)
  int moved = pace(240, 40);
  TEST_ASSERT(moved == 1);
  TEST_ASSERT(s_game.sound.intensity == 1 && s_game.sound.layerVolume[2] > 0);
  // Still again: the three seconds run out, then the step is held, then calm.
  int calm = waitFor(0, 600);
  printf("    calm %d frames after stopping\n", calm);
  TEST_ASSERT(calm >= 180 + 60 && calm < 600);   // the average falls below within the 3 s, then the hold
  // Standing in the knight's sight: one step; at a lunge's distance, one more.
  int knight = actorOfClass("Knight");
  TEST_ASSERT(knight >= 0);
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[knight]) - 80, 128);
  frames(3, 0);
  TEST_ASSERT(s_game.sound.intensity == 1);   // the jump there is no run
  bmBodyPlace(&s_game.player, BM_FIX_INT(s_game.actors.x[knight]) - 24, 128);
  frames(3, 0);
  TEST_ASSERT(s_game.sound.intensity == 2);
  // Moving about in the fight as well: all three.
  int most = pace(300, 20);
  printf("    fighting on the move: intensity %d\n", most);
  TEST_ASSERT(most == 3);
  TEST_ASSERT(bm_script_errors == 0);
}

// The street's bench (scripts/bench.ms, a room attachment): not while the knight keeps watch;
// with it gone, Up at the bench sits the player down (the animation `sit`, held by a lock) and
// restores health and mana; a direction or a jump gets them up.
static void testTheStreetsBenchIsForResting(void)
{
  startWith(s_data, s_size, "rural_street");
  frames(5, 0);
  const uint8_t slot = BM_ATTACH_ROOM_FIRST + 1;
  const int32_t *bench = &s_game.attachments.fields[slot * BM_SCRIPT_MAX_FIELDS];   // Sitting, UpBefore, Health
  TEST_ASSERT(s_game.attachments.classKey[slot] != 0);
  int sit = bmSpriteSheetAnim(&s_game.sheets[s_game.playerSheet], &s_pack, "sit");
  TEST_ASSERT(sit >= 0);
  s_game.health = 2;
  s_game.mana = 0;
  bmBodyPlace(&s_game.player, 345, 128);
  frames(5, 0);
  frames(1, BM_KEY_UP);
  frames(2, 0);
  TEST_ASSERT(bench[0] == 0 && s_game.lockTimer == 0);   // the knight is there
  // The knight gone: rest.
  int knight = actorOfClass("Knight");
  TEST_ASSERT(knight >= 0);
  s_game.actors.gone[knight] = 1;
  s_game.actors.hidden[knight] = 1;
  bmBodyPlace(&s_game.player, 345, 128);
  frames(5, 0);
  uint32_t savesBefore = s_game.saves;
  frames(1, BM_KEY_UP);
  frames(2, 0);
  TEST_ASSERT(bench[0] == 1 && s_game.lockTimer != 0 && s_game.playerAnim.anim == (uint8_t)sit);
  TEST_ASSERT(s_game.health == bmGameMaxHealth(&s_game) && s_game.mana == bmGameMaxMana(&s_game));
  // Resting saves the game: the bench is the place to start again from.
  TEST_ASSERT(s_game.saves == savesBefore + 1 && s_game.savedRoom == s_game.roomIndex && s_game.savedX == 345);
  int32_t x = s_game.player.x;
  frames(120, 0);
  if (s_outDir != NULL)
  {
    uint16_t shot[BM_PPU_WIDTH * BM_PPU_HEIGHT];
    bmHalHostRender(shot);
    char path[512];
    snprintf(path, sizeof(path), "%s/rural_street_bench.ppm", s_outDir);
    FILE *f = fopen(path, "wb");
    if (f != NULL)
    {
      fprintf(f, "P6\n%d %d\n255\n", BM_PPU_WIDTH, BM_PPU_HEIGHT);
      for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
        for (int k = 0; k < 3; k++)
        {
          uint8_t v = (uint8_t)((shot[i] >> (5 * k)) & 31);
          fputc((v << 3) | (v >> 2), f);
        }
      fclose(f);
    }
  }
  TEST_ASSERT(bench[0] == 1 && s_game.player.x == x && s_game.playerAnim.anim == (uint8_t)sit);   // sits still
  // Right: back on their feet. Up again: down again (once, however long it is held); a jump:
  // up again.
  frames(2, BM_KEY_RIGHT);
  TEST_ASSERT(bench[0] == 0 && s_game.lockTimer == 0);
  frames(30, BM_KEY_UP);
  TEST_ASSERT(bench[0] == 1 && s_game.playerAnim.anim == (uint8_t)sit);
  frames(2, BM_KEY_A);
  TEST_ASSERT(bench[0] == 0 && s_game.lockTimer == 0);
  // Up again, the player's own animations: running.
  frames(40, 0);
  frames(10, BM_KEY_LEFT);
  TEST_ASSERT(s_game.animHeld == 0 && s_game.playerAnim.anim == (uint8_t)s_game.playerAnims[BM_PLAYER_ANIM_RUN]);
  // Away from the bench, Up does nothing.
  bmBodyPlace(&s_game.player, 200, 128);
  frames(5, 0);
  frames(1, BM_KEY_UP);
  frames(2, 0);
  TEST_ASSERT(bench[0] == 0 && s_game.lockTimer == 0);
  TEST_ASSERT(bm_script_errors == 0);
}

// Water underfoot (reflect.h, bmReflectSplashes): running across the street's puddle throws
// up splashes (the sprite and the sound "splash"), standing in it does not; the knight walking
// in it splashes too; a splash never cuts off another effect (a pickup's), and the music plays
// on with them, within the voices.
static int splashesShown(void)
{
  int n = 0;
  for (int k = 0; k < BM_GAME_EFFECTS; k++) n += s_game.effectAge[k] != 0 && s_game.effectSheet[k] == s_game.splashSheet;
  return n;
}

static void testPuddlesSplash(void)
{
  startWith(s_data, s_size, "rural_street");
  frames(5, 0);
  TEST_ASSERT(s_game.reflect.on && s_game.splashSheet >= 0);
  int knight = actorOfClass("Knight");
  TEST_ASSERT(knight >= 0);
  s_game.actors.gone[knight] = 1;   // the player alone first
  s_game.actors.hidden[knight] = 1;
  // Standing in it: nothing.
  bmBodyPlace(&s_game.player, 270, 128);
  frames(40, 0);
  TEST_ASSERT(splashesShown() == 0 && strcmp(effectPlaying(), "splash") != 0);
  // Running across it: splashes, seen and heard, the music on meanwhile.
  int seen = 0, heard = 0, musicVoices = 99;
  bool shot = false;
  for (int f = 0; f < 40; f++)
  {
    frames(1, BM_KEY_RIGHT);
    if (!shot && splashesShown() >= 2 && s_outDir != NULL)
    {
      // A picture with the splashes (after the frame they reach the screen in).
      frames(2, BM_KEY_RIGHT);
      int8_t bank = bmGameSheetBank(&s_game, s_game.splashSheet, 0);
      printf("    splash bank %d, colour 1 %04x\n", bank, bank >= 0 ? bmHalHostGetPpu()->palette[256 + bank * 16 + 1] : 0);
      shot = true;
      uint16_t pic[BM_PPU_WIDTH * BM_PPU_HEIGHT];
      bmHalHostRender(pic);
      char path[512];
      snprintf(path, sizeof(path), "%s/rural_street_splash.ppm", s_outDir);
      FILE *f2 = fopen(path, "wb");
      if (f2 != NULL)
      {
        fprintf(f2, "P6\n%d %d\n255\n", BM_PPU_WIDTH, BM_PPU_HEIGHT);
        for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
          for (int k = 0; k < 3; k++)
          {
            uint8_t v = (uint8_t)((pic[i] >> (5 * k)) & 31);
            fputc((v << 3) | (v >> 2), f2);
          }
        fclose(f2);
      }
    }
    seen = splashesShown() > seen ? splashesShown() : seen;
    heard += strcmp(effectPlaying(), "splash") == 0;
    int m = 0;
    for (int v = 0; v < BM_SOUND_VOICES; v++) m += s_game.sound.voices[v].active && s_game.sound.voices[v].owner == 0;
    if (m < musicVoices) musicVoices = m;
  }
  printf("    running in the puddle: %d splashes at once, heard in %d frames, music on %d voices at least\n", seen, heard, musicVoices);
  TEST_ASSERT(seen >= 2 && heard >= 10 && s_game.sound.players[0].playing && musicVoices >= 1);
  TEST_ASSERT(s_game.sound.voicesPeak <= BM_SOUND_VOICES);
  // Another effect sounding: the splashes leave it be.
  bmBodyPlace(&s_game.player, 270, 128);
  frames(20, 0);
  TEST_ASSERT(bmSoundPlayEffect(&s_game.sound, "pickup"));
  for (int f = 0; f < 12; f++)
  {
    frames(1, BM_KEY_RIGHT);
    TEST_ASSERT(strcmp(effectPlaying(), "pickup") == 0);
  }
  // The knight walking in it (towards the player): its splashes.
  startWith(s_data, s_size, "rural_street");
  frames(5, 0);
  knight = actorOfClass("Knight");
  bmBodyPlace(&s_game.player, 280, 128);   // in its sight (96), out of its reach
  int knightSplashes = 0;
  int32_t kx = s_game.actors.x[knight];
  for (int f = 0; f < 60; f++)
  {
    frames(1, 0);
    knightSplashes += splashesShown() > 0;
  }
  printf("    the knight walked %d px, splashes shown in %d frames\n", (int)BM_FIX_INT(kx - s_game.actors.x[knight]), knightSplashes);
  TEST_ASSERT(s_game.actors.x[knight] < kx && knightSplashes > 0);
  TEST_ASSERT(bm_script_errors == 0);
}

static void testThePuddleMirrorsTheStreet(void)
{
  startWith(s_data, s_size, "rural_street");
  TEST_ASSERT(s_game.light.mode == BM_LIGHT_PALETTE && s_game.light.flicker != 0);
  TEST_ASSERT(s_game.reflect.on && s_game.reflect.bg == 2 && s_game.reflect.layer == 0);
  bmBodyPlace(&s_game.player, 300, 128);
  frames(30, 0);
  const BmPpuState *ppu = bmHalHostGetPpu();
  TEST_ASSERT((bmPpuReg(ppu, BM_PPU_DISPCNT) & BM_PPU_DISPCNT_BG(2)) != 0);
  bool scroll = false, blend = false;
  for (int t = 0; t < BM_PPU_LINE_TABLES; t++)
  {
    scroll |= ppu->lineTables[t].reg == BM_PPU_BGVOFS(2);
    blend |= ppu->lineTables[t].reg == BM_PPU_BLDALPHA;
  }
  TEST_ASSERT(scroll && blend && (bmPpuReg(ppu, BM_PPU_BLDCNT) & (1u << 2)) != 0);
  int both = mirroredSprites(ppu);
  TEST_ASSERT(both >= 3);
  bool knightLit = false;
  for (uint8_t i = 0; i < s_game.actors.count; i++)
    if (s_game.actors.sheet[i] >= 0 && s_game.sheets[s_game.actors.sheet[i]].pNormals != NULL) knightLit = true;
  TEST_ASSERT(knightLit);
  // The flicker is shown in 5 steps, cached on entering: one is in palette memory.
  TEST_ASSERT(s_game.light.cacheSteps == BM_LIGHT_CACHE_STEPS && s_game.light.shownStep >= 0 && s_game.light.shownStep < BM_LIGHT_CACHE_STEPS);
  uint16_t shot[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmHalHostRender(shot);
  // Away from the puddle the player is not mirrored; the knight, standing over it, still is.
  bmBodyPlace(&s_game.player, 440, 128);
  frames(30, 0);
  int knight = mirroredSprites(bmHalHostGetPpu());
  TEST_ASSERT(knight > 0 && knight < both);
  // Out of the room the mirror is gone.
  TEST_ASSERT(bmEffectsTeleport(&s_game, (uint32_t)bmGameFindRoom(&s_pack, "debug_lobby"), 40, 160));
  frames(BM_TELEPORT_FRAMES + 2, 0);
  TEST_ASSERT(!s_game.reflect.on && (bmPpuReg(bmHalHostGetPpu(), BM_PPU_DISPCNT) & BM_PPU_DISPCNT_BG(2)) == 0);
  if (s_outDir == NULL) return;
  char path[512];
  snprintf(path, sizeof(path), "%s/rural_street.ppm", s_outDir);
  FILE *f = fopen(path, "wb");
  TEST_ASSERT(f != NULL);
  if (f == NULL) return;
  fprintf(f, "P6\n%d %d\n255\n", BM_PPU_WIDTH, BM_PPU_HEIGHT);
  for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
    for (int k = 0; k < 3; k++)
    {
      uint8_t v = (uint8_t)((shot[i] >> (5 * k)) & 31);
      fputc((v << 3) | (v >> 2), f);
    }
  fclose(f);
}

// The playable characters (game.h, set_character; data/characters.bmdata): the System menu has
// a row for the Character menu, which lists the characters whose figure the pack has;
// choosing Mitsuki (assets/mitsuki, rendered in Blender) makes her figure the player's in
// place: her sheets in sprite memory and the first banks instead of the player's, kept as an
// option; the player's lines in the dialogue are hers then (her Character record's speaker),
// the others' stay theirs. Figures that are not "player"/"player_<name>" are no characters.
static void testChoosingACharacter(void)
{
  start();
  frames(2, 0);
  const int legs = sheetOf("player_legs"), mlegs = sheetOf("mitsuki_legs"), mtorso = sheetOf("mitsuki_torso");
  TEST_ASSERT(legs >= 0 && mlegs >= 0 && mtorso >= 0 && s_game.playerSheet == legs && s_game.character == 0xFFFF);
  TEST_ASSERT(bmGameCharacterCount(&s_game) == 2);
  const uint16_t mitsuki = stringId("player_mitsuki"), hero = stringId("player");
  TEST_ASSERT(bmGameHasCharacter(&s_game, mitsuki) && bmGameHasCharacter(&s_game, hero));
  TEST_ASSERT(!bmGameHasCharacter(&s_game, stringId("mitsuki_legs")) && !bmGameSetCharacter(&s_game, stringId("mitsuki_legs")));

  // The System menu (Start, down to below Map, A): below the languages and the colours the
  // Character row; A there opens the Character menu, the one playing marked.
  uint32_t errors = bm_script_errors;
  press(BM_KEY_START);
  for (int i = 0; i < 4; i++) press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 2 && uiShows("Character") && !uiShows("Mitsuki"));
  const int rows = bmTextLanguageCount(&s_pack) + (s_game.colorCorrectable ? 1 : 0);
  for (int i = 0; i < rows; i++) press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 3 && uiShows("Hero      *") && uiShows("Mitsuki"));
  // Down to Mitsuki (the player's own is first), A.
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(bm_script_errors == errors && uiShows("Mitsuki   *") && !uiShows("Hero      *"));
  TEST_ASSERT(s_game.playerSheet == mlegs && s_game.playerFigure.layerCount == 2 && s_game.playerFigure.nameId == mitsuki);
  TEST_ASSERT(s_game.bankSheet[0] == mlegs && s_game.bankSheet[1] == mtorso);
  TEST_ASSERT(s_game.sheetTile[mlegs] != BM_GAME_NO_TILES && s_game.sheetTile[mtorso] != BM_GAME_NO_TILES);
  TEST_ASSERT(s_game.sheetTile[legs] == BM_GAME_NO_TILES);
  press(BM_KEY_B);
  press(BM_KEY_B);
  press(BM_KEY_B);
  TEST_ASSERT(s_game.screens.count == 0);
  // She runs, her figure drawn from her sheets.
  const int32_t x = s_game.player.x;
  frames(30, BM_KEY_RIGHT);
  TEST_ASSERT(s_game.player.x > x && s_game.playerAnim.anim == (uint8_t)s_game.playerAnims[BM_PLAYER_ANIM_RUN]);
  // The first pieces in OAM are hers (the torso in front, then the legs), in banks 1 and 0.
  const BmPpuState *ppu = bmHalHostGetPpu();
  for (int i = 0; i < 2; i++)
  {
    const int sheet = (ppu->oam[4 * i + 2] >> 12) == 1 ? mtorso : mlegs;
    const uint16_t tile = ppu->oam[4 * i + 2] & 0x3FF;
    TEST_ASSERT(tile >= s_game.sheetTile[sheet] && tile < s_game.sheetTile[sheet] + s_game.sheets[sheet].tileCount);
  }
  // The player's line (the speaker Hero's) is said by her, Vesper's stays Vesper's.
  bmGameSayLine(&s_game, keyOf("HeroAnswers"));
  TEST_ASSERT(s_game.dialogueSpeaker == stringId("Mitsuki") && s_game.dialoguePortrait == stringId("mitsuki_face") &&
              s_game.dialogueVoice == stringId("voice_mitsuki"));
  bmGameSayLine(&s_game, keyOf("VesperWakes"));
  TEST_ASSERT(s_game.dialogueSpeaker == stringId("Vesper") && s_game.dialoguePortrait == stringId("vesper_face"));
  s_game.dialogue = 0;
  // Kept as an option.
  bmGameStart(&s_game, &s_pack, NULL);
  TEST_ASSERT(s_game.character == mitsuki && s_game.playerSheet == mlegs);
  // The player again, for the other tests.
  TEST_ASSERT(bmGameSetCharacter(&s_game, hero) && s_game.playerSheet == legs && s_game.bankSheet[0] == legs);
  bmGameSayLine(&s_game, keyOf("HeroAnswers"));
  TEST_ASSERT(s_game.dialogueSpeaker == stringId("You") && s_game.dialoguePortrait == stringId("hero_face"));
  s_game.dialogue = 0;
  bmGameStart(&s_game, &s_pack, NULL);
  TEST_ASSERT(s_game.playerSheet == legs);
}

// Mitsuki's prologue (assets/mitsuki/home, its room rendered in Blender): the Character menu's
// Prologue row takes the player to her bed; the room's script (prologue.ms) makes her the
// player, asleep (her frames on the bed, facing right), the phone rings, she answers and Hina
// talks with her (Hina's portrait and voice), she gets up and the player has her. The door out
// needs her katana, which lies on its rack on the way; out through it the street, in front of
// the cottage; back in, she is awake.
static void testMitsukisPrologue(void)
{
  start();
  frames(2, 0);
  const uint32_t errors = bm_script_errors;
  press(BM_KEY_START);
  for (int i = 0; i < 4; i++) press(BM_KEY_DOWN);
  press(BM_KEY_A);
  const int rows = bmTextLanguageCount(&s_pack) + (s_game.colorCorrectable ? 1 : 0);
  for (int i = 0; i < rows; i++) press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 3 && uiShows("Prologue"));
  // Down past Hero and Mitsuki to Prologue: out of the menus, and the teleport.
  press(BM_KEY_DOWN);
  press(BM_KEY_DOWN);
  press(BM_KEY_A);
  TEST_ASSERT(s_game.screens.count == 0);
  frames(BM_TELEPORT_FRAMES + 4, 0);
  TEST_ASSERT(s_game.roomIndex == (uint32_t)bmGameFindRoom(&s_pack, "mitsuki_home"));
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) == 56 && s_game.player.facing == 1);
  TEST_ASSERT(s_game.character == stringId("player_mitsuki") && s_game.lockTimer != 0);
  const BmSpriteSheet *sheet = &s_game.sheets[s_game.playerSheet];
  const int sleep = bmSpriteSheetAnim(sheet, &s_pack, "sleep"), ring = bmSpriteSheetAnim(sheet, &s_pack, "ring");
  const int call = bmSpriteSheetAnim(sheet, &s_pack, "call");
  TEST_ASSERT(sleep >= 0 && ring >= 0 && call >= 0 && s_game.playerAnim.anim == (uint8_t)sleep);
  // Pressing nothing: she sleeps through three rings, then answers the fourth.
  bool rang = false;
  int f = 0;
  for (; f < 600 && s_game.dialogue == 0; f++)
  {
    frames(1, 0);
    rang |= s_game.playerAnim.anim == (uint8_t)ring && strcmp(effectPlaying(), "phone_ring") == 0;
  }
  TEST_ASSERT(rang && s_game.dialogue != 0 && s_game.playerAnim.anim == (uint8_t)call);
  TEST_ASSERT(s_game.dialogueSpeaker == stringId("Hina") && s_game.dialoguePortrait == stringId("hina_face") &&
              s_game.dialogueVoice == stringId("voice_hina"));
  // A through the call: Hina's lines and hers, until she is up and can walk.
  int lines = 0, hina = 0;
  uint16_t line = 0xFFFF;
  for (f = 0; f < 4000 && s_game.lockTimer != 0; f++)
  {
    if (s_game.dialogue != 0 && s_game.dialogueLine != line)
    {
      line = s_game.dialogueLine;
      lines++;
      hina += s_game.dialogueSpeaker == stringId("Hina");
    }
    frames(1, (f % 20) == 0 ? BM_KEY_A : 0);
  }
  printf("    the call: %d lines, %d of them Hina's\n", lines, hina);
  TEST_ASSERT(s_game.lockTimer == 0 && lines == 16 && hina == 7);
  // She says she'll bring her katana; it lies on the rack between her and the door.
  for (f = 0; f < 200 && s_game.dialogue == 0; f++) frames(1, 0);
  TEST_ASSERT(s_game.dialogue != 0 && s_game.dialogueSpeaker == stringId("Mitsuki"));
  const uint32_t katana = keyOf("Katana");
  TEST_ASSERT(bmInventoryCount(&s_game.inventory, katana) == 0);
  for (f = 0; f < 600 && s_game.roomIndex == (uint32_t)bmGameFindRoom(&s_pack, "mitsuki_home"); f++)
    frames(1, s_game.dialogue != 0 ? ((f % 20) == 0 ? BM_KEY_A : 0) : BM_KEY_RIGHT);
  TEST_ASSERT(bmInventoryCount(&s_game.inventory, katana) == 1 && s_game.inventory.equipped[0] == katana);
  TEST_ASSERT(s_game.roomIndex == (uint32_t)bmGameFindRoom(&s_pack, "rural_street") && BM_FIX_INT(s_game.player.x) == 118);
  // Back in through the cottage's door (Up): awake, no second call.
  for (f = 0; f < 60 && s_game.dialogue != 0; f++) frames(1, (f % 20) == 0 ? BM_KEY_A : 0);
  frames(20, 0);
  bmBodyPlace(&s_game.player, 94, 128);
  frames(2, 0);
  press(BM_KEY_UP);
  frames(40, 0);
  TEST_ASSERT(s_game.roomIndex == (uint32_t)bmGameFindRoom(&s_pack, "mitsuki_home") && s_game.lockTimer == 0);
  TEST_ASSERT(BM_FIX_INT(s_game.player.x) == 292);
  frames(200, 0);
  TEST_ASSERT(s_game.dialogue == 0 && s_game.lockTimer == 0);
  TEST_ASSERT(bm_script_errors == errors);
  // The player again, for the other tests.
  TEST_ASSERT(bmGameSetCharacter(&s_game, stringId("player")));
  bmGameStart(&s_game, &s_pack, NULL);
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
  if (argc > 3) s_outDir = argv[3];

  RUN_TEST(testAutopilotPlaysThroughTheLevel);
  RUN_TEST(testFallingIntoAPitRespawnsOnSafeGround);
  RUN_TEST(testDoorsLeadBothWays);
  RUN_TEST(testSavePointWritesASaveGame);
  RUN_TEST(testOldSaveGames);
  RUN_TEST(testTheBatChasesHitsAndGoesHome);
  RUN_TEST(testAPickupStaysTaken);
  RUN_TEST(testAnAttachmentMakesThePlayerJumpHigher);
  RUN_TEST(testTheItemTables);
  RUN_TEST(testTheRoomsAttachmentsComeAndGoWithTheRoom);
  RUN_TEST(testAHitStrikesOnce);
  RUN_TEST(testCapabilitiesAndFilters);
  RUN_TEST(testGuardsAndProjectiles);
  RUN_TEST(testTheSwordDefeatsTheBat);
  RUN_TEST(testTheKnightsShield);
  RUN_TEST(testBackThroughADoorAtOnce);
  RUN_TEST(testBreakableWallsAndCrates);
  RUN_TEST(testThePushBlockPuzzle);
  RUN_TEST(testSheetsAndPalettesPerRoom);
  RUN_TEST(testSpritesAcrossRooms);
  RUN_TEST(testWaitingForARoom);
  RUN_TEST(testDialogueScrolls);
  RUN_TEST(testSpritesAreLitTheWayTheyFace);
  RUN_TEST(testTheTorchHalls);
  RUN_TEST(testThePuddleMirrorsTheStreet);
  RUN_TEST(testTheStreetsMusicFollowsThePlayer);
  RUN_TEST(testTheStreetsBenchIsForResting);
  RUN_TEST(testPuddlesSplash);
  RUN_TEST(testColourCorrection);
  RUN_TEST(testChoosingACharacter);
  RUN_TEST(testMitsukisPrologue);
  RUN_TEST(testAimingTheTorso);
  RUN_TEST(testIronBat);
  RUN_TEST(testEquipmentShows);
  RUN_TEST(testTheLock);
  RUN_TEST(testTheGrapple);
  RUN_TEST(testTheUiLayer);
  RUN_TEST(testTheMenus);
  RUN_TEST(testTheWorldMap);
  RUN_TEST(testElementsArmourAndConsumables);
  RUN_TEST(testEquipmentRoles);
  RUN_TEST(testAbilities);
  RUN_TEST(testKnockbackAndFlash);
  RUN_TEST(testADropFallsAndIsTaken);
  RUN_TEST(testTheDebugLobby);
  RUN_TEST(testDoorsOpenAsThePlayerWalksThrough);
  RUN_TEST(testRoomAcoustics);
  RUN_TEST(testCommands);
  RUN_TEST(testEveryWeaponHasCommands);
  RUN_TEST(testAttacksAndMovement);
  RUN_TEST(testHitAndPickupSounds);
  RUN_TEST(testAimedSwings);
  RUN_TEST(testALineAppearsLetterByLetter);
  RUN_TEST(testTheBossIsDefeated);
  free(s_data);
  free(s_windData);
  return TEST_RESULT();
}
