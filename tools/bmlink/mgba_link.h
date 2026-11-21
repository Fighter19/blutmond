/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// A GBA image running in mGBA (libmgba) as the far end of the link: the serial port in
// normal mode with external clock is driven word by word. Between words the emulator runs
// for the time a word takes on the cable. A word clocked while the program has not armed
// the port is lost (as on hardware); bit errors can be added.

#include "link_master.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct BmMgbaLink BmMgbaLink;

BmMgbaLink *bmMgbaLinkOpen(const char *pImage);
void bmMgbaLinkClose(BmMgbaLink *pLink);

void bmMgbaLinkSetCyclesPerWord(BmMgbaLink *pLink, uint32_t cycles);
void bmMgbaLinkSetErrors(BmMgbaLink *pLink, uint32_t flipPerMillion, uint32_t missPerMillion, uint32_t seed);
void bmMgbaLinkRunFrames(BmMgbaLink *pLink, uint32_t frames);

/// "<elf>:<symbol>[+offset]=<value>" or "<=" or no value (print). False on a mismatch.
bool bmMgbaLinkCheck(BmMgbaLink *pLink, const char *pSpec);
/// "<elf>:<symbol>[+offset]=<value>": true once the value in memory is at least that.
bool bmMgbaLinkReached(BmMgbaLink *pLink, const char *pSpec);
bool bmMgbaLinkScreenshot(BmMgbaLink *pLink, const char *pPath);

typedef struct BmMgbaLinkStats
{
  uint64_t words, lost, flips;
  uint64_t cycles;           // emulated time spent in exchanges
} BmMgbaLinkStats;
BmMgbaLinkStats bmMgbaLinkStats(const BmMgbaLink *pLink);

BmLinkTransport bmMgbaLinkTransport(BmMgbaLink *pLink);

/// Two GBAs joined by a link cable (tools/bm_mgba_pair): runs both for `cycles` of emulated
/// time in small steps. When one starts a transfer with its own clock (normal mode, 32 bits),
/// the word goes across after the time 32 bits take at its clock (2 MHz or 256 kHz); the other
/// side takes part if its port is armed for an outside clock, else the word is lost.
typedef struct BmMgbaPairStats
{
  uint64_t words, lost;      // transfers a master started; those the other side missed
} BmMgbaPairStats;
void bmMgbaPairRun(BmMgbaLink *pA, BmMgbaLink *pB, uint64_t cycles, BmMgbaPairStats *pStats);
