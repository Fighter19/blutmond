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
// Packets arrive in the serial interrupt (bmLiveHandler), which only keeps them in the link's
// receive buffers; bmLiveFrame, called once per frame, works through them in order. So the
// interrupt stays short enough not to miss words; a slow frame only delays acknowledgements.

#include "hal.h"
#include "link.h"
#include "pack.h"
#include "state.h"

#define BM_LIVE_RUNTIME_VERSION 1
#define BM_LIVE_MAX_CHUNKS 512
#define BM_LIVE_STATE_CAPACITY 1024

// Input mirror (below): key changes kept on each side, and how often the state is compared.
#define BM_LIVE_INPUT_CHANGES 16
#define BM_LIVE_FOLLOW_CHANGES 64
#define BM_LIVE_INPUT_CHECK_EVERY 64

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
  bool tocComplete, transferFailed;
  uint8_t present[BM_LIVE_MAX_CHUNKS / 8];

  // Packets kept by the interrupt, handled at the frame boundary in this order.
  BmLinkPacket *pending[BM_LINK_SLAVE_RX_SLOTS];
  uint8_t pendingCount;

  uint8_t statePut[BM_LIVE_STATE_CAPACITY];
  uint32_t statePutSize;

  // PPU_SNAPSHOT being sent: parts from snapshotOffset up to snapshotEnd, as the send ring
  // has room (a few per frame).
  uint32_t snapshotOffset, snapshotEnd;

  // Debugging (link-protocol.md §8).
  bool debugAvailable;        // the device can be stopped (bmHalDebugInit)
  bool debugStopped;          // the program is stopped; only GDB packets are handled
  bool debugHalt;             // stop at the next frame boundary
  bool debugNotify;           // the "stopped" packet is still to be sent
  int8_t debugSlot;           // the breakpoint it stopped at, or -1
  BmHalDebugRegs *pDebugRegs; // while stopped
  uint32_t debugStops;        // times the program stopped (for tests)

  uint8_t resetIn;            // frames until the device starts over (RESET), 0: not asked

  // Input mirror, this device reporting (link-protocol.md §10): key changes not yet sent.
  bool inputStream;           // asked for
  bool inputFirst;            // the next frame's keys are reported whatever they are
  bool inputLost;             // more changes than fit while nobody listened
  uint16_t inputKeys;         // keys of the last frame reported
  uint32_t inputThrough;      // last frame whose keys are known to this side
  uint8_t inputCount, inputQuiet;
  struct { uint32_t frame; uint16_t keys; } inputChanges[BM_LIVE_INPUT_CHANGES];
  bool inputHasCheck;
  uint32_t inputCheckFrame, inputCheckCrc;

  // Input mirror, this device following: key changes received, oldest first.
  bool following;
  uint8_t followCount;
  uint16_t followIdle;
  uint16_t followKeys;        // keys at the frame last asked for
  uint32_t followThrough;     // keys are known up to this frame
  struct { uint32_t frame; uint16_t keys; } followChanges[BM_LIVE_FOLLOW_CHANGES];
  bool followHasCheck;
  uint32_t followCheckFrame, followCheckCrc;
  uint32_t followChecks, followMismatches;
  uint32_t followBehindAt;    // frame at which "too far behind" was last reported
  uint32_t followJumps;       // times it was

  BmLiveRegion regions[BM_LIVE_REGIONS];
  uint32_t applied, chunksCopied, chunksReceived;
} BmLive;

/// `pBuffers` are two buffers of `capacity` bytes for packs; `pPack` is the pack running now.
void bmLiveInit(BmLive *pLive, const BmLiveHooks *pHooks, uint8_t *pBuffer0, uint8_t *pBuffer1, uint32_t capacity,
                const BmPack *pPack);

/// Starts listening on the serial port.
void bmLiveStart(BmLive *pLive);

/// The packet handler (serial interrupt). Public for tests that drive the slave directly.
BmLinkTake bmLiveHandler(void *pContext, BmLinkPacket *pPacket);

/// Call once per frame, between game frames: handles the packets received since the last
/// call (chunk data, state, HELLO, APPLY, ...).
void bmLiveFrame(BmLive *pLive);

/// True while a PPU snapshot is being sent. The game should stand still meanwhile (skip its
/// frame, keep calling bmLiveFrame), so the snapshot shows one moment and not a mix of frames.
static inline bool bmLiveCapturing(const BmLive *pLive)
{
  return pLive->snapshotOffset < pLive->snapshotEnd;
}

// --- Input mirror (link-protocol.md §10) ------------------------------------------------------
// One device reports which keys it used for which game frame; another one that started from
// the same state uses the same keys for the same frames and so plays the same game, a little
// behind. Every BM_LIVE_INPUT_CHECK_EVERY frames a checksum of the state goes along, so a
// difference is noticed.

