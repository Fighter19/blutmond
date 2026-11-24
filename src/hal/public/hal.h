/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Hardware abstraction layer. This is the only interface between the engine and a platform.
// Hardware addresses exist only in the platform back ends (src/hal/<platform>/) and in their
// linker scripts. See docs/PLAN.md section 3.

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/// A range of memory the engine may allocate from (one per heap, see BM_MEMORY_HEAP_*).
typedef struct BmHalMemoryRange
{
  void *pStart;
  size_t size;
} BmHalMemoryRange;

/// Number of palette entries: 256 BG colours followed by 256 OBJ colours (BGR555).
#define BM_HAL_PALETTE_SIZE 512
#define BM_HAL_PALETTE_OBJ_OFFSET 256

#define BM_HAL_SCREEN_WIDTH 240
#define BM_HAL_SCREEN_HEIGHT 160

/// Name of the platform back end ("gba", "host").
const char *bmHalPlatformName(void);

/// Memory the engine may use for each heap (EWRAM, IWRAM, VRAM). Ranges not available on a
/// platform have size 0.
void bmHalGetMemoryRanges(BmHalMemoryRange *pRanges, uint32_t count);

/// Switches the display on in a known state: mode 0, all layers off, so only the backdrop
/// colour (BG palette entry 0) is visible.
void bmHalVideoInit(void);

/// Writes one palette entry (BGR555).
void bmHalPaletteWrite(uint16_t index, uint16_t bgr555);

/// Writes `count` palette entries from `index` on (BGR555; entries past the palette are left out).
void bmHalPaletteWriteBlock(uint16_t index, const uint16_t *pColours, uint16_t count);

/// Reads one palette entry (BGR555).
uint16_t bmHalPaletteRead(uint16_t index);

/// Waits for the start of the next vertical blank and restarts the line tables for the next
/// frame. Returns the frame counter after the wait.
uint32_t bmHalVideoWaitVBlank(void);

/// The frame counter now, without waiting: it has gone past what bmHalVideoWaitVBlank returned
/// when the frame's time is over (the next vertical blank has begun).
uint32_t bmHalVideoFrameCount(void);

/// Writes a video register. `offset` is the byte offset in the GBA's I/O layout
/// (BM_PPU_DISPCNT, BM_PPU_BGCNT(n), ... in ppu.h), the platform-neutral register model.
void bmHalVideoWriteReg(uint16_t offset, uint16_t value);
/// Reads a video register back as last written (the GBA keeps copies: most video registers
/// are write-only there).
uint16_t bmHalVideoReadReg(uint16_t offset);

/// The current video state as a PPU snapshot (BMPS, src/ppu/ppu.h): copies `size` bytes from
/// `offset` into `pOut` and returns the snapshot's total size. For inspecting what is loaded
/// (PPU_SNAPSHOT over the live link).
uint32_t bmHalVideoSnapshotPart(uint32_t offset, uint8_t *pOut, uint32_t size);

/// Copies data into video memory. `offset` and `size` must be even (VRAM only takes 16- and
/// 32-bit writes on the GBA).
void bmHalVramWrite(uint32_t offset, const void *pData, uint32_t size);

/// Copies `count` halfwords into OAM starting at halfword `first`. Call during VBlank.
void bmHalOamWrite(uint32_t first, const uint16_t *pData, uint32_t count);

/// Buttons, as bits of the value bmHalInputKeys() returns (pressed = 1).
enum {
  BM_KEY_A = 1 << 0, BM_KEY_B = 1 << 1, BM_KEY_SELECT = 1 << 2, BM_KEY_START = 1 << 3,
  BM_KEY_RIGHT = 1 << 4, BM_KEY_LEFT = 1 << 5, BM_KEY_UP = 1 << 6, BM_KEY_DOWN = 1 << 7,
  BM_KEY_R = 1 << 8, BM_KEY_L = 1 << 9,
};

/// Buttons held right now.
uint16_t bmHalInputKeys(void);

/// Size of the save memory (SRAM on the GBA cartridge).
#define BM_HAL_SAVE_SIZE (32 * 1024)

/// Reads/writes the save memory (battery-backed SRAM on the GBA).
void bmHalSaveRead(uint32_t offset, void *pData, uint32_t size);
void bmHalSaveWrite(uint32_t offset, const void *pData, uint32_t size);

/// Number of line tables (registers that can take a new value on every line). On the GBA
/// slots 0 and 1 are H-blank DMA (channels 0 and 3) and cost nothing; slot 2 is an H-blank
/// interrupt: about 160 short interrupts a frame, and a line can keep the previous value when
/// another interrupt (the link) is being handled at that moment.
#define BM_HAL_LINE_TABLES 3

/// Gives register `reg` the value `pValues[y]` on line y, from the next frame on.
/// `pValues` must hold BM_HAL_SCREEN_HEIGHT entries; the HAL keeps its own copy.
/// `pValues == NULL` switches the line table off.
void bmHalLineTableSet(uint32_t slot, uint16_t reg, const uint16_t *pValues);

