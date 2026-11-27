// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "sprite_asset.h"

#include "light_bake.h"
#include "overrides.h"
#include "xml.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace bm::sprite {

namespace {

int toInt(const xml::Element *e, const char *key, int fallback)
{
  return e->has(key) ? std::stoi(e->attr(key)) : fallback;
}

// The 12 hardware sprite sizes, in tiles.
const int kSizes[12][2] = {{1, 1}, {2, 2}, {4, 4}, {8, 8}, {2, 1}, {4, 1}, {4, 2}, {8, 4}, {1, 2}, {1, 4}, {2, 4}, {4, 8}};

// Packed tiles (sprite-format.md): LZ77 with a window of 512 bytes, so that the runtime unpacks
// with 512 bytes of history. Groups of 8 items behind a flags byte (bit i for item i, from the
// lowest): 0 a byte as it is, 1 a copy of 3 to 18 bytes from 1 to 512 back, in two bytes:
// (length - 3) << 4 | (distance - 1) >> 8, (distance - 1) & 255.
std::vector<uint8_t> packTiles(const std::vector<uint8_t> &in)
{
  std::vector<uint8_t> out;
  size_t i = 0;
  while (i < in.size())
  {
    size_t flagsAt = out.size();
    out.push_back(0);
    for (int item = 0; item < 8 && i < in.size(); item++)
    {
      size_t bestLength = 0, bestDistance = 0;
      for (size_t d = 1; d <= 512 && d <= i; d++)
      {
        size_t n = 0;
        while (n < 18 && i + n < in.size() && in[i + n] == in[i + n - d]) n++;
        if (n > bestLength)
        {
          bestLength = n;
          bestDistance = d;
        }
      }
      if (bestLength >= 3)
      {
        out[flagsAt] = uint8_t(out[flagsAt] | (1u << item));
        out.push_back(uint8_t(((bestLength - 3) << 4) | ((bestDistance - 1) >> 8)));
        out.push_back(uint8_t((bestDistance - 1) & 255));
        i += bestLength;
      }
      else
      {
        out.push_back(in[i++]);
      }
    }
  }
  return out;
}

int luminance(uint16_t c)
{
  return (c & 31) * 2 + ((c >> 5) & 31) * 5 + ((c >> 10) & 31);
}

}  // namespace

const char *boxKindName(BoxKind kind)
{
  return kind == BoxKind::Hit ? "hit" : kind == BoxKind::Guard ? "guard" : "hurt";
}

std::vector<NamedBox> frameBoxes(const Sprite &s, const Frame &f)
{
  std::vector<NamedBox> out;
  for (int k = 0; k < 3; k++)
  {
    bool own = std::any_of(f.boxes.begin(), f.boxes.end(), [&](const NamedBox &b) { return int(b.kind) == k; });
    for (const NamedBox &b : own ? f.boxes : s.boxes)
      if (int(b.kind) == k) out.push_back(b);
  }
  return out;
}

