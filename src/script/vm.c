/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "vm.h"

#include <string.h>

#include "native.h"
#include "shared.h"

// Instructions one call may run before the script counts as stuck.
#define BUDGET BM_VM_BUDGET
#define MAX_DEPTH BM_VM_MAX_DEPTH
#define STACK_REGISTERS BM_VM_STACK_REGISTERS

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t bmVmOpLength(uint8_t op)
{
  static const uint8_t lengths[BM_OP_COUNT] = {
    [BM_OP_LOADI] = 2, [BM_OP_LOADK] = 2, [BM_OP_MOVE] = 2, [BM_OP_GETF] = 2, [BM_OP_SETF] = 2,
    [BM_OP_GETV] = 2, [BM_OP_SETV] = 2, [BM_OP_ADD] = 2, [BM_OP_SUB] = 2, [BM_OP_MUL] = 2,
    [BM_OP_DIV] = 2, [BM_OP_MOD] = 2, [BM_OP_FMUL] = 2, [BM_OP_FDIV] = 2, [BM_OP_EQ] = 2,
    [BM_OP_NE] = 2, [BM_OP_LT] = 2, [BM_OP_LE] = 2, [BM_OP_NEG] = 2, [BM_OP_NOT] = 2,
    [BM_OP_JMP] = 2, [BM_OP_JZ] = 2, [BM_OP_JNZ] = 2, [BM_OP_CALLN] = 2, [BM_OP_CALL] = 3,
    [BM_OP_RET] = 1, [BM_OP_GOTOSTATE] = 1, [BM_OP_STOP] = 1,
  };
  return op < BM_OP_COUNT ? lengths[op] : 0;
}

// --- API table ------------------------------------------------------------------------------

typedef struct ApiInfo
{
  uint8_t argc;
  uint8_t latent;
} ApiInfo;

static const ApiInfo s_api[BM_SCRIPT_API_COUNT] = {
#define X(name, result, argc, a0, a1, a2, a3, latent) { argc, latent },
  BM_SCRIPT_API(X)
#undef X
};

// Parameters of each event (0 or 1).
static const uint8_t s_eventArgs[BM_SCRIPT_EVENT_COUNT] = {
#define X(name, arg, kinds) BM_SCRIPT_##arg != BM_SCRIPT_VOID,
  BM_SCRIPT_EVENTS(X)
#undef X
};

// --- Tables ---------------------------------------------------------------------------------

static const uint8_t *classAt(const BmVmModule *m, uint16_t cls)
{
  return m->pClasses + (uint32_t)cls * BM_VM_CLASS_SIZE;
}

static const uint8_t *functionAt(const BmVmModule *m, uint16_t f)
{
  return m->pFunctions + (uint32_t)f * BM_VM_FUNCTION_SIZE;
}

static const uint8_t *stateAt(const BmVmModule *m, uint16_t cls, uint8_t state)
{
  return m->pData + rd32(classAt(m, cls) + BM_VM_CLASS_STATES_AT) + (uint32_t)state * BM_VM_STATE_SIZE;
}

uint16_t bmVmClassName(const BmVmModule *m, uint16_t cls) { return rd16(classAt(m, cls)); }
uint16_t bmVmClassSprite(const BmVmModule *m, uint16_t cls) { return rd16(classAt(m, cls) + 2); }
uint8_t bmVmClassFieldCount(const BmVmModule *m, uint16_t cls) { return classAt(m, cls)[4]; }
uint8_t bmVmClassStateCount(const BmVmModule *m, uint16_t cls) { return classAt(m, cls)[5]; }
BmScriptKind bmVmClassKind(const BmVmModule *m, uint16_t cls) { return (BmScriptKind)classAt(m, cls)[7]; }
uint16_t bmVmStateName(const BmVmModule *m, uint16_t cls, uint8_t state) { return rd16(stateAt(m, cls, state)); }

void bmVmClassField(const BmVmModule *m, uint16_t cls, uint8_t field, uint16_t *pName, uint8_t *pType, uint8_t *pFlags, int32_t *pDefault)
{
  const uint8_t *f = m->pData + rd32(classAt(m, cls) + BM_VM_CLASS_FIELDS_AT) + (uint32_t)field * BM_VM_FIELD_SIZE;
  if (pName) *pName = rd16(f);
  if (pType) *pType = f[2];
  if (pFlags) *pFlags = f[3];
  if (pDefault) *pDefault = (int32_t)rd32(f + 4);
}

