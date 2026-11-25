// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The sound engine on songs made in the test: pitch, envelope, loops, voices, echo, samples.
// Arguments: the repository's assets folder.

#include "song_asset.h"

extern "C" {
#include "sound.h"
}

#include "test.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bm;

static std::string s_assets;

static std::vector<uint8_t> cook(const song::Song &s, song::Cost *pCost = nullptr)
{
  Report report;
  std::vector<uint8_t> data = song::chunk(s, 0, report, pCost);
  if (data.empty()) printf("%s", report.text().c_str());
  return data;
}

static song::Song oneNote(const std::string &wave, int key, int length)
{
  song::Song s;
  s.path = s_assets + "/test_room/music/test.bmsong";
  s.bpm = 120;
  s.length = 960;
  s.loopStart = -1;
  song::Instrument in;
  in.name = "i";
  in.wave = wave;
  in.release = 0;
  in.volume = 255;
  s.instruments.push_back(in);
  song::Track t;
  t.notes.push_back({0, key, length, 127});
  s.tracks.push_back(t);
  return s;
}

struct Output
{
  std::vector<int8_t> left, right;
};

static Output play(BmSound &sound, int frames)
{
  Output out;
  for (int f = 0; f < frames; f++)
  {
    int8_t l[BM_SOUND_FRAME_SAMPLES], r[BM_SOUND_FRAME_SAMPLES];
    bmSoundFrame(&sound, l, r, BM_SOUND_FRAME_SAMPLES);
    out.left.insert(out.left.end(), l, l + BM_SOUND_FRAME_SAMPLES);
    out.right.insert(out.right.end(), r, r + BM_SOUND_FRAME_SAMPLES);
  }
  return out;
}

static int peak(const std::vector<int8_t> &v, size_t from = 0, size_t to = size_t(-1))
{
  int p = 0;
  for (size_t i = from; i < v.size() && i < to; i++) p = std::max(p, std::abs(int(v[i])));
  return p;
}

static double frequency(const std::vector<int8_t> &v)
{
  int crossings = 0;
  for (size_t i = 1; i < v.size(); i++) crossings += v[i - 1] < 0 && v[i] >= 0;
  return double(crossings) * BM_SOUND_RATE / double(v.size());
}

static void testPitch()
{
  // The A above middle C is 440 Hz; an octave up is twice that, on every wave.
  for (const char *wave : {"sine", "pulse50", "saw", "triangle"})
    for (int key : {57, 69, 81})
    {
      std::vector<uint8_t> data = cook(oneNote(wave, key, 900));
      TEST_ASSERT(!data.empty());
      BmSound sound;
      bmSoundInit(&sound, nullptr, nullptr);
      TEST_ASSERT(bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false));
      Output out = play(sound, 60);
      double expected = 440.0 * std::pow(2.0, (key - 69) / 12.0), got = frequency(out.left);
      if (std::fabs(got - expected) > expected * 0.01) printf("  %s key %d: %.1f Hz, expected %.1f\n", wave, key, got, expected);
      TEST_ASSERT(std::fabs(got - expected) <= expected * 0.01);
      TEST_ASSERT(peak(out.left) > 80);
      TEST_ASSERT(out.left == out.right);  // pan 0
    }
}

static void testEnvelopeAndRelease()
{
  song::Song s = oneNote("pulse50", 60, 24);  // one beat at 120 bpm: 30 frames
  s.instruments[0].attack = 10;
  s.instruments[0].decay = 10;
  s.instruments[0].sustain = 128;
  s.instruments[0].release = 20;
  std::vector<uint8_t> data = cook(s);
  BmSound sound;
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  Output out = play(sound, 80);
  auto frame = [&](int f) { return peak(out.left, size_t(f) * BM_SOUND_FRAME_SAMPLES, size_t(f + 1) * BM_SOUND_FRAME_SAMPLES); };
  TEST_ASSERT(frame(1) < frame(5) && frame(5) < frame(9));     // rising
  TEST_ASSERT(frame(10) > 90);                                // full
  TEST_ASSERT(frame(25) > 40 && frame(25) < 60);              // sustained at half
  TEST_ASSERT(frame(40) < frame(32));                         // released, fading
  TEST_ASSERT(frame(60) == 0);                                // gone
  TEST_ASSERT(sound.voicesUsed == 0 && sound.voicesPeak == 1);
  TEST_ASSERT(!sound.players[0].playing || sound.players[0].tick > 0);
}