Sprite load(const std::string &path)
{
  Sprite s;
  s.path = path;
  s.name = fs::path(path).stem().string();
  auto root = xml::parseFile(path);
  if (root->name != "sprite") throw std::runtime_error(path + ": not a sprite file");
  if (root->attr("version", "1") != "1") throw std::runtime_error(path + ": sprite version " + root->attr("version") + " is not supported");
  s.imageAttr = root->attr("image");
  if (s.imageAttr.empty()) throw std::runtime_error(path + ": no image");
  s.imagePath = (fs::path(path).parent_path() / s.imageAttr).lexically_normal().string();
  s.image = std::make_shared<Image>(loadImage(s.imagePath));
  s.normalAttr = root->attr("normal");
  s.emissive = root->attr("emissive") == "true";
  {
    std::string list = root->attr("shiny");
    size_t at = 0;
    while ((at = list.find('#', at)) != std::string::npos && at + 7 <= list.size())
    {
      s.shiny.push_back(uint32_t(std::stoul(list.substr(at + 1, 6), nullptr, 16)));
      at += 7;
    }
  }
  auto box = [](const xml::Element *e) { return Box{toInt(e, "x", 0), toInt(e, "y", 0), toInt(e, "w", 0), toInt(e, "h", 0)}; };
  auto boxes = [&](const xml::Element *parent) {
    std::vector<NamedBox> out;
    for (const xml::Element *b : parent->childrenNamed("box"))
    {
      NamedBox nb;
      std::string kind = b->attr("kind", "hurt");
      if (kind == "hurt") nb.kind = BoxKind::Hurt;
      else if (kind == "hit") nb.kind = BoxKind::Hit;
      else if (kind == "guard") nb.kind = BoxKind::Guard;
      else throw std::runtime_error(path + ": a box is of kind hurt, hit or guard, not '" + kind + "'");
      nb.name = b->attr("name");
      nb.box = box(b);
      out.push_back(nb);
    }
    return out;
  };
  auto points = [&](const xml::Element *parent) {
    std::vector<Point> out;
    for (const xml::Element *e : parent->childrenNamed("point"))
    {
      Point pt;
      pt.name = e->attr("name");
      if (pt.name.empty()) throw std::runtime_error(path + ": a point needs a name");
      pt.x = toInt(e, "x", 0);
      pt.y = toInt(e, "y", 0);
      pt.angle = toInt(e, "angle", 0);
      std::string order = e->attr("order", "front");
      if (order != "front" && order != "behind") throw std::runtime_error(path + ": a point's order is front or behind, not '" + order + "'");
      pt.behind = order == "behind";
      out.push_back(pt);
    }
    return out;
  };
  if (const xml::Element *h = root->child("hitbox")) s.hitbox = box(h);
  s.boxes = boxes(root.get());
  s.points = points(root.get());
  for (const xml::Element *f : root->childrenNamed("frame"))
  {
    Frame fr;
    fr.x = toInt(f, "x", 0);
    fr.y = toInt(f, "y", 0);
    fr.w = toInt(f, "w", 16);
    fr.h = toInt(f, "h", 16);
    fr.ax = toInt(f, "ax", fr.w / 2);
    fr.ay = toInt(f, "ay", fr.h);
    if (const xml::Element *h = f->child("hitbox"))
    {
      fr.ownHitbox = true;
      fr.hitbox = box(h);
    }
    fr.boxes = boxes(f);
    fr.points = points(f);
    s.frames.push_back(fr);
  }
  for (const xml::Element *pe : root->childrenNamed("palette"))
  {
    PaletteVariant v;
    v.name = pe->attr("name");
    if (v.name.empty() || v.name == "default") throw std::runtime_error(path + ": a palette needs a name other than 'default'");
    v.imageAttr = pe->attr("image");
    if (!v.imageAttr.empty())
    {
      v.imagePath = (fs::path(path).parent_path() / v.imageAttr).lexically_normal().string();
      v.image = std::make_shared<Image>(loadImage(v.imagePath));
    }
    auto rgb = [&](const std::string &t) -> uint32_t {
      if (t.size() != 7 || t[0] != '#') throw std::runtime_error(path + ": palette '" + v.name + "': a colour is #rrggbb, not '" + t + "'");
      return uint32_t(std::stoul(t.substr(1), nullptr, 16));
    };
    for (const xml::Element *sw : pe->childrenNamed("swap")) v.swaps.push_back({rgb(sw->attr("from")), rgb(sw->attr("to"))});
    s.palettes.push_back(v);
  }
  for (const xml::Element *a : root->childrenNamed("animation"))
  {
    Animation an;
    an.name = a->attr("name");
    an.loop = a->attr("loop", "1") != "0";
    for (const xml::Element *st : a->childrenNamed("step")) an.steps.push_back({toInt(st, "frame", 0), toInt(st, "duration", 6)});
    an.directions = toInt(a, "directions", 0);
    an.from = toInt(a, "from", 192);
    an.to = toInt(a, "to", 64);
    s.animations.push_back(an);
  }
  return s;
}

bool save(const Sprite &s, std::string *pError)
{
  std::ofstream out(s.path, std::ios::binary);
  if (!out)
  {
    if (pError) *pError = "cannot write " + s.path;
    return false;
  }
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<sprite version=\"1\" image=\"" << s.imageAttr << "\"";
  if (!s.normalAttr.empty()) out << " normal=\"" << s.normalAttr << "\"";
  if (s.emissive) out << " emissive=\"true\"";
  if (!s.shiny.empty())
  {
    out << " shiny=\"";
    for (size_t i = 0; i < s.shiny.size(); i++)
    {
      char hex[16];
      std::snprintf(hex, sizeof(hex), "%s#%06x", i ? "," : "", s.shiny[i]);
      out << hex;
    }
    out << "\"";
  }
  out << ">\n";
  out << " <hitbox x=\"" << s.hitbox.x << "\" y=\"" << s.hitbox.y << "\" w=\"" << s.hitbox.w << "\" h=\"" << s.hitbox.h << "\"/>\n";
  auto writeBoxes = [&](const std::vector<NamedBox> &boxes, const char *indent) {
    for (const NamedBox &b : boxes)
    {
      out << indent << "<box kind=\"" << boxKindName(b.kind) << "\"";
      if (!b.name.empty()) out << " name=\"" << b.name << "\"";
      out << " x=\"" << b.box.x << "\" y=\"" << b.box.y << "\" w=\"" << b.box.w << "\" h=\"" << b.box.h << "\"/>\n";
    }
  };
  auto writePoints = [&](const std::vector<Point> &points, const char *indent) {
    for (const Point &p : points)
    {
      out << indent << "<point name=\"" << p.name << "\" x=\"" << p.x << "\" y=\"" << p.y << "\"";
      if (p.angle != 0) out << " angle=\"" << p.angle << "\"";
      if (p.behind) out << " order=\"behind\"";
      out << "/>\n";
    }
  };
  writeBoxes(s.boxes, " ");
  writePoints(s.points, " ");
  for (const Frame &f : s.frames)
  {
    out << " <frame x=\"" << f.x << "\" y=\"" << f.y << "\" w=\"" << f.w << "\" h=\"" << f.h << "\" ax=\"" << f.ax << "\" ay=\"" << f.ay << "\"";
    if (!f.ownHitbox && f.boxes.empty() && f.points.empty())
    {
      out << "/>\n";
      continue;
    }
    out << ">\n";
    if (f.ownHitbox) out << "  <hitbox x=\"" << f.hitbox.x << "\" y=\"" << f.hitbox.y << "\" w=\"" << f.hitbox.w << "\" h=\"" << f.hitbox.h << "\"/>\n";
    writeBoxes(f.boxes, "  ");
    writePoints(f.points, "  ");
    out << " </frame>\n";
  }
  for (const PaletteVariant &v : s.palettes)
  {
    out << " <palette name=\"" << v.name << "\"";
    if (!v.imageAttr.empty()) out << " image=\"" << v.imageAttr << "\"";
    if (v.swaps.empty())
    {
      out << "/>\n";
      continue;
    }
    out << ">\n";
    char from[8], to[8];
    for (const auto &sw : v.swaps)
    {
      std::snprintf(from, sizeof from, "#%06x", unsigned(sw.first & 0xFFFFFF));
      std::snprintf(to, sizeof to, "#%06x", unsigned(sw.second & 0xFFFFFF));
      out << "  <swap from=\"" << from << "\" to=\"" << to << "\"/>\n";
    }
    out << " </palette>\n";
  }
  for (const Animation &a : s.animations)
  {
    out << " <animation name=\"" << a.name << "\" loop=\"" << (a.loop ? 1 : 0) << "\"";
    if (a.directions > 0) out << " directions=\"" << a.directions << "\" from=\"" << a.from << "\" to=\"" << a.to << "\"";
    out << ">\n";
    for (const Step &st : a.steps) out << "  <step frame=\"" << st.frame << "\" duration=\"" << st.duration << "\"/>\n";
    out << " </animation>\n";
  }
  out << "</sprite>\n";
  return bool(out);
}

