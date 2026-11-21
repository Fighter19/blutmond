// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Native script modules through the real ARM compiler (only built where one is found): what
// the cooker packs loads, and C that could not live in a module is refused.

#include "aot.h"
#include "mondskript.h"
#include "native_module.h"

extern "C" {
#include "native.h"
#include "pack.h"
}

#include "test.h"

#include <cstring>
#include <string>

using namespace bm;

static uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

static std::string failure(const std::string &c)
{
  try
  {
    buildNativeModule(c, {BM_TEST_ARM_CC, "", ""});
  }
  catch (const std::exception &e)
  {
    return e.what();
  }
  return {};
}

static void testScriptBecomesAModule()
{
  script::Module module = script::compile({{"t.ms", R"(class T extends Actor;
var int A;
function int twice(int v) { return v * 2; }
state S { begin: loop { sleep(3); A = twice(A) / 3 + 1; } }
event tick() { if A > 100 { A = A % 7; } }
event touch() { x = x * 1.5; }
defaultproperties { start_state = S; }
)"}}, [](const std::string &) { return uint16_t(0); });
  TEST_ASSERT(module.ok());
  uint32_t crc = bmCrc32(0, module.data.data(), module.data.size());
  std::vector<uint8_t> chunk = buildNativeModule(script::emitC(module, crc), {BM_TEST_ARM_CC, "", ""});
  TEST_ASSERT(chunk.size() > BM_NATIVE_CHUNK_HEADER && rd32(chunk.data()) == BM_NATIVE_MAGIC);
  uint32_t imageSize = rd32(chunk.data() + 8), relocs = rd32(chunk.data() + 12);
  TEST_ASSERT(relocs >= 3);   // at least the three entries
  printf("  %u bytes of bytecode module, %u bytes of ARM code, %u addresses\n", unsigned(module.data.size()), unsigned(imageSize), unsigned(relocs));

  // It loads at any address: the entries then point into the slot.
  std::vector<uint32_t> slot(imageSize / 4 + 1);
  const char *why = nullptr;
  const uint32_t base = 0x03004000;
  TEST_ASSERT(bmVmNativeRelocate(chunk.data(), uint32_t(chunk.size()), slot.data(), uint32_t(slot.size() * 4), crc, BM_NATIVE_TARGET_ARMV4T, base, &why));
  uint32_t functions = slot[1] >> 16, entries = 0;
  TEST_ASSERT(slot[2] == crc && functions == 4);
  for (uint32_t i = 0; i < functions; i++)
  {
    uint32_t e = slot[3 + i];
    if (e == 0) continue;   // twice() takes a value: not an entry
    entries++;
    TEST_ASSERT(e >= base + 12 + functions * 4 && e < base + imageSize && (e & 3) == 0);
  }
  TEST_ASSERT(entries == 3);
  TEST_ASSERT(!bmVmNativeRelocate(chunk.data(), uint32_t(chunk.size()), slot.data(), uint32_t(slot.size() * 4), crc + 1, BM_NATIVE_TARGET_ARMV4T, base, &why));
}

static void testWhatCannotBeAModuleIsRefused()
{
  const std::string head = "#include \"native.h\"\n"
                           "static uint32_t f(BmVmNativeCtx *c, uint32_t pc);\n"
                           "__attribute__((section(\".bmhead\"), used)) const struct { BmVmNativeHeader head; BmVmNativeEntry entries[1]; } bm_native_module = "
                           "{ { BM_NATIVE_MAGIC, BM_NATIVE_VERSION, 1, 0 }, { f } };\n";
  // Fine as it is.
  TEST_ASSERT(failure(head + "static uint32_t f(BmVmNativeCtx *c, uint32_t pc) { (void)c; return pc; }\n").empty());
  // Global variables: the module would not survive being loaded twice or saved with the state.
  TEST_ASSERT(failure(head + "static int counter;\nstatic uint32_t f(BmVmNativeCtx *c, uint32_t pc) { (void)c; return pc + (uint32_t)counter++; }\n").find("global variables") != std::string::npos);
  TEST_ASSERT(failure(head + "static int start = 5;\nstatic uint32_t f(BmVmNativeCtx *c, uint32_t pc) { (void)c; start += 1; return pc + (uint32_t)start; }\n").find("global variables") != std::string::npos);
  // Calls to things outside the module do not link.
  TEST_ASSERT(failure(head + "extern uint32_t elsewhere(uint32_t);\nstatic uint32_t f(BmVmNativeCtx *c, uint32_t pc) { (void)c; return elsewhere(pc); }\n").find("linking failed") != std::string::npos);
  // Without the header at the start it is not a module.
  TEST_ASSERT(failure("#include \"native.h\"\nuint32_t f(BmVmNativeCtx *c, uint32_t pc) { (void)c; return pc; }\n").find("header") != std::string::npos);
  // Not C.
  TEST_ASSERT(failure("this is not C\n").find("compiler failed") != std::string::npos);
  // Files that are not ARM programs.
  std::string what;
  try { packNativeElf({1, 2, 3}); } catch (const std::exception &e) { what = e.what(); }
  TEST_ASSERT(what.find("not an ELF") != std::string::npos);
}

int main()
{
  RUN_TEST(testScriptBecomesAModule);
  RUN_TEST(testWhatCannotBeAModuleIsRefused);
  return TEST_RESULT();
}
