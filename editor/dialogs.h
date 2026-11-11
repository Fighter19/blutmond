// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Small modal dialogs on ose-ui: a form of labelled fields and a yes/no question. File fields
// have a "..." button that opens the system's file dialog (SDL3) where there is one.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ose::ui {
class Context;
}

namespace bm::editor {

struct FormField
{
  enum class Kind { Text, Number, Choice, OpenFile, SaveFile, Folder };
  std::string label;
  std::string value;               // text, number, file path or the chosen item
  Kind kind = Kind::Text;
  std::vector<std::string> choices;
  double minValue = 0, maxValue = 1e6;
  std::string filter;              // file dialogs: "Tiled rooms;tmx" (name;extensions)
};

/// `onOk` gets the values in field order; it returns false with a reason to keep the dialog open.
void openForm(ose::ui::Context &ui, const std::string &title, std::vector<FormField> fields, const std::string &okLabel,
              std::function<bool(const std::vector<std::string> &values, std::string *pError)> onOk);

void openConfirm(ose::ui::Context &ui, const std::string &title, const std::string &text, const std::string &okLabel,
                 std::function<void()> onOk);

/// Delivers results of system file dialogs; call once per frame on the UI thread.
void pollFileDialogs();

/// Opens the system file dialog; `done` gets the chosen paths (empty if cancelled).
void chooseFiles(FormField::Kind kind, const std::string &filter, bool many, std::function<void(std::vector<std::string>)> done);

}  // namespace bm::editor
