/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "hal.h"
#include "hal_gba_regs.h"
#include "ppu.h"
#include "ppu_snapshot_part.h"
#include "shared.h"

// Provided by the linker script: the parts of EWRAM and IWRAM not used by the program.
extern uint8_t __ewram_heap_start[], __ewram_heap_end[];
extern uint8_t __iwram_heap_start[], __iwram_heap_end[];

extern volatile uint32_t bm_vblank_count;
extern void bm_irq_handler(void);
static bool s_irqReady;

// Line table: register offset and its values. One extra entry because the H-blank DMA of the
// last line reads one value past the end.
static uint16_t s_lineReg = 0xFFFF;
static uint16_t s_lineValues[GBA_SCREEN_HEIGHT + 1] BM_EWRAM_BSS;

const char *bmHalPlatformName(void)
{
  return "gba";
}

void bmHalGetMemoryRanges(BmHalMemoryRange *pRanges, uint32_t count)
{
  const BmHalMemoryRange ranges[3] = {
    { __ewram_heap_start, (size_t)(__ewram_heap_end - __ewram_heap_start) },
    { __iwram_heap_start, (size_t)(__iwram_heap_end - __iwram_heap_start) },
    { (void *)GBA_VRAM, GBA_VRAM_SIZE },
  };
  for (uint32_t i = 0; i < count; i++)
  {
    pRanges[i] = i < 3 ? ranges[i] : (BmHalMemoryRange){ NULL, 0 };
  }
}

static void initInterrupts(void)
{
  if (s_irqReady) return;
  GBA_REG_IME = 0;
  GBA_BIOS_IRQ_VECTOR = bm_irq_handler;
  GBA_REG_DISPSTAT |= 1u << 3;  // VBlank interrupt
  GBA_REG_IE = 1u;              // VBlank
  GBA_REG_IME = 1;
  s_irqReady = true;
}

void bmHalVideoInit(void)
{
  // Mode 0, no layers enabled, forced blank off: only the backdrop colour is visible.
  GBA_REG_DISPCNT = 0;
  initInterrupts();
}

void bmHalPaletteWrite(uint16_t index, uint16_t bgr555)
{
  if (index < BM_HAL_PALETTE_SIZE)
  {
    GBA_PALETTE[index] = bgr555 & 0x7FFF;
  }
}

uint16_t bmHalPaletteRead(uint16_t index)
{
  return index < BM_HAL_PALETTE_SIZE ? GBA_PALETTE[index] : 0;
}

static void restartLineTable(void)
{
  GBA_REG_DMA0CNT_H = 0;
  if (s_lineReg == 0xFFFF)
  {
    return;
  }
  // Line 0 gets its value now; every H-blank then copies the value for the next line.
  GBA_REG16(s_lineReg) = s_lineValues[0];
  GBA_REG_DMA0SAD = (uint32_t)&s_lineValues[1];
  GBA_REG_DMA0DAD = GBA_IO + s_lineReg;
  GBA_REG_DMA0CNT_L = 1;
  GBA_REG_DMA0CNT_H = GBA_DMA_ENABLE | GBA_DMA_START_HBLANK | GBA_DMA_REPEAT | GBA_DMA_DEST_FIXED;
}

uint16_t bmHalVideoLine(void)
{
  return GBA_REG16(0x006) & 0xFF;
}

uint32_t bmHalCycles(void)
{
  // Timer 2 counts cycles, timer 3 its overflows.
  static bool s_running;
  if (!s_running)
  {
    GBA_REG_TM2CNT_L = 0;
    GBA_REG_TM3CNT_L = 0;
    GBA_REG_TM3CNT_H = 0x84;
    GBA_REG_TM2CNT_H = 0x80;
    s_running = true;
  }
  uint16_t high, low;
  do
  {
    high = GBA_REG_TM3CNT_L;
    low = GBA_REG_TM2CNT_L;
  } while (high != GBA_REG_TM3CNT_L);
  return ((uint32_t)high << 16) | low;
}