static void testPanAndVolume()
{
  song::Song s = oneNote("sine", 69, 900);
  s.instruments[0].pan = -64;
  std::vector<uint8_t> data = cook(s);
  BmSound sound;
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  Output out = play(sound, 10);
  TEST_ASSERT(peak(out.left) > 80 && peak(out.right) == 0);
  s.instruments[0].pan = 0;
  s.instruments[0].volume = 64;
  data = cook(s);
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  out = play(sound, 10);
  TEST_ASSERT(peak(out.left) > 20 && peak(out.left) < 40);    // a quarter
}

static void testLoopAndEnd()
{
  // Two beats long with a note on each; looping from the second beat.
  song::Song s = oneNote("pulse50", 60, 6);
  s.length = 48;
  s.loopStart = 24;
  s.tracks[0].notes.push_back({24, 72, 6, 127});
  std::vector<uint8_t> data = cook(s);
  BmSound sound;
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  // About 30 frames per beat. Count the notes that start (a voice's age changes) in 140
  // frames: beats at ticks 0, 24, and after the loop again at 48, 72 and 96.
  int starts = 0;
  uint32_t lastAge = 0;
  for (int f = 0; f < 140; f++)
  {
    int8_t l[BM_SOUND_FRAME_SAMPLES], r[BM_SOUND_FRAME_SAMPLES];
    bmSoundFrame(&sound, l, r, BM_SOUND_FRAME_SAMPLES);
    for (const BmSoundVoice &v : sound.voices)
      if (v.active && v.age > lastAge) { lastAge = v.age; starts++; }
  }
  TEST_ASSERT(starts == 5);                                   // beat 1 once, beat 2 four times
  TEST_ASSERT(sound.players[0].playing);
  // Without a loop it ends.
  s.loopStart = -1;
  data = cook(s);
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  play(sound, 100);
  TEST_ASSERT(!sound.players[0].playing);
}

static void testVoicesAndEffects()
{
  // Twelve notes at once: eight voices sound; the report says so.
  song::Song s = oneNote("sine", 48, 900);
  for (int k = 1; k < 12; k++) s.tracks[0].notes.push_back({0, 48 + k, 900, 60});
  s.instruments[0].volume = 40;
  song::Cost cost;
  std::vector<uint8_t> music = cook(s, &cost);
  TEST_ASSERT(cost.polyphony == 12);
  BmSound sound;
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, music.data(), uint32_t(music.size()), false);
  play(sound, 5);
  TEST_ASSERT(sound.voicesUsed == BM_SOUND_VOICES);

  // An effect takes a voice from the music and gives it back.
  song::Song fx = oneNote("pulse50", 84, 6);
  fx.effect = true;
  fx.length = 12;
  std::vector<uint8_t> effect = cook(fx);
  TEST_ASSERT(bmSoundPlayData(&sound, effect.data(), uint32_t(effect.size()), true));
  play(sound, 2);
  int effects = 0;
  for (const BmSoundVoice &v : sound.voices) effects += v.active && v.owner == 1;
  TEST_ASSERT(effects == 1 && sound.voicesUsed == BM_SOUND_VOICES);
  play(sound, 40);
  effects = 0;
  for (const BmSoundVoice &v : sound.voices) effects += v.active && v.owner == 1;
  TEST_ASSERT(effects == 0);
  TEST_ASSERT(sound.players[0].playing);                      // the music went on
}

