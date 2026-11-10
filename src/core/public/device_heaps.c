#include "shared.h"

#include "device.h"
#include "device_heaps.h"
#include "device_private.h"
#include "memory_private.h"

#include "hal.h"

// A first-fit allocator over the memories the HAL reports. Every heap is a list of blocks in
// address order; each block is either in use or free. Allocating splits a free block, freeing
// merges neighbouring free blocks again. Block descriptors come from a fixed table, so the
// allocator itself never allocates.
//
// Alignment is measured from the start of the heap, not from address 0: on the GBA, VRAM
// structures such as screen blocks (2 KB) and char blocks (16 KB) are aligned relative to the
// start of VRAM, and the host back end can then use plain arrays.

typedef struct HeapState
{
  uint8_t *pStart;
  BmDeviceSize size;
  BmDeviceSize used;
  BmDeviceMemoryPrivate *pFirst;
} HeapState;

static BmDeviceMemoryPrivate s_blocks[BM_MAX_MEMORY_BLOCKS] BM_EWRAM_BSS;
static BmDeviceMemoryPrivate *s_pUnusedBlocks;
static HeapState s_heaps[BM_MAX_MEMORY_HEAPS];

static BmDeviceMemoryPrivate *claimBlock(void)
{
  BmDeviceMemoryPrivate *pBlock = s_pUnusedBlocks;
  if (pBlock != NULL)
  {
    s_pUnusedBlocks = pBlock->pNext;
    *pBlock = (BmDeviceMemoryPrivate){0};
  }
  return pBlock;
}

static void releaseBlock(BmDeviceMemoryPrivate *pBlock)
{
  pBlock->pData = NULL;
  pBlock->size = 0;
  pBlock->inUse = 0;
  pBlock->pNext = s_pUnusedBlocks;
  s_pUnusedBlocks = pBlock;
}

static bool isBlockHandle(BmDeviceMemoryHandle hMemory)
{
  return hMemory >= &s_blocks[0] && hMemory < &s_blocks[BM_MAX_MEMORY_BLOCKS];
}

static BmDeviceSize offsetInHeap(const HeapState *pHeap, const BmDeviceMemoryPrivate *pBlock)
{
  return (BmDeviceSize)((uint8_t *)pBlock->pData - pHeap->pStart);
}

static BmResult heapsAllocateMemory(
    BmDeviceHandle device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory)
{
  (void)device;
  if (pAllocateInfo == NULL || phMemory == NULL ||
      pAllocateInfo->memoryTypeIndex >= BM_MAX_MEMORY_HEAPS || pAllocateInfo->allocationSize == 0)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  BmDeviceSize alignment = pAllocateInfo->alignment != 0 ? pAllocateInfo->alignment : 4;
  if ((alignment & (alignment - 1)) != 0)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  // Sizes are kept multiples of 4 so every block starts 4-byte aligned.
  BmDeviceSize size = (pAllocateInfo->allocationSize + 3u) & ~3u;

  HeapState *pHeap = &s_heaps[pAllocateInfo->memoryTypeIndex];
  for (BmDeviceMemoryPrivate *pBlock = pHeap->pFirst; pBlock != NULL; pBlock = pBlock->pNext)
  {
    if (pBlock->inUse)
    {
      continue;
    }

    BmDeviceSize start = offsetInHeap(pHeap, pBlock);
    BmDeviceSize alignedStart = (start + alignment - 1) & ~(alignment - 1);
    BmDeviceSize padding = alignedStart - start;
    if (pBlock->size < padding || pBlock->size - padding < size)
    {
      continue;
    }

    // Split off the padding in front as its own free block.
    if (padding > 0)
    {
      BmDeviceMemoryPrivate *pFront = claimBlock();
      if (pFront == NULL)
      {
        return BM_ERROR_TOO_MANY_OBJECTS;
      }
      // pFront takes the place of pBlock in the list; pBlock moves behind it.
      *pFront = *pBlock;
      pFront->size = padding;
      pFront->pNext = pBlock;
      pBlock->pData = (uint8_t *)pBlock->pData + padding;
      pBlock->size -= padding;
      // Fix the link that pointed to pBlock.
      if (pHeap->pFirst == pBlock)
      {
        pHeap->pFirst = pFront;
      }
      else
      {
        BmDeviceMemoryPrivate *pPrev = pHeap->pFirst;
        while (pPrev->pNext != pBlock)
        {
          pPrev = pPrev->pNext;
        }
        pPrev->pNext = pFront;
      }
    }

    // Split off the rest behind as a free block.
    if (pBlock->size > size)
    {
      BmDeviceMemoryPrivate *pRest = claimBlock();
      if (pRest == NULL)
      {
        return BM_ERROR_TOO_MANY_OBJECTS;
      }
      pRest->pData = (uint8_t *)pBlock->pData + size;
      pRest->size = pBlock->size - size;
      pRest->heapIndex = pBlock->heapIndex;
      pRest->inUse = 0;
      pRest->pNext = pBlock->pNext;
      pBlock->pNext = pRest;
      pBlock->size = size;
    }

    pBlock->inUse = 1;
    pHeap->used += pBlock->size;
    *phMemory = pBlock;
    return BM_SUCCESS;
  }

  return BM_ERROR_OUT_OF_MEMORY;
}

