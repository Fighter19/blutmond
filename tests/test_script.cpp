// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Mondskript: the compiler and the interpreter against a small stand-in for the game.

#include "aot.h"
#include "mondskript.h"

extern "C" {
#include "native.h"
#include "vm.h"
}

#ifdef BM_TEST_CC
#include <dlfcn.h>
#include <unistd.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#endif

#include "test.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace bm::script;

// --- A stand-in game ---------------------------------------------------------------------

struct Game
{
  std::vector<int32_t> log;
  int32_t vars[BM_SCRIPT_VAR_COUNT] = {};
  int32_t distance = 1000;
  bool animDone = false;
  std::vector<std::string> errors;
  std::vector<int> errorLines;
  int fieldChanges = 0;
  uint32_t seed = 1;
};

static bool hostCall(void *ctx, BmVmInstance *inst, uint16_t api, const int32_t *args, int32_t *result)
{
  Game &g = *static_cast<Game *>(ctx);
  switch (api)
  {
  case BM_SCRIPT_API_sleep:
    inst->wait = BM_VM_SLEEPING;
    inst->timer = uint16_t(args[0] < 1 ? 1 : args[0]);
    return true;
  case BM_SCRIPT_API_finish_anim:
    inst->wait = BM_VM_FINISH_ANIM;
    return true;
  case BM_SCRIPT_API_log: g.log.push_back(args[0]); break;
  case BM_SCRIPT_API_player_distance: *result = g.distance; break;
  case BM_SCRIPT_API_abs: *result = args[0] < 0 ? -args[0] : args[0]; break;
  case BM_SCRIPT_API_anim_done: *result = g.animDone; break;
  case BM_SCRIPT_API_random:
    g.seed = g.seed * 1664525u + 1013904223u;
    *result = args[0] > 0 ? int32_t((g.seed >> 8) % uint32_t(args[0])) : 0;
    break;
  default: break;
  }
  return false;
}
static int32_t hostGet(void *ctx, BmVmInstance *, uint16_t var) { return static_cast<Game *>(ctx)->vars[var]; }
static void hostSet(void *ctx, BmVmInstance *, uint16_t var, int32_t value) { static_cast<Game *>(ctx)->vars[var] = value; }
static void hostField(void *ctx, BmVmInstance *, uint16_t) { static_cast<Game *>(ctx)->fieldChanges++; }
static bool hostWaitOver(void *ctx, BmVmInstance *) { return static_cast<Game *>(ctx)->animDone; }
static void hostError(void *ctx, BmVmInstance *, const char *what, uint16_t, uint16_t line)
{
  static_cast<Game *>(ctx)->errors.push_back(what);
  static_cast<Game *>(ctx)->errorLines.push_back(line);
}

static std::vector<std::string> s_strings;
static uint16_t stringId(const std::string &s)
{
  for (size_t i = 0; i < s_strings.size(); i++)
    if (s_strings[i] == s) return uint16_t(i);
  s_strings.push_back(s);
  return uint16_t(s_strings.size() - 1);
}

static Module build(const std::string &text)
{
  return compile({{"test.ms", text}}, stringId);
}

// With s_native set, every Run also turns its module into C, compiles that with the host's
// compiler and runs the functions as native code: the same tests must pass either way.
static bool s_native = false;
static int s_nativeModules = 0;

