/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

#include "shared.h"
#include "type.h"

#include "memory.h"

#define BM_POOL_USE_INT_HANDLE

#ifdef BM_POOL_USE_INT_HANDLE
typedef unsigned int BmPoolElementHandle;
#define BM_POOL_ELEMENT_INVALID (unsigned int)(-1)
#else
typedef void* BmPoolElementHandle;
#define BM_POOL_ELEMENT_INVALID NULL
#endif

typedef struct BmPool
{
  BmDeviceHandle device;
  BmDeviceMemoryHandle memory;
  size_t elementCount;
  size_t elementCapacity;
  
  BmTypeHandle elementType;

  BmPoolElementHandle pFreeList;
  void *pBuffer;
} BmPool;

BmResult bmPoolInit(BmPool *pool, BmDeviceHandle hDevice, BmDeviceMemoryHandle hMemory, BmDeviceSize size, BmTypeHandle type);
BmResult bmPoolFinalize(BmPool *pool);

BmResult bmPoolAllocate(BmPool *pool, void **ppData);
BmResult bmPoolFree(BmPool *pool, void *pData);

