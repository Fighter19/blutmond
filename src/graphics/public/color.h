#pragma once

#include "shared.h"

/// A colour in the GBA's format: BGR555, bit 15 unused.
typedef struct BmColor
{
  uint16_t color;
} BmColor;

static inline void bmColorInitRGB(BmColor *color, uint8_t r, uint8_t g, uint8_t b)
{
  color->color = (uint16_t)((r & 31) | ((g & 31) << 5) | ((b & 31) << 10));
}

static inline BmColor bmColorRGB(uint8_t r, uint8_t g, uint8_t b)
{
  BmColor color;
  bmColorInitRGB(&color, r, g, b);
  return color;
}