int directionAngle(const Animation &a, size_t i)
{
  int span = (a.to - a.from) & 255;
  if (a.directions <= 1) return a.from & 255;
  return (a.from + span * int(i) / (a.directions - 1)) & 255;
}

Cooked cook(const Sprite &s, const Profile &profile, Report &report)
{
  for (const Animation &a : s.animations)
  {
    if (a.directions == 0) continue;
    std::string where = s.path + " animation '" + a.name + "'";
    if (a.directions < 0 || a.directions > 32) report.error("does-not-fit", where, "a direction set has 1 to 32 directions");
    else if (int(a.steps.size()) != a.directions)
      report.error("directions", where, std::to_string(a.directions) + " directions need as many steps, not " + std::to_string(a.steps.size()));
    if (a.from < 0 || a.from > 255 || a.to < 0 || a.to > 255) report.error("does-not-fit", where, "an angle is 0 to 255");
  }
  Cooked out;
  out.palette.assign(16, 0);
  if (!s.image) return out;
  const Image &img = *s.image;
  AssetOverrides overrides;
  try
  {
    overrides = loadOverrides(s.imagePath);
  }
  catch (const std::exception &e)
  {
    report.error("does-not-fit", overridesPath(s.imagePath), e.what());
  }

  // Colours: BGR555 of every opaque pixel inside a frame.
  constexpr uint16_t NONE = 0xFFFF;
  size_t replaced = 0;
  std::set<uint32_t> replacedSources;
  auto colourAt = [&](int x, int y) -> uint16_t {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return NONE;
    const uint8_t *p = img.pixel(x, y);
    if (p[3] < 128 || (p[0] == 255 && p[1] == 0 && p[2] == 255)) return NONE;
    uint32_t rgb = uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2], repl = 0;
    if (overrides.colourFor(profile.name, rgb, &repl))
    {
      replacedSources.insert(rgb);
      rgb = repl;
    }
    auto q = [](uint32_t v) { return uint16_t(std::lround(float(v) * 31.0f / 255.0f)); };
    return uint16_t(q((rgb >> 16) & 255) | (q((rgb >> 8) & 255) << 5) | (q(rgb & 255) << 10));
  };
  std::set<uint16_t> colours;
  for (const Frame &f : s.frames)
    for (int y = 0; y < f.h; y++)
      for (int x = 0; x < f.w; x++)
        if (uint16_t c = colourAt(f.x + x, f.y + y); c != NONE) colours.insert(c);
  replaced = replacedSources.size();
  if (replaced)
    report.info("colour-override", s.imagePath, std::to_string(replaced) + " source colours replaced for profile " + profile.name);
  if (colours.size() > 15)
  {
    report.error("does-not-fit", s.path,
                 "the sprite uses " + std::to_string(colours.size()) + " colours; a sprite has one palette bank of 15 plus transparency. Reduce its colours");
    return out;
  }
  std::vector<uint16_t> sorted(colours.begin(), colours.end());
  std::stable_sort(sorted.begin(), sorted.end(), [](uint16_t a, uint16_t b) { return luminance(a) < luminance(b); });
  std::map<uint16_t, uint8_t> indexOf;
  for (size_t i = 0; i < sorted.size(); i++)
  {
    out.palette[i + 1] = sorted[i];
    indexOf[sorted[i]] = uint8_t(i + 1);
  }

  // Palette variants: the same slots, other colours. From a second image, each slot takes the
  // colour its pixels have there (pixels of one slot that disagree are reported); from swaps,
  // a slot whose source colour is swapped takes the new one. Other slots keep theirs.
  auto bgr = [](uint32_t rgb) {
    auto q = [](uint32_t v) { return uint16_t(std::lround(float(v) * 31.0f / 255.0f)); };
    return uint16_t(q((rgb >> 16) & 255) | (q((rgb >> 8) & 255) << 5) | (q(rgb & 255) << 10));
  };
  for (const PaletteVariant &v : s.palettes)
  {
    std::vector<uint16_t> pal = out.palette;
    if (v.image)
    {
      const Image &vi = *v.image;
      if (vi.width != img.width || vi.height != img.height)
        report.error("does-not-fit", s.path, "palette '" + v.name + "': " + v.imageAttr + " is not the sheet's size");
      std::vector<int> seen(16, -1);
      bool told = false;
      for (const Frame &f : s.frames)
        for (int y = 0; y < f.h; y++)
          for (int x = 0; x < f.w; x++)
          {
            uint16_t c = colourAt(f.x + x, f.y + y);
            int px = f.x + x, py = f.y + y;
            if (c == NONE || px >= vi.width || py >= vi.height) continue;
            const uint8_t *q = vi.pixel(px, py);
            uint16_t to = bgr(uint32_t(q[0]) << 16 | uint32_t(q[1]) << 8 | q[2]);
            uint8_t slot = indexOf[c];
            if (seen[slot] >= 0 && seen[slot] != to && !told)
            {
              report.warning("palette-pairs", s.path, "palette '" + v.name + "': the pixels of one colour have different colours in " + v.imageAttr +
                                                          " (the first is used)");
              told = true;
            }
            if (seen[slot] < 0) seen[slot] = to;
          }
      for (int k = 1; k < 16; k++)
        if (seen[size_t(k)] >= 0) pal[size_t(k)] = uint16_t(seen[size_t(k)]);
    }
    for (const auto &sw : v.swaps)
    {
      uint16_t from = bgr(sw.first);
      auto it = indexOf.find(from);
      if (it == indexOf.end())
        report.warning("palette-pairs", s.path, "palette '" + v.name + "': the sheet has no colour to swap like that");
      else
        pal[it->second] = bgr(sw.second);
    }
    out.paletteNames.push_back(v.name);
    out.variants.push_back(pal);
  }

  // Light (lighting.md): the slots become (colour, normal) pairs. Each colour starts as one slot
  // with the mean normal of its pixels; while slots are left, the one whose normals spread most
  // is split in two. A pixel takes the slot of its colour with the nearest normal.
  const bool lit = !s.normalAttr.empty() || s.emissive || !s.shiny.empty();
  std::vector<light::Vec3> pixelNormal;   // per sheet pixel
  std::vector<uint8_t> pixelGlow;          // per sheet pixel: black in the normal map (gives light)
  struct Slot { uint8_t colour; light::Vec3 normal; };
  std::vector<Slot> slots;
  std::vector<std::vector<int>> slotsOfColour(16);
  if (lit)
  {
    pixelNormal.assign(size_t(img.width) * img.height, light::Vec3{});
    if (s.normalAttr == "auto")
    {
      for (const Frame &f : s.frames)
      {
        Image part;
        part.width = std::max(0, std::min(f.w, img.width - f.x));
        part.height = std::max(0, std::min(f.h, img.height - f.y));
        part.rgba.resize(size_t(part.width) * part.height * 4);
        for (int y = 0; y < part.height; y++)
          for (int x = 0; x < part.width; x++)
            std::copy(img.pixel(f.x + x, f.y + y), img.pixel(f.x + x, f.y + y) + 4, &part.rgba[(size_t(y) * part.width + x) * 4]);
        Image n = light::autoNormals(part, part.width, part.height);
        for (int y = 0; y < part.height; y++)
          for (int x = 0; x < part.width; x++) pixelNormal[size_t(f.y + y) * img.width + f.x + x] = light::decodeNormal(n.pixel(x, y));
      }
    }
    else if (!s.normalAttr.empty())
    {
      std::string np = (fs::path(s.path).parent_path() / s.normalAttr).lexically_normal().string();
      try
      {
        Image n = loadImage(np);
        if (n.width != img.width || n.height != img.height) report.error("lighting", np, "a normal map must be the size of its sheet");
        else
        {
          pixelGlow.assign(size_t(img.width) * img.height, 0);
          for (int y = 0; y < img.height; y++)
            for (int x = 0; x < img.width; x++)
            {
              const uint8_t *q = n.pixel(x, y);
              pixelNormal[size_t(y) * img.width + x] = light::decodeNormal(q);
              pixelGlow[size_t(y) * img.width + x] = q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] != 0;
            }
        }
      }
      catch (const std::exception &e)
      {
        report.error("lighting", np, e.what());
      }
    }
    // Colours all of whose pixels are black in the normal map give light (lighting.md, as a
    // tile's): their slots are emissive, drawn as they are.
    std::vector<int> glowing(16, -1);   // -1 not seen, 1 all black so far, 0 not
    if (!pixelGlow.empty())
      for (const Frame &f : s.frames)
        for (int y = 0; y < f.h && f.y + y < img.height; y++)
          for (int x = 0; x < f.w && f.x + x < img.width; x++)
            if (uint16_t c = colourAt(f.x + x, f.y + y); c != NONE)
            {
              int &gl = glowing[indexOf[c]];
              bool black = pixelGlow[size_t(f.y + y) * img.width + f.x + x] != 0;
              gl = (gl != 0 && black) ? 1 : 0;
            }
    // The normals of each colour's pixels.
    std::vector<std::vector<light::Vec3>> of(16);
    for (const Frame &f : s.frames)
      for (int y = 0; y < f.h; y++)
        for (int x = 0; x < f.w; x++)
          if (uint16_t c = colourAt(f.x + x, f.y + y); c != NONE) of[indexOf[c]].push_back(pixelNormal[size_t(f.y + y) * img.width + f.x + x]);
    struct Group { uint8_t colour; std::vector<light::Vec3> normals; light::Vec3 mean; float spread; };
    auto measure = [](Group &g) {
      light::Vec3 m{0, 0, 0};
      for (const light::Vec3 &n : g.normals) m.x += n.x, m.y += n.y, m.z += n.z;
      float k = g.normals.empty() ? 1.0f : float(g.normals.size());
      g.mean = light::normalize({m.x / k, m.y / k, m.z / k});
      g.spread = 0;
      for (const light::Vec3 &n : g.normals)
      {
        float dx = n.x - g.mean.x, dy = n.y - g.mean.y, dz = n.z - g.mean.z;
        g.spread += dx * dx + dy * dy + dz * dz;
      }
    };
    std::vector<Group> groups;
    for (size_t k = 1; k <= sorted.size(); k++)
    {
      groups.push_back({uint8_t(k), of[k], {}, 0});
      measure(groups.back());
    }
    while (groups.size() < 15 && !s.normalAttr.empty())
    {
      size_t worst = 0;
      for (size_t g = 1; g < groups.size(); g++)
        if (groups[g].spread > groups[worst].spread) worst = g;
      if (groups.empty() || groups[worst].spread < 0.5f) break;
      std::vector<light::Vec3> centres = light::cluster(groups[worst].normals, std::vector<float>(groups[worst].normals.size(), 1.0f), 2);
      if (centres.size() < 2) break;
      Group a{groups[worst].colour, {}, {}, 0}, b{groups[worst].colour, {}, {}, 0};
      for (const light::Vec3 &n : groups[worst].normals) (light::nearest(centres, n) == 0 ? a : b).normals.push_back(n);
      if (a.normals.empty() || b.normals.empty()) break;
      measure(a);
      measure(b);
      groups[worst] = a;
      groups.push_back(b);
    }
    // Slots in the order of their colours, then from the top-lit to the bottom-lit.
    std::stable_sort(groups.begin(), groups.end(), [](const Group &x, const Group &y) {
      return x.colour != y.colour ? x.colour < y.colour : x.mean.y < y.mean.y;
    });
    for (const Group &g : groups)
    {
      slotsOfColour[g.colour].push_back(int(slots.size()) + 1);
      slots.push_back({g.colour, g.mean});
    }
    // The palettes over the new slots, and each slot's normal and flags.
    auto spread = [&](const std::vector<uint16_t> &pal) {
      std::vector<uint16_t> outPal(16, 0);
      for (size_t i = 0; i < slots.size(); i++) outPal[i + 1] = pal[slots[i].colour];
      return outPal;
    };
    std::vector<uint16_t> base = out.palette;
    out.palette = spread(base);
    for (auto &v : out.variants) v = spread(v);
    out.normals.assign(16, {0, 0, 127, 0});
    for (size_t i = 0; i < slots.size(); i++)
    {
      const light::Vec3 &n = slots[i].normal;
      uint8_t flags = s.emissive || glowing[slots[i].colour] == 1 ? 1 : 0;
      for (uint32_t rgb : s.shiny)
        if (bgr(rgb) == base[slots[i].colour]) flags |= 2;
      out.normals[i + 1] = {int8_t(std::lround(n.x * 127)), int8_t(std::lround(n.y * 127)), int8_t(std::lround(n.z * 127)), int8_t(flags)};
    }
    if (slots.size() > sorted.size())
      report.info("lighting", s.path, std::to_string(sorted.size()) + " colours in " + std::to_string(slots.size()) + " lit slots");
  }
  auto slotOf = [&](uint16_t c, int px, int py) -> uint8_t {
    uint8_t k = indexOf[c];
    if (!lit) return k;
    const std::vector<int> &mine = slotsOfColour[k];
    if (mine.size() <= 1) return mine.empty() ? k : uint8_t(mine[0]);
    light::Vec3 n = pixelNormal[size_t(py) * img.width + px];
    int best = mine[0];
    float bestD = 1e30f;
    for (int sl : mine)
    {
      const light::Vec3 &m = slots[size_t(sl - 1)].normal;
      float d = (m.x - n.x) * (m.x - n.x) + (m.y - n.y) * (m.y - n.y) + (m.z - n.z) * (m.z - n.z);
      if (d < bestD) bestD = d, best = sl;
    }
    return uint8_t(best);
  };

  // Frames into pieces. Identical pieces (the same run of tiles) are stored once.
  std::map<std::vector<uint8_t>, int> runs;
  for (size_t fi = 0; fi < s.frames.size(); fi++)
  {
    const Frame &f = s.frames[fi];
    CookedFrame cf;
    cf.hitbox = f.ownHitbox ? f.hitbox : s.hitbox;
    cf.boxes = frameBoxes(s, f);
    // Boxes: at most 4 of a kind, inside what a frame record can hold; names for hit and guard.
    int perKind[3] = {0, 0, 0};
    for (const NamedBox &b : cf.boxes)
    {
      std::string where = s.path + " frame " + std::to_string(fi);
      if (++perKind[int(b.kind)] == 5) report.error("does-not-fit", where, std::string("more than 4 ") + boxKindName(b.kind) + " boxes");
      if (b.box.x < -128 || b.box.x > 127 || b.box.y < -128 || b.box.y > 127 || b.box.w < 0 || b.box.w > 255 || b.box.h < 0 || b.box.h > 255)
        report.error("does-not-fit", where, "a box goes from -128 to 127 and is at most 255 wide and high");
      if (b.kind != BoxKind::Hurt && b.name.empty()) report.error("box-name", where, std::string("a ") + boxKindName(b.kind) + " box needs a name");
      if (!b.name.empty() && std::find(out.boxNames.begin(), out.boxNames.end(), b.name) == out.boxNames.end()) out.boxNames.push_back(b.name);
    }
    if (out.boxNames.size() > 8) report.error("does-not-fit", s.path, "a sprite has at most 8 box names");
    // Points: the frame's own, then the sprite's of names the frame does not have.
    cf.points = f.points;
    for (const Point &p : s.points)
      if (std::none_of(cf.points.begin(), cf.points.end(), [&](const Point &q) { return q.name == p.name; })) cf.points.push_back(p);
    for (const Point &p : cf.points)
    {
      std::string where = s.path + " frame " + std::to_string(fi);
      if (p.x < -128 || p.x > 127 || p.y < -128 || p.y > 127) report.error("does-not-fit", where, "point '" + p.name + "' goes from -128 to 127");
      if (p.angle < 0 || p.angle > 255) report.error("does-not-fit", where, "point '" + p.name + "': an angle is 0 to 255");
      if (std::find(out.pointNames.begin(), out.pointNames.end(), p.name) == out.pointNames.end()) out.pointNames.push_back(p.name);
    }
    if (cf.points.size() > 4) report.error("does-not-fit", s.path + " frame " + std::to_string(fi), "more than 4 points");
    if (out.pointNames.size() > 8) report.error("does-not-fit", s.path, "a sprite has at most 8 point names");
    if (f.w <= 0 || f.h <= 0 || f.w > 128 || f.h > 128)
    {
      report.error("does-not-fit", s.path, "frame " + std::to_string(fi) + " must be between 1 and 128 pixels wide and high");
      out.frames.push_back(cf);
      continue;
    }
    int tw = (f.w + 7) / 8, th = (f.h + 7) / 8;
    auto index = [&](int px, int py) -> uint8_t {
      if (px >= f.w || py >= f.h) return 0;
      uint16_t c = colourAt(f.x + px, f.y + py);
      return c == NONE ? 0 : slotOf(c, f.x + px, f.y + py);
    };
    std::vector<uint8_t> filled(size_t(tw) * th, 0), covered(size_t(tw) * th, 0);
    int minX = f.w, minY = f.h, maxX = -1, maxY = -1;
    for (int y = 0; y < f.h; y++)
      for (int x = 0; x < f.w; x++)
        if (index(x, y))
        {
          filled[size_t(y / 8) * tw + x / 8] = 1;
          minX = std::min(minX, x);
          maxX = std::max(maxX, x);
          minY = std::min(minY, y);
          maxY = std::max(maxY, y);
        }
    if (maxX >= 0) cf.bounds = {minX - f.ax, minY - f.ay, maxX - minX + 1, maxY - minY + 1};

    for (int cy = 0; cy < th; cy++)
      for (int cx = 0; cx < tw; cx++)
      {
        if (!filled[size_t(cy) * tw + cx] || covered[size_t(cy) * tw + cx]) continue;
        // The hardware size that covers the most new pixels for the tiles it costs.
        int best = 0, bestScore = -1000;
        for (int k = 0; k < 12; k++)
        {
          int w = kSizes[k][0], h = kSizes[k][1], fresh = 0;
          for (int y = cy; y < cy + h && y < th; y++)
            for (int x = cx; x < cx + w && x < tw; x++) fresh += filled[size_t(y) * tw + x] && !covered[size_t(y) * tw + x];
          int score = fresh * 4 - w * h;
          if (score > bestScore || (score == bestScore && w * h > kSizes[best][0] * kSizes[best][1]))
          {
            best = k;
            bestScore = score;
          }
        }
        int w = kSizes[best][0], h = kSizes[best][1];
        for (int y = cy; y < cy + h && y < th; y++)
          for (int x = cx; x < cx + w && x < tw; x++) covered[size_t(y) * tw + x] = 1;
        std::vector<uint8_t> run(size_t(w) * h * 64);
        for (int ty = 0; ty < h; ty++)
          for (int tx = 0; tx < w; tx++)
            for (int p = 0; p < 64; p++) run[(size_t(ty) * w + tx) * 64 + p] = index((cx + tx) * 8 + p % 8, (cy + ty) * 8 + p / 8);
        auto it = runs.find(run);
        int tile;
        if (it != runs.end()) tile = it->second;
        else
        {
          tile = int(out.tiles.size());
          runs[run] = tile;
          for (int t = 0; t < w * h; t++)
          {
            std::array<uint8_t, 64> tileData;
            std::copy(run.begin() + t * 64, run.begin() + (t + 1) * 64, tileData.begin());
            out.tiles.push_back(tileData);
          }
        }
        cf.pieces.push_back({cx * 8 - f.ax, cy * 8 - f.ay, w * 8, h * 8, tile});
      }

    out.maxPieces = std::max(out.maxPieces, int(cf.pieces.size()));
    for (int y = -128; y < 128; y++)
    {
      int cost = 0;
      for (const Piece &p : cf.pieces)
        if (y >= p.dy && y < p.dy + p.h) cost += p.w;
      out.maxLineCost = std::max(out.maxLineCost, cost);
    }
    out.frames.push_back(cf);
  }
  if (out.tiles.size() > 1024)
    report.error("does-not-fit", s.path, std::to_string(out.tiles.size()) + " sprite tiles; sprite memory holds 1024");

  for (const Animation &a : s.animations)
  {
    if (a.steps.empty()) report.warning("animation", s.path, "animation '" + a.name + "' has no steps");
    for (const Step &st : a.steps)
      if (st.frame < 0 || size_t(st.frame) >= s.frames.size())
        report.error("does-not-fit", s.path, "animation '" + a.name + "' uses frame " + std::to_string(st.frame) + ", which does not exist");
  }
  report.info("sprite", s.path,
              std::to_string(s.frames.size()) + " frames in " + std::to_string(out.tiles.size()) + " tiles (" + std::to_string(out.tiles.size() * 32) +
                  " bytes), " + std::to_string(colours.size()) + " colours, at most " + std::to_string(out.maxPieces) + " hardware sprites per frame");
  return out;
}

