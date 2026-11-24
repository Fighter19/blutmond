/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "ui.h"

#include <string.h>

#include "hal.h"
#include "ppu.h"
#include "text.h"
#include "video.h"

// Tiles in character block 2: 0 empty, 1-9 the frame (corners, edges, inside, row by row),
// 10-18 a gauge cell from empty to full in eighths, from 32 the glyph cache.
enum { TILE_FRAME = 1, TILE_FILL = TILE_FRAME + 4, TILE_GAUGE = 10, TILE_GAUGE_2 = 19 };
#define TILE_BYTES 32
#define FONT_HEADER 12
#define RANGE_SIZE 8

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// A frame tile: piece p (0-8, row by row: top left corner ... bottom right corner).
static void frameTile(uint8_t out[TILE_BYTES], int p)
{
  int px = p % 3, py = p / 3;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
    {
      // Distance to the outer edge in x and y (8: not at an edge).
      int ex = px == 0 ? x : px == 2 ? 7 - x : 8;
      int ey = py == 0 ? y : py == 2 ? 7 - y : 8;
      int e = ex < ey ? ex : ey;
      uint8_t c = BM_UI_FILL;
      if (ex == 0 && ey == 0) c = 0;                 // a rounded corner
      else if (e == 0) c = BM_UI_BORDER;
      else if (e == 1) c = BM_UI_BORDER_DARK;
      uint8_t *b = &out[y * 4 + x / 2];
      *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c);
    }
}

// A gauge tile filled `eighths` from the left in colour `fill`: a dark outline, fill or empty
// inside.
static void gaugeTile(uint8_t out[TILE_BYTES], int eighths, uint8_t fill)
{
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
    {
      uint8_t c = 0;
      if (y >= 1 && y <= 6) c = (y == 1 || y == 6) ? BM_UI_BORDER_DARK : x < eighths ? fill : BM_UI_GAUGE_EMPTY;
      uint8_t *b = &out[y * 4 + x / 2];
      *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c);
    }
}

static void loadFixedTiles(void)
{
  static const BmColor palette[14] = {
    { 0 }, { 0x7FFF }, { (uint16_t)(5 | 5 << 5 | 6 << 10) }, { (uint16_t)(3 | 4 << 5 | 10 << 10) },
    { (uint16_t)(26 | 26 << 5 | 28 << 10) }, { (uint16_t)(8 | 9 << 5 | 14 << 10) },
    { (uint16_t)(28 | 6 << 5 | 6 << 10) }, { (uint16_t)(6 | 2 << 5 | 4 << 10) },
    { (uint16_t)(6 | 14 << 5 | 30 << 10) },
    // The map: a room explored, the room the player is in, one revealed only, a door, the marker.
    { (uint16_t)(4 | 8 << 5 | 20 << 10) }, { (uint16_t)(8 | 16 << 5 | 28 << 10) },
    { (uint16_t)(9 | 9 << 5 | 11 << 10) }, { (uint16_t)(30 | 18 << 5 | 4 << 10) },
    { (uint16_t)(31 | 28 << 5 | 6 << 10) },
  };
  bmPaletteLoadBg(BM_UI_PALETTE_BANK * 16, palette, 14);
  uint8_t tile[TILE_BYTES];
  memset(tile, 0, sizeof(tile));
  bmTilesLoadBg(BM_UI_CHAR_BLOCK, 0, tile, TILE_BYTES);
  for (int p = 0; p < 9; p++)
  {
    memset(tile, 0, sizeof(tile));
    frameTile(tile, p);
    bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(TILE_FRAME + p), tile, TILE_BYTES);
  }
  for (int e = 0; e <= 8; e++)
  {
    memset(tile, 0, sizeof(tile));
    gaugeTile(tile, e, BM_UI_GAUGE);
    bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(TILE_GAUGE + e), tile, TILE_BYTES);
    memset(tile, 0, sizeof(tile));
    gaugeTile(tile, e, BM_UI_GAUGE_2);
    bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(TILE_GAUGE_2 + e), tile, TILE_BYTES);
  }
}

