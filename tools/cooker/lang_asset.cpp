// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "lang_asset.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bm::lang {

// A quoted string from `at`; moves `at` past it.
static std::string quoted(const std::string &line, size_t &at, const std::string &where)
{
  while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) at++;
  if (at >= line.size() || line[at] != '"') throw std::runtime_error(where + ": a text in \"quotes\" is expected");
  std::string out;
  for (at++; at < line.size() && line[at] != '"'; at++)
  {
    if (line[at] == '\\' && at + 1 < line.size())
    {
      char c = line[++at];
      out += c == 'n' ? '\n' : c;
    }
    else out += line[at];
  }
  if (at >= line.size()) throw std::runtime_error(where + ": the text is not closed with \"");
  at++;
  return out;
}

Language load(const std::string &path)
{
  std::ifstream in(path);
  if (!in) throw std::runtime_error(path + ": cannot be read");
  Language l;
  l.path = path;
  std::string line;
  for (int n = 1; std::getline(in, line); n++)
  {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string where = path + ":" + std::to_string(n);
    size_t at = 0;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) at++;
    if (at >= line.size() || line[at] == '#') continue;
    if (line.compare(at, 9, "language ") == 0)
    {
      std::istringstream words(line.substr(at + 9));
      words >> l.code;
      size_t q = line.find('"', at);
      if (l.code.empty() || q == std::string::npos) throw std::runtime_error(where + ": language <code> \"<name>\"");
      l.name = quoted(line, q, where);
      continue;
    }
    if (l.code.empty()) throw std::runtime_error(where + ": the file starts with: language <code> \"<name>\"");
    std::string original = quoted(line, at, where);
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) at++;
    if (at >= line.size() || line[at] != '=') throw std::runtime_error(where + ": \"original\" = \"translation\"");
    at++;
    std::string translation = quoted(line, at, where);
    l.texts.push_back({original, translation});
    l.lines.push_back(n);
    l.files.push_back(path);
  }
  if (l.code.empty()) throw std::runtime_error(path + ": no language line");
  return l;
}

}  // namespace bm::lang
