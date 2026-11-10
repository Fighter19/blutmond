/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Runs a GBA image headless in mGBA (libmgba) for a number of frames, then checks the
// screen and/or writes it to a PPM file. Used by the tests to see that a ROM really runs.
//
//   bm_mgba_run <image.gba> [--frames N] [--ppm out.ppm] [--snapshot out.bmps] [--expect-backdrop]
//
// --check <elf>:<symbol>[+offset]=<value> (or <=<value>): after running, reads the 32-bit value
// at the symbol's address (from the ELF's symbol table) and fails if it differs (or is larger). --print does the same
// without checking. Lets tests look at the program's state on the emulated GBA.
//
// --poke <frame> <elf>:<symbol> <file>: at that frame, writes the file's size (u32) and bytes to
// the symbol (a mailbox: struct { u32 size; u8 data[]; }). --dump <elf>:<symbol> <file> writes a
// mailbox's bytes to a file after the run.
//
// --snapshot: writes the video state (registers, VRAM, palette, OAM) as a PPU snapshot that the
// software PPU can render (tools/bm_ppu_render). Line tables are not captured.
//
// --expect-backdrop: every pixel must show BG palette entry 0 (the backdrop colour), and that
// colour must not be black. This is what apps/backdrop draws.

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/gba.h>

#include "ppu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void quietLog(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
  (void)logger;
  (void)category;
  if (level & (mLOG_FATAL | mLOG_ERROR))
  {
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
  }
}

static uint16_t toBgr555(color_t c)
{
  // libmgba's 32-bit colour is XBGR8: red in the lowest byte.
  return (uint16_t)(((c & 0xF8) >> 3) | ((c & 0xF800) >> 6) | ((c & 0xF80000) >> 9));
}

