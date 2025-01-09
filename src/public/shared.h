#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BmResult;
typedef uint32_t BmFlags;

typedef struct BmPhysicalDevice *BmPhysicalDeviceHandle;
typedef struct BmDevice *BmDeviceHandle;
typedef struct BmDeviceMemoryPrivate *BmDeviceMemoryHandle;

extern BmDeviceHandle g_defaultDevice;
extern BmDeviceMemoryHandle g_defaultMemory;