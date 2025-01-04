#include "device.h"
#include "device_malloc.h"

#include "pool.h"

#include "type_manager.h"
#include "errors.h"

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

  uint32_t test_int;

  BmTypeHandle type;

  result = bmTypeManagerCreate(&typeManager, &(BmTypeCreateInfo) {
    .size = sizeof(test_int),
  },
  &type);

  BmPool pool;
  bmPoolInit(&pool, device, memory, type);

  bmPoolAllocate(&pool, (void**)&test_int);

  bmPoolFinalize(&pool);

  bmFreeMemory(device, memory);

  bmTypeManagerFinalize(&typeManager);

  if (result != BM_SUCCESS)
  {
    return 1;
  }

  return 0;
}