// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "mondskript.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

extern "C" {
#include "vm.h"
}

namespace bm::script {

namespace {

// --- API tables ---------------------------------------------------------------------------

struct ApiDef
{
  const char *name;
  BmScriptType result;
  int argc;
  BmScriptType args[BM_SCRIPT_MAX_API_ARGS];
  bool latent;
};
const ApiDef kApi[] = {
#define X(name, result, argc, a0, a1, a2, a3, latent) {#name, BM_SCRIPT_##result, argc, {BM_SCRIPT_##a0, BM_SCRIPT_##a1, BM_SCRIPT_##a2, BM_SCRIPT_##a3}, latent != 0},
  BM_SCRIPT_API(X)
#undef X
};
struct VarDef
{
  const char *name;
  BmScriptType type;
  bool writable;
};
const VarDef kVars[] = {
#define X(name, type, writable) {#name, BM_SCRIPT_##type, writable != 0},
  BM_SCRIPT_ACTOR_VARS(X)
#undef X
};
struct EventDef
{
  const char *name;
  BmScriptType arg;   // VOID: none
  int kinds;          // 1 << BmScriptKind
};
const EventDef kEventDefs[] = {
#define X(name, arg, kinds) {#name, BM_SCRIPT_##arg, kinds},
  BM_SCRIPT_EVENTS(X)
#undef X
};
const char *const kEvents[] = {
#define X(name, arg, kinds) #name,
  BM_SCRIPT_EVENTS(X)
#undef X
};
const char *const kActorOnly[] = {
#define X(name) #name,
  BM_SCRIPT_ACTOR_ONLY(X)
#undef X
};
const char *const kBaseClasses[] = {"Actor", "Attachment", "Screen"};   // by BmScriptKind
const char *const kKeywords[] = {"class", "extends", "var", "const", "function", "event", "state", "begin", "gotostate", "if", "else",
                                 "while", "for", "in", "loop", "break", "continue", "return", "defaultproperties", "true", "false",
                                 "save", "link", "transient", "derived", "wait_until", "enum", "struct", "is"};
const char *const kTypes[] = {"bool", "int", "fix", "name", "text"};

// --- Lexer --------------------------------------------------------------------------------

enum class Tok { End, Ident, Int, Fix, Name, String, Punct };

struct Token
{
  Tok kind = Tok::End;
  std::string text;
  int32_t value = 0;
  int line = 1, column = 1;
};

struct CompileError
{
  int line, column;
  std::string message;
};

std::vector<Token> lex(const std::string &src)
{
  std::vector<Token> out;
  size_t i = 0;
  int line = 1, col = 1;
  auto advance = [&](size_t n) {
    for (size_t k = 0; k < n && i < src.size(); k++, i++)
    {
      if (src[i] == '\n') { line++; col = 1; }
      else col++;
    }
  };
  while (i < src.size())
  {
    char c = src[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(1); continue; }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
    {
      while (i < src.size() && src[i] != '\n') advance(1);
      continue;
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*')
    {
      int l = line, cl = col;
      advance(2);
      while (i < src.size() && !(src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/')) advance(1);
      if (i >= src.size()) throw CompileError{l, cl, "comment without an end"};
      advance(2);
      continue;
    }
    Token t;
    t.line = line;
    t.column = col;
    auto isIdent = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'; };
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
    {
      size_t j = i;
      while (j < src.size() && isIdent(src[j])) j++;
      t.kind = Tok::Ident;
      t.text = src.substr(i, j - i);
      advance(j - i);
    }
    else if (c >= '0' && c <= '9')
    {
      size_t j = i;
      int64_t v = 0;
      if (c == '0' && j + 1 < src.size() && (src[j + 1] == 'x' || src[j + 1] == 'b'))
      {
        int base = src[j + 1] == 'x' ? 16 : 2;
        j += 2;
        size_t digits = j;
        for (; j < src.size() && (isIdent(src[j])); j++)
        {
          if (src[j] == '_') continue;
          char ch = src[j];
          int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : 99;
          if (d >= base) throw CompileError{line, col, "bad digit in number"};
          v = v * base + d;
          if (v > 0xFFFFFFFFll) throw CompileError{line, col, "number too large"};
        }
        if (j == digits) throw CompileError{line, col, "number without digits"};
        t.kind = Tok::Int;
        t.value = int32_t(uint32_t(v));
      }
      else
      {
        for (; j < src.size() && ((src[j] >= '0' && src[j] <= '9') || src[j] == '_'); j++)
        {
          if (src[j] == '_') continue;
          v = v * 10 + (src[j] - '0');
          if (v > 0x7FFFFFFFll) throw CompileError{line, col, "number too large"};
        }
        // A '.' followed by a digit makes it fixed point ("1..5" is a range).
        if (j + 1 < src.size() && src[j] == '.' && src[j + 1] >= '0' && src[j + 1] <= '9')
        {
          j++;
          double frac = 0, scale = 0.1;
          for (; j < src.size() && src[j] >= '0' && src[j] <= '9'; j++, scale *= 0.1) frac += (src[j] - '0') * scale;
          if (v > 0x7FFFFF) throw CompileError{line, col, "fixed-point number too large"};
          t.kind = Tok::Fix;
          t.value = int32_t(v * 256 + int64_t(frac * 256.0 + 0.5));
        }
        else
        {
          t.kind = Tok::Int;
          t.value = int32_t(v);
        }
      }
      t.text = src.substr(i, j - i);
      advance(j - i);
    }
    else if (c == '#')
    {
      size_t j = i + 1;
      while (j < src.size() && isIdent(src[j])) j++;
      if (j == i + 1) throw CompileError{line, col, "a name is written #Name"};
      t.kind = Tok::Name;
      t.text = src.substr(i + 1, j - i - 1);
      advance(j - i);
    }
    else if (c == '"')
    {
      size_t j = i + 1;
      std::string s;
      while (j < src.size() && src[j] != '"' && src[j] != '\n')
      {
        if (src[j] == '\\' && j + 1 < src.size()) j++;
        s += src[j++];
      }
      if (j >= src.size() || src[j] != '"') throw CompileError{line, col, "text without a closing quote"};
      t.kind = Tok::String;
      t.text = s;
      advance(j + 1 - i);
    }
    else
    {
      static const char *const two[] = {"==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "..", ":="};
      t.kind = Tok::Punct;
      t.text = std::string(1, c);
      for (const char *p : two)
        if (i + 1 < src.size() && src[i] == p[0] && src[i + 1] == p[1]) t.text = p;
      if (!std::strchr("+-*/%=<>!(){};,.:&|[]", c)) throw CompileError{line, col, std::string("unexpected character '") + c + "'"};
      advance(t.text.size());
    }
    out.push_back(t);
  }
  Token end;
  end.line = line;
  end.column = col;
  out.push_back(end);
  return out;
}

// --- Enums and structures -------------------------------------------------------------------
//
// Declared before the class of a file, or in files without a class. All of them are known
// before any class is compiled, so a file can use what another declares.

// While compiling, a value of enum k has the type kEnumTypeBase + k; the module only says
// BM_SCRIPT_ENUM.
constexpr int kEnumTypeBase = 0x40;
constexpr int kRecordTypeBase = 0x200;   // record<S>: kRecordTypeBase + structure
BmScriptType enumType(size_t k) { return BmScriptType(kEnumTypeBase + int(k)); }
bool isEnumType(BmScriptType t) { return int(t) >= kEnumTypeBase && int(t) < kRecordTypeBase; }
BmScriptType recordType(size_t k) { return BmScriptType(kRecordTypeBase + int(k)); }
bool isRecordType(BmScriptType t) { return int(t) >= kRecordTypeBase; }
BmScriptType moduleType(BmScriptType t) { return isEnumType(t) ? BM_SCRIPT_ENUM : isRecordType(t) ? BM_SCRIPT_RECORD : t; }

struct Types
{
  std::vector<EnumInfo> enums;
  std::vector<StructInfo> structs;

  int findEnum(const std::string &name) const
  {
    for (size_t i = 0; i < enums.size(); i++)
      if (enums[i].name == name) return int(i);
    return -1;
  }
  int findStruct(const std::string &name) const
  {
    for (size_t i = 0; i < structs.size(); i++)
      if (structs[i].name == name) return int(i);
    return -1;
  }
  static int findValue(const EnumInfo &e, const std::string &value)
  {
    for (size_t i = 0; i < e.values.size(); i++)
      if (e.values[i] == value) return int(i);
    return -1;
  }
  std::string label(BmScriptType t) const
  {
    if (isEnumType(t)) return enums[size_t(int(t) - kEnumTypeBase)].name;
    if (isRecordType(t)) return "record<" + structs[size_t(int(t) - kRecordTypeBase)].name + ">";
    return typeName(t);
  }
};

// A structure's field before layout: types may name structures declared later or elsewhere.
struct RawField
{
  Token type;
  std::string param;          // record<S>: S
  int count = 1;
  Token name;
  std::vector<Token> value;   // the default, up to ';'
};

struct RawRole
{
  Token name;
  Token param;      // equip(Slot value or field), stat_source(field)
  bool hasParam = false;
};

struct RawStruct
{
  StructInfo info;
  Token base;
  std::vector<RawRole> roles;
  std::vector<RawField> fields;
  int state = 0;              // layout: 0 not yet, 1 in progress, 2 done
};

const std::pair<const char *, BmDataType> kDataTypes[] = {
    {"bool", BM_DATA_BOOL}, {"byte", BM_DATA_BYTE}, {"ubyte", BM_DATA_UBYTE}, {"short", BM_DATA_SHORT},
    {"ushort", BM_DATA_USHORT}, {"int", BM_DATA_INT}, {"uint", BM_DATA_UINT}, {"fix", BM_DATA_FIX},
    {"name", BM_DATA_NAME}, {"text", BM_DATA_TEXT}, {"sprite", BM_DATA_SPRITE}, {"sound", BM_DATA_SOUND},
    {"attach", BM_DATA_ATTACH}};

int dataTypeSize(BmDataType t)
{
  switch (t)
  {
  case BM_DATA_BOOL: case BM_DATA_BYTE: case BM_DATA_UBYTE: case BM_DATA_ENUM: return 1;
  case BM_DATA_SHORT: case BM_DATA_USHORT: case BM_DATA_NAME: case BM_DATA_TEXT: case BM_DATA_SPRITE:
  case BM_DATA_SOUND: case BM_DATA_ATTACH: return 2;
  default: return 4;
  }
}

// Reads the enums and structures at the start of a file; stops at 'class' or the end.
class DeclParser
{
public:
  DeclParser(const Source &source, const std::vector<Token> &tokens) : source_(source), tokens_(tokens) {}

  void run(std::vector<EnumInfo> &enums, std::vector<RawStruct> &structs)
  {
    while (peek().kind != Tok::End && !isWord("class"))
    {
      Token t = next();
      if (t.kind == Tok::Ident && t.text == "enum") enums.push_back(enumDecl());
      else if (t.kind == Tok::Ident && t.text == "struct") structs.push_back(structDecl());
      else error(t, "a script starts with 'class Name extends Actor;' (enums and structures may come before it)");
    }
  }

private:
  const Source &source_;
  const std::vector<Token> &tokens_;
  size_t pos_ = 0;

  const Token &peek(size_t ahead = 0) const { return tokens_[std::min(pos_ + ahead, tokens_.size() - 1)]; }
  Token next() { return tokens_[pos_ < tokens_.size() - 1 ? pos_++ : pos_]; }
  [[noreturn]] void error(const Token &t, const std::string &message) const { throw CompileError{t.line, t.column, message}; }
  bool isPunct(const char *p) const { return peek().kind == Tok::Punct && peek().text == p; }
  bool isWord(const char *w) const { return peek().kind == Tok::Ident && peek().text == w; }
  void expectPunct(const char *p)
  {
    if (!isPunct(p)) error(peek(), std::string("expected '") + p + "'" + (peek().kind == Tok::End ? " before the end of the file" : ", found '" + peek().text + "'"));
    pos_++;
  }
  Token ident(const char *what)
  {
    if (peek().kind != Tok::Ident || isKeyword(peek().text)) error(peek(), std::string("expected ") + what);
    return next();
  }

  EnumInfo enumDecl()
  {
    EnumInfo e;
    Token name = ident("the name of the enum");
    e.name = name.text;
    e.file = source_.path;
    e.line = name.line;
    expectPunct("{");
    while (!isPunct("}"))
    {
      Token v = ident("the name of a value");
      if (Types::findValue(e, v.text) >= 0) error(v, "'" + v.text + "' is in this enum twice");
      e.values.push_back(v.text);
      if (!isPunct(",")) break;
      pos_++;
    }
    expectPunct("}");
    if (e.values.empty()) error(name, "an enum needs at least one value");
    if (e.values.size() > BM_DATA_MAX_ENUM_VALUES) error(name, "an enum has at most 256 values");
    return e;
  }

  RawStruct structDecl()
  {
    RawStruct s;
    Token name = ident("the name of the structure");
    s.info.name = name.text;
    s.info.file = source_.path;
    s.info.line = name.line;
    if (isWord("is"))
    {
      pos_++;
      s.base = ident("the structure it is based on");
      s.info.base = s.base.text;
    }
    // Roles: struct Armour is Item : item, equip(slot), stat_source(stats) { ... }
    if (isPunct(":"))
    {
      pos_++;
      while (true)
      {
        RawRole r;
        r.name = ident("a role (item, equip, usable, stat_source)");
        if (isPunct("("))
        {
          pos_++;
          r.param = ident("what the role refers to");
          r.hasParam = true;
          expectPunct(")");
        }
        for (const RawRole &o : s.roles)
          if (o.name.text == r.name.text) error(r.name, "role '" + r.name.text + "' is given twice");
        s.roles.push_back(r);
        if (!isPunct(",")) break;
        pos_++;
      }
    }
    expectPunct("{");
    while (!isPunct("}"))
    {
      if (peek().kind == Tok::End) error(name, "structure '" + name.text + "' has no closing '}'");
      RawField f;
      f.type = ident("the type of a field");
      if (f.type.text == "record")
      {
        expectPunct("<");
        f.param = ident("the structure of the records").text;
        expectPunct(">");
      }
      if (isPunct("["))
      {
        pos_++;
        Token n = next();
        if (n.kind != Tok::Int || n.value < 1 || n.value > 255) error(n, "an array has 1 to 255 elements");
        f.count = n.value;
        expectPunct("]");
      }
      f.name = ident("the name of the field");
      if (isPunct("="))
      {
        pos_++;
        while (!isPunct(";"))
        {
          if (peek().kind == Tok::End || isPunct("}")) error(peek(), "expected ';' after the default");
          f.value.push_back(next());
        }
        if (f.value.empty()) error(peek(), "expected a default value after '='");
      }
      expectPunct(";");
      for (const RawField &g : s.fields)
        if (g.name.text == f.name.text) error(f.name, "field '" + f.name.text + "' is declared twice");
      s.fields.push_back(f);
    }
    expectPunct("}");
    return s;
  }
};

// Lays out the structures (in any order of declaration) and fills in their defaults.
class StructLayout
{
public:
  StructLayout(const std::vector<EnumInfo> &enums, std::vector<RawStruct> &raw, const std::function<uint16_t(const std::string &)> &stringId)
      : enums_(enums), raw_(raw), stringId_(stringId)
  {
  }

  // Returns false with the error's position if a structure cannot be laid out.
  void layout(size_t i)
  {
    RawStruct &s = raw_[i];
    if (s.state == 2) return;
    if (s.state == 1) throw CompileError{s.info.line, 1, "structure '" + s.info.name + "' contains itself"};
    s.state = 1;
    StructInfo &info = s.info;
    size_t at = 0;
    if (!info.base.empty())
    {
      int b = find(info.base);
      if (b < 0) throw CompileError{s.base.line, s.base.column, "there is no structure '" + info.base + "'"};
      layout(size_t(b));
      const StructInfo &base = raw_[size_t(b)].info;
      info.fields = base.fields;
      for (StructField &f : info.fields) f.inherited = true;
      info.defaults = base.defaults;
      info.align = base.align;
      at = base.size;
    }
    for (const RawField &r : s.fields)
    {
      for (const StructField &f : info.fields)
        if (f.name == r.name.text)
          throw CompileError{r.name.line, r.name.column, "field '" + r.name.text + "' exists already in '" + info.base + "'"};
      StructField f;
      f.name = r.name.text;
      f.line = r.name.line;
      f.count = uint8_t(r.count);
      int align = 1;
      if (!fieldType(r, f, align)) throw CompileError{r.type.line, r.type.column, "'" + r.type.text + "' is not a type of a field"};
      f.typeLabel = r.type.text + (r.param.empty() ? "" : "<" + r.param + ">") + (r.count > 1 ? "[" + std::to_string(r.count) + "]" : "");
      at = (at + size_t(align) - 1) / size_t(align) * size_t(align);
      f.offset = uint16_t(at);
      at += size_t(f.elementSize) * f.count;
      info.align = uint16_t(std::max<int>(info.align, align));
      if (at > BM_DATA_MAX_RECORD_SIZE)
        throw CompileError{r.name.line, r.name.column, "structure '" + info.name + "' grows beyond " + std::to_string(BM_DATA_MAX_RECORD_SIZE) +
                                                          " bytes with field '" + f.name + "'"};
      info.fields.push_back(f);
      if (info.fields.size() > BM_DATA_MAX_FIELDS)
        throw CompileError{r.name.line, r.name.column, "a structure has at most " + std::to_string(BM_DATA_MAX_FIELDS) + " fields"};
      info.defaults.resize(at, 0);
      setDefault(info, info.fields.back(), r);
    }
    info.size = uint16_t((at + 3) / 4 * 4);
    if (info.size == 0) info.size = 4;
    info.defaults.resize(info.size, 0);
    s.state = 2;
    for (const RawRole &r : s.roles) role(s, r);
  }

  /// Warnings found while laying out (a stat whose field is named unlike the enum Stat says).
  std::vector<Diagnostic> warnings;

private:
  const StructField *fieldNamed(const StructInfo &info, const std::string &name) const
  {
    for (const StructField &f : info.fields)
      if (f.name == name) return &f;
    return nullptr;
  }

  static bool isWhole(BmDataType t)
  {
    return t == BM_DATA_BOOL || t == BM_DATA_BYTE || t == BM_DATA_UBYTE || t == BM_DATA_SHORT || t == BM_DATA_USHORT ||
           t == BM_DATA_INT || t == BM_DATA_UINT;
  }

  // "MaxHealth" and "max_health" are the same name.
  static std::string plain(const std::string &s)
  {
    std::string out;
    for (char c : s)
      if (c != '_') out += char(std::tolower(uint8_t(c)));
    return out;
  }

  void role(RawStruct &s, const RawRole &r)
  {
    StructInfo &info = s.info;
    auto fail = [&](const Token &t, const std::string &why) { throw CompileError{t.line, t.column, why}; };
    auto need = [&](const char *field, bool ok, const char *what) {
      const StructField *f = fieldNamed(info, field);
      if (f == nullptr || !ok)
        fail(r.name, "role '" + r.name.text + "' needs a field '" + field + "' (" + what + ")");
    };
    if (r.name.text == "item")
    {
      const StructField *t = fieldNamed(info, "title"), *i = fieldNamed(info, "icon"), *m = fieldNamed(info, "max_stack");
      need("title", t && t->type == BM_DATA_TEXT, "text");
      need("icon", i && i->type == BM_DATA_SPRITE, "sprite");
      need("max_stack", m && isWhole(m->type), "a whole number");
      if (r.hasParam) fail(r.param, "role 'item' takes nothing in brackets");
      info.roles |= BM_DATA_ROLE_ITEM;
    }
    else if (r.name.text == "usable")
    {
      bool any = false;
      for (const StructField &f : info.fields) any |= f.type == BM_DATA_ATTACH;
      if (!any) fail(r.name, "role 'usable' needs a field of type attach (what using it does)");
      if (r.hasParam) fail(r.param, "role 'usable' takes nothing in brackets");
      info.roles |= BM_DATA_ROLE_USABLE;
    }
    else if (r.name.text == "equip")
    {
      int slots = findEnum("Slot");
      if (slots < 0) fail(r.name, "role 'equip' needs the equipment slots: declare 'enum Slot { ... }'");
      const EnumInfo &e = enums_[size_t(slots)];
      if (e.values.size() > BM_DATA_MAX_EQUIP_SLOTS)
        fail(r.name, "enum Slot has at most " + std::to_string(BM_DATA_MAX_EQUIP_SLOTS) + " values (the equipment slots)");
      if (!r.hasParam) fail(r.name, "role 'equip' needs its slot: equip(Weapon), or the field that names it: equip(slot)");
      if (const StructField *f = fieldNamed(info, r.param.text))
      {
        if (f->type != BM_DATA_ENUM || f->detail != "Slot" || f->count != 1) fail(r.param, "field '" + f->name + "' is not of type Slot");
        info.equipSlot = BM_DATA_SLOT_FROM_FIELD;
        info.equipOffset = f->offset;
      }
      else
      {
        int v = Types::findValue(e, r.param.text);
        if (v < 0) fail(r.param, "'" + r.param.text + "' is neither a value of enum Slot nor a field of type Slot");
        info.equipSlot = uint8_t(v);
      }
      info.roles |= BM_DATA_ROLE_EQUIP;
    }
    else if (r.name.text == "stat_source")
    {
      std::string name = r.hasParam ? r.param.text : "stats";
      const StructField *f = fieldNamed(info, name);
      if (f == nullptr || f->type != BM_DATA_STRUCT || f->count != 1)
        fail(r.hasParam ? r.param : r.name, "role 'stat_source' needs a field '" + name + "' that is a structure of numbers");
      const StructInfo &inner = raw_[size_t(find(f->detail))].info;
      if (inner.fields.size() > BM_DATA_MAX_STATS)
        fail(r.name, "structure '" + inner.name + "' has more than " + std::to_string(BM_DATA_MAX_STATS) + " stats");
      int stat = findEnum("Stat");
      info.stats.clear();
      for (size_t i = 0; i < inner.fields.size(); i++)
      {
        const StructField &g = inner.fields[i];
        if (!isWhole(g.type) || g.count != 1)
          fail(r.name, "stat '" + g.name + "' of '" + inner.name + "' is not a whole number (field i adds to stat i)");
        info.stats.push_back({uint16_t(f->offset + g.offset), g.type});
        // Field i adds to stat i: say so when the names do not match.
        if (stat >= 0)
        {
          const EnumInfo &e = enums_[size_t(stat)];
          if (i >= e.values.size() || plain(e.values[i]) != plain(g.name))
            warnings.push_back({info.file, g.line, 1,
                                "'" + inner.name + "." + g.name + "' adds to stat " + std::to_string(i) +
                                    (i < e.values.size() ? " (" + e.values[i] + ")" : " (Stat has no value there)"),
                                true});
        }
      }
      info.roles |= BM_DATA_ROLE_STAT_SOURCE;
    }
    else fail(r.name, "'" + r.name.text + "' is not a role (item, equip, usable, stat_source)");
  }

  const std::vector<EnumInfo> &enums_;
  std::vector<RawStruct> &raw_;
  const std::function<uint16_t(const std::string &)> &stringId_;

  int find(const std::string &name) const
  {
    for (size_t i = 0; i < raw_.size(); i++)
      if (raw_[i].info.name == name) return int(i);
    return -1;
  }
  int findEnum(const std::string &name) const
  {
    for (size_t i = 0; i < enums_.size(); i++)
      if (enums_[i].name == name) return int(i);
    return -1;
  }

  bool fieldType(const RawField &r, StructField &f, int &align)
  {
    if (r.type.text == "record")
    {
      if (find(r.param) < 0) throw CompileError{r.type.line, r.type.column, "there is no structure '" + r.param + "'"};
      f.type = BM_DATA_RECORD;
      f.detail = r.param;
    }
    else if (int e = findEnum(r.type.text); e >= 0)
    {
      f.type = BM_DATA_ENUM;
      f.detail = r.type.text;
    }
    else if (int s = find(r.type.text); s >= 0)
    {
      layout(size_t(s));
      f.type = BM_DATA_STRUCT;
      f.detail = r.type.text;
      f.elementSize = raw_[size_t(s)].info.size;
      align = raw_[size_t(s)].info.align;
      return true;
    }
    else
    {
      bool known = false;
      for (const auto &[n, t] : kDataTypes)
        if (r.type.text == n) { f.type = t; known = true; }
      if (!known) return false;
    }
    f.elementSize = uint16_t(dataTypeSize(f.type));
    align = f.elementSize;
    return true;
  }

  static void put(std::vector<uint8_t> &d, size_t at, uint32_t v, int size)
  {
    for (int i = 0; i < size; i++) d[at + size_t(i)] = uint8_t(v >> (8 * i));
  }

  void setDefault(StructInfo &info, const StructField &f, const RawField &r)
  {
    // Without a default: 0, or "none" for references to sprites, sounds and classes; an inline
    // structure starts with its own defaults.
    uint32_t value = 0;
    if (f.type == BM_DATA_SPRITE || f.type == BM_DATA_SOUND || f.type == BM_DATA_ATTACH) value = BM_DATA_NONE_ID;
    if (f.type == BM_DATA_STRUCT)
    {
      if (!r.value.empty()) throw CompileError{r.value[0].line, r.value[0].column, "an inner structure takes the defaults of its own fields"};
      const StructInfo &inner = raw_[size_t(find(f.detail))].info;
      for (int k = 0; k < f.count; k++)
        std::copy(inner.defaults.begin(), inner.defaults.end(), info.defaults.begin() + f.offset + k * f.elementSize);
      return;
    }
    if (!r.value.empty()) value = parseValue(f, r.value);
    for (int k = 0; k < f.count; k++) put(info.defaults, f.offset + size_t(k) * f.elementSize, value, f.elementSize);
  }

  uint32_t parseValue(const StructField &f, const std::vector<Token> &v)
  {
    const Token &t = v[0];
    auto fail = [&](const std::string &why) -> uint32_t { throw CompileError{t.line, t.column, why}; };
    bool negative = t.kind == Tok::Punct && t.text == "-";
    const Token &x = negative && v.size() > 1 ? v[1] : t;
    size_t used = negative ? 2 : 1;
    uint32_t out = 0;
    switch (f.type)
    {
    case BM_DATA_BOOL:
      if (x.kind != Tok::Ident || (x.text != "true" && x.text != "false") || negative) fail("expected true or false");
      out = x.text == "true";
      break;
    case BM_DATA_BYTE: case BM_DATA_UBYTE: case BM_DATA_SHORT: case BM_DATA_USHORT: case BM_DATA_INT: case BM_DATA_UINT:
    {
      if (x.kind != Tok::Int) fail("expected a whole number");
      int64_t n = negative ? -int64_t(x.value) : int64_t(x.kind == Tok::Int && f.type == BM_DATA_UINT ? uint32_t(x.value) : x.value);
      int64_t lo = f.type == BM_DATA_BYTE ? -128 : f.type == BM_DATA_SHORT ? -32768 : f.type == BM_DATA_INT ? INT32_MIN : 0;
      int64_t hi = f.type == BM_DATA_BYTE ? 127 : f.type == BM_DATA_UBYTE ? 255 : f.type == BM_DATA_SHORT ? 32767
                 : f.type == BM_DATA_USHORT ? 65535 : f.type == BM_DATA_INT ? INT32_MAX : UINT32_MAX;
      if (n < lo || n > hi) fail(std::to_string(n) + " does not fit into " + f.typeLabel);
      out = uint32_t(n);
      break;
    }
    case BM_DATA_FIX:
      if (x.kind == Tok::Int) out = uint32_t(x.value * 256);
      else if (x.kind == Tok::Fix) out = uint32_t(x.value);
      else fail("expected a number");
      if (negative) out = uint32_t(-int32_t(out));
      break;
    case BM_DATA_NAME:
      if (x.kind != Tok::Name || negative) fail("expected a name (#Name)");
      out = stringId_(x.text);
      break;
    case BM_DATA_TEXT:
      if (x.kind != Tok::String || negative) fail("expected a text (\"...\")");
      out = stringId_(x.text);
      break;
    case BM_DATA_SPRITE: case BM_DATA_SOUND:
      if ((x.kind != Tok::String && x.kind != Tok::Name) || negative) fail("expected the name of a " + f.typeLabel);
      out = stringId_(x.text);
      break;
    case BM_DATA_RECORD:
      if (x.kind != Tok::Name || negative) fail("expected a record (#Name)");
      out = nameKey(x.text);
      break;
    case BM_DATA_ATTACH:
      if (x.kind != Tok::Ident || negative) fail("expected the name of a script class");
      out = stringId_(x.text);
      break;
    case BM_DATA_ENUM:
    {
      const EnumInfo &e = enums_[size_t(findEnum(f.detail))];
      // Fire, or Element.Fire
      const Token *valueTok = &x;
      if (v.size() >= 3 && x.text == e.name && v[1].kind == Tok::Punct && v[1].text == ".")
      {
        valueTok = &v[2];
        used = 3;
      }
      int k = Types::findValue(e, valueTok->text);
      if (k < 0 || negative) fail("'" + valueTok->text + "' is not a value of " + e.name);
      out = uint32_t(k);
      break;
    }
    default:
      fail("this field has no default");
    }
    if (v.size() != used) throw CompileError{v[used].line, v[used].column, "expected ';' after the default"};
    return out;
  }
};

// --- Compiler -----------------------------------------------------------------------------

struct Function
{
  std::string name;
  BmScriptType result = BM_SCRIPT_VOID;
  std::vector<std::pair<std::string, BmScriptType>> params;
  bool stateCode = false;
  size_t bodyToken = 0;         // index of '{' (or of the first statement of state code)
  int state = -1;               // the state it belongs to, -1: the class
  int event = -1;
  std::vector<uint16_t> code;
  std::vector<std::pair<uint16_t, uint16_t>> lines;
  int regCount = 0;
  int classIndex = 0;
  // Words that hold a function index (CALL) or a constant index (LOADK): renumbered when
  // the classes are written as modules of their own or all together.
  std::vector<size_t> callSites, constSites;
};

struct State
{
  std::string name;
  int code = -1;
  int events[BM_SCRIPT_EVENT_COUNT];
};

struct Class
{
  ClassInfo info;
  std::vector<int32_t> constPool;   // this class's constants (LOADK indices are into it)
  size_t firstFunction = 0, functionCount = 0;
  std::vector<State> states;
  int events[BM_SCRIPT_EVENT_COUNT];
  std::map<std::string, int> functions;            // class-level functions by name
  std::map<std::string, std::pair<BmScriptType, int32_t>> consts;
  int startState = -1;
  int startStateLine = 0;
};

// A value while compiling an expression: a constant not yet in a register, or a register.
struct Value
{
  BmScriptType type = BM_SCRIPT_VOID;
  bool isConst = false;
  bool literalInt = false;      // an integer literal: becomes fix where fix is expected
  int32_t value = 0;
  int reg = -1;
  std::string nameText;         // a #Name literal: becomes a record's key where record<S> is expected
};

class Compiler
{
public:
  Compiler(const Source &source, const std::vector<Token> &tokens, const Types &types,
           const std::function<uint16_t(const std::string &)> &stringId, std::vector<Function> &functions, std::vector<int32_t> &consts,
           int classIndex)
      : source_(source), types_(types), stringId_(stringId), functions_(functions), consts_(consts), classIndex_(classIndex), tokens_(tokens)
  {
    for (int &e : cls_.events) e = -1;
  }

  Class run()
  {
    declarations();
    for (size_t i = firstFunction_; i < functions_.size(); i++) body(functions_[i], int(i));
    return cls_;
  }

  /// False for a file with only enums and structures.
  bool hasClass() const { return hasClass_; }

private:
  const Source &source_;
  const Types &types_;
  const std::function<uint16_t(const std::string &)> &stringId_;
  std::vector<Function> &functions_;
  std::vector<int32_t> &consts_;
  int classIndex_;
  const std::vector<Token> &tokens_;
  size_t pos_ = 0;
  Class cls_;
  bool hasClass_ = true;

  std::string tn(BmScriptType t) const { return types_.label(t); }
  size_t firstFunction_ = 0;

  // The function being compiled.
  Function *fn_ = nullptr;
  std::vector<std::map<std::string, std::pair<int, BmScriptType>>> scopes_;
  int localTop_ = 0, tempTop_ = 0;
  struct Loop
  {
    std::vector<size_t> breaks, continues;
  };
  std::vector<Loop> loops_;
  int lastLine_ = 0;

  // --- Tokens ---
  const Token &peek(size_t ahead = 0) const { return tokens_[std::min(pos_ + ahead, tokens_.size() - 1)]; }
  Token next() { return tokens_[pos_ < tokens_.size() - 1 ? pos_++ : pos_]; }
  [[noreturn]] void error(const Token &t, const std::string &message) const { throw CompileError{t.line, t.column, message}; }
  bool isPunct(const char *p, size_t ahead = 0) const { return peek(ahead).kind == Tok::Punct && peek(ahead).text == p; }
  bool isWord(const char *w, size_t ahead = 0) const { return peek(ahead).kind == Tok::Ident && peek(ahead).text == w; }
  bool acceptPunct(const char *p)
  {
    if (!isPunct(p)) return false;
    pos_++;
    return true;
  }
  bool acceptWord(const char *w)
  {
    if (!isWord(w)) return false;
    pos_++;
    return true;
  }
  void expectPunct(const char *p)
  {
    if (!acceptPunct(p)) error(peek(), std::string("expected '") + p + "'" + (peek().kind == Tok::End ? " before the end of the file" : ", found '" + peek().text + "'"));
  }
  Token expectIdent(const std::string &what)
  {
    if (peek().kind != Tok::Ident || isKeyword(peek().text)) error(peek(), "expected " + what);
    return next();
  }
  // After the name of an enum: ".Value".
  Value enumValue(const Token &enumName)
  {
    int e = types_.findEnum(enumName.text);
    expectPunct(".");
    Token valueTok = expectIdent("a value of " + enumName.text);
    int k = Types::findValue(types_.enums[size_t(e)], valueTok.text);
    if (k < 0) error(valueTok, "'" + valueTok.text + "' is not a value of " + enumName.text);
    Value v;
    v.isConst = true;
    v.type = enumType(size_t(e));
    v.value = k;
    return v;
  }
  bool parseType(BmScriptType *pType)
  {
    if (peek().kind != Tok::Ident) return false;
    static const std::pair<const char *, BmScriptType> types[] = {
        {"bool", BM_SCRIPT_BOOL}, {"int", BM_SCRIPT_INT}, {"fix", BM_SCRIPT_FIX}, {"name", BM_SCRIPT_NAME}, {"text", BM_SCRIPT_TEXT}};
    for (const auto &[n, t] : types)
      if (peek().text == n)
      {
        *pType = t;
        pos_++;
        return true;
      }
    if (int e = types_.findEnum(peek().text); e >= 0)
    {
      *pType = enumType(size_t(e));
      pos_++;
      return true;
    }
    if (isWord("record") && isPunct("<", 1))
    {
      pos_ += 2;
      Token s = expectIdent("the structure of the records");
      int k = types_.findStruct(s.text);
      if (k < 0) error(s, "there is no structure '" + s.text + "'");
      expectPunct(">");
      *pType = recordType(size_t(k));
      return true;
    }
    return false;
  }
  void skipBlock()
  {
    expectPunct("{");
    int depth = 1;
    while (depth > 0)
    {
      if (peek().kind == Tok::End) error(peek(), "'{' without '}'");
      if (isPunct("{")) depth++;
      if (isPunct("}")) depth--;
      pos_++;
    }
  }

  // A literal for a field default or a constant.
  Value literal(BmScriptType want)
  {
    bool negative = acceptPunct("-");
    Token t = next();
    Value v;
    v.isConst = true;
    if (t.kind == Tok::Int)
    {
      v.type = want == BM_SCRIPT_FIX ? BM_SCRIPT_FIX : BM_SCRIPT_INT;
      v.value = want == BM_SCRIPT_FIX ? t.value * 256 : t.value;
    }
    else if (t.kind == Tok::Fix) { v.type = BM_SCRIPT_FIX; v.value = t.value; }
    else if (t.kind == Tok::Name && !negative && isRecordType(want)) { v.type = want; v.value = int32_t(nameKey(t.text)); }
    else if (t.kind == Tok::Name && !negative) { v.type = BM_SCRIPT_NAME; v.value = stringId_(t.text); }
    else if (t.kind == Tok::String && !negative) { v.type = BM_SCRIPT_TEXT; v.value = stringId_(t.text); }
    else if (t.kind == Tok::Ident && (t.text == "true" || t.text == "false") && !negative) { v.type = BM_SCRIPT_BOOL; v.value = t.text == "true"; }
    else if (t.kind == Tok::Ident && cls_.consts.count(t.text)) { v.type = cls_.consts[t.text].first; v.value = cls_.consts[t.text].second; }
    else if (t.kind == Tok::Ident && isEnumType(want) && !negative)
    {
      // Fire, or Element.Fire
      const EnumInfo &e = types_.enums[size_t(int(want) - kEnumTypeBase)];
      Token valueTok = t;
      if (t.text == e.name && acceptPunct(".")) valueTok = expectIdent("a value of " + e.name);
      int k = Types::findValue(e, valueTok.text);
      if (k < 0) error(valueTok, "'" + valueTok.text + "' is not a value of " + e.name);
      v.type = want;
      v.value = k;
    }
    else if (t.kind == Tok::Ident && types_.findEnum(t.text) >= 0 && isPunct(".") && !negative) v = enumValue(t);
    else error(t, "expected a value");
    if (negative) v.value = -v.value;
    if (v.type != want) error(t, std::string("expected a value of type ") + tn(want) + ", this is " + tn(v.type));
    return v;
  }

  // --- Declarations (bodies are skipped and compiled afterwards) ---
  int addFunction(Function f)
  {
    f.classIndex = classIndex_;
    functions_.push_back(std::move(f));
    return int(functions_.size()) - 1;
  }

  int eventIndex(const Token &name) const
  {
    for (int e = 0; e < BM_SCRIPT_EVENT_COUNT; e++)
      if (name.text == kEvents[e]) return e;
    std::string known;
    for (const char *e : kEvents) known += std::string(known.empty() ? "" : ", ") + e;
    error(name, "unknown event '" + name.text + "' (the events are: " + known + ")");
  }

  void eventDecl(int state)
  {
    Token name = expectIdent("the name of an event");
    int e = eventIndex(name);
    const EventDef &def = kEventDefs[e];
    if (!(def.kinds & (1 << cls_.info.kind)))
      error(name, "an " + cls_.info.base + " does not get the event '" + name.text + "'");
    expectPunct("(");
    std::vector<std::pair<std::string, BmScriptType>> params;
    if (def.arg != BM_SCRIPT_VOID)
    {
      BmScriptType t;
      Token at = peek();
      if (!parseType(&t) || t != def.arg) error(at, "event '" + name.text + "' takes one " + tn(def.arg) + ": " + name.text + "(" + tn(def.arg) + " value)");
      params.push_back({expectIdent("the name of the parameter").text, t});
    }
    else if (!isPunct(")")) error(peek(), "event '" + name.text + "' takes no parameters");
    expectPunct(")");
    int &slot = state >= 0 ? cls_.states[size_t(state)].events[e] : cls_.events[e];
    if (slot >= 0) error(name, "event '" + name.text + "' is defined twice");
    Function f;
    f.name = name.text;
    f.event = e;
    f.state = state;
    f.params = params;
    f.bodyToken = pos_;
    skipBlock();
    slot = addFunction(f);
    if (state < 0) cls_.info.events.push_back(name.text);
  }

  void declarations()
  {
    firstFunction_ = functions_.size();
    // Enums and structures come first; DeclParser has read them already.
    while (isWord("enum") || isWord("struct"))
    {
      while (!isPunct("{")) pos_++;
      skipBlock();
    }
    if (peek().kind == Tok::End)
    {
      hasClass_ = false;
      return;
    }
    if (!acceptWord("class")) error(peek(), "a script starts with 'class Name extends Actor;'");
    cls_.info.name = expectIdent("the name of the class").text;
    cls_.info.file = source_.path;
    if (acceptWord("extends"))
    {
      Token base = expectIdent("the base class");
      int kind = -1;
      for (int k = 0; k < BM_SCRIPT_KIND_COUNT; k++)
        if (base.text == kBaseClasses[k]) kind = k;
      if (kind < 0) error(base, "classes extend Actor, Attachment or Screen in this version");
      cls_.info.base = base.text;
      cls_.info.kind = BmScriptKind(kind);
    }
    expectPunct(";");

    while (peek().kind != Tok::End)
    {
      Token t = peek();
      if (acceptWord("var"))
      {
        FieldInfo f;
        f.line = t.line;
        if (acceptPunct("("))
        {
          f.category = expectIdent("a category").text;
          f.flags |= BM_SCRIPT_FIELD_EDITABLE;
          expectPunct(")");
        }
        for (;;)
        {
          if (acceptWord("save")) f.flags |= BM_SCRIPT_FIELD_SAVE;
          else if (acceptWord("link")) f.flags |= BM_SCRIPT_FIELD_LINK;
          else if (acceptWord("transient")) f.flags |= BM_SCRIPT_FIELD_TRANSIENT;
          else if (acceptWord("derived")) f.flags |= BM_SCRIPT_FIELD_TRANSIENT;
          else break;
        }
        if (!parseType(&f.type)) error(peek(), "expected a type (bool, int, fix, name, text, an enum or record<S>)");
        f.typeLabel = tn(f.type);
        Token name = expectIdent("the name of the variable");
        f.name = name.text;
        if (lookupField(f.name) >= 0 || lookupVar(f.name) >= 0 || cls_.consts.count(f.name)) error(name, "'" + f.name + "' exists already");
        if (acceptPunct("=")) f.defaultValue = literal(f.type).value;
        expectPunct(";");
        if (cls_.info.fields.size() >= BM_SCRIPT_MAX_FIELDS)
          error(name, "a class has at most " + std::to_string(BM_SCRIPT_MAX_FIELDS) + " variables in this version");
        cls_.info.fields.push_back(f);
      }
      else if (acceptWord("const"))
      {
        BmScriptType type;
        if (!parseType(&type)) error(peek(), "expected a type");
        Token name = expectIdent("the name of the constant");
        expectPunct("=");
        Value v = literal(type);
        expectPunct(";");
        if (lookupField(name.text) >= 0 || cls_.consts.count(name.text)) error(name, "'" + name.text + "' exists already");
        cls_.consts[name.text] = {type, v.value};
      }
      else if (acceptWord("function"))
      {
        Function f;
        BmScriptType type;
        // "function int f(" has a type; "function f(" has none; "function record<S> f(" too.
        if ((peek(1).kind == Tok::Ident || peek().text == "record") && parseType(&type)) f.result = type;
        Token name = expectIdent("the name of the function");
        f.name = name.text;
        if (cls_.functions.count(f.name) || apiIndex(f.name) >= 0) error(name, "function '" + f.name + "' exists already");
        expectPunct("(");
        while (!isPunct(")"))
        {
          BmScriptType pt;
          if (!parseType(&pt)) error(peek(), "expected the type of a parameter");
          f.params.push_back({expectIdent("the name of the parameter").text, pt});
          if (!acceptPunct(",")) break;
        }
        expectPunct(")");
        if (f.params.size() > BM_SCRIPT_MAX_ARGS) error(name, "a function has at most " + std::to_string(BM_SCRIPT_MAX_ARGS) + " parameters");
        f.bodyToken = pos_;
        skipBlock();
        cls_.functions[f.name] = addFunction(f);
        cls_.info.functions.push_back(f.name);
      }
      else if (acceptWord("event")) eventDecl(-1);
      else if (acceptWord("state"))
      {
        Token name = expectIdent("the name of the state");
        if (stateIndex(name.text) >= 0) error(name, "state '" + name.text + "' is defined twice");
        State s;
        s.name = name.text;
        for (int &e : s.events) e = -1;
        cls_.states.push_back(s);
        cls_.info.states.push_back(name.text);
        int index = int(cls_.states.size()) - 1;
        expectPunct("{");
        while (!isPunct("}"))
        {
          if (peek().kind == Tok::End) error(name, "state '" + name.text + "' has no closing '}'");
          if (acceptWord("event")) eventDecl(index);
          else if (isWord("begin") && isPunct(":", 1))
          {
            pos_ += 2;
            Function f;
            f.name = name.text + " (state code)";
            f.stateCode = true;
            f.state = index;
            f.bodyToken = pos_;
            // The code runs to the '}' that closes the state.
            int depth = 0;
            while (!(isPunct("}") && depth == 0))
            {
              if (peek().kind == Tok::End) error(name, "state '" + name.text + "' has no closing '}'");
              if (isPunct("{")) depth++;
              if (isPunct("}")) depth--;
              pos_++;
            }
            cls_.states[size_t(index)].code = addFunction(f);
          }
          else error(peek(), "a state holds events and, after 'begin:', its code");
        }
        expectPunct("}");
      }
      else if (acceptWord("defaultproperties"))
      {
        expectPunct("{");
        while (!acceptPunct("}"))
        {
          Token name = expectIdent("the name of a property");
          expectPunct("=");
          if (name.text == "start_state")
          {
            Token s = expectIdent("the name of a state");
            cls_.info.startState = s.text;
            cls_.startStateLine = s.line;
          }
          else if (name.text == "sprite")
          {
            if (peek().kind != Tok::String) error(peek(), "sprite = \"name of the sprite\"");
            cls_.info.sprite = next().text;
          }
          else
          {
            int field = lookupField(name.text);
            if (field < 0) error(name, "'" + name.text + "' is not a variable of this class");
            cls_.info.fields[size_t(field)].defaultValue = literal(cls_.info.fields[size_t(field)].type).value;
          }
          expectPunct(";");
        }
      }
      else error(t, "expected var, const, function, event, state or defaultproperties");
    }
    if (!cls_.info.startState.empty())
    {
      cls_.startState = stateIndex(cls_.info.startState);
      if (cls_.startState < 0) throw CompileError{cls_.startStateLine, 1, "start_state: there is no state '" + cls_.info.startState + "'"};
    }
  }

  int lookupField(const std::string &name) const
  {
    for (size_t i = 0; i < cls_.info.fields.size(); i++)
      if (cls_.info.fields[i].name == name) return int(i);
    return -1;
  }
  static int lookupVar(const std::string &name)
  {
    for (int i = 0; i < BM_SCRIPT_VAR_COUNT; i++)
      if (name == kVars[i].name) return i;
    return -1;
  }
  static int apiIndex(const std::string &name)
  {
    for (int i = 0; i < BM_SCRIPT_API_COUNT; i++)
      if (name == kApi[i].name) return i;
    return -1;
  }
  int stateIndex(const std::string &name) const
  {
    for (size_t i = 0; i < cls_.states.size(); i++)
      if (cls_.states[i].name == name) return int(i);
    return -1;
  }

  // --- Code generation ---
  void emit(uint16_t w) { fn_->code.push_back(w); }
  void op(BmVmOp o, int a) { emit(uint16_t(o | (a << 8))); }
  void op2(BmVmOp o, int a, int w1)
  {
    op(o, a);
    emit(uint16_t(w1));
  }
  size_t jump(BmVmOp o, int a)
  {
    op2(o, a, 0);
    return fn_->code.size() - 1;
  }
  void patch(size_t at) { fn_->code[at] = uint16_t(fn_->code.size()); }
  void patchTo(size_t at, size_t target) { fn_->code[at] = uint16_t(target); }
  void markLine(const Token &t)
  {
    if (t.line == lastLine_) return;
    lastLine_ = t.line;
    fn_->lines.push_back({uint16_t(fn_->code.size()), uint16_t(t.line)});
  }
  int temp(const Token &at)
  {
    if (tempTop_ >= BM_SCRIPT_MAX_REGISTERS) error(at, "this function needs too many values at once; split the expression or the function");
    int r = tempTop_++;
    fn_->regCount = std::max(fn_->regCount, tempTop_);
    return r;
  }
  // Puts a value into a register.
  int materialise(Value &v, const Token &at)
  {
    if (!v.isConst) return v.reg;
    int r = temp(at);
    if (v.value >= -32768 && v.value <= 32767) op2(BM_OP_LOADI, r, uint16_t(int16_t(v.value)));
    else
    {
      size_t k = 0;
      for (; k < consts_.size(); k++)
        if (consts_[k] == v.value) break;
      if (k == consts_.size()) consts_.push_back(v.value);
      op2(BM_OP_LOADK, r, int(k));
      fn_->constSites.push_back(fn_->code.size() - 1);
    }
    v.isConst = false;
    v.reg = r;
    return r;
  }
  // An integer literal where fix is wanted becomes fix.
  static void adapt(Value &v, BmScriptType want)
  {
    if (v.isConst && v.literalInt && want == BM_SCRIPT_FIX)
    {
      v.type = BM_SCRIPT_FIX;
      v.value *= 256;
      v.literalInt = false;
    }
    // #ShortSword where a record<S> (or a runtime function's record of any structure) is
    // expected is that record's key.
    if (v.isConst && !v.nameText.empty() && (isRecordType(want) || want == BM_SCRIPT_RECORD))
    {
      v.type = want;
      v.value = int32_t(nameKey(v.nameText));
      v.nameText.clear();
    }
  }
  void expectType(Value &v, BmScriptType want, const Token &at, const char *what)
  {
    adapt(v, want);
    // Runtime functions take records of any structure, and values of any enum.
    if ((want == BM_SCRIPT_RECORD && isRecordType(v.type)) || (want == BM_SCRIPT_ENUM && isEnumType(v.type))) return;
    // A record from a runtime function (of no known structure) goes into any record<S>.
    if (v.type == BM_SCRIPT_RECORD && isRecordType(want)) return;
    if (v.type != want)
      error(at, std::string(what) + " must be " + tn(want) + ", this is " + tn(v.type) +
                    (v.type == BM_SCRIPT_INT && want == BM_SCRIPT_FIX ? " (write fix(...) to convert)"
                     : v.type == BM_SCRIPT_FIX && want == BM_SCRIPT_INT ? " (write int(...) to convert)" : ""));
  }

  bool findLocal(const std::string &name, int *pReg, BmScriptType *pType) const
  {
    for (size_t i = scopes_.size(); i-- > 0;)
    {
      auto it = scopes_[i].find(name);
      if (it != scopes_[i].end())
      {
        *pReg = it->second.first;
        *pType = it->second.second;
        return true;
      }
    }
    return false;
  }

  // --- Expressions ---
  // A primary expression, then field access on records: held.attack, held.bonus.strength,
  // held.combo[1], held.upgrade.attack.
  Value primary()
  {
    Value v = primaryBase();
    while (isRecordType(v.type) && isPunct("."))
    {
      Token dot = next();
      const StructInfo *s = &types_.structs[size_t(int(v.type) - kRecordTypeBase)];
      uint16_t table = stringId_(s->name);
      int offset = 0;
      const StructField *f = nullptr;
      for (;;)
      {
        Token name = expectIdent("the name of a field");
        f = nullptr;
        for (const StructField &c : s->fields)
          if (c.name == name.text) f = &c;
        if (!f) error(name, "'" + s->name + "' has no field '" + name.text + "'");
        offset += f->offset;
        if (acceptPunct("["))
        {
          Token at = peek();
          Value index = expression();
          if (!index.isConst || index.type != BM_SCRIPT_INT) error(at, "the index of a field of a record is a constant number in this version");
          if (index.value < 0 || index.value >= f->count) error(at, "'" + f->name + "' has " + std::to_string(f->count) + " elements");
          offset += index.value * f->elementSize;
          expectPunct("]");
        }
        else if (f->count > 1) error(name, "'" + f->name + "' is an array: write " + f->name + "[index]");
        if (f->type != BM_DATA_STRUCT) break;
        if (!acceptPunct(".")) error(name, "'" + f->name + "' is a structure: name one of its fields (" + f->name + ".field)");
        s = &types_.structs[size_t(types_.findStruct(f->detail))];
      }
      // record_get(table, key, field)
      int first = tempTop_;
      int rTable = temp(dot), rKey = temp(dot), rField = temp(dot);
      Value c;
      c.isConst = true;
      c.type = BM_SCRIPT_INT;
      c.value = table;
      tempTop_ = rTable;
      materialise(c, dot);
      if (v.isConst)
      {
        Value k = v;
        tempTop_ = rKey;
        materialise(k, dot);
      }
      else op2(BM_OP_MOVE, rKey, v.reg);
      c.isConst = true;
      c.value = BM_SCRIPT_RECORD_FIELD(offset, f->type);
      tempTop_ = rField;
      materialise(c, dot);
      tempTop_ = rField + 1;
      Value r;
      r.reg = temp(dot);
      op2(BM_OP_CALLN, r.reg, BM_SCRIPT_API_record_get | (first << 8));
      switch (f->type)
      {
      case BM_DATA_BOOL: r.type = BM_SCRIPT_BOOL; break;
      case BM_DATA_FIX: r.type = BM_SCRIPT_FIX; break;
      case BM_DATA_NAME: case BM_DATA_SPRITE: case BM_DATA_SOUND: case BM_DATA_ATTACH: r.type = BM_SCRIPT_NAME; break;
      case BM_DATA_TEXT: r.type = BM_SCRIPT_TEXT; break;
      case BM_DATA_ENUM: r.type = enumType(size_t(types_.findEnum(f->detail))); break;
      case BM_DATA_RECORD: r.type = recordType(size_t(types_.findStruct(f->detail))); break;
      default: r.type = BM_SCRIPT_INT; break;
      }
      v = r;
    }
    return v;
  }

  Value primaryBase()
  {
    Token t = next();
    Value v;
    if (t.kind == Tok::Int) { v.isConst = true; v.literalInt = true; v.type = BM_SCRIPT_INT; v.value = t.value; return v; }
    if (t.kind == Tok::Fix) { v.isConst = true; v.type = BM_SCRIPT_FIX; v.value = t.value; return v; }
    if (t.kind == Tok::Name) { v.isConst = true; v.type = BM_SCRIPT_NAME; v.value = stringId_(t.text); v.nameText = t.text; return v; }
    if (t.kind == Tok::String) { v.isConst = true; v.type = BM_SCRIPT_TEXT; v.value = stringId_(t.text); return v; }
    if (t.kind == Tok::Punct && t.text == "(")
    {
      v = expression();
      expectPunct(")");
      return v;
    }
    if (t.kind != Tok::Ident) error(t, t.kind == Tok::End ? "the file ends in the middle of an expression" : "unexpected '" + t.text + "'");
    if (t.text == "true" || t.text == "false") { v.isConst = true; v.type = BM_SCRIPT_BOOL; v.value = t.text == "true"; return v; }

    if (isPunct("("))
    {
      pos_++;
      // Conversions.
      if (t.text == "fix" || t.text == "int")
      {
        Value a = expression();
        expectPunct(")");
        bool toFix = t.text == "fix";
        if (a.type != (toFix ? BM_SCRIPT_INT : BM_SCRIPT_FIX) && a.type != (toFix ? BM_SCRIPT_FIX : BM_SCRIPT_INT))
          error(t, t.text + "(...) converts numbers");
        if (a.type == (toFix ? BM_SCRIPT_FIX : BM_SCRIPT_INT)) return a;
        if (a.isConst)
        {
          a.value = toFix ? a.value * 256 : a.value / 256;
          a.type = toFix ? BM_SCRIPT_FIX : BM_SCRIPT_INT;
          a.literalInt = false;
          return a;
        }
        // int(f) truncates toward zero; fix(i) multiplies by 256.
        Value k;
        k.isConst = true;
        k.type = BM_SCRIPT_INT;
        k.value = 256;
        int rk = materialise(k, t), r = temp(t);
        op2(toFix ? BM_OP_MUL : BM_OP_DIV, r, a.reg | (rk << 8));
        v.type = toFix ? BM_SCRIPT_FIX : BM_SCRIPT_INT;
        v.reg = r;
        return v;
      }
      std::vector<Value> args;
      std::vector<Token> argTokens;
      while (!isPunct(")"))
      {
        argTokens.push_back(peek());
        args.push_back(expression());
        if (!acceptPunct(",")) break;
      }
      expectPunct(")");
      int api = apiIndex(t.text);
      auto fnIt = cls_.functions.find(t.text);
      if (api < 0 && fnIt == cls_.functions.end()) error(t, "there is no function '" + t.text + "'");
      int argc = api >= 0 ? kApi[api].argc : int(functions_[size_t(fnIt->second)].params.size());
      if (int(args.size()) != argc) error(t, "'" + t.text + "' takes " + std::to_string(argc) + " values, here are " + std::to_string(args.size()));
      if (api >= 0 && cls_.info.kind != BM_SCRIPT_KIND_ACTOR)
        for (const char *only : kActorOnly)
          if (t.text == only) error(t, "'" + t.text + "' acts on a room's object: an " + cls_.info.base + " cannot use it");
      if (api >= 0 && kApi[api].latent && !(fn_->stateCode))
        error(t, "'" + t.text + "' takes time: it can only be used in the code of a state (after 'begin:')");
      // Arguments go into registers that follow each other.
      int first = tempTop_;
      for (size_t i = 0; i < args.size(); i++)
      {
        BmScriptType want = api >= 0 ? kApi[api].args[i] : functions_[size_t(fnIt->second)].params[i].second;
        expectType(args[i], want, argTokens[i], "this value");
        int r = temp(argTokens[i]);
        if (args[i].isConst)
        {
          Value c = args[i];
          int saved = tempTop_;
          tempTop_ = r;  // load the constant straight into the argument register
          materialise(c, argTokens[i]);
          tempTop_ = saved;
        }
        else op2(BM_OP_MOVE, r, args[i].reg);
      }
      v.type = api >= 0 ? kApi[api].result : functions_[size_t(fnIt->second)].result;
      int result = v.type == BM_SCRIPT_VOID ? 0xFF : temp(t);
      if (api >= 0) op2(BM_OP_CALLN, result, api | (first << 8));
      else
      {
        op2(BM_OP_CALL, result, fnIt->second);
        fn_->callSites.push_back(fn_->code.size() - 1);
        emit(uint16_t(first));
      }
      v.reg = result;
      return v;
    }

    int reg;
    BmScriptType type;
    if (findLocal(t.text, &reg, &type)) { v.type = type; v.reg = reg; return v; }
    if (types_.findEnum(t.text) >= 0 && isPunct(".")) return enumValue(t);
    if (int f = lookupField(t.text); f >= 0)
    {
      v.type = cls_.info.fields[size_t(f)].type;
      v.reg = temp(t);
      op2(BM_OP_GETF, v.reg, f);
      return v;
    }
    if (int var = lookupVar(t.text); var >= 0)
    {
      // In an attachment x, y and facing are its owner's.
      if (cls_.info.kind != BM_SCRIPT_KIND_ACTOR && (var == BM_SCRIPT_VAR_home_x || var == BM_SCRIPT_VAR_home_y))
        error(t, "'" + t.text + "' belongs to a room's object: an " + cls_.info.base + " has none");
      v.type = kVars[var].type;
      v.reg = temp(t);
      op2(BM_OP_GETV, v.reg, var);
      return v;
    }
    if (auto c = cls_.consts.find(t.text); c != cls_.consts.end())
    {
      v.isConst = true;
      v.type = c->second.first;
      v.value = c->second.second;
      return v;
    }
    error(t, "'" + t.text + "' is not known here");
  }

  Value unary()
  {
    Token t = peek();
    if (acceptPunct("-"))
    {
      Value a = unary();
      if (a.type != BM_SCRIPT_INT && a.type != BM_SCRIPT_FIX) error(t, "'-' needs a number");
      if (a.isConst) { a.value = -a.value; return a; }
      Value v;
      v.type = a.type;
      v.reg = temp(t);
      op2(BM_OP_NEG, v.reg, a.reg);
      return v;
    }
    if (acceptPunct("!"))
    {
      Value a = unary();
      if (a.type != BM_SCRIPT_BOOL) error(t, "'!' needs a bool");
      if (a.isConst) { a.value = !a.value; return a; }
      Value v;
      v.type = BM_SCRIPT_BOOL;
      v.reg = temp(t);
      op2(BM_OP_NOT, v.reg, a.reg);
      return v;
    }
    return primary();
  }

  Value binary(int level)
  {
    static const std::vector<std::vector<const char *>> levels = {{"==", "!="}, {"<", "<=", ">", ">="}, {"+", "-"}, {"*", "/", "%"}};
    if (level == int(levels.size())) return unary();
    Value left = binary(level + 1);
    for (;;)
    {
      const char *o = nullptr;
      for (const char *candidate : levels[size_t(level)])
        if (isPunct(candidate)) o = candidate;
      if (!o) return left;
      Token t = next();
      // The left side must be in a register before the right side is compiled.
      Value right = binary(level + 1);
      std::string ops = o;
      bool compare = level < 2;
      // Literals take the other side's type.
      adapt(left, right.type);
      adapt(right, left.type);
      if (left.type != right.type)
        error(t, std::string("'") + o + "' between " + tn(left.type) + " and " + tn(right.type) + ": convert one side with fix(...) or int(...)");
      bool numbers = left.type == BM_SCRIPT_INT || left.type == BM_SCRIPT_FIX;
      if (!numbers && !(level == 0 && left.type != BM_SCRIPT_VOID)) error(t, std::string("'") + o + "' needs numbers");
      if (left.isConst && right.isConst)
      {
        int64_t a = left.value, b = right.value, r = 0;
        bool fix = left.type == BM_SCRIPT_FIX;
        if (ops == "+") r = a + b;
        else if (ops == "-") r = a - b;
        else if (ops == "*") r = fix ? (a * b) >> 8 : a * b;
        else if (ops == "/") r = b == 0 ? 0 : fix ? (a * 256) / b : a / b;
        else if (ops == "%") r = b == 0 ? 0 : a % b;
        else if (ops == "==") r = a == b;
        else if (ops == "!=") r = a != b;
        else if (ops == "<") r = a < b;
        else if (ops == "<=") r = a <= b;
        else if (ops == ">") r = a > b;
        else r = a >= b;
        left.value = int32_t(r);
        left.literalInt = left.literalInt && right.literalInt && !compare;
        if (compare) left.type = BM_SCRIPT_BOOL;
        continue;
      }
      int ra = materialise(left, t), rb = materialise(right, t);
      Value v;
      v.reg = temp(t);
      v.type = compare ? BM_SCRIPT_BOOL : left.type;
      bool fix = left.type == BM_SCRIPT_FIX;
      BmVmOp code;
      bool swap = false;
      if (ops == "+") code = BM_OP_ADD;
      else if (ops == "-") code = BM_OP_SUB;
      else if (ops == "*") code = fix ? BM_OP_FMUL : BM_OP_MUL;
      else if (ops == "/") code = fix ? BM_OP_FDIV : BM_OP_DIV;
      else if (ops == "%") code = BM_OP_MOD;
      else if (ops == "==") code = BM_OP_EQ;
      else if (ops == "!=") code = BM_OP_NE;
      else if (ops == "<") code = BM_OP_LT;
      else if (ops == "<=") code = BM_OP_LE;
      else if (ops == ">") { code = BM_OP_LT; swap = true; }
      else { code = BM_OP_LE; swap = true; }
      op2(code, v.reg, swap ? rb | (ra << 8) : ra | (rb << 8));
      left = v;
    }
  }

  // && and || stop as soon as the result is known.
  Value logical(bool isOr)
  {
    Value left = isOr ? logical(false) : binary(0);
    while (isPunct(isOr ? "||" : "&&"))
    {
      Token t = next();
      if (left.type != BM_SCRIPT_BOOL) error(t, std::string("'") + t.text + "' needs bools");
      int r = temp(t);
      int rl = materialise(left, t);
      op2(BM_OP_MOVE, r, rl);
      size_t skip = jump(isOr ? BM_OP_JNZ : BM_OP_JZ, r);
      Value right = isOr ? logical(false) : binary(0);
      if (right.type != BM_SCRIPT_BOOL) error(t, std::string("'") + t.text + "' needs bools");
      int rr = materialise(right, t);
      op2(BM_OP_MOVE, r, rr);
      patch(skip);
      left = Value{};
      left.type = BM_SCRIPT_BOOL;
      left.reg = r;
    }
    return left;
  }

  Value expression() { return logical(true); }

  Value condition()
  {
    Token t = peek();
    Value c = expression();
    if (c.type != BM_SCRIPT_BOOL) error(t, std::string("a condition must be a bool, this is ") + tn(c.type));
    return c;
  }

  // --- Statements ---
  void block()
  {
    expectPunct("{");
    scopes_.emplace_back();
    int saved = localTop_;
    while (!acceptPunct("}"))
    {
      if (peek().kind == Tok::End) error(peek(), "'{' without '}'");
      statement();
    }
    scopes_.pop_back();
    localTop_ = saved;
    tempTop_ = localTop_;
  }

  int declareLocal(const Token &name, BmScriptType type)
  {
    if (scopes_.back().count(name.text)) error(name, "'" + name.text + "' exists already");
    if (localTop_ >= BM_SCRIPT_MAX_REGISTERS - 4) error(name, "too many local variables");
    int reg = localTop_++;
    tempTop_ = localTop_;
    fn_->regCount = std::max(fn_->regCount, localTop_);
    scopes_.back()[name.text] = {reg, type};
    return reg;
  }

  void store(const Token &target, Value &v)
  {
    int reg;
    BmScriptType type;
    if (findLocal(target.text, &reg, &type))
    {
      expectType(v, type, target, "the value");
      if (v.isConst)
      {
        int saved = tempTop_;
        tempTop_ = reg;
        Value c = v;
        materialise(c, target);
        tempTop_ = saved;
      }
      else if (v.reg != reg) op2(BM_OP_MOVE, reg, v.reg);
      return;
    }
    if (int f = lookupField(target.text); f >= 0)
    {
      expectType(v, cls_.info.fields[size_t(f)].type, target, "the value");
      op2(BM_OP_SETF, materialise(v, target), f);
      return;
    }
    if (int var = lookupVar(target.text); var >= 0)
    {
      if (!kVars[var].writable) error(target, "'" + target.text + "' cannot be changed");
      if (cls_.info.kind != BM_SCRIPT_KIND_ACTOR) error(target, "an " + cls_.info.base + " reads its owner's '" + target.text + "' but cannot change it (use impulse)");
      expectType(v, kVars[var].type, target, "the value");
      op2(BM_OP_SETV, materialise(v, target), var);
      return;
    }
    error(target, cls_.consts.count(target.text) ? "'" + target.text + "' is a constant" : "'" + target.text + "' is not known here");
  }

  // for w in records(Weapon) { ... }: every record of the table, in the order of their keys.
  void forRecords(const Token &name)
  {
    pos_ += 2;
    Token s = expectIdent("the structure of the records");
    int k = types_.findStruct(s.text);
    if (k < 0) error(s, "there is no structure '" + s.text + "'");
    expectPunct(")");
    scopes_.emplace_back();
    int saved = localTop_;
    auto hidden = [&](const char *what) {
      Token h = name;
      h.text = std::string("(") + what + " of " + name.text + ")";
      return declareLocal(h, BM_SCRIPT_INT);
    };
    int table = hidden("table"), index = hidden("index"), limit = hidden("count"), one = hidden("step");
    int var = declareLocal(name, recordType(size_t(k)));
    auto load = [&](int reg, int32_t value) {
      Value c;
      c.isConst = true;
      c.type = BM_SCRIPT_INT;
      c.value = value;
      tempTop_ = reg;
      materialise(c, name);
      tempTop_ = localTop_;
    };
    load(table, stringId_(s.text));
    load(index, 0);
    load(one, 1);
    // record_count(table) and record_at(table, index) take their arguments from consecutive
    // registers: table and index are declared next to each other for that.
    op2(BM_OP_CALLN, limit, BM_SCRIPT_API_record_count | (table << 8));
    size_t top = fn_->code.size();
    int cond = temp(name);
    op2(BM_OP_LT, cond, index | (limit << 8));
    size_t out = jump(BM_OP_JZ, cond);
    tempTop_ = localTop_;
    op2(BM_OP_CALLN, var, BM_SCRIPT_API_record_at | (table << 8));
    loops_.emplace_back();
    block();
    size_t increment = fn_->code.size();
    op2(BM_OP_ADD, index, index | (one << 8));
    op2(BM_OP_JMP, 0, int(top));
    patch(out);
    for (size_t b : loops_.back().breaks) patch(b);
    for (size_t c : loops_.back().continues) patchTo(c, increment);
    loops_.pop_back();
    scopes_.pop_back();
    localTop_ = saved;
    tempTop_ = localTop_;
  }

  void statement()
  {
    tempTop_ = localTop_;
    Token t = peek();
    markLine(t);
    if (isPunct("{")) { block(); return; }
    if (acceptWord("var"))
    {
      if (fn_->stateCode) error(t, "the code of a state cannot have local variables (they would not survive sleep); use a var of the class");
      BmScriptType type;
      if (!parseType(&type)) error(peek(), "expected a type");
      Token name = expectIdent("the name of the variable");
      Value v;
      v.isConst = true;
      v.type = type;
      if (acceptPunct("=")) v = expression();
      expectPunct(";");
      expectType(v, type, name, "the value");
      // Evaluate before the name exists, then give it its register.
      int reg = declareLocal(name, type);
      if (v.isConst)
      {
        tempTop_ = reg;
        materialise(v, name);
        tempTop_ = localTop_;
      }
      else if (v.reg != reg) op2(BM_OP_MOVE, reg, v.reg);
      return;
    }
    if (acceptWord("if"))
    {
      Value c = condition();
      size_t toElse = jump(BM_OP_JZ, materialise(c, t));
      block();
      if (acceptWord("else"))
      {
        size_t toEnd = jump(BM_OP_JMP, 0);
        patch(toElse);
        if (isWord("if")) statement();
        else block();
        patch(toEnd);
      }
      else patch(toElse);
      return;
    }
    if (acceptWord("while") || isWord("loop"))
    {
      bool forever = acceptWord("loop");
      size_t top = fn_->code.size();
      size_t out = 0;
      if (!forever)
      {
        Value c = condition();
        out = jump(BM_OP_JZ, materialise(c, t));
      }
      loops_.emplace_back();
      block();
      op2(BM_OP_JMP, 0, int(top));
      if (!forever) patch(out);
      for (size_t b : loops_.back().breaks) patch(b);
      for (size_t c : loops_.back().continues) patchTo(c, top);
      loops_.pop_back();
      return;
    }
    if (acceptWord("for"))
    {
      if (fn_->stateCode) error(t, "the code of a state cannot use 'for' (its counter would not survive sleep); count in a var of the class");
      Token name = expectIdent("the name of the counter");
      if (!acceptWord("in")) error(peek(), "for x in a..b");
      if (isWord("records") && isPunct("(", 1))
      {
        forRecords(name);
        return;
      }
      scopes_.emplace_back();
      int saved = localTop_;
      Value from = expression();
      expectType(from, BM_SCRIPT_INT, name, "the start");
      int counter = declareLocal(name, BM_SCRIPT_INT);
      if (from.isConst)
      {
        tempTop_ = counter;
        materialise(from, name);
        tempTop_ = localTop_;
      }
      else op2(BM_OP_MOVE, counter, from.reg);
      expectPunct("..");
      Value to = expression();
      expectType(to, BM_SCRIPT_INT, name, "the end");
      Token limitName = name;
      limitName.text = "(end of " + name.text + ")";
      int rt = materialise(to, name);
      int limit = declareLocal(limitName, BM_SCRIPT_INT);
      if (rt != limit) op2(BM_OP_MOVE, limit, rt);
      Value one;
      one.isConst = true;
      one.type = BM_SCRIPT_INT;
      one.value = 1;
      Token oneName = name;
      oneName.text = "(step of " + name.text + ")";
      int step = declareLocal(oneName, BM_SCRIPT_INT);
      tempTop_ = step;
      materialise(one, name);
      tempTop_ = localTop_;

      size_t top = fn_->code.size();
      int cond = temp(name);
      op2(BM_OP_LT, cond, counter | (limit << 8));
      size_t out = jump(BM_OP_JZ, cond);
      loops_.emplace_back();
      block();
      size_t increment = fn_->code.size();
      op2(BM_OP_ADD, counter, counter | (step << 8));
      op2(BM_OP_JMP, 0, int(top));
      patch(out);
      for (size_t b : loops_.back().breaks) patch(b);
      for (size_t c : loops_.back().continues) patchTo(c, increment);
      loops_.pop_back();
      scopes_.pop_back();
      localTop_ = saved;
      tempTop_ = localTop_;
      return;
    }
    if (acceptWord("break") || isWord("continue"))
    {
      bool isBreak = t.text == "break";
      if (!isBreak) pos_++;
      if (loops_.empty()) error(t, "'" + t.text + "' outside a loop");
      size_t j = jump(BM_OP_JMP, 0);
      (isBreak ? loops_.back().breaks : loops_.back().continues).push_back(j);
      expectPunct(";");
      return;
    }
    if (acceptWord("return"))
    {
      if (fn_->stateCode) error(t, "the code of a state does not return; use gotostate");
      if (isPunct(";"))
      {
        if (fn_->result != BM_SCRIPT_VOID) error(t, std::string("this function returns ") + tn(fn_->result));
        op(BM_OP_RET, 0xFF);
      }
      else
      {
        Value v = expression();
        if (fn_->result == BM_SCRIPT_VOID) error(t, "this function returns nothing");
        expectType(v, fn_->result, t, "the result");
        op(BM_OP_RET, materialise(v, t));
      }
      expectPunct(";");
      return;
    }
    if (acceptWord("gotostate"))
    {
      Token name = expectIdent("the name of a state");
      int s = stateIndex(name.text);
      if (s < 0) error(name, "there is no state '" + name.text + "'");
      op(BM_OP_GOTOSTATE, s);
      expectPunct(";");
      return;
    }
    if (isWord("wait_until") && isPunct("(", 1))
    {
      if (!fn_->stateCode) error(t, "'wait_until' takes time: it can only be used in the code of a state (after 'begin:')");
      pos_ += 2;
      size_t top = fn_->code.size();
      Value c = condition();
      expectPunct(")");
      expectPunct(";");
      size_t done = jump(BM_OP_JNZ, materialise(c, t));
      Value one;
      one.isConst = true;
      one.type = BM_SCRIPT_INT;
      one.value = 1;
      int r = materialise(one, t);
      op2(BM_OP_CALLN, 0xFF, BM_SCRIPT_API_sleep | (r << 8));
      op2(BM_OP_JMP, 0, int(top));
      patch(done);
      return;
    }
    // Assignment or a call.
    if (t.kind == Tok::Ident && peek(1).kind == Tok::Punct)
    {
      const std::string o = peek(1).text;
      if (o == "=" || o == "+=" || o == "-=" || o == "*=" || o == "/=")
      {
        Value v;
        if (o == "=")
        {
          pos_ += 2;
          v = expression();
        }
        else
        {
          // a += b is a = a + b: read the target as a value, skip the operator, then the right side.
          Token opTok = peek(1);
          Value left = primary();
          pos_++;
          Value right = expression();
          adapt(left, right.type);
          adapt(right, left.type);
          if (left.type != right.type || (left.type != BM_SCRIPT_INT && left.type != BM_SCRIPT_FIX))
            error(opTok, "'" + opTok.text + "' needs two numbers of the same type");
          int ra = materialise(left, opTok), rb = materialise(right, opTok);
          bool fix = left.type == BM_SCRIPT_FIX;
          const std::string &c = opTok.text;
          BmVmOp code = c == "+=" ? BM_OP_ADD : c == "-=" ? BM_OP_SUB : c == "*=" ? (fix ? BM_OP_FMUL : BM_OP_MUL) : (fix ? BM_OP_FDIV : BM_OP_DIV);
          v.type = left.type;
          v.reg = temp(opTok);
          op2(code, v.reg, ra | (rb << 8));
        }
        expectPunct(";");
        store(t, v);
        return;
      }
    }
    Value v = expression();
    (void)v;
    expectPunct(";");
  }

  void body(Function &f, int index)
  {
    (void)index;
    fn_ = &f;
    pos_ = f.bodyToken;
    scopes_.clear();
    scopes_.emplace_back();
    loops_.clear();
    localTop_ = tempTop_ = 0;
    lastLine_ = 0;
    f.regCount = 0;
    for (const auto &[name, type] : f.params)
    {
      Token t;
      t.text = name;
      declareLocal(t, type);
    }
    if (f.stateCode)
    {
      while (!isPunct("}"))
      {
        if (peek().kind == Tok::End) break;
        statement();
      }
      op(BM_OP_STOP, 0);
    }
    else
    {
      Token open = peek();
      block();
      // Falling off the end returns (a function with a result must have returned before).
      if (f.result != BM_SCRIPT_VOID)
      {
        Value zero;
        zero.isConst = true;
        zero.type = f.result;
        op(BM_OP_RET, materialise(zero, open));
      }
      else op(BM_OP_RET, 0xFF);
    }
    if (f.regCount == 0) f.regCount = 1;
    if (f.code.size() > 512) throw CompileError{tokens_[f.bodyToken].line, 1, "'" + f.name + "' is too long; split it into functions"};
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
void set32(std::vector<uint8_t> &b, size_t at, uint32_t v)
{
  for (int i = 0; i < 4; i++) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

}  // namespace

bool Module::ok() const
{
  for (const Diagnostic &d : diagnostics)
    if (!d.warning) return false;
  return true;
}

const char *typeName(BmScriptType type)
{
  static const char *const names[] = {"nothing", "bool", "int", "fix", "name", "text", "enum", "record"};
  return int(type) >= 0 && int(type) <= BM_SCRIPT_RECORD ? names[type] : "enum";
}

const char *dataTypeName(BmDataType type)
{
  static const char *const names[] = {"bool", "byte", "ubyte", "short", "ushort", "int", "uint", "fix",
                                      "name", "text", "enum", "struct", "sprite", "sound", "record", "attach"};
  return int(type) >= 0 && type < BM_DATA_TYPE_COUNT ? names[type] : "?";
}

uint32_t nameKey(const std::string &name)
{
  uint32_t h = 2166136261u;
  for (unsigned char c : name) h = (h ^ c) * 16777619u;
  return h;
}

bool isKeyword(const std::string &word)
{
  for (const char *k : kKeywords)
    if (word == k) return true;
  return false;
}
bool isTypeName(const std::string &word)
{
  for (const char *k : kTypes)
    if (word == k) return true;
  return false;
}
bool isBuiltin(const std::string &word)
{
  for (const ApiDef &a : kApi)
    if (word == a.name) return true;
  for (const VarDef &v : kVars)
    if (word == v.name) return true;
  for (const char *e : kEvents)
    if (word == e) return true;
  return false;
}

std::vector<std::string> apiReference()
{
  std::vector<std::string> out;
  for (const ApiDef &a : kApi)
  {
    std::string line = std::string(a.result == BM_SCRIPT_VOID ? "" : std::string(typeName(a.result)) + " ") + a.name + "(";
    for (int i = 0; i < a.argc; i++) line += std::string(i ? ", " : "") + typeName(a.args[i]);
    line += ")";
    if (a.latent) line += "  - takes time: only in state code";
    out.push_back(line);
  }
  for (const VarDef &v : kVars) out.push_back(std::string(typeName(v.type)) + " " + v.name + (v.writable ? "" : "  - read only"));
  for (const EventDef &e : kEventDefs)
    out.push_back(std::string("event ") + e.name + "(" + (e.arg == BM_SCRIPT_VOID ? "" : typeName(e.arg)) + ")" +
                  (e.kinds == 1 ? "  - Actor" : e.kinds == 2 ? "  - Attachment" : ""));
  return out;
}

Source readSource(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  return {path, ss.str()};
}

// Writes the given classes as one module (src/script/vm.h): their functions and constants
// renumbered from 0 in that order. Empty, with the reason, if it does not verify.
static std::vector<uint8_t> writeModule(const std::vector<Class> &classes, const std::vector<size_t> &which, const std::vector<Function> &functions,
                                        const std::function<uint16_t(const std::string &)> &stringId, std::string *pWhy)
{
  // Where each chosen class's functions and constants start in this module.
  std::vector<size_t> fnBase(classes.size(), 0), constBase(classes.size(), 0);
  std::vector<int32_t> consts;
  std::vector<const Function *> fns;
  for (size_t k = 0; k < which.size(); k++)
  {
    const Class &c = classes[which[k]];
    fnBase[which[k]] = fns.size();
    constBase[which[k]] = consts.size();
    for (size_t f = 0; f < c.functionCount; f++) fns.push_back(&functions[c.firstFunction + f]);
    consts.insert(consts.end(), c.constPool.begin(), c.constPool.end());
  }
  auto fnIndex = [&](size_t ci, int global) { return uint32_t(int(fnBase[ci]) + global - int(classes[ci].firstFunction)); };
  auto classOf = [&](const Function &f) { return size_t(f.classIndex); };
  auto position = [&](size_t ci) {
    for (size_t k = 0; k < which.size(); k++)
      if (which[k] == ci) return uint32_t(k);
    return 0u;
  };

  std::vector<uint8_t> b(BM_VM_HEADER_SIZE, 0);
  std::memcpy(b.data(), "BMMB", 4);
  auto hdr16 = [&](size_t at, uint32_t v) { b[at] = uint8_t(v); b[at + 1] = uint8_t(v >> 8); };
  hdr16(4, BM_VM_VERSION);
  hdr16(6, BM_SCRIPT_API_COUNT);
  hdr16(8, uint32_t(which.size()));
  hdr16(10, uint32_t(fns.size()));
  hdr16(12, uint32_t(consts.size()));
  hdr16(14, BM_SCRIPT_EVENT_COUNT);

  set32(b, 16, uint32_t(b.size()));
  size_t classTable = b.size();
  b.resize(b.size() + which.size() * BM_VM_CLASS_SIZE, 0);
  for (size_t k = 0; k < which.size(); k++)
  {
    size_t ci = which[k];
    const Class &c = classes[ci];
    size_t at = classTable + k * BM_VM_CLASS_SIZE;
    hdr16(at, stringId(c.info.name));
    hdr16(at + 2, c.info.sprite.empty() ? BM_VM_NONE : stringId(c.info.sprite));
    b[at + 4] = uint8_t(c.info.fields.size());
    b[at + 5] = uint8_t(c.states.size());
    b[at + 6] = uint8_t(c.startState < 0 ? 0xFF : c.startState);
    b[at + 7] = uint8_t(c.info.kind);
    for (int e = 0; e < BM_SCRIPT_EVENT_COUNT; e++) hdr16(at + 8 + size_t(e) * 2, c.events[e] < 0 ? BM_VM_NONE : fnIndex(ci, c.events[e]));
    set32(b, at + BM_VM_CLASS_FIELDS_AT, uint32_t(b.size()));
    for (const FieldInfo &f : c.info.fields)
    {
      put16(b, stringId(f.name));
      b.push_back(uint8_t(moduleType(f.type)));
      b.push_back(f.flags);
      put32(b, uint32_t(f.defaultValue));
    }
    set32(b, at + BM_VM_CLASS_STATES_AT, uint32_t(b.size()));
    for (const State &st : c.states)
    {
      size_t stateAt = b.size();
      put16(b, stringId(st.name));
      put16(b, st.code < 0 ? BM_VM_NONE : fnIndex(ci, st.code));
      for (int e = 0; e < BM_SCRIPT_EVENT_COUNT; e++) put16(b, st.events[e] < 0 ? BM_VM_NONE : fnIndex(ci, st.events[e]));
      b.resize(stateAt + BM_VM_STATE_SIZE, 0);
    }
  }
  set32(b, 20, uint32_t(b.size()));
  uint32_t word = 0;
  for (const Function *f : fns)
  {
    put32(b, word);
    put16(b, uint32_t(f->code.size()));
    b.push_back(uint8_t(f->regCount));
    b.push_back(uint8_t(f->params.size()));
    b.push_back(f->stateCode ? BM_VM_FUNCTION_STATE_CODE : 0);
    b.push_back(0);
    put16(b, position(classOf(*f)));
    word += uint32_t(f->code.size());
  }
  set32(b, 24, uint32_t(b.size()));
  for (int32_t k : consts) put32(b, uint32_t(k));
  set32(b, 28, uint32_t(b.size()));
  set32(b, 32, word);
  for (const Function *f : fns)
  {
    std::vector<uint16_t> code = f->code;
    size_t ci = classOf(*f);
    for (size_t s : f->callSites) code[s] = uint16_t(fnIndex(ci, code[s]));
    for (size_t s : f->constSites) code[s] = uint16_t(code[s] + constBase[ci]);
    for (uint16_t w : code) put16(b, w);
  }
  put16(b, 0);  // the spare word the verifier asks for
  while (b.size() % 4) b.push_back(0);
  // Line table: per function an offset, then file (string id), count and (word, line) pairs.
  set32(b, 36, uint32_t(b.size()));
  size_t lineOffsets = b.size();
  b.resize(b.size() + fns.size() * 4, 0);
  for (size_t i = 0; i < fns.size(); i++)
  {
    set32(b, lineOffsets + i * 4, uint32_t(b.size()));
    put16(b, stringId(classes[classOf(*fns[i])].info.file));
    put16(b, uint32_t(fns[i]->lines.size()));
    for (const auto &[w, line] : fns[i]->lines)
    {
      put16(b, w);
      put16(b, line);
    }
  }

  // The compiler's output must pass the same check the runtime makes.
  BmVmModule check;
  const char *why = nullptr;
  if (!bmVmModuleOpen(&check, b.data(), uint32_t(b.size()), &why))
  {
    if (pWhy) *pWhy = why ? why : "unknown";
    return {};
  }
  return b;
}

Module compile(const std::vector<Source> &sources, const std::function<uint16_t(const std::string &)> &stringId)
{
  Module out;
  std::vector<Function> functions;
  std::vector<Class> classes;
  std::set<std::string> names;

  // First the enums and structures of every file, so that any file can use them.
  std::vector<std::vector<Token>> tokens(sources.size());
  std::vector<bool> readable(sources.size(), false);
  Types types;
  std::vector<RawStruct> raw;
  std::vector<std::string> rawFile;
  for (size_t i = 0; i < sources.size(); i++)
  {
    std::vector<EnumInfo> enums;
    std::vector<RawStruct> structs;
    try
    {
      tokens[i] = lex(sources[i].text);
      DeclParser(sources[i], tokens[i]).run(enums, structs);
      readable[i] = true;
    }
    catch (const CompileError &e)
    {
      out.diagnostics.push_back({sources[i].path, e.line, e.column, e.message, false});
      continue;
    }
    auto taken = [&](const std::string &n) {
      return types.findEnum(n) >= 0 || std::any_of(raw.begin(), raw.end(), [&](const RawStruct &r) { return r.info.name == n; }) ||
             isKeyword(n) || isTypeName(n) || n == "record" || n == "sprite" || n == "sound" || n == "attach";
    };
    for (EnumInfo &e : enums)
    {
      if (taken(e.name)) out.diagnostics.push_back({e.file, e.line, 1, "'" + e.name + "' is declared twice or is a type already", false});
      else types.enums.push_back(e);
    }
    for (RawStruct &r : structs)
    {
      if (taken(r.info.name)) out.diagnostics.push_back({r.info.file, r.info.line, 1, "'" + r.info.name + "' is declared twice or is a type already", false});
      else raw.push_back(r);
    }
  }
  StructLayout layout(types.enums, raw, stringId);
  for (size_t i = 0; i < raw.size(); i++)
  {
    try
    {
      layout.layout(i);
    }
    catch (const CompileError &e)
    {
      out.diagnostics.push_back({raw[i].info.file, e.line, e.column, e.message, false});
      raw[i].state = 2;  // reported once
    }
  }
  for (const Diagnostic &w : layout.warnings) out.diagnostics.push_back(w);
  for (const RawStruct &r : raw) types.structs.push_back(r.info);
  out.enums = types.enums;
  out.structs = types.structs;

  for (size_t si = 0; si < sources.size(); si++)
  {
    if (!readable[si]) continue;
    const Source &src = sources[si];
    size_t functionsBefore = functions.size();
    try
    {
      std::vector<int32_t> classConsts;
      Compiler c(src, tokens[si], types, stringId, functions, classConsts, int(classes.size()));
      Class cls = c.run();
      cls.constPool = classConsts;
      cls.firstFunction = functionsBefore;
      cls.functionCount = functions.size() - functionsBefore;
      if (!c.hasClass()) continue;
      if (!names.insert(cls.info.name).second || types.findEnum(cls.info.name) >= 0 || types.findStruct(cls.info.name) >= 0)
      {
        out.diagnostics.push_back({src.path, 1, 1, "'" + cls.info.name + "' is defined in two files", false});
        functions.resize(functionsBefore);
        continue;
      }
      for (size_t i = functionsBefore; i < functions.size(); i++) cls.info.codeWords += int(functions[i].code.size());
      classes.push_back(cls);
    }
    catch (const CompileError &e)
    {
      out.diagnostics.push_back({src.path, e.line, e.column, e.message, false});
      functions.resize(functionsBefore);
    }
  }
  for (const Class &c : classes) out.classes.push_back(c.info);
  if (!out.ok()) return out;

  // One module with every class (tests, the editor's sizes), and one per class (the pack).
  std::vector<size_t> all;
  for (size_t ci = 0; ci < classes.size(); ci++) all.push_back(ci);
  std::string why;
  out.data = writeModule(classes, all, functions, stringId, &why);
  for (size_t ci = 0; ci < classes.size() && why.empty(); ci++)
    out.classModules.push_back({classes[ci].info.name, writeModule(classes, {ci}, functions, stringId, &why)});
  if (!why.empty())
  {
    out.data.clear();
    out.classModules.clear();
    out.diagnostics.push_back({sources.empty() ? std::string() : sources[0].path, 1, 1, "internal error: the module does not verify: " + why, false});
  }
  return out;
}

}  // namespace bm::script
