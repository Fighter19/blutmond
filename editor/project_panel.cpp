// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "project_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>

#include "dialogs.h"
#include "ui/context.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;
using namespace ose::ui;

namespace bm::editor {

namespace {

std::string fileName(const std::string &path)
{
  return fs::path(path).filename().string();
}

const char *kindName(AssetKind k)
{
  switch (k)
  {
  case AssetKind::Room: return "room";
  case AssetKind::Sprite: return "sprite";
  case AssetKind::Script: return "script";
  case AssetKind::Song: return "song";
  case AssetKind::Data: return "records";
  case AssetKind::Font: return "font";
  case AssetKind::Language: return "translation";
  case AssetKind::World: return "world map";
  case AssetKind::Schema: return "save layouts";
  case AssetKind::Tileset: return "tileset";
  case AssetKind::Image: return "image";
  default: return "file";
  }
}

}  // namespace

void ProjectPanel::bind(Control &root, Context &ui)
{
  ui_ = &ui;
  tree_ = root.findAs<Tree>("project_tree");
  info_ = root.findAs<ItemList>("asset_info");
  name_ = root.findAs<Label>("project_name");
  info_->tileSize = 0;
  tree_->multiSelect = false;
  tree_->selectionChanged.connect([this] {
    showInfo();
    std::string path = selectedPath();
    for (size_t i = 0; i < file_.rooms.size(); i++)
      if (file_.rooms[i] == path && roomPicked) roomPicked(int(i));
  });
  tree_->activated.connect([this](TreeItem *) { openSelected(); });
  tree_->selectionChanged.connect([this] {
    std::string path = selectedPath();
    if (fs::path(path).extension() == ".bmsprite" && spritePicked) spritePicked(path);
    if (fs::path(path).extension() == ".ms" && scriptPicked) scriptPicked(path);
    if (fs::path(path).extension() == ".bmsong" && songPicked) songPicked(path);
    if (fs::path(path).extension() == ".bmdata" && dataPicked) dataPicked(path);
  });
  tree_->contextMenu.connect([this](TreeItem *item, ose::Vec2 at) {
    if (item == nullptr || !item->data.has_value()) return;
    tree_->select(item, false, true);
    contextMenu(std::any_cast<std::string>(item->data), at.x, at.y);
  });
}

void ProjectPanel::contextMenu(const std::string &path, float x, float y)
{
  std::vector<MenuItem> items;
  auto item = [](const std::string &label, const std::string &icon, std::function<void()> run) {
    MenuItem m;
    m.label = label;
    m.icon = icon;
    m.run = std::move(run);
    return m;
  };
  std::string ext = fs::path(path).extension().string();
  for (size_t i = 0; i < file_.rooms.size(); i++)
    if (file_.rooms[i] == path && roomShown) items.push_back(item("Show in the Room tab", "map", [this, i] { roomShown(int(i)); }));
  if (ext == ".bmsprite" && spritePicked) items.push_back(item("Edit in the Animation tab", "film", [this, path] { spritePicked(path); }));
  if (ext == ".ms" && scriptPicked) items.push_back(item("Edit in the Script tab", "terminal", [this, path] { scriptPicked(path); }));
  if (ext == ".bmsong" && songPicked) items.push_back(item("Edit in the Sound tab", "music", [this, path] { songPicked(path); }));
  if (ext == ".bmdata" && dataPicked) items.push_back(item("Edit records in the Data tab", "table", [this, path] { dataPicked(path); }));
  items.push_back(item(ext == ".tmx" ? "Open in Tiled" : "Open with the system's program", "external-link", [this] { openSelected(); }));
  items.push_back(MenuItem::sep());
  items.push_back(item("Delete...", "trash", [this] { deleteSelected(); }));
  PopupMenu::open(*ui_, items, ose::ui::Rect{x, y, 0, 0}, PopupPlacement::AtPoint);
}

void ProjectPanel::setProject(ProjectFile file)
{
  file_ = std::move(file);
  refresh();
}

void ProjectPanel::refresh()
{
  catalog_ = AssetCatalog::scan(file_);
  rebuild();
}

void ProjectPanel::rebuild()
{
  std::string keep = selectedPath();
  name_->text = file_.name.empty() ? "Untitled project" : file_.name;
  if (file_.path.empty()) name_->text += " (not saved)";
  tree_->clear();
  TreeItem *rooms = tree_->root()->add("Rooms", "map");
  rooms->selectable = false;
  for (const std::string &r : file_.rooms)
  {
    const AssetInfo *a = catalog_.find(r);
    TreeItem *item = rooms->add(file_.path.empty() ? fileName(r) : file_.relative(r), a && !a->problem.empty() ? "warning" : "map-marker");
    item->data = r;
    item->secondary = a && !a->problem.empty() ? a->problem : std::string();
  }

  struct Group
  {
    const char *title, *icon;
    AssetKind kind;
    TreeItem *item = nullptr;
  };
  Group groups[] = {{"Rooms not in the project", "file-o", AssetKind::Room},
                    {"Sprites", "film", AssetKind::Sprite},
                    {"Scripts", "terminal", AssetKind::Script},
                    {"Songs and effects", "music", AssetKind::Song},
                    {"Records", "list", AssetKind::Data},
                    {"Fonts", "tags", AssetKind::Font},
                    {"Translations", "globe", AssetKind::Language},
                    {"World map", "map", AssetKind::World},
                    {"Tilesets", "th", AssetKind::Tileset},
                    {"Images", "image", AssetKind::Image},
                    {"Other files", "file", AssetKind::Other}};
  for (const AssetInfo &a : catalog_.assets)
  {
    if (a.kind == AssetKind::Room && a.inProject) continue;
    for (Group &g : groups)
    {
      if (g.kind != a.kind) continue;
      if (!g.item)
      {
        g.item = tree_->root()->add(g.title, g.icon);
        g.item->selectable = false;
      }
      std::string icon = !a.problem.empty() ? "warning" : a.usedBy.empty() && a.kind != AssetKind::Room ? "circle-o" : g.icon;
      // Relative to the project, so files of the same name in different folders differ.
      std::string shown = file_.path.empty() ? fileName(a.path) : file_.relative(a.path);
      TreeItem *item = g.item->add(shown, icon);
      item->data = a.path;
      if (!a.problem.empty()) item->secondary = a.problem;
      else if (a.kind == AssetKind::Room) item->secondary = "not in project";
      else
      {
        // Images used only by sprites say so.
        size_t bySprites = 0;
        for (const std::string &u : a.usedBy) bySprites += fs::path(u).extension() == ".bmsprite" || fs::path(u).extension() == ".bmsong";
        size_t byRooms = a.usedBy.size() - bySprites;
        if (a.usedBy.empty()) item->secondary = "unused";
        else if (byRooms) item->secondary = std::to_string(byRooms) + (byRooms == 1 ? " room" : " rooms");
        else item->secondary = "used by " + fileName(a.usedBy[0]) + (bySprites > 1 ? " and others" : "");
      }
      if (a.hasOverrides) item->secondary += ", overrides";
    }
  }

  // Keep the selection where it was.
  std::vector<TreeItem *> stack{tree_->root()};
  while (!stack.empty())
  {
    TreeItem *t = stack.back();
    stack.pop_back();
    if (t->data.has_value() && std::any_cast<std::string>(t->data) == keep) tree_->select(t, false, false);
    for (size_t i = 0; i < t->childCount(); i++) stack.push_back(t->child(i));
  }
  showInfo();
}

std::string ProjectPanel::selectedPath() const
{
  if (!tree_ || tree_->selection().empty() || !tree_->selection()[0]->data.has_value()) return {};
  return std::any_cast<std::string>(tree_->selection()[0]->data);
}

void ProjectPanel::showRoom(int index)
{
  if (index < 0 || size_t(index) >= file_.rooms.size()) return;
  TreeItem *rooms = tree_->root()->childCount() ? tree_->root()->child(0) : nullptr;
  if (rooms && size_t(index) < rooms->childCount()) tree_->select(rooms->child(size_t(index)), false, false);
  showInfo();
}

void ProjectPanel::showInfo()
{
  info_->items.clear();
  std::string path = selectedPath();
  const AssetInfo *a = path.empty() ? nullptr : catalog_.find(path);
  if (!a)
  {
    info_->items.push_back({"Project file: " + (file_.path.empty() ? std::string("none") : file_.path), "file", {}, {}, {}, {}});
    for (const std::string &d : file_.assetDirs) info_->items.push_back({"Asset folder: " + d, "folder", {}, {}, {}, {}});
    return;
  }
  info_->items.push_back({a->path, "file", {}, {}, {}, {}});
  std::string what = kindName(a->kind);
  if (a->width) what += ", " + std::to_string(a->width) + "x" + std::to_string(a->height) + " pixels";
  info_->items.push_back({what, "info", {}, {}, {}, {}});
  if (!a->problem.empty()) info_->items.push_back({a->problem, "warning", {}, {}, {}, {}});
  if (a->kind != AssetKind::Room)
  {
    if (a->usedBy.empty()) info_->items.push_back({"No room of the project uses it", "circle-o", {}, {}, {}, {}});
    for (const std::string &r : a->usedBy) info_->items.push_back({"Used by " + fileName(r), "map-marker", {}, {}, {}, {}});
    if (a->usedBy.size() > 1)
      info_->items.push_back({"Shared: a change affects all " + std::to_string(a->usedBy.size()) + " of them", "warning", {}, {}, {}, {}});
  }
  if (a->hasOverrides) info_->items.push_back({"Has overrides (" + fileName(a->path) + ".bmasset)", "sliders", {}, {}, {}, {}});
}

void ProjectPanel::save()
{
  if (file_.path.empty())
  {
    if (log) log("the project is not saved yet: File > Save project as", false);
    return;
  }
  std::string error;
  if (!file_.save(&error) && log) log(error, true);
}

std::string ProjectPanel::defaultAssetDir() const
{
  std::string sel = selectedPath();
  if (!sel.empty())
    for (const std::string &d : file_.assetDirs)
      if (sel.rfind(d + "/", 0) == 0) return fs::path(sel).parent_path().string();
  if (!file_.assetDirs.empty()) return file_.assetDirs.front();
  return file_.path.empty() ? fs::current_path().string() : file_.dir();
}

void ProjectPanel::newRoom()
{
  std::vector<std::string> tilesets{"(none)"};
  std::string pick;
  for (const AssetInfo &a : catalog_.assets)
    if ((a.kind == AssetKind::Image || a.kind == AssetKind::Tileset) && a.path.find("blutmond_") == std::string::npos)
    {
      tilesets.push_back(a.path);
      if (pick.empty() && !a.usedBy.empty()) pick = a.path;
    }
  std::string dir = defaultAssetDir();
  int n = 1;
  while (fs::exists(fs::path(dir) / ("room" + std::to_string(n) + ".tmx"))) n++;
  std::string id = "room" + std::to_string(n);
  openForm(*ui_, "New room",
           {{"Room id", id, FormField::Kind::Text, {}, 0, 0, {}},
            {"File", (fs::path(dir) / (id + ".tmx")).string(), FormField::Kind::SaveFile, {}, 0, 0, "Tiled room;tmx"},
            {"Area", "default", FormField::Kind::Text, {}, 0, 0, {}},
            {"Width (screens)", "2", FormField::Kind::Number, {}, 1, 64, {}},
            {"Height (screens)", "1", FormField::Kind::Number, {}, 1, 64, {}},
            {"Tiles", pick.empty() ? "(none)" : pick, FormField::Kind::Choice, tilesets, 0, 0, {}},
            {"Tile size", "16", FormField::Kind::Choice, {"8", "16", "32"}, 0, 0, {}}},
           "Create", [this](const std::vector<std::string> &v, std::string *pError) {
             NewRoom r;
             r.roomId = v[0];
             r.path = v[1];
             r.area = v[2].empty() ? "default" : v[2];
             r.screensX = std::atoi(v[3].c_str());
             r.screensY = std::atoi(v[4].c_str());
             r.tileset = v[5] == "(none)" ? std::string() : v[5];
             r.tileSize = std::atoi(v[6].c_str());
             // The project's own copy of the engine tilesets, if it has one.
             r.engineDir = engineDir;
             for (const AssetInfo &a : catalog_.assets)
               if (fs::path(a.path).filename() == "blutmond_collision.tsx")
               {
                 r.engineDir = fs::path(a.path).parent_path().string();
                 break;
               }
             if (fs::path(r.path).extension() != ".tmx") r.path += ".tmx";
             if (!createRoom(r, pError)) return false;
             file_.addRoom(r.path);
             save();
             refresh();
             showRoom(int(file_.rooms.size()) - 1);
             if (log) log("new room " + r.path + " (open it in Tiled to paint it)", false);
             if (roomsChanged) roomsChanged();
             if (roomPicked) roomPicked(int(file_.rooms.size()) - 1);
             return true;
           });
}

void ProjectPanel::addRoom()
{
  std::string sel = selectedPath();
  const AssetInfo *a = sel.empty() ? nullptr : catalog_.find(sel);
  if (a && a->kind == AssetKind::Room && !a->inProject)
  {
    // A room found in the asset folders: add it as it is.
    file_.addRoom(a->path);
    save();
    refresh();
    if (roomsChanged) roomsChanged();
    return;
  }
  chooseFiles(FormField::Kind::OpenFile, "Tiled rooms;tmx", true, [this](std::vector<std::string> paths) {
    bool changed = false;
    for (const std::string &p : paths) changed |= file_.addRoom(p);
    if (!changed) return;
    save();
    refresh();
    if (roomsChanged) roomsChanged();
  });
}

void ProjectPanel::removeRoom()
{
  std::string sel = selectedPath();
  if (!file_.hasRoom(sel))
  {
    if (log) log("select a room of the project to remove it", false);
    return;
  }
  openConfirm(*ui_, "Remove room",
              "Remove " + fileName(sel) + " from the project? The file stays where it is; doors that lead to it will warn when cooking.",
              "Remove", [this, sel] {
                file_.removeRoom(sel);
                save();
                refresh();
                if (roomsChanged) roomsChanged();
              });
}

void ProjectPanel::importAssets()
{
  chooseFiles(FormField::Kind::OpenFile, "", true, [this](std::vector<std::string> paths) { importFiles(paths); });
}

void ProjectPanel::importFiles(const std::vector<std::string> &paths)
{
  std::string dir = defaultAssetDir();
  bool roomsAdded = false;
  for (const std::string &p : paths)
  {
    std::string target = normalPath(p), error;
    bool inside = false;
    for (const std::string &d : file_.assetDirs) inside |= target.rfind(d + "/", 0) == 0;
    if (!inside && !importAsset(p, dir, &target, &error))
    {
      if (log) log("import: " + error, true);
      continue;
    }
    if (log) log((inside ? "already in the project: " : "imported ") + target, false);
    if (fs::path(target).extension() == ".tmx") roomsAdded |= file_.addRoom(target);
  }
  if (roomsAdded) save();
  refresh();
  if (assetsChanged) assetsChanged();
  if (roomsAdded && roomsChanged) roomsChanged();
}

void ProjectPanel::openSelected()
{
  std::string sel = selectedPath();
  if (sel.empty()) return;
  // In the program the system uses for the file (Tiled for rooms, an image editor for images).
  if (!SDL_OpenURL(("file://" + sel).c_str()) && log) log(std::string("cannot open ") + sel + ": " + SDL_GetError(), true);
}

void ProjectPanel::deleteSelected()
{
  std::string sel = selectedPath();
  const AssetInfo *a = sel.empty() ? nullptr : catalog_.find(sel);
  if (!a) return;
  if (a->kind == AssetKind::Room && a->inProject)
  {
    if (log) log("remove the room from the project first", false);
    return;
  }
  if (!a->usedBy.empty())
  {
    std::string rooms;
    for (const std::string &r : a->usedBy) rooms += (rooms.empty() ? "" : ", ") + fileName(r);
    if (log) log(fileName(sel) + " is used by " + rooms + "; remove it there first (in Tiled)", true);
    return;
  }
  openConfirm(*ui_, "Delete asset", "Delete " + sel + " from the disk? This cannot be undone.", "Delete", [this, sel] {
    std::string error;
    if (!deleteAsset(sel, &error) && log) log(error, true);
    else if (log) log("deleted " + sel, false);
    refresh();
    if (assetsChanged) assetsChanged();
  });
}

void ProjectPanel::newSprite()
{
  std::vector<std::string> images;
  std::string pick = selectedPath();
  for (const AssetInfo &a : catalog_.assets)
    if (a.kind == AssetKind::Image && a.path.find("blutmond_") == std::string::npos) images.push_back(a.path);
  if (images.empty())
  {
    if (log) log("import the sprite's image first (Import, or drop the file onto the editor)", false);
    return;
  }
  if (std::find(images.begin(), images.end(), pick) == images.end()) pick = images.front();
  openForm(*ui_, "New sprite",
           {{"Image", pick, FormField::Kind::Choice, images, 0, 0, {}},
            {"Frame width", "16", FormField::Kind::Number, {}, 8, 128, {}},
            {"Frame height", "16", FormField::Kind::Number, {}, 8, 128, {}}},
           "Create", [this](const std::vector<std::string> &v, std::string *pError) {
             std::string path;
             if (!createSprite(v[0], std::atoi(v[1].c_str()), std::atoi(v[2].c_str()), &path, pError)) return false;
             refresh();
             if (log) log("new sprite " + path, false);
             if (assetsChanged) assetsChanged();
             if (spritePicked) spritePicked(path);
             return true;
           });
}

void ProjectPanel::saveProjectAs()
{
  std::string suggestion = file_.path.empty() ? (fs::path(defaultAssetDir()).parent_path() / "game.bmproj").string() : file_.path;
  openForm(*ui_, "Save project as",
           {{"Name", file_.name.empty() ? "Game" : file_.name, FormField::Kind::Text, {}, 0, 0, {}},
            {"File", suggestion, FormField::Kind::SaveFile, {}, 0, 0, "Blutmond project;bmproj"}},
           "Save", [this](const std::vector<std::string> &v, std::string *pError) {
             ProjectFile f = file_;
             f.name = v[0];
             f.path = normalPath(v[1]);
             if (fs::path(f.path).extension() != ".bmproj") f.path += ".bmproj";
             if (f.assetDirs.empty())
               for (const std::string &r : f.rooms)
               {
                 std::string d = fs::path(r).parent_path().string();
                 if (std::find(f.assetDirs.begin(), f.assetDirs.end(), d) == f.assetDirs.end()) f.assetDirs.push_back(d);
               }
             if (!f.save(pError)) return false;
             file_ = f;
             refresh();
             if (log) log("saved " + file_.path, false);
             return true;
           });
}

void ProjectPanel::openProject()
{
  chooseFiles(FormField::Kind::OpenFile, "Blutmond project;bmproj", false, [this](std::vector<std::string> paths) {
    if (paths.empty()) return;
    try
    {
      setProject(ProjectFile::load(paths[0]));
      if (roomsChanged) roomsChanged();
    }
    catch (const std::exception &e)
    {
      if (log) log(e.what(), true);
    }
  });
}

}  // namespace bm::editor
