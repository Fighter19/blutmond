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

/// A correction of colours for a screen (the pack's CCOR chunk, pack-format.md): the
/// channels (0-31) are mixed by `matrix` (row major: red, green, blue out; 4096 = 1.0),
/// rounded down, kept within 0-31 and looked up in `curve`. For the AGB-001 it is the inverse
/// of its screen's colour mixing and a brightening curve, so that colours look on it as they
/// were drawn on a PC screen.
typedef struct BmColorCorrection   // as stored in CCOR after its first 4 bytes (little endian)
{
  int16_t matrix[9];
  uint8_t curve[32];
} BmColorCorrection;
#ifndef __cplusplus
_Static_assert(sizeof(BmColorCorrection) == 50, "CCOR layout");
#endif

/// One colour (BGR555) corrected: the curve over the mix.
uint16_t bmColorCorrect(const BmColorCorrection *pCorrection, uint16_t bgr555);
/// The two steps apart: the channels mixed (kept within 0-31), and the curve per channel. The
/// light mixes its sources once and puts only the curve over what it lit every frame (the
/// mix taken before the light's colour instead of after: the same for white light).
uint16_t bmColorCorrectMix(const BmColorCorrection *pCorrection, uint16_t bgr555);
uint16_t bmColorCorrectCurve(const BmColorCorrection *pCorrection, uint16_t bgr555);
