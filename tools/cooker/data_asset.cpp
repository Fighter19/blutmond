// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "data_asset.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <map>
#include <sstream>

extern "C" {
#include "data.h"
#include "pack.h"
}

namespace bm {

namespace {

struct DataError
{
  int line;
  std::string message;
};

enum class T { End, Ident, Int, Fix, Name, String, Punct };

struct Tok
{
  T kind = T::End;
  std::string text;
  int64_t value = 0;   // Int: the number; Fix: 24.8
  int line = 1;
};

bool identChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// '#' followed by a letter is a name (#Slash); '#' followed by anything else, and '//', start a
// comment that runs to the end of the line.
std::vector<Tok> lex(const std::string &src)
{
  std::vector<Tok> out;
  int line = 1;
  size_t i = 0;
  while (i < src.size())
  {
    char c = src[i];
    if (c == '\n') { line++; i++; continue; }
    if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
    bool nameStart = c == '#' && i + 1 < src.size() && ((src[i + 1] >= 'a' && src[i + 1] <= 'z') || (src[i + 1] >= 'A' && src[i + 1] <= 'Z') || src[i + 1] == '_');
    if ((c == '#' && !nameStart) || (c == '/' && i + 1 < src.size() && src[i + 1] == '/'))
    {
      while (i < src.size() && src[i] != '\n') i++;
      continue;
    }
    Tok t;
    t.line = line;
    if (nameStart || (identChar(c) && !(c >= '0' && c <= '9')))
    {
      size_t j = nameStart ? i + 1 : i;
      size_t start = j;
      while (j < src.size() && identChar(src[j])) j++;
      t.kind = nameStart ? T::Name : T::Ident;
      t.text = src.substr(start, j - start);
      i = j;
    }
    else if (c >= '0' && c <= '9')
    {
      size_t j = i;
      int64_t v = 0;
      if (c == '0' && j + 1 < src.size() && src[j + 1] == 'x')
      {
        j += 2;
        size_t digits = j;
        for (; j < src.size() && identChar(src[j]); j++)
        {
          char ch = src[j];
          int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
          if (d < 0) throw DataError{line, "bad digit in number"};
          v = v * 16 + d;
          if (v > 0xFFFFFFFFll) throw DataError{line, "number too large"};
        }
        if (j == digits) throw DataError{line, "number without digits"};
        t.kind = T::Int;
      }
      else
      {
        for (; j < src.size() && src[j] >= '0' && src[j] <= '9'; j++)
        {
          v = v * 10 + (src[j] - '0');
          if (v > 0xFFFFFFFFll) throw DataError{line, "number too large"};
        }
        t.kind = T::Int;
        if (j + 1 < src.size() && src[j] == '.' && src[j + 1] >= '0' && src[j + 1] <= '9')
        {
          j++;
          double frac = 0, scale = 0.1;
          for (; j < src.size() && src[j] >= '0' && src[j] <= '9'; j++, scale *= 0.1) frac += (src[j] - '0') * scale;
          if (v > 0x7FFFFF) throw DataError{line, "fixed-point number too large"};
          v = v * 256 + int64_t(frac * 256.0 + 0.5);
          t.kind = T::Fix;
        }
      }
      t.value = v;
      t.text = src.substr(i, j - i);
      i = j;
    }
    else if (c == '"')
    {
      size_t j = i + 1;
      while (j < src.size() && src[j] != '"' && src[j] != '\n')
      {
        if (src[j] == '\\' && j + 1 < src.size()) j++;
        t.text += src[j++];
      }
      if (j >= src.size() || src[j] != '"') throw DataError{line, "text without a closing quote"};
      t.kind = T::String;
      i = j + 1;
    }
    else if (std::string("{}=:.[]-").find(c) != std::string::npos)
    {
      t.kind = T::Punct;
      t.text = std::string(1, c);
      i++;
    }
    else throw DataError{line, std::string("unexpected character '") + c + "'"};
    out.push_back(t);
  }
  Tok end;
  end.line = line;
  out.push_back(end);
  return out;
}

// One step of a field path: a field, optionally with an element.
struct PathStep
{
  std::string field;
  int index = -1;   // -1: all elements
  int line = 0;
};

struct Assignment
{
  std::vector<PathStep> path;
  std::vector<Tok> value;
  int line = 0;
};

struct Record
{
  std::string name, base, file, table;
  int line = 0;
  std::vector<Assignment> assignments;
  std::vector<uint8_t> bytes;
  int state = 0;   // 0 not built, 1 building, 2 built
};

struct Parser
{
  std::vector<Tok> toks;
  size_t pos = 0;

