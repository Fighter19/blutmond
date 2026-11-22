// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Records (docs/plans/structures.md steps 1 and 2): .bmdata files cooked into DTAB chunks
// against the scripts' structures, read in place by src/data, and read by scripts through
// record<S>. Argument: a scratch folder.

#include "data_asset.h"
#include "mondskript.h"

#include "test.h"

extern "C" {
#include "data.h"
#include "pack.h"
#include "vm.h"
}

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using namespace bm;

static std::string s_work;
static std::vector<std::string> s_strings;

static uint16_t stringId(const std::string &s)
{
  for (size_t i = 0; i < s_strings.size(); i++)
    if (s_strings[i] == s) return uint16_t(i);
  s_strings.push_back(s);
  return uint16_t(s_strings.size() - 1);
}

static std::string write(const std::string &name, const std::string &text)
{
  std::string path = s_work + "/" + name;
  std::ofstream(path, std::ios::binary) << text;
  return path;
}

static const char *kStructures = R"(
enum Element { None, Fire, Ice }
struct Stats { byte strength; byte luck = 1; }
struct Item { text title = "Nothing"; ushort price = 10; ubyte max_stack = 1; }
struct Weapon is Item {
  int attack = 1;
  fix reach = 16;
  Element element;
  record<Weapon> upgrade;
  Stats bonus;
  short[3] combo = 2;
  attach behaviour = WeaponSwing;
}
)";

static const char *kWeapons = R"(# weapons
table Weapon

record ShortSword {
  title = "Short Sword"   # a comment after a value
  attack = 3
  reach = 18.5
  upgrade = FlameBrand
  combo[1] = 5
}

record FlameBrand : ShortSword {      // starts as a copy of ShortSword
  title = "Flame Brand"
  attack = -5
  element = Element.Fire
  bonus.strength = 2
  upgrade = #ShortSword
}
)";

struct Cooked
{
  script::Module module;
  std::vector<DataTable> tables;
  Report report;
  std::vector<uint8_t> pack;
  BmPack opened;
};

static void cookPack(Cooked &c, const std::vector<std::string> &scripts, const std::vector<std::string> &data)
{
  std::vector<script::Source> sources;
  for (const std::string &s : scripts) sources.push_back(script::readSource(s));
  c.module = script::compile(sources, stringId);
  for (const script::Diagnostic &d : c.module.diagnostics) printf("  %s:%d: %s\n", d.file.c_str(), d.line, d.message.c_str());
  c.tables = cookData(data, c.module, stringId, c.report);
  BmPackWriter w;
  bmPackWriterInit(&w, BM_FOURCC('G', 'B', 'A', ' '), BM_PACK_LAYOUT_ROM);
  if (!c.module.data.empty()) bmPackWriterAdd(&w, BM_CHUNK_MODB, BM_CATEGORY_SCRIPTS, 0, c.module.data.data(), uint32_t(c.module.data.size()));
  for (const DataTable &t : c.tables) bmPackWriterAdd(&w, BM_CHUNK_DTAB, BM_CATEGORY_OBJECTS, 0, t.chunk.data(), uint32_t(t.chunk.size()));
  uint32_t size = 0;
  uint8_t *p = bmPackWriterFinish(&w, &size);
  c.pack.assign(p, p + size);
  std::free(p);
  bmPackWriterFree(&w);
  TEST_ASSERT(bmPackOpen(&c.opened, c.pack.data(), uint32_t(c.pack.size()), false) == BM_PACK_OK);
}

static std::string firstDataError(const Report &r)
{
  for (const ReportEntry &e : r.entries)
    if (e.level == ReportEntry::Level::Error) return e.message;
  return {};
}