static void testEcho()
{
  song::Song s = oneNote("pulse50", 60, 3);
  s.echoMs = 100;                                             // 1815 samples: about 6 frames
  s.echoFeedback = 128;
  std::vector<uint8_t> data = cook(s);
  static int8_t echo[2 * BM_SOUND_ECHO_MAX];
  BmSound sound;
  bmSoundInit(&sound, nullptr, echo);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  Output out = play(sound, 30);
  auto frame = [&](int f) { return peak(out.left, size_t(f) * BM_SOUND_FRAME_SAMPLES, size_t(f + 1) * BM_SOUND_FRAME_SAMPLES); };
  TEST_ASSERT(frame(0) > 80);                                 // the note
  TEST_ASSERT(frame(4) == 0);                                 // over, before the echo
  TEST_ASSERT(frame(7) > 30 && frame(7) < 70);                // the echo at half
  TEST_ASSERT(frame(13) > 10 && frame(13) < frame(7));        // and its echo, quieter
  // It dies away to silence, even with much feedback (it used to stop at -1, the last of it
  // coming back for ever).
  for (uint8_t feedback : {128, 50, 230})
  {
    s.echoFeedback = feedback;
    data = cook(s);
    bmSoundInit(&sound, nullptr, echo);
    bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
    Output tail = play(sound, 900);
    size_t n = tail.left.size();
    bool silent = true;
    for (size_t i = n - 20 * BM_SOUND_FRAME_SAMPLES; i < n; i++) silent = silent && tail.left[i] == 0 && tail.right[i] == 0;
    TEST_ASSERT(silent);
  }
  s.echoFeedback = 128;
  data = cook(s);
  // Without memory for it there is no echo.
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  out = play(sound, 30);
  TEST_ASSERT(frame(7) == 0);
}

// A room's acoustics (bmSoundSetRoom) replace the song's echo while they are on: none at all
// ("dry"), an echo whose repeats lose their edges (damping), everything low-passed (muffle).
static void testRoomAcoustics()
{
  song::Song s = oneNote("pulse50", 60, 3);
  s.echoMs = 100;
  s.echoFeedback = 128;
  std::vector<uint8_t> data = cook(s);
  static int8_t echo[2 * BM_SOUND_ECHO_MAX];
  BmSound sound;
  static int8_t fxEcho[2 * BM_SOUND_ECHO_MAX + 2 * BM_SOUND_FRAME_SAMPLES];
  auto run = [&](const BmSoundRoom *pRoom, int frames) {
    bmSoundInit(&sound, nullptr, echo);
    bmSoundSetEffectEcho(&sound, fxEcho);
    bmSoundSetRoom(&sound, pRoom);
    bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
    return play(sound, frames);
  };
  auto range = [](int f0, int f1) { return std::make_pair(size_t(f0) * BM_SOUND_FRAME_SAMPLES, size_t(f1) * BM_SOUND_FRAME_SAMPLES); };
  // How sharp the sound's edges are (its treble): the squared steps from sample to sample.
  auto edges = [](const std::vector<int8_t> &v, std::pair<size_t, size_t> r) {
    long sum = 0;
    for (size_t i = r.first + 1; i < r.second; i++) sum += long(v[i] - v[i - 1]) * (v[i] - v[i - 1]);
    return sum;
  };
  // Dry: the song's echo is gone.
  BmSoundRoom dry = {1, 0, 0, 0, 0, 1};   // (the music through the room: echo_music)
  Output out = run(&dry, 20);
  TEST_ASSERT(peak(out.left, range(0, 1).first, range(0, 1).second) > 80);
  TEST_ASSERT(peak(out.left, range(5, 9).first, range(5, 9).second) == 0);
  // An echo of the room's own (50 ms: after about 3 frames), plain and damped.
  BmSoundRoom plain = {1, uint16_t(50 * BM_SOUND_RATE / 1000), 128, 0, 0, 1};
  BmSoundRoom cave = plain;
  cave.damping = 200;
  Output a = run(&plain, 20), b = run(&cave, 20);
  auto echoFrames = range(4, 6);   // the note is over by frame 4
  TEST_ASSERT(peak(a.left, echoFrames.first, echoFrames.second) > 30);
  TEST_ASSERT(peak(b.left, echoFrames.first, echoFrames.second) > 5);
  TEST_ASSERT(edges(b.left, echoFrames) * 3 < edges(a.left, echoFrames));   // duller
  TEST_ASSERT(edges(a.left, range(0, 1)) == edges(b.left, range(0, 1)));     // the note itself as it was
  // Muffled: the note itself loses its edges, but is there.
  BmSoundRoom water = {1, 0, 0, 0, 200, 1};
  Output m = run(&water, 20), d = run(&dry, 20);
  TEST_ASSERT(peak(m.left, range(0, 1).first, range(0, 1).second) > 20);
  TEST_ASSERT(edges(m.left, range(0, 1)) * 3 < edges(d.left, range(0, 1)));
  // Everything dies away.
  for (const BmSoundRoom *r : {&cave, &water})
  {
    Output t = run(r, 600);
    TEST_ASSERT(peak(t.left, t.left.size() - 20 * BM_SOUND_FRAME_SAMPLES, t.left.size()) <= 1);
  }
  // Off again: the song's echo (100 ms, after about 6 frames) as before.
  out = run(nullptr, 20);
  TEST_ASSERT(peak(out.left, range(7, 8).first, range(7, 8).second) > 30);
}

