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
#include "map.h"
#include "actors.h"
#include "body.h"
#include "collision.h"
#include "hits.h"
#include "pack.h"
#include "room.h"
#include "puzzle.h"
#include "grapple.h"
#include "figure.h"
#include "sound.h"
#include "sprite.h"
#include "state.h"
#include "ui.h"

#define BM_GAME_MAX_OBJECTS 64
#define BM_GAME_MAX_SHEETS 24   // sprites in a pack (their data; what is in sprite memory: bmGameSheetBank)
#define BM_GAME_BANKS 13        // sprite palette banks given out (13: hit flash, 14-15: built in)
#define BM_GAME_OBJ_TILES 1024  // sprite memory, in tiles
#define BM_GAME_NO_TILES 0xFFFF
#define BM_GAME_MAX_OAM 48
#define BM_GAME_MAX_OVERRIDES (BM_ROOM_MAX_HOLES + BM_PUZZLE_GATES)
#define BM_GAME_EFFECTS 8      // effects shown at once: sparks (a blocked hit), debris (a broken wall)
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
enum { BM_PLAYER_ANIM_IDLE, BM_PLAYER_ANIM_RUN, BM_PLAYER_ANIM_JUMP, BM_PLAYER_ANIM_FALL, BM_PLAYER_ANIM_CLIMB, BM_PLAYER_ANIM_PUSH, BM_PLAYER_ANIM_COUNT };

