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
// when none is free; an echo (one delay line per side); the room's acoustics (an echo whose
// repeats lose their treble, and a low pass over everything: bmSoundSetRoom).

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
#define BM_SOUND_LAYERS 16            // intensity levels 0-15
#define BM_SOUND_SONG_LAYERS 2        // flag: a table of the layers' ranges follows the header
#define BM_SOUND_LAYER_FULL 4096      // a layer's volume when heard (256 in 4 fraction bits)
#define BM_SOUND_LAYER_STEP 96        // the default fade, 6/256 a frame: 43 frames
#define BM_SOUND_SONG_INTENSITY(flags) ((uint8_t)(15 - (((flags) >> 8) & 15)))   // a song's mode at the start

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
  uint8_t layer;          // of its instrument (music): which layer's volume it follows
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
  int8_t transpose;       // semitones added to every note (an effect's pitch: a voice)
} BmSoundPlayer;

/// What the room does to the sound (a room's `acoustics`, room-format.md §1): an echo after
/// `echoDelay` samples (at most BM_SOUND_ECHO_MAX) coming back at `feedback`/256, each repeat
/// low-passed by `damping` (0 none .. 255 most: a cave's dull, far echo), and the whole mix
/// low-passed by `muffle` (0 none .. 255 most: under water). While `on`, it replaces the echo
/// of the song playing; off, the song's echo is heard.
typedef struct BmSoundRoom
{
  uint8_t on;
  uint16_t echoDelay;
  uint8_t feedback, damping, muffle;
  uint8_t music;          // 1: the music too (in place of its song's echo); 0: the effects only
} BmSoundRoom;

typedef struct BmSound
{
  const BmPack *pPack;
  BmSoundVoice voices[BM_SOUND_VOICES];
  BmSoundPlayer players[2];            // music, effect
  uint32_t frame;
  // The music's volume (0-256), going towards a target by a step a frame; at 0 with
  // stopAtSilence the music stops. Dynamic music: the instruments of a layer are heard while the
  // intensity lies in the layer's range (layerRange: from in bits 0-3, until in 4-7); each
  // layer's volume (0 to BM_SOUND_LAYER_FULL) goes towards that by layerStep a frame (the
  // song's fade time).
  uint16_t musicVolume, musicTarget, musicStep;
  uint8_t stopAtSilence;
  uint8_t intensity;
  uint16_t layerStep;
  uint8_t layerRange[BM_SOUND_LAYERS];
  uint16_t layerVolume[BM_SOUND_LAYERS];
  // Echo of the song playing as music, and the room's acoustics; what is heard (echoDelay ...
  // muffle: the room's while it has some, else the song's echo).
  uint16_t songEchoDelay;
  uint8_t songEchoFeedback;
  BmSoundRoom room;
  uint16_t echoDelay;
  uint8_t echoFeedback, echoDamping, muffle;
  uint16_t echoPosition;
  int16_t echoLow[2], mixLow[2];       // the low passes' last values (left, right; 8.8)
  int8_t *pEchoLeft, *pEchoRight;      // BM_SOUND_ECHO_MAX samples each, or NULL: no echo
  // The effects' own echo (the room's acoustics, when they leave the music to its song): its
  // lines (NULL: the effects dry), place and filters.
  int8_t *pFxEcho;                     // left, then right after BM_SOUND_ECHO_MAX
  uint16_t fxEchoPosition;
  int16_t fxEchoLow[2], fxMixLow[2];
  // For tests and the budget display.
  uint32_t outputCrc;                  // over everything mixed so far
  uint8_t voicesUsed, voicesPeak;
} BmSound;

/// `pEcho` is memory for the echo (2 * BM_SOUND_ECHO_MAX bytes), or NULL for none.
void bmSoundInit(BmSound *pSound, const BmPack *pPack, int8_t *pEcho);

