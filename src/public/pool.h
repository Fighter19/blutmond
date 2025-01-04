/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

#include "shared.h"
#include "type.h"

#include "memory.h"

typedef struct BmPool
{
  BmDeviceHandle device;
  BmDeviceMemoryHandle memory;
  size_t elementCount;
  size_t elementCapacity;
  
  BmTypeHandle elementType;
} BmPool;

BmResult bmPoolInit(BmPool *pool, BmDeviceHandle hDevice, BmDeviceMemoryHandle hMemory, BmTypeHandle type);
BmResult bmPoolFinalize(BmPool *pool);

BmResult bmPoolAllocate(BmPool *pool, void **ppData);

