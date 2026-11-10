/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Rooms from a pack (docs/spec/room-format.md section 7): shows the graphics layers and
// streams their maps into the hardware maps as the camera moves, so rooms can be larger than
// the 512x256 pixels a hardware map holds.

#include "shared.h"
#include "pack.h"

#define BM_ROOM_MAX_LAYERS 4

typedef enum BmRoomRole {
  BM_ROOM_ROLE_BACKGROUND = 0,
  BM_ROOM_ROLE_MAIN = 1,
  BM_ROOM_ROLE_FOREGROUND = 2,
} BmRoomRole;

typedef struct BmRoomLayer
{
  uint8_t role;
  uint8_t priority;
  uint8_t bg;             // hardware background it is shown on
  uint16_t width8, height8;
  const uint8_t *pEntries; // map entries in the pack (little endian u16)
  int32_t parallaxX, parallaxY;  // fix 24.8

  // Streaming state: the window of map cells currently in the hardware map.
  bool loaded;
  int32_t windowX, windowY;
} BmRoomLayer;

typedef struct BmRoom
{
  const BmPack *pPack;
  uint16_t widthPx, heightPx;
  uint8_t collisionCell;
  uint16_t flags;
  int32_t gravity, maxFallSpeed, waterGravity, waterMaxFallSpeed;
  uint16_t objectCount;
  uint16_t tilesChunk, paletteChunk, collisionChunk, objectsChunk;
  uint8_t layerCount;
  BmRoomLayer layers[BM_ROOM_MAX_LAYERS];
  int32_t cameraX, cameraY;
} BmRoom;

/// Number of rooms (ROOM chunks) in a pack.
uint32_t bmRoomCount(const BmPack *pPack);

/// Reads the `index`-th room of a pack. Nothing is shown yet.
BmResult bmRoomOpen(BmRoom *pRoom, const BmPack *pPack, uint32_t index);

/// Uploads the room's palettes and tiles, sets up its backgrounds and shows it with the camera
/// at (x, y) (top-left corner of the screen, clamped to the room).
BmResult bmRoomShow(BmRoom *pRoom, int32_t cameraX, int32_t cameraY);

/// Moves the camera: scrolls the backgrounds and streams map cells that came into view.
void bmRoomSetCamera(BmRoom *pRoom, int32_t cameraX, int32_t cameraY);
