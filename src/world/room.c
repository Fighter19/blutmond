/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "room.h"

#include "hal.h"
#include "video.h"

// Hardware map per layer: 64x32 entries (512x256 pixels) in two screen blocks.
#define HW_COLS 64
#define HW_ROWS 32
// Cells that must be present to cover the screen at any sub-tile scroll position.
#define WINDOW_COLS 32
#define WINDOW_ROWS 22
// Screen blocks 26-31 (the top of char block 3) hold the maps; tiles use char blocks 0-1.
#define FIRST_SCREEN_BLOCK 26

#define ROOM_HEADER_SIZE 44
#define ROOM_LAYER_SIZE 12

uint32_t bmRoomCount(const BmPack *pPack)
{
  uint32_t count = 0;
  for (int32_t i = bmPackFindChunk(pPack, BM_CHUNK_ROOM, 0); i >= 0; i = bmPackFindChunk(pPack, BM_CHUNK_ROOM, (uint32_t)i + 1))
  {
    count++;
  }
  return count;
}

BmResult bmRoomOpen(BmRoom *pRoom, const BmPack *pPack, uint32_t index)
{
  static uint32_t s_unpacked[BM_PACK_ROOM_UNPACK_BYTES / 4] BM_EWRAM_BSS;
  return bmRoomOpenWith(pRoom, pPack, index, (uint8_t *)s_unpacked, sizeof(s_unpacked));
}

// A chunk of the room: in place, or unpacked at the next free place of the buffer.
static const uint8_t *roomChunk(const BmPack *pPack, uint32_t index, uint8_t *pBuffer, uint32_t size, uint32_t *pUsed, uint32_t *pSize)
{
  const uint8_t *p = bmPackChunkUnpack(pPack, index, pBuffer + *pUsed, size - *pUsed, pSize);
  if (p == pBuffer + *pUsed) *pUsed += (*pSize + 3u) & ~3u;
  return p;
}

BmResult bmRoomOpenWith(BmRoom *pRoom, const BmPack *pPack, uint32_t index, uint8_t *pBuffer, uint32_t bufferSize)
{
  uint32_t used = 0;
  int32_t chunk = -1;
  for (uint32_t n = 0; n <= index; n++)
  {
    chunk = bmPackFindChunk(pPack, BM_CHUNK_ROOM, (uint32_t)(chunk + 1));
    if (chunk < 0)
    {
      return BM_ERROR_INVALID_ARGUMENT;
    }
  }
  uint32_t size = 0;
  const uint8_t *p = bmPackChunkData(pPack, (uint32_t)chunk, &size);
  if (p == NULL || size < ROOM_HEADER_SIZE || p[5] > BM_ROOM_MAX_LAYERS || size < ROOM_HEADER_SIZE + (uint32_t)p[5] * ROOM_LAYER_SIZE)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  *pRoom = (BmRoom){0};
  pRoom->pPack = pPack;
  pRoom->widthPx = bmRead16(p + 0);
  pRoom->heightPx = bmRead16(p + 2);
  pRoom->collisionCell = p[4];
  pRoom->layerCount = p[5];
  pRoom->flags = bmRead16(p + 6);
  pRoom->gravity = (int32_t)bmRead32(p + 8);
  pRoom->maxFallSpeed = (int32_t)bmRead32(p + 12);
  pRoom->waterGravity = (int32_t)bmRead32(p + 16);
  pRoom->waterMaxFallSpeed = (int32_t)bmRead32(p + 20);
  pRoom->musicId = bmRead16(p + 24);
  pRoom->objectCount = bmRead16(p + 26);
  pRoom->tilesChunk = bmRead16(p + 30);
  pRoom->paletteChunk = bmRead16(p + 32);
  pRoom->collisionChunk = bmRead16(p + 34);
  pRoom->objectsChunk = bmRead16(p + 36);
  pRoom->liteChunk = bmRead16(p + 42);
  pRoom->pCollision = roomChunk(pPack, pRoom->collisionChunk, pBuffer, bufferSize, &used, &pRoom->collisionSize);
  if (pRoom->pCollision == NULL)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }

  for (uint8_t i = 0; i < pRoom->layerCount; i++)
  {
    const uint8_t *l = p + ROOM_HEADER_SIZE + i * ROOM_LAYER_SIZE;
    BmRoomLayer *pLayer = &pRoom->layers[i];
    pLayer->role = l[0];
    pLayer->priority = l[1] & 3;
    pLayer->bg = i;
    pLayer->parallaxX = (int32_t)bmRead32(l + 4);
    pLayer->parallaxY = (int32_t)bmRead32(l + 8);

    uint32_t mapSize = 0;
    const uint8_t *m = roomChunk(pPack, bmRead16(l + 2), pBuffer, bufferSize, &used, &mapSize);
    if (m == NULL || mapSize < 8)
    {
      return BM_ERROR_INVALID_ARGUMENT;
    }
    pLayer->width8 = bmRead16(m + 0);
    pLayer->height8 = bmRead16(m + 2);
    if (mapSize < 8u + (uint32_t)pLayer->width8 * pLayer->height8 * 2u)
    {
      return BM_ERROR_INVALID_ARGUMENT;
    }
    pLayer->pEntries = m + 8;
  }
  return BM_SUCCESS;
}

