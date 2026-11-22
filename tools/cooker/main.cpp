// SPDX-License-Identifier: BSD-2-Clause OR MIT
// bmcook: cooks Tiled rooms into a pack for one platform profile.
//
//   bmcook [--profile gba|snes|host] [--report report.txt] [--color-matrix m0,...,m8] -o out.bmpk
//          room.tmx... [sprite.bmsprite...] [script.ms...] [song.bmsong...] [records.bmdata...] [font.bmfont...]
//
// Exit status 1 when the report contains errors (the pack is then not written).

#include "cook.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv)
{
  std::string profileName = "gba", out, reportPath, nativeCompiler, nativeInclude, nativeElf, scriptC;
  std::vector<std::string> inputs;
  std::optional<std::array<float, 9>> matrix;

  for (int i = 1; i < argc; i++)
  {
    std::string a = argv[i];
    if ((a == "--profile" || a == "-p") && i + 1 < argc) profileName = argv[++i];
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
    else if (a == "--report" && i + 1 < argc) reportPath = argv[++i];
    else if (a == "--native" && i + 1 < argc) nativeCompiler = argv[++i];
    else if (a == "--native-include" && i + 1 < argc) nativeInclude = argv[++i];
    else if (a == "--native-elf" && i + 1 < argc) nativeElf = argv[++i];
    else if (a == "--script-c" && i + 1 < argc) scriptC = argv[++i];
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
    std::cerr << "usage: bmcook [--profile gba|snes|host] [--report report.txt] [--color-matrix m0,...,m8]\n"
                 "              [--native arm-none-eabi-gcc] [--native-elf out.elf] [--native-include dir] [--script-c out.c] -o out.bmpk room.tmx...\n";
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
    bm::CookOptions options;
    for (const std::string &path : inputs)
    {
      if (path.size() > 9 && path.substr(path.size() - 9) == ".bmsprite") options.sprites.push_back(path);
      else if (path.size() > 3 && path.substr(path.size() - 3) == ".ms") options.scripts.push_back(path);
      else if (path.size() > 7 && path.substr(path.size() - 7) == ".bmdata") options.data.push_back(path);
      else if (path.size() > 7 && path.substr(path.size() - 7) == ".bmsong") options.songs.push_back(path);
      else if (path.size() > 7 && path.substr(path.size() - 7) == ".bmfont") options.fonts.push_back(path);
      else maps.push_back(bm::tmx::load(path));
    }
    options.profile = *profile;
    options.colorMatrix = matrix;
    options.nativeCompiler = nativeCompiler;
    options.nativeIncludeDir = nativeInclude;
    options.nativeElf = nativeElf;
    options.scriptC = scriptC;
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
