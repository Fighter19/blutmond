#pragma once

#include "memory.h"

// One allocation (or one free range) of a device. Both devices (heaps and malloc) use this
// layout so a BmDeviceMemoryHandle means the same everywhere.
typedef struct BmDeviceMemoryPrivate
{
  void *pData;
  BmDeviceSize size;
  uint8_t heapIndex;
  uint8_t inUse;
  // Next block of the same heap in address order (heap device only).
  struct BmDeviceMemoryPrivate *pNext;
} BmDeviceMemoryPrivate;
