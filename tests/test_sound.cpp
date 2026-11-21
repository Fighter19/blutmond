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
  // Without memory for it there is no echo.
  bmSoundInit(&sound, nullptr, nullptr);
  bmSoundPlayData(&sound, data.data(), uint32_t(data.size()), false);
  out = play(sound, 30);
  TEST_ASSERT(frame(7) == 0);
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
  RUN_TEST(testSampleAndDeterminism);
  RUN_TEST(testBrokenSongsAreRefused);
  return TEST_RESULT();
}