bool bmVmClassHandles(const BmVmModule *m, uint16_t cls, BmScriptEvent event)
{
  const uint8_t *c = classAt(m, cls);
  if (rd16(c + 8 + event * 2) != BM_VM_NONE) return true;
  for (uint8_t s = 0; s < c[5]; s++)
  {
    if (rd16(stateAt(m, cls, s) + 4 + event * 2) != BM_VM_NONE) return true;
  }
  return false;
}

uint16_t bmVmLine(const BmVmModule *m, uint16_t function, uint16_t word)
{
  if (m->pLines == NULL || function >= m->functionCount) return 0;
  uint32_t at = rd32(m->pLines + (uint32_t)function * 4);
  if (at == 0 || at + 4 > m->size) return 0;
  const uint8_t *p = m->pData + at;
  uint16_t count = rd16(p + 2), line = 0;
  if (at + 4 + (uint32_t)count * 4 > m->size) return 0;
  for (uint16_t i = 0; i < count; i++)
  {
    if (rd16(p + 4 + i * 4) > word) break;
    line = rd16(p + 6 + i * 4);
  }
  return line;
}

// --- Verification ---------------------------------------------------------------------------

static inline __attribute__((always_inline)) bool verifyFunction(const BmVmModule *m, uint16_t index, const char **ppError)
{
  const uint8_t *f = functionAt(m, index);
  uint32_t start = rd32(f);
  uint16_t words = rd16(f + 4);
  uint8_t regs = f[6], args = f[7], flags = f[8];
  uint16_t cls = rd16(f + 10);
#define FAIL(msg) do { *ppError = msg; return false; } while (0)
  if (start > m->codeWords || words > m->codeWords - start) FAIL("function outside the code");
  if (words == 0) FAIL("empty function");
  if (regs > BM_SCRIPT_MAX_REGISTERS || args > regs || args > BM_SCRIPT_MAX_ARGS) FAIL("bad register count");
  if (cls >= m->classCount) FAIL("function of an unknown class");
  uint8_t fields = bmVmClassFieldCount(m, cls), states = bmVmClassStateCount(m, cls);
  const uint8_t *code = m->pCode + start * 2;

  // First pass: instruction starts, operands.
  uint8_t isStart[512 / 8];
  if (words > 512) FAIL("function too long");
  memset(isStart, 0, sizeof(isStart));
  uint16_t pc = 0;
  uint8_t lastOp = BM_OP_COUNT;
  while (pc < words)
  {
    uint16_t w0 = rd16(code + pc * 2);
    uint8_t op = (uint8_t)w0, a = (uint8_t)(w0 >> 8), length = bmVmOpLength(op);
    if (length == 0) FAIL("unknown instruction");
    if (pc + length > words) FAIL("instruction cut off");
    isStart[pc / 8] |= (uint8_t)(1u << (pc % 8));
    uint16_t w1 = length > 1 ? rd16(code + (pc + 1) * 2) : 0;
    switch (op)
    {
    case BM_OP_LOADI:
      if (a >= regs) FAIL("register out of range");
      break;
    case BM_OP_LOADK:
      if (a >= regs) FAIL("register out of range");
      if (w1 >= m->constCount) FAIL("constant out of range");
      break;
    case BM_OP_MOVE: case BM_OP_NEG: case BM_OP_NOT:
      if (a >= regs || w1 >= regs) FAIL("register out of range");
      break;
    case BM_OP_GETF: case BM_OP_SETF:
      if (a >= regs) FAIL("register out of range");
      if (w1 >= fields) FAIL("field outside the object");
      break;
    case BM_OP_GETV: case BM_OP_SETV:
      if (a >= regs) FAIL("register out of range");
      if (w1 >= BM_SCRIPT_VAR_COUNT) FAIL("unknown actor variable");
      break;
    case BM_OP_ADD: case BM_OP_SUB: case BM_OP_MUL: case BM_OP_DIV: case BM_OP_MOD: case BM_OP_FMUL: case BM_OP_FDIV:
    case BM_OP_EQ: case BM_OP_NE: case BM_OP_LT: case BM_OP_LE:
      if (a >= regs || (w1 & 0xFF) >= regs || (w1 >> 8) >= regs) FAIL("register out of range");
      break;
    case BM_OP_JZ: case BM_OP_JNZ:
      if (a >= regs) FAIL("register out of range");
      break;
    case BM_OP_JMP:
      break;
    case BM_OP_CALLN:
    {
      uint8_t api = (uint8_t)w1, first = (uint8_t)(w1 >> 8);
      if (api >= BM_SCRIPT_API_COUNT) FAIL("unknown runtime function");
      if (a != 0xFF && a >= regs) FAIL("register out of range");
      if (first + s_api[api].argc > regs) FAIL("arguments out of range");
      if (s_api[api].latent && !(flags & BM_VM_FUNCTION_STATE_CODE)) FAIL("latent function outside state code");
      break;
    }
    case BM_OP_CALL:
    {
      uint16_t first = rd16(code + (pc + 2) * 2);
      if (w1 >= m->functionCount) FAIL("call of an unknown function");
      const uint8_t *callee = functionAt(m, w1);
      if (a != 0xFF && a >= regs) FAIL("register out of range");
      if (first + callee[7] > regs) FAIL("arguments out of range");
      if (callee[8] & BM_VM_FUNCTION_STATE_CODE) FAIL("state code cannot be called");
      if (rd16(callee + 10) != cls) FAIL("call into another class");
      break;
    }
    case BM_OP_RET:
      if (a != 0xFF && a >= regs) FAIL("register out of range");
      if (flags & BM_VM_FUNCTION_STATE_CODE) FAIL("return in state code");
      break;
    case BM_OP_GOTOSTATE:
      if (a >= states) FAIL("unknown state");
      break;
    case BM_OP_STOP:
      if (!(flags & BM_VM_FUNCTION_STATE_CODE)) FAIL("stop outside state code");
      break;
    default:
      FAIL("unknown instruction");
    }
    lastOp = op;
    pc = (uint16_t)(pc + length);
  }
  // Running off the end is impossible: the last instruction leaves the function.
  if (lastOp != BM_OP_RET && lastOp != BM_OP_STOP && lastOp != BM_OP_JMP) FAIL("function does not end");

  // Second pass: every jump lands on an instruction.
  pc = 0;
  while (pc < words)
  {
    uint8_t op = (uint8_t)rd16(code + pc * 2);
    if (op == BM_OP_JMP || op == BM_OP_JZ || op == BM_OP_JNZ)
    {
      uint16_t target = rd16(code + (pc + 1) * 2);
      if (target >= words || !(isStart[target / 8] & (1u << (target % 8)))) FAIL("jump into the middle of an instruction");
    }
    pc = (uint16_t)(pc + bmVmOpLength(op));
  }
#undef FAIL
  return true;
}

