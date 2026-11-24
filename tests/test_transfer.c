/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Device to device (src/link/transfer.h), the sending side against a model of the bootstrap
// (src/hal/gba/bootstrap/bootstrap.c; the real one is tested between two GBAs in mGBA): the
// image arrives whole, a lost word makes the sender start again, and without anybody on the
// other end the result says so.

#include <string.h>

#include "bootstrap_protocol.h"
#include "hal_host.h"
#include "pack.h"
#include "test.h"
#include "transfer.h"

typedef struct Model
{
  int phase;               // 0 waiting for start, 1 size, 2 words, 3 checksum, 4 done
  uint32_t reply;
  uint32_t size, taken;
  uint8_t image[4096];
  uint32_t exchanges, dropAt, starts;
  bool started;            // the image passed its checksum and was started
  uint32_t checksumAt;     // the exchange that brought the checksum
} Model;

static uint32_t model(void *pContext, uint32_t in)
{
  Model *m = pContext;
  m->exchanges++;
  if (m->started) return 0xFFFFFFFFu;                       // the image runs now: nobody listens
  if (m->dropAt != 0 && m->exchanges == m->dropAt) return 0xFFFFFFFFu;   // this word is lost
  uint32_t out = m->reply;
  switch (m->phase)
  {
  case 0:
    if (in == BM_BOOTSTRAP_START) { m->phase = 1; m->reply = BM_BOOTSTRAP_READY; m->starts++; }
    else m->reply = BM_BOOTSTRAP_IDLE;
    break;
  case 1:
    if ((in & 3) != 0 || in == 0 || in > sizeof(m->image)) { m->phase = 0; m->reply = BM_BOOTSTRAP_BAD; }
    else { m->size = in; m->taken = 0; m->phase = 2; m->reply = in; }
    break;
  case 2:
    memcpy(m->image + m->taken * 4, &in, 4);
    m->taken++;
    m->reply = m->taken;
    if (m->taken * 4 == m->size) m->phase = 3;
    break;
  case 3:
    m->checksumAt = m->exchanges;
    if (in == bmCrc32(0, m->image, m->size)) { m->phase = 4; m->reply = BM_BOOTSTRAP_OK; }
    else { m->phase = 0; m->reply = BM_BOOTSTRAP_BAD; }
    break;
  default:
    m->started = true;
    break;
  }
  return out;
}

static uint8_t s_runtime[1003];
static uint8_t s_pack[64], s_state[16];

static void testTheRuntimeGoesThroughTheBootstrap(void)
{
  for (uint32_t i = 0; i < sizeof(s_runtime); i++) s_runtime[i] = (uint8_t)(i * 13 + (i >> 4));
  Model m;
  memset(&m, 0, sizeof(m));
  m.reply = BM_BOOTSTRAP_IDLE;
  bmHalHostLinkSetPeer(model, &m);
  BmTransferImages images = { NULL, 0, s_runtime, sizeof(s_runtime) };
  BmTransferStats stats;
  // The model never becomes a runtime, so the transfer ends there.
  TEST_ASSERT(bmTransferSend(&images, s_pack, sizeof(s_pack), -1, s_state, sizeof(s_state), &stats) == BM_TRANSFER_NO_RUNTIME);
  TEST_ASSERT(m.started && stats.usedBootstrap && !stats.usedMultiboot && stats.runtimeTries == 1);
  TEST_ASSERT(m.size == 1004 && memcmp(m.image, s_runtime, sizeof(s_runtime)) == 0 && m.image[1003] == 0);
}

static void testALostWordStartsAgain(void)
{
  Model m;
  BmTransferImages images = { NULL, 0, s_runtime, sizeof(s_runtime) };
  BmTransferStats stats;
  // A clean run tells at which exchanges the image's words and the checksum go.
  memset(&m, 0, sizeof(m));
  m.reply = BM_BOOTSTRAP_IDLE;
  bmHalHostLinkSetPeer(model, &m);
  bmTransferSend(&images, s_pack, sizeof(s_pack), -1, s_state, sizeof(s_state), NULL);
  TEST_ASSERT(m.started && m.checksumAt > 251);
  const uint32_t drops[] = { m.checksumAt - 100, m.checksumAt - 251, m.checksumAt - 1, m.checksumAt, m.checksumAt + 1 };
  for (unsigned k = 0; k < sizeof(drops) / sizeof(drops[0]); k++)
  {
    memset(&m, 0, sizeof(m));
    m.reply = BM_BOOTSTRAP_IDLE;
    m.dropAt = drops[k];
    BmTransferResult result = bmTransferSend(&images, s_pack, sizeof(s_pack), -1, s_state, sizeof(s_state), &stats);
    if (result != BM_TRANSFER_NO_RUNTIME) printf("  word %u lost: result %d after %u tries, %u starts\n", (unsigned)drops[k], (int)result, (unsigned)stats.runtimeTries, (unsigned)m.starts);
    TEST_ASSERT(result == BM_TRANSFER_NO_RUNTIME);
    // Whichever word was lost, the image that was started is the whole one. (If only the
    // last answer was lost, the first attempt had worked and the sender finds nobody.)
    TEST_ASSERT(m.started && memcmp(m.image, s_runtime, sizeof(s_runtime)) == 0);
    TEST_ASSERT(stats.runtimeTries == 2);
  }
}

static uint32_t nobody(void *pContext, uint32_t in)
{
  (void)pContext; (void)in;
  return 0xFFFFFFFFu;
}

static void testNobodyThere(void)
{
  BmTransferStats stats;
  bmHalHostLinkSetPeer(nobody, NULL);
  TEST_ASSERT(bmTransferSend(NULL, s_pack, sizeof(s_pack), -1, s_state, sizeof(s_state), &stats) == BM_TRANSFER_NO_PEER);
  BmTransferImages images = { NULL, 0, s_runtime, sizeof(s_runtime) };
  TEST_ASSERT(bmTransferSend(&images, s_pack, sizeof(s_pack), -1, s_state, sizeof(s_state), &stats) == BM_TRANSFER_NO_PEER);
  // With a bootstrap image the BIOS is tried, and does not answer either.
  images.pBootstrap = s_runtime;
  images.bootstrapSize = 512;
  TEST_ASSERT(bmTransferSend(&images, s_pack, sizeof(s_pack), -1, s_state, sizeof(s_state), &stats) == BM_TRANSFER_BOOT_FAILED);
  TEST_ASSERT(stats.usedMultiboot && !stats.usedBootstrap);
  bmHalHostLinkSetPeer(NULL, NULL);
}

int main(void)
{
  RUN_TEST(testTheRuntimeGoesThroughTheBootstrap);
  RUN_TEST(testALostWordStartsAgain);
  RUN_TEST(testNobodyThere);
  return TEST_RESULT();
}
