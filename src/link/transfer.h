/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Device to device (docs/spec/link-protocol.md §9): this device, as the side that makes the
// clock, brings another one to where it is: the runtime if the other has none (through the
// BIOS's multiboot and the bootstrap), the content it lacks, and the `link` part of the game
// state. The other side needs nothing special: a running runtime is the ordinary live-link
// slave, and a GBA without a cartridge waits in its BIOS.

#include <stdbool.h>
#include <stdint.h>

#include "live.h"

typedef enum BmTransferResult {
  BM_TRANSFER_OK = 0,
  BM_TRANSFER_NO_PEER,        // nobody answers, and there are no images to boot one with
  BM_TRANSFER_BOOT_FAILED,    // the BIOS did not take the bootstrap
  BM_TRANSFER_RUNTIME_FAILED, // the bootstrap did not take the runtime
  BM_TRANSFER_NO_RUNTIME,     // the runtime did not come up after being sent
  BM_TRANSFER_LINK_FAILED,
  BM_TRANSFER_PACK_REFUSED,
  BM_TRANSFER_STATE_REFUSED,
} BmTransferResult;

/// What to boot a device without a runtime with (both multiboot images; NULL: cannot).
typedef struct BmTransferImages
{
  const uint8_t *pBootstrap;   // sent through the BIOS (src/hal/gba/bootstrap)
  uint32_t bootstrapSize;
  const uint8_t *pRuntime;     // sent through the bootstrap
  uint32_t runtimeSize;
} BmTransferImages;

typedef struct BmTransferStats
{
  uint8_t usedMultiboot, usedBootstrap, sentPack;
  uint8_t sentSmaller;         // bmTransferSendWith: the smaller pack went
  uint32_t runtimeTries;       // times the runtime image was sent
  uint32_t runtimeCycles;      // processor cycles sending it took (0 where time is not measured)
  uint32_t chunksSent, bytesSent;
  uint32_t words;              // words exchanged in all
  BmLiveStatus peer;           // the other side after taking the state
} BmTransferStats;

/// Sends. `pPack` is the pack running here, `pState` the state to hand over (bmStateWrite with
/// BM_STATE_LINK), `room` the room it is in: where the other side takes less than the whole
/// pack (a multiboot GBA), the pack is fitted around it (bmLiveMasterPushPackFor). The serial
/// port must not be listening (bmHalLinkStop); it is left idle.
BmTransferResult bmTransferSend(const BmTransferImages *pImages, const uint8_t *pPack, uint32_t packSize, int32_t room,
                                const uint8_t *pState, uint32_t stateSize, BmTransferStats *pStats);
/// The same with a smaller pack (`pSmaller`, its rooms numbered as the first's) for a side that
/// does not run `pPack` and could not hold it (a multiboot GBA): that one is sent instead.
BmTransferResult bmTransferSendWith(const BmTransferImages *pImages, const uint8_t *pPack, uint32_t packSize,
                                    const uint8_t *pSmaller, uint32_t smallerSize, int32_t room,
                                    const uint8_t *pState, uint32_t stateSize, BmTransferStats *pStats);
