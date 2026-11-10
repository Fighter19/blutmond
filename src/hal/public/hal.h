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

/// Waits for the start of the next vertical blank. Returns the frame counter after the wait.
uint32_t bmHalVideoWaitVBlank(void);
