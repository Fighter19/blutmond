#include "shared.h"
#include "tilemap.h"

#include "memory.h"
#include "pool.h"
#include "type.h"
#include "type_manager.h"

struct BmTilemap
{
  BmTilemapUsageFlags usage;
  BmExtent3D extent;

  bool blending;
  BmDeviceMemoryHandle memory;
  BmDeviceSize memoryOffset;
};

static BmPool s_tilemapPool;
static BmTypeHandle s_tilemapType;
static BmDeviceMemoryHandle s_tilemapPoolMemory;

// The pool of tilemap objects lives in EWRAM and is created on first use.
static BmResult ensureTilemapPool(BmDeviceHandle device)
{
  if (s_tilemapPool.device != NULL)
  {
    return s_tilemapPool.device == device ? BM_SUCCESS : BM_ERROR_INVALID_ARGUMENT;
  }
  if (g_typeManager == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmResult result = bmTypeManagerCreate(g_typeManager, &(BmTypeCreateInfo) {
    .name = "BmTilemap",
    .size = sizeof(BmTilemap),
  }, &s_tilemapType);
  if (result != BM_SUCCESS)
  {
    return result;
  }

  BmDeviceSize size = (BmDeviceSize)bmPoolRequiredSize(sizeof(BmTilemap), BM_MAX_TILEMAPS);
  result = bmAllocateMemory(device, &(BmMemoryAllocateInfo) {
    .allocationSize = size,
    .memoryTypeIndex = BM_MEMORY_HEAP_EWRAM,
  }, &s_tilemapPoolMemory);
  if (result != BM_SUCCESS)
  {
    return result;
  }

  return bmPoolInit(&s_tilemapPool, device, s_tilemapPoolMemory, size, s_tilemapType);
}

BmResult bmCreateTilemap(BmDeviceHandle device, const BmTilemapCreateInfo *pCreateInfo, BmTilemap **ppTilemap)
{
  if (device == NULL || pCreateInfo == NULL || ppTilemap == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmResult result = ensureTilemapPool(device);
  if (result != BM_SUCCESS)
  {
    return result;
  }

  BmTilemap *pTilemap = NULL;
  result = bmPoolAllocate(&s_tilemapPool, (void **)&pTilemap);
  if (result != BM_SUCCESS)
  {
    return result;
  }

  pTilemap->usage = pCreateInfo->usage;
  pTilemap->extent = pCreateInfo->extent;
  pTilemap->blending = false;
  pTilemap->memory = NULL;
  pTilemap->memoryOffset = 0;

  *ppTilemap = pTilemap;
  return BM_SUCCESS;
}

BmResult bmDestroyTilemap(BmDeviceHandle device, BmTilemap *pTilemap)
{
  (void)device;
  return bmPoolFree(&s_tilemapPool, pTilemap);
}

BmResult bmBindTilemapMemory(BmDeviceHandle device, uint32_t bindInfoCount, const BmBindTilemapInfo *pBindInfos)
{
  (void)device;
  if (bindInfoCount > 0 && pBindInfos == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0; i < bindInfoCount; i++)
  {
    const BmBindTilemapInfo *pInfo = &pBindInfos[i];
    if (pInfo->pTilemap == NULL)
    {
      return BM_ERROR_INVALID_ARGUMENT;
    }
    pInfo->pTilemap->memory = pInfo->hMemory;
    pInfo->pTilemap->memoryOffset = pInfo->memoryOffset;
  }
  return BM_SUCCESS;
}

BmResult bmTilemapSetBlending(BmTilemap *pTilemap, bool enable)
{
  if (pTilemap == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  pTilemap->blending = enable;
  return BM_SUCCESS;
}
