/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "live.h"

#include "hal.h"
#include "shared.h"

#include <string.h>

BM_IWRAM_CODE static uint32_t exchange(void *pContext, uint32_t received)
{
  return bmLinkSlaveExchange(&((BmLive *)pContext)->slave, received);
}

static void debugStopped(void *pContext, BmHalDebugRegs *pRegs, int slot);

void bmLiveInit(BmLive *l, const BmLiveHooks *pHooks, uint8_t *pBuffer0, uint8_t *pBuffer1, uint32_t capacity,
                const BmPack *pPack)
{
  memset(l, 0, sizeof(*l));
  bmLinkSlaveInit(&l->slave, bmLiveHandler, l);
  l->hooks = *pHooks;
  l->pBuffers[0] = pBuffer0;
  l->pBuffers[1] = pBuffer1;
  l->capacity = capacity;
  l->active = *pPack;
  l->activeBuffer = -1;
  l->regions[0] = (BmLiveRegion){ "link", l, (uint32_t)sizeof(*l), BM_REGION_RUNTIME };
  l->regions[1] = (BmLiveRegion){ "content 0", pBuffer0, capacity, BM_REGION_CONTENT };
  l->regions[2] = (BmLiveRegion){ "content 1", pBuffer1, capacity, BM_REGION_CONTENT };
  l->regions[3] = (BmLiveRegion){ "state put", l->statePut, sizeof(l->statePut), BM_REGION_CONTENT };
  l->debugSlot = -1;
  l->debugAvailable = bmHalDebugInit(debugStopped, l);
}

void bmLiveStart(BmLive *l)
{
  bmHalLinkStart(exchange, l, BM_LINK_WORD_IDLE);
}

// --- Serial interrupt ----------------------------------------------------------------------

BM_IWRAM_CODE BmLinkTake bmLiveHandler(void *pContext, BmLinkPacket *p)
{
  BmLive *l = pContext;
  // Keep it for bmLiveFrame; one buffer must stay free for receiving.
  if (l->pendingCount >= BM_LINK_SLAVE_RX_SLOTS || bmLinkSlaveRxFree(&l->slave) < 2) return BM_LINK_REFUSED;
  l->pending[l->pendingCount++] = p;
  return BM_LINK_KEPT;
}

// --- Frame boundary ------------------------------------------------------------------------

static bool send(BmLive *l, uint8_t type, const void *pPayload, uint16_t length)
{
  uint32_t lock = bmHalLinkLock();
  BmLinkPacket *p = bmLinkSlaveSendBegin(&l->slave);
  bmHalLinkUnlock(lock);
  if (p == NULL) return false;
  p->type = type;
  p->length = length;
  if (length != 0) memcpy(p->payload, pPayload, length);
  lock = bmHalLinkLock();
  bmLinkSlaveSendCommit(&l->slave);
  bmHalLinkUnlock(lock);
  return true;
}

static bool chunkEntry(const BmLive *l, uint32_t index, uint32_t *pOffset, uint32_t *pSize)
{
  if (!l->tocComplete || index >= l->chunkCount) return false;
  const uint8_t *e = l->pStaging + l->tocOffset + index * BM_PACK_CHUNK_ENTRY_SIZE;
  *pOffset = bmRead32(e + 4);
  *pSize = bmRead32(e + 8);
  return *pOffset <= l->totalSize && *pSize <= l->totalSize - *pOffset;
}

/// Copies the chunks of the new pack that the running one already has (same hash and size).
static void copyResidentChunks(BmLive *l)
{
  uint32_t activeCount = bmPackChunkCount(&l->active);
  for (uint32_t i = 0; i < l->chunkCount; i++)
  {
    const uint8_t *e = l->pStaging + l->tocOffset + i * BM_PACK_CHUNK_ENTRY_SIZE;
    uint32_t offset = bmRead32(e + 4), size = bmRead32(e + 8), hash = bmRead32(e + 20);
    if (offset > l->totalSize || size > l->totalSize - offset) continue;
    for (uint32_t k = 0; k < activeCount; k++)
    {
      BmChunkEntry a;
      if (!bmPackGetChunk(&l->active, k, &a) || a.hash != hash || a.size != size) continue;
      memcpy(l->pStaging + offset, l->active.pData + a.offset, size);
      l->present[i / 8] |= (uint8_t)(1u << (i % 8));
      l->chunksCopied++;
      break;
    }
  }
}