static uint16_t layerEntry(const BmRoomLayer *pLayer, int32_t x, int32_t y)
{
  if (x < 0 || y < 0 || x >= pLayer->width8 || y >= pLayer->height8)
  {
    return 0;
  }
  return bmRead16(pLayer->pEntries + ((uint32_t)y * pLayer->width8 + (uint32_t)x) * 2u);
}

// Whether map cell (x, y) of a layer lies in one of the room's holes (only the main and the
// foreground layer have them).
static bool inHole(const BmRoom *pRoom, const BmRoomLayer *pLayer, int32_t x, int32_t y)
{
  if (pLayer->role == BM_ROOM_ROLE_BACKGROUND) return false;
  for (uint8_t k = 0; k < pRoom->holeCount; k++)
  {
    const int16_t *h = pRoom->holes[k];
    if (x >= h[0] && x < h[2] && y >= h[1] && y < h[3]) return true;
  }
  return false;
}

// Whether row y of a layer crosses a hole.
static bool rowHoled(const BmRoom *pRoom, const BmRoomLayer *pLayer, int32_t y)
{
  if (pLayer->role == BM_ROOM_ROLE_BACKGROUND) return false;
  for (uint8_t k = 0; k < pRoom->holeCount; k++)
    if (y >= pRoom->holes[k][1] && y < pRoom->holes[k][3]) return true;
  return false;
}

// Writes room cell (x, y) of a layer to its place in the hardware ring map.
static void writeCell(const BmRoom *pRoom, const BmRoomLayer *pLayer, int32_t x, int32_t y)
{
  uint32_t hx = (uint32_t)x & (HW_COLS - 1);
  uint32_t hy = (uint32_t)y & (HW_ROWS - 1);
  uint8_t block = (uint8_t)(FIRST_SCREEN_BLOCK + pLayer->bg * 2 + (hx >= 32 ? 1 : 0));
  uint16_t entry = inHole(pRoom, pLayer, x, y) ? 0 : layerEntry(pLayer, x, y);
  bmMapLoad(block, (uint16_t)(hy * 32 + (hx & 31)), &entry, 1);
}

static void writeColumn(const BmRoom *pRoom, const BmRoomLayer *pLayer, int32_t x)
{
  for (int32_t y = pLayer->windowY; y < pLayer->windowY + WINDOW_ROWS; y++)
  {
    writeCell(pRoom, pLayer, x, y);
  }
}

