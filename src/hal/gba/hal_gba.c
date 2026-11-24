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

// Line tables. Slots 0 and 1 are fed by H-blank DMA (channels 0 and 3), slot 2 by the H-blank
// interrupt. The vertical-blank interrupt (irq.S) restarts them all from bm_hal_lines: it takes
// `next` as `shown` and writes line 0's value; every H-blank then writes the next line's. So a
// frame that runs late shows the last table again, rather than the DMA reading on past its end.
// Two buffers per slot: a new table is written into the one not shown, and is shown from the
// next vertical blank on, never torn. One extra entry each because the H-blank transfer of the
// last line reads one value past the end.
typedef struct LineSlot
{
  volatile uint16_t *pReg;     // NULL: off
  const uint16_t *pValues;
} LineSlot;
struct
{
  LineSlot next[BM_HAL_LINE_TABLES];    // offset 0: what the next vertical blank shows
  LineSlot shown[BM_HAL_LINE_TABLES];   // offset 24: what is shown now (written by irq.S)
} bm_hal_lines;   // IWRAM (.bss)
// The DMA slots' buffers in EWRAM. The third slot's too, unless the program has its own
// (apps/game/main.c: in IWRAM for a multiboot image, whose EWRAM is short); its own section,
// so that this one is dropped then.
static uint16_t s_lineValuesDma[2][2][GBA_SCREEN_HEIGHT + 1] BM_EWRAM_BSS;
__attribute__((weak, section(".sbss.bm_hal_line_values_irq"))) uint16_t bm_hal_line_values_irq[2][GBA_SCREEN_HEIGHT + 1];

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

void bmHalPaletteWriteBlock(uint16_t index, const uint16_t *pColours, uint16_t count)
{
  volatile uint16_t *p = GBA_PALETTE + index;
  if (index + count > BM_HAL_PALETTE_SIZE) count = index < BM_HAL_PALETTE_SIZE ? (uint16_t)(BM_HAL_PALETTE_SIZE - index) : 0;
  for (uint16_t i = 0; i < count; i++) p[i] = pColours[i] & 0x7FFF;
}