static void receiveToc(BmLive *l, const BmLinkPacket *p)
{
  if (p->length < BM_LIVE_TOC_HEADER) return;
  uint32_t id = bmLinkGet32(p->payload);
  uint32_t total = bmLinkGet32(p->payload + 4);
  uint16_t part = bmLinkGet16(p->payload + 8);
  uint16_t parts = bmLinkGet16(p->payload + 10);
  const uint8_t *bytes = p->payload + BM_LIVE_TOC_HEADER;
  uint32_t n = p->length - BM_LIVE_TOC_HEADER;
  if (part == 0)
  {
    // A new transfer: into the buffer that does not hold the running pack.
    l->transferId = id;
    l->totalSize = total;
    l->tocReceived = 0;
    l->tocComplete = false;
    l->transferFailed = total > l->capacity || n < BM_PACK_HEADER_SIZE;
    l->pStaging = l->pBuffers[l->activeBuffer == 0 ? 1 : 0];
    memset(l->present, 0, sizeof(l->present));
    if (l->transferFailed) return;
    l->tocOffset = bmRead32(bytes + 24);
    l->chunkCount = bmRead32(bytes + 20);
    l->tocStreamSize = BM_PACK_HEADER_SIZE + l->chunkCount * BM_PACK_CHUNK_ENTRY_SIZE;
    if (l->chunkCount > BM_LIVE_MAX_CHUNKS || l->tocOffset < BM_PACK_HEADER_SIZE ||
        l->tocOffset > total || l->chunkCount * BM_PACK_CHUNK_ENTRY_SIZE > total - l->tocOffset)
    {
      l->transferFailed = true;
      return;
    }
    // The padding between chunks belongs to the pack's checksum and must be zero; the buffer
    // may still hold an older pack whose chunks lay elsewhere.
    memset(l->pStaging, 0, total);
  }
  if (id != l->transferId || l->transferFailed) return;
  for (uint32_t i = 0; i < n && l->tocReceived < l->tocStreamSize; i++, l->tocReceived++)
  {
    uint32_t pos = l->tocReceived;
    uint32_t at = pos < BM_PACK_HEADER_SIZE ? pos : l->tocOffset + pos - BM_PACK_HEADER_SIZE;
    l->pStaging[at] = bytes[i];
  }
  if (part + 1u == parts)
  {
    l->tocComplete = l->tocReceived == l->tocStreamSize;
    l->transferFailed = !l->tocComplete;
    if (l->tocComplete) copyResidentChunks(l);
  }
}

static void receiveChunkData(BmLive *l, const BmLinkPacket *p)
{
  if (p->length < BM_LIVE_CHUNK_DATA_HEADER || bmLinkGet32(p->payload) != l->transferId) return;
  uint32_t index = bmLinkGet16(p->payload + 4);
  uint32_t at = bmLinkGet32(p->payload + 8);
  uint32_t n = p->length - BM_LIVE_CHUNK_DATA_HEADER;
  uint32_t offset, size;
  if (!chunkEntry(l, index, &offset, &size) || at > size || n > size - at)
  {
    l->transferFailed = true;
    return;
  }
  memcpy(l->pStaging + offset + at, p->payload + BM_LIVE_CHUNK_DATA_HEADER, n);
}

