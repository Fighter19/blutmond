#pragma once

#include "shared.h"
#include "device.h"

/// The physical device of the current platform. Its devices allocate from the memories the
/// HAL reports.
BmPhysicalDeviceHandle bmGetPhysicalDevice(void);
