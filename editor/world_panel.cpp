// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "world_panel.h"
#include "theme_colors.h"

#include <algorithm>
#include <cmath>

#include "tmx.h"
#include "ui/canvas.h"
#include "ui/containers.h"
#include "ui/context.h"
#include "ui/lists.h"
#include "ui/loader.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;
using namespace ose::ui;
using ose::Vec2;

namespace bm::editor {

namespace {

// A colour per area, from its name, so that rooms of an area look alike.
Color areaColour(const std::string &area)
{
  uint32_t h = 2166136261u;
  for (char ch : area) h = (h ^ uint8_t(ch)) * 16777619u;
  float hue = float(h % 360) / 60.0f;
  float x = 1 - std::fabs(std::fmod(hue, 2.0f) - 1);
  float r = 0, g = 0, b = 0;
  if (hue < 1) r = 1, g = x;
  else if (hue < 2) r = x, g = 1;
  else if (hue < 3) g = 1, b = x;
  else if (hue < 4) g = x, b = 1;
  else if (hue < 5) r = x, b = 1;
  else r = 1, b = x;
  return {0.25f + r * 0.45f, 0.25f + g * 0.45f, 0.3f + b * 0.45f, 1};
}

const Color kWall{0.92f, 0.92f, 0.95f, 1}, kDoor{1, 0.6f, 0.15f, 1}, kLink{0.3f, 0.9f, 0.4f, 0.9f}, kBadLink{1, 0.25f, 0.25f, 1};

}  // namespace

// --- WorldView ----------------------------------------------------------------------------

std::string WorldView::roomAt(Vec2 p) const
{
  if (layout == nullptr) return "";
  // The last drawn is on top.
  for (auto it = layout->rooms.rbegin(); it != layout->rooms.rend(); ++it)
  {
    Vec2 a = toScreen(float(it->x), float(it->y));
    if (p.x >= a.x && p.y >= a.y && p.x < a.x + it->w * cell && p.y < a.y + it->h * cell) return it->id;
  }
  return "";
}

Cursor WorldView::cursor(Vec2 p) const
{
  if (!drag_.empty()) return Cursor::Move;
  return roomAt(p).empty() ? Cursor::Arrow : Cursor::Hand;
}

bool WorldView::event(const InputEvent &e)
{
  switch (e.type)
  {
  case InputEvent::Type::MouseDown:
  {
    if (e.button != MouseButton::Left) return false;
    drag_ = roomAt(e.pos);
    dragFrom_ = e.pos;
    dragged_ = false;
    if (drag_.empty())
    {
      // Empty ground: the map moves.
      panning_ = true;
      originFrom_ = origin;
      return true;
    }
    const world::PlacedRoom *r = layout->find(drag_);
    startX_ = dragX_ = r->x;
    startY_ = dragY_ = r->y;
    selected = drag_;
    if (picked) picked(drag_);
    return true;
  }
  case InputEvent::Type::MouseMove:
  {
    if (panning_)
    {
      origin = {originFrom_.x + e.pos.x - dragFrom_.x, originFrom_.y + e.pos.y - dragFrom_.y};
      return true;
    }
    if (drag_.empty()) return false;
    int dx = int(std::lround((e.pos.x - dragFrom_.x) / cell)), dy = int(std::lround((e.pos.y - dragFrom_.y) / cell));
    dragged_ |= dx != 0 || dy != 0;
    dragX_ = startX_ + dx;
    dragY_ = startY_ + dy;
    return true;
  }
  case InputEvent::Type::MouseUp:
  {
    bool any = panning_ || !drag_.empty();
    if (!drag_.empty() && dragged_ && (dragX_ != startX_ || dragY_ != startY_) && moved) moved(drag_, dragX_, dragY_);
    drag_.clear();
    panning_ = false;
    return any;
  }
  case InputEvent::Type::Wheel:
  {
    // Zoom about the pointer.
    float old = cell;
    cell = std::clamp(cell * (e.delta.y > 0 ? 1.25f : 0.8f), 8.0f, 96.0f);
    Vec2 p{e.pos.x - rect_.x, e.pos.y - rect_.y};
    origin = {p.x - (p.x - origin.x) * cell / old, p.y - (p.y - origin.y) * cell / old};
    return true;
  }
  default:
    return false;
  }
}

void WorldView::draw(Canvas &c)
{
  const Theme &t = theme();
  bool dark = isDarkTheme(t);
  c.rect(rect_, dark ? Color{0.08f, 0.09f, 0.12f, 1} : Color{0.82f, 0.84f, 0.88f, 1});
  c.pushClip(rect_);
  // The grid, a line per cell.
  Color grid = dark ? Color{1, 1, 1, 0.06f} : Color{0, 0, 0, 0.08f};
  float x0 = rect_.x + std::fmod(origin.x, cell), y0 = rect_.y + std::fmod(origin.y, cell);
  for (float x = x0; x < rect_.x + rect_.w; x += cell) c.rect({x, rect_.y, 1, rect_.h}, grid);
  for (float y = y0; y < rect_.y + rect_.h; y += cell) c.rect({rect_.x, y, rect_.w, 1}, grid);
  Vec2 zero = toScreen(0, 0);
  c.rect({zero.x - 1, zero.y - 6, 2, 12}, grid);
  c.rect({zero.x - 6, zero.y - 1, 12, 2}, grid);
  if (layout == nullptr)
  {
    c.popClip();
    return;
  }
  if (fitPending && rect_.w > 0 && rect_.h > 0 && !layout->rooms.empty())
  {
    fitPending = false;
    float w = float(layout->maxX() - layout->minX()), h = float(layout->maxY() - layout->minY());
    cell = std::clamp(std::min((rect_.w - 80) / std::max(1.0f, w), (rect_.h - 80) / std::max(1.0f, h)), 8.0f, 48.0f);
    origin = {(rect_.w - w * cell) / 2 - layout->minX() * cell, (rect_.h - h * cell) / 2 - layout->minY() * cell};
  }
  const Font &f = font();
  float px = fontSize();
  for (const world::PlacedRoom &r : layout->rooms)
  {
    int rx = r.x, ry = r.y;
    bool dragging = r.id == drag_ && dragged_;
    if (dragging)
    {
      rx = dragX_;
      ry = dragY_;
    }
    auto it = areaOf.find(r.id);
    Color col = areaColour(it == areaOf.end() ? "default" : it->second);
    if (r.automatic) col = {col.x, col.y, col.z, 0.55f};
    if (dragging) col = {col.x, col.y, col.z, 0.7f};
    Vec2 a = toScreen(float(rx), float(ry));
    c.rect({a.x, a.y, r.w * cell, r.h * cell}, col);
    // Walls and doors of each cell, as the game's map draws them.
    float wall = std::max(1.5f, cell / 12);
    for (int y = 0; y < r.h; y++)
      for (int x = 0; x < r.w; x++)
      {
        uint8_t k = r.cells[size_t(y * r.w + x)];
        float cx = a.x + x * cell, cy = a.y + y * cell, d0 = cell * 0.3f, d1 = cell * 0.4f;
        if (k & world::North) c.rect({cx, cy, cell, wall}, kWall);
        if (k & world::South) c.rect({cx, cy + cell - wall, cell, wall}, kWall);
        if (k & world::West) c.rect({cx, cy, wall, cell}, kWall);
        if (k & world::East) c.rect({cx + cell - wall, cy, wall, cell}, kWall);
        if (k & (world::North << 4)) c.rect({cx + d0, cy, d1, wall * 1.5f}, kDoor);
        if (k & (world::South << 4)) c.rect({cx + d0, cy + cell - wall * 1.5f, d1, wall * 1.5f}, kDoor);
        if (k & (world::West << 4)) c.rect({cx, cy + d0, wall * 1.5f, d1}, kDoor);
        if (k & (world::East << 4)) c.rect({cx + cell - wall * 1.5f, cy + d0, wall * 1.5f, d1}, kDoor);
      }
    if (r.id == selected) c.border({a.x - 2, a.y - 2, r.w * cell + 4, r.h * cell + 4}, {1, 0.9f, 0.2f, 1}, 2);
    if (cell >= 14)
      c.textIn({a.x + wall + 2, a.y + wall + 1, r.w * cell - 2 * wall - 4, px * 1.3f}, r.id + (r.automatic ? " (doors)" : ""), f, px * 0.85f,
               {1, 1, 1, 0.95f});
  }
  // The doors between rooms: a line from the door's cell to the room it leads to.
  for (const world::Link &l : layout->links)
  {
    const world::PlacedRoom *to = layout->find(l.toRoom);
    if (to == nullptr || (l.matches && l.side != 0)) continue;   // a door that meets its room needs no line
    Vec2 a = toScreen(l.fromX + 0.5f, l.fromY + 0.5f), b = toScreen(to->x + to->w * 0.5f, to->y + to->h * 0.5f);
    c.line(a, b, l.side != 0 ? kBadLink : kLink, 2);
    c.rect({a.x - 3, a.y - 3, 6, 6}, l.side != 0 ? kBadLink : kLink);
  }
  c.popClip();
}

void registerWorldControls()
{
  registerControlType("WorldView", [] { return std::make_unique<WorldView>(); });
}

// --- WorldPanel ---------------------------------------------------------------------------

void WorldPanel::bind(Control &root, Context &ui)
{
  ui_ = &ui;
  view_ = root.findAs<WorldView>("world_view");
  list_ = root.findAs<ItemList>("world_rooms");
  warnings_ = root.findAs<ItemList>("world_warnings");
  status_ = root.findAs<Label>("world_status");
  roomTitle_ = root.findAs<Label>("world_room");
  hidden_ = root.findAs<CheckBox>("world_hidden");
  minimap_ = root.findAs<CheckBox>("world_minimap");
  cellW_ = root.findAs<SpinBox>("world_cell_w");
  cellH_ = root.findAs<SpinBox>("world_cell_h");
  list_->tileSize = 0;
  warnings_->tileSize = 0;
  for (SpinBox *s : {cellW_, cellH_})
  {
    s->minValue = 8;
    s->maxValue = 1024;
    s->decimals = 0;
  }
  view_->layout = &layout_;
  view_->picked = [this](const std::string &id) { select(id); };
  view_->moved = [this](const std::string &id, int x, int y) {
    if (path_.empty()) return;
    world::RoomEntry &e = entry(id);
    e.x = x;
    e.y = y;
    e.hidden = false;
    save();
  };
  list_->selectedChanged.connect([this](int i) {
    if (filling_ || i < 0 || size_t(i) >= infos_.size()) return;
    select(infos_[size_t(i)].id);
  });
  hidden_->toggled.connect([this](bool on) {
    if (filling_ || selected_.empty() || path_.empty()) return;
    entry(selected_).hidden = on;
    save();
  });
  minimap_->toggled.connect([this](bool on) {
    if (filling_ || path_.empty()) return;
    world_.minimap = on;
    save();
  });
  cellW_->valueChanged.connect([this](double v) {
    if (filling_ || path_.empty() || int(v) == world_.cellW) return;
    world_.cellW = int(v);
    save();
  });
  cellH_->valueChanged.connect([this](double v) {
    if (filling_ || path_.empty() || int(v) == world_.cellH) return;
    world_.cellH = int(v);
    save();
  });
  if (auto *b = root.findAs<Button>("world_new")) b->clicked.connect([this] { newWorld(); });
  if (auto *b = root.findAs<Button>("world_put")) b->clicked.connect([this] { putOnMap(); });
  if (auto *b = root.findAs<Button>("world_doors")) b->clicked.connect([this] { placeByDoors(); });
  if (auto *b = root.findAs<Button>("world_fix_all")) b->clicked.connect([this] { fixAll(); });
  relayout();
}

void WorldPanel::setRooms(const std::vector<std::string> &rooms)
{
  roomPaths_ = rooms;
  loadRooms();
}

void WorldPanel::setWorld(const std::string &path)
{
  if (path == path_ && !path.empty()) return;
  path_ = path;
  world_ = {};
  if (view_) view_->fitPending = true;
  if (!path_.empty())
  {
    try
    {
      world_ = world::load(path_);
    }
    catch (const std::exception &e)
    {
      if (log) log(e.what(), true);
    }
  }
  relayout();
}

void WorldPanel::loadRooms()
{
  infos_.clear();
  pathOf_.clear();
  roomTimes_.clear();
  for (const std::string &p : roomPaths_)
  {
    std::error_code ec;
    roomTimes_[p] = fs::last_write_time(p, ec);
    try
    {
      tmx::Map map = tmx::load(p);
      std::string id = world::roomIdOf(map);
      infos_.push_back(world::roomInfo(map, id));
      pathOf_[id] = p;
    }
    catch (const std::exception &e)
    {
      if (log) log(p + ": " + e.what(), true);
    }
  }
  relayout();
}

void WorldPanel::update(float dt)
{
  timer_ += dt;
  if (timer_ < 1.0f) return;
  timer_ = 0;
  for (const auto &[p, t] : roomTimes_)
  {
    std::error_code ec;
    if (fs::last_write_time(p, ec) != t)
    {
      loadRooms();
      return;
    }
  }
}

void WorldPanel::relayout()
{
  layout_ = world::layout(world_, infos_);
  if (view_ == nullptr) return;
  view_->areaOf.clear();
  for (const world::RoomInfo &r : infos_) view_->areaOf[r.id] = r.area;
  fillList();
}

world::RoomEntry &WorldPanel::entry(const std::string &id)
{
  if (world::RoomEntry *e = world_.find(id)) return *e;
  world_.rooms.push_back({id, std::nullopt, std::nullopt, false});
  return world_.rooms.back();
}

void WorldPanel::fillList()
{
  filling_ = true;
  list_->items.clear();
  int selectedRow = -1;
  for (const world::RoomInfo &r : infos_)
  {
    const world::PlacedRoom *p = layout_.find(r.id);
    const world::RoomEntry *e = world_.find(r.id);
    ItemList::Item item;
    if (p) item.text = r.id + "  " + std::to_string(p->x) + ", " + std::to_string(p->y) + (p->automatic ? "  by its doors" : "");
    else item.text = r.id + (e && e->hidden ? "  hidden" : "  not on the map");
    item.icon = p ? (p->automatic ? "link" : "map-marker") : (e && e->hidden ? "eye-slash" : "question");
    if (r.id == selected_) selectedRow = int(list_->items.size());
    list_->items.push_back(item);
  }
  list_->selected = selectedRow;
  warnings_->items.clear();
  auto note = [this](const std::string &text, const char *icon) {
    ItemList::Item item;
    item.text = text;
    item.icon = icon;
    warnings_->items.push_back(item);
  };
  for (const world::Layout::Problem &p : layout_.warnings) note(p.room + ": " + p.message, "warning");
  for (const std::string &id : layout_.unplaced)
  {
    const world::RoomEntry *e = world_.find(id);
    if (!e || !e->hidden) note(id + ": not on the map (no door at an edge leads there from it); fix a place for it in the file or hide it", "info");
  }
  if (layout_.cellCount() > world::MAX_CELLS)
    note(std::to_string(layout_.cellCount()) + " cells, the game keeps " + std::to_string(world::MAX_CELLS), "warning");
  minimap_->setPressed(world_.minimap);
  cellW_->setValue(world_.cellW, false);
  cellH_->setValue(world_.cellH, false);
  if (path_.empty()) status_->text = "The project has no world map: New world map makes one (the rooms are placed by their doors)";
  else
    status_->text = fs::path(path_).filename().string() + ": " + std::to_string(layout_.rooms.size()) + " rooms, " +
                    std::to_string(layout_.cellCount()) + " cells; drag a room to place it, drag the ground to look around, wheel to zoom";
  filling_ = false;
  showSelected();
}

void WorldPanel::select(const std::string &id)
{
  selected_ = id;
  view_->selected = id;
  fillList();
}

void WorldPanel::showSelected()
{
  filling_ = true;
  const world::PlacedRoom *p = layout_.find(selected_);
  const world::RoomEntry *e = world_.find(selected_);
  if (selected_.empty()) roomTitle_->text = "No room selected";
  else if (p)
    roomTitle_->text = selected_ + ": " + std::to_string(p->w) + " x " + std::to_string(p->h) + " cells at " + std::to_string(p->x) + ", " +
                       std::to_string(p->y) + (p->automatic ? " (placed by its doors)" : "");
  else roomTitle_->text = selected_ + ": not on the map";
  hidden_->setPressed(e != nullptr && e->hidden);
  filling_ = false;
}

void WorldPanel::save()
{
  if (path_.empty()) return;
  try
  {
    world::save(world_, path_);
  }
  catch (const std::exception &e)
  {
    if (log) log(e.what(), true);
    return;
  }
  relayout();
  if (changed) changed();
}

void WorldPanel::placeByDoors()
{
  if (path_.empty() || selected_.empty()) return;
  world::RoomEntry &e = entry(selected_);
  e.x.reset();
  e.y.reset();
  e.hidden = false;
  save();
}

void WorldPanel::putOnMap()
{
  if (path_.empty() || selected_.empty() || layout_.find(selected_)) return;
  world::RoomEntry &e = entry(selected_);
  e.x = layout_.rooms.empty() ? 0 : layout_.maxX() + 1;
  e.y = layout_.rooms.empty() ? 0 : layout_.minY();
  e.hidden = false;
  save();
}

void WorldPanel::fixAll()
{
  // Every room where it is now, also those placed by their doors (so that changing a door
  // later does not move them).
  if (path_.empty()) return;
  for (const world::PlacedRoom &p : layout_.rooms)
  {
    world::RoomEntry &e = entry(p.id);
    e.x = p.x;
    e.y = p.y;
  }
  save();
}

void WorldPanel::newWorld()
{
  if (!path_.empty())
  {
    if (log) log("The project has a world map already: " + path_, false);
    return;
  }
  if (newWorldDir.empty())
  {
    if (log) log("The project has no asset folder for the world map", true);
    return;
  }
  world::World w;
  // The first room at the start; the others follow their doors.
  if (!infos_.empty()) w.rooms.push_back({infos_[0].id, 0, 0, false});
  std::string path = (fs::path(newWorldDir) / "world.bmworld").string();
  try
  {
    world::save(w, path);
  }
  catch (const std::exception &e)
  {
    if (log) log(e.what(), true);
    return;
  }
  if (log) log("Wrote " + path, false);
  setWorld(path);
  if (created) created();
  if (changed) changed();
}

}  // namespace bm::editor
