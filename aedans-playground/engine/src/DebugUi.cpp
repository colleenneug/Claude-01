#include "playground/engine/DebugUi.h"

#include "playground/core/Log.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace playground::engine {
namespace {
using log::Category;
}

Result<std::unique_ptr<DebugUi>> DebugUi::create(platform::Window& window, render::Renderer& renderer,
                                                 bool startVisible) {
  if (ImGui::GetCurrentContext() != nullptr) {
    return fail(Errc::InvalidState, "debug UI: there is already an ImGui context (one DebugUi at a time)");
  }
  std::unique_ptr<DebugUi> ui(new DebugUi(renderer, startVisible));
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // no imgui.ini dropped in the working directory: the layout starts the same every run
  io.LogFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 4.0f;

  // GLFW's side chains to the window's own callbacks (installed first), so
  // the game's input keeps arriving as before.
  if (!ImGui_ImplGlfw_InitForVulkan(window.nativeHandle(), /*install_callbacks=*/true)) {
    ImGui::DestroyContext();
    return fail(Errc::PlatformFailure, "debug UI: ImGui's GLFW backend could not start");
  }
  ui->glfwReady_ = true;
  if (Status s = renderer.enableUi(); !s) {
    return tl::unexpected(std::move(s.error()));  // the destructor undoes the rest
  }
  ui->rendererReady_ = true;
  log::info(Category::Engine, "debug UI ready (Dear ImGui {}); F1 shows and hides it{}", IMGUI_VERSION,
            startVisible ? " (shown)" : "");
  return ui;
}

DebugUi::~DebugUi() {
  if (frameBegun_) ImGui::EndFrame();
  if (rendererReady_) renderer_.disableUi();  // the Vulkan side first: it uses the context
  if (glfwReady_) ImGui_ImplGlfw_Shutdown();
  if (ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
}

void DebugUi::beginFrame(platform::InputState& input) {
  frameReady_ = false;
  if (input.pressed(platform::Key::F1)) {
    visible_ = !visible_;
    log::info(Category::Engine, "debug UI {}", visible_ ? "shown" : "hidden");
  }
  if (!visible_) return;
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  frameBegun_ = true;
  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureMouse) input.dropMouse();
  // The keyboard only while typing in a field: a focused panel must not
  // stop WASD or Escape reaching the game.
  if (io.WantTextInput) input.dropKeys();
}

void DebugUi::endFrame() {
  if (!frameBegun_) return;
  for (const Panel& panel : panels_) panel();
  ImGui::Render();
  frameBegun_ = false;
  frameReady_ = true;
}

}  // namespace playground::engine
