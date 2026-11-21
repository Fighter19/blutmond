/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Host-only access to the virtual hardware, for tests, tools and the editor.

#include <stdint.h>

#include "hal.h"
#include "ppu.h"

/// The video state as the software PPU sees it.
const BmPpuState *bmHalHostGetPpu(void);

/// Frames waited for so far.
uint32_t bmHalHostFrame(void);

/// Renders the current video state (BGR555, 240x160).
void bmHalHostRender(uint16_t *pOut);

/// Buttons that bmHalInputKeys() reports from now on.
void bmHalHostSetKeys(uint16_t keys);

/// The save memory (BM_HAL_SAVE_SIZE bytes), e.g. to keep it in a file between runs.
uint8_t *bmHalHostSaveMemory(void);

/// Puts the virtual hardware back into its power-on state.
void bmHalHostReset(void);

/// Sound: copies the frames (BM_HAL_AUDIO_FRAME_SAMPLES per side each) that became audible
/// since the last call, at most `maxFrames`; returns how many. For the game's window.
uint32_t bmHalHostAudioRead(int8_t *pLeft, int8_t *pRight, uint32_t maxFrames);

/// Live link: the master clocks one word. Returns the word the slave had prepared, then runs
/// the link handler with `masterWord` (as the serial interrupt would). 0xFFFFFFFF if no link
/// is started.
uint32_t bmHalHostLinkExchange(uint32_t masterWord);

/// What answers when this program is the master (bmHalLinkMasterExchange): for tests.
typedef uint32_t (*BmHalHostLinkPeer)(void *pContext, uint32_t out);
void bmHalHostLinkSetPeer(BmHalHostLinkPeer peer, void *pContext);

/// The same from another thread (the game's socket link): waits while the main loop holds the
/// link lock instead of losing the word.
uint32_t bmHalHostLinkExchangeWait(uint32_t masterWord);
