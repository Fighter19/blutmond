// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "light_bake.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace bm::light {

Vec3 normalize(Vec3 v)
{
  float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (l < 1e-6f) return {0, 0, 1};
  return {v.x / l, v.y / l, v.z / l};
}

namespace {

uint16_t colourOf(const std::string &s)
{
  if (s.size() < 7 || s[0] != '#') return 0;
  uint32_t v = uint32_t(std::stoul(s.substr(s.size() - 6), nullptr, 16));
  auto q = [](uint32_t c) { return uint16_t(std::lround(float(c) * 31.0f / 255.0f)); };
  return uint16_t(q((v >> 16) & 255) | (q((v >> 8) & 255) << 5) | (q(v & 255) << 10));
}

float number(const tmx::Properties &p, const char *key, float fallback)
{
  auto it = p.find(key);
  if (it == p.end() || it->second.empty()) return fallback;
  try
  {
    return std::stof(it->second);
  }
  catch (const std::exception &)
  {
    return fallback;
  }
}

}  // namespace

Light find(const tmx::Map &map, Report &report)
{
  Light l;
  const tmx::Object *found = nullptr;
  for (const tmx::ObjectGroup &g : map.objectGroups)
    for (const tmx::Object &o : g.objects)
      if (o.type == "Light")
      {
        l.sources.push_back(Source{float(o.x + o.width / 2), float(o.y + o.height / 2)});
        if (found != nullptr)
        {
          if (o.properties != found->properties)
            report.warning("lighting", map.path + " object " + std::to_string(o.id), "the lights of a room share the first one's properties (they flicker together)");
          continue;
        }
        found = &o;
      }
  if (l.sources.size() > 4)
  {
    report.error("lighting", map.path, "a room has at most 4 lights");
    l.sources.resize(4);
  }
  std::string mode = map.property("lighting");
  if (found == nullptr)
  {
    if (!mode.empty()) report.warning("lighting", map.path, "'lighting' without a Light object: the room is not lit");
    return l;
  }
  if (mode.empty() || mode == "palette") l.mode = Mode::Palette;
  else if (mode == "layer") l.mode = Mode::Layer;
  else
  {
    report.error("lighting", map.path, "unknown lighting '" + mode + "' (palette, layer)");
    return l;
  }
  const tmx::Properties &p = found->properties;
  l.objectId = found->id;
  l.x = float(found->x + found->width / 2);
  l.y = float(found->y + found->height / 2);
  l.radius = std::clamp(number(p, "radius", 72), 8.0f, 255.0f);
  l.height = std::clamp(number(p, "height", 12), 0.0f, 127.0f);
  l.intensity = std::clamp(number(p, "intensity", 1), 0.0f, 3.9f);
  l.flicker = std::clamp(number(p, "flicker", 0), 0.0f, 1.0f);
  l.flickerSpeed = std::clamp(int(number(p, "flicker_speed", 6)), 1, 255);
  l.gain = std::clamp(number(p, "gain", 0.5f), 0.0f, 3.9f);
  auto c = p.find("colour");
  if (c != p.end()) l.colour = colourOf(c->second);
  else l.colour = colourOf("#ffc080");
  auto s = p.find("shadows");
  l.shadows = s == p.end() || s->second != "false";
  l.ambient = std::clamp(float(std::atof(map.property("ambient", "1").c_str())), 0.0f, 1.0f);
  std::string index = map.property("light_index", "shared");
  if (index == "fitted") l.index = Light::Index::Fitted;
  else if (index == "normals") l.index = Light::Index::Normals;
  else if (index == "stream") l.index = Light::Index::Stream;
  else if (index == "hybrid") l.index = Light::Index::Hybrid;
  else if (index != "shared") report.error("lighting", map.path, "unknown light_index '" + index + "' (shared, fitted, normals, stream, hybrid)");
  l.streamBanks = std::clamp(std::atoi(map.property("light_stream_banks", "2").c_str()), 0, 14);
  l.fitted = l.index == Light::Index::Shared || l.index == Light::Index::Fitted;
  {
    std::string normals = map.property("light_normals", "7");
    int count = std::atoi(normals.c_str());
    l.streamNormals = count == 15 || (count >= 3 && count <= 7) ? count : 7;
    if (l.streamNormals != count) report.error("lighting", map.path, "light_normals is 3 to 7, or 15");
  }
  std::string bands = map.property("light_bands", "middle");
  if (bands == "mean") l.bands = Light::Bands::Mean;
  else if (bands == "ramp") l.bands = Light::Bands::Ramp;
  else if (bands != "middle") report.error("lighting", map.path, "unknown light_bands '" + bands + "' (middle, mean, ramp)");
  if (l.bands == Light::Bands::Ramp && l.streamNormals == 15) report.error("lighting", map.path, "light_bands = ramp needs light_normals 3 to 7");
  l.bandWidth = std::clamp(std::atoi(map.property("light_band", "24").c_str()), 8, 255);
  l.directions = std::clamp(std::atoi(map.property("light_directions", "8").c_str()), 1, 64);
  l.tolerance = std::clamp(float(std::atof(map.property("light_tolerance", "0.1").c_str())), 0.0f, 1.0f);
  return l;
}

