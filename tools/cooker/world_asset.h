// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The world map (docs/spec/world-format.md): where the rooms lie on a grid of screens, which
// the game's map screen and the HUD's small map show, and the editor's World tab edits.
//
//   <world version="1" cell_w="240" cell_h="160" minimap="true">
//    <room id="test_room" x="0" y="0"/>
//    <room id="debug_lobby" hidden="true"/>     <!-- not on the map -->
//   </world>
//
// A room the file does not place is placed by the cooker next to a placed room it shares a
// door with (an edge door on both sides); a room that cannot be placed so is not on the map.
// Without any placed room the first room of the cook is at (0, 0).
#pragma once

#include "tmx.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bm::world {

/// Sides of a map cell, as bits of its walls (and of its doors, four bits up).
enum Side : uint8_t { North = 1, East = 2, South = 4, West = 8 };

/// What the layout needs to know of a door of a room.
struct DoorInfo
{
  std::string name;
  int x = 0, y = 0, w = 0, h = 0;   // pixels in the room
  std::string direction;            // the property (north, south, east, west), may be empty
  std::string targetRoom, targetDoor;
};

/// What the layout needs to know of a room.
struct RoomInfo
{
  std::string id, path, area;
  int widthPx = 0, heightPx = 0;
  std::vector<DoorInfo> doors;
};

/// From a Tiled room (its id as the cooker gives it).
RoomInfo roomInfo(const tmx::Map &map, const std::string &id);

/// A room's id: its `room_id` property, else the file name without extension.
std::string roomIdOf(const tmx::Map &map);

/// A room as the world file has it.
struct RoomEntry
{
  std::string id;
  std::optional<int> x, y;   // in cells; both or neither
  bool hidden = false;       // never on the map
};

struct World
{
  std::string path;
  int cellW = 240, cellH = 160;   // pixels of a room that make one map cell
  bool minimap = true;            // the HUD shows a small map around the player
  std::vector<RoomEntry> rooms;   // in the file's order

  RoomEntry *find(const std::string &id);
  const RoomEntry *find(const std::string &id) const;
};

/// Reads a world file. Throws std::runtime_error with a message on errors.
World load(const std::string &path);
/// The file's text, as the editor writes it.
std::string text(const World &world);
void save(const World &world, const std::string &path);

/// A room on the map.
struct PlacedRoom
{
  std::string id;
  int x = 0, y = 0, w = 1, h = 1;   // cells
  bool automatic = false;           // placed by its doors, not by the file
  std::vector<uint8_t> cells;       // w * h, row major: walls (bits 0-3) and doors (bits 4-7)
};

/// A door between two rooms on the map.
struct Link
{
  std::string fromRoom, fromDoor, toRoom;
  int fromX = 0, fromY = 0;          // the map cell the door is in
  uint8_t side = 0;                  // the side it is on (0: inside the room, no edge)
  bool matches = true;               // the target room lies on the other side of that edge
};

struct Layout
{
  int cellW = 240, cellH = 160;
  bool minimap = true;
  std::vector<PlacedRoom> rooms;     // placed rooms, in the order of `RoomInfo`s given
  std::vector<std::string> unplaced; // rooms that are not on the map (hidden ones too)
  std::vector<Link> links;
  struct Problem
  {
    std::string room, message;
  };
  std::vector<Problem> warnings;     // overlaps, doors that lead elsewhere than the map shows

  const PlacedRoom *find(const std::string &id) const;
  int minX() const, minY() const, maxX() const, maxY() const;   // bounds in cells (max exclusive)
  size_t cellCount() const;
};

/// The size in cells of a room: its pixels over the cell size, rounded, at least 1.
int cellsAcross(int px, int cell);

/// The map cell (room-local) a pixel of the room is in.
int cellOf(int px, int sizePx, int cells);

/// Places the rooms (rooms of the cook, in the order given).
Layout layout(const World &world, const std::vector<RoomInfo> &rooms);

/// The WRLD chunk (docs/spec/world-format.md §3); `packIndex` gives the pack's room index of a
/// room id, `area` its area group.
std::vector<uint8_t> chunk(const Layout &layout, const std::map<std::string, uint16_t> &packIndex,
                           const std::map<std::string, uint8_t> &area);

/// At most this many cells on a map (the explored ones are a bit each in the save game).
constexpr size_t MAX_CELLS = 512;
/// At most this many rooms on a map.
constexpr size_t MAX_ROOMS = 64;

}  // namespace bm::world
