/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "mgba_link.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/timing.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>
#include <mgba/internal/arm/arm.h>

#include "elf_symbol.h"

#include <stdarg.h>

struct BmMgbaLink
{
  struct mCore *core;
  struct GBA *gba;
  struct GBASIODriver driver;
  color_t *pVideo;
  unsigned width, height;
  uint32_t cyclesPerWord;
  uint32_t flipPerMillion, missPerMillion, rng;
  BmMgbaLinkStats stats;
  struct mTimingEvent clock;  // when the master clocks the next word
  bool clocked;
  uint32_t out, in;
};

static void quietLog(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
  (void)logger;
  (void)category;
  if (level & (mLOG_FATAL | mLOG_ERROR))
  {
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
  }
}

// Register writes pass through unchanged: without this driver mGBA would finish transfers by
// itself; here the master (this tool) does.
static uint16_t writeRegister(struct GBASIODriver *driver, uint32_t address, uint16_t value)
{
  (void)driver;
  (void)address;
  return value;
}

BmMgbaLink *bmMgbaLinkOpen(const char *pImage)
{
  static struct mLogger logger = { .log = quietLog };
  mLogSetDefaultLogger(&logger);
  BmMgbaLink *l = calloc(1, sizeof(*l));
  l->core = mCoreFind(pImage);
  if (l->core == NULL || !l->core->init(l->core))
  {
    fprintf(stderr, "%s: not a GBA image mGBA can run\n", pImage);
    free(l);
    return NULL;
  }
  mCoreInitConfig(l->core, NULL);
  l->core->desiredVideoDimensions(l->core, &l->width, &l->height);
  l->pVideo = calloc((size_t)l->width * l->height, sizeof(color_t));
  l->core->setVideoBuffer(l->core, l->pVideo, l->width);
  if (!mCoreLoadFile(l->core, pImage))
  {
    fprintf(stderr, "%s: cannot load\n", pImage);
    bmMgbaLinkClose(l);
    return NULL;
  }
  l->core->reset(l->core);
  l->gba = l->core->board;
  l->driver.writeRegister = writeRegister;
  GBASIOSetDriver(&l->gba->sio, &l->driver, SIO_NORMAL_32);
  l->cyclesPerWord = 2048;  // about 8000 words per second
  return l;
}

void bmMgbaLinkClose(BmMgbaLink *l)
{
  if (l == NULL) return;
  mCoreConfigDeinit(&l->core->config);
  l->core->deinit(l->core);
  free(l->pVideo);
  free(l);
}

void bmMgbaLinkSetCyclesPerWord(BmMgbaLink *l, uint32_t cycles)
{
  l->cyclesPerWord = cycles;
}

void bmMgbaLinkSetErrors(BmMgbaLink *l, uint32_t flip, uint32_t miss, uint32_t seed)
{
  l->flipPerMillion = flip;
  l->missPerMillion = miss;
  l->rng = seed;
}

void bmMgbaLinkRunFrames(BmMgbaLink *l, uint32_t frames)
{
  for (uint32_t i = 0; i < frames; i++) l->core->runFrame(l->core);
}

static void clockEvent(struct mTiming *timing, void *pContext, uint32_t cyclesLate);

// Runs the emulator until the master clocks the next word. The transfer happens in a timing
// event at that time, so it also reaches a program that waits halted for an interrupt (mGBA
// skips ahead to events while halted; the serial interrupt wakes it, as on hardware).
static void runCycles(BmMgbaLink *l, uint32_t cycles)
{
  uint64_t start = mTimingGlobalTime(&l->gba->timing);
  l->clocked = false;
  l->clock.context = l;
  l->clock.callback = clockEvent;
  l->clock.name = "bmlink clock";
  l->clock.priority = 0x80;
  mTimingSchedule(&l->gba->timing, &l->clock, (int32_t)cycles);
  while (!l->clocked) l->core->runLoop(l->core);
  l->stats.cycles += mTimingGlobalTime(&l->gba->timing) - start;
}

static uint32_t rnd(BmMgbaLink *l)
{
  l->rng = l->rng * 1664525u + 1013904223u;
  return l->rng >> 8;
}

static uint32_t rnd(BmMgbaLink *l);