std::vector<uint8_t> chunk(const Sprite &s, const Cooked &c, uint16_t nameId, const std::vector<uint16_t> &animationNames,
                           const std::vector<uint16_t> &boxNameIds, const std::vector<uint16_t> &paletteNameIds,
                           const std::vector<uint16_t> &pointNameIds)
{
  std::vector<uint8_t> b;
  auto u8 = [&](int v) { b.push_back(uint8_t(v)); };
  auto u16 = [&](uint32_t v) { u8(int(v)); u8(int(v >> 8)); };
  auto u32 = [&](uint32_t v) { u16(v); u16(v >> 16); };

  size_t pieces = 0, steps = 0, boxes = 0;
  for (const CookedFrame &f : c.frames) pieces += f.pieces.size();
  for (const CookedFrame &f : c.frames) boxes += f.boxes.size();
  for (const Animation &a : s.animations) steps += a.steps.size();
  // The tiles, packed (sprite-format.md "Packed tiles") when that is smaller.
  std::vector<uint8_t> tileBytes;
  for (const auto &t : c.tiles)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x += 2) tileBytes.push_back(uint8_t(t[size_t(y * 8 + x)] | (t[size_t(y * 8 + x + 1)] << 4)));
  std::vector<uint8_t> packed = packTiles(tileBytes);
  bool isPacked = packed.size() < tileBytes.size();
  const std::vector<uint8_t> &tileData = isPacked ? packed : tileBytes;

  // Version 3: the header goes on with the boxes' tables (version 2: 52 bytes; a version 1
  // chunk has a 36-byte header, so its frames start at 36) and the number of palettes; the
  // palettes (the default first) are followed by their names.
  // Version 4 (only a sprite with points): the header goes on with the points' tables.
  size_t points = 0;
  for (const CookedFrame &f : c.frames) points += f.points.size();
  const bool withNormals = !c.normals.empty();
  const bool withPoints = points != 0 || withNormals;
  // Version 5 (a sprite with light): the header goes on with the normals' offset.
  const uint32_t header = withNormals ? 72 : withPoints ? 68 : 56;
  const uint32_t palettes = 1 + uint32_t(c.variants.size());
  uint32_t framesAt = header, piecesAt = framesAt + uint32_t(c.frames.size()) * 12, animsAt = piecesAt + uint32_t(pieces) * 6;
  uint32_t stepsAt = animsAt + uint32_t(s.animations.size()) * 8, paletteAt = stepsAt + uint32_t(steps) * 4;
  uint32_t tilesAt = paletteAt + palettes * 32 + palettes * 2;
  uint32_t boxIndexAt = tilesAt + uint32_t((tileData.size() + 1) & ~size_t(1)), boxesAt = boxIndexAt + uint32_t(c.frames.size()) * 2;
  uint32_t namesAt = boxesAt + uint32_t(boxes) * 6;
  uint32_t pointIndexAt = namesAt + uint32_t(c.boxNames.size()) * 2;
  uint32_t pointsAt = pointIndexAt + uint32_t(c.frames.size() + 1) * 2, pointNamesAt = pointsAt + uint32_t(points) * 4;
  uint32_t normalsAt = (pointNamesAt + uint32_t(c.pointNames.size()) * 2 + 3) & ~3u;

  u16(nameId);
  u16(uint32_t(c.frames.size()));
  u16(uint32_t(s.animations.size()));
  u16(uint32_t(c.tiles.size()));
  u16(uint32_t(pieces));
  u16(uint32_t(steps));
  u32(framesAt);
  u32(piecesAt);
  u32(animsAt);
  u32(stepsAt);
  u32(paletteAt);
  u32(tilesAt);
  u32(boxIndexAt);
  u32(boxesAt);
  u32(namesAt);
  u16(uint32_t(boxes));
  u8(int(c.boxNames.size()));
  u8(isPacked ? 1 : 0);   // flags: bit 0 the tiles are packed
  u16(palettes);
  u16(0);
  if (withPoints)
  {
    u32(pointIndexAt);
    u32(pointNamesAt);
    u16(uint32_t(points));
    u8(int(c.pointNames.size()));
    u8(0);
  }
  if (withNormals) u32(normalsAt);

  uint32_t pieceStart = 0;
  for (const CookedFrame &f : c.frames)
  {
    u16(pieceStart);
    u8(int(f.pieces.size()));
    u8(int(f.boxes.size()));
    u8(f.hitbox.x);
    u8(f.hitbox.y);
    u8(f.hitbox.w);
    u8(f.hitbox.h);
    u8(f.bounds.x);
    u8(f.bounds.y);
    u8(f.bounds.w);
    u8(f.bounds.h);
    pieceStart += uint32_t(f.pieces.size());
  }
  for (const CookedFrame &f : c.frames)
    for (const Piece &p : f.pieces)
    {
      int shape = p.w == p.h ? 0 : p.w > p.h ? 1 : 2, size = 0;
      for (int k = 0; k < 12; k++)
        if (kSizes[k][0] * 8 == p.w && kSizes[k][1] * 8 == p.h) size = k % 4;
      u8(p.dx);
      u8(p.dy);
      u8(shape | (size << 2));
      u8(0);
      u16(uint32_t(p.tile));
    }
  uint32_t stepStart = 0;
  for (size_t i = 0; i < s.animations.size(); i++)
  {
    const Animation &a = s.animations[i];
    u16(i < animationNames.size() ? animationNames[i] : 0);
    u16(stepStart);
    u16(uint32_t(a.steps.size()));
    u8((a.loop ? 1 : 0) | (a.directions > 0 ? 2 : 0));
    u8(0);
    stepStart += uint32_t(a.steps.size());
  }
  for (const Animation &a : s.animations)
    for (size_t k = 0; k < a.steps.size(); k++)
    {
      // A direction set's steps hold their angle where others hold their duration.
      u16(uint32_t(a.steps[k].frame));
      u16(a.directions > 0 ? uint32_t(directionAngle(a, k)) : uint32_t(std::max(1, a.steps[k].duration)));
    }
  for (int i = 0; i < 16; i++) u16(c.palette[size_t(i)]);
  for (const auto &v : c.variants)
    for (int i = 0; i < 16; i++) u16(i == 0 ? 0 : v[size_t(i)]);
  for (uint32_t k = 0; k < palettes; k++) u16(k < paletteNameIds.size() ? paletteNameIds[k] : 0xFFFF);
  for (uint8_t v : tileData) u8(v);
  if (tileData.size() % 2) u8(0);
  uint32_t boxStart = 0;
  for (const CookedFrame &f : c.frames)
  {
    u16(boxStart);
    boxStart += uint32_t(f.boxes.size());
  }
  for (const CookedFrame &f : c.frames)
    for (const NamedBox &nb : f.boxes)
    {
      u8(nb.box.x);
      u8(nb.box.y);
      u8(nb.box.w);
      u8(nb.box.h);
      u8(int(nb.kind));
      auto it = std::find(c.boxNames.begin(), c.boxNames.end(), nb.name);
      u8(nb.name.empty() || it == c.boxNames.end() ? 0xFF : int(it - c.boxNames.begin()));
    }
  for (size_t i = 0; i < c.boxNames.size(); i++) u16(i < boxNameIds.size() ? boxNameIds[i] : 0xFFFF);
  if (withPoints)
  {
    // Point index: the first point of each frame and one past the last.
    uint32_t pointStart = 0;
    for (const CookedFrame &f : c.frames)
    {
      u16(pointStart);
      pointStart += uint32_t(f.points.size());
    }
    u16(pointStart);
    for (const CookedFrame &f : c.frames)
      for (const Point &p : f.points)
      {
        u8(p.x);
        u8(p.y);
        u8(p.angle);
        auto it = std::find(c.pointNames.begin(), c.pointNames.end(), p.name);
        u8(int(it - c.pointNames.begin()) | (p.behind ? 0x80 : 0));
      }
    for (size_t i = 0; i < c.pointNames.size(); i++) u16(i < pointNameIds.size() ? pointNameIds[i] : 0xFFFF);
  }
  while (b.size() % 4) u8(0);
  if (withNormals)
    for (const auto &n : c.normals)
      for (int8_t v : n) u8(uint8_t(v));
  return b;
}

}  // namespace bm::sprite
