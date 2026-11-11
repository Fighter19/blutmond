// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Project file, asset catalogue, new rooms and colour overrides, in a scratch copy of the
// test room. Argument: the repository's assets folder.

#include "cook.h"
#include "overrides.h"
#include "project_file.h"
#include "tmx.h"

#include "test.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using namespace bm;
using namespace bm::editor;

static std::string s_assets, s_work;

static void setUp()
{
  fs::remove_all(s_work);
  fs::create_directories(s_work);
  fs::copy(s_assets + "/test_room", s_work + "/test_room", fs::copy_options::recursive);
  fs::copy(s_assets + "/engine", s_work + "/engine", fs::copy_options::recursive);
}

static ProjectFile makeProject()
{
  ProjectFile p;
  p.path = normalPath(s_work + "/game.bmproj");
  p.name = "Test";
  p.rooms = {normalPath(s_work + "/test_room/room.tmx"), normalPath(s_work + "/test_room/room2.tmx")};
  p.assetDirs = {normalPath(s_work + "/test_room"), normalPath(s_work + "/engine")};
  return p;
}

static void testSaveAndLoad()
{
  setUp();
  ProjectFile p = makeProject();
  TEST_ASSERT(p.save());
  ProjectFile q = ProjectFile::load(p.path);
  TEST_ASSERT(q.name == "Test");
  TEST_ASSERT(q.rooms == p.rooms);
  TEST_ASSERT(q.assetDirs == p.assetDirs);
  TEST_ASSERT(!q.addRoom(s_work + "/test_room/../test_room/room.tmx"));  // the same file
  TEST_ASSERT(q.removeRoom(s_work + "/test_room/room2.tmx"));
  TEST_ASSERT(q.rooms.size() == 1);
}

static void testCatalogKnowsWhoUsesWhat()
{
  setUp();
  ProjectFile p = makeProject();
  fs::copy_file(s_work + "/test_room/tileset.png", s_work + "/test_room/unused.png");
  AssetCatalog c = AssetCatalog::scan(p);
  const AssetInfo *tiles = c.find(s_work + "/test_room/tileset.png");
  TEST_ASSERT(tiles != nullptr);
  TEST_ASSERT(tiles->kind == AssetKind::Image);
  TEST_ASSERT(tiles->usedBy.size() == 2);
  TEST_ASSERT(tiles->width == 128 && tiles->height == 64);
  const AssetInfo *collision = c.find(s_work + "/engine/blutmond_collision.tsx");
  TEST_ASSERT(collision != nullptr && collision->kind == AssetKind::Tileset && collision->usedBy.size() == 2);
  const AssetInfo *unused = c.find(s_work + "/test_room/unused.png");
  TEST_ASSERT(unused != nullptr && unused->usedBy.empty());
  const AssetInfo *room = c.find(s_work + "/test_room/room.tmx");
  TEST_ASSERT(room != nullptr && room->kind == AssetKind::Room && room->inProject);
  TEST_ASSERT(c.assets.front().kind == AssetKind::Room);
}

static void testNewRoomCooks()
{
  setUp();
  NewRoom r;
  r.path = s_work + "/test_room/cave.tmx";
  r.roomId = "cave";
  r.area = "test";
  r.screensX = 3;
  r.screensY = 2;
  r.tileset = s_work + "/test_room/tileset.png";
  r.engineDir = s_work + "/engine";
  std::string error;
  TEST_ASSERT(createRoom(r, &error));
  TEST_ASSERT(!createRoom(r, &error));  // never overwrites
  tmx::Map map = tmx::load(r.path);
  TEST_ASSERT(map.width == 45 && map.height == 20);
  TEST_ASSERT(map.property("room_id") == "cave");
  std::vector<tmx::Map> maps{tmx::load(s_work + "/test_room/room.tmx"), map};
  CookOptions options;
  options.profile = *profileByName("gba");
  Report report;
  std::vector<uint8_t> pack = cook(maps, options, report);
  if (pack.empty()) printf("%s", report.text().c_str());
  TEST_ASSERT(!pack.empty());

  ProjectFile p = makeProject();
  TEST_ASSERT(p.addRoom(r.path));
  AssetCatalog c = AssetCatalog::scan(p);
  TEST_ASSERT(c.find(s_work + "/test_room/tileset.png")->usedBy.size() == 3);
}