/// INPUT from the master: one byte, 1 start reporting, 0 stop (reporting and following),
/// 3 get ready to follow (hold still until keys arrive). From the reporting device (and
/// passed on to the following one): u32 last frame whose keys are known, u8 number of
/// changes, u8 flags, u16 0; then per change u32 frame, u16 keys, u16 0; with flag 2 then
/// u32 frame and u32 checksum of the state before that frame. From the following device:
/// byte 2, u8 reason (0 its state differed, 1 too far behind), u8 0 x2, u32 the frame.
#define BM_LIVE_INPUT_HEADER 8
#define BM_LIVE_INPUT_FLAG_LOST 1
#define BM_LIVE_INPUT_FLAG_CHECK 2

/// Reporting side: call before each game frame with the keys it will use.
void bmLiveInput(BmLive *pLive, uint32_t frame, uint16_t keys);
/// Reporting side: whether a state checksum is wanted before frame `frame`; and giving it.
bool bmLiveInputWantsCheck(const BmLive *pLive, uint32_t frame);
void bmLiveInputCheck(BmLive *pLive, uint32_t frame, uint32_t crc);

/// Following side: true while another device's keys arrive.
static inline bool bmLiveFollowing(const BmLive *pLive)
{
  return pLive->following;
}
/// Following side: the keys for frame `frame`. False if they are not known yet (wait).
bool bmLiveFollowKeys(BmLive *pLive, uint32_t frame, uint16_t *pKeys);
/// How many frames the keys are known ahead of `frame` (to catch up when behind).
uint32_t bmLiveFollowAhead(const BmLive *pLive, uint32_t frame);
/// Following side: whether the reporting device sent a checksum for frame `frame` (so the
/// state's checksum is only worked out when it is needed: that takes time).
bool bmLiveFollowWantsCheck(BmLive *pLive, uint32_t frame);
/// Following side: compares `crc` (the state before frame `frame` here) with the reporting
/// device's and reports a difference to the master.
void bmLiveFollowCheck(BmLive *pLive, uint32_t frame, uint32_t crc);
/// Following side: more than BM_LIVE_FOLLOW_MAX_BEHIND frames behind and not catching up.
/// Tells the master, which then loads the other side's state here: a jump instead of a
/// delay that grows.
void bmLiveFollowBehind(BmLive *pLive, uint32_t frame);
#define BM_LIVE_FOLLOW_MAX_BEHIND 45

/// Sends a LOG packet (dropped if the link is busy).
bool bmLiveLog(BmLive *pLive, const char *pText);

// --- Payloads shared with the master ------------------------------------------------------

/// TOC: transfer id, total pack size, part and part count, then header and table bytes.
#define BM_LIVE_TOC_HEADER 12
/// CHUNK_DATA: transfer id, chunk index, reserved, offset in the chunk, then data.
#define BM_LIVE_CHUNK_DATA_HEADER 12
#define BM_LIVE_CHUNK_DATA_MAX (BM_LINK_MAX_PAYLOAD - BM_LIVE_CHUNK_DATA_HEADER)

/// PPU_SNAPSHOT: request u32 offset, u32 length (0: to the end); each answer u32 offset,
/// u32 total size, then data.
#define BM_LIVE_SNAPSHOT_HEADER 8
#define BM_LIVE_SNAPSHOT_DATA_MAX (BM_LINK_MAX_PAYLOAD - BM_LIVE_SNAPSHOT_HEADER)

/// GDB (debugging): byte 0 is the command, byte 1 of an answer its result (0: done), byte 2
/// a breakpoint slot, data from byte 4 (link-protocol.md §8).
#define BM_LIVE_DEBUG_HEADER 4
#define BM_LIVE_DEBUG_DATA_MAX 512
enum {
  BM_DEBUG_INFO = 'i',       // answer: byte 1 stopped, byte 2 breakpoint slots, byte 3 can stop
  BM_DEBUG_HALT = 'h',       // stop at the next frame boundary; answered by STOPPED
  BM_DEBUG_CONTINUE = 'c',
  BM_DEBUG_READ_REGS = 'g',  // answer: r0-r15, cpsr (17 words)
  BM_DEBUG_WRITE_REGS = 'G',
  BM_DEBUG_READ_MEM = 'm',   // u32 address at 4, u16 length at 8
  BM_DEBUG_WRITE_MEM = 'M',  // u32 address at 4, data from 8
  BM_DEBUG_SET_BREAK = 'Z',  // slot in byte 2, u32 address at 4
  BM_DEBUG_CLEAR_BREAK = 'z',
  BM_DEBUG_STOPPED = 'S',    // from the runtime: byte 1 the reason (0 asked to, 1 breakpoint), byte 2 the slot
};
enum { BM_DEBUG_OK = 0, BM_DEBUG_ERROR_RUNNING = 1, BM_DEBUG_ERROR_MEMORY = 2, BM_DEBUG_ERROR_UNSUPPORTED = 3 };

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
