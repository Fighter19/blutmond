/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// bmlink: talks to a running Blutmond runtime over the live link (docs/spec/link-protocol.md).
//
//   bmlink <transport> [options] <command>...
//
// Transports:
//   --xboo <device>       Xboo cable on a parallel port (Linux ppdev, e.g. /dev/parport0)
//   --mgba <image.gba>    the image running in mGBA (tests; only if built with libmgba)
//
// Options (before the commands they affect):
//   --delay N             per-word delay (xcomms "Transfer Delay"); remembered per device
//   --adaptive            lower the delay while transfers are clean, raise it after errors
//   --reset none|xboo     whether the reset wire is connected (default none)
//   --cycles-per-word N   mGBA: emulated time per word (default 2048, about 8000 words/s)
//   --errors FLIP MISS    mGBA: bit flips and lost words per million words
//
// Commands, run in order:
//   boot <image.gba>      xboo: multiboot the runtime (as xcomms does)
//   reset                 pull the reset wire (with --reset xboo)
//   hello                 what the runtime runs and has
//   ping [count]          round trips, prints the rate
//   push <pack> [hot|restart[:room]]   send what changed and apply it (default hot)
//   state-get <file> [save|link|snapshot]
//   state-put <file>      load a state (reload mode "state")
//   run <frames>          mGBA: let the game run
//   check <elf>:<sym>[+off][=|<=]<value>   mGBA: read the program's memory
//   screenshot <file.ppm> mGBA
//   stats                 link counters

#include "link_master.h"
#include "live_master.h"
#include "xboo.h"
#ifdef BM_HAVE_MGBA
#  include "mgba_link.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static uint8_t *readFile(const char *path, uint32_t *pSize)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) { perror(path); return NULL; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *p = malloc((size_t)n + 1);
  if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
  fclose(f);
  *pSize = (uint32_t)n;
  return p;
}

static double now(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

// Remembered delay per device: ~/.config/blutmond/link.conf, lines "<device> <delay>".
static const char *configPath(void)
{
  static char path[512];
  const char *home = getenv("HOME");
  if (home == NULL) return NULL;
  snprintf(path, sizeof(path), "%s/.config/blutmond/link.conf", home);
  return path;
}

static bool loadDelay(const char *device, uint32_t *pDelay)
{
  const char *path = configPath();
  FILE *f = path ? fopen(path, "r") : NULL;
  if (f == NULL) return false;
  char name[400];
  unsigned d;
  bool found = false;
  while (fscanf(f, "%399s %u", name, &d) == 2)
    if (strcmp(name, device) == 0) { *pDelay = d; found = true; }
  fclose(f);
  return found;
}

static void saveDelay(const char *device, uint32_t delay)
{
  const char *path = configPath();
  if (path == NULL) return;
  char dir[512];
  snprintf(dir, sizeof(dir), "%s", path);
  for (char *p = dir + 1; *p; p++)
  {
    if (*p != '/') continue;
    *p = 0;
    mkdir(dir, 0755);  // fails harmlessly if it exists
    *p = '/';
  }
  // Keep the other devices' lines.
  char lines[64][420];
  int count = 0;
  FILE *f = fopen(path, "r");
  if (f != NULL)
  {
    char name[400];
    unsigned d;
    while (count < 64 && fscanf(f, "%399s %u", name, &d) == 2)
      if (strcmp(name, device) != 0) snprintf(lines[count++], sizeof(lines[0]), "%s %u", name, d);
    fclose(f);
  }
  f = fopen(path, "w");
  if (f == NULL) return;
  for (int i = 0; i < count; i++) fprintf(f, "%s\n", lines[i]);
  fprintf(f, "%s %u\n", device, (unsigned)delay);
  fclose(f);
}

static void printStats(const BmLinkMaster *m)
{
  printf("link: %llu words, %u packets in, %u bad packets, %u windows again, %u resyncs, %llu payload bytes acknowledged, delay %u\n",
         (unsigned long long)m->counters.words, (unsigned)m->counters.packetsIn, (unsigned)m->counters.badPackets,
         (unsigned)m->counters.retransmits, (unsigned)m->counters.resyncs, (unsigned long long)m->payloadBytesAcked,
         (unsigned)m->delay);
}

static const char *resultName(uint8_t r)
{
  static const char *names[] = { "ok", "transfer incomplete", "checksum mismatch", "too large", "pack refused",
                                 "state of another layout", "mode not supported" };
  return r < sizeof(names) / sizeof(names[0]) ? names[r] : "?";
}

int main(int argc, char **argv)
{
  const char *xbooDevice = NULL, *mgbaImage = NULL;
  int i = 1;
  uint32_t delay = 1;
  bool delayGiven = false, adaptive = false, resetWired = false;
  uint32_t cyclesPerWord = 0, flip = 0, miss = 0;
  for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++)
  {
    if (strcmp(argv[i], "--xboo") == 0 && i + 1 < argc) xbooDevice = argv[++i];
    else if (strcmp(argv[i], "--mgba") == 0 && i + 1 < argc) mgbaImage = argv[++i];
    else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) { delay = (uint32_t)atoi(argv[++i]); delayGiven = true; }
    else if (strcmp(argv[i], "--adaptive") == 0) adaptive = true;
    else if (strcmp(argv[i], "--reset") == 0 && i + 1 < argc) resetWired = strcmp(argv[++i], "xboo") == 0;
    else if (strcmp(argv[i], "--cycles-per-word") == 0 && i + 1 < argc) cyclesPerWord = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--errors") == 0 && i + 2 < argc)
    {
      flip = (uint32_t)atoi(argv[++i]);
      miss = (uint32_t)atoi(argv[++i]);
    }
    else
    {
      fprintf(stderr, "unknown option %s (see the comment at the top of tools/bmlink/bmlink.c)\n", argv[i]);
      return 2;
    }
  }

  BmLinkTransport transport;
  static BmXboo xboo;
