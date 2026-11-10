/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Link protocol: master and slave connected in-process through a simulated wire that can
// flip bits and drop words (the slave was not ready when the master clocked).

#include "link.h"
#include "link_master.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

typedef struct Wire
{
  BmLinkSlave slave;
  uint32_t prepared;          // word the slave put into its data register
  uint32_t rng;
  uint32_t flipPerMillion;    // chance per word and direction of one flipped bit
  uint32_t missPerMillion;    // chance per word that the slave misses the transfer
  uint32_t flips, misses;
  // What the slave received, in order.
  uint8_t *pReceived;
  uint32_t receivedSize, receivedCapacity;
  uint32_t packets;
  uint32_t refuseEvery;       // the handler refuses every n-th packet once (busy)
  uint32_t handlerCalls;
} Wire;

static uint32_t rnd(Wire *w)
{
  w->rng = w->rng * 1664525u + 1013904223u;
  return w->rng >> 8;
}

static bool chance(Wire *w, uint32_t perMillion)
{
  return perMillion != 0 && rnd(w) % 1000000u < perMillion;
}

static uint32_t wireExchange(void *pContext, uint32_t out)
{
  Wire *w = pContext;
  if (chance(w, w->missPerMillion))
  {
    w->misses++;
    return 0xFFFFFFFFu;  // SO stayed high; the slave saw nothing
  }
  if (chance(w, w->flipPerMillion)) { out ^= 1u << (rnd(w) & 31); w->flips++; }
  uint32_t in = w->prepared;
  if (chance(w, w->flipPerMillion)) { in ^= 1u << (rnd(w) & 31); w->flips++; }
  w->prepared = bmLinkSlaveExchange(&w->slave, out);
  return in;
}

static BmLinkTake handler(void *pContext, BmLinkPacket *p)
{
  Wire *w = pContext;
  w->handlerCalls++;
  if (w->refuseEvery != 0 && w->handlerCalls % w->refuseEvery == 0) return BM_LINK_REFUSED;
  if (w->receivedSize + p->length + 1 > w->receivedCapacity)
  {
    w->receivedCapacity = (w->receivedSize + p->length + 1) * 2;
    w->pReceived = realloc(w->pReceived, w->receivedCapacity);
  }
  w->pReceived[w->receivedSize++] = p->type;
  memcpy(w->pReceived + w->receivedSize, p->payload, p->length);
  w->receivedSize += p->length;
  w->packets++;
  return BM_LINK_DONE;
}

static void wireInit(Wire *w, BmLinkMaster *m, uint32_t seed)
{
  memset(w, 0, sizeof(*w));
  w->rng = seed;
  bmLinkSlaveInit(&w->slave, handler, w);
  w->prepared = BM_LINK_WORD_IDLE;
  BmLinkTransport t = { w, wireExchange, NULL, NULL, 0 };
  bmLinkMasterInit(m, &t);
}

/// Sends `count` packets of pseudo-random sizes and checks that the slave got exactly them.
static bool sendAndCheck(Wire *w, BmLinkMaster *m, uint32_t count, uint32_t seed)
{
  uint8_t *expected = malloc(count * (BM_LINK_MAX_PAYLOAD + 1));
  uint32_t expectedSize = 0;
  uint32_t r = seed;
  static uint8_t payload[BM_LINK_MAX_PAYLOAD];
  for (uint32_t i = 0; i < count; i++)
  {
    r = r * 1103515245u + 12345u;
    uint16_t length = (uint16_t)((r >> 8) % (BM_LINK_MAX_PAYLOAD + 1));
    if (i % 7 == 0) length = BM_LINK_MAX_PAYLOAD;
    for (uint16_t k = 0; k < length; k++)
    {
      r = r * 1103515245u + 12345u;
      payload[k] = (uint8_t)(r >> 16);
      if (i % 5 == 0) payload[k] = 0xFF;  // data that looks like fill words
    }
    uint8_t type = (uint8_t)(BM_LINK_CHUNK_DATA + (i % 3));
    expected[expectedSize++] = type;
    memcpy(expected + expectedSize, payload, length);
    expectedSize += length;
    if (!bmLinkMasterQueue(m, type, payload, length)) { free(expected); return false; }
  }
  bool ok = bmLinkMasterFlush(m);
  if (!ok) printf("flush failed: %s\n", m->pError ? m->pError : "?");
  ok = ok && w->receivedSize == expectedSize && memcmp(w->pReceived, expected, expectedSize) == 0;
  free(expected);
  return ok;
}

