/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The game in a window on the PC (SDL3): shows the software PPU's picture, paces frames at
// the GBA's 59.73 Hz and reads keyboard and gamepads as GBA buttons.
//
// Keys: arrows = D-pad, X or Space = A, Z = B, A = L, S = R, Enter = Start,
// Backspace = Select, F11 = fullscreen, Esc = quit. Gamepads: south button = A, east = B,
// shoulders = L/R, start/back, D-pad or left stick.

#include <stdbool.h>
#include <stdint.h>

bool bmWindowOpen(const char *pTitle, int scale);
/// Shows `pPixels` (BGR555, 240x160), waits for the next frame and returns false when the
/// window was closed. `*pKeys` gets the buttons held (BM_KEY_*).
bool bmWindowFrame(const uint16_t *pPixels, uint16_t *pKeys);
void bmWindowClose(void);
