// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "data_panel.h"
#include "theme_colors.h"

#include <filesystem>
#include <memory>

#include "dialogs.h"
#include "sprite_asset.h"
#include "ui/containers.h"
#include "ui/context.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/renderer.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;
using namespace ose::ui;

namespace bm::editor {

namespace {

std::string stem(const std::string &path)
{
  return fs::path(path).stem().string();
}

uint32_t readLe(const std::vector<uint8_t> &b, size_t at, size_t n)
{
  uint32_t v = 0;
  for (size_t i = 0; i < n && at + i < b.size(); i++) v |= uint32_t(b[at + i]) << (8 * i);
  return v;
}

// What a field's value looks like in the form: a text without its quotes, a name without #.
std::string shown(const script::StructField &f, const std::string &value)
{
  if (f.type == BM_DATA_TEXT || f.type == BM_DATA_SPRITE || f.type == BM_DATA_SOUND) return unquoteText(value);
  if (f.type == BM_DATA_NAME && !value.empty() && value[0] == '#') return value.substr(1);
  return value;
}

// And how a typed value is written into the file ("" removes it: the record then inherits).
std::string written(const script::StructField &f, const std::string &typed)
{
  if (typed.empty()) return "";
  switch (f.type)
  {
  case BM_DATA_TEXT: case BM_DATA_SPRITE: case BM_DATA_SOUND: return quoteText(typed);
  case BM_DATA_NAME: return typed[0] == '#' ? typed : "#" + typed;
  default: return typed;
  }
}

bool isNumber(const std::string &s, bool fraction)
{
  if (s.empty()) return false;
  size_t i = s[0] == '-' ? 1 : 0;
  bool digits = false, dot = false;
  for (; i < s.size(); i++)
  {
    if (s[i] >= '0' && s[i] <= '9') digits = true;
    else if (s[i] == '.' && fraction && !dot) dot = true;
    else return false;
  }
  return digits;
}

}  // namespace

void DataPanel::bind(Control &root, Context &ui, Renderer &renderer)
{
  ui_ = &ui;
  renderer_ = &renderer;
  tableChooser_ = root.findAs<OptionButton>("data_table");
  records_ = root.findAs<ItemList>("data_records");
  form_ = root.findAs<Container>("data_fields");
  status_ = root.findAs<Label>("data_status");
  preview_ = root.findAs<TextureRect>("data_preview");
  previewName_ = root.findAs<Label>("data_preview_name");
  previewText_ = root.findAs<Label>("data_preview_text");
  records_->tileSize = 0;
  tableChooser_->itemSelected.connect([this](int i) {
    if (i >= 0 && size_t(i) < files_.size()) showFile(size_t(i));
  });
  records_->selectedChanged.connect([this](int i) {
    const DataFile &f = files_[current_];
    if (i < 0 || size_t(i) >= f.records.size()) return;
    record_ = f.records[size_t(i)].name;
    rebuild_ = true;
  });
  records_->contextMenu.connect([this](int i, ose::Vec2 at) { recordMenu(i, at); });
  if (auto *b = root.findAs<Button>("data_new")) b->clicked.connect([this] { newRecord(); });
  if (auto *b = root.findAs<Button>("data_duplicate")) b->clicked.connect([this] { duplicateRecord(); });
  if (auto *b = root.findAs<Button>("data_delete")) b->clicked.connect([this] { deleteRecord(); });
}

void DataPanel::setFiles(const std::vector<std::string> &data, const std::vector<std::string> &scripts, const std::vector<std::string> &sprites)
{
  dataPaths_ = data;
  scriptPaths_ = scripts;
  spritePaths_ = sprites;
  std::string path = current_ < files_.size() ? files_[current_].path : std::string();
  compile();
  loadFiles();
  fillTables();
  size_t index = 0;
  for (size_t i = 0; i < files_.size(); i++)
    if (files_[i].path == path) index = i;
  if (!files_.empty()) showFile(index);
}

void DataPanel::compile()
{
  std::vector<script::Source> sources;
  for (const std::string &p : scriptPaths_)
  {
    try
    {
      sources.push_back(script::readSource(p));
    }
    catch (const std::exception &)
    {
    }
  }
  strings_.clear();
  std::map<std::string, uint16_t> ids;
  module_ = script::compile(sources, [&](const std::string &s) {
    auto it = ids.find(s);
    if (it != ids.end()) return it->second;
    uint16_t id = uint16_t(ids.size());
    ids[s] = id;
    strings_[id] = s;
    return id;
  });
}

void DataPanel::loadFiles()
{
  files_.clear();
  for (const std::string &p : dataPaths_)
  {
    DataFile f;
    std::string error;
    if (f.load(p, &error)) files_.push_back(std::move(f));
    else if (log) log(error, true);
  }
}

void DataPanel::fillTables()
{
  std::vector<std::string> names;
  for (const DataFile &f : files_) names.push_back(f.table + "  (" + fs::path(f.path).filename().string() + ")");
  tableChooser_->setItems(names);
}

void DataPanel::showFile(size_t index)
{
  current_ = index;
  tableChooser_->select(int(index), false);
  const DataFile &f = files_[index];
  if (f.find(record_) == nullptr) record_ = f.records.empty() ? std::string() : f.records[0].name;
  fillRecords();
  rebuild_ = true;
}

void DataPanel::fillRecords()
{
  records_->items.clear();
  records_->selected = -1;
  if (current_ >= files_.size()) return;
  const DataFile &f = files_[current_];
  for (size_t i = 0; i < f.records.size(); i++)
  {
    const DataRecord &r = f.records[i];
    records_->items.push_back({r.name + (r.base.empty() ? "" : "  : " + r.base), "file-text", {}, {}, {}, {}});
    if (r.name == record_) records_->selected = int(i);
  }
}

void DataPanel::open(const std::string &path, const std::string &record)
{
  for (size_t i = 0; i < files_.size(); i++)
  {
    if (files_[i].path != path) continue;
    if (!record.empty()) record_ = record;
    showFile(i);
    return;
  }
}

bool DataPanel::openRecord(const std::string &name)
{
  for (size_t i = 0; i < files_.size(); i++)
  {
    if (files_[i].find(name) == nullptr) continue;
    record_ = name;
    showFile(i);
    return true;
  }
  return false;
}

bool DataPanel::hasRecord(const std::string &name) const
{
  return fileOf(name) != nullptr;
}

std::string DataPanel::valueOf(const std::string &record, const std::string &key) const
{
  const DataFile *f = fileOf(record);
  return f ? unquoteText(f->value(record, key)) : std::string();
}

std::string DataPanel::tableOf(const std::string &record) const
{
  const DataFile *f = fileOf(record);
  return f ? f->table : std::string();
}

std::string DataPanel::recordWithKey(uint32_t key) const
{
  for (const DataFile &f : files_)
    for (const DataRecord &r : f.records)
      if (script::nameKey(r.name) == key) return r.name;
  return "";
}

const DataFile *DataPanel::fileOf(const std::string &record) const
{
  for (const DataFile &f : files_)
    if (f.find(record) != nullptr) return &f;
  return nullptr;
}

std::vector<std::string> DataPanel::recordsOf(const std::string &table) const
{
  std::vector<std::string> out;
  for (const DataFile &f : files_)
    if (f.table == table)
      for (const DataRecord &r : f.records) out.push_back(r.name);
  return out;
}

const script::StructInfo *DataPanel::structure(const std::string &name) const
{
  for (const script::StructInfo &s : module_.structs)
    if (s.name == name) return &s;
  return nullptr;
}

const script::EnumInfo *DataPanel::enumeration(const std::string &name) const
{
  for (const script::EnumInfo &e : module_.enums)
    if (e.name == name) return &e;
  return nullptr;
}

std::string DataPanel::defaultText(const script::StructField &f, size_t offset) const
{
  const script::StructInfo *top = structure(files_[current_].table);
  if (top == nullptr) return "";
  const std::vector<uint8_t> &d = top->defaults;
  switch (f.type)
  {
  case BM_DATA_BOOL: return readLe(d, offset, 1) ? "true" : "false";
  case BM_DATA_BYTE: return std::to_string(int8_t(readLe(d, offset, 1)));
  case BM_DATA_UBYTE: return std::to_string(readLe(d, offset, 1));
  case BM_DATA_SHORT: return std::to_string(int16_t(readLe(d, offset, 2)));
  case BM_DATA_USHORT: return std::to_string(readLe(d, offset, 2));
  case BM_DATA_INT: return std::to_string(int32_t(readLe(d, offset, 4)));
  case BM_DATA_UINT: return std::to_string(readLe(d, offset, 4));
  case BM_DATA_FIX:
  {
    int32_t v = int32_t(readLe(d, offset, 4));
    std::string s = std::to_string(double(v) / 256.0);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
  }
  case BM_DATA_ENUM:
  {
    const script::EnumInfo *e = enumeration(f.detail);
    uint32_t v = readLe(d, offset, 1);
    return e && v < e->values.size() ? e->values[v] : std::to_string(v);
  }
  case BM_DATA_TEXT: case BM_DATA_NAME: case BM_DATA_SPRITE: case BM_DATA_SOUND: case BM_DATA_ATTACH:
  {
    uint32_t id = readLe(d, offset, 2);
    auto it = strings_.find(uint16_t(id));
    return id == BM_DATA_NONE_ID || it == strings_.end() ? std::string() : it->second;
  }
  case BM_DATA_RECORD:
    return recordWithKey(readLe(d, offset, 4));
  default:
    return "";
  }
}

void DataPanel::flatten(const script::StructInfo &s, const std::string &prefix, size_t offset, std::vector<Field> &out) const
{
  for (const script::StructField &f : s.fields)
  {
    if (f.type == BM_DATA_STRUCT)
    {
      if (const script::StructInfo *inner = structure(f.detail)) flatten(*inner, prefix + f.name + ".", offset + f.offset, out);
      continue;
    }
    Field field;
    field.key = prefix + f.name;
    field.field = f;
    field.fallback = defaultText(f, offset + f.offset);
    out.push_back(field);
  }
}

std::string DataPanel::effective(const DataFile &file, const std::string &record, const std::string &key) const
{
  return file.value(record, key);
}

std::string DataPanel::spritePath(const std::string &name) const
{
  for (const std::string &p : spritePaths_)
    if (stem(p) == name) return p;
  return "";
}

void DataPanel::buildForm()
{
  form_->clearChildren();
  fields_.clear();
  if (current_ >= files_.size())
  {
    status_->text = "No record files in the project";
    return;
  }
  const DataFile &file = files_[current_];
  const script::StructInfo *s = structure(file.table);
  if (s == nullptr)
  {
    status_->text = "No structure " + file.table + " in the scripts (or they do not compile)";
    return;
  }
  const DataRecord *r = file.find(record_);
  if (r == nullptr)
  {
    status_->text = file.records.empty() ? "No records yet: New record" : "";
    showPreview();
    return;
  }
  status_->text = "record " + r->name + " of " + file.table + (r->base.empty() ? "" : ", a copy of " + r->base + " with its changes") +
                  " — changes are saved and cooked at once";
  flatten(*s, "", 0, fields_);

  auto *grid = static_cast<GridContainer *>(form_->addChild(std::make_unique<GridContainer>()));
  grid->columns = 2;
  grid->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
  for (const Field &fd : fields_)
  {
    const script::StructField &f = fd.field;
    std::string own = file.own(record_, fd.key);
    std::string inherited = r->base.empty() ? std::string() : effective(file, r->base, fd.key);
    // What the record gets without its own value: the base's, else the structure's default.
    std::string fallback = !inherited.empty() ? shown(f, inherited) : fd.fallback;
    std::string from = !inherited.empty() ? "as " + r->base : "default";

    auto *label = static_cast<Label *>(grid->addChild(std::make_unique<Label>(fd.key)));
    label->bold = !own.empty();
    label->tooltip = f.typeLabel + (own.empty() ? " (not set: " + from + ")" : "");

    std::vector<std::string> choices;
    bool list = true;
    if (f.type == BM_DATA_BOOL) choices = {"true", "false"};
    else if (f.type == BM_DATA_ENUM)
    {
      if (const script::EnumInfo *e = enumeration(f.detail)) choices = e->values;
    }
    else if (f.type == BM_DATA_RECORD) choices = recordsOf(f.detail);
    else if (f.type == BM_DATA_SPRITE)
    {
      for (const std::string &p : spritePaths_) choices.push_back(stem(p));
    }
    else list = false;

    std::string key = fd.key;
    if (list)
    {
      // First the choice of not setting it, then the values.
      std::vector<std::string> items{"(" + from + (fallback.empty() ? "" : ": " + fallback) + ")"};
      int selected = 0;
      std::string current = shown(f, own);
      for (size_t i = 0; i < choices.size(); i++)
      {
        items.push_back(choices[i]);
        if (!own.empty() && choices[i] == current) selected = int(i) + 1;
      }
      auto *option = static_cast<OptionButton *>(grid->addChild(std::make_unique<OptionButton>()));
      option->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
      option->setItems(items);
      option->select(selected, false);
      script::StructField field = f;
      option->itemSelected.connect([this, key, field, choices](int i) {
        commit(key, i <= 0 || size_t(i) > choices.size() ? std::string() : written(field, choices[size_t(i - 1)]));
      });
      continue;
    }
    auto *edit = static_cast<LineEdit *>(grid->addChild(std::make_unique<LineEdit>()));
    edit->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
    edit->setText(shown(f, own));
    edit->placeholder = fallback.empty() ? "(" + from + ")" : fallback + "  (" + from + ")";
    script::StructField field = f;
    edit->submitted.connect([this, key, field](const std::string &typed) {
      bool numeric = field.type != BM_DATA_TEXT && field.type != BM_DATA_NAME && field.type != BM_DATA_SOUND && field.type != BM_DATA_ATTACH;
      if (numeric && !typed.empty() && !isNumber(typed, field.type == BM_DATA_FIX))
      {
        status_->text = "'" + key + "' is a " + std::string(field.type == BM_DATA_FIX ? "number" : "whole number") + ": not saved";
        return;
      }
      commit(key, written(field, typed));
    });
  }
  showPreview();
}

void DataPanel::commit(const std::string &key, const std::string &value)
{
  if (current_ >= files_.size() || record_.empty()) return;
  DataFile &file = files_[current_];
  if (file.own(record_, key) == value) return;
  file.set(record_, key, value);
  std::string error;
  if (!file.save(&error))
  {
    if (log) log(error, true);
    return;
  }
  if (log) log("saved " + record_ + "." + key + (value.empty() ? " (removed: inherits)" : " = " + value), false);
  // The form is built again after this event (its controls are still in use now).
  rebuild_ = true;
  if (changed) changed();
}

void DataPanel::showPreview()
{
  if (preview_->texture) renderer_->destroyTexture(preview_->texture);
  preview_->texture = {};
  preview_->textureSize = {0, 0};
  previewName_->text.clear();
  previewText_->text.clear();
  if (current_ >= files_.size() || record_.empty()) return;
  const DataFile &file = files_[current_];

  // A sprite the record names, or one of a record it names (a line's speaker's portrait).
  std::string sprite, name, words;
  for (const Field &fd : fields_)
  {
    const script::StructField &f = fd.field;
    std::string v = unquoteText(file.value(record_, fd.key));
    if (v.empty()) v = fd.fallback;
    if (f.type == BM_DATA_SPRITE && sprite.empty()) sprite = v;
    if (f.type == BM_DATA_TEXT && (fd.key == "name" || fd.key == "title") && name.empty()) name = v;
    if (f.type == BM_DATA_TEXT && (fd.key == "says" || fd.key == "description") && words.empty()) words = v;
    if (f.type == BM_DATA_RECORD && !v.empty())
    {
      if (const DataFile *other = fileOf(v))
      {
        std::string p = unquoteText(other->value(v, "portrait"));
        if (sprite.empty() && !p.empty()) sprite = p;
        std::string n = unquoteText(other->value(v, "name"));
        if (name.empty() && !n.empty()) name = n;
      }
    }
  }
  previewName_->text = name;
  previewText_->text = words;
  std::string path = spritePath(sprite);
  if (path.empty()) return;
  try
  {
    sprite::Sprite s = sprite::load(path);
    if (!s.image || s.frames.empty()) return;
    // Its first frame, four times as large.
    const sprite::Frame &fr = s.frames[0];
    std::vector<uint8_t> rgba(size_t(fr.w * fr.h * 4));
    for (int y = 0; y < fr.h; y++)
      for (int x = 0; x < fr.w; x++)
        for (int c = 0; c < 4; c++)
        {
          int sx = fr.x + x, sy = fr.y + y;
          if (sx < s.image->width && sy < s.image->height)
            rgba[size_t((y * fr.w + x) * 4 + c)] = s.image->rgba[size_t((sy * s.image->width + sx) * 4 + c)];
        }
    preview_->texture = renderer_->createTexture(fr.w, fr.h, rgba.data(), true);
    preview_->textureSize = {float(fr.w * 4), float(fr.h * 4)};
    preview_->customMinSize = preview_->textureSize;
  }
  catch (const std::exception &)
  {
  }
}

void DataPanel::update(float)
{
  if (!rebuild_) return;
  rebuild_ = false;
  // Keep the file as the list shows it (a commit may have added lines).
  fillRecords();
  buildForm();
}

void DataPanel::newRecord()
{
  if (current_ >= files_.size()) return;
  std::string table = files_[current_].table;
  std::vector<std::string> bases{"(none)"};
  for (const DataRecord &r : files_[current_].records) bases.push_back(r.name);
  openForm(*ui_, "New " + table, {{"Name", "", FormField::Kind::Text, {}, 0, 0, {}}, {"Copy of", "(none)", FormField::Kind::Choice, bases, 0, 0, {}}},
           "Create", [this](const std::vector<std::string> &values, std::string *pError) {
             if (hasRecord(values[0]))
             {
               *pError = "there is a record " + values[0] + " already";
               return false;
             }
             DataFile &file = files_[current_];
             if (!file.add(values[0], values[1] == "(none)" ? "" : values[1], pError)) return false;
             if (!file.save(pError)) return false;
             record_ = values[0];
             rebuild_ = true;
             if (changed) changed();
             return true;
           });
}

void DataPanel::duplicateRecord()
{
  if (current_ >= files_.size() || record_.empty()) return;
  std::string name = record_ + "Copy";
  for (int n = 2; hasRecord(name); n++) name = record_ + "Copy" + std::to_string(n);
  DataFile &file = files_[current_];
  std::string error;
  // A copy: a new record based on this one.
  if (!file.add(name, record_, &error) || !file.save(&error))
  {
    if (log) log(error, true);
    return;
  }
  record_ = name;
  rebuild_ = true;
  if (changed) changed();
}

void DataPanel::deleteRecord()
{
  if (current_ >= files_.size() || record_.empty()) return;
  std::string name = record_;
  openConfirm(*ui_, "Delete record", "Delete the record " + name + " from " + fs::path(files_[current_].path).filename().string() +
                                         "? Records based on it or naming it stop cooking until they are changed.",
              "Delete", [this, name] {
                DataFile &file = files_[current_];
                std::string error;
                if (!file.remove(name) || !file.save(&error))
                {
                  if (log) log(error.empty() ? "no record " + name : error, true);
                  return;
                }
                record_ = file.records.empty() ? std::string() : file.records[0].name;
                rebuild_ = true;
                if (changed) changed();
              });
}

void DataPanel::recordMenu(int index, ose::Vec2 at)
{
  const DataFile &f = files_[current_];
  if (index >= 0 && size_t(index) < f.records.size())
  {
    record_ = f.records[size_t(index)].name;
    records_->selected = index;
    rebuild_ = true;
  }
  std::vector<MenuItem> items;
  auto item = [](const std::string &label, const std::string &icon, std::function<void()> run) {
    MenuItem m;
    m.label = label;
    m.icon = icon;
    m.run = std::move(run);
    return m;
  };
  items.push_back(item("New record...", "plus", [this] { newRecord(); }));
  if (index >= 0)
  {
    items.push_back(item("Duplicate", "copy", [this] { duplicateRecord(); }));
    items.push_back(item("Delete...", "trash", [this] { deleteRecord(); }));
  }
  PopupMenu::open(*ui_, items, Rect{at.x, at.y, 0, 0}, PopupPlacement::AtPoint);
}

}  // namespace bm::editor
