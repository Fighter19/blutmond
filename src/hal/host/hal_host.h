/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Host-only access to the virtual hardware, for tests, tools and the editor.

#include <stdint.h>

#include "hal.h"
#include "ppu.h"

/// The video state as the software PPU sees it.
const BmPpuState *bmHalHostGetPpu(void);

/// Frames waited for so far.
uint32_t bmHalHostFrame(void);

/// Renders the current video state (BGR555, 240x160).
void bmHalHostRender(uint16_t *pOut);

/// Buttons that bmHalInputKeys() reports from now on.
void bmHalHostSetKeys(uint16_t keys);

/// Puts the virtual hardware back into its power-on state.
void bmHalHostReset(void);