  const Tok &peek(size_t ahead = 0) const { return toks[std::min(pos + ahead, toks.size() - 1)]; }
  Tok next() { return toks[pos < toks.size() - 1 ? pos++ : pos]; }
  bool isPunct(const char *p, size_t ahead = 0) const { return peek(ahead).kind == T::Punct && peek(ahead).text == p; }
  void expect(const char *p)
  {
    if (!isPunct(p)) throw DataError{peek().line, std::string("expected '") + p + "'" + (peek().kind == T::End ? " before the end of the file" : ", found '" + peek().text + "'")};
    pos++;
  }
  Tok ident(const char *what)
  {
    if (peek().kind != T::Ident) throw DataError{peek().line, std::string("expected ") + what};
    return next();
  }
};

void parseFile(const std::string &path, std::vector<Record> &records, std::string &table, int &tableLine)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) throw DataError{0, "cannot read the file"};
  std::stringstream ss;
  ss << in.rdbuf();
  Parser p;
  p.toks = lex(ss.str());
  if (!(p.peek().kind == T::Ident && p.peek().text == "table")) throw DataError{p.peek().line, "a record file starts with 'table Structure'"};
  p.next();
  Tok t = p.ident("the structure of the table");
  table = t.text;
  tableLine = t.line;
  while (p.peek().kind != T::End)
  {
    Tok kw = p.next();
    if (kw.kind != T::Ident || kw.text != "record") throw DataError{kw.line, "expected 'record Name { ... }'"};
    Record r;
    Tok name = p.ident("the name of the record");
    r.name = name.text;
    r.line = name.line;
    r.file = path;
    r.table = table;
    if (p.isPunct(":"))
    {
      p.next();
      r.base = p.ident("the record it starts as a copy of").text;
    }
    p.expect("{");
    while (!p.isPunct("}"))
    {
      if (p.peek().kind == T::End) throw DataError{name.line, "record '" + r.name + "' has no closing '}'"};
      Assignment a;
      a.line = p.peek().line;
      for (;;)
      {
        PathStep s;
        Tok f = p.ident("the name of a field");
        s.field = f.text;
        s.line = f.line;
        if (p.isPunct("["))
        {
          p.next();
          Tok n = p.next();
          if (n.kind != T::Int) throw DataError{n.line, "an element is chosen with a number: field[1]"};
          s.index = int(n.value);
          p.expect("]");
        }
        a.path.push_back(s);
        if (!p.isPunct(".")) break;
        p.next();
      }
      p.expect("=");
      // The value: one token, '-' and a number, or Enum.Value.
      if (p.isPunct("-")) a.value.push_back(p.next());
      a.value.push_back(p.next());
      if (a.value.back().kind == T::Ident && p.isPunct(".") && p.peek(1).kind == T::Ident)
      {
        a.value.push_back(p.next());
        a.value.push_back(p.next());
      }
      if (a.value.back().kind == T::End || a.value.back().kind == T::Punct) throw DataError{a.line, "expected a value after '='"};
      r.assignments.push_back(a);
    }
    p.expect("}");
    records.push_back(r);
  }
}

class Builder
{
public:
  Builder(const script::Module &module, const std::function<uint16_t(const std::string &)> &stringId, std::vector<Record> &records)
      : module_(module), stringId_(stringId), records_(records)
  {
    for (size_t i = 0; i < records_.size(); i++) byName_[records_[i].name] = i;
  }

