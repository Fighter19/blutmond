/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "link.h"

#include "shared.h"

#include <string.h>

// The word path runs for every word in the serial interrupt, so on the GBA it is ARM code in
// IWRAM with its own copy of the CRC table (the same CRC-32 as bmCrc32, four bits at a time).
static const uint32_t s_crcNibble[16] BM_IWRAM_DATA = {
  0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
  0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

BM_IWRAM_CODE uint32_t bmLinkCrcWord(uint32_t crc, uint32_t word)
{
  crc = ~crc ^ word;  // the four bytes, lowest first
  for (int i = 0; i < 8; i++) crc = (crc >> 4) ^ s_crcNibble[crc & 15];
  return ~crc;
}

BM_IWRAM_CODE uint32_t bmLinkPacketWord(const BmLinkPacket *p, uint32_t index)
{
  if (index == 0) return 0x42000000u | ((uint32_t)p->type << 16) | ((uint32_t)p->seq << 8) | p->flags;
  if (index == 1) return p->length;
  uint32_t offset = (index - 2) * 4;
  uint32_t word = 0;
  for (uint32_t i = 0; i < 4 && offset + i < p->length; i++) word |= (uint32_t)p->payload[offset + i] << (8 * i);
  return word;
}

// --- Receiving -----------------------------------------------------------------------------

enum { RX_IDLE, RX_LENGTH, RX_PAYLOAD, RX_CRC };

void bmLinkRxInit(BmLinkRx *pRx, BmLinkPacket *pBuffer)
{
  memset(pRx, 0, sizeof(*pRx));
  pRx->pPacket = pBuffer;
}

BM_IWRAM_CODE BmLinkRxEvent bmLinkRxWord(BmLinkRx *rx, uint32_t word, uint8_t *pSeq)
{
  // Resync: four fill words and SYNC, in any state.
  if (word == BM_LINK_WORD_FILL)
  {
    if (rx->fillCount < 255) rx->fillCount++;
  }
  else
  {
    bool sync = word == BM_LINK_WORD_SYNC && rx->fillCount >= 4;
    rx->fillCount = 0;
    if (sync)
    {
      rx->state = RX_IDLE;
      return BM_LINK_RX_SYNC;
    }
  }

  BmLinkPacket *p = rx->pPacket;
  switch (rx->state)
  {
  case RX_IDLE:
  {
    if ((word >> 24) != 0x42) return BM_LINK_RX_NOISE;
    if (word == BM_LINK_WORD_POLL) return BM_LINK_RX_POLL;
    if (word == BM_LINK_WORD_IDLE) return BM_LINK_RX_IDLE;
    uint8_t type = (uint8_t)(word >> 16);
    uint8_t low = (uint8_t)word;
    if (type == 0x41 || type == 0x4E)
    {
      *pSeq = (uint8_t)(word >> 8);
      if (type == 0x41 && low == 0) return BM_LINK_RX_ACK;
      if (type == 0x4E && low == 0) return BM_LINK_RX_NAK;
      if (type == 0x4E && low == 1) return BM_LINK_RX_BUSY;
      return BM_LINK_RX_NOISE;
    }
    if (type == 0x4D) return BM_LINK_RX_NOISE;
    p->type = type;
    p->seq = (uint8_t)(word >> 8);
    p->flags = low;
    rx->crc = bmLinkCrcWord(0, word);
    rx->state = RX_LENGTH;
    return BM_LINK_RX_NONE;
  }
  case RX_LENGTH:
    if (word > BM_LINK_MAX_PAYLOAD)
    {
      rx->state = RX_IDLE;
      return BM_LINK_RX_BAD_PACKET;
    }
    p->length = (uint16_t)word;
    rx->crc = bmLinkCrcWord(rx->crc, word);
    rx->wordIndex = 0;
    rx->wordCount = (uint16_t)((word + 3) / 4);
    rx->state = rx->wordCount != 0 ? RX_PAYLOAD : RX_CRC;
    return BM_LINK_RX_NONE;
  case RX_PAYLOAD:
  {
    uint8_t *dst = p->payload + rx->wordIndex * 4;
    dst[0] = (uint8_t)word;
    dst[1] = (uint8_t)(word >> 8);
    dst[2] = (uint8_t)(word >> 16);
    dst[3] = (uint8_t)(word >> 24);
    rx->crc = bmLinkCrcWord(rx->crc, word);
    if (++rx->wordIndex == rx->wordCount) rx->state = RX_CRC;
    return BM_LINK_RX_NONE;
  }
  default:
    rx->state = RX_IDLE;
    return word == rx->crc ? BM_LINK_RX_PACKET : BM_LINK_RX_BAD_PACKET;
  }
}

// --- Slave ---------------------------------------------------------------------------------

#define SLOT(seq) ((uint8_t)(seq) & (BM_LINK_SLAVE_TX_SLOTS - 1))
#define SLAVE_RETRANSMIT_POLLS 32
#define SLAVE_ANSWER_HOLD 8

_Static_assert((BM_LINK_SLAVE_TX_SLOTS & (BM_LINK_SLAVE_TX_SLOTS - 1)) == 0, "tx slots must be a power of two");

void bmLinkSlaveInit(BmLinkSlave *s, BmLinkPacketHandler handler, void *pContext)
{
  memset(s, 0, sizeof(*s));
  bmLinkRxInit(&s->rx, &s->rxSlots[0]);
  s->handler = handler;
  s->pContext = pContext;
}

BM_IWRAM_CODE static void slaveRelease(BmLinkSlave *s, uint8_t count)
{
  s->txBase = (uint8_t)(s->txBase + count);
  s->txCount = (uint8_t)(s->txCount - count);
  if (s->txSend > count)
  {
    s->txSend = (uint8_t)(s->txSend - count);
  }
  else
  {
    s->txSend = 0;
    s->txWord = 0;
  }
  s->pollsWithoutAck = 0;
}

BM_IWRAM_CODE static void slaveRewind(BmLinkSlave *s)
{
  if (s->txSend != 0 || s->txWord != 0) s->counters.retransmits++;
  s->txSend = 0;
  s->txWord = 0;
  s->pollsWithoutAck = 0;
}

BM_IWRAM_CODE static void slaveReceive(BmLinkSlave *s, uint32_t received)
{
  uint8_t seq = 0;
  switch (bmLinkRxWord(&s->rx, received, &seq))
  {
  case BM_LINK_RX_PACKET:
  {
    s->answerHold = 0;
    BmLinkPacket *p = s->rx.pPacket;
    uint8_t diff = (uint8_t)(p->seq - s->rxExpected);
    if (diff == 0)
    {
      BmLinkTake take = s->handler != NULL ? s->handler(s->pContext, p) : BM_LINK_DONE;
      if (take == BM_LINK_KEPT && bmLinkSlaveRxFree(s) < 2) take = BM_LINK_REFUSED;  // handler bug: refuse
      if (take != BM_LINK_REFUSED)
      {
        s->rxExpected++;
        s->counters.packetsIn++;
      }
      else
      {
        s->counters.busy++;
        s->busyPending = true;
      }
      if (take == BM_LINK_KEPT)
      {
        uint32_t slot = (uint32_t)(p - s->rxSlots);
        s->rxHeld |= (uint8_t)(1u << slot);
        for (uint32_t i = 0; i < BM_LINK_SLAVE_RX_SLOTS; i++)
          if (!(s->rxHeld & (1u << i))) { s->rx.pPacket = &s->rxSlots[i]; break; }
      }
    }
    else if (diff < 128)
    {
      s->counters.outOfOrder++;
      s->nakPending = true;
    }
    // else: a packet we already have (the master sent the window again)
    break;
  }
  case BM_LINK_RX_BAD_PACKET:
    s->answerHold = 0;
    s->counters.badPackets++;
    s->nakPending = true;
    break;
  case BM_LINK_RX_POLL:
    // One answer per burst of polls, so the slave's own packets get through; it answers
    // again after a few polls in case the answer was lost, or after anything new arrived.
    if (s->answerHold != 0) s->answerHold--;
    else s->answerPending = true;
    if (s->txCount != 0 && s->txSend == s->txCount && ++s->pollsWithoutAck >= SLAVE_RETRANSMIT_POLLS)
      slaveRewind(s);
    break;
  case BM_LINK_RX_ACK:
  {
    uint8_t diff = (uint8_t)(seq - s->txBase);
    if (diff < s->txCount) slaveRelease(s, (uint8_t)(diff + 1));
    break;
  }
  case BM_LINK_RX_NAK:
  {
    uint8_t diff = (uint8_t)(seq - s->txBase);
    if (diff <= s->txCount)
    {
      if (diff != 0) slaveRelease(s, diff);
      slaveRewind(s);
    }
    break;
  }
  case BM_LINK_RX_SYNC:
    s->counters.resyncs++;
    s->syncWords = 5;
    s->answerPending = false;
    s->answerHold = 0;
    s->nakPending = false;
    s->busyPending = false;
    s->txSend = 0;
    s->txWord = 0;
    break;
  default:
    break;
  }
}

BM_IWRAM_CODE uint32_t bmLinkSlaveExchange(BmLinkSlave *s, uint32_t received)
{
  s->counters.words++;
  slaveReceive(s, received);

  if (s->syncWords != 0)
  {
    s->syncWords--;
    return s->syncWords != 0 ? BM_LINK_WORD_FILL : BM_LINK_WORD_SYNC;
  }
  if (s->txWord == 0 && s->answerPending)
  {
    s->answerPending = false;
    s->answerHold = SLAVE_ANSWER_HOLD;
    uint32_t answer;
    if (s->nakPending) answer = BM_LINK_NAK(s->rxExpected);
    else if (s->busyPending) answer = BM_LINK_BUSY(s->rxExpected);
    else answer = BM_LINK_ACK((uint8_t)(s->rxExpected - 1));
    s->nakPending = false;
    s->busyPending = false;
    return answer;
  }
  if (s->txSend < s->txCount)
  {
    const BmLinkPacket *p = &s->tx[SLOT(s->txBase + s->txSend)];
    uint32_t count = bmLinkPacketWords(p->length);
    uint32_t word;
    if (s->txWord == 0) s->txCrc = 0;
    if (s->txWord == count - 1)
    {
      word = s->txCrc;
    }
    else
    {
      word = bmLinkPacketWord(p, s->txWord);
      s->txCrc = bmLinkCrcWord(s->txCrc, word);
    }
    if (++s->txWord == count)
    {
      s->txWord = 0;
      s->txSend++;
      s->counters.packetsOut++;
    }
    return word;
  }
  return BM_LINK_WORD_IDLE;
}

BM_IWRAM_CODE uint32_t bmLinkSlaveRxFree(const BmLinkSlave *s)
{
  uint32_t n = 0;
  for (uint32_t i = 0; i < BM_LINK_SLAVE_RX_SLOTS; i++) n += !(s->rxHeld & (1u << i));
  return n;
}

void bmLinkSlaveRelease(BmLinkSlave *s, const BmLinkPacket *p)
{
  uint32_t slot = (uint32_t)(p - s->rxSlots);
  if (slot < BM_LINK_SLAVE_RX_SLOTS) s->rxHeld &= (uint8_t)~(1u << slot);
}

BmLinkPacket *bmLinkSlaveSendBegin(BmLinkSlave *s)
{
  // txBase + txCount only grows here: the interrupt adds to txBase what it takes from txCount.
  if (s->txCount >= BM_LINK_SLAVE_TX_SLOTS) return NULL;
  uint8_t seq = (uint8_t)(s->txBase + s->txCount);
  BmLinkPacket *p = &s->tx[SLOT(seq)];
  p->seq = seq;
  p->flags = 0;
  return p;
}

void bmLinkSlaveSendCommit(BmLinkSlave *s)
{
  s->txCount++;
}

bool bmLinkSlaveSend(BmLinkSlave *s, uint8_t type, const void *pPayload, uint16_t length)
{
  if (length > BM_LINK_MAX_PAYLOAD) return false;
  BmLinkPacket *p = bmLinkSlaveSendBegin(s);
  if (p == NULL) return false;
  p->type = type;
  p->length = length;
  if (length != 0) memcpy(p->payload, pPayload, length);
  bmLinkSlaveSendCommit(s);
  return true;
}

uint32_t bmLinkSlaveTxFree(const BmLinkSlave *s)
{
  return BM_LINK_SLAVE_TX_SLOTS - s->txCount;
}