static BmResult heapsMapMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory,
    BmDeviceSize offset,
    BmDeviceSize size,
    void **ppData)
{
  (void)device;
  if (!isBlockHandle(hMemory) || !hMemory->inUse || ppData == NULL ||
      offset > hMemory->size || size > hMemory->size - offset)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  *ppData = (uint8_t *)hMemory->pData + offset;
  return BM_SUCCESS;
}

static BmResult heapsFreeMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory)
{
  (void)device;
  if (!isBlockHandle(hMemory) || !hMemory->inUse)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  HeapState *pHeap = &s_heaps[hMemory->heapIndex];
  pHeap->used -= hMemory->size;
  hMemory->inUse = 0;

  // Merge with the following free block.
  BmDeviceMemoryPrivate *pNext = hMemory->pNext;
  if (pNext != NULL && !pNext->inUse)
  {
    hMemory->size += pNext->size;
    hMemory->pNext = pNext->pNext;
    releaseBlock(pNext);
  }

  // Merge with the preceding free block.
  BmDeviceMemoryPrivate *pPrev = NULL;
  for (BmDeviceMemoryPrivate *p = pHeap->pFirst; p != hMemory; p = p->pNext)
  {
    pPrev = p;
  }
  if (pPrev != NULL && !pPrev->inUse)
  {
    pPrev->size += hMemory->size;
    pPrev->pNext = hMemory->pNext;
    releaseBlock(hMemory);
  }

  return BM_SUCCESS;
}

static BmResult heapsGetMemoryProperties(
    BmDeviceHandle device,
    BmPhysicalDeviceMemoryProperties *pProperties)
{
  (void)device;
  *pProperties = (BmPhysicalDeviceMemoryProperties){0};
  pProperties->memoryHeapCount = BM_MAX_MEMORY_HEAPS;
  pProperties->memoryTypeCount = BM_MAX_MEMORY_HEAPS;
  for (uint32_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    pProperties->memoryHeaps[i].size = s_heaps[i].size;
    pProperties->memoryTypes[i].heapIndex = i;
    pProperties->heapBudget[i] = s_heaps[i].size;
    pProperties->heapUsage[i] = s_heaps[i].used;
  }
  return BM_SUCCESS;
}

static BmResult heapsDestroyDevice(BmDeviceHandle device)
{
  (void)device;
  return BM_SUCCESS;
}

static BmDevice s_device = {
  .name = "heaps",
  .pfnAllocateMemory = heapsAllocateMemory,
  .pfnMapMemory = heapsMapMemory,
  .pfnFreeMemory = heapsFreeMemory,
  .pfnGetMemoryProperties = heapsGetMemoryProperties,
  .pfnDestroyDevice = heapsDestroyDevice,
};

BmDeviceHandle bmDeviceHeapsCreate(void)
{
  s_pUnusedBlocks = NULL;
  for (int i = BM_MAX_MEMORY_BLOCKS - 1; i >= 0; i--)
  {
    releaseBlock(&s_blocks[i]);
  }

  BmHalMemoryRange ranges[BM_MAX_MEMORY_HEAPS];
  bmHalGetMemoryRanges(ranges, BM_MAX_MEMORY_HEAPS);

  for (uint32_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    HeapState *pHeap = &s_heaps[i];
    pHeap->pStart = (uint8_t *)ranges[i].pStart;
    pHeap->size = (BmDeviceSize)(ranges[i].size & ~(size_t)3);
    pHeap->used = 0;
    pHeap->pFirst = NULL;
    if (pHeap->size == 0)
    {
      continue;
    }

    BmDeviceMemoryPrivate *pBlock = claimBlock();
    pBlock->pData = pHeap->pStart;
    pBlock->size = pHeap->size;
    pBlock->heapIndex = (uint8_t)i;
    pHeap->pFirst = pBlock;
  }

  return &s_device;
}
