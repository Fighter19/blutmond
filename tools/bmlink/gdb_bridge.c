/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "gdb_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "live.h"

// --- Where an ARM instruction goes next ------------------------------------------------------

static bool conditionHolds(uint32_t cond, uint32_t cpsr)
{
  bool n = (cpsr >> 31) & 1, z = (cpsr >> 30) & 1, c = (cpsr >> 29) & 1, v = (cpsr >> 28) & 1;
  switch (cond)
  {
  case 0: return z;
  case 1: return !z;
  case 2: return c;
  case 3: return !c;
  case 4: return n;
  case 5: return !n;
  case 6: return v;
  case 7: return !v;
  case 8: return c && !z;
  case 9: return !c || z;
  case 10: return n == v;
  case 11: return n != v;
  case 12: return !z && n == v;
  case 13: return z || n != v;
  default: return true;
  }
}

static uint32_t shifted(uint32_t value, uint32_t type, uint32_t amount, bool immediate, bool carry)
{
  if (immediate && amount == 0)
  {
    // The special meanings of a shift by 0.
    switch (type)
    {
    case 0: return value;
    case 1: return 0;
    case 2: return (value & 0x80000000u) ? 0xFFFFFFFFu : 0;
    default: return (value >> 1) | ((uint32_t)carry << 31);
    }
  }
  if (amount == 0) return value;
  switch (type)
  {
  case 0: return amount >= 32 ? 0 : value << amount;
  case 1: return amount >= 32 ? 0 : value >> amount;
  case 2: return amount >= 32 ? ((value & 0x80000000u) ? 0xFFFFFFFFu : 0) : (uint32_t)((int32_t)value >> amount);
  default: amount &= 31; return amount == 0 ? value : (value >> amount) | (value << (32 - amount));
  }
}

bool bmGdbNextArm(const uint32_t regs[17], BmGdbReadWord read, void *pContext, uint32_t *pNext)
{
  uint32_t pc = regs[15], cpsr = regs[16], insn, word;
  if (!read(pContext, pc, &insn)) return false;
#define REG(n) ((n) == 15 ? pc + 8 : regs[(n)])
  *pNext = pc + 4;
  if (!conditionHolds(insn >> 28, cpsr)) return true;
  bool carry = (cpsr >> 29) & 1;
  if ((insn & 0x0FFFFFF0u) == 0x012FFF10u)
  {
    *pNext = REG(insn & 15);   // BX
  }
  else if ((insn & 0x0E000000u) == 0x0A000000u)
  {
    int32_t offset = (int32_t)(insn << 8) >> 6;   // B, BL: 24 bits, in words
    *pNext = pc + 8 + (uint32_t)offset;
  }
  else if ((insn & 0x0C000000u) == 0 && ((insn >> 12) & 15) == 15 && !((insn & 0x02000090u) == 0x00000090u))
  {
    // Data processing with pc as the destination.
    uint32_t op = (insn >> 21) & 15, rn = REG((insn >> 16) & 15), value;
    if (insn & 0x02000000u)
    {
      uint32_t rot = ((insn >> 8) & 15) * 2;
      value = insn & 0xFF;
      value = rot ? (value >> rot) | (value << (32 - rot)) : value;
    }
    else
    {
      bool byRegister = (insn & 0x10) != 0;
      uint32_t amount = byRegister ? (regs[(insn >> 8) & 15] & 0xFF) : ((insn >> 7) & 31);
      value = shifted(REG(insn & 15), (insn >> 5) & 3, amount, !byRegister, carry);
    }
    switch (op)
    {
    case 0: *pNext = rn & value; break;
    case 1: *pNext = rn ^ value; break;
    case 2: *pNext = rn - value; break;
    case 3: *pNext = value - rn; break;
    case 4: *pNext = rn + value; break;
    case 5: *pNext = rn + value + carry; break;
    case 6: *pNext = rn - value - !carry; break;
    case 7: *pNext = value - rn - !carry; break;
    case 12: *pNext = rn | value; break;
    case 13: *pNext = value; break;
    case 14: *pNext = rn & ~value; break;
    case 15: *pNext = ~value; break;
    default: break;   // comparisons do not write pc
    }
    *pNext &= ~3u;
  }
  else if ((insn & 0x0C500000u) == 0x04100000u && ((insn >> 12) & 15) == 15)
  {
    // LDR pc, [...]
    uint32_t rn = REG((insn >> 16) & 15), offset;
    if (insn & 0x02000000u) offset = shifted(REG(insn & 15), (insn >> 5) & 3, (insn >> 7) & 31, true, carry);
    else offset = insn & 0xFFF;
    uint32_t address = rn;
    if (insn & 0x01000000u) address = (insn & 0x00800000u) ? rn + offset : rn - offset;
    if (!read(pContext, address, &word)) return false;
    *pNext = word & ~3u;
  }
  else if ((insn & 0x0E108000u) == 0x08108000u)
  {
    // LDM with pc in the list: pc is the last word loaded.
    uint32_t count = 0, rn = regs[(insn >> 16) & 15], address;
    for (int i = 0; i < 16; i++) count += (insn >> i) & 1;
    bool before = (insn & 0x01000000u) != 0, up = (insn & 0x00800000u) != 0;
    if (up) address = rn + 4 * (count - 1) + (before ? 4 : 0);
    else address = rn - (before ? 4 : 0);
    if (!read(pContext, address, &word)) return false;
    *pNext = word & ~3u;
  }
#undef REG
  return true;
}

