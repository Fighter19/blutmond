/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The world map (docs/spec/world-format.md): the pack's WRLD chunk says where the rooms lie on
// a grid of screens, with their walls and doors. The cells the player has been in are explored
// (a bit each in the save game); a room can be revealed without being explored (a map
// station). The map screen draws it with ui_map, the HUD shows a small map around the player.

#include <stdbool.h>
#include <stdint.h>

#define BM_MAP_MAX_CELLS 512
#define BM_MAP_MAX_ROOMS 64
#define BM_MAP_HEADER 20
#define BM_MAP_ROOM_SIZE 12
#define BM_MAP_BLINK 16   // frames the player's marker is shown, then as many not

struct BmGame;

/// The map's view of a game: found again when the pack changed; what the HUD shows last.
typedef struct BmMap
{
  const uint8_t *pChunk;    // WRLD, NULL: the game has no map
  const uint8_t *pFrom;     // the pack data and CRC it was found in
  uint32_t fromCrc;
  uint16_t roomCount, cellCount;
  int16_t room;             // the current room's entry, -1: not on the map
  int16_t shownCell;        // the HUD's map: the cell at its middle as last drawn (-1: draw it)
  int16_t shownX, shownY;
  uint8_t settle;           // frames the HUD's map waits after a room was entered (that frame is busy)
} BmMap;

/// After entering a room (or the pack changed): the map's chunk and the room's entry.
void bmMapEnterRoom(struct BmGame *pGame);

/// Once a game frame: the cell the player is in is explored.
void bmMapFrame(struct BmGame *pGame);

/// The map in w x h cells of the UI layer from (col, row), centred on the player (the whole
/// map in the middle if it fits). Cells the player has not seen stay as they are drawn (the
/// frame's inside). The player's cell blinks with the game frame.
void bmMapDraw(struct BmGame *pGame, int col, int row, int w, int h);

/// The HUD's small map at the top right (if the map has one): drawn when the player's cell
/// changed (not in the frame a room was entered), the player's marker steady. `force`: all of it.
void bmMapHud(struct BmGame *pGame, bool force);

/// Explored cells of all, in percent (0 without a map).
int32_t bmMapPercent(const struct BmGame *pGame);

/// Reveals the room with the id `roomName` (a string id of the pack): its cells are shown
/// without being explored. False if it is not on the map.
bool bmMapReveal(struct BmGame *pGame, uint16_t roomName);

/// Whether the cell `cell` (the map's numbering) is explored.
bool bmMapExplored(const struct BmGame *pGame, uint16_t cell);
