/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "map.h"

#include <string.h>

#include "game.h"

// WRLD (docs/spec/world-format.md §3): a header of 20 bytes, rooms of 12, then a byte per cell.
static int16_t rd16s(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static const uint8_t *roomEntry(const BmMap *m, int i)
{
  return m->pChunk + BM_MAP_HEADER + i * BM_MAP_ROOM_SIZE;
}

static void findChunk(BmGame *g)
{
  BmMap *m = &g->map;
  if (m->pFrom == g->pPack->pData && m->fromCrc == g->pPack->header.crc32) return;
  m->pFrom = g->pPack->pData;
  m->fromCrc = g->pPack->header.crc32;
  m->pChunk = NULL;
  m->roomCount = m->cellCount = 0;
  m->room = -1;
  m->shownCell = -1;
  int32_t c = bmPackFindChunk(g->pPack, BM_CHUNK_WRLD, 0);
  if (c < 0) return;
  uint32_t size = 0;
  const uint8_t *p = bmPackChunkData(g->pPack, (uint32_t)c, &size);
  if (p == NULL || size < BM_MAP_HEADER || p[0] != 1) return;
  uint16_t rooms = bmRead16(p + 2), cells = bmRead16(p + 16);
  if (rooms > BM_MAP_MAX_ROOMS || cells > BM_MAP_MAX_CELLS || size < (uint32_t)(BM_MAP_HEADER + rooms * BM_MAP_ROOM_SIZE + cells)) return;
  // Every room's cells lie inside the cell table.
  for (uint16_t i = 0; i < rooms; i++)
  {
    const uint8_t *e = p + BM_MAP_HEADER + i * BM_MAP_ROOM_SIZE;
    if (bmRead16(e + 8) + e[6] * e[7] > cells) return;
  }
  m->pChunk = p;
  m->roomCount = rooms;
  m->cellCount = cells;
}

void bmMapEnterRoom(BmGame *g)
{
  BmMap *m = &g->map;
  findChunk(g);
  m->room = -1;
  m->shownCell = -1;
  m->settle = 1;
  for (uint16_t i = 0; i < m->roomCount; i++)
    if (bmRead16(roomEntry(m, i)) == g->roomIndex) m->room = (int16_t)i;
}

// The cell of the current room the player is in (the map's numbering), -1 if none; and where
// on the map it is.
static int32_t playerCell(const BmGame *g, int16_t *pX, int16_t *pY)
{
  const BmMap *m = &g->map;
  if (m->pChunk == NULL || m->room < 0) return -1;
  const uint8_t *e = roomEntry(m, m->room);
  int32_t w = e[6], h = e[7];
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y) - 1;   // the feet stand on the cell below
  int32_t cx = px <= 0 || g->room.widthPx == 0 ? 0 : px * w / g->room.widthPx;
  int32_t cy = py <= 0 || g->room.heightPx == 0 ? 0 : py * h / g->room.heightPx;
  if (cx >= w) cx = w - 1;
  if (cy >= h) cy = h - 1;
  if (pX) *pX = (int16_t)(rd16s(e + 2) + cx);
  if (pY) *pY = (int16_t)(rd16s(e + 4) + cy);
  return bmRead16(e + 8) + cy * w + cx;
}

bool bmMapExplored(const BmGame *g, uint16_t cell)
{
  return cell < BM_MAP_MAX_CELLS && (g->mapExplored[cell / 8] >> (cell % 8)) & 1;
}

void bmMapFrame(BmGame *g)
{
  findChunk(g);
  if (g->map.pChunk == NULL) return;
  if (g->map.room < 0 && g->map.roomCount != 0) bmMapEnterRoom(g);
  int32_t cell = playerCell(g, NULL, NULL);
  if (cell >= 0 && !bmMapExplored(g, (uint16_t)cell)) g->mapExplored[cell / 8] |= (uint8_t)(1u << (cell % 8));
}

static bool revealed(const BmGame *g, int i)
{
  return (g->mapRevealed[i / 8] >> (i % 8)) & 1;
}

