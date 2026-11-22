// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "world_asset.h"

#include "xml.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bm::world {

namespace {

int number(const std::string &path, const std::string &key, const std::string &v)
{
  try
  {
    size_t used = 0;
    int n = std::stoi(v, &used);
    if (used != v.size()) throw std::invalid_argument(v);
    return n;
  }
  catch (const std::exception &)
  {
    throw std::runtime_error(path + ": " + key + " is not a number: '" + v + "'");
  }
}

// Within this many pixels of a room's edge a door is on that edge.
constexpr int EDGE = 16;

// The side of the room a door is on (0: inside, a door in the back wall or in the middle).
uint8_t sideOf(const DoorInfo &d, const RoomInfo &r)
{
  if (d.direction == "east") return East;
  if (d.direction == "west") return West;
  // "north" is a door in the back wall (Up goes through it, runtime-core.md §6) unless it is
  // at the top edge.
  if (d.y <= EDGE) return North;
  if (d.y + d.h >= r.heightPx - EDGE && d.direction != "north") return South;
  if (d.direction == "north") return 0;
  if (d.x <= EDGE) return West;
  if (d.x + d.w >= r.widthPx - EDGE) return East;
  return 0;
}

struct Cell
{
  int x = 0, y = 0;
};

// The room-local cell a door is in.
Cell doorCell(const DoorInfo &d, const RoomInfo &r, int w, int h, uint8_t side)
{
  Cell c{cellOf(d.x + d.w / 2, r.widthPx, w), cellOf(d.y + d.h / 2, r.heightPx, h)};
  if (side == East) c.x = w - 1;
  if (side == West) c.x = 0;
  if (side == North) c.y = 0;
  if (side == South) c.y = h - 1;
  return c;
}

Cell step(uint8_t side)
{
  return side == East ? Cell{1, 0} : side == West ? Cell{-1, 0} : side == North ? Cell{0, -1} : side == South ? Cell{0, 1} : Cell{0, 0};
}

bool overlaps(const PlacedRoom &a, int x, int y, int w, int h)
{
  return a.x < x + w && x < a.x + a.w && a.y < y + h && y < a.y + a.h;
}

bool contains(const PlacedRoom &a, int x, int y)
{
  return x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h;
}

const char *sideName(uint8_t side)
{
  return side == North ? "north" : side == East ? "east" : side == South ? "south" : side == West ? "west" : "inside";
}

}  // namespace

int cellsAcross(int px, int cell)
{
  if (cell <= 0) return 1;
  int n = int(std::lround(double(px) / cell));
  return n < 1 ? 1 : n;
}

int cellOf(int px, int sizePx, int cells)
{
  if (sizePx <= 0 || px < 0) return 0;
  int c = int(int64_t(px) * cells / sizePx);
  return c >= cells ? cells - 1 : c;
}

std::string roomIdOf(const tmx::Map &map)
{
  std::string id = map.property("room_id");
  if (!id.empty()) return id;
  size_t slash = map.path.find_last_of("/\\");
  std::string file = slash == std::string::npos ? map.path : map.path.substr(slash + 1);
  size_t dot = file.find_last_of('.');
  return dot == std::string::npos ? file : file.substr(0, dot);
}

RoomInfo roomInfo(const tmx::Map &map, const std::string &id)
{
  RoomInfo r;
  r.id = id;
  r.path = map.path;
  r.area = map.property("area", "default");
  r.widthPx = map.width * map.tileWidth;
  r.heightPx = map.height * map.tileHeight;
  for (const tmx::ObjectGroup &g : map.objectGroups)
    for (const tmx::Object &o : g.objects)
    {
      if (o.type != "Door") continue;
      DoorInfo d;
      d.name = o.name;
      d.x = int(std::lround(o.x));
      d.y = int(std::lround(o.y));
      d.w = int(std::lround(o.width));
      d.h = int(std::lround(o.height));
      auto prop = [&](const char *k) {
        auto it = o.properties.find(k);
        return it == o.properties.end() ? std::string() : it->second;
      };
      d.direction = prop("direction");
      d.targetRoom = prop("target_room");
      d.targetDoor = prop("target_door");
      r.doors.push_back(d);
    }
  return r;
}

