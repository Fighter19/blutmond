// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "data_asset.h"
#include "cook.h"
#include <fstream>

#include "aot.h"
#include "mondskript.h"
#include "native_module.h"
#include "overrides.h"
#include "song_asset.h"
extern "C" {
#include "sound.h"
}
#include "font_asset.h"
#include "lang_asset.h"
#include "sprite_asset.h"
#include "world_asset.h"
#include "figure_asset.h"
#include "light_bake.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <stdexcept>

namespace bm {

// --- Profiles and report ------------------------------------------------------------------

std::optional<Profile> profileByName(const std::string &name)
{
  if (name == "gba" || name == "host")
  {
    Profile p;
    p.name = name;
    p.fourcc = name == "gba" ? BM_FOURCC('G', 'B', 'A', ' ') : BM_FOURCC('H', 'O', 'S', 'T');
    return p;
  }
  if (name == "snes")
  {
    // Mode 1: two 16-colour backgrounds (BG1, BG2) with 8 palettes, plus a 4-colour BG3.
    Profile p;
    p.name = name;
    p.fourcc = BM_FOURCC('S', 'N', 'E', 'S');
    p.bgPaletteBanks = 8;
    p.maxLayers4bpp = 2;
    p.tileFormat = TileFormat::SnesPlanar4bpp;
    p.mapFormat = MapFormat::Snes;
    return p;
  }
  return std::nullopt;
}

void Report::info(const std::string &kind, const std::string &asset, const std::string &message)
{
  entries.push_back({ReportEntry::Level::Info, kind, asset, message});
}
void Report::warning(const std::string &kind, const std::string &asset, const std::string &message)
{
  entries.push_back({ReportEntry::Level::Warning, kind, asset, message});
}
void Report::error(const std::string &kind, const std::string &asset, const std::string &message)
{
  entries.push_back({ReportEntry::Level::Error, kind, asset, message});
}
bool Report::hasErrors() const
{
  return std::any_of(entries.begin(), entries.end(), [](const ReportEntry &e) { return e.level == ReportEntry::Level::Error; });
}
std::string Report::text() const
{
  std::ostringstream out;
  out << "profile " << profile << "\n";
  for (const ReportEntry &e : entries)
  {
    const char *level = e.level == ReportEntry::Level::Error ? "error" : e.level == ReportEntry::Level::Warning ? "warning" : "info";
    out << level << " " << e.kind << " " << e.asset << ": " << e.message << "\n";
  }
  return out.str();
}

namespace {

// --- Binary output ------------------------------------------------------------------------

struct Bytes
{
  std::vector<uint8_t> data;
  void u8(uint32_t v) { data.push_back(uint8_t(v)); }
  void u16(uint32_t v) { u8(v); u8(v >> 8); }
  void u32(uint32_t v) { u16(v); u16(v >> 16); }
  void s16(int32_t v) { u16(uint32_t(v) & 0xFFFF); }
  void s32(int32_t v) { u32(uint32_t(v)); }
  void put16(size_t at, uint32_t v) { data[at] = uint8_t(v); data[at + 1] = uint8_t(v >> 8); }
  void align4() { while (data.size() % 4) data.push_back(0); }
};

int32_t toFix(double v)
{
  return int32_t(std::lround(v * 256.0));
}

uint32_t add(BmPackWriter &w, uint32_t id, BmChunkCategory category, uint16_t group, const Bytes &b)
{
  return bmPackWriterAdd(&w, id, category, group, b.data.data(), uint32_t(b.data.size()));
}

}  // namespace

// The CCOR chunk (pack-format.md): u8 version 1, u8 screen (1: AGB-001), u16 0, s16 matrix[9]
// (4096 = 1.0, row major), u8 curve[32]. The AGB-001's values are those of the gba-colors
// shader by Pokefan531: its screen mixes the channels by `mix` (columns: what red, green and
// blue add to each channel); the inverse undoes that, then the curve brightens the darker
// shades the screen swallows.
std::vector<uint8_t> colorCorrectionChunk(CookOptions::ColorCorrection correction)
{
  if (correction == CookOptions::ColorCorrection::None) return {};
  const double mix[9] = {0.82, 0.24, -0.06, 0.125, 0.665, 0.21, 0.195, 0.075, 0.73};
  static const uint8_t curve[32] = {0, 7, 9, 11, 12, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 22,
                                    23, 24, 24, 25, 25, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31};
  const double *m = mix;
  double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
  double inv[9] = {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                   (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                   (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
  Bytes b;
  b.u8(1);
  b.u8(1);
  b.u16(0);
  for (int row = 0; row < 3; row++)
  {
    // Each row adds up to 1.0 (as the screen's do): a grey stays the same grey before the curve.
    int32_t a = int32_t(std::lround(inv[row * 3] * 4096)), c = int32_t(std::lround(inv[row * 3 + 1] * 4096)),
            d = int32_t(std::lround(inv[row * 3 + 2] * 4096));
    int32_t v[3] = {a, c, d};
    v[row] += 4096 - (a + c + d);
    for (int32_t x : v) b.s16(x);
  }
  for (uint8_t c : curve) b.u8(c);
  return b.data;
}

namespace {

// The MIGR chunk (docs/spec/state.md §7) from a schema history:
//   schema 0x1234abcd "label"
//    game.player.x fix 1          the fields a save game of that layout holds, in its order
//   end
//   rename game.hp game.health     a field called so before is called so now
// The chunk: u16 layouts, u16 0; per layout u32 schema hash, u16 fields, u8 flag mask (1:
// save), u8 0, then per field u32 FNV-1a of "block.field" (as named now), u8 type, u8 0, u16 count.
Bytes migrationChunk(const std::string &path, Report &report)
{
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  static const std::map<std::string, uint8_t> types = {{"u8", 0}, {"s8", 1}, {"u16", 2}, {"s16", 3}, {"u32", 4}, {"s32", 5}, {"bool", 6}, {"fix", 7}};
  struct Field
  {
    std::string name;
    uint8_t type;
    uint16_t count;
  };
  struct Layout
  {
    uint32_t hash;
    std::vector<Field> fields;
  };
  std::vector<Layout> layouts;
  std::map<std::string, std::string> renames;
  std::string line;
  int number = 0;
  bool inside = false;
  auto fail = [&](const std::string &why) { throw std::runtime_error(path + ":" + std::to_string(number) + ": " + why); };
  while (std::getline(in, line))
  {
    number++;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream words(line);
    std::string word;
    if (!(words >> word) || word[0] == '#') continue;
    if (word == "schema")
    {
      std::string hash;
      words >> hash;
      Layout l;
      try
      {
        l.hash = uint32_t(std::stoul(hash, nullptr, 16));
      }
      catch (const std::exception &)
      {
        fail("'" + hash + "' is not a schema hash");
      }
      layouts.push_back(l);
      inside = true;
    }
    else if (word == "end") inside = false;
    else if (word == "rename")
    {
      std::string from, to;
      if (!(words >> from >> to)) fail("rename old new");
      renames[from] = to;
    }
    else if (inside)
    {
      std::string type;
      unsigned count = 0;
      if (!(words >> type >> count) || !types.count(type) || count == 0 || count > 0xFFFF) fail("expected: block.field type count");
      layouts.back().fields.push_back({word, types.at(type), uint16_t(count)});
    }
    else fail("'" + word + "' outside a schema");
  }
  Bytes b;
  b.u16(uint32_t(layouts.size()));
  b.u16(0);
  for (const Layout &l : layouts)
  {
    b.u32(l.hash);
    b.u16(uint32_t(l.fields.size()));
    b.u8(1);
    b.u8(0);
    for (const Field &f : l.fields)
    {
      // Renamed since: by the name it has now. Repeated renames are followed.
      std::string name = f.name;
      for (int k = 0; k < 16 && renames.count(name); k++) name = renames.at(name);
      uint32_t h = 2166136261u;
      for (char c : name) h = (h ^ uint8_t(c)) * 16777619u;
      b.u32(h);
      b.u8(f.type);
      b.u8(0);
      b.u16(f.count);
    }
  }
  report.info("migration", path, std::to_string(layouts.size()) + " earlier save layouts, " + std::to_string(b.data.size()) + " bytes");
  return b;
}

// Level data that is the same as an earlier room's (rooms made from the same map: the debug
// rooms) is not written again: the room refers to the earlier chunk, which stays in the group
// of the room that came first (pack-format.md §2).
struct SharedChunks
{
  std::map<std::pair<uint32_t, std::vector<uint8_t>>, uint32_t> index;
  uint32_t savedBytes = 0;
  uint32_t add(BmPackWriter &w, uint32_t id, BmChunkCategory category, uint16_t group, const Bytes &b)
  {
    auto key = std::make_pair(id, b.data);
    auto it = index.find(key);
    if (it != index.end())
    {
      savedBytes += uint32_t(b.data.size());
      return it->second;
    }
    uint32_t chunk = bm::add(w, id, category, group, b);
    index[key] = chunk;
    return chunk;
  }
};

class StringTable
{
public:
  uint16_t id(const std::string &s)
  {
    auto it = index_.find(s);
    if (it != index_.end()) return it->second;
    uint16_t i = uint16_t(strings_.size());
    strings_.push_back(s);
    index_[s] = i;
    return i;
  }
  /// The id of a string already in the table, or -1.
  int find(const std::string &s) const
  {
    auto it = index_.find(s);
    return it != index_.end() ? it->second : -1;
  }
  Bytes chunk() const
  {
    Bytes b;
    b.u16(uint32_t(strings_.size()));
    b.u16(0);
    uint32_t offset = 4 + 4 * uint32_t(strings_.size());
    for (const std::string &s : strings_)
    {
      b.u32(offset);
      offset += uint32_t(s.size()) + 1;
    }
    for (const std::string &s : strings_)
    {
      b.data.insert(b.data.end(), s.begin(), s.end());
      b.u8(0);
    }
    return b;
  }

private:
  std::vector<std::string> strings_;
  std::map<std::string, uint16_t> index_;
};

// --- Colours and tiles --------------------------------------------------------------------

constexpr uint16_t TRANSPARENT = 0xFFFF;
using Tile = std::array<uint16_t, 64>;  // BGR555 per pixel or TRANSPARENT

enum Flip { FLIP_NONE = 0, FLIP_H = 1, FLIP_V = 2, FLIP_HV = 3 };

Tile flipped(const Tile &t, int flip)
{
  Tile out;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
    {
      int sx = (flip & FLIP_H) ? 7 - x : x;
      int sy = (flip & FLIP_V) ? 7 - y : y;
      out[y * 8 + x] = t[sy * 8 + sx];
    }
  return out;
}

bool isEmpty(const Tile &t)
{
  return std::all_of(t.begin(), t.end(), [](uint16_t c) { return c == TRANSPARENT; });
}

struct ColorConverter
{
  std::optional<std::array<float, 9>> matrix;
  std::map<uint16_t, std::set<uint32_t>> sources;  // BGR555 -> distinct 24-bit colours
  std::string profile;
  Report *report = nullptr;
  CookInfo *info = nullptr;
  std::map<std::string, AssetOverrides> overrides;  // per image, loaded on first use
  std::map<std::string, std::set<uint32_t>> overridesUsed;

  const AssetOverrides &overridesOf(const std::string &asset)
  {
    auto it = overrides.find(asset);
    if (it != overrides.end()) return it->second;
    AssetOverrides o;
    try
    {
      o = loadOverrides(asset);
    }
    catch (const std::exception &e)
    {
      if (report) report->error("does-not-fit", overridesPath(asset), e.what());
    }
    return overrides[asset] = o;
  }

  uint16_t convert(const uint8_t *rgba, const std::string &asset = std::string())
  {
    // Fully or mostly transparent pixels, and magenta as the classic colour key.
    if (rgba[3] < 128 || (rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 255)) return TRANSPARENT;
    uint32_t source = uint32_t(rgba[0]) << 16 | uint32_t(rgba[1]) << 8 | rgba[2];
    float r = rgba[0] / 255.0f, g = rgba[1] / 255.0f, b = rgba[2] / 255.0f;
    uint32_t replacement = 0;
    bool overridden = !asset.empty() && overridesOf(asset).colourFor(profile, source, &replacement);
    if (overridden)
    {
      // A hand-made colour: used as it is, without the colour matrix.
      r = float((replacement >> 16) & 255) / 255.0f;
      g = float((replacement >> 8) & 255) / 255.0f;
      b = float(replacement & 255) / 255.0f;
      overridesUsed[asset].insert(source);
      if (info) info->overridden[{asset, source}] = replacement;
    }
    else if (matrix)
    {
      const auto &m = *matrix;
      float nr = m[0] * r + m[1] * g + m[2] * b;
      float ng = m[3] * r + m[4] * g + m[5] * b;
      float nb = m[6] * r + m[7] * g + m[8] * b;
      r = std::clamp(nr, 0.0f, 1.0f);
      g = std::clamp(ng, 0.0f, 1.0f);
      b = std::clamp(nb, 0.0f, 1.0f);
    }
    auto q = [](float v) { return uint16_t(std::lround(v * 31.0f)); };
    uint16_t c = uint16_t(q(r) | (q(g) << 5) | (q(b) << 10));
    sources[c].insert(source);
    if (info && !asset.empty()) info->colourSources[c].insert({asset, source});
    return c;
  }
};

// Brightness for sorting palette entries (dark first), as SceneCompiler did.
int luminance(uint16_t c)
{
  int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  return r * 299 + g * 587 + b * 114;
}

// --- Collision ----------------------------------------------------------------------------

const std::map<std::string, int> kShapes = {
  {"empty", 0}, {"full", 1}, {"half_top", 2}, {"half_bottom", 3}, {"half_left", 4}, {"slope_1_1", 5},
  {"slope_1_2_a", 6}, {"slope_1_2_b", 7}, {"slope_1_4_a", 8}, {"slope_1_4_b", 9}, {"slope_1_4_c", 10},
  {"slope_1_4_d", 11}, {"slope_2_1_low", 12}, {"slope_2_1_high", 13}, {"custom", 14},
};
const std::map<std::string, int> kMaterials = {
  {"normal", 0}, {"water", 1}, {"damage", 2}, {"death", 3}, {"conveyor", 4}, {"ice", 5},
  {"ladder", 6}, {"bounce", 7}, {"sticky", 8},
};

enum RoleId { ROLE_BACKGROUND = 0, ROLE_MAIN = 1, ROLE_FOREGROUND = 2 };

// --- Per area: shared tiles and palettes --------------------------------------------------

struct TileRef
{
  uint16_t unique = 0;
  int flip = FLIP_NONE;
};

struct GraphicsLayer
{
  const tmx::Layer *layer = nullptr;
  RoleId role = ROLE_MAIN;
  int width8 = 0, height8 = 0;      // in 8x8 cells
  std::vector<TileRef> cells;
  // A room lit in mode `layer` (lighting.md): per pixel 0 transparent, 1 opaque and unlit, 2 lit
  // (with its normal).
  std::vector<uint8_t> pixelState;
  std::vector<light::Vec3> normals;
};

struct RoomWork
{
  const tmx::Map *map = nullptr;
  std::string id;
  std::vector<GraphicsLayer> layers;
  light::Light light;               // mode None: not lit
  light::Solid solid;               // for its shadows
};

struct AreaWork
{
  std::string name;
  std::vector<RoomWork *> rooms;
  std::vector<Tile> uniqueTiles;
  std::map<Tile, uint16_t> tileIndex;
  std::vector<std::string> firstUse;  // where each unique tile was first seen
  std::vector<int> bankOf;
  std::vector<std::vector<uint16_t>> banks;  // colours per bank (index 0 = transparent)
  // Lit in mode `palette` (lighting.md): colours 0x8000 and up stand for (colour, light level).
  std::vector<std::pair<uint16_t, uint8_t>> lit;
  std::map<uint32_t, uint16_t> litIndex;
  // Tiles stored as they are (palette indices), after the area's: the light layer's.
  std::vector<std::array<uint8_t, 64>> rawTiles;
  // Mode `palette`: how far the levels are from each pixel's true light (for the report).
  double litError = 0;
  size_t litPixels = 0;
  // What lights that flicker apart would need (for the report): each pixel's colour and its
  // level from every light; the tiles of those and the different entries.
  std::set<std::vector<uint32_t>> apartTiles;
  std::set<uint32_t> apartEntries;

  uint16_t litColour(uint16_t colour, int level)
  {
    if (level <= 0) return colour;
    uint32_t key = uint32_t(colour) << 8 | uint32_t(level);
    auto it = litIndex.find(key);
    if (it != litIndex.end()) return it->second;
    uint16_t id = uint16_t(0x8000 + lit.size());
    lit.emplace_back(colour, uint8_t(level));
    litIndex[key] = id;
    return id;
  }
  uint16_t albedo(uint16_t c) const { return c >= 0x8000 && c != 0xFFFF ? lit[c - 0x8000].first : c; }
  uint8_t level(uint16_t c) const { return c >= 0x8000 && c != 0xFFFF ? lit[c - 0x8000].second : 0; }
};

// A lit tile with more than 15 (colour, level) pairs: the pairs used least go to the nearest
// level of their colour the tile has, else to the colour unlit, until 15 are left.
void fitLitTile(Tile &t, AreaWork &area)
{
  for (;;)
  {
    std::map<uint16_t, int> count;
    for (uint16_t c : t)
      if (c != TRANSPARENT) count[c]++;
    if (count.size() <= 15) return;
    uint16_t least = 0;
    int fewest = 1 << 30;
    for (const auto &[c, n] : count)
      if (c >= 0x8000 && n < fewest) fewest = n, least = c;
    if (fewest == 1 << 30) return;   // only plain colours: reported later
    uint16_t a = area.albedo(least);
    int lv = area.level(least), bestD = 99;
    uint16_t to = a;
    for (const auto &[c, n] : count)
    {
      if (c == least || area.albedo(c) != a) continue;
      int d = std::abs(int(area.level(c)) - lv);
      if (d < bestD) bestD = d, to = c;
    }
    for (uint16_t &c : t)
      if (c == least) c = to;
  }
}

std::string location(const tmx::Map &map, const tmx::Layer &layer, int x, int y)
{
  return map.path + " layer '" + layer.name + "' at " + std::to_string(x) + "," + std::to_string(y);
}

Bytes collisionChunk(const tmx::Map &map, Report &report);

// Cuts every graphics layer of a room into 8x8 tiles and deduplicates them (with flips)
// against the area's tiles.
void collectTiles(RoomWork &room, AreaWork &area, ColorConverter &colors, const Profile &profile, Report &report)
{
  const tmx::Map &map = *room.map;
  const light::Light &lightOf = room.light;
  int layers4bpp = 0;
  for (const tmx::Layer &layer : map.layers)
  {
    std::string role = layer.role();
    RoleId roleId;
    if (role == "background") roleId = ROLE_BACKGROUND;
    else if (role == "main") roleId = ROLE_MAIN;
    else if (role == "foreground") roleId = ROLE_FOREGROUND;
    else continue;

    if (++layers4bpp > profile.maxLayers4bpp)
    {
      report.warning("layer-dropped", map.path + " layer '" + layer.name + "'",
                     "profile " + profile.name + " has " + std::to_string(profile.maxLayers4bpp) +
                     " backgrounds with 16-colour tiles; this layer is left out (on SNES it could become a 4-colour layer)");
      continue;
    }

    GraphicsLayer gl;
    gl.layer = &layer;
    gl.role = roleId;
    if (map.tileWidth % 8 || map.tileHeight % 8)
    {
      report.error("does-not-fit", map.path, "tile size must be a multiple of 8");
      return;
    }
    int sub = map.tileWidth / 8, subY = map.tileHeight / 8;
    gl.width8 = layer.width * sub;
    gl.height8 = layer.height * subY;
    gl.cells.resize(size_t(gl.width8) * gl.height8);
    // Lit: the layers that scroll with the room (a background with parallax is not).
    bool lit = lightOf.mode != light::Mode::None && layer.parallaxX == 1.0f && layer.parallaxY == 1.0f;
    if (lightOf.mode != light::Mode::None && !lit)
      report.info("lighting", map.path + " layer '" + layer.name + "'", "a layer with parallax is not lit");
    if (lightOf.mode == light::Mode::Layer)
    {
      gl.pixelState.assign(size_t(gl.width8) * 8 * gl.height8 * 8, 0);
      gl.normals.assign(gl.pixelState.size(), light::Vec3{});
    }

    for (int my = 0; my < layer.height; my++)
    {
      for (int mx = 0; mx < layer.width; mx++)
      {
        uint32_t raw = layer.at(mx, my);
        uint32_t gid = raw & tmx::GID_MASK;
        if (gid == 0) continue;
        if (raw & tmx::FLIP_D)
        {
          report.error("does-not-fit", location(map, layer, mx, my), "diagonal (rotated) tiles are not supported by the hardware");
          continue;
        }
        const tmx::Tileset *ts = map.tilesetFor(gid);
        if (ts == nullptr || !ts->image)
        {
          report.error("does-not-fit", location(map, layer, mx, my), "tile has no tileset image");
          continue;
        }
        if (ts->tileWidth != map.tileWidth || ts->tileHeight != map.tileHeight)
        {
          report.error("does-not-fit", location(map, layer, mx, my), "tileset tile size differs from the map's tile size");
          continue;
        }
        int local = int(gid - ts->firstGid);
        int tx = (local % ts->columns) * ts->tileWidth;
        int ty = (local / ts->columns) * ts->tileHeight;
        bool fh = (raw & tmx::FLIP_H) != 0, fv = (raw & tmx::FLIP_V) != 0;

        for (int sy = 0; sy < subY; sy++)
        {
          for (int sx = 0; sx < sub; sx++)
          {
            // Which 8x8 part of the source tile lands at (sx, sy) after the metatile flip.
            int srcX = fh ? sub - 1 - sx : sx;
            int srcY = fv ? subY - 1 - sy : sy;
            Tile t;
            for (int py = 0; py < 8; py++)
              for (int px = 0; px < 8; px++)
              {
                int ix = tx + srcX * 8 + px, iy = ty + srcY * 8 + py;
                t[py * 8 + px] = (ix < ts->image->width && iy < ts->image->height) ? colors.convert(ts->image->pixel(ix, iy), ts->imagePath) : TRANSPARENT;
              }
            int flip = (fh ? FLIP_H : 0) | (fv ? FLIP_V : 0);
            t = flipped(t, flip);  // pixels as they appear on screen

            std::vector<uint32_t> apartKey(64, 0xFFFFFFFFu);
            if (lightOf.mode != light::Mode::None)
            {
              std::shared_ptr<Image> normalMap = lit ? light::tilesetNormals(*ts, report) : nullptr;
              int cellX = mx * sub + sx, cellY = my * subY + sy;
              for (int py = 0; py < 8; py++)
                for (int px = 0; px < 8; px++)
                {
                  uint16_t &c = t[py * 8 + px];
                  if (c == TRANSPARENT) continue;
                  int wx = cellX * 8 + px, wy = cellY * 8 + py;
                  size_t at = size_t(wy) * size_t(gl.width8) * 8 + size_t(wx);
                  if (!lit)
                  {
                    if (!gl.pixelState.empty()) gl.pixelState[at] = 1;
                    continue;
                  }
                  // The source pixel of this screen pixel, and its normal turned with the flips.
                  int sxp = (flip & FLIP_H) ? 7 - px : px, syp = (flip & FLIP_V) ? 7 - py : py;
                  int ix = tx + srcX * 8 + sxp, iy = ty + srcY * 8 + syp;
                  light::Vec3 n;
                  bool emissive = false;
                  if (normalMap && ix < normalMap->width && iy < normalMap->height)
                  {
                    const uint8_t *np = normalMap->pixel(ix, iy);
                    // Black in the normal map: the pixel gives light (a lamp's glass, a lit
                    // window) and is drawn as it is (mode palette: one level past the last).
                    emissive = np[0] == 0 && np[1] == 0 && np[2] == 0 && np[3] != 0;
                    n = light::decodeNormal(np);
                  }
                  if (emissive && lightOf.mode == light::Mode::Palette)
                  {
                    c = area.litColour(c, lightOf.levels);
                    continue;
                  }
                  if (flip & FLIP_H) n.x = -n.x;
                  if (flip & FLIP_V) n.y = -n.y;
                  float fx = float(wx) + 0.5f, fy = float(wy) + 0.5f;
                  if (lightOf.mode == light::Mode::Layer)
                  {
                    gl.pixelState[at] = 2;
                    gl.normals[at] = n;
                    continue;
                  }
                  float tv = room.solid.lit(fx, fy, lightOf, n);
                  if (lightOf.sources.size() > 1)
                  {
                    uint32_t key = c;
                    for (const light::Source &src : lightOf.sources)
                    {
                      float one = room.solid.shadowedFrom(fx, fy, lightOf, src) ? 0.0f : light::termFrom(lightOf, src, fx, fy, n);
                      key = key * 8u + uint32_t(std::clamp(int(std::lround(one * float(light::LEVELS - 1))), 0, light::LEVELS - 1));
                    }
                    apartKey[size_t(py * 8 + px)] = key;
                    area.apartEntries.insert(key);
                  }
                  // The level: the nearest, or with `light_dither` the one below or above by an
                  // ordered 4x4 dither of the room pixel (so the steps between levels blend).
                  float value = 0;
                  int level = lightOf.levelFor(tv, wx, wy, &value);
                  area.litError += std::abs(double(tv) - double(value));
                  area.litPixels++;
                  c = area.litColour(c, level);
                }
              if (lightOf.mode == light::Mode::Palette) fitLitTile(t, area);
              if (lightOf.mode == light::Mode::Palette && lightOf.sources.size() > 1) area.apartTiles.insert(apartKey);
            }

            TileRef ref;
            if (!isEmpty(t))
            {
              bool found = false;
              for (int f = 0; f < 4 && !found; f++)
              {
                auto it = area.tileIndex.find(flipped(t, f));
                if (it != area.tileIndex.end())
                {
                  ref.unique = it->second;
                  ref.flip = f;
                  found = true;
                }
              }
              if (!found)
              {
                ref.unique = uint16_t(area.uniqueTiles.size());
                area.uniqueTiles.push_back(t);
                area.tileIndex[t] = ref.unique;
                area.firstUse.push_back(location(map, layer, mx, my));
              }
            }
            gl.cells[size_t(my * subY + sy) * gl.width8 + (mx * sub + sx)] = ref;
          }
        }
      }
    }
    room.layers.push_back(std::move(gl));
  }
}

// Assigns every tile to a palette bank: tiles with the most colours first, each to the bank it
// adds the fewest new colours to.
void packPalettes(AreaWork &area, const Profile &profile, Report &report)
{
  size_t n = area.uniqueTiles.size();
  area.bankOf.assign(n, 0);
  std::vector<std::set<uint16_t>> sets(n);
  for (size_t i = 0; i < n; i++)
    for (uint16_t c : area.uniqueTiles[i])
      if (c != TRANSPARENT) sets[i].insert(c);

  std::vector<size_t> order(n);
  for (size_t i = 0; i < n; i++) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sets[a].size() > sets[b].size(); });

  std::vector<std::set<uint16_t>> banks;
  for (size_t i : order)
  {
    if (i == 0) continue;  // the empty tile
    if (sets[i].size() > 15)
    {
      report.error("does-not-fit", area.firstUse[i],
                   "an 8x8 tile uses " + std::to_string(sets[i].size()) +
                   " colours; 16-colour tiles allow 15 plus transparency. Reduce its colours or use a 256-colour layer");
      continue;
    }
    int best = -1;
    size_t bestNew = 99;
    for (size_t b = 0; b < banks.size(); b++)
    {
      size_t fresh = 0;
      for (uint16_t c : sets[i]) fresh += banks[b].count(c) ? 0 : 1;
      if (banks[b].size() + fresh <= 15 && fresh < bestNew)
      {
        best = int(b);
        bestNew = fresh;
      }
    }
    if (best < 0)
    {
      if (int(banks.size()) >= profile.bgPaletteBanks)
      {
        report.error("does-not-fit", area.firstUse[i],
                     "all " + std::to_string(profile.bgPaletteBanks) + " palette banks of profile " + profile.name +
                     " are full; this tile's colours do not fit. Use fewer distinct colours in area '" + area.name + "'");
        continue;
      }
      banks.emplace_back();
      best = int(banks.size()) - 1;
    }
    banks[size_t(best)].insert(sets[i].begin(), sets[i].end());
    area.bankOf[i] = best;
  }

  area.banks.clear();
  for (const auto &b : banks)
  {
    std::vector<uint16_t> colours(b.begin(), b.end());
    std::stable_sort(colours.begin(), colours.end(), [&](uint16_t a, uint16_t c) {
      int la = luminance(area.albedo(a)) + area.level(a), lc = luminance(area.albedo(c)) + area.level(c);
      return la < lc;
    });
    colours.insert(colours.begin(), 0);  // index 0: transparent
    area.banks.push_back(colours);
  }
  if (area.banks.empty()) area.banks.push_back({0});

  if (banks.size() > 1)
  {
    report.info("palette-split", "area '" + area.name + "'",
                std::to_string(n - 1) + " tiles need " + std::to_string(banks.size()) + " of " +
                std::to_string(profile.bgPaletteBanks) + " palette banks");
  }
}

uint8_t colourIndex(const AreaWork &area, int bank, uint16_t c)
{
  if (c == TRANSPARENT) return 0;
  const auto &colours = area.banks[size_t(bank)];
  return uint8_t(std::find(colours.begin(), colours.end(), c) - colours.begin());
}

Bytes tileChunk(const AreaWork &area, const Profile &profile)
{
  Bytes b;
  b.u16(uint32_t(area.uniqueTiles.size() + area.rawTiles.size()));
  b.u8(4);
  b.u8(profile.tileFormat == TileFormat::SnesPlanar4bpp ? 1 : 0);
  for (size_t i = 0; i < area.uniqueTiles.size(); i++)
  {
    const Tile &t = area.uniqueTiles[i];
    int bank = area.bankOf[i];
    uint8_t idx[64];
    for (int p = 0; p < 64; p++) idx[p] = colourIndex(area, bank, t[p]);
    if (profile.tileFormat == TileFormat::GbaLinear4bpp)
    {
      for (int p = 0; p < 64; p += 2) b.u8(uint32_t(idx[p] | (idx[p + 1] << 4)));
    }
    else
    {
      // SNES 4 bpp: rows of bit planes 0/1, then rows of bit planes 2/3; bit 7 = left pixel.
      uint8_t planes[32] = {};
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
          uint8_t v = idx[y * 8 + x];
          for (int plane = 0; plane < 4; plane++)
            if (v & (1 << plane)) planes[(plane >> 1) * 16 + y * 2 + (plane & 1)] |= uint8_t(0x80 >> x);
        }
      b.data.insert(b.data.end(), planes, planes + 32);
    }
  }
  for (const auto &idx : area.rawTiles)
    for (int p = 0; p < 64; p += 2) b.u8(uint32_t(idx[size_t(p)] | (idx[size_t(p + 1)] << 4)));
  return b;
}

Bytes paletteChunk(const AreaWork &area, uint16_t backdrop)
{
  Bytes b;
  b.u8(0);  // background palette
  b.u8(0);  // first bank
  b.u8(uint32_t(area.banks.size()));
  b.u8(0);
  for (size_t bank = 0; bank < area.banks.size(); bank++)
    for (int i = 0; i < 16; i++)
    {
      uint16_t c = i < int(area.banks[bank].size()) ? area.albedo(area.banks[bank][size_t(i)]) : 0;
      if (bank == 0 && i == 0) c = backdrop;
      b.u16(c);
    }
  return b;
}

Bytes mapChunk(const GraphicsLayer &gl, const AreaWork &area, const Profile &profile)
{
  Bytes b;
  b.u16(uint32_t(gl.width8));
  b.u16(uint32_t(gl.height8));
  b.u8(profile.mapFormat == MapFormat::Snes ? 1 : 0);
  b.u8(0);
  b.u16(0);
  for (const TileRef &r : gl.cells)
  {
    uint32_t bank = uint32_t(area.bankOf[r.unique]);
    bool h = r.flip & FLIP_H, v = r.flip & FLIP_V;
    if (profile.mapFormat == MapFormat::Gba)
      b.u16(r.unique | (h ? 0x400u : 0) | (v ? 0x800u : 0) | (bank << 12));
    else
      b.u16(r.unique | (bank << 10) | (h ? 0x4000u : 0) | (v ? 0x8000u : 0));
  }
  return b;
}

uint16_t parseColour(const std::string &s)
{
  // #RRGGBB or #AARRGGBB as Tiled writes colours
  if (s.size() < 7 || s[0] != '#') return 0;
  uint32_t v = uint32_t(std::stoul(s.substr(s.size() - 6), nullptr, 16));
  auto q = [](uint32_t c) { return uint16_t(std::lround(c * 31.0 / 255.0)); };
  return uint16_t(q((v >> 16) & 255) | (q((v >> 8) & 255) << 5) | (q(v & 255) << 10));
}

double numberProperty(const tmx::Map &map, const std::string &key, double fallback)
{
  std::string v = map.property(key);
  return v.empty() ? fallback : std::stod(v);
}

Bytes collisionChunk(const tmx::Map &map, Report &report)
{
  const tmx::Layer *shapes = nullptr, *materials = nullptr;
  for (const tmx::Layer &l : map.layers)
  {
    if (l.role() == "collision") shapes = &l;
    if (l.role() == "collision_material") materials = &l;
  }
  Bytes b;
  b.u16(uint32_t(map.width));
  b.u16(uint32_t(map.height));
  b.u8(uint32_t(map.tileWidth));
  b.u8(0);
  b.u16(0);
  if (map.tileWidth != map.tileHeight || (map.tileWidth != 16 && map.tileWidth != 8))
  {
    report.error("does-not-fit", map.path, "collision cells must be 8 or 16 pixels square");
  }

  auto lookup = [&](const tmx::Layer *layer, int x, int y) -> const tmx::Properties * {
    if (layer == nullptr) return nullptr;
    uint32_t gid = layer->at(x, y) & tmx::GID_MASK;
    if (gid == 0) return nullptr;
    const tmx::Tileset *ts = map.tilesetFor(gid);
    return ts ? ts->propertiesOf(int(gid - ts->firstGid)) : nullptr;
  };

  for (int y = 0; y < map.height; y++)
  {
    for (int x = 0; x < map.width; x++)
    {
      uint8_t b0 = 0, b1 = 0;
      if (const tmx::Properties *p = lookup(shapes, x, y))
      {
        auto shape = p->find("shape");
        auto solid = p->find("solid");
        if (shape != p->end())
        {
          auto it = kShapes.find(shape->second);
          if (it == kShapes.end())
            report.error("does-not-fit", location(map, *shapes, x, y), "unknown collision shape '" + shape->second + "'");
          else
            b0 = uint8_t(it->second);
        }
        std::string s = solid == p->end() ? "all" : solid->second;
        if (b0 != 0)
        {
          if (s == "all" || s == "top") b0 |= 0x40;
          if (s == "all" || s == "sides") b0 |= 0x80;
        }
        uint32_t raw = shapes->at(x, y);
        if (raw & tmx::FLIP_D)
          report.error("does-not-fit", location(map, *shapes, x, y), "diagonal flips are not allowed in collision layers");
        if (raw & tmx::FLIP_H) b0 |= 0x10;
        if (raw & tmx::FLIP_V) b0 |= 0x20;
      }
      if (const tmx::Properties *p = lookup(materials, x, y))
      {
        auto mat = p->find("material");
        if (mat != p->end())
        {
          auto it = kMaterials.find(mat->second);
          if (it == kMaterials.end())
            report.error("does-not-fit", location(map, *materials, x, y), "unknown material '" + mat->second + "'");
          else
            b1 = uint8_t(it->second);
        }
        auto param = p->find("parameter");
        if (param != p->end()) b1 |= uint8_t((std::stoi(param->second) & 15) << 4);
      }
      b.u8(b0);
      b.u8(b1);
    }
  }
  return b;
}

Bytes objectsChunk(const tmx::Map &map, StringTable &strings, Report &report, uint16_t &count)
{
  std::vector<const tmx::Object *> objects;
  for (const tmx::ObjectGroup &g : map.objectGroups)
  {
    auto role = g.properties.find("role");
    if (role == g.properties.end() || role->second != "objects")
    {
      report.warning("layer-dropped", map.path + " object layer '" + g.name + "'", "object layer without role 'objects' is ignored");
      continue;
    }
    for (const tmx::Object &o : g.objects) objects.push_back(&o);
  }

  // The room's own object, of the class "@room": the map's properties for the runtime. `attach`
  // and `Class.variable` for the room's scripts (attachable-scripts.md §8, "The room as an
  // owner"), `effect`, `effect_a`, `effect_b` (screen effects), `title` (said when the room
  // is entered) and the acoustics (`acoustics`, `echo`, `echo_feedback`, `echo_damping`,
  // `muffle`).
  tmx::Object roomObject;
  roomObject.type = "@room";
  roomObject.id = 0;
  bool attachValues = false;
  for (const auto &[key, value] : map.properties)
  {
    bool classValue = key.find('.') != std::string::npos && key.find('.') > 0;
    attachValues = attachValues || classValue;
    bool acoustics = key == "acoustics" || key == "echo" || key == "echo_feedback" || key == "echo_damping" || key == "muffle";
    if (key == "attach" || classValue || key == "effect" || key == "effect_a" || key == "effect_b" || key == "title" || acoustics)
      roomObject.properties[key] = value;
    if (key == "acoustics" && value != "dry" && value != "hall" && value != "cave" && value != "water")
      report.warning("acoustics", map.path, "unknown acoustics '" + value + "' (dry, hall, cave, water)");
    if (key == "echo" && std::atoi(value.c_str()) * BM_SOUND_RATE / 1000 > BM_SOUND_ECHO_MAX)
      report.warning("acoustics", map.path, "an echo of " + value + " ms is longer than the longest (" +
                     std::to_string(BM_SOUND_ECHO_MAX * 1000 / BM_SOUND_RATE) + " ms)");
  }
  if (!roomObject.properties.empty()) objects.push_back(&roomObject);
  if (attachValues && !roomObject.properties.count("attach"))
    report.warning("room-scripts", map.path, "properties for the room's attachments, but no 'attach' property");

  int starts = 0;
  Bytes entries, props;
  uint32_t propsBase = 4 + 20 * uint32_t(objects.size());
  for (const tmx::Object *o : objects)
  {
    // Without a class it needs a kind (layered-sprites.md §5: the record names the class).
    if (o->type.empty() && !o->properties.count("kind"))
      report.warning("does-not-fit", map.path + " object " + std::to_string(o->id), "object without class (type) or kind is ignored by the runtime");
    if (o->type == "PlayerStart") starts++;
    entries.u16(strings.id(o->type));
    entries.u16(uint32_t(o->id));
    entries.s16(int32_t(std::lround(o->x)));
    entries.s16(int32_t(std::lround(o->y)));
    entries.u16(uint32_t(std::lround(o->width)));
    entries.u16(uint32_t(std::lround(o->height)));
    entries.u16(strings.id(o->name));
    entries.u16(uint32_t(o->properties.size()));
    entries.u32(propsBase + uint32_t(props.data.size()));
    for (const auto &[key, value] : o->properties)
    {
      props.u16(strings.id(key));
      props.u16(strings.id(value));
    }
  }
  if (starts > 1)
    report.error("does-not-fit", map.path, "more than one PlayerStart in the room");

  Bytes b;
  b.u16(uint32_t(objects.size()));
  b.u16(0);
  b.data.insert(b.data.end(), entries.data.begin(), entries.data.end());
  b.data.insert(b.data.end(), props.data.begin(), props.data.end());
  count = uint16_t(objects.size());
  return b;
}

std::string roomIdOf(const tmx::Map &map)
{
  return world::roomIdOf(map);
}

// The head of a LITE chunk (lighting.md): the light, 20 bytes.
void lightHead(Bytes &b, const light::Light &l, uint32_t extra, uint32_t firstBank, uint32_t bankCount)
{
  auto u8of = [](float v, float scale) { return uint32_t(std::clamp(int(std::lround(v * scale)), 0, 255)); };
  b.u8(l.mode == light::Mode::Layer ? 2 : 1);
  b.u8(extra);
  b.u8(firstBank);
  b.u8(bankCount);
  b.s16(int32_t(std::lround(l.x)));
  b.s16(int32_t(std::lround(l.y)));
  b.u8(u8of(l.radius, 1));
  b.u8(u8of(l.height, 1));
  b.u16(l.colour);
  b.u8(u8of(l.ambient, 255));
  b.u8(u8of(l.flicker, 255));
  b.u8(uint32_t(l.flickerSpeed));
  b.u8(u8of(l.intensity, 64));
  b.u8(u8of(l.gain, 64));
  b.u8(l.shadows ? 1 : 0);
  b.u8(l.sources.size() > 1 ? uint32_t(l.sources.size() - 1) : 0);   // more lights, at the chunk's end
  b.u8(0);
}

// The lights after the first, at the end of LITE: s16 x, y each.
void lightSources(Bytes &b, const light::Light &l)
{
  for (size_t i = 1; i < l.sources.size(); i++)
  {
    b.s16(int32_t(std::lround(l.sources[i].x)));
    b.s16(int32_t(std::lround(l.sources[i].y)));
  }
}

// Mode `palette`: the light level of every entry of the area's banks.
Bytes paletteLightChunk(const RoomWork &room, const AreaWork &area, Report &report)
{
  Bytes b;
  // Bit 7 of the count: the light of each level follows the levels (u8, 255 = 1.0).
  lightHead(b, room.light, uint32_t(room.light.levels) | (room.light.levelLight.empty() ? 0u : 0x80u), 0, uint32_t(area.banks.size()));
  size_t lit = 0;
  for (const auto &bank : area.banks)
    for (int i = 0; i < 16; i++)
    {
      uint8_t level = i < int(bank.size()) && i > 0 ? area.level(bank[size_t(i)]) : 0;
      lit += level != 0;
      b.u8(level);
    }
  for (float v : room.light.levelLight) b.u8(uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)));
  while (b.data.size() % 2) b.u8(0);
  report.info("lighting", room.map->path, "baked into the palettes: " + std::to_string(area.lit.size()) + " (colour, light level) pairs, " +
              std::to_string(area.uniqueTiles.size()) + " tiles, " + std::to_string(area.banks.size()) + " banks, mean error " +
              std::to_string(area.litPixels ? area.litError / double(area.litPixels) : 0.0));
  if (room.light.sources.size() > 1)
    report.info("lighting", room.map->path, "if its " + std::to_string(room.light.sources.size()) + " lights flickered apart: " +
                std::to_string(area.apartEntries.size()) + " (colour, level, level) entries (at least " +
                std::to_string((area.apartEntries.size() + 14) / 15) + " banks), " + std::to_string(area.apartTiles.size()) + " lit tiles (before merging flips)");
  (void)lit;
  lightSources(b, room.light);
  return b;
}