// Draws the map's cells that fall into the w x h cells from (col, row), the map cell (x0, y0)
// at (col, row). The rest stays as it is.
static void drawCells(BmGame *g, int col, int row, int w, int h, int x0, int y0, int markX, int markY)
{
  const BmMap *m = &g->map;
  for (uint16_t i = 0; i < m->roomCount; i++)
  {
    const uint8_t *e = roomEntry(m, i);
    int rx = rd16s(e + 2), ry = rd16s(e + 4), rw = e[6], rh = e[7];
    uint16_t first = bmRead16(e + 8);
    bool shown = revealed(g, i);
    int ax = rx > x0 ? rx : x0, ay = ry > y0 ? ry : y0;
    int bx = rx + rw < x0 + w ? rx + rw : x0 + w, by = ry + rh < y0 + h ? ry + rh : y0 + h;
    const uint8_t *cells = m->pChunk + BM_MAP_HEADER + m->roomCount * BM_MAP_ROOM_SIZE + first;
    for (int y = ay; y < by; y++)
      for (int x = ax; x < bx; x++)
      {
        int k = (y - ry) * rw + (x - rx);
        uint16_t look = cells[k];
        if (bmMapExplored(g, (uint16_t)(first + k))) look |= (uint16_t)((i == m->room ? BM_UI_MAP_HERE_KIND : BM_UI_MAP_ROOM_KIND) << 8);
        else if (shown) look |= BM_UI_MAP_REVEALED_KIND << 8;
        else continue;
        if (x == markX && y == markY) look |= BM_UI_MAP_MARKER_BIT;
        bmUiMapCell(&g->ui, col + x - x0, row + y - y0, look);
      }
  }
}

// The first map cell shown across `w` cells so that `at` is in the middle, or the whole map
// (from `min`, `size` cells) in the middle if it fits.
static int viewStart(int at, int w, int min, int size)
{
  if (size <= w) return min - (w - size) / 2;
  int s = at - w / 2;
  if (s < min) s = min;
  if (s > min + size - w) s = min + size - w;
  return s;
}

void bmMapDraw(BmGame *g, int col, int row, int w, int h)
{
  findChunk(g);
  if (!g->ui.ready || w <= 0 || h <= 0) return;
  bmUiFillRect(&g->ui, col, row, w, h);
  const BmMap *m = &g->map;
  if (m->pChunk == NULL) return;
  int16_t px = 0, py = 0;
  bool here = playerCell(g, &px, &py) >= 0;
  int x0 = viewStart(px, w, rd16s(m->pChunk + 8), bmRead16(m->pChunk + 12));
  int y0 = viewStart(py, h, rd16s(m->pChunk + 10), bmRead16(m->pChunk + 14));
  bool blink = here && (g->frame / BM_MAP_BLINK) % 2 == 0;
  drawCells(g, col, row, w, h, x0, y0, blink ? px : -1000, blink ? py : -1000);
}

// The HUD's map: 5 x 3 cells inside a frame at the top right, the player in the middle.
#define HUD_COL 23
#define HUD_W 5
#define HUD_H 3
#define HUD_NONE 0x7FFF   // shownCell: the player is in a room that is not on the map

void bmMapHud(BmGame *g, bool force)
{
  BmMap *m = &g->map;
  if (m->pChunk == NULL || !(m->pChunk[1] & 1) || !g->ui.ready) return;
  // Not in the frame a room was entered: loading it was work enough (on the GBA).
  if (m->settle != 0)
  {
    m->settle--;
    m->shownCell = -1;
    return;
  }
  int16_t px = 0, py = 0;
  int32_t cell = playerCell(g, &px, &py);
  if (cell < 0)
  {
    // A room that is not on the map: no small map there.
    if (force || m->shownCell != HUD_NONE) bmUiClearRect(&g->ui, HUD_COL, 0, HUD_W + 2, HUD_H + 2);
    m->shownCell = HUD_NONE;
    return;
  }
  // The marker stays (a blinking one would cost the GBA a redraw every few frames).
  if (!force && cell == m->shownCell && px == m->shownX && py == m->shownY) return;
  if (force || m->shownCell < 0 || m->shownCell == HUD_NONE) bmUiFrame(&g->ui, HUD_COL, 0, HUD_W + 2, HUD_H + 2);
  bmUiFillRect(&g->ui, HUD_COL + 1, 1, HUD_W, HUD_H);
  drawCells(g, HUD_COL + 1, 1, HUD_W, HUD_H, px - HUD_W / 2, py - HUD_H / 2, px, py);
  m->shownCell = (int16_t)cell;
  m->shownX = px;
  m->shownY = py;
}

int32_t bmMapPercent(const BmGame *g)
{
  const BmMap *m = &g->map;
  if (m->pChunk == NULL || m->cellCount == 0) return 0;
  int32_t n = 0;
  for (uint16_t c = 0; c < m->cellCount; c++) n += bmMapExplored(g, c);
  return n * 100 / m->cellCount;
}

bool bmMapReveal(BmGame *g, uint16_t roomName)
{
  findChunk(g);
  const char *name = bmPackString(g->pPack, roomName);
  if (name == NULL) return false;
  int32_t index = bmGameFindRoom(g->pPack, name);
  for (uint16_t i = 0; index >= 0 && i < g->map.roomCount; i++)
    if (bmRead16(roomEntry(&g->map, i)) == (uint32_t)index)
    {
      g->mapRevealed[i / 8] |= (uint8_t)(1u << (i % 8));
      g->map.shownCell = -1;
      return true;
    }
  return false;
}
