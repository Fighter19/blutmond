/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "ui.h"

#include <string.h>

#include "hal.h"
#include "ppu.h"
#include "video.h"

// Tiles in character block 2: 0 empty, 1-9 the frame (corners, edges, inside, row by row),
// from GLYPHS the font on a transparent ground, then the same on the frame's inside.
enum { TILE_FRAME = 1, TILE_FILL = TILE_FRAME + 4, GLYPHS = 16 };
#define TILE_BYTES 32


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

BmResult bmUiInit(BmUi *ui, const BmPack *pPack, const char *fontName)
{
  memset(ui, 0, sizeof(*ui));
  ui->pPack = pPack;
  const uint8_t *font = NULL;
  uint32_t size = 0;
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_FONT, 0); c >= 0 && font == NULL; c = bmPackFindChunk(pPack, BM_CHUNK_FONT, (uint32_t)c + 1))
  {
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)c, &size);
    if (p == NULL || size < 8) continue;
    const char *name = bmPackString(pPack, (uint16_t)(p[0] | (p[1] << 8)));
    if (fontName == NULL || (name != NULL && strcmp(name, fontName) == 0)) font = p;
  }
  // 8x8 cells only in this version; both sets of glyphs must have tile numbers below 256.
  if (font == NULL || font[2] != 8 || font[3] != 8 || size < 8u + font[5] * 8u || GLYPHS + 2 * font[5] > 256)
    return BM_ERROR_NOT_FOUND;
  ui->first = font[4];
  ui->count = font[5];

  static const BmColor palette[6] = {
    { 0 }, { 0x7FFF }, { (uint16_t)(5 | 5 << 5 | 6 << 10) }, { (uint16_t)(3 | 4 << 5 | 10 << 10) },
    { (uint16_t)(26 | 26 << 5 | 28 << 10) }, { (uint16_t)(8 | 9 << 5 | 14 << 10) },
  };
  bmPaletteLoadBg(BM_UI_PALETTE_BANK * 16, palette, 6);

  uint8_t tile[TILE_BYTES];
  memset(tile, 0, sizeof(tile));
  bmTilesLoadBg(BM_UI_CHAR_BLOCK, 0, tile, TILE_BYTES);
  for (int p = 0; p < 9; p++)
  {
    memset(tile, 0, sizeof(tile));
    frameTile(tile, p);
    bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(TILE_FRAME + p), tile, TILE_BYTES);
  }
  // The glyphs: 1 bit per pixel in the chunk; ink, a shadow below right of it, and either a
  // transparent ground or the frame's inside.
  const uint8_t *rows = font + 8;
  for (int g = 0; g < ui->count; g++)
  {
    for (int set = 0; set < 2; set++)
    {
      uint8_t ground = set == 0 ? 0 : BM_UI_FILL;
      for (int y = 0; y < 8; y++)
      {
        uint8_t ink = rows[g * 8 + y], above = y > 0 ? rows[g * 8 + y - 1] : 0;
        uint8_t shadow = (uint8_t)((above << 1) & ~ink);
        for (int x = 0; x < 8; x += 2)
        {
          uint8_t c[2];
          for (int k = 0; k < 2; k++)
            c[k] = (ink >> (x + k)) & 1 ? BM_UI_INK : (shadow >> (x + k)) & 1 ? BM_UI_SHADOW : ground;
          tile[y * 4 + x / 2] = (uint8_t)(c[0] | (c[1] << 4));
        }
      }
      bmTilesLoadBg(BM_UI_CHAR_BLOCK, (uint16_t)(GLYPHS + set * ui->count + g), tile, TILE_BYTES);
    }
  }

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

void bmUiClearRect(BmUi *ui, int col, int row, int w, int h)
{
  for (int y = row; y < row + h; y++)
    for (int x = col; x < col + w; x++)
      if (x >= 0 && x < 32 && y >= 0 && y < BM_UI_ROWS)
      {
        ui->map[y * 32 + x] = 0;
        ui->dirtyRows |= 1u << y;
      }
}

void bmUiClear(BmUi *ui)
{
  bmUiClearRect(ui, 0, 0, 32, BM_UI_ROWS);
}

