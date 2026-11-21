/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Measures how much faster scripts run as native code than in the interpreter (GBA only; the
// numbers are read by tests/ and quoted in docs/PLAN.md). The embedded pack holds one script
// class as bytecode and as a native module; its `spawn` event is run the same number of times
// both ways and must leave the same fields behind.

#include <string.h>

#include "hal.h"
#include "native.h"
#include "pack.h"
#include "video.h"
#include "vm.h"

extern const uint8_t bm_embedded_pack[];
extern const uint8_t bm_embedded_pack_end[];

#define ROUNDS 50

typedef struct BmBenchResult
{
  uint32_t done;               // 1 when the numbers are in
  uint32_t interpreterCycles;  // per call
  uint32_t nativeCycles;
  uint32_t same;               // both left the same fields
  uint32_t result;             // the field `Result`
  uint32_t errors;
  uint32_t nativeCalls;
  uint32_t percent;            // native time in percent of the interpreter's
} BmBenchResult;
BmBenchResult bm_bench;

static bool call(void *pContext, BmVmInstance *pInstance, uint16_t api, const int32_t *pArgs, int32_t *pResult)
{
  (void)pContext; (void)pInstance; (void)api; (void)pArgs;
  *pResult = 0;
  return false;
}
static int32_t getVar(void *pContext, BmVmInstance *pInstance, uint16_t var) { (void)pContext; (void)pInstance; (void)var; return 0; }
static void setVar(void *pContext, BmVmInstance *pInstance, uint16_t var, int32_t value) { (void)pContext; (void)pInstance; (void)var; (void)value; }
static void error(void *pContext, BmVmInstance *pInstance, const char *pWhat, uint16_t function, uint16_t line)
{
  (void)pContext; (void)pInstance; (void)pWhat; (void)function; (void)line;
  bm_bench.errors++;
}

static uint32_t measure(const BmVmModule *pModule, const BmVmHost *pHost, BmVmInstance *pInstance)
{
  uint32_t started = bmHalCycles();
  for (int i = 0; i < ROUNDS; i++)
  {
    bmVmInstanceInit(pModule, pInstance, 0);
    bmVmEvent(pModule, pHost, pInstance, BM_SCRIPT_EVENT_spawn);
  }
  return (bmHalCycles() - started) / ROUNDS;
}

int main(void)
{
  static BmPack pack;
  static BmVmModule module;
  BmVmHost host = { NULL, call, getVar, setVar, NULL, NULL, error };
  BmVmInstance interpreted, native;
  bmVideoInit();

  uint32_t size = 0, nativeSize = 0, slotSize = 0;
  BmChunkEntry entry;
  const char *why = NULL;
  if (bmPackOpen(&pack, bm_embedded_pack, (uint32_t)(bm_embedded_pack_end - bm_embedded_pack), false) == BM_PACK_OK)
  {
    int32_t chunk = bmPackFindChunk(&pack, BM_CHUNK_MODB, 0), nativeChunk = bmPackFindChunk(&pack, BM_CHUNK_MODN, 0);
    const uint8_t *p = chunk >= 0 ? bmPackChunkData(&pack, (uint32_t)chunk, &size) : NULL;
    if (p != NULL && nativeChunk >= 0 && bmPackGetChunk(&pack, (uint32_t)chunk, &entry) && bmVmModuleOpen(&module, p, size, &why))
    {
      bm_bench.interpreterCycles = measure(&module, &host, &interpreted);
      void *pSlot = bmVmNativeSlot(&slotSize);
      const BmVmNativeModule *pNative = bmVmNativeLoad(bmPackChunkData(&pack, (uint32_t)nativeChunk, &nativeSize), nativeSize, pSlot, slotSize, entry.hash, &why);
      if (pNative != NULL && bmVmModuleSetNative(&module, pNative))
      {
        bm_bench.nativeCycles = measure(&module, &host, &native);
        bm_bench.same = memcmp(&interpreted, &native, sizeof(native)) == 0;
        bm_bench.result = (uint32_t)native.fields[0];
        bm_bench.nativeCalls = bm_native_calls;
        bm_bench.percent = bm_bench.interpreterCycles ? bm_bench.nativeCycles * 100 / bm_bench.interpreterCycles : 100;
        bm_bench.done = 1;
      }
    }
  }
  for (;;) bmHalVideoWaitVBlank();
}
