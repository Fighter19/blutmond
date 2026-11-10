// SPDX-License-Identifier: BSD-2-Clause OR MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bm {

/// RGBA8 image.
struct Image
{
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;

  const uint8_t *pixel(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
};

/// Loads PNG (and other formats stb_image reads). Throws on errors.
Image loadImage(const std::string &path);

}  // namespace bm