static bool sendHello(BmLive *l)
{
  static uint8_t out[BM_LINK_MAX_PAYLOAD] BM_EWRAM_BSS;
  const BmPackHeader *h = &l->active.header;
  uint32_t count = bmPackChunkCount(&l->active);
  uint32_t hashes = count < BM_LIVE_HELLO_MAX_HASHES ? count : BM_LIVE_HELLO_MAX_HASHES;
  bmLinkPut16(out + 0, BM_LINK_PROTOCOL_VERSION);
  bmLinkPut16(out + 2, BM_LIVE_RUNTIME_VERSION);
  bmLinkPut16(out + 4, h->abiMajor);
  bmLinkPut16(out + 6, h->abiMinor);
  bmLinkPut32(out + 8, h->profile);
  bmLinkPut32(out + 12, l->capacity);
  bmLinkPut32(out + 16, l->active.size);
  bmLinkPut32(out + 20, h->crc32);
  bmLinkPut32(out + 24, count);
  bmLinkPut32(out + 28, hashes);
  for (uint32_t i = 0; i < hashes; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&l->active, i, &e);
    bmLinkPut32(out + BM_LIVE_HELLO_SIZE + i * 8, e.hash);
    bmLinkPut32(out + BM_LIVE_HELLO_SIZE + i * 8 + 4, e.size);
  }
  return send(l, BM_LINK_HELLO, out, (uint16_t)(BM_LIVE_HELLO_SIZE + hashes * 8));
}

static bool sendState(BmLive *l, uint8_t mask)
{
  static uint8_t out[BM_LIVE_STATE_CAPACITY] BM_EWRAM_BSS;
  BmStateBlock blocks[8];
  uint32_t n = l->hooks.stateBlocks(l->hooks.pContext, blocks, 8);
  uint32_t size = bmStateWrite(blocks, n, mask, out, sizeof(out));
  return send(l, BM_LINK_STATE_DATA, out, (uint16_t)size);
}

static BmLiveResult applyPack(BmLive *l, uint32_t id, BmLiveMode mode, const char *roomId)
{
  if (id != l->transferId || l->pStaging == NULL || !l->tocComplete || l->transferFailed) return BM_LIVE_ERROR_TRANSFER;
  for (uint32_t i = 0; i < l->chunkCount; i++)
    if (!(l->present[i / 8] & (1u << (i % 8)))) return BM_LIVE_ERROR_TRANSFER;
  BmPack pack;
  BmPackError e = bmPackOpen(&pack, l->pStaging, l->totalSize, true);
  if (e == BM_PACK_ERROR_CRC) return BM_LIVE_ERROR_CRC;
  if (e != BM_PACK_OK) return BM_LIVE_ERROR_PACK;
  if (!l->hooks.usePack(l->hooks.pContext, &pack, mode, roomId)) return BM_LIVE_ERROR_PACK;
  l->active = pack;
  l->activeBuffer = l->pStaging == l->pBuffers[0] ? 0 : 1;
  l->pStaging = NULL;
  l->tocComplete = false;
  return BM_LIVE_OK;
}

static BmLiveResult applyState(BmLive *l)
{
  BmStateBlock blocks[8];
  uint32_t n = l->hooks.stateBlocks(l->hooks.pContext, blocks, 8);
  if (l->statePutSize == 0 || bmStateRead(blocks, n, l->statePut, l->statePutSize) != BM_STATE_OK)
    return BM_LIVE_ERROR_STATE;
  l->statePutSize = 0;
  return l->hooks.afterStateLoad(l->hooks.pContext) ? BM_LIVE_OK : BM_LIVE_ERROR_STATE;
}

static bool handleApply(BmLive *l, const BmLinkPacket *p)
{
  if (bmLinkSlaveTxFree(&l->slave) == 0) return false;  // no room for the answer yet
  BmLiveStatus st = { 0 };
  st.kind = 1;
  st.transferId = p->length >= 4 ? bmLinkGet32(p->payload) : 0;
  st.mode = p->length >= 5 ? p->payload[4] : 0;
  char room[64] = { 0 };
  if (p->length > 8)
  {
    uint32_t n = p->length - 8u < sizeof(room) - 1 ? p->length - 8u : sizeof(room) - 1;
    memcpy(room, p->payload + 8, n);
  }
  switch (st.mode)
  {
  case BM_LIVE_RESTART:
  case BM_LIVE_HOT:
    st.result = (uint8_t)applyPack(l, st.transferId, (BmLiveMode)st.mode, room[0] ? room : NULL);
    break;
  case BM_LIVE_STATE:
    st.result = (uint8_t)applyState(l);
    break;
  default:
    st.result = BM_LIVE_ERROR_UNSUPPORTED;
    break;
  }
  if (st.result == BM_LIVE_OK) l->applied++;
  st.applied = l->applied;
  l->hooks.status(l->hooks.pContext, &st.frame, &st.roomIndex, &st.x, &st.y);
  uint8_t out[BM_LIVE_STATUS_SIZE];
  bmLiveEncodeStatus(&st, out);
  send(l, BM_LINK_STATUS, out, sizeof(out));
  return true;
}

