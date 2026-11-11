// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Editor views drawn with ose-ui.
#pragma once

#include <functional>

#include "project.h"
#include "ui/control.h"

namespace bm::editor {

/// The whole room, zoomable (wheel) and pannable (right or middle drag), with collision and
/// object overlays. A left click moves the GBA camera; its 240x160 frame is outlined.
class RoomOverview : public ose::ui::Control
{
public:
  const char *typeName() const override { return "RoomOverview"; }
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;

  const Project *project = nullptr;
  ose::ui::TextureHandle texture;
  bool showCollision = true, showObjects = true, showGrid = false;
  int cameraX = 0, cameraY = 0;
  std::function<void(int, int)> cameraMoved;

private:
  float zoom_ = 2;
  ose::Vec2 pan_{8, 8};
  bool panning_ = false, placing_ = false;
  ose::Vec2 last_;
  void placeCamera(ose::Vec2 p);
};

}  // namespace bm::editor
