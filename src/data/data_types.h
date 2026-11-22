/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Field types of structures (docs/plans/structures.md §2): what the compiler lays out, the
// cooker writes into DTAB chunks and the runtime reads. The numbers are stored in packs: add
// at the end, never reorder.
typedef enum BmDataType {
  BM_DATA_BOOL = 0,   // 1 byte
  BM_DATA_BYTE,       // 1, signed
  BM_DATA_UBYTE,      // 1
  BM_DATA_SHORT,      // 2, signed
  BM_DATA_USHORT,     // 2
  BM_DATA_INT,        // 4, signed
  BM_DATA_UINT,       // 4
  BM_DATA_FIX,        // 4, 24.8
  BM_DATA_NAME,       // 2, string id
  BM_DATA_TEXT,       // 2, string id
  BM_DATA_ENUM,       // 1, value in declaration order
  BM_DATA_STRUCT,     // the inner structure's size, inline
  BM_DATA_SPRITE,     // 2, string id of the sprite's name; 0xFFFF: none
  BM_DATA_SOUND,      // 2, string id of the song or effect; 0xFFFF: none
  BM_DATA_RECORD,     // 4, key of a record (FNV-1a of its name); 0: none
  BM_DATA_ATTACH,     // 2, string id of a script class; 0xFFFF: none
  BM_DATA_TYPE_COUNT
} BmDataType;

#define BM_DATA_NONE_ID 0xFFFFu       // sprite, sound, attach: nothing
#define BM_DATA_MAX_FIELDS 32         // per structure, inherited ones included
#define BM_DATA_MAX_RECORD_SIZE 64    // bytes
#define BM_DATA_MAX_ENUM_VALUES 256

// Roles a structure declares (docs/spec/data-format.md §6): what the engine does with its
// records. Stored in the DTAB chunk's roles block.
#define BM_DATA_ROLE_ITEM 1           // can be carried: has title, icon, max_stack
#define BM_DATA_ROLE_EQUIP 2          // goes into an equipment slot (one, or the one a field names)
#define BM_DATA_ROLE_USABLE 4         // can be used (use_item): has an attach field
#define BM_DATA_ROLE_STAT_SOURCE 8    // its stats count while it is equipped
#define BM_DATA_MAX_EQUIP_SLOTS 8     // values of the game's enum Slot
#define BM_DATA_MAX_STATS 16          // values of the game's enum Stat
#define BM_DATA_SLOT_FROM_FIELD 0xFFu // the equipment slot is a field of each record