// The chunks of the font (fontName NULL: the first font's).
static void findFont(BmUi *ui, const BmPack *pPack, const char *fontName)
{
  ui->fontChunkCount = 0;
  ui->pPack = pPack;
  ui->pPackData = pPack->pData;
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_FONT, 0); c >= 0 && ui->fontChunkCount < BM_UI_FONT_CHUNKS;
       c = bmPackFindChunk(pPack, BM_CHUNK_FONT, (uint32_t)c + 1))
  {
    uint32_t size = 0;
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)c, &size);
    // Version 2, 8x8 cells, sizes that add up.
    if (p == NULL || size < FONT_HEADER || p[2] != 8 || p[3] != 8 || p[4] != 2) continue;
    if (size < FONT_HEADER + rd16(p + 6) * (uint32_t)RANGE_SIZE + rd16(p + 8) * 8u) continue;
    const char *name = bmPackString(pPack, rd16(p));
    if (fontName == NULL && ui->fontChunkCount > 0)
    {
      // The first font: the chunks with the first one's name.
      const char *first = bmPackString(pPack, rd16(ui->pFontChunks[0]));
      if (name == NULL || first == NULL || strcmp(name, first) != 0) continue;
    }
    else if (fontName != NULL && (name == NULL || strcmp(name, fontName) != 0)) continue;
    ui->pFontChunks[ui->fontChunkCount++] = p;
  }
}

BmResult bmUiInit(BmUi *ui, const BmPack *pPack, const char *fontName)
{
  memset(ui, 0, sizeof(*ui));
  findFont(ui, pPack, fontName);
  if (ui->fontChunkCount == 0) return BM_ERROR_NOT_FOUND;
  loadFixedTiles();
  bmBgSetup(BM_UI_BG, &(BmBgInfo){ .charBlock = BM_UI_CHAR_BLOCK, .screenBlock = BM_UI_SCREEN_BLOCK, .size = BM_BG_SIZE_256x256, .priority = 0 });
  bmBgSetScroll(BM_UI_BG, 0, 0);
  // The rows below the screen stay empty.
  uint16_t empty[32];
  memset(empty, 0, sizeof(empty));
  for (int y = BM_UI_ROWS; y < 32; y++) bmMapLoad(BM_UI_SCREEN_BLOCK, (uint16_t)(y * 32), empty, 32);
  ui->ready = true;
  ui->dirtyRows = (1u << BM_UI_ROWS) - 1;
  bmVideoShowBg(BM_UI_BG, false);
  return BM_SUCCESS;
}

void bmUiSetLanguage(BmUi *ui, uint8_t language)
{
  ui->language = language;
}

// --- Glyphs ---------------------------------------------------------------------------------

// The 8 rows of a character's glyph, or NULL if the font has none.
static const uint8_t *glyphRows(const BmUi *ui, uint32_t code)
{
  for (uint8_t f = 0; f < ui->fontChunkCount; f++)
  {
    const uint8_t *p = ui->pFontChunks[f];
    uint16_t ranges = rd16(p + 6);
    for (uint16_t r = 0; r < ranges; r++)
    {
      const uint8_t *e = p + FONT_HEADER + r * RANGE_SIZE;
      uint32_t first = rd16(e), count = rd16(e + 2);
      if (code < first) break;   // ranges are in order
      if (code < first + count) return p + FONT_HEADER + ranges * RANGE_SIZE + (rd16(e + 4) + (code - first)) * 8u;
    }
  }
  return NULL;
}

