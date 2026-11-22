/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// What scripts can call and read (docs/spec/mondskript.md): one list, used by the compiler
// (names, argument and result types), the verifier (indices, argument counts) and the runtime
// (the functions behind them). The order is the API index stored in bytecode: add at the end,
// never reorder (runtime-abi.md).

// Value types, as the compiler and the class table name them.
typedef enum BmScriptType {
  BM_SCRIPT_VOID = 0,
  BM_SCRIPT_BOOL,
  BM_SCRIPT_INT,
  BM_SCRIPT_FIX,    // 24.8
  BM_SCRIPT_NAME,   // string id, written #Name
  BM_SCRIPT_TEXT,   // string id, written "text"
  BM_SCRIPT_ENUM,   // a value of an enum (0, 1, ...); which enum only the compiler knows
  BM_SCRIPT_RECORD, // record<S>: the key of a record (FNV-1a of its name), 0 none
} BmScriptType;

// X(name, result type, argument count, argument types..., latent)
// Latent functions may only be called from state code; they can take more than one frame.
#define BM_SCRIPT_API(X) \
  X(sleep,              VOID, 1, INT,  VOID, VOID, 1) \
  X(finish_anim,        VOID, 0, VOID, VOID, VOID, 1) \
  X(play_anim,          VOID, 1, NAME, VOID, VOID, 0) \
  X(anim_done,          BOOL, 0, VOID, VOID, VOID, 0) \
  X(player_x,           FIX,  0, VOID, VOID, VOID, 0) \
  X(player_y,           FIX,  0, VOID, VOID, VOID, 0) \
  X(player_distance,    INT,  0, VOID, VOID, VOID, 0) \
  X(move_toward_player, VOID, 1, FIX,  VOID, VOID, 0) \
  X(move_toward,        BOOL, 3, FIX,  FIX,  FIX,  0) \
  X(hurt_player,        VOID, 1, INT,  VOID, VOID, 0) \
  X(enter_door,         VOID, 0, VOID, VOID, VOID, 0) \
  X(hide,               VOID, 0, VOID, VOID, VOID, 0) \
  X(show,               VOID, 0, VOID, VOID, VOID, 0) \
  X(face_player,        VOID, 0, VOID, VOID, VOID, 0) \
  X(log,                VOID, 1, INT,  VOID, VOID, 0) \
  X(random,             INT,  1, INT,  VOID, VOID, 0) \
  X(touching_player,    BOOL, 0, VOID, VOID, VOID, 0) \
  X(give_item,          VOID, 1, NAME, VOID, VOID, 0) \
  X(has_item,           BOOL, 1, NAME, VOID, VOID, 0) \
  X(abs,                INT,  1, INT,  VOID, VOID, 0) \
  X(play_sound,         VOID, 1, NAME, VOID, VOID, 0) \
  X(play_music,         VOID, 1, NAME, VOID, VOID, 0) \
  X(record_get,         INT,  3, INT,  INT,  INT,  0) \
  X(record_count,       INT,  1, INT,  VOID, VOID, 0) \
  X(record_at,          INT,  2, INT,  INT,  VOID, 0) \
  X(item_count,         INT,  1, RECORD, VOID, VOID, 0) \
  X(give,               INT,  2, RECORD, INT,  VOID, 0) \
  X(take,               BOOL, 2, RECORD, INT,  VOID, 0) \
  X(stat,               INT,  1, ENUM, VOID, VOID, 0) \
  X(add_stat,           VOID, 2, ENUM, INT,  VOID, 0) \
  X(attach,             BOOL, 2, INT,  NAME, VOID, 0) \
  X(attach_for,         BOOL, 3, INT,  NAME, INT,  0) \
  X(detach,             VOID, 1, INT,  VOID, VOID, 0) \
  X(impulse,            VOID, 2, FIX,  FIX,  VOID, 0)

