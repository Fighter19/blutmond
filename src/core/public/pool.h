/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

#include "shared.h"
#include "type.h"

#include "memory.h"

// A pool (slab) of fixed-size elements of one type inside one memory allocation.
// Elements are addressed by index; free elements form a list through their first bytes.
// A bitmap after the elements records which ones are in use.

typedef unsigned int BmPoolElementHandle;
#define BM_POOL_ELEMENT_INVALID (unsigned int)(-1)

typedef struct BmPool
{
  BmDeviceHandle device;
  BmDeviceMemoryHandle memory;
  size_t elementSize;
  size_t elementCount;
  size_t elementCapacity;
  /// Elements handed out at least once; indices below this are either used or on the free list.
  size_t elementHighWater;

  BmTypeHandle elementType;

  BmPoolElementHandle pFreeList;
  void *pBuffer;
  uint8_t *pUsedBits;
} BmPool;

/// Bytes a pool needs for `capacity` elements of `elementSize` bytes (elements plus bitmap).
size_t bmPoolRequiredSize(size_t elementSize, size_t capacity);

/// Initialises a pool in the first `size` bytes of `hMemory`.
BmResult bmPoolInit(BmPool *pool, BmDeviceHandle hDevice, BmDeviceMemoryHandle hMemory, BmDeviceSize size, BmTypeHandle type);
/// Calls the type's free function for every element still in use.
BmResult bmPoolFinalize(BmPool *pool);

BmResult bmPoolAllocate(BmPool *pool, void **ppData);
BmResult bmPoolFree(BmPool *pool, void *pData);

/// Index of an element, or BM_POOL_ELEMENT_INVALID if the pointer is not a used element.
BmPoolElementHandle bmPoolIndexOf(const BmPool *pool, const void *pData);
