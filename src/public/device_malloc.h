#pragma once

typedef struct BmDevice *BmDeviceHandle;

BmDeviceHandle bmDeviceMallocCreate();
BmResult bmDeviceMallocDestroy(BmDeviceHandle device);