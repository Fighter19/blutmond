/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The bytecode interpreter for Mondskript (docs/spec/mondskript.md §10): register based,
// 16-bit code words. A module (MODB chunk) is verified when it is opened, so running it can
// never read or write outside an object's fields and the function's registers, jump into the
// middle of an instruction or call something that is not there. Nothing here allocates.

#include <stdbool.h>
#include <stdint.h>

#include "script_api.h"

#define BM_VM_VERSION 2   // 2: the events of script_api.h, class kinds, events with a parameter
#define BM_VM_HEADER_SIZE 40
// A class: name, sprite, fields, states, start state, kind, a function per event (padded to
// 4 bytes), the offsets of its fields and its states. A state: name, code, a function per event.
#define BM_VM_EVENTS_SIZE (((uint32_t)BM_SCRIPT_EVENT_COUNT * 2 + 3) & ~3u)
#define BM_VM_CLASS_FIELDS_AT (8 + BM_VM_EVENTS_SIZE)
#define BM_VM_CLASS_STATES_AT (12 + BM_VM_EVENTS_SIZE)
#define BM_VM_CLASS_SIZE (16 + BM_VM_EVENTS_SIZE)
#define BM_VM_FIELD_SIZE 8
#define BM_VM_STATE_SIZE ((4 + (uint32_t)BM_SCRIPT_EVENT_COUNT * 2 + 3) & ~3u)
#define BM_VM_FUNCTION_SIZE 12
#define BM_VM_NONE 0xFFFFu

#define BM_VM_FUNCTION_STATE_CODE 1   // may call latent functions

/// Instructions. Word 0 is `op | a << 8`; operand words follow as listed.
typedef enum BmVmOp {
  BM_OP_LOADI,      // a = sign-extended word 1
  BM_OP_LOADK,      // a = constant[word 1]
  BM_OP_MOVE,       // a = r[word 1]
  BM_OP_GETF,       // a = field[word 1]
  BM_OP_SETF,       // field[word 1] = a
  BM_OP_GETV,       // a = actor variable word 1
  BM_OP_SETV,       // actor variable word 1 = a
  BM_OP_ADD,        // a = r[b] op r[c], word 1 = b | c << 8
  BM_OP_SUB,
  BM_OP_MUL,
  BM_OP_DIV,        // by zero: 0
  BM_OP_MOD,
  BM_OP_FMUL,       // fix: (b * c) >> 8
  BM_OP_FDIV,       // fix: (b << 8) / c
  BM_OP_EQ,
  BM_OP_NE,
  BM_OP_LT,
  BM_OP_LE,
  BM_OP_NEG,        // a = -r[word 1]
  BM_OP_NOT,        // a = !r[word 1]
  BM_OP_JMP,        // to word 1 (a word index inside the function)
  BM_OP_JZ,         // if r[a] == 0
  BM_OP_JNZ,
  BM_OP_CALLN,      // a = api[word 1 & 0xFF](r[word 1 >> 8] ...)
  BM_OP_CALL,       // a = function[word 1](r[word 2] ...)
  BM_OP_RET,        // return r[a] (a = 0xFF: nothing)
  BM_OP_GOTOSTATE,  // state a
  BM_OP_STOP,       // end of state code
  BM_OP_COUNT
} BmVmOp;

/// Words an instruction takes, or 0 for an unknown one.
uint8_t bmVmOpLength(uint8_t op);

typedef struct BmVmModule
{
  const uint8_t *pData;
  uint32_t size;
  uint16_t classCount, functionCount, constCount;
  const uint8_t *pClasses, *pFunctions, *pConsts, *pCode;
  uint32_t codeWords;
  const uint8_t *pLines;   // may be NULL
  const struct BmVmNativeModule *pNative;   // native.h; NULL: everything is interpreted
  // Whether its code was verified: 0 not yet (bmVmModuleOpenLazy), 1 yes, 2 it was refused.
  // Code of a module not yet verified is verified when it first runs.
  uint8_t verified;
  uint16_t verifiedFunctions;   // functions verified so far (bmVmModuleVerifySome)
} BmVmModule;

/// What an object is waiting for.
typedef enum BmVmWait {
  BM_VM_RUNNING = 0,   // state code continues next frame (or there is none)
  BM_VM_SLEEPING,      // `timer` frames
  BM_VM_FINISH_ANIM,   // until the host says the animation is done
  BM_VM_STOPPED,       // state code reached its end
  BM_VM_FAILED,        // a script error; the object does nothing more
} BmVmWait;

