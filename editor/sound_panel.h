// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The Sound tab: a song (docs/spec/sound-format.md) with its tracks in a piano roll, the
// instrument of the selected track, the song's settings and what it costs on the hardware.
// The preview plays through the engine's own mixer (src/sound/sound.h), so it is sample for
// sample what the device plays. Edits are saved to the .bmsong file shortly after; the editor
// then cooks again and, with Live, the running game gets the song.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "song_asset.h"
#include "ui/control.h"
#include "ui/core.h"

extern "C" {
#include "sound.h"
}

struct SDL_AudioStream;

namespace ose::ui {
class Button;
class CheckBox;
class Context;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class ProgressBar;
class SpinBox;
}  // namespace ose::ui

namespace bm::editor {

/// Notes over time (left to right) and pitch (bottom to top). The selected track's notes are
/// solid, the others pale. Click an empty place to add a note, drag a note to move it, drag
/// its right end to change its length, right click to remove it; the wheel scrolls (with
/// Shift: sideways).
class PianoRoll : public ose::ui::Control
{
public:
  PianoRoll() { focusMode = ose::ui::FocusMode::Click; clipContents = true; }
  const char *typeName() const override { return "PianoRoll"; }
  ose::Vec2 computeMinSize() override { return {300, 200}; }
  void draw(ose::ui::Canvas &c) override;
  bool event(const ose::ui::InputEvent &e) override;

  song::Song *song = nullptr;
  int track = 0;
  int selected = -1;              // note of the track
  int newLength = 12;             // ticks of a note added by a click
  int grid = 6;                   // ticks notes snap to (a sixteenth)
  double playTick = -1;           // the play position, or < 0
  std::function<void()> edited;   // the song changed
  std::function<void()> selectionChanged;
  std::function<void(int key)> audition;  // a note was added or picked

private:
  float keyWidth() const { return 44; }
  float tickWidth() const { return 2.0f; }
  float rowHeight() const { return 9; }
  int topKey() const;
  int noteAt(ose::Vec2 p, bool *pOnEnd) const;
  int tickAt(float x) const;
  int keyAt(float y) const;

  float scrollX_ = 0;
  int scrollKey_ = 84;            // the key in the top row
  int dragging_ = 0;              // 1 move, 2 resize
  int dragTickOffset_ = 0;
};

class SoundPanel
{
public:
  /// The song file was saved: cook again (and push to the running game).
  std::function<void()> changed;
  /// A new song file was written.
  std::function<void()> created;
  std::function<void(const std::string &line, bool error)> log;
  std::string songDir;            // where new songs go

  ~SoundPanel();
  void bind(ose::ui::Control &root, ose::ui::Context &ui);
  void setSongs(const std::vector<std::string> &paths);
  void open(const std::string &path);
  const std::string &path() const { return song_.path; }
  void newSong();
  void update(float dt);

private:
  void recook();                  // after an edit: new song data for the preview and the numbers
  void fill();                    // lists and fields from the song
  void showInstrument();
  void showNote();
  void edited();
  void save();
  void play(bool on);
  /// The preview engine anew (play, recook): with the intensity chosen.
  void resetEngine();
  /// The song as the preview plays it: without the muted tracks (or only the solo one).
  song::Song previewSong() const;
  void showTrackFlags();
  std::string trackLabel(size_t track) const;   // name, wave, notes, layer, muted/solo
  void refreshTrackList();
  void mixAhead(float dt);
  song::Instrument *instrument();

  ose::ui::Context *ui_ = nullptr;
  std::vector<std::string> paths_;
  song::Song song_;
  bool loaded_ = false, updating_ = false, dirty_ = false, playing_ = false;
  float saveTimer_ = 0, mixTime_ = 0;
  std::vector<uint8_t> cooked_;
  song::Cost cost_;
  BmSound sound_{};
  std::vector<int8_t> echo_;
  SDL_AudioStream *stream_ = nullptr;
  bool audioTried_ = false;

  PianoRoll *roll_ = nullptr;
  ose::ui::OptionButton *chooser_ = nullptr, *wave_ = nullptr, *sample_ = nullptr, *noteLength_ = nullptr;
  ose::ui::ItemList *tracks_ = nullptr;
  ose::ui::LineEdit *trackName_ = nullptr;
  ose::ui::SpinBox *bpm_ = nullptr, *bars_ = nullptr, *echo_ms_ = nullptr, *feedback_ = nullptr;
  ose::ui::SpinBox *attack_ = nullptr, *decay_ = nullptr, *sustain_ = nullptr, *release_ = nullptr, *volume_ = nullptr, *pan_ = nullptr, *root_ = nullptr;
  ose::ui::SpinBox *velocity_ = nullptr, *length_ = nullptr;
  ose::ui::CheckBox *loop_ = nullptr, *effect_ = nullptr, *sampleLoop_ = nullptr, *mute_ = nullptr, *solo_ = nullptr;
  ose::ui::SpinBox *layer_ = nullptr, *until_ = nullptr, *intensity_ = nullptr, *startIntensity_ = nullptr, *fade_ = nullptr;
  ose::ui::LineEdit *speakText_ = nullptr;
  int previewIntensity_ = 15;
  std::vector<bool> muted_;         // per track, for the preview
  int soloTrack_ = -1;
  std::u32string speaking_;         // a line being spoken by the effect (Speak)
  size_t speakAt_ = 0;
  ose::ui::Button *play_ = nullptr;
  ose::ui::Label *info_ = nullptr, *voices_ = nullptr, *cpu_ = nullptr, *size_ = nullptr;
  ose::ui::ProgressBar *voicesBar_ = nullptr, *cpuBar_ = nullptr, *sizeBar_ = nullptr;
};

}  // namespace bm::editor
