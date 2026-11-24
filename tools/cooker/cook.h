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
  int bgPaletteBanks = 15;     // banks of 16 colours for 4 bpp backgrounds (on the GBA the 16th is the UI's)
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
  /// Sprite files (.bmsprite, sprite-format.md) to cook into the pack.
  std::vector<std::string> sprites;
  /// Script files (.ms, mondskript.md), compiled into the pack's bytecode module.
  std::vector<std::string> scripts;
  /// Songs and sound effects (.bmsong, sound-format.md).
  std::vector<std::string> songs;
  /// Records (.bmdata, data_asset.h) of the structures the scripts declare.
  std::vector<std::string> data;
  /// Fonts (.bmfont, font_asset.h).
  std::vector<std::string> fonts;
  /// Translations (.bmlang, lang_asset.h), and the main language's code and name.
  std::vector<std::string> languages;
  std::string mainLanguage = "en", mainLanguageName = "English";
  /// The world map (.bmworld, world_asset.h); empty: the pack has none.
  std::string world;
  /// The save layouts of earlier versions (.bmschema); empty: none (a save of another layout
  /// is refused).
  std::string schemaHistory;
  /// If set: the scripts are also compiled to native code with this GCC for the profile's
  /// machine (mondskript.md §12) and packed as a MODN chunk next to the bytecode.
  std::string nativeCompiler;
  /// Where native.h is; empty: the engine sources this cooker was built from.
  std::string nativeIncludeDir;
  /// If set: the linked native module with debug information is written here (for GDB).
  std::string nativeElf;
  /// Left out of the pack, their entries kept (flagged absent, pack-format.md §0), so that an
  /// update brings them later (pack sets): the rooms with these IDs, with every chunk only they
  /// use, and every chunk of these kinds (four-character codes, e.g. "LANG").
  std::vector<std::string> absentRooms;
  std::vector<std::string> absentKinds;
  /// If set: the C made from the scripts is written here.
  std::string scriptC;
};

/// Cooks the given rooms. Rooms with the same `area` property share tiles, palettes and the
/// VRAM layout (pack-format.md §2). Returns the pack (empty if the report has errors).
std::vector<uint8_t> cook(const std::vector<tmx::Map> &rooms, const CookOptions &options, Report &report);

}  // namespace bm