bool bmVmModuleOpen(BmVmModule *m, const uint8_t *p, uint32_t size, const char **ppError)
{
  return bmVmModuleOpenLazy(m, p, size, ppError) && bmVmModuleVerify(m, ppError);
}

bool bmVmModuleOpenLazy(BmVmModule *m, const uint8_t *p, uint32_t size, const char **ppError)
{
  const char *dummy;
  if (ppError == NULL) ppError = &dummy;
#define FAIL(msg) do { *ppError = msg; return false; } while (0)
  memset(m, 0, sizeof(*m));
  if (p == NULL || size < BM_VM_HEADER_SIZE || memcmp(p, "BMMB", 4) != 0) FAIL("not a script module");
  if (rd16(p + 4) != BM_VM_VERSION) FAIL("script module of another version");
  if (rd16(p + 6) > BM_SCRIPT_API_COUNT) FAIL("the module needs a newer runtime");
  if (rd16(p + 14) != BM_SCRIPT_EVENT_COUNT) FAIL("the module was made for other events");
  m->pData = p;
  m->size = size;
  m->classCount = rd16(p + 8);
  m->functionCount = rd16(p + 10);
  m->constCount = rd16(p + 12);
  uint32_t classes = rd32(p + 16), functions = rd32(p + 20), consts = rd32(p + 24), code = rd32(p + 28), lines = rd32(p + 36);
  m->codeWords = rd32(p + 32);
  if (classes > size || (uint32_t)m->classCount * BM_VM_CLASS_SIZE > size - classes) FAIL("class table outside the module");
  if (functions > size || (uint32_t)m->functionCount * BM_VM_FUNCTION_SIZE > size - functions) FAIL("function table outside the module");
  if (consts > size || (uint32_t)m->constCount * 4 > size - consts) FAIL("constants outside the module");
  // One spare word after the code, so reading an operand word is always inside the module.
  if (code > size || m->codeWords >= (size - code) / 2) FAIL("code outside the module");
  if (lines != 0 && (lines > size || (uint32_t)m->functionCount * 4 > size - lines)) FAIL("line table outside the module");
  m->pClasses = p + classes;
  m->pFunctions = p + functions;
  m->pConsts = p + consts;
  m->pCode = p + code;
  m->pLines = lines != 0 ? p + lines : NULL;

  for (uint16_t c = 0; c < m->classCount; c++)
  {
    const uint8_t *cls = classAt(m, c);
    uint8_t fields = cls[4], states = cls[5], start = cls[6];
    uint32_t fieldsAt = rd32(cls + BM_VM_CLASS_FIELDS_AT), statesAt = rd32(cls + BM_VM_CLASS_STATES_AT);
    if (fields > BM_SCRIPT_MAX_FIELDS) FAIL("too many fields");
    if (cls[7] >= BM_SCRIPT_KIND_COUNT) FAIL("unknown kind of class");
    if (fieldsAt > size || (uint32_t)fields * BM_VM_FIELD_SIZE > size - fieldsAt) FAIL("fields outside the module");
    if (statesAt > size || (uint32_t)states * BM_VM_STATE_SIZE > size - statesAt) FAIL("states outside the module");
    if (start != 0xFF && start >= states) FAIL("unknown start state");
  }
#undef FAIL
  return true;
}

