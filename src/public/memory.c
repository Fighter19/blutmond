#include "shared.h"
#include "memory.h"

#include "device_private.h"

BmResult bmAllocateMemory(
    BmDeviceHandle device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory)
{
  return device->pfnAllocateMemory(device, pAllocateInfo, phMemory);
}

BmResult bmMapMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory,
    BmDeviceSize offset,
    BmDeviceSize size,
    void **ppData)
{
  return device->pfnMapMemory(device, hMemory, offset, size, ppData);
}

BmResult bmFreeMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory)
{
  return device->pfnFreeMemory(device, hMemory);
}