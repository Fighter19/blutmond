// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "animation_panel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "ui/canvas.h"
#include "ui/lists.h"
#include "ui/loader.h"
#include "ui/menus.h"
#include "ui/renderer.h"
#include "ui/theme.h"
#include "theme_colors.h"
#include "ui/widgets.h"

using namespace ose::ui;
using ose::Vec2;

namespace bm::editor {

namespace {

constexpr float kFrameSeconds = 280896.0f / 16777216.0f;  // one GBA frame

std::string fileName(const std::string &path)
{
  return std::filesystem::path(path).filename().string();
}

void checker(Canvas &c, const Rect &r, float cell)
{
  c.rect(r, parseColor("#5a6a7a"));
  c.pushClip(r);
  for (int y = 0; y * cell < r.h; y++)
    for (int x = (y & 1); x * cell < r.w; x += 2) c.rect({r.x + x * cell, r.y + y * cell, cell, cell}, parseColor("#64748a"));
  c.popClip();
}

// A point: a crosshair and a line along its angle (256 a turn, 0 forward: to the right).
template <typename Font> void drawPoint(Canvas &c, Vec2 at, const sprite::Point &p, float zoom, bool selected, const Font &fnt, float size)
{
  Color col = p.behind ? Color{0.75f, 0.4f, 1, 1} : Color{1, 0.55f, 0.1f, 1};
  float r = selected ? 6.0f : 4.0f;
  c.line({at.x - r, at.y}, {at.x + r, at.y}, col, selected ? 2.5f : 1.5f);
  c.line({at.x, at.y - r}, {at.x, at.y + r}, col, selected ? 2.5f : 1.5f);
  float a = float(p.angle) * 6.2831853f / 256.0f, len = 6 * zoom;
  c.line(at, {at.x + std::cos(a) * len, at.y + std::sin(a) * len}, col, 1.5f);
  c.text({at.x + 4, at.y + 2}, p.name, fnt, size * 0.75f, col);
}

}  // namespace

// --- SheetView ---------------------------------------------------------------------------

Vec2 SheetView::computeMinSize()
{
  if (!sprite || !sprite->image) return {64, 64};
  return {float(sprite->image->width) * zoom, float(sprite->image->height) * zoom};
}

bool SheetView::event(const InputEvent &e)
{
  if (e.type != InputEvent::Type::MouseDown || e.button != MouseButton::Left || !sprite) return false;
  int x = int((e.pos.x - rect_.x) / zoom), y = int((e.pos.y - rect_.y) / zoom);
  for (size_t i = 0; i < sprite->frames.size(); i++)
  {
    const sprite::Frame &f = sprite->frames[i];
    if (x >= f.x && y >= f.y && x < f.x + f.w && y < f.y + f.h)
    {
      selected = int(i);
      if (picked) picked(selected);
      return true;
    }
  }
  return true;
}

void SheetView::draw(Canvas &c)
{
  if (!sprite || !sprite->image || !texture)
  {
    c.rect(rect_, parseColor("#404040"));
    return;
  }
  Rect r{rect_.x, rect_.y, float(sprite->image->width) * zoom, float(sprite->image->height) * zoom};
  checker(c, r, 4 * zoom);
  c.image(r, texture);
  for (size_t i = 0; i < sprite->frames.size(); i++)
  {
    const sprite::Frame &f = sprite->frames[i];
    Rect fr{rect_.x + f.x * zoom, rect_.y + f.y * zoom, f.w * zoom, f.h * zoom};
    bool sel = int(i) == selected;
    c.border(fr, sel ? Color{1, 1, 1, 1} : Color{0, 0, 0, 0.45f}, sel ? 2.0f : 1.0f);
    c.text({fr.x + 2, fr.y + 1}, std::to_string(i), font(), fontSize() * 0.8f, sel ? Color{1, 1, 1, 1} : Color{0, 0, 0, 0.7f});
    if (sel)
    {
      float ax = fr.x + f.ax * zoom, ay = fr.y + f.ay * zoom;
      c.line({ax - 5, ay}, {ax + 5, ay}, {1, 0.9f, 0.2f, 1}, 2);
      c.line({ax, ay - 5}, {ax, ay + 5}, {1, 0.9f, 0.2f, 1}, 2);
    }
  }
}

// --- AnimPreview -------------------------------------------------------------------------

ose::Vec2 AnimPreview::anchor() const
{
  // The anchor sits at three quarters of the height, so figures stand on a line.
  return {rect_.x + rect_.w * 0.5f, rect_.y + rect_.h * 0.75f};
}

int AnimPreview::hitPoint(ose::Vec2 p) const
{
  if (!editable || !composite.empty()) return -1;
  Vec2 a = anchor();
  for (int i = int(points.size()) - 1; i >= 0; i--)
  {
    float x = a.x + points[size_t(i)].x * zoom, y = a.y + points[size_t(i)].y * zoom;
    if (std::abs(p.x - x) <= 6 && std::abs(p.y - y) <= 6) return i;
  }
  return -1;
}

int AnimPreview::hit(ose::Vec2 p, int *pEdges) const
{
  *pEdges = 0;
  if (!editable || !showBoxes) return -1;
  Vec2 a = anchor();
  const float grab = 5;
  // The selected box first (it may lie under another), then the last drawn.
  std::vector<int> order;
  if (selectedBox >= 0 && size_t(selectedBox) < editBoxes.size()) order.push_back(selectedBox);
  for (int i = int(editBoxes.size()) - 1; i >= 0; i--)
    if (i != selectedBox) order.push_back(i);
  for (int i : order)
  {
    const sprite::Box &b = editBoxes[size_t(i)].box;
    float x0 = a.x + b.x * zoom, y0 = a.y + b.y * zoom, x1 = x0 + b.w * zoom, y1 = y0 + b.h * zoom;
    if (p.x < x0 - grab || p.x > x1 + grab || p.y < y0 - grab || p.y > y1 + grab) continue;
    int edges = 0;
    if (std::abs(p.x - x0) <= grab) edges |= Left;
    else if (std::abs(p.x - x1) <= grab) edges |= Right;
    if (std::abs(p.y - y0) <= grab) edges |= Top;
    else if (std::abs(p.y - y1) <= grab) edges |= Bottom;
    if (edges == 0 && (p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1)) continue;
    *pEdges = edges ? edges : Move;
    return i;
  }
  return -1;
}

ose::ui::Cursor AnimPreview::cursor(ose::Vec2 p) const
{
  int edges = 0;
  int mode = drag_ >= 0 ? dragMode_ : (hit(p, &edges), edges);
  if (mode & (Left | Right)) return (mode & (Top | Bottom)) ? Cursor::Move : Cursor::ResizeH;
  if (mode & (Top | Bottom)) return Cursor::ResizeV;
  if (mode & Move) return Cursor::Move;
  return editable && showBoxes ? Cursor::Crosshair : Cursor::Arrow;
}

bool AnimPreview::event(const InputEvent &e)
{
  switch (e.type)
  {
  case InputEvent::Type::MouseDown:
  {
    if (e.button != MouseButton::Left) return false;
    if (pressed) pressed();   // stops playing: the boxes belong to the frame shown now
    // A point first (they are small and sit on boxes).
    dragPoint_ = hitPoint(e.pos);
    if (dragPoint_ >= 0)
    {
      dragFrom_ = e.pos;
      dragStart_ = {points[size_t(dragPoint_)].x, points[size_t(dragPoint_)].y, 0, 0};
      dragged_ = false;
      if (pointSelected) pointSelected(dragPoint_);
      return true;
    }
    if (!editable || !showBoxes) return true;
    int edges = 0;
    drag_ = hit(e.pos, &edges);
    dragMode_ = drag_ >= 0 ? edges : Draw;
    dragFrom_ = e.pos;
    dragged_ = false;
    if (drag_ >= 0)
    {
      dragStart_ = editBoxes[size_t(drag_)].box;
      if (boxSelected) boxSelected(drag_);
    }
    else
    {
      Vec2 a = anchor();
      dragStart_ = {int(std::floor((e.pos.x - a.x) / zoom)), int(std::floor((e.pos.y - a.y) / zoom)), 0, 0};
    }
    return true;
  }
  case InputEvent::Type::MouseMove:
  {
    if (dragPoint_ >= 0)
    {
      int x = std::clamp(dragStart_.x + int(std::lround((e.pos.x - dragFrom_.x) / zoom)), -128, 127);
      int y = std::clamp(dragStart_.y + int(std::lround((e.pos.y - dragFrom_.y) / zoom)), -128, 127);
      if (x == points[size_t(dragPoint_)].x && y == points[size_t(dragPoint_)].y) return true;
      dragged_ = true;
      points[size_t(dragPoint_)].x = x;
      points[size_t(dragPoint_)].y = y;
      if (pointMoved) pointMoved(dragPoint_, x, y, false);
      return true;
    }
    if (drag_ < 0 && dragMode_ != Draw) return false;
    int dx = int(std::lround((e.pos.x - dragFrom_.x) / zoom)), dy = int(std::lround((e.pos.y - dragFrom_.y) / zoom));
    if (dx == 0 && dy == 0 && !dragged_) return true;
    dragged_ = true;
    sprite::Box b = dragStart_;
    if (dragMode_ == Draw)
    {
      // From where it went down to the pointer, in whole pixels, at least 1 x 1.
      int x1 = b.x + dx, y1 = b.y + dy;
      b = {std::min(b.x, x1), std::min(b.y, y1), std::max(1, std::abs(x1 - b.x)), std::max(1, std::abs(y1 - b.y))};
      drawing_ = b;
      return true;
    }
    if (dragMode_ & Move)
    {
      b.x += dx;
      b.y += dy;
    }
    if (dragMode_ & Left)
    {
      int x1 = b.x + b.w;
      b.x = std::min(b.x + dx, x1 - 1);
      b.w = x1 - b.x;
    }
    if (dragMode_ & Right) b.w = std::max(1, b.w + dx);
    if (dragMode_ & Top)
    {
      int y1 = b.y + b.h;
      b.y = std::min(b.y + dy, y1 - 1);
      b.h = y1 - b.y;
    }
    if (dragMode_ & Bottom) b.h = std::max(1, b.h + dy);
    b.x = std::clamp(b.x, -128, 127);
    b.y = std::clamp(b.y, -128, 127);
    b.w = std::clamp(b.w, 1, 255);
    b.h = std::clamp(b.h, 1, 255);
    editBoxes[size_t(drag_)].box = b;
    if (boxChanged) boxChanged(drag_, b, false);
    return true;
  }
  case InputEvent::Type::MouseUp:
  {
    if (dragPoint_ >= 0)
    {
      int i = dragPoint_;
      dragPoint_ = -1;
      if (dragged_ && pointMoved) pointMoved(i, points[size_t(i)].x, points[size_t(i)].y, true);
      return true;
    }
    if (drag_ >= 0 && dragged_ && boxChanged) boxChanged(drag_, editBoxes[size_t(drag_)].box, true);
    if (dragMode_ == Draw && dragged_ && drawing_.w > 0 && boxDrawn) boxDrawn(drawing_);
    bool any = drag_ >= 0 || dragMode_ == Draw;
    drag_ = -1;
    dragMode_ = 0;
    drawing_ = {};
    return any;
  }
  default:
    return false;
  }
}

void AnimPreview::draw(Canvas &c)
{
  checker(c, rect_, 16);
  if (!composite.empty())
  {
    // A figure: its layers back to front, their pieces and points.
    c.pushClip(rect_);
    Vec2 a = anchor();
    c.rect({rect_.x, a.y, rect_.w, 1}, {0, 0, 0, 0.35f});
    for (const Layer &l : composite)
      if (l.texture) c.image({a.x + l.x * zoom, a.y + l.y * zoom, l.w * zoom, l.h * zoom}, l.texture);
    for (const Layer &l : composite)
    {
      if (showPieces)
        for (const sprite::Piece &p : l.pieces) c.border({a.x + p.dx * zoom, a.y + p.dy * zoom, p.w * zoom, p.h * zoom}, {0.2f, 1, 1, 0.8f}, 1);
      for (const sprite::Point &p : l.points) drawPoint(c, {a.x + p.x * zoom, a.y + p.y * zoom}, p, zoom, false, font(), fontSize());
    }
    if (dial >= 0)
    {
      float d = float(dial) * 6.2831853f / 256.0f;
      c.line(a, {a.x + std::cos(d) * 40 * zoom, a.y + std::sin(d) * 40 * zoom}, {1, 1, 1, 0.6f}, 1);
    }
    c.line({a.x - 6, a.y}, {a.x + 6, a.y}, {1, 0.9f, 0.2f, 1}, 2);
    c.line({a.x, a.y - 6}, {a.x, a.y + 6}, {1, 0.9f, 0.2f, 1}, 2);
    c.popClip();
    return;
  }
  if (!cooked || frame < 0 || size_t(frame) >= cooked->frames.size() || size_t(frame) >= images.size()) return;
  c.pushClip(rect_);
  const sprite::CookedFrame &f = cooked->frames[size_t(frame)];
  // The anchor sits at three quarters of the height, so figures stand on a line.
  float ax = rect_.x + rect_.w * 0.5f, ay = rect_.y + rect_.h * 0.75f;
  c.rect({rect_.x, ay, rect_.w, 1}, {0, 0, 0, 0.35f});
  const FrameImage &img = images[size_t(frame)];
  if (img.texture) c.image({ax + img.x * zoom, ay + img.y * zoom, img.w * zoom, img.h * zoom}, img.texture);
  if (showPieces)
    for (const sprite::Piece &p : f.pieces) c.border({ax + p.dx * zoom, ay + p.dy * zoom, p.w * zoom, p.h * zoom}, {0.2f, 1, 1, 0.8f}, 1);
  if (showHitbox && f.hitbox.w > 0)
    c.border({ax + f.hitbox.x * zoom, ay + f.hitbox.y * zoom, f.hitbox.w * zoom, f.hitbox.h * zoom}, {1, 0.25f, 0.25f, 1}, 2);
  // The boxes: the list's while they belong to the frame shown (they can be edited), else the
  // cooked frame's.
  std::vector<EditBox> shown = editBoxes;
  if (!editable)
  {
    shown.clear();
    for (const sprite::NamedBox &b : f.boxes) shown.push_back({b.box, b.kind});
  }
  if (showBoxes)
    for (size_t i = 0; i < shown.size(); i++)
    {
      const EditBox &b = shown[i];
      Color col = b.kind == sprite::BoxKind::Hit ? Color{1, 0.2f, 0.2f, 1} : b.kind == sprite::BoxKind::Guard ? Color{0.3f, 0.5f, 1, 1} : Color{0.2f, 0.9f, 0.3f, 1};
      Rect r{ax + b.box.x * zoom, ay + b.box.y * zoom, b.box.w * zoom, b.box.h * zoom};
      c.rect(r, {col.x, col.y, col.z, 0.18f});
      bool selected = editable && int(i) == selectedBox;
      c.border(r, col, selected ? 3.0f : 1.5f);
      if (selected)
        // Handles at the corners: drag them to resize.
        for (float hx : {r.x, r.x + r.w})
          for (float hy : {r.y, r.y + r.h}) c.rect({hx - 3, hy - 3, 6, 6}, col);
    }
  if (drawing_.w > 0)
    c.border({ax + drawing_.x * zoom, ay + drawing_.y * zoom, drawing_.w * zoom, drawing_.h * zoom}, {1, 1, 1, 0.9f}, 1.5f);
  // The points: the list's while editable, else the cooked frame's.
  const std::vector<sprite::Point> &pts = editable ? points : f.points;
  for (size_t i = 0; i < pts.size(); i++) drawPoint(c, {ax + pts[i].x * zoom, ay + pts[i].y * zoom}, pts[i], zoom, editable && int(i) == selectedPoint, font(), fontSize());
  if (dial >= 0)
  {
    // The dial's direction, to compare with the hand's.
    float d = float(dial) * 6.2831853f / 256.0f;
    c.line({ax, ay}, {ax + std::cos(d) * 40 * zoom, ay + std::sin(d) * 40 * zoom}, {1, 1, 1, 0.6f}, 1);
  }
  c.line({ax - 6, ay}, {ax + 6, ay}, {1, 0.9f, 0.2f, 1}, 2);
  c.line({ax, ay - 6}, {ax, ay + 6}, {1, 0.9f, 0.2f, 1}, 2);
  c.popClip();
}

void registerAnimationControls()
{
  registerControlType("SheetView", [] { return std::make_unique<SheetView>(); });
  registerControlType("AnimPreview", [] { return std::make_unique<AnimPreview>(); });
}

// --- AnimationPanel ----------------------------------------------------------------------

void AnimationPanel::bind(Control &root, Renderer &renderer)
{
  renderer_ = &renderer;
  sheet_ = root.findAs<SheetView>("anim_sheet");
  preview_ = root.findAs<AnimPreview>("anim_preview");
  chooser_ = root.findAs<OptionButton>("anim_sprite");
  anims_ = root.findAs<ItemList>("anim_list");
  steps_ = root.findAs<ItemList>("anim_steps");
  animName_ = root.findAs<LineEdit>("anim_name");
  loop_ = root.findAs<CheckBox>("anim_loop");
  ownHitbox_ = root.findAs<CheckBox>("anim_own_hitbox");
  duration_ = root.findAs<SpinBox>("anim_duration");
  gridW_ = root.findAs<SpinBox>("anim_grid_w");
  gridH_ = root.findAs<SpinBox>("anim_grid_h");
  frameTitle_ = root.findAs<Label>("anim_frame_title");
  cost_ = root.findAs<Label>("anim_cost");
  info_ = root.findAs<Label>("anim_info");
  play_ = root.findAs<Button>("anim_play");
  static const char *rectIds[4] = {"anim_fx", "anim_fy", "anim_fw", "anim_fh"};
  static const char *hitIds[4] = {"anim_hx", "anim_hy", "anim_hw", "anim_hh"};
  for (int i = 0; i < 4; i++)
  {
    rect_[i] = root.findAs<SpinBox>(rectIds[i]);
    hitbox_[i] = root.findAs<SpinBox>(hitIds[i]);
  }
  boxes_ = root.findAs<ItemList>("anim_boxes");
  boxSprite_ = root.findAs<CheckBox>("anim_box_sprite");
  boxScope_ = root.findAs<OptionButton>("anim_box_scope");
  boxScope_->setItems({"This frame", "The whole sprite"});
  boxScope_->itemSelected.connect([this](int i) {
    if (!updating_) setBoxScope(i == 1);
  });
  // With the mouse in the preview (AnimPreview).
  preview_->pressed = [this] {
    if (!playing_ || !loaded_ || sheet_->selected < 0) return;
    playing_ = false;
    preview_->frame = sheet_->selected;
  };
  preview_->boxSelected = [this](int i) {
    boxes_->selected = i;
    fillBoxes();
  };
  preview_->boxChanged = [this](int i, sprite::Box box, bool done) {
    sprite::NamedBox *b = listedBox(i);
    if (b == nullptr) return;
    b->box = box;
    if (done) boxEdited();
    else
    {
      // Only the numbers while dragging; cooked and saved when it is let go.
      updating_ = true;
      box_[0]->setValue(box.x, false);
      box_[1]->setValue(box.y, false);
      box_[2]->setValue(box.w, false);
      box_[3]->setValue(box.h, false);
      updating_ = false;
    }
  };
  preview_->boxDrawn = [this](sprite::Box box) {
    std::vector<sprite::NamedBox> *list = boxList();
    if (!list) return;
    sprite::NamedBox b;
    b.kind = sprite::BoxKind(std::max(0, std::min(2, boxKind_->selected())));
    b.name = b.kind == sprite::BoxKind::Hit ? "blade" : b.kind == sprite::BoxKind::Guard ? "guard" : "";
    b.box = box;
    if (!boxSprite_->pressed) warnOverride(b.kind);
    list->push_back(b);
    boxes_->selected = int(list->size()) - 1;
    boxEdited();
  };
  boxKind_ = root.findAs<OptionButton>("anim_box_kind");
  boxName_ = root.findAs<LineEdit>("anim_box_name");
  static const char *boxIds[4] = {"anim_bx", "anim_by", "anim_bw", "anim_bh"};
  for (int i = 0; i < 4; i++) box_[i] = root.findAs<SpinBox>(boxIds[i]);
  boxes_->tileSize = 0;
  anchor_[0] = root.findAs<SpinBox>("anim_ax");
  anchor_[1] = root.findAs<SpinBox>("anim_ay");
  anims_->tileSize = steps_->tileSize = 0;

  auto number = [this](SpinBox *s, double lo, double hi, std::function<void(int)> set) {
    s->minValue = lo;
    s->maxValue = hi;
    s->decimals = 0;
    s->valueChanged.connect([this, set](double v) {
      if (updating_ || !loaded_) return;
      set(int(v));
      rebuild();
      edited();
    });
  };
  for (SpinBox *s : {gridW_, gridH_})
  {
    s->minValue = 8;
    s->maxValue = 128;
    s->decimals = 0;
    s->step = 8;
  }
  gridW_->setValue(16, false);
  gridH_->setValue(16, false);
  number(rect_[0], 0, 4096, [this](int v) { if (auto *f = currentFrame()) f->x = v; });
  number(rect_[1], 0, 4096, [this](int v) { if (auto *f = currentFrame()) f->y = v; });
  number(rect_[2], 1, 128, [this](int v) { if (auto *f = currentFrame()) f->w = v; });
  number(rect_[3], 1, 128, [this](int v) { if (auto *f = currentFrame()) f->h = v; });
  number(anchor_[0], -128, 255, [this](int v) { if (auto *f = currentFrame()) f->ax = v; });
  number(anchor_[1], -128, 255, [this](int v) { if (auto *f = currentFrame()) f->ay = v; });
  // The hitbox fields edit the frame's own hitbox if it has one, else the sprite's.
  auto box = [this]() -> sprite::Box * {
    sprite::Frame *f = currentFrame();
    return f && f->ownHitbox ? &f->hitbox : &sprite_.hitbox;
  };
  number(hitbox_[0], -128, 127, [box](int v) { box()->x = v; });
  number(hitbox_[1], -128, 127, [box](int v) { box()->y = v; });
  number(hitbox_[2], 0, 255, [box](int v) { box()->w = v; });
  number(hitbox_[3], 0, 255, [box](int v) { box()->h = v; });
  // Boxes (hitboxes.md §2): of the frame shown, or the sprite's.
  auto selectedBox = [this]() -> sprite::NamedBox * { return listedBox(boxes_->selected); };
  for (int i = 0; i < 4; i++)
  {
    box_[i]->minValue = i < 2 ? -128 : 0;
    box_[i]->maxValue = i < 2 ? 127 : 255;
    box_[i]->decimals = 0;
    box_[i]->valueChanged.connect([this, i, selectedBox](double v) {
      sprite::NamedBox *b = selectedBox();
      if (updating_ || !loaded_ || !b) return;
      int *field[4] = {&b->box.x, &b->box.y, &b->box.w, &b->box.h};
      *field[i] = int(v);
      boxEdited();
    });
  }
  boxSprite_->toggled.connect([this](bool) {
    boxes_->selected = -1;
    fillBoxes();
  });
  boxes_->selectedChanged.connect([this](int) { fillBoxes(); });
  boxKind_->itemSelected.connect([this, selectedBox](int k) {
    sprite::NamedBox *b = selectedBox();
    if (updating_ || !b || k < 0 || k > 2) return;
    b->kind = sprite::BoxKind(k);
    if (b->kind != sprite::BoxKind::Hurt && b->name.empty()) b->name = b->kind == sprite::BoxKind::Hit ? "blade" : "guard";
    boxEdited();
  });
  boxName_->submitted.connect([this, selectedBox](const std::string &name) {
    sprite::NamedBox *b = selectedBox();
    if (updating_ || !b || b->name == name) return;
    b->name = name;
    boxEdited();
  });
  root.findAs<Button>("anim_box_add")->clicked.connect([this] {
    std::vector<sprite::NamedBox> *list = boxList();
    if (!list) return;
    sprite::NamedBox b;
    b.kind = sprite::BoxKind(std::max(0, std::min(2, boxKind_->selected())));
    b.name = b.kind == sprite::BoxKind::Hit ? "blade" : b.kind == sprite::BoxKind::Guard ? "guard" : "";
    b.box = sprite_.hitbox.w > 0 ? sprite_.hitbox : sprite::Box{-8, -16, 16, 16};
    if (b.kind == sprite::BoxKind::Hit) b.box = {6, -18, 16, 6};   // in front of the figure
    if (!boxSprite_->pressed) warnOverride(b.kind);
    list->push_back(b);
    // Selected (in the frame's list its own boxes come first).
    boxes_->selected = int(list->size()) - 1;
    boxEdited();
  });
  root.findAs<Button>("anim_box_delete")->clicked.connect([this] {
    if (listedBox(boxes_->selected) == nullptr) return;
    ListedBox e = listed_[size_t(boxes_->selected)];
    std::vector<sprite::NamedBox> &list = e.sprites ? sprite_.boxes : currentFrame()->boxes;
    list.erase(list.begin() + long(e.index));
    boxes_->selected = std::min(boxes_->selected, int(listed_.size()) - 2);
    boxEdited();
  });
  root.findAs<CheckBox>("anim_show_boxes")->toggled.connect([this](bool on) { preview_->showBoxes = on; });

  number(duration_, 1, 600, [this](int v) {
    int a = currentAnim();
    if (a >= 0 && steps_->selected >= 0 && size_t(steps_->selected) < sprite_.animations[size_t(a)].steps.size())
      sprite_.animations[size_t(a)].steps[size_t(steps_->selected)].duration = v;
  });

  ownHitbox_->toggled.connect([this](bool on) {
    sprite::Frame *f = currentFrame();
    if (updating_ || !f) return;
    f->ownHitbox = on;
    if (on) f->hitbox = sprite_.hitbox;
    rebuild();
    edited();
  });
  loop_->toggled.connect([this](bool on) {
    int a = currentAnim();
    if (updating_ || a < 0) return;
    sprite_.animations[size_t(a)].loop = on;
    rebuild();
    edited();
  });
  sheet_->picked = [this](int) {
    playing_ = false;
    steps_->selected = -1;
    showFrame();
  };
  chooser_->itemSelected.connect([this](int i) {
    if (i >= 0 && size_t(i) < paths_.size()) open(paths_[size_t(i)]);
  });
  anims_->selectedChanged.connect([this](int) {
    playing_ = true;
    playStep_ = playTimer_ = 0;
    steps_->selected = -1;
    fillLists();
  });
  steps_->selectedChanged.connect([this](int) {
    playing_ = false;
    showStep();
  });
  animName_->submitted.connect([this](const std::string &name) {
    int a = currentAnim();
    if (updating_ || a < 0 || name.empty() || sprite_.animations[size_t(a)].name == name) return;
    sprite_.animations[size_t(a)].name = name;
    rebuild();
    edited();
  });
  play_->clicked.connect([this] {
    playing_ = !playing_;
    if (playing_) steps_->selected = -1;
  });
  root.findAs<CheckBox>("anim_show_hitbox")->toggled.connect([this](bool on) { preview_->showHitbox = on; });
  root.findAs<CheckBox>("anim_show_pieces")->toggled.connect([this](bool on) { preview_->showPieces = on; });

  root.findAs<Button>("anim_slice")->clicked.connect([this] { slice(); });
  root.findAs<Button>("anim_add")->clicked.connect([this] {
    if (!loaded_) return;
    sprite::Animation a;
    a.name = "anim" + std::to_string(sprite_.animations.size() + 1);
    a.steps.push_back({std::max(0, sheet_->selected), 8});
    sprite_.animations.push_back(a);
    anims_->selected = int(sprite_.animations.size()) - 1;
    rebuild();
    edited();
  });
  root.findAs<Button>("anim_delete")->clicked.connect([this] {
    int a = currentAnim();
    if (a < 0) return;
    sprite_.animations.erase(sprite_.animations.begin() + a);
    anims_->selected = std::min(a, int(sprite_.animations.size()) - 1);
    rebuild();
    edited();
  });
  root.findAs<Button>("anim_step_add")->clicked.connect([this] {
    int a = currentAnim();
    if (a < 0 || sheet_->selected < 0) return;
    auto &steps = sprite_.animations[size_t(a)].steps;
    int at = steps_->selected >= 0 ? steps_->selected + 1 : int(steps.size());
    steps.insert(steps.begin() + at, {sheet_->selected, int(duration_->value()) > 0 ? int(duration_->value()) : 8});
    steps_->selected = at;
    rebuild();
    edited();
  });
  root.findAs<Button>("anim_step_remove")->clicked.connect([this] {
    int a = currentAnim();
    if (a < 0 || steps_->selected < 0) return;
    auto &steps = sprite_.animations[size_t(a)].steps;
    if (size_t(steps_->selected) >= steps.size()) return;
    steps.erase(steps.begin() + steps_->selected);
    steps_->selected = std::min(steps_->selected, int(steps.size()) - 1);
    rebuild();
    edited();
  });
  auto move = [this](int by) {
    int a = currentAnim(), i = steps_->selected;
    if (a < 0 || i < 0) return;
    auto &steps = sprite_.animations[size_t(a)].steps;
    if (i + by < 0 || size_t(i + by) >= steps.size()) return;
    std::swap(steps[size_t(i)], steps[size_t(i + by)]);
    steps_->selected = i + by;
    rebuild();
    edited();
  };
  root.findAs<Button>("anim_step_up")->clicked.connect([move] { move(-1); });
  root.findAs<Button>("anim_step_down")->clicked.connect([move] { move(1); });

  // Points (layered-sprites.md §4): the frame's own, edited with the numbers or dragged in
  // the preview.
  points_ = root.findAs<ItemList>("anim_points");
  pointName_ = root.findAs<LineEdit>("anim_point_name");
  pointOrder_ = root.findAs<OptionButton>("anim_point_order");
  static const char *pointIds[3] = {"anim_px", "anim_py", "anim_pa"};
  for (int i = 0; i < 3; i++)
  {
    point_[i] = root.findAs<SpinBox>(pointIds[i]);
    point_[i]->minValue = i < 2 ? -128 : 0;
    point_[i]->maxValue = i < 2 ? 127 : 255;
    point_[i]->decimals = 0;
    point_[i]->valueChanged.connect([this, i](double v) {
      sprite::Point *p = selectedPoint();
      if (updating_ || !p) return;
      (i == 0 ? p->x : i == 1 ? p->y : p->angle) = int(v);
      pointEdited();
    });
  }
  points_->tileSize = 0;
  points_->selectedChanged.connect([this](int) { fillPoints(); });
  pointName_->submitted.connect([this](const std::string &name) {
    sprite::Point *p = selectedPoint();
    if (updating_ || !p || name.empty() || p->name == name) return;
    p->name = name;
    pointEdited();
  });
  pointOrder_->itemSelected.connect([this](int k) {
    sprite::Point *p = selectedPoint();
    if (updating_ || !p) return;
    p->behind = k == 1;
    pointEdited();
  });
  root.findAs<Button>("anim_point_add")->clicked.connect([this] {
    sprite::Frame *f = currentFrame();
    if (!f) return;
    sprite::Point p;
    p.name = "point" + std::to_string(f->points.size() + 1);
    p.y = -f->h / 2;
    f->points.push_back(p);
    points_->selected = int(f->points.size()) - 1;
    pointEdited();
  });
  root.findAs<Button>("anim_point_delete")->clicked.connect([this] {
    sprite::Frame *f = currentFrame();
    if (!f || !selectedPoint()) return;
    f->points.erase(f->points.begin() + points_->selected);
    points_->selected = std::min(points_->selected, int(f->points.size()) - 1);
    pointEdited();
  });
  root.findAs<Button>("anim_point_copy")->clicked.connect([this] {
    // To every frame of the animation shown (or every frame), replacing one of the same name.
    const sprite::Point *p = selectedPoint();
    if (!p) return;
    sprite::Point copy = *p;
    int a = currentAnim(), n = 0;
    for (size_t k = 0; k < sprite_.frames.size(); k++)
    {
      if (int(k) == sheet_->selected) continue;
      if (a >= 0 && std::none_of(sprite_.animations[size_t(a)].steps.begin(), sprite_.animations[size_t(a)].steps.end(),
                                 [&](const sprite::Step &s) { return s.frame == int(k); }))
        continue;
      auto &pts = sprite_.frames[k].points;
      pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const sprite::Point &q) { return q.name == copy.name; }), pts.end());
      pts.push_back(copy);
      n++;
    }
    if (log) log("point " + copy.name + " copied to " + std::to_string(n) + " frames", false);
    pointEdited();
  });
  preview_->pointSelected = [this](int i) {
    points_->selected = i;
    fillPoints();
  };
  preview_->pointMoved = [this](int i, int x, int y, bool done) {
    sprite::Frame *f = currentFrame();
    if (!f || i < 0 || size_t(i) >= f->points.size()) return;
    f->points[size_t(i)].x = x;
    f->points[size_t(i)].y = y;
    if (done) pointEdited();
    else
    {
      updating_ = true;
      point_[0]->setValue(x, false);
      point_[1]->setValue(y, false);
      updating_ = false;
    }
  };

  // Direction sets (layered-sprites.md §6) and the dial.
  directions_ = root.findAs<CheckBox>("anim_directions");
  dirFrom_ = root.findAs<SpinBox>("anim_dir_from");
  dirTo_ = root.findAs<SpinBox>("anim_dir_to");
  dial_ = root.findAs<Slider>("anim_dial");
  dialLabel_ = root.findAs<Label>("anim_dial_label");
  layers_ = root.findAs<ItemList>("anim_layers");
  layers_->tileSize = 0;
  for (SpinBox *s : {dirFrom_, dirTo_})
  {
    s->minValue = 0;
    s->maxValue = 255;
    s->decimals = 0;
  }
  dial_->minValue = 0;
  dial_->maxValue = 255;
  dial_->step = 1;
  directions_->toggled.connect([this](bool on) {
    int a = currentAnim();
    if (updating_ || a < 0) return;
    sprite::Animation &an = sprite_.animations[size_t(a)];
    an.directions = on ? int(an.steps.size()) : 0;
    rebuild();
    edited();
  });
  dirFrom_->valueChanged.connect([this](double v) {
    int a = currentAnim();
    if (updating_ || a < 0) return;
    sprite_.animations[size_t(a)].from = int(v);
    rebuild();
    edited();
  });
  dirTo_->valueChanged.connect([this](double v) {
    int a = currentAnim();
    if (updating_ || a < 0) return;
    sprite_.animations[size_t(a)].to = int(v);
    rebuild();
    edited();
  });
  dial_->valueChanged.connect([this](float v) {
    dialAngle_ = int(std::lround(v));
    dialUsed_ = true;
    fillDirections();
    if (figureMode_) showFigure();
  });
}

