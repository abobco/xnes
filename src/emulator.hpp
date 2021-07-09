#pragma once

#include "nes/bus.hpp"
#include "platform_wasm.hpp"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
namespace xn {

static const size_t PALETTE_WIDTH = 64, PALETTE_HEIGHT = 64;

const std::map<SDL_GameControllerButton, uint8_t> gamepad_button_map = {
    {SDL_CONTROLLER_BUTTON_A, 0x80},          // a
    {SDL_CONTROLLER_BUTTON_B, 0x40},          // b
    {SDL_CONTROLLER_BUTTON_START, 0x10},      //+
    {SDL_CONTROLLER_BUTTON_BACK, 0x20},       //-
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x02},  // l
    {SDL_CONTROLLER_BUTTON_DPAD_UP, 0x08},    // u
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x01}, // r
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x04},  // d
};

struct PulseGraph {
  static const size_t MAX_SAMPLES = 512;
  size_t sampleCount = 0;
  std::array<float, MAX_SAMPLES> samples;
  std::string label;
  float volume = 0.1;
  float *volume_ptr = &volume;

  PulseGraph() {}

  PulseGraph(const std::string &label, float volume = 0.1)
      : label(label), volume(volume) {
    std::memset(samples.data(), 0, sizeof(samples));
  }

  void addSample(float new_sample) {
    std::rotate(samples.begin(), samples.begin() + 1, samples.end());
    samples[0] = new_sample;
  }

  static float sampleFunc(void *argv, int i) {
    float *buf = (float *)argv;
    return buf[i];
  }

  void draw() {
    ImGui::PlotLines(label.c_str(), sampleFunc, (void *)samples.data(),
                     MAX_SAMPLES, 0, NULL, -1.0f, 1.0f, ImVec2(0, 80));
  }
};

struct SoundController {
  float volumeGlobal;
  std::vector<PulseGraph> channels = {
      PulseGraph("Square 1"), PulseGraph("Square 2"), PulseGraph("Noise", 0.2)};

  void draw(NesBus &nes) {
    ImGui::Checkbox("Mute", &Sound.muted);
    channels[0].volume_ptr = &nes.apu.pulseChannel_1.volume;
    channels[1].volume_ptr = &nes.apu.pulseChannel_2.volume;
    channels[2].volume_ptr = &nes.apu.noiseChannel.volume;
    // ImGui::SliderFloat("Volume", &volumeGlobal, 0.0, 1.0);

    // ImGui::SliderInt("Pulse Channel 1 iterations",
    //                  &nes.apu.pulseChannel_1.pulse.harmonics, 1, 40);
    // ImGui::SliderInt("Pulse Channel 2 iterations",
    //                  &nes.apu.pulseChannel_2.pulse.harmonics, 1, 40);
    for (auto &c : channels) {
      std::string full_label = c.label + " volume";
      ImGui::SliderFloat(full_label.c_str(), c.volume_ptr, 0.0, 1.0);
    }
    for (auto &c : channels)
      c.draw();
  }

} soundController;

struct RomManager {
  std::string romDirectory;
  std::string activeRom;
  int activeRomIndex = 0;
  std::vector<std::string> filenames;

  RomManager() {}

  void setDirectory(const std::string &rom_dir) {
    romDirectory = rom_dir;
    filenames.clear();
    for (const auto &entry : fs::directory_iterator(romDirectory)) {
      std::string rom = entry.path().string();

      auto it = rom.find(rom_dir);
      if (it != std::string::npos) {
        rom.erase(it, rom_dir.length());
      }
      std::string ext = ".nes";
      if (rom.find(ext) == rom.size() - ext.size())
        filenames.push_back(rom);
    }
  }

  void setActiveRom(NesBus &nes, const std::string &rom_path) {
    auto it = std::find(filenames.begin(), filenames.end(), rom_path);
    if (it != filenames.end()) {
      activeRomIndex = it - filenames.begin();
    }

    if (nes.loadRom(getActiveRomPath()) < 0) {
      std::cerr << "Rom not found!\n";
      exit(-1);
    }
  }

  std::string getActiveRomPath() const {
    return romDirectory + filenames[activeRomIndex];
  }

  std::string getActiveRom() const { return filenames[activeRomIndex]; }

  void update(NesBus &nes) {
    ImGui::Text("Roms:");
    ImGui::Separator();
    int prev_index = activeRomIndex;
    unsigned i = 0;
    for (const std::string &f : filenames) {
      ImGui::RadioButton(f.c_str(), &activeRomIndex, i++);
    }
    if (activeRomIndex != prev_index) {
      nes.guard.lock();
      nes.init();
      nes.loadRom(getActiveRomPath());
      nes.reset();
      nes.guard.unlock();
    }
  }
} romManager;

