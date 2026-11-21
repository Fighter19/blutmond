/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The sound engine (docs/spec/sound-format.md, PLAN.md section 13): a sequencer that plays
// songs (SONG chunks: instruments and note events) on a stereo mixer of 8-bit voices. One
// frame of game time is exactly 304 samples at 18157 Hz, so sound is mixed once per frame.
// Everything is integer arithmetic: the editor's preview and the device produce the same
// samples. The engine is a plain struct with no global state, so the editor runs its own.
//
// This version: 8 voices; built-in waves (pulse 12.5/25/50 %, saw, triangle, sine, noise) and
// samples; an attack/decay/sustain/release envelope per voice, updated once per frame; one
// song as music and one as a sound effect at a time, effects taking voices from the music
// when none is free; an echo (one delay line per side).

#include <stdbool.h>
#include <stdint.h>

#include "pack.h"

#define BM_SOUND_RATE 18157
#define BM_SOUND_FRAME_SAMPLES 304
#define BM_SOUND_VOICES 8
#define BM_SOUND_TICKS_PER_BEAT 24
#define BM_SOUND_ECHO_MAX 4096        // samples of delay per side

#define BM_SOUND_SONG_HEADER 40
#define BM_SOUND_INSTRUMENT_SIZE 28
#define BM_SOUND_EVENT_SIZE 12
#define BM_SOUND_SONG_SFX 1

typedef enum BmSoundWave {
  BM_WAVE_PULSE_12 = 0,
  BM_WAVE_PULSE_25,
  BM_WAVE_PULSE_50,
  BM_WAVE_SAW,
  BM_WAVE_TRIANGLE,
  BM_WAVE_SINE,
  BM_WAVE_NOISE,
  BM_WAVE_BUILTIN_COUNT,
  BM_WAVE_SAMPLE = 255,
} BmSoundWave;

typedef struct BmSoundVoice
{
  const int8_t *pData;
  uint32_t length;        // in samples, 20.12
  uint32_t loopLength;    // 20.12; 0: plays once
  uint32_t position;      // 20.12
  uint32_t step;          // 20.12 per output sample
  int16_t volumeLeft, volumeRight;   // 0..256
  // Envelope and bookkeeping (updated once per frame).
  uint8_t active;
  uint8_t phase;          // 0 attack, 1 decay, 2 sustain, 3 release
  uint8_t owner;          // 0 music, 1 effect
  uint8_t velocity;
  uint16_t level;         // 0..65535
  uint16_t attackRate, decayRate, releaseRate, sustainLevel;
  uint16_t framesLeft;    // until the note is released
  uint8_t volume;
  int8_t pan;
  uint32_t age;
} BmSoundVoice;

/// One song being played.
typedef struct BmSoundPlayer
{
  const uint8_t *pSong;   // NULL: nothing
  uint32_t size;
  uint32_t tick;          // 16.16
  uint32_t nextEvent;
  uint16_t nameId;
  uint8_t playing;
} BmSoundPlayer;

typedef struct BmSound
{
  const BmPack *pPack;
  BmSoundVoice voices[BM_SOUND_VOICES];
  BmSoundPlayer players[2];            // music, effect
  uint32_t frame;
  // Echo of the song playing as music.
  uint16_t echoDelay;
  uint8_t echoFeedback;
  uint16_t echoPosition;
  int8_t *pEchoLeft, *pEchoRight;      // BM_SOUND_ECHO_MAX samples each, or NULL: no echo
  // For tests and the budget display.
  uint32_t outputCrc;                  // over everything mixed so far
  uint8_t voicesUsed, voicesPeak;
} BmSound;

/// `pEcho` is memory for the echo (2 * BM_SOUND_ECHO_MAX bytes), or NULL for none.
void bmSoundInit(BmSound *pSound, const BmPack *pPack, int8_t *pEcho);

/// Number of songs in the pack, and the chunk of the `index`-th one (or -1).
uint32_t bmSoundSongCount(const BmPack *pPack);
int32_t bmSoundSongChunk(const BmPack *pPack, uint32_t index);

/// Plays the song called `pName` as music. Nothing happens if it is the one playing. False if
/// the pack has no such song.
bool bmSoundPlayMusic(BmSound *pSound, const char *pName);
void bmSoundStopMusic(BmSound *pSound);
/// Plays a song as a sound effect (it replaces the effect playing).
bool bmSoundPlayEffect(BmSound *pSound, const char *pName);

/// Plays song data directly (the editor's preview): as music or as an effect.
bool bmSoundPlayData(BmSound *pSound, const uint8_t *pSong, uint32_t size, bool effect);
/// One note with an instrument of song data, for `frames` frames (the editor's keyboard).
void bmSoundNote(BmSound *pSound, const uint8_t *pSong, uint32_t size, uint8_t instrument, uint8_t key, uint8_t velocity, uint16_t frames);

/// One frame: runs the sequencers and the envelopes, then mixes `count` samples
/// (BM_SOUND_FRAME_SAMPLES on the device) into the two buffers.
void bmSoundFrame(BmSound *pSound, int8_t *pLeft, int8_t *pRight, uint32_t count);

/// True if the data is a song the engine can play (every offset inside the chunk).
bool bmSoundSongValid(const uint8_t *pSong, uint32_t size);
