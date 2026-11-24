// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "figure_asset.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bm::figure {

int layerIndex(const Figure &f, const std::string &name)
{
  for (size_t i = 0; i < f.layers.size(); i++)
    if (f.layers[i].name == name) return int(i);
  return -1;
}

Figure load(const std::string &path)
{
  std::ifstream in(path);
  if (!in) throw std::runtime_error(path + ": cannot read");
  Figure f;
  f.path = path;
  f.name = std::filesystem::path(path).stem().string();
  std::string line;
  for (int n = 1; std::getline(in, line); n++)
  {
    std::string where = path + ":" + std::to_string(n);
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    std::istringstream words(line);
    std::string word;
    if (!(words >> word)) continue;
    if (word == "figure") continue;   // the name comes from the file name
    if (word != "layer") throw std::runtime_error(where + ": expected 'layer NAME sprite=...'");
    Layer l;
    if (!(words >> l.name) || l.name.find('=') != std::string::npos) throw std::runtime_error(where + ": a layer needs a name");
    if (layerIndex(f, l.name) >= 0) throw std::runtime_error(where + ": layer '" + l.name + "' twice");
    bool sprite = false;
    while (words >> word)
    {
      size_t eq = word.find('=');
      std::string key = word.substr(0, eq), value = eq == std::string::npos ? std::string() : word.substr(eq + 1);
      if (key == "behind" && eq == std::string::npos) l.behind = true;
      else if (key == "sprite")
      {
        sprite = true;
        l.sprite = value == "none" ? std::string() : value;
      }
      else if (key == "at")
      {
        size_t dot = value.find('.');
        l.parent = value.substr(0, dot);
        l.point = dot == std::string::npos ? std::string() : value.substr(dot + 1);
        if (layerIndex(f, l.parent) < 0) throw std::runtime_error(where + ": 'at' names a layer above this one, not '" + l.parent + "'");
      }
      else if (key == "mode")
      {
        if (value == "own") l.mode = Mode::Own;
        else if (value == "sync") l.mode = Mode::Sync;
        else if (value == "aim") l.mode = Mode::Aim;
        else throw std::runtime_error(where + ": a mode is own, sync or aim, not '" + value + "'");
      }
      else if (key == "palette") l.palette = value == "default" ? std::string() : value;
      else throw std::runtime_error(where + ": unknown '" + word + "' (sprite=, at=, mode=, palette=, behind)");
    }
    if (!sprite) throw std::runtime_error(where + ": a layer needs sprite=NAME (or sprite=none)");
    if (f.layers.empty() && !l.parent.empty()) throw std::runtime_error(where + ": the first layer is the root and hangs on nothing");
    if (!f.layers.empty() && l.parent.empty()) throw std::runtime_error(where + ": every layer but the first needs at=LAYER.POINT");
    if (l.parent.empty() && l.mode == Mode::Sync) throw std::runtime_error(where + ": the root cannot be in sync with a parent");
    f.layers.push_back(l);
    if (f.layers.size() > MAX_LAYERS) throw std::runtime_error(where + ": a figure has at most 4 layers");
  }
  if (f.layers.empty()) throw std::runtime_error(path + ": a figure needs a layer");
  if (f.layers[0].sprite.empty()) throw std::runtime_error(path + ": the root layer needs a sprite");
  return f;
}

}  // namespace bm::figure