RoomEntry *World::find(const std::string &id)
{
  for (RoomEntry &e : rooms)
    if (e.id == id) return &e;
  return nullptr;
}
const RoomEntry *World::find(const std::string &id) const
{
  return const_cast<World *>(this)->find(id);
}

World load(const std::string &path)
{
  World w;
  w.path = path;
  auto root = xml::parseFile(path);
  if (root->name != "world") throw std::runtime_error(path + ": not a world file");
  if (root->attr("version", "1") != "1") throw std::runtime_error(path + ": world version " + root->attr("version") + " is not supported");
  if (root->has("cell_w")) w.cellW = number(path, "cell_w", root->attr("cell_w"));
  if (root->has("cell_h")) w.cellH = number(path, "cell_h", root->attr("cell_h"));
  if (w.cellW < 8 || w.cellH < 8) throw std::runtime_error(path + ": cells are at least 8 x 8 pixels");
  w.minimap = root->attr("minimap", "true") != "false";
  for (const xml::Element *e : root->childrenNamed("room"))
  {
    RoomEntry r;
    r.id = e->attr("id");
    if (r.id.empty()) throw std::runtime_error(path + ": a room without id");
    if (w.find(r.id)) throw std::runtime_error(path + ": room '" + r.id + "' is listed twice");
    if (e->has("x") != e->has("y")) throw std::runtime_error(path + ": room '" + r.id + "' needs both x and y");
    if (e->has("x"))
    {
      r.x = number(path, "x", e->attr("x"));
      r.y = number(path, "y", e->attr("y"));
    }
    r.hidden = e->attr("hidden", "false") == "true";
    w.rooms.push_back(r);
  }
  return w;
}

std::string text(const World &world)
{
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  o << "<world version=\"1\" cell_w=\"" << world.cellW << "\" cell_h=\"" << world.cellH << "\"";
  if (!world.minimap) o << " minimap=\"false\"";
  o << ">\n";
  for (const RoomEntry &r : world.rooms)
  {
    o << " <room id=\"" << r.id << "\"";
    if (r.x && r.y) o << " x=\"" << *r.x << "\" y=\"" << *r.y << "\"";
    if (r.hidden) o << " hidden=\"true\"";
    o << "/>\n";
  }
  o << "</world>\n";
  return o.str();
}

void save(const World &world, const std::string &path)
{
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + path);
  f << text(world);
}

const PlacedRoom *Layout::find(const std::string &id) const
{
  for (const PlacedRoom &r : rooms)
    if (r.id == id) return &r;
  return nullptr;
}

int Layout::minX() const
{
  int v = 0;
  for (size_t i = 0; i < rooms.size(); i++) v = i == 0 ? rooms[i].x : std::min(v, rooms[i].x);
  return v;
}
int Layout::minY() const
{
  int v = 0;
  for (size_t i = 0; i < rooms.size(); i++) v = i == 0 ? rooms[i].y : std::min(v, rooms[i].y);
  return v;
}
int Layout::maxX() const
{
  int v = 0;
  for (size_t i = 0; i < rooms.size(); i++) v = i == 0 ? rooms[i].x + rooms[i].w : std::max(v, rooms[i].x + rooms[i].w);
  return v;
}
int Layout::maxY() const
{
  int v = 0;
  for (size_t i = 0; i < rooms.size(); i++) v = i == 0 ? rooms[i].y + rooms[i].h : std::max(v, rooms[i].y + rooms[i].h);
  return v;
}
size_t Layout::cellCount() const
{
  size_t n = 0;
  for (const PlacedRoom &r : rooms) n += r.cells.size();
  return n;
}