#ifdef BM_HAVE_MGBA
  BmMgbaLink *mgba = NULL;
#endif
  if (xbooDevice != NULL)
  {
    if (!bmXbooOpen(&xboo, xbooDevice))
    {
      fprintf(stderr, "%s\n", xboo.error);
      return 1;
    }
    xboo.resetWired = resetWired;
    if (!delayGiven) loadDelay(xbooDevice, &delay);
    xboo.delay = delay;
    transport = bmXbooTransport(&xboo);
  }
  else if (mgbaImage != NULL)
  {
#ifdef BM_HAVE_MGBA
    mgba = bmMgbaLinkOpen(mgbaImage);
    if (mgba == NULL) return 1;
    if (cyclesPerWord != 0) bmMgbaLinkSetCyclesPerWord(mgba, cyclesPerWord);
    bmMgbaLinkSetErrors(mgba, flip, miss, 12345);
    transport = bmMgbaLinkTransport(mgba);
#else
    fprintf(stderr, "bmlink was built without libmgba\n");
    return 1;
#endif
  }
  else
  {
    fprintf(stderr, "usage: bmlink --xboo <device> | --mgba <image.gba> [options] <command>...\n");
    return 2;
  }
  (void)cyclesPerWord;

  BmLinkMaster m;
  bmLinkMasterInit(&m, &transport);
  m.adaptive = adaptive;
  m.minDelay = 0;
  bmLinkMasterSetDelay(&m, delay);
  BmLiveRemote remote;
  bool haveRemote = false;
  uint32_t transferId = (uint32_t)time(NULL);
  int failed = 0;

  for (; i < argc && !failed; i++)
  {
    const char *cmd = argv[i];
    const char *a1 = i + 1 < argc ? argv[i + 1] : NULL;
    if (strcmp(cmd, "hello") == 0)
    {
      if (!bmLiveMasterHello(&m, &remote)) { failed = 1; break; }
      haveRemote = true;
      const BmLiveHello *h = &remote.hello;
      printf("runtime %u, protocol %u, ABI %u.%u, profile %.4s, pack %u bytes (crc %08X, %u chunks), content buffers %u bytes\n",
             h->runtimeVersion, h->protocolVersion, h->abiMajor, h->abiMinor, (const char *)&h->profile,
             (unsigned)h->activeSize, (unsigned)h->activeCrc, (unsigned)h->chunkCount, (unsigned)h->contentCapacity);
    }
    else if (strcmp(cmd, "ping") == 0)
    {
      int count = a1 && a1[0] >= '0' && a1[0] <= '9' ? atoi(argv[++i]) : 4;
      uint8_t payload[1000];
      for (int k = 0; k < (int)sizeof(payload); k++) payload[k] = (uint8_t)(k * 7);
      double t0 = now();
      uint64_t w0 = m.counters.words;
      for (int k = 0; k < count && !failed; k++)
      {
        BmLinkPacket p;
        if (!bmLinkMasterRequest(&m, BM_LINK_PING, payload, sizeof(payload), BM_LINK_PING, &p, BM_LIVE_ANSWER_POLLS) ||
            p.length != sizeof(payload) || memcmp(p.payload, payload, sizeof(payload)) != 0)
          failed = 1;
      }
      double dt = now() - t0;
      printf("ping: %d x %u bytes there and back, %llu words, %.3f s (%.0f words/s)\n", count, (unsigned)sizeof(payload),
             (unsigned long long)(m.counters.words - w0), dt, (double)(m.counters.words - w0) / (dt > 0 ? dt : 1));
    }
    else if (strcmp(cmd, "push") == 0 && a1 != NULL)
    {
      i++;
      BmLiveMode mode = BM_LIVE_HOT;
      const char *room = NULL;
      if (i + 1 < argc && strcmp(argv[i + 1], "hot") == 0) i++;
      else if (i + 1 < argc && strncmp(argv[i + 1], "restart", 7) == 0)
      {
        mode = BM_LIVE_RESTART;
        if (argv[i + 1][7] == ':') room = argv[i + 1] + 8;
        i++;
      }
      uint32_t size;
      uint8_t *pack = readFile(a1, &size);
      if (pack == NULL) { failed = 1; break; }
      if (!haveRemote && !bmLiveMasterHello(&m, &remote)) { free(pack); failed = 1; break; }
      BmLivePushStats st;
      double t0 = now();
      bool ok = bmLiveMasterPushPack(&m, pack, size, ++transferId, &remote, &st);
      BmLiveStatus status;
      ok = ok && bmLiveMasterApply(&m, transferId, mode, room, &status);
      free(pack);
      if (!ok) { failed = 1; break; }
      printf("push %s: %u chunks sent (%u bytes), %u already there, %.2f s; %s: %s, frame %u, room %u, player %d,%d\n", a1,
             (unsigned)st.chunksSent, (unsigned)st.bytesSent, (unsigned)st.chunksResident, now() - t0,
             mode == BM_LIVE_HOT ? "hot" : "restart", resultName(status.result), (unsigned)status.frame,
             (unsigned)status.roomIndex, status.x, status.y);
      if (status.result != BM_LIVE_OK) failed = 1;
      haveRemote = bmLiveMasterHello(&m, &remote);
    }
    else if (strcmp(cmd, "state-get") == 0 && a1 != NULL)
    {
      i++;
      uint8_t mask = BM_STATE_SNAPSHOT;
      if (i + 1 < argc && strcmp(argv[i + 1], "save") == 0) { mask = BM_STATE_SAVE; i++; }
      else if (i + 1 < argc && strcmp(argv[i + 1], "link") == 0) { mask = BM_STATE_LINK; i++; }
      else if (i + 1 < argc && strcmp(argv[i + 1], "snapshot") == 0) i++;
      uint8_t data[BM_LINK_MAX_PAYLOAD];
      uint32_t size;
      if (!bmLiveMasterGetState(&m, mask, data, sizeof(data), &size)) { failed = 1; break; }
      FILE *f = fopen(a1, "wb");
      if (f == NULL || fwrite(data, 1, size, f) != size) failed = 1;
      if (f) fclose(f);
      printf("state-get: %u bytes to %s\n", (unsigned)size, a1);
    }
    else if (strcmp(cmd, "state-put") == 0 && a1 != NULL)
    {
      i++;
      uint32_t size;
      uint8_t *data = readFile(a1, &size);
      BmLiveStatus status;
      if (data == NULL || !bmLiveMasterPutState(&m, data, size, &status)) { free(data); failed = 1; break; }
      free(data);
      printf("state-put %s: %s, frame %u, room %u, player %d,%d\n", a1, resultName(status.result),
             (unsigned)status.frame, (unsigned)status.roomIndex, status.x, status.y);
      if (status.result != BM_LIVE_OK) failed = 1;
    }
    else if (strcmp(cmd, "boot") == 0 && a1 != NULL)
    {
      i++;
      if (xbooDevice == NULL) { fprintf(stderr, "boot needs --xboo\n"); failed = 1; break; }
      uint32_t size;
      uint8_t *image = readFile(a1, &size);
      if (image == NULL || !bmXbooBoot(&xboo, image, size)) { fprintf(stderr, "boot: %s\n", xboo.error); failed = 1; }
      else printf("boot: %s sent (%u bytes)\n", a1, (unsigned)size);
      free(image);
    }
    else if (strcmp(cmd, "reset") == 0)
    {
      if (transport.reset == NULL || !transport.reset(transport.pContext))
        printf("reset: no reset wire; switch the GBA off and on\n");
    }
    else if (strcmp(cmd, "stats") == 0)
    {
      printStats(&m);
#ifdef BM_HAVE_MGBA
      if (mgba != NULL)
      {
        BmMgbaLinkStats s = bmMgbaLinkStats(mgba);
        printf("wire: %llu words, %llu lost (slave not ready or injected), %llu bits flipped, %.2f s emulated (%.0f cycles per word)\n",
               (unsigned long long)s.words, (unsigned long long)s.lost, (unsigned long long)s.flips, s.cycles / 16777216.0,
               s.words ? (double)s.cycles / (double)s.words : 0.0);
      }
#endif
    }
#ifdef BM_HAVE_MGBA
    else if (strcmp(cmd, "run") == 0 && a1 != NULL && mgba != NULL)
    {
      bmMgbaLinkRunFrames(mgba, (uint32_t)atoi(argv[++i]));
    }
    else if (strcmp(cmd, "check") == 0 && a1 != NULL && mgba != NULL)
    {
      if (!bmMgbaLinkCheck(mgba, argv[++i])) failed = 1;
    }
    else if (strcmp(cmd, "screenshot") == 0 && a1 != NULL && mgba != NULL)
    {
      if (!bmMgbaLinkScreenshot(mgba, argv[++i])) failed = 1;
    }
#endif
    else
    {
      fprintf(stderr, "unknown command %s\n", cmd);
      failed = 2;
    }
  }
  if (failed == 1 && m.pError != NULL) fprintf(stderr, "link: %s\n", m.pError);
  if (failed == 1) printStats(&m);
  if (xbooDevice != NULL)
  {
    if (adaptive && !failed) saveDelay(xbooDevice, m.delay);
    bmXbooClose(&xboo);
  }
#ifdef BM_HAVE_MGBA
  bmMgbaLinkClose(mgba);
#endif
  bmLinkMasterFree(&m);
  return failed;
}