// The effects apart from the music: the song's echo is the music's only, a room's acoustics
// the effects' (unless they take the music too, above); without memory for the effects' echo
// (the multiboot images) they are dry.
static void testEffectsApart()
{
  song::Song quiet = oneNote("pulse50", 60, 3);   // the music: a song with an echo, and silence
  quiet.echoMs = 100;
  quiet.echoFeedback = 128;
  quiet.loopStart = 0;
  quiet.tracks[0].notes.clear();
  song::Song tune = quiet;                       // the music with a note
  tune.tracks[0].notes.push_back({0, 60, 3, 127});
  song::Song fx = oneNote("pulse50", 72, 3);
  fx.effect = true;
  std::vector<uint8_t> quietData = cook(quiet), tuneData = cook(tune), fxData = cook(fx);
  static int8_t echo[2 * BM_SOUND_ECHO_MAX], fxEcho[2 * BM_SOUND_ECHO_MAX + 2 * BM_SOUND_FRAME_SAMPLES];
  auto range = [](int f0, int f1) { return std::make_pair(size_t(f0) * BM_SOUND_FRAME_SAMPLES, size_t(f1) * BM_SOUND_FRAME_SAMPLES); };
  auto run = [&](const std::vector<uint8_t> &music, bool effect, const BmSoundRoom *pRoom, int8_t *pFxEcho) {
    BmSound sound;
    bmSoundInit(&sound, nullptr, echo);
    bmSoundSetEffectEcho(&sound, pFxEcho);
    bmSoundSetRoom(&sound, pRoom);
    bmSoundPlayData(&sound, music.data(), uint32_t(music.size()), false);
    if (effect) bmSoundPlayData(&sound, fxData.data(), uint32_t(fxData.size()), true);
    return play(sound, 20);
  };
  auto loud = [&](const Output &o, int f0, int f1) { return peak(o.left, range(f0, f1).first, range(f0, f1).second); };
  // No room acoustics: the effect is dry (nothing after it), the music's note echoes (100 ms).
  Output e = run(quietData, true, nullptr, fxEcho), m = run(tuneData, false, nullptr, fxEcho);
  TEST_ASSERT(loud(e, 0, 1) > 60 && loud(e, 5, 20) == 0);
  TEST_ASSERT(loud(m, 0, 1) > 60 && loud(m, 7, 8) > 30);
  // A room with an echo (50 ms) for the effects: the effect rings in it, the music keeps its
  // song's echo (its note's echo after 100 ms, none after 50).
  BmSoundRoom hall = {1, uint16_t(50 * BM_SOUND_RATE / 1000), 128, 0, 0, 0};
  e = run(quietData, true, &hall, fxEcho);
  m = run(tuneData, false, &hall, fxEcho);
  TEST_ASSERT(loud(e, 4, 6) > 30);
  TEST_ASSERT(loud(m, 4, 5) == 0 && loud(m, 7, 8) > 30);
  // Without memory for the effects' echo: dry there too.
  e = run(quietData, true, &hall, nullptr);
  TEST_ASSERT(loud(e, 0, 1) > 60 && loud(e, 5, 20) == 0);
}

