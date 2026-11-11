/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "host_link.h"

#include "hal_host.h"

#include <stdio.h>

#if defined(__unix__) || defined(__APPLE__)
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <pthread.h>
#  include <sys/socket.h>
#  include <unistd.h>

static int s_listen = -1;
static pthread_t s_thread;
static volatile bool s_stop;
static volatile int s_client = -1;

static bool readAll(int fd, uint8_t *p, size_t n)
{
  while (n > 0)
  {
    ssize_t r = recv(fd, p, n, 0);
    if (r <= 0) return false;
    p += r;
    n -= (size_t)r;
  }
  return true;
}

static void serve(int fd)
{
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  uint8_t b[4];
  while (!s_stop && readAll(fd, b, 4))
  {
    uint32_t in = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    uint32_t out = bmHalHostLinkExchangeWait(in);
    uint8_t o[4] = { (uint8_t)out, (uint8_t)(out >> 8), (uint8_t)(out >> 16), (uint8_t)(out >> 24) };
    if (send(fd, o, 4, 0) != 4) break;
  }
  close(fd);
}

static void *thread(void *p)
{
  (void)p;
  while (!s_stop)
  {
    int fd = accept(s_listen, NULL, NULL);
    if (fd < 0) break;
    s_client = fd;
    serve(fd);
    s_client = -1;
  }
  return NULL;
}

bool bmHostLinkListen(uint16_t port)
{
  s_listen = socket(AF_INET, SOCK_STREAM, 0);
  if (s_listen < 0) return false;
  int one = 1;
  setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = { 0 };
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s_listen, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s_listen, 1) != 0)
  {
    fprintf(stderr, "live link: cannot listen on 127.0.0.1:%u\n", (unsigned)port);
    close(s_listen);
    s_listen = -1;
    return false;
  }
  s_stop = false;
  if (pthread_create(&s_thread, NULL, thread, NULL) != 0)
  {
    close(s_listen);
    s_listen = -1;
    return false;
  }
  printf("live link on 127.0.0.1:%u (bmlink --tcp 127.0.0.1:%u ...)\n", (unsigned)port, (unsigned)port);
  return true;
}

void bmHostLinkStop(void)
{
  if (s_listen < 0) return;
  s_stop = true;
  shutdown(s_listen, SHUT_RDWR);
  int client = s_client;
  if (client >= 0) shutdown(client, SHUT_RDWR);
  close(s_listen);
  pthread_join(s_thread, NULL);
  s_listen = -1;
}
#else
bool bmHostLinkListen(uint16_t port)
{
  fprintf(stderr, "live link: TCP is not supported on this platform yet (port %u)\n", (unsigned)port);
  return false;
}
void bmHostLinkStop(void) {}
#endif
