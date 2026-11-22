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
//   --tcp [host:]port     the PC game (apps/game, listens on 127.0.0.1:7414 by default)
//
// Options (before the commands they affect):
//   --delay N             per-word delay (xcomms "Transfer Delay"); remembered per device
//   --adaptive            lower the delay while transfers are clean, raise it after errors
//   --logo FILE           the 156 logo bytes a real GBA wants in an image's header (or any GBA
//                         image to take them from); default ~/.config/blutmond/logo.bin
//   --wait SECONDS        boot: how long to keep asking for a GBA that does not answer (20)
//   --reset none|xboo     whether the reset wire is connected (default none)
//   --cycles-per-word N   mGBA: emulated time per word (default 2048, about 8000 words/s)
//   --errors FLIP MISS    mGBA: bit flips and lost words per million words
//
// Commands, run in order:
//   boot <image.gba>      xboo: multiboot the runtime (as xcomms does)
//   boot-if-needed <image.gba>   xboo: boot only if no runtime answers (the runtime is loaded
//                         once; after that content is pushed into it)
//   reset                 pull the reset wire (with --reset xboo)
//   hello                 what the runtime runs and has
//   ping [count]          round trips, prints the rate
//   push <pack> [hot|restart[:room]]   send what changed and apply it (default hot)
//   state-get <file> [save|link|snapshot]
//   state-put <file>      load a state (reload mode "state")
//   run <frames>          mGBA: let the game run
//   check <elf>:<sym>[+off][=|<=]<value>   mGBA: read the program's memory
//   screenshot <file.ppm> mGBA
//   ppu-snapshot <file.bmps>   the video state (VRAM, palettes, OAM, registers) as a PPU
//                         snapshot, for the editor's graphics view and bm_ppu_render
//   mirror [from|to] [host:]port [frames]   "from" (the default): the PC game there plays along
//                         with this device (its state, then the same keys for the same
//                         frames); "to": this device plays along with the PC game. Until
//                         Ctrl-C or for so many frames
//   gdb <port>            a GDB server for the running game (link-protocol.md section 8) until
//                         the debugger detaches; gdb-verbose also prints the packets
//   stats                 link counters

#include "link_master.h"
#include "live_master.h"
#include "gdb_bridge.h"
#include "tcp_link.h"
#include "xboo.h"
#ifdef BM_HAVE_MGBA
#  include "mgba_link.h"
#endif

#include <signal.h>
#ifdef __linux__
#  include <sys/prctl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

static volatile int s_cancel;