#if defined(__unix__) || defined(__APPLE__)
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>

#define SLOTS BM_HAL_DEBUG_SLOTS
#define ANSWER_POLLS 2000000u
#define PACKET_MAX 2048

typedef struct Bridge
{
  BmLinkMaster *pMaster;
  int client;
  bool verbose;
  bool stopped;            // the program is stopped (as far as we know)
  bool stopSeen;           // a "stopped" packet arrived while waiting for something else
  uint8_t stopReason;      // of the last stop: 0 asked to, 1 breakpoint
  bool linkFailed;
  struct { bool used; uint32_t address; } breaks[SLOTS];
  int stepSlot;            // the slot of a step's own breakpoint, or -1
  char in[PACKET_MAX];
  uint32_t inLength;
  bool inPacket;
} Bridge;

// --- The runtime side ------------------------------------------------------------------------

/// Sends a debugger command and waits for its answer. False if the link failed.
static bool command(Bridge *b, uint8_t cmd, uint8_t slot, const uint8_t *pArgs, uint16_t length, BmLinkPacket *pAnswer)
{
  static BmLinkPacket scratch;
  uint8_t payload[BM_LIVE_DEBUG_HEADER + BM_LIVE_DEBUG_DATA_MAX + 8];
  if (pAnswer == NULL) pAnswer = &scratch;
  payload[0] = cmd;
  payload[1] = 0;
  payload[2] = slot;
  payload[3] = 0;
  if (length != 0) memcpy(payload + BM_LIVE_DEBUG_HEADER, pArgs, length);
  if (!bmLinkMasterQueue(b->pMaster, BM_LINK_GDB, payload, (uint16_t)(BM_LIVE_DEBUG_HEADER + length)) || !bmLinkMasterFlush(b->pMaster))
  {
    b->linkFailed = true;
    return false;
  }
  if (cmd == BM_DEBUG_HALT) return true;   // answered by a "stopped" packet, later
  for (;;)
  {
    if (!bmLinkMasterReceive(b->pMaster, BM_LINK_GDB, pAnswer, ANSWER_POLLS))
    {
      b->linkFailed = true;
      return false;
    }
    if (pAnswer->length < BM_LIVE_DEBUG_HEADER) continue;
    if (pAnswer->payload[0] == BM_DEBUG_STOPPED)
    {
      b->stopSeen = true;
      b->stopReason = pAnswer->payload[1];
      continue;
    }
    if (pAnswer->payload[0] == cmd) return true;
  }
}

static bool fine(const BmLinkPacket *p)
{
  return p->payload[1] == BM_DEBUG_OK;
}

static bool readRegs(Bridge *b, uint32_t regs[17])
{
  BmLinkPacket a;
  if (!command(b, BM_DEBUG_READ_REGS, 0, NULL, 0, &a) || !fine(&a) || a.length < BM_LIVE_DEBUG_HEADER + 68) return false;
  for (int i = 0; i < 17; i++) regs[i] = bmLinkGet32(a.payload + BM_LIVE_DEBUG_HEADER + i * 4);
  return true;
}

