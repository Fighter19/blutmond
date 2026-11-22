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
// GBA to GBA (BM_GAME_SEND, cartridge images): Select + Start brings a GBA on the other end of
// a link cable to where this game is, booting it first if it has no cartridge
// (docs/spec/link-protocol.md §9). With BM_SEND_FRAME that happens by itself at that frame.
//
// Host: game [pack.bmpk] [options] [out.ppm]
//   Without --frames, --trace or an output picture the game opens a window and is played with
//   keyboard or gamepad (apps/game/host_window.h); without a pack it plays the test level.
//   The window keeps the save game in a file and listens for the live link on 127.0.0.1:7414
//   (bmlink --tcp, the editor's Run button). Options:
//   --frames N  --autoplay  --overlay  --trace  --snapshot-at N file  --load file  --headless  --window
//   --scale N  --listen PORT  --no-listen  --save file  --no-save  --sound-check file
//   --follow  (wait for another device's keys: bmlink mirror)  --until-frame N  --realtime
//   --random-keys N  (keys change at random every N frames on average)
//   --room id  (start in that room)  --debug-item  (the Debug Mirror from the start)

#if defined(BM_PLATFORM_HOST) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L   // nanosleep
#endif

#include "autopilot.h"
#include "game.h"
#include "game_live.h"
#include "live.h"
#include "native.h"
#include "overlay.h"
#include "hal.h"
#include "pack.h"
#ifdef BM_GAME_SEND
#  include "transfer.h"
#endif
#include "video.h"

#include <string.h>

#ifdef BM_PLATFORM_HOST
#  include "hal_host.h"
#  ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#    include "web_editor.h"
#  endif
#  include "host_link.h"
#  ifdef BM_GAME_WINDOW
#    include "host_window.h"
#  endif
#  include <errno.h>
#  include <time.h>
#  include <sys/stat.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#else
#  include "rom_slot.h"
extern const uint8_t bm_embedded_pack[];
extern const uint8_t bm_embedded_pack_end[];

// Filled in by the editor's Export when it appends a pack to this image (src/pack/rom_slot.h).
// Volatile, so that the compiler does not take the empty values for the real ones.
__attribute__((used, aligned(4), section(".bm_rom_slot"))) const volatile BmRomPackSlot bm_rom_pack_slot = {
  { 'B', 'M', 'R', 'O', 'M', 'P', 'A', 'C', 'K', 'S', 'L', 'T' }, 0, 0 };
#endif

#ifndef BM_AUTOPLAY
#  define BM_AUTOPLAY 0
#endif
#ifndef BM_SNAPSHOT_FRAME
#  define BM_SNAPSHOT_FRAME 0
#endif

#define BM_SNAPSHOT_CAPACITY 3072

/// Snapshot mailboxes, written and read by tools (tools/bm_mgba_run --poke/--dump).
typedef struct BmStateMailbox
{
  uint32_t size;
  uint8_t data[BM_SNAPSHOT_CAPACITY];
} BmStateMailbox;
#if defined(BM_GBA_MULTIBOOT) && !defined(BM_NATIVE_SLOT_BYTES)
// A multiboot image without native code keeps them in IWRAM (ordinary .bss), which has room to
// spare; EWRAM holds the program, the embedded pack and the content pool around it.
#  define BM_MAILBOX_SECTION
#else
#  define BM_MAILBOX_SECTION BM_EWRAM_BSS
#endif
#ifdef BM_PLATFORM_GBA
// The game allocates nothing from the engine's memory heaps (src/core: the device API is for
// other programs): no EWRAM heap, its room goes to the content pool (the linker scripts'
// __bm_ewram_heap_size, 4 KB unless a program sets it).
__asm__(".global __bm_ewram_heap_size\n.set __bm_ewram_heap_size, 0");
#endif
BmStateMailbox bm_state_inbox BM_MAILBOX_SECTION;
BmStateMailbox bm_state_outbox BM_MAILBOX_SECTION;
#if defined(BM_GBA_MULTIBOOT) && !defined(BM_NATIVE_SLOT_BYTES)
uint8_t bm_live_state_out[BM_LIVE_STATE_CAPACITY];   // live.c's, here in IWRAM too
#endif