static void testCleanTransfer(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 1);
  TEST_ASSERT(sendAndCheck(&w, &m, 300, 7));  // more than 256: sequence numbers wrap
  TEST_ASSERT(w.packets == 300);
  TEST_ASSERT(m.counters.retransmits == 0 && m.counters.resyncs == 0);
  TEST_ASSERT(w.slave.counters.badPackets == 0);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

static void testSlaveToMaster(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 2);
  w.flipPerMillion = 2000;
  w.missPerMillion = 1000;
  uint8_t text[64];
  for (uint32_t i = 0; i < 40; i++)
  {
    // Fill the slave's ring, then let the master collect.
    memset(text, (int)i, sizeof(text));
    while (!bmLinkSlaveSend(&w.slave, BM_LINK_LOG, text, (uint16_t)(1 + i)))
      bmLinkMasterReceive(&m, 0xEE, NULL, 50);  // clocks; received packets stay in the inbox
  }
  uint32_t got = 0;
  BmLinkPacket p;
  while (got < 40 && bmLinkMasterReceive(&m, BM_LINK_LOG, &p, 20000))
  {
    TEST_ASSERT(p.length == got + 1 && p.payload[0] == (uint8_t)got);
    got++;
  }
  TEST_ASSERT(got == 40);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

static void testBitErrorsAndMisses(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 3);
  w.flipPerMillion = 1000;   // about one bit error per 1000 words each way
  w.missPerMillion = 500;
  TEST_ASSERT(sendAndCheck(&w, &m, 400, 11));
  TEST_ASSERT(w.flips > 50 && w.misses > 20);
  TEST_ASSERT(m.counters.retransmits > 0);
  printf("  %u words, %u flips, %u misses, %u retransmitted windows, %u resyncs\n", (unsigned)m.counters.words,
         (unsigned)w.flips, (unsigned)w.misses, (unsigned)m.counters.retransmits, (unsigned)m.counters.resyncs);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

static void testHeavyErrorsNeedResync(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 4);
  w.flipPerMillion = 8000;
  w.missPerMillion = 4000;
  m.maxResyncs = 1000;
  TEST_ASSERT(sendAndCheck(&w, &m, 120, 13));
  printf("  %u words, %u retransmitted windows, %u resyncs\n", (unsigned)m.counters.words,
         (unsigned)m.counters.retransmits, (unsigned)m.counters.resyncs);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

static void testBusySlave(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 5);
  w.refuseEvery = 3;
  TEST_ASSERT(sendAndCheck(&w, &m, 100, 17));
  TEST_ASSERT(w.slave.counters.busy > 10);
  TEST_ASSERT(m.counters.resyncs == 0);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

static void testAdaptiveDelay(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 6);
  m.adaptive = true;
  m.minDelay = 1;
  bmLinkMasterSetDelay(&m, 20);
  TEST_ASSERT(sendAndCheck(&w, &m, 300, 19));
  TEST_ASSERT(m.delay < 20);  // clean windows lowered it
  w.flipPerMillion = 3000;
  w.receivedSize = 0;
  m.maxResyncs = 1000;
  TEST_ASSERT(sendAndCheck(&w, &m, 20, 23));
  TEST_ASSERT(m.counters.retransmits > 0);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

static void testNoSlave(void)
{
  Wire w;
  BmLinkMaster m;
  wireInit(&w, &m, 7);
  w.missPerMillion = 1000000;  // nothing connected
  m.maxResyncs = 2;
  uint8_t b = 1;
  bmLinkMasterQueue(&m, BM_LINK_PING, &b, 1);
  TEST_ASSERT(!bmLinkMasterFlush(&m));
  TEST_ASSERT(m.pError != NULL);
  bmLinkMasterFree(&m);
  free(w.pReceived);
}

int main(void)
{
  RUN_TEST(testCleanTransfer);
  RUN_TEST(testSlaveToMaster);
  RUN_TEST(testBitErrorsAndMisses);
  RUN_TEST(testHeavyErrorsNeedResync);
  RUN_TEST(testBusySlave);
  RUN_TEST(testAdaptiveDelay);
  RUN_TEST(testNoSlave);
  return TEST_RESULT();
}