static bool writeRegs(Bridge *b, const uint32_t regs[17])
{
  uint8_t out[68];
  BmLinkPacket a;
  for (int i = 0; i < 17; i++) bmLinkPut32(out + i * 4, regs[i]);
  return command(b, BM_DEBUG_WRITE_REGS, 0, out, sizeof(out), &a) && fine(&a);
}

static bool readMemory(Bridge *b, uint32_t address, uint8_t *pOut, uint32_t size)
{
  while (size != 0)
  {
    uint32_t n = size < 256 ? size : 256;
    uint8_t args[6];
    BmLinkPacket a;
    bmLinkPut32(args, address);
    bmLinkPut16(args + 4, (uint16_t)n);
    if (!command(b, BM_DEBUG_READ_MEM, 0, args, sizeof(args), &a) || !fine(&a) || a.length != BM_LIVE_DEBUG_HEADER + n) return false;
    memcpy(pOut, a.payload + BM_LIVE_DEBUG_HEADER, n);
    address += n;
    pOut += n;
    size -= n;
  }
  return true;
}

static bool writeMemory(Bridge *b, uint32_t address, const uint8_t *pIn, uint32_t size)
{
  while (size != 0)
  {
    uint32_t n = size < 256 ? size : 256;
    uint8_t args[4 + 256];
    BmLinkPacket a;
    bmLinkPut32(args, address);
    memcpy(args + 4, pIn, n);
    if (!command(b, BM_DEBUG_WRITE_MEM, 0, args, (uint16_t)(4 + n), &a) || !fine(&a)) return false;
    address += n;
    pIn += n;
    size -= n;
  }
  return true;
}

static int setBreak(Bridge *b, uint32_t address)
{
  for (int i = 0; i < SLOTS; i++)
  {
    if (b->breaks[i].used && b->breaks[i].address == address) return i;
  }
  for (int i = 0; i < SLOTS; i++)
  {
    if (b->breaks[i].used) continue;
    uint8_t args[4];
    BmLinkPacket a;
    bmLinkPut32(args, address);
    if (!command(b, BM_DEBUG_SET_BREAK, (uint8_t)i, args, sizeof(args), &a) || !fine(&a)) return -1;
    b->breaks[i].used = true;
    b->breaks[i].address = address;
    return i;
  }
  return -1;
}

static void clearBreak(Bridge *b, int slot)
{
  if (slot < 0 || slot >= SLOTS || !b->breaks[slot].used) return;
  command(b, BM_DEBUG_CLEAR_BREAK, (uint8_t)slot, NULL, 0, NULL);
  b->breaks[slot].used = false;
}

static bool readWord(void *pContext, uint32_t address, uint32_t *pWord)
{
  uint8_t raw[4];
  if (!readMemory(pContext, address, raw, 4)) return false;
  *pWord = bmLinkGet32(raw);
  return true;
}

// --- GDB's remote protocol -------------------------------------------------------------------

static int hexDigit(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void hexBytes(char *pOut, const uint8_t *pIn, uint32_t n)
{
  static const char digits[] = "0123456789abcdef";
  for (uint32_t i = 0; i < n; i++)
  {
    pOut[i * 2] = digits[pIn[i] >> 4];
    pOut[i * 2 + 1] = digits[pIn[i] & 15];
  }
  pOut[n * 2] = 0;
}

static bool unhexBytes(uint8_t *pOut, const char *pIn, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++)
  {
    int h = hexDigit(pIn[i * 2]), l = h < 0 ? -1 : hexDigit(pIn[i * 2 + 1]);
    if (l < 0) return false;
    pOut[i] = (uint8_t)(h * 16 + l);
  }
  return true;
}

