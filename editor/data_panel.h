// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Data tab (docs/plans/structures.md step 5): the project's records, table by table. The
// structures come from the scripts (compiled when the tab is filled), the records from the
// .bmdata files. A record's fields are a form: text and numbers typed, enums, records,
// sprites and yes/no chosen from lists; a field the record does not set shows what it gets
// from its base record or the structure's default. Every change is written to the file at
// once (only that line, data_file.h), and the editor cooks and, with Live, pushes it to the
// running game.
//
// A preview shows what the record looks like: a sprite it names (a speaker's portrait) and,
// for a line of dialogue, the speaker's portrait, name and words as in the dialogue box.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "data_file.h"
#include "mondskript.h"
#include "ui/core.h"

namespace ose::ui {
class Context;
class Control;
class ItemList;
class Label;
class OptionButton;
class Renderer;
class TextureRect;
class Container;
}  // namespace ose::ui

namespace bm::editor {

class DataPanel
{
public:
  /// A record file was written: cook again (and push to the running game).
  std::function<void()> changed;
  std::function<void(const std::string &line, bool error)> log;

  void bind(ose::ui::Control &root, ose::ui::Context &ui, ose::ui::Renderer &renderer);
  /// The record files, the scripts that declare the structures, the sprite files (portraits).
  void setFiles(const std::vector<std::string> &data, const std::vector<std::string> &scripts, const std::vector<std::string> &sprites);
  /// Shows a record file (and a record of it).
  void open(const std::string &path, const std::string &record = "");
  /// Shows the record of that name, whichever file has it. False if none does.
  bool openRecord(const std::string &name);
  bool hasRecord(const std::string &name) const;
  /// A record's value for `key` (its own or a base's), a text without its quotes; "" if none.
  std::string valueOf(const std::string &record, const std::string &key) const;
  /// The structure (table) the record belongs to, or "".
  std::string tableOf(const std::string &record) const;
  /// The record whose key (FNV-1a of its name) is `key`, or "".
  std::string recordWithKey(uint32_t key) const;
  void update(float dt);

  // Actions (buttons and the record list's context menu).
  void newRecord();
  void duplicateRecord();
  void deleteRecord();

private:
  struct Field
  {
    std::string key;              // "stats.defence" for a field of an inner structure
    script::StructField field;
    std::string fallback;         // the default as written in a file, for a field not set
  };

  void compile();
  void loadFiles();
  void fillTables();
  void showFile(size_t index);
  void fillRecords();
  void buildForm();
  void showPreview();
  void commit(const std::string &key, const std::string &value);
  void recordMenu(int index, ose::Vec2 at);
  const script::StructInfo *structure(const std::string &name) const;
  const script::EnumInfo *enumeration(const std::string &name) const;
  void flatten(const script::StructInfo &s, const std::string &prefix, size_t defaultsOffset, std::vector<Field> &out) const;
  std::string defaultText(const script::StructField &f, size_t offset) const;
  /// The value a record gets for `key`: its own, a base's, or "" (the structure's default).
  std::string effective(const DataFile &file, const std::string &record, const std::string &key) const;
  /// Records of a structure in all files.
  std::vector<std::string> recordsOf(const std::string &table) const;
  /// The file and record of that name.
  const DataFile *fileOf(const std::string &record) const;
  std::string spritePath(const std::string &name) const;

  ose::ui::Context *ui_ = nullptr;
  ose::ui::Renderer *renderer_ = nullptr;
  ose::ui::OptionButton *tableChooser_ = nullptr;
  ose::ui::ItemList *records_ = nullptr;
  ose::ui::Container *form_ = nullptr;
  ose::ui::Label *status_ = nullptr, *previewName_ = nullptr, *previewText_ = nullptr;
  ose::ui::TextureRect *preview_ = nullptr;

  std::vector<std::string> dataPaths_, scriptPaths_, spritePaths_;
  std::vector<DataFile> files_;
  size_t current_ = 0;          // index into files_
  std::string record_;          // the record shown
  script::Module module_;
  std::map<uint16_t, std::string> strings_;   // string ids of the compile, for text defaults
  std::vector<Field> fields_;
  bool rebuild_ = false;
};

}  // namespace bm::editor
