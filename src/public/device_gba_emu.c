#include "shared.h"

#include "device.h"
#include "device_gba_emu.h"

#include "errors.h"

#include "device_private.h"

#include "pool.h"

#define BM_DEBUG_MEMORY_BLOCK

#ifdef BM_DEBUG_MEMORY_BLOCK
#include <assert.h>
#endif

#ifndef GBA
#define EMU
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif
#endif

typedef struct MemoryRange
{
  void *start;
  size_t size;
  /// Next allocated block with unused memory
  struct MemoryBlock *pNextFreeBlock;
  /// Next allocated block with used memory
  struct MemoryBlock *pNextUsedBlock;
} MemoryRange;

typedef struct MemoryBlock
{
  // If referenced as free block, this is the next free block
  // if referenced as used block, this is the next block for the same memory heap
  struct MemoryBlock *pNext;
  void *pMemory;
  size_t size;
#ifdef BM_DEBUG_MEMORY_BLOCK
  // Determines if the block itself (but not the memory) is allocated
  bool bIsInUse;
#endif
} MemoryBlock;

typedef struct BmDeviceMemoryPrivate
{
  MemoryBlock block;
} BmDeviceMemoryPrivate;

#define MAX_MEMORY_BLOCKS 1024

//static struct MemoryBlock g_memoryBlocks[MAX_MEMORY_BLOCKS];

// Place the memory blocks at the end of EWRAM (assuming allocations don't happen that often)
// therefore placing it into the slower RAM should be fine, if not, we can move it to IWRAM
static struct MemoryBlock *g_memoryBlocks = (struct MemoryBlock*)((char*)0x02000000+0x40000 - sizeof(struct MemoryBlock)*MAX_MEMORY_BLOCKS);

// Next unused block element
static struct MemoryBlock *g_lastFreeBlockElement = NULL;

// These will have to be taken from the linker script
static MemoryRange g_memoryRanges[BM_MAX_MEMORY_HEAPS] = {
  { .start = (void *)0x02000000, .size = 0x40000 }, // EWRAM
  { .start = (void *)0x03000000, .size = 0x8000  }, // IWRAM
  { .start = (void *)0x06000000, .size = 0x18000 }, // VRAM
};

static int GetMemoryTypeIndex(MemoryBlock *block);
static MemoryBlock *ClaimBlock();

static void InitializeMemoryBlocks()
{
  for (size_t i=0; i < MAX_MEMORY_BLOCKS-1; i++)
  {
    g_memoryBlocks[i].pNext = &g_memoryBlocks[i+1];
    g_memoryBlocks[i].pMemory = NULL;
    g_memoryBlocks[i].size = 0;
#ifdef BM_DEBUG_MEMORY_BLOCK
    g_memoryBlocks[i].bIsInUse = false;
#endif
  }

  g_lastFreeBlockElement = &g_memoryBlocks[0];
}

// Traverses linked list of free blocks and returns the next free block that can be used
static MemoryBlock *GetNextFreeBlock(MemoryBlock *pBlock, const BmMemoryAllocateInfo *pAllocateInfo, int *pRemainingSize)
{
  while (pBlock)
  {
    *pRemainingSize = pBlock->size - pAllocateInfo->allocationSize;
    if (*pRemainingSize >= 0)
    {
      return pBlock;
    }

    pBlock = pBlock->pNext;
#ifdef BM_DEBUG_MEMORY_BLOCK
    // Most obvious case of a circular list
    assert(pBlock != pBlock->pNext);
#endif
  }

  return NULL;
}

static MemoryBlock *GetLeafBlock(MemoryBlock *pBlock)
{
  if (pBlock == NULL)
  {
    return NULL;
  }

  while (pBlock->pNext)
  {
    pBlock = pBlock->pNext;
  }

  return pBlock;
}

static BmResult bmGbaEmuAllocateMemory(
    BmDeviceHandle device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory)
{
  (void)device;

  if (pAllocateInfo->memoryTypeIndex >= BM_MAX_MEMORY_HEAPS)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  MemoryRange *pRange = &g_memoryRanges[pAllocateInfo->memoryTypeIndex];
  int remainingSize = -1;
  MemoryBlock *pBlock = GetNextFreeBlock(pRange->pNextFreeBlock, pAllocateInfo, &remainingSize);
  if (pBlock == NULL)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

#ifdef BM_DEBUG_MEMORY_BLOCK
  assert(pBlock->bIsInUse);
#endif

  pBlock->size = pAllocateInfo->allocationSize;
  // Split block into used and free block (if there is enough space)
  if (remainingSize > 0)
  {
    MemoryBlock *pNextFreeBlock = ClaimBlock();
    // pBlock->pNext = NULL;

    if (pRange->pNextFreeBlock == pBlock)
    {
      pRange->pNextFreeBlock = NULL;
    }

    // This block will be used now, so it will be removed from the free list
    // obtain the next free block
    if (pRange->pNextFreeBlock)
    {
      MemoryBlock *pLastFreeBlock = GetLeafBlock(pRange->pNextFreeBlock);

      if (pLastFreeBlock == NULL)
      {
        return BM_ERROR_INVALID_ARGUMENT;
      }
      pLastFreeBlock->pNext = pNextFreeBlock;
    }
    else
    {
      pRange->pNextFreeBlock = pNextFreeBlock;
    }


    pBlock->pNext = pNextFreeBlock;
    
    pNextFreeBlock->pNext = NULL;
    pNextFreeBlock->pMemory = (char*)pBlock->pMemory + pAllocateInfo->allocationSize;
    pNextFreeBlock->size = remainingSize;
  }

  *phMemory = (BmDeviceMemoryPrivate*)pBlock;

  return BM_SUCCESS;
}

