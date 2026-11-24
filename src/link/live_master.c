/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "live_master.h"

#include <string.h>

bool bmLiveMasterHello(BmLinkMaster *m, BmLiveRemote *r)
{
  // The chunk list may take several answers (82 chunks each): asked again from where it stopped
  // (runtime version 4 on; an older one lists only the first ones).
  uint32_t listed = 0;
  for (int part = 0; part < 8; part++)
  {
    uint8_t ask[4];
    bmLinkPut32(ask, listed);
    BmLinkPacket p;
    if (!bmLinkMasterRequest(m, BM_LINK_HELLO, ask, part == 0 ? 0 : 4, BM_LINK_HELLO, &p, BM_LIVE_ANSWER_POLLS)) return false;
    if (p.length < BM_LIVE_HELLO_SIZE) return false;
    BmLiveHello h;
    bmLiveDecodeHello(p.payload, &h);
    if (h.hashCount > BM_LIVE_HELLO_MAX_HASHES || p.length < BM_LIVE_HELLO_SIZE + h.hashCount * BM_LIVE_HELLO_ENTRY ||
        listed + h.hashCount > BM_LIVE_MAX_CHUNKS)
      return false;
    if (part == 0)
    {
      r->hello = h;
      uint32_t tail = BM_LIVE_HELLO_SIZE + h.hashCount * BM_LIVE_HELLO_ENTRY;
      if (h.runtimeVersion >= 6 && p.length >= tail + 4) r->hello.wantedRoom = bmLinkGet32(p.payload + tail);
    }
    for (uint32_t i = 0; i < 3 * h.hashCount; i++) r->pairs[3 * listed + i] = bmLinkGet32(p.payload + BM_LIVE_HELLO_SIZE + i * 4);
    listed += h.hashCount;
    r->hello.hashCount = listed;
    if (h.hashCount == 0 || listed >= r->hello.chunkCount || r->hello.runtimeVersion < 4) break;
  }
  return true;
}

// Whether the runtime has the chunk: anywhere in its running pack (the new one goes beside
// it), or at the same offset (`atOffset`: the new one goes over it at its place).
static bool resident(const BmLiveRemote *r, const BmChunkEntry *e, bool atOffset)
{
  if (r == NULL) return false;
  for (uint32_t i = 0; i < r->hello.hashCount; i++)
    if (r->pairs[3 * i] == e->hash && r->pairs[3 * i + 1] == e->size && (!atOffset || r->pairs[3 * i + 2] == e->offset))
      return true;
  return false;
}

// The chunk table ascends without overlaps (what keeping chunks in place relies on).
static bool ascending(const BmPack *pPack)
{
  uint32_t end = 0;
  for (uint32_t c = 0; c < bmPackChunkCount(pPack); c++)
  {
    BmChunkEntry e;
    bmPackGetChunk(pPack, c, &e);
    if (e.offset < end) return false;
    end = e.offset + e.size;
  }
  return true;
}

// A fitted push: which chunks keep their bytes (the rest are left out, BM_PACK_ENTRY_ABSENT),
// the size and checksum of the pack that results. Its table is the full pack's, so chunk
// indices, room numbers and string ids stay.
#define FIT_MAX_ROOMS 64

typedef struct FitPlan
{
  uint8_t keep[BM_LIVE_MAX_CHUNKS / 8];
  uint32_t size, crc, leftOut;
  // Working space, here rather than on the stack (a GBA sends with a small one).
  uint8_t named[BM_LIVE_MAX_CHUNKS / 8];
  int32_t queue[FIT_MAX_ROOMS];
  uint8_t seen[FIT_MAX_ROOMS];
  uint32_t near;   // queue[0 .. near): the rooms asked for and those one door from them
} FitPlan;

static bool kept(const FitPlan *f, uint32_t i) { return f == NULL || (f->keep[i / 8] & (1u << (i % 8))) != 0; }

static uint32_t padded(uint32_t n) { return (n + 3u) & ~3u; }

// The chunks a ROOM chunk names (room-format.md §5: maps, collision, objects, area), each once.
static uint32_t roomRefs(const BmPack *pPack, uint32_t index, uint16_t *pRefs)
{
  BmChunkEntry e;
  const uint8_t *r = bmPackChunkData(pPack, index, NULL);
  if (r == NULL || !bmPackGetChunk(pPack, index, &e) || e.id != BM_CHUNK_ROOM || e.size < 44) return 0;
  uint32_t n = 0, count = bmPackChunkCount(pPack);
  uint16_t all[5 + 8];
  for (uint32_t k = 0; k < 5; k++) all[n++] = bmRead16(r + 30 + 2 * k);
  for (uint32_t l = 0; l < r[5] && l < 8 && 44u + l * 12u + 4u <= e.size; l++) all[n++] = bmRead16(r + 44 + l * 12 + 2);
  uint32_t out = 0;
  for (uint32_t k = 0; k < n; k++)
  {
    bool seen = all[k] >= count;
    for (uint32_t j = 0; j < out && !seen; j++) seen = pRefs[j] == all[k];
    if (!seen) pRefs[out++] = all[k];
  }
  return out;
}

