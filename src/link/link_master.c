/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "link_master.h"

#include <stdlib.h>
#include <string.h>

#define MAX_ATTEMPTS 4
#define MAX_BUSY_WAITS 200000
#define BUSY_WAIT_POLLS 16
#define STALE_ANSWERS 3

void bmLinkMasterInit(BmLinkMaster *m, const BmLinkTransport *pTransport)
{
  memset(m, 0, sizeof(*m));
  m->transport = *pTransport;
  bmLinkRxInit(&m->rx, &m->rxPacket);
  m->pollTimeout = 400;
  m->maxResyncs = 8;
  m->maxDelay = 64;
  m->cleanWindowsToLower = 8;
}

void bmLinkMasterFree(BmLinkMaster *m)
{
  free(m->pQueue);
  free(m->pInbox);
  m->pQueue = m->pInbox = NULL;
  m->queueCount = m->inboxCount = 0;
}

void bmLinkMasterSetDelay(BmLinkMaster *m, uint32_t delay)
{
  m->delay = delay;
  if (m->transport.setDelay != NULL) m->transport.setDelay(m->transport.pContext, delay);
}

static void inboxPush(BmLinkMaster *m, const BmLinkPacket *p)
{
  if (m->inboxCount == m->inboxCapacity)
  {
    m->inboxCapacity = m->inboxCapacity ? m->inboxCapacity * 2 : 8;
    m->pInbox = realloc(m->pInbox, m->inboxCapacity * sizeof(BmLinkPacket));
  }
  m->pInbox[m->inboxCount++] = *p;
}

/// One word out, one word in; handles whatever the slave sent.
static void xfer(BmLinkMaster *m, uint32_t out)
{
  uint32_t in = m->transport.exchange(m->transport.pContext, out);
  m->counters.words++;
  uint8_t seq = 0;
  BmLinkRxEvent ev = bmLinkRxWord(&m->rx, in, &seq);
  switch (ev)
  {
  case BM_LINK_RX_PACKET:
  {
    uint8_t diff = (uint8_t)(m->rxPacket.seq - m->rxExpected);
    if (diff == 0)
    {
      inboxPush(m, &m->rxPacket);
      m->rxExpected++;
      m->counters.packetsIn++;
      m->ackPending = true;
    }
    else if (diff < 128)
    {
      m->counters.outOfOrder++;
      m->nakPending = true;
    }
    else
    {
      m->ackPending = true;  // a copy of one we have: the ACK was lost
    }
    break;
  }
  case BM_LINK_RX_BAD_PACKET:
    m->counters.badPackets++;
    m->nakPending = true;
    break;
  case BM_LINK_RX_ACK:
  case BM_LINK_RX_NAK:
  case BM_LINK_RX_BUSY:
    m->answer = (uint8_t)ev;
    m->answerSeq = seq;
    break;
  case BM_LINK_RX_SYNC:
    m->sawSync = true;
    break;
  default:
    break;
  }
}

/// Between packets: acknowledge the slave's packets if needed, else poll.
static void pollOrAck(BmLinkMaster *m)
{
  if (m->nakPending)
  {
    m->nakPending = m->ackPending = false;
    xfer(m, BM_LINK_NAK(m->rxExpected));
  }
  else if (m->ackPending)
  {
    m->ackPending = false;
    xfer(m, BM_LINK_ACK((uint8_t)(m->rxExpected - 1)));
  }
  else
  {
    xfer(m, BM_LINK_WORD_POLL);
  }
}

static void sendPacket(BmLinkMaster *m, const BmLinkPacket *p)
{
  uint32_t count = bmLinkPacketWords(p->length);
  uint32_t crc = 0;
  for (uint32_t i = 0; i + 1 < count; i++)
  {
    uint32_t word = bmLinkPacketWord(p, i);
    crc = bmLinkCrcWord(crc, word);
    xfer(m, word);
  }
  xfer(m, crc);
}

bool bmLinkMasterQueue(BmLinkMaster *m, uint8_t type, const void *pPayload, uint16_t length)
{
  if (length > BM_LINK_MAX_PAYLOAD) return false;
  if (m->queueCount == m->queueCapacity)
  {
    m->queueCapacity = m->queueCapacity ? m->queueCapacity * 2 : 32;
    m->pQueue = realloc(m->pQueue, m->queueCapacity * sizeof(BmLinkPacket));
  }
  BmLinkPacket *p = &m->pQueue[m->queueCount];
  p->type = type;
  p->seq = (uint8_t)(m->txBase + m->queueCount);
  p->flags = 0;
  p->length = length;
  if (length != 0) memcpy(p->payload, pPayload, length);
  m->queueCount++;
  return true;
}

static void pop(BmLinkMaster *m, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++) m->payloadBytesAcked += m->pQueue[i].length;
  memmove(m->pQueue, m->pQueue + n, (m->queueCount - n) * sizeof(BmLinkPacket));
  m->queueCount -= n;
  m->txBase = (uint8_t)(m->txBase + n);
}