// --- Input mirror (link-protocol.md §10) ------------------------------------------------------

// Calls of bmLiveFrame without keys from the other side after which following ends (about two
// seconds: the PC game calls it every millisecond while it waits, a device once per frame).
#ifdef BM_PLATFORM_HOST
#  define FOLLOW_TIMEOUT 2000
#else
#  define FOLLOW_TIMEOUT 120
#endif

void bmLiveInput(BmLive *l, uint32_t frame, uint16_t keys)
{
  if (!l->inputStream) return;
  if (l->inputFirst || keys != l->inputKeys)
  {
    if (l->inputCount < BM_LIVE_INPUT_CHANGES)
    {
      l->inputChanges[l->inputCount].frame = frame;
      l->inputChanges[l->inputCount].keys = keys;
      l->inputCount++;
    }
    else l->inputLost = true;
    l->inputFirst = false;
    l->inputKeys = keys;
  }
  l->inputThrough = frame;
  l->inputQuiet++;
}

bool bmLiveInputWantsCheck(const BmLive *l, uint32_t frame)
{
  return l->inputStream && frame % BM_LIVE_INPUT_CHECK_EVERY == 0;
}

void bmLiveInputCheck(BmLive *l, uint32_t frame, uint32_t crc)
{
  l->inputHasCheck = true;
  l->inputCheckFrame = frame;
  l->inputCheckCrc = crc;
}

// Sends what was collected: at once when a key changed, else every second frame so the other
// side knows how far it may play.
static void pumpInput(BmLive *l)
{
  if (!l->inputStream || (l->inputCount == 0 && !l->inputHasCheck && l->inputQuiet < 2)) return;
  uint32_t lock = bmHalLinkLock();
  BmLinkPacket *p = bmLinkSlaveSendBegin(&l->slave);
  bmHalLinkUnlock(lock);
  if (p == NULL) return;
  uint8_t *o = p->payload;
  bmLinkPut32(o, l->inputThrough);
  o[4] = l->inputCount;
  o[5] = (uint8_t)((l->inputLost ? BM_LIVE_INPUT_FLAG_LOST : 0) | (l->inputHasCheck ? BM_LIVE_INPUT_FLAG_CHECK : 0));
  o[6] = o[7] = 0;
  o += BM_LIVE_INPUT_HEADER;
  for (uint32_t i = 0; i < l->inputCount; i++, o += 8)
  {
    bmLinkPut32(o, l->inputChanges[i].frame);
    bmLinkPut16(o + 4, l->inputChanges[i].keys);
    bmLinkPut16(o + 6, 0);
  }
  if (l->inputHasCheck)
  {
    bmLinkPut32(o, l->inputCheckFrame);
    bmLinkPut32(o + 4, l->inputCheckCrc);
    o += 8;
  }
  p->type = BM_LINK_INPUT;
  p->length = (uint16_t)(o - p->payload);
  l->inputCount = 0;
  l->inputLost = false;
  l->inputHasCheck = false;
  l->inputQuiet = 0;
  lock = bmHalLinkLock();
  bmLinkSlaveSendCommit(&l->slave);
  bmHalLinkUnlock(lock);
}