// Mode `layer`: normal tiles over the room, a bank per region of the light, and its LITE.
std::pair<Bytes, Bytes> buildFittedLayer(const RoomWork &room, AreaWork &area, Report &report, const GraphicsLayer *main, const GraphicsLayer *back,
                                         int w8, int h8);

// Mode `layer`, values streamed (light_index = stream): the tiles hold normals only (7 or 15,
// clustered from the room's), so a brick is one tile wherever it is; the cells are grouped into
// bands by their x, and each band's colours are streamed into the palette line by line (H-blank
// DMA): for every room row and band, per normal `light colour × most intensity × gain ×
// max(0, N·F)` with F the lights' direction and falloff at the band's middle on that row. With
// 7 normals two bands share a bank (values 1-7 and 9-15). Shadowed pixels are empty. The
// runtime flickers the light with the blend weight of the light layer.
std::pair<Bytes, Bytes> buildStreamLayer(const RoomWork &room, AreaWork &area, Report &report, const GraphicsLayer *main, const GraphicsLayer *back,
                                         int w8, int h8)
{
  const light::Light &l = room.light;
  int wpx = w8 * 8, hpx = h8 * 8;
  auto state = [&](int x, int y, light::Vec3 *pN) -> int {
    for (const GraphicsLayer *gl : {main, back})
    {
      if (gl == nullptr || gl->pixelState.empty()) continue;
      size_t at = size_t(y) * size_t(wpx) + size_t(x);
      if (gl->pixelState[at] == 0) continue;
      if (pN) *pN = gl->normals[at];
      return gl->pixelState[at];
    }
    return 0;
  };
  // The normals.
  std::map<std::tuple<int, int, int>, std::pair<light::Vec3, float>> counts;
  for (int y = 0; y < hpx; y++)
    for (int x = 0; x < wpx; x++)
    {
      light::Vec3 n;
      if (state(x, y, &n) != 2 || light::term(l, float(x) + 0.5f, float(y) + 0.5f, light::Vec3{0, 0, 1}) <= 0) continue;
      auto key = std::make_tuple(int(std::lround(n.x * 16)), int(std::lround(n.y * 16)), int(std::lround(n.z * 16)));
      counts[key].first = n;
      counts[key].second += 1;
    }
  std::vector<light::Vec3> pts;
  std::vector<float> wts;
  for (const auto &[k, e] : counts) pts.push_back(e.first), wts.push_back(e.second);
  // A bank's 15 values. Flat bands (middle, mean): `samples` groups of the normals (a group's
  // first value 1 + group × groupStride), each group a band of its own.
  const bool ramp = l.bands == light::Light::Bands::Ramp && l.streamNormals <= 7;
  const int samples = 15 / l.streamNormals, groupStride = 16 / samples;
  const int perBank = ramp ? 1 : samples;
  std::vector<light::Vec3> normals = light::cluster(pts, wts, l.streamNormals);
  for (light::Vec3 &n : normals) n = light::normalize(n);
  // Ramp: one band per bank, and per normal samples of the light across it, from its left edge
  // to its right (shared with the neighbours, so a bank's edge does not show). The values go
  // where the pixels are: each normal has its two edges, and the values left go one by one to the
  // normal whose pixels gain most (share / gaps; the flat wall, where a gradient shows, takes
  // most). `rampFirst`: a normal's first value.
  std::vector<int> rampCount(normals.size(), 2), rampFirst(normals.size(), 1);
  if (ramp && !normals.empty())
  {
    std::vector<float> share(normals.size(), 0.0f);
    for (size_t i = 0; i < pts.size(); i++) share[size_t(light::nearest(normals, light::normalize(pts[i])))] += wts[i];
    for (int left = 15 - 2 * int(normals.size()); left > 0; left--)
    {
      size_t best = 0;
      for (size_t k = 1; k < normals.size(); k++)
        if (share[k] / float(rampCount[k] - 1) > share[best] / float(rampCount[best] - 1)) best = k;
      rampCount[best]++;
    }
    for (size_t k = 1; k < normals.size(); k++) rampFirst[k] = rampFirst[k - 1] + rampCount[k - 1];
  }
  // The bands: across what the lights reach, as many as the banks left hold.
  float x0 = 1e30f, x1 = -1e30f;
  for (const light::Source &src : l.sources)
  {
    x0 = std::min(x0, src.x - l.radius);
    x1 = std::max(x1, src.x + l.radius);
  }
  x0 = std::max(0.0f, std::floor(x0 / 8) * 8);
  x1 = std::min(float(wpx), x1);
  int firstBank = int(area.banks.size());
  int banksLeft = std::max(1, 15 - firstBank);
  int bandWidth = std::max(8, l.bandWidth / 8 * 8);
  int bands = int(std::ceil((x1 - x0) / float(bandWidth)));
  if (ramp) bands = (bands + 1) / 2;   // as many banks as flat bands of 7 normals would take
  if (ramp && bands > banksLeft) bands = banksLeft;
  else if ((bands + perBank - 1) / perBank > banksLeft)
  {
    bandWidth = int(std::ceil((x1 - x0) / float(banksLeft * perBank) / 8.0f)) * 8;
    bands = int(std::ceil((x1 - x0) / float(bandWidth)));
    report.info("lighting", room.map->path, "light layer (stream): the bands made " + std::to_string(bandWidth) + " pixels wide to fit the banks");
  }
  int banks = (bands + perBank - 1) / perBank;
  // The band edges where the light changes, not only evenly: along a wall facing the viewer at
  // the lights' height, bands are narrower near a light, where the light changes fast. Per 8 pixel column its band; per band the column it is lit as.
  int columns = int((x1 - x0) / 8.0f);
  std::vector<float> profile(size_t(std::max(columns, 1)), 0.0f);
  for (int c = 0; c < columns; c++)
    for (const light::Source &src : l.sources)
      profile[size_t(c)] = std::max(profile[size_t(c)], light::termFrom(l, src, x0 + float(c * 8 + 4), src.y, light::Vec3{0, 0, 1}));
  // Half by the steps of light as the eye sees them (square roots), half evenly: so the dim
  // tails get bands of their own too.
  float steps = 0;
  for (int c = 1; c < columns; c++) steps += std::abs(std::sqrt(profile[size_t(c)]) - std::sqrt(profile[size_t(c - 1)]));
  float even = columns > 1 ? steps / float(columns - 1) : 1.0f;
  std::vector<float> walked(size_t(std::max(columns, 1)), 0.0f);
  for (int c = 1; c < columns; c++)
    walked[size_t(c)] = walked[size_t(c - 1)] + std::abs(std::sqrt(profile[size_t(c)]) - std::sqrt(profile[size_t(c - 1)])) + even;
  float total = columns > 0 ? walked[size_t(columns - 1)] : 0.0f;
  std::vector<int> bandOfColumn(size_t(std::max(columns, 1)), 0);
  for (int c = 0; c < columns; c++)
    bandOfColumn[size_t(c)] = total > 0 ? std::min(bands - 1, int(walked[size_t(c)] / total * float(bands))) : 0;
  std::vector<float> bandX(size_t(bands), x0), bandL(size_t(bands), x0), bandR(size_t(bands), x0 + 8);
  for (int b = 0; b < bands; b++)
  {
    int first = -1, last = -1;
    for (int c = 0; c < columns; c++)
      if (bandOfColumn[size_t(c)] == b) { if (first < 0) first = c; last = c; }
    bandX[size_t(b)] = first < 0 ? x0 : x0 + float(first + last) * 4.0f + 4.0f;
    if (first >= 0) bandL[size_t(b)] = x0 + float(first * 8), bandR[size_t(b)] = x0 + float(last * 8 + 8);
  }
  // The lights' direction and falloff that a band's colours (sample j) stand for on row y.
  auto sampleF = [&](int band, int j, float y) -> light::Vec3 {
    size_t b = size_t(band);
    if (ramp) return light::scaledDirection(l, bandL[b] + (bandR[b] - bandL[b]) * float(j) / 65536.0f, y);   // j: where, 16.16
    if (l.bands == light::Light::Bands::Mean)
    {
      light::Vec3 sum{0, 0, 0};
      int count = 0;
      for (float x = bandL[b] + 1.0f; x < bandR[b]; x += 2.0f, count++)
      {
        light::Vec3 f = light::scaledDirection(l, x, y);
        sum.x += f.x, sum.y += f.y, sum.z += f.z;
      }
      if (count > 0) sum.x /= float(count), sum.y /= float(count), sum.z /= float(count);
      return sum;
    }
    return light::scaledDirection(l, bandX[b], y);
  };
  // Ramp: which of a band's samples a pixel takes, the two around it by an ordered dither.
  static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  auto rampAt = [&](int band, int count, int px, int py, int *pBelow, float *pFraction) {
    size_t b = size_t(band);
    float t = std::clamp((float(px) + 0.5f - bandL[b]) / std::max(bandR[b] - bandL[b], 1.0f), 0.0f, 1.0f) * float(count - 1);
    int j0 = std::min(int(t), count - 2);
    float f = t - float(j0);
    *pBelow = j0;
    *pFraction = f;
    // Samples far apart (fewer than 4): the nearest, a dither between them would show.
    float threshold = count < 4 ? 0.5f : (float(bayer[(py & 3) * 4 + (px & 3)]) + 0.5f) / 16.0f;
    return f > threshold ? j0 + 1 : j0;
  };
  // Each band's place (slot) in the banks: slot / perBank its bank, slot % perBank its half.
  // Hybrid (light_index = hybrid): the bands nearest the light take the first slots, streamed;
  // the others follow, fixed; with stream every band is streamed.
  const bool hybrid = l.index == light::Light::Index::Hybrid;
  const light::Source first = l.sources.empty() ? light::Source{l.x, l.y} : l.sources[0];
  int nearBands = hybrid ? std::min(bands, l.streamBanks * perBank) : bands;
  int nearFrom = 0;
  if (hybrid && nearBands > 0)
  {
    int middle = 0;
    for (int b = 1; b < bands; b++)
      if (std::abs(bandX[size_t(b)] - first.x) < std::abs(bandX[size_t(middle)] - first.x)) middle = b;
    nearFrom = std::clamp(middle - (nearBands - 1) / 2, 0, bands - nearBands);
  }
  std::vector<int> slotOfBand(size_t(bands), 0);
  {
    int next = 0;
    for (int b = nearFrom; b < nearFrom + nearBands; b++) slotOfBand[size_t(b)] = next++;
    next = (next + perBank - 1) / perBank * perBank;   // the fixed bands start at a bank of their own
    for (int b = 0; b < bands; b++)
      if (b < nearFrom || b >= nearFrom + nearBands) slotOfBand[size_t(b)] = next++;
    banks = (next + perBank - 1) / perBank;
    if (firstBank + banks > 15) report.error("lighting", room.map->path, "the light layer's bands need more banks than are left");
  }
  int nearBanks = (nearBands + perBank - 1) / perBank;
  // A band's values: per normal k its samples j (ramp: rampCount of them across the band; flat:
  // one). `exact` is the light a sample stands for on room row y (direction · falloff).
  auto countOf = [&](int k) { return ramp ? rampCount[size_t(k)] : 1; };
  auto exact = [&](int band, int k, int j, float y) {
    int count = countOf(k);
    return std::max(0.0f, light::dot(normals[size_t(k)], sampleF(band, count > 1 ? j * 65536 / (count - 1) : 0, y)));
  };
  auto isFixed = [&](int band) { return hybrid && (band < nearFrom || band >= nearFrom + nearBands); };
  // Hybrid: the fixed bands' light as (a value per sample) × (a weight per row, the light
  // layer's blend weight per line), the pair fitted to the light on every row (alternating least
  // squares, the best such product); the weight's most is 1. The streamed rows are divided by it.
  int hpxRows = hpx;
  std::vector<float> rowWeight(size_t(hpxRows), 1.0f);
  std::map<std::tuple<int, int, int>, float> fixedValue;   // (band, k, j)
  if (hybrid)
  {
    std::vector<std::tuple<int, int, int>> keys;
    for (int b = 0; b < bands; b++)
      if (isFixed(b))
        for (int k = 0; k < int(normals.size()); k++)
          for (int j = 0; j < countOf(k); j++) keys.emplace_back(b, k, j);
    std::vector<std::vector<float>> light(keys.size(), std::vector<float>(size_t(hpxRows)));
    // How many lit pixels use each sample on each row (a ramp's pixel both samples around it,
    // by how near): the fit is weighted by them, as the light is seen.
    std::map<std::tuple<int, int, int>, size_t> keyIndex;
    for (size_t i = 0; i < keys.size(); i++) keyIndex[keys[i]] = i;
    std::vector<std::vector<float>> use(keys.size(), std::vector<float>(size_t(hpxRows), 0.0f));
    for (int py = 0; py < hpxRows; py++)
      for (int px = 0; px < wpx; px++)
      {
        int column = int((float(px) - x0) / 8.0f);
        if (float(px) < x0 || column >= columns) continue;
        int band = bandOfColumn[size_t(column)];
        light::Vec3 n;
        if (!isFixed(band) || state(px, py, &n) != 2 || room.solid.lit(float(px) + 0.5f, float(py) + 0.5f, l, n) <= 0) continue;
        int k = light::nearest(normals, n), below = 0;
        float fraction = 0;
        if (ramp) rampAt(band, countOf(k), px, py, &below, &fraction);
        use[keyIndex[{band, k, below}]][size_t(py)] += 1 - fraction;
        if (ramp) use[keyIndex[{band, k, below + 1}]][size_t(py)] += fraction;
      }
    for (size_t i = 0; i < keys.size(); i++)
      for (int y = 0; y < hpxRows; y++) light[i][size_t(y)] = exact(std::get<0>(keys[i]), std::get<1>(keys[i]), std::get<2>(keys[i]), float(y) + 0.5f);
    std::vector<float> value(keys.size(), 0.0f);
    for (size_t i = 0; i < keys.size(); i++) value[i] = exact(std::get<0>(keys[i]), std::get<1>(keys[i]), std::get<2>(keys[i]), first.y);
    for (int round = 0; round < 20; round++)
    {
      for (int y = 0; y < hpxRows; y++)
      {
        float sum = 0, vv = 0;
        for (size_t i = 0; i < keys.size(); i++) sum += use[i][size_t(y)] * value[i] * light[i][size_t(y)], vv += use[i][size_t(y)] * value[i] * value[i];
        if (vv > 0) rowWeight[size_t(y)] = std::max(0.0f, sum / vv);
      }
      float most = *std::max_element(rowWeight.begin(), rowWeight.end());
      if (most <= 0) break;
      for (float &w : rowWeight) w /= most;
      for (size_t i = 0; i < keys.size(); i++)
      {
        float sum = 0, ww = 0;
        for (int y = 0; y < hpxRows; y++) sum += use[i][size_t(y)] * rowWeight[size_t(y)] * light[i][size_t(y)], ww += use[i][size_t(y)] * rowWeight[size_t(y)] * rowWeight[size_t(y)];
        if (ww > 0) value[i] = std::max(0.0f, sum / ww);
      }
    }
    for (size_t i = 0; i < keys.size(); i++) fixedValue[keys[i]] = value[i];
  }
  auto vertical = [&](float y) { return rowWeight[size_t(std::clamp(int(y), 0, hpxRows - 1))]; };
  // What sample (band, k, j) shows on row y. Fixed: its value × the row's weight. Streamed: the
  // exact light, but a ramp's edge next to a fixed band takes that band's value there (the same
  // place), so the two meet without a seam.
  auto bandValue = [&](int band, int k, int j, float y) {
    if (isFixed(band)) return fixedValue[{band, k, j}] * vertical(y);
    if (ramp && hybrid)
    {
      int last = countOf(k) - 1;
      if (j == 0 && band > 0 && isFixed(band - 1)) return fixedValue[{band - 1, k, last}] * vertical(y);
      if (j == last && band + 1 < bands && isFixed(band + 1)) return fixedValue[{band + 1, k, 0}] * vertical(y);
    }
    return exact(band, k, j, y);
  };

  Bytes m;
  m.u16(uint32_t(w8));
  m.u16(uint32_t(h8));
  m.u8(0);
  m.u8(0);
  m.u16(0);
  std::map<std::array<uint8_t, 64>, uint32_t> tileOf;
  size_t litCells = 0, edgeCells = 0, streamPixels = 0;
  double streamError = 0, smoothError = 0;   // smooth: the ramp's dither seen from afar
  for (int cy = 0; cy < h8; cy++)
    for (int cx = 0; cx < w8; cx++)
    {
      int column = int((float(cx * 8) - x0) / 8.0f);
      int band = column >= 0 && column < columns ? bandOfColumn[size_t(column)] : -1;
      bool reached = false;
      for (const light::Source &src : l.sources)
      {
        float dx = src.x - float(cx * 8 + 4), dy = src.y - float(cy * 8 + 4);
        reached |= light::falloff(l, dx * dx + dy * dy) > 0;
      }
      if (band < 0 || band >= bands || !reached)
      {
        m.u16(0);
        continue;
      }
      std::array<uint8_t, 64> t{};
      bool any = false, edge = false;
      for (int p = 0; p < 64; p++)
      {
        light::Vec3 n;
        int px = cx * 8 + p % 8, py = cy * 8 + p / 8;
        if (state(px, py, &n) != 2) continue;
        // Empty where every light that reaches it is shadowed.
        bool lit = false, shadowed = false;
        for (const light::Source &src : l.sources)
        {
          float dx = src.x - (float(px) + 0.5f), dy = src.y - (float(py) + 0.5f);
          if (light::falloff(l, dx * dx + dy * dy) <= 0) continue;
          if (room.solid.shadowedFrom(float(px) + 0.5f, float(py) + 0.5f, l, src)) shadowed = true;
          else lit = true;
        }
        edge |= shadowed;
        float truth = room.solid.lit(float(px) + 0.5f, float(py) + 0.5f, l, n);
        if (!lit)
        {
          streamError += truth;
          smoothError += truth;
          streamPixels++;
          continue;
        }
        int k = light::nearest(normals, n);
        int slot = slotOfBand[size_t(band)];
        float fraction = 0;
        int below = 0;
        int count = rampCount[size_t(k)];
        int group = ramp ? rampAt(band, count, px, py, &below, &fraction) : slot % perBank;
        t[size_t(p)] = uint8_t(ramp ? rampFirst[size_t(k)] + group : 1 + k + group * groupStride);
        auto valueOf = [&](int j) { return bandValue(band, k, j, float(py) + 0.5f); };
        float value = valueOf(ramp ? group : 0);
        streamError += std::abs(truth - value);
        if (ramp) smoothError += std::abs(truth - ((1 - fraction) * valueOf(below) + fraction * valueOf(below + 1)));
        else smoothError += std::abs(truth - value);
        streamPixels++;
        any = true;
      }
      if (!any)
      {
        m.u16(0);
        continue;
      }
      litCells++;
      edgeCells += edge;
      auto it = tileOf.find(t);
      uint32_t tile;
      if (it != tileOf.end()) tile = it->second;
      else
      {
        tile = uint32_t(area.uniqueTiles.size() + area.rawTiles.size());
        area.rawTiles.push_back(t);
        tileOf[t] = tile;
      }
      m.u16(tile | uint32_t(firstBank + slotOfBand[size_t(band)] / perBank) << 12);
    }

  // The rows: one per room pixel row and one more (a screen at the bottom reads one past it),
  // of the streamed banks. Hybrid: divided by the row's weight, which the light layer's blend
  // multiplies them with again.
  float most = l.intensity * (1.0f + l.flicker) * l.gain;
  float lc[3] = {float(l.colour & 31), float((l.colour >> 5) & 31), float((l.colour >> 10) & 31)};
  int rows = hpx + 1, stride = nearBanks * 16;
  // The colours of a bank (by its slots) for a row (`y`) and a scale.
  auto bankColours = [&](Bytes &out, int bank, float y, float scale, bool streamedRow) {
    for (int e = 0; e < 16; e++)
    {
      int group = e == 0 ? 0 : (e - 1) / groupStride, k = e == 0 ? -1 : (e - 1) % groupStride;
      if (ramp)
      {
        k = -1;
        for (size_t i = 0; i < normals.size(); i++)
          if (e >= rampFirst[i] && e < rampFirst[i] + rampCount[i]) k = int(i), group = e - rampFirst[i];
      }
      int slot = ramp ? bank : bank * perBank + group, band = -1;
      for (int b = 0; b < bands; b++)
        if (slotOfBand[size_t(b)] == slot) band = b;
      uint16_t c = 0;
      if (k >= 0 && k < int(normals.size()) && (ramp || group < samples) && band >= 0)
      {
        int j = ramp ? group : 0;
        float v = (streamedRow ? bandValue(band, k, j, y) : fixedValue[{band, k, j}]) * most * scale;
        auto ch = [&](int i) { return uint32_t(std::clamp(int(std::lround(lc[i] * v)), 0, 31)); };
        c = uint16_t(ch(0) | ch(1) << 5 | ch(2) << 10);
      }
      out.u16(c);
    }
  };
  Bytes table;
  for (int y = 0; y < rows; y++)
  {
    float fy = float(std::min(y, hpx - 1)) + 0.5f;
    float scale = hybrid ? 1.0f / std::max(vertical(fy), 1.0f / 16.0f) : 1.0f;
    for (int b = 0; b < nearBanks; b++) bankColours(table, b, fy, scale, true);
  }
  Bytes fixed, weights;
  if (hybrid)
  {
    for (int b = nearBanks; b < banks; b++) bankColours(fixed, b, first.y, 1.0f, false);
    for (int y = 0; y < rows; y++) weights.u8(uint32_t(std::lround(vertical(float(std::min(y, hpx - 1)) + 0.5f) * 255)));
    while (weights.data.size() % 4) weights.u8(0);
  }
  Bytes lite;
  lightHead(lite, l, (hybrid ? 0x20 : 0x40) | uint32_t(normals.size()), uint32_t(firstBank), uint32_t(banks));
  lite.u16(uint32_t(rows));
  lite.u16(uint32_t(stride));
  lite.u16(uint32_t(bandWidth));
  lite.u16(hybrid ? uint32_t(banks - nearBanks) : 0);   // hybrid: the fixed banks
  lite.data.insert(lite.data.end(), table.data.begin(), table.data.end());
  lite.data.insert(lite.data.end(), fixed.data.begin(), fixed.data.end());
  lite.data.insert(lite.data.end(), weights.data.begin(), weights.data.end());
  report.info("lighting", room.map->path, std::string(hybrid ? "light layer (hybrid, " + std::to_string(nearBanks) + " banks streamed): " : "light layer (stream): ") + "" + std::to_string(tileOf.size()) + " normal tiles (" + std::to_string(edgeCells) +
              " cells on a shadow's edge), " + std::to_string(normals.size()) + " normals, " + std::to_string(bands) + " bands (narrower near the light) in " + std::to_string(banks) + " banks from bank " + std::to_string(firstBank) + ", " +
              std::to_string(stride) + " colours a line, " + std::to_string(table.data.size()) + " bytes of rows, " + std::to_string(litCells) + " lit cells, mean error " +
              std::to_string(streamPixels ? streamError / double(streamPixels) : 0.0) + (ramp ? " (dither seen from afar " + std::to_string(streamPixels ? smoothError / double(streamPixels) : 0.0) + ")" : ""));
  lightSources(lite, l);
  return {m, lite};
}

