/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Smallest Blutmond program: starts the engine and fades the backdrop colour through a
// blood-moon red. Builds for the host and as a GBA cartridge and multiboot image.

#include "backdrop.h"
#include "device.h"
#include "hal.h"
#include "physical_device.h"

#ifdef BM_PLATFORM_HOST
#  include <stdio.h>
// On the host the program stops after a few frames; on the GBA it runs forever.
#  define BM_HOST_FRAMES 64u
#endif

int main(void)
{
  BmDeviceHandle device = NULL;
  if (bmCreateDevice(bmGetPhysicalDevice(), &device) != BM_SUCCESS)
  {
    return 1;
  }
  g_defaultDevice = device;

  bmHalVideoInit();

  for (uint32_t frame = 0;; frame++)
  {
#ifdef BM_PLATFORM_HOST
    if (frame == BM_HOST_FRAMES)
    {
      break;
    }
#endif
    // Triangle wave 0..31..0 over 64 frames.
    uint32_t t = frame & 63;
    uint8_t level = (uint8_t)(t < 32 ? t : 63 - t);
    bmBackdropSetColor(bmColorRGB((uint8_t)(8 + level * 23 / 31), 0, (uint8_t)(level / 8)));
    bmHalVideoWaitVBlank();
  }

#ifdef BM_PLATFORM_HOST
  printf("%s: backdrop 0x%04X after %u frames\n", bmHalPlatformName(),
         bmBackdropGetColor().color, BM_HOST_FRAMES);
#endif
  return 0;
}
