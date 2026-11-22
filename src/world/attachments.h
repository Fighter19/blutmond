/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Attachments (docs/plans/attachable-scripts.md §3): small scripts of classes that extend
// `Attachment`, attached to the player at run time in numbered slots. Each is an interpreter
// instance like a room's scripted object; its class is kept by the hash of its name, so it
// survives a module being exchanged. The game runs them every frame before the player's body
// step, in slot order: game buttons that changed (press, release), tick, state code; after
// the body step, what happened to the player (jumped, landed).

#include "shared.h"
#include "script_api.h"

#define BM_ATTACH_SLOTS 8

// Game buttons: what the keys mean to attachments (press/release get these names).
#define BM_ATTACH_BUTTONS(X) \
  X(Jump,   BM_KEY_A) \
  X(Attack, BM_KEY_B) \
  X(Dash,   BM_KEY_L) \
  X(Spell,  BM_KEY_R) \
  X(Use,    BM_KEY_SELECT)

typedef struct BmAttachments
{
  // State (snapshot): one interpreter instance per slot.
  uint32_t classKey[BM_ATTACH_SLOTS];     // FNV-1a of the class's name; 0: the slot is empty
  uint8_t state[BM_ATTACH_SLOTS], wait[BM_ATTACH_SLOTS];
  uint16_t pc[BM_ATTACH_SLOTS], timer[BM_ATTACH_SLOTS];
  int32_t fields[BM_ATTACH_SLOTS * BM_SCRIPT_MAX_FIELDS];
  uint16_t lifetime[BM_ATTACH_SLOTS];     // frames left; 0: until detached
  // Derived: the class in the actors' class table, or -1 (not in the pack now: the slot rests).
  int8_t cls[BM_ATTACH_SLOTS];
  uint8_t occupied;                       // a bit per slot with an attachment: nothing to do if 0
  uint16_t buttonName[8];                 // string ids of the game buttons' names
} BmAttachments;

struct BmGame;

/// Attaches the Attachment class named `className` (string id) in `slot`, replacing what is
/// there; it gets `attached()` at once. `lifetime` frames (0: until detached). False if there
/// is no such class or slot.
bool bmAttachmentsAttach(struct BmGame *pGame, uint8_t slot, uint16_t className, uint16_t lifetime);

/// Detaches what is in `slot` (it gets `detached()` first).
void bmAttachmentsDetach(struct BmGame *pGame, uint8_t slot);

/// Before the player's body step: press/release for the game buttons that changed, tick,
/// state code, lifetimes.
void bmAttachmentsFrame(struct BmGame *pGame, uint16_t keys, uint16_t previousKeys);

/// After the player's body step: jumped(), landed() (BM_BODY_* events).
void bmAttachmentsBodyEvents(struct BmGame *pGame, uint32_t bodyEvents);

/// After the pack's scripts were (re)loaded: finds each slot's class again.
void bmAttachmentsBind(struct BmGame *pGame);