static void glyphTile(const uint8_t rows[8], bool onFill, uint8_t tile[TILE_BYTES])
{
  uint8_t ground = onFill ? BM_UI_FILL : 0;
  for (int y = 0; y < 8; y++)
  {
    uint8_t ink = rows[y], above = y > 0 ? rows[y - 1] : 0;
    uint8_t shadow = (uint8_t)((above << 1) & ~ink);   // below right of the ink
    for (int x = 0; x < 8; x += 2)
    {
      uint8_t c[2];
      for (int k = 0; k < 2; k++) c[k] = (ink >> (x + k)) & 1 ? BM_UI_INK : (shadow >> (x + k)) & 1 ? BM_UI_SHADOW : ground;
      tile[y * 4 + x / 2] = (uint8_t)(c[0] | (c[1] << 4));
    }
  }
}

// Codes from here on in the cache are map cells (bmUiMapCell), not characters (the private use
// area: no font of a game has them).
#define MAP_CODE 0xE000u
#define MAP_CODE_END 0xE800u

// A row of 8 pixels as 8 nibbles, the left one lowest (the tile's byte order).
static uint32_t rowOf(uint8_t c)
{
  return c * 0x11111111u;
}

static void mapTile(uint16_t look, uint8_t tile[TILE_BYTES])
{
  static const uint8_t fills[3] = { BM_UI_MAP_ROOM, BM_UI_MAP_HERE, BM_UI_MAP_REVEALED };
  uint8_t kind = (uint8_t)((look >> 8) & 3);
  uint32_t fill = rowOf(fills[kind < 3 ? kind : 0]);
  // Walls on the edges, a door's colour in the middle four pixels of its wall.
  const uint32_t middle = 0x00FFFF00u;   // pixels 2-5 of a row
  uint32_t top = fill, bottom = fill;
  if (look & 1) top = (look & 0x10) ? (rowOf(BM_UI_BORDER) & ~middle) | (rowOf(BM_UI_MAP_DOOR) & middle) : rowOf(BM_UI_BORDER);
  if (look & 4) bottom = (look & 0x40) ? (rowOf(BM_UI_BORDER) & ~middle) | (rowOf(BM_UI_MAP_DOOR) & middle) : rowOf(BM_UI_BORDER);
  for (int y = 0; y < 8; y++)
  {
    uint32_t r = y == 0 ? top : y == 7 ? bottom : fill;
    bool inMiddle = y >= 2 && y <= 5;
    if (look & 8) r = (r & ~0xFu) | ((look & 0x80) && inMiddle ? BM_UI_MAP_DOOR : BM_UI_BORDER);
    if (look & 2) r = (r & 0x0FFFFFFFu) | (uint32_t)((look & 0x20) && inMiddle ? BM_UI_MAP_DOOR : BM_UI_BORDER) << 28;
    if ((look & BM_UI_MAP_MARKER_BIT) && inMiddle) r = (r & ~middle) | (rowOf(BM_UI_MAP_MARKER) & middle);
    tile[y * 4] = (uint8_t)r;
    tile[y * 4 + 1] = (uint8_t)(r >> 8);
    tile[y * 4 + 2] = (uint8_t)(r >> 16);
    tile[y * 4 + 3] = (uint8_t)(r >> 24);
  }
}

static bool slotOnFill(const BmUi *ui, int s)
{
  return (ui->glyphOnFill[s / 32] >> (s % 32)) & 1;
}