std::vector<AnimPreview::FrameImage> AnimationPanel::frameImages(const sprite::Cooked &cooked)
{
  // A picture per cooked frame: its pieces, decoded with the cooked palette.
  std::vector<AnimPreview::FrameImage> out;
  for (const sprite::CookedFrame &f : cooked.frames)
  {
    AnimPreview::FrameImage img;
    if (!f.pieces.empty())
    {
      int x0 = 1000, y0 = 1000, x1 = -1000, y1 = -1000;
      for (const sprite::Piece &p : f.pieces)
      {
        x0 = std::min(x0, p.dx);
        y0 = std::min(y0, p.dy);
        x1 = std::max(x1, p.dx + p.w);
        y1 = std::max(y1, p.dy + p.h);
      }
      img.x = x0;
      img.y = y0;
      img.w = x1 - x0;
      img.h = y1 - y0;
      std::vector<uint8_t> rgba(size_t(img.w) * img.h * 4, 0);
      for (const sprite::Piece &p : f.pieces)
        for (int ty = 0; ty < p.h / 8; ty++)
          for (int tx = 0; tx < p.w / 8; tx++)
          {
            size_t tile = size_t(p.tile + ty * (p.w / 8) + tx);
            if (tile >= cooked.tiles.size()) continue;
            for (int k = 0; k < 64; k++)
            {
              uint8_t idx = cooked.tiles[tile][size_t(k)];
              if (!idx) continue;
              uint16_t c = cooked.palette[idx];
              int px = p.dx - x0 + tx * 8 + k % 8, py = p.dy - y0 + ty * 8 + k / 8;
              uint8_t *o = &rgba[(size_t(py) * img.w + px) * 4];
              auto x = [](unsigned v) { return uint8_t((v << 3) | (v >> 2)); };
              o[0] = x(c & 31);
              o[1] = x((c >> 5) & 31);
              o[2] = x((c >> 10) & 31);
              o[3] = 255;
            }
          }
      img.texture = renderer_->createTexture(img.w, img.h, rgba.data(), true);
    }
    out.push_back(img);
  }
  return out;
}