struct WindowLayout {
  bool horizontalPanel;
  double frameScale;
  ImVec2 contentSize;
  const uint16_t padding = 32;
  WindowLayout(const sdl::WindowGL &window, const gl::Texture2D &frameImage,
               unsigned padding = 32) {
    horizontalPanel = window.dimensions.x > window.dimensions.y;
    // scale frame to length of smallest window dimension
    if (horizontalPanel) {
      frameScale = (double)(window.dimensions.y - padding * 1.5) /
                   NesRenderer::NES_HEIGHT;
    } else {
      frameScale = (double)(window.dimensions.x - padding * 1.5) /
                   NesRenderer::NES_WIDTH;
    }

    // calculate nes frame size
    contentSize = ImVec2(frameImage.width * frameScale + padding,
                         (frameImage.height + padding) * frameScale - padding);
  }
};

void init_texture(xn::gl::Texture2D &tex, uint32_t width, uint32_t height) {
  static uint32_t tex_id_counter = GL_TEXTURE0;

  tex.width = width;
  tex.height = height;
  tex.uniform_idx = tex_id_counter++;
  tex.setTexParams(GL_NEAREST, GL_CLAMP_TO_EDGE);
}

void upload_texture(xn::gl::Texture2D &tex, const NesPixel *data) {
  tex.activate();
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex.width, tex.height, 0, GL_RGB,
               GL_UNSIGNED_BYTE, data);
}

void imgui_draw_texture(const xn::gl::Texture2D &tex, float scale = 2.0) {
  ImVec2 imageExtents(tex.width * scale, tex.height * scale);
  ImGui::Image((ImTextureID)tex.id, imageExtents);
}

template <size_t X, size_t Y>
void update_palette_texture(NesBus &nes,
                            NesRenderer::Sprite<X, Y> &paletteSprite,
                            xn::gl::Texture2D &paletteImage) {
  for (uint8_t i = 0; i < 8; i++)
    for (uint8_t j = 0; j < 4; j++)
      for (uint8_t x = 0; x < (X / 4); x++)
        for (uint8_t y = 0; y < (Y / 8); y++)
          paletteSprite.setPixel(j * (X / 4) + x, i * (Y / 8) + y,
                                 //  {(uint8_t)(i * 16), 0, 0});
                                 nes.ppu.getColorFromPalette(i, j));
  upload_texture(paletteImage, paletteSprite.buffer.data());
}

void updateControllerState(NesBus &nes, sdl::Gamepad &gamepad) {
  for (auto &[k, v] : gamepad_button_map) {
    if (SDL_GameControllerGetButton(gamepad.ctrl, k)) {
      nes.controller[0] |= v;
    } else {
      nes.controller[0] &= (~v);
    }
  }

  if (SDL_GameControllerGetButton(gamepad.ctrl,
                                  SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
    nes.loadState("quicksave.save");
  }

  if (SDL_GameControllerGetButton(gamepad.ctrl,
                                  SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
    nes.saveState("quicksave.save");
  }
}

void updateEmulatorOptions(NesBus &nes, sdl::WindowGL &window,
                           RomManager &romManager) {
  ImGui::Text("Emulator");
  ImGui::Separator();

  if (ImGui::Button("Save")) {
    print("saving ", romManager.getActiveRom(), ".save\n");
    nes.saveState(romManager.getActiveRom() + ".save");
  }
  ImGui::SameLine();
  if (ImGui::Button("Load")) {
    print("loading ", romManager.getActiveRom(), ".save\n");
    nes.loadState(romManager.getActiveRom() + ".save");
  }

  ImGui::Text("window size: (%d, %d)", window.dimensions.x,
              window.dimensions.y);
  ImGui::Text("Mobile: %s", window.mobile ? "true" : "false");

  if (ImGui::Button("Find controllers"))
    window.findGamepads();
  ImGui::SameLine();
  ImGui::Text("Controllers: %d", window.gamepads.size());

  if (window.gamepads.size() > 0) {
    static int controller_index = 0;
    for (auto &g : window.gamepads) {
      g.setHandler(NULL);
      ImGui::RadioButton(g.name.c_str(), &controller_index, g.index);
    }
    updateControllerState(nes, window.gamepads[controller_index]);
  }
}

void updateCpuInfo(NesBus &nes, float &emulation_speed) {
  const auto &r = nes.cpu.registers;
  std::bitset<8> status{nes.cpu.registers.P};
  ImGui::Text("CPU");
  ImGui::Separator();
  ImGui::SliderFloat("Emulation speed", &emulation_speed, 0.01, 50);
  if (emulation_speed > 1.0) {
    Sound.muted = true;
    nes.apu.enabled = false;
  }
  static int emulation_mode = 0;
  static int emulation_mode_prev = 0;
  ImGui::RadioButton("Cycle counting", &emulation_mode, 0);
  ImGui::RadioButton("Fastest", &emulation_mode, 1);
  if (emulation_mode != emulation_mode_prev) {
    if (emulation_mode == 0) {
      Instruction::setCycleCounts(nes.cpu.instructionMap);
      // unknown instructions
      for (const auto &i : dead_cells) {
        nes.cpu.instructionMap[Instruction::Index::pack(i)].toUnknown();
        nes.cpu.instructionMap[Instruction::Index::pack(i)].callback =
            nes.cpu.instrCallbacks.at("NOP");
        nes.cpu.instructionMap[Instruction::Index::pack(i)].addrmodeCallback =
            nes.cpu.addrModeCallbacks.at("#");
      }
    } else if (emulation_mode == 1) {
      for (auto &[k, v] : nes.cpu.instructionMap)
        v.cycles = 2;
    }
  }

  emulation_mode_prev = emulation_mode;
  ImGui::Text("Cycle count: %u", nes.cpu.cycleCount);
  ImGui::Text("Registers:");
  ImGui::Text("Status:%s", status.to_string().c_str());
  ImGui::Text("Stack Pointer:%d", r.S);
  ImGui::Text("PC:%d\nA:%d\tX:%d\tY:%d", r.A, r.PC, r.X, r.Y);

  // show disassembly
  if (0) {
    ImGui::Text("Program:");
    int i = 0;
    std::vector<Instruction> dq;
    nes.cpu.getInstructionQueue(dq);
    for (const auto &instr : dq) {
      std::string txt;
      bool illegalInstr = instr.opcode == "???";
      instr.toString(txt);
      if (i == nes.cpu.disassemblyIndex)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0, 0.0, 1.0, 1.0));
      if (illegalInstr)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0, 0.0, 0.0, 1.0));
      ImGui::Text("%s", txt.c_str());
      if (i++ == nes.cpu.disassemblyIndex || illegalInstr)
        ImGui::PopStyleColor();
    }
  }
}

