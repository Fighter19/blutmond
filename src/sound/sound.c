/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "sound.h"

#include <string.h>

#include "shared.h"

#define NONE 0xFFFFFFFFu
#define FRAC 12
#define WAVE_SAMPLES 64
#define NOISE_SAMPLES 2048

// --- Waves ----------------------------------------------------------------------------------

// The waves in fast memory on the GBA (plain statics are in IWRAM): the mixer reads them per
// sample. The noise is played like a sample (those come from the pack, in ROM or EWRAM), so it
// can be in EWRAM and leave IWRAM to native script code.
static int8_t s_waves[BM_WAVE_NOISE][WAVE_SAMPLES];
static int8_t s_noise[NOISE_SAMPLES] BM_EWRAM_BSS;
static bool s_wavesReady;

// Mixing scratch: one frame of sums, left and right side by side. One engine mixes at a time.
static int32_t s_sum[2 * BM_SOUND_FRAME_SAMPLES];

static void buildWaves(void)
{
  static const int8_t quarterSine[17] = { 0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127 };
  for (int i = 0; i < WAVE_SAMPLES; i++)
  {
    s_waves[BM_WAVE_PULSE_12][i] = i < 8 ? 100 : -100;
    s_waves[BM_WAVE_PULSE_25][i] = i < 16 ? 100 : -100;
    s_waves[BM_WAVE_PULSE_50][i] = i < 32 ? 100 : -100;
    s_waves[BM_WAVE_SAW][i] = (int8_t)(-124 + i * 4);
    s_waves[BM_WAVE_TRIANGLE][i] = (int8_t)(i < 32 ? -124 + i * 8 : 124 - (i - 32) * 8);
    int q = i & 15, quadrant = i >> 4;
    int v = (quadrant & 1) ? quarterSine[16 - q] : quarterSine[q];
    s_waves[BM_WAVE_SINE][i] = (int8_t)((quadrant & 2) ? -v : v);
  }
  uint32_t lfsr = 0x1ACE5;
  for (int i = 0; i < NOISE_SAMPLES; i++)
  {
    lfsr = (lfsr >> 1) ^ ((0u - (lfsr & 1u)) & 0xB400u);
    s_noise[i] = (int8_t)((lfsr & 0xFF) - 128);
  }
  s_wavesReady = true;
}

// 2^(n/12) in 16.16.
static const uint32_t s_semitone[12] = { 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715 };

// Step (20.12) for a key, from the step at key 60.
static uint32_t stepForKey(uint32_t stepAt60, int key)
{
  int n = key - 60, octave = 0;
  while (n < 0) { n += 12; octave--; }
  while (n >= 12) { n -= 12; octave++; }
  uint64_t step = ((uint64_t)stepAt60 * s_semitone[n]) >> 16;
  if (octave >= 0) step <<= octave;
  else step >>= -octave;
  if (step > 0x7FFFFFFFu) step = 0x7FFFFFFFu;
  return step == 0 ? 1 : (uint32_t)step;
}

// --- Songs ----------------------------------------------------------------------------------

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

bool bmSoundSongValid(const uint8_t *p, uint32_t size)
{
  if (p == NULL || size < BM_SOUND_SONG_HEADER) return false;
  uint32_t instruments = rd16(p + 4), events = rd32(p + 8);
  uint32_t instrumentsAt = rd32(p + 28), eventsAt = rd32(p + 32);
  if (instrumentsAt > size || instruments > (size - instrumentsAt) / BM_SOUND_INSTRUMENT_SIZE) return false;
  if (eventsAt > size || events > (size - eventsAt) / BM_SOUND_EVENT_SIZE) return false;
  if (rd32(p + 20) == 0) return false;  // a song that never advances
  for (uint32_t i = 0; i < instruments; i++)
  {
    const uint8_t *in = p + instrumentsAt + i * BM_SOUND_INSTRUMENT_SIZE;
    if (in[0] == BM_WAVE_SAMPLE)
    {
      uint32_t at = rd32(in + 12), length = rd32(in + 16), loop = rd32(in + 20);
      if (at > size || length > size - at || length == 0 || length >= (1u << (32 - FRAC))) return false;
      if (loop != NONE && loop >= length) return false;
    }
    else if (in[0] >= BM_WAVE_BUILTIN_COUNT) return false;
  }
  for (uint32_t i = 0; i < events; i++)
  {
    if (p[eventsAt + i * BM_SOUND_EVENT_SIZE + 6] >= instruments) return false;
  }
  return true;
}

