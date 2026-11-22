/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "native.h"

#include <stddef.h>
#include <string.h>

// The slot is ordinary zero-initialised data: on the GBA that is IWRAM, the fast memory.
#if BM_NATIVE_TARGET != BM_NATIVE_TARGET_NONE
#define SLOT_SIZE 8192
#else
#define SLOT_SIZE 16   // nothing can be loaded on this machine
#endif
// Not static: a debugger loads the module's symbols at this address.
uint32_t bm_native_slot[SLOT_SIZE / 4];

void *bmVmNativeSlot(uint32_t *pSize)
{
  if (pSize != NULL) *pSize = sizeof(bm_native_slot);
  return bm_native_slot;
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool relocated(const uint8_t *table, uint32_t relocs, uint32_t at)
{
  for (uint32_t k = 0; k < relocs; k++)
    if (rd32(table + k * 4) == at) return true;
  return false;
}

bool bmVmNativeRelocate(const uint8_t *p, uint32_t size, void *pSlot, uint32_t slotSize, uint32_t target, uint32_t base, const char **ppError)
{
  const char *dummy;
  if (ppError == NULL) ppError = &dummy;
#define FAIL(msg) do { *ppError = msg; return false; } while (0)
  if (p == NULL || size < BM_NATIVE_CHUNK_HEADER || rd32(p) != BM_NATIVE_MAGIC) FAIL("not a native module");
  uint32_t version = p[4] | ((uint32_t)p[5] << 8), chunkTarget = p[6] | ((uint32_t)p[7] << 8);
  uint32_t imageSize = rd32(p + 8), relocs = rd32(p + 12);
  if (version != BM_NATIVE_VERSION) FAIL("native module of another version");
  if (chunkTarget != target || target == BM_NATIVE_TARGET_NONE) FAIL("native module for another machine");
  // All targets so far have 32-bit addresses: a header is 12 bytes, a table entry 4.
  const uint32_t headerSize = 12;
  if ((imageSize & 3) != 0 || imageSize < headerSize) FAIL("bad native module");
  if (imageSize > size - BM_NATIVE_CHUNK_HEADER || relocs > (size - BM_NATIVE_CHUNK_HEADER - imageSize) / 4) FAIL("native module cut off");
  if (imageSize > slotSize || ((uintptr_t)pSlot & 3) != 0) FAIL("native module too big for its slot");

  // Everything is checked in the pack, before anything is copied over the image in use.
  const uint8_t *image = p + BM_NATIVE_CHUNK_HEADER, *table = image + imageSize;
  if (rd32(image) != BM_NATIVE_MAGIC || (image[4] | ((uint32_t)image[5] << 8)) != BM_NATIVE_VERSION) FAIL("bad native module");
  for (uint32_t i = 0; i < relocs; i++)
  {
    uint32_t at = rd32(table + i * 4);
    if ((at & 3) != 0 || at > imageSize - 4) FAIL("relocation outside the native module");
    if (rd32(image + at) >= imageSize) FAIL("relocation to outside the native module");
  }
  // The image's list of modules, each a relocated address of a module header with its entries.
  uint32_t modules = image[6] | ((uint32_t)image[7] << 8);
  if (modules > (imageSize - headerSize) / 4) FAIL("bad native module");
  uint32_t listEnd = headerSize + modules * 4;
  for (uint32_t m = 0; m < modules; m++)
  {
    uint32_t at = headerSize + m * 4, mod = rd32(image + at);
    if (!relocated(table, relocs, at) || (mod & 3) != 0 || mod < listEnd || mod > imageSize - headerSize) FAIL("bad module in the native image");
    const uint8_t *h = image + mod;
    if (rd32(h) != BM_NATIVE_MAGIC || (h[4] | ((uint32_t)h[5] << 8)) != BM_NATIVE_VERSION) FAIL("bad module in the native image");
    uint32_t functions = h[6] | ((uint32_t)h[7] << 8);
    if (functions > (imageSize - mod - headerSize) / 4) FAIL("bad module in the native image");
    // An entry is nothing, or a relocated address of code (its low bit marks Thumb code).
    for (uint32_t i = 0; i < functions; i++)
    {
      uint32_t e = mod + headerSize + i * 4;
      if (rd32(image + e) == 0) continue;
      if (!relocated(table, relocs, e) || (rd32(image + e) & ~1u) < listEnd) FAIL("entry outside the native module");
    }
  }

  memcpy(pSlot, image, imageSize);
  uint32_t *words = (uint32_t *)pSlot;
  for (uint32_t i = 0; i < relocs; i++) words[rd32(table + i * 4) / 4] += base;
#undef FAIL
  return true;
}

const BmVmNativeImage *bmVmNativeLoad(const uint8_t *p, uint32_t size, void *pSlot, uint32_t slotSize, const char **ppError)
{
  if (!bmVmNativeRelocate(p, size, pSlot, slotSize, BM_NATIVE_TARGET, (uint32_t)(uintptr_t)pSlot, ppError)) return NULL;
  return (const BmVmNativeImage *)pSlot;
}

const BmVmNativeModule *bmVmNativeFind(const BmVmNativeImage *pImage, uint32_t bytecodeCrc)
{
  if (pImage == NULL) return NULL;
  for (uint32_t m = 0; m < pImage->head.functionCount; m++)
    if (pImage->modules[m] != NULL && pImage->modules[m]->head.bytecodeCrc == bytecodeCrc) return pImage->modules[m];
  return NULL;
}
