#include "shared.h"
#include "tilemap.h"

#include "../../public/memory.h"

#include "pool.h"

#include "type.h"
#include "type_manager.h"

#include "errors.h"

typedef struct BmTilemap
{
  BmTilemapUsageFlags usage;
  BmExtent3D extent;

  bool blending;
  BmDeviceMemoryHandle memory;
} BmTilemap;

/*
BmResult bmBackdropAllocate(BmTilemap **ppTilemap)
{
  // TODO: Get 1024*2 bytes of VRAM, aligned to multiples of 2048 bytes
  BmResult result = bmAllocateMemory(NULL, &(BmMemoryAllocateInfo) {
    .allocationSize = 1024*2,
    .memoryTypeIndex = 0,
  }, (BmDeviceMemoryHandle*)ppTilemap);

  return BmResult();
}
*/

static BmPool tilemapPool;
static BmTypeHandle tilemapType;

static BmResult ensureTilemapPool()
{
  BmResult result = BM_SUCCESS;
  if (tilemapPool.device == NULL)
  {
    result = bmTypeManagerCreate(g_typeManager, &(BmTypeCreateInfo) {
      .name = "BmTilemap",
      .size = sizeof(BmTilemap),
      .init = NULL,
      .free = NULL,
    }, &tilemapType);

    if (result != BM_SUCCESS)
    {
      return result;
    }

    return bmPoolInit(&tilemapPool, g_defaultDevice, g_defaultMemory, tilemapType);
  }
  return BM_SUCCESS;
}

BmResult bmCreateTilemap(BmDeviceHandle device, struct BmTilemapCreateInfo *pCreateInfo, struct BmTilemap **ppTilemap)
{
  BmResult result = ensureTilemapPool();
  if (result != BM_SUCCESS)
  {
    return result;
  }

  result = bmPoolAllocate(&tilemapPool, (void**)ppTilemap);
  if (result != BM_SUCCESS)
  {
    return result;
  }
  
  BmTilemap *pTilemap = *ppTilemap;
  pTilemap->usage = pCreateInfo->usage;
  pTilemap->extent = pCreateInfo->extent;
  pTilemap->blending = false;
  pTilemap->memory = NULL;

  return BM_SUCCESS;
}

BmResult bmBindTilemapMemory(BmDeviceHandle hDevice, unsigned char bindInfoCount, const BmBindTilemapInfo *pBindInfos)
{
  for (unsigned char i = 0; i < bindInfoCount; i++)
  {
    BmBindTilemapInfo bindInfo = pBindInfos[i];
    BmTilemap *pTilemap = bindInfo.pTilemap;

    pTilemap->memory = bindInfo.hMemory;
  }
  return BM_SUCCESS;
}