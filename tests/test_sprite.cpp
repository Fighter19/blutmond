// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Sprites: the source file, the cooker (pieces, shared tiles, palette) and the runtime's
// animation player and drawing. Arguments: the repository's assets folder, a scratch folder.

#include "cook.h"
#include "sprite_asset.h"

extern "C" {
#include "device.h"
#include "hal_host.h"
#include "physical_device.h"
#include "sprite.h"
#include "video.h"
}

#include "test.h"

#include <filesystem>

namespace fs = std::filesystem;
using namespace bm;

static std::string s_assets, s_work;

static void testLoadSaveRoundTrip()
{
  sprite::Sprite s = sprite::load(s_assets + "/test_room/player.bmsprite");
  TEST_ASSERT(s.name == "player");
  TEST_ASSERT(s.frames.size() == 10);
  TEST_ASSERT(s.animations.size() == 5);
  TEST_ASSERT(s.animations[1].name == "run" && s.animations[1].steps.size() == 4);
  TEST_ASSERT(s.frames[0].ax == 8 && s.frames[0].ay == 32);

  fs::create_directories(s_work);
  fs::copy_file(s_assets + "/test_room/player.png", s_work + "/player.png", fs::copy_options::overwrite_existing);
  s.path = s_work + "/copy.bmsprite";
  s.frames[3].ownHitbox = true;
  s.frames[3].hitbox = {-4, -20, 8, 20};
  s.animations[0].steps[0].duration = 17;
  TEST_ASSERT(sprite::save(s));
  sprite::Sprite t = sprite::load(s.path);
  TEST_ASSERT(t.frames.size() == s.frames.size());
  TEST_ASSERT(t.frames[3].ownHitbox && t.frames[3].hitbox == s.frames[3].hitbox);
  TEST_ASSERT(!t.frames[2].ownHitbox);
  TEST_ASSERT(t.animations[0].steps[0].duration == 17);
  TEST_ASSERT(t.animations[2].loop == false);
}

static void testCook()
{
  sprite::Sprite s = sprite::load(s_assets + "/test_room/player.bmsprite");
  Report report;
  sprite::Cooked c = sprite::cook(s, *profileByName("gba"), report);
  TEST_ASSERT(!report.hasErrors());
  TEST_ASSERT(c.frames.size() == 10);
  // A 16x32 frame is one tall hardware sprite of 8 tiles.
  TEST_ASSERT(c.frames[0].pieces.size() == 1);
  TEST_ASSERT(c.frames[0].pieces[0].w == 16 && c.frames[0].pieces[0].h == 32);
  TEST_ASSERT(c.frames[0].pieces[0].dx == -8 && c.frames[0].pieces[0].dy == -32);
  TEST_ASSERT(c.maxPieces == 1);
  TEST_ASSERT(c.maxLineCost == 16);
  TEST_ASSERT(c.tiles.size() == 10 * 8);
  TEST_ASSERT(c.palette[0] == 0 && c.palette[1] != 0);
  TEST_ASSERT(c.frames[0].bounds.w > 0 && c.frames[0].bounds.w <= 16);

  // The same picture twice is stored once.
  s.frames.push_back(s.frames[0]);
  Report again;
  sprite::Cooked d = sprite::cook(s, *profileByName("gba"), again);
  TEST_ASSERT(d.tiles.size() == c.tiles.size());
  TEST_ASSERT(d.frames[10].pieces[0].tile == d.frames[0].pieces[0].tile);

  // An animation that uses a frame that is not there does not cook.
  s.animations[0].steps[0].frame = 99;
  Report bad;
  sprite::cook(s, *profileByName("gba"), bad);
  TEST_ASSERT(bad.hasErrors());
}

static std::vector<uint8_t> cookPack()
{
  CookOptions options;
  options.profile = *profileByName("gba");
  options.sprites = {s_assets + "/test_room/player.bmsprite", s_assets + "/test_room/bat.bmsprite"};
  std::vector<tmx::Map> maps{tmx::load(s_assets + "/test_room/room.tmx")};
  Report report;
  return cook(maps, options, report);
}