static uint32_t fitCost(const BmPack *pPack, const FitPlan *f, uint32_t i)
{
  BmChunkEntry e;
  if (kept(f, i) || !bmPackGetChunk(pPack, i, &e) || (e.flags & BM_PACK_ENTRY_ABSENT)) return 0;
  return padded(e.size);
}

static void fitKeep(const BmPack *pPack, FitPlan *f, uint32_t i)
{
  f->size += fitCost(pPack, f, i);
  f->keep[i / 8] |= (uint8_t)(1u << (i % 8));
}

// The room `room` (its number) with what it names, if that fits in `budget`.
static void fitRoom(const BmPack *pPack, FitPlan *f, int32_t room, uint32_t budget)
{
  uint32_t count = bmPackChunkCount(pPack);
  for (uint32_t i = 0; i < count && room >= 0; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(pPack, i, &e);
    if (e.id != BM_CHUNK_ROOM || room-- != 0) continue;
    if ((e.flags & BM_PACK_ENTRY_ABSENT) || kept(f, i)) return;
    uint16_t refs[13];
    uint32_t n = roomRefs(pPack, i, refs), cost = fitCost(pPack, f, i);
    for (uint32_t k = 0; k < n; k++) cost += fitCost(pPack, f, refs[k]);
    if (f->size + cost > budget) return;
    fitKeep(pPack, f, i);
    for (uint32_t k = 0; k < n; k++) fitKeep(pPack, f, refs[k]);
    return;
  }
}

// An entry of the table as sent: the pack's own, or (fitted) with the new offset or left out.
// `pAt`: where the next kept chunk goes.
static void sentEntry(const BmPack *pPack, const FitPlan *f, uint32_t i, uint32_t *pAt, uint8_t *pOut)
{
  memcpy(pOut, pPack->pData + pPack->header.tocOffset + i * BM_PACK_CHUNK_ENTRY_SIZE, BM_PACK_CHUNK_ENTRY_SIZE);
  if (f == NULL) return;
  if (kept(f, i))
  {
    bmLinkPut32(pOut + 4, *pAt);
    *pAt += padded(bmRead32(pOut + 8));
  }
  else
  {
    bmLinkPut32(pOut + 4, 0);
    bmLinkPut32(pOut + 8, 0);
    pOut[16] |= BM_PACK_ENTRY_ABSENT;
  }
}

// The chunk of room number `room`, or -1.
static int32_t roomChunk(const BmPack *pPack, int32_t room)
{
  for (uint32_t i = 0; i < bmPackChunkCount(pPack); i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(pPack, i, &e);
    if (e.id == BM_CHUNK_ROOM && room-- == 0) return (int32_t)i;
  }
  return -1;
}

// The rooms a room's objects lead to (their property `target_room`: doors, teleporters), by
// number, at most `max`.
static uint32_t roomExits(const BmPack *pPack, int32_t room, int32_t *pOut, uint32_t max)
{
  int32_t chunk = roomChunk(pPack, room);
  uint16_t refs[13];
  uint32_t n = chunk < 0 ? 0 : roomRefs(pPack, (uint32_t)chunk, refs), out = 0;
  for (uint32_t k = 0; k < n; k++)
  {
    BmChunkEntry e;
    uint32_t size = 0;
    const uint8_t *o = bmPackChunkData(pPack, refs[k], &size);
    if (o == NULL || !bmPackGetChunk(pPack, refs[k], &e) || e.id != BM_CHUNK_OBJS || size < 4) continue;
    uint32_t count = bmRead16(o);
    for (uint32_t j = 0; j < count && 4 + j * 20 + 20 <= size; j++)
    {
      const uint8_t *ob = o + 4 + j * 20;
      uint32_t props = bmRead16(ob + 14), at = bmRead32(ob + 16);
      for (uint32_t p = 0; p < props && at + p * 4 + 4 <= size; p++)
      {
        const char *key = bmPackString(pPack, bmRead16(o + at + p * 4));
        if (key == NULL || strcmp(key, "target_room") != 0) continue;
        uint16_t target = bmRead16(o + at + p * 4 + 2);
        // The room whose id (ROOM + 40) is that string.
        int32_t r = 0;
        for (uint32_t c = 0; c < bmPackChunkCount(pPack); c++)
        {
          BmChunkEntry re;
          bmPackGetChunk(pPack, c, &re);
          if (re.id != BM_CHUNK_ROOM) continue;
          const uint8_t *rd = bmPackChunkData(pPack, c, NULL);
          if (rd != NULL && re.size >= 44 && bmRead16(rd + 40) == target && out < max) pOut[out++] = r;
          r++;
        }
      }
    }
  }
  return out;
}

