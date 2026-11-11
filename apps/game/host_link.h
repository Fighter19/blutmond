/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The live link of the PC game over TCP (127.0.0.1 only), so tools/bmlink --tcp and the editor
// can push packs and state into a running game the way they do over the Xboo cable. Each word
// is 4 bytes little-endian from the master; the game answers with the word it had prepared.
// A thread serves the socket and runs the link handler, as the serial interrupt does on the GBA.

#include <stdbool.h>
#include <stdint.h>

#define BM_HOST_LINK_DEFAULT_PORT 7414

bool bmHostLinkListen(uint16_t port);
void bmHostLinkStop(void);
