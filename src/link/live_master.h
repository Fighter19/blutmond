/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Master side of live reload (PC tools): ask the runtime what it has, push a pack sending
// only the chunks it does not have, apply it, read and write state.

#include "link_master.h"
#include "live.h"

/// Answers can take a few frames (the runtime handles them at frame boundaries).
#define BM_LIVE_ANSWER_POLLS 2000000u

typedef struct BmLiveRemote
{
  BmLiveHello hello;
  uint32_t pairs[2 * BM_LIVE_HELLO_MAX_HASHES];  // hash, size of the chunks it has
} BmLiveRemote;

typedef struct BmLivePushStats
{
  uint32_t chunksSent, chunksResident;
  uint32_t bytesSent;          // chunk bytes sent (not counting the TOC)
} BmLivePushStats;

bool bmLiveMasterHello(BmLinkMaster *pMaster, BmLiveRemote *pRemote);

/// Sends the pack's table of contents and the chunks `pRemote` does not list (all of them if
/// `pRemote` is NULL). Nothing changes on the runtime until bmLiveMasterApply.
bool bmLiveMasterPushPack(BmLinkMaster *pMaster, const uint8_t *pPack, uint32_t size, uint32_t transferId,
                          const BmLiveRemote *pRemote, BmLivePushStats *pStats);

/// Applies a transfer (restart/hot) or the state sent with bmLiveMasterPutState (mode state).
/// True if the runtime answered; check pStatus->result.
bool bmLiveMasterApply(BmLinkMaster *pMaster, uint32_t transferId, BmLiveMode mode, const char *roomId,
                       BmLiveStatus *pStatus);

bool bmLiveMasterGetState(BmLinkMaster *pMaster, uint8_t flagMask, uint8_t *pOut, uint32_t capacity, uint32_t *pSize);
/// Reads the runtime's video state (a BMPS PPU snapshot, src/ppu/ppu.h: registers, VRAM,
/// palettes, OAM, line tables) into `pOut`; about 100 KB.
bool bmLiveMasterPpuSnapshot(BmLinkMaster *pMaster, uint8_t *pOut, uint32_t capacity, uint32_t *pSize);

bool bmLiveMasterPutState(BmLinkMaster *pMaster, const uint8_t *pData, uint32_t size, BmLiveStatus *pStatus);
