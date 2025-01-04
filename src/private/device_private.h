#pragma once

#include "shared.h"
#include "device.h"

typedef struct BmDevice
{
  const char *name;

  PFN_bmAllocateMemory pfnAllocateMemory;
  PFN_bmMapMemory pfnMapMemory;
  PFN_bmFreeMemory pfnFreeMemory;
} BmDevice;