static void testRuntime()
{
  static std::vector<uint8_t> data;
  data = cookPack();
  TEST_ASSERT(!data.empty());
  BmPack pack;
  TEST_ASSERT(bmPackOpen(&pack, data.data(), uint32_t(data.size()), true) == BM_PACK_OK);
  BmSpriteSheet sheet;
  TEST_ASSERT(bmSpriteSheetFind(&sheet, &pack, "Player") == BM_SUCCESS);  // case does not matter
  TEST_ASSERT(sheet.frameCount == 10 && sheet.animCount == 5);
  BmSpriteSheet bat;
  TEST_ASSERT(bmSpriteSheetFind(&bat, &pack, "bat") == BM_SUCCESS);
  TEST_ASSERT(bmSpriteSheetFind(&bat, &pack, "ghost") == BM_ERROR_NOT_FOUND);

  int32_t run = bmSpriteSheetAnim(&sheet, &pack, "run");
  int32_t jump = bmSpriteSheetAnim(&sheet, &pack, "jump");
  TEST_ASSERT(run == 1 && jump == 2);
  TEST_ASSERT(bmSpriteSheetAnim(&sheet, &pack, "swim") == -1);

  // run: frames 2, 3, 4, 5 for 6 game frames each, then from the start.
  BmAnimState st = {0, 0, 0, 0};
  bmAnimPlay(&st, uint8_t(run), false);
  uint16_t seen[30];
  for (int i = 0; i < 30; i++)
  {
    seen[i] = bmAnimFrame(&sheet, &st);
    bmAnimTick(&sheet, &st);
  }
  TEST_ASSERT(seen[0] == 2 && seen[5] == 2 && seen[6] == 3 && seen[12] == 4 && seen[18] == 5 && seen[23] == 5 && seen[24] == 2);
  TEST_ASSERT(!st.finished);
  TEST_ASSERT(bmAnimFrameAt(&sheet, uint8_t(run), 13) == 4);
  // Playing the same animation again does not start it over; another one does.
  bmAnimPlay(&st, uint8_t(run), false);
  TEST_ASSERT(st.step == 1);
  bmAnimPlay(&st, uint8_t(jump), false);
  TEST_ASSERT(st.step == 0 && bmAnimFrame(&sheet, &st) == 6);
  bmAnimTick(&sheet, &st);
  TEST_ASSERT(st.finished && bmAnimFrame(&sheet, &st) == 6);

  // Drawing: one hardware sprite at anchor - (8, 32); mirrored it covers the same place.
  bmHalHostReset();
  BmDeviceHandle device = nullptr;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  bmVideoInit();
  TEST_ASSERT(bmSpriteSheetLoad(&sheet, 40, 3) == BM_SUCCESS);
  TEST_ASSERT(bmSpriteSheetDraw(&sheet, 0, 100, 100, false, 1, 5, 4) == 1);
  bmVideoWaitFrame();
  const BmPpuState *ppu = bmHalHostGetPpu();
  TEST_ASSERT((ppu->oam[5 * 4] & 0xFF) == 68);
  TEST_ASSERT((ppu->oam[5 * 4 + 1] & 0x1FF) == 92);
  TEST_ASSERT((ppu->oam[5 * 4 + 2] & 0x3FF) == 40);
  TEST_ASSERT((ppu->oam[5 * 4 + 2] >> 12) == 3);
  TEST_ASSERT(ppu->palette[256 + 3 * 16 + 1] != 0);
  TEST_ASSERT(bmSpriteSheetDraw(&sheet, 0, 100, 100, true, 1, 5, 4) == 1);
  bmVideoWaitFrame();
  TEST_ASSERT((ppu->oam[5 * 4 + 1] & 0x1FF) == 92);
  TEST_ASSERT(ppu->oam[5 * 4 + 1] & 0x1000);
  // Off screen: nothing is used.
  TEST_ASSERT(bmSpriteSheetDraw(&sheet, 0, -50, 100, false, 1, 5, 4) == 0);
  BmSpriteBox box = bmSpriteSheetHitbox(&sheet, 0, false), mirrored = bmSpriteSheetHitbox(&sheet, 0, true);
  TEST_ASSERT(box.x == -5 && box.y == -30 && box.w == 10 && box.h == 30);
  TEST_ASSERT(mirrored.x == -5);

  // A chunk cut short is refused instead of read past its end.
  uint32_t size = 0;
  const uint8_t *chunk = bmPackChunkData(&pack, uint32_t(bmSpriteSheetChunk(&pack, 0)), &size);
  TEST_ASSERT(chunk != nullptr && size > 36);
}

int main(int argc, char **argv)
{
  if (argc < 3) return 2;
  s_assets = argv[1];
  s_work = argv[2];
  RUN_TEST(testLoadSaveRoundTrip);
  RUN_TEST(testCook);
  RUN_TEST(testRuntime);
  fs::remove_all(s_work);
  return TEST_RESULT();
}