static void reply(Bridge *b, const char *pText)
{
  char out[PACKET_MAX + 8];
  size_t n = strlen(pText);
  if (n > PACKET_MAX) n = PACKET_MAX;
  uint8_t sum = 0;
  for (size_t i = 0; i < n; i++) sum = (uint8_t)(sum + (uint8_t)pText[i]);
  out[0] = '$';
  memcpy(out + 1, pText, n);
  snprintf(out + 1 + n, 4, "#%02x", sum);
  if (b->verbose) fprintf(stderr, "gdb <- %s\n", pText);
  size_t total = n + 4, sent = 0;
  while (sent < total)
  {
    ssize_t w = send(b->client, out + sent, total - sent, 0);
    if (w <= 0) return;
    sent += (size_t)w;
  }
}

static void replyStop(Bridge *b)
{
  // SIGTRAP for a breakpoint or a step, SIGINT when it was asked to stop.
  reply(b, b->stopReason == 1 ? "S05" : "S02");
}

/// Lets the program run. With `step`, only as far as the next instruction.
static void resume(Bridge *b, bool step)
{
  b->stepSlot = -1;
  if (step)
  {
    uint32_t regs[17], next;
    if (!readRegs(b, regs) || (regs[16] & 0x20) != 0 || !bmGdbNextArm(regs, readWord, b, &next))
    {
      reply(b, "E01");   // Thumb code cannot be stepped (hal.h)
      return;
    }
    if (next & 1)
    {
      // Into Thumb code, where no breakpoint can be put. A call (lr is the next instruction):
      // stop after it. A return: just run on.
      next = regs[14] == regs[15] + 4 ? regs[15] + 4 : 0;
    }
    if (next != 0)
    {
      bool had = false;
      for (int i = 0; i < SLOTS; i++) had = had || (b->breaks[i].used && b->breaks[i].address == next);
      int slot = setBreak(b, next);
      if (slot < 0)
      {
        reply(b, "E01");
        return;
      }
      if (!had) b->stepSlot = slot;
    }
  }
  BmLinkPacket a;
  if (!command(b, BM_DEBUG_CONTINUE, 0, NULL, 0, &a)) return;
  b->stopped = false;
}

static void onStopped(Bridge *b)
{
  b->stopped = true;
  b->stopSeen = false;
  if (b->stepSlot >= 0)
  {
    clearBreak(b, b->stepSlot);
    b->stepSlot = -1;
    b->stopReason = 1;
  }
  replyStop(b);
}

