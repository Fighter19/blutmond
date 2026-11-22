/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Live reload on the host: the game runs with the live module, the master pushes a changed
// pack and state through the host HAL's serial port while frames go on.
// Arguments: the test level pack and a variant with one background tile changed.

#include "autopilot.h"
#include "device.h"
#include "game.h"
#include "game_live.h"
#include "hal_host.h"
#include "live.h"
#include "live_master.h"
#include "physical_device.h"
#include "video.h"

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_SIZE (192 * 1024)   // about as a cartridge image (src/hal/gba/gba_cart.ld)
#define WORDS_PER_FRAME 128   // about 30 KB/s at 60 frames per second

static uint8_t *s_level, *s_variant, *s_scripts;
static uint32_t s_levelSize, s_variantSize, s_scriptsSize;

static BmPack s_pack;
static BmGame s_game;
static BmAutopilot s_pilot;
static BmGameLive s_gameLive = { &s_game, &s_pack, &s_pilot, false, { 0 } };
static BmLive s_live;
static uint8_t s_pool[POOL_SIZE] __attribute__((aligned(4)));

typedef struct Wire
{
  uint32_t words;
  uint32_t rng;
  uint32_t flipPerMillion;
  uint32_t flips;
} Wire;
static Wire s_wire;
static bool s_paused;  // frames go on for the link, the game stands still

static void frame(void)
{
  bmVideoWaitFrame();
  bmLiveFrame(&s_live);
  if (!s_paused && !bmLiveWaiting(&s_live)) bmGameFrame(&s_game, bmAutopilotKeys(&s_pilot, &s_game));
}

static uint32_t rnd(void)
{
  s_wire.rng = s_wire.rng * 1664525u + 1013904223u;
  return s_wire.rng >> 8;
}

static uint32_t exchange(void *pContext, uint32_t out)
{
  (void)pContext;
  if (s_wire.flipPerMillion != 0 && rnd() % 1000000u < s_wire.flipPerMillion) { out ^= 1u << (rnd() & 31); s_wire.flips++; }
  uint32_t in = bmHalHostLinkExchange(out);
  if (s_wire.flipPerMillion != 0 && rnd() % 1000000u < s_wire.flipPerMillion) { in ^= 1u << (rnd() & 31); s_wire.flips++; }
  if (++s_wire.words % WORDS_PER_FRAME == 0) frame();
  return in;
}

// The game with the level, and a pool of `poolSize` bytes for packs; `inPool`: the level lies
// at the pool's start (as a multiboot image's embedded pack), otherwise outside (ROM).
static void startWith(BmLinkMaster *m, uint32_t poolSize, bool inPool)
{
  bmHalHostReset();
  BmDeviceHandle device = NULL;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  memset(s_pool, 0, sizeof(s_pool));
  if (inPool) memcpy(s_pool, s_level, s_levelSize);
  bmPackOpen(&s_pack, inPool ? s_pool : s_level, s_levelSize, true);
  memset(&s_gameLive, 0, sizeof(s_gameLive));
  s_gameLive.pGame = &s_game;
  s_gameLive.pPack = &s_pack;
  s_gameLive.pPilot = &s_pilot;
  bmGameStart(&s_game, &s_pack, NULL);
  memset(&s_pilot, 0, sizeof(s_pilot));
  BmLiveHooks hooks = bmGameLiveHooks(&s_gameLive);
  bmLiveInit(&s_live, &hooks, s_pool, poolSize, &s_pack);
  bmLiveStart(&s_live);
  memset(&s_wire, 0, sizeof(s_wire));
  s_wire.rng = 99;
  BmLinkTransport t = { NULL, exchange, NULL, NULL, 0, NULL };
  bmLinkMasterInit(m, &t);
  s_paused = false;
  for (int i = 0; i < 30; i++) frame();
}

static void start(BmLinkMaster *m)
{
  startWith(m, POOL_SIZE, false);
}