Layout layout(const World &world, const std::vector<RoomInfo> &infos)
{
  Layout out;
  out.cellW = world.cellW;
  out.cellH = world.cellH;
  out.minimap = world.minimap;

  // Per room of the cook: placed or not, where.
  struct Work
  {
    const RoomInfo *info = nullptr;
    int w = 1, h = 1;
    bool hidden = false, placed = false, automatic = false;
    int x = 0, y = 0;
  };
  std::vector<Work> work(infos.size());
  std::map<std::string, size_t> byId;
  for (size_t i = 0; i < infos.size(); i++)
  {
    Work &k = work[i];
    k.info = &infos[i];
    k.w = cellsAcross(infos[i].widthPx, world.cellW);
    k.h = cellsAcross(infos[i].heightPx, world.cellH);
    byId[infos[i].id] = i;
    if (const RoomEntry *e = world.find(infos[i].id))
    {
      k.hidden = e->hidden;
      if (!e->hidden && e->x && e->y)
      {
        k.placed = true;
        k.x = *e->x;
        k.y = *e->y;
      }
    }
  }
  auto placedAt = [&](int x, int y, int w, int h, size_t except) {
    for (size_t i = 0; i < work.size(); i++)
      if (i != except && work[i].placed && work[i].x < x + w && x < work[i].x + work[i].w && work[i].y < y + h && y < work[i].y + work[i].h)
        return true;
    return false;
  };
  if (std::none_of(work.begin(), work.end(), [](const Work &k) { return k.placed; }))
    for (Work &k : work)
      if (!k.hidden)
      {
        k.placed = k.automatic = true;
        break;
      }

  // Rooms the file does not place go next to a placed room they share a door with, until no
  // more can be placed.
  for (bool more = true; more;)
  {
    more = false;
    for (size_t a = 0; a < work.size(); a++)
    {
      if (!work[a].placed) continue;
      const RoomInfo &ra = *work[a].info;
      for (const DoorInfo &d : ra.doors)
      {
        auto t = byId.find(d.targetRoom);
        if (t == byId.end()) continue;
        Work &b = work[t->second];
        if (b.placed || b.hidden) continue;
        uint8_t side = sideOf(d, ra);
        if (side == 0) continue;
        const RoomInfo &rb = *b.info;
        auto td = std::find_if(rb.doors.begin(), rb.doors.end(), [&](const DoorInfo &o) { return o.name == d.targetDoor; });
        if (td == rb.doors.end()) continue;
        uint8_t back = sideOf(*td, rb);
        if (back == 0) continue;
        Cell ca = doorCell(d, ra, work[a].w, work[a].h, side), cb = doorCell(*td, rb, b.w, b.h, back);
        Cell s = step(side);
        int x = work[a].x + ca.x + s.x - cb.x, y = work[a].y + ca.y + s.y - cb.y;
        if (placedAt(x, y, b.w, b.h, t->second)) continue;
        b.placed = b.automatic = true;
        b.x = x;
        b.y = y;
        more = true;
      }
    }
  }

  for (const Work &k : work)
  {
    if (!k.placed)
    {
      out.unplaced.push_back(k.info->id);
      continue;
    }
    PlacedRoom p;
    p.id = k.info->id;
    p.x = k.x;
    p.y = k.y;
    p.w = k.w;
    p.h = k.h;
    p.automatic = k.automatic;
    p.cells.assign(size_t(k.w) * size_t(k.h), 0);
    // Walls all around; the cells inside are open to each other.
    for (int y = 0; y < k.h; y++)
      for (int x = 0; x < k.w; x++)
      {
        uint8_t walls = 0;
        if (y == 0) walls |= North;
        if (y == k.h - 1) walls |= South;
        if (x == 0) walls |= West;
        if (x == k.w - 1) walls |= East;
        p.cells[size_t(y * k.w + x)] = walls;
      }
    for (const DoorInfo &d : k.info->doors)
    {
      uint8_t side = sideOf(d, *k.info);
      if (side == 0) continue;
      Cell c = doorCell(d, *k.info, k.w, k.h, side);
      p.cells[size_t(c.y * k.w + c.x)] |= uint8_t(side << 4);
    }
    out.rooms.push_back(p);
  }

  // Doors between rooms on the map, and whether the map agrees with them.
  for (const PlacedRoom &p : out.rooms)
  {
    const RoomInfo &info = *work[byId[p.id]].info;
    for (const DoorInfo &d : info.doors)
    {
      const PlacedRoom *target = out.find(d.targetRoom);
      if (target == nullptr) continue;
      uint8_t side = sideOf(d, info);
      Cell c = doorCell(d, info, p.w, p.h, side);
      Link l;
      l.fromRoom = p.id;
      l.fromDoor = d.name;
      l.toRoom = d.targetRoom;
      l.fromX = p.x + c.x;
      l.fromY = p.y + c.y;
      l.side = side;
      if (side != 0)
      {
        Cell s = step(side);
        l.matches = contains(*target, l.fromX + s.x, l.fromY + s.y);
        if (!l.matches)
          out.warnings.push_back({p.id, "door '" + d.name + "' (" + sideName(side) + ") leads to '" + d.targetRoom +
                                            "', which is not next to it on the map"});
      }
      out.links.push_back(l);
    }
  }
  for (size_t i = 0; i < out.rooms.size(); i++)
    for (size_t j = i + 1; j < out.rooms.size(); j++)
    {
      const PlacedRoom &a = out.rooms[i], &b = out.rooms[j];
      if (overlaps(a, b.x, b.y, b.w, b.h)) out.warnings.push_back({b.id, "lies over '" + a.id + "' on the map"});
    }
  return out;
}

