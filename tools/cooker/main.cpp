// SPDX-License-Identifier: BSD-2-Clause OR MIT
// bmcook: cooks Tiled rooms into a pack for one platform profile.
//
//   bmcook [--profile gba|snes|host] [--report report.txt] [--color-matrix m0,...,m8] -o out.bmpk room.tmx...
//
// Exit status 1 when the report contains errors (the pack is then not written).

#include "cook.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv)
{
  std::string profileName = "gba", out, reportPath;
  std::vector<std::string> inputs;
  std::optional<std::array<float, 9>> matrix;

  for (int i = 1; i < argc; i++)
  {
    std::string a = argv[i];
    if ((a == "--profile" || a == "-p") && i + 1 < argc) profileName = argv[++i];
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
    else if (a == "--report" && i + 1 < argc) reportPath = argv[++i];
    else if (a == "--color-matrix" && i + 1 < argc)
    {
      std::array<float, 9> m{};
      std::stringstream ss(argv[++i]);
      std::string v;
      for (int k = 0; k < 9 && std::getline(ss, v, ','); k++) m[size_t(k)] = std::stof(v);
      matrix = m;
    }
    else if (!a.empty() && a[0] == '-')
    {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    }
    else inputs.push_back(a);
  }
  if (out.empty() || inputs.empty())
  {
    std::cerr << "usage: bmcook [--profile gba|snes|host] [--report report.txt] [--color-matrix m0,...,m8] -o out.bmpk room.tmx...\n";
    return 2;
  }

  auto profile = bm::profileByName(profileName);
  if (!profile)
  {
    std::cerr << "unknown profile " << profileName << "\n";
    return 2;
  }

  bm::Report report;
  std::vector<uint8_t> pack;
  try
  {
    std::vector<bm::tmx::Map> maps;
    for (const std::string &path : inputs) maps.push_back(bm::tmx::load(path));
    bm::CookOptions options;
    options.profile = *profile;
    options.colorMatrix = matrix;
    pack = bm::cook(maps, options, report);
  }
  catch (const std::exception &e)
  {
    std::cerr << "bmcook: " << e.what() << "\n";
    return 1;
  }

  std::string text = report.text();
  std::cout << text;
  if (!reportPath.empty())
  {
    std::ofstream(reportPath) << text;
  }
  if (report.hasErrors())
  {
    std::cerr << "bmcook: errors, no pack written\n";
    return 1;
  }
  std::ofstream f(out, std::ios::binary);
  f.write(reinterpret_cast<const char *>(pack.data()), std::streamsize(pack.size()));
  std::cout << "wrote " << out << " (" << pack.size() << " bytes)\n";
  return 0;
}