// record_get(table, key, field), record_count(table), record_at(table, index): what the
// compiler makes of record<S> values (docs/plans/structures.md §6). table: string id of the
// structure's name; field: offset | type << 16 (BmDataType).
//
// item_count(record), give(record, amount), take(record, amount): the inventory
// (src/world/inventory.h); an argument of type RECORD takes a record of any structure (or
// #Name). stat(value), add_stat(value, amount): the player's own stat values, numbered by the
// values of an enum the game declares (by convention `enum Stat { ... }`, at most 16).
//
// attach(slot, #Class), attach_for(slot, #Class, frames), detach(slot): the player's
// attachment slots (src/world/attachments.h); attach replaces what is in the slot and is false
// if there is no Attachment class of that name. impulse(vx, vy): added to the player's speed
// once.
#define BM_SCRIPT_RECORD_FIELD(offset, type) ((int32_t)(offset) | ((int32_t)(type) << 16))

typedef enum BmScriptApi {
#define X(name, result, argc, a0, a1, a2, latent) BM_SCRIPT_API_##name,
  BM_SCRIPT_API(X)
#undef X
  BM_SCRIPT_API_COUNT
} BmScriptApi;

// Variables every actor has, read and written like fields.
// X(name, type, writable)
#define BM_SCRIPT_ACTOR_VARS(X) \
  X(x,      FIX, 1) \
  X(y,      FIX, 1) \
  X(home_x, FIX, 0) \
  X(home_y, FIX, 0) \
  X(facing, INT, 1)

typedef enum BmScriptActorVar {
#define X(name, type, writable) BM_SCRIPT_VAR_##name,
  BM_SCRIPT_ACTOR_VARS(X)
#undef X
  BM_SCRIPT_VAR_COUNT
} BmScriptActorVar;

// Kinds of class (attachable-scripts.md §2), by the base class a script extends.
typedef enum BmScriptKind {
  BM_SCRIPT_KIND_ACTOR = 0,        // `extends Actor`: an object of a room
  BM_SCRIPT_KIND_ATTACHMENT = 1,   // `extends Attachment`: attached to the player at run time
  BM_SCRIPT_KIND_COUNT
} BmScriptKind;

// Events the runtime calls. X(name, type of the parameter (VOID: none), kinds that get it:
// 1 Actor, 2 Attachment)
#define BM_SCRIPT_EVENTS(X) \
  X(spawn,    VOID, 1) \
  X(tick,     VOID, 3) \
  X(touch,    VOID, 1) \
  X(attached, VOID, 2) \
  X(detached, VOID, 2) \
  X(press,    NAME, 2) \
  X(release,  NAME, 2) \
  X(jumped,   VOID, 2) \
  X(landed,   VOID, 2)

typedef enum BmScriptEvent {
#define X(name, arg, kinds) BM_SCRIPT_EVENT_##name,
  BM_SCRIPT_EVENTS(X)
#undef X
  BM_SCRIPT_EVENT_COUNT
} BmScriptEvent;

// Runtime functions only an Actor may call: they act on the object itself. X(name)
#define BM_SCRIPT_ACTOR_ONLY(X) \
  X(finish_anim) X(play_anim) X(anim_done) X(player_distance) X(move_toward_player) X(move_toward) \
  X(enter_door) X(hide) X(show) X(face_player) X(touching_player)

// Field flags in the class table: the storage flags of docs/spec/state.md.
#define BM_SCRIPT_FIELD_SAVE 1
#define BM_SCRIPT_FIELD_LINK 2
#define BM_SCRIPT_FIELD_TRANSIENT 4
#define BM_SCRIPT_FIELD_EDITABLE 8   // var(Category): settable from object properties

// Limits of this version.
#define BM_SCRIPT_MAX_FIELDS 8        // per class
#define BM_SCRIPT_MAX_REGISTERS 32    // per function
#define BM_SCRIPT_MAX_ARGS 3