static void testSampleAndDeterminism()
{
  // The test tune with its kick sample: the same samples every time, and not silence.
  song::Song s = song::load(s_assets + "/test_room/music/castle.bmsong");
  song::Cost cost;
  std::vector<uint8_t> data = cook(s, &cost);
  TEST_ASSERT(!data.empty());
  TEST_ASSERT(cost.sampleBytes == 1600);
  TEST_ASSERT(cost.polyphony <= BM_SOUND_VOICES);
  static int8_t echo[2 * BM_SOUND_ECHO_MAX];
  uint32_t check[2];
  for (int run = 0; run < 2; run++)
  {
    BmSound sound;
    bmSoundInit(&sound, nullptr, echo);
    TEST_ASSERT(bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false));
    Output out = play(sound, 600);
    check[run] = sound.outputCrc;
    TEST_ASSERT(peak(out.left) > 60 && peak(out.right) > 60);
    TEST_ASSERT(sound.voicesPeak >= 4 && sound.voicesPeak <= BM_SOUND_VOICES);
    TEST_ASSERT(sound.players[0].playing);                    // it loops
  }
  TEST_ASSERT(check[0] == check[1] && check[0] != 0);

  // The file written back reads the same.
  s.path = "/tmp/blutmond-test-song.bmsong";
  s.instruments[3].sample = s_assets + "/test_room/music/kick.wav";
  TEST_ASSERT(song::save(s));
  song::Song t = song::load(s.path);
  TEST_ASSERT(t.tracks.size() == s.tracks.size() && t.tracks[0].notes.size() == s.tracks[0].notes.size());
  TEST_ASSERT(t.instruments[0].decay == s.instruments[0].decay && t.echoMs == s.echoMs && t.bpm == s.bpm);
  TEST_ASSERT(cook(t) == data);
  std::remove(s.path.c_str());
}

static void testModes()
{
  // Two modes of one song as layers: layer 1 heard at intensity 0 only, layer 2 from 1 on; a
  // note on each every beat. Starts at 0 and fades over 60 frames.
  song::Song s = oneNote("sine", 60, 20);
  s.loopStart = 0;
  s.intensity = 0;
  s.fade = 60;
  s.layers.push_back({1, "calm", 0, 0});
  s.layers.push_back({2, "chase", 1, 15});
  s.instruments[0].layer = 1;
  song::Instrument b = s.instruments[0];
  b.name = "b";
  b.layer = 2;
  s.instruments.push_back(b);
  s.tracks[0].notes.clear();
  song::Track tb;
  tb.instrument = 1;
  for (int t = 0; t < s.length; t += 24)
  {
    s.tracks[0].notes.push_back({t, 60, 20, 127});
    tb.notes.push_back({t, 72, 20, 127});
  }
  s.tracks.push_back(tb);
  std::vector<uint8_t> data = cook(s);
  TEST_ASSERT(!data.empty());
  uint16_t flags = uint16_t(data[2] | data[3] << 8);
  TEST_ASSERT(BM_SOUND_SONG_INTENSITY(flags) == 0 && (flags & BM_SOUND_SONG_LAYERS));
  BmSound sound;
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundSetIntensity(&sound, BM_SOUND_SONG_INTENSITY(flags));
  TEST_ASSERT(bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false));
  TEST_ASSERT(sound.layerVolume[1] == BM_SOUND_LAYER_FULL && sound.layerVolume[2] == 0);   // no fade at the start
  TEST_ASSERT(sound.layerVolume[0] == BM_SOUND_LAYER_FULL && sound.layerVolume[3] == 0);   // the others as by default
  play(sound, 40);
  TEST_ASSERT(sound.voicesPeak == 1);                         // the other mode's notes take no voice
  // The other mode: the one fades out while the other fades in, over the song's 60 frames.
  bmSoundSetIntensity(&sound, 1);
  play(sound, 30);
  TEST_ASSERT(sound.layerVolume[1] > 1500 && sound.layerVolume[1] < 2600);
  TEST_ASSERT(sound.layerVolume[2] > 1500 && sound.layerVolume[2] < 2600);
  TEST_ASSERT(sound.voicesPeak == 2);                         // both while they cross
  play(sound, 31);
  TEST_ASSERT(sound.layerVolume[1] == 0 && sound.layerVolume[2] == BM_SOUND_LAYER_FULL);
  // Without a table and a fade the layers work as before: n heard from n on, in 43 frames.
  song::Song old = s;
  old.fade = 0;
  old.layers.clear();
  data = cook(old);
  TEST_ASSERT(!(uint16_t(data[2] | data[3] << 8) & BM_SOUND_SONG_LAYERS));
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundSetIntensity(&sound, 0);
  TEST_ASSERT(bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false));
  TEST_ASSERT(sound.layerVolume[0] == BM_SOUND_LAYER_FULL && sound.layerVolume[1] == 0);
  bmSoundSetIntensity(&sound, 1);
  play(sound, 42);
  TEST_ASSERT(sound.layerVolume[1] == 42 * BM_SOUND_LAYER_STEP && sound.layerVolume[2] == 0);
  play(sound, 1);
  TEST_ASSERT(sound.layerVolume[1] == BM_SOUND_LAYER_FULL);
  // A range that ends below its start, or a layer twice, is refused; the file keeps the layers.
  Report report;
  song::Song bad = s;
  bad.layers[1].until = 0;
  TEST_ASSERT(song::chunk(bad, 0, report).empty());
  bad = s;
  bad.layers[1].id = 1;
  TEST_ASSERT(song::chunk(bad, 0, report).empty());
  s.path = "/tmp/blutmond-test-modes.bmsong";
  TEST_ASSERT(song::save(s));
  song::Song t = song::load(s.path);
  TEST_ASSERT(t.intensity == 0 && t.fade == 60 && t.layers.size() == 2 && t.layers[0].name == "calm" && t.layers[1].from == 1);
  TEST_ASSERT(t.instruments[0].layer == 1 && t.instruments[1].layer == 2 && cook(t) == cook(s));
  std::remove(s.path.c_str());
}