static void testImportAndDelete()
{
  setUp();
  std::string newPath, error;
  TEST_ASSERT(importAsset(s_work + "/test_room/tileset.png", s_work + "/imported", &newPath, &error));
  TEST_ASSERT(fs::exists(newPath));
  TEST_ASSERT(!importAsset(s_work + "/test_room/tileset.png", s_work + "/imported", &newPath, &error));
  TEST_ASSERT(deleteAsset(newPath, &error));
  TEST_ASSERT(!fs::exists(newPath));
}

static std::vector<uint16_t> cookedPalette(const std::string &profile, CookInfo *pInfo)
{
  std::vector<tmx::Map> maps{tmx::load(s_work + "/test_room/room.tmx")};
  CookOptions options;
  options.profile = *profileByName(profile);
  options.pInfo = pInfo;
  Report report;
  std::vector<uint8_t> pack = cook(maps, options, report);
  std::vector<uint16_t> colours;
  BmPack p;
  if (pack.empty() || bmPackOpen(&p, pack.data(), uint32_t(pack.size()), true) != BM_PACK_OK) return colours;
  uint32_t size = 0;
  const uint8_t *pal = bmPackChunkData(&p, uint32_t(bmPackFindChunk(&p, BM_CHUNK_PALE, 0)), &size);
  for (uint32_t i = 4; i + 1 < size; i += 2) colours.push_back(uint16_t(pal[i] | (pal[i + 1] << 8)));
  return colours;
}

static void testColourOverride()
{
  setUp();
  CookInfo info;
  std::vector<uint16_t> before = cookedPalette("gba", &info);
  TEST_ASSERT(!before.empty());
  // Pick a hardware colour and the source colour behind it.
  uint16_t hw = 0;
  std::pair<std::string, uint32_t> source;
  for (uint16_t c : before)
    if (c != 0 && info.colourSources.count(c))
    {
      hw = c;
      source = *info.colourSources[c].begin();
      break;
    }
  TEST_ASSERT(hw != 0);
  TEST_ASSERT(normalPath(source.first) == normalPath(s_work + "/test_room/tileset.png"));

  AssetOverrides o;
  o.colours["gba"][source.second] = 0xFF0000;  // pure red: BGR555 0x001F
  TEST_ASSERT(saveOverrides(source.first, o));
  TEST_ASSERT(loadOverrides(source.first).colours.at("gba").at(source.second) == 0xFF0000);

  CookInfo info2;
  std::vector<uint16_t> after = cookedPalette("gba", &info2);
  TEST_ASSERT(std::count(after.begin(), after.end(), uint16_t(0x001F)) == 1);
  TEST_ASSERT(info2.overridden.count(source) == 1);
  // Another profile is not affected...
  std::vector<uint16_t> snes = cookedPalette("snes", nullptr);
  TEST_ASSERT(std::count(snes.begin(), snes.end(), uint16_t(0x001F)) == 0);
  // ...unless the override is for every profile.
  o.colours.clear();
  o.colours["*"][source.second] = 0xFF0000;
  TEST_ASSERT(saveOverrides(source.first, o));
  snes = cookedPalette("snes", nullptr);
  TEST_ASSERT(std::count(snes.begin(), snes.end(), uint16_t(0x001F)) == 1);
  // No overrides left: the file goes away.
  TEST_ASSERT(saveOverrides(source.first, AssetOverrides{}));
  TEST_ASSERT(!fs::exists(overridesPath(source.first)));
  TEST_ASSERT(cookedPalette("gba", nullptr) == before);
}

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "usage: test_project <assets dir> <scratch dir>\n");
    return 2;
  }
  s_assets = normalPath(argv[1]);
  s_work = normalPath(argv[2]);
  RUN_TEST(testSaveAndLoad);
  RUN_TEST(testCatalogKnowsWhoUsesWhat);
  RUN_TEST(testNewRoomCooks);
  RUN_TEST(testImportAndDelete);
  RUN_TEST(testColourOverride);
  fs::remove_all(s_work);
  return TEST_RESULT();
}
