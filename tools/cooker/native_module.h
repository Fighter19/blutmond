// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Native script modules (docs/spec/mondskript.md §12): compiles the C the Mondskript compiler
// wrote with the target's GCC, links it at address 0 and packs the result with the list of
// places that hold addresses, for the loader in src/script/native.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bm {

struct NativeBuild
{
  std::string compiler;     // e.g. arm-none-eabi-gcc
  std::string includeDir;   // where native.h is (src/script); empty: the one of this build
  std::string keepElf;      // if set: the linked module with debug information is copied here
};

/// The content of a MODN chunk for a linked ARM module (an ELF file made with --emit-relocs
/// at address 0). Throws std::runtime_error with the reason if it cannot be a module: global
/// variables, an address that cannot be relocated, not ARM.
std::vector<uint8_t> packNativeElf(const std::vector<uint8_t> &elf);

/// Compiles and packs. Throws std::runtime_error with the compiler's messages on failure.
std::vector<uint8_t> buildNativeModule(const std::string &cSource, const NativeBuild &build);

}  // namespace bm
