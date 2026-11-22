/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The platformer runtime that the engine classes need before scripting exists
// (docs/spec/room-format.md section 5): the player with PlatformerControl, the camera with
// CameraZone, KillZone respawns, Door room changes and placeholders for other classes.

#include "attachments.h"
#include "screens.h"
#include "inventory.h"
#include "actors.h"
#include "body.h"
#include "collision.h"
#include "hits.h"
#include "pack.h"
#include "room.h"
#include "sound.h"
#include "sprite.h"
#include "state.h"
#include "ui.h"

#define BM_GAME_MAX_OBJECTS 64
#define BM_GAME_MAX_SHEETS 8
#define BM_GAME_MAX_OAM 48
#define BM_GAME_PLAYER_HEALTH 30   // until the stat MaxHealth gives it (first-items.md §1)

/// Animations the engine's player looks for in the sprite called "player".
enum { BM_PLAYER_ANIM_IDLE, BM_PLAYER_ANIM_RUN, BM_PLAYER_ANIM_JUMP, BM_PLAYER_ANIM_FALL, BM_PLAYER_ANIM_CLIMB, BM_PLAYER_ANIM_COUNT };

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
  uint16_t lockTimer;              // frames the player cannot walk or jump (owner_lock)
  uint8_t animHeld;                // 1: a script's animation plays (owner_play_anim); 2: until the lock ends
  int16_t health;                  // the player's; at 0 they start again at the last safe place
  uint16_t doorCooldown;
  uint32_t deaths, roomChanges, hurts, saves;
  uint16_t saveCooldown;
  // Objects of the current room, read once when the room is entered.
  uint16_t objectCount;
  BmObjectView objects[BM_GAME_MAX_OBJECTS];
  // Sprite sheets of the pack, loaded into sprite memory when a room is entered.
  const uint8_t *pSpritesFrom;                 // the pack they were loaded from
  uint32_t spritesCrc;
  uint8_t sheetCount;
  BmSpriteSheet sheets[BM_GAME_MAX_SHEETS];
  int8_t playerSheet;                          // -1: the built-in figure
  int8_t playerAnims[BM_PLAYER_ANIM_COUNT];
  int8_t objectSheet[BM_GAME_MAX_OBJECTS];     // sheet named like the object's class, or -1
  uint16_t builtinTile;                        // where the built-in graphics start
  BmAnimState playerAnim;
  // Scripted objects of the room (docs/spec/mondskript.md).
  BmActors actors;
  // What the player carries and wears (docs/plans/structures.md §7).
  BmInventory inventory;
  // Scripts attached to the player (docs/plans/attachable-scripts.md).
  BmAttachments attachments;
  // Hits armed by scripts (docs/plans/hitboxes.md).
  BmHits hits;
  // Menus and other screens (screens.h); while one is open the room stands still.
  BmScreens screens;
  // The dialogue box (say): shown until A is pressed.
  uint8_t dialogue;
  uint16_t dialogueSpeaker, dialogueLine;   // string ids
  // The UI layer (src/ui/ui.h): menus, dialogue, the HUD, the debug overlay. What it shows is
  // presentation, not game state.
  BmUi ui;
  // The language texts are shown in (src/ui/text.h): an option kept in the save memory apart
  // from the save game.
  uint8_t language;
  // Music and sound effects. Not part of the game state yet: a loaded state starts the room's
  // song from its beginning.
  BmSound sound;
  const uint8_t *pSoundFrom;
} BmGame;

/// Hurts the player as a hazard does (unless still blinking from the last hit).
void bmGameHurtPlayer(BmGame *pGame, int32_t damage);

/// Strikes the player for `amount` from room x `fromX` (fix: they are pushed away from it),
/// unless still blinking: health, the blink, the push, the end of the player's hits and
/// hit(amount) to their attachments. At 0 health they start again with all of it. False if
/// they could not be struck.
bool bmGameStrikePlayer(BmGame *pGame, int32_t amount, int32_t fromX);

/// Plays the player's animation named `name` (string id) for a script: the engine does not
/// choose another until the lock is over (or, without a lock, until it has finished).
void bmGamePlayPlayerAnim(BmGame *pGame, uint16_t name);

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

/// The player's most health: 30 + the stat MaxHealth (inventory.h).
int32_t bmGameMaxHealth(const BmGame *pGame);

/// Shows the dialogue box (the bottom six rows of the UI layer) with a speaker's name and a line
/// (string ids, shown in the language chosen). It stays until A is pressed.
void bmGameSay(BmGame *pGame, uint16_t speaker, uint16_t line);

/// Shows texts in `language` from now on and keeps it as an option in the save memory.
void bmGameSetLanguage(BmGame *pGame, uint8_t language);

/// Save game in the save memory: the `save` fields. Loading returns false if there is none
/// or it was written by a different layout.
bool bmGameWriteSaveGame(BmGame *pGame);
bool bmGameReadSaveGame(BmGame *pGame);