  const script::StructInfo *structure(const std::string &name) const
  {
    for (const script::StructInfo &s : module_.structs)
      if (s.name == name) return &s;
    return nullptr;
  }

  void build(Record &r)
  {
    if (r.state == 2) return;
    if (r.state == 1) throw DataError{r.line, "record '" + r.name + "' starts as a copy of itself"};
    r.state = 1;
    const script::StructInfo *s = structure(r.table);
    if (!r.base.empty())
    {
      auto it = byName_.find(r.base);
      if (it == byName_.end()) throw DataError{r.line, "there is no record '" + r.base + "'"};
      Record &base = records_[it->second];
      if (base.table != r.table) throw DataError{r.line, "'" + r.base + "' is a " + base.table + ", not a " + r.table};
      build(base);
      // A base that failed has no bytes: copying them would leave this one without room.
      if (base.bytes.empty()) throw DataError{r.line, "'" + r.base + "', which it starts as a copy of, has errors"};
      r.bytes = base.bytes;
    }
    else r.bytes = s->defaults;
    for (const Assignment &a : r.assignments) assign(r, *s, a);
    r.state = 2;
  }

private:
  const script::Module &module_;
  const std::function<uint16_t(const std::string &)> &stringId_;
  std::vector<Record> &records_;
  std::map<std::string, size_t> byName_;

  const script::EnumInfo *enumeration(const std::string &name) const
  {
    for (const script::EnumInfo &e : module_.enums)
      if (e.name == name) return &e;
    return nullptr;
  }

  void assign(Record &r, const script::StructInfo &top, const Assignment &a)
  {
    const script::StructInfo *s = &top;
    size_t base = 0;
    for (size_t i = 0; i < a.path.size(); i++)
    {
      const PathStep &step = a.path[i];
      const script::StructField *f = nullptr;
      for (const script::StructField &c : s->fields)
        if (c.name == step.field) f = &c;
      if (!f) throw DataError{step.line, "'" + s->name + "' has no field '" + step.field + "'"};
      if (step.index >= f->count) throw DataError{step.line, "'" + f->name + "' has " + std::to_string(f->count) + " elements"};
      if (step.index >= 0 && f->count == 1) throw DataError{step.line, "'" + f->name + "' is not an array"};
      size_t first = step.index < 0 ? 0 : size_t(step.index), last = step.index < 0 ? f->count : first + 1;
      bool lastStep = i + 1 == a.path.size();
      if (f->type == BM_DATA_STRUCT)
      {
        if (lastStep) throw DataError{step.line, "'" + f->name + "' is a structure: set its fields (" + f->name + ".field = ...)"};
        if (step.index < 0 && f->count > 1) throw DataError{step.line, "'" + f->name + "' is an array: choose an element (" + f->name + "[0])"};
        base += f->offset + first * f->elementSize;
        s = structure(f->detail);
        continue;
      }
      if (!lastStep) throw DataError{a.path[i + 1].line, "'" + f->name + "' has no fields"};
      uint32_t v = value(*f, a.value);
      for (size_t k = first; k < last; k++)
        for (int b = 0; b < f->elementSize; b++) r.bytes[base + f->offset + k * f->elementSize + size_t(b)] = uint8_t(v >> (8 * b));
    }
  }