/// A palette stream (lighting.md, the streamed light layer): from the next frame on, line y is
/// drawn with palette entries [first, first + count) taken from `pRows + y * count` (BG
/// entries 0-255). On the GBA, row 0 is copied in the vertical blank and DMA channel 3 copies
/// the next row in every H-blank (so it takes line table slot 1's channel while on). The rows
/// must stay readable while shown, 161 of them. NULL switches it off.
void bmHalPaletteStream(uint16_t first, uint16_t count, const uint16_t *pRows);

/// The line the display is drawing now (0-227 on the GBA; 160 and up is the vertical blank).
/// A frame starts at line 160, so this tells how much of a frame's time has been used.
uint16_t bmHalVideoLine(void);

/// A running count of processor cycles, for measuring how long something takes (wraps around;
/// subtract two readings). 0 where time is not measured (the PC).
uint32_t bmHalCycles(void);

/// Sound output: two channels (left, right) of signed 8-bit samples at BM_HAL_AUDIO_RATE,
/// BM_HAL_AUDIO_FRAME_SAMPLES per video frame (exactly one frame on the GBA: timer 0 and the
/// two sound FIFOs). After bmHalAudioStart, fill the buffers bmHalAudioBuffers returns once per
/// frame; they start playing at the next bmHalVideoWaitVBlank.
#define BM_HAL_AUDIO_RATE 18157
#define BM_HAL_AUDIO_FRAME_SAMPLES 304
void bmHalAudioStart(void);
void bmHalAudioBuffers(int8_t **ppLeft, int8_t **ppRight);

/// Debugging on the device itself (docs/spec/link-protocol.md §8). The program can be stopped
/// at breakpoints, its registers and memory read and changed, and continued. Breakpoints are
/// written over ARM instructions in RAM, so they work for code in RAM only and not for Thumb
/// code. Never put one into the link's own code or what it calls: the program has to keep
/// talking while it is stopped.
#define BM_HAL_DEBUG_SLOTS 8
typedef struct BmHalDebugRegs
{
  uint32_t r[16];   // r15: where the program continues
  uint32_t cpsr;
} BmHalDebugRegs;
/// Called when the program stops: at breakpoint `slot`, or with -1 in bmHalDebugBreak. The
/// program continues when it returns, with the registers as they then are.
typedef void (*BmHalDebugHandler)(void *pContext, BmHalDebugRegs *pRegs, int slot);
/// False where the device cannot be debugged this way (the PC: use its debugger).
bool bmHalDebugInit(BmHalDebugHandler handler, void *pContext);
/// False if no breakpoint can be put at `address` (not RAM, not an ARM instruction's place).
bool bmHalDebugSetBreak(uint32_t slot, uint32_t address);
void bmHalDebugClearBreak(uint32_t slot);
/// Takes all breakpoints out (before code in RAM is replaced).
void bmHalDebugClearAll(void);
/// Stops here.
void bmHalDebugBreak(void);
/// False if (part of) the range is not memory the debugger may read or change.
bool bmHalDebugRead(uint32_t address, void *pOut, uint32_t size);
bool bmHalDebugWrite(uint32_t address, const void *pIn, uint32_t size);

/// Live link (docs/spec/link-protocol.md): the serial port as the clocked side, 32 bits per
/// transfer. `handler` runs in the serial interrupt for every word received and returns the
/// word to send in the next transfer. `firstWord` is sent in the first one.
typedef uint32_t (*BmHalLinkHandler)(void *pContext, uint32_t received);
void bmHalLinkStart(BmHalLinkHandler handler, void *pContext, uint32_t firstWord);
void bmHalLinkStop(void);
/// Call every frame while listening (more often does no harm: it looks once per two frames
/// of real time). Words have no frame on the wire, so the port can lose
/// the word boundary (a glitch on the clock, a word that began before the port was armed). If
/// no word has come since the last call, the port is armed anew, which puts the boundary back
/// as soon as the other side is quiet for a moment.
void bmHalLinkRealign(void);
/// Starts the device over as if it had been switched on (the GBA goes back to its BIOS).
/// Returns where that cannot be done.
void bmHalReset(void);

/// Keeps the link interrupt from running (while the main loop changes what it uses).
/// Returns a value for bmHalLinkUnlock.
uint32_t bmHalLinkLock(void);
/// The serial port as the side that makes the clock (GBA to GBA, transfer.h): normal mode,
/// 32 bits, at 2 MHz (`fast`) or 256 kHz. Stop listening (bmHalLinkStop) first. An exchange
/// sends one word and returns the one received, 0xFFFFFFFF if nobody answered.
void bmHalLinkMasterBegin(bool fast);
uint32_t bmHalLinkMasterExchange(uint32_t out);
void bmHalLinkMasterEnd(void);
/// Waits about `cycles` processor cycles (between words, to give the other side time).
void bmHalLinkMasterWait(uint32_t cycles);
void bmHalLinkUnlock(uint32_t state);