static void adaptAfter(BmLinkMaster *m, bool clean)
{
  if (!m->adaptive) return;
  if (clean)
  {
    if (++m->cleanWindows >= m->cleanWindowsToLower && m->delay > m->minDelay)
    {
      bmLinkMasterSetDelay(m, m->delay - 1);
      m->cleanWindows = 0;
    }
  }
  else
  {
    m->cleanWindows = 0;
    uint32_t d = m->delay * 2 + 1;
    bmLinkMasterSetDelay(m, d > m->maxDelay ? m->maxDelay : d);
  }
}

bool bmLinkMasterResync(BmLinkMaster *m)
{
  for (int attempt = 0; attempt < 4; attempt++)
  {
    m->counters.resyncs++;
    for (int i = 0; i < 4; i++) xfer(m, BM_LINK_WORD_FILL);
    m->sawSync = false;
    xfer(m, BM_LINK_WORD_SYNC);
    for (uint32_t k = 0; k < m->pollTimeout && !m->sawSync; k++) xfer(m, BM_LINK_WORD_POLL);
    if (m->sawSync)
    {
      m->answer = 0;
      m->ackPending = m->nakPending = false;
      return true;
    }
    adaptAfter(m, false);
  }
  m->pError = "the slave does not answer a resync";
  return false;
}

bool bmLinkMasterFlush(BmLinkMaster *m)
{
  uint32_t attempts = 0, resyncs = 0, busyWaits = 0;
  while (m->queueCount != 0)
  {
    uint32_t n = m->queueCount < BM_LINK_WINDOW ? m->queueCount : BM_LINK_WINDOW;
    for (uint32_t i = 0; i < n; i++)
    {
      if (m->ackPending || m->nakPending) pollOrAck(m);
      sendPacket(m, &m->pQueue[i]);
    }

    // Poll until the slave answers for this window. Answers that report no progress may be
    // stale (left over from the last window's polls); a few of them count as "nothing arrived".
    uint32_t progress = 0, stale = 0;
    bool busy = false, answered = false;
    m->answer = 0;
    xfer(m, BM_LINK_WORD_POLL);
    for (uint32_t k = 0; k < m->pollTimeout && !answered; k++)
    {
      if (m->answer == 0)
      {
        pollOrAck(m);
        continue;
      }
      uint8_t diff = (uint8_t)(m->answerSeq - m->txBase);
      if (m->answer == BM_LINK_RX_ACK)
      {
        if (diff < n) { progress = diff + 1u; answered = true; }
        else if (++stale >= STALE_ANSWERS) answered = true;
      }
      else if (diff <= n)  // NAK or BUSY with the sequence number expected next
      {
        progress = diff;
        busy = m->answer == BM_LINK_RX_BUSY;
        answered = true;
      }
      else if (++stale >= STALE_ANSWERS)
      {
        answered = true;
      }
      m->answer = 0;
      if (!answered) pollOrAck(m);
    }

    if (progress != 0) pop(m, progress);
    if (busy)
    {
      // The slave works through what it has; give it time, this is not an error.
      if (++busyWaits > MAX_BUSY_WAITS)
      {
        m->pError = "the slave stays busy";
        return false;
      }
      for (int k = 0; k < BUSY_WAIT_POLLS; k++) pollOrAck(m);
      attempts = 0;
      continue;
    }
    if (progress == n)
    {
      attempts = 0;
      adaptAfter(m, true);
      continue;
    }
    m->counters.retransmits++;
    adaptAfter(m, false);
    if (progress != 0)
    {
      attempts = 0;
      continue;
    }
    if (++attempts >= MAX_ATTEMPTS)
    {
      attempts = 0;
      if (++resyncs > m->maxResyncs || !bmLinkMasterResync(m))
      {
        if (m->pError == NULL) m->pError = "too many failed windows";
        return false;
      }
    }
  }
  return true;
}

bool bmLinkMasterReceive(BmLinkMaster *m, uint8_t type, BmLinkPacket *pPacket, uint32_t maxPolls)
{
  for (uint32_t k = 0;; k++)
  {
    for (uint32_t i = 0; i < m->inboxCount; i++)
    {
      if (type != 0 && m->pInbox[i].type != type) continue;
      if (pPacket != NULL) *pPacket = m->pInbox[i];
      memmove(m->pInbox + i, m->pInbox + i + 1, (m->inboxCount - i - 1) * sizeof(BmLinkPacket));
      m->inboxCount--;
      // Make sure the slave hears the acknowledgement before we stop clocking.
      while (m->ackPending || m->nakPending) pollOrAck(m);
      return true;
    }
    if (k >= maxPolls) break;
    pollOrAck(m);
  }
  m->pError = "no answer from the slave";
  return false;
}

bool bmLinkMasterRequest(BmLinkMaster *m, uint8_t type, const void *pPayload, uint16_t length,
                         uint8_t answerType, BmLinkPacket *pAnswer, uint32_t maxPolls)
{
  if (!bmLinkMasterQueue(m, type, pPayload, length) || !bmLinkMasterFlush(m)) return false;
  return answerType == 0 || bmLinkMasterReceive(m, answerType, pAnswer, maxPolls);
}
