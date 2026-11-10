#include "shared.h"

#include "device.h"
#include "device_private.h"
#include "device_malloc.h"
#include "memory_private.h"

#include <stdlib.h>

static BmResult mallocAllocateMemory(
    BmDeviceHandle device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory)
{
  (void)device;
  if (pAllocateInfo == NULL || phMemory == NULL || pAllocateInfo->allocationSize == 0)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmDeviceMemoryHandle hMemory = malloc(sizeof(BmDeviceMemoryPrivate));
  void *pData = malloc(pAllocateInfo->allocationSize);
  if (hMemory == NULL || pData == NULL)
  {
    free(hMemory);
    free(pData);
    return BM_ERROR_OUT_OF_MEMORY;
  }

  *hMemory = (BmDeviceMemoryPrivate){
    .pData = pData,
    .size = pAllocateInfo->allocationSize,
    .heapIndex = (uint8_t)pAllocateInfo->memoryTypeIndex,
    .inUse = 1,
  };
  *phMemory = hMemory;
  return BM_SUCCESS;
}

static BmResult mallocMapMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory,
    BmDeviceSize offset,
    BmDeviceSize size,
    void **ppData)
{
  (void)device;
  if (hMemory == NULL || ppData == NULL || offset > hMemory->size || size > hMemory->size - offset)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  *ppData = (uint8_t *)hMemory->pData + offset;
  return BM_SUCCESS;
}

static BmResult mallocFreeMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory)
{
  (void)device;
  if (hMemory == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  free(hMemory->pData);
  free(hMemory);
  return BM_SUCCESS;
}

static BmResult mallocGetMemoryProperties(
    BmDeviceHandle device,
    BmPhysicalDeviceMemoryProperties *pProperties)
{
  (void)device;
  *pProperties = (BmPhysicalDeviceMemoryProperties){0};
  return BM_SUCCESS;
}

static BmDevice s_deviceMalloc = {
  .name = "malloc",
  .pfnAllocateMemory = mallocAllocateMemory,
  .pfnMapMemory = mallocMapMemory,
  .pfnFreeMemory = mallocFreeMemory,
  .pfnGetMemoryProperties = mallocGetMemoryProperties,
  .pfnDestroyDevice = bmDeviceMallocDestroy,
};

BmDeviceHandle bmDeviceMallocCreate(void)
{
  return &s_deviceMalloc;
}

BmResult bmDeviceMallocDestroy(BmDeviceHandle device)
{
  return device == NULL ? BM_ERROR_INVALID_ARGUMENT : BM_SUCCESS;
}