// Picks what goes into `budget` bytes: every chunk no room names (but the songs and the
// languages), the room `room`, room 0 and the rooms one door from them, the songs, then the
// other rooms nearest to them through doors first (then the ones no door leads to, in order;
// each whole, if it fits), then the languages.
static bool fitPack(const BmPack *pPack, int32_t room, uint32_t budget, FitPlan *f)
{
  uint32_t count = bmPackChunkCount(pPack);
  if (count > BM_LIVE_MAX_CHUNKS || (pPack->header.flags & BM_PACK_FLAG_UPDATE)) return false;
  memset(f, 0, sizeof(*f));
  f->size = BM_PACK_HEADER_SIZE + count * BM_PACK_CHUNK_ENTRY_SIZE;
  uint8_t *named = f->named;
  int32_t rooms = 0;
  for (uint32_t i = 0; i < count; i++)
  {
    uint16_t refs[13];
    uint32_t n = roomRefs(pPack, i, refs);
    for (uint32_t k = 0; k < n; k++) named[refs[k] / 8] |= (uint8_t)(1u << (refs[k] % 8));
    BmChunkEntry e;
    bmPackGetChunk(pPack, i, &e);
    if (e.id == BM_CHUNK_ROOM) rooms++;
  }
  for (uint32_t i = 0; i < count; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(pPack, i, &e);
    if (e.id != BM_CHUNK_ROOM && e.id != BM_CHUNK_LANG && e.id != BM_CHUNK_SONG && !(named[i / 8] & (1u << (i % 8)))) fitKeep(pPack, f, i);
  }
  if (f->size > budget) return false;
  // Breadth first from the room asked for and room 0.
  int32_t *queue = f->queue;
  uint8_t *seen = f->seen;
  uint32_t head = 0, tail = 0;
  if (rooms > FIT_MAX_ROOMS) rooms = FIT_MAX_ROOMS;
  if (room >= 0 && room < rooms) seen[room] = 1, queue[tail++] = room;
  if (!seen[0] && rooms > 0) seen[0] = 1, queue[tail++] = 0;
  // Every room reachable through doors, nearest first (the order to fit them in).
  uint32_t initial = tail;
  while (head < tail)
  {
    int32_t exits[16];
    uint32_t n = roomExits(pPack, queue[head++], exits, 16);
    for (uint32_t k = 0; k < n; k++)
      if (exits[k] < rooms && !seen[exits[k]]) seen[exits[k]] = 1, queue[tail++] = exits[k];
    // Where the rooms one door away end.
    if (head == initial) f->near = tail;
  }
  if (initial == 0) f->near = 0;
  // The room asked for and room 0 and the rooms one door from them, then the songs (a song
  // left out is silence, a room left out cannot be entered), then the rest nearest first.
  for (uint32_t k = 0; k < f->near; k++) fitRoom(pPack, f, queue[k], budget);
  for (uint32_t i = 0; i < count; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(pPack, i, &e);
    if (e.id == BM_CHUNK_SONG && f->size + fitCost(pPack, f, i) <= budget) fitKeep(pPack, f, i);
  }
  for (uint32_t k = f->near; k < tail; k++) fitRoom(pPack, f, queue[k], budget);
  for (int32_t k = 0; k < rooms; k++) fitRoom(pPack, f, k, budget);
  for (uint32_t i = 0; i < count; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(pPack, i, &e);
    if (e.id == BM_CHUNK_LANG && f->size + fitCost(pPack, f, i) <= budget) fitKeep(pPack, f, i);
  }

  // The checksum of what results: the table, then the kept chunks with their padding.
  static const uint8_t zeros[4] = { 0 };
  uint32_t at = BM_PACK_HEADER_SIZE + count * BM_PACK_CHUNK_ENTRY_SIZE, crc = 0;
  for (uint32_t i = 0; i < count; i++)
  {
    uint8_t entry[BM_PACK_CHUNK_ENTRY_SIZE];
    sentEntry(pPack, f, i, &at, entry);
    crc = bmCrc32(crc, entry, sizeof(entry));
  }
  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t n = 0;
    const uint8_t *data = bmPackChunkStored(pPack, i, &n);
    if (!kept(f, i) || data == NULL)
    {
      f->leftOut += data != NULL;
      continue;
    }
    crc = bmCrc32(crc, data, n);
    crc = bmCrc32(crc, zeros, padded(n) - n);
  }
  f->crc = crc;
  return true;
}

