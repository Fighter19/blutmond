/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The test level as a game: D-pad to run, A to jump, up/down on ladders, down + A to drop
// through platforms. With BM_AUTOPLAY (or --autoplay on the host) the autopilot plays.
//
// GBA: the pack is embedded. Host: game <pack.bmpk> [--frames N] [--autoplay] [out.ppm]

#include "autopilot.h"
#include "device.h"
#include "game.h"
#include "hal.h"
#include "pack.h"
#include "physical_device.h"
#include "video.h"

#ifdef BM_PLATFORM_HOST
#  include "hal_host.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#else
extern const uint8_t bm_embedded_pack[];
extern const uint8_t bm_embedded_pack_end[];
#endif

#ifndef BM_AUTOPLAY
#  define BM_AUTOPLAY 0
#endif

/// Numbers tests read from the running program (tools/bm_mgba_run --check).
typedef struct BmGameStats
{
  uint32_t frames;       // game frames run
  uint32_t lagFrames;    // video frames missed because a game frame took too long
  uint32_t deaths;
  uint32_t roomChanges;
  uint32_t roomIndex;
} BmGameStats;
BmGameStats bm_game_stats;

static BmPack s_pack;
static BmGame s_game BM_EWRAM_BSS;
static BmAutopilot s_pilot;

static int run(const uint8_t *pData, uint32_t size, uint32_t frames, bool autoplay)
{
  BmDeviceHandle device = NULL;
  if (bmCreateDevice(bmGetPhysicalDevice(), &device) != BM_SUCCESS) return 1;
  g_defaultDevice = device;
  if (bmPackOpen(&s_pack, pData, size, false) != BM_PACK_OK) return 2;
  if (bmGameStart(&s_game, &s_pack, NULL) != BM_SUCCESS) return 3;

  uint32_t lastVideoFrame = bmVideoWaitFrame();
  for (uint32_t frame = 0; frames == 0 || frame < frames; frame++)
  {
    uint32_t videoFrame = bmVideoWaitFrame();
    if (videoFrame != lastVideoFrame + 1) bm_game_stats.lagFrames += videoFrame - lastVideoFrame - 1;
    lastVideoFrame = videoFrame;
    uint16_t keys = autoplay ? bmAutopilotKeys(&s_pilot, &s_game) : bmHalInputKeys();
    bmGameFrame(&s_game, keys);
    bm_game_stats.frames++;
    bm_game_stats.deaths = s_game.deaths;
    bm_game_stats.roomChanges = s_game.roomChanges;
    bm_game_stats.roomIndex = s_game.roomIndex;
  }
  return 0;
}

#ifdef BM_PLATFORM_HOST
static int writePpm(const char *path, const uint16_t *pixels)
{
  FILE *f = fopen(path, "wb");
  if (f == NULL) return 1;
  fprintf(f, "P6\n%d %d\n255\n", BM_PPU_WIDTH, BM_PPU_HEIGHT);
  for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
  {
    uint8_t rgb[3];
    for (int c = 0; c < 3; c++)
    {
      uint8_t v = (uint8_t)((pixels[i] >> (5 * c)) & 31);
      rgb[c] = (uint8_t)((v << 3) | (v >> 2));
    }
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return 0;
}

int main(int argc, char **argv)
{
  const char *packPath = NULL, *out = NULL;
  uint32_t frames = 1;
  bool autoplay = BM_AUTOPLAY;
  bool trace = false;
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--autoplay") == 0) autoplay = true;
    else if (strcmp(argv[i], "--trace") == 0) trace = true;
    else if (packPath == NULL) packPath = argv[i];
    else out = argv[i];
  }
  if (packPath == NULL)
  {
    fprintf(stderr, "usage: game <pack.bmpk> [--frames N] [--autoplay] [--trace] [out.ppm]\n");
    return 2;
  }
  FILE *f = fopen(packPath, "rb");
  if (f == NULL) { perror(packPath); return 1; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)size);
  if (fread(data, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 1; }
  fclose(f);

  int result;
  if (trace)
  {
    // Frame by frame, printing the player's state (for tuning levels and the autopilot).
    result = run(data, (uint32_t)size, 0 + 1, autoplay);
    for (uint32_t i = 1; result == 0 && i < frames; i++)
    {
      bmVideoWaitFrame();
      uint16_t keys = autoplay ? bmAutopilotKeys(&s_pilot, &s_game) : 0;
      bmGameFrame(&s_game, keys);
      if (i % 10 == 0)
        printf("%4u %-12s x=%4d y=%4d vx=%5d vy=%5d ground=%d climb=%d keys=%03x deaths=%u\n", i, bmGameRoomId(&s_game),
               (int)BM_FIX_INT(s_game.player.x), (int)BM_FIX_INT(s_game.player.y), (int)s_game.player.vx,
               (int)s_game.player.vy, s_game.player.onGround, s_game.player.climbing, keys, (unsigned)s_game.deaths);
    }
  }
  else
  {
    result = run(data, (uint32_t)size, frames, autoplay);
  }
  if (result == 0 && out != NULL)
  {
    static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
    bmHalHostRender(pixels);
    result = writePpm(out, pixels);
  }
  if (result == 0)
  {
    printf("room %s, player %d,%d, deaths %u, hurts %u, room changes %u after %u frames\n", bmGameRoomId(&s_game),
           (int)BM_FIX_INT(s_game.player.x), (int)BM_FIX_INT(s_game.player.y), (unsigned)s_game.deaths,
           (unsigned)s_game.hurts, (unsigned)s_game.roomChanges, frames);
  }
  free(data);
  return result;
}
#else
int main(void)
{
  return run(bm_embedded_pack, (uint32_t)(bm_embedded_pack_end - bm_embedded_pack), 0, BM_AUTOPLAY);
}
#endif