// A row in runs: the cells that lie side by side in one screen block go in one write (on the
// GBA a map write has a cost of its own; a room shown anew writes 66 rows).
static void writeRow(const BmRoom *pRoom, const BmRoomLayer *pLayer, int32_t y)
{
  bool holed = rowHoled(pRoom, pLayer, y);
  uint32_t hy = (uint32_t)y & (HW_ROWS - 1);
  int32_t x = pLayer->windowX, end = pLayer->windowX + WINDOW_COLS;
  // The room's row found once (as layerEntry; cell by cell this was most of a room's showing).
  const uint8_t *row = y >= 0 && y < pLayer->height8 ? pLayer->pEntries + (uint32_t)y * pLayer->width8 * 2u : NULL;
  while (x < end)
  {
    uint32_t hx = (uint32_t)x & (HW_COLS - 1);
    int32_t run = 32 - (int32_t)(hx & 31);
    if (run > end - x) run = end - x;
    uint8_t block = (uint8_t)(FIRST_SCREEN_BLOCK + pLayer->bg * 2 + (hx >= 32 ? 1 : 0));
    // All in the room: straight from the pack (bmHalVramWrite copies whatever the alignment).
    uint16_t entries[32];
    const uint16_t *pFrom = (const uint16_t *)(const void *)(row + x * 2);
    if (row == NULL || x < 0 || x + run > pLayer->width8 || holed)
    {
      for (int32_t k = 0; k < run; k++)
        entries[k] = row != NULL && x + k >= 0 && x + k < pLayer->width8 && !(holed && inHole(pRoom, pLayer, x + k, y)) ? bmRead16(row + (x + k) * 2) : 0;
      pFrom = entries;
    }
    bmMapLoad(block, (uint16_t)(hy * 32 + (hx & 31)), pFrom, (uint32_t)run);
    x += run;
  }
}

static int32_t floorDiv8(int32_t v)
{
  return v >= 0 ? v / 8 : -((-v + 7) / 8);
}

static void updateLayer(const BmRoom *pRoom, BmRoomLayer *pLayer, int32_t cameraX, int32_t cameraY)
{
  // Layer scroll from the camera and the parallax factor (fix 24.8).
  int32_t lx = (int32_t)(((int64_t)cameraX * pLayer->parallaxX) >> 8);
  int32_t ly = (int32_t)(((int64_t)cameraY * pLayer->parallaxY) >> 8);
  int32_t wx = floorDiv8(lx);
  int32_t wy = floorDiv8(ly);

  int32_t dx = wx - pLayer->windowX;
  int32_t dy = wy - pLayer->windowY;
  if (!pLayer->loaded || dx >= WINDOW_COLS || dx <= -WINDOW_COLS || dy >= WINDOW_ROWS || dy <= -WINDOW_ROWS)
  {
    pLayer->windowX = wx;
    pLayer->windowY = wy;
    for (int32_t y = wy; y < wy + WINDOW_ROWS; y++)
    {
      writeRow(pRoom, pLayer, y);
    }
    pLayer->loaded = true;
  }
  else
  {
    // Move the window one column/row at a time, writing what comes into view.
    while (pLayer->windowX < wx)
    {
      pLayer->windowX++;
      writeColumn(pRoom, pLayer, pLayer->windowX + WINDOW_COLS - 1);
    }
    while (pLayer->windowX > wx)
    {
      pLayer->windowX--;
      writeColumn(pRoom, pLayer, pLayer->windowX);
    }
    while (pLayer->windowY < wy)
    {
      pLayer->windowY++;
      writeRow(pRoom, pLayer, pLayer->windowY + WINDOW_ROWS - 1);
    }
    while (pLayer->windowY > wy)
    {
      pLayer->windowY--;
      writeRow(pRoom, pLayer, pLayer->windowY);
    }
  }
  pLayer->scrollX = (int16_t)(lx & 511);
  bmBgSetScroll(pLayer->bg, (int16_t)(lx & 511), (int16_t)(ly & 255));
}

