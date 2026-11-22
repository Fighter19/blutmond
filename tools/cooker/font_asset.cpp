// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "font_asset.h"

#include "image.h"
#include "xml.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace bm::font {

static int number(const std::string &path, const xml::Element *e, const char *key, int fallback)
{
  std::string v = e->attr(key);
  if (v.empty()) return fallback;
  try
  {
    return std::stoi(v);
  }
  catch (const std::exception &)
  {
    throw std::runtime_error(path + ": " + key + " is not a number: '" + v + "'");
  }
}

uint32_t Font::glyphCount() const
{
  uint32_t n = 0;
  for (const Range &r : ranges) n += r.count;
  return n;
}

static uint32_t codePoint(const std::string &path, const std::string &v)
{
  try
  {
    return uint32_t(std::stoul(v, nullptr, v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X') ? 16 : 10));
  }
  catch (const std::exception &)
  {
    throw std::runtime_error(path + ": '" + v + "' is not a character number");
  }
}

Font load(const std::string &path)
{
  Font f;
  f.path = path;
  auto root = xml::parseFile(path);
  if (root->name != "font") throw std::runtime_error(path + ": not a font file");
  if (root->attr("version", "1") != "1") throw std::runtime_error(path + ": font version " + root->attr("version") + " is not supported");
  f.name = root->attr("name", fs::path(path).stem().string());
  std::string image = root->attr("image");
  if (image.empty()) throw std::runtime_error(path + ": no image");
  f.imagePath = (fs::path(path).parent_path() / image).lexically_normal().string();
  f.cellW = number(path, root.get(), "cell_w", 8);
  f.cellH = number(path, root.get(), "cell_h", 8);
  if (f.cellW != 8 || (f.cellH != 8 && f.cellH != 16)) throw std::runtime_error(path + ": cells are 8x8 or 8x16");
  for (const xml::Element *r : root->childrenNamed("range"))
    f.ranges.push_back({codePoint(path, r->attr("first", "32")), codePoint(path, r->attr("count", "1"))});
  if (f.ranges.empty() && !root->attr("first").empty())
    f.ranges.push_back({codePoint(path, root->attr("first")), codePoint(path, root->attr("count", "95"))});
  if (f.ranges.empty()) throw std::runtime_error(path + ": no characters (range)");
  std::vector<Range> sorted = f.ranges;
  std::sort(sorted.begin(), sorted.end(), [](const Range &a, const Range &b) { return a.first < b.first; });
  for (size_t i = 0; i < sorted.size(); i++)
  {
    if (sorted[i].count == 0 || sorted[i].first + sorted[i].count > 0x10000)
      throw std::runtime_error(path + ": characters must lie in 1 to 0xFFFF");
    if (i > 0 && sorted[i].first < sorted[i - 1].first + sorted[i - 1].count) throw std::runtime_error(path + ": ranges overlap");
  }
  Image img = loadImage(f.imagePath);
  uint32_t cells = uint32_t(img.width / f.cellW) * uint32_t(img.height / f.cellH);
  if (cells < f.glyphCount())
    throw std::runtime_error(path + ": the image holds " + std::to_string(cells) + " cells, not " + std::to_string(f.glyphCount()));
  return f;
}

std::vector<uint8_t> chunk(const Font &f, uint16_t nameId)
{
  Image img = loadImage(f.imagePath);
  std::vector<uint8_t> out;
  auto u16 = [&](uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
  };
  u16(nameId);
  out.push_back(uint8_t(f.cellW));
  out.push_back(uint8_t(f.cellH));
  out.push_back(2);
  out.push_back(0);
  u16(uint32_t(f.ranges.size()));
  u16(f.glyphCount());
  u16(0);
  // Ranges by code point; glyphs stay in the order of the cells.
  std::vector<std::pair<Range, uint32_t>> ranges;
  uint32_t glyph = 0;
  for (const Range &r : f.ranges)
  {
    ranges.push_back({r, glyph});
    glyph += r.count;
  }
  std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) { return a.first.first < b.first.first; });
  for (const auto &[r, firstGlyph] : ranges)
  {
    u16(r.first);
    u16(r.count);
    u16(firstGlyph);
    u16(0);
  }
  int columns = img.width / f.cellW;
  for (uint32_t g = 0; g < f.glyphCount(); g++)
  {
    int cx = int(g % uint32_t(columns)) * f.cellW, cy = int(g / uint32_t(columns)) * f.cellH;
    for (int y = 0; y < f.cellH; y++)
    {
      uint8_t bits = 0;
      for (int x = 0; x < 8; x++)
      {
        const uint8_t *p = img.pixel(cx + x, cy + y);
        if (p[3] >= 128 && (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000 >= 128) bits = uint8_t(bits | (1u << x));
      }
      out.push_back(bits);
    }
  }
  return out;
}

}  // namespace bm::font