// --- Sound ----------------------------------------------------------------------------------
//
// Two buffers per side: one plays (DMA 1 and 2 feed FIFO A and B, timer 0 asks for a sample
// every 924 cycles, so a video frame is exactly 304 samples) while the program fills the
// other; at each vertical blank they swap. The buffers are a little longer than a frame, so a
// DMA request that falls just before the swap reads silence instead of something else.

#define AUDIO_BUFFER (BM_HAL_AUDIO_FRAME_SAMPLES + 16)
static int8_t s_audio[2][2][AUDIO_BUFFER];   // in fast memory: written once per sample
static uint8_t s_audioFill;   // the pair being filled
static bool s_audioOn;

void bmHalAudioStart(void)
{
  if (s_audioOn) return;
  GBA_REG_SOUNDCNT_X = 0x80;     // sound on
  // Both direct sound channels at full volume, timer 0; A on the left, B on the right; FIFOs reset.
  GBA_REG_SOUNDCNT_H = 0x0004 | 0x0008 | 0x0200 | 0x0800 | 0x1000 | 0x8000;
  GBA_REG_TM0CNT_H = 0;
  GBA_REG_TM0CNT_L = (uint16_t)(65536 - 924);
  GBA_REG_TM0CNT_H = 0x80;
  s_audioOn = true;
}

void bmHalAudioBuffers(int8_t **ppLeft, int8_t **ppRight)
{
  *ppLeft = s_audio[s_audioFill][0];
  *ppRight = s_audio[s_audioFill][1];
}

static void swapAudio(void)
{
  if (!s_audioOn) return;
  GBA_REG_DMA1CNT_H = 0;
  GBA_REG_DMA2CNT_H = 0;
  GBA_REG_DMA1SAD = (uint32_t)s_audio[s_audioFill][0];
  GBA_REG_DMA1DAD = GBA_FIFO_A;
  GBA_REG_DMA2SAD = (uint32_t)s_audio[s_audioFill][1];
  GBA_REG_DMA2DAD = GBA_FIFO_B;
  uint16_t control = GBA_DMA_ENABLE | GBA_DMA_START_FIFO | GBA_DMA_32BIT | GBA_DMA_REPEAT | GBA_DMA_DEST_FIXED;
  GBA_REG_DMA1CNT_H = control;
  GBA_REG_DMA2CNT_H = control;
  s_audioFill ^= 1;
}

uint32_t bmHalVideoWaitVBlank(void)
{
  initInterrupts();
  // BIOS VBlankIntrWait: halts the CPU until the next VBlank interrupt. The returned count
  // comes from the interrupt handler, so missed frames show up as gaps.
  __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory");
  restartLineTable();
  swapAudio();
  return bm_vblank_count;
}

// Most video registers are write-only on the GBA, so the HAL keeps what it wrote.
static uint16_t s_regs[BM_PPU_REG_SIZE / 2] BM_EWRAM_BSS;

void bmHalVideoWriteReg(uint16_t offset, uint16_t value)
{
  if (offset < BM_PPU_REG_SIZE)
  {
    GBA_REG16(offset) = value;
    s_regs[offset / 2] = value;
  }
}

uint16_t bmHalVideoReadReg(uint16_t offset)
{
  return offset < BM_PPU_REG_SIZE ? s_regs[offset / 2] : 0;
}

uint32_t bmHalVideoSnapshotPart(uint32_t offset, uint8_t *pOut, uint32_t size)
{
  BmPpuSnapshotSource source = {
    s_regs, (const volatile uint8_t *)GBA_VRAM, GBA_PALETTE, GBA_OAM, { s_lineReg }, { s_lineValues },
  };
  return bmPpuSnapshotPart(&source, offset, pOut, size);
}

