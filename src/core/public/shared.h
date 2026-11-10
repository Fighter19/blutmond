#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "errors.h"

// Placement of data and code in the GBA's memories. On other platforms they do nothing.
//   BM_EWRAM_BSS   zero-initialised data in EWRAM (large tables that are not used every cycle)
//   BM_IWRAM_DATA  initialised data in IWRAM (small, hot data)
//   BM_IWRAM_CODE  a function compiled as ARM code and run from IWRAM (hot loops)
#if defined(BM_PLATFORM_GBA)
#  define BM_EWRAM_BSS  __attribute__((section(".sbss")))
#  define BM_IWRAM_DATA __attribute__((section(".iwram.data")))
#  define BM_IWRAM_CODE __attribute__((section(".iwram.text"), target("arm"), noinline))
#else
#  define BM_EWRAM_BSS
#  define BM_IWRAM_DATA
#  define BM_IWRAM_CODE
#endif

typedef int BmResult;
typedef uint32_t BmFlags;
typedef uint32_t BmDeviceSize;

typedef struct BmPhysicalDevice *BmPhysicalDeviceHandle;
typedef struct BmDevice *BmDeviceHandle;
typedef struct BmDeviceMemoryPrivate *BmDeviceMemoryHandle;

typedef enum
{
  BM_STRUCTURE_TYPE_MEMORY_DOMAIN = 0,
  BM_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = BM_STRUCTURE_TYPE_MEMORY_DOMAIN,
  BM_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,

  BM_STRUCTURE_TYPE_TILEMAP_DOMAIN = 1000,
  BM_STRUCTURE_TYPE_TILEMAP_CREATE_INFO,
} BmStructureType;

typedef struct BmExtent3D
{
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} BmExtent3D;

extern BmDeviceHandle g_defaultDevice;
extern BmDeviceMemoryHandle g_defaultMemory;
