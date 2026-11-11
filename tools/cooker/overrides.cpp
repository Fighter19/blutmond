// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "overrides.h"

#include "xml.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace bm {

bool AssetOverrides::empty() const
{
  for (const auto &[profile, map] : colours)
    if (!map.empty()) return false;
  return true;
}

bool AssetOverrides::colourFor(const std::string &profile, uint32_t rgb, uint32_t *pOut) const
{
  for (const std::string &p : {profile, std::string("*")})
  {
    auto it = colours.find(p);
    if (it == colours.end()) continue;
    auto c = it->second.find(rgb);
    if (c != it->second.end())
    {
      *pOut = c->second;
      return true;
    }
  }
  return false;
}

std::string overridesPath(const std::string &assetPath)
{
  return assetPath + ".bmasset";
}

std::string rgbHex(uint32_t rgb)
{
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%06x", unsigned(rgb & 0xFFFFFF));
  return buf;
}

bool parseRgbHex(const std::string &s, uint32_t *pRgb)
{
  if (s.size() != 7 || s[0] != '#') return false;
  uint32_t v = 0;
  for (size_t i = 1; i < 7; i++)
  {
    char c = s[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    v = v * 16 + uint32_t(d);
  }
  *pRgb = v;
  return true;
}

AssetOverrides loadOverrides(const std::string &assetPath)
{
  AssetOverrides o;
  std::string path = overridesPath(assetPath);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return o;
  auto root = xml::parseFile(path);
  if (root->name != "asset") throw std::runtime_error(path + ": not an asset file");
  for (const xml::Element *ov : root->childrenNamed("override"))
  {
    std::string profile = ov->attr("profile", "*");
    for (const xml::Element *c : ov->childrenNamed("colour"))
    {
      uint32_t from = 0, to = 0;
      if (!parseRgbHex(c->attr("from"), &from) || !parseRgbHex(c->attr("to"), &to))
        throw std::runtime_error(path + ": colours are written as #rrggbb");
      o.colours[profile][from] = to;
    }
  }
  return o;
}

bool saveOverrides(const std::string &assetPath, const AssetOverrides &o)
{
  std::string path = overridesPath(assetPath);
  if (o.empty())
  {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<asset version=\"1\">\n";
  for (const auto &[profile, map] : o.colours)
  {
    if (map.empty()) continue;
    out << " <override profile=\"" << profile << "\">\n";
    for (const auto &[from, to] : map) out << "  <colour from=\"" << rgbHex(from) << "\" to=\"" << rgbHex(to) << "\"/>\n";
    out << " </override>\n";
  }
  out << "</asset>\n";
  return bool(out);
}

}  // namespace bm