static void receiveInput(BmLive *l, const BmLinkPacket *p)
{
  if (p->length == 1)
  {
    // Start or stop reporting.
    if (p->payload[0] == 3)
    {
      // About to follow: play nothing of its own from now on, until the first keys are there.
      l->following = true;
      l->followIdle = 0;
      l->followCount = 0;
      l->followHasCheck = false;
      l->followThrough = 0xFFFFFFFFu;   // before every frame
      return;
    }
    l->inputStream = p->payload[0] == 1;
    if (p->payload[0] == 0) l->following = false;
    l->inputFirst = true;
    l->inputCount = 0;
    l->inputLost = false;
    l->inputHasCheck = false;
    return;
  }
  if (p->length < BM_LIVE_INPUT_HEADER) return;
  // Another device's keys: this one follows.
  uint32_t count = p->payload[4], flags = p->payload[5];
  if (p->length < BM_LIVE_INPUT_HEADER + count * 8u + ((flags & BM_LIVE_INPUT_FLAG_CHECK) ? 8u : 0u)) return;
  if (!l->following)
  {
    l->following = true;
    l->followCount = 0;
    l->followHasCheck = false;
  }
  l->followIdle = 0;
  const uint8_t *in = p->payload + BM_LIVE_INPUT_HEADER;
  for (uint32_t i = 0; i < count; i++, in += 8)
  {
    if (l->followCount >= BM_LIVE_FOLLOW_CHANGES)
    {
      // Too far behind to keep them all: the oldest goes, the checksum will tell.
      for (uint32_t k = 1; k < l->followCount; k++) l->followChanges[k - 1] = l->followChanges[k];
      l->followCount--;
    }
    l->followChanges[l->followCount].frame = bmLinkGet32(in);
    l->followChanges[l->followCount].keys = bmLinkGet16(in + 4);
    l->followCount++;
  }
  if (flags & BM_LIVE_INPUT_FLAG_CHECK)
  {
    l->followHasCheck = true;
    l->followCheckFrame = bmLinkGet32(in);
    l->followCheckCrc = bmLinkGet32(in + 4);
  }
  l->followThrough = bmLinkGet32(p->payload);
}

bool bmLiveFollowKeys(BmLive *l, uint32_t frame, uint16_t *pKeys)
{
  if ((int32_t)(l->followThrough - frame) < 0) return false;
  // The changes up to this frame are used up.
  uint32_t used = 0;
  while (used < l->followCount && (int32_t)(l->followChanges[used].frame - frame) <= 0)
  {
    l->followKeys = l->followChanges[used].keys;
    used++;
  }
  if (used != 0)
  {
    for (uint32_t k = used; k < l->followCount; k++) l->followChanges[k - used] = l->followChanges[k];
    l->followCount = (uint8_t)(l->followCount - used);
  }
  *pKeys = l->followKeys;
  return true;
}

uint32_t bmLiveFollowAhead(const BmLive *l, uint32_t frame)
{
  int32_t ahead = (int32_t)(l->followThrough - frame);
  return ahead > 0 ? (uint32_t)ahead : 0;
}

bool bmLiveFollowWantsCheck(BmLive *l, uint32_t frame)
{
  // A checksum for a frame already passed (a jump) is of no use any more.
  if (l->followHasCheck && (int32_t)(l->followCheckFrame - frame) < 0) l->followHasCheck = false;
  return l->followHasCheck && l->followCheckFrame == frame;
}

void bmLiveFollowBehind(BmLive *l, uint32_t frame)
{
  // Once, then again only if it is still so two seconds later.
  if (l->followBehindAt != 0 && frame - l->followBehindAt < 120) return;
  uint8_t out[8] = { 2, 1, 0, 0 };
  bmLinkPut32(out + 4, frame);
  if (!send(l, BM_LINK_INPUT, out, sizeof(out))) return;
  l->followBehindAt = frame != 0 ? frame : 1;
  l->followJumps++;
}

void bmLiveFollowCheck(BmLive *l, uint32_t frame, uint32_t crc)
{
  if (!l->followHasCheck || l->followCheckFrame != frame) return;
  l->followHasCheck = false;
  l->followChecks++;
  if (crc == l->followCheckCrc) return;
  l->followMismatches++;
  uint8_t out[8] = { 2, 0, 0, 0 };
  bmLinkPut32(out + 4, frame);
  send(l, BM_LINK_INPUT, out, sizeof(out));
}

// --- Debugging (link-protocol.md §8) --------------------------------------------------------

static bool debugAnswer(BmLive *l, uint8_t command, uint8_t result, uint8_t slot, const void *pData, uint16_t length)
{
  uint32_t lock = bmHalLinkLock();
  BmLinkPacket *p = bmLinkSlaveSendBegin(&l->slave);
  bmHalLinkUnlock(lock);
  if (p == NULL) return false;
  p->type = BM_LINK_GDB;
  p->length = (uint16_t)(BM_LIVE_DEBUG_HEADER + length);
  p->payload[0] = command;
  p->payload[1] = result;
  p->payload[2] = slot;
  p->payload[3] = 0;
  if (length != 0) memcpy(p->payload + BM_LIVE_DEBUG_HEADER, pData, length);
  lock = bmHalLinkLock();
  bmLinkSlaveSendCommit(&l->slave);
  bmHalLinkUnlock(lock);
  return true;
}

