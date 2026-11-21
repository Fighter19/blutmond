// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "animation_panel.h"

#include <algorithm>
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

void AnimPreview::draw(Canvas &c)
{
  checker(c, rect_, 16);
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
  for (AnimPreview::FrameImage &img : preview_->images)
    if (img.texture) renderer_->destroyTexture(img.texture);
  preview_->images.clear();
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

  // A picture per cooked frame: its pieces, decoded with the cooked palette.
  for (const sprite::CookedFrame &f : cooked_.frames)
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
            if (tile >= cooked_.tiles.size()) continue;
            for (int k = 0; k < 64; k++)
            {
              uint8_t idx = cooked_.tiles[tile][size_t(k)];
              if (!idx) continue;
              uint16_t c = cooked_.palette[idx];
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
    preview_->images.push_back(img);
  }
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
      steps_->items.push_back({"frame " + std::to_string(an.steps[i].frame) + " for " + std::to_string(an.steps[i].duration) + " frames", "image", {}, {}, {}, {}});
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

}  // namespace bm::editor
