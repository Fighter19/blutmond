// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "cook.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
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

  uint16_t convert(const uint8_t *rgba)
  {
    // Fully or mostly transparent pixels, and magenta as the classic colour key.
    if (rgba[3] < 128 || (rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 255)) return TRANSPARENT;
    float r = rgba[0] / 255.0f, g = rgba[1] / 255.0f, b = rgba[2] / 255.0f;
    if (matrix)
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
    sources[c].insert(uint32_t(rgba[0]) << 16 | uint32_t(rgba[1]) << 8 | rgba[2]);
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
};

struct RoomWork
{
  const tmx::Map *map = nullptr;
  std::string id;
  std::vector<GraphicsLayer> layers;
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
};

std::string location(const tmx::Map &map, const tmx::Layer &layer, int x, int y)
{
  return map.path + " layer '" + layer.name + "' at " + std::to_string(x) + "," + std::to_string(y);
}

// Cuts every graphics layer of a room into 8x8 tiles and deduplicates them (with flips)
// against the area's tiles.
void collectTiles(RoomWork &room, AreaWork &area, ColorConverter &colors, const Profile &profile, Report &report)
{
  const tmx::Map &map = *room.map;
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
                t[py * 8 + px] = (ix < ts->image->width && iy < ts->image->height) ? colors.convert(ts->image->pixel(ix, iy)) : TRANSPARENT;
              }
            int flip = (fh ? FLIP_H : 0) | (fv ? FLIP_V : 0);
            t = flipped(t, flip);  // pixels as they appear on screen

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
    std::stable_sort(colours.begin(), colours.end(), [](uint16_t a, uint16_t c) { return luminance(a) < luminance(c); });
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
  b.u16(uint32_t(area.uniqueTiles.size()));
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
      uint16_t c = i < int(area.banks[bank].size()) ? area.banks[bank][size_t(i)] : 0;
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

  int starts = 0;
  Bytes entries, props;
  uint32_t propsBase = 4 + 20 * uint32_t(objects.size());
  for (const tmx::Object *o : objects)
  {
    if (o->type.empty())
      report.warning("does-not-fit", map.path + " object " + std::to_string(o->id), "object without class (type) is ignored by the runtime");
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
  std::string id = map.property("room_id");
  if (!id.empty()) return id;
  size_t slash = map.path.find_last_of("/\\");
  std::string file = slash == std::string::npos ? map.path : map.path.substr(slash + 1);
  size_t dot = file.find_last_of('.');
  return dot == std::string::npos ? file : file.substr(0, dot);
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
    std::string areaName = maps[i].property("area", "default");
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

  BmPackWriter writer;
  bmPackWriterInit(&writer, profile.fourcc, options.layout);
  StringTable strings;

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

    uint16_t areaGroup = uint16_t(1 + a);
    uint16_t backdrop = parseColour(area.rooms.front()->map->property("backdrop", "#000000"));

    Bytes vlay;
    vlay.u16(0);
    vlay.u16(uint32_t(area.uniqueTiles.size()));
    vlay.u16(uint32_t(area.uniqueTiles.size()));
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

      std::vector<std::pair<const GraphicsLayer *, uint32_t>> layerChunks;
      for (const GraphicsLayer &gl : room->layers)
        layerChunks.emplace_back(&gl, add(writer, BM_CHUNK_TMAP, BM_CATEGORY_LEVEL, roomGroup, mapChunk(gl, area, profile)));
      uint32_t collChunk = add(writer, BM_CHUNK_COLL, BM_CATEGORY_LEVEL, roomGroup, collisionChunk(map, report));
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
      r.u16(0);
      for (const auto &[gl, chunk] : layerChunks)
      {
        static const uint8_t priorities[3] = {2, 1, 0};
        r.u8(uint32_t(gl->role));
        r.u8(priorities[gl->role]);
        r.u16(chunk);
        r.s32(toFix(gl->layer->parallaxX));
        r.s32(toFix(gl->layer->parallaxY));
      }
      r.u16(vlayChunk);
      r.u16(paleChunk);
      r.u16(tileChunkIndex);
      r.align4();
      add(writer, BM_CHUNK_ROOM, BM_CATEGORY_LEVEL, roomGroup, r);
    }
  }

  add(writer, BM_CHUNK_STRS, BM_CATEGORY_TEXT, 0, strings.chunk());

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