int32_t bmLiveMasterFindRoom(const uint8_t *pPack, uint32_t size, const char *roomId)
{
  BmPack pack;
  if (roomId == NULL || bmPackOpen(&pack, pPack, size, false) != BM_PACK_OK) return -1;
  const uint8_t *strs = NULL;
  uint32_t strsSize = 0, count = bmPackChunkCount(&pack);
  for (uint32_t i = 0; i < count && strs == NULL; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&pack, i, &e);
    if (e.id == BM_CHUNK_STRS) strs = bmPackChunkData(&pack, i, &strsSize);
  }
  if (strs == NULL || strsSize < 4) return -1;
  int32_t room = 0;
  for (uint32_t i = 0; i < count; i++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&pack, i, &e);
    if (e.id != BM_CHUNK_ROOM) continue;
    const uint8_t *r = bmPackChunkData(&pack, i, NULL);
    uint32_t id = r != NULL && e.size >= 44 ? bmRead16(r + 40) : 0xFFFFu;
    if (id < bmRead16(strs) && 4u + 4u * id + 4u <= strsSize)
    {
      uint32_t at = bmRead32(strs + 4 + 4 * id);
      size_t len = strlen(roomId);
      if (at < strsSize && len < strsSize - at && memcmp(strs + at, roomId, len + 1) == 0) return room;
    }
    room++;
  }
  return -1;
}

bool bmLiveMasterPushPack(BmLinkMaster *m, const uint8_t *pPack, uint32_t size, uint32_t id, const BmLiveRemote *r,
                          BmLivePushStats *pStats)
{
  return bmLiveMasterPushPackFor(m, pPack, size, id, r, -1, pStats);
}

