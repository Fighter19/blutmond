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
#define BM_GAME_PLAYER_HEALTH 30   // the base the stat MaxHealth adds to (first-items.md §1)
#define BM_GAME_PLAYER_MANA 20     // the base the stat MaxMana adds to
#define BM_GAME_MANA_EVERY 90      // frames per point of mana coming back
#define BM_GAME_ELEMENTS 4         // element values 0 (none) to 3; the game's enum names them
#define BM_GAME_ITEM_SLOT 7        // the attachment slot an item being used takes (use_item)
#define BM_GAME_COMMAND_SLOT 6     // the attachment slot a command's behaviour takes (commands.h)

/// While a lock (owner_lock: an attack, player_lock: a scene) holds the player, on the ground:
/// no walking, the speed fades with friction; stopped at once; the speed kept (no friction);
/// walking as usual. The facing stays during an attack's lock.
typedef enum BmLockGround { BM_LOCK_SLIDE = 0, BM_LOCK_STOP, BM_LOCK_KEEP, BM_LOCK_WALK } BmLockGround;
/// In the air: steering as usual; the speed kept (no air drag); no steering, air drag.
typedef enum BmLockAir { BM_LOCK_STEER = 0, BM_LOCK_AIR_KEEP, BM_LOCK_DRIFT } BmLockAir;
#define BM_GAME_NOTICE_FRAMES 120  // how long an obtained item is shown
#define BM_GAME_MUSIC_FADE 120     // frames the music takes to fade away in a room with music "none"
#define BM_GAME_PORTRAIT_X 8       // where a speaker's portrait (32 x 32) is, in the dialogue box
#define BM_GAME_PORTRAIT_Y 124
#define BM_GAME_PORTRAIT_CELLS 5   // the name and the line start this many cells further right

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
  int16_t mana;                    // the player's; spent by spells, back 1 every 90 frames
  int16_t speedPercent;            // added to the player's running speed (add_speed: heavy armour)
  uint8_t resists[BM_GAME_ELEMENTS];   // per element: what the player wears that resists it
  uint8_t striker;                 // the figure striking the player right now (hits.h), else 0xFF
  uint8_t returnPending;           // bmGameReturnToSave asked for: done at the next frame's start
  // How the player moves while a lock holds them (lock_move; BmLockGround, BmLockAir).
  uint8_t lockGround, lockAir;
  // A door with a panel being walked through (effects.h): its object + 1 (0 none) and the
  // frames so far; the door just come through, closing behind the player, and frames left.
  uint8_t doorSeq, doorTimer, doorArrive, doorArriveTimer;
  // Commands (commands.h): the directions as they changed (numpad), when (frame, low 16 bits).
  uint8_t cmdDirs[8];
  uint16_t cmdTimes[8];
  uint8_t cmdHead, cmdCount;
  uint32_t cmdLast;                // the last command done, and how many were
  uint16_t cmdDone;
  // Screen effects and the teleport (effects.h).
  uint8_t fxKind;
  int16_t fxA, fxB;
  uint8_t teleTimer;               // frames left of a teleport, 0 none
  uint32_t teleRoom;
  int16_t teleX, teleY;
  uint8_t fxApplied;               // line-table slots the effects set (not state: video)
  // The HUD (top left: health and mana, while no screen is open) and the notice of an item
  // obtained (bottom left, for a while). What it shows is drawn from the state; only the
  // notice's text and time are state.
  uint32_t noticeKey;              // the record obtained (its title is looked up when drawn)
  uint16_t noticeText;             // string id of its name if it has no title (0xFFFF: none)
  uint8_t noticeCount;
  bool noticeTitled;               // noticeText is the record's title (else a name, tidied)
  uint16_t noticeTimer;            // frames it is still shown
  bool hudHidden;                  // the debug overlay covers its place
  bool hudDirty;                   // the layer was cleared: draw it whole
  bool noticeDrawn;
  uint8_t noticeWidth;             // cells of the notice on screen, 0 none
  int16_t hudShown[4];             // health, its most, mana, its most as last drawn
  // Where the last save point was (the Return Feather goes there).
  uint32_t savedRoom;
  int32_t savedX, savedY;
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
  uint8_t objectDoorSide[BM_GAME_MAX_OBJECTS]; // a door's direction: 0 north (Up goes through), 1 east, 2 west, 3 other
  int16_t objectDoorGround[BM_GAME_MAX_OBJECTS]; // and the ground it stands on (y)
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
  uint16_t dialoguePortrait;                // the speaker's sprite (its name's string id), 0xFFFF none
  uint16_t dialogueVoice;                   // the speaker's voice: a sound effect's name, 0xFFFF none
  uint8_t dialogueShown, dialogueLength;    // letters of the line shown so far, and in all
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

