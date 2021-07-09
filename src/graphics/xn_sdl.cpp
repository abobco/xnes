#include "xn_sdl.hpp"

// reflection 4 dummies
#define DUMP(a)                                                                \
  { std::cout << #a ": " << (a) << std::endl; }

#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(int, canvas_get_width, (),
      { return document.getElementById("canvas").width; });
EM_JS(int, canvas_get_height, (),
      { return document.getElementById("canvas").height; });

EM_JS(int, innerWidth, (), { return window.innerWidth; } )
EM_JS(int, innerHeight, (), { return window.innerHeight; } )

EM_JS(bool, is_mobile_browser, (), 
 {   
   const toMatch = [
        /Android/i,
        /webOS/i,
        /iPhone/i,
        /iPad/i,
        /iPod/i,
        /BlackBerry/i,
        /Windows Phone/i
    ];

    let r =  toMatch.some((toMatchItem) => {
      console.log(navigator.userAgent.match(toMatchItem));
        return navigator.userAgent.match(toMatchItem);
    });
    console.log(r);
    return r;
  }
);
// clang-format on
#endif

namespace xn {

namespace sdl {

Gamepad::Gamepad(uint16_t index) : index(index) {
  joystick = SDL_JoystickOpen(index);
  if (SDL_IsGameController(index)) {
    ctrl = SDL_GameControllerOpen(index);
    char *mapping = SDL_GameControllerMapping(ctrl);
    printf("Controller %i is mapped as \"%s\".", index, mapping);
  }
  id = SDL_JoystickInstanceID(joystick);
  name = SDL_JoystickName(joystick);
}

void Gamepad::setHandler(void (*event_handler)(const SDL_Event &e)) {
  this->event_handler = event_handler;
}

void Gamepad::processEvent(const SDL_Event &e) {
  if (e.type != SDL_JOYBUTTONDOWN && e.type != SDL_JOYBUTTONUP)
    return;
  if (e.jbutton.which != SDL_JoystickInstanceID(joystick))
    return;
  if (event_handler == NULL)
    return;
  event_handler(e);
}

WindowGL::WindowGL(const json &settings) {
  if (!settings["controller_db"].is_null()) {
    std::string gamepad_file = settings["controller_db"];
    SDL_GameControllerAddMappingsFromFile(gamepad_file.c_str());
  }
  init((int)settings["resolution"][0], (int)settings["resolution"][1],
       (std::string)settings["window_title"], (bool)settings["fullscreen"]);
}

WindowGL::WindowGL(int w, int h, std::string title) { init(w, h, title); }

void WindowGL::init(int w, int h, std::string title, bool fullscreen,
                    uint32_t monitor) {
  dimensions = glm::ivec2(w, h);
#ifdef __EMSCRIPTEN__
  mobile = is_mobile_browser();
  dimensions = glm::ivec2(canvas_get_width(), canvas_get_height());
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
    std::cerr << "Error: %s\n" << SDL_GetError() << '\n';
    return;
  }

  // Setup window
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_DisplayMode current;
  SDL_GetCurrentDisplayMode(0, &current);

  uint32_t wflags =
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifndef __EMSCRIPTEN__
  if (fullscreen)
    wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

  window = SDL_CreateWindow(title.c_str(),                           // title
                            SDL_WINDOWPOS_CENTERED_DISPLAY(monitor), // x
                            SDL_WINDOWPOS_CENTERED_DISPLAY(monitor), // y
                            dimensions.x, dimensions.y, // width, height
                            wflags);
  context = SDL_GL_CreateContext(window);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // #ifdef __EMSCRIPTEN__
  //     setSize(innerWidth(), innerHeight());
  // #else
  if (fullscreen) {
    SDL_GetWindowSize(window, &dimensions.x, &dimensions.y);
  }
  // #endif

  findGamepads();

#ifdef __EMSCRIPTEN__
  ImGui_ImplSdl_Init(window);
#else
  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    throw -1;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplSDL2_InitForOpenGL(window, context);
  ImGui_ImplOpenGL3_Init("#version 100");
#endif
}

void WindowGL::imguiNewFrame() {
#ifdef __EMSCRIPTEN__
  ImGui_ImplSdl_NewFrame(window);
#else
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame(window);
  ImGui::NewFrame();
#endif
}

void WindowGL::imguiDrawFrame() {
  ImGui::Render();
#ifndef __EMSCRIPTEN__
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

void WindowGL::findGamepads() {
  DUMP(SDL_NumJoysticks());
  if (SDL_NumJoysticks() > 0) {
    gamepads.clear();
    for (unsigned i = 0; i < SDL_NumJoysticks(); i++) {
      // SDL_JoystickOpen(i);
      gamepads.push_back(Gamepad(i));
    }
  }
}

void WindowGL::poll_events() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
#ifdef __EMSCRIPTEN__
    ImGui_ImplSdl_ProcessEvent(&event);
#else
    ImGui_ImplSDL2_ProcessEvent(&event);
#endif
    switch (event.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      if (keypress_callback != NULL)
        keypress_callback(event);
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      if (mouseclick_callback != NULL)
        mouseclick_callback(event);
      break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
      // if (gamepad_callback != NULL)
      //   gamepad_callback(event);
      for (Gamepad &g : gamepads)
        g.processEvent(event);
      break;
    case SDL_WINDOWEVENT:
      switch (event.window.event) {
      case SDL_WINDOWEVENT_CLOSE: // exit game
        shouldClose = true;
        break;
        if (window_event_callback != NULL)
          window_event_callback(event.window);
        break;
      default:
        break;
      }
      break;

    default:
      if (default_event_callback != NULL)
        default_event_callback(event);
      break;
    }
  }
}

void WindowGL::destroy() {
#ifdef __EMSCRIPTEN__
  ImGui_ImplSdl_Shutdown();
#else
  ImGui_ImplSDL2_Shutdown();
#endif
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
} // namespace sdl
} // namespace xn