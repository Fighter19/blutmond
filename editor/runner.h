// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Running the project from the editor: on the PC (the game in its own window) or on a GBA over
// the Xboo cable (multiboot once, then content is pushed into the running runtime). With
// "live", every cook while a target runs is pushed into it as a hot reload.
//
// Settings: ~/.config/blutmond/editor.conf, "key = value" lines, overridden on the command
// line: game, bmlink (program paths), port (PC live link, default 7414), xboo (device,
// default /dev/parport0), reset (none or xboo), gba-image (the runtime's multiboot image).
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

struct SDL_Process;

namespace bm::editor {

struct RunSettings
{
  std::string game, bmlink, gbaImage;
  std::string xboo = "/dev/parport0";
  std::string reset = "none";
  int port = 7414;

  /// Reads the settings file (missing file: defaults stay).
  void load();
  /// Applies "key=value" (command line); false if the key is unknown.
  bool set(const std::string &key, const std::string &value);
};

enum class RunTarget { Pc = 0, Gba = 1 };

class Runner
{
public:
  RunSettings settings;
  /// Lines of output from the tools, with a flag for errors.
  std::function<void(const std::string &line, bool error)> log;

  ~Runner();

  /// Starts (or restarts) the pack on the target.
  void run(RunTarget target, const std::string &packPath);
  /// Pushes the pack into what runs as a hot reload; false if nothing runs.
  bool pushHot(const std::string &packPath);
  /// Ends the PC game; forgets the GBA (it keeps running).
  void stop();
  /// Call every frame: collects output and notices ended processes.
  void update();

  bool gameRunning() const { return game_ != nullptr; }
  bool gbaActive() const { return gbaActive_; }
  bool busy() const { return job_ != nullptr; }
  bool active() const { return gameRunning() || gbaActive_; }
  const std::string &state() const { return state_; }

private:
  struct Job
  {
    std::vector<std::string> args;
    std::string what;
    bool gba = false;
  };
  void queue(Job job);
  void startNext();
  void drain(bool final);
  std::vector<std::string> linkArgs(RunTarget target) const;

  SDL_Process *game_ = nullptr;
  SDL_Process *job_ = nullptr;
  Job current_;
  std::deque<Job> jobs_;
  std::string partial_;
  bool gbaActive_ = false;
  RunTarget liveTarget_ = RunTarget::Pc;
  std::string state_ = "Not running";
};

}  // namespace bm::editor