void AnimationPanel::freeImages(std::vector<AnimPreview::FrameImage> &images)
{
  for (AnimPreview::FrameImage &img : images)
    if (img.texture) renderer_->destroyTexture(img.texture);
  images.clear();
}

sprite::Point *AnimationPanel::selectedPoint()
{
  sprite::Frame *f = currentFrame();
  if (!f || points_ == nullptr || points_->selected < 0 || size_t(points_->selected) >= f->points.size()) return nullptr;
  return &f->points[size_t(points_->selected)];
}

void AnimationPanel::fillPoints()
{
  if (!points_) return;
  updating_ = true;
  points_->items.clear();
  sprite::Frame *f = loaded_ ? currentFrame() : nullptr;
  if (f)
    for (const sprite::Point &p : f->points)
      points_->items.push_back({p.name + "  " + std::to_string(p.x) + "," + std::to_string(p.y) + "  angle " + std::to_string(p.angle) +
                                    (p.behind ? "  behind" : ""),
                                "dot-circle-o", {}, {}, {}, {}});
  if (points_->selected >= int(points_->items.size())) points_->selected = int(points_->items.size()) - 1;
  const sprite::Point *p = selectedPoint();
  if (p)
  {
    point_[0]->setValue(p->x, false);
    point_[1]->setValue(p->y, false);
    point_[2]->setValue(p->angle, false);
    pointName_->setText(p->name);
    pointOrder_->select(p->behind ? 1 : 0, false);
  }
  preview_->points = f ? f->points : std::vector<sprite::Point>{};
  preview_->selectedPoint = points_->selected;
  updating_ = false;
}

