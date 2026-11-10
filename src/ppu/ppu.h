/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Software PPU: renders the GBA's video state (registers, VRAM, palette, OAM, line table) in
// portable C. It is the reference renderer for tests (golden images), the host back end of
// the HAL and the editor. See docs/PLAN.md section 5.
//
// Registers are kept in the GBA's I/O layout (byte offsets from the start of the I/O area),
// which is the platform-neutral description of the "virtual PPU" used everywhere in Blutmond.
//
// Supported in this version: mode 0 (four regular backgrounds, 4 and 8 bpp, all sizes,
// scrolling, flips, palette banks, priorities), regular sprites (all sizes, 4 and 8 bpp, 1D and
// 2D mapping, flips, priorities), forced blank and per-line register values. Not yet: affine
// backgrounds and sprites, bitmap modes, windows, blending, mosaic.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BM_PPU_WIDTH 240
#define BM_PPU_HEIGHT 160

#define BM_PPU_VRAM_SIZE 0x18000
#define BM_PPU_PALETTE_SIZE 512   // entries (BG 0..255, OBJ 256..511)
#define BM_PPU_OAM_SIZE 512       // halfwords (128 sprites x 4)
#define BM_PPU_REG_SIZE 0x60      // bytes: DISPCNT .. BLDY

// Register byte offsets (GBA I/O layout).
enum {
  BM_PPU_DISPCNT = 0x00,
  BM_PPU_BG0CNT = 0x08,  // BGnCNT  = 0x08 + 2n
  BM_PPU_BG0HOFS = 0x10, // BGnHOFS = 0x10 + 4n
  BM_PPU_BG0VOFS = 0x12, // BGnVOFS = 0x12 + 4n
};

#define BM_PPU_BGCNT(n) (BM_PPU_BG0CNT + 2 * (n))
#define BM_PPU_BGHOFS(n) (BM_PPU_BG0HOFS + 4 * (n))
#define BM_PPU_BGVOFS(n) (BM_PPU_BG0VOFS + 4 * (n))

// DISPCNT bits
#define BM_PPU_DISPCNT_MODE_MASK 0x0007
#define BM_PPU_DISPCNT_OBJ_1D 0x0040
#define BM_PPU_DISPCNT_FORCED_BLANK 0x0080
#define BM_PPU_DISPCNT_BG(n) (0x0100 << (n))
#define BM_PPU_DISPCNT_OBJ 0x1000

/// Number of registers that can change per line (HDMA channels on the GBA).
#define BM_PPU_LINE_TABLES 1

typedef struct BmPpuLineTable
{
  /// Register byte offset, or 0xFFFF when unused.
  uint16_t reg;
  uint16_t values[BM_PPU_HEIGHT];
} BmPpuLineTable;

typedef struct BmPpuState
{
  uint16_t regs[BM_PPU_REG_SIZE / 2];
  uint8_t vram[BM_PPU_VRAM_SIZE];
  uint16_t palette[BM_PPU_PALETTE_SIZE];
  uint16_t oam[BM_PPU_OAM_SIZE];
  BmPpuLineTable lineTables[BM_PPU_LINE_TABLES];
} BmPpuState;

/// Power-on state: all zero, line tables unused.
void bmPpuReset(BmPpuState *pState);

static inline uint16_t bmPpuReg(const BmPpuState *pState, uint16_t offset)
{
  return pState->regs[offset / 2];
}

static inline void bmPpuSetReg(BmPpuState *pState, uint16_t offset, uint16_t value)
{
  if (offset < BM_PPU_REG_SIZE)
  {
    pState->regs[offset / 2] = value;
  }
}

/// Renders one line into `out` (BGR555).
void bmPpuRenderLine(const BmPpuState *pState, int y, uint16_t out[BM_PPU_WIDTH]);

/// Renders the whole screen into `out` (BGR555, row major).
void bmPpuRender(const BmPpuState *pState, uint16_t out[BM_PPU_WIDTH * BM_PPU_HEIGHT]);

// --- Snapshots ---------------------------------------------------------------------------
//
// A PPU snapshot ("BMPS") stores a BmPpuState: header (magic "BMPS", u16 version, u16 line
// table count), registers, VRAM, palette, OAM, line tables; all little endian. See
// docs/spec/link-protocol.md (PPU_SNAPSHOT).

#define BM_PPU_SNAPSHOT_VERSION 1

/// Bytes a snapshot needs.
size_t bmPpuSnapshotSize(void);
/// Writes a snapshot into `pOut` (bmPpuSnapshotSize() bytes).
void bmPpuSnapshotWrite(const BmPpuState *pState, uint8_t *pOut);
/// Reads a snapshot. Returns false if the data is not a snapshot of a known version.
bool bmPpuSnapshotRead(BmPpuState *pState, const uint8_t *pData, size_t size);