/// Memory for the effects' echo in a room with acoustics (2 * BM_SOUND_ECHO_MAX bytes for its
/// lines, then 2 * BM_SOUND_FRAME_SAMPLES for a frame of their output; zeroed), or NULL: the
/// effects are then dry there too.
void bmSoundSetEffectEcho(BmSound *pSound, int8_t *pEcho);

/// Number of songs in the pack, and the chunk of the `index`-th one (or -1).
uint32_t bmSoundSongCount(const BmPack *pPack);
int32_t bmSoundSongChunk(const BmPack *pPack, uint32_t index);

/// Plays the song called `pName` as music. Nothing happens if it is the one playing. False if
/// the pack has no such song.
bool bmSoundPlayMusic(BmSound *pSound, const char *pName);
void bmSoundStopMusic(BmSound *pSound);
/// The same for a room's song: one that starts starts at its own intensity (its `intensity`, its
/// first mode; 15 if it has none); the one playing goes on in the mode it is in.
bool bmSoundPlayRoomMusic(BmSound *pSound, const char *pName);
/// Plays a song as a sound effect (it replaces the effect playing).
bool bmSoundPlayEffect(BmSound *pSound, const char *pName);

/// The same with every note `semitones` higher (negative: lower): a speaker's voice blip.
bool bmSoundPlayEffectAt(BmSound *pSound, const char *pName, int8_t semitones);

/// Stops the effect playing: its notes fade out with their release (a looping effect, such as
/// a block dragged along: puzzle.h).
void bmSoundStopEffect(BmSound *pSound);

/// Whether the effect playing is the song named `pName`.
bool bmSoundEffectIs(const BmSound *pSound, const char *pName);

/// Fades the music to `volume` (0-256) in `frames` frames (0: at once); with `stop`, the music
/// stops when it gets to 0. Starting a song as music sets the volume to 256 again.
void bmSoundFadeMusic(BmSound *pSound, uint16_t volume, uint16_t frames, bool stop);

/// Dynamic music: the instruments of a layer are heard while `level` lies in the layer's range
/// (by default layer n from n to 15: layers adding up; a range ending lower: a mode of the song
/// that gives way to another). Layers coming in or going out fade over the song's fade time. 15
/// at the start: everything. A song started as music begins as the level says, without fading.
void bmSoundSetIntensity(BmSound *pSound, uint8_t level);



/// The room's acoustics (BmSoundRoom); `pRoom` NULL or not `on`: none, the song's echo again.
/// The echo heard so far rings on into the new setting.
void bmSoundSetRoom(BmSound *pSound, const BmSoundRoom *pRoom);

/// Plays song data directly (the editor's preview): as music or as an effect.
bool bmSoundPlayData(BmSound *pSound, const uint8_t *pSong, uint32_t size, bool effect);
/// One note with an instrument of song data, for `frames` frames (the editor's keyboard).
void bmSoundNote(BmSound *pSound, const uint8_t *pSong, uint32_t size, uint8_t instrument, uint8_t key, uint8_t velocity, uint16_t frames);

/// One frame: runs the sequencers and the envelopes, then mixes `count` samples
/// (BM_SOUND_FRAME_SAMPLES on the device) into the two buffers.
void bmSoundFrame(BmSound *pSound, int8_t *pLeft, int8_t *pRight, uint32_t count);

// Inside the engine: the room's acoustics over summed samples (sound.c), and the effects through
// them with their own lines and filters, their frame of output returned (sound_room.c: left,
// then right after BM_SOUND_FRAME_SAMPLES; the multiboot images link a stub without it).
uint32_t bmSoundFinishRoom(BmSound *pSound, const int32_t *pSum, int8_t *pLeft, int8_t *pRight, uint32_t count, uint32_t check);
const int8_t *bmSoundEffectsRoom(BmSound *pSound, const int32_t *pSum, uint32_t count);

/// True if the data is a song the engine can play (every offset inside the chunk).
bool bmSoundSongValid(const uint8_t *pSong, uint32_t size);