void AnimationPanel::pointEdited()
{
  rebuild();
  edited();
  fillPoints();
}

// The direction set's fields, and with the dial: the step nearest to its angle, shown.
void AnimationPanel::fillDirections()
{
  if (!directions_) return;
  updating_ = true;
  int a = currentAnim();
  bool set = a >= 0 && sprite_.animations[size_t(a)].directions > 0;
  directions_->disabled = a < 0;
  dirFrom_->disabled = dirTo_->disabled = !set;
  if (a >= 0)
  {
    directions_->setPressed(set);
    dirFrom_->setValue(sprite_.animations[size_t(a)].from, false);
    dirTo_->setValue(sprite_.animations[size_t(a)].to, false);
  }
  preview_->dial = dialUsed_ && (set || figureMode_) ? dialAngle_ : -1;
  dialLabel_->text = "Dial: " + std::to_string(dialAngle_);
  if (set && dialUsed_ && !figureMode_)
  {
    const sprite::Animation &an = sprite_.animations[size_t(a)];
    size_t best = 0;
    int bestD = 256;
    for (size_t k = 0; k < an.steps.size(); k++)
    {
      int d = (sprite::directionAngle(an, k) - dialAngle_) & 255;
      d = std::min(d, 256 - d);
      if (d < bestD) bestD = d, best = k;
    }
    playing_ = false;
    steps_->selected = int(best);
    dialLabel_->text += " shows step " + std::to_string(best) + " (" + std::to_string(sprite::directionAngle(an, best)) + ")";
    updating_ = false;
    showStep();
    return;
  }
  updating_ = false;
}

