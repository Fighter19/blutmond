// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "script_panel.h"
#include "theme_colors.h"

#include <filesystem>
#include <fstream>
#include <map>

#include "code_edit.h"
#include "dialogs.h"
#include "ui/context.h"
#include "ui/lists.h"
#include "ui/loader.h"
#include "ui/menus.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;
using namespace ose::ui;

namespace bm::editor {

namespace {
std::string fileName(const std::string &path)
{
  return fs::path(path).filename().string();
}
}  // namespace

void ScriptPanel::bind(Control &root, Context &ui)
{
  ui_ = &ui;
  code_ = root.findAs<CodeEdit>("script_code");
  chooser_ = root.findAs<OptionButton>("script_file");
  problems_ = root.findAs<ItemList>("script_problems");
  outline_ = root.findAs<ItemList>("script_outline");
  reference_ = root.findAs<ItemList>("script_reference");
  status_ = root.findAs<Label>("script_status");
  problems_->tileSize = outline_->tileSize = reference_->tileSize = 0;
  code_->classify = [](const std::string &w) {
    return script::isKeyword(w) ? CodeEdit::WordKind::Keyword : script::isTypeName(w) ? CodeEdit::WordKind::Type
           : script::isBuiltin(w) ? CodeEdit::WordKind::Builtin : CodeEdit::WordKind::Plain;
  };
  code_->changed.connect([this] {
    dirty_ = true;
    compilePending_ = true;
    idle_ = 0;
  });
  chooser_->itemSelected.connect([this](int i) {
    if (i >= 0 && size_t(i) < paths_.size()) open(paths_[size_t(i)]);
  });
  // A click on a problem goes to its line (in its file).
  problems_->selectedChanged.connect([this](int i) {
    if (i < 0 || size_t(i) >= shown_.size()) return;
    const script::Diagnostic &d = shown_[size_t(i)];
    if (d.file != path_ && !d.file.empty()) open(d.file);
    code_->goTo(d.line, d.column);
  });
  for (const std::string &line : script::apiReference()) reference_->items.push_back({line, "bolt", {}, {}, {}, {}});
}

void ScriptPanel::setScripts(const std::vector<std::string> &paths)
{
  paths_ = paths;
  std::vector<std::string> names;
  int current = -1;
  for (size_t i = 0; i < paths.size(); i++)
  {
    names.push_back(fileName(paths[i]));
    if (paths[i] == path_) current = int(i);
  }
  chooser_->setItems(names);
  if (current >= 0) chooser_->select(current, false);
  else if (!paths.empty()) open(paths[0]);
  else
  {
    path_.clear();
    code_->setText("");
    status_->text = "No scripts yet: New script";
  }
}

void ScriptPanel::open(const std::string &path)
{
  if (dirty_) save();
  path_ = path;
  std::string text;
  try
  {
    text = script::readSource(path).text;
  }
  catch (const std::exception &e)
  {
    if (log) log(e.what(), true);
  }
  saved_ = text;
  code_->setText(text);
  dirty_ = false;
  for (size_t i = 0; i < paths_.size(); i++)
    if (paths_[i] == path) chooser_->select(int(i), false);
  compileNow();
}

void ScriptPanel::save()
{
  dirty_ = false;
  if (path_.empty()) return;
  std::string text = code_->text();
  if (text == saved_) return;
  if (!text.empty() && text.back() != '\n') text += '\n';
  std::ofstream out(path_, std::ios::binary);
  if (!out || !(out << text))
  {
    if (log) log("cannot write " + path_, true);
    return;
  }
  saved_ = code_->text();
}

void ScriptPanel::compileNow()
{
  compilePending_ = false;
  // All scripts of the project form one module: the open one as it is in the editor.
  std::vector<script::Source> sources;
  for (const std::string &p : paths_)
  {
    if (p == path_) sources.push_back({p, code_->text()});
    else
    {
      try
      {
        sources.push_back(script::readSource(p));
      }
      catch (const std::exception &)
      {
      }
    }
  }
  std::map<std::string, uint16_t> ids;
  script::Module module = script::compile(sources, [&](const std::string &s) {
    auto it = ids.find(s);
    if (it != ids.end()) return it->second;
    uint16_t id = uint16_t(ids.size());
    ids[s] = id;
    return id;
  });

  shown_ = module.diagnostics;
  problems_->items.clear();
  code_->markers.clear();
  for (const script::Diagnostic &d : module.diagnostics)
  {
    problems_->items.push_back({fileName(d.file) + ":" + std::to_string(d.line) + ": " + d.message, d.warning ? "warning" : "error", {}, {}, {}, {}});
    if (d.file == path_) code_->markers.push_back({d.line, d.message});
  }
  problems_->selected = -1;
  if (module.diagnostics.empty()) problems_->items.push_back({"No problems", "check", {}, {}, {}, {}});
  lastOk_ = module.ok();
  size_t words = 0;
  for (const script::ClassInfo &c : module.classes) words += size_t(c.codeWords);
  status_->text = lastOk_ ? std::to_string(module.classes.size()) + " classes compile: " + std::to_string(words * 2) + " bytes of code, " +
                                std::to_string(module.data.size()) + " bytes in the pack"
                          : std::to_string(module.diagnostics.size()) + (module.diagnostics.size() == 1 ? " problem" : " problems") +
                                ": the running game keeps the last version that compiled";
  status_->colorOverride = lastOk_ ? Color{0, 0, 0, 0} : errorColor(status_->theme());
  fillOutline(module);
}

void ScriptPanel::fillOutline(const script::Module &module)
{
  outline_->items.clear();
  const script::ClassInfo *cls = nullptr;
  for (const script::ClassInfo &c : module.classes)
    if (c.file == path_) cls = &c;
  if (!cls)
  {
    outline_->items.push_back({"(fix the problems to see the class)", "info", {}, {}, {}, {}});
    return;
  }
  outline_->items.push_back({"class " + cls->name + (cls->base.empty() ? "" : " extends " + cls->base), "cube", {}, {}, {}, {}});
  if (!cls->sprite.empty()) outline_->items.push_back({"sprite \"" + cls->sprite + "\"", "film", {}, {}, {}, {}});
  outline_->items.push_back({std::to_string(cls->fields.size()) + " of " + std::to_string(BM_SCRIPT_MAX_FIELDS) + " variables, " +
                                 std::to_string(cls->codeWords * 2) + " bytes of code",
                             "info", {}, {}, {}, {}});
  for (const script::FieldInfo &f : cls->fields)
  {
    std::string flags;
    if (f.flags & BM_SCRIPT_FIELD_SAVE) flags += " save";
    if (f.flags & BM_SCRIPT_FIELD_LINK) flags += " link";
    if (f.flags & BM_SCRIPT_FIELD_TRANSIENT) flags += " transient";
    std::string value = f.type == BM_SCRIPT_FIX ? std::to_string(f.defaultValue / 256.0).substr(0, 5)
                        : f.type == BM_SCRIPT_BOOL ? (f.defaultValue ? "true" : "false")
                        : f.type == BM_SCRIPT_INT ? std::to_string(f.defaultValue) : std::string();
    outline_->items.push_back({std::string(script::typeName(f.type)) + " " + f.name + (value.empty() ? "" : " = " + value) +
                                   (f.category.empty() ? "" : "   [" + f.category + ": set per object in the room]") + flags,
                               (f.flags & BM_SCRIPT_FIELD_SAVE) ? "save" : f.category.empty() ? "circle-o" : "sliders", {}, {}, {}, {}});
  }
  for (const std::string &s : cls->states)
    outline_->items.push_back({"state " + s + (s == cls->startState ? "   (start)" : ""), "play", {}, {}, {}, {}});
  for (const std::string &e : cls->events) outline_->items.push_back({"event " + e, "bolt", {}, {}, {}, {}});
  for (const std::string &fn : cls->functions) outline_->items.push_back({"function " + fn, "cog", {}, {}, {}, {}});
}

void ScriptPanel::newScript()
{
  if (scriptDir.empty())
  {
    if (log) log("the project has no asset folder for scripts yet", true);
    return;
  }
  openForm(*ui_, "New script", {{"Class", "Enemy", FormField::Kind::Text, {}, 0, 0, {}}}, "Create",
           [this](const std::vector<std::string> &v, std::string *pError) {
             const std::string &name = v[0];
             bool valid = !name.empty() && !(name[0] >= '0' && name[0] <= '9');
             for (char ch : name) valid = valid && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_');
             if (!valid)
             {
               *pError = "a class name is made of letters, digits and _";
               return false;
             }
             std::string lower = name;
             for (char &ch : lower)
               if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
             std::error_code ec;
             fs::create_directories(scriptDir, ec);
             std::string path = (fs::path(scriptDir) / (lower + ".ms")).string();
             if (fs::exists(path, ec))
             {
               *pError = path + " exists already";
               return false;
             }
             std::ofstream out(path, std::ios::binary);
             out << "class " << name << " extends Actor;\n\n"
                 << "// Objects of class \"" << name << "\" in a room run this script.\n\n"
                 << "var(" << name << ") int Damage = 1;\n\n"
                 << "state Idle {\n"
                 << "  event touch() {\n"
                 << "    hurt_player(Damage);\n"
                 << "  }\n"
                 << "begin:\n"
                 << "  loop {\n"
                 << "    sleep(60);\n"
                 << "  }\n"
                 << "}\n\n"
                 << "defaultproperties {\n"
                 << "  start_state = Idle;\n"
                 << "}\n";
             if (!out)
             {
               *pError = "cannot write " + path;
               return false;
             }
             out.close();
             if (log) log("new script " + path, false);
             if (created) created();
             open(path);
             return true;
           });
}

void ScriptPanel::update(float dt)
{
  idle_ += dt;
  if (compilePending_ && idle_ > 0.35f) compileNow();
  if (dirty_ && idle_ > 0.6f)
  {
    save();
    // The running game only gets scripts that compile.
    if (lastOk_ && changed) changed();
  }
}

}  // namespace bm::editor