typedef enum BmObjectKind {
  BM_OBJECT_OTHER = 0,   // a game class (placeholder until scripts exist)
  BM_OBJECT_PLAYER_START,
  BM_OBJECT_CAMERA_ZONE,
  BM_OBJECT_KILL_ZONE,
  BM_OBJECT_DOOR,
  BM_OBJECT_TRIGGER,
  BM_OBJECT_SAVE_POINT,
  BM_OBJECT_BREAKABLE,   // a wall or floor that a hit breaks (bmGameBreak)
  BM_OBJECT_PUZZLE,      // PushBlock, PressurePlate, Gate (puzzle.h draws them)
  BM_OBJECT_GRAPPLE_ANCHOR,   // a point the grapple bites (grapple.h draws it)
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
  uint8_t hitStop;                 // frames the game holds still after a hit with #Stop struck
  int32_t strikeKnockX, strikeKnockY;   // the push of the hit striking the player now (not state)
  int32_t filterAmount;            // a damage being filtered (damage_in, damage_out), and its
  uint8_t filterElement;           // element: only while the filters run, not state
  uint8_t returnPending;           // bmGameReturnToSave asked for: done at the next frame's start
  // How the player moves while a lock holds them (lock_move; BmLockGround, BmLockAir).
  uint8_t lockGround, lockAir;
  uint8_t pickupSound;             // an item was obtained: the sound "pickup" at the next frame's start
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
  // The world map (map.h): the cells explored and the rooms revealed (save game), and what the
  // map's view of the pack and the HUD's small map are (not state).
  uint8_t mapExplored[BM_MAP_MAX_CELLS / 8];
  uint8_t mapRevealed[BM_MAP_MAX_ROOMS / 8];
  BmMap map;
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
  // What is in sprite memory (layered-sprites.md §2, §3): each sheet's first tile there
  // (BM_GAME_NO_TILES: not loaded), the next free tile and where the room's part starts (after
  // the built-in graphics and the player's sheet, which stay); which sheet and palette each
  // bank holds (0xFF: free). Derived, not state: a room gives them out again.
  uint16_t sheetTile[BM_GAME_MAX_SHEETS];
  uint16_t tileNext, tileFloor;
  uint8_t bankSheet[BM_GAME_BANKS], bankPalette[BM_GAME_BANKS];
  uint16_t spriteMisses;                       // sheets or banks that did not fit (counted)
  // The (sheet, palette) pairs the room has drawn since it was entered: what is loaded again
  // when one more does not fit beside the sheets earlier rooms left (bmGameSheetBank).
  uint8_t usedCount;
  int8_t usedSheet[32];
  uint8_t usedPalette[32];
  int8_t playerSheet;                          // -1: the built-in figure
  // The player as a figure (figure.h): the figure "player" if the pack has one (playerSheet is
  // its root layer's sheet), else the sprite "player" alone (layerCount 0). Its layers' sheets
  // (-1: none), and the sheets that stay in sprite memory, each in a bank of its own from 0.
  BmFigure playerFigure;
  int8_t playerLayerSheet[BM_FIGURE_MAX_LAYERS];
  uint8_t playerSheetCount;
  int8_t playerSheets[BM_FIGURE_MAX_LAYERS];
  uint8_t playerPalettes[BM_FIGURE_MAX_LAYERS];
  int8_t sparkSheet;                           // the sprite "spark" (a blocked hit), or -1
  int8_t debrisSheet;                          // the sprite "debris" (a broken wall), or -1
  // Effects being shown (bmGameSpark, bmGameBreak): their sheet, where, and their age in frames
  // (0: none). Only seen, not state.
  int8_t effectSheet[BM_GAME_EFFECTS];
  int16_t effectX[BM_GAME_EFFECTS], effectY[BM_GAME_EFFECTS];
  uint8_t effectAge[BM_GAME_EFFECTS];
  // Breakable objects of the room (room-format.md §5): per object, bit 7 broken, bits 0-1 what
  // breaks it (BmBreakBy); the cells they leave and the holes in the room are made from these
  // when the room is entered (broken ones are kept in actors' store: bmGameBreak).
  uint8_t objectBreak[BM_GAME_MAX_OBJECTS];
  uint8_t breakCount;
  BmCellOverride breakCells[BM_ROOM_MAX_HOLES];
  // Push blocks, pressure plates and gates of the room (puzzle.h).
  BmPuzzle puzzle;
  // The grapple (grapple.h).
  BmGrapple grapple;
  // What the collision grid reads in place of its cells: broken walls, blocks, closed gates
  // (bmPuzzleOverrides makes them).
  uint8_t overrideCount;
  BmCellOverride overrides[BM_GAME_MAX_OVERRIDES];
  int8_t playerAnims[BM_PLAYER_ANIM_COUNT];
  int8_t objectSheet[BM_GAME_MAX_OBJECTS];     // sheet named like the object's class, or -1
  uint8_t objectDoorSide[BM_GAME_MAX_OBJECTS]; // a door's direction: 0 north (Up goes through), 1 east, 2 west, 3 other
  int16_t objectDoorGround[BM_GAME_MAX_OBJECTS]; // and the ground it stands on (y)
  uint16_t builtinTile;                        // where the built-in graphics start
  BmAnimState playerAnim;
  BmFigureLayerState playerLayers[BM_FIGURE_MAX_LAYERS];   // the player's layers ([0].anim unused: playerAnim)
  uint8_t playerLayerPalette[BM_FIGURE_MAX_LAYERS];      // the palette each layer's figure line names
  uint16_t equipPalette;   // string id of the palette the equipment names (look_palette), 0xFFFF none; derived
  // A door into a room the pack left out (a multiboot GBA, pack-format.md §0): the game holds
  // still and asks for the room over the link (bmLiveHooks.wantedRoom) until a pack that has it
  // arrives, then goes through the door; after BM_GAME_ROOM_WAIT_FRAMES without one, a menu
  // (try again, cancel). Not state: a loaded state or a restart ends it.
  uint16_t wantRoom;       // the string id + 1 of the room's id, 0: none
  uint8_t wantDoor;        // the door (object index + 1)
  uint8_t wantChoice;      // in the menu: 0 try again, 1 cancel
  uint16_t wantTimer;      // frames waited since asked (again)
  // The marker (marker_show): a sprite's first animation drawn at an object's middle.
  uint32_t markerTarget;   // a handle, 0: none
  uint16_t markerSprite;   // string id of the sprite
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
/// The palette bank sheet `sheet` is drawn with in palette `palette` (0: its own; one it does
/// not have: its own), loading its tiles into sprite memory and the palette into a free bank
/// the first time in a room. -1 if the sheet does not fit (counted in spriteMisses); with
/// no bank free, a bank with another palette of the sheet.
#define BM_GAME_ROOM_WAIT_FRAMES 180   // waiting for a left-out room before the menu (3 s)

/// Whether the game waits for a room over the link (a door into a room the pack left out);
/// *pMenu: the menu is up (no answer), with *pChoice selected (0 try again, 1 cancel).
bool bmGameWaitingForRoom(const BmGame *pGame, bool *pMenu, uint8_t *pChoice);

/// The player's layer named by string id `name` (figure.h), or -1.
int8_t bmGamePlayerLayer(const BmGame *pGame, uint16_t name);
/// Where point `point` (string id) of the player's layer `layer` is now: room pixels as fix and
/// its world angle. False if the layer's frame has no such point (then the layer's anchor and
/// the way the player faces).
bool bmGamePlayerPoint(BmGame *pGame, int8_t layer, uint16_t point, int32_t *pX, int32_t *pY, uint8_t *pAngle);

/// layer_aim ... layer_point_angle (script_api.h) on the player's figure; the result.
int32_t bmGameLayerCall(BmGame *pGame, uint16_t api, const int32_t *pArgs);

int8_t bmGameSheetBank(BmGame *pGame, int8_t sheet, uint8_t palette);

/// A spark at room pixel (x, y): the first frames of the sprite "spark" (if the pack has one),
/// as long as its first animation lasts. Shown where a guard box stopped a hit (hits.c).
void bmGameSpark(BmGame *pGame, int32_t x, int32_t y);

/// The same with the sprite named `name` (a string id): effect(#sprite) of scripts.
void bmGameEffect(BmGame *pGame, uint16_t name, int32_t x, int32_t y);

// What breaks a Breakable (its property `by`).
typedef enum BmBreakBy {
  BM_BREAK_ANY = 0,      // any hit of the player's
  BM_BREAK_PIERCE = 1,   // a hit with #Pierce
  BM_BREAK_ABOVE = 2,    // a hit coming from above it (a downward stab, the ground pound)
} BmBreakBy;

/// Breakable object `index` of the room breaks: its cells become its `cell` (empty unless it
/// says `top` or `full`), the main and foreground layers show nothing there, debris flies
/// (the sprite "debris"), the sound "crumble" plays, its `drop` (a record) is left behind as
/// an object of class Drop, and it stays broken (kept in the save game). Nothing if it is
/// broken already or not a Breakable.
void bmGameBreak(BmGame *pGame, uint16_t index);
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

/// Works out what the equipment adds to the stats (the role stat_source, data-format.md §6):
/// after equipping, loading a state and a pack that changed.
void bmGameEquipmentChanged(BmGame *pGame);

/// After something was given or taken: the abilities owned (their stats, their scripts in the
/// ability slots, attachments.h).
void bmGameInventoryChanged(BmGame *pGame);

/// The player's most health: 30 + the stat MaxHealth (inventory.h).
int32_t bmGameMaxHealth(const BmGame *pGame);

/// Shows texts in `language` from now on and keeps it as an option in the save memory.
void bmGameSetLanguage(BmGame *pGame, uint8_t language);

/// Save game in the save memory: the `save` fields. Loading returns false if there is none
/// or it was written by a different layout.
bool bmGameWriteSaveGame(BmGame *pGame);
bool bmGameReadSaveGame(BmGame *pGame);