void AnimationPanel::setSprites(const std::vector<std::string> &paths)
{
  paths_ = paths;
  std::vector<std::string> names;
  int current = -1;
  for (size_t i = 0; i < paths.size(); i++)
  {
    names.push_back(fileName(paths[i]));
    if (paths[i] == sprite_.path) current = int(i);
  }
  chooser_->setItems(names);
  if (current >= 0) chooser_->select(current, false);
  else if (!paths.empty() && !loaded_) open(paths[0]);
}

void AnimationPanel::open(const std::string &path)
{
  if (dirty_) save();
  // Leaving the figure view, if it was on.
  for (FigureLayer &l : figureLayers_) freeImages(l.images);
  figureLayers_.clear();
  preview_->composite.clear();
  if (layers_) layers_->items.clear();
  figureMode_ = false;
  if (std::filesystem::path(path).extension() == ".bmfigure")
  {
    openFigure(path);
    return;
  }
  try
  {
    sprite_ = sprite::load(path);
    loaded_ = true;
  }
  catch (const std::exception &e)
  {
    loaded_ = false;
    sprite_ = sprite::Sprite{};
    info_->text = e.what();
    if (log) log(e.what(), true);
  }
  for (size_t i = 0; i < paths_.size(); i++)
    if (paths_[i] == path) chooser_->select(int(i), false);
  sheet_->selected = loaded_ && !sprite_.frames.empty() ? 0 : -1;
  anims_->selected = loaded_ && !sprite_.animations.empty() ? 0 : -1;
  steps_->selected = -1;
  playing_ = true;
  playStep_ = playTimer_ = 0;
  if (loaded_ && !sprite_.frames.empty())
  {
    updating_ = true;
    gridW_->setValue(sprite_.frames[0].w, false);
    gridH_->setValue(sprite_.frames[0].h, false);
    updating_ = false;
  }
  rebuild();
}

