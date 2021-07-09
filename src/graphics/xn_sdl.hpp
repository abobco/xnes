// #include "xn_common/xn_gui.hpp"
#pragma once
// clang-format off
#ifdef __EMSCRIPTEN__
#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_internal.h>
#include <GLES3/gl3.h>
#include <emscripten.h>
#else
#define SDL_MAIN_HANDLED
#ifdef _WIN32
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <glad/glad.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

// clang-format on

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArrays glBindVertexArraysAPPLE
#define glGenVertexArray glGenVertexArrayAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#else
// #include <SDL_opengles2.h>
#endif

#include <iostream>
#include <util/xn_json.hpp>

namespace xn {

namespace sdl {

static std::vector<SDL_Rect> get_display_list() {
  std::vector<SDL_Rect> displays;
  uint16_t total_displays = SDL_GetNumVideoDisplays();
  displays.resize(total_displays);
  for (auto i = 0; i < total_displays; i++) {
    SDL_GetDisplayBounds(i, &displays[i]);
  }
  return displays;
}

struct Gamepad {
  uint16_t index;
  SDL_JoystickID id;
  SDL_Joystick *joystick = NULL;
  SDL_GameController *ctrl;
  std::string name;
  void (*event_handler)(const SDL_Event &e) = NULL;

  Gamepad() {}

  Gamepad(uint16_t index);

  void setHandler(void (*event_handler)(const SDL_Event &e));

  void processEvent(const SDL_Event &e);
};

struct WindowGL {
  SDL_Window *window;
  SDL_GLContext context;
  glm::ivec2 dimensions;
  bool shouldClose = false;
  bool mobile = false;

  std::map<SDL_FingerID, SDL_TouchFingerEvent> touches;
  std::vector<Gamepad> gamepads;

  void (*mouseclick_callback)(const SDL_Event &e) = NULL;
  void (*keypress_callback)(const SDL_Event &e) = NULL;
  void (*gamepad_callback)(const SDL_Event &e) = NULL;
  void (*default_event_callback)(const SDL_Event &e) = NULL;
  void (*window_event_callback)(const SDL_WindowEvent &e) = NULL;

  WindowGL() {}

  WindowGL(const json &settings);

  WindowGL(int w, int h, std::string title = "ImGUI / WASM / WebGL");

  void init(int w, int h, std::string title = "ImGUI / WASM / WebGL",
            bool fullscreen = false, uint32_t monitor = 1);

  void imguiNewFrame();

  void imguiDrawFrame();

  void findGamepads();

  void setMouseClickCallback(void (*callback)(const SDL_Event &e)) {
    mouseclick_callback = callback;
  }

  void setKeyBoardCallback(void (*callback)(const SDL_Event &e)) {
    keypress_callback = callback;
  }

  void setDefaultEventCallback(void (*callback)(const SDL_Event &e)) {
    default_event_callback = callback;
  }

  void setWindowEventCallback(void (*callback)(const SDL_WindowEvent &e)) {
    window_event_callback = callback;
  }

  void setGamepadCallback(void (*callback)(const SDL_Event &e)) {
    gamepad_callback = callback;
  }

  void setSize(int x, int y) {
    SDL_SetWindowSize(window, x, y);
    dimensions = glm::ivec2(x, y);
  }

  void poll_events();

  void flip() { SDL_GL_SwapWindow(window); }

  void destroy();
};

} // namespace sdl

} // namespace xn