static void testHello(void)
{
  BmLinkMaster m;
  start(&m);
  BmLiveRemote r;
  bool ok = bmLiveMasterHello(&m, &r);
  if (!ok) printf("error %s; slave in %u out %u bad %u busy %u pending %u txCount %u txSend %u words %u; master in %u bad %u inbox %u\n", m.pError,
     (unsigned)s_live.slave.counters.packetsIn, (unsigned)s_live.slave.counters.packetsOut, (unsigned)s_live.slave.counters.badPackets,
     (unsigned)s_live.slave.counters.busy, s_live.pendingCount, s_live.slave.txCount, s_live.slave.txSend, (unsigned)s_live.slave.counters.words, (unsigned)m.counters.packetsIn, (unsigned)m.counters.badPackets, (unsigned)m.inboxCount);
  TEST_ASSERT(ok);
  TEST_ASSERT(r.hello.protocolVersion == BM_LINK_PROTOCOL_VERSION);
  TEST_ASSERT(r.hello.activeSize == s_levelSize);
  TEST_ASSERT(r.hello.chunkCount == bmPackChunkCount(&s_pack));
  TEST_ASSERT(r.hello.hashCount == r.hello.chunkCount);
  TEST_ASSERT(r.hello.contentCapacity == POOL_SIZE);   // the level is outside: all of it is free
  TEST_ASSERT(r.hello.poolCapacity == POOL_SIZE);
  bmLinkMasterFree(&m);
}

static void testHotReloadKeepsTheGameGoing(void)
{
  BmLinkMaster m;
  start(&m);
  BmLiveRemote r;
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  BmLivePushStats stats;
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_variant, s_variantSize, 1, &r, &stats));
  printf("    %u chunks sent (%u bytes), %u resident\n", (unsigned)stats.chunksSent, (unsigned)stats.bytesSent,
         (unsigned)stats.chunksResident);
  TEST_ASSERT(stats.chunksSent >= 1 && stats.chunksResident >= 1);
  TEST_ASSERT(stats.bytesSent < s_variantSize / 2);  // only what changed

  uint32_t frameBefore = s_game.frame;
  BmLiveStatus st;
  TEST_ASSERT(bmLiveMasterApply(&m, 1, BM_LIVE_HOT, NULL, &st));
  TEST_ASSERT(st.kind == 1 && st.result == BM_LIVE_OK);
  TEST_ASSERT(st.frame >= frameBefore);           // the state was kept, not reset
  TEST_ASSERT(s_live.active.size == s_variantSize);
  TEST_ASSERT(memcmp(s_live.active.pData, s_variant, s_variantSize) == 0);
  TEST_ASSERT(s_game.pPack == &s_pack && s_pack.pData == s_live.active.pData);
  TEST_ASSERT(s_live.chunksCopied == stats.chunksResident);

  // The game plays on in the new pack, through the door into the second room.
  for (int i = 0; i < 1500 && s_game.roomChanges == 0; i++) frame();
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  TEST_ASSERT(s_game.deaths == 0);

  // A second push goes to the other end of the pool; the first one is what the game uses now.
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  TEST_ASSERT(r.hello.activeSize == s_variantSize);
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_level, s_levelSize, 2, &r, &stats));
  TEST_ASSERT(bmLiveMasterApply(&m, 2, BM_LIVE_HOT, NULL, &st));
  TEST_ASSERT(st.result == BM_LIVE_OK && st.roomIndex == 1);
  TEST_ASSERT(memcmp(s_live.active.pData, s_level, s_levelSize) == 0);
  bmLinkMasterFree(&m);
}

static void testStateGetAndPut(void)
{
  BmLinkMaster m;
  start(&m);
  static uint8_t snap[3072];
  uint32_t size = 0;
  TEST_ASSERT(bmLiveMasterGetState(&m, BM_STATE_SNAPSHOT, snap, sizeof(snap), &size));
  TEST_ASSERT(size > BM_STATE_HEADER_SIZE);
  uint32_t snapFrame = 0;
  {
    // Read the frame counter out of a copy of the game.
    static BmGame copy;
    static BmAutopilot pilot;
    copy = s_game;
    BmStateBlock blocks[2] = { bmGameStateBlock(&copy), bmAutopilotStateBlock(&pilot) };
    TEST_ASSERT(bmStateRead(blocks, 2, snap, size) == BM_STATE_OK);
    snapFrame = copy.frame;
  }
  for (int i = 0; i < 200; i++) frame();
  TEST_ASSERT(s_game.frame > snapFrame + 150);
  BmLiveStatus st;
  TEST_ASSERT(bmLiveMasterPutState(&m, snap, size, &st));
  TEST_ASSERT(st.result == BM_LIVE_OK);
  TEST_ASSERT(st.frame == snapFrame);

  // Data of another layout is refused and changes nothing.
  snap[8] ^= 0xFF;  // schema hash
  uint32_t now = s_game.frame;
  TEST_ASSERT(bmLiveMasterPutState(&m, snap, size, &st));
  TEST_ASSERT(st.result == BM_LIVE_ERROR_STATE);
  TEST_ASSERT(s_game.frame >= now);
  bmLinkMasterFree(&m);
}