int AnimationPanel::currentAnim() const
{
  return loaded_ && anims_->selected >= 0 && size_t(anims_->selected) < sprite_.animations.size() ? anims_->selected : -1;
}

sprite::Frame *AnimationPanel::currentFrame()
{
  return loaded_ && sheet_->selected >= 0 && size_t(sheet_->selected) < sprite_.frames.size() ? &sprite_.frames[size_t(sheet_->selected)] : nullptr;
}

void AnimationPanel::rebuild()
{
  freeImages(preview_->images);
  if (sheet_->texture) renderer_->destroyTexture(sheet_->texture);
  sheet_->texture = {};
  sheet_->sprite = loaded_ ? &sprite_ : nullptr;
  preview_->cooked = nullptr;
  if (!loaded_)
  {
    cost_->text.clear();
    fillLists();
    return;
  }
  if (sprite_.image) sheet_->texture = renderer_->createTexture(sprite_.image->width, sprite_.image->height, sprite_.image->rgba.data(), true);

  Report report;
  cooked_ = sprite::cook(sprite_, *profileByName("gba"), report);
  std::string problem;
  for (const ReportEntry &e : report.entries)
    if (e.level != ReportEntry::Level::Info && problem.empty()) problem = e.message;
  info_->text = problem.empty() ? sprite_.path : problem;
  info_->colorOverride = problem.empty() ? Color{0, 0, 0, 0} : errorColor(info_->theme());

  preview_->images = frameImages(cooked_);
  preview_->cooked = &cooked_;

  int colours = 0;
  for (int i = 1; i < 16; i++) colours += cooked_.palette[size_t(i)] != 0;
  cost_->text = std::to_string(sprite_.frames.size()) + " frames in " + std::to_string(cooked_.tiles.size()) + " tiles (" +
                std::to_string(cooked_.tiles.size() * 32) + " of 32768 bytes of sprite memory), " + std::to_string(colours) +
                " of 15 colours; the largest frame takes " + std::to_string(cooked_.maxPieces) + " of 128 hardware sprites and " +
                std::to_string(cooked_.maxLineCost) + " of 1210 sprite cycles on a line";
  fillLists();
}

void AnimationPanel::fillLists()
{
  if (figureMode_)
  {
    // The root's animations; the layers in the layers list.
    updating_ = true;
    int keep = anims_->selected;
    anims_->items.clear();
    steps_->items.clear();
    if (!figureLayers_.empty() && figureLayers_[0].ok)
      for (const sprite::Animation &a : figureLayers_[0].sprite.animations)
        anims_->items.push_back({a.name + (a.directions ? "  (" + std::to_string(a.directions) + " directions)" : ""), "film", {}, {}, {}, {}});
    anims_->selected = keep >= 0 && keep < int(anims_->items.size()) ? keep : (anims_->items.empty() ? -1 : 0);
    updating_ = false;
    fillDirections();
    showFigure();
    return;
  }
  updating_ = true;
  int keepAnim = anims_->selected, keepStep = steps_->selected;
  anims_->items.clear();
  steps_->items.clear();
  for (const sprite::Animation &a : sprite_.animations)
  {
    int total = 0;
    for (const sprite::Step &s : a.steps) total += s.duration;
    anims_->items.push_back({a.name + "  (" + std::to_string(a.steps.size()) + " steps, " + std::to_string(total) + " frames" + (a.loop ? ", loops" : "") + ")",
                             "film", {}, {}, {}, {}});
  }
  anims_->selected = keepAnim < int(sprite_.animations.size()) ? keepAnim : -1;
  int a = currentAnim();
  if (a >= 0)
  {
    const sprite::Animation &an = sprite_.animations[size_t(a)];
    for (size_t i = 0; i < an.steps.size(); i++)
      steps_->items.push_back({"frame " + std::to_string(an.steps[i].frame) +
                                   (an.directions > 0 ? " at angle " + std::to_string(sprite::directionAngle(an, i))
                                                      : " for " + std::to_string(an.steps[i].duration) + " frames"),
                               "image", {}, {}, {}, {}});
    steps_->selected = keepStep < int(an.steps.size()) ? keepStep : -1;
    animName_->setText(an.name);
    loop_->setPressed(an.loop);
  }
  else
  {
    animName_->setText("");
    steps_->selected = -1;
  }
  updating_ = false;
  showFrame();
  fillDirections();
}

void AnimationPanel::showFrame()
{
  updating_ = true;
  sprite::Frame *f = currentFrame();
  bool have = f != nullptr;
  for (SpinBox *s : rect_) s->disabled = !have;
  for (SpinBox *s : anchor_) s->disabled = !have;
  ownHitbox_->disabled = !have;
  if (have)
  {
    frameTitle_->text = "Frame " + std::to_string(sheet_->selected);
    rect_[0]->setValue(f->x, false);
    rect_[1]->setValue(f->y, false);
    rect_[2]->setValue(f->w, false);
    rect_[3]->setValue(f->h, false);
    anchor_[0]->setValue(f->ax, false);
    anchor_[1]->setValue(f->ay, false);
    ownHitbox_->setPressed(f->ownHitbox);
  }
  else
    frameTitle_->text = "No frame selected";
  updating_ = false;
  fillBoxes();
  fillPoints();
  updating_ = true;
  const sprite::Box &b = have && f->ownHitbox ? f->hitbox : sprite_.hitbox;
  hitbox_[0]->setValue(b.x, false);
  hitbox_[1]->setValue(b.y, false);
  hitbox_[2]->setValue(b.w, false);
  hitbox_[3]->setValue(b.h, false);
  int a = currentAnim();
  if (a >= 0 && steps_->selected >= 0 && size_t(steps_->selected) < sprite_.animations[size_t(a)].steps.size())
    duration_->setValue(sprite_.animations[size_t(a)].steps[size_t(steps_->selected)].duration, false);
  updating_ = false;
  if (!playing_ && have) preview_->frame = sheet_->selected;
}

void AnimationPanel::showStep()
{
  int a = currentAnim();
  if (a < 0 || steps_->selected < 0) return;
  const auto &steps = sprite_.animations[size_t(a)].steps;
  if (size_t(steps_->selected) >= steps.size()) return;
  sheet_->selected = steps[size_t(steps_->selected)].frame;
  showFrame();
}

void AnimationPanel::slice()
{
  if (!loaded_ || !sprite_.image) return;
  int w = int(gridW_->value()), h = int(gridH_->value());
  if (w <= 0 || h <= 0) return;
  // One frame per grid cell that has pixels; the anchor at the bottom centre.
  std::vector<sprite::Frame> frames;
  const Image &img = *sprite_.image;
  for (int y = 0; y + h <= img.height; y += h)
    for (int x = 0; x + w <= img.width; x += w)
    {
      bool any = false;
      for (int py = 0; py < h && !any; py++)
        for (int px = 0; px < w && !any; px++) any = img.pixel(x + px, y + py)[3] >= 128;
      if (!any) continue;
      sprite::Frame f;
      f.x = x;
      f.y = y;
      f.w = w;
      f.h = h;
      f.ax = w / 2;
      f.ay = h;
      frames.push_back(f);
    }
  sprite_.frames = frames;
  sheet_->selected = frames.empty() ? -1 : 0;
  if (log) log("sliced " + fileName(sprite_.imagePath) + " into " + std::to_string(frames.size()) + " frames of " + std::to_string(w) + "x" + std::to_string(h), false);
  rebuild();
  edited();
}

