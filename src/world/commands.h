/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Commands (special moves): an input of directions, then a game button, does what a record of
// the structure `Command` says. A command belongs to a weapon (it works while that weapon is
// equipped, in equipment slot 0) or to a charm (while that record is equipped in any other
// slot, whatever the weapon). The record's fields, by name:
//
//   name   input       directions and the button, separated by _: Down_DownFwd_Fwd_Attack.
//                      Up, Down, Fwd, Back, UpFwd, UpBack, DownFwd, DownBack (Fwd is the way
//                      the player faces when the button is pressed); Attack, Jump, Dash,
//                      Spell, Use (attachments.h)
//   int    cost        mana it takes; without enough the button does what it does anyway
//   record weapon      the weapon whose command it is, or
//   record charm       the charm that gives it
//   attach behaviour   attached in slot BM_GAME_COMMAND_SLOT for `frames`, its first record
//                      variable set to the command
//   int    frames
//
// The directions are kept as they change (a history of 8, with the frame); an input matches
// when its directions are found in that order, newest last: the last one began at most 10
// frames before the button, each one before it at most 12 frames before the next (directions
// between them are passed over, but none may be older than that). Among the commands that
// match, the one with most directions wins. Its button press does not reach the attachments
// (the weapon does not swing too).

#include "shared.h"

struct BmGame;

#define BM_COMMAND_HISTORY 8

/// Once a frame before the attachments: notes a change of direction and, for game buttons
/// pressed now (no screen, no dialogue, no lock), does the command they finish. Returns the
/// keys that did one (not to be pressed for the attachments).
uint16_t bmCommandsFrame(struct BmGame *pGame, uint16_t keys, uint16_t previousKeys);

/// Finds the Command table of the pack (once per pack: at a room's loading, not in play).
void bmCommandsPrepare(struct BmGame *pGame);

/// The command `input` would do now if `button` (a BM_KEY_*) were pressed, or 0 (tests, UI).
uint32_t bmCommandsMatch(struct BmGame *pGame, uint16_t button);
