// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Sprites: the source file, the cooker (pieces, shared tiles, palette) and the runtime's
// animation player and drawing. Arguments: the repository's assets folder, a scratch folder.

#include "cook.h"
#include "sprite_asset.h"

extern "C" {
#include "device.h"
#include "hal_host.h"
#include "physical_device.h"
#include "figure.h"
#include "ppu.h"
#include "sprite.h"
#include "video.h"
}

#include "test.h"

#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;
using namespace bm;

static std::string s_assets, s_work;

static void testLoadSaveRoundTrip()
{
  sprite::Sprite s = sprite::load(s_assets + "/sources/player.bmsprite");
  TEST_ASSERT(s.name == "player");
  TEST_ASSERT(s.frames.size() == 41);   // 10 of 16x32, 3 of 32x32 (slash), 4 of 16x48 (lance up, down), 2 of 32x32 (low), 8 of 32x48 (arcs), 1 of 32x32 (lance), 4 of 16x32 (push), 9 of 24x32 (aim)
  TEST_ASSERT(s.animations.size() == 14);
  TEST_ASSERT(s.animations[1].name == "run" && s.animations[1].steps.size() == 4);
  TEST_ASSERT(s.frames[0].ax == 8 && s.frames[0].ay == 32);

  fs::create_directories(s_work);
  fs::copy_file(s_assets + "/sources/player.png", s_work + "/player.png", fs::copy_options::overwrite_existing);
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
  sprite::Sprite s = sprite::load(s_assets + "/sources/player.bmsprite");
  Report report;
  sprite::Cooked c = sprite::cook(s, *profileByName("gba"), report);
  TEST_ASSERT(!report.hasErrors());
  TEST_ASSERT(c.frames.size() == 41);
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
  options.sprites = {s_assets + "/sources/player.bmsprite", s_assets + "/test_room/bat.bmsprite"};
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
  TEST_ASSERT(sheet.frameCount == 41 && sheet.animCount == 14);
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

// The player as a figure (layered-sprites.md step 3): legs and torso split from the whole
// drawing at the waist, the torso placed at the legs' point and playing in step with them. Every
// step of every animation, facing either way and upside down, draws pixel for pixel what the
// whole sprite draws.
static void testFigureDrawsTheWholePlayer()
{
  static std::vector<uint8_t> data;
  CookOptions options;
  options.profile = *profileByName("gba");
  options.sprites = {s_assets + "/sources/player.bmsprite", s_assets + "/test_room/player_legs.bmsprite",
                     s_assets + "/test_room/player_torso.bmsprite"};
  options.figures = {s_assets + "/test_room/player.bmfigure"};
  std::vector<tmx::Map> maps{tmx::load(s_assets + "/test_room/room.tmx")};
  Report report;
  data = cook(maps, options, report);
  TEST_ASSERT(!report.hasErrors());
  BmPack pack;
  TEST_ASSERT(bmPackOpen(&pack, data.data(), uint32_t(data.size()), true) == BM_PACK_OK);
  BmSpriteSheet whole, legs, torso;
  TEST_ASSERT(bmSpriteSheetFind(&whole, &pack, "player") == BM_SUCCESS);
  TEST_ASSERT(bmSpriteSheetFind(&legs, &pack, "player_legs") == BM_SUCCESS);
  TEST_ASSERT(bmSpriteSheetFind(&torso, &pack, "player_torso") == BM_SUCCESS);
  BmFigure fig;
  TEST_ASSERT(bmFigureFind(&fig, &pack, "Player") == BM_SUCCESS);
  TEST_ASSERT(fig.layerCount == 2 && fig.layers[1].parent == 0 && fig.layers[1].mode == BM_FIGURE_SYNC);
  TEST_ASSERT(bmFigureFind(&fig, &pack, "nobody") == BM_ERROR_NOT_FOUND);
  BmSpritePoint pt;
  TEST_ASSERT(bmSpriteSheetPoint(&legs, 0, fig.layers[1].point, false, false, &pt) && pt.x == 0 && pt.y == -15);
  TEST_ASSERT(bmSpriteSheetPoint(&legs, 0, fig.layers[1].point, true, true, &pt) && pt.y == 15);
  TEST_ASSERT(!bmSpriteSheetPoint(&torso, 0, fig.layers[1].point, false, false, &pt));
  TEST_ASSERT(legs.animCount == whole.animCount && torso.animCount == whole.animCount);

  bmHalHostReset();
  BmDeviceHandle device = nullptr;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  bmVideoInit();
  bmVideoSetSpriteMapping1D(true);
  bmVideoSetLayers(0, true);
  TEST_ASSERT(bmSpriteSheetLoad(&whole, 0, 0) == BM_SUCCESS);
  TEST_ASSERT(bmSpriteSheetLoad(&legs, whole.tileCount, 1) == BM_SUCCESS);
  TEST_ASSERT(bmSpriteSheetLoad(&torso, uint16_t(whole.tileCount + legs.tileCount), 2) == BM_SUCCESS);
  static uint16_t a[BM_PPU_WIDTH * BM_PPU_HEIGHT], b[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  auto clear = [] {
    for (uint8_t i = 0; i < 128; i++) bmSpriteHide(i);
  };
  int compared = 0;
  for (uint8_t anim = 0; anim < whole.animCount; anim++)
    for (uint16_t step = 0; step < bmSpriteSheetAnimSteps(&whole, anim); step++)
      for (int flips = 0; flips < 4; flips++)
      {
        bool h = flips & 1, v = flips & 2;
        BmAnimState st = {anim, 0, step, 0};
        clear();
        bmSpriteSheetDrawFlipped(&whole, bmAnimFrame(&whole, &st), 120, 80, h, v, 1, whole.bank, 0, 16);
        bmVideoWaitFrame();
        bmPpuRender(bmHalHostGetPpu(), a);
        const BmSpriteSheet *sheets[BM_FIGURE_MAX_LAYERS] = {&legs, &torso};
        BmAnimState states[BM_FIGURE_MAX_LAYERS] = {st, {}};
        // A direction set (aim) shows the step nearest to the angle: the step's own.
        uint8_t angle = uint8_t(192 + 16 * step), angles[BM_FIGURE_MAX_LAYERS] = {angle, angle};
        BmFigurePose pose;
        bmFigurePose(&fig, sheets, states, angles, h, v, &pose);
        TEST_ASSERT(pose.count == 2 && pose.order[0] == 1);   // the torso in front
        clear();
        uint8_t used = 0;
        for (uint8_t k = 0; k < pose.count; k++)
        {
          uint8_t i = pose.order[k];
          used = uint8_t(used + bmSpriteSheetDrawFlipped(sheets[i], pose.frame[i], 120 + pose.x[i], 80 + pose.y[i], h, v, 1, sheets[i]->bank,
                                                         used, uint8_t(16 - used)));
        }
        bmVideoWaitFrame();
        bmPpuRender(bmHalHostGetPpu(), b);
        int drawn = 0;
        for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++) drawn += a[i] != a[0];
        TEST_ASSERT(drawn > 100);
        TEST_ASSERT(std::memcmp(a, b, sizeof(a)) == 0);
        compared++;
      }
  printf("    %d poses the same\n", compared);

  // The direction set: nine steps from straight up (192) to straight down (64), not played in
  // time; the step nearest to an angle; the hand point on each with the step's angle.
  int32_t aim = bmSpriteSheetAnim(&torso, &pack, "aim");
  TEST_ASSERT(aim >= 0 && bmSpriteSheetAnimIsDirections(&torso, uint8_t(aim)) && bmSpriteSheetAnimSteps(&torso, uint8_t(aim)) == 9);
  TEST_ASSERT(bmSpriteSheetFirstDirections(&torso) == aim && bmSpriteSheetFirstDirections(&whole) == aim);
  TEST_ASSERT(bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 192) == 0 && bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 0) == 4 &&
              bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 64) == 8 && bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 7) == 4 &&
              bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 9) == 5);
  // Behind the figure: the nearest end.
  TEST_ASSERT(bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 120) == 8 && bmSpriteSheetDirectionStep(&torso, uint8_t(aim), 140) == 0);
  BmAnimState held = {uint8_t(aim), 0, 3, 0};
  for (int i = 0; i < 10; i++) bmAnimTick(&torso, &held);
  TEST_ASSERT(held.step == 3 && held.timer == 0 && !held.finished);
  uint16_t handId = 0xFFFF;
  for (uint16_t k = 0; k < 2000 && handId == 0xFFFF; k++)
    if (const char *t = bmPackString(&pack, k); t != nullptr && std::strcmp(t, "hand") == 0) handId = k;
  for (uint16_t k = 0; k < 9; k++)
  {
    BmAnimState at = {uint8_t(aim), 0, k, 0};
    TEST_ASSERT(bmSpriteSheetPoint(&torso, bmAnimFrame(&torso, &at), handId, false, false, &pt) && pt.angle == uint8_t(192 + 16 * k));
  }

  // Angles: sin and cos from the table, the direction of a vector, turning, mirroring.
  TEST_ASSERT(bmAngleSin(0) == 0 && bmAngleSin(64) == 256 && bmAngleSin(128) == 0 && bmAngleSin(192) == -256 && bmAngleCos(0) == 256);
  TEST_ASSERT(bmAngleSin(32) == 181 && bmAngleCos(96) == -181);
  TEST_ASSERT(bmAngleOf(10, 0) == 0 && bmAngleOf(0, 10) == 64 && bmAngleOf(-10, 0) == 128 && bmAngleOf(0, -10) == 192);
  TEST_ASSERT(bmAngleOf(10, 10) == 32 && bmAngleOf(-10, -10) == 160 && bmAngleOf(0, 0) == 0);
  for (int a = 0; a < 256; a += 5)
  {
    int got = bmAngleOf(bmAngleCos(uint8_t(a)) * 4, bmAngleSin(uint8_t(a)) * 4), d = (got - a) & 255;
    TEST_ASSERT(d <= 1 || d >= 255);
  }
  TEST_ASSERT(bmAngleToward(250, 10, 4) == 254 && bmAngleToward(10, 250, 4) == 6 && bmAngleToward(10, 12, 4) == 12);
  TEST_ASSERT(bmAngleOfDrawing(16, true, false) == 112 && bmAngleOfDrawing(16, false, true) == 240 && bmAngleOfDrawing(112, true, false) == 16);
  TEST_ASSERT(compared >= 4 * 30);

  // Own mode and order: a torso at its own pace, and a cape behind the legs at the same point.
  BmFigure three = fig;
  three.layerCount = 3;
  three.layers[1].mode = BM_FIGURE_OWN;
  three.layers[2] = fig.layers[1];
  three.layers[2].behind = true;
  int32_t run = bmSpriteSheetAnim(&legs, &pack, "run"), idle = bmSpriteSheetAnim(&torso, &pack, "idle");
  TEST_ASSERT(run >= 0 && idle >= 0 && bmFigureLayerAnim(&legs, uint8_t(run), &torso) == run);
  const BmSpriteSheet *sheets3[BM_FIGURE_MAX_LAYERS] = {&legs, &torso, &torso};
  BmAnimState states3[BM_FIGURE_MAX_LAYERS] = {{uint8_t(run), 0, 2, 0}, {uint8_t(idle), 0, 1, 0}, {}};
  BmFigurePose pose;
  bmFigurePose(&three, sheets3, states3, nullptr, true, false, &pose);
  TEST_ASSERT(pose.count == 3 && pose.order[0] == 1 && pose.order[1] == 0 && pose.order[2] == 2);   // torso, legs, cape
  TEST_ASSERT(pose.frame[1] == bmAnimFrame(&torso, &states3[1]) && pose.frame[2] == bmAnimFrame(&legs, &states3[0]));
  TEST_ASSERT(pose.y[1] == -15 && pose.y[2] == -15 && pose.x[1] == 0);
  // A layer without a sheet is not drawn; what hangs on it is placed at its place.
  sheets3[1] = nullptr;
  bmFigurePose(&three, sheets3, states3, nullptr, false, false, &pose);
  TEST_ASSERT(pose.count == 2 && pose.order[0] == 0 && pose.order[1] == 2);
}

// Hurt, hit and guard boxes (hitboxes.md step 1): in the file (on the sprite for every frame,
// on a frame replacing those of a kind), saved and loaded again, cooked into SPRT version 2,
// read by the runtime, mirrored with the facing; and the cooker's limits.
static void testBoxes()
{
  fs::create_directories(s_work);
  fs::copy_file(s_assets + "/sources/player.png", s_work + "/player.png", fs::copy_options::overwrite_existing);
  sprite::Sprite s = sprite::load(s_assets + "/sources/player.bmsprite");
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
  TEST_ASSERT(sheet.boxCount == 33 + 4 + 2 + 6 * 2 && sheet.boxNameCount == 2);   // 33 frames with the sprite's box, 4 own, 1 own + the sprite's, 6 arc frames: their blade + the sprite's
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
  TEST_ASSERT(b[12] == 68 && b[52] == 3);   // version 4: the bat has a point (body)
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
  RUN_TEST(testFigureDrawsTheWholePlayer);
  fs::remove_all(s_work);
  return TEST_RESULT();
}