bool bmVmModuleVerify(BmVmModule *m, const char **ppError)
{
  uint32_t all = 0xFFFFFFFFu;
  return bmVmModuleVerifySome(m, &all, ppError);
}

// In IWRAM on the GBA with the function checks it inlines, as Thumb code (half the size of
// ARM code; the room acoustics needed the room): from ROM or EWRAM it was slow enough for the
// checks at a room's entry to miss frames. The game gives it its spare time by an estimate of
// its speed (BM_SPARE_WORDS_PER_LINE in apps/game/main.c).
BM_IWRAM_THUMB bool bmVmModuleVerifySome(BmVmModule *m, uint32_t *pWords, const char **ppError)
{
  const char *dummy;
  if (ppError == NULL) ppError = &dummy;
  if (m->verified == 1) return true;
  if (m->verified == 2) { *ppError = "the module was refused"; return false; }
#define FAIL(msg) do { *ppError = msg; m->verified = 2; return false; } while (0)
  for (; m->verifiedFunctions < m->functionCount; m->verifiedFunctions++)
  {
    // What a function costs is about its length: one that does not fit waits for more room.
    uint32_t words = rd16(functionAt(m, m->verifiedFunctions) + 4) + 8u;
    if (words > *pWords) return true;
    *pWords -= words;
    if (!verifyFunction(m, m->verifiedFunctions, ppError)) { m->verified = 2; return false; }
  }
  if (m->verifiedFunctions < m->functionCount) return true;   // more next time
  // Events and state code must be functions of their class with the right shape.
  for (uint16_t c = 0; c < m->classCount; c++)
  {
    const uint8_t *cls = classAt(m, c);
    for (uint8_t s = 0; s <= cls[5]; s++)
    {
      // s == stateCount stands for the class's own events.
      const uint8_t *events = s < cls[5] ? stateAt(m, c, s) + 4 : cls + 8;
      for (int e = 0; e < BM_SCRIPT_EVENT_COUNT; e++)
      {
        uint16_t fn = rd16(events + e * 2);
        if (fn == BM_VM_NONE) continue;
        if (fn >= m->functionCount) FAIL("event without a function");
        const uint8_t *f = functionAt(m, fn);
        if (rd16(f + 10) != c || f[7] != s_eventArgs[e] || (f[8] & BM_VM_FUNCTION_STATE_CODE)) FAIL("bad event function");
      }
      if (s < cls[5])
      {
        uint16_t fn = rd16(stateAt(m, c, s) + 2);
        if (fn == BM_VM_NONE) continue;
        if (fn >= m->functionCount) FAIL("state without code");
        const uint8_t *f = functionAt(m, fn);
        if (rd16(f + 10) != c || f[7] != 0 || !(f[8] & BM_VM_FUNCTION_STATE_CODE)) FAIL("bad state code");
      }
    }
  }
#undef FAIL
  m->verified = 1;
  return true;
}

