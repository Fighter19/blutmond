/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "tcp_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <unistd.h>

bool bmTcpLinkOpen(BmTcpLink *l, const char *address)
{
  memset(l, 0, sizeof(*l));
  l->fd = -1;
  char host[256] = "127.0.0.1";
  const char *port = address;
  const char *colon = strrchr(address, ':');
  if (colon != NULL)
  {
    snprintf(host, sizeof(host), "%.*s", (int)(colon - address), address);
    port = colon + 1;
  }
  struct addrinfo hints = { 0 }, *res = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL)
  {
    snprintf(l->error, sizeof(l->error), "cannot resolve %s", address);
    return false;
  }
  for (struct addrinfo *a = res; a != NULL && l->fd < 0; a = a->ai_next)
  {
    int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) l->fd = fd;
    else close(fd);
  }
  freeaddrinfo(res);
  if (l->fd < 0)
  {
    snprintf(l->error, sizeof(l->error), "cannot connect to %s (is the game running?)", address);
    return false;
  }
  int one = 1;
  setsockopt(l->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return true;
}

void bmTcpLinkClose(BmTcpLink *l)
{
  if (l->fd >= 0) close(l->fd);
  l->fd = -1;
}

static uint32_t exchange(void *pContext, uint32_t out)
{
  BmTcpLink *l = pContext;
  if (l->fd < 0) return 0xFFFFFFFFu;
  uint8_t b[4] = { (uint8_t)out, (uint8_t)(out >> 8), (uint8_t)(out >> 16), (uint8_t)(out >> 24) };
  if (send(l->fd, b, 4, 0) != 4) { bmTcpLinkClose(l); return 0xFFFFFFFFu; }
  size_t got = 0;
  while (got < 4)
  {
    ssize_t r = recv(l->fd, b + got, 4 - got, 0);
    if (r <= 0) { bmTcpLinkClose(l); return 0xFFFFFFFFu; }
    got += (size_t)r;
  }
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
#else
bool bmTcpLinkOpen(BmTcpLink *l, const char *address)
{
  memset(l, 0, sizeof(*l));
  l->fd = -1;
  snprintf(l->error, sizeof(l->error), "%s: TCP is not supported on this platform yet", address);
  return false;
}
void bmTcpLinkClose(BmTcpLink *l) { (void)l; }
static uint32_t exchange(void *pContext, uint32_t out) { (void)pContext; (void)out; return 0xFFFFFFFFu; }
#endif

static void setDelay(void *pContext, uint32_t delay)
{
  (void)pContext;
  (void)delay;
}

BmLinkTransport bmTcpLinkTransport(BmTcpLink *l)
{
  return (BmLinkTransport){ l, exchange, setDelay, NULL, 0u };
}
