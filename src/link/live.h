/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Live reload on the runtime side (docs/spec/runtime-abi.md §3 and §5, link-protocol.md §3):
// receives packs over the link into a content buffer, copies the chunks it already has, and
// applies the result at a frame boundary without a reset. Also answers HELLO, PING,
// STATE_GET and takes STATE_PUT.
//
// Packets arrive in the serial interrupt (bmLiveHandler); the parts that touch the game run
// in bmLiveFrame, which the game calls once per frame.

#include "link.h"
#include "pack.h"
#include "state.h"

#define BM_LIVE_RUNTIME_VERSION 1
#define BM_LIVE_MAX_CHUNKS 512
#define BM_LIVE_STATE_CAPACITY 1024
#define BM_LIVE_DEFERRED 4

/// Reload modes (runtime-abi.md §5).
typedef enum BmLiveMode {
  BM_LIVE_RESTART = 0,
  BM_LIVE_HOT = 1,
  BM_LIVE_HOT_MIGRATE = 2,
  BM_LIVE_MODULE = 3,
  BM_LIVE_STATE = 4,
} BmLiveMode;

/// Results in a STATUS answer to APPLY.
typedef enum BmLiveResult {
  BM_LIVE_OK = 0,
  BM_LIVE_ERROR_TRANSFER,     // unknown transfer, or chunks missing
  BM_LIVE_ERROR_CRC,          // the assembled pack does not match its checksum
  BM_LIVE_ERROR_TOO_LARGE,    // does not fit into a content buffer
  BM_LIVE_ERROR_PACK,         // the game refused the pack
  BM_LIVE_ERROR_STATE,        // state data of another layout
  BM_LIVE_ERROR_UNSUPPORTED,  // mode not available yet
} BmLiveResult;

/// What the game does for the live module.
typedef struct BmLiveHooks
{
  void *pContext;
  /// The state blocks (state.md) for STATE_GET/STATE_PUT and snapshots.
  uint32_t (*stateBlocks)(void *pContext, BmStateBlock *pBlocks, uint32_t max);
  /// Switch to a new pack: restart (state reset, `roomId` or the first room) or hot (state
  /// kept, the current room reloaded around the player). `pPack` stays valid until the next
  /// successful call.
  bool (*usePack)(void *pContext, const BmPack *pPack, BmLiveMode mode, const char *roomId);
  /// After state was loaded: rebuild what is derived (bmGameAfterLoad).
  bool (*afterStateLoad)(void *pContext);
  /// Current frame, room and coarse player position for STATUS.
  void (*status)(void *pContext, uint32_t *pFrame, uint32_t *pRoomIndex, int16_t *pX, int16_t *pY);
} BmLiveHooks;

/// Region table (runtime-abi.md §3): what the runtime keeps and what content owns.
typedef enum BmRegionOwner { BM_REGION_RUNTIME = 0, BM_REGION_CONTENT = 1 } BmRegionOwner;
typedef struct BmLiveRegion
{
  const char *name;
  void *pBase;
  uint32_t size;
  uint8_t owner;              // BmRegionOwner
} BmLiveRegion;
#define BM_LIVE_REGIONS 4

typedef struct BmLive
{
  BmLinkSlave slave;
  BmLiveHooks hooks;

  // Two content buffers; the active pack is in one of them or elsewhere (ROM).
  uint8_t *pBuffers[2];
  uint32_t capacity;
  BmPack active;
  int8_t activeBuffer;        // -1: the active pack is not in a content buffer

  // Pack transfer into the buffer that is not active.
  uint32_t transferId;
  uint32_t totalSize;
  uint8_t *pStaging;
  uint32_t tocStreamSize, tocReceived;
  uint32_t tocOffset;
  uint32_t chunkCount;
  bool tocComplete, copyPending, transferFailed;
  uint8_t present[BM_LIVE_MAX_CHUNKS / 8];

  // Packets handled at the frame boundary.
  BmLinkPacket deferred[BM_LIVE_DEFERRED];
  uint8_t deferredCount;

  uint8_t statePut[BM_LIVE_STATE_CAPACITY];
  uint32_t statePutSize;

  BmLiveRegion regions[BM_LIVE_REGIONS];
  uint32_t applied, chunksCopied, chunksReceived;
} BmLive;

/// `pBuffers` are two buffers of `capacity` bytes for packs; `pPack` is the pack running now.
void bmLiveInit(BmLive *pLive, const BmLiveHooks *pHooks, uint8_t *pBuffer0, uint8_t *pBuffer1, uint32_t capacity,
                const BmPack *pPack);

/// Starts listening on the serial port.
void bmLiveStart(BmLive *pLive);

/// The packet handler (serial interrupt). Public for tests that drive the slave directly.
bool bmLiveHandler(void *pContext, const BmLinkPacket *pPacket);

/// Call once per frame, between game frames: handles HELLO, STATE_GET, APPLY and copies
/// resident chunks.
void bmLiveFrame(BmLive *pLive);

/// Sends a LOG packet (dropped if the link is busy).
bool bmLiveLog(BmLive *pLive, const char *pText);

// --- Payloads shared with the master ------------------------------------------------------

/// TOC: transfer id, total pack size, part and part count, then header and table bytes.
#define BM_LIVE_TOC_HEADER 12
/// CHUNK_DATA: transfer id, chunk index, reserved, offset in the chunk, then data.
#define BM_LIVE_CHUNK_DATA_HEADER 12
#define BM_LIVE_CHUNK_DATA_MAX (BM_LINK_MAX_PAYLOAD - BM_LIVE_CHUNK_DATA_HEADER)

/// HELLO answer, fixed part (followed by `hashCount` pairs of u32 hash and u32 size).
typedef struct BmLiveHello
{
  uint16_t protocolVersion, runtimeVersion;
  uint16_t abiMajor, abiMinor;
  uint32_t profile;
  uint32_t contentCapacity;
  uint32_t activeSize;
  uint32_t activeCrc;
  uint32_t chunkCount;        // chunks of the active pack
  uint32_t hashCount;         // pairs that follow (may be fewer than chunkCount)
} BmLiveHello;
#define BM_LIVE_HELLO_SIZE 32
#define BM_LIVE_HELLO_MAX_HASHES ((BM_LINK_MAX_PAYLOAD - BM_LIVE_HELLO_SIZE) / 8)

/// STATUS: kind (0 report, 1 answer to APPLY), result, mode, then transfer id, frame, room,
/// x, y, applied count.
typedef struct BmLiveStatus
{
  uint8_t kind, result, mode;
  uint32_t transferId;
  uint32_t frame;
  uint32_t roomIndex;
  int16_t x, y;
  uint32_t applied;
} BmLiveStatus;
#define BM_LIVE_STATUS_SIZE 24

void bmLiveEncodeStatus(const BmLiveStatus *pStatus, uint8_t *pOut);
void bmLiveDecodeStatus(const uint8_t *pIn, BmLiveStatus *pStatus);
void bmLiveDecodeHello(const uint8_t *pIn, BmLiveHello *pHello);
