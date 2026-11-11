/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The test level as a game: D-pad to run, A to jump, up/down on ladders, down + A to drop
// through platforms. With BM_AUTOPLAY (or --autoplay on the host) the autopilot plays.
//
// Snapshots (save states, docs/spec/state.md) can be taken and loaded on both platforms, and
// move between them: the bytes are the same.
//
// GBA: the pack is embedded. A snapshot written into bm_state_inbox is loaded at the next
// frame; with BM_SNAPSHOT_FRAME the game writes one into bm_state_outbox at that frame.
// The manual game continues from the save game if there is one.
//
// Live link: the game listens on the serial port (docs/spec/link-protocol.md); a master can
// push packs and state into it while it runs (tools/bmlink, tools/bm_mgba_run --link).
//
// Host: game [pack.bmpk] [options] [out.ppm]
//   Without --frames, --trace or an output picture the game opens a window and is played with
//   keyboard or gamepad (apps/game/host_window.h); without a pack it plays the test level.
//   The window keeps the save game in a file and listens for the live link on 127.0.0.1:7414
//   (bmlink --tcp, the editor's Run button). Options:
//   --frames N  --autoplay  --trace  --snapshot-at N file  --load file  --headless  --window
//   --scale N  --listen PORT  --no-listen  --save file  --no-save

#include "autopilot.h"
#include "device.h"
#include "game.h"
#include "game_live.h"
#include "live.h"
#include "hal.h"
#include "pack.h"
#include "physical_device.h"
#include "video.h"

#ifdef BM_PLATFORM_HOST
#  include "hal_host.h"
#  include "host_link.h"
#  ifdef BM_GAME_WINDOW
#    include "host_window.h"
#  endif
#  include <errno.h>
#  include <sys/stat.h>
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
#ifndef BM_SNAPSHOT_FRAME
#  define BM_SNAPSHOT_FRAME 0
#endif

#define BM_SNAPSHOT_CAPACITY 512

/// Snapshot mailboxes, written and read by tools (tools/bm_mgba_run --poke/--dump).
typedef struct BmStateMailbox
{
  uint32_t size;
  uint8_t data[BM_SNAPSHOT_CAPACITY];
} BmStateMailbox;
BmStateMailbox bm_state_inbox BM_EWRAM_BSS;
BmStateMailbox bm_state_outbox BM_EWRAM_BSS;

#ifdef BM_PLATFORM_HOST
static const char *s_loadPath, *s_snapshotPath;
static uint8_t s_load[BM_SNAPSHOT_CAPACITY];
static uint32_t s_loadSize;
static void writeSnapshotFile(void);
/// Called after every game frame in window mode; false ends the game.
static bool (*s_frameHook)(void);
static bool s_readSave;
#endif

/// Numbers tests read from the running program (tools/bm_mgba_run --check).
typedef struct BmGameStats
{
  uint32_t frames;       // game frames run
  uint32_t lagFrames;    // video frames missed because a game frame took too long
  uint32_t deaths;
  uint32_t roomChanges;
  uint32_t roomIndex;
  uint32_t snapshotsLoaded;
  uint32_t gameFrame;    // the game's own frame counter (part of the state)
  uint32_t liveApplied;  // live reloads applied (packs and state)
} BmGameStats;
BmGameStats bm_game_stats;

static BmPack s_pack;
static BmGame s_game BM_EWRAM_BSS;
static BmAutopilot s_pilot;

#ifndef BM_LIVE_CONTENT_SIZE
#  define BM_LIVE_CONTENT_SIZE (64 * 1024)
#endif
static uint8_t s_content[2][BM_LIVE_CONTENT_SIZE] BM_EWRAM_BSS;
static BmLive s_live BM_EWRAM_BSS;
static BmGameLive s_gameLive = { &s_game, &s_pack, &s_pilot };

static uint32_t stateBlocks(BmStateBlock blocks[2])
{
  return bmGameLiveStateBlocks(&s_gameLive, blocks, 2);
}

static void takeSnapshot(BmStateMailbox *pBox)
{
  BmStateBlock blocks[2];
  uint32_t n = stateBlocks(blocks);
  pBox->size = bmStateWrite(blocks, n, BM_STATE_SNAPSHOT, pBox->data, sizeof(pBox->data));
}

static bool loadSnapshot(const uint8_t *pData, uint32_t size)
{
  BmStateBlock blocks[2];
  uint32_t n = stateBlocks(blocks);
  return bmStateRead(blocks, n, pData, size) == BM_STATE_OK && bmGameAfterLoad(&s_game) == BM_SUCCESS;
}