/// One scripted object. Plain data: it is part of the game state.
typedef struct BmVmInstance
{
  uint16_t classIndex;
  uint8_t state;        // 0xFF: none
  uint8_t wait;         // BmVmWait
  uint16_t pc;          // where the state code continues (word index in its function)
  uint16_t timer;
  int32_t fields[BM_SCRIPT_MAX_FIELDS];
} BmVmInstance;

/// The game behind the scripts.
typedef struct BmVmHost
{
  void *pContext;
  /// Runs API function `api`. Returns true if a latent function started waiting (it then set
  /// pInstance->wait and timer).
  bool (*call)(void *pContext, BmVmInstance *pInstance, uint16_t api, const int32_t *pArgs, int32_t *pResult);
  int32_t (*getVar)(void *pContext, BmVmInstance *pInstance, uint16_t var);
  void (*setVar)(void *pContext, BmVmInstance *pInstance, uint16_t var, int32_t value);
  /// A field with a storage flag changed (so `save` fields can be kept per object).
  void (*fieldChanged)(void *pContext, BmVmInstance *pInstance, uint16_t field);
  /// True when what the instance waits for (BM_VM_FINISH_ANIM) has happened.
  bool (*waitOver)(void *pContext, BmVmInstance *pInstance);
  /// A script failed at run time: function index and source line (0 if unknown).
  void (*error)(void *pContext, BmVmInstance *pInstance, const char *pWhat, uint16_t function, uint16_t line);
} BmVmHost;

/// Opens and verifies a module. On failure `*ppError` says why and the module must not be used.
bool bmVmModuleOpen(BmVmModule *pModule, const uint8_t *pData, uint32_t size, const char **ppError);

/// Opens a module, checking its header and class tables (what reading its classes needs), but
/// not its code: that is verified when code of it first runs (an instance of a class whose
/// module is refused then fails). Much quicker for a pack's many scripts at its start.
bool bmVmModuleOpenLazy(BmVmModule *pModule, const uint8_t *pData, uint32_t size, const char **ppError);

/// Verifies the code of a module opened lazily (nothing to do if it was). False if refused.
bool bmVmModuleVerify(BmVmModule *pModule, const char **ppError);

/// Verifies more functions of a module opened lazily, as many as fit into `*pWords` (their
/// code words, which it takes off; a function that does not fit waits), and the rest of the
/// module when they are all done: the work spread over frames. False if refused.
bool bmVmModuleVerifySome(BmVmModule *pModule, uint32_t *pWords, const char **ppError);

// The class table.
uint16_t bmVmClassName(const BmVmModule *pModule, uint16_t cls);     // string id
uint16_t bmVmClassSprite(const BmVmModule *pModule, uint16_t cls);   // string id or BM_VM_NONE
uint8_t bmVmClassFieldCount(const BmVmModule *pModule, uint16_t cls);
/// Field `field` of a class: name (string id), type (BmScriptType), flags and default value.
void bmVmClassField(const BmVmModule *pModule, uint16_t cls, uint8_t field, uint16_t *pName, uint8_t *pType, uint8_t *pFlags, int32_t *pDefault);
uint8_t bmVmClassStateCount(const BmVmModule *pModule, uint16_t cls);
BmScriptKind bmVmClassKind(const BmVmModule *pModule, uint16_t cls);
uint16_t bmVmStateName(const BmVmModule *pModule, uint16_t cls, uint8_t state);
/// Whether the class (in any state) handles an event.
bool bmVmClassHandles(const BmVmModule *pModule, uint16_t cls, BmScriptEvent event);
/// Source line of code word `word` of a function, or 0.
uint16_t bmVmLine(const BmVmModule *pModule, uint16_t function, uint16_t word);

/// Sets an instance up as a new object of class `cls`: default values, start state.
void bmVmInstanceInit(const BmVmModule *pModule, BmVmInstance *pInstance, uint16_t cls);

/// Calls an event: the current state's version if it has one, else the class's.
void bmVmEvent(const BmVmModule *pModule, const BmVmHost *pHost, BmVmInstance *pInstance, BmScriptEvent event);
/// The same for an event with a parameter (press, release): `arg` is its value.
void bmVmEventWith(const BmVmModule *pModule, const BmVmHost *pHost, BmVmInstance *pInstance, BmScriptEvent event, int32_t arg);

/// Once per frame: counts down sleeps, checks waits and continues the state code.
void bmVmStep(const BmVmModule *pModule, const BmVmHost *pHost, BmVmInstance *pInstance);
