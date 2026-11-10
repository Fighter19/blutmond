/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Symbol lookup in ELF32 files, for tools that look into programs running in mGBA.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Looks up a symbol in an ELF32 file. Returns 0 if it is not there.
static inline uint32_t elfSymbol(const char *path, const char *name)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) return 0;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc((size_t)size);
  uint32_t found = 0;
  if (fread(d, 1, (size_t)size, f) == (size_t)size && size > 52 && memcmp(d, "\x7f" "ELF", 4) == 0 && d[4] == 1)
  {
#define R16(o) ((uint32_t)d[o] | ((uint32_t)d[(o) + 1] << 8))
#define R32(o) (R16(o) | (R16((o) + 2) << 16))
    uint32_t shoff = R32(32), shentsize = R16(46), shnum = R16(48);
    for (uint32_t i = 0; i < shnum && !found; i++)
    {
      uint32_t sh = shoff + i * shentsize;
      if (sh + 40 > (uint32_t)size || R32(sh + 4) != 2) continue;  // SHT_SYMTAB
      uint32_t symOff = R32(sh + 16), symSize = R32(sh + 20), strSec = R32(sh + 24);
      uint32_t strOff = R32(shoff + strSec * shentsize + 16);
      for (uint32_t s = symOff; s + 16 <= symOff + symSize && s + 16 <= (uint32_t)size; s += 16)
      {
        uint32_t nameOff = strOff + R32(s);
        if (nameOff < (uint32_t)size && strcmp((const char *)d + nameOff, name) == 0)
        {
          found = R32(s + 4);
          break;
        }
      }
    }
#undef R16
#undef R32
  }
  free(d);
  fclose(f);
  return found;
}

