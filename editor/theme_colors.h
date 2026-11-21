// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Colours the editor's own controls take from the current theme, so they follow a change of
// theme (classic, light, dark).
#pragma once

#include "ui/theme.h"

namespace bm::editor {

/// True if the theme draws light text on dark panels.
inline bool isDarkTheme(const ose::ui::Theme &theme)
{
  ose::ui::Color c = theme.color("*", "font_color");
  return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z > 0.5f;
}

/// Text colour for a problem.
inline ose::ui::Color errorColor(const ose::ui::Theme &theme)
{
  return theme.color("*", "error");
}

}  // namespace bm::editor