// --- Execution ------------------------------------------------------------------------------

void bmVmInstanceInit(const BmVmModule *m, BmVmInstance *inst, uint16_t cls)
{
  memset(inst, 0, sizeof(*inst));
  inst->classIndex = cls;
  inst->state = classAt(m, cls)[6];
  inst->wait = BM_VM_RUNNING;
  for (uint8_t i = 0; i < bmVmClassFieldCount(m, cls); i++)
  {
    bmVmClassField(m, cls, i, NULL, NULL, NULL, &inst->fields[i]);
  }
}

typedef struct Frame
{
  uint16_t function;
  uint16_t pc;
  uint8_t base;      // first register in the stack
  uint8_t result;    // caller's register for the result (0xFF: none)
} Frame;

static void fail(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst, const char *what, uint16_t function, uint16_t pc)
{
  inst->wait = BM_VM_FAILED;
  if (h->error) h->error(h->pContext, inst, what, function, bmVmLine(m, function, pc));
}

// Division as scripts see it: the same in the interpreter and in native code.
static int32_t vmDiv(int32_t n, int32_t d)
{
  return d == 0 || (d == -1 && n == INT32_MIN) ? 0 : n / d;
}

static int32_t vmMod(int32_t n, int32_t d)
{
  return d == 0 || d == -1 ? 0 : n % d;
}

static int32_t vmFdiv(int32_t n, int32_t d)
{
  return d == 0 ? 0 : (int32_t)(((int64_t)n * 256) / d);
}

static void nativeFail(BmVmNativeCtx *c, uint32_t failure, uint32_t function, uint32_t pc)
{
  static const char *const what[] = {
    [BM_VM_FAIL_BUDGET] = "the script runs too long (an endless loop?)",
    [BM_VM_FAIL_DEPTH] = "calls nested too deep",
    [BM_VM_FAIL_INSTRUCTION] = "bad instruction",
  };
  fail(c->pModule, c->pHost, c->pInstance, what[failure <= BM_VM_FAIL_INSTRUCTION ? failure : BM_VM_FAIL_INSTRUCTION], (uint16_t)function, (uint16_t)pc);
}

static const BmVmNativeRt s_nativeRt = { vmDiv, vmMod, vmFdiv, nativeFail };

uint32_t bm_native_calls;

static uint16_t interpret(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst, uint16_t function, uint16_t pc);

// The parameter of the event being started (press, release). Not a parameter of interpret():
// that would cost the interpreter's loop a register.
static int32_t s_eventArg;

// Runs function `function` from word `pc`: as native code if the module has it. Returns the
// word to continue at if a latent function started waiting, else 0xFFFF.
static uint16_t run(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst, uint16_t function, uint16_t pc, int32_t arg)
{
  // A module opened lazily: its code is verified the first time any of it runs.
  if (m->verified != 1)
  {
    const char *why = NULL;
    if (!bmVmModuleVerify((BmVmModule *)m, &why))
    {
      fail(m, h, inst, why != NULL ? why : "the module was refused", BM_VM_NONE, 0);
      return 0xFFFF;
    }
  }
  // Native entries exist only for functions without parameters.
  if (m->pNative != NULL && m->pNative->entries[function] != NULL)
  {
    // One instruction less than the budget: the interpreter stops before the last one.
    BmVmNativeCtx c = { h, inst, &s_nativeRt, m, BUDGET - 1, functionAt(m, function)[6], 0 };
    bm_native_calls++;
    return (uint16_t)m->pNative->entries[function](&c, pc);
  }
  s_eventArg = arg;
  return interpret(m, h, inst, function, pc);
}

bool bmVmModuleSetNative(BmVmModule *m, const BmVmNativeModule *pNative)
{
  if (pNative != NULL && (pNative->head.magic != BM_NATIVE_MAGIC || pNative->head.version != BM_NATIVE_VERSION || pNative->head.functionCount != m->functionCount))
    return false;
  m->pNative = pNative;
  return true;
}

