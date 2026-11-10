#include "shared.h"
#include "device.h"

#include "device_private.h"

BmResult bmCreateDevice(
    BmPhysicalDeviceHandle hPhysicalDevice,
    BmDeviceHandle *phDevice)
{
  return hPhysicalDevice->pfnCreateDevice(hPhysicalDevice, phDevice);
}

BmResult bmDestroyDevice(
    BmDeviceHandle hDevice)
{
  return hDevice->pfnDestroyDevice(hDevice);
}
// Default device and memory for code that has no device passed in; set by the application.
BmDeviceHandle g_defaultDevice = NULL;
BmDeviceMemoryHandle g_defaultMemory = NULL;
