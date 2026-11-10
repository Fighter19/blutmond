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
  pRoom->objectCount = bmRead16(p + 26);
  pRoom->tilesChunk = bmRead16(p + 30);
  pRoom->paletteChunk = bmRead16(p + 32);
  pRoom->collisionChunk = bmRead16(p + 34);
  pRoom->objectsChunk = bmRead16(p + 36);

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
    const uint8_t *m = bmPackChunkData(pPack, bmRead16(l + 2), &mapSize);
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

// Writes room cell (x, y) of a layer to its place in the hardware ring map.
static void writeCell(const BmRoomLayer *pLayer, int32_t x, int32_t y)
{
  uint32_t hx = (uint32_t)x & (HW_COLS - 1);
  uint32_t hy = (uint32_t)y & (HW_ROWS - 1);
  uint8_t block = (uint8_t)(FIRST_SCREEN_BLOCK + pLayer->bg * 2 + (hx >= 32 ? 1 : 0));
  uint16_t entry = layerEntry(pLayer, x, y);
  bmMapLoad(block, (uint16_t)(hy * 32 + (hx & 31)), &entry, 1);
}

static void writeColumn(const BmRoomLayer *pLayer, int32_t x)
{
  for (int32_t y = pLayer->windowY; y < pLayer->windowY + WINDOW_ROWS; y++)
  {
    writeCell(pLayer, x, y);
  }
}

static void writeRow(const BmRoomLayer *pLayer, int32_t y)
{
  for (int32_t x = pLayer->windowX; x < pLayer->windowX + WINDOW_COLS; x++)
  {
    writeCell(pLayer, x, y);
  }
}

static int32_t floorDiv8(int32_t v)
{
  return v >= 0 ? v / 8 : -((-v + 7) / 8);
}

static void updateLayer(BmRoomLayer *pLayer, int32_t cameraX, int32_t cameraY)
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
      writeRow(pLayer, y);
    }
    pLayer->loaded = true;
  }
  else
  {
    // Move the window one column/row at a time, writing what comes into view.
    while (pLayer->windowX < wx)
    {
      pLayer->windowX++;
      writeColumn(pLayer, pLayer->windowX + WINDOW_COLS - 1);
    }
    while (pLayer->windowX > wx)
    {
      pLayer->windowX--;
      writeColumn(pLayer, pLayer->windowX);
    }
    while (pLayer->windowY < wy)
    {
      pLayer->windowY++;
      writeRow(pLayer, pLayer->windowY + WINDOW_ROWS - 1);
    }
    while (pLayer->windowY > wy)
    {
      pLayer->windowY--;
      writeRow(pLayer, pLayer->windowY);
    }
  }
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
    updateLayer(&pRoom->layers[i], pRoom->cameraX, pRoom->cameraY);
  }
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
    mask |= (uint8_t)(1u << pLayer->bg);
  }
  bmRoomSetCamera(pRoom, cameraX, cameraY);
  bmVideoSetLayers(mask, true);
  return BM_SUCCESS;
}
