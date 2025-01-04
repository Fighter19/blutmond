#include "pool.h"
#include "errors.h"

#include "type_private.h"
#include "memory_private.h"

void bmPoolInit(BmPool *pool, BmDeviceHandle hDevice, BmDeviceMemoryHandle hMemory, BmTypeHandle type)
{
  pool->device = hDevice;
  pool->memory = hMemory;
  pool->elementCount = 0;
  pool->elementCapacity = hMemory->size / type->size;

  pool->elementType = type;
}

BmResult bmPoolAllocate(BmPool *pool, void **ppData)
{
  if (pool->elementCount >= pool->elementCapacity)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  BmResult resMap = bmMapMemory(pool->device, pool->memory, pool->elementCount * pool->elementType->size, pool->elementType->size, ppData);
  if (resMap != BM_SUCCESS)
  {
    return resMap;
  }

  pool->elementCount++;

  bmTypeInitializeMemory(pool->elementType, *ppData);

  return BM_SUCCESS;
}
