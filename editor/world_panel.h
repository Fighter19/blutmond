// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The World tab (docs/spec/world-format.md): the project's rooms on the grid of the world map,
// as the cooker lays them out. Rooms the file places are drawn solid, rooms placed by their
// doors lighter; each cell shows its walls and doors as the game's map does, and lines join
// the doors between rooms (red where the map does not agree with a door). Drag a room to
// place it; the list on the right hides a room from the map or gives it back to its doors.
// Every change is written to the .bmworld file at once and, with Live, reaches the running
// game.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ui/control.h"
#include "ui/core.h"
#include "world_asset.h"

namespace ose::ui {
class CheckBox;
class Context;
class ItemList;
class Label;
class SpinBox;
}  // namespace ose::ui

namespace bm::editor {

/// The map: rooms on a grid, dragged to their places.
class WorldView : public ose::ui::Control
{
public:
  const char *typeName() const override { return "WorldView"; }
  ose::Vec2 computeMinSize() override { return {200, 160}; }
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;
  ose::ui::Cursor cursor(ose::Vec2 p) const override;

  const world::Layout *layout = nullptr;
  std::map<std::string, std::string> areaOf;   // room id -> area (colour)
  std::string selected;
  float cell = 28;           // pixels of a map cell
  ose::Vec2 origin{40, 40};  // where map cell (0, 0) is, from the view's top left
  bool fitPending = true;    // the next draw fits the whole map into the view
  std::function<void(const std::string &)> picked;
  /// A room was dragged to (x, y) (cells).
  std::function<void(const std::string &, int, int)> moved;

private:
  /// The room under a point, or "".
  std::string roomAt(ose::Vec2 p) const;
  ose::Vec2 toScreen(float x, float y) const { return {rect_.x + origin.x + x * cell, rect_.y + origin.y + y * cell}; }
  std::string drag_;
  bool panning_ = false, dragged_ = false;
  ose::Vec2 dragFrom_, originFrom_;
  int dragX_ = 0, dragY_ = 0, startX_ = 0, startY_ = 0;
};

void registerWorldControls();

class WorldPanel
{
public:
  /// The world file was written: cook again and push to the running game.
  std::function<void()> changed;
  /// A world file was made (the project's asset list changes).
  std::function<void()> created;
  std::function<void(const std::string &line, bool error)> log;

  void bind(ose::ui::Control &root, ose::ui::Context &ui);
  /// The project's rooms (Tiled files, in the project's order) and its world file ("": none).
  void setRooms(const std::vector<std::string> &rooms);
  void setWorld(const std::string &path);
  /// Where a new world file goes (the first asset folder).
  std::string newWorldDir;
  /// Reads the rooms again when their files changed.
  void update(float dt);

  // Actions.
  void newWorld();
  void placeByDoors();
  void putOnMap();
  void fixAll();

private:
  void loadRooms();
  void relayout();
  void fillList();
  void showSelected();
  void save();
  void select(const std::string &id);
  world::RoomEntry &entry(const std::string &id);

  ose::ui::Context *ui_ = nullptr;
  WorldView *view_ = nullptr;
  ose::ui::ItemList *list_ = nullptr, *warnings_ = nullptr;
  ose::ui::Label *status_ = nullptr, *roomTitle_ = nullptr;
  ose::ui::CheckBox *hidden_ = nullptr, *minimap_ = nullptr;
  ose::ui::SpinBox *cellW_ = nullptr, *cellH_ = nullptr;

  std::vector<std::string> roomPaths_;
  std::map<std::string, std::filesystem::file_time_type> roomTimes_;
  std::vector<world::RoomInfo> infos_;
  std::map<std::string, std::string> pathOf_;   // room id -> file
  std::string path_;
  world::World world_;
  world::Layout layout_;
  std::string selected_;
  float timer_ = 0;
  bool filling_ = false;
};

}  // namespace bm::editor