// The cache tile showing `code` (on the ground asked for), loading it if it is not there. When
// the cache is full, tiles no cell shows any more are freed first. 0 if nothing helps.
static uint8_t glyphTileFor(BmUi *ui, uint32_t code, bool onFill)
{
  // Most characters are where they were last time.
  uint8_t *hint = &ui->glyphHint[(code & 127) | (onFill ? 128 : 0)];
  if (*hint != 0 && ui->glyphCode[*hint - 1] == code && slotOnFill(ui, *hint - 1) == onFill) return (uint8_t)(BM_UI_GLYPH_FIRST + *hint - 1);
  // The cache fills from slot 0: only the slots used so far can hold it (on the GBA each look
  // at a slot costs, ui.h).
  int freeSlot = -1;
  for (int s = 0; s < ui->glyphUsed; s++)
  {
    if (ui->glyphCode[s] == code && slotOnFill(ui, s) == onFill)
    {
      *hint = (uint8_t)(s + 1);
      return (uint8_t)(BM_UI_GLYPH_FIRST + s);
    }
    if (freeSlot < 0 && ui->glyphCode[s] == 0) freeSlot = s;
  }
  if (freeSlot < 0 && ui->glyphUsed < BM_UI_GLYPH_SLOTS) freeSlot = ui->glyphUsed++;
  if (freeSlot < 0)
  {
    bool used[BM_UI_GLYPH_SLOTS];
    memset(used, 0, sizeof(used));
    for (int i = 0; i < 32 * BM_UI_ROWS; i++)
      if (ui->map[i] >= BM_UI_GLYPH_FIRST) used[ui->map[i] - BM_UI_GLYPH_FIRST] = true;
    for (int s = 0; s < BM_UI_GLYPH_SLOTS; s++)
      if (!used[s]) ui->glyphCode[s] = 0;
    for (int s = 0; s < BM_UI_GLYPH_SLOTS && freeSlot < 0; s++)
      if (!used[s]) freeSlot = s;
    if (freeSlot < 0) return 0;
  }
  uint8_t tile[TILE_BYTES];
  if (code >= MAP_CODE && code < MAP_CODE_END)
  {
    mapTile((uint16_t)(code - MAP_CODE), tile);
  }
  else
  {
    const uint8_t *rows = glyphRows(ui, code);
    if (rows == NULL) return 0;
    glyphTile(rows, onFill, tile);
  }
  bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(BM_UI_GLYPH_FIRST + freeSlot), tile, TILE_BYTES);
  ui->glyphCode[freeSlot] = (uint16_t)code;
  if (onFill) ui->glyphOnFill[freeSlot / 32] |= 1u << (freeSlot % 32);
  else ui->glyphOnFill[freeSlot / 32] &= ~(1u << (freeSlot % 32));
  *hint = (uint8_t)(freeSlot + 1);
  return (uint8_t)(BM_UI_GLYPH_FIRST + freeSlot);
}

// --- Drawing --------------------------------------------------------------------------------

static void setCell(BmUi *ui, int x, int y, uint8_t tile)
{
  if (x < 0 || x >= 32 || y < 0 || y >= BM_UI_ROWS || ui->map[y * 32 + x] == tile) return;
  ui->map[y * 32 + x] = tile;
  ui->dirtyRows |= 1u << y;
}

void bmUiClearRect(BmUi *ui, int col, int row, int w, int h)
{
  for (int y = row; y < row + h; y++)
    for (int x = col; x < col + w; x++) setCell(ui, x, y, 0);
}

void bmUiClear(BmUi *ui)
{
  bmUiClearRect(ui, 0, 0, 32, BM_UI_ROWS);
}

void bmUiFillRect(BmUi *ui, int col, int row, int w, int h)
{
  if (!ui->ready) return;
  for (int y = row; y < row + h; y++)
    for (int x = col; x < col + w; x++) setCell(ui, x, y, TILE_FILL);
}

void bmUiMapCell(BmUi *ui, int col, int row, uint16_t look)
{
  if (!ui->ready || col < 0 || col >= 32 || row < 0 || row >= BM_UI_ROWS) return;
  uint8_t tile = glyphTileFor(ui, MAP_CODE + (look & 0x7FF), false);
  if (tile != 0) setCell(ui, col, row, tile);
}

void bmUiFrame(BmUi *ui, int col, int row, int w, int h)
{
  if (!ui->ready || w < 2 || h < 2) return;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
    {
      int px = x == 0 ? 0 : x == w - 1 ? 2 : 1, py = y == 0 ? 0 : y == h - 1 ? 2 : 1;
      setCell(ui, col + x, row + y, (uint8_t)(TILE_FRAME + py * 3 + px));
    }
}