static void onInterrupt(int sig)
{
  // A second one ends the program at once.
  if (s_cancel) _exit(128 + sig);
  s_cancel = 1;
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

// The 156 logo bytes a real GBA wants in an image's header are not part of this repository
// (tools/gbafix.py). They are taken from a file: one with just the logo, or any GBA image.
static const char *defaultLogoPath(void)
{
  static char path[512];
  const char *home = getenv("HOME");
  snprintf(path, sizeof(path), "%s/.config/blutmond/logo.bin", home ? home : ".");
  return path;
}

/// Makes the header one the BIOS accepts, as xcomms' "header fix" does: logo, fixed value,
/// device type, complement check. False if there is no logo to put in.
static bool fixHeader(uint8_t *pImage, uint32_t size, const char *logoPath)
{
  if (size < 0xC0) return true;
  bool haveLogo = false;
  FILE *f = fopen(logoPath != NULL ? logoPath : defaultLogoPath(), "rb");
  if (f != NULL)
  {
    uint8_t buf[0xA0];
    size_t n = fread(buf, 1, sizeof(buf), f);
    // A file of exactly the logo, or an image with the logo at offset 4.
    int extra = fgetc(f);
    fclose(f);
    if (n == 156 && extra == EOF) { memcpy(pImage + 4, buf, 156); haveLogo = true; }
    else if (n == sizeof(buf)) { memcpy(pImage + 4, buf + 4, 156); haveLogo = true; }
  }
  else
  {
    // The image may carry it already.
    for (int i = 4; i < 0xA0 && !haveLogo; i++) haveLogo = pImage[i] != 0;
  }
  pImage[0xB2] = 0x96;
  pImage[0xB4] = 0x80;
  pImage[0xBE] = pImage[0xBF] = 0;
  uint8_t sum = 0;
  for (int i = 0xA0; i < 0xBD; i++) sum = (uint8_t)(sum + pImage[i]);
  pImage[0xBD] = (uint8_t)(0u - (0x19u + sum));
  return haveLogo;
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

/// Brings one side to the other's state. False if a link failed or the state was refused.
static bool mirrorState(BmLinkMaster *pFrom, BmLinkMaster *pTo, const char *toName)
{
  static uint8_t state[BM_LIVE_STATE_CAPACITY];
  uint32_t size = 0;
  BmLiveStatus status;
  if (!bmLiveMasterGetState(pFrom, BM_STATE_SNAPSHOT, state, sizeof(state), &size)) return false;
  if (!bmLiveMasterPutState(pTo, state, size, &status)) return false;
  if (status.result != BM_LIVE_OK)
  {
    fprintf(stderr, "mirror: %s does not take the state (%s)\n", toName, resultName(status.result));
    return false;
  }
  return true;
}

/// One plays, the other plays along (link-protocol.md section 10). `toDevice`: the PC game
/// plays and the device follows; else the device plays and the PC game follows. Ends after
/// `frameLimit` frames (0: never) or when the program is asked to stop.
static bool mirror(BmLinkMaster *pDevice, BmXboo *pXboo, const char *address, bool toDevice, uint32_t frameLimit)
{
  static BmTcpLink tcp;
  static BmLiveRemote device, pc;
  // The PC game may just have been started: give it a few seconds to listen.
  double until = now() + 5.0;
  bool open = false;
  while (!(open = bmTcpLinkOpen(&tcp, address)) && now() < until && !s_cancel)
  {
    struct timespec nap = { 0, 100000000 };
    nanosleep(&nap, NULL);
  }
  if (!open)
  {
    fprintf(stderr, "mirror: %s\n", tcp.error);
    return false;
  }
  BmLinkTransport transport = bmTcpLinkTransport(&tcp);
  BmLinkMaster pcMaster;
  bmLinkMasterInit(&pcMaster, &transport);
  BmLinkMaster *pFrom = toDevice ? &pcMaster : pDevice, *pTo = toDevice ? pDevice : &pcMaster;
  const char *fromName = toDevice ? "the PC game" : "the device", *toName = toDevice ? "the device" : "the PC game";
  // Asked to stop (Ctrl-C, the editor): end in order, so neither side keeps waiting.
  if (pXboo != NULL) pXboo->cancelByCaller = true;

  bool ok = bmLiveMasterHello(pDevice, &device) && bmLiveMasterHello(&pcMaster, &pc);
  if (ok && (device.hello.activeCrc != pc.hello.activeCrc || device.hello.activeSize != pc.hello.activeSize))
  {
    fprintf(stderr, "mirror: the device and the PC game run different content; push the same pack to both first\n");
    ok = false;
  }
  // Keys first, then the state: so nothing between the two is missing.
  // And the one that follows holds still from before it gets the state, or it would play
  // some frames of its own in between.
  const uint8_t on = 1, off = 0, ready = 3;
  ok = ok && bmLinkMasterQueue(pTo, BM_LINK_INPUT, &ready, 1) && bmLinkMasterFlush(pTo);
  ok = ok && bmLinkMasterQueue(pFrom, BM_LINK_INPUT, &on, 1) && bmLinkMasterFlush(pFrom) && mirrorState(pFrom, pTo, toName);
  if (ok) printf("mirror: %s follows %s\n", toName, fromName);
  uint32_t firstFrame = 0, lastFrame = 0, packets = 0, restarts = 0, jumps = 0;
  double started = now(), lastReport = started;
  double lastAsked = now();
  bool gone = false;
  while (ok && !s_cancel)
  {
    BmLinkPacket p;
    bool got = bmLinkMasterReceive(pFrom, BM_LINK_INPUT, &p, 8) && p.length >= BM_LIVE_INPUT_HEADER;
    if (got)
    {
      lastFrame = bmLinkGet32(p.payload);
      if (packets++ == 0) firstFrame = lastFrame;
      bool lost = (p.payload[5] & BM_LIVE_INPUT_FLAG_LOST) != 0;
      ok = bmLinkMasterQueue(pTo, BM_LINK_INPUT, p.payload, p.length) && bmLinkMasterFlush(pTo);
      // Changes that did not fit on the playing side: start over from its state.
      if (ok && lost) { restarts++; ok = mirrorState(pFrom, pTo, toName); }
    }
    else if (pFrom->pError != NULL && strstr(pFrom->pError, "resync") != NULL) ok = false;
    if (tcp.fd < 0)
    {
      // The window was closed: nothing left to mirror.
      printf("mirror: the PC game is gone\n");
      gone = true;
      break;
    }
    // The following side says so when its state differs or it has fallen behind. Asked only
    // ten times a second: every word costs a GBA an interrupt, and time is what it lacks.
    if (ok && now() - lastAsked >= 0.1)
    {
      lastAsked = now();
      if (bmLinkMasterReceive(pTo, BM_LINK_INPUT, &p, 2) && p.length >= 8 && p.payload[0] == 2)
      {
        uint32_t frame = bmLinkGet32(p.payload + 4);
        if (p.payload[1] == 1) { jumps++; printf("mirror: %s fell behind at frame %u; it jumps to where %s is\n", toName, (unsigned)frame, fromName); }
        else { restarts++; printf("mirror: %s differed at frame %u; taking the state of %s again\n", toName, (unsigned)frame, fromName); }
        // What is still on the way belongs to before the state: not needed any more.
        ok = mirrorState(pFrom, pTo, toName);
      }
    }
    if (!got)
    {
      // Nothing to pass on: leave the cable alone for a moment (an emulated device runs on
      // meanwhile, a real one does anyway).
      if (pDevice->transport.pause != NULL) pDevice->transport.pause(pDevice->transport.pContext, 2);
      else
      {
        struct timespec nap = { 0, 2000000 };
        nanosleep(&nap, NULL);
      }
    }
    if (now() - lastReport >= 5.0)
    {
      lastReport = now();
      printf("mirror: frame %u, %u packets, %u restarts, %u jumps\n", (unsigned)lastFrame, (unsigned)packets, (unsigned)restarts, (unsigned)jumps);
    }
    if (frameLimit != 0 && packets != 0 && lastFrame - firstFrame >= frameLimit) break;
  }
  if (ok && gone)
  {
    // Only the device is left to be told.
    bmLinkMasterQueue(pDevice, BM_LINK_INPUT, &off, 1);
    bmLinkMasterFlush(pDevice);
  }
  else if (ok)
  {
    bmLinkMasterQueue(pFrom, BM_LINK_INPUT, &off, 1);
    bmLinkMasterFlush(pFrom);
    bmLinkMasterQueue(pTo, BM_LINK_INPUT, &off, 1);
    bmLinkMasterFlush(pTo);
    printf("mirror: ended after %u frames in %.1f s, %u packets, %u restarts from the state of %s, %u jumps because %s fell behind\n",
           (unsigned)(lastFrame - firstFrame), now() - started, (unsigned)packets, (unsigned)restarts, fromName, (unsigned)jumps, toName);
  }
  else if (pcMaster.pError != NULL) fprintf(stderr, "mirror: PC game: %s\n", pcMaster.pError);
  if (pXboo != NULL) pXboo->cancelByCaller = false;
  bmLinkMasterFree(&pcMaster);
  bmTcpLinkClose(&tcp);
  return ok;
}

int main(int argc, char **argv)
{
  // Line by line also into a pipe (the editor shows the output while a transfer runs).
  setvbuf(stdout, NULL, _IOLBF, 0);
#ifdef __linux__
  // Started by another program (the editor): end with it, and do not keep the cable.
  prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
  const char *xbooDevice = NULL, *mgbaImage = NULL, *tcpAddress = NULL;
  int i = 1;
  // Between words the GBA has to arm its port again (in an interrupt): a little time, or it
  // is caught in the middle of a word.
  uint32_t delay = 8;
  bool delayGiven = false, adaptive = false, resetWired = false;
  const char *logoPath = NULL;
  uint32_t bootWait = 20;
  uint32_t cyclesPerWord = 0;
#ifdef BM_HAVE_MGBA
  uint32_t flip = 0, miss = 0;
#endif
  for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++)
  {
    if (strcmp(argv[i], "--xboo") == 0 && i + 1 < argc) xbooDevice = argv[++i];
    else if (strcmp(argv[i], "--mgba") == 0 && i + 1 < argc) mgbaImage = argv[++i];
    else if (strcmp(argv[i], "--tcp") == 0 && i + 1 < argc) tcpAddress = argv[++i];
    else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) { delay = (uint32_t)atoi(argv[++i]); delayGiven = true; }
    else if (strcmp(argv[i], "--adaptive") == 0) adaptive = true;
    else if (strcmp(argv[i], "--logo") == 0 && i + 1 < argc) logoPath = argv[++i];
    else if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc) bootWait = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--reset") == 0 && i + 1 < argc) resetWired = strcmp(argv[++i], "xboo") == 0;
    else if (strcmp(argv[i], "--cycles-per-word") == 0 && i + 1 < argc) cyclesPerWord = (uint32_t)atoi(argv[++i]);
#ifdef BM_HAVE_MGBA
    else if (strcmp(argv[i], "--errors") == 0 && i + 2 < argc)
    {
      flip = (uint32_t)atoi(argv[++i]);
      miss = (uint32_t)atoi(argv[++i]);
    }
#endif
    else
    {
      fprintf(stderr, "unknown option %s (see the comment at the top of tools/bmlink/bmlink.c)\n", argv[i]);
      return 2;
    }
  }

  BmLinkTransport transport;
  static BmXboo xboo;
  static BmTcpLink tcp;
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
    // Ctrl-C ends the program between two words, with the port released.
    xboo.pCancel = &s_cancel;
    signal(SIGINT, onInterrupt);
    signal(SIGTERM, onInterrupt);
    if (!delayGiven) loadDelay(xbooDevice, &delay);
    xboo.delay = delay;
    transport = bmXbooTransport(&xboo);
  }
  else if (tcpAddress != NULL)
  {
    if (!bmTcpLinkOpen(&tcp, tcpAddress))
    {
      fprintf(stderr, "%s\n", tcp.error);
      return 1;
    }
    transport = bmTcpLinkTransport(&tcp);
    delay = 0;
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
    fprintf(stderr, "usage: bmlink --xboo <device> | --tcp [host:]port | --mgba <image.gba> [options] <command>...\n");
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
      static uint8_t data[BM_LIVE_STATE_CAPACITY];
      uint32_t size;
      if (!bmLiveMasterGetState(&m, mask, data, sizeof(data), &size)) { failed = 1; break; }
      FILE *f = fopen(a1, "wb");
      if (f == NULL || fwrite(data, 1, size, f) != size) failed = 1;
      if (f) fclose(f);
      printf("state-get: %u bytes to %s\n", (unsigned)size, a1);
    }
    else if (strcmp(cmd, "ppu-snapshot") == 0 && a1 != NULL)
    {
      i++;
      static uint8_t data[128 * 1024];
      uint32_t size = 0;
      double t0 = now();
      if (!bmLiveMasterPpuSnapshot(&m, data, sizeof(data), &size)) { failed = 1; break; }
      FILE *f = fopen(a1, "wb");
      if (f == NULL || fwrite(data, 1, size, f) != size) failed = 1;
      if (f) fclose(f);
      printf("ppu-snapshot: %u bytes to %s, %.2f s\n", (unsigned)size, a1, now() - t0);
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
    else if ((strcmp(cmd, "boot") == 0 || strcmp(cmd, "boot-if-needed") == 0) && a1 != NULL)
    {
      i++;
      if (strcmp(cmd, "boot-if-needed") == 0)
      {
        // Without a runtime nobody acknowledges, so a short try is enough.
        uint32_t resyncs = m.maxResyncs;
        m.maxResyncs = 2;
        haveRemote = bmLiveMasterHello(&m, &remote);
        m.maxResyncs = resyncs;
        if (haveRemote)
        {
          printf("boot: the runtime is already running\n");
          continue;
        }
        if (xbooDevice == NULL) { fprintf(stderr, "boot-if-needed: no runtime answers\n"); failed = 1; break; }
      }
      if (xbooDevice == NULL) { fprintf(stderr, "boot needs --xboo\n"); failed = 1; break; }
      if (strcmp(cmd, "boot") == 0 && !resetWired)
      {
        // A runtime that is running is asked to start the device over, so the BIOS takes
        // the new image.
        uint32_t polls = m.pollTimeout;
        m.pollTimeout = 100;
        if (bmLiveMasterHello(&m, &remote) && bmLinkMasterQueue(&m, BM_LINK_RESET, NULL, 0) && bmLinkMasterFlush(&m))
          printf("boot: asked the running runtime to start the device over\n");
        m.pollTimeout = polls;
      }
      uint32_t size;
      uint8_t *image = readFile(a1, &size);
      bool sent = false;
      if (image == NULL) snprintf(xboo.error, sizeof(xboo.error), "cannot read %s", a1);
      else
      {
        if (!fixHeader(image, size, logoPath))
          fprintf(stderr, "boot: no logo given (--logo, or %s): a real GBA refuses the image after its header\n", defaultLogoPath());
        // The GBA only answers while it waits in its BIOS: keep asking for a while.
        double until = now() + (double)bootWait;
        bmXbooReset(&xboo);   // once (with the reset wire): the GBA then comes up waiting
        bool told = false;
        for (;;)
        {
          sent = bmXbooBoot(&xboo, image, size);
          if (sent || strstr(xboo.error, "no response") == NULL || now() >= until) break;
          if (!told) printf("boot: waiting for the GBA (switch it on without a cartridge, or with Start + Select held) ...\n");
          told = true;
          fflush(stdout);
        }
      }
      if (!sent) { fprintf(stderr, "boot: %s\n", xboo.error); failed = 1; }
      else
      {
        printf("boot: %s sent (%u bytes)\n", a1, (unsigned)size);
        // A new runtime: start the master over (nothing queued, sequence numbers from 0).
        uint32_t d = m.delay;
        bmLinkMasterFree(&m);
        bmLinkMasterInit(&m, &transport);
        m.adaptive = adaptive;
        m.minDelay = 0;
        bmLinkMasterSetDelay(&m, d);
        haveRemote = false;
        // The runtime needs a moment to start: wait until it answers, so the commands that
        // follow find it.
        double until = now() + 10.0;
        uint32_t polls = m.pollTimeout;
        m.pollTimeout = 100;
        while (!haveRemote && now() < until)
        {
          haveRemote = bmLiveMasterHello(&m, &remote);
          if (haveRemote) break;
          bmLinkMasterFree(&m);
          bmLinkMasterInit(&m, &transport);
          m.adaptive = adaptive;
          m.minDelay = 0;
          m.pollTimeout = 100;
          bmLinkMasterSetDelay(&m, d);
        }
        m.pollTimeout = polls;
        if (haveRemote) printf("boot: the runtime is up\n");
        else { fprintf(stderr, "boot: the image was sent, but the runtime does not answer\n"); failed = 1; }
      }
      free(image);
    }
    else if ((strcmp(cmd, "gdb") == 0 || strcmp(cmd, "gdb-verbose") == 0) && a1 != NULL)
    {
      // A GDB server for this game until the debugger detaches.
      if (!bmGdbBridgeRun(&m, (uint16_t)atoi(argv[++i]), strcmp(cmd, "gdb-verbose") == 0)) failed = 1;
    }
    else if (strcmp(cmd, "mirror") == 0 && a1 != NULL)
    {
      // The PC game plays along with this device: same state to start from, then the same
      // keys for the same frames (link-protocol.md section 10). Optional: for how many frames.
      bool toDevice = false;
      if (strcmp(a1, "to") == 0 || strcmp(a1, "from") == 0) { toDevice = strcmp(a1, "to") == 0; i++; }
      if (i + 1 >= argc) { fprintf(stderr, "mirror [from|to] [host:]port [frames]\n"); failed = 2; break; }
      const char *address = argv[++i];
      uint32_t limit = 0;
      if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') limit = (uint32_t)atoi(argv[++i]);
      if (!mirror(&m, xbooDevice != NULL ? &xboo : NULL, address, toDevice, limit)) failed = 1;
      if (s_cancel) break;   // asked to stop: ended in order, nothing more to do
    }
    else if (strcmp(cmd, "reset") == 0)
    {
      // By the wire if there is one, else by asking the runtime.
      if (transport.reset != NULL && transport.reset(transport.pContext)) printf("reset: by the reset wire\n");
      else if (bmLinkMasterQueue(&m, BM_LINK_RESET, NULL, 0) && bmLinkMasterFlush(&m)) printf("reset: the runtime starts the device over\n");
      else { fprintf(stderr, "reset: no reset wire and no runtime that answers; switch the GBA off and on\n"); failed = 1; }
      haveRemote = false;
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
