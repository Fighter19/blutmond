// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "image.h"

#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO_FAILURE_STRINGS_UNUSED
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace bm {

Image loadImage(const std::string &path)
{
  int w = 0, h = 0, n = 0;
  unsigned char *data = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (data == nullptr)
  {
    throw std::runtime_error("cannot load image " + path + ": " + stbi_failure_reason());
  }
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.assign(data, data + size_t(w) * h * 4);
  stbi_image_free(data);
  return img;
}

}  // namespace bm
