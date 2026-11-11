// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The editor's model: rooms (Tiled files), cooking them for a profile, and the pictures and
// numbers the views show. Rendering uses the real runtime (room loader, software PPU).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cook.h"

namespace bm::editor {

struct Rgba
{
  int width = 0, height = 0;
  std::vector<uint8_t> pixels;  // RGBA8
};

struct CollisionCell
{
  uint8_t shape = 0, solidTop = 0, solidSides = 0, material = 0;
};

struct ObjectMarker
{
  std::string type, name;
  int x = 0, y = 0, w = 0, h = 0;
};

struct HardwareUse
{
  int tiles = 0, tileBytes = 0, tileBytesMax = 0;       // background tiles in VRAM
  int paletteBanks = 0, paletteBanksMax = 0;
  int mapBytes = 0, mapBytesMax = 0;                    // hardware maps (screen blocks)
  int layers = 0, layersMax = 0;
  int packBytes = 0;
};

class Project
{
public:
  void setRooms(std::vector<std::string> paths);
  const std::vector<std::string> &rooms() const { return rooms_; }
  int currentRoom() const { return current_; }
  void selectRoom(int index);

  void setProfile(const std::string &name);
  const std::string &profile() const { return profile_; }

  /// Cooks the current room for the profile and, for GBA-like profiles, renders it with the
  /// runtime. Returns false if cooking failed (the report says why).
  bool cook();
  /// Cooks all rooms into one pack for running (GBA format: the runtime on the PC and on the
  /// GBA uses it) and writes it to `path`. False with `*pError` set if that fails.
  bool cookLevel(const std::string &path, std::string *pError) const;
  /// True if a room file changed on disk since the last cook (for hot reload).
  bool changedOnDisk() const;

  const Report &report() const { return report_; }
  const std::string &error() const { return error_; }

  // Results of the last successful cook (empty for profiles the runtime does not run).
  const Rgba &roomImage() const { return roomImage_; }     // whole room, parallax 1
  const Rgba &tileSheet() const { return tileSheet_; }     // unique tiles, 16 per row
  const std::vector<uint16_t> &palette() const { return palette_; }  // 256 BG colours (BGR555)
  int usedBanks() const { return usedBanks_; }
  const std::vector<CollisionCell> &collision() const { return collision_; }
  int collisionWidth() const { return collisionW_; }
  int collisionHeight() const { return collisionH_; }
  int cellSize() const { return cellSize_; }
  const std::vector<ObjectMarker> &objects() const { return objects_; }
  const HardwareUse &hardware() const { return hardware_; }

  /// The GBA screen with the camera at (x, y), with real parallax. Clamped like the runtime.
  Rgba renderScreen(int cameraX, int cameraY, int *pClampedX = nullptr, int *pClampedY = nullptr);

private:
  void analyse();

  std::vector<std::string> rooms_;
  int current_ = -1;
  std::string profile_ = "gba";
  std::filesystem::file_time_type cookedTime_{};
  Report report_;
  std::string error_;
  std::vector<uint8_t> pack_;
  Rgba roomImage_, tileSheet_;
  std::vector<uint16_t> palette_;
  int usedBanks_ = 0;
  std::vector<CollisionCell> collision_;
  int collisionW_ = 0, collisionH_ = 0, cellSize_ = 16;
  std::vector<ObjectMarker> objects_;
  HardwareUse hardware_;
};

/// BGR555 to RGBA8, expanded the way mGBA does.
uint32_t toRgba(uint16_t bgr555);

}  // namespace bm::editor