// Mode `layer`, values shared (light_index = shared): a cell's light is split in two. Its
// pattern, how much each pixel faces the light, comes from the pixel's normal and one of a few
// light directions (the cells' directions clustered: `light_directions`), in 15 steps; the
// same drawing under the same direction gives the same tile, so tiles are merged as the room's
// are. How strong the light is there, its falloff, is the cell's bank: every bank is the same
// 15 steps scaled by one of up to 12 strengths (the cells' falloff clustered). Shadowed pixels
// have no light (so cells on a shadow's edge have tiles of their own).
std::pair<Bytes, Bytes> buildSharedLayer(const RoomWork &room, AreaWork &area, Report &report, const GraphicsLayer *main, const GraphicsLayer *back,
                                         int w8, int h8)
{
  const light::Light &l = room.light;
  int wpx = w8 * 8;
  auto state = [&](int x, int y, light::Vec3 *pN) -> int {
    for (const GraphicsLayer *gl : {main, back})
    {
      if (gl == nullptr || gl->pixelState.empty()) continue;
      size_t at = size_t(y) * size_t(wpx) + size_t(x);
      if (gl->pixelState[at] == 0) continue;
      if (pN) *pN = gl->normals[at];
      return gl->pixelState[at];
    }
    return 0;
  };
  std::vector<int> litCells;
  std::vector<light::Vec3> cellDir, cellFall;
  for (int cy = 0; cy < h8; cy++)
    for (int cx = 0; cx < w8; cx++)
    {
      float mx = float(cx * 8) + 4, my = float(cy * 8) + 4;
      // Split: how lit a flat surface is here (the bank) and the light's slope across it (the
      // pattern): N·L = Lz (nz + nx tx + ny ty) with t = (Lx, Ly) / Lz. With several lights
      // (flickering together) the sums: s = Σ f Lz, t = Σ f (Lx, Ly) / s.
      light::Vec3 F = light::scaledDirection(l, mx, my);
      if (F.z <= 0) continue;
      bool any = false;
      for (int p = 0; p < 64 && !any; p++) any = state(cx * 8 + p % 8, cy * 8 + p / 8, nullptr) == 2;
      if (!any) continue;
      litCells.push_back(cy * w8 + cx);
      cellDir.push_back(light::normalize({F.x / F.z, F.y / F.z, 1}));
      cellFall.push_back(light::Vec3{F.z, 0, 0});
    }
  int firstBank = int(area.banks.size());
  int banks = std::min(light::MAX_REGIONS, 15 - firstBank);
  if (banks < 1)
  {
    report.error("lighting", room.map->path, "no palette bank is left for the light layer");
    banks = 1;
  }
  // The light directions: the cells' directions clustered as unit vectors (so the few cells
  // near the light, where it changes fast, get theirs too); each one's slope t = (Lx, Ly) / Lz.
  std::vector<light::Vec3> dirs = light::cluster(cellDir, std::vector<float>(cellDir.size(), 1.0f), l.directions);
  for (light::Vec3 &d : dirs)
  {
    d = light::normalize(d);
    float lz = std::max(d.z, 0.05f);
    d = light::Vec3{d.x / lz, d.y / lz, 0};
  }
  // Then refined on what they are for: each cell takes the direction whose pattern, at the
  // cell's own strength, is nearest to its pixels' true light; each direction moves to the mean
  // slope of its cells, weighted by how bright they are. A few rounds.
  std::vector<int> cellClass(litCells.size(), 0);
  {
    struct Px { light::Vec3 n; float truth; };
    std::vector<std::vector<Px>> pixels(litCells.size());
    for (size_t i = 0; i < litCells.size(); i++)
    {
      int cx = litCells[i] % w8, cy = litCells[i] / w8;
      for (int p = 0; p < 64; p++)
      {
        light::Vec3 n;
        int px = cx * 8 + p % 8, py = cy * 8 + p / 8;
        if (state(px, py, &n) != 2) continue;
        pixels[i].push_back({n, light::term(l, float(px) + 0.5f, float(py) + 0.5f, n)});
      }
    }
    for (int round = 0; round < 8; round++)
    {
      for (size_t i = 0; i < litCells.size(); i++)
      {
        float best = 1e30f;
        for (size_t c = 0; c < dirs.size(); c++)
        {
          float e = 0;
          for (const Px &q : pixels[i])
            e += std::abs(cellFall[i].x * std::min(4.0f, std::max(0.0f, q.n.z + q.n.x * dirs[c].x + q.n.y * dirs[c].y)) - q.truth);
          if (e < best) best = e, cellClass[i] = int(c);
        }
      }
      std::vector<light::Vec3> sum(dirs.size(), light::Vec3{0, 0, 0});
      std::vector<float> weight(dirs.size(), 0);
      for (size_t i = 0; i < litCells.size(); i++)
      {
        float lz = std::max(cellDir[i].z, 0.05f), w = cellFall[i].x * cellFall[i].x;
        sum[size_t(cellClass[i])].x += cellDir[i].x / lz * w;
        sum[size_t(cellClass[i])].y += cellDir[i].y / lz * w;
        weight[size_t(cellClass[i])] += w;
      }
      for (size_t c = 0; c < dirs.size(); c++)
        if (weight[c] > 0) dirs[c] = light::Vec3{sum[c].x / weight[c], sum[c].y / weight[c], 0};
    }
  }
  // A pixel's pattern: how lit it is against a flat surface there (1: as lit as a flat one).
  // The strengths, spaced as the eye sees them (clustered on their square roots: steps in the
  // dark show more).
  std::vector<light::Vec3> roots;
  for (const light::Vec3 &f : cellFall) roots.push_back(light::Vec3{std::sqrt(f.x), 0, 0});
  std::vector<light::Vec3> strengths = light::cluster(roots, std::vector<float>(roots.size(), 1.0f), banks);
  std::sort(strengths.begin(), strengths.end(), [](const light::Vec3 &a, const light::Vec3 &b) { return a.x < b.x; });
  for (size_t i = 0; i < cellFall.size(); i++) cellFall[i] = roots[i];   // compared as roots
  // The 15 steps of the pattern, one ladder for every bank: fitted to all the room's lit pixels
  // (1D k-means over their patterns, weighted by how bright they are), so the steps lie where
  // the pixels are (around a flat surface's 1) and not evenly.
  auto patternOf = [&](size_t i, const light::Vec3 &n) {
    const light::Vec3 &tc = dirs[size_t(cellClass[i])];
    return std::min(4.0f, std::max(0.0f, n.z + n.x * tc.x + n.y * tc.y));
  };
  std::vector<float> ladder;
  {
    std::map<int, float> counts;
    for (size_t i = 0; i < litCells.size(); i++)
    {
      int cx = litCells[i] % w8, cy = litCells[i] / w8;
      float weight = cellFall[i].x;   // the root of its strength
      for (int p = 0; p < 64; p++)
      {
        light::Vec3 n;
        if (state(cx * 8 + p % 8, cy * 8 + p / 8, &n) != 2) continue;
        float v = patternOf(i, n);
        if (v > 0.02f) counts[int(std::lround(v * 256))] += weight;
      }
    }
    std::vector<light::Vec3> pts;
    std::vector<float> wts;
    for (const auto &[k, c] : counts) pts.push_back(light::Vec3{float(k) / 256.0f, 0, 0}), wts.push_back(c);
    for (const light::Vec3 &c : light::cluster(pts, wts, 15)) ladder.push_back(c.x);
    std::sort(ladder.begin(), ladder.end());
    if (ladder.empty()) ladder.push_back(1.0f);
  }

  Bytes m;
  m.u16(uint32_t(w8));
  m.u16(uint32_t(h8));
  m.u8(0);
  m.u8(0);
  m.u16(0);
  std::vector<uint16_t> entries(size_t(w8) * h8, 0);
  std::map<std::array<uint8_t, 64>, uint32_t> tileOf;
  float error = 0;
  size_t measured = 0, edgeCells = 0, ownCells = 0;
  for (size_t i = 0; i < litCells.size(); i++)
  {
    int cx = litCells[i] % w8, cy = litCells[i] / w8;
    // Two ways for the cell: the shared tile (the pattern for its slope, its strength the
    // bank's), or, where the light changes too fast for that across the cell (near the light),
    // a tile of its own: each pixel's own light on the same ladder, in the bank that fits it
    // best: where the light changes across the cell by more than the tolerance (of full light),
    // which is near the light, and the cell's own tile is nearer to the truth.
    struct Px { int p; float truth; bool shadow; float pattern; };
    std::vector<Px> px;
    bool edge = false;
    for (int p = 0; p < 64; p++)
    {
      light::Vec3 n;
      int x = cx * 8 + p % 8, y = cy * 8 + p / 8;
      if (state(x, y, &n) != 2) continue;
      float fx = float(x) + 0.5f, fy = float(y) + 0.5f;
      bool partly = false;
      float truth = room.solid.lit(fx, fy, l, n, &partly);
      bool shadow = partly && truth <= 0;   // (only used for cells without a shadow's edge)
      edge |= partly;
      px.push_back({p, truth, shadow, patternOf(i, n)});
    }
    // The nearest step of `v` (0: nearer to no light at all).
    auto step = [&](float v) {
      int k = 0;
      float bestD = v;
      for (size_t j = 0; j < ladder.size(); j++)
        if (std::abs(ladder[j] - v) < bestD) bestD = std::abs(ladder[j] - v), k = int(j) + 1;
      return k;
    };
    auto valueOf = [&](int k, float scale) { return k ? scale * ladder[size_t(k - 1)] : 0.0f; };
    int bank = light::nearest(strengths, cellFall[i]);
    float scale = strengths[size_t(bank)].x * strengths[size_t(bank)].x;
    std::array<uint8_t, 64> t{};
    float sharedError = 0;
    for (const Px &q : px)
    {
      int k = q.shadow ? 0 : step(q.pattern);
      t[size_t(q.p)] = uint8_t(k);
        sharedError += std::abs(q.truth - valueOf(k, scale));
    }
    // How much a flat surface's light changes across the cell (its corners): much only near
    // the light.
    float lo = 1e30f, hi = 0;
    for (int corner = 0; corner < 4; corner++)
    {
      float qx = float(cx * 8 + (corner & 1) * 8), qy = float(cy * 8 + (corner >> 1) * 8);
      float v = light::term(l, qx, qy, light::Vec3{0, 0, 1});
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    // Cells a shadow's edge crosses need a tile of their own anyway: the same way, so a pixel
    // in the shadow of one light keeps the light of the others.
    if (!px.empty() && (edge || hi - lo > l.tolerance))
    {
      int bestBank = bank;
      float bestError = sharedError;
      std::array<uint8_t, 64> bestTile = t;
      for (size_t b = 0; b < strengths.size(); b++)
      {
        float sc = strengths[b].x * strengths[b].x, e = 0;
        std::array<uint8_t, 64> u{};
        for (const Px &q : px)
        {
          int k = q.truth > 0 && sc > 0 ? step(q.truth / sc) : 0;
          u[size_t(q.p)] = uint8_t(k);
          e += std::abs(q.truth - valueOf(k, sc));
        }
        if (e < bestError) bestError = e, bestBank = int(b), bestTile = u;
      }
      if (bestBank != bank || bestTile != t)
      {
        ownCells += !edge;
        bank = bestBank;
        scale = strengths[size_t(bank)].x * strengths[size_t(bank)].x;
        t = bestTile;
      }
    }
    bool any = false;
    for (const Px &q : px)
    {
      error += std::abs(q.truth - valueOf(t[size_t(q.p)], scale));
      measured++;
      any |= t[size_t(q.p)] != 0;
    }
    if (!any) continue;
    edgeCells += edge;
    auto it = tileOf.find(t);
    uint32_t tile;
    if (it != tileOf.end()) tile = it->second;
    else
    {
      tile = uint32_t(area.uniqueTiles.size() + area.rawTiles.size());
      area.rawTiles.push_back(t);
      tileOf[t] = tile;
    }
    entries[size_t(litCells[i])] = uint16_t(tile | uint32_t(firstBank + bank) << 12);
  }
  for (uint16_t e : entries) m.u16(e);

  Bytes lite;
  lightHead(lite, l, 0x80 | 15, uint32_t(firstBank), uint32_t(strengths.size()));
  for (const light::Vec3 &st : strengths)
    for (size_t k = 0; k < 15; k++)
      lite.u16(uint32_t(k < ladder.size() ? std::clamp(int(std::lround(st.x * st.x * ladder[k] * 4096)), 0, 32767) : 0));
  report.info("lighting", room.map->path, "light layer (shared): " + std::to_string(tileOf.size()) + " light tiles (" + std::to_string(edgeCells) +
              " cells on a shadow's edge, " + std::to_string(ownCells) + " near the light with their own light), " +
              std::to_string(dirs.size()) + " light directions, " + std::to_string(strengths.size()) +
              " banks from bank " + std::to_string(firstBank) + ", " + std::to_string(litCells.size()) + " lit cells, mean error " +
              std::to_string(measured ? error / float(measured) : 0.0f));
  lightSources(lite, l);
  return {m, lite};
}

// Mode `layer`, values fitted (light_index = fitted): a pixel's value stands for its whole light
// at full intensity, `max(0, N·L) × falloff`, 0 in shadow; its direction, distance and shadow
// are worked out here, per pixel. The light only scales at run time, so a bank is 15 levels of
// light: the lit cells are grouped by how lit they are (one bank each group), each group's
// levels fitted to its pixels (1D k-means), and every pixel takes its bank's nearest level.
std::pair<Bytes, Bytes> buildFittedLayer(const RoomWork &room, AreaWork &area, Report &report, const GraphicsLayer *main, const GraphicsLayer *back,
                                         int w8, int h8)
{
  const light::Light &l = room.light;
  int wpx = w8 * 8;
  auto state = [&](int x, int y, light::Vec3 *pN) -> int {
    for (const GraphicsLayer *gl : {main, back})
    {
      if (gl == nullptr || gl->pixelState.empty()) continue;
      size_t at = size_t(y) * size_t(wpx) + size_t(x);
      if (gl->pixelState[at] == 0) continue;
      if (pN) *pN = gl->normals[at];
      return gl->pixelState[at];
    }
    return 0;
  };
  // The light of every pixel of every cell that has some.
  std::vector<int> litCells;
  std::vector<std::array<float, 64>> cellLight;
  std::vector<light::Vec3> cellKey;
  for (int cy = 0; cy < h8; cy++)
    for (int cx = 0; cx < w8; cx++)
    {
      std::array<float, 64> t;
      float sum = 0, most = 0;
      int count = 0;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
          light::Vec3 n;
          float v = 0;
          int px = cx * 8 + x, py = cy * 8 + y;
          if (state(px, py, &n) == 2)
          {
            float fx = float(px) + 0.5f, fy = float(py) + 0.5f;
            v = room.solid.lit(fx, fy, l, n);
          }
          t[size_t(y * 8 + x)] = v;
          if (v > 0) sum += v, count++, most = std::max(most, v);
        }
      if (count == 0 || most < 0.004f) continue;
      litCells.push_back(cy * w8 + cx);
      cellLight.push_back(t);
      cellKey.push_back(light::Vec3{sum / float(count), most * 0.5f, 0});
    }
  int firstBank = int(area.banks.size());
  int regions = std::min(light::MAX_REGIONS, 15 - firstBank);
  if (regions < 1)
  {
    report.error("lighting", room.map->path, "no palette bank is left for the light layer");
    regions = 1;
  }
  // Groups of cells by how lit they are (their mean and brightest pixel).
  std::vector<light::Vec3> groups = light::cluster(cellKey, std::vector<float>(cellKey.size(), 1.0f), regions);
  std::vector<int> groupOf(litCells.size());
  for (size_t i = 0; i < litCells.size(); i++) groupOf[i] = light::nearest(groups, cellKey[i]);
  // Each group's 15 levels, fitted to its pixels' light.
  std::vector<std::vector<float>> levels(groups.size());
  for (size_t gi = 0; gi < groups.size(); gi++)
  {
    std::map<int, float> counts;   // light in 1/1024 steps: how many pixels
    for (size_t i = 0; i < litCells.size(); i++)
      if (groupOf[i] == int(gi))
        for (float v : cellLight[i])
          if (v > 0) counts[int(std::lround(v * 1024))] += 1;
    std::vector<light::Vec3> pts;
    std::vector<float> wts;
    for (const auto &[k, c] : counts) pts.push_back(light::Vec3{float(k) / 1024.0f, 0, 0}), wts.push_back(c);
    for (const light::Vec3 &c : light::cluster(pts, wts, 15)) levels[gi].push_back(c.x);
    std::sort(levels[gi].begin(), levels[gi].end());
  }

  Bytes m;
  m.u16(uint32_t(w8));
  m.u16(uint32_t(h8));
  m.u8(0);
  m.u8(0);
  m.u16(0);
  std::vector<uint16_t> entries(size_t(w8) * h8, 0);
  std::map<std::array<uint8_t, 64>, uint32_t> tileOf;
  float error = 0;
  size_t measured = 0;
  for (size_t i = 0; i < litCells.size(); i++)
  {
    const std::vector<float> &lv = levels[size_t(groupOf[i])];
    std::array<uint8_t, 64> t{};
    bool any = false;
    for (int p = 0; p < 64; p++)
    {
      float v = cellLight[i][size_t(p)];
      if (v <= 0) continue;
      // The nearest level; nearer to none at all: nothing.
      int best = -1;
      float bestD = v;
      for (size_t k = 0; k < lv.size(); k++)
        if (std::abs(lv[k] - v) < bestD) bestD = std::abs(lv[k] - v), best = int(k);
      error += bestD;
      measured++;
      if (best < 0) continue;
      t[size_t(p)] = uint8_t(best + 1);
      any = true;
    }
    if (!any) continue;
    auto it = tileOf.find(t);
    uint32_t tile;
    if (it != tileOf.end()) tile = it->second;
    else
    {
      tile = uint32_t(area.uniqueTiles.size() + area.rawTiles.size());
      area.rawTiles.push_back(t);
      tileOf[t] = tile;
    }
    entries[size_t(litCells[i])] = uint16_t(tile | uint32_t(firstBank + groupOf[i]) << 12);
  }
  for (uint16_t e : entries) m.u16(e);

  Bytes lite;
  lightHead(lite, l, 0x80 | 15, uint32_t(firstBank), uint32_t(groups.size()));
  for (const std::vector<float> &lv : levels)
    for (size_t k = 0; k < 15; k++) lite.u16(uint32_t(k < lv.size() ? std::clamp(int(std::lround(lv[k] * 4096)), 0, 32767) : 0));
  report.info("lighting", room.map->path, "light layer (fitted): " + std::to_string(tileOf.size()) + " light tiles, " +
              std::to_string(groups.size()) + " banks of 15 levels from bank " + std::to_string(firstBank) + ", " +
              std::to_string(litCells.size()) + " lit cells, mean error " +
              std::to_string(measured ? error / float(measured) : 0.0f));
  lightSources(lite, l);
  return {m, lite};
}

