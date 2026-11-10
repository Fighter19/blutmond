/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Renders a PPU snapshot with the software PPU:  bm_ppu_render <in.bmps> <out.ppm>

#include "ppu.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  if (argc != 3)
  {
    fprintf(stderr, "usage: %s <in.bmps> <out.ppm>\n", argv[0]);
    return 2;
  }

  FILE *f = fopen(argv[1], "rb");
  if (f == NULL)
  {
    perror(argv[1]);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)size);
  size_t got = fread(data, 1, (size_t)size, f);
  fclose(f);

  static BmPpuState state;
  if (got != (size_t)size || !bmPpuSnapshotRead(&state, data, got))
  {
    fprintf(stderr, "%s: not a PPU snapshot\n", argv[1]);
    free(data);
    return 1;
  }
  free(data);

  static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmPpuRender(&state, pixels);

  FILE *out = fopen(argv[2], "wb");
  if (out == NULL)
  {
    perror(argv[2]);
    return 1;
  }
  fprintf(out, "P6\n%d %d\n255\n", BM_PPU_WIDTH, BM_PPU_HEIGHT);
  for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
  {
    uint8_t rgb[3];
    for (int c = 0; c < 3; c++)
    {
      uint8_t v = (uint8_t)((pixels[i] >> (5 * c)) & 31);
      rgb[c] = (uint8_t)((v << 3) | (v >> 2));
    }
    fwrite(rgb, 1, 3, out);
  }
  fclose(out);
  return 0;
}
