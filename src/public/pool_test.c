#include "device.h"
#include "device_malloc.h"

#include "pool.h"

#include "type_manager.h"
#include "errors.h"

#ifdef UNIX
#include <signal.h>

static void bmBreakpoint()
{
  raise(SIGTRAP);
}
#else
static void bmBreakpoint()
{
}
#endif

#ifdef UNIX
#include <stdio.h>

static void bmPrintTestError(const char *expression, const char *file, int line)
{
  printf("Test failed, expression: %s in %s:%d\n", expression, file, line);
}
#else
#define bmPrintTestError(expression, file, line)
#endif

#define TEST_ASSERT(x) if (!(x)) { bmPrintTestError(#x, __FILE__, __LINE__); bmBreakpoint(); return 1; }

static void free_int(void *data)
{
  uint32_t *pInt = (uint32_t*)data;
  *pInt = 0xDDDDDDDD;
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  BmResult result;

  BmDeviceHandle device;
  device = bmDeviceMallocCreate();

  BmTypeManager typeManager;
  bmTypeManagerInit(&typeManager);

  BmDeviceMemoryHandle memory;

  result = bmAllocateMemory(device, &(BmMemoryAllocateInfo) {
    .allocationSize = 1*1024,
    .memoryTypeIndex = 0,
  },
  &memory);
  TEST_ASSERT(result == BM_SUCCESS);

  uint32_t *test_ints[10] = {NULL};

  BmTypeHandle type;

  result = bmTypeManagerCreate(&typeManager, &(BmTypeCreateInfo) {
    .name = "uint32_t",
    .size = sizeof(uint32_t),
    .free = free_int,
  },
  &type);
  TEST_ASSERT(result == BM_SUCCESS);

  BmPool pool;
  result = bmPoolInit(&pool, device, memory, type);
  TEST_ASSERT(result == BM_SUCCESS);

  for (int i = 0; i < 10; i++)
  {
    result = bmPoolAllocate(&pool, (void**)&test_ints[i]);
    *test_ints[i] = i;
    if (result != BM_SUCCESS)
    {
      break;
    }
  }
  TEST_ASSERT(result == BM_SUCCESS);

  result = bmPoolFree(&pool, test_ints[5]);
  TEST_ASSERT(result == BM_SUCCESS);
  result = bmPoolFree(&pool, test_ints[3]);
  TEST_ASSERT(result == BM_SUCCESS);
  uint32_t *test_int_3 = NULL;
  uint32_t *test_int_5 = NULL;
  result = bmPoolAllocate(&pool, (void**)&test_int_3);
  TEST_ASSERT(result == BM_SUCCESS);
  TEST_ASSERT(test_int_3 != NULL);
  *test_int_3 = 0x12345678;

  result = bmPoolAllocate(&pool, (void**)&test_int_5);
  TEST_ASSERT(result == BM_SUCCESS);

  TEST_ASSERT(test_int_5 != NULL);
  TEST_ASSERT(test_ints[3] == test_int_3);

  result = bmPoolFinalize(&pool);
  TEST_ASSERT(result == BM_SUCCESS);

  result = bmFreeMemory(device, memory);
  TEST_ASSERT(result == BM_SUCCESS);

  bmTypeManagerFinalize(&typeManager);

  return 0;
}