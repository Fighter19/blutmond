/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Shows the first room of a pack. The camera follows a fixed path (so tests can compare
// frames) until a button is pressed; then the D-pad moves it.
//
// GBA: the pack is embedded in the image.
// Host: room_viewer <pack.bmpk> [--frames N] [--fresh] [out.ppm]   renders frame N with the
//       software PPU. --fresh shows the room directly at frame N's camera position instead of
//       scrolling there, which must give the same picture (tests the map streaming).

#include "device.h"
#include "hal.h"
#include "pack.h"
#include "physical_device.h"
#include "room.h"
#include "video.h"

#ifdef BM_PLATFORM_HOST
#  include "hal_host.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#else
extern const uint8_t bm_embedded_pack[];
extern const uint8_t bm_embedded_pack_end[];
#endif

/// Camera on the demo path: right and a little down for 300 frames, then it stays.
static void demoCamera(uint32_t frame, int32_t *pX, int32_t *pY)
{
  uint32_t t = frame < 300 ? frame : 300;
  *pX = (int32_t)(t * 7 / 3);
  *pY = (int32_t)(t / 10);
}

static BmPack s_pack;
static BmRoom s_room;

static int run(const uint8_t *pPackData, uint32_t packSize, uint32_t frames, bool fresh)
{
  BmDeviceHandle device = NULL;
  if (bmCreateDevice(bmGetPhysicalDevice(), &device) != BM_SUCCESS)
  {
    return 1;
  }
  g_defaultDevice = device;

  if (bmPackOpen(&s_pack, pPackData, packSize, false) != BM_PACK_OK || bmRoomOpen(&s_room, &s_pack, 0) != BM_SUCCESS)
  {
    return 2;
  }

  bmVideoInit();
  int32_t x = 0, y = 0;
  demoCamera(fresh ? frames : 0, &x, &y);
  if (bmRoomShow(&s_room, x, y) != BM_SUCCESS)
  {
    return 3;
  }
  if (fresh)
  {
    bmVideoWaitFrame();
    return 0;
  }

  bool manual = false;
  for (uint32_t frame = 1; frames == 0 || frame <= frames; frame++)
  {
    bmVideoWaitFrame();
    uint16_t keys = bmHalInputKeys();
    if (keys)
    {
      manual = true;
    }
    if (manual)
    {
      x = s_room.cameraX + ((keys & BM_KEY_RIGHT) ? 2 : 0) - ((keys & BM_KEY_LEFT) ? 2 : 0);
      y = s_room.cameraY + ((keys & BM_KEY_DOWN) ? 2 : 0) - ((keys & BM_KEY_UP) ? 2 : 0);
    }
    else
    {
      demoCamera(frame, &x, &y);
    }
    bmRoomSetCamera(&s_room, x, y);
  }
  return 0;
}

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

int main(int argc, char **argv)
{
  const char *packPath = NULL, *out = NULL;
  uint32_t frames = 1;
  bool fresh = false;
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--fresh") == 0) fresh = true;
    else if (packPath == NULL) packPath = argv[i];
    else out = argv[i];
  }
  if (packPath == NULL)
  {
    fprintf(stderr, "usage: room_viewer <pack.bmpk> [--frames N] [--fresh] [out.ppm]\n");
    return 2;
  }
  FILE *f = fopen(packPath, "rb");
  if (f == NULL)
  {
    perror(packPath);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)size);
  if (fread(data, 1, (size_t)size, f) != (size_t)size)
  {
    fclose(f);
    return 1;
  }
  fclose(f);

  int result = run(data, (uint32_t)size, frames, fresh);
  if (result == 0 && out != NULL)
  {
    static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
    bmHalHostRender(pixels);
    result = writePpm(out, pixels);
  }
  if (result == 0)
  {
    printf("room %ux%u, camera %d,%d after %u frames\n", s_room.widthPx, s_room.heightPx,
           (int)s_room.cameraX, (int)s_room.cameraY, frames);
  }
  free(data);
  return result;
}
#else
int main(void)
{
  return run(bm_embedded_pack, (uint32_t)(bm_embedded_pack_end - bm_embedded_pack), 0, false);
}
#endif
