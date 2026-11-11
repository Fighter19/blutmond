// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "runner.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <fstream>

namespace bm::editor {

namespace {

std::string trim(const std::string &s)
{
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

SDL_Process *spawn(const std::vector<std::string> &args, bool pipe)
{
  std::vector<const char *> argv;
  for (const std::string &a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
  if (pipe)
  {
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  }
  else
  {
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
  }
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_Process *p = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  return p;
}

}  // namespace

void RunSettings::load()
{
  const char *home = std::getenv("HOME");
  if (!home) return;
  std::ifstream in(std::string(home) + "/.config/blutmond/editor.conf");
  std::string line;
  while (std::getline(in, line))
  {
    line = trim(line);
    size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    set(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
  }
}

bool RunSettings::set(const std::string &key, const std::string &value)
{
  if (key == "game") game = value;
  else if (key == "bmlink") bmlink = value;
  else if (key == "gba-image") gbaImage = value;
  else if (key == "xboo") xboo = value;
  else if (key == "reset") reset = value;
  else if (key == "port") port = std::atoi(value.c_str());
  else return false;
  return true;
}

Runner::~Runner()
{
  stop();
  if (job_)
  {
    SDL_KillProcess(job_, true);
    SDL_WaitProcess(job_, true, nullptr);
    SDL_DestroyProcess(job_);
  }
}

std::vector<std::string> Runner::linkArgs(RunTarget target) const
{
  if (target == RunTarget::Pc) return {settings.bmlink, "--tcp", "127.0.0.1:" + std::to_string(settings.port)};
  return {settings.bmlink, "--xboo", settings.xboo, "--reset", settings.reset};
}

void Runner::run(RunTarget target, const std::string &packPath)
{
  if (target == RunTarget::Pc)
  {
    liveTarget_ = RunTarget::Pc;
    if (game_)
    {
      std::vector<std::string> args = linkArgs(target);
      args.insert(args.end(), {"push", packPath, "restart"});
      queue({args, "Restart on the PC", false, {}, false});
      return;
    }
    game_ = spawn({settings.game, packPath, "--listen", std::to_string(settings.port)}, false);
    if (!game_)
    {
      if (log) log("cannot start " + settings.game + ": " + SDL_GetError(), true);
      state_ = "Not running";
      return;
    }
    state_ = "Running on the PC";
    if (log) log("started " + settings.game + " " + packPath, false);
    return;
  }

  if (settings.gbaImage.empty())
  {
    if (log) log("no runtime image for the GBA: set gba-image in ~/.config/blutmond/editor.conf or pass --gba-image", true);
    return;
  }
  std::vector<std::string> args = linkArgs(target);
  args.insert(args.end(), {"boot-if-needed", settings.gbaImage, "push", packPath, "restart"});
  queue({args, "Run on the GBA", true, {}, false});
}

bool Runner::pushHot(const std::string &packPath)
{
  RunTarget target = liveTarget_;
  if (target == RunTarget::Pc && !game_) target = RunTarget::Gba;
  if (target == RunTarget::Gba && !gbaActive_) return false;
  // A newer cook replaces pushes still waiting.
  while (!jobs_.empty() && jobs_.back().what == "Hot reload") jobs_.pop_back();
  std::vector<std::string> args = linkArgs(target);
  args.insert(args.end(), {"push", packPath, "hot"});
  queue({args, "Hot reload", target == RunTarget::Gba, {}, false});
  return true;
}

bool Runner::capture(const std::string &path, std::function<void(bool)> done)
{
  RunTarget target = liveTarget_;
  if (target == RunTarget::Pc && !game_) target = RunTarget::Gba;
  if (target == RunTarget::Gba && !gbaActive_) return false;
  // One capture at a time is enough.
  for (const Job &j : jobs_)
    if (j.what == "Capture") return true;
  if (job_ && current_.what == "Capture") return true;
  std::vector<std::string> args = linkArgs(target);
  args.insert(args.end(), {"ppu-snapshot", path});
  Job job{args, "Capture", target == RunTarget::Gba, std::move(done), true};
  queue(std::move(job));
  return true;
}

void Runner::stop()
{
  if (game_)
  {
    SDL_KillProcess(game_, false);
    SDL_WaitProcess(game_, true, nullptr);
    SDL_DestroyProcess(game_);
    game_ = nullptr;
    if (log) log("stopped the game", false);
  }
  jobs_.clear();
  gbaActive_ = false;
  state_ = "Not running";
}

void Runner::queue(Job job)
{
  jobs_.push_back(std::move(job));
  if (!job_) startNext();
}

void Runner::startNext()
{
  if (jobs_.empty()) return;
  current_ = std::move(jobs_.front());
  jobs_.pop_front();
  job_ = spawn(current_.args, true);
  if (!job_)
  {
    if (log) log("cannot start " + current_.args[0] + ": " + SDL_GetError(), true);
    startNext();
    return;
  }
  state_ = current_.what + "...";
}

void Runner::drain(bool final)
{
  bool quiet = current_.quiet;
  SDL_IOStream *out = job_ ? SDL_GetProcessOutput(job_) : nullptr;
  char buf[512];
  while (out)
  {
    size_t n = SDL_ReadIO(out, buf, sizeof(buf));
    if (n == 0) break;
    partial_.append(buf, n);
  }
  size_t nl;
  while ((nl = partial_.find('\n')) != std::string::npos)
  {
    if (log && !quiet) log(partial_.substr(0, nl), false);
    partial_.erase(0, nl + 1);
  }
  if (final && !partial_.empty())
  {
    if (log && !quiet) log(partial_, false);
    partial_.clear();
  }
}

void Runner::update()
{
  int code = 0;
  if (game_ && SDL_WaitProcess(game_, false, &code))
  {
    SDL_DestroyProcess(game_);
    game_ = nullptr;
    if (log) log("the game ended (exit code " + std::to_string(code) + ")", code != 0);
    if (!gbaActive_) state_ = "Not running";
  }
  if (!job_) return;
  drain(false);
  if (!SDL_WaitProcess(job_, false, &code)) return;
  drain(true);
  SDL_DestroyProcess(job_);
  job_ = nullptr;
  // Only a run decides whether the GBA counts as running; pushes and captures may fail on a
  // bad cable without that.
  if (current_.gba && current_.what == "Run on the GBA")
  {
    gbaActive_ = code == 0;
    if (code == 0) liveTarget_ = RunTarget::Gba;
  }
  if (log && (code != 0 || !current_.quiet))
    log(current_.what + (code == 0 ? ": done" : ": failed (exit code " + std::to_string(code) + ")"), code != 0);
  if (current_.done) current_.done(code == 0);
  state_ = liveTarget_ == RunTarget::Gba && gbaActive_ ? "Running on the GBA" : game_ ? "Running on the PC" : gbaActive_ ? "Running on the GBA" : "Not running";
  startNext();
}

}  // namespace bm::editor