/// The dialogue box (say, the bottom five rows of the UI layer; until A is pressed): the
/// speaker's name and a line of up to two rows (string ids, in the language chosen), and the
/// speaker's portrait, a sprite of the pack by its name's string id (0xFFFF: none), at the box's left.
void bmGameSay(BmGame *pGame, uint16_t speaker, uint16_t line);
void bmGameSayWith(BmGame *pGame, uint16_t speaker, uint16_t line, uint16_t portrait);
/// The same with the speaker's voice (a sound effect's name, string id; 0xFFFF none), heard
/// with every second letter as the line appears, one letter a frame; A shows the rest at once.
void bmGameSpeak(BmGame *pGame, uint16_t speaker, uint16_t line, uint16_t portrait, uint16_t voice);
/// Draws the dialogue box again as far as its line has appeared (after the layer was cleared).
void bmGameDrawDialogue(BmGame *pGame);

/// say_line: a record with the fields `speaker` (a record with `name` and `portrait`) and
/// `says` (docs/plans/dialogue.md §2: speakers and lines as records).
void bmGameSayLine(BmGame *pGame, uint32_t line);

/// A field of a record (of whichever table) by the field's name; false if there is none.
bool bmGameRecordField(const BmGame *pGame, uint32_t key, const char *name, int32_t *pValue);

/// Hurts the player as a hazard does (unless still blinking from the last hit).
void bmGameHurtPlayer(BmGame *pGame, int32_t damage);

/// Strikes the player for `amount` from room x `fromX` (fix: they are pushed away from it),
/// unless still blinking: health, the blink, the push, the end of the player's hits and
/// hit(amount) to their attachments. At 0 health they start again with all of it. False if
/// they could not be struck.
bool bmGameStrikePlayer(BmGame *pGame, int32_t amount, int32_t fromX);

/// The same from figure `striker` (hits.h; 0xFF: none) with an element: the player's
/// resistance to it halves what is left after defence (at least 1).
bool bmGameStrikePlayerWith(BmGame *pGame, int32_t amount, int32_t fromX, uint8_t element, uint8_t striker);

/// The player's most mana: 20 plus the stat MaxMana.
int32_t bmGameMaxMana(const BmGame *pGame);

/// Brings the player to the last save point (the room is entered anew), the menus closed: at
/// the start of the next frame, so that a menu's script that asked for it finishes first.
void bmGameReturnToSave(BmGame *pGame);

/// The notice at the bottom left: `count` of the record `key` were obtained (its `title`, or
/// the string `nameId` if it has none), shown for BM_GAME_NOTICE_FRAMES.
void bmGameNotice(BmGame *pGame, uint32_t key, uint16_t nameId, int32_t count);

/// Puts the player at (x, y) of room `roomIndex` (entered fresh if it is another), as the middle
/// of a teleport does.
void bmGameMovePlayer(BmGame *pGame, uint32_t roomIndex, int32_t x, int32_t y);

/// Uses the item on quick slot `slot` (0: L, 1: Select) if the player has one.
bool bmGameUseQuick(BmGame *pGame, uint8_t slot);

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

/// Shows texts in `language` from now on and keeps it as an option in the save memory.
void bmGameSetLanguage(BmGame *pGame, uint8_t language);

/// Save game in the save memory: the `save` fields. Loading returns false if there is none
/// or it was written by a different layout.
bool bmGameWriteSaveGame(BmGame *pGame);
bool bmGameReadSaveGame(BmGame *pGame);
