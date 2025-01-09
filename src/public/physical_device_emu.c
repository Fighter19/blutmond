#include "shared.h"

#include "device.h"
#include "device_private.h"

#include "errors.h"

#include "device_malloc.h"

#include "physical_device_emu.h"

static BmPhysicalDevice g_emuDevice;

static BmResult bmDevicePhysicalEmuCreateDevice(BmPhysicalDeviceHandle hPhysicalDevice, BmDeviceHandle *phDevice)
{
  (void)hPhysicalDevice;
  *phDevice = bmDeviceMallocCreate();

  return BM_SUCCESS;
}

BmPhysicalDeviceHandle bmCreateDevicePhysicalEmu()
{
  g_emuDevice.name = "emu";
  g_emuDevice.pfnCreateDevice = bmDevicePhysicalEmuCreateDevice;

  return &g_emuDevice;
}
