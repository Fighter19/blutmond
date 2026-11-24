/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "transfer.h"

#include <string.h>

#include "bootstrap_protocol.h"
#include "hal.h"
#include "link.h"
#include "link_master.h"
#include "live_master.h"
#include "multiboot.h"
#include "pack.h"
#include "shared.h"

// Time between words. A running runtime takes each word in its serial interrupt and needs
// about 600 cycles for it (link-protocol.md §7); the bootstrap polls and is quicker.
#define PACKET_GAP 800
#define BOOTSTRAP_GAP 120

static BmLinkMaster s_master BM_EWRAM_BSS;
static BmLinkPacket s_queue[6] BM_EWRAM_BSS;
static BmLinkPacket s_inbox[3] BM_EWRAM_BSS;
static BmLiveRemote s_remote BM_EWRAM_BSS;
static uint32_t s_words;

static uint32_t exchangePacket(void *pContext, uint32_t out)
{
  (void)pContext;
  s_words++;
  uint32_t in = bmHalLinkMasterExchange(out);
  bmHalLinkMasterWait(PACKET_GAP);
  return in;
}

static void pausePacket(void *pContext, uint32_t milliseconds)
{
  (void)pContext;
  bmHalLinkMasterWait(milliseconds * 16780u);
}

static uint32_t exchangeBootstrap(uint32_t out)
{
  s_words++;
  uint32_t in = bmHalLinkMasterExchange(out);
  bmHalLinkMasterWait(BOOTSTRAP_GAP);
  return in;
}

static uint32_t exchangeBios(void *pContext, uint32_t out)
{
  (void)pContext;
  s_words++;
  return bmHalLinkMasterExchange(out);
}

static void delayBios(void *pContext, uint32_t units)
{
  (void)pContext;
  bmHalLinkMasterWait(units * 64);
}

/// Is a bootstrap waiting? (It answers the start word with "ready".)
static bool bootstrapThere(void)
{
  for (int i = 0; i < 4; i++)
  {
    exchangeBootstrap(BM_BOOTSTRAP_START);
    // The answer comes with the next word; an idle word makes the bootstrap wait again.
    if (exchangeBootstrap(BM_BOOTSTRAP_IDLE) == BM_BOOTSTRAP_READY) return true;
  }
  return false;
}

/// The image's words, each answered with the number of words taken before it (the first with
/// the size). Returns how many went through; `*pCrc` is their running checksum. From fast
/// memory: this loop is what the transfer's time is made of.
BM_IWRAM_CODE static uint32_t sendWords(const uint8_t *pImage, uint32_t words, uint32_t size, uint32_t *pCrc)
{
  uint32_t crc = 0;
  for (uint32_t i = 0; i < words; i++)
  {
    const uint8_t *p = pImage + i * 4;
    uint32_t word = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    uint32_t in = bmHalLinkMasterExchange(word);
    crc = bmLinkCrcWord(crc, word);   // part of the time the bootstrap gets for its word
    bmHalLinkMasterWait(BOOTSTRAP_GAP);
    if (in != (i == 0 ? size : i)) return i;
  }
  *pCrc = crc;
  return words;
}

/// One attempt to send an image through the bootstrap.
static bool sendThroughBootstrap(const uint8_t *pImage, uint32_t size, bool again)
{
  uint32_t words = (size + 3) / 4, padded = words * 4, whole = size / 4, crc = 0;
  // A bootstrap in the middle of an earlier attempt takes words as data until it has its
  // count; keep saying start until it answers.
  uint32_t tries = 0;
  for (;;)
  {
    // If only the last answer of the attempt before was lost, it comes now: the image runs.
    if (exchangeBootstrap(BM_BOOTSTRAP_START) == BM_BOOTSTRAP_OK && again && tries == 0) return true;
    if (exchangeBootstrap(padded) == BM_BOOTSTRAP_READY) break;
    if (++tries > BM_BOOTSTRAP_MAX_SIZE / 8 + 16) return false;
  }
  if (sendWords(pImage, whole, padded, &crc) != whole) return false;
  s_words += whole;
  if (whole != words)
  {
    // The last word, padded with zeros.
    uint32_t word = 0;
    for (uint32_t k = 0; whole * 4 + k < size; k++) word |= (uint32_t)pImage[whole * 4 + k] << (k * 8);
    if (exchangeBootstrap(word) != (whole == 0 ? padded : whole)) return false;
    crc = bmLinkCrcWord(crc, word);
  }
  if (exchangeBootstrap(crc) != words) return false;
  return exchangeBootstrap(0) == BM_BOOTSTRAP_OK;
}

static bool hello(void)
{
  s_master.pollTimeout = 64;
  s_master.maxResyncs = 1;
  bool there = bmLiveMasterHello(&s_master, &s_remote);
  s_master.pollTimeout = 4000;
  s_master.maxResyncs = 8;
  return there;
}

