#include "device.h"
#include "device_malloc.h"

#include "pool.h"

#include "type_manager.h"
#include "errors.h"

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

  bmAllocateMemory(device, &(BmMemoryAllocateInfo) {
    .allocationSize = 1*1024,
    .memoryTypeIndex = 0,
  },
  &memory);

  uint32_t *test_ints[10] = {NULL};

  BmTypeHandle type;

  result = bmTypeManagerCreate(&typeManager, &(BmTypeCreateInfo) {
    .name = "uint32_t",
    .size = sizeof(uint32_t),
    .free = free_int,
  },
  &type);

  BmPool pool;
  bmPoolInit(&pool, device, memory, type);

  for (int i = 0; i < 10; i++)
  {
    result = bmPoolAllocate(&pool, (void**)&test_ints[i]);
    *test_ints[i] = i;
    if (result != BM_SUCCESS)
    {
      break;
    }
  }

  if (result != BM_SUCCESS)
  {
    return 1;
  }

  bmPoolFree(&pool, test_ints[5]);
  bmPoolFree(&pool, test_ints[3]);
  uint32_t *test_int_5 = NULL;
  bmPoolAllocate(&pool, (void**)&test_int_5);
  if (test_int_5 == NULL)
  {
    return 1;
  }
  *test_int_5 = 0x12345678;

  result = bmPoolFinalize(&pool);

  if (result != BM_SUCCESS)
  {
    return 1;
  }

  bmFreeMemory(device, memory);

  bmTypeManagerFinalize(&typeManager);

  if (result != BM_SUCCESS)
  {
    return 1;
  }

  return 0;
}