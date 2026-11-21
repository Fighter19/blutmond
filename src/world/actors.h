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
// across rooms and saves (`save` variables), 4 items.

#include "shared.h"
#include "sprite.h"
#include "state.h"
#include "vm.h"

#define BM_ACTORS_MAX 8
#define BM_ACTORS_STORE 16
#define BM_ACTORS_ITEMS 4

struct BmGame;

typedef struct BmActors
{
  // Derived: rebuilt when a room is entered.
  bool hasModule;
  BmVmModule module;
  const uint8_t *pVerified;            // the module data that passed verification
  uint32_t verifiedHash;
  uint8_t object[BM_ACTORS_MAX];       // index into the game's objects
  int8_t sheet[BM_ACTORS_MAX];         // sprite sheet, or -1
  int32_t homeX[BM_ACTORS_MAX], homeY[BM_ACTORS_MAX];
  uint8_t pendingDoor;                 // object index + 1 of a door to go through after the frame
  uint8_t current;                     // the actor whose script is running

  // State.
  uint32_t moduleHash;                 // the module the state below belongs to
  uint8_t count;
  uint16_t classIndex[BM_ACTORS_MAX];
  uint8_t state[BM_ACTORS_MAX], wait[BM_ACTORS_MAX];
  uint16_t pc[BM_ACTORS_MAX], timer[BM_ACTORS_MAX];
  int32_t fields[BM_ACTORS_MAX * BM_SCRIPT_MAX_FIELDS];
  int32_t x[BM_ACTORS_MAX], y[BM_ACTORS_MAX];   // fix 24.8, the anchor
  int8_t facing[BM_ACTORS_MAX];
  uint8_t hidden[BM_ACTORS_MAX], touching[BM_ACTORS_MAX];
  uint8_t anim[BM_ACTORS_MAX], animFinished[BM_ACTORS_MAX];
  uint16_t animStep[BM_ACTORS_MAX], animTimer[BM_ACTORS_MAX];
  uint32_t seed;
  // `save` variables, by room, object and field; and the items the player has.
  uint8_t storeCount;
  uint32_t storeKey[BM_ACTORS_STORE];
  int32_t storeValue[BM_ACTORS_STORE];
  uint8_t itemCount;
  uint32_t items[BM_ACTORS_ITEMS];     // hashes of the item names
} BmActors;

/// Called when a room was entered. `fresh`: objects start anew (a room change or a restart);
/// otherwise the state was just loaded and only what is derived is rebuilt - unless the scripts
/// are not the ones the state was made with, then the objects start anew too.
void bmActorsEnterRoom(struct BmGame *pGame, bool fresh);

/// One frame: tick and touch events, state code, animations.
void bmActorsFrame(struct BmGame *pGame);

/// Draws the scripted objects from OAM entry `firstOam`; returns how many entries it used.
uint8_t bmActorsDraw(struct BmGame *pGame, uint8_t firstOam, uint8_t maxOam);

/// Whether an object of the room is scripted (then the engine leaves it alone).
bool bmActorsOwnObject(const struct BmGame *pGame, uint16_t objectIndex);

/// Script errors since the start (for tests and the status display).
extern uint32_t bm_script_errors;
