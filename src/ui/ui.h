/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The UI layer (docs/plans/ui.md §2): background 3 in front of the room, drawn in 8x8 cells
// (30 x 20 visible), with a font from the pack (FONT chunks), frames, gauges and text. It owns
// what the room leaves free: background 3, character block 2 (tiles), screen block 25 (map),
// background palette bank 15. The map is kept in RAM, one byte per cell, and copied to video
// memory by bmUiPresent, only the rows that changed.
//
// Glyphs are not all in video memory: a cache of 224 tiles holds those on screen, each on a
// transparent ground or on a frame's inside, and is filled from the font when a character is
// drawn. So a font may have many more characters than fit (Japanese), as long as one screen
// does not show more than 224 different ones.
//
// Text is UTF-8. A text is a string id of the pack, shown in the UI's language (text.h); its
// placeholders {0} to {7} are filled from numbers and string ids (bmUiFormat). A character the
// font does not have is drawn as '?'.
//
// Not yet (later steps of ui.md): widgets as data (UISC), variable-width text, icons.

#include "pack.h"
#include "shared.h"

#define BM_UI_BG 3
#define BM_UI_CHAR_BLOCK 2
#define BM_UI_SCREEN_BLOCK 25
#define BM_UI_PALETTE_BANK 15
#define BM_UI_COLUMNS 30
#define BM_UI_ROWS 20
#define BM_UI_FONT_CHUNKS 8      // chunks one font may be made of
#define BM_UI_GLYPH_FIRST 32     // tiles 32-255 hold glyphs
#define BM_UI_GLYPH_SLOTS 224

/// Colours of the UI palette (bank 15): 1 ink, 2 the ink's shadow, 3 a frame's inside, 4 and
/// 5 its border (light, dark), 6 a gauge's fill, 7 its empty part.
enum { BM_UI_INK = 1, BM_UI_SHADOW, BM_UI_FILL, BM_UI_BORDER, BM_UI_BORDER_DARK, BM_UI_GAUGE, BM_UI_GAUGE_EMPTY };

typedef struct BmUi
{
  const BmPack *pPack;
  const uint8_t *pPackData;   // the pack data the font chunks point into
  bool ready;                 // a font was found
  bool shown;
  uint8_t language;           // of the texts shown (text.h)
  uint8_t fontChunkCount;
  uint32_t dirtyRows;         // a bit per map row changed since the last present
  const uint8_t *pFontChunks[BM_UI_FONT_CHUNKS];
  uint16_t glyphCode[BM_UI_GLYPH_SLOTS];  // the code point a cache tile holds; 0: free
  uint32_t glyphOnFill[BM_UI_GLYPH_SLOTS / 32];   // a bit per tile drawn on a frame's inside
  uint8_t glyphHint[256];                 // by (code & 127) and ground: the slot it was in last + 1
  uint8_t map[32 * BM_UI_ROWS];           // the visible rows of screen block 25: tile numbers
} BmUi;

/// A value for a placeholder: a number, or a string id of the pack.
typedef struct BmUiArg
{
  bool isText;
  int32_t value;
} BmUiArg;

static inline BmUiArg bmUiNumberArg(int32_t value)
{
  BmUiArg a;
  a.isText = false;
  a.value = value;
  return a;
}
static inline BmUiArg bmUiTextArg(uint16_t stringId)
{
  BmUiArg a;
  a.isText = true;
  a.value = stringId;
  return a;
}

/// Finds the font named `fontName` (NULL: the first one in the pack; all its FONT chunks), puts
/// the frame and gauge tiles into character block 2 and the UI palette into bank 15, and sets
/// up background 3 (hidden, empty). Call after the video was set up (bmVideoInit). Without the
/// font: BM_ERROR_NOT_FOUND, and the UI draws nothing.
BmResult bmUiInit(BmUi *pUi, const BmPack *pPack, const char *fontName);

/// The language texts are shown in from now on (what is on screen stays as it is).
void bmUiSetLanguage(BmUi *pUi, uint8_t language);

/// Empties the whole layer, or a rectangle of cells. Cells are 0-31 across, 0-19 down; others
/// are left out.
void bmUiClear(BmUi *pUi);
void bmUiClearRect(BmUi *pUi, int col, int row, int w, int h);

/// A frame of w x h cells (at least 2 x 2): border and filled inside. Text drawn on its inside
/// keeps the fill behind it.
void bmUiFrame(BmUi *pUi, int col, int row, int w, int h);

/// A gauge of w cells: `value` of `max` filled, in eighths of a cell.
void bmUiGauge(BmUi *pUi, int col, int row, int w, int32_t value, int32_t max);

/// UTF-8 text from a cell on; '\n' goes to the next row at `col`. Returns the cells written.
int bmUiLabel(BmUi *pUi, int col, int row, const char *text);

/// A number right-aligned in `digits` cells (a minus sign takes one); '*' in every cell if it
/// does not fit.
void bmUiNumber(BmUi *pUi, int col, int row, int32_t value, uint8_t digits);

/// Fills the placeholders {0} to {7} of `format` from `args` ({{ is a brace; a text argument
/// is a string id of `pPack`, in `language`). Writes at most `size` bytes with the
/// terminating zero; returns the length.
uint32_t bmUiFormat(const BmPack *pPack, uint8_t language, char *pOut, uint32_t size, const char *format, const BmUiArg *args, uint8_t argCount);

/// The text with string id `textId` in the UI's language, formatted, wrapped at spaces into
/// w x h cells. Returns the rows used.
int bmUiText(BmUi *pUi, int col, int row, int w, int h, uint16_t textId, const BmUiArg *args, uint8_t argCount);

/// Shows or hides the layer (from the next present).
void bmUiShow(BmUi *pUi, bool shown);

/// Once a frame, in the vertical blank: background 3 on or off, the rows of the map that
/// changed.
void bmUiPresent(BmUi *pUi);

/// After the video state was rebuilt (a state loaded): every tile and the whole map again.
void bmUiRestore(BmUi *pUi);

/// After the pack changed (a hot reload): finds the font's chunks again and restores, keeping
/// what is on screen.
void bmUiRebind(BmUi *pUi, const BmPack *pPack);
