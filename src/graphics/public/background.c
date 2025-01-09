#include "shared.h"
#include "background.h"

#include "../../public/memory.h"

#include "pool.h"

#include "type.h"
#include "type_manager.h"

#include "errors.h"

typedef struct BmBackground
{
#ifndef GBA
  bool blending;
#else
  uint16_t pChars[1024];
#endif
} BmBackground;

/*
BmResult bmBackdropAllocate(BmBackground **ppBackground)
{
  // TODO: Get 1024*2 bytes of VRAM, aligned to multiples of 2048 bytes
  BmResult result = bmAllocateMemory(NULL, &(BmMemoryAllocateInfo) {
    .allocationSize = 1024*2,
    .memoryTypeIndex = 0,
  }, (BmDeviceMemoryHandle*)ppBackground);

  return BmResult();
}
*/

static BmPool backgroundPool;
static BmTypeHandle backgroundType;

static BmResult ensureBackgroundPool()
{
  BmResult result = BM_SUCCESS;
  if (backgroundPool.device == NULL)
  {
    result = bmTypeManagerCreate(g_typeManager, &(BmTypeCreateInfo) {
      .name = "BmBackground",
      .size = sizeof(BmBackground),
      .init = NULL,
      .free = NULL,
    }, &backgroundType);

    if (result != BM_SUCCESS)
    {
      return result;
    }

    return bmPoolInit(&backgroundPool, g_defaultDevice, g_defaultMemory, backgroundType);
  }
  return BM_SUCCESS;
}

BmResult bmCreateBackdrop(BmBackground **ppBackground)
{
  BmResult result = ensureBackgroundPool();
  if (result != BM_SUCCESS)
  {
    return result;
  }

  return bmPoolAllocate(&backgroundPool, (void**)ppBackground);
}

BmResult bmBindBackdropMemory(BmDeviceHandle hDevice, unsigned char bindInfoCount, const BmBindBackgroundInfo *pBindInfos)
{
  pBackground->memory = hMemory;
  return BM_SUCCESS;
}