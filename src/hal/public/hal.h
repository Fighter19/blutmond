/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Hardware abstraction layer. This is the only interface between the engine and a platform.
// Hardware addresses exist only in the platform back ends (src/hal/<platform>/) and in their
// linker scripts. See docs/PLAN.md section 3.

#include <stddef.h>
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

/// Reads one palette entry (BGR555).
uint16_t bmHalPaletteRead(uint16_t index);

/// Waits for the start of the next vertical blank and restarts the line tables for the next
/// frame. Returns the frame counter after the wait.
uint32_t bmHalVideoWaitVBlank(void);

/// Writes a video register. `offset` is the byte offset in the GBA's I/O layout
/// (BM_PPU_DISPCNT, BM_PPU_BGCNT(n), ... in ppu.h), the platform-neutral register model.
void bmHalVideoWriteReg(uint16_t offset, uint16_t value);
uint16_t bmHalVideoReadReg(uint16_t offset);

/// Copies data into video memory. `offset` and `size` must be even (VRAM only takes 16- and
/// 32-bit writes on the GBA).
void bmHalVramWrite(uint32_t offset, const void *pData, uint32_t size);

/// Copies `count` halfwords into OAM starting at halfword `first`. Call during VBlank.
void bmHalOamWrite(uint32_t first, const uint16_t *pData, uint32_t count);

/// Number of line tables (registers that can take a new value on every line).
#define BM_HAL_LINE_TABLES 1

/// Gives register `reg` the value `pValues[y]` on line y, from the next frame on.
/// `pValues` must hold BM_HAL_SCREEN_HEIGHT entries; the HAL keeps its own copy.
/// `pValues == NULL` switches the line table off.
void bmHalLineTableSet(uint32_t slot, uint16_t reg, const uint16_t *pValues);
