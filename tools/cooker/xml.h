// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Minimal XML reader for Tiled files: elements, attributes, text, the five predefined
// entities and numeric character references. No DTDs, no namespaces.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bm::xml {

struct Element
{
  std::string name;
  std::map<std::string, std::string> attributes;
  std::vector<std::unique_ptr<Element>> children;
  std::string text;

  const Element *child(const std::string &childName) const;
  std::vector<const Element *> childrenNamed(const std::string &childName) const;
  std::string attr(const std::string &key, const std::string &fallback = "") const;
  bool has(const std::string &key) const { return attributes.count(key) != 0; }
};

/// Parses a document; throws std::runtime_error with a line number on errors.
std::unique_ptr<Element> parse(const std::string &text);
std::unique_ptr<Element> parseFile(const std::string &path);

}  // namespace bm::xml
