// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The graphics inspector: what is loaded in video memory right now and how much of the
// hardware it takes, for the editor's preview of a room or for a snapshot of the running game
// (PPU_SNAPSHOT over the live link).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ui/control.h"
#include "ui/core.h"
#include "vram.h"

namespace ose::ui {
class ItemList;
class Label;
class ProgressBar;
class Renderer;
class TextureRect;
class VBox;
}  // namespace ose::ui

namespace bm::editor {

/// All 96 KB of VRAM as tiles, 32 per row: char blocks 0-3 (backgrounds) and 4-5 (sprites).
/// Maps are tinted per background, overlaps outlined in red, free space dark. Hovering shows
/// what a tile of memory is used for.
class VramView : public ose::ui::Control
{
public:
  const char *typeName() const override { return "VramView"; }
  ose::Vec2 computeMinSize() override;
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;
  void mouseExited() override;

  const VramAnalysis *analysis = nullptr;
  ose::ui::TextureHandle texture;
  float zoom = 1.5f;
  std::function<void(int unit)> hovered;  // -1 when the mouse leaves

private:
  int hover_ = -1;
  int unitAt(ose::Vec2 p) const;
};

/// 256 colours (BG or OBJ) as 16 banks of 16. Banks not in use are dimmed; entry 0 of each
/// bank is hatched (transparent). A click selects an entry.
class PaletteView : public ose::ui::Control
{
public:
  const char *typeName() const override { return "PaletteView"; }
  ose::Vec2 computeMinSize() override { return {16 * cell + 2, 16 * cell + 2}; }
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;

  const uint16_t *colours = nullptr;  // 256 entries, BGR555
  uint16_t usedBanks = 0;
  bool backdrop = true;               // entry 0 of bank 0 is the backdrop colour
  int selected = -1;
  float cell = 14;
  std::function<void(int index)> picked;
};

/// Sprite rendering time per line against what the hardware has.
class LineCostView : public ose::ui::Control
{
public:
  const char *typeName() const override { return "LineCostView"; }
  ose::Vec2 computeMinSize() override { return {120, BM_PPU_HEIGHT}; }
  void draw(ose::ui::Canvas &c) override;
  const VramAnalysis *analysis = nullptr;
};

void registerGraphicsControls();

/// The Graphics tab: binds the controls of the layout (ids gfx_*) and fills them.
class GraphicsInspector
{
public:
  void bind(ose::ui::Control &root, ose::ui::Renderer &renderer);
  /// Shows a video state; `source` says where it came from.
  void show(const BmPpuState &state, const std::string &source);
  const BmPpuState *state() const { return state_ ? state_.get() : nullptr; }

private:
  ose::ui::Renderer *renderer_ = nullptr;
  std::unique_ptr<BmPpuState> state_;
  VramAnalysis analysis_;
  VramView *vram_ = nullptr;
  PaletteView *bgPalette_ = nullptr, *objPalette_ = nullptr;
  LineCostView *lineCost_ = nullptr;
  ose::ui::TextureRect *screen_ = nullptr;
  ose::ui::ItemList *layers_ = nullptr, *sprites_ = nullptr, *notes_ = nullptr;
  ose::ui::VBox *budgets_ = nullptr;
  ose::ui::Label *source_ = nullptr, *hover_ = nullptr;
};

}  // namespace bm::editor