static uint32_t s_snapshotFrame = BM_SNAPSHOT_FRAME;

static int run(const uint8_t *pData, uint32_t size, uint32_t frames, bool autoplay)
{
  BmDeviceHandle device = NULL;
  if (bmCreateDevice(bmGetPhysicalDevice(), &device) != BM_SUCCESS) return 1;
  g_defaultDevice = device;
  if (bmPackOpen(&s_pack, pData, size, false) != BM_PACK_OK) return 2;
  if (bmGameStart(&s_game, &s_pack, NULL) != BM_SUCCESS) return 3;
#ifdef BM_PLATFORM_GBA
  if (!autoplay) bmGameReadSaveGame(&s_game);  // continue where the last save point was
#else
  if (s_readSave && !autoplay) bmGameReadSaveGame(&s_game);
#endif
#ifdef BM_PLATFORM_HOST
  if (s_loadPath != NULL && !loadSnapshot(s_load, s_loadSize)) return 4;
#endif
  BmLiveHooks hooks = bmGameLiveHooks(&s_gameLive);
  bmLiveInit(&s_live, &hooks, s_content[0], s_content[1], BM_LIVE_CONTENT_SIZE, &s_pack);
  bmLiveStart(&s_live);

  uint32_t lastVideoFrame = bmVideoWaitFrame();
  for (uint32_t frame = 0; frames == 0 || frame < frames; frame++)
  {
    uint32_t videoFrame = bmVideoWaitFrame();
    if (videoFrame != lastVideoFrame + 1) bm_game_stats.lagFrames += videoFrame - lastVideoFrame - 1;
    lastVideoFrame = videoFrame;
    if (bm_state_inbox.size != 0)
    {
      if (loadSnapshot(bm_state_inbox.data, bm_state_inbox.size)) bm_game_stats.snapshotsLoaded++;
      bm_state_inbox.size = 0;
    }
    bmLiveFrame(&s_live);
    uint16_t keys = autoplay ? bmAutopilotKeys(&s_pilot, &s_game) : bmHalInputKeys();
    bmGameFrame(&s_game, keys);
    if (s_snapshotFrame != 0 && s_game.frame == s_snapshotFrame)
    {
      takeSnapshot(&bm_state_outbox);
#ifdef BM_PLATFORM_HOST
      writeSnapshotFile();
#endif
    }
    bm_game_stats.frames++;
    bm_game_stats.deaths = s_game.deaths;
    bm_game_stats.roomChanges = s_game.roomChanges;
    bm_game_stats.roomIndex = s_game.roomIndex;
    bm_game_stats.gameFrame = s_game.frame;
    bm_game_stats.liveApplied = s_live.applied;
#ifdef BM_PLATFORM_HOST
    if (s_frameHook != NULL && !s_frameHook()) break;
#endif
  }
  return 0;
}

#ifdef BM_PLATFORM_HOST
static void writeSnapshotFile(void)
{
  if (s_snapshotPath == NULL) return;
  FILE *f = fopen(s_snapshotPath, "wb");
  if (f == NULL) return;
  fwrite(bm_state_outbox.data, 1, bm_state_outbox.size, f);
  fclose(f);
  printf("snapshot at frame %u: %u bytes to %s\n", (unsigned)s_game.frame, (unsigned)bm_state_outbox.size, s_snapshotPath);
}

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

// --- Window mode ---------------------------------------------------------------------------

static const char *s_savePath;
static uint8_t s_saveWritten[BM_HAL_SAVE_SIZE];

static void defaultSavePath(void)
{
  static char path[1024];
  const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
  if (data != NULL && data[0] != 0) snprintf(path, sizeof(path), "%s/blutmond", data);
  else if (home != NULL) snprintf(path, sizeof(path), "%s/.local/share/blutmond", home);
  else return;
#  if defined(__unix__) || defined(__APPLE__)
  char parent[1024];
  snprintf(parent, sizeof(parent), "%s", path);
  char *slash = strrchr(parent, '/');
  if (slash != NULL) { *slash = 0; mkdir(parent, 0755); }
  if (mkdir(path, 0755) != 0 && errno != EEXIST) return;
#  endif
  strncat(path, "/save.bin", sizeof(path) - strlen(path) - 1);
  s_savePath = path;
}

