// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Fonts (docs/spec/pack-format.md §3.1, FONT): an image of glyph cells and a small XML file.
//
//   <font version="1" name="font" image="font.png" cell_w="8" cell_h="8">
//    <range first="32" count="95"/>         <!-- code points, in the order of the cells -->
//    <range first="0xA0" count="96"/>
//   </font>
//
// One font may be made of several files with the same name and other characters (Latin
// letters in one, kana in another): each is its own chunk, the runtime looks in all of them.
// The cells are read row by row from the top left. Bright opaque pixels (alpha and luminance
// from 128) are the ink; everything else is left out: the runtime draws the shadow itself.
#pragma once

#include "cook.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bm::font {

struct Range
{
  uint32_t first = 32, count = 95;
};

struct Font
{
  std::string path, name;    // name: the attribute, else the file's stem
  std::string imagePath;
  int cellW = 8, cellH = 8;
  std::vector<Range> ranges;
  uint32_t glyphCount() const;
};

/// Reads a font file and checks it against its image. Throws with a message on errors.
Font load(const std::string &path);

/// The FONT chunk (version 2), 12 bytes of header: u16 name, u8 cell width, u8 cell height,
/// u8 2, u8 0, u16 range count, u16 glyph count, u16 0; ranges { u16 first code point, u16 count,
/// u16 first glyph, u16 0 } by code point; then per glyph one byte per pixel row (bit x: ink at
/// pixel x).
std::vector<uint8_t> chunk(const Font &font, uint16_t nameId);

}  // namespace bm::font
