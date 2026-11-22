// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Records (docs/plans/structures.md §3): .bmdata files, one table of a structure each, cooked
// into DTAB chunks (src/data/data.h) against the structures the scripts declare.
//
//   # weapons.bmdata
//   table Weapon
//
//   record ShortSword {
//     title = "Short Sword"
//     attack = 3
//   }
//
//   record FlameBrand : ShortSword {   # starts as a copy of ShortSword
//     attack = 5
//     element = Fire
//     bonus.strength = 2               # a field of an inner structure
//     combo[1] = 4                     # one element of an array (combo = 4 sets all)
//   }
#pragma once

#include "cook.h"
#include "mondskript.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bm {

struct DataTable
{
  std::string structure;
  std::vector<std::string> records;   // names, in the order of their keys
  std::vector<uint8_t> chunk;         // DTAB
};

/// Cooks the record files against the module's structures. Every structure of the module gets
/// a table (one without records still has its defaults). Problems go into the report as
/// errors of kind "data"; the tables are then incomplete.
std::vector<DataTable> cookData(const std::vector<std::string> &paths, const script::Module &module,
                                const std::function<uint16_t(const std::string &)> &stringId, Report &report);

/// CRC-32 over the names, types, counts and offsets of a structure's fields.
uint32_t layoutHash(const script::StructInfo &s);

}  // namespace bm