static int writePpm(const char *path, const color_t *buffer, unsigned width, unsigned height)
{
  FILE *f = fopen(path, "wb");
  if (f == NULL)
  {
    perror(path);
    return 1;
  }
  fprintf(f, "P6\n%u %u\n255\n", width, height);
  for (unsigned i = 0; i < width * height; i++)
  {
    unsigned char rgb[3] = { buffer[i] & 0xFF, (buffer[i] >> 8) & 0xFF, (buffer[i] >> 16) & 0xFF };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return 0;
}

// Looks up a symbol in an ELF32 file. Returns 0 if it is not there.
static uint32_t elfSymbol(const char *path, const char *name)
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

// "<elf>:<symbol>[+offset]=<value>" (value optional); returns false on a mismatch.
static bool checkValue(struct mCore *core, const char *spec, bool check)
{
  char buf[512];
  snprintf(buf, sizeof(buf), "%s", spec);
  char *colon = strrchr(buf, ':');
  char *eq = strchr(colon ? colon : buf, '=');
  bool atMost = eq != NULL && eq > buf && eq[-1] == '<';
  if (atMost) eq[-1] = 0;
  if (colon == NULL)
  {
    fprintf(stderr, "bad --check %s\n", spec);
    return false;
  }
  *colon = 0;
  if (eq) *eq = 0;
  char *plus = strchr(colon + 1, '+');
  uint32_t offset = 0;
  if (plus)
  {
    *plus = 0;
    offset = (uint32_t)strtoul(plus + 1, NULL, 0);
  }
  uint32_t address = elfSymbol(buf, colon + 1);
  if (address == 0)
  {
    fprintf(stderr, "symbol %s not found in %s\n", colon + 1, buf);
    return false;
  }
  uint32_t value = core->busRead32(core, address + offset);
  printf("%s%s%s = %u\n", colon + 1, plus ? "+" : "", plus ? plus + 1 : "", value);
  uint32_t expected = eq ? (uint32_t)strtoul(eq + 1, NULL, 0) : 0;
  if (check && eq && (atMost ? value > expected : value != expected))
  {
    fprintf(stderr, "%s%s%s: expected %s%s, found %u\n", colon + 1, plus ? "+" : "", plus ? plus + 1 : "",
            atMost ? "at most " : "", eq + 1, value);
    return false;
  }
  return true;
}

// "<elf>:<symbol>" -> address, or 0.
static uint32_t symbolAddress(const char *target)
{
  char buf[512];
  snprintf(buf, sizeof(buf), "%s", target);
  char *colon = strrchr(buf, ':');
  if (colon == NULL) return 0;
  *colon = 0;
  return elfSymbol(buf, colon + 1);
}

static bool pokeMailbox(struct mCore *core, const char *target, const char *file)
{
  uint32_t address = symbolAddress(target);
  FILE *f = fopen(file, "rb");
  if (address == 0 || f == NULL)
  {
    fprintf(stderr, "--poke: cannot use %s / %s\n", target, file);
    if (f) fclose(f);
    return false;
  }
  uint8_t data[4096];
  size_t n = fread(data, 1, sizeof(data), f);
  fclose(f);
  for (size_t i = 0; i < n; i++) core->busWrite8(core, address + 4 + (uint32_t)i, data[i]);
  core->busWrite32(core, address, (uint32_t)n);  // size last: the program checks it
  printf("poked %zu bytes into %s\n", n, target);
  return true;
}

static bool dumpMailbox(struct mCore *core, const char *target, const char *file)
{
  uint32_t address = symbolAddress(target);
  if (address == 0) return false;
  uint32_t n = core->busRead32(core, address);
  if (n > 4096) return false;
  FILE *f = fopen(file, "wb");
  if (f == NULL) return false;
  for (uint32_t i = 0; i < n; i++) fputc(core->busRead8(core, address + 4 + i), f);
  fclose(f);
  printf("dumped %u bytes from %s\n", n, target);
  return true;
}

static int writeSnapshot(struct mCore *core, const char *path)
{
  static BmPpuState state;
  bmPpuReset(&state);
  // Video registers are write-only on the GBA, so they come from mGBA's internal copy.
  const struct GBA *gba = core->board;
  for (int i = 0; i < BM_PPU_REG_SIZE / 2; i++)
  {
    state.regs[i] = gba->memory.io[i];
  }
  for (uint32_t i = 0; i < BM_PPU_VRAM_SIZE; i += 2)
  {
    uint16_t v = (uint16_t)core->busRead16(core, 0x06000000u + i);
    state.vram[i] = (uint8_t)v;
    state.vram[i + 1] = (uint8_t)(v >> 8);
  }
  for (uint32_t i = 0; i < BM_PPU_PALETTE_SIZE; i++)
  {
    state.palette[i] = (uint16_t)core->busRead16(core, 0x05000000u + i * 2);
  }
  for (uint32_t i = 0; i < BM_PPU_OAM_SIZE; i++)
  {
    state.oam[i] = (uint16_t)core->busRead16(core, 0x07000000u + i * 2);
  }

  size_t size = bmPpuSnapshotSize();
  uint8_t *data = malloc(size);
  bmPpuSnapshotWrite(&state, data);
  FILE *f = fopen(path, "wb");
  if (f == NULL)
  {
    perror(path);
    free(data);
    return 1;
  }
  fwrite(data, 1, size, f);
  fclose(f);
  free(data);
  return 0;
}

int main(int argc, char **argv)
{
  const char *rom = NULL;
  const char *ppm = NULL;
  int frames = 60;
  int expectBackdrop = 0;
  const char *snapshot = NULL;
  int pokeFrame = -1;
  const char *pokeTarget = NULL, *pokeFile = NULL, *dumpTarget = NULL, *dumpFile = NULL;
  const char *checks[16];
  bool checkFlags[16];
  int checkCount = 0;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      frames = atoi(argv[++i]);
    else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc)
      ppm = argv[++i];
    else if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc)
      snapshot = argv[++i];
    else if (strcmp(argv[i], "--poke") == 0 && i + 3 < argc)
    {
      pokeFrame = atoi(argv[++i]);
      pokeTarget = argv[++i];
      pokeFile = argv[++i];
    }
    else if (strcmp(argv[i], "--dump") == 0 && i + 2 < argc)
    {
      dumpTarget = argv[++i];
      dumpFile = argv[++i];
    }
    else if ((strcmp(argv[i], "--check") == 0 || strcmp(argv[i], "--print") == 0) && i + 1 < argc && checkCount < 16)
    {
      checkFlags[checkCount] = strcmp(argv[i], "--check") == 0;
      checks[checkCount++] = argv[++i];
    }
    else if (strcmp(argv[i], "--expect-backdrop") == 0)
      expectBackdrop = 1;
    else if (argv[i][0] != '-' && rom == NULL)
      rom = argv[i];
    else
    {
      fprintf(stderr, "usage: %s <image.gba> [--frames N] [--ppm out.ppm] [--snapshot out.bmps] [--expect-backdrop]\n", argv[0]);
      return 2;
    }
  }
  if (rom == NULL)
  {
    fprintf(stderr, "no image given\n");
    return 2;
  }

  static struct mLogger logger = { .log = quietLog };
  mLogSetDefaultLogger(&logger);

  struct mCore *core = mCoreFind(rom);
  if (core == NULL || !core->init(core))
  {
    fprintf(stderr, "%s: not a GBA image mGBA can run\n", rom);
    return 1;
  }
  mCoreInitConfig(core, NULL);

  unsigned width = 0, height = 0;
  core->desiredVideoDimensions(core, &width, &height);
  color_t *buffer = calloc((size_t)width * height, sizeof(color_t));
  core->setVideoBuffer(core, buffer, width);

  if (!mCoreLoadFile(core, rom))
  {
    fprintf(stderr, "%s: cannot load\n", rom);
    return 1;
  }
  core->reset(core);
  for (int i = 0; i < frames; i++)
  {
    if (i == pokeFrame && !pokeMailbox(core, pokeTarget, pokeFile)) return 1;
    core->runFrame(core);
  }

  // BG palette entry 0 as the program left it.
  uint16_t backdrop = (uint16_t)core->busRead16(core, 0x05000000u);
  printf("%s: %d frames, %ux%u, backdrop 0x%04X, pixel(0,0) 0x%04X\n",
         rom, frames, width, height, backdrop, toBgr555(buffer[0]));

  int failed = 0;
  if (ppm != NULL && writePpm(ppm, buffer, width, height) != 0)
  {
    failed = 1;
  }
  if (snapshot != NULL && writeSnapshot(core, snapshot) != 0)
  {
    failed = 1;
  }
  for (int i = 0; i < checkCount; i++)
  {
    if (!checkValue(core, checks[i], checkFlags[i])) failed = 1;
  }
  if (dumpTarget != NULL && !dumpMailbox(core, dumpTarget, dumpFile)) failed = 1;
  if (expectBackdrop)
  {
    unsigned mismatches = 0;
    for (unsigned i = 0; i < width * height; i++)
    {
      if (toBgr555(buffer[i]) != (backdrop & 0x7FFF))
      {
        mismatches++;
      }
    }
    if (mismatches != 0 || backdrop == 0)
    {
      fprintf(stderr, "expected the whole screen in a non-black backdrop colour; %u pixels differ\n", mismatches);
      failed = 1;
    }
  }

  mCoreConfigDeinit(&core->config);
  core->deinit(core);
  free(buffer);
  return failed;
}