std::pair<Bytes, Bytes> buildLightLayer(const RoomWork &room, AreaWork &area, Report &report)
{
  const light::Light &l = room.light;
  const GraphicsLayer *main = nullptr, *back = nullptr;
  for (const GraphicsLayer &gl : room.layers)
  {
    if (gl.role == ROLE_MAIN) main = &gl;
    if (gl.role == ROLE_BACKGROUND) back = &gl;
    if (gl.role == ROLE_FOREGROUND)
      report.error("lighting", room.map->path, "a room lit with the light layer has no foreground layer (the light layer takes its place)");
  }
  int w8 = main ? main->width8 : back ? back->width8 : 0, h8 = main ? main->height8 : back ? back->height8 : 0;
  if (l.index == light::Light::Index::Stream || l.index == light::Light::Index::Hybrid) return buildStreamLayer(room, area, report, main, back, w8, h8);
  if (l.index == light::Light::Index::Shared) return buildSharedLayer(room, area, report, main, back, w8, h8);
  if (l.index == light::Light::Index::Fitted) return buildFittedLayer(room, area, report, main, back, w8, h8);
  int wpx = w8 * 8;
  // What shows at a pixel: the main layer's, else the background's.
  auto state = [&](int x, int y, light::Vec3 *pN) -> int {
    for (const GraphicsLayer *gl : {main, back})
    {
      if (gl == nullptr || gl->pixelState.empty()) continue;
      size_t at = size_t(y) * size_t(wpx) + size_t(x);
      if (gl->pixelState[at] == 0) continue;
      if (pN) *pN = gl->normals[at];
      return gl->pixelState[at];
    }
    return 0;
  };
  // Lit cells: their middle inside the radius, some pixel lit.
  std::vector<int> cellRegion(size_t(w8) * h8, -1);
  std::vector<light::Vec3> cellF;
  std::vector<int> litCells;
  std::map<std::tuple<int, int, int>, std::pair<light::Vec3, float>> normalCounts;
  for (int cy = 0; cy < h8; cy++)
    for (int cx = 0; cx < w8; cx++)
    {
      float mx = float(cx * 8 + 4), my = float(cy * 8 + 4);
      light::Vec3 F = light::scaledDirection(l, mx, my);
      if (F.x == 0 && F.y == 0 && F.z <= 0) continue;
      bool any = false;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
          light::Vec3 n;
          if (state(cx * 8 + x, cy * 8 + y, &n) != 2) continue;
          any = true;
          auto key = std::make_tuple(int(std::lround(n.x * 16)), int(std::lround(n.y * 16)), int(std::lround(n.z * 16)));
          auto &e = normalCounts[key];
          e.first = n;
          e.second += 1;
        }
      if (!any) continue;
      litCells.push_back(cy * w8 + cx);
      cellF.push_back(light::scaledDirection(l, mx, my));
    }
  std::vector<light::Vec3> pts;
  std::vector<float> wts;
  for (const auto &[k, e] : normalCounts) pts.push_back(e.first), wts.push_back(e.second);
  std::vector<light::Vec3> normals = light::cluster(pts, wts, light::LAYER_NORMALS);
  for (light::Vec3 &n : normals) n = light::normalize(n);
  int firstBank = int(area.banks.size());
  int regions = std::min(light::MAX_REGIONS, 15 - firstBank);
  if (regions < 1)
  {
    report.error("lighting", room.map->path, "no palette bank is left for the light layer");
    regions = 1;
  }
  // Regions: the cells clustered by how strong the light is there first, its direction second
  // (falloff steps show more than a slightly other direction), each region's light the mean of
  // its cells'. A cell nearer to no light at all than to any region stays dark.
  auto keyOf = [](light::Vec3 f) {
    float m = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    return light::Vec3{f.x * 0.5f, f.y * 0.5f, m};
  };
  std::vector<light::Vec3> keys;
  for (const light::Vec3 &f : cellF) keys.push_back(keyOf(f));
  std::vector<light::Vec3> keyCentres = light::cluster(keys, std::vector<float>(keys.size(), 1.0f), regions);
  keyCentres.push_back(light::Vec3{0, 0, 0});   // no light
  std::vector<light::Vec3> centres(keyCentres.size() - 1, light::Vec3{0, 0, 0});
  std::vector<float> members(centres.size(), 0);
  for (size_t i = 0; i < litCells.size(); i++)
  {
    int k = light::nearest(keyCentres, keys[i]);
    if (k == int(centres.size())) k = -1;
    cellRegion[size_t(litCells[i])] = k;
    if (k < 0) continue;
    centres[size_t(k)].x += cellF[i].x;
    centres[size_t(k)].y += cellF[i].y;
    centres[size_t(k)].z += cellF[i].z;
    members[size_t(k)] += 1;
  }
  for (size_t k = 0; k < centres.size(); k++)
    if (members[k] > 0) centres[k] = {centres[k].x / members[k], centres[k].y / members[k], centres[k].z / members[k]};

  // The normal tiles, merged; map entries name the tile and the region's bank.
  Bytes m;
  m.u16(uint32_t(w8));
  m.u16(uint32_t(h8));
  m.u8(0);
  m.u8(0);
  m.u16(0);
  std::map<std::array<uint8_t, 64>, uint32_t> tileOf;
  size_t edgeTiles = 0, normalPixels = 0;
  double normalError = 0;
  for (int cy = 0; cy < h8; cy++)
    for (int cx = 0; cx < w8; cx++)
    {
      int region = cellRegion[size_t(cy) * w8 + cx];
      if (region < 0)
      {
        m.u16(0);
        continue;
      }
      std::array<uint8_t, 64> t{};
      bool any = false, shadowEdge = false;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
          light::Vec3 n;
          int px = cx * 8 + x, py = cy * 8 + y;
          if (state(px, py, &n) != 2) continue;
          // In the shadow of every light that reaches it: empty. (In the shadow of only some, it
          // keeps its normal and is lit as if by all: this mode has no way to tell.)
          bool partly = false;
          float truth = room.solid.lit(float(px) + 0.5f, float(py) + 0.5f, l, n, &partly);
          bool all = partly && l.sources.size() <= 1;
          if (partly && !all)
          {
            // Shadowed from some lights only: lit if another one reaches it at all.
            all = true;
            for (const light::Source &src : l.sources)
            {
              float ddx = src.x - (float(px) + 0.5f), ddy = src.y - (float(py) + 0.5f);
              if (light::falloff(l, ddx * ddx + ddy * ddy) > 0 && !room.solid.shadowedFrom(float(px) + 0.5f, float(py) + 0.5f, l, src)) all = false;
            }
          }
          if (all)
          {
            shadowEdge = true;
            continue;
          }
          int k = light::nearest(normals, n);
          t[size_t(y * 8 + x)] = uint8_t(1 + k);
          normalError += std::abs(truth -
                                  std::max(0.0f, light::dot(normals[size_t(k)], centres[size_t(region)])));
          normalPixels++;
          any = true;
        }
      if (!any)
      {
        m.u16(0);
        continue;
      }
      edgeTiles += shadowEdge;
      auto it = tileOf.find(t);
      uint32_t tile;
      if (it != tileOf.end()) tile = it->second;
      else
      {
        tile = uint32_t(area.uniqueTiles.size() + area.rawTiles.size());
        area.rawTiles.push_back(t);
        tileOf[t] = tile;
      }
      m.u16(tile | uint32_t(firstBank + region) << 12);
    }

  Bytes lite;
  lightHead(lite, l, uint32_t(normals.size()), uint32_t(firstBank), uint32_t(centres.size()));
  for (size_t i = 0; i < size_t(light::LAYER_NORMALS); i++)
  {
    light::Vec3 n = i < normals.size() ? normals[i] : light::Vec3{};
    lite.u8(uint32_t(int8_t(std::lround(n.x * 127))) & 255);
    lite.u8(uint32_t(int8_t(std::lround(n.y * 127))) & 255);
    lite.u8(uint32_t(int8_t(std::lround(n.z * 127))) & 255);
    lite.u8(0);
  }
  for (const light::Vec3 &f : centres)
  {
    lite.s16(int32_t(std::lround(f.x * 4096)));
    lite.s16(int32_t(std::lround(f.y * 4096)));
    lite.s16(int32_t(std::lround(f.z * 4096)));
    lite.u16(0);
  }
  report.info("lighting", room.map->path, "light layer: " + std::to_string(area.rawTiles.size()) + " normal tiles (" +
              std::to_string(edgeTiles) + " cells on a shadow's edge), " + std::to_string(normals.size()) + " normals, " +
              std::to_string(centres.size()) + " region banks from bank " + std::to_string(firstBank) + ", " +
              std::to_string(litCells.size()) + " lit cells, mean error " + std::to_string(normalPixels ? normalError / double(normalPixels) : 0.0));
  lightSources(lite, l);
  return {m, lite};
}

}  // namespace