uint32_t bmSoundSongCount(const BmPack *pPack)
{
  uint32_t n = 0;
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_SONG, 0); c >= 0; c = bmPackFindChunk(pPack, BM_CHUNK_SONG, (uint32_t)c + 1)) n++;
  return n;
}

int32_t bmSoundSongChunk(const BmPack *pPack, uint32_t index)
{
  int32_t c = -1;
  for (uint32_t i = 0; i <= index; i++)
  {
    c = bmPackFindChunk(pPack, BM_CHUNK_SONG, (uint32_t)(c + 1));
    if (c < 0) return -1;
  }
  return c;
}

void bmSoundInit(BmSound *s, const BmPack *pPack, int8_t *pEcho)
{
  if (!s_wavesReady) buildWaves();
  memset(s, 0, sizeof(*s));
  s->pPack = pPack;
  s->musicVolume = s->musicTarget = 256;
  s->intensity = BM_SOUND_LAYERS - 1;
  for (int l = 0; l < BM_SOUND_LAYERS; l++) s->layerVolume[l] = 256;
  if (pEcho != NULL)
  {
    s->pEchoLeft = pEcho;
    s->pEchoRight = pEcho + BM_SOUND_ECHO_MAX;
    memset(pEcho, 0, 2 * BM_SOUND_ECHO_MAX);
  }
}

static void startPlayer(BmSound *s, int which, const uint8_t *pSong, uint32_t size, uint16_t nameId)
{
  // Notes of the song that was playing fade out.
  for (int v = 0; v < BM_SOUND_VOICES; v++)
  {
    if (s->voices[v].active && s->voices[v].owner == which) s->voices[v].framesLeft = 1;
  }
  BmSoundPlayer *p = &s->players[which];
  p->pSong = pSong;
  p->size = size;
  p->tick = 0;
  p->nextEvent = 0;
  p->nameId = nameId;
  p->playing = pSong != NULL;
  p->transpose = 0;
  if (which == 0)
  {
    // New music: full volume, its layers as the intensity says.
    s->musicVolume = s->musicTarget = 256;
    s->stopAtSilence = 0;
    for (int l = 0; l < BM_SOUND_LAYERS; l++) s->layerVolume[l] = l <= s->intensity ? 256 : 0;
    uint16_t delay = pSong != NULL ? rd16(pSong + 24) : 0;
    s->songEchoDelay = delay > BM_SOUND_ECHO_MAX ? BM_SOUND_ECHO_MAX : delay;
    s->songEchoFeedback = pSong != NULL ? pSong[26] : 0;
    if (!s->room.on)
    {
      // The song's own echo starts afresh (in a room with acoustics the room's goes on).
      s->echoPosition = 0;
      if (s->pEchoLeft != NULL) memset(s->pEchoLeft, 0, 2 * BM_SOUND_ECHO_MAX);
    }
  }
}

static bool playNamed(BmSound *s, const char *pName, int which)
{
  if (s->pPack == NULL || pName == NULL) return false;
  for (int32_t c = bmPackFindChunk(s->pPack, BM_CHUNK_SONG, 0); c >= 0; c = bmPackFindChunk(s->pPack, BM_CHUNK_SONG, (uint32_t)c + 1))
  {
    uint32_t size = 0;
    const uint8_t *p = bmPackChunkData(s->pPack, (uint32_t)c, &size);
    if (p == NULL || size < BM_SOUND_SONG_HEADER) continue;
    const char *name = bmPackString(s->pPack, rd16(p));
    if (name == NULL || strcmp(name, pName) != 0) continue;
    if (which == 0 && s->players[0].playing && s->players[0].pSong == p)
    {
      // Already playing: it goes on, back to full volume if it was fading.
      bmSoundFadeMusic(s, 256, 30, false);
      return true;
    }
    if (!bmSoundSongValid(p, size)) return false;
    startPlayer(s, which, p, size, rd16(p));
    return true;
  }
  return false;
}

bool bmSoundPlayMusic(BmSound *s, const char *pName)
{
  return playNamed(s, pName, 0);
}

void bmSoundStopMusic(BmSound *s)
{
  startPlayer(s, 0, NULL, 0, 0);
}

