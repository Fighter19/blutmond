/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// GBA hardware addresses. Only the GBA back end of the HAL may include this file.

#include <stdint.h>

#define GBA_SCREEN_HEIGHT 160

#define GBA_IO            0x04000000u
#define GBA_VRAM          0x06000000u
#define GBA_VRAM_SIZE     0x18000u
#define GBA_PALETTE       ((volatile uint16_t *)0x05000000u)
#define GBA_OAM           ((volatile uint16_t *)0x07000000u)

#define GBA_REG16(offset) (*(volatile uint16_t *)(GBA_IO + (offset)))
#define GBA_REG32(offset) (*(volatile uint32_t *)(GBA_IO + (offset)))

#define GBA_REG_DISPCNT   GBA_REG16(0x000)
#define GBA_REG_VCOUNT    GBA_REG16(0x006)

// DMA channel 0 (highest priority; used for line tables)
#define GBA_REG_DMA0SAD   GBA_REG32(0x0B0)
#define GBA_REG_DMA0DAD   GBA_REG32(0x0B4)
#define GBA_REG_DMA0CNT_L GBA_REG16(0x0B8)
#define GBA_REG_DMA0CNT_H GBA_REG16(0x0BA)

#define GBA_DMA_DEST_FIXED   (2u << 5)
#define GBA_DMA_REPEAT       (1u << 9)
#define GBA_DMA_START_HBLANK (2u << 12)
#define GBA_DMA_ENABLE       (1u << 15)
