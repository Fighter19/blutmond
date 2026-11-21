/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The sending side of the GBA BIOS's multiboot protocol in normal mode (32 bit), as xcomms'
// NormalBoot does it: for the PC over the Xboo cable (tools/bmlink) and for a GBA that boots
// another one (transfer.h). The receiver is the BIOS of a GBA switched on without a cartridge.

#include <stdbool.h>
#include <stdint.h>

typedef struct BmMultibootPort
{
  void *pContext;
  uint32_t (*exchange)(void *pContext, uint32_t out);   // one word each way
  void (*delay)(void *pContext, uint32_t units);        // may be NULL; units as xcomms' PortDelay
  uint32_t wordDelay;                                   // delay after each word (xcomms "Transfer Delay")
} BmMultibootPort;

#define BM_MULTIBOOT_MAX_SIZE (256u * 1024u)

/// Sends a multiboot image (with its header; up to 256 KB). On failure `*ppError` says at
/// which step.
bool bmMultibootSend(const BmMultibootPort *pPort, const uint8_t *pImage, uint32_t size, const char **ppError);