bool bmSoundPlayEffect(BmSound *s, const char *pName)
{
  return playNamed(s, pName, 1);
}

bool bmSoundPlayEffectAt(BmSound *s, const char *pName, int8_t semitones)
{
  if (!playNamed(s, pName, 1)) return false;
  s->players[1].transpose = semitones;
  return true;
}

void bmSoundFadeMusic(BmSound *s, uint16_t volume, uint16_t frames, bool stop)
{
  s->musicTarget = volume > 256 ? 256 : volume;
  uint32_t distance = s->musicTarget > s->musicVolume ? s->musicTarget - s->musicVolume : s->musicVolume - s->musicTarget;
  s->musicStep = (uint16_t)(frames == 0 ? 256 : (distance + frames - 1) / frames);
  if (s->musicStep == 0) s->musicStep = 1;
  s->stopAtSilence = stop;
}

void bmSoundSetRoom(BmSound *s, const BmSoundRoom *pRoom)
{
  if (pRoom == NULL || !pRoom->on) { s->room = (BmSoundRoom){0}; return; }
  s->room = *pRoom;
  if (s->room.echoDelay > BM_SOUND_ECHO_MAX) s->room.echoDelay = BM_SOUND_ECHO_MAX;
}

void bmSoundSetIntensity(BmSound *s, uint8_t level)
{
  s->intensity = level >= BM_SOUND_LAYERS ? BM_SOUND_LAYERS - 1 : level;
}

// Once a frame: the music's volume and its layers go towards their targets.
static void runVolumes(BmSound *s)
{
  if (s->musicVolume < s->musicTarget)
    s->musicVolume = (uint16_t)(s->musicTarget - s->musicVolume <= s->musicStep ? s->musicTarget : s->musicVolume + s->musicStep);
  else if (s->musicVolume > s->musicTarget)
    s->musicVolume = (uint16_t)(s->musicVolume - s->musicTarget <= s->musicStep ? s->musicTarget : s->musicVolume - s->musicStep);
  if (s->musicVolume == 0 && s->stopAtSilence && s->players[0].playing)
  {
    s->stopAtSilence = 0;
    startPlayer(s, 0, NULL, 0, 0);
    s->musicVolume = 0;   // startPlayer set it back; silence until the next song
    s->musicTarget = 0;
  }
  for (int l = 1; l < BM_SOUND_LAYERS; l++)
  {
    uint16_t target = l <= s->intensity ? 256 : 0, v = s->layerVolume[l];
    if (v < target) s->layerVolume[l] = (uint16_t)(target - v <= BM_SOUND_LAYER_STEP ? target : v + BM_SOUND_LAYER_STEP);
    else if (v > target) s->layerVolume[l] = (uint16_t)(v - target <= BM_SOUND_LAYER_STEP ? target : v - BM_SOUND_LAYER_STEP);
  }
}

bool bmSoundPlayData(BmSound *s, const uint8_t *pSong, uint32_t size, bool effect)
{
  if (pSong != NULL && !bmSoundSongValid(pSong, size)) return false;
  startPlayer(s, effect ? 1 : 0, pSong, size, 0);
  return true;
}

// --- Voices ---------------------------------------------------------------------------------

