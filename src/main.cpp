#include "emulator.hpp"

using namespace xn;

// process window events, draw next emulation frame
void draw_frame();

// sound mixer callback, only used if multithreading is supported
float sound_update();

// user input handlers
void keyboardCallback(const SDL_Event &e);
void gamepadCallback(const SDL_Event &e);
void touchCallback(const SDL_Event &e);
void windowEventCallback(const SDL_WindowEvent &e);

// configure rom folder, default rom, fullscreen, mute, other stuff
json settings;

// NES emulator object, simulates in real time:
// - a 6502 microprocessor
// - the NES picture processing unit (PPU 2C02)
// - the pulse & noise audio channels of the NTSC NES's 2A03 chip
// - a few popular NES game cartridge hardware memory mappers
//   (iNES 000, 001, 002, & 004)
NesBus nes;

SpriteSheet NesTouchButton::ControllerSprites;
std::vector<NesTouchButton> buttons;
std::array<xn::gl::Texture2D, 2> patternImages;
NesRenderer::Sprite<PALETTE_WIDTH, PALETTE_HEIGHT> paletteSprite;
xn::gl::Texture2D frameImage, paletteImage;

bool show_info = true;
float emulation_speed = 1.0;

int main(int argc, char **argv) {
  std::string settings_path = DEFAULT_SETTINGS_FILEPATH;
  if (argc > 1) {
    settings_path = argv[1];
  }
  print("Loading settings file: ", settings_path, '\n');
  settings = load_json_file(settings_path);

  print("Loading rom: ", settings["rom"], '\n');
  romManager.setDirectory(settings["rom_folder"]);
  romManager.setActiveRom(nes, settings["rom"]);
  DUMP(nes.rom->header.getMapperNumber());

  nes.init();

  print("Creating window...\n");
  window = sdl::WindowGL(settings);
  std::vector<SDL_Rect> disp_list = sdl::get_display_list();
  SDL_SetWindowPosition(window.window, disp_list[1].x, disp_list[1].y);
  window.setKeyBoardCallback(keyboardCallback);
  window.setWindowEventCallback(windowEventCallback);
  if (window.mobile) {
    window.setDefaultEventCallback(touchCallback);
    show_info = false;
  }

  print("Creating audio context...\n");
  nes.reset();
  nes.setSampleFrequency(settings["audio_sample_rate"], emulation_speed);
  Sound.init(USE_AUDIO_THREAD, settings["audio_sample_rate"], 1, 8,
             512 * (1 + !USE_AUDIO_THREAD));
  Sound.setMixerCallback(sound_update);
  Sound.muted = true;
  print("Creating textures...\n");
  auto &framebuffer = nes.ppu.getFramebuffer().buffer;
  uint8_t i = 0;
  init_texture(frameImage, NesRenderer::NES_WIDTH, NesRenderer::NES_HEIGHT);
  init_texture(paletteImage, PALETTE_WIDTH, PALETTE_HEIGHT);
  upload_texture(frameImage, framebuffer.data());
  update_palette_texture(nes, paletteSprite, paletteImage);
  for (gl::Texture2D &im : patternImages) {
    init_texture(im, 128, 128);
    upload_texture(im, nes.ppu.getPatternTable(i++, 1).buffer.data());
  }
  NesTouchButton::ControllerSprites = SpriteSheet(
      settings["controller_sprite"], 5, 5, GL_TEXTURE5, glm::uvec2(22 * 5));

  nes.apu.enabled = !settings["mute"];
  print("Starting emulation!\n");
#ifdef __EMSCRIPTEN__
  // in the web version, the draw loop must yield to the main browser thread
  // between each frame
  emscripten_set_main_loop(draw_frame, 0, true);
#else

  while (!window.shouldClose) {
    draw_frame();
  }

  print("Closing window\n");
  window.destroy();
  Sound.destroy();
  return 0;
#endif
}

float sound_update() {
  nes.guard.lock();
  while (!nes.clock()) {
  };
#ifndef __EMSCRIPTEN__
  std::array<float, 3> samples = {(float)nes.apu.pulseChannel_1.output,
                                  (float)nes.apu.pulseChannel_2.output,
                                  (float)nes.apu.noiseChannel.output};
  unsigned i = 0;
  for (PulseGraph &c : soundController.channels) {
    c.addSample(samples[i++]);
  }
#endif
  float sample = static_cast<float>(nes.audioSample);
  nes.guard.unlock();
  return sample;
}

