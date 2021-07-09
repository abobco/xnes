#pragma once
#include "nes/bus.hpp"
#include "sound.hpp"
// #include <graphics/xn_gl.hpp>
#include <graphics/xn_sdl.hpp>
#include <graphics/xn_texture.hpp>
#include <map>

#include <atomic>
#include <list>
#include <queue>
#include <thread>

xn::sdl::WindowGL window;

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/trace.h>
#define USE_AUDIO_THREAD false

unsigned query_sample_rate_of_audiocontexts() {
  return EM_ASM_INT({
    var AudioContext = window.AudioContext || window.webkitAudioContext;
    var ctx = new AudioContext();
    var sr = ctx.sampleRate;
    ctx.close();
    return sr;
  });
}

static const std::string DEFAULT_SETTINGS_FILEPATH =
    "assets/config/nes_config_web.json";

#else
#define USE_AUDIO_THREAD true
static const std::string DEFAULT_SETTINGS_FILEPATH =
    "/home/pi/opengl/xnes-new/assets/config/nes_config.json";
#endif // __EMSCRIPTEN__

void ScrollWhenDraggingOnVoid(const ImVec2 &delta,
                              ImGuiMouseButton mouse_button) {
  static ImVec2 prevDelta = ImVec2(0, 0);
  ImGuiContext &g = *ImGui::GetCurrentContext();
  ImGuiWindow *window = g.CurrentWindow;
  bool hovered = false;
  bool held = false;
  ImGuiButtonFlags button_flags =
      (mouse_button == 0)   ? ImGuiButtonFlags_MouseButtonLeft
      : (mouse_button == 1) ? ImGuiButtonFlags_MouseButtonRight
                            : ImGuiButtonFlags_MouseButtonMiddle;
  if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as
                        // IsAnyItemHovered()!)
    ImGui::ButtonBehavior(window->Rect(),
                          window->GetID("##scrolldraggingoverlay"), &hovered,
                          &held, button_flags);
  if (held && delta.x != 0.0f && prevDelta.x != 0.0f)
    ImGui::SetScrollX(window->Scroll.x + delta.x);
  if (held && delta.y != 0.0f && prevDelta.y != 0.0f)
    ImGui::SetScrollY(window->Scroll.y + delta.y);
  prevDelta = delta;
}

struct SpriteSheet {
  xn::gl::Texture2D texture;
  glm::uvec2 gridDimensions;
  glm::uvec2 gridBoxSize;
  SpriteSheet() {}
  SpriteSheet(const std::string &filepath, uint32_t rows, uint32_t cols,
              uint32_t gl_tex_id = GL_TEXTURE0,
              glm::uvec2 clipArea = glm::uvec2(0, 0))
      : gridDimensions(rows, cols),
        texture(filepath.c_str(), gl_tex_id, false, GL_RGBA, GL_NEAREST) {
    if (clipArea.x == 0 && clipArea.y == 0)
      gridBoxSize = glm::uvec2(texture.width / rows, texture.height / cols);
    else
      gridBoxSize = glm::uvec2(clipArea.x / rows, clipArea.y / cols);
  }

  void getSpriteBounds(glm::uvec2 gridpos, glm::vec2 &uv0_out,
                       glm::vec2 &uv1_out) {
    glm::vec2 tex_d = glm::vec2(texture.width, texture.height);
    gridpos = gridpos % gridDimensions;
    uv0_out = (glm::vec2)(gridpos * gridBoxSize) / tex_d;
    uv1_out = (glm::vec2)((gridpos + glm::uvec2(1)) * gridBoxSize) / tex_d;
  }

  void drawSprite(uint32_t x, uint32_t y, float scale = 1.0) {
    glm::vec2 uv0, uv1;
    getSpriteBounds(glm::uvec2(x, y), uv0, uv1);
    ImVec2 imageExtents(gridBoxSize.x * scale, gridBoxSize.y * scale),
        im_uv0(uv0.x, uv0.y), im_uv1(uv1.x, uv1.y);
    ImGui::Image((ImTextureID)texture.id, imageExtents, im_uv0, im_uv1);
  }
};

// touch buttons for mobile devices
struct NesTouchButton {
  static SpriteSheet ControllerSprites;
  glm::vec2 cursorPos;
  glm::uvec2 spriteSheetPos;
  uint8_t nesInputBits;
  float scale;
  glm::vec2 bbMin;
  glm::vec2 bbMax;
  bool pressed = false;
  static const unsigned pressOffset = 8;

