/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The light of a room (docs/plans/lighting.md): one light, cooked into the room's LITE chunk.
// Each frame its intensity (with the flicker, a function of the frame counter) gives the
// colours of the room's palette banks and of the sprites' banks:
//
//   palette  the cooker baked how lit each pixel is (8 levels) into the room's colours: every
//            entry is a colour and a level, and is that colour × (ambient + light × level)
//   layer    the room's colours are darkened to the ambient; a light layer (a BG of normal
//            tiles, one bank per region of the light) is added over them by alpha blending
//
// Sprites with normals (SPRT version 5) are lit per palette slot by the direction from the
// figure to the light; sprites without are lit as facing the viewer. Emissive slots stay.
// Nothing of this is game state: it follows from the room, the frame counter and positions.

#include "shared.h"

struct BmGame;

#define BM_LIGHT_NONE 0
#define BM_LIGHT_PALETTE 1
#define BM_LIGHT_LAYER 2
#define BM_LIGHT_MAX_SOURCES 4   // lights in a room (they flicker together)

typedef struct BmLight
{
  uint8_t mode;              // BM_LIGHT_*
  const uint8_t *pLite;      // the LITE chunk's data
  const uint8_t *pLevelLight; // palette: each level's light (255 = 1.0), NULL: even steps
  uint32_t liteSize;
  int16_t x, y;              // room pixels (the first light)
  int16_t sx[4], sy[4];      // every light, the first one included
  uint8_t sourceCount;
  uint8_t radius, height;
  uint8_t red, green, blue;  // 0..31
  uint8_t ambient;           // 0..255
  uint8_t flicker, flickerSpeed, intensity, gain;   // /255, frames, /64, /64
  uint8_t shadows;
  uint8_t levels, firstBank, bankCount, normalCount;
  uint8_t layerBg;           // the light layer's background, 0xFF: none
  bool wasLit;               // the sprites' banks hold lit colours (reloaded when that ends)
  bool fresh;                // the room was just entered: every bank is set
  bool fitted;               // layer mode: the values are levels of light, not normals
  bool stream;               // layer mode: the light layer's banks are streamed line by line
  bool hybrid;               // ... only the first streamStride / 16 of them; the others fixed (pFixed),
  uint8_t fixedBanks;        // ... times a weight per room row (pWeights, 255 = 1) as the blend weight
  const uint8_t *pFixed, *pWeights;
  const uint16_t *pStream;   // ... these rows of colours (streamRows of streamStride)
  uint16_t streamRows, streamStride;
  const uint8_t *pPalette;   // the room's palette (PALE) as drawn ...
  uint8_t paletteFirst, paletteBanks;   // ... from bank `paletteFirst` on
  uint16_t litBanks;         // palette mode: the room's banks with lit colours
  uint16_t objDark;          // sprite banks set to ambient only (out of the light), for...
  uint8_t objSheet[13], objPalette[13];   // ... this sheet and palette
  int32_t now;               // the intensity this frame, 256 = 1.0
  int32_t shaded;            // palette: the intensity its banks were last shaded for (-1: none)
} BmLight;

/// A room was shown: reads its light (none: the sprite banks get their own colours back).
void bmLightEnterRoom(struct BmGame *pGame);

/// The intensity at a frame, 256 = 1.0 (the flicker, a pure function of the frame).
int32_t bmLightIntensity(const BmLight *pLight, uint32_t frame);

/// Once a frame after the sprites and the effects: the lit palettes, the light layer's blend.
void bmLightPresent(struct BmGame *pGame);

/// Whether the room is lit (any mode).
bool bmLightOn(const struct BmGame *pGame);
