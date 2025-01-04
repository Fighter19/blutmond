#pragma once

#include "shared.h"

// Forward declaration of BmDevice
typedef struct BmDevice *BmDeviceHandle;
typedef uint32_t BmDeviceSize;
typedef struct BmDeviceMemoryPrivate *BmDeviceMemoryHandle;

typedef struct BmMemoryAllocateInfo
{
  BmDeviceSize allocationSize;
  uint32_t memoryTypeIndex;
} BmMemoryAllocateInfo;

BmResult bmAllocateMemory(
  BmDeviceHandle device,
  const BmMemoryAllocateInfo *pAllocateInfo,
  BmDeviceMemoryHandle *phMemory
);

BmResult bmMapMemory(
  BmDeviceHandle device,
  BmDeviceMemoryHandle hMemory,
  BmDeviceSize offset,
  BmDeviceSize size,
  void **ppData
);

BmResult bmFreeMemory(
  BmDeviceHandle device,
  BmDeviceMemoryHandle hMemory
);

// Used to describe the properties of memory
typedef BmFlags BmMemoryPropertyFlags;

typedef struct BmMemoryType {
  /// Properties this memory type must have
  BmMemoryPropertyFlags flags;
  /// The index of the heap within a device
  uint32_t heapIndex;
} BmMemoryType;