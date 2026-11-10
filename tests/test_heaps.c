#include "device.h"
#include "device_heaps.h"
#include "memory_private.h"
#include "physical_device.h"

#include "test.h"

static BmDeviceHandle s_device;

static void setUp(void)
{
  bmCreateDevice(bmGetPhysicalDevice(), &s_device);
}

static BmResult alloc(uint32_t heap, BmDeviceSize size, BmDeviceSize alignment, BmDeviceMemoryHandle *ph)
{
  return bmAllocateMemory(s_device, &(BmMemoryAllocateInfo) {
    .allocationSize = size, .memoryTypeIndex = heap, .alignment = alignment,
  }, ph);
}

static BmDeviceSize usage(uint32_t heap)
{
  BmPhysicalDeviceMemoryProperties props;
  bmGetPhysicalDeviceMemoryProperties(s_device, &props);
  return props.heapUsage[heap];
}

static void testHeapSizesMatchTheGba(void)
{
  setUp();
  BmPhysicalDeviceMemoryProperties props;
  TEST_ASSERT(bmGetPhysicalDeviceMemoryProperties(s_device, &props) == BM_SUCCESS);
  // The host back end models the GBA memories with their real sizes.
  TEST_ASSERT(props.memoryHeaps[BM_MEMORY_HEAP_EWRAM].size == 256 * 1024);
  TEST_ASSERT(props.memoryHeaps[BM_MEMORY_HEAP_IWRAM].size == 32 * 1024);
  TEST_ASSERT(props.memoryHeaps[BM_MEMORY_HEAP_VRAM].size == 96 * 1024);
}

static void testFreeBlockIsReused(void)
{
  setUp();
  BmDeviceMemoryHandle a, b, c;
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_EWRAM, 1024, 0, &a) == BM_SUCCESS);
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_EWRAM, 120, 0, &b) == BM_SUCCESS);
  TEST_ASSERT(bmFreeMemory(s_device, b) == BM_SUCCESS);
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_EWRAM, 120, 0, &c) == BM_SUCCESS);
  TEST_ASSERT(c->pData == (uint8_t *)a->pData + 1024);
}

static void testFreeMergesNeighbours(void)
{
  setUp();
  BmDeviceMemoryHandle h[3];
  for (int i = 0; i < 3; i++)
  {
    TEST_ASSERT(alloc(BM_MEMORY_HEAP_IWRAM, 1000, 0, &h[i]) == BM_SUCCESS);
  }
  void *first = h[0]->pData;
  TEST_ASSERT(bmFreeMemory(s_device, h[0]) == BM_SUCCESS);
  TEST_ASSERT(bmFreeMemory(s_device, h[2]) == BM_SUCCESS);
  TEST_ASSERT(bmFreeMemory(s_device, h[1]) == BM_SUCCESS);
  TEST_ASSERT(usage(BM_MEMORY_HEAP_IWRAM) == 0);

  // After merging, the whole heap is one block again.
  BmDeviceMemoryHandle all;
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_IWRAM, 32 * 1024, 0, &all) == BM_SUCCESS);
  TEST_ASSERT(all->pData == first);
}

static void testAlignmentIsRelativeToHeapStart(void)
{
  setUp();
  BmDeviceMemoryHandle small, screenBlock;
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_VRAM, 100, 0, &small) == BM_SUCCESS);
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_VRAM, 2048, 2048, &screenBlock) == BM_SUCCESS);
  BmDeviceSize offset = (BmDeviceSize)((uint8_t *)screenBlock->pData - (uint8_t *)small->pData);
  TEST_ASSERT(offset == 2048);
  // The padding in between is still available.
  BmDeviceMemoryHandle filler;
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_VRAM, 1024, 0, &filler) == BM_SUCCESS);
  TEST_ASSERT((uint8_t *)filler->pData < (uint8_t *)screenBlock->pData);
}

static void testOutOfMemoryAndBadArguments(void)
{
  setUp();
  BmDeviceMemoryHandle h;
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_IWRAM, 33 * 1024, 0, &h) == BM_ERROR_OUT_OF_MEMORY);
  TEST_ASSERT(alloc(7, 16, 0, &h) == BM_ERROR_INVALID_ARGUMENT);
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_IWRAM, 16, 3, &h) == BM_ERROR_INVALID_ARGUMENT);
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_IWRAM, 16, 0, &h) == BM_SUCCESS);
  TEST_ASSERT(bmFreeMemory(s_device, h) == BM_SUCCESS);
  TEST_ASSERT(bmFreeMemory(s_device, h) == BM_ERROR_INVALID_ARGUMENT);
}

static void testMapChecksBounds(void)
{
  setUp();
  BmDeviceMemoryHandle h;
  void *p;
  TEST_ASSERT(alloc(BM_MEMORY_HEAP_EWRAM, 64, 0, &h) == BM_SUCCESS);
  TEST_ASSERT(bmMapMemory(s_device, h, 0, 64, &p) == BM_SUCCESS);
  TEST_ASSERT(bmMapMemory(s_device, h, 32, 32, &p) == BM_SUCCESS);
  TEST_ASSERT(bmMapMemory(s_device, h, 32, 33, &p) == BM_ERROR_INVALID_ARGUMENT);
}

int main(void)
{
  RUN_TEST(testHeapSizesMatchTheGba);
  RUN_TEST(testFreeBlockIsReused);
  RUN_TEST(testFreeMergesNeighbours);
  RUN_TEST(testAlignmentIsRelativeToHeapStart);
  RUN_TEST(testOutOfMemoryAndBadArguments);
  RUN_TEST(testMapChecksBounds);
  return TEST_RESULT();
}
