// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "vram.h"

#include <algorithm>
#include <cstdio>

namespace bm::editor {

namespace {

const int kObjSize[3][4][2] = {
  {{8, 8}, {16, 16}, {32, 32}, {64, 64}},   // square
  {{16, 8}, {32, 8}, {32, 16}, {64, 32}},   // wide
  {{8, 16}, {8, 32}, {16, 32}, {32, 64}},   // tall
};

uint16_t vram16(const BmPpuState &s, uint32_t offset)
{
  return uint16_t(s.vram[offset] | (s.vram[offset + 1] << 8));
}

std::string hex(uint32_t v, int digits)
{
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%0*X", digits, unsigned(v));
  return buf;
}

int popcount(uint32_t v)
{
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

std::string kb(int bytes)
{
  char buf[32];
  if (bytes % 1024 == 0) std::snprintf(buf, sizeof(buf), "%d KB", bytes / 1024);
  else std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
  return buf;
}

}  // namespace

VramAnalysis analyseVideo(const BmPpuState &s)
{
  VramAnalysis a;
  a.use.assign(kUnits, 0);
  a.unitBank.assign(kUnits, -1);
  uint16_t dispcnt = bmPpuReg(&s, BM_PPU_DISPCNT);
  a.mode = dispcnt & 7;
  a.forcedBlank = (dispcnt & BM_PPU_DISPCNT_FORCED_BLANK) != 0;
  a.obj1D = (dispcnt & BM_PPU_DISPCNT_OBJ_1D) != 0;
  a.objEnabled = (dispcnt & BM_PPU_DISPCNT_OBJ) != 0;

  for (int u = 0; u < kUnits; u++)
    for (int i = 0; i < kUnitBytes; i++)
      if (s.vram[u * kUnitBytes + i] != 0)
      {
        a.use[size_t(u)] |= USE_NONZERO;
        break;
      }

  if (a.mode != 0) a.notes.push_back("Mode " + std::to_string(a.mode) + ": only mode 0 is analysed in detail");

  // Backgrounds: maps and the tiles they reference.
  for (int n = 0; n < 4 && a.mode == 0; n++)
  {
    BgInfo &bg = a.bgs[n];
    bg.index = n;
    uint16_t cnt = bmPpuReg(&s, uint16_t(BM_PPU_BGCNT(n)));
    bg.enabled = (dispcnt & BM_PPU_DISPCNT_BG(n)) != 0;
    bg.priority = cnt & 3;
    bg.charBlock = (cnt >> 2) & 3;
    bg.bpp8 = (cnt & 0x80) != 0;
    bg.screenBlock = (cnt >> 8) & 31;
    int size = cnt >> 14;
    bg.widthTiles = (size & 1) ? 64 : 32;
    bg.heightTiles = (size & 2) ? 64 : 32;
    bg.screenBlocks = (bg.widthTiles / 32) * (bg.heightTiles / 32);
    bg.scrollX = bmPpuReg(&s, uint16_t(BM_PPU_BGHOFS(n))) & 511;
    bg.scrollY = bmPpuReg(&s, uint16_t(BM_PPU_BGVOFS(n))) & 511;
    if (!bg.enabled) continue;

    std::vector<bool> seen(1024, false);
    for (int b = 0; b < bg.screenBlocks; b++)
    {
      int block = bg.screenBlock + b;
      if (block > 31) break;
      for (int u = 0; u < 0x800 / kUnitBytes; u++) a.use[size_t(block * (0x800 / kUnitBytes) + u)] |= uint16_t(1u << n);
      for (int i = 0; i < 1024; i++)
      {
        uint16_t e = vram16(s, uint32_t(block * 0x800 + i * 2));
        int tile = e & 0x3FF;
        int bank = e >> 12;
        uint32_t address = uint32_t(bg.charBlock * 0x4000 + tile * (bg.bpp8 ? 64 : 32));
        if (address >= 0x10000)
        {
          bg.outOfRange++;
          continue;
        }
        if (!seen[size_t(tile)])
        {
          seen[size_t(tile)] = true;
          bg.tilesUsed++;
          bg.highestTile = std::max(bg.highestTile, tile);
        }
        int unit = int(address / kUnitBytes);
        for (int k = 0; k < (bg.bpp8 ? 2 : 1); k++)
        {
          a.use[size_t(unit + k)] |= uint16_t(0x10u << n);
          if (a.unitBank[size_t(unit + k)] < 0) a.unitBank[size_t(unit + k)] = int8_t(bg.bpp8 ? 16 : bank);
        }
        if (!bg.bpp8) bg.banks |= uint16_t(1u << bank);
      }
    }
    a.bgBanks |= bg.bpp8 ? 0xFFFF : bg.banks;
    if (bg.outOfRange)
      a.notes.push_back("BG" + std::to_string(n) + ": " + std::to_string(bg.outOfRange) +
                        " map entries point past background VRAM (they show sprite tiles' memory as garbage)");
  }

  // Sprites.
  bool bitmapMode = a.mode >= 3;
  for (int i = 0; i < 128; i++)
  {
    uint16_t a0 = s.oam[i * 4], a1 = s.oam[i * 4 + 1], a2 = s.oam[i * 4 + 2];
    bool affine = (a0 & 0x100) != 0;
    if (!affine && (a0 & 0x200)) continue;  // disabled
    int shape = (a0 >> 14) & 3;
    if (shape == 3) continue;
    SpriteInfo sp;
    sp.index = i;
    sp.affine = affine;
    sp.width = kObjSize[shape][a1 >> 14][0];
    sp.height = kObjSize[shape][a1 >> 14][1];
    sp.y = a0 & 0xFF;
    sp.x = a1 & 0x1FF;
    if (sp.x >= 240) sp.x -= 512;
    if (sp.y >= 160) sp.y -= 256;
    sp.bpp8 = (a0 & 0x2000) != 0;
    sp.hflip = !affine && (a1 & 0x1000);
    sp.vflip = !affine && (a1 & 0x2000);
    sp.tile = a2 & 0x3FF;
    sp.priority = (a2 >> 10) & 3;
    sp.bank = a2 >> 12;
    bool doubled = affine && (a0 & 0x200);
    int bw = sp.width * (doubled ? 2 : 1), bh = sp.height * (doubled ? 2 : 1);
    sp.onScreen = sp.x + bw > 0 && sp.x < 240 && sp.y + bh > 0 && sp.y < 160;
    a.sprites.push_back(sp);
    if (!a.objEnabled) continue;
    a.objBanks |= sp.bpp8 ? 0xFFFF : uint16_t(1u << sp.bank);

    // Tiles: 1D mapping takes them one after another, 2D from a 32x32 grid.
    int tw = sp.width / 8, th = sp.height / 8, step = sp.bpp8 ? 2 : 1;
    for (int ty = 0; ty < th; ty++)
      for (int tx = 0; tx < tw; tx++)
      {
        int t = a.obj1D ? sp.tile + (ty * tw + tx) * step : sp.tile + ty * 32 + tx * step;
        for (int k = 0; k < step; k++)
        {
          int u = (t + k) & 1023;
          if (bitmapMode && u < 512) continue;  // that half holds the bitmap
          a.use[size_t(kBgUnits + u)] |= USE_OBJ;
          if (a.unitBank[size_t(kBgUnits + u)] < 0) a.unitBank[size_t(kBgUnits + u)] = int8_t(sp.bpp8 ? 16 : sp.bank);
        }
      }

    // Time per line: a regular sprite costs its width, an affine one 2 x width + 10.
    int cost = affine ? 2 * bw + 10 : sp.width;
    for (int y = 0; y < bh; y++)
    {
      int line = ((sp.y + y) % 256 + 256) % 256;
      if (line < BM_PPU_HEIGHT) a.spriteCycles[line] += cost;
    }
  }

  // Overlaps: a unit used as map and as tiles, or as two maps.
  for (int u = 0; u < kUnits; u++)
  {
    uint16_t v = a.use[size_t(u)];
    int maps = popcount(v & USE_MAP);
    bool tiles = (v & USE_TILES) != 0;
    if (maps > 1 || (maps == 1 && tiles))
    {
      a.use[size_t(u)] |= USE_OVERLAP;
      a.overlaps++;
    }
  }
  if (a.overlaps)
    a.notes.push_back(std::to_string(a.overlaps * kUnitBytes) + " bytes of VRAM are used as two things at once (red in the map)");

  for (int i = 1; i < 256; i++)
    if (i % 16 != 0)
    {
      a.bgColours += s.palette[i] != 0;
      a.objColours += s.palette[256 + i] != 0;
    }
  for (int t = 0; t < BM_PPU_LINE_TABLES; t++)
    if (s.lineTables[t].reg != 0xFFFF) a.lineTableReg = s.lineTables[t].reg;

  // Budgets.
  int mapUnits = 0, tileUnits = 0, otherUnits = 0, objUnits = 0, objOther = 0;
  int screenBlocks = 0;
  for (int u = 0; u < kBgUnits; u++)
  {
    uint16_t v = a.use[size_t(u)];
    if (v & USE_MAP) mapUnits++;
    else if (v & USE_TILES) tileUnits++;
    else if (v & USE_NONZERO) otherUnits++;
  }
  for (int b = 0; b < 32; b++)
  {
    bool used = false;
    for (int u = 0; u < 64 && !used; u++) used = (a.use[size_t(b * 64 + u)] & USE_MAP) != 0;
    screenBlocks += used;
  }
  for (int u = kBgUnits; u < kUnits; u++)
  {
    if (a.use[size_t(u)] & USE_OBJ) objUnits++;
    else if (a.use[size_t(u)] & USE_NONZERO) objOther++;
  }
  int onScreen = int(std::count_if(a.sprites.begin(), a.sprites.end(), [](const SpriteInfo &sp) { return sp.onScreen; }));
  int worst = 0, worstLine = 0, linesOver = 0;
  for (int y = 0; y < BM_PPU_HEIGHT; y++)
  {
    if (a.spriteCycles[y] > worst) { worst = a.spriteCycles[y]; worstLine = y; }
    linesOver += a.spriteCycles[y] > kSpriteCyclesPerLine;
  }
  int objBytes = bitmapMode ? 0x4000 : 0x8000;
  a.budgets.push_back({"Background VRAM", (mapUnits + tileUnits) * kUnitBytes, 0x10000, "B",
                       "maps " + kb(mapUnits * kUnitBytes) + ", tiles " + kb(tileUnits * kUnitBytes) +
                           (otherUnits ? ", " + kb(otherUnits * kUnitBytes) + " loaded but not shown" : std::string())});
  a.budgets.push_back({"Screen blocks", screenBlocks, 32, "", "2 KB each, for the maps"});
  a.budgets.push_back({"Sprite VRAM", objUnits * kUnitBytes, objBytes, "B",
                       objOther ? kb(objOther * kUnitBytes) + " loaded but not used by OAM" : std::string()});
  a.budgets.push_back({"OAM", int(a.sprites.size()), 128, "", std::to_string(onScreen) + " on screen"});
  a.budgets.push_back({"BG palette banks", popcount(a.bgBanks), 16, "", std::to_string(a.bgColours) + " colours set"});
  a.budgets.push_back({"OBJ palette banks", popcount(a.objBanks), 16, "", std::to_string(a.objColours) + " colours set"});
  a.budgets.push_back({"Sprite time, worst line", worst, kSpriteCyclesPerLine, "cycles",
                       "line " + std::to_string(worstLine) + (linesOver ? ", " + std::to_string(linesOver) + " lines drop sprites" : std::string())});
  a.budgets.push_back({"Line tables", a.lineTableReg >= 0 ? 1 : 0, BM_PPU_LINE_TABLES, "",
                       a.lineTableReg >= 0 ? "register 0x" + hex(uint32_t(a.lineTableReg), 2) : std::string()});
  if (linesOver) a.notes.push_back(std::to_string(linesOver) + " lines need more sprite time than the hardware has; sprites drop out there");
  return a;
}

std::string VramAnalysis::describeUnit(int unit) const
{
  if (unit < 0 || unit >= kUnits) return {};
  uint32_t offset = uint32_t(unit * kUnitBytes);
  std::string s = "0x" + hex(0x06000000u + offset, 8) + "  char block " + std::to_string(offset / 0x4000);
  uint16_t v = use[size_t(unit)];
  if (unit < kBgUnits)
  {
    s += ", screen block " + std::to_string(offset / 0x800);
    for (int n = 0; n < 4; n++)
    {
      if (v & (1u << n)) s += ", map of BG" + std::to_string(n);
      if (v & (0x10u << n))
      {
        const BgInfo &bg = bgs[n];
        int tile = int((offset - uint32_t(bg.charBlock) * 0x4000) / (bg.bpp8 ? 64 : 32));
        s += ", tile " + std::to_string(tile) + " of BG" + std::to_string(n);
      }
    }
  }
  else
  {
    s += ", sprite tile " + std::to_string(unit - kBgUnits);
    if (v & USE_OBJ) s += " (used by OAM)";
  }
  int bank = unitBank[size_t(unit)];
  if (bank >= 0) s += bank == 16 ? ", 256 colours" : ", palette bank " + std::to_string(bank);
  if (v & USE_OVERLAP) s += "  OVERLAP";
  else if (!(v & (USE_MAP | USE_TILES | USE_OBJ))) s += (v & USE_NONZERO) ? ", holds data nothing shows" : ", empty";
  return s;
}

void renderVram(const BmPpuState &s, const VramAnalysis &a, std::vector<uint8_t> &rgba, int &width, int &height)
{
  width = 32 * 8;
  height = (kUnits / 32) * 8;
  rgba.assign(size_t(width) * height * 4, 0);
  auto put = [&](int x, int y, uint32_t c) {
    uint8_t *p = &rgba[(size_t(y) * width + x) * 4];
    p[0] = uint8_t(c);
    p[1] = uint8_t(c >> 8);
    p[2] = uint8_t(c >> 16);
    p[3] = 255;
  };
  auto expand = [](uint16_t c) {
    auto x = [](unsigned v) { return (v << 3) | (v >> 2); };
    return x(c & 31) | (x((c >> 5) & 31) << 8) | (x((c >> 10) & 31) << 16);
  };
  for (int u = 0; u < kUnits; u++)
  {
    int cx = (u % 32) * 8, cy = (u / 32) * 8;
    const uint8_t *data = &s.vram[size_t(u) * kUnitBytes];
    int bank = a.unitBank[size_t(u)];
    bool obj = u >= kBgUnits;
    uint16_t v = a.use[size_t(u)];
    bool shown = (v & (USE_TILES | USE_OBJ)) != 0;
    if (!(v & USE_NONZERO))
    {
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) put(cx + x, cy + y, ((x / 4 + y / 4) & 1) ? 0x202020 : 0x282828);
      continue;
    }
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
      {
        uint32_t c;
        if (bank == 16)
        {
          // Half of an 8 bpp tile: four rows of 8 pixels, each drawn twice.
          uint8_t idx = data[(y / 2) * 8 + x];
          c = idx ? expand(s.palette[(obj ? 256 : 0) + idx]) : 0x000000;
        }
        else
        {
          uint8_t b = data[y * 4 + x / 2];
          uint8_t idx = (x & 1) ? b >> 4 : b & 15;
          if (shown && bank >= 0) c = idx ? expand(s.palette[(obj ? 256 : 0) + bank * 16 + idx]) : 0x000000;
          else c = uint32_t(idx * 9 + 40) * 0x010101u;  // grey: not shown, so no palette is known
        }
        put(cx + x, cy + y, c);
      }
  }
}

}  // namespace bm::editor
