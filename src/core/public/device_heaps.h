#pragma once

#include "shared.h"

/// Maximum number of blocks (allocations plus free ranges) over all heaps.
#define BM_MAX_MEMORY_BLOCKS 256

/// Creates the device that allocates from the platform's memories (EWRAM, IWRAM, VRAM), as
/// reported by the HAL. There is one such device; calling this again resets it.
BmDeviceHandle bmDeviceHeapsCreate(void);
