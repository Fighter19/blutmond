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
#define GBA_SRAM          ((volatile uint8_t *)0x0E000000u)

#define GBA_REG16(offset) (*(volatile uint16_t *)(GBA_IO + (offset)))
#define GBA_REG32(offset) (*(volatile uint32_t *)(GBA_IO + (offset)))

#define GBA_REG_DISPCNT   GBA_REG16(0x000)
#define GBA_REG_VCOUNT    GBA_REG16(0x006)
#define GBA_REG_KEYINPUT  GBA_REG16(0x130)
#define GBA_REG_DISPSTAT  GBA_REG16(0x004)
#define GBA_REG_IE        GBA_REG16(0x200)
#define GBA_REG_IME       GBA_REG16(0x208)

// Emulators' own registers (bmHalScreen): mGBA answers 0x1DEA once 0xC0DE is written to its
// debug register; no$gba shows its name at 0x04FFFA00. On a GBA both are open bus.
#define GBA_REG_MGBA_DEBUG GBA_REG16(0xFFF780)
#define GBA_NOCASH_NAME   (*(volatile uint32_t *)(GBA_IO + 0xFFFA00))

// The BIOS jumps to the address stored here on interrupts.
#define GBA_BIOS_IRQ_VECTOR (*(void (*volatile *)(void))0x03007FFCu)

// DMA channel 0 (highest priority; used for line tables)
#define GBA_REG_DMA0SAD   GBA_REG32(0x0B0)
#define GBA_REG_DMA0DAD   GBA_REG32(0x0B4)
#define GBA_REG_DMA0CNT_L GBA_REG16(0x0B8)
#define GBA_REG_DMA0CNT_H GBA_REG16(0x0BA)

// DMA channel 3 (the second line table)
#define GBA_REG_DMA3SAD   GBA_REG32(0x0D4)
#define GBA_REG_DMA3DAD   GBA_REG32(0x0D8)
#define GBA_REG_DMA3CNT_L GBA_REG16(0x0DC)
#define GBA_REG_DMA3CNT_H GBA_REG16(0x0DE)

// DMA channels 1 and 2 feed the sound FIFOs; timer 0 sets the sample rate.
#define GBA_REG_DMA1SAD   GBA_REG32(0x0BC)
#define GBA_REG_DMA1DAD   GBA_REG32(0x0C0)
#define GBA_REG_DMA1CNT_H GBA_REG16(0x0C6)
#define GBA_REG_DMA2SAD   GBA_REG32(0x0C8)
#define GBA_REG_DMA2DAD   GBA_REG32(0x0CC)
#define GBA_REG_DMA2CNT_H GBA_REG16(0x0D2)
#define GBA_REG_SOUNDCNT_H GBA_REG16(0x082)
#define GBA_REG_SOUNDCNT_X GBA_REG16(0x084)
#define GBA_FIFO_A        (GBA_IO + 0x0A0)
#define GBA_FIFO_B        (GBA_IO + 0x0A4)
#define GBA_REG_TM0CNT_L  GBA_REG16(0x100)
#define GBA_REG_TM0CNT_H  GBA_REG16(0x102)
#define GBA_REG_TM2CNT_L  GBA_REG16(0x108)
#define GBA_REG_TM2CNT_H  GBA_REG16(0x10A)
#define GBA_REG_TM3CNT_L  GBA_REG16(0x10C)
#define GBA_REG_TM3CNT_H  GBA_REG16(0x10E)
#define GBA_DMA_START_FIFO   (3u << 12)
#define GBA_DMA_32BIT        (1u << 10)

#define GBA_DMA_DEST_FIXED   (2u << 5)
#define GBA_DMA_REPEAT       (1u << 9)
#define GBA_DMA_START_HBLANK (2u << 12)
#define GBA_DMA_ENABLE       (1u << 15)

// Serial port
#define GBA_REG_SIODATA32 GBA_REG32(0x120)
#define GBA_REG_SIOCNT    GBA_REG16(0x128)
#define GBA_REG_RCNT      GBA_REG16(0x134)
#define GBA_SIO_INTERNAL_CLOCK (1u << 0)
#define GBA_SIO_2MHZ      (1u << 1)
#define GBA_SIO_START     (1u << 7)
#define GBA_SIO_32BIT     (1u << 12)
#define GBA_SIO_IRQ       (1u << 14)
#define GBA_IRQ_SERIAL    (1u << 7)
#define GBA_IRQ_HBLANK    (1u << 1)
#define GBA_DISPSTAT_HBLANK_IRQ (1u << 4)
