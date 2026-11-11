// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "project_file.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "image.h"
#include "overrides.h"
#include "tmx.h"
#include "xml.h"

namespace fs = std::filesystem;

namespace bm::editor {

namespace {

std::string escape(const std::string &s)
{
  std::string out;
  for (char c : s)
  {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

AssetKind kindOf(const std::string &path)
{
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (ext == ".tmx") return AssetKind::Room;
  if (ext == ".tsx") return AssetKind::Tileset;
  if (ext == ".png" || ext == ".bmp" || ext == ".gif" || ext == ".tga") return AssetKind::Image;
  return AssetKind::Other;
}

int tilesetCount(const std::string &tsx)
{
  auto root = xml::parseFile(tsx);
  return std::stoi(root->attr("tilecount", "0"));
}

}  // namespace

std::string normalPath(const std::string &path)
{
  std::error_code ec;
  fs::path p = fs::absolute(fs::path(path), ec).lexically_normal();
  return p.string();
}

// --- Project file ------------------------------------------------------------------------

std::string ProjectFile::dir() const
{
  return fs::path(path).parent_path().string();
}

std::string ProjectFile::relative(const std::string &absolutePath) const
{
  return fs::path(absolutePath).lexically_relative(dir()).generic_string();
}

std::string ProjectFile::absolute(const std::string &relativePath) const
{
  return normalPath((fs::path(dir()) / relativePath).string());
}

ProjectFile ProjectFile::load(const std::string &file)
{
  ProjectFile p;
  p.path = normalPath(file);
  auto root = xml::parseFile(p.path);
  if (root->name != "project") throw std::runtime_error(file + ": not a Blutmond project");
  if (std::stoi(root->attr("version", "0")) != 1) throw std::runtime_error(file + ": project version " + root->attr("version") + " is not supported");
  p.name = root->attr("name", fs::path(file).stem().string());
  for (const xml::Element *r : root->childrenNamed("room")) p.rooms.push_back(p.absolute(r->attr("path")));
  for (const xml::Element *a : root->childrenNamed("assets")) p.assetDirs.push_back(p.absolute(a->attr("path")));
  return p;
}

bool ProjectFile::save(std::string *pError) const
{
  std::ofstream out(path, std::ios::binary);
  if (!out)
  {
    if (pError) *pError = "cannot write " + path;
    return false;
  }
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<project version=\"1\" name=\"" << escape(name) << "\">\n";
  for (const std::string &r : rooms) out << " <room path=\"" << escape(relative(r)) << "\"/>\n";
  for (const std::string &a : assetDirs) out << " <assets path=\"" << escape(relative(a)) << "\"/>\n";
  out << "</project>\n";
  return bool(out);
}

bool ProjectFile::hasRoom(const std::string &room) const
{
  std::string n = normalPath(room);
  return std::find(rooms.begin(), rooms.end(), n) != rooms.end();
}

bool ProjectFile::addRoom(const std::string &room)
{
  if (hasRoom(room)) return false;
  rooms.push_back(normalPath(room));
  return true;
}

bool ProjectFile::removeRoom(const std::string &room)
{
  auto it = std::find(rooms.begin(), rooms.end(), normalPath(room));
  if (it == rooms.end()) return false;
  rooms.erase(it);
  return true;
}

// --- Catalogue ---------------------------------------------------------------------------

AssetCatalog AssetCatalog::scan(const ProjectFile &project)
{
  std::map<std::string, AssetInfo> found;
  auto entry = [&](const std::string &path) -> AssetInfo & {
    std::string n = normalPath(path);
    AssetInfo &a = found[n];
    if (a.path.empty())
    {
      a.path = n;
      a.kind = kindOf(n);
      std::error_code ec;
      a.hasOverrides = fs::exists(overridesPath(n), ec);
      if (!fs::exists(n, ec)) a.problem = "missing";
    }
    return a;
  };

  for (const std::string &dir : project.assetDirs)
  {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
      if (!it->is_regular_file()) continue;
      std::string p = it->path().string();
      std::string ext = it->path().extension().string();
      if (ext == ".bmasset" || ext == ".bmproj" || it->path().filename().string()[0] == '.') continue;
      entry(p);
    }
  }

  for (const std::string &room : project.rooms)
  {
    AssetInfo &r = entry(room);
    r.inProject = true;
    if (!r.problem.empty()) continue;
    try
    {
      tmx::Map map = tmx::load(room);
      for (const tmx::Tileset &ts : map.tilesets)
      {
        if (normalPath(ts.source) != normalPath(room))
        {
          AssetInfo &t = entry(ts.source);
          if (std::find(t.usedBy.begin(), t.usedBy.end(), r.path) == t.usedBy.end()) t.usedBy.push_back(r.path);
        }
        if (!ts.imagePath.empty())
        {
          AssetInfo &img = entry(ts.imagePath);
          if (std::find(img.usedBy.begin(), img.usedBy.end(), r.path) == img.usedBy.end()) img.usedBy.push_back(r.path);
        }
      }
    }
    catch (const std::exception &e)
    {
      r.problem = e.what();
    }
  }

  AssetCatalog c;
  for (auto &[path, a] : found)
  {
    if (a.kind == AssetKind::Image && a.problem.empty())
    {
      try
      {
        Image img = loadImage(path);
        a.width = img.width;
        a.height = img.height;
      }
      catch (const std::exception &e)
      {
        a.problem = e.what();
      }
    }
    c.assets.push_back(std::move(a));
  }
  std::stable_sort(c.assets.begin(), c.assets.end(), [](const AssetInfo &x, const AssetInfo &y) {
    return int(x.kind) != int(y.kind) ? int(x.kind) < int(y.kind) : x.path < y.path;
  });
  return c;
}

const AssetInfo *AssetCatalog::find(const std::string &path) const
{
  std::string n = normalPath(path);
  for (const AssetInfo &a : assets)
    if (a.path == n) return &a;
  return nullptr;
}

// --- File operations ---------------------------------------------------------------------

bool importAsset(const std::string &source, const std::string &dir, std::string *pNewPath, std::string *pError)
{
  std::error_code ec;
  fs::path target = fs::path(dir) / fs::path(source).filename();
  if (!fs::exists(source, ec))
  {
    if (pError) *pError = source + " does not exist";
    return false;
  }
  if (fs::exists(target, ec))
  {
    if (pError) *pError = target.string() + " exists already";
    return false;
  }
  fs::create_directories(dir, ec);
  if (!fs::copy_file(source, target, ec))
  {
    if (pError) *pError = "cannot copy to " + target.string() + ": " + ec.message();
    return false;
  }
  // Overrides travel with the asset.
  if (fs::exists(overridesPath(source), ec)) fs::copy_file(overridesPath(source), overridesPath(target.string()), ec);
  if (pNewPath) *pNewPath = normalPath(target.string());
  return true;
}

bool deleteAsset(const std::string &path, std::string *pError)
{
  std::error_code ec;
  if (!fs::remove(path, ec))
  {
    if (pError) *pError = "cannot delete " + path + (ec ? ": " + ec.message() : std::string());
    return false;
  }
  fs::remove(overridesPath(path), ec);
  return true;
}

bool createRoom(const NewRoom &room, std::string *pError)
{
  auto fail = [&](const std::string &why) {
    if (pError) *pError = why;
    return false;
  };
  std::error_code ec;
  if (room.roomId.empty()) return fail("the room needs an id");
  if (fs::exists(room.path, ec)) return fail(room.path + " exists already");
  if (room.tileSize % 8 != 0 || room.tileSize <= 0) return fail("the tile size must be a multiple of 8");
  int width = (room.screensX * 240 + room.tileSize - 1) / room.tileSize;
  int height = (room.screensY * 160 + room.tileSize - 1) / room.tileSize;
  if (width <= 0 || height <= 0) return fail("the room must be at least one screen");

  fs::path dir = fs::path(room.path).parent_path();
  auto rel = [&](const std::string &p) { return fs::path(normalPath(p)).lexically_relative(normalPath(dir.string())).generic_string(); };

  std::ostringstream tilesets;
  int nextGid = 1;
  try
  {
    if (!room.tileset.empty())
    {
      if (kindOf(room.tileset) == AssetKind::Tileset)
      {
        tilesets << " <tileset firstgid=\"" << nextGid << "\" source=\"" << escape(rel(room.tileset)) << "\"/>\n";
        nextGid += tilesetCount(room.tileset);
      }
      else
      {
        Image img = loadImage(room.tileset);
        int columns = img.width / room.tileSize, rows = img.height / room.tileSize;
        if (columns == 0 || rows == 0) return fail(room.tileset + " is smaller than one tile");
        tilesets << " <tileset firstgid=\"" << nextGid << "\" name=\"" << escape(fs::path(room.tileset).stem().string())
                 << "\" tilewidth=\"" << room.tileSize << "\" tileheight=\"" << room.tileSize << "\" tilecount=\"" << columns * rows
                 << "\" columns=\"" << columns << "\">\n  <image source=\"" << escape(rel(room.tileset)) << "\" width=\"" << img.width
                 << "\" height=\"" << img.height << "\"/>\n </tileset>\n";
        nextGid += columns * rows;
      }
    }
    std::string collisionTsx = (fs::path(room.engineDir) / "blutmond_collision.tsx").string();
    std::string materialTsx = (fs::path(room.engineDir) / "blutmond_material.tsx").string();
    if (!fs::exists(collisionTsx, ec)) return fail(collisionTsx + " not found (the engine's collision tiles)");
    int collisionGid = nextGid;
    tilesets << " <tileset firstgid=\"" << collisionGid << "\" source=\"" << escape(rel(collisionTsx)) << "\"/>\n";
    nextGid += tilesetCount(collisionTsx);
    if (fs::exists(materialTsx, ec))
    {
      tilesets << " <tileset firstgid=\"" << nextGid << "\" source=\"" << escape(rel(materialTsx)) << "\"/>\n";
      nextGid += tilesetCount(materialTsx);
    }

    auto layer = [&](int id, const std::string &name, const std::string &role, int solidRows, int solidGid) {
      std::ostringstream s;
      s << " <layer id=\"" << id << "\" name=\"" << name << "\" width=\"" << width << "\" height=\"" << height << "\"";
      if (role == "collision") s << " opacity=\"0.5\"";
      s << ">\n  <properties>\n   <property name=\"role\" value=\"" << role << "\"/>\n  </properties>\n  <data encoding=\"csv\">\n";
      for (int y = 0; y < height; y++)
      {
        for (int x = 0; x < width; x++)
        {
          s << (y >= height - solidRows ? solidGid : 0);
          if (x + 1 < width || y + 1 < height) s << ",";
        }
        s << "\n";
      }
      s << "</data>\n </layer>\n";
      return s.str();
    };

    int pxW = width * room.tileSize, pxH = height * room.tileSize;
    std::ofstream out(room.path, std::ios::binary);
    if (!out) return fail("cannot write " + room.path);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<map version=\"1.8\" tiledversion=\"1.8.0\" orientation=\"orthogonal\" renderorder=\"right-down\" width=\"" << width
        << "\" height=\"" << height << "\" tilewidth=\"" << room.tileSize << "\" tileheight=\"" << room.tileSize
        << "\" infinite=\"0\" nextlayerid=\"6\" nextobjectid=\"3\">\n"
        << " <properties>\n  <property name=\"room_id\" value=\"" << escape(room.roomId) << "\"/>\n  <property name=\"area\" value=\""
        << escape(room.area) << "\"/>\n </properties>\n"
        << tilesets.str()
        << layer(1, "background", "background", 0, 0) << layer(2, "main", "main", 0, 0)
        << layer(3, "collision", "collision", 1, collisionGid + 1)  // a solid floor to stand on
        << " <objectgroup id=\"5\" name=\"objects\">\n  <properties>\n   <property name=\"role\" value=\"objects\"/>\n  </properties>\n"
        << "  <object id=\"1\" name=\"start\" type=\"PlayerStart\" x=\"32\" y=\"" << pxH - room.tileSize << "\">\n   <point/>\n  </object>\n"
        << "  <object id=\"2\" name=\"room\" type=\"CameraZone\" x=\"0\" y=\"0\" width=\"" << pxW << "\" height=\"" << pxH << "\"/>\n"
        << " </objectgroup>\n</map>\n";
    if (!out) return fail("cannot write " + room.path);
  }
  catch (const std::exception &e)
  {
    return fail(e.what());
  }
  return true;
}

}  // namespace bm::editor
