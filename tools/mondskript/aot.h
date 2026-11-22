// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The C back end (docs/spec/mondskript.md §12): turns a compiled bytecode module into C that
// does exactly what the interpreter does with it, to be compiled for the target as a native
// module (src/script/native.h). `#line` directives point back at the script files.
#pragma once

#include <cstdint>
#include <string>

#include "mondskript.h"

namespace bm::script {

/// A bytecode module to translate: the compiled module (for class names) and the module's
/// bytes (module.data, or one of module.classModules), with their CRC-32, which ties the
/// native code to them.
struct NativeSource
{
  const Module *module;
  const std::vector<uint8_t> *data;   // nullptr: module->data
  uint32_t bytecodeCrc;
  size_t firstClass = 0;              // module->classes index of the bytes' first class
};

/// C source for a native image (native.h) with the code of each module. Empty if a module
/// has errors.
std::string emitC(const std::vector<NativeSource> &sources);

/// The same for one module (module.data).
std::string emitC(const Module &module, uint32_t bytecodeCrc);

}  // namespace bm::script
