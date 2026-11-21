/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "multiboot.h"

#include <stddef.h>

typedef struct Boot
{
  const BmMultibootPort *pPort;
  const uint8_t *pImage;
  uint32_t size;
  uint16_t crc1, crc2;
} Boot;

static void wait(Boot *b, uint32_t units)
{
  if (b->pPort->delay != NULL && units != 0) b->pPort->delay(b->pPort->pContext, units);
}

static uint32_t exchange(Boot *b, uint32_t out)
{
  uint32_t in = b->pPort->exchange(b->pPort->pContext, out);
  wait(b, b->pPort->wordDelay);
  return in;
}

/// The image is padded with zeros to the length sent.
static uint32_t byteAt(const Boot *b, uint32_t i)
{
  return i < b->size ? b->pImage[i] : 0;
}

static uint32_t handshake(Boot *b, uint32_t send, uint8_t receive)
{
  uint32_t reply;
  int i = 0;
  do
  {
    wait(b, 100);
    reply = b->pPort->exchange(b->pPort->pContext, send);
    if (i++ > 1000) break;
  } while ((reply >> 24) != receive);
  return reply;
}

static uint16_t crcStep(Boot *b, uint32_t data)
{
  uint32_t eax = (data & 0xFFFF0000u) | ((data & 0xFFFFu) ^ b->crc1);
  for (int i = 0; i < 32; i++)
  {
    bool bit = eax & 1;
    eax >>= 1;
    if (bit) eax = (eax & 0xFFFF0000u) | ((eax & 0xFFFFu) ^ b->crc2);
  }
  b->crc1 = (uint16_t)eax;
  return b->crc1;
}

bool bmMultibootSend(const BmMultibootPort *pPort, const uint8_t *pImage, uint32_t size, const char **ppError)
{
  const char *dummy;
  if (ppError == NULL) ppError = &dummy;
  Boot boot = { pPort, pImage, size, 0, 0 }, *b = &boot;
#define FAIL(msg) do { *ppError = msg; return false; } while (0)
  if (size > BM_MULTIBOOT_MAX_SIZE) FAIL("multiboot images are at most 256 KB");
  // Length a multiple of 16 and at least 0x1C0.
  uint32_t length = (size + 15) & ~15u;
  if (length < 0x1C0) length = 0x1C0;

  uint32_t reply = handshake(b, 0x6202, 0x72);
  if ((reply >> 24) != 0x72) FAIL("no response from the GBA");
  exchange(b, 0x6102);  // header
  uint32_t i;
  for (i = 0x60; i > 0; i--)
  {
    uint32_t data = byteAt(b, (0x60 - i) * 2) | (byteAt(b, (0x60 - i) * 2 + 1) << 8);
    reply = exchange(b, data);
    if ((reply >> 24) != i) break;
  }
  if (i != 0) FAIL("header not accepted");
  exchange(b, 0x6200);
  exchange(b, 0x6202);

  const uint16_t keyFromMaster = 0xD1;
  uint32_t keyConfirm = 0x6300 | keyFromMaster;
  reply = handshake(b, keyConfirm, 0x73);
  uint32_t key1 = (reply >> 16) & 0xFF;
  keyConfirm = ((key1 + 0x0F) & 0xFF) | 0x6400;
  reply = handshake(b, keyConfirm, 0x73);
  uint32_t seed = 0xFFFF0000u | (key1 << 8) | keyFromMaster;
  wait(b, 0x110E);
  reply = exchange(b, (length >> 2) - 0x64);
  uint32_t key2 = (reply >> 16) & 0xFF;

  b->crc1 = 0xC387;
  b->crc2 = 0xC37B;
  uint32_t offset = 0xC0;
  uint32_t words = (length - 0xC0) >> 2;
  for (i = 0; i < words; i++)
  {
    uint32_t at = 0xC0 + i * 4;
    uint32_t data = byteAt(b, at) | (byteAt(b, at + 1) << 8) | (byteAt(b, at + 2) << 16) | (byteAt(b, at + 3) << 24);
    crcStep(b, data);
    seed = 0x6F646573u * seed + 1;
    data ^= seed;
    data ^= (0xFE000000u - offset) ^ 0x43202F2Fu;
    reply = exchange(b, data);
    if ((reply >> 16) != (offset & 0xFFFF)) break;
    offset += 4;
  }
  if (i != words) FAIL("transfer failed");
  exchange(b, 0x65);
  i = 0;
  do
  {
    reply = exchange(b, 0x65);
    wait(b, 200);
    if (i++ > 1000) break;
  } while ((reply >> 16) == 0x74);
  if ((reply >> 16) != 0x75) FAIL("no final confirmation (bad header?)");
  exchange(b, 0x66);
  uint32_t data = 0xFFFF0000u | ((key2 & 0xFF) << 8) | (keyConfirm & 0xFF);
  uint16_t crc = crcStep(b, data);
  reply = exchange(b, crc);
  if ((reply >> 16) != crc) FAIL("final CRC check failed");
#undef FAIL
  return true;
}
