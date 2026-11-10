/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Runs a GBA image headless in mGBA (libmgba) for a number of frames, then checks the
// screen and/or writes it to a PPM file. Used by the tests to see that a ROM really runs.
//
//   bm_mgba_run <image.gba> [--frames N] [--ppm out.ppm] [--expect-backdrop]
//
// --expect-backdrop: every pixel must show BG palette entry 0 (the backdrop colour), and that
// colour must not be black. This is what apps/backdrop draws.

#include <mgba/core/core.h>
#include <mgba/core/log.h>

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

int main(int argc, char **argv)
{
  const char *rom = NULL;
  const char *ppm = NULL;
  int frames = 60;
  int expectBackdrop = 0;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      frames = atoi(argv[++i]);
    else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc)
      ppm = argv[++i];
    else if (strcmp(argv[i], "--expect-backdrop") == 0)
      expectBackdrop = 1;
    else if (argv[i][0] != '-' && rom == NULL)
      rom = argv[i];
    else
    {
      fprintf(stderr, "usage: %s <image.gba> [--frames N] [--ppm out.ppm] [--expect-backdrop]\n", argv[0]);
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

  core->deinit(core);
  free(buffer);
  return failed;
}
