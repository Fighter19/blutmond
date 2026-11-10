/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Master side of the live link (docs/spec/link-protocol.md), for PC tools: sends packets in
// windows with go-back-N retransmission, resyncs after repeated failures, adapts the per-word
// delay and collects the slave's packets.

#include "link.h"

#include <stddef.h>

enum {
  BM_LINK_CAP_RESET = 1u << 0,     // the reset wire is connected
  BM_LINK_CAP_SD_BURST = 1u << 1,
};

/// Moves 32-bit words to and from the slave. The master makes the clock.
typedef struct BmLinkTransport
{
  void *pContext;
  uint32_t (*exchange)(void *pContext, uint32_t out);
  void (*setDelay)(void *pContext, uint32_t delay);  // optional
  bool (*reset)(void *pContext);                     // optional (BM_LINK_CAP_RESET)
  uint32_t caps;
} BmLinkTransport;

typedef struct BmLinkMaster
{
  BmLinkTransport transport;
  BmLinkRx rx;
  BmLinkPacket rxPacket;
  uint8_t rxExpected;
  bool ackPending, nakPending;

  // Packets not yet acknowledged; pQueue[0] has sequence number txBase.
  BmLinkPacket *pQueue;
  uint32_t queueCount, queueCapacity;
  uint8_t txBase;

  // Packets received from the slave, oldest first.
  BmLinkPacket *pInbox;
  uint32_t inboxCount, inboxCapacity;

  // Last answer to a poll.
  uint8_t answer;          // 0 none, else the BmLinkRxEvent
  uint8_t answerSeq;
  bool sawSync;

  uint32_t pollTimeout;    // polls to wait for an answer (default 400)
  uint32_t maxResyncs;     // per flush before giving up (default 8)

  // Adaptive delay: lowered after `cleanWindowsToLower` clean windows, raised after errors.
  bool adaptive;
  uint32_t delay, minDelay, maxDelay;
  uint32_t cleanWindows, cleanWindowsToLower;

  BmLinkCounters counters;
  uint64_t payloadBytesAcked;
  const char *pError;      // why the last call failed
} BmLinkMaster;

void bmLinkMasterInit(BmLinkMaster *pMaster, const BmLinkTransport *pTransport);
void bmLinkMasterFree(BmLinkMaster *pMaster);

void bmLinkMasterSetDelay(BmLinkMaster *pMaster, uint32_t delay);

/// Queues a packet. It is sent by the next flush.
bool bmLinkMasterQueue(BmLinkMaster *pMaster, uint8_t type, const void *pPayload, uint16_t length);

/// Sends every queued packet and waits until all are acknowledged. False if the link failed.
bool bmLinkMasterFlush(BmLinkMaster *pMaster);

/// Waits (polling) for a packet from the slave, at most `maxPolls` polls. With `type` != 0
/// other packets stay in the inbox. False on timeout.
bool bmLinkMasterReceive(BmLinkMaster *pMaster, uint8_t type, BmLinkPacket *pPacket, uint32_t maxPolls);

/// Resynchronises both sides (four fill words and SYNC, until the slave answers SYNC).
bool bmLinkMasterResync(BmLinkMaster *pMaster);

/// Queue, flush, and wait for an answer of type `answerType` (0: none).
bool bmLinkMasterRequest(BmLinkMaster *pMaster, uint8_t type, const void *pPayload, uint16_t length,
                         uint8_t answerType, BmLinkPacket *pAnswer, uint32_t maxPolls);
