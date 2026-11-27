#include "playground/platform/Window.h"

#include "playground/core/Assert.h"
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
  bool focused = true;
  InputCollector input;

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

// GLFW's key codes to the engine's. Anything not listed is Key::Unknown and
// ignored: the game only binds keys that have a name here.
Key translateKey(int glfwKey) noexcept {
  if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z) {
    return static_cast<Key>(static_cast<int>(Key::A) + (glfwKey - GLFW_KEY_A));
  }
  if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9) {
    return static_cast<Key>(static_cast<int>(Key::Num0) + (glfwKey - GLFW_KEY_0));
  }
  if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F12) {
    return static_cast<Key>(static_cast<int>(Key::F1) + (glfwKey - GLFW_KEY_F1));
  }
  switch (glfwKey) {
    case GLFW_KEY_SPACE:         return Key::Space;
    case GLFW_KEY_ENTER:         return Key::Enter;
    case GLFW_KEY_ESCAPE:        return Key::Escape;
    case GLFW_KEY_TAB:           return Key::Tab;
    case GLFW_KEY_BACKSPACE:     return Key::Backspace;
    case GLFW_KEY_LEFT:          return Key::Left;
    case GLFW_KEY_RIGHT:         return Key::Right;
    case GLFW_KEY_UP:            return Key::Up;
    case GLFW_KEY_DOWN:          return Key::Down;
    case GLFW_KEY_LEFT_SHIFT:    return Key::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT:   return Key::RightShift;
    case GLFW_KEY_LEFT_CONTROL:  return Key::LeftControl;
    case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
    case GLFW_KEY_LEFT_ALT:      return Key::LeftAlt;
    case GLFW_KEY_RIGHT_ALT:     return Key::RightAlt;
    default:                     return Key::Unknown;
  }
}

}  // namespace

// Every input callback finds the window's State through the user pointer
// (set just before this is called), never through the Window object, which
// may move.
void Window::registerInputCallbacks(GLFWwindow* handle) noexcept {
  glfwSetKeyCallback(handle, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* self = static_cast<State*>(glfwGetWindowUserPointer(w));
    if (self == nullptr || action == GLFW_REPEAT) return;  // a held key is just "down"
    self->input.keyEvent(translateKey(key), action == GLFW_PRESS);
  });

  glfwSetMouseButtonCallback(handle, [](GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = static_cast<State*>(glfwGetWindowUserPointer(w));
    if (self == nullptr) return;
    MouseButton which = MouseButton::Count;
    if (button == GLFW_MOUSE_BUTTON_LEFT) which = MouseButton::Left;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) which = MouseButton::Right;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) which = MouseButton::Middle;
    if (which != MouseButton::Count) self->input.mouseButtonEvent(which, action == GLFW_PRESS);
  });

  glfwSetCursorPosCallback(handle, [](GLFWwindow* w, double x, double y) {
    auto* self = static_cast<State*>(glfwGetWindowUserPointer(w));
    if (self != nullptr) self->input.cursorMoved(x, y);
  });

  // The focus-loss guard: keys held during Alt+Tab never get their "up".
  glfwSetScrollCallback(handle, [](GLFWwindow* w, double /*horizontal*/, double vertical) {
    auto* self = static_cast<State*>(glfwGetWindowUserPointer(w));
    if (self != nullptr) self->input.scrolled(vertical);
  });

  glfwSetWindowFocusCallback(handle, [](GLFWwindow* w, int focused) {
    auto* self = static_cast<State*>(glfwGetWindowUserPointer(w));
    if (self == nullptr) return;
    self->focused = (focused == GLFW_TRUE);
    if (!self->focused) {
      self->input.focusLost();
      log::debug(Category::Platform, "window lost focus: released all held keys and buttons");
    }
  });
}

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
  registerInputCallbacks(state->handle);
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

InputCollector& Window::input() noexcept {
  PG_VERIFY(state_ != nullptr, "input() on a moved-from Window");
  return state_->input;
}

bool Window::isFocused() const noexcept { return state_ != nullptr && state_->focused; }

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

Extent2D Window::windowSize() const noexcept {
  if (!isOpen()) return {};
  int width = 0;
  int height = 0;
  glfwGetWindowSize(state_->handle, &width, &height);
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

void Window::setSize(u32 width, u32 height) noexcept {
  if (isOpen() && width > 0 && height > 0) {
    glfwSetWindowSize(state_->handle, static_cast<int>(width), static_cast<int>(height));
  }
}

GLFWwindow* Window::nativeHandle() const noexcept { return state_ ? state_->handle : nullptr; }

}  // namespace playground::platform