#ifdef BM_PLATFORM_HOST
static const char *s_loadPath, *s_snapshotPath;
static const char *s_startRoom;   // --room: the room the game starts in (its id)
static bool s_debugItem;          // --debug-item: the Debug Mirror from the start
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
  uint32_t soundCheck;   // checksum over every sample mixed so far
  uint32_t soundVoices;  // most voices in use at once
  uint32_t loadPeak;     // most display lines a game frame took (228 = a whole frame)
  uint32_t loadTotal;    // lines of all game frames together
  uint32_t nativeCalls;  // script functions run as native code
  uint32_t scriptCycles; // processor cycles spent in scripts (0 on the PC)
  uint32_t scriptErrors;
  uint32_t sendResult;   // GBA to GBA: 0 not sent, 1 arrived, 0x100 + BmTransferResult failed
  uint32_t sendCycles;   // processor cycles the transfer took
  uint32_t sendWords;    // words exchanged
  uint32_t sendFlags;    // 1 booted through the BIOS, 2 runtime sent through the bootstrap, 4 content sent
  uint32_t sendSame;     // the other side reported this game's room and position
  uint32_t sendRuntimeCycles;  // of sendCycles, sending the runtime through the bootstrap
  uint32_t followChecks;      // input mirror: state checksums compared with the other device's
  uint32_t followMismatches;  // ... and how many differed
  uint32_t items;        // things the player carries: the amounts of all inventory entries
  uint32_t inventoryCrc; // CRC-32 over the inventory's keys and amounts (0: empty)
} BmGameStats;
BmGameStats bm_game_stats;

static BmPack s_pack;
static BmGame s_game BM_EWRAM_BSS;
static BmAutopilot s_pilot;

#ifdef BM_NATIVE_SLOT_BYTES
// Room for a pack's native code (src/script/native.c): the images that run it have one.
uint32_t bm_native_slot[BM_NATIVE_SLOT_BYTES / 4];
uint32_t bm_native_slot_bytes = BM_NATIVE_SLOT_BYTES;   // not const: a weak constant can be folded away
#endif

// The content pool for packs that arrive over the link (src/link/live.h). On the GBA it is
// what the program leaves of EWRAM (the linker script); a multiboot image's embedded pack
// lies at its start, so its room is used again once another pack arrives.
#ifdef BM_PLATFORM_GBA
extern uint8_t __bm_pool_start[], __bm_pool_end[];
#  define POOL __bm_pool_start
#  define POOL_SIZE ((uint32_t)(__bm_pool_end - __bm_pool_start))
#else
static uint8_t s_pool[256 * 1024] __attribute__((aligned(4)));
#  define POOL s_pool
#  define POOL_SIZE ((uint32_t)sizeof(s_pool))
#endif
static BmLive s_live BM_EWRAM_BSS;
static BmGameLive s_gameLive = { &s_game, &s_pack, &s_pilot, false, { 0 } };

#ifdef BM_GAME_SEND
extern const uint8_t bm_embedded_bootstrap[], bm_embedded_bootstrap_end[];
extern const uint8_t bm_embedded_runtime[], bm_embedded_runtime_end[];
#  ifndef BM_SEND_FRAME
#    define BM_SEND_FRAME 0
#  endif

// Brings the GBA on the other end of the cable to where this game is. The game stands still
// meanwhile; afterwards it listens on the port again.
static void sendToOther(const uint8_t *pPackData, uint32_t packSize)
{
  static uint8_t state[BM_LIVE_STATE_CAPACITY] BM_EWRAM_BSS;
  BmStateBlock blocks[2];
  uint32_t n = bmGameLiveStateBlocks(&s_gameLive, blocks, 2);
  uint32_t size = bmStateWrite(blocks, n, BM_STATE_LINK, state, sizeof(state));
  BmTransferImages images = { bm_embedded_bootstrap, (uint32_t)(bm_embedded_bootstrap_end - bm_embedded_bootstrap),
                              bm_embedded_runtime, (uint32_t)(bm_embedded_runtime_end - bm_embedded_runtime) };
  BmTransferStats stats;
  bmHalLinkStop();
  uint32_t started = bmHalCycles();
  BmTransferResult result = bmTransferSend(&images, pPackData, packSize, state, size, &stats);
  bm_game_stats.sendCycles = bmHalCycles() - started;
  bm_game_stats.sendWords = stats.words;
  bm_game_stats.sendRuntimeCycles = stats.runtimeCycles;
  bm_game_stats.sendFlags = (uint32_t)(stats.usedMultiboot | (stats.usedBootstrap << 1) | (stats.sentPack << 2));
  bm_game_stats.sendResult = result == BM_TRANSFER_OK ? 1 : 0x100u + (uint32_t)result;
  bm_game_stats.sendSame = result == BM_TRANSFER_OK && stats.peer.roomIndex == s_game.roomIndex &&
                           stats.peer.x == (int16_t)BM_FIX_INT(s_game.player.x) && stats.peer.y == (int16_t)BM_FIX_INT(s_game.player.y);
  bmLiveStart(&s_live);
}
#endif

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

