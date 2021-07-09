#include "emulator.hpp"

namespace xn {
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

void imgui_draw_texture(const xn::gl::Texture2D &tex, float scale) {
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

} // namespace xn