static void noteOn(BmSound *s, int owner, const uint8_t *pSong, uint8_t instrument, uint8_t key, uint8_t velocity, uint16_t frames)
{
  const uint8_t *in = pSong + rd32(pSong + 28) + (uint32_t)instrument * BM_SOUND_INSTRUMENT_SIZE;
  // A layer of the music that is not heard (and not fading) takes no voice.
  uint8_t layer = owner == 0 ? (uint8_t)(rd16(in + 10) & (BM_SOUND_LAYERS - 1)) : 0;
  if (layer != 0 && layer > s->intensity && s->layerVolume[layer] == 0) return;
  int32_t shifted = (int32_t)key + (owner == 1 ? s->players[1].transpose : 0);
  key = (uint8_t)(shifted < 0 ? 0 : shifted > 127 ? 127 : shifted);
  // A free voice; else the quietest one fading out; else, for an effect, the oldest note of
  // the music; else the oldest note of the same owner.
  int pick = -1;
  for (int v = 0; v < BM_SOUND_VOICES && pick < 0; v++)
    if (!s->voices[v].active) pick = v;
  if (pick < 0)
  {
    uint32_t best = NONE;
    for (int v = 0; v < BM_SOUND_VOICES; v++)
      if (s->voices[v].phase == 3 && s->voices[v].level < best) { best = s->voices[v].level; pick = v; }
  }
  if (pick < 0)
  {
    uint32_t oldest = NONE;
    for (int v = 0; v < BM_SOUND_VOICES; v++)
      if ((owner == 1 ? s->voices[v].owner == 0 : s->voices[v].owner == owner) && s->voices[v].age < oldest) { oldest = s->voices[v].age; pick = v; }
  }
  if (pick < 0) return;  // music does not take voices from effects

  BmSoundVoice *v = &s->voices[pick];
  memset(v, 0, sizeof(*v));
  uint8_t wave = in[0];
  uint32_t stepAt60;
  if (wave == BM_WAVE_SAMPLE)
  {
    uint32_t length = rd32(in + 16), loop = rd32(in + 20);
    v->pData = (const int8_t *)(pSong + rd32(in + 12));
    v->length = length << FRAC;
    v->loopLength = loop == NONE ? 0 : (length - loop) << FRAC;
    stepAt60 = rd32(in + 24);
  }
  else if (wave == BM_WAVE_NOISE)
  {
    v->pData = s_noise;
    v->length = v->loopLength = (uint32_t)NOISE_SAMPLES << FRAC;
    stepAt60 = 1u << FRAC;
  }
  else
  {
    v->pData = s_waves[wave];
    v->length = v->loopLength = (uint32_t)WAVE_SAMPLES << FRAC;
    stepAt60 = 3777;  // 261.63 Hz * 64 samples / 18157 Hz, in 20.12
  }
  v->step = stepForKey(stepAt60, key);
  v->volume = in[1];
  v->pan = (int8_t)in[2];
  v->sustainLevel = (uint16_t)(in[3] << 8);
  v->attackRate = rd16(in + 4);
  v->decayRate = rd16(in + 6);
  v->releaseRate = rd16(in + 8);
  v->velocity = velocity > 127 ? 127 : velocity;
  v->framesLeft = frames == 0 ? 1 : frames;
  v->owner = (uint8_t)owner;
  v->layer = layer;
  v->age = s->frame;
  v->active = 1;
}

void bmSoundNote(BmSound *s, const uint8_t *pSong, uint32_t size, uint8_t instrument, uint8_t key, uint8_t velocity, uint16_t frames)
{
  if (!bmSoundSongValid(pSong, size) || instrument >= rd16(pSong + 4)) return;
  noteOn(s, 1, pSong, instrument, key, velocity, frames);
}

static void runPlayer(BmSound *s, int which)
{
  BmSoundPlayer *p = &s->players[which];
  if (!p->playing || p->pSong == NULL) return;
  const uint8_t *song = p->pSong;
  uint32_t count = rd32(song + 8), length = rd32(song + 12), loopStart = rd32(song + 16), rate = rd32(song + 20);
  const uint8_t *events = song + rd32(song + 32);
  p->tick += rate;
  for (int guard = 0; guard < 4; guard++)
  {
    uint32_t now = p->tick >> 16;
    uint32_t until = now < length ? now : length;
    while (p->nextEvent < count)
    {
      const uint8_t *e = events + p->nextEvent * BM_SOUND_EVENT_SIZE;
      uint32_t at = rd32(e);
      // Events at the very end belong to the next pass of a loop.
      if (at > until || (at >= length && length != 0)) break;
      uint32_t frames = ((uint32_t)rd16(e + 4) << 16) / rate;
      noteOn(s, which, song, e[6], e[7], e[8], (uint16_t)(frames > 0xFFFF ? 0xFFFF : frames));
      p->nextEvent++;
    }
    if (now < length) return;
    if (loopStart == NONE || loopStart >= length)
    {
      p->playing = 0;
      return;
    }
    // Back to the loop point, keeping what is left of this frame.
    p->tick -= (length - loopStart) << 16;
    p->nextEvent = 0;
    while (p->nextEvent < count && rd32(events + p->nextEvent * BM_SOUND_EVENT_SIZE) < loopStart) p->nextEvent++;
  }
}