static void testRestartInAnotherRoom(void)
{
  BmLinkMaster m;
  start(&m);
  for (int i = 0; i < 100; i++) frame();
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_variant, s_variantSize, 7, NULL, NULL));  // everything
  BmLiveStatus st;
  TEST_ASSERT(bmLiveMasterApply(&m, 7, BM_LIVE_RESTART, "test_room_2", &st));
  TEST_ASSERT(st.result == BM_LIVE_OK);
  TEST_ASSERT(st.roomIndex == 1);
  TEST_ASSERT(st.frame < 5);
  bmLinkMasterFree(&m);
}

// The pool holds the running pack and the one arriving at its two ends, whatever their sizes,
// so a pack is placed as long as both fit together.
static void testPacksTakeTurnsAtTheEndsOfThePool(void)
{
  BmLinkMaster m;
  uint32_t poolSize = (s_levelSize + s_variantSize + 64) & ~3u;
  startWith(&m, poolSize, true);
  BmLiveRemote r;
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  TEST_ASSERT(r.hello.contentCapacity == ((poolSize - s_levelSize) & ~3u));
  TEST_ASSERT(r.hello.poolCapacity == poolSize);
  BmLiveStatus st;
  const uint8_t *packs[3] = { s_variant, s_level, s_variant };
  uint32_t sizes[3] = { s_variantSize, s_levelSize, s_variantSize };
  for (uint32_t k = 0; k < 3; k++)
  {
    const uint8_t *before = s_live.active.pData;
    BmLivePushStats stats;
    TEST_ASSERT(bmLiveMasterHello(&m, &r));
    TEST_ASSERT(bmLiveMasterPushPack(&m, packs[k], sizes[k], 10 + k, &r, &stats));
    TEST_ASSERT(stats.chunksResident >= 1);   // beside it: what did not change is copied
    TEST_ASSERT(bmLiveMasterApply(&m, 10 + k, BM_LIVE_HOT, NULL, &st));
    TEST_ASSERT(st.result == BM_LIVE_OK);
    TEST_ASSERT(!bmLiveWaiting(&s_live));
    TEST_ASSERT(s_live.active.pData != before);
    // At one end or the other.
    TEST_ASSERT(s_live.active.pData == s_pool || s_live.active.pData + sizes[k] + 4 > s_pool + poolSize);
    TEST_ASSERT(memcmp(s_live.active.pData, packs[k], sizes[k]) == 0);
  }
  bmLinkMasterFree(&m);
}