// The transfer itself, at the emulated time the master clocks the word.
static void transfer(BmMgbaLink *l)
{
  struct GBA *gba = l->gba;
  uint32_t out = l->out;
  l->stats.words++;
  uint16_t cnt = gba->sio.siocnt;
  bool armed = (cnt & 0x0080) && !(cnt & 0x0001) && (cnt & 0x1000) && gba->sio.mode == SIO_NORMAL_32;
  if (!armed || (l->missPerMillion != 0 && rnd(l) % 1000000u < l->missPerMillion))
  {
    l->stats.lost++;
    if (getenv("BMLINK_DEBUG_LOST"))
      fprintf(stderr, "lost: pc %08X cpsr %08X ie %04X ime %u cnt %04X vcount %u\n", (unsigned)gba->cpu->gprs[ARM_PC], (unsigned)gba->cpu->cpsr.packed,
              gba->memory.io[0x200 >> 1], gba->memory.io[0x208 >> 1], cnt, gba->memory.io[6 >> 1]);
    l->in = 0xFFFFFFFFu;
    return;
  }
  if (l->flipPerMillion != 0 && rnd(l) % 1000000u < l->flipPerMillion) { out ^= 1u << (rnd(l) & 31); l->stats.flips++; }
  uint32_t in = gba->memory.io[REG_SIODATA32_LO >> 1] | ((uint32_t)gba->memory.io[REG_SIODATA32_HI >> 1] << 16);
  if (l->flipPerMillion != 0 && rnd(l) % 1000000u < l->flipPerMillion) { in ^= 1u << (rnd(l) & 31); l->stats.flips++; }
  gba->memory.io[REG_SIODATA32_LO >> 1] = (uint16_t)out;
  gba->memory.io[REG_SIODATA32_HI >> 1] = (uint16_t)(out >> 16);
  cnt &= (uint16_t)~0x0080;
  gba->sio.siocnt = cnt;
  gba->memory.io[REG_SIOCNT >> 1] = cnt;
  if (cnt & 0x4000) GBARaiseIRQ(gba, GBA_IRQ_SIO, 0);
  l->in = in;
}

static void clockEvent(struct mTiming *timing, void *pContext, uint32_t cyclesLate)
{
  (void)timing;
  (void)cyclesLate;
  BmMgbaLink *l = pContext;
  transfer(l);
  l->clocked = true;
}

static uint32_t exchange(void *pContext, uint32_t out)
{
  BmMgbaLink *l = pContext;
  l->out = out;
  runCycles(l, l->cyclesPerWord);
  return l->in;
}

bool bmMgbaLinkCheck(BmMgbaLink *l, const char *spec)
{
  char buf[512];
  snprintf(buf, sizeof(buf), "%s", spec);
  char *colon = strrchr(buf, ':');
  if (colon == NULL) return false;
  char *eq = strchr(colon, '=');
  bool atMost = eq != NULL && eq[-1] == '<';
  if (atMost) eq[-1] = 0;
  *colon = 0;
  if (eq) *eq = 0;
  char *plus = strchr(colon + 1, '+');
  uint32_t offset = 0;
  if (plus)
  {
    *plus = 0;
    offset = (uint32_t)strtoul(plus + 1, NULL, 0);
  }
  uint32_t address = elfSymbol(buf, colon + 1);
  if (address == 0)
  {
    fprintf(stderr, "%s: no symbol %s\n", buf, colon + 1);
    return false;
  }
  uint32_t value = l->core->busRead32(l->core, address + offset);
  printf("%s+%u = %u\n", colon + 1, (unsigned)offset, (unsigned)value);
  if (eq == NULL) return true;
  uint32_t expected = (uint32_t)strtoul(eq + 1, NULL, 0);
  bool ok = atMost ? value <= expected : value == expected;
  if (!ok) fprintf(stderr, "%s+%u is %u, expected %s%u\n", colon + 1, (unsigned)offset, (unsigned)value, atMost ? "<=" : "", (unsigned)expected);
  return ok;
}

bool bmMgbaLinkScreenshot(BmMgbaLink *l, const char *path)
{
  FILE *f = fopen(path, "wb");
  if (f == NULL) return false;
  fprintf(f, "P6\n%u %u\n255\n", l->width, l->height);
  for (unsigned i = 0; i < l->width * l->height; i++)
  {
    unsigned char rgb[3] = { l->pVideo[i] & 0xFF, (l->pVideo[i] >> 8) & 0xFF, (l->pVideo[i] >> 16) & 0xFF };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return true;
}

BmMgbaLinkStats bmMgbaLinkStats(const BmMgbaLink *l)
{
  return l->stats;
}

BmLinkTransport bmMgbaLinkTransport(BmMgbaLink *l)
{
  return (BmLinkTransport){ l, exchange, NULL, NULL, 0 };
}
