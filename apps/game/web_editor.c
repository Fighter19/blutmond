/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "web_editor.h"

#include <emscripten.h>

EM_JS(int, bmWebFromEditorJs, (void), {
  return new URLSearchParams(location.search).has('editor') ? 1 : 0;
})

// Packs arrive as messages { type: 'blutmond-pack', data: Uint8Array, mode } and wait in a
// queue until the game takes them at a frame boundary.
EM_JS(void, bmWebListen, (void), {
  if (Module.bmPacks) return;
  Module.bmPacks = [];
  window.addEventListener('message', (e) => {
    if (e.source !== window.parent || !e.data || e.data.type !== 'blutmond-pack') return;
    Module.bmPacks.push({ data: new Uint8Array(e.data.data), mode: e.data.mode | 0 });
  });
  window.parent.postMessage({ type: 'blutmond-ready' }, '*');
})

EM_JS(uint8_t *, bmWebTakePackJs, (uint32_t *pSize, int *pMode), {
  if (!Module.bmPacks || Module.bmPacks.length === 0) return 0;
  const pack = Module.bmPacks.shift();
  const p = _malloc(pack.data.length);
  if (!p) return 0;
  HEAPU8.set(pack.data, p);
  HEAPU32[pSize >> 2] = pack.data.length;
  HEAP32[pMode >> 2] = pack.mode;
  return p;
})

bool bmWebFromEditor(void)
{
  return bmWebFromEditorJs() != 0;
}

uint8_t *bmWebTakePack(uint32_t *pSize, int *pMode)
{
  return bmWebTakePackJs(pSize, pMode);
}