  NesTouchButton(uint8_t nesInputBits, glm::vec2 cursorPos,
                 glm::uvec2 spriteSheetPos, float scale)
      : nesInputBits(nesInputBits), cursorPos(cursorPos),
        spriteSheetPos(spriteSheetPos), scale(scale) {
    draw();
    ImGuiContext &g = *ImGui::GetCurrentContext();
    ImGuiWindow *window = g.CurrentWindow;
    bbMin = {ImGui::GetItemRectMin().x / window->Rect().GetWidth(),
             ImGui::GetItemRectMin().y / window->Rect().GetHeight()};
    bbMax = {ImGui::GetItemRectMax().x / window->Rect().GetWidth(),
             ImGui::GetItemRectMax().y / window->Rect().GetHeight()};
  }

  void draw() const {
    ImGui::SetCursorPosX(cursorPos.x);
    ImGui::SetCursorPosY(cursorPos.y + pressed * pressOffset);
    ControllerSprites.drawSprite(spriteSheetPos.x, spriteSheetPos.y, scale);
  }

  static uint8_t
  get_controller_byte(const std::vector<NesTouchButton> &touch_controller) {
    uint8_t nes_controller = 0;
    for (auto &button : touch_controller) {
      nes_controller |= button.nesInputBits * button.pressed;
    }
    return nes_controller;
  }

  static void update_controller_state(
      std::vector<NesTouchButton> &touch_controller,
      const std::map<SDL_FingerID, SDL_TouchFingerEvent> &touches) {
    for (auto &btn : touch_controller)
      btn.pressed = false;

    for (const auto &[k, v] : touches) {
      for (auto &btn : touch_controller)
        if (v.x >= btn.bbMin.x && v.x <= btn.bbMax.x && v.y >= btn.bbMin.y &&
            v.y <= btn.bbMax.y) {
          btn.pressed = true;
        }
    }
  }

  static void draw_controller(std::vector<NesTouchButton> &buttons,
                              uint32_t padding, glm::ivec2 dimensions) {
    static bool first = true;
    dimensions.x = ImGui::GetWindowContentRegionWidth();
    float buttonScale = dimensions.x / (ControllerSprites.gridBoxSize.x * 3.7);
    float specialButtonScale = buttonScale * 0.75;
    float ext = 3 * buttonScale * ControllerSprites.gridBoxSize.x / 5;
    float specialExt = specialButtonScale * ControllerSprites.gridBoxSize.x / 2;
    float center = dimensions.x / 2;
    float specialCenter = center - specialExt / 2;
    float y_offset = ext * 6 / 4;
    glm::vec2 arrow_center(ext * 0.75, y_offset);
    glm::vec2 ab_center(dimensions.x - ext * 2.2, y_offset);
    float arrow_spread = ext;
    if (first) {

      // select
      buttons.push_back(
          NesTouchButton(0x20, {specialCenter - specialExt, -specialExt / 2},
                         {4, 3}, specialButtonScale));
      // start
      buttons.push_back(NesTouchButton(
          0x10, {specialCenter + specialExt * 3 / 4, -specialExt / 2}, {4, 2},
          specialButtonScale));

      // left
      buttons.push_back(
          NesTouchButton(0x02, {arrow_center.x - arrow_spread, arrow_center.y},
                         {2, 3}, buttonScale));

      // up
      buttons.push_back(
          NesTouchButton(0x08, {arrow_center.x, arrow_center.y - arrow_spread},
                         {3, 3}, buttonScale));

      // down
      buttons.push_back(
          NesTouchButton(0x04, {arrow_center.x, arrow_center.y + arrow_spread},
                         {0, 3}, buttonScale));

      // right
      buttons.push_back(
          NesTouchButton(0x01, {arrow_center.x + arrow_spread, arrow_center.y},
                         {1, 3}, buttonScale));

      // b
      buttons.push_back(
          NesTouchButton(0x40, {ab_center.x, ab_center.y + arrow_spread},
                         {0, 2}, buttonScale));

      // a
      buttons.push_back(
          NesTouchButton(0x80, {ab_center.x + arrow_spread, ab_center.y},
                         {1, 2}, buttonScale));
      first = false;
    } else {
      for (auto &b : buttons) {
        b.draw();
      }
    }
  }
};
