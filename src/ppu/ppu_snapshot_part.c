/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "ppu_snapshot_part.h"

// The snapshot layout (ppu.h): header, registers, VRAM, palette, OAM, line tables.
#define HEADER 8u
#define REGS (HEADER)
#define VRAM (REGS + BM_PPU_REG_SIZE)
#define PALETTE (VRAM + BM_PPU_VRAM_SIZE)
#define OAM (PALETTE + BM_PPU_PALETTE_SIZE * 2u)
#define LINES (OAM + BM_PPU_OAM_SIZE * 2u)
#define LINE_TABLE_SIZE (2u + BM_PPU_HEIGHT * 2u)
#define TOTAL (LINES + BM_PPU_LINE_TABLES * LINE_TABLE_SIZE)

static uint8_t halfwordByte(uint16_t v, uint32_t at)
{
  return (uint8_t)((at & 1) ? v >> 8 : v);
}

static uint8_t byteAt(const BmPpuSnapshotSource *s, uint32_t at)
{
  static const uint8_t header[HEADER] = { 'B', 'M', 'P', 'S', BM_PPU_SNAPSHOT_VERSION, 0, BM_PPU_LINE_TABLES, 0 };
  if (at < REGS) return header[at];
  if (at < VRAM) return halfwordByte(s->pRegs[(at - REGS) / 2], at - REGS);
  if (at < PALETTE) return s->pVram[at - VRAM];
  if (at < OAM) return halfwordByte(s->pPalette[(at - PALETTE) / 2], at - PALETTE);
  if (at < LINES) return halfwordByte(s->pOam[(at - OAM) / 2], at - OAM);
  uint32_t t = (at - LINES) / LINE_TABLE_SIZE, in = (at - LINES) % LINE_TABLE_SIZE;
  if (in < 2) return halfwordByte(s->lineRegs[t], in);
  uint16_t v = s->pLineValues[t] != NULL ? s->pLineValues[t][(in - 2) / 2] : 0;
  return halfwordByte(v, in);
}

uint32_t bmPpuSnapshotPart(const BmPpuSnapshotSource *pSource, uint32_t offset, uint8_t *pOut, uint32_t size)
{
  for (uint32_t i = 0; i < size && offset + i < TOTAL; i++) pOut[i] = byteAt(pSource, offset + i);
  return TOTAL;
}
