// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Songs and sound effects as source assets (docs/spec/sound-format.md): a .bmsong file with
// instruments (a wave or a sample with an envelope) and tracks of notes, and its cooked form,
// the SONG chunk the sound engine plays (src/sound/sound.h).
#pragma once

#include "cook.h"

#include <string>
#include <vector>

namespace bm::song {

struct Instrument
{
  std::string name;
  std::string wave = "pulse50";   // pulse12, pulse25, pulse50, saw, triangle, sine, noise, sample
  std::string sample;             // WAV file for wave "sample", relative to the song
  int root = 60;                  // the key at which the sample plays at its own rate
  bool loop = false;              // the sample repeats while the note is held
  int attack = 0, decay = 0, release = 4;   // in frames (1/60 s)
  int sustain = 255;              // level after the decay, 0-255
  int volume = 200;               // 0-255
  int pan = 0;                    // -64 (left) .. 63 (right)
  int layer = 0;                  // 0: always heard; 1-15: heard from that music intensity on
};

struct Note
{
  int tick = 0;                   // 24 ticks per beat
  int key = 60;                   // MIDI: 60 is middle C
  int length = 24;
  int velocity = 100;             // 1-127
};

struct Track
{
  std::string name;
  int instrument = 0;
  std::vector<Note> notes;
};

struct Song
{
  std::string path, name;         // name: file name without extension
  int bpm = 120;
  int length = 96 * 4;            // ticks
  int loopStart = 0;              // tick the song repeats from; -1: plays once
  bool effect = false;            // a sound effect (plays once, on top of the music)
  int echoMs = 0;                 // delay of the echo, 0: none (at most 225 ms)
  int echoFeedback = 80;          // 0-255
  std::vector<Instrument> instruments;
  std::vector<Track> tracks;
};

Song load(const std::string &path);
bool save(const Song &song, std::string *pError = nullptr);

/// What a song costs, for the report and the editor.
struct Cost
{
  int chunkBytes = 0, sampleBytes = 0;
  int events = 0;
  int polyphony = 0;              // most notes sounding at once (releases included)
};

/// The SONG chunk. `nameId` is the song's name in the string table. Problems go to the report;
/// the result is empty if the song cannot be cooked.
std::vector<uint8_t> chunk(const Song &song, uint16_t nameId, Report &report, Cost *pCost = nullptr);

/// The wave names, in the order of the engine's wave numbers.
const std::vector<std::string> &waveNames();

}  // namespace bm::song