// Two packs that do not fit together: the arriving one is written over the running one, at its
// place, and the chunks that stay at the same offset stay where they are. The game holds still
// from the first packet on, and plays on in the same room afterwards.
static void testAPackTooLargeToGoBesideReplacesTheRunningOne(void)
{
  BmLinkMaster m;
  uint32_t poolSize = (s_levelSize + s_variantSize / 2) & ~3u;
  startWith(&m, poolSize, true);
  for (int i = 0; i < 1500 && s_game.roomChanges == 0; i++) frame();
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  BmLiveRemote r;
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  TEST_ASSERT(r.hello.contentCapacity < s_variantSize && r.hello.poolCapacity >= s_variantSize);
  BmLivePushStats stats;
  uint32_t frameBefore = s_game.frame;
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_variant, s_variantSize, 7, &r, &stats));
  printf("    over it: %u chunks sent (%u bytes), %u kept in place\n", (unsigned)stats.chunksSent, (unsigned)stats.bytesSent,
         (unsigned)stats.chunksResident);
  TEST_ASSERT(r.hello.inPlaceCapacity >= s_variantSize);
  TEST_ASSERT(stats.chunksSent == 1 && stats.bytesSent < s_variantSize / 8);   // one tile changed: one chunk
  TEST_ASSERT(s_live.chunksCopied == stats.chunksResident);
  TEST_ASSERT(bmLiveWaiting(&s_live));
  frameBefore = s_game.frame;
  for (int i = 0; i < 30; i++) frame();
  TEST_ASSERT(s_game.frame == frameBefore);  // it holds still meanwhile
  BmLiveStatus st;
  TEST_ASSERT(bmLiveMasterApply(&m, 7, BM_LIVE_HOT, NULL, &st));
  TEST_ASSERT(st.result == BM_LIVE_OK);
  TEST_ASSERT(!bmLiveWaiting(&s_live));
  TEST_ASSERT(s_live.active.pData == s_pool && memcmp(s_pool, s_variant, s_variantSize) == 0);
  TEST_ASSERT(strcmp(bmGameRoomId(&s_game), "test_room_2") == 0);
  for (int i = 0; i < 60; i++) frame();
  TEST_ASSERT(s_game.frame > frameBefore);

  // A pack whose chunks moved (a script changed size): what moved is sent, the rest is kept,
  // and the result is the pack byte for byte.
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_scripts, s_scriptsSize, 9, &r, &stats));
  printf("    moved: %u chunks sent, %u kept in place\n", (unsigned)stats.chunksSent, (unsigned)stats.chunksResident);
  TEST_ASSERT(bmLiveMasterApply(&m, 9, BM_LIVE_HOT, NULL, &st));
  TEST_ASSERT(st.result == BM_LIVE_OK);
  TEST_ASSERT(s_live.active.pData == s_pool && memcmp(s_pool, s_scripts, s_scriptsSize) == 0);
  for (int i = 0; i < 60; i++) frame();

  bmLinkMasterFree(&m);
}

static void testFailedTransferLeavesTheGameAlone(void)
{
  BmLinkMaster m;
  start(&m);
  BmLiveStatus st;
  TEST_ASSERT(bmLiveMasterApply(&m, 42, BM_LIVE_HOT, NULL, &st));  // nothing sent
  TEST_ASSERT(st.result == BM_LIVE_ERROR_TRANSFER);
  TEST_ASSERT(s_pack.pData == s_level);

  // A transfer whose chunks are not all sent is refused.
  BmLiveRemote r;
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  // Pretend the runtime has every chunk of the variant: nothing is sent.
  BmPack variant;
  bmPackOpen(&variant, s_variant, s_variantSize, false);
  r.hello.hashCount = bmPackChunkCount(&variant);
  for (uint32_t i = 0; i < r.hello.hashCount; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&variant, i, &e);
    r.pairs[2 * i] = e.hash;
    r.pairs[2 * i + 1] = e.size;
  }
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_variant, s_variantSize, 3, &r, NULL));
  TEST_ASSERT(bmLiveMasterApply(&m, 3, BM_LIVE_HOT, NULL, &st));
  TEST_ASSERT(st.result == BM_LIVE_ERROR_TRANSFER);
  TEST_ASSERT(s_pack.pData == s_level);
  TEST_ASSERT(bmLiveMasterApply(&m, 0, BM_LIVE_HOT_MIGRATE, NULL, &st));
  TEST_ASSERT(st.result == BM_LIVE_ERROR_UNSUPPORTED);
  bmLinkMasterFree(&m);
}

static void testPushWithBitErrors(void)
{
  BmLinkMaster m;
  start(&m);
  s_wire.flipPerMillion = 500;
  BmLiveRemote r;
  TEST_ASSERT(bmLiveMasterHello(&m, &r));
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_variant, s_variantSize, 5, NULL, NULL));
  BmLiveStatus st;
  TEST_ASSERT(bmLiveMasterApply(&m, 5, BM_LIVE_HOT, NULL, &st));
  TEST_ASSERT(st.result == BM_LIVE_OK);
  TEST_ASSERT(memcmp(s_live.active.pData, s_variant, s_variantSize) == 0);
  printf("    %u words, %u flipped bits, %u windows again, %u resyncs\n", (unsigned)m.counters.words,
         (unsigned)s_wire.flips, (unsigned)m.counters.retransmits, (unsigned)m.counters.resyncs);
  TEST_ASSERT(s_wire.flips > 0);
  bmLinkMasterFree(&m);
}

