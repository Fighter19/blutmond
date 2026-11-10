/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Test scene for the video API and the software PPU. On the GBA it shows the scene forever; on
// the host it renders the scene with the software PPU and writes it to a PPM file:
//   scene_test [out.ppm] [--no-line-table]

#include "device.h"
#include "physical_device.h"
#include "scene.h"
#include "video.h"

#ifdef BM_PLATFORM_HOST
#  include "hal_host.h"
#  include <stdio.h>
#  include <string.h>
#endif

#ifndef BM_SCENE_LINE_TABLE
#  define BM_SCENE_LINE_TABLE 1
#endif

#ifdef BM_PLATFORM_HOST
static int writePpm(const char *path, const uint16_t *pixels)
{
  FILE *f = fopen(path, "wb");
  if (f == NULL)
  {
    perror(path);
    return 1;
  }
  fprintf(f, "P6\n%d %d\n255\n", BM_PPU_WIDTH, BM_PPU_HEIGHT);
  for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
  {
    // 5 to 8 bits the way mGBA expands colours, so images can be compared byte for byte.
    uint8_t rgb[3];
    for (int c = 0; c < 3; c++)
    {
      uint8_t v = (uint8_t)((pixels[i] >> (5 * c)) & 31);
      rgb[c] = (uint8_t)((v << 3) | (v >> 2));
    }
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return 0;
}
#endif

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  BmDeviceHandle device = NULL;
  if (bmCreateDevice(bmGetPhysicalDevice(), &device) != BM_SUCCESS)
  {
    return 1;
  }
  g_defaultDevice = device;

  bool lineTable = BM_SCENE_LINE_TABLE;
#ifdef BM_PLATFORM_HOST
  const char *out = NULL;
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--no-line-table") == 0) lineTable = false;
    else out = argv[i];
  }
#endif

  sceneSetup(lineTable);

#ifdef BM_PLATFORM_HOST
  bmVideoWaitFrame();
  bmVideoWaitFrame();
  static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmHalHostRender(pixels);
  if (out != NULL && writePpm(out, pixels) != 0)
  {
    return 1;
  }
  printf("scene rendered%s%s\n", out ? " to " : "", out ? out : "");
  return 0;
#else
  for (;;)
  {
    bmVideoWaitFrame();
  }
#endif
}
