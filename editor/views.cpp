// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "views.h"

#include <algorithm>
#include <cmath>

#include "ui/canvas.h"
#include "ui/theme.h"

using namespace ose::ui;
using ose::Vec2;

namespace bm::editor {

namespace {
Color fromBgr555(uint16_t c)
{
  return {float(c & 31) / 31.0f, float((c >> 5) & 31) / 31.0f, float((c >> 10) & 31) / 31.0f, 1};
}
}  // namespace

void RoomOverview::draw(Canvas &c)
{
  c.rect(rect_, parseColor("#404040"));
  if (!project || project->roomImage().width == 0)
  {
    c.textIn(rect_, project && !project->error().empty() ? project->error() : "No picture for this profile",
             font(), fontSize(), parseColor("#ffffff"), Align::Center);
    return;
  }
  c.pushClip(rect_);
  const Rgba &img = project->roomImage();
  float ox = rect_.x + pan_.x, oy = rect_.y + pan_.y;
  c.image({ox, oy, img.width * zoom_, img.height * zoom_}, texture);

  float cell = float(project->cellSize()) * zoom_;
  if (showCollision)
  {
    const auto &cells = project->collision();
    for (int y = 0; y < project->collisionHeight(); y++)
      for (int x = 0; x < project->collisionWidth(); x++)
      {
        const CollisionCell &k = cells[size_t(y) * project->collisionWidth() + x];
        Rect r{ox + x * cell, oy + y * cell, cell, cell};
        if (k.material == 1) c.rect(r, {0.2f, 0.4f, 1, 0.35f});
        else if (k.material == 2 || k.material == 3) c.rect(r, {0.9f, 0.1f, 0.9f, 0.4f});
        else if (k.material == 6) c.rect(r, {0.2f, 0.9f, 0.3f, 0.35f});
        if (k.shape == 0) continue;
        if (k.solidTop && !k.solidSides) c.rect({r.x, r.y, r.w, std::max(2.0f, zoom_ * 2)}, {1, 0.85f, 0.1f, 0.85f});
        else if (k.shape == 1) c.border(r, {1, 0.2f, 0.2f, 0.8f}, 1);
        else c.border(r, {1, 0.6f, 0.1f, 0.9f}, 1);
      }
  }
  if (showGrid)
  {
    for (int x = 0; x <= project->collisionWidth(); x++)
      c.rect({ox + x * cell, oy, 1, project->collisionHeight() * cell}, {0, 0, 0, 0.25f});
    for (int y = 0; y <= project->collisionHeight(); y++)
      c.rect({ox, oy + y * cell, project->collisionWidth() * cell, 1}, {0, 0, 0, 0.25f});
  }
  if (showObjects)
  {
    for (const ObjectMarker &o : project->objects())
    {
      Color col{0.3f, 1, 1, 1};
      if (o.w > 0 && o.h > 0)
        c.border({ox + o.x * zoom_, oy + o.y * zoom_, o.w * zoom_, o.h * zoom_}, withAlpha(col, 0.7f), 1);
      else
        c.rect({ox + o.x * zoom_ - 3, oy + o.y * zoom_ - 3, 6, 6}, col);
      c.text({ox + o.x * zoom_ + 4, oy + o.y * zoom_ - 16}, o.type, font(), fontSize() - 1, col);
    }
  }
  // GBA screen
  c.border({ox + cameraX * zoom_, oy + cameraY * zoom_, 240 * zoom_, 160 * zoom_}, {1, 1, 1, 1}, 2);
  c.popClip();
}

void RoomOverview::placeCamera(Vec2 p)
{
  int x = int((p.x - rect_.x - pan_.x) / zoom_) - 120;
  int y = int((p.y - rect_.y - pan_.y) / zoom_) - 80;
  if (cameraMoved) cameraMoved(x, y);
}

bool RoomOverview::event(const InputEvent &e)
{
  switch (e.type)
  {
  case InputEvent::Type::Wheel: {
    float old = zoom_;
    zoom_ = std::clamp(zoom_ * (e.delta.y > 0 ? 1.25f : 0.8f), 0.25f, 8.0f);
    // Keep the point under the pointer in place.
    Vec2 local{e.pos.x - rect_.x, e.pos.y - rect_.y};
    pan_.x = local.x - (local.x - pan_.x) * zoom_ / old;
    pan_.y = local.y - (local.y - pan_.y) * zoom_ / old;
    return true;
  }
  case InputEvent::Type::MouseDown:
    if (e.button == MouseButton::Left)
    {
      placing_ = true;
      placeCamera(e.pos);
    }
    else
    {
      panning_ = true;
      last_ = e.pos;
    }
    return true;
  case InputEvent::Type::MouseMove:
    if (panning_)
    {
      pan_.x += e.pos.x - last_.x;
      pan_.y += e.pos.y - last_.y;
      last_ = e.pos;
      return true;
    }
    if (placing_)
    {
      placeCamera(e.pos);
      return true;
    }
    return false;
  case InputEvent::Type::MouseUp:
    panning_ = placing_ = false;
    return true;
  default:
    return false;
  }
}

void PaletteGrid::draw(Canvas &c)
{
  const float s = 14;
  c.rect(rect_, parseColor("#808080"));
  if (!project) return;
  const auto &pal = project->palette();
  for (int bank = 0; bank < 16; bank++)
    for (int i = 0; i < 16; i++)
    {
      Rect r{rect_.x + 1 + i * s, rect_.y + 1 + bank * s, s - 1, s - 1};
      bool used = bank < project->usedBanks();
      if (!used)
      {
        c.rect(r, parseColor("#a0a0a0"));
        continue;
      }
      if (i == 0 && bank != 0)
      {
        // transparent: hatched
        c.rect(r, parseColor("#ffffff"));
        c.line({r.x, r.bottom()}, {r.right(), r.y}, parseColor("#c0c0c0"), 1);
        continue;
      }
      c.rect(r, fromBgr555(pal[size_t(bank * 16 + i)]));
    }
}

}  // namespace bm::editor
