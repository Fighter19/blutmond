// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "song_asset.h"

#include "xml.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

extern "C" {
#include "sound.h"
}

namespace fs = std::filesystem;

namespace bm::song {

namespace {

int toInt(const xml::Element *e, const char *key, int fallback)
{
  return e->has(key) ? std::stoi(e->attr(key)) : fallback;
}

struct Wav
{
  int rate = 0;
  std::vector<int8_t> samples;   // signed 8-bit mono
};

uint32_t le(const std::vector<uint8_t> &d, size_t at, int bytes)
{
  uint32_t v = 0;
  for (int i = 0; i < bytes; i++) v |= uint32_t(d[at + size_t(i)]) << (8 * i);
  return v;
}

// PCM WAV, 8 or 16 bit, any number of channels (mixed down to one).
Wav loadWav(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
    throw std::runtime_error(path + ": not a WAV file");
  Wav w;
  int channels = 0, bits = 0, format = 0;
  for (size_t at = 12; at + 8 <= d.size();)
  {
    uint32_t size = le(d, at + 4, 4);
    size_t body = at + 8;
    if (body + size > d.size()) size = uint32_t(d.size() - body);
    if (std::memcmp(d.data() + at, "fmt ", 4) == 0 && size >= 16)
    {
      format = int(le(d, body, 2));
      channels = int(le(d, body + 2, 2));
      w.rate = int(le(d, body + 4, 4));
      bits = int(le(d, body + 14, 2));
    }
    else if (std::memcmp(d.data() + at, "data", 4) == 0)
    {
      if (format != 1 || channels < 1 || (bits != 8 && bits != 16)) throw std::runtime_error(path + ": only uncompressed 8 or 16 bit WAV files are supported");
      size_t frame = size_t(channels) * size_t(bits / 8);
      for (size_t i = 0; i + frame <= size; i += frame)
      {
        int sum = 0;
        for (int c = 0; c < channels; c++)
        {
          size_t p = body + i + size_t(c) * size_t(bits / 8);
          sum += bits == 8 ? int(d[p]) - 128 : int(int16_t(le(d, p, 2))) / 256;
        }
        w.samples.push_back(int8_t(std::clamp(sum / channels, -128, 127)));
      }
    }
    at = body + size + (size & 1);
  }
  if (w.samples.empty() || w.rate <= 0) throw std::runtime_error(path + ": no sound data");
  return w;
}

}  // namespace

const std::vector<std::string> &waveNames()
{
  static const std::vector<std::string> names = {"pulse12", "pulse25", "pulse50", "saw", "triangle", "sine", "noise"};
  return names;
}

Song load(const std::string &path)
{
  Song s;
  s.path = path;
  s.name = fs::path(path).stem().string();
  auto root = xml::parseFile(path);
  if (root->name != "song") throw std::runtime_error(path + ": not a song file");
  if (root->attr("version", "1") != "1") throw std::runtime_error(path + ": song version " + root->attr("version") + " is not supported");
  s.bpm = toInt(root.get(), "bpm", 120);
  s.length = toInt(root.get(), "length", 384);
  s.loopStart = toInt(root.get(), "loop", 0);
  s.effect = root->attr("effect", "0") == "1";
  s.echoMs = toInt(root.get(), "echo", 0);
  s.echoFeedback = toInt(root.get(), "echo-feedback", 80);
  for (const xml::Element *e : root->childrenNamed("instrument"))
  {
    Instrument in;
    in.name = e->attr("name");
    in.wave = e->attr("wave", "pulse50");
    in.sample = e->attr("sample");
    in.root = toInt(e, "root", 60);
    in.loop = e->attr("loop", "0") == "1";
    in.attack = toInt(e, "attack", 0);
    in.decay = toInt(e, "decay", 0);
    in.sustain = toInt(e, "sustain", 255);
    in.release = toInt(e, "release", 4);
    in.volume = toInt(e, "volume", 200);
    in.pan = toInt(e, "pan", 0);
    in.layer = toInt(e, "layer", 0);
    s.instruments.push_back(in);
  }
  for (const xml::Element *t : root->childrenNamed("track"))
  {
    Track tr;
    tr.name = t->attr("name");
    std::string inst = t->attr("instrument");
    tr.instrument = 0;
    for (size_t i = 0; i < s.instruments.size(); i++)
      if (s.instruments[i].name == inst) tr.instrument = int(i);
    for (const xml::Element *n : t->childrenNamed("note"))
      tr.notes.push_back({toInt(n, "t", 0), toInt(n, "key", 60), toInt(n, "len", 24), toInt(n, "vel", 100)});
    s.tracks.push_back(tr);
  }
  return s;
}

bool save(const Song &s, std::string *pError)
{
  std::ofstream out(s.path, std::ios::binary);
  if (!out)
  {
    if (pError) *pError = "cannot write " + s.path;
    return false;
  }
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song version=\"1\" bpm=\"" << s.bpm << "\" length=\"" << s.length << "\" loop=\"" << s.loopStart
      << "\" effect=\"" << (s.effect ? 1 : 0) << "\" echo=\"" << s.echoMs << "\" echo-feedback=\"" << s.echoFeedback << "\">\n";
  for (const Instrument &in : s.instruments)
  {
    out << " <instrument name=\"" << in.name << "\" wave=\"" << in.wave << "\"";
    if (in.wave == "sample") out << " sample=\"" << in.sample << "\" root=\"" << in.root << "\" loop=\"" << (in.loop ? 1 : 0) << "\"";
    out << " attack=\"" << in.attack << "\" decay=\"" << in.decay << "\" sustain=\"" << in.sustain << "\" release=\"" << in.release
        << "\" volume=\"" << in.volume << "\" pan=\"" << in.pan << "\"";
    if (in.layer != 0) out << " layer=\"" << in.layer << "\"";
    out << "/>\n";
  }
  for (const Track &t : s.tracks)
  {
    out << " <track name=\"" << t.name << "\" instrument=\""
        << (t.instrument >= 0 && size_t(t.instrument) < s.instruments.size() ? s.instruments[size_t(t.instrument)].name : std::string()) << "\">\n";
    std::vector<Note> notes = t.notes;
    std::stable_sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) { return a.tick < b.tick; });
    for (const Note &n : notes) out << "  <note t=\"" << n.tick << "\" key=\"" << n.key << "\" len=\"" << n.length << "\" vel=\"" << n.velocity << "\"/>\n";
    out << " </track>\n";
  }
  out << "</song>\n";
  return bool(out);
}