static void runEnvelope(BmSoundVoice *v, uint32_t gain)
{
  switch (v->phase)
  {
  case 0:
    if (v->attackRate == 0 || (uint32_t)v->level + v->attackRate >= 65535) { v->level = 65535; v->phase = 1; }
    else v->level = (uint16_t)(v->level + v->attackRate);
    break;
  case 1:
    if (v->level <= v->sustainLevel || v->level - v->sustainLevel <= v->decayRate || v->decayRate == 0) { v->level = v->sustainLevel; v->phase = 2; }
    else v->level = (uint16_t)(v->level - v->decayRate);
    break;
  case 3:
    if (v->releaseRate == 0 || v->level <= v->releaseRate) { v->level = 0; v->active = 0; }
    else v->level = (uint16_t)(v->level - v->releaseRate);
    break;
  default:
    break;
  }
  if (v->phase != 3 && v->framesLeft != 0 && --v->framesLeft == 0) v->phase = 3;
  if (v->phase == 2 && v->sustainLevel == 0) v->active = 0;  // decayed to silence

  uint32_t volume = ((uint32_t)(v->level >> 8) * v->velocity * v->volume) >> 15;
  if (volume > 256) volume = 256;
  volume = (volume * gain) >> 8;   // the music's volume and layer (256: as it is)
  int32_t pan = v->pan;
  v->volumeLeft = (int16_t)(pan > 0 ? (volume * (uint32_t)(64 - pan)) >> 6 : volume);
  v->volumeRight = (int16_t)(pan < 0 ? (volume * (uint32_t)(64 + pan)) >> 6 : volume);
}

// --- Mixing ---------------------------------------------------------------------------------

// Adds one voice to the sums (left, right, left, right ...).
BM_IWRAM_CODE static void mixVoice(BmSoundVoice *v, int32_t *pSum, uint32_t count)
{
  const int8_t *data = v->pData;
  uint32_t position = v->position, step = v->step, length = v->length, loop = v->loopLength;
  int32_t left = v->volumeLeft, right = v->volumeRight;
  uint32_t samples = length >> FRAC;
  if (loop == length && (samples & (samples - 1)) == 0)
  {
    // A wave that repeats and whose length is a power of two (all built-in waves): the
    // position wraps by masking, with no check per sample.
    uint32_t mask = samples - 1;
    int32_t *end = pSum + 2 * count;
    while (pSum != end)
    {
      int32_t sample = data[(position >> FRAC) & mask];
      int32_t l = pSum[0], r = pSum[1];
      position += step;
      l += sample * left;
      r += sample * right;
      pSum[0] = l;
      pSum[1] = r;
      pSum += 2;
    }
    v->position = position & ((length - 1) | ((1u << FRAC) - 1));
    return;
  }
  for (uint32_t i = 0; i < count; i++)
  {
    if (position >= length)
    {
      if (loop == 0)
      {
        v->active = 0;
        break;
      }
      do position -= loop; while (position >= length);
    }
    int32_t sample = data[position >> FRAC];
    pSum[0] += sample * left;
    pSum[1] += sample * right;
    pSum += 2;
    position += step;
  }
  v->position = position;
}

static inline int32_t clip(int32_t v)
{
  if (v > 127) v = 127;
  if (v < -128) v = -128;
  return v;
}

// Turns the sums into samples for both sides and keeps the checksum.
BM_IWRAM_CODE static uint32_t finishPlain(const int32_t *pSum, int8_t *pLeft, int8_t *pRight, uint32_t count, uint32_t check)
{
  uint32_t a = check & 0xFFFF, b = check >> 16;
  for (uint32_t i = 0; i < count; i++)
  {
    int32_t l = clip(pSum[0] >> 8), r = clip(pSum[1] >> 8);
    pSum += 2;
    pLeft[i] = (int8_t)l;
    pRight[i] = (int8_t)r;
    a += (uint8_t)l;
    b += a;
    a += (uint8_t)r;
    b += a;
  }
  return (a & 0xFFFF) | (b << 16);
}