static int32_t clampCamera(int32_t v, int32_t roomSize, int32_t screenSize)
{
  int32_t max = roomSize - screenSize;
  if (max < 0) max = 0;
  return v < 0 ? 0 : (v > max ? max : v);
}

void bmRoomSetCamera(BmRoom *pRoom, int32_t cameraX, int32_t cameraY)
{
  pRoom->cameraX = clampCamera(cameraX, pRoom->widthPx, BM_HAL_SCREEN_WIDTH);
  pRoom->cameraY = clampCamera(cameraY, pRoom->heightPx, BM_HAL_SCREEN_HEIGHT);
  for (uint8_t i = 0; i < pRoom->layerCount; i++)
  {
    updateLayer(pRoom, &pRoom->layers[i], pRoom->cameraX, pRoom->cameraY);
  }
}

bool bmRoomAddHole(BmRoom *pRoom, int32_t x, int32_t y, int32_t w, int32_t h)
{
  if (pRoom->holeCount >= BM_ROOM_MAX_HOLES || w <= 0 || h <= 0) return false;
  int16_t *hole = pRoom->holes[pRoom->holeCount++];
  hole[0] = (int16_t)floorDiv8(x);
  hole[1] = (int16_t)floorDiv8(y);
  hole[2] = (int16_t)floorDiv8(x + w + 7);
  hole[3] = (int16_t)floorDiv8(y + h + 7);
  // What is on screen of it now: those cells written again.
  for (uint8_t i = 0; i < pRoom->layerCount; i++)
  {
    const BmRoomLayer *pLayer = &pRoom->layers[i];
    if (!pLayer->loaded || pLayer->role == BM_ROOM_ROLE_BACKGROUND) continue;
    for (int32_t cy = hole[1]; cy < hole[3]; cy++)
      for (int32_t cx = hole[0]; cx < hole[2]; cx++)
        if (cx >= pLayer->windowX && cx < pLayer->windowX + WINDOW_COLS && cy >= pLayer->windowY && cy < pLayer->windowY + WINDOW_ROWS)
          writeCell(pRoom, pLayer, cx, cy);
  }
  return true;
}

BmResult bmRoomShow(BmRoom *pRoom, int32_t cameraX, int32_t cameraY)
{
  // Palettes
  uint32_t size = 0;
  const uint8_t *pal = bmPackChunkData(pRoom->pPack, pRoom->paletteChunk, &size);
  if (pal == NULL || size < 4 || size < 4u + pal[2] * 32u)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0; i < pal[2] * 16u; i++)
  {
    BmColor c = { bmRead16(pal + 4 + i * 2) };
    bmPaletteLoadBg((uint16_t)(pal[1] * 16u + i), &c, 1);
  }

  // Tiles (4 bpp) into char block 0 and following.
  const uint8_t *tiles = bmPackChunkData(pRoom->pPack, pRoom->tilesChunk, &size);
  if (tiles == NULL || size < 4 || tiles[2] != 4)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  uint32_t tileBytes = bmRead16(tiles) * 32u;
  if (size < 4 + tileBytes || bmTilesLoadBg(0, 0, tiles + 4, tileBytes) != BM_SUCCESS)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  uint8_t mask = 0;
  for (uint8_t i = 0; i < pRoom->layerCount; i++)
  {
    BmRoomLayer *pLayer = &pRoom->layers[i];
    bmBgSetup(pLayer->bg, &(BmBgInfo){
      .charBlock = 0,
      .screenBlock = (uint8_t)(FIRST_SCREEN_BLOCK + pLayer->bg * 2),
      .size = BM_BG_SIZE_512x256,
      .priority = pLayer->priority,
    });
    pLayer->loaded = false;
    // The light layer shows only while the light blends it (light.h).
    if (pLayer->role != BM_ROOM_ROLE_LIGHT) mask |= (uint8_t)(1u << pLayer->bg);
  }
  bmRoomSetCamera(pRoom, cameraX, cameraY);
  bmVideoSetLayers(mask, true);
  return BM_SUCCESS;
}
