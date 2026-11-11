// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "palette_panel.h"

#include <cstdio>
#include <filesystem>

#include "graphics_view.h"
#include "overrides.h"
#include "project.h"
#include "project_file.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/widgets.h"

using namespace ose::ui;

namespace bm::editor {

namespace {

uint32_t expand5(unsigned v)
{
  return (v << 3) | (v >> 2);
}

uint32_t rgbOf(uint16_t bgr555)
{
  return expand5(bgr555 & 31) << 16 | expand5((bgr555 >> 5) & 31) << 8 | expand5((bgr555 >> 10) & 31);
}

std::string fileName(const std::string &path)
{
  return std::filesystem::path(path).filename().string();
}

}  // namespace

void PalettePanel::bind(Control &root, Project &project)
{
  project_ = &project;
  view_ = root.findAs<PaletteView>("room_palette");
  swatch_ = root.findAs<ColorRect>("colour_swatch");
  channels_[0] = root.findAs<SpinBox>("colour_r");
  channels_[1] = root.findAs<SpinBox>("colour_g");
  channels_[2] = root.findAs<SpinBox>("colour_b");
  title_ = root.findAs<Label>("colour_title");
  sources_ = root.findAs<ItemList>("colour_sources");
  scope_ = root.findAs<OptionButton>("colour_scope");
  reset_ = root.findAs<Button>("colour_reset");
  sources_->tileSize = 0;
  for (SpinBox *s : channels_)
  {
    s->minValue = 0;
    s->maxValue = 31;
    s->decimals = 0;
    s->valueChanged.connect([this](double) {
      if (!updating_) edited();
    });
  }
  view_->picked = [this](int i) { pick(i); };
  reset_->clicked.connect([this] { reset(); });
  scope_->select(0, false);
  showSelection();
}

void PalettePanel::refresh()
{
  const auto &pal = project_->palette();
  view_->colours = pal.size() >= 256 ? pal.data() : nullptr;
  view_->usedBanks = uint16_t((1u << project_->usedBanks()) - 1);
  scope_->setItems({"Only for " + project_->profile(), "For all profiles"});
  // Keep the colour that was selected: find where its source colours went.
  if (!selected_.empty())
  {
    const auto &sources = project_->cookInfo().colourSources;
    int found = -1;
    for (int i = 0; i < 256 && found < 0 && i < int(pal.size()); i++)
    {
      if (i % 16 == 0 || i >= 16 * project_->usedBanks()) continue;
      auto it = sources.find(pal[size_t(i)]);
      if (it == sources.end()) continue;
      for (const auto &s : selected_)
        if (it->second.count(s)) found = i;
    }
    if (found >= 0) index_ = found;
    else
    {
      // Not in this room's palette (another room is shown now).
      index_ = -1;
      selected_.clear();
    }
  }
  view_->selected = index_;
  showSelection();
}

void PalettePanel::pick(int index)
{
  index_ = index;
  selected_.clear();
  const auto &pal = project_->palette();
  if (index >= 0 && size_t(index) < pal.size() && index % 16 != 0)
  {
    auto it = project_->cookInfo().colourSources.find(pal[size_t(index)]);
    if (it != project_->cookInfo().colourSources.end()) selected_ = it->second;
  }
  showSelection();
}

void PalettePanel::showSelection()
{
  sources_->items.clear();
  const auto &pal = project_ ? project_->palette() : std::vector<uint16_t>();
  bool valid = index_ >= 0 && size_t(index_) < pal.size();
  for (SpinBox *s : channels_) s->disabled = !valid || selected_.empty();
  reset_->disabled = true;
  if (!valid)
  {
    title_->text = "Click a colour of the palette";
    swatch_->fill = {0.5f, 0.5f, 0.5f, 1};
    return;
  }
  uint16_t c = pal[size_t(index_)];
  char buf[96];
  std::snprintf(buf, sizeof(buf), "Bank %d, colour %d: %s (BGR555 0x%04X)", index_ / 16, index_ % 16, rgbHex(rgbOf(c)).c_str(), c);
  title_->text = buf;
  updating_ = true;
  for (int k = 0; k < 3; k++) channels_[k]->setValue((c >> (5 * k)) & 31, false);
  updating_ = false;
  swatch_->fill = {float(c & 31) / 31, float((c >> 5) & 31) / 31, float((c >> 10) & 31) / 31, 1};
  if (index_ % 16 == 0)
  {
    sources_->items.push_back({index_ == 0 ? "The backdrop: set it with the room property \"backdrop\"" : "Colour 0 of a bank is transparent",
                               "info", {}, {}, {}, {}});
    return;
  }
  if (selected_.empty())
  {
    sources_->items.push_back({"No source colour known (cook again)", "warning", {}, {}, {}, {}});
    return;
  }
  const auto &overridden = project_->cookInfo().overridden;
  std::set<std::string> assets;
  for (const auto &[asset, rgb] : selected_)
  {
    assets.insert(asset);
    std::string text = rgbHex(rgb) + " in " + fileName(asset);
    auto o = overridden.find({asset, rgb});
    if (o != overridden.end())
    {
      text += ", replaced by " + rgbHex(o->second);
      reset_->disabled = false;
    }
    sources_->items.push_back({text, o != overridden.end() ? "sliders" : "image", {}, {}, asset, {}});
  }
  if (selected_.size() > 1)
    sources_->items.push_back({std::to_string(selected_.size()) + " source colours became this one; a change applies to all of them",
                               "info", {}, {}, {}, {}});
  if (catalog)
    for (const std::string &a : assets)
      if (const AssetInfo *info = catalog->find(a); info && info->usedBy.size() > 1)
        sources_->items.push_back({fileName(a) + " is shared by " + std::to_string(info->usedBy.size()) + " rooms: they all change",
                                   "warning", {}, {}, {}, {}});
}

void PalettePanel::edited()
{
  uint16_t c = uint16_t(int(channels_[0]->value()) | int(channels_[1]->value()) << 5 | int(channels_[2]->value()) << 10);
  swatch_->fill = {float(c & 31) / 31, float((c >> 5) & 31) / 31, float((c >> 10) & 31) / 31, 1};
  pending_ = true;
  timer_ = 0;
}

void PalettePanel::update(float dt)
{
  if (!pending_) return;
  timer_ += dt;
  if (timer_ >= 0.2f) apply();
}

void PalettePanel::apply()
{
  pending_ = false;
  if (selected_.empty()) return;
  uint16_t c = uint16_t(int(channels_[0]->value()) | int(channels_[1]->value()) << 5 | int(channels_[2]->value()) << 10);
  uint32_t rgb = rgbOf(c);
  std::string profile = project_->profile();
  bool everywhere = scope_->selected() == 1;
  for (const auto &[asset, source] : selected_)
  {
    try
    {
      AssetOverrides o = loadOverrides(asset);
      if (everywhere)
      {
        o.colours["*"][source] = rgb;
        o.colours[profile].erase(source);  // a profile's own entry would win
      }
      else
        o.colours[profile][source] = rgb;
      if (!saveOverrides(asset, o) && log) log("cannot write " + overridesPath(asset), true);
    }
    catch (const std::exception &e)
    {
      if (log) log(e.what(), true);
    }
  }
  if (changed) changed();
}

void PalettePanel::reset()
{
  std::string profile = project_->profile();
  for (const auto &[asset, source] : selected_)
  {
    try
    {
      AssetOverrides o = loadOverrides(asset);
      o.colours[profile].erase(source);
      o.colours["*"].erase(source);
      saveOverrides(asset, o);
    }
    catch (const std::exception &e)
    {
      if (log) log(e.what(), true);
    }
  }
  if (changed) changed();
}

}  // namespace bm::editor