static void gauge(BmUi *ui, int col, int row, int w, int32_t value, int32_t max, uint8_t first)
{
  if (!ui->ready || w < 1) return;
  int32_t eighths = max <= 0 || value <= 0 ? 0 : value >= max ? w * 8 : (int32_t)((int64_t)value * w * 8 / max);
  if (value > 0 && eighths == 0) eighths = 1;   // something left shows
  for (int x = 0; x < w; x++)
  {
    int32_t e = eighths - x * 8;
    setCell(ui, col + x, row, (uint8_t)(first + (e < 0 ? 0 : e > 8 ? 8 : e)));
  }
}

void bmUiGauge(BmUi *ui, int col, int row, int w, int32_t value, int32_t max)
{
  gauge(ui, col, row, w, value, max, TILE_GAUGE);
}

void bmUiGauge2(BmUi *ui, int col, int row, int w, int32_t value, int32_t max)
{
  gauge(ui, col, row, w, value, max, TILE_GAUGE_2);
}

// Whether a cell shows the frame's inside (its fill, or a glyph on it).
static bool onFill(const BmUi *ui, int x, int y)
{
  uint8_t tile = ui->map[y * 32 + x];
  return tile == TILE_FILL || (tile >= BM_UI_GLYPH_FIRST && slotOnFill(ui, tile - BM_UI_GLYPH_FIRST));
}

static void put(BmUi *ui, int x, int y, uint32_t code)
{
  if (x < 0 || x >= 32 || y < 0 || y >= BM_UI_ROWS) return;
  bool fill = onFill(ui, x, y);
  if (code == ' ')
  {
    setCell(ui, x, y, fill ? TILE_FILL : 0);   // a space needs no glyph
    return;
  }
  uint8_t tile = glyphTileFor(ui, code, fill);
  if (tile == 0) tile = glyphTileFor(ui, '?', fill);
  if (tile != 0) setCell(ui, x, y, tile);
}

int bmUiLabel(BmUi *ui, int col, int row, const char *text)
{
  if (!ui->ready || text == NULL) return 0;
  int x = col, n = 0;
  while (*text)
  {
    uint32_t c = bmTextNextChar(&text);
    if (c == '\n')
    {
      row++;
      x = col;
      continue;
    }
    put(ui, x++, row, c);
    n++;
  }
  return n;
}

void bmUiNumber(BmUi *ui, int col, int row, int32_t value, uint8_t digits)
{
  char text[12];
  int len = 0;
  uint32_t v = value < 0 ? (uint32_t)0 - (uint32_t)value : (uint32_t)value;
  char reversed[11];
  do
  {
    reversed[len++] = (char)('0' + v % 10);
    v /= 10;
  } while (v != 0);
  int need = len + (value < 0);
  int at = 0;
  if (need > digits || digits > 11)
  {
    for (; at < digits && at < 11; at++) text[at] = '*';
  }
  else
  {
    for (; at < digits - need; at++) text[at] = ' ';
    if (value < 0) text[at++] = '-';
    while (len > 0) text[at++] = reversed[--len];
  }
  text[at] = 0;
  bmUiLabel(ui, col, row, text);
}

uint32_t bmUiFormat(const BmPack *pPack, uint8_t language, char *out, uint32_t size, const char *format, const BmUiArg *args, uint8_t argCount)
{
  uint32_t n = 0;
  if (size == 0) return 0;
#define PUT(ch) do { if (n + 1 < size) out[n++] = (ch); } while (0)
  for (const char *p = format; p != NULL && *p; p++)
  {
    if (p[0] == '{' && p[1] == '{')
    {
      PUT('{');
      p++;
      continue;
    }
    if (!(p[0] == '{' && p[1] >= '0' && p[1] <= '7' && p[2] == '}'))
    {
      PUT(*p);
      continue;
    }
    uint8_t i = (uint8_t)(p[1] - '0');
    p += 2;
    if (i >= argCount) continue;
    if (args[i].isText)
    {
      const char *t = pPack != NULL ? bmText(pPack, language, (uint16_t)args[i].value) : NULL;
      for (; t != NULL && *t; t++) PUT(*t);
      continue;
    }
    char digits[11];
    int len = 0;
    uint32_t v = args[i].value < 0 ? (uint32_t)0 - (uint32_t)args[i].value : (uint32_t)args[i].value;
    do
    {
      digits[len++] = (char)('0' + v % 10);
      v /= 10;
    } while (v != 0);
    if (args[i].value < 0) PUT('-');
    while (len > 0)
    {
      len--;
      PUT(digits[len]);   // not PUT(digits[--len]): PUT skips its argument when the buffer is full
    }
  }
#undef PUT
  // A character cut in the middle at the end is dropped.
  while (n > 0 && ((uint8_t)out[n - 1] & 0xC0) == 0x80) n--;
  if (n > 0 && ((uint8_t)out[n - 1] & 0xC0) == 0xC0) n--;
  out[n] = 0;
  return n;
}

