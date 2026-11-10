/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "hal.h"
#include "hal_host.h"

// The host back end models the GBA's memories as plain arrays of the same sizes, so memory
// budgets behave the same on the PC and on the device.
static uint8_t s_ewram[256 * 1024];
static uint8_t s_iwram[32 * 1024];
static uint8_t s_vram[96 * 1024];

static BmHalHostVideo s_video;

const char *bmHalPlatformName(void)
{
  return "host";
}

void bmHalGetMemoryRanges(BmHalMemoryRange *pRanges, uint32_t count)
{
  const BmHalMemoryRange ranges[3] = {
    { s_ewram, sizeof(s_ewram) },
    { s_iwram, sizeof(s_iwram) },
    { s_vram, sizeof(s_vram) },
  };
  for (uint32_t i = 0; i < count; i++)
  {
    pRanges[i] = i < 3 ? ranges[i] : (BmHalMemoryRange){ NULL, 0 };
  }
}

void bmHalVideoInit(void)
{
  s_video.displayOn = true;
  s_video.mode = 0;
  s_video.layerMask = 0;
}

void bmHalPaletteWrite(uint16_t index, uint16_t bgr555)
{
  if (index < BM_HAL_PALETTE_SIZE)
  {
    s_video.palette[index] = bgr555 & 0x7FFF;
  }
}

uint16_t bmHalPaletteRead(uint16_t index)
{
  return index < BM_HAL_PALETTE_SIZE ? s_video.palette[index] : 0;
}

uint32_t bmHalVideoWaitVBlank(void)
{
  return ++s_video.frame;
}

const BmHalHostVideo *bmHalHostGetVideo(void)
{
  return &s_video;
}

void bmHalHostReset(void)
{
  s_video = (BmHalHostVideo){0};
}
