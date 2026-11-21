// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Mondskript: the compiler and the interpreter against a small stand-in for the game.

#include "mondskript.h"

extern "C" {
#include "vm.h"
}

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

int main()
{
  RUN_TEST(testArithmetic);
  RUN_TEST(testControlFlow);
  RUN_TEST(testStatesAndLatentFunctions);
  RUN_TEST(testErrors);
  RUN_TEST(testEndlessLoopIsStopped);
  RUN_TEST(testCorruptModulesAreSafe);
  return TEST_RESULT();
}
