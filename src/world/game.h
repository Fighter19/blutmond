/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The platformer runtime that the engine classes need before scripting exists
// (docs/spec/room-format.md section 5): the player with PlatformerControl, the camera with
// CameraZone, KillZone respawns, Door room changes and placeholders for other classes.

#include "body.h"
#include "collision.h"
#include "pack.h"
#include "room.h"
#include "state.h"

#define BM_GAME_MAX_PLACEHOLDERS 24
#define BM_GAME_MAX_OBJECTS 64

typedef enum BmObjectKind {
  BM_OBJECT_OTHER = 0,   // a game class (placeholder until scripts exist)
  BM_OBJECT_PLAYER_START,
  BM_OBJECT_CAMERA_ZONE,
  BM_OBJECT_KILL_ZONE,
  BM_OBJECT_DOOR,
  BM_OBJECT_TRIGGER,
  BM_OBJECT_SAVE_POINT,
} BmObjectKind;

typedef struct BmObjectView
{
  const char *className;
  const char *name;
  uint16_t persistentId;
  int16_t x, y;
  uint16_t w, h;
  uint16_t propertyCount;
  const uint8_t *pProperties;  // pairs of u16 string ids
  uint8_t kind;                // BmObjectKind
} BmObjectView;

typedef struct BmGame
{
  const BmPack *pPack;
  BmRoom room;
  uint32_t roomIndex;
  BmCollision collision;
  BmRoomPhysics physics;
  BmBody player;
  BmBodyParams playerParams;
  int32_t zoneX0, zoneY0, zoneX1, zoneY1;  // camera zone the player is in
  uint32_t frame;
  uint16_t keysPrevious;
  uint16_t hurtTimer;
  uint16_t doorCooldown;
  uint32_t deaths, roomChanges, hurts, saves;
  uint16_t saveCooldown;
  // Objects of the current room, read once when the room is entered.
  uint16_t objectCount;
  BmObjectView objects[BM_GAME_MAX_OBJECTS];
} BmGame;

/// Number of objects in the current room and access to one of them.
uint16_t bmGameObjectCount(const BmGame *pGame);
bool bmGameObject(const BmGame *pGame, uint16_t index, BmObjectView *pObject);
/// Value of an object property, or NULL.
const char *bmGameObjectProperty(const BmGame *pGame, const BmObjectView *pObject, const char *key);

/// Index of the room with the given id, or -1.
int32_t bmGameFindRoom(const BmPack *pPack, const char *roomId);

/// Starts in the given room (NULL: the first room), at its PlayerStart. The video state is set
/// up (room layers, sprites). `pPack` must stay valid.
BmResult bmGameStart(BmGame *pGame, const BmPack *pPack, const char *roomId);

/// One frame of game logic for the buttons held (BM_KEY_*), then camera and sprites.
void bmGameFrame(BmGame *pGame, uint16_t keys);

/// Id of the current room.
const char *bmGameRoomId(const BmGame *pGame);

/// The game's state as a schema block (docs/spec/state.md): room, player, counters.
BmStateBlock bmGameStateBlock(BmGame *pGame);

/// After the state was read: enters the stored room again and rebuilds everything derived
/// (collision, objects, camera, streamed maps, sprites) without moving the player.
BmResult bmGameAfterLoad(BmGame *pGame);

/// Save game in the save memory: the `save` fields. Loading returns false if there is none
/// or it was written by a different layout.
bool bmGameWriteSaveGame(BmGame *pGame);
bool bmGameReadSaveGame(BmGame *pGame);
