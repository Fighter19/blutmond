// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "rom_export.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

extern "C" {
#include "rom_slot.h"
}

namespace bm::editor {

namespace {

constexpr size_t kMaxRom = 32u * 1024 * 1024;
constexpr size_t kHeaderSize = 0xC0;
constexpr size_t kComplementOffset = 0xBD;

bool readFile(const std::string &path, std::vector<uint8_t> &data)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

void put32(uint8_t *p, uint32_t v)
{
  for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}

}  // namespace

uint8_t gbaHeaderComplement(const uint8_t *pHeader)
{
  int value = 0;
  for (size_t i = 0xA0; i < kComplementOffset; i++) value -= pHeader[i];
  return uint8_t((value - 0x19) & 0xFF);
}

bool buildExportRom(const std::vector<uint8_t> &runtime, const std::vector<uint8_t> &pack, std::vector<uint8_t> &out,
                    std::string *pError)
{
  auto fail = [&](const std::string &why) {
    if (pError) *pError = why;
    return false;
  };
  if (runtime.size() < kHeaderSize) return fail("the runtime image is too small for a GBA header");
  if (pack.empty()) return fail("the pack is empty");

  // The linker puts the game's slot right after the header (src/hal/gba/gba_cart.ld), so it is
  // the first one: images embedded further on (the multiboot runtime sent to a second GBA)
  // have their own.
  size_t slot = SIZE_MAX;
  for (size_t i = kHeaderSize; i + sizeof(BmRomPackSlot) <= runtime.size() && slot == SIZE_MAX; i += 4)
    if (std::memcmp(&runtime[i], BM_ROM_SLOT_MAGIC, BM_ROM_SLOT_MAGIC_SIZE) == 0) slot = i;
  if (slot == SIZE_MAX) return fail("the runtime image has no pack slot: use the cartridge image game.gba of a current GBA build");

  size_t offset = (runtime.size() + BM_ROM_SLOT_ALIGN - 1) / BM_ROM_SLOT_ALIGN * BM_ROM_SLOT_ALIGN;
  size_t total = offset + pack.size();
  total = (total + 255) / 256 * 256;
  if (total > kMaxRom) return fail("the game does not fit into 32 MB (" + std::to_string(total) + " bytes)");

  out.assign(total, 0);
  std::copy(runtime.begin(), runtime.end(), out.begin());
  std::copy(pack.begin(), pack.end(), out.begin() + std::ptrdiff_t(offset));
  put32(&out[slot + offsetof(BmRomPackSlot, offset)], uint32_t(offset));
  put32(&out[slot + offsetof(BmRomPackSlot, size)], uint32_t(pack.size()));
  out[kComplementOffset] = gbaHeaderComplement(out.data());
  return true;
}

bool exportRomFiles(const std::string &runtimePath, const std::string &packPath, const std::string &outPath, std::string *pError)
{
  std::vector<uint8_t> runtime, pack, rom;
  if (!readFile(runtimePath, runtime))
  {
    if (pError) *pError = "cannot read the runtime image " + runtimePath;
    return false;
  }
  if (!readFile(packPath, pack))
  {
    if (pError) *pError = "cannot read the pack " + packPath;
    return false;
  }
  if (!buildExportRom(runtime, pack, rom, pError)) return false;
  std::ofstream f(outPath, std::ios::binary);
  if (!f.write(reinterpret_cast<const char *>(rom.data()), std::streamsize(rom.size())))
  {
    if (pError) *pError = "cannot write " + outPath;
    return false;
  }
  return true;
}

}  // namespace bm::editor
