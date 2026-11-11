// SPDX-License-Identifier: BSD-2-Clause OR MIT
// What a video state (BmPpuState: the editor's preview or a snapshot from the running game)
// has loaded and how much of the hardware it takes: backgrounds and their maps and tiles,
// sprites, palette banks, the line table, overlaps and free space (docs/PLAN.md section 16).
// Works on the registers, maps and OAM alone, so it describes any snapshot, whoever made it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "ppu.h"
}

namespace bm::editor {

/// Video memory in 32-byte units (one 4 bpp tile, half an 8 bpp tile).
constexpr int kUnitBytes = 32;
constexpr int kBgUnits = 0x10000 / kUnitBytes;    // char blocks 0-3
constexpr int kObjUnits = 0x8000 / kUnitBytes;    // char blocks 4-5
constexpr int kUnits = kBgUnits + kObjUnits;

/// What a unit of VRAM is used for (bits).
enum UnitUse : uint16_t {
  USE_MAP = 0x000F,        // bit n: map of BG n
  USE_TILES = 0x00F0,      // bit 4+n: tiles shown by BG n
  USE_OBJ = 0x0100,        // sprite tiles of an enabled OAM entry
  USE_NONZERO = 0x0200,    // holds data (not all zero)
  USE_OVERLAP = 0x0400,    // used for two things at once
};

struct BgInfo
{
  int index = 0;
  bool enabled = false;
  int charBlock = 0, screenBlock = 0, screenBlocks = 1;
  int widthTiles = 32, heightTiles = 32;
  bool bpp8 = false;
  int priority = 0;
  int scrollX = 0, scrollY = 0;
  int tilesUsed = 0;          // different tiles the map references
  int highestTile = 0;
  uint16_t banks = 0;         // palette banks referenced (4 bpp)
  int outOfRange = 0;         // entries pointing past background VRAM
};

struct SpriteInfo
{
  int index = 0;
  int x = 0, y = 0, width = 8, height = 8;
  int tile = 0, bank = 0, priority = 0;
  bool bpp8 = false, affine = false, hflip = false, vflip = false;
  bool onScreen = false;
};

struct Budget
{
  std::string name;
  int used = 0, total = 0;
  std::string unit;           // "B", "KB", "", ...
  std::string detail;
};

struct VramAnalysis
{
  int mode = 0;
  bool forcedBlank = false, obj1D = false, objEnabled = false;
  BgInfo bgs[4];
  std::vector<SpriteInfo> sprites;            // enabled OAM entries
  std::vector<uint16_t> use;                  // per unit (kUnits)
  std::vector<int8_t> unitBank;               // bank a unit is shown with (-1: unknown, 16: 8 bpp)
  uint16_t bgBanks = 0, objBanks = 0;         // palette banks in use
  int bgColours = 0, objColours = 0;          // non-zero palette entries
  int lineTableReg = -1;                      // register driven by the line table, or -1
  int spriteCycles[BM_PPU_HEIGHT] = {};       // OBJ rendering cost per line
  int overlaps = 0;
  std::vector<Budget> budgets;
  std::vector<std::string> notes;             // warnings in words

  /// Describes one unit for the hover text.
  std::string describeUnit(int unit) const;
};

VramAnalysis analyseVideo(const BmPpuState &state);

/// All of VRAM as an RGBA8 picture of 8x8 tiles, 32 per row (BG units first, then OBJ),
/// each drawn with the bank it is used with; unused data is grey.
void renderVram(const BmPpuState &state, const VramAnalysis &a, std::vector<uint8_t> &rgba, int &width, int &height);

/// Cycles per line the GBA has for sprites (H-blank interval not free).
constexpr int kSpriteCyclesPerLine = 1210;

}  // namespace bm::editor
