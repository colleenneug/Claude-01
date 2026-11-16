#pragma once
// =============================================================================
//  playground/platform/Window.h — one OS window, as an RAII object.
//
//  NO GLFW IN THIS HEADER
//    GLFWwindow is only forward-declared. Code that includes Window.h cannot
//    call GLFW, which keeps input and windowing behind the platform layer —
//    the leak that makes cpp/ hard to port is that ten gameplay files call
//    GLFW directly.
//
//  NO GRAPHICS CONTEXT
//    The window is created with GLFW_NO_API: no OpenGL context. A Vulkan
//    swapchain needs a bare window, and a context created by default would
//    have to be torn down again before the surface could be made.
//
//  MOVABLE, AND WHY THAT TAKES CARE
//    GLFW callbacks find their C++ object through a user pointer. If that
//    pointer were `this`, moving the Window would leave GLFW pointing at the
//    moved-from object. The per-window state therefore lives in a heap block
//    that never moves, and only the pointer to it does.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <memory>
#include <string>
#include <string_view>

struct GLFWwindow;  // opaque here; defined by GLFW, used only in platform/src

namespace playground::platform {

class Platform;

// A size in pixels. Framebuffer pixels and window coordinates differ on
// high-DPI displays; the renderer always wants framebufferSize().
struct Extent2D {
  u32 width = 0;
  u32 height = 0;

  [[nodiscard]] constexpr bool isEmpty() const noexcept { return width == 0 || height == 0; }
  friend constexpr bool operator==(const Extent2D&, const Extent2D&) = default;
};

struct WindowDesc {
  std::string title = "Aedan's Playground";
  u32 width = 1600;   // window coordinates, not framebuffer pixels
  u32 height = 900;
  bool resizable = true;
};

class Window {
 public:
  // Taking Platform& proves GLFW is initialised. Fails with
  // Errc::InvalidArgument for an unusable size, or Errc::PlatformFailure with
  // GLFW's reason if the window could not be created.
  [[nodiscard]] static Result<Window> create(Platform& platform, const WindowDesc& desc);

  Window(Window&&) noexcept;
  Window& operator=(Window&&) noexcept;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window();

  // False only for a moved-from Window.
  [[nodiscard]] bool isOpen() const noexcept;

  // Set by the close button, Alt+F4, or requestClose().
  [[nodiscard]] bool shouldClose() const noexcept;
  void requestClose() noexcept;

  [[nodiscard]] Extent2D framebufferSize() const noexcept;

  // Minimised windows have a zero-sized framebuffer, and a Vulkan swapchain
  // cannot be zero-sized, so the application stops rendering while this is
  // true.
  [[nodiscard]] bool isMinimized() const noexcept;

  // True once after each change of framebuffer size, then false until the
  // next change. The application uses it to tell the renderer to rebuild
  // its swapchain.
  [[nodiscard]] bool consumeFramebufferResized() noexcept;

  void setTitle(const std::string& title) noexcept;

  // For the gpu layer's surface creation (glfwCreateWindowSurface). Nothing
  // outside the platform and gpu layers should need it.
  [[nodiscard]] GLFWwindow* nativeHandle() const noexcept;

 private:
  struct State;  // defined in Window.cpp
  explicit Window(std::unique_ptr<State> state) noexcept;
  std::unique_ptr<State> state_;
};

}  // namespace playground::platform
