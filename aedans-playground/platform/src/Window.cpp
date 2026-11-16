#include "playground/platform/Window.h"

#include "playground/core/Log.h"
#include "playground/platform/Platform.h"

#include <GLFW/glfw3.h>

#include <utility>

namespace playground::platform {

using log::Category;

// The part of a Window that GLFW's callbacks point at. Heap-allocated so its
// address survives moves of the Window that owns it.
struct Window::State {
  GLFWwindow* handle = nullptr;
  bool framebufferResized = false;

  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() {
    if (handle != nullptr) glfwDestroyWindow(handle);
  }
};

namespace {

// Large enough for any real display; small enough to catch a negative number
// that was converted to unsigned somewhere upstream.
constexpr u32 kMaxWindowDimension = 16384;

Extent2D toExtent(int width, int height) noexcept {
  return {
      .width = width > 0 ? static_cast<u32>(width) : 0u,
      .height = height > 0 ? static_cast<u32>(height) : 0u,
  };
}

}  // namespace

Result<Window> Window::create(Platform& /*platform*/, const WindowDesc& desc) {
  if (desc.width == 0 || desc.height == 0 || desc.width > kMaxWindowDimension ||
      desc.height > kMaxWindowDimension) {
    return fail(Errc::InvalidArgument, "window size {}x{} is outside 1..{}", desc.width, desc.height,
                kMaxWindowDimension);
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan: no OpenGL context
  glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

  auto state = std::make_unique<State>();
  state->handle = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                                   desc.title.c_str(), nullptr, nullptr);
  if (state->handle == nullptr) {
    const char* description = nullptr;
    glfwGetError(&description);
    return fail(Errc::PlatformFailure, "could not create a {}x{} window: {}", desc.width, desc.height,
                description ? description : "unknown error");
  }

  glfwSetWindowUserPointer(state->handle, state.get());
  glfwSetFramebufferSizeCallback(state->handle, [](GLFWwindow* handle, int width, int height) {
    auto* self = static_cast<State*>(glfwGetWindowUserPointer(handle));
    if (self == nullptr) return;
    self->framebufferResized = true;
    log::debug(Category::Platform, "framebuffer resized to {}x{}", width, height);
  });

  Window window(std::move(state));
  const Extent2D framebuffer = window.framebufferSize();
  log::info(Category::Platform, "window \"{}\" created: {}x{} (framebuffer {}x{} pixels)", desc.title, desc.width,
            desc.height, framebuffer.width, framebuffer.height);
  return window;
}

Window::Window(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;
Window::~Window() = default;  // State's destructor destroys the GLFW window

bool Window::isOpen() const noexcept { return state_ != nullptr && state_->handle != nullptr; }

bool Window::shouldClose() const noexcept {
  return !isOpen() || glfwWindowShouldClose(state_->handle) == GLFW_TRUE;
}

void Window::requestClose() noexcept {
  if (isOpen()) glfwSetWindowShouldClose(state_->handle, GLFW_TRUE);
}

Extent2D Window::framebufferSize() const noexcept {
  if (!isOpen()) return {};
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(state_->handle, &width, &height);
  return toExtent(width, height);
}

bool Window::isMinimized() const noexcept {
  if (!isOpen()) return true;
  return glfwGetWindowAttrib(state_->handle, GLFW_ICONIFIED) == GLFW_TRUE || framebufferSize().isEmpty();
}

bool Window::consumeFramebufferResized() noexcept {
  if (!isOpen()) return false;
  return std::exchange(state_->framebufferResized, false);
}

void Window::setTitle(const std::string& title) noexcept {
  if (isOpen()) glfwSetWindowTitle(state_->handle, title.c_str());
}

GLFWwindow* Window::nativeHandle() const noexcept { return state_ ? state_->handle : nullptr; }

}  // namespace playground::platform
