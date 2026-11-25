/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "game.h"
#include "sound.h"

// The effects' echo in rooms with acoustics (sound-format.md, "A room's acoustics"): its own
// delay lines, as the music keeps the song's. The multiboot images have no such rooms and no
// room for it: light_off.c gives them none (the effects dry).
// (Its lines, then a frame's output of the effects: sound.c, mixEffects.)
static int8_t s_effectEcho[2 * BM_SOUND_ECHO_MAX + 2 * BM_SOUND_FRAME_SAMPLES] BM_EWRAM_BSS;

int8_t *bmGameEffectEchoMemory(void)
{
  return s_effectEcho;
}
