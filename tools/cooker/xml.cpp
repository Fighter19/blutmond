// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "xml.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bm::xml {

const Element *Element::child(const std::string &childName) const
{
  for (const auto &c : children)
    if (c->name == childName) return c.get();
  return nullptr;
}

std::vector<const Element *> Element::childrenNamed(const std::string &childName) const
{
  std::vector<const Element *> out;
  for (const auto &c : children)
    if (c->name == childName) out.push_back(c.get());
  return out;
}

std::string Element::attr(const std::string &key, const std::string &fallback) const
{
  auto it = attributes.find(key);
  return it == attributes.end() ? fallback : it->second;
}

namespace {

struct Parser
{
  const std::string &s;
  size_t pos = 0;

  [[noreturn]] void fail(const std::string &what) const
  {
    int line = 1;
    for (size_t i = 0; i < pos && i < s.size(); i++)
      if (s[i] == '\n') line++;
    throw std::runtime_error("XML line " + std::to_string(line) + ": " + what);
  }

  bool startsWith(const char *t) const { return s.compare(pos, std::char_traits<char>::length(t), t) == 0; }

  void skipSpace()
  {
    while (pos < s.size() && isspace(static_cast<unsigned char>(s[pos]))) pos++;
  }

  void skipUntil(const char *t)
  {
    size_t e = s.find(t, pos);
    if (e == std::string::npos) fail(std::string("missing ") + t);
    pos = e + std::char_traits<char>::length(t);
  }

  // Skips declarations, comments and processing instructions.
  void skipMisc()
  {
    for (;;)
    {
      skipSpace();
      if (startsWith("<?")) skipUntil("?>");
      else if (startsWith("<!--")) skipUntil("-->");
      else if (startsWith("<!")) skipUntil(">");
      else return;
    }
  }

  std::string name()
  {
    size_t b = pos;
    while (pos < s.size() && (isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_' || s[pos] == '-' ||
                              s[pos] == '.' || s[pos] == ':'))
      pos++;
    if (b == pos) fail("expected a name");
    return s.substr(b, pos - b);
  }

  static void appendUtf8(std::string &out, unsigned long cp)
  {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 63)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63)); }
  }

  std::string decode(const std::string &raw)
  {
    std::string out;
    for (size_t i = 0; i < raw.size(); i++)
    {
      if (raw[i] != '&') { out += raw[i]; continue; }
      size_t e = raw.find(';', i);
      if (e == std::string::npos) fail("unterminated entity");
      std::string ent = raw.substr(i + 1, e - i - 1);
      if (ent == "amp") out += '&';
      else if (ent == "lt") out += '<';
      else if (ent == "gt") out += '>';
      else if (ent == "quot") out += '"';
      else if (ent == "apos") out += '\'';
      else if (!ent.empty() && ent[0] == '#')
        appendUtf8(out, ent.size() > 1 && ent[1] == 'x' ? std::stoul(ent.substr(2), nullptr, 16) : std::stoul(ent.substr(1)));
      else fail("unknown entity &" + ent + ";");
      i = e;
    }
    return out;
  }

  std::unique_ptr<Element> element()
  {
    if (pos >= s.size() || s[pos] != '<') fail("expected '<'");
    pos++;
    auto el = std::make_unique<Element>();
    el->name = name();
    for (;;)
    {
      skipSpace();
      if (startsWith("/>")) { pos += 2; return el; }
      if (startsWith(">")) { pos++; break; }
      std::string key = name();
      skipSpace();
      if (pos >= s.size() || s[pos] != '=') fail("expected '=' after " + key);
      pos++;
      skipSpace();
      char quote = pos < s.size() ? s[pos] : 0;
      if (quote != '"' && quote != '\'') fail("expected a quoted value for " + key);
      size_t e = s.find(quote, pos + 1);
      if (e == std::string::npos) fail("unterminated value for " + key);
      el->attributes[key] = decode(s.substr(pos + 1, e - pos - 1));
      pos = e + 1;
    }
    // Content
    for (;;)
    {
      if (pos >= s.size()) fail("missing </" + el->name + ">");
      if (startsWith("</"))
      {
        pos += 2;
        std::string end = name();
        if (end != el->name) fail("expected </" + el->name + "> but found </" + end + ">");
        skipSpace();
        if (pos >= s.size() || s[pos] != '>') fail("expected '>'");
        pos++;
        return el;
      }
      if (startsWith("<!--")) { skipUntil("-->"); continue; }
      if (startsWith("<![CDATA["))
      {
        size_t e = s.find("]]>", pos);
        if (e == std::string::npos) fail("unterminated CDATA");
        el->text += s.substr(pos + 9, e - pos - 9);
        pos = e + 3;
        continue;
      }
      if (s[pos] == '<') { el->children.push_back(element()); continue; }
      size_t e = s.find('<', pos);
      if (e == std::string::npos) e = s.size();
      el->text += decode(s.substr(pos, e - pos));
      pos = e;
    }
  }
};

}  // namespace

std::unique_ptr<Element> parse(const std::string &text)
{
  Parser p{text};
  p.skipMisc();
  auto root = p.element();
  p.skipMisc();
  if (p.pos != text.size()) p.fail("content after the root element");
  return root;
}

std::unique_ptr<Element> parseFile(const std::string &path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  try
  {
    return parse(ss.str());
  }
  catch (const std::exception &e)
  {
    throw std::runtime_error(path + ": " + e.what());
  }
}

}  // namespace bm::xml