static uint8_t *readFile(const char *path, uint32_t *pSize)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *p = malloc((size_t)n);
  if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
  fclose(f);
  *pSize = (uint32_t)n;
  return p;
}

// Changed scripts pushed into the running game: only the module (and the strings) travel, the
// scripted objects start anew with the new code, and the game goes on.
static void testScriptsArePushedLive(void)
{
  if (s_scripts == NULL) return;
  BmLinkMaster m;
  start(&m);
  uint32_t hashBefore = s_game.actors.moduleHash;
  TEST_ASSERT(s_game.actors.hasModule && hashBefore != 0);
  BmLiveRemote remote;
  TEST_ASSERT(bmLiveMasterHello(&m, &remote));
  BmLivePushStats st;
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_scripts, s_scriptsSize, 77, &remote, &st));
  BmLiveStatus status;
  TEST_ASSERT(bmLiveMasterApply(&m, 77, BM_LIVE_HOT, NULL, &status));
  if (status.result != BM_LIVE_OK) printf("    apply: result %u\n", status.result);
  TEST_ASSERT(status.result == BM_LIVE_OK);
  printf("    %u chunks sent (%u bytes), %u already there\n", (unsigned)st.chunksSent, (unsigned)st.bytesSent, (unsigned)st.chunksResident);
  TEST_ASSERT(st.chunksSent <= 2);
  TEST_ASSERT(s_game.actors.hasModule && s_game.actors.moduleHash != hashBefore);
  // The sound engine reads songs in place: it started over on the new pack and plays on.
  TEST_ASSERT(s_game.sound.pPack == &s_pack && s_game.sound.players[0].playing);
  // With the changed door script the autopilot reaches the door but stays in the first room.
  for (int i = 0; i < 900; i++) frame();
  TEST_ASSERT(s_game.roomChanges == 0);
  TEST_ASSERT(bm_script_errors == 0);
  bmLinkMasterFree(&m);
}

static int actorOfClass(const char *name)
{
  const BmActors *a = &s_game.actors;
  for (uint8_t i = 0; i < a->count; i++)
  {
    uint16_t cls = 0;
    const BmVmModule *m = bmActorsModule(a, i, &cls);
    const char *n = m != NULL ? bmPackString(&s_pack, bmVmClassName(m, cls)) : NULL;
    if (n != NULL && strcmp(n, name) == 0) return i;
  }
  return -1;
}

