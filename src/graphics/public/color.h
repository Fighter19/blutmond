#pragma once

#include "shared.h"

typedef struct BmColor
{
  uint16_t color;
} BmColor;

static void bmColorInitRGB(BmColor *color, uint8_t r, uint8_t g, uint8_t b)
{
  color->color = (r & 31) | ((g & 31) << 5) | ((b & 31) << 10);
}