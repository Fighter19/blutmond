/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "live_master.h"

#include <string.h>

bool bmLiveMasterHello(BmLinkMaster *m, BmLiveRemote *r)
{
  BmLinkPacket p;
  if (!bmLinkMasterRequest(m, BM_LINK_HELLO, NULL, 0, BM_LINK_HELLO, &p, BM_LIVE_ANSWER_POLLS)) return false;
  if (p.length < BM_LIVE_HELLO_SIZE) return false;
  bmLiveDecodeHello(p.payload, &r->hello);
  if (r->hello.hashCount > BM_LIVE_HELLO_MAX_HASHES || p.length < BM_LIVE_HELLO_SIZE + r->hello.hashCount * 8) return false;
  for (uint32_t i = 0; i < 2 * r->hello.hashCount; i++) r->pairs[i] = bmLinkGet32(p.payload + BM_LIVE_HELLO_SIZE + i * 4);
  return true;
}

static bool resident(const BmLiveRemote *r, uint32_t hash, uint32_t size)
{
  if (r == NULL) return false;
  for (uint32_t i = 0; i < r->hello.hashCount; i++)
    if (r->pairs[2 * i] == hash && r->pairs[2 * i + 1] == size) return true;
  return false;
}

bool bmLiveMasterPushPack(BmLinkMaster *m, const uint8_t *pPack, uint32_t size, uint32_t id, const BmLiveRemote *r,
                          BmLivePushStats *pStats)
{
  BmPack pack;
  BmLivePushStats stats = { 0 };
  if (bmPackOpen(&pack, pPack, size, true) != BM_PACK_OK) return false;
  uint8_t buf[BM_LINK_MAX_PAYLOAD];

  // Table of contents: header and entries as one stream, split into parts.
  uint32_t count = bmPackChunkCount(&pack);
  uint32_t stream = BM_PACK_HEADER_SIZE + count * BM_PACK_CHUNK_ENTRY_SIZE;
  uint32_t perPart = BM_LINK_MAX_PAYLOAD - BM_LIVE_TOC_HEADER;
  uint32_t parts = (stream + perPart - 1) / perPart;
  for (uint32_t part = 0; part < parts; part++)
  {
    uint32_t from = part * perPart, n = stream - from < perPart ? stream - from : perPart;
    bmLinkPut32(buf, id);
    bmLinkPut32(buf + 4, size);
    bmLinkPut16(buf + 8, (uint16_t)part);
    bmLinkPut16(buf + 10, (uint16_t)parts);
    for (uint32_t i = 0; i < n; i++)
    {
      uint32_t pos = from + i;
      buf[BM_LIVE_TOC_HEADER + i] = pos < BM_PACK_HEADER_SIZE ? pPack[pos] : pPack[pack.header.tocOffset + pos - BM_PACK_HEADER_SIZE];
    }
    if (!bmLinkMasterQueue(m, BM_LINK_TOC, buf, (uint16_t)(BM_LIVE_TOC_HEADER + n))) return false;
  }

  for (uint32_t c = 0; c < count; c++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&pack, c, &e);
    if (resident(r, e.hash, e.size))
    {
      stats.chunksResident++;
      continue;
    }
    bmLinkPut32(buf, id);
    bmLinkPut16(buf + 4, (uint16_t)c);
    bmLinkPut16(buf + 6, 0);
    bmLinkPut32(buf + 8, e.size);
    bmLinkPut32(buf + 12, e.hash);
    bmLinkMasterQueue(m, BM_LINK_CHUNK_BEGIN, buf, 16);
    for (uint32_t at = 0; at < e.size; at += BM_LIVE_CHUNK_DATA_MAX)
    {
      uint32_t n = e.size - at < BM_LIVE_CHUNK_DATA_MAX ? e.size - at : BM_LIVE_CHUNK_DATA_MAX;
      bmLinkPut32(buf, id);
      bmLinkPut16(buf + 4, (uint16_t)c);
      bmLinkPut16(buf + 6, 0);
      bmLinkPut32(buf + 8, at);
      memcpy(buf + BM_LIVE_CHUNK_DATA_HEADER, pPack + e.offset + at, n);
      bmLinkMasterQueue(m, BM_LINK_CHUNK_DATA, buf, (uint16_t)(BM_LIVE_CHUNK_DATA_HEADER + n));
    }
    bmLinkPut32(buf, id);
    bmLinkPut16(buf + 4, (uint16_t)c);
    bmLinkMasterQueue(m, BM_LINK_CHUNK_END, buf, 6);
    stats.chunksSent++;
    stats.bytesSent += e.size;
    // Flush now and then so the queue stays small.
    if (m->queueCount >= 64 && !bmLinkMasterFlush(m)) return false;
  }
  if (pStats != NULL) *pStats = stats;
  return bmLinkMasterFlush(m);
}

bool bmLiveMasterApply(BmLinkMaster *m, uint32_t id, BmLiveMode mode, const char *roomId, BmLiveStatus *pStatus)
{
  uint8_t buf[8 + 64] = { 0 };
  uint32_t n = 8;
  bmLinkPut32(buf, id);
  buf[4] = (uint8_t)mode;
  if (roomId != NULL)
  {
    size_t len = strlen(roomId);
    if (len > 63) return false;
    memcpy(buf + 8, roomId, len);
    n += (uint32_t)len;
  }
  BmLinkPacket p;
  if (!bmLinkMasterRequest(m, BM_LINK_APPLY, buf, (uint16_t)n, BM_LINK_STATUS, &p, BM_LIVE_ANSWER_POLLS)) return false;
  if (p.length < BM_LIVE_STATUS_SIZE) return false;
  bmLiveDecodeStatus(p.payload, pStatus);
  return true;
}

bool bmLiveMasterGetState(BmLinkMaster *m, uint8_t mask, uint8_t *pOut, uint32_t capacity, uint32_t *pSize)
{
  BmLinkPacket p;
  if (!bmLinkMasterRequest(m, BM_LINK_STATE_GET, &mask, 1, BM_LINK_STATE_DATA, &p, BM_LIVE_ANSWER_POLLS)) return false;
  if (p.length > capacity) return false;
  memcpy(pOut, p.payload, p.length);
  *pSize = p.length;
  return true;
}

bool bmLiveMasterPutState(BmLinkMaster *m, const uint8_t *pData, uint32_t size, BmLiveStatus *pStatus)
{
  if (size > BM_LINK_MAX_PAYLOAD || !bmLinkMasterQueue(m, BM_LINK_STATE_PUT, pData, (uint16_t)size)) return false;
  return bmLiveMasterApply(m, 0, BM_LIVE_STATE, NULL, pStatus);
}