  uint32_t value(const script::StructField &f, const std::vector<Tok> &v)
  {
    bool negative = v[0].kind == T::Punct;
    const Tok &x = v.back();
    auto fail = [&](const std::string &why) -> uint32_t { throw DataError{x.line, why}; };
    if (negative && x.kind != T::Int && x.kind != T::Fix) fail("'-' goes before a number");
    switch (f.type)
    {
    case BM_DATA_BOOL:
      if (x.kind != T::Ident || (x.text != "true" && x.text != "false")) fail("'" + f.name + "' is true or false");
      return x.text == "true";
    case BM_DATA_BYTE: case BM_DATA_UBYTE: case BM_DATA_SHORT: case BM_DATA_USHORT: case BM_DATA_INT: case BM_DATA_UINT:
    {
      if (x.kind != T::Int) fail("'" + f.name + "' is a whole number");
      int64_t n = negative ? -x.value : x.value;
      if (f.type == BM_DATA_INT && !negative && n > INT32_MAX && n <= UINT32_MAX) n = int32_t(uint32_t(n));   // 0xFFFFFFFF
      int64_t lo = f.type == BM_DATA_BYTE ? -128 : f.type == BM_DATA_SHORT ? -32768 : f.type == BM_DATA_INT ? INT32_MIN : 0;
      int64_t hi = f.type == BM_DATA_BYTE ? 127 : f.type == BM_DATA_UBYTE ? 255 : f.type == BM_DATA_SHORT ? 32767
                 : f.type == BM_DATA_USHORT ? 65535 : f.type == BM_DATA_INT ? INT32_MAX : UINT32_MAX;
      if (n < lo || n > hi) fail(std::to_string(n) + " does not fit into '" + f.name + "' (" + f.typeLabel + ")");
      return uint32_t(n);
    }
    case BM_DATA_FIX:
    {
      if (x.kind != T::Int && x.kind != T::Fix) fail("'" + f.name + "' is a number");
      int64_t n = x.kind == T::Int ? x.value * 256 : x.value;
      if (n > INT32_MAX) fail("the number is too large for 'fix'");
      return uint32_t(negative ? -n : n);
    }
    case BM_DATA_NAME:
      if (x.kind != T::Name) fail("'" + f.name + "' is a name: #Name");
      return stringId_(x.text);
    case BM_DATA_TEXT:
      if (x.kind != T::String) fail("'" + f.name + "' is a text: \"...\"");
      return stringId_(x.text);
    case BM_DATA_SPRITE: case BM_DATA_SOUND:
      if (x.kind != T::String && x.kind != T::Name) fail("'" + f.name + "' is the name of a " + f.typeLabel + ": \"name\"");
      return stringId_(x.text);
    case BM_DATA_ATTACH:
      if (x.kind != T::Ident) fail("'" + f.name + "' is the name of a script class");
      return stringId_(x.text);
    case BM_DATA_RECORD:
    {
      if (x.kind != T::Ident && x.kind != T::Name) fail("'" + f.name + "' is a record: its name");
      auto it = byName_.find(x.text);
      if (it == byName_.end()) fail("there is no record '" + x.text + "'");
      if (records_[it->second].table != f.detail) fail("'" + x.text + "' is a " + records_[it->second].table + ", not a " + f.detail);
      return script::nameKey(x.text);
    }
    case BM_DATA_ENUM:
    {
      const script::EnumInfo *e = enumeration(f.detail);
      std::string name = x.text;
      if (v.size() == 3 && v[0].text != e->name) fail("'" + f.name + "' is a " + e->name);
      for (size_t k = 0; k < e->values.size(); k++)
        if (e->values[k] == name && x.kind == T::Ident) return uint32_t(k);
      return fail("'" + name + "' is not a value of " + e->name);
    }
    default:
      fail("this field cannot be set");
    }
    return 0;
  }
};

void put16(std::vector<uint8_t> &b, uint32_t v)
{
  b.push_back(uint8_t(v));
  b.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t> &b, uint32_t v)
{
  put16(b, v);
  put16(b, v >> 16);
}

}  // namespace

uint32_t layoutHash(const script::StructInfo &s)
{
  std::vector<uint8_t> b;
  for (const script::StructField &f : s.fields)
  {
    b.insert(b.end(), f.name.begin(), f.name.end());
    b.push_back(0);
    b.push_back(uint8_t(f.type));
    b.push_back(f.count);
    put16(b, f.offset);
    b.insert(b.end(), f.detail.begin(), f.detail.end());
    b.push_back(0);
  }
  return bmCrc32(0, b.data(), b.size());
}

