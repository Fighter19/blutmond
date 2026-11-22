// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "data_file.h"

#include <fstream>
#include <sstream>

namespace bm::editor {

namespace {

std::string trim(const std::string &s)
{
  size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}

// Where a comment starts on a line (a # outside quotes), or npos.
size_t commentAt(const std::string &line)
{
  bool quoted = false;
  for (size_t i = 0; i < line.size(); i++)
  {
    if (line[i] == '\\' && quoted) i++;
    else if (line[i] == '"') quoted = !quoted;
    else if (line[i] == '#' && !quoted) return i;
  }
  return std::string::npos;
}

bool isWord(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace

bool validRecordName(const std::string &name)
{
  if (name.empty() || (name[0] >= '0' && name[0] <= '9')) return false;
  for (char c : name)
    if (!isWord(c)) return false;
  return true;
}

std::string quoteText(const std::string &text)
{
  std::string out = "\"";
  for (char c : text)
  {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') continue;   // a value is one line
    out += c;
  }
  return out + "\"";
}

std::string unquoteText(const std::string &value)
{
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') return value;
  std::string out;
  for (size_t i = 1; i + 1 < value.size(); i++)
  {
    if (value[i] == '\\' && i + 2 < value.size()) i++;
    out += value[i];
  }
  return out;
}

bool DataFile::load(const std::string &p, std::string *pError)
{
  std::ifstream in(p, std::ios::binary);
  if (!in)
  {
    if (pError) *pError = "cannot read " + p;
    return false;
  }
  path = p;
  lines.clear();
  std::string line;
  while (std::getline(in, line))
  {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  parse();
  return true;
}

bool DataFile::save(std::string *pError) const
{
  std::ostringstream out;
  for (const std::string &line : lines) out << line << '\n';
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f || !(f << out.str()))
  {
    if (pError) *pError = "cannot write " + path;
    return false;
  }
  return true;
}

void DataFile::parse()
{
  table.clear();
  records.clear();
  DataRecord *open = nullptr;
  for (size_t i = 0; i < lines.size(); i++)
  {
    std::string line = lines[i];
    size_t c = commentAt(line);
    if (c != std::string::npos) line = line.substr(0, c);
    line = trim(line);
    if (line.empty()) continue;
    if (open == nullptr)
    {
      if (line.rfind("table ", 0) == 0) table = trim(line.substr(6));
      else if (line.rfind("record ", 0) == 0)
      {
        DataRecord r;
        std::string rest = trim(line.substr(7));
        size_t brace = rest.find('{');
        std::string head = trim(rest.substr(0, brace));
        size_t colon = head.find(':');
        r.name = trim(head.substr(0, colon));
        if (colon != std::string::npos) r.base = trim(head.substr(colon + 1));
        r.first = r.last = int(i);
        records.push_back(r);
        // `record X {}` on one line is a whole record.
        if (brace != std::string::npos && rest.find('}', brace) != std::string::npos) continue;
        open = &records.back();
      }
      continue;
    }
    if (line == "}")
    {
      open->last = int(i);
      open = nullptr;
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    open->values.push_back({trim(line.substr(0, eq)), trim(line.substr(eq + 1)), int(i)});
  }
  if (open != nullptr) open->last = int(lines.size()) - 1;
}

const DataRecord *DataFile::find(const std::string &name) const
{
  for (const DataRecord &r : records)
    if (r.name == name) return &r;
  return nullptr;
}

std::string DataFile::own(const std::string &record, const std::string &key) const
{
  const DataRecord *r = find(record);
  if (r == nullptr) return "";
  for (const DataValue &v : r->values)
    if (v.key == key) return v.value;
  return "";
}

std::string DataFile::value(const std::string &record, const std::string &key) const
{
  std::string name = record;
  for (int depth = 0; depth < 16 && !name.empty(); depth++)
  {
    const DataRecord *r = find(name);
    if (r == nullptr) return "";
    for (const DataValue &v : r->values)
      if (v.key == key) return v.value;
    name = r->base;
  }
  return "";
}

void DataFile::set(const std::string &record, const std::string &key, const std::string &value)
{
  const DataRecord *r = find(record);
  if (r == nullptr) return;
  for (const DataValue &v : r->values)
  {
    if (v.key != key) continue;
    std::string &line = lines[size_t(v.line)];
    if (value.empty())
    {
      lines.erase(lines.begin() + v.line);
    }
    else
    {
      // The same indent and the comment after it stay.
      size_t indent = line.find_first_not_of(" \t");
      size_t c = commentAt(line);
      std::string comment = c != std::string::npos ? "   " + line.substr(c) : "";
      line = line.substr(0, indent == std::string::npos ? 0 : indent) + key + " = " + value + comment;
    }
    parse();
    return;
  }
  if (value.empty()) return;
  if (r->last == r->first)
  {
    // `record X {}` on one line: open it.
    std::string head = lines[size_t(r->first)];
    size_t brace = head.find('{');
    lines[size_t(r->first)] = head.substr(0, brace + 1);
    lines.insert(lines.begin() + r->first + 1, "}");
    parse();
    r = find(record);
  }
  lines.insert(lines.begin() + r->last, "  " + key + " = " + value);
  parse();
}

bool DataFile::add(const std::string &name, const std::string &base, std::string *pError)
{
  if (!validRecordName(name))
  {
    if (pError) *pError = "a record's name is made of letters, digits and _";
    return false;
  }
  if (find(name) != nullptr)
  {
    if (pError) *pError = "there is a record " + name + " already";
    return false;
  }
  if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
  lines.push_back("record " + name + (base.empty() ? "" : " : " + base) + " {");
  lines.push_back("}");
  parse();
  return true;
}

bool DataFile::remove(const std::string &name)
{
  const DataRecord *r = find(name);
  if (r == nullptr) return false;
  int first = r->first, last = r->last;
  // The empty line after it goes too, so that the gaps stay as they were.
  if (last + 1 < int(lines.size()) && trim(lines[size_t(last + 1)]).empty()) last++;
  else if (first > 0 && trim(lines[size_t(first - 1)]).empty()) first--;
  lines.erase(lines.begin() + first, lines.begin() + last + 1);
  parse();
  return true;
}

}  // namespace bm::editor