float falloff(const Light &light, float d2)
{
  float r2 = light.radius * light.radius;
  if (d2 >= r2) return 0;
  float f = 1 - d2 / r2;
  return f * f;
}

float termFrom(const Light &light, const Source &source, float px, float py, Vec3 n)
{
  float dx = source.x - px, dy = source.y - py;
  float f = falloff(light, dx * dx + dy * dy);
  if (f <= 0) return 0;
  Vec3 l = normalize({dx, dy, light.height});
  return std::max(0.0f, dot(n, l)) * f;
}

float term(const Light &light, float px, float py, Vec3 n)
{
  if (light.sources.empty()) return termFrom(light, Source{light.x, light.y}, px, py, n);
  float sum = 0;
  for (const Source &s : light.sources) sum += termFrom(light, s, px, py, n);
  return sum;
}

Vec3 scaledDirection(const Light &light, float px, float py)
{
  Vec3 sum{0, 0, 0};
  std::vector<Source> one{Source{light.x, light.y}};
  for (const Source &s : light.sources.empty() ? one : light.sources)
  {
    float dx = s.x - px, dy = s.y - py;
    float f = falloff(light, dx * dx + dy * dy);
    Vec3 l = normalize({dx, dy, light.height});
    sum.x += l.x * f;
    sum.y += l.y * f;
    sum.z += l.z * f;
  }
  return sum;
}

Solid Solid::fromChunk(const std::vector<uint8_t> &coll)
{
  Solid s;
  if (coll.size() < 8) return s;
  s.width = coll[0] | coll[1] << 8;
  s.height = coll[2] | coll[3] << 8;
  s.cell = coll[4] ? coll[4] : 16;
  s.cells.assign(size_t(s.width) * s.height, 0);
  for (size_t i = 0; i < s.cells.size() && 8 + i * 2 < coll.size(); i++)
  {
    uint8_t b0 = coll[8 + i * 2];
    s.cells[i] = (b0 & 0x0F) == 1 && (b0 & 0xC0) == 0xC0;
  }
  return s;
}

bool Solid::at(int cx, int cy) const
{
  if (cx < 0 || cy < 0 || cx >= width || cy >= height) return false;
  return cells[size_t(cy) * width + cx] != 0;
}

bool Solid::shadowed(float px, float py, const Light &light) const
{
  return shadowedFrom(px, py, light, Source{light.x, light.y});
}

float Solid::lit(float px, float py, const Light &light, Vec3 n, bool *pShadow) const
{
  std::vector<Source> one{Source{light.x, light.y}};
  float sum = 0;
  for (const Source &s : light.sources.empty() ? one : light.sources)
  {
    float dx = s.x - px, dy = s.y - py;
    if (falloff(light, dx * dx + dy * dy) <= 0) continue;
    if (shadowedFrom(px, py, light, s))
    {
      if (pShadow) *pShadow = true;
      continue;
    }
    sum += termFrom(light, s, px, py, n);
  }
  return sum;
}

