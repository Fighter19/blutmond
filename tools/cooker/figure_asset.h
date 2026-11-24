// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Figures (docs/plans/layered-sprites.md §4): what an object shows, made of up to 4 layers,
// each a sprite with its own animation and palette, placed at a point of another layer's
// current frame. A text file, one layer a line:
//
//   # player.bmfigure
//   layer legs   sprite=player_legs                       # the first layer is the root
//   layer torso  sprite=player_torso  at=legs.waist  mode=sync
//   layer aura   sprite=none          at=legs.centre behind
//
// The file name without its extension is the figure's name (the game finds figures by
// name, as sprites). Cooked into a FIGR chunk (sprite-format.md, Figures).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bm::figure {

enum class Mode { Own = 0, Sync = 1 };

struct Layer
{
  std::string name;
  std::string sprite;        // empty: none (filled at run time)
  std::string parent;        // the layer it hangs on; empty: the root
  std::string point;         // the parent's point it is placed at; empty: the parent's anchor
  Mode mode = Mode::Own;
  bool behind = false;       // drawn behind its parent
  std::string palette;       // empty: the sprite's default
};

struct Figure
{
  std::string path;
  std::string name;
  std::vector<Layer> layers;
};

constexpr size_t MAX_LAYERS = 4;

/// Loads a figure file. Throws std::runtime_error on errors (with the line).
Figure load(const std::string &path);

/// Index of the layer called `name`, or -1.
int layerIndex(const Figure &figure, const std::string &name);

/// The FIGR chunk; `id` gives the string id of a name (0xFFFF for an empty one).
template <typename Ids> std::vector<uint8_t> chunk(const Figure &f, Ids id)
{
  std::vector<uint8_t> b;
  auto u8 = [&](int v) { b.push_back(uint8_t(v)); };
  auto u16 = [&](uint32_t v) { u8(int(v)); u8(int(v >> 8)); };
  auto str = [&](const std::string &s) { u16(s.empty() ? 0xFFFFu : uint32_t(id(s))); };
  str(f.name);
  u8(int(f.layers.size()));
  u8(0);
  for (const Layer &l : f.layers)
  {
    str(l.name);
    str(l.sprite);
    str(l.point);
    u8(l.parent.empty() ? 0xFF : layerIndex(f, l.parent));
    u8(int(l.mode));
    u8(l.behind ? 1 : 0);
    u8(0);
    str(l.palette);
  }
  while (b.size() % 4) u8(0);
  return b;
}

}  // namespace bm::figure
