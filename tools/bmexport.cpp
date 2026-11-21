// SPDX-License-Identifier: BSD-2-Clause OR MIT
// bmexport: the editor's Export on the command line. Appends a cooked pack to the game's
// cartridge image, so that the result plays on its own in an emulator or from a cartridge.
//
//   bmexport <runtime game.gba> <pack.bmpk> <out.gba>

#include <cstdio>
#include <string>

#include "rom_export.h"

int main(int argc, char **argv)
{
  if (argc != 4)
  {
    std::fprintf(stderr, "usage: bmexport <runtime game.gba> <pack.bmpk> <out.gba>\n");
    return 2;
  }
  std::string error;
  if (!bm::editor::exportRomFiles(argv[1], argv[2], argv[3], &error))
  {
    std::fprintf(stderr, "bmexport: %s\n", error.c_str());
    return 1;
  }
  std::printf("wrote %s\n", argv[3]);
  return 0;
}
