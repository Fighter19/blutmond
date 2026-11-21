// SPDX-License-Identifier: BSD-2-Clause OR MIT
// A small code editor on ose-ui (which has only a one-line text field): lines in a monospace
// font with a line-number gutter, caret and selection by keyboard and mouse, clipboard, undo,
// automatic indentation, syntax colours and error markers.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/control.h"
#include "ui/core.h"

namespace bm::editor {

class CodeEdit : public ose::ui::Control
{
public:
  CodeEdit();
  const char *typeName() const override { return "CodeEdit"; }
  ose::Vec2 computeMinSize() override { return {200, 100}; }
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;
  ose::ui::Cursor cursor(ose::Vec2) const override { return ose::ui::Cursor::IBeam; }
  void focusChanged(bool on) override;

  std::string text() const;
  /// Replaces the text (undo history starts over).
  void setText(const std::string &text);
  /// Puts the caret on a line and column (from 1) and scrolls there.
  void goTo(int line, int column);
  int caretLine() const { return caret_.line + 1; }

  enum class WordKind { Plain, Keyword, Type, Builtin };
  /// How a word is coloured.
  std::function<WordKind(const std::string &)> classify;
  /// Lines (from 1) with a problem, underlined and marked in the gutter.
  struct Marker
  {
    int line = 0;
    std::string message;
  };
  std::vector<Marker> markers;
  ose::ui::Signal<> changed;   // every edit

private:
  struct Pos
  {
    int line = 0, col = 0;
    bool operator==(const Pos &o) const { return line == o.line && col == o.col; }
    bool operator<(const Pos &o) const { return line != o.line ? line < o.line : col < o.col; }
  };
  struct Snapshot
  {
    std::vector<std::string> lines;
    Pos caret;
  };

  float charWidth() const;
  float lineHeight() const;
  float gutterWidth() const;
  int visibleLines() const;
  Pos posAt(ose::Vec2 p) const;
  void clamp(Pos &p) const;
  bool hasSelection() const { return !(anchor_ == caret_); }
  std::string selectedText() const;
  void eraseSelection();
  void insert(const std::string &s);
  void moveCaret(Pos to, bool extend);
  void showCaret();
  void remember(int kind);
  void restore(std::vector<Snapshot> &from, std::vector<Snapshot> &to);
  void edited();

  std::vector<std::string> lines_{std::string()};
  Pos caret_, anchor_;
  int scrollLine_ = 0;
  float scrollX_ = 0;
  bool selecting_ = false;
  std::vector<Snapshot> undo_, redo_;
  int lastEditKind_ = 0;
  double lastEditTime_ = 0, blinkStart_ = 0;
};

}  // namespace bm::editor
