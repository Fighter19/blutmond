// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Tiled map reader (.tmx with embedded or external .tsx tilesets, CSV layer data).
#pragma once

#include "image.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bm::tmx {

using Properties = std::map<std::string, std::string>;

constexpr uint32_t FLIP_H = 0x80000000u;
constexpr uint32_t FLIP_V = 0x40000000u;
constexpr uint32_t FLIP_D = 0x20000000u;
constexpr uint32_t GID_MASK = 0x0FFFFFFFu;

struct Tileset
{
  uint32_t firstGid = 1;
  std::string name;
  std::string source;  // file the tileset came from (tsx or tmx)
  int tileWidth = 0, tileHeight = 0, tileCount = 0, columns = 0;
  std::string imagePath;
  std::shared_ptr<Image> image;  // loaded on demand
  std::map<int, Properties> tileProperties;
  std::map<int, std::string> tileTypes;

  const Properties *propertiesOf(int localId) const;
};

struct Layer
{
  std::string name;
  Properties properties;
  float parallaxX = 1, parallaxY = 1;
  int width = 0, height = 0;
  std::vector<uint32_t> gids;  // row major, with flip bits

  std::string role() const;
  uint32_t at(int x, int y) const { return gids[size_t(y) * width + x]; }
};

struct Object
{
  int id = 0;
  std::string name, type;
  double x = 0, y = 0, width = 0, height = 0;
  bool point = false;
  Properties properties;
};

struct ObjectGroup
{
  std::string name;
  Properties properties;
  std::vector<Object> objects;
};

struct Map
{
  std::string path;
  int width = 0, height = 0, tileWidth = 0, tileHeight = 0;
  Properties properties;
  std::vector<Tileset> tilesets;
  std::vector<Layer> layers;
  std::vector<ObjectGroup> objectGroups;

  /// Tileset for a gid (without flip bits), or nullptr.
  const Tileset *tilesetFor(uint32_t gid) const;
  std::string property(const std::string &key, const std::string &fallback = "") const;
};

/// Loads a map; tileset images are loaded too. Throws std::runtime_error on errors.
Map load(const std::string &path);

}  // namespace bm::tmx
