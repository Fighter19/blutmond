/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// The multiboot sender (src/link/multiboot.h) against a model of the receiving BIOS as the
// protocol is documented (GBATEK, "BIOS Multi Boot"): handshake, header, keys, the encrypted
// image with its running offset, and the final checksum. The model decrypts what it gets, so
// the image must arrive whole.

#include <string.h>

#include "multiboot.h"
#include "test.h"

typedef struct Bios
{
  int phase;                 // 0 attention, 1 header, 2 after header, 3 keys, 4 length, 5 data, 6 end
  uint32_t headerLeft;
  uint8_t image[4096];
  uint32_t length, offset;
  uint32_t seed, key1, key2, confirm;
  uint16_t crc1;
  uint32_t busy;
  bool done, crcFine;
  uint32_t dropAt;           // lose the word with this number (0: none)
  uint32_t words;
} Bios;

static uint16_t crcStep(uint16_t crc, uint32_t data)
{
  uint32_t eax = (data & 0xFFFF0000u) | ((data & 0xFFFFu) ^ crc);
  for (int i = 0; i < 32; i++)
  {
    bool bit = eax & 1;
    eax >>= 1;
    if (bit) eax = (eax & 0xFFFF0000u) | ((eax & 0xFFFFu) ^ 0xC37B);
  }
  return (uint16_t)eax;
}

static uint32_t bios(void *pContext, uint32_t in)
{
  Bios *b = pContext;
  b->words++;
  if (b->dropAt != 0 && b->words == b->dropAt) return 0xFFFFFFFFu;
  switch (b->phase)
  {
  case 0:
    if (in == 0x6202) { b->phase = 1; return 0x72020000u; }
    return 0;
  case 1:
    if (in == 0x6102) { b->headerLeft = 0x60; return 0x72020000u; }
    if (b->headerLeft != 0)
    {
      uint32_t i = 0x60 - b->headerLeft;
      b->image[i * 2] = (uint8_t)in;
      b->image[i * 2 + 1] = (uint8_t)(in >> 8);
      uint32_t reply = (b->headerLeft << 24) | 0x00020000u;
      if (--b->headerLeft == 0) b->phase = 2;
      return reply;
    }
    return 0;
  case 2:
    if (in == 0x6200) return 0x00020000u;
    if (in == 0x6202) { b->phase = 3; return 0x72020000u; }
    return 0;
  case 3:
    if ((in & 0xFF00) == 0x6300)
    {
      b->key1 = 0xA7;
      b->seed = 0xFFFF0000u | (b->key1 << 8) | (in & 0xFF);
      return 0x73000000u | (b->key1 << 16);
    }
    if ((in & 0xFF00) == 0x6400)
    {
      b->confirm = in & 0xFF;
      b->phase = 4;
      return 0x73000000u | (b->key1 << 16);
    }
    return 0;
  case 4:
    b->length = (in + 0x64) << 2;
    b->key2 = 0x5C;
    b->offset = 0xC0;
    b->crc1 = 0xC387;
    b->phase = 5;
    return 0x73000000u | (b->key2 << 16);
  case 5:
  {
    uint32_t reply = (b->offset & 0xFFFF) << 16;
    b->seed = 0x6F646573u * b->seed + 1;
    uint32_t data = in ^ b->seed ^ ((0xFE000000u - b->offset) ^ 0x43202F2Fu);
    b->crc1 = crcStep(b->crc1, data);
    if (b->offset + 4 <= sizeof(b->image)) memcpy(b->image + b->offset, &data, 4);   // little endian hosts
    b->offset += 4;
    if (b->offset >= b->length) { b->phase = 6; b->busy = 3; }
    return reply;
  }
  default:
    if (in == 0x65) return (b->busy != 0 && b->busy-- != 0) ? 0x00740000u : 0x00750000u;
    if (in == 0x66) return 0x00750000u;
    {
      uint16_t crc = crcStep(b->crc1, 0xFFFF0000u | (b->key2 << 8) | b->confirm);
      b->done = true;
      b->crcFine = (in & 0xFFFF) == crc;
      return (uint32_t)crc << 16;
    }
  }
}

static void testAnImageArrivesWhole(void)
{
  static uint8_t image[1000];
  for (uint32_t i = 0; i < sizeof(image); i++) image[i] = (uint8_t)(i * 7 + (i >> 5));
  Bios b;
  memset(&b, 0, sizeof(b));
  BmMultibootPort port = { &b, bios, NULL, 0 };
  const char *why = NULL;
  TEST_ASSERT(bmMultibootSend(&port, image, sizeof(image), &why));
  TEST_ASSERT(b.done && b.crcFine);
  TEST_ASSERT(b.length == 1008);                         // padded to a multiple of 16
  TEST_ASSERT(memcmp(b.image, image, sizeof(image)) == 0);
  TEST_ASSERT(b.image[1000] == 0 && b.image[1007] == 0);
  // A very small image is padded to the least the BIOS takes.
  memset(&b, 0, sizeof(b));
  TEST_ASSERT(bmMultibootSend(&port, image, 0x100, &why) && b.length == 0x1C0 && b.crcFine);
}

static void testFailuresAreReported(void)
{
  static uint8_t image[1000];
  Bios b;
  BmMultibootPort port = { &b, bios, NULL, 0 };
  const char *why = NULL;
  // Nobody there.
  memset(&b, 0, sizeof(b));
  b.phase = 99;
  b.busy = 0;
  TEST_ASSERT(!bmMultibootSend(&port, image, sizeof(image), &why) && strstr(why, "no response") != NULL);
  // A word lost in the header, and one in the data.
  memset(&b, 0, sizeof(b));
  b.dropAt = 20;
  TEST_ASSERT(!bmMultibootSend(&port, image, sizeof(image), &why) && strstr(why, "header") != NULL);
  memset(&b, 0, sizeof(b));
  b.dropAt = 200;
  TEST_ASSERT(!bmMultibootSend(&port, image, sizeof(image), &why) && strstr(why, "transfer failed") != NULL);
  static uint8_t big[BM_MULTIBOOT_MAX_SIZE + 4];
  TEST_ASSERT(!bmMultibootSend(&port, big, sizeof(big), &why));
}

int main(void)
{
  RUN_TEST(testAnImageArrivesWhole);
  RUN_TEST(testFailuresAreReported);
  return TEST_RESULT();
}
