/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// A cartridge image of the game can carry its pack after the program: the editor's Export
// (editor/rom_export.h) appends the cooked pack to the runtime image and writes where it
// starts and how long it is into this slot. The runtime looks for the slot by its symbol;
// the editor finds it by the magic. An empty slot (size 0) means: play the embedded pack.

#include <stdint.h>

#define BM_ROM_SLOT_MAGIC "BMROMPACKSLT"   // 12 characters, no terminator stored
#define BM_ROM_SLOT_MAGIC_SIZE 12
#define BM_ROM_SLOT_ALIGN 256              // the appended pack starts at a multiple of this

typedef struct BmRomPackSlot
{
  char magic[BM_ROM_SLOT_MAGIC_SIZE];
  uint32_t offset;   // from the start of the cartridge ROM
  uint32_t size;     // bytes; 0: no pack appended
} BmRomPackSlot;
