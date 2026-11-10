#include "shared.h"

#include "device.h"
#include "device_private.h"
#include "device_heaps.h"
#include "physical_device.h"

static BmResult createDevice(BmPhysicalDeviceHandle hPhysicalDevice, BmDeviceHandle *phDevice)
{
  (void)hPhysicalDevice;
  *phDevice = bmDeviceHeapsCreate();
  return *phDevice != NULL ? BM_SUCCESS : BM_ERROR_OUT_OF_MEMORY;
}

static BmPhysicalDevice s_physicalDevice = {
  .name = "platform",
  .pfnCreateDevice = createDevice,
};

BmPhysicalDeviceHandle bmGetPhysicalDevice(void)
{
  return &s_physicalDevice;
}
