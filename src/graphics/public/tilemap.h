#pragma once

#include "shared.h"
#include "device.h"

typedef enum BmTilemapUsageFlagBits {
  /// Indicates that the tilemap can be used as a source for copy operations
  BM_TILEMAP_USAGE_TRANSFER_SRC_BIT = 0x00000001,
  /// Indicates that the tilemap can be used as a destination for copy operations
  BM_TILEMAP_USAGE_TRANSFER_DST_BIT = 0x00000002,
  /// Indicates that the tilemap can be used as a background image
  BM_TILEMAP_USAGE_BACKGROUND_BIT = 0x00000004,
  /// Indicates that the tilemap can be used as a sprite image
  BM_TILEMAP_USAGE_SPRITE_BIT = 0x00000008,
} BmTilemapUsageFlagBits;

typedef BmFlags BmTilemapUsageFlags;

typedef struct BmTilemapCreateInfo
{
  // Allow for extension of this structure
  // in a way that is backwards compatible
  // on an ABI level
  BmStructureType sType;
  const void *pNext;

  BmTilemapUsageFlags usage;
  /// The 3D size of the tilemap
  // Set depth to 1 for 2D tilemaps
  // A 3D size is used in case we ever want to create an isometric game
  // or a game like Minecraft
  BmExtent3D extent;
} BmTilemapCreateInfo;

typedef struct BmTilemap BmTilemap;

typedef struct BmBindTilemapInfo
{
  BmTilemap *pTilemap;
  BmDeviceMemoryHandle hMemory;
  BmDeviceSize memoryOffset;
} BmBindTilemapInfo;

/// Maximum number of tilemap objects that exist at the same time.
#define BM_MAX_TILEMAPS 16

BmResult bmCreateTilemap(BmDeviceHandle device, const BmTilemapCreateInfo *pCreateInfo, BmTilemap **ppTilemap);
BmResult bmDestroyTilemap(BmDeviceHandle device, BmTilemap *pTilemap);
BmResult bmBindTilemapMemory(BmDeviceHandle device, uint32_t bindInfoCount, const BmBindTilemapInfo *pBindInfos);
BmResult bmTilemapSetBlending(BmTilemap *pTilemap, bool enable);