void updatePpuInfo(
    NesBus &nes,
    NesRenderer::Sprite<PALETTE_WIDTH, PALETTE_HEIGHT> &paletteSprite,
    gl::Texture2D &paletteImage, std::array<gl::Texture2D, 2> &patternImages) {
  unsigned i;
  const auto &r = nes.ppu.registers;
  std::bitset<8> bits[] = {r.STATUS.val, r.CTRL.val, r.MASK.val,
                           r.fineX,      r.t.val,    r.v.val};
  std::string labels[] = {"Status", "Control", "Mask", "Fine X", "t", "v"};

  ImGui::NewLine();
  ImGui::Text("PPU");
  ImGui::Separator();
  ImGui::Text("Cycle count: %u", nes.systemClockCount);
  ImGui::Text("Registers:");
  for (i = 0; i < sizeof(bits) / sizeof(bits[0]); i++)
    ImGui::Text("%s:\t%s", labels[i].c_str(), bits[i].to_string().c_str());
#ifndef __EMSCRIPTEN__
  ImGui::NewLine();
  ImGui::Text("Palettes:");
  ImGui::Separator();
  update_palette_texture(nes, paletteSprite, paletteImage);
  imgui_draw_texture(paletteImage);

  ImGui::NewLine();
  ImGui::Text("Sprite Pattern Tables");
  ImGui::Separator();
  i = 0;
  for (auto &im : patternImages) {
    upload_texture(im, nes.ppu.getPatternTable(i++, 1).buffer.data());
    imgui_draw_texture(im, 1.2);
  }
#endif
}

void updateApuInfo(NesBus &nes, SoundController &soundController) {
  ImGui::NewLine();
  ImGui::Text("Audio");
  ImGui::Separator();
  // APU::TriangleWave testWave;
  // float samples[1000];
  // for (auto i = 0; i < 1000; i++) {
  //   samples[i] = testWave.sample(i * M_PI * 4 / 1000.0);
  // }
  // ImGui::PlotLines("triangle", samples, 1000, 0, NULL, -1, 1,
  //                  ImVec2(0, 80));
  soundController.draw(nes);
}

void toggleButton(const char *labelTrue, const char *labelFalse, bool &toggle) {
  if (toggle) {
    if (ImGui::Button(labelTrue))
      toggle = !toggle;
  } else {
    if (ImGui::Button(labelFalse))
      toggle = !toggle;
  }
}

// struct AppWindow {
//   ImVec2 bounds = {0, 0};
//   std::string title;
//   std::function<void(void)> drawFunction;
//   AppWindow(std::string title, std::function<void(void)> drawFunctin,
//             ImVec2 bounds = {0, 0})
//       : title(title), drawFunction(drawFunction), bounds(bounds) {}

//   void draw(int imgui_flags = 0, bool border = true) {
//     ImGui::BeginChild(title.c_str(), bounds, border, imgui_flags);
//     drawFunction();
//     ImGui::EndChild();
//   }
// };

} // namespace xn