// Reload mode `module` (attachable-scripts.md §4): the changed door script is swapped while
// the bat is in the middle of a sleep; the bat goes on exactly where it was, the door keeps its
// variables and runs the new code, and only the door's module (and the strings) travel.
static void testModuleReloadKeepsTheOtherScripts(void)
{
  if (s_scripts == NULL) return;
  BmLinkMaster m;
  start(&m);
  // The second room has the bat and a door; the player next to the bat makes it chase, which
  // it does in steps of sleep(60).
  TEST_ASSERT(bmGameStart(&s_game, &s_pack, "test_room_2") == BM_SUCCESS);
  BmActors *a = &s_game.actors;
  int bat = actorOfClass("Bat"), door = actorOfClass("Door");
  TEST_ASSERT(bat >= 0 && door >= 0);
  if (bat < 0 || door < 0) return;
  s_game.player.x = a->x[bat];
  s_game.player.y = a->y[bat] + BM_FIX(40);
  for (int i = 0; i < 2000 && !(a->wait[bat] == BM_VM_SLEEPING && a->timer[bat] > 20); i++) frame();
  TEST_ASSERT(a->wait[bat] == BM_VM_SLEEPING);
  s_paused = true;
  uint8_t state = a->state[bat], wait = a->wait[bat];
  uint16_t pc = a->pc[bat], timer = a->timer[bat];
  int32_t fields[BM_SCRIPT_MAX_FIELDS], doorFields[BM_SCRIPT_MAX_FIELDS];
  memcpy(fields, &a->fields[bat * BM_SCRIPT_MAX_FIELDS], sizeof(fields));
  memcpy(doorFields, &a->fields[door * BM_SCRIPT_MAX_FIELDS], sizeof(doorFields));
  int32_t x = a->x[bat], y = a->y[bat];
  uint32_t hashBefore = a->moduleHash;

  BmLiveRemote remote;
  TEST_ASSERT(bmLiveMasterHello(&m, &remote));
  BmLivePushStats st;
  TEST_ASSERT(bmLiveMasterPushPack(&m, s_scripts, s_scriptsSize, 78, &remote, &st));
  BmLiveStatus status;
  TEST_ASSERT(bmLiveMasterApply(&m, 78, BM_LIVE_MODULE, NULL, &status));
  TEST_ASSERT(status.result == BM_LIVE_OK);
  printf("    %u chunks sent (%u bytes)\n", (unsigned)st.chunksSent, (unsigned)st.bytesSent);
  TEST_ASSERT(st.chunksSent <= 2);
  TEST_ASSERT(a->moduleHash != hashBefore);
  // The bat: exactly as it was.
  TEST_ASSERT(actorOfClass("Bat") == bat && actorOfClass("Door") == door);
  TEST_ASSERT(a->state[bat] == state && a->wait[bat] == wait && a->pc[bat] == pc && a->timer[bat] == timer);
  TEST_ASSERT(memcmp(fields, &a->fields[bat * BM_SCRIPT_MAX_FIELDS], sizeof(fields)) == 0 && a->x[bat] == x && a->y[bat] == y);
  // The door: the same variables, the new code.
  TEST_ASSERT(memcmp(doorFields, &a->fields[door * BM_SCRIPT_MAX_FIELDS], sizeof(doorFields)) == 0);
  // The bat sleeps on from where it was: one frame later its timer is one less.
  s_paused = false;
  frame();
  TEST_ASSERT(a->timer[bat] == timer - 1 && a->pc[bat] == pc);
  for (int i = 0; i < 300; i++) frame();
  TEST_ASSERT(bm_script_errors == 0);
  bmLinkMasterFree(&m);
}

static void testPpuSnapshot(void)
{
  BmLinkMaster m;
  start(&m);
  for (int i = 0; i < 60; i++) frame();  // sprites and scrolling set up
  s_paused = true;
  frame();
  static uint8_t got[128 * 1024], expected[128 * 1024];
  uint32_t size = 0;
  uint32_t frame0 = bmHalHostFrame();
  TEST_ASSERT(bmLiveMasterPpuSnapshot(&m, got, sizeof(got), &size));
  TEST_ASSERT(size == bmPpuSnapshotSize());
  bmPpuSnapshotWrite(bmHalHostGetPpu(), expected);
  TEST_ASSERT(memcmp(got, expected, size) == 0);
  static BmPpuState state;
  TEST_ASSERT(bmPpuSnapshotRead(&state, got, size));
  TEST_ASSERT((bmPpuReg(&state, BM_PPU_DISPCNT) & BM_PPU_DISPCNT_OBJ) != 0);
  printf("  snapshot of %u bytes in %u frames, %u packets, %u bad\n", (unsigned)size, (unsigned)(bmHalHostFrame() - frame0),
         (unsigned)m.counters.packetsIn, (unsigned)m.counters.badPackets);
  bmLinkMasterFree(&m);
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: test_live <level.bmpk> <variant.bmpk>\n"); return 2; }
  s_level = readFile(argv[1], &s_levelSize);
  s_variant = readFile(argv[2], &s_variantSize);
  if (argc > 3) s_scripts = readFile(argv[3], &s_scriptsSize);
  if (s_level == NULL || s_variant == NULL) { fprintf(stderr, "cannot read the packs\n"); return 2; }
  RUN_TEST(testHello);
  RUN_TEST(testHotReloadKeepsTheGameGoing);
  RUN_TEST(testStateGetAndPut);
  RUN_TEST(testRestartInAnotherRoom);
  RUN_TEST(testFailedTransferLeavesTheGameAlone);
  RUN_TEST(testPacksTakeTurnsAtTheEndsOfThePool);
  RUN_TEST(testAPackTooLargeToGoBesideReplacesTheRunningOne);
  RUN_TEST(testPushWithBitErrors);
  RUN_TEST(testPpuSnapshot);
  RUN_TEST(testScriptsArePushedLive);
  RUN_TEST(testModuleReloadKeepsTheOtherScripts);
  free(s_level);
  free(s_variant);
  return TEST_RESULT();
}
