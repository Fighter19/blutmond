/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// GDB for a running game (docs/spec/link-protocol.md §8): a server for GDB's remote protocol
// on a local port, which turns what GDB asks into debugger packets on the link. The runtime
// only knows how to stop, show and change registers and memory, and keep breakpoints; single
// steps are worked out here (where the instruction goes next, and a breakpoint put there).

#include <stdbool.h>
#include <stdint.h>

#include "link_master.h"

/// Waits for GDB on 127.0.0.1:`port` and serves it until it detaches or disconnects; the
/// program then runs on without breakpoints. False if the link failed or the runtime cannot
/// be debugged.
bool bmGdbBridgeRun(BmLinkMaster *pMaster, uint16_t port, bool verbose);

/// Reads a word of the program's memory; false if it cannot be read.
typedef bool (*BmGdbReadWord)(void *pContext, uint32_t address, uint32_t *pWord);

/// Where the ARM instruction at regs[15] continues (regs: r0-r15, cpsr). Bit 0 of the result
/// set: it switches to Thumb code there. False if memory could not be read.
bool bmGdbNextArm(const uint32_t regs[17], BmGdbReadWord read, void *pContext, uint32_t *pNext);
