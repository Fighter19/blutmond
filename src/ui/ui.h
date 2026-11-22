/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The UI layer (docs/plans/ui.md §2, step 2): background 3 in front of the room, drawn in 8x8
// cells (30 x 20 visible), with a font from the pack (FONT chunk), frames and text. It owns
// what the room leaves free: background 3, character block 2 (tiles), screen block 25 (map),
// background palette bank 15. The map is kept in RAM and copied to video memory by
// bmUiPresent, only the rows that changed, so drawing costs nothing on screen until then.
//
// Text has no strings of its own at run time: a text is a string id of the pack, and its
// placeholders {0} to {7} are filled from numbers and string ids (bmUiFormat). Characters are
// ASCII; one the font does not have is drawn as '?'.
//
// Not yet (later steps of ui.md): widgets as data (UISC), screens and scripts, variable-width
// text, icons, darkening behind frames.

#include "pack.h"
#include "shared.h"

#define BM_UI_BG 3
#define BM_UI_CHAR_BLOCK 2
#define BM_UI_SCREEN_BLOCK 25
#define BM_UI_PALETTE_BANK 15
#define BM_UI_COLUMNS 30
#define BM_UI_ROWS 20

/// Colours of the UI palette (bank 15): 1 ink, 2 the ink's shadow, 3 a frame's inside, 4 and
/// 5 its border (light, dark).
enum { BM_UI_INK = 1, BM_UI_SHADOW, BM_UI_FILL, BM_UI_BORDER, BM_UI_BORDER_DARK };

typedef struct BmUi
{
  const BmPack *pPack;
  bool ready;                 // a font was found and loaded
  bool shown;
  uint32_t dirtyRows;         // a bit per map row changed since the last present
  uint8_t first, count;       // the font's characters
  uint8_t map[32 * BM_UI_ROWS]; // the visible rows of screen block 25: tile numbers (bank 15)
} BmUi;

/// A value for a placeholder: a number, or a string id of the pack.
typedef struct BmUiArg
{
  bool isText;
  int32_t value;
} BmUiArg;

static inline BmUiArg bmUiNumberArg(int32_t value) { return (BmUiArg){ false, value }; }
static inline BmUiArg bmUiTextArg(uint16_t stringId) { return (BmUiArg){ true, stringId }; }

/// Loads the font named `fontName` (NULL: the pack's first) into character block 2, the frame
/// tiles after it, the UI palette into bank 15, and sets up background 3 (hidden, empty).
/// Call after the video was set up (bmVideoInit, a room shown). BM_ERROR_NOT_FOUND without a
/// font: the UI then draws nothing.
BmResult bmUiInit(BmUi *pUi, const BmPack *pPack, const char *fontName);

/// Empties the whole layer, or a rectangle of cells. Cells are 0-31 across, 0-19 down; others
/// are left out.
void bmUiClear(BmUi *pUi);
void bmUiClearRect(BmUi *pUi, int col, int row, int w, int h);

/// A frame of w x h cells (at least 2 x 2): border and filled inside. Text drawn on its inside
/// keeps the fill behind it.
void bmUiFrame(BmUi *pUi, int col, int row, int w, int h);

/// ASCII text from a cell on; '\n' goes to the next row at `col`. Returns the cells written.
int bmUiLabel(BmUi *pUi, int col, int row, const char *text);

/// A number right-aligned in `digits` cells (a minus sign takes one); '*' in every cell if it
/// does not fit.
void bmUiNumber(BmUi *pUi, int col, int row, int32_t value, uint8_t digits);

/// Fills the placeholders {0} to {7} of `format` from `args` ({{ is a brace; a text argument
/// is a string id of `pPack`). Writes at most
/// `size` bytes with the terminating zero; returns the length.
uint32_t bmUiFormat(const BmPack *pPack, char *pOut, uint32_t size, const char *format, const BmUiArg *args, uint8_t argCount);

/// The text with string id `textId`, formatted, wrapped at spaces into w x h cells. Returns
/// the rows used.
int bmUiText(BmUi *pUi, int col, int row, int w, int h, uint16_t textId, const BmUiArg *args, uint8_t argCount);

/// Shows or hides the layer (from the next present).
void bmUiShow(BmUi *pUi, bool shown);

/// Once a frame, in the vertical blank: background 3 on or off, the map if it changed.
void bmUiPresent(BmUi *pUi);