void bmHalVramWrite(uint32_t offset, const void *pData, uint32_t size)
{
  if (offset >= GBA_VRAM_SIZE || size > GBA_VRAM_SIZE - offset || ((offset | size) & 1))
  {
    return;
  }
  volatile uint16_t *pDst = (volatile uint16_t *)(GBA_VRAM + offset);
  const uint8_t *pSrc = (const uint8_t *)pData;
  for (uint32_t i = 0; i < size / 2; i++)
  {
    pDst[i] = (uint16_t)(pSrc[2 * i] | (pSrc[2 * i + 1] << 8));
  }
}

void bmHalOamWrite(uint32_t first, const uint16_t *pData, uint32_t count)
{
  if (first >= BM_PPU_OAM_SIZE || count > BM_PPU_OAM_SIZE - first)
  {
    return;
  }
  for (uint32_t i = 0; i < count; i++)
  {
    GBA_OAM[first + i] = pData[i];
  }
}

void bmHalLineTableSet(uint32_t slot, uint16_t reg, const uint16_t *pValues)
{
  if (slot != 0)
  {
    return;
  }
  if (pValues == NULL || reg >= BM_PPU_REG_SIZE)
  {
    s_lineReg = 0xFFFF;
    GBA_REG_DMA0CNT_H = 0;
    return;
  }
  for (uint32_t y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    s_lineValues[y] = pValues[y];
  }
  s_lineValues[GBA_SCREEN_HEIGHT] = pValues[GBA_SCREEN_HEIGHT - 1];
  s_lineReg = reg;
}

uint16_t bmHalInputKeys(void)
{
  // KEYINPUT has 0 for pressed buttons; the bit order matches BM_KEY_*.
  return (uint16_t)(~GBA_REG_KEYINPUT & 0x03FF);
}

// Tells emulators and flash carts that the cartridge has 32 KB of SRAM.
__attribute__((used, aligned(4))) const char bm_save_type[12] = "SRAM_V113";

void bmHalSaveRead(uint32_t offset, void *pData, uint32_t size)
{
  if (offset > BM_HAL_SAVE_SIZE || size > BM_HAL_SAVE_SIZE - offset || bm_save_type[0] != 'S')
  {
    return;
  }
  // SRAM has an 8-bit bus: byte accesses only.
  uint8_t *p = (uint8_t *)pData;
  for (uint32_t i = 0; i < size; i++) p[i] = GBA_SRAM[offset + i];
}

void bmHalSaveWrite(uint32_t offset, const void *pData, uint32_t size)
{
  if (offset > BM_HAL_SAVE_SIZE || size > BM_HAL_SAVE_SIZE - offset)
  {
    return;
  }
  const uint8_t *p = (const uint8_t *)pData;
  for (uint32_t i = 0; i < size; i++) GBA_SRAM[offset + i] = p[i];
}

// --- Live link: normal mode, 32 bits, external clock (the PC or the other GBA is master) ---

static BmHalLinkHandler s_linkHandler;
static void *s_linkContext;
static uint32_t s_linkNext BM_IWRAM_DATA = 0xFFFFFFFFu;
static uint32_t s_linkArmed;          // the word the port is armed with
static volatile uint32_t s_linkWords; // words received
static uint32_t s_linkWordsSeen;

/// Called from bm_irq_handler (irq.S) in system mode with the user stack. The port is armed
/// again first, with the word the handler prepared last time, so the master can clock the
/// next word right away; the handler's answer goes out one transfer later.
void bm_hal_serial_irq(void);
BM_IWRAM_CODE void bm_hal_serial_irq(void)
{
  uint32_t in = GBA_REG_SIODATA32;
  GBA_REG_SIODATA32 = s_linkNext;
  GBA_REG_SIOCNT = GBA_SIO_32BIT | GBA_SIO_IRQ | GBA_SIO_START;
  s_linkArmed = s_linkNext;
  s_linkWords++;
  s_linkNext = s_linkHandler != NULL ? s_linkHandler(s_linkContext, in) : 0xFFFFFFFFu;
}