void bmUiFrame(BmUi *ui, int col, int row, int w, int h)
{
  if (!ui->ready || w < 2 || h < 2) return;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
    {
      int cx = col + x, cy = row + y;
      if (cx < 0 || cx >= 32 || cy < 0 || cy >= BM_UI_ROWS) continue;
      int px = x == 0 ? 0 : x == w - 1 ? 2 : 1, py = y == 0 ? 0 : y == h - 1 ? 2 : 1;
      ui->map[cy * 32 + cx] = (uint8_t)(TILE_FRAME + py * 3 + px);
      ui->dirtyRows |= 1u << cy;
    }
}

// Whether a cell shows the frame's inside (its fill, or a glyph on it).
static bool onFill(const BmUi *ui, int x, int y)
{
  uint16_t tile = ui->map[y * 32 + x];
  return tile == TILE_FILL || (tile >= GLYPHS + ui->count && tile < GLYPHS + 2 * ui->count);
}

static void put(BmUi *ui, int x, int y, char c)
{
  if (x < 0 || x >= 32 || y < 0 || y >= BM_UI_ROWS) return;
  uint8_t code = (uint8_t)c;
  if (code < ui->first || code >= ui->first + ui->count) code = '?';
  if (code < ui->first || code >= ui->first + ui->count) code = ui->first;
  uint16_t glyph = (uint16_t)(GLYPHS + (code - ui->first) + (onFill(ui, x, y) ? ui->count : 0));
  if (ui->map[y * 32 + x] == glyph) return;
  ui->map[y * 32 + x] = (uint8_t)glyph;
  ui->dirtyRows |= 1u << y;
}

int bmUiLabel(BmUi *ui, int col, int row, const char *text)
{
  if (!ui->ready || text == NULL) return 0;
  int x = col, n = 0;
  for (; *text; text++)
  {
    if (*text == '\n')
    {
      row++;
      x = col;
      continue;
    }
    put(ui, x++, row, *text);
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

uint32_t bmUiFormat(const BmPack *pPack, char *out, uint32_t size, const char *format, const BmUiArg *args, uint8_t argCount)
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
      const char *t = pPack != NULL ? bmPackString(pPack, (uint16_t)args[i].value) : NULL;
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
  out[n] = 0;
  return n;
}

int bmUiText(BmUi *ui, int col, int row, int w, int h, uint16_t textId, const BmUiArg *args, uint8_t argCount)
{
  if (!ui->ready || w < 1 || h < 1) return 0;
  char text[256];
  bmUiFormat(ui->pPack, text, sizeof(text), bmPackString(ui->pPack, textId), args, argCount);
  // Word by word: a word that does not fit on the row goes to the next; one longer than a row
  // is cut. Rows past h are left out.
  int x = 0, y = 0;
  const char *p = text;
  while (*p && y < h)
  {
    if (*p == '\n')
    {
      x = 0;
      y++;
      p++;
      continue;
    }
    if (*p == ' ')
    {
      if (x > 0 && x < w) put(ui, col + x++, row + y, ' ');
      p++;
      continue;
    }
    int len = 0;
    while (p[len] && p[len] != ' ' && p[len] != '\n') len++;
    if (x > 0 && x + len > w)
    {
      x = 0;
      y++;
      if (y >= h) break;
    }
    for (int k = 0; k < len; k++)
    {
      if (x >= w)
      {
        x = 0;
        y++;
        if (y >= h) break;
      }
      put(ui, col + x++, row + y, p[k]);
    }
    p += len;
  }
  return y < h ? y + (x > 0) : h;
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
  // Only the rows that changed.
  for (int y = 0; ui->dirtyRows != 0 && y < BM_UI_ROWS; y++)
  {
    if (!(ui->dirtyRows & (1u << y))) continue;
    uint16_t row[32];
    for (int x = 0; x < 32; x++) row[x] = (uint16_t)(ui->map[y * 32 + x] | (ui->map[y * 32 + x] ? BM_UI_PALETTE_BANK << 12 : 0));
    bmMapLoad(BM_UI_SCREEN_BLOCK, (uint16_t)(y * 32), row, 32);
  }
  ui->dirtyRows = 0;
}
