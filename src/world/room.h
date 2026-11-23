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

// Each layer takes two of the screen blocks 26-31 for its map.
#define BM_ROOM_MAX_LAYERS 3
// Holes: parts of the main and foreground layers shown empty (a wall broken, game.h).
#define BM_ROOM_MAX_HOLES 8

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
  const uint8_t *pEntries; // map entries (little endian u16): in the pack, or unpacked
  int32_t parallaxX, parallaxY;  // fix 24.8

  // Streaming state: the window of map cells currently in the hardware map.
  bool loaded;
  int32_t windowX, windowY;
  int16_t scrollX;        // the horizontal scroll last set (effects.h: a wave builds on it)
} BmRoomLayer;

typedef struct BmRoom
{
  const BmPack *pPack;
  uint16_t widthPx, heightPx;
  uint8_t collisionCell;
  uint16_t flags;
  int32_t gravity, maxFallSpeed, waterGravity, waterMaxFallSpeed;
  uint16_t objectCount;
  uint16_t musicId;          // string id of the room's song, 0xFFFF: none
  uint16_t tilesChunk, paletteChunk, collisionChunk, objectsChunk;
  const uint8_t *pCollision;   // the COLL chunk's data: in the pack, or unpacked
  uint32_t collisionSize;
  uint8_t layerCount;
  BmRoomLayer layers[BM_ROOM_MAX_LAYERS];
  int32_t cameraX, cameraY;
  // Holes in map cells of 8 pixels: x0, y0, x1, y1 (x1, y1 not in it). Not in the pack: the
  // game makes them (bmRoomAddHole) and makes them again when it shows the room anew.
  uint8_t holeCount;
  int16_t holes[BM_ROOM_MAX_HOLES][4];
} BmRoom;

/// Number of rooms (ROOM chunks) in a pack.
uint32_t bmRoomCount(const BmPack *pPack);

/// Reads the `index`-th room of a pack. Nothing is shown yet. Its compressed maps and
/// collision are unpacked into one buffer of BM_PACK_ROOM_UNPACK_BYTES that all rooms opened
/// so share: the room opened before it is not to be used any more.
BmResult bmRoomOpen(BmRoom *pRoom, const BmPack *pPack, uint32_t index);

/// The same, unpacking into `pBuffer` (`size` bytes, 4-byte aligned), which must stay while
/// the room is used: for a room opened beside the game's (the editor's preview).
BmResult bmRoomOpenWith(BmRoom *pRoom, const BmPack *pPack, uint32_t index, uint8_t *pBuffer, uint32_t size);

/// Uploads the room's palettes and tiles, sets up its backgrounds and shows it with the camera
/// at (x, y) (top-left corner of the screen, clamped to the room).
BmResult bmRoomShow(BmRoom *pRoom, int32_t cameraX, int32_t cameraY);

/// A hole of (x, y, w, h) room pixels (to whole 8 pixel cells) in the main and foreground
/// layers: their cells there are shown empty from now on, also where they are on screen now.
/// False when the room has BM_ROOM_MAX_HOLES already.
bool bmRoomAddHole(BmRoom *pRoom, int32_t x, int32_t y, int32_t w, int32_t h);

/// Moves the camera: scrolls the backgrounds and streams map cells that came into view.
void bmRoomSetCamera(BmRoom *pRoom, int32_t cameraX, int32_t cameraY);
