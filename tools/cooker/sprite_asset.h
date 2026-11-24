// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Sprites as source assets (docs/spec/sprite-format.md): a sheet image plus a .bmsprite file
// with frames (rectangle, anchor, hitbox) and animations (steps of frame and duration), and
// their cooked form: 16-colour OBJ tiles, one palette bank, frames split into hardware sprite
// pieces (meta sprites), animations.
#pragma once

#include "cook.h"
#include "image.h"

#include <memory>
#include <string>
#include <vector>

namespace bm::sprite {

struct Box
{
  int x = 0, y = 0, w = 0, h = 0;   // relative to the anchor
  bool operator==(const Box &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

/// Hurt, hit and guard boxes (docs/plans/hitboxes.md §2). The numbers are stored in packs.
enum class BoxKind { Hurt = 0, Hit = 1, Guard = 2 };
const char *boxKindName(BoxKind kind);

struct NamedBox
{
  BoxKind kind = BoxKind::Hurt;
  std::string name;                   // hit and guard boxes: boxes of one name belong together
  Box box;
  bool operator==(const NamedBox &o) const { return kind == o.kind && name == o.name && box == o.box; }
};

/// A point of a frame (docs/plans/layered-sprites.md §4): where another layer of a figure is
/// placed, with a direction for what hangs there and whether that is drawn behind.
struct Point
{
  std::string name;
  int x = 0, y = 0;                   // relative to the anchor
  int angle = 0;                      // 256 steps a turn, 0 forward, clockwise on screen
  bool behind = false;                // what hangs there is drawn behind this layer
  bool operator==(const Point &o) const { return name == o.name && x == o.x && y == o.y && angle == o.angle && behind == o.behind; }
};

struct Frame
{
  int x = 0, y = 0, w = 16, h = 16;   // rectangle in the sheet
  int ax = 8, ay = 16;                // anchor inside the rectangle (the feet for actors)
  bool ownHitbox = false;             // otherwise the sprite's hitbox
  Box hitbox;
  std::vector<NamedBox> boxes;        // replace the sprite's boxes of the same kinds
  std::vector<Point> points;          // and the sprite's points of the same names
};

struct Step
{
  int frame = 0;
  int duration = 6;                   // in game frames
};

struct Animation
{
  std::string name;
  bool loop = true;
  std::vector<Step> steps;
  // A direction set (layered-sprites.md §6): 0, or as many steps as directions, which are not
  // played in time; each stands for a direction, evenly from `from` to `to` (256 a turn, 0
  // forward, clockwise on screen: 192 up, 64 down).
  int directions = 0;
  int from = 192, to = 64;
};

/// The angle step `i` of a direction set stands for.
int directionAngle(const Animation &animation, size_t i);

/// Another palette of the same pixels (docs/plans/layered-sprites.md §2): a second image of the
/// sheet with other colours (paired with the default ones by position), or single colours
/// swapped.
struct PaletteVariant
{
  std::string name;
  std::string imageAttr;              // as written in the file (relative), or empty
  std::string imagePath;              // absolute
  std::shared_ptr<Image> image;
  std::vector<std::pair<uint32_t, uint32_t>> swaps;   // RGB from, to
};

struct Sprite
{
  std::string path;                   // the .bmsprite file
  std::string name;                   // file name without extension: how the game finds it
  std::string imagePath;              // absolute
  std::string imageAttr;              // as written in the file (relative)
  std::shared_ptr<Image> image;
  Box hitbox;
  std::vector<NamedBox> boxes;        // of every frame that has none of that kind
  std::vector<Point> points;          // of every frame that has none of that name
  std::vector<Frame> frames;
  std::vector<Animation> animations;
  std::vector<PaletteVariant> palettes;   // besides the sheet's own ("default")
  // Light (docs/plans/lighting.md): `normal` a normal map beside the sheet or `auto`; `emissive`
  // (not lit); `shiny` source colours (#rrggbb) that get a highlight.
  std::string normalAttr;
  bool emissive = false;
  std::vector<uint32_t> shiny;
};

/// Loads a sprite file and its sheet. Throws std::runtime_error on errors.
Sprite load(const std::string &path);
/// Writes the sprite file (not the image).
bool save(const Sprite &sprite, std::string *pError = nullptr);

/// One hardware sprite of a frame.
struct Piece
{
  int dx = 0, dy = 0;                 // top left, relative to the anchor
  int w = 8, h = 8;                   // one of the 12 OBJ sizes
  int tile = 0;                       // first tile (tiles follow each other: 1D mapping)
};

struct CookedFrame
{
  std::vector<Piece> pieces;
  Box hitbox;
  std::vector<NamedBox> boxes;        // the frame's own, else the sprite's, per kind
  Box bounds;                         // of the opaque pixels, relative to the anchor
  std::vector<Point> points;          // the frame's own, then the sprite's of other names
};

/// The boxes a frame has: its own of a kind, else the sprite's of that kind.
std::vector<NamedBox> frameBoxes(const Sprite &sprite, const Frame &frame);

struct Cooked
{
  std::vector<std::string> boxNames;  // every name of a hit or guard box, in order of use
  std::vector<std::string> pointNames;   // every point name, in order of use
  std::vector<uint16_t> palette;      // 16 entries, 0 transparent
  std::vector<std::string> paletteNames;           // of the variants, in file order
  std::vector<std::vector<uint16_t>> variants;     // 16 entries each, the same slots
  // Per slot a normal (s8 x, y, z) and flags (1 emissive, 2 shiny): empty for a sprite without
  // light (then written as version 4 or less).
  std::vector<std::array<int8_t, 4>> normals;
  std::vector<std::array<uint8_t, 64>> tiles;  // palette indices
  std::vector<CookedFrame> frames;
  int maxPieces = 0;                  // hardware sprites the largest frame needs
  int maxLineCost = 0;                // sprite cycles on its most expensive line
};

/// Cooks the graphics of a sprite for a profile; problems go to the report.
Cooked cook(const Sprite &sprite, const Profile &profile, Report &report);

/// The SPRT chunk (pack-format.md): `nameId` is the sprite's name in the string table,
/// `animationNames` the string ids of its animations, `boxNameIds` those of cooked.boxNames.
/// `paletteNameIds` those of "default" and then cooked.paletteNames, `pointNameIds` those of
/// cooked.pointNames (a sprite with points is written as version 4).
std::vector<uint8_t> chunk(const Sprite &sprite, const Cooked &cooked, uint16_t nameId, const std::vector<uint16_t> &animationNames,
                           const std::vector<uint16_t> &boxNameIds = {}, const std::vector<uint16_t> &paletteNameIds = {},
                           const std::vector<uint16_t> &pointNameIds = {});

}  // namespace bm::sprite
