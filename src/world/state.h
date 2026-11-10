/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Game state description and serialisation (docs/spec/state.md). State lives in plain structs
// without pointers; a schema lists every field with its type, place and flags. Snapshots and
// save games are written field by field in little endian, so the host and the GBA produce and
// accept the same bytes. The schema hash detects data written by a different layout.

#include "shared.h"

#include <stddef.h>

enum {
  BM_STATE_SAVE = 1 << 0,      // written to the save game
  BM_STATE_LINK = 1 << 1,      // sent on link transfer
  BM_STATE_SNAPSHOT = 1 << 2,  // part of a save state
  BM_STATE_TRANSIENT = 1 << 3, // never saved; reset on load
  BM_STATE_DERIVED = 1 << 4,   // never saved; recomputed after loading
};

typedef enum BmStateType {
  BM_TYPE_U8, BM_TYPE_S8, BM_TYPE_U16, BM_TYPE_S16, BM_TYPE_U32, BM_TYPE_S32,
  BM_TYPE_BOOL, BM_TYPE_FIX,
} BmStateType;

typedef struct BmStateField
{
  const char *name;
  uint16_t offset;
  uint8_t type;   // BmStateType
  uint8_t flags;  // BM_STATE_*
  uint16_t count; // array length (1 for single values)
} BmStateField;

#define BM_STATE_FIELD(structType, member, type, flags) \
  { #member, (uint16_t)offsetof(structType, member), (uint8_t)(type), (uint8_t)(flags), 1 }
#define BM_STATE_ARRAY(structType, member, type, flags, n) \
  { #member, (uint16_t)offsetof(structType, member), (uint8_t)(type), (uint8_t)(flags), (uint16_t)(n) }

/// A struct described by a schema.
typedef struct BmStateBlock
{
  const char *name;
  const BmStateField *pFields;
  uint16_t fieldCount;
  void *pBase;
} BmStateBlock;

/// Bytes a value of the type takes when serialised.
uint32_t bmStateTypeSize(uint8_t type);

/// Hash over names, types, flags and counts of all fields (not offsets: those may differ
/// between compilers; the serialised form does not depend on them).
uint32_t bmStateSchemaHash(const BmStateBlock *pBlocks, uint32_t blockCount);

/// Serialised size of the fields that have any of `flagMask`, including the header.
uint32_t bmStateSize(const BmStateBlock *pBlocks, uint32_t blockCount, uint8_t flagMask);

#define BM_STATE_HEADER_SIZE 16

/// Writes the fields with any of `flagMask`: header (magic, version, flag mask, schema hash,
/// payload size, CRC-32) and the values. Returns the bytes written, or 0 if `capacity` is too small.
uint32_t bmStateWrite(const BmStateBlock *pBlocks, uint32_t blockCount, uint8_t flagMask, uint8_t *pOut, uint32_t capacity);

typedef enum BmStateReadResult {
  BM_STATE_OK = 0,
  BM_STATE_BAD_HEADER,
  BM_STATE_OTHER_SCHEMA,   // written by a different layout
  BM_STATE_BAD_CRC,
} BmStateReadResult;

/// Reads data written by bmStateWrite with the same flag mask and schema.
BmStateReadResult bmStateRead(const BmStateBlock *pBlocks, uint32_t blockCount, const uint8_t *pIn, uint32_t size);
