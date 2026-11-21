/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "xboo.h"

#include "multiboot.h"

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
  // InitPort. The data lines as xcomms leaves them after its reset: only bit 2 (the reset
  // wire, active low) high.
  wcontrol(x, x->control);
  unsigned char data = 0x04;
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
void bmXbooReset(BmXboo *x) { (void)x; }
#endif

uint32_t bmXbooExchange(BmXboo *x, uint32_t out)
{
  uint32_t in = exchange32(x, out);
  bmXbooPortDelay(x, x->delay);
  return in;
}

static uint32_t bootExchange(void *pContext, uint32_t out)
{
  return exchange32(pContext, out);
}

static void bootDelay(void *pContext, uint32_t units)
{
  bmXbooPortDelay(pContext, units);
}

bool bmXbooBoot(BmXboo *x, const uint8_t *pImage, uint32_t size)
{
  BmMultibootPort port = { x, bootExchange, bootDelay, x->delay };
  const char *why = NULL;
  if (bmMultibootSend(&port, pImage, size, &why)) return true;
  snprintf(x->error, sizeof(x->error), "%s", why);
  return false;
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
