// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The project file (docs/spec/project-format.md) and the asset catalogue: which rooms belong
// to the project, which assets lie in its asset folders, and which rooms use which asset.
// No UI here, so it can be tested on its own.
#pragma once

#include <string>
#include <vector>

namespace bm::editor {

struct ProjectFile
{
  std::string path;                 // the .bmproj file (absolute)
  std::string name;
  std::vector<std::string> rooms;   // absolute paths, in project order (the first room starts the game)
  std::vector<std::string> assetDirs;  // absolute paths of folders holding assets

  std::string dir() const;
  /// Path as written into the file: relative to the project's folder.
  std::string relative(const std::string &absolute) const;
  std::string absolute(const std::string &relativePath) const;

  /// Throws std::runtime_error with the reason.
  static ProjectFile load(const std::string &path);
  bool save(std::string *pError = nullptr) const;

  bool hasRoom(const std::string &path) const;
  bool addRoom(const std::string &path);       // false if already there
  bool removeRoom(const std::string &path);    // the file stays
};

enum class AssetKind { Room, Tileset, Image, Other };

struct AssetInfo
{
  std::string path;                 // absolute, normalised
  AssetKind kind = AssetKind::Other;
  bool inProject = false;           // rooms: listed in the project
  std::vector<std::string> usedBy;  // room files that reference it
  bool hasOverrides = false;        // a .bmasset next to it
  int width = 0, height = 0;        // images
  std::string problem;              // could not be read, missing, ...
};

struct AssetCatalog
{
  std::vector<AssetInfo> assets;    // sorted: rooms, tilesets, images, others; by path

  /// Looks through the asset folders and the project's rooms. Referenced files outside the
  /// folders are listed too; references to missing files are listed with a problem.
  static AssetCatalog scan(const ProjectFile &project);
  const AssetInfo *find(const std::string &path) const;
};

/// Normalised absolute path for comparing ("a/b/../c" -> "a/c").
std::string normalPath(const std::string &path);

/// Copies a file into `dir`. False with a reason if it exists there already or cannot be copied.
bool importAsset(const std::string &source, const std::string &dir, std::string *pNewPath, std::string *pError);
/// Deletes an asset file and its .bmasset.
bool deleteAsset(const std::string &path, std::string *pError);

struct NewRoom
{
  std::string path;                 // .tmx to write
  std::string roomId, area = "default";
  int screensX = 2, screensY = 1;   // size in GBA screens (240x160), rounded up to 16 px tiles
  std::string tileset;              // image (.png) or tileset (.tsx) for the graphics layers
  int tileSize = 16;
  std::string engineDir;            // folder with blutmond_collision.tsx and blutmond_material.tsx
};

/// Writes a new, empty room in Tiled's format with the layers the cooker expects (background,
/// main, collision, objects with a PlayerStart and a CameraZone).
bool createRoom(const NewRoom &room, std::string *pError);

}  // namespace bm::editor
