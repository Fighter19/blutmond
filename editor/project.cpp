// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "project.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include "device.h"
#include "hal_host.h"
#include "pack.h"
#include "physical_device.h"
#include "room.h"
#include "video.h"
}

namespace bm::editor {

uint32_t toRgba(uint16_t c)
{
  auto x = [](unsigned v) { return (v << 3) | (v >> 2); };
  return x(c & 31) | (x((c >> 5) & 31) << 8) | (x((c >> 10) & 31) << 16) | 0xFF000000u;
}

namespace {

void putPixel(Rgba &img, int x, int y, uint32_t rgba)
{
  if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
  std::memcpy(&img.pixels[(size_t(y) * img.width + x) * 4], &rgba, 4);
}

// The runtime needs a device once per process.
void ensureRuntime()
{
  static bool ready = false;
  if (ready) return;
  BmDeviceHandle device = nullptr;
  bmCreateDevice(bmGetPhysicalDevice(), &device);
  g_defaultDevice = device;
  ready = true;
}

std::string stringAt(const BmPack &pack, uint16_t id)
{
  int32_t idx = bmPackFindChunk(&pack, BM_CHUNK_STRS, 0);
  uint32_t size = 0;
  const uint8_t *p = idx >= 0 ? bmPackChunkData(&pack, uint32_t(idx), &size) : nullptr;
  if (!p || id >= bmRead16(p)) return {};
  uint32_t off = bmRead32(p + 4 + id * 4u);
  return off < size ? std::string(reinterpret_cast<const char *>(p + off)) : std::string();
}

}  // namespace

void Project::setRooms(std::vector<std::string> paths)
{
  rooms_ = std::move(paths);
  current_ = rooms_.empty() ? -1 : 0;
}

void Project::selectRoom(int index)
{
  if (index >= 0 && index < int(rooms_.size())) current_ = index;
}

void Project::setProfile(const std::string &name)
{
  profile_ = name;
}

bool Project::changedOnDisk() const
{
  if (current_ < 0) return false;
  std::error_code ec;
  auto t = std::filesystem::last_write_time(rooms_[size_t(current_)], ec);
  return !ec && t != cookedTime_;
}

bool Project::cook()
{
  error_.clear();
  report_ = Report{};
  pack_.clear();
  roomImage_ = tileSheet_ = Rgba{};
  palette_.assign(256, 0);
  collision_.clear();
  objects_.clear();
  hardware_ = HardwareUse{};
  if (current_ < 0)
  {
    error_ = "no room";
    return false;
  }
  const std::string &path = rooms_[size_t(current_)];
  std::error_code ec;
  cookedTime_ = std::filesystem::last_write_time(path, ec);

  auto profile = profileByName(profile_);
  if (!profile)
  {
    error_ = "unknown profile " + profile_;
    return false;
  }
  try
  {
    std::vector<tmx::Map> maps{tmx::load(path)};
    CookOptions options;
    options.profile = *profile;
    info_ = CookInfo{};
    options.pInfo = &info_;
    pack_ = bm::cook(maps, options, report_);
  }
  catch (const std::exception &e)
  {
    error_ = e.what();
    return false;
  }
  if (pack_.empty()) return false;
  hardware_.packBytes = int(pack_.size());
  hardware_.paletteBanksMax = profile->bgPaletteBanks;
  hardware_.layersMax = profile->maxLayers4bpp;
  hardware_.tileBytesMax = profile->maxTiles * 32;
  analyse();
  return true;
}

bool Project::cookLevel(const std::string &path, std::string *pError) const
{
  try
  {
    std::vector<tmx::Map> maps;
    for (const std::string &r : rooms_) maps.push_back(tmx::load(r));
    CookOptions options;
    options.profile = *profileByName("gba");
    Report report;
    std::vector<uint8_t> pack = bm::cook(maps, options, report);
    for (const auto &e : report.entries)
      if (e.level == ReportEntry::Level::Error && pError) *pError = e.kind + ": " + e.message;
    if (pack.empty()) return false;
    FILE *f = std::fopen(path.c_str(), "wb");
    bool ok = f && std::fwrite(pack.data(), 1, pack.size(), f) == pack.size();
    if (f) std::fclose(f);
    if (!ok && pError) *pError = "cannot write " + path;
    return ok;
  }
  catch (const std::exception &e)
  {
    if (pError) *pError = e.what();
    return false;
  }
}

void Project::analyse()
{
  BmPack pack;
  if (bmPackOpen(&pack, pack_.data(), uint32_t(pack_.size()), true) != BM_PACK_OK) return;
  BmRoom room;
  if (bmRoomOpen(&room, &pack, 0) != BM_SUCCESS) return;

  // Palette and tiles straight from the chunks.
  uint32_t size = 0;
  const uint8_t *pal = bmPackChunkData(&pack, room.paletteChunk, &size);
  if (pal && size >= 4)
  {
    usedBanks_ = pal[2];
    hardware_.paletteBanks = pal[2];
    for (uint32_t i = 0; i < pal[2] * 16u && 4 + i * 2 + 1 < size; i++) palette_[pal[1] * 16u + i] = bmRead16(pal + 4 + i * 2);
  }
  const uint8_t *tiles = bmPackChunkData(&pack, room.tilesChunk, &size);
  bool gbaTiles = tiles && size >= 4 && tiles[3] == 0;
  if (tiles && size >= 4)
  {
    int count = bmRead16(tiles);
    hardware_.tiles = count;
    hardware_.tileBytes = count * 32;
  }
  hardware_.layers = room.layerCount;
  hardware_.mapBytes = room.layerCount * 2 * 2048;
  hardware_.mapBytesMax = 6 * 2048;

  // Collision and objects.
  const uint8_t *coll = bmPackChunkData(&pack, room.collisionChunk, &size);
  if (coll && size >= 8)
  {
    collisionW_ = bmRead16(coll);
    collisionH_ = bmRead16(coll + 2);
    cellSize_ = coll[4];
    collision_.resize(size_t(collisionW_) * collisionH_);
    for (size_t i = 0; i < collision_.size() && 8 + i * 2 + 1 < size; i++)
    {
      uint8_t b0 = coll[8 + i * 2], b1 = coll[8 + i * 2 + 1];
      collision_[i] = {uint8_t(b0 & 15), uint8_t((b0 >> 6) & 1), uint8_t((b0 >> 7) & 1), uint8_t(b1 & 15)};
    }
  }
  const uint8_t *objs = bmPackChunkData(&pack, room.objectsChunk, &size);
  if (objs && size >= 4)
  {
    uint16_t count = bmRead16(objs);
    for (uint16_t i = 0; i < count && 4 + (i + 1u) * 20 <= size; i++)
    {
      const uint8_t *o = objs + 4 + i * 20;
      ObjectMarker m;
      m.type = stringAt(pack, bmRead16(o));
      m.x = int16_t(bmRead16(o + 4));
      m.y = int16_t(bmRead16(o + 6));
      m.w = bmRead16(o + 8);
      m.h = bmRead16(o + 10);
      m.name = stringAt(pack, bmRead16(o + 12));
      objects_.push_back(m);
    }
  }

  if (!gbaTiles) return;  // pictures need the GBA tile format

  // Tile sheet: 16 tiles per row, each in the bank its map entries use (bank 0 if unused).
  int count = bmRead16(tiles);
  std::vector<int> bankOf(size_t(count), 0);
  for (uint8_t l = 0; l < room.layerCount; l++)
    for (uint32_t i = 0; i < uint32_t(room.layers[l].width8) * room.layers[l].height8; i++)
    {
      uint16_t e = bmRead16(room.layers[l].pEntries + i * 2);
      if ((e & 0x3FF) < count) bankOf[e & 0x3FF] = e >> 12;
    }
  tileSheet_.width = 16 * 8;
  tileSheet_.height = ((count + 15) / 16) * 8;
  tileSheet_.pixels.assign(size_t(tileSheet_.width) * tileSheet_.height * 4, 0);
  for (int t = 0; t < count; t++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
      {
        uint8_t b = tiles[4 + t * 32 + y * 4 + x / 2];
        uint8_t idx = (x & 1) ? b >> 4 : b & 15;
        uint32_t rgba = idx ? toRgba(palette_[size_t(bankOf[size_t(t)] * 16 + idx)]) : 0x00000000u;
        putPixel(tileSheet_, (t % 16) * 8 + x, (t / 16) * 8 + y, rgba);
      }

  // Whole room with parallax 1, rendered screen by screen with the runtime.
  ensureRuntime();
  bmHalHostReset();
  bmVideoInit();
  for (uint8_t l = 0; l < room.layerCount; l++) room.layers[l].parallaxX = room.layers[l].parallaxY = 256;
  if (bmRoomShow(&room, 0, 0) != BM_SUCCESS) return;
  roomImage_.width = room.widthPx;
  roomImage_.height = room.heightPx;
  roomImage_.pixels.assign(size_t(room.widthPx) * room.heightPx * 4, 0);
  static uint16_t screen[240 * 160];
  for (int cy = 0; cy < room.heightPx; cy += 160)
    for (int cx = 0; cx < room.widthPx; cx += 240)
    {
      bmRoomSetCamera(&room, cx, cy);
      bmHalHostRender(screen);
      for (int y = 0; y < 160; y++)
        for (int x = 0; x < 240; x++) putPixel(roomImage_, room.cameraX + x, room.cameraY + y, toRgba(screen[y * 240 + x]));
    }
}

Rgba Project::renderScreen(int cameraX, int cameraY, int *pClampedX, int *pClampedY)
{
  Rgba out;
  if (pack_.empty() || roomImage_.width == 0) return out;
  BmPack pack;
  BmRoom room;
  if (bmPackOpen(&pack, pack_.data(), uint32_t(pack_.size()), false) != BM_PACK_OK || bmRoomOpen(&room, &pack, 0) != BM_SUCCESS)
    return out;
  ensureRuntime();
  bmHalHostReset();
  bmVideoInit();
  if (bmRoomShow(&room, cameraX, cameraY) != BM_SUCCESS) return out;
  if (pClampedX) *pClampedX = room.cameraX;
  if (pClampedY) *pClampedY = room.cameraY;
  static uint16_t screen[240 * 160];
  bmHalHostRender(screen);
  out.width = 240;
  out.height = 160;
  out.pixels.resize(240 * 160 * 4);
  for (int i = 0; i < 240 * 160; i++)
  {
    uint32_t c = toRgba(screen[i]);
    std::memcpy(&out.pixels[size_t(i) * 4], &c, 4);
  }
  return out;
}

}  // namespace bm::editor
