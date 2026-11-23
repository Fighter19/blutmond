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

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using namespace bm;

static std::string s_assets, s_work;

static void testLoadSaveRoundTrip()
{
  sprite::Sprite s = sprite::load(s_assets + "/test_room/player.bmsprite");
  TEST_ASSERT(s.name == "player");
  TEST_ASSERT(s.frames.size() == 28);   // 10 of 16x32, 3 of 32x32 (slash), 4 of 16x48 (lance up, down), 2 of 32x32 (low), 8 of 32x48 (arcs), 1 of 32x32 (lance)
  TEST_ASSERT(s.animations.size() == 12);
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
  TEST_ASSERT(c.frames.size() == 28);
  // A 16x32 frame is one tall hardware sprite of 8 tiles.
  TEST_ASSERT(c.frames[0].pieces.size() == 1);
  TEST_ASSERT(c.frames[0].pieces[0].w == 16 && c.frames[0].pieces[0].h == 32);
  TEST_ASSERT(c.frames[0].pieces[0].dx == -8 && c.frames[0].pieces[0].dy == -32);
  TEST_ASSERT(c.maxPieces == 2);   // a 16x48 frame (the swing up or down): 16x32 and 16x16
  TEST_ASSERT(c.maxLineCost == 32);   // the 32-wide slash frames
  TEST_ASSERT(c.tiles.size() >= 10 * 8);
  TEST_ASSERT(c.palette[0] == 0 && c.palette[1] != 0);
  TEST_ASSERT(c.frames[0].bounds.w > 0 && c.frames[0].bounds.w <= 16);

  // The same picture twice is stored once.
  s.frames.push_back(s.frames[0]);
  Report again;
  sprite::Cooked d = sprite::cook(s, *profileByName("gba"), again);
  TEST_ASSERT(d.tiles.size() == c.tiles.size());
  TEST_ASSERT(d.frames.back().pieces[0].tile == d.frames[0].pieces[0].tile);

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
  TEST_ASSERT(sheet.frameCount == 28 && sheet.animCount == 12);
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

// Hurt, hit and guard boxes (hitboxes.md step 1): in the file (on the sprite for every frame,
// on a frame replacing those of a kind), saved and loaded again, cooked into SPRT version 2,
// read by the runtime, mirrored with the facing; and the cooker's limits.
static void testBoxes()
{
  fs::create_directories(s_work);
  fs::copy_file(s_assets + "/test_room/player.png", s_work + "/player.png", fs::copy_options::overwrite_existing);
  sprite::Sprite s = sprite::load(s_assets + "/test_room/player.bmsprite");
  s.path = s_work + "/boxed.bmsprite";
  s.boxes = {{sprite::BoxKind::Hurt, "", {-5, -28, 10, 28}}};                       // every frame
  s.frames[3].boxes = {{sprite::BoxKind::Hurt, "", {-6, -20, 12, 20}},              // ducked
                       {sprite::BoxKind::Hit, "blade", {6, -18, 26, 6}},
                       {sprite::BoxKind::Hit, "blade", {28, -24, 8, 14}},
                       {sprite::BoxKind::Guard, "shield", {-10, -24, 4, 16}}};
  s.frames[4].boxes = {{sprite::BoxKind::Hit, "blade", {6, -18, 26, 6}}};          // keeps the sprite's hurt box
  TEST_ASSERT(sprite::save(s));
  sprite::Sprite t = sprite::load(s.path);
  TEST_ASSERT(t.boxes == s.boxes && t.frames[3].boxes == s.frames[3].boxes && t.frames[4].boxes == s.frames[4].boxes && t.frames[0].boxes.empty());
  std::vector<sprite::NamedBox> f4 = sprite::frameBoxes(t, t.frames[4]);
  TEST_ASSERT(f4.size() == 2 && f4[0].kind == sprite::BoxKind::Hurt && f4[0].box.h == 28 && f4[1].name == "blade");

  CookOptions options;
  options.profile = *profileByName("gba");
  options.sprites = {s.path};
  std::vector<tmx::Map> maps{tmx::load(s_assets + "/test_room/room.tmx")};
  Report report;
  static std::vector<uint8_t> data;
  data = cook(maps, options, report);
  TEST_ASSERT(!report.hasErrors() && !data.empty());
  BmPack pack;
  TEST_ASSERT(bmPackOpen(&pack, data.data(), uint32_t(data.size()), true) == BM_PACK_OK);
  BmSpriteSheet sheet;
  TEST_ASSERT(bmSpriteSheetFind(&sheet, &pack, "boxed") == BM_SUCCESS);
  TEST_ASSERT(sheet.boxCount == 20 + 4 + 2 + 6 * 2 && sheet.boxNameCount == 2);   // 20 frames with the sprite's box, 4 own, 1 own + the sprite's, 6 arc frames: their blade + the sprite's
  BmSpriteNamedBox b[8];
  TEST_ASSERT(bmSpriteSheetBoxes(&sheet, 0, false, b, 8) == 1 && b[0].kind == BM_BOX_HURT && b[0].box.x == -5 && b[0].box.h == 28 && b[0].name == 0xFFFF);
  TEST_ASSERT(bmSpriteSheetBoxes(&sheet, 3, false, b, 8) == 4);
  TEST_ASSERT(b[0].kind == BM_BOX_HURT && b[0].box.h == 20);
  TEST_ASSERT(b[1].kind == BM_BOX_HIT && b[2].kind == BM_BOX_HIT && b[3].kind == BM_BOX_GUARD);
  TEST_ASSERT(std::string(bmPackString(&pack, b[1].name)) == "blade" && b[1].name == b[2].name);
  TEST_ASSERT(std::string(bmPackString(&pack, b[3].name)) == "shield");
  TEST_ASSERT(bmSpriteSheetBoxes(&sheet, 3, true, b, 8) == 4 && b[1].box.x == -6 - 26 && b[1].box.w == 26);   // mirrored
  TEST_ASSERT(bmSpriteSheetBoxes(&sheet, 4, false, b, 8) == 2 && b[0].box.h == 28 && b[1].kind == BM_BOX_HIT);
  TEST_ASSERT(bmSpriteSheetBoxes(&sheet, 3, false, b, 2) == 4);   // how many there are, filled up to max

  // Limits: 4 of a kind per frame, names for hit and guard boxes, sizes.
  auto problem = [&](const std::vector<sprite::NamedBox> &boxes) {
    sprite::Sprite x = s;
    x.frames[1].boxes = boxes;
    Report r;
    sprite::cook(x, *profileByName("gba"), r);
    for (const ReportEntry &e : r.entries)
      if (e.level == ReportEntry::Level::Error) return e.message;
    return std::string();
  };
  sprite::NamedBox hurt{sprite::BoxKind::Hurt, "", {0, 0, 4, 4}};
  TEST_ASSERT(problem({hurt, hurt, hurt, hurt}).empty());
  TEST_ASSERT(problem({hurt, hurt, hurt, hurt, hurt}).find("more than 4 hurt") != std::string::npos);
  TEST_ASSERT(problem({{sprite::BoxKind::Hit, "", {0, 0, 4, 4}}}).find("needs a name") != std::string::npos);
  TEST_ASSERT(problem({{sprite::BoxKind::Hurt, "", {0, 0, 300, 4}}}).find("at most 255") != std::string::npos);
}

// Palettes (sprite-format.md version 3): swaps and a second image give other colours to the
// same slots; they go through the file and into the chunk with their names.
static void testPalettes()
{
  sprite::Sprite s = sprite::load(s_assets + "/test_room/bat.bmsprite");
  TEST_ASSERT(s.palettes.size() == 1 && s.palettes[0].name == "red" && s.palettes[0].swaps.size() == 3);
  Report report;
  sprite::Cooked c = sprite::cook(s, *profileByName("gba"), report);
  TEST_ASSERT(!report.hasErrors() && c.variants.size() == 1 && c.paletteNames[0] == "red");
  auto bgr = [](int r, int g, int b) {
    auto q = [](int v) { return int(std::lround(v * 31.0 / 255.0)); };
    return uint16_t(q(r) | q(g) << 5 | q(b) << 10);
  };
  int body = -1;
  for (int k = 1; k < 16; k++)
    if (c.palette[size_t(k)] == bgr(0x6b, 0x42, 0x8c)) body = k;
  TEST_ASSERT(body > 0 && c.variants[0][size_t(body)] == bgr(0xb0, 0x28, 0x38));
  // A second image: every pixel's colour inverted; each slot takes its pixels' new colour.
  sprite::PaletteVariant inv;
  inv.name = "inverted";
  inv.image = std::make_shared<Image>(*s.image);
  for (size_t i = 0; i < inv.image->rgba.size(); i += 4)
    for (int ch = 0; ch < 3; ch++) inv.image->rgba[i + size_t(ch)] = uint8_t(255 - inv.image->rgba[i + size_t(ch)]);
  s.palettes.push_back(inv);
  Report r2;
  sprite::Cooked d = sprite::cook(s, *profileByName("gba"), r2);
  TEST_ASSERT(!r2.hasErrors() && d.variants.size() == 2 && d.variants[1][size_t(body)] == bgr(255 - 0x6b, 255 - 0x42, 255 - 0x8c));
  // Into the chunk: two palettes after the default, their names after them.
  std::vector<uint8_t> b = sprite::chunk(s, d, 1, {2}, {}, {10, 11, 12});
  TEST_ASSERT(b[12] == 56 && b[52] == 3);
  uint32_t paletteAt = uint32_t(b[28] | b[29] << 8 | b[30] << 16 | b[31] << 24);
  TEST_ASSERT((b[paletteAt + 32 + body * 2] | b[paletteAt + 32 + body * 2 + 1] << 8) == bgr(0xb0, 0x28, 0x38));
  TEST_ASSERT(b[paletteAt + 96] == 10 && b[paletteAt + 98] == 11 && b[paletteAt + 100] == 12);
  // Saved and loaded again: the palette and its swaps.
  fs::create_directories(s_work);
  fs::copy_file(s_assets + "/test_room/bat.png", s_work + "/bat.png", fs::copy_options::overwrite_existing);
  s.palettes.pop_back();
  s.path = s_work + "/bat_copy.bmsprite";
  TEST_ASSERT(sprite::save(s));
  sprite::Sprite t = sprite::load(s.path);
  TEST_ASSERT(t.palettes.size() == 1 && t.palettes[0].name == "red" && t.palettes[0].swaps == s.palettes[0].swaps);
}

int main(int argc, char **argv)
{
  if (argc < 3) return 2;
  s_assets = argv[1];
  s_work = argv[2];
  RUN_TEST(testLoadSaveRoundTrip);
  RUN_TEST(testCook);
  RUN_TEST(testRuntime);
  RUN_TEST(testBoxes);
  RUN_TEST(testPalettes);
  fs::remove_all(s_work);
  return TEST_RESULT();
}
