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
  X(play_music,         VOID, 1, NAME, VOID, VOID, 0)

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

// Events the runtime calls. X(name)
#define BM_SCRIPT_EVENTS(X) \
  X(spawn) \
  X(tick) \
  X(touch)

typedef enum BmScriptEvent {
#define X(name) BM_SCRIPT_EVENT_##name,
  BM_SCRIPT_EVENTS(X)
#undef X
  BM_SCRIPT_EVENT_COUNT
} BmScriptEvent;

// Field flags in the class table: the storage flags of docs/spec/state.md.
#define BM_SCRIPT_FIELD_SAVE 1
#define BM_SCRIPT_FIELD_LINK 2
#define BM_SCRIPT_FIELD_TRANSIENT 4
#define BM_SCRIPT_FIELD_EDITABLE 8   // var(Category): settable from object properties

// Limits of this version.
#define BM_SCRIPT_MAX_FIELDS 8        // per class
#define BM_SCRIPT_MAX_REGISTERS 32    // per function
#define BM_SCRIPT_MAX_ARGS 3
