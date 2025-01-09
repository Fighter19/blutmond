#pragma once
#include "shared.h"
#include "memory.h"

typedef BmResult (*PFN_bmAllocateMemory)(
    struct BmDevice *device,
    const BmMemoryAllocateInfo *pAllocateInfo,
    BmDeviceMemoryHandle *phMemory);

typedef BmResult (*PFN_bmMapMemory)(
    struct BmDevice *device,
    BmDeviceMemoryHandle hMemory,
    BmDeviceSize offset,
    BmDeviceSize size,
    void **ppData);

typedef BmResult (*PFN_bmFreeMemory)(
    struct BmDevice *device,
    BmDeviceMemoryHandle hMemory);

typedef BmResult (*PFN_bmCreateDevice)(
    BmPhysicalDeviceHandle hPhysicalDevice,
    BmDeviceHandle *phDevice);

typedef BmResult (*PFN_bmDestroyDevice)(
    BmDeviceHandle hDevice);

BmResult bmCreateDevice(
    BmPhysicalDeviceHandle hPhysicalDevice,
    BmDeviceHandle *phDevice);

BmResult bmDestroyDevice(
    BmDeviceHandle hDevice);