bool bmLiveMasterPushPackFor(BmLinkMaster *m, const uint8_t *pPack, uint32_t size, uint32_t id, const BmLiveRemote *r,
                             int32_t room, BmLivePushStats *pStats)
{
  BmPack pack;
  BmLivePushStats stats = { 0 };
  if (bmPackOpen(&pack, pPack, size, true) != BM_PACK_OK) return false;
  static uint8_t buf[BM_LINK_MAX_PAYLOAD] BM_EWRAM_BSS;
  // Larger than the runtime takes at all (runtime version 5 on): fitted, with as many rooms as
  // fit; the rest is left out and can come with a later push.
  static FitPlan plan BM_EWRAM_BSS;
  const FitPlan *fit = NULL;
  if (r != NULL && r->hello.runtimeVersion >= 5 && size > r->hello.poolCapacity)
  {
    if (!fitPack(&pack, room, r->hello.poolCapacity, &plan)) return false;
    fit = &plan;
    stats.chunksLeftOut = plan.leftOut;
  }
  uint32_t sentSize = fit != NULL ? fit->size : size;
  // Too large to arrive beside the running pack: it is written over it. At its place the
  // runtime keeps the chunks that stay at the same offset; elsewhere none.
  bool atOffset = false;
  if (r != NULL && sentSize > r->hello.contentCapacity)
  {
    if (fit == NULL && size <= r->hello.inPlaceCapacity && ascending(&pack)) atOffset = true;
    else r = NULL;
  }

  // Table of contents: header and entries as one stream, split into parts.
  uint32_t count = bmPackChunkCount(&pack);
  uint8_t header[BM_PACK_HEADER_SIZE], entry[BM_PACK_CHUNK_ENTRY_SIZE];
  memcpy(header, pPack, BM_PACK_HEADER_SIZE);
  if (fit != NULL)
  {
    bmLinkPut32(header + 16, fit->size);
    bmLinkPut32(header + 32, fit->crc);
  }
  uint32_t at = BM_PACK_HEADER_SIZE + count * BM_PACK_CHUNK_ENTRY_SIZE;
  uint32_t stream = BM_PACK_HEADER_SIZE + count * BM_PACK_CHUNK_ENTRY_SIZE;
  uint32_t perPart = BM_LINK_MAX_PAYLOAD - BM_LIVE_TOC_HEADER;
  uint32_t parts = (stream + perPart - 1) / perPart;
  for (uint32_t part = 0; part < parts; part++)
  {
    uint32_t from = part * perPart, n = stream - from < perPart ? stream - from : perPart;
    bmLinkPut32(buf, id);
    bmLinkPut32(buf + 4, sentSize);
    bmLinkPut16(buf + 8, (uint16_t)part);
    bmLinkPut16(buf + 10, (uint16_t)parts);
    for (uint32_t i = 0; i < n; i++)
    {
      uint32_t pos = from + i;
      if (pos < BM_PACK_HEADER_SIZE)
      {
        buf[BM_LIVE_TOC_HEADER + i] = header[pos];
        continue;
      }
      uint32_t within = (pos - BM_PACK_HEADER_SIZE) % BM_PACK_CHUNK_ENTRY_SIZE;
      if (within == 0) sentEntry(&pack, fit, (pos - BM_PACK_HEADER_SIZE) / BM_PACK_CHUNK_ENTRY_SIZE, &at, entry);
      buf[BM_LIVE_TOC_HEADER + i] = entry[within];
    }
    if (!bmLinkMasterQueue(m, BM_LINK_TOC, buf, (uint16_t)(BM_LIVE_TOC_HEADER + n))) return false;
  }

  for (uint32_t c = 0; c < count; c++)
  {
    BmChunkEntry e;
    bmPackGetChunk(&pack, c, &e);
    const uint8_t *data = bmPackChunkStored(&pack, c, NULL);
    if (!kept(fit, c) || data == NULL) continue;
    if (resident(r, &e, atOffset))
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
      memcpy(buf + BM_LIVE_CHUNK_DATA_HEADER, data + at, n);
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
  // The parts come in order: offset, total, data.
  uint32_t received = 0;
  for (;;)
  {
    if (p.length < BM_LIVE_STATE_HEADER) return false;
    uint32_t offset = bmLinkGet32(p.payload), total = bmLinkGet32(p.payload + 4), n = p.length - BM_LIVE_STATE_HEADER;
    if (total == 0 || total > capacity || offset != received || n > total - offset) return false;
    memcpy(pOut + offset, p.payload + BM_LIVE_STATE_HEADER, n);
    received += n;
    if (received == total) break;
    if (!bmLinkMasterReceive(m, BM_LINK_STATE_DATA, &p, BM_LIVE_ANSWER_POLLS)) return false;
  }
  *pSize = received;
  return true;
}

bool bmLiveMasterPutState(BmLinkMaster *m, const uint8_t *pData, uint32_t size, BmLiveStatus *pStatus)
{
  if (size == 0 || size > BM_LIVE_STATE_CAPACITY) return false;
  static uint8_t part[BM_LINK_MAX_PAYLOAD] BM_EWRAM_BSS;
  for (uint32_t offset = 0; offset < size;)
  {
    uint32_t n = size - offset < BM_LIVE_STATE_DATA_MAX ? size - offset : BM_LIVE_STATE_DATA_MAX;
    bmLinkPut32(part, offset);
    bmLinkPut32(part + 4, size);
    memcpy(part + BM_LIVE_STATE_HEADER, pData + offset, n);
    if (!bmLinkMasterQueue(m, BM_LINK_STATE_PUT, part, (uint16_t)(BM_LIVE_STATE_HEADER + n))) return false;
    offset += n;
  }
  return bmLiveMasterApply(m, 0, BM_LIVE_STATE, NULL, pStatus);
}

bool bmLiveMasterPpuSnapshot(BmLinkMaster *m, uint8_t *pOut, uint32_t capacity, uint32_t *pSize)
{
  uint8_t request[8] = { 0 };
  BmLinkPacket p;
  if (!bmLinkMasterRequest(m, BM_LINK_PPU_SNAPSHOT, request, sizeof(request), BM_LINK_PPU_SNAPSHOT, &p, BM_LIVE_ANSWER_POLLS))
    return false;
  uint32_t received = 0, total = 0;
  for (;;)
  {
    if (p.length < BM_LIVE_SNAPSHOT_HEADER) return false;
    uint32_t offset = bmLinkGet32(p.payload), n = p.length - BM_LIVE_SNAPSHOT_HEADER;
    total = bmLinkGet32(p.payload + 4);
    if (total > capacity || offset > total || n > total - offset) return false;
    memcpy(pOut + offset, p.payload + BM_LIVE_SNAPSHOT_HEADER, n);
    received += n;
    if (received >= total) break;
    if (!bmLinkMasterReceive(m, BM_LINK_PPU_SNAPSHOT, &p, BM_LIVE_ANSWER_POLLS)) return false;
  }
  *pSize = total;
  return true;
}
