/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The live link to the PC game over TCP (apps/game/host_link.h): 4 bytes little-endian per
// word each way.

#include "link_master.h"

#include <stdbool.h>

typedef struct BmTcpLink
{
  int fd;
  char error[256];
} BmTcpLink;

/// `address` is "host:port" or just "port" (127.0.0.1).
bool bmTcpLinkOpen(BmTcpLink *pLink, const char *address);
void bmTcpLinkClose(BmTcpLink *pLink);
BmLinkTransport bmTcpLinkTransport(BmTcpLink *pLink);
