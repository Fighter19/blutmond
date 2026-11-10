#include "device.h"
#include "device_heaps.h"
#include "physical_device.h"
#include "pool.h"
#include "type_manager.h"

#include "test.h"

static BmDeviceHandle s_device;
static BmTypeManager s_typeManager;
static int s_freed;

static void freeInt(void *data)
{
  *(uint32_t *)data = 0xDDDDDDDD;
  s_freed++;
}

static void setUp(void)
{
  bmCreateDevice(bmGetPhysicalDevice(), &s_device);
  bmTypeManagerInit(&s_typeManager);
  s_freed = 0;
}

static BmResult makePool(BmPool *pool, size_t capacity)
{
  BmTypeHandle type;
  BmResult result = bmTypeManagerCreate(&s_typeManager, &(BmTypeCreateInfo) {
    .name = "uint32_t", .size = sizeof(uint32_t), .free = freeInt,
  }, &type);
  if (result != BM_SUCCESS) return result;

  BmDeviceSize size = (BmDeviceSize)bmPoolRequiredSize(sizeof(uint32_t), capacity);
  BmDeviceMemoryHandle memory;
  result = bmAllocateMemory(s_device, &(BmMemoryAllocateInfo) {
    .allocationSize = size, .memoryTypeIndex = BM_MEMORY_HEAP_EWRAM,
  }, &memory);
  if (result != BM_SUCCESS) return result;
  return bmPoolInit(pool, s_device, memory, size, type);
}

static void testAllocateFreeReuse(void)
{
  setUp();
  BmPool pool;
  TEST_ASSERT(makePool(&pool, 100) == BM_SUCCESS);
  TEST_ASSERT(pool.elementCapacity == 100);

  uint32_t *ints[10] = {0};
  for (int i = 0; i < 10; i++)
  {
    TEST_ASSERT(bmPoolAllocate(&pool, (void **)&ints[i]) == BM_SUCCESS);
    *ints[i] = (uint32_t)i;
  }
  TEST_ASSERT(pool.elementCount == 10);

  TEST_ASSERT(bmPoolFree(&pool, ints[5]) == BM_SUCCESS);
  TEST_ASSERT(bmPoolFree(&pool, ints[3]) == BM_SUCCESS);
  TEST_ASSERT(s_freed == 2);

  // The most recently freed element comes back first.
  uint32_t *a = NULL, *b = NULL;
  TEST_ASSERT(bmPoolAllocate(&pool, (void **)&a) == BM_SUCCESS);
  TEST_ASSERT(bmPoolAllocate(&pool, (void **)&b) == BM_SUCCESS);
  TEST_ASSERT(a == ints[3]);
  TEST_ASSERT(b == ints[5]);
  TEST_ASSERT(*a == 0);  // elements are cleared on allocation

  // Other elements kept their values.
  TEST_ASSERT(*ints[4] == 4 && *ints[9] == 9);
}

static void testDoubleFreeAndForeignPointer(void)
{
  setUp();
  BmPool pool;
  TEST_ASSERT(makePool(&pool, 8) == BM_SUCCESS);
  uint32_t *p = NULL;
  TEST_ASSERT(bmPoolAllocate(&pool, (void **)&p) == BM_SUCCESS);
  TEST_ASSERT(bmPoolFree(&pool, p) == BM_SUCCESS);
  TEST_ASSERT(bmPoolFree(&pool, p) == BM_ERROR_INVALID_ARGUMENT);

  uint32_t local = 0;
  TEST_ASSERT(bmPoolFree(&pool, &local) == BM_ERROR_INVALID_ARGUMENT);
}

static void testCapacity(void)
{
  setUp();
  BmPool pool;
  TEST_ASSERT(makePool(&pool, 4) == BM_SUCCESS);
  void *p;
  for (int i = 0; i < 4; i++)
  {
    TEST_ASSERT(bmPoolAllocate(&pool, &p) == BM_SUCCESS);
  }
  TEST_ASSERT(bmPoolAllocate(&pool, &p) == BM_ERROR_OUT_OF_MEMORY);
}

static void testFinalizeFreesOnlyUsedElements(void)
{
  setUp();
  BmPool pool;
  TEST_ASSERT(makePool(&pool, 16) == BM_SUCCESS);
  void *p[6];
  for (int i = 0; i < 6; i++)
  {
    TEST_ASSERT(bmPoolAllocate(&pool, &p[i]) == BM_SUCCESS);
  }
  TEST_ASSERT(bmPoolFree(&pool, p[1]) == BM_SUCCESS);
  TEST_ASSERT(bmPoolFree(&pool, p[4]) == BM_SUCCESS);
  s_freed = 0;
  TEST_ASSERT(bmPoolFinalize(&pool) == BM_SUCCESS);
  TEST_ASSERT(s_freed == 4);
  TEST_ASSERT(pool.elementCount == 0);
}

int main(void)
{
  RUN_TEST(testAllocateFreeReuse);
  RUN_TEST(testDoubleFreeAndForeignPointer);
  RUN_TEST(testCapacity);
  RUN_TEST(testFinalizeFreesOnlyUsedElements);
  return TEST_RESULT();
}
