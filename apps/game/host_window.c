/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "host_window.h"

#include "hal.h"

#include <GLES2/gl2.h>
#include <SDL3/SDL.h>
#include <stdio.h>

// Drawn with GLES 2 (as the editor and, later, the browser build): the 240x160 picture as a
// texture on one quad, scaled by whole numbers and centred.

// 280896 cycles per frame at 16.78 MHz.
#define FRAME_NS 16742706ull

static SDL_Window *s_window;
static SDL_GLContext s_gl;
static GLuint s_texture, s_program, s_buffer;
static Uint64 s_next;
static uint8_t s_rgba[BM_HAL_SCREEN_WIDTH * BM_HAL_SCREEN_HEIGHT * 4];

static GLuint shader(GLenum type, const char *pSource)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &pSource, NULL);
  glCompileShader(s);
  return s;
}

static bool initGl(void)
{
  static const char *vs = "attribute vec2 pos; varying vec2 uv;\n"
                          "void main() { uv = vec2(0.5 + pos.x * 0.5, 0.5 - pos.y * 0.5);"  // row 0 at the top
                          " gl_Position = vec4(pos, 0.0, 1.0); }\n";
  static const char *fs = "precision mediump float; varying vec2 uv; uniform sampler2D tex;\n"
                          "void main() { gl_FragColor = texture2D(tex, uv); }\n";
  s_program = glCreateProgram();
  glAttachShader(s_program, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(s_program, shader(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(s_program, 0, "pos");
  glLinkProgram(s_program);
  GLint ok = 0;
  glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
  if (!ok) return false;
  static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
  glGenBuffers(1, &s_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, s_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glGenTextures(1, &s_texture);
  glBindTexture(GL_TEXTURE_2D, s_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BM_HAL_SCREEN_WIDTH, BM_HAL_SCREEN_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  return true;
}

bool bmWindowOpen(const char *pTitle, int scale)
{
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
  {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return false;
  }
  if (scale < 1) scale = 3;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  s_window = SDL_CreateWindow(pTitle, BM_HAL_SCREEN_WIDTH * scale, BM_HAL_SCREEN_HEIGHT * scale,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  s_gl = s_window ? SDL_GL_CreateContext(s_window) : NULL;
  if (s_gl == NULL || !SDL_GL_MakeCurrent(s_window, s_gl) || !initGl())
  {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    bmWindowClose();
    return false;
  }
  SDL_GL_SetSwapInterval(0);  // paced below at the GBA's rate, not the monitor's
  s_next = SDL_GetTicksNS();
  return true;
}

static void draw(const uint16_t *pPixels)
{
  for (int i = 0; i < BM_HAL_SCREEN_WIDTH * BM_HAL_SCREEN_HEIGHT; i++)
  {
    uint16_t c = pPixels[i];
    for (int k = 0; k < 3; k++)
    {
      unsigned v = (c >> (5 * k)) & 31;
      s_rgba[i * 4 + k] = (uint8_t)((v << 3) | (v >> 2));
    }
    s_rgba[i * 4 + 3] = 255;
  }
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(s_window, &w, &h);
  int scale = w / BM_HAL_SCREEN_WIDTH < h / BM_HAL_SCREEN_HEIGHT ? w / BM_HAL_SCREEN_WIDTH : h / BM_HAL_SCREEN_HEIGHT;
  if (scale < 1) scale = 1;
  int vw = BM_HAL_SCREEN_WIDTH * scale, vh = BM_HAL_SCREEN_HEIGHT * scale;
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
  glUseProgram(s_program);
  glBindTexture(GL_TEXTURE_2D, s_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BM_HAL_SCREEN_WIDTH, BM_HAL_SCREEN_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);
  glBindBuffer(GL_ARRAY_BUFFER, s_buffer);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  SDL_GL_SwapWindow(s_window);
}

static uint16_t gamepadKeys(void)
{
  uint16_t keys = 0;
  int count = 0;
  SDL_JoystickID *pIds = SDL_GetGamepads(&count);
  for (int i = 0; pIds != NULL && i < count; i++)
  {
    SDL_Gamepad *pad = SDL_GetGamepadFromID(pIds[i]);
    if (pad == NULL) continue;
    static const struct { SDL_GamepadButton button; uint16_t key; } map[] = {
      { SDL_GAMEPAD_BUTTON_SOUTH, BM_KEY_A },        { SDL_GAMEPAD_BUTTON_EAST, BM_KEY_B },
      { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, BM_KEY_L }, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, BM_KEY_R },
      { SDL_GAMEPAD_BUTTON_START, BM_KEY_START },    { SDL_GAMEPAD_BUTTON_BACK, BM_KEY_SELECT },
      { SDL_GAMEPAD_BUTTON_DPAD_UP, BM_KEY_UP },     { SDL_GAMEPAD_BUTTON_DPAD_DOWN, BM_KEY_DOWN },
      { SDL_GAMEPAD_BUTTON_DPAD_LEFT, BM_KEY_LEFT }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, BM_KEY_RIGHT },
    };
    for (size_t k = 0; k < sizeof(map) / sizeof(map[0]); k++)
      if (SDL_GetGamepadButton(pad, map[k].button)) keys |= map[k].key;
    const Sint16 dead = 12000;
    Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    if (x < -dead) keys |= BM_KEY_LEFT;
    if (x > dead) keys |= BM_KEY_RIGHT;
    if (y < -dead) keys |= BM_KEY_UP;
    if (y > dead) keys |= BM_KEY_DOWN;
  }
  SDL_free(pIds);
  return keys;
}

bool bmWindowFrame(const uint16_t *pPixels, uint16_t *pKeys)
{
  SDL_Event e;
  while (SDL_PollEvent(&e))
  {
    if (e.type == SDL_EVENT_QUIT) return false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
    {
      if (e.key.key == SDLK_ESCAPE) return false;
      if (e.key.key == SDLK_F11)
        SDL_SetWindowFullscreen(s_window, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) == 0);
    }
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
  }

  draw(pPixels);

  const bool *k = SDL_GetKeyboardState(NULL);
  uint16_t keys = gamepadKeys();
  if (k[SDL_SCANCODE_UP]) keys |= BM_KEY_UP;
  if (k[SDL_SCANCODE_DOWN]) keys |= BM_KEY_DOWN;
  if (k[SDL_SCANCODE_LEFT]) keys |= BM_KEY_LEFT;
  if (k[SDL_SCANCODE_RIGHT]) keys |= BM_KEY_RIGHT;
  if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE]) keys |= BM_KEY_A;
  if (k[SDL_SCANCODE_Z]) keys |= BM_KEY_B;
  if (k[SDL_SCANCODE_A]) keys |= BM_KEY_L;
  if (k[SDL_SCANCODE_S]) keys |= BM_KEY_R;
  if (k[SDL_SCANCODE_RETURN]) keys |= BM_KEY_START;
  if (k[SDL_SCANCODE_BACKSPACE]) keys |= BM_KEY_SELECT;
  *pKeys = keys;

  // Pace at the GBA frame rate; after a long stall start over instead of racing to catch up.
  s_next += FRAME_NS;
  Uint64 now = SDL_GetTicksNS();
  if (s_next > now) SDL_DelayPrecise(s_next - now);
  else if (now - s_next > 4 * FRAME_NS) s_next = now;
  return true;
}

void bmWindowClose(void)
{
  if (s_gl)
  {
    glDeleteTextures(1, &s_texture);
    glDeleteBuffers(1, &s_buffer);
    glDeleteProgram(s_program);
    SDL_GL_DestroyContext(s_gl);
  }
  if (s_window) SDL_DestroyWindow(s_window);
  s_gl = NULL;
  s_window = NULL;
  SDL_Quit();
}
