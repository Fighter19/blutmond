// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Fonts (docs/spec/pack-format.md, FONT): an image of glyph cells and a small XML file.
//
//   <font version="1" image="font.png" cell_w="8" cell_h="8" first="32" count="95"/>
//
// The cells are read row by row from the top left; cell i is the character `first + i`. Bright
// opaque pixels (alpha and luminance from 128) are the ink; everything else is left out: the
// runtime draws the shadow itself, below right of the ink.
#pragma once

#include "cook.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bm::font {

struct Font
{
  std::string path, name;    // name: the file's stem
  std::string imagePath;
  int cellW = 8, cellH = 8;
  int first = 32, count = 95;
};

/// Reads a font file and checks it against its image. Throws with a message on errors.
Font load(const std::string &path);

/// The FONT chunk: u16 name, u8 cell width, u8 cell height, u8 first, u8 count, u16 0, then
/// per glyph one byte per pixel row (cell height rows; bit x is pixel x, set: ink).
std::vector<uint8_t> chunk(const Font &font, uint16_t nameId);

}  // namespace bm::font