std::vector<uint8_t> chunk(const Layout &layout, const std::map<std::string, uint16_t> &packIndex,
                           const std::map<std::string, uint8_t> &area)
{
  std::vector<uint8_t> b;
  auto u8 = [&](uint32_t v) { b.push_back(uint8_t(v)); };
  auto u16 = [&](uint32_t v) {
    u8(v);
    u8(v >> 8);
  };
  std::vector<const PlacedRoom *> rooms;
  for (const PlacedRoom &r : layout.rooms)
    if (packIndex.count(r.id)) rooms.push_back(&r);
  int minX = layout.minX(), minY = layout.minY();
  u8(1);
  u8(layout.minimap ? 1 : 0);
  u16(uint32_t(rooms.size()));
  u16(uint32_t(layout.cellW));
  u16(uint32_t(layout.cellH));
  u16(uint32_t(int16_t(minX)) & 0xFFFF);
  u16(uint32_t(int16_t(minY)) & 0xFFFF);
  u16(uint32_t(layout.maxX() - minX));
  u16(uint32_t(layout.maxY() - minY));
  size_t cells = 0;
  for (const PlacedRoom *r : rooms) cells += r->cells.size();
  u16(uint32_t(cells));
  u16(0);
  size_t first = 0;
  for (const PlacedRoom *r : rooms)
  {
    u16(packIndex.at(r->id));
    u16(uint32_t(int16_t(r->x)) & 0xFFFF);
    u16(uint32_t(int16_t(r->y)) & 0xFFFF);
    u8(uint32_t(r->w));
    u8(uint32_t(r->h));
    u16(uint32_t(first));
    auto a = area.find(r->id);
    u8(a == area.end() ? 0 : a->second);
    u8(0);
    first += r->cells.size();
  }
  for (const PlacedRoom *r : rooms) b.insert(b.end(), r->cells.begin(), r->cells.end());
  while (b.size() % 4) b.push_back(0);
  return b;
}

}  // namespace bm::world
