#include "pool.h"
#include "errors.h"

#include "type_private.h"
#include "memory_private.h"

#include "type_manager.h"

BmResult bmPoolInit(BmPool *pool, BmDeviceHandle hDevice, BmDeviceMemoryHandle hMemory, BmTypeHandle type)
{
  if (pool == NULL || hDevice == NULL || hMemory == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmTypePrivate *type_priv = bmTypeManagerGetTypeFromHandle(g_typeManager, type);
  if (type_priv == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  pool->device = hDevice;
  pool->memory = hMemory;
  pool->elementCount = 0;
  pool->elementCapacity = hMemory->size / type_priv->size;

  pool->elementType = type;
  return BM_SUCCESS;
}

BmResult bmPoolFinalize(BmPool *pool)
{
  BmTypePrivate *type_priv = bmTypeManagerGetTypeFromHandle(g_typeManager, pool->elementType);
  if (pool == NULL || type_priv == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  if (type_priv->free == NULL)
  {
    return BM_SUCCESS;
  }

  for (size_t i = 0; i < pool->elementCount; i++)
  {
    void *pData = (char*)pool->memory + i * type_priv->size;
    if (type_priv->free != NULL)
      type_priv->free(pData);
  }

  return BM_SUCCESS;
}

BmResult bmPoolAllocate(BmPool *pool, void **ppData)
{
  if (pool->elementCount >= pool->elementCapacity)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  BmTypePrivate *type_priv = bmTypeManagerGetTypeFromHandle(g_typeManager, pool->elementType);

  BmResult resMap = bmMapMemory(pool->device, pool->memory, pool->elementCount * type_priv->size, type_priv->size, ppData);
  if (resMap != BM_SUCCESS)
  {
    return resMap;
  }

  pool->elementCount++;

  bmTypeInitializeForType(pool->elementType, *ppData);

  return BM_SUCCESS;
}