/// Checksum of the game state as it is now, for comparing two devices.
static uint32_t stateCrc(void)
{
  static uint8_t buffer[BM_SNAPSHOT_CAPACITY] BM_MAILBOX_SECTION;
  BmStateBlock blocks[2];
  // Only the game itself (the first block): the autopilot's own state is not played along.
  stateBlocks(blocks);
  return bmCrc32(0, buffer, bmStateWrite(blocks, 1, BM_STATE_SNAPSHOT, buffer, sizeof(buffer)));
}

#ifndef BM_OVERLAY
#  define BM_OVERLAY 0
#endif
static bool s_overlay = BM_OVERLAY;   // the debug overlay from the start (L + R + Select toggles it)
static uint32_t s_snapshotFrame = BM_SNAPSHOT_FRAME;
static bool s_waitForMirror;   // --follow: do not play until another device's keys arrive
#ifdef BM_PLATFORM_HOST
static uint32_t s_untilFrame;  // --until-frame: end when the game reaches this frame
static bool s_realtime;        // --realtime: 60 frames per second also without a window
static uint32_t s_randomKeys;  // --random-keys N: keys change at random, every N frames on average (tests)
#endif

static int run(const uint8_t *pData, uint32_t size, uint32_t frames, bool autoplay)
{
  if (bmPackOpen(&s_pack, pData, size, false) != BM_PACK_OK) return 2;
#ifdef BM_PLATFORM_HOST
  if (bmGameStart(&s_game, &s_pack, s_startRoom) != BM_SUCCESS) return 3;
  if (s_debugItem)
  {
    // --debug-item: the Debug Mirror from the start (as L + R + Start gives it).
    uint32_t key = 2166136261u;
    for (const char *n = "DebugMirror"; *n; n++) key = (key ^ (uint8_t)*n) * 16777619u;
    bmInventoryGive(&s_game.inventory, key, 1);
  }
#else
  if (bmGameStart(&s_game, &s_pack, NULL) != BM_SUCCESS) return 3;
#endif
#ifdef BM_PLATFORM_GBA
  if (!autoplay) bmGameReadSaveGame(&s_game);  // continue where the last save point was
#else
  if (s_readSave && !autoplay) bmGameReadSaveGame(&s_game);
#endif
#ifdef BM_PLATFORM_HOST
  if (s_loadPath != NULL && !loadSnapshot(s_load, s_loadSize)) return 4;
#endif
  BmLiveHooks hooks = bmGameLiveHooks(&s_gameLive);
  bmLiveInit(&s_live, &hooks, POOL, POOL_SIZE, &s_pack);
  bmLiveStart(&s_live);
  bmOverlayInit(&s_game, s_overlay);

  uint32_t lastVideoFrame = bmVideoWaitFrame();
  uint32_t catchUp = 0;
  for (uint32_t frame = 0; frames == 0 || frame < frames; frame++)
  {
    if (catchUp == 0)
    {
      uint32_t videoFrame = bmVideoWaitFrame();
      if (videoFrame != lastVideoFrame + 1) bm_game_stats.lagFrames += videoFrame - lastVideoFrame - 1;
      lastVideoFrame = videoFrame;
    }
    if (bm_state_inbox.size != 0 && !bmLiveWaiting(&s_live))
    {
      if (loadSnapshot(bm_state_inbox.data, bm_state_inbox.size)) bm_game_stats.snapshotsLoaded++;
      bm_state_inbox.size = 0;
    }
    bmLiveFrame(&s_live);
    if (bmLiveWaiting(&s_live))
    {
      // The pack is being replaced over itself: nothing of it may be read until the new one
      // is applied. The picture stays; the sound is quiet.
      int8_t *left, *right;
      bmHalAudioBuffers(&left, &right);
      memset(left, 0, BM_HAL_AUDIO_FRAME_SAMPLES);
      memset(right, 0, BM_HAL_AUDIO_FRAME_SAMPLES);
#ifdef BM_PLATFORM_HOST
      if (s_frameHook != NULL && !s_frameHook()) break;
#endif
      frame--;
      continue;
    }
    if (bmLiveCapturing(&s_live))
    {
      // The video state is being read over the link: hold still until it is through.
#ifdef BM_PLATFORM_HOST
      if (s_frameHook != NULL && !s_frameHook()) break;
#endif
      frame--;
      continue;
    }
    uint16_t keys = autoplay ? bmAutopilotKeys(&s_pilot, &s_game) : bmHalInputKeys();
#ifdef BM_PLATFORM_HOST
    if (s_randomKeys != 0)
    {
      // Someone mashing the buttons: mostly running and jumping, now and then anything.
      static uint32_t seed = 12345;
      static uint16_t held;
      seed = seed * 1664525u + 1013904223u;
      if ((seed >> 8) % s_randomKeys == 0)
      {
        seed = seed * 1664525u + 1013904223u;
        uint32_t r = seed >> 8;
        held = (uint16_t)((r & 1 ? BM_KEY_RIGHT : 0) | (r & 6 ? 0 : BM_KEY_LEFT) | (r & 8 ? BM_KEY_A : 0) | (r & 0x30 ? 0 : BM_KEY_UP) |
                          (r & 0xC0 ? 0 : BM_KEY_DOWN) | (r & 0x700 ? 0 : BM_KEY_B));
      }
      keys = held;
    }
#endif
    if (bmLiveFollowing(&s_live) || s_waitForMirror)
    {
      // Another device plays and this one plays along (link-protocol.md §10): the same keys
      // for the same frames. Until they are known for the next frame, hold still.
      if (!bmLiveFollowing(&s_live) || !bmLiveFollowKeys(&s_live, s_game.frame, &keys))
      {
#ifdef BM_PLATFORM_HOST
        struct timespec nap = { 0, 1000000 };
        nanosleep(&nap, NULL);
        if (s_frameHook != NULL && !s_frameHook()) break;
#endif
        catchUp = 0;   // waiting is paced by the screen again
        frame--;
        continue;
      }
      s_waitForMirror = false;
      if (bmLiveFollowWantsCheck(&s_live, s_game.frame)) bmLiveFollowCheck(&s_live, s_game.frame, stateCrc());
      // Behind: catch up with a second frame before the next screen refresh (one, so the
      // sound stays whole). Far behind: ask for the other side's state instead.
      uint32_t behind = bmLiveFollowAhead(&s_live, s_game.frame);
      if (behind > BM_LIVE_FOLLOW_MAX_BEHIND) bmLiveFollowBehind(&s_live, s_game.frame);
      catchUp = behind > 2 && catchUp == 0 ? 1 : 0;
      bm_game_stats.followChecks = s_live.followChecks;
      bm_game_stats.followMismatches = s_live.followMismatches;
    }
#ifdef BM_PLATFORM_HOST
    if (getenv("BM_STATE_DUMP") != NULL && s_game.frame % 64 == 0)
    {
      // For finding what differs between two runs: the state before every 64th frame.
      char path[600];
      snprintf(path, sizeof(path), "%s/%u.bmst", getenv("BM_STATE_DUMP"), (unsigned)s_game.frame);
      takeSnapshot(&bm_state_outbox);
      FILE *df = fopen(path, "wb");
      if (df != NULL) { fwrite(bm_state_outbox.data, 1, bm_state_outbox.size, df); fclose(df); }
    }
#endif
    bmLiveInput(&s_live, s_game.frame, keys);
    if (bmLiveInputWantsCheck(&s_live, s_game.frame)) bmLiveInputCheck(&s_live, s_game.frame, stateCrc());
#ifdef BM_GAME_SEND
    {
      static bool s_held;
      uint16_t pad = bmHalInputKeys();
      bool both = (pad & (BM_KEY_SELECT | BM_KEY_START)) == (BM_KEY_SELECT | BM_KEY_START);
      if ((both && !s_held) || (BM_SEND_FRAME != 0 && s_game.frame == BM_SEND_FRAME && bm_game_stats.sendResult == 0))
      {
        sendToOther(pData, size);
        lastVideoFrame = bmVideoWaitFrame();   // the frames the transfer took are not missed ones
      }
      s_held = both;
    }
#endif
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
    bm_game_stats.items = 0;
    for (uint32_t k = 0; k < s_game.inventory.count; k++) bm_game_stats.items += s_game.inventory.amount[k];
    bm_game_stats.inventoryCrc = s_game.inventory.count == 0 ? 0
                               : bmCrc32(bmCrc32(0, s_game.inventory.key, s_game.inventory.count * 4u), s_game.inventory.amount, s_game.inventory.count);
    bm_game_stats.gameFrame = s_game.frame;
    bm_game_stats.liveApplied = s_live.applied;
    bm_game_stats.soundCheck = s_game.sound.outputCrc;
    bm_game_stats.soundVoices = s_game.sound.voicesPeak;
    // How long the frame took: lines since the vertical blank began (at line 160).
    uint32_t lines = (uint32_t)(bmHalVideoLine() + 228 - 160) % 228;
    if (lines > bm_game_stats.loadPeak) bm_game_stats.loadPeak = lines;
    bm_game_stats.loadTotal += lines;
    bmOverlayFrame(&s_game, keys, lines);
    bm_game_stats.nativeCalls = bm_native_calls;
    bm_game_stats.scriptCycles = bm_script_cycles;
    bm_game_stats.scriptErrors = bm_script_errors;
#ifdef BM_PLATFORM_HOST
    if (s_frameHook != NULL && !s_frameHook()) break;
    if (s_untilFrame != 0 && s_game.frame >= s_untilFrame) break;
    if (s_realtime || s_live.inputStream)
    {
      // 60 frames per second without a window; and while another device plays along, not
      // faster than a GBA draws frames (59.7275 per second), or it could never keep up.
      static struct timespec next;
      struct timespec nowTime;
      const long period = s_live.inputStream ? 16742706 : 16666667;
      clock_gettime(CLOCK_MONOTONIC, &nowTime);
      if (next.tv_sec == 0 || nowTime.tv_sec - next.tv_sec > 1) next = nowTime;
      next.tv_nsec += period;
      if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
      if (nowTime.tv_sec < next.tv_sec || (nowTime.tv_sec == next.tv_sec && nowTime.tv_nsec < next.tv_nsec))
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
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

#  ifdef __EMSCRIPTEN__
// Run from the editor in the browser (web_editor.h): the pack the game plays came from the
// editor, and the editor sends newer ones.
static bool s_fromEditor;
static uint8_t *s_editorPack;

// At a frame boundary: a pack the editor sent is put in place, from the start (mode 0) or as a
// reload that keeps the game going and the scripts that did not change (mode 3).
static void takeEditorPack(void)
{
  uint32_t size = 0;
  int mode = 0;
  uint8_t *data = bmWebTakePack(&size, &mode);
  if (data == NULL) return;
  BmPack pack;
  BmLiveHooks hooks = bmGameLiveHooks(&s_gameLive);
  if (bmPackOpen(&pack, data, size, true) == BM_PACK_OK &&
      hooks.usePack(hooks.pContext, &pack, mode == 0 ? BM_LIVE_RESTART : BM_LIVE_MODULE, NULL))
  {
    free(s_editorPack);   // the game no longer reads the old one
    s_editorPack = data;
    printf("game: pack from the editor, %u bytes, %s\n", (unsigned)size, mode == 0 ? "from the start" : "reloaded");
    if (mode == 0) memset(&s_pilot, 0, sizeof(s_pilot));
  }
  else
  {
    fprintf(stderr, "game: the pack from the editor was not used\n");
    free(data);
  }
}
#  endif

#  ifdef BM_GAME_WINDOW
static bool windowFrame(void)
{
  static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmHalHostRender(pixels);
  uint16_t keys = 0;
  if (!bmWindowFrame(pixels, &keys)) return false;
  bmHalHostSetKeys(keys);
  if (bm_game_stats.frames % 60 == 0) writeSaveFile();
#    ifdef __EMSCRIPTEN__
  if (s_fromEditor) takeEditorPack();
#    endif
  return true;
}
#  endif

int main(int argc, char **argv)
{
  const char *packPath = NULL, *out = NULL;
  uint32_t frames = 1;
  bool autoplay = BM_AUTOPLAY;
  bool trace = false;
  bool framesGiven = false, headless = false, window = false, listen = true, listenGiven = false, save = true;
  const char *soundCheckPath = NULL;
  int scale = 3, port = BM_HOST_LINK_DEFAULT_PORT;
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { frames = (uint32_t)atoi(argv[++i]); framesGiven = true; }
    else if (strcmp(argv[i], "--headless") == 0) headless = true;
    else if (strcmp(argv[i], "--window") == 0) window = true;
    else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
    else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) { port = atoi(argv[++i]); listen = true; listenGiven = true; }
    else if (strcmp(argv[i], "--no-listen") == 0) listen = false;
    else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) s_savePath = argv[++i];
    else if (strcmp(argv[i], "--no-save") == 0) save = false;
    else if (strcmp(argv[i], "--sound-check") == 0 && i + 1 < argc) soundCheckPath = argv[++i];
    else if (strcmp(argv[i], "--autoplay") == 0) autoplay = true;
    else if (strcmp(argv[i], "--overlay") == 0) s_overlay = true;
    else if (strcmp(argv[i], "--room") == 0 && i + 1 < argc) s_startRoom = argv[++i];
    else if (strcmp(argv[i], "--debug-item") == 0) s_debugItem = true;
    else if (strcmp(argv[i], "--follow") == 0) s_waitForMirror = true;
    else if (strcmp(argv[i], "--realtime") == 0) s_realtime = true;
    else if (strcmp(argv[i], "--random-keys") == 0 && i + 1 < argc) s_randomKeys = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--until-frame") == 0 && i + 1 < argc) s_untilFrame = (uint32_t)atoi(argv[++i]);
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
#  ifdef __EMSCRIPTEN__
  listen = false;  // a page has no sockets to listen on; the save game lives as long as the page
  s_fromEditor = bmWebFromEditor();
  if (s_fromEditor) save = false;
#  endif
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
  // Without a window the link is only listened to when asked for (--listen with a port).
  bool headlessLink = !window && listenGiven;
  if (headlessLink) bmHostLinkListen((uint16_t)port);
  if (s_waitForMirror || s_untilFrame != 0) frames = 0;
  uint8_t *data = NULL;
  long size = 0;
#  ifdef __EMSCRIPTEN__
  if (s_fromEditor)
  {
    // The first pack comes from the editor.
    bmWebListen();
    uint32_t editorSize = 0;
    int mode = 0;
    while ((data = bmWebTakePack(&editorSize, &mode)) == NULL) emscripten_sleep(20);
    size = (long)editorSize;
    s_editorPack = data;
  }
#  endif
  if (data == NULL)
  {
    FILE *f = fopen(packPath, "rb");
    if (f == NULL) { perror(packPath); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 1; }
    fclose(f);
  }

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
  if (headlessLink)
  {
    bmHostLinkStop();
    printf("frame %u, %u state checks against the other device, %u different\n", (unsigned)s_game.frame,
           (unsigned)bm_game_stats.followChecks, (unsigned)bm_game_stats.followMismatches);
    if (bm_game_stats.followMismatches != 0) result = 1;
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
  if (result == 0 && soundCheckPath != NULL)
  {
    // The checksum of all sound mixed, to compare with the GBA's (bm_mgba_run --check-file).
    FILE *cf = fopen(soundCheckPath, "w");
    if (cf != NULL)
    {
      fprintf(cf, "%u\n", (unsigned)bm_game_stats.soundCheck);
      fclose(cf);
    }
  }
  if (result == 0 && out != NULL)
  {
    // The picture after the last frame: sprites reach the screen at the next vertical blank.
    bmVideoWaitFrame();
    static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
    bmHalHostRender(pixels);
    result = writePpm(out, pixels);
  }
  if (result == 0)
  {
    printf("room %s, player %d,%d, deaths %u, hurts %u, room changes %u after %u frames, items %u\n", bmGameRoomId(&s_game),
           (int)BM_FIX_INT(s_game.player.x), (int)BM_FIX_INT(s_game.player.y), (unsigned)s_game.deaths,
           (unsigned)s_game.hurts, (unsigned)s_game.roomChanges, frames, (unsigned)bm_game_stats.items);
  }
#  ifdef __EMSCRIPTEN__
  if (s_fromEditor) data = s_editorPack;   // the first pack may have been replaced (and freed) since
#  endif
  free(data);
  return result;
}
#else
int main(void)
{
  uint32_t size = bm_rom_pack_slot.size;
  if (size != 0)  // an exported game: the pack follows the program in ROM
    return run((const uint8_t *)(0x08000000u + bm_rom_pack_slot.offset), size, 0, BM_AUTOPLAY);
  return run(bm_embedded_pack, (uint32_t)(bm_embedded_pack_end - bm_embedded_pack), 0, BM_AUTOPLAY);
}
#endif
