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

  if (type_priv->size < sizeof(BmPoolElementHandle))
  {
    // In order to keep track of free elements, we need to store a pointer to the next free element
    // in the deallocated element.
    // To do this, we ensure, that we have enough space to store this pointer
    return BM_ERROR_INVALID_ARGUMENT;
  }

  pool->device = hDevice;
  pool->memory = hMemory;
  pool->elementCount = 0;
  pool->elementCapacity = hMemory->size / type_priv->size;

  pool->elementType = type;

  BmResult result;
  result = bmMapMemory(pool->device, pool->memory, 0, pool->elementCount * type_priv->size, (void**)&pool->pBuffer);
  if (pool->pBuffer == NULL)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }
#ifdef BM_POOL_USE_INT_HANDLE
  pool->pFreeList = 0;
#else
  pool->pFreeList = (BmPoolElementHandle)pool->pBuffer;
#endif
  return result;
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

  BmPoolElementHandle pElement = pool->pFreeList;
  
  while (pElement != BM_POOL_ELEMENT_INVALID)
  {
#ifdef BM_POOL_USE_INT_HANDLE
    void *pElementPtr = (void*)((char*)pool->pBuffer + pElement * type_priv->size);
    BmPoolElementHandle pNext = *(BmPoolElementHandle*)pElementPtr;
    type_priv->free(pElementPtr);
#else
    BmPoolElementHandle pNext = *(BmPoolElementHandle*)pElement;
    type_priv->free((void*)pElement);
#endif

    pElement = pNext;
  }

  // Unmap memory
  // bmUnmapMemory(pool->device, pool->memory);

  return BM_SUCCESS;
}

BmResult bmPoolAllocate(BmPool *pool, void **ppData)
{
  if (pool->elementCount >= pool->elementCapacity)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  BmTypePrivate *type_priv = bmTypeManagerGetTypeFromHandle(g_typeManager, pool->elementType);

  BmPoolElementHandle pElement = pool->pFreeList;
  if (pElement != BM_POOL_ELEMENT_INVALID)
  {
#ifdef BM_POOL_USE_INT_HANDLE
    void *pElementPtr = (void*)((char*)pool->pBuffer + pElement * type_priv->size);
    pool->pFreeList = *(BmPoolElementHandle*)pElementPtr;
#else
    pool->pFreeList = *(BmPoolElementHandle*)pElement;
#endif
  }
  else
  {
    // Allocate new element
#ifdef BM_POOL_USE_INT_HANDLE
    pElement = pool->elementCount;
#else
    pElement = (BmPoolElementHandle)pool->pBuffer + pool->elementCount * type_priv->size;
#endif
    pool->elementCount++;
  }

  pool->elementCount++;
  if (pool->elementCount >= pool->elementCapacity)
  {
    pool->pFreeList = BM_POOL_ELEMENT_INVALID;
  }

#ifdef BM_POOL_USE_INT_HANDLE
  ppData = (void*)((char*)pool->pBuffer + pElement * type_priv->size);
#else
  *ppData = pElement;
#endif

  bmTypeInitializeForType(pool->elementType, (void*)*ppData);

  return BM_SUCCESS;
}