std::vector<uint8_t> chunk(const Song &s, uint16_t nameId, Report &report, Cost *pCost)
{
  std::vector<uint8_t> b;
  auto u8 = [&](int v) { b.push_back(uint8_t(v)); };
  auto u16 = [&](uint32_t v) { u8(int(v)); u8(int(v >> 8)); };
  auto u32 = [&](uint32_t v) { u16(v); u16(v >> 16); };
  auto fail = [&](const std::string &why) {
    report.error("does-not-fit", s.path, why);
    return std::vector<uint8_t>();
  };
  if (s.bpm < 20 || s.bpm > 400) return fail("the tempo must be between 20 and 400 beats per minute");
  if (s.length <= 0) return fail("the song has no length");
  if (s.instruments.empty()) return fail("the song has no instrument");
  if (s.instruments.size() > 255) return fail("more than 255 instruments");

  // Events of all tracks in time order.
  struct Event
  {
    int tick, length, instrument, key, velocity;
  };
  std::vector<Event> events;
  for (const Track &t : s.tracks)
  {
    if (t.instrument < 0 || size_t(t.instrument) >= s.instruments.size()) return fail("track '" + t.name + "' has no instrument");
    for (const Note &n : t.notes)
    {
      if (n.tick < 0 || n.tick >= s.length) continue;  // outside the song
      events.push_back({n.tick, std::clamp(n.length, 1, 65535), t.instrument, std::clamp(n.key, 0, 127), std::clamp(n.velocity, 1, 127)});
    }
  }
  std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &c) { return a.tick < c.tick; });

  // Samples: 8-bit data after the tables, one copy per file.
  struct Sample
  {
    std::string file;
    Wav wav;
    uint32_t offset = 0;
  };
  std::vector<Sample> samples;
  std::vector<int> sampleOf(s.instruments.size(), -1);
  for (size_t i = 0; i < s.instruments.size(); i++)
  {
    const Instrument &in = s.instruments[i];
    if (in.wave != "sample")
    {
      if (std::find(waveNames().begin(), waveNames().end(), in.wave) == waveNames().end()) return fail("instrument '" + in.name + "': unknown wave '" + in.wave + "'");
      continue;
    }
    std::string file = (fs::path(s.path).parent_path() / in.sample).lexically_normal().string();
    for (size_t k = 0; k < samples.size(); k++)
      if (samples[k].file == file) sampleOf[i] = int(k);
    if (sampleOf[i] >= 0) continue;
    try
    {
      samples.push_back({file, loadWav(file), 0});
      sampleOf[i] = int(samples.size()) - 1;
    }
    catch (const std::exception &e)
    {
      return fail("instrument '" + in.name + "': " + e.what());
    }
    if (samples.back().wav.samples.size() >= (1u << 20)) return fail(file + " is too long (at most 1 048 575 samples)");
  }

  const uint32_t header = BM_SOUND_SONG_HEADER;
  uint32_t instrumentsAt = header, eventsAt = instrumentsAt + uint32_t(s.instruments.size()) * BM_SOUND_INSTRUMENT_SIZE;
  uint32_t samplesAt = eventsAt + uint32_t(events.size()) * BM_SOUND_EVENT_SIZE;
  uint32_t at = samplesAt;
  for (Sample &sm : samples)
  {
    sm.offset = at;
    at += uint32_t(sm.wav.samples.size());
  }

  // Frames per minute on the GBA: 59.7275 * 60.
  uint32_t tickRate = uint32_t(std::lround(double(s.bpm) * BM_SOUND_TICKS_PER_BEAT * 65536.0 / 3583.65));
  u16(nameId);
  u16(s.effect ? BM_SOUND_SONG_SFX : 0);
  u16(uint32_t(s.instruments.size()));
  u16(uint32_t(s.tracks.size()));
  u32(uint32_t(events.size()));
  u32(uint32_t(s.length));
  u32(s.effect || s.loopStart < 0 || s.loopStart >= s.length ? 0xFFFFFFFFu : uint32_t(s.loopStart));
  u32(tickRate);
  u16(uint32_t(std::clamp(s.echoMs * BM_SOUND_RATE / 1000, 0, BM_SOUND_ECHO_MAX)));
  u8(std::clamp(s.echoFeedback, 0, 255));
  u8(0);
  u32(instrumentsAt);
  u32(eventsAt);
  u32(samplesAt);

  for (size_t i = 0; i < s.instruments.size(); i++)
  {
    const Instrument &in = s.instruments[i];
    int wave = BM_WAVE_SAMPLE;
    for (size_t k = 0; k < waveNames().size(); k++)
      if (waveNames()[k] == in.wave) wave = int(k);
    int sustain = std::clamp(in.sustain, 0, 255);
    u8(wave);
    u8(std::clamp(in.volume, 0, 255));
    u8(std::clamp(in.pan, -64, 63));
    u8(sustain);
    // Level changes per frame; the level runs from 0 to 65535. 0 means at once.
    u16(in.attack <= 0 ? 0 : uint32_t(std::max(1, 65535 / in.attack)));
    u16(in.decay <= 0 ? 0 : uint32_t(std::max(1, (65535 - sustain * 256) / in.decay)));
    u16(in.release <= 0 ? 0 : uint32_t(std::max(1, 65535 / in.release)));
    u16(uint32_t(std::clamp(in.layer, 0, 15)));   // the layer (sound-format.md, dynamic music)
    if (sampleOf[i] >= 0)
    {
      const Sample &sm = samples[size_t(sampleOf[i])];
      u32(sm.offset);
      u32(uint32_t(sm.wav.samples.size()));
      u32(in.loop ? 0 : 0xFFFFFFFFu);
      // The step at key 60: the sample's own rate at its root key.
      double step = double(sm.wav.rate) / BM_SOUND_RATE * 4096.0 * std::pow(2.0, (60 - in.root) / 12.0);
      u32(uint32_t(std::lround(step)));
    }
    else
    {
      u32(0);
      u32(0);
      u32(0xFFFFFFFFu);
      u32(0);
    }
  }
  for (const Event &e : events)
  {
    u32(uint32_t(e.tick));
    u16(uint32_t(e.length));
    u8(e.instrument);
    u8(e.key);
    u8(e.velocity);
    u8(0);
    u16(0);
  }
  size_t sampleBytes = 0;
  for (const Sample &sm : samples)
  {
    for (int8_t v : sm.wav.samples) u8(v);
    sampleBytes += sm.wav.samples.size();
  }
  while (b.size() % 4) u8(0);

  // Most notes sounding at once, counting each note's release.
  int polyphony = 0;
  double framesPerTick = 65536.0 / double(tickRate);
  for (const Event &e : events)
  {
    int sounding = 0;
    for (const Event &o : events)
    {
      double end = o.tick + o.length + s.instruments[size_t(o.instrument)].release / framesPerTick;
      if (o.tick <= e.tick && end > e.tick) sounding++;
    }
    polyphony = std::max(polyphony, sounding);
  }
  if (polyphony > BM_SOUND_VOICES)
    report.warning("sound-reduced", s.path,
                   "up to " + std::to_string(polyphony) + " notes sound at once; the engine has " + std::to_string(BM_SOUND_VOICES) +
                       " voices, so the oldest notes are cut");
  report.info("song", s.path,
              std::to_string(events.size()) + " notes, " + std::to_string(s.instruments.size()) + " instruments, " + std::to_string(b.size()) +
                  " bytes (" + std::to_string(sampleBytes) + " of them samples), up to " + std::to_string(polyphony) + " voices");
  if (pCost) *pCost = {int(b.size()), int(sampleBytes), int(events.size()), polyphony};
  if (!bmSoundSongValid(b.data(), uint32_t(b.size()))) return fail("internal error: the cooked song is not valid");
  return b;
}

}  // namespace bm::song