void draw_frame() {
  nes.setSampleFrequency(Sound.sampleRate, emulation_speed);
  // #if USE_AUDIO_THREAD
  //  if (!nes.ppu.frameComplete)
  //    return;
  //  nes.ppu.frameComplete = false;
  //#endif

  window.poll_events();
  window.imguiNewFrame();

  if (window.mobile && window.gamepads.size() == 0 && !show_info) {
    NesTouchButton::update_controller_state(buttons, window.touches);
    nes.controller[0] = NesTouchButton::get_controller_byte(buttons);
  }

#if !USE_AUDIO_THREAD
  static std::vector<ALuint> vProcessed;
  static std::queue<float> qToProcess;
  if (nes.apu.enabled) {
    while (!nes.ppu.frameComplete) {
      if (nes.clock())
        qToProcess.push(static_cast<float>(nes.audioSample));
    }
    nes.ppu.frameComplete = false;
    Sound.step(vProcessed, qToProcess);
  } else {
    nes.drawFrame();
    nes.ppu.frameComplete = false;
  }
#endif

  const uint32_t padding = 32;
  WindowLayout layout(window, frameImage);

  // draw NES frame
  {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(window.dimensions.x, window.dimensions.y));
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar;
    // flags |= ImGuiWindowFlags_NoResize;
    ImGui::Begin("XNES", NULL, flags);
    ImGui::BeginChild("video", layout.contentSize, true,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("Average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    upload_texture(frameImage, nes.ppu.getFramebuffer().buffer.data());
    imgui_draw_texture(frameImage, layout.frameScale);
    ImGui::EndChild();
  }

  // draw UI panel
  {
    int flags = 0;

    if (layout.horizontalPanel)
      ImGui::SameLine();
    if (!show_info)
      flags |= ImGuiWindowFlags_NoScrollbar;

    ImGui::BeginChild("info", ImVec2(0, 0), true, flags);
    toggleButton("Touch Controller", "Options", show_info);
    ImGui::SameLine(layout.contentSize.x - 100);
    toggleButton("Sound: On", "Sound: Off", nes.apu.enabled);

    if (show_info) {
      if (ImGui::Button("Close"))
        window.shouldClose = true;
      updateEmulatorOptions(nes, window, romManager);
      ImGui::NewLine();

      if (ImGui::TreeNode("More Stuff")) {
        romManager.update(nes);
        ImGui::NewLine();

        updateCpuInfo(nes, emulation_speed);
        ImGui::NewLine();

        updatePpuInfo(nes, paletteSprite, paletteImage, patternImages);
        ImGui::NewLine();

        updateApuInfo(nes, soundController);
        ImGui::TreePop();
      }

      ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
      ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y),
                               ImGuiMouseButton_Middle);
    } else {
      NesTouchButton::draw_controller(buttons, padding, window.dimensions);
    }
  }

  ImGui::EndChild();
  ImGui::End();
  glClearColor(0.5, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  window.imguiDrawFrame();
  window.flip();
}

void windowEventCallback(const SDL_WindowEvent &e) {
  switch (e.event) {
  case SDL_WINDOWEVENT_RESIZED:
    if (e.data1 != window.dimensions.x && e.data2 != window.dimensions.x)
      window.setSize(e.data1, e.data2);
    break;

  case SDL_WINDOWEVENT_MINIMIZED:
    alSourcePause(g_OpenAL_Instance.source);
    break;

  case SDL_WINDOWEVENT_RESTORED:
    alSourcePlay(g_OpenAL_Instance.source);
    break;
  }
}

void touchCallback(const SDL_Event &e) {
  switch (e.type) {
  case SDL_FINGERDOWN:
  case SDL_FINGERMOTION:
    window.touches[e.tfinger.fingerId] = e.tfinger;
    break;
  case SDL_FINGERUP:
    window.touches.erase(e.tfinger.fingerId);
    break;
  default:
    break;
  }
}

// clang-format off
void map_key(Uint32 eventType, uint8_t controllerFlag) {
  if (eventType == SDL_KEYDOWN)    nes.controller[0] |= controllerFlag;
  else if (eventType == SDL_KEYUP) nes.controller[0] &= (~controllerFlag);
}

void keyboardCallback(const SDL_Event &e) {
  switch (e.key.keysym.sym) {
  case SDLK_ESCAPE: window.shouldClose = true; break;
  case SDLK_x:      map_key(e.key.type, 0x80); break;
  case SDLK_z:      map_key(e.key.type, 0x40); break;
  case SDLK_a:      map_key(e.key.type, 0x20); break;
  case SDLK_s:      map_key(e.key.type, 0x10); break;
  case SDLK_UP:     map_key(e.key.type, 0x08); break;
  case SDLK_DOWN:   map_key(e.key.type, 0x04); break;
  case SDLK_LEFT:   map_key(e.key.type, 0x02); break;
  case SDLK_RIGHT:  map_key(e.key.type, 0x01); break;
  default: break;
  }
}
// clang-format on
