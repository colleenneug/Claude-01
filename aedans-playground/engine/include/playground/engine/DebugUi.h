#pragma once
// =============================================================================
//  playground/engine/DebugUi.h — the in-engine developer UI (Dear ImGui).
//
//  F1 shows and hides it. While it is shown, each frame:
//    beginFrame   (before the game sees input) ImGui's new frame, fed by its
//                 GLFW backend; if the UI is using the mouse (a window under
//                 it, a drag), or the keyboard because a text field is being
//                 typed in, the game gets that input dropped, so clicking a
//                 button does not also shoot (keys otherwise still reach the
//                 game: WASD and Escape work with a panel focused);
//    the panels   added with addPanel(), drawn between the game's update and
//                 its extract;
//    endFrame     ImGui::Render(); the renderer draws the result over the
//                 finished frame (FrameData::drawUi; render/src/UiOverlay).
//  Hidden, it costs nothing: no ImGui frame at all.
//
//  OWNERSHIP: this owns ImGui's context and its GLFW side; the renderer owns
//  the Vulkan side (Renderer::enableUi). create() makes the context, then
//  asks the renderer; the destructor asks the renderer to let go first, then
//  shuts down GLFW's side and the context. A renderer that cannot draw it
//  (or a window-less run: --server never makes one) means no DebugUi.
//
//  No ImGui header appears here: callers' panels include <imgui.h> themselves.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/platform/Input.h"
#include "playground/platform/Window.h"
#include "playground/render/Renderer.h"

#include <functional>
#include <memory>
#include <vector>

namespace playground::engine {

class DebugUi {
 public:
  [[nodiscard]] static Result<std::unique_ptr<DebugUi>> create(platform::Window& window, render::Renderer& renderer,
                                                               bool startVisible = false);
  ~DebugUi();
  DebugUi(const DebugUi&) = delete;
  DebugUi& operator=(const DebugUi&) = delete;

  // F1 toggles; when visible, starts ImGui's frame and drops from `input`
  // what the UI is using.
  void beginFrame(platform::InputState& input);
  // When a frame was begun: the panels, then ImGui::Render().
  void endFrame();
  // Whether this frame has UI to draw (begun and ended).
  [[nodiscard]] bool frameReady() const noexcept { return frameReady_; }

  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void setVisible(bool visible) noexcept { visible_ = visible; }

  using Panel = std::function<void()>;
  void addPanel(Panel panel) { panels_.push_back(std::move(panel)); }

 private:
  DebugUi(render::Renderer& renderer, bool visible) noexcept : renderer_(renderer), visible_(visible) {}

  render::Renderer& renderer_;
  std::vector<Panel> panels_;
  bool visible_ = false;
  bool frameBegun_ = false;
  bool frameReady_ = false;
  bool glfwReady_ = false;
  bool rendererReady_ = false;
};

}  // namespace playground::engine
