/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The bootstrap (docs/spec/link-protocol.md §9): a multiboot image of about 1 KB that a GBA
// without a cartridge gets through the slow BIOS protocol. It then takes the real image (the
// runtime with its content) at the serial port's full speed and starts it.
//
// It runs from IWRAM, because the image it receives goes where the BIOS put the bootstrap
// itself (EWRAM, 0x02000000). So everything here is in the .iwram sections, nothing is
// called outside this file, and the start code that brought us here is not used again.

#include <stdint.h>

#include "../hal_gba_regs.h"
#include "bootstrap_protocol.h"

#define IWRAM_CODE __attribute__((section(".iwram.text"), target("arm"), noinline))
#define IWRAM_DATA __attribute__((section(".iwram.data")))

#define IMAGE ((volatile uint32_t *)0x02000000)

// CRC-32 (IEEE), four bits at a time: the same as bmCrc32.
static uint32_t s_crcTable[16] IWRAM_DATA = {
  0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
  0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

IWRAM_CODE static uint32_t exchange(uint32_t out)
{
  GBA_REG_SIODATA32 = out;
  GBA_REG_SIOCNT = GBA_SIO_32BIT | GBA_SIO_START;   // the other side makes the clock
  while (GBA_REG_SIOCNT & GBA_SIO_START) {}
  return GBA_REG_SIODATA32;
}

IWRAM_CODE static uint32_t crcWord(uint32_t crc, uint32_t word)
{
  crc ^= word;
  for (int i = 0; i < 8; i++) crc = s_crcTable[crc & 15] ^ (crc >> 4);
  return crc;
}

IWRAM_CODE int main(void)
{
  GBA_REG16(0x208) = 0;       // no interrupts: the port is polled
  GBA_REG_RCNT = 0;
  GBA_REG16(0x000) = 0x0080;  // screen blank (white) while waiting
  uint32_t reply = BM_BOOTSTRAP_IDLE;
  for (;;)
  {
    // Wait for the start word, then take the size.
    if (exchange(reply) != BM_BOOTSTRAP_START)
    {
      reply = BM_BOOTSTRAP_IDLE;
      continue;
    }
    uint32_t size = exchange(BM_BOOTSTRAP_READY);
    if ((size & 3) != 0 || size == 0 || size > BM_BOOTSTRAP_MAX_SIZE)
    {
      reply = BM_BOOTSTRAP_BAD;
      continue;
    }
    // The image: each answer is the number of words taken so far (the size first), so the
    // sender sees a lost word at once.
    uint32_t words = size / 4, crc = 0xFFFFFFFFu;
    reply = size;
    for (uint32_t i = 0; i < words; i++)
    {
      uint32_t word = exchange(reply);
      IMAGE[i] = word;
      crc = crcWord(crc, word);
      reply = i + 1;
    }
    if (exchange(reply) != ~crc)
    {
      reply = BM_BOOTSTRAP_BAD;
      continue;
    }
    // Say so (the sender reads it with one more word), then start the image.
    exchange(BM_BOOTSTRAP_OK);
    GBA_REG_SIOCNT = GBA_SIO_32BIT;
    ((void (*)(void))0x020000C0)();
  }
}