/// One debugger command; false if the answer has to wait for room.
static bool handleDebug(BmLive *l, const BmLinkPacket *p)
{
  if (p->length < 1) return true;
  uint8_t command = p->payload[0], slot = p->length > 2 ? p->payload[2] : 0;
  uint32_t address = p->length >= 8 ? bmLinkGet32(p->payload + 4) : 0;
  if (!l->debugAvailable && command != BM_DEBUG_INFO) return debugAnswer(l, command, BM_DEBUG_ERROR_UNSUPPORTED, 0, NULL, 0);
  switch (command)
  {
  case BM_DEBUG_INFO:
  {
    uint8_t info[4] = { l->debugStopped, BM_HAL_DEBUG_SLOTS, l->debugAvailable, 0 };
    return debugAnswer(l, command, BM_DEBUG_OK, 0, info, sizeof(info));
  }
  case BM_DEBUG_HALT:
    if (l->debugStopped) l->debugNotify = true;   // it is: say so again
    else l->debugHalt = true;
    return true;
  case BM_DEBUG_CONTINUE:
    if (!debugAnswer(l, command, l->debugStopped ? BM_DEBUG_OK : BM_DEBUG_ERROR_RUNNING, 0, NULL, 0)) return false;
    l->debugStopped = false;
    return true;
  case BM_DEBUG_READ_REGS:
  {
    uint8_t out[17 * 4];
    if (!l->debugStopped) return debugAnswer(l, command, BM_DEBUG_ERROR_RUNNING, 0, NULL, 0);
    for (uint32_t i = 0; i < 16; i++) bmLinkPut32(out + i * 4, l->pDebugRegs->r[i]);
    bmLinkPut32(out + 64, l->pDebugRegs->cpsr);
    return debugAnswer(l, command, BM_DEBUG_OK, 0, out, sizeof(out));
  }
  case BM_DEBUG_WRITE_REGS:
    // Writing is done before the answer: if the answer has to wait, doing it again is harmless.
    if (!l->debugStopped || p->length < BM_LIVE_DEBUG_HEADER + 17 * 4) return debugAnswer(l, command, BM_DEBUG_ERROR_RUNNING, 0, NULL, 0);
    for (uint32_t i = 0; i < 16; i++) l->pDebugRegs->r[i] = bmLinkGet32(p->payload + BM_LIVE_DEBUG_HEADER + i * 4);
    l->pDebugRegs->cpsr = bmLinkGet32(p->payload + BM_LIVE_DEBUG_HEADER + 64);
    return debugAnswer(l, command, BM_DEBUG_OK, 0, NULL, 0);
  case BM_DEBUG_READ_MEM:
  {
    uint8_t data[BM_LIVE_DEBUG_DATA_MAX];
    uint32_t length = p->length >= 10 ? bmLinkGet16(p->payload + 8) : 0;
    if (length > sizeof(data) || !bmHalDebugRead(address, data, length)) return debugAnswer(l, command, BM_DEBUG_ERROR_MEMORY, 0, NULL, 0);
    return debugAnswer(l, command, BM_DEBUG_OK, 0, data, (uint16_t)length);
  }
  case BM_DEBUG_WRITE_MEM:
    return debugAnswer(l, command, p->length > 8 && bmHalDebugWrite(address, p->payload + 8, p->length - 8u) ? BM_DEBUG_OK : BM_DEBUG_ERROR_MEMORY, 0, NULL, 0);
  case BM_DEBUG_SET_BREAK:
    // Setting it twice is harmless: the slot is cleared first.
    return debugAnswer(l, command, p->length >= 8 && bmHalDebugSetBreak(slot, address) ? BM_DEBUG_OK : BM_DEBUG_ERROR_MEMORY, slot, NULL, 0);
  case BM_DEBUG_CLEAR_BREAK:
    bmHalDebugClearBreak(slot);
    return debugAnswer(l, command, BM_DEBUG_OK, slot, NULL, 0);
  default:
    return debugAnswer(l, command, BM_DEBUG_ERROR_UNSUPPORTED, 0, NULL, 0);
  }
}

