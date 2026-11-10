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

#define CONTENT_SIZE (64 * 1024)
#define WORDS_PER_FRAME 128   // about 30 KB/s at 60 frames per second

static uint8_t *s_level, *s_variant;
static uint32_t s_levelSize, s_variantSize;

static BmPack s_pack;
static BmGame s_game;
static BmAutopilot s_pilot;
static BmGameLive s_gameLive = { &s_game, &s_pack, &s_pilot };
static BmLive s_live;
static uint8_t s_content[2][CONTENT_SIZE];

typedef struct Wire
{
  uint32_t words;
  uint32_t rng;
  uint32_t flipPerMillion;
  uint32_t flips;
} Wire;
static Wire s_wire;

static void frame(void)
{
  bmVideoWaitFrame();
  bmLiveFrame(&s_live);
  bmGameFrame(&s_game, bmAutopilotKeys(&s_pilot, &s_game));
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

static void start(BmLinkMaster *m)
{
  bmHalHostReset();
  BmDeviceHandle device = NULL;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  bmPackOpen(&s_pack, s_level, s_levelSize, true);
  bmGameStart(&s_game, &s_pack, NULL);
  memset(&s_pilot, 0, sizeof(s_pilot));
  BmLiveHooks hooks = bmGameLiveHooks(&s_gameLive);
  bmLiveInit(&s_live, &hooks, s_content[0], s_content[1], CONTENT_SIZE, &s_pack);
  bmLiveStart(&s_live);
  memset(&s_wire, 0, sizeof(s_wire));
  s_wire.rng = 99;
  BmLinkTransport t = { NULL, exchange, NULL, NULL, 0 };
  bmLinkMasterInit(m, &t);
  for (int i = 0; i < 30; i++) frame();
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
  TEST_ASSERT(r.hello.contentCapacity == CONTENT_SIZE);
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

  // A second push goes into the other buffer; the first one is what the game uses now.
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
  uint8_t snap[1024];
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
  TEST_ASSERT(bmLiveMasterApply(&m, 0, BM_LIVE_MODULE, NULL, &st));
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

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: test_live <level.bmpk> <variant.bmpk>\n"); return 2; }
  s_level = readFile(argv[1], &s_levelSize);
  s_variant = readFile(argv[2], &s_variantSize);
  if (s_level == NULL || s_variant == NULL) { fprintf(stderr, "cannot read the packs\n"); return 2; }
  RUN_TEST(testHello);
  RUN_TEST(testHotReloadKeepsTheGameGoing);
  RUN_TEST(testStateGetAndPut);
  RUN_TEST(testRestartInAnotherRoom);
  RUN_TEST(testFailedTransferLeavesTheGameAlone);
  RUN_TEST(testPushWithBitErrors);
  free(s_level);
  free(s_variant);
  return TEST_RESULT();
}