BmTransferResult bmTransferSend(const BmTransferImages *pImages, const uint8_t *pPack, uint32_t packSize, int32_t room,
                                const uint8_t *pState, uint32_t stateSize, BmTransferStats *pStats)
{
  return bmTransferSendWith(pImages, pPack, packSize, NULL, 0, room, pState, stateSize, pStats);
}

BmTransferResult bmTransferSendWith(const BmTransferImages *pImages, const uint8_t *pPack, uint32_t packSize,
                                    const uint8_t *pSmaller, uint32_t smallerSize, int32_t room,
                                    const uint8_t *pState, uint32_t stateSize, BmTransferStats *pStats)
{
  BmTransferStats stats;
  memset(&stats, 0, sizeof(stats));
  BmTransferResult result = BM_TRANSFER_OK;
  s_words = 0;
  BmLinkTransport transport = { NULL, exchangePacket, NULL, NULL, 0, pausePacket };
  bmLinkMasterInitFixed(&s_master, &transport, s_queue, sizeof(s_queue) / sizeof(s_queue[0]), s_inbox, sizeof(s_inbox) / sizeof(s_inbox[0]));
  bmHalLinkMasterBegin(true);

  if (!hello())
  {
    // No runtime there. A bootstrap, or a GBA waiting in its BIOS?
    bool canBoot = pImages != NULL && pImages->pRuntime != NULL;
    if (!canBoot) result = BM_TRANSFER_NO_PEER;
    else if (!bootstrapThere())
    {
      if (pImages->pBootstrap == NULL) result = BM_TRANSFER_NO_PEER;
      else
      {
        // The BIOS protocol, at the slower clock.
        BmMultibootPort port = { NULL, exchangeBios, delayBios, 36 };
        bmHalLinkMasterBegin(false);
        stats.usedMultiboot = 1;
        if (!bmMultibootSend(&port, pImages->pBootstrap, pImages->bootstrapSize, NULL)) result = BM_TRANSFER_BOOT_FAILED;
        bmHalLinkMasterBegin(true);
        bmHalLinkMasterWait(2000000);   // the BIOS checks and starts the bootstrap
      }
    }
    if (result == BM_TRANSFER_OK)
    {
      stats.usedBootstrap = 1;
      uint32_t started = bmHalCycles();
      bool sent = false;
      while (!sent && stats.runtimeTries < 3)
      {
        sent = sendThroughBootstrap(pImages->pRuntime, pImages->runtimeSize, stats.runtimeTries != 0);
        stats.runtimeTries++;
      }
      stats.runtimeCycles = bmHalCycles() - started;
      if (!sent) result = BM_TRANSFER_RUNTIME_FAILED;
    }
    if (result == BM_TRANSFER_OK)
    {
      // The runtime starts up: ask until it answers.
      bool up = false;
      for (int i = 0; i < 200 && !up; i++)
      {
        bmHalLinkMasterWait(200000);
        up = hello();
      }
      if (!up) result = BM_TRANSFER_NO_RUNTIME;
    }
  }

  // Content: only if the other side runs something else, and then only what it lacks. The
  // smaller pack instead where the other side does not run this one and could not hold it.
  BmPack pack;
  if (result == BM_TRANSFER_OK && bmPackOpen(&pack, pPack, packSize, false) != BM_PACK_OK) result = BM_TRANSFER_PACK_REFUSED;
  if (result == BM_TRANSFER_OK && pSmaller != NULL && packSize > s_remote.hello.poolCapacity &&
      (s_remote.hello.activeSize != pack.size || s_remote.hello.activeCrc != pack.header.crc32))
  {
    pPack = pSmaller;
    packSize = smallerSize;
    stats.sentSmaller = 1;
    if (bmPackOpen(&pack, pPack, packSize, false) != BM_PACK_OK) result = BM_TRANSFER_PACK_REFUSED;
  }
  if (result == BM_TRANSFER_OK && (s_remote.hello.activeSize != pack.size || s_remote.hello.activeCrc != pack.header.crc32))
  {
    BmLivePushStats push;
    BmLiveStatus status;
    uint32_t id = 0x47324700u | (s_words & 0xFF);
    stats.sentPack = 1;
    if (!bmLiveMasterPushPackFor(&s_master, pPack, packSize, id, &s_remote, room, &push) || !bmLiveMasterApply(&s_master, id, BM_LIVE_RESTART, NULL, &status))
      result = BM_TRANSFER_LINK_FAILED;
    else if (status.result != BM_LIVE_OK) result = BM_TRANSFER_PACK_REFUSED;
    stats.chunksSent = push.chunksSent;
    stats.bytesSent = push.bytesSent;
  }
  if (result == BM_TRANSFER_OK)
  {
    if (!bmLiveMasterPutState(&s_master, pState, stateSize, &stats.peer)) result = BM_TRANSFER_LINK_FAILED;
    else if (stats.peer.result != BM_LIVE_OK) result = BM_TRANSFER_STATE_REFUSED;
  }
  bmHalLinkMasterEnd();
  stats.words = s_words;
  if (pStats != NULL) *pStats = stats;
  return result;
}