#ifdef BM_TEST_CC
static uint32_t crc32(const std::vector<uint8_t> &data)
{
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t b : data)
  {
    crc ^= b;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

static const BmVmNativeModule *compileNative(const Module &module)
{
  static int s_files = 0;
  std::string base = (std::filesystem::temp_directory_path() / ("bm_test_native_" + std::to_string(getpid()) + "_" + std::to_string(s_files++))).string();
  std::string c = emitC(module, crc32(module.data));
  std::ofstream(base + ".c") << c;
  std::string command = std::string(BM_TEST_CC) + " -shared -fPIC -O1 -Wall -Werror -Wno-unused-label -I" BM_TEST_SCRIPT_INCLUDE " -I" BM_TEST_CORE_INCLUDE
                        " -DBM_PLATFORM_HOST -o " + base + ".so " + base + ".c";
  const BmVmNativeModule *native = nullptr;
  if (std::system(command.c_str()) == 0)
  {
    void *lib = dlopen((base + ".so").c_str(), RTLD_NOW);
    if (lib) native = static_cast<const BmVmNativeModule *>(dlsym(lib, "bm_native_module"));
    if (!native) printf("  cannot load %s.so: %s\n", base.c_str(), dlerror());
  }
  else printf("  the C made from the script does not compile: %s.c\n", base.c_str());
  if (native)
  {
    std::remove((base + ".c").c_str());
    std::remove((base + ".so").c_str());
    s_nativeModules++;
  }
  return native;
}
#endif

struct Run
{
  Module module;
  BmVmModule vm;
  BmVmInstance inst;
  Game game;
  BmVmHost host;
  bool ok = false;

  explicit Run(const std::string &text) : module(build(text))
  {
    host = {&game, hostCall, hostGet, hostSet, hostField, hostWaitOver, hostError};
    if (!module.ok())
    {
      for (const Diagnostic &d : module.diagnostics) printf("  %s:%d:%d: %s\n", d.file.c_str(), d.line, d.column, d.message.c_str());
      return;
    }
    const char *why = nullptr;
    ok = bmVmModuleOpen(&vm, module.data.data(), uint32_t(module.data.size()), &why);
#ifdef BM_TEST_CC
    if (ok && s_native)
    {
      const BmVmNativeModule *native = compileNative(module);
      ok = native != nullptr && native->head.bytecodeCrc == crc32(module.data) && bmVmModuleSetNative(&vm, native);
    }
#endif
    if (ok) bmVmInstanceInit(&vm, &inst, 0);
  }
  void event(BmScriptEvent e) { bmVmEvent(&vm, &host, &inst, e); }
  void step(int frames = 1)
  {
    for (int i = 0; i < frames; i++) bmVmStep(&vm, &host, &inst);
  }
};

static std::string firstError(const std::string &text, int *pLine = nullptr)
{
  Module m = build(text);
  for (const Diagnostic &d : m.diagnostics)
    if (!d.warning)
    {
      if (pLine) *pLine = d.line;
      return d.message;
    }
  return {};
}

// --- Tests -------------------------------------------------------------------------------

static void testArithmetic()
{
  Run r(R"(class T extends Actor;
var int A = 7;
var fix F = 1.5;
const int TEN = 10;
function int twice(int v) { return v * 2; }
function fix half(fix v) { return v / 2; }
event spawn() {
  log(A + 3 * 2);            // 13: precedence
  log((A + 3) * 2);          // 20
  log(twice(A) - TEN);       // 4
  log(-A % 4);               // -3: the remainder keeps the sign of the left side
  log(A / 0);                // 0: defined
  log(int(F * 2));           // 3: fix times a literal
  log(int(half(F) * 4));     // 3
  log(int(fix(A) / 2 * 2));  // 7: 3.5 * 2
  log(int(F + 1));           // 2: the literal becomes fix
  var int big = 100000;
  log(big * 3);              // 300000: constants beyond 16 bits
  var bool b = A > 5 && F < 2.0 || false;
  if b { log(1); } else { log(0); }
  if (!b || A == 7) { log(2); }
  A += 5; A -= 2; A *= 3;    // 30
  log(A);
  F *= 2;                    // 3.0
  log(int(F));
}
)");
  TEST_ASSERT(r.ok);
  r.event(BM_SCRIPT_EVENT_spawn);
  std::vector<int32_t> expected = {13, 20, 4, -3, 0, 3, 3, 7, 2, 300000, 1, 2, 30, 3};
  if (r.game.log != expected)
  {
    printf("  got:");
    for (int32_t v : r.game.log) printf(" %d", int(v));
    printf("\n");
  }
  TEST_ASSERT(r.game.log == expected);
  TEST_ASSERT(r.game.errors.empty());
  TEST_ASSERT(r.game.fieldChanges == 4);
}

static void testControlFlow()
{
  Run r(R"(class T extends Actor;
var int Sum;
function int fib(int n) {
  if n < 2 { return n; }
  return fib(n - 1) + fib(n - 2);
}
event tick() {
  Sum = 0;
  for i in 0..10 {
    if i == 3 { continue; }
    if i == 8 { break; }
    Sum += i;
  }
  log(Sum);                  // 0+1+2+4+5+6+7 = 25
  var int n = 0;
  while n < 5 { n += 2; }
  log(n);                    // 6
  loop { n -= 1; if n <= 0 { break; } }
  log(n);
  log(fib(10));              // 55: calls nest
  x = x + 2.5;
  facing = -1;
}
)");
  TEST_ASSERT(r.ok);
  r.event(BM_SCRIPT_EVENT_tick);
  if (r.game.log != std::vector<int32_t>{25, 6, 0, 55})
  {
    printf("  got:");
    for (int32_t v : r.game.log) printf(" %d", int(v));
    for (const std::string &e : r.game.errors) printf(" [%s]", e.c_str());
    printf("\n");
  }
  TEST_ASSERT((r.game.log == std::vector<int32_t>{25, 6, 0, 55}));
  TEST_ASSERT(r.game.vars[BM_SCRIPT_VAR_x] == 640);
  TEST_ASSERT(r.game.vars[BM_SCRIPT_VAR_facing] == -1);
}

static void testStatesAndLatentFunctions()
{
  Run r(R"(class Bat extends Actor;
var(Combat) int Damage = 2;
var int Woken;
state Sleeping {
  event tick() { if player_distance() < 48 { gotostate Awake; } }
  begin:
    log(1);
    loop { sleep(10); log(2); }
}
state Awake {
  event touch() { log(Damage); }
  begin:
    Woken += 1;
    log(3);
    finish_anim();
    log(4);
    wait_until(player_distance() > 100);
    log(5);
    gotostate Sleeping;
}
defaultproperties {
  Damage = 5;
  start_state = Sleeping;
  sprite = "bat";
}
)");
  TEST_ASSERT(r.ok);
  TEST_ASSERT(r.module.classes[0].sprite == "bat");
  TEST_ASSERT(r.module.classes[0].fields[0].category == "Combat");
  TEST_ASSERT(r.inst.fields[0] == 5);                       // defaultproperties wins over the initialiser
  TEST_ASSERT(r.inst.state == 0);

  r.step();                                                 // log 1, then sleeping
  TEST_ASSERT((r.game.log == std::vector<int32_t>{1}));
  TEST_ASSERT(r.inst.wait == BM_VM_SLEEPING);
  r.step(9);
  TEST_ASSERT(r.game.log.size() == 1);                      // 10 frames are not over
  r.step();
  TEST_ASSERT((r.game.log == std::vector<int32_t>{1, 2}));   // woke up, logged, sleeps again
  r.event(BM_SCRIPT_EVENT_touch);                           // not handled in this state: nothing
  TEST_ASSERT(r.game.log.size() == 2);

  r.game.distance = 20;
  r.event(BM_SCRIPT_EVENT_tick);                            // the state's tick: wake up
  TEST_ASSERT(r.inst.state == 1 && r.inst.pc == 0);
  r.step();                                                 // Awake's code from the start
  TEST_ASSERT(r.inst.fields[1] == 1);
  TEST_ASSERT(r.game.log.back() == 3 && r.inst.wait == BM_VM_FINISH_ANIM);
  r.step(5);
  TEST_ASSERT(r.game.log.back() == 3);                      // the animation is not done
  r.event(BM_SCRIPT_EVENT_touch);
  TEST_ASSERT(r.game.log.back() == 5);                      // Awake's touch: Damage
  r.game.animDone = true;
  r.step();
  TEST_ASSERT(r.game.log.back() == 4);                      // now waiting for the player to leave
  r.step(20);
  TEST_ASSERT(r.game.log.back() == 4);
  r.game.distance = 200;
  r.step(2);
  TEST_ASSERT(r.inst.state == 0);                           // back asleep
  r.step();
  TEST_ASSERT(r.game.log.back() == 1);

  // An instance is plain data: a copy continues exactly where the original was.
  BmVmInstance copy = r.inst;
  size_t logged = r.game.log.size();
  r.step(10);
  std::vector<int32_t> first(r.game.log.begin() + long(logged), r.game.log.end());
  r.inst = copy;
  r.game.log.resize(logged);
  r.step(10);
  TEST_ASSERT(std::vector<int32_t>(r.game.log.begin() + long(logged), r.game.log.end()) == first);
}

static void testErrors()
{
  int line = 0;
  TEST_ASSERT(firstError("var int x;").find("class Name extends Actor") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() {\n  sleep(3);\n}\n", &line).find("takes time") != std::string::npos);
  TEST_ASSERT(line == 3);
  TEST_ASSERT(firstError("class T extends Actor;\nvar int A;\nvar fix F;\nevent tick() { A = A + F; }\n", &line).find("convert one side") != std::string::npos);
  TEST_ASSERT(line == 4);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { y = nothere; }\n").find("'nothere' is not known") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { home_x = 1; }\n").find("cannot be changed") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent explode() { }\n").find("unknown event") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { gotostate Nowhere; }\n").find("no state 'Nowhere'") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { if 3 { } }\n").find("condition must be a bool") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { log(1, 2); }\n").find("takes 1 values") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { log(1) }\n").find("expected ';'") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nstate S { begin: var int i = 0; }\n").find("local variables") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\ndefaultproperties { start_state = Gone; }\n").find("no state 'Gone'") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nvar int a;var int b;var int c;var int d;var int e;var int f;var int g;var int h;var int i;\n").find("at most") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nfunction int f() { return 1.5; }\n").find("must be int") != std::string::npos);
  TEST_ASSERT(firstError("class T extends Actor;\nevent tick() { /* open\n").find("comment without an end") != std::string::npos);
  // Two files with the same class.
  Module m = compile({{"a.ms", "class T extends Actor;"}, {"b.ms", "class T extends Actor;"}}, stringId);
  TEST_ASSERT(!m.ok() && m.diagnostics[0].file == "b.ms");
  // A file with an error does not stop the others from being reported.
  m = compile({{"a.ms", "class A extends Actor; var int"}, {"b.ms", "class B extends Actor; event tick() { oops(); }"}}, stringId);
  TEST_ASSERT(m.diagnostics.size() == 2 && m.data.empty());
}