static void dropPending(BmLive *l, BmLinkPacket *p)
{
  uint32_t lock = bmHalLinkLock();
  uint32_t i = 0;
  while (i < l->pendingCount && l->pending[i] != p) i++;
  if (i < l->pendingCount)
  {
    l->pendingCount--;
    for (; i < l->pendingCount; i++) l->pending[i] = l->pending[i + 1];
    bmLinkSlaveRelease(&l->slave, p);
  }
  bmHalLinkUnlock(lock);
}

// The program is stopped (hal.h): tell the debugger and do what it asks until it says
// continue. The serial interrupt keeps receiving; anything but debugger commands waits.
static void debugStopped(void *pContext, BmHalDebugRegs *pRegs, int slot)
{
  BmLive *l = pContext;
  l->pDebugRegs = pRegs;
  l->debugSlot = (int8_t)slot;
  l->debugStopped = true;
  l->debugNotify = true;
  l->debugStops++;
  while (l->debugStopped)
  {
    if (l->debugNotify && debugAnswer(l, BM_DEBUG_STOPPED, slot >= 0, (uint8_t)slot, NULL, 0)) l->debugNotify = false;
    uint32_t lock = bmHalLinkLock();
    BmLinkPacket *p = NULL;
    for (uint32_t i = 0; i < l->pendingCount && p == NULL; i++)
    {
      if (l->pending[i]->type == BM_LINK_GDB) p = l->pending[i];
    }
    bmHalLinkUnlock(lock);
    if (p != NULL && handleDebug(l, p)) dropPending(l, p);
  }
  l->pDebugRegs = NULL;
  l->debugSlot = -1;
}

/// Handles one received packet; false if it has to wait (no room to answer yet).
static bool handle(BmLive *l, const BmLinkPacket *p)
{
  switch (p->type)
  {
  case BM_LINK_PING:
    return send(l, BM_LINK_PING, p->payload, p->length);
  case BM_LINK_TOC:
    receiveToc(l, p);
    return true;
  case BM_LINK_CHUNK_DATA:
    receiveChunkData(l, p);
    return true;
  case BM_LINK_CHUNK_END:
    if (p->length >= 6 && bmLinkGet32(p->payload) == l->transferId)
    {
      uint32_t index = bmLinkGet16(p->payload + 4);
      if (index < BM_LIVE_MAX_CHUNKS) l->present[index / 8] |= (uint8_t)(1u << (index % 8));
      l->chunksReceived++;
    }
    return true;
  case BM_LINK_STATE_PUT:
    if (p->length <= sizeof(l->statePut))
    {
      memcpy(l->statePut, p->payload, p->length);
      l->statePutSize = p->length;
    }
    return true;
  case BM_LINK_HELLO:
    return sendHello(l);
  case BM_LINK_STATE_GET:
    return sendState(l, p->length >= 1 ? p->payload[0] : BM_STATE_SNAPSHOT);
  case BM_LINK_APPLY:
    return handleApply(l, p);
  case BM_LINK_GDB:
    return handleDebug(l, p);
  case BM_LINK_INPUT:
    receiveInput(l, p);
    return true;
  case BM_LINK_RESET:
    l->resetIn = 20;   // a few frames later: the acknowledgement has to get out first
    return true;
  case BM_LINK_PPU_SNAPSHOT:
  {
    uint32_t offset = p->length >= 4 ? bmLinkGet32(p->payload) : 0;
    uint32_t length = p->length >= 8 ? bmLinkGet32(p->payload + 4) : 0;
    uint8_t dummy;
    uint32_t total = bmHalVideoSnapshotPart(0, &dummy, 0);
    l->snapshotOffset = offset < total ? offset : total;
    l->snapshotEnd = length == 0 || length > total - l->snapshotOffset ? total : l->snapshotOffset + length;
    return true;
  }
  default:
    return true;  // not handled by this runtime (CHUNK_BEGIN carries nothing needed here)
  }
}

