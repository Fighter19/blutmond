// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The world map's layout in the cooker (tools/cooker/world_asset.h, docs/spec/world-format.md):
// rooms placed by the file and by their doors, walls and doors of the cells, the warnings, the
// file's text and the WRLD chunk.
#include "world_asset.h"

#include "test.h"

#include <cstring>
#include <map>
#include <filesystem>
#include <string>

using namespace bm::world;

namespace {

DoorInfo door(const std::string &name, int x, int y, int w, int h, const std::string &direction, const std::string &room,
              const std::string &target)
{
  DoorInfo d;
  d.name = name;
  d.x = x;
  d.y = y;
  d.w = w;
  d.h = h;
  d.direction = direction;
  d.targetRoom = room;
  d.targetDoor = target;
  return d;
}

RoomInfo room(const std::string &id, int w, int h, std::vector<DoorInfo> doors)
{
  RoomInfo r;
  r.id = id;
  r.widthPx = w;
  r.heightPx = h;
  r.doors = std::move(doors);
  return r;
}

// A: two screens wide, a door east (to B's west door, which is in B's lower cell), one in the
// floor (to C), one in the back wall (to D). B: one wide, two high. C: below A. D: a hidden
// room. E: no door leads there.
std::vector<RoomInfo> rooms()
{
  return {
    room("a", 480, 192, {door("e", 464, 48, 16, 32, "east", "b", "w"), door("s", 100, 176, 32, 16, "", "c", "n"),
                         door("back", 200, 100, 32, 48, "north", "d", "x")}),
    room("b", 240, 320, {door("w", 0, 240, 16, 48, "west", "a", "e")}),
    room("c", 240, 160, {door("n", 100, 0, 32, 16, "", "a", "s")}),
    room("d", 240, 160, {door("x", 0, 100, 16, 48, "west", "a", "back")}),
    room("e", 240, 160, {}),
  };
}

void testRoomsArePlacedByTheirDoors()
{
  World w;
  w.rooms.push_back({"a", 0, 0, false});
  w.rooms.push_back({"d", std::nullopt, std::nullopt, true});
  Layout l = layout(w, rooms());
  TEST_ASSERT(l.rooms.size() == 3);
  const PlacedRoom *a = l.find("a"), *b = l.find("b"), *c = l.find("c");
  TEST_ASSERT(a && b && c && !l.find("d") && !l.find("e"));
  TEST_ASSERT(a->w == 2 && a->h == 1 && !a->automatic);   // 192 px are one cell of 160 (rounded)
  // B's lower cell is right of A's east cell: B starts one row up.
  TEST_ASSERT(b->automatic && b->x == 2 && b->y == -1 && b->w == 1 && b->h == 2);
  // C's top cell is below A's cell with the door in the floor (x 100: the first cell).
  TEST_ASSERT(c->automatic && c->x == 0 && c->y == 1);
  TEST_ASSERT(l.unplaced.size() == 2);
  TEST_ASSERT(l.warnings.empty());
  TEST_ASSERT(l.minX() == 0 && l.minY() == -1 && l.maxX() == 3 && l.maxY() == 2 && l.cellCount() == 5);
  // Walls all around each room, doors where they are: A's east cell has a wall and a door
  // east, A's first cell a door south; inside a room there is no wall between cells.
  TEST_ASSERT(a->cells[0] == (North | South | West | South << 4));
  TEST_ASSERT(a->cells[1] == (North | South | East | East << 4));
  TEST_ASSERT(b->cells[0] == (North | East | West));
  TEST_ASSERT(b->cells[1] == (South | East | West | West << 4));
  // The doors between rooms on the map, all where the map says; the back wall door's room is
  // not on the map.
  int links = 0;
  for (const Link &k : l.links)
  {
    TEST_ASSERT(k.matches);
    links++;
  }
  TEST_ASSERT(links == 4);
}

void testWithoutPlacesTheFirstRoomIsTheStart()
{
  Layout l = layout(World{}, rooms());
  // D is reached through a door in the back wall only: not placed.
  TEST_ASSERT(l.rooms.size() == 3 && l.find("a")->x == 0 && l.find("a")->y == 0 && l.find("a")->automatic);
  TEST_ASSERT(l.find("b")->x == 2 && l.find("b")->y == -1);
}

void testProblemsAreReported()
{
  // B placed by hand where its door does not meet A's, and E over A.
  World w;
  w.rooms.push_back({"a", 0, 0, false});
  w.rooms.push_back({"b", 5, 0, false});
  w.rooms.push_back({"e", 1, 0, false});
  Layout l = layout(w, rooms());
  bool notNext = false, over = false;
  for (const Layout::Problem &p : l.warnings)
  {
    notNext |= p.room == "a" && p.message.find("door 'e'") != std::string::npos && p.message.find("not next to it") != std::string::npos;
    over |= p.room == "e" && p.message.find("lies over 'a'") != std::string::npos;
  }
  TEST_ASSERT(notNext && over);
  int mismatched = 0;
  for (const Link &k : l.links) mismatched += !k.matches;
  TEST_ASSERT(mismatched == 2);   // a to b and b to a
  // A room the file places is not moved to where its door would put it.
  TEST_ASSERT(l.find("b")->x == 5 && !l.find("b")->automatic);
}

std::string s_dir = "test-output/world";

void testTheFileAndTheChunk()
{
  const std::string &dir = s_dir;
  World w;
  w.cellW = 256;
  w.cellH = 192;
  w.minimap = false;
  w.rooms.push_back({"a", 3, -2, false});
  w.rooms.push_back({"d", std::nullopt, std::nullopt, true});
  std::filesystem::create_directories(dir);
  std::string path = dir + "/test.bmworld";
  save(w, path);
  World r = load(path);
  TEST_ASSERT(r.cellW == 256 && r.cellH == 192 && !r.minimap && r.rooms.size() == 2);
  TEST_ASSERT(r.rooms[0].id == "a" && *r.rooms[0].x == 3 && *r.rooms[0].y == -2 && !r.rooms[0].hidden);
  TEST_ASSERT(r.rooms[1].id == "d" && !r.rooms[1].x && r.rooms[1].hidden);
  TEST_ASSERT(text(r) == text(w));

  Layout l = layout(load(path), rooms());
  // 480 x 192 is 2 x 1 cells of 256 x 192.
  std::map<std::string, uint16_t> index = {{"a", 4}, {"b", 1}, {"c", 0}};
  std::map<std::string, uint8_t> area = {{"a", 2}};
  std::vector<uint8_t> c = chunk(l, index, area);
  auto u16 = [&](size_t at) { return int(c[at] | c[at + 1] << 8); };
  auto s16 = [&](size_t at) { return int(int16_t(u16(at))); };
  TEST_ASSERT(c.size() % 4 == 0 && c[0] == 1 && c[1] == 0 && u16(2) == 3);
  TEST_ASSERT(u16(4) == 256 && u16(6) == 192);
  TEST_ASSERT(s16(8) == l.minX() && s16(10) == l.minY() && u16(12) == l.maxX() - l.minX() && u16(14) == l.maxY() - l.minY());
  TEST_ASSERT(u16(16) == int(l.cellCount()));
  // The first room: a, pack room 4, at (3, -2), 2 x 1 cells from cell 0, area 2.
  TEST_ASSERT(u16(20) == 4 && s16(22) == 3 && s16(24) == -2 && c[26] == 2 && c[27] == 1 && u16(28) == 0 && c[30] == 2);
  TEST_ASSERT(u16(32) == 1 && u16(40) == 2);   // b, from cell 2
  // The cells after the rooms.
  TEST_ASSERT(c[20 + 3 * 12] == l.rooms[0].cells[0]);
}

void testCellsOfARoom()
{
  TEST_ASSERT(cellsAcross(1024, 240) == 4 && cellsAcross(192, 160) == 1 && cellsAcross(100, 240) == 1 && cellsAcross(360, 240) == 2);
  TEST_ASSERT(cellOf(0, 1024, 4) == 0 && cellOf(255, 1024, 4) == 0 && cellOf(256, 1024, 4) == 1 && cellOf(1023, 1024, 4) == 3);
  TEST_ASSERT(cellOf(2000, 1024, 4) == 3 && cellOf(-5, 1024, 4) == 0);
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc > 1) s_dir = argv[1];
  RUN_TEST(testCellsOfARoom);
  RUN_TEST(testRoomsArePlacedByTheirDoors);
  RUN_TEST(testWithoutPlacesTheFirstRoomIsTheStart);
  RUN_TEST(testProblemsAreReported);
  RUN_TEST(testTheFileAndTheChunk);
  return TEST_RESULT();
}
