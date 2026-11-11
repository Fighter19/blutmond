// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Project panel: the project's rooms and the assets in its folders, which rooms use which
// asset, and the actions on them: new room, add and remove rooms, import, open and delete
// assets. Changes to the room list are saved to the project file right away.
#pragma once

#include <functional>
#include <string>

#include "project_file.h"

namespace ose::ui {
class Context;
class Control;
class ItemList;
class Label;
class Tree;
class TreeItem;
}  // namespace ose::ui

namespace bm::editor {

class ProjectPanel
{
public:
  /// The room list changed (added, removed, reordered): the editor reloads its rooms.
  std::function<void()> roomsChanged;
  /// A project room was picked (index into file().rooms).
  std::function<void(int)> roomPicked;
  std::function<void(const std::string &line, bool error)> log;
  /// Where the engine's collision tilesets are (for new rooms).
  std::string engineDir;

  void bind(ose::ui::Control &root, ose::ui::Context &ui);
  void setProject(ProjectFile file);
  const ProjectFile &file() const { return file_; }
  const AssetCatalog &catalog() const { return catalog_; }
  /// Scans the folders again and rebuilds the tree.
  void refresh();
  void showRoom(int index);

  // Actions (menu and buttons).
  void newRoom();
  void addRoom();
  void removeRoom();
  void importAssets();
  void importFiles(const std::vector<std::string> &paths);
  void openSelected();
  void deleteSelected();
  void saveProjectAs();
  void openProject();

private:
  void rebuild();
  void showInfo();
  void save();
  std::string selectedPath() const;
  std::string defaultAssetDir() const;

  ose::ui::Context *ui_ = nullptr;
  ose::ui::Tree *tree_ = nullptr;
  ose::ui::ItemList *info_ = nullptr;
  ose::ui::Label *name_ = nullptr;
  ProjectFile file_;
  AssetCatalog catalog_;
};

}  // namespace bm::editor