bool Solid::shadowedFrom(float px, float py, const Light &light, const Source &source) const
{
  if (!light.shadows || cells.empty()) return false;
  int sx = int(std::floor(px / float(cell))), sy = int(std::floor(py / float(cell)));
  int lx = int(std::floor(source.x / float(cell))), ly = int(std::floor(source.y / float(cell)));
  float dx = source.x - px, dy = source.y - py;
  float len = std::sqrt(dx * dx + dy * dy);
  int steps = int(len * 2) + 1;
  // The solid cells the ray starts in (the wall or floor the pixel is part of) do not shadow
  // it; a solid cell after it has been in the open does.
  bool open = !at(sx, sy);
  for (int k = 1; k < steps; k++)
  {
    float t = float(k) / float(steps);
    int cx = int(std::floor((px + dx * t) / float(cell))), cy = int(std::floor((py + dy * t) / float(cell)));
    if (cx == lx && cy == ly) continue;
    bool solid = at(cx, cy);
    if (!solid) open = true;
    else if (open) return true;
  }
  return false;
}

Vec3 decodeNormal(const uint8_t *rgba)
{
  Vec3 n{rgba[0] / 127.5f - 1.0f, rgba[1] / 127.5f - 1.0f, rgba[2] / 127.5f - 1.0f};
  if (n.z < 0) n.z = 0;
  return normalize(n);
}

void encodeNormal(Vec3 n, uint8_t *rgba)
{
  n = normalize(n);
  auto e = [](float v) { return uint8_t(std::clamp(int(std::lround((v + 1.0f) * 127.5f)), 0, 255)); };
  rgba[0] = e(n.x);
  rgba[1] = e(n.y);
  rgba[2] = e(n.z);
  rgba[3] = 255;
}

Image autoNormals(const Image &image, int cellW, int cellH)
{
  Image out;
  out.width = image.width;
  out.height = image.height;
  out.rgba.assign(size_t(out.width) * out.height * 4, 0);
  if (cellW <= 0 || cellH <= 0) return out;
  auto opaque = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return false;
    const uint8_t *p = image.pixel(x, y);
    return p[3] >= 128 && !(p[0] == 255 && p[1] == 0 && p[2] == 255);
  };
  std::vector<float> h(size_t(image.width) * image.height, 0);
  for (int cy0 = 0; cy0 < image.height; cy0 += cellH)
    for (int cx0 = 0; cx0 < image.width; cx0 += cellW)
    {
      int w = std::min(cellW, image.width - cx0), hh = std::min(cellH, image.height - cy0);
      // Distance to the nearest transparent pixel or the cell's edge, up to 3: a bevel.
      for (int y = 0; y < hh; y++)
        for (int x = 0; x < w; x++)
        {
          int ix = cx0 + x, iy = cy0 + y;
          if (!opaque(ix, iy)) continue;
          int d = 3;
          for (int r = 1; r <= 3 && d == 3; r++)
            for (int oy = -r; oy <= r && d == 3; oy++)
              for (int ox = -r; ox <= r; ox++)
              {
                int qx = x + ox, qy = y + oy;
                if (qx < 0 || qy < 0 || qx >= w || qy >= hh || !opaque(cx0 + qx, cy0 + qy))
                {
                  if (std::max(std::abs(ox), std::abs(oy)) == r) { d = r - 1; break; }
                }
              }
          const uint8_t *p = image.pixel(ix, iy);
          float lum = (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / 255.0f;
          h[size_t(iy) * image.width + ix] = float(d) * 0.8f + lum * 1.5f;
        }
    }
  for (int y = 0; y < image.height; y++)
    for (int x = 0; x < image.width; x++)
    {
      uint8_t *o = &out.rgba[(size_t(y) * out.width + x) * 4];
      if (!opaque(x, y))
      {
        encodeNormal({0, 0, 1}, o);
        o[3] = 0;
        continue;
      }
      int cx0 = x / cellW * cellW, cy0 = y / cellH * cellH;
      auto H = [&](int qx, int qy) {
        qx = std::clamp(qx, cx0, std::min(cx0 + cellW, image.width) - 1);
        qy = std::clamp(qy, cy0, std::min(cy0 + cellH, image.height) - 1);
        return opaque(qx, qy) ? h[size_t(qy) * image.width + qx] : 0.0f;
      };
      float gx = (H(x + 1, y) - H(x - 1, y)) * 0.5f, gy = (H(x, y + 1) - H(x, y - 1)) * 0.5f;
      encodeNormal({-gx, -gy, 1.0f}, o);
    }
  return out;
}