// The chunk, read back by the runtime's reader: fields, keys in order, values, a copy that
// changes some fields, defaults for keys that are not there.
static void testTables()
{
  Cooked c;
  cookPack(c, {write("weapons.ms", kStructures)}, {write("weapons.bmdata", kWeapons)});
  TEST_ASSERT(c.module.ok() && !c.report.hasErrors());
  if (!c.module.ok() || c.report.hasErrors())
  {
    printf("%s", c.report.text().c_str());
    return;
  }
  TEST_ASSERT(c.tables.size() == 3);   // every structure gets a table, also one without records

  BmDataTable t;
  TEST_ASSERT(bmDataFindTable(&c.opened, stringId("Weapon"), &t));
  TEST_ASSERT(t.recordCount == 2 && t.recordSize == 36 && t.fieldCount == 10);
  TEST_ASSERT(bmDataRecordKey(&t, 0) < bmDataRecordKey(&t, 1));
  const script::StructInfo *weapon = nullptr;
  for (const script::StructInfo &s : c.module.structs)
    if (s.name == "Weapon") weapon = &s;
  TEST_ASSERT(weapon && t.layoutHash == layoutHash(*weapon));
  BmDataField f = bmDataTableField(&t, 7);   // bonus
  TEST_ASSERT(s_strings[f.name] == "bonus" && f.type == BM_DATA_STRUCT && s_strings[f.detail] == "Stats");

  auto field = [&](const char *name) {
    for (const script::StructField &sf : weapon->fields)
      if (sf.name == name) return sf;
    return script::StructField{};
  };
  const uint8_t *sword = bmDataRecord(&t, script::nameKey("ShortSword"));
  const uint8_t *brand = bmDataRecord(&t, script::nameKey("FlameBrand"));
  TEST_ASSERT(sword && brand);
  if (!sword || !brand) return;
  auto read = [&](const uint8_t *r, const char *name, int element = 0) {
    script::StructField sf = field(name);
    return bmDataRead(&t, r, uint16_t(sf.offset + element * sf.elementSize), sf.type);
  };
  TEST_ASSERT(s_strings[size_t(read(sword, "title"))] == "Short Sword");
  TEST_ASSERT(read(sword, "price") == 10 && read(sword, "max_stack") == 1);   // the structure's defaults
  TEST_ASSERT(read(sword, "attack") == 3 && read(sword, "reach") == 18 * 256 + 128);
  TEST_ASSERT(uint32_t(read(sword, "upgrade")) == script::nameKey("FlameBrand"));
  TEST_ASSERT(read(sword, "combo", 0) == 2 && read(sword, "combo", 1) == 5 && read(sword, "combo", 2) == 2);
  TEST_ASSERT(s_strings[size_t(read(sword, "behaviour"))] == "WeaponSwing");
  // FlameBrand: a copy of ShortSword with some fields changed.
  TEST_ASSERT(s_strings[size_t(read(brand, "title"))] == "Flame Brand");
  TEST_ASSERT(read(brand, "attack") == -5 && read(brand, "reach") == 18 * 256 + 128 && read(brand, "element") == 1);
  TEST_ASSERT(read(brand, "combo", 1) == 5);
  TEST_ASSERT(bmDataRead(&t, brand, field("bonus").offset, BM_DATA_BYTE) == 2);       // bonus.strength
  TEST_ASSERT(bmDataRead(&t, brand, field("bonus").offset + 1, BM_DATA_BYTE) == 1);   // bonus.luck: default
  // A key that is not in the table reads the defaults.
  TEST_ASSERT(bmDataRecord(&t, script::nameKey("Excalibur")) == nullptr);
  const uint8_t *none = bmDataRecordOrDefaults(&t, script::nameKey("Excalibur"));
  TEST_ASSERT(s_strings[size_t(read(none, "title"))] == "Nothing" && read(none, "attack") == 1);
  // The empty table of Stats.
  BmDataTable stats;
  TEST_ASSERT(bmDataFindTable(&c.opened, stringId("Stats"), &stats) && stats.recordCount == 0);

  // A damaged chunk is refused.
  std::vector<uint8_t> bad = c.tables[0].chunk;
  BmDataTable x;
  TEST_ASSERT(bmDataTableOpen(&x, bad.data(), uint32_t(bad.size())));
  TEST_ASSERT(!bmDataTableOpen(&x, bad.data(), uint32_t(bad.size() - 1)));
  bad[4] = 3;   // a record size that is not a multiple of 4
  TEST_ASSERT(!bmDataTableOpen(&x, bad.data(), uint32_t(bad.size())));
}

// --- Scripts reading records -------------------------------------------------------------

struct Host
{
  const BmPack *pack = nullptr;
  std::vector<int32_t> log;
  std::vector<std::string> errors;
};

static bool hostCall(void *ctx, BmVmInstance *, uint16_t api, const int32_t *args, int32_t *result)
{
  Host &h = *static_cast<Host *>(ctx);
  if (api == BM_SCRIPT_API_log) h.log.push_back(args[0]);
  else bmDataScriptCall(h.pack, api, args, result);
  return false;
}
static int32_t hostGet(void *, BmVmInstance *, uint16_t) { return 0; }
static void hostSet(void *, BmVmInstance *, uint16_t, int32_t) {}
static void hostField(void *, BmVmInstance *, uint16_t) {}
static bool hostWaitOver(void *, BmVmInstance *) { return true; }
static void hostError(void *ctx, BmVmInstance *, const char *what, uint16_t, uint16_t) { static_cast<Host *>(ctx)->errors.push_back(what); }

