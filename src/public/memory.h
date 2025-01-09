#pragma once

#include "shared.h"

// Forward declaration of BmDevice
typedef struct BmDevice *BmDeviceHandle;
typedef uint32_t BmDeviceSize;
typedef struct BmDeviceMemoryPrivate *BmDeviceMemoryHandle;

typedef struct BmBackground *BmBackgroundHandle;
typedef struct BmBuffer *BmBufferHandle;

typedef enum
{
  BM_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO = 0,
} BmStructureType;

typedef struct BmMemoryDedicatedAllocateInfo
{
  BmStructureType sType;

  BmBufferHandle buffer;
} BmMemoryDedicatedAllocateInfo;

typedef struct BmMemoryAllocateInfo
{
  const void *pNext;
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

#define BM_MAX_MEMORY_HEAPS 3
#define BM_MAX_MEMORY_TYPES 2

typedef struct BmPhysicalDeviceMemoryProperties {
  /// Memory heaps (EWRAM, IWRAM, VRAM)
  BmDeviceSize heapCount;
  BmDeviceSize heaps[BM_MAX_MEMORY_HEAPS];
  /// Memory types
  uint32_t memoryTypeCount;
  BmMemoryType memoryTypes[BM_MAX_MEMORY_TYPES];

  BmDeviceSize heapBudget[BM_MAX_MEMORY_HEAPS];
  BmDeviceSize heapUsage[BM_MAX_MEMORY_HEAPS];
} BmPhysicalDeviceMemoryProperties;

BmResult bmGetPhysicalDeviceMemoryProperties(
  BmPhysicalDeviceHandle device,
  BmPhysicalDeviceMemoryProperties *pMemoryProperties
);