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
  struct MemoryBlock *pFirstFreeBlock;
} MemoryRange;

typedef struct MemoryBlock
{
  // If referenced as free block, this is the next free block
  // if referenced as used block, this is the next block for the same memory heap
  struct MemoryBlock *pNext;
  void *pMemory;
  size_t size;
#ifdef BM_DEBUG_MEMORY_BLOCK
  bool bIsInUse;
#endif
} MemoryBlock;

typedef struct BmDeviceMemoryPrivate
{
  MemoryBlock block;
} BmDeviceMemoryPrivate;

#define MAX_MEMORY_BLOCKS 1024

static struct MemoryBlock g_memoryBlocks[MAX_MEMORY_BLOCKS];

// These will have to be taken from the linker script
static MemoryRange g_memoryRanges[BM_MAX_MEMORY_HEAPS] = {
  { .start = (void *)0x02000000, .size = 0x40000 }, // EWRAM
  { .start = (void *)0x03000000, .size = 0x8000 },  // IWRAM
  { .start = (void *)0x06000000, .size = 0x18000 }, // VRAM
};

static BmResult bmGbaEmuAllocateMemory(
    BmDeviceHandle device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory)
{
  (void)device;

  if (pAllocateInfo->memoryTypeIndex >= BM_MAX_MEMORY_HEAPS)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  MemoryRange *pRange = &g_memoryRanges[pAllocateInfo->memoryTypeIndex];
  MemoryBlock *pBlock = pRange->pFirstFreeBlock;
  if (pBlock == NULL)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  int remainingSize = pBlock->size - pAllocateInfo->allocationSize;
  if (remainingSize < 0)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

#ifdef BM_DEBUG_MEMORY_BLOCK
  assert(!pBlock->bIsInUse);
  pBlock->bIsInUse = true;
#endif

  pBlock->size = pAllocateInfo->allocationSize;
  pBlock->pNext = NULL;
  if (remainingSize - sizeof(MemoryBlock) > 0)
  {
    pBlock->pNext = (MemoryBlock*)((char*)pBlock + sizeof(MemoryBlock));
    pBlock->pNext->pNext = NULL;
    pBlock->pNext->pMemory = (char*)pBlock->pMemory + pAllocateInfo->allocationSize;
    pBlock->pNext->size = remainingSize;
#ifdef BM_DEBUG_MEMORY_BLOCK
    assert(!pBlock->pNext->bIsInUse);
    pBlock->pNext->bIsInUse = false;
#endif
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

static int GetMemoryTypeIndex(BmDeviceMemoryHandle hMemory)
{
  for (size_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
    MemoryRange *pRange = &g_memoryRanges[i];

    if (hMemory->block.pMemory >= pRange->start && hMemory->block.pMemory < (void*)((char*)pRange->start + pRange->size))
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

  int memoryTypeIndex = GetMemoryTypeIndex(hMemory);
#ifdef BM_DEBUG_MEMORY_BLOCK
  assert(memoryTypeIndex >= 0);
#endif
  MemoryBlock *pLastFreeBlock = g_memoryRanges[memoryTypeIndex].pFirstFreeBlock;
  g_memoryRanges[memoryTypeIndex].pFirstFreeBlock = &hMemory->block;
  hMemory->block.pNext = pLastFreeBlock;

#ifdef BM_DEBUG_MEMORY_BLOCK
  assert(hMemory->block.bIsInUse);
  hMemory->block.bIsInUse = false;
#endif

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

BmDeviceHandle bmDeviceGbaCreate()
{
  for (size_t i = 0; i < BM_MAX_MEMORY_HEAPS; i++)
  {
#ifdef EMU
    void *pResult = mmap(g_memoryRanges[i].start, g_memoryRanges[i].size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (pResult == MAP_FAILED)
    {
      assert(false);
      return NULL;
    }
#endif

    g_memoryRanges[i].pFirstFreeBlock = &g_memoryBlocks[i];
  
    g_memoryBlocks[i].pNext = NULL;
    g_memoryBlocks[i].pMemory = g_memoryRanges[i].start;
    g_memoryBlocks[i].size = g_memoryRanges[i].size;
#ifdef BM_DEBUG_MEMORY_BLOCK
    g_memoryBlocks[i].bIsInUse = false;
#endif
  }
  
  return &g_deviceGbaEmu;
}