void AnimationPanel::edited()
{
  dirty_ = true;
  saveTimer_ = 0;
}

void AnimationPanel::save()
{
  dirty_ = false;
  if (!loaded_) return;
  std::string error;
  if (!sprite::save(sprite_, &error))
  {
    if (log) log(error, true);
    return;
  }
  if (changed) changed();
}

void AnimationPanel::update(float dt)
{
  if (dirty_ && (saveTimer_ += dt) > 0.4f) save();
  play_->text = playing_ ? "Pause" : "Play";
  if (figureMode_)
  {
    // The root's animation in time; the other layers follow it (showFigure).
    int a = anims_->selected;
    if (!playing_ || figureLayers_.empty() || !figureLayers_[0].ok || a < 0 || size_t(a) >= figureLayers_[0].sprite.animations.size()) return;
    const sprite::Animation &an = figureLayers_[0].sprite.animations[size_t(a)];
    if (an.steps.empty()) return;
    playTime_ += dt;
    bool changedStep = false;
    while (playTime_ >= kFrameSeconds)
    {
      playTime_ -= kFrameSeconds;
      if (an.directions > 0) break;
      if (size_t(playStep_) >= an.steps.size()) playStep_ = 0;
      if (++playTimer_ >= an.steps[size_t(playStep_)].duration)
      {
        playTimer_ = 0;
        playStep_ = size_t(playStep_) + 1 < an.steps.size() ? playStep_ + 1 : 0;
        changedStep = true;
      }
    }
    if (changedStep || preview_->composite.empty()) showFigure();
    return;
  }
  // The boxes can be changed with the mouse while the frame they belong to is shown.
  preview_->editable = loaded_ && !playing_ && preview_->frame == sheet_->selected;
  int a = currentAnim();
  if (!playing_ || a < 0 || sprite_.animations[size_t(a)].steps.empty()) return;
  const sprite::Animation &an = sprite_.animations[size_t(a)];
  playTime_ += dt;
  while (playTime_ >= kFrameSeconds)
  {
    playTime_ -= kFrameSeconds;
    if (size_t(playStep_) >= an.steps.size()) playStep_ = 0;
    if (++playTimer_ >= an.steps[size_t(playStep_)].duration)
    {
      playTimer_ = 0;
      if (size_t(playStep_) + 1 < an.steps.size()) playStep_++;
      else playStep_ = 0;  // the preview repeats animations that do not loop, to look at them
    }
  }
  if (size_t(playStep_) >= an.steps.size()) playStep_ = 0;
  preview_->frame = an.steps[size_t(playStep_)].frame;
}

std::vector<sprite::NamedBox> *AnimationPanel::boxList()
{
  if (!loaded_) return nullptr;
  if (boxSprite_ && boxSprite_->pressed) return &sprite_.boxes;
  sprite::Frame *f = currentFrame();
  return f ? &f->boxes : nullptr;
}

sprite::NamedBox *AnimationPanel::listedBox(int i)
{
  if (!loaded_ || i < 0 || size_t(i) >= listed_.size()) return nullptr;
  const ListedBox &e = listed_[size_t(i)];
  if (e.sprites) return e.index < sprite_.boxes.size() ? &sprite_.boxes[e.index] : nullptr;
  sprite::Frame *f = currentFrame();
  return f && e.index < f->boxes.size() ? &f->boxes[e.index] : nullptr;
}

void AnimationPanel::fillBoxes()
{
  if (!boxes_) return;
  updating_ = true;
  boxes_->items.clear();
  listed_.clear();
  // The sprite's boxes; or the frame's: its own, then the sprite's of the kinds it has none of
  // (they are what the frame has, as the game sees it; editing one changes the sprite's).
  sprite::Frame *f = loaded_ ? currentFrame() : nullptr;
  if (loaded_ && boxSprite_->pressed)
    for (size_t i = 0; i < sprite_.boxes.size(); i++) listed_.push_back({true, i});
  else if (f)
  {
    for (size_t i = 0; i < f->boxes.size(); i++) listed_.push_back({false, i});
    for (size_t i = 0; i < sprite_.boxes.size(); i++)
    {
      sprite::BoxKind kind = sprite_.boxes[i].kind;
      bool own = std::any_of(f->boxes.begin(), f->boxes.end(), [&](const sprite::NamedBox &b) { return b.kind == kind; });
      if (!own) listed_.push_back({true, i});
    }
  }
  for (const ListedBox &e : listed_)
  {
    const sprite::NamedBox &b = e.sprites ? sprite_.boxes[e.index] : f->boxes[e.index];
    boxes_->items.push_back({std::string(sprite::boxKindName(b.kind)) + (b.name.empty() ? "" : " " + b.name) + "  " + std::to_string(b.box.x) + "," +
                                 std::to_string(b.box.y) + "  " + std::to_string(b.box.w) + "x" + std::to_string(b.box.h) +
                                 (e.sprites && !boxSprite_->pressed ? "  (the sprite's)" : ""),
                             b.kind == sprite::BoxKind::Hit ? "crosshairs" : b.kind == sprite::BoxKind::Guard ? "lock" : "square-o", {}, {}, {},
                             {}});
  }
  if (boxes_->selected >= int(boxes_->items.size())) boxes_->selected = int(boxes_->items.size()) - 1;
  const sprite::NamedBox *b = listedBox(boxes_->selected);
  if (b)
  {
    box_[0]->setValue(b->box.x, false);
    box_[1]->setValue(b->box.y, false);
    box_[2]->setValue(b->box.w, false);
    box_[3]->setValue(b->box.h, false);
    boxKind_->select(int(b->kind), false);
    boxName_->setText(b->name);
  }
  // The preview shows these boxes (to change them with the mouse) and marks the one selected.
  preview_->editBoxes.clear();
  for (const ListedBox &e : listed_)
  {
    const sprite::NamedBox &nb = e.sprites ? sprite_.boxes[e.index] : f->boxes[e.index];
    preview_->editBoxes.push_back({nb.box, nb.kind});
  }
  preview_->selectedBox = boxes_->selected;
  // Its scope: this frame's own, or the sprite's (for every frame without its own of the kind).
  boxScope_->disabled = b == nullptr || f == nullptr;
  if (b) boxScope_->select(listed_[size_t(boxes_->selected)].sprites ? 1 : 0, false);
  updating_ = false;
}

void AnimationPanel::warnOverride(sprite::BoxKind kind)
{
  sprite::Frame *f = currentFrame();
  if (f == nullptr || !log) return;
  if (std::any_of(f->boxes.begin(), f->boxes.end(), [&](const sprite::NamedBox &b) { return b.kind == kind; })) return;
  std::string hidden;
  for (const sprite::NamedBox &b : sprite_.boxes)
    if (b.kind == kind) hidden += (hidden.empty() ? "" : ", ") + (b.name.empty() ? std::string(sprite::boxKindName(kind)) : b.name);
  if (!hidden.empty())
    log("frame " + std::to_string(sheet_->selected) + " has its own " + sprite::boxKindName(kind) + " boxes now: the sprite's (" + hidden +
            ") no longer apply to it",
        false);
}

void AnimationPanel::setBoxScope(bool sprites)
{
  sprite::Frame *f = currentFrame();
  int i = boxes_->selected;
  if (f == nullptr || listedBox(i) == nullptr || listed_[size_t(i)].sprites == sprites) return;
  ListedBox e = listed_[size_t(i)];
  if (!sprites) warnOverride(sprite_.boxes[e.index].kind);
  std::vector<sprite::NamedBox> &from = e.sprites ? sprite_.boxes : f->boxes;
  std::vector<sprite::NamedBox> &to = sprites ? sprite_.boxes : f->boxes;
  sprite::NamedBox moved = from[e.index];
  from.erase(from.begin() + long(e.index));
  to.push_back(moved);
  // Selected again where it is now (in the sprite's list, or the frame's own at its end).
  boxSprite_->setPressed(boxSprite_->pressed && sprites);
  fillBoxes();
  for (size_t k = 0; k < listed_.size(); k++)
    if (listed_[k].sprites == sprites && listed_[k].index == to.size() - 1) boxes_->selected = int(k);
  if (log)
    log(std::string("box ") + sprite::boxKindName(moved.kind) + (moved.name.empty() ? "" : " " + moved.name) +
            (sprites ? ": now the sprite's, for every frame without its own " + std::string(sprite::boxKindName(moved.kind)) + " boxes"
                     : ": now frame " + std::to_string(sheet_->selected) + "'s own (other frames do not have it any more)"),
        false);
  boxEdited();
}

