// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The editor's video memory analysis (editor/vram.h) on hand-made video states.

#include "vram.h"

#include "test.h"

#include <cstring>

using namespace bm::editor;

static BmPpuState s_state;

static void reset()
{
  bmPpuReset(&s_state);
}

// BG0: char block 0, map in screen block 28, 32x32, 4 bpp; the map uses tiles 1-3 in bank 2.
static void oneBackground()
{
  reset();
  bmPpuSetReg(&s_state, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0));
  bmPpuSetReg(&s_state, BM_PPU_BGCNT(0), uint16_t(28 << 8));
  for (int i = 0; i < 1024; i++)
  {
    uint16_t e = uint16_t((2 << 12) | (1 + i % 3));
    s_state.vram[28 * 0x800 + i * 2] = uint8_t(e);
    s_state.vram[28 * 0x800 + i * 2 + 1] = uint8_t(e >> 8);
  }
  std::memset(s_state.vram + 32, 0x11, 3 * 32);  // tiles 1-3
  s_state.palette[2 * 16 + 1] = 0x7FFF;
}

static int budget(const VramAnalysis &a, const std::string &name)
{
  for (const Budget &b : a.budgets)
    if (b.name == name) return b.used;
  return -1;
}

static void testBackground()
{
  oneBackground();
  VramAnalysis a = analyseVideo(s_state);
  TEST_ASSERT(a.bgs[0].enabled && !a.bgs[1].enabled);
  TEST_ASSERT(a.bgs[0].screenBlock == 28 && a.bgs[0].screenBlocks == 1);
  TEST_ASSERT(a.bgs[0].tilesUsed == 3);
  TEST_ASSERT(a.bgs[0].banks == (1u << 2));
  TEST_ASSERT(a.bgBanks == (1u << 2));
  TEST_ASSERT(a.overlaps == 0);
  TEST_ASSERT(budget(a, "Screen blocks") == 1);
  TEST_ASSERT(budget(a, "Background VRAM") == 0x800 + 3 * 32);
  TEST_ASSERT((a.use[1] & USE_TILES) && a.unitBank[1] == 2);
  TEST_ASSERT(a.use[28 * 64] & USE_MAP);
  TEST_ASSERT(a.describeUnit(1).find("tile 1 of BG0") != std::string::npos);
}

static void testOverlap()
{
  // BG1 takes its tiles from char block 3, where BG0's map lies (screen block 28 = 0xE000).
  oneBackground();
  bmPpuSetReg(&s_state, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_BG(1));
  bmPpuSetReg(&s_state, BM_PPU_BGCNT(1), uint16_t((3 << 2) | (29 << 8)));
  for (int i = 0; i < 1024; i++)
  {
    uint16_t e = uint16_t(0x100 + i % 4);  // tiles 256-259 of char block 3 = 0xE000
    s_state.vram[29 * 0x800 + i * 2] = uint8_t(e);
    s_state.vram[29 * 0x800 + i * 2 + 1] = uint8_t(e >> 8);
  }
  VramAnalysis a = analyseVideo(s_state);
  TEST_ASSERT(a.overlaps == 4);
  TEST_ASSERT(!a.notes.empty());
  TEST_ASSERT(a.use[28 * 64] & USE_OVERLAP);
}

static void testSprites()
{
  reset();
  bmPpuSetReg(&s_state, BM_PPU_DISPCNT, BM_PPU_DISPCNT_OBJ | BM_PPU_DISPCNT_OBJ_1D);
  for (int i = 0; i < 128; i++) s_state.oam[i * 4] = 0x0200;  // all disabled
  // Sprite 0: 32x32 at 10,20, tile 8, bank 3. Sprite 1: 64x64 at 0,100 (16 of them on one line
  // are too much for the hardware).
  s_state.oam[0] = 20;
  s_state.oam[1] = uint16_t((2 << 14) | 10);
  s_state.oam[2] = uint16_t((3 << 12) | 8);
  for (int k = 1; k <= 20; k++)
  {
    s_state.oam[k * 4] = 100;
    s_state.oam[k * 4 + 1] = uint16_t(3 << 14);
    s_state.oam[k * 4 + 2] = 100;
  }
  VramAnalysis a = analyseVideo(s_state);
  TEST_ASSERT(a.sprites.size() == 21);
  TEST_ASSERT(a.sprites[0].width == 32 && a.sprites[0].height == 32 && a.sprites[0].x == 10 && a.sprites[0].y == 20);
  TEST_ASSERT(a.objBanks & (1u << 3));
  TEST_ASSERT(a.use[kBgUnits + 8] & USE_OBJ);
  TEST_ASSERT(a.use[kBgUnits + 8 + 15] & USE_OBJ);
  TEST_ASSERT(!(a.use[kBgUnits + 8 + 16] & USE_OBJ));
  TEST_ASSERT(a.spriteCycles[30] == 32);
  TEST_ASSERT(a.spriteCycles[120] == 20 * 64);
  TEST_ASSERT(a.spriteCycles[120] > kSpriteCyclesPerLine);
  bool warned = false;
  for (const std::string &n : a.notes) warned |= n.find("sprites drop out") != std::string::npos;
  TEST_ASSERT(warned);
}

static void testRender()
{
  oneBackground();
  VramAnalysis a = analyseVideo(s_state);
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  renderVram(s_state, a, rgba, w, h);
  TEST_ASSERT(w == 256 && h == (kUnits / 32) * 8);
  // Tile 1, pixel 0: colour 1 of bank 2 = white.
  const uint8_t *p = &rgba[size_t(0 * w + 8) * 4];
  TEST_ASSERT(p[0] == 255 && p[1] == 255 && p[2] == 255);
}

int main()
{
  RUN_TEST(testBackground);
  RUN_TEST(testOverlap);
  RUN_TEST(testSprites);
  RUN_TEST(testRender);
  return TEST_RESULT();
}