// Cells a UTF-8 word takes.
static int cellsOf(const char *p, int bytes)
{
  int n = 0;
  for (const char *end = p + bytes; p < end; n++) bmTextNextChar(&p);
  return n;
}

int bmUiText(BmUi *ui, int col, int row, int w, int h, uint16_t textId, const BmUiArg *args, uint8_t argCount)
{
  return bmUiTextRange(ui, col, row, w, h, textId, args, argCount, 0, 0x7FFFFFFF, NULL);
}

int bmUiTextRange(BmUi *ui, int col, int row, int w, int h, uint16_t textId, const BmUiArg *args, uint8_t argCount, int32_t first,
                  int32_t last, int32_t *pCount)
{
  return bmUiTextScroll(ui, col, row, w, h, textId, args, argCount, 0, first, last, pCount, NULL);
}

int bmUiTextScroll(BmUi *ui, int col, int row, int w, int h, uint16_t textId, const BmUiArg *args, uint8_t argCount, int scroll,
                   int32_t first, int32_t last, int32_t *pCount, bool *pMore)
{
  if (pCount) *pCount = 0;
  if (pMore) *pMore = false;
  if (!ui->ready || w < 1 || h < 1) return 0;
  if (scroll < 0) scroll = 0;
  h += scroll;   // laid out from the text's first row; rows before `scroll` take no place on screen
  row -= scroll;
  char text[256];
  bmUiFormat(ui->pPack, ui->language, text, sizeof(text), bmText(ui->pPack, ui->language, textId), args, argCount);
  // Word by word: a word that does not fit on the row goes to the next; one longer than a row
  // is cut. Rows past h are left out. Characters are counted (spaces and line breaks too); only
  // those from `first` to before `last` are drawn, the others only take their places.
  int x = 0, y = 0;
  int32_t n = 0;
  const char *p = text;
  while (*p && y < h)
  {
    if (*p == '\n')
    {
      x = 0;
      y++;
      p++;
      n++;
      continue;
    }
    if (*p == ' ')
    {
      if (x > 0 && x < w)
      {
        if (n >= first && n < last && y >= scroll) put(ui, col + x, row + y, ' ');
        x++;
      }
      p++;
      n++;
      continue;
    }
    int bytes = 0;
    while (p[bytes] && p[bytes] != ' ' && p[bytes] != '\n') bytes++;
    int len = cellsOf(p, bytes);
    if (x > 0 && x + len > w)
    {
      x = 0;
      y++;
      if (y >= h) break;
    }
    const char *end = p + bytes;
    while (p < end)
    {
      if (x >= w)
      {
        x = 0;
        y++;
        if (y >= h) break;
      }
      uint32_t c = bmTextNextChar(&p);
      if (n >= first && n < last && y >= scroll) put(ui, col + x, row + y, c);
      x++;
      n++;
    }
    p = end;
  }
  if (pMore)
  {
    // Text left after the last row shown (but spaces and line breaks).
    while (*p == ' ' || *p == '\n') p++;
    *pMore = *p != 0;
  }
  if (pCount) *pCount = n;
  return (y < h ? y + (x > 0) : h) - scroll;
}

void bmUiShow(BmUi *ui, bool shown)
{
  ui->shown = shown;
}

