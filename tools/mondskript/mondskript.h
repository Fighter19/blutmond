// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Mondskript compiler (docs/spec/mondskript.md): source files into a bytecode module (MODB
// chunk) for the interpreter in src/script/vm.h. A file holds enums and structures
// (docs/plans/structures.md), then at most one class. Used by the cooker and by the editor,
// which shows the diagnostics and the class outline.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include "data_types.h"
#include "script_api.h"
}

namespace bm::script {

struct Diagnostic
{
  std::string file;
  int line = 0, column = 0;
  std::string message;
  bool warning = false;
};

struct FieldInfo
{
  std::string name, category;   // category: var(Category), shown in the editor
  std::string typeLabel;        // as written: "int", "Element"
  BmScriptType type = BM_SCRIPT_INT;
  uint8_t flags = 0;            // BM_SCRIPT_FIELD_*
  int32_t defaultValue = 0;
  int line = 0;
};

struct ClassInfo
{
  std::string name, base, file, sprite, startState;
  BmScriptKind kind = BM_SCRIPT_KIND_ACTOR;
  std::vector<FieldInfo> fields;
  std::vector<std::string> states, events, functions;
  int codeWords = 0;
};

struct EnumInfo
{
  std::string name, file;
  std::vector<std::string> values;   // value i is the i-th name
  int line = 0;
};

/// A field of a structure as laid out in a record (docs/plans/structures.md §2).
struct StructField
{
  std::string name;
  BmDataType type = BM_DATA_INT;
  std::string detail;      // enum, structure or record<S> target; empty otherwise
  std::string typeLabel;   // as written: "int", "Element", "record<Weapon>", "int[3]"
  uint8_t count = 1;       // elements; more than 1 for a fixed array
  uint16_t offset = 0;     // bytes from the start of the record
  uint16_t elementSize = 0;
  bool inherited = false;  // comes from the base structure
  int line = 0;
};

struct StructInfo
{
  std::string name, base, file;
  std::vector<StructField> fields;
  uint16_t size = 0;                // bytes, a multiple of 4
  uint16_t align = 1;
  std::vector<uint8_t> defaults;    // a record with every field at its default (little endian)
  int line = 0;
  // Roles (BM_DATA_ROLE_*, docs/spec/data-format.md §6), resolved by the compiler.
  uint8_t roles = 0;
  uint8_t equipSlot = BM_DATA_SLOT_FROM_FIELD;   // the slot (enum Slot), or from the field below
  uint16_t equipOffset = 0;                      // the field of type Slot when it is per record
  struct Stat
  {
    uint16_t offset = 0;
    BmDataType type = BM_DATA_INT;
  };
  std::vector<Stat> stats;                       // stat i: where its value is in a record
};

struct Source
{
  std::string path, text;
};

struct ClassModule
{
  std::string name;             // the class
  std::vector<uint8_t> data;    // a module with only this class (one MODB chunk in a pack)
};

struct Module
{
  std::vector<uint8_t> data;    // every class in one module; empty when there are errors
  std::vector<ClassModule> classModules;   // the same classes, a module each
  std::vector<ClassInfo> classes;
  std::vector<EnumInfo> enums;
  std::vector<StructInfo> structs;
  std::vector<Diagnostic> diagnostics;
  bool ok() const;
};

/// Compiles the sources into one module. `stringId` puts a string into the pack's string table
/// and returns its id (names, texts, class and field names).
Module compile(const std::vector<Source> &sources, const std::function<uint16_t(const std::string &)> &stringId);

/// Reads a file into a Source. Throws std::runtime_error if it cannot be read.
Source readSource(const std::string &path);

const char *typeName(BmScriptType type);
const char *dataTypeName(BmDataType type);

/// FNV-1a (32 bits) of a name: how records and items are identified in saved state.
uint32_t nameKey(const std::string &name);

/// What scripts can call and read, one line each ("sleep(int)  takes time"), for the editor.
std::vector<std::string> apiReference();

/// The words of the language, for syntax colouring.
bool isKeyword(const std::string &word);
bool isTypeName(const std::string &word);
bool isBuiltin(const std::string &word);

}  // namespace bm::script
