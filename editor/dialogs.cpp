// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "dialogs.h"

#include <SDL3/SDL.h>

#include <memory>
#include <mutex>

#include "ui/containers.h"
#include "ui/context.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/widgets.h"

using namespace ose::ui;

namespace bm::editor {

namespace {

// Results of SDL file dialogs arrive on another thread; they are handed over here.
struct PendingDialog
{
  std::function<void(std::vector<std::string>)> done;
  std::vector<std::string> paths;
  bool finished = false;
};
std::mutex g_mutex;
std::vector<std::shared_ptr<PendingDialog>> g_pending;

void SDLCALL dialogDone(void *userdata, const char *const *filelist, int)
{
  auto *p = static_cast<PendingDialog *>(userdata);
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const char *const *f = filelist; f && *f; f++) p->paths.push_back(*f);
  p->finished = true;
}

std::unique_ptr<Window> makeWindow(const std::string &title, float width)
{
  auto w = std::make_unique<Window>();
  w->title = title;
  w->customMinSize = {width, 0};
  return w;
}

}  // namespace

void pollFileDialogs()
{
  std::vector<std::shared_ptr<PendingDialog>> done;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_pending.size();)
    {
      if (g_pending[i]->finished)
      {
        done.push_back(g_pending[i]);
        g_pending.erase(g_pending.begin() + long(i));
      }
      else
        i++;
    }
  }
  for (auto &p : done) p->done(p->paths);
}

void chooseFiles(FormField::Kind kind, const std::string &filter, bool many, std::function<void(std::vector<std::string>)> done)
{
  auto p = std::make_shared<PendingDialog>();
  p->done = std::move(done);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending.push_back(p);
  }
  // The filter strings must live as long as the dialog: keep them in the pending entry's lambda.
  static std::string name, pattern;
  size_t semi = filter.find(';');
  name = semi == std::string::npos ? filter : filter.substr(0, semi);
  pattern = semi == std::string::npos ? "*" : filter.substr(semi + 1);
  static SDL_DialogFileFilter filters[1];
  filters[0] = {name.c_str(), pattern.c_str()};
  int count = filter.empty() ? 0 : 1;
  if (kind == FormField::Kind::Folder) SDL_ShowOpenFolderDialog(dialogDone, p.get(), nullptr, nullptr, many);
  else if (kind == FormField::Kind::SaveFile) SDL_ShowSaveFileDialog(dialogDone, p.get(), nullptr, filters, count, nullptr);
  else SDL_ShowOpenFileDialog(dialogDone, p.get(), nullptr, filters, count, nullptr, many);
}

void openForm(Context &ui, const std::string &title, std::vector<FormField> fields, const std::string &okLabel,
              std::function<bool(const std::vector<std::string> &, std::string *)> onOk)
{
  auto window = makeWindow(title, 520);
  auto *box = window->addChild(std::make_unique<VBox>());
  auto *grid = static_cast<GridContainer *>(box->addChild(std::make_unique<GridContainer>()));
  grid->columns = 2;

  // Each field reads its value back through a getter.
  auto getters = std::make_shared<std::vector<std::function<std::string()>>>();
  for (const FormField &f : fields)
  {
    auto label = std::make_unique<Label>();
    label->text = f.label;
    grid->addChild(std::move(label));
    if (f.kind == FormField::Kind::Choice)
    {
      auto *opt = static_cast<OptionButton *>(grid->addChild(std::make_unique<OptionButton>()));
      opt->setItems(f.choices);
      int index = 0;
      for (size_t i = 0; i < f.choices.size(); i++)
        if (f.choices[i] == f.value) index = int(i);
      if (!f.choices.empty()) opt->select(index, false);
      opt->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
      getters->push_back([opt] { return opt->selectedText(); });
    }
    else if (f.kind == FormField::Kind::Number)
    {
      auto *spin = static_cast<SpinBox *>(grid->addChild(std::make_unique<SpinBox>()));
      spin->minValue = f.minValue;
      spin->maxValue = f.maxValue;
      spin->decimals = 0;
      spin->setValue(std::atof(f.value.c_str()), false);
      getters->push_back([spin] { return std::to_string(int(spin->value())); });
    }
    else
    {
      auto row = std::make_unique<HBox>();
      auto *edit = static_cast<LineEdit *>(row->addChild(std::make_unique<LineEdit>()));
      edit->setText(f.value);
      edit->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
      edit->customMinSize = {300, 0};
      if (f.kind != FormField::Kind::Text)
      {
        auto *browse = static_cast<Button *>(row->addChild(std::make_unique<Button>("...")));
        FormField::Kind kind = f.kind;
        std::string filter = f.filter;
        browse->clicked.connect([edit, kind, filter] {
          chooseFiles(kind, filter, false, [edit](std::vector<std::string> paths) {
            if (!paths.empty()) edit->setText(paths[0]);
          });
        });
      }
      row->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
      grid->addChild(std::move(row));
      getters->push_back([edit] { return edit->text(); });
    }
  }

  auto *error = static_cast<Label *>(box->addChild(std::make_unique<Label>()));
  error->colorOverride = Color{0.8f, 0.1f, 0.1f, 1};
  auto buttons = std::make_unique<HBox>();
  buttons->addChild(std::make_unique<Control>())->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
  auto *ok = static_cast<Button *>(buttons->addChild(std::make_unique<Button>(okLabel)));
  auto *cancel = static_cast<Button *>(buttons->addChild(std::make_unique<Button>("Cancel")));
  box->addChild(std::move(buttons));

  Window *w = window.get();
  Context *ctx = &ui;
  ok->clicked.connect([ctx, w, getters, error, onOk] {
    std::vector<std::string> values;
    for (auto &g : *getters) values.push_back(g());
    std::string why;
    if (onOk(values, &why)) ctx->closePopup(w);
    else error->text = why;
  });
  cancel->clicked.connect([ctx, w] { ctx->closePopup(w); });
  ui.openPopup(std::move(window), {}, PopupPlacement::Center, true);
}

void openConfirm(Context &ui, const std::string &title, const std::string &text, const std::string &okLabel, std::function<void()> onOk)
{
  auto window = makeWindow(title, 460);
  auto *box = window->addChild(std::make_unique<VBox>());
  auto *label = static_cast<Label *>(box->addChild(std::make_unique<Label>()));
  label->text = text;
  label->wrap = true;
  auto buttons = std::make_unique<HBox>();
  buttons->addChild(std::make_unique<Control>())->sizeFlagsH = SizeFlags::Fill | SizeFlags::Expand;
  auto *ok = static_cast<Button *>(buttons->addChild(std::make_unique<Button>(okLabel)));
  auto *cancel = static_cast<Button *>(buttons->addChild(std::make_unique<Button>("Cancel")));
  box->addChild(std::move(buttons));
  Window *w = window.get();
  Context *ctx = &ui;
  ok->clicked.connect([ctx, w, onOk] {
    ctx->closePopup(w);
    onOk();
  });
  cancel->clicked.connect([ctx, w] { ctx->closePopup(w); });
  ui.openPopup(std::move(window), {}, PopupPlacement::Center, true);
}

}  // namespace bm::editor
