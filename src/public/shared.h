#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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