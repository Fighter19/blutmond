// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "sound_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "dialogs.h"
#include "ui/canvas.h"
#include "ui/context.h"
#include "ui/lists.h"
#include "ui/loader.h"
#include "ui/menus.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;
using namespace ose::ui;
using ose::Vec2;

namespace bm::editor {

namespace {

constexpr float kFrameSeconds = 280896.0f / 16777216.0f;
constexpr int kLowKey = 24, kHighKey = 107;

std::string fileName(const std::string &path)
{
  return fs::path(path).filename().string();
}

bool isBlackKey(int key)
{
  int n = key % 12;
  return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

std::string keyName(int key)
{
  static const char *const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return std::string(names[key % 12]) + std::to_string(key / 12 - 1);
}

uint32_t rd32(const uint8_t *p)
{
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

}  // namespace

// --- PianoRoll ---------------------------------------------------------------------------

int PianoRoll::topKey() const
{
  return std::clamp(scrollKey_, kLowKey + int(rect_.h / rowHeight()) - 1, kHighKey);
}

int PianoRoll::tickAt(float x) const
{
  return int((x - rect_.x - keyWidth() + scrollX_) / tickWidth());
}

int PianoRoll::keyAt(float y) const
{
  return topKey() - int((y - rect_.y) / rowHeight());
}

int PianoRoll::noteAt(Vec2 p, bool *pOnEnd) const
{
  if (!song || track < 0 || size_t(track) >= song->tracks.size() || p.x < rect_.x + keyWidth()) return -1;
  int key = keyAt(p.y);
  float x = p.x - rect_.x - keyWidth() + scrollX_;
  const auto &notes = song->tracks[size_t(track)].notes;
  for (size_t i = notes.size(); i-- > 0;)
  {
    float x0 = float(notes[i].tick) * tickWidth(), x1 = float(notes[i].tick + notes[i].length) * tickWidth();
    if (notes[i].key == key && x >= x0 && x < x1)
    {
      if (pOnEnd) *pOnEnd = x >= x1 - 5 && x1 - x0 > 8;
      return int(i);
    }
  }
  return -1;
}

bool PianoRoll::event(const InputEvent &e)
{
  using T = InputEvent::Type;
  if (!song || track < 0 || size_t(track) >= song->tracks.size()) return false;
  auto &notes = song->tracks[size_t(track)].notes;
  if (e.type == T::Wheel)
  {
    if (e.mods & Mod::Shift) scrollX_ = std::max(0.0f, scrollX_ - e.delta.y * 48);
    else scrollKey_ = std::clamp(topKey() + int(e.delta.y * 3), kLowKey, kHighKey);
    return true;
  }
  if (e.type == T::MouseDown && e.button == MouseButton::Right)
  {
    int n = noteAt(e.pos, nullptr);
    if (n < 0) return true;
    notes.erase(notes.begin() + n);
    selected = -1;
    if (selectionChanged) selectionChanged();
    if (edited) edited();
    return true;
  }
  if (e.type == T::MouseDown && e.button == MouseButton::Left)
  {
    if (e.pos.x < rect_.x + keyWidth())
    {
      // The keyboard on the left plays the key.
      if (audition) audition(keyAt(e.pos.y));
      return true;
    }
    bool onEnd = false;
    int n = noteAt(e.pos, &onEnd);
    if (n >= 0)
    {
      selected = n;
      dragging_ = onEnd ? 2 : 1;
      dragTickOffset_ = tickAt(e.pos.x) - notes[size_t(n)].tick;
      if (audition) audition(notes[size_t(n)].key);
    }
    else
    {
      int tick = std::max(0, tickAt(e.pos.x) / grid * grid), key = std::clamp(keyAt(e.pos.y), 0, 127);
      if (tick >= song->length) return true;
      notes.push_back({tick, key, newLength, 100});
      selected = int(notes.size()) - 1;
      dragging_ = 1;
      dragTickOffset_ = 0;
      if (audition) audition(key);
      if (edited) edited();
    }
    if (selectionChanged) selectionChanged();
    return true;
  }
  if (e.type == T::MouseMove && dragging_ && selected >= 0 && size_t(selected) < notes.size())
  {
    song::Note &note = notes[size_t(selected)];
    song::Note before = note;
    if (dragging_ == 1)
    {
      int tick = (tickAt(e.pos.x) - dragTickOffset_ + grid / 2) / grid * grid;
      note.tick = std::clamp(tick, 0, std::max(0, song->length - grid));
      note.key = std::clamp(keyAt(e.pos.y), kLowKey, kHighKey);
    }
    else
    {
      int end = (tickAt(e.pos.x) + grid / 2) / grid * grid;
      note.length = std::max(grid / 2, end - note.tick);
    }
    if (before.tick != note.tick || before.key != note.key || before.length != note.length)
    {
      if (before.key != note.key && audition) audition(note.key);
      if (selectionChanged) selectionChanged();
      if (edited) edited();
    }
    return true;
  }
  if (e.type == T::MouseUp && dragging_)
  {
    dragging_ = 0;
    return true;
  }
  if (e.type == T::KeyDown && (e.key == "delete" || e.key == "backspace") && selected >= 0 && size_t(selected) < notes.size())
  {
    notes.erase(notes.begin() + selected);
    selected = -1;
    if (selectionChanged) selectionChanged();
    if (edited) edited();
    return true;
  }
  return false;
}

void PianoRoll::draw(Canvas &c)
{
  c.rect(rect_, parseColor("#e8e8e8"));
  if (!song) return;
  float kw = keyWidth(), tw = tickWidth(), rh = rowHeight();
  int top = topKey(), rows = int(rect_.h / rh) + 1;
  float gx = rect_.x + kw;
  c.pushClip(rect_);

  // Rows: black keys darker, a line at every C.
  for (int r = 0; r < rows; r++)
  {
    int key = top - r;
    if (key < kLowKey) break;
    float y = rect_.y + float(r) * rh;
    if (isBlackKey(key)) c.rect({gx, y, rect_.w - kw, rh}, parseColor("#d8d8d8"));
    if (key % 12 == 0) c.rect({gx, y + rh - 1, rect_.w - kw, 1}, parseColor("#b0b0b0"));
  }
  // Columns: sixteenths faint, beats stronger, bars strongest; grey after the song's end.
  int firstTick = int(scrollX_ / tw) / 6 * 6;
  for (int tick = firstTick; float(tick) * tw - scrollX_ < rect_.w - kw; tick += 6)
  {
    float x = gx + float(tick) * tw - scrollX_;
    if (x < gx) continue;
    bool bar = tick % 96 == 0, beat = tick % 24 == 0;
    c.rect({x, rect_.y, 1, rect_.h}, bar ? parseColor("#808080") : beat ? parseColor("#b4b4b4") : parseColor("#d4d4d4"));
    if (bar) c.text({x + 3, rect_.y + 1}, std::to_string(tick / 96 + 1), font(), fontSize() * 0.8f, parseColor("#606060"));
  }
  float endX = gx + float(song->length) * tw - scrollX_;
  if (endX < rect_.right()) c.rect({std::max(gx, endX), rect_.y, rect_.right() - std::max(gx, endX), rect_.h}, {0, 0, 0, 0.25f});
  if (song->loopStart >= 0 && !song->effect)
  {
    float x = gx + float(song->loopStart) * tw - scrollX_;
    if (x >= gx) c.rect({x, rect_.y, 2, rect_.h}, parseColor("#2080d0"));
  }

  // Notes: other tracks pale, this one solid.
  for (size_t t = 0; t < song->tracks.size(); t++)
  {
    bool mine = int(t) == track;
    if (mine) continue;
    for (const song::Note &n : song->tracks[t].notes)
    {
      float y = rect_.y + float(top - n.key) * rh, x = gx + float(n.tick) * tw - scrollX_;
      if (x + float(n.length) * tw < gx) continue;
      c.rect({x, y + 1, std::max(2.0f, float(n.length) * tw - 1), rh - 2}, {0.45f, 0.5f, 0.6f, 0.35f});
    }
  }
  if (track >= 0 && size_t(track) < song->tracks.size())
  {
    const auto &notes = song->tracks[size_t(track)].notes;
    for (size_t i = 0; i < notes.size(); i++)
    {
      const song::Note &n = notes[i];
      float y = rect_.y + float(top - n.key) * rh, x = gx + float(n.tick) * tw - scrollX_;
      if (x + float(n.length) * tw < gx) continue;
      Rect r{x, y, std::max(3.0f, float(n.length) * tw - 1), rh - 1};
      // Louder notes are darker.
      float v = 0.75f - 0.45f * float(n.velocity) / 127.0f;
      c.rect(r, int(i) == selected ? Color{0.9f, 0.45f, 0.1f, 1} : Color{v * 0.4f, v * 0.7f, 0.2f + v, 1});
      c.border(r, {0, 0, 0, 0.5f}, 1);
    }
  }
  if (playTick >= 0)
  {
    float x = gx + float(playTick) * tw - scrollX_;
    if (x >= gx) c.rect({x, rect_.y, 2, rect_.h}, parseColor("#d02020"));
  }

  // The keyboard.
  c.rect({rect_.x, rect_.y, kw, rect_.h}, parseColor("#ffffff"));
  for (int r = 0; r < rows; r++)
  {
    int key = top - r;
    if (key < kLowKey) break;
    float y = rect_.y + float(r) * rh;
    if (isBlackKey(key)) c.rect({rect_.x, y, kw * 0.6f, rh}, parseColor("#303030"));
    c.rect({rect_.x, y + rh - 1, kw, 1}, parseColor("#c0c0c0"));
    if (key % 12 == 0) c.text({rect_.x + kw * 0.6f + 1, y - 2}, keyName(key), font(), fontSize() * 0.75f, parseColor("#404040"));
  }
  c.rect({gx - 1, rect_.y, 1, rect_.h}, parseColor("#606060"));
  c.popClip();
}

// --- SoundPanel --------------------------------------------------------------------------

SoundPanel::~SoundPanel()
{
  if (stream_) SDL_DestroyAudioStream(stream_);
}

song::Instrument *SoundPanel::instrument()
{
  if (!loaded_ || roll_->track < 0 || size_t(roll_->track) >= song_.tracks.size()) return nullptr;
  int i = song_.tracks[size_t(roll_->track)].instrument;
  return i >= 0 && size_t(i) < song_.instruments.size() ? &song_.instruments[size_t(i)] : nullptr;
}

void SoundPanel::bind(Control &root, Context &ui)
{
  ui_ = &ui;
  roll_ = root.findAs<PianoRoll>("snd_roll");
  chooser_ = root.findAs<OptionButton>("snd_song");
  wave_ = root.findAs<OptionButton>("snd_wave");
  sample_ = root.findAs<OptionButton>("snd_sample");
  noteLength_ = root.findAs<OptionButton>("snd_note_length");
  tracks_ = root.findAs<ItemList>("snd_tracks");
  trackName_ = root.findAs<LineEdit>("snd_track_name");
  play_ = root.findAs<Button>("snd_play");
  info_ = root.findAs<Label>("snd_info");
  voices_ = root.findAs<Label>("snd_voices");
  cpu_ = root.findAs<Label>("snd_cpu");
  size_ = root.findAs<Label>("snd_size");
  voicesBar_ = root.findAs<ProgressBar>("snd_voices_bar");
  cpuBar_ = root.findAs<ProgressBar>("snd_cpu_bar");
  sizeBar_ = root.findAs<ProgressBar>("snd_size_bar");
  loop_ = root.findAs<CheckBox>("snd_loop");
  effect_ = root.findAs<CheckBox>("snd_effect");
  sampleLoop_ = root.findAs<CheckBox>("snd_sample_loop");
  tracks_->tileSize = 0;
  echo_.assign(2 * BM_SOUND_ECHO_MAX, 0);
  bmSoundInit(&sound_, nullptr, echo_.data());

  auto number = [this, &root](const char *id, double lo, double hi, std::function<void(int)> set) {
    SpinBox *s = root.findAs<SpinBox>(id);
    s->minValue = lo;
    s->maxValue = hi;
    s->decimals = 0;
    s->valueChanged.connect([this, set](double v) {
      if (updating_ || !loaded_) return;
      set(int(v));
      edited();
    });
    return s;
  };
  bpm_ = number("snd_bpm", 20, 400, [this](int v) { song_.bpm = v; });
  bars_ = number("snd_bars", 1, 64, [this](int v) {
    song_.length = v * 96;
    if (song_.loopStart >= song_.length) song_.loopStart = 0;
  });
  echo_ms_ = number("snd_echo", 0, 225, [this](int v) { song_.echoMs = v; });
  feedback_ = number("snd_feedback", 0, 255, [this](int v) { song_.echoFeedback = v; });
  attack_ = number("snd_attack", 0, 600, [this](int v) { if (auto *i = instrument()) i->attack = v; });
  decay_ = number("snd_decay", 0, 600, [this](int v) { if (auto *i = instrument()) i->decay = v; });
  sustain_ = number("snd_sustain", 0, 255, [this](int v) { if (auto *i = instrument()) i->sustain = v; });
  release_ = number("snd_release", 0, 600, [this](int v) { if (auto *i = instrument()) i->release = v; });
  volume_ = number("snd_volume", 0, 255, [this](int v) { if (auto *i = instrument()) i->volume = v; });
  pan_ = number("snd_pan", -64, 63, [this](int v) { if (auto *i = instrument()) i->pan = v; });
  root_ = number("snd_root", 0, 127, [this](int v) { if (auto *i = instrument()) i->root = v; });
  auto note = [this]() -> song::Note * {
    if (!loaded_ || roll_->track < 0 || size_t(roll_->track) >= song_.tracks.size()) return nullptr;
    auto &notes = song_.tracks[size_t(roll_->track)].notes;
    return roll_->selected >= 0 && size_t(roll_->selected) < notes.size() ? &notes[size_t(roll_->selected)] : nullptr;
  };
  velocity_ = number("snd_velocity", 1, 127, [note](int v) { if (auto *n = note()) n->velocity = v; });
  length_ = number("snd_length", 1, 960, [note](int v) { if (auto *n = note()) n->length = v; });

  std::vector<std::string> waves = song::waveNames();
  waves.push_back("sample");
  wave_->setItems(waves);
  wave_->itemSelected.connect([this](int) {
    song::Instrument *in = instrument();
    if (updating_ || !in) return;
    in->wave = wave_->selectedText();
    if (in->wave == "sample" && in->sample.empty()) in->sample = sample_->selectedText();
    edited();
    showInstrument();
  });
  sample_->itemSelected.connect([this](int) {
    song::Instrument *in = instrument();
    if (updating_ || !in) return;
    in->sample = sample_->selectedText();
    edited();
  });
  noteLength_->setItems({"1/16", "1/8", "1/4", "1/2", "1 bar"});
  noteLength_->select(1, false);
  noteLength_->itemSelected.connect([this](int i) {
    static const int lengths[] = {6, 12, 24, 48, 96};
    roll_->newLength = lengths[std::clamp(i, 0, 4)];
  });
  loop_->toggled.connect([this](bool on) {
    if (updating_ || !loaded_) return;
    song_.loopStart = on ? 0 : -1;
    edited();
  });
  effect_->toggled.connect([this](bool on) {
    if (updating_ || !loaded_) return;
    song_.effect = on;
    edited();
  });
  sampleLoop_->toggled.connect([this](bool on) {
    song::Instrument *in = instrument();
    if (updating_ || !in) return;
    in->loop = on;
    edited();
  });
  chooser_->itemSelected.connect([this](int i) {
    if (i >= 0 && size_t(i) < paths_.size()) open(paths_[size_t(i)]);
  });
  tracks_->selectedChanged.connect([this](int i) {
    roll_->track = i;
    roll_->selected = -1;
    showInstrument();
    showNote();
  });
  trackName_->submitted.connect([this](const std::string &name) {
    song::Instrument *in = instrument();
    if (updating_ || !in || name.empty() || in->name == name) return;
    // The name must stay unique: tracks find their instrument by it.
    for (const song::Instrument &o : song_.instruments)
      if (&o != in && o.name == name) return;
    in->name = name;
    song_.tracks[size_t(roll_->track)].name = name;
    edited();
    fill();
  });
  play_->clicked.connect([this] { play(!playing_); });
  root.findAs<Button>("snd_test")->clicked.connect([this] {
    if (roll_->audition) roll_->audition(60);
  });
  root.findAs<Button>("snd_track_add")->clicked.connect([this] {
    if (!loaded_) return;
    song::Instrument in;
    int n = int(song_.instruments.size()) + 1;
    do in.name = "track" + std::to_string(n++);
    while (std::any_of(song_.instruments.begin(), song_.instruments.end(), [&](const song::Instrument &o) { return o.name == in.name; }));
    song_.instruments.push_back(in);
    song_.tracks.push_back({in.name, int(song_.instruments.size()) - 1, {}});
    roll_->track = int(song_.tracks.size()) - 1;
    roll_->selected = -1;
    edited();
    fill();
  });
  root.findAs<Button>("snd_track_remove")->clicked.connect([this] {
    if (!loaded_ || song_.tracks.size() <= 1 || roll_->track < 0 || size_t(roll_->track) >= song_.tracks.size()) return;
    int inst = song_.tracks[size_t(roll_->track)].instrument;
    song_.tracks.erase(song_.tracks.begin() + roll_->track);
    // Its instrument goes with it unless another track uses it.
    bool used = std::any_of(song_.tracks.begin(), song_.tracks.end(), [&](const song::Track &t) { return t.instrument == inst; });
    if (!used && inst >= 0 && size_t(inst) < song_.instruments.size())
    {
      song_.instruments.erase(song_.instruments.begin() + inst);
      for (song::Track &t : song_.tracks)
        if (t.instrument > inst) t.instrument--;
    }
    roll_->track = std::min(roll_->track, int(song_.tracks.size()) - 1);
    roll_->selected = -1;
    edited();
    fill();
  });

  roll_->edited = [this] { edited(); };
  roll_->selectionChanged = [this] { showNote(); };
  roll_->audition = [this](int key) {
    song::Instrument *in = instrument();
    if (!in || cooked_.empty()) return;
    bmSoundNote(&sound_, cooked_.data(), uint32_t(cooked_.size()), uint8_t(in - song_.instruments.data()), uint8_t(std::clamp(key, 0, 127)), 110, 15);
  };
}

void SoundPanel::setSongs(const std::vector<std::string> &paths)
{
  paths_ = paths;
  std::vector<std::string> names;
  int current = -1;
  for (size_t i = 0; i < paths.size(); i++)
  {
    names.push_back(fileName(paths[i]));
    if (paths[i] == song_.path) current = int(i);
  }
  chooser_->setItems(names);
  if (current >= 0) chooser_->select(current, false);
  else if (!paths.empty() && !loaded_) open(paths[0]);
  else if (paths.empty()) info_->text = "No songs yet: New song";
}

void SoundPanel::open(const std::string &path)
{
  if (dirty_) save();
  play(false);
  try
  {
    song_ = song::load(path);
    loaded_ = true;
  }
  catch (const std::exception &e)
  {
    loaded_ = false;
    song_ = song::Song{};
    info_->text = e.what();
    if (log) log(e.what(), true);
  }
  for (size_t i = 0; i < paths_.size(); i++)
    if (paths_[i] == path) chooser_->select(int(i), false);
  roll_->song = loaded_ ? &song_ : nullptr;
  roll_->track = loaded_ && !song_.tracks.empty() ? 0 : -1;
  roll_->selected = -1;
  recook();
  fill();
}

void SoundPanel::recook()
{
  // The engine reads the song data in place: it starts over on the new data, at the same place.
  bool wasPlaying = playing_;
  int which = song_.effect ? 1 : 0;
  uint32_t tick = sound_.players[which].tick;
  bmSoundInit(&sound_, nullptr, echo_.data());
  cooked_.clear();
  cost_ = song::Cost{};
  if (!loaded_) return;
  Report report;
  cooked_ = song::chunk(song_, 0, report, &cost_);
  std::string problem;
  for (const ReportEntry &e : report.entries)
    if (e.level != ReportEntry::Level::Info && problem.empty()) problem = e.message;
  info_->text = problem.empty() ? song_.path : problem;
  info_->colorOverride = problem.empty() || !cooked_.empty() ? Color{0, 0, 0, 0} : Color{0.8f, 0.1f, 0.1f, 1};
  playing_ = false;
  if (wasPlaying && !cooked_.empty() && bmSoundPlayData(&sound_, cooked_.data(), uint32_t(cooked_.size()), which == 1))
  {
    BmSoundPlayer &p = sound_.players[which];
    uint32_t length = rd32(cooked_.data() + 12), events = rd32(cooked_.data() + 8);
    if ((tick >> 16) < length)
    {
      p.tick = tick;
      const uint8_t *e = cooked_.data() + rd32(cooked_.data() + 32);
      while (p.nextEvent < events && rd32(e + p.nextEvent * BM_SOUND_EVENT_SIZE) < (tick >> 16)) p.nextEvent++;
    }
    playing_ = true;
  }

  // What it costs (docs/spec/sound-format.md, "Cost on the GBA").
  int voices = std::min(cost_.polyphony, BM_SOUND_VOICES);
  int cpu = 6 + (song_.echoMs > 0 && !song_.effect ? 4 : 0) + 2 * voices;
  voices_->text = "Voices: up to " + std::to_string(cost_.polyphony) + " of " + std::to_string(BM_SOUND_VOICES) +
                  (cost_.polyphony > BM_SOUND_VOICES ? " (the oldest notes are cut)" : "");
  voicesBar_->value = std::min(1.0f, float(cost_.polyphony) / BM_SOUND_VOICES);
  cpu_->text = "GBA processor: about " + std::to_string(cpu) + " % of a frame at the busiest moment";
  cpuBar_->value = std::min(1.0f, float(cpu) / 100.0f);
  size_->text = "Size: " + std::to_string(cost_.chunkBytes) + " bytes (" + std::to_string(cost_.sampleBytes) + " of them samples), " +
                std::to_string(cost_.events) + " notes";
  sizeBar_->value = std::min(1.0f, float(cost_.chunkBytes) / 65536.0f);
}

void SoundPanel::fill()
{
  updating_ = true;
  tracks_->items.clear();
  for (const song::Track &t : song_.tracks)
  {
    std::string wave = t.instrument >= 0 && size_t(t.instrument) < song_.instruments.size() ? song_.instruments[size_t(t.instrument)].wave : "?";
    tracks_->items.push_back({t.name + "  (" + wave + ", " + std::to_string(t.notes.size()) + " notes)", "music", {}, {}, {}, {}});
  }
  tracks_->selected = roll_->track;
  bpm_->setValue(song_.bpm, false);
  bars_->setValue(std::max(1, song_.length / 96), false);
  echo_ms_->setValue(song_.echoMs, false);
  feedback_->setValue(song_.echoFeedback, false);
  loop_->setPressed(song_.loopStart >= 0);
  effect_->setPressed(song_.effect);
  // The WAV files next to the song.
  std::vector<std::string> wavs;
  std::error_code ec;
  if (loaded_)
    for (fs::directory_iterator it(fs::path(song_.path).parent_path(), ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == ".wav") wavs.push_back(it->path().filename().string());
  std::sort(wavs.begin(), wavs.end());
  sample_->setItems(wavs);
  updating_ = false;
  showInstrument();
  showNote();
}

void SoundPanel::showInstrument()
{
  updating_ = true;
  song::Instrument *in = instrument();
  bool have = in != nullptr;
  for (SpinBox *s : {attack_, decay_, sustain_, release_, volume_, pan_, root_}) s->disabled = !have;
  if (have)
  {
    trackName_->setText(in->name);
    const auto &items = wave_->items();
    for (size_t i = 0; i < items.size(); i++)
      if (items[i] == in->wave) wave_->select(int(i), false);
    for (size_t i = 0; i < sample_->items().size(); i++)
      if (sample_->items()[i] == in->sample) sample_->select(int(i), false);
    attack_->setValue(in->attack, false);
    decay_->setValue(in->decay, false);
    sustain_->setValue(in->sustain, false);
    release_->setValue(in->release, false);
    volume_->setValue(in->volume, false);
    pan_->setValue(in->pan, false);
    root_->setValue(in->root, false);
    sampleLoop_->setPressed(in->loop);
    bool isSample = in->wave == "sample";
    sample_->disabled = root_->disabled = sampleLoop_->disabled = !isSample;
  }
  else trackName_->setText("");
  updating_ = false;
}

void SoundPanel::showNote()
{
  updating_ = true;
  bool have = false;
  if (loaded_ && roll_->track >= 0 && size_t(roll_->track) < song_.tracks.size())
  {
    const auto &notes = song_.tracks[size_t(roll_->track)].notes;
    if (roll_->selected >= 0 && size_t(roll_->selected) < notes.size())
    {
      have = true;
      velocity_->setValue(notes[size_t(roll_->selected)].velocity, false);
      length_->setValue(notes[size_t(roll_->selected)].length, false);
    }
  }
  velocity_->disabled = length_->disabled = !have;
  updating_ = false;
}

void SoundPanel::edited()
{
  dirty_ = true;
  saveTimer_ = 0;
  recook();
  // The track list shows note counts and waves.
  updating_ = true;
  for (size_t i = 0; i < song_.tracks.size() && i < tracks_->items.size(); i++)
  {
    const song::Track &t = song_.tracks[i];
    std::string wave = t.instrument >= 0 && size_t(t.instrument) < song_.instruments.size() ? song_.instruments[size_t(t.instrument)].wave : "?";
    tracks_->items[i].text = t.name + "  (" + wave + ", " + std::to_string(t.notes.size()) + " notes)";
  }
  updating_ = false;
}

void SoundPanel::save()
{
  dirty_ = false;
  if (!loaded_) return;
  std::string error;
  if (!song::save(song_, &error))
  {
    if (log) log(error, true);
    return;
  }
  // Only songs that cook reach the running game.
  if (!cooked_.empty() && changed) changed();
}

void SoundPanel::play(bool on)
{
  if (on && !cooked_.empty())
  {
    bmSoundInit(&sound_, nullptr, echo_.data());
    playing_ = bmSoundPlayData(&sound_, cooked_.data(), uint32_t(cooked_.size()), song_.effect);
  }
  else
  {
    bmSoundInit(&sound_, nullptr, echo_.data());
    playing_ = false;
  }
}

void SoundPanel::newSong()
{
  if (songDir.empty())
  {
    if (log) log("the project has no asset folder for songs yet", true);
    return;
  }
  openForm(*ui_, "New song",
           {{"Name", "song", FormField::Kind::Text, {}, 0, 0, {}}, {"Kind", "Music", FormField::Kind::Choice, {"Music", "Sound effect"}, 0, 0, {}}},
           "Create", [this](const std::vector<std::string> &v, std::string *pError) {
             std::string name = v[0];
             bool valid = !name.empty();
             for (char ch : name) valid = valid && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_');
             if (!valid)
             {
               *pError = "a song's name is made of letters, digits and _ (scripts and rooms use it)";
               return false;
             }
             std::error_code ec;
             fs::create_directories(songDir, ec);
             song::Song s;
             s.path = (fs::path(songDir) / (name + ".bmsong")).string();
             if (fs::exists(s.path, ec))
             {
               *pError = s.path + " exists already";
               return false;
             }
             s.effect = v[1] != "Music";
             s.length = s.effect ? 48 : 384;
             s.loopStart = s.effect ? -1 : 0;
             s.bpm = s.effect ? 150 : 120;
             song::Instrument in;
             in.name = "lead";
             in.wave = "pulse25";
             in.decay = 10;
             in.sustain = s.effect ? 0 : 160;
             s.instruments.push_back(in);
             s.tracks.push_back({"lead", 0, {{0, 72, 12, 100}}});
             if (!song::save(s, pError)) return false;
             if (log) log("new song " + s.path, false);
             if (created) created();
             open(s.path);
             return true;
           });
}

// Keeps a few frames of sound queued at the sound card, mixed by the engine itself.
void SoundPanel::mixAhead(float dt)
{
  bool sounding = playing_;
  for (const BmSoundVoice &v : sound_.voices) sounding = sounding || v.active;
  if (!sounding) return;
  if (!audioTried_)
  {
    audioTried_ = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
      SDL_AudioSpec spec = {SDL_AUDIO_S8, 2, BM_SOUND_RATE};
      stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
      if (stream_) SDL_ResumeAudioStreamDevice(stream_);
    }
    if (!stream_ && log) log("no sound device: the song plays silently", false);
  }
  int frames = 0;
  if (stream_)
  {
    // About four frames ahead: short enough to follow edits, long enough not to stutter.
    int queued = SDL_GetAudioStreamQueued(stream_);
    frames = std::clamp((4 * 2 * BM_SOUND_FRAME_SAMPLES - queued) / (2 * BM_SOUND_FRAME_SAMPLES) + 1, 0, 8);
    if (queued >= 4 * 2 * BM_SOUND_FRAME_SAMPLES) frames = 0;
  }
  else
  {
    mixTime_ += dt;
    frames = std::min(8, int(mixTime_ / kFrameSeconds));
    mixTime_ -= float(frames) * kFrameSeconds;
  }
  for (int f = 0; f < frames; f++)
  {
    int8_t left[BM_SOUND_FRAME_SAMPLES], right[BM_SOUND_FRAME_SAMPLES], both[2 * BM_SOUND_FRAME_SAMPLES];
    bmSoundFrame(&sound_, left, right, BM_SOUND_FRAME_SAMPLES);
    for (int i = 0; i < BM_SOUND_FRAME_SAMPLES; i++)
    {
      both[2 * i] = left[i];
      both[2 * i + 1] = right[i];
    }
    if (stream_) SDL_PutAudioStreamData(stream_, both, sizeof(both));
  }
}

void SoundPanel::update(float dt)
{
  if (dirty_ && (saveTimer_ += dt) > 0.5f) save();
  mixAhead(dt);
  int which = song_.effect ? 1 : 0;
  if (playing_ && !sound_.players[which].playing) playing_ = false;  // a song without a loop ended
  play_->text = playing_ ? "Stop" : "Play";
  roll_->playTick = playing_ ? double(sound_.players[which].tick) / 65536.0 : -1;
}

}  // namespace bm::editor