uint16_t bmHalPaletteRead(uint16_t index)
{
  return index < BM_HAL_PALETTE_SIZE ? GBA_PALETTE[index] : 0;
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
// other. The interrupt handler (irq.S) starts at each vertical blank the pair filled last,
// handed over when the program waits for that blank (bm_hal_audio_next). A frame that ran
// late handed none over: then a frame of silence plays (rather than the DMA reading on past
// the buffer, over the other pair: old sound again). The buffers are a little longer than a
// frame, so a DMA request that falls just before the restart reads silence too.
//
// That must not happen while the sound plays: 16 samples of silence, and the frame's last 16
// lost, are a click (heard as crackling over a quiet held note). The FIFOs ask for 16 bytes
// every 16 samples, exactly every 12 lines (16 * 924 = 12 * 1232 cycles), so where those
// requests fall against the vertical blank stays as it was when the sound started. Started
// from the interrupt handler, the first came within a sample of the restart, and a handler a
// little later than that first one (it varies on the device) let one in before the restart.
// Started on line 214 instead, the first request comes some 6 lines after each blank: the
// restart may be that late (measured in mGBA by delaying it; line 218 would be the worst).

#define AUDIO_BUFFER (BM_HAL_AUDIO_FRAME_SAMPLES + 16)
#define AUDIO_START_LINE 214
#define AUDIO_DMA (GBA_DMA_ENABLE | GBA_DMA_START_FIFO | GBA_DMA_32BIT | GBA_DMA_REPEAT | GBA_DMA_DEST_FIXED)
// In fast memory: written once per sample. Aligned like the silence: the DMA moves words and
// leaves out the address's low two bits, so an odd address had each frame start with the bytes
// before it and lose its last ones (a buzz of 60 a second, loudest on low held notes).
static int8_t s_audio[2][2][AUDIO_BUFFER] __attribute__((aligned(4)));
static uint8_t s_audioFill;   // the pair being filled
static bool s_audioOn;
static bool s_audioFilled;    // the pair was given out this frame (bmHalAudioBuffers)
int8_t bm_hal_audio_silence[AUDIO_BUFFER] BM_EWRAM_BSS __attribute__((aligned(4)));
// Read by irq.S: left, right (0: none handed over), sound on.
struct { int8_t *pLeft, *pRight; uint32_t on; } bm_hal_audio_next BM_EWRAM_BSS;

void bmHalAudioStart(void)
{
  if (s_audioOn) return;
  GBA_REG_SOUNDCNT_X = 0x80;     // sound on
  GBA_REG_TM0CNT_H = 0;
  GBA_REG_TM0CNT_L = (uint16_t)(65536 - 924);
  GBA_REG_DMA1DAD = GBA_FIFO_A;
  GBA_REG_DMA2DAD = GBA_FIFO_B;
  GBA_REG_DMA1SAD = (uint32_t)bm_hal_audio_silence;
  GBA_REG_DMA2SAD = (uint32_t)bm_hal_audio_silence;
  // Both direct sound channels at full volume, timer 0; A on the left, B on the right. The
  // FIFOs are emptied and the timer started on a chosen line (AUDIO_START_LINE), with
  // interrupts off for those few writes, so the FIFO requests fall where they should.
  for (;;)
  {
    while (GBA_REG_VCOUNT != AUDIO_START_LINE) {}
    uint16_t ime = GBA_REG_IME;
    GBA_REG_IME = 0;
    if (GBA_REG_VCOUNT != AUDIO_START_LINE)
    {
      GBA_REG_IME = ime;   // an interrupt took the line: the next frame's
      continue;
    }
    GBA_REG_SOUNDCNT_H = 0x0004 | 0x0008 | 0x0200 | 0x0800 | 0x1000 | 0x8000;
    GBA_REG_DMA1CNT_H = AUDIO_DMA;
    GBA_REG_DMA2CNT_H = AUDIO_DMA;
    GBA_REG_TM0CNT_H = 0x80;
    s_audioOn = true;
    bm_hal_audio_next.on = 1;
    GBA_REG_IME = ime;
    break;
  }
}

void bmHalAudioBuffers(int8_t **ppLeft, int8_t **ppRight)
{
  *ppLeft = s_audio[s_audioFill][0];
  *ppRight = s_audio[s_audioFill][1];
  s_audioFilled = true;
}

// The pair filled this frame goes to the interrupt handler, to play from the next blank.
static void handOverAudio(void)
{
  if (!s_audioOn || !s_audioFilled) return;
  s_audioFilled = false;
  bm_hal_audio_next.pRight = s_audio[s_audioFill][1];
  bm_hal_audio_next.pLeft = s_audio[s_audioFill][0];   // last: the handler reads it first
  s_audioFill ^= 1;
}

uint32_t bmHalVideoWaitVBlank(void)
{
  initInterrupts();
  handOverAudio();
  // BIOS VBlankIntrWait: halts the CPU until the next VBlank interrupt. The returned count
  // comes from the interrupt handler, so missed frames show up as gaps.
  __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory");
  // The third table switched off and no longer shown: no more H-blank interrupts.
  if (bm_hal_lines.shown[2].pReg == NULL && bm_hal_lines.next[2].pReg == NULL && (GBA_REG_IE & GBA_IRQ_HBLANK))
  {
    GBA_REG_IE &= (uint16_t)~GBA_IRQ_HBLANK;
    GBA_REG_DISPSTAT &= (uint16_t)~GBA_DISPSTAT_HBLANK_IRQ;
  }
  return bm_vblank_count;
}

uint32_t bmHalVideoFrameCount(void)
{
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
  BmPpuSnapshotSource source = { s_regs, (const volatile uint8_t *)GBA_VRAM, GBA_PALETTE, GBA_OAM, { 0 }, { NULL } };
  for (uint32_t t = 0; t < BM_HAL_LINE_TABLES; t++)
  {
    // The tables as last set.
    const LineSlot *l = &bm_hal_lines.next[t];
    source.lineRegs[t] = l->pReg != NULL ? (uint16_t)((uint32_t)l->pReg - GBA_IO) : 0xFFFF;
    source.pLineValues[t] = l->pValues;
  }
  return bmPpuSnapshotPart(&source, offset, pOut, size);
}

void bmHalVramWrite(uint32_t offset, const void *pData, uint32_t size)
{
  if (offset >= GBA_VRAM_SIZE || size > GBA_VRAM_SIZE - offset || ((offset | size) & 1))
  {
    return;
  }
  // Video memory takes 16- and 32-bit writes only. Aligned (tiles in a pack are): words; a
  // room's tiles took ten times as long built from bytes.
  if ((((uintptr_t)pData | offset) & 3) == 0)
  {
    volatile uint32_t *pDst = (volatile uint32_t *)(GBA_VRAM + offset);
    const uint32_t *pSrc = (const uint32_t *)pData;
    for (uint32_t words = size / 4; words != 0; words--) *pDst++ = *pSrc++;
    if (size & 2) *(volatile uint16_t *)pDst = *(const uint16_t *)pSrc;
    return;
  }
  volatile uint16_t *pDst = (volatile uint16_t *)(GBA_VRAM + offset);
  if (((uintptr_t)pData & 1) == 0)
  {
    const uint16_t *pSrc = (const uint16_t *)pData;
    for (uint32_t i = 0; i < size / 2; i++) pDst[i] = pSrc[i];
    return;
  }
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
  if (slot >= BM_HAL_LINE_TABLES)
  {
    return;
  }
  LineSlot *pNext = &bm_hal_lines.next[slot];
  if (pValues == NULL || reg >= BM_PPU_REG_SIZE)
  {
    // Off from the next vertical blank on (the H-blank interrupt then stops in bmHalVideoWaitVBlank).
    uint16_t ime = GBA_REG_IME;
    GBA_REG_IME = 0;
    pNext->pReg = NULL;
    pNext->pValues = NULL;
    GBA_REG_IME = ime;
    return;
  }
  initInterrupts();
  // A table set earlier this frame and not shown yet is dropped first: a vertical blank while
  // this one is written shows the current one again, and the buffer not shown stays free.
  uint16_t ime = GBA_REG_IME;
  GBA_REG_IME = 0;
  *pNext = bm_hal_lines.shown[slot];
  GBA_REG_IME = ime;
  uint16_t(*pBuffers)[GBA_SCREEN_HEIGHT + 1] = slot == 2 ? bm_hal_line_values_irq : s_lineValuesDma[slot];
  uint16_t *pTo = bm_hal_lines.shown[slot].pValues == pBuffers[0] ? pBuffers[1] : pBuffers[0];
  for (uint32_t y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    pTo[y] = pValues[y];
  }
  pTo[GBA_SCREEN_HEIGHT] = pValues[GBA_SCREEN_HEIGHT - 1];
  ime = GBA_REG_IME;
  GBA_REG_IME = 0;
  pNext->pReg = &GBA_REG16(reg);
  pNext->pValues = pTo;
  GBA_REG_IME = ime;
  if (slot == 2)
  {
    GBA_REG_DISPSTAT |= GBA_DISPSTAT_HBLANK_IRQ;
    GBA_REG_IE |= GBA_IRQ_HBLANK;
  }
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
