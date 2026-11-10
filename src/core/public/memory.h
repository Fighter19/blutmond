#pragma once

#include "shared.h"

typedef struct BmBackground *BmBackgroundHandle;
typedef struct BmBuffer *BmBufferHandle;

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
  /// Required alignment in bytes, a power of two. 0 means 4.
  BmDeviceSize alignment;
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

/// Heaps of the GBA profile. Other profiles map their memories onto these indices.
enum {
  BM_MEMORY_HEAP_EWRAM = 0,
  BM_MEMORY_HEAP_IWRAM = 1,
  BM_MEMORY_HEAP_VRAM = 2,
};

#define BM_MAX_MEMORY_HEAPS 3
#define BM_MAX_MEMORY_TYPES 3

typedef struct BmMemoryHeap {
  /// Size of the heap
  BmDeviceSize size;
} BmMemoryHeap;

typedef struct BmPhysicalDeviceMemoryProperties {
  /// Memory heaps (EWRAM, IWRAM, VRAM)
  uint32_t memoryHeapCount;
  BmMemoryHeap memoryHeaps[BM_MAX_MEMORY_HEAPS];
  /// Memory types
  uint32_t memoryTypeCount;
  BmMemoryType memoryTypes[BM_MAX_MEMORY_TYPES];

  /// Equivalent to MemoryBudget extension
  BmDeviceSize heapBudget[BM_MAX_MEMORY_HEAPS];
  BmDeviceSize heapUsage[BM_MAX_MEMORY_HEAPS];
} BmPhysicalDeviceMemoryProperties;

BmResult bmGetPhysicalDeviceMemoryProperties(
  BmDeviceHandle device,
  BmPhysicalDeviceMemoryProperties *pMemoryProperties
);