static void handlePacket(Bridge *b, char *p)
{
  char out[PACKET_MAX + 1];
  uint32_t regs[17];
  if (b->verbose) fprintf(stderr, "gdb -> %s\n", p);
  switch (p[0])
  {
  case '?':
    replyStop(b);
    return;
  case 'g':
  {
    // r0-r15, eight unused floating point registers of 12 bytes and their status, cpsr.
    uint8_t raw[168];
    if (!readRegs(b, regs)) { reply(b, "E01"); return; }
    memset(raw, 0, sizeof(raw));
    for (int i = 0; i < 16; i++) bmLinkPut32(raw + i * 4, regs[i]);
    bmLinkPut32(raw + 164, regs[16]);
    hexBytes(out, raw, sizeof(raw));
    reply(b, out);
    return;
  }
  case 'G':
  {
    uint8_t raw[168];
    if (strlen(p + 1) < sizeof(raw) * 2 || !unhexBytes(raw, p + 1, sizeof(raw))) { reply(b, "E01"); return; }
    for (int i = 0; i < 16; i++) regs[i] = bmLinkGet32(raw + i * 4);
    regs[16] = bmLinkGet32(raw + 164);
    reply(b, writeRegs(b, regs) ? "OK" : "E01");
    return;
  }
  case 'p':
  {
    unsigned long n = strtoul(p + 1, NULL, 16);
    uint8_t raw[12] = { 0 };
    if (!readRegs(b, regs)) { reply(b, "E01"); return; }
    if (n < 16) bmLinkPut32(raw, regs[n]);
    else if (n == 25) bmLinkPut32(raw, regs[16]);
    hexBytes(out, raw, n >= 16 && n < 24 ? 12 : 4);
    reply(b, out);
    return;
  }
  case 'P':
  {
    char *eq = strchr(p, '=');
    unsigned long n = strtoul(p + 1, NULL, 16);
    uint8_t raw[4];
    if (eq == NULL || !unhexBytes(raw, eq + 1, 4) || !readRegs(b, regs)) { reply(b, "E01"); return; }
    if (n < 16) regs[n] = bmLinkGet32(raw);
    else if (n == 25) regs[16] = bmLinkGet32(raw);
    reply(b, writeRegs(b, regs) ? "OK" : "E01");
    return;
  }
  case 'm':
  {
    char *comma = strchr(p, ',');
    uint32_t address = (uint32_t)strtoul(p + 1, NULL, 16), length = comma ? (uint32_t)strtoul(comma + 1, NULL, 16) : 0;
    uint8_t raw[PACKET_MAX / 2];
    if (length > sizeof(raw) - 1) length = sizeof(raw) - 1;
    if (length == 0 || !readMemory(b, address, raw, length)) { reply(b, "E01"); return; }
    hexBytes(out, raw, length);
    reply(b, out);
    return;
  }
  case 'M':
  {
    char *comma = strchr(p, ','), *colon = strchr(p, ':');
    uint32_t address = (uint32_t)strtoul(p + 1, NULL, 16), length = comma ? (uint32_t)strtoul(comma + 1, NULL, 16) : 0;
    uint8_t raw[PACKET_MAX / 2];
    if (colon == NULL || length > sizeof(raw) || strlen(colon + 1) < length * 2 || !unhexBytes(raw, colon + 1, length)) { reply(b, "E01"); return; }
    reply(b, length == 0 || writeMemory(b, address, raw, length) ? "OK" : "E01");
    return;
  }
  case 'Z': case 'z':
  {
    // Only breakpoints written into ARM code (type 0, kind 4).
    char *c1 = strchr(p, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    if (p[1] != '0') { reply(b, ""); return; }
    if (c1 == NULL || c2 == NULL) { reply(b, "E01"); return; }
    uint32_t address = (uint32_t)strtoul(c1 + 1, NULL, 16), kind = (uint32_t)strtoul(c2 + 1, NULL, 16);
    if (p[0] == 'Z')
    {
      reply(b, kind == 4 && setBreak(b, address) >= 0 ? "OK" : "E01");
      return;
    }
    for (int i = 0; i < SLOTS; i++)
    {
      if (b->breaks[i].used && b->breaks[i].address == address) clearBreak(b, i);
    }
    reply(b, "OK");
    return;
  }
  case 'c':
    resume(b, false);
    return;
  case 's':
    resume(b, true);
    return;
  case 'H': case 'T':
    reply(b, "OK");
    return;
  case 'q':
    if (strncmp(p, "qSupported", 10) == 0) reply(b, "PacketSize=400");
    else if (strcmp(p, "qAttached") == 0) reply(b, "1");
    else if (strcmp(p, "qC") == 0) reply(b, "QC1");
    else if (strcmp(p, "qfThreadInfo") == 0) reply(b, "m1");
    else if (strcmp(p, "qsThreadInfo") == 0) reply(b, "l");
    else if (strncmp(p, "qSymbol", 7) == 0) reply(b, "OK");
    else reply(b, "");
    return;
  default:
    reply(b, "");   // not supported
    return;
  }
}

/// Takes the breakpoints out and lets the program run.
static void release(Bridge *b)
{
  for (int i = 0; i < SLOTS; i++) clearBreak(b, i);
  if (b->stopped && !b->linkFailed) command(b, BM_DEBUG_CONTINUE, 0, NULL, 0, NULL);
  b->stopped = false;
}

bool bmGdbBridgeRun(BmLinkMaster *pMaster, uint16_t port, bool verbose)
{
  static Bridge bridge;
  Bridge *b = &bridge;
  memset(b, 0, sizeof(*b));
  b->pMaster = pMaster;
  b->verbose = verbose;
  b->stepSlot = -1;
  b->client = -1;

  BmLinkPacket a;
  if (!command(b, BM_DEBUG_INFO, 0, NULL, 0, &a))
  {
    fprintf(stderr, "gdb: the runtime does not answer\n");
    return false;
  }
  if (a.length < BM_LIVE_DEBUG_HEADER + 3 || a.payload[BM_LIVE_DEBUG_HEADER + 2] == 0)
  {
    fprintf(stderr, "gdb: this runtime cannot be stopped (the PC game is debugged with the PC's debugger)\n");
    return false;
  }
  b->stopped = a.payload[BM_LIVE_DEBUG_HEADER] != 0;

  int listener = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0 ||
      bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 1) != 0)
  {
    fprintf(stderr, "gdb: cannot listen on 127.0.0.1:%u\n", (unsigned)port);
    if (listener >= 0) close(listener);
    return false;
  }
  printf("gdb: waiting on 127.0.0.1:%u (in gdb: target remote :%u)\n", (unsigned)port, (unsigned)port);
  fflush(stdout);
  b->client = accept(listener, NULL, NULL);
  close(listener);
  if (b->client < 0) return false;
  setsockopt(b->client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  // GDB expects a stopped program.
  if (!b->stopped)
  {
    command(b, BM_DEBUG_HALT, 0, NULL, 0, NULL);
    while (!b->linkFailed && !b->stopSeen)
    {
      if (!bmLinkMasterReceive(pMaster, BM_LINK_GDB, &a, ANSWER_POLLS)) b->linkFailed = true;
      else if (a.length >= BM_LIVE_DEBUG_HEADER && a.payload[0] == BM_DEBUG_STOPPED) b->stopSeen = true;
    }
    b->stopped = !b->linkFailed;
    b->stopSeen = false;
    b->stopReason = 0;
  }

  bool open = true, detached = false;
  while (open && !b->linkFailed)
  {
    // While the program runs, watch for it stopping; the socket is only looked at.
    if (!b->stopped)
    {
      if (b->stopSeen) onStopped(b);
      else if (bmLinkMasterReceive(pMaster, BM_LINK_GDB, &a, 32) && a.length >= BM_LIVE_DEBUG_HEADER && a.payload[0] == BM_DEBUG_STOPPED)
      {
        b->stopReason = a.payload[1];
        onStopped(b);
      }
    }
    fd_set set;
    struct timeval none = { 0, 0 };
    FD_ZERO(&set);
    FD_SET(b->client, &set);
    if (select(b->client + 1, &set, NULL, NULL, b->stopped ? NULL : &none) <= 0) continue;
    char buffer[512];
    ssize_t n = recv(b->client, buffer, sizeof(buffer), 0);
    if (n <= 0) break;
    for (ssize_t i = 0; i < n && open; i++)
    {
      char c = buffer[i];
      if (!b->inPacket)
      {
        if (c == '$')
        {
          b->inPacket = true;
          b->inLength = 0;
        }
        else if (c == 3 && !b->stopped)
        {
          command(b, BM_DEBUG_HALT, 0, NULL, 0, NULL);   // Ctrl-C
        }
        continue;
      }
      if (c != '#')
      {
        if (b->inLength < sizeof(b->in) - 1) b->in[b->inLength++] = c;
        continue;
      }
      // The two checksum digits follow; the connection is reliable, so they are not checked.
      b->in[b->inLength] = 0;
      b->inPacket = false;
      i += 2;
      if (send(b->client, "+", 1, 0) != 1) open = false;
      if (b->in[0] == 'D' || b->in[0] == 'k')
      {
        release(b);
        if (b->in[0] == 'D') reply(b, "OK");
        detached = true;
        open = false;
      }
      else if (b->stopped || b->in[0] == '?') handlePacket(b, b->in);
    }
  }
  if (!detached && !b->linkFailed)
  {
    // The debugger went away: do not leave the program stopped.
    if (!b->stopped)
    {
      command(b, BM_DEBUG_HALT, 0, NULL, 0, NULL);
      while (!b->linkFailed && !b->stopSeen)
      {
        if (!bmLinkMasterReceive(pMaster, BM_LINK_GDB, &a, ANSWER_POLLS)) b->linkFailed = true;
        else if (a.length >= BM_LIVE_DEBUG_HEADER && a.payload[0] == BM_DEBUG_STOPPED) b->stopSeen = true;
      }
      b->stopped = !b->linkFailed;
    }
    release(b);
  }
  close(b->client);
  printf("gdb: session over\n");
  return !b->linkFailed;
}

#else

bool bmGdbBridgeRun(BmLinkMaster *pMaster, uint16_t port, bool verbose)
{
  (void)pMaster; (void)port; (void)verbose;
  fprintf(stderr, "gdb: not available in this build\n");
  return false;
}

#endif
