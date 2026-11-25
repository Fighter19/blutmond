// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The light of a room as the cooker bakes it (docs/plans/lighting.md): the `Light` object, normal
// maps of tilesets and sprites (drawn or `auto`), the light's term per pixel with shadows from the
// collision grid, and the small clustering both lighting modes use.
#pragma once

#include "cook.h"
#include "image.h"
#include "tmx.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bm::light {

/// Light levels of the baked mode (`palette`): level 0 is ambient only, 7 the most.
constexpr int LEVELS = 8;
/// The light layer's region banks at most (mode `layer`).
constexpr int MAX_REGIONS = 14;
/// Normals of the light layer (its tiles' values 1 to 15).
constexpr int LAYER_NORMALS = 15;

enum class Mode { None = 0, Palette = 1, Layer = 2 };

struct Vec3
{
  float x = 0, y = 0, z = 1;
};

Vec3 normalize(Vec3 v);
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// Where one light of a room is (room pixels).
struct Source
{
  float x = 0, y = 0;
};

struct Light
{
  Mode mode = Mode::None;
  int objectId = 0;
  float x = 0, y = 0;         // the first light (the one the runtime's head of LITE names)
  // Every light of the room, the first one included. They share the first one's properties
  // (colour, radius, height, intensity and flicker): they flicker together.
  std::vector<Source> sources;
  float radius = 72, height = 12;
  float intensity = 1, flicker = 0;
  int flickerSpeed = 6;
  uint16_t colour = 0x029F;   // BGR555 of #ffc080 as the cooker converts it
  float ambient = 1;
  float gain = 0.5f;          // the light layer's strength (it adds, the baked mode multiplies)
  bool shadows = true;
  bool dither = false;          // Palette: levels dithered between neighbours (room property `light_dither`)
  int levels = LEVELS;          // Palette: levels of light, 2 to 16 (room property `light_levels`)
  // Palette: the light of each level (0 to 1). Empty: even steps (k / (levels - 1)). With
  // `light_curve = perceived` even steps of how bright a surface looks (the square root of
  // ambient + light), so they are finer in the dark, where a step shows most.
  std::vector<float> levelLight;
  /// The level for light `tv` at room pixel (x, y): the nearest, or with `dither` the one below
  /// or above by an ordered 4x4 dither; `pValue` its light.
  int levelFor(float tv, int x, int y, float *pValue) const;
  // Mode `layer`: what the light layer's 15 values stand for (room property `light_index`):
  // shared (default) a pixel's light for one of a few light directions, scaled per bank;
  // fitted its own light, levels fitted per bank; normals one of 15 directions.
  // stream: normals in the tiles, the light per band and line streamed into the palette (H-blank
  // DMA, cartridge images only).
  // hybrid: as stream, but only the banks near the light streamed; the others fixed, times a
  // weight per line (the light layer's blend weight) for the vertical falloff.
  enum class Index { Shared, Fitted, Normals, Stream, Hybrid } index = Index::Shared;
  int streamBanks = 2;          // Hybrid: banks streamed near the light (room property `light_stream_banks`)
  int streamNormals = 7;        // Stream: 15 or 3 to 7 normals; flat bands: 15 / normals bands a bank (room property `light_normals`)
  // Stream: what a band's colours are (room property `light_bands`): the light at its middle
  // column, its mean across the band, or a ramp: a band per bank whose samples (its edges and
  // between, shared with the neighbours) the pixels pick by an ordered dither of where they are.
  enum class Bands { Middle, Mean, Ramp } bands = Bands::Middle;
  int bandWidth = 24;           // Stream: a band's width in pixels (room property `light_band`)
  bool fitted = true;           // Shared or Fitted: the values are levels of light
  int directions = 8;           // Shared: how many light directions (room property `light_directions`)
  float tolerance = 0.1f;       // Shared: a cell across which the light changes more (of full light) gets a tile of its own
};

/// The room's light: its one `Light` object and the map's `lighting` and `ambient`. Mode None
/// without a light. More than one light, or a mode that is not `palette` or `layer`, is reported.
Light find(const tmx::Map &map, Report &report);

/// `(1 - d²/r²)²` inside the radius, else 0.
float falloff(const Light &light, float d2);

/// How lit a surface at room pixel (px, py) with normal n is at intensity 1, without shadows:
/// the sum over the lights of max(0, n·L) × falloff, L from the pixel to the light raised by
/// its height.
float term(const Light &light, float px, float py, Vec3 n);
/// The same from one light.
float termFrom(const Light &light, const Source &source, float px, float py, Vec3 n);

/// The light's direction from (px, py), scaled by the falloff there, summed over the lights
/// (what a region bank stands for in mode `layer`).
Vec3 scaledDirection(const Light &light, float px, float py);

/// Solid cells of the collision grid (shape full, solid on all sides), for shadows.
struct Solid
{
  int width = 0, height = 0, cell = 16;
  std::vector<uint8_t> cells;

  /// From a COLL chunk's data (room-format.md §7).
  static Solid fromChunk(const std::vector<uint8_t> &coll);
  bool at(int cx, int cy) const;
  /// Whether a straight line from (px, py) to the light crosses a solid cell, but for the solid
  /// cells it starts in (the wall the pixel is on).
  bool shadowed(float px, float py, const Light &light) const;   // from the first light
  bool shadowedFrom(float px, float py, const Light &light, const Source &source) const;
  /// How lit (as `term`) with each light's shadows; `pShadow` set if some light whose radius
  /// reaches the pixel is shadowed there.
  float lit(float px, float py, const Light &light, Vec3 n, bool *pShadow = nullptr) const;
};

/// Decodes a normal from a normal map pixel (RGB: x right, y down, z toward the viewer).
Vec3 decodeNormal(const uint8_t *rgba);
/// Encodes one (alpha 255).
void encodeNormal(Vec3 n, uint8_t *rgba);

/// Normals made from a drawing (`auto`): per cell of cellW × cellH, the height is the brightness
/// plus a bevel that rises from the cell's opaque edge, smoothed; the normals follow its slope.
Image autoNormals(const Image &image, int cellW, int cellH);

/// The normal map of a tileset: its property `normal` (an image beside it, the sheet's size, or
/// `auto`). nullptr without one (flat). Loaded once per path.
std::shared_ptr<Image> tilesetNormals(const tmx::Tileset &tileset, Report &report);

/// k-means of weighted points into at most k centres (fewer when there are fewer distinct
/// points), started from the farthest points so the result does not depend on chance.
std::vector<Vec3> cluster(const std::vector<Vec3> &points, const std::vector<float> &weights, int k);

/// The index of the nearest centre.
int nearest(const std::vector<Vec3> &centres, Vec3 p);

}  // namespace bm::light
