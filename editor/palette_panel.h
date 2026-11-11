// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Palette tweaks: pick a colour of the cooked palette, see which source colours of which
// images became it, and change it. The change is stored as an override next to each source
// image (<image>.bmasset, keyed by profile; docs/spec/asset-model.md), never in the image,
// and takes effect at the next cook - which the editor starts at once, so with "Live" the
// running game (PC or GBA) shows the new colour a moment later.
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>

namespace ose::ui {
class ColorRect;
class Control;
class ItemList;
class Label;
class OptionButton;
class SpinBox;
class Button;
}  // namespace ose::ui

namespace bm::editor {

class PaletteView;
class Project;
struct AssetCatalog;

class PalettePanel
{
public:
  /// Overrides changed on disk: cook again (and push to the running game).
  std::function<void()> changed;
  std::function<void(const std::string &line, bool error)> log;
  const AssetCatalog *catalog = nullptr;

  void bind(ose::ui::Control &root, Project &project);
  /// After every cook: shows the new palette and keeps the same colour selected.
  void refresh();
  /// Call every frame: writes edits at most a few times per second.
  void update(float dt);

private:
  void pick(int index);
  void edited();
  void apply();
  void reset();
  void showSelection();

  Project *project_ = nullptr;
  PaletteView *view_ = nullptr;
  ose::ui::ColorRect *swatch_ = nullptr;
  ose::ui::SpinBox *channels_[3] = {};
  ose::ui::Label *title_ = nullptr;
  ose::ui::ItemList *sources_ = nullptr;
  ose::ui::OptionButton *scope_ = nullptr;
  ose::ui::Button *reset_ = nullptr;
  int index_ = -1;
  std::set<std::pair<std::string, uint32_t>> selected_;  // the source colours behind it
  bool pending_ = false, updating_ = false;
  float timer_ = 0;
};

}  // namespace bm::editor
