#pragma once

#include "shared.h"
#include "device.h"

BmResult bmCreateTilemap(BmDeviceHandle device, struct BmTilemapCreateInfo *pCreateInfo, struct BmTilemap **ppTilemap);
BmResult bmTilemapSetBlending(struct BmTilemap *pTilemap, bool enable);

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

typedef struct BmBindTilemapInfo
{
  struct BmTilemap *pTilemap;
  BmDeviceMemoryHandle hMemory;
  BmDeviceSize memoryOffset;
} BmBindTilemapInfo;