void bmHalLinkStart(BmHalLinkHandler handler, void *pContext, uint32_t firstWord)
{
  initInterrupts();
  GBA_REG_IE &= ~GBA_IRQ_SERIAL;
  s_linkHandler = handler;
  s_linkContext = pContext;
  GBA_REG_RCNT = 0;                 // serial port in normal/multiplayer modes
  GBA_REG_SIOCNT = GBA_SIO_32BIT;   // external clock
  GBA_REG_SIODATA32 = firstWord;
  s_linkNext = firstWord;
  s_linkArmed = firstWord;
  GBA_REG_SIOCNT = GBA_SIO_32BIT | GBA_SIO_IRQ;
  GBA_REG_SIOCNT = GBA_SIO_32BIT | GBA_SIO_IRQ | GBA_SIO_START;
  GBA_REG_IE |= GBA_IRQ_SERIAL;
}

void bmHalLinkRealign(void)
{
  // Looked at once per two frames of real time, however often this is called: arming the
  // port anew while a word is under way is exactly what cuts words apart, so "no word" has
  // to mean a pause much longer than a word and the gaps between words.
  static uint32_t s_checkedAt;
  uint32_t vblanks = bm_vblank_count;
  if (s_linkHandler == NULL || vblanks - s_checkedAt < 2) return;
  s_checkedAt = vblanks;
  uint32_t lock = bmHalLinkLock();
  if (s_linkWords == s_linkWordsSeen)
  {
    // Clearing the start bit drops whatever part of a word was shifted in.
    GBA_REG_SIOCNT = GBA_SIO_32BIT | GBA_SIO_IRQ;
    GBA_REG_SIODATA32 = s_linkArmed;
    GBA_REG_SIOCNT = GBA_SIO_32BIT | GBA_SIO_IRQ | GBA_SIO_START;
  }
  s_linkWordsSeen = s_linkWords;
  bmHalLinkUnlock(lock);
}

void bmHalReset(void)
{
  GBA_REG16(0x208) = 0;          // no interrupts on the way
  __asm__ volatile("swi 0x26");  // BIOS HardReset: as if switched on
}

void bmHalLinkStop(void)
{
  GBA_REG_IE &= ~GBA_IRQ_SERIAL;
  GBA_REG_SIOCNT = GBA_SIO_32BIT;
  s_linkHandler = NULL;
}

static uint16_t s_masterMode;

void bmHalLinkMasterBegin(bool fast)
{
  GBA_REG_IE &= ~GBA_IRQ_SERIAL;
  GBA_REG_RCNT = 0;
  s_masterMode = (uint16_t)(GBA_SIO_32BIT | GBA_SIO_INTERNAL_CLOCK | (fast ? GBA_SIO_2MHZ : 0));
  GBA_REG_SIOCNT = s_masterMode;
}

BM_IWRAM_CODE uint32_t bmHalLinkMasterExchange(uint32_t out)
{
  GBA_REG_SIODATA32 = out;
  GBA_REG_SIOCNT = s_masterMode | GBA_SIO_START;
  while (GBA_REG_SIOCNT & GBA_SIO_START) {}
  return GBA_REG_SIODATA32;
}

BM_IWRAM_CODE void bmHalLinkMasterWait(uint32_t cycles)
{
  // Four cycles per turn from IWRAM (subs 1, a taken branch 3).
  uint32_t turns = cycles / 4 + 1;
  __asm__ volatile("1: subs %0, %0, #1\n bne 1b" : "+r"(turns) : : "cc");
}

void bmHalLinkMasterEnd(void)
{
  GBA_REG_SIOCNT = GBA_SIO_32BIT;
}

uint32_t bmHalLinkLock(void)
{
  uint32_t state = GBA_REG_IE & GBA_IRQ_SERIAL;
  GBA_REG_IE &= ~GBA_IRQ_SERIAL;
  return state;
}

void bmHalLinkUnlock(uint32_t state)
{
  GBA_REG_IE |= (uint16_t)state;
}