static void testBrokenSongsAreRefused()
{
  std::vector<uint8_t> data = cook(oneNote("sine", 69, 100));
  TEST_ASSERT(bmSoundSongValid(data.data(), uint32_t(data.size())));
  TEST_ASSERT(!bmSoundSongValid(data.data(), 20));
  std::vector<uint8_t> bad = data;
  bad[BM_SOUND_SONG_HEADER] = 77;                             // an unknown wave
  TEST_ASSERT(!bmSoundSongValid(bad.data(), uint32_t(bad.size())));
  bad = data;
  bad[BM_SOUND_SONG_HEADER + BM_SOUND_INSTRUMENT_SIZE + 6] = 9;  // an event with an instrument that is not there
  TEST_ASSERT(!bmSoundSongValid(bad.data(), uint32_t(bad.size())));
  bad = data;
  bad[8] = 0xFF;
  bad[9] = 0xFF;                                              // more events than the chunk holds
  TEST_ASSERT(!bmSoundSongValid(bad.data(), uint32_t(bad.size())));
  BmSound sound;
  bmSoundInit(&sound, nullptr, nullptr);
  TEST_ASSERT(!bmSoundPlayData(&sound, bad.data(), uint32_t(bad.size()), false));
  // Random damage: refused or played without leaving the chunk (the sanitizer build checks).
  uint32_t seed = 7;
  auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
  std::vector<uint8_t> tune = cook(song::load(s_assets + "/test_room/music/castle.bmsong"));
  for (int round = 0; round < 300; round++)
  {
    std::vector<uint8_t> d = tune;
    for (int k = 0; k < 3; k++) d[rnd() % 200] = uint8_t(rnd());
    bmSoundInit(&sound, nullptr, nullptr);
    if (bmSoundPlayData(&sound, d.data(), uint32_t(d.size()), false)) play(sound, 20);
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  s_assets = argv[1];
  RUN_TEST(testPitch);
  RUN_TEST(testEnvelopeAndRelease);
  RUN_TEST(testPanAndVolume);
  RUN_TEST(testLoopAndEnd);
  RUN_TEST(testVoicesAndEffects);
  RUN_TEST(testEcho);
  RUN_TEST(testRoomAcoustics);
  RUN_TEST(testEffectsApart);
  RUN_TEST(testSampleAndDeterminism);
  RUN_TEST(testModes);
  RUN_TEST(testBrokenSongsAreRefused);
  return TEST_RESULT();
}
