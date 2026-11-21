// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Script tab: the project's Mondskript files in a code editor. While typing, all scripts
// of the project are compiled together (the open one from the editor, the others from disk);
// problems are marked on their lines, and the outline shows the class as the runtime gets it:
// variables with type, category and storage flags, states, events, code size. The file is
// saved shortly after an edit; when everything compiles, the editor cooks again and, with
// Live, the running game gets the new scripts.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "mondskript.h"

namespace ose::ui {
class Context;
class Control;
class ItemList;
class Label;
class OptionButton;
}  // namespace ose::ui

namespace bm::editor {

class CodeEdit;

class ScriptPanel
{
public:
  /// A script was saved and all scripts compile: cook again (and push to the running game).
  std::function<void()> changed;
  /// A new script file was written.
  std::function<void()> created;
  std::function<void(const std::string &line, bool error)> log;
  /// Where new scripts go.
  std::string scriptDir;

  void bind(ose::ui::Control &root, ose::ui::Context &ui);
  void setScripts(const std::vector<std::string> &paths);
  void open(const std::string &path);
  const std::string &path() const { return path_; }
  void newScript();
  void update(float dt);

private:
  void compileNow();
  void save();
  void fillOutline(const script::Module &module);

  ose::ui::Context *ui_ = nullptr;
  CodeEdit *code_ = nullptr;
  ose::ui::OptionButton *chooser_ = nullptr;
  ose::ui::ItemList *problems_ = nullptr, *outline_ = nullptr, *reference_ = nullptr;
  ose::ui::Label *status_ = nullptr;
  std::vector<std::string> paths_;
  std::string path_, saved_;
  std::vector<script::Diagnostic> shown_;
  bool dirty_ = false, compilePending_ = false, lastOk_ = false;
  float idle_ = 0;
};

}  // namespace bm::editor
