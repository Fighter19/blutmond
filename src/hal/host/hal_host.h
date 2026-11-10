/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Host-only access to the virtual hardware, for tests and the editor.

#include <stdbool.h>
#include <stdint.h>

#include "hal.h"

typedef struct BmHalHostVideo
{
  bool displayOn;
  uint8_t mode;
  uint8_t layerMask;
  uint32_t frame;
  uint16_t palette[BM_HAL_PALETTE_SIZE];
} BmHalHostVideo;

const BmHalHostVideo *bmHalHostGetVideo(void);

/// Puts the virtual hardware back into its power-on state.
void bmHalHostReset(void);
