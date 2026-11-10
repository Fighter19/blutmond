/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Live link protocol (docs/spec/link-protocol.md): words, packets, the receive state machine
// and the slave side that the runtime runs from its serial interrupt. The master side (PC
// tools) is in link_master.h.
//
// Every exchange moves one 32-bit word in each direction. The slave prepares its word before
// the master clocks it, so it answers "one word late".

#include <stdbool.h>
#include <stdint.h>

#define BM_LINK_PROTOCOL_VERSION 1

#define BM_LINK_WORD_POLL 0x424D504Cu  // "BMPL", master: give me your next word
#define BM_LINK_WORD_IDLE 0x424D4944u  // "BMID", slave: nothing to send
#define BM_LINK_WORD_SYNC 0x53594E43u  // "SYNC", after four 0xFFFFFFFF
#define BM_LINK_WORD_FILL 0xFFFFFFFFu
#define BM_LINK_ACK(seq) (0x42410000u | ((uint32_t)(uint8_t)(seq) << 8))
#define BM_LINK_NAK(seq) (0x424E0000u | ((uint32_t)(uint8_t)(seq) << 8))
// NAK with bit 0 set: the receiver was busy (not an error); `seq` is expected next.
#define BM_LINK_BUSY(seq) (0x424E0001u | ((uint32_t)(uint8_t)(seq) << 8))

#define BM_LINK_MAX_PAYLOAD 1024
#define BM_LINK_WINDOW 16

typedef enum BmLinkType {
  BM_LINK_HELLO = 0x01,
  BM_LINK_PING = 0x02,
  BM_LINK_CHUNK_BEGIN = 0x10,
  BM_LINK_CHUNK_DATA = 0x11,
  BM_LINK_CHUNK_END = 0x12,
  BM_LINK_TOC = 0x13,
  BM_LINK_DROP = 0x14,
  BM_LINK_SESSION = 0x15,
  BM_LINK_STATUS = 0x16,
  BM_LINK_TEST = 0x17,
  BM_LINK_APPLY = 0x18,
  BM_LINK_STATE_GET = 0x20,
  BM_LINK_STATE_DATA = 0x21,
  BM_LINK_STATE_PUT = 0x22,
  BM_LINK_LOG = 0x30,
  BM_LINK_PROFILE = 0x31,
  BM_LINK_PPU_SNAPSHOT = 0x32,
  BM_LINK_MEM_READ = 0x38,
  BM_LINK_MEM_WRITE = 0x39,
  BM_LINK_GDB = 0x40,
  BM_LINK_WATCHDOG = 0x50,
  BM_LINK_ERROR = 0x7F,
} BmLinkType;

typedef struct BmLinkPacket
{
  uint8_t type, seq, flags;
  uint16_t length;                              // payload bytes
  uint8_t payload[BM_LINK_MAX_PAYLOAD];
} BmLinkPacket;

/// Number of words a packet takes on the wire (header, length, payload, CRC).
static inline uint32_t bmLinkPacketWords(uint16_t length)
{
  return 3u + (length + 3u) / 4u;
}

/// Word `index` of a packet as sent (0 .. bmLinkPacketWords - 1). The CRC word needs `crc`,
/// the running CRC over the words before it (bmLinkCrcWord).
uint32_t bmLinkPacketWord(const BmLinkPacket *pPacket, uint32_t index);
uint32_t bmLinkCrcWord(uint32_t crc, uint32_t word);

// --- Receiving -----------------------------------------------------------------------------

typedef enum BmLinkRxEvent {
  BM_LINK_RX_NONE = 0,     // inside a packet, nothing complete yet
  BM_LINK_RX_NOISE,        // a word outside a packet that means nothing
  BM_LINK_RX_POLL,
  BM_LINK_RX_IDLE,
  BM_LINK_RX_ACK,          // *pSeq: last sequence number received in order
  BM_LINK_RX_NAK,          // *pSeq: sequence number expected next
  BM_LINK_RX_BUSY,         // *pSeq: expected next; the receiver could not take it yet
  BM_LINK_RX_SYNC,
  BM_LINK_RX_PACKET,       // a packet with a correct CRC is in the receive buffer
  BM_LINK_RX_BAD_PACKET,   // a packet ended with a wrong CRC, or its length was impossible
} BmLinkRxEvent;

typedef struct BmLinkRx
{
  BmLinkPacket *pPacket;   // where packets are received
  uint32_t crc;
  uint16_t wordIndex, wordCount;
  uint8_t state;
  uint8_t fillCount;       // consecutive 0xFFFFFFFF words (resync detection)
} BmLinkRx;

