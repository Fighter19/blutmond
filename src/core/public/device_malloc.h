#pragma once

#include "shared.h"

/// Device backed by malloc, for host tools. It ignores memoryTypeIndex.
BmDeviceHandle bmDeviceMallocCreate(void);
BmResult bmDeviceMallocDestroy(BmDeviceHandle device);
