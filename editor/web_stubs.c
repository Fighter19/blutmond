// SPDX-License-Identifier: BSD-2-Clause OR MIT
// The editor in the browser: Emscripten's SDL3 has no processes and no file dialogs. These
// stand in for them, so the editor builds unchanged: starting a program fails with a reason
// (Run and Sync need the desktop editor), and a file dialog closes at once without a choice
// (paths are typed into the form's field instead).

#include <SDL3/SDL.h>

SDL_Process *SDL_CreateProcessWithProperties(SDL_PropertiesID props)
{
  (void)props;
  SDL_SetError("programs cannot be started from the browser; Run and Sync need the desktop editor");
  return NULL;
}

SDL_IOStream *SDL_GetProcessOutput(SDL_Process *process)
{
  (void)process;
  return NULL;
}

bool SDL_KillProcess(SDL_Process *process, bool force)
{
  (void)process;
  (void)force;
  return false;
}

bool SDL_WaitProcess(SDL_Process *process, bool block, int *exitcode)
{
  (void)process;
  (void)block;
  if (exitcode) *exitcode = -1;
  return true;
}

void SDL_DestroyProcess(SDL_Process *process)
{
  (void)process;
}

static void noChoice(SDL_DialogFileCallback callback, void *userdata)
{
  static const char *const none[] = { NULL };
  callback(userdata, none, -1);
}

void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
                            const SDL_DialogFileFilter *filters, int nfilters, const char *default_location, bool allow_many)
{
  (void)window; (void)filters; (void)nfilters; (void)default_location; (void)allow_many;
  noChoice(callback, userdata);
}

void SDL_ShowSaveFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
                            const SDL_DialogFileFilter *filters, int nfilters, const char *default_location)
{
  (void)window; (void)filters; (void)nfilters; (void)default_location;
  noChoice(callback, userdata);
}

void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
                              const char *default_location, bool allow_many)
{
  (void)window; (void)default_location; (void)allow_many;
  noChoice(callback, userdata);
}