std::shared_ptr<Image> tilesetNormals(const tmx::Tileset &tileset, Report &report)
{
  static std::map<std::string, std::shared_ptr<Image>> cache;
  auto p = tileset.properties.find("normal");
  if (p == tileset.properties.end() || p->second.empty() || !tileset.image) return nullptr;
  std::string key = tileset.imagePath + "|" + p->second;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  std::shared_ptr<Image> img;
  if (p->second == "auto")
  {
    img = std::make_shared<Image>(autoNormals(*tileset.image, tileset.tileWidth, tileset.tileHeight));
  }
  else
  {
    std::string dir = tileset.source;
    size_t slash = dir.find_last_of('/');
    dir = slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
    std::string path = p->second[0] == '/' ? p->second : dir + p->second;
    try
    {
      img = std::make_shared<Image>(loadImage(path));
      if (img->width != tileset.image->width || img->height != tileset.image->height)
      {
        report.error("lighting", path, "a normal map must be the size of its tileset's image");
        img = nullptr;
      }
    }
    catch (const std::exception &e)
    {
      report.error("lighting", path, e.what());
      img = nullptr;
    }
  }
  return cache[key] = img;
}

std::vector<Vec3> cluster(const std::vector<Vec3> &points, const std::vector<float> &weights, int k)
{
  std::vector<Vec3> centres;
  if (points.empty() || k <= 0) return centres;
  auto d2 = [](Vec3 a, Vec3 b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return x * x + y * y + z * z; };
  // Start: the heaviest point, then each time the point farthest from the centres so far.
  size_t first = size_t(std::max_element(weights.begin(), weights.end()) - weights.begin());
  centres.push_back(points[first]);
  while (int(centres.size()) < k)
  {
    float best = 1e-6f;
    int pick = -1;
    for (size_t i = 0; i < points.size(); i++)
    {
      float m = 1e30f;
      for (const Vec3 &c : centres) m = std::min(m, d2(points[i], c));
      if (m > best) best = m, pick = int(i);
    }
    if (pick < 0) break;
    centres.push_back(points[size_t(pick)]);
  }
  for (int iter = 0; iter < 24; iter++)
  {
    std::vector<Vec3> sum(centres.size(), Vec3{0, 0, 0});
    std::vector<float> weight(centres.size(), 0);
    for (size_t i = 0; i < points.size(); i++)
    {
      int c = nearest(centres, points[i]);
      float w = weights[i];
      sum[size_t(c)].x += points[i].x * w;
      sum[size_t(c)].y += points[i].y * w;
      sum[size_t(c)].z += points[i].z * w;
      weight[size_t(c)] += w;
    }
    for (size_t c = 0; c < centres.size(); c++)
      if (weight[c] > 0) centres[c] = {sum[c].x / weight[c], sum[c].y / weight[c], sum[c].z / weight[c]};
  }
  return centres;
}

int nearest(const std::vector<Vec3> &centres, Vec3 p)
{
  int best = 0;
  float bestD = 1e30f;
  for (size_t c = 0; c < centres.size(); c++)
  {
    float x = centres[c].x - p.x, y = centres[c].y - p.y, z = centres[c].z - p.z;
    float d = x * x + y * y + z * z;
    if (d < bestD) bestD = d, best = int(c);
  }
  return best;
}

}  // namespace bm::light
