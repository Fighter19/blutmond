/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "xboo.h"

#include <stdio.h>
#include <string.h>

#ifdef __linux__
#  include <fcntl.h>
#  include <linux/parport.h>
#  include <linux/ppdev.h>
#  include <sys/ioctl.h>
#  include <unistd.h>

static void wcontrol(BmXboo *x, unsigned char v)
{
  ioctl(x->fd, PPWCONTROL, &v);
}

static unsigned char rstatus(BmXboo *x)
{
  unsigned char v = 0;
  ioctl(x->fd, PPRSTATUS, &v);
  return v;
}

bool bmXbooOpen(BmXboo *x, const char *pDevice)
{
  memset(x, 0, sizeof(*x));
  x->control = PARPORT_CONTROL_INIT;
  x->delay = 1;
  x->fd = open(pDevice, O_RDWR);
  if (x->fd < 0)
  {
    snprintf(x->error, sizeof(x->error), "cannot open %s", pDevice);
    return false;
  }
  if (ioctl(x->fd, PPCLAIM) != 0)
  {
    snprintf(x->error, sizeof(x->error), "cannot claim %s (is another program using it?)", pDevice);
    close(x->fd);
    x->fd = -1;
    return false;
  }
  // InitPort
  wcontrol(x, x->control);
  unsigned char data = 0xFF;
  ioctl(x->fd, PPWDATA, &data);
  return true;
}

void bmXbooClose(BmXboo *x)
{
  if (x->fd < 0) return;
  ioctl(x->fd, PPRELEASE);
  close(x->fd);
  x->fd = -1;
}

void bmXbooPortDelay(BmXboo *x, uint32_t delay)
{
  unsigned char v = 0;
  for (uint32_t i = 0; i < delay * 6; i++) ioctl(x->fd, PPRDATA, &v);
}

// XbooExchange32 from xcomms parport.cxx, bit for bit.
static uint32_t exchange32(BmXboo *x, uint32_t data)
{
  unsigned char al = 0;
  unsigned char ah = x->control;
  for (int i = 0; i < 32; i++)
  {
    bool leading = (data & 0x80000000u) != 0;
    data <<= 1;
    al = leading ? 0x02 : 0x00;
    ah &= leading ? 0x0C : 0x00;
    al ^= 0x03;          // inverted bit, SC low
    al ^= ah;            // saved SD state and reset
    wcontrol(x, al);
    ah = al;
    al = rstatus(x);
    data += (al & 0xC0) ? 1u : 0u;
    al = ah;
    al--;                // SC high
    wcontrol(x, al);
    al &= (unsigned char)~PARPORT_CONTROL_AUTOFD;
    wcontrol(x, al);
  }
  x->control = al;
  return data;
}

static void send32(BmXboo *x, uint32_t data)
{
  for (int i = 0; i < 32; i++)
  {
    bool leading = (data & 0x80000000u) != 0;
    data <<= 1;
    unsigned char b = leading ? (PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT)
                              : (PARPORT_CONTROL_STROBE | PARPORT_CONTROL_AUTOFD | PARPORT_CONTROL_INIT);
    wcontrol(x, b);
    b &= (unsigned char)~PARPORT_CONTROL_STROBE;
    wcontrol(x, b);
    b &= (unsigned char)~PARPORT_CONTROL_AUTOFD;
    wcontrol(x, b);
  }
}

void bmXbooReset(BmXboo *x)
{
  if (!x->resetWired) return;
  unsigned char ah = (unsigned char)(x->control & 0xFB);
  unsigned char al = ah;
  ioctl(x->fd, PPWDATA, &ah);
  for (int i = 0; i < 12 * 4; i++) ioctl(x->fd, PPRDATA, &al);
  ah |= 4;
  ioctl(x->fd, PPWDATA, &ah);
}
#else
bool bmXbooOpen(BmXboo *x, const char *pDevice)
{
  memset(x, 0, sizeof(*x));
  snprintf(x->error, sizeof(x->error), "%s: the Xboo transport needs Linux ppdev", pDevice);
  return false;
}
void bmXbooClose(BmXboo *x) { (void)x; }
void bmXbooPortDelay(BmXboo *x, uint32_t delay) { (void)x; (void)delay; }
static uint32_t exchange32(BmXboo *x, uint32_t data) { (void)x; (void)data; return 0xFFFFFFFFu; }
static void send32(BmXboo *x, uint32_t data) { (void)x; (void)data; }
void bmXbooReset(BmXboo *x) { (void)x; }
#endif

uint32_t bmXbooExchange(BmXboo *x, uint32_t out)
{
  uint32_t in = exchange32(x, out);
  bmXbooPortDelay(x, x->delay);
  return in;
}

static void delaySend(BmXboo *x, uint32_t data)
{
  send32(x, data);
  bmXbooPortDelay(x, x->delay);
}

static uint32_t handshake(BmXboo *x, uint32_t send, uint8_t receive)
{
  uint32_t reply;
  int i = 0;
  do
  {
    bmXbooPortDelay(x, 100);
    reply = exchange32(x, send);
    if (i++ > 1000) break;
  } while ((reply >> 24) != receive);
  return reply;
}

