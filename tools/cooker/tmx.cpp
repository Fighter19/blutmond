// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "tmx.h"

#include "xml.h"

#include <sstream>
#include <stdexcept>

namespace bm::tmx {

namespace {

std::string directoryOf(const std::string &path)
{
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string join(const std::string &dir, const std::string &file)
{
  if (file.empty() || file[0] == '/' || dir.empty()) return file;
  return dir + file;
}

Properties readProperties(const xml::Element *el)
{
  Properties props;
  if (el == nullptr) return props;
  if (const xml::Element *ps = el->child("properties"))
  {
    for (const xml::Element *p : ps->childrenNamed("property"))
    {
      props[p->attr("name")] = p->has("value") ? p->attr("value") : p->text;
    }
  }
  return props;
}

void readTilesetBody(const xml::Element &ts, Tileset &out, const std::string &dir)
{
  out.name = ts.attr("name");
  out.tileWidth = std::stoi(ts.attr("tilewidth", "0"));
  out.tileHeight = std::stoi(ts.attr("tileheight", "0"));
  out.tileCount = std::stoi(ts.attr("tilecount", "0"));
  out.columns = std::stoi(ts.attr("columns", "0"));
  if (const xml::Element *img = ts.child("image"))
  {
    out.imagePath = join(dir, img->attr("source"));
  }
  for (const xml::Element *t : ts.childrenNamed("tile"))
  {
    int id = std::stoi(t->attr("id"));
    Properties props = readProperties(t);
    if (!props.empty()) out.tileProperties[id] = props;
    std::string type = t->has("class") ? t->attr("class") : t->attr("type");
    if (!type.empty()) out.tileTypes[id] = type;
  }
}

std::vector<uint32_t> readCsv(const xml::Element &data, size_t expected, const std::string &layerName)
{
  if (data.attr("encoding") != "csv" || data.has("compression"))
  {
    throw std::runtime_error("layer '" + layerName + "': only CSV layer data is supported (set Tile Layer Format to CSV in Tiled)");
  }
  std::vector<uint32_t> out;
  std::string token;
  std::stringstream ss(data.text);
  while (std::getline(ss, token, ','))
  {
    size_t b = token.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    out.push_back(uint32_t(std::stoul(token.substr(b))));
  }
  if (out.size() != expected)
  {
    throw std::runtime_error("layer '" + layerName + "': expected " + std::to_string(expected) + " tiles, found " + std::to_string(out.size()));
  }
  return out;
}

}  // namespace

const Properties *Tileset::propertiesOf(int localId) const
{
  auto it = tileProperties.find(localId);
  return it == tileProperties.end() ? nullptr : &it->second;
}

std::string Layer::role() const
{
  auto it = properties.find("role");
  return it == properties.end() ? std::string() : it->second;
}

const Tileset *Map::tilesetFor(uint32_t gid) const
{
  const Tileset *best = nullptr;
  for (const Tileset &ts : tilesets)
    if (gid >= ts.firstGid && (best == nullptr || ts.firstGid > best->firstGid)) best = &ts;
  return best;
}

std::string Map::property(const std::string &key, const std::string &fallback) const
{
  auto it = properties.find(key);
  return it == properties.end() ? fallback : it->second;
}

Map load(const std::string &path)
{
  auto root = xml::parseFile(path);
  if (root->name != "map") throw std::runtime_error(path + ": not a Tiled map");
  if (root->attr("orientation") != "orthogonal") throw std::runtime_error(path + ": only orthogonal maps are supported");
  if (root->attr("infinite", "0") != "0") throw std::runtime_error(path + ": infinite maps are not supported");

  const std::string dir = directoryOf(path);
  Map map;
  map.path = path;
  map.width = std::stoi(root->attr("width"));
  map.height = std::stoi(root->attr("height"));
  map.tileWidth = std::stoi(root->attr("tilewidth"));
  map.tileHeight = std::stoi(root->attr("tileheight"));
  map.properties = readProperties(root.get());

  for (const auto &child : root->children)
  {
    if (child->name == "tileset")
    {
      Tileset ts;
      ts.firstGid = uint32_t(std::stoul(child->attr("firstgid", "1")));
      if (child->has("source"))
      {
        std::string tsxPath = join(dir, child->attr("source"));
        auto tsx = xml::parseFile(tsxPath);
        ts.source = tsxPath;
        readTilesetBody(*tsx, ts, directoryOf(tsxPath));
      }
      else
      {
        ts.source = path;
        readTilesetBody(*child, ts, dir);
      }
      if (!ts.imagePath.empty())
      {
        ts.image = std::make_shared<Image>(loadImage(ts.imagePath));
      }
      map.tilesets.push_back(std::move(ts));
    }
    else if (child->name == "layer")
    {
      Layer layer;
      layer.name = child->attr("name");
      layer.properties = readProperties(child.get());
      layer.parallaxX = std::stof(child->attr("parallaxx", "1"));
      layer.parallaxY = std::stof(child->attr("parallaxy", "1"));
      layer.width = std::stoi(child->attr("width"));
      layer.height = std::stoi(child->attr("height"));
      const xml::Element *data = child->child("data");
      if (data == nullptr) throw std::runtime_error("layer '" + layer.name + "' has no data");
      layer.gids = readCsv(*data, size_t(layer.width) * layer.height, layer.name);
      map.layers.push_back(std::move(layer));
    }
    else if (child->name == "objectgroup")
    {
      ObjectGroup group;
      group.name = child->attr("name");
      group.properties = readProperties(child.get());
      for (const xml::Element *o : child->childrenNamed("object"))
      {
        Object obj;
        obj.id = std::stoi(o->attr("id", "0"));
        obj.name = o->attr("name");
        obj.type = o->has("class") ? o->attr("class") : o->attr("type");
        obj.x = std::stod(o->attr("x", "0"));
        obj.y = std::stod(o->attr("y", "0"));
        obj.width = std::stod(o->attr("width", "0"));
        obj.height = std::stod(o->attr("height", "0"));
        obj.point = o->child("point") != nullptr;
        obj.properties = readProperties(o);
        group.objects.push_back(std::move(obj));
      }
      map.objectGroups.push_back(std::move(group));
    }
  }
  return map;
}

}  // namespace bm::tmx
