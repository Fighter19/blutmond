// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Mondskript compiler (docs/spec/mondskript.md): source files, one class each, into a
// bytecode module (MODB chunk) for the interpreter in src/script/vm.h. Used by the cooker and
// by the editor, which shows the diagnostics and the class outline.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

extern "C" {
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
  BmScriptType type = BM_SCRIPT_INT;
  uint8_t flags = 0;            // BM_SCRIPT_FIELD_*
  int32_t defaultValue = 0;
  int line = 0;
};

struct ClassInfo
{
  std::string name, base, file, sprite, startState;
  std::vector<FieldInfo> fields;
  std::vector<std::string> states, events, functions;
  int codeWords = 0;
};

struct Source
{
  std::string path, text;
};

struct Module
{
  std::vector<uint8_t> data;    // empty when there are errors
  std::vector<ClassInfo> classes;
  std::vector<Diagnostic> diagnostics;
  bool ok() const;
};

/// Compiles the sources into one module. `stringId` puts a string into the pack's string table
/// and returns its id (names, texts, class and field names).
Module compile(const std::vector<Source> &sources, const std::function<uint16_t(const std::string &)> &stringId);

/// Reads a file into a Source. Throws std::runtime_error if it cannot be read.
Source readSource(const std::string &path);

const char *typeName(BmScriptType type);

/// The words of the language, for syntax colouring.
bool isKeyword(const std::string &word);
bool isTypeName(const std::string &word);
bool isBuiltin(const std::string &word);

}  // namespace bm::script