static uint16_t calcCrc(BmXboo *x, uint32_t data)
{
  uint32_t eax = (data & 0xFFFF0000u) | ((data & 0xFFFFu) ^ x->crc1);
  for (int i = 0; i < 32; i++)
  {
    bool bit = eax & 1;
    eax >>= 1;
    if (bit) eax = (eax & 0xFFFF0000u) | ((eax & 0xFFFFu) ^ x->crc2);
  }
  x->crc1 = (uint16_t)eax;
  return x->crc1;
}

bool bmXbooBoot(BmXboo *x, const uint8_t *pImage, uint32_t size)
{
  static uint8_t image[256 * 1024 + 16];
  if (size > 256 * 1024)
  {
    snprintf(x->error, sizeof(x->error), "multiboot images are at most 256 KB");
    return false;
  }
  // Length a multiple of 16 and at least 0x1C0.
  uint32_t length = (size + 15) & ~15u;
  if (length < 0x1C0) length = 0x1C0;
  memset(image, 0, length);
  memcpy(image, pImage, size);

  bmXbooReset(x);
  uint32_t reply = handshake(x, 0x6202, 0x72);
  if ((reply >> 24) != 0x72)
  {
    snprintf(x->error, sizeof(x->error), "no response from the GBA");
    return false;
  }
  bmXbooExchange(x, 0x6102);  // header
  uint32_t i;
  for (i = 0x60; i > 0; i--)
  {
    uint32_t data = image[(0x60 - i) * 2] | ((uint32_t)image[(0x60 - i) * 2 + 1] << 8);
    reply = bmXbooExchange(x, data);
    if ((reply >> 24) != i) break;
  }
  if (i != 0)
  {
    snprintf(x->error, sizeof(x->error), "header not accepted");
    return false;
  }
  bmXbooExchange(x, 0x6200);
  bmXbooExchange(x, 0x6202);

  const uint16_t keyFromMaster = 0xD1;
  uint32_t keyConfirm = 0x6300 | keyFromMaster;
  reply = handshake(x, keyConfirm, 0x73);
  uint32_t key1 = (reply >> 16) & 0xFF;
  keyConfirm = ((key1 + 0x0F) & 0xFF) | 0x6400;
  reply = handshake(x, keyConfirm, 0x73);
  uint32_t seed = 0xFFFF0000u | (key1 << 8) | keyFromMaster;
  bmXbooPortDelay(x, 0x110E);
  reply = bmXbooExchange(x, (length >> 2) - 0x64);
  uint32_t key2 = (reply >> 16) & 0xFF;

  x->crc1 = 0xC387;
  x->crc2 = 0xC37B;
  uint32_t offset = 0xC0;
  uint32_t words = (length - 0xC0) >> 2;
  for (i = 0; i < words; i++)
  {
    const uint8_t *p = image + 0xC0 + i * 4;
    uint32_t data = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    calcCrc(x, data);
    seed = 0x6F646573u * seed + 1;
    data ^= seed;
    data ^= (0xFE000000u - offset) ^ 0x43202F2Fu;
    reply = bmXbooExchange(x, data);
    if ((reply >> 16) != (offset & 0xFFFF)) break;
    offset += 4;
  }
  if (i != words)
  {
    snprintf(x->error, sizeof(x->error), "transfer failed at offset 0x%X", (unsigned)offset);
    return false;
  }
  bmXbooExchange(x, 0x65);
  i = 0;
  do
  {
    reply = bmXbooExchange(x, 0x65);
    bmXbooPortDelay(x, 200);
    if (i++ > 1000) break;
  } while ((reply >> 16) == 0x74);
  if ((reply >> 16) != 0x75)
  {
    snprintf(x->error, sizeof(x->error), "no final confirmation (bad header?)");
    return false;
  }
  bmXbooExchange(x, 0x66);
  uint32_t data = 0xFFFF0000u | ((key2 & 0xFF) << 8) | (keyConfirm & 0xFF);
  uint16_t crc = calcCrc(x, data);
  reply = bmXbooExchange(x, crc);
  if ((reply >> 16) != crc)
  {
    snprintf(x->error, sizeof(x->error), "final CRC check failed");
    return false;
  }
  (void)delaySend;
  return true;
}

static uint32_t transportExchange(void *pContext, uint32_t out)
{
  return bmXbooExchange(pContext, out);
}

static void transportDelay(void *pContext, uint32_t delay)
{
  ((BmXboo *)pContext)->delay = delay;
}

static bool transportReset(void *pContext)
{
  BmXboo *x = pContext;
  if (!x->resetWired) return false;
  bmXbooReset(x);
  return true;
}

BmLinkTransport bmXbooTransport(BmXboo *x)
{
  return (BmLinkTransport){ x, transportExchange, transportDelay, transportReset, x->resetWired ? BM_LINK_CAP_RESET : 0u };
}
