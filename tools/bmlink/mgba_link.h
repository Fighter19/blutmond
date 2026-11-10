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
bool bmMgbaLinkScreenshot(BmMgbaLink *pLink, const char *pPath);

typedef struct BmMgbaLinkStats
{
  uint64_t words, lost, flips;
  uint64_t cycles;           // emulated time spent in exchanges
} BmMgbaLinkStats;
BmMgbaLinkStats bmMgbaLinkStats(const BmMgbaLink *pLink);

BmLinkTransport bmMgbaLinkTransport(BmMgbaLink *pLink);
