/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "credits.h"

namespace bm::editor {

// Kept in step with docs/PLAN.md §21 (sources) and the libraries the build pulls in.
const char *credits()
{
  return "BUILT WITH\n"
         "ose-ui: the editor's user interface\n"
         "SDL 3: window, input and sound of the editor and the PC game\n"
         "stb_image (Sean Barrett): reading images\n"
         "DejaVu fonts and Font Awesome 4.7: the editor's text and icons\n"
         "doctest: tests of ose-ui\n"
         "Emscripten: the web build\n"
         "GNU Arm Embedded toolchain: the GBA images\n"
         "Tiled (Thorbjørn Lindeijer and contributors): rooms are edited in it\n"
         "mGBA (endrift and contributors): the emulator every GBA test runs in\n"
         "\n"
         "HARDWARE\n"
         "GBATEK (Martin Korth): memory, video, sound, serial port, DMA and BIOS of the GBA\n"
         "gbadev.net documentation: flash cartridges (\"Bootleg Carts\")\n"
         "\n"
         "LINK CABLE\n"
         "xcomms by David Murphy (WinterMute), and its branch parport (Fighter19/xcomms): the Xboo "
         "cable on the parallel port, multiboot and reset\n"
         "gba-remote-play (afska), gba-multiboot-pico (loopj), gba-link-connection (rodri042) and "
         "the pinout notes by AlexApps99: link projects with a Raspberry Pi and an RP2040\n"
         "\n"
         "SOUND\n"
         "Compared with the GBA's sound drivers: GAX (Shin'en; notes and gaxtapper by loveemu), "
         "Krawall (sebknzl), MusyX, the standard driver m4a (summary by loveemu) and ipatix's "
         "mixer, Maxmod; Battle of the Bits and VGMPF for their history\n"
         "\n"
         "COLLISION\n"
         "DSVEdit (LagoLunatic): how the GBA and DS Castlevania games store collision\n"
         "Sonic Physics Guide (Sonic Retro): height arrays and angles per tile\n"
         "Data Crystal: Super Metroid\n"
         "\n"
         "EARLIER WORK\n"
         "SceneCompiler and SceneLoaderGBA, the author's earlier projects: ideas\n"
         "\n"
         "Blutmond is licensed under BSD-2-Clause or MIT. Every name above belongs to its owners; "
         "the projects listed are not affiliated with Blutmond.";
}

}  // namespace bm::editor