std::vector<DataTable> cookData(const std::vector<std::string> &paths, const script::Module &module,
                                const std::function<uint16_t(const std::string &)> &stringId, Report &report)
{
  std::vector<Record> records;
  for (const std::string &path : paths)
  {
    std::vector<Record> fileRecords;
    std::string table;
    int tableLine = 0;
    try
    {
      parseFile(path, fileRecords, table, tableLine);
    }
    catch (const DataError &e)
    {
      report.error("data", path + ":" + std::to_string(e.line), e.message);
      continue;
    }
    bool known = std::any_of(module.structs.begin(), module.structs.end(), [&](const script::StructInfo &s) { return s.name == table; });
    if (!known)
    {
      report.error("data", path + ":" + std::to_string(tableLine), "there is no structure '" + table + "' (structures are declared in scripts)");
      continue;
    }
    records.insert(records.end(), fileRecords.begin(), fileRecords.end());
  }

  // Names are unique in the whole project, and so are their keys.
  std::map<std::string, const Record *> names;
  std::map<uint32_t, const Record *> keys;
  std::vector<Record> unique;
  for (const Record &r : records)
  {
    std::string where = r.file + ":" + std::to_string(r.line);
    if (names.count(r.name))
    {
      report.error("data", where, "record '" + r.name + "' exists already (" + names[r.name]->file + ")");
      continue;
    }
    uint32_t key = script::nameKey(r.name);
    if (key == 0 || keys.count(key))
    {
      report.error("data", where, "the name '" + r.name + "' has the same key as '" + (key ? keys[key]->name : std::string("nothing")) + "': rename one");
      continue;
    }
    names[r.name] = &r;
    keys[key] = &r;
    unique.push_back(r);
  }

  Builder builder(module, stringId, unique);
  for (Record &r : unique)
  {
    try
    {
      builder.build(r);
    }
    catch (const DataError &e)
    {
      report.error("data", r.file + ":" + std::to_string(e.line), e.message);
      r.state = 2;
      r.bytes.clear();
    }
  }

  std::vector<DataTable> out;
  for (const script::StructInfo &s : module.structs)
  {
    std::vector<const Record *> rows;
    for (const Record &r : unique)
      if (r.table == s.name && r.bytes.size() == s.size) rows.push_back(&r);
    std::sort(rows.begin(), rows.end(), [](const Record *a, const Record *b) { return script::nameKey(a->name) < script::nameKey(b->name); });
    DataTable t;
    t.structure = s.name;
    std::vector<uint8_t> &b = t.chunk;
    put16(b, stringId(s.name));
    put16(b, uint32_t(rows.size()));
    put16(b, s.size);
    put16(b, uint32_t(s.fields.size()));
    put32(b, layoutHash(s));
    for (const script::StructField &f : s.fields)
    {
      put16(b, stringId(f.name));
      b.push_back(uint8_t(f.type));
      b.push_back(f.count);
      put16(b, f.offset);
      put16(b, f.detail.empty() ? BM_DATA_NONE_ID : stringId(f.detail));
    }
    for (const Record *r : rows)
    {
      put32(b, script::nameKey(r->name));
      put16(b, stringId(r->name));
      put16(b, 0);
      t.records.push_back(r->name);
    }
    b.insert(b.end(), s.defaults.begin(), s.defaults.end());
    for (const Record *r : rows) b.insert(b.end(), r->bytes.begin(), r->bytes.end());
    // The roles block (data-format.md §6), only for a structure with roles.
    if (s.roles != 0)
    {
      b.insert(b.end(), {'R', 'O', 'L', 'E'});
      b.push_back(s.roles);
      b.push_back(s.equipSlot);
      put16(b, s.equipOffset);
      b.push_back(uint8_t(s.stats.size()));
      b.push_back(0);
      put16(b, 0);
      for (const script::StructInfo::Stat &st : s.stats)
      {
        put16(b, st.offset);
        b.push_back(uint8_t(st.type));
        b.push_back(0);
      }
    }
    report.info("data", s.file, s.name + ": " + std::to_string(rows.size()) + " records of " + std::to_string(s.size) + " bytes");
    out.push_back(std::move(t));
  }
  return out;
}

}  // namespace bm
