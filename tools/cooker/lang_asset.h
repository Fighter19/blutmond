// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Translations (docs/spec/pack-format.md §3.2): one file per language, UTF-8.
//
//   # de.bmlang
//   language de "Deutsch"
//   "Short Sword" = "Kurzschwert"
//   "A plain blade." = "Eine schlichte Klinge."
//
// Each line after the first gives a text of the main language (exactly as in the scripts and
// records) and its translation; \" \\ and \n inside quotes. Comments start with #.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace bm::lang {

struct Language
{
  std::string path, code, name;
  std::vector<std::pair<std::string, std::string>> texts;   // original, translation; with line numbers below
  std::vector<int> lines;
  std::vector<std::string> files;   // per text, the file it is in (a language may come in several)
};

/// Reads a language file. Throws with file and line on errors.
Language load(const std::string &path);

}  // namespace bm::lang
