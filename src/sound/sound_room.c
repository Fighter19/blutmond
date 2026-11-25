/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
// A room's acoustics (sound-format.md, "A room's acoustics"), for the music (echo_music) and for
// the sound effects with lines of their own ("The effects apart"): apart from sound.c so that
// the multiboot images, whose rooms have no acoustics, link stubs instead (light_off.c).
#include <string.h>

#include "shared.h"
#include "sound.h"

static inline int32_t clip(int32_t v)
{
  return v < -128 ? -128 : v > 127 ? 127 : v;
}

// The room's acoustics: the echo with each repeat low-passed (damping), then the whole output
// (muffle): one-pole filters, y += (x - y) * (256 - k) / 256, kept in 8.8. (At k = 0 they pass
// the samples unchanged; the song's echo has its own loop without them, as it plays most of
// the time.) In IWRAM as Thumb code: rooms with acoustics are few, IWRAM is full.
BM_IWRAM_THUMB uint32_t bmSoundFinishRoom(BmSound *s, const int32_t *pSum, int8_t *pLeft, int8_t *pRight, uint32_t count, uint32_t check)
{
  uint32_t a = check & 0xFFFF, b = check >> 16;
  int8_t *echoLeft = s->pEchoLeft, *echoRight = s->pEchoRight;
  uint32_t position = s->echoPosition, delay = s->echoDelay;
  int32_t feedback = s->echoFeedback;
  int32_t keepEcho = 256 - s->echoDamping, keepMix = 256 - s->muffle;
  int32_t echoL = s->echoLow[0], echoR = s->echoLow[1], mixL = s->mixLow[0], mixR = s->mixLow[1];
  for (uint32_t i = 0; i < count; i++)
  {
    // The echo low-passed, then rounded towards zero (finishEcho).
    echoL += (((int32_t)echoLeft[position] << 8) - echoL) * keepEcho >> 8;
    echoR += (((int32_t)echoRight[position] << 8) - echoR) * keepEcho >> 8;
    int32_t el = (echoL >> 8) * feedback, er = (echoR >> 8) * feedback;
    el = (el + ((el >> 31) & 255)) >> 8;
    er = (er + ((er >> 31) & 255)) >> 8;
    int32_t l = clip((pSum[0] >> 8) + el);
    int32_t r = clip((pSum[1] >> 8) + er);
    pSum += 2;
    echoLeft[position] = (int8_t)l;
    echoRight[position] = (int8_t)r;
    if (++position >= delay) position = 0;
    mixL += ((l << 8) - mixL) * keepMix >> 8;
    mixR += ((r << 8) - mixR) * keepMix >> 8;
    l = mixL >> 8;
    r = mixR >> 8;
    pLeft[i] = (int8_t)l;
    pRight[i] = (int8_t)r;
    a += (uint8_t)l;
    b += a;
    a += (uint8_t)r;
    b += a;
  }
  s->echoPosition = (uint16_t)position;
  s->echoLow[0] = (int16_t)echoL;
  s->echoLow[1] = (int16_t)echoR;
  s->mixLow[0] = (int16_t)mixL;
  s->mixLow[1] = (int16_t)mixR;
  return (a & 0xFFFF) | (b << 16);
}


const int8_t *bmSoundEffectsRoom(BmSound *s, const int32_t *pSum, uint32_t count)
{
  // bmSoundFinishRoom reads the music's fields: the effects' meanwhile, then back. Their frame
  // of output after the lines in the same memory.
  BmSound music = *s;
  int8_t *out = s->pFxEcho + 2 * BM_SOUND_ECHO_MAX;
  s->pEchoLeft = s->pFxEcho;
  s->pEchoRight = s->pFxEcho + BM_SOUND_ECHO_MAX;
  s->echoDelay = s->room.echoDelay != 0 ? s->room.echoDelay : 1;
  s->echoFeedback = s->room.echoDelay != 0 ? s->room.feedback : 0;
  s->echoDamping = s->room.damping;
  s->muffle = s->room.muffle;
  s->echoPosition = s->fxEchoPosition < s->echoDelay ? s->fxEchoPosition : 0;
  memcpy(s->echoLow, s->fxEchoLow, sizeof(s->echoLow));
  memcpy(s->mixLow, s->fxMixLow, sizeof(s->mixLow));
  bmSoundFinishRoom(s, pSum, out, out + BM_SOUND_FRAME_SAMPLES, count, 0);
  s->fxEchoPosition = s->echoPosition;
  memcpy(s->fxEchoLow, s->echoLow, sizeof(s->echoLow));
  memcpy(s->fxMixLow, s->mixLow, sizeof(s->mixLow));
  s->pEchoLeft = music.pEchoLeft;
  s->pEchoRight = music.pEchoRight;
  s->echoPosition = music.echoPosition;
  s->echoDelay = music.echoDelay;
  s->echoFeedback = music.echoFeedback;
  s->echoDamping = music.echoDamping;
  s->muffle = music.muffle;
  memcpy(s->echoLow, music.echoLow, sizeof(s->echoLow));
  memcpy(s->mixLow, music.mixLow, sizeof(s->mixLow));
  return out;
}
