// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The editor's model of a record file (editor/data_file.h): values changed, added and removed
// line by line with the rest of the file as it was, records added and removed, and what comes
// out still cooks.

#include "data_asset.h"
#include "data_file.h"

#include "test.h"

#include <filesystem>
#include <fstream>

using namespace bm::editor;

static const char *const kStructs = "enum Element { None, Fire, Ice }\n"
                                    "struct Speaker { text name; sprite portrait; }\n"
                                    "struct Line { record<Speaker> speaker; text says; int pause = 4; Element mood; }\n";

static const char *const kFile = "# Lines.\n"
                                 "table Line\n"
                                 "\n"
                                 "record Hello {\n"
                                 "  speaker = Elder   # who\n"
                                 "  says = \"Hello, \\\"you\\\".\"\n"
                                 "}\n"
                                 "\n"
                                 "record Again : Hello {}\n";

static std::string s_dir;

static std::string write(const std::string &name, const std::string &text)
{
  std::string path = s_dir + "/" + name;
  std::ofstream(path, std::ios::binary) << text;
  return path;
}

static bool cooks(const DataFile &lines)
{
  std::string speakers = write("speakers.bmdata", "table Speaker\nrecord Elder {\n  name = \"Elder\"\n}\n");
  bm::script::Module module = bm::script::compile({{s_dir + "/s.ms", kStructs}}, [](const std::string &) { return uint16_t(1); });
  if (!module.ok()) return false;
  bm::Report report;
  bm::cookData({speakers, lines.path}, module, [](const std::string &) { return uint16_t(1); }, report);
  for (const bm::ReportEntry &e : report.entries)
    if (e.level == bm::ReportEntry::Level::Error)
    {
      std::printf("    %s\n", e.message.c_str());
      return false;
    }
  return true;
}

static void testReadAndChange()
{
  DataFile f;
  TEST_ASSERT(f.load(write("lines.bmdata", kFile), nullptr));
  TEST_ASSERT(f.table == "Line" && f.records.size() == 2);
  TEST_ASSERT(f.records[1].name == "Again" && f.records[1].base == "Hello" && f.records[1].first == f.records[1].last);
  TEST_ASSERT(f.own("Hello", "speaker") == "Elder" && f.own("Hello", "says") == "\"Hello, \\\"you\\\".\"");
  TEST_ASSERT(unquoteText(f.own("Hello", "says")) == "Hello, \"you\".");
  TEST_ASSERT(f.own("Again", "says").empty() && f.value("Again", "says") == f.own("Hello", "says"));
  // A value changed keeps its line's indent and comment.
  f.set("Hello", "speaker", "Elder");
  f.set("Hello", "pause", "8");
  TEST_ASSERT(f.lines[4] == "  speaker = Elder   # who");
  TEST_ASSERT(f.lines[6] == "  pause = 8" && f.lines[7] == "}");
  // A text with quotes is written escaped and reads back.
  f.set("Hello", "says", quoteText("Hi \"there\""));
  TEST_ASSERT(unquoteText(f.own("Hello", "says")) == "Hi \"there\"");
  // A value in a record written on one line opens it.
  f.set("Again", "mood", "Fire");
  TEST_ASSERT(f.own("Again", "mood") == "Fire" && f.value("Again", "pause") == "8");
  // Removed: the record inherits again.
  f.set("Hello", "pause", "");
  TEST_ASSERT(f.own("Hello", "pause").empty() && f.lines.size() == 11);
  TEST_ASSERT(f.lines[0] == "# Lines." && f.lines[2].empty());
  TEST_ASSERT(f.save(nullptr) && cooks(f));
}

static void testAddAndRemove()
{
  DataFile f;
  TEST_ASSERT(f.load(write("lines.bmdata", kFile), nullptr));
  std::string error;
  TEST_ASSERT(!f.add("Hello", "", &error) && !error.empty());
  TEST_ASSERT(!f.add("2nd", "", nullptr) && !f.add("with space", "", nullptr));
  TEST_ASSERT(f.add("Bye", "Hello", nullptr));
  f.set("Bye", "says", quoteText("Bye."));
  TEST_ASSERT(f.records.size() == 3 && f.find("Bye")->base == "Hello" && f.value("Bye", "speaker") == "Elder");
  TEST_ASSERT(f.save(nullptr) && cooks(f));
  TEST_ASSERT(f.remove("Again") && !f.remove("Again") && f.records.size() == 2);
  TEST_ASSERT(f.save(nullptr) && cooks(f));
  // Read again from the disk: the same.
  DataFile g;
  TEST_ASSERT(g.load(f.path, nullptr) && g.lines == f.lines && g.records.size() == 2);
}

int main()
{
  s_dir = (std::filesystem::temp_directory_path() / "blutmond_test_data_file").string();
  std::filesystem::create_directories(s_dir);
  RUN_TEST(testReadAndChange);
  RUN_TEST(testAddAndRemove);
  return TEST_RESULT();
}
