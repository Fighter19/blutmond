/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Words of the bootstrap's protocol (link-protocol.md §9), shared by the bootstrap (src/hal/gba/bootstrap) and the sender
// (src/link/transfer.c). Serial port in normal mode, 32 bits; the sender makes the clock and
// reads each answer one word late:
//
//   sender   START   size    word 0  word 1 ... word n-1   crc     any
//   answer   (idle)  READY   size    1      ... n-1        n       OK or BAD
//
// The size is in bytes, a multiple of 4. The checksum is the CRC-32 of the words (bmCrc32).
// An answer that is not the expected one means a word was lost: the sender starts again.

#define BM_BOOTSTRAP_START 0x424D4253u   // "BMBS"
#define BM_BOOTSTRAP_READY 0x424D4252u   // "BMBR"
#define BM_BOOTSTRAP_OK    0x424D4F4Bu   // "BMOK"
#define BM_BOOTSTRAP_BAD   0x424D4E4Fu   // "BMNO"
#define BM_BOOTSTRAP_IDLE  0x424D4249u   // "BMBI"
#define BM_BOOTSTRAP_MAX_SIZE (256u * 1024u)
