// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Animation tab: a sprite (docs/spec/sprite-format.md) with its sheet, frames (rectangle,
// anchor, hitbox), animations and steps. The preview is drawn from the cooked sprite, so it
// shows the tiles, colours and hardware sprite pieces the device gets. Every edit is saved to
// the .bmsprite file shortly after; the editor then cooks again and, with Live, the running
// game shows it.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "sprite_asset.h"
#include "ui/control.h"
#include "ui/core.h"

namespace ose::ui {
class Button;
class CheckBox;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class Renderer;
class SpinBox;
}  // namespace ose::ui

namespace bm::editor {

/// The sheet image with the frames outlined; a click selects the frame under it.
class SheetView : public ose::ui::Control
{
public:
  const char *typeName() const override { return "SheetView"; }
  ose::Vec2 computeMinSize() override;
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;

  const sprite::Sprite *sprite = nullptr;
  ose::ui::TextureHandle texture;
  float zoom = 3;
  int selected = -1;
  std::function<void(int)> picked;
};

/// One cooked frame, enlarged, with the anchor, the hitbox and the hardware sprite pieces.
class AnimPreview : public ose::ui::Control
{
public:
  const char *typeName() const override { return "AnimPreview"; }
  ose::Vec2 computeMinSize() override { return {200, 200}; }
  void draw(ose::ui::Canvas &c) override;

  struct FrameImage
  {
    ose::ui::TextureHandle texture;
    int x = 0, y = 0, w = 0, h = 0;  // relative to the anchor
  };
  const sprite::Cooked *cooked = nullptr;
  std::vector<FrameImage> images;
  int frame = 0;
  float zoom = 4;
  bool showHitbox = true, showPieces = true, showBoxes = true;
  int selectedBox = -1;   // drawn thicker: the box being edited
};

void registerAnimationControls();

class AnimationPanel
{
public:
  /// The sprite file was saved: cook again (and push to the running game).
  std::function<void()> changed;
  std::function<void(const std::string &line, bool error)> log;

  void bind(ose::ui::Control &root, ose::ui::Renderer &renderer);
  /// The sprites of the project (for the chooser).
  void setSprites(const std::vector<std::string> &paths);
  void open(const std::string &path);
  const std::string &path() const { return sprite_.path; }
  /// Advances the preview and saves pending edits.
  void update(float dt);

private:
  void rebuild();          // after any change: cook, pictures, lists
  void fillLists();
  void showFrame();
  void showStep();
  void edited();           // an edit was made: save soon
  void save();
  void slice();
  std::vector<sprite::NamedBox> *boxList();   // where Add puts a box: the frame's, or the sprite's
  /// The box of the list's entry `i`: one of the frame's own, or one of the sprite's (also
  /// listed for a frame that has no box of that kind itself: it is the frame's too).
  sprite::NamedBox *listedBox(int i);
  void fillBoxes();
  void boxEdited();
  int currentAnim() const;
  sprite::Frame *currentFrame();

  ose::ui::Renderer *renderer_ = nullptr;
  std::vector<std::string> paths_;
  sprite::Sprite sprite_;
  struct ListedBox
  {
    bool sprites;       // in sprite_.boxes, else in the frame's boxes
    size_t index;
  };
  std::vector<ListedBox> listed_;   // the boxes list's entries
  sprite::Cooked cooked_;
  bool loaded_ = false, updating_ = false, dirty_ = false, playing_ = true;
  float saveTimer_ = 0, playTime_ = 0;
  int playStep_ = 0, playTimer_ = 0;

  SheetView *sheet_ = nullptr;
  AnimPreview *preview_ = nullptr;
  ose::ui::OptionButton *chooser_ = nullptr;
  ose::ui::ItemList *anims_ = nullptr, *steps_ = nullptr;
  ose::ui::LineEdit *animName_ = nullptr;
  ose::ui::CheckBox *loop_ = nullptr, *ownHitbox_ = nullptr;
  ose::ui::SpinBox *duration_ = nullptr, *gridW_ = nullptr, *gridH_ = nullptr;
  ose::ui::SpinBox *rect_[4] = {}, *anchor_[2] = {}, *hitbox_[4] = {};
  ose::ui::Label *frameTitle_ = nullptr, *cost_ = nullptr, *info_ = nullptr;
  ose::ui::Button *play_ = nullptr;
  ose::ui::ItemList *boxes_ = nullptr;
  ose::ui::CheckBox *boxSprite_ = nullptr;
  ose::ui::OptionButton *boxKind_ = nullptr;
  ose::ui::SpinBox *box_[4] = {};
  ose::ui::LineEdit *boxName_ = nullptr;
};

}  // namespace bm::editor
