/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// GBA hardware addresses. Only the GBA back end of the HAL may include this file.

#include <stdint.h>

#define GBA_SCREEN_HEIGHT 160

#define GBA_VRAM          0x06000000u
#define GBA_VRAM_SIZE     0x18000u
#define GBA_PALETTE       ((volatile uint16_t *)0x05000000u)

#define GBA_REG_DISPCNT   (*(volatile uint16_t *)0x04000000u)
#define GBA_REG_VCOUNT    (*(volatile uint16_t *)0x04000006u)
