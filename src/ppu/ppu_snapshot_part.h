/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Writes any part of a PPU snapshot (BMPS, ppu.h) straight from where the video state lives,
// without a BmPpuState in between: the GBA reads its VRAM, palette and OAM and the HAL's
// register copies, so a snapshot can go over the link in packets (PPU_SNAPSHOT,
// docs/spec/link-protocol.md) without 100 KB of RAM. Built for every platform.

#include "ppu.h"

typedef struct BmPpuSnapshotSource
{
  const uint16_t *pRegs;                 // BM_PPU_REG_SIZE / 2 registers
  const volatile uint8_t *pVram;         // BM_PPU_VRAM_SIZE bytes
  const volatile uint16_t *pPalette;     // BM_PPU_PALETTE_SIZE entries
  const volatile uint16_t *pOam;         // BM_PPU_OAM_SIZE halfwords
  uint16_t lineRegs[BM_PPU_LINE_TABLES]; // 0xFFFF: unused
  const uint16_t *pLineValues[BM_PPU_LINE_TABLES];
} BmPpuSnapshotSource;

/// Copies `size` bytes of the snapshot from `offset` (fewer at the end). Returns the
/// snapshot's total size.
uint32_t bmPpuSnapshotPart(const BmPpuSnapshotSource *pSource, uint32_t offset, uint8_t *pOut, uint32_t size);
