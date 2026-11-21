// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Export: one GBA image to play in an emulator or write to a flash cartridge. The runtime's
// cartridge image (build-gba/game.gba) gets the cooked pack appended, its pack slot
// (src/pack/rom_slot.h) pointed at it, and its header checksum filled in again.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bm::editor {

/// Builds the exported image from the runtime image and a cooked pack. False with a reason
/// if the runtime has no pack slot or the result would not fit into 32 MB.
bool buildExportRom(const std::vector<uint8_t> &runtime, const std::vector<uint8_t> &pack, std::vector<uint8_t> &out,
                    std::string *pError);

/// The same with files: reads the runtime and the pack, writes the image.
bool exportRomFiles(const std::string &runtimePath, const std::string &packPath, const std::string &outPath, std::string *pError);

/// The GBA header's complement check over 0xA0..0xBC, as the BIOS checks it.
uint8_t gbaHeaderComplement(const uint8_t *pHeader);

}  // namespace bm::editor