BM_IWRAM_CODE static uint16_t interpret(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst, uint16_t function, uint16_t pc)
{
  int32_t regs[STACK_REGISTERS];
  Frame frames[MAX_DEPTH];
  uint32_t depth = 0, budget = BUDGET;
  uint8_t stateBefore = inst->state;
  const uint8_t *f = functionAt(m, function);
  const uint8_t *code = m->pCode + rd32(f) * 2;
  uint32_t base = 0;
  bool stateCode = (f[8] & BM_VM_FUNCTION_STATE_CODE) != 0;
  memset(regs, 0, (size_t)f[6] * sizeof(int32_t));
  if (f[7] != 0) regs[0] = s_eventArg;   // an event's parameter

  for (;;)
  {
    if (--budget == 0)
    {
      fail(m, h, inst, "the script runs too long (an endless loop?)", function, pc);
      return 0xFFFF;
    }
    uint16_t w0 = rd16(code + pc * 2);
    uint8_t op = (uint8_t)w0, a = (uint8_t)(w0 >> 8);
    uint16_t w1 = rd16(code + (pc + 1) * 2);  // verified: a word follows unless the op is one word long
    int32_t *r = regs + base;
    switch (op)
    {
    case BM_OP_LOADI: r[a] = (int16_t)w1; pc += 2; break;
    case BM_OP_LOADK: r[a] = (int32_t)rd32(m->pConsts + (uint32_t)w1 * 4); pc += 2; break;
    case BM_OP_MOVE: r[a] = r[w1]; pc += 2; break;
    case BM_OP_GETF: r[a] = inst->fields[w1]; pc += 2; break;
    case BM_OP_SETF:
      inst->fields[w1] = r[a];
      if (h->fieldChanged) h->fieldChanged(h->pContext, inst, w1);
      pc += 2;
      break;
    case BM_OP_GETV: r[a] = h->getVar(h->pContext, inst, w1); pc += 2; break;
    case BM_OP_SETV: h->setVar(h->pContext, inst, w1, r[a]); pc += 2; break;
    case BM_OP_ADD: r[a] = (int32_t)((uint32_t)r[w1 & 0xFF] + (uint32_t)r[w1 >> 8]); pc += 2; break;
    case BM_OP_SUB: r[a] = (int32_t)((uint32_t)r[w1 & 0xFF] - (uint32_t)r[w1 >> 8]); pc += 2; break;
    case BM_OP_MUL: r[a] = (int32_t)((uint32_t)r[w1 & 0xFF] * (uint32_t)r[w1 >> 8]); pc += 2; break;
    case BM_OP_DIV: r[a] = vmDiv(r[w1 & 0xFF], r[w1 >> 8]); pc += 2; break;
    case BM_OP_MOD: r[a] = vmMod(r[w1 & 0xFF], r[w1 >> 8]); pc += 2; break;
    case BM_OP_FMUL: r[a] = (int32_t)(((int64_t)r[w1 & 0xFF] * r[w1 >> 8]) >> 8); pc += 2; break;
    case BM_OP_FDIV: r[a] = vmFdiv(r[w1 & 0xFF], r[w1 >> 8]); pc += 2; break;
    case BM_OP_EQ: r[a] = r[w1 & 0xFF] == r[w1 >> 8]; pc += 2; break;
    case BM_OP_NE: r[a] = r[w1 & 0xFF] != r[w1 >> 8]; pc += 2; break;
    case BM_OP_LT: r[a] = r[w1 & 0xFF] < r[w1 >> 8]; pc += 2; break;
    case BM_OP_LE: r[a] = r[w1 & 0xFF] <= r[w1 >> 8]; pc += 2; break;
    case BM_OP_NEG: r[a] = (int32_t)(0u - (uint32_t)r[w1]); pc += 2; break;
    case BM_OP_NOT: r[a] = !r[w1]; pc += 2; break;
    case BM_OP_JMP: pc = w1; break;
    case BM_OP_JZ: pc = r[a] == 0 ? w1 : (uint16_t)(pc + 2); break;
    case BM_OP_JNZ: pc = r[a] != 0 ? w1 : (uint16_t)(pc + 2); break;
    case BM_OP_CALLN:
    {
      int32_t result = 0;
      bool waiting = h->call(h->pContext, inst, w1 & 0xFF, r + (w1 >> 8), &result);
      if (a != 0xFF) r[a] = result;
      pc += 2;
      // Latent functions only exist in state code, which is never called: depth is 0 here.
      if (waiting) return pc;
      if (inst->wait == BM_VM_FAILED) return 0xFFFF;
      break;
    }
    case BM_OP_CALL:
    {
      const uint8_t *callee = functionAt(m, w1);
      uint16_t first = rd16(code + (pc + 2) * 2);
      uint32_t newBase = base + f[6];
      if (depth >= MAX_DEPTH || newBase + callee[6] > STACK_REGISTERS)
      {
        fail(m, h, inst, "calls nested too deep", function, pc);
        return 0xFFFF;
      }
      frames[depth++] = (Frame){ function, (uint16_t)(pc + 3), (uint8_t)base, a };
      memset(regs + newBase, 0, (size_t)callee[6] * sizeof(int32_t));
      for (uint8_t i = 0; i < callee[7]; i++) regs[newBase + i] = r[first + i];
      function = w1;
      f = callee;
      code = m->pCode + rd32(f) * 2;
      base = newBase;
      pc = 0;
      break;
    }
    case BM_OP_RET:
    {
      int32_t value = a != 0xFF ? r[a] : 0;
      if (depth == 0) return 0xFFFF;
      Frame fr = frames[--depth];
      function = fr.function;
      f = functionAt(m, function);
      code = m->pCode + rd32(f) * 2;
      base = fr.base;
      pc = fr.pc;
      if (fr.result != 0xFF) regs[base + fr.result] = value;
      break;
    }
    case BM_OP_GOTOSTATE:
      inst->state = a;
      inst->pc = 0;
      inst->timer = 0;
      inst->wait = BM_VM_RUNNING;
      pc += 1;
      // State code ends here: the new state's code starts at the next step. An event goes on
      // to its end.
      if (stateCode && depth == 0) return 0xFFFF;
      break;
    case BM_OP_STOP:
      if (inst->state == stateBefore) inst->wait = BM_VM_STOPPED;
      return 0xFFFF;
    default:
      fail(m, h, inst, "bad instruction", function, pc);
      return 0xFFFF;
    }
  }
}

