/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Xboo cable on the parallel port through Linux ppdev, ported from xcomms (branch parport,
// source/parport.cxx and source/xboo.cxx): the same bit timing, delay loop and multiboot
// sequence, so whatever xcomms boots this boots too.
//
// Wires (DB-25 pins): SC on STROBE (1), SI on AUTOFD (14), SO on ACK (10), SD on SELECTIN
// (17), reset on data bit 2 (4), GND. Only SC, SI, SO and GND are needed.

#include "link_master.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct BmXboo
{
  int fd;
  unsigned char control;     // last value written to the control port
  uint32_t delay;            // xcomms "Transfer Delay": PortDelay(delay) after each word
  bool resetWired;           // reset setting (link-protocol.md §6): none or xboo
  uint16_t crc1, crc2;       // multiboot CRC
  char error[256];
} BmXboo;

bool bmXbooOpen(BmXboo *pXboo, const char *pDevice);
void bmXbooClose(BmXboo *pXboo);

uint32_t bmXbooExchange(BmXboo *pXboo, uint32_t out);   // one word, then the delay
void bmXbooPortDelay(BmXboo *pXboo, uint32_t delay);
void bmXbooReset(BmXboo *pXboo);

/// Boots `image` (a multiboot image, header included) with the BIOS protocol exactly as
/// xcomms NormalBoot does. With the reset wire the GBA is reset first; otherwise it must be
/// switched on with no cartridge (or waiting for multiboot).
bool bmXbooBoot(BmXboo *pXboo, const uint8_t *pImage, uint32_t size);

/// As a link transport.
BmLinkTransport bmXbooTransport(BmXboo *pXboo);