static void readSaveFile(void)
{
  FILE *f = s_savePath ? fopen(s_savePath, "rb") : NULL;
  if (f == NULL) return;
  size_t n = fread(bmHalHostSaveMemory(), 1, BM_HAL_SAVE_SIZE, f);
  fclose(f);
  memcpy(s_saveWritten, bmHalHostSaveMemory(), BM_HAL_SAVE_SIZE);
  s_readSave = n > 0;
}

static void writeSaveFile(void)
{
  if (s_savePath == NULL || memcmp(s_saveWritten, bmHalHostSaveMemory(), BM_HAL_SAVE_SIZE) == 0) return;
  FILE *f = fopen(s_savePath, "wb");
  if (f == NULL) return;
  fwrite(bmHalHostSaveMemory(), 1, BM_HAL_SAVE_SIZE, f);
  fclose(f);
  memcpy(s_saveWritten, bmHalHostSaveMemory(), BM_HAL_SAVE_SIZE);
}

#  ifdef BM_GAME_WINDOW
static bool windowFrame(void)
{
  static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmHalHostRender(pixels);
  uint16_t keys = 0;
  if (!bmWindowFrame(pixels, &keys)) return false;
  bmHalHostSetKeys(keys);
  if (bm_game_stats.frames % 60 == 0) writeSaveFile();
  return true;
}
#  endif

int main(int argc, char **argv)
{
  const char *packPath = NULL, *out = NULL;
  uint32_t frames = 1;
  bool autoplay = BM_AUTOPLAY;
  bool trace = false;
  bool framesGiven = false, headless = false, window = false, listen = true, save = true;
  int scale = 3, port = BM_HOST_LINK_DEFAULT_PORT;
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { frames = (uint32_t)atoi(argv[++i]); framesGiven = true; }
    else if (strcmp(argv[i], "--headless") == 0) headless = true;
    else if (strcmp(argv[i], "--window") == 0) window = true;
    else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
    else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) { port = atoi(argv[++i]); listen = true; }
    else if (strcmp(argv[i], "--no-listen") == 0) listen = false;
    else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) s_savePath = argv[++i];
    else if (strcmp(argv[i], "--no-save") == 0) save = false;
    else if (strcmp(argv[i], "--autoplay") == 0) autoplay = true;
    else if (strcmp(argv[i], "--trace") == 0) trace = true;
    else if (strcmp(argv[i], "--snapshot-at") == 0 && i + 2 < argc)
    {
      s_snapshotFrame = (uint32_t)atoi(argv[++i]);
      s_snapshotPath = argv[++i];
    }
    else if (strcmp(argv[i], "--load") == 0 && i + 1 < argc)
    {
      s_loadPath = argv[++i];
      FILE *lf = fopen(s_loadPath, "rb");
      if (lf == NULL) { perror(s_loadPath); return 1; }
      s_loadSize = (uint32_t)fread(s_load, 1, sizeof(s_load), lf);
      fclose(lf);
    }
    else if (argv[i][0] == '-')
    {
      fprintf(stderr, "unknown option %s (see the comment at the top of apps/game/main.c)\n", argv[i]);
      return 2;
    }
    else if (packPath == NULL) packPath = argv[i];
    else out = argv[i];
  }
  if (!window) window = !headless && !framesGiven && !trace && out == NULL;
#  ifdef BM_GAME_DEFAULT_PACK
  if (packPath == NULL) packPath = BM_GAME_DEFAULT_PACK;
#  endif
  if (packPath == NULL)
  {
    fprintf(stderr, "usage: game <pack.bmpk> [options] [out.ppm] (see apps/game/main.c)\n");
    return 2;
  }
  if (window)
  {
#  ifdef BM_GAME_WINDOW
    if (save)
    {
      if (s_savePath == NULL) defaultSavePath();
      readSaveFile();
    }
    char title[512];
    const char *name = strrchr(packPath, '/');
    snprintf(title, sizeof(title), "Blutmond - %s", name ? name + 1 : packPath);
    if (!bmWindowOpen(title, scale)) return 1;
    if (listen) bmHostLinkListen((uint16_t)port);
    s_frameHook = windowFrame;
    frames = 0;
#  else
    (void)scale; (void)port; (void)listen; (void)save;
    fprintf(stderr, "game: built without SDL3, so there is no window; use --frames N [out.ppm]\n");
    return 2;
#  endif
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
#  ifdef BM_GAME_WINDOW
  if (window)
  {
    bmHostLinkStop();
    writeSaveFile();
    bmWindowClose();
    frames = bm_game_stats.frames;
  }
#  endif
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
