/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Scripted objects (docs/spec/mondskript.md): the objects of a room whose class is defined by
// a script in the pack's module. Each has a position, an animation and the interpreter's state
// (current state, place in the state code, fields). Everything that matters is kept in plain
// arrays so it is part of the game state: save states and link transfers continue scripts in
// the middle of a sleep.
//
// Limits of this version: 8 scripted objects per room, 8 variables per class, 16 values kept
// across rooms and saves (`save` variables). Items are in the inventory (inventory.h).

#include "shared.h"
#include "sprite.h"
#include "state.h"
#include "vm.h"

#define BM_ACTORS_MAX 8
#define BM_ACTORS_STORE 16
#define BM_ACTORS_MODULES 24   // script modules in a pack (one per class)
#define BM_ACTORS_CLASSES 32   // classes in all of them
#define BM_ACTORS_OWNER 0xFF   // `current` while an attachment of the player runs
#define BM_ACTORS_ROOM 0xFE    // `current` while an attachment of the room runs
#define BM_ACTORS_SCREEN 0xFD  // `current` while a screen runs (currentSlot: its place on the stack)

struct BmGame;

typedef struct BmActors
{
  // Derived: rebuilt when a room is entered. The pack's script modules (one per class, or
  // one for all in older packs), and a table of every class in them.
  bool hasModule;                      // there is at least one class
  uint8_t moduleCount;
  BmVmModule modules[BM_ACTORS_MODULES];
  const uint8_t *pVerified[BM_ACTORS_MODULES];   // the module data that passed verification
  uint32_t verifiedHash[BM_ACTORS_MODULES];
  uint8_t classCount;
  uint8_t classModule[BM_ACTORS_CLASSES];        // which module a class is in
  uint8_t classInModule[BM_ACTORS_CLASSES];      // its index there
  // A reload of single modules (bmActorsPrepareReload): what each object was before, by
  // hashes of names, since string ids change from pack to pack.
  bool keep;
  uint8_t keepCount;
  uint16_t keepObject[BM_ACTORS_MAX];  // the object's persistent id
  uint32_t keepClass[BM_ACTORS_MAX], keepModule[BM_ACTORS_MAX], keepLayout[BM_ACTORS_MAX], keepState[BM_ACTORS_MAX];
  uint8_t object[BM_ACTORS_MAX];       // index into the game's objects; 0xFF: made at run time
  uint8_t roomCount;                   // the room's objects; those made at run time come after
  int8_t sheet[BM_ACTORS_MAX];         // sprite sheet, or -1
  int32_t homeX[BM_ACTORS_MAX], homeY[BM_ACTORS_MAX];
  uint8_t pendingDoor;                 // object index + 1 of a door to go through after the frame
  uint8_t current;                     // the actor whose script is running
  uint8_t currentSlot;                 // while an attachment runs: its slot

  // State.
  uint32_t moduleHash;                 // the modules the state below belongs to
  uint8_t count;
  uint16_t classIndex[BM_ACTORS_MAX];  // into the class table
  uint8_t state[BM_ACTORS_MAX], wait[BM_ACTORS_MAX];
  uint16_t pc[BM_ACTORS_MAX], timer[BM_ACTORS_MAX];
  int32_t fields[BM_ACTORS_MAX * BM_SCRIPT_MAX_FIELDS];
  int32_t x[BM_ACTORS_MAX], y[BM_ACTORS_MAX];   // fix 24.8, the anchor
  int8_t facing[BM_ACTORS_MAX];
  uint8_t hidden[BM_ACTORS_MAX], touching[BM_ACTORS_MAX];
  uint8_t anim[BM_ACTORS_MAX], animFinished[BM_ACTORS_MAX];
  uint16_t animStep[BM_ACTORS_MAX], animTimer[BM_ACTORS_MAX];
  int32_t health[BM_ACTORS_MAX];       // struck only while above 0 (hits.h)
  uint8_t team[BM_ACTORS_MAX];         // 0: enemies
  uint8_t invulnerable[BM_ACTORS_MAX]; // frames until it can be struck again
  uint8_t spawned[BM_ACTORS_MAX];      // made at run time (spawn): no room object, gone with the room
  uint8_t gone[BM_ACTORS_MAX];         // destroyed: the place can be taken by the next spawn
  uint32_t seed;
  // `save` variables, by room, object and field.
  uint8_t storeCount;
  uint32_t storeKey[BM_ACTORS_STORE];
  int32_t storeValue[BM_ACTORS_STORE];
} BmActors;

/// The module of an actor's class and the class's index in it (for tests and tools).
const BmVmModule *bmActorsModule(const BmActors *pActors, uint8_t actor, uint16_t *pClassInModule);

/// Before a pack whose scripts may differ in single classes is put in place (reload mode
/// `module`): notes each object's class, module, variables and state, so that the next
/// bmActorsEnterRoom keeps every object whose module did not change, keeps the variables of
/// one whose class still has the same variables (its state's code starts over), and starts
/// only the others anew.
void bmActorsPrepareReload(struct BmGame *pGame);

/// Called when a room was entered. `fresh`: objects start anew (a room change or a restart);
/// otherwise the state was just loaded and only what is derived is rebuilt - unless the scripts
/// are not the ones the state was made with, then the objects start anew too.
void bmActorsEnterRoom(struct BmGame *pGame, bool fresh);

/// One frame: tick and touch events, state code, animations.
void bmActorsFrame(struct BmGame *pGame);

/// Draws the scripted objects from OAM entry `firstOam`; returns how many entries it used.
uint8_t bmActorsDraw(struct BmGame *pGame, uint8_t firstOam, uint8_t maxOam);

/// The box of scripted object `actor` in room pixels: its rectangle, its sprite's body box, or
/// 16x16 above its anchor.
void bmActorsBox(const struct BmGame *pGame, uint8_t actor, int32_t *pX, int32_t *pY, int32_t *pW, int32_t *pH);

/// A new object of the Actor class named `className` (string id) at (x, y) (fix), in a free
/// place (one destroyed, or after the room's objects); its first record<S> variable is set to
/// `record` if that is not 0. It gets spawn(). False if there is no such class or no place.
bool bmActorsSpawn(struct BmGame *pGame, uint16_t className, int32_t x, int32_t y, uint32_t record);

/// Object `actor` is gone: not drawn, not run, its hits end; its place is free.
void bmActorsDestroy(struct BmGame *pGame, uint8_t actor);

/// Runs an event of scripted object `actor` (hit, struck).
void bmActorsEvent(struct BmGame *pGame, uint8_t actor, BmScriptEvent event, int32_t arg);

/// Whether an object of the room is scripted (then the engine leaves it alone).
bool bmActorsOwnObject(const struct BmGame *pGame, uint16_t objectIndex);

/// Script errors since the start (for tests and the status display).
extern uint32_t bm_script_errors;
/// Processor cycles spent running scripts since the start (bmHalCycles).
extern uint32_t bm_script_cycles;
