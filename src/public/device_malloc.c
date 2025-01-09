#include "shared.h"

#include "device.h"
#include "device_private.h"

#include "errors.h"

#include <stdlib.h>

#include "memory_private.h"

#include "device_malloc.h"

static BmResult bmMallocAllocateMemory(
    BmDeviceHandle device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory)
{
  (void)device;
  BmDeviceMemoryHandle hMemory = malloc(sizeof(BmMemoryAllocateInfo) + pAllocateInfo->allocationSize);
  if (hMemory == NULL)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  hMemory->size = pAllocateInfo->allocationSize;
  *phMemory = hMemory;

  return BM_SUCCESS;
}

static BmResult bmMallocMapMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory,
    BmDeviceSize offset,
    BmDeviceSize size,
    void **ppData)
{
  (void)device;
  (void)size;
  *ppData = (char*)hMemory + sizeof(BmMemoryAllocateInfo) + offset;

  return BM_SUCCESS;
}

static BmResult bmMallocFreeMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory)
{
  (void)device;
  free(hMemory);

  return BM_SUCCESS;
}

static void bmDeviceMallocInit(BmDevice *device)
{
  device->name = "malloc";

  device->pfnAllocateMemory = bmMallocAllocateMemory;
  device->pfnMapMemory = bmMallocMapMemory;
  device->pfnFreeMemory = bmMallocFreeMemory;

  device->pfnDestroyDevice = bmDeviceMallocDestroy;
}

static BmDevice g_deviceMalloc = {0};

BmDeviceHandle bmDeviceMallocCreate()
{
  BmDeviceHandle device = &g_deviceMalloc;
  bmDeviceMallocInit(device);

  return device;
}

BmResult bmDeviceMallocDestroy(BmDeviceHandle device)
{
  if (device == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  return BM_SUCCESS;
}