static BmResult bmGbaEmuMapMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory,
    BmDeviceSize offset,
    BmDeviceSize size,
    void **ppData)
{
  (void)device;

  *ppData = (char*)hMemory->block.pMemory + offset;
  if (size > hMemory->block.size)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  return BM_SUCCESS;
}

static int GetMemoryTypeIndex(MemoryBlock *block)
{
  for (size_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    MemoryRange *pRange = &g_memoryRanges[i];

    if (block->pMemory >= pRange->start && block->pMemory < (void*)((char*)pRange->start + pRange->size))
    {
      return i;
    }
  }

  return -1;
}

static BmResult bmGbaEmuFreeMemory(
    BmDeviceHandle device,
    BmDeviceMemoryHandle hMemory)
{
  (void)device;

  int memoryTypeIndex = GetMemoryTypeIndex(&hMemory->block);
#ifdef BM_DEBUG_MEMORY_BLOCK
  assert(memoryTypeIndex >= 0);
#endif
  MemoryBlock *pLastFreeBlock = g_memoryRanges[memoryTypeIndex].pNextFreeBlock;
  g_memoryRanges[memoryTypeIndex].pNextFreeBlock = &hMemory->block;
  hMemory->block.pNext = pLastFreeBlock;

  // TODO: Potentially merge blocks and release them from memory type specific handling

  return BM_SUCCESS;
}

static BmResult bmGbaEmuDestroyDevice(
    BmDeviceHandle device)
{
  (void)device;
#ifdef EMU
  for (size_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    int nResult = munmap(g_memoryRanges[i].start, g_memoryRanges[i].size);
    if (nResult != 0)
    {
      assert(false);
      return BM_ERROR_INVALID_ARGUMENT;
    }
  }
#endif

  return BM_SUCCESS;
}

static BmDevice g_deviceGbaEmu = {
  .name = "gba_emu",
  .pfnAllocateMemory = bmGbaEmuAllocateMemory,
  .pfnMapMemory = bmGbaEmuMapMemory,
  .pfnFreeMemory = bmGbaEmuFreeMemory,

  .pfnDestroyDevice = bmGbaEmuDestroyDevice,
};

static MemoryBlock *ClaimBlock()
{
  MemoryBlock *pNew = g_lastFreeBlockElement;
  g_lastFreeBlockElement = pNew->pNext;
#ifdef BM_DEBUG_MEMORY_BLOCK
  pNew->bIsInUse = true;
#endif
  pNew->pNext = NULL;
  pNew->size = 0;
  pNew->pMemory = NULL;
  return pNew;
}

/*
static void FreeBlock(MemoryBlock *pBlock)
{
  pBlock->pNext = g_lastFreeBlockElement;
  g_lastFreeBlockElement = pBlock;
#ifdef BM_DEBUG_MEMORY_BLOCK
  pBlock->bIsInUse = false;
#endif
}
*/


BmDeviceHandle bmDeviceGbaCreate()
{
#ifdef EMU
  for (size_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    void *pResult = mmap(g_memoryRanges[i].start, g_memoryRanges[i].size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (pResult == MAP_FAILED)
    {
      assert(false);
      return NULL;
    }
  }
#endif

  InitializeMemoryBlocks();

  for (size_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    MemoryBlock *pBlock = ClaimBlock();
    pBlock->pMemory = g_memoryRanges[i].start;
    pBlock->size = g_memoryRanges[i].size;
    if (i == 0)
    {
      // subtract memory blocks from EWRAM
      pBlock->size -= sizeof(struct MemoryBlock)*MAX_MEMORY_BLOCKS;
    }

    g_memoryRanges[i].pNextFreeBlock = pBlock;
    g_memoryRanges[i].pNextUsedBlock = NULL;
  }



  return &g_deviceGbaEmu;
}

#ifdef BM_DEBUG_MEMORY_BLOCK
#include <stdio.h>
void DebugMemory()
{
  for (size_t i = 0; i < MAX_MEMORY_BLOCKS; i++)
  {
    MemoryBlock *pBlock = &g_memoryBlocks[i];
    if (pBlock->bIsInUse)
    {
      printf("Block %ld is in use\n", i);
      printf("  Memory: %p\n", pBlock->pMemory);
      printf("  Size: %ld\n", pBlock->size);
      printf("  Next: %p\n", (void*)pBlock->pNext);
    }
  }
}
#endif