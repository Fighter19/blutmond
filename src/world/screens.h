/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Screens (docs/plans/ui.md §4): classes that extend `Screen`, opened on a stack of at most 4
// (the Start menu, Equipment on top of it, ...). While one is open the room stands still and
// is darkened behind it; the top screen gets the buttons as press(name) and release(name)
// (#Up #Down #Left #Right #A #B #Start #Select #L #R; held directions repeat after 15 frames,
// every 5), every open screen gets tick() and runs its state code. A screen draws itself with
// the ui_* calls in opened(), which comes again after a state was loaded or the language
// changed (with the layer cleared first). Start opens the class named StartMenu, if the pack
// has one, when no screen is open.

#include "shared.h"
#include "script_api.h"

#define BM_SCREENS_MAX 4

typedef struct BmScreens
{
  // State (snapshot).
  uint8_t count;
  uint32_t classKey[BM_SCREENS_MAX];      // FNV-1a of the class's name
  uint8_t state[BM_SCREENS_MAX], wait[BM_SCREENS_MAX];
  uint16_t pc[BM_SCREENS_MAX], timer[BM_SCREENS_MAX];
  int32_t fields[BM_SCREENS_MAX * BM_SCRIPT_MAX_FIELDS];
  uint8_t repeat;                         // frames a direction has been held
  // Derived: the class in the actors' class table, or -1; the buttons' names (string ids).
  int8_t cls[BM_SCREENS_MAX];
  uint16_t buttonName[10];
} BmScreens;

struct BmGame;

/// Opens the Screen class named `className` (string id) on top; it gets opened(). False if
/// there is no such class or the stack is full.
bool bmScreensOpen(struct BmGame *pGame, uint16_t className);

/// Closes the top screen (it gets closed() first). When the last one closes, the UI layer is
/// cleared and hidden and the room is shown plainly again.
void bmScreensClose(struct BmGame *pGame);

/// One frame while a screen is open: buttons to the top one, tick and state code for all.
void bmScreensFrame(struct BmGame *pGame, uint16_t keys, uint16_t previousKeys);

/// Clears the UI layer and lets every open screen draw itself again, bottom first (after a
/// state was loaded, the language changed).
void bmScreensRedraw(struct BmGame *pGame);

/// After the pack's scripts were (re)loaded: finds each screen's class again.
void bmScreensBind(struct BmGame *pGame);