// Sends the next parts of a requested PPU snapshot, straight into the send ring.
static void pumpSnapshot(BmLive *l)
{
  while (l->snapshotOffset < l->snapshotEnd)
  {
    uint32_t lock = bmHalLinkLock();
    BmLinkPacket *p = bmLinkSlaveSendBegin(&l->slave);
    bmHalLinkUnlock(lock);
    if (p == NULL) return;
    uint32_t n = l->snapshotEnd - l->snapshotOffset;
    if (n > BM_LIVE_SNAPSHOT_DATA_MAX) n = BM_LIVE_SNAPSHOT_DATA_MAX;
    p->type = BM_LINK_PPU_SNAPSHOT;
    p->length = (uint16_t)(BM_LIVE_SNAPSHOT_HEADER + n);
    uint32_t total = bmHalVideoSnapshotPart(l->snapshotOffset, p->payload + BM_LIVE_SNAPSHOT_HEADER, n);
    bmLinkPut32(p->payload, l->snapshotOffset);
    bmLinkPut32(p->payload + 4, total);
    l->snapshotOffset += n;
    lock = bmHalLinkLock();
    bmLinkSlaveSendCommit(&l->slave);
    bmHalLinkUnlock(lock);
  }
}

void bmLiveFrame(BmLive *l)
{
  bmHalLinkRealign();
  if (l->resetIn != 0 && --l->resetIn == 0) bmHalReset();
  for (;;)
  {
    uint32_t lock = bmHalLinkLock();
    BmLinkPacket *p = l->pendingCount != 0 ? l->pending[0] : NULL;
    bmHalLinkUnlock(lock);
    if (p == NULL || !handle(l, p)) break;
    dropPending(l, p);
  }
  if (l->debugHalt)
  {
    l->debugHalt = false;
    bmHalDebugBreak();
  }
  pumpInput(l);
  // Following ends when it is told to, or when nothing has come for a long time.
  if (l->following && ++l->followIdle >= FOLLOW_TIMEOUT) l->following = false;
  pumpSnapshot(l);
}

bool bmLiveLog(BmLive *l, const char *pText)
{
  size_t n = strlen(pText);
  return send(l, BM_LINK_LOG, pText, (uint16_t)(n < BM_LINK_MAX_PAYLOAD ? n : BM_LINK_MAX_PAYLOAD));
}

// --- Payloads ------------------------------------------------------------------------------

void bmLiveEncodeStatus(const BmLiveStatus *s, uint8_t *o)
{
  o[0] = s->kind;
  o[1] = s->result;
  o[2] = s->mode;
  o[3] = 0;
  bmLinkPut32(o + 4, s->transferId);
  bmLinkPut32(o + 8, s->frame);
  bmLinkPut32(o + 12, s->roomIndex);
  bmLinkPut16(o + 16, (uint16_t)s->x);
  bmLinkPut16(o + 18, (uint16_t)s->y);
  bmLinkPut32(o + 20, s->applied);
}

void bmLiveDecodeStatus(const uint8_t *i, BmLiveStatus *s)
{
  s->kind = i[0];
  s->result = i[1];
  s->mode = i[2];
  s->transferId = bmLinkGet32(i + 4);
  s->frame = bmLinkGet32(i + 8);
  s->roomIndex = bmLinkGet32(i + 12);
  s->x = (int16_t)bmLinkGet16(i + 16);
  s->y = (int16_t)bmLinkGet16(i + 18);
  s->applied = bmLinkGet32(i + 20);
}

void bmLiveDecodeHello(const uint8_t *i, BmLiveHello *h)
{
  h->protocolVersion = bmLinkGet16(i + 0);
  h->runtimeVersion = bmLinkGet16(i + 2);
  h->abiMajor = bmLinkGet16(i + 4);
  h->abiMinor = bmLinkGet16(i + 6);
  h->profile = bmLinkGet32(i + 8);
  h->contentCapacity = bmLinkGet32(i + 12);
  h->activeSize = bmLinkGet32(i + 16);
  h->activeCrc = bmLinkGet32(i + 20);
  h->chunkCount = bmLinkGet32(i + 24);
  h->hashCount = bmLinkGet32(i + 28);
}
