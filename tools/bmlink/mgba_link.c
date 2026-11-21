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
  bool paired;               // bmMgbaPairRun: the clock event only ends the step
  int32_t masterLeft;        // cycles until the transfer this side started is through (0: none)
  bool finishPending;        // a transfer's result, put into the port at this side's next step
  bool woken;                // a halted program was woken to end a step (see clockEvent)
  uint32_t wokenPc;
  uint32_t finishData;
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
static void sioFinish(struct GBA *gba, uint32_t data);

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
  if (l->woken)
  {
    l->woken = false;
    struct ARMCore *cpu = l->gba->cpu;
    if ((uint32_t)cpu->gprs[ARM_PC] == l->wokenPc && !cpu->halted)
    {
      cpu->halted = 1;
      cpu->nextEvent = cpu->cycles;
    }
  }
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
  if (!l->paired) transfer(l);
  else if (l->finishPending)
  {
    // Inside the emulator's own event, so the interrupt reaches a halted program.
    l->finishPending = false;
    sioFinish(l->gba, l->finishData);
  }
  if (l->paired && l->gba->cpu->halted)
  {
    // A halted program is run from event to event until an interrupt wakes it, which would
    // carry it far ahead of the other GBA. Wake it so the step ends here; runCycles halts it
    // again unless an interrupt really came.
    l->woken = true;
    l->wokenPc = (uint32_t)l->gba->cpu->gprs[ARM_PC];
    l->gba->cpu->halted = 0;
  }
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

bool bmMgbaLinkReached(BmMgbaLink *l, const char *spec)
{
  char buf[512];
  snprintf(buf, sizeof(buf), "%s", spec);
  char *colon = strrchr(buf, ':'), *eq = colon ? strchr(colon, '=') : NULL;
  if (colon == NULL || eq == NULL) return false;
  *colon = 0;
  *eq = 0;
  char *plus = strchr(colon + 1, '+');
  uint32_t offset = 0;
  if (plus)
  {
    *plus = 0;
    offset = (uint32_t)strtoul(plus + 1, NULL, 0);
  }
  uint32_t address = elfSymbol(buf, colon + 1);
  return address != 0 && l->core->busRead32(l->core, address + offset) >= (uint32_t)strtoul(eq + 1, NULL, 0);
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

// --- Two GBAs --------------------------------------------------------------------------------

static uint32_t sioData(struct GBA *gba)
{
  return gba->memory.io[REG_SIODATA32_LO >> 1] | ((uint32_t)gba->memory.io[REG_SIODATA32_HI >> 1] << 16);
}

static void sioFinish(struct GBA *gba, uint32_t data)
{
  gba->memory.io[REG_SIODATA32_LO >> 1] = (uint16_t)data;
  gba->memory.io[REG_SIODATA32_HI >> 1] = (uint16_t)(data >> 16);
  uint16_t cnt = gba->sio.siocnt & (uint16_t)~0x0080;
  gba->sio.siocnt = cnt;
  gba->memory.io[REG_SIOCNT >> 1] = cnt;
  if (cnt & 0x4000) GBARaiseIRQ(gba, GBA_IRQ_SIO, 0);
}

// One side as the master of a transfer, if it started one.
static void pairClock(BmMgbaLink *m, BmMgbaLink *s, uint32_t step, BmMgbaPairStats *pStats)
{
  if (m->finishPending) return;
  uint16_t cnt = m->gba->sio.siocnt;
  bool started = (cnt & 0x0080) && (cnt & 0x0001) && (cnt & 0x1000) && m->gba->sio.mode == SIO_NORMAL_32;
  if (!started)
  {
    m->masterLeft = 0;
    return;
  }
  if (m->masterLeft == 0)
  {
    // 32 bits at 2 MHz or 256 kHz, in cycles of the 16.78 MHz processor clock.
    m->masterLeft = (cnt & 0x0002) ? 32 * 8 : 32 * 64;
    return;
  }
  m->masterLeft -= (int32_t)step;
  if (m->masterLeft > 0) return;
  m->masterLeft = 0;
  uint16_t other = s->gba->sio.siocnt;
  bool armed = !s->finishPending && (other & 0x0080) && !(other & 0x0001) && (other & 0x1000) && s->gba->sio.mode == SIO_NORMAL_32;
  uint32_t out = sioData(m->gba);
  if (pStats != NULL) pStats->words++;
  if (armed)
  {
    m->finishData = sioData(s->gba);
    s->finishData = out;
    m->finishPending = s->finishPending = true;
  }
  else
  {
    if (pStats != NULL) pStats->lost++;
    if (getenv("BMLINK_DEBUG_LOST"))
      fprintf(stderr, "lost %08X at %llu: other cnt %04X mode %d ie %04X pc %08X\n", (unsigned)out, (unsigned long long)mTimingGlobalTime(&m->gba->timing), other,
              (int)s->gba->sio.mode, s->gba->memory.io[0x200 >> 1], (unsigned)s->gba->cpu->gprs[ARM_PC]);
    m->finishData = 0xFFFFFFFFu;
    m->finishPending = true;
  }
}

void bmMgbaPairRun(BmMgbaLink *a, BmMgbaLink *b, uint64_t cycles, BmMgbaPairStats *pStats)
{
  // A step runs a little longer than asked (to the end of an instruction): the two are kept
  // together by their own clocks.
  const uint32_t step = 32;
  a->paired = b->paired = true;
  uint64_t startA = mTimingGlobalTime(&a->gba->timing), startB = mTimingGlobalTime(&b->gba->timing);
  uint64_t done = 0;
  while (done < cycles)
  {
    runCycles(a, step);
    uint64_t now = mTimingGlobalTime(&a->gba->timing) - startA;
    uint32_t took = (uint32_t)(now - done);
    done = now;
    uint64_t behind = mTimingGlobalTime(&b->gba->timing) - startB;
    if (behind < done) runCycles(b, (uint32_t)(done - behind));
    pairClock(a, b, took, pStats);
    pairClock(b, a, took, pStats);
  }
  a->paired = b->paired = false;
}

BmMgbaLinkStats bmMgbaLinkStats(const BmMgbaLink *l)
{
  return l->stats;
}

static void pauseFrames(void *pContext, uint32_t milliseconds)
{
  bmMgbaLinkRunFrames(pContext, milliseconds / 16 + 1);
}

BmLinkTransport bmMgbaLinkTransport(BmMgbaLink *l)
{
  return (BmLinkTransport){ l, exchange, NULL, NULL, 0, pauseFrames };
}
