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