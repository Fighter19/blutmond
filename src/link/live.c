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
  default:
    return true;  // not handled by this runtime (CHUNK_BEGIN carries nothing needed here)
  }
}

void bmLiveFrame(BmLive *l)
{
  for (;;)
  {
    uint32_t lock = bmHalLinkLock();
    BmLinkPacket *p = l->pendingCount != 0 ? l->pending[0] : NULL;
    bmHalLinkUnlock(lock);
    if (p == NULL || !handle(l, p)) break;
    lock = bmHalLinkLock();
    l->pendingCount--;
    for (uint32_t i = 0; i < l->pendingCount; i++) l->pending[i] = l->pending[i + 1];
    bmLinkSlaveRelease(&l->slave, p);
    bmHalLinkUnlock(lock);
  }
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
