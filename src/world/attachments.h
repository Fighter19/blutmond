/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Attachments (docs/plans/attachable-scripts.md §3): small scripts of classes that extend
// `Attachment`, attached at run time in numbered slots to the player or to the room (§8, "The
// room as an owner"). Each is an interpreter
// instance like a room's scripted object; its class is kept by the hash of its name, so it
// survives a module being exchanged. The game runs them every frame before the player's body
// step, in slot order: game buttons that changed (press, release), tick, state code; after
// the body step, what happened to the player (jumped, landed). The room's attachments come
// after the body step and before the room's objects: tick and state code. They are attached
// from the room's property `attach` when the room is entered, and detached when it is left.

#include "shared.h"
#include "script_api.h"

#define BM_ATTACH_PLAYER_SLOTS 8   // slots 0 to 7: the player's
#define BM_ATTACH_ROOM_SLOTS 4     // slots 8 to 11: the room's
#define BM_ATTACH_ROOM_FIRST BM_ATTACH_PLAYER_SLOTS
#define BM_ATTACH_SLOTS (BM_ATTACH_PLAYER_SLOTS + BM_ATTACH_ROOM_SLOTS)
#define BM_ATTACH_STATS 4          // stats one attachment's add_stat is undone for when it goes

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
  // What add_stat from each slot added (stat + 1, 0 none; the amount), taken away again when
  // the slot is emptied.
  uint8_t statOf[BM_ATTACH_SLOTS * BM_ATTACH_STATS];
  int16_t statAdded[BM_ATTACH_SLOTS * BM_ATTACH_STATS];
  // Derived: the class in the actors' class table, or -1 (not in the pack now: the slot rests).
  int8_t cls[BM_ATTACH_SLOTS];
  uint16_t occupied;                      // a bit per slot with an attachment: nothing to do if 0
  uint16_t buttonName[8];                 // string ids of the game buttons' names
} BmAttachments;

struct BmGame;

/// The damage `amount` (of `element`) through the filter event `event` (damage_in,
/// damage_out) of the player's attachments in slot order; set_damage in them changes it.
int32_t bmAttachmentsFilter(struct BmGame *pGame, uint8_t event, int32_t amount, uint8_t element);

/// Attaches the Attachment class named `className` (string id) in `slot` (0 to 7: the
/// player's, 8 to 11: the room's), replacing what is there; it gets `attached()` at once.
/// `lifetime` frames (0: until detached). False if there is no such class or slot.
bool bmAttachmentsAttach(struct BmGame *pGame, uint8_t slot, uint16_t className, uint16_t lifetime);

/// Equips the record `key` in equipment slot `slot` (0 to 7; key 0 empties it): the class
/// its `attach` field names goes into the attachment slot of the same number, with its first
/// record<S> variable set to the record. False if the player has none of it or the slot is not
/// one; nothing changes then.
bool bmAttachmentsEquip(struct BmGame *pGame, uint8_t slot, uint32_t key);

/// Attaches again what is equipped (after a save game was read: attachments are not in it).
void bmAttachmentsEquipAll(struct BmGame *pGame);

/// Attaches the class named `className` (string id) in `slot` for `lifetime` frames (0: until
/// detached) with its first record<S> variable set to `record` (a command, commands.h).
bool bmAttachmentsAttachRecord(struct BmGame *pGame, uint8_t slot, uint16_t className, uint16_t lifetime, uint32_t record);

/// Detaches what is in `slot` (it gets `detached()` first).
void bmAttachmentsDetach(struct BmGame *pGame, uint8_t slot);

/// Before the player's body step: press/release for the game buttons that changed, tick,
/// state code, lifetimes.
void bmAttachmentsFrame(struct BmGame *pGame, uint16_t keys, uint16_t previousKeys);

/// After the player's body step: jumped(), landed() (BM_BODY_* events).
void bmAttachmentsBodyEvents(struct BmGame *pGame, uint32_t bodyEvents);

/// The room's attachments, once a frame before the room's objects: tick, state code, lifetimes.
void bmAttachmentsRoomFrame(struct BmGame *pGame);

/// Before the room is left (still loaded): detaches the room's attachments.
void bmAttachmentsLeaveRoom(struct BmGame *pGame);

/// After a room was entered anew (its objects are read): attaches what its `attach` property
/// names, sets their variables from its `Class.variable` properties and kept `save` values.
void bmAttachmentsEnterRoom(struct BmGame *pGame);

/// Runs an event of the attachment in `slot`, if there is one (hit, struck).
void bmAttachmentsEvent(struct BmGame *pGame, uint8_t slot, BmScriptEvent event, int32_t arg);

/// The player was struck: hit(amount) to each of the player's attachments.
void bmAttachmentsPlayerHit(struct BmGame *pGame, int32_t amount);

/// Uses an item the player owns (use_item, a quick slot): the script its record's first
/// `attach` field names is attached for one frame in slot BM_GAME_ITEM_SLOT with its first
/// record<S> variable set to the record; its attached() does what the item does (and takes it,
/// first-items.md §6). False if the player has none or the record names no such script.
bool bmAttachmentsUse(struct BmGame *pGame, uint32_t record);

/// After the pack's scripts were (re)loaded: finds each slot's class again.
void bmAttachmentsBind(struct BmGame *pGame);