void bmLinkRxInit(BmLinkRx *pRx, BmLinkPacket *pBuffer);
BmLinkRxEvent bmLinkRxWord(BmLinkRx *pRx, uint32_t word, uint8_t *pSeq);

// --- Slave ---------------------------------------------------------------------------------

/// What a packet handler did with a packet.
typedef enum BmLinkTake {
  BM_LINK_REFUSED = 0,  // cannot take it now: not acknowledged, the master sends it again
  BM_LINK_DONE = 1,     // handled; the buffer is reused
  BM_LINK_KEPT = 2,     // the handler keeps the buffer until bmLinkSlaveRelease
} BmLinkTake;

/// Called (from the serial interrupt) for each packet received in order with a good CRC.
/// Must be quick: the next word arrives soon. Long work is done later on kept packets.
typedef BmLinkTake (*BmLinkPacketHandler)(void *pContext, BmLinkPacket *pPacket);

#ifndef BM_LINK_SLAVE_RX_SLOTS
#  define BM_LINK_SLAVE_RX_SLOTS 6
#endif

#ifndef BM_LINK_SLAVE_TX_SLOTS
#  define BM_LINK_SLAVE_TX_SLOTS 4
#endif

typedef struct BmLinkCounters
{
  uint32_t words;
  uint32_t packetsIn, packetsOut;
  uint32_t badPackets;    // CRC errors and impossible lengths
  uint32_t outOfOrder;    // packets dropped because one before them was missing
  uint32_t busy;          // packets the handler could not take
  uint32_t retransmits;   // windows sent again
  uint32_t resyncs;
} BmLinkCounters;

typedef struct BmLinkSlave
{
  BmLinkRx rx;
  BmLinkPacket rxSlots[BM_LINK_SLAVE_RX_SLOTS];
  uint8_t rxHeld;          // bit per slot kept by the handler
  BmLinkPacketHandler handler;
  void *pContext;
  uint8_t rxExpected;      // next sequence number from the master
  bool nakPending;         // a packet was lost since the last answer to a poll
  bool busyPending;        // the handler refused a packet since the last answer
  bool answerPending;      // a poll waits for an ACK/NAK
  uint8_t answerHold;      // polls to ignore after an answer
  uint8_t syncWords;       // words of the resync answer still to send (4 fill + SYNC)

  // Transmit ring: packets from `txBase` on are not yet acknowledged.
  BmLinkPacket tx[BM_LINK_SLAVE_TX_SLOTS];
  uint8_t txBase;          // sequence number of the oldest unacknowledged packet
  uint8_t txCount;         // packets in the ring
  uint8_t txSend;          // packets of the ring already (being) sent
  uint16_t txWord;         // word of packet `txSend` to send next
  uint32_t txCrc;
  uint16_t pollsWithoutAck;
  BmLinkCounters counters;
} BmLinkSlave;

void bmLinkSlaveInit(BmLinkSlave *pSlave, BmLinkPacketHandler handler, void *pContext);

/// One exchange: `received` is the word the master just sent; returns the word to send in the
/// next exchange. Call from the serial interrupt.
uint32_t bmLinkSlaveExchange(BmLinkSlave *pSlave, uint32_t received);

/// Receive buffers not kept by the handler. A handler may only keep a packet while at least
/// two are free (one keeps receiving).
uint32_t bmLinkSlaveRxFree(const BmLinkSlave *pSlave);

/// Gives a kept packet's buffer back (bmHalLinkLock around it from the main loop).
void bmLinkSlaveRelease(BmLinkSlave *pSlave, const BmLinkPacket *pPacket);

/// Queues a packet to the master. Returns false if the ring is full. Not for the main loop
/// while the serial interrupt can run; use Begin/Commit there.
bool bmLinkSlaveSend(BmLinkSlave *pSlave, uint8_t type, const void *pPayload, uint16_t length);

/// Sending from the main loop: Begin returns a free transmit buffer (or NULL) that the caller
/// fills (type, length, payload) without blocking the interrupt; Commit (with the interrupt
/// blocked, it only counts) queues it.
BmLinkPacket *bmLinkSlaveSendBegin(BmLinkSlave *pSlave);
void bmLinkSlaveSendCommit(BmLinkSlave *pSlave);

/// Free transmit slots.
uint32_t bmLinkSlaveTxFree(const BmLinkSlave *pSlave);

// --- Payload helpers (little endian) -------------------------------------------------------

static inline void bmLinkPut16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static inline void bmLinkPut32(uint8_t *p, uint32_t v)
{
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static inline uint16_t bmLinkGet16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t bmLinkGet32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
