// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Cooks Tiled rooms into a pack for one platform profile (docs/spec/asset-model.md,
// room-format.md, pack-format.md).
#pragma once

#include "tmx.h"

extern "C" {
#include "pack.h"
}

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bm {

enum class TileFormat { GbaLinear4bpp, SnesPlanar4bpp };
enum class MapFormat { Gba, Snes };

struct Profile
{
  std::string name;
  uint32_t fourcc = 0;
  int bgPaletteBanks = 16;     // banks of 16 colours for 4 bpp backgrounds
  int maxTiles = 1024;         // tile numbers a map entry can address
  int maxLayers4bpp = 4;       // backgrounds with 16-colour tiles
  TileFormat tileFormat = TileFormat::GbaLinear4bpp;
  MapFormat mapFormat = MapFormat::Gba;
};

/// "gba", "snes" or "host" (the GBA's limits, for the PC and the browser).
std::optional<Profile> profileByName(const std::string &name);

struct ReportEntry
{
  enum class Level { Info, Warning, Error } level = Level::Info;
  std::string kind;   // colour-merged, palette-split, layer-dropped, does-not-fit, ...
  std::string asset;  // file and location
  std::string message;
};

struct Report
{
  std::string profile;
  std::vector<ReportEntry> entries;

  void info(const std::string &kind, const std::string &asset, const std::string &message);
  void warning(const std::string &kind, const std::string &asset, const std::string &message);
  void error(const std::string &kind, const std::string &asset, const std::string &message);
  bool hasErrors() const;
  std::string text() const;
};

/// What the editor needs to trace cooked data back to its sources.
struct CookInfo
{
  /// Hardware colour (BGR555) -> (source image, source colour 0xRRGGBB) that became it.
  std::map<uint16_t, std::set<std::pair<std::string, uint32_t>>> colourSources;
  /// Source colours replaced by an override: (image, source colour) -> replacement.
  std::map<std::pair<std::string, uint32_t>, uint32_t> overridden;
};

struct CookOptions
{
  Profile profile;
  BmPackLayout layout = BM_PACK_LAYOUT_ROM;
  /// Optional 3x3 matrix applied to colours (0..1 RGB) before converting to the profile's
  /// format, e.g. to compensate for the GBA screen. Row major.
  std::optional<std::array<float, 9>> colorMatrix;
  /// Filled when given.
  CookInfo *pInfo = nullptr;
};

/// Cooks the given rooms. Rooms with the same `area` property share tiles, palettes and the
/// VRAM layout (pack-format.md §2). Returns the pack (empty if the report has errors).
std::vector<uint8_t> cook(const std::vector<tmx::Map> &rooms, const CookOptions &options, Report &report);

}  // namespace bm
