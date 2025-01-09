#pragma once

#include "shared.h"
#include "device.h"

typedef struct BmDevice
{
  const char *name;

  PFN_bmAllocateMemory pfnAllocateMemory;
  PFN_bmMapMemory pfnMapMemory;
  PFN_bmFreeMemory pfnFreeMemory;

  PFN_bmDestroyDevice pfnDestroyDevice;
} BmDevice;

typedef struct BmPhysicalDevice
{
  const char *name;

  PFN_bmCreateDevice pfnCreateDevice;
} BmPhysicalDevice;