static const StructInfo *findStruct(const Module &m, const std::string &name)
{
  for (const StructInfo &s : m.structs)
    if (s.name == name) return &s;
  return nullptr;
}

static const StructField *findField(const StructInfo &s, const std::string &name)
{
  for (const StructField &f : s.fields)
    if (f.name == name) return &f;
  return nullptr;
}

static int32_t readLe(const std::vector<uint8_t> &d, size_t at, int size, bool sign)
{
  uint32_t v = 0;
  for (int i = 0; i < size; i++) v |= uint32_t(d[at + size_t(i)]) << (8 * i);
  if (sign && size < 4 && (v >> (8 * size - 1)) & 1) v |= ~0u << (8 * size);
  return int32_t(v);
}

// Enums in classes: variables, values written Element.Fire (bare where the type is known),
// comparisons; mixing two enums or an enum and a number is an error.
static void testEnums()
{
  Run r(R"(enum Element { None, Fire, Ice, Holy }
enum Mood { Calm, Angry }
class T extends Actor;
var Element E = Ice;
var Mood M;
event spawn() {
  log(E == Element.Ice ? 1 : 0);
}
)");
  // No '?:' in the language: the error says where.
  TEST_ASSERT(!r.ok);
  Run s(R"(enum Element { None, Fire, Ice, Holy }
enum Mood { Calm, Angry }
class T extends Actor;
var Element E = Ice;
var Mood M = Mood.Angry;
const int ONE = 1;
function bool is_fire(Element e) { return e == Element.Fire; }
event spawn() {
  if E == Element.Ice { log(1); }
  E = Element.Fire;
  if is_fire(E) { log(2); }
  var Element local = Element.Holy;
  if local != E { log(3); }
  if M == Mood.Angry { log(4); }
}
)");
  TEST_ASSERT(s.ok);
  TEST_ASSERT(s.module.enums.size() == 2 && s.module.enums[0].values.size() == 4);
  TEST_ASSERT(s.inst.fields[0] == 2 && s.inst.fields[1] == 1);   // defaults: Ice, Angry
  TEST_ASSERT(s.module.classes[0].fields[0].typeLabel == "Element");
  s.event(BM_SCRIPT_EVENT_spawn);
  TEST_ASSERT((s.game.log == std::vector<int32_t>{1, 2, 3, 4}));
  TEST_ASSERT(s.game.errors.empty());

  const char *decl = "enum Element { None, Fire }\nenum Mood { Calm, Angry }\nclass T extends Actor;\nvar Element E;\nvar Mood M;\n";
  TEST_ASSERT(firstError(std::string(decl) + "event tick() { E = M; }\n").find("must be Element, this is Mood") != std::string::npos);
  TEST_ASSERT(firstError(std::string(decl) + "event tick() { E = 1; }\n").find("must be Element, this is int") != std::string::npos);
  TEST_ASSERT(firstError(std::string(decl) + "event tick() { if E == M { } }\n").find("between Element and Mood") != std::string::npos);
  TEST_ASSERT(firstError(std::string(decl) + "event tick() { E = Element.Ice; }\n").find("'Ice' is not a value of Element") != std::string::npos);
  TEST_ASSERT(firstError(std::string(decl) + "event tick() { if E < Element.Fire { } }\n").find("needs numbers") != std::string::npos);
  TEST_ASSERT(firstError("enum E { A, A }\nclass T extends Actor;\n").find("twice") != std::string::npos);
  TEST_ASSERT(firstError("enum E { }\nclass T extends Actor;\n").find("at least one value") != std::string::npos);
}

