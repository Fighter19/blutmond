// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "font_asset.h"

#include "image.h"
#include "xml.h"

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

Font load(const std::string &path)
{
  Font f;
  f.path = path;
  f.name = fs::path(path).stem().string();
  auto root = xml::parseFile(path);
  if (root->name != "font") throw std::runtime_error(path + ": not a font file");
  if (root->attr("version", "1") != "1") throw std::runtime_error(path + ": font version " + root->attr("version") + " is not supported");
  std::string image = root->attr("image");
  if (image.empty()) throw std::runtime_error(path + ": no image");
  f.imagePath = (fs::path(path).parent_path() / image).lexically_normal().string();
  f.cellW = number(path, root.get(), "cell_w", 8);
  f.cellH = number(path, root.get(), "cell_h", 8);
  f.first = number(path, root.get(), "first", 32);
  f.count = number(path, root.get(), "count", 95);
  if (f.cellW != 8 || (f.cellH != 8 && f.cellH != 16)) throw std::runtime_error(path + ": cells are 8x8 or 8x16");
  if (f.first < 1 || f.count < 1 || f.first + f.count > 256) throw std::runtime_error(path + ": the characters must lie in 1 to 255");
  Image img = loadImage(f.imagePath);
  int columns = img.width / f.cellW, rows = img.height / f.cellH;
  if (columns * rows < f.count)
    throw std::runtime_error(path + ": the image holds " + std::to_string(columns * rows) + " cells, not " + std::to_string(f.count));
  return f;
}

std::vector<uint8_t> chunk(const Font &f, uint16_t nameId)
{
  Image img = loadImage(f.imagePath);
  std::vector<uint8_t> out = {uint8_t(nameId), uint8_t(nameId >> 8), uint8_t(f.cellW), uint8_t(f.cellH), uint8_t(f.first), uint8_t(f.count), 0, 0};
  int columns = img.width / f.cellW;
  for (int g = 0; g < f.count; g++)
  {
    int cx = (g % columns) * f.cellW, cy = (g / columns) * f.cellH;
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
