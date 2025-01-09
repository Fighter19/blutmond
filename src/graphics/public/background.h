#pragma once

#include "shared.h"

BmResult bmBackdropAllocate(struct BmBackground **ppBackground);
BmResult bmBackgroundSetBlending(struct BmBackground *pBackground, bool enable);

typedef struct BmBindBackgroundInfo
{
  struct BmBackground *pBackground;
  BmDeviceMemoryHandle hMemory;
  BmDeviceSize memoryOffset;
} BmBindBackgroundInfo;