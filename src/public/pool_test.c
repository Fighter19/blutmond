#include "device.h"
#include "device_malloc.h"

#include "pool.h"

#include "type_manager.h"

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  BmDeviceHandle device;
  device = bmDeviceMallocCreate();

  BmTypeManager typeManager;
  bmTypeManagerInitialize(&typeManager);

  BmDeviceMemoryHandle memory;

  bmAllocateMemory(device, &(BmMemoryAllocateInfo) {
    .allocationSize = 1*1024,
    .memoryTypeIndex = 0,
  },
  &memory);

  bmFreeMemory(device, memory);


  bmTypeManagerFinalize(&typeManager);
  return 0;
}