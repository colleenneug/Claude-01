#pragma once
// =============================================================================
//  playground/platform/Platform.h — the windowing system's process lifetime.
//
//  GLFW has process-global state: glfwInit() must succeed before any window
//  exists, and glfwTerminate() must run after the last one is gone. This
//  class is that pair as RAII. Create it before any Window and destroy it
//  after the last one; Window::create() takes a Platform& so the order is
//  enforced by the type system rather than by a comment.
//
//  It also:
//    - routes every GLFW error into the log (category "platform"), so a
//      failure on a machine we have never seen explains itself;
//    - turns Ctrl+C / SIGTERM into a flag the main loop polls, so stopping
//      the program from a terminal still runs the clean shutdown path;
//    - answers "can this machine run Vulkan?" before the renderer exists.
//
//  At most one Platform may be alive at a time.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <memory>
#include <span>
#include <string_view>

namespace playground::platform {

class Platform {
 public:
  // Fails with Errc::AlreadyInitialized if another Platform is alive, or
  // Errc::PlatformFailure with GLFW's reason (e.g. no display) otherwise.
  [[nodiscard]] static Result<std::unique_ptr<Platform>> create();

  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;
  Platform(Platform&&) = delete;  // GLFW and the signal handlers are bound to
  Platform& operator=(Platform&&) = delete;  // this one object for its lifetime

  // Processes pending window and input events and returns at once.
  void pollEvents() noexcept;

  // Sleeps until an event arrives or the timeout passes, then processes
  // events. This is how an idle loop avoids spinning a CPU core at 100%.
  void waitEvents(f64 timeoutSeconds) noexcept;

  // True when a Vulkan loader and at least one driver were found.
  [[nodiscard]] bool vulkanSupported() const noexcept;

  // The instance extensions a Vulkan surface needs on this platform, e.g.
  // VK_KHR_surface and VK_KHR_xcb_surface. Empty when Vulkan is unsupported.
  // The strings are owned by GLFW and valid for this Platform's lifetime.
  [[nodiscard]] std::span<const char* const> requiredVulkanInstanceExtensions() const noexcept;

  // GLFW's version string, e.g. "3.3.10 X11 GLX EGL OSMesa".
  [[nodiscard]] std::string_view versionString() const noexcept;

  // Set by Ctrl+C (SIGINT) or SIGTERM. Never cleared once set.
  [[nodiscard]] static bool interruptRequested() noexcept;

 private:
  Platform() = default;

  using SignalHandler = void (*)(int);
  SignalHandler previousInterruptHandler_ = nullptr;
  SignalHandler previousTerminateHandler_ = nullptr;
};

}  // namespace playground::platform
