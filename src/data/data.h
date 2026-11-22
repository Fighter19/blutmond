/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Records (docs/plans/structures.md): one DTAB chunk per structure, read in place (from ROM on
// a cartridge). The chunk:
//
//   u16 structure (string id of its name)   u16 record count   u16 record size   u16 fields
//   u32 layout hash (CRC-32 over the fields' names, types, counts and offsets)
//   fields:   { u16 name; u8 type (BmDataType); u8 count; u16 offset; u16 detail; } [fields]
//             detail: string id of the enum, inner structure or record<S> target, else 0xFFFF
//   keys:     { u32 key (FNV-1a of the record's name); u16 name; u16 0; } [records], by key
//   defaults: one record with every field at the structure's default
//   records:  [records * record size]
//   roles:    (only a structure with roles) "ROLE", u8 roles (BM_DATA_ROLE_*), u8 equipment slot
//             (BM_DATA_SLOT_FROM_FIELD: the one-byte field at the offset that follows), u16 its
//             offset, u8 stat count, u8 0, u16 0; { u16 offset; u8 type; u8 0; } [stat count]
//
// Everything little endian; the record size is a multiple of 4.

#include "data_types.h"
#include "pack.h"

#include <stdbool.h>
#include <stdint.h>

#define BM_DATA_HEADER_SIZE 12
#define BM_DATA_FIELD_SIZE 8
#define BM_DATA_KEY_SIZE 8

typedef struct BmDataTable
{
  uint16_t structure;
  uint16_t recordCount;
  uint16_t recordSize;
  uint16_t fieldCount;
  uint32_t layoutHash;
  const uint8_t *pFields;
  const uint8_t *pKeys;
  const uint8_t *pDefaults;
  const uint8_t *pRecords;
  const uint8_t *pRoles;     // the roles block, NULL: no roles
} BmDataTable;

typedef struct BmDataField
{
  uint16_t name;
  uint8_t type;      // BmDataType
  uint8_t count;
  uint16_t offset;
  uint16_t detail;
} BmDataField;

/// Checks a DTAB chunk (sizes, fields inside a record, keys in order) and describes it.
bool bmDataTableOpen(BmDataTable *pTable, const uint8_t *pData, uint32_t size);

/// The table of the structure with this name (string id) in the pack.
bool bmDataFindTable(const BmPack *pPack, uint16_t structure, BmDataTable *pTable);

/// Opens (and checks) every table of the pack now, so that the first lookup of a record does
/// not pay for it in the middle of a game frame. bmDataFindRecord does it when the pack changed.
void bmDataPrepare(const BmPack *pPack);

/// The table that holds the record with this key, and the record (any structure).
bool bmDataFindRecord(const BmPack *pPack, uint32_t key, BmDataTable *pTable, const uint8_t **ppRecord);

/// The structure's roles (BM_DATA_ROLE_*), 0 without.
uint8_t bmDataTableRoles(const BmDataTable *pTable);

/// The equipment slot a record of a structure with the role `equip` goes into, or 0xFF.
uint8_t bmDataRecordSlot(const BmDataTable *pTable, const uint8_t *pRecord);

/// The stats a record of a structure with the role `stat_source` adds while equipped: stat i in
/// pStats[i] (BM_DATA_MAX_STATS of them). Returns how many there are (0 without the role).
uint32_t bmDataRecordStats(const BmDataTable *pTable, const uint8_t *pRecord, int32_t *pStats);

/// The value of a record's first field of type `attach` (a class's name as a string id), or
/// BM_DATA_NONE_ID: the script that comes with it when it is equipped.
uint16_t bmDataRecordBehaviour(const BmDataTable *pTable, const uint8_t *pRecord);

BmDataField bmDataTableField(const BmDataTable *pTable, uint16_t index);
uint32_t bmDataRecordKey(const BmDataTable *pTable, uint16_t index);
uint16_t bmDataRecordName(const BmDataTable *pTable, uint16_t index);

/// The record with this key, or NULL.
const uint8_t *bmDataRecord(const BmDataTable *pTable, uint32_t key);

/// The record with this key, or the structure's defaults if there is none (a save from
/// another version cannot make a script read nonsense).
const uint8_t *bmDataRecordOrDefaults(const BmDataTable *pTable, uint32_t key);

/// A value of a field in a record, widened to 32 bits by its type (signed types sign-extended).
/// 0 if the field does not lie inside the record.
int32_t bmDataRead(const BmDataTable *pTable, const uint8_t *pRecord, uint16_t offset, BmDataType type);

/// Bytes of a value of this type (an inner structure has no fixed size: 0).
uint32_t bmDataTypeSize(BmDataType type);

/// The script calls behind record<S> (record_get, record_count, record_at; script_api.h).
/// False if `api` is none of them. A table that is not in the pack has no records; a key that
/// is not in the table reads the structure's defaults, or 0 without a table.
bool bmDataScriptCall(const BmPack *pPack, uint16_t api, const int32_t *pArgs, int32_t *pResult);