// Structures: layout in declaration order with alignment, a base structure's fields in front,
// sizes a multiple of 4, defaults; across files and in any order of declaration.
static void testStructures()
{
  Module m = compile({{"items.ms", R"(
struct Item {
  text  title = "Nothing";
  name  icon;
  short price = 10;
  ubyte max_stack = 1;
}
struct Weapon is Item {
  int      attack    = 1;
  byte     reach     = -3;
  Element  element   = Fire;
  name     swing     = #Slash;
  sound    sfx;
  attach   behaviour = WeaponSwing;
  fix      speed     = 1.5;
  record<Weapon> upgrade = #FlameBrand;
  Stats    bonus;
  short[3] combo     = 7;
}
)"},
                      {"types.ms", R"(
enum Element { None, Fire, Ice }
struct Stats { byte strength = 2; byte luck; }
)"}},
                     stringId);
  for (const Diagnostic &d : m.diagnostics) printf("  %s:%d: %s\n", d.file.c_str(), d.line, d.message.c_str());
  TEST_ASSERT(m.ok());
  TEST_ASSERT(m.classes.empty() && m.structs.size() == 3);   // files without a class are fine
  const StructInfo *item = findStruct(m, "Item"), *weapon = findStruct(m, "Weapon"), *stats = findStruct(m, "Stats");
  TEST_ASSERT(item && weapon && stats);
  if (!item || !weapon || !stats) return;
  // Item: title 0, icon 2, price 4, max_stack 6, size 8.
  TEST_ASSERT(item->size == 8 && findField(*item, "max_stack")->offset == 6);
  TEST_ASSERT(stats->size == 4 && stats->defaults[0] == 2);
  // Weapon: Item's 8 bytes, attack 8, reach 12, element 13, swing 14, sfx 16, behaviour 18,
  // speed 20, upgrade 24, bonus 28 (Stats, padded to 4 bytes), combo 32 (3 x 2), end 38, size 40.
  struct { const char *name; int offset; BmDataType type; } expected[] = {
      {"title", 0, BM_DATA_TEXT},    {"attack", 8, BM_DATA_INT},     {"reach", 12, BM_DATA_BYTE},  {"element", 13, BM_DATA_ENUM},
      {"swing", 14, BM_DATA_NAME},   {"sfx", 16, BM_DATA_SOUND},     {"behaviour", 18, BM_DATA_ATTACH}, {"speed", 20, BM_DATA_FIX},
      {"upgrade", 24, BM_DATA_RECORD}, {"bonus", 28, BM_DATA_STRUCT}, {"combo", 32, BM_DATA_SHORT}};
  for (const auto &e : expected)
  {
    const StructField *f = findField(*weapon, e.name);
    TEST_ASSERT(f != nullptr);
    if (!f) continue;
    if (f->offset != e.offset || f->type != e.type) printf("  %s at %d, type %s\n", e.name, f->offset, dataTypeName(f->type));
    TEST_ASSERT(f->offset == e.offset && f->type == e.type);
  }
  TEST_ASSERT(weapon->size == 40 && weapon->fields.size() == 14 && weapon->fields[0].inherited && !weapon->fields[4].inherited);
  TEST_ASSERT(findField(*weapon, "combo")->count == 3 && findField(*weapon, "combo")->typeLabel == "short[3]");
  TEST_ASSERT(findField(*weapon, "upgrade")->detail == "Weapon" && findField(*weapon, "element")->detail == "Element");
  // Defaults.
  const std::vector<uint8_t> &d = weapon->defaults;
  TEST_ASSERT(d.size() == 40);
  TEST_ASSERT(s_strings[size_t(readLe(d, 0, 2, false))] == "Nothing");
  TEST_ASSERT(readLe(d, 4, 2, true) == 10 && d[6] == 1);
  TEST_ASSERT(readLe(d, 8, 4, true) == 1 && readLe(d, 12, 1, true) == -3 && d[13] == 1);
  TEST_ASSERT(s_strings[size_t(readLe(d, 14, 2, false))] == "Slash");
  TEST_ASSERT(readLe(d, 16, 2, false) == BM_DATA_NONE_ID);
  TEST_ASSERT(s_strings[size_t(readLe(d, 18, 2, false))] == "WeaponSwing");
  TEST_ASSERT(readLe(d, 20, 4, true) == 384);
  TEST_ASSERT(uint32_t(readLe(d, 24, 4, false)) == nameKey("FlameBrand"));
  TEST_ASSERT(d[28] == 2 && d[29] == 0);                       // Stats' own defaults
  TEST_ASSERT(readLe(d, 32, 2, true) == 7 && readLe(d, 36, 2, true) == 7 && d[38] == 0);
  TEST_ASSERT(nameKey("") == 2166136261u && nameKey("a") == 0xe40c292cu);   // FNV-1a, as src/world/actors.c

  // Errors.
  auto err = [](const std::string &text) {
    Module e = compile({{"s.ms", text}}, stringId);
    return e.diagnostics.empty() ? std::string() : e.diagnostics[0].message;
  };
  TEST_ASSERT(err("struct A { B b; }\nstruct B { A a; }\n").find("contains itself") != std::string::npos);
  TEST_ASSERT(err("struct A { int[17] big; }\n").find("grows beyond 64 bytes") != std::string::npos);
  TEST_ASSERT(err("struct A { byte b = 300; }\n").find("does not fit") != std::string::npos);
  TEST_ASSERT(err("struct A { float f; }\n").find("not a type") != std::string::npos);
  TEST_ASSERT(err("struct A { int x; }\nstruct B is A { int x; }\n").find("exists already") != std::string::npos);
  TEST_ASSERT(err("struct A is Nope { }\n").find("no structure 'Nope'") != std::string::npos);
  TEST_ASSERT(err("struct A { record<Nope> r; }\n").find("no structure 'Nope'") != std::string::npos);
  TEST_ASSERT(err("enum E { X }\nstruct A { E e = Y; }\n").find("'Y' is not a value of E") != std::string::npos);
  TEST_ASSERT(err("struct A { int x; }\nstruct A { int y; }\n").find("declared twice") != std::string::npos);
  std::string many = "struct A {";
  for (int i = 0; i < 33; i++) many += " bool f" + std::to_string(i) + ";";
  TEST_ASSERT(err(many + " }\n").find("at most 32 fields") != std::string::npos);
}

static void testEndlessLoopIsStopped()
{
  Run r("class T extends Actor;\nevent tick() {\n  loop {\n    x = x + 1.0;\n  }\n}\n");
  TEST_ASSERT(r.ok);
  r.event(BM_SCRIPT_EVENT_tick);
  TEST_ASSERT(r.game.errors.size() == 1);
  TEST_ASSERT(r.game.errors[0].find("too long") != std::string::npos);
  TEST_ASSERT(r.game.errorLines[0] >= 3 && r.game.errorLines[0] <= 5);
  TEST_ASSERT(r.inst.wait == BM_VM_FAILED);
  r.event(BM_SCRIPT_EVENT_tick);                            // a failed object does nothing more
  TEST_ASSERT(r.game.errors.size() == 1);
  // Recursion without an end is stopped too.
  Run deep("class T extends Actor;\nfunction int f(int n) { return f(n + 1); }\nevent tick() { log(f(0)); }\n");
  TEST_ASSERT(deep.ok);
  deep.event(BM_SCRIPT_EVENT_tick);
  TEST_ASSERT(deep.game.errors.size() == 1 && deep.game.errors[0].find("nested too deep") != std::string::npos);
}

// Whatever bytes a module holds, the verifier either refuses it or running it stays inside the
// object, the registers and the code (the sanitizer build would catch anything else).
static void testCorruptModulesAreSafe()
{
  Run r(R"(class T extends Actor;
var int A = 1;
var fix F = 2.5;
function int f(int a, int b) { return a * b + A; }
state S {
  event touch() { A = f(A, 3); }
  begin:
    loop { sleep(2); A += 1; if A > 40 { gotostate U; } }
}
state U { begin: x = x + F; sleep(1); gotostate S; }
event tick() { if abs(A) > 3 && random(4) == 1 { log(A); } y = y - F; }
defaultproperties { start_state = S; }
)");
  TEST_ASSERT(r.ok);
  std::vector<uint8_t> original = r.module.data;
  uint32_t seed = 12345;
  auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
  int accepted = 0, refused = 0;
  for (int round = 0; round < 4000; round++)
  {
    std::vector<uint8_t> data = original;
    int changes = 1 + int(rnd() % 3);
    for (int k = 0; k < changes; k++) data[rnd() % data.size()] = uint8_t(rnd());
    if (round % 16 == 0) data.resize(data.size() - (rnd() % 24) - 1);  // cut short
    BmVmModule vm;
    const char *why = nullptr;
    if (!bmVmModuleOpen(&vm, data.data(), uint32_t(data.size()), &why))
    {
      refused++;
      continue;
    }
    accepted++;
    Game game;
    BmVmHost host = {&game, hostCall, hostGet, hostSet, hostField, hostWaitOver, hostError};
    for (uint16_t cls = 0; cls < vm.classCount; cls++)
    {
      BmVmInstance inst;
      bmVmInstanceInit(&vm, &inst, cls);
      for (int frame = 0; frame < 30; frame++)
      {
        bmVmEvent(&vm, &host, &inst, BmScriptEvent(frame % BM_SCRIPT_EVENT_COUNT));
        bmVmStep(&vm, &host, &inst);
      }
    }
  }
  printf("  %d changed modules refused, %d accepted and run\n", refused, accepted);
  TEST_ASSERT(refused > 100 && accepted > 100);
}

// The loader's checks, on a module image made by hand for the GBA's machine.
static void testNativeLoaderChecks()
{
  auto put32 = [](std::vector<uint8_t> &b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back(uint8_t(v >> (k * 8))); };
  // Image: header (magic, version 1, 2 functions, crc), two entries, 8 bytes of "code".
  std::vector<uint8_t> image;
  put32(image, BM_NATIVE_MAGIC);
  put32(image, BM_NATIVE_VERSION | (2u << 16));
  put32(image, 0xC0FFEE);
  put32(image, 20);          // entry 0: the code at offset 20
  put32(image, 0);           // entry 1: none
  put32(image, 0xE12FFF1E);  // bx lr
  put32(image, 12);          // an address inside the image
  auto chunk = [&](const std::vector<uint8_t> &img, std::vector<uint32_t> relocs, uint32_t target = BM_NATIVE_TARGET_ARMV4T) {
    std::vector<uint8_t> c;
    put32(c, BM_NATIVE_MAGIC);
    put32(c, BM_NATIVE_VERSION | (target << 16));
    put32(c, uint32_t(img.size()));
    put32(c, uint32_t(relocs.size()));
    c.insert(c.end(), img.begin(), img.end());
    for (uint32_t r : relocs) put32(c, r);
    return c;
  };
  uint32_t slot[16] = {};
  const char *why = nullptr;
  auto load = [&](const std::vector<uint8_t> &c, uint32_t crc = 0xC0FFEE, uint32_t slotSize = sizeof(slot)) {
    why = nullptr;
    return bmVmNativeRelocate(c.data(), uint32_t(c.size()), slot, slotSize, crc, BM_NATIVE_TARGET_ARMV4T, 0x03000100, &why);
  };
  TEST_ASSERT(load(chunk(image, {12, 24})));
  TEST_ASSERT(slot[3] == 0x03000100 + 20 && slot[6] == 0x03000100 + 12 && slot[4] == 0 && slot[5] == 0xE12FFF1E);
  TEST_ASSERT(!load(chunk(image, {12, 24}), 0xBAD) && std::string(why).find("other scripts") != std::string::npos);
  TEST_ASSERT(!load(chunk(image, {12, 24}), 0xC0FFEE, 16) && std::string(why).find("too big") != std::string::npos);
  TEST_ASSERT(!load(chunk(image, {12, 24}, BM_NATIVE_TARGET_NONE)) && std::string(why).find("another machine") != std::string::npos);
  TEST_ASSERT(!load(chunk(image, {12, 28})) && std::string(why).find("relocation outside") != std::string::npos);
  TEST_ASSERT(!load(chunk(image, {12, 22})));                  // not on a word
  TEST_ASSERT(!load(chunk(image, {24})) && std::string(why).find("entry outside") != std::string::npos);   // the entry is not relocated
  std::vector<uint8_t> wild = image;
  wild[12] = 4;                                                // entry 0 points into the header
  TEST_ASSERT(!load(chunk(wild, {12, 24})));
  wild = image;
  wild[24] = 200;                                              // an address beyond the image
  TEST_ASSERT(!load(chunk(wild, {12, 24})) && std::string(why).find("to outside") != std::string::npos);
  std::vector<uint8_t> cut = chunk(image, {12, 24});
  cut.resize(cut.size() - 5);
  TEST_ASSERT(!load(cut) && std::string(why).find("cut off") != std::string::npos);
  // On this machine nothing native can be loaded from a pack.
  std::vector<uint8_t> good = chunk(image, {12, 24});
  TEST_ASSERT(bmVmNativeLoad(good.data(), uint32_t(good.size()), slot, sizeof(slot), 0xC0FFEE, &why) == nullptr);
  // A native module for other bytecode is not accepted by a module.
  Run r("class T extends Actor;\nevent tick() { log(1); }\n");
  TEST_ASSERT(r.ok);
  BmVmNativeModule other = {{BM_NATIVE_MAGIC, BM_NATIVE_VERSION, uint16_t(r.vm.functionCount + 1), 0}, {nullptr}};
  TEST_ASSERT(!bmVmModuleSetNative(&r.vm, &other));
}

#ifdef BM_TEST_CC
// The same object run both ways, frame by frame: fields, waits, places to continue, what it
// called and what it changed must be the same at every step.
static void testNativeMatchesInterpreter()
{
  const char *text = R"(class T extends Actor;
var int A = 3;
var fix F = 0.5;
var save int Kept;
function int f(int a, int b) { if a > b { return a - b; } return f(a + 1, b) + 1; }
function fix g(fix v) { return v * F / 3 + fix(A % 7); }
state S {
  event touch() { A = f(A, A + 5) - 50; Kept += 1; }
  begin:
    loop { sleep(2); A += 1; if A > 40 { gotostate U; } }
}
state U {
  event tick() { x = g(x) - y; }
  begin:
    x = x + F; finish_anim(); sleep(3); log(int(x)); A = A / 3 - 20; gotostate S;
}
event tick() { if abs(A) > 3 && random(4) == 1 { log(A); } y = y - F; facing = -facing + A % 3; }
defaultproperties { start_state = S; }
)";
  s_native = false;
  Run a(text);
  s_native = true;
  Run b(text);
  TEST_ASSERT(a.ok && b.ok && a.vm.pNative == nullptr && b.vm.pNative != nullptr);
  uint32_t before = bm_native_calls;
  for (int frame = 0; frame < 600 && g_testFailures == 0; frame++)
  {
    for (Run *r : {&a, &b})
    {
      r->game.animDone = frame % 5 == 0;
      r->event(BM_SCRIPT_EVENT_tick);
      if (frame % 37 == 11) r->event(BM_SCRIPT_EVENT_touch);
      r->step();
    }
    TEST_ASSERT(std::memcmp(&a.inst, &b.inst, sizeof(a.inst)) == 0);
    TEST_ASSERT(a.game.log == b.game.log && std::memcmp(a.game.vars, b.game.vars, sizeof(a.game.vars)) == 0);
    TEST_ASSERT(a.game.fieldChanges == b.game.fieldChanges && a.game.seed == b.game.seed);
  }
  TEST_ASSERT(a.game.errors.empty() && b.game.errors.empty());
  TEST_ASSERT(a.game.log.size() > 20);
  TEST_ASSERT(bm_native_calls - before > 600);
}
#endif

int main()
{
  RUN_TEST(testArithmetic);
  RUN_TEST(testControlFlow);
  RUN_TEST(testStatesAndLatentFunctions);
  RUN_TEST(testErrors);
  RUN_TEST(testEnums);
  RUN_TEST(testStructures);
  RUN_TEST(testEndlessLoopIsStopped);
  RUN_TEST(testCorruptModulesAreSafe);
  RUN_TEST(testNativeLoaderChecks);
#ifdef BM_TEST_CC
  // Once more as native code.
  s_native = true;
  RUN_TEST(testArithmetic);
  RUN_TEST(testControlFlow);
  RUN_TEST(testStatesAndLatentFunctions);
  RUN_TEST(testEnums);
  RUN_TEST(testEndlessLoopIsStopped);
  RUN_TEST(testNativeMatchesInterpreter);
  printf("  %d modules compiled to native code\n", s_nativeModules);
#endif
  return TEST_RESULT();
}