void bmVmEvent(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst, BmScriptEvent event)
{
  bmVmEventWith(m, h, inst, event, 0);
}

void bmVmEventWith(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst, BmScriptEvent event, int32_t arg)
{
  if (inst->wait == BM_VM_FAILED || inst->classIndex >= m->classCount) return;
  uint16_t fn = BM_VM_NONE;
  if (inst->state != 0xFF && inst->state < bmVmClassStateCount(m, inst->classIndex))
    fn = rd16(stateAt(m, inst->classIndex, inst->state) + 4 + event * 2);
  if (fn == BM_VM_NONE) fn = rd16(classAt(m, inst->classIndex) + 8 + event * 2);
  if (fn == BM_VM_NONE) return;
  run(m, h, inst, fn, 0, arg);
}

void bmVmStep(const BmVmModule *m, const BmVmHost *h, BmVmInstance *inst)
{
  if (inst->classIndex >= m->classCount) return;
  switch (inst->wait)
  {
  case BM_VM_FAILED: case BM_VM_STOPPED:
    return;
  case BM_VM_SLEEPING:
    if (inst->timer > 1)
    {
      inst->timer--;
      return;
    }
    inst->timer = 0;
    break;
  case BM_VM_FINISH_ANIM:
    if (h->waitOver && !h->waitOver(h->pContext, inst)) return;
    break;
  default:
    break;
  }
  if (inst->state == 0xFF || inst->state >= bmVmClassStateCount(m, inst->classIndex)) return;
  uint16_t fn = rd16(stateAt(m, inst->classIndex, inst->state) + 2);
  if (fn == BM_VM_NONE)
  {
    inst->wait = BM_VM_STOPPED;
    return;
  }
  inst->wait = BM_VM_RUNNING;
  uint8_t state = inst->state;
  uint16_t next = run(m, h, inst, fn, inst->pc, 0);
  // A latent call keeps its place unless the state changed meanwhile.
  if (next != 0xFFFF && inst->state == state) inst->pc = next;
}
