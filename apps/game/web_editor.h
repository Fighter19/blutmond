/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The game in the browser, run from the editor in the browser (docs/USAGE.md §10): the page
// is opened as game.html?editor in a frame of the editor's page, and the editor sends it
// cooked packs with postMessage, the first to start, each later one as a reload (`module`).
// What the desktop editor does over the live link, without a link.

#include <stdbool.h>
#include <stdint.h>

/// True if this page was opened by the editor (game.html?editor).
bool bmWebFromEditor(void);

/// Starts listening for packs and tells the editor the game is ready for them.
void bmWebListen(void);

/// The next pack the editor sent (malloc'ed, the caller frees it), or NULL. `pMode`: 0 start
/// it from the beginning, 3 reload it keeping the game going (BmLiveMode).
uint8_t *bmWebTakePack(uint32_t *pSize, int *pMode);
