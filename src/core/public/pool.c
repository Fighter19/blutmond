#include "pool.h"
#include "errors.h"

#include "type_private.h"
#include "type_manager.h"

#include <string.h>

static size_t bitmapBytes(size_t capacity)
{
  return (capacity + 7) / 8;
}

size_t bmPoolRequiredSize(size_t elementSize, size_t capacity)
{
  return elementSize * capacity + bitmapBytes(capacity);
}

static bool isUsed(const BmPool *pool, size_t index)
{
  return (pool->pUsedBits[index / 8] >> (index % 8)) & 1u;
}

static void setUsed(BmPool *pool, size_t index, bool used)
{
  if (used)
  {
    pool->pUsedBits[index / 8] |= (uint8_t)(1u << (index % 8));
  }
  else
  {
    pool->pUsedBits[index / 8] &= (uint8_t)~(1u << (index % 8));
  }
}

static void *elementPtr(const BmPool *pool, size_t index)
{
  return (uint8_t *)pool->pBuffer + index * pool->elementSize;
}

BmResult bmPoolInit(BmPool *pool, BmDeviceHandle hDevice, BmDeviceMemoryHandle hMemory, BmDeviceSize size, BmTypeHandle type)
{
  if (pool == NULL || hDevice == NULL || hMemory == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmTypePrivate *typePriv = bmTypeManagerGetTypeFromHandle(g_typeManager, type);
  if (typePriv == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  // A free element stores the index of the next free element in its first bytes.
  if (typePriv->size < sizeof(BmPoolElementHandle))
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  // Largest capacity whose elements plus bitmap fit into `size`.
  size_t capacity = (size_t)size * 8 / (typePriv->size * 8 + 1);
  while (capacity > 0 && bmPoolRequiredSize(typePriv->size, capacity) > size)
  {
    capacity--;
  }
  if (capacity == 0)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  void *pData = NULL;
  BmResult result = bmMapMemory(hDevice, hMemory, 0, size, &pData);
  if (result != BM_SUCCESS)
  {
    return result;
  }

  *pool = (BmPool){
    .device = hDevice,
    .memory = hMemory,
    .elementSize = typePriv->size,
    .elementCapacity = capacity,
    .elementType = type,
    .pFreeList = BM_POOL_ELEMENT_INVALID,
    .pBuffer = pData,
    .pUsedBits = (uint8_t *)pData + typePriv->size * capacity,
  };
  memset(pool->pUsedBits, 0, bitmapBytes(capacity));
  return BM_SUCCESS;
}

BmResult bmPoolFinalize(BmPool *pool)
{
  if (pool == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  BmTypePrivate *typePriv = bmTypeManagerGetTypeFromHandle(g_typeManager, pool->elementType);
  if (typePriv == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  for (size_t i = 0; i < pool->elementHighWater; i++)
  {
    if (isUsed(pool, i))
    {
      if (typePriv->free != NULL)
      {
        typePriv->free(elementPtr(pool, i));
      }
      setUsed(pool, i, false);
    }
  }
  pool->elementCount = 0;
  pool->elementHighWater = 0;
  pool->pFreeList = BM_POOL_ELEMENT_INVALID;
  return BM_SUCCESS;
}

BmResult bmPoolAllocate(BmPool *pool, void **ppData)
{
  if (pool == NULL || ppData == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  if (pool->elementCount >= pool->elementCapacity)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  BmPoolElementHandle index = pool->pFreeList;
  if (index != BM_POOL_ELEMENT_INVALID)
  {
    // Re-use the most recently freed element.
    memcpy(&pool->pFreeList, elementPtr(pool, index), sizeof(BmPoolElementHandle));
  }
  else
  {
    index = (BmPoolElementHandle)pool->elementHighWater++;
  }

  pool->elementCount++;
  setUsed(pool, index, true);

  void *pElement = elementPtr(pool, index);
  memset(pElement, 0, pool->elementSize);
  bmTypeInitializeForType(pool->elementType, pElement);

  *ppData = pElement;
  return BM_SUCCESS;
}

BmPoolElementHandle bmPoolIndexOf(const BmPool *pool, const void *pData)
{
  if (pool == NULL || pData == NULL || (const uint8_t *)pData < (const uint8_t *)pool->pBuffer)
  {
    return BM_POOL_ELEMENT_INVALID;
  }
  size_t offset = (size_t)((const uint8_t *)pData - (const uint8_t *)pool->pBuffer);
  size_t index = offset / pool->elementSize;
  if (offset % pool->elementSize != 0 || index >= pool->elementHighWater || !isUsed(pool, index))
  {
    return BM_POOL_ELEMENT_INVALID;
  }
  return (BmPoolElementHandle)index;
}

BmResult bmPoolFree(BmPool *pool, void *pData)
{
  BmPoolElementHandle index = bmPoolIndexOf(pool, pData);
  if (index == BM_POOL_ELEMENT_INVALID)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmTypePrivate *typePriv = bmTypeManagerGetTypeFromHandle(g_typeManager, pool->elementType);
  if (typePriv != NULL && typePriv->free != NULL)
  {
    typePriv->free(pData);
  }

  // The freed element becomes the head of the free list.
  memcpy(pData, &pool->pFreeList, sizeof(BmPoolElementHandle));
  pool->pFreeList = index;
  setUsed(pool, index, false);
  pool->elementCount--;
  return BM_SUCCESS;
}
