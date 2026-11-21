/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Native script modules (docs/spec/mondskript.md §12): the functions of a bytecode module
// translated to C and compiled for the target. A native module always comes with its bytecode
// module, which keeps the class tables, and does exactly what the interpreter would do: the
// same fields, the same waits and the same place to continue (the bytecode word index), so
// game state made with one runs on with the other.
//
// This header is also what the generated C includes. Native code has no global variables and
// calls nothing at a fixed address: everything goes through the context.

#include "vm.h"

#define BM_NATIVE_MAGIC 0x414E4D42u   // "BMNA"
#define BM_NATIVE_VERSION 1
#define BM_NATIVE_CHUNK_HEADER 16

// Machines a native module can be built for (the chunk's target field).
#define BM_NATIVE_TARGET_NONE 0
#define BM_NATIVE_TARGET_ARMV4T 1     // GBA: ARM7TDMI, little endian, interworking
#if defined(BM_PLATFORM_GBA)
#define BM_NATIVE_TARGET BM_NATIVE_TARGET_ARMV4T
#else
#define BM_NATIVE_TARGET BM_NATIVE_TARGET_NONE
#endif

// The interpreter's limits, which native code keeps too.
#define BM_VM_BUDGET 4000
#define BM_VM_MAX_DEPTH 16
#define BM_VM_STACK_REGISTERS 128

typedef enum BmVmFailure {
  BM_VM_FAIL_BUDGET,
  BM_VM_FAIL_DEPTH,
  BM_VM_FAIL_INSTRUCTION,
} BmVmFailure;

struct BmVmNativeCtx;

/// What the runtime does for native code.
typedef struct BmVmNativeRt
{
  int32_t (*div)(int32_t n, int32_t d);    // by zero: 0
  int32_t (*mod)(int32_t n, int32_t d);
  int32_t (*fdiv)(int32_t n, int32_t d);   // fix: (n << 8) / d
  /// Stops the object with a script error at bytecode word `pc` of `function`.
  void (*fail)(struct BmVmNativeCtx *pCtx, uint32_t failure, uint32_t function, uint32_t pc);
} BmVmNativeRt;

typedef struct BmVmNativeCtx
{
  const BmVmHost *pHost;
  BmVmInstance *pInstance;
  const BmVmNativeRt *pRt;
  const BmVmModule *pModule;
  int32_t budget;        // instructions left
  uint16_t registers;    // registers in use by the functions being run
  uint8_t depth;         // calls inside each other
} BmVmNativeCtx;

/// Runs a function (an event or state code) from bytecode word `pc`. Returns the word to
/// continue at if a latent function started waiting, else 0xFFFF.
typedef uint32_t (*BmVmNativeEntry)(BmVmNativeCtx *pCtx, uint32_t pc);

/// The start of a native module's image.
typedef struct BmVmNativeHeader
{
  uint32_t magic;
  uint16_t version;
  uint16_t functionCount;
  uint32_t bytecodeCrc;                 // CRC-32 of the bytecode module it was made from
} BmVmNativeHeader;

typedef struct BmVmNativeModule
{
  BmVmNativeHeader head;
  BmVmNativeEntry entries[1];           // functionCount of them; NULL: not an entry (the interpreter runs it)
} BmVmNativeModule;

/// Copies the image of a MODN chunk to `pSlot` and relocates it there. Returns the module, or
/// NULL with a reason: not for this machine, too big for the slot, damaged, or not made from
/// the bytecode with checksum `bytecodeCrc`.
const BmVmNativeModule *bmVmNativeLoad(const uint8_t *pChunk, uint32_t size, void *pSlot, uint32_t slotSize, uint32_t bytecodeCrc, const char **ppError);

/// The loader's work for any target (for tests): checks the chunk, copies the image to `pSlot`
/// and adds `base` to every address in it.
bool bmVmNativeRelocate(const uint8_t *pChunk, uint32_t size, void *pSlot, uint32_t slotSize, uint32_t bytecodeCrc, uint32_t target, uint32_t base, const char **ppError);

/// Makes a module run its functions as native code (NULL: interpret again). The native module
/// must have been made from this bytecode; false if its function count does not fit.
bool bmVmModuleSetNative(BmVmModule *pModule, const BmVmNativeModule *pNative);

/// The slot native modules are loaded into (IWRAM on the GBA), and its size.
void *bmVmNativeSlot(uint32_t *pSize);

/// Calls into native code since the start (for tests).
extern uint32_t bm_native_calls;
