// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The C back end (docs/spec/mondskript.md §12): turns a compiled bytecode module into C that
// does exactly what the interpreter does with it, to be compiled for the target as a native
// module (src/script/native.h). `#line` directives point back at the script files.
#pragma once

#include <cstdint>
#include <string>

#include "mondskript.h"

namespace bm::script {

/// C source for a module. `bytecodeCrc` is the CRC-32 of module.data, which ties the native
/// module to its bytecode. Empty if the module has errors.
std::string emitC(const Module &module, uint32_t bytecodeCrc);

}  // namespace bm::script