std::vector<uint8_t> cook(const std::vector<tmx::Map> &maps, const CookOptions &options, Report &report)
{
  const Profile &profile = options.profile;
  report.profile = profile.name;

  // Rooms and areas
  std::vector<RoomWork> rooms(maps.size());
  std::vector<AreaWork> areas;
  std::set<std::string> roomIds;
  for (size_t i = 0; i < maps.size(); i++)
  {
    rooms[i].map = &maps[i];
    rooms[i].id = roomIdOf(maps[i]);
    if (!roomIds.insert(rooms[i].id).second)
      report.error("does-not-fit", maps[i].path, "room id '" + rooms[i].id + "' is used twice");
    // A lit room (lighting.md) is an area of its own: its tiles and palettes are its light's.
    rooms[i].light = light::find(maps[i], report);
    if (rooms[i].light.mode != light::Mode::None)
    {
      Report quiet;
      rooms[i].solid = light::Solid::fromChunk(collisionChunk(maps[i], quiet).data);
    }
    std::string areaName = rooms[i].light.mode != light::Mode::None ? "@lit:" + rooms[i].id : maps[i].property("area", "default");
    auto it = std::find_if(areas.begin(), areas.end(), [&](const AreaWork &a) { return a.name == areaName; });
    if (it == areas.end())
    {
      areas.emplace_back();
      areas.back().name = areaName;
      it = areas.end() - 1;
    }
    it->rooms.push_back(&rooms[i]);

    for (const tmx::Layer &l : maps[i].layers)
    {
      std::string role = l.role();
      static const std::set<std::string> known = {"background", "main", "foreground", "collision", "collision_material"};
      if (role.empty())
        report.warning("layer-dropped", maps[i].path + " layer '" + l.name + "'", "layer without role is not cooked");
      else if (!known.count(role))
        report.error("does-not-fit", maps[i].path + " layer '" + l.name + "'", "unknown layer role '" + role + "'");
    }
  }
  if (areas.size() > 255) report.error("does-not-fit", "project", "more than 255 areas");

  ColorConverter colors;
  colors.matrix = options.colorMatrix;
  colors.profile = profile.name;
  colors.report = &report;
  colors.info = options.pInfo;

  BmPackWriter writer;
  bmPackWriterInit(&writer, profile.fourcc, options.layout);
  StringTable strings;
  // The pack's room index of each room id (the order of the ROOM chunks: by area), and the
  // area group, for the world map.
  std::map<std::string, uint16_t> roomPackIndex;
  std::map<std::string, uint8_t> roomArea;
  SharedChunks shared;
  // Per room the maps and collision it reads, with their sizes: compressed afterwards where
  // every room that reads them fits the runtime's unpack buffer (BM_PACK_ROOM_UNPACK_BYTES).
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> roomLevelChunks;
  std::vector<std::string> roomLevelIds;

  // The string table (group 0) is written last, once all names are known; the runtime finds
  // it by its chunk id.

  for (size_t a = 0; a < areas.size(); a++)
  {
    AreaWork &area = areas[a];
    area.uniqueTiles.push_back(Tile{});  // tile 0: empty
    area.uniqueTiles[0].fill(TRANSPARENT);
    area.tileIndex[area.uniqueTiles[0]] = 0;
    area.firstUse.push_back("(empty tile)");
    for (RoomWork *room : area.rooms) collectTiles(*room, area, colors, profile, report);

    if (int(area.uniqueTiles.size()) > profile.maxTiles)
    {
      report.error("does-not-fit", "area '" + area.name + "'",
                   std::to_string(area.uniqueTiles.size()) + " different 8x8 tiles; profile " + profile.name +
                   " can address " + std::to_string(profile.maxTiles));
    }
    packPalettes(area, profile, report);
    // The light layer (lighting.md, mode `layer`): its normal tiles after the area's, banks after
    // its banks, a map and the light's table; made now so the tiles go into the area's chunk.
    std::map<const RoomWork *, std::pair<Bytes, Bytes>> lightLayers;   // map, LITE
    for (RoomWork *room : area.rooms)
      if (room->light.mode == light::Mode::Layer) lightLayers[room] = buildLightLayer(*room, area, report);

    uint16_t areaGroup = uint16_t(1 + a);
    uint16_t backdrop = parseColour(area.rooms.front()->map->property("backdrop", "#000000"));

    Bytes vlay;
    vlay.u16(0);
    vlay.u16(uint32_t(area.uniqueTiles.size() + area.rawTiles.size()));
    vlay.u16(uint32_t(area.uniqueTiles.size() + area.rawTiles.size()));
    vlay.u16(0);
    vlay.u16(0); vlay.u16(0); vlay.u16(0); vlay.u16(0);
    vlay.u8((1u << area.banks.size()) - 1);
    vlay.u8(0);
    vlay.u16(0);
    vlay.u16(0xF000);  // screen blocks 28-31 hold the room maps
    vlay.u16(0);
    uint32_t vlayChunk = add(writer, BM_CHUNK_VLAY, BM_CATEGORY_GRAPHICS, areaGroup, vlay);
    uint32_t paleChunk = add(writer, BM_CHUNK_PALE, BM_CATEGORY_GRAPHICS, areaGroup, paletteChunk(area, backdrop));
    uint32_t tileChunkIndex = add(writer, BM_CHUNK_TILE, BM_CATEGORY_GRAPHICS, areaGroup, tileChunk(area, profile));

    size_t merged = 0;
    for (const auto &[c, src] : colors.sources) merged += src.size() > 1 ? 1 : 0;
    if (merged)
      report.info("colour-merged", "area '" + area.name + "'",
                  std::to_string(merged) + " hardware colours each stand for several source colours");
    report.info("tiles", "area '" + area.name + "'",
                std::to_string(area.uniqueTiles.size()) + " different 8x8 tiles (" +
                std::to_string(area.uniqueTiles.size() * 32) + " bytes), " + std::to_string(area.banks.size()) + " palette banks");

    for (RoomWork *room : area.rooms)
    {
      const tmx::Map &map = *room->map;
      uint16_t roomGroup = uint16_t(256 + (room - rooms.data()));

      struct LayerEntry { uint32_t role, priority, chunk; float parallaxX, parallaxY; };
      std::vector<LayerEntry> layerChunks;
      // The runtime gives each layer two of six screen blocks; the light layer is one of them.
      size_t layerCount = room->layers.size() + (room->light.mode == light::Mode::Layer ? 1 : 0);
      if (layerCount > 3)
        report.error("layers", map.path, "a room can have at most 3 graphics layers (it has " + std::to_string(room->layers.size()) +
                     (room->light.mode == light::Mode::Layer ? ", and the light layer takes one" : "") + ")");
      std::vector<std::pair<uint32_t, uint32_t>> levelChunks;
      // The light layer first: as BG 0 it lies in front of the others of its priority.
      uint32_t liteChunk = 0;
      if (room->light.mode == light::Mode::Layer)
      {
        const auto &[m, lite] = lightLayers[room];
        uint32_t chunk = shared.add(writer, BM_CHUNK_TMAP, BM_CATEGORY_LEVEL, roomGroup, m);
        layerChunks.push_back({3, 1, chunk, 1.0f, 1.0f});
        levelChunks.emplace_back(chunk, uint32_t(m.data.size()));
        liteChunk = add(writer, BM_CHUNK_LITE, BM_CATEGORY_LEVEL, roomGroup, lite);
      }
      else if (room->light.mode == light::Mode::Palette)
      {
        liteChunk = add(writer, BM_CHUNK_LITE, BM_CATEGORY_LEVEL, roomGroup, paletteLightChunk(*room, area, report));
      }
      for (const GraphicsLayer &gl : room->layers)
      {
        static const uint8_t priorities[3] = {2, 1, 0};
        Bytes m = mapChunk(gl, area, profile);
        uint32_t chunk = shared.add(writer, BM_CHUNK_TMAP, BM_CATEGORY_LEVEL, roomGroup, m);
        layerChunks.push_back({uint32_t(gl.role), priorities[gl.role], chunk, gl.layer->parallaxX, gl.layer->parallaxY});
        levelChunks.emplace_back(chunk, uint32_t(m.data.size()));
      }
      Bytes coll = collisionChunk(map, report);
      uint32_t collChunk = shared.add(writer, BM_CHUNK_COLL, BM_CATEGORY_LEVEL, roomGroup, coll);
      levelChunks.emplace_back(collChunk, uint32_t(coll.data.size()));
      roomLevelChunks.push_back(levelChunks);
      roomLevelIds.push_back(room->id);
      uint16_t objectCount = 0;
      uint32_t objsChunk = add(writer, BM_CHUNK_OBJS, BM_CATEGORY_OBJECTS, roomGroup, objectsChunk(map, strings, report, objectCount));

      // Doors must lead to a room of this cook.
      for (const tmx::ObjectGroup &g : map.objectGroups)
        for (const tmx::Object &o : g.objects)
          if (o.type == "Door")
          {
            auto t = o.properties.find("target_room");
            if (t == o.properties.end() || !roomIds.count(t->second))
              report.warning("door-target", map.path + " object " + std::to_string(o.id),
                             "target room '" + (t == o.properties.end() ? std::string() : t->second) + "' is not part of this cook");
          }

      Bytes r;
      r.u16(uint32_t(map.width * map.tileWidth));
      r.u16(uint32_t(map.height * map.tileHeight));
      r.u8(uint32_t(map.tileWidth));
      r.u8(uint32_t(layerChunks.size()));
      r.u16(map.property("edges_solid", "true") == "true" ? 1 : 0);
      r.s32(toFix(numberProperty(map, "gravity", 0.25)));
      r.s32(toFix(numberProperty(map, "max_fall_speed", 4.0)));
      r.s32(toFix(numberProperty(map, "water_gravity", 0.08)));
      r.s32(toFix(numberProperty(map, "water_max_fall_speed", 1.5)));
      std::string music = map.property("music");
      r.u16(music.empty() ? 0xFFFF : strings.id(music));
      r.u16(objectCount);
      r.u8(uint32_t(areaGroup));
      r.u8(3);  // dependencies: VLAY, PALE, TILE of the area
      r.u16(tileChunkIndex);
      r.u16(paleChunk);
      r.u16(collChunk);
      r.u16(objsChunk);
      r.u16(vlayChunk);
      r.u16(strings.id(room->id));
      r.u16(liteChunk);   // the light (LITE), 0: none
      for (const LayerEntry &l : layerChunks)
      {
        r.u8(l.role);
        r.u8(l.priority);
        r.u16(l.chunk);
        r.s32(toFix(l.parallaxX));
        r.s32(toFix(l.parallaxY));
      }
      r.u16(vlayChunk);
      r.u16(paleChunk);
      r.u16(tileChunkIndex);
      r.align4();
      roomArea[room->id] = uint8_t(areaGroup);
      roomPackIndex[room->id] = uint16_t(roomPackIndex.size());
      add(writer, BM_CHUNK_ROOM, BM_CATEGORY_LEVEL, roomGroup, r);
    }
  }

  // Maps and collision compressed (pack-format.md §4): a chunk is, if every room that reads it
  // has all its compressed chunks fit the runtime's buffer together, as the runtime unpacks them.
  {
    std::map<uint32_t, bool> fits;
    for (const auto &chunks : roomLevelChunks)
    {
      uint32_t need = 0;
      for (const auto &[chunk, size] : chunks) need += (size + 3u) & ~3u;
      for (const auto &[chunk, size] : chunks)
        fits[chunk] = (fits.count(chunk) ? fits[chunk] : true) && need <= BM_PACK_ROOM_UNPACK_BYTES;
      if (need > BM_PACK_ROOM_UNPACK_BYTES)
        report.info("uncompressed", "room '" + roomLevelIds[size_t(&chunks - roomLevelChunks.data())] + "'",
                    "maps and collision take " + std::to_string(need) + " bytes unpacked, more than the runtime's buffer (" +
                    std::to_string(BM_PACK_ROOM_UNPACK_BYTES) + "): stored uncompressed");
    }
    uint32_t before = 0, after = 0;
    for (const auto &[chunk, ok] : fits)
    {
      if (!ok) continue;
      before += writer.pChunks[chunk].entry.size;
      bmPackWriterCompress(&writer, chunk);
      after += writer.pChunks[chunk].entry.size;
    }
    if (before != 0)
      report.info("compressed", "project", "maps and collision: " + std::to_string(before) + " bytes compressed to " + std::to_string(after));
  }

  // Sprites: global for now (every room may use every sprite).
  std::set<std::string> spriteNames;
  std::map<std::string, std::pair<int, int>> spriteMemory;   // name: tiles, palettes (the room report)
  std::map<std::string, std::string> classSprites;           // a script class's sprite
  std::map<std::string, std::vector<std::string>> spritePoints, spritePalettes;   // for checking figures
  for (const std::string &path : options.sprites)
  {
    try
    {
      sprite::Sprite s = sprite::load(path);
      if (!spriteNames.insert(s.name).second)
      {
        report.error("does-not-fit", path, "sprite name '" + s.name + "' is used twice");
        continue;
      }
      sprite::Cooked cooked = sprite::cook(s, profile, report);
      std::vector<uint16_t> names;
      for (const sprite::Animation &a : s.animations) names.push_back(strings.id(a.name));
      Bytes b;
      std::vector<uint16_t> boxNames;
      for (const std::string &n : cooked.boxNames) boxNames.push_back(strings.id(n));
      std::vector<uint16_t> paletteNames{strings.id("default")};
      for (const std::string &n : cooked.paletteNames) paletteNames.push_back(strings.id(n));
      std::vector<uint16_t> pointNames;
      for (const std::string &n : cooked.pointNames) pointNames.push_back(strings.id(n));
      b.data = sprite::chunk(s, cooked, strings.id(s.name), names, boxNames, paletteNames, pointNames);
      spriteMemory[s.name] = {int(cooked.tiles.size()), int(paletteNames.size())};
      spritePoints[s.name] = cooked.pointNames;
      spritePalettes[s.name] = cooked.paletteNames;
      add(writer, BM_CHUNK_SPRT, BM_CATEGORY_ACTORS, 0, b);
    }
    catch (const std::exception &e)
    {
      report.error("does-not-fit", path, e.what());
    }
  }
  // Figures (figure_asset.h): their sprites, points and palettes must be there.
  std::set<std::string> figureNames;
  std::map<std::string, std::vector<std::string>> figureSprites;   // a figure's sprites, each once
  for (const std::string &path : options.figures)
  {
    try
    {
      figure::Figure f = figure::load(path);
      if (!figureNames.insert(f.name).second)
      {
        report.error("does-not-fit", path, "figure name '" + f.name + "' is used twice");
        continue;
      }
      bool ok = true;
      for (const figure::Layer &l : f.layers)
      {
        std::string where = path + " layer '" + l.name + "'";
        if (!l.sprite.empty() && !spriteNames.count(l.sprite))
        {
          report.error("figure", where, "no sprite '" + l.sprite + "'");
          ok = false;
          continue;
        }
        const figure::Layer &parent = f.layers[size_t(std::max(0, figure::layerIndex(f, l.parent)))];
        const std::vector<std::string> &points = spritePoints[parent.sprite];
        if (!l.point.empty() && !parent.sprite.empty() && std::find(points.begin(), points.end(), l.point) == points.end())
          report.warning("figure", where, "sprite '" + parent.sprite + "' has no point '" + l.point + "': the layer is placed at its anchor");
        const std::vector<std::string> &palettes = spritePalettes[l.sprite];
        if (!l.palette.empty() && std::find(palettes.begin(), palettes.end(), l.palette) == palettes.end())
          report.warning("figure", where, "sprite '" + l.sprite + "' has no palette '" + l.palette + "': the default is used");
      }
      if (!ok) continue;
      for (const figure::Layer &l : f.layers)
        if (!l.sprite.empty() && std::find(figureSprites[f.name].begin(), figureSprites[f.name].end(), l.sprite) == figureSprites[f.name].end())
          figureSprites[f.name].push_back(l.sprite);
      Bytes b;
      b.data = figure::chunk(f, [&](const std::string &n) { return strings.id(n); });
      add(writer, BM_CHUNK_FIGR, BM_CATEGORY_ACTORS, 0, b);
    }
    catch (const std::exception &e)
    {
      report.error("does-not-fit", path, e.what());
    }
  }


  // Songs and sound effects, one chunk each.
  std::set<std::string> songNames;
  for (const std::string &path : options.songs)
  {
    try
    {
      song::Song s = song::load(path);
      if (!songNames.insert(s.name).second)
      {
        report.error("does-not-fit", path, "song name '" + s.name + "' is used twice");
        continue;
      }
      Bytes b;
      b.data = song::chunk(s, strings.id(s.name), report);
      if (!b.data.empty()) add(writer, BM_CHUNK_SONG, s.effect ? BM_CATEGORY_SFX : BM_CATEGORY_MUSIC, 0, b);
    }
    catch (const std::exception &e)
    {
      report.error("does-not-fit", path, e.what());
    }
  }
  // Fonts, one chunk each.
  // Several files may make up one font (by name) if they do not share characters.
  std::map<std::string, std::vector<font::Range>> fontRanges;
  for (const std::string &path : options.fonts)
  {
    try
    {
      font::Font f = font::load(path);
      bool clash = false;
      for (const font::Range &r : f.ranges)
        for (const font::Range &o : fontRanges[f.name])
          clash |= r.first < o.first + o.count && o.first < r.first + r.count;
      if (clash)
      {
        report.error("does-not-fit", path, "font '" + f.name + "' has some of these characters in another file already");
        continue;
      }
      fontRanges[f.name].insert(fontRanges[f.name].end(), f.ranges.begin(), f.ranges.end());
      Bytes b;
      b.data = font::chunk(f, strings.id(f.name));
      add(writer, BM_CHUNK_FONT, BM_CATEGORY_GRAPHICS, 0, b);
    }
    catch (const std::exception &e)
    {
      report.error("does-not-fit", path, e.what());
    }
  }
  // A room's music must be a song of this cook.
  for (const tmx::Map &map : maps)
  {
    std::string music = map.property("music");
    // "none": the music fades away in this room (runtime-core.md §6).
    if (!music.empty() && music != "none" && !songNames.count(music))
      report.warning("music", map.path, "the room's music '" + music + "' is not a song of this cook");
  }

  if (options.scripts.empty() && !options.data.empty())
    report.error("data", options.data[0], "records need the structures declared in the scripts: cook the scripts too");
  // Scripts: all classes in one module.
  if (!options.scripts.empty())
  {
    // In order of their file names, not of the command line: string ids are given out in the
    // order of first use, and a script's module should only change when that script does.
    std::vector<std::string> scriptPaths = options.scripts;
    std::stable_sort(scriptPaths.begin(), scriptPaths.end(), [](const std::string &x, const std::string &y) {
      std::string bx = x.substr(x.find_last_of("/\\") + 1), by = y.substr(y.find_last_of("/\\") + 1);
      return bx != by ? bx < by : x < y;
    });
    std::vector<script::Source> sources;
    for (const std::string &path : scriptPaths)
    {
      try
      {
        sources.push_back(script::readSource(path));
      }
      catch (const std::exception &e)
      {
        report.error("script", path, e.what());
      }
    }
    script::Module module = script::compile(sources, [&](const std::string &s) { return strings.id(s); });
    for (const script::Diagnostic &d : module.diagnostics)
    {
      std::string where = d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.column);
      if (d.warning) report.warning("script", where, d.message);
      else report.error("script", where, d.message);
    }
    if (module.ok())
    {
      // One module per class (attachable-scripts.md §4): a changed script changes one chunk.
      size_t words = 0, bytes = 0;
      for (const script::ClassModule &cm : module.classModules)
      {
        Bytes b;
        b.data = cm.data;
        add(writer, BM_CHUNK_MODB, BM_CATEGORY_SCRIPTS, 0, b);
        bytes += cm.data.size();
      }
      for (const script::ClassInfo &c : module.classes) words += size_t(c.codeWords);
      for (const script::ClassInfo &c : module.classes)
        if (!c.sprite.empty()) classSprites[c.name] = c.sprite;
      // The runtime holds 40 modules (src/world/actors.h, BM_ACTORS_MODULES): more are not loaded.
      if (module.classModules.size() > 40)
        report.error("script", "modules", std::to_string(module.classModules.size()) + " classes: the runtime loads 40");
      report.info("script", "modules", std::to_string(module.classes.size()) + " classes, " + std::to_string(words * 2) + " bytes of code, " +
                                         std::to_string(bytes) + " bytes in the pack");
      for (const script::StructInfo &s : module.structs)
        report.info("structure", s.file, s.name + (s.base.empty() ? "" : " is " + s.base) + ": " + std::to_string(s.fields.size()) +
                                             " fields, " + std::to_string(s.size) + " bytes per record");
      // Records: one table per structure, global (structures.md §4).
      for (const DataTable &t : cookData(options.data, module, [&](const std::string &s) { return strings.id(s); }, report))
      {
        Bytes d;
        d.data = t.chunk;
        add(writer, BM_CHUNK_DTAB, BM_CATEGORY_OBJECTS, 0, d);
      }
      if (!options.scriptC.empty() || !options.nativeCompiler.empty())
      {
        // One native image with the code of every class's module.
        std::vector<script::NativeSource> natives;
        for (size_t i = 0; i < module.classModules.size(); i++)
        {
          const std::vector<uint8_t> &d = module.classModules[i].data;
          natives.push_back({&module, &d, bmCrc32(0, d.data(), d.size()), i});
        }
        std::string c = script::emitC(natives);
        if (!options.scriptC.empty()) std::ofstream(options.scriptC) << c;
        if (!options.nativeCompiler.empty())
        {
          try
          {
            Bytes n;
            n.data = buildNativeModule(c, {options.nativeCompiler, options.nativeIncludeDir, options.nativeElf});
            add(writer, BM_CHUNK_MODN, BM_CATEGORY_SCRIPTS, 0, n);
            report.info("script", "native module", std::to_string(n.data.size()) + " bytes of native code and addresses in the pack");
          }
          catch (const std::exception &e)
          {
            report.error("script", "native module", e.what());
          }
        }
      }
    }
  }

  // Languages: the main one first (no translations), then one chunk per language file. A
  // translation is found by its original among the pack's strings.
  if (!options.languages.empty())
  {
    std::vector<lang::Language> languages;
    lang::Language main;
    main.code = options.mainLanguage;
    main.name = options.mainLanguageName;
    languages.push_back(main);
    // A language may come in several files (a part of the game with texts of its own, kept
    // beside it: Mitsuki's prologue): their texts are put together, the first file's name kept.
    for (const std::string &path : options.languages)
    {
      try
      {
        lang::Language l = lang::load(path);
        auto same = std::find_if(languages.begin() + 1, languages.end(), [&](const lang::Language &o) { return o.code == l.code; });
        if (same == languages.end())
        {
          languages.push_back(std::move(l));
          continue;
        }
        same->texts.insert(same->texts.end(), l.texts.begin(), l.texts.end());
        same->lines.insert(same->lines.end(), l.lines.begin(), l.lines.end());
        same->files.insert(same->files.end(), l.files.begin(), l.files.end());
      }
      catch (const std::exception &e)
      {
        report.error("text", path, e.what());
      }
    }
    std::set<std::string> codes;
    for (const lang::Language &l : languages)
    {
      if (!codes.insert(l.code).second)
      {
        report.error("text", l.path, "language '" + l.code + "' is the project's main language");
        continue;
      }
      uint16_t code = strings.id(l.code), name = strings.id(l.name);
      std::map<uint16_t, std::string> entries;
      int unused = 0;
      for (size_t i = 0; i < l.texts.size(); i++)
      {
        int id = strings.find(l.texts[i].first);
        if (id < 0)
        {
          if (unused++ < 5)
            report.warning("text", l.files[i] + ":" + std::to_string(l.lines[i]), "\"" + l.texts[i].first + "\" is no text of this pack");
          continue;
        }
        entries[uint16_t(id)] = l.texts[i].second;
      }
      if (unused > 5) report.warning("text", l.path, std::to_string(unused - 5) + " more translations of texts this pack does not have");
      // Characters no font of the pack has (they would show as '?'): what a new language,
      // Japanese say, needs a font for.
      if (!fontRanges.empty())
      {
        std::set<uint32_t> missing;
        for (const auto &[id, text] : entries)
          for (size_t k = 0; k < text.size();)
          {
            uint8_t c = uint8_t(text[k]);
            int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
            uint32_t code = more == 0 ? c : more == 1 ? c & 0x1Fu : more == 2 ? c & 0x0Fu : c & 0x07u;
            for (int m = 1; m <= more && k + size_t(m) < text.size(); m++) code = (code << 6) | (uint8_t(text[k + size_t(m)]) & 0x3Fu);
            k += size_t(more) + 1;
            if (code == '\n' || code == ' ') continue;
            bool found = false;
            for (const auto &[name, ranges] : fontRanges)
              for (const font::Range &r : ranges) found |= code >= r.first && code < r.first + r.count;
            if (!found) missing.insert(code);
          }
        if (!missing.empty())
        {
          std::string list;
          int shown = 0;
          for (uint32_t code : missing)
            if (shown++ < 12)
            {
              char hex[16];
              std::snprintf(hex, sizeof(hex), " U+%04X", unsigned(code));
              list += hex;
            }
          report.warning("text", l.path, "language " + l.code + ": " + std::to_string(missing.size()) + " characters no font has (shown as ?):" + list);
        }
      }
      Bytes b;
      b.u16(code);
      b.u16(name);
      b.u16(uint32_t(entries.size()));
      b.u16(0);
      uint32_t offset = 8 + 8 * uint32_t(entries.size());
      for (const auto &[id, text] : entries)
      {
        b.u16(id);
        b.u16(0);
        b.u32(offset);
        offset += uint32_t(text.size()) + 1;
      }
      for (const auto &[id, text] : entries)
      {
        b.data.insert(b.data.end(), text.begin(), text.end());
        b.u8(0);
      }
      if (!l.path.empty()) report.info("text", l.path, "language " + l.code + ": " + std::to_string(entries.size()) + " texts translated");
      add(writer, BM_CHUNK_LANG, BM_CATEGORY_TEXT, 0, b);
    }
  }
  // Sprite memory per room (layered-sprites.md §3): what a room loads when it is entered (its
  // objects' sprites, door panels, the puzzle's, each in the palettes its objects ask for) on
  // top of the player's sheet and the built-in graphics; effects and objects made later come
  // on top of that.
  if (!spriteMemory.empty())
  {
    auto lower = [](std::string t) {
      for (char &ch : t) ch = char(std::tolower(static_cast<unsigned char>(ch)));
      return t;
    };
    std::map<std::string, std::string> byLower;
    for (const auto &e : spriteMemory) byLower[lower(e.first)] = e.first;
    // The player's sprites stay in sprite memory, each in a bank of its own from bank 0: those
    // of the figure "player", else the sprite "player". A pack with playable characters to
    // choose from (figures "player_<name>", sprite-format.md "Figures") keeps room for the
    // largest of them; no room draws any of their sprites.
    std::vector<std::string> playerSprites;
    int playerTiles = 0, playerBanks = 0;
    auto playable = [](const std::string &n) { return n == "player" || n.rfind("player_", 0) == 0; };
    for (const auto &[name, sprites] : figureSprites)
    {
      if (!playable(name)) continue;
      int tiles = 0;
      for (const std::string &n : sprites) tiles += spriteMemory.count(n) ? spriteMemory[n].first : 0;
      playerTiles = std::max(playerTiles, tiles);
      playerBanks = std::max(playerBanks, int(sprites.size()));
      playerSprites.insert(playerSprites.end(), sprites.begin(), sprites.end());
    }
    if (playerSprites.empty())
    {
      playerSprites = {"player"};
      playerTiles = spriteMemory.count("player") ? spriteMemory["player"].first : 0;
      playerBanks = 1;
    }
    const int freeTiles = 1024 - 12 - playerTiles, banks = 13 - playerBanks;
    for (const RoomWork &r : rooms)
    {
      std::map<std::string, std::set<std::string>> used;   // sprite: palettes
      for (const tmx::ObjectGroup &og : r.map->objectGroups)
        for (const tmx::Object &o : og.objects)
        {
          std::string sprite;
          auto cs = classSprites.find(o.type);
          if (cs != classSprites.end()) sprite = cs->second;
          else if (byLower.count(lower(o.type))) sprite = byLower[lower(o.type)];
          auto panel = o.properties.find("panel");
          if (panel != o.properties.end()) sprite = panel->second;
          if (o.type == "PushBlock" || o.type == "Gate" || o.type == "PressurePlate") sprite = "puzzle";
          if (sprite.empty() || !spriteMemory.count(sprite) ||
              std::find(playerSprites.begin(), playerSprites.end(), sprite) != playerSprites.end())
            continue;
          auto pal = o.properties.find("palette");
          used[sprite].insert(pal != o.properties.end() ? pal->second : "default");
        }
      int tiles = 0, pairs = 0;
      std::string list;
      for (const auto &u : used)
      {
        tiles += spriteMemory[u.first].first;
        pairs += int(u.second.size());
        list += (list.empty() ? "" : ", ") + u.first + (u.second.size() > 1 ? " (" + std::to_string(u.second.size()) + " palettes)" : "");
      }
      std::string line = std::to_string(tiles) + " of " + std::to_string(freeTiles) + " sprite tiles, " + std::to_string(pairs) + " of " +
                         std::to_string(banks) + " palette banks" + (list.empty() ? "" : ": " + list);
      if (tiles > freeTiles || pairs > banks)
        report.warning("sprite-memory", "room '" + r.id + "'", line + " (more than there is: some are not drawn)");
      else
        report.info("sprite-memory", "room '" + r.id + "'", line);
    }
  }
  if (shared.savedBytes != 0)
    report.info("shared", "project", std::to_string(shared.savedBytes) + " bytes of maps and collision are the same as another room's and stored once");
  // The world map (world_asset.h): where the rooms lie, their walls and doors.
  if (!options.world.empty())
  {
    try
    {
      world::World w = world::load(options.world);
      std::vector<world::RoomInfo> infos;
      for (const RoomWork &room : rooms) infos.push_back(world::roomInfo(*room.map, room.id));
      world::Layout l = world::layout(w, infos);
      for (const world::RoomEntry &e : w.rooms)
        if (!roomPackIndex.count(e.id)) report.info("world", options.world, "room '" + e.id + "' is not part of this cook");
      for (const world::Layout::Problem &p : l.warnings) report.warning("world", options.world + " room '" + p.room + "'", p.message);
      for (const world::PlacedRoom &p : l.rooms)
        if (p.automatic)
          report.info("world", options.world + " room '" + p.id + "'",
                      "placed by its doors at " + std::to_string(p.x) + ", " + std::to_string(p.y));
      for (const std::string &id : l.unplaced)
      {
        const world::RoomEntry *e = w.find(id);
        if (e == nullptr || !e->hidden) report.info("world", options.world + " room '" + id + "'", "not on the map (no door leads to it from the map)");
      }
      if (l.rooms.size() > world::MAX_ROOMS)
        report.error("does-not-fit", options.world, std::to_string(l.rooms.size()) + " rooms on the map, at most " + std::to_string(world::MAX_ROOMS));
      if (l.cellCount() > world::MAX_CELLS)
        report.error("does-not-fit", options.world, std::to_string(l.cellCount()) + " map cells, at most " + std::to_string(world::MAX_CELLS));
      for (const world::PlacedRoom &p : l.rooms)
        if (p.w > 255 || p.h > 255) report.error("does-not-fit", options.world + " room '" + p.id + "'", "more than 255 cells across");
      Bytes b;
      b.data = world::chunk(l, roomPackIndex, roomArea);
      report.info("world", options.world, std::to_string(l.rooms.size()) + " rooms, " + std::to_string(l.cellCount()) + " cells, " +
                  std::to_string(b.data.size()) + " bytes");
      add(writer, BM_CHUNK_WRLD, BM_CATEGORY_LEVEL, 0, b);
    }
    catch (const std::exception &e)
    {
      report.error("does-not-fit", options.world, e.what());
    }
  }
  if (!options.schemaHistory.empty())
  {
    try
    {
      add(writer, BM_CHUNK_MIGR, BM_CATEGORY_META, 0, migrationChunk(options.schemaHistory, report));
    }
    catch (const std::exception &e)
    {
      report.error("does-not-fit", options.schemaHistory, e.what());
    }
  }
  if (profile.name == "gba" || profile.name == "host")
  {
    Bytes ccor;
    ccor.data = colorCorrectionChunk(options.colorCorrection);
    if (!ccor.data.empty()) add(writer, BM_CHUNK_CCOR, BM_CATEGORY_META, 0, ccor);
  }
  add(writer, BM_CHUNK_STRS, BM_CATEGORY_TEXT, 0, strings.chunk());

  for (const auto &[asset, used] : colors.overridesUsed)
    report.info("colour-override", asset,
                std::to_string(used.size()) + " source colours replaced for profile " + profile.name + " (" + overridesPath(asset) + ")");

  // Left out for an update to bring (pack sets, pack-format.md §0): the rooms asked for, and
  // what only they use (their maps, collision, objects; an area's graphics no kept room
  // shows), and every chunk of the kinds asked for. The entries stay: chunk numbers and string
  // ids are the same as in the full pack, so an update made from it fits this one.
  if (!options.absentRooms.empty() || !options.absentKinds.empty() || !options.absentAssets.empty())
  {
    std::set<uint16_t> absentIds;
    for (const std::string &id : options.absentRooms)
    {
      if (!roomIds.count(id)) report.warning("absent", "room '" + id + "'", "no room has this id");
      absentIds.insert(strings.id(id));
    }
    std::vector<int> users(writer.chunkCount, 0), absentUsers(writer.chunkCount, 0);
    std::vector<bool> absent(writer.chunkCount, false);
    for (uint32_t k = 0; k < writer.chunkCount; k++)
    {
      const BmPackWriterChunk &c = writer.pChunks[k];
      if (c.entry.id != BM_CHUNK_ROOM || c.entry.size < 44) continue;
      const uint8_t *r = c.pData;
      bool gone = absentIds.count(uint16_t(r[40] | r[41] << 8)) != 0;
      absent[k] = gone;
      std::vector<uint32_t> refs = {uint32_t(r[30] | r[31] << 8), uint32_t(r[32] | r[33] << 8), uint32_t(r[34] | r[35] << 8),
                                    uint32_t(r[36] | r[37] << 8), uint32_t(r[38] | r[39] << 8)};
      for (uint32_t l = 0; l < r[5] && 44u + l * 12u + 4u <= c.entry.size; l++) refs.push_back(uint32_t(r[44 + l * 12 + 2] | r[44 + l * 12 + 3] << 8));
      if (uint32_t lite = uint32_t(r[42] | r[43] << 8); lite != 0) refs.push_back(lite);   // the room's light (0: not lit)
      std::sort(refs.begin(), refs.end());
      refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
      for (uint32_t ref : refs)
        if (ref < writer.chunkCount)
        {
          users[ref]++;
          if (gone) absentUsers[ref]++;
        }
    }
    std::set<uint32_t> kinds;
    for (const std::string &kind : options.absentKinds)
    {
      if (kind.size() != 4) report.warning("absent", kind, "a kind of chunk is four characters (LANG, MODB, ...)");
      else kinds.insert(BM_FOURCC(kind[0], kind[1], kind[2], kind[3]));
    }
    // Sprites and songs by name: both start with the string id of their name.
    std::set<uint16_t> assetIds;
    for (const std::string &name : options.absentAssets)
    {
      if (!spriteNames.count(name) && !songNames.count(name)) report.warning("absent", "asset '" + name + "'", "no sprite or song has this name");
      assetIds.insert(strings.id(name));
    }
    uint32_t chunks = 0, bytes = 0;
    for (uint32_t k = 0; k < writer.chunkCount; k++)
    {
      BmPackWriterChunk &c = writer.pChunks[k];
      bool named = (c.entry.id == BM_CHUNK_SPRT || c.entry.id == BM_CHUNK_SONG) && c.entry.size >= 2 &&
                   assetIds.count(uint16_t(c.pData[0] | c.pData[1] << 8)) != 0;
      bool gone = absent[k] || (users[k] != 0 && users[k] == absentUsers[k]) || kinds.count(c.entry.id) != 0 || named;
      if (!gone || c.entry.id == BM_CHUNK_STRS) continue;
      c.entry.flags = BM_PACK_ENTRY_ABSENT;
      chunks++;
      bytes += c.entry.size;
    }
    report.info("absent", "project", std::to_string(chunks) + " chunks left out (" + std::to_string(bytes) +
                                         " bytes): an update brings them (pack sets)");
  }

  std::vector<uint8_t> out;
  if (!report.hasErrors())
  {
    uint32_t size = 0;
    uint8_t *data = bmPackWriterFinish(&writer, &size);
    out.assign(data, data + size);
    free(data);
  }
  bmPackWriterFree(&writer);
  return out;
}

}  // namespace bm