static void testScripts()
{
  Cooked c;
  cookPack(c, {write("weapons.ms", kStructures), write("hero.ms", R"(class Hero extends Actor;
var record<Weapon> held = #ShortSword;
var record<Weapon> lost = #Excalibur;
function int damage(record<Weapon> w) { return w.attack * 2; }
event spawn() {
  log(held.attack);                     // 3
  log(int(held.reach));                 // 18
  log(held.combo[1]);                   // 5
  log(damage(held.upgrade));            // -10: FlameBrand
  log(held.upgrade.bonus.strength);     // 2
  if held.upgrade.element == Element.Fire { log(100); }
  if held.upgrade.upgrade == held { log(101); }
  if held != #FlameBrand { log(102); }
  log(held.price);                      // 10
  log(lost.attack);                     // 1: not in the table, the defaults
  held = #FlameBrand;
  log(held.attack);                     // -5
  var int total = 0;
  for w in records(Weapon) { total += w.attack; }
  log(total);                           // -2
  var int n = 0;
  for s in records(Stats) { n += 1; }
  log(n);                               // 0
}
)")},
          {write("weapons.bmdata", kWeapons)});
  TEST_ASSERT(c.module.ok() && !c.report.hasErrors());
  if (!c.module.ok()) return;
  TEST_ASSERT(c.module.classes[0].fields[0].typeLabel == "record<Weapon>");

  BmVmModule vm;
  const char *why = nullptr;
  TEST_ASSERT(bmVmModuleOpen(&vm, c.module.data.data(), uint32_t(c.module.data.size()), &why));
  Host h;
  h.pack = &c.opened;
  BmVmHost host = {&h, hostCall, hostGet, hostSet, hostField, hostWaitOver, hostError};
  BmVmInstance inst;
  bmVmInstanceInit(&vm, &inst, 0);
  TEST_ASSERT(uint32_t(inst.fields[0]) == script::nameKey("ShortSword"));
  bmVmEvent(&vm, &host, &inst, BM_SCRIPT_EVENT_spawn);
  std::vector<int32_t> expected = {3, 18, 5, -10, 2, 100, 101, 102, 10, 1, -5, -2, 0};
  if (h.log != expected)
  {
    printf("  got:");
    for (int32_t v : h.log) printf(" %d", int(v));
    printf("\n");
  }
  TEST_ASSERT(h.log == expected);
  TEST_ASSERT(h.errors.empty());

  // Without the table in the pack every field reads 0 and there are no records.
  Host empty;
  BmVmHost host2 = {&empty, hostCall, hostGet, hostSet, hostField, hostWaitOver, hostError};
  BmVmInstance inst2;
  bmVmInstanceInit(&vm, &inst2, 0);
  bmVmEvent(&vm, &host2, &inst2, BM_SCRIPT_EVENT_spawn);
  TEST_ASSERT(empty.errors.empty() && !empty.log.empty() && empty.log[0] == 0 && empty.log[1] == 0);
}

static void testScriptErrors()
{
  auto err = [](const std::string &cls) {
    script::Module m = script::compile({{"w.ms", kStructures}, {"c.ms", cls}}, stringId);
    return m.diagnostics.empty() ? std::string() : m.diagnostics[0].message;
  };
  const std::string head = "class T extends Actor;\nvar record<Weapon> w;\n";
  TEST_ASSERT(err(head + "event tick() { log(w.nothing); }").find("has no field 'nothing'") != std::string::npos);
  TEST_ASSERT(err(head + "event tick() { log(w.combo); }").find("is an array") != std::string::npos);
  TEST_ASSERT(err(head + "event tick() { log(w.combo[3]); }").find("has 3 elements") != std::string::npos);
  TEST_ASSERT(err(head + "event tick() { log(w.bonus); }").find("is a structure") != std::string::npos);
  TEST_ASSERT(err(head + "event tick() { w = 3; }").find("must be record<Weapon>") != std::string::npos);
  TEST_ASSERT(err(head + "var record<Stats> s;\nevent tick() { w = s; }").find("must be record<Weapon>, this is record<Stats>") != std::string::npos);
  TEST_ASSERT(err("class T extends Actor;\nvar record<Nope> w;").find("no structure 'Nope'") != std::string::npos);
  TEST_ASSERT(err(head + "state S { begin: for x in records(Weapon) { } }").find("cannot use 'for'") != std::string::npos);
}

// Problems in record files are reported with file and line.
static void testDataErrors()
{
  std::string structures = write("weapons.ms", kStructures);
  auto err = [&](const std::string &text, const std::string &more = std::string()) {
    Cooked c;
    std::vector<std::string> data = {write("e.bmdata", text)};
    if (!more.empty()) data.push_back(write("f.bmdata", more));
    cookPack(c, {structures}, data);
    return firstDataError(c.report);
  };
  TEST_ASSERT(err("record A { }").find("starts with 'table") != std::string::npos);
  TEST_ASSERT(err("table Spell\n").find("no structure 'Spell'") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { power = 3 }").find("no field 'power'") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { price = -1 }").find("does not fit") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { attack = \"x\" }").find("whole number") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { element = Water }").find("not a value of Element") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { upgrade = Nope }").find("no record 'Nope'") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A : B { }\nrecord B : A { }").find("copy of itself") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { }", "table Weapon\nrecord A { }").find("exists already") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { bonus = 1 }").find("is a structure") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { combo[3] = 1 }").find("has 3 elements") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { title = \"open }").find("closing quote") != std::string::npos);
  TEST_ASSERT(err("table Weapon\nrecord A { attack = 1").find("no closing '}'") != std::string::npos);
  TEST_ASSERT(err("table Stats\nrecord A { }", "table Weapon\nrecord B { upgrade = A }").find("is a Stats, not a Weapon") != std::string::npos);
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    printf("usage: test_data <scratch folder>\n");
    return 2;
  }
  s_work = argv[1];
  fs::remove_all(s_work);
  fs::create_directories(s_work);
  RUN_TEST(testTables);
  RUN_TEST(testScripts);
  RUN_TEST(testScriptErrors);
  RUN_TEST(testDataErrors);
  return TEST_RESULT();
}
