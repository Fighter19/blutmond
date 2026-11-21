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
static uint32_t s_slot[SLOT_SIZE / 4];

void *bmVmNativeSlot(uint32_t *pSize)
{
  if (pSize != NULL) *pSize = sizeof(s_slot);
  return s_slot;
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool bmVmNativeRelocate(const uint8_t *p, uint32_t size, void *pSlot, uint32_t slotSize, uint32_t bytecodeCrc, uint32_t target, uint32_t base, const char **ppError)
{
  const char *dummy;
  if (ppError == NULL) ppError = &dummy;
#define FAIL(msg) do { *ppError = msg; return false; } while (0)
  if (p == NULL || size < BM_NATIVE_CHUNK_HEADER || rd32(p) != BM_NATIVE_MAGIC) FAIL("not a native module");
  uint32_t version = p[4] | ((uint32_t)p[5] << 8), chunkTarget = p[6] | ((uint32_t)p[7] << 8);
  uint32_t imageSize = rd32(p + 8), relocs = rd32(p + 12);
  if (version != BM_NATIVE_VERSION) FAIL("native module of another version");
  if (chunkTarget != target || target == BM_NATIVE_TARGET_NONE) FAIL("native module for another machine");
  // All targets so far have 32-bit addresses: the entries follow the 12 bytes of the header.
  const uint32_t entriesAt = 12;
  if ((imageSize & 3) != 0 || imageSize < entriesAt) FAIL("bad native module");
  if (imageSize > size - BM_NATIVE_CHUNK_HEADER || relocs > (size - BM_NATIVE_CHUNK_HEADER - imageSize) / 4) FAIL("native module cut off");
  if (imageSize > slotSize || ((uintptr_t)pSlot & 3) != 0) FAIL("native module too big for its slot");

  // Everything is checked in the pack, before anything is copied over the module in use.
  const uint8_t *image = p + BM_NATIVE_CHUNK_HEADER, *table = image + imageSize;
  if (rd32(image) != BM_NATIVE_MAGIC || (image[4] | ((uint32_t)image[5] << 8)) != BM_NATIVE_VERSION) FAIL("bad native module");
  if (rd32(image + 8) != bytecodeCrc) FAIL("native module made from other scripts");
  uint32_t functions = image[6] | ((uint32_t)image[7] << 8);
  if (functions > (imageSize - entriesAt) / 4) FAIL("bad native module");
  for (uint32_t i = 0; i < relocs; i++)
  {
    uint32_t at = rd32(table + i * 4);
    if ((at & 3) != 0 || at > imageSize - 4) FAIL("relocation outside the native module");
    if (rd32(image + at) >= imageSize) FAIL("relocation to outside the native module");
  }
  // An entry is nothing, or a relocated address behind the table (its low bit marks Thumb code).
  for (uint32_t i = 0; i < functions; i++)
  {
    uint32_t at = entriesAt + i * 4, e = rd32(image + at) & ~1u;
    if (rd32(image + at) == 0) continue;
    bool relocated = false;
    for (uint32_t k = 0; k < relocs && !relocated; k++) relocated = rd32(table + k * 4) == at;
    if (!relocated || e < entriesAt + functions * 4) FAIL("entry outside the native module");
  }

  memcpy(pSlot, image, imageSize);
  uint32_t *words = (uint32_t *)pSlot;
  for (uint32_t i = 0; i < relocs; i++) words[rd32(table + i * 4) / 4] += base;
#undef FAIL
  return true;
}

const BmVmNativeModule *bmVmNativeLoad(const uint8_t *p, uint32_t size, void *pSlot, uint32_t slotSize, uint32_t bytecodeCrc, const char **ppError)
{
  if (!bmVmNativeRelocate(p, size, pSlot, slotSize, bytecodeCrc, BM_NATIVE_TARGET, (uint32_t)(uintptr_t)pSlot, ppError)) return NULL;
  return (const BmVmNativeModule *)pSlot;
}