void AnimationPanel::boxEdited()
{
  rebuild();
  edited();
  fillBoxes();
}

}  // namespace bm::editor

namespace bm::editor {

void AnimationPanel::openFigure(const std::string &path)
{
  figureMode_ = true;
  loaded_ = false;   // the sprite fields have nothing to edit
  sprite_ = sprite::Sprite{};
  sprite_.path = path;
  for (size_t i = 0; i < paths_.size(); i++)
    if (paths_[i] == path) chooser_->select(int(i), false);
  try
  {
    figure_ = figure::load(path);
  }
  catch (const std::exception &e)
  {
    info_->text = e.what();
    if (log) log(e.what(), true);
    figure_ = figure::Figure{};
  }
  // Each layer's sprite: the project's sprite of that name, cooked.
  for (const figure::Layer &l : figure_.layers)
  {
    FigureLayer fl;
    for (const std::string &p : paths_)
      if (!l.sprite.empty() && std::filesystem::path(p).extension() == ".bmsprite" && std::filesystem::path(p).stem().string() == l.sprite)
      {
        try
        {
          fl.sprite = sprite::load(p);
          Report report;
          fl.cooked = sprite::cook(fl.sprite, *profileByName("gba"), report);
          fl.images = frameImages(fl.cooked);
          fl.ok = true;
        }
        catch (const std::exception &e)
        {
          if (log) log(p + ": " + e.what(), true);
        }
      }
    figureLayers_.push_back(std::move(fl));
  }
  layers_->items.clear();
  for (size_t i = 0; i < figure_.layers.size(); i++)
  {
    const figure::Layer &l = figure_.layers[i];
    std::string mode = l.mode == figure::Mode::Sync ? "sync" : l.mode == figure::Mode::Aim ? "aim" : "own";
    layers_->items.push_back({l.name + ": " + (l.sprite.empty() ? "no sprite" : l.sprite) + (l.parent.empty() ? "  (root)" : "  at " + l.parent + "." + l.point) +
                                  ", " + mode + (l.behind ? ", behind" : "") + (l.palette.empty() ? "" : ", palette " + l.palette) +
                                  (figureLayers_[i].ok || l.sprite.empty() ? "" : "  (no such sprite)"),
                              "clone", {}, {}, {}, {}});
  }
  info_->text = path + ": a figure of " + std::to_string(figure_.layers.size()) + " layers";
  info_->colorOverride = Color{0, 0, 0, 0};
  sheet_->sprite = nullptr;
  sheet_->selected = -1;
  if (sheet_->texture) renderer_->destroyTexture(sheet_->texture);
  sheet_->texture = {};
  preview_->cooked = nullptr;
  freeImages(preview_->images);
  showFrame();   // nothing: the fields are for a sprite
  anims_->selected = 0;
  playing_ = true;
  playStep_ = playTimer_ = 0;
  fillLists();
  showFigure();
}

// The figure's pose for the root's animation at its step (as the runtime does, figure.h): a
// layer plays the animation of its sprite named like its parent's at the same step; one whose
// animation is a direction set (or an `aim` layer: its first) shows the step nearest to the dial.
void AnimationPanel::showFigure()
{
  preview_->composite.clear();
  size_t n = figure_.layers.size();
  if (n == 0 || figureLayers_.empty() || !figureLayers_[0].ok) return;
  int rootAnim = anims_->selected;
  const auto &rootAnims = figureLayers_[0].sprite.animations;
  if (rootAnim < 0 || size_t(rootAnim) >= rootAnims.size()) return;
  std::vector<int> anim(n, -1), frame(n, -1), ox(n, 0), oy(n, 0);
  std::vector<bool> behind(n, false);
  int pieces = 0, tiles = 0, banks = 0, worst = 0;
  for (size_t i = 0; i < n; i++)
  {
    const figure::Layer &l = figure_.layers[i];
    const FigureLayer &fl = figureLayers_[i];
    int parent = l.parent.empty() ? -1 : figure::layerIndex(figure_, l.parent);
    if (parent >= 0)
    {
      ox[i] = ox[size_t(parent)];
      oy[i] = oy[size_t(parent)];
      int pf = frame[size_t(parent)];
      if (pf >= 0 && figureLayers_[size_t(parent)].ok)
        for (const sprite::Point &p : figureLayers_[size_t(parent)].cooked.frames[size_t(pf)].points)
          if (p.name == l.point)
          {
            ox[i] += p.x;
            oy[i] += p.y;
            behind[i] = p.behind;
          }
    }
    behind[i] = behind[i] || l.behind;
    if (!fl.ok) continue;
    tiles += int(fl.cooked.tiles.size());
    banks++;
    const std::string &want = rootAnims[size_t(rootAnim)].name;
    for (size_t a = 0; a < fl.sprite.animations.size(); a++)
      if (fl.sprite.animations[a].name == want) anim[i] = int(a);
    if (l.mode == figure::Mode::Aim)
      for (size_t a = 0; a < fl.sprite.animations.size() && anim[i] < 0; a++)
        if (fl.sprite.animations[a].directions > 0) anim[i] = int(a);
    if (anim[i] < 0) continue;
    const sprite::Animation &an = fl.sprite.animations[size_t(anim[i])];
    if (an.steps.empty()) continue;
    size_t step = std::min(size_t(playStep_), an.steps.size() - 1);
    if (an.directions > 0)
    {
      int bestD = 256;
      for (size_t k = 0; k < an.steps.size(); k++)
      {
        int d = (sprite::directionAngle(an, k) - dialAngle_) & 255;
        d = std::min(d, 256 - d);
        if (d < bestD) bestD = d, step = k;
      }
    }
    frame[i] = an.steps[step].frame;
    if (frame[i] < 0 || size_t(frame[i]) >= fl.cooked.frames.size()) frame[i] = -1;
  }
  // Back to front: each layer in front of all before it, or right behind its parent.
  std::vector<size_t> order;
  for (size_t i = 0; i < n; i++)
  {
    size_t at = order.size();
    int parent = figure_.layers[i].parent.empty() ? -1 : figure::layerIndex(figure_, figure_.layers[i].parent);
    if (i != 0 && behind[i])
      for (size_t k = 0; k < order.size(); k++)
        if (int(order[k]) == parent) at = k;
    order.insert(order.begin() + long(at), i);
  }
  std::vector<int> lineCost(256, 0);
  for (size_t i : order)
  {
    if (frame[i] < 0) continue;
    const FigureLayer &fl = figureLayers_[i];
    const AnimPreview::FrameImage &img = fl.images[size_t(frame[i])];
    AnimPreview::Layer layer;
    layer.texture = img.texture;
    layer.x = img.x + ox[i];
    layer.y = img.y + oy[i];
    layer.w = img.w;
    layer.h = img.h;
    for (sprite::Piece p : fl.cooked.frames[size_t(frame[i])].pieces)
    {
      p.dx += ox[i];
      p.dy += oy[i];
      layer.pieces.push_back(p);
      pieces++;
      for (int y = std::max(-128, p.dy); y < p.dy + p.h && y < 128; y++) lineCost[size_t(y + 128)] += p.w;
    }
    for (sprite::Point p : fl.cooked.frames[size_t(frame[i])].points)
    {
      p.x += ox[i];
      p.y += oy[i];
      layer.points.push_back(p);
    }
    preview_->composite.push_back(layer);
  }
  for (int c : lineCost) worst = std::max(worst, c);
  preview_->dial = dialUsed_ ? dialAngle_ : -1;
  cost_->text = "This pose: " + std::to_string(pieces) + " hardware sprites (12 for a figure), " + std::to_string(worst) +
                " of 1210 sprite cycles on its worst line. The figure: " + std::to_string(tiles) + " tiles of sprite memory, " +
                std::to_string(banks) + " palette banks";
}

}  // namespace bm::editor