// The same with the echo: what was heard `delay` samples ago comes back, quieter.
BM_IWRAM_CODE static uint32_t finishEcho(BmSound *s, const int32_t *pSum, int8_t *pLeft, int8_t *pRight, uint32_t count, uint32_t check)
{
  uint32_t a = check & 0xFFFF, b = check >> 16;
  int8_t *echoLeft = s->pEchoLeft, *echoRight = s->pEchoRight;
  uint32_t position = s->echoPosition, delay = s->echoDelay;
  int32_t feedback = s->echoFeedback;
  for (uint32_t i = 0; i < count; i++)
  {
    // The echo rounded towards zero, so it dies away: rounded down, a -1 (and with much
    // feedback a little more) would come back unchanged for ever, the end of the sound
    // repeating quietly once a delay.
    int32_t el = echoLeft[position] * feedback, er = echoRight[position] * feedback;
    el = (el + ((el >> 31) & 255)) >> 8;
    er = (er + ((er >> 31) & 255)) >> 8;
    int32_t l = clip((pSum[0] >> 8) + el);
    int32_t r = clip((pSum[1] >> 8) + er);
    pSum += 2;
    echoLeft[position] = (int8_t)l;
    echoRight[position] = (int8_t)r;
    if (++position >= delay) position = 0;
    pLeft[i] = (int8_t)l;
    pRight[i] = (int8_t)r;
    a += (uint8_t)l;
    b += a;
    a += (uint8_t)r;
    b += a;
  }
  s->echoPosition = (uint16_t)position;
  return (a & 0xFFFF) | (b << 16);
}

// The room's acoustics: the echo with each repeat low-passed (damping), then the whole output
// (muffle): one-pole filters, y += (x - y) * (256 - k) / 256, kept in 8.8. (At k = 0 they pass
// the samples unchanged; the song's echo has its own loop without them, as it plays most of
// the time.)
BM_IWRAM_CODE static uint32_t finishRoom(BmSound *s, const int32_t *pSum, int8_t *pLeft, int8_t *pRight, uint32_t count, uint32_t check)
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

BM_IWRAM_CODE static void clearSums(int32_t *pSum, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++)
  {
    pSum[0] = 0;
    pSum[1] = 0;
    pSum += 2;
  }
}

void bmSoundFrame(BmSound *s, int8_t *pLeft, int8_t *pRight, uint32_t count)
{
  if (count > BM_SOUND_FRAME_SAMPLES) count = BM_SOUND_FRAME_SAMPLES;
  s->frame++;
  runVolumes(s);
  runPlayer(s, 0);
  runPlayer(s, 1);

  clearSums(s_sum, count);
  uint8_t used = 0;
  for (int i = 0; i < BM_SOUND_VOICES; i++)
  {
    BmSoundVoice *v = &s->voices[i];
    if (!v->active) continue;
    runEnvelope(v, v->owner == 0 ? ((uint32_t)s->musicVolume * s->layerVolume[v->layer]) >> 8 : 256);
    if (!v->active) continue;
    used++;
    if (v->volumeLeft != 0 || v->volumeRight != 0) mixVoice(v, s_sum, count);
    else
    {
      // Silent, but time passes for it.
      uint64_t position = (uint64_t)v->position + (uint64_t)v->step * count;
      if (position >= v->length)
      {
        if (v->loopLength == 0) v->active = 0;
        else position = v->length - v->loopLength + (position - v->length) % v->loopLength;
      }
      v->position = (uint32_t)position;
    }
  }
  s->voicesUsed = used;
  if (used > s->voicesPeak) s->voicesPeak = used;

  // What is heard: the room's acoustics, or the song's echo. (A room with only a muffle runs
  // the echo with nothing coming back: one sample of delay, no feedback.)
  if (s->room.on)
  {
    s->echoDelay = s->room.echoDelay != 0 ? s->room.echoDelay : 1;
    s->echoFeedback = s->room.echoDelay != 0 ? s->room.feedback : 0;
    s->echoDamping = s->room.damping;
    s->muffle = s->room.muffle;
  }
  else
  {
    s->echoDelay = s->songEchoDelay;
    s->echoFeedback = s->songEchoFeedback;
    s->echoDamping = 0;
    s->muffle = 0;
  }
  if (s->echoPosition >= s->echoDelay) s->echoPosition = 0;
  if (s->echoDelay != 0 && s->pEchoLeft != NULL)
  {
    if (s->echoDamping != 0 || s->muffle != 0) s->outputCrc = finishRoom(s, s_sum, pLeft, pRight, count, s->outputCrc);
    else s->outputCrc = finishEcho(s, s_sum, pLeft, pRight, count, s->outputCrc);
  }
  else s->outputCrc = finishPlain(s_sum, pLeft, pRight, count, s->outputCrc);
}
