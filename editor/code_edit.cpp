// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "code_edit.h"

#include <algorithm>

#include "ui/canvas.h"
#include "ui/context.h"
#include "ui/font.h"
#include "ui/theme.h"

using namespace ose::ui;
using ose::Vec2;

namespace bm::editor {

namespace {

bool isWordChar(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

const Color kText = parseColor("#101010");
const Color kKeyword = parseColor("#0000a0");
const Color kType = parseColor("#007070");
const Color kBuiltin = parseColor("#7a3e00");
const Color kNumber = parseColor("#a00050");
const Color kString = parseColor("#a04000");
const Color kComment = parseColor("#408040");
const Color kName = parseColor("#6a2c91");

}  // namespace

CodeEdit::CodeEdit()
{
  focusMode = FocusMode::Click;
  clipContents = true;
}

void CodeEdit::focusChanged(bool on)
{
  // Typed characters only arrive while text input is switched on.
  if (Context *ctx = context(); ctx && ctx->textInput) ctx->textInput(on);
  selecting_ = false;
}

float CodeEdit::charWidth() const
{
  const Font &f = theme().monoFont ? *theme().monoFont : font();
  return f.advance('M', fontSize());
}

float CodeEdit::lineHeight() const
{
  const Font &f = theme().monoFont ? *theme().monoFont : font();
  return std::max(1.0f, f.lineHeight(fontSize()) + 1);
}

float CodeEdit::gutterWidth() const
{
  int digits = 1;
  for (size_t n = lines_.size(); n >= 10; n /= 10) digits++;
  return charWidth() * float(std::max(3, digits) + 2);
}

int CodeEdit::visibleLines() const
{
  return std::max(1, int(rect_.h / lineHeight()));
}

std::string CodeEdit::text() const
{
  std::string out;
  for (size_t i = 0; i < lines_.size(); i++)
  {
    out += lines_[i];
    if (i + 1 < lines_.size()) out += '\n';
  }
  return out;
}

void CodeEdit::setText(const std::string &text)
{
  lines_.clear();
  size_t start = 0;
  for (;;)
  {
    size_t nl = text.find('\n', start);
    std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Tabs would not line up with the columns: two spaces each.
    std::string expanded;
    for (char c : line)
    {
      if (c == '\t') expanded += "  ";
      else expanded += c;
    }
    lines_.push_back(expanded);
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  caret_ = anchor_ = Pos{};
  scrollLine_ = 0;
  scrollX_ = 0;
  undo_.clear();
  redo_.clear();
  markers.clear();
}

void CodeEdit::clamp(Pos &p) const
{
  p.line = std::clamp(p.line, 0, int(lines_.size()) - 1);
  p.col = std::clamp(p.col, 0, int(lines_[size_t(p.line)].size()));
}

void CodeEdit::goTo(int line, int column)
{
  Pos p{line - 1, column - 1};
  clamp(p);
  caret_ = anchor_ = p;
  showCaret();
  grabFocus();
}

CodeEdit::Pos CodeEdit::posAt(Vec2 p) const
{
  Pos out;
  out.line = scrollLine_ + int((p.y - rect_.y) / lineHeight());
  out.col = int((p.x - rect_.x - gutterWidth() + scrollX_) / charWidth() + 0.5f);
  clamp(out);
  return out;
}

std::string CodeEdit::selectedText() const
{
  Pos a = std::min(anchor_, caret_), b = std::max(anchor_, caret_);
  if (a.line == b.line) return lines_[size_t(a.line)].substr(size_t(a.col), size_t(b.col - a.col));
  std::string out = lines_[size_t(a.line)].substr(size_t(a.col)) + "\n";
  for (int l = a.line + 1; l < b.line; l++) out += lines_[size_t(l)] + "\n";
  return out + lines_[size_t(b.line)].substr(0, size_t(b.col));
}

void CodeEdit::eraseSelection()
{
  if (!hasSelection()) return;
  Pos a = std::min(anchor_, caret_), b = std::max(anchor_, caret_);
  lines_[size_t(a.line)] = lines_[size_t(a.line)].substr(0, size_t(a.col)) + lines_[size_t(b.line)].substr(size_t(b.col));
  lines_.erase(lines_.begin() + a.line + 1, lines_.begin() + b.line + 1);
  caret_ = anchor_ = a;
}

void CodeEdit::insert(const std::string &s)
{
  eraseSelection();
  std::string &line = lines_[size_t(caret_.line)];
  std::string tail = line.substr(size_t(caret_.col));
  line.erase(size_t(caret_.col));
  size_t start = 0;
  for (;;)
  {
    size_t nl = s.find('\n', start);
    std::string part = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    part.erase(std::remove(part.begin(), part.end(), '\r'), part.end());
    for (size_t t; (t = part.find('\t')) != std::string::npos;) part.replace(t, 1, "  ");
    lines_[size_t(caret_.line)] += part;
    caret_.col = int(lines_[size_t(caret_.line)].size());
    if (nl == std::string::npos) break;
    lines_.insert(lines_.begin() + caret_.line + 1, std::string());
    caret_.line++;
    caret_.col = 0;
    start = nl + 1;
  }
  lines_[size_t(caret_.line)] += tail;
  anchor_ = caret_;
}

void CodeEdit::moveCaret(Pos to, bool extend)
{
  clamp(to);
  caret_ = to;
  if (!extend) anchor_ = caret_;
  showCaret();
}

void CodeEdit::showCaret()
{
  int rows = visibleLines();
  if (caret_.line < scrollLine_) scrollLine_ = caret_.line;
  if (caret_.line >= scrollLine_ + rows) scrollLine_ = caret_.line - rows + 1;
  float x = float(caret_.col) * charWidth(), view = rect_.w - gutterWidth() - charWidth() * 2;
  if (x < scrollX_) scrollX_ = x;
  if (view > 0 && x > scrollX_ + view) scrollX_ = x - view;
  if (Context *ctx = context()) blinkStart_ = ctx->time();
}

// Edits of the same kind made shortly after each other undo together.
void CodeEdit::remember(int kind)
{
  double now = context() ? context()->time() : 0;
  if (!undo_.empty() && kind == lastEditKind_ && kind != 0 && now - lastEditTime_ < 1.0)
  {
    lastEditTime_ = now;
    return;
  }
  undo_.push_back({lines_, caret_});
  if (undo_.size() > 200) undo_.erase(undo_.begin());
  redo_.clear();
  lastEditKind_ = kind;
  lastEditTime_ = now;
}

void CodeEdit::restore(std::vector<Snapshot> &from, std::vector<Snapshot> &to)
{
  if (from.empty()) return;
  to.push_back({lines_, caret_});
  lines_ = from.back().lines;
  caret_ = anchor_ = from.back().caret;
  from.pop_back();
  lastEditKind_ = 0;
  clamp(caret_);
  anchor_ = caret_;
  showCaret();
  changed.emit();
}

void CodeEdit::edited()
{
  showCaret();
  changed.emit();
}

bool CodeEdit::event(const InputEvent &e)
{
  using T = InputEvent::Type;
  Context *ctx = context();
  if (e.type == T::MouseDown && e.button == MouseButton::Left)
  {
    if (!hasFocus()) grabFocus();
    Pos p = posAt(e.pos);
    if (e.clicks >= 2)
    {
      // The word under the pointer.
      const std::string &line = lines_[size_t(p.line)];
      int a = p.col, b = p.col;
      while (a > 0 && isWordChar(line[size_t(a - 1)])) a--;
      while (b < int(line.size()) && isWordChar(line[size_t(b)])) b++;
      anchor_ = {p.line, a};
      caret_ = {p.line, b};
      return true;
    }
    selecting_ = true;
    moveCaret(p, (e.mods & Mod::Shift) != 0);
    return true;
  }
  if (e.type == T::MouseMove && selecting_)
  {
    moveCaret(posAt(e.pos), true);
    return true;
  }
  if (e.type == T::MouseUp && selecting_)
  {
    selecting_ = false;
    return true;
  }
  if (e.type == T::Wheel)
  {
    scrollLine_ = std::clamp(scrollLine_ - int(e.delta.y * 3), 0, std::max(0, int(lines_.size()) - 1));
    return true;
  }
  if (!hasFocus()) return false;
  if (e.type == T::Text)
  {
    remember(1);
    // A closing brace on an otherwise empty line goes one level back.
    std::string &line = lines_[size_t(caret_.line)];
    if (e.text == "}" && !hasSelection() && caret_.col >= 2 && line.find_first_not_of(' ') == std::string::npos)
    {
      line.erase(0, 2);
      caret_.col -= 2;
      anchor_ = caret_;
    }
    insert(e.text);
    edited();
    return true;
  }
  if (e.type != T::KeyDown) return e.type == T::KeyUp;

  bool shift = e.mods & Mod::Shift, ctrl = e.mods & (Mod::Ctrl | Mod::Super);
  const std::string &k = e.key;
  const std::string &line = lines_[size_t(caret_.line)];
  if (k == "left" || k == "right")
  {
    int dir = k == "left" ? -1 : 1;
    Pos p = caret_;
    if (!shift && hasSelection() && !ctrl) p = dir < 0 ? std::min(anchor_, caret_) : std::max(anchor_, caret_);
    else if (ctrl)
    {
      // By word.
      const std::string &l = lines_[size_t(p.line)];
      if (dir < 0)
      {
        while (p.col > 0 && !isWordChar(l[size_t(p.col - 1)])) p.col--;
        while (p.col > 0 && isWordChar(l[size_t(p.col - 1)])) p.col--;
      }
      else
      {
        while (p.col < int(l.size()) && !isWordChar(l[size_t(p.col)])) p.col++;
        while (p.col < int(l.size()) && isWordChar(l[size_t(p.col)])) p.col++;
      }
    }
    else if (dir < 0 && p.col == 0 && p.line > 0) p = {p.line - 1, int(lines_[size_t(p.line - 1)].size())};
    else if (dir > 0 && p.col == int(line.size()) && p.line + 1 < int(lines_.size())) p = {p.line + 1, 0};
    else p.col += dir;
    moveCaret(p, shift);
  }
  else if (k == "up") moveCaret({caret_.line - 1, caret_.col}, shift);
  else if (k == "down") moveCaret({caret_.line + 1, caret_.col}, shift);
  else if (k == "pageup") moveCaret({caret_.line - visibleLines(), caret_.col}, shift);
  else if (k == "pagedown") moveCaret({caret_.line + visibleLines(), caret_.col}, shift);
  else if (k == "home")
  {
    // First to the start of the text on the line, then to column 0.
    size_t first = line.find_first_not_of(' ');
    int indent = first == std::string::npos ? 0 : int(first);
    moveCaret(ctrl ? Pos{0, 0} : Pos{caret_.line, caret_.col == indent ? 0 : indent}, shift);
  }
  else if (k == "end") moveCaret(ctrl ? Pos{int(lines_.size()) - 1, int(lines_.back().size())} : Pos{caret_.line, int(line.size())}, shift);
  else if (k == "backspace" || k == "delete")
  {
    remember(2);
    if (!hasSelection())
    {
      Pos p = caret_;
      if (k == "backspace")
      {
        if (p.col > 0)
        {
          // In the indentation a backspace takes a whole level.
          bool indentOnly = line.find_first_not_of(' ') >= size_t(p.col);
          p.col -= indentOnly && p.col >= 2 && p.col % 2 == 0 ? 2 : 1;
        }
        else if (p.line > 0) p = {p.line - 1, int(lines_[size_t(p.line - 1)].size())};
      }
      else
      {
        if (p.col < int(line.size())) p.col++;
        else if (p.line + 1 < int(lines_.size())) p = {p.line + 1, 0};
      }
      anchor_ = p;
    }
    eraseSelection();
    edited();
  }
  else if (k == "enter" || k == "kp_enter")
  {
    remember(3);
    // The new line starts where this one does, one level deeper after an opening brace.
    size_t first = line.find_first_not_of(' ');
    std::string indent(first == std::string::npos ? line.size() : first, ' ');
    std::string before = line.substr(0, size_t(std::min(caret_.col, int(line.size()))));
    size_t last = before.find_last_not_of(' ');
    if (last != std::string::npos && (before[last] == '{' || before[last] == ':')) indent += "  ";
    if (indent.size() > size_t(caret_.col) && first == std::string::npos) indent.resize(size_t(caret_.col));
    insert("\n" + indent);
    edited();
  }
  else if (k == "tab")
  {
    remember(1);
    insert("  ");
    edited();
  }
  else if (ctrl && k == "a")
  {
    anchor_ = {0, 0};
    caret_ = {int(lines_.size()) - 1, int(lines_.back().size())};
  }
  else if (ctrl && (k == "c" || k == "x"))
  {
    if (hasSelection() && ctx && ctx->setClipboard)
    {
      ctx->setClipboard(selectedText());
      if (k == "x")
      {
        remember(0);
        eraseSelection();
        edited();
      }
    }
  }
  else if (ctrl && k == "v")
  {
    if (ctx && ctx->getClipboard)
    {
      remember(0);
      insert(ctx->getClipboard());
      edited();
    }
  }
  else if (ctrl && k == "z" && !shift) restore(undo_, redo_);
  else if (ctrl && (k == "y" || (k == "z" && shift))) restore(redo_, undo_);
  else if (k == "escape")
  {
    releaseFocus();
  }
  else
  {
    // Function keys and other shortcuts stay with the application; typing stays here.
    return !(ctrl || (k.size() >= 2 && k[0] == 'f' && k[1] >= '0' && k[1] <= '9'));
  }
  return true;
}

void CodeEdit::draw(Canvas &c)
{
  const Font &f = theme().monoFont ? *theme().monoFont : font();
  float px = fontSize(), lh = lineHeight(), cw = charWidth(), gutter = gutterWidth();
  c.rect(rect_, parseColor("#ffffff"));
  c.rect({rect_.x, rect_.y, gutter - cw * 0.5f, rect_.h}, parseColor("#e4e4e4"));
  int rows = visibleLines() + 1;
  scrollLine_ = std::clamp(scrollLine_, 0, std::max(0, int(lines_.size()) - 1));

  // Whether the first visible line starts inside a /* comment */.
  bool inComment = false;
  for (int l = 0; l < scrollLine_; l++)
  {
    const std::string &s = lines_[size_t(l)];
    for (size_t i = 0; i + 1 < s.size(); i++)
    {
      if (!inComment && s[i] == '/' && s[i + 1] == '/') break;
      if (!inComment && s[i] == '/' && s[i + 1] == '*') { inComment = true; i++; }
      else if (inComment && s[i] == '*' && s[i + 1] == '/') { inComment = false; i++; }
    }
  }

  Pos selA = std::min(anchor_, caret_), selB = std::max(anchor_, caret_);
  float textX = rect_.x + gutter - scrollX_;
  for (int row = 0; row < rows; row++)
  {
    int l = scrollLine_ + row;
    if (l >= int(lines_.size())) break;
    const std::string &s = lines_[size_t(l)];
    float y = rect_.y + float(row) * lh;

    const Marker *marker = nullptr;
    for (const Marker &m : markers)
      if (m.line == l + 1) marker = &m;
    if (marker) c.rect({rect_.x, y, gutter - cw * 0.5f, lh}, parseColor("#e06060"));
    c.textIn({rect_.x, y, gutter - cw, lh}, std::to_string(l + 1), f, px, marker ? parseColor("#ffffff") : parseColor("#707070"), Align::End, false);

    c.pushClip({rect_.x + gutter - cw * 0.5f, rect_.y, rect_.w - gutter + cw * 0.5f, rect_.h});
    if (l == caret_.line && hasFocus() && !hasSelection()) c.rect({rect_.x + gutter - cw * 0.5f, y, rect_.w, lh}, parseColor("#f4f4e6"));
    if (hasSelection() && l >= selA.line && l <= selB.line)
    {
      int a = l == selA.line ? selA.col : 0, b = l == selB.line ? selB.col : int(s.size()) + 1;
      c.rect({textX + float(a) * cw, y, float(b - a) * cw, lh}, hasFocus() ? parseColor("#b4c8f0") : parseColor("#d8d8d8"));
    }

    // Colours token by token.
    size_t i = 0;
    while (i < s.size())
    {
      size_t j = i;
      Color colour = kText;
      if (inComment)
      {
        size_t end = s.find("*/", i);
        j = end == std::string::npos ? s.size() : end + 2;
        if (end != std::string::npos) inComment = false;
        colour = kComment;
      }
      else if (s.compare(i, 2, "//") == 0) { j = s.size(); colour = kComment; }
      else if (s.compare(i, 2, "/*") == 0)
      {
        size_t end = s.find("*/", i + 2);
        j = end == std::string::npos ? s.size() : end + 2;
        inComment = end == std::string::npos;
        colour = kComment;
      }
      else if (s[i] == '"')
      {
        j = i + 1;
        while (j < s.size() && s[j] != '"') j += s[j] == '\\' ? 2 : 1;
        j = std::min(s.size(), j + 1);
        colour = kString;
      }
      else if (s[i] == '#' && i + 1 < s.size() && isWordChar(s[i + 1]))
      {
        j = i + 1;
        while (j < s.size() && isWordChar(s[j])) j++;
        colour = kName;
      }
      else if (s[i] >= '0' && s[i] <= '9')
      {
        while (j < s.size() && (isWordChar(s[j]) || (s[j] == '.' && j + 1 < s.size() && s[j + 1] >= '0' && s[j + 1] <= '9'))) j++;
        colour = kNumber;
      }
      else if (isWordChar(s[i]))
      {
        while (j < s.size() && isWordChar(s[j])) j++;
        WordKind kind = classify ? classify(s.substr(i, j - i)) : WordKind::Plain;
        colour = kind == WordKind::Keyword ? kKeyword : kind == WordKind::Type ? kType : kind == WordKind::Builtin ? kBuiltin : kText;
      }
      else if (s[i] == ' ')
      {
        while (j < s.size() && s[j] == ' ') j++;
        i = j;
        continue;
      }
      else j = i + 1;
      c.text({textX + float(i) * cw, y}, std::string_view(s).substr(i, j - i), f, px, colour);
      i = j;
    }
    if (marker)
    {
      // A wavy line under the text of the line.
      size_t first = s.find_first_not_of(' ');
      float x0 = textX + float(first == std::string::npos ? 0 : first) * cw, x1 = textX + float(std::max<size_t>(s.size(), 1)) * cw;
      for (float x = x0; x < x1; x += 4) c.line({x, y + lh - 1}, {x + 2, y + lh - 3}, parseColor("#d02020"), 1), c.line({x + 2, y + lh - 3}, {x + 4, y + lh - 1}, parseColor("#d02020"), 1);
    }
    if (l == caret_.line && hasFocus())
    {
      double t = context() ? context()->time() - blinkStart_ : 0;
      if (int(t * 2) % 2 == 0) c.rect({textX + float(caret_.col) * cw, y, 2, lh}, parseColor("#000000"));
    }
    c.popClip();
  }

  // Where the view is in the file.
  if (int(lines_.size()) > rows - 1)
  {
    float total = float(lines_.size()), top = float(scrollLine_) / total, size = float(rows - 1) / total;
    c.rect({rect_.right() - 6, rect_.y + top * rect_.h, 5, std::max(12.0f, size * rect_.h)}, parseColor("#a0a0a0"));
  }
}

}  // namespace bm::editor