void bmUiPresent(BmUi *ui)
{
  if (!ui->ready) return;
  // The room sets its own layers when it is shown: background 3 is put back every time.
  bmVideoShowBg(BM_UI_BG, ui->shown);
  for (int y = 0; ui->dirtyRows != 0 && y < BM_UI_ROWS; y++)
  {
    if (!(ui->dirtyRows & (1u << y))) continue;
    uint16_t row[32];
    for (int x = 0; x < 32; x++) row[x] = (uint16_t)(ui->map[y * 32 + x] | (ui->map[y * 32 + x] ? BM_UI_PALETTE_BANK << 12 : 0));
    bmMapLoad(BM_UI_SCREEN_BLOCK, (uint16_t)(y * 32), row, 32);
  }
  ui->dirtyRows = 0;
}

void bmUiRestore(BmUi *ui)
{
  if (!ui->ready) return;
  loadFixedTiles();
  bmBgSetup(BM_UI_BG, &(BmBgInfo){ .charBlock = BM_UI_CHAR_BLOCK, .screenBlock = BM_UI_SCREEN_BLOCK, .size = BM_BG_SIZE_256x256, .priority = 0 });
  for (int s = 0; s < BM_UI_GLYPH_SLOTS; s++)
  {
    if (ui->glyphCode[s] == 0) continue;
    uint8_t tile[TILE_BYTES];
    if (ui->glyphCode[s] >= MAP_CODE && ui->glyphCode[s] < MAP_CODE_END)
    {
      mapTile((uint16_t)(ui->glyphCode[s] - MAP_CODE), tile);
    }
    else
    {
      const uint8_t *rows = glyphRows(ui, ui->glyphCode[s]);
      if (rows == NULL) continue;
      glyphTile(rows, slotOnFill(ui, s), tile);
    }
    bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(BM_UI_GLYPH_FIRST + s), tile, TILE_BYTES);
  }
  ui->dirtyRows = (1u << BM_UI_ROWS) - 1;
}

void bmUiRebind(BmUi *ui, const BmPack *pPack)
{
  if (!ui->ready) return;
  const char *name = ui->fontChunkCount > 0 ? bmPackString(ui->pPack, rd16(ui->pFontChunks[0])) : NULL;
  // The name's text lives in the old pack: compare before it is gone (both are in place now).
  char copy[32];
  size_t n = 0;
  for (; name != NULL && name[n] && n < sizeof(copy) - 1; n++) copy[n] = name[n];
  copy[n] = 0;
  findFont(ui, pPack, n > 0 ? copy : NULL);
  if (ui->fontChunkCount == 0)
  {
    ui->ready = false;
    return;
  }
  bmUiRestore(ui);
}

static int8_t s_lightBg = -1;

void bmUiSetLightLayer(int8_t bg)
{
  s_lightBg = bg;
}

void bmUiBlend(void)
{
  if (s_lightBg >= 0)
  {
    // A room lit with a light layer (light.h): it is added over the room, and so is the UI,
    // at full weight (one pair of weights serves both).
    uint8_t room = (uint8_t)((BM_LAYER_BG(0) | BM_LAYER_BG(1) | BM_LAYER_BG(2)) & ~BM_LAYER_BG(s_lightBg));
    bmBlendSet(BM_BLEND_ALPHA, (uint8_t)(BM_LAYER_BG(BM_UI_BG) | BM_LAYER_BG(s_lightBg)), (uint8_t)(room | BM_LAYER_OBJ | BM_LAYER_BACKDROP), 16, 16, 0);
    return;
  }
  bmBlendSet(BM_BLEND_ALPHA, BM_LAYER_BG(BM_UI_BG), BM_LAYER_BG(0) | BM_LAYER_BG(1) | BM_LAYER_BG(2) | BM_LAYER_OBJ | BM_LAYER_BACKDROP,
             BM_UI_BLEND_UI, BM_UI_BLEND_BEHIND, 0);
}
